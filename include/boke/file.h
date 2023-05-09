#pragma once
namespace boke {
HANDLE OpenFile(const char* filename);
void CloseFile(HANDLE file);
DWORD GetFileSize(HANDLE file);
DWORD ReadFileToBuffer(HANDLE file, const DWORD file_size, LPVOID buffer);
char* LoadFileToBuffer(const char* const filepath, boke::AllocatorData* allocator_data);
}
