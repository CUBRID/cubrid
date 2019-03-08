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

//
// xasl_analytic - implements XASL structures for analytics
//

#include "xasl_analytic.hpp"

#include "dbtype.h"

namespace cubxasl
{
  void
  analytic_list_node::init ()
  {
    /* is_first_exec_time */
    is_first_exec_time = true;

    /* part_value */
    db_make_null (&part_value);

    /* curr_cnt */
    curr_cnt = 0;
  }
}
