/*
 *
 * Copyright 2018 gRPC authors.
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

#include <stddef.h>

#include "src/core/lib/iomgr/iomgr_internal.h"
#include "src/core/lib/iomgr/timer.h"
#include "src/core/lib/iomgr/timer_manager.h"

static grpc_iomgr_platform_vtable* iomgr_platform_vtable = nullptr;

platform_t pt;

void grpc_set_iomgr_platform_vtable(grpc_iomgr_platform_vtable* vtable) {
  iomgr_platform_vtable = vtable;
}

platform_t grpc_check_iomgr_platform() { return pt; }

void grpc_determine_iomgr_platform() {
  if (iomgr_platform_vtable == nullptr) {
    char* type;
    type = getenv("GRPC_PLATFORM_TYPE");
    if (type == NULL) {
      pt = IOMGR_TCP;
      gpr_log(GPR_INFO, "Select TCP mode by default");
    } else if (strcmp(type, "RDMA_BP") == 0) {
      pt = IOMGR_RDMA_BP;
      gpr_log(GPR_INFO, "Select RDMA Busy Polling mode");
    } else if (strcmp(type, "RDMA_EVENT") == 0) {
      pt = IOMGR_RDMA_EVENT;
      gpr_log(GPR_INFO, "Select RDMA Event mode");
    } else if (strcmp(type, "RDMA_BPEV") == 0) {
      pt = IOMGR_RDMA_BPEV;
      gpr_log(GPR_INFO, "Select RDMA BPEV mode");
    } else if (strcmp(type, "TCP") == 0) {
      pt = IOMGR_TCP;
      gpr_log(GPR_INFO, "Select TCP mode");
    } else {
      gpr_log(GPR_ERROR, "Unknown grpc platform: %s", type);
      exit(1);
    }
    grpc_set_default_iomgr_platform();
  }
}

void grpc_iomgr_platform_init() { iomgr_platform_vtable->init(); }

void grpc_iomgr_platform_flush() { iomgr_platform_vtable->flush(); }

void grpc_iomgr_platform_shutdown() { iomgr_platform_vtable->shutdown(); }

void grpc_iomgr_platform_shutdown_background_closure() {
  iomgr_platform_vtable->shutdown_background_closure();
}

bool grpc_iomgr_platform_is_any_background_poller_thread() {
  return iomgr_platform_vtable->is_any_background_poller_thread();
}

bool grpc_iomgr_platform_add_closure_to_background_poller(
    grpc_closure* closure, grpc_error_handle error) {
  return iomgr_platform_vtable->add_closure_to_background_poller(closure,
                                                                 error);
}
