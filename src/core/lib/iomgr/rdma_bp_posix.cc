#include <grpc/support/port_platform.h>

#include "src/core/lib/iomgr/port.h"

#ifdef GRPC_POSIX_SOCKET_TCP

#include <errno.h>
#include <grpcpp/get_clock.h>
#include <limits.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <algorithm>
#include <unordered_map>
#include "src/core/lib/iomgr/ev_epollex_rdma_bp_linux.h"
#include "src/core/lib/iomgr/rdma_bp_posix.h"

#include <grpc/slice.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/sync.h>
#include <grpc/support/time.h>
#include "include/grpcpp/stats_time.h"

#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/debug/stats.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/iomgr/buffer_list.h"
#include "src/core/lib/iomgr/ev_posix.h"
#include "src/core/lib/iomgr/executor.h"
#include "src/core/lib/iomgr/socket_utils_posix.h"
#include "src/core/lib/profiling/timers.h"
#include "src/core/lib/rdma/RDMASenderReceiver.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/slice/slice_string_helpers.h"

#define PREALLOCATE_SIZE (128ul * 1024 * 1024)
grpc_core::TraceFlag grpc_trace_transport_bp(false, "transport_event");

namespace {
struct grpc_rdma {
  grpc_rdma() {}
  grpc_endpoint base;
  grpc_fd* em_fd;
  int fd;
  /* Used by the endpoint read function to distinguish the very first read call
   * from the rest */
  bool is_first_read;
  // double target_length;
  // double bytes_read_this_round;
  grpc_core::RefCount refcount;
  gpr_atm shutdown_count;

  RDMASenderReceiverBP* rdmasr;

  // int min_read_chunk_size;
  // int max_read_chunk_size;

  /* garbage after the last read */
  grpc_slice_buffer last_read_buffer;

  grpc_slice_buffer* incoming_buffer;
  size_t total_recv_bytes = 0;
  // int inq;          /* bytes pending on the socket from the last read. */
  // bool inq_capable; /* cache whether kernel supports inq */

  grpc_slice_buffer* outgoing_buffer;
  size_t total_send_bytes = 0;
  /* byte within outgoing_buffer->slices[0] to write next */
  size_t outgoing_byte_idx;

  grpc_closure* read_cb;

  grpc_closure* write_cb;
  grpc_closure* release_fd_cb;
  int* release_fd;

  grpc_closure read_done_closure;
  grpc_closure write_done_closure;
  grpc_closure error_closure;

  std::string peer_string;
  std::string local_address;

  grpc_resource_user* resource_user;
  grpc_resource_user_slice_allocator slice_allocator;
  bool final_read;
  bool preallocate;

  cycles_t last_read_time;
  cycles_t last_write_time;
  // grpc_core::TracedBuffer* tb_head; /* List of traced buffers */
  // gpr_mu tb_mu; /* Lock for access to list of traced buffers */

  /* grpc_endpoint_write takes an argument which if non-null means that the
   * transport layer wants the TCP layer to collect timestamps for this write.
   * This arg is forwarded to the timestamps callback function when the ACK
   * timestamp is received from the kernel. This arg is a (void *) which allows
   * users of this API to pass in a pointer to any kind of structure. This
   * structure could actually be a tag or any book-keeping object that the user
   * can use to distinguish between different traced writes. The only
   * requirement from the TCP endpoint layer is that this arg should be non-null
   * if the user wants timestamps for the write. */
  // void* outgoing_buffer_arg;
  // /* A counter which starts at 0. It is initialized the first time the socket
  //  * options for collecting timestamps are set, and is incremented with each
  //  * byte sent. */
  // int bytes_counter;
  // bool socket_ts_enabled; /* True if timestamping options are set on the
  // socket
  //                          */
  // bool ts_capable;        /* Cache whether we can set timestamping options */
  // gpr_atm stop_error_notification; /* Set to 1 if we do not want to be
  // notified
  //                                     on errors anymore */
};

}  // namespace

