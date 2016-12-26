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
 * cas_runner.c -
 */

#ident "$Id$"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <signal.h>
#include <math.h>
#if defined(WINDOWS)
#include <winsock2.h>
#include <windows.h>
#include <process.h>
#include <sys/timeb.h>
#else
#include <pthread.h>
#include <unistd.h>
#endif
#ifdef HAVE_GETOPT_H
#include <getopt.h>
#else
#include "getopt.h"
#endif

#include "cas_common.h"
#include "porting.h"
#include "cas_cci.h"
#include "broker_log_util.h"

#define PRINT_CCI_ERROR(ERRCODE, CCI_ERROR, result_fp)	\
	do {					\
	  T_CCI_ERROR *cci_error_p = CCI_ERROR;	\
	  if ((ERRCODE) == CCI_ER_DBMS && cci_error_p != NULL) {	\
	    if (!ignore_error(cci_error_p->err_code)) {			\
	      cas_error_flag = 1;			\
	      if (cubrid_manager_run) {			\
	        fprintf(cas_error_fp, "server error : (%d) %s\n", cci_error_p->err_code, cci_error_p->err_msg);	\
	      } 					\
	      else {					\
	        if (result_fp) {			\
	          fprintf(result_fp, "%s: server error : %d %s\n", exec_script_file, cci_error_p->err_code, cci_error_p->err_msg);	\
	        }					\
	        fprintf(cas_error_fp, "%s: server error : %d %s\n", exec_script_file, cci_error_p->err_code, cci_error_p->err_msg);	\
	      }						\
	    }						\
	  }					\
	  else if ((ERRCODE) < 0) {		\
	    char msgbuf[1024] = "";		\
	    cas_error_flag = 1;			\
	    if (cubrid_manager_run) {		\
	      cci_get_error_msg(ERRCODE, NULL, msgbuf, sizeof(msgbuf));	\
	      fprintf(cas_error_fp, "%s\n", msgbuf);	\
	    }					\
	    else {				\
	      cci_get_error_msg(ERRCODE, NULL, msgbuf, sizeof(msgbuf));	\
	      if (result_fp) {			\
	        fprintf(result_fp, "%s: cci_error : %d %s\n", exec_script_file, (ERRCODE), msgbuf);	\
	      }					\
	      fprintf(cas_error_fp, "%s: cci_error : %d %s\n", exec_script_file, (ERRCODE), msgbuf);	\
	    }					\
	  }					\
	} while (0)

#define FREE_BIND_INFO(NUM_BIND, BIND_INFO)	\
	do {					\
	  int i;				\
	  for (i=0 ; i < NUM_BIND ; i++) {	\
	    FREE_MEM(BIND_INFO[i].value);	\
	  }					\
	  NUM_BIND = 0;				\
	} while (0)

#define MAX_NODE_INFO	100
#define MAX_IGN_SRV_ERR	100

#if defined(WINDOWS)
#define	strcasecmp(X, Y)		_stricmp(X, Y)
#ifdef THREAD_FUNC
#undef THREAD_FUNC
#endif
#define THREAD_FUNC			unsigned __stdcall
#define SLEEP_SEC(X)			Sleep((X) * 1000)
#define SLEEP_MILISEC(sec, msec)	Sleep((sec) * 1000 + (msec))
#else
#define THREAD_FUNC		void*
#define SLEEP_SEC(X)		sleep(X)
#define SLEEP_MILISEC(sec, msec)			\
	do {						\
	  struct timeval sleep_time_val;		\
	  sleep_time_val.tv_sec = sec;			\
	  sleep_time_val.tv_usec = (msec) * 1000;	\
	  select(0, 0, 0, 0, &sleep_time_val);		\
	} while(0)
#endif

#define STRDUP(TARGET, SOURCE) \
        do {                                          \
          if(TARGET != NULL) free(TARGET);    \
          TARGET = strdup (SOURCE);            \
        } while(0)

#define SERVER_HANDLE_ALLOC_SIZE        (MAX_SERVER_H_ID + 1)

typedef struct t_bind_info T_BIND_INFO;
struct t_bind_info
{
  char *value;
  int type;
  int len;
};

typedef struct t_node_info T_NODE_INFO;
struct t_node_info
{
  char *node_name;
  char *dbname;
  char *ip;
  char *dbuser;
  char *dbpasswd;
  int port;
};

static double calc_stddev (double *t, double avg, int count);
static double calc_avg (double *t, int count);
static void calc_min_max (double *t, int count, double *min, double *max);
static int get_args (int argc, char *argv[]);
static int read_conf (void);
static void cas_runner (FILE * fp, FILE * result_fp, double *ret_exec_time, double *ret_prepare_time);
static THREAD_FUNC thr_main (void *arg);
static int process_execute (char *msg, int *req_h, int num_bind, T_BIND_INFO * bind_info, FILE * result_fp,
			    double *sum_execute_time);
static int process_bind (char *msg, int *num_bind_p, T_BIND_INFO * bind_info);
static int process_endtran (int con_h, int *req_h, FILE * result_fp);
static int process_close_req (char *linebuf, int *req_h, FILE * result_fp);
static void print_result (int cci_res, int req_id, FILE * fp);
static void free_node (T_NODE_INFO * node);
static int make_node_info (T_NODE_INFO * node, char *node_name, char *info_str);
static int set_args_with_node_info (char *node_name);
static int ignore_error (int code);
static char *make_sql_stmt (char *src);

const char *cci_client_name = "JDBC";

