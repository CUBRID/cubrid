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
 * pserver_task.c -
 */

#ident "$Id$"

#include <stdio.h>
#include <string.h>

#include "cm_auto_task.h"
#include "cm_porting.h"
#include "cm_dstring.h"
#include "cm_nameval.h"
#include "cm_server_util.h"
#include "cm_config.h"
#include "cm_user.h"
#include "cm_text_encryption.h"
#include "string.h"
#include "stdlib.h"
#ifdef	_DEBUG_
#include "deb.h"
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
      if (IS_INVALID_SOCKET (client_info[index].sock_fd)
	  || (index == current_index))
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