static grpc_error_handle rdma_annotate_error(grpc_error_handle src_error,
                                             grpc_rdma* rdma) {
  return grpc_error_set_str(
      grpc_error_set_int(
          grpc_error_set_int(src_error, GRPC_ERROR_INT_FD, rdma->fd),
          /* All tcp errors are marked with UNAVAILABLE so that application may
           * choose to retry. */
          GRPC_ERROR_INT_GRPC_STATUS, GRPC_STATUS_UNAVAILABLE),
      GRPC_ERROR_STR_TARGET_ADDRESS,
      grpc_slice_from_copied_string(rdma->peer_string.c_str()));
}

static void notify_on_read(grpc_rdma* rdma) {
  grpc_fd_notify_on_read(rdma->em_fd, &rdma->read_done_closure);
}

static void notify_on_write(grpc_rdma* rdma) {
  grpc_fd_notify_on_write(rdma->em_fd, &rdma->write_done_closure);
}

static void rdma_shutdown(grpc_endpoint* ep, grpc_error_handle why) {
  grpc_rdma* rdma = reinterpret_cast<grpc_rdma*>(ep);
  // printf("rdma shutdown, shutdown fd %d\n", grpc_fd_wrapped_fd(rdma->em_fd));
  rdma->rdmasr->Shutdown();
  grpc_fd_shutdown(rdma->em_fd, why);
  grpc_resource_user_shutdown(rdma->resource_user);
}

static void rdma_free(grpc_rdma* rdma) {
  // printf("rdma free, orphan fd %d\n", grpc_fd_wrapped_fd(rdma->em_fd));
  grpc_fd_orphan(rdma->em_fd, rdma->release_fd_cb, rdma->release_fd,
                 "rdma_unref_orphan");
  grpc_slice_buffer_destroy_internal(&rdma->last_read_buffer);
  grpc_resource_user_unref(rdma->resource_user);
  delete rdma->rdmasr;
  delete rdma;
}

#ifndef NDEBUG
#define RDMA_UNREF(rdma, reason) rdma_unref((rdma), (reason), DEBUG_LOCATION)
#define RDMA_REF(rdma, reason) rdma_ref((rdma), (reason), DEBUG_LOCATION)
static void rdma_unref(grpc_rdma* rdma, const char* reason,
                       const grpc_core::DebugLocation& debug_location) {
  if (GPR_UNLIKELY(rdma->refcount.Unref(debug_location, reason))) {
    rdma_free(rdma);
  }
}

static void rdma_ref(grpc_rdma* rdma, const char* reason,
                     const grpc_core::DebugLocation& debug_location) {
  rdma->refcount.Ref(debug_location, reason);
}
#else
#define RDMA_UNREF(rdma, reason) rdma_unref((rdma))
#define RDMA_REF(rdma, reason) rdma_ref((rdma))
static void rdma_unref(grpc_rdma* rdma) {
  if (GPR_UNLIKELY(rdma->refcount.Unref())) {
    rdma_free(rdma);
  }
}

static void rdma_ref(grpc_rdma* rdma) { rdma->refcount.Ref(); }
#endif

static void rdma_destroy(grpc_endpoint* ep) {
  grpc_rdma* rdma = reinterpret_cast<grpc_rdma*>(ep);
  // printf("rdma destroy, fd = %d\n", grpc_fd_wrapped_fd(rdma->em_fd));
  grpc_slice_buffer_reset_and_unref_internal(&rdma->last_read_buffer);
  RDMA_UNREF(rdma, "destroy");
}

static void call_read_cb(grpc_rdma* rdma, grpc_error_handle error) {
  grpc_closure* cb = rdma->read_cb;

  rdma->read_cb = nullptr;
  rdma->incoming_buffer = nullptr;
  grpc_core::Closure::Run(DEBUG_LOCATION, cb, error);
}

// -----< rdma_read >-----

#define MAX_READ_IOVEC 4

