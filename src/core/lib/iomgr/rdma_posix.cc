//
//
// Copyright 2015 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//

#include "absl/base/thread_annotations.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

#include <grpc/impl/grpc_types.h>
#include <grpc/support/port_platform.h>

#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/port.h"

#ifdef GRPC_USE_IBVERBS

#include <errno.h>
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

#include "absl/log/check.h"
#include "absl/log/log.h"

#include <grpc/slice.h>
#include <grpc/support/alloc.h>
#include <grpc/support/string_util.h>
#include <grpc/support/sync.h>
#include <grpc/support/time.h>

#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/debug/event_log.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/experiments/experiments.h"
#include "src/core/lib/gprpp/crash.h"
#include "src/core/lib/gprpp/strerror.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/ibverbs/pair.h"
#include "src/core/lib/ibverbs/poller.h"
#include "src/core/lib/iomgr/buffer_list.h"
#include "src/core/lib/iomgr/ev_posix.h"
#include "src/core/lib/iomgr/event_engine_shims/endpoint.h"
#include "src/core/lib/iomgr/executor.h"
#include "src/core/lib/iomgr/rdma_posix.h"
#include "src/core/lib/iomgr/socket_utils_posix.h"
#include "src/core/lib/resource_quota/api.h"
#include "src/core/lib/resource_quota/memory_quota.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/slice/slice_string_helpers.h"
#include "src/core/telemetry/stats.h"
#include "src/core/telemetry/stats_data.h"
#include "src/core/util/string.h"
#include "src/core/util/useful.h"

namespace {

struct grpc_rdma {
  explicit grpc_rdma(const grpc_core::PosixTcpOptions& tcp_options) {}
  grpc_endpoint base;
  grpc_fd* em_fd;
  int fd;
  int inq;  // bytes pending on the socket from the last read.
  double target_length;
  double bytes_read_this_round;
  grpc_core::RefCount refcount;
  gpr_atm shutdown_count;

  grpc_core::ibverbs::PairPollable* pair;

  // garbage after the last read
  grpc_slice_buffer last_read_buffer;

  grpc_core::Mutex read_mu;
  grpc_slice_buffer* incoming_buffer ABSL_GUARDED_BY(read_mu) = nullptr;

  grpc_slice_buffer* outgoing_buffer;
  // byte within outgoing_buffer->slices[0] to write next
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

  grpc_core::MemoryOwner memory_owner;
  grpc_core::MemoryAllocator::Reservation self_reservation;

  int min_progress_size;  // A hint from upper layers specifying the minimum
                          // number of bytes that need to be read to make
                          // meaningful progress

  gpr_atm stop_error_notification;  // Set to 1 if we do not want to be notified
                                    // on errors anymore

  int set_rcvlowat = 0;

  // Used by the endpoint read function to distinguish the very first read call
  // from the rest
  bool is_first_read;
  bool has_posted_reclaimer ABSL_GUARDED_BY(read_mu) = false;
};

struct backup_poller {
  gpr_mu* pollset_mu;
  grpc_closure run_poller;
};

void LogCommonIOErrors(absl::string_view prefix, int error_no) {
  switch (error_no) {
    case ECONNABORTED:
      grpc_core::global_stats().IncrementEconnabortedCount();
      return;
    case ECONNRESET:
      grpc_core::global_stats().IncrementEconnresetCount();
      return;
    case EPIPE:
      grpc_core::global_stats().IncrementEpipeCount();
      return;
    case ETIMEDOUT:
      grpc_core::global_stats().IncrementEtimedoutCount();
      return;
    case ECONNREFUSED:
      grpc_core::global_stats().IncrementEconnrefusedCount();
      return;
    case ENETUNREACH:
      grpc_core::global_stats().IncrementEnetunreachCount();
      return;
    case ENOMSG:
      grpc_core::global_stats().IncrementEnomsgCount();
      return;
    case ENOTCONN:
      grpc_core::global_stats().IncrementEnotconnCount();
      return;
    case ENOBUFS:
      grpc_core::global_stats().IncrementEnobufsCount();
      return;
    default:
      grpc_core::global_stats().IncrementUncommonIoErrorCount();
      LOG_EVERY_N_SEC(ERROR, 1)
          << prefix.data()
          << " encountered uncommon error: " << grpc_core::StrError(error_no);
      return;
  }
}

}  // namespace

#define BACKUP_POLLER_POLLSET(b) ((grpc_pollset*)((b) + 1))

static grpc_core::Mutex* g_backup_poller_mu = nullptr;
static int g_uncovered_notifications_pending
    ABSL_GUARDED_BY(g_backup_poller_mu);
static backup_poller* g_backup_poller ABSL_GUARDED_BY(g_backup_poller_mu);

static void rdma_handle_read(void* arg /* grpc_rdma */,
                             grpc_error_handle error);
