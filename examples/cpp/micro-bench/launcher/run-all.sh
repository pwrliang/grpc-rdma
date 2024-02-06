set -e
export RDMA_VERBOSITY=ERROR
export MB_HOME=/users/PAS0350/geng161/.clion/grpc-rdma/examples/cpp/micro-bench/cmake-build-release-pitzer02
#export MB_HOME=/home/geng.161/.clion/grpc-rdma/examples/cpp/micro-bench/cmake-build-release-mri

hostfile_template="hosts"
hostfile_template=$(realpath "$hostfile_template")
if [[ ! -f "$hostfile_template" ]]; then
  echo "Bad hostfile $hostfile_template"
  exit 1
fi

GRPC_MODES=(TCP RDMA_BP RDMA_EVENT)
GRPC_MODES=(TCP RDMA_EVENT RDMA_BP RDMA_BPEV)

function set_hostfile() {
  name_prefix=$(basename "$hostfile_template")
  hostfile="/tmp/grpc_mb_$name_prefix"
  ./gen_hostfile.py ./hosts "$1" >"$hostfile"
  export HOSTS_PATH="$hostfile"
}

function throughput() {
  clients=(1 2 4 8 16 32 64)
  server_thread=64
  cqs=$server_thread
  req=32
  resp=8
  rpcs=10000000
  duration=10
  concurrent=1

  for numa in true false; do
    for grp_mode in "${GRPC_MODES[@]}"; do
      for n_clients in "${clients[@]}"; do
        set_hostfile "$n_clients"

        export GRPC_PLATFORM_TYPE=$grp_mode
        export LOG_NAME="${grp_mode}_tput_cli_${n_clients}_numa_${numa}"
        ./run.sh --server-thread=$server_thread \
          --cqs=$cqs \
          --req=$req \
          --resp=$resp \
          --rpcs=$rpcs \
          --concurrent=$concurrent \
          --duration=$duration \
          --numa=$numa \
          --polling-timeout=500
      done
    done
  done
}

function adhoc() {
  server_thread=64
  cqs=$server_thread
  req=32
  resp=8
  rpcs=10000000
  concurrent=1
  duration=10
  numa="false"
  grp_mode="RDMA_BPEV"
  n_clients=64

  set_hostfile "$n_clients"
  export GRPC_PLATFORM_TYPE="$grp_mode"
  export LOG_NAME="adhoc_${grp_mode}_tput_cli_${n_clients}_numa_${numa}"

  ./run.sh --server-thread=$server_thread \
    --cqs=$cqs \
    --req=$req \
    --resp=$resp \
    --rpcs=$rpcs \
    --concurrent=$concurrent \
    --duration=$duration \
    --numa=$numa \
    --overwrite \
    --polling-timeout=500
}

for i in "$@"; do
  case $i in
  --throughput)
    throughput
    shift
    ;;
  --adhoc)
    adhoc
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
