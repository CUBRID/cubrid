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
 * server_util.c -
 */

#ident "$Id$"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/types.h>		/* umask()          */
#include <sys/stat.h>		/* umask(), stat()  */
#include <time.h>
#include <ctype.h>		/* isalpha()        */
#include <errno.h>
#include <fcntl.h>

#if defined(WINDOWS)
#include <process.h>
#include <io.h>
#include <direct.h>
#include <winsock2.h>
#include <sys/locking.h>
#include <Tlhelp32.h>
#else
#include <unistd.h>
#include <dirent.h>		/* opendir() ...    */
#include <pwd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/statvfs.h>
#include <netdb.h>
#include <arpa/inet.h>
#if defined(LINUX)
#include <sys/wait.h>
#endif /* LINUX */
#if !defined(HPUX) && !defined(AIX)
#include <sys/procfs.h>
#endif
#include <sys/file.h>
#endif

#include "cm_porting.h"
#include "cm_server_util.h"
#include "cm_dep.h"
#include "cm_config.h"
#include "cm_job_task.h"
#include "cm_command_execute.h"
#include "cm_stat.h"
#include <assert.h>

#ifdef FSERVER_SLAVE
#define DEF_TASK_FUNC(TASK_FUNC_PTR)	TASK_FUNC_PTR
#else
#define DEF_TASK_FUNC(TASK_FUNC_PTR)	NULL
#endif

/* for ut_getdelim */
#define MAX_LINE ((int)(10*1024*1024))
#define MIN_CHUNK 4096


