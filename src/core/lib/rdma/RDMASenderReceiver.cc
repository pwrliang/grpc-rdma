#include "RDMASenderReceiver.h"
#include "log.h"
#include "fcntl.h"
#include <thread>

#ifndef EPOLLEXCLUSIVE
#define EPOLLEXCLUSIVE (1 << 28)
#endif

#define IBV_DEV_NAME "mlx5_0"

// -----< RDMASenderReceiver >-----

std::atomic<bool> RDMASenderReceiver::node_opened_(false);
RDMANode RDMASenderReceiver::node_;

RDMASenderReceiver::RDMASenderReceiver()
  : ringbuf_sz_(DEFAULT_RINGBUF_SZ),
    sendbuf_sz_(DEFAULT_SENDBUF_SZ),
    head_recvbuf_sz_(DEFAULT_HEADBUF_SZ),
    head_sendbuf_sz_(DEFAULT_HEADBUF_SZ) {
  if (!node_opened_.exchange(true)) {
    node_.open(IBV_DEV_NAME);
  }

  if (sendbuf_sz_ >= ringbuf_sz_ / 2) {
    sendbuf_sz_ = ringbuf_sz_ / 2 - 1;
    rdma_log(RDMA_WARNING, "RDMASenderReceiver::RDMASenderReceiver, "
             "set sendbuf size to %d", sendbuf_sz_);
  }

  ibv_pd* pd = node_.get_pd();

  sendbuf_ = new uint8_t[sendbuf_sz_];
  if (sendbuf_mr_.local_reg(pd, sendbuf_, sendbuf_sz_)) {
    rdma_log(RDMA_ERROR,
             "RDMASenderReceiver::RDMASenderReceiver, failed to local_reg "
             "sendbuf_mr");
    exit(-1);
  }

  assert(posix_memalign(&head_recvbuf_, head_recvbuf_sz_, head_recvbuf_sz_) == 0);
  memset(head_recvbuf_, 0, head_recvbuf_sz_);
  if (local_head_recvbuf_mr_.local_reg(pd, head_recvbuf_, head_recvbuf_sz_)) {
    rdma_log(RDMA_ERROR,
             "RDMASenderReceiver::RDMASenderReceiver, failed to local_reg "
             "local_head_recvbuf_mr");
             exit(-1);
  }

  assert(posix_memalign(&head_sendbuf_, head_sendbuf_sz_, head_sendbuf_sz_) == 0);
  memset(head_sendbuf_, 0, head_sendbuf_sz_);
  if (head_sendbuf_mr_.local_reg(pd, head_sendbuf_, head_sendbuf_sz_)) {
    rdma_log(RDMA_ERROR,
             "RDMASenderReceiver::RDMASenderReceiver, failed to local_reg "
             "head_sendbuf_mr");
    exit(-1);
  }

}

RDMASenderReceiver::~RDMASenderReceiver() {
  delete(sendbuf_);
  free(head_recvbuf_);
  free(head_sendbuf_);
  ringbuf_ = nullptr;
  conn_ = nullptr;
}

void RDMASenderReceiver::update_remote_head() {
  if (!ringbuf_ || !conn_) {
    rdma_log(RDMA_ERROR, "RDMASenderReceiver::update_remote_head, ringbuf or connector has not been initialized");
    exit(-1);
  }

  reinterpret_cast<size_t*>(head_sendbuf_)[0] = ringbuf_->get_head();
  conn_->post_send_and_poll_completion(remote_head_recvbuf_mr_, 0,
                                       head_sendbuf_mr_, 0, 
                                       head_sendbuf_sz_, IBV_WR_RDMA_WRITE);
  rdma_log(RDMA_INFO, "RDMASenderReceiver::update_remote_head, %d", reinterpret_cast<size_t*>(head_sendbuf_)[0]);
}

void RDMASenderReceiver::update_local_head() {
  remote_ringbuf_head_ = reinterpret_cast<size_t*>(head_recvbuf_)[0];
}

// -----< RDMASenderReceiverBP >-----



// -----< RDMASenderReceiverEvent >-----

