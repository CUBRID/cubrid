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
 * fserver_slave.c -
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#if defined(WINDOWS)
#include <io.h>
#include <process.h>
#else
#include <unistd.h>
#endif

#include "cm_porting.h"
#include "cm_server_util.h"
#include "cm_nameval.h"
#include "cm_dstring.h"
#include "cm_job_task.h"
#include "cm_config.h"
#include "cm_text_encryption.h"
#include "cm_broker_admin.h"
#include "cm_version.h"
#include "assert.h"

#if defined(WINDOWS)
#include "cm_wsa_init.h"
#endif

#ifdef	_DEBUG_
#include "deb.h"
#endif

static int ut_process_request (nvplist * req, nvplist * res);
static int _ut_get_dbaccess (nvplist * req, char *dbid, char *dbpasswd);
static void uGenerateStatus (nvplist * req, nvplist * res, int retval,
			     char *_dbmt_error);
static int ut_validate_token (nvplist * req);

T_EMGR_VERSION CLIENT_VERSION;

int
main (int argc, char *argv[])
{
  char *in_file, *out_file;
  nvplist *cli_request, *cli_response;
  char cubrid_err_log_file[256];
  char cubrid_err_log_env[256 + 32];

  if (argc < 3)
    exit (0);

#if defined(WINDOWS)
  wsa_initialize ();
#endif

  in_file = argv[1];
  out_file = argv[2];

  sys_config_init ();
  uReadEnvVariables (argv[0], stdout);
  uReadSystemConfig ();

  uca_init (NULL);


  /* From now on, interpret all the message as 'en_US' type.
   * In other language setting, it may corrupt.
   */
  putenv ((char *) "CUBRID_LANG=en_US");	/* set as default language type */

  sprintf (cubrid_err_log_file, "%s/cmclt.%d.err", sco.dbmt_tmp_dir,
	   (int) getpid ());
  sprintf (cubrid_err_log_env, "CUBRID_ERROR_LOG=%s", cubrid_err_log_file);
  putenv (cubrid_err_log_env);

  cli_request = nv_create (5, NULL, "\n", ":", "\n");
  cli_response = nv_create (5, NULL, "\n", ":", "\n");

  nv_readfrom (cli_request, in_file);
  ut_process_request (cli_request, cli_response);
  nv_writeto (cli_response, out_file);

  nv_destroy (cli_request);
  nv_destroy (cli_response);

  unlink (cubrid_err_log_file);

  return 0;
}

static int
ut_process_request (nvplist * req, nvplist * res)
{
  int task_code;
  int retval = ERR_NO_ERROR;
  char *dbname, *task;
  char dbid[512];
  char dbpasswd[512];
  T_TASK_FUNC task_func;
  char access_log_flag;
  char _dbmt_error[DBMT_ERROR_MSG_SIZE];
  int major_ver, minor_ver;
  char *cli_ver;

  memset (_dbmt_error, 0, sizeof (_dbmt_error));

  task = nv_get_val (req, "task");
  dbname = nv_get_val (req, "dbname");

  task_code = ut_get_task_info (task, &access_log_flag, &task_func);
#ifdef DIAG_DEVEL
  switch (task_code)
    {
      /* case TS_ANALYZECASLOG: */
    case TS_GET_DIAGDATA:
      nv_reset_nvp (res);
      nv_init (res, 5, NULL, "\n", ":DIAG_DEL:", "END__DIAGDATA\n");
      break;
    }
#endif

  /* insert task,status,note to the front of response */
  nv_add_nvp (res, "task", task);
  nv_add_nvp (res, "status", "none");
  nv_add_nvp (res, "note", "none");

  if (ut_validate_token (req) == 0)
    {
      retval = ERR_INVALID_TOKEN;
      uGenerateStatus (req, res, retval, _dbmt_error);
      return 0;
    }

  /* if database name is specified */
  if (dbname)
    {
      _ut_get_dbaccess (req, dbid, dbpasswd);
      nv_add_nvp (req, "_DBID", dbid);
      nv_add_nvp (req, "_DBPASSWD", dbpasswd);
      nv_add_nvp (req, "_DBNAME", dbname);
    }

  /* set CLIENT_VERSION */
  cli_ver = nv_get_val (req, "_CLIENT_VERSION");

  if (cli_ver == NULL)
    cli_ver = strdup ("1.0");

  make_version_info (cli_ver, &major_ver, &minor_ver);
  CLIENT_VERSION = EMGR_MAKE_VER (major_ver, minor_ver);	/* global variable */

  sprintf (_dbmt_error, "?");	/* prevent to have null string */
  if (task_code == TS_UNDEFINED)
    {
      strcpy (_dbmt_error, task);
      retval = ERR_UNDEFINED_TASK;
    }
  else
    {
      if (access_log_flag)
	{
	  ut_access_log (req, NULL);
	}
      retval = (*task_func) (req, res, _dbmt_error);
    }

  uGenerateStatus (req, res, retval, _dbmt_error);
  return 0;
}

