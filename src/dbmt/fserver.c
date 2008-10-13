/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * fserver.c - 
 */

#ident "$Id$"

#include <stdio.h>
#include <stdlib.h>		/* atoi()    */
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <ctype.h>		/* isalnum() */
#include <sys/stat.h>		/* stat()    */
#include <fcntl.h>

#ifdef WIN32
#include <winsock2.h>
#include <windows.h>
#include <process.h>
#include <io.h>
#else
#include <dirent.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>		/* getpid()  */
#include <netinet/in.h>
#include <pthread.h>
#include <netinet/tcp.h>
#endif

#include "dbmt_porting.h"
#include "nameval.h"
#include "server_util.h"
#include "dbmt_config.h"
#include "fserver_task.h"
#include "dbi.h"
#ifdef WIN32
#include "wsa_init.h"
#endif

#ifdef	_DEBUG_
#include "deb.h"
#endif
typedef struct
{
  char req_filename[512];
  char res_filename[512];
  int childpid;
  int clt_sock_fd;
} T_THR_ARG;

char g_pidfile_path[1024] = "";

static void service_start (void);
static THREAD_FUNC fserver_slave_thr_f (void *arg);
static int is_conflict (char *task_entered, char *dbname_entered);
static void print_usage (char *pname);
static void start_fserver (void);
static int net_init ();
static void send_res_file (int sock_fd, char *filename);

static int fserver_sockfd;
static FILE *start_log_fp;

#ifdef WIN32
int
CtrlHandler (DWORD fdwCtrlType)
{
  switch (fdwCtrlType)
    {
    case CTRL_C_EVENT:
    case CTRL_CLOSE_EVENT:
    case CTRL_SHUTDOWN_EVENT:
      if (strlen (g_pidfile_path) > 0)
	unlink (g_pidfile_path);
      return FALSE;
    case CTRL_BREAK_EVENT:
    case CTRL_LOGOFF_EVENT:
    default:
      return FALSE;
    }
}
#else
static void
term_handler (int signo)
{
  if (strlen (g_pidfile_path) > 0)
    unlink (g_pidfile_path);
  exit (0);
}
#endif

static THREAD_FUNC
fserver_slave_thr_f (void *arg)
{
  char *req_filename = ((T_THR_ARG *) arg)->req_filename;
  char *res_filename = ((T_THR_ARG *) arg)->res_filename;
  int childpid = ((T_THR_ARG *) arg)->childpid;
  int clt_sock_fd = ((T_THR_ARG *) arg)->clt_sock_fd;
  int i;

  wait_proc (childpid);
  for (i = 0; i < 30; i++)
    {
      if (unlink (req_filename) < 0)
	{
	  SLEEP_MILISEC (0, 100);
	}
      else
	{
	  break;
	}
    }
  send_res_file (clt_sock_fd, res_filename);
  for (i = 0; i < 30; i++)
    {
      if (unlink (res_filename) < 0)
	{
	  SLEEP_MILISEC (0, 100);
	}
      else
	{
	  break;
	}
    }

  CLOSE_SOCKET (clt_sock_fd);

  free ((T_THR_ARG *) arg);

#ifdef WIN32
  return;
#else
  return NULL;
#endif
}

typedef struct
{
  short run_task;
  char check_dbname;
  char check_task;
  short conflict_task[6];
} T_CONFLICT_TABLE;

