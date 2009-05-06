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
 * repl_svr_tp.c : hread handler routine for repl_server only.
 *
 * Note:
 *     repl_server has a three kinds of threads.
 *
 *     1. Main thread
 *        - initialize the resources
 *        - start up the SEND threads
 *        - distribute the requests of repl_agent to the SEND threads
 *        - finalize
 *     2. a set of SEND threads
 *        - process the requests of repl_agent
 *        - send the TR logs to the repl_agent
 *        - start up a READ threads to prefetch the logs
 *     3. READ threads
 *        - prefetch the transaction logs
 *        - when the mode of log copy is set "on-demand", exit
 */

#ident "$Id$"

#include "porting.h"
#include "utility.h"
#include "repl_support.h"
#include "repl_tp.h"
#include "repl_server.h"

REPL_TPOOL repl_tpool = NULL;

pthread_t *repl_tr_send_thread;	/* log sender thread array */
pthread_key_t d_key;
pthread_mutex_t file_Mutex;	/* to access the common resource
				 *  - repl_Log, err_Log_fp
				 */
pthread_mutex_t error_Mutex;

/* for generation of agent id */
int agent_ID = 1;

#define SIZEOF_REPL_LOG_BUFFER \
        (offsetof(REPL_LOG_BUFFER, logpage) + repl_Log.pgsize)

#define REPL_LOG_IS_IN_ARCHIVE(pageid)                                        \
        ((pageid) < repl_Log.log_hdr->nxarv_pageid                            \
         && ((pageid) + repl_Log.log_hdr->npages)                             \
              <= repl_Log.log_hdr->append_lsa.pageid)

#define REPL_LOG_IS_IN_ARCHIVE2(pageid, current_pageid)                       \
        ((pageid) < repl_Log.log_hdr->nxarv_pageid ||                         \
         (pageid) < current_pageid )

#define SLEEP_USEC(sec, usec)                        \
        do {                                            \
          struct timeval sleep_time_val;                \
          sleep_time_val.tv_sec = sec;                  \
          sleep_time_val.tv_usec = usec;                \
          select(0, 0, 0, 0, &sleep_time_val);          \
        } while (0)

static int repl_fetch_log_hdr (void);
static PAGEID repl_log_to_phypageid (PAGEID logical_pageid);
static void *repl_svr_tr_read (void *arg);
static void repl_svr_dkey_delete (void *value);
static REPL_AGENT_INFO *repl_get_agent_info (char *ip);
static void *repl_svr_get_send_data_buffer (void);
static int
repl_svr_init_new_agent (int agentid, int agent_fd, int agent_port_id);
static void *repl_svr_tr_send ();
static int
repl_log_pbfetch (REPL_PB * pb, int index, PAGEID pageid, int pagesize);
static int repl_pbfetch_from_archive (PAGEID pageid, char *data);
static void *repl_svr_find_conn_or_buf (int agentid, bool bufyn);
static void repl_svr_shutdown_by_signal (int sig_no);

#if 0
/******************************************************************************
 * repl_svr_close_pb : clear the page buffer used by reader thread.
 *
 * arguments:
 *      REPL_CONN     conn : socket of agent connection
 *
 * returns/side-effects : N/A
 *
 * description :
 *
 *****************************************************************************/
static void
repl_svr_close_pb (REPL_CONN * conn)
{
  int i;
  REPL_PB *pb = conn->pb;

  /* waits for the repl_agent requests "disk read" request */
  PTHREAD_MUTEX_LOCK (pb->mutex);
  while (!pb->need_shutdown && pb->on_demand == false)
    PTHREAD_COND_TIMEDWAIT (pb->write_cond, pb->mutex);

  PTHREAD_MUTEX_UNLOCK (pb->mutex);

  for (i = 0; i < REPL_LOG_BUFFER_SIZE && pb->log_buffer[i]; i++)
    {
      free_and_init (pb->log_buffer[i]);
      pb->log_buffer[i] = NULL;
    }

  free_and_init (pb->log_buffer);
  pb->log_buffer = NULL;

  PTHREAD_COND_DESTROY (pb->read_cond);
  PTHREAD_COND_DESTROY (pb->write_cond);
  PTHREAD_MUTEX_DESTROY (pb->mutex);

  free_and_init (pb);
  conn->pb = NULL;
}
#endif

REPL_CONN *
repl_svr_get_repl_connection (int agent_fd)
{
  REPL_CONN *conn_p = repl_Conn_h;

  while (conn_p)
    {
      if (conn_p->fd > 0 && conn_p->fd == agent_fd)
	{
	  break;
	}
      conn_p = conn_p->next;
    }

  return conn_p;
}

REPL_CONN *
repl_svr_get_main_connection (void)
{
  REPL_CONN *conn_p = repl_Conn_h;

  while (conn_p)
    {
      if (conn_p->fd > 0 && conn_p->agentid == -1)
	{
	  break;
	}
      conn_p = conn_p->next;
    }

  return conn_p;
}

REPL_CONN *
repl_svr_get_new_repl_connection (void)
{
  REPL_CONN *conn_p;
  char *buffer_p;
  int size;
  int i;

  size = sizeof (REPL_CONN);
  size += sizeof (REPL_PB);
  size += sizeof (REPL_LOG_BUFFER *) * REPL_LOG_BUFFER_SIZE;
  size += SIZEOF_REPL_LOG_BUFFER * REPL_LOG_BUFFER_SIZE;

  buffer_p = (char *) malloc (size);
  if (buffer_p == NULL)
    {
      return NULL;
    }
  memset (buffer_p, 0, size);

  conn_p = (REPL_CONN *) buffer_p;
  buffer_p = buffer_p + sizeof (REPL_CONN);

  /* allocate the page buffer area */
  conn_p->pb = (REPL_PB *) buffer_p;
  buffer_p = buffer_p + sizeof (REPL_PB);

  conn_p->pb->log_buffer = (REPL_LOG_BUFFER **) buffer_p;
  buffer_p = buffer_p + sizeof (REPL_LOG_BUFFER *) * REPL_LOG_BUFFER_SIZE;
  for (i = 0; i < REPL_LOG_BUFFER_SIZE; i++)
    {
      conn_p->pb->log_buffer[i] = (REPL_LOG_BUFFER *) buffer_p;
      buffer_p = buffer_p + SIZEOF_REPL_LOG_BUFFER;
    }

  conn_p->fd = 0;
  conn_p->agentid = -1;
  conn_p->pb->head = 0;
  conn_p->pb->tail = 0;
  conn_p->pb->start_pageid = 0;
  conn_p->pb->max_pageid = 0;
  conn_p->pb->need_shutdown = false;
  conn_p->pb->on_demand = false;
  conn_p->next = NULL;

  PTHREAD_MUTEX_INIT (conn_p->pb->mutex);
  PTHREAD_COND_INIT (conn_p->pb->read_cond);
  PTHREAD_COND_INIT (conn_p->pb->write_cond);

  return (conn_p);
}

