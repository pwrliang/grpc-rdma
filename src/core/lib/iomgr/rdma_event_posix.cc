#include <grpc/support/port_platform.h>

#include "src/core/lib/iomgr/port.h"

#ifdef GRPC_POSIX_SOCKET_TCP

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <algorithm>

#include <grpc/slice.h>
#include <grpc/support/log.h>
#include <grpc/support/sync.h>
#include "include/grpcpp/stats_time.h"

#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/iomgr/buffer_list.h"
#include "src/core/lib/iomgr/ev_posix.h"
#include "src/core/lib/iomgr/executor.h"
#include "src/core/lib/iomgr/rdma_event_posix.h"
#include "src/core/lib/iomgr/timer.h"
#include "src/core/lib/rdma/rdma_sender_receiver.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/slice/slice_string_helpers.h"

#ifdef GRPC_MSG_IOVLEN_TYPE
typedef GRPC_MSG_IOVLEN_TYPE msg_iovlen_type;
#else
typedef size_t msg_iovlen_type;
#endif

extern grpc_core::TraceFlag grpc_rdma_trace;

namespace {
struct grpc_rdma {
  grpc_rdma() {}
  grpc_endpoint base;
  grpc_fd* em_fd;
  int fd;
  /* Used by the endpoint read function to distinguish the very first read call
   * from the rest */
  bool is_first_read;
  grpc_core::RefCount refcount;
  gpr_atm shutdown_count;

  RDMASenderReceiverEvent* rdmasr;

  /* garbage after the last read */
  grpc_slice_buffer last_read_buffer;
  grpc_slice_buffer* incoming_buffer;
  grpc_slice_buffer* outgoing_buffer;
  /* byte within outgoing_buffer->slices[0] to write next */
  size_t outgoing_byte_idx;

  grpc_closure* read_cb;
  grpc_closure* write_cb;
  grpc_closure* release_fd_cb;
  int* release_fd;

  grpc_closure read_done_closure;
  grpc_closure write_done_closure;
  grpc_closure error_closure;
  grpc_closure check_conn_closure;

  std::string peer_string;
  std::string local_address;

  grpc_resource_user* resource_user;
  grpc_resource_user_slice_allocator slice_allocator;
  grpc_timer check_conn_timer;
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
  rdma->rdmasr->Shutdown();
  grpc_fd_shutdown(rdma->em_fd, why);
  grpc_resource_user_shutdown(rdma->resource_user);
}

static void rdma_free(grpc_rdma* rdma) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_rdma_trace)) {
    gpr_log(GPR_INFO, "rdma %p free, orphan fd %d", rdma,
            grpc_fd_wrapped_fd(rdma->em_fd));
  }
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
  if (GRPC_TRACE_FLAG_ENABLED(grpc_rdma_trace)) {
    gpr_log(GPR_INFO, "rdma %p destroy, fd = %d", rdma,
            grpc_fd_wrapped_fd(rdma->em_fd));
  }
  grpc_timer_cancel(&rdma->check_conn_timer);
  grpc_slice_buffer_reset_and_unref_internal(&rdma->last_read_buffer);
  RDMA_UNREF(rdma, "destroy");
}

static void call_read_cb(grpc_rdma* rdma, grpc_error_handle error) {
  grpc_closure* cb = rdma->read_cb;

  rdma->read_cb = nullptr;
  rdma->incoming_buffer = nullptr;
  grpc_core::Closure::Run(DEBUG_LOCATION, cb, error);
}

#define MAX_READ_IOVEC 1024