static const T_CONFLICT_TABLE cft_table[] = {
  /* Check dbname only for all tasks. */
  {TS_CREATEDB, 1, 0, {-1}},
  {TS_DELETEDB, 1, 0, {-1}},
  {TS_RENAMEDB, 1, 0, {-1}},
  {TS_STARTDB, 1, 0, {-1}},
  {TS_STOPDB, 1, 0, {-1}},
  {TS_COPYDB, 1, 0, {-1}},
  {TS_OPTIMIZEDB, 1, 0, {-1}},
  {TS_CHECKDB, 1, 0, {-1}},
  {TS_COMPACTDB, 1, 0, {-1}},
  {TS_BACKUPDB, 1, 0, {-1}},
  {TS_ADDVOLDB, 1, 0, {-1}},
  {TS_UNLOADDB, 1, 0, {-1}},
  {TS_LOADDB, 1, 0, {-1}},
  {TS_RESTOREDB, 1, 0, {-1}},

  /* Check only the task name.                    */
  /* Dbmt user manamgement should be sequential.  */
  {TS_GETDBMTUSERINFO, 0, 1,
   {TS_GETDBMTUSERINFO, TS_DELETEDBMTUSER, TS_UPDATEDBMTUSER,
    TS_SETDBMTPASSWD, TS_ADDDBMTUSER, -1}},
  {TS_DELETEDBMTUSER, 0, 1,
   {TS_GETDBMTUSERINFO, TS_DELETEDBMTUSER, TS_UPDATEDBMTUSER,
    TS_SETDBMTPASSWD, TS_ADDDBMTUSER, -1}},
  {TS_UPDATEDBMTUSER, 0, 1,
   {TS_GETDBMTUSERINFO, TS_DELETEDBMTUSER, TS_UPDATEDBMTUSER,
    TS_SETDBMTPASSWD, TS_ADDDBMTUSER, -1}},
  {TS_SETDBMTPASSWD, 0, 1,
   {TS_GETDBMTUSERINFO, TS_DELETEDBMTUSER, TS_UPDATEDBMTUSER,
    TS_SETDBMTPASSWD, TS_ADDDBMTUSER, -1}},
  {TS_ADDDBMTUSER, 0, 1,
   {TS_GETDBMTUSERINFO, TS_DELETEDBMTUSER, TS_UPDATEDBMTUSER,
    TS_SETDBMTPASSWD, TS_ADDDBMTUSER, -1}},
  {TS_GETAUTOADDVOL, 0, 1, {TS_SETAUTOADDVOL, -1}},
  {TS_SETAUTOADDVOL, 0, 1, {TS_GETAUTOADDVOL, -1}},

  /* Check both the dbname and task name.    */
  /* System parameter should be sequential.  */
  {TS_SETSYSPARAM, 1, 1, {TS_SETSYSPARAM, TS_GETALLSYSPARAM, -1}},
#if 0
  {TS_SETSYSPARAM2, 1, 1, {TS_SETSYSPARAM, TS_GETALLSYSPARAM, -1}},
  {TS_GETSYSPARAM, 1, 1, {TS_SETSYSPARAM, TS_GETALLSYSPARAM, -1}},
#endif
  {TS_GETALLSYSPARAM, 1, 1, {TS_SETSYSPARAM, TS_GETALLSYSPARAM, -1}},

  /* Only the db modification tasks are prevented, check dbname. */
  {TS_GENERALDBINFO, 1, 1, {TS_DELETEDB, TS_RENAMEDB, TS_STOPDB, -1}},
  {TS_DBSPACEINFO, 1, 1, {TS_DELETEDB, TS_RENAMEDB, TS_STOPDB, -1}},
  {TS_USERINFO, 1, 1, {TS_DELETEDB, TS_RENAMEDB, TS_STOPDB, -1}},
  {TS_CREATEUSER, 1, 1, {TS_DELETEDB, TS_RENAMEDB, TS_STOPDB, -1}},
  {TS_DELETEUSER, 1, 1, {TS_DELETEDB, TS_RENAMEDB, TS_STOPDB, -1}},
  {TS_UPDATEUSER, 1, 1, {TS_DELETEDB, TS_RENAMEDB, TS_STOPDB, -1}},
  {TS_CLASSINFO, 1, 1, {TS_DELETEDB, TS_RENAMEDB, TS_STOPDB, -1}},
  {TS_CLASS, 1, 1, {TS_DELETEDB, TS_RENAMEDB, TS_STOPDB, -1}},
  {TS_RENAMECLASS, 1, 1, {TS_DELETEDB, TS_RENAMEDB, TS_STOPDB, -1}},
  {TS_DROPCLASS, 1, 1, {TS_DELETEDB, TS_RENAMEDB, TS_STOPDB, -1}},
  {TS_CREATECLASS, 1, 1, {TS_DELETEDB, TS_RENAMEDB, TS_STOPDB, -1}},
  {TS_CREATEVCLASS, 1, 1, {TS_DELETEDB, TS_RENAMEDB, TS_STOPDB, -1}},
  {TS_GETLOGINFO, 1, 1, {TS_DELETEDB, TS_RENAMEDB, TS_STOPDB, -1}},
  {TS_ADDATTRIBUTE, 1, 1, {TS_DELETEDB, TS_RENAMEDB, TS_STOPDB, -1}},
  {TS_DROPATTRIBUTE, 1, 1, {TS_DELETEDB, TS_RENAMEDB, TS_STOPDB, -1}},
  {TS_UPDATEATTRIBUTE, 1, 1, {TS_DELETEDB, TS_RENAMEDB, TS_STOPDB, -1}},
  {TS_ADDCONSTRAINT, 1, 1, {TS_DELETEDB, TS_RENAMEDB, TS_STOPDB, -1}},
  {TS_DROPCONSTRAINT, 1, 1, {TS_DELETEDB, TS_RENAMEDB, TS_STOPDB, -1}},
  {TS_GETSUPERCLASSESINFO, 1, 1, {TS_DELETEDB, TS_RENAMEDB, TS_STOPDB, -1}},
  {TS_ADDRESOLUTION, 1, 1, {TS_DELETEDB, TS_RENAMEDB, TS_STOPDB, -1}},
  {TS_DROPRESOLUTION, 1, 1, {TS_DELETEDB, TS_RENAMEDB, TS_STOPDB, -1}},
  {TS_ADDMETHOD, 1, 1, {TS_DELETEDB, TS_RENAMEDB, TS_STOPDB, -1}},
  {TS_DROPMETHOD, 1, 1, {TS_DELETEDB, TS_RENAMEDB, TS_STOPDB, -1}},
  {TS_UPDATEMETHOD, 1, 1, {TS_DELETEDB, TS_RENAMEDB, TS_STOPDB, -1}},
  {TS_ADDMETHODFILE, 1, 1, {TS_DELETEDB, TS_RENAMEDB, TS_STOPDB, -1}},
  {TS_DROPMETHODFILE, 1, 1, {TS_DELETEDB, TS_RENAMEDB, TS_STOPDB, -1}},
  {TS_ADDQUERYSPEC, 1, 1, {TS_DELETEDB, TS_RENAMEDB, TS_STOPDB, -1}},
  {TS_DROPQUERYSPEC, 1, 1, {TS_DELETEDB, TS_RENAMEDB, TS_STOPDB, -1}},
  {TS_CHANGEQUERYSPEC, 1, 1, {TS_DELETEDB, TS_RENAMEDB, TS_STOPDB, -1}},
  {TS_VALIDATEQUERYSPEC, 1, 1, {TS_DELETEDB, TS_RENAMEDB, TS_STOPDB, -1}},
  {TS_VALIDATEVCLASS, 1, 1, {TS_DELETEDB, TS_RENAMEDB, TS_STOPDB, -1}},
  {TS_ADDSUPER, 1, 1, {TS_DELETEDB, TS_RENAMEDB, TS_STOPDB, -1}},
  {TS_DROPSUPER, 1, 1, {TS_DELETEDB, TS_RENAMEDB, TS_STOPDB, -1}},
  {TS_VIEWLOG, 1, 1, {TS_DELETEDB, TS_RENAMEDB, TS_STOPDB, -1}},
  {TS_GETBACKUPINFO, 1, 1, {TS_DELETEDB, TS_RENAMEDB, TS_STOPDB, -1}},
  {TS_ADDBACKUPINFO, 1, 1, {TS_DELETEDB, TS_RENAMEDB, TS_STOPDB, -1}},
  {TS_DELETEBACKUPINFO, 1, 1, {TS_DELETEDB, TS_RENAMEDB, TS_STOPDB, -1}},
  {TS_SETBACKUPINFO, 1, 1, {TS_DELETEDB, TS_RENAMEDB, TS_STOPDB, -1}},
  {TS_GETDBERROR, 1, 1, {TS_DELETEDB, TS_RENAMEDB, TS_STOPDB, -1}},
  {TS_REGISTERLOCALDB, 1, 1, {TS_DELETEDB, TS_RENAMEDB, TS_STOPDB, -1}},
  {TS_REMOVELOCALDB, 1, 1, {TS_DELETEDB, TS_RENAMEDB, TS_STOPDB, -1}},
  {TS_ADDNEWTRIGGER, 1, 1, {TS_DELETEDB, TS_RENAMEDB, TS_STOPDB, -1}},
  {TS_ALTERTRIGGER, 1, 1, {TS_DELETEDB, TS_RENAMEDB, TS_STOPDB, -1}},
  {TS_DROPTRIGGER, 1, 1, {TS_DELETEDB, TS_RENAMEDB, TS_STOPDB, -1}},
  {TS_GETTRIGGERINFO, 1, 1, {TS_DELETEDB, TS_RENAMEDB, TS_STOPDB, -1}},
  {TS_GETFILE, 1, 1, {TS_DELETEDB, TS_RENAMEDB, TS_STOPDB, TS_BACKUPDB, -1}},
#if 0
  {TS_POPSPACEINFO, 1, 1, {TS_DELETEDB, TS_RENAMEDB, TS_STOPDB, -1}},
#endif
#if 0
  {TS_STARTINFO, 0, 0, {-1}},
  {TS_RESETLOG, 0, 0, {-1}},
  {TS_BACKUPDBINFO, 0, 0, {-1}},
  {TS_UNLOADDBINFO, 0, 0, {-1}},
  {TS_GETTRANINFO, 0, 0, {-1}},
  {TS_KILLTRAN, 0, 0, {-1}},
  {TS_LOCKDB, 0, 0, {-1}},
  {TS_GETBACKUPLIST, 0, 0, {-1}},
  {TS_BACKUPVOLINFO, 0, 0, {-1}},
  {TS_GETDBSIZE, 0, 0, {-1}},
  {TS_LOADACCESSLOG, 0, 0, {-1}},
  {TS_DELACCESSLOG, 0, 0, {-1}},
  {TS_DELERRORLOG, 0, 0, {-1}},
  {TS_CHECKDIR, 0, 0, {-1}},
  {TS_AUTOBACKUPDBERRLOG, 0, 0, {-1}},
  {TS_KILL_PROCESS, 0, 0, {-1}},
  {TS_GETENV, 0, 0, {-1}},
  {TS_GETACCESSRIGHT, 0, 0, {-1}},
  {TS_GETADDVOLSTATUS, 0, 0, {-1}},
  {TS_GETHISTORY, 0, 0, {-1}},
  {TS_SETHISTORY, 0, 0, {-1}},
  {TS_GETHISTORYFILELIST, 0, 0, {-1}},
  {TS_READHISTORYFILE, 0, 0, {-1}},
  {TS_CHECKAUTHORITY, 0, 0, {-1}},
  {TS_GETAUTOADDVOLLOG, 0, 0, {-1}},
  {TS2_GETINITUNICASINFO, 0, 0, {-1}},
  {TS2_GETUNICASINFO, 0, 0, {-1}},
  {TS2_STARTUNICAS, 0, 0, {-1}},
  {TS2_STOPUNICAS, 0, 0, {-1}},
  {TS2_GETADMINLOGINFO, 0, 0, {-1}},
  {TS2_GETLOGFILEINFO, 0, 0, {-1}},
  {TS2_ADDBROKER, 0, 0, {-1}},
  {TS2_GETADDBROKERINFO, 0, 0, {-1}},
  {TS2_DELETEBROKER, 0, 0, {-1}},
  {TS2_RENAMEBROKER, 0, 0, {-1}},
  {TS2_GETBROKERSTATUS, 0, 0, {-1}},
  {TS2_GETBROKERCONF, 0, 0, {-1}},
  {TS2_GETBROKERONCONF, 0, 0, {-1}},
  {TS2_SETBROKERCONF, 0, 0, {-1}},
  {TS2_SETBROKERONCONF, 0, 0, {-1}},
  {TS2_STARTBROKER, 0, 0, {-1}},
  {TS2_STOPBROKER, 0, 0, {-1}},
  {TS2_SUSPENDBROKER, 0, 0, {-1}},
  {TS2_RESUMEBROKER, 0, 0, {-1}},
  {TS2_BROKERJOBFIRST, 0, 0, {-1}},
  {TS2_BROKERJOBINFO, 0, 0, {-1}},
  {TS2_ADDBROKERAS, 0, 0, {-1}},
  {TS2_DROPBROKERAS, 0, 0, {-1}},
  {TS2_RESTARTBROKERAS, 0, 0, {-1}},
  {TS2_GETBROKERSTATUSLOG, 0, 0, {-1}},
  {TS2_GETBROKERMCONF, 0, 0, {-1}},
  {TS2_SETBROKERMCONF, 0, 0, {-1}},
  {TS2_GETBROKERASLIMIT, 0, 0, {-1}},
  {TS2_GETBROKERENVINFO, 0, 0, {-1}},
  {TS2_SETBROKERENVINFO, 0, 0, {-1}},
  {TS2_ACCESSLISTADDIP, 0, 0, {-1}},
  {TS2_ACCESSLISTDELETEIP, 0, 0, {-1}},
  {TS2_ACCESSLISTINFO, 0, 0, {-1}},
#endif
  {TS_DBMTUSERLOGIN, 1, 1, {-1}},
  {TS_CHANGEOWNER, 1, 1, {TS_DELETEDB, TS_RENAMEDB, TS_STOPDB, -1}},
  {TS_REMOVE_LOG, 0, 0, {-1}},
  {TS_UNDEFINED, 0, 0, {-1}}
};

