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
DIR=""
AFFINITY=false
SERVER_TIMEOUT=0
CLIENT_TIMEOUT=0
SEND_INTERVAL=0
OVERWRITE=false

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
  --direction=*)
    DIR="${i#*=}"
    shift
    ;;
  --affinity)
    AFFINITY=true
    shift
    ;;
  --overwrite)
    OVERWRITE=true
    shift
    ;;
  --server-timeout=*)
    SERVER_TIMEOUT="${i#*=}"
    shift
    ;;
  --client-timeout=*)
    CLIENT_TIMEOUT="${i#*=}"
    shift
    ;;
  --send-interval=*)
    SEND_INTERVAL="${i#*=}"
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

curr_log_path="$LOG_PATH/${MODE}_client_${NP}_poll_${POLLING_THREADS}_interval_${SEND_INTERVAL}_dir_${DIR}.log"
if [[ $OVERWRITE ]]; then
  rm -f "$curr_log_path"
fi

if [[ -f "$curr_log_path" ]]; then
  echo "$curr_log_path exists, skip"
else
  rm -f "${curr_log_path}.tmp"
  echo "Running $MODE with $NP clients, max polling threads: ${POLLING_THREADS}, computing thread: ${COMPUTING_THREAD}, batch size: $BATCH_SIZE Direction: ${DIR}"
  # Evaluate
  while true; do
    mpirun --bind-to none \
      --oversubscribe -quiet \
      -mca btl_tcp_if_include ib0 \
      -hostfile "$HOSTS_PATH" \
      "$MB_PATH" \
      -mode="$MODE" \
      -dir="$DIR" \
      -polling_thread="$POLLING_THREADS" \
      -computing_thread="$COMPUTING_THREAD" \
      -batch "$BATCH_SIZE" \
      -affinity "$AFFINITY" \
      -server_timeout "$SERVER_TIMEOUT" \
      -client_timeout "$CLIENT_TIMEOUT" \
      -send_interval "$SEND_INTERVAL" |& tee -a "${curr_log_path}.tmp"
    row_count=$(grep -c "Rank:" <"${curr_log_path}.tmp")
    if [[ $row_count -eq "$NP" ]]; then
      mv "${curr_log_path}.tmp" "${curr_log_path}"
      break
    fi
    sleep 5
  done

fi
