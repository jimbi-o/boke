#include "boke/allocator.h"
#include "boke/container.h"
namespace boke {
bool IsPrimeNumber(const uint32_t n) {
  if (n <= 1) { return false; }
  for (uint32_t i = 2; i * i <= n; i++) {
    if (n % i == 0) { return false; }
  }
  return true;
}
uint32_t GetLargerOrEqualPrimeNumber(const uint32_t n) {
  if (IsPrimeNumber(n)) { return n; }
  if (n <= 2) { return 2; }
  auto p = n + 1 + n % 2; // odd number larger than n.
  while (!IsPrimeNumber(p)) {
    p += 2;
  }
  return p;
}
bool IsCloseToFull(const uint32_t load, const uint32_t capacity) {
  const float loadFactor = 0.65f;
  return static_cast<float>(load) / static_cast<float>(capacity) >= loadFactor;
}
} // namespace boke
#include "doctest/doctest.h"
TEST_CASE("resizable array") {
  using namespace boke;
  ResizableArray<uint32_t> resizable_array(0, 4);
  CHECK_UNARY(resizable_array.empty());
  CHECK_EQ(resizable_array.size(), 0);
  CHECK_EQ(resizable_array.capacity(), 4);
  resizable_array.push_back(0);
  CHECK_EQ(resizable_array.front(), 0);
  CHECK_EQ(resizable_array.back(), 0);
  resizable_array.push_back(1);
  resizable_array.push_back(2);
  CHECK_UNARY_FALSE(resizable_array.empty());
  CHECK_EQ(resizable_array.size(), 3);
  CHECK_EQ(resizable_array.capacity(), 4);
  CHECK_EQ(resizable_array[0], 0);
  CHECK_EQ(resizable_array[1], 1);
  CHECK_EQ(resizable_array[2], 2);
  CHECK_EQ(resizable_array.front(), 0);
  CHECK_EQ(resizable_array.back(), 2);
  auto it = resizable_array.begin();
  CHECK_EQ(*it, 0);
  it++;
  CHECK_EQ(*it, 1);
  it++;
  CHECK_EQ(*it, 2);
  it++;
  CHECK_EQ(it, resizable_array.end());
  resizable_array[0] = 99;
  CHECK_EQ(resizable_array[0], 99);
  CHECK_EQ(resizable_array[1], 1);
  CHECK_EQ(resizable_array[2], 2);
  resizable_array[1] = 18;
  CHECK_EQ(resizable_array[0], 99);
  CHECK_EQ(resizable_array[1], 18);
  CHECK_EQ(resizable_array[2], 2);
  resizable_array[2] = 21;
  CHECK_EQ(resizable_array[0], 99);
  CHECK_EQ(resizable_array[1], 18);
  CHECK_EQ(resizable_array[2], 21);
  resizable_array.push_back(3);
  CHECK_EQ(resizable_array.size(), 4);
  CHECK_EQ(resizable_array.capacity(), 4);
  CHECK_EQ(resizable_array[0], 99);
  CHECK_EQ(resizable_array[1], 18);
  CHECK_EQ(resizable_array[2], 21);
  CHECK_EQ(resizable_array[3], 3);
  resizable_array.push_back(4);
  CHECK_EQ(resizable_array.size(), 5);
  CHECK_GE(resizable_array.capacity(), 5);
  CHECK_EQ(resizable_array[0], 99);
  CHECK_EQ(resizable_array[1], 18);
  CHECK_EQ(resizable_array[2], 21);
  CHECK_EQ(resizable_array[3], 3);
  CHECK_EQ(resizable_array[4], 4);
  CHECK_EQ(resizable_array.front(), 99);
  CHECK_EQ(resizable_array.back(), 4);
  CHECK_NE(it, resizable_array.end());
  const auto capacity = resizable_array.capacity();
  resizable_array.clear();
  CHECK_UNARY(resizable_array.empty());
  CHECK_EQ(resizable_array.size(), 0);
  CHECK_EQ(resizable_array.capacity(), capacity);
  resizable_array.push_back(0);
  resizable_array.push_back(1);
  CHECK_EQ(resizable_array.size(), 2);
  CHECK_EQ(resizable_array[0], 0);
  CHECK_EQ(resizable_array[1], 1);
  resizable_array.release_allocated_buffer();
  CHECK_UNARY(resizable_array.empty());
  CHECK_EQ(resizable_array.size(), 0);
  CHECK_EQ(resizable_array.capacity(), 0);
  resizable_array.push_back(0);
  resizable_array.push_back(1);
  CHECK_EQ(resizable_array.size(), 2);
  CHECK_EQ(resizable_array[0], 0);
  CHECK_EQ(resizable_array[1], 1);
  CHECK_UNARY_FALSE(resizable_array.empty());
  CHECK_EQ(resizable_array.size(), 2);
  CHECK_GE(resizable_array.capacity(), 2);
}
TEST_CASE("empty resizable array") {
  using namespace boke;
  ResizableArray<uint32_t> resizable_array(0, 0);
  CHECK_UNARY(resizable_array.empty());
  CHECK_EQ(resizable_array.size(), 0);
  CHECK_EQ(resizable_array.capacity(), 0);
  resizable_array.push_back(0);
  CHECK_EQ(resizable_array.front(), 0);
  CHECK_EQ(resizable_array.back(), 0);
  CHECK_UNARY_FALSE(resizable_array.empty());
  CHECK_EQ(resizable_array.size(), 1);
  CHECK_GT(resizable_array.capacity(), 0);
}
TEST_CASE("move") {
  using namespace boke;
  ResizableArray<uint32_t> resizable_array_a;
  resizable_array_a.push_back(1);
  resizable_array_a.push_back(2);
  resizable_array_a.push_back(3);
  auto resizable_array_b = std::move(resizable_array_a);
  CHECK_EQ(resizable_array_a.size(), 0);
  CHECK_EQ(resizable_array_a.capacity(), 0);
  CHECK_EQ(resizable_array_a.begin(), nullptr);
  CHECK_EQ(resizable_array_b.size(), 3);
  CHECK_GE(resizable_array_b.capacity(), 3);
  CHECK_NE(resizable_array_b.begin(), nullptr);
  CHECK_EQ(resizable_array_b[0], 1);
  CHECK_EQ(resizable_array_b[1], 2);
  CHECK_EQ(resizable_array_b[2], 3);
  resizable_array_a = std::move(resizable_array_b);
  CHECK_EQ(resizable_array_b.size(), 0);
  CHECK_EQ(resizable_array_b.capacity(), 0);
  CHECK_EQ(resizable_array_b.begin(), nullptr);
  CHECK_EQ(resizable_array_a.size(), 3);
  CHECK_GE(resizable_array_a.capacity(), 3);
  CHECK_NE(resizable_array_a.begin(), nullptr);
  CHECK_EQ(resizable_array_a[0], 1);
  CHECK_EQ(resizable_array_a[1], 2);
  CHECK_EQ(resizable_array_a[2], 3);
  resizable_array_a = std::move(resizable_array_a);
  resizable_array_b = std::move(resizable_array_b);
  CHECK_EQ(resizable_array_b.size(), 0);
  CHECK_EQ(resizable_array_b.capacity(), 0);
  CHECK_EQ(resizable_array_b.begin(), nullptr);
  CHECK_EQ(resizable_array_a.size(), 3);
  CHECK_GE(resizable_array_a.capacity(), 3);
  CHECK_NE(resizable_array_a.begin(), nullptr);
  CHECK_EQ(resizable_array_a[0], 1);
  CHECK_EQ(resizable_array_a[1], 2);
  CHECK_EQ(resizable_array_a[2], 3);
  ResizableArray<uint32_t> resizable_array_c(std::move(resizable_array_a));
  CHECK_EQ(resizable_array_b.size(), 0);
  CHECK_EQ(resizable_array_b.capacity(), 0);
  CHECK_EQ(resizable_array_b.begin(), nullptr);
  CHECK_EQ(resizable_array_a.size(), 0);
  CHECK_EQ(resizable_array_a.capacity(), 0);
  CHECK_EQ(resizable_array_a.begin(), nullptr);
  CHECK_EQ(resizable_array_c.size(), 3);
  CHECK_GE(resizable_array_c.capacity(), 3);
  CHECK_NE(resizable_array_c.begin(), nullptr);
  CHECK_EQ(resizable_array_c[0], 1);
  CHECK_EQ(resizable_array_c[1], 2);
  CHECK_EQ(resizable_array_c[2], 3);
  ResizableArray<uint32_t> resizable_array_d;
  resizable_array_d.push_back(0);
  resizable_array_d = std::move(resizable_array_c);
  CHECK_EQ(resizable_array_d.size(), 3);
  CHECK_GE(resizable_array_d.capacity(), 3);
  CHECK_NE(resizable_array_d.begin(), nullptr);
  CHECK_EQ(resizable_array_d[0], 1);
  CHECK_EQ(resizable_array_d[1], 2);
  CHECK_EQ(resizable_array_d[2], 3);
  resizable_array_d.push_back(101);
  CHECK_EQ(resizable_array_d[3], 101);
  resizable_array_a.~ResizableArray();
  resizable_array_b.~ResizableArray();
  resizable_array_c.~ResizableArray();
  resizable_array_d.~ResizableArray();
}