ibv_comp_channel* rdma_create_channel(RDMANode* node) {
  ibv_context* ctx = node->get_ctx();
  ibv_comp_channel* channel = ibv_create_comp_channel(ctx);
  int flags = fcntl(channel->fd, F_GETFL);
  if (fcntl(channel->fd, F_SETFL, flags | O_NONBLOCK) < 0) {
    rdma_log(RDMA_ERROR,
        "rdma_create_channel, failed to change channel fd to non-blocking");
    exit(-1);
  }
  return channel;
}

int rdma_destroy_channel(ibv_comp_channel* channel) {
  return ibv_destroy_comp_channel(channel);
}

void rdma_epoll_add_channel(int epfd, ibv_comp_channel* channel) {
  struct epoll_event ev_fd;
  ev_fd.events = static_cast<uint32_t>(EPOLLIN | EPOLLET | EPOLLEXCLUSIVE);
  ev_fd.data.ptr = reinterpret_cast<void*>(reinterpret_cast<intptr_t>(channel) | 3);
  if (epoll_ctl(epfd, EPOLL_CTL_ADD, channel->fd, &ev_fd) != 0) {
    switch (errno) {
      case EEXIST:
        rdma_log(RDMA_WARNING, "rdma_epoll_add_channel, EEXIST");
        return;
      default:
        rdma_log(RDMA_ERROR, "rdma_epoll_add_channel, failed to add channel fd to epfd");
        exit(-1);
    }
  }
  rdma_log(RDMA_INFO, "rdma_epoll_add_channel, add channel %p to epfd %d",
           channel, epfd);
  return;
}

void rdma_epoll_del_channel(int epfd, ibv_comp_channel* channel) {
  struct epoll_event ev_fd;
  epoll_ctl(epfd, EPOLL_CTL_DEL, channel->fd, &ev_fd);
}

bool rdma_is_available_event(struct epoll_event* ev) {
  return (reinterpret_cast<intptr_t>(ev->data.ptr) & 3) == 3;
}

void* rdma_check_incoming(struct epoll_event* ev) {
  if (rdma_is_available_event(ev) == false) return nullptr;

  ibv_comp_channel* channel = reinterpret_cast<ibv_comp_channel*>(reinterpret_cast<intptr_t>(ev->data.ptr) & ~3);
  ibv_cq* cq = nullptr;
  void* ev_ctx = nullptr;
  if (ibv_get_cq_event(channel, &cq, &ev_ctx) == -1) {
    rdma_log(RDMA_WARNING, "rdma_check_incoming, "
             "failed to get event from channel");
    
    return nullptr;
  }
  ibv_ack_cq_events(cq, 1);
  if (ibv_req_notify_cq(cq, 0)) {
    rdma_log(RDMA_ERROR, "rdma_check_incoming, "
             "failed to request CQ notification");
    exit(-1);
  }
  if (!ev_ctx) {
    rdma_log(RDMA_ERROR, "rdma_check_incoming, failed to retrieve rdmasr");
    exit(-1);
  }
  rdma_log(RDMA_DEBUG, "rdma_check_incoming, found incoming");
  RDMASenderReceiverEvent* rdmasr = (RDMASenderReceiverEvent*)(ev_ctx);
  rdmasr->unacked_event_num_.fetch_add(1);
  return rdmasr->get_user_data();
}

RDMASenderReceiverEvent::RDMASenderReceiverEvent() {
  ibv_pd* pd = node_.get_pd();

  ringbuf_event_ = new RingBufferEvent(ringbuf_sz_);
  if (local_ringbuf_mr_.local_reg(pd, ringbuf_event_->get_buf(), ringbuf_sz_)) {
    rdma_log(RDMA_ERROR,
             "RDMASenderReceiverEvent::RDMASenderReceiverEvent, failed to local_reg "
             "local_ringbuf_mr");
    exit(-1);
  }

  ringbuf_ = ringbuf_event_;
  unacked_event_num_.store(0);

  rdma_log(RDMA_INFO, "RDMASenderReceiverEvent created");
}

RDMASenderReceiverEvent::~RDMASenderReceiverEvent() {
  if (conn_event_) {
    delete conn_event_;
  }
  if (ringbuf_event_) {
    delete ringbuf_event_;
  }
}

