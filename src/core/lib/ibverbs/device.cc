/**
 * Copyright (c) 2017-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */
#ifdef GRPC_USE_IBVERBS

#include <algorithm>
#include <vector>

#include <grpc/support/log.h>

#include "src/core/lib/ibverbs/device.h"

namespace grpc_core {
namespace ibverbs {

// Scope guard for ibverbs device list.
class IbvDevices {
 public:
  IbvDevices() {
    list_ = ibv_get_device_list(&size_);
    if (list_ == nullptr) {
      size_ = 0;
    }
  }

  ~IbvDevices() {
    if (list_ != nullptr) {
      ibv_free_device_list(list_);
    }
  }

  int size() { return size_; }

  struct ibv_device*& operator[](int i) { return list_[i]; }

 protected:
  int size_;
  struct ibv_device** list_;
};

Device::Device() {
  auto& config = Config::Get();
  auto dev_name = config.get_device_name();
  IbvDevices devices;

  if (devices.size() == 0) {
    gpr_log(GPR_ERROR, "No ibverbs devices present");
    abort();
  }
  std::vector<std::string> names;
  for (auto i = 0; i < devices.size(); i++) {
    names.push_back(devices[i]->name);
  }

  // dev is unspecific, use the first one
  if (dev_name.empty()) {
    std::sort(names.begin(), names.end());
    dev_name = names[0];
  } else {
    auto it = std::find(names.begin(), names.end(), dev_name);

    if (it == names.end()) {
      gpr_log(GPR_ERROR, "Cannot find device %s", dev_name.c_str());
      abort();
    }
  }

  // Look for specified device name
  for (int i = 0; i < devices.size(); i++) {
    if (dev_name == devices[i]->name) {
      context_ = ibv_open_device(devices[i]);
      break;
    }
  }
  if (!context_) {
    gpr_log(GPR_ERROR, "Unable to find device named: %s", dev_name.c_str());
  }
  int rv;

  // Query and store device attributes
  rv = ibv_query_device(context_, &device_attr_);
  if (rv != 0) {
    gpr_log(GPR_ERROR, "ibv_query_device: %s", strerror(errno));
  }
  GPR_ASSERT(rv == 0);

  // Query and store port attributes
  rv = ibv_query_port(context_, config.get_port_num(), &port_attr_);
  if (rv != 0) {
    gpr_log(GPR_ERROR, "ibv_query_port: %s", strerror(errno));
  }
  GPR_ASSERT(rv == 0);

  // Protection domain
  pd_ = ibv_alloc_pd(context_);
  GPR_ASSERT(pd_);
}

ibv_pd* Device::get_pd() const { return pd_; }

Device::~Device() {
  int rv;

  rv = ibv_dealloc_pd(pd_);
  GPR_ASSERT(rv == 0);

  rv = ibv_close_device(context_);
  GPR_ASSERT(rv == 0);
}
}  // namespace ibverbs
}  // namespace grpc_core
#endif