#pragma once
#include <stdint.h>
namespace boke {
struct AllocatorData;
AllocatorData* GetAllocatorData(void* buffer, const uint32_t buffer_size_in_bytes);
void* Allocate(const uint32_t size_in_bytes, const uint32_t alignment, AllocatorData*);
void Deallocate(void* ptr, AllocatorData*);
template <typename T>
T* Allocate(AllocatorData* allocator_data) {
  auto buf = Allocate(sizeof(T), alignof(T), allocator_data);
  return static_cast<T*>(buf);
}
template <typename T>
T* AllocateArray(const uint32_t count, AllocatorData* allocator_data) {
  auto buf = Allocate(sizeof(T) * count, alignof(T), allocator_data);
  return static_cast<T*>(buf);
}
}
