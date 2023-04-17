#include "include/grpcpp/stats_time.h"
#include "src/core/lib/rdma/rdma_sender_receiver.h"

grpc_core::TraceFlag grpc_rdma_sr_bp_trace(false, "rdma_sr_bp");
grpc_core::TraceFlag grpc_rdma_sr_bp_debug_trace(false, "rdma_sr_bp_debug");

RDMASenderReceiverBP::RDMASenderReceiverBP(int fd, bool server)
    : RDMASenderReceiver(
          new RDMAConn(fd, &RDMANode::GetInstance(), "data"),
          new RingBufferBP(RDMAConfig::GetInstance().get_ring_buffer_size()),
          new RDMAConn(fd, &RDMANode::GetInstance(), "metadata"), server),
      bytes_outstanding_send_(0) {
  auto pd = RDMANode::GetInstance().get_pd();

  if (local_ringbuf_mr_.RegisterLocal(pd, ringbuf_->get_buf(),
                                      ringbuf_->get_capacity())) {
    gpr_log(GPR_ERROR, "failed to RegisterLocal local_ringbuf_mr");
    abort();
  }
}

RDMASenderReceiverBP::~RDMASenderReceiverBP() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_rdma_sr_bp_debug_trace)) {
    debug_ = false;
    debug_thread_.join();
  }
  delete conn_data_;
  delete conn_metadata_;
  delete ringbuf_;
}

void RDMASenderReceiverBP::Init() {
  conn_data_->SyncMR(local_ringbuf_mr_, remote_ringbuf_mr_);
  conn_data_->SyncMR(local_metadata_recvbuf_mr_, remote_metadata_recvbuf_mr_);
  conn_data_->SyncQP();
  conn_metadata_->SyncQP();
  conn_data_->Sync();
  conn_metadata_->Sync();
  status_ = Status::kConnected;

  if (GRPC_TRACE_FLAG_ENABLED(grpc_rdma_sr_bp_debug_trace)) {
    debug_ = true;
    debug_thread_ = std::thread([this]() {
      char last_buffer[1024];
      char dump_path[255];
      char dump_tag[255];

      std::sprintf(last_buffer, "dbg_%c_%p", is_server() ? 'S' : 'C', this);
      pthread_setname_np(pthread_self(), last_buffer);

      std::sprintf(dump_path, "/tmp/rb_%c_%p", is_server() ? 'S' : 'C', this);
      std::sprintf(dump_tag, "/tmp/dump_%p", this);

      gpr_log(GPR_INFO,
              "Dbg, touch %s to dump, %c rdmasr: %p cap: %zu max send: %zu",
              dump_tag, is_server() ? 'S' : 'C', this, ringbuf_->get_capacity(),
              ringbuf_->get_max_send_size());

      while (debug_) {
        char buffer[1024];
        size_t remote_ringbuf_sz = remote_ringbuf_mr_.length();
        size_t used = (remote_ringbuf_sz + remote_ringbuf_tail_ -
                       get_remote_ringbuf_head()) %
                      remote_ringbuf_sz;
        std::sprintf(
            buffer,
            "%c rdmasr: %p head: %zu avail mlens: "
            "%zu garbage: %zu remote head: %zu remote tail: %zu pending "
            "send: "
            "%u used: %zu remain: %zu, tx: %zu rx: %zu tx_cnt: %d rx_cnt: "
            "%d",
            is_server() ? 'S' : 'C', this, ringbuf_->get_head(),
            dynamic_cast<RingBufferBP*>(ringbuf_)->CheckMessageLength(),
            ringbuf_->get_garbage(), get_remote_ringbuf_head(),
            remote_ringbuf_tail_, last_failed_send_size_.load(), used,
            remote_ringbuf_sz - 8 - used, total_sent_.load(),
            total_recv_.load(), write_counter_.load(), read_counter_.load());
        if (strcmp(last_buffer, buffer) != 0) {
          gpr_log(GPR_INFO, "%s", buffer);
          strcpy(last_buffer, buffer);
        }

        if (access(dump_tag, F_OK) != -1) {
          ringbuf_->Dump(dump_path);
          remove(dump_tag);
          gpr_log(GPR_INFO, "Ring buffer dumped to %s", dump_path);
        }
        sleep(1);
      }
    });
  }
}

