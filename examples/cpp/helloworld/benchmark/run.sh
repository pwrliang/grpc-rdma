#!/usr/bin/env bash
set -e
SCRIPT_DIR=$(realpath "$(dirname "$0")")

if [[ -z "$HOSTS_PATH" ]]; then
  echo "Using default $HOSTS_PATH"
  HOSTS_PATH="$SCRIPT_DIR/hosts"
fi
SERVER=$(head -n 1 "$HOSTS_PATH")
NP=$(awk '{ sum += $1 } END { print sum }' <(tail -n +2 "$HOSTS_PATH" |cut -d"=" -f2,2))
SERVER_THREADS=$(nproc)
CLIENT_THREADS=1
SERVER_PROGRAM="greeter_async_server2"
BATCH_SIZE=10000

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
    -*|--*)
      echo "Unknown option $i"
      exit 1
      ;;
    *)
      ;;
  esac
done


LOG_PATH=$(realpath "$SCRIPT_DIR/logs/logs_$NP")

if [[ -n "$LOG_SUFFIX" ]]; then
  LOG_PATH="${LOG_PATH}_${LOG_SUFFIX}"
fi
mkdir -p "$LOG_PATH"
export RDMA_VERBOSITY=ERROR

function start_server() {
  mpirun --bind-to none -x GRPC_PLATFORM_TYPE -x RDMA_VERBOSITY \
         -n 1 -host "$SERVER" \
         "$HELLOWORLD_HOME"/$SERVER_PROGRAM -threads="$SERVER_THREADS" &
}

function kill_server() {
  ssh "$SERVER" 'ps aux|pgrep greeter|xargs kill 2>/dev/null || true'
}

WORKLOADS="greeter_async_client greeter_async_client2"
#WORKLOADS="greeter_async_client2"
for workload in ${WORKLOADS}; do
  curr_log_path="$LOG_PATH/${workload}.log"
  if [[ -f "$curr_log_path" ]]; then
      echo "$curr_log_path exists, skip"
    else
      kill_server
      while true; do
        start_server
        tmp_host="/tmp/clients.$RANDOM"
        if [[ -f $tmp_host ]]; then
          tmp_host="/tmp/clients.$RANDOM"
        fi
        tail -n +2 "$HOSTS_PATH" > "$tmp_host"
        echo "============================= Running $workload with $NP clients"
        # Evaluate
        mpirun --bind-to none -x GRPC_PLATFORM_TYPE -x RDMA_VERBOSITY \
            --oversubscribe \
            -mca btl_tcp_if_include ib0 \
            -np $NP -hostfile "$tmp_host" \
            "$HELLOWORLD_HOME/$workload" \
            -host "$SERVER" \
            -threads "$CLIENT_THREADS" \
            -batch "$BATCH_SIZE" | tee "${curr_log_path}.tmp" 2>&1
        kill_server
        sleep 2

        row_count=$(grep -c "Throughput" < "${curr_log_path}.tmp")
        if [[ $row_count != "1" ]]; then
          echo "Found error when evaluate workload $workload, retrying"
        else
          mv "${curr_log_path}.tmp" "${curr_log_path}"
          break
        fi
      done
    fi
done
