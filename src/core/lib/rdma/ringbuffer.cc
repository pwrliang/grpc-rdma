#include "grpc/impl/codegen/log.h"
#include "include/grpcpp/stats_time.h"
#include "src/core/lib/debug/trace.h"
#include "ringbuffer.h"
#include "rdma_conn.h"
#define MIN3(a, b, c) MIN(a, MIN(b, c))
grpc_core::DebugOnlyTraceFlag grpc_trace_ringbuffer(false, "rdma_ringbuffer");

size_t MIN_ZEROCOPY_SIZE = 512;

size_t global_sendbuf_capacities[GS_CAP_GENRES] = {1024, 
                                                   32*1024, 
                                                   1024*1024, 
                                                   4*1024*1024, 
                                                   16*1024*1024, 
                                                   64*1024*1024, 
                                                   256*1024*1024};

// size_t global_sendbuf_nums[GS_CAP_GENRES] = {64,  // 1KB
//                                              64,  // 32KB
//                                              64,  // 1MB
//                                              64,  // 4MB
//                                              16,  // 16MB
//                                              16,  // 64MB
//                                              4};  // 256MB

size_t global_sendbuf_nums[GS_CAP_GENRES] = {1,  // 1KB
                                             1,  // 32KB
                                             1,  // 1MB
                                             1,  // 4MB
                                             1,  // 16MB
                                             1,  // 64MB
                                             1};  // 256MB

uint8_t* global_sendbuf_alloc(size_t size) {
  GlobalSendBufferManager& gsbm = GlobalSendBufferManager::GetInstance();
  return gsbm.alloc(size);
}

bool global_sendbuf_free(uint8_t* buf) {
  GlobalSendBufferManager& gsbm = GlobalSendBufferManager::GetInstance();
  return gsbm.free(buf);
}

GlobalSendBuffer::GlobalSendBuffer(size_t capacity, int id) : capacity_(capacity), genre_id_(id) {
  buf_ = new uint8_t[capacity];

  auto& node = RDMANode::GetInstance();
  auto pd = node.get_pd();

  if (mr_.RegisterLocal(pd, buf_, capacity)) {
    gpr_log(GPR_ERROR, "failed to RegisterLocal global_sendbuf_mr");
    exit(-1);
  }

}

GlobalSendBuffer::~GlobalSendBuffer() {
  delete[] buf_;
}

GlobalSendBufferManager::GlobalSendBufferManager() {
  for (int i = 0; i < GS_CAP_GENRES; i++) {
    size_t cap = global_sendbuf_capacities[i];
    size_t num = global_sendbuf_nums[i];
    for (int j = 0; j < num; j++) {
      GlobalSendBuffer* gsb = new GlobalSendBuffer(cap, i);
      all_bufs_.insert(std::pair<uint8_t*, GlobalSendBuffer*>(gsb->buf_, gsb));
      free_bufs_[i].insert(std::pair<uint8_t*, GlobalSendBuffer*>(gsb->buf_, gsb));
    }
    failed_alloc_num_[i].store(0);
  }

  alloc_num_[GS_CAP_GENRES].store(0);
  failed_alloc_num_[GS_CAP_GENRES].store(0);
}

GlobalSendBufferManager::~GlobalSendBufferManager() {
  for (std::map<uint8_t*, GlobalSendBuffer*>::iterator it = all_bufs_.begin(); it != all_bufs_.end(); it++) {
    delete it->second;
  }
}

uint8_t* GlobalSendBufferManager::alloc(size_t size) {
  if (size < MIN_ZEROCOPY_SIZE) return nullptr;

  std::lock_guard<std::mutex> lck(mtx_);
  int i = 0;
  while (i < GS_CAP_GENRES && size > global_sendbuf_capacities[i]) { i++; }

  // size <= global_sendbuf_capacities[i] || i == GS_CAP_GENRES
  if (i == GS_CAP_GENRES) { // size is larger than max capacity
    failed_alloc_num_[GS_CAP_GENRES].fetch_add(1);
    return nullptr; 
  }

  alloc_num_[i].fetch_add(1);
  if (free_bufs_[i].size() == 0) { // no free bufs
    failed_alloc_num_[i].fetch_add(1);
    return nullptr;
  }

  std::map<uint8_t*, GlobalSendBuffer*>::iterator it = free_bufs_[i].begin();
  uint8_t* buf = it->first;
  GlobalSendBuffer* gsb = it->second;
  gsb->used_ = size;
  free_bufs_[i].erase(it);
  // used_bufs_[i].insert(std::pair<uint8_t*, GlobalSendBuffer*>(buf, gsb));
  used_bufs_[i][buf] = gsb;

  printf("GSBM::alloc, buf = %p, size = %lld, capacity = %lld\n", buf, size, global_sendbuf_capacities[i]);

  return buf;
}