static void rdma_do_read(grpc_rdma* rdma) {
  GRPCProfiler profiler(GRPC_STATS_TIME_TRANSPORT_DO_READ);
  struct msghdr msg;
  struct iovec iov[MAX_READ_IOVEC];
  size_t iov_len = rdma->incoming_buffer->count;
  GPR_ASSERT(iov_len <= MAX_READ_IOVEC);

  // We don't have space to read, this maybe caused by urgent flag
  if (iov_len == 0) {
    notify_on_read(rdma);
    return;
  }

  for (size_t i = 0; i < iov_len; i++) {
    iov[i].iov_base = GRPC_SLICE_START_PTR(rdma->incoming_buffer->slices[i]);
    iov[i].iov_len = GRPC_SLICE_LENGTH(rdma->incoming_buffer->slices[i]);
  }
  msg.msg_iov = iov;
  msg.msg_iovlen = iov_len;

  ssize_t read_bytes;
  int err = rdma->rdmasr->Recv(&msg, &read_bytes);

  if (read_bytes < 0) {
    if (err == EAGAIN) {
      notify_on_read(rdma);
    } else {
      grpc_slice_buffer_reset_and_unref_internal(rdma->incoming_buffer);
      call_read_cb(rdma,
                   rdma_annotate_error(GRPC_OS_ERROR(err, "recvmsg"), rdma));
      RDMA_UNREF(rdma, "read");
    }
    return;
  } else if (read_bytes == 0) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_rdma_trace)) {
      gpr_log(GPR_INFO, "close rdma %p, err: %s", rdma, strerror(err));
    }
    grpc_slice_buffer_reset_and_unref_internal(rdma->incoming_buffer);
    call_read_cb(
        rdma, rdma_annotate_error(
                  GRPC_ERROR_CREATE_FROM_STATIC_STRING("Socket closed"), rdma));
    RDMA_UNREF(rdma, "read");
    return;
  }

  if (GRPC_TRACE_FLAG_ENABLED(grpc_rdma_trace)) {
    gpr_log(GPR_INFO, "rdma_do_read recv %zu bytes", read_bytes);
  }
  if (read_bytes < rdma->incoming_buffer->length) {
    grpc_slice_buffer_trim_end(rdma->incoming_buffer,
                               rdma->incoming_buffer->length - read_bytes,
                               &rdma->last_read_buffer);
  }
  call_read_cb(rdma, GRPC_ERROR_NONE);
  RDMA_UNREF(rdma, "read");
}

static void rdma_read_allocation_done(void* rdmap, grpc_error_handle error) {
  grpc_rdma* rdma = static_cast<grpc_rdma*>(rdmap);

  if (GPR_UNLIKELY(error != GRPC_ERROR_NONE)) {
    grpc_slice_buffer_reset_and_unref_internal(rdma->incoming_buffer);
    grpc_slice_buffer_reset_and_unref_internal(&rdma->last_read_buffer);
    call_read_cb(rdma, GRPC_ERROR_REF(error));
    RDMA_UNREF(rdma, "read");
  } else {
    rdma_do_read(rdma);
  }
}

static void rdma_continue_read(grpc_rdma* rdma) {
  GRPCProfiler profiler(GRPC_STATS_TIME_TRANSPORT_CONTINUE_READ);
  size_t target_read_size = rdma->rdmasr->MarkMessageLength();

  target_read_size = MAX(5 * target_read_size, 1024 * 1024 * 1024);

  /* Wait for allocation only when there is no buffer left. */
  if (rdma->incoming_buffer->length < target_read_size) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_rdma_trace)) {
      gpr_log(GPR_INFO, "rdma allocate slice: %zu", target_read_size);
    }
    if (GPR_UNLIKELY(!grpc_resource_user_alloc_slices(
            &rdma->slice_allocator,
            target_read_size - rdma->incoming_buffer->length, 1,
            rdma->incoming_buffer))) {
      return;
    }
  }
  if (GRPC_TRACE_FLAG_ENABLED(grpc_rdma_trace)) {
    gpr_log(GPR_INFO, "RDMA:%p do_read, len: %zu", rdma, target_read_size);
  }
  rdma_do_read(rdma);
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
    rdma_continue_read(rdma);
  }
  if (rdma->rdmasr->HasPendingWrite()) {
    grpc_fd_set_writable(rdma->em_fd);
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
    if (GRPC_TRACE_FLAG_ENABLED(grpc_rdma_trace)) {
      gpr_log(GPR_INFO, "rdma_read, is_first_read, call notify_on_read");
    }
    notify_on_read(rdma);
  } else if (!urgent && rdma->rdmasr->MarkMessageLength() == 0) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_rdma_trace)) {
      gpr_log(GPR_INFO, "rdma_read, urgent, call notify_on_read");
    }
    notify_on_read(rdma);
  } else {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_rdma_trace)) {
      gpr_log(GPR_INFO, "rdma_read, call rdma_handle_read");
    }
    grpc_core::Closure::Run(DEBUG_LOCATION, &rdma->read_done_closure,
                            GRPC_ERROR_NONE);
  }
}

#define MAX_WRITE_IOVEC 1000

