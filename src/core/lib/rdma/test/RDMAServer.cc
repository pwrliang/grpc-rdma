#include <netinet/tcp.h>
#include <sys/socket.h>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include "../RDMASenderReceiver.h"
#include "SockUtils.h"
#include "thpool.h"

#define MAX_CONN_NUM 10
#define MAX_THREAD_NUM 10
#define DEFAULT_PORT 50050
#define MAX_BUF_SZ (1024 * 1024 * 8)

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
      rdma_log(RDMA_ERROR,
               "RDMAServer::RDMAServer, error on setsockopt (SO_REUSEADDR)");
      exit(-1);
    }
    opt = 1;
    if (setsockopt(sockfd_, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt))) {
      rdma_log(RDMA_ERROR,
               "RDMAServer::RDMAServer, error on setsockopt (SO_REUSEPORT)");
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
    if (::bind(sockfd_, (struct sockaddr*)&server_addr, sizeof(server_addr)) <
        0) {
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
    const char* addr_buf_ptr;
    int client_port, id = 0;
    while (true) {
      int newsd = accept(sockfd_, &client_sockaddr, &addr_len);
      if (newsd < 0) {
        rdma_log(RDMA_ERROR, "RDMAServer::start, error on accept");
        exit(-1);
      }
      switch (client_sockaddr.sa_family) {
        case AF_INET:
          addr_buf_ptr = inet_ntop(
              AF_INET, &(((struct sockaddr_in*)&client_sockaddr)->sin_addr),
              addr_buf, 1024);
          client_port = ((struct sockaddr_in*)&client_sockaddr)->sin_port;
          break;
        case AF_INET6:
          addr_buf_ptr = inet_ntop(
              AF_INET6, &(((struct sockaddr_in6*)&client_sockaddr)->sin6_addr),
              addr_buf, 1024);
          client_port = ((struct sockaddr_in6*)&client_sockaddr)->sin6_port;
          break;
      }
      // rdma_log(RDMA_INFO, "TCPServer::start, accept connection from %s:%d
      // newsd = %d",
      //                      addr_buf_ptr, client_port, newsd);

      printf("TCPServer::start, accept connection from %s:%d newsd = %d\n",
             addr_buf_ptr, client_port, newsd);
      int flag = 1;
      setsockopt(newsd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));
      thread_handler_args* hargs =
          (thread_handler_args*)malloc(sizeof(thread_handler_args));
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

static void* thread_handler_bp(void* args) {
  thread_handler_args* hargs = (thread_handler_args*)args;
  int sockfd = hargs->sockfd;
  RDMASenderReceiverBP* rdmasr = new RDMASenderReceiverBP();

  rdmasr->connect(sockfd);
  printf("%d-th connection established\n", hargs->id);
  uint8_t* recv_buf = new uint8_t[1024 * 1024 * 16];

  std::atomic_bool updated;
  updated.store(false);
  std::thread th_monitor = std::thread([&]() {
    while (true) {
      sleep(2);
      if (!updated.load()) {
        printf("Stuck\n");
        exit(1);
      } else {
        updated.store(false);
      }
    }
  });

  while (true) {
    auto msg_size = rdmasr->check_and_ack_incomings();

    if (msg_size > 0) {
      struct msghdr msg;
      struct iovec iov;

      msg.msg_iov = &iov;
      msg.msg_iovlen = 1;
      iov.iov_base = recv_buf;
      iov.iov_len = msg_size;
      if (msg_size >= DEFAULT_RINGBUF_SZ) {
        printf("wrong len: %zu\n", msg_size);
      }
      auto read_size = rdmasr->recv(&msg);

      if (read_size != msg_size) {
        printf("%zu vs %zu\n", read_size, msg_size);
        exit(1);
      }
      struct msghdr send_msg;
      struct iovec send_iov;
      send_iov.iov_base = recv_buf;
      send_iov.iov_len = msg_size;
      send_msg.msg_iov = &send_iov;
      send_msg.msg_iovlen = 1;

      while (rdmasr->send(&send_msg, msg_size) == false) {
      }
      updated.store(true);
    }
  }

  delete[] recv_buf;
  return nullptr;
}

static void* thread_handler_event(void* args) {
  thread_handler_args* hargs = (thread_handler_args*)args;
  int sockfd = hargs->sockfd;
  RDMASenderReceiverEvent* rdmasr = new RDMASenderReceiverEvent();

  // rdmasr->connect(sockfd);
  // printf("%d-th connection established\n", hargs->id);

}

int main(int argc, char* argv[]) {
  RDMAServer server;
  server.attach(thread_handler_bp);
  server.start(DEFAULT_PORT);
  return 0;
}