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
#include <assert.h>
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
#if defined(AIX)
#define _BOOL
#include <curses.h>
#else
#include <curses.h>
#endif
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

#define         FIELD_DELIMITER          ' '

#define         FIELD_WIDTH_BROKER_NAME 20

#if defined(WINDOWS) && !defined(PRId64)
# define PRId64 "lld"
#endif

typedef enum
{
  FIELD_BROKER_NAME = 0,
  FIELD_PID,
  FIELD_PSIZE,
  FIELD_PORT,
  FIELD_ACTIVE_P,
  FIELD_ACTIVE_C,
  FIELD_APPL_SERVER_NUM_TOTAL,
  FIELD_APPL_SERVER_NUM_CLIENT_WAIT,
  FIELD_APPL_SERVER_NUM_BUSY,
  FIELD_APPL_SERVER_NUM_CLIENT_WAIT_IN_SEC,
  FIELD_APPL_SERVER_NUM_BUSY_IN_SEC,	/* = 10 */
  FIELD_JOB_QUEUE_ID,
  FIELD_THREAD,
  FIELD_CPU_USAGE,
  FIELD_CPU_TIME,
  FIELD_TPS,
  FIELD_QPS,
  FIELD_NUM_OF_SELECT_QUERIES,
  FIELD_NUM_OF_INSERT_QUERIES,
  FIELD_NUM_OF_UPDATE_QUERIES,
  FIELD_NUM_OF_DELETE_QUERIES,	/* = 20 */
  FIELD_NUM_OF_OTHERS_QUERIES,
  FIELD_K_QPS,
  FIELD_H_KEY,
  FIELD_H_ID,
  FIELD_H_ALL,
  FIELD_NK_QPS,
  FIELD_LONG_TRANSACTION,
  FIELD_LONG_QUERY,
  FIELD_ERROR_QUERIES,
  FIELD_UNIQUE_ERROR_QUERIES,	/* = 30 */
  FIELD_CANCELED,
  FIELD_ACCESS_MODE,
  FIELD_SQL_LOG,
  FIELD_NUMBER_OF_CONNECTION,
  FIELD_PROXY_ID,
  FIELD_SHARD_ID,
  FIELD_ID,
  FIELD_LQS,
  FIELD_STATUS,
  FIELD_LAST_ACCESS_TIME,	/* = 40 */
  FIELD_DB_NAME,
  FIELD_HOST,
  FIELD_LAST_CONNECT_TIME,
  FIELD_CLIENT_IP,
  FIELD_SQL_LOG_MODE,
  FIELD_TRANSACTION_STIME,
  FIELD_CONNECT,
  FIELD_RESTART,
  FIELD_STMT_Q_SIZE,
  FIELD_SHARD_Q_SIZE,		/* = 50 */
  FIELD_LAST = FIELD_SHARD_Q_SIZE
} FIELD_NAME;

typedef enum
{
  FIELD_T_STRING = 0,
  FIELD_T_INT,
  FIELD_T_FLOAT,
  FIELD_T_UINT64,
  FIELD_T_INT64,
  FIELD_T_TIME
} FIELD_TYPE;

typedef enum
{
  FIELD_LEFT_ALIGN = 0,
  FIELD_RIGHT_ALIGN
} FIELD_ALIGN;

struct status_field
{
  FIELD_NAME name;
  unsigned int width;
  char title[256];
  FIELD_ALIGN align;
};

struct status_field fields[FIELD_LAST + 1] = {
  {FIELD_BROKER_NAME, FIELD_WIDTH_BROKER_NAME, "NAME", FIELD_LEFT_ALIGN},
  {FIELD_PID, 5, "PID", FIELD_RIGHT_ALIGN},
  {FIELD_PSIZE, 7, "PSIZE", FIELD_RIGHT_ALIGN},
  {FIELD_PORT, 5, "PORT", FIELD_RIGHT_ALIGN},
  {FIELD_ACTIVE_P, 10, "Active-P", FIELD_RIGHT_ALIGN},
  {FIELD_ACTIVE_C, 10, "Active-C", FIELD_RIGHT_ALIGN},
  {FIELD_APPL_SERVER_NUM_TOTAL, 5, "", FIELD_RIGHT_ALIGN},
  {FIELD_APPL_SERVER_NUM_CLIENT_WAIT, 6, "W", FIELD_RIGHT_ALIGN},
  {FIELD_APPL_SERVER_NUM_BUSY, 6, "B", FIELD_RIGHT_ALIGN},
  {FIELD_APPL_SERVER_NUM_CLIENT_WAIT_IN_SEC, 6, "", FIELD_RIGHT_ALIGN},
  {FIELD_APPL_SERVER_NUM_BUSY_IN_SEC, 6, "", FIELD_RIGHT_ALIGN},
  {FIELD_JOB_QUEUE_ID, 4, "JQ", FIELD_RIGHT_ALIGN},
  {FIELD_THREAD, 4, "THR", FIELD_RIGHT_ALIGN},
  {FIELD_CPU_USAGE, 6, "CPU", FIELD_RIGHT_ALIGN},
  {FIELD_CPU_TIME, 6, "CTIME", FIELD_RIGHT_ALIGN},
  {FIELD_TPS, 20, "TPS", FIELD_RIGHT_ALIGN},
  {FIELD_QPS, 20, "QPS", FIELD_RIGHT_ALIGN},
  {FIELD_NUM_OF_SELECT_QUERIES, 8, "SELECT", FIELD_RIGHT_ALIGN},
  {FIELD_NUM_OF_INSERT_QUERIES, 8, "INSERT", FIELD_RIGHT_ALIGN},
  {FIELD_NUM_OF_UPDATE_QUERIES, 8, "UPDATE", FIELD_RIGHT_ALIGN},
  {FIELD_NUM_OF_DELETE_QUERIES, 8, "DELETE", FIELD_RIGHT_ALIGN},
  {FIELD_NUM_OF_OTHERS_QUERIES, 8, "OTHERS", FIELD_RIGHT_ALIGN},
  {FIELD_K_QPS, 7, "K-QPS", FIELD_RIGHT_ALIGN},
  {FIELD_H_KEY, 7, "(H-KEY", FIELD_RIGHT_ALIGN},
  {FIELD_H_ID, 7, "H_ID", FIELD_RIGHT_ALIGN},
  {FIELD_H_ALL, 7, "H-ALL)", FIELD_RIGHT_ALIGN},
  {FIELD_NK_QPS, 7, "NK-QPS", FIELD_RIGHT_ALIGN},
  /*
   * 5: width of long transaction count
   * 1: delimiter(/)
   * 4: width of long transaction time
   * output example :
   *    [long transaction count]/[long transaction time]
   *    10/60.0
   * */
  {FIELD_LONG_TRANSACTION, 5 + 1 + 4, "LONG-T", FIELD_RIGHT_ALIGN},
  /*
   * 5: width of long query count
   * 1: delimiter(/)
   * 4: width of long query time
   * output example :
   *    [long query count]/[long query time]
   *    10/60.0
   * */
  {FIELD_LONG_QUERY, 5 + 1 + 4, "LONG-Q", FIELD_RIGHT_ALIGN},
  {FIELD_ERROR_QUERIES, 13, "ERR-Q", FIELD_RIGHT_ALIGN},
  {FIELD_UNIQUE_ERROR_QUERIES, 13, "UNIQUE-ERR-Q", FIELD_RIGHT_ALIGN},
  {FIELD_CANCELED, 10, "CANCELED", FIELD_RIGHT_ALIGN},
  {FIELD_ACCESS_MODE, 13, "ACCESS_MODE", FIELD_RIGHT_ALIGN},
  {FIELD_SQL_LOG, 9, "SQL_LOG", FIELD_RIGHT_ALIGN},
  {FIELD_NUMBER_OF_CONNECTION, 9, "#CONNECT", FIELD_RIGHT_ALIGN},
  {FIELD_PROXY_ID, 8, "PROXY_ID", FIELD_RIGHT_ALIGN},
  {FIELD_SHARD_ID, 8, "SHARD_ID", FIELD_RIGHT_ALIGN},
  {FIELD_ID, 5, "ID", FIELD_RIGHT_ALIGN},
  {FIELD_LQS, 10, "LQS", FIELD_RIGHT_ALIGN},
  {FIELD_STATUS, 12, "STATUS", FIELD_LEFT_ALIGN},
  {FIELD_LAST_ACCESS_TIME, 19, "LAST ACCESS TIME", FIELD_RIGHT_ALIGN},
  {FIELD_DB_NAME, 16, "DB", FIELD_RIGHT_ALIGN},
  {FIELD_HOST, 16, "HOST", FIELD_RIGHT_ALIGN},
  {FIELD_LAST_CONNECT_TIME, 19, "LAST CONNECT TIME", FIELD_RIGHT_ALIGN},
  {FIELD_CLIENT_IP, 15, "CLIENT IP", FIELD_RIGHT_ALIGN},
  {FIELD_SQL_LOG_MODE, 15, "SQL_LOG_MODE", FIELD_RIGHT_ALIGN},
  {FIELD_TRANSACTION_STIME, 19, "TRANSACTION STIME", FIELD_RIGHT_ALIGN},
  {FIELD_CONNECT, 9, "#CONNECT", FIELD_RIGHT_ALIGN},
  {FIELD_RESTART, 9, "#RESTART", FIELD_RIGHT_ALIGN},
  {FIELD_STMT_Q_SIZE, 7, "STMT-Q", FIELD_RIGHT_ALIGN},
  {FIELD_SHARD_Q_SIZE, 7, "SHARD-Q", FIELD_RIGHT_ALIGN}
};

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
  UINT64 num_tx;
  UINT64 num_qx;
  UINT64 num_lt;
  UINT64 num_lq;
  UINT64 num_eq;
  UINT64 num_eq_ui;
  UINT64 num_interrupt;
  UINT64 tps;
  UINT64 qps;
  UINT64 lts;
  UINT64 lqs;
  UINT64 eqs_ui;
  UINT64 eqs;
  UINT64 its;
  UINT64 num_select_query;
  UINT64 num_insert_query;
  UINT64 num_update_query;
  UINT64 num_delete_query;
  UINT64 num_others_query;
