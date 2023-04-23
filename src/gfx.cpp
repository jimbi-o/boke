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
}
TEST_CASE("imgui") {
  using namespace boke;
  const uint32_t main_buffer_size_in_bytes = 16 * 1024;
  std::byte main_buffer[main_buffer_size_in_bytes];
  auto allocator_data = GetAllocatorData(main_buffer, main_buffer_size_in_bytes);
  auto json = GetJson("tests/config.json", allocator_data);
  auto window_info = CreateWin32Window(json, allocator_data);
  auto gfx_libraries = LoadGfxLibraries();
  auto dxgi = InitDxgi<AdapterType::kHighPerformance>(gfx_libraries.dxgi_library);
  auto device = CreateDevice(gfx_libraries.d3d12_library, dxgi.adapter);
  REQUIRE_NE(device, nullptr);
  auto descriptor_heap = CreateDescriptorHeap(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, json["descriptor_handles"]["shader_visible_buffer_num"].GetUint(), D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE);
  REQUIRE_NE(descriptor_heap, nullptr);
  const auto descriptor_handle_heap_info_full = GetDescriptorHandleHeapInfoFull(descriptor_heap, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, device);
  const auto [imgui_font_handle_cpu, imgui_font_handle_gpu] = GetDescriptorHandle(0, descriptor_handle_heap_info_full);
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls
  ImGui_ImplWin32_Init(window_info.hwnd);
  ImGui_ImplDX12_Init(device,
                      json["swapchain"]["num"].GetInt(),
                      GetDxgiFormat(json["swapchain"]["format"].GetString()),
                      descriptor_heap,
                      imgui_font_handle_cpu,
                      imgui_font_handle_gpu);
  const uint32_t max_loop_num = json["max_loop_num"].GetUint();
  for (uint32_t frame_count = 0; frame_count < max_loop_num; frame_count++) {
  }
  descriptor_heap->Release();
  device->Release();
  TermDxgi(dxgi);
  ReleaseGfxLibraries(gfx_libraries);
  ReleaseWin32Window(window_info, allocator_data);
}
