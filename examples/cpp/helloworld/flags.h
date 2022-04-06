

#ifndef HELLOWORLD_FLAGS_H
#define HELLOWORLD_FLAGS_H
#include <gflags/gflags_declare.h>

DECLARE_int32(threads);
DECLARE_int32(batch);
DECLARE_string(host);
DECLARE_int32(req);
DECLARE_int32(resp);
DECLARE_int32(warmup);
#endif  // HELLOWORLD_FLAGS_H
