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
 * broker_monitor.c -
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <time.h>
#include <string.h>
#include <stdarg.h>
#ifdef HAVE_GETOPT_H
#include <getopt.h>
#else
#include "getopt.h"
#endif

#if defined(WINDOWS)
#include <winsock2.h>
#include <windows.h>
#include <conio.h>
#else
#include <curses.h>
#endif

#if defined(WINDOWS)
#include <sys/timeb.h>
#else
#include <sys/types.h>
#include <regex.h>
#include <sys/time.h>
#endif

#include "porting.h"
#include "cas_common.h"
#include "broker_config.h"
#include "broker_shm.h"
#include "broker_util.h"
#include "broker_process_size.h"
#include "porting.h"
#if !defined(WINDOWS)
#include "broker_process_info.h"
#endif
#if defined(CUBRID_SHARD)
#include "cas_util.h"
#include "shard_shm.h"
#include "shard_metadata.h"
#endif /* CUBRID_SHARD */

#define		DEFAULT_CHECK_PERIOD		300	/* seconds */
#define		MAX_APPL_NUM		100

/* structure for appl monitoring */
typedef struct appl_monitoring_item APPL_MONITORING_ITEM;
struct appl_monitoring_item
{
  INT64 num_query_processed;
  INT64 num_long_query;
  INT64 qps;
  INT64 lqs;
};

/* structure for broker monitoring */
typedef struct br_monitoring_item BR_MONITORING_ITEM;
struct br_monitoring_item
{
  INT64 num_tx;
  INT64 num_qx;
  INT64 num_lt;
  INT64 num_lq;
  INT64 num_eq;
  INT64 num_interrupt;
  INT64 tps;
  INT64 qps;
  INT64 lts;
  INT64 lqs;
  INT64 eqs;
  INT64 its;
#if defined(CUBRID_SHARD)
  INT64 num_hnqx;
  INT64 num_hkqx;
  INT64 num_hiqx;
  INT64 num_haqx;
  INT64 hnqps;
  INT64 hkqps;
  INT64 hiqps;
  INT64 haqps;
#endif				/* CUBRID_SHARD */
};

#if defined(CUBRID_SHARD)
typedef struct shard_stat_item SHARD_STAT_ITEM;
struct shard_stat_item
{
  int shard_id;

  INT64 num_hint_key_queries_requested;
  INT64 num_hint_id_queries_requested;
  INT64 num_hint_all_queries_requested;
};

typedef struct key_stat_item KEY_STAT_ITEM;
struct key_stat_item
{
  INT64 num_range_queries_requested[SHARD_KEY_RANGE_MAX];
};
#endif /* CUBRID_SHARD */

static void str_to_screen (const char *msg);
static void print_newline ();
static int get_char (void);
static void print_usage (void);
static int get_args (int argc, char *argv[], char *br_vector);
static void print_job_queue (T_MAX_HEAP_NODE *);
static void ip2str (unsigned char *ip, char *ip_str);
static void time2str (const time_t t, char *str);

static void
appl_info_display (T_SHM_APPL_SERVER * shm_appl,
		   T_APPL_SERVER_INFO * as_info_p, int br_index,
		   int proxy_index, int shard_index, int as_index,
		   APPL_MONITORING_ITEM * appl_mnt_old, time_t current_time,
		   double elapsed_time);
static int appl_monitor (char *br_vector);
static int br_monitor (char *br_vector);
#ifdef GET_PSINFO
static void time_format (int t, char *time_str);
#endif
static void print_header (bool use_pdh_flag);

#if defined(WINDOWS)
static void move (int x, int y);
static void refresh ();
static void clear ();
static void clrtobot ();
static void clrtoeol ();
static void endwin ();
#endif

#if defined(CUBRID_SHARD)
static int metadata_monitor (void);
static int client_monitor (void);
#endif /* CUBRID_SHARD */

static T_SHM_BROKER *shm_br;
static bool display_job_queue = false;
static int refresh_sec = 0;
static bool br_monitor_flag = false;
static int last_access_sec = 0;
static bool tty_mode = false;
static bool full_info_flag = false;
static int state_interval = 1;

static int max_col_len = 0;

#if defined(CUBRID_SHARD)
static bool metadata_monitor_flag = false;
static bool client_monitor_flag = false;
#endif /* CUBRID_SHARD */

#if defined(WINDOWS)
HANDLE h_console;
CONSOLE_SCREEN_BUFFER_INFO scr_info;
#endif
static void
str_to_screen (const char *msg)
{
#ifdef WINDOWS
  DWORD size;
  (void) WriteConsole (h_console, msg, strlen (msg), &size, NULL);
#else
  (void) addstr (msg);
#endif
}

static void
str_out (const char *fmt, ...)
{
  va_list ap;
  char out_buf[1024];

  va_start (ap, fmt);
  if (refresh_sec > 0 && !tty_mode)
    {
      vsprintf (out_buf, fmt, ap);
      str_to_screen (out_buf);
    }
  else
    {
      vprintf (fmt, ap);
    }
  va_end (ap);
}

static void
print_newline ()
{
  if (refresh_sec > 0 && !tty_mode)
    {
      clrtoeol ();
      str_to_screen ("\n");
    }
  else
    {
      printf ("\n");
    }
}

static int
get_char (void)
{
#ifdef WINDOWS
  int i;
  for (i = 0; i < refresh_sec * 10; i++)
    {
      if (shm_br != NULL && shm_br->magic == 0)
	{
	  return 0;
	}

      if (_kbhit ())
	{
	  return _getch ();
	}
      else
	{
	  SLEEP_MILISEC (0, 100);
	}
    }
  return 0;
#else
  return getch ();
#endif
}


int
main (int argc, char **argv)
{
  T_BROKER_INFO br_info[MAX_BROKER_NUM];
  int num_broker, master_shm_id;
  int err, i;
  char *br_vector;
#if defined(WINDOWS)
#else
  WINDOW *win;
#endif

  if (argc == 2 && strcmp (argv[1], "--version") == 0)
    {
      fprintf (stderr, "VERSION %s\n", makestring (BUILD_NUMBER));
      return 1;
    }

  err = broker_config_read (NULL, br_info, &num_broker, &master_shm_id, NULL,
			    0, NULL, NULL, NULL);
  if (err < 0)
    exit (1);

  ut_cd_work_dir ();

  shm_br =
    (T_SHM_BROKER *) uw_shm_open (master_shm_id, SHM_BROKER,
				  SHM_MODE_MONITOR);
  if (shm_br == NULL)
    {
      fprintf (stderr, "master shared memory open error[0x%x]\r\n",
	       master_shm_id);
      exit (1);
    }
  if (shm_br->num_broker < 1 || shm_br->num_broker > MAX_BROKER_NUM)
    {
      fprintf (stderr, "broker configuration error\r\n");
      return 1;
    }

  br_vector = (char *) malloc (shm_br->num_broker);
  if (br_vector == NULL)
    {
      fprintf (stderr, "memory allocation error\r\n");
      return 1;
    }
  for (i = 0; i < shm_br->num_broker; i++)
    {
      br_vector[i] = 0;
    }

  if (get_args (argc, argv, br_vector) < 0)
    {
      free (br_vector);
      return 1;
    }

  if (refresh_sec > 0 && !tty_mode)
    {
#if defined(WINDOWS)
      h_console = GetStdHandle (STD_OUTPUT_HANDLE);
      if (h_console == NULL)
	{
	  refresh_sec = 0;
	}
      if (!GetConsoleScreenBufferInfo (h_console, &scr_info))
	{
	  scr_info.dwSize.X = 80;
	  scr_info.dwSize.Y = 50;
	}
//  FillConsoleOutputCharacter(h_console, ' ', scr_info.dwSize.X * scr_info.dwSize.Y, top_left_pos, &size);
#else
      win = initscr ();
      timeout (refresh_sec * 1000);
      noecho ();
#endif
    }

  while (1)
    {
      if (refresh_sec > 0 && !tty_mode)
	{
	  move (0, 0);
	  refresh ();
	}

      if (shm_br == NULL || shm_br->magic == 0)
	{
	  if (shm_br)
	    {
	      uw_shm_detach (shm_br);
	    }

	  shm_br =
	    (T_SHM_BROKER *) uw_shm_open (master_shm_id, SHM_BROKER,
					  SHM_MODE_MONITOR);
	}
      else
	{
	  if (br_monitor_flag == true)
	    {
	      br_monitor (br_vector);
	    }
#if defined(CUBRID_SHARD)
	  else if (metadata_monitor_flag == true)
	    {
	      metadata_monitor ();
	    }
	  else if (client_monitor_flag == true)
	    {
	      client_monitor ();
	    }
#endif /* CUBRID_SHARD */
	  else
	    {
	      appl_monitor (br_vector);
	    }
	}

      if (refresh_sec > 0 && !tty_mode)
	{
	  int in_ch = 0;

	  refresh ();
	  clrtobot ();
	  move (0, 0);
	  refresh ();
	  in_ch = get_char ();

#if defined (WINDOWS)
	  if (shm_br != NULL && shm_br->magic == 0)
	    {
	      uw_shm_detach (shm_br);
	      shm_br = NULL;
	    }
#endif
	  if (in_ch == 'q')
	    {
	      break;
	    }
	  else if (in_ch == '' || in_ch == '\r' || in_ch == '\n' || in_ch == ' ')
	    {
	      clear ();
	      refresh ();
	    }
	}
      else if (refresh_sec > 0)
	{
	  for (i = 0; i < 10; i++)
	    {
#if defined (WINDOWS)
	      if (shm_br != NULL && shm_br->magic == 0)
		{
		  uw_shm_detach (shm_br);
		  shm_br = NULL;
		}
#endif
	      SLEEP_MILISEC (0, refresh_sec * 100);
	    }
	  fflush (stdout);
	}
      else
	{
	  break;
	}
    }				/* end of while(1) */

  if (shm_br != NULL)
    {
      uw_shm_detach (shm_br);
    }

  if (refresh_sec > 0 && !tty_mode)
    {
      endwin ();
    }

  exit (0);
}

