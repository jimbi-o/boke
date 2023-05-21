#pragma once
#include "d3d12_name_alias.h"
namespace boke {
struct ResourceInfo;
struct RenderPassInfo;
struct BarrierTransitionInfoPerResource {
  D3D12_BARRIER_SYNC   sync{D3D12_BARRIER_SYNC_NONE};
  D3D12_BARRIER_ACCESS access{D3D12_BARRIER_ACCESS_NO_ACCESS};
  D3D12_BARRIER_LAYOUT layout{};
};
struct BarrierSet {
  StrHashMap<BarrierTransitionInfoPerResource>* transition_info{};
  StrHashMap<BarrierTransitionInfoPerResource>* next_transition_info{};
};
void InitTransitionInfo(const StrHashMap<ResourceInfo>& resource_info, StrHashMap<BarrierTransitionInfoPerResource>& transition_info);
void UpdateTransitionInfo(BarrierSet& barrier_set);
void FlipPingPongIndex(const RenderPassInfo& render_pass_info, const BarrierSet& barrier_set, StrHashMap<uint32_t>& pingpong_current_write_index);
void ConfigureRenderPassBarriersTextureTransitions(const RenderPassInfo& render_pass_info, const StrHashMap<uint32_t>& pingpong_current_write_index, BarrierSet& barrier_set);
void ProcessBarriers(const BarrierSet& barrier_set, const StrHashMap<ID3D12Resource*>& resources, const StrHashMap<uint32_t>& pingpong_current_write_index, D3d12CommandList* command_list);
void ResetBarrierSyncAccessStatus(BarrierSet& barrier_set);
}
