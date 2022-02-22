#include "../RDMASenderReceiver.h"
#include <unistd.h>
#include <thread>
#include "netinet/tcp.h"
#include "SockUtils.h"
#include <sys/socket.h>
#include <sys/stat.h>
#include <fcntl.h>

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
    RDMAClient() {
      sockfd_ = SocketUtils::socket(AF_INET, SOCK_STREAM, 0);
    }

    int connect(const char* server_address, int port) {
      server_address_ = server_address_;
      port_ = port;
      struct sockaddr_in serv_addr;
      char serv_ip[16];
      strcpy(serv_ip, server_address);
      bzero(&serv_addr, sizeof(serv_addr));
      SocketUtils::setAddr(serv_addr, serv_ip, port);

      int flag = 1;
      if (setsockopt(sockfd_, IPPROTO_TCP, TCP_NODELAY, (char *)&flag, sizeof(flag))) {
        rdma_log(RDMA_ERROR, "RDMAClient::connect, error on setsockopt (TCP_NODELAY)");
        exit(-1);
      }
      printf("client connect to %s:%d\n", server_address, port);
      if (::connect(sockfd_, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
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

#define SEND_BUF_SZ (8*1000*1000)
#define BATCH_SZ (100000)

static long long total_send_size = 0;
static size_t send_id = 0;

void send_thread_bp(RDMASenderReceiverBP* rdmasr) {
  uint8_t *send_data = new uint8_t[SEND_BUF_SZ];
  read(open("/dev/random", O_RDONLY), send_data, SEND_BUF_SZ);
  size_t send_data_sz;
  struct msghdr send_msg;
  struct iovec send_iov;
  send_iov.iov_base = send_data;
  send_msg.msg_iov = &send_iov;
  send_msg.msg_iovlen = 1;

  // long long total_sent_sz = 0;
  for (int i = 0; i < BATCH_SZ; i++) {
    send_data_sz = random(SEND_BUF_SZ / 2, SEND_BUF_SZ);
    send_iov.iov_len = send_data_sz;
    if (rdmasr->send(&send_msg, send_data_sz) == false) {
      printf("%d-th send failed\n", i);
      exit(-1);
    }
    total_send_size += send_data_sz;
    // printf("%d-th send %d bytes, total sent size = %lld\n", i, send_data_sz, total_sent_sz);
    send_id++;
  }
  printf("send complete\n");
  delete send_data;
}

int main(int argc, char *argv[]) {
  RDMAClient client;
  int fd = client.connect("10.3.1.1", 50051);

  RDMASenderReceiverBP rdmasr;
  rdmasr.connect(fd);
  printf("connection established\n");

  std::thread send_thread(send_thread_bp, &rdmasr);

  struct msghdr recv_msg;
  struct iovec recv_iov;
  recv_msg.msg_iov = &recv_iov;
  recv_msg.msg_iovlen = 1;
  uint8_t *recv_buf = new uint8_t[1024 * 1024 * 16];
  size_t batch_sz = BATCH_SZ;
  size_t send_buf_sz = SEND_BUF_SZ;

  long long check_point = send_buf_sz * 100;
  long long check_point_inc = check_point;

  size_t id = 0;
  long long total_recv_sz = 0;
  long long target_recv_sz = batch_sz * send_buf_sz;
  while (true) {
    while (rdmasr.check_incoming() == false || rdmasr.check_and_ack_incomings() == 0) {}
    int check_size = rdmasr.check_and_ack_incomings();
    recv_iov.iov_base = recv_buf;
    recv_iov.iov_len = check_size;
    // printf("\t %d-th data incoming %d\n", id, check_size);
    int read_size = rdmasr.recv(&recv_msg);
    total_recv_sz += read_size;
    // printf("\t %d-th recv %d bytes, total recv size = %lld\n", 
    //       id, read_size, total_recv_sz);

    if (id % 500 == 0) {
      printf("%d-th, total recv %lld bytes, total send (%d times, %lld bytes)\n", 
              id, total_recv_sz, send_id, total_send_size);
      check_point += check_point_inc;
    }

    if (total_recv_sz == total_send_size && send_id == BATCH_SZ) {
      printf("recv complete, total recv %lld size, id = %d, total send %d times\n", 
              total_recv_sz, id, send_id);
      break;
    }
    id++;
  }

  delete recv_buf;
  send_thread.join();

  // sleep(10);

  return 0;
}