set -e
export RDMA_VERBOSITY=ERROR
export HELLOWORLD_HOME=/home/geng.161/.cache/clion/tmp/tmp.uJwkbosFK5/examples/cpp/helloworld/cmake-build-release-ri2-head

hostfile_template="hosts"
hostfile_template=$(realpath "$hostfile_template")
if [[ ! -f "$hostfile_template" ]]; then
  echo "Bad hostfile $hostfile_template"
  exit 1
fi

GRPC_MODES=(TCP RDMA_BP RDMA_EVENT)
GRPC_MODES=(TCP RDMA_EVENT RDMA_BP RDMA_BPEV)

function set_hostfile() {
  n_clients=$1
  name_prefix=$(basename "$hostfile_template")
  hostfile="/tmp/grpc_mb_$name_prefix"
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

function varying_data_size() {
  server_thread=1
  REQs=(128 256 512 1024 16384 32768 65536 131072 524288 1048576 2097152 4194304)
  RESPs=(128 256 512 1024 16384 32768 65536 131072 524288 1048576 2097152 4194304)
  n_client=1
  set_hostfile "$n_client"
  GRPC_MODES=(RDMA_BPEV)

  for grp_mode in "${GRPC_MODES[@]}"; do
    for i in "${!REQs[@]}"; do
      REQ=${REQs[i]}
      RESP=${RESPs[i]}
      bp_timeout=0
      if [[ $REQ -le 1024 ]]; then
        batch_size=200000
        bp_timeout=50
      elif [[ $REQ -le 131072 ]]; then
        batch_size=100000
        bp_timeout=500
      elif [[ $REQ -le 4194304 ]]; then
        bp_timeout=500
        batch_size=50000
      fi

      export GRPC_PLATFORM_TYPE=$grp_mode
      export LOG_SUFFIX="${grp_mode}_${REQ}_${RESP}"
      ./run.sh --server_thread=$server_thread \
        --client_thread=1 \
        --req="$REQ" \
        --resp="$RESP" \
        --bp-timeout=$bp_timeout \
        --batch=$batch_size --warmup=10000 --overwrite
    done
  done
}

function varying_data_size_zero_cp() {
  server_thread=1
  REQs=(131072 262144) #524288 1048576 2097152 4194304
  RESPs=(131072 262144)
  GRPC_MODES=(RDMA_BP RDMA_BPEV)
  n_client=1
  set_hostfile "$n_client"

  for grp_mode in "${GRPC_MODES[@]}"; do
    for i in "${!REQs[@]}"; do
      REQ=${REQs[i]}
      RESP=${RESPs[i]}
      bp_timeout=0
      if [[ $REQ -le 1048576 ]]; then
        batch_size=200000
        bp_timeout=5000
        n_warmup=100000
      elif [[ $REQ -le 4194304 ]]; then
        bp_timeout=5000
        batch_size=50000
        n_warmup=1000
      fi

      export GRPC_PLATFORM_TYPE=$grp_mode

      for enable_zp in true false; do
        export GRPC_RDMA_ZEROCOPY_ENABLE="$enable_zp"
        export LOG_SUFFIX="${grp_mode}_${REQ}_${RESP}_zerocp_${enable_zp}"

        ./run.sh --server_thread=$server_thread \
          --client_thread=1 \
          --req="$REQ" \
          --resp="$RESP" \
          --bp-timeout=$bp_timeout \
          --batch=$batch_size \
          --warmup=$n_warmup --overwrite
      done
    done
  done
}

function throughput() {
  REQs=(128 256 512 1024 16384 32768 65536 131072 524288 1048576 2097152 4194304)
  RESPs=(128 256 512 1024 16384 32768 65536 131072 524288 1048576 2097152 4194304)
  server_thread=32
  n_client=64
  set_hostfile "$n_client"

  for grp_mode in "${GRPC_MODES[@]}"; do
    for i in "${!REQs[@]}"; do
      REQ=${REQs[i]}
      RESP=${RESPs[i]}
      bp_timeout=0
      if [[ $REQ -le 1024 ]]; then
        batch_size=200000
      elif [[ $REQ -le 131072 ]]; then
        batch_size=100000
      elif [[ $REQ -le 4194304 ]]; then
        batch_size=20000
      fi

      export GRPC_PLATFORM_TYPE=$grp_mode
      export LOG_SUFFIX="${grp_mode}_${REQ}_${RESP}"
      ./run.sh --server_thread=$server_thread \
        --client_thread=1 \
        --req="$REQ" \
        --resp="$RESP" \
        --bp-timeout=$bp_timeout \
        --batch=$batch_size
    done
  done
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
      for grp_mode in "${GRPC_MODES[@]}"; do
        export GRPC_PLATFORM_TYPE=$grp_mode
        export LOG_SUFFIX="${grp_mode}_${REQ}_${RESP}_${poll_num}"
        ./run.sh --server_thread=$server_thread \
          --client_thread=1 \
          --profiling=milli \
          --req="$REQ" \
          --resp="$RESP" \
          --batch=100000 --bp-timeout=50
      done
    done
  done
}

function client_scalability() {
  REQs=(1024 2048 4096 8192 16384 32768)
  RESPs=(1024 2048 4096 8192 16384 32768)
  N_CLIENTS=(1 2 4 8 16 32 64 128)

  GRPC_MODES=(TCP RDMA_EVENT RDMA_BP RDMA_BPEV)
  REQs=(64 128)
  RESPs=(64 128)

  for i in "${!REQs[@]}"; do
    REQ=${REQs[i]}
    RESP=${RESPs[i]}
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
          --batch=500000 --bp-timeout=$bp_to # --profiling=milli
      done
    done
  done
}

function mb() {
  N_CLIENTS=(1 2 4 8 16 32 64)
  N_CLIENTS=(32)
  GRPC_MODES=(TCP RDMA_EVENT RDMA_BPNV RDMA_BP RDMA_BPEV)
  GRPC_MODES=(RDMA_BP)
  REQs=(64)
  RESPs=(64)

  for i in "${!REQs[@]}"; do
    REQ=${REQs[i]}
    RESP=${RESPs[i]}
    for delay in 0; do
      for grpc_mode in "${GRPC_MODES[@]}"; do
        for j in "${!N_CLIENTS[@]}"; do
          n_cli="${N_CLIENTS[j]}"
          set_hostfile "$n_cli"
          export LOG_SUFFIX="${grpc_mode}_${REQ}_${RESP}_delay_${delay}"
          thread=28
          affinity="false"
          if [[ $n_cli -lt 32 ]]; then
            if [[ "${grpc_mode}" == "RDMA_BPEV" ]]; then
              thread=27 # Only use 27 thread as we leave one core for busy polling
            else
              thread=28
            fi
            affinity="true"
          elif [[ $n_cli -le 32 ]]; then
            thread=32
          elif [[ $n_cli -le 64 ]]; then
            thread=32
          else
            thread=64 # for 128 client
          fi

          if [[ $delay -eq 0 ]]; then
            bp_to=50
          else
            bp_to=0
          fi

          if [[ "$grpc_mode" == "RDMA_BPNV" ]]; then
            export GRPC_PLATFORM_TYPE="RDMA_BP"
            export GRPC_BP_YIELD="false"
            thread=$n_cli
          else
            export GRPC_PLATFORM_TYPE=$grpc_mode
            export GRPC_BP_YIELD="true"
          fi

          batch=500000
          if [[ $delay -gt 0 ]]; then
            batch=200000
          fi

          ./run.sh --server_thread="$thread" \
            --client_thread=1 \
            --affinity="$affinity" \
            --req="$REQ" \
            --resp="$RESP" \
            --send-interval="$delay" \
            --batch=$batch \
            --bp-timeout=$bp_to --overwrite # --profiling=milli
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
        ./run.sh --server_thread=$n_threads --client_thread=1 --req="$REQ" --resp="$RESP" --batch=100000
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
  --micro-benchmark)
    mb
    shift
    ;;
  --thread-scalability)
    thread_scalability
    shift
    ;;
  --varying-data-size)
    varying_data_size
    shift
    ;;
  --varying-data-size-zerocp)
    varying_data_size_zero_cp
    shift
    ;;
  --throughput)
    throughput
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
