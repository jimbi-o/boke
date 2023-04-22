#include "boke/framework.h"
#include <windows.h>
#include "boke/allocator.h"
namespace {
static const uint32_t main_buffer_size_in_bytes = 32 * 1024 * 1024;
static std::byte main_buffer[main_buffer_size_in_bytes];
auto OpenFile(const char* filename) {
  auto file = CreateFile(filename, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
  return file;
}
auto CloseFile(HANDLE file) {
  CloseHandle(file);
}
auto GetFileSize(HANDLE file) {
  if (file == INVALID_HANDLE_VALUE) { return DWORD{}; }
  LARGE_INTEGER file_size{};
  if (!GetFileSizeEx(file, &file_size)) { return DWORD{}; }
  return static_cast<DWORD>(file_size.QuadPart);
}
auto ReadFileToBuffer(HANDLE file, const DWORD file_size, LPVOID buffer) {
  DWORD bytes_read{};
  ReadFile(file, buffer, file_size, &bytes_read, NULL);
  return bytes_read;
}
auto LoadFileToBuffer(const char* const filepath, boke::AllocatorData* allocator_data) {
  using namespace boke;
  auto file = OpenFile(filepath);
  auto file_size = GetFileSize(file);
  auto buffer = static_cast<char*>(Allocate(file_size, alignof(char), allocator_data));
  const auto read_size = ReadFileToBuffer(file, file_size, buffer);
  if (read_size != file_size) {
    Deallocate(buffer, allocator_data);
    buffer = nullptr;
  }
  CloseFile(file);
  return buffer;
}
auto GetJson(const char* const json_path, boke::AllocatorData* allocator_data) {
  using namespace boke;
  using namespace rapidjson;
  auto json_text = LoadFileToBuffer(json_path, allocator_data);
  Document d;
  d.Parse(json_text);
  Deallocate(json_text, allocator_data);
  return d;
}
} // namespace
namespace boke {
int32_t Run(const char* const config_path) {
  auto allocator_data = GetAllocatorData(main_buffer, main_buffer_size_in_bytes);
  auto json = GetJson(config_path, allocator_data);
  return 0;
}
} // namespace boke
#include <doctest/doctest.h>
TEST_CASE("read file") {
  using namespace boke;
  const char filepath[] = "tests/test.json";
  auto file = OpenFile(filepath);
  auto file_size = GetFileSize(file);
  REQUIRE_LE(file_size, main_buffer_size_in_bytes);
  auto allocator_data = GetAllocatorData(main_buffer, main_buffer_size_in_bytes);
  auto buffer = static_cast<LPVOID>(Allocate(file_size, alignof(char), allocator_data));
  CHECK_EQ(ReadFileToBuffer(file, file_size, buffer), file_size);
  CloseFile(file);
}
TEST_CASE("load file to buffer") {
  using namespace boke;
  const char filepath[] = "tests/test.json";
  auto allocator_data = GetAllocatorData(main_buffer, main_buffer_size_in_bytes);
  auto buffer = LoadFileToBuffer(filepath, allocator_data);
  CHECK_NE(buffer, nullptr);
}
TEST_CASE("json") {
  using namespace boke;
  const char config_path[] = "tests/test.json";
  auto allocator_data = GetAllocatorData(main_buffer, main_buffer_size_in_bytes);
  auto json = GetJson(config_path, allocator_data);
  CHECK_EQ(json["testval"], 123);
}
TEST_CASE("framework") {
  const char config_path[] = "tests/test.json";
  CHECK_EQ(boke::Run(config_path), 0);
}
