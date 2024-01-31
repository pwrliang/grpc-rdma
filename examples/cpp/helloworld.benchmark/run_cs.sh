export GRPC_RDMA_ZEROCOPY_ENABLE=true
export GRPC_VERBOSITY=INFO
#export GRPC_TRACE="rdma_sr_bpev,transport_bpev"
# export GRPC_TRACE="rdma,rdma_sr_event"
export GRPC_RDMA_AFFINITY=false
export GRPC_RDMA_BPEV_POLLER=both
#export GRPC_RDMA_BPEV_POLLING_TIMEOUT=100
export GRPC_PLATFORM_TYPE=RDMA_EVENT
export GRPC_RDMA_RING_BUFFER_SIZE=$((1024*1024*64))
export GRPC_RDMA_BPEV_POLLING_THREAD=2
./cmake/build/greeter_cs -host="$1" -req $((1024*1024*4)) -resp $((1024*1024*4)) -batch 1000 -threads 8 -worker_id="$2"