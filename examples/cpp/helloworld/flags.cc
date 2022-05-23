#include "flags.h"

#include <gflags/gflags.h>

DEFINE_int32(cqs, 1, "");
DEFINE_int32(threads, 1, "");
DEFINE_int32(batch, 100000, "");
DEFINE_string(host, "localhost", "");
DEFINE_int32(req, 64, "");
DEFINE_int32(resp, 1024, "");
DEFINE_int32(warmup, 1000, "");
DEFINE_int32(poll_num, 1, "");
DEFINE_bool(affinity, false, "");
DEFINE_bool(grab_mem, false, "");
DEFINE_string(mode, "", "");
DEFINE_int32(sleep, 0, "");
DEFINE_int32(executor, 0, "");