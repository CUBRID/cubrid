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
 * repl_server.c - The main routine of Transaction Log Reader/Sender
 *                 process for replication
 *
 * Note:
 *   repl_server reads the transaction log file of the master server
 *   and sends the logs to the repl_agent, which runs at the distributor site.
 *
 *   repl_server consists of MAIN,  SEND and READ threads.
 */

#ident "$Id$"

#include <stdio.h>
#include <stdlib.h>
#ifdef HAVE_GETOPT_H
#include <getopt.h>
#else
#include "getopt.h"
#endif

#include "porting.h"
#include "repl_support.h"
#include "system_parameter.h"
#include "utility.h"
#include "databases_file.h"
#include "message_catalog.h"
#include "repl_tp.h"
#include "repl_server.h"
#include "environment_variable.h"

/* Global variables for argument parsing */
const char *db_Name = "";	/* the target database name */
const char *err_Log = "";	/* file path to write the log */
int port_Num = 0;		/* the TCP port number
				 * to listen the req. of
				 * repl_agent
				 */
int max_Agent_num = 0;

FILE *err_Log_fp = NULL;	/* error log file pointer */
FILE *agent_List_fp = NULL;	/* agent list file */
REPL_AGENT_INFO *agent_List;	/* agent list info */


REPL_CONN *repl_Conn_h = NULL;
int active_Conn_num;

/* info. for accessing the active log file */
REPL_ACT_LOG repl_Log = { 0, NULL, NULL, REPL_DEF_LOG_PAGE_SIZE };

/* info. for accessing the archive log file */
REPL_ARV_LOG repl_Arv = { 0, NULL, NULL, 0 };
REPL_ERR *err_Head = NULL;

int debug_Dump_info = 0;


#define REPLSERVER_ARG_DBNAME           1
#define REPLSERVER_ARG_PORTNUM          2
#define REPLSERVER_ARG_ERRLOG           3
#define REPLSERVER_ARG_AGENTNUM         4
#define REPLSERVER_ARG_FSYNC_SEC        5


/*
 * This process should have four arguments when it runs.
 *    database-name : the target database name to be replicated
 *    TCP PortNum   : the designated TCP Port number to catch the
 *                    reqests of the repl_agent
 *    err log file  : the log file to write down the message from this server.
 *    # of agents   : the maximum number of agents to be served
 */

static int repl_load_agent_list (char *file_name);
static REPL_AGENT_INFO *repl_get_agent_info (int agent_id);
static int repl_write_agent_list (void);

/*
 * repl_svr_shutdown() - Shutdown process of repl_server
 *   return: none
 *   started : true if main job process
 *             false if in the initialization step
 *
 * Note:
 *         : all resources are released
 *
 *     called by MAIN thread or SIGNAL HANDLER thread
 */
static void
repl_svr_shutdown (bool started)
{
  if (started == true)
    {
      repl_svr_sock_shutdown ();
      repl_svr_clear_all_conn ();
    }

  if (repl_Log.hdr_page)
    {
      free_and_init (repl_Log.hdr_page);
      repl_Log.log_hdr = NULL;
    }
  if (repl_Log.log_vdes > 0)
    {
      close (repl_Log.log_vdes);
    }

  if (repl_Arv.hdr_page)
    {
      free_and_init (repl_Arv.hdr_page);
      repl_Arv.hdr_page = NULL;
    }
  if (repl_Arv.log_vdes > 0)
    {
      close (repl_Arv.log_vdes);
    }

  repl_error_flush (err_Log_fp, 1);
  if (agent_List_fp)
    {
      fclose (agent_List_fp);
    }
}

/*
 * repl_process_request() - Base worker routine, process each request
 *   return: none
 *   input_orderp: pointer to the request info
 *
 * Note:
 *         A SEND thread calls this function to process the assigned
 *         request.
 *         Thread looks at workorder req_buf field to determine requested
 *         operation. Executes assoicated account routine, sends response
 *         back to client and then will do an implicit exit when this routine
 *         finishes.
 *
 *      called by SEND thread
 */
