#include <doctest/doctest.h>
#include "boke/boke.h"
TEST_CASE("log") {
  spdlog::info("hello {}", "world");
  CHECK_UNARY(true);
}
TEST_CASE("sid") {
  namespace sid = foonathan::string_id;
  using namespace sid::literals;
  sid::default_database database;
  sid::string_id id("Test", database);
  switch (id.hash_code()) {
    case "Test"_id:
      CHECK_UNARY(true);
      break;
    case "NoTest"_id:
    default:
      CHECK_UNARY(false);
      break;
  }
}
