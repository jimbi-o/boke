#include "boke/util.h"
#include "spdlog/spdlog.h"
int main(int argc, char* argv[]) {
  using namespace boke;
  const auto val = Align(3U, 8U);
  spdlog::info("hello {}", val);
  return 0;
}