static int
is_conflict (char *task_entered, char *dbname_entered)
{
#ifdef WIN32
  HANDLE handle;
  WIN32_FIND_DATA data;
  int found;
  char find_file[512];
#else
  DIR *dp;
  struct dirent *dirp;
#endif
  int te_num, tr_num, i, j;	/* each task number */
  int retval = -1;

#ifdef WIN32
  sprintf (find_file, "%s/DBMT_comm_*", sco.dbmt_tmp_dir);
  handle = FindFirstFile (find_file, &data);
  if (handle == INVALID_HANDLE_VALUE)
    return 0;
#else
  dp = opendir (sco.dbmt_tmp_dir);
  if (dp == NULL)
    return 0;
#endif

#ifdef WIN32
  for (found = 1; found; found = FindNextFile (handle, &data))
#else
  while ((dirp = readdir (dp)) != NULL)
#endif
    {
      /* if comm file is found, read task from it */
#ifndef WIN32
      if (!strncmp (dirp->d_name, "DBMT_comm_", 10))
#endif
	{
	  FILE *infile;
	  char strbuf[512];
	  char task_running[128], dbname_running[128];

#ifdef WIN32
	  sprintf (strbuf, "%s/%s", sco.dbmt_tmp_dir, data.cFileName);
#else
	  sprintf (strbuf, "%s/%s", sco.dbmt_tmp_dir, dirp->d_name);
#endif
	  infile = fopen (strbuf, "r");
	  if (infile == NULL)
	    continue;
	  memset (task_running, 0, sizeof (task_running));
	  memset (dbname_running, 0, sizeof (dbname_running));
	  while (fgets (strbuf, sizeof (strbuf), infile))
	    {
	      if (!strncmp (strbuf, "task:", 5))
		strcpy (task_running, strbuf + 5);
	      if (!strncmp (strbuf, "dbname:", 7))
		strcpy (dbname_running, strbuf + 7);
	    }
	  fclose (infile);
	  uRemoveCRLF (task_running);
	  uRemoveCRLF (dbname_running);

	  /* now check if new task conflicts with it */
	  tr_num = ut_get_task_info (task_running, NULL, NULL);
	  te_num = ut_get_task_info (task_entered, NULL, NULL);
	  /* for each entry in cft_set */
	  retval = -1;
	  for (i = 0; cft_table[i].run_task != TS_UNDEFINED; ++i)
	    {
	      /* locate the entry with task num of currently running task */
	      if (cft_table[i].run_task == tr_num)
		{
		  if (cft_table[i].check_task == 1)
		    {
		      retval = 0;
		      for (j = 0; cft_table[i].conflict_task[j] > 0; ++j)
			{
			  if (cft_table[i].conflict_task[j] == te_num)
			    retval = 1;
			}
		    }

		  if ((cft_table[i].check_dbname == 1) && (retval != 0))
		    {
		      if (uStringEqual (dbname_entered, dbname_running))
			retval = 1;
		      else
			retval = 0;
		    }

		  if (retval == 1)
		    {		/* conflict detected */
#ifdef WIN32
		      FindClose (handle);
#else
		      closedir (dp);
#endif
		      return 1;
		    }
		}
	    }
	}
    }
#ifdef WIN32
  FindClose (handle);
#else
  closedir (dp);
#endif

  return 0;			/* no conflict found */
}

