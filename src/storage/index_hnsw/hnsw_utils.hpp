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
#include <memory>

// *********************************************************************************
// From Usearch's implementation
// *********************************************************************************

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

template <typename at, typename other_at = at> at exchange (at &obj, other_at&& new_value)
{
  at old_value = std::move (obj);
  obj = std::forward<other_at> (new_value);
  return old_value;
}

template <typename element_at,                                //
	  typename comparator_at = std::less<void>,           // <void> is needed before C++14.
	  typename allocator_at = std::allocator<element_at>> //
class max_heap_gt
{
  public:
    using element_t = element_at;
    using comparator_t = comparator_at;
    using allocator_t = allocator_at;

    using value_type = element_t;

    static_assert (std::is_trivially_destructible<element_t>(), "This heap is designed for trivial structs");
    static_assert (std::is_trivially_copy_constructible<element_t>(), "This heap is designed for trivial structs");

  private:
    element_t *elements_;
    std::size_t size_;
    std::size_t capacity_;

  public:
    max_heap_gt() noexcept : elements_ (nullptr), size_ (0), capacity_ (0) {}

    max_heap_gt (max_heap_gt &&other) noexcept
      : elements_ (exchange (other.elements_, nullptr)), size_ (exchange (other.size_, 0)),
	capacity_ (exchange (other.capacity_, 0)) {}

    max_heap_gt &operator= (max_heap_gt &&other) noexcept
    {
      std::swap (elements_, other.elements_);
      std::swap (size_, other.size_);
      std::swap (capacity_, other.capacity_);
      return *this;
    }

    max_heap_gt (max_heap_gt const &) = delete;
    max_heap_gt &operator= (max_heap_gt const &) = delete;

    ~max_heap_gt() noexcept
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
    inline element_t *data() noexcept
    {
      return elements_;
    }
    inline element_t const *data() const noexcept
    {
      return elements_;
    }
    inline void clear() noexcept
    {
      size_ = 0;
    }
    inline void shrink (std::size_t n) noexcept
    {
      size_ = (std::min<std::size_t>) (n, size_);
    }

    /// @brief  Selects the largest element in the heap.
    /// @return Reference to the stored element.
    inline element_t const &top() const noexcept
    {
      return elements_[0];
    }

    /// @brief Invalidates the "max-heap" property, transforming into ascending range.
    inline void sort_ascending() noexcept
    {
      std::sort_heap (elements_, elements_ + size_, &less);
    }

    /**
     *  @brief Ensures the heap has enough capacity for the specified number of elements.
     *  @param new_capacity The desired minimum capacity.
     *  @return True if the capacity was successfully increased, false otherwise.
     */
    bool reserve (std::size_t new_capacity) noexcept
    {
      if (new_capacity < capacity_)
	{
	  return true;
	}

      new_capacity = ceil2 (new_capacity);
      new_capacity = (std::max<std::size_t>) (new_capacity, (std::max<std::size_t>) (capacity_ * 2u, 16u));
      auto allocator = allocator_t{};
      auto new_elements = allocator.allocate (new_capacity);
      if (!new_elements)
	{
	  return false;
	}

      if (elements_)
	{
	  std::memcpy (new_elements, elements_, size_ * sizeof (element_t));
	  allocator.deallocate (elements_, capacity_);
	}
      elements_ = new_elements;
      capacity_ = new_capacity;
      return new_elements;
    }

    /**
     *  @brief Inserts an element into the heap.
     *  @param element The element to be inserted.
     *  @return True if the element was successfully inserted, false otherwise.
     */
    bool insert (element_t &&element) noexcept
    {
      if (!reserve (size_ + 1))
	{
	  return false;
	}

      insert_reserved (std::move (element));
      return true;
    }

    /**
     *  @brief Inserts an element into the heap without reserving additional space.
     *  @param element The element to be inserted.
     */
    void insert_reserved (element_t &&element) noexcept
    {
      new (&elements_[size_]) element_t (element);
      size_++;
      shift_up (size_ - 1);
    }

    /**
     *  @brief Inserts multiple elements into the heap.
     *  @param elements Pointer to the elements to be inserted.
     *  @return True if the elements were successfully inserted, false otherwise.
     */
    inline bool insert_many (element_t const *elements) noexcept
    {
      // Wikipedia describes a procedure, due to Floyd, which constructs a heap from an array in linear time.
      // It also mentions a procedure for merging two heaps, of sizes 𝑛 and 𝑘, in time 𝑂(𝑘+log𝑘log𝑛).
      // Altogether, we can add 𝑘 elements to a heap of length 𝑛 in time 𝑂(𝑘+log𝑘log𝑛): first build a heap containing
      // 𝑘 elements to be inserted (takes 𝑂(𝑘) time), then merge that with the heap of size 𝑛 (takes 𝑂(𝑘+log𝑘log𝑛)
      // time). Compare this to repeated insertion, which would run in time 𝑂(𝑘log𝑛).
      return false;
    }

    element_t pop() noexcept
    {
      element_t result = top();
      std::swap (elements_[0], elements_[size_ - 1]);
      size_--;
      elements_[size_].~element_t();
      shift_down (0);
      return result;
    }

  private:
    static std::size_t parent_idx (std::size_t i) noexcept
    {
      return (i - 1u) / 2u;
    }
    static std::size_t left_child_idx (std::size_t i) noexcept
    {
      return (i * 2u) + 1u;
    }
    static std::size_t right_child_idx (std::size_t i) noexcept
    {
      return (i * 2u) + 2u;
    }
    static bool less (element_t const &a, element_t const &b) noexcept
    {
      return comparator_t{} (a, b);
    }

    /**
     *  @brief Shifts an element up to maintain the heap property.
     *         This operation is called when a new element is @b added at the end of the heap.
     *         The element is moved up until the heap property is restored.
     *  @param i Index of the element to be shifted up.
     */
    void shift_up (std::size_t i) noexcept
    {
      for (; i && less (elements_[parent_idx (i)], elements_[i]); i = parent_idx (i))
	{
	  std::swap (elements_[parent_idx (i)], elements_[i]);
	}
    }

    /**
     *  @brief Shifts an element down to maintain the heap property.
     *         This operation is called when the root element is @b removed and the last element is moved to the root.
     *         The element is moved down until the heap property is restored.
     *  @param i Index of the element to be shifted down.
     */
    void shift_down (std::size_t i) noexcept
    {
      std::size_t max_idx = i;

      std::size_t left = left_child_idx (i);
      if (left < size_ && less (elements_[max_idx], elements_[left]))
	{
	  max_idx = left;
	}

      std::size_t right = right_child_idx (i);
      if (right < size_ && less (elements_[max_idx], elements_[right]))
	{
	  max_idx = right;
	}

      if (i != max_idx)
	{
	  std::swap (elements_[i], elements_[max_idx]);
	  shift_down (max_idx);
	}
    }
};

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

      if (size_ > slot)
	{
	  std::memmove (elements_ + slot + 1, elements_ + slot, (size_ - slot) * sizeof (element_t));
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
      if (to_move > 0)
	{
	  std::memmove (elements_ + slot + 1, elements_ + slot, to_move * sizeof (element_t));
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
