set -e
export RDMA_VERBOSITY=ERROR
export HELLOWORLD_HOME=/home/geng.161/.cache/clion/tmp/tmp.wFl8U4DvNn/examples/cpp/helloworld/cmake-build-release-ri2-head

hostfile_template="hosts"
hostfile_template=$(realpath "$hostfile_template")
if [[ ! -f "$hostfile_template" ]]; then
  echo "Bad hostfile $hostfile_template"
  exit 1
fi

POLL_NUMS=(1)
GRPC_MODES=(TCP RDMA_BP RDMA_EVENT)
GRPC_MODES=(RDMA_BP RDMA_BPEV RDMA_EVENT)
GRPC_MODES=(RDMA_BPEV)

function set_hostfile() {
  n_clients=$1
  name_prefix=$(basename "$hostfile_template")
  hostfile="/tmp/$name_prefix"
  ./gen_hostfile.py ./hosts "$n_clients" >"$hostfile"
  export HOSTS_PATH="$hostfile"
}

function get_batch_size() {
  data_size=$1
  if [[ $data_size -le 32*1024 ]]; then
    batch_size=100000
  elif [[ $data_size -le 64*1024 ]]; then
    batch_size=80000
  elif [[ $data_size -le 128*1024 ]]; then
    batch_size=60000
  elif [[ $data_size -le 256*1024 ]]; then
    batch_size=40000
  elif [[ $data_size -le 512*1024 ]]; then
    batch_size=20000
  elif [[ $data_size -le 1024*1024 ]]; then
    batch_size=10000
  elif [[ $data_size -le 2*1024*1024 ]]; then
    batch_size=5000
  else
    batch_size=2500
  fi
  echo $batch_size
}

function profile() {
  server_thread=28
  REQs=(64)
  RESPs=(64)
  for n_client in $(seq 1 1 28); do
    set_hostfile "$n_client"
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
  done
}

function client_scalability() {
  REQs=(64)
  RESPs=(64)
  N_CLIENTS=(1 2 4 8 16 32 64 128)
  N_CLIENTS=(128)

  for i in "${!REQs[@]}"; do
    REQ=${REQs[i]}
    RESP=${RESPs[i]}
    for poll_num in "${POLL_NUMS[@]}"; do
      for grpc_mode in "${GRPC_MODES[@]}"; do
        export GRPC_PLATFORM_TYPE=$grpc_mode
        for j in "${!N_CLIENTS[@]}"; do
          n_cli="${N_CLIENTS[j]}"
          set_hostfile "$n_cli"
          export LOG_SUFFIX="${grpc_mode}_${REQ}_${RESP}"
          thread=28
          bp_to=0
          if [[ n_cli -le 32 ]]; then
            if [[ "${grpc_mode}" == "RDMA_BPEV" ]]; then
              thread=27
            else
              thread=28
            fi
            bp_to=50
          elif [[ n_cli -le 64 ]]; then
            thread=32
          else
            thread=64 # for 128 client
          fi

          ./run.sh --server_thread=$thread \
            --client_thread=1 \
            --req="$REQ" \
            --resp="$RESP" \
            --poll-num="$poll_num" \
            --batch=1000000 --bp-timeout=$bp_to --overwrite
        done
      done
    done
  done
}

function thread_scalability() {
  REQs=(64 1024 4096 65536 131072)
  RESPs=(64 1024 4096 65536 131072)
  REQs=(64)
  RESPs=(64)
  n_clients=28
  set_hostfile $n_clients

  for i in "${!REQs[@]}"; do
    REQ=${REQs[i]}
    RESP=${RESPs[i]}
    for grp_mode in "${GRPC_MODES[@]}"; do
      export GRPC_PLATFORM_TYPE=$grp_mode
      for n_threads in $(seq 16 1 28); do
        export LOG_SUFFIX="${grp_mode}_${REQ}_${RESP}_th_${n_threads}"
        ./run.sh --server_thread=$n_threads --client_thread=1 --req="$REQ" --resp="$RESP" --poll-num=1 --batch=100000
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
  --client-scalability)
    client_scalability
    shift
    ;;
  --thread-scalability)
    thread_scalability
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