int
repl_svr_clear_repl_connection (REPL_CONN * connection)
{
  if (connection)
    {
      free_and_init (connection);
    }

  return NO_ERROR;
}

int
repl_svr_add_repl_connection (REPL_CONN * conn_p)
{
  if (repl_Conn_h)
    conn_p->next = repl_Conn_h;
  repl_Conn_h = conn_p;

  return NO_ERROR;
}

/*
 * repl_svr_init_new_agent() - initialize the connection info & page buffers
 *   return:  error code
 *   agentid : agent id
 *   agent_fd : socket of agent connection
 *
 * Note:
 *      When the agent contacts to the server, server generates a unique
 *      agent id and prepares the log buffer areas and log reader thread.
 *
 *      The log reader thread works for prefetching the log pages to improve
 *      the performance. If it reads all the current pages, it dies and
 *      the sender thread reads the physical log pages directly.
 *      Because, the reader thread doesn't know where is the end of log.
 *      (the log header is not flushed whenever the server commits...
 *       So, the only one who knows the end of log is an apply thread of
 *       repl_agent. When the repl_agent meets "end of log", it tries to read
 *       the target log page from the disk to apply the transactions
 *       promptly.)
 */
static int
repl_svr_init_new_agent (int agentid, int agent_fd, int agent_port_id)
{
  REPL_CONN *conn_p;
  char *ip;
  REPL_AGENT_INFO *agent_info;

  conn_p = repl_svr_get_repl_connection (agent_fd);
  REPL_CHECK_ERR_NULL (REPL_FILE_SVR_TP, REPL_SERVER_MEMORY_ERROR, conn_p);

  conn_p->agentid = agentid;

  ip = repl_svr_get_ip (conn_p->fd);

  agent_info = repl_get_agent_info (ip);
  if (agent_info == NULL)
    {
      agent_info = (REPL_AGENT_INFO *) malloc (DB_SIZEOF (REPL_AGENT_INFO));
      if (agent_info == NULL)
	{
	  repl_svr_remove_conn (conn_p->fd);
	  REPL_ERR_RETURN (REPL_FILE_SVR_TP, REPL_SERVER_MEMORY_ERROR);
	}
      agent_info->next = agent_List;
      agent_List = agent_info;
    }
  strcpy (agent_info->ip, ip);
  agent_info->port_id = agent_port_id;
  agent_info->safe_pageid = -1;
  agent_info->agentid = agentid;

  PTHREAD_CREATE (conn_p->read_thread, NULL, repl_svr_tr_read, conn_p);

  return NO_ERROR;
}

/*
 * repl_svr_remove_conn() - free  the connection info for a specific connection
 *   return: none
 *   conn : socket of agent connection
 */
void
repl_svr_remove_conn (int fd)
{
  REPL_CONN *conn_p = repl_Conn_h;
  REPL_CONN *prev_conn_p = NULL;

  while (conn_p)
    {
      if (conn_p->fd == fd)
	{
	  break;
	}
      prev_conn_p = conn_p;
      conn_p = conn_p->next;
    }

  if (conn_p)
    {
      if (conn_p == repl_Conn_h)
	{
	  repl_Conn_h = conn_p->next;
	}
      else
	{
	  prev_conn_p->next = conn_p->next;
	}

      if (active_Conn_num > 0)
	{
	  active_Conn_num--;
	}

      if (conn_p->pb)
	{
	  conn_p->pb->need_shutdown = true;
	  if (conn_p->agentid != -1 && conn_p->read_thread != 0)
	    {
	      PTHREAD_JOIN (conn_p->read_thread);
	    }
	  conn_p->pb->on_demand = true;
	}
      close (conn_p->fd);
      repl_svr_clear_repl_connection (conn_p);
    }
}

/*
 * repl_svr_clear_all_conn() - free  all  the connection info
 *   return: none
 */
void
repl_svr_clear_all_conn (void)
{
  REPL_CONN *conn_p;

  while (repl_Conn_h)
    {
      conn_p = repl_Conn_h;
      repl_Conn_h = repl_Conn_h->next;
      close (conn_p->fd);
      repl_svr_clear_repl_connection (conn_p);
    }
}

/*
 * repl_svr_find_conn_and_buf() - find out the target connection & buffer
 *   return: if bufyn is true, the pointer to the page buffer, otherwise,
 *           the pointer to the connection info.
 *   agentid: agent identifier
 *   bufyn  : true if the caller wants the page buffer.
 *
 * Note:
 *    This function is used when the caller wants the connection or page
 *    buffer information for a specific agent identified by agentid.
 *    repl_server assigns a page buffer area for a repl_agent. When the
 *    repl_agent contacts to the repl_server first time, the repl_server
 *    allocates and sends an unique ID for it. Next time the repl_agent
 *    the repl_server, it should attach the agent id to the request buffer for
 *    requests something to the repl_server to know which repl_agent sends
 *    the request.
 */
static void *
repl_svr_find_conn_or_buf (int agentid, bool bufyn)
{
  REPL_CONN *conn_p;

  conn_p = repl_Conn_h;
  while (conn_p)
    {
      if (conn_p->agentid == agentid)
	{
	  if (bufyn)
	    return (void *) conn_p->pb;
	  else
	    return (void *) conn_p;
	}
      conn_p = conn_p->next;
    }
  return NULL;
}