bool RDMASenderReceiverBP::HasMessage() const {
  return dynamic_cast<RingBufferBP*>(ringbuf_)->CheckFirstMessageLength() > 0;
}

size_t RDMASenderReceiverBP::MarkMessageLength() {
  return unread_mlens_ =
             dynamic_cast<RingBufferBP*>(ringbuf_)->CheckMessageLength();
}

int RDMASenderReceiverBP::SendChunk(msghdr* msg, ssize_t* sz) {
  ContentAssertion cass(write_counter_);
  auto mhz = grpc_stats_time_get_cpu_mhz();
  size_t remote_ringbuf_sz = remote_ringbuf_mr_.length();
  size_t mlen = 0;

  if (sz != nullptr) {
    *sz = -1;
  }

  if (status_ != Status::kConnected) {
    return EPIPE;
  }

  for (int i = 0; i < msg->msg_iovlen; i++) {
    mlen += msg->msg_iov[i].iov_len;
  }

  size_t len = mlen + sizeof(size_t) + 1;

  if (mlen == 0 || mlen > ringbuf_->get_max_send_size()) {
    gpr_log(GPR_ERROR, "Invalid mlen, expected (0, %zu] actually size: %zu",
            ringbuf_->get_max_send_size(), mlen);
    return EINVAL;
  }

  if (GRPC_TRACE_FLAG_ENABLED(grpc_rdma_sr_bp_trace)) {
    gpr_log(GPR_INFO, "rdmasr: %p send, mlen: %zu", this, mlen);
  }

  if (!isWritable(mlen) ||
      bytes_outstanding_send_.load() + len >= sendbuf_sz_) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_rdma_sr_bp_trace)) {
      size_t used = (remote_ringbuf_sz + remote_ringbuf_tail_ -
                     get_remote_ringbuf_head()) %
                    remote_ringbuf_sz;
      gpr_log(GPR_INFO,
              "rdmasr: %p ring buffer is full, with mlen: %zu, used: %zu", this,
              mlen, used);
    }
    last_failed_send_size_ = mlen;
    return EAGAIN;
  }

  GlobalSendBufferManager& gsbm = GlobalSendBufferManager::GetInstance();
  struct ibv_sge sges[RDMA_MAX_WRITE_IOVEC];
  size_t sge_idx = 0;
  uint8_t* sendbuf_ptr = sendbuf_ + bytes_outstanding_send_.load();
  *reinterpret_cast<size_t*>(sendbuf_ptr) = mlen;
  init_sge(sges, sendbuf_ptr, sizeof(size_t), sendbuf_mr_.lkey());
  sendbuf_ptr += sizeof(size_t);

  cycles_t begin_cycles = get_cycles();
  size_t nwritten = 0;
  bool zerocopy = false;
  for (size_t iov_idx = 0; iov_idx < msg->msg_iovlen; iov_idx++) {
    uint8_t* iov_base = (uint8_t*)(msg->msg_iov[iov_idx].iov_base);
    size_t iov_len = msg->msg_iov[iov_idx].iov_len;
    uint32_t lkey = sendbuf_mr_.lkey();

    GlobalSendBuffer* gsb = gsbm.contains(iov_base);
    if (gsb != nullptr) {
      zerocopy = true;
      total_zerocopy_send_size += iov_len;
      uint8_t* buf = gsb->get_buf();
      uint8_t* end = buf + gsb->get_used();
      size_t remaining = end - iov_base - iov_len;
      if (buf != iov_base) {
        gsbm.remove_link(iov_base);
      }
      if (remaining > 0) {
        gsbm.add_link(iov_base + iov_len, buf);
      }
      lkey = gsb->get_mr().lkey();
      // printf("SendChunk find zerocopy = %lld\n", iov_len);
    } else {
      cycles_t begin_cycles = get_cycles();
      memcpy(sendbuf_ptr, iov_base, iov_len);
      cycles_t t_cycles = get_cycles() - begin_cycles;
      size_t mb_s = iov_len / (t_cycles / mhz);
      grpc_stats_time_add_custom(GRPC_STATS_TIME_ADHOC_2, mb_s);
      iov_base = sendbuf_ptr;
      sendbuf_ptr += iov_len;
      // printf("SendChunk no zerocopy = %lld\n", iov_len);
    }

    uint64_t iov_addr = (uint64_t)iov_base;

    if (sges[sge_idx].addr + sges[sge_idx].length == iov_addr) {
      sges[sge_idx].length += iov_len;
    } else {
      init_sge(&sges[++sge_idx], iov_base, iov_len, lkey);
    }

    nwritten += iov_len;
  }
  cycles_t t_cycles = get_cycles() - begin_cycles;
  if (nwritten > 1024) {
    grpc_stats_time_add_custom(GRPC_STATS_TIME_SEND_COPY_BW,
                               nwritten / (t_cycles / mhz));
  }
  // gpr_log(GPR_INFO, "SendChunk memcpy: Size: %zu, Time: %.2lf us, Speed: %lf
  // MB/s",
  //       iov_len, t_cycles / mhz_, iov_len / (t_cycles / mhz_));

  *sendbuf_ptr = 1;
  if (sges[sge_idx].addr + sges[sge_idx].length == (uint64_t)sendbuf_ptr) {
    sges[sge_idx].length += 1;
  } else {
    init_sge(&sges[++sge_idx], sendbuf_ptr, 1, sendbuf_mr_.lkey());
  }

  GPR_ASSERT(nwritten == mlen);
  int n_post_send;
  {
    GRPCProfiler profiler(GRPC_STATS_TIME_SEND_POST);
    if (zerocopy) {
      n_post_send = conn_data_->PostSendRequests(
          remote_ringbuf_mr_, remote_ringbuf_tail_, sges, sge_idx + 1, len,
          IBV_WR_RDMA_WRITE);
    } else {
      n_post_send = conn_data_->PostSendRequest(
          remote_ringbuf_mr_, remote_ringbuf_tail_, sendbuf_mr_,
          bytes_outstanding_send_.load(), len, IBV_WR_RDMA_WRITE);
    }
    n_outstanding_send_ += n_post_send;
  }
  bytes_outstanding_send_ += len;
  remote_ringbuf_tail_ = (remote_ringbuf_tail_ + len) % remote_ringbuf_sz;
  last_failed_send_size_ = 0;
  total_sent_ += len;
  if (sz != nullptr) {
    *sz = nwritten;
  }

  // printf("SendChunk, zerocopy = %d, len = %lld, sge_num = %d\n", zerocopy,
  // len, sge_idx + 1);

  return 0;
}

