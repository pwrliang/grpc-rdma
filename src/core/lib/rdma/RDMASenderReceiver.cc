#include "RDMASenderReceiver.h"
#include <chrono>

// -----< RDMASenderReceiver >-----

RDMASenderReceiver::RDMASenderReceiver(RDMANode* node)
    : node_(node),
      ringbuf_sz_(DEFAULT_RINGBUF_SZ),
      remote_ringbuf_head_(0),
      remote_ringbuf_tail_(0),
      sendbuf_sz_(DEFAULT_SENDBUF_SZ),
      sendbuf_offset_sz_(DEFAULT_SENDBUF_OFFSET_SZ) {
  ringbuf_ = new RingBuffer(ringbuf_sz_);
  if (!ringbuf_) {
    rdma_log(
        RDMA_ERROR,
        "RDMASenderReceiver::RDMASenderReceiver, failed to initialize ringbuf");
    exit(-1);
  }

  if (node == nullptr) {
    node_ = new RDMANode();
    node_->open("mlx5_0");
    external_node_flag_ = false;
  }

  ibv_pd* pd = node_->get_pd();

  if (local_ringbuf_mr_.local_reg(pd, ringbuf_->get_buf(), ringbuf_sz_)) {
    rdma_log(RDMA_ERROR,
             "RDMASenderReceiver::RDMASenderReceiver, failed to local_reg "
             "local_ringbuf_mr");
    exit(-1);
  }

  GPR_ASSERT(posix_memalign(&headbuf_, 64, DEFAULT_HEAD_SZ) == 0);
  memset(headbuf_, 0, DEFAULT_HEAD_SZ);
  if (local_headbuf_mr_.local_reg(pd, headbuf_, DEFAULT_HEAD_SZ)) {
    rdma_log(RDMA_ERROR,
             "RDMASenderReceiver::RDMASenderReceiver, failed to local_reg "
             "local_headbuf_mr");
    exit(-1);
  }

  sendbuf_ = new char[sendbuf_sz_];
  if (sendbuf_mr_.local_reg(pd, sendbuf_, sendbuf_sz_)) {
    rdma_log(RDMA_ERROR,
             "RDMASenderReceiver::RDMASenderReceiver, failed to local_reg "
             "sendbuf_mr");
    exit(-1);
  }
  sendbuf_0cp_dataptr_ = sendbuf_ + sendbuf_offset_sz_;

  GPR_ASSERT(posix_memalign(&sendbuf_head_, 64, DEFAULT_HEAD_SZ) == 0);
  memset(sendbuf_head_, 0, DEFAULT_HEAD_SZ);
  if (sendbuf_head_mr_.local_reg(pd, sendbuf_head_, DEFAULT_HEAD_SZ)) {
    rdma_log(RDMA_ERROR,
             "RDMASenderReceiver::RDMASenderReceiver, failed to local_reg "
             "sendbuf_head_mr");
    exit(-1);
  }

  opcode_ = IBV_WR_RDMA_WRITE;
}

RDMASenderReceiver::~RDMASenderReceiver() {
  if (conn_) delete (conn_);
  if (ringbuf_) delete (ringbuf_);
  free(headbuf_);
  if (sendbuf_) delete (sendbuf_);
  free(sendbuf_head_);
  if (node_) {
    if (external_node_flag_ == false)
      delete node_;
    else
      node_ = nullptr;
  }
}

void RDMASenderReceiver::init_conn(int sd) {
  fd_ = sd;
  ringbuf_->fd_ = sd;
  conn_ = new RDMAConn(sd, node_);
  conn_->init();
  conn_->sync_mr(local_ringbuf_mr_, remote_ringbuf_mr_);
  conn_->sync_mr(local_headbuf_mr_, remote_headbuf_mr_);
}

