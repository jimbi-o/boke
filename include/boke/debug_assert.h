#pragma once
#include <stdint.h>
#include "debug_assert.hpp"
namespace boke {
struct DebugAssert
    : debug_assert::default_handler // use the default handler
    , debug_assert::set_level<~0U>  // level -1, i.e. all assertions, 0 would mean none, 1 would be level 1, 2 level 2 or lower,...
{};
} // namespace boke
