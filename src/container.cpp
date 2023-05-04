#include "boke/allocator.h"
#include "boke/container.h"
#include "boke/util.h"
namespace boke {
tote::AllocatorCallbacks<AllocatorData> GetAllocatorCallbacks(AllocatorData* allocator_data) {
  return tote::AllocatorCallbacks<AllocatorData> {
    .allocate = Allocate,
    .deallocate = Deallocate,
    .user_context = allocator_data,
  };
}
StrHash GetStrHash(const char* const str) {
  return foonathan::string_id::detail::sid_hash(str);
}
} // namespace boke
