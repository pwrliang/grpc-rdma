#ifndef BENCHMARK_UTILS
#define BENCHMARK_UTILS

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <memory>
#include <string>
#include <fstream>
#include <thread>
#include <sys/time.h>
#include <unistd.h>
#include <mpi.h>
#include <getopt.h>
#include <sstream>
#include <condition_variable>
#include <mutex>
#include <atomic>
#include "../../../src/core/lib/rdma/RDMAUtils.h"

extern int world_size, world_rank;

int mathToInt(std::string math) {
    std::stringstream mathStrm(math);
    int result;
    mathStrm >> result;
    char op;
    int num;
    while(mathStrm >> op >> num) result *= num;
    return result;
}

std::vector<int> Split2Int(const std::string& str, char delim) {
  std::vector<int> ints;
  size_t start;
  size_t end = 0;
  while ((start = str.find_first_not_of(delim, end)) != std::string::npos) {
    end = str.find(delim, start);
    ints.push_back(mathToInt(str.substr(start, end - start)));
  }
  return ints;
}

int random(int min, int max) {
  static bool first = true;
  if (first) {
    srand(time(NULL));  // seeding for the first time only!
    first = false;
  }
  return min + rand() % ((max + 1) - min);
}



#endif //#ifndef BENCHMARK_UTILS