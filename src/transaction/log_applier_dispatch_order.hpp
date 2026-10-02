/*
 * Copyright 2008 Search Solution Corporation
 * Copyright 2016 CUBRID Corporation
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 */

#ifndef _LOG_APPLIER_DISPATCH_ORDER_HPP_
#define _LOG_APPLIER_DISPATCH_ORDER_HPP_

#include "log_applier_queue.hpp"

#include <cstddef>

/* Tracks tasks accepted by the shared worker queue, matches asynchronous results by sequence,
 * and exposes completed tasks in dispatch order. It does not evaluate dependencies or select a worker. */
struct la_dispatch_order;

enum la_dispatch_status
{
  LA_DISPATCH_ACCEPTED = 0,
  LA_DISPATCH_FULL,
  LA_DISPATCH_STOPPED,
  LA_DISPATCH_LIMIT_REACHED
};

struct la_retired_task
{
  la_apply *apply;
  int record_type;
  la_apply_result result;
};

int la_dispatch_order_create (std::size_t capacity, la_dispatch_order **order_out);
void la_dispatch_order_destroy (la_dispatch_order *order);
la_dispatch_status la_dispatch_order_try_enqueue_task (la_dispatch_order *order, la_task_queue *task_queue,
    la_apply_task &task);
int la_dispatch_order_record_task_result (la_dispatch_order *order, const la_apply_result &result);
bool la_dispatch_order_try_retire_task (la_dispatch_order *order, la_retired_task &retired);
std::size_t la_dispatch_order_unretired_count (const la_dispatch_order *order);

#endif /* _LOG_APPLIER_DISPATCH_ORDER_HPP_ */
