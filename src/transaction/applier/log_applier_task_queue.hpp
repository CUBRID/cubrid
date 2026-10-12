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
 * log_applier_task_queue.hpp - shared task queue feeding the parallel applier workers
 */

#ifndef _LOG_APPLIER_TASK_QUEUE_HPP_
#define _LOG_APPLIER_TASK_QUEUE_HPP_

#include "log_applier_task.hpp"		/* LA_TASK */

extern void la_task_queue_init (void);
extern int la_task_queue_push (const LA_TASK *task);
extern bool la_task_queue_is_full (void);
extern int la_task_queue_pop (LA_TASK *task);
extern void la_task_queue_signal_shutdown (void);
extern int la_task_queue_get_count (void);

#endif /* _LOG_APPLIER_TASK_QUEUE_HPP_ */