static void rdma_do_read(grpc_rdma* rdma) {
  GRPCProfiler profiler(GRPC_STATS_TIME_TRANSPORT_DO_READ, 0);
  struct msghdr msg;
  struct iovec iov[MAX_READ_IOVEC];
  size_t iov_len = rdma->incoming_buffer->count;
  GPR_ASSERT(iov_len > 0 && iov_len <= MAX_READ_IOVEC);

  for (size_t i = 0; i < iov_len; i++) {
    iov[i].iov_base = GRPC_SLICE_START_PTR(rdma->incoming_buffer->slices[i]);
    iov[i].iov_len = GRPC_SLICE_LENGTH(rdma->incoming_buffer->slices[i]);
  }
  msg.msg_iov = iov;
  msg.msg_iovlen = iov_len;

  size_t read_bytes = rdma->rdmasr->recv(&msg);
  GPR_ASSERT(read_bytes > 0);
  rdma->total_recv_bytes += read_bytes;
  if (GRPC_TRACE_FLAG_ENABLED(grpc_trace_transport_bp)) {
    gpr_log(GPR_INFO, "rdma_do_read recv %zu bytes", read_bytes);
  }
  if (read_bytes < rdma->incoming_buffer->length) {
    grpc_slice_buffer_trim_end(rdma->incoming_buffer,
                               rdma->incoming_buffer->length - read_bytes,
                               &rdma->last_read_buffer);
  }
  if (!rdma->final_read) {
    call_read_cb(rdma, GRPC_ERROR_NONE);
  }
  RDMA_UNREF(rdma, "read");
}

static void rdma_continue_read(grpc_rdma* rdma) {
  GRPCProfiler profiler(GRPC_STATS_TIME_TRANSPORT_CONTINUE_READ);
  size_t mlen = rdma->rdmasr->get_unread_data_size();
  GPR_ASSERT(mlen > 0);
  if (rdma->incoming_buffer->length < mlen) {
    size_t target_size = mlen - rdma->incoming_buffer->length;
    if (GRPC_TRACE_FLAG_ENABLED(grpc_trace_transport_bp)) {
      gpr_log(
          GPR_INFO,
          "rdma_continue_read, data size = %zu, incoming buffer size = %zu, "
          "allocate %zu bytes",
          mlen, rdma->incoming_buffer->length, target_size);
    }
    if (GPR_UNLIKELY(!grpc_resource_user_alloc_slices(
            &rdma->slice_allocator, target_size, 1, rdma->incoming_buffer))) {
      return;
    }
  }
  if (GRPC_TRACE_FLAG_ENABLED(grpc_trace_transport_bp)) {
    gpr_log(GPR_INFO,
            "rdma_continue_read, data size = %zu, incoming buffer size = %zu, "
            "call rdma_do_read",
            mlen, rdma->incoming_buffer->length);
  }
  rdma_do_read(rdma);
}

static void rdma_read_allocation_done(void* rdmap, grpc_error_handle error) {
  grpc_rdma* rdma = static_cast<grpc_rdma*>(rdmap);
  if (GPR_UNLIKELY(error != GRPC_ERROR_NONE)) {
    grpc_slice_buffer_reset_and_unref_internal(rdma->incoming_buffer);
    grpc_slice_buffer_reset_and_unref_internal(&rdma->last_read_buffer);
    call_read_cb(rdma, GRPC_ERROR_REF(error));
    RDMA_UNREF(rdma, "read");
  } else {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_trace_transport_bp)) {
      gpr_log(
          GPR_INFO,
          "rdma_read_allocation_done, data size = %zu, incoming buffer size "
          "= %zu, call rdma_do_read",
          rdma->rdmasr->get_unread_data_size(), rdma->incoming_buffer->length);
    }
    //    printf("rdma_read_allocation_done, msg %zu, incoming_buffer %zu, fd:
    //    %d\n",
    //           rdma->rdmasr->get_unread_data_size(),
    //           rdma->incoming_buffer->length, rdma->fd);
    rdma_do_read(rdma);
  }
}

// when there is no data in rdma, call it, test if remote socket closed
thread_local absl::Time last_bp_tcp_read_;

// when there is no data in rdma, call it, test if remote socket closed
int tcp_do_read(grpc_rdma* rdma) {
  uint8_t buf[16];
  int ret = 1;

  if ((absl::Now() - last_bp_tcp_read_) > absl::Milliseconds(10)) {
    do {
      ret = recv(rdma->fd, buf, 16, 0);
    } while (ret < 0 && errno == EINTR);
    last_bp_tcp_read_ = absl::Now();
  }

  return ret;
}


