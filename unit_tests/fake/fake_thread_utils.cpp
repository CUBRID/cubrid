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

#include "error_code.h"
#include "lock_free.h"
#include "lockfree_transaction_system.hpp"

int
lf_initialize_transaction_systems (int max_threads)
{
  return NO_ERROR;
}

int
css_get_max_conn (void)
{
  return 1;
}

namespace lockfree // needed by cubthread::manager
{
  const float bitmap::FULL_USAGE_RATIO = 1.0f;
  const float bitmap::NINTETYFIVE_PERCENTILE_USAGE_RATIO = 0.95f;
  bitmap::bitmap () = default;

  void
  bitmap::init (chunking_style style_arg, int entries_count_arg, float usage_ratio_arg) {}

  bitmap::~bitmap () {}

  int
  bitmap::get_entry ()
  {
    return 0;
  }

  void
  bitmap::free_entry (int entry_idx) {}

  namespace tran
  {
    index
    system::assign_index ()
    {
      return INVALID_INDEX;
    }

    system::system (size_t max_tran_count) : m_max_tran_per_table (max_tran_count) {}

    void
    system::free_index (index idx) {}

  } // namespace tran

} // namespace lockfree
