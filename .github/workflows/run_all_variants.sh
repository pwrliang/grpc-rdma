#!/usr/bin/env bash

for type in TCP RDMA_BP RDMA_EVENT RDMA_BPEV; do
  echo "Evaluating $type $1 $2"
  GRPC_PLATFORM_TYPE=$type $(realpath "$1") "$2"
done