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

const size_t DEFAULT_RINGBUF_SZ = 1024ull * 1024 * 16;
const size_t DEFAULT_SENDBUF_SZ = 1024ull * 1024 * 16;
// we use headbuf_sz to mem align headbuf. don't set it too big
const size_t DEFAULT_HEADBUF_SZ = 64;

class RDMASenderReceiver {
  public:

    RDMASenderReceiver();
    virtual ~RDMASenderReceiver();
    
    RDMANode* get_node() { return &node_; }
    size_t get_unread_data_size() { return unread_mlens_; }
    virtual size_t get_max_send_size() { return max_send_size_; }
    bool if_write_again() { return write_again_.exchange(false); } // if previous is true, only one thread return true
    void write_again() { write_again_.store(true); }
  
  protected:
    virtual void connect(int fd);
    void update_remote_head();
    void update_local_head();

    static RDMANode node_;
    static std::atomic_bool node_opened_;
    static std::atomic_bool node_opened_done_;

    int fd_;
    RDMAConn* conn_data_ = nullptr; // no ownership 
    RDMAConn* conn_head_ = nullptr; 

    size_t ringbuf_sz_, remote_ringbuf_head_ = 0, remote_ringbuf_tail_ = 0;
    RingBuffer* ringbuf_ = nullptr; // no ownership 
    MemRegion local_ringbuf_mr_, remote_ringbuf_mr_;
    size_t garbage_ = 0;
    size_t unread_mlens_ = 0;
    unsigned long long int total_recv_sz = 0;

    size_t sendbuf_sz_;
    uint8_t* sendbuf_ = nullptr;
    MemRegion sendbuf_mr_;
    size_t max_send_size_ = 0;
    unsigned long long int total_send_sz = 0;
    std::atomic<bool> write_again_;

    size_t head_recvbuf_sz_;
    void* head_recvbuf_ = nullptr;
    MemRegion local_head_recvbuf_mr_, remote_head_recvbuf_mr_;
    
    size_t head_sendbuf_sz_;
    void* head_sendbuf_ = nullptr;
    MemRegion head_sendbuf_mr_;
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

    virtual void connect(int fd);
    bool connected() { return connected_; }

    virtual bool send(msghdr* msg, size_t mlen);
    virtual size_t recv(msghdr* msg, size_t msghdr_size);

    // this should be thread safe, 
    bool check_incoming();

    bool check_incoming_() { return ringbuf_bp_->check_head(); }

    size_t check_and_ack_incomings_locked();

    void diagnosis();

  protected:
    std::atomic_bool checked_; // there is a thread already found new incoming data
    RingBufferBP* ringbuf_bp_ = nullptr;
    RDMAConnBP* conn_data_bp_ = nullptr;
    bool connected_ = false;
};

// ibv_comp_channel* rdma_create_channel(RDMANode* node);
// int rdma_destroy_channel(ibv_comp_channel* channel);
// void rdma_epoll_add_channel(int epfd, ibv_comp_channel* channel);
// void rdma_epoll_del_channel(int epfd, ibv_comp_channel* channel);
// bool rdma_is_available_event(struct epoll_event* ev);
// void* rdma_check_incoming(struct epoll_event* ev);

class RDMASenderReceiverEvent : public RDMASenderReceiver {
  public:
    RDMASenderReceiverEvent();
    virtual ~RDMASenderReceiverEvent();

    // create channel for each rdmasr.
    void connect(int fd); 
    bool connected() { return connected_; }

    virtual bool send(msghdr* msg, size_t mlen);
    virtual size_t recv(msghdr* msg);

    bool check_incoming();
    size_t check_and_ack_incomings_locked();
    
    int get_channel_fd() { return conn_data_event_->get_channel_fd(); }
    
  protected:
    std::atomic_bool checked_;
    RingBufferEvent* ringbuf_event_ = nullptr;
    RDMAConnEvent* conn_data_event_ = nullptr;
    bool connected_ = false;
};

#endif