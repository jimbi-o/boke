#include "boke/allocator.h"
#include "boke/container.h"
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
struct BarrierTransitionInfo {
  StrHash resource_name{};
  uint32_t resource_index{};
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
auto ConfigureBarriersTextureTransitions(const RenderPassInfo& next_render_pass, StrHashMap<D3D12_BARRIER_LAYOUT>& current_layout, StrHashMap<uint32_t>& pingpong_current_write_index, BarrierTransitionInfo* barrier_transition_info) {
  uint32_t barrier_num = 0;
  return barrier_num;
};
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
  StrHashMap<D3D12_BARRIER_LAYOUT> current_layout(GetAllocatorCallbacks(allocator_data));
  current_layout["gbuffer0"_id] = D3D12_BARRIER_LAYOUT_RENDER_TARGET;
  current_layout["gbuffer1"_id] = D3D12_BARRIER_LAYOUT_RENDER_TARGET;
  current_layout["gbuffer2"_id] = D3D12_BARRIER_LAYOUT_RENDER_TARGET;
  current_layout["gbuffer3"_id] = D3D12_BARRIER_LAYOUT_RENDER_TARGET;
  current_layout["depth"_id] = D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE;
  current_layout[GetPinpongResourceId("primary"_id, 0)] = D3D12_BARRIER_LAYOUT_RENDER_TARGET;
  current_layout[GetPinpongResourceId("primary"_id, 1)] = D3D12_BARRIER_LAYOUT_RENDER_TARGET;
  current_layout["imgui_font"_id] = D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE;
  current_layout["swapchain"_id] = D3D12_BARRIER_LAYOUT_PRESENT;
  StrHashMap<uint32_t> pingpong_current_write_index(GetAllocatorCallbacks(allocator_data));
  pingpong_current_write_index["primary"_id] = 0;
  BarrierTransitionInfo barriers[16]{};
  // gbuffer
  auto barrier_num = ConfigureBarriersTextureTransitions(render_pass_info[0], current_layout, pingpong_current_write_index, barriers);
  CHECK_EQ(barrier_num, 0);
  CHECK_EQ(current_layout["gbuffer0"_id], D3D12_BARRIER_LAYOUT_RENDER_TARGET);
  CHECK_EQ(current_layout["gbuffer1"_id], D3D12_BARRIER_LAYOUT_RENDER_TARGET);
  CHECK_EQ(current_layout["gbuffer2"_id], D3D12_BARRIER_LAYOUT_RENDER_TARGET);
  CHECK_EQ(current_layout["gbuffer3"_id], D3D12_BARRIER_LAYOUT_RENDER_TARGET);
  CHECK_EQ(current_layout["depth"_id], D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE);
  CHECK_EQ(current_layout[GetPinpongResourceId("primary"_id, 0)], D3D12_BARRIER_LAYOUT_RENDER_TARGET);
  CHECK_EQ(current_layout[GetPinpongResourceId("primary"_id, 1)], D3D12_BARRIER_LAYOUT_RENDER_TARGET);
  CHECK_EQ(current_layout["imgui_font"_id], D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(current_layout["swapchain"_id], D3D12_BARRIER_LAYOUT_PRESENT);
  CHECK_EQ(pingpong_current_write_index["primary"_id], 0);
  // lighting
  barrier_num = ConfigureBarriersTextureTransitions(render_pass_info[1], current_layout, pingpong_current_write_index, barriers);
  CHECK_EQ(barrier_num, 4);
  CHECK_EQ(barriers[0].resource_name, "gbuffer0"_id);
  CHECK_EQ(barriers[0].resource_index, 0);
  CHECK_EQ(barriers[0].sync_before, D3D12_BARRIER_SYNC_RENDER_TARGET);
  CHECK_EQ(barriers[0].sync_after, D3D12_BARRIER_SYNC_PIXEL_SHADING);
  CHECK_EQ(barriers[0].access_before, D3D12_BARRIER_ACCESS_RENDER_TARGET);
  CHECK_EQ(barriers[0].access_after, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
  CHECK_EQ(barriers[0].layout_before, D3D12_BARRIER_LAYOUT_RENDER_TARGET);
  CHECK_EQ(barriers[0].layout_after, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(barriers[1].resource_name, "gbuffer1"_id);
  CHECK_EQ(barriers[1].resource_index, 0);
  CHECK_EQ(barriers[1].sync_before, D3D12_BARRIER_SYNC_RENDER_TARGET);
  CHECK_EQ(barriers[1].sync_after, D3D12_BARRIER_SYNC_PIXEL_SHADING);
  CHECK_EQ(barriers[1].access_before, D3D12_BARRIER_ACCESS_RENDER_TARGET);
  CHECK_EQ(barriers[1].access_after, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
  CHECK_EQ(barriers[1].layout_before, D3D12_BARRIER_LAYOUT_RENDER_TARGET);
  CHECK_EQ(barriers[1].layout_after, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(barriers[2].resource_name, "gbuffer2"_id);
  CHECK_EQ(barriers[2].resource_index, 0);
  CHECK_EQ(barriers[2].sync_before, D3D12_BARRIER_SYNC_RENDER_TARGET);
  CHECK_EQ(barriers[2].sync_after, D3D12_BARRIER_SYNC_PIXEL_SHADING);
  CHECK_EQ(barriers[2].access_before, D3D12_BARRIER_ACCESS_RENDER_TARGET);
  CHECK_EQ(barriers[2].access_after, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
  CHECK_EQ(barriers[2].layout_before, D3D12_BARRIER_LAYOUT_RENDER_TARGET);
  CHECK_EQ(barriers[2].layout_after, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(barriers[3].resource_name, "gbuffer3"_id);
  CHECK_EQ(barriers[3].resource_index, 0);
  CHECK_EQ(barriers[3].sync_before, D3D12_BARRIER_SYNC_RENDER_TARGET);
  CHECK_EQ(barriers[3].sync_after, D3D12_BARRIER_SYNC_PIXEL_SHADING);
  CHECK_EQ(barriers[3].access_before, D3D12_BARRIER_ACCESS_RENDER_TARGET);
  CHECK_EQ(barriers[3].access_after, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
  CHECK_EQ(barriers[3].layout_before, D3D12_BARRIER_LAYOUT_RENDER_TARGET);
  CHECK_EQ(barriers[3].layout_after, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(current_layout["gbuffer0"_id], D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(current_layout["gbuffer1"_id], D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(current_layout["gbuffer2"_id], D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(current_layout["gbuffer3"_id], D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(current_layout["depth"_id], D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE);
  CHECK_EQ(current_layout[GetPinpongResourceId("primary"_id, 0)], D3D12_BARRIER_LAYOUT_RENDER_TARGET);
  CHECK_EQ(current_layout[GetPinpongResourceId("primary"_id, 1)], D3D12_BARRIER_LAYOUT_RENDER_TARGET);
  CHECK_EQ(current_layout["imgui_font"_id], D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(current_layout["swapchain"_id], D3D12_BARRIER_LAYOUT_PRESENT);
  CHECK_EQ(pingpong_current_write_index["primary"_id], 0);
  // tonemap
  barrier_num = ConfigureBarriersTextureTransitions(render_pass_info[2], current_layout, pingpong_current_write_index, barriers);
  CHECK_EQ(barrier_num, 1);
  CHECK_EQ(barriers[0].resource_name, "primary"_id);
  CHECK_EQ(barriers[0].resource_index, 0);
  CHECK_EQ(barriers[0].sync_before, D3D12_BARRIER_SYNC_RENDER_TARGET);
  CHECK_EQ(barriers[0].sync_after, D3D12_BARRIER_SYNC_PIXEL_SHADING);
  CHECK_EQ(barriers[0].access_before, D3D12_BARRIER_ACCESS_RENDER_TARGET);
  CHECK_EQ(barriers[0].access_after, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
  CHECK_EQ(barriers[0].layout_before, D3D12_BARRIER_LAYOUT_RENDER_TARGET);
  CHECK_EQ(barriers[0].layout_after, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(current_layout["gbuffer0"_id], D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(current_layout["gbuffer1"_id], D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(current_layout["gbuffer2"_id], D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(current_layout["gbuffer3"_id], D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(current_layout["depth"_id], D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE);
  CHECK_EQ(current_layout[GetPinpongResourceId("primary"_id, 0)], D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(current_layout[GetPinpongResourceId("primary"_id, 1)], D3D12_BARRIER_LAYOUT_RENDER_TARGET);
  CHECK_EQ(current_layout["imgui_font"_id], D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(current_layout["swapchain"_id], D3D12_BARRIER_LAYOUT_PRESENT);
  CHECK_EQ(pingpong_current_write_index["primary"_id], 1);
  // oetf
  barrier_num = ConfigureBarriersTextureTransitions(render_pass_info[3], current_layout, pingpong_current_write_index, barriers);
  CHECK_EQ(barrier_num, 2);
  CHECK_EQ(barriers[0].resource_name, "primary"_id);
  CHECK_EQ(barriers[0].resource_index, 1);
  CHECK_EQ(barriers[0].sync_before, D3D12_BARRIER_SYNC_RENDER_TARGET);
  CHECK_EQ(barriers[0].sync_after, D3D12_BARRIER_SYNC_PIXEL_SHADING);
  CHECK_EQ(barriers[0].access_before, D3D12_BARRIER_ACCESS_RENDER_TARGET);
  CHECK_EQ(barriers[0].access_after, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
  CHECK_EQ(barriers[0].layout_before, D3D12_BARRIER_LAYOUT_RENDER_TARGET);
  CHECK_EQ(barriers[0].layout_after, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(barriers[1].resource_name, "swapchain"_id);
  CHECK_EQ(barriers[1].resource_index, 0);
  CHECK_EQ(barriers[1].sync_before, D3D12_BARRIER_SYNC_NONE);
  CHECK_EQ(barriers[1].sync_after, D3D12_BARRIER_SYNC_RENDER_TARGET);
  CHECK_EQ(barriers[1].access_before, D3D12_BARRIER_ACCESS_NO_ACCESS);
  CHECK_EQ(barriers[1].access_after, D3D12_BARRIER_ACCESS_RENDER_TARGET);
  CHECK_EQ(barriers[1].layout_before, D3D12_BARRIER_LAYOUT_PRESENT);
  CHECK_EQ(barriers[1].layout_after, D3D12_BARRIER_LAYOUT_RENDER_TARGET);
  CHECK_EQ(current_layout["gbuffer0"_id], D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(current_layout["gbuffer1"_id], D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(current_layout["gbuffer2"_id], D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(current_layout["gbuffer3"_id], D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(current_layout["depth"_id], D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE);
  CHECK_EQ(current_layout[GetPinpongResourceId("primary"_id, 0)], D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(current_layout[GetPinpongResourceId("primary"_id, 1)], D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(current_layout["imgui_font"_id], D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(current_layout["swapchain"_id], D3D12_BARRIER_LAYOUT_RENDER_TARGET);
  CHECK_EQ(pingpong_current_write_index["primary"_id], 1);
  // imgui
  barrier_num = ConfigureBarriersTextureTransitions(render_pass_info[4], current_layout, pingpong_current_write_index, barriers);
  CHECK_EQ(barrier_num, 0);
  // present
  barrier_num = ConfigureBarriersTextureTransitions(render_pass_info[3], current_layout, pingpong_current_write_index, barriers);
  CHECK_EQ(barrier_num, 1);
  CHECK_EQ(barriers[0].resource_name, "swapchain"_id);
  CHECK_EQ(barriers[0].resource_index, 0);
  CHECK_EQ(barriers[0].sync_before, D3D12_BARRIER_SYNC_RENDER_TARGET);
  CHECK_EQ(barriers[0].sync_after, D3D12_BARRIER_SYNC_NONE);
  CHECK_EQ(barriers[0].access_before, D3D12_BARRIER_ACCESS_RENDER_TARGET);
  CHECK_EQ(barriers[0].access_after, D3D12_BARRIER_ACCESS_NO_ACCESS);
  CHECK_EQ(barriers[0].layout_before, D3D12_BARRIER_LAYOUT_RENDER_TARGET);
  CHECK_EQ(barriers[0].layout_after, D3D12_BARRIER_LAYOUT_PRESENT);
  CHECK_EQ(current_layout["gbuffer0"_id], D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(current_layout["gbuffer1"_id], D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(current_layout["gbuffer2"_id], D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(current_layout["gbuffer3"_id], D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(current_layout["depth"_id], D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE);
  CHECK_EQ(current_layout[GetPinpongResourceId("primary"_id, 0)], D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(current_layout[GetPinpongResourceId("primary"_id, 1)], D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(current_layout["imgui_font"_id], D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(current_layout["swapchain"_id], D3D12_BARRIER_LAYOUT_PRESENT);
  CHECK_EQ(pingpong_current_write_index["primary"_id], 1);
}