/*
 * repl_svr_get_send_data_buffer() - returns the pointer to the send
 *                                   data buffer
 *   return: a pointer to the buffer
 *
 * Note:
 *        For the SEND threads, we create a key for a send data buffer,
 *        TSD(Thread Specific Data) in order to avoid memory allocation
 *        for every request.
 *        The size of data buffer would be same as the size of the log page
 *        (The only job the SNED threads have to is sending a log page..)
 *
 *         by SEND threads
 */
static void *
repl_svr_get_send_data_buffer (void)
{
  SIMPLE_BUF *buf;
  int error = NO_ERROR;

  buf = (SIMPLE_BUF *) PTHREAD_GETSPECIFIC (d_key);

  if (buf == NULL)
    {
      buf = (SIMPLE_BUF *) malloc (DB_SIZEOF (SIMPLE_BUF));
      if (buf == NULL)
	{
	  REPL_ERR_LOG (REPL_FILE_SVR_TP, REPL_SERVER_MEMORY_ERROR);
	  return NULL;
	}

      buf->data = (char *) malloc (REPL_SIMPLE_BUF_SIZE);
      if (buf->data == NULL)
	{
	  REPL_ERR_LOG (REPL_FILE_SVR_TP, REPL_SERVER_MEMORY_ERROR);
	  return NULL;
	}
      buf->result = (char *) (buf->data + repl_Log.pgsize);
      buf->length = REPL_SIMPLE_BUF_SIZE;
      PTHREAD_SETSPECIFIC (d_key, (void *) buf);
    }
  return (void *) buf;
}

/*
 * repl_log_to_phypageid() - get the physical page id from the logical pageid
 *   return: physical page id
 *   logical_pageid : logical page id
 *
 * Note:
 *      active log      0, 1, 2, .... 4999   (total 5,000 pages)
 *      archive log0
 *
 *   called by SEND thread or READ thread
 *   caller should do "mutex lock"
 *
 */
static PAGEID
repl_log_to_phypageid (PAGEID logical_pageid)
{
  PAGEID phy_pageid;

  if (logical_pageid == LOG_HDR_PAGEID)
    phy_pageid = REPL_LOG_PHYSICAL_HDR_PAGEID;
  else
    {
      phy_pageid = logical_pageid - repl_Log.log_hdr->fpageid;
      if (phy_pageid >= repl_Log.log_hdr->npages)
	phy_pageid %= repl_Log.log_hdr->npages;
      else if (phy_pageid < 0)
	phy_pageid = repl_Log.log_hdr->npages -
	  ((-phy_pageid) % repl_Log.log_hdr->npages);
      phy_pageid++;
    }
  return phy_pageid;
}

/*
 * repl_fetch_log_hdr() - Reads the log header file from the disk
 *   return: error code
 *
 * Note:
 *     called by SEND thread
 *     caller should do "mutex lock"
 */
static int
repl_fetch_log_hdr (void)
{
  int error = NO_ERROR;

  error = repl_io_read (repl_Log.log_vdes, (void *) repl_Log.hdr_page,
			0, repl_Log.pgsize);
  REPL_CHECK_ERR_ERROR (REPL_FILE_SVR_TP, REPL_SERVER_IO_ERROR);

  repl_Log.log_hdr = (struct log_header *) (repl_Log.hdr_page->area);

  return error;
}

/*
 * repl_pbfetch_from_archive() - read the log page from archive
 *   return: error code
 *   pageid: requested pageid
 *
 * Note:
 *     When the master db generates the archive, and the repl_agent requests
 *     the page in archive, repl_server fetches the page from the archive.
 */
static int
repl_pbfetch_from_archive (PAGEID pageid, char *data)
{
  int error = NO_ERROR;
  char arv_name[PATH_MAX];
  int loop_cnt = 0;

  /* Start from the first archive. --> need to guess the target archive
   *                                   directly.
   */
  while (loop_cnt < repl_Log.log_hdr->nxarv_num &&
	 repl_Arv.arv_num >= 0 &&
	 repl_Arv.arv_num <= repl_Log.log_hdr->nxarv_num)
    {
      /* is the first time to access the target archive? */
      if (repl_Arv.log_vdes == 0)
	{
	  fileio_make_log_archive_name (arv_name, log_Archive_path,
					log_Prefix, repl_Arv.arv_num);
	  /* open the archive file */
	  repl_Arv.log_vdes = repl_io_open (arv_name, O_RDONLY, 0);
	  if (repl_Arv.log_vdes == NULL_VOLDES)
	    {
	      repl_Arv.arv_num++;
	      repl_Arv.log_vdes = 0;
	      loop_cnt++;
	      continue;
	    }
	  /* If this is the frist time to read archive log,
	   * read the header info of the target archive
	   */
	  if (repl_Arv.hdr_page == NULL)
	    {
	      repl_Arv.hdr_page = (LOG_PAGE *) malloc (repl_Log.pgsize);
	      REPL_CHECK_ERR_NULL (REPL_FILE_SVR_TP, REPL_SERVER_MEMORY_ERROR,
				   repl_Arv.hdr_page);
	    }

	  error = repl_io_read (repl_Arv.log_vdes, repl_Arv.hdr_page, 0,
				repl_Log.pgsize);
	  REPL_CHECK_ERR_ERROR_ONE_ARG (REPL_FILE_SVR_TP,
					REPL_SERVER_CANT_READ_ARCHIVE,
					arv_name);
	  repl_Arv.log_hdr =
	    (struct log_arv_header *) repl_Arv.hdr_page->area;
	}

      /* is the right archive file ? */
      if (pageid >= repl_Arv.log_hdr->fpageid
	  && pageid < repl_Arv.log_hdr->fpageid + repl_Arv.log_hdr->npages)
	{
	  error = repl_io_read (repl_Arv.log_vdes, data,
				pageid - repl_Arv.log_hdr->fpageid + 1,
				repl_Log.pgsize);
	  REPL_CHECK_ERR_ERROR (REPL_FILE_SVR_TP, REPL_SERVER_IO_ERROR);
	  return error;
	}
      /* we have to try the next archive file */
      else
	{
	  repl_Arv.arv_num++;
	  close (repl_Arv.log_vdes);
	  repl_Arv.log_vdes = 0;
	}
      if (repl_Arv.arv_num >= repl_Log.log_hdr->nxarv_num)
	repl_Arv.arv_num = 0;
      loop_cnt++;
    }
  /* Oh Oh, there is no archive to save this page */
  REPL_ERR_RETURN_ONE_ARG (REPL_FILE_SVR_TP,
			   REPL_SERVER_CANT_OPEN_ARCHIVE, arv_name);
}

