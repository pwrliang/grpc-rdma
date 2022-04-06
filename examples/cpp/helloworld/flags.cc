#include "flags.h"

#include <gflags/gflags.h>

DEFINE_int32(threads, 1, "");
DEFINE_int32(batch, 100000, "");
DEFINE_string(host, "localhost", "");
DEFINE_int32(req, 64, "");
DEFINE_int32(resp, 1024, "");
DEFINE_int32(warmup, 100, "");