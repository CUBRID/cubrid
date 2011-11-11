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
};

static void str_to_screen (const char *msg);
static void print_newline ();
static int get_char (void);
static void print_usage (void);
static int get_args (int argc, char *argv[], char *br_vector);
static void print_job_queue (T_MAX_HEAP_NODE *);
static void ip2str (unsigned char *ip, char *ip_str);
static void time2str (const time_t t, char *str);
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

static T_SHM_BROKER *shm_br;
static bool display_job_queue = false;
static int refresh_sec = 0;
static bool br_monitor_flag = false;
static int last_access_sec = 0;
static bool tty_mode = false;
static bool full_info_flag = false;
static int state_interval = 1;

static int max_col_len = 0;

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
      fprintf (stderr, "master shared memory open error\r\n");
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
	    br_monitor (br_vector);
	  else
	    appl_monitor (br_vector);
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
	  for (i = 0; i < refresh_sec * 10; i++)
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
  printf ("broker_monitor [-b] [-q] [-t] [-s <sec>] [-f] [<expr>]\n");
  printf ("\t-b brief mode (show broker info)\n");
  printf ("\t-q display job queue\n");
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

  display_job_queue = false;
  refresh_sec = 0;
  br_monitor_flag = false;
  last_access_sec = 0;
  full_info_flag = false;
  state_interval = 1;

  while ((c = getopt (argc, argv, "hbqts:l:f")) != EOF)
    {
      switch (c)
	{
	case 't':
	  tty_mode = true;
	  break;
	case 'q':
	  display_job_queue = true;
	  break;
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

  static APPL_MONITORING_ITEM *appl_mnt_olds;

  time_t current_time, time_cur;
  INT64 qps, lqs;
  double elapsed_time = 0;
#ifdef GET_PSINFO
  T_PSINFO proc_info;
  char time_str[32];
#endif

  if (appl_mnt_olds == NULL)
    {
      int n = 0;
      for (i = 0; i < shm_br->num_broker; i++)
	{
	  n += shm_br->br_info[i].appl_server_max_num;
	}
      appl_mnt_olds =
	(APPL_MONITORING_ITEM *) calloc (sizeof (APPL_MONITORING_ITEM), n);
      if (appl_mnt_olds == NULL)
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

      if (br_vector[i] == 0)
	continue;

      str_out ("%% %s ", shm_br->br_info[i].name);

      if (shm_br->br_info[i].service_flag == ON)
	{
	  shm_appl = (T_SHM_APPL_SERVER *)
	    uw_shm_open (shm_br->br_info[i].appl_server_shm_id,
			 SHM_APPL_SERVER, SHM_MODE_MONITOR);

	  if (shm_appl == NULL)
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
	      str_out ("%s ", shm_br->br_info[i].access_log_file);
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

	      appl_offset = 0;
	      for (k = 0; k < i; k++)
		{
		  appl_offset += shm_br->br_info[k].appl_server_max_num;
		}

	      for (j = 0; j < shm_br->br_info[i].appl_server_max_num; j++)
		{
		  if (shm_appl->as_info[j].service_flag != SERVICE_ON)
		    continue;

		  if (last_access_sec > 0)
		    {
		      if (shm_appl->as_info[j].uts_status != UTS_STATUS_BUSY
			  || current_time -
			  shm_appl->as_info[j].last_access_time <
			  last_access_sec)
			{
			  continue;
			}
		    }
		  col_len = 0;

		  col_len += sprintf (line_buf + col_len, "%2d ", j + 1);
		  col_len += sprintf (line_buf + col_len, "%5d ",
				      shm_appl->as_info[j].pid);

		  if (elapsed_time > 0)
		    {
		      qps = (shm_appl->as_info[j].num_queries_processed -
			     appl_mnt_olds[appl_offset + j].
			     num_query_processed) / elapsed_time;

		      lqs = (shm_appl->as_info[j].num_long_queries -
			     appl_mnt_olds[appl_offset + j].
			     num_long_query) / elapsed_time;

		      appl_mnt_olds[appl_offset + j].num_query_processed =
			shm_appl->as_info[j].num_queries_processed;

		      appl_mnt_olds[appl_offset + j].num_long_query =
			shm_appl->as_info[j].num_long_queries;

		      appl_mnt_olds[appl_offset + j].qps = qps;
		      appl_mnt_olds[appl_offset + j].lqs = lqs;
		    }
		  else
		    {
		      qps = appl_mnt_olds[appl_offset + j].qps;
		      lqs = appl_mnt_olds[appl_offset + j].lqs;
		    }

		  col_len += sprintf (line_buf + col_len, "%5ld ", qps);
		  col_len += sprintf (line_buf + col_len, "%5ld ", lqs);

#if defined(WINDOWS)
		  col_len +=
		    sprintf (line_buf + col_len, "%5d ",
			     shm_appl->as_info[j].as_port);
#endif

#if defined(WINDOWS)
		  if (shm_appl->use_pdh_flag == TRUE)
		    {
		      col_len += sprintf (line_buf + col_len, "%5d ",
					  shm_appl->as_info[j].pdh_workset);
		    }
#else
		  col_len += sprintf (line_buf + col_len, "%5d ",
				      getsize (shm_appl->as_info[j].pid));
#endif
		  if (shm_appl->as_info[j].uts_status == UTS_STATUS_BUSY)
		    {
		      if (IS_APPL_SERVER_TYPE_CAS
			  (shm_br->br_info[i].appl_server))
			{
			  if (shm_appl->as_info[j].con_status ==
			      CON_STATUS_OUT_TRAN)
			    {
			      col_len +=
				sprintf (line_buf + col_len, "%-12s ",
					 "CLOSE WAIT");
			    }
			  else if (shm_appl->as_info[j].log_msg[0] == '\0')
			    {
			      col_len +=
				sprintf (line_buf + col_len, "%-12s ",
					 "CLIENT WAIT");
			    }
			  else
			    {
			      col_len +=
				sprintf (line_buf + col_len, "%-12s ",
					 "BUSY");
			    }
			}
		      else
			{
			  col_len += sprintf (line_buf + col_len, "%-12s ",
					      "BUSY");
			}
		    }
#if defined(WINDOWS)
		  else if (shm_appl->as_info[j].uts_status ==
			   UTS_STATUS_BUSY_WAIT)
		    {
		      col_len += sprintf (line_buf + col_len, "%-12s ",
					  "BUSY");
		    }
#endif
		  else if (shm_appl->as_info[j].uts_status ==
			   UTS_STATUS_RESTART)
		    {
		      col_len += sprintf (line_buf + col_len, "%-12s ",
					  "INITIALIZE");
		    }
		  else
		    {
		      col_len += sprintf (line_buf + col_len, "%-12s ",
					  "IDLE");
		    }

#ifdef GET_PSINFO
		  get_psinfo (shm_appl->as_info[j].pid, &proc_info);
		  col_len += sprintf (line_buf + col_len, "%5.2f",
				      proc_info.pcpu);
		  time_format (proc_info.cpu_time, time_str);
		  col_len += sprintf (line_buf + col_len, "%7s ", time_str);
#elif WINDOWS
		  if (shm_appl->use_pdh_flag == TRUE)
		    {
		      float pct_cpu;
		      pct_cpu = shm_appl->as_info[j].pdh_pct_cpu;
		      if (pct_cpu >= 0)
			{
			  col_len += sprintf (line_buf + col_len, "%5.2f ",
					      pct_cpu);
			}
		      else
			{
			  col_len += sprintf (line_buf + col_len, "%5s ",
					      " - ");
			}
		    }
#endif

		  if (full_info_flag)
		    {
		      char sql_log_mode_string[16];

		      last_access_time =
			shm_appl->as_info[j].last_access_time;
		      localtime_r (&last_access_time, &cur_tm);
		      cur_tm.tm_year += 1900;

		      col_len += sprintf (line_buf + col_len,
					  "%02d/%02d/%02d %02d:%02d:%02d ",
					  cur_tm.tm_year, cur_tm.tm_mon + 1,
					  cur_tm.tm_mday, cur_tm.tm_hour,
					  cur_tm.tm_min, cur_tm.tm_sec);

		      if (shm_appl->as_info[j].database_name[0] != '\0')
			{
			  col_len +=
			    sprintf (line_buf + col_len, "%16.16s ",
				     shm_appl->as_info[j].database_name);
			  col_len +=
			    sprintf (line_buf + col_len, "%16.16s ",
				     shm_appl->as_info[j].database_host);
			  last_connect_time =
			    shm_appl->as_info[j].last_connect_time;
			  localtime_r (&last_connect_time, &cur_tm);
			  cur_tm.tm_year += 1900;

			  col_len += sprintf (line_buf + col_len,
					      "%02d/%02d/%02d %02d:%02d:%02d ",
					      cur_tm.tm_year,
					      cur_tm.tm_mon + 1,
					      cur_tm.tm_mday,
					      cur_tm.tm_hour, cur_tm.tm_min,
					      cur_tm.tm_sec);
			}
		      else
			{
			  col_len +=
			    sprintf (line_buf + col_len, "%16c %16c %19c ",
				     '-', '-', '-');
			}

		      col_len +=
			sprintf (line_buf + col_len, "%15.15s ",
				 ut_get_ipv4_string (ip_str, sizeof (ip_str),
						     shm_appl->as_info[j].
						     cas_clt_ip));

		      strncpy (sql_log_mode_string, "-",
			       sizeof (sql_log_mode_string));

		      if (shm_appl->as_info[j].cur_sql_log_mode !=
			  shm_appl->sql_log_mode)
			{
			  if (shm_appl->as_info[j].cur_sql_log_mode ==
			      SQL_LOG_MODE_NONE)
			    {
			      strncpy (sql_log_mode_string, "NONE",
				       sizeof (sql_log_mode_string));
			    }
			  else if (shm_appl->as_info[j].cur_sql_log_mode ==
				   SQL_LOG_MODE_ERROR)
			    {
			      strncpy (sql_log_mode_string, "ERROR",
				       sizeof (sql_log_mode_string));
			    }
			  else if (shm_appl->as_info[j].cur_sql_log_mode ==
				   SQL_LOG_MODE_TIMEOUT)
			    {
			      strncpy (sql_log_mode_string, "TIMEOUT",
				       sizeof (sql_log_mode_string));
			    }
			  else if (shm_appl->as_info[j].cur_sql_log_mode ==
				   SQL_LOG_MODE_NOTICE)
			    {
			      strncpy (sql_log_mode_string, "NOTICE",
				       sizeof (sql_log_mode_string));
			    }
			  else if (shm_appl->as_info[j].cur_sql_log_mode ==
				   SQL_LOG_MODE_ALL)
			    {
			      strncpy (sql_log_mode_string, "ALL",
				       sizeof (sql_log_mode_string));
			    }
			}

		      col_len +=
			sprintf (line_buf + col_len, "%15.15s ",
				 sql_log_mode_string);

		      tran_start_time =
			shm_appl->as_info[j].transaction_start_time;
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
			  col_len +=
			    sprintf (line_buf + col_len, "%19c ", '-');
			}
		      col_len +=
			sprintf (line_buf + col_len, "%9d ",
				 shm_appl->as_info[j].num_connect_requests);
		      col_len +=
			sprintf (line_buf + col_len, "%9d ",
				 shm_appl->as_info[j].num_restarts);
		    }

		  if (col_len >= max_col_len)
		    {
		      max_col_len = col_len;
		    }
		  else
		    {
		      sprintf (line_buf + col_len, "%*c",
			       max_col_len - col_len, ' ');
		    }
		  str_out ("%s", line_buf);
		  print_newline ();
		  if (shm_appl->as_info[j].uts_status == UTS_STATUS_BUSY)
		    {
		      sprintf (line_buf, "SQL: %s",
			       shm_appl->as_info[j].log_msg);
		      str_out ("%s", line_buf);
		      print_newline ();
		    }
		}
	      print_newline ();

	      if (display_job_queue == true)
		print_job_queue (job_queue);

	      uw_shm_detach (shm_appl);
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
  char buf[128];
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

  buf_len = 0;
  buf_len += sprintf (buf + buf_len, "  %-12s", "NAME");
  buf_len += sprintf (buf + buf_len, "%6s", "PID");
  if (full_info_flag)
    {
      buf_len += sprintf (buf + buf_len, "%7s", "PSIZE");
    }
  buf_len += sprintf (buf + buf_len, "%6s", "PORT");
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
#ifdef GET_PSINFO
  buf_len += sprintf (buf + buf_len, "%4s", "THR");
  buf_len += sprintf (buf + buf_len, "%6s", "CPU");
  buf_len += sprintf (buf + buf_len, "%6s", "TIME");
#endif
  buf_len += sprintf (buf + buf_len, "%9s", "REQ");
  buf_len += sprintf (buf + buf_len, "%5s", "TPS");
  buf_len += sprintf (buf + buf_len, "%5s", "QPS");
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
      time_t cur_time;

      if (br_vector[i] == 0)
	continue;

      str_out ("* %-12s", shm_br->br_info[i].name);

      if (shm_br->br_info[i].service_flag == ON)
	{
	  shm_appl = (T_SHM_APPL_SERVER *)
	    uw_shm_open (shm_br->br_info[i].appl_server_shm_id,
			 SHM_APPL_SERVER, SHM_MODE_MONITOR);

	  if (shm_appl == NULL)
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

	      cur_time = time (NULL);

	      for (j = 0; j < shm_br->br_info[i].appl_server_max_num; j++)
		{
		  num_req += shm_appl->as_info[j].num_request;

		  if (full_info_flag)
		    {
		      bool time_expired =
			(cur_time - shm_appl->as_info[j].last_access_time >=
			 state_interval);

		      if (shm_appl->as_info[j].uts_status == UTS_STATUS_BUSY
			  && shm_appl->as_info[j].con_status !=
			  CON_STATUS_OUT_TRAN)
			{
			  if (shm_appl->as_info[j].log_msg[0] == '\0')
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
		      else if (shm_appl->as_info[j].uts_status ==
			       UTS_STATUS_BUSY_WAIT)
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

	      num_tx_cur = 0;
	      num_qx_cur = 0;
	      num_lt_cur = 0;
	      num_lq_cur = 0;
	      num_eq_cur = 0;
	      num_interrupts_cur = 0;
	      for (j = 0; j < shm_br->br_info[i].appl_server_max_num; j++)
		{
		  num_tx_cur +=
		    shm_appl->as_info[j].num_transactions_processed;
		  num_qx_cur += shm_appl->as_info[j].num_queries_processed;
		  num_lt_cur += shm_appl->as_info[j].num_long_transactions;
		  num_lq_cur += shm_appl->as_info[j].num_long_queries;
		  num_eq_cur += shm_appl->as_info[j].num_error_queries;
		  num_interrupts_cur += shm_appl->as_info[j].num_interrupts;
		}

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

		  br_mnt_olds[i].num_tx = num_tx_cur;
		  br_mnt_olds[i].num_qx = num_qx_cur;
		  br_mnt_olds[i].num_lt = num_lt_cur;
		  br_mnt_olds[i].num_lq = num_lq_cur;
		  br_mnt_olds[i].num_eq = num_eq_cur;
		  br_mnt_olds[i].num_interrupt = num_interrupts_cur;

		  br_mnt_olds[i].tps = tps;
		  br_mnt_olds[i].qps = qps;
		  br_mnt_olds[i].lts = lts;
		  br_mnt_olds[i].lqs = lqs;
		  br_mnt_olds[i].eqs = eqs;
		  br_mnt_olds[i].its = its;
		}
	      else
		{
		  tps = br_mnt_olds[i].tps;
		  qps = br_mnt_olds[i].qps;
		  lts = br_mnt_olds[i].lts;
		  lqs = br_mnt_olds[i].lqs;
		  eqs = br_mnt_olds[i].eqs;
		  its = br_mnt_olds[i].its;
		}

	      str_out (" %4ld", tps);
	      str_out (" %4ld", qps);
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

  col_len += sprintf (buf + col_len, "%2s ", "ID");
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
      col_len += sprintf (buf + col_len, "%15s ", "CLIENT IP");
      col_len += sprintf (buf + col_len, "%15s ", "SQL_LOG_MODE");
      col_len += sprintf (buf + col_len, "%19s ", "TRANSACTION STIME");
      col_len += sprintf (buf + col_len, "%9s ", "# CONNECT");
      col_len += sprintf (buf + col_len, "%9s ", "# RESTART");
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
