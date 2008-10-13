/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * repl_svr.h : header file for the repl_server
 *
 */

#ifndef _REPL_SVR_H_
#define _REPL_SVR_H_

#ident "$Id$"

#include "log_prv.h"

/* the maximum number of connections  */
#define MAX_NUM_CONNECTIONS              (max_Agent_num+1)
#define MAX_WORKER_THREAD                2
#define DEFAULT_MAX_AGENTS               10

/* the TCP Port number to listen the request */
#define REPL_SERVER_PORT                 (port_Num)
#define REPL_ACTIVE_AGENT_NUM            (active_Conn_num-1)


#define REPL_LOG_BUFFER_SIZE            100

#define REPL_SIMPLE_BUF_SIZE            (repl_Log.pgsize + COMM_RESP_BUF_SIZE )

/* Simple String buffer */
typedef struct
{
  char *data;
  char *result;

  int length;
} SIMPLE_BUF;

/* The log volume info */
typedef struct
{
  int log_vdes;
  LOG_PAGE *hdr_page;
  struct log_header *log_hdr;
  int pgsize;
} REPL_ACT_LOG;

typedef struct
{
  int log_vdes;
  LOG_PAGE *hdr_page;
  struct log_arv_header *log_hdr;
  int arv_num;
} REPL_ARV_LOG;


/* The request queue structure */
typedef struct
{
  int agent_fd;
  char req_buf[COMM_REQ_BUF_SIZE];
} REPL_REQUEST;

/* Page buffer structure per repl_agent */
typedef struct
{
  REPL_LOG_BUFFER **log_buffer;
  int head;
  int tail;
  PAGEID start_pageid;		/* start point to copy log  */
  PAGEID max_pageid;		/* min pageid in the buffer */
  bool need_shutdown;
  bool on_demand;
  pthread_mutex_t mutex;
  pthread_cond_t read_cond;
  pthread_cond_t write_cond;
} REPL_PB;

/* connection info list per repl_agent */
typedef struct repl_conn
{
  int fd;			/* socket descriptor */
  int agentid;			/* unique agent id */
  pthread_t read_thread;	/* log reader thread per agent */
  REPL_PB *pb;			/* page buffer area */
  struct repl_conn *next;
} REPL_CONN;

/* repl_agent info list */
typedef struct repl_agent_info
{
  int agentid;			/* unique agent id */
  char ip[20];			/* agent ip address */
  int port_id;			/* agent port id for status check */
  PAGEID safe_pageid;
  struct repl_agent_info *next;
} REPL_AGENT_INFO;

/* The job queue structure for each worker thread */
typedef struct repl_tpool_work
{
  void (*routine) (void *);	/* process routine */
  void *arg;			/* req. buffer */
  struct repl_tpool_work *next;	/* next job */
} REPL_TPOOL_WORK;

/* The thread pool structure */
typedef struct repl_tpool
{
  /* pool characteristics */
  int max_queue_size;
  int do_not_block_when_full;
  /* pool state */
  int cur_queue_size;
  REPL_TPOOL_WORK *queue_head;
  REPL_TPOOL_WORK *queue_tail;
  int queue_closed;
  int shutdown;
  /* pool synchronization */
  pthread_mutex_t queue_lock;
  pthread_cond_t queue_not_empty;
  pthread_cond_t queue_not_full;
  pthread_cond_t queue_empty;
} *REPL_TPOOL;

extern int repl_svr_process_get_log_req (int agentid, PAGEID pageid,
					 bool * in_archive,
					 SIMPLE_BUF ** data);
extern int repl_svr_process_read_log_req (int agentid, PAGEID pageid,
					  bool * in_archive,
					  SIMPLE_BUF ** data);
extern int repl_svr_process_log_hdr_req (int agentid, REPL_REQUEST * req,
					 SIMPLE_BUF ** data);
extern int repl_svr_process_agent_id_req (REPL_REQUEST * req, int port_id,
					  int *agentid);
extern int repl_svr_tp_init (int thread_num, int do_not_block_when_full);
extern int repl_svr_tp_add_work (void (*routine) (void *), void *arg);
extern int repl_svr_tp_destroy (int finish);
extern bool repl_check_shutdown (void);
extern int repl_svr_tp_start (void);
extern bool repl_svr_check_shutdown (void);
extern void repl_svr_shutdown_immediately (void);
extern char *repl_svr_get_ip (int agent_fd);
extern int repl_svr_sock_get_request ();
extern int repl_svr_sock_send_result (int agent_fd, int result);
extern int repl_svr_sock_send_logpage (int agent_fd, int result,
				       bool in_archive, SIMPLE_BUF * buf);
extern int repl_svr_sock_shutdown (void);
extern void repl_svr_thread_end (void);
extern int repl_svr_sock_init (void);
extern void repl_svr_remove_conn (int agent_fd);
extern void repl_svr_clear_all_conn (void);
extern REPL_CONN *repl_svr_get_new_repl_connection (void);
extern REPL_CONN *repl_svr_get_main_connection (void);
extern REPL_CONN *repl_svr_get_repl_connection (int agent_fd);
extern int repl_svr_add_repl_connection (REPL_CONN * node);
extern int repl_svr_clear_repl_connection (REPL_CONN * connection);
extern void repl_process_request (void *input_orderp);

extern FILE *err_Log_fp;
extern int agent_Num;
extern REPL_ACT_LOG repl_Log;
extern REPL_ARV_LOG repl_Arv;
extern REPL_CONN *repl_Conn_h;	/* pointer to the header of
				 * connection info list
				 */
extern int active_Conn_num;
extern REPL_AGENT_INFO *agent_List;	/* agent list info */
extern FILE *agent_List_fp;	/* agent list file */
extern pthread_mutex_t file_Mutex;
extern int max_Agent_num;

#endif /* _REPL_SVR_H_ */
