#include "string_util.h"
#include "boke/allocator.h"
#include <cstdlib>
namespace boke {
wchar_t* ConvertAsciiCharToWchar(const char* str) {
  const auto len = static_cast<uint32_t>(strlen(str)) + 1;
  auto ret = AllocateArray<wchar_t>(len);
  mbstowcs(ret, str, len);
  return ret;
}
}
