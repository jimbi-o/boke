#pragma once
namespace boke {
struct ResourceInfo;
struct ResourceSet;
struct DescriptorHeapSetConfig {
  uint32_t extra_rtv_num{};
  uint32_t extra_dsv_num{};
  uint32_t extra_srv_num{};
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
struct DescriptorHandleIncrementSize {
  uint32_t rtv{};
  uint32_t dsv{};
  uint32_t cbv_srv_uav{};
};
struct DescriptorHeapSet {
  DescriptorHeaps descriptor_heaps{};
  DescriptorHeapHeadAddr head_addr{};
  DescriptorHandleIncrementSize increment_size{};
};
struct DescriptorHandleNum {
  uint32_t rtv{};
  uint32_t dsv{};
  uint32_t cbv_srv_uav{};
};
struct DescriptorHeaps;
struct DescriptorHandles;
DescriptorHeapSet CreateDescriptorHeaps(const StrHashMap<ResourceInfo>& resource_info, D3d12Device* device, const DescriptorHandleNum& extra_handle_num);
void ReleaseDescriptorHeaps(DescriptorHeapSet&);
DescriptorHandles* PrepareDescriptorHandles(const StrHashMap<ResourceInfo>& resource_info, const ResourceSet* resource_set, D3d12Device* device, const DescriptorHeapHeadAddr& descriptor_heap_head_addr, const DescriptorHandleIncrementSize& descriptor_handle_increment_size);
void ReleaseDescriptorHandles(DescriptorHandles*);
void AddDescriptorHandlesRtv(const StrHash resource_id, DXGI_FORMAT format, ID3D12Resource** resources, const uint32_t resource_num, D3d12Device* device, const DescriptorHeapHeadAddr& descriptor_heap_head_addr, const DescriptorHandleIncrementSize& descriptor_handle_increment_size, DescriptorHandles* descriptor_handles);
void AddDescriptorHandlesDsv(const StrHash resource_id, DXGI_FORMAT format, ID3D12Resource** resources, const uint32_t resource_num, D3d12Device* device, const DescriptorHeapHeadAddr& descriptor_heap_head_addr, const DescriptorHandleIncrementSize& descriptor_handle_increment_size, DescriptorHandles* descriptor_handles);
void AddDescriptorHandlesSrv(const StrHash resource_id, DXGI_FORMAT format, ID3D12Resource** resources, const uint32_t resource_num, D3d12Device* device, const DescriptorHeapHeadAddr& descriptor_heap_head_addr, const DescriptorHandleIncrementSize& descriptor_handle_increment_size, DescriptorHandles* descriptor_handles);
void AddDescriptorHandlesCbv(const StrHash resource_id, ID3D12Resource** resources, const uint32_t resource_num, const uint32_t buffer_size_in_bytes, D3d12Device* device, const DescriptorHeapHeadAddr& descriptor_heap_head_addr, const DescriptorHandleIncrementSize& descriptor_handle_increment_size, DescriptorHandles* descriptor_handles);
D3D12_CPU_DESCRIPTOR_HANDLE GetDescriptorHandleRtv(const StrHash resource_id, const uint32_t index, const DescriptorHandles*);
D3D12_CPU_DESCRIPTOR_HANDLE GetDescriptorHandleDsv(const StrHash resource_id, const uint32_t index, const DescriptorHandles*);
D3D12_CPU_DESCRIPTOR_HANDLE GetDescriptorHandleSrv(const StrHash resource_id, const uint32_t index, const DescriptorHandles*);
D3D12_CPU_DESCRIPTOR_HANDLE GetDescriptorHandleCbv(const StrHash resource_id, const uint32_t index, const DescriptorHandles*);
ID3D12DescriptorHeap* CreateDescriptorHeap(D3d12Device* device, const D3D12_DESCRIPTOR_HEAP_TYPE descriptor_heap_type, const uint32_t descriptor_handle_num, const D3D12_DESCRIPTOR_HEAP_FLAGS descriptor_heap_flag);
} // namespace boke
