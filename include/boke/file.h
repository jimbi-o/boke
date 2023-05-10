#pragma once
namespace boke {
char* LoadFileToBuffer(const char* const filepath, boke::AllocatorData* allocator_data);
char* LoadFileToBuffer(const char* const filepath, boke::AllocatorData* allocator_data, uint32_t* bytes_read);
}
