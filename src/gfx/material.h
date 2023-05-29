#pragma once
namespace boke {
struct MaterialSet;
MaterialSet* CreateMaterialSet(const rapidjson::Value& json, D3d12Device* device);
void ReleaseMaterialSet(MaterialSet* material_set);
ID3D12RootSignature* GetRootsig(const MaterialSet*, const StrHash material_id);
ID3D12PipelineState* GetPso(const MaterialSet*, const StrHash material_id);
}
