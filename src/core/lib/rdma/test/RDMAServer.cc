#include "../RDMASenderReceiver.h"
#include "Utils.h"
#include "SockUtils.h"
#include "thpool.h"
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <condition_variable>
#include <mutex>
#include <chrono>
#include <thread>

#define MAX_CONN_NUM 10
#define MAX_THREAD_NUM 10
#define DEFAULT_PORT 50050
#define MAX_BUF_SZ  (1024 * 1024 * 8)

class RDMAServer;

typedef struct thread_handler_args {
  int sockfd;
  int id;
} thread_handler_args;

typedef void* (*thread_handler)(void* args);

class RDMAServer {
  public:
    RDMAServer() {
      sockfd_ = SocketUtils::socket(AF_INET, SOCK_STREAM, 0);
      int opt = 1;
      if (setsockopt(sockfd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
          rdma_log(RDMA_ERROR, "RDMAServer::RDMAServer, error on setsockopt (SO_REUSEADDR)");
          exit(-1);
      }
      opt = 1;
      if (setsockopt(sockfd_, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt))) {
          rdma_log(RDMA_ERROR, "RDMAServer::RDMAServer, error on setsockopt (SO_REUSEPORT)");
          exit(-1);
      }

      pool_ = thpool_init(MAX_THREAD_NUM);
      if (!pool_) {
        rdma_log(RDMA_ERROR, "RDMAServer::RDMAServer, error on thpool_init");
        exit(-1);
      }
    }

    void attach(thread_handler handler) { handler_ = handler; }

    void start(int port) {
      port_ = port;
      struct sockaddr_in server_addr;
      bzero(&server_addr, sizeof(struct sockaddr_in));
      SocketUtils::setAddr(server_addr, port);
      if (::bind(sockfd_, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
          rdma_log(RDMA_ERROR, "RDMAServer::start, error on bind");
          exit(-1);
      }
      if (::listen(sockfd_, MAX_CONN_NUM) < 0) {
          rdma_log(RDMA_ERROR, "RDMAServer::start, error on listen");
          exit(-1);
      }

      struct sockaddr client_sockaddr;
      socklen_t addr_len = sizeof(client_sockaddr);
      char addr_buf[1024];
      const char *addr_buf_ptr;
      int client_port, id = 0;
      while (true) {
        int newsd = accept(sockfd_, &client_sockaddr, &addr_len);
        if (newsd < 0) {
          rdma_log(RDMA_ERROR, "RDMAServer::start, error on accept");
          exit(-1);
        }
        switch (client_sockaddr.sa_family) {
          case AF_INET:
            addr_buf_ptr = inet_ntop(AF_INET, &(((struct sockaddr_in *)&client_sockaddr)->sin_addr), addr_buf, 1024);
            client_port = ((struct sockaddr_in *)&client_sockaddr)->sin_port;
            break;
          case AF_INET6:
            addr_buf_ptr = inet_ntop(AF_INET6, &(((struct sockaddr_in6 *)&client_sockaddr)->sin6_addr), addr_buf, 1024);
            client_port = ((struct sockaddr_in6 *)&client_sockaddr)->sin6_port;
            break;
        }
        // rdma_log(RDMA_INFO, "TCPServer::start, accept connection from %s:%d newsd = %d",
        //                      addr_buf_ptr, client_port, newsd);

        printf("TCPServer::start, accept connection from %s:%d newsd = %d\n",
                             addr_buf_ptr, client_port, newsd);
        int flag = 1;
        setsockopt(newsd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));
        thread_handler_args *hargs = (thread_handler_args *)malloc(sizeof(thread_handler_args));
        hargs->sockfd = newsd;
        hargs->id = id++;
        thpool_add_work(pool_, handler_, (void*)hargs);
      }
    }

  private:
    int sockfd_;
    int port_;
    threadpool pool_;
    thread_handler handler_;
};

static size_t send_recv_timeout_s = 10;
std::condition_variable send_recv_timer;
std::mutex send_recv_mtx;
bool send_recv_timer_start = false, sender_recver_stop = false, sender_recver_alive = true;
int send1recv2=0;
size_t send_size;

static void send_recv_diagnosis(RDMASenderReceiverBP* rdmasr) {
  std::unique_lock<std::mutex> lck(send_recv_mtx);

  while (sender_recver_alive) {
    while (!send_recv_timer_start) { std::this_thread::yield(); }

    if (send_recv_timer.wait_for(lck, std::chrono::seconds(send_recv_timeout_s)) == std::cv_status::no_timeout) {
      continue;
    }

    // timeout
    sender_recver_stop = true;
    switch (send1recv2) {
      case 1:
        printf("\n\nsender stopped, send size = %d\n", send_size);
        break;
      case 2:
        printf("\n\nrecver stopped\n");
        break;
    }
    // printf("sender or recver stopped\n");
    // while (sender_recver_stop) {
    //   std::this_thread::yield();
    // }
    rdmasr->diagnosis();
    while (sender_recver_stop) {}
  }
}

static void* thread_handler_bp(void* args) {
      thread_handler_args *hargs = (thread_handler_args *)args;
      int sockfd= hargs->sockfd;
      RDMASenderReceiverBP* rdmasr = new RDMASenderReceiverBP();
      rdmasr->connect(sockfd);
      printf("%d-th connection established\n", hargs->id);
      // uint8_t *recv_buf = new uint8_t[MAX_BUF_SZ];
      struct msghdr msg;
      struct iovec iov;
      msg.msg_iov = &iov;
      msg.msg_iovlen = 1;
      uint8_t *recv_buf = new uint8_t[1024 * 1024 * 16];

      std::thread diagnosis(send_recv_diagnosis, rdmasr);

      size_t id = 0;
      while (true) {

        send1recv2 = 2;
        send_recv_timer_start = true;
        while (rdmasr->check_and_ack_incomings() == 0) {
          while(sender_recver_stop) { 
            std::this_thread::yield(); 
          }
        }
        send_recv_timer_start = false;
        send_recv_timer.notify_one();

        int check_size = rdmasr->check_and_ack_incomings();
        iov.iov_base = recv_buf;
        iov.iov_len = check_size;
        // printf("%d-th data incoming %d\n", id, check_size);
        int read_size = rdmasr->recv(&msg);

        int send_length = read_size;
        while (send_length > 0) {
          size_t n = MIN(send_length, rdmasr->get_max_send_size());
          send_length -= n;
          iov.iov_len = n;

          send1recv2 = 1;
          send_size = n;
          send_recv_timer_start = true;
          while (rdmasr->send(&msg, n) == false) {
            while(sender_recver_stop) { 
              std::this_thread::yield(); 
            }
          }
          send_recv_timer_start = false;
          send_recv_timer.notify_one();

          // printf("%d-th send %d bytes\n", id, n);
          iov.iov_base = (uint8_t*)(iov.iov_base) + n;
        }
        // printf("%d-th send %d bytes\n\n", id, read_size);

        id++;
      }

      sender_recver_alive = false;
      diagnosis.join();
      delete recv_buf;
      return nullptr;
    }

int main(int argc, char *argv[]) {
  RDMAServer server;
  server.attach(thread_handler_bp);
  server.start(DEFAULT_PORT);
  return 0;
}