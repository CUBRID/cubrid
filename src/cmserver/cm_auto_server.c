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
 * pserver.c -
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include <stdlib.h>		/* atoi()       */
#include <signal.h>		/* SIG_TTOU ... */
#include <sys/types.h>		/* AF_INET ... */
#include <string.h>		/* memset()    */
#include <sys/stat.h>		/* umask(), stat() */
#include <ctype.h>		/* isalnum() */
#include <fcntl.h>
#include <errno.h>

#if defined(WINDOWS)
#include <winsock.h>
#include <direct.h>
#include <io.h>
#include <process.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>		/* INADDR_ANY  */
#include <arpa/inet.h>		/* inet_ntoa() */
#include <unistd.h>		/* read(), write() */
#include <netinet/tcp.h>
#if !defined(HPUX) && !defined(AIX)
#include <sys/procfs.h>		/* umask(), stat() */
#endif
#include <pthread.h>
#include <dirent.h>
#include <sys/file.h>
#endif

#include "cm_porting.h"
#include "cm_server_stat.h"
#include "cm_server_util.h"
#include "cm_stat.h"
#include "cm_dep.h"
#include "cm_autojob.h"
#include "cm_auto_task.h"
#include "cm_config.h"
#include "cm_command_execute.h"
#include "cm_text_encryption.h"
#include "cm_connect_info.h"
#if defined(WINDOWS)
#include "cm_wsa_init.h"
#endif


#define NO_USER		1000
#define UNAUTHORIZED	1001
#define VALID_USER	1002

#define MAX_CLIENT_NUM	128

char g_pidfile_path[1024] = "";

static THREAD_FUNC service_start (void *ud);
static THREAD_FUNC automation_start (void *ud);
static void print_usage (char *progname);
static void start_pserver (void);
static char *ut_token_generate (char *client_ip, char *client_port,
				char *dbmt_id, int proc_id);
static int net_init (void);
static void prepare_response (userdata * ud, nvplist * res);
static void auto_start_UniCAS (void);
static void client_info_reset (T_CLIENT_INFO * client);

static SOCKET pserver_sockfd;
static FILE *start_log_fp;
static int pid_lock_fd;
static char cubrid_err_log_env[256];
static char cubrid_err_file[256];

#if defined(WINDOWS)
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
  g_pidfile_path[sizeof (g_pidfile_path) - 1] = '\0';

  if (strlen (g_pidfile_path) > 0)
    unlink (g_pidfile_path);
  exit (0);
}
#endif

