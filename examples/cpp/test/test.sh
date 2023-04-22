#!/usr/bin/env bash
set -e

PORT=$(shuf -i 2000-65000 -n 1)
NP=4

function start_server() {
  TEST_BUILD_ROOT=$1
  "$TEST_BUILD_ROOT"/greeter_server $PORT |& tee server.log &
  SERVER_PID=$!
}

function start_client() {
  TEST_BUILD_ROOT=$1
  mpirun --bind-to none -n $NP -output-filename client_log "$TEST_BUILD_ROOT"/greeter_client $PORT
  echo "Killing $SERVER_PID"
  kill -9 $SERVER_PID
}

function start_async_server() {
  TEST_BUILD_ROOT=$1
  "$TEST_BUILD_ROOT"/greeter_async_server $PORT |& tee server.log &
  SERVER_PID=$!
}

function start_async_client() {
  TEST_BUILD_ROOT=$1
  mpirun --bind-to none -n $NP -output-filename client_log "$TEST_BUILD_ROOT"/greeter_async_client $PORT
  echo "Killing $SERVER_PID"
  kill -9 $SERVER_PID
}

function start_async_client2() {
  TEST_BUILD_ROOT=$1
  mpirun --bind-to none -n $NP -output-filename client_log "$TEST_BUILD_ROOT"/greeter_async_client2 $PORT
  echo "Killing $SERVER_PID"
  kill -9 $SERVER_PID
}

function cleanup() {
  if ps aux | pgrep greeter; then
    ps aux | pgrep greeter | xargs kill -9
  fi
}

if [[ $# -eq 0 ]]; then
  echo 'No args'
  exit 1
fi

for i in "$@"; do
  case $i in
  --clean)
    cleanup
    shift
    ;;
  --test-client=*)
    prefix="${i#*=}"
    start_server "$prefix"
    start_client "$prefix"
    cleanup
    shift
    ;;
  --test-async-client=*)
    prefix="${i#*=}"
    start_async_server "$prefix"
    start_async_client "$prefix"
    cleanup
    shift
    ;;
  --test-async-client2=*)
    prefix="${i#*=}"
    start_async_server "$prefix"
    start_async_client2 "$prefix"
    cleanup
    shift
    ;;
  *)
    echo "Unknown option $i"
    exit 1
    ;;
  esac
done
