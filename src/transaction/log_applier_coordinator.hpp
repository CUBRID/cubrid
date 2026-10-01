/*
 * Copyright 2008 Search Solution Corporation
 * Copyright 2016 CUBRID Corporation
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 */

#ifndef _LOG_APPLIER_COORDINATOR_HPP_
#define _LOG_APPLIER_COORDINATOR_HPP_

#include "log_applier_dispatch_order.hpp"
#include "log_applier_worker.hpp"

#include <cstddef>

struct la_coordinator;

int la_coordinator_create (std::size_t worker_count, std::size_t task_queue_capacity,
			   std::size_t dispatch_order_capacity, const la_worker_operations &worker_operations,
			   void *shared_context, la_coordinator **coordinator_out);
int la_coordinator_start_workers (la_coordinator *coordinator);
void la_coordinator_stop_workers (la_coordinator *coordinator);
void la_coordinator_destroy (la_coordinator *coordinator);
la_dispatch_status la_coordinator_try_enqueue_task (la_coordinator *coordinator, la_apply_task &task);
int la_coordinator_collect_results (la_coordinator *coordinator, std::size_t *collected_count);
bool la_coordinator_try_retire_task (la_coordinator *coordinator, la_retired_task &retired);
std::size_t la_coordinator_unretired_count (const la_coordinator *coordinator);

#endif /* _LOG_APPLIER_COORDINATOR_HPP_ */