static T_FSERVER_TASK_INFO task_info[] = {
  {"startinfo", TS_STARTINFO, 0, DEF_TASK_FUNC (ts_startinfo), FSVR_SA},
  {"userinfo", TS_USERINFO, 0, DEF_TASK_FUNC (ts_userinfo), FSVR_CS},
  {"createuser", TS_CREATEUSER, 1, DEF_TASK_FUNC (ts_create_user), FSVR_CS},
  {"deleteuser", TS_DELETEUSER, 1, DEF_TASK_FUNC (ts_delete_user), FSVR_CS},
  {"updateuser", TS_UPDATEUSER, 1, DEF_TASK_FUNC (ts_update_user), FSVR_CS},
  {"createdb", TS_CREATEDB, 1, DEF_TASK_FUNC (tsCreateDB), FSVR_SA},
  {"deletedb", TS_DELETEDB, 1, DEF_TASK_FUNC (tsDeleteDB), FSVR_SA},
  {"renamedb", TS_RENAMEDB, 1, DEF_TASK_FUNC (tsRenameDB), FSVR_SA},
  {"startdb", TS_STARTDB, 0, DEF_TASK_FUNC (tsStartDB), FSVR_NONE},
  {"stopdb", TS_STOPDB, 0, DEF_TASK_FUNC (tsStopDB), FSVR_CS},
  {"dbspaceinfo", TS_DBSPACEINFO, 0, DEF_TASK_FUNC (tsDbspaceInfo),
   FSVR_SA_CS},
  {"classinfo", TS_CLASSINFO, 0, DEF_TASK_FUNC (ts_class_info), FSVR_SA_CS},
  {"class", TS_CLASS, 0, DEF_TASK_FUNC (ts_class), FSVR_CS},
  {"setsysparam", TS_SETSYSPARAM, 1, DEF_TASK_FUNC (ts_set_sysparam),
   FSVR_NONE},
  {"getallsysparam", TS_GETALLSYSPARAM, 0,
   DEF_TASK_FUNC (ts_get_all_sysparam), FSVR_NONE},
  {"addvoldb", TS_ADDVOLDB, 1, DEF_TASK_FUNC (tsRunAddvoldb), FSVR_SA_CS},
  {"getloginfo", TS_GETLOGINFO, 0, DEF_TASK_FUNC (ts_get_log_info),
   FSVR_NONE},
  {"viewlog", TS_VIEWLOG, 0, DEF_TASK_FUNC (ts_view_log), FSVR_NONE},
  {"viewlog2", TS_VIEWLOG, 0, DEF_TASK_FUNC (ts_view_log), FSVR_NONE},
  {"resetlog", TS_RESETLOG, 0, DEF_TASK_FUNC (ts_reset_log), FSVR_NONE},
  {"getenv", TS_GETENV, 0, DEF_TASK_FUNC (tsGetEnvironment), FSVR_SA_UC},
  {"updateattribute", TS_UPDATEATTRIBUTE, 1,
   DEF_TASK_FUNC (ts_update_attribute), FSVR_CS},
  {"kill_process", TS_KILL_PROCESS, 1, DEF_TASK_FUNC (ts_kill_process),
   FSVR_NONE},
  {"copydb", TS_COPYDB, 1, DEF_TASK_FUNC (ts_copydb), FSVR_SA},
  {"optimizedb", TS_OPTIMIZEDB, 1, DEF_TASK_FUNC (ts_optimizedb), FSVR_SA_CS},
  {"plandump", TS_PLANDUMP, 1, DEF_TASK_FUNC (ts_plandump), FSVR_CS},
  {"paramdump", TS_PARAMDUMP, 1, DEF_TASK_FUNC (ts_paramdump), FSVR_SA_CS},
  {"statdump", TS_STATDUMP, 1, DEF_TASK_FUNC (ts_statdump), FSVR_CS},
  {"checkdb", TS_CHECKDB, 0, DEF_TASK_FUNC (ts_checkdb), FSVR_SA_CS},
  {"compactdb", TS_COMPACTDB, 1, DEF_TASK_FUNC (ts_compactdb), FSVR_SA},
  {"backupdbinfo", TS_BACKUPDBINFO, 0, DEF_TASK_FUNC (ts_backupdb_info),
   FSVR_NONE},
  {"backupdb", TS_BACKUPDB, 0, DEF_TASK_FUNC (ts_backupdb), FSVR_SA_CS},
  {"unloaddb", TS_UNLOADDB, 1, DEF_TASK_FUNC (ts_unloaddb), FSVR_SA},
  {"unloadinfo", TS_UNLOADDBINFO, 0, DEF_TASK_FUNC (ts_unloaddb_info),
   FSVR_NONE},
  {"loaddb", TS_LOADDB, 1, DEF_TASK_FUNC (ts_loaddb), FSVR_SA},
  {"gettransactioninfo", TS_GETTRANINFO, 0, DEF_TASK_FUNC (ts_get_tran_info),
   FSVR_CS},
  {"killtransaction", TS_KILLTRAN, 1, DEF_TASK_FUNC (ts_killtran), FSVR_CS},
  {"lockdb", TS_LOCKDB, 0, DEF_TASK_FUNC (ts_lockdb), FSVR_CS},
  {"getbackuplist", TS_GETBACKUPLIST, 0, DEF_TASK_FUNC (ts_get_backup_list),
   FSVR_NONE},
  {"restoredb", TS_RESTOREDB, 1, DEF_TASK_FUNC (ts_restoredb), FSVR_SA},
  {"backupvolinfo", TS_BACKUPVOLINFO, 0, DEF_TASK_FUNC (ts_backup_vol_info),
   FSVR_SA},
  {"getdbsize", TS_GETDBSIZE, 0, DEF_TASK_FUNC (ts_get_dbsize), FSVR_SA_CS},
  {"getbackupinfo", TS_GETBACKUPINFO, 0, DEF_TASK_FUNC (ts_get_backup_info),
   FSVR_NONE},
  {"addbackupinfo", TS_ADDBACKUPINFO, 1, DEF_TASK_FUNC (ts_add_backup_info),
   FSVR_NONE},
  {"deletebackupinfo", TS_DELETEBACKUPINFO, 1,
   DEF_TASK_FUNC (ts_delete_backup_info), FSVR_NONE},
  {"setbackupinfo", TS_SETBACKUPINFO, 0, DEF_TASK_FUNC (ts_set_backup_info),
   FSVR_NONE},
  {"getautoaddvol", TS_GETAUTOADDVOL, 0, DEF_TASK_FUNC (ts_get_auto_add_vol),
   FSVR_NONE},
  {"setautoaddvol", TS_SETAUTOADDVOL, 1, DEF_TASK_FUNC (ts_set_auto_add_vol),
   FSVR_NONE},
  {"loadaccesslog", TS_LOADACCESSLOG, 0, DEF_TASK_FUNC (ts_load_access_log),
   FSVR_NONE},
  {"deleteaccesslog", TS_DELACCESSLOG, 1,
   DEF_TASK_FUNC (ts_delete_access_log), FSVR_NONE},
  {"deleteerrorlog", TS_DELERRORLOG, 1, DEF_TASK_FUNC (ts_delete_error_log),
   FSVR_NONE},
  {"checkdir", TS_CHECKDIR, 0, DEF_TASK_FUNC (ts_check_dir), FSVR_NONE},
  {"getautobackupdberrlog", TS_AUTOBACKUPDBERRLOG, 0,
   DEF_TASK_FUNC (ts_get_autobackupdb_error_log), FSVR_NONE},
  {"getautoexecqueryerrlog", TS_AUTOEXECQUERYERRLOG, 0,
   DEF_TASK_FUNC (ts_get_autoexecquery_error_log), FSVR_NONE},
  {"getdbmtuserinfo", TS_GETDBMTUSERINFO, 0,
   DEF_TASK_FUNC (tsGetDBMTUserInfo), FSVR_NONE},
  {"deletedbmtuser", TS_DELETEDBMTUSER, 1, DEF_TASK_FUNC (tsDeleteDBMTUser),
   FSVR_NONE},
  {"updatedbmtuser", TS_UPDATEDBMTUSER, 1, DEF_TASK_FUNC (tsUpdateDBMTUser),
   FSVR_NONE},
  {"setdbmtpasswd", TS_SETDBMTPASSWD, 1,
   DEF_TASK_FUNC (tsChangeDBMTUserPasswd), FSVR_NONE},
  {"adddbmtuser", TS_ADDDBMTUSER, 1, DEF_TASK_FUNC (tsCreateDBMTUser),
   FSVR_NONE},
  {"getaddvolstatus", TS_GETADDVOLSTATUS, 0,
   DEF_TASK_FUNC (ts_get_addvol_status), FSVR_NONE},
  {"getautoaddvollog", TS_GETAUTOADDVOLLOG, 0,
   DEF_TASK_FUNC (tsGetAutoaddvolLog), FSVR_UC},
  {"getinitbrokersinfo", TS2_GETINITUNICASINFO, 0,
   DEF_TASK_FUNC (ts2_get_unicas_info), FSVR_UC},
  {"getbrokersinfo", TS2_GETUNICASINFO, 0,
   DEF_TASK_FUNC (ts2_get_unicas_info), FSVR_UC},
  {"startbroker", TS2_STARTUNICAS, 0, DEF_TASK_FUNC (ts2_start_unicas),
   FSVR_UC},
  {"stopbroker", TS2_STOPUNICAS, 0, DEF_TASK_FUNC (ts2_stop_unicas), FSVR_UC},
  {"getadminloginfo", TS2_GETADMINLOGINFO, 0,
   DEF_TASK_FUNC (ts2_get_admin_log_info), FSVR_UC},
  {"getlogfileinfo", TS2_GETLOGFILEINFO, 0,
   DEF_TASK_FUNC (ts2_get_logfile_info), FSVR_UC},
  {"getaddbrokerinfo", TS2_GETADDBROKERINFO, 0,
   DEF_TASK_FUNC (ts2_get_add_broker_info), FSVR_UC},
  {"deletebroker", TS2_DELETEBROKER, 1, DEF_TASK_FUNC (ts2_delete_broker),
   FSVR_UC},
  {"getbrokerstatus", TS2_GETBROKERSTATUS, 0,
   DEF_TASK_FUNC (ts2_get_broker_status), FSVR_UC},
  {"broker_setparam", TS2_SETBROKERCONF, 1,
   DEF_TASK_FUNC (ts2_set_broker_conf), FSVR_UC},
  {"broker_start", TS2_STARTBROKER, 0, DEF_TASK_FUNC (ts2_start_broker),
   FSVR_UC},
  {"broker_stop", TS2_STOPBROKER, 0, DEF_TASK_FUNC (ts2_stop_broker),
   FSVR_UC},
  {"broker_restart", TS2_RESTARTBROKERAS, 0,
   DEF_TASK_FUNC (ts2_restart_broker_as), FSVR_UC},
  {"checkfile", TS_CHECKFILE, 0, DEF_TASK_FUNC (ts_check_file), FSVR_NONE},
  {"addtrigger", TS_ADDNEWTRIGGER, 1, DEF_TASK_FUNC (ts_trigger_operation),
   FSVR_SA_CS},
  {"altertrigger", TS_ALTERTRIGGER, 1, DEF_TASK_FUNC (ts_trigger_operation),
   FSVR_SA_CS},
  {"droptrigger", TS_DROPTRIGGER, 1, DEF_TASK_FUNC (ts_trigger_operation),
   FSVR_SA_CS},
  {"gettriggerinfo", TS_GETTRIGGERINFO, 0, DEF_TASK_FUNC (ts_get_triggerinfo),
   FSVR_SA_CS},
  {"getautoexecquery", TS_GETAUTOEXECQUERY, 0,
   DEF_TASK_FUNC (ts_get_autoexec_query), FSVR_SA_CS},
  {"setautoexecquery", TS_SETAUTOEXECQUERY, 1,
   DEF_TASK_FUNC (ts_set_autoexec_query), FSVR_SA_CS},
  {"getdiagdata", TS_GET_DIAGDATA, 0, DEF_TASK_FUNC (ts_get_diagdata),
   FSVR_NONE},
  {"getbrokerdiagdata", TS_GET_BROKER_DIAGDATA, 0,
   DEF_TASK_FUNC (ts_get_broker_diagdata),
   FSVR_NONE},
  {"addstatustemplate", TS_ADDSTATUSTEMPLATE, 0,
   DEF_TASK_FUNC (ts_addstatustemplate), FSVR_NONE},
  {"updatestatustemplate", TS_UPDATESTATUSTEMPLATE, 0,
   DEF_TASK_FUNC (ts_updatestatustemplate), FSVR_NONE},
  {"removestatustemplate", TS_REMOVESTATUSTEMPLATE, 0,
   DEF_TASK_FUNC (ts_removestatustemplate), FSVR_NONE},
  {"getstatustemplate", TS_GETSTATUSTEMPLATE, 0,
   DEF_TASK_FUNC (ts_getstatustemplate), FSVR_NONE},
  {"analyzecaslog", TS_ANALYZECASLOG, 0, DEF_TASK_FUNC (ts_analyzecaslog),
   FSVR_NONE},
  {"executecasrunner", TS_EXECUTECASRUNNER, 0,
   DEF_TASK_FUNC (ts_executecasrunner), FSVR_NONE},
  {"removecasrunnertmpfile", TS_REMOVECASRUNNERTMPFILE, 0,
   DEF_TASK_FUNC (ts_removecasrunnertmpfile), FSVR_NONE},
  {"getcaslogtopresult", TS_GETCASLOGTOPRESULT, 0,
   DEF_TASK_FUNC (ts_getcaslogtopresult), FSVR_NONE},
  {"dbmtuserlogin", TS_DBMTUSERLOGIN, 0, DEF_TASK_FUNC (tsDBMTUserLogin),
   FSVR_NONE},
  {"removelog", TS_REMOVE_LOG, 0, DEF_TASK_FUNC (ts_remove_log), FSVR_NONE},
  {NULL, TS_UNDEFINED, 0, NULL, FSVR_NONE}
};

