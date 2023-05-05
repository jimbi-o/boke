#pragma once
namespace boke {
struct RenderPassInfo;
struct Size2d {
  uint32_t width{};
  uint32_t height{};
};
enum class ResourceCreationType : uint8_t {
  kNone,
  kRtv,
  kDsv,
};
struct ResourceInfo {
  ResourceCreationType creation_type{};
  D3D12_RESOURCE_FLAGS flags{};
  DXGI_FORMAT format{};
  Size2d size{};
  bool pingpong{};
};
DXGI_FORMAT GetDxgiFormat(const char* format);
StrHash GetPinpongResourceId(const StrHash id, const uint32_t index);
void ConfigureResourceInfo(const uint32_t render_pass_info_len, RenderPassInfo* render_pass_info, const rapidjson::Value& resource_options, StrHashMap<ResourceInfo>& resource_info);
StrHash GetResourceIdPingpongRead(const StrHash id, const StrHashMap<uint32_t>& pingpong_current_write_index);
StrHash GetResourceIdPingpongWrite(const StrHash id, const StrHashMap<uint32_t>& pingpong_current_write_index);
}
