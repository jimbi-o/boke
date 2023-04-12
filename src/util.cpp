#include "boke/util.h"
namespace boke {
uint32_t Align(const uint32_t val, const uint32_t alignment) {
  const auto mask = alignment - 1;
  return (val + mask) & ~mask;
}
}
