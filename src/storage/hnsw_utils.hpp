/*
 *
 * Copyright 2016 CUBRID Corporation
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 */

//
// hnsw_utils.hpp
//

#pragma once

#include <cstddef>
#include <cstring>
#include <cassert>
#include <type_traits>
#include <functional>
#include <optional>
#include <memory>

#include "scope_exit.hpp"

#define HNSW_UTIL_DEBUG 0
#define HNSW_UTIL_PRINT(fmt, ...) do { if (HNSW_UTIL_DEBUG) { fprintf (stdout, fmt, ##__VA_ARGS__); fflush (stdout); } } while (0)

template <typename T>
struct lightweight_span
{
  const T *data_;
  std::size_t size_;

  lightweight_span (const T *data, std::size_t size)
    : data_ (data), size_ (size) {}

  const T *data() const
  {
    return data_;
  }
  std::size_t size() const
  {
    return size_;
  }

  const T &operator[] (std::size_t i) const
  {
    return data_[i];
  }
  const T *begin() const
  {
    return data_;
  }
  const T *end() const
  {
    return data_ + size_;
  }
};

template <typename Cleanup = void (*)()>
class rw_span_cursor
{
  public:
    std::byte *p;
    std::byte *end;

    std::optional<scope_exit<Cleanup>> m_guard;

    explicit rw_span_cursor (std::byte *span, std::size_t size)
      : p (span), end (span + size) {}

    ~rw_span_cursor() = default;

    rw_span_cursor (const rw_span_cursor &) = delete;
    rw_span_cursor &operator= (const rw_span_cursor &) = delete;

    rw_span_cursor (rw_span_cursor &&other) noexcept
      : p (other.p), end (other.end), m_guard (std::move (other.m_guard))
    {
      other.p = nullptr;
      other.end = nullptr;
    }

    rw_span_cursor &operator= (rw_span_cursor &&other) noexcept
    {
      if (this != &other)
	{
	  p = other.p;
	  end = other.end;
	  m_guard = std::move (other.m_guard);

	  other.p = nullptr;
	  other.end = nullptr;
	}
      return *this;
    }

    template <typename F>
    void set_scope_exit (F &&f)
    {
      m_guard.emplace (std::forward<F> (f));
    }

    void close()
    {
      if (m_guard.has_value())
	{
	  m_guard->release();
	  m_guard.reset();
	}
    }

    template <typename T>
    T &write_then_ref (const T &v)
    {
      static_assert (std::is_trivially_copyable_v<T>, "T must be POD.");
      assert (p + sizeof (T) <= end);

      std::memcpy (p, &v, sizeof (T));
      T &ref = *reinterpret_cast<T *> (p);
      p += sizeof (T);
      return ref;
    }

    template <typename T>
    void write_array (const T *src, std::size_t count)
    {
      static_assert (std::is_trivially_copyable_v<T>, "T must be POD.");
      std::size_t bytes = sizeof (T) * count;
      assert (p + bytes <= end);

      std::memcpy (p, src, bytes);
      p += bytes;
    }

    template <typename T>
    T read_pod()
    {
      static_assert (std::is_trivially_copyable_v<T>, "T must be POD.");
      assert (p + sizeof (T) <= end);

      T v = *reinterpret_cast<const T *> (p);
      p += sizeof (T);
      return v;
    }

    template <typename T>
    lightweight_span<T> read_array (std::size_t count)
    {
      static_assert (std::is_trivially_copyable_v<T>, "T must be POD.");
      std::size_t bytes = sizeof (T) * count;
      assert (p + bytes <= end);

      T *arr = reinterpret_cast<T *> (p);
      p += bytes;
      return lightweight_span<T> (arr, count);
    }

    template <typename T>
    T *ref_pod()
    {
      static_assert (std::is_trivially_copyable_v<T>, "T must be POD.");
      assert (p + sizeof (T) <= end);

      return reinterpret_cast<T *> (p);
    }

    void skip (std::size_t bytes)
    {
      assert (p + bytes <= end);
      p += bytes;
    }

    std::size_t remaining () const
    {
      return static_cast<std::size_t> (end - p);
    }
};

/// @brief  Simply dereferencing misaligned pointers can be dangerous.
template <typename at> void misaligned_store (void *ptr, at v) noexcept
{
  static_assert (!std::is_reference<at>::value, "Can't store a reference");
  std::memcpy (ptr, &v, sizeof (at));
}

/// @brief  Simply dereferencing misaligned pointers can be dangerous.
template <typename at> at misaligned_load (void const *ptr) noexcept
{
  static_assert (!std::is_reference<at>::value, "Can't load a reference");
  at v;
  std::memcpy (&v, ptr, sizeof (at));
  return v;
}

