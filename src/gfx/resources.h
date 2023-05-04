#pragma once
namespace boke {
struct Size2d {
  uint32_t width{};
  uint32_t height{};
};
DXGI_FORMAT GetDxgiFormat(const char* format);
}
