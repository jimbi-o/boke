#include "json.h"
#include <windows.h>
#include "boke/allocator.h"
#include "boke/file.h"
namespace boke {
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
char* LoadFileToBuffer(const char* const filepath, boke::AllocatorData* allocator_data) {
  using namespace boke;
  auto file = OpenFile(filepath);
  auto file_size = GetFileSize(file);
  auto buffer = AllocateArray<char>(file_size + 1, allocator_data);
  const auto read_size = ReadFileToBuffer(file, file_size, buffer);
  if (read_size != file_size) {
    Deallocate(buffer, allocator_data);
    buffer = nullptr;
  }
  buffer[read_size] = '\0';
  CloseFile(file);
  return buffer;
}
} // namespace boke
