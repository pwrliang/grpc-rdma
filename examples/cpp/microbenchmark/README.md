## Server

`export GRPC_PROFILING=micro`

`./greeter_async_server2 -mode=RDMA_BP -cqs=4 -threads=4 -resp=1024`

## Client
`mpirun -n 8 ./greeter_async_client2 -mode=RDMA_BP -host=mi100-01 -threads=1 -batch=10000 -req=1024`