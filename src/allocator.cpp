#include "boke/allocator.h"
#include "boke/util.h"
namespace {
auto CalculateListSizeToAllocate(const boke::AllocatorData* allocator_data, const uint32_t new_capacity) {
  const auto offset_list_size = boke::GetUint32(sizeof(allocator_data->offset_list[0])) * new_capacity;
  const auto metadata_list_size = boke::GetUint32(sizeof(allocator_data->metadata_list[0])) * new_capacity;
  const auto offset = boke::Align(offset_list_size, allocator_data->alignment);
  const auto total_size = boke::Align(offset + metadata_list_size, allocator_data->alignment);
  return std::make_tuple(offset, total_size);
}
auto GetAllocation(boke::AllocatorData* allocator_data, const uint32_t size) {
  return allocator_data->offset_allocator.allocate(size / allocator_data->alignment);
}
auto GetVoidPointer(const boke::AllocatorData* allocator_data, const OffsetAllocator::Allocation& allocation) {
  return reinterpret_cast<void*>(allocator_data->head_addr + allocation.offset * allocator_data->alignment);
}
template <typename T>
auto GetPointer(const boke::AllocatorData* allocator_data, const OffsetAllocator::Allocation& allocation) {
  return static_cast<T*>(GetVoidPointer(allocator_data, allocation));
}
auto IsListExpansionNeeded(const boke::AllocatorData* allocator_data) {
  return allocator_data->offset_num + 1 >= allocator_data->offset_capacity;
}
auto FindIndex(boke::AllocatorData* allocator_data, const uint32_t offset) {
  for (uint32_t i = allocator_data->offset_num; i != ~0U; i--) {
    if (allocator_data->offset_list[i] == offset) {
      return i;
    }
  }
  return ~0U;
}
auto GetAllocation(boke::AllocatorData* allocator_data, void* ptr) {
  const auto diff = reinterpret_cast<std::uintptr_t>(ptr) - allocator_data->head_addr;
  const auto offset = boke::GetUint32(diff) / allocator_data->alignment;
  const auto index = FindIndex(allocator_data, offset);
  // assert(index != ~0U); // TODO use debug_assert
  return std::make_pair(index, OffsetAllocator::Allocation{.offset = offset, .metadata = allocator_data->metadata_list[index],});
}
auto AddAllocation(boke::AllocatorData* allocator_data, const OffsetAllocator::Allocation& allocation) {
  allocator_data->offset_list[allocator_data->offset_num] = allocation.offset;
  allocator_data->metadata_list[allocator_data->offset_num] = allocation.metadata;
  allocator_data->offset_num++;
}
auto RemoveUnusedIndices(boke::AllocatorData* allocator_data) {
  uint32_t j = 0;
  for (uint32_t i = 0; i < allocator_data->offset_num; i++) {
    if (allocator_data->offset_list[i] != ~0U) {
      allocator_data->offset_list[j] = allocator_data->offset_list[i];
      allocator_data->metadata_list[j] = allocator_data->metadata_list[i];
      j++;
    }
  }
  if (j > 0) {
    allocator_data->offset_num = j;
    return true;
  }
  return false;
}
auto ExpandList(boke::AllocatorData* allocator_data) {
  const auto new_capacity = allocator_data->offset_capacity * 2;
  // assert(new_capacity > allocator_data->offset_num); // TODO use debug_assert
  const auto [offset_to_metadata_list, total_size] = CalculateListSizeToAllocate(allocator_data, new_capacity);
  const auto allocation = allocator_data->offset_allocator.allocate(total_size / allocator_data->alignment);
  boke::Deallocate(allocator_data->offset_list, allocator_data);
  auto offset_list = GetPointer<uint32_t>(allocator_data, allocation);
  auto metadata_list = static_cast<OffsetAllocator::NodeIndex*>(reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(offset_list) + offset_to_metadata_list));
  memcpy(offset_list, allocator_data->offset_list, sizeof(offset_list[0]) * allocator_data->offset_num);
  memcpy(metadata_list, allocator_data->metadata_list, sizeof(metadata_list[0]) * allocator_data->offset_num);
  allocator_data->offset_list = offset_list;
  allocator_data->metadata_list = metadata_list;
  AddAllocation(allocator_data, allocation);
  allocator_data->offset_capacity = new_capacity;
}
} // namespace
namespace boke {
AllocatorData GetAllocatorData(void* buffer, const uint32_t buffer_size_in_bytes, const uint32_t alignment) {
  const auto size = buffer_size_in_bytes;
  const auto initial_capacity = 8;
  AllocatorData allocator_data {
    .offset_allocator = OffsetAllocator::Allocator(size / alignment),
    .head_addr = reinterpret_cast<std::uintptr_t>(buffer),
    .offset_list = nullptr,
    .metadata_list = nullptr,
    .offset_capacity = initial_capacity,
    .offset_num = 1,
    .alignment = alignment,
  };
  {
    const auto [offset_to_metadata_list, total_size] = CalculateListSizeToAllocate(&allocator_data, initial_capacity);
    const auto allocation = GetAllocation(&allocator_data, total_size);
    allocator_data.offset_list = GetPointer<uint32_t>(&allocator_data, allocation);
    allocator_data.metadata_list = static_cast<OffsetAllocator::NodeIndex*>(reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(allocator_data.offset_list) + offset_to_metadata_list));
    allocator_data.offset_list[0] = allocation.offset;
    allocator_data.metadata_list[0] = allocation.metadata;
  }
  return allocator_data;
}
void* Allocate(const uint32_t size, AllocatorData* allocator_data) {
  if (IsListExpansionNeeded(allocator_data)) {
    if (!RemoveUnusedIndices(allocator_data)) {
      ExpandList(allocator_data);
    }
  }
  auto allocation = allocator_data->offset_allocator.allocate(size / allocator_data->alignment);
  AddAllocation(allocator_data, allocation);
  return GetVoidPointer(allocator_data, allocation);
}
void Deallocate(void* ptr, AllocatorData* allocator_data) {
  const auto [index, allocation_to_remove] = GetAllocation(allocator_data, ptr);
  allocator_data->offset_allocator.free(allocation_to_remove);
  if (index + 1 == allocator_data->offset_num) {
    allocator_data->offset_num--;
  } else {
    allocator_data->offset_list[index] = ~0U;
  }
}
} // namespace boke
