#pragma once
namespace boke {
struct MaterialSet {
  StrHashMap<ID3D12RootSignature*>* rootsig_list{};
  StrHashMap<ID3D12PipelineState*>* pso_list{};
};
void CreateMaterialSet(const rapidjson::Value& json, D3d12Device* device, AllocatorData* allocator_data, MaterialSet& material_set);
}
