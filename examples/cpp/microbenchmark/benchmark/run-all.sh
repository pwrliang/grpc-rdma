set -e
export MB_HOME=/home/geng.161/.cache/clion/tmp/tmp.uJwkbosFK5/examples/cpp/microbenchmark/cmake-build-release-ri2-head
SERVER_CPU_CORES=28
hostfile_template="hosts"
hostfile_template=$(realpath "$hostfile_template")
if [[ ! -f "$hostfile_template" ]]; then
  echo "Bad hostfile $hostfile_template"
  exit 1
fi

MODES=(event)

function set_hostfile() {
  n_clients=$1
  name_prefix=$(basename "$hostfile_template")
  hostfile="/tmp/$name_prefix"
  ./gen_hostfile.py ./hosts "$n_clients" >"$hostfile"
  export HOSTS_PATH="$hostfile"
}

function client_scalability() {
  MODES=(bp bprr event)
  dir=bi
  for mode in "${MODES[@]}"; do
    for interval in 0 50 200; do
      for n_clients in 1 2 4 8 16 $SERVER_CPU_CORES 32 64 128; do
        set_hostfile $n_clients
        th=$SERVER_CPU_CORES
        if [[ $mode == "bpev" ]]; then
          th=24
        elif [[ $mode == "bprr" ]]; then
          th=$SERVER_CPU_CORES
        elif [[ $mode == "event" ]]; then
          th=$SERVER_CPU_CORES
        fi
        # Enable affinity when the number of clients is less than CPU cores.
        if [[ $n_clients -lt $SERVER_CPU_CORES ]]; then
          ./run.sh --polling-thread=$th \
            --mode="${mode}" \
            --direction="${dir}" \
            --batch=200000 \
            --server-timeout=-1 \
            --client-timeout=-1 \
            --send-interval=$interval \
            --affinity
        else
          ./run.sh --polling-thread=$th \
            --mode="${mode}" \
            --direction="${dir}" \
            --batch=200000 \
            --server-timeout=-1 \
            --client-timeout=-1 \
            --send-interval=$interval
        fi
      done
    done
  done
}

function thread_scalability() {
  n_clients=64
  MODES=(bprr event)
  set_hostfile $n_clients
  for mode in "${MODES[@]}"; do
    for n_threads in 1 2 4 8 16 28 32 64; do
      ./run.sh --polling-thread=$n_threads --mode="${mode}" --batch=1000000
    done
  done
}

function epoll_thread_scalability() {
  SCRIPT_DIR=$(realpath "$(dirname "$0")")
  LOG_PREFIX=$(realpath "$SCRIPT_DIR/logs")

  for n_threads in 1 2 4 8 16 28 32 64 128; do
    LOG_PATH="$LOG_PREFIX/epoll_thread_${n_threads}_r.log"
    "$MB_HOME"/epoll_bench -nclient $n_threads -runtime 8 -client_timeout -1 -max_worker 999 |& tee "$LOG_PATH"
    LOG_PATH="$LOG_PREFIX/epoll_thread_${n_threads}_rw.log"
    "$MB_HOME"/epoll_bench -nclient $n_threads -runtime 8 -rw -client_timeout -1 -server_timeout -1 -max_worker 999 |& tee "$LOG_PATH"
  done
}

function epoll_thread_scalability_one_worker() {
  SCRIPT_DIR=$(realpath "$(dirname "$0")")
  LOG_PREFIX=$(realpath "$SCRIPT_DIR/logs")

  for n_threads in 1 2 4 8 16 28 32 64 128 256; do
    LOG_PATH="$LOG_PREFIX/epoll_1_worker_thread_${n_threads}_r.log"
    if [[ -f "$LOG_PATH" ]]; then
      echo "skip $LOG_PATH"
    else
      "$MB_HOME"/epoll_bench -nclient $n_threads -runtime 8 -max_worker 1 -client_timeout -1 |& tee "$LOG_PATH"
    fi
    LOG_PATH="$LOG_PREFIX/epoll_1_worker_thread_${n_threads}_rw.log"
    if [[ -f "$LOG_PATH" ]]; then
      echo "skip $LOG_PATH"
    else
      "$MB_HOME"/epoll_bench -nclient $n_threads -runtime 8 -rw -max_worker 1 -client_timeout -1 |& tee "$LOG_PATH"
    fi
  done
}

for i in "$@"; do
  case $i in
  --client-scalability)
    client_scalability
    shift
    ;;
  --thread-scalability)
    thread_scalability
    shift
    ;;
  --epoll-thread-scalability)
    epoll_thread_scalability
    shift
    ;;
  --epoll-thread-scalability-one-worker)
    epoll_thread_scalability_one_worker
    shift
    ;;
  --* | -*)
    echo "Unknown option $i"
    exit 1
    ;;
  *)
    echo "???"
    exit 1
    ;;
  esac
done
