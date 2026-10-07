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

//
// record_descriptor.cpp - extended functionality of recdes
//

#include "record_descriptor.hpp"

#include "error_code.h"
#include "memory_alloc.h"
#include "packer.hpp"
#include "slotted_page.h"

#include <cstring>
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

//  record_descriptor extends functionality for recdes:
//
//  typedef struct recdes RECDES;	/* RECORD DESCRIPTOR */
//  struct recdes
//  {
//    int area_size;		/* Length of the allocated area. It includes only the data field. The value is negative
//				 * if data is inside buffer. For example, peeking in a slotted page. */
//    int length;			/* Length of the data. Does not include the length and type fields */
//    INT16 type;			/* Type of record */
//    char *data;			/* The data */
//  };
//

record_descriptor::record_descriptor (const cubmem::block_allocator &alloc /* = cubmem::PRIVATE_BLOCK_ALLOCATOR */)
  : m_recdes ()
  , m_own_data (alloc)
  , m_data_source (data_source::INVALID)
{
  m_recdes.area_size = 0;
  m_recdes.length = 0;
  m_recdes.type = REC_HOME;
  m_recdes.data = NULL;
}

record_descriptor::record_descriptor (const recdes &rec,
				      const cubmem::block_allocator &alloc /* = cubmem::PRIVATE_BLOCK_ALLOCATOR */)
  : record_descriptor (alloc)
{
  set_recdes (rec);
}

record_descriptor::record_descriptor (record_descriptor &&other)
{
  m_recdes = other.m_recdes;
  m_own_data = std::move (other.m_own_data);
  m_data_source = other.m_data_source;

  other.m_data_source = data_source::INVALID;
  other.m_recdes.data = NULL;
  other.m_recdes.type = REC_UNKNOWN;
}

record_descriptor::record_descriptor (const char *data, std::size_t size)
  : record_descriptor ()
{
  set_data (data, size);
}

record_descriptor::~record_descriptor (void)
{
}

void
record_descriptor::set_recdes (const recdes &rec)
{
  assert (m_data_source == data_source::INVALID);

  m_recdes.type = rec.type;
  if (rec.length != 0)
    {
      // copy content from argument
      m_recdes.area_size = rec.length;
      m_recdes.length = m_recdes.area_size;
      m_own_data.extend_to ((std::size_t) m_recdes.area_size);
      m_recdes.data = m_own_data.get_ptr ();
      std::memcpy (m_recdes.data, rec.data, m_recdes.length);

      m_data_source = data_source::COPIED;  // we assume this is a copied record
    }
}

int
record_descriptor::peek (cubthread::entry *thread_p, PAGE_PTR page, PGSLOTID slotid)
{
  return get (thread_p, page, slotid, record_get_mode::PEEK_RECORD);
}

int
record_descriptor::copy (cubthread::entry *thread_p, PAGE_PTR page, PGSLOTID slotid)
{
  return get (thread_p, page, slotid, record_get_mode::COPY_RECORD);
}

int
record_descriptor::get (cubthread::entry *thread_p, PAGE_PTR page, PGSLOTID slotid, record_get_mode mode)
{
  int mode_to_int = static_cast<int> (mode);
  SCAN_CODE sc = spage_get_record (thread_p, page, slotid, &m_recdes, mode_to_int);
  if (sc == S_SUCCESS)
    {
      update_source_after_get (mode);
      return NO_ERROR;
    }

  if (sc == S_DOESNT_FIT)
    {
      // extend and try again
      assert (m_recdes.length < 0);
      assert (mode == record_get_mode::COPY_RECORD);

      std::size_t required_size = static_cast<std::size_t> (-m_recdes.length);
      resize_buffer (required_size);

      sc = spage_get_record (thread_p, page, slotid, &m_recdes, mode_to_int);
      if (sc == S_SUCCESS)
	{
	  update_source_after_get (mode);
	  return NO_ERROR;
	}
    }

  // failed
  assert (false);
  return ER_FAILED;
}

void
record_descriptor::resize_buffer (std::size_t required_size)
{
  assert (m_data_source == data_source::INVALID || is_mutable ());

  if (m_recdes.area_size > 0 && required_size <= (std::size_t) m_recdes.area_size)
    {
      // resize not required
      return;
    }

  m_own_data.extend_to (required_size);

  m_recdes.data = m_own_data.get_ptr ();
  m_recdes.area_size = (int) required_size;

  if (m_data_source == data_source::INVALID)
    {
      m_data_source = data_source::NEW;
    }
}

