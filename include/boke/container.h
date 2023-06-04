#pragma once
#include <stdint.h>
#include <string.h>
#include <utility>
#include "boke/allocator.h"
#include "boke/str_hash.h"
namespace boke {
template <typename T>
class ResizableArray final {
 public:
  ResizableArray();
  ResizableArray(const uint32_t initial_size, const uint32_t initial_capacity);
  ResizableArray(const uint32_t initial_capacity);
  ResizableArray(ResizableArray&&);
  ResizableArray& operator=(ResizableArray&&);
  ~ResizableArray();
  constexpr uint32_t size() const { return size_; }
  constexpr uint32_t capacity() const { return capacity_; }
  constexpr bool empty() const { return size() == 0; }
  void reserve(const uint32_t capacity);
  /**
   * reset size to zero.
   * destructor for T is not called.
   **/
  void clear() { size_ = 0; }
  /**
   * release allocated buffer which reduces size and capacity to zero.
   * destructor for T is not called.
   **/
  void release_allocated_buffer();
  void push_back(T);
  T* begin() { return head_; }
  const T* begin() const { return head_; }
  T* end() { return head_ + size_; }
  const T* end() const { return head_ + size_; }
  T& front() { return *head_; }
  const T& front() const { return *head_; }
  T& back() { return *(head_ + size_ - 1); }
  const T& back() const { return *(head_ + size_ - 1); }
  T& operator[](const uint32_t index) { return *(head_ + index); }
  const T& operator[](const uint32_t index) const { return *(head_ + index); }
 private:
  void change_capacity(const uint32_t new_capacity);
  uint32_t size_;
  uint32_t capacity_;
  T* head_;
  ResizableArray(const ResizableArray&) = delete;
  void operator=(const ResizableArray&) = delete;
};
/**
 * HashMap using open addressing.
 **/
template <typename T>
class StrHashMap final {
 public:
  using SimpleIteratorFunction = void (*)(const StrHash, T*);
  using ConstSimpleIteratorFunction = void (*)(const StrHash, const T*);
  template <typename U>
  using IteratorFunction = void (*)(U*, const StrHash, T*);
  template <typename U>
  using ConstIteratorFunction = void (*)(U*, const StrHash, const T*);
  StrHashMap();
  StrHashMap(const uint32_t initial_capacity);
  StrHashMap(StrHashMap&&);
  StrHashMap& operator=(StrHashMap&&);
  ~StrHashMap();
  constexpr uint32_t size() const { return size_; }
  constexpr uint32_t capacity() const { return capacity_; }
  constexpr bool empty() const { return size() == 0; }
  void reserve(const uint32_t capacity);
  /**
   * clear entries and reset size to zero.
   * destructor for T is not called.
   **/
  void clear();
  /**
   * release allocated buffer which reduces size and capacity to zero.
   * destructor for T is not called.
   **/
  void release_allocated_buffer();
  void insert(const StrHash, T);
  void erase(const StrHash);
  bool contains(const StrHash) const;
  T& operator[](const StrHash);
  const T& operator[](const StrHash) const;
  T* get(const StrHash);
  const T* get(const StrHash) const;
  void iterate(SimpleIteratorFunction&&);
  void iterate(ConstSimpleIteratorFunction&&) const;
  template <typename T> void iterate(IteratorFunction<T>&&, T*);
  template <typename T> void iterate(ConstIteratorFunction<T>&&, T*) const;
 private:
  uint32_t find_slot_index(const StrHash) const;
  bool check_load_factor_and_resize();
  void change_capacity(const uint32_t new_capacity);
  void insert_impl(const uint32_t, const StrHash, T value);
  bool* occupied_flags_{};
  StrHash* keys_{};
  T* values_{};
  uint32_t size_{};
  uint32_t capacity_{}; // always >0 for simple implementation.
  StrHashMap(const StrHashMap&) = delete;
  void operator=(const StrHashMap&) = delete;
};
template <typename T>
ResizableArray<T>::ResizableArray()
    : size_(0)
    , capacity_(0)
    , head_(nullptr)
{
}
template <typename T>
ResizableArray<T>::ResizableArray(const uint32_t initial_size, const uint32_t initial_capacity)
    : size_(initial_size)
    , capacity_(0)
    , head_(nullptr)
{
  change_capacity(initial_size > initial_capacity ? initial_size: initial_capacity);
}
template <typename T>
ResizableArray<T>::ResizableArray(const uint32_t initial_capacity)
    : size_()
    , capacity_(0)
    , head_(nullptr)
{
  change_capacity(initial_capacity);
}
template <typename T>
ResizableArray<T>::~ResizableArray() {
  release_allocated_buffer();
}
template <typename T>
ResizableArray<T>::ResizableArray(ResizableArray&& other)
    : size_(other.size_)
    , capacity_(other.capacity_)
    , head_(other.head_)
{
  other.size_ = 0;
  other.capacity_ = 0;
  other.head_ = nullptr;
}
template <typename T>
ResizableArray<T> & ResizableArray<T>::operator=(ResizableArray&& other) {
  if (this != &other) {
    if (head_) {
      Deallocate(head_);
    }
    size_ = other.size_;
    capacity_ = other.capacity_;
    head_ = other.head_;
    other.size_ = 0;
    other.capacity_ = 0;
    other.head_ = nullptr;
  }
  return *this;
}
template <typename T>
void ResizableArray<T>::release_allocated_buffer() {
  if (head_ != nullptr) {
    Deallocate(head_);
    head_ = nullptr;
  }
  size_ = 0;
  capacity_ = 0;
  head_ = nullptr;
}
template <typename T>
void ResizableArray<T>::reserve(const uint32_t capacity) {
  change_capacity(capacity);
}
template <typename T>
void ResizableArray<T>::push_back(T val) {
  auto index = size_;
  size_++;
  if (index >= capacity_) {
    change_capacity(size_ * 2);
  }
  head_[index] = val;
}
template <typename T>
void ResizableArray<T>::change_capacity(const uint32_t new_capacity) {
  if (new_capacity < capacity_) { return; }
  const auto prev_head = head_;
  if (size_ > new_capacity) {
    size_ = new_capacity;
  }
  capacity_ = new_capacity;
  if (capacity_ > 0) {
    head_ = AllocateArray<T>(new_capacity);
  } else {
    head_ = nullptr;
  }
  if (prev_head != nullptr) {
    memcpy(head_, prev_head, sizeof(T) * (size_));
    Deallocate(prev_head);
  }
}
bool IsPrimeNumber(const uint32_t);
uint32_t GetLargerOrEqualPrimeNumber(const uint32_t);
bool IsCloseToFull(const uint32_t load, const uint32_t capacity);
uint32_t Align(const uint32_t val, const uint32_t alignment);
template <typename T>
StrHashMap<T>::StrHashMap()
    : size_(0)
    , capacity_(0)
{
  change_capacity(GetLargerOrEqualPrimeNumber(capacity_));
}
template <typename T>
StrHashMap<T>::StrHashMap(const uint32_t initial_capacity)
    : size_(0)
    , capacity_(0)
{
  change_capacity(GetLargerOrEqualPrimeNumber(initial_capacity));
}
template <typename T>
StrHashMap<T>::StrHashMap(StrHashMap&& other)
    : occupied_flags_(other.occupied_flags_)
    , keys_(other.keys_)
    , values_(other.values_)
    , size_(other.size_)
    , capacity_(other.capacity_)
{
  other.occupied_flags_ = nullptr;
  other.keys_ = nullptr;
  other.values_ = nullptr;
  other.size_ = 0;
  other.capacity_ = 0;
}
template <typename T>
StrHashMap<T>& StrHashMap<T>::operator=(StrHashMap&& other)
{
  if (this != &other) {
    if (capacity_ > 0) {
      Deallocate(occupied_flags_);
      Deallocate(keys_);
      Deallocate(values_);
    }
    occupied_flags_ = other.occupied_flags_;
    keys_ = other.keys_;
    values_ = other.values_;
    size_ = other.size_;
    capacity_ = other.capacity_;
    other.occupied_flags_ = nullptr;
    other.keys_ = nullptr;
    other.values_ = nullptr;
    other.size_ = 0;
    other.capacity_ = 0;
  }
  return *this;
}
template <typename T>
StrHashMap<T>::~StrHashMap() {
  release_allocated_buffer();
}
template <typename T>
void StrHashMap<T>::reserve(const uint32_t capacity) {
  change_capacity(GetLargerOrEqualPrimeNumber(capacity));
}
template <typename T>
void StrHashMap<T>::clear() {
  if (capacity_ > 0) {
    memset(occupied_flags_, 0, sizeof(occupied_flags_[0]) * capacity_);
  }
  size_ = 0;
}
template <typename T>
void StrHashMap<T>::release_allocated_buffer() {
  if (capacity_ > 0) {
    Deallocate(occupied_flags_);
    Deallocate(keys_);
    Deallocate(values_);
    capacity_ = 0;
  }
  size_ = 0;
}
template <typename T>
void StrHashMap<T>::insert(const StrHash key, T value) {
  auto index = capacity_ > 0 ? find_slot_index(key) : ~0U;
  if (index != ~0U && occupied_flags_[index]) {
    values_[index] = value;
    return;
  }
  size_++;
  if (check_load_factor_and_resize()) {
    index = find_slot_index(key);
  }
  insert_impl(index, key, value);
}
template <typename T>
void StrHashMap<T>::insert_impl(const uint32_t index, const StrHash key, T value) {
  occupied_flags_[index] = true;
  keys_[index] = key;
  values_[index] = value;
}
template <typename T>
void StrHashMap<T>::erase(const StrHash key) {
  auto i = find_slot_index(key);
  if (!occupied_flags_[i]) { return; }
  occupied_flags_[i] = false;
  auto j = i;
  while (true) {
    j = (j + 1) % capacity_;
    if (!occupied_flags_[j]) { break; }
    auto k = keys_[j] % capacity_;
    if (i <= j) {
      if (i < k && k <= j) {
        continue;
      }
    } else {
      if (i < k || k <= j) {
        continue;
      }
    }
    occupied_flags_[i] = occupied_flags_[j];
    keys_[i] = keys_[j];
    values_[i] = values_[j];
    occupied_flags_[j] = false;
    i = j;
  }
  size_--;
}
template <typename T>
bool StrHashMap<T>::contains(const StrHash key) const {
  if (size_ == 0) { return false; }
  const auto index = find_slot_index(key);
  return occupied_flags_[index];
}
template <typename T>
T& StrHashMap<T>::operator[](const StrHash key) {
  if (!contains(key)) {
    insert(key, {});
  }
  const auto index = find_slot_index(key);
  return values_[index];
}
template <typename T>
const T& StrHashMap<T>::operator[](const StrHash key) const {
  const auto index = find_slot_index(key);
  return values_[index];
}
template <typename T>
T* StrHashMap<T>::get(const StrHash key) {
  if (size_ == 0) { return nullptr; }
  const auto index = find_slot_index(key);
  if (occupied_flags_[index]) {
    return &values_[index];
  }
  return nullptr;
}
template <typename T>
const T* StrHashMap<T>::get(const StrHash key) const {
  return const_cast<StrHashMap<T>*>(this)->get(key);
}
template <typename T>
void StrHashMap<T>::iterate(SimpleIteratorFunction&& f) {
  for (uint32_t i = 0; i < capacity_; i++) {
    if (!occupied_flags_[i]) { continue; }
    f(keys_[i], &values_[i]);
  }
}
template <typename T>
void StrHashMap<T>::iterate(ConstSimpleIteratorFunction&& f) const {
  for (uint32_t i = 0; i < capacity_; i++) {
    if (!occupied_flags_[i]) { continue; }
    f(keys_[i], &values_[i]);
  }
}
template <typename T>
template <typename U>
void StrHashMap<T>::iterate(IteratorFunction<U>&& f, U* entity) {
  for (uint32_t i = 0; i < capacity_; i++) {
    if (!occupied_flags_[i]) { continue; }
    f(entity, keys_[i], &values_[i]);
  }
}
template <typename T>
template <typename U>
void StrHashMap<T>::iterate(ConstIteratorFunction<U>&& f, U* entity) const {
  for (uint32_t i = 0; i < capacity_; i++) {
    if (!occupied_flags_[i]) { continue; }
    f(entity, keys_[i], &values_[i]);
  }
}
template <typename T>
uint32_t StrHashMap<T>::find_slot_index(const StrHash key) const {
  auto index = static_cast<uint32_t>(key % capacity_);
  while (occupied_flags_[index] && keys_[index] != key) {
    index = (index + 1) % capacity_;
  }
  return index;
}
template <typename T>
bool StrHashMap<T>::check_load_factor_and_resize() {
  if (!IsCloseToFull(size_, capacity_)) { return false; }
  change_capacity(GetLargerOrEqualPrimeNumber(capacity_ + 2));
  return true;
}
template <typename T>
void StrHashMap<T>::change_capacity(const uint32_t new_capacity) {
  if (capacity_ >= new_capacity) { return; }
  const auto prev_capacity = capacity_;
  const auto prev_size = size_;
  const auto prev_occupied_flags = occupied_flags_;
  const auto prev_keys = keys_;
  const auto prev_values = values_;
  capacity_ = new_capacity;
  {
    occupied_flags_ = AllocateArray<bool>(capacity_);
    keys_ = AllocateArray<StrHash>(capacity_);
    values_ = AllocateArray<T>(capacity_);
  }
  clear();
  for (uint32_t i = 0; i < prev_capacity; i++) {
    if (prev_occupied_flags[i]) {
      const auto index = find_slot_index(prev_keys[i]);
      insert_impl(index, prev_keys[i], prev_values[i]);
    }
  }
  size_ = prev_size;
  if (prev_capacity > 0) {
    Deallocate(prev_occupied_flags);
    Deallocate(prev_keys);
    Deallocate(prev_values);
  }
}
}
