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
 * packable_helper.hpp
 */

#ifndef _PACKABLE_HELPER_HPP_
#define _PACKABLE_HELPER_HPP_

#include "packer.hpp"
#include "packable_object.hpp"

#include "memory_alloc.h"
#include "memory_private_allocator.hpp" /* cubmem::PRIVATE_BLOCK_ALLOCATOR */

namespace cubpacking
{
  template <typename T>
  class packable_object_with_cond : public packable_object
  {
public:
    packable_object_with_cond (T&& obj, bool cond)
    : m_obj (std::forward<T>(obj))
    , m_cond (cond)
    {}

    size_t get_packed_size (packer &serializator, std::size_t start_offset) const override
    {
        size_t size = serializator.get_packed_bool_size (start_offset); /* has_object */
        if (m_cond)
        {
          size += serializator.get_all_packed_size_starting_offset (size, m_obj);
        }
        return size;
    }

    void pack (packer &serializator) const override
    {
        serializator.pack_bool (m_cond);
        if (m_cond)
        {
          serializator.pack_overloaded (m_obj);
        }
    }

    void unpack (unpacker &deserializator) override
    {
	deserializator.unpack_bool (m_cond);
        if (m_cond)
        {
          deserializator.unpack_overloaded (m_obj);
        }
    }

    protected:
      T& m_obj;
      bool m_cond;
  };

  class packing_helper_string : public packable_object
  {
    private:
      char* &m_obj;
      int m_size;

    public:
      packing_helper_string (char *&str)
      : m_obj (str)
      , m_size (-1)
      {
      }

      packing_helper_string (char *&str, int& size)
      : m_obj (str)
      , m_size (size)
      {
      }

      void pack (packer &serializator) const
      {
        int size = (m_size == -1) ? strlen (m_obj) : m_size;
	serializator.pack_c_string (m_obj, size);
      }

      void
      unpack (unpacker &deserializator)
      {
	cubmem::extensible_block blk { cubmem::PRIVATE_BLOCK_ALLOCATOR };
	deserializator.unpack_string_to_memblock (blk);
	m_obj = blk.release_ptr ();
      }

      size_t
      get_packed_size (packer &serializator, std::size_t start_offset) const
      {
        int size = (m_size == -1) ? strlen (m_obj) : m_size;
	return serializator.get_packed_c_string_size (m_obj, size, start_offset);
      }
  };

  class packing_helper_int_array : public packable_object
  {
    public:
      packing_helper_int_array (int *&arr, int size)
	: m_obj (arr)
	, m_size (size)
      {}

      void
      pack (packer &serializator) const
      {
	serializator.pack_int_array (m_obj, m_size);
      }

      void
      unpack (unpacker &deserializator)
      {
	if (m_size > 0)
	  {
	    m_obj = (int *) db_private_alloc (NULL, sizeof (int) * m_size);
	    deserializator.unpack_int_array (m_obj, m_size);
	  }
      }

      size_t
      get_packed_size (packer &serializator, std::size_t start_offset) const
      {
	return serializator.get_packed_int_array_size (start_offset, m_size); // arg_pos
      }

    private:
      int *&m_obj;
      int &m_size;
  };
}

#endif
