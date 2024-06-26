include(${PROJECT_SOURCE_DIR}/cmake/modules/FindHdrHistogram.cmake)
if(HdrHistogram_FOUND)
    set(_gRPC_HDRHISTOGRAM_INCLUDE_DIR ${HdrHistogram_INCLUDE_DIRS})
    set(_gRPC_HDRHISTOGRAM_LIBRARIES ${HdrHistogram_LIBRARIES})
else ()
    message(FATAL_ERROR "HdrHistogram not found, need to build and install from https://github.com/HdrHistogram/HdrHistogram_c")
endif()