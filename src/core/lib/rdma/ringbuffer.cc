#include "ringbuffer.h"
#include "grpc/impl/codegen/log.h"
#include "src/core/lib/debug/trace.h"
#define MIN3(a, b, c) MIN(a, MIN(b, c))
#define MIN4(a, b, c, d) MIN(MIN(a, b), MIN(c, d))
grpc_core::DebugOnlyTraceFlag grpc_trace_ringbuffer(false, "rdma_ringbuffer");

// to reduce operation, the caller should guarantee the arguments are valid
uint8_t RingBufferBP::check_tail(size_t head, size_t mlen) const {
  return buf_[(head + mlen + sizeof(size_t) + capacity_) % capacity_];
}

size_t RingBufferBP::check_mlen(size_t head) const {
  GPR_ASSERT(head < capacity_);
  size_t mlen;

  if (head + sizeof(size_t) <= capacity_) {
    mlen = *reinterpret_cast<size_t*>(buf_ + head);
    if (mlen == 0) {
      return 0;
    }
    if (mlen + sizeof(size_t) + 1 < capacity_ && check_tail(head, mlen) != 1) {
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
      check_tail(head, mlen) != 1) {
    return 0;
  }
  memcpy(&mlen, buf_ + head, r);
  memcpy(reinterpret_cast<uint8_t*>(&mlen) + r, buf_, l);

  return mlen;
}

size_t RingBufferBP::check_mlens(size_t head) const {
  size_t mlen, mlens = 0;
  while ((mlen = check_mlen(head)) > 0) {
    mlens += mlen;
    head = (head + mlen + sizeof(size_t) + 1) % capacity_;
  }
  return mlens;
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

bool RingBufferBP::read_to_msghdr(msghdr* msg, size_t& expected_mlens) {
  auto head = head_;
  GPR_ASSERT(expected_mlens > 0 && expected_mlens < capacity_);
  GPR_ASSERT(head < capacity_);

  size_t iov_idx = 0, iov_offset = 0;
  size_t mlen = check_mlen(head), m_offset = 0;
  size_t mlens = 0, lens = 0, buf_offset = (head + sizeof(size_t)) % capacity_;
  size_t msghdr_size = 0;

  for (int i = 0; i < msg->msg_iovlen; i++) {
    msghdr_size += msg->msg_iov[i].iov_len;
  }

  while (iov_idx < msg->msg_iovlen && mlen > 0) {
    size_t iov_rlen = msg->msg_iov[iov_idx].iov_len -
                      iov_offset;  // rest space of current slice
    size_t m_rlen =
        mlen -
        m_offset;  // uncopied bytes for currecnt message (length of mlen)
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
    if (n == iov_rlen) {
      // all space of the current slice has been used up. move to next slice
      iov_idx++;
      iov_offset = 0;
    }
    if (n == m_rlen) {
      // current message (length of mlen) has been copyied. move to next head
      mlens += mlen;
      lens += sizeof(size_t) + mlen + 1;

      // when we have chance to read, read greedily.
      if (mlens >= expected_mlens) {
        break;
      }
      head =
          (head + sizeof(size_t) + mlen + 1) % capacity_;  // move to next head
      mlen = check_mlen(head);           // check mlen of the new head
      if (mlens + mlen > msghdr_size) {  // msghdr could not hold newf mlen
        break;
      }
      m_offset = 0;

      // move buf_offset to the first place right after the head of the next
      // message if no next message, the loop will finish.
      buf_offset += 1 + sizeof(size_t);
    }
    buf_offset = buf_offset % capacity_;
  }

  expected_mlens = mlens;
  reset_buf_and_update_head(lens);

  garbage_ += lens;
  if (garbage_ >= capacity_ / 2) {
    garbage_ = 0;
    return true;
  }
  return false;
}

// -----< RingBufferEvent >-----

bool RingBufferEvent::read_to_msghdr(msghdr* msg, size_t& expected_read_size) {
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

  update_head(lens);
  expected_read_size = lens;

  garbage_ += lens;
  if (garbage_ >= capacity_ / 2) {
    garbage_ = 0;
    return true;
  }

  return false;
}

uint8_t RingBufferAdaptive::check_tail(size_t head, size_t mlen) const {
  return buf_[(head + mlen + sizeof(size_t) + capacity_) % capacity_];
}

size_t RingBufferAdaptive::check_mlen(size_t head,
                                      RDMASenderReceiverMode& mode) const {
  GPR_ASSERT(head < capacity_);
  size_t mlen;

  auto parse_hdr = [&](uint64_t hdr) {
    mode = static_cast<RDMASenderReceiverMode>(hdr >> 56);
    mlen = hdr & ((static_cast<uint64_t>(1) << 56) - 1);
  };

  if (head + sizeof(size_t) <= capacity_) {
    size_t hdr = *reinterpret_cast<uint64_t*>(buf_ + head);
    parse_hdr(hdr);

    if (mlen == 0) {
      return 0;
    }
    if (mlen + sizeof(size_t) + 1 < capacity_ && check_tail(head, mlen) != 1) {
      return 0;
    }
    // need read again, since mlen is read before tag = 1
    hdr = *reinterpret_cast<uint64_t*>(buf_ + head);
    parse_hdr(hdr);
    return mlen;
  }

  size_t r = capacity_ - head;
  size_t l = sizeof(uint64_t) - r;
  size_t hdr;

  memcpy(&hdr, buf_ + head, r);
  memcpy(reinterpret_cast<uint8_t*>(&hdr) + r, buf_, l);

  if (hdr == 0) {
    return 0;
  }

  parse_hdr(hdr);

  // hdr is invalid because we can not see the tail
  if (mlen && mlen + sizeof(size_t) + 1 < capacity_ &&
      check_tail(head, mlen) != 1) {
    return 0;
  }

  // Read again
  memcpy(&hdr, buf_ + head, r);
  memcpy(reinterpret_cast<uint8_t*>(&hdr) + r, buf_, l);

  parse_hdr(hdr);
  return mlen;
}

size_t RingBufferAdaptive::check_mlens(size_t head) const {
  size_t mlen, mlens = 0;
  RDMASenderReceiverMode lastMode = RDMASenderReceiverMode::kNone;
  RDMASenderReceiverMode mode;

  while ((mlen = check_mlen(head, mode)) > 0) {
    if (lastMode != RDMASenderReceiverMode::kNone && lastMode != mode) {
      gpr_log(GPR_INFO, "Mode change from %s to %s",
              rdma_mode_to_string(lastMode), rdma_mode_to_string(mode));
      break;
    }
    mlens += mlen;
    lastMode = mode;
    head = (head + mlen + sizeof(size_t) + 1) % capacity_;
  }

  return mlens;
}

size_t RingBufferAdaptive::reset_buf_and_update_head(size_t lens) {
  if (head_ + lens > capacity_) {
    memset(buf_ + head_, 0, capacity_ - head_);
    memset(buf_, 0, lens + head_ - capacity_);
  } else {
    memset(buf_ + head_, 0, lens);
  }
  return update_head(lens);
}

bool RingBufferAdaptive::read_to_msghdr(msghdr* msg, size_t& expected_mlens) {
  GPR_ASSERT(expected_mlens > 0 && expected_mlens < capacity_);
  GPR_ASSERT(head_ < capacity_);
  auto head = head_;
  RDMASenderReceiverMode mode;
  size_t iov_idx = 0, iov_offset = 0;
  size_t mlen = check_mlen(head, mode), m_offset = 0;
  size_t mlens = 0, lens = 0,
         buf_offset = (head + sizeof(uint64_t)) % capacity_;
  size_t msghdr_size = 0;

  for (int i = 0; i < msg->msg_iovlen; i++) {
    msghdr_size += msg->msg_iov[i].iov_len;
  }

  while (iov_idx < msg->msg_iovlen && mlen > 0) {
    size_t iov_rlen = msg->msg_iov[iov_idx].iov_len -
                      iov_offset;  // rest space of current slice
    size_t m_rlen =
        mlen -
        m_offset;  // uncopied bytes for currecnt message (length of mlen)
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
    if (n == iov_rlen) {
      // all space of the current slice has been used up. move to next slice
      iov_idx++;
      iov_offset = 0;
    }

    if (n == m_rlen) {
      // current message (length of mlen) has been copyied. move to next head
      mlens += mlen;
      lens += sizeof(uint64_t) + mlen + 1;

      // when we have chance to read, read greedily.
      if (mlens >= expected_mlens) {
        break;
      }
      head = (head + sizeof(uint64_t) + mlen + 1) %
             capacity_;  // move to next head
      auto lastMode = mode;
      mlen = check_mlen(head, mode);  // check mlen of the new head
      // msghdr could not hold newf mlen or mode changed
      if (mlens + mlen > msghdr_size || mode != lastMode) {
        break;
      }
      m_offset = 0;

      // move buf_offset to the first place right after the head of the next
      // message if no next message, the loop will finish.
      buf_offset += 1 + sizeof(uint64_t);
    }
    buf_offset = buf_offset % capacity_;
  }

  expected_mlens = mlens;
  reset_buf_and_update_head(lens);

  garbage_ += lens;
  if (garbage_ >= capacity_ / 2) {
    garbage_ = 0;
    return true;
  }
  return false;
}