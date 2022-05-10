

#ifndef HELLOWORLD_FLAGS_H
#define HELLOWORLD_FLAGS_H
#include <gflags/gflags_declare.h>

DECLARE_int32(batch);
DECLARE_int32(port);
DECLARE_int32(warmup);
DECLARE_int32(polling_thread);
DECLARE_bool(affinity);
DECLARE_string(mode);
DECLARE_int32(computing_thread);
DECLARE_int32(timeout);
#endif  // HELLOWORLD_FLAGS_H
