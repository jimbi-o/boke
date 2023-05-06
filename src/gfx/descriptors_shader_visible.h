#pragma once
namespace boke {
uint32_t GetShaderVisibleDescriptorNum(const RenderPassInfo& render_pass_info);
void CopyDescriptorsToShaderVisibleDescriptor(const RenderPassInfo& render_pass_info, DescriptorHandles& descriptor_handles, StrHashMap<uint32_t> pingpong_current_write_index, const uint32_t increment_size, D3d12Device* device, const D3D12_CPU_DESCRIPTOR_HANDLE& dst_handle, const uint32_t dst_handle_num);
}
