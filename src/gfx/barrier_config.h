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
  StrHashMap<uint32_t>* pingpong_current_write_index{};
  StrHashMap<BarrierTransitionInfoPerResource>* transition_info{};
  StrHashMap<BarrierTransitionInfoPerResource>* next_transition_info{};
};
void InitTransitionInfo(const StrHashMap<ResourceInfo>& resource_info, StrHashMap<BarrierTransitionInfoPerResource>& transition_info);
void ProcessBarriers(const RenderPassInfo& render_pass_info, StrHashMap<ID3D12Resource*>& resources, BarrierSet& barrier_set, D3d12CommandList* command_list);
}
