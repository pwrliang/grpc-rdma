include(FindPackageHandleStandardArgs)

set(HdrHistogram_ROOT_DIR "" CACHE PATH "Folder contains HdrHistogram")

# We are testing only a couple of files in the include directories
find_path(HdrHistogram_INCLUDE_DIR hdr/hdr_histogram.h
        PATHS ${HdrHistogram_ROOT_DIR})

find_library(HdrHistogram_LIBRARY hdr_histogram)

find_package_handle_standard_args(HdrHistogram DEFAULT_MSG HdrHistogram_INCLUDE_DIR HdrHistogram_LIBRARY)

if(HdrHistogram_FOUND)
    set(HdrHistogram_INCLUDE_DIRS ${HdrHistogram_INCLUDE_DIR})
    set(HdrHistogram_LIBRARIES ${HdrHistogram_LIBRARY})
    message(STATUS "Found HdrHistogram  (include: ${HdrHistogram_INCLUDE_DIR}, library: ${HdrHistogram_LIBRARY})")
    mark_as_advanced(HdrHistogram_LIBRARY_DEBUG HdrHistogram_LIBRARY_RELEASE
            HdrHistogram_LIBRARY HdrHistogram_INCLUDE_DIR HdrHistogram_ROOT_DIR)
endif()