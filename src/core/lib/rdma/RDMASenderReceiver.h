#ifndef _RDMASENDERRECEIVER_H_
#define _RDMASENDERRECEIVER_H_

#include <sys/epoll.h>
#include <atomic>
#include <cstdlib>
#include <map>
#include <mutex>
#include <vector>
#include "RDMAConn.h"
#include "ringbuffer.h"
#include "log.h"

const size_t DEFAULT_RINGBUF_SZ = 200;
const size_t DEFAULT_SENDBUF_SZ = 1024ull * 1024 * 16;
// we use headbuf_sz to mem align headbuf. don't set it too big
const size_t DEFAULT_HEADBUF_SZ = 64;

class RDMASenderReceiver {
  public:

    RDMASenderReceiver();
    virtual ~RDMASenderReceiver();
    
    RDMANode* get_node() { return &node_; }
    size_t get_unread_data_size() { return unread_data_size_; }
    virtual size_t get_max_send_size() { return max_send_size_; }
  
  protected:
    void update_remote_head();
    void update_local_head();

    static RDMANode node_;
    static std::atomic<bool> node_opened_;

    int fd_;
    RDMAConn* conn_ = nullptr;

    size_t ringbuf_sz_, remote_ringbuf_head_ = 0, remote_ringbuf_tail_ = 0;
    RingBuffer* ringbuf_ = nullptr;
    MemRegion local_ringbuf_mr_, remote_ringbuf_mr_;
    size_t garbage_ = 0;
    size_t unread_data_size_ = 0;
    unsigned long long int total_recv_sz = 0;

    size_t sendbuf_sz_;
    uint8_t* sendbuf_ = nullptr;
    MemRegion sendbuf_mr_;
    size_t max_send_size_ = 0;
    unsigned long long int total_send_sz = 0;

    size_t head_recvbuf_sz_;
    void* head_recvbuf_ = nullptr;
    MemRegion local_head_recvbuf_mr_, remote_head_recvbuf_mr_;
    
    size_t head_sendbuf_sz_;
    void* head_sendbuf_ = nullptr;
    MemRegion head_sendbuf_mr_;
    
    size_t test_send_id = 0;
    size_t test_send_to[1024*1024*16];
};


/*
 * 1. update_remote_head after garbage >= ringbuf_size_ / 2, so sendbuf_size_ <= ringbuf_size_ / 2.
 * 2. reset ringbuf fisrt, then update head.
 * 3. mlen: length of pure data; len: mlen + sizeof(size_t) + 1.
 */
class RDMASenderReceiverBP : public RDMASenderReceiver {
  public:
    RDMASenderReceiverBP();
    virtual ~RDMASenderReceiverBP();

    void connect(int fd);
    bool connected() { return connected_; }

    virtual bool send(msghdr* msg, size_t mlen);
    virtual size_t recv(msghdr* msg);

    // this should be thread safe, 
    bool check_incoming();

    size_t check_and_ack_incomings();

    void diagnosis();

  protected:
    RingBufferBP* ringbuf_bp_ = nullptr;
    RDMAConnBP* conn_bp_ = nullptr;
    bool connected_ = false;

    
};

ibv_comp_channel* rdma_create_channel(RDMANode* node);
int rdma_destroy_channel(ibv_comp_channel* channel);
void rdma_epoll_add_channel(int epfd, ibv_comp_channel* channel);
void rdma_epoll_del_channel(int epfd, ibv_comp_channel* channel);
bool rdma_is_available_event(struct epoll_event* ev);
void* rdma_check_incoming(struct epoll_event* ev);

class RDMASenderReceiverEvent : public RDMASenderReceiver {
  public:
    RDMASenderReceiverEvent();
    virtual ~RDMASenderReceiverEvent();

    // create channel for each rdmasr.
    void connect(int fd, void* user_data); // user_data will be used as cq_context when create srq 
    bool connected() { return connected_; }

    virtual bool send(msghdr* msg, size_t mlen);
    virtual size_t recv(msghdr* msg);

    size_t check_and_ack_incomings();
    
    void* get_user_data() { return user_data_; }
    ibv_comp_channel* get_channel() { return channel_; }

    friend void* rdma_check_incoming(struct epoll_event* ev);
  protected:
    RingBufferEvent* ringbuf_event_ = nullptr;
    RDMAConnEvent* conn_event_ = nullptr;
    ibv_comp_channel* channel_ = nullptr;
    std::atomic<int> unprocessed_event_num_;
    std::atomic<int> unacked_event_num_;
    bool connected_ = false;

    void* user_data_;
};

#endif