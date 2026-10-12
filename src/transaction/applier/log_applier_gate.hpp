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
 * log_applier_gate.hpp - dependency gate that holds a transaction back until what it conflicts with is applied
 */

#ifndef _LOG_APPLIER_GATE_HPP_
#define _LOG_APPLIER_GATE_HPP_

#include "log_lsa.hpp"

extern int la_gate_init (int commit_order_capacity);
extern int la_gate_order_get_count (void);
extern void la_gate_set_initial_frontier (const LOG_LSA *restart_lsa);
extern bool la_gate_get_frontier (LOG_LSA *frontier);
extern bool la_gate_get_order_tail (LOG_LSA *commit_lsa);
extern int la_gate_order_push (const LOG_LSA *commit_lsa);
extern void la_gate_advance_frontier (void);
extern void la_gate_mark_completed (const LOG_LSA *commit_seq);
extern bool la_gate_is_satisfied (const LOG_LSA *dep);

#endif /* _LOG_APPLIER_GATE_HPP_ */
