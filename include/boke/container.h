#pragma once
#include "tote/array.h"
#include "tote/hash_map.h"
namespace boke {
struct AllocatorData;
template <typename T> using Array = tote::ResizableArray<T, AllocatorData>;
tote::AllocatorCallbacks<AllocatorData> GetAllocatorCallbacks(AllocatorData* allocator_data);
using namespace foonathan::string_id::literals;
template <typename T> using StrHashMap = tote::HashMap<foonathan::string_id::hash_type, T, AllocatorData>;
}
