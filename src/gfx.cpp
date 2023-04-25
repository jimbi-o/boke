#include <Windows.h>
#include "dxgi1_6.h"
#ifndef NDEBUG
#include "dxgidebug.h"
#endif
#ifndef NDEBUG
#include "d3d12sdklayers.h"
#endif
#include "boke/allocator.h"
#include "boke/debug_assert.h"
#include "imgui.h"
#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_dx12.h"
#include "json.h"
#include "string_util.h"
#include "doctest/doctest.h"
#define CALL_DLL_FUNCTION(library, function) reinterpret_cast<decltype(&function)>(GetProcAddress(library, #function))
#define LOAD_DLL_FUNCTION(library, function) decltype(&function) function = reinterpret_cast<decltype(function)>(GetProcAddress(library, #function))
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
namespace {
using namespace boke;
using D3d12Device = ID3D12Device10;
using DxgiSwapchain = IDXGISwapChain4;
using D3d12Fence = ID3D12Fence1;
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
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
struct WindowInfo {
  HWND hwnd{};
  LPWSTR class_name{};
  HINSTANCE h_instance{};
};
auto CreateWin32Window(const rapidjson::Document& json, boke::AllocatorData* allocator_data) {
  auto title = ConvertAsciiCharToWchar(json["title"].GetString(), allocator_data);
  WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(nullptr), nullptr, nullptr, nullptr, nullptr, title, nullptr };
  ::RegisterClassExW(&wc);
  HWND hwnd = ::CreateWindowW(wc.lpszClassName, title, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, 0, json["screen_width"].GetInt(), json["screen_height"].GetInt(), nullptr, nullptr, wc.hInstance, nullptr);
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
struct GfxLibraries {
  HMODULE dxgi_library{};
  HMODULE d3d12_library{};
};
auto LoadGfxLibraries() {
  auto dxgi_library = LoadLibrary("Dxgi.dll");
  DEBUG_ASSERT(dxgi_library, DebugAssert());
  auto d3d12_library = LoadLibrary("D3D12.dll");
  DEBUG_ASSERT(d3d12_library, DebugAssert{});
  return GfxLibraries {
    .dxgi_library = dxgi_library,
    .d3d12_library = d3d12_library,
  };
}
auto ReleaseGfxLibraries(GfxLibraries& libraries) {
  FreeLibrary(libraries.d3d12_library);
#ifndef NDEBUG
  IDXGIDebug1* debug = nullptr;
  auto hr = CALL_DLL_FUNCTION(libraries.dxgi_library, DXGIGetDebugInterface1)(0, IID_PPV_ARGS(&debug));
  if (FAILED(hr)) {
    spdlog::warn("DXGIGetDebugInterface failed. {}", hr);
    return;
  }
  debug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_ALL);
  debug->Release();
#endif
  FreeLibrary(libraries.dxgi_library);
}
struct DxgiCore {
  IDXGIFactory7* factory{};
  IDXGIAdapter4* adapter{};
};
enum AdapterType : uint8_t { kHighPerformance, kWarp, };
template <AdapterType adapter_type>
auto InitDxgi(HMODULE library) {
  SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  IDXGIFactory7* factory = nullptr;
  auto hr = CALL_DLL_FUNCTION(library, CreateDXGIFactory2)(0, IID_PPV_ARGS(&factory));
  DEBUG_ASSERT(SUCCEEDED(hr) && factory, DebugAssert{});
  IDXGIAdapter4* adapter = nullptr;
  if constexpr (adapter_type == AdapterType::kWarp) {
    hr = factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter));
  } else {
    hr = factory->EnumAdapterByGpuPreference(0, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter));
  }
  DEBUG_ASSERT(SUCCEEDED(hr) && adapter, DebugAssert{});
  {
    DXGI_ADAPTER_DESC1 desc;
    adapter->GetDesc1(&desc);
    if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
      spdlog::info("IDXGIAdapter4 is software");
    }
  }
  return DxgiCore {
    .factory = factory,
    .adapter = adapter,
  };
}
auto TermDxgi(DxgiCore& dxgi) {
  auto refval = dxgi.adapter->Release();
  if (refval != 0UL) {
    spdlog::error("adapter reference left. {}", refval);
  }
  refval = dxgi.factory->Release();
  if (refval != 0UL) {
    spdlog::error("factory reference left. {}", refval);
  }
}
auto CreateDevice(HMODULE library, IDXGIAdapter4* adapter) {
#ifndef NDEBUG
  if (IsDebuggerPresent()) {
    ID3D12Debug* debug_interface = nullptr;
    if (SUCCEEDED(CALL_DLL_FUNCTION(library, D3D12GetDebugInterface)(IID_PPV_ARGS(&debug_interface)))) {
      debug_interface->EnableDebugLayer();
      spdlog::info("EnableDebugLayer");
      ID3D12Debug1* debug_interface1 = nullptr;
      if (SUCCEEDED(debug_interface->QueryInterface(IID_PPV_ARGS(&debug_interface1)))) {
        debug_interface1->SetEnableGPUBasedValidation(true);
        spdlog::info("SetEnableGPUBasedValidation");
        debug_interface1->Release();
      }
      debug_interface->Release();
    }
  }
#endif
  {
    UUID experimental_features[] = { D3D12ExperimentalShaderModels };
    auto hr = CALL_DLL_FUNCTION(library, D3D12EnableExperimentalFeatures)(1, experimental_features, nullptr, nullptr);
    if (SUCCEEDED(hr)) {
      spdlog::info("experimental shader models enabled.");
    } else {
      spdlog::warn("Failed to enable experimental shader models. {}", hr);
    }
  }
  const auto feature_level = D3D_FEATURE_LEVEL_12_2;
  D3d12Device* device{};
  auto hr = CALL_DLL_FUNCTION(library, D3D12CreateDevice)(adapter, feature_level, IID_PPV_ARGS(&device));
  DEBUG_ASSERT(SUCCEEDED(hr) && device, DebugAssert{});
  if (FAILED(hr)) {
    spdlog::critical("D3D12CreateDevice failed. {}", hr);
    exit(1);
  }
#ifndef NDEBUG
  if (IsDebuggerPresent()) {
    ID3D12InfoQueue* info_queue = nullptr;
    if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&info_queue)))) {
      info_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, true);
      info_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, true);
      info_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, true);
