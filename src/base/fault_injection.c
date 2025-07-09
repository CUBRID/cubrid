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
 * fault_injection.c :
 *
 */

#ident "$Id$"

#include "fault_injection.h"

#if defined (SERVER_MODE) || defined (SA_MODE)
#include "log_impl.h"
#endif /* defined (SERVER_MODE) || defined (SA_MODE) */
#include "porting.h"
#include "system_parameter.h"
#if defined (SERVER_MODE) || defined (SA_MODE)
#include "thread_manager.hpp"	// for thread_get_thread_entry_info
#endif /* defined (SERVER_MODE) || defined (SA_MODE) */

#include <assert.h>
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

#if !defined(NDEBUG)

static int fi_handler_exit (THREAD_ENTRY * thread_p, void *arg, const char *caller_file, const int caller_line);
static int fi_handler_random_exit (THREAD_ENTRY * thread_p, void *arg, const char *caller_file, const int caller_line);
static int fi_handler_random_fail (THREAD_ENTRY * thread_p, void *arg, const char *caller_file, const int caller_line);
static int fi_handler_hang (THREAD_ENTRY * thread_p, void *arg, const char *caller_file, const int caller_line);

static FI_TEST_ITEM *fi_code_item (THREAD_ENTRY * thread_p, FI_TEST_CODE code);

/******************************************************************************
 *
 * FI test scenario array
 *
 * Register new scenario in here with new FI_TEST_CODE & handler function
 *
 *******************************************************************************/
FI_TEST_ITEM fi_Test_array[] = {
  {FI_TEST_HANG, fi_handler_hang, FI_INIT_STATE},
  {FI_TEST_FILE_IO_FORMAT, fi_handler_random_exit, FI_INIT_STATE},
  {FI_TEST_DISK_MANAGER_VOLUME_ADD, fi_handler_random_exit, FI_INIT_STATE},
  {FI_TEST_DISK_MANAGER_VOLUME_EXPAND, fi_handler_random_exit, FI_INIT_STATE},
  {FI_TEST_FILE_IO_WRITE_PARTS1, fi_handler_random_exit, FI_INIT_STATE},
  {FI_TEST_FILE_IO_WRITE_PARTS2, fi_handler_random_exit, FI_INIT_STATE},
  {FI_TEST_FILE_MANAGER_UNDO_TRACKER_REGISTER, fi_handler_exit, FI_INIT_STATE},
  {FI_TEST_BTREE_MANAGER_RANDOM_EXIT, fi_handler_random_exit, FI_INIT_STATE},
  {FI_TEST_LOG_MANAGER_RANDOM_EXIT_AT_RUN_POSTPONE, fi_handler_random_exit, FI_INIT_STATE},
  {FI_TEST_LOG_MANAGER_RANDOM_EXIT_AT_END_SYSTEMOP, fi_handler_random_exit, FI_INIT_STATE},
  {FI_TEST_BTREE_MANAGER_PAGE_DEALLOC_FAIL, fi_handler_random_fail, FI_INIT_STATE}
};

FI_TEST_CODE fi_Group_none[] = {
  FI_TEST_NONE
};

FI_TEST_CODE fi_Group_recovery[] = {
  FI_TEST_LOG_MANAGER_RANDOM_EXIT_AT_RUN_POSTPONE,
  FI_TEST_LOG_MANAGER_RANDOM_EXIT_AT_END_SYSTEMOP,
  FI_TEST_BTREE_MANAGER_RANDOM_EXIT,
  FI_TEST_BTREE_MANAGER_PAGE_DEALLOC_FAIL,
  FI_TEST_NONE
};

FI_TEST_CODE *fi_Groups[FI_GROUP_MAX + 1] = {
  fi_Group_none,
  fi_Group_recovery
};

/*
 * fi_thread_init -
 *
 * return: NO_ERROR or ER_FAILED
 *
 *   thread_p(in):
 */
