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

#ifndef GRPC_SRC_CORE_LIB_EVENT_ENGINE_RDMA_ENGINE_RDMA_ENDPOINT_H
#define GRPC_SRC_CORE_LIB_EVENT_ENGINE_RDMA_ENGINE_RDMA_ENDPOINT_H

#include <grpc/support/port_platform.h>

// IWYU pragma: no_include <bits/types/struct_iovec.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <new>
#include <utility>

#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/functional/any_invocable.h"
#include "absl/hash/hash.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/event_engine/memory_allocator.h>
#include <grpc/event_engine/slice_buffer.h>
#include <grpc/support/alloc.h>

#include "src/core/lib/event_engine/extensions/supports_fd.h"
#include "src/core/lib/event_engine/posix.h"
#include "src/core/lib/event_engine/posix_engine/event_poller.h"
#include "src/core/lib/event_engine/posix_engine/posix_engine_closure.h"
#include "src/core/lib/event_engine/posix_engine/tcp_socket_utils.h"
#include "src/core/lib/event_engine/posix_engine/traced_buffer_list.h"
#include "src/core/lib/event_engine/rdma_engine/event_poller.h"
#include "src/core/lib/gprpp/crash.h"
#include "src/core/lib/gprpp/ref_counted.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/ibverbs/pair.h"
#include "src/core/lib/iomgr/port.h"
#include "src/core/lib/resource_quota/memory_quota.h"

#ifdef GRPC_POSIX_SOCKET_TCP

#include <sys/socket.h>  // IWYU pragma: keep
#include <sys/types.h>   // IWYU pragma: keep

#ifdef GRPC_MSG_IOVLEN_TYPE
typedef GRPC_MSG_IOVLEN_TYPE msg_iovlen_type;
#else
typedef size_t msg_iovlen_type;
#endif

#endif  //  GRPC_POSIX_SOCKET_TCP

namespace grpc_event_engine {
namespace experimental {

#ifdef GRPC_POSIX_SOCKET_TCP

class RdmaEndpointImpl : public grpc_core::RefCounted<RdmaEndpointImpl> {
 public:
  RdmaEndpointImpl(
      RdmaEventHandle* handle, PosixEngineClosure* on_done,
      std::shared_ptr<grpc_event_engine::experimental::EventEngine> engine,
      grpc_event_engine::experimental::MemoryAllocator&& allocator,
      const PosixTcpOptions& options);
  ~RdmaEndpointImpl() override;
  bool Read(
      absl::AnyInvocable<void(absl::Status)> on_read,
      grpc_event_engine::experimental::SliceBuffer* buffer,
      const grpc_event_engine::experimental::EventEngine::Endpoint::ReadArgs*
          args);
  bool Write(
      absl::AnyInvocable<void(absl::Status)> on_writable,
      grpc_event_engine::experimental::SliceBuffer* data,
      const grpc_event_engine::experimental::EventEngine::Endpoint::WriteArgs*
          args);
  const grpc_event_engine::experimental::EventEngine::ResolvedAddress&
  GetPeerAddress() const {
    return peer_address_;
  }
  const grpc_event_engine::experimental::EventEngine::ResolvedAddress&
  GetLocalAddress() const {
    return local_address_;
  }

  int GetWrappedFd() { return fd_; }

  bool CanTrackErrors() const { return poller_->CanTrackErrors(); }

  void MaybeShutdown(
      absl::Status why,
      absl::AnyInvocable<void(absl::StatusOr<int> release_fd)> on_release_fd);

