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

// heap_pending_record - a compact record and its retained OOS values

#include "config.h"

#include "heap_pending_record.hpp"

#include "error_code.h"
#include "error_manager.h"
#include "memory_alloc.h"

#include <new>
#include <utility>

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

heap_pending_record::heap_pending_record ()
  : m_record (cubmem::STANDARD_BLOCK_ALLOCATOR)
{
  m_record.set_external_buffer (nullptr, 0);
}

heap_pending_record::heap_pending_record (heap_pending_record &&other) noexcept
  : m_record (std::move (other.m_record))
  , m_bytes (other.m_bytes)
  , m_state (other.m_state)
{
  m_values.swap (other.m_values);
  other.m_bytes = 0;
  other.m_state = state::empty;
}

heap_pending_record::~heap_pending_record ()
{
  for (auto &value : m_values)
    {
      char *data = value.data ();
      free_and_init (data);
    }
}

int
heap_pending_record::retain (oos_buffer value, int &index)
{
  index = (int) m_values.size ();
  try
    {
      m_values.push_back (value);
    }
  catch (const std::bad_alloc &)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (oos_buffer));
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }
  m_bytes += value.size ();
  return NO_ERROR;
}

bool
heap_pending_record::owns (const RECDES &record) const
{
  return record.data != nullptr && record.data == get_recdes ().data
	 && record.length > 0 && record.length <= get_recdes ().area_size;
}

oos_buffer
heap_pending_record::resolve (const RECDES &record, std::size_t index, std::size_t length) const
{
  if (!is_prepared () || !owns (record) || index >= m_values.size () || m_values[index].size () != length)
    {
      return { nullptr, 0 };
    }
  return m_values[index];
}
