

#ifndef HELLOWORLD_FLAGS_H
#define HELLOWORLD_FLAGS_H
#include <gflags/gflags_declare.h>
DECLARE_bool(affinity);
DECLARE_int32(batch);
DECLARE_int32(cqs);
DECLARE_int32(executor);
DECLARE_bool(generic);
DECLARE_string(host);
DECLARE_int32(threads);
DECLARE_string(mode);
DECLARE_int32(port);
DECLARE_int32(polling_thread);
DECLARE_int32(req);
DECLARE_int32(resp);
DECLARE_int32(start_cpu);
DECLARE_int32(send_interval);
DECLARE_int32(warmup);
#endif  // HELLOWORLD_FLAGS_H
