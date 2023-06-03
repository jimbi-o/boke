#include "dxgi1_6.h"
#include <D3D12MemAlloc.h>
#include "boke/allocator.h"
#include "boke/container.h"
#include "boke/debug_assert.h"
#include "boke/str_hash.h"
#include "boke/util.h"
#include "core.h"
#include "d3d12_util.h"
#include "json.h"
#include "render_pass_info.h"
#include "resources.h"
namespace boke {
struct ResourceSet {
  StrHashMap<uint32_t>* resource_index;
  ResizableArray<D3D12MA::Allocation*>* allocations;
  ResizableArray<ID3D12Resource*>* resources;
};
} // namespace boke
namespace {
using namespace boke;
void* GpuMemoryAllocatorAllocate(size_t size, size_t alignment, void*) {
  return boke::Allocate(boke::GetUint32(size), boke::GetUint32(alignment));
}
void GpuMemoryAllocatorDeallocate(void* ptr, void*) {
  boke::Deallocate(ptr);
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
auto GetTypelessFormat(const DXGI_FORMAT format) {
  switch (format) {
    case DXGI_FORMAT_D24_UNORM_S8_UINT:
      return DXGI_FORMAT_R24G8_TYPELESS;
    default:
      return format;
  }
}
auto CreateTexture2dDsv(D3D12MA::Allocator* allocator, const Size2d& size, const DXGI_FORMAT format, const D3D12_RESOURCE_FLAGS flags) {
  D3D12_CLEAR_VALUE clear_value{
    .Format = format,
    .DepthStencil = {
      .Depth = 1.0f, // set 0.0f for inverse-z
      .Stencil = 0,
    },
  };
  return CreateTexture2d(allocator, size, GetTypelessFormat(format),
                         flags,
                         D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE,
                         &clear_value,
                         0, nullptr);
}
struct ResourceSetCreationAsseet {
  D3D12MA::Allocator* allocator{};
  StrHashMap<uint32_t>* resource_index;
  ResizableArray<D3D12MA::Allocation*>* allocations;
  ResizableArray<ID3D12Resource*>* resources;
};
void CreateResourceImpl(ResourceSetCreationAsseet* asset, const StrHash resource_id, const ResourceInfo* resource_info) {
  D3D12MA::Allocation* allocation[2];
  switch (resource_info->creation_type) {
    case ResourceCreationType::kRtv: {
      for (uint32_t i = 0; i < resource_info->physical_resource_num; i++) {
        allocation[i] = CreateTexture2dRtv(asset->allocator, resource_info->size, resource_info->format, resource_info->flags);
      }
      break;
    }
    case ResourceCreationType::kDsv: {
      DEBUG_ASSERT(resource_info->physical_resource_num == 1, DebugAssert{});
      allocation[0] = CreateTexture2dDsv(asset->allocator, resource_info->size, resource_info->format, resource_info->flags);
      break;
    }
    case ResourceCreationType::kNone: {
      return;
    }
  }
  (*asset->resource_index)[resource_id] = asset->allocations->size();
  for (uint32_t i = 0; i < resource_info->physical_resource_num; i++) {
    asset->allocations->push_back(allocation[i]);
    asset->resources->push_back(allocation[i]->GetResource());
    SetD3d12Name(allocation[i]->GetResource(), resource_id, i);
  }
}
auto GetCreationType(const char* const flag) {
  if (strcmp(flag, "rtv") == 0) {
    return ResourceCreationType::kRtv;
  }
  if (strcmp(flag, "dsv") == 0) {
    return ResourceCreationType::kDsv;
  }
  if (strcmp(flag, "present") == 0) {
    return ResourceCreationType::kNone;
  }
  if (strcmp(flag, "srv") == 0) {
    return ResourceCreationType::kNone;
  }
  DEBUG_ASSERT(false, DebugAssert{});
  return ResourceCreationType::kNone;
}
auto GetResourceFlags(const rapidjson::Value& array) {
  D3D12_RESOURCE_FLAGS flag = D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;
  for (const auto& entity : array.GetArray()) {
    if ((flag & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET) == 0 && strcmp(entity.GetString(), "rtv") == 0) {
      DEBUG_ASSERT((flag & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL) == 0, DebugAssert{});
      flag |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
      continue;
    }
    if ((flag & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL) == 0 && strcmp(entity.GetString(), "dsv") == 0) {
      DEBUG_ASSERT((flag & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET) == 0, DebugAssert{});
      flag |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
      continue;
    }
    if ((flag & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE) != 0 && strcmp(entity.GetString(), "srv") == 0) {
      flag &= ~D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;
      continue;
    }
  }
  return flag;
}
auto GetTotalPhysicalResourceNum(const StrHashMap<ResourceInfo>& resource_info) {
  uint32_t sum = 0;
  resource_info.iterate<uint32_t>([](uint32_t* sum, const StrHash, const ResourceInfo* info) {
    *sum += info->physical_resource_num;
  }, &sum);
  return sum;
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
  if (strcmp(format, "R24G8_TYPELESS") == 0) {
    return DXGI_FORMAT_R24G8_TYPELESS;
  }
  if (strcmp(format, "R24_UNORM_X8_TYPELESS") == 0) {
    return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
  }
  if (strcmp(format, "X24_TYPELESS_G8_UINT") == 0) {
    return DXGI_FORMAT_X24_TYPELESS_G8_UINT;
  }
  DEBUG_ASSERT(false, DebugAssert{});
  return DXGI_FORMAT_R8G8B8A8_UNORM;
}
Size2d GetSize2d(const rapidjson::Value& array) {
  return Size2d {
    .width = array[0].GetUint(),
    .height = array[1].GetUint(),
  };
}
void ParseResourceInfo(const rapidjson::Value& resources, StrHashMap<ResourceInfo>& resource_info) {
  for (const auto& resource : resources.GetArray()) {
    const auto name = resource["name"].GetString();
    const auto hash = GetStrHash(name);
    resource_info[hash] = {
      .creation_type = GetCreationType(resource["initial_flag"].GetString()),
      .flags = GetResourceFlags(resource["flags"]),
      .format = GetDxgiFormat(resource["format"].GetString()),
      .size = GetSize2d(resource["size"]),
      .physical_resource_num = resource["pingpong"].GetBool() ? 2U : 1U,
      .pingpong = resource["pingpong"].GetBool(),
    };
  }
}
void CollectResourceNames(const StrHashMap<ResourceInfo>& resource_info, StrHashMap<const char*>& resource_name) {
  resource_info.iterate<StrHashMap<const char*>>([](StrHashMap<const char*>* resource_name, const StrHash resource_id, const ResourceInfo*) {
    resource_name->insert(resource_id, GetStr(resource_id));},
    &resource_name);
}
ID3D12Resource* GetResource(const ResourceSet* resource_set, const StrHash id, const uint32_t index) {
  return (*resource_set->resources)[(*resource_set->resource_index)[id] + index];
}
void SetResource(StrHash id, ID3D12Resource* resource, ResourceSet* resource_set) {
  if (resource_set->resource_index->contains(id)) {
    (*resource_set->resources)[(*resource_set->resource_index)[id]] = resource;
    return;
  }
  (*resource_set->resource_index)[id] = resource_set->resource_index->size();
  resource_set->resources->push_back(resource);
}
D3D12MA::Allocator* CreateGpuMemoryAllocator(DxgiAdapter* adapter, D3d12Device* device) {
  using namespace D3D12MA;
  ALLOCATION_CALLBACKS allocation_callbacks{
    .pAllocate = GpuMemoryAllocatorAllocate,
    .pFree = GpuMemoryAllocatorDeallocate,
    .pPrivateData = nullptr,
  };
  ALLOCATOR_DESC allocator_desc{
    .Flags = ALLOCATOR_FLAG_SINGLETHREADED | ALLOCATOR_FLAG_DEFAULT_POOLS_NOT_ZEROED,
    .pDevice = device,
    .PreferredBlockSize = 0,
    .pAllocationCallbacks = &allocation_callbacks,
    .pAdapter = adapter,
  };
  Allocator* allocator;
  const auto hr = CreateAllocator(&allocator_desc, &allocator);
  DEBUG_ASSERT(SUCCEEDED(hr), DebugAssert{});
  return allocator;
}
void ReleaseGpuMemoryAllocator(D3D12MA::Allocator* allocator) {
  allocator->Release();
}
ResourceSet* CreateResources(const StrHashMap<ResourceInfo>& resource_info, D3D12MA::Allocator* allocator) {
  auto resource_set = New<ResourceSet>();
  resource_set->resource_index = New<StrHashMap<uint32_t>>();
  resource_set->allocations = New<ResizableArray<D3D12MA::Allocation*>>();
  resource_set->resources = New<ResizableArray<ID3D12Resource*>>();
  resource_set->resource_index->reserve(resource_info.size());
  const uint32_t physical_resource_num = GetTotalPhysicalResourceNum(resource_info);
  resource_set->allocations->reserve(physical_resource_num);
  resource_set->resources->reserve(physical_resource_num);
  ResourceSetCreationAsseet asset{
    .allocator = allocator,
    .resource_index = resource_set->resource_index,
    .allocations = resource_set->allocations,
    .resources = resource_set->resources,
  };
  resource_info.iterate<ResourceSetCreationAsseet>(CreateResourceImpl, &asset);
  return resource_set;
};
void ReleaseResources(ResourceSet* resource_set) {
  // allocation->GetResource() does not increment ref count.
  for (auto& allocation : (*resource_set->allocations)) {
    allocation->Release();
  }
  resource_set->resource_index->~StrHashMap<uint32_t>();
  resource_set->allocations->~ResizableArray<D3D12MA::Allocation*>();
  resource_set->resources->~ResizableArray<ID3D12Resource*>();
}
void InitPingpongCurrentWriteIndex(const StrHashMap<ResourceInfo>& resource_info, StrHashMap<uint32_t>& current_write_index) {
  resource_info.iterate<StrHashMap<uint32_t>>([](StrHashMap<uint32_t>* current_write_index, const StrHash resource_id, const ResourceInfo* resource_info) {
    if (resource_info->pingpong) {
      current_write_index->insert(resource_id, 0);
    }
  }, &current_write_index);
}
void AddResource(const StrHash id, ID3D12Resource** resource, const uint32_t resource_num, ResourceSet* resource_set) {
  (*resource_set->resource_index)[id] = resource_set->resources->size();
  for (uint32_t i = 0; i < resource_num; i++) {
    resource_set->resources->push_back(resource[i]);
    SetD3d12Name(resource[i], id, i);
  }
}
uint32_t GetPingpongIndexRead(const StrHashMap<uint32_t>& current_write_index, const StrHash id) {
  if (!current_write_index.contains(id)) {
    return 0;
  }
  return current_write_index[id] == 0 ? 1 : 0;
}
uint32_t GetPingpongIndexWrite(const StrHashMap<uint32_t>& current_write_index, const StrHash id) {
  if (!current_write_index.contains(id)) {
    return 0;
  }
  return current_write_index[id];
}
uint32_t GetPhysicalResourceNum(const StrHashMap<uint32_t>& current_write_index, const StrHash id) {
  if (!current_write_index.contains(id)) {
    return 1;
  }
  return 2;
}
} // namespace boke
#include "doctest/doctest.h"
TEST_CASE("resources") {
  using namespace boke;
  // render pass & resource info
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
  InitAllocator(main_buffer, main_buffer_size_in_bytes);
  // core units
  auto gfx_libraries = LoadGfxLibraries();
  auto dxgi = InitDxgi(gfx_libraries.dxgi_library, AdapterType::kHighPerformance);
  auto device = CreateDevice(gfx_libraries.d3d12_library, dxgi.adapter);
  // parse resource info
  const uint32_t primary_width = 1920;
  const uint32_t primary_height = 1080;
  StrHashMap<ResourceInfo> resource_info;
  ParseResourceInfo(GetJson("tests/resources.json"), resource_info);
  CHECK_EQ(resource_info.size(), 7);
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
  CHECK_EQ(resource_info["swapchain"_id].creation_type, ResourceCreationType::kNone);
  CHECK_EQ(resource_info["swapchain"_id].flags, D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE);
  CHECK_EQ(resource_info["swapchain"_id].format, DXGI_FORMAT_R8G8B8A8_UNORM);
  CHECK_EQ(resource_info["swapchain"_id].size.width, primary_width);
  CHECK_EQ(resource_info["swapchain"_id].size.height, primary_height);
  CHECK_EQ(resource_info["swapchain"_id].pingpong, false);
  // gpu resources
  auto gpu_memory_allocator = CreateGpuMemoryAllocator(dxgi.adapter, device);
  auto resource_set = CreateResources(resource_info, gpu_memory_allocator);
  auto& resource_index = *resource_set->resource_index;
  auto& allocations = *resource_set->allocations;
  auto& resources = *resource_set->resources;
  CHECK_EQ(resource_index.size(), 6);
  CHECK_UNARY(resource_index.contains("gbuffer0"_id));
  CHECK_UNARY(resource_index.contains("gbuffer1"_id));
  CHECK_UNARY(resource_index.contains("gbuffer2"_id));
  CHECK_UNARY(resource_index.contains("gbuffer3"_id));
  CHECK_UNARY(resource_index.contains("depth"_id));
  CHECK_UNARY(resource_index.contains("primary"_id));
  CHECK_EQ(allocations.size(), 7);
  CHECK_EQ(resources.size(), 7);
  for (uint32_t i = 0; i < 7; i++) {
    CAPTURE(i);
    CHECK_EQ(allocations[i]->GetResource(), resources[i]);
  }
  auto desc = resources[resource_index["gbuffer0"_id]]->GetDesc();
  CHECK_EQ(desc.Dimension, D3D12_RESOURCE_DIMENSION_TEXTURE2D);
  CHECK_EQ(desc.Width, primary_width);
  CHECK_EQ(desc.Height, primary_height);
  CHECK_EQ(desc.DepthOrArraySize, 1);
  CHECK_EQ(desc.MipLevels, 1);
  CHECK_EQ(desc.Format, DXGI_FORMAT_R8G8B8A8_UNORM);
  CHECK_EQ(desc.SampleDesc.Count, 1);
  CHECK_EQ(desc.SampleDesc.Quality, 0);
  CHECK_EQ(desc.Layout, D3D12_TEXTURE_LAYOUT_UNKNOWN);
  CHECK_EQ(desc.Flags, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
  desc = resources[resource_index["gbuffer1"_id]]->GetDesc();
  CHECK_EQ(desc.Dimension, D3D12_RESOURCE_DIMENSION_TEXTURE2D);
  CHECK_EQ(desc.Width, primary_width);
  CHECK_EQ(desc.Height, primary_height);
  CHECK_EQ(desc.DepthOrArraySize, 1);
  CHECK_EQ(desc.MipLevels, 1);
  CHECK_EQ(desc.Format, DXGI_FORMAT_R8G8B8A8_UNORM);
  CHECK_EQ(desc.SampleDesc.Count, 1);
  CHECK_EQ(desc.SampleDesc.Quality, 0);
  CHECK_EQ(desc.Layout, D3D12_TEXTURE_LAYOUT_UNKNOWN);
  CHECK_EQ(desc.Flags, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
  desc = resources[resource_index["gbuffer2"_id]]->GetDesc();
  CHECK_EQ(desc.Dimension, D3D12_RESOURCE_DIMENSION_TEXTURE2D);
  CHECK_EQ(desc.Width, primary_width);
  CHECK_EQ(desc.Height, primary_height);
  CHECK_EQ(desc.DepthOrArraySize, 1);
  CHECK_EQ(desc.MipLevels, 1);
  CHECK_EQ(desc.Format, DXGI_FORMAT_R10G10B10A2_UNORM);
  CHECK_EQ(desc.SampleDesc.Count, 1);
  CHECK_EQ(desc.SampleDesc.Quality, 0);
  CHECK_EQ(desc.Layout, D3D12_TEXTURE_LAYOUT_UNKNOWN);
  CHECK_EQ(desc.Flags, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
  desc = resources[resource_index["gbuffer3"_id]]->GetDesc();
  CHECK_EQ(desc.Dimension, D3D12_RESOURCE_DIMENSION_TEXTURE2D);
  CHECK_EQ(desc.Width, primary_width);
  CHECK_EQ(desc.Height, primary_height);
  CHECK_EQ(desc.DepthOrArraySize, 1);
  CHECK_EQ(desc.MipLevels, 1);
  CHECK_EQ(desc.Format, DXGI_FORMAT_R8G8B8A8_UNORM);
  CHECK_EQ(desc.SampleDesc.Count, 1);
  CHECK_EQ(desc.SampleDesc.Quality, 0);
  CHECK_EQ(desc.Layout, D3D12_TEXTURE_LAYOUT_UNKNOWN);
  CHECK_EQ(desc.Flags, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
  CHECK_EQ(desc.SampleDesc.Count, 1);
  CHECK_EQ(desc.SampleDesc.Quality, 0);
  CHECK_EQ(desc.Layout, D3D12_TEXTURE_LAYOUT_UNKNOWN);
  CHECK_EQ(desc.Flags, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
  desc = resources[resource_index["depth"_id]]->GetDesc();
  CHECK_EQ(desc.Dimension, D3D12_RESOURCE_DIMENSION_TEXTURE2D);
  CHECK_EQ(desc.Width, primary_width);
  CHECK_EQ(desc.Height, primary_height);
  CHECK_EQ(desc.DepthOrArraySize, 1);
  CHECK_EQ(desc.MipLevels, 1);
  CHECK_EQ(desc.Format, DXGI_FORMAT_R24G8_TYPELESS);
  CHECK_EQ(desc.SampleDesc.Count, 1);
  CHECK_EQ(desc.SampleDesc.Quality, 0);
  CHECK_EQ(desc.Layout, D3D12_TEXTURE_LAYOUT_UNKNOWN);
  CHECK_EQ(desc.Flags, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL | D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE);
  desc = resources[resource_index["primary"_id]]->GetDesc();
  CHECK_EQ(desc.Dimension, D3D12_RESOURCE_DIMENSION_TEXTURE2D);
  CHECK_EQ(desc.Width, primary_width);
  CHECK_EQ(desc.Height, primary_height);
  CHECK_EQ(desc.DepthOrArraySize, 1);
  CHECK_EQ(desc.MipLevels, 1);
  CHECK_EQ(desc.Format, DXGI_FORMAT_R16G16B16A16_FLOAT);
  desc = resources[resource_index["primary"_id] + 1]->GetDesc();
  CHECK_EQ(desc.Dimension, D3D12_RESOURCE_DIMENSION_TEXTURE2D);
  CHECK_EQ(desc.Width, primary_width);
  CHECK_EQ(desc.Height, primary_height);
  CHECK_EQ(desc.DepthOrArraySize, 1);
  CHECK_EQ(desc.MipLevels, 1);
  CHECK_EQ(desc.Format, DXGI_FORMAT_R16G16B16A16_FLOAT);
  // terminate
  ReleaseResources(resource_set);
  gpu_memory_allocator->Release();
  resource_info.~StrHashMap<ResourceInfo>();
  device->Release();
  TermDxgi(dxgi);
  ReleaseGfxLibraries(gfx_libraries);
  delete[] main_buffer;
}
