#include "dxgi1_6.h"
#include <D3D12MemAlloc.h>
#include "boke/allocator.h"
#include "boke/container.h"
#include "boke/debug_assert.h"
#include "boke/str_hash.h"
#include "boke/util.h"
#include "core.h"
#include "json.h"
#include "render_pass_info.h"
#include "resources.h"
namespace boke {
class DescriptorHandles final {
 public:
  DescriptorHandles(tote::AllocatorCallbacks<AllocatorData>);
  ~DescriptorHandles();
  StrHashMap<D3D12_CPU_DESCRIPTOR_HANDLE> rtv;
  StrHashMap<D3D12_CPU_DESCRIPTOR_HANDLE> dsv;
  StrHashMap<D3D12_CPU_DESCRIPTOR_HANDLE> srv;
 private:
  DescriptorHandles() = delete;
  DescriptorHandles(const DescriptorHandles&) = delete;
  DescriptorHandles(DescriptorHandles&&) = delete;
  void operator=(const DescriptorHandles&) = delete;
  void operator=(DescriptorHandles&&) = delete;
};
} // namespace boke
namespace {
using namespace boke;
struct DescriptorHandleIncrementSize {
  uint32_t rtv{};
  uint32_t dsv{};
  uint32_t cbv_srv_uav{};
};
auto GetDescriptorHandleIncrementSize(D3d12Device* device) {
  return DescriptorHandleIncrementSize{
    .rtv = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV),
    .dsv = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV),
    .cbv_srv_uav = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV),
  };
}
struct DescriptorHandleNum {
  uint32_t rtv{};
  uint32_t dsv{};
  uint32_t cbv_srv_uav{};
};
void CountDescriptorHandleNumImpl(DescriptorHandleNum* descriptor_handle_num, const StrHash, const ResourceInfo* resource_info) {
  if (resource_info->flags == D3D12_RESOURCE_FLAG_NONE) { return; }
  const uint32_t add_val = resource_info->pingpong ? 2 : 1;
  if (resource_info->flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET) {
    descriptor_handle_num->rtv += add_val;
  }
  if (resource_info->flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL) {
    descriptor_handle_num->dsv += add_val;
  }
  if (!(resource_info->flags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE)) {
    descriptor_handle_num->cbv_srv_uav += add_val;
  }
}
auto CountDescriptorHandleNum(const StrHashMap<ResourceInfo>& resouce_info) {
  DescriptorHandleNum descriptor_handle_num{};
  resouce_info.iterate<DescriptorHandleNum>(CountDescriptorHandleNumImpl, &descriptor_handle_num);
  return descriptor_handle_num;
}
auto CreateDescriptorHeap(D3d12Device* device, const D3D12_DESCRIPTOR_HEAP_TYPE descriptor_heap_type, const uint32_t descriptor_heap_num, const D3D12_DESCRIPTOR_HEAP_FLAGS descriptor_heap_flag) {
  ID3D12DescriptorHeap* descriptor_heap{};
  const D3D12_DESCRIPTOR_HEAP_DESC desc = {
    .Type = descriptor_heap_type,
    .NumDescriptors = descriptor_heap_num,
    .Flags = descriptor_heap_flag,
  };
  auto hr = device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&descriptor_heap));
  DEBUG_ASSERT(SUCCEEDED(hr), DebugAssert{});
  return descriptor_heap;
}
struct DescriptorHeaps {
  ID3D12DescriptorHeap* rtv{};
  ID3D12DescriptorHeap* dsv{};
  ID3D12DescriptorHeap* cbv_srv_uav{};
};
auto CreateDescriptorHeaps(D3d12Device* device, const DescriptorHandleNum& descriptor_handle_num) {
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
struct DescriptorHeapHeadAddr {
  D3D12_CPU_DESCRIPTOR_HANDLE rtv{};
  D3D12_CPU_DESCRIPTOR_HANDLE dsv{};
  D3D12_CPU_DESCRIPTOR_HANDLE cbv_srv_uav{};
};
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
auto GetSrvDesc2d(const DXGI_FORMAT format) {
  return D3D12_SHADER_RESOURCE_VIEW_DESC{
    .Format = format,
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
  const StrHashMap<ID3D12Resource*>* resources{};
  D3d12Device* device{};
  const DescriptorHeapHeadAddr* descriptor_heap_head_addr{};
  const DescriptorHandleIncrementSize* descriptor_handle_increment_size{};
  DescriptorHandles* descriptor_handles{};
};
void PrepareDescriptorHandlesImpl(DescriptorHandleImplAsset* asset, const StrHash resource_id, const ResourceInfo* resource_info) {
  if (resource_info->flags == D3D12_RESOURCE_FLAG_NONE) { return; }
  if (resource_info->pingpong) {
    const auto resource_id_pingpong_resolved = GetPinpongResourceId(resource_id, 0);
    const auto resource_id_pingpong_resolved_pingpong = GetPinpongResourceId(resource_id, 1);
    auto resource = (*asset->resources)[resource_id_pingpong_resolved];
    auto resource_pingpong = (*asset->resources)[resource_id_pingpong_resolved_pingpong];
    if (resource_info->flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET) {
      const auto desc = GetRtvDesc2d(resource_info->format);
      const auto handle = GetDescriptorHandle(asset->descriptor_heap_head_addr->rtv, asset->descriptor_handle_increment_size->rtv, asset->descriptor_handles->rtv.size());
      const auto handle_pingpong = GetDescriptorHandle(asset->descriptor_heap_head_addr->rtv, asset->descriptor_handle_increment_size->rtv, asset->descriptor_handles->rtv.size() + 1);
      asset->device->CreateRenderTargetView(resource, &desc, handle);
      asset->device->CreateRenderTargetView(resource_pingpong, &desc, handle_pingpong);
      asset->descriptor_handles->rtv[resource_id_pingpong_resolved] = handle;
      asset->descriptor_handles->rtv[resource_id_pingpong_resolved_pingpong] = handle_pingpong;
    }
    DEBUG_ASSERT(!(resource_info->flags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE), DebugAssert{});
    {
      const auto desc = GetSrvDesc2d(resource_info->format);
      const auto handle = GetDescriptorHandle(asset->descriptor_heap_head_addr->cbv_srv_uav, asset->descriptor_handle_increment_size->cbv_srv_uav, asset->descriptor_handles->srv.size());
      const auto handle_pingpong = GetDescriptorHandle(asset->descriptor_heap_head_addr->cbv_srv_uav, asset->descriptor_handle_increment_size->cbv_srv_uav, asset->descriptor_handles->srv.size() + 1);
      asset->device->CreateShaderResourceView(resource, &desc, handle);
      asset->device->CreateShaderResourceView(resource_pingpong, &desc, handle_pingpong);
      asset->descriptor_handles->srv[resource_id_pingpong_resolved] = handle;
      asset->descriptor_handles->srv[resource_id_pingpong_resolved_pingpong] = handle_pingpong;
    }
    return;
  }
  auto resource = (*asset->resources)[resource_id];
  if (resource_info->flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET) {
    const auto desc = GetRtvDesc2d(resource_info->format);
    const auto handle = GetDescriptorHandle(asset->descriptor_heap_head_addr->rtv, asset->descriptor_handle_increment_size->rtv, asset->descriptor_handles->rtv.size());
    asset->device->CreateRenderTargetView(resource, &desc, handle);
    asset->descriptor_handles->rtv[resource_id] = handle;
  }
  if (resource_info->flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL) {
    const auto desc = GetDsvDesc2d(resource_info->format);
    const auto handle = GetDescriptorHandle(asset->descriptor_heap_head_addr->dsv, asset->descriptor_handle_increment_size->dsv, asset->descriptor_handles->dsv.size());
    asset->device->CreateDepthStencilView(resource, &desc, handle);
    asset->descriptor_handles->dsv[resource_id] = handle;
  }
  if (!(resource_info->flags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE)) {
    const auto desc = GetSrvDesc2d(resource_info->format);
    const auto handle = GetDescriptorHandle(asset->descriptor_heap_head_addr->cbv_srv_uav, asset->descriptor_handle_increment_size->cbv_srv_uav, asset->descriptor_handles->srv.size());
    asset->device->CreateShaderResourceView(resource, &desc, handle);
    asset->descriptor_handles->srv[resource_id] = handle;
  }
}
} // namespace
namespace boke {
DescriptorHandles::DescriptorHandles(tote::AllocatorCallbacks<AllocatorData> allocator_data)
    : rtv(allocator_data)
    , dsv(allocator_data)
    , srv(allocator_data) {}
DescriptorHandles::~DescriptorHandles() {}
auto PrepareDescriptorHandles(const StrHashMap<ResourceInfo>& resource_info, const StrHashMap<ID3D12Resource*>& resources, D3d12Device* device, const DescriptorHeapHeadAddr& descriptor_heap_head_addr, const DescriptorHandleIncrementSize& descriptor_handle_increment_size, DescriptorHandles& descriptor_handles) {
  DescriptorHandleImplAsset asset {
    .resources = &resources,
    .device = device,
    .descriptor_heap_head_addr = &descriptor_heap_head_addr,
    .descriptor_handle_increment_size = &descriptor_handle_increment_size,
    .descriptor_handles = &descriptor_handles,
  };
  resource_info.iterate<DescriptorHandleImplAsset>(PrepareDescriptorHandlesImpl, &asset);
}
auto AddDescriptorHandlesRtv(const StrHash resource_id, DXGI_FORMAT format, ID3D12Resource** resources, const uint32_t resource_num, D3d12Device* device, const DescriptorHeapHeadAddr& descriptor_heap_head_addr, const DescriptorHandleIncrementSize& descriptor_handle_increment_size, DescriptorHandles& descriptor_handles) {
  const auto desc = GetRtvDesc2d(format);
  for (uint32_t i = 0; i < resource_num; i++) {
    const auto handle = GetDescriptorHandle(descriptor_heap_head_addr.rtv, descriptor_handle_increment_size.rtv, descriptor_handles.rtv.size());
    device->CreateRenderTargetView(resources[i], &desc, handle);
    descriptor_handles.rtv[resource_num == 1 ? resource_id : GetPinpongResourceId(resource_id, i)] = handle;
  }
}
auto AddDescriptorHandlesDsv(const StrHash resource_id, DXGI_FORMAT format, ID3D12Resource** resources, const uint32_t resource_num, D3d12Device* device, const DescriptorHeapHeadAddr& descriptor_heap_head_addr, const DescriptorHandleIncrementSize& descriptor_handle_increment_size, DescriptorHandles& descriptor_handles) {
  const auto desc = GetDsvDesc2d(format);
  for (uint32_t i = 0; i < resource_num; i++) {
    const auto handle = GetDescriptorHandle(descriptor_heap_head_addr.dsv, descriptor_handle_increment_size.dsv, descriptor_handles.dsv.size());
    device->CreateDepthStencilView(resources[0], &desc, handle);
    descriptor_handles.dsv[resource_num == 1 ? resource_id : GetPinpongResourceId(resource_id, i)] = handle;
  }
}
auto AddDescriptorHandlesSrv(const StrHash resource_id, DXGI_FORMAT format, ID3D12Resource** resources, const uint32_t resource_num, D3d12Device* device, const DescriptorHeapHeadAddr& descriptor_heap_head_addr, const DescriptorHandleIncrementSize& descriptor_handle_increment_size, DescriptorHandles& descriptor_handles) {
  const auto desc = GetSrvDesc2d(format);
  for (uint32_t i = 0; i < resource_num; i++) {
    const auto handle = GetDescriptorHandle(descriptor_heap_head_addr.cbv_srv_uav, descriptor_handle_increment_size.cbv_srv_uav, descriptor_handles.srv.size());
    if (resources != nullptr) {
      device->CreateShaderResourceView(resources[i], &desc, handle);
    }
    descriptor_handles.srv[resource_num == 1 ? resource_id : GetPinpongResourceId(resource_id, i)] = handle;
  }
}
} // namespace boke
#include "doctest/doctest.h"
TEST_CASE("descriptors") {
  using namespace boke;
  // render pass & resource info
  const uint32_t swapchain_num = 3;
  StrHash gbuffers[] = {"gbuffer0"_id, "gbuffer1"_id, "gbuffer2"_id, "gbuffer3"_id,};
  StrHash primary[] = {"primary"_id,};
  StrHash swapchain[] = {"swapchain"_id,};
  StrHash imgui_font[] = {"imgui_font"_id,};
  const uint32_t render_pass_info_len = 6;
  RenderPassInfo render_pass_info[render_pass_info_len] = {
    {
      // gbuffer
      .queue = "direct"_id,
      .rtv = gbuffers,
      .rtv_num = 4,
      .dsv = "depth"_id,
    },
    {
      // lighting
      .queue = "direct"_id,
      .srv = gbuffers,
      .srv_num = 4,
      .rtv = primary,
      .rtv_num = 1,
    },
    {
      // tonemap
      .queue = "direct"_id,
      .srv = primary,
      .srv_num = 1,
      .rtv = primary,
      .rtv_num = 1,
    },
    {
      // oetf
      .queue = "direct"_id,
      .srv = primary,
      .srv_num = 1,
      .rtv = swapchain,
      .rtv_num = 1,
    },
    {
      // imgui
      .queue = "direct"_id,
      .srv = imgui_font,
      .srv_num = 1,
      .rtv = swapchain,
      .rtv_num = 1,
    },
    {
      // present
      .queue = "direct"_id,
      .present = "swapchain"_id,
    },
  };
  // allocator
  const uint32_t main_buffer_size_in_bytes = 1024 * 1024;
  auto main_buffer = new std::byte[main_buffer_size_in_bytes];
  auto allocator_data = GetAllocatorData(main_buffer, main_buffer_size_in_bytes);
  // core units
  auto gfx_libraries = LoadGfxLibraries();
  auto dxgi = InitDxgi(gfx_libraries.dxgi_library, AdapterType::kHighPerformance);
  auto device = CreateDevice(gfx_libraries.d3d12_library, dxgi.adapter);
  // parse resource info
  const auto json = GetJson("tests/resources.json", allocator_data);
  StrHashMap<ResourceInfo> resource_info(GetAllocatorCallbacks(allocator_data));
  ConfigureResourceInfo(render_pass_info_len, render_pass_info, json["resource_options"], resource_info);
  // dummy resources
  StrHashMap<ID3D12Resource*> resources(GetAllocatorCallbacks(allocator_data));
  resources["gbuffer0"_id] = nullptr;
  resources["gbuffer1"_id] = nullptr;
  resources["gbuffer2"_id] = nullptr;
  resources["gbuffer3"_id] = nullptr;
  resources["depth"_id] = nullptr;
  resources[GetPinpongResourceId("primary"_id, 0)] = nullptr;
  resources[GetPinpongResourceId("primary"_id, 1)] = nullptr;
  // prepare descriptor handles
  auto descriptor_handle_increment_size = GetDescriptorHandleIncrementSize(device);
  auto descriptor_handle_num = CountDescriptorHandleNum(resource_info);
  CHECK_EQ(descriptor_handle_num.rtv, 6);
  CHECK_EQ(descriptor_handle_num.dsv, 1);
  CHECK_EQ(descriptor_handle_num.cbv_srv_uav, 6);
  descriptor_handle_num.rtv += swapchain_num; // for swapchain
  descriptor_handle_num.cbv_srv_uav += 1; // for imgui font
  auto descriptor_heaps = CreateDescriptorHeaps(device, descriptor_handle_num);
  CHECK_NE(descriptor_heaps.rtv, nullptr);
  CHECK_NE(descriptor_heaps.dsv, nullptr);
  CHECK_NE(descriptor_heaps.cbv_srv_uav, nullptr);
  auto descriptor_heap_head_addr = GetDescriptorHeapHeadAddr(descriptor_heaps);
  CHECK_NE(descriptor_heap_head_addr.rtv.ptr, 0UL);
  CHECK_NE(descriptor_heap_head_addr.dsv.ptr, 0UL);
  CHECK_NE(descriptor_heap_head_addr.cbv_srv_uav.ptr, 0UL);
  DescriptorHandles descriptor_handles(GetAllocatorCallbacks(allocator_data));
  PrepareDescriptorHandles(resource_info, resources, device, descriptor_heap_head_addr, descriptor_handle_increment_size, descriptor_handles);
  CHECK_EQ(descriptor_handles.rtv.size(), 6);
  CHECK_NE(descriptor_handles.rtv["gbuffer0"_id].ptr, 0UL);
  CHECK_NE(descriptor_handles.rtv["gbuffer1"_id].ptr, 0UL);
  CHECK_NE(descriptor_handles.rtv["gbuffer2"_id].ptr, 0UL);
  CHECK_NE(descriptor_handles.rtv["gbuffer3"_id].ptr, 0UL);
  CHECK_NE(descriptor_handles.rtv[GetPinpongResourceId("primary"_id, 0)].ptr, 0UL);
  CHECK_NE(descriptor_handles.rtv[GetPinpongResourceId("primary"_id, 1)].ptr, 0UL);
  CHECK_NE(descriptor_handles.rtv[GetPinpongResourceId("primary"_id, 0)].ptr, descriptor_handles.rtv[GetPinpongResourceId("primary"_id, 1)].ptr);
  CHECK_EQ(descriptor_handles.dsv.size(), 1);
  CHECK_NE(descriptor_handles.dsv["depth"_id].ptr, 0UL);
  CHECK_EQ(descriptor_handles.srv.size(), 6);
  CHECK_NE(descriptor_handles.srv["gbuffer0"_id].ptr, 0UL);
  CHECK_NE(descriptor_handles.srv["gbuffer1"_id].ptr, 0UL);
  CHECK_NE(descriptor_handles.srv["gbuffer2"_id].ptr, 0UL);
  CHECK_NE(descriptor_handles.srv["gbuffer3"_id].ptr, 0UL);
  CHECK_NE(descriptor_handles.srv[GetPinpongResourceId("primary"_id, 0)].ptr, 0UL);
  CHECK_NE(descriptor_handles.srv[GetPinpongResourceId("primary"_id, 1)].ptr, 0UL);
  CHECK_NE(descriptor_handles.srv[GetPinpongResourceId("primary"_id, 0)].ptr, descriptor_handles.srv[GetPinpongResourceId("primary"_id, 1)].ptr);
  ID3D12Resource* swapchain_resources[swapchain_num]{};
  AddDescriptorHandlesRtv("swapchain"_id, resource_info["swapchain"_id].format, swapchain_resources, swapchain_num, device, descriptor_heap_head_addr, descriptor_handle_increment_size, descriptor_handles);
  CHECK_EQ(descriptor_handles.rtv.size(), 9);
  CHECK_NE(descriptor_handles.rtv["gbuffer0"_id].ptr, 0UL);
  CHECK_NE(descriptor_handles.rtv["gbuffer1"_id].ptr, 0UL);
  CHECK_NE(descriptor_handles.rtv["gbuffer2"_id].ptr, 0UL);
  CHECK_NE(descriptor_handles.rtv["gbuffer3"_id].ptr, 0UL);
  CHECK_NE(descriptor_handles.rtv[GetPinpongResourceId("primary"_id, 0)].ptr, 0UL);
  CHECK_NE(descriptor_handles.rtv[GetPinpongResourceId("primary"_id, 1)].ptr, 0UL);
  CHECK_NE(descriptor_handles.rtv[GetPinpongResourceId("primary"_id, 0)].ptr, descriptor_handles.rtv[GetPinpongResourceId("primary"_id, 1)].ptr);
  CHECK_NE(descriptor_handles.rtv[GetPinpongResourceId("swapchain"_id, 0)].ptr, 0UL);
  CHECK_NE(descriptor_handles.rtv[GetPinpongResourceId("swapchain"_id, 1)].ptr, 0UL);
  CHECK_NE(descriptor_handles.rtv[GetPinpongResourceId("swapchain"_id, 2)].ptr, 0UL);
  CHECK_NE(descriptor_handles.rtv[GetPinpongResourceId("swapchain"_id, 0)].ptr, descriptor_handles.rtv[GetPinpongResourceId("swapchain"_id, 1)].ptr);
  CHECK_NE(descriptor_handles.rtv[GetPinpongResourceId("swapchain"_id, 0)].ptr, descriptor_handles.rtv[GetPinpongResourceId("swapchain"_id, 2)].ptr);
  CHECK_NE(descriptor_handles.rtv[GetPinpongResourceId("swapchain"_id, 1)].ptr, descriptor_handles.rtv[GetPinpongResourceId("swapchain"_id, 2)].ptr);
  CHECK_EQ(descriptor_handles.dsv.size(), 1);
  CHECK_NE(descriptor_handles.dsv["depth"_id].ptr, 0UL);
  CHECK_EQ(descriptor_handles.srv.size(), 6);
  CHECK_NE(descriptor_handles.srv["gbuffer0"_id].ptr, 0UL);
  CHECK_NE(descriptor_handles.srv["gbuffer1"_id].ptr, 0UL);
  CHECK_NE(descriptor_handles.srv["gbuffer2"_id].ptr, 0UL);
  CHECK_NE(descriptor_handles.srv["gbuffer3"_id].ptr, 0UL);
  CHECK_NE(descriptor_handles.srv[GetPinpongResourceId("primary"_id, 0)].ptr, 0UL);
  CHECK_NE(descriptor_handles.srv[GetPinpongResourceId("primary"_id, 1)].ptr, 0UL);
  CHECK_NE(descriptor_handles.srv[GetPinpongResourceId("primary"_id, 0)].ptr, descriptor_handles.srv[GetPinpongResourceId("primary"_id, 1)].ptr);
  AddDescriptorHandlesSrv("imgui_font"_id, DXGI_FORMAT_UNKNOWN, nullptr, 1, device, descriptor_heap_head_addr, descriptor_handle_increment_size, descriptor_handles);
  CHECK_EQ(descriptor_handles.rtv.size(), 9);
  CHECK_NE(descriptor_handles.rtv["gbuffer0"_id].ptr, 0UL);
  CHECK_NE(descriptor_handles.rtv["gbuffer1"_id].ptr, 0UL);
  CHECK_NE(descriptor_handles.rtv["gbuffer2"_id].ptr, 0UL);
  CHECK_NE(descriptor_handles.rtv["gbuffer3"_id].ptr, 0UL);
  CHECK_NE(descriptor_handles.rtv[GetPinpongResourceId("primary"_id, 0)].ptr, 0UL);
  CHECK_NE(descriptor_handles.rtv[GetPinpongResourceId("primary"_id, 1)].ptr, 0UL);
  CHECK_NE(descriptor_handles.rtv[GetPinpongResourceId("primary"_id, 0)].ptr, descriptor_handles.rtv[GetPinpongResourceId("primary"_id, 1)].ptr);
  CHECK_NE(descriptor_handles.rtv[GetPinpongResourceId("swapchain"_id, 0)].ptr, 0UL);
  CHECK_NE(descriptor_handles.rtv[GetPinpongResourceId("swapchain"_id, 1)].ptr, 0UL);
  CHECK_NE(descriptor_handles.rtv[GetPinpongResourceId("swapchain"_id, 2)].ptr, 0UL);
  CHECK_NE(descriptor_handles.rtv[GetPinpongResourceId("swapchain"_id, 0)].ptr, descriptor_handles.rtv[GetPinpongResourceId("swapchain"_id, 1)].ptr);
  CHECK_NE(descriptor_handles.rtv[GetPinpongResourceId("swapchain"_id, 0)].ptr, descriptor_handles.rtv[GetPinpongResourceId("swapchain"_id, 2)].ptr);
  CHECK_NE(descriptor_handles.rtv[GetPinpongResourceId("swapchain"_id, 1)].ptr, descriptor_handles.rtv[GetPinpongResourceId("swapchain"_id, 2)].ptr);
  CHECK_EQ(descriptor_handles.dsv.size(), 1);
  CHECK_NE(descriptor_handles.dsv["depth"_id].ptr, 0UL);
  CHECK_EQ(descriptor_handles.srv.size(), 7);
  CHECK_NE(descriptor_handles.srv["gbuffer0"_id].ptr, 0UL);
  CHECK_NE(descriptor_handles.srv["gbuffer1"_id].ptr, 0UL);
  CHECK_NE(descriptor_handles.srv["gbuffer2"_id].ptr, 0UL);
  CHECK_NE(descriptor_handles.srv["gbuffer3"_id].ptr, 0UL);
  CHECK_NE(descriptor_handles.srv[GetPinpongResourceId("primary"_id, 0)].ptr, 0UL);
  CHECK_NE(descriptor_handles.srv[GetPinpongResourceId("primary"_id, 1)].ptr, 0UL);
  CHECK_NE(descriptor_handles.srv[GetPinpongResourceId("primary"_id, 0)].ptr, descriptor_handles.srv[GetPinpongResourceId("primary"_id, 1)].ptr);
  CHECK_NE(descriptor_handles.srv["imgui_font"_id].ptr, 0UL);
  descriptor_heaps.rtv->Release();
  descriptor_heaps.dsv->Release();
  descriptor_heaps.cbv_srv_uav->Release();
  descriptor_handles.~DescriptorHandles();
  resources.~StrHashMap<ID3D12Resource*>();
  resource_info.~StrHashMap<ResourceInfo>();
  device->Release();
  TermDxgi(dxgi);
  ReleaseGfxLibraries(gfx_libraries);
  delete[] main_buffer;
  // TODO set name to resources
}