int
main (int argc, char **argv)
{
  FILE *pidfile;
  int pidnum;
  char *ars_cmd;
  char pid_file_name[512];

#if defined(WINDOWS)
  FreeConsole ();
  start_log_fp = fopen ("cub_autostart.log", "w");
  if (start_log_fp == NULL)
    start_log_fp = stdout;
#else
  start_log_fp = stdout;

  signal (SIGPIPE, SIG_IGN);
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
  fprintf (start_log_fp, "cub_auto %s\n", makestring (BUILD_NUMBER));
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

  if (uCheckSystemConfig (start_log_fp) < 0)
    {
      fprintf (start_log_fp,
	       "Error while checking system configuration file.\n");
      exit (1);
    }

  pid_lock_fd =
    uCreateLockFile (conf_get_dbmt_file
		     (FID_PSERVER_PID_LOCK, pid_file_name));

  conf_get_dbmt_file (FID_PSERVER_PID, pid_file_name);

  strcpy (g_pidfile_path, pid_file_name);
  if (strcmp (ars_cmd, "start") == 0)
    {
      if (access (pid_file_name, F_OK) < 0)
	{
	  start_pserver ();
	}
      else
	{
	  pidfile = fopen (pid_file_name, "rt");
	  if (pidfile == NULL)
	    {
	      fprintf (start_log_fp,
		       "Error : pid file exists, but can not open it[%s].\n",
		       pid_file_name);
	    }
	  else
	    {
	      fscanf (pidfile, "%d", &pidnum);
	      fclose (pidfile);

	      if (((kill (pidnum, 0) < 0) && (errno == ESRCH)) ||
		  (is_cmserver_process (pidnum, PSERVER_MODULE_NAME) == 0))
		{
		  /* fprintf (start_log_fp, "Previous pid file found. Removing and proceeding...\n"); */
		  unlink (pid_file_name);
		  start_pserver ();
		}
	      else
		{
		  fprintf (start_log_fp,
			   "Error : Server[pid=%d] already running.\n",
			   pidnum);
		}
	    }
	}
    }
  else if (strcmp (ars_cmd, "stop") == 0)
    {
      if (access (pid_file_name, F_OK) < 0)
	{
	  fprintf (start_log_fp,
		   "Error : Can not stop. Server not running.\n");
	  exit (1);
	}
      else
	{
	  pidfile = fopen (pid_file_name, "rt");
	  if (pidfile != NULL)
	    {
	      fscanf (pidfile, "%d", &pidnum);
	      fclose (pidfile);
	    }
	  /* fprintf (start_log_fp, "Stopping polling server process with pid %d\n", pidnum); */
	  if ((pidfile == NULL) || (kill (pidnum, SIGTERM) < 0))
	    {
	      fprintf (start_log_fp, "Error : Failed to stop the server.\n");
	    }
	  else
	    {
	      char strbuf[512];
	      unlink (pid_file_name);
	      unlink (conf_get_dbmt_file (FID_CONN_LIST, strbuf));
	    }
	}
    }
  else if (strcmp (ars_cmd, "getpid") == 0)
    {
      if (access (pid_file_name, F_OK) < 0)
	{
	  exit (1);
	}
      else
	{
	  pidfile = fopen (pid_file_name, "rt");
	  if (pidfile != NULL)
	    {
	      fscanf (pidfile, "%d", &pidnum);
	      fclose (pidfile);
	    }

	  if (pidfile == NULL || ((kill (pidnum, 0) < 0) && (errno == ESRCH))
	      || (is_cmserver_process (pidnum, PSERVER_MODULE_NAME) == 0))
	    {
	      exit (1);
	    }
	  else
	    {
	      fprintf (stdout, "%d\n", pidnum);
	      exit (0);
	    }
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

static THREAD_FUNC
service_start (void *ud)
{
  SOCKET newsockfd, sockfd, maxfd;
  int i, maxi, nready;
  T_SOCKLEN clilen;
  nvplist *cli_request, *cli_response;
  char *pstrbuf;
  struct sockaddr_in cli_addr;
  fd_set rset, allset;
  T_CLIENT_INFO client_info[MAX_CLIENT_NUM];
  int one = 1;

  memset (client_info, '\0', sizeof (client_info));
  for (i = 0; i < MAX_CLIENT_NUM; i++)
    {
      client_info_reset (&(client_info[i]));
    }

  FD_ZERO (&allset);
  FD_SET (pserver_sockfd, &allset);
  maxfd = pserver_sockfd;
  maxi = -1;

  cli_request = nv_create (5, NULL, "\n", ":", "\n");
  cli_response = nv_create (5, NULL, "\n", ":", "\n");
  if ((cli_request == NULL) || (cli_response == NULL))
    {
      goto return_statement;
    }

  for (;;)
    {
      rset = allset;
      nready = select ((int) maxfd + 1, &rset, NULL, NULL, NULL);
      if (FD_ISSET (pserver_sockfd, &rset))
	{
	  clilen = sizeof (cli_addr);
	  newsockfd =
	    accept (pserver_sockfd, (struct sockaddr *) &cli_addr, &clilen);
	  setsockopt (newsockfd, IPPROTO_TCP, TCP_NODELAY, (char *) &one,
		      sizeof (one));

	  for (i = 0; i < MAX_CLIENT_NUM; ++i)
	    {
	      if (IS_INVALID_SOCKET (client_info[i].sock_fd))
		{
#if defined(WINDOWS)
		  u_long one = 1;
		  ioctlsocket (newsockfd, FIONBIO, (u_long FAR *) & one);
#else
		  fcntl (newsockfd, F_SETFL, FNDELAY);
#endif
		  client_info[i].sock_fd = newsockfd;
		  client_info[i].state = UNAUTHORIZED;
		  break;
		}
	    }
	  /* if there are too many clients */
	  if (i == MAX_CLIENT_NUM)
	    {
	      CLOSE_SOCKET (newsockfd);
	      continue;
	    }

	  FD_SET (newsockfd, &allset);
	  if (newsockfd > maxfd)
	    maxfd = newsockfd;
	  if (i > maxi)
	    maxi = i;
	  if (--nready <= 0)
	    continue;
	}
      /* handling new requests */
      for (i = 0; i <= maxi; i++)
	{
	  sockfd = client_info[i].sock_fd;
	  if (IS_INVALID_SOCKET (sockfd))
	    continue;

	  if (FD_ISSET (sockfd, &rset))
	    {
	      nv_add_nvp (cli_request, "_SVRTYPE", "psvr");
	      clilen = sizeof (cli_addr);
	      getpeername (sockfd, (struct sockaddr *) &cli_addr, &clilen);
	      pstrbuf = inet_ntoa (cli_addr.sin_addr);
	      nv_add_nvp (cli_request, "_CLIENTIP", pstrbuf);
	      nv_add_nvp_int (cli_request, "_CLIENTPORT", cli_addr.sin_port);

	      if (client_info[i].state == UNAUTHORIZED)
		{
		  ut_receive_request (sockfd, cli_request);
		  if (ts_validate_user (cli_request, cli_response))
		    {
		      if (ts_check_client_version (cli_request, cli_response))
			{
			  int aleady_connected_index;
			  char *value;
			  /* generate token and record new connection to file */
			  value = nv_get_val (cli_request, "id");
			  if (value != NULL)
			    {
			      client_info[i].user_id = strdup (value);
			    }
			  else
			    {
			      client_info[i].user_id = NULL;
			    }
			  client_info[i].ip_address = strdup (pstrbuf);

			  if ((sco.iAllow_AdminMultiCon == 0) &&
			      /*(strcmp( client_info[i].user_id, "admin") == 0) && */
			      (aleady_connected_index =
			       ts_check_already_connected (cli_response, maxi,
							   i,
							   client_info)) > -1)
			    {
			      /* must reject this connection */
			      char message[100];
			      snprintf (message, sizeof (message) - 1,
					"User was already connected from another client(%s)",
					client_info[aleady_connected_index].
					ip_address);
			      ut_send_response (sockfd, cli_response);
			      nv_add_nvp (cli_request, "_ID",
					  client_info[i].user_id);
			      ut_error_log (cli_request, message);
			      ut_access_log (cli_request, "rejected");
			      CLOSE_SOCKET (sockfd);
			      FD_CLR (sockfd, &allset);
			      client_info_reset (&(client_info[i]));
			    }
			  else
			    {	/* accept connection */
			      /* generate token and record new connection to file */
			      pstrbuf =
				ut_token_generate (nv_get_val
						   (cli_request, "_CLIENTIP"),
						   nv_get_val (cli_request,
							       "_CLIENTPORT"),
						   nv_get_val (cli_request,
							       "id"),
						   getpid ());
			      nv_add_nvp (cli_response, "token", pstrbuf);
			      nv_add_nvp (cli_request, "_ID",
					  client_info[i].user_id);
			      free (pstrbuf);

			      dbmt_con_add (nv_get_val
					    (cli_request, "_CLIENTIP"),
					    nv_get_val (cli_request,
							"_CLIENTPORT"),
					    nv_get_val (cli_request,
							"clientver"),
					    nv_get_val (cli_request, "id"));
			      ut_send_response (sockfd, cli_response);
			      client_info[i].state = VALID_USER;
			      ut_access_log (cli_request, "connected");
			    }
			}
		      else
			{
			  ut_send_response (sockfd, cli_response);
			  CLOSE_SOCKET (sockfd);
			  FD_CLR (sockfd, &allset);
			  client_info_reset (&(client_info[i]));
			  ut_access_log (cli_request, "rejected");
			  ut_error_log (cli_request, "version-mismatched");
			}
		    }
		  else
		    {
		      /* Authentication failed. Drop connection */
		      ut_send_response (sockfd, cli_response);
		      CLOSE_SOCKET (sockfd);
		      FD_CLR (sockfd, &allset);
		      client_info_reset (&(client_info[i]));
		      ut_access_log (cli_request, "rejected");
		    }
		}
	      else if (client_info[i].state == VALID_USER)
		{
		  nv_add_nvp (cli_request, "_ID", client_info[i].user_id);
		  if (ut_receive_request (sockfd, cli_request))
		    {

		      nvplist *res;
		      res = nv_create (5, NULL, "\n", ":", "\n");
		      if (res != NULL)
			{
			  prepare_response ((userdata *) ud, res);
			  ((userdata *) ud)->last_request_time = time (NULL);
			  ut_send_response (sockfd, res);
			  nv_destroy (res);
			}

		    }
		  else
		    {
		      /* connection closed */
		      CLOSE_SOCKET (sockfd);
		      ut_access_log (cli_request, "disconnected");
		      FD_CLR (sockfd, &allset);
		      client_info_reset (&(client_info[i]));
		      dbmt_con_delete (nv_get_val (cli_request, "_CLIENTIP"),
				       nv_get_val (cli_request,
						   "_CLIENTPORT"));
		    }
		}

	      nv_reset_nvp (cli_request);
	      nv_reset_nvp (cli_response);

	      if (--nready <= 0)
		break;
	    }
	}			/* end for */
    }

  free ((void *) ud);
return_statement:
  nv_destroy (cli_request);
  nv_destroy (cli_response);

#if defined(WINDOWS)
  return;
#else
  return NULL;
#endif
}

static THREAD_FUNC
automation_start (void *ud)
{
  struct stat statbuf;
  ajob ajob_list[AUTOJOB_SIZE];
  int i;
  time_t prev_check_time, cur_time;

  /* set up automation list */
  aj_initialize (ajob_list, ud);
  for (i = 0; i < AUTOJOB_SIZE; ++i)
    {
      if (ajob_list[i].ajob_loader)
	ajob_list[i].ajob_loader (&(ajob_list[i]));
    }

  prev_check_time = time (NULL);
  for (;;)
    {
      unlink (cubrid_err_file);
      putenv (cubrid_err_log_env);
      SLEEP_MILISEC (sco.iMonitorInterval, 0);
      cur_time = time (NULL);

      for (i = 0; i < AUTOJOB_SIZE; ++i)
	{
	  /* check automation.conf and see if it has changed since last access */
	  stat (ajob_list[i].config_file, &statbuf);
	  if (ajob_list[i].last_modi != statbuf.st_mtime)
	    {
	      ajob_list[i].last_modi = statbuf.st_mtime;
	      if (ajob_list[i].ajob_loader)
		ajob_list[i].ajob_loader (&(ajob_list[i]));
	    }

	  /* if unchanged, go ahead and check value */
	  if (ajob_list[i].is_on && ajob_list[i].ajob_handler)
	    {
	      ajob_list[i].ajob_handler (ajob_list[i].hd, prev_check_time,
					 cur_time);
	    }
	}
      prev_check_time = cur_time;
    }

  free ((void *) ud);
#if defined(WINDOWS)
  return;
#else
  return NULL;
#endif
}

static void
print_usage (char *progname)
{
  fprintf (start_log_fp, "Usage: %s [command]\n", progname);
  fprintf (start_log_fp, "commands are :\n");
  fprintf (start_log_fp, "   start   -  start the server\n");
  fprintf (start_log_fp, "   stop    -  stop the server\n");
}

/* perform any initialization or setting before running the pserver */
static void
start_pserver (void)
{
  int i;
  userdata ud;
  char strbuf[1024];
  T_THREAD pid1, pid3;

  sprintf (cubrid_err_file, "%s/cub_autoclient.err", sco.dbmt_tmp_dir);
  sprintf (cubrid_err_log_env, "CUBRID_ERROR_LOG=%s", cubrid_err_file);
  putenv (cubrid_err_log_env);

#if 0
  fprintf (start_log_fp, "Polling Server port : %d\n", sco.iPsvr_port);
  fprintf (start_log_fp, "Polling Server Access log : %s\n",
	   conf_get_dbmt_file (FID_PSERVER_ACCESS_LOG, strbuf));
  fprintf (start_log_fp, "Polling Server Error  log : %s\n",
	   conf_get_dbmt_file (FID_PSERVER_ERROR_LOG, strbuf));
  fprintf (start_log_fp, "Polling Interval : %d\n", sco.iMonitorInterval);
#endif

  /* remove any temporary files from previous run */
  unlink (conf_get_dbmt_file (FID_CONN_LIST, strbuf));
  unlink (conf_get_dbmt_file (FID_PSVR_DBINFO_TEMP, strbuf));

#if 0
  fprintf (start_log_fp, "Starting polling server as a daemon.\n");
#endif
  ut_daemon_start ();

  if (ut_write_pid (conf_get_dbmt_file (FID_PSERVER_PID, strbuf)) < 0)
    exit (1);

  auto_start_UniCAS ();

  memset (&ud, 0, sizeof (ud));
  /* initialize memory for active databases information */
  for (i = 0; i < MAX_INSTALLED_DB; ++i)
    ud.dbvect[i] = 0;

#if defined(WINDOWS)
  SetConsoleCtrlHandler ((PHANDLER_ROUTINE) CtrlHandler, TRUE);
#else
  signal (SIGINT, term_handler);
  signal (SIGTERM, term_handler);
#endif


  if (net_init () < 0)
    exit (1);

  uRemoveLockFile (pid_lock_fd);

  fflush (start_log_fp);

  server_fd_clear (pserver_sockfd);

  THREAD_BEGIN (pid1, service_start, &ud);
  THREAD_BEGIN (pid3, automation_start, &ud);

  while (1)
    SLEEP_MILISEC (1, 0);
}






/*
 *   client ip : client port : dbmt id : pserver pid
 *      15            5           8           5      = 33 + 4 = 37
 *   40 - 37 = 8.
 *   Thus, 3 bytes for checksum
 */
static char *
ut_token_generate (char *client_ip, char *client_port, char *dbmt_id,
		   int proc_id)
{
  char sbuf[TOKEN_LENGTH + 1];
  char token_string[TOKEN_ENC_LENGTH];
  int i, len;

  if ((client_ip == NULL) || (client_port == NULL) || (dbmt_id == NULL))
    return NULL;

  sprintf (sbuf, "%s:%s:%s:%d:", client_ip, client_port, dbmt_id, proc_id);
  len = strlen (sbuf);
  /* insert padding to checksum part */
  for (i = len; i < TOKEN_LENGTH; ++i)
    sbuf[i] = '*';
  sbuf[i] = '\0';

  uEncrypt (TOKEN_LENGTH, sbuf, token_string);

  return strdup (token_string);
}

static int
net_init (void)
{
  int optval = 1;
  struct sockaddr_in serv_addr;

#if defined(WINDOWS)
  if (wsa_initialize () < 0)
    {
      return -1;
    }
#endif

  /* set up network */
  pserver_sockfd = socket (AF_INET, SOCK_STREAM, 0);
  if (IS_INVALID_SOCKET (pserver_sockfd))
    {
      perror ("socket");
      return -1;
    }

  memset ((char *) &serv_addr, 0, sizeof (serv_addr));
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_addr.s_addr = htonl (INADDR_ANY);
  serv_addr.sin_port = htons ((unsigned short) sco.iPsvr_port);

  if (setsockopt
      (pserver_sockfd, SOL_SOCKET, SO_REUSEADDR, (const char *) &optval,
       sizeof (optval)) < 0)
    {
      perror ("setsockopt");
      return -1;
    }
  if (bind
      (pserver_sockfd, (struct sockaddr *) &serv_addr,
       sizeof (serv_addr)) < 0)
    {
      perror ("bind");
      return -1;
    }

  if (listen (pserver_sockfd, 5) < 0)
    {
      perror ("listen");
      return -1;
    }
  return 0;
}

static void
prepare_response (userdata * ud, nvplist * res)
{
}

static void
auto_start_UniCAS (void)
{
  T_CM_ERROR uc_start_error_msg;
  if (sco.iAutoStart_UniCAS)
    {
      /* start UniCAS */
      if (cm_broker_env_start (&uc_start_error_msg) < 0)
	{
#if 0
	  fprintf (start_log_fp, "Starting CUBRID CAS - %s\n",
		   uc_start_error_msg);
#endif
	}
      else
	{
#if 0
	  fprintf (start_log_fp, "Starting CUBRID CAS ... OK\n");
#endif
	}
    }
}

static void
client_info_reset (T_CLIENT_INFO * client_info)
{
  client_info->sock_fd = INVALID_SOCKET;
  client_info->state = NO_USER;
  FREE_MEM (client_info->user_id);
  FREE_MEM (client_info->ip_address);
}
