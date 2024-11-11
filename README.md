RR-Compound: RDMA-fused gRPC towards General-Purpose Data Processing for both Low latency and High Throughput
===================================

RR-Compound is a RDMA-fused gRPC for general-purpose data processing to achieve both low latency and
high throughput. RR-Compound is fully compatible with gRPC and can be used as a drop-in replacement
without changing existing applications.

This branch of RR-Compound is developed based on the latest version of gRPC. 
Since gRPC has removed the `epollex` poller and now uses the `epoll1` poller by default, 
RR-Compound also adopts the `epoll1` poller, which is less performant compared to `epollex`. 
**To replicate the performance numbers in the paper, you should evaluate the code in the [master](https://github.com/pwrliang/grpc-rdma) branch.**

# 1. Build and Install

RR-Compound depends `libibverbs`.
Please make sure the library is installed before building RR-Compound. After this, you may follow the [official
instructions of gRPC](https://grpc.io/docs/languages/cpp/quickstart/) to build RR-Compound.

# 2. Configurations

Users can tune parameters using environment variables, 
including enabling or disabling RDMA support. 
By default, **RDMA support is disabled**, causing RR-Compound to behave like standard gRPC.
To enable RDMA support, set `export GRPC_ENABLE_RDMA_SUPPORT=true` before launching your program. 
This environment variable must be enabled on both the **client** and **server** sides.


| Key                               | Default Value | Comments                                                                                                                         |
|-----------------------------------|---------------|----------------------------------------------------------------------------------------------------------------------------------|
| GRPC_ENABLE_RDMA_SUPPORT          | "false"       | Whether to enable RDMA support                                                                                                   |
| GRPC_RDMA_DEVICE_NAME             | ""            | If this value is unspecific, RR-Compound uses the first RDMA device                                                              |          
| GRPC_RDMA_PORT_NUM                | 1             | RDMA port number                                                                                                                 |
| GRPC_RDMA_GID_INDEX               | 0             | RDMA gid                                                                                                                         | 
| GRPC_RDMA_POLLER_THREAD_NUM       | 1             | How many polling threads are used to detect incoming messages                                                                    |                                                             
| GRPC_RDMA_BUSY_POLLING_TIMEOUT_US | 500           | A threshold hold to determine whether resort to epoll to wait for incoming messages, unit: microseconds, only effective for BPEV |
| GRPC_RDMA_POLLER_SLEEP_TIMEOUT_MS | 1000          | Putting polling threads into sleep if no connections are found within the timeout                                                |
| GRPC_RDMA_RING_BUFFER_SIZE_KB     | 4096          | Ring buffer size in KB per connection                                                                                            |

# 3. Example - Use RR-Compound in Ray

We noticed that many users in the Ray community have requested an RDMA-enhanced gRPC for improved performance. 
In response, we have forked [releases/2.38.0](https://github.com/pwrliang/ray) of Ray and replaced its gRPC dependency with RR-Compound.