 private:
  void UpdateRcvLowat() ABSL_EXCLUSIVE_LOCKS_REQUIRED(read_mu_);
  void HandleWrite(absl::Status status);
  void HandleError(absl::Status status);
  void HandleRead(absl::Status status) ABSL_NO_THREAD_SAFETY_ANALYSIS;
  bool HandleReadLocked(absl::Status& status)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(read_mu_);
  void MaybeMakeReadSlices() ABSL_EXCLUSIVE_LOCKS_REQUIRED(read_mu_);
  bool RdmaDoRead(absl::Status& status) ABSL_EXCLUSIVE_LOCKS_REQUIRED(read_mu_);
  void FinishEstimate();
  void AddToEstimate(size_t bytes);
  void MaybePostReclaimer() ABSL_EXCLUSIVE_LOCKS_REQUIRED(read_mu_);
  void PerformReclamation() ABSL_LOCKS_EXCLUDED(read_mu_);
  bool RdmaFlush(absl::Status& status);
  absl::Status RdmaAnnotateError(absl::Status src_error) const;
  grpc_core::Mutex read_mu_;
  PosixSocketWrapper sock_;
  int fd_;
  grpc_core::ibverbs::PairPollable* pair_;
  bool is_first_read_ = true;
  bool has_posted_reclaimer_ ABSL_GUARDED_BY(read_mu_) = false;
  double target_length_;
  int set_rcvlowat_ = 0;
  double bytes_read_this_round_ = 0;

  // garbage after the last read.
  grpc_event_engine::experimental::SliceBuffer last_read_buffer_;

  grpc_event_engine::experimental::SliceBuffer* incoming_buffer_
      ABSL_GUARDED_BY(read_mu_) = nullptr;

  grpc_event_engine::experimental::SliceBuffer* outgoing_buffer_ = nullptr;
  // byte within outgoing_buffer's slices[0] to write next.
  size_t outgoing_byte_idx_ = 0;

  PosixEngineClosure* on_read_ = nullptr;
  PosixEngineClosure* on_write_ = nullptr;
  PosixEngineClosure* on_error_ = nullptr;
  PosixEngineClosure* on_done_ = nullptr;
  absl::AnyInvocable<void(absl::Status)> read_cb_ ABSL_GUARDED_BY(read_mu_);
  absl::AnyInvocable<void(absl::Status)> write_cb_;

  grpc_event_engine::experimental::EventEngine::ResolvedAddress peer_address_;
  grpc_event_engine::experimental::EventEngine::ResolvedAddress local_address_;

  // Maintain a shared_ptr to mem_quota_ to ensure the underlying basic memory
  // quota is not deleted until the endpoint is destroyed.
  grpc_core::MemoryQuotaRefPtr mem_quota_;
  grpc_core::MemoryOwner memory_owner_;
  grpc_core::MemoryAllocator::Reservation self_reservation_;

  void* outgoing_buffer_arg_ = nullptr;

  absl::AnyInvocable<void(absl::StatusOr<int>)> on_release_fd_ = nullptr;

  // A counter which starts at 0. It is initialized the first time the
  // socket options for collecting timestamps are set, and is incremented
  // with each byte sent.
  int bytes_counter_ = -1;
  // True if timestamping options are set on the socket.
#ifdef GRPC_LINUX_ERRQUEUE
  bool socket_ts_enabled_ = false;
#endif  // GRPC_LINUX_ERRQUEUE
  // Cache whether we can set timestamping options
  bool ts_capable_ = true;
  // Set to 1 if we do not want to be notified on errors anymore.
  std::atomic<bool> stop_error_notification_{false};
  // A hint from upper layers specifying the minimum number of bytes that need
  // to be read to make meaningful progress.
  int min_progress_size_ = 1;
  TracedBufferList traced_buffers_;
  // The handle is owned by the RdmaEndpointImpl object.
  EventHandle* handle_;
  PosixEventPoller* poller_;
  std::shared_ptr<grpc_event_engine::experimental::EventEngine> engine_;
};

class RdmaEndpoint : public PosixEndpointWithFdSupport {
 public:
  RdmaEndpoint(
      RdmaEventHandle* handle, PosixEngineClosure* on_shutdown,
      std::shared_ptr<grpc_event_engine::experimental::EventEngine> engine,
      grpc_event_engine::experimental::MemoryAllocator&& allocator,
      const PosixTcpOptions& options)
      : impl_(new RdmaEndpointImpl(handle, on_shutdown, std::move(engine),
                                   std::move(allocator), options)) {}

  bool Read(
      absl::AnyInvocable<void(absl::Status)> on_read,
      grpc_event_engine::experimental::SliceBuffer* buffer,
      const grpc_event_engine::experimental::EventEngine::Endpoint::ReadArgs*
          args) override {
    return impl_->Read(std::move(on_read), buffer, args);
  }

