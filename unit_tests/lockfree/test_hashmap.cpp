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

#include "test_hashmap.hpp"

#include "lockfree_hashmap.hpp"
#include "lockfree_transaction_system.hpp"

using namespace lockfree;

namespace test_lockfree
{
  using int_hashmap = hashmap<int, int>;
  static lf_entry_descriptor g_entdesc;

  int
  test_hashmap_functional ()
  {
    tran::system sys (10);
    int_hashmap hash;
    hash.init (sys, 10, 10, 10, g_entdesc);
    return 0;
  }

  int
  test_hashmap_performance ()
  {
    tran::system sys (10);
    int_hashmap hash;
    hash.init (sys, 10, 10, 10, g_entdesc);
    return 0;
  }
} // namespace test_lockfree
