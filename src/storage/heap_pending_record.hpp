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

#ifndef _HEAP_PENDING_RECORD_HPP_
#define _HEAP_PENDING_RECORD_HPP_

#include "oos_file.hpp"
#include "record_descriptor.hpp"

#include <cstddef>
#include <vector>

// RECDES views borrow this owner's record and payload allocations. Moving the
// owner preserves those allocations; readers must finish before its destruction.
class heap_pending_record
{
  public:
    heap_pending_record ();
    ~heap_pending_record ();
    heap_pending_record (heap_pending_record &&other) noexcept;
    heap_pending_record (const heap_pending_record &) = delete;
    heap_pending_record &operator= (const heap_pending_record &) = delete;

    record_descriptor &record ()
    {
      return m_record;
    }
    const RECDES &get_recdes () const
    {
      return m_record.get_recdes ();
    }
    std::size_t retained_bytes () const
    {
      return m_record.get_recdes ().area_size + m_bytes + m_values.capacity () * sizeof (oos_buffer);
    }
    int retain (oos_buffer value, int &index);
    oos_buffer resolve (const RECDES &record, std::size_t index, std::size_t length) const;
    bool owns (const RECDES &record) const;
    bool is_prepared () const
    {
      return m_state == state::prepared;
    }
    bool is_finalized () const
    {
      return m_state == state::finalized;
    }
    void mark_prepared ()
    {
      m_state = state::prepared;
    }
    void finish (bool success)
    {
      m_state = success ? state::finalized : state::failed;
      if (success)
	{
	  m_record.set_type (REC_HOME);
	}
    }

  private:
    record_descriptor m_record;
    std::vector<oos_buffer> m_values;
    std::size_t m_bytes = 0;
    enum class state { empty, prepared, finalized, failed };
    state m_state = state::empty;
};

#endif // _HEAP_PENDING_RECORD_HPP_
