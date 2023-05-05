## 1. Build 
```
cd grpc-rdma/examples/cpp/rdma_microbenchmark
mkdir build && cd build
cmake .. -DCMAKE_PREFIX_PATH=/users/PAS0350/geng161/.local -DCMAKE_C_COMPILER=$(which gcc) -DCMAKE_CXX_COMPILER=$(which g++)
make -j8
```

## 2. Small scale test
- Edit `testhosts`
- First line is the hostname of server; lines after the first line represent clients
- slots is the number of processes launching for a given host
- slots should always be 1 for the server
- Execute `test.sh` to run the RDMA microbenchmark

## 3. Large scale test
- After you run the small scale test smoothly, you can try the script `benchmark/run-all.sh`
- Edit `run-all.sh`, change MB_HOME, SERVER_CPU_CORES according to your settings.
- MB_HOME is the location of binary you just built, SERVER_CPU_CORES is the number of cores of server node
- Edit benchmark/hosts, the first line is server others are clients. DO NOT NEED TO SPECIFY `slots`.
- Run experiments with `cd benchmark && ./run-all.sh --client-scalability`, logs are under `benchmark/logs/`