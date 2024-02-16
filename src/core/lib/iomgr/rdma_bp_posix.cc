#include <grpc/support/port_platform.h>

#include "src/core/lib/iomgr/port.h"

#ifdef GRPC_USE_IBVERBS

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <algorithm>

#include <grpc/slice.h>
#include <grpc/support/log.h>
#include <grpc/support/sync.h>
#include <grpcpp/stats_time.h>

#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/debug/stats_data.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/ibverbs/pair.h"
#include "src/core/lib/ibverbs/poller.h"
#include "src/core/lib/iomgr/buffer_list.h"
#include "src/core/lib/iomgr/ev_posix.h"
#include "src/core/lib/iomgr/executor.h"
#include "src/core/lib/iomgr/rdma_bp_posix.h"
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

  grpc_core::ibverbs::PairPollable* pair;
  bool enable_poller;

  /* garbage after the last read */
  grpc_slice_buffer last_read_buffer;
  grpc_slice_buffer* incoming_buffer;
  int inq;
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

  std::string peer_string;
  std::string local_address;

  grpc_resource_user* resource_user;
  grpc_resource_user_slice_allocator slice_allocator;
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

  if (rdma->pair != nullptr) {
    if (rdma->enable_poller) {
      grpc_core::ibverbs::Poller::Get().RemovePollable(rdma->pair);
    }
    rdma->pair->Disconnect();
    gpr_log(GPR_INFO, "Putback a Pair %p", rdma->pair);
    grpc_core::ibverbs::PairPool::Get().Putback(rdma->pair);
    rdma->pair = nullptr;
  }
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
  grpc_slice_buffer_reset_and_unref_internal(&rdma->last_read_buffer);
  RDMA_UNREF(rdma, "destroy");
}

static void call_read_cb(grpc_rdma* rdma, grpc_error_handle error) {
  grpc_closure* cb = rdma->read_cb;

  rdma->read_cb = nullptr;
  rdma->incoming_buffer = nullptr;
  grpc_core::Closure::Run(DEBUG_LOCATION, cb, error);
}

#define MAX_READ_IOVEC 4

static void rdma_do_read(grpc_rdma* rdma) {
  GRPCProfiler profiler(GRPC_STATS_TIME_TRANSPORT_DO_READ);

  struct iovec iov[MAX_READ_IOVEC];
  ssize_t read_bytes;
  size_t total_read_bytes = 0;
  size_t iov_len =
      std::min<size_t>(MAX_READ_IOVEC, rdma->incoming_buffer->count);
  auto* pair = rdma->pair;

  for (size_t i = 0; i < iov_len; i++) {
    iov[i].iov_base = GRPC_SLICE_START_PTR(rdma->incoming_buffer->slices[i]);
    iov[i].iov_len = GRPC_SLICE_LENGTH(rdma->incoming_buffer->slices[i]);
  }

  do {
    /* Assume there is something on the queue. If we receive TCP_INQ from
     * kernel, we will update this value, otherwise, we have to assume there is
     * always something to read until we get EAGAIN. */
    rdma->inq = 1;

    GRPC_STATS_INC_TCP_READ_OFFER(rdma->incoming_buffer->length);
    GRPC_STATS_INC_TCP_READ_OFFER_IOV_SIZE(rdma->incoming_buffer->count);

    read_bytes = 0;
    if (iov_len > 0) {
      read_bytes = pair->Recv(iov[0].iov_base, iov[0].iov_len);
    }

    /* We have read something in previous reads. We need to deliver those
     * bytes to the upper layer. */
    if (read_bytes == 0) {
      auto readable = pair->GetReadableSize();
      rdma->inq = readable > 0;

      if (total_read_bytes > 0) {
        break;
      } else {
        auto status = pair->get_status();

        if (status == grpc_core::ibverbs::PairStatus::kHalfClosed) {
          gpr_log(GPR_INFO, "Half close, Pair %p", pair);
          grpc_slice_buffer_reset_and_unref(rdma->incoming_buffer);
          call_read_cb(
              rdma,
              rdma_annotate_error(
                  GRPC_ERROR_CREATE_FROM_STATIC_STRING("Pair closed"), rdma));
          RDMA_UNREF(rdma, "read");
          return;
        } else if (status == grpc_core::ibverbs::PairStatus::kError) {
          gpr_log(GPR_INFO, "Half Error, Pair %p", pair);
          grpc_slice_buffer_reset_and_unref(rdma->incoming_buffer);
          std::string err = "Pair error, " + rdma->pair->get_error();
          call_read_cb(
              rdma,
              rdma_annotate_error(
                  GRPC_ERROR_CREATE_FROM_STATIC_STRING(err.c_str()), rdma));
          RDMA_UNREF(rdma, "read");
          return;
        }
        /* We've consumed the edge, request a new one */
        notify_on_read(rdma);
        return;
      }
    }

    GRPC_STATS_INC_TCP_READ_SIZE(read_bytes);
    //    add_to_estimate(rdma, static_cast<size_t>(read_bytes));
    GPR_DEBUG_ASSERT((size_t)read_bytes <=
                     rdma->incoming_buffer->length - total_read_bytes);

    total_read_bytes += read_bytes;
    if (rdma->inq == 0 || total_read_bytes == rdma->incoming_buffer->length) {
      /* We have filled incoming_buffer, and we cannot read any more. */
      break;
    }

    /* We had a partial read, and still have space to read more data.
     * So, adjust IOVs and try to read more. */
    size_t remaining = read_bytes;
    size_t j = 0;
    for (size_t i = 0; i < iov_len; i++) {
      if (remaining >= iov[i].iov_len) {
        remaining -= iov[i].iov_len;
        continue;
      }
      if (remaining > 0) {
        iov[j].iov_base = static_cast<char*>(iov[i].iov_base) + remaining;
        iov[j].iov_len = iov[i].iov_len - remaining;
        remaining = 0;
      } else {
        iov[j].iov_base = iov[i].iov_base;
        iov[j].iov_len = iov[i].iov_len;
      }
      ++j;
    }
    iov_len = j;
  } while (true);

  if (rdma->inq == 0) {
    //    finish_estimate(rdma);
  }

  GPR_DEBUG_ASSERT(total_read_bytes > 0);
  if (total_read_bytes < rdma->incoming_buffer->length) {
    grpc_slice_buffer_trim_end(rdma->incoming_buffer,
                               rdma->incoming_buffer->length - total_read_bytes,
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
  size_t target_read_size = std::max(256ul, rdma->pair->GetReadableSize());

  if (rdma->incoming_buffer->length == 0 &&
      rdma->incoming_buffer->count < MAX_READ_IOVEC) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_rdma_trace)) {
      gpr_log(GPR_INFO, "rdma allocate slice: %zu", target_read_size);
    }
    if (GPR_UNLIKELY(!grpc_resource_user_alloc_slices(&rdma->slice_allocator,
                                                      target_read_size, 1,
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
  } else if (!urgent && rdma->inq == 0) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_rdma_trace)) {
      gpr_log(GPR_INFO, "rdma_read, urgent, call notify_on_read");
    }
    notify_on_read(rdma);
  } else {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_rdma_trace)) {
      gpr_log(GPR_INFO, "rdma_read, call rdma_handle_read");
    }
    /* Not the first time. We may or may not have more bytes available. In any
     * case call rdma->read_done_closure (i.e rdma_handle_read()) which does the
     * right thing (i.e calls rdma_do_read() which either reads the available
     * bytes or calls notify_on_read() to be notified when new bytes become
     * available */
    grpc_core::Closure::Run(DEBUG_LOCATION, &rdma->read_done_closure,
                            GRPC_ERROR_NONE);
  }
}

