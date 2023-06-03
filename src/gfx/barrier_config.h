#pragma once
#include "d3d12_name_alias.h"
namespace boke {
struct ResourceInfo;
struct ResourceSet;
struct RenderPassInfo;
struct BarrierTransitionInfo;
BarrierTransitionInfo* InitTransitionInfo(const StrHashMap<ResourceInfo>& resource_info);
void ReleaseTransitionInfo(BarrierTransitionInfo*);
void AddTransitionInfo(const StrHash resource_id, const uint32_t transition_num, const D3D12_BARRIER_LAYOUT layout, BarrierTransitionInfo* transition_info);
void UpdateTransitionInfo(BarrierTransitionInfo* transition_info);
void FlipPingPongIndex(const RenderPassInfo& render_pass_info, const BarrierTransitionInfo* transition_info, const StrHashMap<ResourceInfo>& resource_info, StrHashMap<uint32_t>& current_write_index_list);
void ConfigureRenderPassBarriersTextureTransitions(const RenderPassInfo& render_pass_info, const StrHashMap<uint32_t>& current_write_index_list, BarrierTransitionInfo* transition_info);
void ProcessBarriers(const BarrierTransitionInfo* transition_info, const ResourceSet* resource_set, D3d12CommandList* command_list);
void ResetBarrierSyncAccessStatus(BarrierTransitionInfo* transition_info);
}
