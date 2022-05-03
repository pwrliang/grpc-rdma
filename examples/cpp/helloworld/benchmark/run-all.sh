set -e
export RDMA_VERBOSITY=ERROR
export HELLOWORLD_HOME=/home/geng.161/.cache/clion/tmp/tmp.wFl8U4DvNn/examples/cpp/helloworld/cmake-build-release-ri2-head

hostfile_template="hosts"
hostfile_template=$(realpath "$hostfile_template")
if [[ ! -f "$hostfile_template" ]]; then
  echo "Bad hostfile $hostfile_template"
  exit 1
fi

# for grp_mode in TCP RDMA_BP RDMA_EVENT; do

REQs=(64 128 256 512 1024 2048 4096 8192 16384 32768 65536 131072)
RESPs=(64 128 256 512 1024 2048 4096 8192 16384 32768 65536 131072)
POLL_NUMS=(1)
GRPC_MODES=(TCP RDMA_BP RDMA_EVENT)

function set_hostfile() {
  n_clients=$1
  name_prefix=$(basename "$hostfile_template")
  hostfile="/tmp/$name_prefix.${RANDOM}"
  while [[ -f "$hostfile" ]]; do
    hostfile="/tmp/$name_prefix.${RANDOM}"
  done
  ./gen_hostfile.py ./hosts "$n_clients" >"$hostfile"
  export HOSTS_PATH="$hostfile"
}

function profile() {
  n_clients=1
  server_thread=1
  #  for n_clients in 1 2 4 8 16 28 32; do
  set_hostfile $n_clients
  for i in "${!REQs[@]}"; do
    REQ=${REQs[i]}
    RESP=${RESPs[i]}
    for poll_num in "${POLL_NUMS[@]}"; do
      for grp_mode in "${GRPC_MODES[@]}"; do
        export GRPC_PLATFORM_TYPE=$grp_mode
        export LOG_SUFFIX="${grp_mode}_${REQ}_${RESP}_${poll_num}"
        ./run.sh --server_thread=$server_thread \
          --client_thread=1 \
          --profiling=milli \
          --req="$REQ" \
          --resp="$RESP" \
          --poll-num="$poll_num" \
          --batch=100000
      done
    done
  done
  #  done
}

function benchmark() {
  REQs=(64 1024 4096 65536)
  RESPs=(64 1024 4096 65536)

  for i in "${!REQs[@]}"; do
    REQ=${REQs[i]}
    RESP=${RESPs[i]}
    for poll_num in "${POLL_NUMS[@]}"; do
      for grp_mode in "${GRPC_MODES[@]}"; do
        export GRPC_PLATFORM_TYPE=$grp_mode
        for n_clients in 1 2 4 8 16 28 32 64 128; do
          set_hostfile $n_clients
          export LOG_SUFFIX="${grp_mode}_${REQ}_${RESP}_${poll_num}"
          ./run.sh --server_thread=28 --client_thread=1 --req="$REQ" --resp="$RESP" --poll-num="$poll_num" --batch=100000
        done
      done
    done
  done
}

for i in "$@"; do
  case $i in
  --profile)
    profile
    shift
    ;;
  --bench)
    benchmark
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

# Thread Scalability

#REQ=64
#RESP=1024
#for poll_num in 1 10 100 1000; do
#  for grp_mode in TCP RDMA_BP RDMA_EVENT; do
#    export GRPC_PLATFORM_TYPE=$grp_mode
#    n_clients=28
#    for n_th in $(seq 1 28); do
#      name_prefix=$(basename "$hostfile_template")
#      hostfile="/tmp/$name_prefix.${RANDOM}"
#      while [[ -f "$hostfile" ]]; do
#        hostfile="/tmp/$name_prefix.${RANDOM}"
#      done
#      ./gen_hostfile.py ./hosts "$n_clients" >"$hostfile"
#      export HOSTS_PATH="$hostfile"
#      export LOG_SUFFIX="${grp_mode}_th_${n_th}_${poll_num}"
#      ./run.sh --server_thread="${n_th}" --client_thread=1 --req=$REQ --resp=$RESP --poll-num=$poll_num --batch=100000
#    done
#  done
#done
