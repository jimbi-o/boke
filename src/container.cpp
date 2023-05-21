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
} // namespace boke
