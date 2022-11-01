

#ifndef HELLOWORLD_BYTEBUFFER_UTIL_H
#define HELLOWORLD_BYTEBUFFER_UTIL_H
#include <string>
#include <vector>
#include "grpc/byte_buffer.h"
#include "grpc/slice.h"

std::string ByteBufferToString(const grpc::ByteBuffer& byte_buffer) {
  std::vector<grpc::Slice> slices;
  std::string buf;

  byte_buffer.Dump(&slices);
  buf.reserve(byte_buffer.Length());
  for (auto s = slices.begin(); s != slices.end(); s++) {
    buf.append(reinterpret_cast<const char*>(s->begin()), s->size());
  }
  return buf;
}

grpc::ByteBuffer StringToByteBuffer(const std::string& str) {
  std::vector<grpc::Slice> slices;
  grpc::Slice slice(str);

  slices.push_back(slice);
  grpc::ByteBuffer buffer(slices.data(), slices.size());
  return buffer;
}

#endif  // HELLOWORLD_BYTEBUFFER_UTIL_H
