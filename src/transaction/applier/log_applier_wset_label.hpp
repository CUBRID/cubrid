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

/*
 * log_applier_wset_label.hpp - interface for the pending writeset label
 *
 * The coordinator stores a LOG_DUMMY_WSET_LABEL here until the following COMMIT or SYSOP_END of the
 * same transaction consumes it. Only the coordinator accesses the holder, so it needs no lock.
 */

#ifndef _LOG_APPLIER_WSET_LABEL_HPP_
#define _LOG_APPLIER_WSET_LABEL_HPP_

#include "log_lsa.hpp"

extern void la_wset_label_store (int tranid, const LOG_LSA *dependency_seq);
extern bool la_wset_label_matches (int tranid);
extern bool la_wset_label_take (int tranid, LOG_LSA *dependency_seq);

#endif /* _LOG_APPLIER_WSET_LABEL_HPP_ */