static char *broker_host = NULL;
static int broker_port = 0;
static char *dbname = NULL;
static char *dbuser = NULL;
static char *dbpasswd = NULL;
static int num_thread = 1;
static int repeat_count = 1;
static char *exec_script_file;
static int batch_mode = 0;
static char *result_file = NULL;
static char *cas_err_file = NULL;
static int fork_delay = 0;
static char *node_name = NULL;
static int think_time = 0;
static int qa_test_flag = 0;
static int num_replica = 1;
static int dump_query_plan = 0;
static int autocommit_mode = 0;
static int statdump_mode = 0;

static double *run_time_exec;
static FILE *cas_error_fp;
static int cas_error_flag = 0;
static int cubrid_manager_run = 0;

static T_NODE_INFO node_table[MAX_NODE_INFO];
static int num_node = 0;
static int ign_srv_err_list[MAX_IGN_SRV_ERR];
static int num_ign_srv_err = 0;

int
main (int argc, char *argv[])
{
  pthread_t *thr_id;
  int i;
  const char *err_str = "-";
  double avg;
  double stddev;
  double min, max;
  char *cm_out_msg_fname = NULL;
  FILE *cm_out_msg_fp = NULL;

#if !defined(WINDOWS)
  signal (SIGPIPE, SIG_IGN);
#endif

  if (read_conf () < 0)
    {
      return -1;
    }

  if (get_args (argc, argv) < 0)
    return -1;

  cm_out_msg_fname = getenv ("CUBRID_MANAGER_OUT_MSG_FILE");
  if (cm_out_msg_fname != NULL)
    {
      cubrid_manager_run = 1;
    }

  if (node_name != NULL)
    {
      if (set_args_with_node_info (node_name) < 0)
	{
	  fprintf (stderr, "error:node (%s)\n", node_name);
	  return -1;
	}
    }

  if (broker_host == NULL || broker_host[0] == '\0')
    {
      fprintf (stderr, "error:broker_host\n");
      return -1;
    }
  if (broker_port <= 0)
    {
      fprintf (stderr, "error:broker_port(%d)\n", broker_port);
      return -1;
    }
  if (dbname == NULL || dbname[0] == '\0')
    {
      fprintf (stderr, "errorr:dbname\n");
      return -1;
    }
  if (dbuser == NULL)
    dbuser = (char *) "PUBLIC";
  if (dbpasswd == NULL)
    dbpasswd = (char *) "";

  cas_error_fp = fopen (cas_err_file, (batch_mode ? "a" : "w"));
  if (cas_error_fp == NULL)
    {
      fprintf (stderr, "fopen error [%s]\n", cas_err_file);
      return -1;
    }

#ifdef DUP_RUN
  num_thread = 1;
#endif

  if (repeat_count < 1)
    repeat_count = 1;
  if (num_thread < 1)
    num_thread = 1;
  if (num_replica < 1)
    num_replica = 1;

  if (!batch_mode && !cubrid_manager_run)
    {
      fprintf (stdout, "broker_host = %s\n", broker_host);
      fprintf (stdout, "broker_port = %d\n", broker_port);
      fprintf (stdout, "num_thread = %d\n", num_thread);
      fprintf (stdout, "repeat = %d\n", repeat_count);
      fprintf (stdout, "dbname = %s\n", dbname);
      fprintf (stdout, "dbuser = %s\n", dbuser);
      fprintf (stdout, "dbpasswd = %s\n", dbpasswd);
      if (result_file)
	{
	  fprintf (stdout, "result_file = %s\n", result_file);
	}
    }

  thr_id = (pthread_t *) malloc (sizeof (pthread_t) * num_thread);
  if (thr_id == NULL)
    {
      fprintf (stderr, "malloc error\n");
      return -1;
    }
  run_time_exec = (double *) malloc (sizeof (double) * num_thread * repeat_count);
  if (run_time_exec == NULL)
    {
      FREE_MEM (thr_id);
      fprintf (stderr, "malloc error\n");
      return -1;
    }

  cci_init ();

  if (qa_test_flag == 1)
    {
      int *con_handle;
      T_CCI_ERROR cci_error;
      con_handle = (int *) malloc (sizeof (int) * num_thread);
      if (con_handle == NULL)
	{
	  FREE_MEM (thr_id);
	  fprintf (stderr, "malloc error\n");
	  return -1;
	}
      for (i = 0; i < num_thread; i++)
	{
	  con_handle[i] = cci_connect (broker_host, broker_port, dbname, dbuser, dbpasswd);
	  cci_get_db_version (con_handle[i], NULL, 0);
	}
      for (i = 0; i < num_thread; i++)
	{
	  cci_disconnect (con_handle[i], &cci_error);
	}
      FREE_MEM (con_handle);
    }

  for (i = 0; i < num_thread; i++)
    {
      if (i > 0 && fork_delay > 0)
	{
	  SLEEP_SEC (fork_delay);
	}

      if (pthread_create (&thr_id[i], NULL, thr_main, (void *) &i) < 0)
	{
	  FREE_MEM (thr_id);
	  perror ("Error:cannot create thread");
	  return -1;
	}
    }

  for (i = 0; i < num_thread; i++)
    {
      if (pthread_join (thr_id[i], NULL) < 0)
	{
	  perror ("pthread_join");
	}
    }

  if (cm_out_msg_fname != NULL)
    {
      cm_out_msg_fp = fopen (cm_out_msg_fname, "w");
    }

  if (cm_out_msg_fp == NULL)
    {
      cm_out_msg_fp = stdout;
    }

  fclose (cas_error_fp);
  if (cas_error_flag)
    {
      FILE *err_fp;
      if (cm_out_msg_fname != NULL)
	{
	  err_fp = cm_out_msg_fp;
	}
      else
	{
	  err_fp = stderr;
	}
      err_str = "ERR";
      fprintf (err_fp, "\n");
      fprintf (err_fp, "********************************\n");
      if (!cubrid_manager_run)
	{
	  fprintf (err_fp, "cas error : %s\n", cas_err_file);
	}
      if (!batch_mode)
	{
	  char buf[1024];
	  FILE *fp;
	  size_t readlen;
	  fp = fopen (cas_err_file, "r");
	  if (fp != NULL)
	    {
	      while ((readlen = fread (buf, 1, sizeof (buf), fp)) > 0)
		{
		  if (readlen > sizeof (buf))
		    {
		      readlen = sizeof (buf);
		    }
		  fwrite (buf, 1, readlen, err_fp);
		}
	      fclose (fp);
	    }
	}
      fprintf (err_fp, "********************************\n");
    }
  else
    {
      if (!batch_mode)
	unlink (cas_err_file);
    }

  avg = calc_avg (run_time_exec, num_thread * repeat_count);
  stddev = calc_stddev (run_time_exec, avg, num_thread * repeat_count);

  if (cubrid_manager_run)
    {
      calc_min_max (run_time_exec, num_thread * repeat_count, &min, &max);
      fprintf (cm_out_msg_fp, "min : %.6f\n", min);
      fprintf (cm_out_msg_fp, "max : %.6f\n", max);
      fprintf (cm_out_msg_fp, "avg : %.6f\n", avg);
      fprintf (cm_out_msg_fp, "stddev : %.6f\n", stddev);
    }
  else
    {
      fprintf (stdout, "%.6f %.6f %s\n", avg, stddev, err_str);
    }

  if (cm_out_msg_fname != NULL)
    {
      fflush (cm_out_msg_fp);
      if (cm_out_msg_fp != stdout)
	{
	  fclose (cm_out_msg_fp);
	}
    }
  FREE_MEM (thr_id);
  FREE_MEM (run_time_exec);

  return 0;
}

