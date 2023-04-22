#include <Windows.h>
#include "boke/allocator.h"
#include "imgui.h"
#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_dx12.h"
#include "json.h"
#include "string_util.h"
#include "doctest/doctest.h"
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
auto DestroyWin32Window(WindowInfo& info, boke::AllocatorData* allocator_data) {
  using namespace boke;
  ::DestroyWindow(info.hwnd);
  ::UnregisterClassW(info.class_name, info.h_instance);
  Deallocate(info.class_name, allocator_data);
}
}
TEST_CASE("imgui") {
  using namespace boke;
  const uint32_t main_buffer_size_in_bytes = 16 * 1024;
  std::byte main_buffer[main_buffer_size_in_bytes];
  auto allocator_data = GetAllocatorData(main_buffer, main_buffer_size_in_bytes);
  auto json = GetJson("tests/config.json", allocator_data);
  auto window_info = CreateWin32Window(json, allocator_data);
  DestroyWin32Window(window_info, allocator_data);
}
