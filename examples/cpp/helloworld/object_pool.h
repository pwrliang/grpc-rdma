

#ifndef HELLOWORLD_OBJECT_POOL_H
#define HELLOWORLD_OBJECT_POOL_H
#include <mutex>
#include <queue>
#include <unordered_map>
template <typename T>
class ObjectPool {
 public:
  template <typename... Args>
  std::unique_ptr<T> TakeObject(Args&&... args) {
    std::lock_guard<std::mutex> lg(mutex_);
    std::unique_ptr<T> obj;
    if (objects_.empty()) {
      objects_.push(std::unique_ptr<T>(new T(std::forward<Args>(args)...)));
      printf("Require object\n");
    }
    obj = std::move(objects_.front());
    objects_.pop();
    return obj;
  }

  void PutObject(std::unique_ptr<T> obj) {
    std::lock_guard<std::mutex> lg(mutex_);
    objects_.push(std::move(obj));
  }

 private:
  std::queue<std::unique_ptr<T>> objects_;
  std::mutex mutex_;
};

#endif  // HELLOWORLD_OBJECT_POOL_H