/*
 * repl_log_pbfetch() - fetch a log page and write it to the page buffer
 *   return: error code
 *    pb       : pointer to the page buffer
 *    index    : the target index of page buffer
 *    pageid   : the target page id
 *    pagesize : the size of page
 *
 * Note:
 *   called by READ threads, the caller should do "mutex lock"
 */
static int
repl_log_pbfetch (REPL_PB * pb, int index, PAGEID pageid, int pagesize)
{
  REPL_LOG_BUFFER *log_bufptr;
  PAGEID phy_pageid = NULL_PAGEID;
  int error = NO_ERROR;
  int retry = 5;

  /* get the physical page id */
  phy_pageid = repl_log_to_phypageid (pageid);

  /* set the target buffer ? */
  log_bufptr = pb->log_buffer[index];

  /* read the target page */
  if (REPL_LOG_IS_IN_ARCHIVE (pageid))
    {
      error = repl_pbfetch_from_archive (pageid,
					 (char *) &log_bufptr->logpage);
      log_bufptr->in_archive = true;
      REPL_CHECK_ERR_ERROR (REPL_FILE_SVR_TP, REPL_SERVER_IO_ERROR);
    }
  else
    {
      error = repl_io_read (repl_Log.log_vdes, &log_bufptr->logpage,
			    phy_pageid, pagesize);
      log_bufptr->in_archive = false;
      REPL_CHECK_ERR_ERROR (REPL_FILE_SVR_TP, REPL_SERVER_IO_ERROR);
    }

  while (error == NO_ERROR && retry > 0
	 && log_bufptr->logpage.hdr.logical_pageid != pageid)
    {
      /* if the master generates the log archive,
       * re-fetch the log header, try again */
      error = repl_fetch_log_hdr ();
      REPL_CHECK_ERR_ERROR (REPL_FILE_SVR_TP, REPL_SERVER_IO_ERROR);

      if (REPL_LOG_IS_IN_ARCHIVE (pageid))
	{
	  error = repl_pbfetch_from_archive (pageid,
					     (char *) &log_bufptr->logpage);
	  log_bufptr->in_archive = true;
	  REPL_CHECK_ERR_ERROR (REPL_FILE_SVR_TP, REPL_SERVER_IO_ERROR);
	}
      else
	{
	  error = repl_io_read (repl_Log.log_vdes, &log_bufptr->logpage,
				phy_pageid, pagesize);
	  log_bufptr->in_archive = false;
	  REPL_CHECK_ERR_ERROR (REPL_FILE_SVR_TP, REPL_SERVER_IO_ERROR);
	}
      if (log_bufptr->logpage.hdr.logical_pageid != pageid)
	{
	  SLEEP_USEC (0, 100 * 1000);
	}
      retry--;
    }

  if (retry <= 0)
    {
      error = REPL_SERVER_IO_ERROR;
    }

  if (error == NO_ERROR)
    {
      log_bufptr->pageid = pageid;
      log_bufptr->phy_pageid = phy_pageid;
    }

  return error;
}

/*
 * repl_svr_process_read_log_req() - Send the log page to the repl_agent
 *                                   by reading the physical log file
 *                                   intead of using the buffer
 *   return: error code
 *   agent_id :  the target agent id
 *   in_archive   :
 *   pageid: requested pageid
 *
 * Note:
 *      The last page of the master db is not full. But, in order to propagate
 *      the updated records to the slaves, we have to send the partial log
 *      page to the repl_agent. So, the SEND thread reads the active log file
 *      directly instead of fetching the page from the memory buffer.
 *
 *    called by SEND thread
 */
int
repl_svr_process_read_log_req (int agentid, PAGEID pageid, bool * in_archive,
			       SIMPLE_BUF ** data)
{
  SIMPLE_BUF *buf;
  int error = NO_ERROR;
  LOG_PAGE *logpg;
  int result = REPL_REQUEST_SUCCESS;
  REPL_PB *pb;
  int retry = 10;

  *data = NULL;
  *in_archive = false;

  buf = (SIMPLE_BUF *) repl_svr_get_send_data_buffer ();
  if (buf == NULL)
    {
      return REPL_SERVER_INTERNAL_ERROR;
    }
  *data = buf;

  pb = (REPL_PB *) repl_svr_find_conn_or_buf (agentid, true);

  /* check the buffer size */
  if (buf->length < REPL_SIMPLE_BUF_SIZE)
    {
      buf->data = (char *) realloc (buf->data, REPL_SIMPLE_BUF_SIZE);
      REPL_CHECK_ERR_NULL (REPL_FILE_SVR_TP, REPL_SERVER_MEMORY_ERROR,
			   buf->data);
      buf->result = (char *) (buf->data + repl_Log.pgsize);
      buf->length = REPL_SIMPLE_BUF_SIZE;
    }

  /* check if the target page is in archive */
  if (REPL_LOG_IS_IN_ARCHIVE (pageid))
    {
      error = repl_pbfetch_from_archive (pageid, buf->data);
      *in_archive = true;
      REPL_CHECK_ERR_ERROR (REPL_FILE_SVR_TP, REPL_SERVER_IO_ERROR);
    }
  else
    {
      /* read from the active log file */
      error = repl_io_read (repl_Log.log_vdes, buf->data,
			    repl_log_to_phypageid (pageid), repl_Log.pgsize);
      *in_archive = false;
      REPL_CHECK_ERR_ERROR (REPL_FILE_SVR_TP, REPL_SERVER_IO_ERROR);
    }
  logpg = (LOG_PAGE *) buf->data;

  /* the fetched page is not the target page ? */
  while (retry-- > 0 && error == NO_ERROR
	 && logpg->hdr.logical_pageid != pageid)
    {
      /* if the master generates the log archive,
       * re-fetch the log header, try again
       */
      error = repl_fetch_log_hdr ();
      REPL_CHECK_ERR_ERROR (REPL_FILE_SVR_TP, REPL_SERVER_IO_ERROR);

      /* Sometimes, the master db server doesn't flush the log header
       * promptly. So, we have to check the pageid of the  fetched page,
       * At this point, the fetched page is the first page of the log file,
       * So, if the target pageid is less than the fetch pageid, we have to
       * access the archive log.
       */
      if (REPL_LOG_IS_IN_ARCHIVE2 (pageid, logpg->hdr.logical_pageid))
	{
	  error = repl_pbfetch_from_archive (pageid, buf->data);
	  *in_archive = true;
	  REPL_CHECK_ERR_ERROR (REPL_FILE_SVR_TP, REPL_SERVER_IO_ERROR);
	}
      else
	{
	  error = repl_io_read (repl_Log.log_vdes, buf->data,
				repl_log_to_phypageid (pageid),
				repl_Log.pgsize);
	  *in_archive = false;
	  REPL_CHECK_ERR_ERROR (REPL_FILE_SVR_TP, REPL_SERVER_IO_ERROR);
	}
      logpg = (LOG_PAGE *) buf->data;
    }
  REPL_CHECK_ERR_ERROR (REPL_FILE_SVR_TP, REPL_SERVER_IO_ERROR);

  if (pb)
    {
      PTHREAD_MUTEX_LOCK (pb->mutex);
      pb->on_demand = true;
      PTHREAD_MUTEX_UNLOCK (pb->mutex);
    }

  if (retry <= 0)
    {
      error = REPL_SERVER_IO_ERROR;
    }

  return error;
}

