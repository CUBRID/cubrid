/*
 * Copyright 2008 Search Solution Corporation
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
 * packer.hpp
 */

#ifndef _PACKER_HPP_
#define _PACKER_HPP_

#include "dbtype_def.h"
#include "mem_block.hpp"

#include <functional>
#include <vector>
#include <string>
#include <utility>

// forward definition
struct or_buf;
struct db_value;

namespace cubpacking
{
  class packable_object;
};

/*
 * the packer object packs primitive objects from a buffer and unpacker unpacks same objects from the buffer.
 * the packer & unpacker implementations should be mirrored.
 *
 * the buffer is provided at initialization; packer/unpacker classes are not meant for multi-threaded access.
 */
namespace cubpacking
{
  class packer
  {
    public:
      packer ();
      packer (char *storage, const size_t amount);
      void set_buffer (char *storage, const size_t amount);

      size_t get_packed_int_size (size_t curr_offset);
      void pack_int (const int value);
      size_t get_packed_size_overloaded (int value, size_t curr_offset);
      void pack_overloaded (int value);

      size_t get_packed_bool_size (size_t curr_offset);
      void pack_bool (bool value);
      size_t get_packed_size_overloaded (bool value, size_t curr_offset);
      void pack_overloaded (bool value);

      size_t get_packed_short_size (size_t curr_offset);
      void pack_short (const short value);
      size_t get_packed_size_overloaded (short value, size_t curr_offset);
      void pack_overloaded (short value);

      size_t get_packed_bigint_size (size_t curr_offset);
      void pack_bigint (const std::int64_t &value);
      void pack_bigint (const std::uint64_t &value);
      size_t get_packed_size_overloaded (const std::int64_t &value, size_t curr_offset);
      size_t get_packed_size_overloaded (const std::uint64_t &value, size_t curr_offset);
      void pack_overloaded (const std::int64_t &value);
      void pack_overloaded (const std::uint64_t &value);

      void pack_int_array (const int *array, const int count);

      size_t get_packed_int_vector_size (size_t curr_offset, const size_t count);
      void pack_int_vector (const std::vector<int> &array);

      size_t get_packed_db_value_size (const db_value &value, size_t curr_offset);
      void pack_db_value (const db_value &value);
      size_t get_packed_size_overloaded (const db_value &value, size_t curr_offset);
      void pack_overloaded (const db_value &value);

      size_t get_packed_small_string_size (const char *string, const size_t curr_offset);
      void pack_small_string (const char *string, const size_t str_size = 0);

      size_t get_packed_large_string_size (const std::string &str, const size_t curr_offset);
      void pack_large_string (const std::string &str);

      size_t get_packed_string_size (const std::string &str, const size_t curr_offset);
      void pack_string (const std::string &str);
      size_t get_packed_size_overloaded (const std::string &value, size_t curr_offset);
      void pack_overloaded (const std::string &str);

      size_t get_packed_c_string_size (const char *str, const size_t str_size, const size_t curr_offset);
      void pack_c_string (const char *str, const size_t str_size);

      size_t get_packed_size_overloaded (const packable_object &po, size_t curr_offset);
      void pack_overloaded (const packable_object &po);

      template <typename T>
      size_t get_packed_size_overloaded (const std::reference_wrapper<T> wrapper, const size_t curr_offset);

      template <typename T>
      void pack_overloaded (const std::reference_wrapper<T> &wrapper);

      template <typename T>
      size_t get_packed_size_overloaded (const std::vector<T> container, const size_t curr_offset);

      template <typename T>
      void pack_overloaded (const std::vector<T> &container);

      size_t get_packed_oid_size (const size_t curr_offset);
      void pack_oid (const OID &oid);
      size_t get_packed_size_overloaded (const OID &oid, size_t curr_offset);
      void pack_overloaded (const OID &oid);

      // packer should gradually replace OR_BUF, but they will coexist for a while. there will be functionality
      // strictly dependent on or_buf, so packer will have to cede at least some of the packing to or_buf
      //
      void delegate_to_or_buf (const size_t size, or_buf &buf);

      const char *get_curr_ptr (void);;
      size_t get_current_size (void);
      void align (const size_t req_alignment);
      const char *get_buffer_start (void);
      const char *get_buffer_end (void);
      bool is_ended (void);

      std::size_t get_packed_buffer_size (const char *stream, const std::size_t length, const std::size_t curr_offset) const;
      void pack_buffer_with_length (const char *stream, const std::size_t length);

      void pack_overloaded (const cubmem::block &blk);
      size_t get_packed_size_overloaded (const cubmem::block &blk, size_t curr_offset);

      // template function to pack object as int type
      template <typename T>
      void pack_to_int (const T &t);

      // template functions to pack objects in bulk
      // note - it requires versions of get_packed_size_overloaded and pack_overloaded

