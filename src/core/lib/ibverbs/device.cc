/**
 * Copyright (c) 2017-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */
#ifdef GRPC_USE_IBVERBS

#include "src/core/lib/ibverbs/device.h"

#include <algorithm>
#include <vector>

#include "absl/log/absl_check.h"
#include "absl/log/log.h"

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
  auto& config = ConfigVars::Get();
  auto dev_name = config.RdmaDeviceName();
  IbvDevices devices;

  if (devices.size() == 0) {
    LOG(FATAL) << "Cannot find any ibverbs device";
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
      LOG(FATAL) << "Cannot find device " << dev_name;
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
    LOG(FATAL) << "Cannot open device " << dev_name;
  }
  int rv;

  // Query and store device attributes
  rv = ibv_query_device(context_, &device_attr_);
  if (rv != 0) {
    LOG(FATAL) << "ibv_query_device failed, error " << strerror(errno);
  }

  // Query and store port attributes
  rv = ibv_query_port(context_, config.RdmaPortNum(), &port_attr_);
  if (rv != 0) {
    LOG(FATAL) << "ibv_query_port failed, error " << strerror(errno);
  }

  // Protection domain
  pd_ = ibv_alloc_pd(context_);
  ABSL_CHECK(pd_);
}

ibv_pd* Device::get_pd() const { return pd_; }

Device::~Device() {
  int rv;

  rv = ibv_dealloc_pd(pd_);
  ABSL_CHECK_EQ(rv, 0);

  rv = ibv_close_device(context_);
  ABSL_CHECK_EQ(rv, 0);
}
}  // namespace ibverbs
}  // namespace grpc_core
#endif