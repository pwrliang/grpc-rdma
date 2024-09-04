// Copyright 2022 The gRPC Authors
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
#include "src/core/lib/event_engine/rdma_engine/ev_bpev_rdma_linux.h"

#include <stdint.h>
#include <sys/poll.h>

#include <atomic>
#include <memory>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/status.h>
#include <grpc/support/port_platform.h>
#include <grpc/support/sync.h>

#include "src/core/lib/event_engine/poller.h"
#include "src/core/lib/event_engine/time_util.h"
#include "src/core/lib/gprpp/crash.h"
#include "src/core/lib/iomgr/port.h"

// This polling engine is only relevant on linux kernels supporting epoll
// epoll_create() or epoll_create1()
#ifdef GRPC_LINUX_EPOLL
#include <errno.h>
#include <limits.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include "src/core/lib/event_engine/posix_engine/event_poller.h"
#include "src/core/lib/event_engine/posix_engine/lockfree_event.h"
#include "src/core/lib/event_engine/posix_engine/posix_engine_closure.h"
#include "src/core/lib/event_engine/posix_engine/tcp_socket_utils.h"
#include "src/core/lib/event_engine/posix_engine/wakeup_fd_posix.h"
#include "src/core/lib/event_engine/posix_engine/wakeup_fd_posix_default.h"
#include "src/core/lib/gprpp/fork.h"
#include "src/core/lib/gprpp/status_helper.h"
#include "src/core/lib/gprpp/strerror.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/ibverbs/pair.h"
#include "src/core/lib/ibverbs/poller.h"

#define MAX_EPOLL_EVENTS_HANDLED_PER_ITERATION 1

namespace grpc_event_engine {
namespace experimental {

int ExchangeData(int fd, const char* buf_in, char* buf_out, const size_t sz) {
  size_t bytes_send = 0, bytes_recv = 0;
  if (fd < 3) {
    LOG(ERROR) << "failed to sync data with remote, no opened socket " << fd;
    return EINVAL;
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
            return err;
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
            return err;
          }
        } else {
          bytes_recv += n;
        }
      }
    }
  }

  return 0;
}

class BpevEventHandle : public RdmaEventHandle {
 public:
  BpevEventHandle(int fd, BpevPoller* poller)
      : fd_(fd),
        list_(this),
        poller_(poller),
        read_closure_(std::make_unique<LockfreeEvent>(poller->GetScheduler())),
        write_closure_(std::make_unique<LockfreeEvent>(poller->GetScheduler())),
        error_closure_(
            std::make_unique<LockfreeEvent>(poller->GetScheduler())) {
    read_closure_->InitEvent();
    write_closure_->InitEvent();
    error_closure_->InitEvent();
    pending_read_.store(false, std::memory_order_relaxed);
    pending_write_.store(false, std::memory_order_relaxed);
    pending_error_.store(false, std::memory_order_relaxed);

    std::stringstream ss;
    ss << this;
    // pair must be ready for the wakeup fd
    pair_ = grpc_core::ibverbs::PairPool::Get().Take(ss.str());
  }

  void ReInit(int fd) {
    fd_ = fd;
    read_closure_->InitEvent();
    write_closure_->InitEvent();
    error_closure_->InitEvent();
    pending_read_.store(false, std::memory_order_relaxed);
    pending_write_.store(false, std::memory_order_relaxed);
    pending_error_.store(false, std::memory_order_relaxed);

    std::stringstream ss;
    ss << this;

    pair_ = grpc_core::ibverbs::PairPool::Get().Take(ss.str());
  }