static void rdma_handle_read(void* arg /* grpc_rdma */,
                             grpc_error_handle error) {
  GRPCProfiler profiler(GRPC_STATS_TIME_TRANSPORT_HANDLE_READ);
  grpc_rdma* rdma = static_cast<grpc_rdma*>(arg);

  if (GPR_UNLIKELY(error != GRPC_ERROR_NONE)) {
    grpc_slice_buffer_reset_and_unref_internal(rdma->incoming_buffer);
    grpc_slice_buffer_reset_and_unref_internal(&rdma->last_read_buffer);
    call_read_cb(rdma, GRPC_ERROR_REF(error));
    RDMA_UNREF(rdma, "read");
  } else {
    auto msg_len = rdma->rdmasr->check_and_ack_incomings_locked();
    if (msg_len == 0) {
      if (tcp_do_read(rdma) == 0) {
        //        printf("case A, close rdma, fd = %d\n", rdma->fd);
        grpc_slice_buffer_reset_and_unref_internal(rdma->incoming_buffer);
        call_read_cb(
            rdma,
            rdma_annotate_error(
                GRPC_ERROR_CREATE_FROM_STATIC_STRING("Socket closed"), rdma));
        RDMA_UNREF(rdma, "read");
      } else {
        notify_on_read(rdma);
      }
    } else {
      if (GRPC_TRACE_FLAG_ENABLED(grpc_trace_transport_bp)) {
        gpr_log(
            GPR_INFO,
            "rdma_handle_read, found %zu bytes data, call rdma_continue_read",
            rdma->rdmasr->get_unread_data_size());
      }
      if (tcp_do_read(rdma) == 0) {
        rdma->final_read = true;
        rdma_continue_read(rdma);
        RDMA_UNREF(rdma, "read");
      } else {
        rdma_continue_read(rdma);
      }
    }
  }
}

static void rdma_read(grpc_endpoint* ep, grpc_slice_buffer* incoming_buffer,
                      grpc_closure* cb, bool urgent) {
  GRPCProfiler profiler(GRPC_STATS_TIME_TRANSPORT_READ);
  grpc_rdma* rdma = reinterpret_cast<grpc_rdma*>(ep);
  GPR_ASSERT(rdma->read_cb == nullptr);
  rdma->read_cb = cb;
  rdma->incoming_buffer = incoming_buffer;
  grpc_slice_buffer_reset_and_unref_internal(incoming_buffer);
  grpc_slice_buffer_swap(incoming_buffer, &rdma->last_read_buffer);
  RDMA_REF(rdma, "read");

  if (rdma->is_first_read) {
    rdma->is_first_read = false;
    if (GRPC_TRACE_FLAG_ENABLED(grpc_trace_transport_bp)) {
      gpr_log(GPR_INFO, "rdma_read, is_first_read, call notify_on_read");
    }
    notify_on_read(rdma);
  } else if (!urgent && rdma->rdmasr->get_unread_data_size() == 0) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_trace_transport_bp)) {
      gpr_log(GPR_INFO, "rdma_read, urgent, call notify_on_read");
    }
    notify_on_read(rdma);
  } else {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_trace_transport_bp)) {
      gpr_log(GPR_INFO, "rdma_read, call rdma_handle_read");
    }
    grpc_core::Closure::Run(DEBUG_LOCATION, &rdma->read_done_closure,
                            GRPC_ERROR_NONE);
  }
}

// -----< rdma_write >-----
#define MAX_WRITE_IOVEC 1000

