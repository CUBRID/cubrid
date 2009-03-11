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

#define _REENTRANT		/* for thread library */

#include <stdio.h>
#include <stdlib.h>		/* atoi()       */
#include <signal.h>		/* SIG_TTOU ... */
#include <sys/types.h>		/* AF_INET ... */
#include <string.h>		/* memset()    */
#include <sys/stat.h>		/* umask(), stat() */
#include <ctype.h>		/* isalnum() */
#include <fcntl.h>
#include <errno.h>

#ifdef WIN32
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
#include "cm_nameval.h"
#include "cm_autojob.h"
#include "cm_auto_task.h"
#include "cm_config.h"
#include "cm_command_execute.h"
#include "cm_text_encryption.h"
#include "cm_broker_admin.h"
#ifdef WIN32
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
static int uRecordConnection (char *new_ip, char *new_port,
			      char *client_version);
static int uRemoveConnection (char *new_ip, char *new_port, char *client_ver);
#ifdef HOST_MONITOR_PROC
static THREAD_FUNC collect_start (void *ud);
static void setup_dbstate (userdata * ud);
static void setup_casstate (userdata * ud);
static int _isInDBList (userdata * ud, char *dbname);
static int _GetFreeIndex (userdata * ud);
#endif
static char *ut_token_generate (char *client_ip, char *client_port,
				char *dbmt_id, int proc_id);
static int net_init ();
static void prepare_response (userdata * ud, nvplist * res);
static void auto_start_UniCAS ();
static void client_info_reset (T_CLIENT_INFO * client);

#if 0				/* ACTIVITY_PROFILE */
static void uGenerateStatus (nvplist * req, nvplist * res, int retval,
			     char *diagerror);

static void diag_config_reset (T_CLIENT_INFO * client_info);

#define ADD_CAS_MONITOR_CONFIG(TARGET, SOURCE)  \
    do { \
        TARGET.head |= SOURCE.head; \
        TARGET.body[0] |= SOURCE.body[0]; \
        TARGET.body[1] |= SOURCE.body[1]; \
    } while(0)


#define ADD_SERVER_MONITOR_CONFIG(TARGET, SOURCE) \
    do { \
        TARGET.head[0] |= SOURCE.head[0]; \
        TARGET.head[1] |= SOURCE.head[1]; \
        TARGET.body[0] |= SOURCE.body[0]; \
        TARGET.body[1] |= SOURCE.body[1]; \
        TARGET.body[2] |= SOURCE.body[2]; \
        TARGET.body[3] |= SOURCE.body[3]; \
        TARGET.body[4] |= SOURCE.body[4]; \
        TARGET.body[5] |= SOURCE.body[5]; \
        TARGET.body[6] |= SOURCE.body[6]; \
        TARGET.body[7] |= SOURCE.body[7]; \
    } while(0)

#define COPY_CAS_MONITOR_CONFIG(TARGET, SOURCE)  \
    do { \
        TARGET.head = SOURCE.head; \
        TARGET.body[0] = SOURCE.body[0]; \
        TARGET.body[1] = SOURCE.body[1]; \
    } while(0)


#define COPY_SERVER_MONITOR_CONFIG(TARGET, SOURCE) \
    do { \
        TARGET.head[0] = SOURCE.head[0]; \
        TARGET.head[1] = SOURCE.head[1]; \
        TARGET.body[0] = SOURCE.body[0]; \
        TARGET.body[1] = SOURCE.body[1]; \
        TARGET.body[2] = SOURCE.body[2]; \
        TARGET.body[3] = SOURCE.body[3]; \
        TARGET.body[4] = SOURCE.body[4]; \
        TARGET.body[5] = SOURCE.body[5]; \
        TARGET.body[6] = SOURCE.body[6]; \
        TARGET.body[7] = SOURCE.body[7]; \
    } while(0)
#endif

static int pserver_sockfd;
static FILE *start_log_fp;
static int pid_lock_fd;
static char cubrid_err_log_env[256];
static char cubrid_err_file[256];

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

