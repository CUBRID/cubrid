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

#ifndef _HEAP_PENDING_OOS_VALUES_HPP_
#define _HEAP_PENDING_OOS_VALUES_HPP_

#include "oos_file.hpp"

#include <cstddef>
#include <vector>

/* Owns only serialized values selected for OOS. Records borrow these allocations
 * until their last reader/finalizer returns; record copies do not transfer ownership. */
class heap_pending_oos_values
{
  public:
    ~heap_pending_oos_values ();
    heap_pending_oos_values () = default;
    heap_pending_oos_values (const heap_pending_oos_values &) = delete;
    heap_pending_oos_values &operator= (const heap_pending_oos_values &) = delete;
    int retain (oos_buffer value);
    std::size_t size () const
    {
      return m_values.size ();
    }
    std::size_t retained_bytes () const
    {
      return m_bytes + m_values.capacity () * sizeof (oos_buffer);
    }
    void discard_since (std::size_t count);

  private:
    std::vector<oos_buffer> m_values;
    std::size_t m_bytes = 0;
};

#endif // _HEAP_PENDING_OOS_VALUES_HPP_
