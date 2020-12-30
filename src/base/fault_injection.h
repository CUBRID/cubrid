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
 * fault_injection.h :
 *
 */

#ifndef _FAULT_INJECTION_H_
#define _FAULT_INJECTION_H_

#ident "$Id$"

#include "error_manager.h"
#include "thread_compat.hpp"

#if !defined(NDEBUG)
#define FI_INSERTED(code) 		fi_test_on(code)
#define FI_SET(th, code, state)		fi_set(th, code, state)
#define FI_RESET(th, code)		fi_reset(th, code)
#define FI_TEST(th, code, state) 	fi_test(th, code, NULL, state, ARG_FILE_LINE)
#define FI_TEST_ARG(th, code, arg, state) fi_test(th, code, arg, state, ARG_FILE_LINE)
#else
#define FI_INSERTED(code) 0
#define FI_SET(th, code, state)
#define FI_RESET(th, code)
#define FI_TEST(th, code, state) (NO_ERROR)
#define FI_TEST_ARG(th, code, arg, state) (NO_ERROR)
#endif


typedef enum
{
  /* todo: if we want to do regression tests with fault-injection, we shouldn't set codes in system parameter.
   *       it's better to have a string that we can parse and offers us more flexibility into changing/extending
   *       functionality without worrying of backward compatibility.
   */

  FI_TEST_NONE = 0,

  /* common */
  FI_TEST_HANG = 1,

  /* IO & DISK MANAGER */
  FI_TEST_FILE_IO_FORMAT = 100000,
  FI_TEST_DISK_MANAGER_VOLUME_ADD = 100001,
  FI_TEST_DISK_MANAGER_VOLUME_EXPAND = 100002,
  FI_TEST_FILE_IO_WRITE_PARTS1 = 100003,
  FI_TEST_FILE_IO_WRITE_PARTS2 = 100004,

  /* FILE MANAGER */
  FI_TEST_FILE_MANAGER_UNDO_TRACKER_REGISTER = 200000,	/* unused */

  /* BTREE MANAGER */
  FI_TEST_BTREE_MANAGER_RANDOM_EXIT = 300000,
  FI_TEST_BTREE_MANAGER_PAGE_DEALLOC_FAIL = 300001,

  /* QUERY MANAGER (start number is 400000) */

  /* LOG MANAGER */
  FI_TEST_LOG_MANAGER_RANDOM_EXIT_AT_RUN_POSTPONE = 500000,
  FI_TEST_LOG_MANAGER_RANDOM_EXIT_AT_END_SYSTEMOP = 500001,

  /* etc .... */

} FI_TEST_CODE;

typedef enum
{
  FI_GROUP_NONE = 0,
  FI_GROUP_RECOVERY = 1,
  FI_GROUP_MAX = FI_GROUP_RECOVERY
} FI_GROUP_CODE;

#define FI_INIT_STATE 0


typedef int (*FI_HANDLER_FUNC) (THREAD_ENTRY * thread_p, void *arg, const char *caller_file, const int caller_line);

typedef struct fi_test_item FI_TEST_ITEM;
struct fi_test_item
{
  FI_TEST_CODE code;
  FI_HANDLER_FUNC func;
  int state;
};

extern int fi_thread_init (THREAD_ENTRY * thread_p);
extern int fi_thread_final (THREAD_ENTRY * thread_p);
extern int fi_set (THREAD_ENTRY * thread_p, FI_TEST_CODE code, int state);
extern int fi_set_force (THREAD_ENTRY * thread_p, FI_TEST_CODE code, int state);
extern void fi_reset (THREAD_ENTRY * thread_p, FI_TEST_CODE code);
extern int fi_test (THREAD_ENTRY * thread_p, FI_TEST_CODE code, void *arg, int state, const char *caller_file,
		    const int caller_line);
extern int fi_state (THREAD_ENTRY * thread_p, FI_TEST_CODE code);
extern bool fi_test_on (FI_TEST_CODE code);

extern FI_TEST_CODE *fi_Groups[FI_GROUP_MAX + 1];

#endif /* _FAULT_INJECTION_H_ */
