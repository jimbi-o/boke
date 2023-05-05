#pragma once
namespace boke {
struct DescriptorHandles {
  StrHashMap<D3D12_CPU_DESCRIPTOR_HANDLE>* rtv;
  StrHashMap<D3D12_CPU_DESCRIPTOR_HANDLE>* dsv;
  StrHashMap<D3D12_CPU_DESCRIPTOR_HANDLE>* srv;
};
} // namespace boke