#if defined(CUBRID_SHARD)
  UINT64 num_hnqx;
  UINT64 num_hkqx;
  UINT64 num_hiqx;
  UINT64 num_haqx;
  UINT64 hnqps;
  UINT64 hkqps;
  UINT64 hiqps;
  UINT64 haqps;
#endif				/* CUBRID_SHARD */
};

#if defined(CUBRID_SHARD)
typedef struct shard_stat_item SHARD_STAT_ITEM;
struct shard_stat_item
{
  int shard_id;

  INT64 num_hint_key_queries_requested;
  INT64 num_hint_id_queries_requested;
  INT64 num_no_hint_queries_requested;
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
static int print_title (char *buf_p, int buf_offset, FIELD_NAME name,
			const char *new_title_p);
static void print_value (FIELD_NAME name, const void *value, FIELD_TYPE type);
static const char *get_access_mode_string (T_ACCESS_MODE_VALUE mode);
static const char *get_sql_log_mode_string (T_SQL_LOG_MODE_VALUE mode);
static const char *get_status_string (T_APPL_SERVER_INFO * as_info_p,
				      char appl_server);
static void get_cpu_usage_string (char *buf_p, float usage);


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
static char service_filter_value = SERVICE_UNKNOWN;
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
      return 3;
    }

  err = broker_config_read (NULL, br_info, &num_broker, &master_shm_id, NULL,
			    0, NULL, NULL, NULL);
  if (err < 0)
    {
      return 2;
    }

  ut_cd_work_dir ();

  shm_br =
    (T_SHM_BROKER *) uw_shm_open (master_shm_id, SHM_BROKER,
				  SHM_MODE_MONITOR);
  if (shm_br == NULL)
    {
      /* This means we have to launch broker */
      fprintf (stdout, "master shared memory open error[0x%x]\r\n",
	       master_shm_id);
      return 1;
    }
  if (shm_br->num_broker < 1 || shm_br->num_broker > MAX_BROKER_NUM)
    {
      fprintf (stderr, "broker configuration error\r\n");
      return 3;
    }

  br_vector = (char *) malloc (shm_br->num_broker);
  if (br_vector == NULL)
    {
      fprintf (stderr, "memory allocation error\r\n");
      return 3;
    }
  for (i = 0; i < shm_br->num_broker; i++)
    {
      br_vector[i] = 0;
    }

  if (get_args (argc, argv, br_vector) < 0)
    {
      free (br_vector);
      return 3;
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

  return 0;
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
  printf ("\t<expr> part of broker name or SERVICE=[ON|OFF]\n");
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
  service_filter_value = SERVICE_UNKNOWN;
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
      if (br_name_opt_flag == false)
	{
	  if (strncasecmp (argv[optind], "SERVICE=", strlen ("SERVICE=")) ==
	      0)
	    {
	      char *value_p;
	      value_p = argv[optind] + strlen ("SERVICE=");
	      if (strcasecmp (value_p, "ON") == 0)
		{
		  service_filter_value = SERVICE_ON;
		  break;
		}
	      else if (strcasecmp (value_p, "OFF") == 0)
		{
		  service_filter_value = SERVICE_OFF;
		  break;
		}
	      else
		{
		  print_usage ();
		  return -1;
		}
	    }
	}

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

  if (localtime_r (&t, &s_tm) == NULL)
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
  UINT64 qps;
  UINT64 lqs;
  int col_len;
#if !defined (CUBRID_SHARD)
  time_t tran_start_time;
  char ip_str[16];
#endif /* CUBRID_SHARD */

  int as_id;
  int proxy_id;
#if !defined (WINDOWS)
  int psize;
#endif
  char buf[256];

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

      if (as_info_p->uts_status == UTS_STATUS_BUSY
	  && IS_APPL_SERVER_TYPE_CAS (shm_br->br_info[br_index].appl_server)
	  && as_info_p->con_status == CON_STATUS_OUT_TRAN)
	{
	  return;
	}
    }

  col_len = 0;
  as_id = as_index + 1;
#if defined(CUBRID_SHARD)
  proxy_id = proxy_index + 1;
  print_value (FIELD_PROXY_ID, &proxy_id, FIELD_T_INT);
  print_value (FIELD_SHARD_ID, &shard_index, FIELD_T_INT);
  print_value (FIELD_ID, &as_id, FIELD_T_INT);
#else
  print_value (FIELD_ID, &as_id, FIELD_T_INT);
#endif /* CUBRID_SHARD */
  print_value (FIELD_PID, &as_info_p->pid, FIELD_T_INT);
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

  print_value (FIELD_QPS, &qps, FIELD_T_UINT64);
  print_value (FIELD_LQS, &lqs, FIELD_T_UINT64);
