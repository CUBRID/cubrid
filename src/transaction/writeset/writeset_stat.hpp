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

/*
 * writeset_stat.hpp - writeset statistics of the HA master, shown by SHOW WRITESET STATUS
 */

#ifndef _WRITESET_STAT_HPP_
#define _WRITESET_STAT_HPP_

#ident "$Id$"

#if !defined(SERVER_MODE) && !defined(SA_MODE)
#error Belongs to server module
#endif /* !defined (SERVER_MODE) && !defined (SA_MODE) */

#include "dbtype.h"		/* DB_VALUE */
#include "thread_compat.hpp"	/* THREAD_ENTRY */

extern void wset_stat_start (void);

extern void wset_stat_count_key_limit_exceeded (void);
extern void wset_stat_count_fallback (void);
extern void wset_stat_count_publish (bool is_ref);
extern void wset_stat_count_history_clear (void);
extern void wset_stat_count_clear_capacity (void);
extern void wset_stat_count_conflict (bool from_ref);
extern void wset_stat_count_commit (bool fallback, bool key_limit);

/* SHOW WRITESET STATUS: the writeset statistics of this server, counted since it started */
extern int wset_status_start_scan (THREAD_ENTRY *thread_p, int type, DB_VALUE **arg_values, int arg_cnt,
				   void **ptr);

#endif /* _WRITESET_STAT_HPP_ */
