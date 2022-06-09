#!/usr/bin/env bash
#set -e
SCRIPT_DIR=$(realpath "$(dirname "$0")")

if [[ -z "$HOSTS_PATH" ]]; then
  echo "Using default $HOSTS_PATH"
  HOSTS_PATH="$SCRIPT_DIR/hosts"
fi
SERVER=$(head -n 1 "$HOSTS_PATH")
NP=$(awk '{ sum += $1 } END { print sum }' <(tail -n +2 "$HOSTS_PATH" | cut -d"=" -f2,2))
SERVER_THREADS=$(nproc)
CLIENT_THREADS=1
SERVER_PROGRAM="greeter_async_server2"
BATCH_SIZE=10000
REQ_SIZE=64
RESP_SIZE=4096
N_REPEAT=1
POLL_NUM=100
PROFILING=""
AFFINITY="false"
OVERWRITE=0

if [[ ! -f "$HELLOWORLD_HOME/$SERVER_PROGRAM" ]]; then
  echo "Invalid HELLOWORLD_HOME"
  exit 1
fi

for i in "$@"; do
  case $i in
  --server_thread=*)
    SERVER_THREADS="${i#*=}"
    shift
    ;;
  --client_thread=*)
    CLIENT_THREADS="${i#*=}"
    shift
    ;;
  --batch=*)
    BATCH_SIZE="${i#*=}"
    shift
    ;;
  --req=*)
    REQ_SIZE="${i#*=}"
    shift
    ;;
  --resp=*)
    RESP_SIZE="${i#*=}"
    shift
    ;;
  --poll-num=*)
    POLL_NUM="${i#*=}"
    shift
    ;;
  --profiling=*)
    PROFILING="${i#*=}"
    N_REPEAT=1
    export GRPC_PROFILING=$PROFILING
    shift
    ;;
  --executor=*)
    EXECUTOR="${i#*=}"
    export GRPC_EXECUTOR=$EXECUTOR
    shift
    ;;
  --sleep=*)
    SLEEP="${i#*=}"
    export GRPC_SLEEP=$SLEEP
    shift
    ;;
  --bp-timeout=*)
    export GRPC_BP_TIMEOUT="${i#*=}"
    shift
    ;;
  --max-poller=*)
    export GRPC_RDMA_MAX_POLLER="${i#*=}"
    shift
    ;;
  --affinity)
    AFFINITY="true"
    shift
    ;;
  --overwrite)
    OVERWRITE=1
    shift
    ;;
  --* | -*)
    echo "Unknown option $i"
    exit 1
    ;;
  *) ;;

  esac
done

LOG_PATH=$(realpath "$SCRIPT_DIR/logs/")

if [[ -n $PROFILING ]]; then
  LOG_PATH="$LOG_PATH/profile_cli_$NP"
else
  LOG_PATH="$LOG_PATH/bench_cli_$NP"
fi

if [[ -n "$LOG_SUFFIX" ]]; then
  LOG_PATH="${LOG_PATH}_${LOG_SUFFIX}"
fi
mkdir -p "$LOG_PATH"
export RDMA_VERBOSITY=ERROR

function start_server() {
  mpirun --bind-to none -x GRPC_PLATFORM_TYPE -x RDMA_VERBOSITY -x GRPC_PROFILING -x GRPC_SLEEP -x GRPC_EXECUTOR -x GRPC_BP_TIMEOUT -x GRPC_RDMA_MAX_POLLER \
    -n 1 -host "$SERVER" \
    "$HELLOWORLD_HOME"/$SERVER_PROGRAM \
    -threads="$SERVER_THREADS" \
    -cqs="$SERVER_THREADS" \
    -resp "$RESP_SIZE" -affinity="$AFFINITY" |& tee "$1" &
}

function kill_server() {
  ssh "$SERVER" "ps aux| pgrep greeter |xargs kill 2>/dev/null || true"
  sleep 3
}

WORKLOADS="greeter_async_client greeter_async_client2"
WORKLOADS="greeter_async_client2"
for workload in ${WORKLOADS}; do
  curr_log_path="$LOG_PATH/${workload}.log"
  if [[ $OVERWRITE -eq 1 ]]; then
    rm -f "$curr_log_path"
  fi
  if [[ -f "$curr_log_path" ]]; then
    echo "$curr_log_path exists, skip"
  else
    kill_server
    rm -f "${curr_log_path}.tmp"
    while true; do
      tmp_host="/tmp/clients.$RANDOM"
      tail -n +2 "$HOSTS_PATH" >"$tmp_host"
      echo "============================= Running $workload with $NP clients, server threads: $SERVER_THREADS, req: $REQ_SIZE, resp: $RESP_SIZE batch: $BATCH_SIZE poll_num: $POLL_NUM"
      start_server "$LOG_PATH/server_${workload}.log"
      # Evaluate
      mpirun --bind-to none -x GRPC_PLATFORM_TYPE -x RDMA_VERBOSITY -x GRPC_PROFILING -x GRPC_BP_TIMEOUT \
        --oversubscribe \
        -mca btl_tcp_if_include ib0 \
        -np "$NP" -hostfile "$tmp_host" \
        "$HELLOWORLD_HOME/$workload" \
        -host "$SERVER" \
        -threads "$CLIENT_THREADS" \
        -cqs "$CLIENT_THREADS" \
        -batch "$BATCH_SIZE" \
        -poll_num "$POLL_NUM" \
        -req "$REQ_SIZE" | tee -a "${curr_log_path}.tmp" 2>&1
      if [[ $PROFILING != "" ]]; then
        ssh "$SERVER" "ps aux | pgrep greeter | xargs kill -SIGUSR1 2>/dev/null || true"
        sleep 1
      fi
      kill_server
      rm -f core.*

      row_count=$(grep -c "Throughput" <"${curr_log_path}.tmp")
      if [[ $row_count == "$N_REPEAT" ]]; then
        mv "${curr_log_path}.tmp" "${curr_log_path}"
        break
      fi
    done
  fi
done
