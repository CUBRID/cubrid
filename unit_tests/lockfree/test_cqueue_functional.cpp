
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

#include "test_cqueue_functional.hpp"

#include "lockfree_circular_queue.hpp"

#include <cstdint>
#include <atomic>
#include <iostream>

namespace test_lockfree {

typedef uint64_t op_count_type;
typedef std::atomic<op_count_type> atomic_op_count_type;
typedef lockfree::circular_queue<int> test_cqueue;

void
consume_global_count (test_cqueue & cqueue, const std::string & my_name, const op_count_type global_op_count,
                      atomic_op_count_type & consumed_op_count)
{
  int cosumed_data;
  op_count_type local_count;
  while (consumed_op_count < global_op_count)
    {
      if (cqueue.consume (cosumed_data))
        {
          local_count = ++consumed_op_count;
          if (local_count % 100000)
            {
              sync_cout (std::string ("    ").append (my_name).append (" consumed ").append (local_count));
            }
        }
    }
}

int test_cqueue_functional (void)
{
  return 0;
}

} // namespace test_lockfree
