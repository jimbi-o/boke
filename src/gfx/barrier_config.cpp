#include "boke/allocator.h"
#include "boke/container.h"
#include "boke/debug_assert.h"
#include "boke/str_hash.h"
#include "render_pass_info.h"
#include "resources.h"
namespace boke {
const uint32_t kInvalidIndex = ~0U;
struct BarrierTransitionInfoPerResource {
  D3D12_BARRIER_SYNC   sync{D3D12_BARRIER_SYNC_NONE};
  D3D12_BARRIER_ACCESS access{D3D12_BARRIER_ACCESS_NO_ACCESS};
  D3D12_BARRIER_LAYOUT layout{};
};
auto GetResourceIdPingpongRead(const StrHash id, const StrHashMap<uint32_t>& pingpong_current_write_index) {
  if (!pingpong_current_write_index.contains(id)) { return id; }
  return GetPinpongResourceId(id, pingpong_current_write_index[id] == 0 ? 1 : 0);
}
auto GetResourceIdPingpongWrite(const StrHash id, const StrHashMap<uint32_t>& pingpong_current_write_index) {
  if (!pingpong_current_write_index.contains(id)) { return id; }
  return GetPinpongResourceId(id, pingpong_current_write_index[id]);
}
auto GetPingPongFlippingResourceList(const RenderPassInfo& render_pass, const StrHashMap<BarrierTransitionInfoPerResource>& transition_info, const StrHashMap<uint32_t>& pingpong_current_write_index, const uint32_t result_len, StrHash* result) {
  uint32_t result_num = 0;
  for (uint32_t i = 0; i < render_pass.srv_num; i++) {
    const auto srv = render_pass.srv[i];
    if (!pingpong_current_write_index.contains(srv)) { continue; }
    bool found_rtv = false;
    for (uint32_t j = 0; j < render_pass.rtv_num; j++) {
      if (srv != render_pass.rtv[j]) { continue; }
      result[result_num] = srv;
      result_num++;
      DEBUG_ASSERT(result_num <= result_len, DebugAssert{});
      found_rtv = true;
      break;
    }
    if (found_rtv) { continue; }
    // looking at write because it's before flip.
    if (transition_info[GetResourceIdPingpongWrite(srv, pingpong_current_write_index)].layout == D3D12_BARRIER_LAYOUT_RENDER_TARGET) {
      result[result_num] = srv;
      result_num++;
      DEBUG_ASSERT(result_num <= result_len, DebugAssert{});
    }
  }
  return result_num;
}
auto FlipPingPongIndex(const uint32_t list_len, const StrHash* flip_list, StrHashMap<uint32_t>& pingpong_current_write_index) {
  for (uint32_t i = 0; i < list_len; i++) {
    const auto current_index = pingpong_current_write_index[flip_list[i]];
    pingpong_current_write_index[flip_list[i]] = (current_index == 0) ? 1 : 0;
  }
}
auto ConfigureBarriersTextureTransitions(const RenderPassInfo& next_render_pass,
                                         const StrHashMap<uint32_t>& pingpong_current_write_index,
                                         const StrHashMap<BarrierTransitionInfoPerResource>& transition_info,
                                         StrHashMap<BarrierTransitionInfoPerResource>& next_transition_info) {
  for (uint32_t i = 0; i < next_render_pass.srv_num; i++) {
    const auto resource_id = GetResourceIdPingpongRead(next_render_pass.srv[i], pingpong_current_write_index);
    if (transition_info[resource_id].layout != D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE) {
      next_transition_info[resource_id] = {
        .sync   = D3D12_BARRIER_SYNC_PIXEL_SHADING,
        .access = D3D12_BARRIER_ACCESS_SHADER_RESOURCE,
        .layout = D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE,
      };
    }
  }
  for (uint32_t i = 0; i < next_render_pass.rtv_num; i++) {
    const auto resource_id = GetResourceIdPingpongWrite(next_render_pass.rtv[i], pingpong_current_write_index);
    if (transition_info[resource_id].layout != D3D12_BARRIER_LAYOUT_RENDER_TARGET) {
      next_transition_info[resource_id] = {
        .sync   = D3D12_BARRIER_SYNC_RENDER_TARGET,
        .access = D3D12_BARRIER_ACCESS_RENDER_TARGET,
        .layout = D3D12_BARRIER_LAYOUT_RENDER_TARGET,
      };
    }
  }
  if (next_render_pass.dsv != kEmptyStr && transition_info[next_render_pass.dsv].layout != D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE) {
    next_transition_info[next_render_pass.dsv] = {
      .sync   = D3D12_BARRIER_SYNC_DEPTH_STENCIL,
      .access = D3D12_BARRIER_ACCESS_DEPTH_STENCIL_WRITE,
      .layout = D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE,
    };
  }
  if (next_render_pass.present != kEmptyStr && transition_info[next_render_pass.present].layout != D3D12_BARRIER_LAYOUT_PRESENT) {
    next_transition_info[next_render_pass.present] = {
      .sync   = D3D12_BARRIER_SYNC_NONE,
      .access = D3D12_BARRIER_ACCESS_NO_ACCESS,
      .layout = D3D12_BARRIER_LAYOUT_PRESENT,
    };
  }
}
auto UpdateTransitionInfoEntity(StrHashMap<BarrierTransitionInfoPerResource>* transition_info, const StrHash key, const BarrierTransitionInfoPerResource* value) {
  DEBUG_ASSERT(transition_info->contains(key), DebugAssert{});
  (*transition_info)[key] = *value;
}
auto UpdateTransitionInfo(const StrHashMap<BarrierTransitionInfoPerResource>& next_transition_info, StrHashMap<BarrierTransitionInfoPerResource>& transition_info) {
  next_transition_info.iterate<StrHashMap<BarrierTransitionInfoPerResource>>(UpdateTransitionInfoEntity, &transition_info);
}
auto ConfigureInitialLayout(const uint32_t render_pass_num, RenderPassInfo* render_pass, const StrHashMap<uint32_t>& pingpong_current_write_index, StrHashMap<D3D12_BARRIER_LAYOUT>& resource_layout) {
  for (uint32_t i = 0; i < render_pass_num; i++) {
    for (uint32_t j = 0; j < render_pass[i].rtv_num; j++) {
      const auto& rtv = render_pass[i].rtv[j];
      DEBUG_ASSERT(!resource_layout.contains(rtv) || resource_layout[rtv] == D3D12_BARRIER_LAYOUT_RENDER_TARGET, DebugAssert{});
      if (!resource_layout.contains(rtv)) {
        if (pingpong_current_write_index.contains(rtv)) {
          resource_layout[GetPinpongResourceId(rtv, 0)] = D3D12_BARRIER_LAYOUT_RENDER_TARGET;
          resource_layout[GetPinpongResourceId(rtv, 1)] = D3D12_BARRIER_LAYOUT_RENDER_TARGET;
        } else {
          resource_layout[rtv] = D3D12_BARRIER_LAYOUT_RENDER_TARGET;
        }
      }
    }
    if (render_pass[i].dsv != kEmptyStr) {
      DEBUG_ASSERT(!resource_layout.contains(render_pass[i].dsv) || resource_layout[render_pass[i].dsv] == D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE, DebugAssert{});
      DEBUG_ASSERT(!pingpong_current_write_index.contains(render_pass[i].dsv), DebugAssert{});
      if (!resource_layout.contains(render_pass[i].dsv)) {
        resource_layout[render_pass[i].dsv] = D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE;
      }
    }
    if (render_pass[i].present != kEmptyStr) {
      DEBUG_ASSERT(!pingpong_current_write_index.contains(render_pass[i].present), DebugAssert{});
      resource_layout[render_pass[i].present] = D3D12_BARRIER_LAYOUT_PRESENT;
    }
  }
}
auto SetLayout(StrHashMap<BarrierTransitionInfoPerResource>* transition_info, const StrHash resource_id, const D3D12_BARRIER_LAYOUT* layout) {
  (*transition_info)[resource_id].layout = *layout;
}
auto ApplyInitialLayout(const StrHashMap<D3D12_BARRIER_LAYOUT>& resource_layout, StrHashMap<BarrierTransitionInfoPerResource>& transition_info) {
  resource_layout.iterate<StrHashMap<BarrierTransitionInfoPerResource>>(SetLayout, &transition_info);
}
auto ConfigurePingpongBuffers(const uint32_t render_pass_num, RenderPassInfo* render_pass, StrHashMap<uint32_t>& pingpong_current_write_index) {
  for (uint32_t i = 0; i < render_pass_num; i++) {
    for (uint32_t j = 0; j < render_pass[i].srv_num; j++) {
      const auto srv = render_pass[i].srv[j];
      if (pingpong_current_write_index.contains(srv)) { continue; }
      for (uint32_t k = 0; k < render_pass[i].rtv_num; k++) {
        if (srv == render_pass[i].rtv[k]) {
          pingpong_current_write_index[srv] = 0;
          break;
        }
      }
    }
  }
}
} // namespace boke
#include "doctest/doctest.h"
TEST_CASE("barrier config") {
  using namespace boke;
  const uint32_t main_buffer_size_in_bytes = 16 * 1024;
  std::byte main_buffer[main_buffer_size_in_bytes];
  auto allocator_data = GetAllocatorData(main_buffer, main_buffer_size_in_bytes);
  StrHash gbuffers[] = {"gbuffer0"_id, "gbuffer1"_id, "gbuffer2"_id, "gbuffer3"_id,};
  StrHash primary[] = {"primary"_id,};
  StrHash swapchain[] = {"swapchain"_id,};
  StrHash imgui_font[] = {"imgui_font"_id,};
  const uint32_t render_pass_info_len = 6;
  RenderPassInfo render_pass_info[render_pass_info_len] = {
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
  StrHashMap<uint32_t> pingpong_current_write_index(GetAllocatorCallbacks(allocator_data));
  ConfigurePingpongBuffers(render_pass_info_len, render_pass_info, pingpong_current_write_index);
  CHECK_EQ(pingpong_current_write_index.size(), 1);
  CHECK_EQ(pingpong_current_write_index["primary"_id], 0);
  StrHashMap<BarrierTransitionInfoPerResource> transition_info(GetAllocatorCallbacks(allocator_data));
  {
    StrHashMap<D3D12_BARRIER_LAYOUT> initial_layout(GetAllocatorCallbacks(allocator_data));
    ConfigureInitialLayout(render_pass_info_len, render_pass_info, pingpong_current_write_index, initial_layout);
    CHECK_EQ(initial_layout["gbuffer0"_id], D3D12_BARRIER_LAYOUT_RENDER_TARGET);
    CHECK_EQ(initial_layout["gbuffer1"_id], D3D12_BARRIER_LAYOUT_RENDER_TARGET);
    CHECK_EQ(initial_layout["gbuffer2"_id], D3D12_BARRIER_LAYOUT_RENDER_TARGET);
    CHECK_EQ(initial_layout["gbuffer3"_id], D3D12_BARRIER_LAYOUT_RENDER_TARGET);
    CHECK_EQ(initial_layout["depth"_id], D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE);
    CHECK_EQ(initial_layout[GetPinpongResourceId("primary"_id, 0)], D3D12_BARRIER_LAYOUT_RENDER_TARGET);
    CHECK_EQ(initial_layout[GetPinpongResourceId("primary"_id, 1)], D3D12_BARRIER_LAYOUT_RENDER_TARGET);
    CHECK_EQ(initial_layout["swapchain"_id], D3D12_BARRIER_LAYOUT_PRESENT);
    ApplyInitialLayout(initial_layout, transition_info);
    CHECK_EQ(transition_info["gbuffer0"_id].layout, D3D12_BARRIER_LAYOUT_RENDER_TARGET);
    CHECK_EQ(transition_info["gbuffer1"_id].layout, D3D12_BARRIER_LAYOUT_RENDER_TARGET);
    CHECK_EQ(transition_info["gbuffer2"_id].layout, D3D12_BARRIER_LAYOUT_RENDER_TARGET);
    CHECK_EQ(transition_info["gbuffer3"_id].layout, D3D12_BARRIER_LAYOUT_RENDER_TARGET);
    CHECK_EQ(transition_info["depth"_id].layout, D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE);
    CHECK_EQ(transition_info[GetPinpongResourceId("primary"_id, 0)].layout, D3D12_BARRIER_LAYOUT_RENDER_TARGET);
    CHECK_EQ(transition_info[GetPinpongResourceId("primary"_id, 1)].layout, D3D12_BARRIER_LAYOUT_RENDER_TARGET);
    CHECK_EQ(transition_info["swapchain"_id].layout, D3D12_BARRIER_LAYOUT_PRESENT);
  }
  transition_info["imgui_font"_id].layout = D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE;
  const uint32_t pingpong_flip_list_len = 16;
  StrHash pingpong_flip_list[pingpong_flip_list_len]{};
  StrHashMap<BarrierTransitionInfoPerResource> next_transition_info(GetAllocatorCallbacks(allocator_data));
  // gbuffer
  auto current_render_pass_pingpong_flip_list_result_len = GetPingPongFlippingResourceList(render_pass_info[0], transition_info, pingpong_current_write_index, pingpong_flip_list_len, pingpong_flip_list);
  CHECK_EQ(current_render_pass_pingpong_flip_list_result_len, 0);
  FlipPingPongIndex(current_render_pass_pingpong_flip_list_result_len, pingpong_flip_list, pingpong_current_write_index);
  CHECK_EQ(pingpong_current_write_index["primary"_id], 0);
  ConfigureBarriersTextureTransitions(render_pass_info[0], pingpong_current_write_index, transition_info, next_transition_info);
  CHECK_EQ(next_transition_info.size(), 0);
  UpdateTransitionInfo(next_transition_info, transition_info);
  next_transition_info.clear();
  CHECK_EQ(transition_info["gbuffer0"_id].layout, D3D12_BARRIER_LAYOUT_RENDER_TARGET);
  CHECK_EQ(transition_info["gbuffer1"_id].layout, D3D12_BARRIER_LAYOUT_RENDER_TARGET);
  CHECK_EQ(transition_info["gbuffer2"_id].layout, D3D12_BARRIER_LAYOUT_RENDER_TARGET);
  CHECK_EQ(transition_info["gbuffer3"_id].layout, D3D12_BARRIER_LAYOUT_RENDER_TARGET);
  CHECK_EQ(transition_info["depth"_id].layout, D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE);
  CHECK_EQ(transition_info[GetPinpongResourceId("primary"_id, 0)].layout, D3D12_BARRIER_LAYOUT_RENDER_TARGET);
  CHECK_EQ(transition_info[GetPinpongResourceId("primary"_id, 1)].layout, D3D12_BARRIER_LAYOUT_RENDER_TARGET);
  CHECK_EQ(transition_info["imgui_font"_id].layout, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(transition_info["swapchain"_id].layout, D3D12_BARRIER_LAYOUT_PRESENT);
  CHECK_EQ(transition_info["gbuffer0"_id].sync, D3D12_BARRIER_SYNC_NONE);
  CHECK_EQ(transition_info["gbuffer1"_id].sync, D3D12_BARRIER_SYNC_NONE);
  CHECK_EQ(transition_info["gbuffer2"_id].sync, D3D12_BARRIER_SYNC_NONE);
  CHECK_EQ(transition_info["gbuffer3"_id].sync, D3D12_BARRIER_SYNC_NONE);
  CHECK_EQ(transition_info["depth"_id].sync, D3D12_BARRIER_SYNC_NONE);
  CHECK_EQ(transition_info[GetPinpongResourceId("primary"_id, 0)].sync, D3D12_BARRIER_SYNC_NONE);
  CHECK_EQ(transition_info[GetPinpongResourceId("primary"_id, 1)].sync, D3D12_BARRIER_SYNC_NONE);
  CHECK_EQ(transition_info["imgui_font"_id].sync, D3D12_BARRIER_SYNC_NONE);
  CHECK_EQ(transition_info["swapchain"_id].sync, D3D12_BARRIER_SYNC_NONE);
  CHECK_EQ(transition_info["gbuffer0"_id].access, D3D12_BARRIER_ACCESS_NO_ACCESS);
  CHECK_EQ(transition_info["gbuffer1"_id].access, D3D12_BARRIER_ACCESS_NO_ACCESS);
  CHECK_EQ(transition_info["gbuffer2"_id].access, D3D12_BARRIER_ACCESS_NO_ACCESS);
  CHECK_EQ(transition_info["gbuffer3"_id].access, D3D12_BARRIER_ACCESS_NO_ACCESS);
  CHECK_EQ(transition_info["depth"_id].access, D3D12_BARRIER_ACCESS_NO_ACCESS);
  CHECK_EQ(transition_info[GetPinpongResourceId("primary"_id, 0)].access, D3D12_BARRIER_ACCESS_NO_ACCESS);
  CHECK_EQ(transition_info[GetPinpongResourceId("primary"_id, 1)].access, D3D12_BARRIER_ACCESS_NO_ACCESS);
  CHECK_EQ(transition_info["imgui_font"_id].access, D3D12_BARRIER_ACCESS_NO_ACCESS);
  CHECK_EQ(transition_info["swapchain"_id].access, D3D12_BARRIER_ACCESS_NO_ACCESS);
  // lighting
  current_render_pass_pingpong_flip_list_result_len = GetPingPongFlippingResourceList(render_pass_info[1], transition_info, pingpong_current_write_index, pingpong_flip_list_len, pingpong_flip_list);
  FlipPingPongIndex(current_render_pass_pingpong_flip_list_result_len, pingpong_flip_list, pingpong_current_write_index);
  CHECK_EQ(current_render_pass_pingpong_flip_list_result_len, 0);
  ConfigureBarriersTextureTransitions(render_pass_info[1], pingpong_current_write_index, transition_info, next_transition_info);
  CHECK_EQ(next_transition_info.size(), 4);
  CHECK_EQ(next_transition_info["gbuffer0"_id].layout, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(next_transition_info["gbuffer1"_id].layout, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(next_transition_info["gbuffer2"_id].layout, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(next_transition_info["gbuffer3"_id].layout, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(next_transition_info["gbuffer0"_id].sync, D3D12_BARRIER_SYNC_PIXEL_SHADING);
  CHECK_EQ(next_transition_info["gbuffer1"_id].sync, D3D12_BARRIER_SYNC_PIXEL_SHADING);
  CHECK_EQ(next_transition_info["gbuffer2"_id].sync, D3D12_BARRIER_SYNC_PIXEL_SHADING);
  CHECK_EQ(next_transition_info["gbuffer3"_id].sync, D3D12_BARRIER_SYNC_PIXEL_SHADING);
  CHECK_EQ(next_transition_info["gbuffer0"_id].access, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
  CHECK_EQ(next_transition_info["gbuffer1"_id].access, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
  CHECK_EQ(next_transition_info["gbuffer2"_id].access, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
  CHECK_EQ(next_transition_info["gbuffer3"_id].access, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
  UpdateTransitionInfo(next_transition_info, transition_info);
  next_transition_info.clear();
  CHECK_EQ(transition_info["gbuffer0"_id].layout, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(transition_info["gbuffer1"_id].layout, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(transition_info["gbuffer2"_id].layout, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(transition_info["gbuffer3"_id].layout, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(transition_info["depth"_id].layout, D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE);
  CHECK_EQ(transition_info[GetPinpongResourceId("primary"_id, 0)].layout, D3D12_BARRIER_LAYOUT_RENDER_TARGET);
  CHECK_EQ(transition_info[GetPinpongResourceId("primary"_id, 1)].layout, D3D12_BARRIER_LAYOUT_RENDER_TARGET);
  CHECK_EQ(transition_info["imgui_font"_id].layout, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(transition_info["swapchain"_id].layout, D3D12_BARRIER_LAYOUT_PRESENT);
  CHECK_EQ(transition_info["gbuffer0"_id].sync, D3D12_BARRIER_SYNC_PIXEL_SHADING);
  CHECK_EQ(transition_info["gbuffer1"_id].sync, D3D12_BARRIER_SYNC_PIXEL_SHADING);
  CHECK_EQ(transition_info["gbuffer2"_id].sync, D3D12_BARRIER_SYNC_PIXEL_SHADING);
  CHECK_EQ(transition_info["gbuffer3"_id].sync, D3D12_BARRIER_SYNC_PIXEL_SHADING);
  CHECK_EQ(transition_info["depth"_id].sync, D3D12_BARRIER_SYNC_NONE);
  CHECK_EQ(transition_info[GetPinpongResourceId("primary"_id, 0)].sync, D3D12_BARRIER_SYNC_NONE);
  CHECK_EQ(transition_info[GetPinpongResourceId("primary"_id, 1)].sync, D3D12_BARRIER_SYNC_NONE);
  CHECK_EQ(transition_info["imgui_font"_id].sync, D3D12_BARRIER_SYNC_NONE);
  CHECK_EQ(transition_info["swapchain"_id].sync, D3D12_BARRIER_SYNC_NONE);
  CHECK_EQ(transition_info["gbuffer0"_id].access, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
  CHECK_EQ(transition_info["gbuffer1"_id].access, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
  CHECK_EQ(transition_info["gbuffer2"_id].access, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
  CHECK_EQ(transition_info["gbuffer3"_id].access, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
  CHECK_EQ(transition_info["depth"_id].access, D3D12_BARRIER_ACCESS_NO_ACCESS);
  CHECK_EQ(transition_info[GetPinpongResourceId("primary"_id, 0)].access, D3D12_BARRIER_ACCESS_NO_ACCESS);
  CHECK_EQ(transition_info[GetPinpongResourceId("primary"_id, 1)].access, D3D12_BARRIER_ACCESS_NO_ACCESS);
  CHECK_EQ(transition_info["imgui_font"_id].access, D3D12_BARRIER_ACCESS_NO_ACCESS);
  CHECK_EQ(transition_info["swapchain"_id].access, D3D12_BARRIER_ACCESS_NO_ACCESS);
  // tonemap
  current_render_pass_pingpong_flip_list_result_len = GetPingPongFlippingResourceList(render_pass_info[2], transition_info, pingpong_current_write_index, pingpong_flip_list_len, pingpong_flip_list);
  CHECK_EQ(current_render_pass_pingpong_flip_list_result_len, 1);
  CHECK_EQ(pingpong_flip_list[0], "primary"_id);
  FlipPingPongIndex(current_render_pass_pingpong_flip_list_result_len, pingpong_flip_list, pingpong_current_write_index);
  CHECK_EQ(pingpong_current_write_index["primary"_id], 1);
  ConfigureBarriersTextureTransitions(render_pass_info[2], pingpong_current_write_index, transition_info, next_transition_info);
  CHECK_EQ(next_transition_info.size(), 1);
  CHECK_EQ(next_transition_info[GetPinpongResourceId("primary"_id, 0)].layout, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(next_transition_info[GetPinpongResourceId("primary"_id, 0)].sync, D3D12_BARRIER_SYNC_PIXEL_SHADING);
  CHECK_EQ(next_transition_info[GetPinpongResourceId("primary"_id, 0)].access, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
  UpdateTransitionInfo(next_transition_info, transition_info);
  next_transition_info.clear();
  CHECK_EQ(transition_info["gbuffer0"_id].layout, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(transition_info["gbuffer1"_id].layout, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(transition_info["gbuffer2"_id].layout, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(transition_info["gbuffer3"_id].layout, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(transition_info["depth"_id].layout, D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE);
  CHECK_EQ(transition_info[GetPinpongResourceId("primary"_id, 0)].layout, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(transition_info[GetPinpongResourceId("primary"_id, 1)].layout, D3D12_BARRIER_LAYOUT_RENDER_TARGET);
  CHECK_EQ(transition_info["imgui_font"_id].layout, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(transition_info["swapchain"_id].layout, D3D12_BARRIER_LAYOUT_PRESENT);
  CHECK_EQ(transition_info["gbuffer0"_id].sync, D3D12_BARRIER_SYNC_PIXEL_SHADING);
  CHECK_EQ(transition_info["gbuffer1"_id].sync, D3D12_BARRIER_SYNC_PIXEL_SHADING);
  CHECK_EQ(transition_info["gbuffer2"_id].sync, D3D12_BARRIER_SYNC_PIXEL_SHADING);
  CHECK_EQ(transition_info["gbuffer3"_id].sync, D3D12_BARRIER_SYNC_PIXEL_SHADING);
  CHECK_EQ(transition_info["depth"_id].sync, D3D12_BARRIER_SYNC_NONE);
  CHECK_EQ(transition_info[GetPinpongResourceId("primary"_id, 0)].sync, D3D12_BARRIER_SYNC_PIXEL_SHADING);
  CHECK_EQ(transition_info[GetPinpongResourceId("primary"_id, 1)].sync, D3D12_BARRIER_SYNC_NONE);
  CHECK_EQ(transition_info["imgui_font"_id].sync, D3D12_BARRIER_SYNC_NONE);
  CHECK_EQ(transition_info["swapchain"_id].sync, D3D12_BARRIER_SYNC_NONE);
  CHECK_EQ(transition_info["gbuffer0"_id].access, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
  CHECK_EQ(transition_info["gbuffer1"_id].access, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
  CHECK_EQ(transition_info["gbuffer2"_id].access, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
  CHECK_EQ(transition_info["gbuffer3"_id].access, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
  CHECK_EQ(transition_info["depth"_id].access, D3D12_BARRIER_ACCESS_NO_ACCESS);
  CHECK_EQ(transition_info[GetPinpongResourceId("primary"_id, 0)].access, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
  CHECK_EQ(transition_info[GetPinpongResourceId("primary"_id, 1)].access, D3D12_BARRIER_ACCESS_NO_ACCESS);
  CHECK_EQ(transition_info["imgui_font"_id].access, D3D12_BARRIER_ACCESS_NO_ACCESS);
  CHECK_EQ(transition_info["swapchain"_id].access, D3D12_BARRIER_ACCESS_NO_ACCESS);
  // oetf
  current_render_pass_pingpong_flip_list_result_len = GetPingPongFlippingResourceList(render_pass_info[3], transition_info, pingpong_current_write_index, pingpong_flip_list_len, pingpong_flip_list);
  CHECK_EQ(current_render_pass_pingpong_flip_list_result_len, 1);
  CHECK_EQ(pingpong_flip_list[0], "primary"_id);
  FlipPingPongIndex(current_render_pass_pingpong_flip_list_result_len, pingpong_flip_list, pingpong_current_write_index);
  CHECK_EQ(pingpong_current_write_index["primary"_id], 0);
  ConfigureBarriersTextureTransitions(render_pass_info[3], pingpong_current_write_index, transition_info, next_transition_info);
  CHECK_EQ(next_transition_info.size(), 2);
  CHECK_EQ(next_transition_info[GetPinpongResourceId("primary"_id, 1)].layout, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(next_transition_info["swapchain"_id].layout, D3D12_BARRIER_LAYOUT_RENDER_TARGET);
  CHECK_EQ(next_transition_info[GetPinpongResourceId("primary"_id, 1)].sync, D3D12_BARRIER_SYNC_PIXEL_SHADING);
  CHECK_EQ(next_transition_info["swapchain"_id].sync, D3D12_BARRIER_SYNC_RENDER_TARGET);
  CHECK_EQ(next_transition_info[GetPinpongResourceId("primary"_id, 1)].access, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
  CHECK_EQ(next_transition_info["swapchain"_id].access, D3D12_BARRIER_ACCESS_RENDER_TARGET);
  UpdateTransitionInfo(next_transition_info, transition_info);
  next_transition_info.clear();
  CHECK_EQ(transition_info["gbuffer0"_id].layout, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(transition_info["gbuffer1"_id].layout, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(transition_info["gbuffer2"_id].layout, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(transition_info["gbuffer3"_id].layout, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(transition_info["depth"_id].layout, D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE);
  CHECK_EQ(transition_info[GetPinpongResourceId("primary"_id, 0)].layout, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(transition_info[GetPinpongResourceId("primary"_id, 1)].layout, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(transition_info["imgui_font"_id].layout, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(transition_info["swapchain"_id].layout, D3D12_BARRIER_LAYOUT_RENDER_TARGET);
  CHECK_EQ(transition_info["gbuffer0"_id].sync, D3D12_BARRIER_SYNC_PIXEL_SHADING);
  CHECK_EQ(transition_info["gbuffer1"_id].sync, D3D12_BARRIER_SYNC_PIXEL_SHADING);
  CHECK_EQ(transition_info["gbuffer2"_id].sync, D3D12_BARRIER_SYNC_PIXEL_SHADING);
  CHECK_EQ(transition_info["gbuffer3"_id].sync, D3D12_BARRIER_SYNC_PIXEL_SHADING);
  CHECK_EQ(transition_info["depth"_id].sync, D3D12_BARRIER_SYNC_NONE);
  CHECK_EQ(transition_info[GetPinpongResourceId("primary"_id, 0)].sync, D3D12_BARRIER_SYNC_PIXEL_SHADING);
  CHECK_EQ(transition_info[GetPinpongResourceId("primary"_id, 1)].sync, D3D12_BARRIER_SYNC_PIXEL_SHADING);
  CHECK_EQ(transition_info["imgui_font"_id].sync, D3D12_BARRIER_SYNC_NONE);
  CHECK_EQ(transition_info["swapchain"_id].sync, D3D12_BARRIER_SYNC_RENDER_TARGET);
  CHECK_EQ(transition_info["gbuffer0"_id].access, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
  CHECK_EQ(transition_info["gbuffer1"_id].access, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
  CHECK_EQ(transition_info["gbuffer2"_id].access, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
  CHECK_EQ(transition_info["gbuffer3"_id].access, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
  CHECK_EQ(transition_info["depth"_id].access, D3D12_BARRIER_ACCESS_NO_ACCESS);
  CHECK_EQ(transition_info[GetPinpongResourceId("primary"_id, 0)].access, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
  CHECK_EQ(transition_info[GetPinpongResourceId("primary"_id, 1)].access, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
  CHECK_EQ(transition_info["imgui_font"_id].access, D3D12_BARRIER_ACCESS_NO_ACCESS);
  CHECK_EQ(transition_info["swapchain"_id].access, D3D12_BARRIER_ACCESS_RENDER_TARGET);
  // imgui
  current_render_pass_pingpong_flip_list_result_len = GetPingPongFlippingResourceList(render_pass_info[4], transition_info, pingpong_current_write_index, pingpong_flip_list_len, pingpong_flip_list);
  CHECK_EQ(current_render_pass_pingpong_flip_list_result_len, 0);
  FlipPingPongIndex(current_render_pass_pingpong_flip_list_result_len, pingpong_flip_list, pingpong_current_write_index);
  CHECK_EQ(pingpong_current_write_index["primary"_id], 0);
  ConfigureBarriersTextureTransitions(render_pass_info[4], pingpong_current_write_index, transition_info, next_transition_info);
  CHECK_EQ(next_transition_info.size(), 0);
  UpdateTransitionInfo(next_transition_info, transition_info);
  CHECK_EQ(transition_info["gbuffer0"_id].layout, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(transition_info["gbuffer1"_id].layout, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(transition_info["gbuffer2"_id].layout, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(transition_info["gbuffer3"_id].layout, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(transition_info["depth"_id].layout, D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE);
  CHECK_EQ(transition_info[GetPinpongResourceId("primary"_id, 0)].layout, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(transition_info[GetPinpongResourceId("primary"_id, 1)].layout, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(transition_info["imgui_font"_id].layout, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(transition_info["swapchain"_id].layout, D3D12_BARRIER_LAYOUT_RENDER_TARGET);
  CHECK_EQ(transition_info["gbuffer0"_id].sync, D3D12_BARRIER_SYNC_PIXEL_SHADING);
  CHECK_EQ(transition_info["gbuffer1"_id].sync, D3D12_BARRIER_SYNC_PIXEL_SHADING);
  CHECK_EQ(transition_info["gbuffer2"_id].sync, D3D12_BARRIER_SYNC_PIXEL_SHADING);
  CHECK_EQ(transition_info["gbuffer3"_id].sync, D3D12_BARRIER_SYNC_PIXEL_SHADING);
  CHECK_EQ(transition_info["depth"_id].sync, D3D12_BARRIER_SYNC_NONE);
  CHECK_EQ(transition_info[GetPinpongResourceId("primary"_id, 0)].sync, D3D12_BARRIER_SYNC_PIXEL_SHADING);
  CHECK_EQ(transition_info[GetPinpongResourceId("primary"_id, 1)].sync, D3D12_BARRIER_SYNC_PIXEL_SHADING);
  CHECK_EQ(transition_info["imgui_font"_id].sync, D3D12_BARRIER_SYNC_NONE);
  CHECK_EQ(transition_info["swapchain"_id].sync, D3D12_BARRIER_SYNC_RENDER_TARGET);
  CHECK_EQ(transition_info["gbuffer0"_id].access, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
  CHECK_EQ(transition_info["gbuffer1"_id].access, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
  CHECK_EQ(transition_info["gbuffer2"_id].access, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
  CHECK_EQ(transition_info["gbuffer3"_id].access, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
  CHECK_EQ(transition_info["depth"_id].access, D3D12_BARRIER_ACCESS_NO_ACCESS);
  CHECK_EQ(transition_info[GetPinpongResourceId("primary"_id, 0)].access, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
  CHECK_EQ(transition_info[GetPinpongResourceId("primary"_id, 1)].access, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
  CHECK_EQ(transition_info["imgui_font"_id].access, D3D12_BARRIER_ACCESS_NO_ACCESS);
  CHECK_EQ(transition_info["swapchain"_id].access, D3D12_BARRIER_ACCESS_RENDER_TARGET);
  // present
  current_render_pass_pingpong_flip_list_result_len = GetPingPongFlippingResourceList(render_pass_info[5], transition_info, pingpong_current_write_index, pingpong_flip_list_len, pingpong_flip_list);
  CHECK_EQ(current_render_pass_pingpong_flip_list_result_len, 0);
  FlipPingPongIndex(current_render_pass_pingpong_flip_list_result_len, pingpong_flip_list, pingpong_current_write_index);
  CHECK_EQ(pingpong_current_write_index["primary"_id], 0);
  ConfigureBarriersTextureTransitions(render_pass_info[5], pingpong_current_write_index, transition_info, next_transition_info);
  CHECK_EQ(next_transition_info.size(), 1);
  CHECK_EQ(next_transition_info["swapchain"_id].layout, D3D12_BARRIER_LAYOUT_PRESENT);
  CHECK_EQ(next_transition_info["swapchain"_id].sync, D3D12_BARRIER_SYNC_NONE);
  CHECK_EQ(next_transition_info["swapchain"_id].access, D3D12_BARRIER_ACCESS_NO_ACCESS);
  UpdateTransitionInfo(next_transition_info, transition_info);
  next_transition_info.clear();
  CHECK_EQ(transition_info["gbuffer0"_id].layout, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(transition_info["gbuffer1"_id].layout, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(transition_info["gbuffer2"_id].layout, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(transition_info["gbuffer3"_id].layout, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(transition_info["depth"_id].layout, D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE);
  CHECK_EQ(transition_info[GetPinpongResourceId("primary"_id, 0)].layout, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(transition_info[GetPinpongResourceId("primary"_id, 1)].layout, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(transition_info["imgui_font"_id].layout, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(transition_info["swapchain"_id].layout, D3D12_BARRIER_LAYOUT_PRESENT);
  CHECK_EQ(transition_info["gbuffer0"_id].sync, D3D12_BARRIER_SYNC_PIXEL_SHADING);
  CHECK_EQ(transition_info["gbuffer1"_id].sync, D3D12_BARRIER_SYNC_PIXEL_SHADING);
  CHECK_EQ(transition_info["gbuffer2"_id].sync, D3D12_BARRIER_SYNC_PIXEL_SHADING);
  CHECK_EQ(transition_info["gbuffer3"_id].sync, D3D12_BARRIER_SYNC_PIXEL_SHADING);
  CHECK_EQ(transition_info["depth"_id].sync, D3D12_BARRIER_SYNC_NONE);
  CHECK_EQ(transition_info[GetPinpongResourceId("primary"_id, 0)].sync, D3D12_BARRIER_SYNC_PIXEL_SHADING);
  CHECK_EQ(transition_info[GetPinpongResourceId("primary"_id, 1)].sync, D3D12_BARRIER_SYNC_PIXEL_SHADING);
  CHECK_EQ(transition_info["imgui_font"_id].sync, D3D12_BARRIER_SYNC_NONE);
  CHECK_EQ(transition_info["swapchain"_id].sync, D3D12_BARRIER_SYNC_NONE);
  CHECK_EQ(transition_info["gbuffer0"_id].access, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
  CHECK_EQ(transition_info["gbuffer1"_id].access, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
  CHECK_EQ(transition_info["gbuffer2"_id].access, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
  CHECK_EQ(transition_info["gbuffer3"_id].access, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
  CHECK_EQ(transition_info["depth"_id].access, D3D12_BARRIER_ACCESS_NO_ACCESS);
  CHECK_EQ(transition_info[GetPinpongResourceId("primary"_id, 0)].access, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
  CHECK_EQ(transition_info[GetPinpongResourceId("primary"_id, 1)].access, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
  CHECK_EQ(transition_info["imgui_font"_id].access, D3D12_BARRIER_ACCESS_NO_ACCESS);
  CHECK_EQ(transition_info["swapchain"_id].access, D3D12_BARRIER_ACCESS_NO_ACCESS);
}