void
repl_process_request (void *input_orderp)
{
  int req_type;			/* request type from the agent */
  int pageid;			/* the target pageid to be processed */
  int agentid;			/* agent id of the repl_agent */
  int agent_port;		/* agent port id of the repl_agent */
  REPL_REQUEST *req = (REPL_REQUEST *) input_orderp;
  int error = NO_ERROR;
  SIMPLE_BUF *data = NULL;
  REPL_AGENT_INFO *agent_info = NULL;
  bool in_archive;

  sscanf (req->req_buf, "%d %d %d", &req_type, &agentid, &pageid);

  /* request processing */
  switch (req_type)
    {
      /* STEP1 : repl_agent starts with requesting the agent id */
    case REPL_MSG_GET_AGENT_ID:
      /* agentid is repl_agent port id */
      agent_port = agentid;
      agentid = -1;
      (void) repl_svr_process_agent_info_req (req, agent_port, &agentid);
      break;

      /* STEP2 : repl_agent requests the log header info, if this is the
       *         first time to contact server
       */
    case REPL_MSG_GET_LOG_HEADER:
      error = repl_svr_process_log_hdr_req (agentid, req, &data);
      break;

      /* STEP 3: repl_agent requests the TR log continuously */
    case REPL_MSG_GET_NEXT_LOG:
      error =
	repl_svr_process_get_log_req (agentid, pageid, &in_archive, &data);
      if (error != NO_ERROR)
	{
	  error =
	    repl_svr_process_read_log_req (agentid, pageid, &in_archive,
					   &data);
	}
      break;

      /* STEP 4: repl_agent requests the TR log again, repl_server should
       *         read the log page from the disk instead of fetching
       *         the page from the buffer area
       */
    case REPL_MSG_READ_LOG:
      error =
	repl_svr_process_read_log_req (agentid, pageid, &in_archive, &data);
      break;
    default:
      break;
    }

  /* send result */
  switch (req_type)
    {
    case REPL_MSG_GET_AGENT_ID:
      error = repl_svr_sock_send_result (req->agent_fd, agentid);
      if (error == NO_ERROR && agentid != -1)
	{
	  repl_write_agent_list ();
	}
      break;

    case REPL_MSG_GET_LOG_HEADER:
      if (error != NO_ERROR)
	{
	  (void) repl_svr_sock_send_logpage (req->agent_fd, REPL_REQUEST_FAIL,
					     in_archive, data);
	}
      else
	{
	  error =
	    repl_svr_sock_send_logpage (req->agent_fd, REPL_REQUEST_SUCCESS,
					in_archive, data);
	}
      break;

    case REPL_MSG_GET_NEXT_LOG:
    case REPL_MSG_READ_LOG:
      if (error != NO_ERROR)
	{
	  (void) repl_svr_sock_send_logpage (req->agent_fd, REPL_REQUEST_FAIL,
					     in_archive, data);
	}
      else
	{
	  error =
	    repl_svr_sock_send_logpage (req->agent_fd, REPL_REQUEST_SUCCESS,
					in_archive, data);
	}

      if (error == NO_ERROR)
	{
	  agent_info = repl_get_agent_info (agentid);
	  if (agent_info != NULL)
	    {
	      agent_info->safe_pageid = pageid - 1;
	      repl_write_agent_list ();
	    }
	}
      break;

    default:
      (void) repl_svr_sock_send_result (req->agent_fd, REPL_REQUEST_FAIL);
      break;
    }
}

/*
 * repl_log_iopagesize() - get the log page size and active log file name
 *   return: NO_ERROR or REPL_SERVER_ERROR
 *    db_Name     : db name of the master db
 *    logpath     : file path of the transaction log
 *    log_prefix  : log prefix
 *
 * Note:
 *     Get the following things
 *               - log_Name_active : the file name of active log file
 *               - log_io_pagesize : the size of IO page
 */
static int
repl_log_iopagesize (THREAD_ENTRY * thread_p, const char *db_Name,
		     const char *logpath, const char *log_prefix)
{
  int error = NO_ERROR;

  /*
   * set the global variables related log  (refer to log_page_buffer.c)
   *  - log_Name_active
   *  - log_Name_info
   *  - log_Name_bkupinfo
   *  - log_Name_volinfo
   *  - log_Db_fullname
   */

  error = logpb_initialize_log_names (thread_p, db_Name, logpath, log_prefix);
  if (error != NO_ERROR)
    {
      return error;
    }

  /* We read the active log file to get the io page size.. */
  if ((repl_Log.log_vdes = repl_io_open (log_Name_active, O_RDONLY, 0))
      == NULL_VOLDES)
    {
      REPL_ERR_RETURN_ONE_ARG (REPL_FILE_SERVER, REPL_SERVER_CANT_OPEN_ACTIVE,
			       log_Name_active);
    }

  repl_Log.hdr_page = (LOG_PAGE *) malloc (REPL_DEF_LOG_PAGE_SIZE);
  REPL_CHECK_ERR_NULL (REPL_FILE_SERVER, REPL_SERVER_MEMORY_ERROR,
		       repl_Log.hdr_page);

  error = repl_io_read (repl_Log.log_vdes, (char *) repl_Log.hdr_page, 0,
			REPL_DEF_LOG_PAGE_SIZE);
  REPL_CHECK_ERR_ERROR (REPL_FILE_SERVER, REPL_SERVER_IO_ERROR);

  repl_Log.log_hdr = (struct log_header *) repl_Log.hdr_page->area;

  repl_Log.pgsize = repl_Log.log_hdr->db_iopagesize;

  if (repl_Log.pgsize > REPL_DEF_LOG_PAGE_SIZE)
    {
      repl_Log.hdr_page = (LOG_PAGE *) realloc (repl_Log.hdr_page,
						repl_Log.pgsize);
    }

  return error;
}

