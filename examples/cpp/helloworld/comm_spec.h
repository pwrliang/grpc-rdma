

#ifndef RDMASCRATCH_COMM_SPEC_H
#define RDMASCRATCH_COMM_SPEC_H
#include <cstring>
#include <map>
#include <numeric>
#include <string>
#include <vector>
#include "mpi.h"
#ifdef OPEN_MPI
#define NULL_COMM NULL
#else
#define NULL_COMM -1
#endif
#define ValidComm(comm) ((comm) != NULL_COMM)
#define SERVER_RANK 0
inline void InitMPIComm() {
  int provided;
  MPI_Init_thread(NULL, NULL, MPI_THREAD_MULTIPLE, &provided);
}

inline void FinalizeMPIComm() { MPI_Finalize(); }

/**
 * @brief CommSpec records the mappings of fragments, workers, and the
 * threads(tasks) in each worker.
 *
 */
class CommSpec {
 public:
  __attribute__((no_sanitize_address)) CommSpec()
      : worker_num_(1),
        worker_id_(0),
        local_num_(1),
        local_id_(0),
        comm_(NULL_COMM),
        local_comm_(NULL_COMM),
        client_comm_(NULL_COMM),
        owner_(false),
        local_owner_(false),
        client_owner_(false) {}

  __attribute__((no_sanitize_address)) CommSpec(const CommSpec& comm_spec)
      : worker_num_(comm_spec.worker_num_),
        worker_id_(comm_spec.worker_id_),
        local_num_(comm_spec.local_num_),
        local_id_(comm_spec.local_id_),
        comm_(comm_spec.comm_),
        local_comm_(comm_spec.local_comm_),
        client_comm_(comm_spec.client_comm_),
        owner_(false),
        local_owner_(false),
        client_owner_(false),
        worker_host_id_(comm_spec.worker_host_id_),
        host_worker_list_(comm_spec.host_worker_list_) {}

  __attribute__((no_sanitize_address)) ~CommSpec() {
    if (owner_ && ValidComm(comm_)) {
      MPI_Comm_free(&comm_);
    }
    if (local_owner_ && ValidComm(local_comm_)) {
      MPI_Comm_free(&local_comm_);
    }
    if (client_owner_ && ValidComm(client_comm_)) {
      MPI_Comm_free(&client_comm_);
    }
  }

  __attribute__((no_sanitize_address)) CommSpec& operator=(
      const CommSpec& rhs) {
    if (owner_ && ValidComm(comm_)) {
      MPI_Comm_free(&comm_);
    }
    if (local_owner_ && ValidComm(local_comm_)) {
      MPI_Comm_free(&local_comm_);
    }
    if (client_owner_ && ValidComm(client_comm_)) {
      MPI_Comm_free(&client_comm_);
    }

    worker_num_ = rhs.worker_num_;
    worker_id_ = rhs.worker_id_;
    local_num_ = rhs.local_num_;
    local_id_ = rhs.local_id_;
    comm_ = rhs.comm_;
    local_comm_ = rhs.local_comm_;
    client_comm_ = rhs.client_comm_;
    owner_ = false;
    local_owner_ = false;
    client_owner_ = false;

    return *this;
  }

  __attribute__((no_sanitize_address)) void Init(
      MPI_Comm comm, const std::string& hostname = "") {
    if (owner_ && ValidComm(comm_)) {
      MPI_Comm_free(&comm_);
    }
    if (local_owner_ && ValidComm(local_comm_)) {
      MPI_Comm_free(&local_comm_);
    }
    if (client_owner_ && ValidComm(client_comm_)) {
      MPI_Comm_free(&client_comm_);
    }
    MPI_Comm_rank(comm, &worker_id_);
    MPI_Comm_size(comm, &worker_num_);

    comm_ = comm;
    owner_ = false;
    local_owner_ = false;
    client_owner_ = false;

    initLocalInfo(hostname);
  }

  __attribute__((no_sanitize_address)) void Dup() {
    if (!owner_) {
      MPI_Comm old_comm = comm_;
      MPI_Comm_dup(old_comm, &comm_);
      owner_ = true;
    }
    if (!local_owner_) {
      MPI_Comm old_local_comm = local_comm_;
      MPI_Comm_dup(old_local_comm, &local_comm_);
      local_owner_ = true;
    }
    if (!client_owner_) {
      MPI_Comm old_client_comm = client_comm_;
      MPI_Comm_dup(old_client_comm, &client_comm_);
      client_owner_ = true;
    }
  }

  inline int worker_num() const { return worker_num_; }

  inline int worker_id() const { return worker_id_; }

  inline int local_num() const { return local_num_; }

  inline int local_id() const { return local_id_; }

