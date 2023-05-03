#include "boke/allocator.h"
#include "boke/container.h"
#include "boke/debug_assert.h"
#include "boke/str_hash.h"
namespace boke {
struct RenderPassInfo {
  StrHash queue{};
  StrHash* srv{};
  uint32_t srv_num{};
  StrHash* rtv{};
  uint32_t rtv_num{};
  StrHash  dsv{};
  StrHash  present{};
};
struct BarrierTransitionInfoPerResource {
  D3D12_BARRIER_LAYOUT layout{};
  D3D12_BARRIER_SYNC   sync{D3D12_BARRIER_SYNC_NONE};
  D3D12_BARRIER_ACCESS access{D3D12_BARRIER_ACCESS_NO_ACCESS};
};
struct RenderPassBarrierTransitionInfo {
  StrHash resource_id{};
  D3D12_BARRIER_SYNC sync_before{};
  D3D12_BARRIER_SYNC sync_after{};
  D3D12_BARRIER_ACCESS access_before{};
  D3D12_BARRIER_ACCESS access_after{};
  D3D12_BARRIER_LAYOUT layout_before{};
  D3D12_BARRIER_LAYOUT layout_after{};
};
auto HashInteger(const uint64_t x) {
  // Jenkins hash
  const uint8_t* data = reinterpret_cast<const uint8_t*>(&x);
  const size_t length = sizeof(x);
  uint64_t hash = 0;
  for (size_t i = 0; i < length; ++i) {
    hash += data[i];
    hash += (hash << 10);
    hash ^= (hash >> 6);
  }
  hash += (hash << 3);
  hash ^= (hash >> 11);
  hash += (hash << 15);
  return hash;
}
auto GetPinpongResourceId(const StrHash id, const uint32_t index) {
  return HashInteger(id + index);
}
auto GetResourceId(const StrHash id, const StrHashMap<uint32_t>& pingpong_current_write_index) {
  if (!pingpong_current_write_index.contains(id)) { return id; }
  return GetPinpongResourceId(id, pingpong_current_write_index[id]);
}
auto GetPingPongFlippingResourceList(const RenderPassInfo& render_pass, const uint32_t result_len, StrHash* result) {
  uint32_t result_num = 0;
  // TODO
  return result_num;
}
auto ConfigureBarriersTextureTransitions(const RenderPassInfo& next_render_pass,
                                         const uint32_t pingpong_flip_list_len, const StrHash* pingpong_flip_list, const StrHashMap<uint32_t>& pingpong_current_write_index,
                                         StrHashMap<BarrierTransitionInfoPerResource>& resource_info,
                                         const uint32_t barrier_transition_info_len, RenderPassBarrierTransitionInfo* barrier_transition_info) {
  uint32_t barrier_num = 0;
#if 0
  for (uint32_t i = 0; i < next_render_pass.srv_num; i++) {
    const auto resource_id = GetResourceId(next_render_pass.srv[i], pingpong_current_write_index);
    if (current_layout[resource_id] == D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE) { continue; }
    auto& barrier = barrier_transition_info[barrier_num];
    barrier.resource_id   = resource_id;
    barrier.sync_before   = current_sync.contains(resource_id) ? current_sync[resource_id] : D3D12_BARRIER_SYNC_NONE;
    barrier.sync_after    = D3D12_BARRIER_SYNC_PIXEL_SHADING;
    barrier.access_before = current_access.contains(resource_id) ? current_access[resource_id] : D3D12_BARRIER_ACCESS_NO_ACCESS;
    barrier.access_after  = D3D12_BARRIER_ACCESS_SHADER_RESOURCE;
    barrier.layout_before = current_layout[resource_id];
    barrier.layout_after  = D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE;
    current_sync[resource_id]   = barrier.sync_after;
    current_access[resource_id] = barrier.access_after;
    current_layout[resource_id] = barrier.layout_after;
    barrier_num++;
    DEBUG_ASSERT(barrier_num <= barrier_transition_info_len, DebugAssert{});
  }
  for (uint32_t i = 0; i < next_render_pass.rtv_num; i++) {
    const auto resource_id = GetResourceId(next_render_pass.rtv[i], pingpong_current_write_index);
    if (current_layout[resource_id] == D3D12_BARRIER_LAYOUT_RENDER_TARGET) { continue; }
    auto& barrier = barrier_transition_info[barrier_num];
    barrier.resource_id   = resource_id;
    barrier.sync_before   = current_sync.contains(resource_id) ? current_sync[resource_id] : D3D12_BARRIER_SYNC_NONE;
    barrier.sync_after    = D3D12_BARRIER_SYNC_RENDER_TARGET;
    barrier.access_before = current_access.contains(resource_id) ? current_access[resource_id] : D3D12_BARRIER_ACCESS_NO_ACCESS;
    barrier.access_after  = D3D12_BARRIER_ACCESS_RENDER_TARGET;
    barrier.layout_before = current_layout[resource_id];
    barrier.layout_after  = D3D12_BARRIER_LAYOUT_RENDER_TARGET;
    current_sync[resource_id]   = barrier.sync_after;
    current_access[resource_id] = barrier.access_after;
    current_layout[resource_id] = barrier.layout_after;
    barrier_num++;
    DEBUG_ASSERT(barrier_num <= barrier_transition_info_len, DebugAssert{});
  }
  if (next_render_pass.dsv != StrHash{}) {
    const auto resource_id = GetResourceId(next_render_pass.dsv, pingpong_current_write_index);
    if (current_layout[resource_id] != D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE) {
      auto& barrier = barrier_transition_info[barrier_num];
      barrier.resource_id   = resource_id;
      barrier.sync_before   = current_sync.contains(resource_id) ? current_sync[resource_id] : D3D12_BARRIER_SYNC_NONE;
      barrier.sync_after    = D3D12_BARRIER_SYNC_DEPTH_STENCIL;
      barrier.access_before = current_access.contains(resource_id) ? current_access[resource_id] : D3D12_BARRIER_ACCESS_NO_ACCESS;
      barrier.access_after  = D3D12_BARRIER_ACCESS_DEPTH_STENCIL_WRITE;
      barrier.layout_before = current_layout[resource_id];
      barrier.layout_after  = D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE;
      current_sync[resource_id]   = barrier.sync_after;
      current_access[resource_id] = barrier.access_after;
      current_layout[resource_id] = barrier.layout_after;
      barrier_num++;
      DEBUG_ASSERT(barrier_num <= barrier_transition_info_len, DebugAssert{});
    }
  }
  if (next_render_pass.present != StrHash{}) {
    const auto resource_id = GetResourceId(next_render_pass.present, pingpong_current_write_index);
    if (current_layout[resource_id] != D3D12_BARRIER_LAYOUT_PRESENT) {
      auto& barrier = barrier_transition_info[barrier_num];
      barrier.resource_id   = resource_id;
      barrier.sync_before   = current_sync.contains(resource_id) ? current_sync[resource_id] : D3D12_BARRIER_SYNC_NONE;
      barrier.sync_after    = D3D12_BARRIER_SYNC_NONE;
      barrier.access_before = current_access.contains(resource_id) ? current_access[resource_id] : D3D12_BARRIER_ACCESS_NO_ACCESS;
      barrier.access_after  = D3D12_BARRIER_ACCESS_NO_ACCESS;
      barrier.layout_before = current_layout[resource_id];
      barrier.layout_after  = D3D12_BARRIER_LAYOUT_PRESENT;
      current_sync[resource_id]   = barrier.sync_after;
      current_access[resource_id] = barrier.access_after;
      current_layout[resource_id] = barrier.layout_after;
      barrier_num++;
      DEBUG_ASSERT(barrier_num <= barrier_transition_info_len, DebugAssert{});
    }
  }
#endif
  return barrier_num;
}
auto FlipPingPongIndex(const uint32_t list_len, const StrHash* flip_list, StrHashMap<uint32_t>& pingpong_current_write_index) {
}
}
#include "doctest/doctest.h"
TEST_CASE("barrier config") {
  using namespace boke;
  const uint32_t main_buffer_size_in_bytes = 16 * 1024;
  std::byte main_buffer[main_buffer_size_in_bytes];
  auto allocator_data = GetAllocatorData(main_buffer, main_buffer_size_in_bytes);
  StrHash gbuffers[] = {"gbuffer0"_id, "gbuffer1"_id, "gbuffer2"_id, "gbuffer3"_id,};
  StrHash primary[] = {"primary"_id,};
  StrHash swapchain[] = {"swapchain"_id,};
  StrHash imgui_font[] = {"imgui"_id,};
  RenderPassInfo render_pass_info[] = {
    {
      // gbuffer
      .queue = "direct"_id,
      .rtv = gbuffers,
      .rtv_num = 4,
      .dsv = "depth"_id,
    },
    {
      // lighting
      .queue = "direct"_id,
      .srv = gbuffers,
      .srv_num = 4,
      .rtv = primary,
      .rtv_num = 1,
    },
    {
      // tonemap
      .queue = "direct"_id,
      .srv = primary,
      .srv_num = 1,
      .rtv = primary,
      .rtv_num = 1,
    },
    {
      // oetf
      .queue = "direct"_id,
      .srv = primary,
      .srv_num = 1,
      .rtv = swapchain,
      .rtv_num = 1,
    },
    {
      // imgui
      .queue = "direct"_id,
      .srv = imgui_font,
      .srv_num = 1,
      .rtv = swapchain,
      .rtv_num = 1,
    },
    {
      // present
      .queue = "direct"_id,
      .present = "swapchain"_id,
    },
  };
  StrHashMap<BarrierTransitionInfoPerResource> barrier_transition_info_per_resource(GetAllocatorCallbacks(allocator_data));
  barrier_transition_info_per_resource["gbuffer0"_id].layout = D3D12_BARRIER_LAYOUT_RENDER_TARGET;
  barrier_transition_info_per_resource["gbuffer1"_id].layout = D3D12_BARRIER_LAYOUT_RENDER_TARGET;
  barrier_transition_info_per_resource["gbuffer2"_id].layout = D3D12_BARRIER_LAYOUT_RENDER_TARGET;
  barrier_transition_info_per_resource["gbuffer3"_id].layout = D3D12_BARRIER_LAYOUT_RENDER_TARGET;
  barrier_transition_info_per_resource["depth"_id].layout = D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE;
  barrier_transition_info_per_resource[GetPinpongResourceId("primary"_id, 0)].layout = D3D12_BARRIER_LAYOUT_RENDER_TARGET;
  barrier_transition_info_per_resource[GetPinpongResourceId("primary"_id, 1)].layout = D3D12_BARRIER_LAYOUT_RENDER_TARGET;
  barrier_transition_info_per_resource["imgui_font"_id].layout = D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE;
  barrier_transition_info_per_resource["swapchain"_id].layout = D3D12_BARRIER_LAYOUT_PRESENT;
  StrHashMap<uint32_t> pingpong_current_write_index(GetAllocatorCallbacks(allocator_data));
  pingpong_current_write_index["primary"_id] = 0;
  const uint32_t pingpong_flip_list_len = 16;
  StrHash pingpong_flip_list[pingpong_flip_list_len]{};
  const uint32_t barrier_transition_info_len = 16;
  RenderPassBarrierTransitionInfo barriers[barrier_transition_info_len]{};
  // gbuffer
  auto current_render_pass_pingpong_flip_list_result_len = GetPingPongFlippingResourceList(render_pass_info[0], pingpong_flip_list_len, pingpong_flip_list);
  CHECK_EQ(current_render_pass_pingpong_flip_list_result_len, 0);
  auto barrier_num = ConfigureBarriersTextureTransitions(render_pass_info[0], pingpong_flip_list_len, pingpong_flip_list, pingpong_current_write_index, barrier_transition_info_per_resource, barrier_transition_info_len, barriers);
  CHECK_EQ(barrier_num, 0);
  CHECK_EQ(barrier_transition_info_per_resource["gbuffer0"_id].layout, D3D12_BARRIER_LAYOUT_RENDER_TARGET);
  CHECK_EQ(barrier_transition_info_per_resource["gbuffer1"_id].layout, D3D12_BARRIER_LAYOUT_RENDER_TARGET);
  CHECK_EQ(barrier_transition_info_per_resource["gbuffer2"_id].layout, D3D12_BARRIER_LAYOUT_RENDER_TARGET);
  CHECK_EQ(barrier_transition_info_per_resource["gbuffer3"_id].layout, D3D12_BARRIER_LAYOUT_RENDER_TARGET);
  CHECK_EQ(barrier_transition_info_per_resource["depth"_id].layout, D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE);
  CHECK_EQ(barrier_transition_info_per_resource[GetPinpongResourceId("primary"_id, 0)].layout, D3D12_BARRIER_LAYOUT_RENDER_TARGET);
  CHECK_EQ(barrier_transition_info_per_resource[GetPinpongResourceId("primary"_id, 1)].layout, D3D12_BARRIER_LAYOUT_RENDER_TARGET);
  CHECK_EQ(barrier_transition_info_per_resource["imgui_font"_id].layout, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(barrier_transition_info_per_resource["swapchain"_id].layout, D3D12_BARRIER_LAYOUT_PRESENT);
  CHECK_EQ(barrier_transition_info_per_resource["gbuffer0"_id].sync, D3D12_BARRIER_SYNC_RENDER_TARGET);
  CHECK_EQ(barrier_transition_info_per_resource["gbuffer1"_id].sync, D3D12_BARRIER_SYNC_RENDER_TARGET);
  CHECK_EQ(barrier_transition_info_per_resource["gbuffer2"_id].sync, D3D12_BARRIER_SYNC_RENDER_TARGET);
  CHECK_EQ(barrier_transition_info_per_resource["gbuffer3"_id].sync, D3D12_BARRIER_SYNC_RENDER_TARGET);
  CHECK_EQ(barrier_transition_info_per_resource["depth"_id].sync, D3D12_BARRIER_SYNC_DEPTH_STENCIL);
  CHECK_EQ(barrier_transition_info_per_resource[GetPinpongResourceId("primary"_id, 0)].sync, D3D12_BARRIER_SYNC_NONE);
  CHECK_EQ(barrier_transition_info_per_resource[GetPinpongResourceId("primary"_id, 1)].sync, D3D12_BARRIER_SYNC_NONE);
  CHECK_EQ(barrier_transition_info_per_resource["imgui_font"_id].sync, D3D12_BARRIER_SYNC_NONE);
  CHECK_EQ(barrier_transition_info_per_resource["swapchain"_id].sync, D3D12_BARRIER_SYNC_NONE);
  CHECK_EQ(barrier_transition_info_per_resource["gbuffer0"_id].access, D3D12_BARRIER_ACCESS_RENDER_TARGET);
  CHECK_EQ(barrier_transition_info_per_resource["gbuffer1"_id].access, D3D12_BARRIER_ACCESS_RENDER_TARGET);
  CHECK_EQ(barrier_transition_info_per_resource["gbuffer2"_id].access, D3D12_BARRIER_ACCESS_RENDER_TARGET);
  CHECK_EQ(barrier_transition_info_per_resource["gbuffer3"_id].access, D3D12_BARRIER_ACCESS_RENDER_TARGET);
  CHECK_EQ(barrier_transition_info_per_resource["depth"_id].access, D3D12_BARRIER_ACCESS_DEPTH_STENCIL_WRITE);
  CHECK_EQ(barrier_transition_info_per_resource[GetPinpongResourceId("primary"_id, 0)].access, D3D12_BARRIER_ACCESS_NO_ACCESS);
  CHECK_EQ(barrier_transition_info_per_resource[GetPinpongResourceId("primary"_id, 1)].access, D3D12_BARRIER_ACCESS_NO_ACCESS);
  CHECK_EQ(barrier_transition_info_per_resource["imgui_font"_id].access, D3D12_BARRIER_ACCESS_NO_ACCESS);
  CHECK_EQ(barrier_transition_info_per_resource["swapchain"_id].access, D3D12_BARRIER_ACCESS_NO_ACCESS);
  FlipPingPongIndex(current_render_pass_pingpong_flip_list_result_len, pingpong_flip_list, pingpong_current_write_index);
  CHECK_EQ(pingpong_current_write_index["primary"_id], 0);
  // lighting
  current_render_pass_pingpong_flip_list_result_len = GetPingPongFlippingResourceList(render_pass_info[1], pingpong_flip_list_len, pingpong_flip_list);
  CHECK_EQ(pingpong_flip_list_len, 0);
  barrier_num = ConfigureBarriersTextureTransitions(render_pass_info[1], pingpong_flip_list_len, pingpong_flip_list, pingpong_current_write_index, barrier_transition_info_per_resource, barrier_transition_info_len, barriers);
  CHECK_EQ(barrier_num, 4);
  CHECK_EQ(barriers[0].resource_id, "gbuffer0"_id);
  CHECK_EQ(barriers[0].sync_before, D3D12_BARRIER_SYNC_RENDER_TARGET);
  CHECK_EQ(barriers[0].sync_after, D3D12_BARRIER_SYNC_PIXEL_SHADING);
  CHECK_EQ(barriers[0].access_before, D3D12_BARRIER_ACCESS_RENDER_TARGET);
  CHECK_EQ(barriers[0].access_after, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
  CHECK_EQ(barriers[0].layout_before, D3D12_BARRIER_LAYOUT_RENDER_TARGET);
  CHECK_EQ(barriers[0].layout_after, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(barriers[1].resource_id, "gbuffer1"_id);
  CHECK_EQ(barriers[1].sync_before, D3D12_BARRIER_SYNC_RENDER_TARGET);
  CHECK_EQ(barriers[1].sync_after, D3D12_BARRIER_SYNC_PIXEL_SHADING);
  CHECK_EQ(barriers[1].access_before, D3D12_BARRIER_ACCESS_RENDER_TARGET);
  CHECK_EQ(barriers[1].access_after, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
  CHECK_EQ(barriers[1].layout_before, D3D12_BARRIER_LAYOUT_RENDER_TARGET);
  CHECK_EQ(barriers[1].layout_after, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(barriers[2].resource_id, "gbuffer2"_id);
  CHECK_EQ(barriers[2].sync_before, D3D12_BARRIER_SYNC_RENDER_TARGET);
  CHECK_EQ(barriers[2].sync_after, D3D12_BARRIER_SYNC_PIXEL_SHADING);
  CHECK_EQ(barriers[2].access_before, D3D12_BARRIER_ACCESS_RENDER_TARGET);
  CHECK_EQ(barriers[2].access_after, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
  CHECK_EQ(barriers[2].layout_before, D3D12_BARRIER_LAYOUT_RENDER_TARGET);
  CHECK_EQ(barriers[2].layout_after, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(barriers[3].resource_id, "gbuffer3"_id);
  CHECK_EQ(barriers[3].sync_before, D3D12_BARRIER_SYNC_RENDER_TARGET);
  CHECK_EQ(barriers[3].sync_after, D3D12_BARRIER_SYNC_PIXEL_SHADING);
  CHECK_EQ(barriers[3].access_before, D3D12_BARRIER_ACCESS_RENDER_TARGET);
  CHECK_EQ(barriers[3].access_after, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
  CHECK_EQ(barriers[3].layout_before, D3D12_BARRIER_LAYOUT_RENDER_TARGET);
  CHECK_EQ(barriers[3].layout_after, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(barrier_transition_info_per_resource["gbuffer0"_id].layout, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(barrier_transition_info_per_resource["gbuffer1"_id].layout, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(barrier_transition_info_per_resource["gbuffer2"_id].layout, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(barrier_transition_info_per_resource["gbuffer3"_id].layout, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(barrier_transition_info_per_resource["depth"_id].layout, D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE);
  CHECK_EQ(barrier_transition_info_per_resource[GetPinpongResourceId("primary"_id, 0)].layout, D3D12_BARRIER_LAYOUT_RENDER_TARGET);
  CHECK_EQ(barrier_transition_info_per_resource[GetPinpongResourceId("primary"_id, 1)].layout, D3D12_BARRIER_LAYOUT_RENDER_TARGET);
  CHECK_EQ(barrier_transition_info_per_resource["imgui_font"_id].layout, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(barrier_transition_info_per_resource["swapchain"_id].layout, D3D12_BARRIER_LAYOUT_PRESENT);
  CHECK_EQ(barrier_transition_info_per_resource["gbuffer0"_id].sync, D3D12_BARRIER_SYNC_PIXEL_SHADING);
  CHECK_EQ(barrier_transition_info_per_resource["gbuffer1"_id].sync, D3D12_BARRIER_SYNC_PIXEL_SHADING);
  CHECK_EQ(barrier_transition_info_per_resource["gbuffer2"_id].sync, D3D12_BARRIER_SYNC_PIXEL_SHADING);
  CHECK_EQ(barrier_transition_info_per_resource["gbuffer3"_id].sync, D3D12_BARRIER_SYNC_PIXEL_SHADING);
  CHECK_EQ(barrier_transition_info_per_resource["depth"_id].sync, D3D12_BARRIER_SYNC_DEPTH_STENCIL);
  CHECK_EQ(barrier_transition_info_per_resource[GetPinpongResourceId("primary"_id, 0)].sync, D3D12_BARRIER_SYNC_RENDER_TARGET);
  CHECK_EQ(barrier_transition_info_per_resource[GetPinpongResourceId("primary"_id, 1)].sync, D3D12_BARRIER_SYNC_NONE);
  CHECK_EQ(barrier_transition_info_per_resource["imgui_font"_id].sync, D3D12_BARRIER_SYNC_NONE);
  CHECK_EQ(barrier_transition_info_per_resource["swapchain"_id].sync, D3D12_BARRIER_SYNC_NONE);
  CHECK_EQ(barrier_transition_info_per_resource["gbuffer0"_id].access, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
  CHECK_EQ(barrier_transition_info_per_resource["gbuffer1"_id].access, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
  CHECK_EQ(barrier_transition_info_per_resource["gbuffer2"_id].access, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
  CHECK_EQ(barrier_transition_info_per_resource["gbuffer3"_id].access, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
  CHECK_EQ(barrier_transition_info_per_resource["depth"_id].access, D3D12_BARRIER_ACCESS_DEPTH_STENCIL_WRITE);
  CHECK_EQ(barrier_transition_info_per_resource[GetPinpongResourceId("primary"_id, 0)].access, D3D12_BARRIER_ACCESS_RENDER_TARGET);
  CHECK_EQ(barrier_transition_info_per_resource[GetPinpongResourceId("primary"_id, 1)].access, D3D12_BARRIER_ACCESS_NO_ACCESS);
  CHECK_EQ(barrier_transition_info_per_resource["imgui_font"_id].access, D3D12_BARRIER_ACCESS_NO_ACCESS);
  CHECK_EQ(barrier_transition_info_per_resource["swapchain"_id].access, D3D12_BARRIER_ACCESS_NO_ACCESS);
  FlipPingPongIndex(current_render_pass_pingpong_flip_list_result_len, pingpong_flip_list, pingpong_current_write_index);
  CHECK_EQ(pingpong_current_write_index["primary"_id], 0);
  // tonemap
  current_render_pass_pingpong_flip_list_result_len = GetPingPongFlippingResourceList(render_pass_info[2], pingpong_flip_list_len, pingpong_flip_list);
  CHECK_EQ(pingpong_flip_list_len, 1);
  CHECK_EQ(pingpong_flip_list[0], "primary"_id);
  barrier_num = ConfigureBarriersTextureTransitions(render_pass_info[2], pingpong_flip_list_len, pingpong_flip_list, pingpong_current_write_index, barrier_transition_info_per_resource, barrier_transition_info_len, barriers);
  CHECK_EQ(barrier_num, 1);
  CHECK_EQ(barriers[0].resource_id, GetPinpongResourceId("primary"_id, 0));
  CHECK_EQ(barriers[0].sync_before, D3D12_BARRIER_SYNC_RENDER_TARGET);
  CHECK_EQ(barriers[0].sync_after, D3D12_BARRIER_SYNC_PIXEL_SHADING);
  CHECK_EQ(barriers[0].access_before, D3D12_BARRIER_ACCESS_RENDER_TARGET);
  CHECK_EQ(barriers[0].access_after, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
  CHECK_EQ(barriers[0].layout_before, D3D12_BARRIER_LAYOUT_RENDER_TARGET);
  CHECK_EQ(barriers[0].layout_after, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(barrier_transition_info_per_resource["gbuffer0"_id].layout, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(barrier_transition_info_per_resource["gbuffer1"_id].layout, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(barrier_transition_info_per_resource["gbuffer2"_id].layout, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(barrier_transition_info_per_resource["gbuffer3"_id].layout, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(barrier_transition_info_per_resource["depth"_id].layout, D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE);
  CHECK_EQ(barrier_transition_info_per_resource[GetPinpongResourceId("primary"_id, 0)].layout, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(barrier_transition_info_per_resource[GetPinpongResourceId("primary"_id, 1)].layout, D3D12_BARRIER_LAYOUT_RENDER_TARGET);
  CHECK_EQ(barrier_transition_info_per_resource["imgui_font"_id].layout, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(barrier_transition_info_per_resource["swapchain"_id].layout, D3D12_BARRIER_LAYOUT_PRESENT);
  CHECK_EQ(barrier_transition_info_per_resource["gbuffer0"_id].sync, D3D12_BARRIER_SYNC_PIXEL_SHADING);
  CHECK_EQ(barrier_transition_info_per_resource["gbuffer1"_id].sync, D3D12_BARRIER_SYNC_PIXEL_SHADING);
  CHECK_EQ(barrier_transition_info_per_resource["gbuffer2"_id].sync, D3D12_BARRIER_SYNC_PIXEL_SHADING);
  CHECK_EQ(barrier_transition_info_per_resource["gbuffer3"_id].sync, D3D12_BARRIER_SYNC_PIXEL_SHADING);
  CHECK_EQ(barrier_transition_info_per_resource["depth"_id].sync, D3D12_BARRIER_SYNC_DEPTH_STENCIL);
  CHECK_EQ(barrier_transition_info_per_resource[GetPinpongResourceId("primary"_id, 0)].sync, D3D12_BARRIER_SYNC_PIXEL_SHADING);
  CHECK_EQ(barrier_transition_info_per_resource[GetPinpongResourceId("primary"_id, 1)].sync, D3D12_BARRIER_SYNC_RENDER_TARGET);
  CHECK_EQ(barrier_transition_info_per_resource["imgui_font"_id].sync, D3D12_BARRIER_SYNC_NONE);
  CHECK_EQ(barrier_transition_info_per_resource["swapchain"_id].sync, D3D12_BARRIER_SYNC_NONE);
  CHECK_EQ(barrier_transition_info_per_resource["gbuffer0"_id].access, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
  CHECK_EQ(barrier_transition_info_per_resource["gbuffer1"_id].access, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
  CHECK_EQ(barrier_transition_info_per_resource["gbuffer2"_id].access, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
  CHECK_EQ(barrier_transition_info_per_resource["gbuffer3"_id].access, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
  CHECK_EQ(barrier_transition_info_per_resource["depth"_id].access, D3D12_BARRIER_ACCESS_DEPTH_STENCIL_WRITE);
  CHECK_EQ(barrier_transition_info_per_resource[GetPinpongResourceId("primary"_id, 0)].access, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
  CHECK_EQ(barrier_transition_info_per_resource[GetPinpongResourceId("primary"_id, 1)].access, D3D12_BARRIER_ACCESS_RENDER_TARGET);
  CHECK_EQ(barrier_transition_info_per_resource["imgui_font"_id].access, D3D12_BARRIER_ACCESS_NO_ACCESS);
  CHECK_EQ(barrier_transition_info_per_resource["swapchain"_id].access, D3D12_BARRIER_ACCESS_NO_ACCESS);
  FlipPingPongIndex(current_render_pass_pingpong_flip_list_result_len, pingpong_flip_list, pingpong_current_write_index);
  CHECK_EQ(pingpong_current_write_index["primary"_id], 1);
  // oetf
  current_render_pass_pingpong_flip_list_result_len = GetPingPongFlippingResourceList(render_pass_info[3], pingpong_flip_list_len, pingpong_flip_list);
  CHECK_EQ(pingpong_flip_list_len, 0);
  barrier_num = ConfigureBarriersTextureTransitions(render_pass_info[3], pingpong_flip_list_len, pingpong_flip_list, pingpong_current_write_index, barrier_transition_info_per_resource, barrier_transition_info_len, barriers);
  CHECK_EQ(barrier_num, 2);
  CHECK_EQ(barriers[0].resource_id, GetPinpongResourceId("primary"_id, 1));
  CHECK_EQ(barriers[0].sync_before, D3D12_BARRIER_SYNC_RENDER_TARGET);
  CHECK_EQ(barriers[0].sync_after, D3D12_BARRIER_SYNC_PIXEL_SHADING);
  CHECK_EQ(barriers[0].access_before, D3D12_BARRIER_ACCESS_RENDER_TARGET);
  CHECK_EQ(barriers[0].access_after, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
  CHECK_EQ(barriers[0].layout_before, D3D12_BARRIER_LAYOUT_RENDER_TARGET);
  CHECK_EQ(barriers[0].layout_after, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(barriers[1].resource_id, "swapchain"_id);
  CHECK_EQ(barriers[1].sync_before, D3D12_BARRIER_SYNC_NONE);
  CHECK_EQ(barriers[1].sync_after, D3D12_BARRIER_SYNC_RENDER_TARGET);
  CHECK_EQ(barriers[1].access_before, D3D12_BARRIER_ACCESS_NO_ACCESS);
  CHECK_EQ(barriers[1].access_after, D3D12_BARRIER_ACCESS_RENDER_TARGET);
  CHECK_EQ(barriers[1].layout_before, D3D12_BARRIER_LAYOUT_PRESENT);
  CHECK_EQ(barriers[1].layout_after, D3D12_BARRIER_LAYOUT_RENDER_TARGET);
  CHECK_EQ(barrier_transition_info_per_resource["gbuffer0"_id].layout, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(barrier_transition_info_per_resource["gbuffer1"_id].layout, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(barrier_transition_info_per_resource["gbuffer2"_id].layout, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(barrier_transition_info_per_resource["gbuffer3"_id].layout, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(barrier_transition_info_per_resource["depth"_id].layout, D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE);
  CHECK_EQ(barrier_transition_info_per_resource[GetPinpongResourceId("primary"_id, 0)].layout, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(barrier_transition_info_per_resource[GetPinpongResourceId("primary"_id, 1)].layout, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(barrier_transition_info_per_resource["imgui_font"_id].layout, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(barrier_transition_info_per_resource["swapchain"_id].layout, D3D12_BARRIER_LAYOUT_RENDER_TARGET);
  CHECK_EQ(barrier_transition_info_per_resource["gbuffer0"_id].sync, D3D12_BARRIER_SYNC_PIXEL_SHADING);
  CHECK_EQ(barrier_transition_info_per_resource["gbuffer1"_id].sync, D3D12_BARRIER_SYNC_PIXEL_SHADING);
  CHECK_EQ(barrier_transition_info_per_resource["gbuffer2"_id].sync, D3D12_BARRIER_SYNC_PIXEL_SHADING);
  CHECK_EQ(barrier_transition_info_per_resource["gbuffer3"_id].sync, D3D12_BARRIER_SYNC_PIXEL_SHADING);
  CHECK_EQ(barrier_transition_info_per_resource["depth"_id].sync, D3D12_BARRIER_SYNC_DEPTH_STENCIL);
  CHECK_EQ(barrier_transition_info_per_resource[GetPinpongResourceId("primary"_id, 0)].sync, D3D12_BARRIER_SYNC_PIXEL_SHADING);
  CHECK_EQ(barrier_transition_info_per_resource[GetPinpongResourceId("primary"_id, 1)].sync, D3D12_BARRIER_SYNC_PIXEL_SHADING);
  CHECK_EQ(barrier_transition_info_per_resource["imgui_font"_id].sync, D3D12_BARRIER_SYNC_NONE);
  CHECK_EQ(barrier_transition_info_per_resource["swapchain"_id].sync, D3D12_BARRIER_SYNC_RENDER_TARGET);
  CHECK_EQ(barrier_transition_info_per_resource["gbuffer0"_id].access, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
  CHECK_EQ(barrier_transition_info_per_resource["gbuffer1"_id].access, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
  CHECK_EQ(barrier_transition_info_per_resource["gbuffer2"_id].access, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
  CHECK_EQ(barrier_transition_info_per_resource["gbuffer3"_id].access, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
  CHECK_EQ(barrier_transition_info_per_resource["depth"_id].access, D3D12_BARRIER_ACCESS_DEPTH_STENCIL_WRITE);
  CHECK_EQ(barrier_transition_info_per_resource[GetPinpongResourceId("primary"_id, 0)].access, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
  CHECK_EQ(barrier_transition_info_per_resource[GetPinpongResourceId("primary"_id, 1)].access, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
  CHECK_EQ(barrier_transition_info_per_resource["imgui_font"_id].access, D3D12_BARRIER_ACCESS_NO_ACCESS);
  CHECK_EQ(barrier_transition_info_per_resource["swapchain"_id].access, D3D12_BARRIER_ACCESS_RENDER_TARGET);
  FlipPingPongIndex(current_render_pass_pingpong_flip_list_result_len, pingpong_flip_list, pingpong_current_write_index);
  CHECK_EQ(pingpong_current_write_index["primary"_id], 1);
  // imgui
  current_render_pass_pingpong_flip_list_result_len = GetPingPongFlippingResourceList(render_pass_info[4], pingpong_flip_list_len, pingpong_flip_list);
  CHECK_EQ(pingpong_flip_list_len, 0);
  barrier_num = ConfigureBarriersTextureTransitions(render_pass_info[4], pingpong_flip_list_len, pingpong_flip_list, pingpong_current_write_index, barrier_transition_info_per_resource, barrier_transition_info_len, barriers);
  CHECK_EQ(barrier_transition_info_per_resource["gbuffer0"_id].layout, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(barrier_transition_info_per_resource["gbuffer1"_id].layout, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(barrier_transition_info_per_resource["gbuffer2"_id].layout, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(barrier_transition_info_per_resource["gbuffer3"_id].layout, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(barrier_transition_info_per_resource["depth"_id].layout, D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE);
  CHECK_EQ(barrier_transition_info_per_resource[GetPinpongResourceId("primary"_id, 0)].layout, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(barrier_transition_info_per_resource[GetPinpongResourceId("primary"_id, 1)].layout, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(barrier_transition_info_per_resource["imgui_font"_id].layout, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(barrier_transition_info_per_resource["swapchain"_id].layout, D3D12_BARRIER_LAYOUT_RENDER_TARGET);
  CHECK_EQ(barrier_transition_info_per_resource["gbuffer0"_id].sync, D3D12_BARRIER_SYNC_PIXEL_SHADING);
  CHECK_EQ(barrier_transition_info_per_resource["gbuffer1"_id].sync, D3D12_BARRIER_SYNC_PIXEL_SHADING);
  CHECK_EQ(barrier_transition_info_per_resource["gbuffer2"_id].sync, D3D12_BARRIER_SYNC_PIXEL_SHADING);
  CHECK_EQ(barrier_transition_info_per_resource["gbuffer3"_id].sync, D3D12_BARRIER_SYNC_PIXEL_SHADING);
  CHECK_EQ(barrier_transition_info_per_resource["depth"_id].sync, D3D12_BARRIER_SYNC_DEPTH_STENCIL);
  CHECK_EQ(barrier_transition_info_per_resource[GetPinpongResourceId("primary"_id, 0)].sync, D3D12_BARRIER_SYNC_PIXEL_SHADING);
  CHECK_EQ(barrier_transition_info_per_resource[GetPinpongResourceId("primary"_id, 1)].sync, D3D12_BARRIER_SYNC_PIXEL_SHADING);
  CHECK_EQ(barrier_transition_info_per_resource["imgui_font"_id].sync, D3D12_BARRIER_SYNC_PIXEL_SHADING);
  CHECK_EQ(barrier_transition_info_per_resource["swapchain"_id].sync, D3D12_BARRIER_SYNC_RENDER_TARGET);
  CHECK_EQ(barrier_transition_info_per_resource["gbuffer0"_id].access, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
  CHECK_EQ(barrier_transition_info_per_resource["gbuffer1"_id].access, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
  CHECK_EQ(barrier_transition_info_per_resource["gbuffer2"_id].access, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
  CHECK_EQ(barrier_transition_info_per_resource["gbuffer3"_id].access, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
  CHECK_EQ(barrier_transition_info_per_resource["depth"_id].access, D3D12_BARRIER_ACCESS_DEPTH_STENCIL_WRITE);
  CHECK_EQ(barrier_transition_info_per_resource[GetPinpongResourceId("primary"_id, 0)].access, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
  CHECK_EQ(barrier_transition_info_per_resource[GetPinpongResourceId("primary"_id, 1)].access, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
  CHECK_EQ(barrier_transition_info_per_resource["imgui_font"_id].access, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
  CHECK_EQ(barrier_transition_info_per_resource["swapchain"_id].access, D3D12_BARRIER_ACCESS_RENDER_TARGET);
  CHECK_EQ(barrier_num, 0);
  FlipPingPongIndex(current_render_pass_pingpong_flip_list_result_len, pingpong_flip_list, pingpong_current_write_index);
  CHECK_EQ(pingpong_current_write_index["primary"_id], 1);
  // present
  current_render_pass_pingpong_flip_list_result_len = GetPingPongFlippingResourceList(render_pass_info[4], pingpong_flip_list_len, pingpong_flip_list);
  CHECK_EQ(pingpong_flip_list_len, 0);
  barrier_num = ConfigureBarriersTextureTransitions(render_pass_info[3], pingpong_flip_list_len, pingpong_flip_list, pingpong_current_write_index, barrier_transition_info_per_resource, barrier_transition_info_len, barriers);
  CHECK_EQ(barrier_num, 1);
  CHECK_EQ(barriers[0].resource_id, "swapchain"_id);
  CHECK_EQ(barriers[0].sync_before, D3D12_BARRIER_SYNC_RENDER_TARGET);
  CHECK_EQ(barriers[0].sync_after, D3D12_BARRIER_SYNC_NONE);
  CHECK_EQ(barriers[0].access_before, D3D12_BARRIER_ACCESS_RENDER_TARGET);
  CHECK_EQ(barriers[0].access_after, D3D12_BARRIER_ACCESS_NO_ACCESS);
  CHECK_EQ(barriers[0].layout_before, D3D12_BARRIER_LAYOUT_RENDER_TARGET);
  CHECK_EQ(barriers[0].layout_after, D3D12_BARRIER_LAYOUT_PRESENT);
  CHECK_EQ(barrier_transition_info_per_resource["gbuffer0"_id].layout, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(barrier_transition_info_per_resource["gbuffer1"_id].layout, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(barrier_transition_info_per_resource["gbuffer2"_id].layout, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(barrier_transition_info_per_resource["gbuffer3"_id].layout, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(barrier_transition_info_per_resource["depth"_id].layout, D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE);
  CHECK_EQ(barrier_transition_info_per_resource[GetPinpongResourceId("primary"_id, 0)].layout, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(barrier_transition_info_per_resource[GetPinpongResourceId("primary"_id, 1)].layout, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(barrier_transition_info_per_resource["imgui_font"_id].layout, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(barrier_transition_info_per_resource["swapchain"_id].layout, D3D12_BARRIER_LAYOUT_PRESENT);
  CHECK_EQ(barrier_transition_info_per_resource["gbuffer0"_id].sync, D3D12_BARRIER_SYNC_PIXEL_SHADING);
  CHECK_EQ(barrier_transition_info_per_resource["gbuffer1"_id].sync, D3D12_BARRIER_SYNC_PIXEL_SHADING);
  CHECK_EQ(barrier_transition_info_per_resource["gbuffer2"_id].sync, D3D12_BARRIER_SYNC_PIXEL_SHADING);
  CHECK_EQ(barrier_transition_info_per_resource["gbuffer3"_id].sync, D3D12_BARRIER_SYNC_PIXEL_SHADING);
  CHECK_EQ(barrier_transition_info_per_resource["depth"_id].sync, D3D12_BARRIER_SYNC_DEPTH_STENCIL);
  CHECK_EQ(barrier_transition_info_per_resource[GetPinpongResourceId("primary"_id, 0)].sync, D3D12_BARRIER_SYNC_PIXEL_SHADING);
  CHECK_EQ(barrier_transition_info_per_resource[GetPinpongResourceId("primary"_id, 1)].sync, D3D12_BARRIER_SYNC_PIXEL_SHADING);
  CHECK_EQ(barrier_transition_info_per_resource["imgui_font"_id].sync, D3D12_BARRIER_SYNC_PIXEL_SHADING);
  CHECK_EQ(barrier_transition_info_per_resource["swapchain"_id].sync, D3D12_BARRIER_SYNC_NONE);
  CHECK_EQ(barrier_transition_info_per_resource["gbuffer0"_id].access, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
  CHECK_EQ(barrier_transition_info_per_resource["gbuffer1"_id].access, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
  CHECK_EQ(barrier_transition_info_per_resource["gbuffer2"_id].access, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
  CHECK_EQ(barrier_transition_info_per_resource["gbuffer3"_id].access, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
  CHECK_EQ(barrier_transition_info_per_resource["depth"_id].access, D3D12_BARRIER_ACCESS_DEPTH_STENCIL_WRITE);
  CHECK_EQ(barrier_transition_info_per_resource[GetPinpongResourceId("primary"_id, 0)].access, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
  CHECK_EQ(barrier_transition_info_per_resource[GetPinpongResourceId("primary"_id, 1)].access, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
  CHECK_EQ(barrier_transition_info_per_resource["imgui_font"_id].access, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
  CHECK_EQ(barrier_transition_info_per_resource["swapchain"_id].access, D3D12_BARRIER_ACCESS_NO_ACCESS);
  FlipPingPongIndex(current_render_pass_pingpong_flip_list_result_len, pingpong_flip_list, pingpong_current_write_index);
  CHECK_EQ(pingpong_current_write_index["primary"_id], 1);
}
