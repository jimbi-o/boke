#include "d3d12_util.h"
#include "boke/str_hash.h"
namespace boke {
void SetD3d12Name(ID3D12Object* obj, const char* const name) {
  const uint32_t name_len = 64;
  wchar_t wname[name_len];
  swprintf(wname, name_len, L"%hs", name);
  obj->SetName(wname);
}
void SetD3d12Name(ID3D12Object* obj, const StrHash hash) {
  SetD3d12Name(obj, GetStr(hash));
}
void SetD3d12Name(ID3D12Object* obj, const StrHash hash, const uint32_t index) {
  const uint32_t name_len = 64;
  wchar_t wname[name_len];
  swprintf(wname, name_len, L"%hs_%d", GetStr(hash), index);
  obj->SetName(wname);
}
}
