/**
 * Copyright (c) 2017-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */
#ifdef GRPC_USE_IBVERBS

#include <grpc/support/log.h>

#include "grpcpp/stats_time.h"

#include "src/core/lib/ibverbs/config.h"
#include "src/core/lib/ibverbs/pair.h"
#include "src/core/lib/ibverbs/ring_buffer.h"

namespace grpc_core {
namespace ibverbs {

PairPollable::PairPollable()
    : dev_(Device::Get()),
      read_content_(0),
      write_content_(0),
      status_(PairStatus::kUninitialized) {
  auto& config = Config::Get();

  cq_ =
      ibv_create_cq(dev_->context_, kCompletionQueueCapacity, this, nullptr, 0);
  int rv;
  // Create queue pair
  ibv_device_attr dev_attr;
  rv = ibv_query_device(dev_->context_, &dev_attr);
  GPR_ASSERT(rv == 0);
  max_sge_num_ = dev_attr.max_sge;

  {
    struct ibv_qp_init_attr attr;
    memset(&attr, 0, sizeof(struct ibv_qp_init_attr));
    attr.send_cq = cq_;
    attr.recv_cq = cq_;
    attr.cap.max_send_wr = PairPollable::kSendCompletionQueueCapacity;
    attr.cap.max_recv_wr = PairPollable::kRecvCompletionQueueCapacity;
    attr.cap.max_send_sge = max_sge_num_;
    attr.cap.max_recv_sge = 1;  // exchanging MR only needs 1 sge
    attr.qp_type = IBV_QPT_RC;
    attr.sq_sig_all = 0;
    qp_ = ibv_create_qp(dev_->pd_, &attr);
    GPR_ASSERT(qp_);

    //    ibv_query_qp_data_in_order(qp_, IBV_WR_RDMA_WRITE, )
  }

  // Populate local address.
  // The Packet Sequence Number field (PSN) is random which makes that
  // the remote end of this pair needs to have the contents of the
  // full address struct in order to connect, and vice versa.
  {
    auto port_num = config.get_port_num();
    auto gid_index = config.get_gid_index();
    struct ibv_port_attr attr;
    memset(&attr, 0, sizeof(struct ibv_port_attr));
    rv = ibv_query_port(dev_->context_, port_num, &attr);
    GPR_ASSERT(rv == 0);
    rv = ibv_query_gid(dev_->context_, port_num, gid_index,
                       &self_.addr_.ibv_gid);
    GPR_ASSERT(rv == 0);
    self_.addr_.lid = attr.lid;
    self_.addr_.qpn = qp_->qp_num;
    self_.addr_.psn = rand() & 0xffffff;
  }
  self_.addr_.tag = IBVERBS_PAIR_TAG_POLLABLE;

  grpc_error_handle err = grpc_wakeup_fd_init(&wakeup_fd_);
  GPR_ASSERT(err == GRPC_ERROR_NONE);
}

PairPollable::~PairPollable() {
  gpr_log(GPR_INFO, "Destroy Pair %p", this);
  IBVERBS_CHECK(error_, ibv_destroy_qp(qp_));
  IBVERBS_CHECK(error_, ibv_destroy_cq(cq_));
  grpc_wakeup_fd_destroy(&wakeup_fd_);
}

void PairPollable::Init() {
  struct ibv_qp_attr attr;

  if (status_ == PairStatus::kUninitialized || status_ == PairStatus::kError ||
      status_ == PairStatus::kDisconnected) {
    auto& config = Config::Get();
    // Init queue pair
    memset(&attr, 0, sizeof(struct ibv_qp_attr));
    attr.qp_state = IBV_QPS_INIT;
    attr.pkey_index = 0;
    attr.port_num = config.get_port_num();
    attr.qp_access_flags = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE;
    IBVERBS_CHECK(error_, ibv_modify_qp(qp_, &attr,
                                        IBV_QP_STATE | IBV_QP_PKEY_INDEX |
                                            IBV_QP_PORT | IBV_QP_ACCESS_FLAGS));
    // Clear queue, which can be nonempty if the last time connection fails
    mr_posted_recv_ = std::queue<std::unique_ptr<MemoryRegion>>();
    auto recv_buf_size = config.get_ring_buffer_size_kb() * 1024;
    auto zerocopy_buf_size = config.get_zerocopy_buffer_size_kb() * 1024;
    auto send_buf_size = recv_buf_size / 2;

    // used to check peer has the same size.
    self_.addr_.ring_buffer_size = recv_buf_size;

    initSendBuffer(kDataBuffer, send_buf_size);
    initRecvBuffer(kDataBuffer, recv_buf_size);
    initSendBuffer(kStatusBuffer, sizeof(status_report));
    initRecvBuffer(kStatusBuffer, sizeof(status_report));
    initSendBuffer(kZeroCopyBuffer, zerocopy_buf_size);

    auto& data_buf = recv_buffers_[kDataBuffer];

    ring_buf_ = RingBufferPollable(data_buf->data(), data_buf->size());
    ring_buf_.Init();

    zerocopy_buffer_tail_ = 0;

    error_.clear();
    last_qp_query_ts_ = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::system_clock::now().time_since_epoch())
                            .count();

    internal_read_size_ = 0;
    remote_tail_ = 0;
    partial_write_ = false;

    pending_write_num_status_ = 0;
    pending_write_num_data_ = 0;

    total_read_size_ = 0;
    total_write_size_ = 0;

    status_ = PairStatus::kInitialized;
    zerocopy_bytes_ = 0;
    copy_bytes_ = 0;
  }
}

bool PairPollable::Connect(const std::vector<char>& bytes) {
  if (status_ == PairStatus::kInitialized) {
    gpr_log(GPR_INFO, "Connecting Pair %p", this);
    peer_ = Address(bytes);

    GPR_ASSERT(peer_.addr_.tag == self_.addr_.tag);
    GPR_ASSERT(peer_.addr_.ring_buffer_size == self_.addr_.ring_buffer_size);

    initQPs();

    syncMemoryRegion(kDataBuffer);
    syncMemoryRegion(kStatusBuffer);

    if (error_.empty()) {
#ifndef NDEBUG
      debugging_ = true;
      monitor_thread_ = std::thread(&PairPollable::printStatus, this);
#else
      debugging_ = false;
#endif
      status_ = PairStatus::kConnected;
      return true;
    }
  }
  return false;
}

uint64_t PairPollable::Send(void* buf, uint64_t payload_size) {
  GRPCProfiler profiler(GRPC_STATS_TIME_PAIR_SEND);
  ContentAssertion cassert(write_content_);
  auto* send_buf = send_buffers_[kDataBuffer].get();
  const ibv_mr& peer = mr_peer_[kDataBuffer]->mr();
  auto recv_buf_size = GetWritableSize();
  auto send_buf_size = send_buf->size() - RingBufferPollable::reserved_space;
  auto free_size = std::min(recv_buf_size, send_buf_size);

  partial_write_ = payload_size > free_size;
  auto size = std::min(payload_size, free_size);

  if (size == 0 || status_ != PairStatus::kConnected) {
    return 0;
  }

  // Wait writing done to reuse send buffer
  waitDataWrites();

  auto encoded_size =
      RingBufferPollable::EncodeBuffer(send_buf->data(), buf, size);
  std::vector<ring_buffer_write_request> reqs;

  assert(encoded_size <= send_buf->size());
  remote_tail_ = ring_buf_.GetWriteRequests(encoded_size, remote_tail_, reqs);

  for (auto& req : reqs) {
    rdma_write_request w_req;

    w_req.id = WR_ID_DATA;
    w_req.addr = (uint64_t)send_buf->data() + req.src_offset;
    w_req.lkey = send_buf->get_mr()->lkey;
    w_req.remote_addr = (uint64_t)peer.addr + req.dst_offset;
    w_req.rkey = peer.rkey;
    w_req.size = req.size;
    postWrite(w_req);
  }

  total_write_size_ += size;
  return size;
}

uint64_t PairPollable::Send(iovec* iov, uint64_t iov_size) {
  GRPCProfiler profiler(GRPC_STATS_TIME_PAIR_SEND);
  ContentAssertion cassert(write_content_);
  auto* send_buf = send_buffers_[kDataBuffer].get();
  const ibv_mr& peer = mr_peer_[kDataBuffer]->mr();
  auto recv_buf_size = GetWritableSize();
  auto send_buf_size = send_buf->size() - RingBufferPollable::reserved_space;
  auto free_size = std::min(recv_buf_size, send_buf_size);
  uint64_t payload_size = 0;

  for (int i = 0; i < iov_size; i++) {
    payload_size += iov[i].iov_len;
  }

  partial_write_ = payload_size > free_size;

  auto size = std::min(payload_size, free_size);

  if (size == 0 || status_ != PairStatus::kConnected) {
    return 0;
  }

  // Wait writing done to reuse send buffer
  waitDataWrites();
  uint64_t encoded_payload_size;
  auto encoded_buffer_size = RingBufferPollable::EncodeBuffer(
      send_buf->data(), free_size, iov, iov_size, encoded_payload_size);

  if (encoded_payload_size > 0) {
    std::vector<ring_buffer_write_request> reqs;

    assert(encoded_buffer_size <= send_buf->size());
    remote_tail_ =
        ring_buf_.GetWriteRequests(encoded_buffer_size, remote_tail_, reqs);

    for (auto& req : reqs) {
      rdma_write_request w_req;

      w_req.id = WR_ID_DATA;
      w_req.addr = (uint64_t)send_buf->data() + req.src_offset;
      w_req.lkey = send_buf->get_mr()->lkey;
      w_req.remote_addr = (uint64_t)peer.addr + req.dst_offset;
      w_req.rkey = peer.rkey;
      w_req.size = req.size;
      postWrite(w_req);
    }

    total_write_size_ += size;
  }
  return encoded_payload_size;
}

uint64_t PairPollable::Recv(void* buf, uint64_t capacity) {
  GRPCProfiler profiler(GRPC_STATS_TIME_PAIR_RECV);
  if (status_ != PairStatus::kConnected) {
    return 0;
  }
  ContentAssertion cassert(read_content_);
  uint64_t internal_read_bytes;
  auto read_size = ring_buf_.Read(buf, capacity, &internal_read_bytes);

  internal_read_size_ += internal_read_bytes;
  total_read_size_ += read_size;

  if (internal_read_size_ >= ring_buf_.get_capacity() / 2) {
    auto* status_buf = send_buffers_[kStatusBuffer].get();
    auto* status = reinterpret_cast<status_report*>(status_buf->data());

    status->remote_head = ring_buf_.get_head();
    updateStatus();
    waitStatusWrites();
    internal_read_size_ = 0;
  }
  return read_size;
}

bool PairPollable::HasMessage() const { return ring_buf_.HasMessage(); }

uint64_t PairPollable::GetReadableSize() const {
  return status_ == PairStatus::kConnected ? ring_buf_.GetReadableSize() : 0;
}

uint64_t PairPollable::GetWritableSize() const {
  auto* status_buf = recv_buffers_[kStatusBuffer].get();
  auto* status = reinterpret_cast<status_report*>(status_buf->data());
  auto remote_head = status->remote_head;
  auto writable_size = ring_buf_.GetWritableSize(remote_head, remote_tail_);

  return writable_size;
}

bool PairPollable::HasPendingWrites() const { return partial_write_; }

uint8_t* PairPollable::AllocateSendBuffer(size_t size) {
  if (size == 0) {
    return nullptr;
  }
  Buffer* buf = send_buffers_[kZeroCopyBuffer].get();
  auto buf_size = buf->size();
  uint32_t tail;

  bool alloc_success;
  do {
    tail = zerocopy_buffer_tail_;
    if (tail != 0 || tail + size > buf_size) {
      return nullptr;
    }
    alloc_success =
        zerocopy_buffer_tail_.compare_exchange_strong(tail, tail + size);
  } while (!alloc_success);
  return buf->data() + tail;
}

void PairPollable::Disconnect() {
  if (status_ != PairStatus::kUninitialized &&
      status_ != PairStatus::kDisconnected) {
    gpr_log(GPR_INFO, "Disconnecting Pair %p", this);

    if (get_status() == PairStatus::kConnected) {
      waitDataWrites();  // Wait for pending writes
      auto* p_status = reinterpret_cast<status_report*>(
          send_buffers_[kStatusBuffer]->data());
      p_status->peer_exit = 1;  // notify peer im exiting
      updateStatus();
      waitStatusWrites();
    }

    closeQPs();

    if (debugging_) {
      debugging_ = false;
      monitor_thread_.join();
    }
    status_ = PairStatus::kDisconnected;
  }
}

PairStatus PairPollable::get_status() {
  if (status_ == PairStatus::kConnected) {
    auto* status_buf = recv_buffers_[kStatusBuffer].get();
    auto* status =
        reinterpret_cast<volatile status_report*>(status_buf->data());
    if (status->peer_exit == 1) {
      return PairStatus::kHalfClosed;
    }

    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::system_clock::now().time_since_epoch())
                   .count();
    if (now - last_qp_query_ts_ >= STATUS_CHECK_INTERVAL_MS) {
      struct ibv_qp_attr attr;
      struct ibv_qp_init_attr init_attr;

      if (ibv_query_qp(qp_, &attr, IBV_QP_STATE, &init_attr)) {
        return PairStatus::kDisconnected;
      }
      if (qp_->state != IBV_QPS_RTS) {
        return PairStatus::kHalfClosed;
      }
      last_qp_query_ts_ = now;
    }
  }
  return status_;
}

