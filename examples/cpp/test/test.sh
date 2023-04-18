#!/usr/bin/env bash
set -e
TEST_BUILD_ROOT=$1
PORT=$(shuf -i 2000-65000 -n 1)

function start_async_server() {
  $TEST_BUILD_ROOT/greeter_async_server $PORT &
  SERVER_PID=$!
}

function start_async_client2() {
  if [[ $GRPC_PLATFORM_TYPE == "RDMA" ]]; then
    mpirun --bind-to none -n 4 -output-filename client_log $TEST_BUILD_ROOT/greeter_async_client2 $PORT
  else
    mpirun --bind-to none -n 1 -output-filename client_log $TEST_BUILD_ROOT/greeter_async_client2 $PORT
  fi
  echo "Killing $SERVER_PID"
  kill -9 $SERVER_PID
}

function cleanup() {
  if ps aux | pgrep greeter; then
    ps aux | pgrep greeter | xargs kill -9
  fi
}

cleanup
start_async_server
start_async_client2
cleanup