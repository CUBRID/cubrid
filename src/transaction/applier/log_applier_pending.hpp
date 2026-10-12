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
 * log_applier_pending.hpp - transactions held back by the dependency gate until their predecessors are applied
 */

#ifndef _LOG_APPLIER_PENDING_HPP_
#define _LOG_APPLIER_PENDING_HPP_

#include "log_applier_task.hpp"		/* LA_TASK */

extern int la_pending_push (const LA_TASK *task);
extern bool la_pending_pop_ready (LA_TASK *task);
extern void la_pending_clear (void);
extern bool la_pending_is_empty (void);
extern void la_pending_foreach (void (*fn) (const LA_TASK * task, void *arg), void *arg);

#endif /* _LOG_APPLIER_PENDING_HPP_ */
