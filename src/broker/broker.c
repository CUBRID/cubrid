/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * broker.c -
 */

#ident "$Id$"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <fcntl.h>

#ifdef WIN32
#include <winsock2.h>
#include <windows.h>
#include <process.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <io.h>
#else
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
#include "error.h"
#include "env_str_def.h"
#include "shm.h"
#include "disp_intf.h"
#include "getsize.h"
#include "util.h"
#include "uw_acl.h"
#include "file_name.h"
#include "er_html.h"

#ifdef CAS_BROKER
#include "cas_intf.h"
#include "send_fd.h"
#endif

#ifdef WIN32
#include "wsa_init.h"
#endif


#ifdef WIN_FW
#if !defined(CAS_BROKER) || !defined(WIN32)
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

#ifdef WIN32
#define ALLOC_COUNTER_VALUE()						\
  	do {								\
	    int _mem_size = sizeof(PDH_FMT_COUNTERVALUE_ITEM) *		\
	      			   num_counter_value;			\
	    cntvalue_pid = (PDH_FMT_COUNTERVALUE_ITEM*) realloc(cntvalue_pid, _mem_size);		\
	    cntvalue_workset = (PDH_FMT_COUNTERVALUE_ITEM*) realloc(cntvalue_workset, _mem_size);	\
	    cntvalue_pct_cpu = (PDH_FMT_COUNTERVALUE_ITEM*) realloc(cntvalue_pct_cpu, _mem_size);	\
	    cntvalue_num_thr = (PDH_FMT_COUNTERVALUE_ITEM*) realloc(cntvalue_num_thr, _mem_size);	\
	} while (0)

#define IS_COUNTER_VALUE_PTR_NULL()					\
  	(cntvalue_pid == NULL || cntvalue_workset == NULL ||		\
	 cntvalue_pct_cpu == NULL || cntvalue_num_thr == NULL)

#endif

#ifdef WIN32
#define F_OK	0
#endif

typedef struct t_clt_table T_CLT_TABLE;
struct t_clt_table
{
  int clt_sock_fd;
  char ip_addr[IP_ADDR_STR_LEN];
};

typedef struct t_client_info T_CLIENT_INFO;
struct t_client_info
{
  int as_index;
  int content_length;
  int session_id;
  int env_buf_size;
  int env_buf_alloc_size;
  char clt_ip_addr[IP_ADDR_STR_LEN];
  char *path_info;
  char *clt_appl_name;
  char *delimiter_str;
  char *out_file_name;
  char *env_buf;
};


static void cleanup (int signo);
static int init_env (void);

static THREAD_FUNC receiver_thr_f (void *arg);
static THREAD_FUNC dispatch_thr_f (void *arg);

static THREAD_FUNC psize_check_thr_f (void *arg);

#ifdef CAS_BROKER
static THREAD_FUNC cas_monitor_thr_f (void *arg);
static int read_nbytes_from_client (int sock_fd, char *buf, int size);
#else
static THREAD_FUNC service_thr_f (void *arg);
static int read_client_data (int clt_sock_fd, T_CLIENT_INFO * clt_info);
static int process_request (int clt_sock_fd, T_CLIENT_INFO * clt_info);
static void ip2str (unsigned char *ip, char *ip_str);
static int str2ip (char *ip_str, unsigned char *ip);
static void ht_error_message (int clt_sock_fd, int cur_error_code,
			      int cur_os_errno);
static void set_close_job_info (T_CLIENT_INFO * info);
static void set_close_job (T_MAX_HEAP_NODE * job);
#endif

#if defined(WIN_FW)
static THREAD_FUNC service_thr_f (void *arg);
static int process_cas_request (int cas_pid, int as_index, int clt_sock_fd,
				int srv_sock_fd);
static int read_from_cas_client (int sock_fd, char *buf, int size,
				 int as_index, int cas_pid);
#endif

static int write_to_client (int sock_fd, char *buf, int size);
static int read_from_client (int sock_fd, char *buf, int size);
static int run_appl_server (int as_index);
static int stop_appl_server (int as_index);
static void restart_appl_server (int as_index);
static int connect_srv (char *br_name, int as_index);
static int find_idle_cas (void);
static int find_drop_as_index (void);
static int find_add_as_index (void);
static void check_cas_log (char *br_name, int as_index);

#ifdef WIN32
static int pdh_init ();
static int pdh_collect ();
static int pdh_get_value (int pid, int *workset, float *pct_cpu,
			  int *br_num_thr);
#endif


#ifndef WIN32
extern char **environ;
#endif

static int sock_fd;
static struct sockaddr_in sock_addr;
static int sock_addr_len;

static T_SHM_BROKER *shm_br = NULL;
static T_SHM_APPL_SERVER *shm_appl;

static int br_index = -1;

#if !defined(CAS_BROKER) || defined(WIN_FW)
static int num_thr;
#endif

#ifdef WIN32
static HANDLE clt_table_mutex = NULL;
static HANDLE num_busy_uts_mutex = NULL;
static HANDLE session_mutex = NULL;
static HANDLE suspend_mutex = NULL;
static HANDLE run_appl_mutex = NULL;
static HANDLE con_status_mutex = NULL;
static HANDLE service_flag_mutex = NULL;
#else
static pthread_mutex_t clt_table_mutex;
static pthread_mutex_t suspend_mutex;
static pthread_mutex_t run_appl_mutex;
static pthread_mutex_t con_status_mutex;
static pthread_mutex_t service_flag_mutex;
#ifndef CAS_BROKER
static pthread_mutex_t num_busy_uts_mutex;
static pthread_mutex_t session_mutex;
#endif
#endif


static char run_appl_server_flag = 0;

static int process_flag = 1;

static int num_busy_uts = 0;

#if !defined(CAS_BROKER) || defined(WIN_FW)
static int last_job_fetch_time;
static time_t last_session_id = 0;
static T_MAX_HEAP_NODE *session_request_q;
#endif

static int hold_job = 0;

#ifdef WIN32
typedef PDH_STATUS (__stdcall * PDHOpenQuery) (LPCSTR, DWORD_PTR,
					       PDH_HQUERY *);
typedef PDH_STATUS (__stdcall * PDHCloseQuery) (PDH_HQUERY);
typedef PDH_STATUS (__stdcall * PDHAddCounter) (PDH_HQUERY, LPCSTR, DWORD_PTR,
						PDH_HCOUNTER *);
typedef PDH_STATUS (__stdcall * PDHCollectQueryData) (PDH_HQUERY);
typedef PDH_STATUS (__stdcall * PDHGetFormattedCounterArray) (PDH_HCOUNTER,
							      DWORD, LPDWORD,
							      LPDWORD,
							      PPDH_FMT_COUNTERVALUE_ITEM_A);
PDHOpenQuery fp_PdhOpenQuery;
PDHCloseQuery fp_PdhCloseQuery;
PDHAddCounter fp_PdhAddCounter;
PDHCollectQueryData fp_PdhCollectQueryData;
PDHGetFormattedCounterArray fp_PdhGetFormattedCounterArray;

HCOUNTER counter_pid;
HCOUNTER counter_pct_cpu;
HCOUNTER counter_workset;
HCOUNTER counter_num_thr;
PDH_FMT_COUNTERVALUE_ITEM *cntvalue_pid = NULL;
PDH_FMT_COUNTERVALUE_ITEM *cntvalue_pct_cpu = NULL;
PDH_FMT_COUNTERVALUE_ITEM *cntvalue_workset = NULL;
PDH_FMT_COUNTERVALUE_ITEM *cntvalue_num_thr = NULL;
HQUERY pdh_h_query;
int num_counter_value;
unsigned long pdh_num_proc;
#endif


#ifdef WIN32
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
  char buf[PATH_MAX];
  char *p;
  int i;
  T_THREAD receiver_thread;
  T_THREAD dispatch_thread;
  T_THREAD psize_check_thread;
#if !defined(CAS_BROKER) || defined(WIN_FW)
  T_THREAD service_thread;
  int *thr_index;
#endif
  int wait_job_cnt;
  int cur_appl_server_num;
#ifdef CAS_BROKER
  T_THREAD cas_monitor_thread;
