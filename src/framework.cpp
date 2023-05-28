#include "boke/framework.h"
#include "boke/allocator.h"
#include "json.h"
namespace {
static const uint32_t main_buffer_size_in_bytes = 32 * 1024 * 1024;
static std::byte main_buffer[main_buffer_size_in_bytes];
} // namespace
namespace boke {
int32_t Run(const char* const config_path) {
  InitAllocator(main_buffer, main_buffer_size_in_bytes);
  auto json = GetJson(config_path);
  return 0;
}
} // namespace boke
#include <doctest/doctest.h>
TEST_CASE("framework") {
  const char config_path[] = "tests/test.json";
  CHECK_EQ(boke::Run(config_path), 0);
}