void RDMASenderReceiverEvent::connect(int fd, void* user_data) {
  channel_ = rdma_create_channel(&node_);
  user_data_ = user_data;
  conn_event_ = new RDMAConnEvent(fd, &node_, channel_, this);
  conn_ = conn_event_;
  conn_event_->sync_mr(local_ringbuf_mr_, remote_ringbuf_mr_);
  conn_event_->sync_mr(local_head_recvbuf_mr_, remote_head_recvbuf_mr_);

  ibv_req_notify_cq(conn_event_->rcq_, 0);

  conn_event_->post_recvs(ringbuf_event_->get_buf(), ringbuf_sz_, local_ringbuf_mr_.lkey(), DEFAULT_MAX_POST_RECV);

  rdma_log(RDMA_INFO, "RDMASenderReceiverEvent connected, channel fd = %d", channel_->fd);
  connected_ = true;
}

size_t RDMASenderReceiverEvent::check_and_ack_incomings() {
  unread_data_size_ += conn_event_->poll_recv_completions_and_post_recvs(ringbuf_event_->get_buf(), ringbuf_sz_, local_ringbuf_mr_.lkey());
  return unread_data_size_;
}

size_t RDMASenderReceiverEvent::recv(msghdr* msg) {
  size_t read_size = ringbuf_event_->read_to_msghdr(msg, unread_data_size_);
  if (read_size == 0) {
    rdma_log(RDMA_WARNING, "RDMASenderReceiverEvent::recv, read_size == 0");
    return 0;
  }
  unread_data_size_ -= read_size;
  ringbuf_event_->update_head(read_size);
  garbage_ += read_size;
  total_recv_sz += read_size;
  if (garbage_ >= ringbuf_sz_ / 2) {
    update_remote_head();
    garbage_ = 0;
  }
  return read_size;
}

bool RDMASenderReceiverEvent::send(msghdr* msg, size_t mlen) {
  if (mlen > sendbuf_sz_) {
    rdma_log(RDMA_ERROR, "RDMASenderReceiverEvent::send, mlen > sendbuf size");
    return false;
  }

  size_t remote_ringbuf_sz = remote_ringbuf_mr_.length();
  // size_t free = (remote_ringbuf_sz + remote_ringbuf_head_ - remote_ringbuf_tail_) % remote_ringbuf_sz;
  // while (mlen > free) {
  //   update_local_head();
  //   free = (remote_ringbuf_sz + 1 + remote_ringbuf_head_ - remote_ringbuf_tail_) % (remote_ringbuf_sz + 1);
  //   std::this_thread::yield();
  // }

  size_t used = (remote_ringbuf_sz + remote_ringbuf_tail_ - remote_ringbuf_head_) % remote_ringbuf_sz;
  while (used + mlen >= remote_ringbuf_sz) {
    update_local_head();
    used = (remote_ringbuf_sz + remote_ringbuf_tail_ - remote_ringbuf_head_) % remote_ringbuf_sz;
    std::this_thread::yield();
  }

  uint8_t* start = sendbuf_;
  for (size_t iov_idx = 0, nwritten = 0;
       iov_idx < msg->msg_iovlen && nwritten < mlen;
       iov_idx++) {
    void* iov_base = msg->msg_iov[iov_idx].iov_base;
    size_t iov_len = msg->msg_iov[iov_idx].iov_len;
    nwritten += iov_len;
    if (nwritten <= sendbuf_sz_) {
      memcpy(start, iov_base, iov_len);
    } else {
      rdma_log(RDMA_ERROR, "RDMASenderReceiverEvent::send, mlen incorrect");
      return false;
    }
    start += iov_len;
  }

  conn_event_->post_send_and_poll_completion(remote_ringbuf_mr_, remote_ringbuf_tail_,
                                       sendbuf_mr_, 0, mlen, IBV_WR_RDMA_WRITE_WITH_IMM);
  remote_ringbuf_tail_ = (remote_ringbuf_tail_ + mlen) % remote_ringbuf_sz;
  total_send_sz += mlen;
  return true;
}