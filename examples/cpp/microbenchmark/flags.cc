#include "flags.h"

#include <gflags/gflags.h>
DEFINE_bool(affinity, false, "");
DEFINE_int32(batch, 100000, "");
DEFINE_int32(cqs, 1, "");
DEFINE_int32(executor, 0, "");
DEFINE_bool(generic, false, "");
DEFINE_string(host, "", "");
DEFINE_int32(threads, 1, "");
DEFINE_string(mode, "", "bp,event,bpev,bprr");
DEFINE_int32(port, 12345, "");
DEFINE_int32(polling_thread, 8, "");
DEFINE_int32(req, 64, "");
DEFINE_int32(resp, 1024, "");
DEFINE_int32(start_cpu, 0, "Start number of CPU binding");
DEFINE_int32(send_interval, 0, "max sleep time for each sending");
DEFINE_int32(warmup, 10000, "");