/*
 * repl_svr_process_get_log_req() - Send the log page to the repl_agent
 *   return: error code
 *   agent_id :  the target agent id
 *   in_archive   :
 *   pageid: requested pageid
 *
 * Note:
 *   the main routine for process "REPL_MSG_GET_NEXT_LOG" request.
 *
 *   called by SEND thread
 */
int
repl_svr_process_get_log_req (int agentid, PAGEID pageid, bool * in_archive,
			      SIMPLE_BUF ** data)
{
  SIMPLE_BUF *buf;
  int error = NO_ERROR;
  int result = REPL_REQUEST_SUCCESS;
  PAGEID min_pageid;
  int target_index;
  REPL_PB *pb;
  int diff;

  *data = NULL;
  *in_archive = false;
  if ((buf = (SIMPLE_BUF *) repl_svr_get_send_data_buffer ()) == NULL)
    return REPL_SERVER_INTERNAL_ERROR;

  if ((pb = (REPL_PB *) repl_svr_find_conn_or_buf (agentid, true)) == NULL)
    return REPL_SERVER_INTERNAL_ERROR;

  /* check the buffer size */
  if (buf->length < REPL_SIMPLE_BUF_SIZE)
    {
      buf->data = (char *) realloc (buf->data, REPL_SIMPLE_BUF_SIZE);
      REPL_CHECK_ERR_NULL (REPL_FILE_SVR_TP, REPL_SERVER_MEMORY_ERROR,
			   buf->data);
      buf->result = (char *) (buf->data + repl_Log.pgsize);
      buf->length = REPL_SIMPLE_BUF_SIZE;
    }

  PTHREAD_MUTEX_LOCK (pb->mutex);
  if (pb->start_pageid == -1)
    {
      pb->start_pageid = pageid;
      PTHREAD_COND_BROADCAST (pb->write_cond);
    }
  if (pageid > repl_Log.log_hdr->append_lsa.pageid)
    {
      PTHREAD_MUTEX_UNLOCK (pb->mutex);
      return REPL_SERVER_INTERNAL_ERROR;
    }
  PTHREAD_MUTEX_UNLOCK (pb->mutex);

  PTHREAD_MUTEX_LOCK (pb->mutex);
  while (!pb->need_shutdown && !pb->on_demand && pb->head == pb->tail)
    {
      PTHREAD_COND_TIMEDWAIT (pb->read_cond, pb->mutex);
    }
  if (pb->need_shutdown || !pb->on_demand)
    {
      PTHREAD_MUTEX_UNLOCK (pb->mutex);
      return REPL_SERVER_INTERNAL_ERROR;
    }
  /* read the target page from the buffer */
  min_pageid = pb->log_buffer[pb->head]->pageid;
  if (min_pageid != 0 && min_pageid <= pageid && pageid < pb->max_pageid)
    {
      diff = pageid - min_pageid;
      target_index = (pb->head + diff) % REPL_LOG_BUFFER_SIZE;
      memcpy (buf->data, &(pb->log_buffer[target_index]->logpage),
	      repl_Log.pgsize);
      *in_archive = pb->log_buffer[target_index]->in_archive;
      pb->head = (pb->head + 1 + diff) % REPL_LOG_BUFFER_SIZE;
      PTHREAD_COND_BROADCAST (pb->write_cond);
    }
  else if (pageid >= pb->max_pageid)
    {
      pb->max_pageid = pageid;
      pb->head = pb->tail = 0;
      PTHREAD_MUTEX_UNLOCK (pb->mutex);
      REPL_ERR_RETURN (REPL_FILE_SVR_TP, REPL_SERVER_INTERNAL_ERROR);
    }
  else
    {
      PTHREAD_MUTEX_UNLOCK (pb->mutex);
      REPL_ERR_RETURN (REPL_FILE_SVR_TP, REPL_SERVER_INTERNAL_ERROR);
    }
  PTHREAD_MUTEX_UNLOCK (pb->mutex);

  *data = buf;

  return error;
}

/*
 * repl_svr_process_log_hdr_req() - Send the log header to the repl_agent
 *   return  : error code
 *
 *     agentid(in) : the target agent id
 *     req(in)     : request info.
 *     data(out)   : log header data
 *
 * Note:
 *    the main routine for process "REPL_MSG_GET_LOG_HEADER" request.
 *    . reads the log header from the active log
 *    . wake up the TR reader thread to
 *
 *     called by SEND thread
 */
