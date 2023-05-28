#pragma once
#include "d3d12_name_alias.h"
namespace boke {
struct ResourceInfo;
struct ResourceSet;
struct RenderPassInfo;
struct BarrierTransitionInfoIndex {
  uint32_t physical_resource_num{};
  uint32_t index{};
};
struct BarrierTransitionInfoPerResource {
  D3D12_BARRIER_SYNC   sync{D3D12_BARRIER_SYNC_NONE};
  D3D12_BARRIER_ACCESS access{D3D12_BARRIER_ACCESS_NO_ACCESS};
  D3D12_BARRIER_LAYOUT layout{};
};
struct BarrierTransitionInfo {
  StrHashMap<BarrierTransitionInfoIndex>& transition_info_index;
  ResizableArray<BarrierTransitionInfoPerResource>& transition_info;
};
void InitTransitionInfo(const StrHashMap<ResourceInfo>& resource_info, BarrierTransitionInfo& transition_info);
void AddTransitionInfo(const StrHash resource_id, const uint32_t transition_num, const D3D12_BARRIER_LAYOUT layout, BarrierTransitionInfo& transition_info);
void UpdateTransitionInfo(BarrierTransitionInfo& transition_info);
void FlipPingPongIndex(const RenderPassInfo& render_pass_info, const BarrierTransitionInfo& transition_info, StrHashMap<uint32_t>& pingpong_current_write_index);
void ConfigureRenderPassBarriersTextureTransitions(const RenderPassInfo& render_pass_info, const StrHashMap<uint32_t>& pingpong_current_write_index, BarrierTransitionInfo& transition_info);
void ProcessBarriers(const BarrierTransitionInfo& transition_info, const ResourceSet& resource_set, D3d12CommandList* command_list);
void ResetBarrierSyncAccessStatus(BarrierTransitionInfo& transition_info);
}