static void rdma_handle_write(void* arg /* grpc_rdma */,
                              grpc_error_handle error);
static void rdma_drop_uncovered_then_handle_write(void* arg /* grpc_rdma */,
                                                  grpc_error_handle error);

static void done_poller(void* bp, grpc_error_handle /*error_ignored*/) {
  backup_poller* p = static_cast<backup_poller*>(bp);
  GRPC_TRACE_LOG(rdma, INFO) << "BACKUP_POLLER:" << p << " destroy";
  grpc_pollset_destroy(BACKUP_POLLER_POLLSET(p));
  gpr_free(p);
}

static void run_poller(void* bp, grpc_error_handle /*error_ignored*/) {
  backup_poller* p = static_cast<backup_poller*>(bp);
  GRPC_TRACE_LOG(rdma, INFO) << "BACKUP_POLLER:" << p << " run";
  gpr_mu_lock(p->pollset_mu);
  grpc_core::Timestamp deadline =
      grpc_core::Timestamp::Now() + grpc_core::Duration::Seconds(10);
  GRPC_LOG_IF_ERROR(
      "backup_poller:pollset_work",
      grpc_pollset_work(BACKUP_POLLER_POLLSET(p), nullptr, deadline));
  gpr_mu_unlock(p->pollset_mu);
  g_backup_poller_mu->Lock();
  // last "uncovered" notification is the ref that keeps us polling
  if (g_uncovered_notifications_pending == 1) {
    CHECK(g_backup_poller == p);
    g_backup_poller = nullptr;
    g_uncovered_notifications_pending = 0;
    g_backup_poller_mu->Unlock();
    GRPC_TRACE_LOG(rdma, INFO) << "BACKUP_POLLER:" << p << " shutdown";
    grpc_pollset_shutdown(BACKUP_POLLER_POLLSET(p),
                          GRPC_CLOSURE_INIT(&p->run_poller, done_poller, p,
                                            grpc_schedule_on_exec_ctx));
  } else {
    g_backup_poller_mu->Unlock();
    GRPC_TRACE_LOG(rdma, INFO) << "BACKUP_POLLER:" << p << " reschedule";
    grpc_core::Executor::Run(&p->run_poller, absl::OkStatus(),
                             grpc_core::ExecutorType::DEFAULT,
                             grpc_core::ExecutorJobType::LONG);
  }
}

static void drop_uncovered(grpc_rdma* /*rdma*/) {
  int old_count;
  backup_poller* p;
  g_backup_poller_mu->Lock();
  p = g_backup_poller;
  old_count = g_uncovered_notifications_pending--;
  g_backup_poller_mu->Unlock();
  CHECK_GT(old_count, 1);
  GRPC_TRACE_LOG(rdma, INFO) << "BACKUP_POLLER:" << p << " uncover cnt "
                             << old_count << "->" << old_count - 1;
}

// gRPC API considers a Write operation to be done the moment it clears ‘flow
// control’ i.e., not necessarily sent on the wire. This means that the
// application MIGHT not call `grpc_completion_queue_next/pluck` in a timely
// manner when its `Write()` API is acked.
//
// We need to ensure that the fd is 'covered' (i.e being monitored by some
// polling thread and progress is made) and hence add it to a backup poller here
static void cover_self(grpc_rdma* rdma) {
  backup_poller* p;
  g_backup_poller_mu->Lock();
  int old_count = 0;
  if (g_uncovered_notifications_pending == 0) {
    g_uncovered_notifications_pending = 2;
    p = static_cast<backup_poller*>(
        gpr_zalloc(sizeof(*p) + grpc_pollset_size()));
    g_backup_poller = p;
    grpc_pollset_init(BACKUP_POLLER_POLLSET(p), &p->pollset_mu);
    g_backup_poller_mu->Unlock();
    GRPC_TRACE_LOG(rdma, INFO) << "BACKUP_POLLER:" << p << " create";
    grpc_core::Executor::Run(
        GRPC_CLOSURE_INIT(&p->run_poller, run_poller, p, nullptr),
        absl::OkStatus(), grpc_core::ExecutorType::DEFAULT,
        grpc_core::ExecutorJobType::LONG);
  } else {
    old_count = g_uncovered_notifications_pending++;
    p = g_backup_poller;
    g_backup_poller_mu->Unlock();
  }
  GRPC_TRACE_LOG(rdma, INFO) << "BACKUP_POLLER:" << p << " add " << rdma
                             << " cnt " << old_count - 1 << "->" << old_count;
  grpc_pollset_add_fd(BACKUP_POLLER_POLLSET(p), rdma->em_fd);
}

static void notify_on_read(grpc_rdma* rdma) {
  GRPC_TRACE_LOG(rdma, INFO) << "RDMA:" << rdma << " notify_on_read";
  grpc_fd_notify_on_read(rdma->em_fd, &rdma->read_done_closure);
}

