/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 */

//
// record_descriptor.cpp - extended functionality of recdes
//

#include "record_descriptor.hpp"

#include "error_code.h"
#include "memory_alloc.h"
#include "slotted_page.h"

#include <cstring>

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

record_descriptor::record_descriptor (void)
{
  m_recdes.area_size = 0;
  m_recdes.length = 0;
  m_recdes.type = REC_HOME;
  m_recdes.data = NULL;
  m_own_data = NULL;
  m_data_source = data_source::INVALID;
}

record_descriptor::record_descriptor (const recdes &rec)
  : record_descriptor ()
{
  m_recdes.type = rec.type;
  if (rec.length != 0)
    {
      // copy content from argument
      m_recdes.area_size = rec.length;
      m_recdes.length = m_recdes.area_size;
      m_own_data = m_recdes.data = (char *) db_private_alloc (NULL, m_recdes.area_size);
      assert (m_own_data != NULL);
      std::memcpy (m_recdes.data, rec.data, m_recdes.length);

      m_data_source = data_source::COPIED;  // we assume this is a copied record
    }
}

record_descriptor::record_descriptor (const char *data, size_t size)
  : record_descriptor ()
{
  set_data (data, size);
}

record_descriptor::~record_descriptor (void)
{
  if (m_own_data != NULL)
    {
      db_private_free (NULL, m_own_data);
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

      size_t required_size = static_cast<size_t> (-m_recdes.length);
      resize (thread_p, required_size, false);

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
record_descriptor::resize (cubthread::entry *thread_p, std::size_t required_size, bool copy_data)
{
  if (required_size <= m_recdes.area_size)
    {
      // resize not required
      return;
    }

  if (m_own_data == NULL)
    {
      m_own_data = (char *) db_private_alloc (thread_p, static_cast<int> (required_size));
      assert (m_own_data != NULL);
      if (copy_data)
	{
	  assert (m_recdes.data != NULL);
	  std::memcpy (m_own_data, m_recdes.data, m_recdes.length);
	}
    }
  else
    {
      m_own_data = (char *) db_private_realloc (thread_p, m_own_data, static_cast<int> (required_size));
      assert (m_own_data != NULL);
      if (copy_data)
	{
	  // realloc copied data
	}
    }

  m_recdes.data = m_own_data;
  m_recdes.area_size = (int) required_size;
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
record_descriptor::set_data (const char *data, size_t size)
{
  // data is assigned and cannot be changed
  m_data_source = data_source::IMMUTABLE;
  m_recdes.data = const_cast<char *> (data);    // status will protect against changes
  m_recdes.length = (int) size;
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
      resize (NULL, new_size, true);
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
  assert (m_data_source == data_source::COPIED || m_data_source == data_source::NEW);
}
