#include "boke/allocator.h"
#include "boke/container.h"
#include "boke/debug_assert.h"
#include "boke/str_hash.h"
namespace boke {
const uint32_t kInvalidIndex = ~0U;
struct RenderPassInfo {
  StrHash queue{kEmptyStr};
  StrHash* srv{};
  uint32_t srv_num{};
  StrHash* rtv{};
  uint32_t rtv_num{};
  StrHash  dsv{kEmptyStr};
  StrHash  present{kEmptyStr};
};
struct BarrierTransitionInfoPerResource {
  D3D12_BARRIER_SYNC   sync{D3D12_BARRIER_SYNC_NONE};
  D3D12_BARRIER_ACCESS access{D3D12_BARRIER_ACCESS_NO_ACCESS};
  D3D12_BARRIER_LAYOUT layout{};
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
  StrHashMap<BarrierTransitionInfoPerResource> transition_info(GetAllocatorCallbacks(allocator_data));
  transition_info["gbuffer0"_id].layout = D3D12_BARRIER_LAYOUT_RENDER_TARGET;
  transition_info["gbuffer1"_id].layout = D3D12_BARRIER_LAYOUT_RENDER_TARGET;
  transition_info["gbuffer2"_id].layout = D3D12_BARRIER_LAYOUT_RENDER_TARGET;
  transition_info["gbuffer3"_id].layout = D3D12_BARRIER_LAYOUT_RENDER_TARGET;
  transition_info["depth"_id].layout = D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE;
  transition_info[GetPinpongResourceId("primary"_id, 0)].layout = D3D12_BARRIER_LAYOUT_RENDER_TARGET;
  transition_info[GetPinpongResourceId("primary"_id, 1)].layout = D3D12_BARRIER_LAYOUT_RENDER_TARGET;
  transition_info["imgui_font"_id].layout = D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE;
  transition_info["swapchain"_id].layout = D3D12_BARRIER_LAYOUT_PRESENT;
  StrHashMap<uint32_t> pingpong_current_write_index(GetAllocatorCallbacks(allocator_data));
  pingpong_current_write_index["primary"_id] = 0;
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
