

#include "flags.h"

#include <gflags/gflags.h>

DEFINE_int32(cqs, 1, "");
DEFINE_int32(threads, 1, "");
DEFINE_int32(batch, 100000, "");
DEFINE_int32(worker_id, -1, "");
DEFINE_string(host, "localhost", "");
DEFINE_int32(req, 64, "");
DEFINE_int32(resp, 1024, "");
DEFINE_int32(warmup, 1000, "");
DEFINE_bool(affinity, false, "");
DEFINE_bool(grab_mem, false, "");
DEFINE_string(mode, "TCP", "");
DEFINE_int32(send_interval, 0, "");
DEFINE_int32(executor, 0, "");
DEFINE_int32(node, 1, "");
DEFINE_int32(cpu, 28, "");
DEFINE_bool(server, false, "");
DEFINE_bool(generic, false, "");