RR-Compound: RDMA-fused gRPC towards General-Purpose Data Processing for both Low latency and High Throughput
===================================

RR-Compound is a RDMA-fused gRPC for general-purpose data processing to achieve both low latency and
high throughput. RR-Compound is fully compatible with gRPC and can be used as a drop-in replacement 
without changing existing applications.

# 1. Build and Install

RR-Compound is developed based on gRPC. The only new dependency introduced in the RR-Compound is `libibverbs`.
Please make sure the library is installed before building RR-Compound. After this, you may follow the [official 
instructions of gRPC](https://grpc.io/docs/languages/cpp/quickstart/) to build RR-Compound.

# 2. Configurations

| Key                            | Value                            | Comments                                                                                                                         |
|--------------------------------|----------------------------------|----------------------------------------------------------------------------------------------------------------------------------|
| GRPC_PLATFORM_TYPE             | RDMA_BP,RDMA_EVENT,RDMA_BPEV,TCP | Connection Management Type, default: TCP                                                                                         |
| GRPC_RDMA_RING_BUFFER_SIZE     | >=4MB                            | Ring buffer size per connection, defualt: 10485760, unit: byte                                                                   |
| GRPC_RDMA_ZEROCOPY_ENABLE      | true,false                       | Whether enable zero-copy, default: false                                                                                         |
| GRPC_RDMA_BPEV_POLLING_TIMEOUT | >=0                              | A threshold hold to determine whether resort to epoll to wait for incoming messages, unit: microseconds, only effective for BPEV |
| GRPC_RDMA_BPEV_POLLING_THREAD  | >=1                              | The number of threads used as polling threads, only effective for BPEV                                                           |