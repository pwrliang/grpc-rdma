#ifndef MICROBENCHMARK_MB_H
#define MICROBENCHMARK_MB_H
#include <string>
#include "grpc/impl/codegen/log.h"
#define MAX_EVENTS 100

enum class Mode { kBusyPolling, kBusyPollingRR, kEvent };
bool running = true;

Mode parse_mode(const std::string& mode) {
  if (mode == "bp") {
    return Mode::kBusyPolling;
  } else if (mode == "bprr") {
    return Mode::kBusyPollingRR;
  } else if (mode == "event") {
    return Mode::kEvent;
  }
  gpr_log(GPR_ERROR, "Invalid mode: %s", mode.c_str());
  exit(1);
}

std::string mode_to_string(Mode mode) {
  switch (mode) {
    case Mode::kBusyPolling:
      return "Busy Polling";
    case Mode::kBusyPollingRR:
      return "Busy Poling (RR)";
    case Mode::kEvent:
      return "Event";
  }
  return "";
}
#endif  // MICROBENCHMARK_MB_H
