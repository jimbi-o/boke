#include "dxgi1_6.h"
#include <D3D12MemAlloc.h>
#include "boke/allocator.h"
#include "boke/container.h"
#include "boke/debug_assert.h"
#include "boke/str_hash.h"
#include "boke/util.h"
#include "core.h"
#include "resources.h"
namespace boke {
void* GpuMemoryAllocatorAllocate(size_t size, size_t alignment, void* private_data) {
  return Allocate(GetUint32(size), GetUint32(alignment), static_cast<AllocatorData*>(private_data));
}
void GpuMemoryAllocatorDeallocate(void* ptr, void* private_data) {
  Deallocate(ptr, static_cast<AllocatorData*>(private_data));
}
auto CreateGpuMemoryAllocator(DxgiAdapter* adapter, D3d12Device* device, AllocatorData* allocator_data) {
  using namespace D3D12MA;
  ALLOCATION_CALLBACKS allocation_callbacks{
    .pAllocate = GpuMemoryAllocatorAllocate,
    .pFree = GpuMemoryAllocatorDeallocate,
    .pPrivateData = allocator_data,
  };
  ALLOCATOR_DESC allocatorDesc{
    .Flags = ALLOCATOR_FLAG_SINGLETHREADED | ALLOCATOR_FLAG_DEFAULT_POOLS_NOT_ZEROED,
    .pDevice = device,
    .PreferredBlockSize = 0,
    .pAllocationCallbacks = &allocation_callbacks,
    .pAdapter = adapter,
  };
  Allocator* allocator;
  const auto hr = CreateAllocator(&allocatorDesc, &allocator);
  DEBUG_ASSERT(SUCCEEDED(hr), DebugAssert{});
  return allocator;
}
auto CreateTexture2d(D3D12MA::Allocator* allocator,
                     const Size2d& size,
                     const DXGI_FORMAT format,
                     const D3D12_RESOURCE_FLAGS flags,
                     const D3D12_BARRIER_LAYOUT layout,
                     const D3D12_CLEAR_VALUE* clear_value,
                     const uint32_t castable_format_num, DXGI_FORMAT* castable_formats) {
  using namespace D3D12MA;
  D3D12_RESOURCE_DESC1 resource_desc{
    .Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D,
    .Alignment = 0,
    .Width = size.width,
    .Height = size.height,
    .DepthOrArraySize = 1,
    .MipLevels = 1,
    .Format = format,
    .SampleDesc = {
      .Count = 1,
      .Quality = 0,
    },
    .Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN,
    .Flags = flags,
  };
  D3D12MA::ALLOCATION_DESC allocation_desc{
    .HeapType = D3D12_HEAP_TYPE_DEFAULT,
  };
  D3D12MA::Allocation* allocation{};
  const auto hr = allocator->CreateResource3(
      &allocation_desc,
      &resource_desc,
      layout,
      clear_value,
      castable_format_num, castable_formats,
      &allocation,
      IID_NULL, nullptr); // pointer to resource
  DEBUG_ASSERT(SUCCEEDED(hr), DebugAssert{});
  return allocation;
}
auto CreateTexture2dRtv(D3D12MA::Allocator* allocator, const Size2d& size, const DXGI_FORMAT format) {
  D3D12_CLEAR_VALUE clear_value{
    .Format = format,
    .Color = {},
  };
  return CreateTexture2d(allocator, size, format,
                         D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
                         D3D12_BARRIER_LAYOUT_RENDER_TARGET,
                         &clear_value,
                         0, nullptr);
}
auto CreateTexture2dDsv(D3D12MA::Allocator* allocator, const Size2d& size, const DXGI_FORMAT format) {
  D3D12_CLEAR_VALUE clear_value{
    .Format = format,
    .DepthStencil = {
      .Depth = 1.0f, // set 0.0f for inverse-z
      .Stencil = 0,
    },
  };
  return CreateTexture2d(allocator, size, format,
                         D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL,
                         D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE,
                         &clear_value,
                         0, nullptr);
}
}
#include "doctest/doctest.h"
TEST_CASE("resources") {
  using namespace boke;
  // core units
  auto gfx_libraries = LoadGfxLibraries();
  auto dxgi = InitDxgi(gfx_libraries.dxgi_library, AdapterType::kHighPerformance);
  auto device = CreateDevice(gfx_libraries.d3d12_library, dxgi.adapter);
  device->Release();
  TermDxgi(dxgi);
  ReleaseGfxLibraries(gfx_libraries);
}
