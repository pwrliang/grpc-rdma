#include "src/core/lib/rdma/ringbuffer.h"
#include <stdio.h>
#include <vector>
#include "grpc/impl/codegen/log.h"
#include "src/core/lib/debug/trace.h"
size_t random_range(size_t min, size_t max) {  // range : [min, max]
  static bool first = true;
  if (first) {
    srand(time(NULL));  // seeding for the first time only!
    first = false;
  }
  return min + rand() % ((max + 1) - min);
}

void WriteRB(RingBufferBP& rb, size_t& remote_tail, void* src, size_t len) {
  size_t right_len =
      std::max(remote_tail + len, rb.get_capacity()) - rb.get_capacity();
  size_t left_len = len - right_len;

  auto before = rb.CheckMessageLength();
  memcpy(rb.get_buf() + remote_tail, src, left_len);
  remote_tail = (remote_tail + left_len) % rb.get_capacity();

  if (right_len > 0) {
    GPR_ASSERT(remote_tail == 0);
    memcpy(rb.get_buf(), reinterpret_cast<char*>(src) + left_len, right_len);
    remote_tail = (remote_tail + right_len) % rb.get_capacity();
  }
  auto after = rb.CheckMessageLength();
  GPR_ASSERT(after - before + 9 == len);
}

void Test() {
  size_t capacity = random_range(64, 1024ul * 1024);
  size_t data_len = random_range(1, 4ul * 1024 * 1024);

  RingBufferBP rb(capacity);
  std::vector<uint8_t> send_data(data_len);
  std::vector<uint8_t> recv_data(data_len);
  std::vector<uint8_t> send_buf(rb.get_sendbuf_size());
  size_t remote_tail = 0;
  auto send_it = send_data.begin();
  auto recv_it = recv_data.begin();

  for (auto& c : send_data) {
    c = rand() % 255;
  }

  while (send_it != send_data.end() || recv_it != recv_data.end()) {
    size_t used_rb =
        (rb.get_capacity() + remote_tail - rb.get_head()) % rb.get_capacity();
    size_t free_rb =
        rb.get_capacity() - used_rb > 8 ? (rb.get_capacity() - used_rb - 8) : 0;
    size_t curr_mlen = 0;
    if (send_it != send_data.end() && free_rb > 9) {
      curr_mlen = random_range(
          1, std::min(static_cast<size_t>(send_data.end() - send_it),
                      std::min(rb.get_max_send_size(),
                               free_rb > 9 ? (free_rb - 9) : 0)));
      // write to send buf
      *reinterpret_cast<size_t*>(send_buf.data()) = curr_mlen;
      memcpy(send_buf.data() + sizeof(size_t), &*send_it, curr_mlen);
      *reinterpret_cast<char*>(send_buf.data() + sizeof(size_t) + curr_mlen) =
          1;
      send_it += curr_mlen;

      GPR_ASSERT(free_rb >= sizeof(size_t) + curr_mlen + 1);
      auto before_mlen = rb.CheckFirstMessageLength();
      WriteRB(rb, remote_tail, send_buf.data(), sizeof(size_t) + curr_mlen + 1);
      auto after_mlen = rb.CheckFirstMessageLength();
      GPR_ASSERT(before_mlen == 0 || before_mlen == after_mlen);
    }

    /////////// read
    msghdr msghdr;
    size_t msg_iov_len = random_range(0, 10);
    size_t iov_len = random_range(1, 1024ul * 1024);
    size_t total_iov_len = 0;

    msghdr.msg_iov = static_cast<iovec*>(malloc(sizeof(iovec) * msg_iov_len));
    msghdr.msg_iovlen = msg_iov_len;

    for (int i = 0; i < msg_iov_len; i++) {
      msghdr.msg_iov[i].iov_len = (iov_len + msg_iov_len - 1) / msg_iov_len;
      msghdr.msg_iov[i].iov_base = malloc(msghdr.msg_iov[i].iov_len);
      total_iov_len += msghdr.msg_iov[i].iov_len;
    }
    bool recycle;
    size_t actual_read = rb.Read(&msghdr, recycle);

    size_t total_sent = send_it - send_data.begin();
    size_t total_recv = recv_it - recv_data.begin();

    GPR_ASSERT(total_sent >= total_recv);

    size_t remaining = actual_read;

    for (int i = 0; i < msg_iov_len && remaining > 0; i++) {
      if (remaining >= msghdr.msg_iov[i].iov_len) {
        remaining -= msghdr.msg_iov[i].iov_len;
      } else {
        msghdr.msg_iov[i].iov_len = remaining;
        remaining = 0;
      }

      memcpy(&*recv_it, msghdr.msg_iov[i].iov_base, msghdr.msg_iov[i].iov_len);
      recv_it += msghdr.msg_iov[i].iov_len;
    }

    for (int i = 0; i < msg_iov_len; i++) {
      free(msghdr.msg_iov[i].iov_base);
    }
    free(msghdr.msg_iov);
  }
  GPR_ASSERT(send_data == recv_data);
}

int main(int argc, char* argv[]) {
  gpr_log_verbosity_init();
  grpc_tracer_init();
  for (int i = 0; i < 100; i++) {
    Test();
  }
  printf("Tests pass\n");
  grpc_tracer_shutdown();
}