bool GlobalSendBufferManager::free(uint8_t* buf) {
  std::lock_guard<std::mutex> lck(mtx_);
  std::map<uint8_t*, GlobalSendBuffer*>::iterator it = all_bufs_.find(buf);

  if (it == all_bufs_.end()) {
    return false; // this buffer is not a GlobalSendBuffer
  }

  GlobalSendBuffer* gsb = it->second;
  int genre_id = gsb->genre_id_;
  it = used_bufs_[genre_id].find(buf);

  if (it == used_bufs_[genre_id].end()) {
    return false; // this buffer is a GlobalSendBuffer, but it is a free buffer
  }

  printf("GSBM::free, buf = %p, size = %lld, capacity = %lld\n", buf, gsb->used_, global_sendbuf_capacities[genre_id]);

  gsb->used_ = 0;
  used_bufs_[genre_id].erase(it);
  // free_bufs_[genre_id].insert(std::pair<uint8_t*, GlobalSendBuffer*>(buf, gsb));
  free_bufs_[genre_id][buf] = gsb;
  return true;
}

GlobalSendBuffer* GlobalSendBufferManager::contains(uint8_t* ptr) {
  std::lock_guard<std::mutex> lck(mtx_);
  std::map<uint8_t*, GlobalSendBuffer*>::iterator it = all_bufs_.find(ptr);
  if (it != all_bufs_.end()) { // ptr is a buf
    return it->second;
  }

  std::map<uint8_t*, uint8_t*>::iterator link = linkers_.find(ptr);
  if (link == linkers_.end()) {
    return nullptr;
  }

  // ptr is a head
  it = all_bufs_.find(link->second);
  if (it != all_bufs_.end()) {
    return it->second;
  }

  return nullptr;
}

bool GlobalSendBufferManager::add_link(uint8_t* head, uint8_t* buf) {
  std::lock_guard<std::mutex> lck(mtx_);
  std::map<uint8_t*, GlobalSendBuffer*>::iterator it = all_bufs_.find(buf);
  if (it == all_bufs_.end()) {
    return false;
  }

  linkers_[head] = buf;
  return true;
}

bool GlobalSendBufferManager::remove_link(uint8_t* head) {
  std::lock_guard<std::mutex> lck(mtx_);
  std::map<uint8_t*, uint8_t*>::iterator it = linkers_.find(head);
  if (it == linkers_.end()) {
    return false;
  }

  linkers_.erase(it);
  return true;
}

// to reduce operation, the caller should guarantee the arguments are valid
uint8_t RingBufferBP::checkTail(size_t head, size_t mlen) const {
  return buf_[(head + mlen + sizeof(size_t) + capacity_) % capacity_];
}

size_t RingBufferBP::checkFirstMesssageLength(size_t head) const {
  GPR_ASSERT(head < capacity_);
  size_t mlen;

  if (head + sizeof(size_t) <= capacity_) {
    mlen = *reinterpret_cast<size_t*>(buf_ + head);
    if (mlen == 0 || mlen > get_max_send_size() || checkTail(head, mlen) != 1) {
      return 0;
    }
    // need read again, since mlen is read before tag = 1
    return *reinterpret_cast<size_t*>(buf_ + head);
  }

  size_t r = capacity_ - head;
  size_t l = sizeof(size_t) - r;
  memcpy(&mlen, buf_ + head, r);
  memcpy(reinterpret_cast<uint8_t*>(&mlen) + r, buf_, l);
  if (mlen == 0 || mlen > get_max_send_size() || checkTail(head, mlen) != 1) {
    return 0;
  }
  memcpy(&mlen, buf_ + head, r);
  memcpy(reinterpret_cast<uint8_t*>(&mlen) + r, buf_, l);

  return mlen;
}

size_t RingBufferBP::checkMessageLength(size_t head) const {
  size_t mlen, mlens = 0;
  while ((mlen = checkFirstMesssageLength(head)) > 0) {
    mlens += mlen;
    head = (head + mlen + sizeof(size_t) + 1) % capacity_;
  }
  return mlens;
}

size_t RingBufferBP::resetBufAndUpdateHead(size_t lens) {
  if (head_ + lens > capacity_) {
    memset(buf_ + head_, 0, capacity_ - head_);
    memset(buf_, 0, lens + head_ - capacity_);
  } else {
    memset(buf_ + head_, 0, lens);
  }
  return updateHead(lens);
}

