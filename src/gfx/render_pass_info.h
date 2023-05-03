#pragma once
namespace boke {
struct RenderPassInfo {
  StrHash queue{kEmptyStr};
  StrHash* srv{};
  uint32_t srv_num{};
  StrHash* rtv{};
  uint32_t rtv_num{};
  StrHash  dsv{kEmptyStr};
  StrHash  present{kEmptyStr};
};
}
