RR-Compound: RDMA-fused gRPC towards General-Purpose Data Processing for both Low latency and High Throughput
===================================

RR-Compound is a RDMA-fused gRPC for general-purpose data processing to achieve both low latency and
high throughput. RR-Compound is fully compatible with gRPC and can be used as a drop-in replacement
without changing existing applications.

# 1. Build and Install

RR-Compound is developed based on gRPC v1.38.0. RR-Compound depends `libibverbs`.
Please make sure the library is installed before building RR-Compound. After this, you may follow the [official
instructions of gRPC](https://grpc.io/docs/languages/cpp/quickstart/) to build RR-Compound.

# 2. Configurations

| Key                               | Default Value | Comments                                                                                                                         |
|-----------------------------------|---------------|----------------------------------------------------------------------------------------------------------------------------------|
| GRPC_PLATFORM_TYPE                | ""            | Connection Management Type, available options: RDMA_BP,RDMA_EVENT,RDMA_BPEV,TCP. If this value is not set, TCP will be used      |
| GRPC_RDMA_DEVICE_NAME             | ""            | If this value is unspecific, RR-Compound uses the first RDMA device                                                              |          
| GRPC_RDMA_PORT_NUM                | 1             | RDMA port number                                                                                                                 |
| GRPC_RDMA_GID_INDEX               | 0             | RDMA gid                                                                                                                         | 
| GRPC_RDMA_POLLER_THREAD_NUM       | 1             | How many polling threads are used to detect incoming messages                                                                    |                                                             
| GRPC_RDMA_BUSY_POLLING_TIMEOUT_US | 500           | A threshold hold to determine whether resort to epoll to wait for incoming messages, unit: microseconds, only effective for BPEV |
| GRPC_RDMA_POLLER_SLEEP_TIMEOUT_MS | 1000          | Putting polling threads into sleep if no connections are found within the timeout                                                |
| GRPC_RDMA_RING_BUFFER_SIZE_KB     | 4096          | Ring buffer size in KB per connection                                                                                            |