static bool rdma_flush(grpc_rdma* rdma, grpc_error_handle* error) {
  GRPCProfiler profiler(GRPC_STATS_TIME_TRANSPORT_FLUSH, 0);
  struct msghdr msg;
  struct iovec iov[MAX_WRITE_IOVEC];
  size_t iov_size;
  size_t sending_length;
  size_t unwind_slice_idx;
  size_t unwind_byte_idx;

  size_t outgoing_slice_idx = 0;
  size_t total_sent_length = 0;
  size_t max_send_size = rdma->rdmasr->get_max_send_size();

  while (true) {
    sending_length = 0;
    unwind_slice_idx = outgoing_slice_idx;
    unwind_byte_idx = rdma->outgoing_byte_idx;
    for (iov_size = 0;
         outgoing_slice_idx != rdma->outgoing_buffer->count &&
         iov_size != MAX_WRITE_IOVEC && sending_length < max_send_size;
         iov_size++) {
      iov[iov_size].iov_base =
          GRPC_SLICE_START_PTR(
              rdma->outgoing_buffer->slices[outgoing_slice_idx]) +
          rdma->outgoing_byte_idx;
      size_t iov_len =
          GRPC_SLICE_LENGTH(rdma->outgoing_buffer->slices[outgoing_slice_idx]) -
          rdma->outgoing_byte_idx;
      if (sending_length + iov_len > max_send_size) {
        iov[iov_size].iov_len = max_send_size - sending_length;
        rdma->outgoing_byte_idx += iov[iov_size].iov_len;
      } else {
        iov[iov_size].iov_len = iov_len;
        outgoing_slice_idx++;
        rdma->outgoing_byte_idx = 0;
      }
      sending_length += iov[iov_size].iov_len;
    }
    GPR_ASSERT(iov_size > 0);

    msg.msg_iov = iov;
    msg.msg_iovlen = iov_size;

    if (GRPC_TRACE_FLAG_ENABLED(grpc_trace_transport_bp)) {
      gpr_log(GPR_INFO, "rdma_flush try to send %zu bytes, %zu, %zu, %zu",
              sending_length, iov_size, total_sent_length,
              rdma->outgoing_buffer->length);
    }
    bool send_ok = rdma->rdmasr->send(&msg, sending_length);
    if (send_ok) {
      grpc_stats_time_add_custom(GRPC_STATS_TIME_SEND_SIZE, sending_length);
    }
    if (!send_ok) {
      if (rdma->rdmasr->IfRemoteExit()) {
        grpc_slice_buffer_reset_and_unref_internal(rdma->outgoing_buffer);
        return true;
      }
      // not enough space in remote
      rdma->outgoing_byte_idx = unwind_byte_idx;
      for (size_t idx = 0; idx < unwind_slice_idx; idx++) {
        grpc_slice_buffer_remove_first(rdma->outgoing_buffer);
      }
      rdma->rdmasr->write_again();
      return false;
    }
    rdma->rdmasr->write_again_done();

    if (outgoing_slice_idx == rdma->outgoing_buffer->count) {
      *error = GRPC_ERROR_NONE;
      grpc_slice_buffer_reset_and_unref_internal(rdma->outgoing_buffer);
      return true;
    }
  }
}

static void rdma_handle_write(void* arg /* grpc_tcp */,
                              grpc_error_handle error) {
  GRPCProfiler profiler(GRPC_STATS_TIME_TRANSPORT_HANDLE_WRITE);
  grpc_rdma* rdma = static_cast<grpc_rdma*>(arg);
  grpc_closure* cb;

  if (error != GRPC_ERROR_NONE) {
    cb = rdma->write_cb;
    rdma->write_cb = nullptr;
    grpc_core::Closure::Run(DEBUG_LOCATION, cb, GRPC_ERROR_REF(error));
    RDMA_UNREF(rdma, "write");
    return;
  }

  bool flush_result = rdma_flush(rdma, &error);
  if (!flush_result) {
    notify_on_write(rdma);
    GPR_DEBUG_ASSERT(error == GRPC_ERROR_NONE);
  } else {
    cb = rdma->write_cb;
    rdma->write_cb = nullptr;
    grpc_core::Closure::Run(DEBUG_LOCATION, cb, error);
    RDMA_UNREF(rdma, "write");
  }
}

