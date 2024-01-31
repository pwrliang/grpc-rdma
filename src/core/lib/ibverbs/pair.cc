/**
 * Copyright (c) 2017-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */
#include "pair.h"

#include <grpc/support/log.h>

#include "src/core/lib/ibverbs/ring_buffer.h"

namespace grpc_core {
namespace ibverbs {

PairPollable::PairPollable(const std::shared_ptr<Device>& dev)
    : dev_(dev),
      read_content_(0),
      write_content_(0),
      status_(PairStatus::kUninitialized) {
  cq_ =
      ibv_create_cq(dev_->context_, kCompletionQueueCapacity, this, nullptr, 0);
  int rv;
  // Create queue pair
  {
    struct ibv_qp_init_attr attr;
    memset(&attr, 0, sizeof(struct ibv_qp_init_attr));
    attr.send_cq = cq_;
    attr.recv_cq = cq_;
    attr.cap.max_send_wr = PairPollable::kSendCompletionQueueCapacity;
    attr.cap.max_recv_wr = PairPollable::kRecvCompletionQueueCapacity;
    attr.cap.max_send_sge = 1;
    attr.cap.max_recv_sge = 1;
    attr.qp_type = IBV_QPT_RC;
    attr.sq_sig_all = 0;
    qp_ = ibv_create_qp(dev->pd_, &attr);
    GPR_ASSERT(qp_);

    //    ibv_query_qp_data_in_order(qp_, IBV_WR_RDMA_WRITE, )
  }

  // Populate local address.
  // The Packet Sequence Number field (PSN) is random which makes that
  // the remote end of this pair needs to have the contents of the
  // full address struct in order to connect, and vice versa.
  {
    struct ibv_port_attr attr;
    memset(&attr, 0, sizeof(struct ibv_port_attr));
    rv = ibv_query_port(dev_->context_, dev_->attr_.port, &attr);
    GPR_ASSERT(rv == 0);
    rv = ibv_query_gid(dev_->context_, dev_->attr_.port, dev_->attr_.index,
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
  gpr_log(GPR_INFO, "Destroy Pair");
  IBVERBS_CHECK(error_, ibv_destroy_qp(qp_));
  IBVERBS_CHECK(error_, ibv_destroy_cq(cq_));
  grpc_wakeup_fd_destroy(&wakeup_fd_);
}

void PairPollable::Init() {
  struct ibv_qp_attr attr;

  if (status_ == PairStatus::kUninitialized || status_ == PairStatus::kError) {
    // Init queue pair
    memset(&attr, 0, sizeof(struct ibv_qp_attr));
    attr.qp_state = IBV_QPS_INIT;
    attr.pkey_index = 0;
    attr.port_num = dev_->attr_.port;
    attr.qp_access_flags = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE;
    IBVERBS_CHECK(error_, ibv_modify_qp(qp_, &attr,
                                        IBV_QP_STATE | IBV_QP_PKEY_INDEX |
                                            IBV_QP_PORT | IBV_QP_ACCESS_FLAGS));

    initSendBuffer(kDataBuffer, kSendBufSize);
    initRecvBuffer(kDataBuffer, kRecvBufSize);
    initSendBuffer(kStatusBuffer, sizeof(status_report));
    initRecvBuffer(kStatusBuffer, sizeof(status_report));

    auto& data_buf = recv_buffers_[kDataBuffer];

    ring_buf_ = RingBufferPollable(data_buf->data(), data_buf->size());
    ring_buf_.Init();

    error_.clear();
    internal_read_size_ = 0;
    remote_tail_ = 0;
    remain_write_size_ = 0;

    pending_write_num_status_ = 0;
    pending_write_num_data_ = 0;

    total_read_size_ = 0;
    total_write_size_ = 0;

    status_ = PairStatus::kInitialized;
  }
}

void PairPollable::Connect(const std::vector<char>& bytes) {
  if (status_ == PairStatus::kInitialized) {
    gpr_log(GPR_INFO, "Connecting Pair %p", this);
    peer_ = Address(bytes);

    GPR_ASSERT(peer_.addr_.tag = self_.addr_.tag);

    initQPs();
    syncMemoryRegion(kDataBuffer);
    syncMemoryRegion(kStatusBuffer);
    assert(mr_posted_recv_.empty());

    debugging_ = true;
    //    monitor_thread_ = std::thread(&PairPollable::printStatus, this);
    status_ = PairStatus::kConnected;
  }
}

uint64_t PairPollable::Send(void* buf, uint64_t payload_size) {
  ContentAssertion cassert(write_content_);
  auto* send_buf = send_buffers_[kDataBuffer].get();
  const ibv_mr& peer = mr_peer_[kDataBuffer]->mr();
  auto recv_buf_size = GetWritableSize();
  auto send_buf_size = send_buf->size() - RingBufferPollable::reserved_space;
  auto free_size = std::min(recv_buf_size, send_buf_size);

  remain_write_size_ = payload_size > free_size ? payload_size - free_size : 0;
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

uint64_t PairPollable::Recv(void* buf, uint64_t capacity) {
  ContentAssertion cassert(read_content_);
  uint64_t internal_read_bytes;
  auto read_size = ring_buf_.Read(buf, capacity, &internal_read_bytes);

  internal_read_size_ += internal_read_bytes;
  total_read_size_ += read_size;

  if (internal_read_size_ >= kRecvBufSize / 2) {
    auto* status_buf = send_buffers_[kStatusBuffer].get();
    auto* status = reinterpret_cast<status_report*>(status_buf->data());

    status->remote_head = ring_buf_.get_head();
    updateStatus();
    waitStatusWrites();
    internal_read_size_ = 0;
  }
  return read_size;
}

uint64_t PairPollable::GetReadableSize() const {
  return status_ == PairStatus::kUninitialized ? 0
                                               : ring_buf_.GetReadableSize();
}

uint64_t PairPollable::GetWritableSize() const {
  auto* status_buf = recv_buffers_[kStatusBuffer].get();
  auto* status = reinterpret_cast<status_report*>(status_buf->data());
  auto remote_head = status->remote_head;
  auto writable_size = ring_buf_.GetWritableSize(remote_head, remote_tail_);

  return writable_size;
}

uint64_t PairPollable::GetRemainWriteSize() const {
  return remain_write_size_.load();
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
      //      monitor_thread_.join();
    }
    status_ = PairStatus::kDisconnected;
  }
}

PairStatus PairPollable::get_status() const {
  if (!error_.empty()) {
    return PairStatus::kError;
  } else if (status_ == PairStatus::kConnected) {
    auto* status_buf = recv_buffers_[kStatusBuffer].get();
    auto* status =
        reinterpret_cast<volatile status_report*>(status_buf->data());
    if (status->peer_exit == 1) {
      return PairStatus::kHalfClosed;
    }
  }
  return status_;
}

grpc_wakeup_fd* PairPollable::get_wakeup_fd() { return &wakeup_fd_; }

int PairPollable::pollCompletions() {
  std::array<struct ibv_wc, kCompletionQueueCapacity> wc;
  int n_completions = 0;

  // Invoke handler for every work completion.
  for (;;) {
    auto nwc = ibv_poll_cq(cq_, wc.size(), wc.data());
    GPR_ASSERT(nwc >= 0);
    // Handle work completions
    for (int i = 0; i < nwc && error_.empty(); i++) {
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

  memset(&attr, 0, sizeof(attr));
  attr.qp_state = IBV_QPS_RTR;
  attr.path_mtu = IBV_MTU_1024;
  attr.dest_qp_num = peer_.addr_.qpn;
  attr.rq_psn = peer_.addr_.psn;
  attr.max_dest_rd_atomic = 1;
  attr.min_rnr_timer = 20;  // receiver not ready, 10.24 milliseconds delay
  attr.ah_attr.is_global = 0;
  attr.ah_attr.dlid = peer_.addr_.lid;
  attr.ah_attr.port_num = dev_->attr_.port;
  if (peer_.addr_.ibv_gid.global.interface_id) {
    attr.ah_attr.is_global = 1;
    attr.ah_attr.grh.hop_limit = 1;
    attr.ah_attr.grh.dgid = peer_.addr_.ibv_gid;
    attr.ah_attr.grh.sgid_index = dev_->attr_.index;
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

  while (mr_pending_send_[buffer_id] != nullptr) {
    pollCompletions();
  }

  // wait for memory region from peer
  while (mr_peer_[buffer_id] == nullptr) {
    pollCompletions();
  }
}

void PairPollable::initSendBuffer(int buffer_id, uint64_t size) {
  auto& buffer = send_buffers_[buffer_id];

  if (buffer == nullptr) {
    buffer = std::make_unique<Buffer>(dev_->pd_, size);
  }

  memset(buffer->data(), 0, buffer->size());
}

void PairPollable::initRecvBuffer(int buffer_id, uint64_t size) {
  auto& buffer = recv_buffers_[buffer_id];

  if (buffer == nullptr) {
    buffer = std::make_unique<Buffer>(dev_->pd_, size);
  }

  memset(buffer->data(), 0, buffer->size());
  // creating each receiver buffer is also expected a buffer from peer
  auto mr = std::make_unique<MemoryRegion>(dev_->pd_);
  postReceiveMemoryRegion(mr.get());
  mr_posted_recv_.emplace(std::move(mr));
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
    ss << "completion has error, wr_id " << wc->wr_id << " opcode "
       << wc->opcode << " status " << wc->status;
    error_ = ss.str();
    gpr_log(GPR_ERROR, "%s", error_.c_str());
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

void PairPollable::postWrite(const rdma_write_request& req) {
  GPR_ASSERT(req.id != 0);
  GPR_ASSERT(req.size != 0);
  struct ibv_sge list;
  list.addr = req.addr;
  list.length = req.size;
  list.lkey = req.lkey;

  struct ibv_send_wr wr;
  memset(&wr, 0, sizeof(wr));
  wr.wr_id = req.id;
  wr.sg_list = &list;
  wr.num_sge = 1;
  wr.opcode = IBV_WR_RDMA_WRITE;
  wr.send_flags = IBV_SEND_SIGNALED;

  wr.wr.rdma.remote_addr = req.remote_addr;
  wr.wr.rdma.rkey = req.rkey;

  struct ibv_send_wr* bad_wr;

  switch (req.id) {
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
}  // namespace ibverbs
}  // namespace grpc_core
