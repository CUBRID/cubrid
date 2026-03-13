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

/*
 * span.hpp
 */

#ifndef _SPAN_HPP_
#define _SPAN_HPP_

#ident "$Id$"

#include "assert.h"

#include <cstddef>

namespace cubbase
{
  template <typename T>
  class span
  {
    public:
      using element_type = T;
      using pointer = T*;
      using reference = T&;
      using const_reference = const T&;
      using iterator = T*;
      using const_iterator = const T*;
      using size_type = std::size_t;

      span() noexcept :
	_data (nullptr),
	_size (0)
      {
      }

      span (T *ptr, size_type count) noexcept :
	_data (ptr),
	_size (count)
      {
      }

      template <std::size_t N>
      span (T (&arr)[N]) noexcept :
	_data (arr),
	_size (N)
      {
      }

      iterator begin() noexcept
      {
	return _data;
      }

      iterator end() noexcept
      {
	return _data + _size;
      }

      const_iterator begin() const noexcept
      {
	return _data;
      }

      const_iterator end() const noexcept
      {
	return _data + _size;
      }

      reference operator[] (size_type idx) noexcept
      {
	assert (idx < _size);
	return _data[idx];
      }

      reference operator[] (size_type idx) const noexcept
      {
	assert (idx < _size);
	return _data[idx];
      }

      pointer data() const noexcept
      {
	return _data;
      }

      size_type size() const noexcept
      {
	return _size;
      }

      bool empty() const noexcept
      {
	return _size == 0;
      }

      span<T> subspan (size_type offset, size_type count = static_cast<size_type> (-1)) const noexcept
      {
	assert (offset <= _size);
	size_type max_len = _size - offset;
	if (count > max_len || count == static_cast<size_type> (-1))
	  {
	    count = max_len;
	  }
	return span<T> (_data + offset, count);
      }

    public:
      T *_data;
      size_type _size;
  };
}

#endif /* _SPAN_HPP_ */