      // get packed size of all arguments. equivalent to:
      //
      // size_t total_size = 0;
      // for (arg : args)
      //   total_size += get_packed_size_overloaded (arg);
      // return total_size;
      //
      template <typename ... Args>
      size_t get_all_packed_size (Args &&... args);
      template <typename ... Args>
      size_t get_all_packed_size_starting_offset (size_t start_offset, Args &&... args);

      // pack all arguments. equivalent to:
      //
      // for (arg : args)
      //   pack_overloaded (arg);
      //
      template <typename ... Args>
      void pack_all (Args &&... args);

      // compute size of all arguments, extend the buffer to required size and then pack all arguments
      template <typename ExtBlk, typename ... Args>
      void set_buffer_and_pack_all (ExtBlk &eb, Args &&... args);

      // compute size of all arguments, extend the buffer by new required size
      // and then pack all arguments and then end of previous end of buffer
      template <typename ExtBlk, typename ... Args>
      void append_to_buffer_and_pack_all (ExtBlk &eb, Args &&... args);

    private:
      void pack_large_c_string (const char *string, const size_t str_size);

      template <typename T, typename ... Args>
      size_t get_all_packed_size_recursive (size_t curr_offset, T &&t, Args &&... args);
      template <typename T>
      size_t get_all_packed_size_recursive (size_t curr_offset, T &&t);

      template <typename T, typename ... Args>
      void pack_all_recursive (T &&t, Args &&... args);
      template <typename T>
      void pack_all_recursive (T &&t);

      const char *m_start_ptr; /* start of buffer */
      const char *m_end_ptr;     /* end of available serialization scope */
      char *m_ptr;
  };

  class unpacker
  {
    public:
      unpacker () = default;
      unpacker (const char *storage, const size_t amount);
      unpacker (const cubmem::block &blk);

      void set_buffer (const char *storage, const size_t amount);

      void unpack_int (int &value);
      void unpack_overloaded (int &value);
      void peek_unpack_int (int &value);
      void unpack_int_array (int *array, int &count);
      void unpack_int_vector (std::vector <int> &array);

      void unpack_bool (bool &value);
      void unpack_overloaded (bool &value);

      void unpack_short (short &value);
      void unpack_overloaded (short &value);

      void unpack_bigint (std::int64_t &value);
      void unpack_bigint (std::uint64_t &value);
      void unpack_overloaded (std::int64_t &value);
      void unpack_overloaded (std::uint64_t &value);

      void unpack_small_string (char *string, const size_t max_size);
      void unpack_large_string (std::string &str);
      void unpack_string (std::string &str);
      void unpack_overloaded (std::string &str);
      void unpack_c_string (char *str, const size_t max_str_size);
      void unpack_string_to_memblock (cubmem::extensible_block &blk);

      void unpack_db_value (db_value &value);
      void unpack_overloaded (db_value &value);

      void unpack_overloaded (packable_object &po);

      void peek_unpack_buffer_length (int &value);
      void unpack_buffer_with_length (char *stream, const std::size_t max_length);

      void peek_unpack_block_length (int &value);
      void unpack_overloaded (cubmem::block &blk);

      void unpack_oid (OID &oid);
      void unpack_overloaded (OID &oid);

      const char *get_curr_ptr (void);
      void align (const size_t req_alignment);
      size_t get_current_size (void);
      const char *get_buffer_start (void);
      const char *get_buffer_end (void);
      bool is_ended (void);

      // packer should gradually replace OR_BUF, but they will coexist for a while. there will be functionality
      // strictly dependent on or_buf, so packer will have to cede at least some of the packing to or_buf
      //
      void delegate_to_or_buf (const size_t size, or_buf &buf);

      // template function to unpack object from int type to T type
      template <typename T>
      void unpack_from_int (T &t);

      template <typename T>
      void unpack_overloaded (std::vector<T> &container);

      // template functions to unpack object in bulk
      // note - it requires implementations of unpack_overloaded for all types

      // unpack all arguments. equivalent to:
      //
      // for (arg : args)
      //   unpack_overloaded (arg);
      //
      // note - arguments should be of same type and order like when they were packed.
      template <typename ... Args>
      void unpack_all (Args &&... args);

    private:
      void unpack_string_size (size_t &len);

      template <typename T, typename ... Args>
      void unpack_all_recursive (T &&t, Args &&... args);
      template <typename T>
      void unpack_all_recursive (T &&t);

      const char *m_start_ptr; /* start of buffer */
      const char *m_end_ptr;     /* end of available serialization scope */
      const char *m_ptr;
  };

} // namespace cubpacking

// for legacy C files, because indent is confused by namespaces
using packing_packer = cubpacking::packer;
using packing_unpacker = cubpacking::unpacker;

//////////////////////////////////////////////////////////////////////////
// Template/inline implementation
//////////////////////////////////////////////////////////////////////////

namespace cubpacking
{
  //
  // packer
  //

  template <typename T>
  void
  packer::pack_to_int (const T &t)
  {
    pack_int ((int) t);
  }

  template <typename T>
  void
  packer::pack_overloaded (const std::reference_wrapper<T> &wrapper)
  {
    pack_overloaded (wrapper.get ());
  }

