#include <wchar.h>
#include <Windows.h>
#include "dxgi1_6.h"
#include "imgui.h"
#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_dx12.h"
#include "boke/allocator.h"
#include "boke/container.h"
#include "boke/debug_assert.h"
#include "boke/str_hash.h"
#include "boke/util.h"
#include "barrier_config.h"
#include "core.h"
#include "descriptors.h"
#include "descriptors_shader_visible.h"
#include "imgui_util.h"
#include "json.h"
#include "material.h"
#include "render_pass_info.h"
#include "resources.h"
#include "string_util.h"
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
namespace {
using namespace boke;
using DxgiSwapchain = IDXGISwapChain4;
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam)) { return true; }
  switch (msg) {
    case WM_SIZE: {
#if 0
      if (g_pd3dDevice != nullptr && wParam != SIZE_MINIMIZED)
      {
        WaitForLastSubmittedFrame();
        CleanupRenderTarget();
        HRESULT result = g_pSwapChain->ResizeBuffers(0, (UINT)LOWORD(lParam), (UINT)HIWORD(lParam), DXGI_FORMAT_UNKNOWN, DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT);
        assert(SUCCEEDED(result) && "Failed to resize swapchain.");
        CreateRenderTarget();
      }
#endif
      return 0;
    }
    case WM_SYSCOMMAND: {
      if ((wParam & 0xfff0) == SC_KEYMENU) {
        // Disable ALT application menu
        return 0;
      }
      break;
    }
    case WM_DESTROY: {
      ::PostQuitMessage(0);
      return 0;
    }
  }
  return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}
