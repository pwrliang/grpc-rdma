#!/usr/bin/env bash
set -e
SCRIPT_DIR=$(realpath "$(dirname "$0")")

if [[ -z "$HOSTS_PATH" ]]; then
  echo "Using default $HOSTS_PATH"
  HOSTS_PATH="$SCRIPT_DIR/hosts"
fi
NP=$(awk '{ sum += $1 } END { print sum }' <(tail -n +2 "$HOSTS_PATH" | cut -d"=" -f2,2))
POLLING_THREADS=$(nproc)
MB_PATH="$MB_HOME"/mb
BATCH_SIZE=10000
MODE=""

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
  --mode=*)
    MODE="${i#*=}"
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

curr_log_path="$LOG_PATH/${MODE}_client_${NP}.log"

#rm -f $curr_log_path
if [[ -f "$curr_log_path" ]]; then
  echo "$curr_log_path exists, skip"
else
  rm -f "${curr_log_path}.tmp"
  echo "============================= Running $MODE with $NP clients, max polling threads: ${POLLING_THREADS}, batch size: $BATCH_SIZE"
  # Evaluate
  mpirun --bind-to none \
    --oversubscribe \
    -mca btl_tcp_if_include ib0 \
    -hostfile "$HOSTS_PATH" \
    "$MB_PATH" \
    -mode="$MODE" \
    -polling_thread="$POLLING_THREADS" \
    -batch "$BATCH_SIZE" |& tee -a "${curr_log_path}.tmp"
    if [[ $? -eq 0 ]]; then
      mv "${curr_log_path}.tmp" "${curr_log_path}"
    fi
fi