#if defined(WINDOWS)
  print_value (FIELD_PORT, &(as_info_p->as_port), FIELD_T_INT);
#endif
#if defined(WINDOWS)
  if (shm_appl->use_pdh_flag == TRUE)
    {
      print_value (FIELD_PSIZE, &(as_info_p->pdh_workset), FIELD_T_INT);
    }
#else
  psize = getsize (as_info_p->pid);
  print_value (FIELD_PSIZE, &psize, FIELD_T_INT);
#endif
  print_value (FIELD_STATUS,
	       get_status_string (as_info_p,
				  shm_br->br_info[br_index].appl_server),
	       FIELD_T_STRING);

#ifdef GET_PSINFO
  get_psinfo (as_info_p->pid, &proc_info);

  get_cpu_usage_string (buf, proc_info.pcpu);
  print_value (FIELD_CPU_USAGE, buf, FIELD_T_STRING);

  time_format (proc_info.cpu_time, time_str);
  print_value (FIELD_CPU_TIME, time_str, FIELD_T_STRING);
#elif WINDOWS
  if (shm_appl->use_pdh_flag == TRUE)
    {
      get_cpu_usage_string (buf, as_info_p->pdh_pct_cpu);
      print_value (FIELD_CPU_USAGE, buf, FIELD_T_STRING);
    }
#endif

  if (full_info_flag)
    {
      print_value (FIELD_LAST_ACCESS_TIME, &(as_info_p->last_access_time),
		   FIELD_T_TIME);
      if (as_info_p->database_name[0] != '\0')
	{
	  print_value (FIELD_DB_NAME, as_info_p->database_name,
		       FIELD_T_STRING);
	  print_value (FIELD_HOST, as_info_p->database_host, FIELD_T_STRING);
	  print_value (FIELD_LAST_CONNECT_TIME,
		       &(as_info_p->last_connect_time), FIELD_T_TIME);
	}
      else
	{
	  print_value (FIELD_DB_NAME, (char *) "-", FIELD_T_STRING);
	  print_value (FIELD_HOST, (char *) "-", FIELD_T_STRING);
	  print_value (FIELD_LAST_CONNECT_TIME, (char *) "-", FIELD_T_STRING);
	}

#if !defined(CUBRID_SHARD)
      print_value (FIELD_CLIENT_IP,
		   ut_get_ipv4_string (ip_str, sizeof (ip_str),
				       as_info_p->cas_clt_ip),
		   FIELD_T_STRING);
#endif /* !CUBRID_SHARD */
      if (as_info_p->cur_sql_log_mode != shm_appl->sql_log_mode)
	{
	  print_value (FIELD_SQL_LOG_MODE,
		       get_sql_log_mode_string (as_info_p->cur_sql_log_mode),
		       FIELD_T_STRING);
	}
      else
	{
	  print_value (FIELD_SQL_LOG_MODE, (char *) "-", FIELD_T_STRING);
	}

#if !defined(CUBRID_SHARD)
      tran_start_time = as_info_p->transaction_start_time;
      if (tran_start_time != (time_t) 0)
	{
	  print_value (FIELD_TRANSACTION_STIME, &tran_start_time,
		       FIELD_T_TIME);
	}
      else
	{
	  print_value (FIELD_TRANSACTION_STIME, (char *) "-", FIELD_T_STRING);
	}
      print_value (FIELD_CONNECT, &(as_info_p->num_connect_requests),
		   FIELD_T_INT);
      print_value (FIELD_RESTART, &(as_info_p->num_restarts), FIELD_T_INT);
#endif /* !CUBRID_SHARD */
    }

  print_newline ();
  if (as_info_p->uts_status == UTS_STATUS_BUSY)
    {
      str_out ("SQL: %s", as_info_p->log_msg);
      print_newline ();
    }
}