static void
print_usage (void)
{
#if defined(CUBRID_SHARD)
  printf
    ("shard_broker_monitor [-b] [-t] [-s <sec>] [-m] [-c] [-f] [<expr>]\n");
  printf ("\t-m display metadata information\n");
  printf ("\t-c display client information\n");
#else
  printf ("broker_monitor [-b] [-q] [-t] [-s <sec>] [-f] [<expr>]\n");
  printf ("\t-q display job queue\n");
#endif /* CUBRID_SHARD */
  printf ("\t-b brief mode (show broker info)\n");
  printf ("\t-s refresh time in sec\n");
  printf ("\t-f full info\n");
}

static int
get_args (int argc, char *argv[], char *br_vector)
{
  int c, j;
  int status;
  bool br_name_opt_flag = false;
#if !defined(WINDOWS)
  regex_t re;
#endif

#if defined(CUBRID_SHARD)
  char optchars[] = "hbqts:l:fmc";
#else
  char optchars[] = "hbqts:l:f";
#endif

  display_job_queue = false;
  refresh_sec = 0;
  br_monitor_flag = false;
  last_access_sec = 0;
  full_info_flag = false;
  state_interval = 1;
  while ((c = getopt (argc, argv, optchars)) != EOF)
    {
      switch (c)
	{
	case 't':
	  tty_mode = true;
	  break;
	case 'q':
#if defined(CUBRID_SHARD)
	  printf ("not support -q option yet.\n");
	  return -1;
#else
	  display_job_queue = true;
	  break;
#endif /* CUBRID_SHARD */
	case 's':
	  refresh_sec = atoi (optarg);
	  break;
	case 'b':
	  br_monitor_flag = true;
	  break;
	case 'l':
	  state_interval = last_access_sec = atoi (optarg);
	  if (state_interval < 1)
	    {
	      state_interval = 1;
	    }
	  break;
	case 'f':
	  full_info_flag = true;
	  break;
#if defined(CUBRID_SHARD)
	case 'm':
	  metadata_monitor_flag = true;
	  break;
	case 'c':
	  client_monitor_flag = true;
	  break;
#endif /* CUBRID_SHARD */
	case 'h':
	case '?':
	  print_usage ();
	  return -1;
	}
    }

  for (; optind < argc; optind++)
    {
      br_name_opt_flag = true;
#if !defined(WINDOWS)
      if (regcomp (&re, argv[optind], 0) != 0)
	{
	  fprintf (stderr, "%s\r\n", argv[optind]);
	  return -1;
	}
#endif
      for (j = 0; j < shm_br->num_broker; j++)
	{
#if defined(WINDOWS)
	  status =
	    (strstr (shm_br->br_info[j].name, argv[optind]) != NULL) ? 0 : 1;
#else
	  status = regexec (&re, shm_br->br_info[j].name, 0, NULL, 0);
#endif
	  if (status == 0)
	    {
	      br_vector[j] = 1;
	    }
	}
#if !defined(WINDOWS)
      regfree (&re);
#endif
    }

  if (br_name_opt_flag == false)
    {
      for (j = 0; j < shm_br->num_broker; j++)
	br_vector[j] = 1;
    }

  return 0;
}

static void
print_job_queue (T_MAX_HEAP_NODE * job_queue)
{
  T_MAX_HEAP_NODE item;
  char first_flag = 1;
  char outbuf[1024];

  while (1)
    {
      char ip_str[64];
      char time_str[64];

      if (max_heap_delete (job_queue, &item) < 0)
	break;

      if (first_flag)
	{
	  sprintf (outbuf, "%5s  %s%9s%13s%13s", "ID", "PRIORITY", "IP",
		   "TIME", "REQUEST");
	  str_out ("%s", outbuf);
	  print_newline ();
	  first_flag = 0;
	}

      ip2str (item.ip_addr, ip_str);
      time2str (item.recv_time, time_str);
      sprintf (outbuf, "%5d%7d%17s%10s   %s:%s",
	       item.id, item.priority, ip_str, time_str, item.script,
	       item.prg_name);
      str_out ("%s", outbuf);
      print_newline ();
    }
  if (!first_flag)
    print_newline ();
}

static void
ip2str (unsigned char *ip, char *ip_str)
{
  sprintf (ip_str, "%d.%d.%d.%d", (unsigned char) ip[0],
	   (unsigned char) ip[1],
	   (unsigned char) ip[2], (unsigned char) ip[3]);
}

static void
time2str (const time_t t, char *str)
{
  struct tm s_tm;

#if defined (WINDOWS)
  if (localtime_s (&s_tm, &t) != 0)
#else /* !WINDOWS */
  if (localtime_r (&t, &s_tm) == NULL)
#endif /* !WINDOWS */
    {
      *str = '\0';
      return;
    }
  sprintf (str, "%02d:%02d:%02d", s_tm.tm_hour, s_tm.tm_min, s_tm.tm_sec);
}

