#pragma once
namespace boke {
struct AllocatorData;
AllocatorData* GetAllocatorData(void* buffer, const uint32_t buffer_size_in_bytes, const uint32_t alignment = 8);
void* Allocate(const uint32_t size_in_bytes, AllocatorData*);
void Deallocate(void* ptr, AllocatorData*);
}
