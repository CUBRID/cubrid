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
// query_analytic - interface for analytic query execution
//

#ifndef _QUERY_ANALYTIC_HPP_
#define _QUERY_ANALYTIC_HPP_

#if !defined (SERVER_MODE) && !defined (SA_MODE)
#error Wrong module
#endif // not server and not SA

#include "system.h"               // QUERY_ID

// forward definitions
struct val_descr;

namespace cubthread
{
  class entry;
}

namespace cubxasl
{
  struct analytic_list_node;
} // namespace cubxasl

int qdata_initialize_analytic_func (cubthread::entry *thread_p, cubxasl::analytic_list_node *func_p, QUERY_ID query_id);
int qdata_evaluate_analytic_func (cubthread::entry *thread_p, cubxasl::analytic_list_node *func_p, val_descr *vd);
int qdata_finalize_analytic_func (cubthread::entry *thread_p, cubxasl::analytic_list_node *func_p, bool is_same_group);

#endif // _QUERY_ANALYTIC_HPP_
