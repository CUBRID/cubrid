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
 * writeset_order.hpp - transaction ordering with writeset history
 */

#ifndef _WRITESET_ORDER_HPP_
#define _WRITESET_ORDER_HPP_

#ident "$Id$"

#include "log_lsa.hpp"

typedef struct log_tdes LOG_TDES;

extern void wset_order_find_dependency (LOG_TDES *tdes, LOG_LSA *dependency_out);
extern void wset_order_record_commit (LOG_TDES *tdes, const LOG_LSA *commit_lsa);
extern void wset_order_publish_commit_keys (LOG_TDES *tdes, const LOG_LSA *commit_lsa);
extern void wset_order_find_sysop_dependency (LOG_TDES *tdes, LOG_LSA *dependency_out);
extern void wset_order_advance_barrier (const LOG_LSA *lsa, const char *reason);

#endif /* _WRITESET_ORDER_HPP_ */
