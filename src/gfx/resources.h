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
DXGI_FORMAT GetDxgiFormat(const char* format);
Size2d GetSize2d(const rapidjson::Value&);
void ParseResourceInfo(const rapidjson::Value& resources, StrHashMap<ResourceInfo>& resource_info);
void InitPingpongCurrentWriteIndex(const StrHashMap<ResourceInfo>& resource_info, StrHashMap<uint32_t>& pingpong_current_write_index);
StrHash GetPinpongResourceId(const StrHash id, const uint32_t index);
StrHash GetResourceIdPingpongRead(const StrHash id, const StrHashMap<uint32_t>& pingpong_current_write_index);
StrHash GetResourceIdPingpongWrite(const StrHash id, const StrHashMap<uint32_t>& pingpong_current_write_index);
D3D12MA::Allocator* CreateGpuMemoryAllocator(DxgiAdapter* adapter, D3d12Device* device, AllocatorData* allocator_data);
void ReleaseGpuMemoryAllocator(D3D12MA::Allocator* allocator);
void CreateResources(const StrHashMap<ResourceInfo>& resource_info, D3D12MA::Allocator* allocator, StrHashMap<D3D12MA::Allocation*>& allocations, StrHashMap<ID3D12Resource*>& resources);
void ReleaseAllocations(StrHashMap<D3D12MA::Allocation*>&& allocations, StrHashMap<ID3D12Resource*>&& resources);
}
