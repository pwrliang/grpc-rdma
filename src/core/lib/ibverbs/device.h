/**
 * Copyright (c) 2017-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#ifndef GRPC_SRC_CORE_LIB_IBVERBS_DEVICE_H
#define GRPC_SRC_CORE_LIB_IBVERBS_DEVICE_H

#include <infiniband/verbs.h>

#include <atomic>
#include <memory>
#include <string>
#include <thread>

namespace grpc_core {
namespace ibverbs {
struct attr {
  std::string name;
  int port;
  int index;
};

class Device {
 public:
  Device(const struct attr& attr, ibv_context* context);

  ~Device();

 private:
  struct attr attr_;
  const std::string pciBusID_;
  ibv_context* context_;
  ibv_device_attr deviceAttr_;
  ibv_port_attr portAttr_;
  ibv_pd* pd_;
  ibv_comp_channel* comp_channel_;

  void loop();

  std::atomic<bool> done_;
  std::unique_ptr<std::thread> loop_;

  friend class PairPollable;
};

std::shared_ptr<Device> CreateDevice(const struct attr&);

}  // namespace ibverbs

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_LIB_IBVERBS_DEVICE_H
