#include <Windows.h>
#include "dxgi1_6.h"
#ifndef NDEBUG
#include "dxgidebug.h"
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
  using namespace boke;
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
  using namespace boke;
  ::DestroyWindow(info.hwnd);
  ::UnregisterClassW(info.class_name, info.h_instance);
  Deallocate(info.class_name, allocator_data);
}
struct DxgiCore {
  HMODULE library{};
  IDXGIFactory7* factory{};
  IDXGIAdapter4* adapter{};
};
enum AdapterType : uint8_t { kHighPerformance, kWarp, };
template <AdapterType adapter_type>
auto InitDxgi() {
  using namespace boke;
  SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  auto library = LoadLibrary("Dxgi.dll");
  DEBUG_ASSERT(library, DebugAssert());
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
    .library = library,
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
#ifndef NDEBUG
  IDXGIDebug1* debug = nullptr;
  auto hr = CALL_DLL_FUNCTION(dxgi.library, DXGIGetDebugInterface1)(0, IID_PPV_ARGS(&debug));
  if (FAILED(hr)) {
    spdlog::warn("DXGIGetDebugInterface failed. {}", hr);
    return;
  }
  debug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_ALL);
  debug->Release();
#endif
  FreeLibrary(dxgi.library);
}
}
TEST_CASE("imgui") {
  using namespace boke;
  const uint32_t main_buffer_size_in_bytes = 16 * 1024;
  std::byte main_buffer[main_buffer_size_in_bytes];
  auto allocator_data = GetAllocatorData(main_buffer, main_buffer_size_in_bytes);
  auto json = GetJson("tests/config.json", allocator_data);
  auto window_info = CreateWin32Window(json, allocator_data);
  auto dxgi = InitDxgi<AdapterType::kHighPerformance>();
  TermDxgi(dxgi);
  ReleaseWin32Window(window_info, allocator_data);
}
