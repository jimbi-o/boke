#pragma once
#include "d3d12_name_alias.h"
namespace boke {
void InitImgui(HWND hwnd, D3d12Device* device, const uint32_t swapchain_buffer_num, const DXGI_FORMAT swapchain_format,
               ID3D12DescriptorHeap* shader_visible_descriptor_heap, const D3D12_CPU_DESCRIPTOR_HANDLE& imgui_font_cpu_handle, const D3D12_GPU_DESCRIPTOR_HANDLE imgui_font_gpu_handle);
void TermImgui();
}