static void
calc_min_max (double *t, int count, double *min, double *max)
{
  int i;
  if (count <= 0)
    {
      *min = 0;
      *max = 0;
      return;
    }
  *min = t[0];
  *max = t[0];
  for (i = 1; i < count; i++)
    {
      if (*min > t[i])
	*min = t[i];
      if (*max < t[i])
	*max = t[i];
    }
}

static double
calc_avg (double *t, int count)
{
  double sum = 0;
  int i;
  for (i = 0; i < count; i++)
    {
      sum += t[i];
    }
  return (sum / count);
}

static double
calc_stddev (double *t, double avg, int count)
{
  double sum = 0;
  int i;
  for (i = 0; i < count; i++)
    {
      sum += ((t[i] - avg) * (t[i] - avg));
    }
  sum /= count;
  return (sqrt (sum));
}

static int
get_args (int argc, char *argv[])
{
  int c;

  while ((c = getopt (argc, argv, "saQbqI:P:d:u:p:t:r:o:e:f:n:h:R:")) != EOF)
    {
      switch (c)
	{
	case 's':
	  statdump_mode = 1;
	  break;
	case 'a':
	  autocommit_mode = 1;
	  break;
	case 'b':
	  batch_mode = 1;
	  break;
	case 'q':
	  qa_test_flag = 1;
	  break;
	case 'I':
	  broker_host = optarg;
	  break;
	case 'P':
	  broker_port = atoi (optarg);
	  break;
	case 'd':
	  dbname = optarg;
	  break;
	case 'u':
	  dbuser = optarg;
	  break;
	case 'p':
	  dbpasswd = optarg;
#if defined (LIUNUX)
	  memset (optarg, '*', strlen (optarg));
#endif
	  break;
	case 't':
	  num_thread = atoi (optarg);
	  if (num_thread < 1)
	    num_thread = 1;
	  break;
	case 'r':
	  repeat_count = atoi (optarg);
	  if (repeat_count < 1)
	    repeat_count = 1;
	  break;
	case 'o':
	  result_file = optarg;
	  break;
	case 'e':
	  cas_err_file = optarg;
	  break;
	case 'f':
	  fork_delay = atoi (optarg);
	  break;
	case 'n':
	  node_name = optarg;
	  break;
	case 'h':
	  think_time = atoi (optarg);
	  break;
	case 'R':
	  num_replica = atoi (optarg);
	  break;
	case 'Q':
	  dump_query_plan = 1;
	  break;
	case '?':
	  goto getargs_err;
	}
    }

  if (optind >= argc)
    {
      goto getargs_err;
    }

  exec_script_file = argv[optind];

  if (batch_mode)
    {
      if (result_file != NULL && strcmp (result_file, "stdout") == 0)
	result_file = NULL;
    }

  return 0;

getargs_err:
  fprintf (stderr,
	   "usage : %s [OPTION] exec_script_file\n" "\n" "valid options:\n" "  -I   broker host\n"
	   "  -P   broker port\n" "  -d   database name\n" "  -u   user name\n" "  -p   user password\n"
	   "  -t   the number of thread\n" "  -r   the number of times to execute entire query by each thread\n"
	   "  -Q   enable to print a plan per query\n" "  -o   result file\n"
	   "  -s   enable to print a statdump per query\n" "  -a   enable auto commit mode\n", argv[0]);
  return -1;
}