static void notify_on_write(grpc_rdma* rdma) {
  GRPC_TRACE_LOG(rdma, INFO) << "RDMA:" << rdma << " notify_on_write";
  if (!grpc_event_engine_run_in_background()) {
    cover_self(rdma);
  }
  grpc_fd_notify_on_write(rdma->em_fd, &rdma->write_done_closure);
}

// static void tcp_drop_uncovered_then_handle_write(void* arg,
//                                                  grpc_error_handle error) {
//   GRPC_TRACE_LOG(rdma, INFO)
//       << "TCP:" << arg << " got_write: " << grpc_core::StatusToString(error);
//   drop_uncovered(static_cast<grpc_rdma*>(arg));
//   tcp_handle_write(arg, error);
// }

static void add_to_estimate(grpc_rdma* rdma, size_t bytes) {
  rdma->bytes_read_this_round += static_cast<double>(bytes);
}

static void finish_estimate(grpc_rdma* rdma) {
  // If we read >80% of the target buffer in one read loop, increase the size
  // of the target buffer to either the amount read, or twice its previous
  // value
  if (rdma->bytes_read_this_round > rdma->target_length * 0.8) {
    rdma->target_length =
        std::max(2 * rdma->target_length, rdma->bytes_read_this_round);
  } else {
    rdma->target_length =
        0.99 * rdma->target_length + 0.01 * rdma->bytes_read_this_round;
  }
  rdma->bytes_read_this_round = 0;
}

static grpc_error_handle rdma_annotate_error(grpc_error_handle src_error,
                                             grpc_rdma* rdma) {
  return grpc_error_set_int(
      grpc_error_set_int(src_error, grpc_core::StatusIntProperty::kFd,
                         rdma->fd),
      // All rdma errors are marked with UNAVAILABLE so that application may
      // choose to retry.
      grpc_core::StatusIntProperty::kRpcStatus, GRPC_STATUS_UNAVAILABLE);
}

