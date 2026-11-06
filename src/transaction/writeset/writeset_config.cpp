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
 * writeset_config.cpp - cached writeset configuration
 */

#ident "$Id$"

#include "config.h"

#include "writeset_config.hpp"

#include "system_parameter.h"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

#if defined(SERVER_MODE) || defined(SA_MODE)

static INT64 wset_Key_limit = 0;
static int wset_Trace_level = 0;

void
wset_config_initialize (void)
{
  wset_Key_limit = (INT64) prm_get_integer_value (PRM_ID_HA_WRITESET_HISTORY_SIZE);
  wset_Trace_level = prm_get_integer_value (PRM_ID_HA_WRITESET_TRACE_LEVEL);
}

void
wset_config_finalize (void)
{
  wset_Key_limit = 0;
  wset_Trace_level = 0;
}

INT64
wset_config_get_key_limit (void)
{
  return wset_Key_limit;
}

int
wset_config_get_trace_level (void)
{
  return wset_Trace_level;
}

#endif /* SERVER_MODE || SA_MODE */
