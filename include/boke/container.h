#pragma once
#include <stdint.h>
#include "tote/array.h"
#include "tote/hash_map.h"
namespace boke {
struct AllocatorData;
tote::AllocatorCallbacks<AllocatorData> GetAllocatorCallbacks(AllocatorData* allocator_data);
template <typename T> using Array = tote::ResizableArray<T, AllocatorData>;
template <typename T> using StrHashMap = tote::HashMap<foonathan::string_id::hash_type, T, AllocatorData>;
}
