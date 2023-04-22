#include "string_util.h"
#include "boke/allocator.h"
#include <cstdlib>
namespace boke {
wchar_t* ConvertAsciiCharToWchar(const char* str, AllocatorData* allocator_data) {
  const auto len = static_cast<uint32_t>(strlen(str)) + 1;
  auto ret = AllocateArray<wchar_t>(len, allocator_data);
  mbstowcs(ret, str, len);
  return ret;
}
}
