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
CQS=$SERVER_THREADS
SERVER_PROGRAM="mb_server"
N_RPCS=10000000
STREAMING="false"
DURATION=10
CONCURRENT=1
REQ_SIZE=64
RESP_SIZE=4096
DELAY=0
RANDOM_DELAY="false"
NUMA="false"
PROFILING="false"
OVERWRITE=0
N_WARMUP=10000

for i in "$@"; do
  case $i in
  --server-thread=*)
    SERVER_THREADS="${i#*=}"
    shift
    ;;
  --cqs=*)
    CQS="${i#*=}"
    shift
    ;;
  --rpcs=*)
    N_RPCS="${i#*=}"
    shift
    ;;
  --streaming=*)
    STREAMING="${i#*=}"
    shift
    ;;
  --duration=*)
    DURATION="${i#*=}"
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
  --delay=*)
    DELAY="${i#*=}"
    shift
    ;;
  --random-delay=*)
    RANDOM_DELAY="${i#*=}"
    shift
    ;;
  --concurrent=*)
    CONCURRENT="${i#*=}"
    shift
    ;;
  --polling-timeout=*)
    export GRPC_RDMA_BUSY_POLLING_TIMEOUT_US="${i#*=}"
    shift
    ;;
  --polling-thread=*)
    export GRPC_RDMA_POLLER_THREAD_NUM="${i#*=}"
    shift
    ;;
  --ring-buffer-kb=*)
    export GRPC_RDMA_RING_BUFFER_SIZE_KB="${i#*=}"
    shift
    ;;
  --numa=*)
    NUMA="${i#*=}"
    shift
    ;;
  --warmup=*)
    N_WARMUP="${i#*=}"
    shift
    ;;
  --profiling=*)
    PROFILING="${i#*=}"
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

function kill_server() {
   ssh "$SERVER" "pkill -9 pidstat"
  # Signal to print profiling results
  ssh "$SERVER" 'pgrep mb_server | xargs kill -USR1 2>/dev/null'
  # Kill Server
  ssh "$SERVER" 'pgrep mb_server | xargs kill -9 2>/dev/null && while [[ $(ps aux | pgrep mb_server) ]]; do sleep 1; done || true'
}

function start_server() {
  kill_server
  echo "Start server on node $SERVER"

  # Generate head
  pidstat  -r -u -w -h 1 1 | grep '#' > "${server_stat_log_path}"
  ssh "${SERVER}" "nohup sh -c 'pidstat  -r -u -w -h 1 | grep --line-buffered mb_server' >>${server_stat_log_path} 2>/dev/null &"
  mpirun --bind-to none -q \
    -x GRPC_ENABLE_RDMA_SUPPORT \
    -x GRPC_RDMA_BUSY_POLLING_TIMEOUT_US \
    -x GRPC_RDMA_POLLER_THREAD_NUM \
    -x GRPC_RDMA_RING_BUFFER_SIZE_KB \
    -x GRPC_TRACE \
    -n 1 -host "$SERVER" \
    "$MB_HOME"/$SERVER_PROGRAM \
    -cqs=$CQS \
    -streaming=$STREAMING \
    -threads=$SERVER_THREADS \
    -resp=$RESP_SIZE \
    -numa=$NUMA \
    -profiling=$PROFILING |& tee "$1" &
}

if [[ ! -f "$MB_HOME/$SERVER_PROGRAM" ]]; then
  echo "Invalid MB_HOME"
  exit 1
fi

LOG_PREFIX=$(realpath "$SCRIPT_DIR/logs/")
mkdir -p "$LOG_PREFIX"

server_log_path="${LOG_PREFIX}/server_${LOG_NAME}.log"
server_stat_log_path="${LOG_PREFIX}/server_stat_${LOG_NAME}.log"
cli_log_path="${LOG_PREFIX}/client_${LOG_NAME}.log"

if [[ $OVERWRITE -eq 1 ]]; then
  rm -f "$cli_log_path"
fi

if [[ -f "$cli_log_path" ]]; then
  echo "$cli_log_path exists, skip"
else
  rm -f "${cli_log_path}.tmp"
  while true; do
    tmp_host="/tmp/${USER}_clients"
    tail -n +2 "$HOSTS_PATH" >"$tmp_host"
    echo "============================= Running RDMA_ENABLE: $GRPC_ENABLE_RDMA_SUPPORT, clients: $NP, server threads: $SERVER_THREADS, req: $REQ_SIZE, resp: $RESP_SIZE rpcs: $N_RPCS duration: $DURATION"
    start_server "$server_log_path"

    # Evaluate
    cmd="mpirun --bind-to none \
      -x GRPC_ENABLE_RDMA_SUPPORT \
      -x GRPC_RDMA_RING_BUFFER_SIZE_KB \
      -x GRPC_RDMA_BUSY_POLLING_TIMEOUT_US \
      -x GRPC_TRACE \
      --oversubscribe \
      -hostfile $tmp_host \
      $MB_HOME/mb_client \
      -target=${SERVER}:50051 \
      -streaming=$STREAMING \
      -req=$REQ_SIZE \
      -warmup=$N_WARMUP \
      -rpcs=$N_RPCS \
      -concurrent=$CONCURRENT \
      -duration=$DURATION \
      -delay=$DELAY \
      -report_interval=1 \
      -random_delay=$RANDOM_DELAY"

    echo "$cmd" >"${cli_log_path}.tmp"
    eval "$cmd" 2>&1 | tee -a "${cli_log_path}.tmp"
    kill_server

    row_count=$(grep -c "Aggregated" <"${cli_log_path}.tmp")
    if [[ $row_count -eq 1 ]]; then
      mv "${cli_log_path}.tmp" "${cli_log_path}"
      break
    fi
    break
  done
fi

unset GRPC_ENABLE_RDMA_SUPPORT
unset GRPC_RDMA_BUSY_POLLING_TIMEOUT_US
unset GRPC_RDMA_POLLING_THREAD
