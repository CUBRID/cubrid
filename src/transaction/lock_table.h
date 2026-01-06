/*
 *
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
 * lock_table.h - Lock conversion and compatibility tables and inline accessors.
 */

#ifndef _LOCK_TABLE_H_
#define _LOCK_TABLE_H_

#include "storage_common.h"	// LOCK, LOCK_COMPATIBILITY

extern const LOCK lock_Conv[LOCK_COUNT][LOCK_COUNT];
extern const LOCK_COMPATIBILITY lock_Comp[LOCK_COUNT][LOCK_COUNT];

inline LOCK
lock_conv (LOCK requested, LOCK current)
{
  assert (lock_Conv[requested][current] != NA_LOCK);
  return lock_Conv[requested][current];
}

inline LOCK_COMPATIBILITY
lock_compat (LOCK requested, LOCK current)
{
  assert (lock_Comp[requested][current] != LOCK_COMPAT_UNKNOWN);
  return lock_Comp[requested][current];
}

inline const char *
lock_to_lockmode_string (LOCK lock)
{
  assert (lock >= NULL_LOCK && lock < LOCK_COUNT);
  switch (lock)
    {
    case NULL_LOCK:
      return "NULL_LOCK";
    case IS_LOCK:
      return "IS_LOCK";
    case S_LOCK:
      return "S_LOCK";
    case IX_LOCK:
      return "IX_LOCK";
    case SIX_LOCK:
      return "SIX_LOCK";
    case U_LOCK:
      return "U_LOCK";
    case BU_LOCK:
      return "BU_LOCK";
    case SCH_S_LOCK:
      return "SCH_S_LOCK";
    case SCH_M_LOCK:
      return "SCH_M_LOCK";
    case X_LOCK:
      return "X_LOCK";
    default:			// NA_LOCK, 
      return "UNKNOWN";
    }
}

#endif // _LOCK_TABLE_H_
