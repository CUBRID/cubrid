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
 * writeset_config.hpp - cached writeset configuration
 */

#ifndef _WRITESET_CONFIG_HPP_
#define _WRITESET_CONFIG_HPP_

#ident "$Id$"

#include "storage_common.h"

extern void wset_config_initialize (void);
extern void wset_config_finalize (void);
extern INT64 wset_config_get_key_limit (void);
extern int wset_config_get_trace_level (void);

#endif /* _WRITESET_CONFIG_HPP_ */