template <typename at> class misaligned_ref_gt
{
    using element_t = at;
    using mutable_t = typename std::remove_const<element_t>::type;
    std::byte *ptr_;

  public:
    misaligned_ref_gt (std::byte *ptr) noexcept : ptr_ (ptr) {}
    operator mutable_t() const noexcept
    {
      return misaligned_load<mutable_t> (ptr_);
    }
    misaligned_ref_gt &operator= (mutable_t const &v) noexcept
    {
      misaligned_store<mutable_t> (ptr_, v);
      return *this;
    }

    void reset (std::byte *ptr) noexcept
    {
      ptr_ = ptr;
    }
    std::byte *ptr() const noexcept
    {
      return ptr_;
    }
};

/**
 *  @brief  A pointer to a misaligned memory location with a specific type.
 *          It is needed to avoid Undefined Behavior when dereferencing addresses
 *          indivisible by `sizeof(at)`.
 */
template <typename at> class misaligned_ptr_gt
{
    using element_t = at;
    using mutable_t = typename std::remove_const<element_t>::type;
    std::byte *ptr_;

  public:
    using iterator_category = std::random_access_iterator_tag;
    using value_type = element_t;
    using difference_type = std::ptrdiff_t;
    using pointer = misaligned_ptr_gt<element_t>;
    using reference = misaligned_ref_gt<element_t>;

    misaligned_ptr_gt (std::byte *ptr) noexcept : ptr_ (ptr) {}

    reference operator*() const noexcept
    {
      return {ptr_};
    }
    reference operator[] (std::size_t i) noexcept
    {
      return reference (ptr_ + i * sizeof (element_t));
    }
    value_type operator[] (std::size_t i) const noexcept
    {
      return misaligned_load<element_t> (ptr_ + i * sizeof (element_t));
    }

    misaligned_ptr_gt &operator++() noexcept
    {
      ptr_ += sizeof (element_t);
      return *this;
    }
    misaligned_ptr_gt &operator--() noexcept
    {
      ptr_ -= sizeof (element_t);
      return *this;
    }
    misaligned_ptr_gt operator++ (int) noexcept
    {
      misaligned_ptr_gt tmp = *this;
      ++ (*this);
      return tmp;
    }
    misaligned_ptr_gt operator-- (int) noexcept
    {
      misaligned_ptr_gt tmp = *this;
      -- (*this);
      return tmp;
    }
    misaligned_ptr_gt operator+ (difference_type d) const noexcept
    {
      return misaligned_ptr_gt (ptr_ + d * sizeof (element_t));
    }
    misaligned_ptr_gt operator- (difference_type d) const noexcept
    {
      return misaligned_ptr_gt (ptr_ - d * sizeof (element_t));
    }
    difference_type operator- (const misaligned_ptr_gt &other) const noexcept
    {
      return (ptr_ - other.ptr_) / sizeof (element_t);
    }

    misaligned_ptr_gt &operator+= (difference_type d) noexcept
    {
      ptr_ += d * sizeof (element_t);
      return *this;
    }
    misaligned_ptr_gt &operator-= (difference_type d) noexcept
    {
      ptr_ -= d * sizeof (element_t);
      return *this;
    }

    bool operator== (misaligned_ptr_gt const &other) const noexcept
    {
      return ptr_ == other.ptr_;
    }
    bool operator!= (misaligned_ptr_gt const &other) const noexcept
    {
      return ptr_ != other.ptr_;
    }
    bool operator< (misaligned_ptr_gt const &other) const noexcept
    {
      return ptr_ < other.ptr_;
    }
    bool operator<= (misaligned_ptr_gt const &other) const noexcept
    {
      return ptr_ <= other.ptr_;
    }
    bool operator> (misaligned_ptr_gt const &other) const noexcept
    {
      return ptr_ > other.ptr_;
    }
    bool operator>= (misaligned_ptr_gt const &other) const noexcept
    {
      return ptr_ >= other.ptr_;
    }
};

template <typename at, typename other_at = at> at exchange (at &obj, other_at&& new_value)
{
  at old_value = std::move (obj);
  obj = std::forward<other_at> (new_value);
  return old_value;
}

/**
 *  @brief  Similar to `std::priority_queue`, but allows raw access to underlying
 *          memory and always keeps the data sorted. Ideal for small collections
 *          under 128 elements.
 */
template <typename element_at,                                //
	  typename comparator_at = std::less<void>,           // <void> is needed before C++14.
	  typename allocator_at = std::allocator<element_at>> //
class sorted_buffer_gt
{
  public:
    using element_t = element_at;
    using comparator_t = comparator_at;
    using allocator_t = allocator_at;

    static_assert (std::is_trivially_destructible<element_t>(), "This heap is designed for trivial structs");
    static_assert (std::is_trivially_copy_constructible<element_t>(), "This heap is designed for trivial structs");

    using value_type = element_t;

