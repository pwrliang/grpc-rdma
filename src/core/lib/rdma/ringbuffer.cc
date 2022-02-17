#include "ringbuffer.h"
#include "log.h"

#define MIN3(a, b, c) MIN(a, MIN(b, c))

// -----< RingBuffer >-----

RingBuffer::RingBuffer(size_t capacity) 
  : capacity_(capacity) {
  buf_ = new uint8_t[capacity];
}

RingBuffer::~RingBuffer() {
  if (buf_) {
    delete buf_;
  }
}

size_t RingBuffer::update_head(size_t inc) {
  head_ = (head_ + inc) % capacity_;
  return head_;
}


// -----< RingBufferBP >-----



// -----< RingBufferEvent >-----

size_t RingBufferEvent::read_to_msghdr(msghdr* msg, size_t head, size_t expected_read_size) {
  if (expected_read_size == 0) {
    rdma_log(RDMA_WARNING, "RingBufferEvent::read_to_msghdr, expected read size == 0");
    return 0;
  }
  if (head > capacity_) {
    rdma_log(RDMA_ERROR, "RingBufferEvent::read_to_msghdr, head out of bound");
    exit(-1);
  }
  if (expected_read_size > capacity_) {
    rdma_log(RDMA_ERROR, "RingBufferEvent::read_to_msghdr, expected read size is too big");
    exit(-1);
  }

  size_t read_size = 0, iov_rlen;
  uint8_t *iov_rbase, *rb_ptr;
  for (size_t i = 0, iov_offset = 0, n; 
       read_size < expected_read_size && i < msg->msg_iovlen;
       read_size += n) {
    iov_rlen = msg->msg_iov[i].iov_len - iov_offset;
    iov_rbase = (uint8_t*)(msg->msg_iov[i].iov_base) + iov_offset;
    rb_ptr = buf_ + head;
    n = MIN3(capacity_ - head, expected_read_size - read_size, iov_rlen);
    memcpy(iov_rbase, rb_ptr, n);
    rdma_log(RDMA_INFO, "RingBufferEvent::read_to_msghdr, read %d bytes from head %d", n, head);
    head += n;
    if (head == capacity_) {
      head = 0;
      iov_offset += n;
    }
    if (n == iov_rlen) {
      i++;
      iov_offset = 0;
    }
  }

  if (read_size != expected_read_size) {
    rdma_log(RDMA_ERROR, "RingBufferEvent::read_to_msghdr, read size (%d) != expected read size (%d)",
             read_size, expected_read_size);
    exit(-1);
  }

  return read_size;
}