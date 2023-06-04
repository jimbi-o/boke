#pragma once
#include "d3d12_name_alias.h"
namespace D3D12MA {
class Allocator;
class Allocation;
}
namespace boke {
struct RenderPassInfo;
struct Size2d {
  uint32_t width{};
  uint32_t height{};
};
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
  uint32_t physical_resource_num{};
  bool pingpong{false};
};
struct ResourceSet;
DXGI_FORMAT GetDxgiFormat(const char* format);
Size2d GetSize2d(const rapidjson::Value&);
StrHashMap<ResourceInfo> ParseResourceInfo(const rapidjson::Value& resources);
StrHashMap<uint32_t> InitWriteIndexList(const StrHashMap<ResourceInfo>& resource_info);
StrHashMap<const char*> CollectResourceNames(const StrHashMap<ResourceInfo>& resource_info);
D3D12MA::Allocator* CreateGpuMemoryAllocator(DxgiAdapter* adapter, D3d12Device* device);
void ReleaseGpuMemoryAllocator(D3D12MA::Allocator* allocator);
ResourceSet* CreateResources(const StrHashMap<ResourceInfo>& resource_info, D3D12MA::Allocator* allocator);
void ReleaseResources(ResourceSet*);
void AddResource(const StrHash id, ID3D12Resource** resource, const uint32_t resource_num, ResourceSet* resource_set);
uint32_t GetResourceLocalIndexRead(const StrHashMap<uint32_t>& current_write_index_list, const StrHash id);
uint32_t GetResourceLocalIndexWrite(const StrHashMap<uint32_t>& current_write_index_list, const StrHash id);
ID3D12Resource* GetResource(const ResourceSet* resource_set, const StrHash id, const uint32_t index);
}