#endif

  signal (SIGTERM, cleanup);
  signal (SIGINT, cleanup);
#ifndef WIN32
  signal (SIGCHLD, SIG_IGN);
  signal (SIGPIPE, SIG_IGN);
#endif

  MUTEX_INIT (clt_table_mutex);
  MUTEX_INIT (suspend_mutex);
  MUTEX_INIT (run_appl_mutex);
  MUTEX_INIT (con_status_mutex);
  MUTEX_INIT (service_flag_mutex);
#ifndef CAS_BROKER
  MUTEX_INIT (num_busy_uts_mutex);
  MUTEX_INIT (session_mutex);
#endif


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

#ifdef WIN32
  if (wsa_initialize () < 0)
    {
      UW_SET_ERROR_CODE (UW_ER_CANT_CREATE_SOCKET, 0);
      goto error1;
    }
#endif

  if (init_env () == -1)
    {
      goto error1;
    }

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

#if !defined(CAS_BROKER) || defined(WIN_FW)
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
      session_request_q[i].clt_sock_fd = -1;
    }
#endif /* ifndef CAS_BROKER */

  sprintf (buf, "%s/%s", shm_appl->log_dir, CUBRID_SQL_LOG_DIR);
  set_cubrid_file (FID_SQL_LOG_DIR, buf);

  THREAD_BEGIN (receiver_thread, receiver_thr_f, NULL);
  THREAD_BEGIN (dispatch_thread, dispatch_thr_f, NULL);
  THREAD_BEGIN (psize_check_thread, psize_check_thr_f, NULL);

#ifdef CAS_BROKER
  THREAD_BEGIN (cas_monitor_thread, cas_monitor_thr_f, NULL);
#endif

#if !defined(CAS_BROKER) || defined(WIN_FW)
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
#ifdef UNIXWARE711
  for (i = 0; i < shm_br->br_info[br_index].appl_server_max_num; i++)
    {
      shm_appl->as_info[i].clt_sock_fd = 0;
    }
