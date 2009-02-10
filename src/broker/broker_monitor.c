/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution. 
 *
 *   This program is free software; you can redistribute it and/or modify 
 *   it under the terms of the GNU General Public License as published by 
 *   the Free Software Foundation; version 2 of the License. 
 *
 *  This program is distributed in the hope that it will be useful, 
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of 
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the 
 *  GNU General Public License for more details. 
 *
 *  You should have received a copy of the GNU General Public License 
 *  along with this program; if not, write to the Free Software 
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA 
 *
 */


/*
 * broker_monitor.c - 
 */

#ident "$Id$"

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <time.h>
#include <string.h>

#if defined(WIN32)
#include <winsock2.h>
#include <windows.h>
#include <conio.h>
#elif !defined(MONITOR2)
#include <curses.h>
#endif

#ifdef WIN32
#include <sys/timeb.h>
#else
#include <sys/types.h>
#include <regex.h>
#include <sys/time.h>
#endif

#include "cas_common.h"
#include "broker_config.h"
#include "broker_shm.h"
#include "broker_util.h"
#include "broker_process_size.h"
#include "porting.h"
#ifndef WIN32
#include "broker_process_info.h"
#endif

#ifdef WIN32
#include "broker_getopt.h"
#endif

#define		DEFAULT_CHECK_PERIOD		300	/* seconds */
#define		MAX_APPL_NUM		100

#ifdef WIN32
#define STR_TO_SCREEN(MSG)	\
	do {			\
		DWORD	size;	\
		WriteConsole(h_console, MSG, strlen(MSG), &size, NULL);	\
	} while (0)
#else
#define STR_TO_SCREEN(MSG)	addstr(MSG)
#endif

#if defined(MONITOR2)
#define STR_OUT(FMT, STR)	printf(FMT, STR)
#else
#define STR_OUT(FMT, STR)		\
	do {				\
	  char		out_buf[1024];	\
	  if (refresh_sec > 0) {	\
	    sprintf(out_buf, FMT, STR);	\
	    STR_TO_SCREEN(out_buf);	\
	  }				\
	  else {			\
	    printf(FMT, STR);		\
	  }				\
	} while (0)
#endif

#ifdef MONITOR2
#define PRINT_NEWLINE()		printf("\n");
#else
#define PRINT_NEWLINE()			\
	do {				\
	  if (refresh_sec > 0) {	\
	    clrtoeol();			\
	    STR_TO_SCREEN("\n");	\
	  }				\
	  else {			\
	    printf("\n");		\
	  }				\
	} while (0)
#endif

#ifdef WIN32
#define GET_CHAR(VAR)					\
	do {						\
	  int	i;					\
	  VAR = 0;					\
	  for (i=0 ; i < refresh_sec * 10 ; i++) {	\
	    if (_kbhit()) {				\
	      VAR = _getch();				\
	      break;					\
	    }						\
	    else {					\
	      SLEEP_MILISEC(0, 100);			\
	    }						\
	  }						\
	} while (0)
#else
#define GET_CHAR(VAR)		VAR = getch()
#endif

static int get_args (int argc, char *argv[], char *br_vector);
static void print_job_queue (T_MAX_HEAP_NODE *);
static void ip2str (unsigned char *ip, char *ip_str);
static void time2str (const time_t t, char *str);
static int appl_monitor (char *br_vector);
static int br_monitor (char *br_vector);
#ifdef GET_PSINFO
static void time_format (int t, char *time_str);
#endif
static void print_header (int check_period, char use_pdh_flag);

#ifdef WIN32
static void move (int x, int y);
static void refresh ();
static void clear ();
static void clrtobot ();
static void clrtoeol ();
static void endwin ();
#endif

#if defined(LINUX) || defined(ALPHA_LINUX)
extern char *optarg;
extern int optind, opterr, optopt;
#endif

static T_SHM_BROKER *shm_br;
static int check_period;
static char display_job_queue;
static int refresh_sec = 0;
static char br_monitor_flag = FALSE;
static int last_access_sec = 0;

static int max_col_len = 0;

#ifdef WIN32
HANDLE h_console;
CONSOLE_SCREEN_BUFFER_INFO scr_info;
#endif

