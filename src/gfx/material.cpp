#include "directx/d3dx12_pipeline_state_stream.h"
#include "dxgi1_6.h"
#include "boke/allocator.h"
#include "boke/container.h"
#include "boke/debug_assert.h"
#include "boke/file.h"
#include "boke/str_hash.h"
#include "boke/util.h"
#include "core.h"
#include "json.h"
#include "material.h"
#include "resources.h"
namespace {
using namespace boke;
auto LoadRootsig(const rapidjson::Value& rootsig_json, D3d12Device* device, StrHashMap<ID3D12RootSignature*>& rootsig_list, AllocatorData* allocator_data) {
  const auto filename = rootsig_json["file"].GetString();
  const auto rootsig_id = GetStrHash(rootsig_json["name"].GetString());
  if (rootsig_list.contains(rootsig_id)) { return rootsig_list[rootsig_id]; }
  uint32_t len = 0;
  const auto buffer = LoadFileToBuffer(filename, allocator_data, &len);
  DEBUG_ASSERT(buffer != nullptr, DebugAssert{});
  DEBUG_ASSERT(len > 0, DebugAssert{});
  ID3D12RootSignature* rootsig = nullptr;
  const auto hr = device->CreateRootSignature(0, buffer, len, IID_PPV_ARGS(&rootsig));
  DEBUG_ASSERT(SUCCEEDED(hr), DebugAssert{});
  rootsig_list[rootsig_id] = rootsig;
  Deallocate(buffer, allocator_data);
  return rootsig;
}
auto LoadShaderObjectList(const rapidjson::Value& json, AllocatorData* allocator_data, CD3DX12_PIPELINE_STATE_STREAM5_PARSE_HELPER& stream) {
  for (auto& shader : json.GetArray()) {
    uint32_t len = 0;
    const auto buffer = LoadFileToBuffer(shader["filename"].GetString(), allocator_data, &len);
    DEBUG_ASSERT(buffer != nullptr, DebugAssert{});
    DEBUG_ASSERT(len > 0, DebugAssert{});
    D3D12_SHADER_BYTECODE shader_bytecode{
      .pShaderBytecode = buffer,
      .BytecodeLength = len,
    };
    const auto target = shader["target"].GetString();
    if (strcmp(target, "ps") == 0) {
      stream.PSCb(shader_bytecode);
      continue;
    }
    if (strcmp(target, "cs") == 0) {
      stream.CSCb(shader_bytecode);
      continue;
    }
    if (strcmp(target, "as") == 0) {
      stream.ASCb(shader_bytecode);
      continue;
    }
    if (strcmp(target, "ms") == 0) {
      stream.MSCb(shader_bytecode);
      continue;
    }
    DEBUG_ASSERT(false, DebugAssert{});
  }
}
auto DeallocateShaderBytecode(D3D12_SHADER_BYTECODE& bytecode, AllocatorData* allocator_data) {
  if (bytecode.pShaderBytecode) {
    Deallocate(const_cast<void*>(bytecode.pShaderBytecode), allocator_data);
  }
}
auto CleanupShaderObject(CD3DX12_PIPELINE_STATE_STREAM5_PARSE_HELPER& stream, AllocatorData* allocator_data) {
  DeallocateShaderBytecode(stream.PipelineStream.PS, allocator_data);
  DeallocateShaderBytecode(stream.PipelineStream.CS, allocator_data);
  DeallocateShaderBytecode(stream.PipelineStream.AS, allocator_data);
  DeallocateShaderBytecode(stream.PipelineStream.MS, allocator_data);
}
auto SetRtvFormat(const rapidjson::Value& rtv_json, CD3DX12_PIPELINE_STATE_STREAM5_PARSE_HELPER& stream) {
  D3D12_RT_FORMAT_ARRAY array{};
  array.NumRenderTargets = rtv_json.Size();
  for (uint32_t i = 0; i < array.NumRenderTargets; i++) {
    array.RTFormats[i] = GetDxgiFormat(rtv_json[i].GetString());
  }
  stream.RTVFormatsCb(array);
}
auto CreatePsoDesc(const rapidjson::Value& json, D3d12Device* device, StrHashMap<ID3D12RootSignature*>& rootsig_list, AllocatorData* allocator_data) {
  CD3DX12_PIPELINE_STATE_STREAM5_PARSE_HELPER stream;
  stream.RootSignatureCb(LoadRootsig(json["rootsig"], device, rootsig_list, allocator_data));
  LoadShaderObjectList(json["shader_list"], allocator_data, stream);
  if (json.HasMember("rtv")) {
    SetRtvFormat(json["rtv"], stream);
  }
  return stream;
}
auto CreatePso(D3d12Device* device, CD3DX12_PIPELINE_STATE_STREAM5_PARSE_HELPER& stream) {
  D3D12_PIPELINE_STATE_STREAM_DESC desc = {
    .SizeInBytes = sizeof(stream.PipelineStream),
    .pPipelineStateSubobjectStream = &stream.PipelineStream,
  };
  ID3D12PipelineState* pso = nullptr;
  const auto hr = device->CreatePipelineState(&desc, IID_PPV_ARGS(&pso));
  DEBUG_ASSERT(SUCCEEDED(hr), DebugAssert{});
  return pso;
}
} // namespace
namespace boke {
void CreateMaterialSet(const rapidjson::Value& json, D3d12Device* device, AllocatorData* allocator_data, MaterialSet& material_set) {
  for (const auto& material : json.GetArray()) {
    auto stream = CreatePsoDesc(material, device, *material_set.rootsig_list, allocator_data);
    auto pso = CreatePso(device, stream);
    CleanupShaderObject(stream, allocator_data);
    const auto material_id = GetStrHash(material["name"].GetString());
    material_set.pso_list->insert(material_id, pso);
  }
}
} // namespace boke
#include "doctest/doctest.h"
TEST_CASE("create rootsig&pso") {
  using namespace boke;
  // allocator
  const uint32_t main_buffer_size_in_bytes = 1024 * 1024;
  auto main_buffer = new std::byte[main_buffer_size_in_bytes];
  auto allocator_data = GetAllocatorData(main_buffer, main_buffer_size_in_bytes);
  // core units
  auto gfx_libraries = LoadGfxLibraries();
  auto dxgi = InitDxgi(gfx_libraries.dxgi_library, AdapterType::kHighPerformance);
  auto device = CreateDevice(gfx_libraries.d3d12_library, dxgi.adapter);
  // rootsig container
  StrHashMap<ID3D12RootSignature*> rootsig_list(GetAllocatorCallbacks(allocator_data));
  // parse json
  const auto json = GetJson("test/material-list.json", allocator_data);
  for (const auto& material : json.GetArray()) {
    auto stream = CreatePsoDesc(material, device, rootsig_list, allocator_data);
    auto pso = CreatePso(device, stream);
    CleanupShaderObject(stream, allocator_data);
    CHECK_NE(pso, nullptr);
    pso->Release();
  }
  // terminate
  rootsig_list.iterate([](const StrHash, ID3D12RootSignature** rootsig) {(*rootsig)->Release();});
  rootsig_list.~StrHashMap<ID3D12RootSignature*>();
  device->Release();
  TermDxgi(dxgi);
  ReleaseGfxLibraries(gfx_libraries);
  delete[] main_buffer;
}
TEST_CASE("create materials") {
}
