#ifndef HELLOWORLD_COMMON_H
#define HELLOWORLD_COMMON_H
#include <string>
#define TOTAL_N_REQS (1000)
const int min_msg_size = 1;
const int max_msg_size = 4 * 1024 * 1024 - 1024;  // gRPC's 4M limit

inline int get_msg_size(int min, int max)  // range : [min, max]
{
  static bool first = true;
  if (first) {
    srand(time(NULL));  // seeding for the first time only!
    first = false;
  }
  return min + rand() % ((max + 1) - min);
}

inline std::string gen_random_msg(const int len) {
  static const char alphanum[] =
      "0123456789"
      "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
      "abcdefghijklmnopqrstuvwxyz";
  std::string tmp_s;
  tmp_s.reserve(len);

  for (int i = 0; i < len; ++i) {
    tmp_s += alphanum[rand() % (sizeof(alphanum) - 1)];
  }

  return tmp_s;
}

#endif  // HELLOWORLD_COMMON_H