static THREAD_FUNC
thr_main (void *arg)
{
  int id = *(int *) arg;
  FILE *fp;
  int i;
  FILE *result_fp;

  fp = fopen (exec_script_file, "r");
  if (fp == NULL)
    {
      fprintf (stderr, "fopen error [%s]\n", exec_script_file);
      goto end;
    }

  if (result_file == NULL)
    {
      result_fp = NULL;
    }
  else if (strcmp (result_file, "stdout") == 0)
    {
      result_fp = stdout;
    }
  else if (strcmp (result_file, "stderr") == 0)
    {
      result_fp = stderr;
    }
  else
    {
      char result_filename[256];
      sprintf (result_filename, "%s.%d", result_file, id);
      result_fp = fopen (result_filename, "w");
    }

#ifndef DUP_RUN
  if (repeat_count > 1)
    {
      cas_runner (fp, result_fp, NULL, NULL);
      fseek (fp, 0, SEEK_SET);
    }
#endif

  for (i = 0; i < repeat_count; i++)
    {
      double e, p;
      cas_runner (fp, result_fp, &e, &p);
      run_time_exec[id * repeat_count + i] = e;
      fseek (fp, 0, SEEK_SET);
      if (think_time > 0)
	SLEEP_SEC (think_time);
    }

  fclose (fp);
  if (result_fp != NULL && result_fp != stderr && result_fp != stdout)
    fclose (result_fp);

end:
#if defined(WINDOWS)
  return 0;
#else
  return NULL;
#endif
}

