#include "ringbuffer.h"

RingBuffer::RingBuffer(msglen_t capacity) {
  size_ = capacity;
  buf_ = (uint8_t*)malloc(capacity);
  memset(buf_, 0, size_);
  if (buf_) {
    reset_head();
    end_ = buf_ + size_;
  } else {
    rdma_log(RDMA_ERROR, "RingBuffer::RingBuffer, cannot malloc space for buf");
    free(buf_);
    exit(-1);
  }
}

RingBuffer::~RingBuffer() {
  free(buf_);
  head_ = nullptr;
  end_ = nullptr;
}

void RingBuffer::reset_head() { head_ = buf_; }

uint8_t* RingBuffer::get_buf() { return buf_; }

msglen_t RingBuffer::get_buf_size() { return size_; }

uint8_t* RingBuffer::get_head() { return head_; }

msglen_t RingBuffer::check_mlen(uint8_t* head) {
  GPR_ASSERT(head >= buf_ && head < end_);
  msglen_t mlen;
  if (head + sizeof(msglen_t) <= end_) {
    mlen = *(msglen_t*)head;
    if (mlen > 0 && !check_tail(head, mlen)) return 0;
    msglen_t temp = *(msglen_t*)head;
    // assert(temp == mlen);

    return temp;
  }

  size_t rlen = end_ - head;
  size_t llen = sizeof(msglen_t) - rlen;
  memcpy(&mlen, head, rlen);
  memcpy((uint8_t*)(&mlen) + rlen, buf_, llen);
  if (mlen > 0 && !check_tail(head, mlen)) return 0;

  msglen_t temp;
  rlen = end_ - head;
  llen = sizeof(msglen_t) - rlen;
  memcpy(&temp, head, rlen);
  memcpy((uint8_t*)(&temp) + rlen, buf_, llen);
  // assert(temp == mlen);

  return temp;
}

msglen_t RingBuffer::check_mlen() { return check_mlen(head_); }

tailtag_t RingBuffer::check_tail(uint8_t* head, msglen_t mlen) {
  GPR_ASSERT(mlen > 0 && mlen < size_);
  // tailtag_t ret;
  uint8_t* tail;
  if (head + mlen + sizeof(msglen_t) + sizeof(tailtag_t) <= end_) {
    tail = head + sizeof(msglen_t) + mlen;
  } else {
    tail = buf_ + sizeof(msglen_t) + mlen - (end_ - head);
  }
  return *tail;
}

uint8_t* RingBuffer::update_head() {
  msglen_t mlen = check_mlen();
  GPR_ASSERT(mlen > 0 && mlen < size_);
  msglen_t len = mlen + sizeof(msglen_t) + sizeof(tailtag_t);
  uint8_t* old_head = head_;

  if (head_ + len >= end_) {
    head_ = head_ + len - size_;
  } else {
    head_ = head_ + len;
  }

  GPR_ASSERT(head_ >= buf_ && head_ < end_);

  return old_head;
}

msglen_t RingBuffer::reset_buf_from(uint8_t* head) {
  GPR_ASSERT(head >= buf_ && head < end_);
  msglen_t ret = 0, len;

  while (head != head_) {
    auto mlen = check_mlen(head);
    GPR_ASSERT(mlen > 0);
    if (!mlen || mlen > size_) return 0;
    len = mlen + sizeof(msglen_t) + sizeof(tailtag_t);
    for (size_t nwritten = 0, n; nwritten < len; nwritten += n) {
      n = MIN(end_ - head, len - nwritten);
      bzero(head, n);
      head += n;
      if (head == end_) head = buf_;
    }
    ret += len;
  }

  return ret;
}

msglen_t RingBuffer::reset_buf_update_head() {
  uint8_t* old_head = update_head();
  return reset_buf_from(old_head);
}

msglen_t RingBuffer::get_msg(void* message) {
  uint8_t* start = head_ + sizeof(msglen_t);
  if (start >= end_) start -= size_;
  msglen_t mlen = check_mlen();

  GPR_ASSERT(mlen < size_);

  if (mlen == 0) return 0;

  for (msglen_t nwritten = 0, n; nwritten < mlen; nwritten += n) {
    n = MIN(end_ - start, mlen - nwritten);
    memcpy((void*)((uintptr_t)message + nwritten), start, n);
    start += n;
    if (start == end_) start = buf_;
  }

  return mlen;
}

msglen_t RingBuffer::get_msghdr(msghdr* message) {
  uint8_t* start = head_ + sizeof(msglen_t);
  if (start >= end_) start -= size_;
  msglen_t mlen = check_mlen();

  GPR_ASSERT(mlen < size_);
  if (mlen == 0) return 0;

  msglen_t nwritten = 0;
  for (msglen_t iov_idx = 0, iov_nwritten = 0, n;
       nwritten < mlen && iov_idx < message->msg_iovlen; nwritten += n) {
    size_t cur_iov_len = message->msg_iov[iov_idx].iov_len - iov_nwritten;
    void* iov_base =
        (void*)((uint8_t*)(message->msg_iov[iov_idx].iov_base) + iov_nwritten);
    n = MIN(end_ - start, mlen - nwritten);
    n = MIN(n, cur_iov_len);
    memcpy(iov_base, start, n);
    // printf("recv %d bytes from %d, %d\n", n, start - buf_, start + n - buf_);
    start += n;
    if (start == end_) {
      start = buf_;
      iov_nwritten += n;
    }
    if (n == cur_iov_len) {
      iov_idx++;
      iov_nwritten = 0;
    }
  }

  GPR_ASSERT(nwritten == mlen);
  return nwritten;
}

msglen_t RingBuffer::get_msghdr_zerocopy(msghdr* message) {
  uint8_t* start = head_ + sizeof(msglen_t);
  if (start >= end_) start -= size_;
  msglen_t mlen = check_mlen();

  GPR_ASSERT(mlen < size_);
  if (mlen == 0) return 0;

  size_t iov_idx = 0;
  for (msglen_t nwritten = 0, n; nwritten < mlen; nwritten += n) {
    n = MIN(end_ - start, mlen - nwritten);
    message->msg_iov[iov_idx].iov_base = start;
    message->msg_iov[iov_idx].iov_len = n;
    start += n;
    if (start == end_) start = buf_;
    iov_idx++;
  }
  message->msg_iovlen = iov_idx;
  return mlen;
}