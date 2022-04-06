set -e
export RDMA_VERBOSITY=ERROR
export HELLOWORLD_HOME=/home/geng.161/.cache/clion/tmp/tmp.wFl8U4DvNn/examples/cpp/helloworld/cmake-build-release-ri2-head

hostfile_template="hosts"
hostfile_template=$(realpath "$hostfile_template")
if [[ ! -f "$hostfile_template" ]]; then
  echo "Bad hostfile $hostfile_template"
  exit 1
fi

# for grp_mode in TCP RDMA_BP RDMA_EVENT; do
for grp_mode in RDMA_BP; do
  export GRPC_PLATFORM_TYPE=$grp_mode
  # 1 2 4 8 16 32 64
  for n_clients in 1 2 4 8 16 32 28 64 128; do
    name_prefix=$(basename "$hostfile_template")
    hostfile="/tmp/$name_prefix.${RANDOM}"
    while [[ -f "$hostfile" ]]; do
      hostfile="/tmp/$name_prefix.${RANDOM}"
    done
    ./gen_hostfile.py ./hosts $n_clients > "$hostfile"
    export HOSTS_PATH="$hostfile"
    export LOG_SUFFIX="${grp_mode}"
#     rm -rf ./logs/*
    ./run.sh --server_thread=28 --client_thread=1 --batch=60000
  done
done