#pragma once
#include "boke/str_hash.h"
namespace boke {
void SetD3d12Name(ID3D12Object*, const char* const);
void SetD3d12Name(ID3D12Object*, const StrHash hash);
void SetD3d12Name(ID3D12Object*, const StrHash hash, const uint32_t index);
}
