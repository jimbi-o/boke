#pragma once
#include <stdint.h>
namespace boke {
template <typename T> auto GetUint32(const T v) { return static_cast<uint32_t>(v); }
uintptr_t Align(const uintptr_t val, const uint32_t alignment/*power of two*/);
uint32_t Align(const uint32_t val, const uint32_t alignment/*power of two*/);
}