grpc_wakeup_fd* PairPollable::get_wakeup_fd() { return &wakeup_fd_; }

int PairPollable::pollCompletions() {
  std::array<struct ibv_wc, kCompletionQueueCapacity> wc;
  int n_completions = 0;

  // Invoke handler for every work completion.
  while (error_.empty()) {
    auto nwc = ibv_poll_cq(cq_, wc.size(), wc.data());
    GPR_ASSERT(nwc >= 0);
    // Handle work completions
    for (int i = 0; i < nwc; i++) {
      handleCompletion(&wc[i]);
      n_completions++;
    }

    // Break unless wc was filled
    if (nwc == 0 || nwc < wc.size()) {
      break;
    }
  }
  return n_completions;
}

void PairPollable::waitStatusWrites() {
  while (pending_write_num_status_ > 0 && error_.empty()) {
    pollCompletions();
  }
}

void PairPollable::waitDataWrites() {
  while (pending_write_num_data_ > 0 && error_.empty()) {
    pollCompletions();
  }
}

void PairPollable::initQPs() {
  struct ibv_qp_attr attr;
  int rv;
  auto& config = Config::Get();

  memset(&attr, 0, sizeof(attr));
  attr.qp_state = IBV_QPS_RTR;
  attr.path_mtu = IBV_MTU_1024;
  attr.dest_qp_num = peer_.addr_.qpn;
  attr.rq_psn = peer_.addr_.psn;
  attr.max_dest_rd_atomic = 1;
  attr.min_rnr_timer = 20;  // receiver not ready, 10.24 milliseconds delay
  attr.ah_attr.is_global = 0;
  attr.ah_attr.dlid = peer_.addr_.lid;
  attr.ah_attr.port_num = config.get_port_num();
  if (peer_.addr_.ibv_gid.global.interface_id) {
    attr.ah_attr.is_global = 1;
    attr.ah_attr.grh.hop_limit = 1;
    attr.ah_attr.grh.dgid = peer_.addr_.ibv_gid;
    attr.ah_attr.grh.sgid_index = config.get_gid_index();
  }

  // Move to Ready To Receive (RTR) state
  rv = ibv_modify_qp(qp_, &attr,
                     IBV_QP_STATE | IBV_QP_PATH_MTU | IBV_QP_DEST_QPN |
                         IBV_QP_RQ_PSN | IBV_QP_AV | IBV_QP_MAX_DEST_RD_ATOMIC |
                         IBV_QP_MIN_RNR_TIMER);
  GPR_ASSERT(rv == 0);

  memset(&attr, 0, sizeof(attr));
  attr.qp_state = IBV_QPS_RTS;
  attr.sq_psn = self_.addr_.psn;
  attr.ah_attr.is_global = 1;
  attr.timeout = 14;  // 0.0335 s
  attr.retry_cnt = 7;
  attr.rnr_retry = 7; /* infinite */
  attr.max_rd_atomic = 1;

  // Move to Ready To Send (RTS) state
  rv = ibv_modify_qp(qp_, &attr,
                     IBV_QP_STATE | IBV_QP_TIMEOUT | IBV_QP_RETRY_CNT |
                         IBV_QP_RNR_RETRY | IBV_QP_SQ_PSN |
                         IBV_QP_MAX_QP_RD_ATOMIC);
  GPR_ASSERT(rv == 0);
}

