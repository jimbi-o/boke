#include "dxgi1_6.h"
#include "boke/allocator.h"
#include "boke/container.h"
#include "boke/debug_assert.h"
#include "boke/str_hash.h"
#include "boke/util.h"
#include "core.h"
#include "descriptors.h"
#include "json.h"
#include "render_pass_info.h"
#include "resources.h"
namespace boke {
namespace {
struct HandleIndex {
  union {
    uint32_t rtv;
    uint32_t dsv;
  };
  uint32_t srv;
};
} // namespace
struct DescriptorHandles {
  StrHashMap<HandleIndex>* handle_index;
  ResizableArray<D3D12_CPU_DESCRIPTOR_HANDLE>* rtv_handles;
  ResizableArray<D3D12_CPU_DESCRIPTOR_HANDLE>* dsv_handles;
  ResizableArray<D3D12_CPU_DESCRIPTOR_HANDLE>* cbv_srv_uav_handles;
};
}
namespace {
using namespace boke;
auto GetDescriptorHandleIncrementSize(D3d12Device* device) {
  return DescriptorHandleIncrementSize{
    .rtv = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV),
    .dsv = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV),
    .cbv_srv_uav = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV),
  };
}
void CountDescriptorHandleNumImpl(DescriptorHandleNum* descriptor_handle_num, const StrHash, const ResourceInfo* resource_info) {
  if (resource_info->flags == D3D12_RESOURCE_FLAG_NONE) { return; }
  if (resource_info->flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET) {
    descriptor_handle_num->rtv += resource_info->physical_resource_num;
  }
  if (resource_info->flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL) {
    descriptor_handle_num->dsv += resource_info->physical_resource_num;
  }
  if (!(resource_info->flags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE)) {
    descriptor_handle_num->cbv_srv_uav += resource_info->physical_resource_num;
  }
}
auto CountDescriptorHandleNum(const StrHashMap<ResourceInfo>& resouce_info) {
  DescriptorHandleNum descriptor_handle_num{};
  resouce_info.iterate<DescriptorHandleNum>(CountDescriptorHandleNumImpl, &descriptor_handle_num);
  return descriptor_handle_num;
}
auto CreateDescriptorHeapsImpl(D3d12Device* device, const DescriptorHandleNum& descriptor_handle_num) {
  DescriptorHeaps descriptor_heaps{
    .rtv = CreateDescriptorHeap(device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, descriptor_handle_num.rtv, D3D12_DESCRIPTOR_HEAP_FLAG_NONE),
    .dsv = CreateDescriptorHeap(device, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, descriptor_handle_num.dsv, D3D12_DESCRIPTOR_HEAP_FLAG_NONE),
    .cbv_srv_uav = CreateDescriptorHeap(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, descriptor_handle_num.cbv_srv_uav, D3D12_DESCRIPTOR_HEAP_FLAG_NONE),
  };
  descriptor_heaps.rtv->SetName(L"descriptor_heaps_rtv");
  descriptor_heaps.dsv->SetName(L"descriptor_heaps_dsv");
  descriptor_heaps.cbv_srv_uav->SetName(L"descriptor_heaps_cbv_srv_uav");
  return descriptor_heaps;
}
auto GetDescriptorHeapHeadAddr(const DescriptorHeaps& descriptor_heaps) {
  return DescriptorHeapHeadAddr{
    .rtv = descriptor_heaps.rtv->GetCPUDescriptorHandleForHeapStart(),
    .dsv = descriptor_heaps.dsv->GetCPUDescriptorHandleForHeapStart(),
    .cbv_srv_uav = descriptor_heaps.cbv_srv_uav->GetCPUDescriptorHandleForHeapStart(),
  };
}
auto GetRtvDesc2d(const DXGI_FORMAT format) {
  return D3D12_RENDER_TARGET_VIEW_DESC{
    .Format = format,
    .ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D,
    .Texture2D = {
      .MipSlice = 0,
      .PlaneSlice = 0,
    },
  };
}
auto GetDsvDesc2d(const DXGI_FORMAT format) {
  return D3D12_DEPTH_STENCIL_VIEW_DESC{
    .Format = format,
    .ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D,
    .Flags = D3D12_DSV_FLAG_NONE, // read only dsv not implemented.
    .Texture2D = {
      .MipSlice = 0,
    },
  };
}
auto GetSrvValidFormat(const DXGI_FORMAT format) {
  switch (format) {
    case DXGI_FORMAT_D24_UNORM_S8_UINT:
      return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    default:
      return format;
  }
}
auto GetSrvDesc2d(const DXGI_FORMAT format) {
  return D3D12_SHADER_RESOURCE_VIEW_DESC{
    .Format = GetSrvValidFormat(format),
    .ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D,
    .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
    .Texture2D = {
      .MostDetailedMip = 0,
      .MipLevels = 1,
      .PlaneSlice = 0,
      .ResourceMinLODClamp = 0.0f,
    },
  };
}
auto GetDescriptorHandle(const D3D12_CPU_DESCRIPTOR_HANDLE& head_addr, const uint32_t increment_size, const uint32_t index) {
  return D3D12_CPU_DESCRIPTOR_HANDLE{
    .ptr = head_addr.ptr + increment_size * index,
  };
}
struct DescriptorHandleImplAsset {
  const ResourceSet* resource_set;
  D3d12Device* device{};
  const DescriptorHeapHeadAddr& descriptor_heap_head_addr{};
  const DescriptorHandleIncrementSize& descriptor_handle_increment_size{};
  DescriptorHandles* descriptor_handles;
};
void PrepareDescriptorHandlesImpl(DescriptorHandleImplAsset* asset, const StrHash resource_id, const ResourceInfo* resource_info) {
  if (resource_info->flags == D3D12_RESOURCE_FLAG_NONE) { return; }
  ID3D12Resource* resource[2];
  for (uint32_t i = 0; i < resource_info->physical_resource_num; i++) {
    resource[i] = GetResource(asset->resource_set, resource_id, i);
  }
  if (resource_info->flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET) {
    AddDescriptorHandlesRtv(resource_id, resource_info->format, resource, resource_info->physical_resource_num, asset->device, asset->descriptor_heap_head_addr, asset->descriptor_handle_increment_size, asset->descriptor_handles);
  }
  if (resource_info->flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL) {
    DEBUG_ASSERT(resource_info->physical_resource_num == 1, DebugAssert{});
    AddDescriptorHandlesDsv(resource_id, resource_info->format, resource, resource_info->physical_resource_num, asset->device, asset->descriptor_heap_head_addr, asset->descriptor_handle_increment_size, asset->descriptor_handles);
  }
  if (!(resource_info->flags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE)) {
    AddDescriptorHandlesSrv(resource_id, resource_info->format, resource, resource_info->physical_resource_num, asset->device, asset->descriptor_heap_head_addr, asset->descriptor_handle_increment_size, asset->descriptor_handles);
  }
}
} // namespace
namespace boke {
DescriptorHeapSet CreateDescriptorHeaps(const StrHashMap<ResourceInfo>& resource_info, D3d12Device* device, const DescriptorHandleNum& extra_handle_num) {
  const auto descriptor_handle_increment_size = GetDescriptorHandleIncrementSize(device);
  auto descriptor_handle_num = CountDescriptorHandleNum(resource_info);
  descriptor_handle_num.rtv += extra_handle_num.rtv;
  descriptor_handle_num.dsv += extra_handle_num.dsv;
  descriptor_handle_num.cbv_srv_uav += extra_handle_num.cbv_srv_uav;
  auto descriptor_heaps = CreateDescriptorHeapsImpl(device, descriptor_handle_num);
  auto descriptor_heap_head_addr = GetDescriptorHeapHeadAddr(descriptor_heaps);
  return {
    .descriptor_heaps = descriptor_heaps,
    .head_addr = descriptor_heap_head_addr,
    .increment_size = descriptor_handle_increment_size,
  };
}
void ReleaseDescriptorHeaps(DescriptorHeapSet& descriptor_heaps) {
  descriptor_heaps.descriptor_heaps.rtv->Release();
  descriptor_heaps.descriptor_heaps.dsv->Release();
  descriptor_heaps.descriptor_heaps.cbv_srv_uav->Release();
}
DescriptorHandles* PrepareDescriptorHandles(const StrHashMap<ResourceInfo>& resource_info, const ResourceSet* resource_set, D3d12Device* device, const DescriptorHeapHeadAddr& descriptor_heap_head_addr, const DescriptorHandleIncrementSize& descriptor_handle_increment_size) {
  auto descriptor_handles = New<DescriptorHandles>();
  descriptor_handles->handle_index = New<StrHashMap<HandleIndex>>();
  descriptor_handles->rtv_handles = New<ResizableArray<D3D12_CPU_DESCRIPTOR_HANDLE>>();
  descriptor_handles->dsv_handles = New<ResizableArray<D3D12_CPU_DESCRIPTOR_HANDLE>>();
  descriptor_handles->cbv_srv_uav_handles = New<ResizableArray<D3D12_CPU_DESCRIPTOR_HANDLE>>();
  DescriptorHandleImplAsset asset {
    .resource_set = resource_set,
    .device = device,
    .descriptor_heap_head_addr = descriptor_heap_head_addr,
    .descriptor_handle_increment_size = descriptor_handle_increment_size,
    .descriptor_handles = descriptor_handles,
  };
  resource_info.iterate<DescriptorHandleImplAsset>(PrepareDescriptorHandlesImpl, &asset);
  return descriptor_handles;
}
void ReleaseDescriptorHandles(DescriptorHandles* descriptor_handles) {
  Deallocate(descriptor_handles->handle_index);
  Deallocate(descriptor_handles->rtv_handles);
  Deallocate(descriptor_handles->dsv_handles);
  Deallocate(descriptor_handles->cbv_srv_uav_handles);
  Deallocate(descriptor_handles);
}
void AddDescriptorHandlesRtv(const StrHash resource_id, DXGI_FORMAT format, ID3D12Resource** resources, const uint32_t resource_num, D3d12Device* device, const DescriptorHeapHeadAddr& descriptor_heap_head_addr, const DescriptorHandleIncrementSize& descriptor_handle_increment_size, DescriptorHandles* descriptor_handles) {
  (*descriptor_handles->handle_index)[resource_id].rtv = descriptor_handles->rtv_handles->size();
  const auto desc = GetRtvDesc2d(format);
  for (uint32_t i = 0; i < resource_num; i++) {
    const auto handle = GetDescriptorHandle(descriptor_heap_head_addr.rtv, descriptor_handle_increment_size.rtv, descriptor_handles->rtv_handles->size());
    if (resources && resources[i]) {
      device->CreateRenderTargetView(resources[i], &desc, handle);
    }
    descriptor_handles->rtv_handles->push_back(handle);
  }
}
void AddDescriptorHandlesDsv(const StrHash resource_id, DXGI_FORMAT format, ID3D12Resource** resources, const uint32_t resource_num, D3d12Device* device, const DescriptorHeapHeadAddr& descriptor_heap_head_addr, const DescriptorHandleIncrementSize& descriptor_handle_increment_size, DescriptorHandles* descriptor_handles) {
  (*descriptor_handles->handle_index)[resource_id].dsv = descriptor_handles->dsv_handles->size();
  const auto desc = GetDsvDesc2d(format);
  for (uint32_t i = 0; i < resource_num; i++) {
    const auto handle = GetDescriptorHandle(descriptor_heap_head_addr.dsv, descriptor_handle_increment_size.dsv, descriptor_handles->dsv_handles->size());
    if (resources && resources[i]) {
      device->CreateDepthStencilView(resources[i], &desc, handle);
    }
    descriptor_handles->dsv_handles->push_back(handle);
  }
}
void AddDescriptorHandlesSrv(const StrHash resource_id, DXGI_FORMAT format, ID3D12Resource** resources, const uint32_t resource_num, D3d12Device* device, const DescriptorHeapHeadAddr& descriptor_heap_head_addr, const DescriptorHandleIncrementSize& descriptor_handle_increment_size, DescriptorHandles* descriptor_handles) {
  (*descriptor_handles->handle_index)[resource_id].srv = descriptor_handles->cbv_srv_uav_handles->size();
  const auto desc = GetSrvDesc2d(format);
  for (uint32_t i = 0; i < resource_num; i++) {
    const auto handle = GetDescriptorHandle(descriptor_heap_head_addr.cbv_srv_uav, descriptor_handle_increment_size.cbv_srv_uav, descriptor_handles->cbv_srv_uav_handles->size());
    if (resources && resources[i]) {
      device->CreateShaderResourceView(resources[i], &desc, handle);
    }
    descriptor_handles->cbv_srv_uav_handles->push_back(handle);
  }
}
ID3D12DescriptorHeap* CreateDescriptorHeap(D3d12Device* device, const D3D12_DESCRIPTOR_HEAP_TYPE descriptor_heap_type, const uint32_t descriptor_handle_num, const D3D12_DESCRIPTOR_HEAP_FLAGS descriptor_heap_flag) {
  ID3D12DescriptorHeap* descriptor_heap{};
  const D3D12_DESCRIPTOR_HEAP_DESC desc = {
    .Type = descriptor_heap_type,
    .NumDescriptors = descriptor_handle_num,
    .Flags = descriptor_heap_flag,
  };
  auto hr = device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&descriptor_heap));
  DEBUG_ASSERT(SUCCEEDED(hr), DebugAssert{});
  return descriptor_heap;
}
D3D12_CPU_DESCRIPTOR_HANDLE GetDescriptorHandleRtv(const StrHash resource_id, const uint32_t index, const DescriptorHandles* descriptor_handles) {
  return (*descriptor_handles->rtv_handles)[(*descriptor_handles->handle_index)[resource_id].rtv + index];
}
D3D12_CPU_DESCRIPTOR_HANDLE GetDescriptorHandleDsv(const StrHash resource_id, const uint32_t index, const DescriptorHandles* descriptor_handles) {
  return (*descriptor_handles->dsv_handles)[(*descriptor_handles->handle_index)[resource_id].dsv + index];
}
D3D12_CPU_DESCRIPTOR_HANDLE GetDescriptorHandleSrv(const StrHash resource_id, const uint32_t index, const DescriptorHandles* descriptor_handles) {
  return (*descriptor_handles->cbv_srv_uav_handles)[(*descriptor_handles->handle_index)[resource_id].srv + index];
}
} // namespace boke
#include "doctest/doctest.h"
TEST_CASE("descriptors") {
  using namespace boke;
  // render pass & resource info
  const uint32_t swapchain_num = 3;
  // allocator
  const uint32_t main_buffer_size_in_bytes = 1024 * 1024;
  auto main_buffer = new std::byte[main_buffer_size_in_bytes];
  InitAllocator(main_buffer, main_buffer_size_in_bytes);
  // core units
  auto gfx_libraries = LoadGfxLibraries();
  auto dxgi = InitDxgi(gfx_libraries.dxgi_library, AdapterType::kHighPerformance);
  auto device = CreateDevice(gfx_libraries.d3d12_library, dxgi.adapter);
  // parse resource info
  auto resource_info = ParseResourceInfo(GetJson("tests/resources.json"));
  // resources
  auto gpu_memory_allocator = CreateGpuMemoryAllocator(dxgi.adapter, device);
  auto resource_set = CreateResources(resource_info, gpu_memory_allocator);
  // prepare descriptor handles
  auto descriptor_handle_increment_size = GetDescriptorHandleIncrementSize(device);
  auto descriptor_handle_num = CountDescriptorHandleNum(resource_info);
  CHECK_EQ(descriptor_handle_num.rtv, 6);
  CHECK_EQ(descriptor_handle_num.dsv, 1);
  CHECK_EQ(descriptor_handle_num.cbv_srv_uav, 6);
  descriptor_handle_num.rtv += swapchain_num; // for swapchain
  descriptor_handle_num.cbv_srv_uav += 1; // for imgui font
  auto descriptor_heaps = CreateDescriptorHeapsImpl(device, descriptor_handle_num);
  CHECK_NE(descriptor_heaps.rtv, nullptr);
  CHECK_NE(descriptor_heaps.dsv, nullptr);
  CHECK_NE(descriptor_heaps.cbv_srv_uav, nullptr);
  auto descriptor_heap_head_addr = GetDescriptorHeapHeadAddr(descriptor_heaps);
  CHECK_NE(descriptor_heap_head_addr.rtv.ptr, 0UL);
  CHECK_NE(descriptor_heap_head_addr.dsv.ptr, 0UL);
  CHECK_NE(descriptor_heap_head_addr.cbv_srv_uav.ptr, 0UL);
  auto descriptor_handles = PrepareDescriptorHandles(resource_info, resource_set, device, descriptor_heap_head_addr, descriptor_handle_increment_size);
  CHECK_EQ(descriptor_handles->handle_index->size(), 6);
  CHECK_UNARY(descriptor_handles->handle_index->contains("gbuffer0"_id));
  CHECK_UNARY(descriptor_handles->handle_index->contains("gbuffer1"_id));
  CHECK_UNARY(descriptor_handles->handle_index->contains("gbuffer2"_id));
  CHECK_UNARY(descriptor_handles->handle_index->contains("gbuffer3"_id));
  CHECK_UNARY(descriptor_handles->handle_index->contains("depth"_id));
  CHECK_UNARY(descriptor_handles->handle_index->contains("primary"_id));
  CHECK_LT((*descriptor_handles->handle_index)["gbuffer0"_id].rtv, 6);
  CHECK_LT((*descriptor_handles->handle_index)["gbuffer1"_id].rtv, 6);
  CHECK_LT((*descriptor_handles->handle_index)["gbuffer2"_id].rtv, 6);
  CHECK_LT((*descriptor_handles->handle_index)["gbuffer3"_id].rtv, 6);
  CHECK_LT((*descriptor_handles->handle_index)["primary"_id].rtv, 6);
  CHECK_LT((*descriptor_handles->handle_index)["gbuffer0"_id].srv, 6);
  CHECK_LT((*descriptor_handles->handle_index)["gbuffer1"_id].srv, 6);
  CHECK_LT((*descriptor_handles->handle_index)["gbuffer2"_id].srv, 6);
  CHECK_LT((*descriptor_handles->handle_index)["gbuffer3"_id].srv, 6);
  CHECK_LT((*descriptor_handles->handle_index)["primary"_id].srv, 6);
  CHECK_EQ((*descriptor_handles->handle_index)["depth"_id].dsv, 0);
  CHECK_EQ(descriptor_handles->rtv_handles->size(), 6);
  CHECK_EQ(descriptor_handles->dsv_handles->size(), 1);
  CHECK_EQ(descriptor_handles->cbv_srv_uav_handles->size(), 6);
  ID3D12Resource* swapchain_resources[swapchain_num]{};
  AddDescriptorHandlesRtv("swapchain"_id, resource_info["swapchain"_id].format, swapchain_resources, swapchain_num, device, descriptor_heap_head_addr, descriptor_handle_increment_size, descriptor_handles);
  CHECK_UNARY(descriptor_handles->handle_index->contains("gbuffer0"_id));
  CHECK_UNARY(descriptor_handles->handle_index->contains("gbuffer1"_id));
  CHECK_UNARY(descriptor_handles->handle_index->contains("gbuffer2"_id));
  CHECK_UNARY(descriptor_handles->handle_index->contains("gbuffer3"_id));
  CHECK_UNARY(descriptor_handles->handle_index->contains("depth"_id));
  CHECK_UNARY(descriptor_handles->handle_index->contains("primary"_id));
  CHECK_UNARY(descriptor_handles->handle_index->contains("swapchain"_id));
  CHECK_LT((*descriptor_handles->handle_index)["gbuffer0"_id].rtv, 6);
  CHECK_LT((*descriptor_handles->handle_index)["gbuffer1"_id].rtv, 6);
  CHECK_LT((*descriptor_handles->handle_index)["gbuffer2"_id].rtv, 6);
  CHECK_LT((*descriptor_handles->handle_index)["gbuffer3"_id].rtv, 6);
  CHECK_LT((*descriptor_handles->handle_index)["primary"_id].rtv, 6);
  CHECK_EQ((*descriptor_handles->handle_index)["swapchain"_id].rtv, 6);
  CHECK_LT((*descriptor_handles->handle_index)["gbuffer0"_id].srv, 6);
  CHECK_LT((*descriptor_handles->handle_index)["gbuffer1"_id].srv, 6);
  CHECK_LT((*descriptor_handles->handle_index)["gbuffer2"_id].srv, 6);
  CHECK_LT((*descriptor_handles->handle_index)["gbuffer3"_id].srv, 6);
  CHECK_LT((*descriptor_handles->handle_index)["primary"_id].srv, 6);
  CHECK_EQ((*descriptor_handles->handle_index)["depth"_id].dsv, 0);
  CHECK_EQ(descriptor_handles->rtv_handles->size(), 9);
  CHECK_EQ(descriptor_handles->dsv_handles->size(), 1);
  CHECK_EQ(descriptor_handles->cbv_srv_uav_handles->size(), 6);
  ReleaseDescriptorHandles(descriptor_handles);
  descriptor_heaps.rtv->Release();;
  descriptor_heaps.dsv->Release();;
  descriptor_heaps.cbv_srv_uav->Release();;
  ReleaseResources(resource_set);
  ReleaseGpuMemoryAllocator(gpu_memory_allocator);
  resource_info.~StrHashMap<ResourceInfo>();
  device->Release();
  TermDxgi(dxgi);
  ReleaseGfxLibraries(gfx_libraries);
  delete[] main_buffer;
}
