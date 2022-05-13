#!/usr/bin/env bash
SCRIPT_DIR=$(realpath "$(dirname "$0")")

if [[ -z "$HOSTS_PATH" ]]; then
  echo "Using default $HOSTS_PATH"
  HOSTS_PATH="$SCRIPT_DIR/hosts"
fi
NP=$(awk '{ sum += $1 } END { print sum }' <(tail -n +2 "$HOSTS_PATH" | cut -d"=" -f2,2))
POLLING_THREADS=$(nproc)
MB_PATH="$MB_HOME"/mb
COMPUTING_THREAD=0
BATCH_SIZE=10000
MODE=""
RW=false
MAX_WORKER=-1

if [[ ! -f "$MB_PATH" ]]; then
  echo "Invalid MB_HOME"
  exit 1
fi

for i in "$@"; do
  case $i in
  --batch=*)
    BATCH_SIZE="${i#*=}"
    shift
    ;;
  --polling-thread=*)
    POLLING_THREADS="${i#*=}"
    shift
    ;;
  --computing-thread=*)
    COMPUTING_THREAD="${i#*=}"
    shift
    ;;
  --mode=*)
    MODE="${i#*=}"
    shift
    ;;
  --read-write)
    RW=true
    shift
    ;;
  --max-worker=*)
    MAX_WORKER="${i#*=}"
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

if [[ -n "$LOG_SUFFIX" ]]; then
  LOG_PATH="${LOG_PATH}_${LOG_SUFFIX}"
fi
mkdir -p "$LOG_PATH"

curr_log_path="$LOG_PATH/${MODE}_client_${NP}_poll_${POLLING_THREADS}_rw_${RW}.log"

#rm -f $curr_log_path
if [[ -f "$curr_log_path" ]]; then
  echo "$curr_log_path exists, skip"
else
  rm -f "${curr_log_path}.tmp"
  echo "============================= Running $MODE with $NP clients, max polling threads: ${POLLING_THREADS}, computing thread: ${COMPUTING_THREAD}, batch size: $BATCH_SIZE"
  # Evaluate
  while true; do
    mpirun --bind-to none \
      --oversubscribe -quiet \
      -mca btl_tcp_if_include ib0 \
      -hostfile "$HOSTS_PATH" \
      "$MB_PATH" \
      -mode="$MODE" \
      -rw=$RW \
      -polling_thread="$POLLING_THREADS" \
      -computing_thread="$COMPUTING_THREAD" \
      -batch "$BATCH_SIZE" \
      -max_worker "$MAX_WORKER" |& tee -a "${curr_log_path}.tmp"
    row_count=$(grep -c "Latency" <"${curr_log_path}.tmp")
    if [[ ($MAX_WORKER -ne -1 && $row_count -eq $MAX_WORKER) || ($MAX_WORKER -eq -1 && $row_count -eq "$NP") ]]; then
      mv "${curr_log_path}.tmp" "${curr_log_path}"
      break
    fi
    sleep 5
  done

fi