static void
cas_runner (FILE * fp, FILE * result_fp, double *ret_exec_time, double *ret_prepare_time)
{
  char *sql_stmt = NULL;
  int con_h = -1;
  T_CCI_ERROR cci_error;
  int num_bind = 0;
  double prepare_time = 0;
  double sum_execute_time = 0;
  double sum_prepare_time = 0;
  char *linebuf = NULL;
  char *data = NULL;
  T_BIND_INFO *bind_info = NULL;
  int *req_h = NULL;
  int req_stat_h = -1;
  int error;
  int ind;
  int i;
  T_STRING *linebuf_tstr = NULL;
#ifdef DUP_RUN
  int dup_con_h;
  int *dup_req_h = NULL;
#endif

  linebuf_tstr = t_string_make (1000);
  req_h = (int *) malloc (sizeof (int) * SERVER_HANDLE_ALLOC_SIZE);
  bind_info = (T_BIND_INFO *) malloc (sizeof (T_BIND_INFO) * MAX_BIND_VALUE);
#ifdef DUP_RUN
  dup_req_h = (int *) malloc (sizeof (int) * SERVER_HANDLE_ALLOC_SIZE);
#endif

  if (linebuf_tstr == NULL || req_h == NULL || bind_info == NULL
#ifdef DUP_RUN
      || dup_req_h == NULL
#endif
    )
    {
      fprintf (stderr, "malloc error\n");
      goto end_cas_runner;
    }
  memset (req_h, 0, sizeof (int) * SERVER_HANDLE_ALLOC_SIZE);
  memset (bind_info, 0, sizeof (T_BIND_INFO) * MAX_BIND_VALUE);
#ifdef DUP_RUN
  memset (dup_req_h, 0, sizeof (int) * SERVER_HANDLE_ALLOC_SIZE);
#endif

  con_h = cci_connect (broker_host, broker_port, dbname, dbuser, dbpasswd);
  if (con_h < 0)
    {
      PRINT_CCI_ERROR (con_h, NULL, result_fp);
      goto end_cas_runner;
    }
#ifdef DUP_RUN
  dup_con_h = cci_connect (broker_host, broker_port, dbname, dbuser, dbpasswd);
  if (dup_con_h < 0)
    {
      fprintf (stderr, "DUP_RUN cci_connect error\n");
      goto end_cas_runner;
    }
#endif

  if (autocommit_mode)
    {
      if (cci_set_autocommit (con_h, CCI_AUTOCOMMIT_TRUE) < 0)
	{
	  fprintf (stderr, "cannot set autocommit mode");
	  goto end_cas_runner;
	}
#ifdef DUP_RUN
      if (cci_set_autocommit (dup_con_h, CCI_AUTOCOMMIT_TRUE) < 0)
	{
	  fprintf (stderr, "DUP_RUN cannot set autocommit mode");
	  goto end_cas_runner;
	}
#endif
    }

  if (statdump_mode)
    {
      req_stat_h = cci_prepare (con_h, "set @collect_exec_stats = 1", 0, &cci_error);
      if (req_stat_h < 0)
	{
	  fprintf (stderr, "cci_prepare error\n");
	}
      else
	{
	  int res;
	  error = cci_execute (req_stat_h, 0, 0, &cci_error);

	  res = cci_close_req_handle (req_stat_h);
	  if (res < 0)
	    {
	      fprintf (stderr, "cci_close_req_error\n");
	      req_stat_h = -1;
	    }

	  if (error < 0)
	    {
	      fprintf (stderr, "cci_execute error\n");
	    }
	  else
	    {
	      req_stat_h = cci_prepare (con_h, "show exec statistics", 0, &cci_error);
	      if (req_stat_h < 0)
		{
		  fprintf (stderr, "cci_prepare error\n");
		}
	    }
	}
    }

  while (1)
    {
      if (ut_get_line (fp, linebuf_tstr, NULL, NULL) < 0)
	{
	  fprintf (stderr, "malloc error\n");
	  goto end_cas_runner;
	}
      if (t_string_len (linebuf_tstr) <= 0)
	break;
      linebuf = t_string_str (linebuf_tstr);

      if (linebuf[strlen (linebuf) - 1] == '\n')
	linebuf[strlen (linebuf) - 1] = '\0';

      if (linebuf[0] == 'Q')
	{
	  FREE_MEM (sql_stmt);
	  sql_stmt = make_sql_stmt (linebuf + 2);
	  if (sql_stmt == NULL)
	    {
	      goto end_cas_runner;
	    }

	  if (result_fp)
	    {
	      fprintf (result_fp, "-------------- query -----------------\n");
	      fprintf (result_fp, "%s\n", sql_stmt);
	    }
	}
      else if (linebuf[0] == 'P')
	{
	  int req_id, prepare_flag;
	  struct timeval begin, end;

	  if (sscanf (linebuf + 2, "%d %d", &req_id, &prepare_flag) < 2)
	    {
	      fprintf (stderr, "file format error : %s\n", linebuf);
	      FREE_MEM (sql_stmt);
	      goto end_cas_runner;
	    }
	  if (req_id < 0 || req_id >= SERVER_HANDLE_ALLOC_SIZE)
	    {
	      fprintf (stderr, "request id error : %d (valid range 0-%d)\n", req_id, SERVER_HANDLE_ALLOC_SIZE - 1);
	      FREE_MEM (sql_stmt);
	      goto end_cas_runner;
	    }
	  gettimeofday (&begin, NULL);
	  req_h[req_id] = cci_prepare (con_h, sql_stmt, prepare_flag, &cci_error);
	  gettimeofday (&end, NULL);
	  prepare_time = ut_diff_time (&begin, &end);
	  sum_prepare_time += prepare_time;

	  if (result_fp)
	    {
	      fprintf (result_fp, "cci_prepare elapsed time : %.3f \n", prepare_time);
	    }

	  if (req_h[req_id] < 0)
	    {
	      fprintf (cas_error_fp, "prepare error\n%s\nrequest id %d\n", linebuf, req_id);
	      PRINT_CCI_ERROR (req_h[req_id], &cci_error, result_fp);
	    }
#ifdef DUP_RUN
	  dup_req_h[req_id] = cci_prepare (dup_con_h, sql_stmt, prepare_flag, &cci_error);
#endif
	  FREE_MEM (sql_stmt);
	}
      else if (linebuf[0] == 'B')
	{
	  if (process_bind (linebuf, &num_bind, bind_info) < 0)
	    {
	      FREE_BIND_INFO (num_bind, bind_info);
	      goto end_cas_runner;
	    }
	}
      else if (linebuf[0] == 'E')
	{
	  int res;
	  res = process_execute (linebuf, req_h, num_bind, bind_info, result_fp, &sum_execute_time);
#ifdef DUP_RUN
	  process_execute (linebuf, dup_req_h, num_bind, bind_info, result_fp, NULL);
#endif
	  FREE_BIND_INFO (num_bind, bind_info);
	  num_bind = 0;
	  if (res < 0)
	    goto end_cas_runner;
	}
      else if (linebuf[0] == 'C')
	{
	  if (process_close_req (linebuf, req_h, result_fp) < 0)
	    goto end_cas_runner;
#ifdef DUP_RUN
	  process_close_req (linebuf, dup_req_h, result_fp);
#endif
	}
      else if (linebuf[0] == 'T')
	{
	  if (process_endtran (con_h, req_h, result_fp) < 0)
	    goto end_cas_runner;
#ifdef DUP_RUN
	  if (process_endtran (dup_con_h, dup_req_h, result_fp) < 0)
	    {
	      fprintf (stderr, "DUP_RUN end_transaction error\n");
	    }
#endif
	  if (statdump_mode && req_stat_h > 0)
	    {
	      error = cci_execute (req_stat_h, 0, 0, &cci_error);
	      if (error < 0)
		{
		  fprintf (cas_error_fp, "execute error\nshow exec statistics\nrequest id %d\n", req_stat_h);
		  continue;
		}
	      if (result_fp)
		{
		  fprintf (result_fp, "SHOW EXEC STATISTICS\n");
		}

	      while (1)
		{
		  error = cci_cursor (req_stat_h, 1, CCI_CURSOR_CURRENT, &cci_error);

		  if (error == CCI_ER_NO_MORE_DATA)
		    {
		      break;
		    }

		  if (error < 0)
		    {
		      fprintf (cas_error_fp, "cursor error\nrequest id %d\n", req_stat_h);
		      PRINT_CCI_ERROR (error, &cci_error, result_fp);
		      break;
		    }

		  error = cci_fetch (req_stat_h, &cci_error);
		  if (error < 0)
		    {
		      fprintf (cas_error_fp, "fetch error\nrequest id %d\n", req_stat_h);
		      PRINT_CCI_ERROR (error, &cci_error, result_fp);
		      break;
		    }
		  for (i = 1; i <= 2; i++)
		    {
		      error = cci_get_data (req_stat_h, i, CCI_A_TYPE_STR, &data, &ind);
		      if (error < 0)
			{
			  fprintf (cas_error_fp, "get data error\nrequest id %d\n", req_stat_h);
			  PRINT_CCI_ERROR (error, NULL, result_fp);
			  break;
			}
		      if (ind < 0 || data == NULL)
			{
			  if (result_fp)
			    {
			      fprintf (result_fp, "<NULL>\t|");
			    }
			}
		      else
			{
			  if (result_fp)
			    {
			      fprintf (result_fp, "%s\t|", data);
			    }
			}
		    }
		  if (result_fp)
		    {
		      fprintf (result_fp, "\n");
		    }
		}
	    }
	}
      else
	{
	  fprintf (stderr, "file format error : %s\n", linebuf);
	}
    }

end_cas_runner:
  if (req_stat_h > 0)
    {
      cci_close_req_handle (req_stat_h);
    }

  if (con_h > 0)
    {
      cci_disconnect (con_h, &cci_error);
    }
#ifdef DUP_RUN
  if (dup_con_h > 0)
    {
      cci_disconnect (dup_con_h, &cci_error);
    }
#endif

  FREE_MEM (req_h);
  FREE_MEM (bind_info);
  if (linebuf_tstr)
    t_string_free (linebuf_tstr);
#ifdef DUP_RUN
  FREE_MEM (dup_req_h);
#endif
  FREE_MEM (sql_stmt);

  if (ret_exec_time)
    *ret_exec_time = sum_execute_time;
  if (ret_prepare_time)
    *ret_prepare_time = sum_prepare_time;
}