  __attribute__((no_sanitize_address)) inline MPI_Comm comm() const {
    return comm_;
  }

  __attribute__((no_sanitize_address)) inline MPI_Comm local_comm() const {
    return local_comm_;
  }

  __attribute__((no_sanitize_address)) inline MPI_Comm client_comm() const {
    return client_comm_;
  }

  inline int host_num() const { return host_worker_list_.size(); }

  inline int host_id() const { return worker_host_id_[worker_id_]; }

  inline int host_id(int worker_id) const { return worker_host_id_[worker_id]; }

  inline std::vector<int>& host_worker_list(int host_id) {
    return host_worker_list_[host_id];
  }

  inline const std::vector<int>& host_worker_list(int host_id) const {
    return host_worker_list_[host_id];
  }

 private:
  __attribute__((no_sanitize_address)) void initLocalInfo(
      const std::string& hostname) {
    char hn[MPI_MAX_PROCESSOR_NAME];
    int hn_len;

    if (hostname.empty()) {
      MPI_Get_processor_name(hn, &hn_len);
    } else {
      hn_len = hostname.length() > MPI_MAX_PROCESSOR_NAME
                   ? MPI_MAX_PROCESSOR_NAME
                   : hostname.length();
      strncpy(hn, hostname.c_str(), hn_len);
    }

    char* recv_buf = reinterpret_cast<char*>(calloc(worker_num_, sizeof(hn)));
    MPI_Allgather(hn, MPI_MAX_PROCESSOR_NAME, MPI_CHAR, recv_buf,
                  MPI_MAX_PROCESSOR_NAME, MPI_CHAR, comm_);

    std::vector<std::string> worker_host_names(worker_num_);
    for (int i = 0; i < worker_num_; ++i) {
      worker_host_names[i].assign(
          &recv_buf[i * MPI_MAX_PROCESSOR_NAME],
          strlen(&recv_buf[i * MPI_MAX_PROCESSOR_NAME]));
    }
    free(recv_buf);

    std::map<std::string, int> hostname2id;
    worker_host_id_.clear();
    worker_host_id_.resize(worker_num_);

    host_worker_list_.clear();

    for (int i = 0; i < worker_num_; ++i) {
      auto iter = hostname2id.find(worker_host_names[i]);
      if (iter == hostname2id.end()) {
        int new_id = hostname2id.size();
        worker_host_id_[i] = new_id;
        hostname2id[worker_host_names[i]] = new_id;

        std::vector<int> vec;
        vec.push_back(i);
        host_worker_list_.emplace_back(std::move(vec));
      } else {
        worker_host_id_[i] = iter->second;
        host_worker_list_[iter->second].push_back(i);
      }
    }
    if (local_owner_ && ValidComm(local_comm_)) {
      MPI_Comm_free(&local_comm_);
    }
    MPI_Comm_split(comm_, worker_host_id_[worker_id_], worker_id_,
                   &local_comm_);
    MPI_Comm_size(local_comm_, &local_num_);
    MPI_Comm_rank(local_comm_, &local_id_);
    local_owner_ = true;

    if (client_owner_ && ValidComm(client_comm_)) {
      MPI_Comm_free(&client_comm_);
    }
    MPI_Comm_split(comm_, worker_id_ == SERVER_RANK ? 0 : 1, worker_id_,
                   &client_comm_);
    client_owner_ = true;
  }

  int worker_num_;
  int worker_id_;

  int local_num_;
  int local_id_;

  MPI_Comm comm_;
  MPI_Comm local_comm_;
  MPI_Comm client_comm_;
  bool owner_;
  bool local_owner_;
  bool client_owner_;

  std::vector<int> worker_host_id_;
  std::vector<std::vector<int>> host_worker_list_;
};

void Gather(MPI_Comm comm, const std::string& in,
            std::vector<std::string>& out) {
  int send_len = in.size();
  int comm_size;
  MPI_Comm_size(comm, &comm_size);
  std::vector<int> recvcounts(comm_size);
  MPI_Gather(&send_len, 1, MPI_INT, recvcounts.data(), 1, MPI_INT, 0, comm);
  std::vector<int> displs;
  int total_size = 0;
  for (auto len : recvcounts) {
    displs.push_back(total_size);
    total_size += len;
  }
  char* data = static_cast<char*>(malloc(total_size));

  MPI_Gatherv(in.data(), send_len, MPI_CHAR, data, recvcounts.data(),
              displs.data(), MPI_CHAR, 0, comm);
  out.resize(comm_size);
  for (int i = 0; i < comm_size; i++) {
    //    out[i].resize(recvcounts[i]);
    out[i].assign(data + displs[i], recvcounts[i]);
  }
  free(data);
}

#endif  // RDMASCRATCH_COMM_SPEC_H