static void rdma_free(grpc_rdma* rdma) {
  grpc_fd_orphan(rdma->em_fd, rdma->release_fd_cb, rdma->release_fd,
                 "tcp_unref_orphan");
  grpc_slice_buffer_destroy(&rdma->last_read_buffer);
  if (rdma->pair != nullptr) {
    grpc_core::ibverbs::Poller::Get().RemovePollable(rdma->pair);
    rdma->pair->Disconnect();
    LOG(INFO) << "Putback a Pair " << rdma->pair;
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
  grpc_fd_shutdown(rdma->em_fd, absl::UnavailableError("endpoint shutdown"));
  if (grpc_event_engine_can_track_errors()) {
    gpr_atm_no_barrier_store(&rdma->stop_error_notification, true);
    grpc_fd_set_error(rdma->em_fd);
  }
  rdma->read_mu.Lock();
  rdma->memory_owner.Reset();
  rdma->read_mu.Unlock();
  RDMA_UNREF(rdma, "destroy");
}

static void perform_reclamation(grpc_rdma* rdma)
    ABSL_LOCKS_EXCLUDED(rdma->read_mu) {
  GRPC_TRACE_LOG(resource_quota, INFO)
      << "RDMA: benign reclamation to free memory";
  rdma->read_mu.Lock();
  if (rdma->incoming_buffer != nullptr) {
    grpc_slice_buffer_reset_and_unref(rdma->incoming_buffer);
  }
  rdma->has_posted_reclaimer = false;
  rdma->read_mu.Unlock();
}

static void maybe_post_reclaimer(grpc_rdma* rdma)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(rdma->read_mu) {
  if (!rdma->has_posted_reclaimer) {
    rdma->has_posted_reclaimer = true;
    RDMA_REF(rdma, "posted_reclaimer");
    rdma->memory_owner.PostReclaimer(
        grpc_core::ReclamationPass::kBenign,
        [rdma](absl::optional<grpc_core::ReclamationSweep> sweep) {
          if (sweep.has_value()) {
            perform_reclamation(rdma);
          }
          RDMA_UNREF(rdma, "posted_reclaimer");
        });
  }
}

static void rdma_trace_read(grpc_rdma* rdma, grpc_error_handle error)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(rdma->read_mu) {
  grpc_closure* cb = rdma->read_cb;
  if (GRPC_TRACE_FLAG_ENABLED(rdma)) {
    LOG(INFO) << "RDMA:" << rdma << " call_cb " << cb << " " << cb->cb << ":"
              << cb->cb_arg;
    size_t i;
    LOG(INFO) << "READ " << rdma << " (peer=" << rdma->peer_string
              << ") error=" << grpc_core::StatusToString(error);
    if (ABSL_VLOG_IS_ON(2)) {
      for (i = 0; i < rdma->incoming_buffer->count; i++) {
        char* dump = grpc_dump_slice(rdma->incoming_buffer->slices[i],
                                     GPR_DUMP_HEX | GPR_DUMP_ASCII);
        VLOG(2) << "READ DATA: " << dump;
        gpr_free(dump);
      }
    }
  }
}

static void update_rcvlowat(grpc_rdma* rdma)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(rdma->read_mu) {
  if (!grpc_core::IsTcpRcvLowatEnabled()) return;

  // TODO(ctiller): Check if supported by OS.
  // TODO(ctiller): Allow some adjustments instead of hardcoding things.

  static constexpr int kRcvLowatMax = 16 * 1024 * 1024;
  static constexpr int kRcvLowatThreshold = 16 * 1024;

  int remaining = std::min(static_cast<int>(rdma->incoming_buffer->length),
                           rdma->min_progress_size);

  remaining = std::min(remaining, kRcvLowatMax);

  // Setting SO_RCVLOWAT for small quantities does not save on CPU.
  if (remaining < 2 * kRcvLowatThreshold) {
    remaining = 0;
  }

  // Decrement remaining by kRcvLowatThreshold. This would have the effect of
  // waking up a little early. It would help with latency because some bytes
  // may arrive while we execute the recvmsg syscall after waking up.
  if (remaining > 0) {
    remaining -= kRcvLowatThreshold;
  }

  // We still do not know the RPC size. Do not set SO_RCVLOWAT.
  if (rdma->set_rcvlowat <= 1 && remaining <= 1) return;

  // Previous value is still valid. No change needed in SO_RCVLOWAT.
  if (rdma->set_rcvlowat == remaining) {
    return;
  }
  if (setsockopt(rdma->fd, SOL_SOCKET, SO_RCVLOWAT, &remaining,
                 sizeof(remaining)) != 0) {
    LOG(ERROR) << "Cannot set SO_RCVLOWAT on fd=" << rdma->fd
               << " err=" << grpc_core::StrError(errno);
    return;
  }
  rdma->set_rcvlowat = remaining;
}

// Returns true if data available to read or error other than EAGAIN.
#define MAX_READ_IOVEC 64
static bool rdma_do_read(grpc_rdma* rdma, grpc_error_handle* error)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(rdma->read_mu) {
  GRPC_LATENT_SEE_INNER_SCOPE("rdma_do_read");
  GRPC_TRACE_LOG(rdma, INFO) << "RDMA:" << rdma << " do_read";
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

  CHECK_NE(rdma->incoming_buffer->length, 0u);
  DCHECK_GT(rdma->min_progress_size, 0);

  do {
    // Assume there is something on the queue. If we receive TCP_INQ from
    // kernel, we will update this value, otherwise, we have to assume there is
    // always something to read until we get EAGAIN.
    rdma->inq = 1;

    grpc_core::global_stats().IncrementTcpReadOffer(
        rdma->incoming_buffer->length);
    grpc_core::global_stats().IncrementTcpReadOfferIovSize(
        rdma->incoming_buffer->count);

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
          LOG(ERROR) << "Half closed, Pair " << pair;
          grpc_slice_buffer_reset_and_unref(rdma->incoming_buffer);
          *error =
              rdma_annotate_error(absl::InternalError("Pair closed"), rdma);
          return true;
        } else if (status == grpc_core::ibverbs::PairStatus::kError) {
          LOG(ERROR) << "Pair error, Pair " << pair;
          grpc_slice_buffer_reset_and_unref(rdma->incoming_buffer);
          std::string err = "Pair error, " + rdma->pair->get_error();
          *error = rdma_annotate_error(absl::InternalError(err), rdma);

          return true;
        }
        finish_estimate(rdma);
        return false;
      }
    }

    grpc_core::global_stats().IncrementTcpReadSize(read_bytes);
    add_to_estimate(rdma, static_cast<size_t>(read_bytes));
    DCHECK((size_t)read_bytes <=
           rdma->incoming_buffer->length - total_read_bytes);

    total_read_bytes += read_bytes;
    if (rdma->inq == 0 || total_read_bytes == rdma->incoming_buffer->length) {
      /* We have filled incoming_buffer, and we cannot read any more. */
      break;
    }

    // We had a partial read, and still have space to read more data.
    // So, adjust IOVs and try to read more.
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
    finish_estimate(rdma);
  }

  DCHECK_GT(total_read_bytes, 0u);
  *error = absl::OkStatus();
  if (grpc_core::IsTcpFrameSizeTuningEnabled()) {
    // Update min progress size based on the total number of bytes read in
    // this round.
    rdma->min_progress_size -= total_read_bytes;
    if (rdma->min_progress_size > 0) {
      // There is still some bytes left to be read before we can signal
      // the read as complete. Append the bytes read so far into
      // last_read_buffer which serves as a staging buffer. Return false
      // to indicate tcp_handle_read needs to be scheduled again.
      grpc_slice_buffer_move_first(rdma->incoming_buffer, total_read_bytes,
                                   &rdma->last_read_buffer);
      return false;
    } else {
      // The required number of bytes have been read. Append the bytes
      // read in this round into last_read_buffer. Then swap last_read_buffer
      // and incoming_buffer. Now incoming buffer contains all the bytes
      // read since the start of the last tcp_read operation. last_read_buffer
      // would contain any spare space left in the incoming buffer. This
      // space will be used in the next tcp_read operation.
      rdma->min_progress_size = 1;
      grpc_slice_buffer_move_first(rdma->incoming_buffer, total_read_bytes,
                                   &rdma->last_read_buffer);
      grpc_slice_buffer_swap(&rdma->last_read_buffer, rdma->incoming_buffer);
      return true;
    }
  }
  if (total_read_bytes < rdma->incoming_buffer->length) {
    grpc_slice_buffer_trim_end(rdma->incoming_buffer,
                               rdma->incoming_buffer->length - total_read_bytes,
                               &rdma->last_read_buffer);
  }
  return true;
}