static int _maybe_ip_addr (char *hostname);
static int _ip_equal_hostent (struct hostent *hp, char *token);

int
_op_check_is_localhost (char *token, char *hname)
{
  struct hostent *hp;

  if ((hp = gethostbyname (hname)) == NULL)
    {
      return -1;
    }

  /* if token is an ip address. */
  if (_maybe_ip_addr (token) > 0)
    {
      /* if token equal 127.0.0.1 or the ip is in the list of hname. */
      if ((strcmp (token, "127.0.0.1") == 0)
	  || _ip_equal_hostent (hp, token) == 0)
	{
	  return 0;
	}
    }
  else
    {
      /* 
       * if token is not an ip address, 
       * then compare it with the hostname ignore case.
       */
      if ((strcasecmp (token, hname) == 0)
	  || (strcasecmp (token, "localhost") == 0))
	{
	  return 0;
	}
    }
  return -1;
}

static int
_maybe_ip_addr (char *hostname)
{
  if (hostname == NULL)
    {
      return 0;
    }
  return (isdigit (hostname[0]) ? 1 : 0);
}

static int
_ip_equal_hostent (struct hostent *hp, char *token)
{
  int i;
  int retval = -1;
  const char *tmpstr = NULL;
  struct in_addr inaddr;

  if (hp == NULL)
    {
      return retval;
    }

  for (i = 0; hp->h_addr_list[i] != NULL; i++)
    {
      /* change ip address of hname to string. */
      inaddr.s_addr = *(unsigned long *) hp->h_addr_list[i];
      tmpstr = inet_ntoa (inaddr);

      /* compare the ip string with token. */
      if (strcmp (token, tmpstr) == 0)
	{
	  retval = 0;
	  break;
	}
    }
  return retval;
}

void
append_host_to_dbname (char *name_buf, const char *dbname, int buf_len)
{
  snprintf (name_buf, buf_len, "%s@127.0.0.1", dbname);
}

void *
increase_capacity (void *ptr, int block_size, int old_count, int new_count)
{
  if (new_count <= old_count || new_count <= 0)
    return NULL;

  if (ptr == NULL)
    {
      if ((ptr = malloc (block_size * new_count)) == NULL)
	return NULL;
      memset (ptr, 0, block_size * new_count);
    }
  else
    {
      if ((ptr = realloc (ptr, block_size * new_count)) == NULL)
	return NULL;
      memset ((char *) ptr + old_count * block_size, 0,
	      block_size * (new_count - old_count));
    }

  return ptr;
}

char *
strcpy_limit (char *dest, const char *src, int buf_len)
{
  strncpy (dest, src, buf_len - 1);
  dest[buf_len - 1] = '\0';
  return dest;
}

int
ut_getdelim (char **lineptr, int *n, int delimiter, FILE * fp)
{
  int result;
  int cur_len = 0;
  int c;

  if (lineptr == NULL || n == NULL || fp == NULL)
    {
      return -1;
    }

  if (*lineptr == NULL || *n == 0)
    {
      char *new_lineptr;
      *n = MIN_CHUNK;
      new_lineptr = (char *) realloc (*lineptr, *n);

      if (new_lineptr == NULL)
	{
	  return -1;
	}
      *lineptr = new_lineptr;
    }

  for (;;)
    {
      c = getc (fp);
      if (c == EOF)
	{
	  result = -1;
	  break;
	}

      /* Make enough space for len+1 (for final NUL) bytes. */
      if (cur_len + 1 >= *n)
	{
	  int line_len = 2 * *n + 1;
	  char *new_lineptr;

	  if (line_len > MAX_LINE)
	    {
	      line_len = MAX_LINE;
	    }
	  if (cur_len + 1 >= line_len)
	    {
	      return -1;
	    }

	  new_lineptr = (char *) realloc (*lineptr, line_len);
	  if (new_lineptr == NULL)
	    {
	      return -1;
	    }

	  *lineptr = new_lineptr;
	  *n = line_len;
	}
      (*lineptr)[cur_len] = c;
      cur_len++;

      if (c == delimiter)
	break;
    }
  (*lineptr)[cur_len] = '\0';
  result = cur_len ? cur_len : result;

  return result;
}

int
ut_getline (char **lineptr, int *n, FILE * fp)
{
  return ut_getdelim (lineptr, n, '\n', fp);
}

void
uRemoveCRLF (char *str)
{
  int i;
  if (str == NULL)
    return;
  for (i = strlen (str) - 1; (i >= 0) && (str[i] == 10 || str[i] == 13); i--)
    {
      str[i] = '\0';
    }
}