static void
service_start (void)
{
  int newsockfd, childpid;
  struct sockaddr_in cli_addr;
  nvplist *cli_request, *cli_response;
  struct sockaddr_in temp_addr;
  T_SOCKLEN slen, clilen;
  T_THREAD fserver_slave_id;
  T_THR_ARG *thr_arg;
  int req_count = 0;
  char req_filename[512], res_filename[512];
  char cmd_name[512];
  char *argv[4];
  int one = 1;

  for (;;)
    {
      cli_request = nv_create (5, NULL, "\n", ":", "\n");
      cli_response = nv_create (5, NULL, "\n", ":", "\n");

      clilen = sizeof (cli_addr);
      newsockfd =
	accept (fserver_sockfd, (struct sockaddr *) &cli_addr, &clilen);
      if (newsockfd < 0)
	{
	  continue;
	}

      setsockopt (newsockfd, IPPROTO_TCP, TCP_NODELAY, (char *) &one,
		  sizeof (one));

      ut_receive_request (newsockfd, cli_request);

      /* conflict resolution */
      if (is_conflict
	  (nv_get_val (cli_request, "task"),
	   nv_get_val (cli_request, "dbname")))
	{
	  nv_add_nvp (cli_response, "task", nv_get_val (cli_request, "task"));
	  nv_add_nvp (cli_response, "status", "failure");
	  nv_add_nvp (cli_response, "note",
		      "This job can not be performed now. Please try again later.");

	  ut_send_response (newsockfd, cli_response);

	  nv_destroy (cli_request);
	  nv_destroy (cli_response);
	  CLOSE_SOCKET (newsockfd);
	  continue;
	}

      /* add server-specific information to the request */
      nv_add_nvp_int (cli_request, "_STAMP", req_count);
      nv_add_nvp (cli_request, "_SVRTYPE", "fsvr");
      nv_add_nvp (cli_request, "_PROGNAME", sco.szProgname);
      slen = sizeof (temp_addr);
      if (getpeername (newsockfd, (struct sockaddr *) &temp_addr, &slen) >= 0)
	{
	  nv_add_nvp (cli_request, "_CLIENTIP",
		      inet_ntoa (temp_addr.sin_addr));
	  nv_add_nvp_int (cli_request, "_CLIENTPORT", temp_addr.sin_port);
	}

      /* determine file name */
      sprintf (req_filename, "%s/DBMT_comm_%d.%d.req", sco.dbmt_tmp_dir,
	       (int) getpid (), req_count);
      sprintf (res_filename, "%s/DBMT_comm_%d.%d.res", sco.dbmt_tmp_dir,
	       (int) getpid (), req_count);
      req_count++;

      /* write request to file */
      nv_writeto (cli_request, req_filename);
      sprintf (cmd_name, "%s/bin/cub_job%s", sco.szCubrid, DBMT_EXE_EXT);
      argv[0] = cmd_name;
      argv[1] = req_filename;
      argv[2] = res_filename;
      argv[3] = NULL;

      childpid = run_child (argv, 0, NULL, NULL, NULL, NULL);	/* cub_job */

      if (childpid > 0)
	{
	  thr_arg = (T_THR_ARG *) malloc (sizeof (T_THR_ARG));
	  strcpy (thr_arg->req_filename, req_filename);
	  strcpy (thr_arg->res_filename, res_filename);
	  thr_arg->childpid = childpid;
	  thr_arg->clt_sock_fd = newsockfd;
	  THREAD_BEGIN (fserver_slave_id, fserver_slave_thr_f, thr_arg);
	  SLEEP_MILISEC (0, 100);
	}
      else
	{
	  char buf[1024];
	  sprintf (buf, "CreateProcess error : %s", cmd_name);
	  nv_add_nvp (cli_response, "task", nv_get_val (cli_request, "task"));
	  nv_add_nvp (cli_response, "status", "failure");
	  nv_add_nvp (cli_response, "note", buf);
	  ut_send_response (newsockfd, cli_response);
	  CLOSE_SOCKET (newsockfd);
	}
      nv_destroy (cli_request);
      nv_destroy (cli_response);
    }
}

