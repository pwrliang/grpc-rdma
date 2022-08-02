#include "grpc/impl/codegen/log.h"
#include "include/grpcpp/stats_time.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/rdma/ringbuffer.h"
#define MIN3(a, b, c) MIN(a, MIN(b, c))
grpc_core::DebugOnlyTraceFlag grpc_trace_ringbuffer(false, "rdma_ringbuffer");

void mt_memcpy(uint8_t* dest, const uint8_t* src, size_t size) {
#pragma omp parallel for num_threads(4)
  for (size_t i = 0; i < size; i++) {
    dest[i] = src[i];
  }
}
// to reduce operation, the caller should guarantee the arguments are valid
uint8_t RingBufferBP::checkTail(size_t head, size_t mlen) const {
  return buf_[(head + mlen + sizeof(size_t) + capacity_) % capacity_];
}

size_t RingBufferBP::checkFirstMesssageLength(size_t head) const {
  GPR_ASSERT(head < capacity_);
  size_t mlen;

  if (head + sizeof(size_t) <= capacity_) {
    mlen = *reinterpret_cast<size_t*>(buf_ + head);
    if (mlen == 0 || mlen > get_max_send_size() || checkTail(head, mlen) != 1) {
      return 0;
    }
    // need read again, since mlen is read before tag = 1
    return *reinterpret_cast<size_t*>(buf_ + head);
  }

  size_t r = capacity_ - head;
  size_t l = sizeof(size_t) - r;
  memcpy(&mlen, buf_ + head, r);
  memcpy(reinterpret_cast<uint8_t*>(&mlen) + r, buf_, l);
  if (mlen == 0 || mlen > get_max_send_size() || checkTail(head, mlen) != 1) {
    return 0;
  }
  memcpy(&mlen, buf_ + head, r);
  memcpy(reinterpret_cast<uint8_t*>(&mlen) + r, buf_, l);

  return mlen;
}

size_t RingBufferBP::checkMessageLength(size_t head) const {
  size_t mlen, mlens = 0;
  while ((mlen = checkFirstMesssageLength(head)) > 0) {
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
  if (expected_mlens == 0 || expected_mlens > get_max_send_size()) {
    gpr_log(GPR_ERROR, "Illegal expected_mlens: %zu, head: %zu", expected_mlens,
            head);
  }
  GPR_ASSERT(head < capacity_);

  size_t iov_idx = 0, iov_offset = 0;
  size_t mlen = checkFirstMesssageLength(head), m_offset = 0;
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
      mlen = checkFirstMesssageLength(head);  // check mlen of the new head
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

size_t RingBufferBP::Read(msghdr* msg, bool& recycle) {
  if (msg->msg_iovlen == 0) {
    recycle = false;
    return false;
  }
  auto curr_head = head_;

  // iter on msg
  size_t curr_msg_offset = (curr_head + sizeof(size_t)) % capacity_;
  size_t rest_mlen = checkFirstMesssageLength(curr_head);
  // inter on iov
  size_t curr_iov_offset = 0;
  size_t rest_iov_len = msg->msg_iov[0].iov_len;
  size_t total_read = 0;

  GPR_ASSERT(curr_head < capacity_);

  auto next_head = [this](size_t head) {
    return (head + sizeof(size_t) + checkFirstMesssageLength(head) + 1) %
           capacity_;
  };

  cycles_t memcpy_cycles = 0;

  for (size_t iov_idx = 0; iov_idx < msg->msg_iovlen && rest_mlen > 0;) {
    size_t len = std::min(rest_iov_len, rest_mlen);
    size_t right_len = std::max(curr_msg_offset + len, capacity_) - capacity_;
    size_t left_len = len - right_len;

    auto begin = get_cycles();

    mt_memcpy(
        static_cast<uint8_t*>(msg->msg_iov[iov_idx].iov_base) + curr_iov_offset,
        buf_ + curr_msg_offset, left_len);
    curr_msg_offset = (curr_msg_offset + left_len) % capacity_;
    curr_iov_offset += left_len;

    // cyclic case
    if (right_len > 0) {
      mt_memcpy(static_cast<uint8_t*>(msg->msg_iov[iov_idx].iov_base) +
                    curr_iov_offset,
                buf_ + curr_msg_offset, right_len);
      curr_msg_offset = (curr_msg_offset + right_len) % capacity_;
      curr_iov_offset += right_len;
    }

    memcpy_cycles += get_cycles() - begin;

    rest_iov_len -= len;
    rest_mlen -= len;
    total_read += len;

    if (rest_iov_len == 0) {
      iov_idx++;
      curr_iov_offset = 0;
      if (iov_idx < msg->msg_iovlen) {
        rest_iov_len = msg->msg_iov[iov_idx].iov_len;
      }
    }

    if (rest_mlen == 0) {
      curr_head = next_head(curr_head);
      curr_msg_offset = (curr_head + sizeof(size_t)) % capacity_;
      rest_mlen = checkFirstMesssageLength(curr_head);
    }
  }

  grpc_stats_time_add(GRPC_STATS_TIME_ADHOC_1, memcpy_cycles);

  // partial read current msg
  if (rest_mlen > 0) {
    size_t consumed_mlen = checkFirstMesssageLength(curr_head) - rest_mlen;

    // calculate new head
    curr_head = (curr_head + consumed_mlen) % capacity_;

    size_t right_len =
        std::max(curr_head + sizeof(size_t), capacity_) - capacity_;
    size_t left_len = sizeof(size_t) - right_len;
    // write back rest_mlen
    memcpy(buf_ + curr_head, &rest_mlen, left_len);
    memcpy(buf_, reinterpret_cast<char*>(&rest_mlen) + left_len, right_len);
  }

  size_t read_lens = (curr_head - head_ + capacity_) % capacity_;
  resetBufAndUpdateHead(read_lens);

  garbage_ += read_lens;
  recycle = false;
  if (garbage_ >= capacity_ / 2) {
    garbage_ = 0;
    recycle = true;
  }
  return total_read;
}

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