#if defined(IOV_MAX) && IOV_MAX < 128
#define MAX_WRITE_IOVEC IOV_MAX
#else
#define MAX_WRITE_IOVEC 128
#endif

static bool rdma_flush(grpc_rdma* rdma, grpc_error_handle* error) {
  GRPCProfiler profiler(GRPC_STATS_TIME_TRANSPORT_FLUSH);
  struct iovec iov[MAX_WRITE_IOVEC];
  msg_iovlen_type iov_size;
  ssize_t sent_length;
  size_t sending_length;
  size_t trailing;
  size_t unwind_slice_idx;
  size_t unwind_byte_idx;

  // We always start at zero, because we eagerly unref and trim the slice
  // buffer as we write
  size_t outgoing_slice_idx = 0;
  auto* pair = rdma->pair;

  while (true) {
    sending_length = 0;
    unwind_slice_idx = outgoing_slice_idx;
    unwind_byte_idx = rdma->outgoing_byte_idx;
    for (iov_size = 0; outgoing_slice_idx != rdma->outgoing_buffer->count &&
                       iov_size != MAX_WRITE_IOVEC;
         iov_size++) {
      iov[iov_size].iov_base =
          GRPC_SLICE_START_PTR(
              rdma->outgoing_buffer->slices[outgoing_slice_idx]) +
          rdma->outgoing_byte_idx;
      iov[iov_size].iov_len =
          GRPC_SLICE_LENGTH(rdma->outgoing_buffer->slices[outgoing_slice_idx]) -
          rdma->outgoing_byte_idx;
      sending_length += iov[iov_size].iov_len;
      outgoing_slice_idx++;
      rdma->outgoing_byte_idx = 0;
    }
    GPR_ASSERT(iov_size > 0);

    sent_length = pair->Send(iov, iov_size);

    if (sent_length == 0) {
      auto status = pair->get_status();

      if (status == grpc_core::ibverbs::PairStatus::kConnected) {
        rdma->outgoing_byte_idx = unwind_byte_idx;
        // unref all and forget about all slices that have been written to this
        // point
        for (size_t idx = 0; idx < unwind_slice_idx; ++idx) {
          grpc_slice_buffer_remove_first(rdma->outgoing_buffer);
        }
        return false;
      } else if (status == grpc_core::ibverbs::PairStatus::kHalfClosed) {
        *error = rdma_annotate_error(
            GRPC_ERROR_CREATE_FROM_STATIC_STRING("Peer has been exited"), rdma);
        grpc_slice_buffer_reset_and_unref(rdma->outgoing_buffer);
        return true;
      } else {
        auto err = "RDMA Pair has an internal error, " + pair->get_error();
        *error = rdma_annotate_error(
            GRPC_ERROR_CREATE_FROM_STATIC_STRING(err.c_str()), rdma);
        grpc_slice_buffer_reset_and_unref(rdma->outgoing_buffer);
        return true;
      }
    }

    GPR_ASSERT(rdma->outgoing_byte_idx == 0);
    trailing = sending_length - static_cast<size_t>(sent_length);
    while (trailing > 0) {
      size_t slice_length;

      outgoing_slice_idx--;
      slice_length =
          GRPC_SLICE_LENGTH(rdma->outgoing_buffer->slices[outgoing_slice_idx]);
      if (slice_length > trailing) {
        rdma->outgoing_byte_idx = slice_length - trailing;
        break;
      } else {
        trailing -= slice_length;
      }
    }

    if (outgoing_slice_idx == rdma->outgoing_buffer->count) {
      *error = GRPC_ERROR_NONE;
      grpc_slice_buffer_reset_and_unref(rdma->outgoing_buffer);
      return true;
    }
  }
}