static void
appl_info_display (T_SHM_APPL_SERVER * shm_appl,
		   T_APPL_SERVER_INFO * as_info_p, int br_index,
		   int proxy_index, int shard_index, int as_index,
		   APPL_MONITORING_ITEM * appl_mnt_old, time_t current_time,
		   double elapsed_time)
{
  struct tm cur_tm;
  INT64 qps, lqs;
  int col_len;
  char line_buf[1024], ip_str[16];
  time_t last_access_time, last_connect_time, tran_start_time;

  if (as_info_p->service_flag != SERVICE_ON)
    {
      return;
    }

  if (last_access_sec > 0)
    {
      if (as_info_p->uts_status != UTS_STATUS_BUSY
	  || current_time - as_info_p->last_access_time < last_access_sec)
	{
	  return;
	}
    }
  col_len = 0;
#if defined(CUBRID_SHARD)
  col_len += sprintf (line_buf + col_len, "%8d ", proxy_index + 1);
  col_len += sprintf (line_buf + col_len, "%8d ", shard_index);
  col_len += sprintf (line_buf + col_len, "%8d ", as_index + 1);
#else
  col_len += sprintf (line_buf + col_len, "%2d ", as_index + 1);
#endif /* CUBRID_SHARD */
  col_len += sprintf (line_buf + col_len, "%5d ", as_info_p->pid);

  if (elapsed_time > 0)
    {
      qps = (as_info_p->num_queries_processed -
	     appl_mnt_old->num_query_processed) / elapsed_time;
      lqs = (as_info_p->num_long_queries -
	     appl_mnt_old->num_long_query) / elapsed_time;

      appl_mnt_old->num_query_processed = as_info_p->num_queries_processed;

      appl_mnt_old->num_long_query = as_info_p->num_long_queries;

      appl_mnt_old->qps = qps;
      appl_mnt_old->lqs = lqs;
    }
  else
    {
      qps = appl_mnt_old->qps;
      lqs = appl_mnt_old->lqs;
    }

  col_len += sprintf (line_buf + col_len, "%5ld ", qps);
  col_len += sprintf (line_buf + col_len, "%5ld ", lqs);

#if defined(WINDOWS)
  col_len += sprintf (line_buf + col_len, "%5d ", as_info_p->as_port);
#endif

#if defined(WINDOWS)
  if (shm_appl->use_pdh_flag == TRUE)
    {
      col_len += sprintf (line_buf + col_len, "%5d ", as_info_p->pdh_workset);
    }
#else
  col_len += sprintf (line_buf + col_len, "%5d ", getsize (as_info_p->pid));
#endif
  if (as_info_p->uts_status == UTS_STATUS_BUSY)
    {
      if (IS_APPL_SERVER_TYPE_CAS (shm_br->br_info[br_index].appl_server))
	{
	  if (as_info_p->con_status == CON_STATUS_OUT_TRAN)
	    {
	      col_len += sprintf (line_buf + col_len, "%-12s ", "CLOSE WAIT");
	    }
	  else if (as_info_p->log_msg[0] == '\0')
	    {
	      col_len +=
		sprintf (line_buf + col_len, "%-12s ", "CLIENT WAIT");
	    }
	  else
	    {
	      col_len += sprintf (line_buf + col_len, "%-12s ", "BUSY");
	    }
	}
      else
	{
	  col_len += sprintf (line_buf + col_len, "%-12s ", "BUSY");
	}
    }
#if defined(WINDOWS)
  else if (as_info_p->uts_status == UTS_STATUS_BUSY_WAIT)
    {
      col_len += sprintf (line_buf + col_len, "%-12s ", "BUSY");
    }
#endif
  else if (as_info_p->uts_status == UTS_STATUS_RESTART)
    {
      col_len += sprintf (line_buf + col_len, "%-12s ", "INITIALIZE");
    }
#if defined(CUBRID_SHARD)
  else if (as_info_p->uts_status == UTS_STATUS_CON_WAIT)
    {
      col_len += sprintf (line_buf + col_len, "%-12s ", "CON WAIT");
    }
#endif
  else
    {
      col_len += sprintf (line_buf + col_len, "%-12s ", "IDLE");
    }

#ifdef GET_PSINFO
  get_psinfo (as_info_p->pid, &proc_info);
  col_len += sprintf (line_buf + col_len, "%5.2f", proc_info.pcpu);
  time_format (proc_info.cpu_time, time_str);
  col_len += sprintf (line_buf + col_len, "%7s ", time_str);
#elif WINDOWS
  if (shm_appl->use_pdh_flag == TRUE)
    {
      float pct_cpu;
      pct_cpu = as_info_p->pdh_pct_cpu;
      if (pct_cpu >= 0)
	{
	  col_len += sprintf (line_buf + col_len, "%5.2f ", pct_cpu);
	}
      else
	{
	  col_len += sprintf (line_buf + col_len, "%5s ", " - ");
	}
    }
#endif

  if (full_info_flag)
    {
      char sql_log_mode_string[16];

      last_access_time = as_info_p->last_access_time;
      localtime_r (&last_access_time, &cur_tm);
      cur_tm.tm_year += 1900;

      col_len += sprintf (line_buf + col_len,
			  "%02d/%02d/%02d %02d:%02d:%02d ",
			  cur_tm.tm_year, cur_tm.tm_mon + 1,
			  cur_tm.tm_mday, cur_tm.tm_hour,
			  cur_tm.tm_min, cur_tm.tm_sec);

      if (as_info_p->database_name[0] != '\0')
	{
	  col_len +=
	    sprintf (line_buf + col_len, "%16.16s ",
		     as_info_p->database_name);
	  col_len +=
	    sprintf (line_buf + col_len, "%16.16s ",
		     as_info_p->database_host);
	  last_connect_time = as_info_p->last_connect_time;
	  localtime_r (&last_connect_time, &cur_tm);
	  cur_tm.tm_year += 1900;

	  col_len += sprintf (line_buf + col_len,
			      "%02d/%02d/%02d %02d:%02d:%02d ",
			      cur_tm.tm_year,
			      cur_tm.tm_mon + 1,
			      cur_tm.tm_mday,
			      cur_tm.tm_hour, cur_tm.tm_min, cur_tm.tm_sec);
	}
      else
	{
	  col_len +=
	    sprintf (line_buf + col_len, "%16c %16c %19c ", '-', '-', '-');
	}

#if !defined(CUBRID_SHARD)
      col_len +=
	sprintf (line_buf + col_len, "%15.15s ",
		 ut_get_ipv4_string (ip_str, sizeof (ip_str),
				     as_info_p->cas_clt_ip));
#endif /* !CUBRID_SHARD */

      strncpy (sql_log_mode_string, "-", sizeof (sql_log_mode_string));

      if (as_info_p->cur_sql_log_mode != shm_appl->sql_log_mode)
	{
	  if (as_info_p->cur_sql_log_mode == SQL_LOG_MODE_NONE)
	    {
	      strncpy (sql_log_mode_string, "NONE",
		       sizeof (sql_log_mode_string));
	    }
	  else if (as_info_p->cur_sql_log_mode == SQL_LOG_MODE_ERROR)
	    {
	      strncpy (sql_log_mode_string, "ERROR",
		       sizeof (sql_log_mode_string));
	    }
	  else if (as_info_p->cur_sql_log_mode == SQL_LOG_MODE_TIMEOUT)
	    {
	      strncpy (sql_log_mode_string, "TIMEOUT",
		       sizeof (sql_log_mode_string));
	    }
	  else if (as_info_p->cur_sql_log_mode == SQL_LOG_MODE_NOTICE)
	    {
	      strncpy (sql_log_mode_string, "NOTICE",
		       sizeof (sql_log_mode_string));
	    }
	  else if (as_info_p->cur_sql_log_mode == SQL_LOG_MODE_ALL)
	    {
	      strncpy (sql_log_mode_string, "ALL",
		       sizeof (sql_log_mode_string));
	    }
	}

      col_len +=
	sprintf (line_buf + col_len, "%15.15s ", sql_log_mode_string);

#if !defined(CUBRID_SHARD)
      tran_start_time = as_info_p->transaction_start_time;
      if (tran_start_time != (time_t) 0)
	{
	  localtime_r (&tran_start_time, &cur_tm);
	  cur_tm.tm_year += 1900;

	  col_len +=
	    sprintf (line_buf + col_len,
		     "%02d/%02d/%02d %02d:%02d:%02d ",
		     cur_tm.tm_year, cur_tm.tm_mon + 1,
		     cur_tm.tm_mday, cur_tm.tm_hour,
		     cur_tm.tm_min, cur_tm.tm_sec);
	}
      else
	{
	  col_len += sprintf (line_buf + col_len, "%19c ", '-');
	}
      col_len +=
	sprintf (line_buf + col_len, "%9d ",
		 (int) as_info_p->num_connect_requests);
      col_len +=
	sprintf (line_buf + col_len, "%9d ", (int) as_info_p->num_restarts);
#endif /* !CUBRID_SHARD */
    }

  if (col_len >= max_col_len)
    {
      max_col_len = col_len;
    }
  else
    {
      sprintf (line_buf + col_len, "%*c", max_col_len - col_len, ' ');
    }
  str_out ("%s", line_buf);
  print_newline ();
  if (as_info_p->uts_status == UTS_STATUS_BUSY)
    {
      sprintf (line_buf, "SQL: %s", as_info_p->log_msg);
      str_out ("%s", line_buf);
      print_newline ();
    }
}