int
fi_thread_init (THREAD_ENTRY * thread_p)
{
  FI_TEST_ITEM *fi_test_array = NULL;
  unsigned int i;


#if defined (SERVER_MODE)
  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
    }
  if (thread_p == NULL)
    {
      assert (thread_p != NULL);

      return ER_FAILED;
    }

  if (thread_p->fi_test_array == NULL)
    {
      thread_p->fi_test_array = (FI_TEST_ITEM *) malloc (sizeof (fi_Test_array));
      if (thread_p->fi_test_array == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (fi_Test_array));
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}
    }

  fi_test_array = thread_p->fi_test_array;

  memcpy (fi_test_array, fi_Test_array, sizeof (fi_Test_array));

#else
  fi_test_array = fi_Test_array;
#endif

  for (i = 0; i < DIM (fi_Test_array); i++)
    {
      fi_test_array[i].state = FI_INIT_STATE;
    }

  return NO_ERROR;
}

/*
 * fi_thread_final -
 *
 * return: NO_ERROR or ER_FAILED
 *
 *   thread_p(in):
 */
int
fi_thread_final (THREAD_ENTRY * thread_p)
{
#if defined (SERVER_MODE)
  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
    }
  if (thread_p == NULL)
    {
      assert (thread_p != NULL);

      return ER_FAILED;
    }

  if (thread_p->fi_test_array != NULL)
    {
      free_and_init (thread_p->fi_test_array);
    }
#endif

  return NO_ERROR;
}

/*
 * fi_code_item -
 *
 * return: NO_ERROR or ER_FAILED
 *
 *   code(in):
 *   state(in):
 */
static FI_TEST_ITEM *
fi_code_item (THREAD_ENTRY * thread_p, FI_TEST_CODE code)
{
  FI_TEST_ITEM *fi_test_array;
  FI_TEST_ITEM *item;
  unsigned int i;

#if defined(SERVER_MODE)
  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
    }
  if (thread_p == NULL)
    {
      assert (thread_p != NULL);

      return NULL;
    }

  fi_test_array = thread_p->fi_test_array;
#else
  fi_test_array = fi_Test_array;
#endif

  item = NULL;
  for (i = 0; i < DIM (fi_Test_array); i++)
    {
      if (fi_test_array[i].code == code)
	{
	  item = &fi_test_array[i];
	  break;
	}
    }

  assert (item != NULL);
  return item;
}

/*
 * fi_set -
 *
 * return: NO_ERROR or ER_FAILED
 *
 *   code(in):
 *   state(in):
 */
int
fi_set (THREAD_ENTRY * thread_p, FI_TEST_CODE code, int state)
{
  FI_TEST_ITEM *item = NULL;


  if (sysprm_find_fi_code_in_integer_list (PRM_ID_FAULT_INJECTION_IDS, (int) code) == false)
    {
      return NO_ERROR;
    }

  item = fi_code_item (thread_p, code);
  if (item == NULL)
    {
      assert (item != NULL);
      return ER_FAILED;
    }

  if (item->state == state - 1)
    {
      item->state = state;
    }

  return NO_ERROR;
}

/*
 * fi_set_force -
 *
 * return: NO_ERROR or error code
 *
 *   code(in):
 *   state(in):
 */
int
fi_set_force (THREAD_ENTRY * thread_p, FI_TEST_CODE code, int state)
{
  FI_TEST_ITEM *item = NULL;

  if (sysprm_find_fi_code_in_integer_list (PRM_ID_FAULT_INJECTION_IDS, (int) code) == false)
    {
      return NO_ERROR;
    }

  item = fi_code_item (thread_p, code);
  if (item == NULL)
    {
      assert (item != NULL);
      return ER_FAILED;
    }

  item->state = state;

  return NO_ERROR;
}

/*
 * fi_reset -
 *
 * return:
 *
 *   code(in):
 */
void
fi_reset (THREAD_ENTRY * thread_p, FI_TEST_CODE code)
{
  FI_TEST_ITEM *item = NULL;

  item = fi_code_item (thread_p, code);
  item->state = FI_INIT_STATE;
}

/*
 * fi_test -
 *
 * return: NO_ERROR or error code
 *
 *   code(in):
 *   arg(in):
 *   state(in):
 */
