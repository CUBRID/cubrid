/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * pserver_task.c -
 */

#ident "$Id$"

#include <stdio.h>
#include <string.h>

#include "pserver_task.h"
#include "dbmt_porting.h"
#include "dstring.h"
#include "nameval.h"
#include "server_util.h"
#include "dbmt_config.h"
#include "dbmt_user.h"
#include "tea.h"
#include "string.h"
#include "stdlib.h"
#ifdef	_DEBUG_
#include "deb.h"
#endif

#if 0				/* ACTIVITY_PROFILE */
#include "diag_client_request.h"

int get_client_monitoring_config (nvplist * cli_request,
				  T_CLIENT_MONITOR_CONFIG * c_config);
#endif

int
ts_validate_user (nvplist * req, nvplist * res)
{
  char *id, *passwd;
  char strbuf[1024];
  int retval, i;
  T_DBMT_USER dbmt_user;

  id = nv_get_val (req, "id");
  passwd = nv_get_val (req, "password");

  nv_add_nvp (res, "task", "authenticate");
  nv_add_nvp (res, "status", "none");
  nv_add_nvp (res, "note", "none");
  /* id, passwd checking */
  if (id == NULL)
    {
      nv_update_val (res, "status", "failure");
      nv_update_val (res, "note", "id missing in request");
      ut_error_log (req, "ID not specified in the request");
      return 0;
    }

  if (dbmt_user_read (&dbmt_user, strbuf) != ERR_NO_ERROR)
    {
      nv_update_val (res, "status", "failure");
      nv_update_val (res, "note", "password file open error");
      ut_error_log (req, "Failed to read user info");
      return 0;
    }

  retval = -1;
  for (i = 0; i < dbmt_user.num_dbmt_user; i++)
    {
      if (strcmp (dbmt_user.user_info[i].user_name, id) == 0)
	{
	  char decrypted[PASSWD_LENGTH + 1];

	  uDecrypt (PASSWD_LENGTH, dbmt_user.user_info[i].user_passwd,
		    decrypted);
	  if (uStringEqual (passwd, decrypted))
	    {
	      nv_update_val (res, "status", "success");
	      retval = 1;
	    }
	  else
	    {
	      nv_update_val (res, "status", "failure");
	      nv_update_val (res, "note", "Incorrect password");
	      ut_error_log (req, "Incorrect password");
	      retval = 0;
	    }
	  break;
	}
    }
  dbmt_user_free (&dbmt_user);

  if (retval < 0)
    {
      nv_update_val (res, "status", "failure");
      nv_update_val (res, "note", "user not found");
      ut_error_log (req, "User not found.");
      return 0;
    }

  return retval;
}

int
ts_check_client_version (nvplist * req, nvplist * res)
{
  char *p;
  int major_ver, minor_ver;
  T_EMGR_VERSION clt_ver;

  major_ver = minor_ver = 0;
  p = nv_get_val (req, "clientver");
  if (p != NULL)
    {
      major_ver = atoi (p);
      p = strchr (p, '.');
      if (p != NULL)
	minor_ver = atoi (p + 1);
    }
  clt_ver = EMGR_MAKE_VER (major_ver, minor_ver);

  if (clt_ver < EMGR_MAKE_VER (1, 0))
    {
      nv_update_val (res, "status", "failure");
      nv_update_val (res, "note",
		     "Can not connect to the server due to version mismatch.");
      return 0;
    }

  return 1;
}

int
ts_check_already_connected (nvplist * cli_response, int max_index,
			    int current_index, T_CLIENT_INFO * client_info)
{
  int index = 0;
  for (index = 0; index <= max_index; index++)
    {
      if ((client_info[index].sock_fd == -1) || (index == current_index))
	continue;

      if (!strcmp
	  (client_info[current_index].user_id, client_info[index].user_id))
	{
	  char message[1024];
	  sprintf (message,
		   "User %s was already connected from another client(%s)",
		   client_info[index].user_id, client_info[index].ip_address);

	  nv_update_val (cli_response, "status", "failure");
	  nv_update_val (cli_response, "note", message);
	  return index;
	}
    }

  return -1;
}

