#include "imgui_util.h"
#include "imgui.h"
#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_dx12.h"
namespace boke {
void InitImgui(HWND hwnd, D3d12Device* device, const uint32_t swapchain_buffer_num, const DXGI_FORMAT swapchain_format,
               ID3D12DescriptorHeap* shader_visible_descriptor_heap, const D3D12_CPU_DESCRIPTOR_HANDLE& imgui_font_cpu_handle, const D3D12_GPU_DESCRIPTOR_HANDLE imgui_font_gpu_handle) {
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
  ImGui_ImplWin32_Init(hwnd);
  ImGui_ImplDX12_Init(device,
                      swapchain_buffer_num,
                      swapchain_format,
                      shader_visible_descriptor_heap,
                      imgui_font_cpu_handle,
                      imgui_font_gpu_handle);
  ImGui_ImplDX12_CreateDeviceObjects();
}
void TermImgui() {
  ImGui_ImplDX12_Shutdown();
  ImGui_ImplWin32_Shutdown();
  ImGui::DestroyContext();
}
}
