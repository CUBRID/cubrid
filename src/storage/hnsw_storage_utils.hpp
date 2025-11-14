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
// hnsw_storage_utils.hpp
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

template <typename T>
struct lightweight_span
{
  const T* data_;
  size_t size_;

  lightweight_span(const T* data, size_t size)
    : data_(data), size_(size) {}

  const T* data() const { return data_; }
  size_t size() const { return size_; }

  const T& operator[](size_t i) const { return data_[i]; }
  const T* begin() const { return data_; }
  const T* end() const { return data_ + size_; }
};

template <typename Cleanup = void(*)()>
class rw_span_cursor
{
  public:
    std::byte* p;
    std::byte* end;

    std::optional<scope_exit<Cleanup>> m_guard;

    explicit rw_span_cursor (std::byte* span, size_t size)
    : p(span), end(span + size) {}

    ~rw_span_cursor() = default;
    
    rw_span_cursor(const rw_span_cursor&) = delete;
    rw_span_cursor& operator=(const rw_span_cursor&) = delete;

    rw_span_cursor(rw_span_cursor&& other) noexcept
    : p(other.p), end(other.end), m_guard(std::move(other.m_guard))
    {
      other.p = nullptr;
      other.end = nullptr;
    }

    rw_span_cursor& operator=(rw_span_cursor&& other) noexcept
    {
      if (this != &other)
      {
        p = other.p;
        end = other.end;
        m_guard = std::move(other.m_guard);

        other.p = nullptr;
        other.end = nullptr;
      }
      return *this;
    }

    template <typename F>
    void set_scope_exit(F &&f)
    {
      m_guard.emplace(std::forward<F>(f));
    }

    void close()
    {
      if (m_guard.has_value()) {
        m_guard->release();
        m_guard.reset();
      }
    }

    template <typename T>
    T& write_then_ref(const T& v)
    {
      static_assert(std::is_trivially_copyable_v<T>, "T must be POD.");
      assert(p + sizeof(T) <= end);
  
      std::memcpy(p, &v, sizeof(T));
      T& ref = *reinterpret_cast<T*>(p);
      p += sizeof(T);
      return ref;
    }
  
    template <typename T>
    void write_array(const T* src, size_t count)
    {
      static_assert(std::is_trivially_copyable_v<T>, "T must be POD.");
      size_t bytes = sizeof(T) * count;
      assert(p + bytes <= end);
  
      std::memcpy(p, src, bytes);
      p += bytes;
    }
  
    template <typename T>
    T read_pod()
    {
      static_assert(std::is_trivially_copyable_v<T>, "T must be POD.");
      assert(p + sizeof(T) <= end);
  
      T v = *reinterpret_cast<const T*>(p);
      p += sizeof(T);
      return v;
    }
  
    template <typename T>
    lightweight_span<T> read_array(size_t count)
    {
      static_assert(std::is_trivially_copyable_v<T>, "T must be POD.");
      size_t bytes = sizeof(T) * count;
      assert(p + bytes <= end);
  
      T* arr = reinterpret_cast<T*>(p);
      p += bytes;
      return lightweight_span<T>(arr, count);
    }
  
    template <typename T>
    T* ref_pod()
    {
      static_assert(std::is_trivially_copyable_v<T>, "T must be POD.");
      assert(p + sizeof(T) <= end);
  
      return reinterpret_cast<T*>(p);
    }
  
    void skip(size_t bytes)
    {
      assert(p + bytes <= end);
      p += bytes;
    }
};
