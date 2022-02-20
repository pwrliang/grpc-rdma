#include "ringbuffer.h"
#include "log.h"

#define MIN3(a, b, c) MIN(a, MIN(b, c))
#define MIN4(a, b, c, d) MIN(MIN(a, b), MIN(c, d))

// -----< RingBuffer >-----

RingBuffer::RingBuffer(size_t capacity) : capacity_(capacity) {
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

bool RingBufferBP::check_head() {
  size_t head = head_;
  for (size_t i = 0; i < sizeof(size_t); i++) {
    if (buf_[head] != 0) return true;
    head = (head + 1) % capacity_;
  }
  return false;
}

// to reduce operation, the caller should guarantee the arguments are valid
uint8_t RingBufferBP::check_tail(size_t head, size_t mlen) {
  return buf_[(head + mlen + sizeof(size_t) + capacity_) % capacity_];
}

size_t RingBufferBP::check_mlen(size_t head) {
  if (head >= capacity_) {
    rdma_log(RDMA_ERROR, "RingBufferBP::check_mlen, head %d is out of range %d",
             head, capacity_);
    exit(-1);
  }

  size_t mlen;

  if (head + sizeof(size_t) <= capacity_) {
    mlen = *(size_t*)(buf_ + head);
    if (mlen && mlen + sizeof(size_t) + 1 < capacity_ &&
        check_tail(head, mlen) != 1) {
      return 0;
    }
    if (mlen > capacity_ / 2 + 100) {
      return 0;
    }
    return *(size_t*)(buf_ + head);
  }

  size_t r = capacity_ - head;
  size_t l = sizeof(size_t) - r;
  memcpy(&mlen, buf_ + head, r);
  memcpy((uint8_t*)(&mlen) + r, buf_, l);
  if (mlen && mlen + sizeof(size_t) + 1 < capacity_ &&
      check_tail(head, mlen) != 1) {
    return 0;
  }
  memcpy(&mlen, buf_ + head, r);
  memcpy((uint8_t*)(&mlen) + r, buf_, l);

  if (mlen > capacity_ / 2 + 100) {
    return 0;
  }

  return mlen;
}

size_t RingBufferBP::check_lens(size_t head) {
  size_t mlen, lens = 0;
  while ((mlen = check_mlen(head)) > 0) {
    if (mlen > 1024 * 1024) {
      printf("mlen = %zu\n", mlen);
    }
    lens += mlen + sizeof(size_t) + 1;
    head = (head + mlen + sizeof(size_t) + 1) % capacity_;
  }
  return lens;
}

size_t RingBufferBP::reset_buf_and_update_head(size_t lens) {
  if (head_ + lens > capacity_) {
    memset(buf_ + head_, 0, capacity_ - head_);
    memset(buf_, 0, lens + head_ - capacity_);
  } else {
    memset(buf_ + head_, 0, lens);
  }
  return update_head(lens);
}

size_t RingBufferBP::read_to_msghdr(msghdr* msg, size_t head,
                                    size_t expected_read_size) {
  if (expected_read_size == 0) {
    rdma_log(RDMA_WARNING,
             "RingBufferBP::read_to_msghdr, expected read size == 0");
    return 0;
  }
  if (head >= capacity_) {
    rdma_log(RDMA_ERROR, "RingBufferBP::read_to_msghdr, head out of bound");
    exit(-1);
  }
  if (expected_read_size >= capacity_) {
    rdma_log(RDMA_ERROR,
             "RingBufferEvent::read_to_msghdr, expected read size is too big");
    exit(-1);
  }

  uint8_t* iov_rbase;
  size_t iov_idx = 0, iov_offset = 0, iov_rlen;
  size_t mlen = check_mlen(head), m_offset = 0, m_rlen;
  size_t mlens = 0, read_size = 0,
         buf_offset = (head + sizeof(size_t)) % capacity_, n;
  while (read_size < expected_read_size && iov_idx < msg->msg_iovlen &&
         mlen > 0) {
    iov_rlen = msg->msg_iov[iov_idx].iov_len -
               iov_offset;  // rest space of current slice
    m_rlen = mlen -
             m_offset;  // uncopied bytes for currecnt message (length of mlen)
    n = MIN3(iov_rlen, m_rlen, capacity_ - buf_offset);
    iov_rbase = (uint8_t*)(msg->msg_iov[iov_idx].iov_base) + iov_offset;
    memcpy(iov_rbase, buf_ + buf_offset, n);
    rdma_log(RDMA_INFO,
             "RingBufferBP::read_to_msghdr, read %d bytes from head %d", n,
             buf_offset);
    buf_offset += n;
    read_size += n;
    iov_offset += n;
    m_offset += n;
    if (n == iov_rlen) {
      // all space of the current slice has been used up. move to next slice
      iov_idx++;
      iov_offset = 0;
    }
    if (n == m_rlen) {
      // current message (length of mlen) has been copyied. move to next head
      mlens += mlen;
      head =
          (head + sizeof(size_t) + mlen + 1) % capacity_;  // move to next head
      mlen = check_mlen(head);  // check mlen of the new head
      m_offset = 0;
      read_size += 1 + sizeof(size_t);  // add head and tail size to read_size

      // move buf_offset to the first place right after the head of the next
      // message if no next message, the loop will finish.
      buf_offset += 1 + sizeof(size_t);
    }
    buf_offset = buf_offset % capacity_;
  }

  if (read_size != expected_read_size) {
    rdma_log(RDMA_ERROR,
             "RingBufferBP::read_to_msghdr, read size (%d) != expected read "
             "size (%d)",
             read_size, expected_read_size);
    exit(-1);
  }

  reset_buf_and_update_head(read_size);

  return mlens;
}

// -----< RingBufferEvent >-----

size_t RingBufferEvent::read_to_msghdr(msghdr* msg, size_t head,
                                       size_t expected_read_size) {
  if (expected_read_size == 0) {
    rdma_log(RDMA_WARNING,
             "RingBufferEvent::read_to_msghdr, expected read size == 0");
    return 0;
  }
  if (head >= capacity_) {
    rdma_log(RDMA_ERROR, "RingBufferEvent::read_to_msghdr, head out of bound");
    exit(-1);
  }
  if (expected_read_size >= capacity_) {
    rdma_log(RDMA_ERROR,
             "RingBufferEvent::read_to_msghdr, expected read size is too big");
    exit(-1);
  }

  size_t read_size = 0, iov_rlen;
  uint8_t *iov_rbase, *rb_ptr;
  for (size_t i = 0, iov_offset = 0, n;
       read_size < expected_read_size && i < msg->msg_iovlen; read_size += n) {
    iov_rlen = msg->msg_iov[i].iov_len - iov_offset;
    iov_rbase = (uint8_t*)(msg->msg_iov[i].iov_base) + iov_offset;
    rb_ptr = buf_ + head;
    n = MIN3(capacity_ - head, expected_read_size - read_size, iov_rlen);
    memcpy(iov_rbase, rb_ptr, n);
    rdma_log(RDMA_INFO,
             "RingBufferEvent::read_to_msghdr, read %d bytes from head %d", n,
             head);
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
    rdma_log(RDMA_ERROR,
             "RingBufferEvent::read_to_msghdr, read size (%d) != expected read "
             "size (%d)",
             read_size, expected_read_size);
    exit(-1);
  }

  update_head(read_size);

  return read_size;
}