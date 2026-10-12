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
 * log_applier_dispatch_order.hpp - dispatch-order FIFO matching worker results to dispatched tasks
 */

#ifndef _LOG_APPLIER_DISPATCH_ORDER_HPP_
#define _LOG_APPLIER_DISPATCH_ORDER_HPP_

#include "log_applier_result.hpp"	/* LA_RESULT */

#include <sys/time.h>

/* repl_list a dispatched task applies; defined in log_applier.c. An entry only holds a pointer to it. */
typedef struct la_apply LA_APPLY;

typedef struct la_dispatch_entry LA_DISPATCH_ENTRY;
struct la_dispatch_entry
{
  UINT64 seq;
  LA_APPLY *apply;		/* repl_list slot freed only when this entry retires, so the coordinator cannot reuse it
				 * while the result is still pending */
  int tranid;
  int rectype;
  bool result_ready;
  LA_RESULT result;
};

extern int la_dispatch_order_init (int capacity);
extern int la_dispatch_order_get_capacity (void);
extern int la_dispatch_order_push (LA_APPLY *apply, int tranid, int rectype, UINT64 *seq_out);
extern LA_DISPATCH_ENTRY *la_dispatch_order_peek (void);
extern void la_dispatch_order_pop (void);
extern int la_dispatch_order_attach_result (const LA_RESULT *result);
extern int la_dispatch_order_get_count (void);

#endif /* _LOG_APPLIER_DISPATCH_ORDER_HPP_ */
