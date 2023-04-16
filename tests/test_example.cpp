#include "boke/allocator.h"
#include "boke/container.h"
#include "boke/debug_assert.h"
#include "boke/str_hash.h"
#include "boke/util.h"
#include <doctest/doctest.h>
namespace {
static const uint32_t buffer_size_in_bytes = 32 * 1024 * 1024;
static std::byte buffer[buffer_size_in_bytes];
} // namespace
TEST_CASE("log") {
  spdlog::info("hello {}", "world");
  CHECK_UNARY(true);
}
TEST_CASE("array with custom allocation") {
  using namespace boke;
  auto allocator_data = GetAllocatorData(buffer, buffer_size_in_bytes);
  Array<uint32_t> array(GetAllocatorCallbacks(allocator_data));
  array.push_back(1);
  array.push_back(3);
  array.push_back(5);
  array.push_back(7);
  array.push_back(9);
  array.push_back(101);
  array.push_back(102);
  Array<uint32_t> array2(GetAllocatorCallbacks(allocator_data));
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
TEST_CASE("string hash map") {
  using namespace boke;
  auto allocator_data = GetAllocatorData(buffer, buffer_size_in_bytes);
  StrHashMap<uint32_t> map(GetAllocatorCallbacks(allocator_data));
  map.insert("TestA"_id, 0);
  map.insert("TestB"_id, 1);
  map.insert("TestC"_id, 2);
  CHECK_EQ(map["TestA"_id], 0);
  CHECK_EQ(map["TestB"_id], 1);
  CHECK_EQ(map["TestC"_id], 2);
  map.insert("TestA"_id, 3);
  CHECK_EQ(map["TestA"_id], 3);
  CHECK_EQ(map["TestB"_id], 1);
  CHECK_EQ(map["TestC"_id], 2);
  StrHashMap<uint32_t> map2(GetAllocatorCallbacks(allocator_data));
  map2.insert("TestD"_id, 4);
  map2.insert("TestE"_id, 5);
  map2.insert("TestF"_id, 6);
  map.insert("TestD"_id, 7);
  map.insert("TestE"_id, 8);
  map.insert("TestF"_id, 9);
  CHECK_EQ(map["TestA"_id], 3);
  CHECK_EQ(map["TestB"_id], 1);
  CHECK_EQ(map["TestC"_id], 2);
  CHECK_EQ(map["TestD"_id], 7);
  CHECK_EQ(map["TestE"_id], 8);
  CHECK_EQ(map["TestF"_id], 9);
  CHECK_EQ(map2["TestD"_id], 4);
  CHECK_EQ(map2["TestE"_id], 5);
  CHECK_EQ(map2["TestF"_id], 6);
}
TEST_CASE("assert") {
  using namespace boke;
  DEBUG_ASSERT(true, DebugAssert());
}
