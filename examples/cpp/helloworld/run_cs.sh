export GRPC_RDMA_ZEROCOPY_ENABLE=false
export GRPC_VERBOSITY=INFO
#export GRPC_TRACE="rdma_sr_bpev,transport_bpev"
#export GRPC_TRACE="rdma,rdma_sr_event"
export GRPC_RDMA_AFFINITY=false
export GRPC_RDMA_BPEV_POLLER=both
#export GRPC_RDMA_BPEV_POLLING_TIMEOUT=100
export GRPC_PLATFORM_TYPE=RDMA_EVENT
export GRPC_RDMA_RING_BUFFER_SIZE=1048576
export GRPC_RDMA_BPEV_POLLING_THREAD=2
./cmake-build-release-ri2-head/greeter_cs -host="$1" -req 524288 -resp 524288 -batch 10000 -threads 1