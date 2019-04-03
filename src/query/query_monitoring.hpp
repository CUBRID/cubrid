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
// Monitoring queries
//

#ifndef _QUERY_MONITORING_HPP_
#define _QUERY_MONITORING_HPP_

#include "storage_common.h"

typedef struct tran_query_exec_info TRAN_QUERY_EXEC_INFO;
struct tran_query_exec_info
{
  char *wait_for_tran_index_string;
  float query_time;
  float tran_time;
  char *query_stmt;
  char *sql_id;
  XASL_ID xasl_id;
};

#endif // !_QUERY_MONITORING_HPP_
