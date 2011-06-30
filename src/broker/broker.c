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
 * broker.c -
 */

#ident "$Id$"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <fcntl.h>

#if !defined(WINDOWS)
#include <pthread.h>
#include <unistd.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <sys/time.h>
#include <sys/un.h>
#endif

#ifdef BROKER_DEBUG
#include <sys/time.h>
#endif

#include "cas_common.h"
#include "broker_error.h"
#include "broker_env_def.h"
#include "broker_shm.h"
#include "broker_msg.h"
#include "broker_process_size.h"
#include "broker_util.h"
#include "broker_access_list.h"
#include "broker_filename.h"
#include "broker_er_html.h"

#include "cas_intf.h"
#include "broker_send_fd.h"

#if defined(WINDOWS)
#include "broker_wsa_init.h"
#endif


#ifdef WIN_FW
#if !defined(WINDOWS)
#error DEFINE ERROR
#endif
#endif

#ifdef BROKER_RESTART_DEBUG
#define		PS_CHK_PERIOD		30
#else
#define		PS_CHK_PERIOD		600
#endif

#ifdef ASYNC_MODE
#ifdef HPUX10_2
#define		SELECT_MASK		int
#else
#define		SELECT_MASK		fd_set
#endif
#endif

#define		IP_ADDR_STR_LEN		20
#define		BUFFER_SIZE		1024

#define		ENV_BUF_INIT_SIZE	512
#define		ALIGN_ENV_BUF_SIZE(X)	\
	((((X) + ENV_BUF_INIT_SIZE) / ENV_BUF_INIT_SIZE) * ENV_BUF_INIT_SIZE)

#ifdef BROKER_DEBUG
#define		BROKER_LOG(X)			\
		do {				\
		  FILE		*fp;		\
		  fp = fopen("broker.log", "a");	\
		  if (fp) {			\
		    struct timeval tv;		\
		    gettimeofday(&tv, NULL);	\
		    fprintf(fp, "%d %d.%06d %s\n", __LINE__, (int)tv.tv_sec, (int)tv.tv_usec, X);	\
		    fclose(fp);			\
		  }				\
		} while (0)