int RDMASenderReceiver::update_remote_head() {
  size_t local_ringbuf_head = ringbuf_->get_head() - ringbuf_->get_buf();

  reinterpret_cast<size_t*>(sendbuf_head_)[0] = remote_version_;
  reinterpret_cast<size_t*>(sendbuf_head_)[1] = local_ringbuf_head;
  reinterpret_cast<size_t*>(sendbuf_head_)[2] = remote_version_;

  size_t doff = 0;
  int ret = conn_->rdma_write(remote_headbuf_mr_, doff, sendbuf_head_mr_, 0,
                              DEFAULT_HEAD_SZ, IBV_WR_RDMA_WRITE);
  if (ret) {
    rdma_log(RDMA_ERROR,
             "fd = %d, RDMASenderReceiver::update_remote_head, error", fd_);
  }

  remote_version_++;
  return ret;
}

void RDMASenderReceiver::update_local_head() {
  size_t v1, v2, head;

  do {
    v1 = reinterpret_cast<size_t*>(headbuf_)[0];
    head = reinterpret_cast<size_t*>(headbuf_)[1];
    v2 = reinterpret_cast<size_t*>(headbuf_)[2];
  } while (v1 != v2 || v1 == local_version_);

  remote_ringbuf_head_ = head;
  local_version_ = v1;
}

bool RDMASenderReceiver::check_incoming(bool flag) {
  return ringbuf_->check_mlen() > 0;
}

int RDMASenderReceiver::get_mlen() { return ringbuf_->check_mlen(); }

int RDMASenderReceiver::send_msg(const char* msg, size_t mlen) {
  size_t remote_ringbuf_sz = remote_ringbuf_mr_.length();
  size_t len = mlen + sizeof(msglen_t) + sizeof(tailtag_t);
  size_t free =
      remote_ringbuf_head_ > remote_ringbuf_tail_
          ? remote_ringbuf_head_ - remote_ringbuf_tail_
          : remote_ringbuf_sz - (remote_ringbuf_tail_ - remote_ringbuf_head_);

  if (len > free) {
    update_local_head();
    return 1;
  }

  *(msglen_t*)sendbuf_ = mlen;
  memcpy(sendbuf_ + sizeof(msglen_t), msg, mlen);
  sendbuf_[len - 1] = 1;

  size_t old_remote = remote_ringbuf_tail_;
  if (conn_->rdma_write(remote_ringbuf_mr_, remote_ringbuf_tail_, sendbuf_mr_,
                        0, len, opcode_))
    return 1;

  return 0;
}

int RDMASenderReceiver::send_msghdr(const msghdr* msg, size_t mlen) {
  size_t remote_ringbuf_sz = remote_ringbuf_mr_.length();
  size_t len = mlen + sizeof(msglen_t) + sizeof(tailtag_t);
  size_t free =
      remote_ringbuf_head_ > remote_ringbuf_tail_
          ? remote_ringbuf_head_ - remote_ringbuf_tail_
          : remote_ringbuf_sz - (remote_ringbuf_tail_ - remote_ringbuf_head_);

  if (len > free) {
    update_local_head();
    return 1;
  }

  *(msglen_t*)sendbuf_ = mlen;
  char* start = sendbuf_ + sizeof(msglen_t);
  for (size_t iov_idx = 0, nwritten = 0;
       iov_idx < msg->msg_iovlen && nwritten < mlen; iov_idx++) {
    char* iov_base = (char*)msg->msg_iov[iov_idx].iov_base;
    size_t iov_len = msg->msg_iov[iov_idx].iov_len;
    iov_len = MIN(iov_len, mlen - nwritten);
    memcpy(start, iov_base, iov_len);
    start += iov_len;
    nwritten += iov_len;
  }
  *start = 1;

  size_t old_remote = remote_ringbuf_tail_;
  if (conn_->rdma_write(remote_ringbuf_mr_, remote_ringbuf_tail_, sendbuf_mr_,
                        0, len, opcode_))
    return 1;
  // printf("send %d bytes to remote %d, new remote %d\n",len, old_remote,
  // remote_ringbuf_tail_);

  return 0;
}

