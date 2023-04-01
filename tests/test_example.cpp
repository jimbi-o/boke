#include <doctest/doctest.h>
#include <spdlog/spdlog.h>
#include "boke/boke.h"
TEST_CASE("log") {
  spdlog::info("hello {}", "world");
  CHECK_UNARY(true);
}