static int
appl_monitor (char *br_vector)
{
  struct tm cur_tm;
  time_t last_access_time, last_connect_time, tran_start_time;
  T_MAX_HEAP_NODE job_queue[JOB_QUEUE_MAX_SIZE + 1];
  T_SHM_APPL_SERVER *shm_appl;
  int i, j, k, appl_offset;
  int col_len;
  char line_buf[1024], ip_str[16];
  static time_t time_old;

  static APPL_MONITORING_ITEM *appl_mnt_olds = NULL;

  time_t current_time, time_cur;
  double elapsed_time = 0;
#ifdef GET_PSINFO
  T_PSINFO proc_info;
  char time_str[32];
#endif

#if defined(CUBRID_SHARD)
  char *shm_as_cp = NULL;

  T_SHM_PROXY *shm_proxy_p = NULL;
  T_PROXY_INFO *proxy_info_p = NULL;
  T_SHARD_INFO *shard_info_p = NULL;
  int proxy_index, shard_index, cas_index;
  static int *tot_appl_cnt = NULL;
#endif /* CUBRID_SHARD */

  if (appl_mnt_olds == NULL)
    {
      int n = 0;
#if defined(CUBRID_SHARD)
      if (tot_appl_cnt == NULL)
	{
	  tot_appl_cnt = (int *) malloc (sizeof (int *) * shm_br->num_broker);
	  if (tot_appl_cnt == NULL)
	    {
	      return -1;
	    }
	}
      for (i = 0; i < shm_br->num_broker; i++)
	{
	  if (shm_br->br_info[i].service_flag == ON)
	    {
	      shm_as_cp =
		(char *) uw_shm_open (shm_br->br_info[i].appl_server_shm_id,
				      SHM_APPL_SERVER, SHM_MODE_MONITOR);
	      if (shm_as_cp == NULL)
		{
		  str_out ("%s [%d : 0x%x]", "shared memory open error", i,
			   shm_br->br_info[i].appl_server_shm_id);
		  return -1;
		}
	      shm_proxy_p = shard_shm_get_proxy (shm_as_cp);
	      if (shm_proxy_p == NULL)
		{
		  str_out ("%s [%d]", "proxy shared memory open error", i);
		  uw_shm_detach (shm_as_cp);
		  return -1;
		}
	      proxy_info_p = shard_shm_get_first_proxy_info (shm_proxy_p);
	      if (proxy_info_p == NULL)
		{
		  str_out ("%s [%d]", "proxy info shared memory open error",
			   i);
		  uw_shm_detach (shm_as_cp);
		  return -1;
		}
	      shard_info_p = shard_shm_get_first_shard_info (proxy_info_p);
	      if (shard_info_p == NULL)
		{
		  str_out ("%s [%d]", "shard shared memory open error", i);
		  uw_shm_detach (shm_as_cp);
		  return -1;
		}

	      tot_appl_cnt[i] =
		(shm_proxy_p->max_num_proxy * proxy_info_p->max_shard *
		 shard_info_p->max_appl_server);

	      n += tot_appl_cnt[i];
	      uw_shm_detach (shm_as_cp);
	    }
	  else
	    {
	      tot_appl_cnt[i] = 0;
	    }
	}
#else
      for (i = 0; i < shm_br->num_broker; i++)
	{
	  n += shm_br->br_info[i].appl_server_max_num;
	}
#endif /* CUBRID_SHARD */
      appl_mnt_olds =
	(APPL_MONITORING_ITEM *) calloc (sizeof (APPL_MONITORING_ITEM), n);
      if (appl_mnt_olds == NULL)
	{
	  return -1;
	}
      memset ((char *) appl_mnt_olds, 0, sizeof (APPL_MONITORING_ITEM) * n);
      (void) time (&time_old);
      time_old--;
    }

  (void) time (&time_cur);

  elapsed_time = difftime (time_cur, time_old);

  for (i = 0; i < shm_br->num_broker; i++)
    {

      if (br_vector[i] == 0)
	continue;

      str_out ("%% %s ", shm_br->br_info[i].name);

      if (shm_br->br_info[i].service_flag == ON)
	{
#if defined(CUBRID_SHARD)
	  shm_as_cp =
	    (char *) uw_shm_open (shm_br->br_info[i].appl_server_shm_id,
				  SHM_APPL_SERVER, SHM_MODE_MONITOR);
	  shm_appl = shard_shm_get_appl_server (shm_as_cp);
	  if (shm_as_cp == NULL || shm_appl == NULL)
#else
	  shm_appl = (T_SHM_APPL_SERVER *)
	    uw_shm_open (shm_br->br_info[i].appl_server_shm_id,
			 SHM_APPL_SERVER, SHM_MODE_MONITOR);
	  if (shm_appl == NULL)
#endif /* CUBRID_SHARD */
	    {
	      str_out ("%s", "shared memory open error");
	      print_newline ();
	    }
	  else
	    {
	      if (shm_appl->suspend_mode != SUSPEND_NONE)
		{
		  str_out ("%s", " SUSPENDED");
		  print_newline ();
		  str_out ("%s", "  ");
		}
	      str_out (" - %s ", shm_appl->appl_server_name);
	      str_out ("[%d,", shm_br->br_info[i].pid);
	      str_out ("%d] ", shm_br->br_info[i].port);
#if !defined(CUBRID_SHARD)
	      str_out ("%s ", shm_br->br_info[i].access_log_file);
#endif /* CUBRID_SHARD */
	      str_out ("%s ", shm_br->br_info[i].error_log_file);
	      print_newline ();

	      if (display_job_queue == true)
		{
		  memcpy (job_queue, shm_appl->job_queue,
			  sizeof (T_MAX_HEAP_NODE) * (JOB_QUEUE_MAX_SIZE +
						      1));
		  str_out (" JOB QUEUE:%d", job_queue[0].id);
		}
	      else
		{
		  str_out (" JOB QUEUE:%d", shm_appl->job_queue[0].id);
		}

	      if (shm_br->br_info[i].auto_add_appl_server == ON)
		{
		  str_out (", AUTO_ADD_APPL_SERVER:%s", "ON");
		}
	      else
		{
		  str_out (", AUTO_ADD_APPL_SERVER:%s", "OFF");
		}

	      if (shm_appl->sql_log_mode == SQL_LOG_MODE_NONE)
		{
		  str_out (", SQL_LOG_MODE:%s:%d", "NONE",
			   shm_appl->sql_log_max_size);
		}
	      else if (shm_appl->sql_log_mode == SQL_LOG_MODE_ERROR)
		{
		  str_out (", SQL_LOG_MODE:%s:%d", "ERROR",
			   shm_appl->sql_log_max_size);
		}
	      else if (shm_appl->sql_log_mode == SQL_LOG_MODE_TIMEOUT)
		{
		  str_out (", SQL_LOG_MODE:%s:%d", "TIMEOUT",
			   shm_appl->sql_log_max_size);
		}
	      else if (shm_appl->sql_log_mode == SQL_LOG_MODE_NOTICE)
		{
		  str_out (", SQL_LOG_MODE:%s:%d", "NOTICE",
			   shm_appl->sql_log_max_size);
		}
	      else if (shm_appl->sql_log_mode == SQL_LOG_MODE_ALL)
		{
		  str_out (", SQL_LOG_MODE:%s:%d", "ALL",
			   shm_appl->sql_log_max_size);
		}

	      if (shm_br->br_info[i].slow_log_mode == SLOW_LOG_MODE_ON)
		{
		  str_out (", SLOW_LOG:%s", "ON");
		}
	      else
		{
		  str_out (", SLOW_LOG:%s", "OFF");
		}

	      print_newline ();

	      str_out (" LONG_TRANSACTION_TIME:%.2f",
		       (shm_appl->long_transaction_time / 1000.0));
	      str_out (", LONG_QUERY_TIME:%.2f",
		       (shm_appl->long_query_time / 1000.0));

	      if (IS_APPL_SERVER_TYPE_CAS (shm_br->br_info[i].appl_server))
		{
		  str_out (", SESSION_TIMEOUT:%d",
			   shm_br->br_info[i].session_timeout);
		}
	      print_newline ();

	      if (shm_appl->keep_connection == KEEP_CON_OFF)
		{
		  str_out (" KEEP_CONNECTION:%s", "OFF");
		}
	      else if (shm_appl->keep_connection == KEEP_CON_ON)
		{
		  str_out (" KEEP_CONNECTION:%s", "ON");
		}
	      else if (shm_appl->keep_connection == KEEP_CON_AUTO)
		{
		  str_out (" KEEP_CONNECTION:%s", "AUTO");
		}

	      if (shm_appl->access_mode == READ_WRITE_ACCESS_MODE)
		{
		  str_out (", ACCESS_MODE:%s", "RW");
		}
	      else if (shm_appl->access_mode == READ_ONLY_ACCESS_MODE)
		{
		  str_out (", ACCESS_MODE:%s", "RO");
		}
	      else if (shm_appl->access_mode == SLAVE_ONLY_ACCESS_MODE)
		{
		  str_out (", ACCESS_MODE:%s", "SO");
		}
	      str_out (", MAX_QUERY_TIMEOUT:%d", shm_appl->query_timeout);
	      print_newline ();

#if defined (WINDOWS)
	      print_header (shm_appl->use_pdh_flag);
#else
	      print_header (false);
#endif
	      current_time = time (NULL);

	      /* CAS INFORMATION DISPLAY */
	      appl_offset = 0;
#if defined(CUBRID_SHARD)
	      for (k = 0; k < i; k++)
		{
		  appl_offset += tot_appl_cnt[k];
		}

	      shm_proxy_p = shard_shm_get_proxy (shm_as_cp);
	      if (shm_proxy_p == NULL)
		{
		  str_out ("%s", "shared memory open error");
		  print_newline ();
		  continue;
		}

	      for (proxy_index = 0, proxy_info_p =
		   shard_shm_get_first_proxy_info (shm_proxy_p); proxy_info_p;
		   proxy_index++, proxy_info_p =
		   shard_shm_get_next_proxy_info (proxy_info_p))
		{
		  for (shard_index = 0, shard_info_p =
		       shard_shm_get_first_shard_info (proxy_info_p);
		       shard_info_p;
		       shard_index++, shard_info_p =
		       shard_shm_get_next_shard_info (shard_info_p))
		    {
		      /* j == cas_index */
		      for (cas_index = 0;
			   cas_index < shard_info_p->num_appl_server;
			   cas_index++, appl_offset++)
			{
			  appl_info_display (shm_appl,
					     &(shard_info_p->
					       as_info[cas_index]), i,
					     proxy_index, shard_index,
					     cas_index,
					     &(appl_mnt_olds[appl_offset]),
					     current_time, elapsed_time);
			}
		    }
		}
#else
	      for (k = 0; k < i; k++)
		{
		  appl_offset += shm_br->br_info[k].appl_server_max_num;
		}
	      for (j = 0; j < shm_br->br_info[i].appl_server_max_num; j++)
		{
		  appl_info_display (shm_appl, &(shm_appl->as_info[j]), i, -1,
				     -1, j, &(appl_mnt_olds[appl_offset + j]),
				     current_time, elapsed_time);
		}		/* CAS INFORMATION DISPLAY */
#endif /* CUBIRD_SHARD */
	      print_newline ();

	      if (display_job_queue == true)
		print_job_queue (job_queue);

#if defined(CUBRID_SHARD)
	      uw_shm_detach (shm_as_cp);
#else
	      uw_shm_detach (shm_appl);
#endif /* CUBRID_SHARD */
	    }
	}

      else
	{			/* service_flag == OFF */
	  str_out ("%s", "OFF");
	  print_newline ();
	  print_newline ();
	}
    }

  if (elapsed_time > 0)
    {
      time_old = time_cur;
    }

  return 0;
}

