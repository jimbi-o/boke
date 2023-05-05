#pragma once
namespace boke {
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
}
