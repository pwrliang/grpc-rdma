#ifndef _RDMASENDERRECEIVER_H_
#define _RDMASENDERRECEIVER_H_

#include <grpc/impl/codegen/log.h>
#include <sys/epoll.h>
#include <atomic>
#include <cstdlib>
#include <map>
#include <mutex>
#include <vector>
#include "RDMAUtils.h"
#include "log.h"
#include "ringbuffer.h"

// before turn off RDMA_IS_ON,
// make sure turn off RDMA_ZEROCOPY_IS_ON and RDMA_CONNECTION_SETUP_IS_ON
// in include/grpc/impl/codegen/grpc_types.h
#define RDMA_IS_ON
const static int rdma_mode =
    1;  // used in grpc project, to denote what rdma mode is used

class RDMASenderReceiver {
 public:
  const static size_t DEFAULT_RINGBUF_SZ = 1024ull * 1024 * 256;
  const static size_t DEFAULT_SENDBUF_SZ = 1024ull * 1024 * 256;
  const static size_t DEFAULT_SENDBUF_OFFSET_SZ = 1024;
  const static size_t DEFAULT_HEAD_SZ = 64;

  RDMASenderReceiver(RDMANode* node = nullptr);
  virtual ~RDMASenderReceiver();

  virtual void init_conn(int sd);
  int update_remote_head();
  void update_local_head();

  virtual bool check_incoming(bool flag);
  virtual int get_mlen();

  // send msg length of mlen to remote, success return 0, error return 1;
  virtual int send_msg(const char* msg, size_t mlen);
  virtual int send_msghdr(const msghdr* msg, size_t mlen);
  virtual int send_msghdr_zerocopy(const msghdr* msg, size_t mlen);

  // recv msg from remote, return recv length, return -1 if remote close
  virtual int recv_msg(char* msg);
  virtual int recv_msghdr(msghdr* msg);
  virtual int recv_msghdr_zerocopy(msghdr* msg);
  void zerocopy_reset_buf_update_remote_head();
  bool zerocopy_contains(char* bytes);

  // whether send_bye in destruction
  RDMANode* node_ = nullptr;
  RDMAConn* conn_ = nullptr;
  bool external_node_flag_ = true;
  ibv_wr_opcode opcode_;
  int fd_;

  size_t ringbuf_sz_, remote_ringbuf_head_, remote_ringbuf_tail_;
  RingBuffer* ringbuf_ = nullptr;
  MemRegion local_ringbuf_mr_, remote_ringbuf_mr_;
  size_t total_recv_bytes_ = 0;

  void* headbuf_ = nullptr;
  MemRegion local_headbuf_mr_, remote_headbuf_mr_;
  size_t local_version_ = 0;

  size_t sendbuf_sz_, sendbuf_offset_sz_;
  char *sendbuf_ = nullptr, *sendbuf_0cp_dataptr_ = nullptr;
  MemRegion sendbuf_mr_;
  uint8_t* zerocopy_last_head_ = nullptr;
  size_t total_send_bytes_ = 0;

  void* sendbuf_head_ = nullptr;
  MemRegion sendbuf_head_mr_;
  size_t remote_version_ = 1;

  size_t max_message_size_sofar_ = 0;
};

class RDMAEventSenderReceiver final : public RDMASenderReceiver {
 public:
  RDMAEventSenderReceiver(int epfd, RDMANode* node = nullptr);
  virtual ~RDMAEventSenderReceiver() {}

  static int* _epfds;
  static int _num_epfds;
  static void create_epfds(int num);

  void init_conn(int sd);
  int event_post_recv();
  bool check_incoming(bool flag);
  int get_mlen();

  int send_msg(const char* msg, size_t mlen);
  int send_msghdr(const msghdr* msg, size_t mlen);
  int send_msghdr_zerocopy(const msghdr* msg, size_t mlen);
  int recv_msg(char* msg);
  int recv_msghdr(msghdr* msg);
  int recv_msghdr_zerocopy(msghdr* msg);
  size_t _expected_recv_bytes = 0;
  int _epfd = 0;
};

#endif
