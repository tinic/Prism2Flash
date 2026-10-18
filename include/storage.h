/* SPDX-License-Identifier: GPL-2.0-only */
#pragma once
#include <algorithm>
#include <cstdint>
#include <memory>
#include <new>
#include <span>
#include <type_traits>
#include <utility>

namespace p2 {
// Explicit allocation failure for the exception-free Amiga runtime. Elements
// retain normal C++ construction, destruction and move semantics.
template <typename T, std::size_t Limit> class BoundedVector {
  static_assert(Limit > 0);
  static_assert(std::is_nothrow_default_constructible_v<T>);
  static_assert(std::is_nothrow_move_assignable_v<T>);

public:
  BoundedVector() = default;
  BoundedVector(const BoundedVector &) = delete;
  BoundedVector &operator=(const BoundedVector &) = delete;
  BoundedVector(BoundedVector &&other) noexcept
      : data_(std::move(other.data_)), size_(std::exchange(other.size_, 0)),
        capacity_(std::exchange(other.capacity_, 0)) {}
  BoundedVector &operator=(BoundedVector &&other) noexcept {
    if (this != &other) {
      data_ = std::move(other.data_);
      size_ = std::exchange(other.size_, 0);
      capacity_ = std::exchange(other.capacity_, 0);
    }
    return *this;
  }
  [[nodiscard]] bool push_back(T value) {
    if (size_ == Limit)
      return false;
    if (size_ == capacity_) {
      const auto capacity = std::min(Limit, capacity_ ? capacity_ * 2 : 16);
      auto next = std::unique_ptr<T[]>{new (std::nothrow) T[capacity]{}};
      if (!next)
        return false;
      if (size_)
        std::move(begin(), end(), next.get());
      data_ = std::move(next);
      capacity_ = capacity;
    }
    data_[size_++] = std::move(value);
    return true;
  }
  void truncate(std::size_t size) {
    while (size_ > size)
      data_[--size_] = T{};
  }
  [[nodiscard]] std::size_t size() const { return size_; }
  [[nodiscard]] bool empty() const { return size_ == 0; }
  T *begin() { return data_.get(); }
  T *end() { return size_ ? data_.get() + size_ : data_.get(); }
  const T *begin() const { return data_.get(); }
  const T *end() const { return size_ ? data_.get() + size_ : data_.get(); }
  T &operator[](std::size_t i) { return data_[i]; }
  const T &operator[](std::size_t i) const { return data_[i]; }
  [[nodiscard]] std::span<const T> view() const { return {data_.get(), size_}; }

private:
  std::unique_ptr<T[]> data_;
  std::size_t size_ = 0;
  std::size_t capacity_ = 0;
};

class ByteBuffer {
public:
  ByteBuffer() = default;
  ByteBuffer(const ByteBuffer &) = delete;
  ByteBuffer &operator=(const ByteBuffer &) = delete;
  ByteBuffer(ByteBuffer &&other) noexcept
      : data_(std::move(other.data_)), size_(std::exchange(other.size_, 0)) {}
  ByteBuffer &operator=(ByteBuffer &&other) noexcept {
    if (this != &other) {
      data_ = std::move(other.data_);
      size_ = std::exchange(other.size_, 0);
    }
    return *this;
  }
  [[nodiscard]] static ByteBuffer allocate(std::size_t size) {
    ByteBuffer buffer;
    buffer.data_.reset(new (std::nothrow) std::uint8_t[size]);
    if (buffer.data_)
      buffer.size_ = size;
    return buffer;
  }
  explicit operator bool() const { return data_ != nullptr; }
  [[nodiscard]] std::span<std::uint8_t> bytes() { return {data_.get(), size_}; }
  [[nodiscard]] std::span<const std::uint8_t> bytes() const {
    return {data_.get(), size_};
  }
  [[nodiscard]] std::size_t size() const { return size_; }

private:
  std::unique_ptr<std::uint8_t[]> data_;
  std::size_t size_ = 0;
};
} // namespace p2