static void
print_usage (char *pname)
{
  fprintf (start_log_fp, "Usage: %s [command]\n", pname);
  fprintf (start_log_fp, "commands are :\n");
  fprintf (start_log_fp, "   start   -  start the server\n");
  fprintf (start_log_fp, "   stop    -  stop the server\n");
}

static void
start_fserver (void)
{
#ifdef WIN32
  HANDLE handle;
  WIN32_FIND_DATA data;
  int found;
  char isolation_lv_env_name[32];
  char isolation_lv_env_value[4];
#else
  DIR *dirp = NULL;
  struct dirent *dp = NULL;
  char isolation_lv_env_string[64];
#endif
  char buf[512];

  /* change isolation level environment variable */
#ifdef	WIN32
  sprintf (isolation_lv_env_name, "CUBRID_ISOLATION_LEVEL");
  sprintf (isolation_lv_env_value, "%d", TRAN_COMMIT_CLASS_UNCOMMIT_INSTANCE);

  if (SetEnvironmentVariable (isolation_lv_env_name, isolation_lv_env_value)
      == 0)
    {
      fprintf (start_log_fp,
	       "Error while setting environment variable CUBRID_ISOLATION_LEVEL ---> continue\n");
    }
#else
  sprintf (isolation_lv_env_string, "CUBRID_ISOLATION_LEVEL=%d",
	   TRAN_COMMIT_CLASS_UNCOMMIT_INSTANCE);
  if (putenv (isolation_lv_env_string) != 0)
    {
      fprintf (start_log_fp,
	       "Error while setting environment variable CUBRID_ISOLATION_LEVEL ---> continue\n");
    }
#endif

#if 0
  fprintf (start_log_fp, "Server port : %d\n", sco.iFsvr_port);
  fprintf (start_log_fp, "Access log : %s\n",
	   conf_get_dbmt_file (FID_FSERVER_ACCESS_LOG, buf));
  fprintf (start_log_fp, "Error  log : %s\n",
	   conf_get_dbmt_file (FID_FSERVER_ERROR_LOG, buf));

  fprintf (start_log_fp, "Starting server as a daemon.\n");
#endif
  /* fprintf (start_log_fp, "\ncms start ... OK\n"); */
  ut_daemon_start ();

#ifdef WIN32
  SetConsoleCtrlHandler ((PHANDLER_ROUTINE) CtrlHandler, TRUE);
#else
  signal (SIGINT, term_handler);
  signal (SIGTERM, term_handler);
#endif

  if (net_init () < 0)
    exit (1);
  if (ut_write_pid (conf_get_dbmt_file (FID_FSERVER_PID, buf)) < 0)
    exit (1);

  fflush (start_log_fp);

  server_fd_clear (fserver_sockfd);

  /* Delete any temporary from from sco.dbmt_tmp_dir */
#ifdef WIN32
  sprintf (buf, "%s/*", sco.dbmt_tmp_dir);
  if ((handle = FindFirstFile (buf, &data)) != INVALID_HANDLE_VALUE)
#else
  if ((dirp = opendir (sco.dbmt_tmp_dir)) != NULL)
#endif
    {
#ifdef WIN32
      for (found = 1; found; found = FindNextFile (handle, &data))
#else
      while ((dp = readdir (dirp)) != NULL)
#endif
	{
	  char *p;

#ifdef WIN32
	  p = data.cFileName;
#else
	  p = dp->d_name;
#endif
	  if ((!strncmp (p, "DBMT_proc_", 10)) ||
	      (!strncmp (p, "DBMT_task_", 10)) ||
	      (!strncmp (p, "DBMT_comm_", 10)) ||
	      (!strncmp (p, "DBMT_util_", 10)) ||
	      (!strncmp (p, "DBMT_stat_", 10)) ||
	      (!strncmp (p, "DBMT_auto_", 10)))
	    {
	      sprintf (buf, "%s/%s", sco.dbmt_tmp_dir, p);
	      unlink (buf);
	    }
	}
#ifdef WIN32
      FindClose (handle);
#else
      closedir (dirp);
#endif
    }

  service_start ();
}

