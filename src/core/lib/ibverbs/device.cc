/**
 * Copyright (c) 2017-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */
#include "device.h"

#include <fcntl.h>
#include <poll.h>

#include <algorithm>
#include <vector>

#include <grpc/support/log.h>

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

std::vector<std::string> getDeviceNames() {
  IbvDevices devices;
  std::vector<std::string> deviceNames;
  for (auto i = 0; i < devices.size(); ++i) {
    deviceNames.push_back(devices[i]->name);
  }
  std::sort(deviceNames.begin(), deviceNames.end());
  return deviceNames;
}

Device::Device(const struct attr& attr, ibv_context* context)
    : attr_(attr), context_(context) {
  int rv;

  // Query and store device attributes
  rv = ibv_query_device(context_, &deviceAttr_);
  if (rv != 0) {
    gpr_log(GPR_ERROR, "ibv_query_device: %s", strerror(errno));
  }
  GPR_ASSERT(rv == 0);

  // Query and store port attributes
  rv = ibv_query_port(context_, attr_.port, &portAttr_);
  if (rv != 0) {
    gpr_log(GPR_ERROR, "ibv_query_port: %s", strerror(errno));
  }
  GPR_ASSERT(rv == 0);

  // Protection domain
  pd_ = ibv_alloc_pd(context_);
  GPR_ASSERT(pd_);

  // Completion channel
  comp_channel_ = ibv_create_comp_channel(context_);
  GPR_ASSERT(comp_channel_);

  // Start thread to poll completion queue and dispatch
  // completions for completed work requests.
  done_ = false;
  // loop_.reset(new std::thread(&Device::loop, this));
}

Device::~Device() {
  int rv;

  done_ = true;
  // loop_->join();

  rv = ibv_destroy_comp_channel(comp_channel_);
  GPR_ASSERT(rv == 0);

  rv = ibv_dealloc_pd(pd_);
  GPR_ASSERT(rv == 0);

  rv = ibv_close_device(context_);
  GPR_ASSERT(rv == 0);
}

void Device::loop() {
  int rv;

  auto flags = fcntl(comp_channel_->fd, F_GETFL);
  GPR_ASSERT(flags != -1);

  rv = fcntl(comp_channel_->fd, F_SETFL, flags | O_NONBLOCK);
  GPR_ASSERT(rv != -1);

  struct pollfd pfd;
  pfd.fd = comp_channel_->fd;
  pfd.events = POLLIN;
  pfd.revents = 0;

  while (!done_) {
    do {
      rv = poll(&pfd, 1, 10);
    } while ((rv == 0 && !done_) || (rv == -1 && errno == EINTR));
    GPR_ASSERT(rv != -1);

    if (done_ && rv == 0) {
      break;
    }

    struct ibv_cq* cq;
    void* cqContext;
    rv = ibv_get_cq_event(comp_channel_, &cq, &cqContext);
    GPR_ASSERT(rv == 0);

    // Completion queue context is a Pair*.
    // Delegate handling of this event to the pair itself.
    //    Pair* pair = static_cast<Pair*>(cqContext);
    //    pair->handleCompletionEvent();
  }
}

std::shared_ptr<Device> CreateDevice(const struct attr& constAttr) {
  struct attr attr = constAttr;
  IbvDevices devices;

  // Default to using the first device if not specified
  if (attr.name.empty()) {
    if (devices.size() == 0) {
      gpr_log(GPR_ERROR, "No ibverbs devices present");
    }
    std::vector<std::string> names;
    for (auto i = 0; i < devices.size(); i++) {
      names.push_back(devices[i]->name);
    }
    std::sort(names.begin(), names.end());
    attr.name = names[0];
  }

  // Look for specified device name
  ibv_context* context = nullptr;
  for (int i = 0; i < devices.size(); i++) {
    if (attr.name == devices[i]->name) {
      context = ibv_open_device(devices[i]);
      break;
    }
  }
  if (!context) {
    gpr_log(GPR_ERROR, "Unable to find device named: %s", attr.name.c_str());
  }
  return std::make_shared<Device>(attr, context);
}

}  // namespace ibverbs
}  // namespace grpc_core
