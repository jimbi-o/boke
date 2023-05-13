#include "json.h"
#include <windows.h>
#include "boke/allocator.h"
#include "boke/debug_assert.h"
#include "boke/file.h"
namespace {
HANDLE OpenFile(const char* filename) {
  auto file = CreateFile(filename, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
  return file;
}
void CloseFile(HANDLE file) {
  CloseHandle(file);
}
DWORD GetFileSize(HANDLE file) {
  if (file == INVALID_HANDLE_VALUE) { return DWORD{}; }
  LARGE_INTEGER file_size{};
  if (!GetFileSizeEx(file, &file_size)) { return DWORD{}; }
  return static_cast<DWORD>(file_size.QuadPart);
}
DWORD ReadFileToBuffer(HANDLE file, const DWORD file_size, LPVOID buffer) {
  DWORD bytes_read{};
  ReadFile(file, buffer, file_size, &bytes_read, NULL);
  return bytes_read;
}
char* LoadFileToBufferImpl(const char* const filepath, boke::AllocatorData* allocator_data, uint32_t* bytes_read) {
  using namespace boke;
  auto file = OpenFile(filepath);
  DEBUG_ASSERT(file != 0, DebugAssert{});
  auto file_size = GetFileSize(file);
  auto buffer = AllocateArray<char>(file_size + 1, allocator_data);
  const auto read_size = ReadFileToBuffer(file, file_size, buffer);
  if (read_size != file_size) {
    Deallocate(buffer, allocator_data);
    buffer = nullptr;
  }
  buffer[read_size] = '\0';
  CloseFile(file);
  if (bytes_read) {
    *bytes_read = read_size;
  }
  return buffer;
}
} // namespace
namespace boke {
char* LoadFileToBuffer(const char* const filepath, boke::AllocatorData* allocator_data) {
  return LoadFileToBufferImpl(filepath, allocator_data, nullptr);
}
char* LoadFileToBuffer(const char* const filepath, boke::AllocatorData* allocator_data, uint32_t* bytes_read) {
  return LoadFileToBufferImpl(filepath, allocator_data, bytes_read);
}
} // namespace boke
#include "doctest/doctest.h"
TEST_CASE("read file") {
  using namespace boke;
  const uint32_t main_buffer_size_in_bytes = 16 * 1024;
  std::byte main_buffer[main_buffer_size_in_bytes];
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
  const uint32_t main_buffer_size_in_bytes = 16 * 1024;
  std::byte main_buffer[main_buffer_size_in_bytes];
  const char filepath[] = "tests/test.json";
  auto allocator_data = GetAllocatorData(main_buffer, main_buffer_size_in_bytes);
  auto buffer = LoadFileToBuffer(filepath, allocator_data);
  CHECK_NE(buffer, nullptr);
}