auto GetPrimarybufferSize(const rapidjson::Value& json) {
  return Size2d{
    .width = json["screen_width"].GetUint(),
    .height = json["screen_height"].GetUint(),
  };
}
auto GetSwapchainBufferSize(const rapidjson::Value& json) {
  const auto& swapchain_size = json["swapchain"]["size"];
  return Size2d{
    .width = swapchain_size[0].GetUint(),
    .height = swapchain_size[1].GetUint(),
  };
}
struct WindowInfo {
  HWND hwnd{};
  LPWSTR class_name{};
  HINSTANCE h_instance{};
};
auto CreateWin32Window(const char* title_cstr, const Size2d& size, boke::AllocatorData* allocator_data) {
  auto title = ConvertAsciiCharToWchar(title_cstr, allocator_data);
  WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(nullptr), nullptr, nullptr, nullptr, nullptr, title, nullptr };
  ::RegisterClassExW(&wc);
  HWND hwnd = ::CreateWindowW(wc.lpszClassName, title, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, 0, size.width, size.height, nullptr, nullptr, wc.hInstance, nullptr);
  ::ShowWindow(hwnd, SW_SHOWDEFAULT);
  ::UpdateWindow(hwnd);
  return WindowInfo {
    .hwnd = hwnd,
    .class_name = title,
    .h_instance = wc.hInstance,
  };
}
auto ReleaseWin32Window(WindowInfo& info, boke::AllocatorData* allocator_data) {
  ::DestroyWindow(info.hwnd);
  ::UnregisterClassW(info.class_name, info.h_instance);
  Deallocate(info.class_name, allocator_data);
}
auto CreateCommandQueue(D3d12Device* const device, const D3D12_COMMAND_LIST_TYPE type, const D3D12_COMMAND_QUEUE_PRIORITY priority, const D3D12_COMMAND_QUEUE_FLAGS flags, const UINT node_mask = 0) {
  D3D12_COMMAND_QUEUE_DESC desc = { .Type = type, .Priority = priority, .Flags = flags, .NodeMask = node_mask, };
  ID3D12CommandQueue* command_queue = nullptr;
  auto hr = device->CreateCommandQueue(&desc, IID_PPV_ARGS(&command_queue));
  DEBUG_ASSERT(SUCCEEDED(hr), DebugAssert{});
  return command_queue;
}
auto CreateFence(D3d12Device* const device) {
  ID3D12Fence* fence_base{};
  auto hr = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_base));
  DEBUG_ASSERT(SUCCEEDED(hr), DebugAssert{});
  D3d12Fence* fence{};
  hr = fence_base->QueryInterface(IID_PPV_ARGS(&fence));
  DEBUG_ASSERT(SUCCEEDED(hr), DebugAssert{});
  fence_base->Release();
  return fence;
}
auto DisableAltEnterFullscreen(HWND hwnd, DxgiFactory* factory) {
  const auto hr = factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);
  if (FAILED(hr)) {
    spdlog::warn("DisableAltEnterFullscreen failed. {}", hr);
  }
}
auto IsVariableRefreshRateSupported(DxgiFactory* factory) {
  BOOL result = false;
  const auto hr = factory->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &result, sizeof(result));
  if (FAILED(hr)) {
    spdlog::warn("CheckFeatureSupport(tearing) failed. {}", hr);
  }
  return static_cast<bool>(result);
}
auto CreateSwapchain(DxgiFactory* factory, ID3D12CommandQueue* command_queue, HWND hwnd, const DXGI_FORMAT format, const uint32_t backbuffer_num) {
  DXGI_SWAP_CHAIN_DESC1 desc = {};
  desc.Width = desc.Height = 0; // get value from hwnd
  desc.Format = format;
  desc.Stereo = false;
  desc.SampleDesc.Count = 1;
  desc.SampleDesc.Quality = 0;
  desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  desc.BufferCount = backbuffer_num;
  desc.Scaling = DXGI_SCALING_NONE;
  desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
  desc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
  desc.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
  // desc.Flags = (variable_refresh_rate_supported ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0) | DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
  IDXGISwapChain1* swapchain_base = nullptr;
  auto hr = factory->CreateSwapChainForHwnd(command_queue, hwnd, &desc, nullptr, nullptr, &swapchain_base);
  DEBUG_ASSERT(SUCCEEDED(hr), DebugAssert{});
  DxgiSwapchain* swapchain{};
  hr = swapchain_base->QueryInterface(IID_PPV_ARGS(&swapchain));
  DEBUG_ASSERT(SUCCEEDED(hr), DebugAssert{});
  swapchain_base->Release();
  DEBUG_ASSERT(SUCCEEDED(hr), DebugAssert{});
  return swapchain;
}
auto GetSwapchainBuffer(DxgiSwapchain* swapchain, const uint32_t index) {
  ID3D12Resource* resource = nullptr;
  const auto hr = swapchain->GetBuffer(index, IID_PPV_ARGS(&resource));
  DEBUG_ASSERT(SUCCEEDED(hr), DebugAssert{});
  return resource;
}
auto GetSwapchainSize(DxgiSwapchain* swapchain) {
  DXGI_SWAP_CHAIN_DESC1 desc = {};
  const auto hr = swapchain->GetDesc1(&desc);
  DEBUG_ASSERT(SUCCEEDED(hr), DebugAssert{});
  return Size2d {
    .width = desc.Width,
    .height = desc.Height,
  };
}
auto SetD3d12NameToList(ID3D12Object** list, const uint32_t num, const wchar_t* basename) {
  const uint32_t name_len = 64;
  wchar_t name[name_len];
  for (uint32_t i = 0; i < num; i++) {
    swprintf(name, name_len, L"%s%d", basename, i);
    list[i]->SetName(name);
  }
}
auto CreateCommandAllocator(D3d12Device* device, const D3D12_COMMAND_LIST_TYPE type) {
  D3d12CommandAllocator* command_allocator = nullptr;
  const auto hr = device->CreateCommandAllocator(type, IID_PPV_ARGS(&command_allocator));
  DEBUG_ASSERT(SUCCEEDED(hr), DebugAssert{});
  return command_allocator;
}
auto CreateCommandList(D3d12Device* device, const D3D12_COMMAND_LIST_TYPE type) {
  D3d12CommandList* command_list = nullptr;
  const auto hr = device->CreateCommandList1(0, type, D3D12_COMMAND_LIST_FLAG_NONE, IID_PPV_ARGS(&command_list));
  DEBUG_ASSERT(SUCCEEDED(hr), DebugAssert{});
  return command_list;
}
auto StartCommandListRecording(D3d12CommandList* command_list, D3d12CommandAllocator* command_allocator, const uint32_t descriptor_heap_num, ID3D12DescriptorHeap** descriptor_heaps) {
  const auto hr = command_list->Reset(command_allocator, nullptr);
  DEBUG_ASSERT(SUCCEEDED(hr), DebugAssert{});
  command_list->SetDescriptorHeaps(descriptor_heap_num, descriptor_heaps);
}
auto EndCommandListRecording(D3d12CommandList* command_list) {
  const auto hr = command_list->Close();
  DEBUG_ASSERT(SUCCEEDED(hr), DebugAssert{});
}
struct DescriptorHandleHeapInfoCpu {
  D3D12_CPU_DESCRIPTOR_HANDLE descriptor_head_addr_cpu{};
  uint32_t descriptor_handle_increment_size{};
};
auto GetDescriptorHandleHeapInfoCpu(ID3D12DescriptorHeap* descriptor_heap, const D3D12_DESCRIPTOR_HEAP_TYPE descriptor_heap_type, D3d12Device* device) {
  const auto descriptor_head_addr_cpu = descriptor_heap->GetCPUDescriptorHandleForHeapStart();
  const auto descriptor_handle_increment_size = device->GetDescriptorHandleIncrementSize(descriptor_heap_type);
  return DescriptorHandleHeapInfoCpu {
    .descriptor_head_addr_cpu = descriptor_head_addr_cpu,
    .descriptor_handle_increment_size = descriptor_handle_increment_size,
  };
}
struct DescriptorHandleHeapInfoFull {
  D3D12_CPU_DESCRIPTOR_HANDLE descriptor_head_addr_cpu{};
  D3D12_GPU_DESCRIPTOR_HANDLE descriptor_head_addr_gpu{};
  uint32_t descriptor_handle_increment_size{};
};
auto GetDescriptorHandleHeapInfoFull(ID3D12DescriptorHeap* descriptor_heap, const D3D12_DESCRIPTOR_HEAP_TYPE descriptor_heap_type, D3d12Device* device) {
  const auto descriptor_head_addr_cpu = descriptor_heap->GetCPUDescriptorHandleForHeapStart();
  const auto descriptor_head_addr_gpu = descriptor_heap->GetGPUDescriptorHandleForHeapStart();
  const auto descriptor_handle_increment_size = device->GetDescriptorHandleIncrementSize(descriptor_heap_type);
  return DescriptorHandleHeapInfoFull {
    .descriptor_head_addr_cpu = descriptor_head_addr_cpu,
    .descriptor_head_addr_gpu = descriptor_head_addr_gpu,
    .descriptor_handle_increment_size = descriptor_handle_increment_size,
  };
}
auto GetDescriptorHeapHandleCpu(const uint32_t index, const uint32_t increment_size, const D3D12_CPU_DESCRIPTOR_HANDLE& head_addr_cpu) {
  return D3D12_CPU_DESCRIPTOR_HANDLE{
    .ptr = head_addr_cpu.ptr + increment_size * index,
  };
}
auto GetDescriptorHeapHandleGpu(const uint32_t index, const uint32_t increment_size, const D3D12_GPU_DESCRIPTOR_HANDLE& head_addr_gpu) {
  return D3D12_GPU_DESCRIPTOR_HANDLE{
    .ptr = head_addr_gpu.ptr + increment_size * index,
  };
}
auto GetDescriptorHandle(const uint32_t index, const DescriptorHandleHeapInfoFull& info) {
  const auto handle_cpu = GetDescriptorHeapHandleCpu(index, info.descriptor_handle_increment_size, info.descriptor_head_addr_cpu);
  const auto handle_gpu = GetDescriptorHeapHandleGpu(index, info.descriptor_handle_increment_size, info.descriptor_head_addr_gpu);
  return std::make_pair(handle_cpu, handle_gpu);
}
enum class WindowMessage : uint8_t { kContinue, kQuit, };
auto ProcessWindowMessages() {
  WindowMessage result = WindowMessage::kContinue;
  MSG msg{};
  while (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
    ::TranslateMessage(&msg);
    ::DispatchMessage(&msg);
    if (msg.message == WM_QUIT) {
      result = WindowMessage::kQuit;
    }
  }
  return result;
}
auto WaitForSwapchain(HANDLE handle) {
  const auto result = WaitForSingleObjectEx(handle, 1000, true);
  switch (result) {
    case WAIT_TIMEOUT: {
      spdlog::error("wait timeout");
      return false;
    }
    case WAIT_FAILED: {
      spdlog::error("wait failed.");
      return false;
    }
  }
  return true;
}
auto WaitForFence(HANDLE fence_event, D3d12Fence* fence, const uint64_t fence_signal_val) {
  const auto comp_val = fence->GetCompletedValue();
  if (comp_val >= fence_signal_val) { return; }
  const auto hr = fence->SetEventOnCompletion(fence_signal_val, fence_event);
  DEBUG_ASSERT(SUCCEEDED(hr), DebugAssert{});
}
struct GfxCoreUnit {
  WindowInfo window_info{};
  GfxLibraries gfx_libraries{};
  DxgiCore dxgi_core{};
};
auto PrepareGfxCore(const char* title, const Size2d& size, const boke::AdapterType adapter_type, boke::AllocatorData* allocator_data) {
  auto window_info = CreateWin32Window(title, size, allocator_data);
  auto gfx_libraries = LoadGfxLibraries();
  auto dxgi_core = InitDxgi(gfx_libraries.dxgi_library, adapter_type);
  return GfxCoreUnit {
    .window_info = window_info,
    .gfx_libraries = gfx_libraries,
    .dxgi_core = dxgi_core,
  };
}
auto ReleaseGfxCore(GfxCoreUnit& core, boke::AllocatorData* allocator_data) {
  TermDxgi(core.dxgi_core);
  ReleaseGfxLibraries(core.gfx_libraries);
  ReleaseWin32Window(core.window_info, allocator_data);
}
struct RenderPassFuncCommonParams {
  StrHashMap<uint32_t>& pingpong_current_write_index;
  StrHashMap<ID3D12Resource*>& resources;
  DescriptorHandles& descriptor_handles;
  MaterialSet& material_set;
  Size2d primarybuffer_size{};
};
struct RenderPassFuncIndividualParams {
  RenderPassInfo& render_pass_info;
  D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle;
};
void SetViewportAndScissor(const Size2d& size, D3d12CommandList* command_list) {
  {
    D3D12_VIEWPORT viewport{0.0f, 0.0f, static_cast<float>(size.width), static_cast<float>(size.height), D3D12_MIN_DEPTH, D3D12_MAX_DEPTH};
    command_list->RSSetViewports(1, &viewport);
  }
  {
    D3D12_RECT scissor_rect{0L, 0L, static_cast<LONG>(size.width), static_cast<LONG>(size.height)};
    command_list->RSSetScissorRects(1, &scissor_rect);
  }
}
void SetRtvAndDsv(const RenderPassFuncCommonParams& common_params, const RenderPassFuncIndividualParams& pass_params, D3d12CommandList* command_list) {
  const uint32_t max_rtv_num = 8;
  D3D12_CPU_DESCRIPTOR_HANDLE rtv_handles[max_rtv_num]{};
  for (uint32_t i = 0; i < pass_params.render_pass_info.rtv_num; i++) {
    rtv_handles[i] = (*common_params.descriptor_handles.rtv)[GetResourceIdPingpongWrite(pass_params.render_pass_info.rtv[i], common_params.pingpong_current_write_index)];
  }
  const auto dsv_handle = (pass_params.render_pass_info.dsv != kEmptyStr) ? &(*common_params.descriptor_handles.dsv)[pass_params.render_pass_info.dsv] : nullptr;
  command_list->OMSetRenderTargets(pass_params.render_pass_info.rtv_num, rtv_handles, false, dsv_handle);
}
void RenderPassGeometry(const RenderPassFuncCommonParams& common_params, const RenderPassFuncIndividualParams& pass_params, D3d12CommandList* command_list) {
  SetViewportAndScissor(common_params.primarybuffer_size, command_list);
  SetRtvAndDsv(common_params, pass_params, command_list);
  const auto& material_id = pass_params.render_pass_info.material_id;
  command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  command_list->SetGraphicsRootSignature(GetRootsig(common_params.material_set, material_id));
  if (pass_params.gpu_handle.ptr) {
    command_list->SetGraphicsRootDescriptorTable(0, pass_params.gpu_handle);
  }
  command_list->OMSetStencilRef(pass_params.render_pass_info.stencil_val);
  command_list->SetPipelineState(GetPso(common_params.material_set, material_id));
  command_list->DispatchMesh(1, 1, 1);
}
void RenderPassPostProcess(const RenderPassFuncCommonParams& common_params, const RenderPassFuncIndividualParams& pass_params, D3d12CommandList* command_list) {
  SetViewportAndScissor(common_params.primarybuffer_size, command_list);
  SetRtvAndDsv(common_params, pass_params, command_list);
  const auto& material_id = pass_params.render_pass_info.material_id;
  command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  command_list->SetGraphicsRootSignature(GetRootsig(common_params.material_set, material_id));
  if (pass_params.gpu_handle.ptr) {
    command_list->SetGraphicsRootDescriptorTable(0, pass_params.gpu_handle);
  }
  command_list->OMSetStencilRef(pass_params.render_pass_info.stencil_val);
  command_list->SetPipelineState(GetPso(common_params.material_set, material_id));
  command_list->DispatchMesh(1, 1, 1);
}
void RenderPassNoOp(const RenderPassFuncCommonParams&, const RenderPassFuncIndividualParams&, D3d12CommandList*) {}
void RenderPassImgui(const RenderPassFuncCommonParams& common_params, const RenderPassFuncIndividualParams&, D3d12CommandList* command_list) {
  RenderImgui(command_list, (*common_params.descriptor_handles.rtv)["swapchain"_id]);
}
using RenderPassFunc = void (*)(const RenderPassFuncCommonParams&, const RenderPassFuncIndividualParams&, D3d12CommandList*);
auto GatherRenderPassFunc(const uint32_t render_pass_info_len, const RenderPassInfo* render_pass_info, RenderPassFunc* render_pass_func) {
  for (uint32_t i = 0; i < render_pass_info_len; i++) {
    switch (render_pass_info[i].type) {
      case "geometry"_id: {
        render_pass_func[i] = RenderPassGeometry;
        break;
      }
      case "postprocess"_id: {
        render_pass_func[i] = RenderPassPostProcess;
        break;
      }
      case "imgui"_id: {
        render_pass_func[i] = RenderPassImgui;
        break;
      }
      case "no-op"_id: {
        render_pass_func[i] = RenderPassNoOp;
        break;
      }
      default: {
        DEBUG_ASSERT(false, DebugAssert{});
        render_pass_func[i] = RenderPassNoOp;
        break;
      }
    }
  }
}
auto GetJsonStrHash(const rapidjson::Value& json, const char* const name) {
  if (!json.HasMember(name)) { return kEmptyStr; }
  return GetStrHash(json[name].GetString());
}
StrHash* GetJsonStrHashList(const rapidjson::Value& json, const char* const name, AllocatorData* allocator_data, uint32_t* len) {
  if (!json.HasMember(name)) {
    *len = 0;
    return nullptr;
  }
  *len = json[name].Size();
  auto list = AllocateArray<StrHash>(*len, allocator_data);
  for (uint32_t i = 0; i < *len; i++) {
    const auto& elem = json[name][i];
    list[i] = GetStrHash(elem.GetString());
  }
  return list;
}
auto ParseRenderPass(const rapidjson::Value& json, AllocatorData* allocator_data, uint32_t* render_pass_info_len) {
  *render_pass_info_len = json.Size();
  auto render_pass_info = AllocateArray<RenderPassInfo>(*render_pass_info_len, allocator_data);
  for (uint32_t i = 0; i < *render_pass_info_len; i++) {
    const auto& pass = json[i];
    render_pass_info[i].queue = GetJsonStrHash(pass, "queue");
    render_pass_info[i].type = GetJsonStrHash(pass, "type");
    render_pass_info[i].material = GetJsonStrHash(pass, "material");
    render_pass_info[i].srv = GetJsonStrHashList(pass, "srv", allocator_data, &render_pass_info[i].srv_num);
    render_pass_info[i].rtv = GetJsonStrHashList(pass, "rtv", allocator_data, &render_pass_info[i].rtv_num);
    render_pass_info[i].dsv = GetJsonStrHash(pass, "dsv");
    render_pass_info[i].present = GetJsonStrHash(pass, "present");
    render_pass_info[i].material_id = GetJsonStrHash(pass, "material");
    render_pass_info[i].stencil_val = pass.HasMember("stencil_val") ? static_cast<uint8_t>(pass["stencil_val"].GetUint()) : 0;
  }
  return render_pass_info;
}
}
#include "doctest/doctest.h"
TEST_CASE("imgui") {
  ProcessWindowMessages(); // to get rid of previous messages
  using namespace boke;
  const uint32_t main_buffer_size_in_bytes = 16 * 1024;
  std::byte main_buffer[main_buffer_size_in_bytes];
  auto allocator_data = GetAllocatorData(main_buffer, main_buffer_size_in_bytes);
  auto json = GetJson("tests/config-imgui.json", allocator_data);
  const uint32_t frame_buffer_num = json["frame_buffer_num"].GetUint();
  // core units
  const auto primarybuffer_size = GetPrimarybufferSize(json);
  auto window_info = CreateWin32Window(json["title"].GetString(), primarybuffer_size, allocator_data);
  auto gfx_libraries = LoadGfxLibraries();
  auto dxgi = InitDxgi(gfx_libraries.dxgi_library, AdapterType::kHighPerformance);
  auto device = CreateDevice(gfx_libraries.d3d12_library, dxgi.adapter);
  REQUIRE_NE(device, nullptr);
  // command queue & fence
  auto command_queue = CreateCommandQueue(device, D3D12_COMMAND_LIST_TYPE_DIRECT, D3D12_COMMAND_QUEUE_PRIORITY_NORMAL, D3D12_COMMAND_QUEUE_FLAG_NONE);
  REQUIRE_NE(command_queue, nullptr);
  auto fence = CreateFence(device);
  REQUIRE_NE(fence, nullptr);
  auto fence_event = CreateEvent(nullptr, false, false, nullptr);
  REQUIRE_NE(fence_event, nullptr);
  auto fence_signal_val_list = AllocateArray<uint64_t>(frame_buffer_num, allocator_data);
  std::fill(fence_signal_val_list, fence_signal_val_list + frame_buffer_num, 0);
  uint64_t fence_signal_val = 0;
  // swapchain
  const auto swapchain_format = GetDxgiFormat(json["swapchain"]["format"].GetString());
  const auto swapchain_buffer_num = json["swapchain"]["num"].GetUint();
  auto swapchain = CreateSwapchain(dxgi.factory, command_queue, window_info.hwnd, swapchain_format, swapchain_buffer_num);
  REQUIRE_NE(swapchain, nullptr);
  {
    const auto hr = swapchain->SetMaximumFrameLatency(frame_buffer_num);
    CHECK_UNARY(SUCCEEDED(hr));
  }
  auto swapchain_latency_object = swapchain->GetFrameLatencyWaitableObject();
  auto swapchain_rtv = AllocateArray<D3D12_CPU_DESCRIPTOR_HANDLE>(swapchain_buffer_num, allocator_data);
  auto swapchain_resources = AllocateArray<ID3D12Resource*>(swapchain_buffer_num, allocator_data);
  for (uint32_t i = 0; i < swapchain_buffer_num; i++) {
    swapchain_resources[i] = GetSwapchainBuffer(swapchain, i);
    REQUIRE_NE(swapchain_resources[i], nullptr);
  }
  SetD3d12NameToList(reinterpret_cast<ID3D12Object**>(swapchain_resources), swapchain_buffer_num, L"swapchain");
  auto descriptor_heap_rtv = CreateDescriptorHeap(device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, swapchain_buffer_num, D3D12_DESCRIPTOR_HEAP_FLAG_NONE);
  {
    const auto descriptor_handle_heap_info_cpu = GetDescriptorHandleHeapInfoCpu(descriptor_heap_rtv, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, device);
    const D3D12_RENDER_TARGET_VIEW_DESC rtv_desc = {
      .Format = swapchain_format,
      .ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D,
      .Texture2D = {},
    };
    for (uint32_t i = 0; i < swapchain_buffer_num; i++) {
      swapchain_rtv[i] = GetDescriptorHeapHandleCpu(i, descriptor_handle_heap_info_cpu.descriptor_handle_increment_size, descriptor_handle_heap_info_cpu.descriptor_head_addr_cpu);
      device->CreateRenderTargetView(swapchain_resources[i], &rtv_desc, swapchain_rtv[i]);
    }
  }
  // command allocator & list
  auto command_allocator = AllocateArray<D3d12CommandAllocator*>(frame_buffer_num, allocator_data);
  for (uint32_t i = 0; i < frame_buffer_num; i++) {
    command_allocator[i] = CreateCommandAllocator(device, D3D12_COMMAND_LIST_TYPE_DIRECT);
    REQUIRE_NE(command_allocator[i], nullptr);
  }
  auto command_list = CreateCommandList(device, D3D12_COMMAND_LIST_TYPE_DIRECT);
  REQUIRE_NE(command_list, nullptr);
  // descriptor heap
  auto descriptor_heap = CreateDescriptorHeap(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, json["descriptor_handles"]["shader_visible_buffer_num"].GetUint(), D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE);
  REQUIRE_NE(descriptor_heap, nullptr);
  const auto descriptor_handle_heap_info_full = GetDescriptorHandleHeapInfoFull(descriptor_heap, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, device);
  // imgui
  const auto [imgui_font_handle_cpu, imgui_font_handle_gpu] = GetDescriptorHandle(0, descriptor_handle_heap_info_full);
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
  ImGui_ImplWin32_Init(window_info.hwnd);
  ImGui_ImplDX12_Init(device,
                      swapchain_buffer_num,
                      swapchain_format,
                      descriptor_heap,
                      imgui_font_handle_cpu,
                      imgui_font_handle_gpu);
  ImGui_ImplDX12_CreateDeviceObjects();
  // frame start
  const uint32_t max_loop_num = json["max_loop_num"].GetUint();
  for (uint32_t frame_count = 0; frame_count < max_loop_num; frame_count++) {
    if (ProcessWindowMessages() == WindowMessage::kQuit) { break; }
    const auto frame_index = frame_count % frame_buffer_num;
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    ImGui::ShowDemoWindow();
    ImGui::Render();
    if (!WaitForSwapchain(swapchain_latency_object)) { break; }
    WaitForFence(fence_event, fence, fence_signal_val_list[frame_index]);
    const auto swapchain_backbuffer_index = swapchain->GetCurrentBackBufferIndex();
    StartCommandListRecording(command_list, command_allocator[frame_index], 1, &descriptor_heap);
    {
      D3D12_TEXTURE_BARRIER barrier {
        .SyncBefore = D3D12_BARRIER_SYNC_NONE,
        .SyncAfter  = D3D12_BARRIER_SYNC_RENDER_TARGET,
        .AccessBefore = D3D12_BARRIER_ACCESS_NO_ACCESS,
        .AccessAfter  = D3D12_BARRIER_ACCESS_RENDER_TARGET,
        .LayoutBefore = D3D12_BARRIER_LAYOUT_PRESENT,
        .LayoutAfter  = D3D12_BARRIER_LAYOUT_RENDER_TARGET,
        .pResource = swapchain_resources[swapchain_backbuffer_index],
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
      D3D12_BARRIER_GROUP barrier_group {
        .Type = D3D12_BARRIER_TYPE_TEXTURE,
        .NumBarriers = 1,
        .pTextureBarriers = &barrier,
      };
      command_list->Barrier(1, &barrier_group);
    }
    {
      const FLOAT clear_color[] = {0.0f, 0.0f, 0.0f, 0.0f,};
      command_list->ClearRenderTargetView(swapchain_rtv[swapchain_backbuffer_index], clear_color, 0, nullptr);
      command_list->OMSetRenderTargets(1, &swapchain_rtv[swapchain_backbuffer_index], false, nullptr);
      command_list->SetDescriptorHeaps(1, &descriptor_heap);
      ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), command_list);
    }
    {
      D3D12_TEXTURE_BARRIER barrier {
        .SyncBefore = D3D12_BARRIER_SYNC_RENDER_TARGET,
        .SyncAfter  = D3D12_BARRIER_SYNC_NONE,
        .AccessBefore = D3D12_BARRIER_ACCESS_RENDER_TARGET,
        .AccessAfter  = D3D12_BARRIER_ACCESS_NO_ACCESS,
        .LayoutBefore = D3D12_BARRIER_LAYOUT_RENDER_TARGET,
        .LayoutAfter  = D3D12_BARRIER_LAYOUT_PRESENT,
        .pResource = swapchain_resources[swapchain_backbuffer_index],
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
      D3D12_BARRIER_GROUP barrier_group {
        .Type = D3D12_BARRIER_TYPE_TEXTURE,
        .NumBarriers = 1,
        .pTextureBarriers = &barrier,
      };
      command_list->Barrier(1, &barrier_group);
    }
    EndCommandListRecording(command_list);
    command_queue->ExecuteCommandLists(1, reinterpret_cast<ID3D12CommandList**>(&command_list));
    // https://learn.microsoft.com/en-us/windows/win32/api/dxgi/nf-dxgi-idxgiswapchain-present
    swapchain->Present(1, 0);
    fence_signal_val++;
    command_queue->Signal(fence, fence_signal_val);
    fence_signal_val_list[frame_index] = fence_signal_val;
  }
  WaitForFence(fence_event, fence, fence_signal_val);
  ImGui_ImplDX12_Shutdown();
  ImGui_ImplWin32_Shutdown();
  ImGui::DestroyContext();
  descriptor_heap->Release();
  command_list->Release();
  for (uint32_t i = 0; i < frame_buffer_num; i++) {
    command_allocator[i]->Release();
  }
  Deallocate(command_allocator, allocator_data);
  descriptor_heap_rtv->Release();
  for (uint32_t i = 0; i < swapchain_buffer_num; i++) {
    swapchain_resources[i]->Release();
  }
  swapchain->Release();
  fence->Release();
  command_queue->Release();
  device->Release();
  TermDxgi(dxgi);
  ReleaseGfxLibraries(gfx_libraries);
  ReleaseWin32Window(window_info, allocator_data);
}
TEST_CASE("multiple render pass") {
  ProcessWindowMessages(); // to get rid of previous messages
  using namespace boke;
  // allocator
  const uint32_t main_buffer_size_in_bytes = 1024 * 1024;
  auto main_buffer = new std::byte[main_buffer_size_in_bytes];
  auto allocator_data = GetAllocatorData(main_buffer, main_buffer_size_in_bytes);
  InitStrHashSystem(allocator_data);
  const auto json = GetJson("tests/formatted-config-multipass.json", allocator_data);
  // render pass & resource info
  const uint32_t frame_buffer_num = json["frame_buffer_num"].GetUint();
  uint32_t render_pass_info_len{};
  auto render_pass_info = ParseRenderPass(json["render_pass"], allocator_data, &render_pass_info_len);
  // core units
  const auto primarybuffer_size = GetSwapchainBufferSize(json);
  auto core = PrepareGfxCore(json["title"].GetString(), primarybuffer_size, AdapterType::kHighPerformance, allocator_data);
  auto device = CreateDevice(core.gfx_libraries.d3d12_library, core.dxgi_core.adapter);
  // resource info
  StrHashMap<ResourceInfo> resource_info(GetAllocatorCallbacks(allocator_data));
  ParseResourceInfo(json["resource"], resource_info);
  StrHashMap<uint32_t> pingpong_current_write_index(GetAllocatorCallbacks(allocator_data));
  InitPingpongCurrentWriteIndex(resource_info, pingpong_current_write_index);
  // resources
  auto gpu_memory_allocator = CreateGpuMemoryAllocator(core.dxgi_core.adapter, device, allocator_data);
  StrHashMap<D3D12MA::Allocation*> allocations(GetAllocatorCallbacks(allocator_data));
  StrHashMap<ID3D12Resource*> resources(GetAllocatorCallbacks(allocator_data));
  CreateResources(resource_info, gpu_memory_allocator, allocations, resources);
  // descriptor handles
  const auto swapchain_buffer_num = frame_buffer_num + 1;
  auto descriptor_heaps = CreateDescriptorHeaps(resource_info, device, {swapchain_buffer_num, 0, 1/*imgui_font*/});
  DescriptorHandles descriptor_handles{
    .rtv = New<StrHashMap<D3D12_CPU_DESCRIPTOR_HANDLE>>(allocator_data, GetAllocatorCallbacks(allocator_data)),
    .dsv = New<StrHashMap<D3D12_CPU_DESCRIPTOR_HANDLE>>(allocator_data, GetAllocatorCallbacks(allocator_data)),
    .srv = New<StrHashMap<D3D12_CPU_DESCRIPTOR_HANDLE>>(allocator_data, GetAllocatorCallbacks(allocator_data)),
  };
  PrepareDescriptorHandles(resource_info, resources, device, descriptor_heaps.head_addr, descriptor_heaps.increment_size, descriptor_handles);
  // descriptor handles (gpu)
  const uint32_t shader_visible_descriptor_handle_num = json["descriptor_handles"]["shader_visible_buffer_num"].GetUint();
  auto shader_visible_descriptor_heap = CreateDescriptorHeap(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, shader_visible_descriptor_handle_num, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE);
  ShaderVisibleDescriptorHandleInfo shader_visible_descriptor_handle_info{
    .head_addr_cpu = shader_visible_descriptor_heap->GetCPUDescriptorHandleForHeapStart(),
    .head_addr_gpu = shader_visible_descriptor_heap->GetGPUDescriptorHandleForHeapStart(),
    .increment_size = descriptor_heaps.increment_size.cbv_srv_uav,
    .reserved_handle_num = 0,
    .total_handle_num = shader_visible_descriptor_handle_num,
  };
  uint32_t shader_visible_descriptor_handle_occupied_handle_num = 0;
  // barrier resources
  BarrierSet barrier_set = {
    .transition_info = New<StrHashMap<BarrierTransitionInfoPerResource>>(allocator_data, GetAllocatorCallbacks(allocator_data)),
    .next_transition_info = New<StrHashMap<BarrierTransitionInfoPerResource>>(allocator_data, GetAllocatorCallbacks(allocator_data)),
  };
  InitTransitionInfo(resource_info, *barrier_set.transition_info);
  // command queue & fence
  auto command_queue = CreateCommandQueue(device, D3D12_COMMAND_LIST_TYPE_DIRECT, D3D12_COMMAND_QUEUE_PRIORITY_NORMAL, D3D12_COMMAND_QUEUE_FLAG_NONE);
  auto fence = CreateFence(device);
  auto fence_event = CreateEvent(nullptr, false, false, nullptr);
  auto fence_signal_val_list = AllocateArray<uint64_t>(frame_buffer_num, allocator_data);
  std::fill(fence_signal_val_list, fence_signal_val_list + frame_buffer_num, 0);
  uint64_t fence_signal_val = 0;
  // command allocator & list
  auto command_allocator = AllocateArray<D3d12CommandAllocator*>(frame_buffer_num, allocator_data);
  for (uint32_t i = 0; i < frame_buffer_num; i++) {
    command_allocator[i] = CreateCommandAllocator(device, D3D12_COMMAND_LIST_TYPE_DIRECT);
  }
  auto command_list = CreateCommandList(device, D3D12_COMMAND_LIST_TYPE_DIRECT);
  // swapchain
  const auto swapchain_format = GetDxgiFormat(json["swapchain"]["format"].GetString());
  auto swapchain = CreateSwapchain(core.dxgi_core.factory, command_queue, core.window_info.hwnd, swapchain_format, swapchain_buffer_num);
  {
    const auto hr = swapchain->SetMaximumFrameLatency(frame_buffer_num);
    CHECK_UNARY(SUCCEEDED(hr));
  }
  auto swapchain_latency_object = swapchain->GetFrameLatencyWaitableObject();
  auto swapchain_resources = AllocateArray<ID3D12Resource*>(swapchain_buffer_num, allocator_data);
  {
    for (uint32_t i = 0; i < swapchain_buffer_num; i++) {
      swapchain_resources[i] = GetSwapchainBuffer(swapchain, i);
    }
    SetD3d12NameToList(reinterpret_cast<ID3D12Object**>(swapchain_resources), swapchain_buffer_num, L"swapchain");
    AddDescriptorHandlesRtv("swapchain"_id, swapchain_format, swapchain_resources, swapchain_buffer_num, device, descriptor_heaps.head_addr.rtv, descriptor_heaps.increment_size.rtv, descriptor_handles);
  }
  // materials
  StrHashMap<StrHash> material_rootsig_map(GetAllocatorCallbacks(allocator_data));
  StrHashMap<ID3D12RootSignature*> rootsig_list(GetAllocatorCallbacks(allocator_data));
  StrHashMap<ID3D12PipelineState*> pso_list(GetAllocatorCallbacks(allocator_data));
  MaterialSet material_set {
    .material_rootsig_map = &material_rootsig_map,
    .rootsig_list = &rootsig_list,
    .pso_list = &pso_list,
  };
  CreateMaterialSet(json["material"], device, allocator_data, material_set);
  // render pass
  auto render_pass_func = AllocateArray<RenderPassFunc>(render_pass_info_len, allocator_data);
  GatherRenderPassFunc(render_pass_info_len, render_pass_info, render_pass_func);
  // init imgui
  {
    AddDescriptorHandlesSrv("imgui_font"_id, DXGI_FORMAT_UNKNOWN, nullptr, 1,  device, descriptor_heaps.head_addr.cbv_srv_uav, descriptor_heaps.increment_size.cbv_srv_uav, descriptor_handles);
    const auto imgui_font_cpu_handle = (*descriptor_handles.srv)["imgui_font"_id];
    const auto imgui_font_gpu_handle = shader_visible_descriptor_handle_info.head_addr_gpu;
    InitImgui(core.window_info.hwnd, device, swapchain_buffer_num, swapchain_format,
              shader_visible_descriptor_heap, imgui_font_cpu_handle, imgui_font_gpu_handle);
    const auto dst_cpu_handle = shader_visible_descriptor_handle_info.head_addr_cpu;
    device->CopyDescriptorsSimple(1, dst_cpu_handle, imgui_font_cpu_handle, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    shader_visible_descriptor_handle_info.reserved_handle_num++;
  }
  // frame loop
  const uint32_t max_loop_num = json["max_loop_num"].GetUint();
  RenderPassFuncCommonParams render_pass_common_params {
    .pingpong_current_write_index = pingpong_current_write_index,
    .resources = resources,
    .descriptor_handles = descriptor_handles,
    .material_set = material_set,
    .primarybuffer_size = primarybuffer_size,
  };
  for (uint32_t frame_count = 0; frame_count < max_loop_num; frame_count++) {
    if (ProcessWindowMessages() == WindowMessage::kQuit) { break; }
    const auto frame_index = frame_count % frame_buffer_num;
    InformImguiNewFrame();
    if (!WaitForSwapchain(swapchain_latency_object)) { break; }
    WaitForFence(fence_event, fence, fence_signal_val_list[frame_index]);
    // bind current swapchain backbuffer
    const auto swapchain_backbuffer_index = swapchain->GetCurrentBackBufferIndex();
    resources["swapchain"_id] = swapchain_resources[swapchain_backbuffer_index];
    (*descriptor_handles.rtv)["swapchain"_id] = (*descriptor_handles.rtv)[GetPinpongResourceId("swapchain"_id, swapchain_backbuffer_index)];
    // record commands
    StartCommandListRecording(command_list, command_allocator[frame_index], 1, &shader_visible_descriptor_heap);
    for (uint32_t i = 0; i < render_pass_info_len; i++) {
      UpdateTransitionInfo(barrier_set);
      FlipPingPongIndex(render_pass_info[i], barrier_set, pingpong_current_write_index);
      ConfigureRenderPassBarriersTextureTransitions(render_pass_info[i], pingpong_current_write_index, barrier_set);
      ProcessBarriers(barrier_set, resources, pingpong_current_write_index, command_list);
      const auto gpu_handle = PrepareRenderPassShaderVisibleDescriptorHandles(render_pass_info[i],
                                                                              descriptor_handles,
                                                                              pingpong_current_write_index,
                                                                              device,
                                                                              shader_visible_descriptor_handle_info,
                                                                              &shader_visible_descriptor_handle_occupied_handle_num);
      render_pass_func[i](render_pass_common_params, {render_pass_info[i], gpu_handle,}, command_list);
    }
    EndCommandListRecording(command_list);
    command_queue->ExecuteCommandLists(1, reinterpret_cast<ID3D12CommandList**>(&command_list));
    ResetBarrierSyncAccessStatus(barrier_set);
    swapchain->Present(1, 0);
    fence_signal_val++;
    command_queue->Signal(fence, fence_signal_val);
    fence_signal_val_list[frame_index] = fence_signal_val;
  }
  // terminate
  Deallocate(render_pass_func, allocator_data);
  WaitForFence(fence_event, fence, fence_signal_val);
  ReleaseMaterialSet(material_set);
  for (uint32_t i = 0; i < swapchain_buffer_num; i++) {
    swapchain_resources[i]->Release();
  }
  Deallocate(swapchain_resources, allocator_data);
  TermImgui();
  swapchain->Release();
  command_list->Release();
  for (uint32_t i = 0; i < frame_buffer_num; i++) {
    command_allocator[i]->Release();
  }
  fence->Release();
  command_queue->Release();
  barrier_set.transition_info->~StrHashMap<BarrierTransitionInfoPerResource>();
  barrier_set.next_transition_info->~StrHashMap<BarrierTransitionInfoPerResource>();
  shader_visible_descriptor_heap->Release();
  descriptor_handles.rtv->~StrHashMap<D3D12_CPU_DESCRIPTOR_HANDLE>();
  descriptor_handles.dsv->~StrHashMap<D3D12_CPU_DESCRIPTOR_HANDLE>();
  descriptor_handles.srv->~StrHashMap<D3D12_CPU_DESCRIPTOR_HANDLE>();
  ReleaseDescriptorHeaps(descriptor_heaps);
  ReleaseAllocations(std::move(allocations), std::move(resources));
  ReleaseGpuMemoryAllocator(gpu_memory_allocator);
  pingpong_current_write_index.~StrHashMap<uint32_t>();
  resource_info.~StrHashMap<ResourceInfo>();
  device->Release();
  ReleaseGfxCore(core, allocator_data);
  TermStrHashSystem(allocator_data);
  delete[] main_buffer;
}