static void maybe_make_read_slices(grpc_rdma* rdma)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(rdma->read_mu) {
  static const int kBigAlloc = 64 * 1024;
  static const int kSmallAlloc = 8 * 1024;
  if (rdma->incoming_buffer->length <
      std::max<size_t>(rdma->min_progress_size, 1)) {
    size_t allocate_length = rdma->min_progress_size;
    const size_t target_length = static_cast<size_t>(rdma->target_length);
    // If memory pressure is low and we think there will be more than
    // min_progress_size bytes to read, allocate a bit more.
    const bool low_memory_pressure =
        rdma->memory_owner.GetPressureInfo().pressure_control_value < 0.8;
    if (low_memory_pressure && target_length > allocate_length) {
      allocate_length = target_length;
    }
    int extra_wanted = std::max<int>(
        1, allocate_length - static_cast<int>(rdma->incoming_buffer->length));
    if (extra_wanted >=
        (low_memory_pressure ? kSmallAlloc * 3 / 2 : kBigAlloc)) {
      while (extra_wanted > 0) {
        extra_wanted -= kBigAlloc;
        grpc_slice_buffer_add_indexed(rdma->incoming_buffer,
                                      rdma->memory_owner.MakeSlice(kBigAlloc));
        grpc_core::global_stats().IncrementTcpReadAlloc64k();
      }
    } else {
      while (extra_wanted > 0) {
        extra_wanted -= kSmallAlloc;
        grpc_slice_buffer_add_indexed(
            rdma->incoming_buffer, rdma->memory_owner.MakeSlice(kSmallAlloc));
        grpc_core::global_stats().IncrementTcpReadAlloc8k();
      }
    }
    maybe_post_reclaimer(rdma);
  }
}

static void rdma_handle_read(void* arg /* grpc_rdma */,
                             grpc_error_handle error) {
  grpc_rdma* rdma = static_cast<grpc_rdma*>(arg);
  GRPC_TRACE_LOG(rdma, INFO)
      << "RDMA:" << rdma << " got_read: " << grpc_core::StatusToString(error);
  rdma->read_mu.Lock();
  grpc_error_handle tcp_read_error;
  if (GPR_LIKELY(error.ok()) && rdma->memory_owner.is_valid()) {
    maybe_make_read_slices(rdma);
    if (!rdma_do_read(rdma, &tcp_read_error)) {
      // Maybe update rcv lowat value based on the number of bytes read in this
      // round.
      update_rcvlowat(rdma);
      rdma->read_mu.Unlock();
      // We've consumed the edge, request a new one
      notify_on_read(rdma);
      return;
    }
    rdma_trace_read(rdma, tcp_read_error);
  } else {
    if (!rdma->memory_owner.is_valid() && error.ok()) {
      tcp_read_error =
          rdma_annotate_error(absl::InternalError("Socket closed"), rdma);
    } else {
      tcp_read_error = error;
    }
    grpc_slice_buffer_reset_and_unref(rdma->incoming_buffer);
    grpc_slice_buffer_reset_and_unref(&rdma->last_read_buffer);
  }
  // Update rcv lowat needs to be called at the end of the current read
  // operation to ensure the right SO_RCVLOWAT value is set for the next read.
  // Otherwise the next endpoint read operation may get stuck indefinitely
  // because the previously set rcv lowat value will persist and the socket may
  // erroneously considered to not be ready for read.
  update_rcvlowat(rdma);
  grpc_closure* cb = rdma->read_cb;
  rdma->read_cb = nullptr;
  rdma->incoming_buffer = nullptr;
  rdma->read_mu.Unlock();
  grpc_core::Closure::Run(DEBUG_LOCATION, cb, tcp_read_error);
  RDMA_UNREF(rdma, "read");
}

