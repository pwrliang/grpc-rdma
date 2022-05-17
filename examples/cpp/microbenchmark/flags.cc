#include "flags.h"

#include <gflags/gflags.h>

DEFINE_int32(batch, 100000, "");
DEFINE_int32(port, 12345, "");
DEFINE_int32(warmup, 10000, "");
DEFINE_int32(polling_thread, 8, "");
DEFINE_bool(affinity, false, "");
DEFINE_string(mode, "", "bp,event,bprr");
DEFINE_int32(computing_thread, 0, "");
DEFINE_int32(client_timeout, 0, "epoll timeout");
DEFINE_int32(server_timeout, 0, "epoll timeout");
DEFINE_string(host, "", "");
DEFINE_bool(mpiserver, true, "");
DEFINE_int32(nclient, 0, "");
DEFINE_int32(runtime, 8, "");
DEFINE_string(dir, "bi", "s2c,c2s,bi");
DEFINE_int32(max_worker, -1, "");