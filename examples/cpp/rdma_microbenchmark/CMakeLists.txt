cmake_minimum_required(VERSION 3.5.1)

project(rdma_microbenchmark C CXX)

include("../cmake/FindGFlags.cmake")
if (NOT GFLAGS_FOUND)
    message(FATAL_ERROR "gflags not found, build without gflags")
else ()
    include_directories(SYSTEM ${GFLAGS_INCLUDE_DIRS})
endif ()
find_package(MPI REQUIRED)
add_subdirectory(../../.. ${CMAKE_CURRENT_BINARY_DIR}/grpc EXCLUDE_FROM_ALL)
include_directories(SYSTEM ${MPI_CXX_INCLUDE_PATH})
include_directories(${PROJECT_SOURCE_DIR}/../../..)
include_directories(${PROJECT_SOURCE_DIR}/../../../include)
include_directories(${PROJECT_SOURCE_DIR}/../../../third_party/abseil-cpp)

#set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address -fno-omit-frame-pointer -O1")
#set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=address")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread")

add_executable(mb mb.cc flags.cc)
target_link_libraries(mb
        ${GFLAGS_LIBRARIES} ${MPI_CXX_LIBRARIES} grpc++ ibverbs)