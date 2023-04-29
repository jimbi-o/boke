#include "boke/allocator.h"
#include <algorithm>
#include "boke/debug_assert.h"
#include "boke/util.h"
#include "offsetAllocator.hpp"
namespace boke {
struct AllocatorData {
  OffsetAllocator::Allocator* offset_allocator;
  std::uintptr_t head_addr{};
  uint32_t size{};
};
} // namespace boke
namespace boke {
AllocatorData* GetAllocatorData(void* buffer, const uint32_t buffer_size_in_bytes) {
  const auto head_addr = reinterpret_cast<std::uintptr_t>(buffer);
  const auto aligned_head_addr = Align(head_addr, alignof(AllocatorData));
  auto allocator_data = static_cast<AllocatorData*>(reinterpret_cast<void*>(aligned_head_addr));
  const auto offset_allocator_addr = Align(aligned_head_addr + sizeof(AllocatorData), alignof(OffsetAllocator::Allocator));
  allocator_data->offset_allocator = new (reinterpret_cast<void*>(offset_allocator_addr)) OffsetAllocator::Allocator(buffer_size_in_bytes);
  allocator_data->head_addr = offset_allocator_addr + sizeof(OffsetAllocator::Allocator);
  allocator_data->size = buffer_size_in_bytes - GetUint32(allocator_data->head_addr - head_addr);
  return allocator_data;
}
void* Allocate(const uint32_t size, const uint32_t min_alignment, AllocatorData* allocator_data) {
  const auto alignment = std::max(min_alignment, 8U);
  DEBUG_ASSERT(alignment % 8/*Align(sizeof(OffsetAllocator::NodeIndex)+1,4)*/ == 0, DebugAssert());

  const auto total_size = size + alignment + sizeof(OffsetAllocator::NodeIndex);
  const auto allocation = allocator_data->offset_allocator->allocate(GetUint32(total_size));
  DEBUG_ASSERT(allocation.offset != OffsetAllocator::Allocation::NO_SPACE, DebugAssert());
  DEBUG_ASSERT(allocation.metadata != OffsetAllocator::Allocation::NO_SPACE, DebugAssert());

  const auto ptr_val = allocator_data->head_addr + allocation.offset;
  auto aligned_ptr_val = Align(ptr_val, alignment);
  if (aligned_ptr_val - ptr_val < sizeof(OffsetAllocator::NodeIndex) + 1) {
    aligned_ptr_val += alignment;
  }
  DEBUG_ASSERT(aligned_ptr_val + size <= ptr_val + total_size, DebugAssert());

  auto aligned_ptr = reinterpret_cast<void*>(aligned_ptr_val);
  {
    auto offset_allocator_metadata_array = reinterpret_cast<OffsetAllocator::NodeIndex*>(aligned_ptr);
    offset_allocator_metadata_array[-1] = allocation.offset;
  }
  {
    const auto shift = aligned_ptr_val - ptr_val;
    DEBUG_ASSERT(shift > sizeof(OffsetAllocator::NodeIndex) && shift <= 256, DebugAssert());
    auto shift_array = reinterpret_cast<uint8_t*>(aligned_ptr);
    shift_array[-5] = static_cast<uint8_t>(shift & 0xFF); // to [-4] is occupied by offset_allocator_metadata_array[-1]
  }
  return aligned_ptr;
}
void Deallocate(void* ptr, AllocatorData* allocator_data) {
  if (ptr == nullptr) { return; }
  auto aligned_ptr = reinterpret_cast<uint8_t*>(ptr);
  auto shift = static_cast<uint32_t>(aligned_ptr[-5]);
  if (shift == 0) {
    shift = 256;
  }
  const auto raw_ptr = reinterpret_cast<uintptr_t>(ptr) - shift;
  const auto offset = GetUint32(raw_ptr - allocator_data->head_addr);
  const auto metadata = reinterpret_cast<uint32_t*>(ptr);
  allocator_data->offset_allocator->free({.offset = offset, .metadata = metadata[-1]});
}
} // namespace boke
