/**
 * Copyright (c) 2017-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#ifndef GRPC_SRC_CORE_LIB_IBVERBS_DEVICE_H
#define GRPC_SRC_CORE_LIB_IBVERBS_DEVICE_H
#ifdef GRPC_USE_IBVERBS
#include <infiniband/verbs.h>

#include <atomic>
#include <memory>
#include <string>
#include <thread>

#include "src/core/lib/ibverbs/config.h"

namespace grpc_core {
namespace ibverbs {
class Device {
 public:
  Device();

  Device(const Config&) = delete;

  Device& operator=(const Device&) = delete;

  ~Device();

  static std::shared_ptr<Device> Get() {
    static auto device = std::make_shared<Device>();

    return device;
  }

  ibv_pd* get_pd() const;

 private:
  ibv_context* context_;
  ibv_device_attr device_attr_;
  ibv_port_attr port_attr_;
  ibv_pd* pd_;

  friend class PairPollable;
};
}  // namespace ibverbs

}  // namespace grpc_core
#endif
#endif  // GRPC_SRC_CORE_LIB_IBVERBS_DEVICE_H