static int
read_conf (void)
{
  FILE *fp;
  char read_buf[1024];
  char buf1[1024], buf2[1024], buf3[1024];
  int lineno = 0;
  const char *conf_file;
  int num_token;
  char *p;

  /* set initial error file name */
  if (cas_err_file == NULL)
    {
      cas_err_file = strdup ("cas_error");
    }

  conf_file = getenv (CAS_RUNNER_CONF_ENV);
  if (conf_file == NULL)
    conf_file = CAS_RUNNER_CONF;

  fp = fopen (conf_file, "r");
  if (fp == NULL)
    {
      /* 
       * fprintf(stderr, "fopen error [%s]\n", CAS_RUNNER_CONF); return -1; */
      return 0;
    }

  while (fgets (read_buf, sizeof (read_buf), fp))
    {
      lineno++;

      p = strchr (read_buf, '#');
      if (p)
	{
	  *p = '\0';
	}
      num_token = sscanf (read_buf, "%1023s %1023s %1023s", buf1, buf2, buf3);
      if (num_token < 2)
	{
	  continue;
	}

      if (num_token == 3)
	{
	  if (strcasecmp (buf1, "node") == 0)
	    {
	      if (num_node >= MAX_NODE_INFO)
		{
		  goto error;
		}
	      if (make_node_info (&node_table[num_node], buf2, buf3) < 0)
		{
		  continue;
		}
	      num_node++;
	    }
	}
      else
	{
	  if (strcasecmp (buf1, "CAS_IP") == 0)
	    {
	      STRDUP (broker_host, buf2);
	    }
	  else if (strcasecmp (buf1, "CAS_PORT") == 0)
	    {
	      broker_port = atoi (buf2);
	    }
	  else if (strcasecmp (buf1, "DBNAME") == 0)
	    {
	      STRDUP (dbname, buf2);
	    }
	  else if (strcasecmp (buf1, "NUM_THREAD") == 0)
	    {
	      num_thread = atoi (buf2);
	    }
	  else if (strcasecmp (buf1, "DBUSER") == 0)
	    {
	      STRDUP (dbuser, buf2);
	    }
	  else if (strcasecmp (buf1, "DBPASSWD") == 0)
	    {
	      STRDUP (dbpasswd, buf2);
	    }
	  else if (strcasecmp (buf1, "REPEAT") == 0)
	    {
	      repeat_count = atoi (buf2);
	    }
	  else if (strcasecmp (buf1, "RESULT_FILE") == 0)
	    {
	      STRDUP (result_file, buf2);
	    }
	  else if (strcasecmp (buf1, "CAS_ERROR_FILE") == 0)
	    {
	      STRDUP (cas_err_file, buf2);
	    }
	  else if (strcasecmp (buf1, "FORK_DELAY") == 0)
	    {
	      fork_delay = atoi (buf2);
	    }
	  else if (strcasecmp (buf1, "IGNORE_SERVER_ERROR") == 0)
	    {
	      int ign_err = atoi (buf2);
	      if (ign_err < 0)
		{
		  if (num_ign_srv_err >= MAX_IGN_SRV_ERR)
		    goto error;
		  ign_srv_err_list[num_ign_srv_err++] = ign_err;
		}
	    }
	  else
	    goto error;
	}
    }

  fclose (fp);
  return 0;

error:
  fprintf (stderr, "%s : error [%d] line\n", CAS_RUNNER_CONF, lineno);
  fclose (fp);
  return -1;
}

static int
process_bind (char *linebuf, int *num_bind_p, T_BIND_INFO * bind_info)
{
  char *p;
  int num_bind = *num_bind_p;

  if (num_bind >= MAX_BIND_VALUE)
    {
      fprintf (stderr, "bind buffer overflow[%d]\n", num_bind);
      return -1;
    }

  bind_info[num_bind].type = atoi (linebuf + 2);
  p = strchr (linebuf + 2, ' ');
  if (p == NULL)
    {
      fprintf (stderr, "file format error : %s\n", linebuf);
      return -1;
    }

  if ((bind_info[num_bind].type == CCI_U_TYPE_CHAR) || (bind_info[num_bind].type == CCI_U_TYPE_STRING)
      || (bind_info[num_bind].type == CCI_U_TYPE_NCHAR) || (bind_info[num_bind].type == CCI_U_TYPE_VARNCHAR)
      || (bind_info[num_bind].type == CCI_U_TYPE_BIT) || (bind_info[num_bind].type == CCI_U_TYPE_VARBIT)
      || (bind_info[num_bind].type == CCI_U_TYPE_ENUM))
    {
      bind_info[num_bind].len = atoi (p + 1);
      p = strchr (p + 1, ' ');
      if (p == NULL)
	{
	  fprintf (stderr, "file format error : %s\n", linebuf);
	  return -1;
	}
    }
  else if (bind_info[num_bind].type == CCI_U_TYPE_BLOB || bind_info[num_bind].type == CCI_U_TYPE_CLOB)
    {
      fprintf (stderr, "binding BLOB/CLOB is not implemented : %s\nreplaced with NULL value.\n", p + 1);
      bind_info[num_bind].type = CCI_U_TYPE_NULL;
    }

  bind_info[num_bind].value = strdup (p + 1);
  if (bind_info[num_bind].value == NULL)
    {
      fprintf (stderr, "malloc error\n");
      return -1;
    }
  *num_bind_p = num_bind + 1;
  return 0;
}

