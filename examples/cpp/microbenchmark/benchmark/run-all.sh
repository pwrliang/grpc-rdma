set -e
export MB_HOME=/home/geng.161/.cache/clion/tmp/tmp.wFl8U4DvNn/examples/cpp/microbenchmark/cmake-build-release-ri2-head

hostfile_template="hosts"
hostfile_template=$(realpath "$hostfile_template")
if [[ ! -f "$hostfile_template" ]]; then
  echo "Bad hostfile $hostfile_template"
  exit 1
fi

MODES=(bp bprr event)
#MODES=(event)

function set_hostfile() {
  n_clients=$1
  name_prefix=$(basename "$hostfile_template")
  hostfile="/tmp/$name_prefix"
  ./gen_hostfile.py ./hosts "$n_clients" >"$hostfile"
  export HOSTS_PATH="$hostfile"
}

function client_scalability() {
  for mode in "${MODES[@]}"; do
    for n_clients in 1 2 4 8 16 28 32 64 128; do
      set_hostfile $n_clients
      ./run.sh --polling-thread=28 --mode="${mode}" --batch=1000000
      #      ./run.sh --polling-thread=8 --computing-thread=20 --mode="${mode}" --batch=1000000
      #      ./run.sh --polling-thread=8 --computing-thread=28 --mode="${mode}" --batch=1000000
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
