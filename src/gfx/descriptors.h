#pragma once
namespace boke {
struct ResourceInfo;
struct DescriptorHeaps {
  ID3D12DescriptorHeap* rtv{};
  ID3D12DescriptorHeap* dsv{};
  ID3D12DescriptorHeap* cbv_srv_uav{};
};
struct DescriptorHandleNum {
  uint32_t rtv{};
  uint32_t dsv{};
  uint32_t cbv_srv_uav{};
};
struct DescriptorHandleIncrementSize {
  uint32_t rtv{};
  uint32_t dsv{};
  uint32_t cbv_srv_uav{};
};
struct DescriptorHeapHeadAddr {
  D3D12_CPU_DESCRIPTOR_HANDLE rtv{};
  D3D12_CPU_DESCRIPTOR_HANDLE dsv{};
  D3D12_CPU_DESCRIPTOR_HANDLE cbv_srv_uav{};
};
struct DescriptorHeapSet {
  DescriptorHeaps descriptor_heaps{};
  DescriptorHandleIncrementSize increment_size{};
  DescriptorHeapHeadAddr head_addr{};
};
struct DescriptorHeapSetConfig {
  uint32_t extra_rtv_num{};
  uint32_t extra_dsv_num{};
  uint32_t extra_srv_num{};
};
struct DescriptorHandles {
  StrHashMap<D3D12_CPU_DESCRIPTOR_HANDLE>* rtv;
  StrHashMap<D3D12_CPU_DESCRIPTOR_HANDLE>* dsv;
  StrHashMap<D3D12_CPU_DESCRIPTOR_HANDLE>* srv;
};
DescriptorHeapSet CreateDescriptorHeaps(const StrHashMap<ResourceInfo>& resource_info, D3d12Device* device, const DescriptorHandleNum& extra_handle_num);
void PrepareDescriptorHandles(const StrHashMap<ResourceInfo>& resource_info, const StrHashMap<ID3D12Resource*>& resources, D3d12Device* device, const DescriptorHeapHeadAddr& descriptor_heap_head_addr, const DescriptorHandleIncrementSize& descriptor_handle_increment_size, DescriptorHandles& descriptor_handles);
void AddDescriptorHandlesRtv(const StrHash resource_id, DXGI_FORMAT format, ID3D12Resource** resources, const uint32_t resource_num, D3d12Device* device, const D3D12_CPU_DESCRIPTOR_HANDLE& descriptor_heap_head_addr, const uint32_t descriptor_handle_increment_size, DescriptorHandles& descriptor_handles);
void AddDescriptorHandlesDsv(const StrHash resource_id, DXGI_FORMAT format, ID3D12Resource** resources, const uint32_t resource_num, D3d12Device* device, const D3D12_CPU_DESCRIPTOR_HANDLE& descriptor_heap_head_addr, const uint32_t descriptor_handle_increment_size, DescriptorHandles& descriptor_handles);
void AddDescriptorHandlesSrv(const StrHash resource_id, DXGI_FORMAT format, ID3D12Resource** resources, const uint32_t resource_num, D3d12Device* device, const D3D12_CPU_DESCRIPTOR_HANDLE& descriptor_heap_head_addr, const uint32_t descriptor_handle_increment_size, DescriptorHandles& descriptor_handles);
} // namespace boke