static void rdma_read(grpc_endpoint* ep, grpc_slice_buffer* incoming_buffer,
                      grpc_closure* cb, bool urgent, int min_progress_size) {
  grpc_rdma* rdma = reinterpret_cast<grpc_rdma*>(ep);
  CHECK_EQ(rdma->read_cb, nullptr);
  rdma->read_cb = cb;
  rdma->read_mu.Lock();
  rdma->incoming_buffer = incoming_buffer;
  rdma->min_progress_size = grpc_core::IsTcpFrameSizeTuningEnabled()
                                ? std::max(min_progress_size, 1)
                                : 1;
  grpc_slice_buffer_reset_and_unref(incoming_buffer);
  grpc_slice_buffer_swap(incoming_buffer, &rdma->last_read_buffer);
  RDMA_REF(rdma, "read");
  if (rdma->is_first_read) {
    rdma->read_mu.Unlock();
    // Endpoint read called for the very first time. Register read callback with
    // the polling engine
    rdma->is_first_read = false;
    notify_on_read(rdma);
  } else if (!urgent && rdma->inq == 0) {
    rdma->read_mu.Unlock();
    // Upper layer asked to read more but we know there is no pending data
    // to read from previous reads. So, wait for POLLIN.
    //
    notify_on_read(rdma);
  } else {
    rdma->read_mu.Unlock();
    // Not the first time. We may or may not have more bytes available. In any
    // case call rdma->read_done_closure (i.e tcp_handle_read()) which does the
    // right thing (i.e calls tcp_do_read() which either reads the available
    // bytes or calls notify_on_read() to be notified when new bytes become
    // available
    grpc_core::Closure::Run(DEBUG_LOCATION, &rdma->read_done_closure,
                            absl::OkStatus());
  }
}

static void tcp_handle_error(void* /*arg*/ /* grpc_rdma */,
                             grpc_error_handle /*error*/) {
  LOG(ERROR) << "Error handling is not supported for this platform";
  CHECK(0);
}

#if defined(IOV_MAX) && IOV_MAX < 260
#define MAX_WRITE_IOVEC IOV_MAX
#else
#define MAX_WRITE_IOVEC 260
#endif

static bool rdma_flush(grpc_rdma* rdma, grpc_error_handle* error) {
  auto* pair = rdma->pair;
  // We always start at zero, because we eagerly unref and trim the slice
  // buffer as we write
  size_t outgoing_slice_idx = 0;

  ABSL_CHECK_GT(rdma->outgoing_buffer->count, 0);

  size_t sent_length =
      pair->Send(rdma->outgoing_buffer->slices, rdma->outgoing_buffer->count,
                 rdma->outgoing_byte_idx);

  while (sent_length > 0) {
    auto slice_len =
        GRPC_SLICE_LENGTH(rdma->outgoing_buffer->slices[outgoing_slice_idx]) -
        rdma->outgoing_byte_idx;

    if (sent_length >= slice_len) {
      sent_length -= slice_len;
      outgoing_slice_idx++;
      rdma->outgoing_byte_idx = 0;
    } else {
      rdma->outgoing_byte_idx += sent_length;
      break;
    }
  }

  // Partial send
  if (rdma->outgoing_byte_idx > 0 ||
      outgoing_slice_idx < rdma->outgoing_buffer->count) {
    auto status = pair->get_status();

    if (status == grpc_core::ibverbs::PairStatus::kConnected) {
      // unref all and forget about all slices that have been written to this
      // point
      for (size_t idx = 0; idx < outgoing_slice_idx; ++idx) {
        grpc_slice_buffer_remove_first(rdma->outgoing_buffer);
      }
      return false;
    } else if (status == grpc_core::ibverbs::PairStatus::kHalfClosed) {
      *error =
          rdma_annotate_error(GRPC_ERROR_CREATE("Peer has been exited"), rdma);
      grpc_slice_buffer_reset_and_unref(rdma->outgoing_buffer);
      return true;
    } else {
      auto err = "RDMA Pair has an internal error, " + pair->get_error();
      *error = rdma_annotate_error(GRPC_ERROR_CREATE(err.c_str()), rdma);
      grpc_slice_buffer_reset_and_unref(rdma->outgoing_buffer);
      return true;
    }
  } else {
    *error = absl::OkStatus();
    grpc_slice_buffer_reset_and_unref(rdma->outgoing_buffer);
    return true;
  }
}

