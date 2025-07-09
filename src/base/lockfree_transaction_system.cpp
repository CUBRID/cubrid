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

#include "lockfree_transaction_system.hpp"

#include <cassert>
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace lockfree
{
  namespace tran
  {
    system::system (size_t max_tran_count)
      : m_max_tran_per_table (max_tran_count)
      , m_tran_idx_map ()
    {
      assert (m_max_tran_per_table > 0);
      m_tran_idx_map.init (bitmap::chunking_style::ONE_CHUNK, static_cast<int> (max_tran_count),
			   bitmap::FULL_USAGE_RATIO);
    }

    index
    system::assign_index ()
    {
      int ret = m_tran_idx_map.get_entry ();
      if (ret < 0)
	{
	  assert (false);
	  return INVALID_INDEX;
	}
      return static_cast<index> (ret);
    }

    void
    system::free_index (index idx)
    {
      if (idx == INVALID_INDEX)
	{
	  assert (false);
	  return;
	}
      m_tran_idx_map.free_entry (static_cast<int> (idx));
    }

    size_t
    system::get_max_transaction_count () const
    {
      return m_max_tran_per_table;
    }
  } // namespace tran
} // namespace lockfree
