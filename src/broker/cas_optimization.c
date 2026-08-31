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
 * cas_optimization.c -
 * Optimization level management functions
 * Used for query plan dumping and SQL logging
 */

#ident "$Id$"

#include "system_parameter.h"
#include "cas_optimization.h"
#include "cas_common_vars.h"
#include "optimizer.h"

/* CAS_TLS: in the merged server one driver-session thread speaks for one
 * session, and the CAS original kept this per-process */
static CAS_TLS int saved_Optimization_level = -1;

void
set_optimization_level (int level)
{
  /* through the optimizer's setter: in the merged server the level must land
   * on the session override slot qo_get_optimization_param prefers — a plain
   * sysprm write is invisible to this session's optimizer (session-parameter
   * read-through) and would race every other session.  Outside a bracket the
   * setter is the sysprm write the CAS original did. */
  qo_set_optimization_param (&saved_Optimization_level, QO_PARAM_LEVEL, level);
}

void
reset_optimization_level_as_saved (void)
{
  int level = CHK_OPTIMIZATION_LEVEL_VALID (saved_Optimization_level) ? saved_Optimization_level : 1;

  qo_set_optimization_param (NULL, QO_PARAM_LEVEL, level);
  saved_Optimization_level = -1;
}
