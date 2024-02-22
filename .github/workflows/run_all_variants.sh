#!/usr/bin/env bash

for type in TCP RDMA_BP RDMA_EVENT RDMA_BPEV; do
  GRPC_PLATFORM_TYPE=$type "$1"
done