  void InitializePair(absl::Status& status) override {
    status = absl::OkStatus();
    LOG(INFO) << "InitializePair " << pair_;
    pair_->Init();

    std::vector<char> addr_bytes = pair_->get_self_address().bytes();
    std::vector<char> peer_addr_bytes(addr_bytes.size());

    int err = ExchangeData(fd_, addr_bytes.data(), peer_addr_bytes.data(),
                           addr_bytes.size());

    if (err == 0) {
      if (!pair_->Connect(peer_addr_bytes)) {
        LOG(ERROR) << "Making RDMA connection failed, error "
                   << pair_->get_error();
        pair_->Disconnect();  // Cleanup
        grpc_core::ibverbs::PairPool::Get().Putback(pair_);
        status = absl::FailedPreconditionError(
            "Making RDMA connection failed, error " + pair_->get_error());
        pair_ = nullptr;
        return;
      }
    } else {
      LOG(ERROR) << "Exchanging RDMA QP failed, error "
                 << grpc_core::StrError(err);
      grpc_core::ibverbs::PairPool::Get().Putback(pair_);
      status = absl::FailedPreconditionError(std::strerror(err));
      pair_ = nullptr;
      return;
    }
    // the Poller uses background to poll RDMA buffers
    grpc_core::ibverbs::Poller::Get().AddPollable(pair_);
  }

  grpc_core::ibverbs::PairPollable* GetPair() override { return pair_; }

  BpevPoller* Poller() override { return poller_; }
  bool SetPendingActions(bool pending_read, bool pending_write,
                         bool pending_error) {
    // Another thread may be executing ExecutePendingActions() at this point
    // This is possible for instance, if one instantiation of Work(..) sets
    // an fd to be readable while the next instantiation of Work(...) may
    // set the fd to be writable. While the second instantiation is running,
    // ExecutePendingActions() of the first instantiation may execute in
    // parallel and read the pending_<***>_ variables. So we need to use
    // atomics to manipulate pending_<***>_ variables.

    if (pending_read) {
      pending_read_.store(true, std::memory_order_release);
    }

    if (pending_write) {
      pending_write_.store(true, std::memory_order_release);
    }

    if (pending_error) {
      pending_error_.store(true, std::memory_order_release);
    }

    return pending_read || pending_write || pending_error;
  }
  int WrappedFd() override { return fd_; }
  void OrphanHandle(PosixEngineClosure* on_done, int* release_fd,
                    absl::string_view reason) override;
  void ShutdownHandle(absl::Status why) override;
  void NotifyOnRead(PosixEngineClosure* on_read) override;
  void NotifyOnWrite(PosixEngineClosure* on_write) override;
  void NotifyOnError(PosixEngineClosure* on_error) override;
  void SetReadable() override;
  void SetWritable() override;
  void SetHasError() override;
  bool IsHandleShutdown() override;
  inline void ExecutePendingActions() {
    // These may execute in Parallel with ShutdownHandle. Thats not an issue
    // because the lockfree event implementation should be able to handle it.
    if (pending_read_.exchange(false, std::memory_order_acq_rel)) {
      read_closure_->SetReady();
    }
    if (pending_write_.exchange(false, std::memory_order_acq_rel)) {
      write_closure_->SetReady();
    }
    if (pending_error_.exchange(false, std::memory_order_acq_rel)) {
      error_closure_->SetReady();
    }
  }
  grpc_core::Mutex* mu() { return &mu_; }
  LockfreeEvent* ReadClosure() { return read_closure_.get(); }
  LockfreeEvent* WriteClosure() { return write_closure_.get(); }
  LockfreeEvent* ErrorClosure() { return error_closure_.get(); }
  BpevPoller::HandlesList& ForkFdListPos() { return list_; }

  ~BpevEventHandle() override = default;

 private:
  void HandleShutdownInternal(absl::Status why, bool releasing_fd);
  // See BpevPoller::ShutdownHandle for explanation on why a mutex is
  // required.
  grpc_core::Mutex mu_;
  int fd_;
  grpc_core::ibverbs::PairPollable* pair_;
  // See BpevPoller::SetPendingActions for explanation on why pending_<***>_
  // need to be atomic.
  std::atomic<bool> pending_read_{false};
  std::atomic<bool> pending_write_{false};
  std::atomic<bool> pending_error_{false};
  BpevPoller::HandlesList list_;
  BpevPoller* poller_;
  std::unique_ptr<LockfreeEvent> read_closure_;
  std::unique_ptr<LockfreeEvent> write_closure_;
  std::unique_ptr<LockfreeEvent> error_closure_;
};

