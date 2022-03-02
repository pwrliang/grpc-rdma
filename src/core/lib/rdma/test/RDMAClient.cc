#include "../RDMASenderReceiver.h"
#include <unistd.h>
#include <thread>
#include "netinet/tcp.h"
#include "SockUtils.h"
#include <sys/socket.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <condition_variable>
#include <mutex>
#include <chrono>


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

#define SEND_BUF_SZ (1024 * 1024 * 6)
#define BATCH_SZ (100000)

static long long total_send_size = 0;
static size_t send_id = 0;
static size_t send_timeout_s = 10, recv_timeout_s = 10;
std::condition_variable send_timer, recv_timer;
std::mutex send_mtx, recv_mtx;
bool send_timer_start = false, sender_stop = false, sender_alive = true;
bool recv_timer_start = false, recver_stop = false, recver_alive = true;
size_t send_size;

static void send_diagnosis(RDMASenderReceiver* rdmasr) {
  std::unique_lock<std::mutex> lck(send_mtx);

  while (sender_alive) {
    while (!send_timer_start) { std::this_thread::yield(); }
    if (!sender_alive) return;

    if (send_timer.wait_for(lck, std::chrono::seconds(send_timeout_s)) == std::cv_status::no_timeout) {
      continue;
    }

    // timeout
    sender_stop = true;
    printf("\n\nsender stopped, send size = %d\n", send_size);
    // while (sender_stop) {
    //   std::this_thread::yield();
    // }
    // rdmasr->diagnosis();
    sleep(10);
    while (sender_stop) {}
  }
}

static void recv_diagnosis(RDMASenderReceiver* rdmasr) {
  std::unique_lock<std::mutex> lck(recv_mtx);

  while (recver_alive) {
    while (!recv_timer_start) { std::this_thread::yield(); }
    if (!recver_alive) return;

    if (recv_timer.wait_for(lck, std::chrono::seconds(recv_timeout_s)) == std::cv_status::no_timeout) {
      continue;
    }

    // timeout
    recver_stop = true;
    printf("\n\nrecver stopped\n");
    // printf("recver stopped\n");
    // while (recver_stop) {
    //   std::this_thread::yield();
    // }
    // rdmasr->diagnosis();
    sleep(10);
    while (recver_stop) {}
  }
}


void send_thread_bp(RDMASenderReceiverBP* rdmasr) {
  uint8_t *send_data = new uint8_t[SEND_BUF_SZ];
  // read(open("/dev/random", O_RDONLY), send_data, SEND_BUF_SZ);
  for (size_t i = 0; i < SEND_BUF_SZ; i++) send_data[i] = (i % 254) + 2;
  // memset(send_data, 10, SEND_BUF_SZ);
  size_t send_data_sz;
  struct msghdr send_msg;
  struct iovec send_iov;
  send_iov.iov_base = send_data;
  send_msg.msg_iov = &send_iov;
  send_msg.msg_iovlen = 1;

  std::thread diagnosis(send_diagnosis, rdmasr);

  // long long total_sent_sz = 0;
  for (int i = 0; i < BATCH_SZ; i++) {
    send_data_sz = random(1, SEND_BUF_SZ);
    send_iov.iov_len = send_data_sz;

    send_size = send_data_sz;
    send_timer_start = true;
    while (rdmasr->send(&send_msg, send_data_sz) == false) {
      while(sender_stop) { 
        std::this_thread::yield(); 
      }
    }
    send_timer_start = false;
    send_timer.notify_one();

    total_send_size += send_data_sz;
    // printf("%d-th send %d bytes, total sent size = %lld\n", i, send_data_sz, total_sent_sz);
    send_id++;
  }
  printf("send complete\n");

  sender_alive = false;
  send_timer_start = true;
  diagnosis.join();
  delete send_data;
}

void send_thread_event(RDMASenderReceiverEvent* rdmasr) {
  uint8_t *send_data = new uint8_t[SEND_BUF_SZ];
  // read(open("/dev/random", O_RDONLY), send_data, SEND_BUF_SZ);
  for (size_t i = 0; i < SEND_BUF_SZ; i++) send_data[i] = (i % 254) + 2;
  // memset(send_data, 10, SEND_BUF_SZ);
  size_t send_data_sz;
  struct msghdr send_msg;
  struct iovec send_iov;
  send_iov.iov_base = send_data;
  send_msg.msg_iov = &send_iov;
  send_msg.msg_iovlen = 1;

  std::thread diagnosis(send_diagnosis, rdmasr);

  // long long total_sent_sz = 0;
  for (int i = 0; i < BATCH_SZ; i++) {
    send_data_sz = random(1, SEND_BUF_SZ);
    send_iov.iov_len = send_data_sz;

    send_size = send_data_sz;
    send_timer_start = true;
    while (rdmasr->send(&send_msg, send_data_sz) == false) {
      while(sender_stop) { 
        std::this_thread::yield(); 
      }
    }
    send_timer_start = false;
    send_timer.notify_one();

    total_send_size += send_data_sz;
    // printf("%d-th send %d bytes, total sent size = %lld\n", i, send_data_sz, total_sent_sz);
    send_id++;
  }
  printf("send complete, total send %lld size\n", total_send_size);

  sender_alive = false;
  send_timer_start = true;
  diagnosis.join();
  delete send_data;
}

