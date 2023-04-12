#pragma once
#include "offsetAllocator.hpp"
namespace boke {
struct AllocatorData {
  OffsetAllocator::Allocator offset_allocator;
  std::uintptr_t head_addr{};
  uint32_t* offset_list{};
  OffsetAllocator::NodeIndex* metadata_list{};
  uint32_t offset_capacity{};
  uint32_t offset_num{};
  uint32_t alignment{};
};
AllocatorData GetAllocatorData(void* buffer, const uint32_t buffer_size_in_bytes, const uint32_t alignment = 8);
void* Allocate(const uint32_t size_in_bytes, AllocatorData*);
void Deallocate(void* ptr, AllocatorData*);
}
