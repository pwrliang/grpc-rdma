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
#include <cassert>

class RingBuffer {
  public:
    RingBuffer(size_t capacity);
    virtual ~RingBuffer();

    uint8_t* get_buf() { return buf_; }
    size_t get_capacity() { return capacity_; }
    size_t get_head() { return head_; }
    size_t update_head(size_t inc);

  protected:
    uint8_t* buf_ = nullptr;
    size_t capacity_;
    size_t head_ = 0;
};

class RingBufferBP : public RingBuffer {
  public:
    RingBufferBP(size_t capacity) : RingBuffer(capacity) {}
};

class RingBufferEvent : public RingBuffer {
  public:
    RingBufferEvent(size_t capcatiy) : RingBuffer(capcatiy) {}

    size_t read_to_msghdr(msghdr* msg, size_t size) { return read_to_msghdr(msg, head_, size); }
    size_t read_to_msghdr(msghdr* msg, size_t head, size_t expected_read_size);
};

#endif