int
fi_test (THREAD_ENTRY * thread_p, FI_TEST_CODE code, void *arg, int state, const char *caller_file,
	 const int caller_line)
{
  FI_TEST_ITEM *item = NULL;

  if (sysprm_find_fi_code_in_integer_list (PRM_ID_FAULT_INJECTION_IDS, (int) code) == false)
    {
      return NO_ERROR;
    }

  item = fi_code_item (thread_p, code);
  if (item == NULL)
    {
      assert (item != NULL);
      return ER_FAILED;
    }

  if (item->state == state)
    {
      return (*item->func) (thread_p, arg, caller_file, caller_line);
    }

  return NO_ERROR;
}

/*
 * fi_state -
 *
 * return:
 *
 *   code(in):
 */
int
fi_state (THREAD_ENTRY * thread_p, FI_TEST_CODE code)
{
  FI_TEST_ITEM *item = NULL;

  if (sysprm_find_fi_code_in_integer_list (PRM_ID_FAULT_INJECTION_IDS, (int) code) == false)
    {
      return FI_INIT_STATE;
    }

  item = fi_code_item (thread_p, code);
  assert (item != NULL);

  return item->state;
}

/*
 * fi_test_on -
 *
 * return: true or false
 *
 *   code(in):
 */
bool
fi_test_on (FI_TEST_CODE code)
{
  return sysprm_find_fi_code_in_integer_list (PRM_ID_FAULT_INJECTION_IDS, (int) code);
}


/*
 * fi_handler_exit -
 *
 * return: NO_ERROR
 *
 *   arg(in):
 */
static int
fi_handler_exit (THREAD_ENTRY * thread_p, void *arg, const char *caller_file, const int caller_line)
{
  exit (0);

  return NO_ERROR;
}

/*
 * fi_handler_hang -
 *
 * return: NO_ERROR
 *
 *   arg(in):
 */
static int
fi_handler_hang (THREAD_ENTRY * thread_p, void *arg, const char *caller_file, const int caller_line)
{
  while (true)
    {
      sleep (1);
    }

  return NO_ERROR;
}

static int
fi_handler_random_exit (THREAD_ENTRY * thread_p, void *arg, const char *caller_file, const int caller_line)
{
  static bool init = false;
  int r;
  int mod_factor;

  if (arg == NULL)
    {
      mod_factor = 20000;
    }
  else
    {
      mod_factor = *((int *) arg);
    }

  if (init == false)
    {
      srand ((unsigned int) time (NULL));
      init = true;
    }
  r = rand ();

#if 0
  if ((r % 10) == 0)
    {
      /* todo: what is the purpose of this? */
      LOG_CS_ENTER (thread_p);
      logpb_flush_pages_direct (thread_p);
      LOG_CS_EXIT (thread_p);
    }
#endif
  if ((r % mod_factor) == 0)
    {
      er_print_callstack (ARG_FILE_LINE, "FAULT INJECTION: RANDOM EXIT\n");
      er_set (ER_NOTIFICATION_SEVERITY, caller_file, caller_line, ER_FAILED_ASSERTION, 1,
	      "fault injection: random exit");

      if (prm_get_bool_value (PRM_ID_FAULT_INJECTION_ACTION_PREFER_ABORT_TO_EXIT))
	{
	  abort ();
	}
      else
	{
	  _exit (0);
	}
    }

  return NO_ERROR;
}

static int
fi_handler_random_fail (THREAD_ENTRY * thread_p, void *arg, const char *caller_file, const int caller_line)
{
  static bool init = false;
  int r;
  int mod_factor;

  if (arg == NULL)
    {
      mod_factor = 20000;
    }
  else
    {
      mod_factor = *((int *) arg);
    }

  if (init == false)
    {
      srand ((unsigned int) time (NULL));
      init = true;
    }
  r = rand ();

  if ((r % mod_factor) == 0)
    {
      er_set (ER_NOTIFICATION_SEVERITY, caller_file, caller_line, ER_FAILED_ASSERTION, 1,
	      "fault injection: random fail");

      return ER_FAILED;
    }

  return NO_ERROR;
}
#endif