void PairPollable::closeQPs() {
  struct ibv_qp_attr qp_attr;
  int rv;

  qp_attr.qp_state = IBV_QPS_ERR;
  rv = ibv_modify_qp(qp_, &qp_attr, IBV_QP_STATE);
  GPR_ASSERT(rv == 0);

  qp_attr.qp_state = IBV_QPS_RESET;
  rv = ibv_modify_qp(qp_, &qp_attr, IBV_QP_STATE);
  GPR_ASSERT(rv == 0);
}

void PairPollable::syncMemoryRegion(int buffer_id) {
  auto* buffer = recv_buffers_[buffer_id].get();

  assert(buffer != nullptr);
  auto mr = std::make_unique<MemoryRegion>(dev_->pd_, buffer->get_mr());

  // Send memory region to peer
  sendMemoryRegion(buffer_id, mr.get());
  mr_pending_send_[buffer_id] = std::move(mr);  // keep reference

  // wait for memory region from peer
  while (error_.empty() && mr_peer_[buffer_id] == nullptr) {
    pollCompletions();
  }
}

void PairPollable::initSendBuffer(BufferType type, uint64_t size) {
  auto& buffer = send_buffers_[type];

  if (buffer == nullptr) {
    buffer = std::make_unique<Buffer>(dev_->pd_, size);
  }

  memset(buffer->data(), 0, buffer->size());
}