static bool rdma_flush(grpc_rdma* rdma, grpc_error_handle* error) {
  GRPCProfiler profiler(GRPC_STATS_TIME_TRANSPORT_FLUSH);
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
    if (GRPC_TRACE_FLAG_ENABLED(grpc_rdma_trace)) {
      gpr_log(GPR_INFO, "rdma_flush try to send %zu bytes, %zu, %zu, %zu",
              sending_length, iov_size, total_sent_length,
              rdma->outgoing_buffer->length);
    }
    ssize_t sent_length;

    int err = rdma->rdmasr->Send(&msg, &sent_length);

    if (sent_length < 0) {
      if (err == EAGAIN) {
        rdma->outgoing_byte_idx = unwind_byte_idx;
        for (size_t idx = 0; idx < unwind_slice_idx; ++idx) {
          grpc_slice_buffer_remove_first(rdma->outgoing_buffer);
        }
        return false;
      } else if (err == EPIPE) {
        *error = rdma_annotate_error(GRPC_OS_ERROR(err, "sendmsg"), rdma);
        grpc_slice_buffer_reset_and_unref_internal(rdma->outgoing_buffer);
        return true;
      } else {
        *error = rdma_annotate_error(GRPC_OS_ERROR(err, "sendmsg"), rdma);
        grpc_slice_buffer_reset_and_unref_internal(rdma->outgoing_buffer);
        return true;
      }
    }

//    grpc_stats_time_add_custom(GRPC_STATS_TIME_SEND_SIZE, sent_length);

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
    if (GRPC_TRACE_FLAG_ENABLED(grpc_rdma_trace)) {
      gpr_log(GPR_INFO, "write: delayed");
    }
    notify_on_write(rdma);
    GPR_DEBUG_ASSERT(error == GRPC_ERROR_NONE);
  } else {
    cb = rdma->write_cb;
    rdma->write_cb = nullptr;
    if (GRPC_TRACE_FLAG_ENABLED(grpc_rdma_trace)) {
      gpr_log(GPR_INFO, "write: %s", grpc_error_std_string(error).c_str());
    }
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

#define RDMA_CHECK_CONN_INTERVAL_MS (100)

static void rdma_check_conn(void* arg /* grpc_rdma */,
                            grpc_error_handle error) {
  if (error == GRPC_ERROR_NONE) {
    grpc_rdma* rdma = static_cast<grpc_rdma*>(arg);
    uint8_t buf;
    int ret;
    return;  // fixme: check conn
    do {
      ret = recv(rdma->fd, &buf, 0, 0);
    } while (ret < 0 && errno == EINTR);

    if (ret == 0) {
      if (GRPC_TRACE_FLAG_ENABLED(grpc_rdma_trace)) {
        gpr_log(GPR_INFO, "rdmasr %p shutdown", rdma);
      }
      rdma->rdmasr->Shutdown();
    } else {
      grpc_timer_init(
          &rdma->check_conn_timer,
          grpc_core::ExecCtx::Get()->Now() + RDMA_CHECK_CONN_INTERVAL_MS,
          &rdma->check_conn_closure);
    }
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

grpc_endpoint* grpc_rdma_event_create(grpc_fd* em_fd,
                                      const grpc_channel_args* channel_args,
                                      const char* peer_string, bool server) {
  grpc_resource_quota* resource_quota = grpc_resource_quota_create(nullptr);
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
  GRPC_CLOSURE_INIT(&rdma->check_conn_closure, rdma_check_conn, rdma,
                    grpc_schedule_on_exec_ctx);

  grpc_timer_init(
      &rdma->check_conn_timer,
      grpc_core::ExecCtx::Get()->Now() + RDMA_CHECK_CONN_INTERVAL_MS,
      &rdma->check_conn_closure);

  rdma->rdmasr = new RDMASenderReceiverEvent(rdma->fd, server);
  rdma->rdmasr->Init();
  grpc_fd_set_rdmasr(em_fd, rdma->rdmasr);

  return &rdma->base;
}

int grpc_rdma_event_fd(grpc_endpoint* ep) {
  grpc_rdma* rdma = reinterpret_cast<grpc_rdma*>(ep);
  GPR_ASSERT(ep->vtable == &vtable);
  return grpc_fd_wrapped_fd(rdma->em_fd);
}

void* grpc_rdma_event_require_zerocopy_sendspace(grpc_endpoint* ep,
                                                 size_t size) {
  grpc_rdma* rdma = reinterpret_cast<grpc_rdma*>(ep);
  GPR_ASSERT(ep->vtable == &vtable);
  return rdma->rdmasr->RequireZerocopySendSpace(size);
}

#endif /* GRPC_POSIX_SOCKET_TCP */