int RDMASenderReceiverBP::Send(msghdr* msg, ssize_t* sz) {
  ContentAssertion cass(write_counter_);
  size_t remote_ringbuf_sz = remote_ringbuf_mr_.length();
  size_t mlen = 0;

  if (sz != nullptr) {
    *sz = -1;
  }

  if (status_ != Status::kConnected) {
    return EPIPE;
  }

  for (int i = 0; i < msg->msg_iovlen; i++) {
    mlen += msg->msg_iov[i].iov_len;
  }

  if (mlen == 0 || mlen > ringbuf_->get_max_send_size()) {
    gpr_log(GPR_ERROR, "Invalid mlen, expected (0, %zu] actually size: %zu",
            ringbuf_->get_max_send_size(), mlen);
    return EINVAL;
  }

  if (GRPC_TRACE_FLAG_ENABLED(grpc_rdma_sr_bp_trace)) {
    gpr_log(GPR_INFO, "rdmasr: %p send, mlen: %zu", this, mlen);
  }

  if (PollLastSendCompletion() != 0) {
    return EPIPE;
  }

  if (!isWritable(mlen)) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_rdma_sr_bp_trace)) {
      size_t used = (remote_ringbuf_sz + remote_ringbuf_tail_ -
                     get_remote_ringbuf_head()) %
                    remote_ringbuf_sz;
      gpr_log(GPR_INFO,
              "rdmasr: %p ring buffer is full, with mlen: %zu, used: %zu", this,
              mlen, used);
    }
    last_failed_send_size_ = mlen;
    return EAGAIN;
  }

  bool zerocopy = false;
  struct ibv_sge sges[RDMA_MAX_WRITE_IOVEC];
  size_t sge_idx = 0;
  uint8_t* sendbuf_ptr = sendbuf_ + sizeof(size_t);
  size_t iov_idx = 0, nwritten = 0;

  *reinterpret_cast<size_t*>(sendbuf_) = mlen;
  init_sge(sges, sendbuf_, sizeof(size_t), sendbuf_mr_.lkey());

  while (iov_idx < msg->msg_iovlen && nwritten < mlen) {
    void* iov_base = msg->msg_iov[iov_idx].iov_base;
    size_t iov_len = msg->msg_iov[iov_idx].iov_len;

    if (ZerocopySendbufContains(iov_base) && false) {
      zerocopy = true;
      init_sge(&sges[++sge_idx], iov_base, iov_len,
               zerocopy_sendbuf_mr_.lkey());
      unfinished_zerocopy_send_size_.fetch_sub(iov_len);
      total_zerocopy_send_size += iov_len;
    } else {
      mt_memcpy(sendbuf_ptr, iov_base, iov_len);

      if (sges[sge_idx].lkey == sendbuf_mr_.lkey()) {  // last sge in sendbuf
        sges[sge_idx].length += iov_len;               // merge in last sge
      } else {  // last sge in zerocopy_sendbuf
        init_sge(&sges[++sge_idx], sendbuf_ptr, iov_len, sendbuf_mr_.lkey());
      }
      sendbuf_ptr += iov_len;
    }

    nwritten += iov_len;
    iov_idx++;
  }

  *sendbuf_ptr = 1;
  if (sges[sge_idx].lkey == sendbuf_mr_.lkey()) {
    sges[sge_idx].length += 1;
  } else {
    init_sge(&sges[++sge_idx], sendbuf_ptr, 1, sendbuf_mr_.lkey());
  }

  size_t len = mlen + sizeof(size_t) + 1;
  {
    GRPCProfiler profiler(GRPC_STATS_TIME_SEND_POST);
    int n_post_send;

    if (zerocopy) {
      n_post_send = conn_data_->PostSendRequests(
          remote_ringbuf_mr_, remote_ringbuf_tail_, sges, sge_idx + 1, len,
          IBV_WR_RDMA_WRITE);
    } else {
      n_post_send =
          conn_data_->PostSendRequest(remote_ringbuf_mr_, remote_ringbuf_tail_,
                                      sendbuf_mr_, 0, len, IBV_WR_RDMA_WRITE);
    }
    n_outstanding_send_ += n_post_send;
  }

  remote_ringbuf_tail_ = (remote_ringbuf_tail_ + len) % remote_ringbuf_sz;
  last_failed_send_size_ = 0;
  if (unfinished_zerocopy_send_size_ == 0) {
    last_zerocopy_send_finished_ = true;
  }
  total_sent_ += mlen;

  if (GRPC_TRACE_FLAG_ENABLED(grpc_rdma_sr_bp_debug_trace)) {
    total_sent_ += nwritten;
    printf("zerocopy send = %lld, total send = %lld, ratio = %.4lf\n",
           total_zerocopy_send_size, total_sent_.load(),
           double(total_zerocopy_send_size) / total_sent_.load());
  }
  if (sz != nullptr) {
    *sz = nwritten;
  }
  return 0;
}