#if 0
      D3D12_MESSAGE_SEVERITY suppressed_severity[] = {
        D3D12_MESSAGE_SEVERITY_INFO,
      };
      // D3D12_MESSAGE_ID supressed_id[] = {
      // };
      D3D12_INFO_QUEUE_FILTER queue_filter = {};
      queue_filter.DenyList.NumSeverities = _countof(suppressed_severity);
      queue_filter.DenyList.pSeverityList = suppressed_severity;
      // queue_filter.DenyList.NumIDs = _countof(supressed_id);
      // queue_filter.DenyList.pIDList = supressed_id;
      info_queue->PushStorageFilter(&queue_filter);
#endif
      info_queue->Release();
    }
  }
#endif
  {
    D3D12_FEATURE_DATA_D3D12_OPTIONS5 options{};
    if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &options, sizeof(options)))) {
      spdlog::info("ray tracing tier:{} (1.0:{} 1.1:{})", fmt::underlying(options.RaytracingTier), fmt::underlying(D3D12_RAYTRACING_TIER_1_0), fmt::underlying(D3D12_RAYTRACING_TIER_1_1));
    }
  }
  {
    D3D12_FEATURE_DATA_D3D12_OPTIONS7 options{};
    if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS7, &options, sizeof(options)))) {
      spdlog::info("mesh shader tier:{} (1:{})", fmt::underlying(options.MeshShaderTier), fmt::underlying(D3D12_MESH_SHADER_TIER_1));
    }
  }
  {
    D3D12_FEATURE_DATA_D3D12_OPTIONS12 options{};
    if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS12, &options, sizeof(options)))) {
      spdlog::info("enhanced barriers:{}", options.EnhancedBarriersSupported);
    }
  }
  device->SetName(L"device");
  return device;
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
auto DisableAltEnterFullscreen(HWND hwnd, IDXGIFactory7* factory) {
  const auto hr = factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);
  if (FAILED(hr)) {
    spdlog::warn("DisableAltEnterFullscreen failed. {}", hr);
  }
}
auto IsVariableRefreshRateSupported(IDXGIFactory7* factory) {
  BOOL result = false;
  const auto hr = factory->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &result, sizeof(result));
  if (FAILED(hr)) {
    spdlog::warn("CheckFeatureSupport(tearing) failed. {}", hr);
  }
  return static_cast<bool>(result);
}
auto CreateSwapchain(IDXGIFactory7* factory, ID3D12CommandQueue* command_queue, HWND hwnd, const DXGI_FORMAT format, const uint32_t backbuffer_num) {
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
#if 0
  // TODO
  hr = swapchain_->SetMaximumFrameLatency(frame_buffer_num);
  DEBUG_ASSERT(SUCCEEDED(hr), DebugAssert{});
  frame_latency_waitable_object_ = swapchain_->GetFrameLatencyWaitableObject();
  // get swapchain params
  {
    DXGI_SWAP_CHAIN_DESC1 desc = {};
    auto hr = swapchain_->GetDesc1(&desc);
    if (FAILED(hr)) {
      logwarn("swapchain_->GetDesc1 failed. {}", hr);
    } else {
      width_ = desc.Width;
      height_ = desc.Height;
      format_ = desc.Format;
      swapchain_buffer_num_ = desc.BufferCount;
    }
  }
  // get swapchain resource buffers for rtv
  {
    resources_ = AllocateArraySystem<ID3D12Resource*>(swapchain_buffer_num_);
    for (uint32_t i = 0; i < swapchain_buffer_num_; i++) {
      ID3D12Resource* resource = nullptr;
      auto hr = swapchain_->GetBuffer(i, IID_PPV_ARGS(&resource));
      if (FAILED(hr)) {
        spdlog::error("swapchain_->GetBuffer failed. {} {}", i, hr);
        assert(false && "swapchain_->GetBuffer failed.");
        for (uint32_t j = 0; j < i; j++) {
          resources_[i]->Release();
        }
        for (uint32_t j = 0; j < swapchain_buffer_num_; j++) {
          resources_[i] = nullptr;
        }
        return false;
      }
      resources_[i] = resource;
      SetD3d12Name(resources_[i], "swapchain" + std::to_string(i));
    }
  }
  // prepare rtv
  {
    D3D12_DESCRIPTOR_HEAP_DESC descriptor_heap_desc {
      .Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
      .NumDescriptors = swapchain_buffer_num_,
      .Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE,
      .NodeMask = 0
    };
    auto hr = device->CreateDescriptorHeap(&descriptor_heap_desc, IID_PPV_ARGS(&descriptor_heap_));
    if (FAILED(hr)) {
      spdlog::error("swapchain CreateDescriptorHeap failed. {} {}", hr, swapchain_buffer_num_);
      assert(false && "swapchain CreateDescriptorHeap failed.");
      return false;
    }
    descriptor_heap_->SetName(L"swapchain descriptor heap");
    const D3D12_RENDER_TARGET_VIEW_DESC rtv_desc = {
      .Format = format_,
      .ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D,
      .Texture2D = {},
    };
    auto rtv_handle = descriptor_heap_->GetCPUDescriptorHandleForHeapStart();
    auto rtv_step_size = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    cpu_handles_rtv_ = AllocateArraySystem<D3D12_CPU_DESCRIPTOR_HANDLE>(swapchain_buffer_num_);
    for (uint32_t i = 0; i < swapchain_buffer_num_; i++) {
      device->CreateRenderTargetView(resources_[i], &rtv_desc, rtv_handle);
      cpu_handles_rtv_[i] = rtv_handle;
      rtv_handle.ptr += rtv_step_size;
    }
  }
#endif
}
auto GetDxgiFormat(const char* format) {
  if (strcmp(format, "R8G8B8A8_UNORM") == 0) {
    return DXGI_FORMAT_R8G8B8A8_UNORM;
  }
  if (strcmp(format, "R8G8B8A8_UNORM_SRGB") == 0) {
    return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
  }
  if (strcmp(format, "B8G8R8A8_UNORM") == 0) {
    return DXGI_FORMAT_B8G8R8A8_UNORM;
  }
  if (strcmp(format, "B8G8R8A8_UNORM_SRGB") == 0) {
    return DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
  }
  if (strcmp(format, "R16G16B16A16_FLOAT") == 0) {
    return DXGI_FORMAT_R16G16B16A16_FLOAT;
  }
  if (strcmp(format, "R10G10B10A2_UNORM") == 0) {
    return DXGI_FORMAT_R10G10B10A2_UNORM;
  }
  if (strcmp(format, "R10G10B10_XR_BIAS_A2_UNORM") == 0) {
    return DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM;
  }
  return DXGI_FORMAT_R8G8B8A8_UNORM;
}
auto CreateDescriptorHeap(D3d12Device* device, const D3D12_DESCRIPTOR_HEAP_TYPE descriptor_heap_type, const uint32_t descriptor_heap_num, const D3D12_DESCRIPTOR_HEAP_FLAGS descriptor_heap_flag = D3D12_DESCRIPTOR_HEAP_FLAG_NONE) {
  ID3D12DescriptorHeap* descriptor_heap{};
  const D3D12_DESCRIPTOR_HEAP_DESC desc = {
    .Type = descriptor_heap_type,
    .NumDescriptors = descriptor_heap_num,
    .Flags = descriptor_heap_flag,
  };
  auto hr = device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&descriptor_heap));
  DEBUG_ASSERT(SUCCEEDED(hr), DebugAssert{});
  return descriptor_heap;
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
}
TEST_CASE("imgui") {
  using namespace boke;
  const uint32_t main_buffer_size_in_bytes = 16 * 1024;
  std::byte main_buffer[main_buffer_size_in_bytes];
  auto allocator_data = GetAllocatorData(main_buffer, main_buffer_size_in_bytes);
  auto json = GetJson("tests/config.json", allocator_data);
  const uint32_t frame_buffer_num = json["frame_buffer_num"].GetUint();
  auto window_info = CreateWin32Window(json, allocator_data);
  auto gfx_libraries = LoadGfxLibraries();
  auto dxgi = InitDxgi<AdapterType::kHighPerformance>(gfx_libraries.dxgi_library);
  auto device = CreateDevice(gfx_libraries.d3d12_library, dxgi.adapter);
  REQUIRE_NE(device, nullptr);
  auto command_queue = CreateCommandQueue(device, D3D12_COMMAND_LIST_TYPE_DIRECT, D3D12_COMMAND_QUEUE_PRIORITY_NORMAL, D3D12_COMMAND_QUEUE_FLAG_NONE);
  REQUIRE_NE(command_queue, nullptr);
  auto fence = CreateFence(device);
  REQUIRE_NE(fence, nullptr);
  auto fence_event = CreateEvent(nullptr, false, false, nullptr);
  REQUIRE_NE(fence_event, nullptr);
  auto fence_signal_val_list = AllocateArray<uint64_t>(frame_buffer_num, allocator_data);
  std::fill(fence_signal_val_list, fence_signal_val_list + frame_buffer_num, 0);
  uint64_t fence_signal_val = 0;
  const auto swapchain_format = GetDxgiFormat(json["swapchain"]["format"].GetString());
  const auto swapchain_backbuffer_num = json["swapchain"]["num"].GetInt();
  auto swapchain = CreateSwapchain(dxgi.factory, command_queue, window_info.hwnd, swapchain_format, swapchain_backbuffer_num);
  REQUIRE_NE(swapchain, nullptr);
  auto swapchain_latency_object = swapchain->GetFrameLatencyWaitableObject();
  auto descriptor_heap = CreateDescriptorHeap(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, json["descriptor_handles"]["shader_visible_buffer_num"].GetUint(), D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE);
  REQUIRE_NE(descriptor_heap, nullptr);
  const auto descriptor_handle_heap_info_full = GetDescriptorHandleHeapInfoFull(descriptor_heap, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, device);
  const auto [imgui_font_handle_cpu, imgui_font_handle_gpu] = GetDescriptorHandle(0, descriptor_handle_heap_info_full);
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
  ImGui_ImplWin32_Init(window_info.hwnd);
  ImGui_ImplDX12_Init(device,
                      swapchain_backbuffer_num,
                      swapchain_format,
                      descriptor_heap,
                      imgui_font_handle_cpu,
                      imgui_font_handle_gpu);
  const uint32_t max_loop_num = json["max_loop_num"].GetUint();
  for (uint32_t frame_count = 0; frame_count < max_loop_num; frame_count++) {
    if (ProcessWindowMessages() == WindowMessage::kQuit) { break; }
    const auto frame_index = frame_count % frame_buffer_num;
#if 0
    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    ImGui::ShowDemoWindow();
    ImGui::Render();
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), command_list);
#endif
    if (!WaitForSwapchain(swapchain_latency_object)) { break; }
    WaitForFence(fence_event, fence, fence_signal_val_list[frame_index]);
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
  swapchain->Release();
  fence->Release();
  command_queue->Release();
  device->Release();
  TermDxgi(dxgi);
  ReleaseGfxLibraries(gfx_libraries);
  ReleaseWin32Window(window_info, allocator_data);
}
