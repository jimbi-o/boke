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
  bool pingpong{};
};
struct ResourceSet {
  StrHashMap<uint32_t>& resource_index;
  Array<ID3D12Resource*>& resources;
};
DXGI_FORMAT GetDxgiFormat(const char* format);
Size2d GetSize2d(const rapidjson::Value&);
void ParseResourceInfo(const rapidjson::Value& resources, StrHashMap<ResourceInfo>& resource_info);
void InitPingpongCurrentWriteIndex(const StrHashMap<ResourceInfo>& resource_info, StrHashMap<uint32_t>& pingpong_current_write_index);
void CollectResourceNames(const StrHashMap<ResourceInfo>& resource_info, StrHashMap<const char*>& resource_name);
D3D12MA::Allocator* CreateGpuMemoryAllocator(DxgiAdapter* adapter, D3d12Device* device, AllocatorData* allocator_data);
void ReleaseGpuMemoryAllocator(D3D12MA::Allocator* allocator);
ResourceSet CreateResources(const StrHashMap<ResourceInfo>& resource_info, D3D12MA::Allocator* allocator, StrHashMap<uint32_t>& resource_index, Array<D3D12MA::Allocation*>& allocations, Array<ID3D12Resource*>& resources);
void ReleaseResources(StrHashMap<uint32_t>&& resource_index, Array<D3D12MA::Allocation*>&& allocations, Array<ID3D12Resource*>&& resources);
void AddResource(const StrHash id, ID3D12Resource** resource, const uint32_t resource_num, ResourceSet& resource_set);
uint32_t GetPingpongIndexRead(const StrHashMap<uint32_t>& pingpong_current_write_index, const StrHash id);
uint32_t GetPingpongIndexWrite(const StrHashMap<uint32_t>& pingpong_current_write_index, const StrHash id);
ID3D12Resource* GetResource(const ResourceSet& resource_set, const StrHash id, const uint32_t index);
uint32_t GetPhysicalResourceNum(const StrHashMap<uint32_t>& pingpong_current_write_index, const StrHash id);
}