void
record_descriptor::update_source_after_get (record_get_mode mode)
{
  switch (mode)
    {
    case record_get_mode::PEEK_RECORD:
      m_data_source = data_source::PEEKED;
      break;
    case record_get_mode::COPY_RECORD:
      m_data_source = data_source::COPIED;
      break;
    default:
      assert (false);
      m_data_source = data_source::INVALID;
      break;
    }
}

const recdes &
record_descriptor::get_recdes (void) const
{
  assert (m_data_source != data_source::INVALID);
  return m_recdes;
}

const char *
record_descriptor::get_data (void) const
{
  assert (m_data_source != data_source::INVALID);
  return m_recdes.data;
}

std::size_t
record_descriptor::get_size (void) const
{
  assert (m_data_source != data_source::INVALID);
  assert (m_recdes.length > 0);
  return static_cast<std::size_t> (m_recdes.length);
}

char *
record_descriptor::get_data_for_modify (void)
{
  check_changes_are_permitted ();

  return m_recdes.data;
}

void
record_descriptor::set_data (const char *data, std::size_t size)
{
  // data is assigned and cannot be changed
  m_data_source = data_source::IMMUTABLE;
  m_recdes.data = const_cast<char *> (data);    // status will protect against changes
  m_recdes.length = (int) size;
}

void
record_descriptor::set_record_length (std::size_t length)
{
  check_changes_are_permitted ();
  assert (m_recdes.area_size >= 0 && length <= (size_t) m_recdes.area_size);
  m_recdes.length = (int) length;
}

void
record_descriptor::set_type (std::int16_t type)
{
  check_changes_are_permitted ();
  m_recdes.type = type;
}

void
record_descriptor::set_external_buffer (char *buf, std::size_t buf_size)
{
  m_own_data.freemem ();
  m_recdes.data = buf;
  m_recdes.area_size = (int) buf_size;
  m_data_source = data_source::NEW;
}

void
record_descriptor::move_data (std::size_t dest_offset, std::size_t source_offset)
{
  check_changes_are_permitted ();

  // should replace RECORD_MOVE_DATA
  if (dest_offset == source_offset)
    {
      // no moving
      return;
    }

  std::size_t rec_size = get_size ();

  // safe-guard: source offset cannot be outside record
  assert (rec_size >= source_offset);

  std::size_t memmove_size = rec_size - source_offset;
  std::size_t new_size = rec_size + dest_offset - source_offset;

  if (dest_offset > source_offset)
    {
      // record is being increased; make sure we have enough space
      resize_buffer (new_size);
    }

  if (memmove_size > 0)
    {
      std::memmove (m_recdes.data + dest_offset, m_recdes.data + source_offset, memmove_size);
    }
  m_recdes.length = static_cast<int> (new_size);
}

void
record_descriptor::modify_data (std::size_t offset, std::size_t old_size, std::size_t new_size, const char *new_data)
{
  check_changes_are_permitted ();

  // should replace RECORD_REPLACE_DATA
  move_data (offset + new_size, offset + old_size);
  if (new_size > 0)
    {
      std::memcpy (m_recdes.data + offset, new_data, new_size);
    }
}

void
record_descriptor::delete_data (std::size_t offset, std::size_t data_size)
{
  check_changes_are_permitted ();

  // just move data
  move_data (offset, offset + data_size);
}

void
record_descriptor::insert_data (std::size_t offset, std::size_t new_size, const char *new_data)
{
  modify_data (offset, 0, new_size, new_data);
}

void
record_descriptor::check_changes_are_permitted (void) const
{
  assert (is_mutable ());
}

bool
record_descriptor::is_mutable () const
{
  return m_data_source == data_source::COPIED || m_data_source == data_source::NEW;
}

void
record_descriptor::pack (cubpacking::packer &packer) const
{
  packer.pack_short (m_recdes.type);
  packer.pack_buffer_with_length (m_recdes.data, m_recdes.length);
}

void
record_descriptor::unpack (cubpacking::unpacker &unpacker)
{
  // resize_buffer requires m_data_source to be set
  m_data_source = data_source::COPIED;

  unpacker.unpack_short (m_recdes.type);
  unpacker.peek_unpack_buffer_length (m_recdes.length);
  resize_buffer (m_recdes.length);
  unpacker.unpack_buffer_with_length (m_recdes.data, m_recdes.length);
}

std::size_t
record_descriptor::get_packed_size (cubpacking::packer &packer, std::size_t curr_offset) const
{
  std::size_t entry_size = packer.get_packed_short_size (curr_offset);
  entry_size += packer.get_packed_buffer_size (m_recdes.data, m_recdes.length, entry_size);

  return entry_size;
}

void
record_descriptor::release_buffer (char *&data, std::size_t &size)
{
  size = m_own_data.get_size ();
  data = m_own_data.release_ptr ();
}
