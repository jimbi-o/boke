#include "Windows.h"
extern "C" { __declspec(dllexport) extern const UINT D3D12SDKVersion = 700;}
extern "C" { __declspec(dllexport) extern const char* D3D12SDKPath = reinterpret_cast<const char*>(u8".\\d3d12\\"); }