/*
 *  dbid, dbpasswd is filled by this function
 *  caller must provide space for dbid, dbpasswd
 */
static int
_ut_get_dbaccess (nvplist * req, char *dbid, char *dbpasswd)
{
  nvplist *ud = NULL;
  char strbuf[1024];
  char *task, *id, *svrtype, *clientip, *dbname;
  char *tok[3];
  char _dbmt_error[1024];

  dbid[0] = dbpasswd[0] = '\0';

  id = nv_get_val (req, "_ID");
  svrtype = nv_get_val (req, "_SVRTYPE");
  clientip = nv_get_val (req, "_CLIENTIP");
  task = nv_get_val (req, "task");
  dbname = nv_get_val (req, "dbname");

  if ((ud = nv_create (6, NULL, "\n", ":", "\n")) == NULL)
    return 0;

  /* read cm.pass */
  if (_tsReadUserCapability (ud, id, _dbmt_error) != ERR_NO_ERROR)
    {
      nv_destroy (ud);
      return 0;
    }

  /* extract user info */
  if (nv_get_val (ud, dbname) == NULL)
    {
      nv_destroy (ud);
      return 0;
    }
  strcpy (strbuf, nv_get_val (ud, dbname));

  string_tokenize2 (strbuf, tok, 3, ';');

  if (tok[1] == NULL)
    dbid[0] = '\0';
  else
    {
      strcpy (dbid, tok[1]);
    }
  uDecrypt (PASSWD_LENGTH, tok[2], dbpasswd);

  nv_destroy (ud);
  return 1;
}


