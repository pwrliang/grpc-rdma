// Copyright 2023 gRPC authors.
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

//
// Automatically generated by tools/codegen/core/gen_config_vars.py
//

#include "src/core/lib/config/config_vars.h"

#include <grpc/support/port_platform.h>

#include "absl/flags/flag.h"
#include "absl/strings/escaping.h"
#include "src/core/lib/config/load_config.h"

#ifndef GPR_DEFAULT_LOG_VERBOSITY_STRING
#define GPR_DEFAULT_LOG_VERBOSITY_STRING ""
#endif  // !GPR_DEFAULT_LOG_VERBOSITY_STRING

#ifdef GRPC_ENABLE_FORK_SUPPORT
#define GRPC_ENABLE_FORK_SUPPORT_DEFAULT true
#else
#define GRPC_ENABLE_FORK_SUPPORT_DEFAULT false
#endif  // GRPC_ENABLE_FORK_SUPPORT

ABSL_FLAG(std::vector<std::string>, grpc_experiments, {},
          "A comma separated list of currently active experiments. Experiments "
          "may be prefixed with a '-' to disable them.");
ABSL_FLAG(absl::optional<int32_t>, grpc_client_channel_backup_poll_interval_ms,
          {},
          "Declares the interval in ms between two backup polls on client "
          "channels. These polls are run in the timer thread so that gRPC can "
          "process connection failures while there is no active polling "
          "thread. They help reconnect disconnected client channels (mostly "
          "due to idleness), so that the next RPC on this channel won't fail. "
          "Set to 0 to turn off the backup polls.");
ABSL_FLAG(absl::optional<std::string>, grpc_dns_resolver, {},
          "Declares which DNS resolver to use. The default is ares if gRPC is "
          "built with c-ares support. Otherwise, the value of this environment "
          "variable is ignored.");
ABSL_FLAG(std::vector<std::string>, grpc_trace, {},
          "A comma separated list of tracers that provide additional insight "
          "into how gRPC C core is processing requests via debug logs.");
ABSL_FLAG(absl::optional<std::string>, grpc_verbosity, {},
          "Logging verbosity.");
ABSL_FLAG(absl::optional<bool>, grpc_enable_fork_support, {},
          "Enable fork support");
ABSL_FLAG(absl::optional<bool>, grpc_enable_rdma_support, {},
          "Enable RDMA support");
ABSL_FLAG(absl::optional<std::string>, grpc_poll_strategy, {},
          "Declares which polling engines to try when starting gRPC. This is a "
          "comma-separated list of engines, which are tried in priority order "
          "first -> last.");
ABSL_FLAG(absl::optional<bool>, grpc_abort_on_leaks, {},
          "A debugging aid to cause a call to abort() when gRPC objects are "
          "leaked past grpc_shutdown()");
ABSL_FLAG(absl::optional<std::string>, grpc_system_ssl_roots_dir, {},
          "Custom directory to SSL Roots");
ABSL_FLAG(absl::optional<std::string>, grpc_default_ssl_roots_file_path, {},
          "Path to the default SSL roots file.");
ABSL_FLAG(absl::optional<bool>, grpc_not_use_system_ssl_roots, {},
          "Disable loading system root certificates.");
ABSL_FLAG(absl::optional<std::string>, grpc_ssl_cipher_suites, {},
          "A colon separated list of cipher suites to use with OpenSSL");
ABSL_FLAG(absl::optional<bool>, grpc_cpp_experimental_disable_reflection, {},
          "EXPERIMENTAL. Only respected when there is a dependency on "
          ":grpc++_reflection. If true, no reflection server will be "
          "automatically added.");
ABSL_FLAG(absl::optional<std::string>, grpc_rdma_device_name, {}, "");
ABSL_FLAG(absl::optional<int32_t>, grpc_rdma_port_num, {}, "");
ABSL_FLAG(absl::optional<int32_t>, grpc_rdma_gid_index, {}, "");
ABSL_FLAG(absl::optional<int32_t>, grpc_rdma_poller_thread_num, {}, "");
ABSL_FLAG(absl::optional<int32_t>, grpc_rdma_busy_polling_timeout_us, {}, "");
ABSL_FLAG(absl::optional<int32_t>, grpc_rdma_poller_sleep_timeout_ms, {}, "");
ABSL_FLAG(absl::optional<int32_t>, grpc_rdma_ring_buffer_size_kb, {}, "");

