#include <cstddef>
#include "boke/allocator.h"
#include "boke/str_hash.h"
#include <doctest/doctest.h>
namespace {
static const uint32_t buffer_size_in_bytes = 1024 * 1024;
static std::byte buffer[buffer_size_in_bytes];
} // namespace
TEST_CASE("string hash") {
  using namespace boke;
  auto allocator_data = GetAllocatorData(buffer, buffer_size_in_bytes);
  auto string_hash_database = CreateStringHashDatabase(allocator_data);
  char test_string[] = "test string";
  auto sid = GetSid(test_string, string_hash_database);
  CHECK_UNARY(strcmp(GetStringFromHash(sid.hash_code(), string_hash_database), test_string) == 0);
  auto sid2 = GetSid("test string", string_hash_database);
  CHECK_UNARY(strcmp(GetStringFromHash(sid2.hash_code(), string_hash_database), test_string) == 0);
  auto sid3 = GetSid("test string 2", string_hash_database);
  CHECK_UNARY_FALSE(strcmp(GetStringFromHash(sid3.hash_code(), string_hash_database), test_string) == 0);
}
