#pragma once
namespace boke {
class LinearAllocator {
 public:
  explicit LinearAllocator(const std::byte* buffer, const uint32_t size_in_byte) : head_(reinterpret_cast<uintptr_t>(buffer)), size_in_byte_(size_in_byte), offset_in_byte_(0) {}
  ~LinearAllocator() {}
  LinearAllocator() = delete;
  LinearAllocator(const LinearAllocator&) = delete;
  LinearAllocator& operator=(const LinearAllocator&) = delete;
  void* Allocate(const uint32_t bytes, uint32_t alignment_in_bytes) {
    auto addr_aligned = Align(head_ + offset_in_byte_, alignment_in_bytes);
    offset_in_byte_ = GetUint32(addr_aligned - head_) + bytes;
    DEBUG_ASSERT(offset_in_byte_ <= size_in_byte_, DebugAssert{});
    return reinterpret_cast<void*>(addr_aligned);
  }
  constexpr auto GetOffset() const { return offset_in_byte_; }
  constexpr void Reset() { offset_in_byte_ = 0; }
  constexpr auto GetBufferSizeInByte() const { return size_in_byte_; }
  auto GetBuffer() const { return reinterpret_cast<std::byte*>(head_); }
 private:
  const std::uintptr_t head_;
  const uint32_t size_in_byte_;
  uint32_t offset_in_byte_;
};
}
