

#ifndef HELLOWORLD_FLAGS_H
#define HELLOWORLD_FLAGS_H
#include <gflags/gflags_declare.h>

DECLARE_int32(batch);
DECLARE_int32(port);
DECLARE_int32(polling_thread);
DECLARE_bool(affinity);
DECLARE_string(mode);
DECLARE_int32(computing_thread);
DECLARE_int32(client_timeout);
DECLARE_int32(server_timeout);
DECLARE_string(host);
DECLARE_bool(mpiserver);
DECLARE_int32(nclient);
DECLARE_int32(runtime);
DECLARE_string(dir);
DECLARE_bool(rw);
DECLARE_int32(start_cpu);
DECLARE_int32(send_interval);
#endif  // HELLOWORLD_FLAGS_H