#if 0				/* ACTIVITY_PROFILE */
int
get_client_monitoring_config (nvplist * cli_request,
			      T_CLIENT_MONITOR_CONFIG * c_config)
{
  char *monitor_db, *monitor_cas, *monitor_resource;
  char *activity_cas, *activity_db;
  char *cas_mon_req, *cas_mon_tran, *cas_mon_active_session;
  char *cas_act_req, *cas_act_tran;
  char *db_name;

  monitor_db = nv_get_val (cli_request, "mon_db");
  monitor_cas = nv_get_val (cli_request, "mon_cas");
  monitor_resource = nv_get_val (cli_request, "mon_resource");
  activity_db = nv_get_val (cli_request, "act_db");
  activity_cas = nv_get_val (cli_request, "act_cas");

  if (monitor_db && !strcasecmp (monitor_db, "yes"))
    {
      int header_flag;
      char *monitor_target;

      /* 1. parse "cubrid_query" monitor list */
      header_flag = 0;
      monitor_target = nv_get_val (cli_request, "mon_cub_query_open_page");
      if (monitor_target && !strcasecmp (monitor_target, "yes"))
	{
	  SET_CLIENT_MONITOR_INFO_CUB_QUERY_OPEN_PAGE (*c_config);
	  header_flag = 1;
	}
      monitor_target = nv_get_val (cli_request, "mon_cub_query_opened_page");
      if (monitor_target && !strcasecmp (monitor_target, "yes"))
	{
	  SET_CLIENT_MONITOR_INFO_CUB_QUERY_OPENED_PAGE (*c_config);
	  header_flag = 1;
	}
      monitor_target = nv_get_val (cli_request, "mon_cub_query_slow_query");
      if (monitor_target && !strcasecmp (monitor_target, "yes"))
	{
	  SET_CLIENT_MONITOR_INFO_CUB_QUERY_SLOW_QUERY (*c_config);
	  header_flag = 1;
	}
      monitor_target = nv_get_val (cli_request, "mon_cub_query_full_scan");
      if (monitor_target && !strcasecmp (monitor_target, "yes"))
	{
	  SET_CLIENT_MONITOR_INFO_CUB_QUERY_FULL_SCAN (*c_config);
	  header_flag = 1;
	}
      if (header_flag == 1)
	SET_CLIENT_MONITOR_INFO_CUB_QUERY (*c_config);

      /* 2. parse "cubrid_conn" monitor list */
      header_flag = 0;
      monitor_target = nv_get_val (cli_request, "mon_cub_conn_cli_request");
      if (monitor_target && !strcasecmp (monitor_target, "yes"))
	{
	  SET_CLIENT_MONITOR_INFO_CUB_CONN_CLI_REQUEST (*c_config);
	  header_flag = 1;
	}
      monitor_target =
	nv_get_val (cli_request, "mon_cub_conn_aborted_clients");
      if (monitor_target && !strcasecmp (monitor_target, "yes"))
	{
	  SET_CLIENT_MONITOR_INFO_CUB_CONN_ABORTED_CLIENTS (*c_config);
	  header_flag = 1;
	}
      monitor_target = nv_get_val (cli_request, "mon_cub_conn_conn_req");
      if (monitor_target && !strcasecmp (monitor_target, "yes"))
	{
	  SET_CLIENT_MONITOR_INFO_CUB_CONN_CONN_REQ (*c_config);
	  header_flag = 1;
	}
      monitor_target = nv_get_val (cli_request, "mon_cub_conn_conn_reject");
      if (monitor_target && !strcasecmp (monitor_target, "yes"))
	{
	  SET_CLIENT_MONITOR_INFO_CUB_CONN_CONN_REJECT (*c_config);
	  header_flag = 1;
	}
      if (header_flag == 1)
	SET_CLIENT_MONITOR_INFO_CUB_CONNECTION (*c_config);

      /* 3. parse "cubrid_buffer" monitor list */
      header_flag = 0;
      monitor_target = nv_get_val (cli_request, "mon_cub_buffer_page_write");
      if (monitor_target && !strcasecmp (monitor_target, "yes"))
	{
	  SET_CLIENT_MONITOR_INFO_CUB_BUFFER_PAGE_WRITE (*c_config);
	  header_flag = 1;
	}
      monitor_target = nv_get_val (cli_request, "mon_cub_buffer_page_read");
      if (monitor_target && !strcasecmp (monitor_target, "yes"))
	{
	  SET_CLIENT_MONITOR_INFO_CUB_BUFFER_PAGE_READ (*c_config);
	  header_flag = 1;
	}
      if (header_flag == 1)
	SET_CLIENT_MONITOR_INFO_CUB_BUFFER (*c_config);

      /* 4 parse "cubrid_locl" monitor list */
      header_flag = 0;
      monitor_target = nv_get_val (cli_request, "mon_cub_lock_deadlock");
      if (monitor_target && !strcasecmp (monitor_target, "yes"))
	{
	  SET_CLIENT_MONITOR_INFO_CUB_LOCK_DEADLOCK (*c_config);
	  header_flag = 1;
	}
      monitor_target = nv_get_val (cli_request, "mon_cub_lock_request");
      if (monitor_target && !strcasecmp (monitor_target, "yes"))
	{
	  SET_CLIENT_MONITOR_INFO_CUB_LOCK_REQUEST (*c_config);
	  header_flag = 1;
	}
      if (header_flag == 1)
	SET_CLIENT_MONITOR_INFO_CUB_LOCK (*c_config);
    }

  if (activity_db && !strcasecmp (activity_db, "yes"))
    {
      char *act_target;
      int header_flag;

      header_flag = 0;
      act_target = nv_get_val (cli_request, "act_cub_query_fullscan");
      if (act_target && !strcasecmp (act_target, "yes"))
	{
	  SET_CLIENT_ACTINFO_CUB_QUERY_FULLSCAN (*c_config);
	  header_flag = 1;
	}
      act_target = nv_get_val (cli_request, "act_cub_lock_deadlock");
      if (act_target && !strcasecmp (act_target, "yes"))
	{
	  SET_CLIENT_ACTINFO_CUB_LOCK_DEADLOCK (*c_config);
	  header_flag = 1;
	}
      act_target = nv_get_val (cli_request, "act_cub_buffer_page_read");
      if (act_target && !strcasecmp (act_target, "yes"))
	{
	  SET_CLIENT_ACTINFO_CUB_BUFFER_PAGE_READ (*c_config);
	  header_flag = 1;
	}
      act_target = nv_get_val (cli_request, "act_cub_buffer_page_write");
      if (act_target && !strcasecmp (act_target, "yes"))
	{
	  SET_CLIENT_ACTINFO_CUB_BUFFER_PAGE_WRITE (*c_config);
	  header_flag = 1;
	}
      if (header_flag == 1)
	SET_CLIENT_ACTINFO_CUB (*c_config);
    }

  if (monitor_cas && !strcasecmp (monitor_cas, "yes"))
    {
      cas_mon_req = nv_get_val (cli_request, "cas_mon_req");
      cas_mon_tran = nv_get_val (cli_request, "cas_mon_tran");
      cas_mon_active_session =
	nv_get_val (cli_request, "cas_mon_act_session");

      SET_CLIENT_MONITOR_INFO_CAS (*c_config);
      if (cas_mon_req && !strcasecmp (cas_mon_req, "yes"))
	SET_CLIENT_MONITOR_INFO_CAS_REQ (*c_config);

      if (cas_mon_tran && !strcasecmp (cas_mon_tran, "yes"))
	SET_CLIENT_MONITOR_INFO_CAS_TRAN (*c_config);

      if (cas_mon_active_session
	  && !strcasecmp (cas_mon_active_session, "yes"))
	SET_CLIENT_MONITOR_INFO_CAS_ACTIVE_SESSION (*c_config);
    }

  if (activity_cas && !strcasecmp (activity_cas, "yes"))
    {
      cas_act_req = nv_get_val (cli_request, "cas_act_req");
      cas_act_tran = nv_get_val (cli_request, "cas_act_tran");

      SET_CLIENT_ACTINFO_CAS (*c_config);
      if (cas_act_req && !strcasecmp (cas_act_req, "yes"))
	SET_CLIENT_ACTINFO_CAS_REQ (*c_config);

      if (cas_act_tran && !strcasecmp (cas_act_tran, "yes"))
	SET_CLIENT_ACTINFO_CAS_TRAN (*c_config);
    }

  return ERR_NO_ERROR;
}
#endif
