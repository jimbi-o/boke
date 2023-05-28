#include "boke/str_hash.h"
#include "boke/allocator.h"
#include "boke/debug_assert.h"
#include "boke/util.h"
namespace {
using namespace boke;
foonathan::string_id::default_database* str_hash_database{};
foonathan::string_id::default_database* CreateStringHashDatabase() {
  auto ptr = Allocate(GetUint32(sizeof(foonathan::string_id::default_database)), alignof(foonathan::string_id::default_database));
  return new (ptr) foonathan::string_id::default_database();
}
foonathan::string_id::string_id GetSid(const char* const str, foonathan::string_id::default_database* database) {
  return foonathan::string_id::string_id(foonathan::string_id::string_info(str), *database);
}
foonathan::string_id::string_id GetSid(const char* const str, const size_t str_len, foonathan::string_id::default_database* database) {
  return foonathan::string_id::string_id(foonathan::string_id::string_info(str, str_len), *database);
}
const char* GetStringFromHash(foonathan::string_id::hash_type hash, foonathan::string_id::default_database* database) {
  return database->lookup(hash);
}
}
namespace boke {
void InitStrHashSystem() {
  DEBUG_ASSERT(str_hash_database == nullptr, DebugAssert{});
  str_hash_database = CreateStringHashDatabase();
}
void TermStrHashSystem() {
  Deallocate(str_hash_database);
  str_hash_database = nullptr;
}
StrHash GetStrHash(const char* const str) {
  if (str_hash_database == nullptr) {
    return foonathan::string_id::detail::sid_hash(str);
  }
  const auto sid = GetSid(str, str_hash_database);
  return sid.hash_code();
}
const char* GetStr(const StrHash hash) {
  if (str_hash_database == nullptr) {
    return "";
  }
  return GetStringFromHash(hash, str_hash_database);
}
} // namespace boke
#include "doctest/doctest.h"
TEST_CASE("string hash impl") {
  const uint32_t buffer_size_in_bytes = 16 * 1024;
  std::byte buffer[buffer_size_in_bytes];
  using namespace boke;
  InitAllocator(buffer, buffer_size_in_bytes);
  auto string_hash_database = CreateStringHashDatabase();
  char test_string[] = "test string";
  auto sid = GetSid(test_string, string_hash_database);
  CHECK_UNARY(strcmp(GetStringFromHash(sid.hash_code(), string_hash_database), test_string) == 0);
  auto sid2 = GetSid("test string", string_hash_database);
  CHECK_UNARY(strcmp(GetStringFromHash(sid2.hash_code(), string_hash_database), test_string) == 0);
  auto sid3 = GetSid("test string 2", string_hash_database);
  CHECK_UNARY_FALSE(strcmp(GetStringFromHash(sid3.hash_code(), string_hash_database), test_string) == 0);
}
TEST_CASE("str hash") {
  const uint32_t buffer_size_in_bytes = 16 * 1024;
  std::byte buffer[buffer_size_in_bytes];
  using namespace boke;
  InitAllocator(buffer, buffer_size_in_bytes);
  InitStrHashSystem();
  const char str[] = "hello world";
  const auto hash = GetStrHash(str);
  CHECK_NE(hash, kEmptyStr);
  CHECK_NE(str, GetStr(hash));
  CHECK_EQ(strcmp(str, GetStr(hash)), 0);
  TermStrHashSystem();
}