namespace {

int EpollCreateAndCloexec() {
#ifdef GRPC_LINUX_EPOLL_CREATE1
  int fd = epoll_create1(EPOLL_CLOEXEC);
  if (fd < 0) {
    LOG(ERROR) << "epoll_create1 unavailable";
  }
#else
  int fd = epoll_create(MAX_EPOLL_EVENTS);
  if (fd < 0) {
    LOG(ERROR) << "epoll_create unavailable";
  } else if (fcntl(fd, F_SETFD, FD_CLOEXEC) != 0) {
    LOG(ERROR) << "fcntl following epoll_create failed";
    return -1;
  }
#endif
  return fd;
}

// Only used when GRPC_ENABLE_FORK_SUPPORT=1
std::list<BpevPoller*> fork_poller_list;

// Only used when GRPC_ENABLE_FORK_SUPPORT=1
BpevEventHandle* fork_fd_list_head = nullptr;
gpr_mu fork_fd_list_mu;

void ForkFdListAddHandle(BpevEventHandle* handle) {
  if (grpc_core::Fork::Enabled()) {
    gpr_mu_lock(&fork_fd_list_mu);
    handle->ForkFdListPos().next = fork_fd_list_head;
    handle->ForkFdListPos().prev = nullptr;
    if (fork_fd_list_head != nullptr) {
      fork_fd_list_head->ForkFdListPos().prev = handle;
    }
    fork_fd_list_head = handle;
    gpr_mu_unlock(&fork_fd_list_mu);
  }
}

void ForkFdListRemoveHandle(BpevEventHandle* handle) {
  if (grpc_core::Fork::Enabled()) {
    gpr_mu_lock(&fork_fd_list_mu);
    if (fork_fd_list_head == handle) {
      fork_fd_list_head = handle->ForkFdListPos().next;
    }
    if (handle->ForkFdListPos().prev != nullptr) {
      handle->ForkFdListPos().prev->ForkFdListPos().next =
          handle->ForkFdListPos().next;
    }
    if (handle->ForkFdListPos().next != nullptr) {
      handle->ForkFdListPos().next->ForkFdListPos().prev =
          handle->ForkFdListPos().prev;
    }
    gpr_mu_unlock(&fork_fd_list_mu);
  }
}

void ForkPollerListAddPoller(BpevPoller* poller) {
  if (grpc_core::Fork::Enabled()) {
    gpr_mu_lock(&fork_fd_list_mu);
    fork_poller_list.push_back(poller);
    gpr_mu_unlock(&fork_fd_list_mu);
  }
}

void ForkPollerListRemovePoller(BpevPoller* poller) {
  if (grpc_core::Fork::Enabled()) {
    gpr_mu_lock(&fork_fd_list_mu);
    fork_poller_list.remove(poller);
    gpr_mu_unlock(&fork_fd_list_mu);
  }
}

bool InitBpevPollerLinux();

// Called by the child process's post-fork handler to close open fds,
// including the global epoll fd of each poller. This allows gRPC to shutdown in
// the child process without interfering with connections or RPCs ongoing in the
// parent.
void ResetEventManagerOnFork() {
  // Delete all pending BpevEventHandles.
  gpr_mu_lock(&fork_fd_list_mu);
  while (fork_fd_list_head != nullptr) {
    close(fork_fd_list_head->WrappedFd());
    BpevEventHandle* next = fork_fd_list_head->ForkFdListPos().next;
    delete fork_fd_list_head;
    fork_fd_list_head = next;
  }
  // Delete all registered pollers. This also closes all open epoll_sets
  while (!fork_poller_list.empty()) {
    BpevPoller* poller = fork_poller_list.front();
    fork_poller_list.pop_front();
    poller->Close();
  }
  gpr_mu_unlock(&fork_fd_list_mu);
  InitBpevPollerLinux();
}

// It is possible that GLIBC has epoll but the underlying kernel doesn't.
// Create epoll_fd to make sure epoll support is available
bool InitBpevPollerLinux() {
  if (!grpc_event_engine::experimental::SupportsWakeupFd()) {
    return false;
  }
  int fd = EpollCreateAndCloexec();
  if (fd <= 0) {
    return false;
  }
  if (grpc_core::Fork::Enabled()) {
    if (grpc_core::Fork::RegisterResetChildPollingEngineFunc(
            ResetEventManagerOnFork)) {
      gpr_mu_init(&fork_fd_list_mu);
    }
  }
  close(fd);
  return true;
}

}  // namespace

