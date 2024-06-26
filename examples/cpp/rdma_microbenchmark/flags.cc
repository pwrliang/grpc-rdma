#include "flags.h"

#include <gflags/gflags.h>

DEFINE_int32(batch, 100000, "");
DEFINE_int32(port, 12345, "");
DEFINE_int32(warmup, 10000, "");
DEFINE_int32(polling_thread, 8, "");
DEFINE_bool(affinity, false, "");
DEFINE_int32(cpu_limit, -1, "");
DEFINE_string(mode, "", "bp,event,bpev,bprr");
DEFINE_int32(computing_thread, 0, "");
DEFINE_int32(client_timeout, 0, "epoll timeout");
DEFINE_int32(server_timeout, 0, "epoll timeout");
DEFINE_string(host, "", "");
DEFINE_bool(mpiserver, true, "");
DEFINE_int32(nclient, 0, "");
DEFINE_int32(runtime, 8, "");
DEFINE_string(dir, "bi", "s2c,c2s,bi");
DEFINE_int32(max_worker, -1, "");
DEFINE_bool(rw, true, "read and write");
DEFINE_int32(start_cpu, 0, "Start number of CPU binding");
DEFINE_int32(send_interval, 0, "max sleep time for each sending");
DEFINE_int32(work_thread, -1,
             "It is used to control how many threads are serving");