// Copyright 2022 gRPC Authors
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
#include "src/core/lib/event_engine/rdma_engine/rdma_endpoint.h"

#include <errno.h>
#include <inttypes.h>
#include <limits.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string>
#include <type_traits>

#include "absl/functional/any_invocable.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/types/optional.h"

#include <grpc/event_engine/internal/slice_cast.h>
#include <grpc/event_engine/slice.h>
#include <grpc/event_engine/slice_buffer.h>
#include <grpc/status.h>
#include <grpc/support/port_platform.h>

#include "src/core/lib/debug/trace.h"
#include "src/core/lib/event_engine/posix_engine/event_poller.h"
#include "src/core/lib/event_engine/posix_engine/internal_errqueue.h"
#include "src/core/lib/event_engine/posix_engine/tcp_socket_utils.h"
#include "src/core/lib/event_engine/tcp_socket_utils.h"
#include "src/core/lib/experiments/experiments.h"
#include "src/core/lib/gprpp/debug_location.h"
#include "src/core/lib/gprpp/load_file.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/status_helper.h"
#include "src/core/lib/gprpp/strerror.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/ibverbs/pair.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/resource_quota/resource_quota.h"
#include "src/core/lib/slice/slice.h"

#ifdef GRPC_POSIX_SOCKET_TCP
#ifdef GRPC_LINUX_ERRQUEUE
#include <dirent.h>            // IWYU pragma: keep
#include <linux/capability.h>  // IWYU pragma: keep
#include <linux/errqueue.h>    // IWYU pragma: keep
#include <linux/netlink.h>     // IWYU pragma: keep
#include <sys/prctl.h>         // IWYU pragma: keep
#include <sys/resource.h>      // IWYU pragma: keep
#endif
#include <netinet/in.h>  // IWYU pragma: keep
#include <sys/poll.h>

#include "src/core/lib/event_engine/rdma_engine/event_poller.h"
#include "src/core/lib/ibverbs/poller.h"

#define MAX_READ_IOVEC 64