static void rdma_write(grpc_endpoint* ep, grpc_slice_buffer* buf,
                       grpc_closure* cb, void* arg) {
  GRPCProfiler profiler(GRPC_STATS_TIME_TRANSPORT_WRITE);
  grpc_rdma* rdma = reinterpret_cast<grpc_rdma*>(ep);
  grpc_error_handle error = GRPC_ERROR_NONE;
  GPR_ASSERT(rdma->write_cb == nullptr);

  if (buf->length == 0) {
    grpc_core::Closure::Run(
        DEBUG_LOCATION, cb,
        grpc_fd_is_shutdown(rdma->em_fd)
            ? rdma_annotate_error(GRPC_ERROR_CREATE_FROM_STATIC_STRING("EOF"),
                                  rdma)
            : GRPC_ERROR_NONE);
    return;
  }
  rdma->outgoing_buffer = buf;
  rdma->outgoing_byte_idx = 0;

  bool flush_result = rdma_flush(rdma, &error);
  if (!flush_result) {
    RDMA_REF(rdma, "write");
    rdma->write_cb = cb;
    notify_on_write(rdma);
  } else {
    grpc_core::Closure::Run(DEBUG_LOCATION, cb, error);
  }
}

static void rdma_add_to_pollset(grpc_endpoint* ep, grpc_pollset* pollset) {
  grpc_rdma* rdma = reinterpret_cast<grpc_rdma*>(ep);
  grpc_pollset_add_fd(pollset, rdma->em_fd);
}

static void rdma_add_to_pollset_set(grpc_endpoint* ep,
                                    grpc_pollset_set* pollset_set) {
  grpc_rdma* rdma = reinterpret_cast<grpc_rdma*>(ep);
  grpc_pollset_set_add_fd(pollset_set, rdma->em_fd);
}

static void rdma_delete_from_pollset_set(grpc_endpoint* ep,
                                         grpc_pollset_set* pollset_set) {
  grpc_rdma* rdma = reinterpret_cast<grpc_rdma*>(ep);
  grpc_pollset_set_del_fd(pollset_set, rdma->em_fd);
}

static absl::string_view rdma_get_peer(grpc_endpoint* ep) {
  grpc_rdma* rdma = reinterpret_cast<grpc_rdma*>(ep);
  return rdma->peer_string;
}

static absl::string_view rdma_get_local_address(grpc_endpoint* ep) {
  grpc_rdma* rdma = reinterpret_cast<grpc_rdma*>(ep);
  return rdma->local_address;
}

static int rdma_get_fd(grpc_endpoint* ep) {
  grpc_rdma* rdma = reinterpret_cast<grpc_rdma*>(ep);
  return rdma->fd;
}

static grpc_resource_user* rdma_get_resource_user(grpc_endpoint* ep) {
  grpc_rdma* rdma = reinterpret_cast<grpc_rdma*>(ep);
  return rdma->resource_user;
}

static bool rdma_can_track_err(grpc_endpoint* ep) {
  grpc_rdma* rdma = reinterpret_cast<grpc_rdma*>(ep);
  if (!grpc_event_engine_can_track_errors()) {
    return false;
  }
  struct sockaddr addr;
  socklen_t len = sizeof(addr);
  if (getsockname(rdma->fd, &addr, &len) < 0) {
    return false;
  }
  return addr.sa_family == AF_INET || addr.sa_family == AF_INET6;
}

static const grpc_endpoint_vtable vtable = {rdma_read,
                                            rdma_write,
                                            rdma_add_to_pollset,
                                            rdma_add_to_pollset_set,
                                            rdma_delete_from_pollset_set,
                                            rdma_shutdown,
                                            rdma_destroy,
                                            rdma_get_resource_user,
                                            rdma_get_peer,
                                            rdma_get_local_address,
                                            rdma_get_fd,
                                            rdma_can_track_err};
