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
#include "d3d12_util.h"
#include "material.h"
#include "resources.h"
namespace {
using namespace boke;
std::pair<StrHash, ID3D12RootSignature*> LoadRootsig(const rapidjson::Value& rootsig_json, D3d12Device* device, StrHashMap<ID3D12RootSignature*>& rootsig_list) {
  const auto filename = rootsig_json.GetString();
  const auto rootsig_id = GetStrHash(filename);
  if (rootsig_list.contains(rootsig_id)) { return {rootsig_id, rootsig_list[rootsig_id]}; }
  uint32_t len = 0;
  const auto buffer = LoadFileToBuffer(filename, &len);
  DEBUG_ASSERT(buffer != nullptr, DebugAssert{});
  DEBUG_ASSERT(len > 0, DebugAssert{});
  ID3D12RootSignature* rootsig = nullptr;
  const auto hr = device->CreateRootSignature(0, buffer, len, IID_PPV_ARGS(&rootsig));
  DEBUG_ASSERT(SUCCEEDED(hr), DebugAssert{});
  rootsig_list[rootsig_id] = rootsig;
  Deallocate(buffer);
  SetD3d12Name(rootsig, filename);
  return {rootsig_id, rootsig};
}
auto LoadShaderObjectList(const rapidjson::Value& json, CD3DX12_PIPELINE_STATE_STREAM5_PARSE_HELPER& stream) {
  for (auto& shader : json.GetArray()) {
    uint32_t len = 0;
    const auto buffer = LoadFileToBuffer(shader["filename"].GetString(), &len);
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
auto DeallocateShaderBytecode(D3D12_SHADER_BYTECODE& bytecode) {
  if (bytecode.pShaderBytecode) {
    Deallocate(const_cast<void*>(bytecode.pShaderBytecode));
  }
}
auto CleanupShaderObject(CD3DX12_PIPELINE_STATE_STREAM5_PARSE_HELPER& stream) {
  DeallocateShaderBytecode(stream.PipelineStream.PS);
  DeallocateShaderBytecode(stream.PipelineStream.CS);
  DeallocateShaderBytecode(stream.PipelineStream.AS);
  DeallocateShaderBytecode(stream.PipelineStream.MS);
}
auto SetRtvFormat(const rapidjson::Value& rtv_json, CD3DX12_PIPELINE_STATE_STREAM5_PARSE_HELPER& stream) {
  D3D12_RT_FORMAT_ARRAY array{};
  array.NumRenderTargets = rtv_json.Size();
  for (uint32_t i = 0; i < array.NumRenderTargets; i++) {
    array.RTFormats[i] = GetDxgiFormat(rtv_json[i].GetString());
  }
  stream.RTVFormatsCb(array);
}
auto CreatePsoDesc(const rapidjson::Value& json, ID3D12RootSignature* rootsig) {
  CD3DX12_PIPELINE_STATE_STREAM5_PARSE_HELPER stream;
  stream.RootSignatureCb(rootsig);
  LoadShaderObjectList(json["shader_list"], stream);
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
void CreateMaterialSet(const rapidjson::Value& json, D3d12Device* device, MaterialSet& material_set) {
  for (const auto& material : json.GetArray()) {
    auto [rootsig_id, rootsig] = LoadRootsig(material["rootsig"], device, *material_set.rootsig_list);
    auto stream = CreatePsoDesc(material, rootsig);
    auto pso = CreatePso(device, stream);
    CleanupShaderObject(stream);
    SetD3d12Name(pso, material["name"].GetString());
    const auto material_id = GetStrHash(material["name"].GetString());
    material_set.pso_list->insert(material_id, pso);
    material_set.material_rootsig_map->insert(material_id, rootsig_id);
  }
}
void ReleaseMaterialSet(MaterialSet& material_set) {
  material_set.rootsig_list->iterate([](const StrHash, ID3D12RootSignature** rootsig) { (*rootsig)->Release(); });
  material_set.pso_list->iterate([](const StrHash, ID3D12PipelineState** pso) { (*pso)->Release(); });
  material_set.rootsig_list->~StrHashMap<ID3D12RootSignature*>();
  material_set.pso_list->~StrHashMap<ID3D12PipelineState*>();
  material_set.material_rootsig_map->~StrHashMap<StrHash>();
}
ID3D12RootSignature* GetRootsig(const MaterialSet& material_set, const StrHash material_id) {
  const auto& rootsig_id = (*material_set.material_rootsig_map)[material_id];
  return (*material_set.rootsig_list)[rootsig_id];
}
ID3D12PipelineState* GetPso(const MaterialSet& material_set, const StrHash material_id) {
  return (*material_set.pso_list)[material_id];
}
} // namespace boke
#include "doctest/doctest.h"
TEST_CASE("create rootsig&pso") {
  using namespace boke;
  // allocator
  const uint32_t main_buffer_size_in_bytes = 1024 * 1024;
  auto main_buffer = new std::byte[main_buffer_size_in_bytes];
  InitAllocator(main_buffer, main_buffer_size_in_bytes);
  // core units
  auto gfx_libraries = LoadGfxLibraries();
  auto dxgi = InitDxgi(gfx_libraries.dxgi_library, AdapterType::kHighPerformance);
  auto device = CreateDevice(gfx_libraries.d3d12_library, dxgi.adapter);
  // rootsig container
  StrHashMap<ID3D12RootSignature*> rootsig_list;
  // parse json
  const auto json = GetJson("tests/test-material-list.json");
  for (const auto& material : json.GetArray()) {
    auto rootsig = LoadRootsig(material["rootsig"], device, rootsig_list);
    auto stream = CreatePsoDesc(material, rootsig.second);
    auto pso = CreatePso(device, stream);
    CleanupShaderObject(stream);
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
