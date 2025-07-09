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
 * set_scan.c - Routines to implement set based derived tables.
 */

#ident "$Id$"

#include "config.h"

#include <string.h>

#include "set_scan.h"

#include "dbtype.h"
#include "fetch.h"
#include "xasl.h"
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

/*
 * qproc_next_set_scan () -
 *   return: SCAN_CODE
 *   s_id(in)   : s_id: Scan identifier
 *
 * Note: Move the current element to the next element in the set. Signal
 * scan end if at the end of the set.
 */
SCAN_CODE
qproc_next_set_scan (THREAD_ENTRY * thread_p, SCAN_ID * s_id)
{
  SET_SCAN_ID *set_id = &s_id->s.ssid;
  REGU_VARIABLE *func;
  DB_SET *setp;

  func = set_id->set_ptr;
  if (func->type == TYPE_FUNC && func->value.funcp->ftype == F_SEQUENCE)
    {
      /* if its the empty set, return end of scan */
      if (set_id->operand == NULL)
	{
	  return S_END;
	}

      if (s_id->position == S_BEFORE)
	{
	  s_id->position = S_ON;
	  set_id->cur_index = 0;

	  db_value_clear (s_id->val_list->valp->val);
	  if (fetch_copy_dbval (thread_p, &set_id->operand->value, s_id->vd, NULL, NULL, NULL,
				s_id->val_list->valp->val) != NO_ERROR)
	    {
	      return S_ERROR;
	    }
	  return S_SUCCESS;
	}
      else if (s_id->position == S_ON)
	{
	  set_id->cur_index++;
	  if (set_id->cur_index == set_id->set_card)
	    {
	      return S_END;
	    }
	  set_id->operand = set_id->operand->next;

	  db_value_clear (s_id->val_list->valp->val);
	  if (fetch_copy_dbval (thread_p, &set_id->operand->value, s_id->vd, NULL, NULL, NULL,
				s_id->val_list->valp->val) != NO_ERROR)
	    {
	      return S_ERROR;
	    }
	  return S_SUCCESS;
	}
      else if (s_id->position == S_AFTER)
	{
	  return S_END;
	}
      else
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_UNKNOWN_CRSPOS, 0);
	  return S_ERROR;
	}
    }
  else
    {
      /* if its the empty set, return end of scan */
      if (DB_IS_NULL (&set_id->set))
	{
	  return S_END;
	}
      setp = db_get_set (&set_id->set);
      if (!setp)
	{
	  return S_END;
	}
      set_id->set_card = db_set_size (setp);
      if (set_id->set_card == 0)
	{
	  return S_END;
	}

      if (s_id->position == S_BEFORE)
	{
	  s_id->position = S_ON;
	  set_id->cur_index = 0;

	  db_value_clear (s_id->val_list->valp->val);
	  if (db_set_get (setp, set_id->cur_index, s_id->val_list->valp->val) != NO_ERROR)
	    {
	      return S_ERROR;
	    }
	  return S_SUCCESS;
	}
      else if (s_id->position == S_ON)
	{
	  set_id->cur_index++;
	  if (set_id->cur_index == set_id->set_card)
	    {
	      return S_END;
	    }

	  db_value_clear (s_id->val_list->valp->val);
	  if (db_set_get (setp, set_id->cur_index, s_id->val_list->valp->val) != NO_ERROR)
	    {
	      return S_ERROR;
	    }
	  return S_SUCCESS;
	}
      else if (s_id->position == S_AFTER)
	{
	  return S_END;
	}
      else
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_UNKNOWN_CRSPOS, 0);
	  return S_ERROR;
	}
    }				/* else */

}
