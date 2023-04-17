#!/usr/bin/env bash
set -e
TEST_BUILD_ROOT=$1
PORT=$(shuf -i 2000-65000 -n 1)

function start_async_server() {
  $TEST_BUILD_ROOT/greeter_async_server $PORT &
  SERVER_PID=$!
}

function start_async_client2() {
  mpirun --bind-to none -n 4 $TEST_BUILD_ROOT/greeter_async_client2 $PORT
  echo "Killing $SERVER_PID"
  kill -9 $SERVER_PID
}

start_async_server
start_async_client2