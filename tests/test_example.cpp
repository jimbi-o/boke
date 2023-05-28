#include "boke/allocator.h"
#include "boke/container.h"
#include "boke/debug_assert.h"
#include "boke/str_hash.h"
#include "boke/util.h"
#include <doctest/doctest.h>
namespace {
static const uint32_t main_buffer_size_in_bytes = 32 * 1024 * 1024;
static std::byte main_buffer[main_buffer_size_in_bytes];
} // namespace
TEST_CASE("log") {
  spdlog::info("hello {}", "world");
  CHECK_UNARY(true);
}
TEST_CASE("assert") {
  using namespace boke;
  DEBUG_ASSERT(true, DebugAssert());
}
TEST_CASE("json") {
  using namespace rapidjson;
  const char json[] = R"--({"project":"rapidjson","stars":10})--";
  // Could use `GenericDocument<rapidjson::UTF8<>, CustomAllocator> doc` instead,
  // but free() function must be a static function.
  Document d;
  d.Parse(json);
  CHECK_EQ(d["stars"], 10);
}
