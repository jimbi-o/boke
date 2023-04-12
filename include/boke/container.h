#pragma once
#include "tote/array.h"
namespace boke {
struct AllocatorData;
template <typename T> using Array = tote::ResizableArray<uint32_t, AllocatorData>;
tote::AllocatorCallbacks<AllocatorData> GetAllocatorCallbacks(AllocatorData* allocator_data);
}
