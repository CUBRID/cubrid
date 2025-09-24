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
 * px_sort.cpp
 */

#include "external_sort.h"
#include "px_sort.h"
#include "thread_entry_task.hpp"
#include "error_manager.h"
#include "system_parameter.h"
#include "memory_alloc.h"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

#if defined(SERVER_MODE)
/*
 * sort_copy_sort_info () - copy sort info from src_info to dest_info
 *   return: NO_ERROR
 *   dest_param(in):
 *   src_param(in):
 *   parallel_num(in):
 */
int
sort_copy_sort_info (THREAD_ENTRY * thread_p, SORT_INFO ** dest_sort_info, SORT_INFO * src_sort_info)
{
  int error = NO_ERROR;
  SORT_INFO *sort_info;

  sort_info = *dest_sort_info = (SORT_INFO *) db_private_alloc (thread_p, sizeof (SORT_INFO));
  if (sort_info == NULL)
    {
      error = ER_OUT_OF_VIRTUAL_MEMORY;
      goto end;
    }
  memcpy (sort_info, src_sort_info, sizeof (SORT_INFO));

  sort_info->s_id = (QFILE_SORT_SCAN_ID *) db_private_alloc (thread_p, sizeof (QFILE_SORT_SCAN_ID));
  if (sort_info->s_id == NULL)
    {
      error = ER_OUT_OF_VIRTUAL_MEMORY;
      goto end;
    }
  memcpy (sort_info->s_id, src_sort_info->s_id, sizeof (QFILE_SORT_SCAN_ID));

  sort_info->s_id->s_id = (QFILE_LIST_SCAN_ID *) db_private_alloc (thread_p, sizeof (QFILE_LIST_SCAN_ID));
  if (sort_info->s_id->s_id == NULL)
    {
      error = ER_OUT_OF_VIRTUAL_MEMORY;
      goto end;
    }
  memcpy (sort_info->s_id->s_id, src_sort_info->s_id->s_id, sizeof (QFILE_LIST_SCAN_ID));

  sort_info->output_file = NULL;
  sort_info->input_file = NULL;

end:
  if (error != NO_ERROR)
    {
      /* free memory */
      if (sort_info != NULL)
	{
	  if (sort_info->s_id != NULL)
	    {
	      if (sort_info->s_id->s_id != NULL)
		{
		  db_private_free_and_init (thread_p, sort_info->s_id->s_id);
		}
	      db_private_free_and_init (thread_p, sort_info->s_id);
	    }
	  db_private_free_and_init (thread_p, sort_info);
	}
    }

  return error;
}

#endif