  bool Write(
      absl::AnyInvocable<void(absl::Status)> on_writable,
      grpc_event_engine::experimental::SliceBuffer* data,
      const grpc_event_engine::experimental::EventEngine::Endpoint::WriteArgs*
          args) override {
    return impl_->Write(std::move(on_writable), data, args);
  }

  const grpc_event_engine::experimental::EventEngine::ResolvedAddress&
  GetPeerAddress() const override {
    return impl_->GetPeerAddress();
  }
  const grpc_event_engine::experimental::EventEngine::ResolvedAddress&
  GetLocalAddress() const override {
    return impl_->GetLocalAddress();
  }

  int GetWrappedFd() override { return impl_->GetWrappedFd(); }

  bool CanTrackErrors() override { return impl_->CanTrackErrors(); }

  void Shutdown(absl::AnyInvocable<void(absl::StatusOr<int> release_fd)>
                    on_release_fd) override {
    if (!shutdown_.exchange(true, std::memory_order_acq_rel)) {
      impl_->MaybeShutdown(absl::FailedPreconditionError("Endpoint closing"),
                           std::move(on_release_fd));
    }
  }

  ~RdmaEndpoint() override {
    if (!shutdown_.exchange(true, std::memory_order_acq_rel)) {
      impl_->MaybeShutdown(absl::FailedPreconditionError("Endpoint closing"),
                           nullptr);
    }
  }

 private:
  RdmaEndpointImpl* impl_;
  std::atomic<bool> shutdown_{false};
};

#else  // GRPC_POSIX_SOCKET_TCP

class RdmaEndpoint : public PosixEndpointWithFdSupport {
 public:
  RdmaEndpoint() = default;

  bool Read(absl::AnyInvocable<void(absl::Status)> /*on_read*/,
            grpc_event_engine::experimental::SliceBuffer* /*buffer*/,
            const grpc_event_engine::experimental::EventEngine::Endpoint::
                ReadArgs* /*args*/) override {
    grpc_core::Crash("RdmaEndpoint::Read not supported on this platform");
  }

  bool Write(absl::AnyInvocable<void(absl::Status)> /*on_writable*/,
             grpc_event_engine::experimental::SliceBuffer* /*data*/,
             const grpc_event_engine::experimental::EventEngine::Endpoint::
                 WriteArgs* /*args*/) override {
    grpc_core::Crash("RdmaEndpoint::Write not supported on this platform");
  }

  const grpc_event_engine::experimental::EventEngine::ResolvedAddress&
  GetPeerAddress() const override {
    grpc_core::Crash(
        "RdmaEndpoint::GetPeerAddress not supported on this platform");
  }
  const grpc_event_engine::experimental::EventEngine::ResolvedAddress&
  GetLocalAddress() const override {
    grpc_core::Crash(
        "RdmaEndpoint::GetLocalAddress not supported on this platform");
  }

  int GetWrappedFd() override {
    grpc_core::Crash(
        "RdmaEndpoint::GetWrappedFd not supported on this platform");
  }

  bool CanTrackErrors() override {
    grpc_core::Crash(
        "RdmaEndpoint::CanTrackErrors not supported on this platform");
  }

  void Shutdown(absl::AnyInvocable<void(absl::StatusOr<int> release_fd)>
                    on_release_fd) override {
    grpc_core::Crash("RdmaEndpoint::Shutdown not supported on this platform");
  }

  ~RdmaEndpoint() override = default;
};

#endif  // GRPC_POSIX_SOCKET_TCP

// Create a RdmaEndpoint.
// A shared_ptr of the EventEngine is passed to the endpoint to ensure that
// the EventEngine is alive for the lifetime of the endpoint. The ownership
// of the EventHandle is transferred to the endpoint.
std::unique_ptr<RdmaEndpoint> CreateRdmaEndpoint(
    RdmaEventHandle* handle, PosixEngineClosure* on_shutdown,
    std::shared_ptr<EventEngine> engine,
    grpc_event_engine::experimental::MemoryAllocator&& allocator,
    const PosixTcpOptions& options);

}  // namespace experimental
}  // namespace grpc_event_engine

#endif  // GRPC_SRC_CORE_LIB_EVENT_ENGINE_RDMA_ENGINE_RDMA_ENDPOINT_H