void PairPollable::initRecvBuffer(BufferType type, uint64_t size) {
  auto& buffer = recv_buffers_[type];

  if (buffer == nullptr) {
    buffer = std::make_unique<Buffer>(dev_->pd_, size);
  }

  memset(buffer->data(), 0, buffer->size());
  // creating each receiver buffer is also expected a buffer from peer
  auto mr = std::make_unique<MemoryRegion>(dev_->pd_);
  postReceiveMemoryRegion(mr.get());
  mr_posted_recv_.emplace(std::move(mr));
  mr_peer_[type] = nullptr;  // this will be filled when the mr received
}

void PairPollable::sendMemoryRegion(int buffer_id, MemoryRegion* mr) {
  struct ibv_sge list = mr->sge();
  struct ibv_send_wr wr;
  memset(&wr, 0, sizeof(wr));

  wr.wr_id = WR_ID_MR + buffer_id;
  wr.sg_list = &list;
  wr.num_sge = 1;
  wr.opcode = IBV_WR_SEND_WITH_IMM;
  wr.send_flags = IBV_SEND_SIGNALED;
  wr.imm_data = buffer_id;
  struct ibv_send_wr* bad_wr = nullptr;
  IBVERBS_CHECK(error_, ibv_post_send(qp_, &wr, &bad_wr));
}