void BpevEventHandle::OrphanHandle(PosixEngineClosure* on_done, int* release_fd,
                                   absl::string_view reason) {
  bool is_release_fd = (release_fd != nullptr);
  bool was_shutdown = false;
  if (!read_closure_->IsShutdown()) {
    was_shutdown = true;
    HandleShutdownInternal(absl::Status(absl::StatusCode::kUnknown, reason),
                           is_release_fd);
  }

  // If release_fd is not NULL, we should be relinquishing control of the file
  // descriptor fd->fd (but we still own the grpc_fd structure).
  if (is_release_fd) {
    if (!was_shutdown) {
      epoll_event phony_event;
      if (epoll_ctl(poller_->g_epoll_set_.epfd, EPOLL_CTL_DEL, fd_,
                    &phony_event) != 0) {
        LOG(ERROR) << "OrphanHandle: epoll_ctl failed: "
                   << grpc_core::StrError(errno);
      }
    }
    *release_fd = fd_;
  } else {
    shutdown(fd_, SHUT_RDWR);
    close(fd_);
  }

  ForkFdListRemoveHandle(this);
  {
    // See BpevPoller::ShutdownHandle for explanation on why a mutex is
    // required here.
    grpc_core::MutexLock lock(&mu_);
    read_closure_->DestroyEvent();
    write_closure_->DestroyEvent();
    error_closure_->DestroyEvent();
  }
  pending_read_.store(false, std::memory_order_release);
  pending_write_.store(false, std::memory_order_release);
  pending_error_.store(false, std::memory_order_release);
  {
    grpc_core::MutexLock lock(&poller_->mu_);
    poller_->free_epoll1_handles_list_.push_back(this);
  }
  if (on_done != nullptr) {
    on_done->SetStatus(absl::OkStatus());
    poller_->GetScheduler()->Run(on_done);
  }

  // put back pair and delete wakeup fd from epfd
  epoll_event phony_event;
  if (epoll_ctl(poller_->g_epoll_set_.epfd, EPOLL_CTL_DEL,
                pair_->get_wakeup_fd()->read_fd, &phony_event) != 0) {
    LOG(ERROR) << "OrphanHandle: epoll_ctl failed: "
               << grpc_core::StrError(errno);
  }
  {
    poller_->g_epoll_set_.fd_mu.WriterLock();
    auto it = std::find(poller_->g_epoll_set_.fds.begin(),
                        poller_->g_epoll_set_.fds.end(), this);
    if (it != poller_->g_epoll_set_.fds.end()) {
      poller_->g_epoll_set_.fds.erase(it);
    }
    poller_->g_epoll_set_.fd_mu.WriterUnlock();
  }

  pair_->Disconnect();
  grpc_core::ibverbs::Poller::Get().RemovePollable(pair_);
  grpc_core::ibverbs::PairPool::Get().Putback(pair_);
  pair_ = nullptr;
}

// if 'releasing_fd' is true, it means that we are going to detach the internal
// fd from grpc_fd structure (i.e which means we should not be calling
// shutdown() syscall on that fd)
void BpevEventHandle::HandleShutdownInternal(absl::Status why,
                                             bool releasing_fd) {
  grpc_core::StatusSetInt(&why, grpc_core::StatusIntProperty::kRpcStatus,
                          GRPC_STATUS_UNAVAILABLE);
  if (read_closure_->SetShutdown(why)) {
    if (releasing_fd) {
      epoll_event phony_event;
      if (epoll_ctl(poller_->g_epoll_set_.epfd, EPOLL_CTL_DEL, fd_,
                    &phony_event) != 0) {
        LOG(ERROR) << "HandleShutdownInternal: epoll_ctl failed: "
                   << grpc_core::StrError(errno);
      }
    }
    write_closure_->SetShutdown(why);
    error_closure_->SetShutdown(why);
  }
}