namespace grpc_core {

ConfigVars::ConfigVars(const Overrides& overrides)
    : client_channel_backup_poll_interval_ms_(
          LoadConfig(FLAGS_grpc_client_channel_backup_poll_interval_ms,
                     "GRPC_CLIENT_CHANNEL_BACKUP_POLL_INTERVAL_MS",
                     overrides.client_channel_backup_poll_interval_ms, 5000)),
      enable_fork_support_(LoadConfig(
          FLAGS_grpc_enable_fork_support, "GRPC_ENABLE_FORK_SUPPORT",
          overrides.enable_fork_support, GRPC_ENABLE_FORK_SUPPORT_DEFAULT)),
      enable_rdma_support_(LoadConfig(FLAGS_grpc_enable_rdma_support,
                                      "GRPC_ENABLE_RDMA_SUPPORT",
                                      overrides.enable_rdma_support, false)),
      abort_on_leaks_(LoadConfig(FLAGS_grpc_abort_on_leaks,
                                 "GRPC_ABORT_ON_LEAKS",
                                 overrides.abort_on_leaks, false)),
      not_use_system_ssl_roots_(LoadConfig(
          FLAGS_grpc_not_use_system_ssl_roots, "GRPC_NOT_USE_SYSTEM_SSL_ROOTS",
          overrides.not_use_system_ssl_roots, false)),
      cpp_experimental_disable_reflection_(
          LoadConfig(FLAGS_grpc_cpp_experimental_disable_reflection,
                     "GRPC_CPP_EXPERIMENTAL_DISABLE_REFLECTION",
                     overrides.cpp_experimental_disable_reflection, false)),
      dns_resolver_(LoadConfig(FLAGS_grpc_dns_resolver, "GRPC_DNS_RESOLVER",
                               overrides.dns_resolver, "")),
      verbosity_(LoadConfig(FLAGS_grpc_verbosity, "GRPC_VERBOSITY",
                            overrides.verbosity,
                            GPR_DEFAULT_LOG_VERBOSITY_STRING)),
      poll_strategy_(LoadConfig(FLAGS_grpc_poll_strategy, "GRPC_POLL_STRATEGY",
                                overrides.poll_strategy, "all")),
      ssl_cipher_suites_(LoadConfig(
          FLAGS_grpc_ssl_cipher_suites, "GRPC_SSL_CIPHER_SUITES",
          overrides.ssl_cipher_suites,
          "TLS_AES_128_GCM_SHA256:TLS_AES_256_GCM_SHA384:TLS_CHACHA20_POLY1305_"
          "SHA256:ECDHE-ECDSA-AES128-GCM-SHA256:ECDHE-ECDSA-AES256-GCM-SHA384:"
          "ECDHE-RSA-AES128-GCM-SHA256:ECDHE-RSA-AES256-GCM-SHA384")),
      experiments_(LoadConfig(FLAGS_grpc_experiments, "GRPC_EXPERIMENTS",
                              overrides.experiments, "")),
      trace_(LoadConfig(FLAGS_grpc_trace, "GRPC_TRACE", overrides.trace, "")),
      override_system_ssl_roots_dir_(overrides.system_ssl_roots_dir),
      override_default_ssl_roots_file_path_(
          overrides.default_ssl_roots_file_path),
      override_rdma_device_name_(overrides.rdma_device_name),
      rdma_port_num_(LoadConfig(FLAGS_grpc_rdma_port_num, "GRPC_RDMA_PORT_NUM",
                                overrides.rdma_port_num, 1)),
      rdma_gid_index_(LoadConfig(FLAGS_grpc_rdma_gid_index,
                                 "GRPC_RDMA_GID_INDEX",
                                 overrides.rdma_gid_index, 0)),
      rdma_poller_thread_num_(LoadConfig(FLAGS_grpc_rdma_poller_thread_num,
                                         "GRPC_RDMA_POLLER_THREAD_NUM",
                                         overrides.rdma_poller_thread_num, 1)),
      rdma_busy_polling_timeout_us_(
          LoadConfig(FLAGS_grpc_rdma_busy_polling_timeout_us,
                     "GRPC_RDMA_BUSY_POLLING_TIMEOUT_US",
                     overrides.rdma_busy_polling_timeout_us, 500)),
      rdma_poller_sleep_timeout_ms_(
          LoadConfig(FLAGS_grpc_rdma_poller_sleep_timeout_ms,
                     "GRPC_RDMA_POLLER_SLEEP_TIMEOUT_MS",
                     overrides.rdma_poller_sleep_timeout_ms, 1000)),
      rdma_ring_buffer_size_kb_(LoadConfig(
          FLAGS_grpc_rdma_ring_buffer_size_kb, "GRPC_RDMA_RING_BUFFER_SIZE_KB",
          overrides.rdma_ring_buffer_size_kb, 4096)) {}

std::string ConfigVars::SystemSslRootsDir() const {
  return LoadConfig(FLAGS_grpc_system_ssl_roots_dir,
                    "GRPC_SYSTEM_SSL_ROOTS_DIR", override_system_ssl_roots_dir_,
                    "");
}

std::string ConfigVars::DefaultSslRootsFilePath() const {
  return LoadConfig(FLAGS_grpc_default_ssl_roots_file_path,
                    "GRPC_DEFAULT_SSL_ROOTS_FILE_PATH",
                    override_default_ssl_roots_file_path_, "");
}

std::string ConfigVars::RdmaDeviceName() const {
  return LoadConfig(FLAGS_grpc_rdma_device_name, "GRPC_RDMA_DEVICE_NAME",
                    override_rdma_device_name_, "");
}

std::string ConfigVars::ToString() const {
  return absl::StrCat(
      "experiments: ", "\"", absl::CEscape(Experiments()), "\"",
      ", client_channel_backup_poll_interval_ms: ",
      ClientChannelBackupPollIntervalMs(), ", dns_resolver: ", "\"",
      absl::CEscape(DnsResolver()), "\"", ", trace: ", "\"",
      absl::CEscape(Trace()), "\"", ", verbosity: ", "\"",
      absl::CEscape(Verbosity()), "\"",
      ", enable_fork_support: ", EnableForkSupport() ? "true" : "false",
      ", enable_rdma_support: ", EnableRdmaSupport() ? "true" : "false",
      ", poll_strategy: ", "\"", absl::CEscape(PollStrategy()), "\"",
      ", abort_on_leaks: ", AbortOnLeaks() ? "true" : "false",
      ", system_ssl_roots_dir: ", "\"", absl::CEscape(SystemSslRootsDir()),
      "\"", ", default_ssl_roots_file_path: ", "\"",
      absl::CEscape(DefaultSslRootsFilePath()), "\"",
      ", not_use_system_ssl_roots: ", NotUseSystemSslRoots() ? "true" : "false",
      ", ssl_cipher_suites: ", "\"", absl::CEscape(SslCipherSuites()), "\"",
      ", cpp_experimental_disable_reflection: ",
      CppExperimentalDisableReflection() ? "true" : "false",
      ", rdma_device_name: ", RdmaDeviceName(),
      ", rdma_port_num: ", RdmaPortNum(), ", rdma_gid_index: ", RdmaGidIndex(),
      ", rdma_poller_thread_num: ", RdmaPollerThreadNum(),
      ", rdma_busy_polling_timeout_us: ", RdmaBusyPollingTimeoutUs(),
      ", rdma_poller_sleep_timeout_ms: ", RdmaPollerSleepTimeoutMs(),
      ", rdma_ring_buffer_size_kb: ", RdmaRingBufferSizeKb());
}

}  // namespace grpc_core
