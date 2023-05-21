#pragma once
namespace boke {
struct MaterialSet {
  StrHashMap<StrHash>* material_rootsig_map{};
  StrHashMap<ID3D12RootSignature*>* rootsig_list{};
  StrHashMap<ID3D12PipelineState*>* pso_list{};
};
void CreateMaterialSet(const rapidjson::Value& json, D3d12Device* device, AllocatorData* allocator_data, MaterialSet& material_set);
void ReleaseMaterialSet(MaterialSet& material_set);
ID3D12RootSignature* GetRootsig(const MaterialSet&, const StrHash material_id);
ID3D12PipelineState* GetPso(const MaterialSet&, const StrHash material_id);
}
