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
 */

#ifndef _LOG_APPLIER_QUEUE_HPP_
#define _LOG_APPLIER_QUEUE_HPP_

#include "log_applier_task.hpp"

#include <cstddef>

struct la_task_queue;
struct la_result_queue;

enum la_queue_enqueue_status
{
  LA_QUEUE_ENQUEUE_ACCEPTED = 0,
  LA_QUEUE_ENQUEUE_FULL,
  LA_QUEUE_ENQUEUE_STOPPED
};

int la_task_queue_create (std::size_t capacity, la_task_queue **queue_out);
void la_task_queue_destroy (la_task_queue *queue);
la_queue_enqueue_status la_task_queue_try_enqueue (la_task_queue *queue, const la_apply_task &task);
bool la_task_queue_dequeue (la_task_queue *queue, la_apply_task &task);
void la_task_queue_shutdown (la_task_queue *queue);

int la_result_queue_create (la_result_queue **queue_out);
void la_result_queue_destroy (la_result_queue *queue);
void la_result_queue_enqueue (la_result_queue *queue, const la_apply_result &result);
bool la_result_queue_try_dequeue (la_result_queue *queue, la_apply_result &result);

#endif /* _LOG_APPLIER_QUEUE_HPP_ */