char *
time_to_str (time_t t, const char *fmt, char *buf, int type)
{
  struct tm ltm;
  struct tm *tm_p;

  tm_p = localtime (&t);
  if (tm_p == NULL)
    {
      *buf = '\0';
      return buf;
    }
  ltm = *tm_p;

  if (type == TIME_STR_FMT_DATE)
    sprintf (buf, fmt, ltm.tm_year + 1900, ltm.tm_mon + 1, ltm.tm_mday);
  else if (type == TIME_STR_FMT_TIME)
    sprintf (buf, fmt, ltm.tm_hour, ltm.tm_min, ltm.tm_sec);
  else				/* TIME_STR_FMT_DATE_TIME */
    sprintf (buf, fmt, ltm.tm_year + 1900, ltm.tm_mon + 1, ltm.tm_mday,
	     ltm.tm_hour, ltm.tm_min, ltm.tm_sec);
  return buf;
}

int
uStringEqual (const char *str1, const char *str2)
{
  if (str1 == NULL || str2 == NULL)
    return 0;
  if (strcmp (str1, str2) == 0)
    return 1;
  return 0;
}

int
uStringEqualIgnoreCase (const char *str1, const char *str2)
{
  if (str1 == NULL || str2 == NULL)
    return 0;
  if (strcasecmp (str1, str2) == 0)
    return 1;
  return 0;
}

int
ut_error_log (nvplist * req, const char *errmsg)
{
  const char *id, *addr, *task, *stype;
  FILE *logf;
  char strbuf[512];
  int lock_fd;
  T_DBMT_FILE_ID log_fid;

  stype = nv_get_val (req, "_SVRTYPE");
  if ((task = nv_get_val (req, "task")) == NULL)
    task = "-";
  if ((addr = nv_get_val (req, "_CLIENTIP")) == NULL)
    addr = "-";
  if ((id = nv_get_val (req, "_ID")) == NULL)
    id = "-";
  if (errmsg == NULL)
    errmsg = "-";

  if (uStringEqual (stype, "psvr"))
    log_fid = FID_PSERVER_ERROR_LOG;
  else if (uStringEqual (stype, "fsvr"))
    log_fid = FID_FSERVER_ERROR_LOG;
  else
    return -1;

  lock_fd = uCreateLockFile (conf_get_dbmt_file (FID_LOCK_SVR_LOG, strbuf));
  if (lock_fd < 0)
    return -1;

  logf = fopen (conf_get_dbmt_file (log_fid, strbuf), "a");
  if (logf != NULL)
    {
      time_to_str (time (NULL), "%04d/%02d/%02d %02d:%02d:%02d", strbuf,
		   TIME_STR_FMT_DATE_TIME);
      fprintf (logf, "%s %s %s %s %s\n", strbuf, id, addr, task, errmsg);
      fflush (logf);
      fclose (logf);
    }

  uRemoveLockFile (lock_fd);
  return 1;
}

int
ut_access_log (nvplist * req, const char *msg)
{
  const char *id, *cli_addr, *task, *stype;
  FILE *logf;
  char strbuf[512];
  int lock_fd;
  T_DBMT_FILE_ID log_fid;

  if (req == NULL)
    return 1;

  stype = nv_get_val (req, "_SVRTYPE");
  if ((task = nv_get_val (req, "task")) == NULL)
    task = "-";
  if ((cli_addr = nv_get_val (req, "_CLIENTIP")) == NULL)
    cli_addr = "-";
  if ((id = nv_get_val (req, "_ID")) == NULL)
    id = "-";
  if (msg == NULL)
    msg = "";

  if (uStringEqual (stype, "psvr"))
    log_fid = FID_PSERVER_ACCESS_LOG;
  else if (uStringEqual (stype, "fsvr"))
    log_fid = FID_FSERVER_ACCESS_LOG;
  else
    return -1;

  lock_fd = uCreateLockFile (conf_get_dbmt_file (FID_LOCK_SVR_LOG, strbuf));
  if (lock_fd < 0)
    return -1;

  logf = fopen (conf_get_dbmt_file (log_fid, strbuf), "a");
  if (logf != NULL)
    {
      time_to_str (time (NULL), "%04d/%02d/%02d %02d:%02d:%02d", strbuf,
		   TIME_STR_FMT_DATE_TIME);
      fprintf (logf, "%s %s %s %s %s\n", strbuf, id, cli_addr, task, msg);

      fflush (logf);
      fclose (logf);
    }

  uRemoveLockFile (lock_fd);
  return 1;
}

int
ut_get_task_info (char *task, char *access_log_flag, T_TASK_FUNC * task_func)
{
  int i;

  for (i = 0; task_info[i].task_str != NULL; i++)
    {
      if (uStringEqual (task, task_info[i].task_str))
	{
	  if (access_log_flag)
	    *access_log_flag = task_info[i].access_log_flag;
	  if (task_func)
	    *task_func = task_info[i].task_func;
	  return task_info[i].task_code;
	}
    }

  return TS_UNDEFINED;
}

int
ut_send_response (SOCKET fd, nvplist * res)
{
  int i;

  if (res == NULL)
    return -1;

  for (i = 0; i < res->nvplist_size; ++i)
    {
      if (res->nvpairs[i] == NULL)
	continue;
      write_to_socket (fd, dst_buffer (res->nvpairs[i]->name),
		       dst_length (res->nvpairs[i]->name));
      write_to_socket (fd, dst_buffer (res->delimiter), res->delimiter->dlen);
      write_to_socket (fd, dst_buffer (res->nvpairs[i]->value),
		       dst_length (res->nvpairs[i]->value));
      write_to_socket (fd, dst_buffer (res->endmarker), res->endmarker->dlen);
    }
  write_to_socket (fd, dst_buffer (res->listcloser), res->listcloser->dlen);

  return 0;
}

/*
 *  read incoming data and construct name-value pair list of request
 */
int
ut_receive_request (SOCKET fd, nvplist * req)
{
  int rc;
  char c;
  dstring *linebuf = NULL;
  char *p;

  linebuf = dst_create ();

  while ((rc = read_from_socket (fd, &c, 1)) == 1)
    {
      char *dstbuf;

      if (c == '\n')
	{
	  /* if null string, stop parsing */
	  if (dst_length (linebuf) == 0)
	    {
	      dst_destroy (linebuf);
	      return 1;
	    }

	  dstbuf = dst_buffer (linebuf);
	  if (dstbuf != NULL)
	    {
	      p = strchr (dstbuf, ':');
	      if (p)
		{
		  *p = '\0';
		  p++;
		  nv_add_nvp (req, dst_buffer (linebuf), p);
		}
	    }
	  dst_reset (linebuf);
	}
      else
	{
	  if (c != '\r')
	    dst_append (linebuf, &c, 1);
	}
    }

  dst_destroy (linebuf);
  return 0;
}