static void rdma_handle_write(void* arg /* grpc_rdma */,
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

// TODO: we may use grpc_fd_notify_on_read, refer
// tcp_server_posix.cc::tcp_server_start
int exchange_data(int fd, const char* buf_in, char* buf_out, const size_t sz) {
  size_t bytes_send = 0, bytes_recv = 0;
  if (fd < 3) {
    gpr_log(GPR_ERROR,
            "RDMAConn::sync_data, failed to sync data with remote, no opened "
            "socket(sd: %d)",
            fd);
    return -1;
  }

  struct pollfd fds[1];

  fds[0].fd = fd;
  fds[0].events = POLLIN | POLLOUT;

  while (bytes_recv < sz) {
    auto r = poll(fds, 1, -1);

    if (r > 0) {
      if (bytes_send < sz && (fds[0].revents & POLLOUT)) {
        ssize_t n = ::write(fd, buf_in + bytes_send, sz - bytes_send);

        if (n < 0) {
          if (errno == EINTR || errno == EAGAIN) {
          } else {
            gpr_log(GPR_ERROR, "exchange_data, write errno %d: %s", errno,
                    strerror(errno));
            return -1;
          }
        } else {
          bytes_send += n;
        }
      }

      if (fds[0].revents & POLLIN) {
        ssize_t n = ::read(fd, buf_out + bytes_recv, sz - bytes_recv);

        if (n < 0) {
          if (errno == EINTR || errno == EAGAIN) {
          } else {
            gpr_log(GPR_ERROR, "exchange_data, read errno %d: %s", errno,
                    strerror(errno));
            return -1;
          }
        } else {
          bytes_recv += n;
        }
      }
    }
  }

  return 0;
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
                                   const char* peer_string,
                                   bool enable_poller) {
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
  new (&rdma->refcount) grpc_core::RefCount(
      1, GRPC_TRACE_FLAG_ENABLED(grpc_rdma_trace) ? "rdma" : nullptr);
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
  rdma->inq = 1;

  auto* pair = grpc_core::ibverbs::PairPool::Get().Take();

  gpr_log(GPR_INFO, "Take a Pair %p", pair);

  pair->Init();

  std::vector<char> addr_bytes = pair->get_self_address().bytes();
  std::vector<char> peer_addr_bytes(addr_bytes.size());

  int err = exchange_data(rdma->fd, addr_bytes.data(), peer_addr_bytes.data(),
                          addr_bytes.size());

  GPR_ASSERT(err != -1);

  gpr_log(GPR_INFO, "Exchanging data finished, fd %d", rdma->fd);

  if (!pair->Connect(peer_addr_bytes)) {
    gpr_log(GPR_ERROR, "Connection failed");
    pair->Disconnect();  // Cleanup
    grpc_core::ibverbs::PairPool::Get().Putback(pair);
    gpr_log(GPR_INFO, "Put a Pair %p", pair);
    delete rdma;
    return nullptr;
  }

  rdma->pair = pair;
  rdma->enable_poller = enable_poller;
  grpc_fd_set_arg(em_fd, pair);  // pass pair to grpc_fd

  if (enable_poller) {
    grpc_core::ibverbs::Poller::Get().AddPollable(pair);
    gpr_log(GPR_INFO, "Add pair %p", pair);
  }

  return &rdma->base;
}

void* grpc_rdma_bp_require_zerocopy_sendspace(grpc_endpoint* ep, size_t size) {
  // FIXME:
  return nullptr;
}

#endif /* GRPC_POSIX_SOCKET_TCP */
