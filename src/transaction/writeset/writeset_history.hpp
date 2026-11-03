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
 * writeset_history.hpp - global writeset commit history
 */

#ifndef _WRITESET_HISTORY_HPP_
#define _WRITESET_HISTORY_HPP_

#ident "$Id$"

#include "storage_common.h"

extern void wset_history_initialize (void);
extern void wset_history_finalize (void);
extern void wset_history_get_usage (INT64 *history_size, INT64 *entry_count);

#endif /* _WRITESET_HISTORY_HPP_ */