static int
br_monitor (char *br_vector)
{
  T_SHM_APPL_SERVER *shm_appl;
  int i, j, num_req;
  char buf[1024];
  int buf_len;
#ifdef GET_PSINFO
  T_PSINFO proc_info;
  char time_str[32];
#endif
  static BR_MONITORING_ITEM *br_mnt_olds = NULL;
  static time_t time_old;
  time_t time_cur;
  INT64 num_tx_cur = 0, num_qx_cur = 0, num_interrupts_cur = 0;
  INT64 num_lt_cur = 0, num_lq_cur = 0, num_eq_cur = 0;
  INT64 tps = 0, qps = 0, lts = 0, lqs = 0, eqs = 0, its = 0;
  static unsigned int tty_print_header = 0;
  double elapsed_time;
#if defined(CUBRID_SHARD)
  char *shm_as_cp = NULL;

  T_SHM_PROXY *shm_proxy_p = NULL;
  T_PROXY_INFO *proxy_info_p = NULL;
  T_SHARD_INFO *shard_info_p = NULL;

  INT64 num_hnqx_cur = 0, num_hkqx_cur = 0, num_hiqx_cur = 0, num_haqx_cur =
    0;
  INT64 hnqps = 0, hkqps = 0, hiqps = 0, haqps = 0;
#endif /* CUBRID_SHARD */

  T_APPL_SERVER_INFO *as_info_p = NULL;

  buf_len = 0;
  buf_len += sprintf (buf + buf_len, "  %-12s", "NAME");
  buf_len += sprintf (buf + buf_len, "%6s", "PID");
  if (full_info_flag)
    {
      buf_len += sprintf (buf + buf_len, "%7s", "PSIZE");
    }
  buf_len += sprintf (buf + buf_len, "%6s", "PORT");
#if defined(CUBRID_SHARD)
  buf_len += sprintf (buf + buf_len, "%10s", "Active-P");
  buf_len += sprintf (buf + buf_len, "%10s", "Active-C");
#else
  if (full_info_flag)
    {
      buf_len +=
	sprintf (buf + buf_len, "  AS(T   W   B %2ds-W %2ds-B)",
		 state_interval, state_interval);
    }
  else
    {
      buf_len += sprintf (buf + buf_len, "%4s", "AS");
    }

  buf_len += sprintf (buf + buf_len, "%4s", "JQ");
#endif /* CUBRID_SHARD */

#ifdef GET_PSINFO
  buf_len += sprintf (buf + buf_len, "%4s", "THR");
  buf_len += sprintf (buf + buf_len, "%6s", "CPU");
  buf_len += sprintf (buf + buf_len, "%6s", "TIME");
#endif
  buf_len += sprintf (buf + buf_len, "%9s", "REQ");
  buf_len += sprintf (buf + buf_len, "%5s", "TPS");
  buf_len += sprintf (buf + buf_len, "%5s", "QPS");
#if defined(CUBRID_SHARD)
  buf_len += sprintf (buf + buf_len, "%7s", "K-QPS");
  if (full_info_flag)
    {
      buf_len += sprintf (buf + buf_len, "%7s", "(H-KEY");
      buf_len += sprintf (buf + buf_len, "%7s", "H-ID");
      buf_len += sprintf (buf + buf_len, "%7s", "H-ALL)");
    }
  buf_len += sprintf (buf + buf_len, "%7s", "NK-QPS");
#endif /* CUBRID_SHARD */

  buf_len += sprintf (buf + buf_len, "%10s", "LONG-T");
  buf_len += sprintf (buf + buf_len, "%10s", "LONG-Q");
  buf_len += sprintf (buf + buf_len, "%7s", "ERR-Q");

  if (full_info_flag)
    {
      buf_len += sprintf (buf + buf_len, "%10s", "CANCELED");
      buf_len += sprintf (buf + buf_len, "%13s", "ACCESS_MODE");
      buf_len += sprintf (buf + buf_len, "%9s", "SQL_LOG");
    }

  if (tty_mode == false || (tty_print_header++ % 20 == 0))
    {
      str_out ("%s", buf);
      print_newline ();
      for (i = strlen (buf); i > 0; i--)
	str_out ("%s", "=");
      print_newline ();
    }

  if (br_mnt_olds == NULL)
    {
      br_mnt_olds =
	(BR_MONITORING_ITEM *) calloc (sizeof (BR_MONITORING_ITEM),
				       shm_br->num_broker);
      if (br_mnt_olds == NULL)
	{
	  return -1;
	}
      (void) time (&time_old);
      time_old--;
    }

  (void) time (&time_cur);

  elapsed_time = difftime (time_cur, time_old);

  for (i = 0; i < shm_br->num_broker; i++)
    {
      int num_client_wait, num_busy, num_client_wait_nsec, num_busy_nsec;
#if defined(CUBRID_SHARD)
      int proxy_index, shard_index, cas_index, tot_proxy, tot_cas;
#endif /* CUBRID_SHARD */
      time_t cur_time;

      if (br_vector[i] == 0)
	continue;

      str_out ("* %-12s", shm_br->br_info[i].name);

      if (shm_br->br_info[i].service_flag == ON)
	{
#if defined(CUBRID_SHARD)
	  shm_as_cp =
	    (char *) uw_shm_open (shm_br->br_info[i].appl_server_shm_id,
				  SHM_APPL_SERVER, SHM_MODE_MONITOR);
	  shm_appl = shard_shm_get_appl_server (shm_as_cp);
	  if (shm_as_cp == NULL || shm_appl == NULL)
#else
	  shm_appl = (T_SHM_APPL_SERVER *)
	    uw_shm_open (shm_br->br_info[i].appl_server_shm_id,
			 SHM_APPL_SERVER, SHM_MODE_MONITOR);

	  if (shm_appl == NULL)
#endif /* CUBRID_SHARD */
	    {
	      str_out ("%s", "shared memory open error");
	      print_newline ();
	    }
	  else
	    {
	      num_req = 0;
	      num_client_wait = 0;
	      num_client_wait_nsec = 0;
	      num_busy = 0;
	      num_busy_nsec = 0;

#if !defined(CUBRID_SHARD)
	      cur_time = time (NULL);

	      for (j = 0; j < shm_br->br_info[i].appl_server_max_num; j++)
		{
		  as_info_p = &(shm_appl->as_info[j]);
		  num_req += as_info_p->num_request;

		  if (full_info_flag)
		    {
		      bool time_expired =
			(cur_time - as_info_p->last_access_time >=
			 state_interval);

		      if (as_info_p->uts_status == UTS_STATUS_BUSY
			  && as_info_p->con_status != CON_STATUS_OUT_TRAN
			  && as_info_p->con_status != CON_STATUS_OUT_TRAN_HOLDABLE)
			{
			  if (as_info_p->log_msg[0] == '\0')
			    {
			      num_client_wait++;
			      if (time_expired)
				{
				  num_client_wait_nsec++;
				}
			    }
			  else
			    {
			      num_busy++;
			      if (time_expired)
				{
				  num_busy_nsec++;
				}
			    }
			}
#if defined(WIDOWS)
		      else if (as_info_p->uts_status == UTS_STATUS_BUSY_WAIT)
			{
			  num_busy++;
			  if (time_expired)
			    {
			      num_busy_nsec++;
			    }
			}
#endif
		    }
		}
#endif /* !CUBRID_SHARD */

	      str_out ("%6d", shm_br->br_info[i].pid);

	      if (full_info_flag)
		{
#if defined(WINDOWS)
		  if (shm_appl->use_pdh_flag == TRUE)
		    {
		      str_out ("%7d ", shm_br->br_info[i].pdh_workset);
		    }
#else
		  str_out ("%7d", getsize (shm_br->br_info[i].pid));
#endif
		}

	      str_out ("%6d", shm_br->br_info[i].port);
#if !defined(CUBRID_SHARD)
	      if (full_info_flag)
		{
		  str_out ("   %3d %3d %3d  %4d  %4d ",
			   shm_br->br_info[i].appl_server_num,
			   num_client_wait, num_busy, num_client_wait_nsec,
			   num_busy_nsec);
		}
	      else
		{
		  str_out ("%4d", shm_br->br_info[i].appl_server_num);
		}

	      str_out ("%4d", shm_appl->job_queue[0].id);

#ifdef GET_PSINFO
	      get_psinfo (shm_br->br_info[i].pid, &proc_info);
	      str_out ("%4d", proc_info.num_thr);
	      str_out ("%6.2f", proc_info.pcpu);
	      time_format (proc_info.cpu_time, time_str);
	      str_out ("%6s", time_str);
#endif

	      str_out (" %8d", num_req);
#endif /* !CUBRID_SHARD */

#if defined(CUBRID_SHARD)
	      shm_proxy_p = shard_shm_get_proxy (shm_as_cp);
	      if (shm_proxy_p == NULL)
		{
		  str_out ("%s", "shared memory open error");
		  print_newline ();
		  continue;
		}
	      num_hnqx_cur = 0;
	      num_hkqx_cur = 0;
	      num_hiqx_cur = 0;
	      num_haqx_cur = 0;
#endif
	      num_tx_cur = 0;
	      num_qx_cur = 0;
	      num_lt_cur = 0;
	      num_lq_cur = 0;
	      num_eq_cur = 0;
	      num_interrupts_cur = 0;

#if defined(CUBRID_SHARD)
	      tot_cas = tot_proxy = 0;
	      for (proxy_index = 0, proxy_info_p =
		   shard_shm_get_first_proxy_info (shm_proxy_p); proxy_info_p;
		   proxy_index++, proxy_info_p =
		   shard_shm_get_next_proxy_info (proxy_info_p))
		{
		  for (shard_index = 0, shard_info_p =
		       shard_shm_get_first_shard_info (proxy_info_p);
		       shard_info_p;
		       shard_index++, shard_info_p =
		       shard_shm_get_next_shard_info (shard_info_p))
		    {
		      for (cas_index = 0;
			   cas_index < shard_info_p->num_appl_server;
			   cas_index++)
			{
			  as_info_p = &(shard_info_p->as_info[cas_index]);
			  num_req += as_info_p->num_requests_received;
			  num_tx_cur += as_info_p->num_transactions_processed;
			  num_qx_cur += as_info_p->num_queries_processed;
			  num_lt_cur += as_info_p->num_long_transactions;
			  num_lq_cur += as_info_p->num_long_queries;
			  num_eq_cur += as_info_p->num_error_queries;
			  num_interrupts_cur += as_info_p->num_interrupts;

			}
		      tot_cas += cas_index;
		    }		/* SHARD */
		  num_hnqx_cur +=
		    proxy_info_p->num_hint_none_queries_processed;
		  num_hkqx_cur +=
		    proxy_info_p->num_hint_key_queries_processed;
		  num_hiqx_cur += proxy_info_p->num_hint_id_queries_processed;
		  num_haqx_cur +=
		    proxy_info_p->num_hint_all_queries_processed;
		}		/* PROXY */
	      tot_proxy += proxy_index;
#else
	      for (j = 0; j < shm_br->br_info[i].appl_server_max_num; j++)
		{
		  as_info_p = &(shm_appl->as_info[j]);
		  num_req += as_info_p->num_requests_received;
		  num_tx_cur += as_info_p->num_transactions_processed;
		  num_qx_cur += as_info_p->num_queries_processed;
		  num_lt_cur += as_info_p->num_long_transactions;
		  num_lq_cur += as_info_p->num_long_queries;
		  num_eq_cur += as_info_p->num_error_queries;
		  num_interrupts_cur += as_info_p->num_interrupts;
		}		/* CAS */
#endif /* CUBRID_SHARD */

	      if (elapsed_time > 0)
		{
		  tps = ((num_tx_cur - br_mnt_olds[i].num_tx) / elapsed_time);
		  qps = ((num_qx_cur - br_mnt_olds[i].num_qx) / elapsed_time);
		  lts = ((num_lt_cur - br_mnt_olds[i].num_lt) / elapsed_time);
		  lqs = ((num_lq_cur - br_mnt_olds[i].num_lq) / elapsed_time);
		  eqs = ((num_eq_cur - br_mnt_olds[i].num_eq) / elapsed_time);
		  its =
		    ((num_interrupts_cur -
		      br_mnt_olds[i].num_interrupt) / elapsed_time);
#if defined(CUBRID_SHARD)
		  hnqps =
		    ((num_hnqx_cur - br_mnt_olds[i].num_hnqx) / elapsed_time);
		  hkqps =
		    ((num_hkqx_cur - br_mnt_olds[i].num_hkqx) / elapsed_time);
		  hiqps =
		    ((num_hiqx_cur - br_mnt_olds[i].num_hiqx) / elapsed_time);
		  haqps =
		    ((num_haqx_cur - br_mnt_olds[i].num_haqx) / elapsed_time);
#endif /* CUBRID_SHARD */

		  br_mnt_olds[i].num_tx = num_tx_cur;
		  br_mnt_olds[i].num_qx = num_qx_cur;
		  br_mnt_olds[i].num_lt = num_lt_cur;
		  br_mnt_olds[i].num_lq = num_lq_cur;
		  br_mnt_olds[i].num_eq = num_eq_cur;
		  br_mnt_olds[i].num_interrupt = num_interrupts_cur;
#if defined(CUBRID_SHARD)
		  br_mnt_olds[i].num_hnqx = num_hnqx_cur;
		  br_mnt_olds[i].num_hkqx = num_hkqx_cur;
		  br_mnt_olds[i].num_hiqx = num_hiqx_cur;
		  br_mnt_olds[i].num_haqx = num_haqx_cur;
#endif /* CUBRID_SHARD */

		  br_mnt_olds[i].tps = tps;
		  br_mnt_olds[i].qps = qps;
		  br_mnt_olds[i].lts = lts;
		  br_mnt_olds[i].lqs = lqs;
		  br_mnt_olds[i].eqs = eqs;
		  br_mnt_olds[i].its = its;
#if defined(CUBRID_SHARD)
		  br_mnt_olds[i].hnqps = hnqps;
		  br_mnt_olds[i].hkqps = hkqps;
		  br_mnt_olds[i].hiqps = hiqps;
		  br_mnt_olds[i].haqps = haqps;
#endif /* CUBRID_SHARD */
		}
	      else
		{
		  tps = br_mnt_olds[i].tps;
		  qps = br_mnt_olds[i].qps;
		  lts = br_mnt_olds[i].lts;
		  lqs = br_mnt_olds[i].lqs;
		  eqs = br_mnt_olds[i].eqs;
		  its = br_mnt_olds[i].its;
#if defined(CUBRID_SHARD)
		  hnqps = br_mnt_olds[i].hnqps;
		  hkqps = br_mnt_olds[i].hkqps;
		  hiqps = br_mnt_olds[i].hiqps;
		  haqps = br_mnt_olds[i].haqps;
#endif /* CUBRID_SHARD */
		}

#if defined(CUBRID_SHARD)
	      str_out (" %9d", tot_proxy);
	      str_out (" %9d", tot_cas);

	      str_out (" %8d", num_req);
#endif /* CUBRID_SHARD */
	      str_out (" %4ld", tps);
	      str_out (" %4ld", qps);
#if defined(CUBRID_SHARD)
	      str_out (" %6ld", hkqps + hiqps + haqps);
	      if (full_info_flag)
		{
		  str_out (" %6ld", hkqps);
		  str_out (" %6ld", hiqps);
		  str_out (" %6ld", haqps);
		}
	      str_out (" %6ld", hnqps);
#endif /* CUBRID_SHARD */
	      str_out (" %4ld/%-.1f", lts,
		       (shm_appl->long_transaction_time / 1000.0));
	      str_out (" %4ld/%-.1f", lqs,
		       (shm_appl->long_query_time / 1000.0));
	      str_out (" %6ld", eqs);

	      if (full_info_flag)
		{
		  str_out (" %9ld", its);
		  switch (shm_br->br_info[i].access_mode)
		    {
		    case READ_ONLY_ACCESS_MODE:
		      str_out ("%13s", " RO");
		      break;
		    case SLAVE_ONLY_ACCESS_MODE:
		      str_out ("%13s", " SO");
		      break;
		    case READ_WRITE_ACCESS_MODE:
		      str_out ("%13s", " RW");
		      break;
		    default:
		      str_out ("%13s", " --");
		      break;
		    }

		  switch (shm_br->br_info[i].sql_log_mode)
		    {
		    case SQL_LOG_MODE_NONE:
		      str_out ("%9s", " NONE");
		      break;
		    case SQL_LOG_MODE_ERROR:
		      str_out ("%9s", " ERROR");
		      break;
		    case SQL_LOG_MODE_TIMEOUT:
		      str_out ("%9s", " TIMEOUT");
		      break;
		    case SQL_LOG_MODE_NOTICE:
		      str_out ("%9s", " NOTICE");
		      break;
		    case SQL_LOG_MODE_ALL:
		      str_out ("%9s", " ALL");
		      break;
		    default:
		      str_out ("%9s", " --");
		      break;
		    }
		}

	      print_newline ();

	      if (shm_appl->suspend_mode != SUSPEND_NONE)
		{
		  str_out ("%s", "	SUSPENDED");
		  print_newline ();
		}

	      uw_shm_detach (shm_appl);
	    }
	}

      else
	{			/* service_flag == OFF */
	  str_out ("%s", " OFF");
	  print_newline ();
	}
    }

  if (elapsed_time > 0)
    {
      time_old = time_cur;
    }

  return 0;
}