int RDMASenderReceiverBP::Recv(msghdr* msg, ssize_t* sz) {
  ContentAssertion cass(read_content_conter_);
  size_t mlens =
      dynamic_cast<RingBufferBP*>(ringbuf_)->CheckFirstMessageLength();

  if (sz != nullptr) {
    *sz = -1;
  }

  if (status_ == Status::kNew || status_ == Status::kDisconnected) {
    return ENOTCONN;
  }

  if (mlens == 0) {
    if (sz != nullptr && status_ == Status::kShutdown) {
      *sz = 0;
    }
    return EAGAIN;
  }

  size_t read_mlens = mlens;
  size_t head = ringbuf_->get_head();
  cycles_t begin_cycles = get_cycles();
  bool should_recycle = ringbuf_->Read(msg, read_mlens);
  cycles_t t_cycles = get_cycles() - begin_cycles;
  size_t new_head = ringbuf_->get_head();

  // gpr_log(GPR_INFO, "ringbuf Read: Size: %zu, Time: %.2lf us, Speed: %lf
  // MB/s",
  //         read_mlens, t_cycles / mhz_, read_mlens / (t_cycles / mhz_));

  if (sz != nullptr) {
    if (read_mlens == 0) {
      *sz = -1;
      return EAGAIN;
    }
    *sz = read_mlens;
  }

  if (GRPC_TRACE_FLAG_ENABLED(grpc_rdma_sr_bp_debug_trace) ||
      GRPC_TRACE_FLAG_ENABLED(grpc_rdma_sr_bp_trace)) {
    read_counter_++;
    total_recv_ += read_mlens;
  }

  if (should_recycle && status_ != Status::kShutdown) {
    int r = updateRemoteMetadata();
    // N.B. IsPeerAlive calls read, should put it on the rhs to reduce overhead
    if (r != 0 && conn_metadata_->IsPeerAlive()) {
      return r;
    }
    if (GRPC_TRACE_FLAG_ENABLED(grpc_rdma_sr_bp_trace)) {
      gpr_log(GPR_INFO, "%c updateRemoteMetadata %d, head: %zu",
              is_server() ? 'S' : 'C', read_counter_.load(),
              reinterpret_cast<size_t*>(metadata_sendbuf_)[0]);
    }
  }

  if (GRPC_TRACE_FLAG_ENABLED(grpc_rdma_sr_bp_trace)) {
    gpr_log(GPR_INFO, "%c recv %d, pos: %zu->%zu, mlens: %zu, read: %zu",
            is_server() ? 'S' : 'C', read_counter_.load(), head, new_head,
            mlens, read_mlens);
  }

  return 0;
}