int
repl_svr_process_log_hdr_req (int agentid, REPL_REQUEST * req,
			      SIMPLE_BUF ** data)
{
  int error = NO_ERROR;
  SIMPLE_BUF *buf;
  REPL_PB *pb = NULL;

  *data = NULL;
  if ((buf = (SIMPLE_BUF *) repl_svr_get_send_data_buffer ()) == NULL)
    {
      return REPL_SERVER_INTERNAL_ERROR;
    }
  *data = buf;

  /* check the buffer size */
  if (buf->length < REPL_SIMPLE_BUF_SIZE)
    {
      buf->data = (char *) realloc (buf->data, REPL_SIMPLE_BUF_SIZE);
      REPL_CHECK_ERR_NULL (REPL_FILE_SVR_TP, REPL_SERVER_MEMORY_ERROR,
			   buf->data);
      buf->result = (char *) (buf->data + repl_Log.pgsize);
      buf->length = REPL_SIMPLE_BUF_SIZE;
    }

  /* read the log header */
  PTHREAD_MUTEX_LOCK (file_Mutex);
  error = repl_fetch_log_hdr ();
  REPL_CHECK_ERR_ERROR (REPL_FILE_SVR_TP, REPL_SERVER_INTERNAL_ERROR);
  memcpy (buf->data, repl_Log.hdr_page, repl_Log.pgsize);
  PTHREAD_MUTEX_UNLOCK (file_Mutex);

  pb = (REPL_PB *) repl_svr_find_conn_or_buf (agentid, true);
  while (pb == NULL)
    {
      /* the READ thread has not created the page buffer yet */
      pb = (REPL_PB *) repl_svr_find_conn_or_buf (agentid, true);
    }

  PTHREAD_MUTEX_LOCK (pb->mutex);
  pb->start_pageid = -1;
  PTHREAD_COND_BROADCAST (pb->write_cond);
  PTHREAD_MUTEX_UNLOCK (pb->mutex);

  return error;
}

/*
 * repl_svr_process_agent_info_req() - generate the agent id and io pagesize
 *   return: error code
 *
 *   req(in)           : request info.
 *   agent_port_id(in) : repl_agent port id
 *   agentid(out)      : repl_agent id
 *
 * Note:
 *    To dedicate the repl_agent to a specific TR log reader and log buffer,
 *    we give the unique id to each repl_agent.
 *
 *    called by SEND thread
 */
int
repl_svr_process_agent_info_req (REPL_REQUEST * req, int agent_port_id,
				 int *agentid)
{
  int error = NO_ERROR;

  /* we use global mutex */
  PTHREAD_MUTEX_LOCK (file_Mutex);
  *agentid = agent_ID++;

  /* read the log header */
  error = repl_fetch_log_hdr ();
  REPL_CHECK_ERR_ERROR (REPL_FILE_SVR_TP, REPL_SERVER_INTERNAL_ERROR);
  PTHREAD_MUTEX_UNLOCK (file_Mutex);
  error = repl_svr_init_new_agent (*agentid, req->agent_fd, agent_port_id);

  if (error != NO_ERROR)
    {
      *agentid = -1;
    }

  return error;
}

/*
 * repl_svr_tr_send() - the start function of a sender thread
 *   return: void *
 *
 * Note:
 *    called by SEND thread
 */
static void *
repl_svr_tr_send ()
{
  REPL_TPOOL_WORK *my_workp;

  /* Start a loop */
  for (;;)
    {
      /* Check queue for work */

      /* lock .. in order to retrieve the queue info */
      PTHREAD_MUTEX_LOCK (repl_tpool->queue_lock);

      /* if the queue is empty and it's not a shutdown case, wait ! */
      while ((repl_tpool->cur_queue_size == 0) && (!repl_tpool->shutdown))
	{
	  PTHREAD_COND_TIMEDWAIT (repl_tpool->queue_not_empty,
				  repl_tpool->queue_lock);
	}

      /* Has a shutdown started while i was sleeping? */
      if (repl_tpool->shutdown)
	{
	  PTHREAD_MUTEX_UNLOCK (repl_tpool->queue_lock);
	  break;
	}

      /* Get to work, dequeue the next item */
      my_workp = repl_tpool->queue_head;
      repl_tpool->cur_queue_size--;
      if (repl_tpool->cur_queue_size == 0)
	{
	  repl_tpool->queue_head = repl_tpool->queue_tail = NULL;
	}
      else
	{
	  repl_tpool->queue_head = my_workp->next;
	}


      /* Handle waiting add_work threads */
      if ((!repl_tpool->do_not_block_when_full)
	  && (repl_tpool->cur_queue_size == (repl_tpool->max_queue_size - 1)))
	{
	  PTHREAD_COND_BROADCAST (repl_tpool->queue_not_full);
	}

      /* Handle waiting destroyer threads */
      if (repl_tpool->cur_queue_size == 0)
	{
	  PTHREAD_COND_BROADCAST (repl_tpool->queue_empty);
	}

      PTHREAD_MUTEX_UNLOCK (repl_tpool->queue_lock);

      /* Do this work item */
      (*(my_workp->routine)) (my_workp->arg);
      free_and_init (my_workp->arg);
      free_and_init (my_workp);
    }
  PTHREAD_EXIT;
  return NULL;
}

/*
 * repl_svr_tr_read() - Reads the transaction log file of master
 *   return: void *
 *
 * Note:
 *   repl_server has a thread that reads the transaction log file of
 *   the master server.
 *   It reads the log file from the start point of replication log.
 *
 *   repl_server has same number READ threads as the number of
 *   repl_agent connected to the repl_server.
 *   The reason we don't share the READ thread and page buffer is
 *   we can't guarantee  that the status of many repl_agent are same.
 *   That is, repl_agent 1 may be reading page 2001 while another
 *   agent is reading page 3765.
 *
 *    called by READ thread
 */