#ifdef GET_PSINFO
static void
time_format (int t, char *time_str)
{
  int min, sec;

  min = t / 60;
  sec = t % 60;
  sprintf (time_str, "%d:%02d", min, sec);
}
#endif

static void
print_header (bool use_pdh_flag)
{
  char buf[256];
  char line_buf[256];
  int col_len = 0;
  int i;

#if defined(CUBRID_SHARD)
  col_len += sprintf (buf + col_len, "%8s ", "PROXY_ID");
  col_len += sprintf (buf + col_len, "%8s ", "SHARD_ID");
  col_len += sprintf (buf + col_len, "%8s ", "CAS_ID");
#else
  col_len += sprintf (buf + col_len, "%2s ", "ID");
#endif /* CUBRID_SHARD */
  col_len += sprintf (buf + col_len, "%5s ", "PID");
  col_len += sprintf (buf + col_len, "%5s ", "QPS");
  col_len += sprintf (buf + col_len, "%5s ", "LQS");
#if defined(WINDOWS)
  col_len += sprintf (buf + col_len, "%6s ", "PORT");
#endif
#if defined(WINDOWS)
  if (use_pdh_flag == true)
    {
      col_len += sprintf (buf + col_len, "%5s ", "PSIZE");
    }
#else
  col_len += sprintf (buf + col_len, "%5s ", "PSIZE");
#endif
  col_len += sprintf (buf + col_len, "%-12s ", "STATUS");
#if 0
  col_len += sprintf (buf + col_len, "%6s ", "PORT");
#endif
#ifdef GET_PSINFO
  col_len += sprintf (buf + col_len, "%5s ", "CPU");
  col_len += sprintf (buf + col_len, "%8s ", "CTIME");
#elif WINDOWS
  if (use_pdh_flag == true)
    {
      col_len += sprintf (buf + col_len, "%5s ", "CPU");
    }
#endif
  if (full_info_flag)
    {
      col_len += sprintf (buf + col_len, "%19s ", "LAST ACCESS TIME");
      col_len += sprintf (buf + col_len, "%16s ", "DB");
      col_len += sprintf (buf + col_len, "%16s ", "HOST");
      col_len += sprintf (buf + col_len, "%19s ", "LAST CONNECT TIME");
#if !defined(CUBRID_SHARD)
      col_len += sprintf (buf + col_len, "%15s ", "CLIENT IP");
#endif /* CUBRID_SHARD */
      col_len += sprintf (buf + col_len, "%15s ", "SQL_LOG_MODE");
#if !defined(CUBRID_SHARD)
      col_len += sprintf (buf + col_len, "%19s ", "TRANSACTION STIME");
      col_len += sprintf (buf + col_len, "%9s ", "# CONNECT");
      col_len += sprintf (buf + col_len, "%9s ", "# RESTART");
#endif /* CUBRID_SHARD */
    }

  for (i = 0; i < col_len; i++)
    line_buf[i] = '-';
  line_buf[i] = '\0';

  str_out ("%s", line_buf);
  print_newline ();
  str_out ("%s", buf);
  print_newline ();
  str_out ("%s", line_buf);
  print_newline ();
}