/*
 * repl_get_log_volume() - get the full path of the active log file
 *   return: NO_ERROR or REPL_SERVER_ERROR
 *
 * Note:
 *        . reads the databases.txt, find the volume info of the target db.
 *        . get log_Name_active,
 */
static int
repl_get_log_volume (THREAD_ENTRY * thread_p)
{
  DB_INFO *db = NULL;
  DB_INFO *dir = NULL;
  const char *log_prefix;
  int error = NO_ERROR;
  char tmp_str[1024];
  int len;

  sysprm_load_and_init (db_Name, NULL);

  if ((cfg_read_directory (&dir, true) != NO_ERROR) || dir == NULL)
    REPL_ERR_LOG (REPL_FILE_SERVER, REPL_SERVER_CANT_OPEN_DBINFO);

  if (error == NO_ERROR)
    {
      db = cfg_find_db_list (dir, db_Name);
      REPL_CHECK_ERR_NULL_ONE_ARG (REPL_FILE_SERVER,
				   REPL_SERVER_CANT_FIND_DBINFO,
				   db, (char *) db_Name);
    }

  log_prefix = fileio_get_base_file_name (db_Name);
  REPL_CHECK_ERR_NULL (REPL_FILE_SERVER,
		       REPL_AGENT_INTERNAL_ERROR, log_prefix);

  /* get the io page size and set the global variable - log_Name_active */
  if (error == NO_ERROR)
    {
      len = strlen (db->pathname) + strlen (db_Name) + 3;
      COMPOSE_FULL_NAME (tmp_str, db->pathname, db_Name);
      error =
	repl_log_iopagesize (thread_p, tmp_str, db->logpath, log_prefix);
      if (error == NO_ERROR)
	{
	  sprintf (tmp_str, "%s/%s_repl.lst", db->pathname, db_Name);
	  error = repl_load_agent_list (tmp_str);
	}
    }

  cfg_free_directory (dir);

  return error;
}

static int
repl_load_agent_list (char *file_name)
{
  int error = NO_ERROR;
  char strLine[1024];
  char ip[1024];
  int portid, pageid;
  REPL_AGENT_INFO *agInfo;

  REPL_CHECK_ERR_NULL (REPL_FILE_SERVER,
		       REPL_AGENT_INTERNAL_ERROR, file_name);

  agent_List_fp = fopen (file_name, "w+");
  REPL_CHECK_ERR_NULL (REPL_FILE_SERVER,
		       REPL_AGENT_INTERNAL_ERROR, agent_List_fp);
  while (fgets (strLine, 1024, agent_List_fp) != NULL)
    {
      sscanf (strLine, "%s %d %d", ip, &portid, &pageid);
      agInfo = (REPL_AGENT_INFO *) malloc (DB_SIZEOF (REPL_AGENT_INFO));
      REPL_CHECK_ERR_NULL (REPL_FILE_SERVER,
			   REPL_AGENT_INTERNAL_ERROR, agInfo);

      agInfo->agentid = -1;
      strcpy (agInfo->ip, ip);
      agInfo->port_id = portid;
      agInfo->safe_pageid = pageid;

      agInfo->next = agent_List;
      agent_List = agInfo;
    }

  return error;
}

static void
usage (const char *argv0)
{
  char *exec_name;

  exec_name = basename ((char *) argv0);
  msgcat_init ();
  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS, 1, 29),
	   VERSION, exec_name, exec_name);
  msgcat_final ();
}

/*
 * main() - Main Routine of repl_server process
 *   return: int
 *   argc : the number of arguments
 *   argv : argument list
 *
 * Note:
 *      The main routine.
 *         1. Initialize
 *            . signal process
 *            . get parameters (dbname, port_num, repl_err_log path)
 *            . get the log file name & IO page size
 *            . initialize communication stuff
 *            . initialize thread pool (daemon thread will work right now..)
 *         2. body (loop) - process the request
 *            . catch the request
 *            . if shutdown request --> process
 *            . else add the request to the job queue (worker threads will
 *                                                     catch them..)
 */