void PairPollable::postReceiveMemoryRegion(MemoryRegion* mr) {
  struct ibv_sge list = mr->sge();
  struct ibv_recv_wr wr;
  memset(&wr, 0, sizeof(wr));
  wr.sg_list = &list;
  wr.num_sge = 1;

  // The work request is serialized and sent to the driver so it
  // doesn't need to be valid after the ibv_post_recv call.
  struct ibv_recv_wr* bad_wr = nullptr;
  IBVERBS_CHECK(error_, ibv_post_recv(qp_, &wr, &bad_wr));
}

void PairPollable::handleCompletion(ibv_wc* wc) {
  if (wc->status != IBV_WC_SUCCESS) {
    std::stringstream ss;
    ss << "poll completion has error, wr_id " << wc->wr_id << " opcode "
       << wc->opcode << " status " << wc->status;
    error_ = ss.str();
    status_ = PairStatus::kError;
#ifndef NDEBUG
    gpr_log(GPR_ERROR, "Pair %p, %s", this, error_.c_str());
#endif

    return;
  }

  switch (wc->opcode) {
    case IBV_WC_RDMA_WRITE: {
      if (wc->wr_id == WR_ID_DATA) {
        pending_write_num_data_--;
        GPR_ASSERT(pending_write_num_data_ >= 0);
      } else if (wc->wr_id == WR_ID_STATUS) {
        pending_write_num_status_--;
        GPR_ASSERT(pending_write_num_status_ >= 0);
      }
      break;
    }
    case IBV_WC_RECV: {
      // Memory region ready
      auto buffer_id = wc->imm_data;

      mr_peer_[buffer_id] = std::move(mr_posted_recv_.front());
      mr_posted_recv_.pop();
      break;
    }
    case IBV_WC_SEND: {
      // Memory region has been sent
      if (wc->wr_id >= WR_ID_MR) {
        auto buffer_id = wc->wr_id - WR_ID_MR;
        mr_pending_send_[buffer_id] = nullptr;
      }
      break;
    }
    default:
      GPR_ASSERT(false);
  }
}

void PairPollable::postWrite(int wr_id, struct ibv_sge* sg_list, int num_seg,
                             uint64_t remote_addr, uint32_t rkey) {
  struct ibv_send_wr wr;
  memset(&wr, 0, sizeof(wr));
  wr.wr_id = wr_id;
  wr.sg_list = sg_list;
  wr.num_sge = num_seg;
  wr.opcode = IBV_WR_RDMA_WRITE;
  wr.send_flags = IBV_SEND_SIGNALED;
  wr.wr.rdma.remote_addr = remote_addr;
  wr.wr.rdma.rkey = rkey;

  struct ibv_send_wr* bad_wr;

  switch (wr_id) {
    case WR_ID_DATA:
      pending_write_num_data_++;
      break;
    case WR_ID_STATUS:
      pending_write_num_status_++;
      break;
    default:
      assert(false);
  }
  IBVERBS_CHECK(error_, ibv_post_send(qp_, &wr, &bad_wr));
}
void PairPollable::postWrite(const rdma_write_request& req) {
  GPR_ASSERT(req.id != 0);
  GPR_ASSERT(req.size != 0);
  struct ibv_sge list;
  list.addr = req.addr;
  list.length = req.size;
  list.lkey = req.lkey;

  postWrite(req.id, &list, 1, req.remote_addr, req.rkey);
}

