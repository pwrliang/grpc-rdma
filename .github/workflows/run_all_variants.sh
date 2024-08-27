#!/usr/bin/env bash

for use_rdma in true false; do
  echo "Use RDMA $use_rdma $1 $2"
  GRPC_ENABLE_RDMA_SUPPORT=$use_rdma $(realpath "$1") "$2"
done