int
main (int argc, char **argv)
{
  int error = NO_ERROR;
  struct timeval time_fsync, time_now;

  struct option agent_option[] = {
    {"master-db", 1, 0, 'd'},
    {"port", 1, 0, 'p'},
    {"error-file", 1, 0, 'e'},
    {"agent-num", 1, 0, 'a'},
    {0, 0, 0, 0}
  };
  const char *env;

  /*
   * argument parsing, to get
   *         - db name               (db_Name)
   *         - port num              (port_Num)
   *         - err log file path     (err_Log)
   */

  err_Log_fp = stdout;

  /* initialize message catalog for argument parsing and usage() */
  if (utility_initialize () != NO_ERROR)
    {
      REPL_ERR_LOG (REPL_FILE_SERVER, REPL_SERVER_CANT_OPEN_CATALOG);
      return (-1);
    }

  while (1)
    {
      int option_index = 0;
      int option_key;

      option_key = getopt_long (argc, argv, "hd:p:e:a:",
				agent_option, &option_index);
      if (option_key == -1)
	{
	  break;
	}

      switch (option_key)
	{
	case 'd':
	  db_Name = strdup (optarg);
	  break;
	case 'p':
	  port_Num = atoi (optarg);
	  break;
	case 'e':
	  err_Log = strdup (optarg);
	  break;
	case 'a':
	  max_Agent_num = atoi (optarg);
	  break;
	case 'h':
	default:
	  usage (argv[0]);
	  return -1;
	}
    }

  if (max_Agent_num < 1)
    {
      max_Agent_num = DEFAULT_MAX_AGENTS;
    }

  /* check the argument, we have to get db_Name & port_Num here */
  if (strlen (db_Name) < 1 || port_Num <= 0)
    {
      msgcat_init ();
      fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS, 1, 22));
      msgcat_final ();
      return (-1);
    }

  /* open the error log file */
  err_Log_fp = fopen (err_Log, "a");
  if (err_Log_fp == NULL)
    {
      err_Log_fp = stdout;
    }

  /* get the full name of active log file */
  if ((error = repl_get_log_volume (NULL)) != NO_ERROR)
    {
      repl_svr_shutdown (false);
      return (-1);
    }

  /* to be a daemon process */
  env = envvar_get ("NO_DAEMON");
  if (env == NULL || strcmp (env, "no") == 0)
    {
      if (repl_start_daemon () != NO_ERROR)
	{
	  exit (-1);
	}
    }

  /* signal processing & thread pool init */
  if (repl_svr_tp_init (MAX_WORKER_THREAD, 0) != NO_ERROR)
    {
      repl_svr_shutdown (false);
      return (-1);
    }

  /* connect to the master just for showing the process status using commdb */
  if (repl_connect_to_master (true, db_Name) != NO_ERROR)
    {
      REPL_ERR_LOG (REPL_FILE_SERVER, REPL_SERVER_CANT_CONNECT_TO_MASTER);
      repl_svr_shutdown (false);
      return (-1);
    }

  /* Initialize the communication stuff */
  error = repl_svr_sock_init ();
  if (error != NO_ERROR)
    {
      repl_svr_shutdown (true);
      return (-1);
    }

  /*
   * Now, we are ready for starting the worker threads.
   */
  error = repl_svr_tp_start ();
  if (error != NO_ERROR)
    {
      repl_svr_shutdown (true);
      return (-1);
    }

  /* Get the request from repl_agent... */
  gettimeofday (&time_fsync, NULL);
  for (;;)
    {

      /*
       * check the "shutdown in progress by a signal,
       * if true, don't catch any request ...
       */
      if (repl_svr_check_shutdown () == true)
	{
	  break;
	}

      /* Wait for a request */
      if ((error = repl_svr_sock_get_request ()) != NO_ERROR)
	{
	  break;
	}

      gettimeofday (&time_now, NULL);
      if ((time_now.tv_sec - time_fsync.tv_sec) > REPLSERVER_ARG_FSYNC_SEC)
	{
	  fflush (agent_List_fp);
	  gettimeofday (&time_fsync, NULL);
	}
    }

  repl_svr_thread_end ();
  repl_svr_shutdown (true);
  return 0;
}

static REPL_AGENT_INFO *
repl_get_agent_info (int agent_id)
{
  REPL_AGENT_INFO *tmp;

  for (tmp = agent_List; tmp != NULL; tmp = tmp->next)
    {
      if (tmp->agentid == agent_id)
	{
	  return tmp;
	}
    }

  return NULL;
}

static int
repl_write_agent_list ()
{
  int error = NO_ERROR;
  REPL_AGENT_INFO *agent_info;

  fseek (agent_List_fp, 0, SEEK_SET);
  for (agent_info = agent_List; agent_info != NULL;
       agent_info = agent_info->next)
    {
      fprintf (agent_List_fp, "%s %d %d\n",
	       agent_info->ip, agent_info->port_id, agent_info->safe_pageid);
    }

  return error;
}