void
send_msg_with_file (SOCKET sock_fd, char *filename)
{
  FILE *res_file;
  char *file_name[10];
  char buf[1024];
  int file_num = -1;
  long *file_size = NULL;
  int index, file_send_flag;
  int *del_flag = NULL;
#ifdef	_DEBUG_
  FILE *log_file;
  char log_filepath[1024];
#endif

  if (IS_INVALID_SOCKET (sock_fd))
    return;

#ifdef	_DEBUG_
#if !defined (DO_NOT_USE_CUBRIDENV)
  sprintf (log_filepath, "%s/tmp/getfile.log", sco.szCubrid);
#else
  sprintf (log_filepath, "%s/getfile.log", CUBRID_TMPDIR);
#endif
  log_file = fopen (log_filepath, "w+");
#endif

  memset (buf, '\0', sizeof (buf));
  memset (file_name, 0, sizeof (file_name));

  file_send_flag = 1;
  /* point to file whether transfer or no */

  res_file = fopen (filename, "r");
  index = 0;
  if (res_file)
    {
      while (1)
	{
	  fgets (buf, 1024, res_file);
	  if (write_to_socket (sock_fd, buf, strlen (buf)) < 0)
	    {
	      goto finalize;
	    }

	  if (strncmp (buf, "status:failure", 14) == 0)
	    file_send_flag = 0;
	  /* nothing to send any file */

	  if (strncmp (buf, "file_num", 8) == 0)
	    {
	      FREE_MEM (file_size);
	      FREE_MEM (del_flag);
	      file_num = atoi (buf + 9);
	      if (file_num <= 0 || file_num >= INT_MAX)
		{
		  file_num = -1;
		  goto finalize;
		}

	      file_size = (long *) malloc (sizeof (long) * file_num);
	      if (file_size == NULL)
		{
		  goto finalize;
		}
	      memset (file_size, 0, sizeof (long) * file_num);

	      del_flag = (int *) malloc (sizeof (int) * file_num);
	      if (del_flag == NULL)
		{
		  goto finalize;
		}
	      memset (del_flag, 0, sizeof (int) * file_num);
	    }

	  if (strncmp (buf, "open:file", 9) == 0)
	    {
	      fgets (buf, 1024, res_file);	/* filename */
	      if (write_to_socket (sock_fd, buf, strlen (buf)) < 0)
		{
		  goto finalize;
		}

	      file_name[index] = strdup (buf + 9);
	      *(file_name[index] + strlen (file_name[index]) - 1) = '\0';

	      fgets (buf, 1024, res_file);	/* filestatus */
	      if (write_to_socket (sock_fd, buf, strlen (buf)) < 0)
		{
		  goto finalize;
		}

	      fgets (buf, 1024, res_file);	/* file size */
	      if (write_to_socket (sock_fd, buf, strlen (buf)) < 0)
		{
		  goto finalize;
		}
	      if (file_size == NULL)
		{
		  goto finalize;
		}
	      file_size[index] = atoi (buf + 9);

	      fgets (buf, 1024, res_file);	/* delflag */
	      if (write_to_socket (sock_fd, buf, strlen (buf)) < 0)
		{
		  goto finalize;
		}

	      if (del_flag == NULL)
		{
		  goto finalize;
		}
	      del_flag[index] = (*(buf + 8) == 'y') ? 1 : 0;
	      fgets (buf, 1024, res_file);	/* close:file */
	      if (write_to_socket (sock_fd, buf, strlen (buf)) < 0)
		{
		  goto finalize;
		}

	      index++;
	      if (file_num == index)
		break;
	    }
	}
      write_to_socket (sock_fd, "\n", 1);
      /* send \n to indicate last data because used fgets */
      fclose (res_file);
    }

  /* file send */
  if (file_send_flag == 1)
    {
      /* stay client's response */
      char recv_buf[100];
      int total_send;
      memset (recv_buf, '\0', sizeof (recv_buf));

      for (index = 0; index < file_num; index++)
	{
	  total_send = 0;

	  if (recv (sock_fd, recv_buf, sizeof (recv_buf), 0) < 0)
	    {
#ifndef	WINDOWS
	      if (errno == EINTR)
		{
		  break;
		}
	      else if (errno == EBADF)
		{
		  break;
		}
	      else if (errno == ENOTSOCK)
		{
		  break;
		}
	      else
		{
		  break;
		}
#else
	      break;
#endif
	    }
	  else if (strncmp (recv_buf, "ACK", 3) != 0)
	    {
	      break;
	    }
#ifdef	_DEBUG_
	  if (log_file)
	    {
	      fprintf (log_file, "filename : %s\n", file_name[index]);
	      fprintf (log_file, "filesize : %d bytes\n", file_size[index]);
	    }
	  total_send =
	    send_file_to_client (sock_fd, file_name[index], log_file);
#else
	  total_send = send_file_to_client (sock_fd, file_name[index], NULL);
#endif

	  if ((file_size == NULL) || (total_send != file_size[index]))
	    {
#ifdef	_DEBUG_
	      fprintf (log_file,
		       "file send error totalsend:%d file_size:%d\n",
		       total_send, file_size[index]);
#endif
	      break;
	    }
#ifdef	_DEBUG_
	  fprintf (log_file, "file_name : %s\n", file_name[index]);
#endif
	}
    }

finalize:
  for (index = 0; index < file_num; index++)
    {
      if ((del_flag != NULL) && (del_flag[index] == 1))
	{
	  unlink (file_name[index]);
	}
      /* remove compress file */

      FREE_MEM (file_name[index]);
    }

  FREE_MEM (file_size);
  FREE_MEM (del_flag);
#ifdef	_DEBUG_
  if (log_file)
    fclose (log_file);
#endif
}

int
send_file_to_client (SOCKET sock_fd, char *file_name, FILE * log_file)
{
  int send_file;
  int read_len;
  long total_send;
  char buf[5120];

  memset (buf, '\0', sizeof (buf));
#ifdef	WINDOWS
  send_file = open (file_name, O_RDONLY | O_BINARY);
#else
  send_file = open (file_name, O_RDONLY);
#endif

  total_send = 0;

  if (send_file < 0)
    {
      return 0;
    }

  while ((read_len = read (send_file, buf, sizeof (buf))) > 0)
    {
      if (write_to_socket (sock_fd, buf, read_len) != -1)
	{
	  total_send += read_len;
	}
      else
	{
	  CLOSE_SOCKET (sock_fd);
#ifdef	_DEBUG_
	  fprintf (log_file, "\nCan't Send to socket...\n");
#endif
	  break;
	}
    }
  close (send_file);
#ifdef	_DEBUG_
  if (log_file)
    {
      fprintf (log_file, "\nSend to client(in func) => %d byte\n",
	       total_send);
    }
#endif
  return total_send;
}


