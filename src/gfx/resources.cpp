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
enum class ResourceCreationType : uint8_t {
  kNone,
  kRtv,
  kDsv,
};
struct ResourceInfo {
  ResourceCreationType creation_type{};
  D3D12_RESOURCE_FLAGS flags{};
  DXGI_FORMAT format{};
  Size2d size{};
  bool pingpong{};
};
struct DescriptorHandleIncrementSize {
  uint32_t rtv{};
  uint32_t dsv{};
  uint32_t cbv_srv_uav{};
};
struct DescriptorHandleNum {
  uint32_t rtv{};
  uint32_t dsv{};
  uint32_t cbv_srv_uav{};
};
struct DescriptorHeaps {
  ID3D12DescriptorHeap* rtv{};
  ID3D12DescriptorHeap* dsv{};
  ID3D12DescriptorHeap* cbv_srv_uav{};
};
struct DescriptorHeapHeadAddr {
  D3D12_CPU_DESCRIPTOR_HANDLE rtv{};
  D3D12_CPU_DESCRIPTOR_HANDLE dsv{};
  D3D12_CPU_DESCRIPTOR_HANDLE cbv_srv_uav{};
};
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
void* GpuMemoryAllocatorAllocate(size_t size, size_t alignment, void* private_data) {
  return boke::Allocate(boke::GetUint32(size), boke::GetUint32(alignment), static_cast<boke::AllocatorData*>(private_data));
}
void GpuMemoryAllocatorDeallocate(void* ptr, void* private_data) {
  boke::Deallocate(ptr, static_cast<boke::AllocatorData*>(private_data));
}
auto HashInteger(const uint64_t x) {
  // Jenkins hash
  const uint8_t* data = reinterpret_cast<const uint8_t*>(&x);
  const size_t length = sizeof(x);
  uint64_t hash = 0;
  for (size_t i = 0; i < length; ++i) {
    hash += data[i];
    hash += (hash << 10);
    hash ^= (hash >> 6);
  }
  hash += (hash << 3);
  hash ^= (hash >> 11);
  hash += (hash << 15);
  return hash;
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
auto CreateTexture2dRtv(D3D12MA::Allocator* allocator, const Size2d& size, const DXGI_FORMAT format, const D3D12_RESOURCE_FLAGS flags) {
  D3D12_CLEAR_VALUE clear_value{
    .Format = format,
    .Color = {},
  };
  return CreateTexture2d(allocator, size, format,
                         flags,
                         D3D12_BARRIER_LAYOUT_RENDER_TARGET,
                         &clear_value,
                         0, nullptr);
}
auto CreateTexture2dDsv(D3D12MA::Allocator* allocator, const Size2d& size, const DXGI_FORMAT format, const D3D12_RESOURCE_FLAGS flags) {
  D3D12_CLEAR_VALUE clear_value{
    .Format = format,
    .DepthStencil = {
      .Depth = 1.0f, // set 0.0f for inverse-z
      .Stencil = 0,
    },
  };
  return CreateTexture2d(allocator, size, format,
                         flags,
                         D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE,
                         &clear_value,
                         0, nullptr);
}
struct ResourceCreationImplAsset {
  D3D12MA::Allocator* allocator{};
  StrHashMap<D3D12MA::Allocation*>* allocations{};
  StrHashMap<ID3D12Resource*>* resources{};
};
void CreateResourceImpl(ResourceCreationImplAsset* asset, const StrHash resource_id, const ResourceInfo* resource_info) {
  switch (resource_info->creation_type) {
    case ResourceCreationType::kRtv: {
      if (resource_info->pingpong) {
        auto allocation0 = CreateTexture2dRtv(asset->allocator, resource_info->size, resource_info->format, resource_info->flags);
        auto allocation1 = CreateTexture2dRtv(asset->allocator, resource_info->size, resource_info->format, resource_info->flags);
        const auto id0 = GetPinpongResourceId(resource_id, 0);
        const auto id1 = GetPinpongResourceId(resource_id, 1);
        asset->allocations->insert(id0, allocation0);
        asset->allocations->insert(id1, allocation1);
        asset->resources->insert(id0, allocation0->GetResource());
        asset->resources->insert(id1, allocation1->GetResource());
        break;
      }
      auto allocation = CreateTexture2dRtv(asset->allocator, resource_info->size, resource_info->format, resource_info->flags);
      asset->allocations->insert(resource_id, allocation);
      asset->resources->insert(resource_id, allocation->GetResource());
      break;
    }
    case ResourceCreationType::kDsv: {
      auto allocation = CreateTexture2dDsv(asset->allocator, resource_info->size, resource_info->format, resource_info->flags);
      asset->allocations->insert(resource_id, allocation);
      asset->resources->insert(resource_id, allocation->GetResource());
      break;
    }
    case ResourceCreationType::kNone: {
      break;
    }
  }
}
// TODO move to descriptors.cpp
auto GetDescriptorHandleIncrementSize(D3d12Device* device) {
  return DescriptorHandleIncrementSize{
    .rtv = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV),
    .dsv = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV),
    .cbv_srv_uav = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV),
  };
}
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
auto CreateDescriptorHeaps(D3d12Device* device, const DescriptorHandleNum& descriptor_handle_num) {
  return DescriptorHeaps{
    .rtv = CreateDescriptorHeap(device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, descriptor_handle_num.rtv, D3D12_DESCRIPTOR_HEAP_FLAG_NONE),
    .dsv = CreateDescriptorHeap(device, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, descriptor_handle_num.dsv, D3D12_DESCRIPTOR_HEAP_FLAG_NONE),
    .cbv_srv_uav = CreateDescriptorHeap(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, descriptor_handle_num.cbv_srv_uav, D3D12_DESCRIPTOR_HEAP_FLAG_NONE),
  };
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
DXGI_FORMAT GetDxgiFormat(const char* format) {
  if (strcmp(format, "R8G8B8A8_UNORM") == 0) {
    return DXGI_FORMAT_R8G8B8A8_UNORM;
  }
  if (strcmp(format, "R8G8B8A8_UNORM_SRGB") == 0) {
    return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
  }
  if (strcmp(format, "B8G8R8A8_UNORM") == 0) {
    return DXGI_FORMAT_B8G8R8A8_UNORM;
  }
  if (strcmp(format, "B8G8R8A8_UNORM_SRGB") == 0) {
    return DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
  }
  if (strcmp(format, "R16G16B16A16_FLOAT") == 0) {
    return DXGI_FORMAT_R16G16B16A16_FLOAT;
  }
  if (strcmp(format, "R10G10B10A2_UNORM") == 0) {
    return DXGI_FORMAT_R10G10B10A2_UNORM;
  }
  if (strcmp(format, "R10G10B10_XR_BIAS_A2_UNORM") == 0) {
    return DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM;
  }
  if (strcmp(format, "D24_UNORM_S8_UINT") == 0) {
    return DXGI_FORMAT_D24_UNORM_S8_UINT;
  }
  DEBUG_ASSERT(false, DebugAssert{});
  return DXGI_FORMAT_R8G8B8A8_UNORM;
}
struct ResourceOptions {
  const rapidjson::Value* options{};
  Size2d size{};
  DXGI_FORMAT format{};
};
StrHash GetPinpongResourceId(const StrHash id, const uint32_t index) {
  return HashInteger(id + index);
}
void FillResourceOptions(const ResourceOptions* resource_options, const StrHash resource_id, ResourceInfo* info) {
  bool size_found = false;
  bool format_found = false;
  for (const auto& option : resource_options->options->GetArray()) {
    const auto hash = GetStrHash(option["name"].GetString());
    if (hash != resource_id) { continue; }
    if (option.HasMember("size")) {
      DEBUG_ASSERT(false, DebugAssert{}); // not checked yet.
      auto& size = option["size"];
      info->size.width  = size[0].GetUint();
      info->size.height = size[1].GetUint();
      size_found = true;
    }
    if (option.HasMember("format")) {
      info->format = GetDxgiFormat(option["format"].GetString());
      format_found = true;
    }
  }
  if (!size_found) {
    if (info->creation_type == ResourceCreationType::kNone && resource_id != "swapchain"_id) {
      info->size = {0, 0};
    } else {
      info->size = resource_options->size;
    }
  }
  if (!format_found) {
    if (info->creation_type == ResourceCreationType::kNone && resource_id != "swapchain"_id) {
      info->format = DXGI_FORMAT_UNKNOWN;
    } else {
      info->format = resource_options->format;
    }
  }
}
void ConfigureResourceInfo(const uint32_t render_pass_info_len, RenderPassInfo* render_pass_info, const rapidjson::Value& resource_options, StrHashMap<ResourceInfo>& resource_info) {
  for (uint32_t i = 0; i < render_pass_info_len; i++) {
    for (uint32_t j = 0; j < render_pass_info[i].srv_num; j++) {
      const auto& resource_id = render_pass_info[i].srv[j];
      if (!resource_info.contains(resource_id)) {
        resource_info[resource_id] = {
          .creation_type = ResourceCreationType::kNone,
        };
        continue;
      }
      resource_info[resource_id].flags &= ~D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;
      DEBUG_ASSERT((resource_info[resource_id].flags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE) == 0, DebugAssert{});
    }
    for (uint32_t j = 0; j < render_pass_info[i].rtv_num; j++) {
      const auto& resource_id = render_pass_info[i].rtv[j];
      if (!resource_info.contains(resource_id)) {
        resource_info[resource_id] = {
          .creation_type = ResourceCreationType::kRtv,
          .flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE,
        };
      } else {
        for (uint32_t k = 0; k < render_pass_info[i].srv_num; k++) {
          if (render_pass_info[i].srv[k] == resource_id) {
            resource_info[resource_id].pingpong = true;
            break;
          }
        }
      }
      DEBUG_ASSERT(resource_info[resource_id].creation_type == ResourceCreationType::kRtv, DebugAssert{});
      DEBUG_ASSERT(resource_info[resource_id].flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, DebugAssert{});
    }
    if (render_pass_info[i].dsv != kEmptyStr) {
      const auto& resource_id = render_pass_info[i].dsv;
      if (!resource_info.contains(resource_id)) {
        resource_info[resource_id] = {
          .creation_type = ResourceCreationType::kDsv,
          .flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL | D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE,
        };
      }
      DEBUG_ASSERT(resource_info[resource_id].creation_type == ResourceCreationType::kDsv, DebugAssert{});
      DEBUG_ASSERT(resource_info[resource_id].flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL, DebugAssert{});
    }
    if (render_pass_info[i].present != kEmptyStr) {
      const auto& resource_id = render_pass_info[i].present;
      if (!resource_info.contains(resource_id)) {
        resource_info[resource_id] = {
          .creation_type = ResourceCreationType::kNone,
        };
      }
      resource_info[resource_id].creation_type = ResourceCreationType::kNone;
      resource_info[resource_id].flags = D3D12_RESOURCE_FLAG_NONE;
    }
  }
  ResourceOptions resource_options_parsed{};
  {
    resource_options_parsed.options = &resource_options["options"];
    const auto& common_settings = resource_options["common_settings"];
    resource_options_parsed.size.width = common_settings["size"][0].GetUint();
    resource_options_parsed.size.height = common_settings["size"][1].GetUint();
    resource_options_parsed.format = GetDxgiFormat(common_settings["format"].GetString());
  }
  resource_info.iterate<const ResourceOptions>(FillResourceOptions, &resource_options_parsed);
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
DescriptorHandles::DescriptorHandles(tote::AllocatorCallbacks<AllocatorData> allocator_data)
    : rtv(allocator_data)
    , dsv(allocator_data)
    , srv(allocator_data) {}
DescriptorHandles::~DescriptorHandles() {}
auto CreateResources(const StrHashMap<ResourceInfo>& resource_info, D3D12MA::Allocator* allocator, StrHashMap<D3D12MA::Allocation*>& allocations, StrHashMap<ID3D12Resource*>& resources) {
  ResourceCreationImplAsset asset{
    .allocator = allocator,
    .allocations = &allocations,
    .resources = &resources,
  };
  resource_info.iterate<ResourceCreationImplAsset>(CreateResourceImpl, &asset);
}
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
} // namespace
#include "doctest/doctest.h"
TEST_CASE("resources") {
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
  auto json = GetJson("tests/resources.json", allocator_data);
  const uint32_t primary_width = json["resource_options"]["common_settings"]["size"][0].GetUint();
  const uint32_t primary_height = json["resource_options"]["common_settings"]["size"][1].GetUint();
  StrHashMap<ResourceInfo> resource_info(GetAllocatorCallbacks(allocator_data));
  ConfigureResourceInfo(render_pass_info_len, render_pass_info, json["resource_options"], resource_info);
  CHECK_EQ(resource_info.size(), 8);
  CHECK_EQ(resource_info["gbuffer0"_id].creation_type, ResourceCreationType::kRtv);
  CHECK_EQ(resource_info["gbuffer0"_id].flags, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
  CHECK_EQ(resource_info["gbuffer0"_id].format, DXGI_FORMAT_R8G8B8A8_UNORM);
  CHECK_EQ(resource_info["gbuffer0"_id].size.width, primary_width);
  CHECK_EQ(resource_info["gbuffer0"_id].size.height, primary_height);
  CHECK_EQ(resource_info["gbuffer0"_id].pingpong, false);
  CHECK_EQ(resource_info["gbuffer1"_id].creation_type, ResourceCreationType::kRtv);
  CHECK_EQ(resource_info["gbuffer1"_id].flags, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
  CHECK_EQ(resource_info["gbuffer1"_id].format, DXGI_FORMAT_R8G8B8A8_UNORM);
  CHECK_EQ(resource_info["gbuffer1"_id].size.width, primary_width);
  CHECK_EQ(resource_info["gbuffer1"_id].size.height, primary_height);
  CHECK_EQ(resource_info["gbuffer1"_id].pingpong, false);
  CHECK_EQ(resource_info["gbuffer2"_id].creation_type, ResourceCreationType::kRtv);
  CHECK_EQ(resource_info["gbuffer2"_id].flags, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
  CHECK_EQ(resource_info["gbuffer2"_id].format, DXGI_FORMAT_R10G10B10A2_UNORM);
  CHECK_EQ(resource_info["gbuffer2"_id].size.width, primary_width);
  CHECK_EQ(resource_info["gbuffer2"_id].size.height, primary_height);
  CHECK_EQ(resource_info["gbuffer2"_id].pingpong, false);
  CHECK_EQ(resource_info["gbuffer3"_id].creation_type, ResourceCreationType::kRtv);
  CHECK_EQ(resource_info["gbuffer3"_id].flags, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
  CHECK_EQ(resource_info["gbuffer3"_id].format, DXGI_FORMAT_R8G8B8A8_UNORM);
  CHECK_EQ(resource_info["gbuffer3"_id].size.width, primary_width);
  CHECK_EQ(resource_info["gbuffer3"_id].size.height, primary_height);
  CHECK_EQ(resource_info["gbuffer3"_id].pingpong, false);
  CHECK_EQ(resource_info["depth"_id].creation_type, ResourceCreationType::kDsv);
  CHECK_EQ(resource_info["depth"_id].flags, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL | D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE);
  CHECK_EQ(resource_info["depth"_id].format, DXGI_FORMAT_D24_UNORM_S8_UINT);
  CHECK_EQ(resource_info["depth"_id].size.width, primary_width);
  CHECK_EQ(resource_info["depth"_id].size.height, primary_height);
  CHECK_EQ(resource_info["depth"_id].pingpong, false);
  CHECK_EQ(resource_info["primary"_id].creation_type, ResourceCreationType::kRtv);
  CHECK_EQ(resource_info["primary"_id].format, DXGI_FORMAT_R16G16B16A16_FLOAT);
  CHECK_EQ(resource_info["primary"_id].flags, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
  CHECK_EQ(resource_info["primary"_id].size.width, primary_width);
  CHECK_EQ(resource_info["primary"_id].size.height, primary_height);
  CHECK_EQ(resource_info["primary"_id].pingpong, true);
  CHECK_EQ(resource_info["imgui_font"_id].creation_type, ResourceCreationType::kNone);
  CHECK_EQ(resource_info["imgui_font"_id].flags, D3D12_RESOURCE_FLAG_NONE);
  CHECK_EQ(resource_info["imgui_font"_id].format, DXGI_FORMAT_UNKNOWN);
  CHECK_EQ(resource_info["imgui_font"_id].size.width, 0);
  CHECK_EQ(resource_info["imgui_font"_id].size.height, 0);
  CHECK_EQ(resource_info["imgui_font"_id].pingpong, false);
  CHECK_EQ(resource_info["swapchain"_id].creation_type, ResourceCreationType::kNone);
  CHECK_EQ(resource_info["swapchain"_id].flags, D3D12_RESOURCE_FLAG_NONE);
  CHECK_EQ(resource_info["swapchain"_id].format, DXGI_FORMAT_R8G8B8A8_UNORM);
  CHECK_EQ(resource_info["swapchain"_id].size.width, primary_width);
  CHECK_EQ(resource_info["swapchain"_id].size.height, primary_height);
  CHECK_EQ(resource_info["swapchain"_id].pingpong, false);
  // gpu resource + descriptor handles
  auto gpu_memory_allocator = CreateGpuMemoryAllocator(dxgi.adapter, device, allocator_data);
  StrHashMap<D3D12MA::Allocation*> allocations(GetAllocatorCallbacks(allocator_data));
  StrHashMap<ID3D12Resource*> resources(GetAllocatorCallbacks(allocator_data));
  CreateResources(resource_info, gpu_memory_allocator, allocations, resources);
  CHECK_EQ(allocations.size(), 7);
  CHECK_NE(allocations["gbuffer0"_id], nullptr);
  CHECK_NE(allocations["gbuffer1"_id], nullptr);
  CHECK_NE(allocations["gbuffer2"_id], nullptr);
  CHECK_NE(allocations["gbuffer3"_id], nullptr);
  CHECK_NE(allocations["depth"_id], nullptr);
  CHECK_NE(allocations[GetPinpongResourceId("primary"_id, 0)], nullptr);
  CHECK_NE(allocations[GetPinpongResourceId("primary"_id, 1)], nullptr);
  CHECK_EQ(resources.size(), 7);
  CHECK_NE(resources["gbuffer0"_id], nullptr);
  CHECK_NE(resources["gbuffer1"_id], nullptr);
  CHECK_NE(resources["gbuffer2"_id], nullptr);
  CHECK_NE(resources["gbuffer3"_id], nullptr);
  CHECK_NE(resources["depth"_id], nullptr);
  CHECK_NE(resources[GetPinpongResourceId("primary"_id, 0)], nullptr);
  CHECK_NE(resources[GetPinpongResourceId("primary"_id, 1)], nullptr);
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
  AddDescriptorHandlesRtv("swapchain"_id, DXGI_FORMAT_R8G8B8A8_UNORM, swapchain_resources, swapchain_num, device, descriptor_heap_head_addr, descriptor_handle_increment_size, descriptor_handles);
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
  // terminate
  allocations.iterate([](const StrHash, D3D12MA::Allocation** allocation) {
    (*allocation)->Release();
  });
  // resources acquired from allocation->GetResource() does not need Release
  descriptor_heaps.rtv->Release();
  descriptor_heaps.dsv->Release();
  descriptor_heaps.cbv_srv_uav->Release();
  gpu_memory_allocator->Release();
  descriptor_handles.~DescriptorHandles();
  resources.~StrHashMap<ID3D12Resource*>();
  allocations.~StrHashMap<D3D12MA::Allocation*>();
  resource_info.~StrHashMap<ResourceInfo>();
  device->Release();
  TermDxgi(dxgi);
  ReleaseGfxLibraries(gfx_libraries);
  delete[] main_buffer;
  // TODO set name to resources
}
