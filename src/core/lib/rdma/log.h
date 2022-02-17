#ifndef _LOG_H_
#define _LOG_H_
#include <stdlib.h>
#include <string.h>
#include <iostream>
#include <sys/syscall.h>
#include <unistd.h>
#include <stdarg.h>
#include <sys/types.h>
#include <time.h>


typedef enum rdma_log_severity {
    RDMA_LOG_SEVERITY_DEBUG,
    RDMA_LOG_SEVERITY_INFO,
    RDMA_LOG_SEVERITY_WARNING,
    RDMA_LOG_SEVERITY_ERROR
} rdma_log_severity;



#define RDMA_DEBUG __FILE__, __LINE__, RDMA_LOG_SEVERITY_DEBUG
#define RDMA_INFO __FILE__, __LINE__, RDMA_LOG_SEVERITY_INFO
#define RDMA_WARNING __FILE__, __LINE__, RDMA_LOG_SEVERITY_WARNING
#define RDMA_ERROR __FILE__, __LINE__, RDMA_LOG_SEVERITY_ERROR

// this should be called in constructor of RDMANode::open
void SET_RDMA_VERBOSITY();

void rdma_log(const char *file, int line, rdma_log_severity severity, const char *format, ...);

#endif