static int
appl_monitor (char *br_vector)
{
  T_MAX_HEAP_NODE job_queue[JOB_QUEUE_MAX_SIZE + 1];
  T_SHM_APPL_SERVER *shm_appl;
  int i, j, k, appl_offset;
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
	  if (shm_br->br_info[i].service_flag == SERVICE_ON)
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
	{
	  continue;
	}

      if (service_filter_value != SERVICE_UNKNOWN
	  && service_filter_value != shm_br->br_info[i].service_flag)
	{
	  continue;
	}

      str_out ("%% %s", shm_br->br_info[i].name);

      if (shm_br->br_info[i].service_flag == SERVICE_ON)
	{
#if defined(CUBRID_SHARD)
	  shm_as_cp =
	    (char *) uw_shm_open (shm_br->br_info[i].appl_server_shm_id,
				  SHM_APPL_SERVER, SHM_MODE_MONITOR);
	  shm_appl = shard_shm_get_appl_server (shm_as_cp);
	  if (shm_as_cp == NULL || shm_appl == NULL)
#else
	  shm_appl =
	    (T_SHM_APPL_SERVER *) uw_shm_open (shm_br->br_info[i].
					       appl_server_shm_id,
					       SHM_APPL_SERVER,
					       SHM_MODE_MONITOR);
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
		}
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
			   cas_index < shard_info_p->max_appl_server;
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
	  str_out ("%c%s", FIELD_DELIMITER, "OFF");
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
  int i, j;
  char buf[1024];
  UINT64 num_connect;
  int buf_offset;
#ifdef GET_PSINFO
  T_PSINFO proc_info;
  char time_str[32];
#endif
  static BR_MONITORING_ITEM *br_mnt_olds = NULL;
  static time_t time_old;
  time_t time_cur;
  UINT64 num_req;
  UINT64 num_tx_cur = 0, num_qx_cur = 0, num_interrupts_cur = 0;
  UINT64 num_lt_cur = 0, num_lq_cur = 0, num_eq_cur = 0;
  UINT64 num_eq_ui_cur = 0;
  UINT64 lts = 0, lqs = 0, eqs = 0, its = 0;
  UINT64 eqs_ui = 0;
  UINT64 tps = 0, qps = 0;
  static unsigned int tty_print_header = 0;
  double elapsed_time;
  UINT64 num_select_query_cur = 0;
  UINT64 num_insert_query_cur = 0;
  UINT64 num_update_query_cur = 0;
  UINT64 num_delete_query_cur = 0;
  UINT64 num_others_query_cur = 0;
  UINT64 num_select_query = 0;
  UINT64 num_insert_query = 0;
  UINT64 num_update_query = 0;
  UINT64 num_delete_query = 0;
  UINT64 num_others_query = 0;

#if defined(CUBRID_SHARD)
  UINT64 num_stmt_q = 0, num_shard_q = 0;
  char *shm_as_cp = NULL;

  T_SHM_PROXY *shm_proxy_p = NULL;
  T_PROXY_INFO *proxy_info_p = NULL;
  T_SHARD_INFO *shard_info_p = NULL;

  INT64 num_hnqx_cur = 0, num_hkqx_cur = 0, num_hiqx_cur = 0, num_haqx_cur =
    0;
  INT64 hnqps = 0, hkqps = 0, hiqps = 0, haqps = 0;
  INT64 total_kqps = 0;
#endif /* CUBRID_SHARD */

  T_APPL_SERVER_INFO *as_info_p = NULL;

  buf_offset = 0;
  buf_offset = print_title (buf, buf_offset, FIELD_BROKER_NAME, NULL);
  buf_offset = print_title (buf, buf_offset, FIELD_PID, NULL);
  if (full_info_flag)
    {
      buf_offset = print_title (buf, buf_offset, FIELD_PSIZE, NULL);
    }
  buf_offset = print_title (buf, buf_offset, FIELD_PORT, NULL);
#if defined(CUBRID_SHARD)
  buf_offset = print_title (buf, buf_offset, FIELD_ACTIVE_P, NULL);
  buf_offset = print_title (buf, buf_offset, FIELD_ACTIVE_C, NULL);

  buf_offset = print_title (buf, buf_offset, FIELD_STMT_Q_SIZE, NULL);
  buf_offset = print_title (buf, buf_offset, FIELD_SHARD_Q_SIZE, NULL);
#else
  if (full_info_flag)
    {
      char field_title_with_interval[256];

      buf_offset = print_title (buf, buf_offset,
				FIELD_APPL_SERVER_NUM_TOTAL, (char *) "AS(T");
      buf_offset = print_title (buf, buf_offset,
				FIELD_APPL_SERVER_NUM_CLIENT_WAIT, NULL);
      buf_offset = print_title (buf, buf_offset,
				FIELD_APPL_SERVER_NUM_BUSY, NULL);
      sprintf (field_title_with_interval, "%d%s", state_interval, "s-W");
      buf_offset = print_title (buf, buf_offset,
				FIELD_APPL_SERVER_NUM_CLIENT_WAIT_IN_SEC,
				field_title_with_interval);
      sprintf (field_title_with_interval, "%d%s", state_interval, "s-B)");
      buf_offset = print_title (buf, buf_offset,
				FIELD_APPL_SERVER_NUM_BUSY_IN_SEC,
				field_title_with_interval);
    }
  else
    {
      buf_offset = print_title (buf, buf_offset,
				FIELD_APPL_SERVER_NUM_TOTAL, (char *) "AS");
    }

  buf_offset = print_title (buf, buf_offset, FIELD_JOB_QUEUE_ID, NULL);
#endif /* CUBRID_SHARD */

#ifdef GET_PSINFO
  buf_offset = print_title (buf, buf_offset, FIELD_THREAD, NULL);
  buf_offset = print_title (buf, buf_offset, FIELD_CPU_USAGE, NULL);
  buf_offset = print_title (buf, buf_offset, FIELD_CPU_TIME, NULL);
#endif

  buf_offset = print_title (buf, buf_offset, FIELD_TPS, NULL);
  buf_offset = print_title (buf, buf_offset, FIELD_QPS, NULL);
  if (full_info_flag == false)
    {
      buf_offset = print_title (buf, buf_offset, FIELD_NUM_OF_SELECT_QUERIES,
				NULL);
      buf_offset = print_title (buf, buf_offset, FIELD_NUM_OF_INSERT_QUERIES,
				NULL);
      buf_offset = print_title (buf, buf_offset, FIELD_NUM_OF_UPDATE_QUERIES,
				NULL);
      buf_offset = print_title (buf, buf_offset, FIELD_NUM_OF_DELETE_QUERIES,
				NULL);
      buf_offset = print_title (buf, buf_offset, FIELD_NUM_OF_OTHERS_QUERIES,
				NULL);
    }
#if defined(CUBRID_SHARD)
  buf_offset = print_title (buf, buf_offset, FIELD_K_QPS, NULL);
  if (full_info_flag)
    {
      buf_offset = print_title (buf, buf_offset, FIELD_H_KEY, NULL);
      buf_offset = print_title (buf, buf_offset, FIELD_H_ID, NULL);
      buf_offset = print_title (buf, buf_offset, FIELD_H_ALL, NULL);
    }
  buf_offset = print_title (buf, buf_offset, FIELD_NK_QPS, NULL);
#endif /* CUBRID_SHARD */
  buf_offset = print_title (buf, buf_offset, FIELD_LONG_TRANSACTION, NULL);
  buf_offset = print_title (buf, buf_offset, FIELD_LONG_QUERY, NULL);
  buf_offset = print_title (buf, buf_offset, FIELD_ERROR_QUERIES, NULL);
  buf_offset = print_title (buf, buf_offset, FIELD_UNIQUE_ERROR_QUERIES,
			    NULL);
  if (full_info_flag)
    {
      buf_offset = print_title (buf, buf_offset, FIELD_CANCELED, NULL);
      buf_offset = print_title (buf, buf_offset, FIELD_ACCESS_MODE, NULL);
      buf_offset = print_title (buf, buf_offset, FIELD_SQL_LOG, NULL);
    }
  buf_offset = print_title (buf, buf_offset, FIELD_NUMBER_OF_CONNECTION,
			    NULL);

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
#else /* !CUBRID_SHARD */
      time_t cur_time;
#endif /* CUBRID_SHARD */
      char shortened_broker_name[FIELD_WIDTH_BROKER_NAME + 1];

      if (br_vector[i] == 0)
	{
	  continue;
	}

      if (service_filter_value != SERVICE_UNKNOWN
	  && service_filter_value != shm_br->br_info[i].service_flag)
	{
	  continue;
	}

      if (strlen (shm_br->br_info[i].name) <= FIELD_WIDTH_BROKER_NAME)
	{
	  sprintf (shortened_broker_name, "%s", shm_br->br_info[i].name);
	}
      else
	{
	  sprintf (shortened_broker_name, "%.*s...",
		   FIELD_WIDTH_BROKER_NAME - 3, shm_br->br_info[i].name);
	}

      str_out ("*%c", FIELD_DELIMITER);
      print_value (FIELD_BROKER_NAME, shortened_broker_name, FIELD_T_STRING);
      if (shm_br->br_info[i].service_flag == SERVICE_ON)
	{
#if defined(CUBRID_SHARD)
	  shm_as_cp =
	    (char *) uw_shm_open (shm_br->br_info[i].
				  appl_server_shm_id,
				  SHM_APPL_SERVER, SHM_MODE_MONITOR);
	  shm_appl = shard_shm_get_appl_server (shm_as_cp);
	  if (shm_as_cp == NULL || shm_appl == NULL)
#else
	  shm_appl =
	    (T_SHM_APPL_SERVER *) uw_shm_open (shm_br->
					       br_info[i].
					       appl_server_shm_id,
					       SHM_APPL_SERVER,
					       SHM_MODE_MONITOR);
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
	      num_connect = 0;
#if !defined(CUBRID_SHARD)
	      cur_time = time (NULL);

	      for (j = 0; j < shm_br->br_info[i].appl_server_max_num; j++)
		{
		  as_info_p = &(shm_appl->as_info[j]);

		  if (as_info_p->service_flag != ON)
		    {
		      continue;
		    }

		  num_req += as_info_p->num_request;
		  num_connect += as_info_p->num_connect_requests;
		  if (full_info_flag)
		    {
		      bool time_expired =
			(cur_time - as_info_p->last_access_time >=
			 state_interval);

		      if (as_info_p->uts_status == UTS_STATUS_BUSY
			  && as_info_p->con_status != CON_STATUS_OUT_TRAN)
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

	      print_value (FIELD_PID, &(shm_br->br_info[i].pid), FIELD_T_INT);
	      if (full_info_flag)
		{
#if defined(WINDOWS)
		  if (shm_appl->use_pdh_flag == TRUE)
		    {
		      print_value (FIELD_PSIZE,
				   &(shm_br->br_info[i].pdh_workset),
				   FIELD_T_INT);
		    }
#else
		  int process_size;

		  process_size = getsize (shm_br->br_info[i].pid);
		  print_value (FIELD_PSIZE, &process_size, FIELD_T_INT);
#endif
		}

	      print_value (FIELD_PORT, &(shm_br->br_info[i].port),
			   FIELD_T_INT);
#if !defined(CUBRID_SHARD)
	      print_value (FIELD_APPL_SERVER_NUM_TOTAL,
			   &(shm_br->br_info[i].appl_server_num),
			   FIELD_T_INT);
	      if (full_info_flag)
		{

		  print_value (FIELD_APPL_SERVER_NUM_CLIENT_WAIT,
			       &num_client_wait, FIELD_T_INT);
		  print_value (FIELD_APPL_SERVER_NUM_BUSY,
			       &num_busy, FIELD_T_INT);
		  print_value (FIELD_APPL_SERVER_NUM_CLIENT_WAIT_IN_SEC,
			       &num_client_wait_nsec, FIELD_T_INT);
		  print_value (FIELD_APPL_SERVER_NUM_BUSY_IN_SEC,
			       &num_busy_nsec, FIELD_T_INT);
		}

	      print_value (FIELD_JOB_QUEUE_ID,
			   &(shm_appl->job_queue[0].id), FIELD_T_INT);
#ifdef GET_PSINFO
	      get_psinfo (shm_br->br_info[i].pid, &proc_info);

	      print_value (FIELD_THREAD, &(proc_info.num_thr), FIELD_T_INT);

	      get_cpu_usage_string (buf, proc_info.pcpu);
	      print_value (FIELD_CPU_USAGE, buf, FIELD_T_STRING);

	      time_format (proc_info.cpu_time, time_str);
	      print_value (FIELD_CPU_TIME, &time_str, FIELD_T_STRING);
#endif
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
	      num_eq_ui_cur = 0;
	      num_interrupts_cur = 0;
	      num_select_query_cur = 0;
	      num_insert_query_cur = 0;
	      num_update_query_cur = 0;
	      num_delete_query_cur = 0;
	      num_others_query_cur = 0;

#if defined(CUBRID_SHARD)
	      tot_cas = tot_proxy = 0;
	      num_stmt_q = num_shard_q = 0;
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
			  num_eq_ui_cur +=
			    as_info_p->num_unique_error_queries;
			  num_interrupts_cur += as_info_p->num_interrupts;
			  num_select_query_cur +=
			    as_info_p->num_select_queries;
			  num_insert_query_cur +=
			    as_info_p->num_insert_queries;
			  num_update_query_cur +=
			    as_info_p->num_update_queries;
			  num_delete_query_cur +=
			    as_info_p->num_delete_queries;

			}
		      num_others_query_cur = (num_qx_cur -
					      num_select_query_cur -
					      num_insert_query_cur -
					      num_update_query_cur -
					      num_delete_query_cur);

		      tot_cas += cas_index;
		      num_shard_q += shard_info_p->waiter_count;
		    }		/* SHARD */
		  num_hnqx_cur +=
		    proxy_info_p->num_hint_none_queries_processed;
		  num_hkqx_cur +=
		    proxy_info_p->num_hint_key_queries_processed;
		  num_hiqx_cur += proxy_info_p->num_hint_id_queries_processed;
		  num_haqx_cur +=
		    proxy_info_p->num_hint_all_queries_processed;
		  num_stmt_q += proxy_info_p->stmt_waiter_count;
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
		  num_eq_ui_cur += as_info_p->num_unique_error_queries;
		  num_interrupts_cur += as_info_p->num_interrupts;
		  num_select_query_cur += as_info_p->num_select_queries;
		  num_insert_query_cur += as_info_p->num_insert_queries;
		  num_update_query_cur += as_info_p->num_update_queries;
		  num_delete_query_cur += as_info_p->num_delete_queries;
		}		/* CAS */
	      num_others_query_cur = (num_qx_cur -
				      num_select_query_cur -
				      num_insert_query_cur -
				      num_update_query_cur -
				      num_delete_query_cur);
#endif /* CUBRID_SHARD */

	      if (elapsed_time > 0)
		{
		  tps = (num_tx_cur - br_mnt_olds[i].num_tx) / elapsed_time;
		  qps = (num_qx_cur - br_mnt_olds[i].num_qx) / elapsed_time;
		  lts = num_lt_cur - br_mnt_olds[i].num_lt;
		  lqs = num_lq_cur - br_mnt_olds[i].num_lq;
		  eqs = num_eq_cur - br_mnt_olds[i].num_eq;
		  eqs_ui = num_eq_ui_cur - br_mnt_olds[i].num_eq_ui;
		  its = num_interrupts_cur - br_mnt_olds[i].num_interrupt;
		  num_select_query =
		    num_select_query_cur - br_mnt_olds[i].num_select_query;
		  num_insert_query =
		    num_insert_query_cur - br_mnt_olds[i].num_insert_query;
		  num_update_query =
		    num_update_query_cur - br_mnt_olds[i].num_update_query;
		  num_delete_query =
		    num_delete_query_cur - br_mnt_olds[i].num_delete_query;
		  num_others_query =
		    num_others_query_cur - br_mnt_olds[i].num_others_query;
#if defined(CUBRID_SHARD)
		  hnqps =
		    (num_hnqx_cur - br_mnt_olds[i].num_hnqx) / elapsed_time;
		  hkqps =
		    (num_hkqx_cur - br_mnt_olds[i].num_hkqx) / elapsed_time;
		  hiqps =
		    (num_hiqx_cur - br_mnt_olds[i].num_hiqx) / elapsed_time;
		  haqps =
		    (num_haqx_cur - br_mnt_olds[i].num_haqx) / elapsed_time;
#endif /* CUBRID_SHARD */
		  br_mnt_olds[i].num_tx = num_tx_cur;
		  br_mnt_olds[i].num_qx = num_qx_cur;
		  br_mnt_olds[i].num_lt = num_lt_cur;
		  br_mnt_olds[i].num_lq = num_lq_cur;
		  br_mnt_olds[i].num_eq = num_eq_cur;
		  br_mnt_olds[i].num_eq_ui = num_eq_ui_cur;
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
		  br_mnt_olds[i].eqs_ui = eqs_ui;
		  br_mnt_olds[i].its = its;
		  br_mnt_olds[i].num_select_query = num_select_query_cur;
		  br_mnt_olds[i].num_insert_query = num_insert_query_cur;
		  br_mnt_olds[i].num_update_query = num_update_query_cur;
		  br_mnt_olds[i].num_delete_query = num_delete_query_cur;
		  br_mnt_olds[i].num_others_query = num_others_query_cur;
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
		  eqs_ui = br_mnt_olds[i].eqs_ui;
		  its = br_mnt_olds[i].its;
		  num_select_query = br_mnt_olds[i].num_select_query;
		  num_insert_query = br_mnt_olds[i].num_insert_query;
		  num_update_query = br_mnt_olds[i].num_update_query;
		  num_delete_query = br_mnt_olds[i].num_delete_query;
		  num_others_query = br_mnt_olds[i].num_others_query;
#if defined(CUBRID_SHARD)
		  hnqps = br_mnt_olds[i].hnqps;
		  hkqps = br_mnt_olds[i].hkqps;
		  hiqps = br_mnt_olds[i].hiqps;
		  haqps = br_mnt_olds[i].haqps;
#endif /* CUBRID_SHARD */
		}

#if defined(CUBRID_SHARD)
	      print_value (FIELD_ACTIVE_P, &tot_proxy, FIELD_T_INT);
	      print_value (FIELD_ACTIVE_C, &tot_cas, FIELD_T_INT);

	      print_value (FIELD_STMT_Q_SIZE, &num_stmt_q, FIELD_T_INT);
	      print_value (FIELD_SHARD_Q_SIZE, &num_shard_q, FIELD_T_INT);
#endif /* CUBRID_SHARD */

	      print_value (FIELD_TPS, &tps, FIELD_T_UINT64);
	      print_value (FIELD_QPS, &qps, FIELD_T_UINT64);

	      if (full_info_flag == false)
		{
		  print_value (FIELD_NUM_OF_SELECT_QUERIES,
			       &num_select_query, FIELD_T_UINT64);
		  print_value (FIELD_NUM_OF_INSERT_QUERIES,
			       &num_insert_query, FIELD_T_UINT64);
		  print_value (FIELD_NUM_OF_UPDATE_QUERIES,
			       &num_update_query, FIELD_T_UINT64);
		  print_value (FIELD_NUM_OF_DELETE_QUERIES,
			       &num_delete_query, FIELD_T_UINT64);
		  print_value (FIELD_NUM_OF_OTHERS_QUERIES,
			       &num_others_query, FIELD_T_UINT64);
		}

#if defined(CUBRID_SHARD)
	      total_kqps = hkqps + hiqps + haqps;
	      print_value (FIELD_K_QPS, &total_kqps, FIELD_T_INT64);
	      if (full_info_flag)
		{
		  print_value (FIELD_H_KEY, &hkqps, FIELD_T_INT64);
		  print_value (FIELD_H_ID, &hiqps, FIELD_T_INT64);
		  print_value (FIELD_H_ALL, &haqps, FIELD_T_INT64);
		}
	      print_value (FIELD_NK_QPS, &hnqps, FIELD_T_INT64);
#endif /* CUBRID_SHARD */
	      sprintf (buf, "%lu/%-.1f", lts,
		       (shm_appl->long_transaction_time / 1000.0));
	      print_value (FIELD_LONG_TRANSACTION, buf, FIELD_T_STRING);
	      sprintf (buf, "%lu/%-.1f", lqs,
		       (shm_appl->long_query_time / 1000.0));
	      print_value (FIELD_LONG_QUERY, buf, FIELD_T_STRING);
	      print_value (FIELD_ERROR_QUERIES, &eqs, FIELD_T_UINT64);
	      print_value (FIELD_UNIQUE_ERROR_QUERIES, &eqs_ui,
			   FIELD_T_UINT64);
	      if (full_info_flag)
		{
		  print_value (FIELD_CANCELED, &its, FIELD_T_INT64);
		  print_value (FIELD_ACCESS_MODE,
			       get_access_mode_string (shm_br->br_info[i].
						       access_mode),
			       FIELD_T_STRING);

		  print_value (FIELD_SQL_LOG,
			       get_sql_log_mode_string (shm_br->br_info[i].
							sql_log_mode),
			       FIELD_T_STRING);
		}

	      print_value (FIELD_NUMBER_OF_CONNECTION, &num_connect,
			   FIELD_T_UINT64);
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
	  str_out ("%s", "OFF");
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
  int buf_offset = 0;
  int i;

#if defined(CUBRID_SHARD)
  buf_offset = print_title (buf, buf_offset, FIELD_PROXY_ID, NULL);
  buf_offset = print_title (buf, buf_offset, FIELD_SHARD_ID, NULL);
  buf_offset = print_title (buf, buf_offset, FIELD_ID, NULL);
#else
  buf_offset = print_title (buf, buf_offset, FIELD_ID, NULL);
#endif /* CUBRID_SHARD */
  buf_offset = print_title (buf, buf_offset, FIELD_PID, NULL);
  buf_offset = print_title (buf, buf_offset, FIELD_QPS, NULL);
  buf_offset = print_title (buf, buf_offset, FIELD_LQS, NULL);
#if defined(WINDOWS)
  buf_offset = print_title (buf, buf_offset, FIELD_PORT, NULL);
#endif
#if defined(WINDOWS)
  if (use_pdh_flag == true)
    {
      buf_offset = print_title (buf, buf_offset, FIELD_PSIZE, NULL);
    }
#else
  buf_offset = print_title (buf, buf_offset, FIELD_PSIZE, NULL);
#endif
  buf_offset = print_title (buf, buf_offset, FIELD_STATUS, NULL);
#if 0
  buf_offset = print_title (buf, buf_offset, FIELD_PORT, NULL);
#endif
#ifdef GET_PSINFO
  buf_offset = print_title (buf, buf_offset, FIELD_CPU_USAGE, NULL);
  buf_offset = print_title (buf, buf_offset, FIELD_CPU_TIME, NULL);
#elif WINDOWS
  if (use_pdh_flag == true)
    {
      buf_offset = print_title (buf, buf_offset, FIELD_CPU_USAGE, NULL);
    }
#endif
  if (full_info_flag)
    {
      buf_offset = print_title (buf, buf_offset, FIELD_LAST_ACCESS_TIME,
				NULL);
      buf_offset = print_title (buf, buf_offset, FIELD_DB_NAME, NULL);
      buf_offset = print_title (buf, buf_offset, FIELD_HOST, NULL);
      buf_offset = print_title (buf, buf_offset, FIELD_LAST_CONNECT_TIME,
				NULL);
#if !defined(CUBRID_SHARD)
      buf_offset = print_title (buf, buf_offset, FIELD_CLIENT_IP, NULL);
#endif /* CUBRID_SHARD */
      buf_offset = print_title (buf, buf_offset, FIELD_SQL_LOG_MODE, NULL);
#if !defined(CUBRID_SHARD)
      buf_offset = print_title (buf, buf_offset, FIELD_TRANSACTION_STIME,
				NULL);
      buf_offset = print_title (buf, buf_offset, FIELD_CONNECT, NULL);
      buf_offset = print_title (buf, buf_offset, FIELD_RESTART, NULL);
#endif /* CUBRID_SHARD */
    }

  for (i = 0; i < buf_offset; i++)
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
  COORD pos = {
    0, 0
  };
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
  T_SHM_SHARD_CONN_STAT *shard_stat_p;

  char *shm_as_cp = NULL;
  T_SHM_PROXY *shm_proxy_p = NULL;
  T_PROXY_INFO *proxy_info_p = NULL;

  SHARD_STAT_ITEM *shard_stat_items = NULL;
  KEY_STAT_ITEM *key_stat_items = NULL;

  int shard_stat_items_size;
  int key_stat_items_size;
  static SHARD_STAT_ITEM *shard_stat_items_old = NULL;
  static KEY_STAT_ITEM *key_stat_items_old = NULL;
  SHARD_STAT_ITEM *shard_stat_items_old_p = NULL;
  KEY_STAT_ITEM *key_stat_items_old_p = NULL;

  static time_t time_old;
  time_t time_cur;
  double elapsed_time;

  int i, j, k;
  int shmid;
  int col_len;
  char buf[1024];
  char line_buf[1024];
  int proxy_index;

  INT64 num_hint_key_qr;
  INT64 num_hint_id_qr;
  INT64 num_no_hint_qr;
  INT64 num_all_qr;

  INT64 num_range_qr;


  shard_stat_items_size = sizeof (SHARD_STAT_ITEM) * MAX_SHARD_CONN;
  key_stat_items_size = sizeof (KEY_STAT_ITEM) * MAX_SHARD_KEY;

  if (shard_stat_items_old == NULL)
    {
      shard_stat_items_old =
	(SHARD_STAT_ITEM *) calloc (shard_stat_items_size,
				    shm_br->num_broker);
      if (shard_stat_items_old == NULL)
	{
	  goto free_and_error;
	}
      memset ((void *) shard_stat_items_old, 0,
	      shard_stat_items_size * shm_br->num_broker);
    }

  if (key_stat_items_old == NULL)
    {
      key_stat_items_old =
	(KEY_STAT_ITEM *) calloc (key_stat_items_size, shm_br->num_broker);
      if (key_stat_items_old == NULL)
	{
	  goto free_and_error;
	}
      memset ((void *) key_stat_items_old, 0,
	      key_stat_items_size * shm_br->num_broker);

      (void) time (&time_old);
      time_old--;
    }

  (void) time (&time_cur);
  elapsed_time = difftime (time_cur, time_old);

  for (i = 0; i < shm_br->num_broker; i++)
    {
      if (shm_br->br_info[i].service_flag != SERVICE_ON)
	{
	  continue;
	}
      shmid = shm_br->br_info[i].metadata_shm_id;
      shm_metadata_cp =
	(char *) uw_shm_open (shmid, SHM_BROKER, SHM_MODE_MONITOR);
      if (shm_metadata_cp == NULL)
	{
	  str_out ("%s", "shared memory open error");
	  goto free_and_error;
	}

      shard_stat_items_old_p =
	(SHARD_STAT_ITEM *) (((char *) shard_stat_items_old) +
			     (shard_stat_items_size * i));
      key_stat_items_old_p =
	(KEY_STAT_ITEM *) (((char *) key_stat_items_old) +
			   (key_stat_items_size * i));

      shm_user_p = shard_metadata_get_user (shm_metadata_cp);
      shm_key_p = shard_metadata_get_key (shm_metadata_cp);
      shm_conn_p = shard_metadata_get_conn (shm_metadata_cp);

      str_out ("%% %s ", shm_br->br_info[i].name);
      str_out ("[%x] ", shmid);
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
	  goto free_and_error;
	}
      shm_proxy_p = shard_shm_get_proxy (shm_as_cp);
      if (shm_proxy_p == NULL)
	{
	  str_out ("%s", "shared memory open error");
	  goto free_and_error;
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
	  goto free_and_error;
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
	      goto free_and_error;
	    }
	  for (j = 0; j < proxy_info_p->num_shard_conn;
	       shard_stat_p = shard_shm_get_shard_stat (proxy_info_p, ++j))
	    {
	      shard_stat_items[j].num_hint_key_queries_requested +=
		shard_stat_p->num_hint_key_queries_requested;
	      shard_stat_items[j].num_hint_id_queries_requested +=
		shard_stat_p->num_hint_id_queries_requested;
	      shard_stat_items[j].num_no_hint_queries_requested +=
		shard_stat_p->num_no_hint_queries_requested;
	    }
	}			/* proxy_info loop */

      str_out ("SHARD STATISTICS ");
      print_newline ();

      col_len = 0;
      col_len += sprintf (buf + col_len, "%5s ", "ID");
      col_len += sprintf (buf + col_len, "%10s ", "NUM-KEY-Q");
      col_len += sprintf (buf + col_len, "%10s ", "NUM-ID-Q");
      col_len += sprintf (buf + col_len, "%15s ", "NUM-NO-HINT-Q");
      col_len += sprintf (buf + col_len, "%15s ", "SUM");

      for (k = 0; k < col_len; k++)
	{
	  line_buf[k] = '-';
	}
      line_buf[k] = '\0';

      str_out ("\t%s", buf);
      print_newline ();
      str_out ("\t%s", line_buf);
      print_newline ();

      for (j = 0; j < shm_conn_p->num_shard_conn; j++)
	{
	  if (elapsed_time > 0)
	    {
	      num_hint_key_qr =
		shard_stat_items[j].num_hint_key_queries_requested -
		shard_stat_items_old_p[j].num_hint_key_queries_requested;
	      num_hint_id_qr =
		shard_stat_items[j].num_hint_id_queries_requested -
		shard_stat_items_old_p[j].num_hint_id_queries_requested;
	      num_no_hint_qr =
		shard_stat_items[j].num_no_hint_queries_requested -
		shard_stat_items_old_p[j].num_no_hint_queries_requested;
	      num_all_qr = num_hint_key_qr + num_hint_id_qr + num_no_hint_qr;

	      num_hint_key_qr = num_hint_key_qr / elapsed_time;
	      num_hint_id_qr = num_hint_id_qr / elapsed_time;
	      num_no_hint_qr = num_no_hint_qr / elapsed_time;
	      num_all_qr = num_all_qr / elapsed_time;
	    }
	  else
	    {
	      num_hint_key_qr =
		shard_stat_items[j].num_hint_key_queries_requested;
	      num_hint_id_qr =
		shard_stat_items[j].num_hint_id_queries_requested;
	      num_no_hint_qr =
		shard_stat_items[j].num_no_hint_queries_requested;
	      num_all_qr = num_hint_key_qr + num_hint_id_qr + num_no_hint_qr;
	    }

	  str_out ("\t%5d %10" PRId64 " %10" PRId64 " %15" PRId64 " %15"
		   PRId64, j, num_hint_key_qr, num_hint_id_qr, num_no_hint_qr,
		   num_all_qr);

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
	      goto free_and_error;
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
		  goto free_and_error;
		}

	      for (j = 0;
		   j < proxy_info_p->num_shard_key;
		   key_stat_p = shard_shm_get_key_stat (proxy_info_p, ++j))
		{
		  for (k = 0; k < key_stat_p->num_key_range; k++)
		    {
		      key_stat_items[j].
			num_range_queries_requested[k] +=
			key_stat_p->stat[k].num_range_queries_requested;
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
		{
		  line_buf[k] = '-';
		}
	      line_buf[k] = '\0';

	      str_out ("\t%s", buf);
	      print_newline ();
	      str_out ("\t%s", line_buf);
	      print_newline ();

	      for (k = 0; k < key_p->num_key_range; k++)
		{
		  range_p = (T_SHARD_KEY_RANGE *) (&(key_p->range[k]));

		  if (elapsed_time > 0)
		    {
		      num_range_qr =
			key_stat_items[j].num_range_queries_requested[k] -
			key_stat_items_old_p[j].
			num_range_queries_requested[k];

		      num_range_qr = num_range_qr / elapsed_time;
		    }
		  else
		    {
		      num_range_qr =
			key_stat_items[j].num_range_queries_requested[k];
		    }

		  str_out ("\t%5d ~ %5d : %10d %9" PRId64, range_p->min,
			   range_p->max, range_p->shard_id, num_range_qr);
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

      if (shard_stat_items)
	{
	  memcpy ((void *) shard_stat_items_old_p, shard_stat_items,
		  sizeof (SHARD_STAT_ITEM) * shm_conn_p->num_shard_conn);
	}

      if (key_stat_items)
	{
	  memcpy ((void *) key_stat_items_old_p, key_stat_items,
		  sizeof (KEY_STAT_ITEM) * shm_key_p->num_shard_key);
	}

      uw_shm_detach (shm_metadata_cp);
      uw_shm_detach (shm_as_cp);

      shm_metadata_cp = shm_as_cp = NULL;
    }

  if (elapsed_time > 0)
    {
      time_old = time_cur;
    }

  return 0;

free_and_error:
  if (shard_stat_items != NULL)
    {
      free ((char *) shard_stat_items);
    }
  if (key_stat_items != NULL)
    {
      free ((char *) key_stat_items);
    }
  if (shm_metadata_cp != NULL)
    {
      uw_shm_detach (shm_metadata_cp);
    }
  if (shm_as_cp != NULL)
    {
      uw_shm_detach (shm_as_cp);
    }
  return -1;
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
      if (shm_br->br_info[i].service_flag != SERVICE_ON)
	{
	  continue;
	}

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
	  for (client_index = 0; client_index < proxy_info_p->max_context;
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
	      str_out ("   %04d/%02d/%02d %02d:%02d:%02d", ct1.tm_year,
		       ct1.tm_mon + 1, ct1.tm_mday, ct1.tm_hour, ct1.tm_min,
		       ct1.tm_sec);
	      if (client_info_p->req_time > 0)
		{
		  localtime_r (&client_info_p->req_time, &ct1);
		  ct1.tm_year += 1900;
		  str_out ("   %04d/%02d/%02d %02d:%02d:%02d", ct1.tm_year,
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
		  str_out ("   %04d/%02d/%02d %02d:%02d:%02d", ct1.tm_year,
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
      uw_shm_detach (shm_as_cp);
    }
  return 0;
}
#endif /* CUBRID_SHARD */

static int
print_title (char *buf_p, int buf_offset, FIELD_NAME name,
	     const char *new_title_p)
{
  struct status_field *field_p = NULL;
  const char *title_p = NULL;

  assert (buf_p != NULL);
  assert (buf_offset >= 0);

  field_p = &fields[name];

  if (new_title_p != NULL)
    {
      title_p = new_title_p;
    }
  else
    {
      title_p = field_p->title;
    }

  switch (field_p->name)
    {
    case FIELD_BROKER_NAME:
      buf_offset += sprintf (buf_p + buf_offset, "%c%c",
			     FIELD_DELIMITER, FIELD_DELIMITER);
      if (field_p->align == FIELD_LEFT_ALIGN)
	{
	  buf_offset += sprintf (buf_p + buf_offset, "%-*s",
				 field_p->width, title_p);
	}
      else
	{
	  buf_offset += sprintf (buf_p + buf_offset, "%*s",
				 field_p->width, title_p);
	}
      break;
    default:
      if (field_p->align == FIELD_LEFT_ALIGN)
	{
	  buf_offset += sprintf (buf_p + buf_offset, "%-*s",
				 field_p->width, title_p);
	}
      else
	{
	  buf_offset += sprintf (buf_p + buf_offset, "%*s",
				 field_p->width, title_p);
	}
      break;
    }
  buf_offset += sprintf (buf_p + buf_offset, "%c", FIELD_DELIMITER);

  return buf_offset;
}

static void
print_value (FIELD_NAME name, const void *value_p, FIELD_TYPE type)
{
  struct status_field *field_p = NULL;
  struct tm cur_tm;
  char time_buf[64];

  assert (value_p != NULL);

  field_p = &fields[name];

  switch (type)
    {
    case FIELD_T_INT:
      if (field_p->align == FIELD_LEFT_ALIGN)
	{
	  str_out ("%-*d", field_p->width, *(const int *) value_p);
	}
      else
	{
	  str_out ("%*d", field_p->width, *(const int *) value_p);
	}
      break;
    case FIELD_T_STRING:
      if (field_p->align == FIELD_LEFT_ALIGN)
	{
	  str_out ("%-*.*s", field_p->width, field_p->width,
		   (const char *) value_p);
	}
      else
	{
	  str_out ("%*.*s", field_p->width, field_p->width,
		   (const char *) value_p);
	}
      break;
    case FIELD_T_FLOAT:
      if (field_p->align == FIELD_LEFT_ALIGN)
	{
	  str_out ("%-*f", field_p->width, *(const float *) value_p);
	}
      else
	{
	  str_out ("%*f", field_p->width, *(const float *) value_p);
	}
      break;
    case FIELD_T_UINT64:
      if (field_p->align == FIELD_LEFT_ALIGN)
	{
	  str_out ("%-*lu", field_p->width, *(const UINT64 *) value_p);
	}
      else
	{
	  str_out ("%*lu", field_p->width, *(const UINT64 *) value_p);
	}
      break;
    case FIELD_T_INT64:
      if (field_p->align == FIELD_LEFT_ALIGN)
	{
	  str_out ("%-*ld", field_p->width, *(const INT64 *) value_p);
	}
      else
	{
	  str_out ("%*ld", field_p->width, *(const INT64 *) value_p);
	}
      break;
    case FIELD_T_TIME:
      localtime_r ((const time_t *) value_p, &cur_tm);
      cur_tm.tm_year += 1900;
      sprintf (time_buf, "%02d/%02d/%02d %02d:%02d:%02d", cur_tm.tm_year,
	       cur_tm.tm_mon + 1, cur_tm.tm_mday, cur_tm.tm_hour,
	       cur_tm.tm_min, cur_tm.tm_sec);
      if (field_p->align == FIELD_LEFT_ALIGN)
	{
	  str_out ("%-*s", field_p->width, time_buf);
	}
      else
	{
	  str_out ("%*s", field_p->width, time_buf);
	}
    default:
      break;
    }
  str_out ("%c", FIELD_DELIMITER);
}

static const char *
get_sql_log_mode_string (T_SQL_LOG_MODE_VALUE mode)
{
  switch (mode)
    {
    case SQL_LOG_MODE_NONE:
      return "NONE";
    case SQL_LOG_MODE_ERROR:
      return "ERROR";
    case SQL_LOG_MODE_TIMEOUT:
      return "TIMEOUT";
    case SQL_LOG_MODE_NOTICE:
      return "NOTICE";
    case SQL_LOG_MODE_ALL:
      return "ALL";
    default:
      return "-";
    }
}

static const char *
get_access_mode_string (T_ACCESS_MODE_VALUE mode)
{
  switch (mode)
    {
    case READ_ONLY_ACCESS_MODE:
      return "RO";
    case SLAVE_ONLY_ACCESS_MODE:
      return "SO";
    case READ_WRITE_ACCESS_MODE:
      return "RW";
    default:
      return "--";
    }
}

static const char *
get_status_string (T_APPL_SERVER_INFO * as_info_p, char appl_server)
{
  assert (as_info_p != NULL);

  if (as_info_p->uts_status == UTS_STATUS_BUSY)
    {
      if (IS_APPL_SERVER_TYPE_CAS (appl_server))
	{
	  if (as_info_p->con_status == CON_STATUS_OUT_TRAN)
	    {
	      return "CLOSE_WAIT";
	    }
	  else if (as_info_p->log_msg[0] == '\0')
	    {
	      return "CLIENT_WAIT";
	    }
	  else
	    {
	      return "BUSY";
	    }
	}
      else
	{
	  return "BUSY";
	}
    }
#if defined(WINDOWS)
  else if (as_info_p->uts_status == UTS_STATUS_BUSY_WAIT)
    {
      return "BUSY";
    }
#endif
  else if (as_info_p->uts_status == UTS_STATUS_RESTART)
    {
      return "INITIALIZE";
    }
#if defined(CUBRID_SHARD)
  else if (as_info_p->uts_status == UTS_STATUS_CON_WAIT)
    {
      return "CON WAIT";
    }
#endif
  else
    {
      return "IDLE";
    }

  return NULL;
}

static void
get_cpu_usage_string (char *buf_p, float usage)
{
  assert (buf_p != NULL);

  if (usage >= 0)
    {
      sprintf (buf_p, "%.2f", usage);
    }
  else
    {
      sprintf (buf_p, " - ");
    }
}