namespace grpc_event_engine {
namespace experimental {

namespace {

absl::Status PosixOSError(int error_no, absl::string_view call_name) {
  return absl::UnknownError(absl::StrCat(
      call_name, ": ", grpc_core::StrError(error_no), " (", error_no, ")"));
}

}  // namespace

#if defined(IOV_MAX) && IOV_MAX < 260
#define MAX_WRITE_IOVEC IOV_MAX
#else
#define MAX_WRITE_IOVEC 260
#endif

void RdmaEndpointImpl::AddToEstimate(size_t bytes) {
  bytes_read_this_round_ += static_cast<double>(bytes);
}

void RdmaEndpointImpl::FinishEstimate() {
  // If we read >80% of the target buffer in one read loop, increase the size of
  // the target buffer to either the amount read, or twice its previous value.
  if (bytes_read_this_round_ > target_length_ * 0.8) {
    target_length_ = std::max(2 * target_length_, bytes_read_this_round_);
  } else {
    target_length_ = 0.99 * target_length_ + 0.01 * bytes_read_this_round_;
  }
  bytes_read_this_round_ = 0;
}

absl::Status RdmaEndpointImpl::RdmaAnnotateError(absl::Status src_error) const {
  grpc_core::StatusSetInt(&src_error, grpc_core::StatusIntProperty::kFd,
                          handle_->WrappedFd());
  grpc_core::StatusSetInt(&src_error, grpc_core::StatusIntProperty::kRpcStatus,
                          GRPC_STATUS_UNAVAILABLE);
  return src_error;
}

// Returns true if data available to read or error other than EAGAIN.
bool RdmaEndpointImpl::RdmaDoRead(absl::Status& status) {
  GRPC_LATENT_SEE_INNER_SCOPE("RdmaDoRead");
  struct iovec iov[MAX_READ_IOVEC];
  ssize_t read_bytes;
  size_t total_read_bytes = 0;
  size_t iov_len = std::min<size_t>(MAX_READ_IOVEC, incoming_buffer_->Count());

  for (size_t i = 0; i < iov_len; i++) {
    MutableSlice& slice =
        internal::SliceCast<MutableSlice>(incoming_buffer_->MutableSliceAt(i));
    iov[i].iov_base = slice.begin();
    iov[i].iov_len = slice.length();
  }

  CHECK_NE(incoming_buffer_->Length(), 0u);
  DCHECK_GT(min_progress_size_, 0);

  do {
    read_bytes = 0;
    if (iov_len > 0) {
      read_bytes = pair_->Recv(iov[0].iov_base, iov[0].iov_len);
    }

    /* We have read something in previous reads. We need to deliver those
     * bytes to the upper layer. */
    if (read_bytes == 0) {
      if (total_read_bytes > 0) {
        break;
      } else {  // read zero bytes, something may go wrong
        auto pair_status = pair_->get_status();
        // active exit
        bool peer_exit =
            pair_status == grpc_core::ibverbs::PairStatus::kHalfClosed;

        // passive exit
        if (!peer_exit) {
          struct pollfd fds[1];

          fds[0].fd = fd_;
          fds[0].events = POLLIN;

          if (poll(fds, 1, 0) > 0) {
            char buffer;

            int bytes_received = recv(fd_, &buffer, 1, 0);
            peer_exit = bytes_received == 0;
          }
        }

        if (peer_exit) {
          incoming_buffer_->Clear();
          status = RdmaAnnotateError(absl::InternalError("Pair closed"));
          return true;
        } else if (pair_status == grpc_core::ibverbs::PairStatus::kError) {
          LOG(ERROR) << "Pair error, Pair " << pair_;
          incoming_buffer_->Clear();
          status = RdmaAnnotateError(absl::InternalError(pair_->get_error()));
          return true;
        }
        // DoRead is invoked but no data to read and connection is still alive
        FinishEstimate();
        return false;
      }
    }

    AddToEstimate(read_bytes);
    DCHECK((size_t)read_bytes <= incoming_buffer_->Length() - total_read_bytes);

    total_read_bytes += read_bytes;
    if (total_read_bytes == incoming_buffer_->Length()) {
      break;
    }

    // We had a partial read, and still have space to read more data. So, adjust
    // IOVs and try to read more.
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

  if (pair_->GetReadableSize() == 0) {
    FinishEstimate();
  }

  DCHECK_GT(total_read_bytes, 0u);
  status = absl::OkStatus();
  if (grpc_core::IsTcpFrameSizeTuningEnabled()) {
    // Update min progress size based on the total number of bytes read in
    // this round.
    min_progress_size_ -= total_read_bytes;
    if (min_progress_size_ > 0) {
      // There is still some bytes left to be read before we can signal
      // the read as complete. Append the bytes read so far into
      // last_read_buffer which serves as a staging buffer. Return false
      // to indicate tcp_handle_read needs to be scheduled again.
      incoming_buffer_->MoveFirstNBytesIntoSliceBuffer(total_read_bytes,
                                                       last_read_buffer_);
      return false;
    } else {
      // The required number of bytes have been read. Append the bytes
      // read in this round into last_read_buffer. Then swap last_read_buffer
      // and incoming_buffer. Now incoming buffer contains all the bytes
      // read since the start of the last tcp_read operation. last_read_buffer
      // would contain any spare space left in the incoming buffer. This
      // space will be used in the next tcp_read operation.
      min_progress_size_ = 1;
      incoming_buffer_->MoveFirstNBytesIntoSliceBuffer(total_read_bytes,
                                                       last_read_buffer_);
      incoming_buffer_->Swap(last_read_buffer_);
      return true;
    }
  }
  if (total_read_bytes < incoming_buffer_->Length()) {
    incoming_buffer_->MoveLastNBytesIntoSliceBuffer(
        incoming_buffer_->Length() - total_read_bytes, last_read_buffer_);
  }
  return true;
}

void RdmaEndpointImpl::PerformReclamation() {
  read_mu_.Lock();
  if (incoming_buffer_ != nullptr) {
    incoming_buffer_->Clear();
  }
  has_posted_reclaimer_ = false;
  read_mu_.Unlock();
}

void RdmaEndpointImpl::MaybePostReclaimer() {
  if (!has_posted_reclaimer_) {
    has_posted_reclaimer_ = true;
    memory_owner_.PostReclaimer(
        grpc_core::ReclamationPass::kBenign,
        [self = Ref(DEBUG_LOCATION, "Posix Reclaimer")](
            absl::optional<grpc_core::ReclamationSweep> sweep) {
          if (sweep.has_value()) {
            self->PerformReclamation();
          }
        });
  }
}

void RdmaEndpointImpl::UpdateRcvLowat() {
  if (!grpc_core::IsTcpRcvLowatEnabled()) return;

  // TODO(ctiller): Check if supported by OS.
  // TODO(ctiller): Allow some adjustments instead of hardcoding things.

  static constexpr int kRcvLowatMax = 16 * 1024 * 1024;
  static constexpr int kRcvLowatThreshold = 16 * 1024;

  int remaining = std::min({static_cast<int>(incoming_buffer_->Length()),
                            kRcvLowatMax, min_progress_size_});

  // Setting SO_RCVLOWAT for small quantities does not save on CPU.
  if (remaining < kRcvLowatThreshold) {
    remaining = 0;
  }

  // If zerocopy is off, wake shortly before the full RPC is here. More can
  // show up partway through recvmsg() since it takes a while to copy data.
  // So an early wakeup aids latency.
  if (remaining > 0) {
    remaining -= kRcvLowatThreshold;
  }

  // We still do not know the RPC size. Do not set SO_RCVLOWAT.
  if (set_rcvlowat_ <= 1 && remaining <= 1) return;

  // Previous value is still valid. No change needed in SO_RCVLOWAT.
  if (set_rcvlowat_ == remaining) {
    return;
  }
  auto result = sock_.SetSocketRcvLowat(remaining);
  if (result.ok()) {
    set_rcvlowat_ = *result;
  } else {
    LOG(ERROR) << "ERROR in SO_RCVLOWAT: " << result.status().message();
  }
}

void RdmaEndpointImpl::MaybeMakeReadSlices() {
  static const int kBigAlloc = 64 * 1024;
  static const int kSmallAlloc = 8 * 1024;
  if (incoming_buffer_->Length() < std::max<size_t>(min_progress_size_, 1)) {
    size_t allocate_length = min_progress_size_;
    const size_t target_length = static_cast<size_t>(target_length_);
    // If memory pressure is low and we think there will be more than
    // min_progress_size bytes to read, allocate a bit more.
    const bool low_memory_pressure =
        memory_owner_.GetPressureInfo().pressure_control_value < 0.8;
    if (low_memory_pressure && target_length > allocate_length) {
      allocate_length = target_length;
    }
    int extra_wanted = std::max<int>(
        1, allocate_length - static_cast<int>(incoming_buffer_->Length()));
    if (extra_wanted >=
        (low_memory_pressure ? kSmallAlloc * 3 / 2 : kBigAlloc)) {
      while (extra_wanted > 0) {
        extra_wanted -= kBigAlloc;
        incoming_buffer_->AppendIndexed(
            Slice(memory_owner_.MakeSlice(kBigAlloc)));
      }
    } else {
      while (extra_wanted > 0) {
        extra_wanted -= kSmallAlloc;
        incoming_buffer_->AppendIndexed(
            Slice(memory_owner_.MakeSlice(kSmallAlloc)));
      }
    }
    MaybePostReclaimer();
  }
}

bool RdmaEndpointImpl::HandleReadLocked(absl::Status& status) {
  if (status.ok() && memory_owner_.is_valid()) {
    MaybeMakeReadSlices();
    if (!RdmaDoRead(status)) {
      UpdateRcvLowat();
      // We've consumed the edge, request a new one.
      return false;
    }
  } else {
    if (!memory_owner_.is_valid() && status.ok()) {
      status = RdmaAnnotateError(absl::UnknownError("Shutting down endpoint"));
    }
    incoming_buffer_->Clear();
    last_read_buffer_.Clear();
  }
  return true;
}

void RdmaEndpointImpl::HandleRead(absl::Status status) {
  bool ret = false;
  absl::AnyInvocable<void(absl::Status)> cb = nullptr;
  grpc_core::EnsureRunInExecCtx([&, this]() mutable {
    grpc_core::MutexLock lock(&read_mu_);
    ret = HandleReadLocked(status);
    if (ret) {
      GRPC_TRACE_LOG(event_engine_endpoint, INFO)
          << "Endpoint[" << this << "]: Read complete";
      cb = std::move(read_cb_);
      read_cb_ = nullptr;
      incoming_buffer_ = nullptr;
    }
  });
  if (!ret) {
    handle_->NotifyOnRead(on_read_);
    return;
  }
  cb(status);
  Unref();
}

bool RdmaEndpointImpl::Read(absl::AnyInvocable<void(absl::Status)> on_read,
                            SliceBuffer* buffer,
                            const EventEngine::Endpoint::ReadArgs* args) {
  grpc_core::ReleasableMutexLock lock(&read_mu_);
  GRPC_TRACE_LOG(event_engine_endpoint, INFO)
      << "Endpoint[" << this << "]: Read";
  CHECK(read_cb_ == nullptr);
  incoming_buffer_ = buffer;
  incoming_buffer_->Clear();
  incoming_buffer_->Swap(last_read_buffer_);
  if (args != nullptr && grpc_core::IsTcpFrameSizeTuningEnabled()) {
    min_progress_size_ = std::max(static_cast<int>(args->read_hint_bytes), 1);
  } else {
    min_progress_size_ = 1;
  }
  Ref().release();
  if (is_first_read_) {
    read_cb_ = std::move(on_read);
    UpdateRcvLowat();
    // Endpoint read called for the very first time. Register read callback
    // with the polling engine.
    is_first_read_ = false;
    lock.Release();
    handle_->NotifyOnRead(on_read_);
  } else if (pair_->GetReadableSize() == 0) {
    read_cb_ = std::move(on_read);
    UpdateRcvLowat();
    lock.Release();
    // Upper layer asked to read more but we know there is no pending data to
    // read from previous reads. So, wait for POLLIN.
    handle_->NotifyOnRead(on_read_);
  } else {
    absl::Status status;
    MaybeMakeReadSlices();

    if (!RdmaDoRead(status)) {
      UpdateRcvLowat();
      read_cb_ = std::move(on_read);
      // We've consumed the edge, request a new one.
      lock.Release();
      handle_->NotifyOnRead(on_read_);
      return false;
    }
    if (!status.ok()) {
      // Read failed immediately. Schedule the on_read callback to run
      // asynchronously.
      lock.Release();
      engine_->Run([on_read = std::move(on_read), status, this]() mutable {
        GRPC_TRACE_LOG(event_engine_endpoint, INFO)
            << "Endpoint[" << this << "]: Read failed immediately: " << status;
        on_read(status);
      });
      Unref();
      return false;
    }
    // Read succeeded immediately. Return true and don't run the on_read
    // callback.
    incoming_buffer_ = nullptr;
    Unref();
    GRPC_TRACE_LOG(event_engine_endpoint, INFO)
        << "Endpoint[" << this << "]: Read succeeded immediately";
    return true;
  }
  return false;
}

void RdmaEndpointImpl::HandleError(absl::Status /*status*/) {
  grpc_core::Crash("Error handling not supported on this platform");
}

bool RdmaEndpointImpl::RdmaFlush(absl::Status& status) {
  // We always start at zero, because we eagerly unref and trim the slice
  // buffer as we write
  size_t outgoing_slice_idx = 0;
  status = absl::OkStatus();

  ABSL_CHECK_GT(outgoing_buffer_->Count(), 0);

  size_t sent_length =
      pair_->Send(outgoing_buffer_->c_slice_buffer()->slices,
                  outgoing_buffer_->Count(), outgoing_byte_idx_);

  while (sent_length > 0) {
    auto slice_len =
        GRPC_SLICE_LENGTH(
            outgoing_buffer_->c_slice_buffer()->slices[outgoing_slice_idx]) -
        outgoing_byte_idx_;

    if (sent_length >= slice_len) {
      sent_length -= slice_len;
      outgoing_slice_idx++;
      outgoing_byte_idx_ = 0;
    } else {
      outgoing_byte_idx_ += sent_length;
      break;
    }
  }

  // Partial send
  if (outgoing_byte_idx_ > 0 ||
      outgoing_slice_idx < outgoing_buffer_->Count()) {
    auto pair_status = pair_->get_status();

    if (pair_status == grpc_core::ibverbs::PairStatus::kConnected) {
      // unref all and forget about all slices that have been written to this
      // point
      for (size_t idx = 0; idx < outgoing_slice_idx; ++idx) {
        outgoing_buffer_->TakeFirst();
      }
      return false;
    } else if (pair_status == grpc_core::ibverbs::PairStatus::kHalfClosed) {
      status = RdmaAnnotateError(GRPC_ERROR_CREATE("Peer has been exited"));
      outgoing_buffer_->Clear();
      return true;
    } else {
      auto err = "RDMA Pair has an internal error, " + pair_->get_error();
      status = RdmaAnnotateError(GRPC_ERROR_CREATE(err));
      outgoing_buffer_->Clear();
      return true;
    }
  } else {
    outgoing_buffer_->Clear();
    return true;
  }
}

void RdmaEndpointImpl::HandleWrite(absl::Status status) {
  if (!status.ok()) {
    GRPC_TRACE_LOG(event_engine_endpoint, INFO)
        << "Endpoint[" << this << "]: Write failed: " << status;
    absl::AnyInvocable<void(absl::Status)> cb_ = std::move(write_cb_);
    write_cb_ = nullptr;
    cb_(status);
    Unref();
    return;
  }
  bool flush_result = RdmaFlush(status);
  if (!flush_result) {
    DCHECK(status.ok());
    handle_->NotifyOnWrite(on_write_);
  } else {
    GRPC_TRACE_LOG(event_engine_endpoint, INFO)
        << "Endpoint[" << this << "]: Write complete: " << status;
    absl::AnyInvocable<void(absl::Status)> cb_ = std::move(write_cb_);
    write_cb_ = nullptr;
    cb_(status);
    Unref();
  }
}

bool RdmaEndpointImpl::Write(absl::AnyInvocable<void(absl::Status)> on_writable,
                             SliceBuffer* data,
                             const EventEngine::Endpoint::WriteArgs* args) {
  absl::Status status = absl::OkStatus();

  CHECK(write_cb_ == nullptr);
  DCHECK_NE(data, nullptr);

  GRPC_TRACE_LOG(event_engine_endpoint, INFO)
      << "Endpoint[" << this << "]: Write " << data->Length() << " bytes";

  if (data->Length() == 0) {
    if (handle_->IsHandleShutdown()) {
      status = RdmaAnnotateError(absl::InternalError("EOF"));
      engine_->Run(
          [on_writable = std::move(on_writable), status, this]() mutable {
            GRPC_TRACE_LOG(event_engine_endpoint, INFO)
                << "Endpoint[" << this << "]: Write failed: " << status;
            on_writable(status);
          });
      return false;
    }
    GRPC_TRACE_LOG(event_engine_endpoint, INFO)
        << "Endpoint[" << this << "]: Write skipped";
    return true;
  }

  //  zerocopy_send_record = TcpGetSendZerocopyRecord(*data);
  //  if (zerocopy_send_record == nullptr) {
  // Either not enough bytes, or couldn't allocate a zerocopy context.
  outgoing_buffer_ = data;
  outgoing_byte_idx_ = 0;
  //  }

  bool flush_result = RdmaFlush(status);
  if (!flush_result) {
    Ref().release();
    write_cb_ = std::move(on_writable);
    handle_->NotifyOnWrite(on_write_);
    return false;
  }
  if (!status.ok()) {
    // Write failed immediately. Schedule the on_writable callback to run
    // asynchronously.
    engine_->Run(
        [on_writable = std::move(on_writable), status, this]() mutable {
          GRPC_TRACE_LOG(event_engine_endpoint, INFO)
              << "Endpoint[" << this << "]: Write failed: " << status;
          on_writable(status);
        });
    return false;
  }
  // Write succeeded immediately. Return true and don't run the on_writable
  // callback.
  GRPC_TRACE_LOG(event_engine_endpoint, INFO)
      << "Endpoint[" << this << "]: Write succeded immediately";
  return true;
}

void RdmaEndpointImpl::MaybeShutdown(
    absl::Status why,
    absl::AnyInvocable<void(absl::StatusOr<int>)> on_release_fd) {
  on_release_fd_ = std::move(on_release_fd);
  grpc_core::StatusSetInt(&why, grpc_core::StatusIntProperty::kRpcStatus,
                          GRPC_STATUS_UNAVAILABLE);
  handle_->ShutdownHandle(why);
  read_mu_.Lock();
  memory_owner_.Reset();
  read_mu_.Unlock();
  Unref();
}

RdmaEndpointImpl ::~RdmaEndpointImpl() {
  int release_fd = -1;
  handle_->OrphanHandle(on_done_,
                        on_release_fd_ == nullptr ? nullptr : &release_fd, "");

  if (on_release_fd_ != nullptr) {
    engine_->Run([on_release_fd = std::move(on_release_fd_),
                  release_fd]() mutable { on_release_fd(release_fd); });
  }
  delete on_read_;
  delete on_write_;
  delete on_error_;
}

RdmaEndpointImpl::RdmaEndpointImpl(RdmaEventHandle* handle,
                                   PosixEngineClosure* on_done,
                                   std::shared_ptr<EventEngine> engine,
                                   MemoryAllocator&& /*allocator*/,
                                   const PosixTcpOptions& options)
    : pair_(handle->GetPair()),
      sock_(PosixSocketWrapper(handle->WrappedFd())),
      on_done_(on_done),
      traced_buffers_(),
      handle_(handle),
      poller_(handle->Poller()),
      engine_(engine) {
  PosixSocketWrapper sock(handle->WrappedFd());
  fd_ = handle_->WrappedFd();
  CHECK(options.resource_quota != nullptr);
  auto peer_addr_string = sock.PeerAddressString();
  mem_quota_ = options.resource_quota->memory_quota();
  memory_owner_ = mem_quota_->CreateMemoryOwner();
  self_reservation_ = memory_owner_.MakeReservation(sizeof(RdmaEndpointImpl));
  auto local_address = sock.LocalAddress();
  if (local_address.ok()) {
    local_address_ = *local_address;
  }
  auto peer_address = sock.PeerAddress();
  if (peer_address.ok()) {
    peer_address_ = *peer_address;
  }
  target_length_ = static_cast<double>(options.tcp_read_chunk_size);
  bytes_read_this_round_ = 0;

  on_read_ = PosixEngineClosure::ToPermanentClosure(
      [this](absl::Status status) { HandleRead(std::move(status)); });
  on_write_ = PosixEngineClosure::ToPermanentClosure(
      [this](absl::Status status) { HandleWrite(std::move(status)); });
  on_error_ = PosixEngineClosure::ToPermanentClosure(
      [this](absl::Status status) { HandleError(std::move(status)); });

  // Start being notified on errors if poller can track errors.
  if (poller_->CanTrackErrors()) {
    Ref().release();
    handle_->NotifyOnError(on_error_);
  }
}

std::unique_ptr<RdmaEndpoint> CreateRdmaEndpoint(
    RdmaEventHandle* handle, PosixEngineClosure* on_shutdown,
    std::shared_ptr<EventEngine> engine, MemoryAllocator&& allocator,
    const PosixTcpOptions& options) {
  DCHECK_NE(handle, nullptr);
  LOG(INFO) << "Create rdma endpoint";
  return std::make_unique<RdmaEndpoint>(handle, on_shutdown, std::move(engine),
                                        std::move(allocator), options);
}

}  // namespace experimental
}  // namespace grpc_event_engine

#else  // GRPC_POSIX_SOCKET_TCP

namespace grpc_event_engine {
namespace experimental {

std::unique_ptr<RdmaEndpoint> CreateRdmaEndpoint(
    EventHandle* /*handle*/, PosixEngineClosure* /*on_shutdown*/,
    std::shared_ptr<EventEngine> /*engine*/,
    const PosixTcpOptions& /*options*/) {
  grpc_core::Crash("Cannot create RdmaEndpoint on this platform");
}

}  // namespace experimental
}  // namespace grpc_event_engine

#endif  // GRPC_POSIX_SOCKET_TCP
