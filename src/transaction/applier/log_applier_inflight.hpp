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
 * log_applier_inflight.hpp - in-flight list: tasks handed to the workers and not yet retired
 */

#ifndef _LOG_APPLIER_INFLIGHT_HPP_
#define _LOG_APPLIER_INFLIGHT_HPP_

#include "log_applier_result.hpp"	/* LA_RESULT */

/* repl_list a dispatched task applies; defined in log_applier.c. An entry only holds a pointer to it. */
typedef struct la_apply LA_APPLY;

typedef struct la_inflight_entry LA_INFLIGHT_ENTRY;

extern int la_inflight_init (int capacity);
extern void la_inflight_clear (void);
extern int la_inflight_get_capacity (void);
extern int la_inflight_push (LA_APPLY *apply, int tranid, int rectype, UINT64 *seq_out);
extern LA_INFLIGHT_ENTRY *la_inflight_peek (void);
extern void la_inflight_pop (void);
extern int la_inflight_attach_result (const LA_RESULT *result);
extern int la_inflight_get_count (void);
extern LA_INFLIGHT_ENTRY *la_inflight_entry_at (int pos);

extern UINT64 la_inflight_entry_get_sequence (const LA_INFLIGHT_ENTRY *entry);
extern LA_APPLY *la_inflight_entry_get_apply (const LA_INFLIGHT_ENTRY *entry);
extern LA_APPLY *la_inflight_entry_take_apply (LA_INFLIGHT_ENTRY *entry);
extern int la_inflight_entry_get_tranid (const LA_INFLIGHT_ENTRY *entry);
extern int la_inflight_entry_get_rectype (const LA_INFLIGHT_ENTRY *entry);
extern bool la_inflight_entry_has_result (const LA_INFLIGHT_ENTRY *entry);
extern const LA_RESULT *la_inflight_entry_get_result (const LA_INFLIGHT_ENTRY *entry);

#endif /* _LOG_APPLIER_INFLIGHT_HPP_ */