void PairPollable::updateStatus() {
  auto* status_buf = send_buffers_[kStatusBuffer].get();
  auto* status = reinterpret_cast<status_report*>(status_buf->data());

  status->remote_head = ring_buf_.get_head();

  rdma_write_request req;
  const ibv_mr& peer = mr_peer_[BufferType::kStatusBuffer]->mr();

  req.id = WR_ID_STATUS;
  req.addr = (uint64_t)status;
  req.lkey = status_buf->get_mr()->lkey;
  req.remote_addr = (uint64_t)peer.addr;
  req.rkey = peer.rkey;
  req.size = status_buf->size();

  postWrite(req);
}

const std::string& PairPollable::get_error() const { return error_; }
#if 1
uint64_t PairPollable::Send(grpc_slice* slices, size_t slice_count,
                            size_t byte_idx) {
  GRPCProfiler profiler(GRPC_STATS_TIME_PAIR_SEND);
  ContentAssertion cassert(write_content_);
  auto* send_buf = send_buffers_[kDataBuffer].get();
  auto remote_head = get_remote_head();
  auto remote_tail = remote_tail_;
  uint64_t send_buf_tail = 0;
  uint64_t total_slice_size = 0;
  uint64_t written_slice_size = 0;

  if (status_ != PairStatus::kConnected) {
    return 0;
  }

  for (int i = 0; i < slice_count; i++) {
    total_slice_size += GRPC_SLICE_LENGTH(slices[i]);
  }
  total_slice_size -= byte_idx;

  sg_list_.clear();

  // Wait writing done to reuse send buffer
  waitDataWrites();

  // Copy data to send buffer and create SGEs
  for (int i = 0; i < slice_count && sg_list_.size() < max_sge_num_; i++) {
    uint8_t* slice_ptr = GRPC_SLICE_START_PTR(slices[i]) + byte_idx;
    uint64_t slice_len = GRPC_SLICE_LENGTH(slices[i]) - byte_idx;
    byte_idx = 0;

    uint64_t recv_buf_free = ring_buf_.GetFreeSize(remote_head, remote_tail);
    uint64_t send_buf_free = send_buf->size() - send_buf_tail;
    auto payload_size = std::min(
        slice_len,
        std::min(RingBufferPollable::CalculateWritableSize(send_buf_free),
                 RingBufferPollable::CalculateWritableSize(recv_buf_free)));

    if (payload_size == 0) {
      break;
    }

    uint64_t encoded_size = RingBufferPollable::GetEncodedSize(payload_size);
    GPR_ASSERT(send_buf_tail + encoded_size <= send_buf->size());

    auto* next_ptr = RingBufferPollable::AppendHeader(
        send_buf->data() + send_buf_tail, payload_size);
    next_ptr =
        RingBufferPollable::AppendPayload(next_ptr, slice_ptr, payload_size);
    next_ptr = RingBufferPollable::AppendFooter(next_ptr);
    GPR_ASSERT((next_ptr - (send_buf->data() + send_buf_tail)) == encoded_size);

    ibv_sge sge;
    sge.addr = reinterpret_cast<uint64_t>(send_buf->data()) + send_buf_tail;
    sge.length = encoded_size;
    sge.lkey = send_buf->get_mr()->lkey;

    sg_list_.push_back(sge);
    written_slice_size += payload_size;
    send_buf_tail += encoded_size;
    remote_tail = ring_buf_.NextTail(remote_tail, encoded_size);
    GPR_ASSERT(send_buf_tail <= send_buf->size());
  }

  partial_write_ = written_slice_size < total_slice_size;

  if (!sg_list_.empty()) {
    std::array<ibv_send_wr, 2> wrs;
    const auto& recv_buf_mr = mr_peer_[kDataBuffer]->mr();

    remote_tail_ = ring_buf_.GetWriteRequests(remote_tail_, recv_buf_mr.addr,
                                              recv_buf_mr.rkey, sg_list_, wrs);

    wrs[0].wr_id = WR_ID_DATA;
    GPR_ASSERT(wrs[0].num_sge <= max_sge_num_);
    pending_write_num_data_++;
    // circular case
    if (wrs[0].next != nullptr) {
      wrs[1].wr_id = WR_ID_DATA;
      GPR_ASSERT(wrs[1].num_sge <= max_sge_num_);
      pending_write_num_data_++;
    }

    ibv_send_wr* bad_wr;
    IBVERBS_CHECK(error_, ibv_post_send(qp_, &wrs[0], &bad_wr));
  }

  total_write_size_ += written_slice_size;
  return written_slice_size;
}
#else
uint64_t PairPollable::Send(grpc_slice* slices, size_t slice_count,
                            size_t byte_idx) {
  GRPCProfiler profiler(GRPC_STATS_TIME_PAIR_SEND);
  ContentAssertion cassert(write_content_);
  auto* send_buf = send_buffers_[kDataBuffer].get();
  const ibv_mr& peer = mr_peer_[kDataBuffer]->mr();
  auto recv_buf_size = GetWritableSize();
  auto send_buf_size = send_buf->size() - RingBufferPollable::reserved_space;
  auto free_size = std::min(recv_buf_size, send_buf_size);
  uint64_t payload_size = 0;

  for (int i = 0; i < slice_count; i++) {
    payload_size += GRPC_SLICE_LENGTH(slices[i]);
  }

  payload_size -= byte_idx;  // offset of the first slice

  partial_write_ = payload_size > free_size;

  auto size = std::min(payload_size, free_size);

  if (size == 0 || status_ != PairStatus::kConnected) {
    return 0;
  }

  // Wait writing done to reuse send buffer
  waitDataWrites();
  uint64_t encoded_payload_size;
  auto encoded_buffer_size = RingBufferPollable::EncodeBuffer(
      send_buf->data(), free_size, slices, slice_count, byte_idx,
      encoded_payload_size);

  if (encoded_payload_size > 0) {
    std::vector<ring_buffer_write_request> reqs;

    assert(encoded_buffer_size <= send_buf->size());
    remote_tail_ =
        ring_buf_.GetWriteRequests(encoded_buffer_size, remote_tail_, reqs);

    for (auto& req : reqs) {
      rdma_write_request w_req;

      w_req.id = WR_ID_DATA;
      w_req.addr = (uint64_t)send_buf->data() + req.src_offset;
      w_req.lkey = send_buf->get_mr()->lkey;
      w_req.remote_addr = (uint64_t)peer.addr + req.dst_offset;
      w_req.rkey = peer.rkey;
      w_req.size = req.size;
      postWrite(w_req);
    }

    total_write_size_ += size;
  }
  return encoded_payload_size;
}
#endif

