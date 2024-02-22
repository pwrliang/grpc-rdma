set -e

if [[ -z "$MB_HOME" ]]; then
  export MB_HOME=/users/PAS0350/geng161/.clion/grpc-rdma/examples/cpp/micro-bench/cmake-build-release-pitzer02
  #export MB_HOME=/home/geng.161/.clion/grpc-rdma/examples/cpp/micro-bench/cmake-build-release-mri
fi

hostfile_template="hosts"
hostfile_template=$(realpath "$hostfile_template")
if [[ ! -f "$hostfile_template" ]]; then
  echo "Bad hostfile $hostfile_template"
  exit 1
fi

# Available options: TCP RDMA_EVENT RDMA_BP RDMA_BPEV
GRPC_MODES=(TCP RDMA_EVENT RDMA_BP RDMA_BPEV)

function set_hostfile() {
  name_prefix=$(basename "$hostfile_template")
  hostfile="/tmp/grpc_mb_$name_prefix"
  ./gen_hostfile.py ./hosts "$1" >"$hostfile"
  export HOSTS_PATH="$hostfile"
}

function throughput() {
  clients=(1 2 4 8 16 32 64 128)
  server_thread=38
  cqs=$server_thread
  req=32
  resp=8
  rpcs=10000000
  duration=10
  concurrent=1
  streaming="true"

  for grp_mode in "${GRPC_MODES[@]}"; do
    for n_clients in "${clients[@]}"; do
      set_hostfile "$n_clients"

      export GRPC_PLATFORM_TYPE=$grp_mode
      export LOG_NAME="tput_${grp_mode}_cli_${n_clients}_concurrent_${concurrent}_streaming_${streaming}"
      ./run.sh --server-thread=$server_thread \
        --cqs=$cqs \
        --req=$req \
        --resp=$resp \
        --rpcs=$rpcs \
        --concurrent=$concurrent \
        --duration=$duration \
        --polling-timeout=100 \
        --streaming="$streaming" \
        --polling-thread=2 \
        --overwrite
    done
  done
}

function varying_clients_latency() {
  clients=(1 2 4 8 16 32 64 128)
  server_thread=38
  cqs=$server_thread
  req=32
  resp=8
  rpcs=10000000
  duration=30
  concurrent=1
  streaming="true"

  GRPC_MODES=(TCP RDMA_BPEV)

  for grp_mode in "${GRPC_MODES[@]}"; do
    for n_clients in "${clients[@]}"; do
      set_hostfile "$n_clients"

      export GRPC_PLATFORM_TYPE=$grp_mode
      export LOG_NAME="lat_${grp_mode}_cli_${n_clients}_concurrent_${concurrent}_streaming_${streaming}"
      ./run.sh --server-thread=$server_thread \
        --cqs=$cqs \
        --req=$req \
        --resp=$resp \
        --rpcs=$rpcs \
        --concurrent=$concurrent \
        --duration=$duration \
        --polling-timeout=100 \
        --streaming="$streaming" \
        --polling-thread=2 \
        --random-delay="true"
    done
  done
}

function varying_busy_polling_time() {
  time_list=(0 2 4 6 8 10 12 14 16)
  server_thread=1
  cqs=$server_thread
  req=32
  resp=8
  rpcs=10000000
  duration=10
  concurrent=1
  streaming="true"
  n_clients=1
  grp_mode="RDMA_BPEV"

  set_hostfile "$n_clients"

  for bp_timeout in "${time_list[@]}"; do
    export GRPC_PLATFORM_TYPE=$grp_mode
    export LOG_NAME="tput_${grp_mode}_cli_${n_clients}_bp_timeout_${bp_timeout}"
    ./run.sh --server-thread=$server_thread \
      --cqs=$cqs \
      --req=$req \
      --resp=$resp \
      --rpcs=$rpcs \
      --concurrent=$concurrent \
      --duration=$duration \
      --polling-timeout=$bp_timeout \
      --streaming="$streaming" \
      --polling-thread=1
  done
}

#function latency() {
#  size_list=(64)
#  server_thread=1
#  cqs=$server_thread
#  resp=8
#  rpcs=10000000
#  duration=10
#  concurrent=1
#  streaming="true"
#  numa="false"
#  n_clients=1
#
#  for grp_mode in "${GRPC_MODES[@]}"; do
#    for req in "${size_list[@]}"; do
#      set_hostfile "$n_clients"
#
#      export GRPC_PLATFORM_TYPE=$grp_mode
#      export LOG_NAME="latency_${grp_mode}_size_${req}_streaming_${streaming}"
#      ./run.sh --server-thread=$server_thread \
#        --cqs=$cqs \
#        --req=$req \
#        --resp=$resp \
#        --rpcs=$rpcs \
#        --concurrent=$concurrent \
#        --duration=$duration \
#        --numa=$numa \
#        --polling-timeout=100 \
#        --streaming="$streaming" \
#        --polling-thread=1
#    done
#  done
#}