static void *
repl_svr_tr_read (void *arg)
{
  int error;
  REPL_PB *pb;
  REPL_CONN *conn_p;

  conn_p = (REPL_CONN *) arg;

  pb = conn_p->pb;

  while (true)
    {
      PTHREAD_MUTEX_LOCK (pb->mutex);
      while (!pb->need_shutdown && ((repl_Log.log_hdr == NULL) ||
				    /* log header is not fetched yet */
				    ((pb->tail + 1) % REPL_LOG_BUFFER_SIZE ==
				     pb->head) ||
				    /* buffer is full */
				    (pb->start_pageid < 1)
				    /* agent is not started */
	     ))
	{

	  /*
	   * wait a moment & re-check the header.
	   * the wake up condition would be raised by the sender thread
	   * when the sender re-fetch the log header.
	   */
	  PTHREAD_COND_TIMEDWAIT (pb->write_cond, pb->mutex);
	}

      if (pb->max_pageid == 0)
	{
	  pb->max_pageid = pb->start_pageid;
	}

      /* if shutdown is progress or
       * it already prefetched all the pages..
       */
      if (pb->need_shutdown == true ||
	  repl_Log.log_hdr->append_lsa.pageid < pb->max_pageid)
	{
	  PTHREAD_COND_BROADCAST (pb->read_cond);
	  PTHREAD_MUTEX_UNLOCK (pb->mutex);
	  break;
	}

      error = repl_log_pbfetch (pb, pb->tail, pb->max_pageid,
				repl_Log.pgsize);
      if (error != NO_ERROR)
	{
	  PTHREAD_MUTEX_UNLOCK (pb->mutex);
	  break;
	}

      pb->tail = (pb->tail + 1) % REPL_LOG_BUFFER_SIZE;

      pb->max_pageid++;

      PTHREAD_COND_BROADCAST (pb->read_cond);
      PTHREAD_MUTEX_UNLOCK (pb->mutex);
    }				/* end of main loop */

  pb->on_demand = true;
  PTHREAD_EXIT;
  return NULL;
}

/*
 * repl_dkey_delete() - delete a thread specific key
 *   return: none
 *
 * Note:
 *   by MAIN thread
 */
static void
repl_svr_dkey_delete (void *value)
{
  SIMPLE_BUF *data_buf = (SIMPLE_BUF *) value;

  free_and_init (data_buf->data);
  free_and_init (data_buf);
}

/*
 * repl_svr_check_shutdown() - Check if the status is
 *                             "shutdown is progress or not"
 *   return: bool
 *
 * Note:
 *    called by a MAIN thread
 */
bool
repl_svr_check_shutdown ()
{
  bool result;

  PTHREAD_MUTEX_LOCK (repl_tpool->queue_lock);
  result = repl_tpool->shutdown;
  PTHREAD_MUTEX_UNLOCK (repl_tpool->queue_lock);
  return result;
}

/*
 * repl_svr_shutdown_immediately() - send the signal to the main thread to
 *                                   shutdown the process
 *   return: none
 *
 * Note:
 *    We have to stop all the things immediately..
 *       (critical system call failure ...)
 *    But.. just exit is a good way? I have to find out other ways...
 *
 *    called by a main thread
 */
void
repl_svr_shutdown_immediately ()
{
  exit (-1);
}

/*
 * repl_svr_shutdown_by_signal() - shutdown the process
 *   return: none
 *
 * Note:
 *   When the process receives the shutdown signal, just set the need_shutdown
 *   as 1, then the working threads will check this value and will die..
 */
static void
repl_svr_shutdown_by_signal (int ignore)
{
  REPL_CONN *conn_p;

  repl_svr_tp_destroy (1);

  conn_p = repl_Conn_h;
  while (conn_p)
    {
      if (conn_p->pb)
	{
	  PTHREAD_MUTEX_LOCK (conn_p->pb->mutex);
	  conn_p->pb->need_shutdown = true;
	  close (conn_p->fd);
	  PTHREAD_COND_BROADCAST (conn_p->pb->write_cond);
	  PTHREAD_COND_BROADCAST (conn_p->pb->read_cond);
	  PTHREAD_MUTEX_UNLOCK (conn_p->pb->mutex);
	}
      conn_p = conn_p->next;
    }

  return;
}

/*
 * repl_svr_tp_init() - Initialize the thread pool
 *   return: NO_ERROR or REPL_SERVER_ERROR
 *
 * Note:
 *   . allocate memory for thread pool
 *   . initialize each field
 *   . initialize a mutex, condition variables
 *   . set signal mask and startup the signal handler thread
 *
 *  by main thread
 */
int
repl_svr_tp_init (int thread_num, int do_not_block_when_full)
{
  int error = NO_ERROR;

  /* signal processing */
  repl_signal_process (repl_svr_shutdown_by_signal);

  /* allocate a pool data structure */
  repl_tpool = (REPL_TPOOL) malloc (DB_SIZEOF (struct repl_tpool));
  REPL_CHECK_ERR_NULL (REPL_FILE_SVR_TP, REPL_SERVER_MEMORY_ERROR,
		       repl_tpool);

  /* initialize thread pool fields */
  repl_tpool->max_queue_size = thread_num;
  repl_tpool->do_not_block_when_full = do_not_block_when_full;
  repl_tpool->cur_queue_size = 0;
  repl_tpool->queue_head = NULL;
  repl_tpool->queue_tail = NULL;
  repl_tpool->queue_closed = 0;
  repl_tpool->shutdown = false;

  /* initialize the mutex and condition variables */
  PTHREAD_MUTEX_INIT (repl_tpool->queue_lock);
  PTHREAD_COND_INIT (repl_tpool->queue_not_empty);
  PTHREAD_COND_INIT (repl_tpool->queue_not_full);
  PTHREAD_COND_INIT (repl_tpool->queue_empty);

  PTHREAD_MUTEX_INIT (file_Mutex);
  PTHREAD_MUTEX_INIT (error_Mutex);

  /* allocate worker threads */
  repl_tr_send_thread =
    (pthread_t *) malloc (DB_SIZEOF (pthread_t) * thread_num);
  REPL_CHECK_ERR_NULL (REPL_FILE_SVR_TP, REPL_SERVER_MEMORY_ERROR,
		       repl_tr_send_thread);

  PTHREAD_KEY_CREATE (d_key, repl_svr_dkey_delete);

  return error;
}

