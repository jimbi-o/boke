#include "core.h"
#include "dxgi1_6.h"
#ifndef NDEBUG
#include "dxgidebug.h"
#endif
#ifndef NDEBUG
#include "d3d12sdklayers.h"
#endif
#include "boke/debug_assert.h"
#define CALL_DLL_FUNCTION(library, function) reinterpret_cast<decltype(&function)>(GetProcAddress(library, #function))
#define LOAD_DLL_FUNCTION(library, function) decltype(&function) function = reinterpret_cast<decltype(function)>(GetProcAddress(library, #function))
#define ENABLE_GPU_VALIDATION
namespace boke {
GfxLibraries LoadGfxLibraries() {
  auto dxgi_library = LoadLibrary("Dxgi.dll");
  DEBUG_ASSERT(dxgi_library, DebugAssert());
  auto d3d12_library = LoadLibrary("D3D12.dll");
  DEBUG_ASSERT(d3d12_library, DebugAssert{});
  return GfxLibraries {
    .dxgi_library = dxgi_library,
    .d3d12_library = d3d12_library,
  };
}
void ReleaseGfxLibraries(GfxLibraries& libraries) {
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
DxgiCore InitDxgi(HMODULE library, const AdapterType adapter_type) {
  SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  DxgiFactory* factory = nullptr;
  auto hr = CALL_DLL_FUNCTION(library, CreateDXGIFactory2)(0, IID_PPV_ARGS(&factory));
  DEBUG_ASSERT(SUCCEEDED(hr) && factory, DebugAssert{});
  DxgiAdapter* adapter = nullptr;
  switch (adapter_type) {
    case AdapterType::kWarp: {
      hr = factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter));
      break;
    }
    default: {
      hr = factory->EnumAdapterByGpuPreference(0, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter));
      break;
    }
  }
  DEBUG_ASSERT(SUCCEEDED(hr) && adapter, DebugAssert{});
  {
    DXGI_ADAPTER_DESC1 desc;
    adapter->GetDesc1(&desc);
    if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
      spdlog::info("DxgiAdapter is software");
    }
  }
  return DxgiCore {
    .factory = factory,
    .adapter = adapter,
  };
}
void TermDxgi(DxgiCore& dxgi) {
  auto refval = dxgi.adapter->Release();
  if (refval != 0UL) {
    spdlog::error("adapter reference left. {}", refval);
  }
  refval = dxgi.factory->Release();
  if (refval != 0UL) {
    spdlog::error("factory reference left. {}", refval);
  }
}
D3d12Device* CreateDevice(HMODULE library, DxgiAdapter* adapter) {
#if !defined(NDEBUG) && defined(ENABLE_GPU_VALIDATION)
  if (IsDebuggerPresent()) {
    ID3D12Debug* debug_interface = nullptr;
    if (SUCCEEDED(CALL_DLL_FUNCTION(library, D3D12GetDebugInterface)(IID_PPV_ARGS(&debug_interface)))) {
      debug_interface->EnableDebugLayer();
      spdlog::info("EnableDebugLayer");
      ID3D12Debug6* debug_interface6 = nullptr;
      if (SUCCEEDED(debug_interface->QueryInterface(IID_PPV_ARGS(&debug_interface6)))) {
        debug_interface6->SetEnableGPUBasedValidation(true);
        debug_interface6->SetForceLegacyBarrierValidation(false);
        spdlog::info("SetEnableGPUBasedValidation");
        debug_interface6->Release();
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
} // namespace boke