static void rdma_handle_write(void* arg /* grpc_rdma */,
                              grpc_error_handle error) {
  grpc_rdma* rdma = static_cast<grpc_rdma*>(arg);
  grpc_closure* cb;

  if (!error.ok()) {
    cb = rdma->write_cb;
    rdma->write_cb = nullptr;
    grpc_core::Closure::Run(DEBUG_LOCATION, cb, error);
    RDMA_UNREF(rdma, "write");
    return;
  }

  bool flush_result = rdma_flush(rdma, &error);
  if (!flush_result) {
    GRPC_TRACE_LOG(rdma, INFO) << "write: delayed";
    notify_on_write(rdma);
    // tcp_flush does not populate error if it has returned false.
    DCHECK(error.ok());
  } else {
    cb = rdma->write_cb;
    rdma->write_cb = nullptr;
    GRPC_TRACE_LOG(rdma, INFO) << "write: " << grpc_core::StatusToString(error);
    // No need to take a ref on error since tcp_flush provides a ref.
    grpc_core::Closure::Run(DEBUG_LOCATION, cb, error);
    RDMA_UNREF(rdma, "write");
  }
}

static void rdma_write(grpc_endpoint* ep, grpc_slice_buffer* buf,
                       grpc_closure* cb, void* arg, int /*max_frame_size*/) {
  grpc_rdma* rdma = reinterpret_cast<grpc_rdma*>(ep);
  grpc_error_handle error;

  grpc_core::EventLog::Append("rdma-write-outstanding", buf->length);

  if (GRPC_TRACE_FLAG_ENABLED(rdma)) {
    size_t i;

    for (i = 0; i < buf->count; i++) {
      LOG(INFO) << "WRITE " << rdma << " (peer=" << rdma->peer_string << ")";
      if (ABSL_VLOG_IS_ON(2)) {
        char* data =
            grpc_dump_slice(buf->slices[i], GPR_DUMP_HEX | GPR_DUMP_ASCII);
        VLOG(2) << "WRITE DATA: " << data;
        gpr_free(data);
      }
    }
  }

  CHECK_EQ(rdma->write_cb, nullptr);

  if (buf->length == 0) {
    grpc_core::Closure::Run(
        DEBUG_LOCATION, cb,
        grpc_fd_is_shutdown(rdma->em_fd)
            ? rdma_annotate_error(GRPC_ERROR_CREATE("EOF"), rdma)
            : absl::OkStatus());
    return;
  }

  // Either not enough bytes, or couldn't allocate a zerocopy context.
  rdma->outgoing_buffer = buf;
  rdma->outgoing_byte_idx = 0;

  bool flush_result = rdma_flush(rdma, &error);
  if (!flush_result) {
    RDMA_REF(rdma, "write");
    rdma->write_cb = cb;
    GRPC_TRACE_LOG(rdma, INFO) << "write: delayed";
    notify_on_write(rdma);
  } else {
    GRPC_TRACE_LOG(rdma, INFO) << "write: " << grpc_core::StatusToString(error);
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

static bool rdma_can_track_err(grpc_endpoint* ep) {
  grpc_rdma* rdma = reinterpret_cast<grpc_rdma*>(ep);
  return false;
}

// TODO: we may use grpc_fd_notify_on_read, refer
// tcp_server_posix.cc::tcp_server_start
int exchange_data(int fd, const char* buf_in, char* buf_out, const size_t sz) {
  size_t bytes_send = 0, bytes_recv = 0;
  if (fd < 3) {
    LOG(ERROR) << "failed to sync data with remote, no opened socket " << fd;
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
            int err = errno;
            LOG(ERROR) << "exchange_data, write error, errno " << err
                       << " errmsg " << strerror(err);
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
            int err = errno;
            LOG(ERROR) << "exchange_data, read error, errno " << err
                       << " errmsg " << strerror(err);
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
                                            rdma_destroy,
                                            rdma_get_peer,
                                            rdma_get_local_address,
                                            rdma_get_fd,
                                            rdma_can_track_err};

grpc_endpoint* grpc_rdma_create(grpc_fd* em_fd,
                                const grpc_core::PosixTcpOptions& options,
                                absl::string_view peer_string) {
  grpc_rdma* rdma = new grpc_rdma(options);
  rdma->base.vtable = &vtable;
  rdma->peer_string = std::string(peer_string);
  rdma->fd = grpc_fd_wrapped_fd(em_fd);
  CHECK(options.resource_quota != nullptr);
  rdma->memory_owner =
      options.resource_quota->memory_quota()->CreateMemoryOwner();
  rdma->self_reservation =
      rdma->memory_owner.MakeReservation(sizeof(grpc_rdma));
  grpc_resolved_address resolved_local_addr;
  memset(&resolved_local_addr, 0, sizeof(resolved_local_addr));
  resolved_local_addr.len = sizeof(resolved_local_addr.addr);
  absl::StatusOr<std::string> addr_uri;
  if (getsockname(rdma->fd,
                  reinterpret_cast<sockaddr*>(resolved_local_addr.addr),
                  &resolved_local_addr.len) < 0 ||
      !(addr_uri = grpc_sockaddr_to_uri(&resolved_local_addr)).ok()) {
    rdma->local_address = "";
  } else {
    rdma->local_address = addr_uri.value();
  }
  rdma->read_cb = nullptr;
  rdma->write_cb = nullptr;
  rdma->release_fd_cb = nullptr;
  rdma->release_fd = nullptr;
  rdma->target_length = static_cast<double>(options.tcp_read_chunk_size);
  rdma->bytes_read_this_round = 0;
  // Will be set to false by the very first endpoint read function
  rdma->is_first_read = true;
  rdma->min_progress_size = 1;
  // paired with unref in grpc_rdma_destroy
  new (&rdma->refcount)
      grpc_core::RefCount(1, GRPC_TRACE_FLAG_ENABLED(rdma) ? "rdma" : nullptr);
  gpr_atm_no_barrier_store(&rdma->shutdown_count, 0);
  rdma->em_fd = em_fd;
  grpc_slice_buffer_init(&rdma->last_read_buffer);
  GRPC_CLOSURE_INIT(&rdma->read_done_closure, rdma_handle_read, rdma,
                    grpc_schedule_on_exec_ctx);
  GRPC_CLOSURE_INIT(&rdma->write_done_closure, rdma_handle_write, rdma,
                    grpc_schedule_on_exec_ctx);
  // Always assume there is something on the queue to read.
  rdma->inq = 1;
  // Start being notified on errors if event engine can track errors.
  // if (grpc_event_engine_can_track_errors()) {
  //   // Grab a ref to rdma so that we can safely access the rdma struct when
  //   // processing errors. We unref when we no longer want to track errors
  //   // separately.
  //   RDMA_REF(rdma, "error-tracking");
  //   gpr_atm_rel_store(&rdma->stop_error_notification, 0);
  //   GRPC_CLOSURE_INIT(&rdma->error_closure, tcp_handle_error, rdma,
  //                     grpc_schedule_on_exec_ctx);
  //   grpc_fd_notify_on_error(rdma->em_fd, &rdma->error_closure);
  // }

  std::string pair_id = std::to_string((long)em_fd) + rdma->peer_string;
  auto* pair = grpc_core::ibverbs::PairPool::Get().Take(pair_id);
  LOG(INFO) << "Take a Pair " << pair << ", peer " << rdma->peer_string;
  pair->Init();

  std::vector<char> addr_bytes = pair->get_self_address().bytes();
  std::vector<char> peer_addr_bytes(addr_bytes.size());

  int err = exchange_data(rdma->fd, addr_bytes.data(), peer_addr_bytes.data(),
                          addr_bytes.size());

  ABSL_CHECK_NE(err, -1);

  LOG(INFO) << "Exchanging data finished, fd %d" << rdma->fd;

  if (!pair->Connect(peer_addr_bytes)) {
    LOG(INFO) << "Connection failed";
    pair->Disconnect();  // Cleanup
    grpc_core::ibverbs::PairPool::Get().Putback(pair);
    LOG(INFO) << "Putback a Pair " << pair;
    delete rdma;
    return nullptr;
  }

  rdma->pair = pair;
  grpc_core::ibverbs::Poller::Get().AddPollable(pair);
  grpc_fd_set_arg(em_fd, pair);  // pass pair to grpc_fd

  return &rdma->base;
}

// int grpc_rdma_fd(grpc_endpoint* ep) {
//   grpc_rdma* rdma = reinterpret_cast<grpc_rdma*>(ep);
//   CHECK(ep->vtable == &vtable);
//   return grpc_fd_wrapped_fd(rdma->em_fd);
// }

// void grpc_rdma_destroy_and_release_fd(grpc_endpoint* ep, int* fd,
//                                       grpc_closure* done) {
//   if (grpc_event_engine::experimental::grpc_is_event_engine_endpoint(ep)) {
//     return grpc_event_engine::experimental::
//         grpc_event_engine_endpoint_destroy_and_release_fd(ep, fd, done);
//   }
//   grpc_rdma* rdma = reinterpret_cast<grpc_rdma*>(ep);
//   CHECK(ep->vtable == &vtable);
//   rdma->release_fd = fd;
//   rdma->release_fd_cb = done;
//   grpc_slice_buffer_reset_and_unref(&rdma->last_read_buffer);
//   // if (grpc_event_engine_can_track_errors()) {
//   //   // Stop errors notification.
//   //   ZerocopyDisableAndWaitForRemaining(rdma);
//   //   gpr_atm_no_barrier_store(&rdma->stop_error_notification, true);
//   //   grpc_fd_set_error(rdma->em_fd);
//   // }
//   rdma->read_mu.Lock();
//   rdma->memory_owner.Reset();
//   rdma->read_mu.Unlock();
//   RDMA_UNREF(rdma, "destroy");
// }

// void grpc_rdma_posix_init() { g_backup_poller_mu = new grpc_core::Mutex; }

// void grpc_rdma_posix_shutdown() {
//   delete g_backup_poller_mu;
//   g_backup_poller_mu = nullptr;
// }

#endif  // GRPC_POSIX_SOCKET_TCP
