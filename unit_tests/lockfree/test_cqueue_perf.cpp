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

/*
 * test_cqueue_perf.cpp - implementation of performance compare
 */

#include "test_cqueue_perf.hpp"
#include "test_cqueue_perf_interface.hpp"

namespace test_lockfree {

void
test_compare_lfcqs (void)
{
  lockfree_cqueue_tester *lfcq_testers[LFCQ_IMPLEMENTATION_COUNT] =
    { create_lfcq_stdatomic_tester (), create_lfcq_portatomic_tester (), create_lfcq_old_tester () };

  std::size_t thread_count = 4;
  std::size_t op_count = 1000000;
  std::size_t lfcq_size = 1024;

  for (unsigned i = 0; i < LFCQ_IMPLEMENTATION_COUNT; i++)
    {
      lfcq_testers[i]->test_run_count (thread_count, op_count, lfcq_size);
    }
}

} // namespace test_lockfree
