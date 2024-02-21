# Copyright 2018 gRPC authors.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# cmake build file for C++ helloworld example.
# Assumes protobuf and gRPC have been installed using cmake.
# See cmake_externalproject/CMakeLists.txt for all-in-one cmake build
# that automatically builds all the dependencies before building helloworld.

cmake_minimum_required(VERSION 3.8)

project(Microbenchmark C CXX)

include(../cmake/common.cmake)
include(FindHdrHistogram.cmake)
if (NOT HdrHistogram_FOUND)
    message(FATAL_ERROR "HdrHistogram not found, need to build and install from https://github.com/HdrHistogram/HdrHistogram_c")
endif ()

message("protoc: ${_PROTOBUF_PROTOC}")

# Proto file
get_filename_component(mb_proto "../../protos/micro_benchmark.proto" ABSOLUTE)
get_filename_component(mb_proto_path "${mb_proto}" PATH)

message("mb_proto: ${mb_proto}, mb_proto_path: ${mb_proto_path}")

# Generated sources
set(mb_proto_srcs "${CMAKE_CURRENT_BINARY_DIR}/micro_benchmark.pb.cc")
set(mb_proto_hdrs "${CMAKE_CURRENT_BINARY_DIR}/micro_benchmark.pb.h")
set(mb_grpc_srcs "${CMAKE_CURRENT_BINARY_DIR}/micro_benchmark.grpc.pb.cc")
set(mb_grpc_hdrs "${CMAKE_CURRENT_BINARY_DIR}/micro_benchmark.grpc.pb.h")
add_custom_command(
        OUTPUT "${mb_proto_srcs}" "${mb_proto_hdrs}" "${mb_grpc_srcs}" "${mb_grpc_hdrs}"
        COMMAND ${_PROTOBUF_PROTOC}
        ARGS --grpc_out "${CMAKE_CURRENT_BINARY_DIR}"
        --cpp_out "${CMAKE_CURRENT_BINARY_DIR}"
        -I "${mb_proto_path}" -I "/home/geng.161/.local/include" -I "/users/PAS0350/geng161/.local/include"
        --plugin=protoc-gen-grpc="${_GRPC_CPP_PLUGIN_EXECUTABLE}"
        "${mb_proto}"
        DEPENDS "${mb_proto}")

# Include generated *.pb.h files
include_directories("${CMAKE_CURRENT_BINARY_DIR}")
find_package(MPI REQUIRED)
find_package(absl REQUIRED)

# mb_grpc_proto
add_library(mb_grpc_proto
        ${mb_grpc_srcs}
        ${mb_grpc_hdrs}
        ${mb_proto_srcs}
        ${mb_proto_hdrs})
target_link_libraries(mb_grpc_proto
        ${_REFLECTION}
        ${_GRPC_GRPCPP}
        ${_PROTOBUF_LIBPROTOBUF})

# Targets greeter_[async_](client|server)
foreach (_target
        mb_server mb_client)
    add_executable(${_target} "${_target}.cc")
    target_include_directories(${_target} PRIVATE ${MPI_CXX_INCLUDE_PATH} ${HdrHistogram_INCLUDE_DIRS})
    target_link_libraries(${_target}
            mb_grpc_proto
            absl::flags
            absl::flags_parse
            ${_REFLECTION}
            ${_GRPC_GRPCPP}
            ${_PROTOBUF_LIBPROTOBUF}
            ${MPI_CXX_LIBRARIES}
            ${HdrHistogram_LIBRARIES}
            numa
    )
endforeach ()
