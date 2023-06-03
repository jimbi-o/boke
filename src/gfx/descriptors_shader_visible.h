#pragma once
#include "d3d12_name_alias.h"
namespace boke {
struct ShaderVisibleDescriptorHandleInfo {
  D3D12_CPU_DESCRIPTOR_HANDLE head_addr_cpu{};
  D3D12_GPU_DESCRIPTOR_HANDLE head_addr_gpu{};
  uint32_t increment_size{};
  uint32_t reserved_handle_num{};
  uint32_t total_handle_num{};
};
struct RenderPassInfo;
struct DescriptorHandles;
D3D12_GPU_DESCRIPTOR_HANDLE PrepareRenderPassShaderVisibleDescriptorHandles(const RenderPassInfo& render_pass_info, const DescriptorHandles* descriptor_handles, const StrHashMap<uint32_t>& current_write_index, D3d12Device* device, const ShaderVisibleDescriptorHandleInfo& info, uint32_t* occupied_handle_num);
}