function varying_clients() {
  server_thread=30
  cqs=$server_thread
  req=32
  resp=8
  rpcs=10000000
  concurrent=1
  duration=20
  numa="false"
  grp_mode="RDMA_BPEV"
  streaming="true"
  export GRPC_PLATFORM_TYPE="$grp_mode"
  polling_thread_num=1

  for n_clients in 900 950 1000 1500 2000 2500 3000 3500; do
    set_hostfile "$n_clients"
    export LOG_NAME="polling_thread_${polling_thread_num}_working_thread_${server_thread}_clients_${n_clients}"

    ./run.sh --server-thread=$server_thread \
      --cqs=$cqs \
      --req=$req \
      --resp=$resp \
      --rpcs=$rpcs \
      --concurrent=$concurrent \
      --duration=$duration \
      --numa=$numa \
      --overwrite \
      --polling-timeout=0 \
      --streaming="$streaming" \
      --polling-thread=$polling_thread_num \
      --random-delay="true"
  done
}

function varying_polling_threads_latency() {
  server_thread=32
  cqs=$server_thread
  req=32
  resp=8
  rpcs=10000000
  concurrent=1
  duration=60
  numa="false"
  grp_mode="RDMA_BPEV"
  n_clients=320
  streaming="true"

  set_hostfile "$n_clients"
  export GRPC_PLATFORM_TYPE="$grp_mode"

  for thread_num in 1 2 3 4 5 6 7 8; do
    export LOG_NAME="lat_polling_thread_${thread_num}_clients_${n_clients}"

    ./run.sh --server-thread=$server_thread \
      --cqs=$cqs \
      --req=$req \
      --resp=$resp \
      --rpcs=$rpcs \
      --concurrent=$concurrent \
      --duration=$duration \
      --numa=$numa \
      --polling-timeout=0 \
      --streaming="$streaming" \
      --polling-thread=$thread_num \
      --random-delay="true"
  done
}

function varying_polling_threads_throughput() {
  server_thread=32
  cqs=$server_thread
  req=32
  resp=8
  rpcs=10000000
  concurrent=1
  duration=20
  numa="false"
  grp_mode="RDMA_BPEV"
  n_clients=64
  streaming="true"

  set_hostfile "$n_clients"
  export GRPC_PLATFORM_TYPE="$grp_mode"

  for thread_num in 1 2 3 4 5 6 7 8; do
    export LOG_NAME="tput_polling_thread_${thread_num}_clients_${n_clients}"

    ./run.sh --server-thread=$server_thread \
      --cqs=$cqs \
      --req=$req \
      --resp=$resp \
      --rpcs=$rpcs \
      --concurrent=$concurrent \
      --duration=$duration \
      --numa=$numa \
      --polling-timeout=0 \
      --streaming="$streaming" \
      --polling-thread=$thread_num \
      --random-delay="false"
  done
}

function bandwidth() {
  # 8 clients 32k req 128kb ringbuf stuck
  n_clients=8
  server_thread=40
  cqs=$server_thread
  req=$((32 * 1024))
  resp=8
  rpcs=10000000
  concurrent=1
  duration=10
  grp_mode="RDMA_BPEV"
  streaming="true"

  set_hostfile "$n_clients"
  export GRPC_PLATFORM_TYPE="$grp_mode"

  for ring_buf_kb in 128; do
    export LOG_NAME="bandwidth_${grp_mode}_cli_${n_clients}_req_${req}_ringbuf_${ring_buf_kb}"

    ./run.sh --server-thread=$server_thread \
      --cqs=$cqs \
      --req=$req \
      --resp=$resp \
      --rpcs=$rpcs \
      --concurrent=$concurrent \
      --duration=$duration \
      --polling-timeout=500 \
      --streaming="$streaming" \
      --polling-thread=1 \
      --ring-buffer-kb=$ring_buf_kb \
      --overwrite
  done
}

function adhoc() {
  n_clients=8
  server_thread=$n_clients
  cqs=$server_thread
  req=$((512 * 1024))
  resp=8
  rpcs=10000000
  concurrent=1
  duration=10
  numa="false"
  grp_mode="RDMA_BPEV"
  streaming="true"
  ring_buf_kb=32

  set_hostfile "$n_clients"
  export GRPC_PLATFORM_TYPE="$grp_mode"
  export LOG_NAME="adhoc_${grp_mode}_cli_${n_clients}_cq_thread_${server_thread}"

  ./run.sh --server-thread=$server_thread \
    --cqs=$cqs \
    --req=$req \
    --resp=$resp \
    --rpcs=$rpcs \
    --concurrent=$concurrent \
    --duration=$duration \
    --numa=$numa \
    --overwrite \
    --polling-timeout=100 \
    --streaming="$streaming" \
    --polling-thread=1 \
    --random-delay="false" \
    --profiling="false" \
    --ring-buffer-kb=$ring_buf_kb
}

for i in "$@"; do
  case $i in
  --throughput)
    throughput
    shift
    ;;
  --latency)
    latency
    shift
    ;;
  --bandwidth)
    bandwidth
    shift
    ;;
  --adhoc)
    adhoc
    shift
    ;;
  --varying-clients)
    varying_clients
    shift
    ;;
  --varying-clients-latency)
    varying_clients_latency
    shift
    ;;
  --varying-polling-threads-latency)
    varying_polling_threads_latency
    shift
    ;;
  --varying-polling-threads-throughput)
    varying_polling_threads_throughput
    shift
    ;;
  --varying-busy-polling-time)
    varying_busy_polling_time
    shift
    ;;
  --* | -*)
    echo "Unknown option $i"
    exit 1
    ;;
  *)
    echo "--bench or --profile"
    exit 1
    ;;
  esac
done
