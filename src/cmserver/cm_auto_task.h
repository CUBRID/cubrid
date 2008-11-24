/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution. 
 *
 *   This program is free software; you can redistribute it and/or modify 
 *   it under the terms of the GNU General Public License as published by 
 *   the Free Software Foundation; version 2 of the License. 
 *
 *  This program is distributed in the hope that it will be useful, 
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of 
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the 
 *  GNU General Public License for more details. 
 *
 *  You should have received a copy of the GNU General Public License 
 *  along with this program; if not, write to the Free Software 
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA 
 *
 */


/*
 * cm_auto_task.h -
 */

#ifndef _CM_AUTO_TASK_H_
#define _CM_AUTO_TASK_H_

#ident "$Id$"

#include "cm_nameval.h"
#include "cm_version.h"
#ifdef DIAG_DEVEL
#include "perf_monitor.h"
#endif

typedef struct
{
  int sock_fd;
  int state;
  char *user_id;
  char *ip_address;
#if 0				/* ACTIVITY PROFILE */
  int diag_ref_count;
  MONITOR_CAS_CONFIG diag_cas_config;
  struct
  {
    char server_name[MAX_SERVER_NAMELENGTH];
    MONITOR_SERVER_CONFIG server_config;
  } diag_server_config[MAX_SERVER_COUNT];
  int mon_server_num;
#endif
} T_CLIENT_INFO;

int ts_validate_user (nvplist * req, nvplist * res);
int ts_check_client_version (nvplist * req, nvplist * res);
int ts_check_already_connected (nvplist * cli_response, int max_index,
				int current_index,
				T_CLIENT_INFO * client_info);

#endif /* _CM_AUTO_TASK_H_ */
