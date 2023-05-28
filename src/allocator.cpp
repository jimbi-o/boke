#include "boke/allocator.h"
#include <algorithm>
#include <limits>
#include "boke/debug_assert.h"
#include "boke/util.h"
#include "offsetAllocator.hpp"
namespace {
using namespace boke;
using ShiftType = uint16_t;
struct AllocatorData {
  OffsetAllocator::Allocator* offset_allocator;
  std::uintptr_t head_addr{};
  uint32_t size{};
};
const auto kMaxShiftVal = GetUint32(std::numeric_limits<ShiftType>::max()) + 1;
const auto kMetadataSize = static_cast<uint32_t>(sizeof(OffsetAllocator::NodeIndex) + sizeof(ShiftType));
const uint32_t kMinAlignment = 8; // minimum power of two integer larger than kMetadataSize
const uint32_t kMaxNodeIndex = 128 * 1024;
AllocatorData* allocator = nullptr;
auto GetAlignedAddr(std::uintptr_t raw_addr_val, const uint32_t alignment) {
  DEBUG_ASSERT(alignment >= kMinAlignment, DebugAssert{});
  DEBUG_ASSERT(alignment < kMaxShiftVal, DebugAssert{});
  DEBUG_ASSERT((alignment & (alignment - 1)) == 0, DebugAssert{}); // power of two
  auto aligned_addr = Align(raw_addr_val, alignment);
  if (aligned_addr - raw_addr_val < kMetadataSize) {
    return aligned_addr + alignment;
  }
  return aligned_addr;
}
void SetMetadata(void* aligned_ptr, const OffsetAllocator::NodeIndex metadata) {
  auto metadata_cast_ptr = reinterpret_cast<OffsetAllocator::NodeIndex*>(aligned_ptr);
  metadata_cast_ptr[-1] = metadata;
}
auto GetMetadata(const void* aligned_ptr) {
  static_assert(sizeof(OffsetAllocator::NodeIndex) == 4);
  auto metadata_cast_ptr = reinterpret_cast<const OffsetAllocator::NodeIndex*>(aligned_ptr);
  return metadata_cast_ptr[-1];
}
void SetShift(const std::uintptr_t raw_addr, const std::uintptr_t aligned_addr, void* aligned_ptr) {
  static_assert(sizeof(ShiftType) == 2);
  const auto shift = aligned_addr - raw_addr;
  DEBUG_ASSERT(shift < kMaxShiftVal, DebugAssert{});
  auto shift_cast_ptr = reinterpret_cast<ShiftType*>(aligned_ptr);
  shift_cast_ptr[-3] = 0xFFFF & shift;
}
auto GetShift(const void* aligned_ptr) {
  auto shift_cast_ptr = reinterpret_cast<const ShiftType*>(aligned_ptr);
  auto shift = GetUint32(shift_cast_ptr[-3]);
  if (shift == 0) {
    shift = kMaxShiftVal;
  }
  return shift;
}
auto GetRawAddr(const std::uintptr_t aligned_addr, const uint32_t shift) {
  return aligned_addr - shift;
}
AllocatorData* GetAllocatorData(void* buffer, const uint32_t buffer_size_in_bytes) {
  const auto head_addr = reinterpret_cast<std::uintptr_t>(buffer);
  const auto aligned_head_addr = Align(head_addr, alignof(AllocatorData));
  auto allocator_data = static_cast<AllocatorData*>(reinterpret_cast<void*>(aligned_head_addr));
  const auto offset_allocator_addr = Align(aligned_head_addr + sizeof(AllocatorData), alignof(OffsetAllocator::Allocator));
  allocator_data->offset_allocator = new (reinterpret_cast<void*>(offset_allocator_addr)) OffsetAllocator::Allocator(buffer_size_in_bytes, kMaxNodeIndex);
  allocator_data->head_addr = offset_allocator_addr + sizeof(OffsetAllocator::Allocator);
  allocator_data->size = buffer_size_in_bytes - GetUint32(allocator_data->head_addr - head_addr);
  return allocator_data;
}
} // namespace
namespace boke {
void InitAllocator(void* buffer, const uint32_t buffer_size_in_bytes) {
  allocator = GetAllocatorData(buffer, buffer_size_in_bytes);
}
void* Allocate(const uint32_t size, const uint32_t alignment) {
  const auto valid_alignment = std::max(alignment, kMinAlignment);
  const auto total_size = size + valid_alignment + kMetadataSize - 1;
  const auto allocation = allocator->offset_allocator->allocate(total_size);
  DEBUG_ASSERT(allocation.offset != OffsetAllocator::Allocation::NO_SPACE, DebugAssert());
  DEBUG_ASSERT(allocation.metadata != OffsetAllocator::Allocation::NO_SPACE, DebugAssert());
  const auto raw_addr = allocator->head_addr + allocation.offset;
  DEBUG_ASSERT(raw_addr + total_size <= allocator->head_addr + allocator->size, DebugAssert{});
  const auto aligned_addr = GetAlignedAddr(raw_addr, valid_alignment);
  DEBUG_ASSERT(aligned_addr + size <= raw_addr + total_size, DebugAssert{});
  auto aligned_ptr = reinterpret_cast<void*>(aligned_addr);
  SetMetadata(aligned_ptr, allocation.metadata);
  SetShift(raw_addr, aligned_addr, aligned_ptr);
  DEBUG_ASSERT(GetMetadata(aligned_ptr) == allocation.metadata, DebugAssert{});
  DEBUG_ASSERT(GetShift(aligned_ptr) == aligned_addr - raw_addr, DebugAssert{});
  return aligned_ptr;
}
void Deallocate(void* ptr) {
  if (ptr == nullptr) { return; }
  const auto metadata = GetMetadata(ptr);
  const auto shift = GetShift(ptr);
  const auto aligned_addr = reinterpret_cast<std::uintptr_t>(ptr);
  const auto raw_addr = GetRawAddr(aligned_addr, shift);
  const auto offset = GetUint32(raw_addr - allocator->head_addr);
  DEBUG_ASSERT(offset < allocator->size, DebugAssert());
  DEBUG_ASSERT(metadata < kMaxNodeIndex, DebugAssert());
  allocator->offset_allocator->free({.offset = offset, .metadata = metadata});
}
} // namespace boke
#include "doctest/doctest.h"
TEST_CASE("aligned address") {
  using namespace boke;
  CHECK_EQ(Align(123u, 8u), 128u);
  CHECK_EQ(GetAlignedAddr(0ul, 8ul), 8ul);
  CHECK_EQ(GetAlignedAddr(1ul, 8ul), 8ul);
  CHECK_EQ(GetAlignedAddr(2ul, 8ul), 8ul);
  CHECK_EQ(GetAlignedAddr(3ul, 8ul), 16ul);
  CHECK_EQ(GetAlignedAddr(4ul, 8ul), 16ul);
  CHECK_EQ(GetAlignedAddr(5ul, 8ul), 16ul);
  CHECK_EQ(GetAlignedAddr(6ul, 8ul), 16ul);
  CHECK_EQ(GetAlignedAddr(7ul, 8ul), 16ul);
  CHECK_EQ(GetAlignedAddr(8ul, 8ul), 16ul);
  CHECK_EQ(GetAlignedAddr(9ul, 8ul), 16ul);
  CHECK_EQ(GetAlignedAddr(10ul, 8ul), 16ul);
  CHECK_EQ(GetAlignedAddr(11ul, 8ul), 24ul);
  CHECK_EQ(GetAlignedAddr(12ul, 8ul), 24ul);
  CHECK_EQ(GetAlignedAddr(13ul, 8ul), 24ul);
  CHECK_EQ(GetAlignedAddr(14ul, 8ul), 24ul);
  CHECK_EQ(GetAlignedAddr(15ul, 8ul), 24ul);
  CHECK_EQ(GetAlignedAddr(16ul, 8ul), 24ul);
  CHECK_EQ(GetAlignedAddr(17ul, 8ul), 24ul);
  CHECK_EQ(GetAlignedAddr(18ul, 8ul), 24ul);
  CHECK_EQ(GetAlignedAddr(19ul, 8ul), 32ul);
  CHECK_EQ(GetAlignedAddr(20ul, 8ul), 32ul);
  CHECK_EQ(GetAlignedAddr(21ul, 8ul), 32ul);
  CHECK_EQ(GetAlignedAddr(22ul, 8ul), 32ul);
  CHECK_EQ(GetAlignedAddr(23ul, 8ul), 32ul);
  CHECK_EQ(GetAlignedAddr(24ul, 8ul), 32ul);
  CHECK_EQ(GetAlignedAddr(25ul, 8ul), 32ul);
  CHECK_EQ(GetAlignedAddr(26ul, 8ul), 32ul);
  CHECK_EQ(GetAlignedAddr(27ul, 8ul), 40ul);
  CHECK_EQ(GetAlignedAddr(28ul, 8ul), 40ul);
  CHECK_EQ(GetAlignedAddr(29ul, 8ul), 40ul);
  CHECK_EQ(GetAlignedAddr(30ul, 8ul), 40ul);
  CHECK_EQ(GetAlignedAddr(31ul, 8ul), 40ul);
  CHECK_EQ(GetAlignedAddr(32ul, 8ul), 40ul);
  CHECK_EQ(GetAlignedAddr(33ul, 8ul), 40ul);
  CHECK_EQ(GetAlignedAddr(34ul, 8ul), 40ul);
  CHECK_EQ(GetAlignedAddr(35ul, 8ul), 48ul);
  CHECK_EQ(GetAlignedAddr(0ul, 16ul), 16ul);
  CHECK_EQ(GetAlignedAddr(1ul, 16ul), 16ul);
  CHECK_EQ(GetAlignedAddr(2ul, 16ul), 16ul);
  CHECK_EQ(GetAlignedAddr(3ul, 16ul), 16ul);
  CHECK_EQ(GetAlignedAddr(4ul, 16ul), 16ul);
  CHECK_EQ(GetAlignedAddr(5ul, 16ul), 16ul);
  CHECK_EQ(GetAlignedAddr(6ul, 16ul), 16ul);
  CHECK_EQ(GetAlignedAddr(7ul, 16ul), 16ul);
  CHECK_EQ(GetAlignedAddr(8ul, 16ul), 16ul);
  CHECK_EQ(GetAlignedAddr(9ul, 16ul), 16ul);
  CHECK_EQ(GetAlignedAddr(10ul, 16ul), 16ul);
  CHECK_EQ(GetAlignedAddr(11ul, 16ul), 32ul);
  CHECK_EQ(GetAlignedAddr(12ul, 16ul), 32ul);
  CHECK_EQ(GetAlignedAddr(13ul, 16ul), 32ul);
  CHECK_EQ(GetAlignedAddr(14ul, 16ul), 32ul);
  CHECK_EQ(GetAlignedAddr(15ul, 16ul), 32ul);
  CHECK_EQ(GetAlignedAddr(16ul, 16ul), 32ul);
  CHECK_EQ(GetAlignedAddr(17ul, 16ul), 32ul);
  CHECK_EQ(GetAlignedAddr(18ul, 16ul), 32ul);
  CHECK_EQ(GetAlignedAddr(19ul, 16ul), 32ul);
  CHECK_EQ(GetAlignedAddr(20ul, 16ul), 32ul);
  CHECK_EQ(GetAlignedAddr(21ul, 16ul), 32ul);
  CHECK_EQ(GetAlignedAddr(22ul, 16ul), 32ul);
  CHECK_EQ(GetAlignedAddr(23ul, 16ul), 32ul);
  CHECK_EQ(GetAlignedAddr(24ul, 16ul), 32ul);
  CHECK_EQ(GetAlignedAddr(25ul, 16ul), 32ul);
  CHECK_EQ(GetAlignedAddr(26ul, 16ul), 32ul);
  CHECK_EQ(GetAlignedAddr(27ul, 16ul), 48ul);
  CHECK_EQ(GetAlignedAddr(28ul, 16ul), 48ul);
  CHECK_EQ(GetAlignedAddr(29ul, 16ul), 48ul);
  CHECK_EQ(GetAlignedAddr(30ul, 16ul), 48ul);
  CHECK_EQ(GetAlignedAddr(31ul, 16ul), 48ul);
  CHECK_EQ(GetAlignedAddr(32ul, 16ul), 48ul);
  CHECK_EQ(GetAlignedAddr(33ul, 16ul), 48ul);
  CHECK_EQ(GetAlignedAddr(34ul, 16ul), 48ul);
  CHECK_EQ(GetAlignedAddr(35ul, 16ul), 48ul);
}
TEST_CASE("allocation") {
  using namespace boke;
  std::byte buffer[2048];
  const auto raw_addr_root = reinterpret_cast<std::uintptr_t>(buffer);
  uint32_t alignment_list[] = {1,2,4,8,16,256,512,};
  for (uint32_t i = 0; i < 7; i++) {
    CAPTURE(alignment_list[i]);
    const auto alignment = alignment_list[i];
    auto raw_addr = raw_addr_root;
    while (raw_addr % alignment != 0) {
      raw_addr++;
    }
    for (uint32_t j = 0; j <= alignment; j++) {
      CAPTURE(j);
      auto aligned_addr = GetAlignedAddr(raw_addr, std::max(alignment, kMinAlignment));
      CHECK_EQ(aligned_addr % alignment, 0);
      CHECK_LE(raw_addr + kMetadataSize, aligned_addr);
      auto aligned_ptr = reinterpret_cast<void*>(aligned_addr);
      SetMetadata(aligned_ptr, 123);
      SetShift(raw_addr, aligned_addr, aligned_ptr);
      CHECK_EQ(GetMetadata(aligned_ptr), 123);
      CHECK_EQ(GetShift(aligned_ptr), aligned_addr - raw_addr);
      CHECK_EQ(GetRawAddr(aligned_addr, GetShift(aligned_ptr)), raw_addr);
      raw_addr++;
    }
  }
}
TEST_CASE("AllocationData") {
  using namespace boke;
  const uint32_t buffer_size = 16 * 1024;
  std::byte buffer[buffer_size];
  InitAllocator(buffer, buffer_size);
  CHECK_NE(allocator, nullptr);
  uint32_t alignment_list[] = {1,2,4,8,16,256,512,};
  uint32_t alloc_size_list[] = {1,2,3,5,8,128,255,511,512,513,};
  for (uint32_t i = 0; i < 7; i++) {
    const auto alignment = alignment_list[i];
    CAPTURE(alignment);
    void* ptr_list[10]{};
    for (uint32_t j = 0; j < 10; j++) {
      const uint32_t alloc_size = alloc_size_list[j];
      CAPTURE(alloc_size);
      auto ptr = Allocate(alloc_size, alignment);
      CHECK_NE(ptr, nullptr);
      CHECK_EQ(reinterpret_cast<std::uintptr_t>(ptr) % alignment, 0);
      CHECK_LE(reinterpret_cast<std::uintptr_t>(ptr) + alloc_size, allocator->head_addr + allocator->size);
      for (uint32_t k = 0; k < j; k++) {
        CAPTURE(k);
        CAPTURE(reinterpret_cast<std::uintptr_t>(ptr_list[k]));
        CAPTURE(reinterpret_cast<std::uintptr_t>(ptr));
        CAPTURE(ptr_list[k] < ptr);
        if (ptr_list[k] < ptr) {
          CHECK_LE(reinterpret_cast<std::uintptr_t>(ptr_list[k]) + alloc_size_list[k], reinterpret_cast<std::uintptr_t>(ptr));
        } else {
          CHECK_GE(reinterpret_cast<std::uintptr_t>(ptr) + alloc_size, reinterpret_cast<std::uintptr_t>(ptr_list[k]));
        }
      }
      ptr_list[j] = ptr;
    }
    for (uint32_t j = 0; j < 10; j++) {
      Deallocate(ptr_list[j]);
    }
  }
}
