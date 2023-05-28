#pragma once
#include <stdint.h>
namespace boke {
void InitAllocator(void* buffer, const uint32_t buffer_size_in_bytes);
void* Allocate(const uint32_t size_in_bytes, const uint32_t alignment);
void Deallocate(void* ptr);
template <typename T>
T* Allocate() {
  auto buf = Allocate(sizeof(T), alignof(T));
  return static_cast<T*>(buf);
}
template <typename T, typename... U>
T* New(U... args) {
  auto buf = Allocate(sizeof(T), alignof(T));
  return new(buf) T(args...);
}
template <typename T>
T* AllocateArray(const uint32_t count) {
  auto buf = Allocate(sizeof(T) * count, alignof(T));
  return static_cast<T*>(buf);
}
}
