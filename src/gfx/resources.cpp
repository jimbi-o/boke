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
namespace {
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
} // namespace
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
class DescriptorHandles final {
 public:
  DescriptorHandles(tote::AllocatorCallbacks<AllocatorData>);
  ~DescriptorHandles();
  StrHashMap<D3D12_CPU_DESCRIPTOR_HANDLE> rtv_handles;
  StrHashMap<D3D12_CPU_DESCRIPTOR_HANDLE> dsv_handles;
  StrHashMap<D3D12_CPU_DESCRIPTOR_HANDLE> srv_handles;
 private:
  DescriptorHandles() = delete;
  DescriptorHandles(const DescriptorHandles&) = delete;
  DescriptorHandles(DescriptorHandles&&) = delete;
  void operator=(const DescriptorHandles&) = delete;
  void operator=(DescriptorHandles&&) = delete;
};
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
auto CreateTexture2dRtv(D3D12MA::Allocator* allocator, const Size2d& size, const DXGI_FORMAT format, const D3D12_RESOURCE_FLAGS optional_flags = D3D12_RESOURCE_FLAG_NONE) {
  D3D12_CLEAR_VALUE clear_value{
    .Format = format,
    .Color = {},
  };
  return CreateTexture2d(allocator, size, format,
                         D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | optional_flags,
                         D3D12_BARRIER_LAYOUT_RENDER_TARGET,
                         &clear_value,
                         0, nullptr);
}
auto CreateTexture2dDsv(D3D12MA::Allocator* allocator, const Size2d& size, const DXGI_FORMAT format, const D3D12_RESOURCE_FLAGS optional_flags = D3D12_RESOURCE_FLAG_NONE) {
  D3D12_CLEAR_VALUE clear_value{
    .Format = format,
    .DepthStencil = {
      .Depth = 1.0f, // set 0.0f for inverse-z
      .Stencil = 0,
    },
  };
  return CreateTexture2d(allocator, size, format,
                         D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL | optional_flags,
                         D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE,
                         &clear_value,
                         0, nullptr);
}
DescriptorHandles::DescriptorHandles(tote::AllocatorCallbacks<AllocatorData> allocator_data)
    : rtv_handles(allocator_data)
    , dsv_handles(allocator_data)
    , srv_handles(allocator_data) {}
DescriptorHandles::~DescriptorHandles() {}
} // namespace
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
  StrHashMap<ID3D12Resource*> resources(GetAllocatorCallbacks(allocator_data));
  // CreateResources(resource_info, resources);
  CHECK_EQ(resources.size(), 6);
  CHECK_NE(resources["gbuffer0"_id], nullptr);
  CHECK_NE(resources["gbuffer1"_id], nullptr);
  CHECK_NE(resources["gbuffer2"_id], nullptr);
  CHECK_NE(resources["gbuffer3"_id], nullptr);
  CHECK_NE(resources["depth"_id], nullptr);
  CHECK_NE(resources[GetPinpongResourceId("primary"_id, 0)], nullptr);
  CHECK_NE(resources[GetPinpongResourceId("primary"_id, 1)], nullptr);
  DescriptorHandles descriptor_handles(GetAllocatorCallbacks(allocator_data));
  // PrepareDescriptorHandles(resources, descriptor_handles);
  CHECK_EQ(descriptor_handles.rtv_handles.size(), 9);
  CHECK_NE(descriptor_handles.rtv_handles["gbuffer0"_id].ptr, 0UL);
  CHECK_NE(descriptor_handles.rtv_handles["gbuffer1"_id].ptr, 0UL);
  CHECK_NE(descriptor_handles.rtv_handles["gbuffer2"_id].ptr, 0UL);
  CHECK_NE(descriptor_handles.rtv_handles["gbuffer3"_id].ptr, 0UL);
  CHECK_NE(descriptor_handles.rtv_handles[GetPinpongResourceId("primary"_id, 0)].ptr, 0UL);
  CHECK_NE(descriptor_handles.rtv_handles[GetPinpongResourceId("primary"_id, 1)].ptr, 0UL);
  CHECK_NE(descriptor_handles.rtv_handles[GetPinpongResourceId("primary"_id, 0)].ptr, descriptor_handles.rtv_handles[GetPinpongResourceId("primary"_id, 1)].ptr);
  CHECK_NE(descriptor_handles.rtv_handles[GetPinpongResourceId("swapchain"_id, 0)].ptr, 0UL);
  CHECK_NE(descriptor_handles.rtv_handles[GetPinpongResourceId("swapchain"_id, 1)].ptr, 0UL);
  CHECK_NE(descriptor_handles.rtv_handles[GetPinpongResourceId("swapchain"_id, 2)].ptr, 0UL);
  CHECK_NE(descriptor_handles.rtv_handles[GetPinpongResourceId("swapchain"_id, 0)].ptr, descriptor_handles.rtv_handles[GetPinpongResourceId("swapchain"_id, 1)].ptr);
  CHECK_NE(descriptor_handles.rtv_handles[GetPinpongResourceId("swapchain"_id, 0)].ptr, descriptor_handles.rtv_handles[GetPinpongResourceId("swapchain"_id, 2)].ptr);
  CHECK_NE(descriptor_handles.rtv_handles[GetPinpongResourceId("swapchain"_id, 1)].ptr, descriptor_handles.rtv_handles[GetPinpongResourceId("swapchain"_id, 2)].ptr);
  CHECK_NE(descriptor_handles.dsv_handles.size(), 1);
  CHECK_NE(descriptor_handles.dsv_handles["depth"_id].ptr, 0UL);
  CHECK_NE(descriptor_handles.srv_handles.size(), 7);
  CHECK_NE(descriptor_handles.srv_handles["gbuffer0"_id].ptr, 0UL);
  CHECK_NE(descriptor_handles.srv_handles["gbuffer1"_id].ptr, 0UL);
  CHECK_NE(descriptor_handles.srv_handles["gbuffer2"_id].ptr, 0UL);
  CHECK_NE(descriptor_handles.srv_handles["gbuffer3"_id].ptr, 0UL);
  CHECK_NE(descriptor_handles.srv_handles[GetPinpongResourceId("primary"_id, 0)].ptr, 0UL);
  CHECK_NE(descriptor_handles.srv_handles[GetPinpongResourceId("primary"_id, 1)].ptr, 0UL);
  CHECK_NE(descriptor_handles.srv_handles[GetPinpongResourceId("primary"_id, 0)].ptr, descriptor_handles.srv_handles[GetPinpongResourceId("primary"_id, 1)].ptr);
  CHECK_NE(descriptor_handles.srv_handles["imgui_font"_id].ptr, 0UL);
  // terminate
  gpu_memory_allocator->Release();
  descriptor_handles.~DescriptorHandles();
  resources.~StrHashMap<ID3D12Resource*>();
  resource_info.~StrHashMap<ResourceInfo>();
  device->Release();
  TermDxgi(dxgi);
  ReleaseGfxLibraries(gfx_libraries);
  delete[] main_buffer;
}