static int
process_execute (char *linebuf, int *req_h, int num_bind, T_BIND_INFO * bind_info, FILE * result_fp,
		 double *sum_execute_time)
{
  int req_id, exec_flag;
  T_CCI_ERROR cci_error;
  struct timeval begin, end;
  double elapsed_time = 0;

  if (sscanf (linebuf + 2, "%d %d", &req_id, &exec_flag) < 2)
    {
      fprintf (stderr, "file format error : %s\n", linebuf);
      return -1;
    }
  if (req_id < 0 || req_id >= SERVER_HANDLE_ALLOC_SIZE)
    {
      fprintf (stderr, "request id error : %d (valid range 0-%d)\n", req_id, SERVER_HANDLE_ALLOC_SIZE - 1);
      return -1;
    }

  if (num_replica > 1)
    {
      exec_flag |= CCI_EXEC_QUERY_ALL;
    }

  if (req_h[req_id] > 0)
    {
      int res;
      if (num_bind > 0)
	{
	  int i, k;
	  for (k = 0; k < num_replica; k++)
	    {
	      for (i = 0; i < num_bind; i++)
		{
		  if ((bind_info[i].type == CCI_U_TYPE_VARBIT) || (bind_info[i].type == CCI_U_TYPE_BIT))
		    {
		      T_CCI_BIT vptr;
		      memset ((char *) &vptr, 0x00, sizeof (T_CCI_BIT));
		      vptr.size = bind_info[i].len;
		      vptr.buf = (char *) bind_info[i].value;
		      res =
			cci_bind_param (req_h[req_id], (k * num_bind) + i + 1, CCI_A_TYPE_BIT, (void *) &(vptr),
					(T_CCI_U_TYPE) bind_info[i].type, CCI_BIND_PTR);
		    }
		  else
		    {
		      res =
			cci_bind_param (req_h[req_id], (k * num_bind) + i + 1, CCI_A_TYPE_STR, bind_info[i].value,
					(T_CCI_U_TYPE) bind_info[i].type, 0);
		    }
		  if (res < 0)
		    {
		      fprintf (cas_error_fp, "bind error\n%s\nrequest id %d bind %d\n", linebuf, req_id, i);
		      PRINT_CCI_ERROR (res, NULL, result_fp);
		    }
		}
	    }
	}

      if (dump_query_plan)
	exec_flag |= CCI_EXEC_QUERY_INFO;

      gettimeofday (&begin, NULL);
      res = cci_execute (req_h[req_id], exec_flag, 0, &cci_error);
      gettimeofday (&end, NULL);
      elapsed_time = ut_diff_time (&begin, &end);
      if (!batch_mode && !cubrid_manager_run)
	{
	  fprintf (stdout, "exec_time : %.3f \n", elapsed_time);
	}

      if (result_fp)
	{
	  fprintf (result_fp, "cci_execute elapsed_time : %.3f \n", elapsed_time);
	}

      if (res < 0)
	{
	  fprintf (cas_error_fp, "execute error\n%s\nrequest id %d\n", linebuf, req_id);
	  PRINT_CCI_ERROR (res, &cci_error, result_fp);
	}
      else
	{
	  print_result (res, req_h[req_id], result_fp);
	}
    }
  if (sum_execute_time)
    *sum_execute_time += elapsed_time;

  return 0;
}

static int
process_close_req (char *linebuf, int *req_h, FILE * result_fp)
{
  int req_id, res;

  req_id = atoi (linebuf + 2);
  if (req_id < 0 || req_id >= SERVER_HANDLE_ALLOC_SIZE)
    {
      fprintf (cas_error_fp, "close error\n%s\nrequest id %d\n", linebuf, req_id);
      PRINT_CCI_ERROR (CCI_ER_REQ_HANDLE, NULL, result_fp);
      return 0;
    }
  if (req_h[req_id] > 0)
    {
      res = cci_close_req_handle (req_h[req_id]);
      if (res < 0)
	{
	  fprintf (cas_error_fp, "close error\n%s\nrequest id %d\n", linebuf, req_id);
	  PRINT_CCI_ERROR (res, NULL, result_fp);
	}
    }
  req_h[req_id] = 0;
  return 0;
}

static int
process_endtran (int con_h, int *req_h, FILE * result_fp)
{
  int res, i;
  T_CCI_ERROR cci_error;

  struct timeval begin, end;
  double commit_time;

  if (!autocommit_mode)
    {
      gettimeofday (&begin, NULL);
      res = cci_end_tran (con_h, CCI_TRAN_ROLLBACK, &cci_error);
      gettimeofday (&end, NULL);
      commit_time = ut_diff_time (&begin, &end);

      if (result_fp)
	{
	  fprintf (result_fp, "cci_end_tran elapsed_time : %.3f \n", commit_time);
	}

      if (res < 0)
	{
	  fprintf (cas_error_fp, "end tran error\nconnection handle id %d\n", con_h);
	}
      PRINT_CCI_ERROR (res, &cci_error, result_fp);
    }

  for (i = 0; i < SERVER_HANDLE_ALLOC_SIZE; i++)
    {
      req_h[i] = 0;
    }

  return 0;
}

