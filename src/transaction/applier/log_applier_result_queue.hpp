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
 * log_applier_result_queue.hpp - shared result queue returning worker results to the coordinator
 */

#ifndef _LOG_APPLIER_RESULT_QUEUE_HPP_
#define _LOG_APPLIER_RESULT_QUEUE_HPP_

#include "log_applier_result.hpp"	/* LA_RESULT */

extern int la_result_queue_init (int capacity);
extern int la_result_queue_push (const LA_RESULT *result);
extern int la_result_queue_try_pop (LA_RESULT *result);
extern int la_result_queue_pop (LA_RESULT *result);
extern bool la_result_queue_wait_for_result (int timeout_msec);
extern void la_result_queue_signal_shutdown (void);
extern int la_result_queue_get_count (void);

#endif /* _LOG_APPLIER_RESULT_QUEUE_HPP_ */
