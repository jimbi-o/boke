#pragma once
#include "d3d12_name_alias.h"
namespace boke {
struct GfxLibraries {
  HMODULE dxgi_library{};
  HMODULE d3d12_library{};
};
struct DxgiCore {
  DxgiFactory* factory{};
  DxgiAdapter* adapter{};
};
enum AdapterType : uint8_t { kHighPerformance, kWarp, };
GfxLibraries LoadGfxLibraries();
void ReleaseGfxLibraries(GfxLibraries&);
DxgiCore InitDxgi(HMODULE dxgi_library, const AdapterType);
void TermDxgi(DxgiCore&);
D3d12Device* CreateDevice(HMODULE d3d12_library, DxgiAdapter*);
}