grpc_endpoint* grpc_rdma_bp_create(grpc_fd* em_fd,
                                   const grpc_channel_args* channel_args,
                                   const char* peer_string) {
  grpc_resource_quota* resource_quota = grpc_resource_quota_create(nullptr);
  if (channel_args != nullptr) {
    for (size_t i = 0; i < channel_args->num_args; i++) {
      if (0 == strcmp(channel_args->args[i].key, GRPC_ARG_RESOURCE_QUOTA)) {
        grpc_resource_quota_unref_internal(resource_quota);
        resource_quota =
            grpc_resource_quota_ref_internal(static_cast<grpc_resource_quota*>(
                channel_args->args[i].value.pointer.p));
      }
    }
  }
  grpc_rdma* rdma = new grpc_rdma();
  rdma->base.vtable = &vtable;
  rdma->peer_string = peer_string;
  rdma->fd = grpc_fd_wrapped_fd(em_fd);
  grpc_resolved_address resolved_local_addr;
  memset(&resolved_local_addr, 0, sizeof(resolved_local_addr));
  resolved_local_addr.len = sizeof(resolved_local_addr.addr);
  if (getsockname(rdma->fd,
                  reinterpret_cast<sockaddr*>(resolved_local_addr.addr),
                  &resolved_local_addr.len) < 0) {
    rdma->local_address = "";
  } else {
    rdma->local_address = grpc_sockaddr_to_uri(&resolved_local_addr);
  }
  rdma->read_cb = nullptr;
  rdma->write_cb = nullptr;
  rdma->release_fd_cb = nullptr;
  rdma->release_fd = nullptr;
  rdma->incoming_buffer = nullptr;
  rdma->is_first_read = true;
  new (&rdma->refcount) grpc_core::RefCount(1, nullptr);
  gpr_atm_no_barrier_store(&rdma->shutdown_count, 0);
  rdma->em_fd = em_fd;
  grpc_slice_buffer_init(&rdma->last_read_buffer);
  rdma->resource_user = grpc_resource_user_create(resource_quota, peer_string);
  grpc_resource_user_slice_allocator_init(&rdma->slice_allocator,
                                          rdma->resource_user,
                                          rdma_read_allocation_done, rdma);
  grpc_resource_quota_unref_internal(resource_quota);
  GRPC_CLOSURE_INIT(&rdma->read_done_closure, rdma_handle_read, rdma,
                    grpc_schedule_on_exec_ctx);
  GRPC_CLOSURE_INIT(&rdma->write_done_closure, rdma_handle_write, rdma,
                    grpc_schedule_on_exec_ctx);
  rdma->rdmasr = new RDMASenderReceiverBP();
  rdma->rdmasr->connect(rdma->fd);
  rdma->final_read = false;
  rdma->preallocate = true;
  rdma->last_read_time = 0;
  rdma->last_write_time = 0;
  grpc_fd_set_rdmasr_bp(em_fd, rdma->rdmasr);
  return &rdma->base;
}

int grpc_rdma_bp_fd(grpc_endpoint* ep) {
  grpc_rdma* rdma = reinterpret_cast<grpc_rdma*>(ep);
  GPR_ASSERT(ep->vtable == &vtable);
  return grpc_fd_wrapped_fd(rdma->em_fd);
}

void grpc_rdma_bp_destroy_and_release_fd(grpc_endpoint* ep, int* fd,
                                         grpc_closure* done) {
  grpc_rdma* rdma = reinterpret_cast<grpc_rdma*>(ep);
  GPR_ASSERT(ep->vtable == &vtable);
  rdma->release_fd = fd;
  rdma->release_fd_cb = done;
  grpc_slice_buffer_reset_and_unref_internal(&rdma->last_read_buffer);
  RDMA_UNREF(rdma, "destroy");
}

RDMASenderReceiver* grpc_rdma_bp_get_rdmasr(grpc_endpoint* ep) {
  grpc_rdma* rdma = reinterpret_cast<grpc_rdma*>(ep);
  GPR_ASSERT(ep->vtable == &vtable);
  return rdma->rdmasr;
}

void* grpc_rdma_bp_require_zerocopy_sendspace(grpc_endpoint* ep, size_t size) {
  grpc_rdma* rdma = reinterpret_cast<grpc_rdma*>(ep);
  GPR_ASSERT(ep->vtable == &vtable);
  return rdma->rdmasr->require_zerocopy_sendspace(size);
}

#endif /* GRPC_POSIX_SOCKET_TCP */