bool RingBufferBP::Read(msghdr* msg, size_t& expected_mlens) {
  auto head = head_;
  if (expected_mlens == 0 || expected_mlens > get_max_send_size()) {
    gpr_log(GPR_ERROR, "Illegal expected_mlens: %zu, head: %zu", expected_mlens,
            head);
  }
  GPR_ASSERT(head < capacity_);

  size_t iov_idx = 0, iov_offset = 0;
  size_t mlen = checkFirstMesssageLength(head), m_offset = 0;
  size_t read_mlens = 0, read_lens = 0;
  size_t buf_offset = (head + sizeof(size_t)) % capacity_;
  size_t msghdr_size = 0;

  // calculate total space
  for (int i = 0; i < msg->msg_iovlen; i++) {
    msghdr_size += msg->msg_iov[i].iov_len;
  }

  while (iov_idx < msg->msg_iovlen && mlen > 0 && mlen <= msghdr_size) {
    size_t iov_rlen = msg->msg_iov[iov_idx].iov_len -
                      iov_offset;     // rest space of current slice
    size_t m_rlen = mlen - m_offset;  // uncopied bytes of current message
    size_t n = MIN3(iov_rlen, m_rlen, capacity_ - buf_offset);
    auto* iov_rbase =
        static_cast<uint8_t*>(msg->msg_iov[iov_idx].iov_base) + iov_offset;
    cycles_t begin_cycles = get_cycles();
    memcpy(iov_rbase, buf_ + buf_offset, n);
    cycles_t t_cycles = get_cycles() - begin_cycles;
    size_t mb_s = n / (t_cycles / mhz_);
    grpc_stats_time_add_custom(GRPC_STATS_TIME_ADHOC_4, mb_s);

#ifndef NDEBUG
    if (GRPC_TRACE_FLAG_ENABLED(grpc_trace_ringbuffer)) {
      gpr_log(GPR_DEBUG, "read_to_msghdr, read %zu bytes from head %zu", n,
              buf_offset);
    }
#endif
    buf_offset += n;
    iov_offset += n;
    m_offset += n;

    // current slice is used up
    if (n == iov_rlen) {
      // all space of the current slice has been used up. move to next slice
      iov_idx++;
      iov_offset = 0;
    }

    // current message is used up
    if (n == m_rlen) {
      // current message (length of mlen) has been copyied. move to next head
      read_mlens += mlen;
      read_lens += sizeof(size_t) + mlen + 1;

      // when we have chance to read, read greedily.
      if (read_mlens >= expected_mlens) {
        break;
      }
      head =
          (head + sizeof(size_t) + mlen + 1) % capacity_;  // move to next head
      mlen = checkFirstMesssageLength(head);  // check mlen of the new head
      if (read_mlens + mlen > msghdr_size) {  // msghdr could not hold new mlen
        break;
      }
      m_offset = 0;

      // move buf_offset to the first place right after the head of the next
      // message if no next message, the loop will finish.
      buf_offset += 1 + sizeof(size_t);
    }
    buf_offset = buf_offset % capacity_;
  }

  expected_mlens = read_mlens;
  resetBufAndUpdateHead(read_lens);

  garbage_ += read_lens;
  if (garbage_ >= capacity_ / 2) {
    garbage_ = 0;
    return true;
  }
  return false;
}

// -----< RingBufferEvent >-----

bool RingBufferEvent::Read(msghdr* msg, size_t& expected_read_size) {
  auto head = head_;
  GPR_ASSERT(expected_read_size > 0 && expected_read_size < capacity_);
  GPR_ASSERT(head < capacity_);

  size_t lens = 0, iov_rlen;
  uint8_t *iov_rbase, *rb_ptr;
  for (size_t i = 0, iov_offset = 0, n;
       lens < expected_read_size && i < msg->msg_iovlen; lens += n) {
    iov_rlen =
        msg->msg_iov[i].iov_len - iov_offset;  // rest space of current slice
    iov_rbase = static_cast<uint8_t*>(msg->msg_iov[i].iov_base) + iov_offset;
    rb_ptr = buf_ + head;
    n = MIN3(capacity_ - head, expected_read_size - lens, iov_rlen);
    memcpy(iov_rbase, rb_ptr, n);
#ifndef NDEBUG
    if (GRPC_TRACE_FLAG_ENABLED(grpc_trace_ringbuffer)) {
      gpr_log(GPR_DEBUG, "read_to_msghdr, read %zu bytes from head %zu", n,
              head);
    }
#endif
    head += n;
    iov_offset += n;
    if (n == iov_rlen) {
      // all space of the current slice has been used up. move to next slice
      i++;
      iov_offset = 0;
    }
    head = head % capacity_;
  }

  updateHead(lens);
  expected_read_size = lens;

  garbage_ += lens;
  if (garbage_ >= capacity_ / 2) {
    garbage_ = 0;
    return true;
  }

  return false;
}