BpevPoller::BpevPoller(Scheduler* scheduler)
    : scheduler_(scheduler), was_kicked_(false), closed_(false) {
  g_epoll_set_.epfd = EpollCreateAndCloexec();
  wakeup_fd_ = *CreateWakeupFd();
  CHECK(wakeup_fd_ != nullptr);
  CHECK_GE(g_epoll_set_.epfd, 0);
  GRPC_TRACE_LOG(event_engine_poller, INFO)
      << "grpc epoll fd: " << g_epoll_set_.epfd;
  struct epoll_event ev{};
  ev.events = static_cast<uint32_t>(EPOLLIN | EPOLLET);
  ev.data.ptr = wakeup_fd_.get();
  CHECK(epoll_ctl(g_epoll_set_.epfd, EPOLL_CTL_ADD, wakeup_fd_->ReadFd(),
                  &ev) == 0);
  g_epoll_set_.num_events = 0;
  g_epoll_set_.cursor = 0;
  ForkPollerListAddPoller(this);
  polling_timeout_us_ = grpc_core::ConfigVars::Get().RdmaBusyPollingTimeoutUs();
}

void BpevPoller::Shutdown() { ForkPollerListRemovePoller(this); }

void BpevPoller::Close() {
  grpc_core::MutexLock lock(&mu_);
  if (closed_) return;

  if (g_epoll_set_.epfd >= 0) {
    close(g_epoll_set_.epfd);
    g_epoll_set_.epfd = -1;
  }

  while (!free_epoll1_handles_list_.empty()) {
    BpevEventHandle* handle =
        reinterpret_cast<BpevEventHandle*>(free_epoll1_handles_list_.front());
    free_epoll1_handles_list_.pop_front();
    delete handle;
  }
  closed_ = true;
}

BpevPoller::~BpevPoller() { Close(); }

EventHandle* BpevPoller::CreateHandle(int fd, absl::string_view /*name*/,
                                      bool track_err) {
  // We still relay on TCP fd to exchange pair data and monitor connection
  BpevEventHandle* new_handle = nullptr;
  {
    grpc_core::MutexLock lock(&mu_);
    if (free_epoll1_handles_list_.empty()) {
      new_handle = new BpevEventHandle(fd, this);
    } else {
      new_handle =
          reinterpret_cast<BpevEventHandle*>(free_epoll1_handles_list_.front());
      free_epoll1_handles_list_.pop_front();
      new_handle->ReInit(fd);
    }
  }
  ForkFdListAddHandle(new_handle);
  struct epoll_event ev;
  ev.events = static_cast<uint32_t>(EPOLLIN | EPOLLOUT | EPOLLET);
  // Use the least significant bit of ev.data.ptr to store track_err. We expect
  // the addresses to be word aligned. We need to store track_err to avoid
  // synchronization issues when accessing it after receiving an event.
  // Accessing fd would be a data race there because the fd might have been
  // returned to the free list at that point.
  ev.data.ptr = reinterpret_cast<void*>(reinterpret_cast<intptr_t>(new_handle) |
                                        (track_err ? 1 : 0));
  if (epoll_ctl(g_epoll_set_.epfd, EPOLL_CTL_ADD, fd, &ev) != 0) {
    LOG(ERROR) << "epoll_ctl failed: " << grpc_core::StrError(errno);
  }

  {
    struct epoll_event wakeup_ep_ev;
    wakeup_ep_ev.events = static_cast<uint32_t>(EPOLLIN | EPOLLET);
    wakeup_ep_ev.data.ptr =
        reinterpret_cast<void*>(reinterpret_cast<intptr_t>(new_handle) | 2);
    int wakeup_fd = new_handle->GetPair()->get_wakeup_fd()->read_fd;

    if (epoll_ctl(g_epoll_set_.epfd, EPOLL_CTL_ADD, wakeup_fd, &wakeup_ep_ev) !=
        0) {
      switch (errno) {
        case EEXIST:
          break;
        default: {
          LOG(ERROR) << "Cannot add pair " << GRPC_OS_ERROR(errno, "epoll_ctl");
          abort();
        }
      }
    }
  }

  g_epoll_set_.fd_mu.WriterLock();
  if (std::find(g_epoll_set_.fds.begin(), g_epoll_set_.fds.end(), new_handle) ==
      g_epoll_set_.fds.end()) {
    g_epoll_set_.fds.push_back(new_handle);
  }
  g_epoll_set_.fd_mu.WriterUnlock();

  return new_handle;
}

