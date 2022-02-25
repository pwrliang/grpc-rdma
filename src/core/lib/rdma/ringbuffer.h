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

class RDMASenderReceiverBP;
class RDMASenderReceiverEvent;

// the data size of ringbuffer should <= capacity - 1, which means the ringbuffer cannot be full.
// if data size == capacity, then it is possible that remote_head == remote_tail,
// then remote cannot tell if there it is full or empty.
class RingBuffer {
  public:
    RingBuffer(size_t capacity);
    virtual ~RingBuffer();

    uint8_t* get_buf() { return buf_; }
    size_t get_capacity() { return capacity_; }
    size_t get_head() { return head_; }

  friend class RDMASenderReceiverBP;
  friend class RDMASenderReceiverEvent;
  protected:
    size_t update_head(size_t inc);

    uint8_t* buf_ = nullptr;
    size_t capacity_;
    size_t head_ = 0;

    size_t test_recv_head[1024 * 1024 * 16], test_recv_read_size[1024 * 1024 * 16];
    size_t test_recv_id = 0;
};

class RingBufferBP : public RingBuffer {
  public:
    RingBufferBP(size_t capacity) : RingBuffer(capacity) { bzero(zeros, 1024 * 1024 * 16); }

    // uint8_t check_head() { return buf_[head_]; }
    bool check_head();
    size_t check_mlens() { return check_mlens(head_); }

    size_t read_to_msghdr(msghdr* msg, size_t size) { return read_to_msghdr(msg, head_, size); }
    
  protected:
    uint8_t check_tail(size_t head, size_t mlen);
    size_t check_mlen(size_t head);
    size_t check_mlens(size_t head);

    // reset buf first then update head. Otherswise new head may read the old data from unupdated space.
    size_t reset_buf_and_update_head(size_t lens);

    // it guarantees to read out data of size expected_read_size 
    size_t read_to_msghdr(msghdr* msg, size_t head, size_t expected_read_size);

    uint8_t zeros[1024 * 1024 * 16];
};

class RingBufferEvent : public RingBuffer {
  public:
    RingBufferEvent(size_t capcatiy) : RingBuffer(capcatiy) {}

    size_t read_to_msghdr(msghdr* msg, size_t size) { return read_to_msghdr(msg, head_, size); }

  protected:
    size_t read_to_msghdr(msghdr* msg, size_t head, size_t expected_read_size);
};

#endif