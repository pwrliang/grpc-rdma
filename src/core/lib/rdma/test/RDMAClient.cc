#include <fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <thread>
#include "../RDMASenderReceiver.h"
#include "SockUtils.h"
#include "netinet/tcp.h"
int random(int min, int max) {
  static bool first = true;
  if (first) {
    srand(time(NULL));  // seeding for the first time only!
    first = false;
  }
  return min + rand() % ((max + 1) - min);
}

class RDMAClient {
 public:
  RDMAClient() { sockfd_ = SocketUtils::socket(AF_INET, SOCK_STREAM, 0); }

  int connect(const char* server_address, int port) {
    server_address_ = server_address_;
    port_ = port;
    struct sockaddr_in serv_addr;
    char serv_ip[16];
    strcpy(serv_ip, server_address);
    bzero(&serv_addr, sizeof(serv_addr));
    SocketUtils::setAddr(serv_addr, serv_ip, port);

    int flag = 1;
    if (setsockopt(sockfd_, IPPROTO_TCP, TCP_NODELAY, (char*)&flag,
                   sizeof(flag))) {
      rdma_log(RDMA_ERROR,
               "RDMAClient::connect, error on setsockopt (TCP_NODELAY)");
      exit(-1);
    }
    printf("client connect to %s:%d\n", server_address, port);
    if (::connect(sockfd_, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) <
        0) {
      rdma_log(RDMA_ERROR, "RDMAClient::connect, error on connect");
      exit(-1);
    }
    return sockfd_;
  }

 private:
  int sockfd_;
  char* server_address_;
  int port_;
};

struct LastN {
  using T = size_t;
  void add(T e) {
    std::lock_guard<std::mutex> lg(mutex_);
    if (buf_.size() == limit) {
      for (int i = 0; i < limit - 1; i++) {
        buf_[i] = buf_[i + 1];
      }
      buf_[limit - 1] = e;
    } else {
      buf_.push_back(e);
    }
  }

  void print() {
    std::lock_guard<std::mutex> lg(mutex_);
    for (int i = 0; i < buf_.size(); i++) {
      printf("%2zu ", buf_[i]);
    }
    printf("\n");
  }
  size_t limit = 10;
  std::vector<T> buf_;
  std::mutex mutex_;
};

#define SEND_BUF_SZ (80)

int main(int argc, char* argv[]) {
  if (argc != 2) {
    printf("Bad param, need ip addr\n");
    exit(1);
  }
  RDMAClient client;
  int fd = client.connect(argv[1], 50050);

  RDMASenderReceiverBP rdmasr;
  rdmasr.connect(fd);
  printf("connection established\n");

  struct msghdr recv_msg;
  struct iovec recv_iov;
  std::mutex mutex;
  std::queue<std::vector<char>> sent_data;
  recv_msg.msg_iov = &recv_iov;
  recv_msg.msg_iovlen = 1;
  std::atomic_uint64_t sent_count;
  sent_count.store(0);

  LastN lastN, lastNrecv;

  std::thread th_recv = std::thread([&]() {
    size_t recv_count = 0;
    while (true) {
      auto msg_size = rdmasr.check_and_ack_incomings();

      if (msg_size > 0) {
        if (msg_size > SEND_BUF_SZ) {
          printf("Unexpected len: %zu vs %d", msg_size, SEND_BUF_SZ);
          assert(msg_size <= SEND_BUF_SZ);
        }
        std::vector<char> data_to_recv(msg_size);
        std::vector<char> should_recv;
        struct msghdr msg;
        struct iovec iov;

        msg.msg_iov = &iov;
        msg.msg_iovlen = 1;
        iov.iov_base = data_to_recv.data();
        iov.iov_len = msg_size;
        recv_count++;
        lastNrecv.add(msg_size);

        {
          std::lock_guard<std::mutex> lg(mutex);
          if (sent_data.empty()) {
            printf("Queue is empty, msg_len: %zu sent cnt: %llu recv cnt: %zu\n",
                   msg_size, sent_count.load(), recv_count);
            printf("Sender len\n");
            lastN.print();
            printf("Recv len\n");
            lastNrecv.print();
            assert(!sent_data.empty());
          }
          should_recv = sent_data.front();
          assert(should_recv.size() == msg_size);
          sent_data.pop();
        }

        auto read_size = rdmasr.recv(&msg);
        if (read_size != msg_size) {
          printf("%zu vs %zu\n", read_size, msg_size);
          assert(read_size != msg_size);
        }

        if (should_recv != data_to_recv) {
          printf("Recv wrong data, got\n");
          for (auto& e : data_to_recv) {
            printf("%2d ", e);
          }

          printf("\nShould be: \n");
          for (auto& e : should_recv) {
            printf("%2d ", e);
          }
          printf("\n");
          exit(1);
        }
      }
    }
  });
  th_recv.detach();

  while (true) {
    auto send_data_sz = random(1, SEND_BUF_SZ);
    std::vector<char> data_to_send(send_data_sz);

    for (int i = 0; i < data_to_send.size(); i++) {
      data_to_send[i] = i % 254 + 2;
    }

    struct msghdr send_msg;
    struct iovec send_iov;
    send_iov.iov_base = data_to_send.data();
    send_iov.iov_len = send_data_sz;
    send_msg.msg_iov = &send_iov;
    send_msg.msg_iovlen = 1;

    {
      std::lock_guard<std::mutex> lg(mutex);
      sent_data.push(data_to_send);
    }
    sent_count++;
    lastN.add(send_data_sz);

    while (!rdmasr.send(&send_msg, send_data_sz))
      ;
  }

  return 0;
}