int RDMASenderReceiverBP::RecvEx(msghdr* msg, ssize_t* sz) {
  ContentAssertion cass(read_content_conter_);
  auto mhz = grpc_stats_time_get_cpu_mhz();
  size_t mlens =
      dynamic_cast<RingBufferBP*>(ringbuf_)->CheckFirstMessageLength();

  if (sz != nullptr) {
    *sz = -1;
  }

  if (status_ == Status::kNew || status_ == Status::kDisconnected) {
    return ENOTCONN;
  }

  if (mlens == 0) {
    if (sz != nullptr && status_ == Status::kShutdown) {
      *sz = 0;
      return ECONNRESET;
    }
    return EAGAIN;
  }

  size_t head = ringbuf_->get_head();
  cycles_t begin_cycles = get_cycles();
  bool should_recycle;
  size_t actual_read =
      dynamic_cast<RingBufferBP*>(ringbuf_)->Read(msg, should_recycle);
  cycles_t t_cycles = get_cycles() - begin_cycles;
  size_t new_head = ringbuf_->get_head();

  gpr_log(GPR_INFO,
          "Size: %zu, msg_iovlen: %zu, Time: %.2lf us, Read Speed: %lf MB/s",
          actual_read, msg->msg_iovlen, t_cycles / mhz,
          actual_read / (t_cycles / mhz));

  if (actual_read > 1024) {
    grpc_stats_time_add_custom(GRPC_STATS_TIME_RECV_COPY_BW,
                               actual_read / (t_cycles / mhz));
  }

  if (sz != nullptr) {
    *sz = actual_read;
  }

  if (GRPC_TRACE_FLAG_ENABLED(grpc_rdma_sr_bp_debug_trace) ||
      GRPC_TRACE_FLAG_ENABLED(grpc_rdma_sr_bp_trace)) {
    read_counter_++;
    total_recv_ += actual_read;
  }

  if (should_recycle && status_ != Status::kShutdown) {
    int r = updateRemoteMetadata();
    // N.B. IsPeerAlive calls read, should put it on the rhs to reduce overhead
    if (r != 0 && conn_metadata_->IsPeerAlive()) {
      return r;
    }
    if (GRPC_TRACE_FLAG_ENABLED(grpc_rdma_sr_bp_trace)) {
      gpr_log(GPR_INFO, "%c updateRemoteMetadata %d, head: %zu",
              is_server() ? 'S' : 'C', read_counter_.load(),
              reinterpret_cast<size_t*>(metadata_sendbuf_)[0]);
    }
  }

  if (GRPC_TRACE_FLAG_ENABLED(grpc_rdma_sr_bp_trace)) {
    gpr_log(GPR_INFO, "%c recv %d, pos: %zu->%zu, mlens: %zu, read: %zu",
            is_server() ? 'S' : 'C', read_counter_.load(), head, new_head,
            mlens, actual_read);
  }

  return 0;
}
