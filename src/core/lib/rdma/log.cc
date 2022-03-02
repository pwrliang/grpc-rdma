#include "log.h"

const std::string RDMA_ENV_VAR = "RDMA_VERBOSITY";

rdma_log_severity rdma_min_severity_to_print = RDMA_LOG_SEVERITY_ERROR;


void SET_RDMA_VERBOSITY() {
    char *verbosity = std::getenv(RDMA_ENV_VAR.c_str());
    if (verbosity) {
      if (strcmp(verbosity, "DEBUG") == 0) {
        rdma_min_severity_to_print = RDMA_LOG_SEVERITY_DEBUG;
      } else if (strcmp(verbosity, "INFO") == 0) {
        rdma_min_severity_to_print = RDMA_LOG_SEVERITY_INFO;
      } else if (strcmp(verbosity, "WARNING") == 0) {
        rdma_min_severity_to_print = RDMA_LOG_SEVERITY_WARNING;
      } else if (strcmp(verbosity, "ERROR") == 0) {
        rdma_min_severity_to_print = RDMA_LOG_SEVERITY_ERROR;
      }
    }
}

const char* rdma_log_severity_string(rdma_log_severity severity) {
    switch (severity) {
        case RDMA_LOG_SEVERITY_DEBUG:
            return "DEBUG";
        case RDMA_LOG_SEVERITY_INFO:
            return "INFO";
        case RDMA_LOG_SEVERITY_WARNING:
            return "WARNING!";
        case RDMA_LOG_SEVERITY_ERROR:
            return "ERROR!!";
    }
    return "UNKNOWN";
}

void rdma_log(const char* file, int line, rdma_log_severity severity, const char *format, ...) {
    if (severity < rdma_min_severity_to_print) return;

    char *message = nullptr;
    va_list args;
    va_start(args, format);
    if (vasprintf(&message, format, args) == -1) {
        va_end(args);
        return;
    }
    va_end(args);

    long tid = syscall(__NR_gettid);
    char file_temp[64];
    strcpy(file_temp, file);
    char *final_slash = strrchr(file_temp, '/');
    char *display_file;
    if (final_slash == nullptr) display_file = file_temp;
    else display_file = final_slash + 1;
    fprintf(stderr, "[%-8s,%7ld %30s:%4d]\n%s\n\n", 
        rdma_log_severity_string(severity), tid, display_file, line, message);

    free(message);
}
