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

#include "src/core/lib/event_engine/default_event_engine_factory.h"

#include <memory>

#include <grpc/event_engine/event_engine.h>
#include <grpc/support/port_platform.h>

#include "src/core/lib/config/config_vars.h"

#if defined(GPR_WINDOWS)
#include "src/core/lib/event_engine/windows/windows_engine.h"

namespace grpc_event_engine {
namespace experimental {

std::unique_ptr<EventEngine> DefaultEventEngineFactory() {
  return std::make_unique<WindowsEventEngine>();
}

}  // namespace experimental
}  // namespace grpc_event_engine
#elif defined(GRPC_CFSTREAM)
#include "src/core/lib/event_engine/cf_engine/cf_engine.h"

namespace grpc_event_engine {
namespace experimental {

std::unique_ptr<EventEngine> DefaultEventEngineFactory() {
  return std::make_unique<CFEventEngine>();
}

}  // namespace experimental
}  // namespace grpc_event_engine
#else
#include "src/core/lib/event_engine/posix_engine/posix_engine.h"
#include "src/core/lib/event_engine/rdma_engine/rdma_engine.h"

namespace grpc_event_engine {
namespace experimental {

std::unique_ptr<EventEngine> DefaultEventEngineFactory() {
  auto& config = grpc_core::ConfigVars::Get();
  if (config.EnableRdmaSupport()) {
    return std::make_unique<RdmaEventEngine>();
  } else {
    return std::make_unique<PosixEventEngine>();
  }
}

}  // namespace experimental
}  // namespace grpc_event_engine

#endif
