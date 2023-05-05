#include "dxgi1_6.h"
#include "boke/allocator.h"
#include "boke/container.h"
#include "boke/debug_assert.h"
#include "boke/str_hash.h"
#include "boke/util.h"
#include "core.h"
#include "descriptors.h"
#include "json.h"
#include "render_pass_info.h"
#include "resources.h"
namespace {
using namespace boke;
uint32_t GetShaderVisibleDescriptorNum(const RenderPassInfo& render_pass_info) {
  return render_pass_info.srv_num;
}
void CopyDescriptorsToShaderVisibleDescriptor(const RenderPassInfo& render_pass_info, DescriptorHandles& descriptor_handles, StrHashMap<uint32_t> pingpong_current_write_index, const uint32_t increment_size, D3d12Device* device, const D3D12_CPU_DESCRIPTOR_HANDLE& dst_handle, const uint32_t dst_handle_num) {
  DEBUG_ASSERT(render_pass_info.srv_num == dst_handle_num, DebugAssert{});
  if (render_pass_info.srv_num == 0) { return; }
  const uint32_t src_descriptor_num_len = 16;
  uint32_t src_descriptor_num[src_descriptor_num_len]{};
  D3D12_CPU_DESCRIPTOR_HANDLE src_descriptor_handles[src_descriptor_num_len]{};
  uint32_t src_descriptor_num_index = 0;
  {
    const auto resource_id = GetResourceIdPingpongRead(render_pass_info.srv[0], pingpong_current_write_index);
    const auto& handle = (*descriptor_handles.srv)[resource_id];
    src_descriptor_handles[src_descriptor_num_index].ptr = handle.ptr;
    src_descriptor_num[src_descriptor_num_index] = 1;
  }
  for (uint32_t i = 1; i < render_pass_info.srv_num; i++) {
    const auto resource_id = GetResourceIdPingpongRead(render_pass_info.srv[i], pingpong_current_write_index);
    const auto& handle = (*descriptor_handles.srv)[resource_id];
    if (src_descriptor_handles[src_descriptor_num_index].ptr + src_descriptor_num[src_descriptor_num_index] * increment_size == handle.ptr) {
      src_descriptor_num[src_descriptor_num_index]++;
      continue;
    }
    src_descriptor_num_index++;
    src_descriptor_handles[src_descriptor_num_index].ptr = handle.ptr;
    src_descriptor_num[src_descriptor_num_index] = 1;
    DEBUG_ASSERT(src_descriptor_num_index <= src_descriptor_num_len, DebugAssert{});
  }
  device->CopyDescriptors(dst_handle_num, &dst_handle, &render_pass_info.srv_num, src_descriptor_num_index + 1, src_descriptor_handles, src_descriptor_num, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
}
} // namespace
