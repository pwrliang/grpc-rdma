#!/usr/bin/env bash
set -e

PORT=$(shuf -i 2000-65000 -n 1)

function start_async_server() {
  TEST_BUILD_ROOT=$1
  $TEST_BUILD_ROOT/greeter_async_server $PORT |& tee server.log &
  SERVER_PID=$!
}

function start_async_client2() {
  TEST_BUILD_ROOT=$1
  mpirun --bind-to none -n 4 -output-filename client_log $TEST_BUILD_ROOT/greeter_async_client2 $PORT
  echo "Killing $SERVER_PID"
  kill -9 $SERVER_PID
}

function cleanup() {
  if ps aux | pgrep greeter; then
    ps aux | pgrep greeter | xargs kill -9
  fi
}
for i in "$@"; do
  case $i in
  --clean)
    cleanup
    shift
    ;;
  --test=*)
    prefix="${i#*=}"
    start_async_server "$prefix"
    start_async_client2 "$prefix"
    shift
    ;;
  --* | -*)
    echo "Unknown option $i"
    exit 1
    ;;
  *) ;;
  esac
done