uint64_t PairPollable::SendZerocopy(grpc_slice* slices, size_t slice_count,
                                    size_t byte_idx) {
  GRPCProfiler profiler(GRPC_STATS_TIME_PAIR_SEND);
  ContentAssertion cassert(write_content_);
  auto* send_buf = send_buffers_[kDataBuffer].get();
  auto* zerocopy_buf = send_buffers_[kZeroCopyBuffer].get();
  auto remote_head = get_remote_head();
  auto remote_tail = remote_tail_;
  uint64_t send_buf_tail = 0;
  uint64_t total_slice_size = 0;
  uint64_t written_slice_size = 0;

  if (status_ != PairStatus::kConnected) {
    return 0;
  }

  for (int i = 0; i < slice_count; i++) {
    total_slice_size += GRPC_SLICE_LENGTH(slices[i]);
  }
  total_slice_size -= byte_idx;

  waitDataWrites();
  sg_list_.clear();

  for (int i = 0; i < slice_count && sg_list_.size() < max_sge_num_; i++) {
    uint8_t* slice_ptr = GRPC_SLICE_START_PTR(slices[i]) + byte_idx;
    uint64_t slice_len = GRPC_SLICE_LENGTH(slices[i]) - byte_idx;
    uint64_t recv_buf_free = ring_buf_.GetFreeSize(remote_head, remote_tail);
    uint64_t send_buf_free = send_buf->size() - send_buf_tail;

    byte_idx = 0;

    if (slice_ptr >= zerocopy_buf->data() &&
        slice_ptr + slice_len <= zerocopy_buf->data() + zerocopy_buf->size()) {
      auto payload_size = std::min(
          slice_len, RingBufferPollable::CalculateWritableSize(recv_buf_free));
      // need to store header, footer, padding
      if (payload_size == 0 ||
          send_buf_free < 3ul * RingBufferPollable::alignment ||
          sg_list_.size() + 4 > max_sge_num_) {
        break;
      }
      uint64_t encoded_size = RingBufferPollable::GetEncodedSize(payload_size);
      ibv_sge sge;
      // header
      sge.addr = reinterpret_cast<uint64_t>(send_buf->data()) + send_buf_tail;
      sge.length = sizeof(uint64_t);
      sge.lkey = send_buf->get_mr()->lkey;

      ring_buf_.AppendHeader(reinterpret_cast<uint8_t*>(sge.addr),
                             payload_size);

      send_buf_tail += sge.length;
      sg_list_.push_back(sge);

      // payload, buffer is pinned
      sge.addr = reinterpret_cast<uint64_t>(slice_ptr);
      sge.length = payload_size;
      sge.lkey = zerocopy_buf->get_mr()->lkey;

      sg_list_.push_back(sge);

      // padding
      auto padding_size =
          RingBufferPollable::round_up(payload_size) - payload_size;
      if (padding_size > 0) {
        sge.addr = reinterpret_cast<uint64_t>(send_buf->data()) + send_buf_tail;
        sge.length = padding_size;
        sge.lkey = send_buf->get_mr()->lkey;

        send_buf_tail += sge.length;
        sg_list_.push_back(sge);
      }

      // footer
      sge.addr = reinterpret_cast<uint64_t>(send_buf->data()) + send_buf_tail;
      sge.length = sizeof(uint64_t);
      sge.lkey = send_buf->get_mr()->lkey;

      ring_buf_.AppendFooter(reinterpret_cast<uint8_t*>(sge.addr));

      send_buf_tail += sge.length;
      sg_list_.push_back(sge);
      zerocopy_buffer_tail_ -= payload_size;
      written_slice_size += payload_size;
      remote_tail = ring_buf_.NextTail(remote_tail, encoded_size);
      GPR_ASSERT(send_buf_tail <= send_buf->size());
      zerocopy_bytes_ += payload_size;
    } else {
      auto payload_size = std::min(
          slice_len,
          std::min(RingBufferPollable::CalculateWritableSize(send_buf_free),
                   RingBufferPollable::CalculateWritableSize(recv_buf_free)));

      if (payload_size == 0) {
        break;
      }

      uint64_t encoded_size = RingBufferPollable::GetEncodedSize(payload_size);
      GPR_ASSERT(send_buf_tail + encoded_size <= send_buf->size());

      auto* next_ptr = RingBufferPollable::AppendHeader(
          send_buf->data() + send_buf_tail, payload_size);
      next_ptr =
          RingBufferPollable::AppendPayload(next_ptr, slice_ptr, payload_size);
      next_ptr = RingBufferPollable::AppendFooter(next_ptr);
      GPR_ASSERT((next_ptr - (send_buf->data() + send_buf_tail)) ==
                 encoded_size);

      ibv_sge sge;
      sge.addr = reinterpret_cast<uint64_t>(send_buf->data()) + send_buf_tail;
      sge.length = encoded_size;
      sge.lkey = send_buf->get_mr()->lkey;

      sg_list_.push_back(sge);
      written_slice_size += payload_size;
      send_buf_tail += encoded_size;
      remote_tail = ring_buf_.NextTail(remote_tail, encoded_size);
      GPR_ASSERT(send_buf_tail <= send_buf->size());
      copy_bytes_ += payload_size;
    }
  }

  partial_write_ = written_slice_size < total_slice_size;

  if (!sg_list_.empty()) {
    std::array<ibv_send_wr, 2> wrs;
    const auto& recv_buf_mr = mr_peer_[kDataBuffer]->mr();

    remote_tail_ = ring_buf_.GetWriteRequests(remote_tail_, recv_buf_mr.addr,
                                              recv_buf_mr.rkey, sg_list_, wrs);

    wrs[0].wr_id = WR_ID_DATA;
    GPR_ASSERT(wrs[0].num_sge <= max_sge_num_);
    pending_write_num_data_++;
    // circular case
    if (wrs[0].next != nullptr) {
      wrs[1].wr_id = WR_ID_DATA;
      GPR_ASSERT(wrs[1].num_sge <= max_sge_num_);
      pending_write_num_data_++;
    }

    ibv_send_wr* bad_wr;
    IBVERBS_CHECK(error_, ibv_post_send(qp_, &wrs[0], &bad_wr));
  }

  total_write_size_ += written_slice_size;
  return written_slice_size;
}

}  // namespace ibverbs
}  // namespace grpc_core
#endif
