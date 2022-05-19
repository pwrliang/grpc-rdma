set -e
export MB_HOME=/home/geng.161/.cache/clion/tmp/tmp.wFl8U4DvNn/examples/cpp/microbenchmark/cmake-build-release-ri2-head

hostfile_template="hosts"
hostfile_template=$(realpath "$hostfile_template")
if [[ ! -f "$hostfile_template" ]]; then
  echo "Bad hostfile $hostfile_template"
  exit 1
fi

MODES=(bp bprr event)

function set_hostfile() {
  n_clients=$1
  name_prefix=$(basename "$hostfile_template")
  hostfile="/tmp/$name_prefix"
  ./gen_hostfile.py ./hosts "$n_clients" >"$hostfile"
  export HOSTS_PATH="$hostfile"
}

function client_scalability() {
  max_worker=-1
  if [[ $max_worker -ne -1 ]]; then
    export LOG_SUFFIX="${max_worker}_worker"
  fi
  for mode in "${MODES[@]}"; do
#    for dir in s2c c2s bi; do
      dir=bi
      for n_clients in 1 2 4 8 16 28 32 64 128 256; do
        set_hostfile $n_clients
        ./run.sh --polling-thread=28 \
          --mode="${mode}" \
          --direction="${dir}" \
          --batch=200000 \
          --server-timeout=-1 \
          --client-timeout=-1 \
          --affinity --overwrite --send-interval=100
      done
#    done
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