/* Generate status, note and write to log */
static void
uGenerateStatus (nvplist * req, nvplist * res, int retval, char *_dbmt_error)
{
  char strbuf[1024];

  if ((retval == -1) || (retval == 0))
    return;

  if ((retval == 1) || (retval == ERR_NO_ERROR))
    {
      nv_update_val (res, "status", "success");
      return;
    }

  if (retval == ERR_FILE_COMPRESS)
    {
      nv_update_val (res, "status", "success");
      nv_update_val (res, "note",
		     "Can't compress the file. Download original file");
      return;
    }

  nv_update_val (res, "status", "failure");
  switch (retval)
    {
    case ERR_GENERAL_ERROR:
      sprintf (strbuf, "Unknown general error");
      break;
    case ERR_UNDEFINED_TASK:
      sprintf (strbuf, "Undefined request - %s", _dbmt_error);
      break;
    case ERR_DBDIRNAME_NULL:
      sprintf (strbuf, "Can not find the directory database(%s) is located",
	       _dbmt_error);
      break;
    case ERR_GET_FILE:
      sprintf (strbuf, "Can't get requested files");
      break;
    case ERR_REQUEST_FORMAT:
      sprintf (strbuf, "Invalid request format");
      break;
    case ERR_DATABASETXT_OPEN:
      sprintf (strbuf, "'%s' open error", CUBRID_DATABASE_TXT);
      break;
    case ERR_USER_CAPABILITY:
      sprintf (strbuf, "Failed to get user profile for '%s'", _dbmt_error);
      break;
    case ERR_FILE_INTEGRITY:
      sprintf (strbuf, "Password file(%s) integrity failure", _dbmt_error);
      break;
    case ERR_SYSTEM_CALL:
      sprintf (strbuf, "Command returned error : %s", _dbmt_error);
      break;
    case ERR_PASSWORD_FILE:
      sprintf (strbuf, "Password file(%s) open error", _dbmt_error);
      break;
    case ERR_PARAM_MISSING:
      sprintf (strbuf, "Parameter(%s) missing in the request", _dbmt_error);
      break;
    case ERR_DIR_CREATE_FAIL:
      sprintf (strbuf, "Directory(%s) creation failed", _dbmt_error);
      break;
    case ERR_FILE_OPEN_FAIL:
      sprintf (strbuf, "File(%s) open error", _dbmt_error);
      break;
    case ERR_STANDALONE_MODE:
      sprintf (strbuf, "Database(%s) is running in standalone mode",
	       _dbmt_error);
      break;
    case ERR_DB_ACTIVE:
      sprintf (strbuf, "Database(%s) is active state.", _dbmt_error);
      break;
    case ERR_DB_INACTIVE:
      sprintf (strbuf, "Database(%s) is not active state", _dbmt_error);
      break;
    case ERR_DB_NONEXISTANT:
      sprintf (strbuf, "Database(%s) does not exist", _dbmt_error);
      break;
    case ERR_DBMTUSER_EXIST:
      sprintf (strbuf, "CUBRID Manager user(%s) already exist", _dbmt_error);
      break;
    case ERR_DIROPENFAIL:
      sprintf (strbuf, "Failed to read directory(%s)", _dbmt_error);
      break;
    case ERR_PERMISSION:
      sprintf (strbuf, "No permission for %s", _dbmt_error);
      break;
    case ERR_INVALID_TOKEN:
      sprintf (strbuf,
	       "Request is rejected due to invalid token. Please reconnect.");
      break;
    case ERR_SYSTEM_CALL_CON_DUMP:
      sprintf (strbuf, "%s", _dbmt_error);
      break;
    case ERR_STAT:
      sprintf (strbuf, "Failed to get the file information");
      break;
    case ERR_OPENDIR:
      sprintf (strbuf, "Failed to open the directory");
      break;
    case ERR_UNICASCONF_OPEN:
      sprintf (strbuf, "Failed to open unicas.conf");
      break;
    case ERR_UNICASCONF_PARAM_MISSING:
      sprintf (strbuf, "Specified parameter does not exist in unicas.conf");
      break;
    case ERR_DBLOGIN_FAIL:
      sprintf (strbuf, "Failed to log in to database using id:%s",
	       _dbmt_error);
      break;
    case ERR_DBRESTART_FAIL:
      sprintf (strbuf, "Failed to restart database(%s)", _dbmt_error);
      break;
    case ERR_DBUSER_NOTFOUND:
      sprintf (strbuf, "Database user (%s) not found", _dbmt_error);
      break;
    case ERR_DBPASSWD_CLEAR:
      sprintf (strbuf, "Failed to clear existing password - %s", _dbmt_error);
      break;
    case ERR_DBPASSWD_SET:
      sprintf (strbuf, "Failed to set new password - %s", _dbmt_error);
      break;
    case ERR_MEM_ALLOC:
      sprintf (strbuf, "Memory allocation error.");
      break;
    case ERR_TMPFILE_OPEN_FAIL:
      sprintf (strbuf, "Temporal file open error.");
      break;
    case ERR_WITH_MSG:
      strcpy (strbuf, _dbmt_error);
      break;
    case ERR_UPA_SYSTEM:
      sprintf (strbuf, "Authentication System Error.");
      break;
    case ERR_TEMPLATE_ALREADY_EXIST:
      sprintf (strbuf, "Template (%s) already exist.", _dbmt_error);
      break;
    default:
      sprintf (strbuf, "error");
      break;
    }
  nv_update_val (res, "note", strbuf);
  ut_error_log (req, strbuf);
}


/* validate token and insert id */
static int
ut_validate_token (nvplist * req)
{
  FILE *infile;
  int lfd, retval;
  char *ip, *port, *id, *tok[3];
  char *token, token_content[TOKEN_LENGTH + 1], strbuf[1024];
  char ip_t[20], port_t[10];
  char cli_ver[15];

  if ((token = nv_get_val (req, "token")) == NULL)
    return 0;

  uDecrypt (TOKEN_LENGTH, token, token_content);

  if (string_tokenize2 (token_content, tok, 3, ':') < 0)
    return 0;

  ip = tok[0];
  port = tok[1];
  id = tok[2];
  nv_add_nvp (req, "_ID", id);

  /* check if ip is an existing ip */
  retval = 0;
  lfd = uCreateLockFile (conf_get_dbmt_file (FID_LOCK_CONN_LIST, strbuf));
  if (lfd >= 0)
    {
      infile = fopen (conf_get_dbmt_file (FID_CONN_LIST, strbuf), "r");
      if (infile != NULL)
	{
	  while (fgets (strbuf, sizeof (strbuf), infile))
	    {
	      if (sscanf (strbuf, "%s %s", ip_t, port_t) < 2)
		continue;
	      if (uStringEqual (ip, ip_t))
		{
		  retval = 1;
		  sscanf (strbuf, "%*s %*s %*s %*s %s", cli_ver);
		  nv_add_nvp (req, "_CLIENT_VERSION", cli_ver);
		  break;
		}
	    }
	  fclose (infile);
	}
      uRemoveLockFile (lfd);
    }

  return retval;
}