// Process the epoll events found by DoEpollWait() function.
// - g_epoll_set.cursor points to the index of the first event to be processed
// - This function then processes up-to max_epoll_events_to_handle and
//   updates the g_epoll_set.cursor.
// It returns true, it there was a Kick that forced invocation of this
// function. It also returns the list of closures to run to take action
// on file descriptors that became readable/writable.
bool BpevPoller::ProcessEpollEvents(int max_epoll_events_to_handle,
                                    Events& pending_events) {
  int64_t num_events = g_epoll_set_.num_events;
  int64_t cursor = g_epoll_set_.cursor;
  bool was_kicked = false;
  for (int idx = 0; (idx < max_epoll_events_to_handle) && cursor != num_events;
       idx++) {
    int64_t c = cursor++;
    struct epoll_event* ev = &g_epoll_set_.events[c];
    void* data_ptr = ev->data.ptr;
    if (data_ptr == wakeup_fd_.get()) {
      CHECK(wakeup_fd_->ConsumeWakeup().ok());
      was_kicked = true;
    } else {
      if (reinterpret_cast<intptr_t>(data_ptr) & 2) {  // from epoll_wait
        BpevEventHandle* handle = reinterpret_cast<BpevEventHandle*>(
            reinterpret_cast<intptr_t>(data_ptr) & ~intptr_t{2});
        auto* pair = handle->GetPair();

        // pair maybe null when fd is destroyed
        if (pair != nullptr) {
          auto status = pair->get_status();
          bool read_ev = false;
          bool write_ev = false;

          if (status != grpc_core::ibverbs::PairStatus::kUninitialized &&
              status != grpc_core::ibverbs::PairStatus::kDisconnected) {
            auto why = grpc_wakeup_fd_consume_wakeup(pair->get_wakeup_fd());
            if (!why.ok()) {
              LOG(ERROR) << "Consume wakeup fd error, " << why.message();
            }
            /* If half-closed, trigger read to free resources */
            if (pair->HasMessage() ||
                pair->get_status() ==
                    grpc_core::ibverbs::PairStatus::kHalfClosed) {
              read_ev = true;
            }

            if (pair->HasPendingWrites()) {
              write_ev = true;
            }
          }
          if (handle->SetPendingActions(read_ev, write_ev, false)) {
            pending_events.push_back(handle);
          }
        }
      } else {  // TCP events from epoll_wait, RDMA events from busy polling
        BpevEventHandle* handle = reinterpret_cast<BpevEventHandle*>(
            reinterpret_cast<intptr_t>(data_ptr) & ~intptr_t{1});
        bool track_err = reinterpret_cast<intptr_t>(data_ptr) & intptr_t{1};
        bool cancel = (ev->events & EPOLLHUP) != 0;
        bool error = (ev->events & EPOLLERR) != 0;
        bool read_ev = (ev->events & (EPOLLIN | EPOLLPRI)) != 0;
        bool write_ev = (ev->events & EPOLLOUT) != 0;
        bool err_fallback = error && !track_err;

        if (handle->SetPendingActions(read_ev || cancel || err_fallback,
                                      write_ev || cancel || err_fallback,
                                      error && !err_fallback)) {
          pending_events.push_back(handle);
        }
      }
    }
  }
  g_epoll_set_.cursor = cursor;
  return was_kicked;
}