int RDMASenderReceiver::send_msghdr_zerocopy(const msghdr* msg, size_t mlen) {
  size_t remote_ringbuf_sz = remote_ringbuf_mr_.length();
  size_t len = mlen + sizeof(msglen_t) + sizeof(tailtag_t);
  size_t free =
      remote_ringbuf_head_ > remote_ringbuf_tail_
          ? remote_ringbuf_head_ - remote_ringbuf_tail_
          : remote_ringbuf_sz - (remote_ringbuf_tail_ - remote_ringbuf_head_);

  if (len > free) {
    update_local_head();
    return 1;
  }

  size_t old_remote = remote_ringbuf_tail_;

  // locate data_idx
  char* start = sendbuf_ + sizeof(msglen_t);
  char* end = start;
  int data_idx = -1, data_len = 0;
  for (int i = 0; i < msg->msg_iovlen; i++) {
    void* bytes = msg->msg_iov[i].iov_base;
    if (zerocopy_contains((char*)bytes)) {
      data_idx = i;
      data_len = msg->msg_iov[i].iov_len;
      start = (char*)msg->msg_iov[i].iov_base;
      end = start + data_len;
      // printf("contains, %d, %d, %p, %p, %p, %p\n", data_idx, data_len, start,
      // bytes, _sendbuf_mempool->_bigbuf_root, sendbuf_); for (char* s = start;
      // s <= end; s++) printf("%d,", *start); printf("\n");
      break;
    }
  }

  // left
  for (int i = data_idx - 1; i >= 0; i--) {
    start -= msg->msg_iov[i].iov_len;
    memcpy(start, msg->msg_iov[i].iov_base, msg->msg_iov[i].iov_len);
    // printf("memcpy, %d, %p, %p, %d\n", i, start, msg->msg_iov[i].iov_base,
    // msg->msg_iov[i].iov_len);
  }
  start -= sizeof(msglen_t);
  *(msglen_t*)start = mlen;
  int start_offset = start - sendbuf_;

  // right
  for (int i = data_idx + 1; i < msg->msg_iovlen; i++) {
    memcpy(end, msg->msg_iov[i].iov_base, msg->msg_iov[i].iov_len);
    // printf("memcpy, %d, %p, %p, %d\n", i, end, msg->msg_iov[i].iov_base,
    // msg->msg_iov[i].iov_len);
    end += msg->msg_iov[i].iov_len;
  }
  *end = 1;

  if (conn_->rdma_write(remote_ringbuf_mr_, remote_ringbuf_tail_, sendbuf_mr_,
                        start_offset, len, opcode_))
    return 1;

  return 0;
}

int RDMASenderReceiver::recv_msg(char* msg) {
  int rc = ringbuf_->get_msg(msg);
  if (rc) {
    ringbuf_->reset_buf_update_head();
    update_remote_head();
  }
  return rc;
}

int RDMASenderReceiver::recv_msghdr(msghdr* msg) {
  int rc = ringbuf_->get_msghdr(msg);
  if (rc) {
    if (ringbuf_->reset_buf_update_head() > 0) {
      update_remote_head();
    }
  }
  return rc;
}

int RDMASenderReceiver::recv_msghdr_zerocopy(msghdr* msg) {
  int rc = ringbuf_->get_msghdr_zerocopy(msg);
  if (rc) {
    zerocopy_last_head_ = ringbuf_->update_head();
  }
  return rc;
}

void RDMASenderReceiver::zerocopy_reset_buf_update_remote_head() {
  ringbuf_->reset_buf_from(zerocopy_last_head_);
  update_remote_head();
}

bool RDMASenderReceiver::zerocopy_contains(char* bytes) {
  if (bytes >= sendbuf_ && bytes < (sendbuf_ + sendbuf_sz_)) return true;
  return false;
}

// -----< RDMAEventSenderReceiver >-----

int RDMAEventSenderReceiver::_num_epfds = 0;
int* RDMAEventSenderReceiver::_epfds = nullptr;

void RDMAEventSenderReceiver::create_epfds(int num) {
  // RDMAEventSenderReceiver::_epfd = epoll_create1(0);
  RDMAEventSenderReceiver::_num_epfds = num;
  RDMAEventSenderReceiver::_epfds = new int[num];
  for (int i = 0; i < num; i++) {
    RDMAEventSenderReceiver::_epfds[i] = epoll_create1(0);
  }
}

