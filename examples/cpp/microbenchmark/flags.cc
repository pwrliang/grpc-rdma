#include "flags.h"

#include <gflags/gflags.h>

DEFINE_int32(batch, 100000, "");
DEFINE_int32(port, 12345, "");
DEFINE_int32(warmup, 10000, "");
DEFINE_int32(polling_thread, 8, "");
DEFINE_bool(affinity, false, "");
DEFINE_string(mode, "", "bp,event,bprr");
DEFINE_int32(computing_thread, 0, "");
DEFINE_int32(timeout, 0, "epoll timeout");
DEFINE_string(host, "", "");
DEFINE_bool(mpiserver, true, "");
DEFINE_int32(nclient, 0, "");