void
ut_daemon_start (void)
{
#if defined(WINDOWS)
  return;
#else
  int childpid;

  /* Ignore the terminal stop signals */
  signal (SIGTTOU, SIG_IGN);
  signal (SIGTTIN, SIG_IGN);
  signal (SIGTSTP, SIG_IGN);

#if 0
  /* to make it run in background */
  signal (SIGHUP, SIG_IGN);
  childpid = PROC_FORK ();
  if (childpid > 0)
    exit (0);			/* kill parent */
#endif

  /* setpgrp(); */
  setsid ();			/* become process group leader and  */
  /* disconnect from control terminal */

  signal (SIGHUP, SIG_IGN);
  childpid = PROC_FORK ();	/* to prevent from reaquiring control terminal */
  if (childpid > 0)
    exit (0);			/* kill parent */

#if 0
  /* change current working directory */
  chdir ("/");
  /* clear umask */
  umask (0);
#endif
#endif /* ifndef WINDOWS */
}

int
ut_write_pid (char *pid_file)
{
  FILE *pidfp;

  pidfp = fopen (pid_file, "w");
  if (pidfp == NULL)
    {
      perror ("fopen");
      return -1;
    }
  fprintf (pidfp, "%d\n", (int) getpid ());
  fclose (pidfp);
  return 0;
}

void
server_fd_clear (SOCKET srv_fd)
{
#if !defined(WINDOWS)
  SOCKET i;
  int fd;

  for (i = 3; i < 1024; i++)
    {
      if (i != srv_fd)
	close (i);
    }

  fd = open ("/dev/null", O_RDWR);
#ifndef DIAG_DEBUG
  dup2 (fd, 1);
#endif
  dup2 (fd, 2);
#endif /* !WINDOWS */
}

int
uRetrieveDBDirectory (const char *dbname, char *target)
{
  int ret_val;
#ifdef	WINDOWS
  char temp_name[512];
#endif

  ret_val = uReadDBtxtFile (dbname, 1, target);
#ifdef	WINDOWS
  if (ret_val == ERR_NO_ERROR)
    {
      strcpy (temp_name, target);
      memset (target, '\0', strlen (target));
      if (GetLongPathName (temp_name, target, MAX_PATH) == 0)
	{
	  strcpy (target, temp_name);
	}
    }
#endif

  return ret_val;
}


int
_isRegisteredDB (char *dn)
{
  if (uReadDBtxtFile (dn, 0, NULL) == ERR_NO_ERROR)
    return 1;

  return 0;
}

/* dblist should have enough space  */
/* db names are delimited by '\0' for ease of using it */
int
uReadDBnfo (char *dblist)
{
  int retval = 0;
  char strbuf[512];
  char *p;
  FILE *infp;
  int lock_fd;

  lock_fd =
    uCreateLockFile (conf_get_dbmt_file (FID_LOCK_PSVR_DBINFO, strbuf));
  if (lock_fd < 0)
    return -1;

  infp = fopen (conf_get_dbmt_file (FID_PSVR_DBINFO_TEMP, strbuf), "r");
  if (infp == NULL)
    {
      retval = -1;
    }
  else
    {
      fgets (strbuf, sizeof (strbuf), infp);
      retval = atoi (strbuf);

      p = dblist;
      while (fgets (strbuf, sizeof (strbuf), infp))
	{
	  ut_trim (strbuf);
	  strcpy (p, strbuf);
	  p += (strlen (p) + 1);
	}
      fclose (infp);
    }

  uRemoveLockFile (lock_fd);

  return retval;
}

void
uWriteDBnfo (void)
{
  T_SERVER_STATUS_RESULT *cmd_res;

  cmd_res = cmd_server_status ();
  uWriteDBnfo2 (cmd_res);
  cmd_servstat_result_free (cmd_res);
}

void
uWriteDBnfo2 (T_SERVER_STATUS_RESULT * cmd_res)
{
  int i;
  int dbcnt;
  char strbuf[1024];
  int dbvect[MAX_INSTALLED_DB];
  int lock_fd;
  FILE *outfp;
  T_SERVER_STATUS_INFO *info;

  lock_fd =
    uCreateLockFile (conf_get_dbmt_file (FID_LOCK_PSVR_DBINFO, strbuf));
  if (lock_fd < 0)
    return;

  outfp = fopen (conf_get_dbmt_file (FID_PSVR_DBINFO_TEMP, strbuf), "w");
  if (outfp != NULL)
    {
      dbcnt = 0;
      if (cmd_res == NULL)
	{
	  fprintf (outfp, "%d\n", dbcnt);
	}
      else
	{
	  info = (T_SERVER_STATUS_INFO *) cmd_res->result;
	  for (i = 0; i < cmd_res->num_result; i++)
	    {
	      if (_isRegisteredDB (info[i].db_name))
		{
		  dbvect[dbcnt] = i;
		  ++dbcnt;
		}
	    }
	  fprintf (outfp, "%d\n", dbcnt);
	  info = (T_SERVER_STATUS_INFO *) cmd_res->result;
	  for (i = 0; i < dbcnt; i++)
	    fprintf (outfp, "%s\n", info[dbvect[i]].db_name);
	}
      fclose (outfp);
    }

  uRemoveLockFile (lock_fd);
}

int
uCreateLockFile (char *lockfile)
{
  int outfd;
#if !defined(WINDOWS)
  struct flock lock;
#endif

  outfd = open (lockfile, O_WRONLY | O_CREAT | O_TRUNC, 0666);

  if (outfd < 0)
    return outfd;

#if defined(WINDOWS)
  while (_locking (outfd, _LK_NBLCK, 1) < 0)
    Sleep (100);
#else
  lock.l_type = F_WRLCK;
  lock.l_start = 0;
  lock.l_whence = SEEK_SET;
  lock.l_len = 0;

  while (fcntl (outfd, F_SETLK, &lock) < 0)
    SLEEP_MILISEC (0, 10);
#endif

  return outfd;
}

void
uRemoveLockFile (int outfd)
{
#if !defined(WINDOWS)
  struct flock lock;

  lock.l_type = F_UNLCK;
  lock.l_start = 0;
  lock.l_whence = SEEK_SET;
  lock.l_len = 0;
#endif

#if defined(WINDOWS)
  _locking (outfd, _LK_UNLCK, 1);
#else
  fcntl (outfd, F_SETLK, &lock);
#endif
  close (outfd);
}

int
uRemoveDir (char *dir, int remove_file_in_dir)
{
  char path[1024];
  char command[2048];

  if (dir == NULL)
    return ERR_DIR_REMOVE_FAIL;

  strcpy (path, dir);
  memset (command, '\0', sizeof (command));
  ut_trim (path);

#if defined(WINDOWS)
  unix_style_path (path);
#endif

  if (access (path, F_OK) == 0)
    {
      if (remove_file_in_dir == REMOVE_DIR_FORCED)
	{
	  sprintf (command, "%s %s %s", DEL_DIR, DEL_DIR_OPT, path);
	  if (system (command) == -1)
	    return ERR_DIR_REMOVE_FAIL;
	}
      else
	{
	  if (rmdir (path) == -1)
	    return ERR_DIR_REMOVE_FAIL;
	}
    }
  else
    return ERR_DIR_REMOVE_FAIL;

  return ERR_NO_ERROR;
}



