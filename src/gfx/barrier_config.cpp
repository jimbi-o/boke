#include "boke/allocator.h"
#include "boke/container.h"
#include "boke/debug_assert.h"
#include "boke/str_hash.h"
#include "boke/util.h"
#include "barrier_config.h"
#include "json.h"
#include "render_pass_info.h"
#include "resources.h"
namespace {
using namespace boke;
const uint32_t kInvalidIndex = ~0U;
auto FlipPingPongIndexImpl(const uint32_t list_len, const StrHash* flip_list, StrHashMap<uint32_t>& pingpong_current_write_index) {
  for (uint32_t i = 0; i < list_len; i++) {
    const auto current_index = pingpong_current_write_index[flip_list[i]];
    pingpong_current_write_index[flip_list[i]] = (current_index == 0) ? 1 : 0;
  }
}
auto IsSame(const BarrierTransitionInfoPerResource& a, const BarrierTransitionInfoPerResource& b) {
  if (a.sync != b.sync) { return false; }
  if (a.access != b.access) { return false; }
  if (a.layout != b.layout) { return false; }
  return true;
}
auto GetTransitionInfoIndex(const StrHash resource_id, const BarrierTransitionInfo& transition_info) {
  return transition_info.transition_info_index[resource_id];
}
auto GetCurrentTransitionInfo(const StrHash resource_id, const uint32_t resource_local_index, const BarrierTransitionInfo& transition_info) {
  const auto& transition_info_index = GetTransitionInfoIndex(resource_id, transition_info);
  return transition_info.transition_info[transition_info_index.index + resource_local_index];
}
auto GetNextTransitionInfo(const StrHash resource_id, const uint32_t resource_local_index, const BarrierTransitionInfo& transition_info) {
  const auto& transition_info_index = GetTransitionInfoIndex(resource_id, transition_info);
  return transition_info.transition_info[transition_info_index.index + transition_info_index.physical_resource_num + resource_local_index];
}
auto GetCurrentTransitionInfo(const uint32_t resource_local_index, const BarrierTransitionInfoIndex& transition_info_index, const BarrierTransitionInfo& transition_info) {
  return transition_info.transition_info[transition_info_index.index + resource_local_index];
}
auto GetNextTransitionInfo(const uint32_t resource_local_index, const BarrierTransitionInfoIndex& transition_info_index, const BarrierTransitionInfo& transition_info) {
  return transition_info.transition_info[transition_info_index.index + transition_info_index.physical_resource_num + resource_local_index];
}
auto UpdateNextTransitionInfo(const StrHash resource_id, const uint32_t resource_local_index, const BarrierTransitionInfoPerResource& info, BarrierTransitionInfo& transition_info) {
  const auto& transition_info_index = GetTransitionInfoIndex(resource_id, transition_info);
  transition_info.transition_info[transition_info_index.index + transition_info_index.physical_resource_num + resource_local_index] = info;
}
auto ConfigureBarriersTextureTransitions(const RenderPassInfo& next_render_pass, const StrHashMap<uint32_t>& pingpong_current_write_index, BarrierTransitionInfo& transition_info) {
  // srv
  BarrierTransitionInfoPerResource info{
    .sync = D3D12_BARRIER_SYNC_PIXEL_SHADING,
    .access = D3D12_BARRIER_ACCESS_SHADER_RESOURCE,
    .layout = D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE
  };
  for (uint32_t i = 0; i < next_render_pass.srv_num; i++) {
    UpdateNextTransitionInfo(next_render_pass.srv[i], GetPingpongIndexRead(pingpong_current_write_index, next_render_pass.srv[i]), info, transition_info);
  }
  // rtv
  info = BarrierTransitionInfoPerResource{
    .sync = D3D12_BARRIER_SYNC_RENDER_TARGET,
    .access = D3D12_BARRIER_ACCESS_RENDER_TARGET,
    .layout = D3D12_BARRIER_LAYOUT_RENDER_TARGET,
  };
  for (uint32_t i = 0; i < next_render_pass.rtv_num; i++) {
    UpdateNextTransitionInfo(next_render_pass.rtv[i], GetPingpongIndexWrite(pingpong_current_write_index, next_render_pass.rtv[i]), info, transition_info);
  }
  // dsv
  if (next_render_pass.dsv != kEmptyStr) {
    info = BarrierTransitionInfoPerResource{
      .sync = D3D12_BARRIER_SYNC_DEPTH_STENCIL,
      .access = D3D12_BARRIER_ACCESS_DEPTH_STENCIL_WRITE,
      .layout = D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE,
    };
    UpdateNextTransitionInfo(next_render_pass.dsv, GetPingpongIndexWrite(pingpong_current_write_index, next_render_pass.dsv), info, transition_info);
  }
  // present
  if (next_render_pass.present != kEmptyStr) {
    info = BarrierTransitionInfoPerResource{
      .sync = D3D12_BARRIER_SYNC_NONE,
      .access = D3D12_BARRIER_ACCESS_NO_ACCESS,
      .layout = D3D12_BARRIER_LAYOUT_PRESENT,
    };
    UpdateNextTransitionInfo(next_render_pass.present, GetPingpongIndexWrite(pingpong_current_write_index, next_render_pass.present), info, transition_info);
  }
}
auto GetPingPongFlippingResourceList(const RenderPassInfo& render_pass, const BarrierTransitionInfo& transition_info, const StrHashMap<uint32_t>& pingpong_current_write_index, const uint32_t result_len, StrHash* result) {
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
    if (GetCurrentTransitionInfo(srv, GetPingpongIndexWrite(pingpong_current_write_index, srv), transition_info).layout == D3D12_BARRIER_LAYOUT_RENDER_TARGET) {
      result[result_num] = srv;
      result_num++;
      DEBUG_ASSERT(result_num <= result_len, DebugAssert{});
    }
  }
  return result_num;
}
auto IsReadOnlyAccess(const D3D12_BARRIER_ACCESS access) {
  if (access & D3D12_BARRIER_ACCESS_RENDER_TARGET) { return false; }
  if (access & D3D12_BARRIER_ACCESS_DEPTH_STENCIL_WRITE) { return false; }
  if (access & D3D12_BARRIER_ACCESS_COPY_DEST) { return false; }
  if (access & D3D12_BARRIER_ACCESS_RESOLVE_DEST) { return false; }
  if (access & D3D12_BARRIER_ACCESS_RAYTRACING_ACCELERATION_STRUCTURE_WRITE) { return false; }
  return true;
}
struct ProcessBarriersImplAsset {
  D3D12_TEXTURE_BARRIER* barriers{};
  const ResourceSet& resource_set;
  const BarrierTransitionInfo& transition_info;
  uint32_t barrier_index{};
};
void ProcessBarriersImpl(ProcessBarriersImplAsset* asset, const StrHash resource_id, const BarrierTransitionInfoIndex* transition_info_index) {
  for (uint32_t i = 0; i < transition_info_index->physical_resource_num; i++) {
    const auto& transition_info = GetCurrentTransitionInfo(i, *transition_info_index, asset->transition_info);
    const auto& next_transition_info = GetNextTransitionInfo(i, *transition_info_index, asset->transition_info);
    if (next_transition_info.layout == transition_info.layout) { continue; }
    // spdlog::info("{:x} {:x}->{:x}", resource_id, GetUint32(transition_info.layout), GetUint32(next_transition_info.layout));
    asset->barriers[asset->barrier_index] = D3D12_TEXTURE_BARRIER{
      .SyncBefore = transition_info.sync,
      .SyncAfter  = next_transition_info.sync,
      .AccessBefore = transition_info.access,
      .AccessAfter  = next_transition_info.access,
      .LayoutBefore = transition_info.layout,
      .LayoutAfter  = next_transition_info.layout,
      .pResource = GetResource(asset->resource_set, resource_id, i),
      .Subresources = {
        .IndexOrFirstMipLevel = 0xffffffff,
        .NumMipLevels = 0,
        .FirstArraySlice = 0,
        .NumArraySlices = 0,
        .FirstPlane = 0,
        .NumPlanes = 0,
      },
      .Flags = D3D12_TEXTURE_BARRIER_FLAG_NONE,
    };
    asset->barrier_index++;
  }
}
void InitTransitionInfoImpl(BarrierTransitionInfo* transition_info, const StrHash resource_id, const ResourceInfo* resource_info) {
  D3D12_BARRIER_LAYOUT layout{};
  switch (resource_info->creation_type) {
    case ResourceCreationType::kRtv: {
      layout = D3D12_BARRIER_LAYOUT_RENDER_TARGET;
      break;
    }
    case ResourceCreationType::kDsv: {
      layout = D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE;
      break;
    }
    case ResourceCreationType::kNone: {
      return;
    }
  }
  AddTransitionInfo(resource_id, resource_info->pingpong ? 2 : 1, layout, *transition_info);
}
void UpdateTransitionInfoImpl(BarrierTransitionInfo* transition_info, const StrHash, BarrierTransitionInfoIndex* transition_info_index) {
  for (uint32_t i = 0; i < transition_info_index->physical_resource_num; i++) {
    transition_info->transition_info[transition_info_index->index + i] = GetNextTransitionInfo(i, *transition_info_index, *transition_info);
  }
}
} // namespace
namespace boke {
void InitTransitionInfo(const StrHashMap<ResourceInfo>& resource_info, BarrierTransitionInfo& transition_info) {
  resource_info.iterate<BarrierTransitionInfo>(InitTransitionInfoImpl, &transition_info);
}
void AddTransitionInfo(const StrHash resource_id, const uint32_t transition_num, const D3D12_BARRIER_LAYOUT layout, BarrierTransitionInfo& transition_info) {
  const uint32_t current_size = transition_info.transition_info.size();
  BarrierTransitionInfoIndex transition_info_index{
    .physical_resource_num = transition_num,
    .index = current_size,
  };
  BarrierTransitionInfoPerResource info{
    .sync = D3D12_BARRIER_SYNC_NONE,
    .access = D3D12_BARRIER_ACCESS_NO_ACCESS,
    .layout = layout,
  };
  while (transition_info.transition_info.size() < transition_info_index.index + transition_num * 2) {
    transition_info.transition_info.push_back(info);
  }
  transition_info.transition_info_index[resource_id] = transition_info_index;
}
void UpdateTransitionInfo(BarrierTransitionInfo& transition_info) {
  transition_info.transition_info_index.iterate<BarrierTransitionInfo>(UpdateTransitionInfoImpl, &transition_info);
}
void FlipPingPongIndex(const RenderPassInfo& render_pass_info, const BarrierTransitionInfo& transition_info, StrHashMap<uint32_t>& pingpong_current_write_index) {
  const uint32_t pingpong_flip_list_len = 8;
  StrHash pingpong_flip_list[pingpong_flip_list_len]{};
  const auto current_render_pass_pingpong_flip_list_result_len = GetPingPongFlippingResourceList(render_pass_info, transition_info, pingpong_current_write_index, pingpong_flip_list_len, pingpong_flip_list);
  DEBUG_ASSERT(current_render_pass_pingpong_flip_list_result_len <= pingpong_flip_list_len, DebugAssert{});
  FlipPingPongIndexImpl(current_render_pass_pingpong_flip_list_result_len, pingpong_flip_list, pingpong_current_write_index);
}
void ConfigureRenderPassBarriersTextureTransitions(const RenderPassInfo& render_pass_info, const StrHashMap<uint32_t>& pingpong_current_write_index, BarrierTransitionInfo& transition_info) {
  ConfigureBarriersTextureTransitions(render_pass_info, pingpong_current_write_index, transition_info);
}
void ProcessBarriers(const BarrierTransitionInfo& transition_info, const ResourceSet& resource_set, D3d12CommandList* command_list) {
  const uint32_t barrier_num = 16;
  D3D12_TEXTURE_BARRIER barriers[barrier_num]{};
  ProcessBarriersImplAsset asset{
    .barriers = barriers,
    .resource_set = resource_set,
    .transition_info = transition_info,
    .barrier_index = 0,
  };
  transition_info.transition_info_index.iterate<ProcessBarriersImplAsset>(ProcessBarriersImpl, &asset);
  if (asset.barrier_index == 0) { return; }
  DEBUG_ASSERT(asset.barrier_index <= barrier_num, DebugAssert{});
  D3D12_BARRIER_GROUP barrier_group {
    .Type = D3D12_BARRIER_TYPE_TEXTURE,
    .NumBarriers = asset.barrier_index,
    .pTextureBarriers = barriers,
  };
  command_list->Barrier(1, &barrier_group);
}
void ResetBarrierSyncAccessStatus(BarrierTransitionInfo& transition_info) {
  for (auto& info : transition_info.transition_info) {
    info.sync = D3D12_BARRIER_SYNC_NONE;
    info.access = D3D12_BARRIER_ACCESS_NO_ACCESS;
  }
}
}
#include "doctest/doctest.h"
TEST_CASE("barrier config") {
  using namespace boke;
  const uint32_t main_buffer_size_in_bytes = 16 * 1024;
  std::byte main_buffer[main_buffer_size_in_bytes];
  InitAllocator(main_buffer, main_buffer_size_in_bytes);
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
  StrHashMap<uint32_t> pingpong_current_write_index;
  pingpong_current_write_index["primary"_id] = 0;
  StrHashMap<BarrierTransitionInfoIndex> transition_info_index;
  ResizableArray<BarrierTransitionInfoPerResource> transition_info_internal;
  BarrierTransitionInfo transition_info{
    .transition_info_index = transition_info_index,
    .transition_info = transition_info_internal,
  };
  StrHashMap<ResourceInfo> resource_info;
  ParseResourceInfo(GetJson("tests/resources.json"), resource_info);
  InitTransitionInfo(resource_info, transition_info);
  CHECK_EQ(transition_info.transition_info_index.size(), 6);
  CHECK_UNARY(transition_info.transition_info_index.contains("gbuffer0"_id));
  CHECK_UNARY(transition_info.transition_info_index.contains("gbuffer1"_id));
  CHECK_UNARY(transition_info.transition_info_index.contains("gbuffer2"_id));
  CHECK_UNARY(transition_info.transition_info_index.contains("gbuffer3"_id));
  CHECK_UNARY(transition_info.transition_info_index.contains("depth"_id));
  CHECK_UNARY(transition_info.transition_info_index.contains("primary"_id));
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer0"_id].index].layout, D3D12_BARRIER_LAYOUT_RENDER_TARGET);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer1"_id].index].layout, D3D12_BARRIER_LAYOUT_RENDER_TARGET);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer2"_id].index].layout, D3D12_BARRIER_LAYOUT_RENDER_TARGET);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer3"_id].index].layout, D3D12_BARRIER_LAYOUT_RENDER_TARGET);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["depth"_id].index].layout, D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["primary"_id].index].layout, D3D12_BARRIER_LAYOUT_RENDER_TARGET);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["primary"_id].index + 1].layout, D3D12_BARRIER_LAYOUT_RENDER_TARGET);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer0"_id].index].sync, D3D12_BARRIER_SYNC_NONE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer1"_id].index].sync, D3D12_BARRIER_SYNC_NONE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer2"_id].index].sync, D3D12_BARRIER_SYNC_NONE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer3"_id].index].sync, D3D12_BARRIER_SYNC_NONE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["depth"_id].index].sync, D3D12_BARRIER_SYNC_NONE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["primary"_id].index].sync, D3D12_BARRIER_SYNC_NONE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["primary"_id].index + 1].sync, D3D12_BARRIER_SYNC_NONE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer0"_id].index].access, D3D12_BARRIER_ACCESS_NO_ACCESS);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer1"_id].index].access, D3D12_BARRIER_ACCESS_NO_ACCESS);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer2"_id].index].access, D3D12_BARRIER_ACCESS_NO_ACCESS);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer3"_id].index].access, D3D12_BARRIER_ACCESS_NO_ACCESS);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["depth"_id].index].access, D3D12_BARRIER_ACCESS_NO_ACCESS);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["primary"_id].index].access, D3D12_BARRIER_ACCESS_NO_ACCESS);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["primary"_id].index + 1].access, D3D12_BARRIER_ACCESS_NO_ACCESS);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer0"_id].index + 1].layout, D3D12_BARRIER_LAYOUT_RENDER_TARGET);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer1"_id].index + 1].layout, D3D12_BARRIER_LAYOUT_RENDER_TARGET);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer2"_id].index + 1].layout, D3D12_BARRIER_LAYOUT_RENDER_TARGET);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer3"_id].index + 1].layout, D3D12_BARRIER_LAYOUT_RENDER_TARGET);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["depth"_id].index + 1].layout, D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["primary"_id].index + 2].layout, D3D12_BARRIER_LAYOUT_RENDER_TARGET);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["primary"_id].index + 3].layout, D3D12_BARRIER_LAYOUT_RENDER_TARGET);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer0"_id].index + 1].sync, D3D12_BARRIER_SYNC_NONE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer1"_id].index + 1].sync, D3D12_BARRIER_SYNC_NONE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer2"_id].index + 1].sync, D3D12_BARRIER_SYNC_NONE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer3"_id].index + 1].sync, D3D12_BARRIER_SYNC_NONE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["depth"_id].index + 1].sync, D3D12_BARRIER_SYNC_NONE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["primary"_id].index + 2].sync, D3D12_BARRIER_SYNC_NONE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["primary"_id].index + 3].sync, D3D12_BARRIER_SYNC_NONE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer0"_id].index + 1].access, D3D12_BARRIER_ACCESS_NO_ACCESS);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer1"_id].index + 1].access, D3D12_BARRIER_ACCESS_NO_ACCESS);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer2"_id].index + 1].access, D3D12_BARRIER_ACCESS_NO_ACCESS);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer3"_id].index + 1].access, D3D12_BARRIER_ACCESS_NO_ACCESS);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["depth"_id].index + 1].access, D3D12_BARRIER_ACCESS_NO_ACCESS);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["primary"_id].index + 2].access, D3D12_BARRIER_ACCESS_NO_ACCESS);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["primary"_id].index + 3].access, D3D12_BARRIER_ACCESS_NO_ACCESS);
  AddTransitionInfo("swapchain"_id, 1, D3D12_BARRIER_LAYOUT_PRESENT, transition_info);
  CHECK_EQ(transition_info.transition_info_index.size(), 7);
  CHECK_UNARY(transition_info.transition_info_index.contains("gbuffer0"_id));
  CHECK_UNARY(transition_info.transition_info_index.contains("gbuffer1"_id));
  CHECK_UNARY(transition_info.transition_info_index.contains("gbuffer2"_id));
  CHECK_UNARY(transition_info.transition_info_index.contains("gbuffer3"_id));
  CHECK_UNARY(transition_info.transition_info_index.contains("depth"_id));
  CHECK_UNARY(transition_info.transition_info_index.contains("primary"_id));
  CHECK_UNARY(transition_info.transition_info_index.contains("swapchain"_id));
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer0"_id].index].layout, D3D12_BARRIER_LAYOUT_RENDER_TARGET);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer1"_id].index].layout, D3D12_BARRIER_LAYOUT_RENDER_TARGET);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer2"_id].index].layout, D3D12_BARRIER_LAYOUT_RENDER_TARGET);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer3"_id].index].layout, D3D12_BARRIER_LAYOUT_RENDER_TARGET);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["depth"_id].index].layout, D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["primary"_id].index].layout, D3D12_BARRIER_LAYOUT_RENDER_TARGET);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["primary"_id].index + 1].layout, D3D12_BARRIER_LAYOUT_RENDER_TARGET);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["swapchain"_id].index].layout, D3D12_BARRIER_LAYOUT_PRESENT);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer0"_id].index].sync, D3D12_BARRIER_SYNC_NONE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer1"_id].index].sync, D3D12_BARRIER_SYNC_NONE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer2"_id].index].sync, D3D12_BARRIER_SYNC_NONE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer3"_id].index].sync, D3D12_BARRIER_SYNC_NONE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["depth"_id].index].sync, D3D12_BARRIER_SYNC_NONE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["primary"_id].index].sync, D3D12_BARRIER_SYNC_NONE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["primary"_id].index + 1].sync, D3D12_BARRIER_SYNC_NONE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["swapchain"_id].index].sync, D3D12_BARRIER_SYNC_NONE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer0"_id].index].access, D3D12_BARRIER_ACCESS_NO_ACCESS);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer1"_id].index].access, D3D12_BARRIER_ACCESS_NO_ACCESS);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer2"_id].index].access, D3D12_BARRIER_ACCESS_NO_ACCESS);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer3"_id].index].access, D3D12_BARRIER_ACCESS_NO_ACCESS);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["depth"_id].index].access, D3D12_BARRIER_ACCESS_NO_ACCESS);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["primary"_id].index].access, D3D12_BARRIER_ACCESS_NO_ACCESS);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["primary"_id].index + 1].access, D3D12_BARRIER_ACCESS_NO_ACCESS);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["swapchain"_id].index].access, D3D12_BARRIER_ACCESS_NO_ACCESS);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer0"_id].index + 1].layout, D3D12_BARRIER_LAYOUT_RENDER_TARGET);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer1"_id].index + 1].layout, D3D12_BARRIER_LAYOUT_RENDER_TARGET);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer2"_id].index + 1].layout, D3D12_BARRIER_LAYOUT_RENDER_TARGET);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer3"_id].index + 1].layout, D3D12_BARRIER_LAYOUT_RENDER_TARGET);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["depth"_id].index + 1].layout, D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["primary"_id].index + 2].layout, D3D12_BARRIER_LAYOUT_RENDER_TARGET);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["primary"_id].index + 3].layout, D3D12_BARRIER_LAYOUT_RENDER_TARGET);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["swapchain"_id].index + 1].layout, D3D12_BARRIER_LAYOUT_PRESENT);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer0"_id].index + 1].sync, D3D12_BARRIER_SYNC_NONE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer1"_id].index + 1].sync, D3D12_BARRIER_SYNC_NONE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer2"_id].index + 1].sync, D3D12_BARRIER_SYNC_NONE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer3"_id].index + 1].sync, D3D12_BARRIER_SYNC_NONE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["depth"_id].index + 1].sync, D3D12_BARRIER_SYNC_NONE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["primary"_id].index + 2].sync, D3D12_BARRIER_SYNC_NONE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["primary"_id].index + 3].sync, D3D12_BARRIER_SYNC_NONE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["swapchain"_id].index + 1].sync, D3D12_BARRIER_SYNC_NONE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer0"_id].index + 1].access, D3D12_BARRIER_ACCESS_NO_ACCESS);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer1"_id].index + 1].access, D3D12_BARRIER_ACCESS_NO_ACCESS);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer2"_id].index + 1].access, D3D12_BARRIER_ACCESS_NO_ACCESS);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer3"_id].index + 1].access, D3D12_BARRIER_ACCESS_NO_ACCESS);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["depth"_id].index + 1].access, D3D12_BARRIER_ACCESS_NO_ACCESS);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["primary"_id].index + 2].access, D3D12_BARRIER_ACCESS_NO_ACCESS);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["primary"_id].index + 3].access, D3D12_BARRIER_ACCESS_NO_ACCESS);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["swapchain"_id].index + 1].access, D3D12_BARRIER_ACCESS_NO_ACCESS);
  const uint32_t pingpong_flip_list_len = 16;
  StrHash pingpong_flip_list[pingpong_flip_list_len]{};
  // gbuffer
  auto current_render_pass_pingpong_flip_list_result_len = GetPingPongFlippingResourceList(render_pass_info[0], transition_info, pingpong_current_write_index, pingpong_flip_list_len, pingpong_flip_list);
  CHECK_EQ(current_render_pass_pingpong_flip_list_result_len, 0);
  FlipPingPongIndexImpl(current_render_pass_pingpong_flip_list_result_len, pingpong_flip_list, pingpong_current_write_index);
  CHECK_EQ(pingpong_current_write_index["primary"_id], 0);
  ConfigureBarriersTextureTransitions(render_pass_info[0], pingpong_current_write_index, transition_info);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer0"_id].index + 1].layout, D3D12_BARRIER_LAYOUT_RENDER_TARGET);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer1"_id].index + 1].layout, D3D12_BARRIER_LAYOUT_RENDER_TARGET);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer2"_id].index + 1].layout, D3D12_BARRIER_LAYOUT_RENDER_TARGET);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer3"_id].index + 1].layout, D3D12_BARRIER_LAYOUT_RENDER_TARGET);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["depth"_id].index + 1].layout, D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["primary"_id].index + 2].layout, D3D12_BARRIER_LAYOUT_RENDER_TARGET);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["primary"_id].index + 3].layout, D3D12_BARRIER_LAYOUT_RENDER_TARGET);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["swapchain"_id].index + 1].layout, D3D12_BARRIER_LAYOUT_PRESENT);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer0"_id].index + 1].sync, D3D12_BARRIER_SYNC_RENDER_TARGET);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer1"_id].index + 1].sync, D3D12_BARRIER_SYNC_RENDER_TARGET);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer2"_id].index + 1].sync, D3D12_BARRIER_SYNC_RENDER_TARGET);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer3"_id].index + 1].sync, D3D12_BARRIER_SYNC_RENDER_TARGET);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["depth"_id].index + 1].sync, D3D12_BARRIER_SYNC_DEPTH_STENCIL);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["primary"_id].index + 2].sync, D3D12_BARRIER_SYNC_NONE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["primary"_id].index + 3].sync, D3D12_BARRIER_SYNC_NONE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["swapchain"_id].index + 1].sync, D3D12_BARRIER_SYNC_NONE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer0"_id].index + 1].access, D3D12_BARRIER_ACCESS_RENDER_TARGET);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer1"_id].index + 1].access, D3D12_BARRIER_ACCESS_RENDER_TARGET);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer2"_id].index + 1].access, D3D12_BARRIER_ACCESS_RENDER_TARGET);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer3"_id].index + 1].access, D3D12_BARRIER_ACCESS_RENDER_TARGET);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["depth"_id].index + 1].access, D3D12_BARRIER_ACCESS_DEPTH_STENCIL_WRITE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["primary"_id].index + 2].access, D3D12_BARRIER_ACCESS_NO_ACCESS);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["primary"_id].index + 3].access, D3D12_BARRIER_ACCESS_NO_ACCESS);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["swapchain"_id].index + 1].access, D3D12_BARRIER_ACCESS_NO_ACCESS);
  UpdateTransitionInfo(transition_info);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer0"_id].index].layout, D3D12_BARRIER_LAYOUT_RENDER_TARGET);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer1"_id].index].layout, D3D12_BARRIER_LAYOUT_RENDER_TARGET);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer2"_id].index].layout, D3D12_BARRIER_LAYOUT_RENDER_TARGET);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer3"_id].index].layout, D3D12_BARRIER_LAYOUT_RENDER_TARGET);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["depth"_id].index].layout, D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["primary"_id].index].layout, D3D12_BARRIER_LAYOUT_RENDER_TARGET);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["primary"_id].index + 1].layout, D3D12_BARRIER_LAYOUT_RENDER_TARGET);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["swapchain"_id].index].layout, D3D12_BARRIER_LAYOUT_PRESENT);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer0"_id].index].sync, D3D12_BARRIER_SYNC_RENDER_TARGET);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer1"_id].index].sync, D3D12_BARRIER_SYNC_RENDER_TARGET);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer2"_id].index].sync, D3D12_BARRIER_SYNC_RENDER_TARGET);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer3"_id].index].sync, D3D12_BARRIER_SYNC_RENDER_TARGET);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["depth"_id].index].sync, D3D12_BARRIER_SYNC_DEPTH_STENCIL);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["primary"_id].index].sync, D3D12_BARRIER_SYNC_NONE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["primary"_id].index + 1].sync, D3D12_BARRIER_SYNC_NONE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["swapchain"_id].index].sync, D3D12_BARRIER_SYNC_NONE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer0"_id].index].access, D3D12_BARRIER_ACCESS_RENDER_TARGET);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer1"_id].index].access, D3D12_BARRIER_ACCESS_RENDER_TARGET);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer2"_id].index].access, D3D12_BARRIER_ACCESS_RENDER_TARGET);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer3"_id].index].access, D3D12_BARRIER_ACCESS_RENDER_TARGET);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["depth"_id].index].access, D3D12_BARRIER_ACCESS_DEPTH_STENCIL_WRITE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["primary"_id].index].access, D3D12_BARRIER_ACCESS_NO_ACCESS);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["primary"_id].index + 1].access, D3D12_BARRIER_ACCESS_NO_ACCESS);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["swapchain"_id].index].access, D3D12_BARRIER_ACCESS_NO_ACCESS);
  // lighting
  current_render_pass_pingpong_flip_list_result_len = GetPingPongFlippingResourceList(render_pass_info[1], transition_info, pingpong_current_write_index, pingpong_flip_list_len, pingpong_flip_list);
  FlipPingPongIndexImpl(current_render_pass_pingpong_flip_list_result_len, pingpong_flip_list, pingpong_current_write_index);
  CHECK_EQ(current_render_pass_pingpong_flip_list_result_len, 0);
  ConfigureBarriersTextureTransitions(render_pass_info[1], pingpong_current_write_index, transition_info);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer0"_id].index + 1].layout, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer1"_id].index + 1].layout, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer2"_id].index + 1].layout, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer3"_id].index + 1].layout, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["depth"_id].index + 1].layout, D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["primary"_id].index + 2].layout, D3D12_BARRIER_LAYOUT_RENDER_TARGET);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["primary"_id].index + 3].layout, D3D12_BARRIER_LAYOUT_RENDER_TARGET);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["swapchain"_id].index + 1].layout, D3D12_BARRIER_LAYOUT_PRESENT);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer0"_id].index + 1].sync, D3D12_BARRIER_SYNC_PIXEL_SHADING);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer1"_id].index + 1].sync, D3D12_BARRIER_SYNC_PIXEL_SHADING);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer2"_id].index + 1].sync, D3D12_BARRIER_SYNC_PIXEL_SHADING);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer3"_id].index + 1].sync, D3D12_BARRIER_SYNC_PIXEL_SHADING);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["depth"_id].index + 1].sync, D3D12_BARRIER_SYNC_DEPTH_STENCIL);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["primary"_id].index + 2].sync, D3D12_BARRIER_SYNC_RENDER_TARGET);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["primary"_id].index + 3].sync, D3D12_BARRIER_SYNC_NONE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["swapchain"_id].index + 1].sync, D3D12_BARRIER_SYNC_NONE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer0"_id].index + 1].access, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer1"_id].index + 1].access, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer2"_id].index + 1].access, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer3"_id].index + 1].access, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["depth"_id].index + 1].access, D3D12_BARRIER_ACCESS_DEPTH_STENCIL_WRITE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["primary"_id].index + 2].access, D3D12_BARRIER_ACCESS_RENDER_TARGET);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["primary"_id].index + 3].access, D3D12_BARRIER_ACCESS_NO_ACCESS);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["swapchain"_id].index + 1].access, D3D12_BARRIER_ACCESS_NO_ACCESS);
  UpdateTransitionInfo(transition_info);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer0"_id].index].layout, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer1"_id].index].layout, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer2"_id].index].layout, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer3"_id].index].layout, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["depth"_id].index].layout, D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["primary"_id].index].layout, D3D12_BARRIER_LAYOUT_RENDER_TARGET);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["primary"_id].index + 1].layout, D3D12_BARRIER_LAYOUT_RENDER_TARGET);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["swapchain"_id].index].layout, D3D12_BARRIER_LAYOUT_PRESENT);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer0"_id].index].sync, D3D12_BARRIER_SYNC_PIXEL_SHADING);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer1"_id].index].sync, D3D12_BARRIER_SYNC_PIXEL_SHADING);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer2"_id].index].sync, D3D12_BARRIER_SYNC_PIXEL_SHADING);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer3"_id].index].sync, D3D12_BARRIER_SYNC_PIXEL_SHADING);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["depth"_id].index].sync, D3D12_BARRIER_SYNC_DEPTH_STENCIL);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["primary"_id].index].sync, D3D12_BARRIER_SYNC_RENDER_TARGET);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["primary"_id].index + 1].sync, D3D12_BARRIER_SYNC_NONE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["swapchain"_id].index].sync, D3D12_BARRIER_SYNC_NONE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer0"_id].index].access, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer1"_id].index].access, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer2"_id].index].access, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer3"_id].index].access, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["depth"_id].index].access, D3D12_BARRIER_ACCESS_DEPTH_STENCIL_WRITE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["primary"_id].index].access, D3D12_BARRIER_ACCESS_RENDER_TARGET);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["primary"_id].index + 1].access, D3D12_BARRIER_ACCESS_NO_ACCESS);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["swapchain"_id].index].access, D3D12_BARRIER_ACCESS_NO_ACCESS);
  // tonemap
  current_render_pass_pingpong_flip_list_result_len = GetPingPongFlippingResourceList(render_pass_info[2], transition_info, pingpong_current_write_index, pingpong_flip_list_len, pingpong_flip_list);
  CHECK_EQ(current_render_pass_pingpong_flip_list_result_len, 1);
  CHECK_EQ(pingpong_flip_list[0], "primary"_id);
  FlipPingPongIndexImpl(current_render_pass_pingpong_flip_list_result_len, pingpong_flip_list, pingpong_current_write_index);
  CHECK_EQ(pingpong_current_write_index["primary"_id], 1);
  ConfigureBarriersTextureTransitions(render_pass_info[2], pingpong_current_write_index, transition_info);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer0"_id].index + 1].layout, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer1"_id].index + 1].layout, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer2"_id].index + 1].layout, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer3"_id].index + 1].layout, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["depth"_id].index + 1].layout, D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["primary"_id].index + 2].layout, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["primary"_id].index + 3].layout, D3D12_BARRIER_LAYOUT_RENDER_TARGET);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["swapchain"_id].index + 1].layout, D3D12_BARRIER_LAYOUT_PRESENT);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer0"_id].index + 1].sync, D3D12_BARRIER_SYNC_PIXEL_SHADING);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer1"_id].index + 1].sync, D3D12_BARRIER_SYNC_PIXEL_SHADING);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer2"_id].index + 1].sync, D3D12_BARRIER_SYNC_PIXEL_SHADING);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer3"_id].index + 1].sync, D3D12_BARRIER_SYNC_PIXEL_SHADING);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["depth"_id].index + 1].sync, D3D12_BARRIER_SYNC_DEPTH_STENCIL);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["primary"_id].index + 2].sync, D3D12_BARRIER_SYNC_PIXEL_SHADING);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["primary"_id].index + 3].sync, D3D12_BARRIER_SYNC_RENDER_TARGET);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["swapchain"_id].index + 1].sync, D3D12_BARRIER_SYNC_NONE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer0"_id].index + 1].access, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer1"_id].index + 1].access, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer2"_id].index + 1].access, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer3"_id].index + 1].access, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["depth"_id].index + 1].access, D3D12_BARRIER_ACCESS_DEPTH_STENCIL_WRITE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["primary"_id].index + 2].access, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["primary"_id].index + 3].access, D3D12_BARRIER_ACCESS_RENDER_TARGET);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["swapchain"_id].index + 1].access, D3D12_BARRIER_ACCESS_NO_ACCESS);
  UpdateTransitionInfo(transition_info);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer0"_id].index].layout, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer1"_id].index].layout, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer2"_id].index].layout, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer3"_id].index].layout, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["depth"_id].index].layout, D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["primary"_id].index].layout, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["primary"_id].index + 1].layout, D3D12_BARRIER_LAYOUT_RENDER_TARGET);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["swapchain"_id].index].layout, D3D12_BARRIER_LAYOUT_PRESENT);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer0"_id].index].sync, D3D12_BARRIER_SYNC_PIXEL_SHADING);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer1"_id].index].sync, D3D12_BARRIER_SYNC_PIXEL_SHADING);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer2"_id].index].sync, D3D12_BARRIER_SYNC_PIXEL_SHADING);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer3"_id].index].sync, D3D12_BARRIER_SYNC_PIXEL_SHADING);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["depth"_id].index].sync, D3D12_BARRIER_SYNC_DEPTH_STENCIL);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["primary"_id].index].sync, D3D12_BARRIER_SYNC_PIXEL_SHADING);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["primary"_id].index + 1].sync, D3D12_BARRIER_SYNC_RENDER_TARGET);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["swapchain"_id].index].sync, D3D12_BARRIER_SYNC_NONE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer0"_id].index].access, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer1"_id].index].access, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer2"_id].index].access, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer3"_id].index].access, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["depth"_id].index].access, D3D12_BARRIER_ACCESS_DEPTH_STENCIL_WRITE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["primary"_id].index].access, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["primary"_id].index + 1].access, D3D12_BARRIER_ACCESS_RENDER_TARGET);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["swapchain"_id].index].access, D3D12_BARRIER_ACCESS_NO_ACCESS);
  // oetf
  current_render_pass_pingpong_flip_list_result_len = GetPingPongFlippingResourceList(render_pass_info[3], transition_info, pingpong_current_write_index, pingpong_flip_list_len, pingpong_flip_list);
  CHECK_EQ(current_render_pass_pingpong_flip_list_result_len, 1);
  CHECK_EQ(pingpong_flip_list[0], "primary"_id);
  FlipPingPongIndexImpl(current_render_pass_pingpong_flip_list_result_len, pingpong_flip_list, pingpong_current_write_index);
  CHECK_EQ(pingpong_current_write_index["primary"_id], 0);
  ConfigureBarriersTextureTransitions(render_pass_info[3], pingpong_current_write_index, transition_info);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer0"_id].index + 1].layout, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer1"_id].index + 1].layout, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer2"_id].index + 1].layout, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer3"_id].index + 1].layout, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["depth"_id].index + 1].layout, D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["primary"_id].index + 2].layout, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["primary"_id].index + 3].layout, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["swapchain"_id].index + 1].layout, D3D12_BARRIER_LAYOUT_RENDER_TARGET);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer0"_id].index + 1].sync, D3D12_BARRIER_SYNC_PIXEL_SHADING);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer1"_id].index + 1].sync, D3D12_BARRIER_SYNC_PIXEL_SHADING);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer2"_id].index + 1].sync, D3D12_BARRIER_SYNC_PIXEL_SHADING);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer3"_id].index + 1].sync, D3D12_BARRIER_SYNC_PIXEL_SHADING);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["depth"_id].index + 1].sync, D3D12_BARRIER_SYNC_DEPTH_STENCIL);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["primary"_id].index + 2].sync, D3D12_BARRIER_SYNC_PIXEL_SHADING);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["primary"_id].index + 3].sync, D3D12_BARRIER_SYNC_PIXEL_SHADING);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["swapchain"_id].index + 1].sync, D3D12_BARRIER_SYNC_RENDER_TARGET);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer0"_id].index + 1].access, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer1"_id].index + 1].access, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer2"_id].index + 1].access, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer3"_id].index + 1].access, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["depth"_id].index + 1].access, D3D12_BARRIER_ACCESS_DEPTH_STENCIL_WRITE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["primary"_id].index + 2].access, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["primary"_id].index + 3].access, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["swapchain"_id].index + 1].access, D3D12_BARRIER_ACCESS_RENDER_TARGET);
  UpdateTransitionInfo(transition_info);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer0"_id].index].layout, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer1"_id].index].layout, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer2"_id].index].layout, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer3"_id].index].layout, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["depth"_id].index].layout, D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["primary"_id].index].layout, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["primary"_id].index + 1].layout, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["swapchain"_id].index].layout, D3D12_BARRIER_LAYOUT_RENDER_TARGET);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer0"_id].index].sync, D3D12_BARRIER_SYNC_PIXEL_SHADING);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer1"_id].index].sync, D3D12_BARRIER_SYNC_PIXEL_SHADING);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer2"_id].index].sync, D3D12_BARRIER_SYNC_PIXEL_SHADING);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer3"_id].index].sync, D3D12_BARRIER_SYNC_PIXEL_SHADING);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["depth"_id].index].sync, D3D12_BARRIER_SYNC_DEPTH_STENCIL);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["primary"_id].index].sync, D3D12_BARRIER_SYNC_PIXEL_SHADING);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["primary"_id].index + 1].sync, D3D12_BARRIER_SYNC_PIXEL_SHADING);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["swapchain"_id].index].sync, D3D12_BARRIER_SYNC_RENDER_TARGET);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer0"_id].index].access, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer1"_id].index].access, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer2"_id].index].access, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer3"_id].index].access, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["depth"_id].index].access, D3D12_BARRIER_ACCESS_DEPTH_STENCIL_WRITE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["primary"_id].index].access, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["primary"_id].index + 1].access, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["swapchain"_id].index].access, D3D12_BARRIER_ACCESS_RENDER_TARGET);
  // imgui
  current_render_pass_pingpong_flip_list_result_len = GetPingPongFlippingResourceList(render_pass_info[4], transition_info, pingpong_current_write_index, pingpong_flip_list_len, pingpong_flip_list);
  CHECK_EQ(current_render_pass_pingpong_flip_list_result_len, 0);
  FlipPingPongIndexImpl(current_render_pass_pingpong_flip_list_result_len, pingpong_flip_list, pingpong_current_write_index);
  CHECK_EQ(pingpong_current_write_index["primary"_id], 0);
  ConfigureBarriersTextureTransitions(render_pass_info[4], pingpong_current_write_index, transition_info);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer0"_id].index + 1].layout, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer1"_id].index + 1].layout, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer2"_id].index + 1].layout, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer3"_id].index + 1].layout, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["depth"_id].index + 1].layout, D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["primary"_id].index + 2].layout, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["primary"_id].index + 3].layout, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["swapchain"_id].index + 1].layout, D3D12_BARRIER_LAYOUT_RENDER_TARGET);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer0"_id].index + 1].sync, D3D12_BARRIER_SYNC_PIXEL_SHADING);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer1"_id].index + 1].sync, D3D12_BARRIER_SYNC_PIXEL_SHADING);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer2"_id].index + 1].sync, D3D12_BARRIER_SYNC_PIXEL_SHADING);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer3"_id].index + 1].sync, D3D12_BARRIER_SYNC_PIXEL_SHADING);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["depth"_id].index + 1].sync, D3D12_BARRIER_SYNC_DEPTH_STENCIL);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["primary"_id].index + 2].sync, D3D12_BARRIER_SYNC_PIXEL_SHADING);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["primary"_id].index + 3].sync, D3D12_BARRIER_SYNC_PIXEL_SHADING);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["swapchain"_id].index + 1].sync, D3D12_BARRIER_SYNC_RENDER_TARGET);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer0"_id].index + 1].access, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer1"_id].index + 1].access, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer2"_id].index + 1].access, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer3"_id].index + 1].access, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["depth"_id].index + 1].access, D3D12_BARRIER_ACCESS_DEPTH_STENCIL_WRITE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["primary"_id].index + 2].access, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["primary"_id].index + 3].access, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["swapchain"_id].index + 1].access, D3D12_BARRIER_ACCESS_RENDER_TARGET);
  UpdateTransitionInfo(transition_info);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer0"_id].index].layout, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer1"_id].index].layout, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer2"_id].index].layout, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer3"_id].index].layout, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["depth"_id].index].layout, D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["primary"_id].index].layout, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["primary"_id].index + 1].layout, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["swapchain"_id].index].layout, D3D12_BARRIER_LAYOUT_RENDER_TARGET);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer0"_id].index].sync, D3D12_BARRIER_SYNC_PIXEL_SHADING);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer1"_id].index].sync, D3D12_BARRIER_SYNC_PIXEL_SHADING);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer2"_id].index].sync, D3D12_BARRIER_SYNC_PIXEL_SHADING);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer3"_id].index].sync, D3D12_BARRIER_SYNC_PIXEL_SHADING);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["depth"_id].index].sync, D3D12_BARRIER_SYNC_DEPTH_STENCIL);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["primary"_id].index].sync, D3D12_BARRIER_SYNC_PIXEL_SHADING);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["primary"_id].index + 1].sync, D3D12_BARRIER_SYNC_PIXEL_SHADING);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["swapchain"_id].index].sync, D3D12_BARRIER_SYNC_RENDER_TARGET);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer0"_id].index].access, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer1"_id].index].access, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer2"_id].index].access, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer3"_id].index].access, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["depth"_id].index].access, D3D12_BARRIER_ACCESS_DEPTH_STENCIL_WRITE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["primary"_id].index].access, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["primary"_id].index + 1].access, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["swapchain"_id].index].access, D3D12_BARRIER_ACCESS_RENDER_TARGET);
  // present
  current_render_pass_pingpong_flip_list_result_len = GetPingPongFlippingResourceList(render_pass_info[5], transition_info, pingpong_current_write_index, pingpong_flip_list_len, pingpong_flip_list);
  CHECK_EQ(current_render_pass_pingpong_flip_list_result_len, 0);
  FlipPingPongIndexImpl(current_render_pass_pingpong_flip_list_result_len, pingpong_flip_list, pingpong_current_write_index);
  CHECK_EQ(pingpong_current_write_index["primary"_id], 0);
  ConfigureBarriersTextureTransitions(render_pass_info[5], pingpong_current_write_index, transition_info);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer0"_id].index + 1].layout, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer1"_id].index + 1].layout, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer2"_id].index + 1].layout, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer3"_id].index + 1].layout, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["depth"_id].index + 1].layout, D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["primary"_id].index + 2].layout, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["primary"_id].index + 3].layout, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["swapchain"_id].index + 1].layout, D3D12_BARRIER_LAYOUT_PRESENT);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer0"_id].index + 1].sync, D3D12_BARRIER_SYNC_PIXEL_SHADING);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer1"_id].index + 1].sync, D3D12_BARRIER_SYNC_PIXEL_SHADING);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer2"_id].index + 1].sync, D3D12_BARRIER_SYNC_PIXEL_SHADING);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer3"_id].index + 1].sync, D3D12_BARRIER_SYNC_PIXEL_SHADING);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["depth"_id].index + 1].sync, D3D12_BARRIER_SYNC_DEPTH_STENCIL);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["primary"_id].index + 2].sync, D3D12_BARRIER_SYNC_PIXEL_SHADING);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["primary"_id].index + 3].sync, D3D12_BARRIER_SYNC_PIXEL_SHADING);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["swapchain"_id].index + 1].sync, D3D12_BARRIER_SYNC_NONE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer0"_id].index + 1].access, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer1"_id].index + 1].access, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer2"_id].index + 1].access, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer3"_id].index + 1].access, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["depth"_id].index + 1].access, D3D12_BARRIER_ACCESS_DEPTH_STENCIL_WRITE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["primary"_id].index + 2].access, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["primary"_id].index + 3].access, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["swapchain"_id].index + 1].access, D3D12_BARRIER_ACCESS_NO_ACCESS);
  UpdateTransitionInfo(transition_info);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer0"_id].index].layout, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer1"_id].index].layout, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer2"_id].index].layout, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer3"_id].index].layout, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["depth"_id].index].layout, D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["primary"_id].index].layout, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["primary"_id].index + 1].layout, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["swapchain"_id].index].layout, D3D12_BARRIER_LAYOUT_PRESENT);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer0"_id].index].sync, D3D12_BARRIER_SYNC_PIXEL_SHADING);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer1"_id].index].sync, D3D12_BARRIER_SYNC_PIXEL_SHADING);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer2"_id].index].sync, D3D12_BARRIER_SYNC_PIXEL_SHADING);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer3"_id].index].sync, D3D12_BARRIER_SYNC_PIXEL_SHADING);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["depth"_id].index].sync, D3D12_BARRIER_SYNC_DEPTH_STENCIL);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["primary"_id].index].sync, D3D12_BARRIER_SYNC_PIXEL_SHADING);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["primary"_id].index + 1].sync, D3D12_BARRIER_SYNC_PIXEL_SHADING);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["swapchain"_id].index].sync, D3D12_BARRIER_SYNC_NONE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer0"_id].index].access, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer1"_id].index].access, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer2"_id].index].access, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["gbuffer3"_id].index].access, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["depth"_id].index].access, D3D12_BARRIER_ACCESS_DEPTH_STENCIL_WRITE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["primary"_id].index].access, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["primary"_id].index + 1].access, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
  CHECK_EQ(transition_info.transition_info[transition_info.transition_info_index["swapchain"_id].index].access, D3D12_BARRIER_ACCESS_NO_ACCESS);
}
