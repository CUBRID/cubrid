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
 * lock_table.h - Definitions and data types of lock table
 */

#ifndef _LOCK_TABLE_H_
#define _LOCK_TABLE_H_

#include "storage_common.h" // LOCK, LOCK_COMPATIBILITY

extern const LOCK lock_Conv[LOCK_COUNT][LOCK_COUNT];
extern const LOCK_COMPATIBILITY lock_Comp[LOCK_COUNT][LOCK_COUNT];
extern const char* lock_mode_string[LOCK_COUNT];

inline LOCK
lock_conv (LOCK requested, LOCK current) {
  assert (lock_Conv[requested][current] != NA_LOCK);
  return lock_Conv[requested][current];
}

inline LOCK_COMPATIBILITY
lock_compat (LOCK requested, LOCK current) {
  assert (lock_Comp[requested][current] != LOCK_COMPAT_UNKNOWN);
  return lock_Comp[requested][current];
}

inline const char*
LOCK_TO_LOCKMODE_STRING (LOCK lock) {
  assert (lock >= 0 && lock < LOCK_COUNT);
  return lock_mode_string[(int) lock];
}

#endif // _LOCK_TABLE_H_