int
main (int argc, char **argv)
{
  FILE *pidfile;
  int pidnum;
  char *ars_cmd;

#ifndef WIN32
  signal (SIGCHLD, SIG_IGN);
  signal (SIGPIPE, SIG_IGN);
#endif

#ifdef WIN32
  start_log_fp = fopen ("cub_jsstart.log", "w");
  if (start_log_fp == NULL)
    start_log_fp = stdout;
#else
  start_log_fp = stdout;
#endif

  if (argc != 2)
    {
      fprintf (start_log_fp,
	       "Error : Wrong number of command line arguments.\n");
      print_usage (argv[0]);
      exit (1);
    }
  ars_cmd = argv[1];

  if (strcmp (ars_cmd, "--version") == 0)
    {
      fprintf (start_log_fp, "CUBRID Manager Server ver : %s\n",
	       makestring (BUILD_NUMBER));
      exit (1);
    }

#if 0
  fprintf (start_log_fp, "cub_js %s\n", makestring (BUILD_NUMBER));
#endif

  sys_config_init ();

  if (uReadEnvVariables (argv[0], start_log_fp) < 0)
    {
      fprintf (start_log_fp, "Error while reading environment variables.\n");
      exit (1);
    }

  if (uReadSystemConfig () < 0)
    {
      fprintf (start_log_fp,
	       "Error while reading system configuration file.\n");
      exit (1);
    }

  make_default_env ();

  /* check system configuration */
  if (uCheckSystemConfig (start_log_fp) < 0)
    {
      fprintf (start_log_fp,
	       "Error while checking system configuration file.\n");
      exit (1);
    }

  if (strcmp (ars_cmd, "start") == 0)
    {
      char tmpfile[512];

      SLEEP_MILISEC (0, 100);
      uRemoveLockFile (uCreateLockFile
		       (conf_get_dbmt_file (FID_PSERVER_PID_LOCK, tmpfile)));

      conf_get_dbmt_file (FID_PSERVER_PID, tmpfile);
      if (access (tmpfile, F_OK) < 0)
	{
	  fprintf (start_log_fp, "Error : %s not found.\n", tmpfile);
	  fprintf (start_log_fp,
		   "      : cub_auto may not be running. Start cub_auto first.\n");
	  exit (1);
	}
#if 0
      fprintf (start_log_fp, "%s found.\n", tmpfile);
#endif

      conf_get_dbmt_file (FID_FSERVER_PID, tmpfile);
      strcpy (g_pidfile_path, tmpfile);

      if (access (tmpfile, F_OK) < 0)
	{
	  start_fserver ();
	}
      else
	{
	  pidfile = fopen (tmpfile, "rt");
	  fscanf (pidfile, "%d", &pidnum);
	  fclose (pidfile);

	  if (((kill (pidnum, 0) < 0) && (errno == ESRCH)) ||
	      (is_cmserver_process (pidnum, FSERVER_MODULE_NAME) == 0))
	    {
	      /* fprintf (start_log_fp, "Previous pid file found. Removing and proceeding ...\n"); */
	      unlink (tmpfile);
	      start_fserver ();
	    }
	  else
	    {
	      fprintf (start_log_fp,
		       "Error : Server[pid=%d] already running.\n", pidnum);
	    }
	}
    }
  else if (strcmp (ars_cmd, "stop") == 0)
    {
      char tmpfile[512];

      conf_get_dbmt_file (FID_FSERVER_PID, tmpfile);
      if (access (tmpfile, F_OK) < 0)
	{
	  fprintf (start_log_fp,
		   "Error : Can not stop. Server not running.\n");
	  exit (1);
	}
      else
	{
	  pidfile = fopen (tmpfile, "rt");
	  fscanf (pidfile, "%d", &pidnum);
	  fclose (pidfile);

	  /* fprintf (start_log_fp, "Stopping server process with pid %d\n", pidnum); */
	  if ((kill (pidnum, SIGTERM)) < 0)
	    fprintf (start_log_fp, "Error : Failed to stop the server.\n");
	  else
	    unlink (tmpfile);
	}
    }
  else
    {
      fprintf (start_log_fp, "Error : Invalid command - %s\n", ars_cmd);
      print_usage (argv[0]);
      exit (1);
    }

  return 0;
}

