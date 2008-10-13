/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * pserver_task.h -
 */

#ifndef _PSERVER_TASK_H_
#define _PSERVER_TASK_H_

#ident "$Id$"

#include "nameval.h"
#include "emgrver.h"
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

#endif /* _PSERVER_TASK_H_ */
