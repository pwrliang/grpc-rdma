#include "grpc/impl/codegen/log.h"

#include "src/core/lib/debug/trace.h"
#include "src/core/lib/rdma/ringbuffer.h"
#define MIN3(a, b, c) MIN(a, MIN(b, c))
grpc_core::DebugOnlyTraceFlag grpc_trace_ringbuffer(false, "rdma_ringbuffer");

// to reduce operation, the caller should guarantee the arguments are valid
uint8_t RingBufferBP::checkTail(size_t head, size_t mlen) const {
  return buf_[(head + mlen + sizeof(size_t) + capacity_) % capacity_];
}

size_t RingBufferBP::checkMessageLength(size_t head) const {
  GPR_ASSERT(head < capacity_);
  size_t mlen;

  if (head + sizeof(size_t) <= capacity_) {
    mlen = *reinterpret_cast<size_t*>(buf_ + head);
    if (mlen == 0) {
      return 0;
    }
    if (mlen + sizeof(size_t) + 1 < capacity_ && checkTail(head, mlen) != 1) {
      return 0;
    }
    // need read again, since mlen is read before tag = 1
    return *reinterpret_cast<size_t*>(buf_ + head);
  }

  size_t r = capacity_ - head;
  size_t l = sizeof(size_t) - r;
  memcpy(&mlen, buf_ + head, r);
  memcpy(reinterpret_cast<uint8_t*>(&mlen) + r, buf_, l);
  if (mlen == 0) return 0;
  if (mlen && mlen + sizeof(size_t) + 1 < capacity_ &&
      checkTail(head, mlen) != 1) {
    return 0;
  }
  memcpy(&mlen, buf_ + head, r);
  memcpy(reinterpret_cast<uint8_t*>(&mlen) + r, buf_, l);

  return mlen;
}

size_t RingBufferBP::checkFirstMessageLength(size_t head) const {
  size_t mlen, mlens = 0;
  while ((mlen = checkMessageLength(head)) > 0) {
    mlens += mlen;
    head = (head + mlen + sizeof(size_t) + 1) % capacity_;
  }
  return mlens;
}

size_t RingBufferBP::resetBufAndUpdateHead(size_t lens) {
  if (head_ + lens > capacity_) {
    memset(buf_ + head_, 0, capacity_ - head_);
    memset(buf_, 0, lens + head_ - capacity_);
  } else {
    memset(buf_ + head_, 0, lens);
  }
  return updateHead(lens);
}

bool RingBufferBP::Read(msghdr* msg, size_t& expected_mlens) {
  auto head = head_;
  GPR_ASSERT(expected_mlens > 0 && expected_mlens < capacity_);
  GPR_ASSERT(head < capacity_);

  size_t iov_idx = 0, iov_offset = 0;
  size_t mlen = checkMessageLength(head), m_offset = 0;
  size_t read_mlens = 0, read_lens = 0;
  size_t buf_offset = (head + sizeof(size_t)) % capacity_;
  size_t msghdr_size = 0;

  // calculate total space
  for (int i = 0; i < msg->msg_iovlen; i++) {
    msghdr_size += msg->msg_iov[i].iov_len;
  }

  while (iov_idx < msg->msg_iovlen && mlen > 0 && mlen <= msghdr_size) {
    size_t iov_rlen = msg->msg_iov[iov_idx].iov_len -
                      iov_offset;     // rest space of current slice
    size_t m_rlen = mlen - m_offset;  // uncopied bytes of current message
    size_t n = MIN3(iov_rlen, m_rlen, capacity_ - buf_offset);
    auto* iov_rbase =
        static_cast<uint8_t*>(msg->msg_iov[iov_idx].iov_base) + iov_offset;
    memcpy(iov_rbase, buf_ + buf_offset, n);
#ifndef NDEBUG
    if (GRPC_TRACE_FLAG_ENABLED(grpc_trace_ringbuffer)) {
      gpr_log(GPR_DEBUG, "read_to_msghdr, read %zu bytes from head %zu", n,
              buf_offset);
    }
#endif
    buf_offset += n;
    iov_offset += n;
    m_offset += n;

    // current slice is used up
    if (n == iov_rlen) {
      // all space of the current slice has been used up. move to next slice
      iov_idx++;
      iov_offset = 0;
    }

    // current message is used up
    if (n == m_rlen) {
      // current message (length of mlen) has been copyied. move to next head
      read_mlens += mlen;
      read_lens += sizeof(size_t) + mlen + 1;

      // when we have chance to read, read greedily.
      if (read_mlens >= expected_mlens) {
        break;
      }
      head =
          (head + sizeof(size_t) + mlen + 1) % capacity_;  // move to next head
      mlen = checkMessageLength(head);                // check mlen of the new head
      if (read_mlens + mlen > msghdr_size) {  // msghdr could not hold new mlen
        break;
      }
      m_offset = 0;

      // move buf_offset to the first place right after the head of the next
      // message if no next message, the loop will finish.
      buf_offset += 1 + sizeof(size_t);
    }
    buf_offset = buf_offset % capacity_;
  }

  expected_mlens = read_mlens;
  resetBufAndUpdateHead(read_lens);

  garbage_ += read_lens;
  if (garbage_ >= capacity_ / 2) {
    garbage_ = 0;
    return true;
  }
  return false;
}

// -----< RingBufferEvent >-----

bool RingBufferEvent::Read(msghdr* msg, size_t& expected_read_size) {
  auto head = head_;
  GPR_ASSERT(expected_read_size > 0 && expected_read_size < capacity_);
  GPR_ASSERT(head < capacity_);

  size_t lens = 0, iov_rlen;
  uint8_t *iov_rbase, *rb_ptr;
  for (size_t i = 0, iov_offset = 0, n;
       lens < expected_read_size && i < msg->msg_iovlen; lens += n) {
    iov_rlen =
        msg->msg_iov[i].iov_len - iov_offset;  // rest space of current slice
    iov_rbase = static_cast<uint8_t*>(msg->msg_iov[i].iov_base) + iov_offset;
    rb_ptr = buf_ + head;
    n = MIN3(capacity_ - head, expected_read_size - lens, iov_rlen);
    memcpy(iov_rbase, rb_ptr, n);
#ifndef NDEBUG
    if (GRPC_TRACE_FLAG_ENABLED(grpc_trace_ringbuffer)) {
      gpr_log(GPR_DEBUG, "read_to_msghdr, read %zu bytes from head %zu", n,
              head);
    }
#endif
    head += n;
    iov_offset += n;
    if (n == iov_rlen) {
      // all space of the current slice has been used up. move to next slice
      i++;
      iov_offset = 0;
    }
    head = head % capacity_;
  }

  updateHead(lens);
  expected_read_size = lens;

  garbage_ += lens;
  if (garbage_ >= capacity_ / 2) {
    garbage_ = 0;
    return true;
  }

  return false;
}