static int
net_init ()
{
  int optval = 1;
  struct sockaddr_in serv_addr;

#ifdef WIN32
  if (wsa_initialize () < 0)
    return -1;
#endif

  if ((fserver_sockfd = socket (AF_INET, SOCK_STREAM, 0)) < 0)
    {
      perror ("socket");
      return -1;
    }

  memset ((char *) &serv_addr, 0, sizeof (serv_addr));
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_addr.s_addr = htonl (INADDR_ANY);
  serv_addr.sin_port = htons ((unsigned short) sco.iFsvr_port);

  if (setsockopt
      (fserver_sockfd, SOL_SOCKET, SO_REUSEADDR, (const char *) &optval,
       sizeof (optval)) < 0)
    {
      perror ("setsockopt");
      return -1;
    }

  if ((bind
       (fserver_sockfd, (struct sockaddr *) &serv_addr,
	sizeof (serv_addr))) < 0)
    {
      perror ("bind");
      return -1;
    }
  if ((listen (fserver_sockfd, 100)) < 0)
    {
      perror ("liseten");
      return -1;
    }
  return 0;
}

static void
send_res_file (int sock_fd, char *filename)
{
  int fd, read_len;
  char buf[1024];
  FILE *file;

  file = fopen (filename, "r");
  if (file)
    {
      fgets (buf, 1024, file);
      if ((strlen (buf) > 12) && (strncmp (buf, "task:getfile", 12) == 0))
	{
	  fgets (buf, 1024, file);
	  if ((strlen (buf) > 14)
	      && (strncmp (buf, "status:success", 14) == 0))
	    {
	      /* call send file function */
	      fclose (file);
	      send_msg_with_file (sock_fd, filename);
	      return;
	    }
	}
      fclose (file);
    }

  fd = open (filename, O_RDONLY);
  if (fd < 0)
    return;

  while ((read_len = read (fd, buf, sizeof (buf))) > 0)
    {
      write_to_socket (sock_fd, buf, read_len);
    }
  close (fd);
}
