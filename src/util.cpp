#include "boke/util.h"
namespace boke {
template <typename T>
T Align(const T val, const T alignment) {
  const auto mask = alignment - 1;
  return (val + mask) & ~mask;
}
uintptr_t Align(const uintptr_t val, const uint32_t alignment) {
  return Align<uintptr_t>(val, alignment);
}
uint32_t Align(const uint32_t val, const uint32_t alignment) {
  return Align<uint32_t>(val, alignment);
}
}
