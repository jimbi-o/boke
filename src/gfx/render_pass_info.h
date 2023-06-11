#pragma once
namespace boke {
struct RenderPassInfo {
  StrHash queue{kEmptyStr};
  StrHash type{kEmptyStr};
  StrHash material{kEmptyStr};
  StrHash* cbv{};
  uint32_t cbv_num{};
  StrHash* srv{};
  uint32_t srv_num{};
  StrHash* rtv{};
  uint32_t rtv_num{};
  StrHash  dsv{kEmptyStr};
  StrHash  present{kEmptyStr};
  StrHash  material_id{kEmptyStr};
  uint8_t stencil_val{};
};
}