RDMAEventSenderReceiver::RDMAEventSenderReceiver(int epfd, RDMANode* node)
    : RDMASenderReceiver(node), _epfd(epfd) {
  opcode_ = IBV_WR_RDMA_WRITE_WITH_IMM;
  if (external_node_flag_ == false) node_->event_channel_init();
  // printf("create event rdmasr with epfd = %d\n", epfd);
}

void RDMAEventSenderReceiver::init_conn(int sd) {
  fd_ = sd;
  ringbuf_->fd_ = sd;
  conn_ = new RDMAConn(sd, node_);
  conn_->event_init(_epfd, this);
  conn_->sync_mr(local_ringbuf_mr_, remote_ringbuf_mr_);
  conn_->sync_mr(local_headbuf_mr_, remote_headbuf_mr_);
  for (int i = 0; i < 1; i++) {
    event_post_recv();
  }
  char tmp;
  if (conn_->sync_data((char*)"s", &tmp, 1)) {
    rdma_log(RDMA_ERROR,
             "RDMAEventSenderReceiver::init_conn, failed to sync after "
             "switching QP to RTS");
  }
}

int RDMAEventSenderReceiver::event_post_recv() {
  return conn_->event_post_recv(ringbuf_->get_buf(), ringbuf_->get_buf_size(),
                                local_ringbuf_mr_.lkey());
}

bool RDMAEventSenderReceiver::check_incoming(bool flag) {
  int ret = conn_->event_check_incoming(_epfd, flag);
  if (ret == -1) {
    return false;
  }
  return true;
}

// get_mlen should be called after check_incoming
int RDMAEventSenderReceiver::get_mlen() {
  if (conn_->_rcq_unprocessed_event_count.load(std::memory_order_relaxed) ==
      0) {
    return 0;
  }
  return RDMASenderReceiver::get_mlen();
}

int RDMAEventSenderReceiver::send_msg(const char* msg, size_t mlen) {
  size_t remote_ringbuf_sz = remote_ringbuf_mr_.length();
  size_t len = mlen + sizeof(msglen_t) + sizeof(tailtag_t);
  size_t free =
      remote_ringbuf_head_ > remote_ringbuf_tail_
          ? remote_ringbuf_head_ - remote_ringbuf_tail_
          : remote_ringbuf_sz - (remote_ringbuf_tail_ - remote_ringbuf_head_);

  if (len > free) {
    update_local_head();
    return 1;
  }

  *(msglen_t*)sendbuf_ = mlen;
  memcpy(sendbuf_ + sizeof(msglen_t), msg, mlen);
  sendbuf_[len - 1] = 1;

  size_t old_remote = remote_ringbuf_tail_;
  if (conn_->rdma_write_event(remote_ringbuf_mr_, remote_ringbuf_tail_,
                              sendbuf_mr_, 0, len, IBV_WR_RDMA_WRITE, opcode_,
                              mlen)) {
    rdma_log(RDMA_ERROR,
             "%p, fd = %d, RDMAEventSenderReceiver::send_msg, failed in "
             "rdma_write_event",
             this, fd_);
    return 1;
  }
  return 0;
}

int RDMAEventSenderReceiver::send_msghdr(const msghdr* msg, size_t mlen) {
  size_t remote_ringbuf_sz = remote_ringbuf_mr_.length();
  size_t len = mlen + sizeof(msglen_t) + sizeof(tailtag_t);
  size_t free =
      remote_ringbuf_head_ > remote_ringbuf_tail_
          ? remote_ringbuf_head_ - remote_ringbuf_tail_
          : remote_ringbuf_sz - (remote_ringbuf_tail_ - remote_ringbuf_head_);

  if (len > free) {
    update_local_head();
    return 1;
  }

  *(msglen_t*)sendbuf_ = mlen;
  char* start = sendbuf_ + sizeof(msglen_t);
  for (size_t iov_idx = 0, nwritten = 0;
       iov_idx < msg->msg_iovlen && nwritten < mlen; iov_idx++) {
    char* iov_base = (char*)msg->msg_iov[iov_idx].iov_base;
    size_t iov_len = msg->msg_iov[iov_idx].iov_len;
    iov_len = MIN(iov_len, mlen - nwritten);
    memcpy(start, iov_base, iov_len);
    start += iov_len;
    nwritten += iov_len;
  }
  *start = 1;

  size_t old_remote = remote_ringbuf_tail_;
  if (conn_->rdma_write_event(remote_ringbuf_mr_, remote_ringbuf_tail_,
                              sendbuf_mr_, 0, len, IBV_WR_RDMA_WRITE, opcode_,
                              mlen)) {
    rdma_log(RDMA_ERROR,
             "fd = %d, RDMAEventSenderReceiver::send_msghdr, failed in "
             "rdma_write_event",
             fd_);
    return 1;
  }

  return 0;
}