  private:
    element_t *elements_;
    std::size_t size_;
    std::size_t capacity_;

  public:
    sorted_buffer_gt() noexcept : elements_ (nullptr), size_ (0), capacity_ (0) {}

    sorted_buffer_gt (sorted_buffer_gt &&other) noexcept
      : elements_ (exchange (other.elements_, nullptr)), size_ (exchange (other.size_, 0)),
	capacity_ (exchange (other.capacity_, 0)) {}

    sorted_buffer_gt &operator= (sorted_buffer_gt &&other) noexcept
    {
      std::swap (elements_, other.elements_);
      std::swap (size_, other.size_);
      std::swap (capacity_, other.capacity_);
      return *this;
    }

    sorted_buffer_gt (sorted_buffer_gt const &) = delete;
    sorted_buffer_gt &operator= (sorted_buffer_gt const &) = delete;

    ~sorted_buffer_gt() noexcept
    {
      reset();
    }

    void reset() noexcept
    {
      if (elements_)
	allocator_t{}.deallocate (elements_, capacity_);
      elements_ = nullptr;
      capacity_ = 0;
      size_ = 0;
    }

    inline bool empty() const noexcept
    {
      return !size_;
    }
    inline std::size_t size() const noexcept
    {
      return size_;
    }
    inline std::size_t capacity() const noexcept
    {
      return capacity_;
    }
    inline element_t const &top() const noexcept
    {
      return elements_[size_ - 1];
    }
    inline void clear() noexcept
    {
      size_ = 0;
    }

    static inline std::size_t ceil2 (std::size_t v) noexcept
    {
      v--;
      v |= v >> 1;
      v |= v >> 2;
      v |= v >> 4;
      v |= v >> 8;
      v |= v >> 16;
      v |= v >> 32;
      v++;
      return v;
    }

    bool reserve (std::size_t new_capacity) noexcept
    {
      if (new_capacity < capacity_)
	{
	  return true;
	}

      new_capacity = ceil2 (new_capacity);
      new_capacity = (std::max<std::size_t>) (new_capacity, (std::max<std::size_t>) (capacity_ * 2u, 16u));
      auto allocator = allocator_t{};
      auto new_elements = allocator.allocate (new_capacity + 1);
      if (!new_elements)
	{
	  return false;
	}

      if (size_)
	{
	  std::memcpy (new_elements, elements_, size_ * sizeof (element_t));
	}
      if (elements_)
	{
	  allocator.deallocate (elements_, capacity_ + 1);
	}

      elements_ = new_elements;
      capacity_ = new_capacity;
      return true;
    }

    inline void insert_reserved (element_t &&element) noexcept
    {
      std::size_t slot = size_ ? std::lower_bound (elements_, elements_ + size_, element, &less) - elements_ : 0;

      //HNSW_ALGO_PRINT("[sorted_buffer_gt] insert_reserved: slot: %d, size: %d, capacity: %d\n", (int) slot, (int) size_, (int) capacity_);
#if 0
      std::size_t to_move = size_ - slot;
      element_t *source = elements_ + size_ - 1;
      for (; to_move; --to_move, --source)
	{
	  source[1] = source[0];
	}
      elements_[slot] = element;
#endif

      for (std::size_t i = size_; i > slot; --i)
	{
	  elements_[i] = std::move (elements_[i - 1]);
	}

      elements_[slot] = std::move (element);

      size_++;

      //HNSW_ALGO_PRINT("[sorted_buffer_gt] insert_reserved: size: %d, capacity: %d\n", (int) size_, (int) capacity_);
    }

    /**
    *  @return `true` if the entry was added, `false` if it wasn't relevant enough.
    */
    inline bool insert (element_t &&element, std::size_t limit) noexcept
    {
      std::size_t slot = size_ ? std::lower_bound (elements_, elements_ + size_, element, &less) - elements_ : 0;
      if (slot == limit)
	{
	  return false;
	}
      std::size_t to_move = size_ - slot - (size_ == limit);
      element_t *source = elements_ + size_ - 1 - (size_ == limit);
      for (; to_move; --to_move, --source)
	{
	  source[1] = source[0];
	}
      elements_[slot] = element;
      size_ += size_ != limit;
      return true;
    }

    inline element_t pop() noexcept
    {
      size_--;
      element_t result = elements_[size_];
      elements_[size_].~element_t();
      return result;
    }

    void sort_ascending() noexcept {}
    inline void shrink (std::size_t n) noexcept
    {
      size_ = (std::min<std::size_t>) (n, size_);
    }

    inline element_t *data() noexcept
    {
      return elements_;
    }
    inline element_t const *data() const noexcept
    {
      return elements_;
    }

  private:
    static bool less (element_t const &a, element_t const &b) noexcept
    {
      return comparator_t{} (a, b);
    }
};
