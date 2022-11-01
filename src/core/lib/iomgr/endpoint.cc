/*
 *
 * Copyright 2015 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <grpc/support/port_platform.h>
#include <map>
#include <mutex>

#include "src/core/lib/iomgr/endpoint.h"
#include "src/core/lib/iomgr/ev_posix.h"
#include "src/core/lib/iomgr/iomgr_internal.h"
#include "src/core/lib/iomgr/rdma_bp_posix.h"
#include "src/core/lib/iomgr/rdma_bpev_posix.h"
#include "src/core/lib/iomgr/rdma_event_posix.h"
#include "src/core/lib/iomgr/tcp_posix.h"

grpc_core::TraceFlag grpc_tcp_trace(false, "tcp");
grpc_core::TraceFlag grpc_rdma_trace(false, "rdma");
std::map<std::string, grpc_endpoint*> peer2endpoint;
std::mutex peer2endpoint_mtx;
std::atomic_size_t global_endpoint_count;

std::string grpc_trim_peer(const std::string peer) {
  std::string ret = peer;
  int n = ret.find("ipv4:");
  if (n != std::string::npos) {
    ret = ret.substr(n + strlen("ipv4:"));
  }
  return ret;
}

grpc_endpoint* grpc_endpoint_create(grpc_fd* fd, const grpc_channel_args* args,
                                    const char* peer_string, bool server) {
  grpc_endpoint* ep = nullptr;
  switch (grpc_check_iomgr_platform()) {
    case IOMGR_RDMA_EVENT:
      ep = grpc_rdma_event_create(fd, args, peer_string, server);
      break;
    case IOMGR_RDMA_BP:
      ep = grpc_rdma_bp_create(fd, args, peer_string, server);
      break;
    case IOMGR_RDMA_BPEV:
      ep = grpc_rdma_bpev_create(fd, args, peer_string, server);
      break;
    case IOMGR_TCP:
      ep = grpc_tcp_create(fd, args, peer_string);
      break;
    default:
      gpr_log(GPR_ERROR, "unknown platform type");
      abort();
  }
  std::string peer = grpc_trim_peer(peer_string);
  {
    std::unique_lock<std::mutex> lck(peer2endpoint_mtx);
    peer2endpoint.insert(std::pair<std::string, grpc_endpoint*>(peer, ep));
  }
  return ep;
}

void grpc_endpoint_read(grpc_endpoint* ep, grpc_slice_buffer* slices,
                        grpc_closure* cb, bool urgent) {
  ep->vtable->read(ep, slices, cb, urgent);
}

void grpc_endpoint_write(grpc_endpoint* ep, grpc_slice_buffer* slices,
                         grpc_closure* cb, void* arg) {
  ep->vtable->write(ep, slices, cb, arg);
}

void grpc_endpoint_add_to_pollset(grpc_endpoint* ep, grpc_pollset* pollset) {
  ep->vtable->add_to_pollset(ep, pollset);
}

void grpc_endpoint_add_to_pollset_set(grpc_endpoint* ep,
                                      grpc_pollset_set* pollset_set) {
  ep->vtable->add_to_pollset_set(ep, pollset_set);
}

void grpc_endpoint_delete_from_pollset_set(grpc_endpoint* ep,
                                           grpc_pollset_set* pollset_set) {
  ep->vtable->delete_from_pollset_set(ep, pollset_set);
}

void grpc_endpoint_shutdown(grpc_endpoint* ep, grpc_error_handle why) {
  ep->vtable->shutdown(ep, why);
}

void grpc_endpoint_destroy(grpc_endpoint* ep) {
  int fd = grpc_endpoint_get_fd(ep);
  std::string peer = grpc_trim_peer((std::string)grpc_endpoint_get_peer(ep));
  std::string local = (std::string)grpc_endpoint_get_local_address(ep);
  {
    std::unique_lock<std::mutex> lck(peer2endpoint_mtx);
    peer2endpoint.erase(peer);
  }
  ep->vtable->destroy(ep);
  //  printf("endpoint %p is destroyed, peer: %s, local: %s, attached fd: %d,
  //  global endpoint count = %d\n",
  //         ep, peer.c_str(), local.c_str(), fd,
  //         global_endpoint_count.fetch_sub(1) - 1);
}

absl::string_view grpc_endpoint_get_peer(grpc_endpoint* ep) {
  return ep->vtable->get_peer(ep);
}

absl::string_view grpc_endpoint_get_local_address(grpc_endpoint* ep) {
  return ep->vtable->get_local_address(ep);
}

int grpc_endpoint_get_fd(grpc_endpoint* ep) { return ep->vtable->get_fd(ep); }

grpc_resource_user* grpc_endpoint_get_resource_user(grpc_endpoint* ep) {
  return ep->vtable->get_resource_user(ep);
}

bool grpc_endpoint_can_track_err(grpc_endpoint* ep) {
  return ep->vtable->can_track_err(ep);
}
