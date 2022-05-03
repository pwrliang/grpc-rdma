#ifndef _RINGBUFFER_H_
#define _RINGBUFFER_H_
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>
#include <atomic>
#include <cassert>
#include <iomanip>
#include <sstream>

// the data size of ringbuffer should <= capacity - 1, which means the
// ringbuffer cannot be full. if data size == capacity, then it is possible that
// remote_head == remote_tail, then remote cannot tell if there it is full or
// empty.
class RingBuffer {
 public:
  explicit RingBuffer(size_t capacity)
      : capacity_(capacity),
        buf_(new uint8_t[capacity]),
        head_(0),
        garbage_(0) {
    memset(buf_, 0, capacity);
  }
  virtual ~RingBuffer() { delete[] buf_; }

  const uint8_t* get_buf() const { return buf_; }

  uint8_t* get_buf() { return buf_; }

  size_t get_head() const { return head_; }

  virtual size_t get_sendbuf_size() const = 0;

  virtual size_t get_max_send_size() const = 0;

  size_t get_capacity() const { return capacity_; }

  /**
   * Read data from ringbuffer
   * @param msg
   * @param expected_lens maximum length of data to read. This size can not
   * exceed the data size of actually data in ring buffer. After reading, this
   * variable will be changed to actual read size.
   * @return Return true if the size of garbage exceeds half of ring buffer.
   */
  virtual bool read_to_msghdr(msghdr* msg, size_t& expected_lens) = 0;

 protected:
  size_t update_head(size_t inc) {
    head_ = (head_ + inc) % capacity_;
    return head_;
  }

  size_t capacity_;
  uint8_t* buf_;
  size_t head_;
  size_t garbage_;
};

class RingBufferBP : public RingBuffer {
 public:
  explicit RingBufferBP(size_t capacity) : RingBuffer(capacity) {}

  size_t check_mlens() const { return check_mlens(head_); }
  size_t check_mlen() const { return check_mlen(head_); }

  bool read_to_msghdr(msghdr* msg, size_t& expected_lens) override;

  size_t get_sendbuf_size() const override {
    /*
     * BP: garbage max R/2 - 1, minimum free size = R - 8 - (R/2 - 1)
     */
    return capacity_ / 2 - 7;
  }

  size_t get_max_send_size() const override {
    return get_sendbuf_size() - sizeof(size_t) - 1;
  }

 protected:
  uint8_t check_tail(size_t head, size_t mlen) const;
  size_t check_mlen(size_t head) const;
  size_t check_mlens(size_t head) const;

  // reset buf first then update head. Otherswise new head may read the old data
  // from unupdated space.
  size_t reset_buf_and_update_head(size_t lens);
};

class RingBufferEvent : public RingBuffer {
 public:
  explicit RingBufferEvent(size_t capcatiy) : RingBuffer(capcatiy) {}

  bool read_to_msghdr(msghdr* msg, size_t& size) override;

  size_t get_sendbuf_size() const override {
    /*
     * Event: garbage max R/2 - 1, available R-1, send = R-1 - (R/2-1) = R/2
     */
    return capacity_ / 2;
  }

  size_t get_max_send_size() const override { return get_sendbuf_size(); }

 private:
};

#endif