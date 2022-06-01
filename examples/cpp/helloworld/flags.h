

#ifndef HELLOWORLD_FLAGS_H
#define HELLOWORLD_FLAGS_H
#include <gflags/gflags_declare.h>

DECLARE_int32(cqs);
DECLARE_int32(threads);
DECLARE_int32(batch);
DECLARE_string(host);
DECLARE_int32(req);
DECLARE_int32(resp);
DECLARE_int32(warmup);
DECLARE_int32(poll_num);
DECLARE_bool(affinity);
DECLARE_bool(grab_mem);
DECLARE_string(mode);
DECLARE_int32(sleep);
DECLARE_int32(executor);
DECLARE_int32(node);
DECLARE_int32(cpu);
#endif  // HELLOWORLD_FLAGS_H