int main(int argc, char *argv[]) {
  RDMAClient client;
  int fd = client.connect("10.3.1.6", 50050);
  int epfd = epoll_create1(EPOLL_CLOEXEC);

  RDMASenderReceiverEvent rdmasr;
  rdmasr.connect(fd, reinterpret_cast<void*>(&fd));
  rdma_epoll_add_channel(epfd, rdmasr.get_channel());
  printf("connection established\n");

  std::thread send_thread(send_thread_event, &rdmasr);

  struct msghdr recv_msg;
  struct iovec recv_iov;
  recv_msg.msg_iov = &recv_iov;
  recv_msg.msg_iovlen = 1;
  uint8_t *recv_buf = new uint8_t[1024 * 1024 * 16];
  epoll_event ep_events[100];
  size_t batch_sz = BATCH_SZ;
  size_t send_buf_sz = SEND_BUF_SZ;

  long long check_point = send_buf_sz * 100;
  long long check_point_inc = check_point;

  size_t id = 0;
  long long total_recv_sz = 0;
  long long target_recv_sz = batch_sz * send_buf_sz;

  std::thread diagnosis(recv_diagnosis, &rdmasr);
  while (true) {
    recv_timer_start = true;
    int r = 0;
    do {
      r = epoll_wait(epfd, ep_events, 100, 0);
      while (recver_stop) {
        std::this_thread::yield(); 
      }
    } while ((r < 0 && errno == EINTR) || r == 0);
    recv_timer_start = false;
    recv_timer.notify_one();

    bool readable_flag = false;
    for (size_t i = 0; i < r; i++) {
      epoll_event* ev = &ep_events[i];
      if (!rdma_is_available_event(ev)) continue;
      int* fd = reinterpret_cast<int*>(rdma_check_incoming(ev));
      if (fd == nullptr) continue;
      readable_flag = true;
    }

    if (readable_flag == false || rdmasr.check_and_ack_incomings() == 0) continue;
    size_t check_size = rdmasr.get_unread_data_size();
    if (check_size >= DEFAULT_RINGBUF_SZ) {
      printf("wrong len: %zu\n", check_size);
    }
    recv_iov.iov_base = recv_buf;
    recv_iov.iov_len = check_size;
    // printf("\t %d-th data incoming %d\n", id, check_size);
    int read_size = rdmasr.recv(&recv_msg);
    total_recv_sz += read_size;

    if (id % 1000 == 0) {
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

  recver_alive = false;
  recv_timer_start = true;
  diagnosis.join();
  delete recv_buf;


  send_thread.join();

  rdma_epoll_del_channel(epfd, rdmasr.get_channel());

  return 0;
}

// int main(int argc, char *argv[]) {

//   RDMAClient client;
//   int fd = client.connect("10.3.1.11", 50050);

//   RDMASenderReceiverBP rdmasr;
//   rdmasr.connect(fd);
//   printf("connection established\n");

//   std::thread send_thread(send_thread_bp, &rdmasr);

//   struct msghdr recv_msg;
//   struct iovec recv_iov;
//   recv_msg.msg_iov = &recv_iov;
//   recv_msg.msg_iovlen = 1;
//   uint8_t *recv_buf = new uint8_t[1024 * 1024 * 16];
//   size_t batch_sz = BATCH_SZ;
//   size_t send_buf_sz = SEND_BUF_SZ;

//   long long check_point = send_buf_sz * 100;
//   long long check_point_inc = check_point;

//   size_t id = 0;
//   long long total_recv_sz = 0;
//   long long target_recv_sz = batch_sz * send_buf_sz;

//   // std::condition_variable timer;
//   // std::mutex mtx;
//   // std::atomic<bool> started(false), stop(false);

//   std::thread diagnosis(recv_diagnosis, &rdmasr);
//   while (true) {

//     recv_timer_start = true;
//     while (rdmasr.check_incoming() == false || rdmasr.check_and_ack_incomings() == 0) {
//       while(recver_stop) { std::this_thread::yield(); }
//     }
//     recv_timer_start = false;
//     recv_timer.notify_one();

//     int check_size = rdmasr.check_and_ack_incomings();
//     recv_iov.iov_base = recv_buf;
//     recv_iov.iov_len = check_size;
//     // printf("\t %d-th data incoming %d\n", id, check_size);
//     int read_size = rdmasr.recv(&recv_msg, check_size);
//     total_recv_sz += read_size;
//     // printf("\t %d-th recv %d bytes, total recv size = %lld\n", 
//     //       id, read_size, total_recv_sz);

//     if (id % 1000 == 0) {
//       printf("%d-th, total recv %lld bytes, total send (%d times, %lld bytes)\n", 
//               id, total_recv_sz, send_id, total_send_size);
//       check_point += check_point_inc;
//     }

//     if (total_recv_sz == total_send_size && send_id == BATCH_SZ) {
//       printf("recv complete, total recv %lld size, id = %d, total send %d times\n", 
//               total_recv_sz, id, send_id);
//       break;
//     }
//     id++;
//   }

//   recver_alive = false;
//   delete recv_buf;


//   send_thread.join();

//   return 0;
// }