int
uCreateDir (char *new_dir)
{
  char *p, path[1024];

  if (new_dir == NULL)
    return ERR_DIR_CREATE_FAIL;

  strcpy (path, new_dir);
  ut_trim (path);

#if defined(WINDOWS)
  unix_style_path (path);
#endif

#if defined(WINDOWS)
  if (path[0] == '/')
    p = path + 1;
  else if (strlen (path) > 3 && path[2] == '/')
    p = path + 3;
  else
    return ERR_DIR_CREATE_FAIL;
#else
  if (path[0] != '/')
    return ERR_DIR_CREATE_FAIL;
  p = path + 1;
#endif

  while (p != NULL)
    {
      p = strchr (p, '/');
      if (p != NULL)
	*p = '\0';
      if (access (path, F_OK) < 0)
	{
	  if (mkdir (path, 0700) < 0)
	    {
	      return ERR_DIR_CREATE_FAIL;
	    }
	}
      if (p != NULL)
	{
	  *p = '/';
	  p++;
	}
    }
  return ERR_NO_ERROR;
}

void
close_all_fds (int init_fd)
{
  int i;

  for (i = init_fd; i < 1024; i++)
    {
      close (i);
    }
}

char *
ut_trim (char *str)
{
  char *p;
  char *s;

  if (str == NULL)
    return (str);

  for (s = str;
       *s != '\0' && (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r');
       s++)
    ;
  if (*s == '\0')
    {
      *str = '\0';
      return (str);
    }

  /* *s must be a non-white char */
  for (p = s; *p != '\0'; p++)
    ;
  for (p--; *p == ' ' || *p == '\t' || *p == '\n' || *p == '\r'; p--)
    ;
  *++p = '\0';

  if (s != str)
    memmove (str, s, strlen (s) + 1);

  return (str);
}

#if defined(WINDOWS)
int
ut_disk_free_space (char *path)
{
  char buf[1024];
  DWORD a, b, c, d;

  strcpy (buf, path);
  ut_trim (buf);
  if (buf[1] == ':')
    strcpy (buf + 2, "/");
  else
    strcpy (buf, "c:/");

  if (GetDiskFreeSpace (buf, &a, &b, &c, &d) == 0)
    return 0;

  return ((c >> 10) * ((b * a) >> 10));
}
#else
int
ut_disk_free_space (char *path)
{
  struct statvfs sv;

  if (statvfs (path, &sv) < 0)
    return 0;

  return ((((sv.f_bavail) >> 10) * sv.f_frsize) >> 10);
}
#endif

char *
ip2str (unsigned char *ip, char *ip_str)
{
  sprintf (ip_str, "%d.%d.%d.%d", (unsigned char) ip[0],
	   (unsigned char) ip[1],
	   (unsigned char) ip[2], (unsigned char) ip[3]);
  return ip_str;
}

int
string_tokenize_accept_laststring_space (char *str, char *tok[], int num_tok)
{
  int i;
  char *p;

  tok[0] = str;
  for (i = 1; i < num_tok; i++)
    {
      tok[i] = strpbrk (tok[i - 1], " \t");
      if (tok[i] == NULL)
	return -1;
      *(tok[i]) = '\0';
      p = (tok[i]) + 1;
      for (; *p && (*p == ' ' || *p == '\t'); p++)
	;
      if (*p == '\0')
	return -1;
      tok[i] = p;
    }

  return 0;
}

int
string_tokenize (char *str, char *tok[], int num_tok)
{
  int i;
  char *p;

  tok[0] = str;
  for (i = 1; i < num_tok; i++)
    {
      tok[i] = strpbrk (tok[i - 1], " \t");
      if (tok[i] == NULL)
	return -1;
      *(tok[i]) = '\0';
      p = (tok[i]) + 1;
      for (; *p && (*p == ' ' || *p == '\t'); p++)
	;
      if (*p == '\0')
	return -1;
      tok[i] = p;
    }
  p = strpbrk (tok[num_tok - 1], " \t");
  if (p)
    *p = '\0';

  return 0;
}

int
string_tokenize2 (char *str, char *tok[], int num_tok, int c)
{
  int i;
  char *p;

  for (i = 0; i < num_tok; i++)
    tok[i] = NULL;

  if (str == NULL)
    return -1;

  tok[0] = str;
  for (i = 1; i < num_tok; i++)
    {
      tok[i] = strchr (tok[i - 1], c);
      if (tok[i] == NULL)
	return -1;
      *(tok[i]) = '\0';
      (tok[i])++;
    }
  p = strchr (tok[num_tok - 1], c);
  if (p)
    *p = '\0';

  return 0;
}


int
read_from_socket (SOCKET sock_fd, char *buf, int size)
{
  int read_len;
  fd_set read_mask;
  int nfound;
  int maxfd;
  struct timeval timeout_val;

  timeout_val.tv_sec = 5;
  timeout_val.tv_usec = 0;

  FD_ZERO (&read_mask);
  FD_SET (sock_fd, (fd_set *) & read_mask);
  maxfd = (int) sock_fd + 1;
  nfound =
    select (maxfd, &read_mask, (fd_set *) 0, (fd_set *) 0, &timeout_val);
  if (nfound < 0)
    {
      return -1;
    }

  if (FD_ISSET (sock_fd, (fd_set *) & read_mask))
    {
      read_len = recv (sock_fd, buf, size, 0);
    }
  else
    {
      return -1;
    }

  return read_len;
}

int
write_to_socket (SOCKET sock_fd, const char *buf, int size)
{
  int write_len;
  fd_set write_mask;
  int nfound;
  int maxfd;
  struct timeval timeout_val;

  timeout_val.tv_sec = 5;
  timeout_val.tv_usec = 0;

  if (IS_INVALID_SOCKET (sock_fd))
    {
      return -1;
    }

  FD_ZERO (&write_mask);
  FD_SET (sock_fd, (fd_set *) & write_mask);
  maxfd = (int) sock_fd + 1;
  nfound =
    select (maxfd, (fd_set *) 0, &write_mask, (fd_set *) 0, &timeout_val);
  if (nfound < 0)
    {
      return -1;
    }

  if (FD_ISSET (sock_fd, (fd_set *) & write_mask))
    {
      write_len = send (sock_fd, buf, size, 0);
    }
  else
    {
      return -1;
    }

  return write_len;
}

#if defined(WINDOWS)
int
kill (int pid, int signo)
{
  HANDLE phandle;

  phandle = OpenProcess (PROCESS_TERMINATE, FALSE, pid);
  if (phandle == NULL)
    {
      int error = GetLastError ();
      if (error == ERROR_ACCESS_DENIED)
	errno = EPERM;
      else
	errno = ESRCH;
      return -1;
    }

  if (signo == SIGTERM)
    TerminateProcess (phandle, 0);

  CloseHandle (phandle);
  return 0;
}
#endif

#if defined(WINDOWS)
void
unix_style_path (char *path)
{
  char *p;
  for (p = path; *p; p++)
    {
      if (*p == '\\')
	*p = '/';
    }
}

char *
nt_style_path (char *path, char *new_path_buf)
{
  char *p;
  char *q;
  char tmp_path_buf[1024];

  if (path == new_path_buf)
    {
      strcpy (tmp_path_buf, path);
      q = tmp_path_buf;
    }
  else
    {
      q = new_path_buf;
    }
  if (strlen (path) < 2 || path[1] != ':')
    {
      *q++ = _getdrive () + 'A' - 1;
      *q++ = ':';
      *q++ = '\\';
    }

  for (p = path; *p; p++, q++)
    {
      if (*p == '/')
	*q = '\\';
      else
	*q = *p;
    }
  *q = '\0';
  for (q--; q != new_path_buf; q--)
    {
      if (*q == '\\')
	*q = '\0';
      else
	break;
    }
  if (*q == ':')
    {
      q++;
      *q++ = '\\';
      *q++ = '\\';
      *q = '\0';
    }
  if (path == new_path_buf)
    {
      strcpy (new_path_buf, tmp_path_buf);
    }
  return new_path_buf;
}
#endif

void
wait_proc (int pid)
{
#if defined(WINDOWS)
  HANDLE h;
#endif

  if (pid <= 0)
    return;

#if defined(WINDOWS)
  h = OpenProcess (SYNCHRONIZE, FALSE, pid);
  if (h)
    {
      WaitForSingleObject (h, INFINITE);
      CloseHandle (h);
    }
#else
  while (1)
    {
      if (kill (pid, 0) < 0)
	break;
      SLEEP_MILISEC (0, 100);
    }
#endif
}

int
make_version_info (char *cli_ver, int *major_ver, int *minor_ver)
{
  char *p;
  if (cli_ver == NULL)
    return 0;

  p = cli_ver;
  *major_ver = atoi (p);
  p = strchr (p, '.');
  if (p != NULL)
    *minor_ver = atoi (p + 1);
  else
    *minor_ver = 0;

  return 1;
}

int
file_copy (char *src_file, char *dest_file)
{
  char strbuf[1024];
  int src_fd, dest_fd;
  size_t read_size, write_size;

  if ((src_fd = open (src_file, O_RDONLY)) == -1)
    return -1;

  if ((dest_fd = open (dest_file, O_WRONLY | O_CREAT | O_TRUNC, 0644)) == -1)
    {
      close (src_fd);
      return -1;
    }

#if defined(WINDOWS)
  if (setmode (src_fd, O_BINARY) == -1 || setmode (dest_fd, O_BINARY) == -1)
    {
      close (src_fd);
      close (dest_fd);
      return -1;
    }
#endif

  while ((read_size = read (src_fd, strbuf, sizeof (strbuf))) > 0)
    {
      if (read_size > sizeof (strbuf)
	  || (write_size = write (dest_fd, strbuf, read_size)) < read_size)
	{
	  close (src_fd);
	  close (dest_fd);
	  unlink (dest_file);
	  return -1;
	}
    }

  close (src_fd);
  close (dest_fd);

  return 0;
}

int
move_file (char *src_file, char *dest_file)
{
  if (file_copy (src_file, dest_file) < 0)
    {
      return -1;
    }
  else
    unlink (src_file);

  return 0;
}

#if defined(WINDOWS)
void
remove_end_of_dir_ch (char *path)
{
  if (path && path[strlen (path) - 1] == '\\')
    path[strlen (path) - 1] = '\0';
}
#endif

int
is_cmserver_process (int pid, const char *module_name)
{
#if defined(WINDOWS)
  HANDLE hModuleSnap = NULL;
  MODULEENTRY32 me32 = { 0 };

  hModuleSnap = CreateToolhelp32Snapshot (TH32CS_SNAPMODULE, pid);
  if (hModuleSnap == (HANDLE) - 1)
    return -1;

  me32.dwSize = sizeof (MODULEENTRY32);

  if (Module32First (hModuleSnap, &me32))
    {
      do
	{
	  if (strcasecmp (me32.szModule, module_name) == 0)
	    {
	      CloseHandle (hModuleSnap);
	      return 1;
	    }
	}
      while (Module32Next (hModuleSnap, &me32));
    }
  CloseHandle (hModuleSnap);
  return 0;
#else
  const char *argv[10];
  int argc = 0;
  char cmjs_pid[10];
  char result_file[1024];
  char buf[1024], cur_pid[10], prog_name[32];
  FILE *fRes;

  snprintf (cmjs_pid, sizeof (cmjs_pid) - 1, "%d", pid);
  snprintf (result_file, sizeof (result_file) - 1, "%s/DBMT_js.%d",
	    sco.dbmt_tmp_dir, (int) getpid ());
  argv[argc++] = "/bin/ps";
  argv[argc++] = "-e";
  argv[argc] = NULL;

  if (run_child (argv, 1, NULL, result_file, NULL, NULL) < 0)
    {				/* ps */
      return -1;
    }

  fRes = fopen (result_file, "r");
  if (fRes)
    {
      while (fgets (buf, 1024, fRes))
	{
	  if (sscanf (buf, "%9s %*s %*s %31s", cur_pid, prog_name) != 2)
	    {
	      continue;
	    }

	  if (!strcmp (cur_pid, cmjs_pid) && !strcmp (prog_name, module_name))
	    {
	      fclose (fRes);
	      unlink (result_file);
	      return 1;
	    }
	}

      fclose (fRes);
    }

  unlink (result_file);
  return 0;
#endif
}

int
make_default_env (void)
{
  int retval = ERR_NO_ERROR;
  char strbuf[512];
  FILE *fd;

  /* create log/manager directory */
#if !defined (DO_NOT_USE_CUBRIDENV)
  sprintf (strbuf, "%s/%s", sco.szCubrid, DBMT_LOG_DIR);
#else
  sprintf (strbuf, "%s", DBMT_LOG_DIR);
#endif
  if ((retval = uCreateDir (strbuf)) != ERR_NO_ERROR)
    return retval;

  /* create var/manager direcory */
#if !defined (DO_NOT_USE_CUBRIDENV)
  sprintf (strbuf, "%s/%s", sco.szCubrid, DBMT_PID_DIR);
#else
  sprintf (strbuf, "%s", DBMT_PID_DIR);
#endif
  if ((retval = uCreateDir (strbuf)) != ERR_NO_ERROR)
    return retval;

  /* if databases.txt file doesn't exist, create 0 byte file. */
  sprintf (strbuf, "%s/%s", sco.szCubrid_databases, CUBRID_DATABASE_TXT);
  if (access (strbuf, F_OK) < 0)
    {
      if ((fd = fopen (strbuf, "a")) == NULL)
	return ERR_FILE_CREATE_FAIL;
      fclose (fd);
    }
  return retval;
}