int
main (int argc, char **argv)
{
  FILE *pidfile;
  int pidnum;
  char *ars_cmd;
  char pid_file_name[512];
  char err_msg[1024];

#ifdef WIN32
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
  mkdir (sco.dbmt_tmp_dir, 0777);

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

#if 0
  fprintf (start_log_fp, "Checking uc_admin.so ... ");
#endif
  if (uca_init (err_msg) < 0)
    {
      fprintf (start_log_fp, "Checking uc_admin.so ... Error\n%s\n", err_msg);
    }
#if 0
  else
    fprintf (start_log_fp, "%s OK\n", uca_version ());
#endif

  conf_get_dbmt_file (FID_PSERVER_PID, pid_file_name);

  strcpy (g_pidfile_path, pid_file_name);
  if (strcmp (ars_cmd, "start") == 0)
    {
      if (access (pid_file_name, F_OK) < 0)
	start_pserver ();
      else
	{
	  pidfile = fopen (pid_file_name, "rt");
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
	    fprintf (start_log_fp,
		     "Error : Server[pid=%d] already running.\n", pidnum);
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
	  fscanf (pidfile, "%d", &pidnum);
	  fclose (pidfile);
	  /* fprintf (start_log_fp, "Stopping polling server process with pid %d\n", pidnum); */
	  if ((kill (pidnum, SIGTERM)) < 0)
	    fprintf (start_log_fp, "Error : Failed to stop the server.\n");
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
	  fscanf (pidfile, "%d", &pidnum);
	  fclose (pidfile);

	  if (((kill (pidnum, 0) < 0) && (errno == ESRCH)) ||
	      (is_cmserver_process (pidnum, PSERVER_MODULE_NAME) == 0))
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
#if 0				/* ACTIVITY_PROFILE */
  uca_init (NULL);
#endif

  cli_request = nv_create (5, NULL, "\n", ":", "\n");
  cli_response = nv_create (5, NULL, "\n", ":", "\n");

  for (;;)
    {
      rset = allset;
      nready = select (maxfd + 1, &rset, NULL, NULL, NULL);
      if (FD_ISSET (pserver_sockfd, &rset))
	{
	  clilen = sizeof (cli_addr);
	  newsockfd =
	    accept (pserver_sockfd, (struct sockaddr *) &cli_addr, &clilen);
	  setsockopt (newsockfd, IPPROTO_TCP, TCP_NODELAY, (char *) &one,
		      sizeof (one));

	  for (i = 0; i < MAX_CLIENT_NUM; ++i)
	    {
	      if (client_info[i].sock_fd < 0)
		{
#ifdef WIN32
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
	  if ((sockfd = client_info[i].sock_fd) < 0)
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
			  /* generate token and record new connection to file */
			  client_info[i].user_id =
			    strdup (nv_get_val (cli_request, "id"));
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
			      sprintf (message,
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

			      uRecordConnection (nv_get_val
						 (cli_request, "_CLIENTIP"),
						 nv_get_val (cli_request,
							     "_CLIENTPORT"),
						 nv_get_val (cli_request,
							     "clientver"));
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
#if 0				/* ACTIVITY PROFILE */
		      char *task;
		      int j, index;
		      nvplist *res;
		      char err_msg[1024];
		      res = nv_create (5, NULL, "\n", ":", "\n");
		      task = nv_get_val (cli_request, "task");
		      if (task && !strcmp (task, "setclientdiaginfo"))
			{
			  int ret_val;
			  char *db_name;
			  char time_buf[16];
			  struct timeval current_time;
			  T_CLIENT_MONITOR_CONFIG c_config;

			  nv_add_nvp (res, "task", task);
			  nv_add_nvp (res, "status", "none");
			  nv_add_nvp (res, "note", "none");

			  ret_val = ERR_NO_ERROR;
			  db_name = nv_get_val (cli_request, "db_name");
			  init_monitor_config (&c_config);
			  ret_val =
			    get_client_monitoring_config (cli_request,
							  &c_config);

			  if (ret_val == ERR_NO_ERROR)
			    {
			      MONITOR_CAS_CONFIG shm_cas_config;
			      void *shm_cas, *shm_server;

			      client_info[i].diag_ref_count++;
			      ADD_CAS_MONITOR_CONFIG (client_info[i].
						      diag_cas_config,
						      c_config.cas);

			      /* flag set to cas shared memory */
			      shm_cas = uca_broker_shm_open (err_msg);
			      if (shm_cas)
				{
				  if (uca_get_br_diagconfig_with_opened_shm
				      (shm_cas, (void *) &shm_cas_config,
				       err_msg) != -1)
				    {
				      ADD_CAS_MONITOR_CONFIG (shm_cas_config,
							      c_config.cas);
				      uca_set_br_diagconfig_with_opened_shm
					(shm_cas, (void *) &shm_cas_config,
					 err_msg);

				      uca_shm_detach (shm_cas);
				    }
				}

			      if (db_name)
				{
				  for (j = 0;
				       j < client_info[i].mon_server_num; j++)
				    {
				      char *server_name
					=
					client_info[i].diag_server_config[j].
					server_name;
				      if (!strcmp (server_name, db_name))
					{
					  ADD_SERVER_MONITOR_CONFIG
					    (client_info[i].
					     diag_server_config[j].
					     server_config, c_config.server);
					  break;
					}
				    }

				  if (j == client_info[i].mon_server_num)
				    {
				      client_info[i].mon_server_num++;
				      COPY_SERVER_MONITOR_CONFIG (client_info
								  [i].
								  diag_server_config
								  [j].
								  server_config,
								  c_config.
								  server);
				      strcpy (client_info[i].
					      diag_server_config[j].
					      server_name, db_name);
				    }

				  /* flag set to server shared memory */
				  ret_val = getservershmid (db_name, err_msg);
				  if (ret_val != -1)
				    {
				      shm_server =
					diag_shm_open (ret_val,
						       SHM_DIAG_SERVER,
						       DIAG_SHM_MODE_ADMIN);
				      if (shm_server)
					{
					  ADD_SERVER_MONITOR_CONFIG (((T_SHM_DIAG_INFO_SERVER *) shm_server)->server_diag_config, c_config.server);

					  diag_shm_detach (shm_server);
					}
				    }
				}
			      ret_val = ERR_NO_ERROR;
			    }
			  gettimeofday (&current_time, NULL);

			  sprintf (time_buf, "%ld", current_time.tv_sec);
			  nv_add_nvp (res, "start_time_sec", time_buf);
			  sprintf (time_buf, "%ld", current_time.tv_usec);
			  nv_add_nvp (res, "start)time_usec", time_buf);

			  uGenerateStatus (cli_request, res, ret_val,
					   err_msg);
			  ut_send_response (sockfd, res);
			  nv_destroy (res);
			}
		      else if (task && !strcmp (task, "removeclientdiaginfo"))
			{
			  int db_index, db_num, ret_val, skip_cas;
			  int db_index1, userindex, db_index2;
			  void *shm_cas, *shm_server;
			  MONITOR_CAS_CONFIG new_cas_config;
			  MONITOR_SERVER_CONFIG new_server_config;

			  nv_add_nvp (res, "task", task);
			  nv_add_nvp (res, "status", "none");
			  nv_add_nvp (res, "note", "none");

			  ret_val = ERR_NO_ERROR;

			  client_info[i].diag_ref_count--;

			  if (client_info[i].diag_ref_count != 0)
			    goto removeclientdiaginfo_final;

			  /* 1. delete content in client_info[i].diag_cas_config */
			  init_cas_monitor_config (&
						   (client_info[i].
						    diag_cas_config));

			  /* 2. delete content in client_info[i].diag_server_config */
			  db_num = client_info[i].mon_server_num;
			  for (db_index = 0; db_index < db_num; db_index++)
			    {
			      init_server_monitor_config (&
							  (client_info[i].
							   diag_server_config
							   [db_index].
							   server_config));
			    }
			  client_info[i].mon_server_num = 0;

			  /* 3. modify shered memory's value by every cilent_info's 
			   * diag_cas_config and diag_server_config's value. */
			  skip_cas = 0;
			  for (db_index1 = 0; db_index1 < db_num; db_index1++)
			    {
			      init_server_monitor_config (&new_server_config);

			      if (!skip_cas)
				init_cas_monitor_config (&new_cas_config);

			      for (userindex = 0; userindex < maxi;
				   userindex++)
				{
				  if (client_info[userindex].state ==
				      VALID_USER)
				    {
				      if (!skip_cas)
					{
					  ADD_CAS_MONITOR_CONFIG
					    (new_cas_config,
					     client_info[userindex].
					     diag_cas_config);
					}

				      for (db_index2 = 0;
					   db_index2 <
					   client_info[userindex].
					   mon_server_num; db_index2++)
					{
					  if (!strcmp
					      (client_info[i].
					       diag_server_config[db_index1].
					       server_name,
					       client_info[userindex].
					       diag_server_config[db_index2].
					       server_name))
					    {
					      ADD_SERVER_MONITOR_CONFIG
						(new_server_config,
						 client_info[userindex].
						 diag_server_config
						 [db_index2].server_config);
					    }
					}
				    }
				}
			      if (!skip_cas)
				{
				  /* set value to shared memory */
				  shm_cas = uca_broker_shm_open (err_msg);
				  if (shm_cas)
				    {
				      ret_val =
					uca_set_br_diagconfig_with_opened_shm
					(shm_cas, &new_cas_config, err_msg);
				      uca_shm_detach (shm_cas);
				    }
				  skip_cas = 1;
				}

			      /* open shared memory by 
			       * client_info[i].diag_server_config[db_index1].servername
			       * and memory's value set to new_server_config */
			      ret_val =
				getservershmid (client_info[i].
						diag_server_config[db_index1].
						server_name, err_msg);
			      if (ret_val != -1)
				{
				  shm_server =
				    diag_shm_open (ret_val, SHM_DIAG_SERVER,
						   DIAG_SHM_MODE_ADMIN);
				  if (shm_server)
				    {
				      COPY_SERVER_MONITOR_CONFIG (((T_SHM_DIAG_INFO_SERVER *) shm_server)->server_diag_config, new_server_config);

				      diag_shm_detach (shm_server);
				    }
				}
			      memset (client_info[i].
				      diag_server_config[db_index1].
				      server_name, '\0',
				      MAX_SERVER_NAMELENGTH);
			      ret_val = ERR_NO_ERROR;
			    }
			removeclientdiaginfo_final:
			  uGenerateStatus (cli_request, res, ret_val,
					   err_msg);
			  ut_send_response (sockfd, res);
			  nv_destroy (res);
			}
		      else
			{
			  prepare_response ((userdata *) ud, res);
			  ((userdata *) ud)->last_request_time = time (NULL);
			  ut_send_response (sockfd, res);
			  nv_destroy (res);
			}
#else
		      nvplist *res;
		      res = nv_create (5, NULL, "\n", ":", "\n");
		      prepare_response ((userdata *) ud, res);
		      ((userdata *) ud)->last_request_time = time (NULL);
		      ut_send_response (sockfd, res);
		      nv_destroy (res);
#endif
		    }
		  else
		    {
		      /* connection closed */
		      CLOSE_SOCKET (sockfd);
		      ut_access_log (cli_request, "disconnected");
		      FD_CLR (sockfd, &allset);
		      client_info_reset (&(client_info[i]));
		      uRemoveConnection (nv_get_val
					 (cli_request, "_CLIENTIP"),
					 nv_get_val (cli_request,
						     "_CLIENTPORT"),
					 nv_get_val (cli_request,
						     "clientversion"));
		    }
		}

	      nv_reset_nvp (cli_request);
	      nv_reset_nvp (cli_response);

	      if (--nready <= 0)
		break;
	    }
	}			/* end for */
    }
  nv_destroy (cli_request);
  nv_destroy (cli_response);

#ifdef WIN32
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

#ifdef WIN32
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
#ifdef HOST_MONITOR_PROC
  T_THREAD pid2;
#endif

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

#ifdef WIN32
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
#ifdef HOST_MONITOR_PROC
  THREAD_BEGIN (pid2, collect_start, &ud);
#endif
  THREAD_BEGIN (pid3, automation_start, &ud);

  while (1)
    SLEEP_MILISEC (1, 0);
}

/*
 *  connection list format :
 *     ip, port, date, time
 */
static int
uRecordConnection (char *new_ip, char *new_port, char *client_version)
{
  FILE *outfile;
  char strbuf[512];
  int lock_fd, retval;

  lock_fd = uCreateLockFile (conf_get_dbmt_file (FID_LOCK_CONN_LIST, strbuf));
  if (lock_fd < 0)
    return -1;

  retval = -1;
  outfile = fopen (conf_get_dbmt_file (FID_CONN_LIST, strbuf), "a");
  if (outfile != NULL)
    {
      time_to_str (time (NULL), "%04d/%02d/%02d %02d:%02d:%02d", strbuf,
		   TIME_STR_FMT_DATE_TIME);
      fprintf (outfile, "%s %s %s %s\n", new_ip, new_port, strbuf,
	       client_version);
      fclose (outfile);
      retval = 0;
    }
  uRemoveLockFile (lock_fd);

  return retval;
}

static int
uRemoveConnection (char *new_ip, char *new_port, char *client_ver)
{
  char ip[20];
  char port[10];
  char version[16];
  char c_date[16];
  char c_time[16];
  char sbuf[1024];
  FILE *infile, *outfile;
  char tmpfile[512];
  char conn_list_file[512];
  int lock_fd, retval;

  lock_fd = uCreateLockFile (conf_get_dbmt_file (FID_LOCK_CONN_LIST, sbuf));
  if (lock_fd < 0)
    return -1;

  conf_get_dbmt_file (FID_CONN_LIST, conn_list_file);
  infile = fopen (conn_list_file, "r");
  sprintf (tmpfile, "%s/DBMT_util_014.%d", sco.dbmt_tmp_dir, (int) getpid ());
  outfile = fopen (tmpfile, "w");

  if (infile == NULL || outfile == NULL)
    {
      if (infile)
	fclose (infile);
      if (outfile)
	fclose (outfile);
      retval = -1;
    }
  else
    {
      while (fgets (sbuf, 1024, infile))
	{
	  ut_trim (sbuf);
	  sscanf (sbuf, "%s %s %s %s %s\n", ip, port, c_date, c_time,
		  version);
	  if ((uStringEqual (new_ip, ip)) && (uStringEqual (new_port, port)))
	    continue;
	  fprintf (outfile, "%s\n", sbuf);
	}
      fclose (outfile);
      fclose (infile);

      move_file (tmpfile, conn_list_file);
      retval = 0;
    }
  uRemoveLockFile (lock_fd);

  return retval;
}

#ifdef HOST_MONITOR_PROC
static THREAD_FUNC
collect_start (void *arg)
{
  userdata *ud = (userdata *) arg;
  char tmpfile[512];
  struct stat statbuf;
  time_t moditime = 0;

  /* if db state is changed, update. */
  record_system_info (&(ud->ssbuf));
  stat (conf_get_dbmt_file (FID_PSVR_DBINFO_TEMP, tmpfile), &statbuf);
  moditime = statbuf.st_mtime;
  ((userdata *) ud)->dbsrv_refresh_flag = 1;
  setup_dbstate (ud);
  record_cubrid_proc_info (ud);
  setup_casstate (ud);
  record_unicas_proc_info (ud->casvect, ud->casbuf);

  /* start data collecting loop */
  for (;;)
    {
      sleep (sco.iMonitorInterval);
      if (time (NULL) - ud->last_request_time > 5)
	continue;

      /* check if db state is changed */
      stat (conf_get_dbmt_file (FID_PSVR_DBINFO_TEMP, tmpfile), &statbuf);
      if (moditime != statbuf.st_mtime)
	{
	  ud->dbsrv_refresh_flag = 1;
	  moditime = statbuf.st_mtime;
	}
      setup_dbstate (ud);
      setup_casstate (ud);

      record_system_info (&(ud->ssbuf));
      record_cubrid_proc_info (ud);
      record_unicas_proc_info (ud->casvect, ud->casbuf);
    }
  return NULL;
}

/* fill in dbname, pid in db_stat struct */
static void
setup_dbstate (userdata * ud)
{
  int i, fri, dbcnt;
  char strbuf[1024];
  char *dbname;
  int db_srv_pid;

  if (!(ud->dbsrv_refresh_flag))
    return;

  for (i = 0; i < MAX_INSTALLED_DB; ++i)
    {
      ud->dbvect[i] = 0;
    }

  /* read active db list */
  memset (strbuf, 0, sizeof (strbuf));
  dbcnt = uReadDBnfo (strbuf);

  /* for each active db, make entry for new dbname */
  dbname = strbuf;
  for (i = 0; i < dbcnt; ++i)
    {
      if (!(_isInDBList (ud, dbname)))
	{
	  db_srv_pid = get_db_server_pid (dbname);
	  if (db_srv_pid > 0)
	    {
	      fri = _GetFreeIndex (ud);
	      if (fri >= 0)
		{
		  strcpy (ud->dbbuf[fri].db_name, dbname);
		  ud->dbbuf[fri].db_pid = db_srv_pid;
		  ud->dbvect[fri] = 1;
		}
	    }
	}
      dbname = dbname + strlen (dbname) + 1;
    }
  ud->dbsrv_refresh_flag = 0;
}

/* fill in cas_pid of cas_stat */
/* cas_pid is directly used to get process info in record_unicas_proc_info() */
static void
setup_casstate (userdata * ud)
{
  DIR *dp;
  FILE *infile;
  struct dirent *dirp;
  int as_pid, i, idx = 0;
  char caspiddir[512], filepath[512], strbuf[1024];
  char *p;
  T_DM_UC_INFO uc_info;

  for (i = 0; i < MAX_UNICAS_PROC; ++i)
    ud->casvect[i] = 0;

  sprintf (caspiddir, "%s/var/as_pid", sco.szUnicas_home);
  dp = opendir (caspiddir);

  while ((dirp = readdir (dp)) != NULL)
    {
      if (dirp->d_name[0] == '.')
	continue;

      sprintf (filepath, "%s/var/as_pid/%s", sco.szUnicas_home, dirp->d_name);
      if ((infile = fopen (filepath, "r")) != NULL)
	{
	  if (fscanf (infile, "%d", &as_pid) == 1)
	    {
	      ud->casvect[idx] = 1;
	      ud->casbuf[idx].cas_pid = as_pid;
	      strcpy (ud->casbuf[idx].cas_name, dirp->d_name);
	      p = strchr (ud->casbuf[idx].cas_name, '.');
	      if (p)
		*p = '\0';
	      idx++;
	      if (idx >= MAX_UNICAS_PROC)
		{
		  fclose (infile);
		  closedir (dp);
		  return;
		}
	    }
	  fclose (infile);
	}
    }
  closedir (dp);

  if (uca_br_info (&uc_info, strbuf) < 0)
    return;

  for (i = 0; i < uc_info.num_info; i++)
    {
      if (strcmp (uc_info.info.br_info[i].status, "OFF") == 0)
	continue;
      ud->casvect[idx] = 1;
      ud->casbuf[idx].cas_pid = uc_info.info.br_info[i].pid;
      if (strcmp (uc_info.info.br_info[i].as_type, "CAS") == 0)
	strcpy (ud->casbuf[idx].cas_name, "Cbroker");
      else
	strcpy (ud->casbuf[idx].cas_name, "Tbroker");
      idx++;
      if (idx >= MAX_UNICAS_PROC)
	{
	  break;
	}
    }
  uca_br_info_free (&uc_info);
}

static int
_isInDBList (userdata * ud, char *dbname)
{
  int i;

  for (i = 0; i < MAX_INSTALLED_DB; ++i)
    {
      if ((ud->dbvect[i] == 1)
	  && (uStringEqual (ud->dbbuf[i].db_name, dbname)))
	return 1;
    }
  return 0;
}

static int
_GetFreeIndex (userdata * ud)
{
  int i;
  for (i = 0; i < MAX_INSTALLED_DB; ++i)
    {
      if (ud->dbvect[i] != 1)
	return i;
    }
  return -1;
}
#endif /* HOST_MONITOR_PROC */

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
net_init ()
{
  int optval = 1;
  struct sockaddr_in serv_addr;

#ifdef WIN32
  if (wsa_initialize () < 0)
    {
      return -1;
    }
#endif

  /* set up network */
  if ((pserver_sockfd = socket (AF_INET, SOCK_STREAM, 0)) < 0)
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
#ifdef HOST_MONITOR_PROC
  int i;

  nv_add_nvp_int (res, "LOAD_AVG_1M", ud->ssbuf.load_avg[0]);
  nv_add_nvp_int (res, "LOAD_AVG_5M", ud->ssbuf.load_avg[1]);
  nv_add_nvp_int (res, "LOAD_AVG_15M", ud->ssbuf.load_avg[2]);
  nv_add_nvp_int (res, "CPU_TIME_IDLE", ud->ssbuf.cpu_states[0]);
  nv_add_nvp_int (res, "CPU_TIME_USER", ud->ssbuf.cpu_states[1]);
  nv_add_nvp_int (res, "CPU_TIME_KERNEL", ud->ssbuf.cpu_states[2]);
  nv_add_nvp_int (res, "CPU_TIME_IOWAIT", ud->ssbuf.cpu_states[3]);
  nv_add_nvp_int (res, "CPU_TIME_SWAP", ud->ssbuf.cpu_states[4]);
  nv_add_nvp_int (res, "MEMORY_REAL", ud->ssbuf.memory_stats[0]);
  nv_add_nvp_int (res, "MEMORY_ACTIVE", ud->ssbuf.memory_stats[1]);
  nv_add_nvp_int (res, "MEMORY_FREE", ud->ssbuf.memory_stats[2]);
  nv_add_nvp_int (res, "SWAP_IN_USE", ud->ssbuf.memory_stats[3]);
  nv_add_nvp_int (res, "SWAP_FREE", ud->ssbuf.memory_stats[4]);

  nv_add_nvp (res, "start", "dblist");
  for (i = 0; i < MAX_INSTALLED_DB; ++i)
    {
      if (ud->dbvect[i])
	{
	  nv_add_nvp (res, "DBNAME", ud->dbbuf[i].db_name);
	  nv_add_nvp_int (res, "DBPID", ud->dbbuf[i].db_pid);
	  nv_add_nvp_int (res, "DBSIZE", ud->dbbuf[i].db_size);
	  nv_add_nvp (res, "DBSTATE", ud->dbbuf[i].proc_stat);
	  nv_add_nvp_time (res, "DBSTARTTIME", ud->dbbuf[i].db_start_time,
			   "%04d/%02d/%02d-%02d:%02d:%02d", NV_ADD_DATE_TIME);
	  nv_add_nvp_float (res, "DBCPUUSAGE", ud->dbbuf[i].db_cpu_usage,
			    "%f");
	  nv_add_nvp_float (res, "DBMEMUSAGE", ud->dbbuf[i].db_mem_usage,
			    "%f");
	}
    }
  nv_add_nvp (res, "end", "dblist");
  nv_add_nvp (res, "start", "brokerlist");
  for (i = 0; i < MAX_UNICAS_PROC; ++i)
    {
      if (ud->casvect[i])
	{
	  nv_add_nvp (res, "UNICAS_NAME", ud->casbuf[i].cas_name);
	  nv_add_nvp_int (res, "UNICAS_PID", ud->casbuf[i].cas_pid);
	  nv_add_nvp_int (res, "UNICAS_SIZE", ud->casbuf[i].cas_size);
	  nv_add_nvp (res, "UNICAS_STATE", ud->casbuf[i].proc_stat);
	  nv_add_nvp_time (res, "UNICAS_STARTTIME",
			   ud->casbuf[i].cas_start_time,
			   "%04d/%02d/%02d-%02d:%02d:%02d", NV_ADD_DATE_TIME);
	  nv_add_nvp_float (res, "UNICAS_CPUUSAGE",
			    ud->casbuf[i].cas_cpu_usage, "%f");
	  nv_add_nvp_float (res, "UNICAS_MEMUSAGE",
			    ud->casbuf[i].cas_mem_usage, "%f");
	}
    }
  nv_add_nvp (res, "end", "brokerlist");

  nv_add_nvp (res, "start", "iostat");
#ifdef HOST_MONITOR_IO
  record_iostat (res);
#endif
  nv_add_nvp (res, "end", "iostat");
#endif
}

static void
auto_start_UniCAS ()
{
  char uc_start_error_msg[1024];
  if (sco.iAutoStart_UniCAS)
    {
      /* start UniCAS */
      if (uca_start (uc_start_error_msg) < 0)
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
  client_info->sock_fd = -1;
  client_info->state = NO_USER;
  FREE_MEM (client_info->user_id);
  FREE_MEM (client_info->ip_address);

#if 0				/* ACTIVITY_PROFILE */
  diag_config_reset (client_info);
#endif
}

#if 0				/* ACTIVITY_PROFILE */
static void
diag_config_reset (T_CLIENT_INFO * client_info)
{
  int i;

  if (!client_info)
    return;

  init_cas_monitor_config (&(client_info->diag_cas_config));
  for (i = 0; i < client_info->mon_server_num; i++)
    {
      memset (client_info->diag_server_config[i].server_name, '\0',
	      MAX_SERVER_NAMELENGTH);
      init_server_monitor_config (&
				  (client_info->diag_server_config[i].
				   server_config));
    }
  client_info->mon_server_num = 0;
}

static void
uGenerateStatus (nvplist * req, nvplist * res, int retval, char *diagerror)
{
  char strbuf[1024];

  if ((retval == -1) || (retval == 0))
    return;

  if ((retval == 1) || (retval == ERR_NO_ERROR))
    {
      nv_update_val (res, "status", "success");
      return;
    }

  nv_update_val (res, "status", "failure");
  switch (retval)
    {
    case ERR_GENERAL_ERROR:
      sprintf (strbuf, "Unknown general error");
      break;
    case ERR_UNDEFINED_TASK:
      sprintf (strbuf, "Undefined request - %s", diagerror);
      break;
    case ERR_PARAM_MISSING:
      sprintf (strbuf, "Parameter(%s) missing in the request", diagerror);
      break;
    case ERR_FILE_OPEN_FAIL:
      sprintf (strbuf, "File(%s) open error", diagerror);
      break;
    case ERR_WITH_MSG:
      sprintf (strbuf, "%s", diagerror);
      break;
    case ERR_SYSTEM_CALL:
      sprintf (strbuf, "Error while running(%s)", diagerror);
      break;
    default:
      sprintf (strbuf, "error");
      break;
    }

  nv_update_val (res, "note", strbuf);
}
#endif
