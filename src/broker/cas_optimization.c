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

static int saved_Optimization_level = -1;

void
set_optimization_level (int level)
{
  saved_Optimization_level = prm_get_integer_value (PRM_ID_OPTIMIZATION_LEVEL);
  prm_set_integer_value (PRM_ID_OPTIMIZATION_LEVEL, level);
}

void
reset_optimization_level_as_saved (void)
{
  if (CHK_OPTIMIZATION_LEVEL_VALID (saved_Optimization_level))
    {
      prm_set_integer_value (PRM_ID_OPTIMIZATION_LEVEL, saved_Optimization_level);
    }
  else
    {
      prm_set_integer_value (PRM_ID_OPTIMIZATION_LEVEL, 1);
    }
  saved_Optimization_level = -1;
}