static void
print_result (int cci_res, int req_id, FILE * result_fp)
{
  int column_count;
  int res;
  int i;
  int ind;
  char *buffer;
  T_CCI_ERROR cci_error;
  int num_tuple = 0;
  T_CCI_CUBRID_STMT cmd_type;
  char *plan;

  if (result_fp == NULL)
    return;

  fprintf (result_fp, "cci_execute:%d\n", cci_res);

  if (dump_query_plan)
    {
      if (cci_get_query_plan (req_id, &plan) >= 0)
	{
	  fprintf (result_fp, "---------- query plan --------------\n");
	  fprintf (result_fp, "%s\n", (plan ? plan : ""));
	  cci_query_info_free (plan);
	}
    }


  cci_get_result_info (req_id, &cmd_type, &column_count);

/*
  if (cmd_type == CUBRID_STMT_CALL_SP) {
    column_count = cci_get_bind_num(req_id);
    printf("col cnt = %d\n", column_count);
  }
*/

  res = cci_cursor (req_id, 1, CCI_CURSOR_FIRST, &cci_error);
  if (res == CCI_ER_NO_MORE_DATA || column_count <= 0)
    return;
  if (res < 0)
    {
      fprintf (cas_error_fp, "cursor error\nrequest id %d\n", req_id);
      PRINT_CCI_ERROR (res, &cci_error, result_fp);
      return;
    }

  fprintf (result_fp, "---------- query result --------------\n");

  while (1)
    {
      res = cci_fetch (req_id, &cci_error);
      if (res < 0)
	{
	  fprintf (cas_error_fp, "fetch error\nrequest id %d\n", req_id);
	  PRINT_CCI_ERROR (res, &cci_error, result_fp);
	  break;
	}
      for (i = 0; i < column_count; i++)
	{
	  res = cci_get_data (req_id, i + 1, CCI_A_TYPE_STR, &buffer, &ind);
	  if (res < 0)
	    {
	      fprintf (cas_error_fp, "get data error\nrequest id %d\n", req_id);
	      PRINT_CCI_ERROR (res, NULL, result_fp);
	      break;
	    }
	  if (ind < 0 || buffer == NULL)
	    fprintf (result_fp, "<NULL>|");
	  else
	    fprintf (result_fp, "%s|", buffer);
	}
      fprintf (result_fp, "\n");
      num_tuple++;

      if (cmd_type == CUBRID_STMT_CALL_SP)
	{
	  break;
	}
      else
	{
	  res = cci_cursor (req_id, 1, CCI_CURSOR_CURRENT, &cci_error);
	  if (res == CCI_ER_NO_MORE_DATA)
	    break;
	  if (res < 0)
	    {
	      fprintf (cas_error_fp, "cursor error\nrequest id %d\n", req_id);
	      PRINT_CCI_ERROR (res, NULL, result_fp);
	      break;
	    }
	}
    }

  fprintf (result_fp, "-- %d rows ----------------------------\n", num_tuple);
}

static int
make_node_info (T_NODE_INFO * node, char *node_name, char *info_str)
{
  char *p;
  char *str = NULL;
  int i;
  char *token[5];

  memset (node, 0, sizeof (T_NODE_INFO));

  trim (node_name);

  info_str = strdup (info_str);
  if (info_str == NULL)
    {
      fprintf (stderr, "malloc error\n");
      return -1;
    }
  trim (info_str);

  str = info_str;
  token[0] = str;
  for (i = 1; i < 5; i++)
    {
      p = strchr (str, ':');
      if (p == NULL)
	goto err;
      *p = '\0';
      str = p + 1;
      token[i] = str;
    }

  node->node_name = strdup (node_name);
  node->dbname = strdup (token[0]);
  node->ip = strdup (token[1]);
  node->port = atoi (token[2]);
  node->dbuser = strdup (token[3]);
  node->dbpasswd = strdup (token[4]);

  if (node->node_name == NULL || node->dbname == NULL || node->ip == NULL || node->dbuser == NULL
      || node->dbpasswd == NULL)
    {
      goto err;
    }

  FREE_MEM (info_str);
  return 0;

err:
  FREE_MEM (info_str);
  free_node (node);
  fprintf (stderr, "invalid node format (%s)\n", info_str);
  return -1;
}

static void
free_node (T_NODE_INFO * node)
{
  FREE_MEM (node->node_name);
  FREE_MEM (node->dbname);
  FREE_MEM (node->ip);
  FREE_MEM (node->dbuser);
  FREE_MEM (node->dbpasswd);
}

static int
set_args_with_node_info (char *node_name)
{
  int i;
  T_NODE_INFO *node;

  node = NULL;
  for (i = 0; i < num_node; i++)
    {
      if (strcasecmp (node_table[i].node_name, node_name) == 0)
	{
	  node = &node_table[i];
	  break;
	}
    }
  if (node == NULL)
    return -1;

  if (dbname == NULL)
    dbname = node->dbname;
  if (broker_host == NULL)
    broker_host = node->ip;
  if (broker_port == 0)
    broker_port = node->port;
  if (dbuser == NULL)
    dbuser = node->dbuser;
  if (dbpasswd == NULL)
    dbpasswd = node->dbpasswd;
  return 0;
}

static int
ignore_error (int code)
{
  int i;
  for (i = 0; i < num_ign_srv_err; i++)
    {
      if (ign_srv_err_list[i] == code)
	return 1;
    }
  return 0;
}

static char *
make_sql_stmt (char *src)
{
  char *p;
  char *tmp;
  int query_len;
  char *query;

  tmp = (char *) malloc (strlen (src) + 3);
  if (tmp == NULL)
    {
      fprintf (stderr, "malloc error\n");
      return NULL;
    }
  strcpy (tmp, src);
  for (p = tmp; *p; p++)
    {
      if (*p == 1)
	*p = '\n';
    }

  if (cubrid_manager_run)
    {
      query_len = (int) strlen (tmp);
    }
  else
    {
      trim (tmp);
      query_len = strlen (tmp);
      if (query_len > 0)
	{
	  if (tmp[query_len - 1] != ';')
	    {
	      tmp[query_len++] = ';';
	    }
	  tmp[query_len++] = '\n';
	  tmp[query_len] = '\0';
	}
    }

  if (num_replica == 1)
    {
      query = tmp;
    }
  else
    {
      int i;
      int offset = 0;
      query = (char *) malloc ((query_len + 1) * num_replica);
      if (query == NULL)
	{
	  fprintf (stderr, "malloc error\n");
	  FREE_MEM (tmp);
	  return NULL;
	}
      for (i = 0; i < num_replica; i++)
	{
	  strcpy (query + offset, tmp);
	  offset += query_len;
	}
      FREE_MEM (tmp);
    }
  return query;
}
