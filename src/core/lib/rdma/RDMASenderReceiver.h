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
const size_t DEFAULT_HEADBUF_SZ = 64;

class RDMASenderReceiver {
  public:

    RDMASenderReceiver();
    virtual ~RDMASenderReceiver();
    
    RDMANode* get_node() { return &node_; }
    size_t get_unread_data_size() { return unread_mlens_; }
    virtual size_t get_max_send_size() { return max_send_size_; }
    bool if_write_again() { return write_again_.load(); } // if previous is true, only one thread return true
    void write_again() { write_again_.store(true); }
    void write_again_done() { write_again_.store(false); }
  
  protected:
    virtual void connect(int fd);
    virtual void update_remote_metadata();
    virtual void update_local_metadata();

    static RDMANode node_;
    static std::atomic_bool node_opened_;
    static std::atomic_bool node_opened_done_;

    int fd_;
    RDMAConn* conn_data_ = nullptr; // no ownership 
    RDMAConn* conn_metadata_ = nullptr; 

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

    size_t metadata_recvbuf_sz_;
    void* metadata_recvbuf_ = nullptr;
    MemRegion local_metadata_recvbuf_mr_, remote_metadata_recvbuf_mr_;
    
    size_t metadata_sendbuf_sz_;
    void* metadata_sendbuf_ = nullptr;
    MemRegion metadata_sendbuf_mr_;
};


/*
 * 1. update_remote_metadata after garbage >= ringbuf_size_ / 2, so sendbuf_size_ <= ringbuf_size_ / 2.
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

class RDMASenderReceiverEvent : public RDMASenderReceiver {
  public:
    RDMASenderReceiverEvent();
    virtual ~RDMASenderReceiverEvent();

    // create channel for each rdmasr.
    virtual void connect(int fd); 
    virtual void update_remote_metadata() override;
    virtual void update_local_metadata() override;
    bool connected() { return connected_; }

    virtual bool send(msghdr* msg, size_t mlen);
    virtual size_t recv(msghdr* msg);

    void check_data() { check_data_.store(true); }
    void check_metadata() { check_metadata_.store(true); }

    bool get_write_flag() { return write_flag_; }
    void set_write_flag(bool flag) { write_flag_ = flag; }

    bool check_incoming();
    size_t check_and_ack_incomings_locked();
    void check_and_ack_meta_incoming_locked();
    size_t ack_outgoings();
    
    // int get_send_channel_fd() { return conn_data_event_->send_channel_->fd; }
    int get_recv_channel_fd() { return conn_data_event_->recv_channel_->fd; }
    int get_metadata_recv_channel_fd() { return conn_metadata_event_->recv_channel_->fd; }

    
  protected:
    RingBufferEvent* ringbuf_event_ = nullptr;
    RDMAConnEvent* conn_data_event_ = nullptr;
    RDMAConnEvent* conn_metadata_event_ = nullptr;
    bool connected_ = false;

    // this need to sync in initialization
    size_t remote_rr_tail_ = 0, remote_rr_head_ = 0;

    bool write_flag_ = false;
    std::atomic_bool check_data_;
    std::atomic_bool check_metadata_;
};

#endif