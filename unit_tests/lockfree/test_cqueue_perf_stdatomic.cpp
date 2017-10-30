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

#include "test_cqueue_perf_interface.hpp"

#define USE_STD_ATOMIC
#include "lockfree_circular_queue.hpp"

namespace test_lockfree {

class lcfq_statomic_tester : public lockfree_cqueue_tester
{
public:
  void test_run_count (std::size_t thread_count, std::size_t op_count, std::size_t cqueue_size)
    {
      run_count<lockfree::circular_queue<int> > (thread_count, op_count, cqueue_size);
    }
};

lockfree_cqueue_tester*
create_lfcq_stdatomic_tester ()
{
  return new lcfq_statomic_tester ();
}

} // namespace test_lockfree