/*
 * repl_svr_tp_start() - Start the SEND threads
 *   return: error code
 *
 * Note:
 *    We have to start the main job (log transfer)
 *    after the all other basic things  done. (getting the active log file
 *    path, log page size, fetching the log header, etc..)
 *    By calling this function, predefined number of SEND threads
 *    will be created and run.
 *
 *   by MAIN thread
 */
int
repl_svr_tp_start ()
{
  int i;
  int size_worker_thread;

  size_worker_thread = sizeof (repl_tr_send_thread) / sizeof (pthread_t);

  for (i = 0; i < size_worker_thread; i++)
    {
      PTHREAD_CREATE (repl_tr_send_thread[i], NULL, repl_svr_tr_send, NULL);
    }

  return NO_ERROR;
}

/*
 * repl_svr_tp_add_work() - add a request to the job queue
 *   return: error code
 *
 * Note:
 *      When the repl_agent send a request to the repl_server,
 *      the main thread of repl_server add the request to the job queue,
 *      then one of woker threads catch the request from the job queue and
 *      process it.
 *
 *   1. if queue is full, and the queue is set with "not block when full",
 *      just return only with logging to the "err file" --> ignore the request
 *   2. else if queue is full, and it is not shutdown case,
 *      wait for "queue not full"
 *   3. else if it's a shutdown case.. just return.. we don't have to process
 *      this request...
 *   4. Now.. We process the normal case..
 *      . allocate the work order entry
 *      . set the work order entry (process routine & argument)
 *      . insert this entry to the queue (if queue is empty status, broadcast
 *        "queue_not_empty" signal to the waiting worker thread.
 *
 *   by main thread
 */
int
repl_svr_tp_add_work (void (*routine) (void *), void *arg)
{
  REPL_TPOOL_WORK *workp = NULL;
  int error = NO_ERROR;

  PTHREAD_MUTEX_LOCK (repl_tpool->queue_lock);

  /* no space and this caller doesn't want to wait */
  if ((repl_tpool->cur_queue_size == repl_tpool->max_queue_size)
      && repl_tpool->do_not_block_when_full)
    {
      REPL_ERR_LOG (REPL_FILE_SVR_TP, REPL_SERVER_REQ_QUEUE_IS_FULL);
      PTHREAD_MUTEX_UNLOCK (repl_tpool->queue_lock);
      return NO_ERROR;
    }

  /* queue is full and.. have to wait.. */
  while ((repl_tpool->cur_queue_size == repl_tpool->max_queue_size)
	 && (!(repl_tpool->shutdown || repl_tpool->queue_closed)))
    {
      PTHREAD_COND_WAIT (repl_tpool->queue_not_full, repl_tpool->queue_lock);
    }

  /* the pool is in the process of being destroyed */
  if (repl_tpool->shutdown || repl_tpool->queue_closed)
    {
      PTHREAD_MUTEX_UNLOCK (repl_tpool->queue_lock);
      return NO_ERROR;
    }

  /* allocate work structure */
  workp = (REPL_TPOOL_WORK *) malloc (DB_SIZEOF (REPL_TPOOL_WORK));
  REPL_CHECK_ERR_NULL2 (REPL_FILE_SVR_TP, REPL_SERVER_MEMORY_ERROR,
			repl_tpool->queue_lock, workp);

  workp->routine = routine;
  workp->arg = arg;
  workp->next = NULL;

  if (repl_tpool->cur_queue_size == 0)
    {
      repl_tpool->queue_tail = repl_tpool->queue_head = workp;
      PTHREAD_COND_BROADCAST (repl_tpool->queue_not_empty);
    }
  else
    {
      repl_tpool->queue_tail->next = workp;
      repl_tpool->queue_tail = workp;
    }

  repl_tpool->cur_queue_size++;

  PTHREAD_MUTEX_UNLOCK (repl_tpool->queue_lock);

  return NO_ERROR;
}

/*
 * repl_svr_tp_destroy() - free the thread pool resource
 *   return: error code
 *   finish : If the finish flag is set, wait for workers to drain queue
 *
 * Note:
 *    called by main thread or repl_shutdown_thread
 */
int
repl_svr_tp_destroy (int finish)
{

  if (repl_tpool == NULL)
    return NO_ERROR;

  PTHREAD_MUTEX_LOCK (repl_tpool->queue_lock);

  /* Is a shutdown already in progress? */
  if (repl_tpool->queue_closed || repl_tpool->shutdown)
    {
      PTHREAD_MUTEX_UNLOCK (repl_tpool->queue_lock);
      return NO_ERROR;
    }

  repl_tpool->queue_closed = 1;

  /* If the finish flag is set, wait for workers to drain queue */
  if (finish == 1)
    {
      while (repl_tpool->cur_queue_size != 0)
	{
	  PTHREAD_COND_TIMEDWAIT (repl_tpool->queue_empty,
				  repl_tpool->queue_lock);
	}
    }

  repl_tpool->shutdown = true;

  /* Wake up any workers so they recheck shutdown flag */
  PTHREAD_COND_BROADCAST (repl_tpool->queue_not_empty);
  PTHREAD_COND_BROADCAST (repl_tpool->queue_not_full);

  PTHREAD_MUTEX_UNLOCK (repl_tpool->queue_lock);

  return NO_ERROR;
}

/*
 * repl_svr_thread_end() - free thread pool resources.
 *   return: none
 *
 * Note:
 *    called by main thread or repl_shutdown_thread
 */
void
repl_svr_thread_end ()
{
  REPL_TPOOL_WORK *cur_nodep;

  while (repl_tpool->queue_head != NULL)
    {
      cur_nodep = repl_tpool->queue_head->next;
      repl_tpool->queue_head = repl_tpool->queue_head->next;
      free_and_init (cur_nodep);
    }
  free_and_init (repl_tpool);

  PTHREAD_KEY_DELETE (d_key);

  /* Now free pool structures */
  free_and_init (repl_tr_send_thread);

  return;
}

static REPL_AGENT_INFO *
repl_get_agent_info (char *ip)
{
  REPL_AGENT_INFO *tmp;

  for (tmp = agent_List; tmp != NULL; tmp = tmp->next)
    {
      if (strcmp (tmp->ip, ip) == 0)
	return tmp;
    }

  return NULL;
}