//  Do epoll_wait and store the events in g_epoll_set.events field. This does
//  not "process" any of the events yet; that is done in ProcessEpollEvents().
//  See ProcessEpollEvents() function for more details. It returns the number
// of events generated by epoll_wait.
int BpevPoller::DoEpollWait(EventEngine::Duration timeout) {
  int r = 0;
  int timeout_ms =
      static_cast<int>(grpc_event_engine::experimental::Milliseconds(timeout));
  int polling_timeout_us = polling_timeout_us_;
  int64_t t_elapsed_us;

  if (timeout_ms > 0) {
    polling_timeout_us = std::min(polling_timeout_us, timeout_ms * 1000);
  }

  // Busy polling before seek to epoll_wait
  if (polling_timeout_us > 0) {
    g_epoll_set_.fd_mu.ReaderLock();
    auto begin_poll_time = absl::Now();

    do {
      for (auto it = g_epoll_set_.fds.begin();
           r < MAX_EPOLL_EVENTS && it != g_epoll_set_.fds.end(); it++) {
        auto* fd = *it;
        auto* pair = fd->GetPair();

        CHECK(pair != nullptr);

        if (pair->get_status() == grpc_core::ibverbs::PairStatus::kConnected) {
          bool readable = pair->HasMessage();
          bool writable = pair->HasPendingWrites();
          uint32_t events = 0;

          if (readable) {
            events |= EPOLLIN;
          }
          if (writable) {
            events |= EPOLLOUT;
          }

          if (events) {
            g_epoll_set_.events[r].events = events;
            // do not "or 2", so it is handled by
            // the TCP fd logic in ProcessEpollEvents
            g_epoll_set_.events[r].data.ptr = reinterpret_cast<void*>(fd);
            r++;
          }
        }
      }
      t_elapsed_us = ToInt64Microseconds((absl::Now() - begin_poll_time));
    } while (r == 0 && t_elapsed_us < polling_timeout_us);
    g_epoll_set_.fd_mu.ReaderUnlock();
  }

  if (timeout_ms > 0) {
    timeout_ms = std::max(0l, timeout_ms - t_elapsed_us / 1000);
  }

  if (r == 0) {
    do {
      r = epoll_wait(g_epoll_set_.epfd, g_epoll_set_.events, MAX_EPOLL_EVENTS,
                     timeout_ms);
    } while (r < 0 && errno == EINTR);

    if (r < 0) {
      grpc_core::Crash(absl::StrFormat(
          "(event_engine) BpevPoller:%p encountered epoll_wait error: %s", this,
          grpc_core::StrError(errno).c_str()));
    }
  }

  g_epoll_set_.num_events = r;
  g_epoll_set_.cursor = 0;

  return r;
}

// Might be called multiple times
void BpevEventHandle::ShutdownHandle(absl::Status why) {
  // A mutex is required here because, the SetShutdown method of the
  // lockfree event may schedule a closure if it is already ready and that
  // closure may call OrphanHandle. Execution of ShutdownHandle and OrphanHandle
  // in parallel is not safe because some of the lockfree event types e.g, read,
  // write, error may-not have called SetShutdown when DestroyEvent gets
  // called in the OrphanHandle method.
  grpc_core::MutexLock lock(&mu_);
  HandleShutdownInternal(why, false);
}

bool BpevEventHandle::IsHandleShutdown() { return read_closure_->IsShutdown(); }

void BpevEventHandle::NotifyOnRead(PosixEngineClosure* on_read) {
  read_closure_->NotifyOn(on_read);
}

void BpevEventHandle::NotifyOnWrite(PosixEngineClosure* on_write) {
  write_closure_->NotifyOn(on_write);
}

void BpevEventHandle::NotifyOnError(PosixEngineClosure* on_error) {
  error_closure_->NotifyOn(on_error);
}

void BpevEventHandle::SetReadable() { read_closure_->SetReady(); }

void BpevEventHandle::SetWritable() { write_closure_->SetReady(); }

void BpevEventHandle::SetHasError() { error_closure_->SetReady(); }