int RDMAEventSenderReceiver::send_msghdr_zerocopy(const msghdr* msg,
                                                  size_t mlen) {
  size_t remote_ringbuf_sz = remote_ringbuf_mr_.length();
  size_t len = mlen + sizeof(msglen_t) + sizeof(tailtag_t);
  size_t free =
      remote_ringbuf_head_ > remote_ringbuf_tail_
          ? remote_ringbuf_head_ - remote_ringbuf_tail_
          : remote_ringbuf_sz - (remote_ringbuf_tail_ - remote_ringbuf_head_);

  if (len > free) {
    update_local_head();
    return 1;
  }

  // locate data_idx
  char* start = sendbuf_;
  char* end = sendbuf_;
  size_t data_idx = -1, data_len = 0;
  for (size_t i = 0; i < msg->msg_iovlen; i++) {
    void* bytes = msg->msg_iov[i].iov_base;
    if (zerocopy_contains((char*)bytes)) {
      data_idx = i;
      data_len = msg->msg_iov[i].iov_len;
      start = (char*)msg->msg_iov[i].iov_base;
      end = start + data_len;
      break;
    }
  }

  // left
  for (int i = data_idx - 1; i >= 0; i--) {
    start -= msg->msg_iov[i].iov_len;
    memcpy(start, msg->msg_iov[i].iov_base, msg->msg_iov[i].iov_len);
  }
  start -= sizeof(msglen_t);
  *(msglen_t*)start = mlen;
  size_t start_offset = start - sendbuf_;

  // right
  for (size_t i = data_idx + 1; i < msg->msg_iovlen; i++) {
    memcpy(end, msg->msg_iov[i].iov_base, msg->msg_iov[i].iov_len);
    end += msg->msg_iov[i].iov_len;
  }
  *end = 1;

  size_t old_remote = remote_ringbuf_tail_;
  if (conn_->rdma_write_event(remote_ringbuf_mr_, remote_ringbuf_tail_,
                              sendbuf_mr_, start_offset, len, IBV_WR_RDMA_WRITE,
                              opcode_, mlen)) {
    rdma_log(RDMA_ERROR,
             "fd = %d, RDMAEventSenderReceiver::send_msghdr_zerocopy, failed "
             "in rdma_write_event",
             fd_);
    return 1;
  }

  return 0;
}

int RDMAEventSenderReceiver::recv_msg(char* msg) {
  int ret = RDMASenderReceiver::recv_msg(msg);
  if (ret > 0) {
    conn_->_rcq_unprocessed_event_count.fetch_sub(1, std::memory_order_relaxed);
    if (event_post_recv()) {
      rdma_log(RDMA_ERROR,
               "RDMAEventSenderReceiver::recv_msg, failed to post recv");
      return 0;
    }
  }
  return ret;
}

int RDMAEventSenderReceiver::recv_msghdr(msghdr* msg) {
  int ret = RDMASenderReceiver::recv_msghdr(msg);
  if (ret > 0) {
    conn_->_rcq_unprocessed_event_count.fetch_sub(1, std::memory_order_relaxed);
  }
  return ret;
}

int RDMAEventSenderReceiver::recv_msghdr_zerocopy(msghdr* msg) {
  int ret = RDMASenderReceiver::recv_msghdr_zerocopy(msg);
  if (ret > 0) {
    conn_->_rcq_unprocessed_event_count.fetch_sub(1, std::memory_order_relaxed);
  }
  return ret;
}