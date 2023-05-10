#include "json.h"
#include <windows.h>
#include "boke/allocator.h"
#include "boke/file.h"
namespace boke {
rapidjson::Document GetJson(const char* const json_path, boke::AllocatorData* allocator_data) {
  using namespace boke;
  using namespace rapidjson;
  auto json_text = LoadFileToBuffer(json_path, allocator_data);
  Document d;
  d.Parse(json_text);
  Deallocate(json_text, allocator_data);
  return d;
}
} // namespace boke
#include "doctest/doctest.h"
TEST_CASE("json") {
  using namespace boke;
  const uint32_t main_buffer_size_in_bytes = 16 * 1024;
  std::byte main_buffer[main_buffer_size_in_bytes];
  const char config_path[] = "tests/test.json";
  auto allocator_data = GetAllocatorData(main_buffer, main_buffer_size_in_bytes);
  auto json = GetJson(config_path, allocator_data);
  CHECK_EQ(json["testval"], 123);
}