// Polls the registered Fds for events until timeout is reached or there is a
// Kick(). If there is a Kick(), it collects and processes any previously
// un-processed events. If there are no un-processed events, it returns
// Poller::WorkResult::Kicked{}
Poller::WorkResult BpevPoller::Work(
    EventEngine::Duration timeout,
    absl::FunctionRef<void()> schedule_poll_again) {
  Events pending_events;
  bool was_kicked_ext = false;
  if (g_epoll_set_.cursor == g_epoll_set_.num_events) {
    if (DoEpollWait(timeout) == 0) {
      return Poller::WorkResult::kDeadlineExceeded;
    }
  }
  {
    grpc_core::MutexLock lock(&mu_);
    // If was_kicked_ is true, collect all pending events in this iteration.
    if (ProcessEpollEvents(
            was_kicked_ ? INT_MAX : MAX_EPOLL_EVENTS_HANDLED_PER_ITERATION,
            pending_events)) {
      was_kicked_ = false;
      was_kicked_ext = true;
    }
    if (was_kicked_ext) {  // pending_events.empty()
      return Poller::WorkResult::kKicked;
    }
  }
  // Run the provided callback.
  schedule_poll_again();
  // Process all pending events inline.
  for (auto& it : pending_events) {
    it->ExecutePendingActions();
  }
  return was_kicked_ext ? Poller::WorkResult::kKicked : Poller::WorkResult::kOk;
}

void BpevPoller::Kick() {
  grpc_core::MutexLock lock(&mu_);
  if (was_kicked_ || closed_) {
    return;
  }
  was_kicked_ = true;
  CHECK(wakeup_fd_->Wakeup().ok());
}

std::shared_ptr<BpevPoller> MakeBpevPoller(Scheduler* scheduler) {
  static bool kBpevPollerSupported = InitBpevPollerLinux();
  if (kBpevPollerSupported) {
    return std::make_shared<BpevPoller>(scheduler);
  }
  return nullptr;
}

void BpevPoller::PrepareFork() { Kick(); }

// TODO(vigneshbabu): implement
void BpevPoller::PostforkParent() {}

// TODO(vigneshbabu): implement
void BpevPoller::PostforkChild() {}

}  // namespace experimental
}  // namespace grpc_event_engine

#else  // defined(GRPC_LINUX_EPOLL)
#if defined(GRPC_POSIX_SOCKET_EV_EPOLL1)

namespace grpc_event_engine {
namespace experimental {

using ::grpc_event_engine::experimental::EventEngine;
using ::grpc_event_engine::experimental::Poller;

BpevPoller::BpevPoller(Scheduler* /* engine */) {
  grpc_core::Crash("unimplemented");
}

void BpevPoller::Shutdown() { grpc_core::Crash("unimplemented"); }

BpevPoller::~BpevPoller() { grpc_core::Crash("unimplemented"); }

EventHandle* BpevPoller::CreateHandle(int /*fd*/, absl::string_view /*name*/,
                                      bool /*track_err*/) {
  grpc_core::Crash("unimplemented");
}

bool BpevPoller::ProcessEpollEvents(int /*max_epoll_events_to_handle*/,
                                    Events& /*pending_events*/) {
  grpc_core::Crash("unimplemented");
}

int BpevPoller::DoEpollWait(EventEngine::Duration /*timeout*/) {
  grpc_core::Crash("unimplemented");
}

Poller::WorkResult BpevPoller::Work(
    EventEngine::Duration /*timeout*/,
    absl::FunctionRef<void()> /*schedule_poll_again*/) {
  grpc_core::Crash("unimplemented");
}

void BpevPoller::Kick() { grpc_core::Crash("unimplemented"); }

// If GRPC_LINUX_EPOLL is not defined, it means epoll is not available. Return
// nullptr.
std::shared_ptr<BpevPoller> MakeBpevPoller(Scheduler* /*scheduler*/) {
  return nullptr;
}

void BpevPoller::PrepareFork() {}

void BpevPoller::PostforkParent() {}

void BpevPoller::PostforkChild() {}

}  // namespace experimental
}  // namespace grpc_event_engine

#endif  // defined(GRPC_POSIX_SOCKET_EV_EPOLL1)
#endif  // !defined(GRPC_LINUX_EPOLL)
