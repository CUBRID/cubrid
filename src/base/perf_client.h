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
 * perf_client.h - Monitor execution statistics at client
 */

#ifndef _PERF_CLIENT_H_
#define _PERF_CLIENT_H_

#ident "$Id$"

#include "system.h"

/* Client execution statistic structure */
typedef struct perfmon_client_stat_info PERFMON_CLIENT_STAT_INFO;
struct perfmon_client_stat_info
  {
  time_t cpu_start_usr_time;
  time_t cpu_start_sys_time;
  time_t elapsed_start_time;
  UINT64 *base_server_stats;
  UINT64 *current_server_stats;
  UINT64 *old_global_stats;
  UINT64 *current_global_stats;
  };

extern bool perfmon_Iscollecting_stats;

extern int perfmon_start_stats (bool for_all_trans);
extern int perfmon_stop_stats (void);
extern void perfmon_reset_stats (void);
extern int perfmon_print_stats (FILE * stream);
extern int perfmon_print_global_stats (FILE * stream, FILE * bin_stream, bool cumulative, const char *substr);
extern int perfmon_get_stats (void);
extern int perfmon_get_global_stats (void);

#endif /* _PERF_CLIENT_H_ */