  template <typename T>
  size_t
  packer::get_packed_size_overloaded (const std::reference_wrapper<T> wrapper, size_t curr_offset)
  {
    return get_packed_size_overloaded (wrapper.get (), curr_offset);
  }

  template <typename T>
  size_t
  packer::get_packed_size_overloaded (const std::vector<T> container, const size_t curr_offset)
  {
    size_t size = get_packed_bigint_size (curr_offset);

    for (const T &t: container)
      {
	size += get_packed_size_overloaded (t, size);
      }

    return size;
  }

  template <typename T>
  void
  packer::pack_overloaded (const std::vector<T> &container)
  {
    const size_t count = container.size ();
    pack_bigint (count);
    for (const T &t : container)
      {
	pack_overloaded (t);
      }
  }

  template <typename ... Args>
  size_t
  packer::get_all_packed_size (Args &&... args)
  {
    return get_all_packed_size_recursive (0, std::forward<Args> (args)...);
  }

  template <typename ... Args>
  size_t
  packer::get_all_packed_size_starting_offset (size_t start_offset, Args &&... args)
  {
    size_t total_size = get_all_packed_size_recursive (start_offset, std::forward<Args> (args)...);
    return total_size - start_offset;
  }

  template <typename T>
  size_t
  packer::get_all_packed_size_recursive (size_t curr_offset, T &&t)
  {
    return curr_offset + get_packed_size_overloaded (std::forward<T> (t), curr_offset);
  }

  template <typename T, typename ... Args>
  size_t
  packer::get_all_packed_size_recursive (size_t curr_offset, T &&t, Args &&... args)
  {
    size_t next_offset = curr_offset + get_packed_size_overloaded (std::forward<T> (t), curr_offset);
    return get_all_packed_size_recursive (next_offset, std::forward<Args> (args)...);
  }

  template <typename ... Args>
  void
  packer::pack_all (Args &&... args)
  {
    pack_all_recursive (std::forward<Args> (args)...);
  }

  template <typename T>
  void
  packer::pack_all_recursive (T &&t)
  {
    pack_overloaded (std::forward<T> (t));
  }

  template <typename T, typename ... Args>
  void
  packer::pack_all_recursive (T &&t, Args &&... args)
  {
    pack_overloaded (std::forward<T> (t));
    pack_all (std::forward<Args> (args)...);
  }

  template <typename ExtBlk, typename ... Args>
  void
  packer::set_buffer_and_pack_all (ExtBlk &eb, Args &&... args)
  {
    size_t total_size = get_all_packed_size (std::forward<Args> (args)...);
    eb.extend_to (total_size);

    set_buffer (eb.get_ptr (), total_size);
    pack_all (std::forward<Args> (args)...);
  }


  template <typename ExtBlk, typename ... Args>
  void
  packer::append_to_buffer_and_pack_all (ExtBlk &eb, Args &&... args)
  {
    if (get_buffer_start () != eb.get_ptr ())
      {
	/* first call */
	return set_buffer_and_pack_all (eb, std::forward<Args> (args)...);
      }

    assert (get_curr_ptr () >= eb.get_ptr () && get_curr_ptr () <= eb.get_ptr () + eb.get_size ());

    size_t offset = get_curr_ptr () - get_buffer_start ();
    assert (offset >= 0);

    size_t available = eb.get_ptr () + eb.get_size () - get_curr_ptr ();

    size_t total_size = get_all_packed_size (std::forward<Args> (args)...);

    if (available < total_size)
      {
	eb.extend_by (total_size - available);
      }

    m_start_ptr = eb.get_ptr ();
    m_ptr = eb.get_ptr () + offset;
    m_end_ptr = eb.get_ptr () + offset + total_size;

    pack_all (std::forward<Args> (args)...);
  }

  //
  // unpacker
  //

  template <typename T>
  void
  unpacker::unpack_from_int (T &t)
  {
    int int_val;
    unpack_int (int_val);
    t = (T) int_val;
  }

  template <typename T>
  void
  unpacker::unpack_overloaded (std::vector<T> &container)
  {
    int64_t count;
    unpack_bigint (count);

    if (count > 0)
      {
	container.resize (count);
	for (int i = 0; i < count; i++)
	  {
	    unpack_overloaded (container[i]);
	  }
      }
  }

  template <typename ... Args>
  void
  unpacker::unpack_all (Args &&... args)
  {
    unpack_all_recursive (std::forward<Args> (args)...);
  }

  template <typename T, typename ... Args>
  void
  unpacker::unpack_all_recursive (T &&t, Args &&... args)
  {
    unpack_overloaded (std::forward<T> (t));
    unpack_all_recursive (std::forward<Args> (args)...);
  }

  template <typename T>
  void
  unpacker::unpack_all_recursive (T &&t)
  {
    unpack_overloaded (std::forward<T> (t));
  }
} // namespace cubpacking

#endif /* _PACKER_HPP_ */