#if defined(WINDOWS)
static void
refresh ()
{
}

static void
move (int x, int y)
{
  COORD pos;
  pos.X = x;
  pos.Y = y;

  SetConsoleCursorPosition (h_console, pos);
}

static void
clear ()
{
  COORD pos = { 0, 0 };
  DWORD size;
  FillConsoleOutputCharacter (h_console, ' ',
			      scr_info.dwSize.X * scr_info.dwSize.Y, pos,
			      &size);
}

static void
clrtobot ()
{
  CONSOLE_SCREEN_BUFFER_INFO scr_buf_info;
  DWORD size;

  if (!GetConsoleScreenBufferInfo (h_console, &scr_buf_info))
    {
      return;
    }
  FillConsoleOutputCharacter (h_console,
			      ' ',
			      (scr_buf_info.dwSize.Y -
			       scr_buf_info.dwCursorPosition.Y) *
			      scr_buf_info.dwSize.X,
			      scr_buf_info.dwCursorPosition, &size);
}

static void
clrtoeol ()
{
  CONSOLE_SCREEN_BUFFER_INFO scr_buf_info;
  DWORD size;

  if (!GetConsoleScreenBufferInfo (h_console, &scr_buf_info))
    {
      return;
    }
  FillConsoleOutputCharacter (h_console,
			      ' ',
			      scr_buf_info.dwSize.X -
			      scr_buf_info.dwCursorPosition.X + 1,
			      scr_buf_info.dwCursorPosition, &size);
  move (scr_buf_info.dwSize.X, scr_buf_info.dwCursorPosition.Y);
}

static void
endwin ()
{
  clear ();
  move (0, 0);
}
#endif

#if defined(CUBRID_SHARD)
static int
metadata_monitor (void)
{
  char *shm_metadata_cp = NULL;

  T_SHM_SHARD_USER *shm_user_p;
  T_SHM_SHARD_KEY *shm_key_p;
  T_SHM_SHARD_CONN *shm_conn_p;

  T_SHARD_USER *user_p;
  T_SHARD_KEY *key_p;
  T_SHARD_KEY_RANGE *range_p;
  T_SHARD_CONN *conn_p;

  T_SHM_SHARD_KEY_STAT *key_stat_p;
  T_SHM_SHARD_KEY_RANGE_STAT *range_stat_p;
  T_SHM_SHARD_CONN_STAT *shard_stat_p;

  char *shm_as_cp = NULL;
  T_SHM_PROXY *shm_proxy_p = NULL;
  T_PROXY_INFO *proxy_info_p = NULL;

  SHARD_STAT_ITEM *shard_stat_items = NULL;
  KEY_STAT_ITEM *key_stat_items = NULL;

  int i, j, k;
  int shmid;
  int col_len;
  char buf[1024];
  char line_buf[1024];
  int proxy_index;
  int shard_stat_index, key_stat_index, range_index;

  for (i = 0; i < shm_br->num_broker; i++)
    {
      if (shm_br->br_info[i].service_flag != ON)
	{
	  continue;
	}
      shmid = shm_br->br_info[i].metadata_shm_id;
      shm_metadata_cp =
	(char *) uw_shm_open (shmid, SHM_BROKER, SHM_MODE_MONITOR);
      if (shm_metadata_cp == NULL)
	{
	  str_out ("%s", "shared memory open error");
	  return -1;
	}

      shm_user_p = shard_metadata_get_user (shm_metadata_cp);
      shm_key_p = shard_metadata_get_key (shm_metadata_cp);
      shm_conn_p = shard_metadata_get_conn (shm_metadata_cp);

      str_out ("%% %s ", shm_br->br_info[i].name);
      str_out ("[%d] ", shmid);
      print_newline ();
      str_out ("MODULAR : %d, ", shm_br->br_info[i].shard_key_modular);
      str_out ("LIBRARY_NAME : %s, ",
	       (shm_br->br_info[i].shard_key_library_name[0] ==
		0) ? "NOT DEFINED" : shm_br->br_info[i].
	       shard_key_library_name);
      str_out ("FUNCTION_NAME : %s ",
	       (shm_br->br_info[i].shard_key_function_name[0] ==
		0) ? "NOT DEFINED" : shm_br->br_info[i].
	       shard_key_function_name);
      print_newline ();

      /* PRINT CONN INFO */
      if (full_info_flag)
	{
	  str_out ("SHARD : ");
	  for (j = 0; j < shm_conn_p->num_shard_conn; j++)
	    {
	      if (j != 0)
		{
		  str_out (", ");
		}
	      conn_p = (T_SHARD_CONN *) (&(shm_conn_p->shard_conn[j]));
	      str_out ("%d [%s] [%s]", conn_p->shard_id, conn_p->db_conn_info,
		       conn_p->db_name);
	    }
	  print_newline ();
	}

      /* PRINT SHARD STATSTICS */
      shm_as_cp =
	(char *) uw_shm_open (shm_br->br_info[i].appl_server_shm_id,
			      SHM_APPL_SERVER, SHM_MODE_MONITOR);
      if (shm_as_cp == NULL)
	{
	  str_out ("%s", "shared memory open error");
	  return -1;
	}
      shm_proxy_p = shard_shm_get_proxy (shm_as_cp);
      if (shm_proxy_p == NULL)
	{
	  str_out ("%s", "shared memory open error");
	  uw_shm_detach (shm_as_cp);
	  return -1;
	}

      if (shard_stat_items != NULL)
	{
	  free ((char *) shard_stat_items);
	}
      shard_stat_items =
	(SHARD_STAT_ITEM *) malloc (sizeof (SHARD_STAT_ITEM) *
				    shm_conn_p->num_shard_conn);
      if (shard_stat_items == NULL)
	{
	  str_out ("%s", "malloc error");
	  uw_shm_detach (shm_as_cp);
	  return -1;
	}
      memset ((char *) shard_stat_items, 0,
	      sizeof (SHARD_STAT_ITEM) * shm_conn_p->num_shard_conn);

      for (proxy_index = 0, proxy_info_p =
	   shard_shm_get_first_proxy_info (shm_proxy_p); proxy_info_p;
	   proxy_index++, proxy_info_p =
	   shard_shm_get_next_proxy_info (proxy_info_p))
	{
	  shard_stat_p = shard_shm_get_shard_stat (proxy_info_p, 0);
	  if (shard_stat_p == NULL)
	    {
	      str_out ("%s", "shard_stat open error");
	      uw_shm_detach (shm_as_cp);
	      return -1;
	    }
	  for (shard_stat_index = 0;
	       shard_stat_index < proxy_info_p->num_shard_conn;
	       shard_stat_p =
	       shard_shm_get_shard_stat (proxy_info_p, ++shard_stat_index))
	    {
	      shard_stat_items[shard_stat_index].
		num_hint_key_queries_requested +=
		shard_stat_p->num_hint_key_queries_requested;
	      shard_stat_items[shard_stat_index].
		num_hint_id_queries_requested +=
		shard_stat_p->num_hint_id_queries_requested;
	      shard_stat_items[shard_stat_index].
		num_hint_all_queries_requested +=
		shard_stat_p->num_hint_all_queries_requested;
	    }
	}			/* proxy_info loop */

      if (shard_stat_items == NULL)
	{
	  str_out ("%s", "stat open error");
	  return -1;
	}

      str_out ("SHARD STATISTICS ");
      print_newline ();

      col_len = 0;
      col_len += sprintf (buf + col_len, "%5s ", "ID");
      col_len += sprintf (buf + col_len, "%10s", "NUM-KEY-Q");
      col_len += sprintf (buf + col_len, "%10s", "NUM-ID-Q");
      col_len += sprintf (buf + col_len, "%10s", "SUM");

      for (k = 0; k < col_len; k++)
	line_buf[k] = '-';
      line_buf[k] = '\0';

      str_out ("\t%s", buf);
      print_newline ();
      str_out ("\t%s", line_buf);
      print_newline ();

      for (shard_stat_index = 0;
	   shard_stat_index < shm_conn_p->num_shard_conn; shard_stat_index++)
	{
	  str_out ("\t%5d %10ld %9ld %9ld", shard_stat_index,
		   shard_stat_items[shard_stat_index].
		   num_hint_key_queries_requested,
		   shard_stat_items[shard_stat_index].
		   num_hint_id_queries_requested,
		   shard_stat_items[shard_stat_index].
		   num_hint_key_queries_requested +
		   shard_stat_items[shard_stat_index].
		   num_hint_id_queries_requested);
	  print_newline ();
	}
      print_newline ();

      /* PRINT KEY STATISTICS */
      if (full_info_flag)
	{
	  if (key_stat_items != NULL)
	    {
	      free ((char *) key_stat_items);
	    }
	  key_stat_items =
	    (KEY_STAT_ITEM *) malloc (sizeof (KEY_STAT_ITEM) *
				      shm_key_p->num_shard_key);
	  if (key_stat_items == NULL)
	    {
	      str_out ("%s", "malloc error");
	      uw_shm_detach (shm_as_cp);
	      return -1;
	    }
	  memset ((char *) key_stat_items, 0,
		  sizeof (KEY_STAT_ITEM) * shm_key_p->num_shard_key);

	  for (proxy_index = 0, proxy_info_p =
	       shard_shm_get_first_proxy_info (shm_proxy_p); proxy_info_p;
	       proxy_index++, proxy_info_p =
	       shard_shm_get_next_proxy_info (proxy_info_p))
	    {
	      key_stat_p = shard_shm_get_key_stat (proxy_info_p, 0);
	      if (key_stat_p == NULL)
		{
		  str_out ("%s", "key_stat open error");
		  uw_shm_detach (shm_as_cp);
		  return -1;
		}

	      for (key_stat_index = 0;
		   key_stat_index < proxy_info_p->num_shard_key;
		   key_stat_p =
		   shard_shm_get_key_stat (proxy_info_p, ++key_stat_index))
		{
		  for (range_index = 0;
		       range_index < key_stat_p->num_key_range; range_index++)
		    {
		      key_stat_items[key_stat_index].
			num_range_queries_requested[range_index] +=
			key_stat_p->stat[range_index].
			num_range_queries_requested;
		    }
		}
	    }

	  for (j = 0; j < shm_key_p->num_shard_key; j++)
	    {
	      key_p = (T_SHARD_KEY *) (&(shm_key_p->shard_key[j]));
	      str_out ("RANGE STATISTICS : %s ", key_p->key_column);
	      print_newline ();
	      col_len = 0;
	      col_len += sprintf (buf + col_len, "%5s ~ ", "MIN");
	      col_len += sprintf (buf + col_len, "%5s : ", "MAX");
	      col_len += sprintf (buf + col_len, "%10s", "SHARD");
	      col_len += sprintf (buf + col_len, "%10s", "NUM-Q");
	      for (k = 0; k < col_len; k++)
		line_buf[k] = '-';
	      line_buf[k] = '\0';
	      str_out ("\t%s", buf);
	      print_newline ();
	      str_out ("\t%s", line_buf);
	      print_newline ();
	      for (k = 0; k < key_p->num_key_range; k++)
		{
		  range_p = (T_SHARD_KEY_RANGE *) (&(key_p->range[k]));
		  str_out ("\t%5d ~ %5d : %10d %9ld", range_p->min,
			   range_p->max, range_p->shard_id,
			   key_stat_items[j].num_range_queries_requested[k]);
		  print_newline ();
		}
	    }
	}

      /* PRINT USER INFO */
      if (full_info_flag)
	{
	  for (j = 0; j < shm_user_p->num_shard_user; j++)
	    {
	      user_p = (T_SHARD_USER *) (&(shm_user_p->shard_user[j]));
	      str_out ("DB Alias : %s [USER : %s, PASSWD : %s]",
		       user_p->db_name, user_p->db_user, user_p->db_password);
	      print_newline ();
	    }
	}
      print_newline ();

      uw_shm_detach (shm_metadata_cp);
      uw_shm_detach (shm_as_cp);
    }

  /*
     col_len = 0;
     col_len +=
     sprintf (line_buf + col_len, "%8d ",
     proxy_index + 1);
   */

  return 0;
}

