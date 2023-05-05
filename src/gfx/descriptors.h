#pragma once
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