#endif
#endif

  SET_BROKER_OK_CODE ();

  while (process_flag)
    {
      if (shm_appl->suspend_mode != SUSPEND_NONE)
	{
	  if (shm_appl->suspend_mode == SUSPEND_REQ)
	    {
	      MUTEX_LOCK (suspend_mutex);
	      shm_appl->suspend_mode = SUSPEND;
	      MUTEX_UNLOCK (suspend_mutex);
	    }
	  else if (shm_appl->suspend_mode == SUSPEND_CHANGE_PRIORITY_REQ)
	    {
	      MUTEX_LOCK (clt_table_mutex);
	      shm_appl->suspend_mode = SUSPEND_CHANGE_PRIORITY;
	    }
	  else if (shm_appl->suspend_mode == SUSPEND_END_CHANGE_PRIORITY)
	    {
	      shm_appl->suspend_mode = SUSPEND;
	      MUTEX_UNLOCK (clt_table_mutex);
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
      if ((wait_job_cnt >= 1) && (new_as_index > 1) &&
	  (shm_appl->as_info[new_as_index - 1].service_flag != SERVICE_ON))
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
		  shm_appl->as_info[add_as_index].session_keep = FALSE;
		  shm_appl->as_info[add_as_index].psize = getsize (pid);
		  shm_appl->as_info[add_as_index].psize_time = time (NULL);
		  shm_appl->as_info[add_as_index].uts_status =
		    UTS_STATUS_IDLE;
		  shm_appl->as_info[add_as_index].service_flag = SERVICE_ON;
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
	  MUTEX_LOCK (service_flag_mutex);
	  drop_as_index = find_drop_as_index ();
	  if (drop_as_index >= 0)
	    shm_appl->as_info[drop_as_index].service_flag = SERVICE_OFF_ACK;
	  MUTEX_UNLOCK (service_flag_mutex);

	  if (drop_as_index >= 0)
	    {
	      MUTEX_LOCK (con_status_mutex);
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
		  drop_as_index = -1;
		}
	      CON_STATUS_UNLOCK (&(shm_appl->as_info[drop_as_index]),
				 CON_STATUS_LOCK_BROKER);
	      MUTEX_UNLOCK (con_status_mutex);
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

static char *cas_client_type_str[] = {
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
  int clt_sock_fd;
  int job_queue_size;
  T_MAX_HEAP_NODE *job_queue;
  T_MAX_HEAP_NODE new_job;
  int job_count;
  int read_len;
  int one = 1;
#ifdef CAS_BROKER
  char cas_req_header[SRV_CON_CLIENT_INFO_SIZE];
  T_BROKER_VERSION clt_ver;
  char cas_client_type;
#else
  char magic[sizeof (UW_SOCKET_MAGIC)];
  char priority;
  char pre_send_data[PRE_SEND_DATA_SIZE];
  int total_read_size;
  int session_id;
#endif
  job_queue_size = shm_appl->job_queue_size;
  job_queue = shm_appl->job_queue;
  job_count = 1;

#ifndef WIN32
  signal (SIGPIPE, SIG_IGN);
#endif

  while (process_flag)
    {
      clt_sock_addr_len = sizeof (clt_sock_addr);
      clt_sock_fd =
	accept (sock_fd, (struct sockaddr *) &clt_sock_addr,
		&clt_sock_addr_len);
      if (clt_sock_fd < 0)
	continue;

#if !defined(WIN32) && defined(ASYNC_MODE)
      if (fcntl (clt_sock_fd, F_SETFL, FNDELAY) < 0)
	{
	  CLOSE_SOCKET (clt_sock_fd);
	  continue;
	}
#endif

      setsockopt (clt_sock_fd, IPPROTO_TCP, TCP_NODELAY, (char *) &one,
		  sizeof (one));

#ifdef CAS_BROKER
      cas_client_type = CAS_CLIENT_NONE;

      read_len = read_nbytes_from_client (clt_sock_fd,
					  cas_req_header,
					  SRV_CON_CLIENT_INFO_SIZE);
      if (read_len < 0)
	{
	  CLOSE_SOCKET (clt_sock_fd);
	  continue;
	}
      if (strncmp (cas_req_header, "CANCEL", 6) == 0)
	{
	  int ret_code = 0;
#ifndef WIN32
	  int pid, i;
#endif

#ifndef WIN32
	  memcpy ((char *) &pid, cas_req_header + 6, 4);
	  pid = ntohl (pid);
	  ret_code = CAS_ER_QUERY_CANCEL;
	  for (i = 0; i < shm_br->br_info[br_index].appl_server_max_num; i++)
	    {
	      if (shm_appl->as_info[i].service_flag == SERVICE_ON &&
		  shm_appl->as_info[i].pid == pid &&
		  shm_appl->as_info[i].uts_status == UTS_STATUS_BUSY)
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

      clt_ver = CAS_MAKE_VER (cas_req_header[SRV_CON_MSG_IDX_MAJOR_VER],
			      cas_req_header[SRV_CON_MSG_IDX_MINOR_VER],
			      cas_req_header[SRV_CON_MSG_IDX_PATCH_VER]);
      if (clt_ver > CAS_CUR_VERSION)
	{
	  CAS_SEND_ERROR_CODE (clt_sock_fd, CAS_ER_VERSION);
	  CLOSE_SOCKET (clt_sock_fd);
	  continue;
	}
#else
      read_len =
	read_from_client (clt_sock_fd, magic, sizeof (UW_SOCKET_MAGIC));
      if ((read_len <= 0) || strcmp (magic, UW_SOCKET_MAGIC) != 0)
	{
	  CLOSE_SOCKET (clt_sock_fd);
	  continue;
	}
      read_len = read_from_client (clt_sock_fd, &priority, 1);
      if (read_len <= 0)
	{
	  CLOSE_SOCKET (clt_sock_fd);
	  continue;
	}
      if (priority < 0)
	priority = 0;
      else if (priority > 2)
	priority = 2;

      total_read_size = 0;
      while (total_read_size < PRE_SEND_DATA_SIZE)
	{
	  read_len =
	    read_from_client (clt_sock_fd, pre_send_data + total_read_size,
			      PRE_SEND_DATA_SIZE - total_read_size);
	  if (read_len <= 0)
	    {
	      total_read_size = -1;
	      break;
	    }
	  total_read_size += read_len;
	}
      if (total_read_size < 0)
	{
	  CLOSE_SOCKET (clt_sock_fd);
	  continue;
	}
#endif

#ifndef CAS_BROKER
#ifdef _EDU_
      if (shm_br->br_info[br_index].appl_server != APPL_SERVER_UTS_W)
	{
	  if (strncmp
	      (pre_send_data + PRE_SEND_KEY_OFFSET, EDU_KEY,
	       sizeof (EDU_KEY)) != 0)
	    {
	      ht_error_message (clt_sock_fd, UW_ER_INVALID_CLIENT, 0);
	      CLOSE_SOCKET (clt_sock_fd);
	      continue;
	    }
	}
#endif
#endif

      if (v3_acl != NULL)
	{
	  unsigned char ip_addr[4];

#ifdef CAS_BROKER
	  memcpy (ip_addr, &(clt_sock_addr.sin_addr), 4);
#else
	  if (shm_br->br_info[br_index].appl_server == APPL_SERVER_UTS_W)
	    str2ip (pre_send_data + PRE_SEND_SCRIPT_SIZE, ip_addr);
	  else
	    memcpy (ip_addr, &(clt_sock_addr.sin_addr), 4);
#endif

	  if (uw_acl_check (ip_addr) < 0)
	    {
#ifdef CAS_BROKER
	      CAS_SEND_ERROR_CODE (clt_sock_fd, CAS_ER_NOT_AUTHORIZED_CLIENT);
#else
	      ht_error_message (clt_sock_fd, UW_ER_INVALID_CLIENT, 0);
#endif
	      CLOSE_SOCKET (clt_sock_fd);
	      continue;
	    }
	}

#ifndef CAS_BROKER
      session_id =
	atoi (pre_send_data + PRE_SEND_SCRIPT_SIZE + PRE_SEND_PRG_NAME_SIZE);

      if (session_id > 0)
	{			/* session request */
	  int session_index, i;

	  MUTEX_LOCK (session_mutex);
	  for (session_index = -1, i = 0; i < shm_appl->num_appl_server; i++)
	    {
	      if ((shm_appl->as_info[i].session_keep == TRUE) &&
		  (shm_appl->as_info[i].session_id == session_id))
		{
		  session_index = i;
		}
	    }

	  if (session_index == -1)
	    {
	      SLEEP_MILISEC (0, 100);
	      ht_error_message (clt_sock_fd, UW_ER_SESSION_NOT_FOUND, 0);
	      CLOSE_SOCKET (clt_sock_fd);
#ifdef SESSION_LOG
	      SESSION_LOG_WRITE ((unsigned char
				  *) (&(clt_sock_addr.sin_addr)), session_id,
				 "SESSION NOT FOUND", -1);
#endif
	    }
	  else
	    {
	      if (session_request_q[session_index].clt_sock_fd > 0)
		{
		  CLOSE_SOCKET (clt_sock_fd);
		}
	      else
		{
		  job_count =
		    (job_count >= JOB_COUNT_MAX) ? 1 : job_count + 1;
		  session_request_q[session_index].id = job_count;
		  session_request_q[session_index].priority = 0;
		  session_request_q[session_index].clt_sock_fd = clt_sock_fd;
		  session_request_q[session_index].recv_time = time (NULL);
		  if (shm_br->br_info[br_index].appl_server ==
		      APPL_SERVER_UTS_W)
		    {
		      str2ip (pre_send_data + PRE_SEND_SCRIPT_SIZE,
			      session_request_q[session_index].ip_addr);
		    }
		  else
		    {
		      memcpy (session_request_q[session_index].ip_addr,
			      &(clt_sock_addr.sin_addr), 4);
		    }
		  strcpy (session_request_q[session_index].script,
			  pre_send_data);
		  if (shm_br->br_info[br_index].appl_server ==
		      APPL_SERVER_UTS_W)
		    {
		      strcpy (session_request_q[session_index].prg_name, "");
		    }
		  else
		    {
		      strcpy (session_request_q[session_index].prg_name,
			      pre_send_data + PRE_SEND_SCRIPT_SIZE);
		    }
		}
	    }

	  MUTEX_UNLOCK (session_mutex);
	  continue;
	}
#endif /* ifndef CAS_BROKER */


      if (job_queue[0].id == job_queue_size)
	{
	  SLEEP_SEC (1);
	  if (job_queue[0].id == job_queue_size)
	    {
#ifdef CAS_BROKER
	      CAS_SEND_ERROR_CODE (clt_sock_fd, CAS_ER_FREE_SERVER);
#else
	      ht_error_message (clt_sock_fd, UW_ER_NO_FREE_UTS, 0);
#endif
	      CLOSE_SOCKET (clt_sock_fd);
	      continue;
	    }
	}

      job_count = (job_count >= JOB_COUNT_MAX) ? 1 : job_count + 1;
      new_job.id = job_count;
      new_job.clt_sock_fd = clt_sock_fd;
      new_job.recv_time = time (NULL);
#ifdef CAS_BROKER
      new_job.priority = 0;
      new_job.script[0] = '\0';
      new_job.clt_major_version = cas_req_header[SRV_CON_MSG_IDX_MAJOR_VER];
      new_job.clt_minor_version = cas_req_header[SRV_CON_MSG_IDX_MINOR_VER];
      new_job.clt_patch_version = cas_req_header[SRV_CON_MSG_IDX_PATCH_VER];
      new_job.cas_client_type = cas_client_type;
      memcpy (new_job.ip_addr, &(clt_sock_addr.sin_addr), 4);
      strcpy (new_job.prg_name, cas_client_type_str[cas_client_type]);
#else
      new_job.priority = priority * shm_br->br_info[br_index].priority_gap;
      strcpy (new_job.script, pre_send_data);
      if (shm_br->br_info[br_index].appl_server == APPL_SERVER_UTS_W)
	{
	  str2ip (pre_send_data + PRE_SEND_SCRIPT_SIZE, new_job.ip_addr);
	  strcpy (new_job.prg_name, "");
	}
      else
	{
	  memcpy (new_job.ip_addr, &(clt_sock_addr.sin_addr), 4);
	  strcpy (new_job.prg_name, pre_send_data + PRE_SEND_SCRIPT_SIZE);
	}
#endif

      while (1)
	{
	  MUTEX_LOCK (clt_table_mutex);
	  if (max_heap_insert (job_queue, job_queue_size, &new_job) < 0)
	    {
	      MUTEX_UNLOCK (clt_table_mutex);
	      SLEEP_MILISEC (0, 100);
	    }
	  else
	    {
	      MUTEX_UNLOCK (clt_table_mutex);
	      break;
	    }
	}
    }

#ifdef WIN32
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
#ifdef CAS_BROKER
#ifndef WIN32
  int srv_sock_fd;
#endif
#endif

  job_queue = shm_appl->job_queue;

  while (process_flag)
    {
      for (i = 0; i < shm_br->br_info[br_index].appl_server_max_num; i++)
	{
	  if (shm_appl->as_info[i].session_keep == TRUE)
	    continue;
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

      MUTEX_LOCK (suspend_mutex);
      if (shm_appl->suspend_mode != SUSPEND_NONE)
	{
	  MUTEX_UNLOCK (suspend_mutex);
	  continue;
	}

      MUTEX_LOCK (clt_table_mutex);
      if (max_heap_delete (job_queue, &cur_job) < 0)
	{
	  MUTEX_UNLOCK (clt_table_mutex);
	  MUTEX_UNLOCK (suspend_mutex);
	  SLEEP_MILISEC (0, 10);
	  continue;
	}
      MUTEX_UNLOCK (clt_table_mutex);

      hold_job = 1;
      max_heap_incr_priority (job_queue);

      while (1)
	{
	  MUTEX_LOCK (service_flag_mutex);
	  as_index = find_idle_cas ();
	  MUTEX_UNLOCK (service_flag_mutex);

	  if (as_index < 0)
	    SLEEP_MILISEC (0, 10);
	  else
	    break;
	}

      hold_job = 0;

#if defined(CAS_BROKER) && !defined(WIN_FW)
      shm_appl->as_info[as_index].clt_major_version =
	cur_job.clt_major_version;
      shm_appl->as_info[as_index].clt_minor_version =
	cur_job.clt_minor_version;
      shm_appl->as_info[as_index].clt_patch_version =
	cur_job.clt_patch_version;
      shm_appl->as_info[as_index].cas_client_type = cur_job.cas_client_type;
#ifdef WIN32
      memcpy (shm_appl->as_info[as_index].cas_clt_ip, cur_job.ip_addr, 4);
      shm_appl->as_info[as_index].uts_status = UTS_STATUS_BUSY_WAIT;
      CAS_SEND_ERROR_CODE (cur_job.clt_sock_fd,
			   shm_appl->as_info[as_index].as_port);
      CLOSE_SOCKET (cur_job.clt_sock_fd);
      shm_appl->as_info[as_index].num_request++;
      shm_appl->as_info[as_index].last_access_time = time (NULL);
#else
#ifdef UNIXWARE711
      if (shm_appl->as_info[as_index].clt_sock_fd > 0)
	{
	  CLOSE_SOCKET (shm_appl->as_info[as_index].clt_sock_fd);
	  shm_appl->as_info[as_index].clt_sock_fd = 0;
	}
#endif
      srv_sock_fd = connect_srv (shm_br->br_info[br_index].name, as_index);
      if (srv_sock_fd >= 0)
	{
	  int ip_addr;

	  CAS_SEND_ERROR_CODE (cur_job.clt_sock_fd, 0);
	  memcpy (&ip_addr, cur_job.ip_addr, 4);
	  if (send_fd (srv_sock_fd, cur_job.clt_sock_fd, ip_addr) == 0)
	    {			/* fail */
	      CAS_SEND_ERROR_CODE (cur_job.clt_sock_fd, CAS_ER_FREE_SERVER);
	      shm_appl->as_info[as_index].uts_status = UTS_STATUS_IDLE;
	    }
	  else
	    {
	      char read_buf[64];
	      shm_appl->as_info[as_index].num_request++;
	      read (srv_sock_fd, read_buf, sizeof (read_buf));
	    }
	  CLOSE_SOCKET (srv_sock_fd);
	}
      else
	{
	  CAS_SEND_ERROR_CODE (cur_job.clt_sock_fd, CAS_ER_FREE_SERVER);
	  shm_appl->as_info[as_index].uts_status = UTS_STATUS_IDLE;
	}
#ifdef UNIXWARE711
      shm_appl->as_info[as_index].clt_sock_fd = cur_job.clt_sock_fd;
#else
      CLOSE_SOCKET (cur_job.clt_sock_fd);
#endif
#endif /* ifdef !WIN32 */
#else
      session_request_q[as_index] = cur_job;
#endif

      MUTEX_UNLOCK (suspend_mutex);
    }

#ifdef WIN32
  return;
#else
  return NULL;
#endif
}

#if !defined(CAS_BROKER)
static THREAD_FUNC
service_thr_f (void *arg)
{
  int self_index = *((int *) arg);
  int clt_sock_fd;
  T_CLIENT_INFO clt_info;
  time_t job_fetch_time;
  int job_queue_size;
  T_MAX_HEAP_NODE *job_queue;
  T_MAX_HEAP_NODE cur_job;
  int job_wait_count;

  job_queue_size = shm_appl->job_queue_size;
  job_queue = shm_appl->job_queue;

  clt_info.as_index = self_index;
  clt_info.env_buf_alloc_size = ENV_BUF_INIT_SIZE;
  clt_info.env_buf = (char *) malloc (clt_info.env_buf_alloc_size);
  if (clt_info.env_buf == NULL)
    {
      goto finale;
    }

  job_wait_count = 0;

  while (process_flag)
    {
      if (shm_appl->as_info[self_index].session_keep == TRUE)
	{
	  MUTEX_LOCK (session_mutex);
	  if (session_request_q[self_index].clt_sock_fd > 0)
	    {
	      cur_job = session_request_q[self_index];
	      session_request_q[self_index].clt_sock_fd = -1;
	    }
	  else
	    {
	      job_wait_count++;
	      if (shm_br->br_info[br_index].session_timeout >= 0 &&
		  job_wait_count >=
		  shm_br->br_info[br_index].session_timeout * 100)
		{
		  shm_appl->as_info[self_index].uts_status = UTS_STATUS_BUSY;
		  shm_appl->as_info[self_index].session_keep = FALSE;
		  set_close_job (&cur_job);
		  set_close_job_info (&clt_info);
		}
	      else
		{
		  MUTEX_UNLOCK (session_mutex);
		  SLEEP_MILISEC (0, 10);
		  continue;
		}
	    }
	  clt_sock_fd = cur_job.clt_sock_fd;
	  job_wait_count = 0;
	  MUTEX_UNLOCK (session_mutex);
#ifdef SESSION_LOG
	  SESSION_LOG_WRITE (cur_job.ip_addr,
			     shm_appl->as_info[self_index].session_id,
			     cur_job.script, self_index);
#endif
	}
      else
	{
	  if (session_request_q[self_index].clt_sock_fd > 0)
	    {
	      cur_job = session_request_q[self_index];
	      session_request_q[self_index].clt_sock_fd = -1;
	    }
	  else
	    {
	      SLEEP_MILISEC (0, 10);
	      continue;
	    }

	  clt_sock_fd = cur_job.clt_sock_fd;
	  ip2str (cur_job.ip_addr, clt_info.clt_ip_addr);
	}			/* end of else. if (session_keep == FALSE) */

      if (clt_sock_fd > 0)
	{
	  if (read_client_data (clt_sock_fd, &clt_info) < 0)
	    {
	      CLOSE_SOCKET (clt_sock_fd);
	      shm_appl->as_info[self_index].uts_status = UTS_STATUS_IDLE;
	      continue;
	    }
	}

      if ((strcmp (cur_job.script, "OPEN_SESSION") == 0) &&
	  (strcmp (cur_job.prg_name, "OPEN_SESSION") == 0))
	{
	  time_t session_id;
	  char buf[32];
	  if (shm_br->br_info[br_index].session_flag == ON)
	    {
	      MUTEX_LOCK (num_busy_uts_mutex);
	      num_busy_uts++;
	      MUTEX_UNLOCK (num_busy_uts_mutex);

	      session_request_q[self_index].clt_sock_fd = -1;
	      shm_appl->as_info[self_index].session_keep = TRUE;
	      MUTEX_LOCK (session_mutex);
	      session_id = time (NULL);
	      if (session_id <= last_session_id)
		session_id = last_session_id + 1;
	      last_session_id = session_id;
	      shm_appl->as_info[self_index].session_id = session_id;
	      MUTEX_UNLOCK (session_mutex);
	      V3_WRITE_HEADER_OK_FILE_SOCK (clt_sock_fd);
	      sprintf (buf, "%ld", session_id);
	      write_to_client (clt_sock_fd, buf, strlen (buf));
#ifdef SESSION_LOG
	      SESSION_LOG_WRITE (cur_job.ip_addr, session_id, "OPEN_SESSION",
				 self_index);
#endif
	    }
	  else
	    {
	      char *p;
	      V3_WRITE_HEADER_ERR_SOCK (clt_sock_fd);
	      p = "cannot open session";
	      write_to_client (clt_sock_fd, p, strlen (p));
	    }

	  shm_appl->as_info[self_index].uts_status = UTS_STATUS_IDLE;
	  CLOSE_SOCKET (clt_sock_fd);
	  continue;
	}
      else if ((strcmp (cur_job.script, "CLOSE_SESSION") == 0) &&
	       (strcmp (cur_job.prg_name, "CLOSE_SESSION") == 0))
	{
	  shm_appl->as_info[self_index].uts_status = UTS_STATUS_BUSY;
	  shm_appl->as_info[self_index].session_keep = FALSE;
	  MUTEX_LOCK (num_busy_uts_mutex);
	  num_busy_uts--;
	  MUTEX_UNLOCK (num_busy_uts_mutex);
	}

      job_fetch_time = time (NULL);
      (shm_appl->as_info[self_index].num_request)++;
      shm_appl->as_info[self_index].uts_status = UTS_STATUS_BUSY;
      shm_appl->as_info[self_index].last_access_time = job_fetch_time;
      strcpy (shm_appl->as_info[self_index].clt_appl_name,
	      clt_info.clt_appl_name);
      strcpy (shm_appl->as_info[self_index].clt_req_path_info,
	      clt_info.path_info);
      strcpy (shm_appl->as_info[self_index].clt_ip_addr,
	      clt_info.clt_ip_addr);

      last_job_fetch_time = job_fetch_time;

      if (shm_appl->as_info[self_index].session_keep == FALSE)
	{
	  MUTEX_LOCK (num_busy_uts_mutex);
	  num_busy_uts++;
	  MUTEX_UNLOCK (num_busy_uts_mutex);
	}

      process_request (clt_sock_fd, &clt_info);
      CLOSE_SOCKET (clt_sock_fd);

      shm_appl->as_info[self_index].clt_appl_name[0] = '\0';
      shm_appl->as_info[self_index].clt_req_path_info[0] = '\0';
      shm_appl->as_info[self_index].clt_ip_addr[0] = '\0';

      if (shm_appl->as_info[self_index].session_keep == FALSE)
	{
	  MUTEX_LOCK (num_busy_uts_mutex);
	  num_busy_uts--;
	  MUTEX_UNLOCK (num_busy_uts_mutex);

	  /* restart appl_server */
	  /*mutex entry section */
	  shm_appl->as_info[self_index].mutex_flag[SHM_MUTEX_BROKER] = TRUE;
	  shm_appl->as_info[self_index].mutex_turn = SHM_MUTEX_ADMIN;
	  while ((shm_appl->as_info[self_index].mutex_flag[SHM_MUTEX_ADMIN] ==
		  TRUE)
		 && (shm_appl->as_info[self_index].mutex_turn ==
		     SHM_MUTEX_ADMIN))
	    {			/* no-op */
	    }

#if defined(WIN32)
	  if (shm_appl->use_pdh_flag == TRUE)
	    {
	      if (shm_appl->as_info[self_index].pid ==
		  shm_appl->as_info[self_index].pdh_pid
		  && shm_appl->as_info[self_index].pdh_workset >
		  shm_br->br_info[br_index].appl_server_max_size)
		{
		  shm_appl->as_info[self_index].uts_status =
		    UTS_STATUS_RESTART;
		  restart_appl_server (self_index);
		}
	    }
	  else
	    {
	      if (((shm_br->br_info[br_index].appl_server ==
		    APPL_SERVER_UTS_C)
		   || (shm_br->br_info[br_index].appl_server ==
		       APPL_SERVER_UTS_W))
		  && (shm_appl->as_info[self_index].num_request % 1000 == 0))
		{
		  shm_appl->as_info[self_index].uts_status =
		    UTS_STATUS_RESTART;
		  restart_appl_server (self_index);
		}
	    }
#else
	  if (time (NULL) - shm_appl->as_info[self_index].psize_time >
	      PS_CHK_PERIOD)
	    {
	      shm_appl->as_info[self_index].uts_status = UTS_STATUS_RESTART;
	      restart_appl_server (self_index);
	    }
#endif

	  /* mutex exit section */
	  shm_appl->as_info[self_index].mutex_flag[SHM_MUTEX_BROKER] = FALSE;
	}

      shm_appl->as_info[self_index].uts_status = UTS_STATUS_IDLE;
    }

finale:
#ifdef WIN32
  return;
#else
  return NULL;
#endif
}
#elif defined(WIN_FW)
static THREAD_FUNC
service_thr_f (void *arg)
{
  int self_index = *((int *) arg);
  int clt_sock_fd, srv_sock_fd;
  int ip_addr;
  int cas_pid;
  T_MAX_HEAP_NODE cur_job;

  while (process_flag)
    {
      if (session_request_q[self_index].clt_sock_fd > 0)
	{
	  cur_job = session_request_q[self_index];
	  session_request_q[self_index].clt_sock_fd = -1;
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
      if (srv_sock_fd < 0)
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
init_env ()
{
  char *port;
  int n;
  int one = 1;

  /* get a Unix stream socket */
  if ((sock_fd = socket (AF_INET, SOCK_STREAM, 0)) < 0)
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
  memcpy (&sock_addr.sin_addr, &n, sizeof (long));

  if (bind (sock_fd, (struct sockaddr *) &sock_addr, sock_addr_len) < 0)
    {
      UW_SET_ERROR_CODE (UW_ER_CANT_BIND, errno);
      return (-1);
    }

  if (listen (sock_fd, 100) < 0)
    {
      UW_SET_ERROR_CODE (UW_ER_CANT_BIND, 0);
      return (-1);
    }

  return (0);
}

static int
read_from_client (int sock_fd, char *buf, int size)
{
  int read_len;
#ifdef ASYNC_MODE
  SELECT_MASK read_mask;
  int nfound;
  int maxfd;
  struct timeval timeout = { 60, 0 };
#endif

#ifdef ASYNC_MODE
  FD_ZERO (&read_mask);
  FD_SET (sock_fd, (fd_set *) & read_mask);
  maxfd = sock_fd + 1;
  nfound =
    select (maxfd, &read_mask, (SELECT_MASK *) 0, (SELECT_MASK *) 0,
	    &timeout);
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
write_to_client (int sock_fd, char *buf, int size)
{
  int write_len;
#ifdef ASYNC_MODE
  SELECT_MASK write_mask;
  int nfound;
  int maxfd;
  struct timeval timeout = { 60, 0 };
#endif

  if (sock_fd < 0)
    return -1;

#ifdef ASYNC_MODE
  FD_ZERO (&write_mask);
  FD_SET (sock_fd, (fd_set *) & write_mask);
  maxfd = sock_fd + 1;
  nfound =
    select (maxfd, (SELECT_MASK *) 0, &write_mask, (SELECT_MASK *) 0,
	    &timeout);
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

#ifndef CAS_BROKER
static int
read_client_data (int clt_sock_fd, T_CLIENT_INFO * clt_info)
{
  int tmp;
  int read_len;
  int env_buf_size;
  int tot_read_size, read_size;
  int length;
  char clt_addr_str[32];
  char int_str[INT_STR_LEN];
  char *env_buf;
  char *p;

  /* read environment values */
  read_len = read_from_client (clt_sock_fd, int_str, sizeof (int_str));
  if (read_len <= 0)
    {
      return -1;
    }
  env_buf_size = atoi (int_str);
  tmp = env_buf_size;

  sprintf (clt_addr_str, "%s=%s", REMOTE_ADDR_ENV_STR, clt_info->clt_ip_addr);
  env_buf_size += (strlen (clt_addr_str) + 1);

  clt_info->env_buf_size = env_buf_size;
  if (env_buf_size > clt_info->env_buf_alloc_size)
    {
      clt_info->env_buf_alloc_size = ALIGN_ENV_BUF_SIZE (env_buf_size);
      env_buf =
	(char *) realloc (clt_info->env_buf, clt_info->env_buf_alloc_size);
      if (env_buf == NULL)
	{
	  UW_SET_ERROR_CODE (UW_ER_NO_MORE_MEMORY, 0);
	  return -1;
	}
      clt_info->env_buf = env_buf;
    }
  else
    env_buf = clt_info->env_buf;

  tot_read_size = 0;
  while (tot_read_size < tmp)
    {
      read_size =
	read_from_client (clt_sock_fd, env_buf + tot_read_size,
			  tmp - tot_read_size);
      if (read_size <= 0)
	{
	  return -1;
	}
      tot_read_size += read_size;
    }
  memccpy (env_buf + tmp, clt_addr_str, '\0', 10000);

  length = strlen (CONTENT_LENGTH_ENV_STR) + 1;
  clt_info->content_length = 0;
  for (p = env_buf; p - env_buf < env_buf_size;)
    {
      if ((strncmp (p, CONTENT_LENGTH_ENV_STR, length - 1) == 0) &&
	  (p[length - 1] == '='))
	{
	  clt_info->content_length = atoi (p + length);
	  if (clt_info->content_length < 0)
	    clt_info->content_length = 0;
	  break;
	}
      p = p + strlen (p) + 1;
    }

  length = strlen (PATH_INFO_ENV_STR) + 1;
  clt_info->path_info = "";
  for (p = env_buf; p - env_buf < env_buf_size; p += strlen (p) + 1)
    {
      if ((strncmp (p, PATH_INFO_ENV_STR, length - 1) == 0) &&
	  (p[length - 1] == '='))
	{
	  clt_info->path_info = p + length;
	  break;
	}
    }

  length = strlen (CLT_APPL_NAME_ENV_STR) + 1;	/* 1 is '=' */
  clt_info->clt_appl_name = "";
  for (p = env_buf; p - env_buf < env_buf_size; p += strlen (p) + 1)
    {
      if ((strncmp (p, CLT_APPL_NAME_ENV_STR, length - 1) == 0) &&
	  (p[length - 1] == '='))
	{
	  clt_info->clt_appl_name = p + length;
	  break;
	}
    }

  length = strlen (SID_ENV_STR);
  clt_info->session_id = 0;
  for (p = env_buf; p - env_buf < env_buf_size; p += strlen (p) + 1)
    {
      if (strncmp (p, SID_ENV_STR, length) == 0)
	{
	  clt_info->session_id = atoi (p + length + 1);
	  break;
	}
    }

  length = strlen (DELIMITER_ENV_STR) + 1;
  clt_info->delimiter_str = NULL;
  for (p = env_buf; p - env_buf < env_buf_size; p += strlen (p) + 1)
    {
      if ((strncmp (p, DELIMITER_ENV_STR, length - 1) == 0) &&
	  (p[length - 1] == '='))
	{
	  clt_info->delimiter_str = p + length;
	  break;
	}
    }

  length = strlen (OUT_FILE_NAME_ENV_STR) + 1;
  clt_info->out_file_name = NULL;
  for (p = env_buf; p - env_buf < env_buf_size; p += strlen (p) + 1)
    {
      if ((strncmp (p, OUT_FILE_NAME_ENV_STR, length - 1) == 0) &&
	  (p[length - 1] == '='))
	{
	  clt_info->out_file_name = p + length;
	  break;
	}
    }

  return 0;
}
#endif

#ifndef CAS_BROKER
static int
process_request (int clt_sock_fd, T_CLIENT_INFO * clt_info)
{
  int srv_sock_fd;
  char int_str[INT_STR_LEN], read_buf[BUFFER_SIZE];
  char *p;
  int read_len, total_read;
  int write_len, remain_write;
  int err_code, os_err_code = 0;
  int as_index = clt_info->as_index;
  int env_buf_size = clt_info->env_buf_size;
  char *env_buf = clt_info->env_buf;

  srv_sock_fd = connect_srv (shm_br->br_info[br_index].name, as_index);
  if (srv_sock_fd < 0)
    {
      err_code = errno;
      os_err_code = errno;
      goto error;
    }

  if (WRITE_TO_SOCKET (srv_sock_fd, UW_SOCKET_MAGIC, sizeof (UW_SOCKET_MAGIC))
      < 0)
    {
      err_code = UW_ER_COMMUNICATION;
      goto error;
    }

  sprintf (int_str, "%d", env_buf_size);
  WRITE_TO_SOCKET (srv_sock_fd, int_str, sizeof (int_str));
  WRITE_TO_SOCKET (srv_sock_fd, env_buf, env_buf_size);

  total_read = 0;
  while (total_read < clt_info->content_length)
    {
      read_len = read_from_client (clt_sock_fd, read_buf, sizeof (read_buf));
      if (read_len <= 0)
	{
	  goto error;
	}
      total_read += read_len;
      WRITE_TO_SOCKET (srv_sock_fd, read_buf, read_len);
    }

  if (shm_br->br_info[br_index].appl_server == APPL_SERVER_UTS_C &&
      clt_info->out_file_name == NULL)
    {
      V3_WRITE_HEADER_OK_FILE_SOCK (clt_sock_fd);
    }

  write_len = 1;
  while (1)
    {
      read_len = READ_FROM_SOCKET (srv_sock_fd, read_buf, sizeof (read_buf));
      if (read_len <= 0)
	break;
      if (write_len > 0)
	{
	  p = read_buf;
	  remain_write = read_len;
	  while (remain_write > 0)
	    {
	      if (clt_sock_fd < 0)
		break;

	      write_len = write_to_client (clt_sock_fd, p, remain_write);
	      if (write_len <= 0)
		break;
	      p += write_len;
	      remain_write -= write_len;
	    }
	}
    }

  CLOSE_SOCKET (srv_sock_fd);
  return 0;

error:
  ht_error_message (clt_sock_fd, err_code, os_err_code);
  if (srv_sock_fd > 0)
    CLOSE_SOCKET (srv_sock_fd);

  return -1;
}
#endif

#ifndef CAS_BROKER
static void
ht_error_message (int clt_sock_fd, int cur_error_code, int cur_os_errno)
{
  char msg_buf[1024];
  char html_file[FILE_NAME_LEN];

  if (shm_br->br_info[br_index].appl_server == APPL_SERVER_UTS_W)
    {
      char *er_msg = NULL;

      sprintf (msg_buf, "Content-type: text/html\n\n");
      write_to_client (clt_sock_fd, msg_buf, strlen (msg_buf));

      get_cubrid_file (FID_ER_HTML, html_file);
      if (read_er_html (html_file, cur_error_code, cur_os_errno, &er_msg) >=
	  0)
	{
	  write_to_client (clt_sock_fd, er_msg, strlen (er_msg));
	  FREE_MEM (er_msg);
	}
      else
	{
	  sprintf (msg_buf,
		   "<HTML>\n<HEAD>\n<TITLE> Error message from UniWeb </TITLE>\n</HEAD>\n");
	  write_to_client (clt_sock_fd, msg_buf, strlen (msg_buf));
	  sprintf (msg_buf, "<BODY>\n<H2> Error message from UniWeb </H2>\n");
	  write_to_client (clt_sock_fd, msg_buf, strlen (msg_buf));
	  sprintf (msg_buf, "<P> The error code is %d.\n", cur_error_code);
	  write_to_client (clt_sock_fd, msg_buf, strlen (msg_buf));
	  sprintf (msg_buf, "<P> %s\n",
		   uw_get_error_message (cur_error_code, cur_os_errno));
	  write_to_client (clt_sock_fd, msg_buf, strlen (msg_buf));
	  sprintf (msg_buf, "</BODY>\n</HTML>\n");
	  write_to_client (clt_sock_fd, msg_buf, strlen (msg_buf));
	}
    }
  else
    {
      V3_WRITE_HEADER_ERR_SOCK (clt_sock_fd);
      sprintf (msg_buf, "%s\n",
	       uw_get_error_message (cur_error_code, cur_os_errno));
      write_to_client (clt_sock_fd, msg_buf, strlen (msg_buf) + 1);
    }
}
#endif

static int
run_appl_server (int as_index)
{
  char port_str[AS_PORT_STR_SIZE];
  char appl_name[APPL_SERVER_NAME_MAX_SIZE], appl_name_str[64];
  char access_log_env_str[256], error_log_env_str[256];
  int pid;
  char argv0[64];
  char buf[PATH_MAX];
#ifndef WIN32
  int i;
#endif

  while (1)
    {
      MUTEX_LOCK (run_appl_mutex);
      if (run_appl_server_flag)
	{
	  MUTEX_UNLOCK (run_appl_mutex);
	  SLEEP_MILISEC (0, 100);
	  continue;
	}
      else
	{
	  run_appl_server_flag = 1;
	  MUTEX_UNLOCK (run_appl_mutex);
	  break;
	}
    }

  shm_appl->as_info[as_index].service_ready_flag = FALSE;

#ifndef WIN32
  signal (SIGCHLD, SIG_IGN);
#endif

#ifndef WIN32
#ifdef UNIXWARE7
  pid = fork1 ();
#else
  pid = fork ();
#endif
  if (pid == 0)
    {
      signal (SIGCHLD, SIG_DFL);

      for (i = 3; i < 128; i++)
	close (i);
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

      sprintf (argv0, "%s_%s_%d", shm_br->br_info[br_index].name, appl_name,
	       as_index + 1);

#ifdef WIN32
      pid = run_child (appl_name);
#else
      execle (appl_name, argv0, NULL, environ);
#endif

#ifndef WIN32
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
  shm_appl->as_info[as_index].pid = 0;
  shm_appl->as_info[as_index].last_access_time = time (NULL);
  return 0;
}

static void
restart_appl_server (int as_index)
{
  int new_pid;

#if defined(WIN32)
  ut_kill_process (shm_appl->as_info[as_index].pid,
		   shm_br->br_info[br_index].name, as_index);
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
      char pid_file_name[PATH_MAX];
      FILE *fp;
      int old_pid;

      get_cubrid_file (FID_AS_PID_DIR, pid_file_name);
      sprintf (pid_file_name, "%s%s_%d.pid", pid_file_name,
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

  if (shm_appl->as_info[as_index].psize <= 0 ||
      shm_appl->as_info[as_index].psize >
      shm_br->br_info[br_index].appl_server_max_size)
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

#ifndef CAS_BROKER
static void
ip2str (unsigned char *ip, char *ip_str)
{
  sprintf (ip_str, "%d.%d.%d.%d", (unsigned char) ip[0],
	   (unsigned char) ip[1],
	   (unsigned char) ip[2], (unsigned char) ip[3]);
}
#endif

#ifndef CAS_BROKER
static int
str2ip (char *ip_str, unsigned char *ip)
{
  int ip1, ip2, ip3, ip4;

  memset (ip, 0, 4);
  if (sscanf (ip_str, "%d%*c%d%*c%d%*c%d", &ip1, &ip2, &ip3, &ip4) < 4)
    return -1;
  ip[0] = (unsigned char) ip1;
  ip[1] = (unsigned char) ip2;
  ip[2] = (unsigned char) ip3;
  ip[3] = (unsigned char) ip4;
  return 0;
}
#endif

#ifndef CAS_BROKER
static void
set_close_job (T_MAX_HEAP_NODE * job)
{
  job->id = 0;
  job->priority = 0;
  job->clt_sock_fd = -1;
  job->recv_time = time (NULL);
  memset (job->ip_addr, 0, 4);
  strcpy (job->script, "CLOSE_SESSION");
  strcpy (job->prg_name, "CLOSE_SESSION");
}
#endif

#ifndef CAS_BROKER
static void
set_close_job_info (T_CLIENT_INFO * info)
{
  char buf[128];
  char *p;
  int env_buf_size = 0;

  p = info->env_buf;

  info->path_info = p;
  sprintf (buf, "%s=/CLOSE_SESSION", PATH_INFO_ENV_STR);
  env_buf_size += strlen (buf) + 1;
  p = memccpy (p, buf, '\0', 1000000);

  info->clt_appl_name = p;
  sprintf (buf, "%s=CLOSE_SESSION", CLT_APPL_NAME_ENV_STR);
  env_buf_size += strlen (buf) + 1;
  p = memccpy (p, buf, '\0', 1000000);

  info->delimiter_str = p;
  sprintf (buf, "%s=???", DELIMITER_ENV_STR);
  env_buf_size += strlen (buf) + 1;
  p = memccpy (p, buf, '\0', 1000000);

  sprintf (buf, "%s=GET", REQUEST_METHOD_ENV_STR);
  env_buf_size += strlen (buf) + 1;
  p = memccpy (p, buf, '\0', 1000000);

  info->content_length = 0;
  info->session_id = 0;
  strcpy (info->clt_ip_addr, "broker");
  info->out_file_name = NULL;
  info->env_buf_size = env_buf_size;
}
#endif

#ifdef CAS_BROKER
static int
read_nbytes_from_client (int sock_fd, char *buf, int size)
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
#endif

static int
connect_srv (char *br_name, int as_index)
{
  int sock_addr_len;
#ifdef WIN32
  struct sockaddr_in sock_addr;
#else
  struct sockaddr_un sock_addr;
#endif
  int srv_sock_fd;
  int one = 1;
  char retry_count = 0;
#ifndef WIN32
  char buf[PATH_MAX];
#endif

retry:

#ifdef WIN32
  srv_sock_fd = socket (AF_INET, SOCK_STREAM, 0);
  if (srv_sock_fd < 0)
    return UW_ER_CANT_CREATE_SOCKET;

  memset (&sock_addr, 0, sizeof (struct sockaddr_in));
  sock_addr.sin_family = AF_INET;
  sock_addr.sin_port =
    htons ((unsigned short) shm_appl->as_info[as_index].as_port);
  memcpy (&sock_addr.sin_addr, shm_br->my_ip_addr, 4);
  sock_addr_len = sizeof (struct sockaddr_in);
#else
  srv_sock_fd = socket (AF_UNIX, SOCK_STREAM, 0);
  if (srv_sock_fd < 0)
    return UW_ER_CANT_CREATE_SOCKET;

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
      return UW_ER_CANT_CONNECT;
    }

  setsockopt (srv_sock_fd, IPPROTO_TCP, TCP_NODELAY, (char *) &one,
	      sizeof (one));

  return srv_sock_fd;
}

#ifdef CAS_BROKER
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
#ifdef WIN32
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
#ifdef WIN32
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

#ifndef WIN32
  return NULL;
#endif
}
#endif

#ifdef WIN32
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

static int
pdh_init ()
{
  HMODULE h_module;
  PDH_STATUS pdh_status;
  CHAR path_buffer[128];

  h_module = LoadLibrary ("pdh.dll");
  if (h_module == NULL)
    {
      return -1;
    }

  fp_PdhOpenQuery = (PDHOpenQuery) GetProcAddress (h_module, "PdhOpenQueryA");
  if (fp_PdhOpenQuery == NULL)
    {
      return -1;
    }

  fp_PdhAddCounter =
    (PDHAddCounter) GetProcAddress (h_module, "PdhAddCounterA");
  if (fp_PdhAddCounter == NULL)
    {
      return -1;
    }

  fp_PdhCollectQueryData =
    (PDHCollectQueryData) GetProcAddress (h_module, "PdhCollectQueryData");
  if (fp_PdhCollectQueryData == NULL)
    {
      return -1;
    }

  fp_PdhGetFormattedCounterArray =
    (PDHGetFormattedCounterArray) GetProcAddress (h_module,
						  "PdhGetFormattedCounterArrayA");
  if (fp_PdhGetFormattedCounterArray == NULL)
    {
      return -1;
    }

  fp_PdhCloseQuery =
    (PDHCloseQuery) GetProcAddress (h_module, "PdhCloseQuery");
  if (fp_PdhCloseQuery == NULL)
    {
      return -1;
    }

  pdh_status = (*fp_PdhOpenQuery) (0, 0, &pdh_h_query);
  if (pdh_status != ERROR_SUCCESS)
    {
      return -1;
    }

  strcpy (path_buffer, "\\Process(*)\\ID Process");
  pdh_status =
    (*fp_PdhAddCounter) (pdh_h_query, path_buffer, 0, &counter_pid);
  if (pdh_status != ERROR_SUCCESS)
    {
      return -1;
    }

  strcpy (path_buffer, "\\Process(*)\\Working Set");
  pdh_status =
    (*fp_PdhAddCounter) (pdh_h_query, path_buffer, 0, &counter_workset);
  if (pdh_status != ERROR_SUCCESS)
    {
      return -1;
    }

  strcpy (path_buffer, "\\Process(*)\\% Processor Time");
  pdh_status =
    (*fp_PdhAddCounter) (pdh_h_query, path_buffer, 0, &counter_pct_cpu);
  if (pdh_status != ERROR_SUCCESS)
    {
      return -1;
    }

  strcpy (path_buffer, "\\Process(*)\\Thread Count");
  pdh_status =
    (*fp_PdhAddCounter) (pdh_h_query, path_buffer, 0, &counter_num_thr);
  if (pdh_status != ERROR_SUCCESS)
    {
      return -1;
    }

  num_counter_value = 128;

  ALLOC_COUNTER_VALUE ();
  if (IS_COUNTER_VALUE_PTR_NULL ())
    {
      return -1;
    }
  memset (cntvalue_pid, 0,
	  sizeof (PDH_FMT_COUNTERVALUE_ITEM) * num_counter_value);
  memset (cntvalue_workset, 0,
	  sizeof (PDH_FMT_COUNTERVALUE_ITEM) * num_counter_value);
  memset (cntvalue_pct_cpu, 0,
	  sizeof (PDH_FMT_COUNTERVALUE_ITEM) * num_counter_value);
  memset (cntvalue_num_thr, 0,
	  sizeof (PDH_FMT_COUNTERVALUE_ITEM) * num_counter_value);

  return 0;
}

static int
pdh_collect ()
{
  unsigned long in_size;
  PDH_STATUS pdh_status;
  int i, retry_count = 10;
  char success_flag = FALSE;

  if (IS_COUNTER_VALUE_PTR_NULL ())
    {
      ALLOC_COUNTER_VALUE ();
      if (IS_COUNTER_VALUE_PTR_NULL ())
	goto collect_error;
    }

  for (i = 0; i < retry_count; i++)
    {
      pdh_status = (*fp_PdhCollectQueryData) (pdh_h_query);
      if (pdh_status != ERROR_SUCCESS)
	{
	  continue;
	}
      in_size = sizeof (PDH_FMT_COUNTERVALUE_ITEM) * num_counter_value;

      pdh_status =
	(*fp_PdhGetFormattedCounterArray) (counter_pid, PDH_FMT_LONG,
					   &in_size, &pdh_num_proc,
					   cntvalue_pid);
      if (pdh_status != ERROR_SUCCESS)
	{
	  if (pdh_status == PDH_MORE_DATA)
	    {
	      num_counter_value *= 2;
	      ALLOC_COUNTER_VALUE ();
	      if (IS_COUNTER_VALUE_PTR_NULL ())
		{
		  goto collect_error;
		}
	    }
	  continue;
	}
      pdh_status =
	(*fp_PdhGetFormattedCounterArray) (counter_workset, PDH_FMT_LONG,
					   &in_size, &pdh_num_proc,
					   cntvalue_workset);
      if (pdh_status != ERROR_SUCCESS)
	{
	  continue;
	}
      pdh_status =
	(*fp_PdhGetFormattedCounterArray) (counter_pct_cpu, PDH_FMT_DOUBLE,
					   &in_size, &pdh_num_proc,
					   cntvalue_pct_cpu);
      if (pdh_status != ERROR_SUCCESS)
	{
	  continue;
	}
      pdh_status =
	(*fp_PdhGetFormattedCounterArray) (counter_num_thr, PDH_FMT_LONG,
					   &in_size, &pdh_num_proc,
					   cntvalue_num_thr);
      if (pdh_status != ERROR_SUCCESS)
	{
	  continue;
	}

      success_flag = TRUE;
      break;
    }

  if (success_flag == TRUE)
    {
      return 0;
    }

collect_error:
  pdh_num_proc = 0;
  return -1;
}

static int
pdh_get_value (int pid, int *workset, float *pct_cpu, int *br_num_thr)
{
  unsigned long i;

  if (pid <= 0)
    {
      *workset = 0;
      *pct_cpu = 0;
      if (br_num_thr)
	*br_num_thr = 0;
      return 0;
    }

  for (i = 0; i < pdh_num_proc; i++)
    {
      if (cntvalue_pid[i].FmtValue.longValue == pid)
	{
	  *workset = (int) (cntvalue_workset[i].FmtValue.longValue / 1024);
	  *pct_cpu = (float) (cntvalue_pct_cpu[i].FmtValue.doubleValue);
	  if (br_num_thr)
	    {
	      *br_num_thr = (int) (cntvalue_num_thr[i].FmtValue.longValue);
	    }
	  return 0;
	}
    }

  return -1;
}
#else /* ifndef WIN32 */
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
  char log_filename[512];

  if (shm_br->br_info[br_index].appl_server != APPL_SERVER_CAS)
    return;
  if (!(shm_appl->sql_log_mode & SQL_LOG_MODE_ON))
    return;

  get_cubrid_file (FID_SQL_LOG_DIR, log_filename);
  sprintf (log_filename, "%s%s_%d.sql.log", log_filename, br_name,
	   as_index + 1);

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
process_cas_request (int cas_pid, int as_index, int clt_sock_fd,
		     int srv_sock_fd)
{
  char read_buf[1024];
  int msg_size;
  int read_len;
  int tmp_int;
  char *tmp_p;
#ifdef WIN32
  int glo_size;
#endif

#ifdef WIN32
  shm_appl->as_info[as_index].glo_flag = 0;
#endif

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
#ifdef WIN32
      while (shm_appl->as_info[as_index].glo_flag)
	{
	  SLEEP_MILISEC (0, 10);
	}
      shm_appl->as_info[as_index].glo_flag = 1;
      shm_appl->as_info[as_index].glo_read_size = -1;
      shm_appl->as_info[as_index].glo_write_size = -1;
#endif
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

#ifdef WIN32
      while (shm_appl->as_info[as_index].glo_read_size < 0)
	{
	  SLEEP_MILISEC (0, 10);
	}
      glo_size = shm_appl->as_info[as_index].glo_read_size;
      while (glo_size > 0)
	{
	  read_len =
	    read_from_cas_client (clt_sock_fd, read_buf,
				  (glo_size >
				   sizeof (read_buf) ? sizeof (read_buf) :
				   glo_size), as_index, cas_pid);
	  if (read_len <= 0)
	    return -1;
	  if (send (srv_sock_fd, read_buf, read_len, 0) < read_len)
	    return -1;
	  glo_size -= read_len;
	}
#endif

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

#ifdef WIN32
      while (shm_appl->as_info[as_index].glo_write_size < 0)
	{
	  SLEEP_MILISEC (0, 10);
	}
      glo_size = shm_appl->as_info[as_index].glo_write_size;
      while (glo_size > 0)
	{
	  read_len =
	    recv (srv_sock_fd, read_buf,
		  (glo_size >
		   sizeof (read_buf) ? sizeof (read_buf) : glo_size), 0);
	  if (read_len <= 0)
	    {
	      return -1;
	    }
	  if (write_to_client (clt_sock_fd, read_buf, read_len) < 0)
	    return -1;
	  glo_size -= read_len;
	}
#endif

      if (shm_appl->as_info[as_index].close_flag ||
	  shm_appl->as_info[as_index].pid != cas_pid)
	{
	  break;
	}
    }

  return 0;
}

static int
read_from_cas_client (int sock_fd, char *buf, int size, int as_index,
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
  nfound =
    select (maxfd, &read_mask, (SELECT_MASK *) 0, (SELECT_MASK *) 0,
	    &timeout);
  if (nfound < 1)
    {
      if (shm_appl->as_info[as_index].close_flag ||
	  shm_appl->as_info[as_index].pid != cas_pid)
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
      if (shm_appl->as_info[i].session_keep == TRUE)
	{
	  continue;
	}
      if (shm_appl->as_info[i].service_flag != SERVICE_ON)
	{
	  continue;
	}
      if (shm_appl->as_info[i].uts_status == UTS_STATUS_IDLE)
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
    }				/* end of for */

  if (wait_cas_id >= 0)
    {
      MUTEX_LOCK (con_status_mutex);
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
      MUTEX_UNLOCK (con_status_mutex);
    }

#if defined(CAS_BROKER) && defined(WIN32)
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

  if (shm_br->br_info[br_index].appl_server != APPL_SERVER_CAS)
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

      if (shm_appl->as_info[i].uts_status == UTS_STATUS_BUSY &&
	  shm_appl->as_info[i].con_status == CON_STATUS_OUT_TRAN &&
	  wait_time > max_wait_time &&
	  wait_time > shm_br->br_info[br_index].time_to_kill &&
	  exist_idle_cas == 0)
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
