#ifndef _RINGBUFFER_H_
#define _RINGBUFFER_H_
#include <grpc/impl/codegen/log.h>
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
#include "RDMAUtils.h"
#include "log.h"

typedef size_t msglen_t;
typedef int8_t tailtag_t;

class RingBuffer {
 public:
  RingBuffer(msglen_t capacity);
  virtual ~RingBuffer();

  void reset_head();
  msglen_t check_mlen(uint8_t* head);
  msglen_t check_mlen();
  tailtag_t check_tail(uint8_t* head, msglen_t mlen);

  // move _head according to its mlen
  uint8_t* update_head();

  // reset _buf from head until _head, argument will be changed.
  msglen_t reset_buf_from(uint8_t* head);

  // reset_buf_from(update_head())
  msglen_t reset_buf_update_head();

  // read incoming data, copy to message, return mlen
  msglen_t get_msg(void* message);
  msglen_t get_msghdr(msghdr* message);
  msglen_t get_msghdr_zerocopy(msghdr* message);

  uint8_t* get_buf();
  msglen_t get_buf_size();
  uint8_t* get_head();

  int fd_;

 private:
  uint8_t* buf_;
  uint8_t* head_;
  uint8_t* end_;
  msglen_t size_;
  bool exit_flag_ = false;

  msglen_t last_recv_ = 0;
};

#endif