#define		BROKER_LOG_INT(X)			\
		do {				\
		  FILE	*fp;			\
		  struct timeval tv;		\
		  gettimeofday(&tv, NULL);	\
		  fp = fopen("broker.log", "a");		\
		  if (fp) {			\
		    fprintf(fp, "%d %d.%06d %s=%d\n", __LINE__, (int)tv.tv_sec, (int)tv.tv_usec, #X, X);			\
		    fclose(fp);			\
		  }				\
		} while (0)
#endif

#ifdef SESSION_LOG
#define		SESSION_LOG_WRITE(IP, SID, APPL, INDEX)	\
		do {				\
		  FILE	*fp;			\
		  struct timeval tv;		\
		  char	ip_str[64];		\
		  gettimeofday(&tv, NULL);	\
		  ip2str(IP, ip_str);		\
		  fp = fopen("session.log", "a");	\
		  if (fp) {			\
		    fprintf(fp, "%d %-15s %d %d.%06d %s %d \n", br_index, ip_str, (int) SID, tv.tv_sec, tv.tv_usec, APPL, INDEX); \
		    fclose(fp);			\
		  }				\
		} while (0)
#endif

#ifdef _EDU_
#define		EDU_KEY	"86999522480552846466422480899195252860256028745"
#endif

#define V3_WRITE_HEADER_OK_FILE_SOCK(sock_fd)	\
	do {				\
	  char          buf[V3_RESPONSE_HEADER_SIZE];	\
	  memset(buf, '\0', sizeof(buf));	\
	  sprintf(buf, V3_HEADER_OK);	\
	  write_to_client(sock_fd, buf, sizeof(buf));	\
	} while(0);

#define V3_WRITE_HEADER_ERR_SOCK(sockfd)		\
	do {				\
	  char          buf[V3_RESPONSE_HEADER_SIZE];	\
	  memset(buf, '\0', sizeof(buf));	\
	  sprintf(buf, V3_HEADER_ERR);		\
	  write_to_client(sockfd, buf, sizeof(buf)); \
	} while(0);

#define SET_BROKER_ERR_CODE()			\
	do {					\
	  if (shm_br && br_index >= 0) {	\
	    shm_br->br_info[br_index].err_code = uw_get_error_code();	\
	    shm_br->br_info[br_index].os_err_code = uw_get_os_error_code(); \
	  }					\
	} while (0)

#define SET_BROKER_OK_CODE()				\
	do {						\
	  if (shm_br && br_index >= 0) {		\
	    shm_br->br_info[br_index].err_code = 0;	\
	  }						\
	} while (0)


#define JOB_COUNT_MAX		1000000

#if defined(WINDOWS)
#define F_OK	0
#else
#define SOCKET_TIMEOUT_SEC	2
#endif

typedef struct t_clt_table T_CLT_TABLE;
struct t_clt_table
{
  SOCKET clt_sock_fd;
  char ip_addr[IP_ADDR_STR_LEN];
};

static void cleanup (int signo);
static int init_env (void);

static THREAD_FUNC receiver_thr_f (void *arg);
static THREAD_FUNC dispatch_thr_f (void *arg);

static THREAD_FUNC psize_check_thr_f (void *arg);

static THREAD_FUNC cas_monitor_thr_f (void *arg);
static int read_nbytes_from_client (SOCKET sock_fd, char *buf, int size);

#if defined(WIN_FW)
static THREAD_FUNC service_thr_f (void *arg);
static int process_cas_request (int cas_pid, int as_index, SOCKET clt_sock_fd,
				SOCKET srv_sock_fd);
static int read_from_cas_client (SOCKET sock_fd, char *buf, int size,
				 int as_index, int cas_pid);
#endif

static int write_to_client (SOCKET sock_fd, char *buf, int size);
static int write_to_client_with_timeout (SOCKET sock_fd, char *buf, int size,
					 int timeout_sec);
static int read_from_client (SOCKET sock_fd, char *buf, int size);
static int read_from_client_with_timeout (SOCKET sock_fd, char *buf, int size,
					  int timeout_sec);
static int run_appl_server (int as_index);
static int stop_appl_server (int as_index);
static void restart_appl_server (int as_index);
static SOCKET connect_srv (char *br_name, int as_index);
static int find_idle_cas (void);
static int find_drop_as_index (void);
static int find_add_as_index (void);
static void check_cas_log (char *br_name, int as_index);

static SOCKET sock_fd;
static struct sockaddr_in sock_addr;
static int sock_addr_len;

static T_SHM_BROKER *shm_br = NULL;
static T_SHM_APPL_SERVER *shm_appl;

static int br_index = -1;

#if defined(WIN_FW)
static int num_thr;
#endif

static pthread_mutex_t clt_table_mutex;
static pthread_mutex_t suspend_mutex;
static pthread_mutex_t run_appl_mutex;
static pthread_mutex_t con_status_mutex;
static pthread_mutex_t service_flag_mutex;



static char run_appl_server_flag = 0;

static int process_flag = 1;

static int num_busy_uts = 0;

static int max_open_fd = 128;

#if defined(WIN_FW)
static int last_job_fetch_time;
static time_t last_session_id = 0;
static T_MAX_HEAP_NODE *session_request_q;
#endif

static int hold_job = 0;

#if defined(WINDOWS)
int WINAPI
WinMain (HINSTANCE hInstance,	// handle to current instance
	 HINSTANCE hPrevInstance,	// handle to previous instance
	 LPSTR lpCmdLine,	// pointer to command line
	 int nShowCmd		// show state of window
  )
#else
int
main (int argc, char *argv[])
#endif
{
  char *p;
  int i;
  pthread_t receiver_thread;
  pthread_t dispatch_thread;
  pthread_t psize_check_thread;
#if defined(WIN_FW)
  pthread_t service_thread;
  int *thr_index;
#endif
  int wait_job_cnt;
  int cur_appl_server_num;
  pthread_t cas_monitor_thread;

  signal (SIGTERM, cleanup);
  signal (SIGINT, cleanup);
#if !defined(WINDOWS)
  signal (SIGCHLD, SIG_IGN);
  signal (SIGPIPE, SIG_IGN);
#endif

  pthread_mutex_init (&clt_table_mutex, NULL);
  pthread_mutex_init (&suspend_mutex, NULL);
  pthread_mutex_init (&run_appl_mutex, NULL);
  pthread_mutex_init (&con_status_mutex, NULL);
  pthread_mutex_init (&service_flag_mutex, NULL);

  p = getenv (MASTER_SHM_KEY_ENV_STR);
  if (p == NULL)
    {
      UW_SET_ERROR_CODE (UW_ER_SHM_OPEN, 0);
      goto error1;
    }

  shm_br =
    (T_SHM_BROKER *) uw_shm_open (atoi (p), SHM_BROKER, SHM_MODE_ADMIN);
  if (shm_br == NULL)
    {
      UW_SET_ERROR_CODE (UW_ER_SHM_OPEN, 0);
      goto error1;
    }

  if ((p = getenv (PORT_NUMBER_ENV_STR)) == NULL)
    {
      UW_SET_ERROR_CODE (UW_ER_CANT_CREATE_SOCKET, 0);
      goto error1;
    }

  for (i = 0, br_index = -1; i < shm_br->num_broker; i++)
    {
      if (shm_br->br_info[i].port == atoi (p))
	{
	  br_index = i;
	  break;
	}
    }
  if (br_index == -1)
    {
      UW_SET_ERROR_CODE (UW_ER_CANT_CREATE_SOCKET, 0);
      goto error1;
    }

#if defined(WINDOWS)
  if (wsa_initialize () < 0)
    {
      UW_SET_ERROR_CODE (UW_ER_CANT_CREATE_SOCKET, 0);
      goto error1;
    }
#endif

  if (uw_acl_make (shm_br->br_info[br_index].acl_file) < 0)
    {
      goto error1;
    }

  if ((p = getenv (APPL_SERVER_SHM_KEY_STR)) == NULL)
    {
      UW_SET_ERROR_CODE (UW_ER_SHM_OPEN, 0);
      goto error1;
    }
  shm_appl =
    (T_SHM_APPL_SERVER *) uw_shm_open (atoi (p), SHM_APPL_SERVER,
				       SHM_MODE_ADMIN);
  if (shm_appl == NULL)
    {
      UW_SET_ERROR_CODE (UW_ER_SHM_OPEN, 0);
      goto error1;
    }

  if (init_env () == -1)
    {
      goto error1;
    }

#if defined(WIN_FW)
  num_thr = shm_br->br_info[br_index].appl_server_max_num;

  thr_index = (int *) malloc (sizeof (int) * num_thr);
  if (thr_index == NULL)
    {
      UW_SET_ERROR_CODE (UW_ER_NO_MORE_MEMORY, 0);
      goto error1;
    }

  /* initialize session request queue. queue size is 1 */
  session_request_q =
    (T_MAX_HEAP_NODE *) malloc (sizeof (T_MAX_HEAP_NODE) * num_thr);
  if (session_request_q == NULL)
    {
      UW_SET_ERROR_CODE (UW_ER_NO_MORE_MEMORY, 0);
      goto error1;
    }
  for (i = 0; i < num_thr; i++)
    {
      session_request_q[i].clt_sock_fd = INVALID_SOCKET;
    }
#endif

  set_cubrid_file (FID_SQL_LOG_DIR, shm_appl->log_dir);

  while (shm_br->br_info[br_index].ready_to_service != true)
    {
      SLEEP_MILISEC (0, 200);
    }

  THREAD_BEGIN (receiver_thread, receiver_thr_f, NULL);
  THREAD_BEGIN (dispatch_thread, dispatch_thr_f, NULL);
  THREAD_BEGIN (psize_check_thread, psize_check_thr_f, NULL);

  THREAD_BEGIN (cas_monitor_thread, cas_monitor_thr_f, NULL);

#if defined(WIN_FW)
  for (i = 0; i < num_thr; i++)
    {
      thr_index[i] = i;
      THREAD_BEGIN (service_thread, service_thr_f, thr_index + i);
      shm_appl->as_info[i].last_access_time = time (NULL);
      if (i < shm_br->br_info[br_index].appl_server_min_num)
	{
	  shm_appl->as_info[i].service_flag = SERVICE_ON;
	}
      else
	{
	  shm_appl->as_info[i].service_flag = SERVICE_OFF_ACK;
	}
    }
#endif

  SET_BROKER_OK_CODE ();

  while (process_flag)
    {
      if (shm_appl->suspend_mode != SUSPEND_NONE)
	{
	  if (shm_appl->suspend_mode == SUSPEND_REQ)
	    {
	      pthread_mutex_lock (&suspend_mutex);
	      shm_appl->suspend_mode = SUSPEND;
	      pthread_mutex_unlock (&suspend_mutex);
	    }
	  else if (shm_appl->suspend_mode == SUSPEND_CHANGE_PRIORITY_REQ)
	    {
	      pthread_mutex_lock (&clt_table_mutex);
	      shm_appl->suspend_mode = SUSPEND_CHANGE_PRIORITY;
	    }
	  else if (shm_appl->suspend_mode == SUSPEND_END_CHANGE_PRIORITY)
	    {
	      shm_appl->suspend_mode = SUSPEND;
	      pthread_mutex_unlock (&clt_table_mutex);
	    }
	  SLEEP_MILISEC (1, 0);
	  continue;
	}

      SLEEP_MILISEC (0, 100);

      if (shm_br->br_info[br_index].auto_add_appl_server == OFF)
	{
	  continue;
	}

      cur_appl_server_num = shm_br->br_info[br_index].appl_server_num;

      wait_job_cnt = shm_appl->job_queue[0].id + hold_job;
      wait_job_cnt -= (cur_appl_server_num - num_busy_uts);

#if 0
      if ((wait_job_cnt >= 1) && (new_as_index > 1)
	  && (shm_appl->as_info[new_as_index - 1].service_flag != SERVICE_ON))
	{
	  shm_appl->as_info[new_as_index - 1].service_flag = SERVICE_ON;
	  continue;
	}
#endif

      /* ADD UTS */
      if (cur_appl_server_num < shm_br->br_info[br_index].appl_server_max_num)
	{
	  int add_as_index;

	  add_as_index = find_add_as_index ();
	  if (add_as_index >= 0 && wait_job_cnt >= 1)
	    {
	      int pid;

	      pid = run_appl_server (add_as_index);
	      if (pid > 0)
		{
		  shm_appl->as_info[add_as_index].pid = pid;
		  shm_appl->as_info[add_as_index].session_id = 0;
		  shm_appl->as_info[add_as_index].psize = getsize (pid);
		  shm_appl->as_info[add_as_index].psize_time = time (NULL);
		  shm_appl->as_info[add_as_index].uts_status =
		    UTS_STATUS_IDLE;
		  shm_appl->as_info[add_as_index].service_flag = SERVICE_ON;
		  shm_appl->as_info[add_as_index].reset_flag = FALSE;
		  (shm_br->br_info[br_index].appl_server_num)++;
		  (shm_appl->num_appl_server)++;
		}
	      /*
	         else {
	         (shm_br->br_info[br_index].appl_server_num)--;
	         (shm_appl->num_appl_server)--;
	         }
	       */
	    }
	}			/* end of if - add uts */

      /* DROP UTS */
      if (cur_appl_server_num > shm_br->br_info[br_index].appl_server_min_num
	  && wait_job_cnt <= 0)
	{
	  int drop_as_index = -1;
	  pthread_mutex_lock (&service_flag_mutex);
	  drop_as_index = find_drop_as_index ();
	  if (drop_as_index >= 0)
	    shm_appl->as_info[drop_as_index].service_flag = SERVICE_OFF_ACK;
	  pthread_mutex_unlock (&service_flag_mutex);

	  if (drop_as_index >= 0)
	    {
	      pthread_mutex_lock (&con_status_mutex);
	      CON_STATUS_LOCK (&(shm_appl->as_info[drop_as_index]),
			       CON_STATUS_LOCK_BROKER);
	      if (shm_appl->as_info[drop_as_index].uts_status ==
		  UTS_STATUS_IDLE)
		{
		  /* do nothing */
		}
	      else if (shm_appl->as_info[drop_as_index].cur_keep_con ==
		       KEEP_CON_AUTO
		       && shm_appl->as_info[drop_as_index].uts_status ==
		       UTS_STATUS_BUSY
		       && shm_appl->as_info[drop_as_index].con_status ==
		       CON_STATUS_OUT_TRAN
		       && time (NULL) -
		       shm_appl->as_info[drop_as_index].last_access_time >
		       shm_br->br_info[br_index].time_to_kill)
		{
		  shm_appl->as_info[drop_as_index].con_status =
		    CON_STATUS_CLOSE;
		}
	      else
		{
		  shm_appl->as_info[drop_as_index].service_flag = SERVICE_ON;
		  CON_STATUS_UNLOCK (&(shm_appl->as_info[drop_as_index]),
				     CON_STATUS_LOCK_BROKER);
		  drop_as_index = -1;
		}

	      if (drop_as_index >= 0)
		{
		  CON_STATUS_UNLOCK (&(shm_appl->as_info[drop_as_index]),
				     CON_STATUS_LOCK_BROKER);
		}
	      pthread_mutex_unlock (&con_status_mutex);
	    }

	  if (drop_as_index >= 0)
	    {
	      (shm_br->br_info[br_index].appl_server_num)--;
	      (shm_appl->num_appl_server)--;
	      stop_appl_server (drop_as_index);
	    }
	}			/* end of if (cur_num > min_num) */

    }				/* end of while (process_flag) */

error1:
  SET_BROKER_ERR_CODE ();
  return -1;
}

static void
cleanup (int signo)
{
  signal (signo, SIG_IGN);

  process_flag = 0;
#ifdef SOLARIS
  SLEEP_MILISEC (1, 0);
#endif
  CLOSE_SOCKET (sock_fd);
  exit (0);
}

static const char *cas_client_type_str[] = {
  "UNKNOWN",			/* CAS_CLIENT_NONE */
  "CCI",			/* CAS_CLIENT_CCI */
  "ODBC",			/* CAS_CLIENT_ODBC */
  "JDBC",			/* CAS_CLIENT_JDBC */
  "PHP",			/* CAS_CLIENT_PHP */
  "OLEDB"			/* CAS_CLIENT_OLEDB */
};

static THREAD_FUNC
receiver_thr_f (void *arg)
{
  T_SOCKLEN clt_sock_addr_len;
  struct sockaddr_in clt_sock_addr;
  SOCKET clt_sock_fd;
  int job_queue_size;
  T_MAX_HEAP_NODE *job_queue;
  T_MAX_HEAP_NODE new_job;
  int job_count;
  int read_len;
  int one = 1;
  char cas_req_header[SRV_CON_CLIENT_INFO_SIZE];
  char cas_client_type;
  job_queue_size = shm_appl->job_queue_size;
  job_queue = shm_appl->job_queue;
  job_count = 1;

#if !defined(WINDOWS)
  signal (SIGPIPE, SIG_IGN);
#endif

  while (process_flag)
    {
      clt_sock_addr_len = sizeof (clt_sock_addr);
      clt_sock_fd =
	accept (sock_fd, (struct sockaddr *) &clt_sock_addr,
		&clt_sock_addr_len);
      if (IS_INVALID_SOCKET (clt_sock_fd))
	{
	  continue;
	}

#if !defined(WINDOWS) && defined(ASYNC_MODE)
      if (fcntl (clt_sock_fd, F_SETFL, FNDELAY) < 0)
	{
	  CLOSE_SOCKET (clt_sock_fd);
	  continue;
	}
#endif

      setsockopt (clt_sock_fd, IPPROTO_TCP, TCP_NODELAY, (char *) &one,
		  sizeof (one));

      cas_client_type = CAS_CLIENT_NONE;

      read_len = read_nbytes_from_client (clt_sock_fd,
					  cas_req_header,
					  SRV_CON_CLIENT_INFO_SIZE);
      if (read_len < 0)
	{
	  CLOSE_SOCKET (clt_sock_fd);
	  continue;
	}

      if (strncmp (cas_req_header, "PING", 4) == 0)
	{
	  int ret_code = 0;
	  CAS_SEND_ERROR_CODE (clt_sock_fd, ret_code);
	  CLOSE_SOCKET (clt_sock_fd);
	  continue;
	}

      if (strncmp (cas_req_header, "CANCEL", 6) == 0)
	{
	  int ret_code = 0;
#if !defined(WINDOWS)
	  int pid, i;
#endif

#if !defined(WINDOWS)
	  memcpy ((char *) &pid, cas_req_header + 6, 4);
	  pid = ntohl (pid);
	  ret_code = CAS_ER_QUERY_CANCEL;
	  for (i = 0; i < shm_br->br_info[br_index].appl_server_max_num; i++)
	    {
	      if (shm_appl->as_info[i].service_flag == SERVICE_ON
		  && shm_appl->as_info[i].pid == pid
		  && shm_appl->as_info[i].uts_status == UTS_STATUS_BUSY)
		{
		  ret_code = 0;
		  kill (pid, SIGUSR1);
		  break;
		}
	    }
#endif
	  CAS_SEND_ERROR_CODE (clt_sock_fd, ret_code);
	  CLOSE_SOCKET (clt_sock_fd);
	  continue;
	}

      cas_client_type = cas_req_header[SRV_CON_MSG_IDX_CLIENT_TYPE];
      if (strncmp (cas_req_header, SRV_CON_CLIENT_MAGIC_STR,
		   SRV_CON_CLIENT_MAGIC_LEN) != 0
	  || cas_client_type < CAS_CLIENT_TYPE_MIN
	  || cas_client_type > CAS_CLIENT_TYPE_MAX)
	{
	  CAS_SEND_ERROR_CODE (clt_sock_fd, CAS_ER_COMMUNICATION);
	  CLOSE_SOCKET (clt_sock_fd);
	  continue;
	}

      if (v3_acl != NULL)
	{
	  unsigned char ip_addr[4];

	  memcpy (ip_addr, &(clt_sock_addr.sin_addr), 4);

	  if (uw_acl_check (ip_addr) < 0)
	    {
	      CAS_SEND_ERROR_CODE (clt_sock_fd, CAS_ER_NOT_AUTHORIZED_CLIENT);
	      CLOSE_SOCKET (clt_sock_fd);
	      continue;
	    }
	}

      if (job_queue[0].id == job_queue_size)
	{
	  CAS_SEND_ERROR_CODE (clt_sock_fd, CAS_ER_FREE_SERVER);
	  CLOSE_SOCKET (clt_sock_fd);
	  continue;
	}

      if (max_open_fd < clt_sock_fd)
	{
	  max_open_fd = clt_sock_fd;
	}

      job_count = (job_count >= JOB_COUNT_MAX) ? 1 : job_count + 1;
      new_job.id = job_count;
      new_job.clt_sock_fd = clt_sock_fd;
      new_job.recv_time = time (NULL);
      new_job.priority = 0;
      new_job.script[0] = '\0';
      new_job.clt_major_version = cas_req_header[SRV_CON_MSG_IDX_MAJOR_VER];
      new_job.clt_minor_version = cas_req_header[SRV_CON_MSG_IDX_MINOR_VER];
      new_job.clt_patch_version = cas_req_header[SRV_CON_MSG_IDX_PATCH_VER];
      new_job.cas_client_type = cas_client_type;
      memcpy (new_job.ip_addr, &(clt_sock_addr.sin_addr), 4);
      strcpy (new_job.prg_name, cas_client_type_str[(int) cas_client_type]);

      while (1)
	{
	  pthread_mutex_lock (&clt_table_mutex);
	  if (max_heap_insert (job_queue, job_queue_size, &new_job) < 0)
	    {
	      pthread_mutex_unlock (&clt_table_mutex);
	      SLEEP_MILISEC (0, 100);
	    }
	  else
	    {
	      pthread_mutex_unlock (&clt_table_mutex);
	      break;
	    }
	}
    }

#if defined(WINDOWS)
  return;
#else
  return NULL;
#endif
}


static THREAD_FUNC
dispatch_thr_f (void *arg)
{
  T_MAX_HEAP_NODE *job_queue;
  T_MAX_HEAP_NODE cur_job;
  int as_index, i;
#if !defined(WINDOWS)
  SOCKET srv_sock_fd;
#endif

  job_queue = shm_appl->job_queue;

  while (process_flag)
    {
      for (i = 0; i < shm_br->br_info[br_index].appl_server_max_num; i++)
	{
	  if (shm_appl->as_info[i].service_flag == SERVICE_OFF)
	    {
	      if (shm_appl->as_info[i].uts_status == UTS_STATUS_IDLE)
		shm_appl->as_info[i].service_flag = SERVICE_OFF_ACK;
	    }
	}

      if (shm_appl->suspend_mode != SUSPEND_NONE)
	{
	  SLEEP_MILISEC (1, 0);
	  continue;
	}

      pthread_mutex_lock (&suspend_mutex);
      if (shm_appl->suspend_mode != SUSPEND_NONE)
	{
	  pthread_mutex_unlock (&suspend_mutex);
	  continue;
	}

      pthread_mutex_lock (&clt_table_mutex);
      if (max_heap_delete (job_queue, &cur_job) < 0)
	{
	  pthread_mutex_unlock (&clt_table_mutex);
	  pthread_mutex_unlock (&suspend_mutex);
	  SLEEP_MILISEC (0, 30);
	  continue;
	}
      pthread_mutex_unlock (&clt_table_mutex);

      hold_job = 1;
      max_heap_incr_priority (job_queue);

    retry:
      while (1)
	{
	  pthread_mutex_lock (&service_flag_mutex);
	  as_index = find_idle_cas ();
	  pthread_mutex_unlock (&service_flag_mutex);

	  if (as_index < 0)
	    SLEEP_MILISEC (0, 30);
	  else
	    break;
	}

      hold_job = 0;

#if !defined(WIN_FW)
      shm_appl->as_info[as_index].clt_major_version =
	cur_job.clt_major_version;
      shm_appl->as_info[as_index].clt_minor_version =
	cur_job.clt_minor_version;
      shm_appl->as_info[as_index].clt_patch_version =
	cur_job.clt_patch_version;
      shm_appl->as_info[as_index].cas_client_type = cur_job.cas_client_type;
#if defined(WINDOWS)
      memcpy (shm_appl->as_info[as_index].cas_clt_ip, cur_job.ip_addr, 4);
      shm_appl->as_info[as_index].uts_status = UTS_STATUS_BUSY_WAIT;
      CAS_SEND_ERROR_CODE (cur_job.clt_sock_fd,
			   shm_appl->as_info[as_index].as_port);
      CLOSE_SOCKET (cur_job.clt_sock_fd);
      shm_appl->as_info[as_index].num_request++;
      shm_appl->as_info[as_index].last_access_time = time (NULL);
#else

      srv_sock_fd = connect_srv (shm_br->br_info[br_index].name, as_index);

      if (!IS_INVALID_SOCKET (srv_sock_fd))
	{
	  int ip_addr;
	  int ret_val;
	  int con_status, uts_status;

	  con_status = htonl (shm_appl->as_info[as_index].con_status);

	  ret_val =
	    write_to_client_with_timeout (srv_sock_fd,
					  (char *) &con_status,
					  sizeof (int), SOCKET_TIMEOUT_SEC);
	  if (ret_val != sizeof (int))
	    {
	      CLOSE_SOCKET (srv_sock_fd);
	      goto retry;
	    }

	  ret_val =
	    read_from_client_with_timeout (srv_sock_fd,
					   (char *) &con_status,
					   sizeof (int), SOCKET_TIMEOUT_SEC);
	  if (ret_val != sizeof (int)
	      || ntohl (con_status) != CON_STATUS_IN_TRAN)
	    {
	      CLOSE_SOCKET (srv_sock_fd);
	      goto retry;
	    }

	  memcpy (&ip_addr, cur_job.ip_addr, 4);
	  ret_val = send_fd (srv_sock_fd, cur_job.clt_sock_fd, ip_addr);
	  if (ret_val > 0)
	    {
	      ret_val =
		read_from_client_with_timeout (srv_sock_fd,
					       (char *) &uts_status,
					       sizeof (int),
					       SOCKET_TIMEOUT_SEC);
	    }
	  CLOSE_SOCKET (srv_sock_fd);

	  if (ret_val < 0)
	    {
	      CAS_SEND_ERROR_CODE (cur_job.clt_sock_fd, CAS_ER_FREE_SERVER);
	    }
	  else
	    {
	      shm_appl->as_info[as_index].num_request++;
	    }
	}
      else
	{
	  goto retry;
	}

      CLOSE_SOCKET (cur_job.clt_sock_fd);
#endif /* ifdef !WINDOWS */
#else
      session_request_q[as_index] = cur_job;
#endif

      pthread_mutex_unlock (&suspend_mutex);
    }

#if defined(WINDOWS)
  return;
#else
  return NULL;
#endif
}

#if defined(WIN_FW)
static THREAD_FUNC
service_thr_f (void *arg)
{
  int self_index = *((int *) arg);
  SOCKET clt_sock_fd, srv_sock_fd;
  int ip_addr;
  int cas_pid;
  T_MAX_HEAP_NODE cur_job;

  while (process_flag)
    {
      if (!IS_INVALID_SOCKET (session_request_q[self_index].clt_sock_fd))
	{
	  cur_job = session_request_q[self_index];
	  session_request_q[self_index].clt_sock_fd = INVALID_SOCKET;
	}
      else
	{
	  SLEEP_MILISEC (0, 10);
	  continue;
	}

      clt_sock_fd = cur_job.clt_sock_fd;
      memcpy (&ip_addr, cur_job.ip_addr, 4);

      shm_appl->as_info[self_index].clt_major_version =
	cur_job.clt_major_version;
      shm_appl->as_info[self_index].clt_minor_version =
	cur_job.clt_minor_version;
      shm_appl->as_info[self_index].clt_patch_version =
	cur_job.clt_patch_version;
      shm_appl->as_info[self_index].cas_client_type = cur_job.cas_client_type;
      shm_appl->as_info[self_index].close_flag = 0;
      cas_pid = shm_appl->as_info[self_index].pid;

      srv_sock_fd = connect_srv (shm_br->br_info[br_index].name, self_index);
      if (IS_INVALID_SOCKET (srv_sock_fd))
	{
	  CAS_SEND_ERROR_CODE (clt_sock_fd, CAS_ER_FREE_SERVER);
	  shm_appl->as_info[self_index].uts_status = UTS_STATUS_IDLE;
	  CLOSE_SOCKET (cur_job.clt_sock_fd);
	  continue;
	}
      else
	{
	  CAS_SEND_ERROR_CODE (clt_sock_fd, 0);
	  shm_appl->as_info[self_index].num_request++;
	  shm_appl->as_info[self_index].last_access_time = time (NULL);
	}

      process_cas_request (cas_pid, self_index, clt_sock_fd, srv_sock_fd,
			   cur_job.clt_major_version);

      CLOSE_SOCKET (clt_sock_fd);
      CLOSE_SOCKET (srv_sock_fd);
    }

  return;
}
#endif

static int
init_env (void)
{
  char *port;
  int n;
  int one = 1;

  /* get a Unix stream socket */
  sock_fd = socket (AF_INET, SOCK_STREAM, 0);
  if (IS_INVALID_SOCKET (sock_fd))
    {
      UW_SET_ERROR_CODE (UW_ER_CANT_CREATE_SOCKET, errno);
      return (-1);
    }
  if ((setsockopt (sock_fd, SOL_SOCKET, SO_REUSEADDR, (char *) &one,
		   sizeof (one))) < 0)
    {
      UW_SET_ERROR_CODE (UW_ER_CANT_CREATE_SOCKET, errno);
      return (-1);
    }
  if ((port = getenv (PORT_NUMBER_ENV_STR)) == NULL)
    {
      UW_SET_ERROR_CODE (UW_ER_CANT_CREATE_SOCKET, 0);
      return (-1);
    }
  memset (&sock_addr, 0, sizeof (struct sockaddr_in));
  sock_addr.sin_family = AF_INET;
  sock_addr.sin_port = htons ((unsigned short) (atoi (port)));
  sock_addr_len = sizeof (struct sockaddr_in);

  n = INADDR_ANY;
  memcpy (&sock_addr.sin_addr, &n, sizeof (int));

  if (bind (sock_fd, (struct sockaddr *) &sock_addr, sock_addr_len) < 0)
    {
      UW_SET_ERROR_CODE (UW_ER_CANT_BIND, errno);
      return (-1);
    }

  if (listen (sock_fd, shm_appl->job_queue_size) < 0)
    {
      UW_SET_ERROR_CODE (UW_ER_CANT_BIND, 0);
      return (-1);
    }

  return (0);
}

static int
read_from_client (SOCKET sock_fd, char *buf, int size)
{
  return read_from_client_with_timeout (sock_fd, buf, size, 60);
}

static int
read_from_client_with_timeout (SOCKET sock_fd, char *buf, int size,
			       int timeout_sec)
{
  int read_len;
#ifdef ASYNC_MODE
  SELECT_MASK read_mask;
  int nfound;
  int maxfd;
  struct timeval timeout_val, *timeout_ptr;

  if (timeout_sec < 0)
    {
      timeout_ptr = NULL;
    }
  else
    {
      timeout_val.tv_sec = timeout_sec;
      timeout_val.tv_usec = 0;
      timeout_ptr = &timeout_val;
    }
#endif

#ifdef ASYNC_MODE
  FD_ZERO (&read_mask);
  FD_SET (sock_fd, (fd_set *) & read_mask);
  maxfd = (int) sock_fd + 1;
  nfound =
    select (maxfd, &read_mask, (SELECT_MASK *) 0, (SELECT_MASK *) 0,
	    timeout_ptr);
  if (nfound < 0)
    {
      return -1;
    }
#endif

#ifdef ASYNC_MODE
  if (FD_ISSET (sock_fd, (fd_set *) & read_mask))
    {
#endif
      read_len = READ_FROM_SOCKET (sock_fd, buf, size);
#ifdef ASYNC_MODE
    }
  else
    {
      return -1;
    }
#endif

  return read_len;
}

static int
write_to_client (SOCKET sock_fd, char *buf, int size)
{
  return write_to_client_with_timeout (sock_fd, buf, size, 60);
}

static int
write_to_client_with_timeout (SOCKET sock_fd, char *buf, int size,
			      int timeout_sec)
{
  int write_len;
#ifdef ASYNC_MODE
  SELECT_MASK write_mask;
  int nfound;
  int maxfd;
  struct timeval timeout_val, *timeout_ptr;

  if (timeout_sec < 0)
    {
      timeout_ptr = NULL;
    }
  else
    {
      timeout_val.tv_sec = timeout_sec;
      timeout_val.tv_usec = 0;
      timeout_ptr = &timeout_val;
    }
#endif

  if (IS_INVALID_SOCKET (sock_fd))
    return -1;

#ifdef ASYNC_MODE
  FD_ZERO (&write_mask);
  FD_SET (sock_fd, (fd_set *) & write_mask);
  maxfd = (int) sock_fd + 1;
  nfound =
    select (maxfd, (SELECT_MASK *) 0, &write_mask, (SELECT_MASK *) 0,
	    timeout_ptr);
  if (nfound < 0)
    {
      return -1;
    }
#endif

#ifdef ASYNC_MODE
  if (FD_ISSET (sock_fd, (fd_set *) & write_mask))
    {
#endif
      write_len = WRITE_TO_SOCKET (sock_fd, buf, size);
#ifdef ASYNC_MODE
    }
  else
    {
      return -1;
    }
#endif

  return write_len;
}


static int
run_appl_server (int as_index)
{
  char port_str[AS_PORT_STR_SIZE];
  char appl_name[APPL_SERVER_NAME_MAX_SIZE], appl_name_str[64];
  char access_log_env_str[256], error_log_env_str[256];
  int pid;
  char argv0[128];
  char buf[PATH_MAX];
#if !defined(WINDOWS)
  int i;
#endif

  while (1)
    {
      pthread_mutex_lock (&run_appl_mutex);
      if (run_appl_server_flag)
	{
	  pthread_mutex_unlock (&run_appl_mutex);
	  SLEEP_MILISEC (0, 100);
	  continue;
	}
      else
	{
	  run_appl_server_flag = 1;
	  pthread_mutex_unlock (&run_appl_mutex);
	  break;
	}
    }

  shm_appl->as_info[as_index].service_ready_flag = FALSE;

#if !defined(WINDOWS)
  signal (SIGCHLD, SIG_IGN);
#endif

#if !defined(WINDOWS)
  pid = fork ();
  if (pid == 0)
    {
      signal (SIGCHLD, SIG_DFL);

      for (i = 3; i <= max_open_fd; i++)
	{
	  close (i);
	}
#endif

      sprintf (port_str, "%s=%s%s.%d", PORT_NAME_ENV_STR,
	       get_cubrid_file (FID_SOCK_DIR, buf),
	       shm_br->br_info[br_index].name, as_index + 1);
      putenv (port_str);

      strcpy (appl_name, shm_appl->appl_server_name);
      sprintf (appl_name_str, "%s=%s", APPL_NAME_ENV_STR, appl_name);
      putenv (appl_name_str);

      sprintf (access_log_env_str, "%s=%s", ACCESS_LOG_ENV_STR,
	       shm_br->br_info[br_index].access_log_file);
      putenv (access_log_env_str);

      sprintf (error_log_env_str, "%s=%s", ERROR_LOG_ENV_STR,
	       shm_br->br_info[br_index].error_log_file);
      putenv (error_log_env_str);

      snprintf (argv0, sizeof (argv0) - 1, "%s_%s_%d",
		shm_br->br_info[br_index].name, appl_name, as_index + 1);

#if defined(WINDOWS)
      pid = run_child (appl_name);
#else
      execle (appl_name, argv0, NULL, environ);
#endif

#if !defined(WINDOWS)
      exit (0);
    }
#endif

  SERVICE_READY_WAIT (shm_appl->as_info[as_index].service_ready_flag);
  run_appl_server_flag = 0;

  return pid;
}

static int
stop_appl_server (int as_index)
{
  ut_kill_process (shm_appl->as_info[as_index].pid,
		   shm_br->br_info[br_index].name, as_index);

#if defined(WINDOWS)
  /* [CUBRIDSUS-2068] make the broker sleep for 0.1 sec
     when stopping the cas in order to  prevent communication
     error occurred on windows. */
  SLEEP_MILISEC (0, 100);
#endif

  shm_appl->as_info[as_index].pid = 0;
  shm_appl->as_info[as_index].last_access_time = time (NULL);
  return 0;
}

static void
restart_appl_server (int as_index)
{
  int new_pid;

#if defined(WINDOWS)
  ut_kill_process (shm_appl->as_info[as_index].pid,
		   shm_br->br_info[br_index].name, as_index);

  /* [CUBRIDSUS-2068] make the broker sleep for 0.1 sec
     when stopping the cas in order to  prevent communication
     error occurred on windows. */
  SLEEP_MILISEC (0, 100);

  new_pid = run_appl_server (as_index);
  shm_appl->as_info[as_index].pid = new_pid;
#else

  shm_appl->as_info[as_index].psize =
    getsize (shm_appl->as_info[as_index].pid);
  if (shm_appl->as_info[as_index].psize > 1)
    {
      shm_appl->as_info[as_index].psize_time = time (NULL);
    }
  else
    {
      char pid_file_name[PATH_MAX], dirname[PATH_MAX];
      FILE *fp;
      int old_pid;

      get_cubrid_file (FID_AS_PID_DIR, dirname);
      snprintf (pid_file_name, PATH_MAX - 1, "%s%s_%d.pid", dirname,
		shm_br->br_info[br_index].name, as_index + 1);
      fp = fopen (pid_file_name, "r");
      if (fp)
	{
	  fscanf (fp, "%d", &old_pid);
	  fclose (fp);

	  shm_appl->as_info[as_index].psize = getsize (old_pid);
	  if (shm_appl->as_info[as_index].psize > 1)
	    {
	      shm_appl->as_info[as_index].pid = old_pid;
	      shm_appl->as_info[as_index].psize_time = time (NULL);
	    }
	  else
	    {
	      unlink (pid_file_name);
	    }
	}
    }

  if (shm_appl->as_info[as_index].psize <= 0)
    {
      if (shm_appl->as_info[as_index].pid > 0)
	{
	  ut_kill_process (shm_appl->as_info[as_index].pid,
			   shm_br->br_info[br_index].name, as_index);
	}

      new_pid = run_appl_server (as_index);
      shm_appl->as_info[as_index].pid = new_pid;
    }
#endif
}

static int
read_nbytes_from_client (SOCKET sock_fd, char *buf, int size)
{
  int total_read_size = 0, read_len;

  while (total_read_size < size)
    {
      read_len = read_from_client (sock_fd,
				   buf + total_read_size,
				   size - total_read_size);
      if (read_len <= 0)
	{
	  total_read_size = -1;
	  break;
	}
      total_read_size += read_len;
    }
  return total_read_size;
}

static SOCKET
connect_srv (char *br_name, int as_index)
{
  int sock_addr_len;
#if defined(WINDOWS)
  struct sockaddr_in sock_addr;
#else
  struct sockaddr_un sock_addr;
  struct timeval tv;
#endif
  SOCKET srv_sock_fd;
  int one = 1;
  char retry_count = 0;
#if !defined(WINDOWS)
  char buf[PATH_MAX];
#endif

retry:

#if defined(WINDOWS)
  srv_sock_fd = socket (AF_INET, SOCK_STREAM, 0);
  if (IS_INVALID_SOCKET (srv_sock_fd))
    return INVALID_SOCKET;

  memset (&sock_addr, 0, sizeof (struct sockaddr_in));
  sock_addr.sin_family = AF_INET;
  sock_addr.sin_port =
    htons ((unsigned short) shm_appl->as_info[as_index].as_port);
  memcpy (&sock_addr.sin_addr, shm_br->my_ip_addr, 4);
  sock_addr_len = sizeof (struct sockaddr_in);
#else
  srv_sock_fd = socket (AF_UNIX, SOCK_STREAM, 0);
  if (IS_INVALID_SOCKET (srv_sock_fd))
    return INVALID_SOCKET;

  memset (&sock_addr, 0, sizeof (struct sockaddr_un));
  sock_addr.sun_family = AF_UNIX;
  sprintf (sock_addr.sun_path, "%s/%s.%d",
	   get_cubrid_file (FID_SOCK_DIR, buf), br_name, as_index + 1);
  sock_addr_len =
    strlen (sock_addr.sun_path) + sizeof (sock_addr.sun_family) + 1;
#endif

  if (connect (srv_sock_fd, (struct sockaddr *) &sock_addr, sock_addr_len) <
      0)
    {
      if (retry_count < 1)
	{
	  int new_pid;

	  ut_kill_process (shm_appl->as_info[as_index].pid,
			   shm_br->br_info[br_index].name, as_index);
	  new_pid = run_appl_server (as_index);
	  shm_appl->as_info[as_index].pid = new_pid;
	  retry_count++;
	  CLOSE_SOCKET (srv_sock_fd);
	  goto retry;
	}
      CLOSE_SOCKET (srv_sock_fd);
      return INVALID_SOCKET;
    }

  setsockopt (srv_sock_fd, IPPROTO_TCP, TCP_NODELAY, (char *) &one,
	      sizeof (one));

  return srv_sock_fd;
}

static THREAD_FUNC
cas_monitor_thr_f (void *ar)
{
  int i, new_pid, tmp_num_busy_uts;

  while (process_flag)
    {
      tmp_num_busy_uts = 0;
      for (i = 0; i < shm_br->br_info[br_index].appl_server_max_num; i++)
	{
	  if (shm_appl->as_info[i].service_flag != SERVICE_ON)
	    continue;
	  if (shm_appl->as_info[i].uts_status == UTS_STATUS_BUSY)
	    tmp_num_busy_uts++;
#if defined(WINDOWS)
	  else if (shm_appl->as_info[i].uts_status == UTS_STATUS_BUSY_WAIT)
	    {
	      if (time (NULL) - shm_appl->as_info[i].last_access_time > 10)
		shm_appl->as_info[i].uts_status = UTS_STATUS_IDLE;
	      else
		tmp_num_busy_uts++;
	    }
#endif

/*      if (shm_appl->as_info[i].service_flag != SERVICE_ON)
        continue;  */
	  if (shm_appl->as_info[i].uts_status == UTS_STATUS_BUSY)
	    {
#if defined(WINDOWS)
	      HANDLE phandle;
	      phandle = OpenProcess (SYNCHRONIZE, FALSE,
				     shm_appl->as_info[i].pid);
	      if (phandle == NULL)
		{
		  restart_appl_server (i);
		  shm_appl->as_info[i].uts_status = UTS_STATUS_IDLE;
		}
	      else
		{
		  CloseHandle (phandle);
		}
#else
	      if (kill (shm_appl->as_info[i].pid, 0) < 0)
		{
		  SLEEP_MILISEC (1, 0);
		  if (kill (shm_appl->as_info[i].pid, 0) < 0)
		    {
		      restart_appl_server (i);
		      shm_appl->as_info[i].uts_status = UTS_STATUS_IDLE;
		    }
		}
#endif
	    }
	  if (shm_appl->as_info[i].uts_status == UTS_STATUS_RESTART)
	    {
	      stop_appl_server (i);
	      new_pid = run_appl_server (i);
	      shm_appl->as_info[i].pid = new_pid;
	      shm_appl->as_info[i].uts_status = UTS_STATUS_IDLE;
	    }
	}
      num_busy_uts = tmp_num_busy_uts;
      shm_br->br_info[br_index].num_busy_count = num_busy_uts;
      SLEEP_MILISEC (0, 100);
    }

#if !defined(WINDOWS)
  return NULL;
#endif
}

#if defined(WINDOWS)
static int
get_cputime_sec (int pid)
{
  ULARGE_INTEGER ul;
  HANDLE hProcess;
  FILETIME ctime, etime, systime, usertime;
  int cputime = 0;

  if (pid <= 0)
    return 0;

  hProcess = OpenProcess (PROCESS_QUERY_INFORMATION, FALSE, pid);
  if (hProcess == NULL)
    {
      return -1;
    }

  if (GetProcessTimes (hProcess, &ctime, &etime, &systime, &usertime) != 0)
    {
      ul.HighPart = systime.dwHighDateTime + usertime.dwHighDateTime;
      ul.LowPart = systime.dwLowDateTime + usertime.dwLowDateTime;
      cputime = ((int) (ul.QuadPart / 10000000));
    }
  CloseHandle (hProcess);

  return cputime;
}

static THREAD_FUNC
psize_check_thr_f (void *ar)
{
  int pid;
  int workset_size;
  float pct_cpu;
  int cpu_time;
  int br_num_thr;
  int i;

  if (pdh_init () < 0)
    {
      shm_appl->use_pdh_flag = FALSE;
      return;
    }
  else
    {
      shm_appl->use_pdh_flag = TRUE;
    }

  while (process_flag)
    {
      pdh_collect ();

      if (pdh_get_value (shm_br->br_info[br_index].pid,
			 &workset_size, &pct_cpu, &br_num_thr) < 0)
	{
	  shm_br->br_info[br_index].pdh_pct_cpu = 0;
	}
      else
	{
	  cpu_time = get_cputime_sec (shm_br->br_info[br_index].pid);
	  if (cpu_time >= 0)
	    {
	      shm_br->br_info[br_index].cpu_time = cpu_time;
	    }
	  shm_br->br_info[br_index].pdh_workset = workset_size;
	  shm_br->br_info[br_index].pdh_pct_cpu = pct_cpu;
	  shm_br->br_info[br_index].pdh_num_thr = br_num_thr;
	}

      for (i = 0; i < shm_br->br_info[br_index].appl_server_max_num; i++)
	{
	  if (shm_appl->as_info[i].service_flag != SERVICE_ON)
	    {
	      continue;
	    }

	  check_cas_log (shm_br->br_info[br_index].name, i);

	  pid = shm_appl->as_info[i].pid;

	  cpu_time = get_cputime_sec (pid);
	  if (cpu_time < 0)
	    {
	      shm_appl->as_info[i].cpu_time = 0;
#if 0
	      HANDLE hProcess;
	      hProcess = OpenProcess (PROCESS_QUERY_INFORMATION, FALSE, pid);
	      if (hProcess == NULL)
		{
		  pid = 0;
		  shm_appl->as_info[i].pid = 0;
		  shm_appl->as_info[i].cpu_time = 0;
		}
#endif
	    }
	  else
	    {
	      shm_appl->as_info[i].cpu_time = cpu_time;
	    }

	  if (pdh_get_value (pid, &workset_size, &pct_cpu, NULL) >= 0)
	    {
	      shm_appl->as_info[i].pdh_pid = pid;
	      shm_appl->as_info[i].pdh_workset = workset_size;
	      shm_appl->as_info[i].pdh_pct_cpu = pct_cpu;
	    }
	}
      SLEEP_MILISEC (1, 0);
    }
}

#else /* ifndef WINDOWS */
static THREAD_FUNC
psize_check_thr_f (void *ar)
{
  int i;

  while (process_flag)
    {
      for (i = 0; i < shm_br->br_info[br_index].appl_server_max_num; i++)
	{
	  if (shm_appl->as_info[i].service_flag != SERVICE_ON)
	    continue;

	  shm_appl->as_info[i].psize = getsize (shm_appl->as_info[i].pid);
#if 0
	  if (shm_appl->as_info[i].psize < 0 && shm_appl->as_info[i].pid > 0)
	    {
	      if (kill (shm_appl->as_info[i].pid, 0) < 0 && errno == ESRCH)
		{
		  shm_appl->as_info[i].pid = 0;
		}
	    }
#endif
	  check_cas_log (shm_br->br_info[br_index].name, i);
	}
      SLEEP_MILISEC (1, 0);
    }
  return NULL;
}
#endif

static void
check_cas_log (char *br_name, int as_index)
{
  char log_filename[PATH_MAX], dirname[PATH_MAX];

  if (IS_NOT_APPL_SERVER_TYPE_CAS (shm_br->br_info[br_index].appl_server))
    return;
  if (shm_appl->as_info[as_index].cur_sql_log_mode == SQL_LOG_MODE_NONE)
    return;

  get_cubrid_file (FID_SQL_LOG_DIR, dirname);
  snprintf (log_filename, PATH_MAX, "%s%s_%d.sql.log", dirname,
	    br_name, as_index + 1);

  if (access (log_filename, F_OK) < 0)
    {
      FILE *fp;
      fp = fopen (log_filename, "a");
      if (fp != NULL)
	fclose (fp);
      shm_appl->as_info[as_index].cas_log_reset = CAS_LOG_RESET_REOPEN;
    }
}

#ifdef WIN_FW
static int
process_cas_request (int cas_pid, int as_index, SOCKET clt_sock_fd,
		     SOCKET srv_sock_fd)
{
  char read_buf[1024];
  int msg_size;
  int read_len;
  int tmp_int;
  char *tmp_p;

  msg_size = SRV_CON_DB_INFO_SIZE;
  while (msg_size > 0)
    {
      read_len =
	read_from_cas_client (clt_sock_fd, read_buf, msg_size, as_index,
			      cas_pid);
      if (read_len <= 0)
	{
	  return -1;
	}
      if (send (srv_sock_fd, read_buf, read_len, 0) < read_len)
	return -1;
      msg_size -= read_len;
    }

  if (recv (srv_sock_fd, (char *) &msg_size, 4, 0) < 4)
    return -1;
  if (write_to_client (clt_sock_fd, (char *) &msg_size, 4) < 0)
    return -1;
  msg_size = ntohl (msg_size);
  while (msg_size > 0)
    {
      read_len =
	recv (srv_sock_fd, read_buf,
	      (msg_size >
	       sizeof (read_buf) ? sizeof (read_buf) : msg_size), 0);
      if (read_len <= 0)
	{
	  return -1;
	}
      if (write_to_client (clt_sock_fd, read_buf, read_len) < 0)
	return -1;
      msg_size -= read_len;
    }

  while (1)
    {
      tmp_int = 4;
      tmp_p = (char *) &msg_size;
      while (tmp_int > 0)
	{
	  read_len =
	    read_from_cas_client (clt_sock_fd, tmp_p, tmp_int, as_index,
				  cas_pid);
	  if (read_len <= 0)
	    return -1;
	  tmp_int -= read_len;
	  tmp_p += read_len;
	}
      if (send (srv_sock_fd, (char *) &msg_size, 4, 0) < 0)
	{
	  return -1;
	}

      msg_size = ntohl (msg_size);
      while (msg_size > 0)
	{
	  read_len =
	    read_from_cas_client (clt_sock_fd, read_buf,
				  (msg_size >
				   sizeof (read_buf) ? sizeof (read_buf) :
				   msg_size), as_index, cas_pid);
	  if (read_len <= 0)
	    return -1;
	  if (send (srv_sock_fd, read_buf, read_len, 0) < read_len)
	    return -1;
	  msg_size -= read_len;
	}

      if (recv (srv_sock_fd, (char *) &msg_size, 4, 0) < 4)
	{
	  return -1;
	}
      if (write_to_client (clt_sock_fd, (char *) &msg_size, 4) < 0)
	return -1;

      msg_size = ntohl (msg_size);
      while (msg_size > 0)
	{
	  read_len =
	    recv (srv_sock_fd, read_buf,
		  (msg_size >
		   sizeof (read_buf) ? sizeof (read_buf) : msg_size), 0);
	  if (read_len <= 0)
	    {
	      return -1;
	    }
	  if (write_to_client (clt_sock_fd, read_buf, read_len) < 0)
	    return -1;
	  msg_size -= read_len;
	}

      if (shm_appl->as_info[as_index].close_flag
	  || shm_appl->as_info[as_index].pid != cas_pid)
	{
	  break;
	}
    }

  return 0;
}

static int
read_from_cas_client (SOCKET sock_fd, char *buf, int size, int as_index,
		      int cas_pid)
{
  int read_len;
#ifdef ASYNC_MODE
  SELECT_MASK read_mask;
  int nfound;
  int maxfd;
  struct timeval timeout = { 1, 0 };
#endif

retry:

#ifdef ASYNC_MODE
  FD_ZERO (&read_mask);
  FD_SET (sock_fd, (fd_set *) & read_mask);
  maxfd = sock_fd + 1;
  nfound = select (maxfd, &read_mask, (SELECT_MASK *) 0, (SELECT_MASK *) 0,
		   &timeout);
  if (nfound < 1)
    {
      if (shm_appl->as_info[as_index].close_flag
	  || shm_appl->as_info[as_index].pid != cas_pid)
	{
	  return -1;
	}
      goto retry;
    }
#endif

#ifdef ASYNC_MODE
  if (FD_ISSET (sock_fd, (fd_set *) & read_mask))
    {
#endif
      read_len = READ_FROM_SOCKET (sock_fd, buf, size);
#ifdef ASYNC_MODE
    }
  else
    {
      return -1;
    }
#endif

  return read_len;
}
#endif



static int
find_idle_cas (void)
{
  int i;
  int idle_cas_id = -1;
  time_t max_wait_time;
  int wait_cas_id;
  time_t cur_time = time (NULL);

  wait_cas_id = -1;
  max_wait_time = 0;

  for (i = 0; i < shm_br->br_info[br_index].appl_server_max_num; i++)
    {
      if (shm_appl->as_info[i].service_flag != SERVICE_ON)
	{
	  continue;
	}
      if (shm_appl->as_info[i].uts_status == UTS_STATUS_IDLE
#if !defined (WINDOWS)
	  && kill (shm_appl->as_info[i].pid, 0) == 0
#endif
	)
	{
	  idle_cas_id = i;
	  wait_cas_id = -1;
	  break;
	}
      if (shm_br->br_info[br_index].appl_server_num ==
	  shm_br->br_info[br_index].appl_server_max_num
	  && shm_appl->as_info[i].uts_status == UTS_STATUS_BUSY
	  && shm_appl->as_info[i].cur_keep_con == KEEP_CON_AUTO
	  && shm_appl->as_info[i].con_status == CON_STATUS_OUT_TRAN)
	{
	  time_t wait_time = cur_time - shm_appl->as_info[i].last_access_time;
	  if (wait_time > max_wait_time || wait_cas_id == -1)
	    {
	      max_wait_time = wait_time;
	      wait_cas_id = i;
	    }
	}
    }

  if (wait_cas_id >= 0)
    {
      pthread_mutex_lock (&con_status_mutex);
      CON_STATUS_LOCK (&(shm_appl->as_info[wait_cas_id]),
		       CON_STATUS_LOCK_BROKER);
      if (shm_appl->as_info[wait_cas_id].con_status == CON_STATUS_OUT_TRAN)
	{
	  idle_cas_id = wait_cas_id;
	  shm_appl->as_info[wait_cas_id].con_status =
	    CON_STATUS_CLOSE_AND_CONNECT;
	}
      CON_STATUS_UNLOCK (&(shm_appl->as_info[wait_cas_id]),
			 CON_STATUS_LOCK_BROKER);
      pthread_mutex_unlock (&con_status_mutex);
    }

#if defined(WINDOWS)
  if (idle_cas_id >= 0)
    {
      HANDLE h_proc;
      h_proc =
	OpenProcess (SYNCHRONIZE, FALSE, shm_appl->as_info[idle_cas_id].pid);
      if (h_proc == NULL)
	{
	  shm_appl->as_info[i].uts_status = UTS_STATUS_RESTART;
	  idle_cas_id = -1;
	}
      else
	{
	  CloseHandle (h_proc);
	}
    }
#endif

  if (idle_cas_id < 0)
    {
      return -1;
    }

  shm_appl->as_info[idle_cas_id].uts_status = UTS_STATUS_BUSY;
  return idle_cas_id;
}

static int
find_drop_as_index (void)
{
  int i, drop_as_index, exist_idle_cas;
  time_t max_wait_time, wait_time;

  if (IS_NOT_APPL_SERVER_TYPE_CAS (shm_br->br_info[br_index].appl_server))
    {
      drop_as_index = shm_br->br_info[br_index].appl_server_num - 1;
      wait_time =
	time (NULL) - shm_appl->as_info[drop_as_index].last_access_time;
      if (shm_appl->as_info[drop_as_index].uts_status == UTS_STATUS_IDLE
	  && wait_time > shm_br->br_info[br_index].time_to_kill)
	{
	  return drop_as_index;
	}
      return -1;
    }

  drop_as_index = -1;
  max_wait_time = -1;
  exist_idle_cas = 0;

  for (i = shm_br->br_info[br_index].appl_server_max_num - 1; i >= 0; i--)
    {
      if (shm_appl->as_info[i].service_flag != SERVICE_ON)
	continue;

      wait_time = time (NULL) - shm_appl->as_info[i].last_access_time;

      if (shm_appl->as_info[i].uts_status == UTS_STATUS_IDLE)
	{
	  if (wait_time > shm_br->br_info[br_index].time_to_kill)
	    {
	      drop_as_index = i;
	      break;
	    }
	  else
	    {
	      exist_idle_cas = 1;
	      drop_as_index = -1;
	    }
	}

      if (shm_appl->as_info[i].uts_status == UTS_STATUS_BUSY
	  && shm_appl->as_info[i].con_status == CON_STATUS_OUT_TRAN
	  && wait_time > max_wait_time
	  && wait_time > shm_br->br_info[br_index].time_to_kill
	  && exist_idle_cas == 0)
	{
	  max_wait_time = wait_time;
	  drop_as_index = i;
	}
    }

  return drop_as_index;
}

static int
find_add_as_index ()
{
  int i;

  for (i = 0; i < shm_br->br_info[br_index].appl_server_max_num; i++)
    {
      if (shm_appl->as_info[i].service_flag == SERVICE_OFF_ACK)
	return i;
    }
  return -1;
}
