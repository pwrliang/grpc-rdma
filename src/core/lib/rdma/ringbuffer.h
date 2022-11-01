#ifndef GRPC_CORE_LIB_RDMA_RINGBUFFER_H
#define GRPC_CORE_LIB_RDMA_RINGBUFFER_H
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
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <thread>
#include "grpcpp/get_clock.h"
#include "rdma_conn.h"

#include "grpc/impl/codegen/log.h"

typedef enum {
  GS1KB,
  GS32KB,
  GS1MB,
  GS4MB,
  GS16MB,
  GS64MB,
  GS256MB,
  GS_CAP_GENRES,
} rdma_global_sendbuf_capacity_genre;

class GlobalSendBufferManager;

class GlobalSendBuffer {
 public:
  GlobalSendBuffer(size_t capacity, int id);
  ~GlobalSendBuffer();

  uint8_t* get_buf() const { return buf_; }

  size_t get_capacity() const { return capacity_; }

  size_t get_used() const { return used_; }

  int get_genre_id() const { return genre_id_; }

  MemRegion& get_mr() { return mr_; }

 private:
  friend class GlobalSendBufferManager;
  uint8_t* buf_;
  uint8_t* end_;
  size_t capacity_;
  int genre_id_;
  size_t used_ = 0;
  MemRegion mr_;
};

class GlobalSendBufferManager {
 public:
  GlobalSendBufferManager();
  ~GlobalSendBufferManager();

  static GlobalSendBufferManager& GetInstance() {
    static GlobalSendBufferManager gsbm;
    return gsbm;
  }

  uint8_t* alloc(size_t size);

  bool free(uint8_t* buf);

  GlobalSendBuffer* contains(uint8_t* ptr);

  bool add_link(uint8_t* head, uint8_t* buf);

  bool remove_link(uint8_t* head);

 private:
  std::mutex mtx_;
  std::map<uint8_t*, GlobalSendBuffer*> all_bufs_;
  std::map<uint8_t*, GlobalSendBuffer*> free_bufs_[GS_CAP_GENRES];
  std::map<uint8_t*, GlobalSendBuffer*> used_bufs_[GS_CAP_GENRES];
  std::map<uint8_t*, uint8_t*>
      linkers_;  // first is head, second is buf: buf < head < buf + capacity

  std::atomic_size_t alloc_num_[GS_CAP_GENRES];
  std::atomic_size_t failed_alloc_num_[GS_CAP_GENRES + 1];
};

void mt_memcpy(void* dest, const void* src, size_t size);
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
    mhz_ = get_cpu_mhz(0);
  }
  virtual ~RingBuffer() { delete[] buf_; }

  const uint8_t* get_buf() const { return buf_; }

  uint8_t* get_buf() { return buf_; }

  size_t get_head() const { return head_; }

  virtual size_t get_sendbuf_size() const = 0;

  virtual size_t get_max_send_size() const = 0;

  size_t get_garbage() const { return garbage_; }

  size_t get_capacity() const { return capacity_; }

  /**
   * Read data from ringbuffer
   * @param msg
   * @param expected_lens maximum length of data to read. This size can not
   * exceed the data size of actually data in ring buffer. After reading, this
   * variable will be changed to actual read size.
   * @return Return true if the size of garbage exceeds half of ring buffer.
   */
  virtual bool Read(msghdr* msg, size_t& expected_lens) = 0;

  virtual size_t Read(msghdr* msg, bool& recycle) = 0;

  void Dump(const char* path) {
    std::ofstream b_stream(path, std::fstream::out | std::fstream::binary);

    if (b_stream) {
      b_stream.write(reinterpret_cast<char const*>(buf_), capacity_);
      GPR_ASSERT(b_stream.good());
    }
  }

 protected:
  size_t updateHead(size_t inc) {
    head_ = (head_ + inc) % capacity_;
    return head_;
  }

  size_t capacity_;
  uint8_t* buf_;
  size_t head_;
  size_t garbage_;
  double mhz_;
};

class RingBufferBP : public RingBuffer {
 public:
  explicit RingBufferBP(size_t capacity) : RingBuffer(capacity) {
    GPR_ASSERT(capacity_ > 33);  // ensure get_max_send_size() > 0
  }

  size_t CheckMessageLength() const { return checkMessageLength(head_); }

  size_t CheckFirstMessageLength() const {
    return checkFirstMesssageLength(head_);
  }

  bool Read(msghdr* msg, size_t& expected_lens) override;

  size_t Read(msghdr* msg, bool& recycle) override;

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
  // reset buf first then update head. Otherswise new head may read the old data
  // from unupdated space.
  size_t resetBufAndUpdateHead(size_t lens);

  uint8_t checkTail(size_t head, size_t mlen) const;

  size_t checkFirstMesssageLength(size_t head) const;

  size_t checkMessageLength(size_t head) const;
};

class RingBufferEvent : public RingBuffer {
 public:
  explicit RingBufferEvent(size_t capcatiy) : RingBuffer(capcatiy) {
    GPR_ASSERT(capacity_ > 1);  // ensure get_max_send_size() > 0
  }

  bool Read(msghdr* msg, size_t& size) override;

  size_t Read(msghdr* msg, bool& recycle) override;

  size_t get_sendbuf_size() const override {
    /*
     * Event: garbage max R/2 - 1, available R-1, send = R-1 - (R/2-1) = R/2
     */
    return capacity_ / 2;
  }

  size_t get_max_send_size() const override { return get_sendbuf_size(); }
};
#endif  // GRPC_CORE_LIB_RDMA_RINGBUFFER_H