int
main (int argc, char **argv)
{
  T_BROKER_INFO br_info[MAX_BROKER_NUM];
  char admin_log_file[256];
  int num_broker, master_shm_id;
  int err, i;
  char *br_vector;
#if defined(MONITOR2)
#elif defined(WIN32)
#else
  WINDOW *win;
#endif

  if (argc == 2 && strcmp (argv[1], "--version") == 0)
    {
      fprintf (stderr, "VERSION %s\n", makestring (BUILD_NUMBER));
      return 1;
    }

  ut_cd_work_dir ();

  err =
    broker_config_read (br_info, &num_broker, &master_shm_id, admin_log_file);

  if (err < 0)
    exit (1);

  shm_br =
    (T_SHM_BROKER *) uw_shm_open (master_shm_id, SHM_BROKER,
				  SHM_MODE_MONITOR);
  if (shm_br == NULL)
    {
      fprintf (stderr, "master shared memory open error\r\n");
      exit (1);
    }

  br_vector = (char *) malloc (shm_br->num_broker);
  if (br_vector == NULL)
    {
      fprintf (stderr, "memory allocation error\r\n");
      return 1;
    }
  for (i = 0; i < shm_br->num_broker; i++)
    br_vector[i] = 0;

  check_period = 0;
  display_job_queue = FALSE;

  if (get_args (argc, argv, br_vector) < 0)
    return 1;

  if (refresh_sec > 0)
    {
#if defined(MONITOR2)
      refresh_sec = 0;
#elif defined(WIN32)
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
      if (refresh_sec > 0)
	{
#ifndef MONITOR2
	  move (0, 0);
	  refresh ();
#endif
	}

#ifndef WIN32
      if (shm_br == NULL || shm_br->magic == 0)
	{
	  if (shm_br)
	    uw_shm_detach (shm_br);
	  shm_br =
	    (T_SHM_BROKER *) uw_shm_open (master_shm_id, SHM_BROKER,
					  SHM_MODE_MONITOR);
	}
      else
	{
#endif
	  if (br_monitor_flag == TRUE)
	    br_monitor (br_vector);
	  else
	    appl_monitor (br_vector);
#ifndef WIN32
	}
#endif

#if defined(MONITOR2)
      break;
#else
      if (refresh_sec > 0)
	{
	  int in_ch = 0;

	  refresh ();
	  clrtobot ();
	  move (0, 0);
	  refresh ();
	  GET_CHAR (in_ch);

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
      else
	{
	  break;
	}
#endif
    }				/* end of while(1) */

  uw_shm_detach (shm_br);

#ifndef MONITOR2
  if (refresh_sec > 0)
    endwin ();
#endif

  exit (0);
}

static int
get_args (int argc, char *argv[], char *br_vector)
{
  int c, j;
  int status;
  char br_name_opt_flag = FALSE, errflag = FALSE;
#ifndef WIN32
  regex_t re;
#endif

  while ((c = getopt (argc, argv, "bqt:s:l:")) != EOF)
    {
      switch (c)
	{
	case 't':
	  check_period = atoi (optarg);
	  break;
	case 'q':
	  display_job_queue = TRUE;
	  break;
	case 's':
	  refresh_sec = atoi (optarg);
	  break;
	case 'b':
	  br_monitor_flag = TRUE;
	  break;
	case 'l':
	  last_access_sec = atoi (optarg);
	  break;
	case '?':
	  errflag = TRUE;
	}
    }

  if (last_access_sec > 0)
    {
      check_period = 0;
    }

  if (errflag == TRUE)
    {
      return -1;
    }

  for (; optind < argc; optind++)
    {
      br_name_opt_flag = TRUE;
#ifndef WIN32
      if (regcomp (&re, argv[optind], 0) != 0)
	{
	  fprintf (stderr, "%s\r\n", argv[optind]);
	  return -1;
	}
#endif
      for (j = 0; j < shm_br->num_broker; j++)
	{
#ifdef WIN32
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
#ifndef WIN32
      regfree (&re);
#endif
    }

  if (br_name_opt_flag == FALSE)
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
      char time_str[16];

      if (max_heap_delete (job_queue, &item) < 0)
	break;

      if (first_flag)
	{
	  sprintf (outbuf, "%5s  %s%9s%13s%13s", "ID", "PRIORITY", "IP",
		   "TIME", "REQUEST");
	  STR_OUT ("%s", outbuf);
	  PRINT_NEWLINE ();
	  first_flag = 0;
	}

      ip2str (item.ip_addr, ip_str);
      time2str (item.recv_time, time_str);
      sprintf (outbuf, "%5d%7d%17s%10s   %s:%s",
	       item.id, item.priority, ip_str, time_str, item.script,
	       item.prg_name);
      STR_OUT ("%s", outbuf);
      PRINT_NEWLINE ();
    }
  if (!first_flag)
    PRINT_NEWLINE ();
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

  s_tm = *localtime (&t);
  sprintf (str, "%02d:%02d:%02d", s_tm.tm_hour, s_tm.tm_min, s_tm.tm_sec);
}

static int
appl_monitor (char *br_vector)
{
  struct tm *cur_tm;
  time_t last_access_time;
  int num_req_array[MAX_APPL_NUM];
  time_t sec_array[MAX_APPL_NUM];
  int msec_array[MAX_APPL_NUM];
  T_TIMEVAL cur_tv;
  T_MAX_HEAP_NODE job_queue[JOB_QUEUE_MAX_SIZE + 1];
  T_SHM_APPL_SERVER *shm_appl;
  int i, j;
  int col_len;
  char line_buf[1024];
  char use_pdh_flag;
  time_t current_time;
#ifdef GET_PSINFO
  T_PSINFO proc_info;
  char time_str[32];
#endif

  for (i = 0; i < shm_br->num_broker; i++)
    {

      if (br_vector[i] == 0)
	continue;

      STR_OUT ("%% %s ", shm_br->br_info[i].name);

      if (shm_br->br_info[i].service_flag == ON)
	{
	  shm_appl = (T_SHM_APPL_SERVER *)
	    uw_shm_open (shm_br->br_info[i].appl_server_shm_id,
			 SHM_APPL_SERVER, SHM_MODE_MONITOR);

	  if (shm_appl == NULL)
	    {
	      STR_OUT ("%s", "shared memory open error");
	      PRINT_NEWLINE ();
	    }
	  else
	    {
#ifdef WIN32
	      use_pdh_flag = shm_appl->use_pdh_flag;
#else
	      use_pdh_flag = 0;
#endif
	      if (shm_appl->suspend_mode != SUSPEND_NONE)
		{
		  STR_OUT ("%s", " SUSPENDED");
		  PRINT_NEWLINE ();
		  STR_OUT ("%s", "  ");
		}
	      STR_OUT (" - %s ", shm_appl->appl_server_name);
	      STR_OUT ("[%d,", shm_br->br_info[i].pid);
	      STR_OUT ("%d] ", shm_br->br_info[i].port);
	      STR_OUT ("%s ", shm_br->br_info[i].access_log_file);
	      STR_OUT ("%s ", shm_br->br_info[i].error_log_file);
	      PRINT_NEWLINE ();
	      STR_OUT ("%s", "  ");

	      if (display_job_queue == TRUE)
		{
		  memcpy (job_queue, shm_appl->job_queue,
			  sizeof (T_MAX_HEAP_NODE) * (JOB_QUEUE_MAX_SIZE +
						      1));
		  STR_OUT ("job_queue : %d, ", job_queue[0].id);
		}
	      else
		{
		  STR_OUT ("job_queue : %d, ", shm_appl->job_queue[0].id);
		}

	      if (shm_br->br_info[i].auto_add_appl_server == ON)
		STR_OUT ("%s", "AUTO-ADD-ON, ");
	      else
		STR_OUT ("%s", "AUTO-ADD-OFF, ");

	      if (shm_br->br_info[i].appl_server == APPL_SERVER_CAS)
		{
		  STR_OUT ("TIMEOUT:%d, ",
			   shm_br->br_info[i].session_timeout);
		}
	      else
		{
		  if (shm_br->br_info[i].session_flag == ON)
		    STR_OUT ("SESSION-ON(%d), ",
			     shm_br->br_info[i].session_timeout);
		  else
		    STR_OUT ("%s", "SESSION-OFF, ");
		}

	      if (shm_appl->sql_log_mode & SQL_LOG_MODE_ON)
		{
		  STR_OUT ("%s", "SQL-LOG-ON");
		  if (shm_appl->sql_log_mode ^ SQL_LOG_MODE_ON)
		    {
		      STR_OUT ("%s", ":");
		      if (shm_appl->sql_log_mode & SQL_LOG_MODE_APPEND)
			STR_OUT ("%s", "A");
		      if (shm_appl->sql_log_mode & SQL_LOG_MODE_BIND_VALUE)
			STR_OUT ("%s", "B");

		      if ((shm_appl->sql_log_mode & SQL_LOG_MODE_APPEND) &&
			  (shm_appl->sql_log_max_size > 0))
			{
			  STR_OUT (":%d", shm_appl->sql_log_max_size);
			}
		    }
		  if ((!(shm_appl->sql_log_mode & SQL_LOG_MODE_APPEND)) &&
		      (shm_appl->sql_log_time < SQL_LOG_TIME_MAX))
		    {
		      STR_OUT ("(%d)", shm_appl->sql_log_time);
		    }
		}
	      else
		STR_OUT ("%s", "SQL-LOG-OFF");

	      if (shm_appl->keep_connection == KEEP_CON_ON)
		{
		  STR_OUT (", %s", "KC:ON");
		}
	      else if (shm_appl->keep_connection == KEEP_CON_AUTO)
		{
		  STR_OUT (", %s", "KC:AUTO");
		}

	      PRINT_NEWLINE ();

	      print_header (check_period, use_pdh_flag);

	      TIMEVAL_MAKE (&cur_tv);

	      if (check_period > 0)
		{
		  for (j = 0; j < shm_br->br_info[i].appl_server_max_num; j++)
		    {
		      if (shm_appl->as_info[j].uts_status == UTS_STATUS_BUSY)
			{
			  sec_array[j] =
			    TIMEVAL_GET_SEC (&cur_tv) -
			    shm_appl->as_info[j].last_access_time;
			  if (sec_array[j] > check_period)
			    sec_array[j] = check_period;
			}
		      else
			sec_array[j] = 0;
		      msec_array[j] = 0;
		    }
		  ut_get_request_time (&cur_tv,
				       shm_br->br_info[i].appl_server_max_num,
				       shm_br->br_info[i].access_log_file,
				       sec_array, msec_array, check_period);

		  ut_get_num_request (&cur_tv,
				      shm_br->br_info[i].appl_server_max_num,
				      shm_br->br_info[i].access_log_file,
				      num_req_array, check_period);
		}

	      current_time = time (NULL);
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
#if 0
		  if (shm_appl->as_info[j].service_flag == SERVICE_ON)
		    col_len += sprintf (line_buf + col_len, "ON  ");
		  else
		    col_len += sprintf (line_buf + col_len, "OFF ");
#endif
		  col_len +=
		    sprintf (line_buf + col_len, "%5d ",
			     shm_appl->as_info[j].pid);
		  col_len +=
		    sprintf (line_buf + col_len, "%5d ",
			     shm_appl->as_info[j].num_request);
		  if (check_period > 0)
		    {
		      col_len +=
			sprintf (line_buf + col_len, "%5d ",
				 num_req_array[j]);
		      col_len +=
			sprintf (line_buf + col_len, "%3d.%d ", sec_array[j],
				 msec_array[j] / 100);
		    }
#ifdef WIN32
		  col_len +=
		    sprintf (line_buf + col_len, "%5d ",
			     shm_appl->as_info[j].as_port);
#endif

#ifdef WIN32
		  if (shm_appl->use_pdh_flag == TRUE)
		    {
		      col_len +=
			sprintf (line_buf + col_len, "%5d ",
				 shm_appl->as_info[j].pdh_workset);
		    }
#else
		  col_len +=
		    sprintf (line_buf + col_len, "%5d ",
			     getsize (shm_appl->as_info[j].pid));
#endif
		  if (shm_appl->as_info[j].uts_status == UTS_STATUS_BUSY)
		    {
		      if (shm_br->br_info[i].appl_server == APPL_SERVER_CAS)
			{
			  if (shm_appl->as_info[j].con_status ==
			      CON_STATUS_OUT_TRAN)
			    col_len +=
			      sprintf (line_buf + col_len, "%-12s",
				       "CLOSE WAIT ");
			  else if (shm_appl->as_info[j].log_msg[0] == '\0')
			    col_len +=
			      sprintf (line_buf + col_len, "%-12s",
				       "CLIENT WAIT ");
			  else
			    col_len +=
			      sprintf (line_buf + col_len, "%-12s",
				       " BUSY  ");
			}
		      else
			col_len +=
			  sprintf (line_buf + col_len, "%-12s", " BUSY  ");
		    }
#ifdef WIN32
		  else if (shm_appl->as_info[j].uts_status ==
			   UTS_STATUS_BUSY_WAIT)
		    col_len +=
		      sprintf (line_buf + col_len, "%-12s", " BUSY  ");
#endif
		  else if (shm_appl->as_info[j].uts_status ==
			   UTS_STATUS_RESTART)
		    col_len +=
		      sprintf (line_buf + col_len, "INITIALIZE APPL_SERVER ");
		  else
		    col_len +=
		      sprintf (line_buf + col_len, "%-12s", " IDLE  ");
#if 0
		  col_len +=
		    sprintf (line_buf + col_len, "%s ",
			     shm_appl->as_info[j].appl_name);
#endif
#if 0
		  col_len +=
		    sprintf (line_buf + col_len, "%d ",
			     shm_appl->as_info[j].port);
#endif

#ifdef GET_PSINFO
		  get_psinfo (shm_appl->as_info[j].pid, &proc_info);
		  col_len +=
		    sprintf (line_buf + col_len, "%5.2f", proc_info.pcpu);
		  time_format (proc_info.cpu_time, time_str);
		  col_len += sprintf (line_buf + col_len, "%7s ", time_str);
#elif WIN32
		  if (shm_appl->use_pdh_flag == TRUE)
		    {
		      float pct_cpu;
		      pct_cpu = shm_appl->as_info[j].pdh_pct_cpu;
		      if (pct_cpu >= 0)
			col_len +=
			  sprintf (line_buf + col_len, "%5.2f ", pct_cpu);
		      else
			col_len +=
			  sprintf (line_buf + col_len, "%5s ", " - ");
		    }
#endif

		  last_access_time = shm_appl->as_info[j].last_access_time;
		  cur_tm = localtime (&last_access_time);
		  cur_tm->tm_year += 1900;

		  col_len +=
		    sprintf (line_buf + col_len,
			     "%02d/%02d/%02d %02d:%02d:%02d ",
			     cur_tm->tm_year, cur_tm->tm_mon + 1,
			     cur_tm->tm_mday, cur_tm->tm_hour, cur_tm->tm_min,
			     cur_tm->tm_sec);
		  if (shm_appl->as_info[j].clt_ip_addr[0] != '\0')
		    {
		      col_len += sprintf (line_buf + col_len, "%s ",
					  shm_appl->as_info[j].clt_ip_addr);
		    }
		  if (shm_appl->as_info[j].clt_appl_name[0] != '\0')
		    {
		      col_len += sprintf (line_buf + col_len, "%s ",
					  shm_appl->as_info[j].clt_appl_name);
		    }
		  if (shm_appl->as_info[j].clt_req_path_info[0] != '\0')
		    {
		      col_len += sprintf (line_buf + col_len, "%s ",
					  shm_appl->as_info[j].
					  clt_req_path_info);
		    }
		  if (shm_appl->as_info[j].session_keep == TRUE)
		    col_len += sprintf (line_buf + col_len, "T ");
#if 0
		  else
		    col_len += sprintf (line_buf + col_len, "F ");
#endif
		  if (shm_appl->as_info[j].uts_status == UTS_STATUS_BUSY)
		    col_len +=
		      sprintf (line_buf + col_len, "%s ",
			       shm_appl->as_info[j].log_msg);
		  if (col_len >= max_col_len)
		    {
		      max_col_len = col_len;
		    }
		  else
		    {
		      sprintf (line_buf + col_len, "%*c",
			       max_col_len - col_len, ' ');
		    }
		  STR_OUT ("%s", line_buf);
		  PRINT_NEWLINE ();
		}
	      PRINT_NEWLINE ();

	      if (display_job_queue == TRUE)
		print_job_queue (job_queue);

	      uw_shm_detach (shm_appl);
	    }
	}
      else
	{			/* service_flag == OFF */
	  STR_OUT ("%s", "OFF");
	  PRINT_NEWLINE ();
	  PRINT_NEWLINE ();
	}
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
  static unsigned long *num_tx_olds = NULL;
  static unsigned long *num_qx_olds = NULL;
  static time_t time_old;
  unsigned long num_tx_cur;
  unsigned long num_qx_cur;
  time_t time_cur;
  int tps;
  int qps;

  buf_len = 0;
  buf_len += sprintf (buf + buf_len, "  %-12s", "NAME");
  buf_len += sprintf (buf + buf_len, "%6s", "PID");
  buf_len += sprintf (buf + buf_len, "%6s", "PORT");
  buf_len += sprintf (buf + buf_len, "%4s", "AS");
  buf_len += sprintf (buf + buf_len, "%4s", "JQ");
#ifdef GET_PSINFO
  buf_len += sprintf (buf + buf_len, "%4s", "THR");
  buf_len += sprintf (buf + buf_len, "%6s", "CPU");
  buf_len += sprintf (buf + buf_len, "%6s", "TIME");
#endif
  buf_len += sprintf (buf + buf_len, "%9s", "REQ");
  buf_len += sprintf (buf + buf_len, "%4s", "TPS");
  buf_len += sprintf (buf + buf_len, "%4s", "QPS");
  buf_len += sprintf (buf + buf_len, "%5s", "AUTO");
  buf_len += sprintf (buf + buf_len, "%5s", "SES");
  buf_len += sprintf (buf + buf_len, "%5s", "SQLL");
  buf_len += sprintf (buf + buf_len, "%5s", "CONN");

  STR_OUT ("%s", buf);
  PRINT_NEWLINE ();
  for (i = strlen (buf); i > 0; i--)
    STR_OUT ("%s", "=");
  PRINT_NEWLINE ();
  if (num_tx_olds == NULL)
    {
      num_tx_olds =
	(unsigned long *) calloc (sizeof (unsigned long), shm_br->num_broker);
      (void) time (&time_old);
    }

  if (num_qx_olds == NULL)
    {
      num_qx_olds =
	(unsigned long *) calloc (sizeof (unsigned long), shm_br->num_broker);
      (void) time (&time_old);
    }

  (void) time (&time_cur);

  for (i = 0; i < shm_br->num_broker; i++)
    {

      if (br_vector[i] == 0)
	continue;

      STR_OUT ("* %-12s", shm_br->br_info[i].name);

      if (shm_br->br_info[i].service_flag == ON)
	{
	  shm_appl = (T_SHM_APPL_SERVER *)
	    uw_shm_open (shm_br->br_info[i].appl_server_shm_id,
			 SHM_APPL_SERVER, SHM_MODE_MONITOR);

	  if (shm_appl == NULL)
	    {
	      STR_OUT ("%s", "shared memory open error");
	      PRINT_NEWLINE ();
	    }
	  else
	    {
	      STR_OUT ("%6d", shm_br->br_info[i].pid);
	      STR_OUT ("%6d", shm_br->br_info[i].port);
	      STR_OUT ("%4d", shm_br->br_info[i].appl_server_num);
	      STR_OUT ("%4d", shm_appl->job_queue[0].id);

#ifdef GET_PSINFO
	      get_psinfo (shm_br->br_info[i].pid, &proc_info);
	      STR_OUT ("%4d", proc_info.num_thr);
	      STR_OUT ("%6.2f", proc_info.pcpu);
	      time_format (proc_info.cpu_time, time_str);
	      STR_OUT ("%6s", time_str);
#endif

	      num_req = 0;
	      for (j = 0; j < shm_br->br_info[i].appl_server_max_num; j++)
		{
		  num_req += shm_appl->as_info[j].num_request;
		}
	      STR_OUT (" %8d", num_req);

	      if (refresh_sec > 0)
		{
		  num_tx_cur = 0;
		  num_qx_cur = 0;
		  for (j = 0; j < shm_br->br_info[i].appl_server_max_num; j++)
		    {
		      num_tx_cur +=
			(unsigned long) shm_appl->as_info[j].
			num_transactions_processed;
		      num_qx_cur +=
			(unsigned long) shm_appl->as_info[j].
			num_query_processed;

		    }
		  tps = (int) ((num_tx_cur - num_tx_olds[i]) /
			       difftime (time_cur, time_old));
		  qps =
		    (int) ((num_qx_cur - num_qx_olds[i]) / difftime (time_cur,
								     time_old));
		  num_tx_olds[i] = num_tx_cur;
		  num_qx_olds[i] = num_qx_cur;
		  STR_OUT (" %3d", tps);
		  STR_OUT (" %3d", qps);
		}
	      else
		{
		  STR_OUT (" %3s", "---");
		  STR_OUT (" %3s", "---");
		}

	      if (shm_br->br_info[i].auto_add_appl_server == ON)
		STR_OUT ("%5s", "ON ");
	      else
		STR_OUT ("%5s", "OFF");

	      if (shm_br->br_info[i].session_flag == ON)
		STR_OUT ("%5d", shm_br->br_info[i].session_timeout);
	      else
		STR_OUT ("%5s", "OFF");

	      if (shm_appl->sql_log_mode & SQL_LOG_MODE_ON)
		{
		  if (shm_appl->sql_log_mode & SQL_LOG_MODE_APPEND)
		    {
		      strcpy (buf, "ON:A");
		    }
		  else
		    {
		      if (shm_appl->sql_log_time < SQL_LOG_TIME_MAX)
			sprintf (buf, "%d", shm_appl->sql_log_time);
		      else
			strcpy (buf, "ON");
		    }
		}
	      else
		strcpy (buf, "OFF");
	      STR_OUT ("%5s", buf);

	      if (shm_appl->keep_connection == KEEP_CON_OFF)
		STR_OUT ("%5s", "OFF");
	      else if (shm_appl->keep_connection == KEEP_CON_ON)
		STR_OUT ("%5s", "ON ");
	      else if (shm_appl->keep_connection == KEEP_CON_AUTO)
		STR_OUT ("%5s", "AUTO");

	      PRINT_NEWLINE ();

	      if (shm_appl->suspend_mode != SUSPEND_NONE)
		{
		  STR_OUT ("%s", "	SUSPENDED");
		  PRINT_NEWLINE ();
		}

	      uw_shm_detach (shm_appl);
	    }
	}
      else
	{			/* service_flag == OFF */
	  STR_OUT ("%s", "OFF");
	  PRINT_NEWLINE ();
	}
    }
  time_old = time_cur;

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
print_header (int check_period, char use_pdh_flag)
{
  char buf[128];
  char line_buf[128];
  int col_len = 0;
  int i;

  col_len += sprintf (buf + col_len, "%2s", "ID ");
  col_len += sprintf (buf + col_len, "%6s", "PID ");
  col_len += sprintf (buf + col_len, "%6s", "C ");
  if (check_period > 0)
    {
      col_len += sprintf (buf + col_len, "%6s", "C/T ");
      col_len += sprintf (buf + col_len, "%6s", "PTIME ");
    }
#ifdef WIN32
  col_len += sprintf (buf + col_len, "%6s", "PORT ");
#endif
#ifdef WIN32
  if (use_pdh_flag == TRUE)
    {
      col_len += sprintf (buf + col_len, "%5s", "PSIZE ");
    }
#else
  col_len += sprintf (buf + col_len, "%5s", "PSIZE ");
#endif
  col_len += sprintf (buf + col_len, "%-12s", "STATUS ");
#if 0
  col_len += sprintf (buf + col_len, "%6s", "PORT ");
#endif
#ifdef GET_PSINFO
  col_len += sprintf (buf + col_len, "%5s", "CPU ");
  col_len += sprintf (buf + col_len, "%8s", "CTIME ");
#elif WIN32
  if (use_pdh_flag == TRUE)
    {
      col_len += sprintf (buf + col_len, "%5s", "CPU ");
    }
#endif
  col_len += sprintf (buf + col_len, "%19s", "LAST ACCESS TIME ");

  for (i = 0; i < col_len; i++)
    line_buf[i] = '-';
  line_buf[i] = 0;

  STR_OUT ("%s", line_buf);
  PRINT_NEWLINE ();
  STR_OUT ("%s", buf);
  PRINT_NEWLINE ();
  STR_OUT ("%s", line_buf);
  PRINT_NEWLINE ();
}

#ifdef WIN32
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
