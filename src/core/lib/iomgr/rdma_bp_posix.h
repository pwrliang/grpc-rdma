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

#ifndef GRPC_CORE_LIB_IOMGR_RDMA_BP_POSIX_H
#define GRPC_CORE_LIB_IOMGR_RDMA_BP_POSIX_H
/*
   Low level TCP "bottom half" implementation, for use by transports built on
   top of a TCP connection.

   Note that this file does not (yet) include APIs for creating the socket in
   the first place.

   All calls passing slice transfer ownership of a slice refcount unless
   otherwise specified.
*/

#include <grpc/support/port_platform.h>

#include "src/core/lib/iomgr/port.h"

#include "src/core/lib/debug/trace.h"
#include "src/core/lib/iomgr/buffer_list.h"
#include "src/core/lib/iomgr/endpoint.h"
#include "src/core/lib/iomgr/ev_posix.h"

/* Create a rdma endpoint given a file desciptor and a read slice size.
   Takes ownership of fd. */
grpc_endpoint* grpc_rdma_bp_create(grpc_fd* fd, const grpc_channel_args* args,
                                   const char* peer_string, bool server);

/* Return the rdma endpoint's fd, or -1 if this is not available. Does not
   release the fd.
   Requires: ep must be a rdma endpoint.
 */
int grpc_rdma_bp_fd(grpc_endpoint* ep);

/* Destroy the rdma endpoint without closing its fd. *fd will be set and done
 * will be called when the endpoint is destroyed.
 * Requires: ep must be a rdma endpoint and fd must not be NULL. */
void grpc_rdma_bp_destroy_and_release_fd(grpc_endpoint* ep, int* fd,
                                         grpc_closure* done);

void* grpc_rdma_bp_require_zerocopy_sendspace(grpc_endpoint* ep, size_t size);

#endif /* GRPC_CORE_LIB_IOMGR_RDMA_BP_POSIX_H */
