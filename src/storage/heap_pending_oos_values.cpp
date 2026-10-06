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

// heap_pending_oos_values - ownership of serialized values awaiting OOS insertion

#include "config.h"

#include "heap_pending_oos_values.hpp"

#include "error_code.h"
#include "error_manager.h"
#include "memory_alloc.h"

#include <new>

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

heap_pending_oos_values::~heap_pending_oos_values ()
{
  discard_since (0);
}

int
heap_pending_oos_values::retain (oos_buffer value)
{
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

void
heap_pending_oos_values::discard_since (std::size_t count)
{
  while (m_values.size () > count)
    {
      char *data = m_values.back ().data ();
      m_bytes -= m_values.back ().size ();
      free_and_init (data);
      m_values.pop_back ();
    }
}

