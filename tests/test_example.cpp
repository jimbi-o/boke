#include <doctest/doctest.h>
#include "boke/boke.h"
#include "offsetAllocator.hpp"
#include "tote/array.h"
#include "tote/hash_map.h"
namespace boke {
struct AllocatorData;
template <typename T> using Array = tote::ResizableArray<uint32_t, AllocatorData>;
template <typename T> auto GetUint32(const T v) { return static_cast<uint32_t>(v); }
void* Allocate(const uint32_t, AllocatorData*);
void Deallocate(void* ptr, AllocatorData*);
tote::AllocatorCallbacks<AllocatorData> GetAllocatorCallbacks();
} // namespace boke
namespace {
static const uint32_t buffer_size_in_bytes = 32 * 1024 * 1024;
static std::byte buffer[buffer_size_in_bytes];
} // namespace
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
} // namespace boke
namespace {
auto CalculateListSizeToAllocate(const boke::AllocatorData* allocator_data, const uint32_t new_capacity) {
  const auto offset_list_size = boke::GetUint32(sizeof(allocator_data->offset_list[0])) * new_capacity;
  const auto metadata_list_size = boke::GetUint32(sizeof(allocator_data->metadata_list[0])) * new_capacity;
  const auto offset = tote::Align(offset_list_size, allocator_data->alignment);
  const auto total_size = tote::Align(offset + metadata_list_size, allocator_data->alignment);
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
auto GetAllocatorData() {
  const auto size = buffer_size_in_bytes;
  const auto alignment = 8;
  const auto initial_capacity = 8;
  boke::AllocatorData allocator_data {
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
tote::AllocatorCallbacks<AllocatorData> GetAllocatorCallbacks() {
  static auto allocator_data = GetAllocatorData();
  return tote::AllocatorCallbacks<AllocatorData> {
    .allocate = Allocate,
    .deallocate = Deallocate,
    .user_context = &allocator_data,
  };
}
} // namespace boke
TEST_CASE("log") {
  spdlog::info("hello {}", "world");
  CHECK_UNARY(true);
}
TEST_CASE("sid") {
  namespace sid = foonathan::string_id;
  using namespace sid::literals;
  sid::default_database database;
  sid::string_id id("Test", database);
  switch (id.hash_code()) {
    case "Test"_id:
      CHECK_UNARY(true);
      break;
    case "NoTest"_id:
    default:
      CHECK_UNARY(false);
      break;
  }
}
TEST_CASE("array with custom allocation") {
  using namespace boke;
  Array<uint32_t> array(GetAllocatorCallbacks());
  array.push_back(1);
  array.push_back(3);
  array.push_back(5);
  array.push_back(7);
  array.push_back(9);
  array.push_back(101);
  array.push_back(102);
  Array<uint32_t> array2(GetAllocatorCallbacks());
  array2.push_back(1);
  array2.push_back(2);
  array2.push_back(3);
  array2.push_back(4);
  array2.push_back(5);
  array.push_back(1001);
  array.push_back(1002);
  array2.push_back(11);
  array2.push_back(12);
  array.push_back(10001);
  array.push_back(10002);
  array.push_back(10003);
  array.push_back(10004);
  array2.push_back(101);
  array2.push_back(102);
  array2.push_back(103);
  array2.push_back(104);
  CHECK_EQ(array[0], 1);
  CHECK_EQ(array[1], 3);
  CHECK_EQ(array[2], 5);
  CHECK_EQ(array[3], 7);
  CHECK_EQ(array[4], 9);
  CHECK_EQ(array[5], 101);
  CHECK_EQ(array[6], 102);
  CHECK_EQ(array[7], 1001);
  CHECK_EQ(array[8], 1002);
  CHECK_EQ(array[9], 10001);
  CHECK_EQ(array[10], 10002);
  CHECK_EQ(array[11], 10003);
  CHECK_EQ(array[12], 10004);
  CHECK_EQ(array2[0], 1);
  CHECK_EQ(array2[1], 2);
  CHECK_EQ(array2[2], 3);
  CHECK_EQ(array2[3], 4);
  CHECK_EQ(array2[4], 5);
  CHECK_EQ(array2[5], 11);
  CHECK_EQ(array2[6], 12);
  CHECK_EQ(array2[7], 101);
  CHECK_EQ(array2[8], 102);
  CHECK_EQ(array2[9], 103);
  CHECK_EQ(array2[10], 104);
  array.clear();
  array2.release_allocated_buffer();
  for (uint32_t i = 0; i < 1000; i++) {
    array.push_back(i * 2);
    array2.push_back(i + 1);
  }
  for (uint32_t i = 0; i < 1000; i++) {
    CHECK_EQ(array[i], i * 2);
    CHECK_EQ(array2[i], i + 1);
  }
}