static int
client_monitor (void)
{
  char *shm_as_cp = NULL;
  T_SHM_APPL_SERVER *shm_appl = NULL;
  T_SHM_PROXY *shm_proxy_p = NULL;
  T_PROXY_INFO *proxy_info_p = NULL;
  T_CLIENT_INFO *client_info_p = NULL;
  int i, len, proxy_index, client_index;
  char buf[256];
  char line_buf[256];
  int col_len = 0;
  char *ip_str;
  struct tm ct1;
  for (i = 0; i < shm_br->num_broker; i++)
    {
      shm_as_cp =
	(char *) uw_shm_open (shm_br->br_info[i].appl_server_shm_id,
			      SHM_APPL_SERVER, SHM_MODE_MONITOR);
      if (shm_as_cp == NULL)
	{
	  str_out ("%s", "shared memory open error");
	  return -1;
	}
      shm_proxy_p = shard_shm_get_proxy (shm_as_cp);
      if (shm_proxy_p == NULL)
	{
	  str_out ("%s", "shared memory open error");
	  uw_shm_detach (shm_as_cp);
	  return -1;
	}

      for (proxy_index = 0, proxy_info_p =
	   shard_shm_get_first_proxy_info (shm_proxy_p);
	   proxy_info_p;
	   proxy_index++, proxy_info_p =
	   shard_shm_get_next_proxy_info (proxy_info_p))
	{
	  str_out ("%% %s(%d), MAX-CLIENT : %d ",
		   shm_br->br_info[i].name, proxy_index,
		   proxy_info_p->max_client);
	  print_newline ();
	  col_len = 0;
	  col_len += sprintf (buf + col_len, "%10s", "CLIENT-ID");
	  col_len += sprintf (buf + col_len, "%20s", "CLIENT-IP");
	  col_len += sprintf (buf + col_len, "%22s", "CONN-TIME");
	  col_len += sprintf (buf + col_len, "%22s", "L-REQ-TIME");
	  col_len += sprintf (buf + col_len, "%22s", "L-RES-TIME");
	  if (full_info_flag)
	    {
	      col_len += sprintf (buf + col_len, "%12s", "L-REQ-CODE");
	    }

	  for (len = 0; len < col_len; len++)
	    line_buf[len] = '-';
	  line_buf[len] = '\0';
	  str_out ("%s", line_buf);
	  print_newline ();
	  str_out ("%s", buf);
	  print_newline ();
	  str_out ("%s", line_buf);
	  print_newline ();
	  client_info_p = shard_shm_get_first_client_info (proxy_info_p);
	  for (client_index = 0; client_index < proxy_info_p->max_client;
	       client_index++, client_info_p =
	       shard_shm_get_next_client_info (client_info_p))
	    {
	      if (client_info_p->client_id == -1)
		{
		  continue;
		}
	      str_out ("%10d", client_info_p->client_id);
	      ip_str =
		ut_uchar2ipstr ((unsigned char *) (&client_info_p->
						   client_ip));
	      str_out ("%20s", ip_str);
	      localtime_r (&client_info_p->connect_time, &ct1);
	      ct1.tm_year += 1900;
	      str_out ("   %4d/%2d/%2d %2d:%2d:%2d", ct1.tm_year,
		       ct1.tm_mon + 1, ct1.tm_mday, ct1.tm_hour, ct1.tm_min,
		       ct1.tm_sec);
	      if (client_info_p->req_time > 0)
		{
		  localtime_r (&client_info_p->req_time, &ct1);
		  ct1.tm_year += 1900;
		  str_out ("   %4d/%2d/%2d %2d:%2d:%2d", ct1.tm_year,
			   ct1.tm_mon + 1, ct1.tm_mday, ct1.tm_hour,
			   ct1.tm_min, ct1.tm_sec);
		}
	      else
		{
		  str_out ("%22s", "-");
		}

	      if (client_info_p->res_time > 0)
		{
		  localtime_r (&client_info_p->res_time, &ct1);
		  ct1.tm_year += 1900;
		  str_out ("   %4d/%2d/%2d %2d:%2d:%2d", ct1.tm_year,
			   ct1.tm_mon + 1, ct1.tm_mday, ct1.tm_hour,
			   ct1.tm_min, ct1.tm_sec);
		}
	      else
		{
		  str_out ("%22s", "-");
		}

	      if (full_info_flag)
		{
		  str_out ("%11d", client_info_p->func_code - 1);
		}
	      print_newline ();
	    }
	}
    }
  return 0;
}
#endif /* CUBRID_SHARD */
