/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 */

/*
 * show_scan.c - scan information for show statements
 */

#ident "$Id$"


#include "config.h"

#include <stdio.h>
#include <string.h>

#include "porting.h"
#include "error_manager.h"
#include "memory_alloc.h"


#include "query_manager.h"
#include "object_primitive.h"
#include "scan_manager.h"

#include "disk_manager.h"
#include "log_manager.h"
#include "slotted_page.h"
#include "heap_file.h"
#include "btree.h"

#if defined(SERVER_MODE)
#include "thread.h"
#endif /* SERVER_MODE */

#if defined(ENABLE_SYSTEMTAP)
#include "probes.h"
#endif /* ENABLE_SYSTEMTAP */

typedef SCAN_CODE (*NEXT_SCAN_FUNC) (THREAD_ENTRY * thread_p, int cursor,
				     DB_VALUE ** out_values, int out_cnt,
				     void *ctx);
typedef int (*START_SCAN_FUNC) (THREAD_ENTRY * thread_p, int show_type,
				DB_VALUE ** arg_values, int arg_cnt,
				void **ctx);
typedef int (*END_SCAN_FUNC) (THREAD_ENTRY * thread_p, void **ctx);

typedef struct show_request SHOW_REQUEST;
struct show_request
{
  SHOWSTMT_TYPE show_type;	/* show stmt type */
  START_SCAN_FUNC start_func;	/* start scan function */
  NEXT_SCAN_FUNC next_func;	/* next scan function */
  END_SCAN_FUNC end_func;	/* end scan function */
};

static bool show_scan_Inited = false;

static SHOW_REQUEST show_Requests[SHOWSTMT_END];

/*
 *  showstmt_scan_init () - initialize the scan functions of 
 *                          show statments.
 *   return: NULL
 */
void
showstmt_scan_init (void)
{
  SHOW_REQUEST *req;

  if (show_scan_Inited)
    {
      return;
    }

  memset (show_Requests, 0, SHOWSTMT_END * sizeof (SHOW_REQUEST));

  req = &show_Requests[SHOWSTMT_VOLUME_HEADER];
  req->show_type = SHOWSTMT_VOLUME_HEADER;
  req->start_func = disk_volume_header_start_scan;
  req->next_func = disk_volume_header_next_scan;
  req->end_func = disk_volume_header_end_scan;

  /* append to init other show statement scan function here */

  show_scan_Inited = true;
}

/*
 *  showstmt_next_scan () - scan values from different show statment.
 *   return: NO_ERROR, or ER_code
 *   thread_p(in):
 *   s_id(in):
 */
SCAN_CODE
showstmt_next_scan (THREAD_ENTRY * thread_p, SCAN_ID * s_id)
{
  SHOWSTMT_SCAN_ID *stsidp = &s_id->s.stsid;
  SHOWSTMT_TYPE show_type = stsidp->show_type;
  NEXT_SCAN_FUNC next_func = NULL;
  SCAN_CODE code;
  int i;

  next_func = show_Requests[show_type].next_func;
  assert (next_func != NULL
	  && show_type == show_Requests[show_type].show_type);

  /* free values which need be cleared */
  for (i = 0; i < stsidp->out_cnt; i++)
    {
      pr_clear_value (stsidp->out_values[i]);
    }
  code = (*next_func) (thread_p, stsidp->cursor++, stsidp->out_values,
		       stsidp->out_cnt, stsidp->ctx);
  return code;
}

/*
 *  showstmt_start_scan () - before scan.
 *   return: NO_ERROR, or ER_code
 *   thread_p(in): 
 *   s_id(in):
 */
int
showstmt_start_scan (THREAD_ENTRY * thread_p, SCAN_ID * s_id)
{
  SHOWSTMT_SCAN_ID *stsidp = &s_id->s.stsid;
  SHOWSTMT_TYPE show_type = stsidp->show_type;
  START_SCAN_FUNC start_func = NULL;
  int error;

  assert (show_type == show_Requests[show_type].show_type);
  start_func = show_Requests[show_type].start_func;
  if (start_func == NULL)
    {
      return NO_ERROR;
    }

  error = (*start_func) (thread_p, (int) show_type, stsidp->arg_values,
			 stsidp->arg_cnt, &stsidp->ctx);
  return error;
}

/*
 *  showstmt_end_scan () - after scan.
 *   return: NO_ERROR, or ER_code
 *   thread_p(in): 
 *   s_id(in):
 */
int
showstmt_end_scan (THREAD_ENTRY * thread_p, SCAN_ID * s_id)
{
  SHOWSTMT_SCAN_ID *stsidp = &s_id->s.stsid;
  SHOWSTMT_TYPE show_type = stsidp->show_type;
  END_SCAN_FUNC end_func = NULL;
  int error;

  assert (show_type == show_Requests[show_type].show_type);
  end_func = show_Requests[show_type].end_func;
  if (end_func == NULL)
    {
      return NO_ERROR;
    }
  error = (*end_func) (thread_p, &stsidp->ctx);
  return error;
}
