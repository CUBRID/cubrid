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
 *
 */

/*
 * repl_agent.c - main routine of Transaction Log Receiver
 *                at the distributor site
 *
 * Note:
 *   repl_agent has two roles, one is to receive the transaction logs from the
 *   repl_server and write down them to the copy log file. Anther thing is to
 *   apply the transaction logs to the slave db.
 *
 *   repl_agent has 4 kinds of threads.
 *     - a main thread : initialize and control the other threads
 *     - log receiver threads : requests the target logs to the repl_server and
 *                              receives, write to the log buffer.
 *                              There are as many as the # of masters to be
 *                              replicated.
 *     - log flush threads : flushes the log buffers to the disk.
 *                           Also, the # of flush threads are same as the
 *                           # of masters.. As a result of each log flush
 *                           thread job, there would be a copy log file.
 *     - log apply threads : applies the transaction logs to the slave.
 *                           Log apply thread is created for each slave db.
 *                           So, if we have 5 slave to be replicated, then
 *                           the # of log apply threads would be 5.
 *
 *  We have following global variables for communication between threads
 *
 *      - MASTER_INFO   *mInfo[]
 *      - SLAVE_INFO    *sInfo[]
 */

#ident "$Id$"

#include <unistd.h>
#include <stdlib.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <pthread.h>
#ifdef HAVE_GETOPT_H
#include <getopt.h>
#else
#include "getopt.h"
#endif

#include "porting.h"
#include "repl_agent.h"
#include "repl_support.h"
#include "repl_tp.h"
#include "object_primitive.h"
#include "db.h"
#include "schema_manager.h"
#include "transform_cl.h"
#include "locator_cl.h"
#include "page_buffer.h"
#include "file_manager.h"
#include "slotted_page.h"
#include "message_catalog.h"
#include "utility.h"
#include "object_accessor.h"
#include "parser.h"
#include "object_print.h"
#include "log_compress.h"
#include "environment_variable.h"

MASTER_INFO **mInfo = NULL;	/* master db info. */
SLAVE_INFO **sInfo = NULL;	/* slave db info. */

int repl_Master_num = 0;	/* the # of master dbs */
int repl_Slave_num = 0;		/* the # of master dbs */
int trail_File_vdes = 0;	/* the trail log file descriptor */

FILE *err_Log_fp;		/* error log file */
FILE *perf_Log_fp = NULL;	/* perf log file */
int perf_Log_size;		/* perf log file size */
FILE *history_fp = NULL;	/* user and trigger history file */
int agent_status_port;		/* agent status port id */
int perf_Commit_msec = 500;	/* commit interval of apply thread */
bool restart_Agent = false;
int agent_Max_size = 50;	/* 50M */
int debug_Dump_info = 0;

FILE *debug_Log_fd = NULL;

/* Global variables for argument parsing */
char *dist_LogPath = (char *) "";	/* the distributor log path */
char *dist_Dbname = NULL;	/* the distributor db name */
char *dist_Passwd = NULL;	/* the password to access dist db */
const char *env_Passwd = "REPL_PASS";
const char *env_First = "REPL_FIRST";
int create_Arv = false;
REPL_ERR *err_Head = NULL;
char *log_dump_fn = NULL;	/* file name of replication copy log */
int log_pagesize = 4096;	/* log page size for log dump */
int retry_Connect = false;	/* try to connect to repl_server forever */

extern int repl_Pipe_to_master;

#define REPLAGENT_ARG_DBNAME           1
#define REPLAGENT_ARG_PASSWD           2
#define REPLAGENT_ARG_ARV              3
#define REPLAGENT_ARG_LOG_DUMP         4
#define REPLAGENT_ARG_LOG_DUMP_PSIZE   5

/*
 * Following macros are re-defined from the log_impl.h to traverse the log file
 */

#define REPL_LOGAREA_SIZE(m_idx)                                             \
        (mInfo[m_idx]->io_pagesize - SSIZEOF(LOG_HDRPAGE))

/* macros to process the log record at the edge point of the page */
/* copied the macros of log_impl.h, and adjusted some logic */
#define REPL_LOG_READ_ALIGN(offset, pageid, log_pgptr, release, m_idx)        \
  do {                                                                        \
    (offset) = DB_ALIGN((offset), MAX_ALIGNMENT);                             \
    while ((offset) >= REPL_LOGAREA_SIZE(m_idx)) {                            \
      if (release == 1) repl_ag_release_page_buffer(pageid, m_idx);           \
      if (((log_pgptr) = repl_ag_get_page(++(pageid), m_idx)) == NULL) {      \
        REPL_ERR_LOG(REPL_FILE_AGENT, REPL_AGENT_IO_ERROR);                   \
      } else { release = 1; }                                                 \
      (offset) -= REPL_LOGAREA_SIZE(m_idx);                                   \
      (offset) = DB_ALIGN((offset), MAX_ALIGNMENT);                           \
    }                                                                         \
  } while(0)

#define REPL_LOG_READ_ADD_ALIGN(add, offset, pageid,                          \
            log_pgptr, release, m_idx)                                        \
  do {                                                                        \
    (offset) += (add);                                                        \
    REPL_LOG_READ_ALIGN((offset), (pageid), (log_pgptr), (release), m_idx);   \
  } while(0)

#define REPL_LOG_READ_ADVANCE_WHEN_DOESNT_FIT(length, offset, pageid,         \
              pg_, release, m_idx)                                            \
  do {                                                                        \
    if ((off_t)((offset)+(length)) >= REPL_LOGAREA_SIZE(m_idx)) {             \
      if ((release) == 1) repl_ag_release_page_buffer(pageid, m_idx);         \
      if (((pg_) = repl_ag_get_page(++(pageid), m_idx)) == NULL) {            \
        REPL_ERR_LOG(REPL_FILE_AGENT, REPL_AGENT_IO_ERROR);                   \
      } else { release = 1; }                                                 \
      (offset) = 0;                                                           \
    }                                                                         \
  } while(0)


/*
 * This process should have four arguments when it runs.
 *    database-name : the target database name to be replicated
 *    TCP PortNum   : the designated TCP Port number to catch the
 *                    reqests of the repl_agent
 *    err log file  : the log file to write down the message from this server.
 *    # of agents   : the maximum number of agents to be served
 */

/* followings are copied from the $CUBRID/src/storage/overflow.c
 * We need to define following structures at the common header file.
 */
struct ovf_recv_links
{
  VFID ovf_vfid;
  VPID new_vpid;
};

struct ovf_first_part
{
  VPID next_vpid;
  int length;
  char data[1];			/* Really more than one */
};
struct ovf_rest_parts
{
  VPID next_vpid;
  char data[1];			/* Really more than one */
};

/* use overflow page list to reduce memory copy overhead. */
struct ovf_page_list
{
  char *rec_type;		/* record type */
  char *data;			/* overflow page data: header + real data */
  int length;			/* total length of data */
  struct ovf_page_list *next;	/* next page */
};

/* debug info */
struct debug_string
{
  size_t length;
  size_t max_length;
  char *data;
};
static struct debug_string *debug_record_data = NULL;
static struct debug_string *debug_workspace_data = NULL;

/* Functions */
static int repl_ag_set_copy_log (int m_idx, PAGEID start_pageid,
				 PAGEID first_pageid, PAGEID last_pageid,
				 bool flushyn);
static int repl_ag_adjust_copy_log (int vdes, MASTER_INFO * minfo);
static LOG_PAGE *repl_ag_get_page (PAGEID pageid, int m_idx);
static void repl_log_copy_fromlog (char *rec_type, char *area, int length,
				   PAGEID log_pageid, PGLENGTH log_offset,
				   LOG_PAGE * log_pgptr, int m_idx);
static int repl_ag_get_log_data (struct log_rec *lrec,
				 LOG_LSA * lsa,
				 LOG_PAGE * pgptr,
				 int m_idx, unsigned int match_rcvindex,
				 unsigned int *rcvindex,
				 void **logs,
				 char **rec_type, char **data, int *d_length);
static int repl_ag_add_repl_item (REPL_APPLY * apply, LOG_LSA lsa);
static int repl_ag_apply_delete_log (REPL_ITEM * item);
static int repl_ag_update_query_execute (const char *sql);
static int repl_ag_apply_schema_log (REPL_ITEM * item);
static int repl_ag_get_overflow_recdes (struct log_rec *lrec, void *logs,
					char **area, int *length, int m_idx,
					unsigned int rcvindex);
static int repl_ag_get_relocation_recdes (struct log_rec *lrec,
					  LOG_PAGE * pgptr, int m_idx,
					  unsigned int match_rcvindex,
					  void **logs, char **rec_type,
					  char **data, int *d_length);
static int repl_ag_get_next_update_log (struct log_rec *prev_lrec,
					LOG_PAGE * pgptr, int m_idx,
					void **logs, char **rec_type,
					char **data, int *d_length);
static int repl_ag_get_recdes (LOG_LSA * lsa, int m_idx, LOG_PAGE * pgptr,
			       RECDES * recdes, unsigned int *rcvindex,
			       char *log_data, char *rec_type, bool * ovfyn);
static int repl_get_current (OR_BUF * buf, SM_CLASS * class_,
			     int bound_bit_flag, DB_OTMPL * def,
			     DB_VALUE * key);
static int repl_disk_to_obj (MOBJ classobj, RECDES * record, DB_OTMPL * def,
			     DB_VALUE * key);
static int repl_ag_apply_update_log (REPL_ITEM * item, int m_idx);
static int repl_ag_log_get_file_line (FILE * fp);
static REPL_CACHE_PB *repl_init_cache_pb (void);
static int repl_expand_cache_log_buffer (REPL_CACHE_PB * cache_pb,
					 int slb_cnt, int slb_size,
					 int def_buf_size);
static REPL_CACHE_LOG_BUFFER *repl_cache_buffer_replace (REPL_CACHE_PB *
							 cache_pb, int fd,
							 PAGEID phy_pageid,
							 int io_pagesize,
							 int def_buf_size);
static unsigned int repl_ag_pid_hash (const void *key_pid,
				      unsigned int htsize);
static int repl_ag_pid_hash_cmpeq (const void *key_pid1,
				   const void *key_pid2);
static int repl_ag_add_master_info (int idx);
static void repl_ag_clear_master_info (int dbid);
static void repl_ag_clear_master_info_all ();
static int repl_ag_add_slave_info (int idx);
static void repl_ag_clear_slave_info (SLAVE_INFO * sinfo);
static void repl_ag_clear_slave_info_all ();
static int repl_ag_open_env_file (char *repl_log_path);
static void repl_ag_shutdown ();
static int repl_ag_get_env (DB_QUERY_RESULT * result);
static int repl_ag_get_master (DB_QUERY_RESULT * result);
static int repl_ag_init_repl_lists (SLAVE_INFO * sinfo, int idx,
				    bool need_realloc);
static int repl_ag_get_repl_group (SLAVE_INFO * sinfo, int m_idx);
static char *repl_ag_get_class_from_repl_group (SLAVE_INFO * sinfo, int m_idx,
						char *class_name,
						LOG_LSA * lsa);
static int repl_ag_get_slave (DB_QUERY_RESULT * result);
static int repl_ag_get_parameters_internal (int (*func) (DB_QUERY_RESULT *),
					    const char *query);
static int repl_ag_get_parameters ();
static void usage_cubrid_repl_agent (const char *argv0);

static int repl_debug_add_value (struct debug_string **record_data,
				 DB_VALUE * value);
static bool repl_debug_check_data (char *str);
static int
repl_debug_object2string (DB_OBJECT * class_obj, DB_VALUE * key,
			  struct debug_string **record_data);
static void repl_restart_agent (char *agent_pathname, bool print_msg);

/*
 * repl_ag_set_copy_log() - Set the end log info
 *   return: NO_ERROR or REPL_IO_ERROR
 *    m_idx(in)        : the array index of the target master info
 *    start_pageid(in) : start page of the log copy
 *    first_pageid(in) : first page of the active log copy file
 *    last_pageid(in)  : final page of the log copy
 *    flushyn(in)      : true if we need flushing
 *
 * Note:
 *
 *       copy_log is used for marking the last received LSA of TR log.
 *       So, copy_log is maintained by RECV thread, and it is flushed
 *       at the end of the copylog file.
 *       We append copy_log at the end of file to minimize the
 *       "disk seek overhead".
 *
 *      call chain:  repl_ag_get_log_header() <- RECV
 *
 *      called by RECV thread
 *
 *      the caller should do "mutex lock"
 */
static int
repl_ag_set_copy_log (int m_idx, PAGEID start_pageid, PAGEID first_pageid,
		      PAGEID last_pageid, bool flushyn)
{
  MASTER_INFO *minfo = mInfo[m_idx];
  int error = NO_ERROR;

  minfo->copy_log.start_pageid = start_pageid;
  minfo->copy_log.first_pageid = first_pageid;
  minfo->copy_log.last_pageid = last_pageid;

  if (flushyn == true)
    error = repl_io_write_copy_log_info (minfo->pb->log_vdes,
					 &(minfo->copy_log), 0,
					 minfo->io_pagesize);
  REPL_CHECK_ERR_ERROR (REPL_FILE_AGENT, REPL_AGENT_IO_ERROR);

  return error;
}

/*
 * repl_ag_adjust_copy_log() - adjust the copy log info
 *   return: NO_ERROR or REPL_IO_ERROR
 *   vdes(in): the volume descriptor for the copy log
 *   minfo(int/out) : the target master info
 *
 * Note:
 *    In case of the repl_agent stops with any reason before flushing the
 *    copy log, restart of repl_agent would get wrong information which
 *    record it flushed finally.
 *
 *    So, the repl_aegnt reads the readl log page (first & last), when
 *    it meets the odd copy log info.
 */
static int
repl_ag_adjust_copy_log (int vdes, MASTER_INFO * minfo)
{
  off_t offset = 0;
  int error = NO_ERROR;
  LOG_PAGE *log_page;
  int archive_vol;
  char archive_path[FILE_PATH_LENGTH + 4];

  log_page = malloc (minfo->io_pagesize);
  REPL_CHECK_ERR_NULL (REPL_FILE_AGENT, REPL_AGENT_MEMORY_ERROR, log_page);

  /* find out the last point of the target copy log */
  offset = lseek (vdes, (off_t) 0, SEEK_END);

  /* If no data page, just return */
  if (offset <= SSIZEOF (COPY_LOG))
    {
      free_and_init (log_page);
      return REPL_AGENT_IO_ERROR;
    }

  offset -= SSIZEOF (COPY_LOG);

  /* find out the last page id */
  error = repl_io_read (vdes, (void *) log_page,
			offset / minfo->io_pagesize - 1, minfo->io_pagesize);
  REPL_CHECK_ERR_ERROR_WITH_FREE (REPL_FILE_AGENT, REPL_AGENT_MEMORY_ERROR,
				  log_page);

  minfo->copy_log.last_pageid = log_page->hdr.logical_pageid;

  /* find out the first page id */
  error = repl_io_read (vdes, (void *) log_page, 0, minfo->io_pagesize);
  REPL_CHECK_ERR_ERROR_WITH_FREE (REPL_FILE_AGENT, REPL_AGENT_MEMORY_ERROR,
				  log_page);

  minfo->copy_log.first_pageid = log_page->hdr.logical_pageid;

  /* find out the start page id */
  sprintf (archive_path, "%s.ar%d", minfo->copylog_path, 0);
  archive_vol = repl_io_open (archive_path, O_RDONLY, 0);

  if (archive_vol == NULL_VOLDES)
    {
      minfo->copy_log.start_pageid = minfo->copy_log.first_pageid;
    }
  else
    {
      error = repl_io_read (archive_vol, (void *) log_page, 0,
			    minfo->io_pagesize);
      if (error == NO_ERROR)
	{
	  minfo->copy_log.start_pageid = log_page->hdr.logical_pageid;
	}
      else			/* if the size of archive is 0 */
	{
	  minfo->copy_log.start_pageid = minfo->copy_log.first_pageid;
	}
      close (archive_vol);
    }

  /* flush the final page info */
  error = repl_io_write_copy_log_info (vdes, &(minfo->copy_log),
				       offset / minfo->io_pagesize,
				       minfo->io_pagesize);
  REPL_CHECK_ERR_ERROR_WITH_FREE (REPL_FILE_AGENT, REPL_AGENT_MEMORY_ERROR,
				  log_page);

  free_and_init (log_page);
  return NO_ERROR;
}

/*
 * repl_ag_get_log_header() - get the current log header
 *   return: NO_ERROR or REPL_IO_ERROR or REPL_AGENT_ERROR
 *   m_idx(in): the array index of the target master info
 *   first(in):
 *
 * Note:
 *    Before start the replication, send a request to the repl_server
 *    to fetch the log header
 *
 *    call chain: RECV
 *
 *    called by RECV thread
 *
 *    caller don't need to process mutex lock
 */
int
repl_ag_get_log_header (int m_idx, bool first)
{
  MASTER_INFO *minfo = mInfo[m_idx];
  REPL_PB *pb = minfo->pb;
  int error = NO_ERROR;

  /* send the request to the repl_server and get the result */
  error = repl_ag_sock_request_log_hdr (m_idx);
  REPL_CHECK_ERR_ERROR (REPL_FILE_AGENT, REPL_AGENT_SOCK_ERROR);

  /* copy the result to the memory buffer area */
  PTHREAD_MUTEX_LOCK (pb->mutex);
  memcpy (pb->log_hdr_buffer, minfo->conn.resp_buffer, minfo->io_pagesize);
  pb->log_hdr = (struct log_header *) (pb->log_hdr_buffer->area);
  minfo->io_pagesize = pb->log_hdr->db_iopagesize;

  /* If this is the first time, set the start point to copy */
  if (first == true)
    {
      PAGEID temp;

      temp = minfo->copy_log.start_pageid;

      /* flush the trail log - the last point we copied already */
      if ((error = repl_ag_set_copy_log (m_idx, temp, temp,
					 temp, true)) != NO_ERROR)
	{
	  PTHREAD_MUTEX_UNLOCK (pb->mutex);
	  return error;
	}
    }
  else
    {
#if 0
      error = repl_io_read (pb->log_vdes, (void *) &(minfo->copy_log), 1,
			    SSIZEOF (COPY_LOG), true, true);
      if (minfo->copy_log.first_pageid <= 0 ||
	  minfo->copy_log.start_pageid <= 0 ||
	  minfo->copy_log.last_pageid <= 0 ||
	  (minfo->copy_log.last_pageid - minfo->copy_log.first_pageid >
	   10000))
	{
	  repl_ag_adjust_copy_log (pb->log_vdes, minfo);
	}
#endif
      error = repl_ag_adjust_copy_log (pb->log_vdes, minfo);
      if (error != NO_ERROR)
	{
	  PAGEID temp;
	  temp = minfo->copy_log.start_pageid;
	  if ((error = repl_ag_set_copy_log (m_idx, temp, temp,
					     temp, true)) != NO_ERROR)
	    {
	      PTHREAD_MUTEX_UNLOCK (pb->mutex);
	      return error;
	    }
	}
    }

  PTHREAD_COND_BROADCAST (pb->read_cond);
  PTHREAD_MUTEX_UNLOCK (pb->mutex);

  return error;
}

/*
 * repl_ag_does_page_exist() -
 *   return:
 *
 *    pageid(in): the target page id
 *    m_idx(in): the array index of the target master info
 *
 * Note
 *
 */
bool
repl_ag_does_page_exist (PAGEID pageid, int m_idx)
{
  REPL_PB *pb;
  COPY_LOG *copy_log;
  REPL_LOG_BUFFER *repl_log_buffer;

  pb = mInfo[m_idx]->pb;
  copy_log = &mInfo[m_idx]->copy_log;

  if ((pb->min_pageid <= pageid && pageid <= pb->max_pageid)
      || (copy_log->first_pageid <= pageid
	  && pageid <= copy_log->last_pageid))
    {
      repl_log_buffer = repl_ag_get_page_buffer (pageid, m_idx);
      if (repl_log_buffer != NULL)
	{
	  if (repl_log_buffer->pageid == pageid)
	    {
	      repl_ag_release_page_buffer (pageid, m_idx);
	      return true;
	    }
	  repl_ag_release_page_buffer (pageid, m_idx);
	}
    }

  return false;
}

/*
 * repl_ag_is_in_archive() -
 *   return:
 *
 *    pageid(in): the target page id
 *    m_idx(in): the array index of the target master info
 *
 * Note
 *
 */
bool
repl_ag_is_in_archive (PAGEID pageid, int m_idx)
{
  REPL_LOG_BUFFER *repl_log_buffer;
  bool in_archive = false;

  repl_log_buffer = repl_ag_get_page_buffer (pageid, m_idx);
  if (repl_log_buffer != NULL)
    {
      if (repl_log_buffer->pageid == pageid)
	{
	  in_archive = repl_log_buffer->in_archive;
	}
      repl_ag_release_page_buffer (pageid, m_idx);
    }

  return in_archive;
}

/*
 * repl_ag_valid_page() -
 *   return:
 *
 *    log_page(in):
 *    m_idx(in): the array index of the target master info
 *
 * Note
 *
 */
bool
repl_ag_valid_page (LOG_PAGE * log_page, int m_idx)
{
  LOG_LSA lsa;
  REPL_LOG_BUFFER *prev_page;
  LOG_PAGE *pg_ptr;
  struct log_rec *lrec;
  int pageid;

  if (log_page->hdr.offset < 0)
    {
      lsa.pageid = log_page->hdr.logical_pageid;
      do
	{
	  lsa.pageid--;
	  prev_page = repl_ag_get_page_buffer (lsa.pageid, m_idx);
	  if (prev_page == NULL)
	    {
	      return false;
	    }
	  pg_ptr = &(prev_page->logpage);

	  lsa.offset = pg_ptr->hdr.offset;
	}
      while (lsa.offset < 0);

      pageid = lsa.pageid;
      while (lsa.pageid == pageid && !LSA_ISNULL (&lsa))
	{
	  lrec = (struct log_rec *) ((char *) pg_ptr->area + lsa.offset);
	  LSA_COPY (&lsa, &lrec->forw_lsa);	/* set the next record */
	}

      if (LSA_ISNULL (&lsa) || lsa.pageid == log_page->hdr.logical_pageid)
	{
	  return false;
	}
    }

  return true;
}

/*
 * repl_ag_get_page_buffer() - return the target page buffer
 *   return: pointer to the target page buffer
 *    pageid(in): the target page id
 *    m_idx(in): the array index of the target master info
 *
 * Note
 *    the APPLY thread get the target log page...
 *         if(the target page is in page buffer area)
 *            OK... got it !
 *         else (the target page is the last page from the master)
 *            read again (the RECV thread continuously fetches the last
 *                        page)
 *
 *    call chain :
 *      - repl_ag_get_page()
 *                <- APPLY
 *      - repl_ag_get_page()
 *                <- repl_ag_apply_update_log() <- repl_ag_apply_repl_log()
 *                <- APPLY
 *      - repl_ag_get_page()
 *                <- repl_ag_set_repl_log()
 *                <- APPLY
 *      -  APPLY
 *
 *    called by APPLY thread
 *    caller (APPLY thread) should do "mutex lock"
 */
REPL_LOG_BUFFER *
repl_ag_get_page_buffer (PAGEID pageid, int m_idx)
{
  int gap;
  REPL_PB *pb = mInfo[m_idx]->pb;
  REPL_LOG_BUFFER *buf = NULL;
  PAGEID phy_pageid;
  SLAVE_INFO *sinfo;
  MASTER_INFO *minfo;
  int error = NO_ERROR;
  REPL_CACHE_PB *cache_pb;
  REPL_CACHE_LOG_BUFFER *cache_buf = NULL;

  sinfo = repl_ag_get_slave_info (NULL);
  minfo = mInfo[m_idx];
  cache_pb = minfo->cache_pb;

  /* if the target page is in the buffer area */
  if (pageid >= pb->min_pageid && pageid <= pb->max_pageid)
    {
      /* the target page is in the page buffer area */
      gap = pageid - pb->log_buffer[0]->pageid;
      buf =
	pb->log_buffer[(gap + minfo->log_buffer_size) %
		       minfo->log_buffer_size];
      return buf;
    }

  /* if the target page is not in the buffer area, and the page id
   * is greater than the max pageid of buffer, then we have to
   * wait for RECV thread to fetch the next page
   */
  else if (pageid > pb->max_pageid)
    {
      pb->read_pageid = pageid;
      PTHREAD_COND_BROADCAST (pb->end_cond);
      PTHREAD_MUTEX_UNLOCK (pb->mutex);

      PTHREAD_MUTEX_LOCK (pb->mutex);
      while (pageid > pb->max_pageid)
	{
	  PTHREAD_COND_TIMEDWAIT (pb->read_cond, pb->mutex);
	  if (pb->need_shutdown)
	    return NULL;
	}
      if (pageid >= pb->min_pageid && pageid <= pb->max_pageid)
	{
	  gap = pageid - pb->log_buffer[0]->pageid;
	  buf =
	    pb->log_buffer[(gap + minfo->log_buffer_size) %
			   minfo->log_buffer_size];
	  return buf;
	}
    }

  PTHREAD_MUTEX_LOCK (cache_pb->mutex);
  /* find the target page in the cache log buffer pool */
  cache_buf = (REPL_CACHE_LOG_BUFFER *) mht_get (cache_pb->hash_table,
						 (void *) &pageid);
  if (cache_buf != NULL)
    {
      cache_buf->fix_count++;
    }
  else
    {
      phy_pageid = pageid - minfo->copy_log.first_pageid;
      cache_buf =
	repl_cache_buffer_replace (cache_pb, pb->log_vdes, phy_pageid,
				   minfo->io_pagesize,
				   minfo->cache_buffer_size);
      if (cache_buf == NULL
	  || cache_buf->log_buffer.logpage.hdr.logical_pageid != pageid)
	{
	  REPL_ERR_LOG (REPL_FILE_AGENT, REPL_AGENT_INTERNAL_ERROR);
	  PTHREAD_MUTEX_UNLOCK (cache_pb->mutex);
	  return NULL;
	}
      if (cache_buf->log_buffer.pageid != 0)
	{
	  (void) mht_rem (cache_pb->hash_table,
			  &cache_buf->log_buffer.pageid, NULL, NULL);
	}
      cache_buf->log_buffer.pageid = pageid;
      cache_buf->fix_count = 1;
      cache_buf->log_buffer.phy_pageid = phy_pageid;
      cache_buf->log_buffer.in_archive = false;
      if (mht_put (cache_pb->hash_table,
		   &cache_buf->log_buffer.pageid, cache_buf) == NULL)
	{
	  PTHREAD_MUTEX_UNLOCK (cache_pb->mutex);
	  return NULL;
	}
    }
  PTHREAD_MUTEX_UNLOCK (cache_pb->mutex);
  buf = &(cache_buf->log_buffer);

  return buf;
}

/*
 * repl_ag_release_page_buffer() - decrease the fix_count of the target buffer
 *   return: none
 *   pageid(in): the target page id
 *   m_idx(in): the array index of the target master info
 *
 * Note:
 *    if pageid exist hash table then it is cache buffer's pageid.
 *
 *    if ( pageid exist hash table )
 *         dicrement buffer's fix_count
 *    else ( pageid is not cach buffer's pageid )
 *         no op.
 *
 *   if cache buffer's fix_count < 0 then programing error.
 */
void
repl_ag_release_page_buffer (PAGEID pageid, int m_idx)
{
  REPL_CACHE_PB *cache_pb;
  REPL_PB *pb;
  MASTER_INFO *minfo;
  REPL_CACHE_LOG_BUFFER *cache_buf;
  int error;

  minfo = mInfo[m_idx];
  pb = minfo->pb;
  cache_pb = minfo->cache_pb;

  if (pageid >= pb->min_pageid)
    return;

  PTHREAD_MUTEX_LOCK (cache_pb->mutex);
  cache_buf = (REPL_CACHE_LOG_BUFFER *) mht_get (cache_pb->hash_table,
						 (void *) &pageid);
  /* if cache_buf == NULL then pageid is not cache buffer's pageid */
  if (cache_buf != NULL)
    {
      cache_buf->fix_count--;
      cache_buf->recently_freed = true;
      if (cache_buf->fix_count < 0)
	{
	  /* cache buffer's fix_count < 0 : programing error.. */
	  cache_buf->fix_count = 0;
	  REPL_ERR_LOG (REPL_FILE_AGENT, REPL_AGENT_INTERNAL_ERROR);
	}
    }
  PTHREAD_MUTEX_UNLOCK (cache_pb->mutex);
}

/*
 * repl_ag_get_page() - return the target page
 *   return: pointer to the target page
 *   pageid(in): the target page id
 *   m_idx(in): the array index of the target master info
 *
 * Note:
 *    call chain :
 *      - <- APPLY
 *      - <- repl_ag_apply_update_log() <- repl_ag_apply_repl_log()
 *                <- APPLY
 *      - <- repl_ag_set_repl_log()
 *                <- APPLY
 *    called by APPLY thread
 *    caller (APPLY thread) should do "mutex lock"
 */
static LOG_PAGE *
repl_ag_get_page (PAGEID pageid, int m_idx)
{
  REPL_LOG_BUFFER *buf = NULL;

  buf = repl_ag_get_page_buffer (pageid, m_idx);

  if (buf == NULL)
    return NULL;

  return &buf->logpage;
}

/*
 * repl_log_copy_frmlog() - copy a portion of the log
 *   return: none
 *   rec_type(out)
 *   area: Area where the portion of the log is copied.
 *               (Set as a side effect)
 *   length: the length to copy (type change PGLENGTH -> int)
 *   log_pageid: log page identifier of the log data to copy
 *               (May be set as a side effect)
 *   log_offset: log offset within the log page of the log data to copy
 *               (May be set as a side effect)
 *   log_pgptr: the buffer containing the log page
 *               (May be set as a side effect)
 *
 * Note:
 *   Copy "length" bytes of the log starting at log_pageid,
 *   log_offset onto the given area.
 *
 *   area is set as a side effect.
 *   log_pageid, log_offset, and log_pgptr are set as a side effect.
 */

static void
repl_log_copy_fromlog (char *rec_type, char *area, LOG_ZIP_SIZE_T length,
		       PAGEID log_pageid, PGLENGTH log_offset,
		       LOG_PAGE * log_pgptr, int m_idx)
{
  int rec_length = SSIZEOF (INT16);
  LOG_ZIP_SIZE_T copy_length;	/* Length to copy into area */
  LOG_ZIP_SIZE_T t_length;	/* target length  */
  int area_offset = 0;		/* The area offset */
  int error = NO_ERROR;
  LOG_PAGE *pg;
  int release_yn = 0;

  pg = log_pgptr;

  /* filter the record type */
  /* NOTES : in case of overflow page, we don't need to fetch the rectype */
  while (rec_type != NULL && rec_length > 0)
    {
      REPL_LOG_READ_ADVANCE_WHEN_DOESNT_FIT (0, log_offset, log_pageid,
					     pg, release_yn, m_idx);
      if (pg == NULL)
	{
	  return;
	}

      copy_length = ((log_offset + rec_length <= LOGAREA_SIZE)
		     ? rec_length : (LOGAREA_SIZE - log_offset));
      memcpy (rec_type + area_offset, (char *) pg->area + log_offset,
	      copy_length);
      rec_length -= copy_length;
      area_offset += copy_length;
      log_offset += copy_length;
      length = length - SSIZEOF (INT16);
    }

  area_offset = 0;
  t_length = length;

  /* The log data is not contiguous */
  while (t_length > 0)
    {
      REPL_LOG_READ_ADVANCE_WHEN_DOESNT_FIT (0, log_offset, log_pageid,
					     pg, release_yn, m_idx);
      if (pg == NULL)
	{
	  return;
	}

      copy_length = ((log_offset + t_length <= LOGAREA_SIZE)
		     ? t_length : (LOGAREA_SIZE - log_offset));
      memcpy (area + area_offset, (char *) pg->area + log_offset,
	      copy_length);
      t_length -= copy_length;
      area_offset += copy_length;
      log_offset += copy_length;
    }

  if (pg && release_yn == 1)
    {
      repl_ag_release_page_buffer (pg->hdr.logical_pageid, m_idx);
    }
}

/*
 * repl_ag_get_log_data() - get the data area of log record
 *   return: error code
 *           *rcvindex : recovery index to be returned
 *           **logs : the specialized log info
 *           **rec_type : the type of RECDES
 *           **data : the log data
 *           *d_length : the length of data
 *           *copyyn : true if we alloc area for the data
 *   lrec : target log record
 *   lsa : the LSA of the target log record
 *   pgptr : the start log page pointer
 *   m_idx : index of the master info array
 *   match_rcvindex
 *
 * Note: get the data area, and rcvindex, length of data for the
 *              given log record
 */
static int
repl_ag_get_log_data (struct log_rec *lrec,
		      LOG_LSA * lsa,
		      LOG_PAGE * pgptr,
		      int m_idx, unsigned int match_rcvindex,
		      unsigned int *rcvindex,
		      void **logs,
		      char **rec_type, char **data, int *d_length)
{
  LOG_PAGE *pg;
  PGLENGTH offset;
  int length;			/* type change PGLENGTH -> int */
  PAGEID pageid;
  int error = NO_ERROR;
  struct log_undoredo *undoredo;
  struct log_undo *undo;
  struct log_redo *redo;
  int release_yn = 0;

  bool is_undo_zip = false;
  bool is_zip = false;
  int rec_len = 0;
  int nLength = 0;
  int undo_length = 0;
  int redo_length = 0;
  int temp_length = 0;
  char *undo_data = NULL;

  PGLENGTH temp_offset;
  PAGEID temp_pageid;
  LOG_PAGE *temp_pg;
  SLAVE_INFO *slave_info_p;

  bool is_overflow = false;
  bool is_diff = false;

  pg = pgptr;
  slave_info_p = repl_ag_get_slave_info (NULL);
  if (slave_info_p == NULL)
    {
      return REPL_AGENT_INTERNAL_ERROR;
    }

  offset = SSIZEOF (struct log_rec) +lsa->offset;
  pageid = lsa->pageid;

  REPL_LOG_READ_ALIGN (offset, pageid, pg, release_yn, m_idx);
  REPL_CHECK_ERR_ERROR (REPL_FILE_AGENT, REPL_AGENT_INTERNAL_ERROR);

  switch (lrec->type)
    {
    case LOG_UNDOREDO_DATA:
    case LOG_DIFF_UNDOREDO_DATA:
      if (lrec->type == LOG_DIFF_UNDOREDO_DATA)
	{
	  is_diff = true;
	}
      else
	{
	  is_diff = false;
	}

      length = SSIZEOF (struct log_undoredo);
      REPL_LOG_READ_ADVANCE_WHEN_DOESNT_FIT (length, offset, pageid,
					     pg, release_yn, m_idx);

      if (error == NO_ERROR)
	{
	  undoredo = (struct log_undoredo *) ((char *) pg->area + offset);

	  undo_length = undoredo->ulength;	/* redo log length */
	  temp_length = undoredo->rlength;	/* for the replication, we just need
						 * the redo data */
	  length = GET_ZIP_LEN (undoredo->rlength);

	  if (match_rcvindex == 0
	      || undoredo->data.rcvindex == match_rcvindex)
	    {
	      if (rcvindex)
		{
		  *rcvindex = undoredo->data.rcvindex;
		}

	      if (logs)
		{
		  *logs = (void *) undoredo;
		}
	    }
	  else if (logs)
	    {
	      *logs = (void *) NULL;
	    }

	  REPL_LOG_READ_ADD_ALIGN (SSIZEOF (*undoredo), offset,
				   pageid, pg, release_yn, m_idx);
	  if (error == NO_ERROR)
	    {
	      if (is_diff)
		{		/* XOR Redo Data */
		  temp_pg = pg;
		  temp_pageid = pageid;
		  temp_offset = offset;

		  if (ZIP_CHECK (undo_length))
		    {		/* Undo data is Zip Check */
		      is_undo_zip = true;
		      undo_length = GET_ZIP_LEN (undo_length);
		    }

		  undo_data = (char *) malloc (undo_length);
		  if (undo_data == NULL)
		    {
		      REPL_ERR_LOG (REPL_FILE_AGENT, REPL_AGENT_MEMORY_ERROR);
		      return REPL_AGENT_MEMORY_ERROR;
		    }

		  /* get undo data for XOR process */
		  repl_log_copy_fromlog (NULL, undo_data, undo_length,
					 pageid, offset, pg, m_idx);

		  if (is_undo_zip && undo_length > 0)
		    {
		      if (!log_unzip (slave_info_p->undo_unzip_ptr,
				      undo_length, undo_data))
			{
			  if (release_yn == 1)
			    {
			      repl_ag_release_page_buffer (pg->hdr.
							   logical_pageid,
							   m_idx);
			    }
			  REPL_ERR_LOG (REPL_FILE_AGENT,
					REPL_AGENT_UNZIP_ERROR);
			  if (undo_data)
			    {
			      free_and_init (undo_data);
			    }
			  return REPL_AGENT_UNZIP_ERROR;
			}
		    }

		  REPL_LOG_READ_ADD_ALIGN (undo_length, temp_offset,
					   temp_pageid, temp_pg,
					   release_yn, m_idx);
		  pg = temp_pg;
		  pageid = temp_pageid;
		  offset = temp_offset;
		}
	      else
		{
		  REPL_LOG_READ_ADD_ALIGN (GET_ZIP_LEN (undo_length),
					   offset, pageid, pg,
					   release_yn, m_idx);
		}
	    }
	}
      break;

    case LOG_UNDO_DATA:
      length = SSIZEOF (struct log_undo);
      REPL_LOG_READ_ADVANCE_WHEN_DOESNT_FIT (length, offset,
					     pageid, pg, release_yn, m_idx);
      if (error == NO_ERROR)
	{
	  undo = (struct log_undo *) ((char *) pg->area + offset);
	  temp_length = undo->length;
	  length = (int) GET_ZIP_LEN (undo->length);

	  if (match_rcvindex == 0 || undo->data.rcvindex == match_rcvindex)
	    {
	      if (logs)
		{
		  *logs = (void *) undo;
		}

	      if (rcvindex)
		{
		  *rcvindex = undo->data.rcvindex;
		}
	    }
	  else if (logs)
	    {
	      *logs = (void *) NULL;
	    }
	  REPL_LOG_READ_ADD_ALIGN (SSIZEOF (*undo), offset, pageid,
				   pg, release_yn, m_idx);
	}
      break;

    case LOG_REDO_DATA:
      length = SSIZEOF (struct log_redo);
      REPL_LOG_READ_ADVANCE_WHEN_DOESNT_FIT (length, offset,
					     pageid, pg, release_yn, m_idx);
      if (error == NO_ERROR)
	{
	  redo = (struct log_redo *) ((char *) pg->area + offset);
	  temp_length = redo->length;
	  length = GET_ZIP_LEN (redo->length);

	  if (match_rcvindex == 0 || redo->data.rcvindex == match_rcvindex)
	    {
	      if (logs)
		{
		  *logs = (void *) redo;
		}
	      if (rcvindex)
		{
		  *rcvindex = redo->data.rcvindex;
		}
	    }
	  else if (logs)
	    {
	      *logs = (void *) NULL;
	    }
	  REPL_LOG_READ_ADD_ALIGN (SSIZEOF (*redo), offset, pageid,
				   pg, release_yn, m_idx);
	}
      break;

    default:
      if (logs)
	{
	  *logs = NULL;
	}
      if (release_yn == 1)
	{
	  repl_ag_release_page_buffer (pg->hdr.logical_pageid, m_idx);
	}

      return error;
    }

  REPL_CHECK_ERR_ERROR_WITH_FREE (REPL_FILE_AGENT, REPL_AGENT_INTERNAL_ERROR,
				  undo_data);

  if (ZIP_CHECK (temp_length))
    {
      is_zip = true;
      nLength = GET_ZIP_LEN (temp_length);
    }
  else
    {
      is_zip = false;
    }

  if (*data == NULL)
    {
      /* general cases, use the pre-allocated buffer */

      *data = malloc (length);
      is_overflow = true;

      if (*data == NULL)
	{
	  *d_length = 0;
	  if (release_yn == 1)
	    {
	      repl_ag_release_page_buffer (pg->hdr.logical_pageid, m_idx);
	    }

	  if (undo_data)
	    {
	      free_and_init (undo_data);
	    }

	  return REPL_AGENT_MEMORY_ERROR;
	}
    }

  if (is_zip)
    {
      /* Get Zip Data */
      repl_log_copy_fromlog (NULL, *data, nLength, pageid, offset, pg, m_idx);
    }
  else
    {
      /* Get Redo Data */
      repl_log_copy_fromlog (rec_type ? *rec_type : NULL, *data, length,
			     pageid, offset, pg, m_idx);
    }

  if (is_zip && nLength != 0)
    {
      if (!log_unzip (slave_info_p->redo_unzip_ptr, nLength, *data))
	{
	  if (release_yn == 1)
	    {
	      repl_ag_release_page_buffer (pg->hdr.logical_pageid, m_idx);
	    }
	  REPL_ERR_LOG (REPL_FILE_AGENT, REPL_AGENT_UNZIP_ERROR);
	  if (undo_data)
	    {
	      free_and_init (undo_data);
	    }
	  return REPL_AGENT_UNZIP_ERROR;
	}
    }

  if (is_zip)
    {
      if (is_diff)
	{
	  if (is_undo_zip)
	    {
	      undo_length = (slave_info_p->undo_unzip_ptr)->data_length;
	      redo_length = (slave_info_p->redo_unzip_ptr)->data_length;
	      (void) log_diff (undo_length,
			       (slave_info_p->undo_unzip_ptr)->log_data,
			       redo_length,
			       (slave_info_p->redo_unzip_ptr)->log_data);
	    }
	  else
	    {
	      redo_length = (slave_info_p->redo_unzip_ptr)->data_length;
	      (void) log_diff (undo_length, undo_data,
			       redo_length,
			       (slave_info_p->redo_unzip_ptr)->log_data);
	    }
	}
      else
	{
	  redo_length = (slave_info_p->redo_unzip_ptr)->data_length;
	}

      if (rec_type)
	{
	  rec_len = SSIZEOF (INT16);
	  length = redo_length - rec_len;
	}
      else
	{
	  length = redo_length;
	}

      if (is_overflow)
	{
	  free_and_init (*data);
	  *data = malloc (length);
	  if (*data == NULL)
	    {
	      *d_length = 0;
	      if (release_yn == 1)
		{
		  repl_ag_release_page_buffer (pg->hdr.logical_pageid, m_idx);
		}
	      if (undo_data)
		{
		  free_and_init (undo_data);
		}
	      return REPL_AGENT_MEMORY_ERROR;
	    }
	}

      if (rec_type)
	{
	  memcpy (*rec_type, (slave_info_p->redo_unzip_ptr)->log_data,
		  rec_len);
	  memcpy (*data,
		  (slave_info_p->redo_unzip_ptr)->log_data + rec_len, length);
	}
      else
	{
	  memcpy (*data, (slave_info_p->redo_unzip_ptr)->log_data,
		  redo_length);
	}
    }

  *d_length = length;

  if (release_yn == 1)
    {
      repl_ag_release_page_buffer (pg->hdr.logical_pageid, m_idx);
    }

  if (undo_data)
    {
      free_and_init (undo_data);
    }

  return error;
}

/*
 * repl_ag_get_master_info_index() - return the array index of master
 *   return: the array index
 *   dbid(in): unique ID of the master db
 *
 * Note:
 *     We load the master db info to an array - mInfo[]
 *     This function returns the index of the array matching with the dbid
 *
 *     call chain : <- repl_tr_log_apply
 *
 *     called by APPLY thread
 *
 *     The caller don't need to do "mutex lock", because the mInfo is
 *     populated once by the main thread before creating threads.
 */
int
repl_ag_get_master_info_index (int dbid)
{
  int i;

  for (i = 0; i < repl_Master_num; i++)
    if (mInfo[i]->dbid == dbid)
      return i;
  return -1;

}

/*
 * repl_ag_is_idle() -
 *
 *   return:
 *
 *   sinfo(in): the pointer to the target slave info
 *   idx(in): the index of MASTER_MAP of a slage
 *
 * Note:
 */
bool
repl_ag_is_idle (SLAVE_INFO * sinfo, int idx)
{
  int i;

  for (i = 0; i < sinfo->masters[idx].cur_repl; i++)
    {
      if (sinfo->masters[idx].repl_lists[i]->tranid != 0
	  && sinfo->masters[idx].repl_lists[i]->repl_tail != NULL)
	{
	  return false;
	}
    }

  return true;
}

/*
 * repl_ag_find_apply_list() - return the apply list for the target
 *                             transaction id
 *   return: pointer to the target apply list
 *   sinfo(in/out): the pointer to the target slave info
 *   tranid(in): the target transaction id
 *   idx(in): the index of MASTER_MAP of a slage
 *
 * Note:
 *     When we apply the transaction logs to the slave, we have to take them
 *     in turns of commit order.
 *     So, each slave maintains the apply list per transaction.
 *     And an apply list has one or more replication item.
 *     When the APPLY thread meets the "LOG COMMIT" record, it finds out
 *     the apply list of the target transaction, and apply the replication
 *     items to the slave orderly.
 *
 *     call chain :
 *          <- repl_ag_set_repl_log <- APPLY
 *              ==> to insert a replication item to the apply list
 *          <- repl_ag_apply_repl_log <- APPLY
 *              ==> to apply replication items to the apply list
 *
 *     called by APPLY thread
 *
 *     The caller don't need to do "mutex lock", because the mInfo is
 *     populated once by the main thread before creating threads.
 */
REPL_APPLY *
repl_ag_find_apply_list (SLAVE_INFO * sinfo, int tranid, int idx)
{
  int i;
  int free_index = -1;

  /* find out the matched index */
  for (i = 0; i < sinfo->masters[idx].cur_repl; i++)
    {
      if (sinfo->masters[idx].repl_lists[i]->tranid == tranid)
	return sinfo->masters[idx].repl_lists[i];

      /* retreive the free index  for the laster use */
      else if ((free_index < 0) &&
	       (sinfo->masters[idx].repl_lists[i]->tranid == 0))
	free_index = i;
    }

  /* not matched, but we have free space */
  if (free_index >= 0)
    {
      sinfo->masters[idx].repl_lists[free_index]->tranid = tranid;
#if 0
      if (lsa)
	LSA_COPY (&sinfo->masters[idx].repl_lists[free_index]->
		  start_lsa, lsa);
#endif
      return sinfo->masters[idx].repl_lists[free_index];
    }

  /* not matched, no free space */
  if (sinfo->masters[idx].cur_repl == sinfo->masters[idx].repl_cnt)
    {
      /* array is full --> realloc */
      if (repl_ag_init_repl_lists (sinfo, idx, true) == NO_ERROR)
	{
	  sinfo->masters[idx].repl_lists[sinfo->masters[idx].cur_repl]->
	    tranid = tranid;
#if 0
	  if (lsa)
	    LSA_COPY (&sinfo->masters[idx].
		      repl_lists[sinfo->masters[idx].cur_repl -
				 1]->start_lsa, lsa);
#endif
	  sinfo->masters[idx].cur_repl++;
	  return sinfo->masters[idx].repl_lists[sinfo->masters[idx].
						cur_repl - 1];
	}
      return NULL;
    }

  /* mot matched, no free space, array is not full */
  sinfo->masters[idx].repl_lists[sinfo->masters[idx].cur_repl]->tranid =
    tranid;
  sinfo->masters[idx].cur_repl++;
#if 0
  if (lsa)
    LSA_COPY (&sinfo->masters[idx].
	      repl_lists[sinfo->masters[idx].cur_repl - 1]->start_lsa, lsa);
#endif
  return sinfo->masters[idx].repl_lists[sinfo->masters[idx].cur_repl - 1];
}

/*
 * repl_ag_add_repl_item() - add the replication item into the apply list
 *   return: NO_ERROR or REPL_IO_ERROR or REPL_SOCK_ERROR
 *   apply(in/out): log apply list
 *   lsa(in): the target LSA of the log
 *
 * Note:
 *      call chain:  repl_ag_set_repl_log() <- repl_tr_log_apply
 *
 *      called by APPLY thread
 *
 *      The caller don't need to do "mutex lock", the "log apply lists &
 *      items" are local resources of the APPLY thread.
 */
static int
repl_ag_add_repl_item (REPL_APPLY * apply, LOG_LSA lsa)
{
  REPL_ITEM *item;
  int error = NO_ERROR;

  item = malloc (sizeof (REPL_ITEM));
  REPL_CHECK_ERR_NULL (REPL_FILE_AGENT, REPL_AGENT_MEMORY_ERROR, item);

  LSA_COPY (&item->lsa, &lsa);
  item->next = NULL;

  if (apply->repl_head == NULL)
    {
      apply->repl_head = apply->repl_tail = item;
    }
  else
    {
      apply->repl_tail->next = item;
      apply->repl_tail = item;
    }
  return error;
}

/*
 * repl_ag_apply_delete_log() - apply the delete log to the target slave
 *   return: NO_ERROR or error code
 *   item(in): replication item
 *
 * Note:
 *      call chain:
 *          repl_ag_apply_repl_log() <- repl_tr_log_apply() <- RECV
 *
 *      called by APPLY thread
 */
static int
repl_ag_apply_delete_log (REPL_ITEM * item)
{
  DB_OBJECT *class_obj, *obj;
  int error = NO_ERROR, au_save;

  /* find out class object by class name */
  class_obj = db_find_class (item->class_name);
  REPL_CHECK_ERR_NULL (REPL_FILE_AGENT, REPL_AGENT_INTERNAL_ERROR, class_obj);

  /* find out object by primary key */
  obj = obj_repl_find_object_by_pkey (class_obj, &item->key, AU_FETCH_UPDATE);

  AU_SAVE_AND_DISABLE (au_save);
  /* delete this object */
  if (obj)
    {
      error = db_drop (obj);
      if (error == ER_NET_CANT_CONNECT_SERVER || error == ER_OBJ_NO_CONNECT)
	{
	  AU_RESTORE (au_save);

	  return REPL_AGENT_CANT_CONNECT_TO_SLAVE;
	}
      REPL_CHECK_ERR_ERROR (REPL_FILE_AGENT, REPL_AGENT_INTERNAL_ERROR);
    }
  AU_RESTORE (au_save);


  return error;
}

/*
 * repl_ag_update_query_execute()
 *   return: NO_ERROR or error code
 *   sql(in)
 */
static int
repl_ag_update_query_execute (const char *sql)
{
  int error = NO_ERROR;
  DB_QUERY_RESULT *result;
  DB_QUERY_ERROR query_error;

  if (db_execute (sql, &result, &query_error) < 0)
    {
      REPL_ERR_LOG (REPL_FILE_AGENT, REPL_AGENT_INTERNAL_ERROR);
      return er_errid ();
    }
  error = db_query_end (result);
  if (error != NO_ERROR)
    REPL_ERR_LOG (REPL_FILE_AGENT, REPL_AGENT_INTERNAL_ERROR);

  return error;
}

/*
 * repl_ag_apply_schema_log() - apply the schema log to the target slave
 *   return: NO_ERROR or error code
 *   item(in): replication item
 *
 * Note:
 *      call chain:
 *          repl_ag_apply_repl_log() <- repl_tr_log_apply() <- RECV
 *
 *      called by APPLY thread
 */
static int
repl_ag_apply_schema_log (REPL_ITEM * item)
{
  char *ddl;
  int error = NO_ERROR;

  switch (item->item_type)
    {
    case CUBRID_STMT_CREATE_CLASS:
    case CUBRID_STMT_ALTER_CLASS:
    case CUBRID_STMT_RENAME_CLASS:
    case CUBRID_STMT_DROP_CLASS:

    case CUBRID_STMT_CREATE_INDEX:
    case CUBRID_STMT_ALTER_INDEX:
    case CUBRID_STMT_DROP_INDEX:

#if 0
      /* serial replication is not schema replication but data replication */
    case CUBRID_STMT_CREATE_SERIAL:
    case CUBRID_STMT_ALTER_SERIAL:
    case CUBRID_STMT_DROP_SERIAL:
#endif

    case CUBRID_STMT_DROP_DATABASE:
    case CUBRID_STMT_DROP_LABEL:

    case CUBRID_STMT_CREATE_STORED_PROCEDURE:
    case CUBRID_STMT_DROP_STORED_PROCEDURE:

      ddl = db_get_string (&item->key);
      if (repl_ag_update_query_execute (ddl) != NO_ERROR)
	{
	  if (er_errid () == ER_NET_CANT_CONNECT_SERVER
	      || error == ER_OBJ_NO_CONNECT)
	    {
	      error = REPL_AGENT_CANT_CONNECT_TO_SLAVE;
	    }
	  else
	    {
	      error = REPL_AGENT_QUERY_ERROR;
	    }
	}
      break;

    case CUBRID_STMT_CREATE_USER:
    case CUBRID_STMT_ALTER_USER:
    case CUBRID_STMT_DROP_USER:
    case CUBRID_STMT_GRANT:
    case CUBRID_STMT_REVOKE:

    case CUBRID_STMT_CREATE_TRIGGER:
    case CUBRID_STMT_RENAME_TRIGGER:
    case CUBRID_STMT_DROP_TRIGGER:
    case CUBRID_STMT_REMOVE_TRIGGER:
    case CUBRID_STMT_SET_TRIGGER:
      ddl = db_get_string (&item->key);
      if (fprintf (history_fp, "%s\n", ddl) < 0)
	REPL_ERR_LOG (REPL_FILE_AGENT, REPL_AGENT_INTERNAL_ERROR);
      else
	fflush (history_fp);
      break;

    default:
      return NO_ERROR;
    }

  return error;
}

/*
 * repl_ag_get_overflow_update_recdes() - prepare the overflow page update
 *   return: NO_ERROR or error code
 *
 * Note:
 *     For the overflow page udpate, the layout of transaction log is ..
 *
 *  1   |LOG_REDO_DATA:RVOVF_PAGE_UPDATE:vfid(2240/0/0):data(2241/0, 8994) |
 *  2   |LOG_REDO_DATA:RVOVF_PAGE_UPDATE:vfid(2241/0/0):data(-1/-1) |
 *  3   |LOG_UNDOREDO_DATA:RVOVF_NEWPAGE_LINK:vfid(2241/0/0):data(2242/0) |
 *  4   |LOG_UNDO_DATA:RVOVF_NEWPAGE_LOGICAL_UNDO:vfid(-1/-1/0):data(2242/0) |
 *  5   |LOG_REDO_DATA:RVOVF_PAGE_UPDATE:vfid(2242/0/0):data(-1/-1) |
 *  6|->|LOG_UNDOREDO_DATA:RVOVF_CHANGE_LINK:2242/0/0
 *   |       ..... (some other logs)
 *   |
 *  7---|LOG_REPLICATION_DATA:class_name, key_value, lsa |
 *
 *
 *      we are here .. log #6 -- RVOVF_CHANGE_LINK
 *
 *      The first overflow page structure is not same as the rest pages,
 *      without knowing about that, we can't retrieve the real data..
 *
 *      So, we travese the log file backward to find out the first page,
 *      then we make the log record via forward traversing.
 *
 *      vfid = vfid of RVOVF_CHANGE_LINK (2242)
 *      prev record = NULL;
 *      get previous record whithin the same transaction boundary;
 *      while(record) {
 *         if(record is LOG_REDO_DATA and
 *            rcvindex is RVOVF_PAGE_UPDATE and
 *            next vfid of this record == vfid) {
 *            vfid = current vfid
 *            prev_record = record
 *         }
 *         record = get previous record whithin the same transaction boundary;
 *      }
 *
 *      record = prev_record  -- first page
 *      first = 1
 *      while(record) {
 *         if(record is LOG_REDO_DATA and
 *            rcvindex is RVOVF_PAGE_UPDATE) {
 *            if(first) get first page data;
 *            else      get rest page data;
 *            add data;
 *         }
 *         get next record;
 *      }
 *
 *     For the overflow page insert, the layout of transaction log is ..
 *
 *  1   |LOG_UNDO_DATA:RVOVF_NEWPAGE_LOGICAL_UNDO:vfid(454/0):new_vpid(973/0)|
 *  2      |LOG_REDO_DATA:RVOVF_NEWPAGE_INSERT:P/O/V(973/0/0):length(4088):data|
 *  3   |LOG_UNDO_DATA:RVOVF_NEWPAGE_LOGICAL_UNDO:vfid(454/0):new_vpid(974/0)|
 *  4      |LOG_REDO_DATA:RVOVF_NEWPAGE_INSERT:P/O/V(974/0/0):length(4088):data|
 *  5   |LOG_UNDO_DATA:RVOVF_NEWPAGE_LOGICAL_UNDO:vfid(454/0):new_vpid(975/0)|
 *  6      |LOG_REDO_DATA:RVOVF_NEWPAGE_INSERT:P/O/V(975/0/0):length(1880):data|
 *  7|->|LOG_UNDOREDO_DATA:RVHF_INSERT:.... : REC_BIGONE, 973/-1/0
 *   |       ..... (some other logs)
 *   |
 *  8---|LOG_REPLICATION_DATA:class_name, key_value, lsa |
 *
 *
 *      we are here .. log #7 -- RVHF_INSERT
 *
 *    The logic is same as update cases except for the target rcvindex
 *                - RVOVF_NEWPAGE_INSERT
 *
 *
 *    call chain : APPLY thread
 *
 *    called by apply thread
 *
 *     APPLY thread processes the mutex lock
 */
static int
repl_ag_get_overflow_recdes (struct log_rec *log_record, void *logs,
			     char **area, int *length, int m_idx,
			     unsigned int rcvindex)
{
  LOG_LSA current_lsa;
  LOG_PAGE *current_log_page;
  struct log_rec *current_log_record;
  struct ovf_page_list *ovf_list_head = NULL;
  struct ovf_page_list *ovf_list_tail = NULL;
  struct ovf_page_list *ovf_list_data = NULL;
  struct log_redo *redo_log;
  VPID *temp_vpid;
  VPID prev_vpid;
  bool first = true;
  bool error_status = true;
  bool is_end_of_record = false;
  int copyed_len;
  int area_len;
  int area_offset;
  int error = NO_ERROR;

  if (logs == NULL)
    {
      return REPL_AGENT_INTERNAL_ERROR;
    }

  LSA_COPY (&current_lsa, &log_record->prev_tranlsa);
  prev_vpid.pageid = ((struct log_undoredo *) logs)->data.pageid;
  prev_vpid.volid = ((struct log_undoredo *) logs)->data.volid;

  while (!is_end_of_record && !LSA_ISNULL (&current_lsa))
    {
      current_log_page = repl_ag_get_page (current_lsa.pageid, m_idx);
      if (current_log_page == NULL)
	{
	  while (ovf_list_head)
	    {
	      ovf_list_data = ovf_list_head;
	      ovf_list_head = ovf_list_head->next;
	      free_and_init (ovf_list_data->data);
	      free_and_init (ovf_list_data);
	    }

	  return REPL_AGENT_INTERNAL_ERROR;
	}
      current_log_record =
	(struct log_rec *) ((char *) current_log_page->area +
			    current_lsa.offset);

      if (current_log_record->trid != log_record->trid)
	{
	  repl_ag_release_page_buffer (current_lsa.pageid, m_idx);
	  break;
	}

      /* process only LOG_REDO_DATA */
      if (current_log_record->type == LOG_REDO_DATA)
	{
	  ovf_list_data =
	    (struct ovf_page_list *) malloc (sizeof (struct ovf_page_list));
	  if (ovf_list_data == NULL)
	    {
	      /* malloc failed */
	      repl_ag_release_page_buffer (current_lsa.pageid, m_idx);

	      while (ovf_list_head)
		{
		  ovf_list_data = ovf_list_head;
		  ovf_list_head = ovf_list_head->next;
		  free_and_init (ovf_list_data->data);
		  free_and_init (ovf_list_data);
		}

	      return REPL_AGENT_MEMORY_ERROR;
	    }
	  memset (ovf_list_data, 0, DB_SIZEOF (struct ovf_page_list));
	  error =
	    repl_ag_get_log_data (current_log_record, &current_lsa,
				  current_log_page, m_idx, rcvindex, NULL,
				  (void **) (&redo_log), NULL,
				  &ovf_list_data->data,
				  &ovf_list_data->length);

	  if (error == NO_ERROR && redo_log && ovf_list_data->data)
	    {
	      temp_vpid = (VPID *) ovf_list_data->data;
	      if (error_status == true
		  || (temp_vpid->pageid == prev_vpid.pageid
		      && temp_vpid->volid == prev_vpid.volid))
		{
		  /* add to linked-list */
		  if (ovf_list_head == NULL)
		    {
		      ovf_list_head = ovf_list_tail = ovf_list_data;
		    }
		  else
		    {
		      ovf_list_data->next = ovf_list_head;
		      ovf_list_head = ovf_list_data;
		    }

		  /* error check */
		  if (temp_vpid->pageid == prev_vpid.pageid
		      && temp_vpid->volid == prev_vpid.volid)
		    {
		      error_status = false;
		    }
		  prev_vpid.pageid = redo_log->data.pageid;
		  prev_vpid.volid = redo_log->data.volid;
		  *length += ovf_list_data->length;
		}
	      else
		{
		  if (error_status == false
		      && (temp_vpid->pageid != prev_vpid.pageid
			  || temp_vpid->volid != prev_vpid.volid))
		    {
		      is_end_of_record = true;
		    }
		  free_and_init (ovf_list_data->data);
		  free_and_init (ovf_list_data);
		}
	    }
	  else
	    {
	      if (ovf_list_data->data)
		{
		  free_and_init (ovf_list_data->data);
		}
	      free_and_init (ovf_list_data);
	    }
	}
      repl_ag_release_page_buffer (current_lsa.pageid, m_idx);
      LSA_COPY (&current_lsa, &current_log_record->prev_tranlsa);
    }

  *area = malloc (*length);
  if (*area == NULL)
    {
      /* malloc failed: clear linked-list */
      while (ovf_list_head)
	{
	  ovf_list_data = ovf_list_head;
	  ovf_list_head = ovf_list_head->next;
	  free_and_init (ovf_list_data->data);
	  free_and_init (ovf_list_data);
	}
      return REPL_AGENT_MEMORY_ERROR;
    }

  /* make record description */
  copyed_len = 0;
  while (ovf_list_head)
    {
      ovf_list_data = ovf_list_head;
      ovf_list_head = ovf_list_head->next;

      if (first)
	{
	  area_offset = offsetof (struct ovf_first_part, data);
	  first = false;
	}
      else
	{
	  area_offset = offsetof (struct ovf_rest_parts, data);
	}
      area_len = ovf_list_data->length - area_offset;
      memcpy (*area + copyed_len, ovf_list_data->data + area_offset,
	      area_len);
      copyed_len += area_len;

      free_and_init (ovf_list_data->data);
      free_and_init (ovf_list_data);
    }

  return error;
}

static int
repl_ag_get_relocation_recdes (struct log_rec *lrec,
			       LOG_PAGE * pgptr,
			       int m_idx, unsigned int match_rcvindex,
			       void **logs,
			       char **rec_type, char **data, int *d_length)
{
  struct log_rec *tmp_lrec;
  unsigned int rcvindex;
  LOG_PAGE *pg = pgptr;
  int release_yn = 0;
  LOG_LSA lsa;
  int error = NO_ERROR;

  LSA_COPY (&lsa, &lrec->prev_tranlsa);
  if (!LSA_ISNULL (&lsa))
    {
      pg = repl_ag_get_page (lsa.pageid, m_idx);
      if (pg == NULL)
	{
	  return REPL_AGENT_INTERNAL_ERROR;
	}

      if (pg != pgptr)
	{
	  release_yn = 1;
	}

      tmp_lrec = (struct log_rec *) ((char *) pg->area + lsa.offset);
      if (tmp_lrec->trid != lrec->trid)
	{
	  error = REPL_AGENT_GET_LOG_PAGE_FAIL;
	}
      else
	{
	  error = repl_ag_get_log_data (tmp_lrec, &lsa, pg, m_idx,
					RVHF_INSERT, &rcvindex, logs,
					rec_type, data, d_length);
	}
    }
  else
    {
      error = REPL_AGENT_GET_LOG_PAGE_FAIL;
    }

  if (release_yn == 1)
    {
      repl_ag_release_page_buffer (lsa.pageid, m_idx);
    }

  return error;
}

/*
 * repl_ag_get_next_update_log() - get the right update log
 *   return: NO_ERROR or error code
 *   prev_lrec(in):  prev log record
 *   pgptr(in):  the start log page pointer
 *   m_idx(in):  index of master info
 *   logs(out) : the specialized log info
 *   rec_type(out) : the type of RECDES
 *   data(out) : the log data
 *   d_length(out): the length of data
 *
 * Note:
 *      When the repl_agent meets the REC_ASSIGN_ADDRESS or REC_RELOCATION
 *      record, it should fetch the real UPDATE log record to be processed.
 */
static int
repl_ag_get_next_update_log (struct log_rec *prev_lrec,
			     LOG_PAGE * pgptr, int m_idx, void **logs,
			     char **rec_type, char **data, int *d_length)
{
  LOG_PAGE *pg;
  LOG_LSA lsa;
  PGLENGTH offset;
  int length;			/* type change PGLENGTH -> int */
  PAGEID pageid;
  int error = NO_ERROR;
  struct log_rec *lrec;
  struct log_undoredo *undoredo;
  struct log_undoredo *prev_log;
  LOG_ZIP_SIZE_T nLength = 0;
  int release_yn = 0;
  int temp_length = 0;
  int undo_length = 0;
  int redo_length = 0;

  bool is_zip = false;
  bool is_undo_zip = false;

  char *undo_data = NULL;
  LOG_ZIP *log_unzip_data = NULL;
  LOG_ZIP *log_undo_data = NULL;
  int rec_len = 0;

  SLAVE_INFO *sInfo;

  bool bIsDiff = false;

  pg = pgptr;
  LSA_COPY (&lsa, &prev_lrec->forw_lsa);
  prev_log = *(struct log_undoredo **) logs;

  sInfo = repl_ag_get_slave_info (NULL);
  if (sInfo == NULL)
    {
      return REPL_AGENT_INTERNAL_ERROR;
    }

  log_undo_data = sInfo->undo_unzip_ptr;
  log_unzip_data = sInfo->redo_unzip_ptr;

  while (true)
    {
      while (pg && pg->hdr.logical_pageid == lsa.pageid)
	{
	  lrec = (struct log_rec *) ((char *) pg->area + lsa.offset);
	  if (lrec->trid == prev_lrec->trid &&
	      (lrec->type == LOG_UNDOREDO_DATA
	       || lrec->type == LOG_DIFF_UNDOREDO_DATA))
	    {
	      if (lrec->type == LOG_DIFF_UNDOREDO_DATA)
		{
		  bIsDiff = true;
		}
	      else
		{
		  bIsDiff = false;
		}

	      offset = SSIZEOF (struct log_rec) + lsa.offset;
	      pageid = lsa.pageid;
	      REPL_LOG_READ_ALIGN (offset, pageid, pg, release_yn, m_idx);
	      length = SSIZEOF (struct log_undoredo);
	      REPL_LOG_READ_ADVANCE_WHEN_DOESNT_FIT (length, offset,
						     pageid, pg,
						     release_yn, m_idx);
	      if (error == NO_ERROR)
		{
		  undoredo =
		    (struct log_undoredo *) ((char *) pg->area + offset);
		  undo_length = undoredo->ulength;
		  temp_length = undoredo->rlength;
		  length = GET_ZIP_LEN (undoredo->rlength);

		  if (undoredo->data.rcvindex == RVHF_UPDATE &&
		      undoredo->data.pageid == prev_log->data.pageid &&
		      undoredo->data.offset == prev_log->data.offset &&
		      undoredo->data.volid == prev_log->data.volid)
		    {
		      REPL_LOG_READ_ADD_ALIGN (SSIZEOF (*undoredo),
					       offset, pageid, pg,
					       release_yn, m_idx);

		      if (bIsDiff)
			{
			  if (ZIP_CHECK (undo_length))
			    {
			      is_undo_zip = true;
			      undo_length = GET_ZIP_LEN (undo_length);
			    }

			  undo_data = (char *) malloc (undo_length);
			  if (undo_data == NULL)
			    {
			      REPL_ERR_LOG (REPL_FILE_AGENT,
					    REPL_AGENT_MEMORY_ERROR);
			      return REPL_AGENT_MEMORY_ERROR;
			    }

			  repl_log_copy_fromlog (NULL, undo_data,
						 undo_length, pageid,
						 offset, pg, m_idx);

			  if (is_undo_zip)
			    {
			      if (!log_unzip
				  (log_undo_data, undo_length, undo_data))
				{
				  if (release_yn == 1 && pg)
				    {
				      repl_ag_release_page_buffer (pg->
								   hdr.
								   logical_pageid,
								   m_idx);
				    }
				  REPL_ERR_LOG (REPL_FILE_AGENT,
						REPL_AGENT_UNZIP_ERROR);
				  if (undo_data)
				    {
				      free_and_init (undo_data);
				    }
				  return REPL_AGENT_UNZIP_ERROR;
				}
			    }
			  REPL_LOG_READ_ADD_ALIGN (undo_length, offset,
						   pageid, pg,
						   release_yn, m_idx);
			}
		      else
			{
			  REPL_LOG_READ_ADD_ALIGN (GET_ZIP_LEN
						   (undo_length),
						   offset, pageid, pg,
						   release_yn, m_idx);
			}

		      if (ZIP_CHECK (temp_length))
			{
			  is_zip = true;
			  nLength = GET_ZIP_LEN (temp_length);
			  repl_log_copy_fromlog (NULL, *data, nLength,
						 pageid, offset, pg, m_idx);
			}
		      else
			{
			  repl_log_copy_fromlog (*rec_type, *data,
						 length, pageid, offset,
						 pg, m_idx);
			  is_zip = false;
			}

		      if (is_zip && nLength != 0)
			{
			  if (!log_unzip (log_unzip_data, nLength, *data))
			    {
			      if (release_yn == 1 && pg)
				{
				  repl_ag_release_page_buffer (pg->hdr.
							       logical_pageid,
							       m_idx);
				}
			      REPL_ERR_LOG (REPL_FILE_AGENT,
					    REPL_AGENT_UNZIP_ERROR);
			      if (undo_data)
				{
				  free_and_init (undo_data);
				}
			      return REPL_AGENT_UNZIP_ERROR;
			    }
			}

		      if (is_zip)
			{
			  if (bIsDiff)
			    {
			      if (is_undo_zip && log_undo_data != NULL)
				{
				  undo_length = log_undo_data->data_length;
				  redo_length = log_unzip_data->data_length;

				  (void) log_diff (undo_length,
						   log_undo_data->
						   log_data,
						   redo_length,
						   log_unzip_data->log_data);
				}
			      else
				{
				  redo_length = log_unzip_data->data_length;
				  (void) log_diff (undo_length,
						   undo_data,
						   redo_length,
						   log_unzip_data->log_data);
				}
			    }
			  else
			    {
			      redo_length = log_unzip_data->data_length;
			    }

			  if (rec_type)
			    {
			      rec_len = SSIZEOF (INT16);
			      memcpy (*rec_type,
				      log_unzip_data->log_data, rec_len);
			      memcpy (*data,
				      log_unzip_data->log_data +
				      rec_len, redo_length - rec_len);
			      length = redo_length - rec_len;
			    }
			  else
			    {
			      memcpy (*data, log_unzip_data->log_data,
				      redo_length);
			      length = redo_length;
			    }
			}

		      *d_length = length;
		      if (release_yn == 1 && pg)
			{
			  repl_ag_release_page_buffer (pg->hdr.
						       logical_pageid, m_idx);
			}

		      if (undo_data)
			{
			  free_and_init (undo_data);
			}

		      return error;
		    }
		}
	    }
	  else if (lrec->trid == prev_lrec->trid &&
		   (lrec->type == LOG_COMMIT || lrec->type == LOG_ABORT))
	    {
	      REPL_ERR_LOG (REPL_FILE_AGENT, REPL_AGENT_INTERNAL_ERROR);
	      if (release_yn == 1 && pg)
		{
		  repl_ag_release_page_buffer (pg->hdr.logical_pageid, m_idx);
		}
	      return error;
	    }
	  LSA_COPY (&lsa, &lrec->forw_lsa);
	}

      if (release_yn == 1 && pg)
	{
	  repl_ag_release_page_buffer (pg->hdr.logical_pageid, m_idx);
	}
      pg = repl_ag_get_page (lsa.pageid, m_idx);
      release_yn = 1;
    }

  if (release_yn == 1 && pg)
    {
      repl_ag_release_page_buffer (pg->hdr.logical_pageid, m_idx);
    }

  return error;
}

/*
 * repl_ag_get_recdes() - get the record description from the log file
 *   return: NO_ERROR or error code
 *    m_idx: the array index of the target master info
 *    pgptr: point to the target log page
 *    recdes(out): record description (output)
 *    rcvindex(out): recovery index (output)
 *    log_data: log data area
 *    ovf_yn(out)  : true if the log data is in overflow page
 *
 * Note:
 *     To replicate the data, we have to filter the record descripion
 *     from the log record. This function retrieves the record description
 *     for the given lsa.
 *
 *      call chain:  <- repl_ag_apply_update_log() <- repl_ag_apply_repl_log()
 *
 *      called by APPLY thread
 */
static int
repl_ag_get_recdes (LOG_LSA * lsa, int m_idx, LOG_PAGE * pgptr,
		    RECDES * recdes, unsigned int *rcvindex,
		    char *log_data, char *rec_type, bool * ovfyn)
{
  struct log_rec *lrec;
  LOG_PAGE *pg;
  int length;
  int error = NO_ERROR;
  char *area = NULL;
  void *logs = NULL;

  pg = pgptr;
  lrec = (struct log_rec *) ((char *) pg->area + lsa->offset);

  error = repl_ag_get_log_data (lrec, lsa, pg, m_idx, 0, rcvindex,
				&logs, &rec_type, &log_data, &length);

  if (error == NO_ERROR)
    {
      recdes->type = *(INT16 *) (rec_type);
      recdes->data = log_data;
      recdes->area_size = recdes->length = length;
    }
  else
    REPL_ERR_LOG (REPL_FILE_AGENT, REPL_AGENT_INTERNAL_ERROR);

  /* Now.. we have to process overflow pages */
  length = 0;
  if (*rcvindex == RVOVF_CHANGE_LINK)
    {
      /* if overflow page update */
      error = repl_ag_get_overflow_recdes (lrec, logs, &area, &length,
					   m_idx, RVOVF_PAGE_UPDATE);
      recdes->type = REC_BIGONE;
    }
  else if (recdes->type == REC_BIGONE)
    {
      /* if overflow page insert */
      error = repl_ag_get_overflow_recdes (lrec, logs, &area, &length,
					   m_idx, RVOVF_NEWPAGE_INSERT);
    }
  else if (*rcvindex == RVHF_INSERT && recdes->type == REC_ASSIGN_ADDRESS)
    {
      error = repl_ag_get_next_update_log (lrec,
					   pg, m_idx, &logs, &rec_type,
					   &log_data, &length);
      if (error == NO_ERROR)
	{
	  recdes->type = *(INT16 *) (rec_type);
	  recdes->data = log_data;
	  recdes->area_size = recdes->length = length;
	}
      return error;
    }
  else if (*rcvindex == RVHF_UPDATE && recdes->type == REC_RELOCATION)
    {
      error = repl_ag_get_relocation_recdes (lrec, pg, m_idx, 0,
					     &logs, &rec_type,
					     &log_data, &length);
      if (error == NO_ERROR)
	{
	  recdes->type = *(INT16 *) (rec_type);
	  recdes->data = log_data;
	  recdes->area_size = recdes->length = length;
	}
      return error;
    }
  else
    {
      return error;
    }

  REPL_CHECK_ERR_ERROR_WITH_FREE (REPL_FILE_AGENT, REPL_AGENT_INTERNAL_ERROR,
				  area);

  recdes->data = (char *) (area);
  recdes->area_size = recdes->length = length;
  *ovfyn = true;

  return error;
}

static bool
repl_debug_check_data (char *str)
{
  int length, i;

  length = strlen (str);
  for (i = 0; i < length; i++)
    {
      if (!(('a' <= str[i] && str[i] <= 'z')
	    || ('A' <= str[i] && str[i] <= 'Z')
	    || ('0' <= str[i] && str[i] <= '9') || str[i] == ' '))
	{
	  return false;
	}
    }
  return true;
}

static int
repl_debug_add_value (struct debug_string **record_data, DB_VALUE * value)
{
  PARSER_VARCHAR *buf;
  PARSER_CONTEXT *parser;

  parser = parser_create_parser ();
  if (parser == NULL)
    {
      return ER_FAILED;
    }

  buf = describe_value (parser, NULL, value);
  if (*record_data == NULL)
    {
      (*record_data) =
	(struct debug_string *) malloc (sizeof (struct debug_string));
      if ((*record_data) != NULL)
	{
	  (*record_data)->data = NULL;
	  (*record_data)->length = 0;
	  (*record_data)->max_length = 0;
	}
    }

  if ((*record_data) != NULL
      && ((*record_data)->max_length
	  <= ((*record_data)->length + pt_get_varchar_length (buf) + 3)))
    {
      (*record_data) =
	(struct debug_string *) realloc ((void *) (*record_data),
					 debug_record_data->max_length +
					 10240);
      if ((*record_data) != NULL)
	{
	  (*record_data)->max_length += 10240;
	}
    }

  if ((*record_data) != NULL && (*record_data)->data)
    {
      strcat ((*record_data)->data,
	      (const char *) pt_get_varchar_bytes (buf));
      strcat ((*record_data)->data, ", ");
    }

  parser_free_parser (parser);

  return NO_ERROR;
}

/*
 * repl_get_current()
 *   return: NO_ERROR or error code
 *
 * Note:
 *     Analyze the record description, get the value for each attribute,
 *     call dbt_put_internal() for update...
 */
static int
repl_get_current (OR_BUF * buf, SM_CLASS * sm_class,
		  int bound_bit_flag, DB_OTMPL * def, DB_VALUE * key)
{
  SM_ATTRIBUTE *att;
  int *vars = NULL;
  int i, j, offset, offset2, pad;
  char *bits, *start, *v_start;
  int rc = NO_ERROR;
  DB_VALUE value;
  int error = NO_ERROR;

  if (sm_class->variable_count)
    {
      vars = (int *) malloc (sizeof (int) * sm_class->variable_count);
      REPL_CHECK_ERR_NULL (REPL_FILE_AGENT, REPL_AGENT_MEMORY_ERROR, vars);
      offset = or_get_int (buf, &rc);
      for (i = 0; i < sm_class->variable_count; i++)
	{
	  offset2 = or_get_int (buf, &rc);
	  vars[i] = offset2 - offset;
	  offset = offset2;
	}
    }

  bits = NULL;
  if (bound_bit_flag)
    {
      /* assume that the buffer is in contiguous memory and that we
       * can seek ahead to the bound bits.  */
      bits = (char *) buf->ptr + sm_class->fixed_size;
    }

  att = sm_class->attributes;
  start = buf->ptr;

  /* process the fixed length column */
  for (i = 0; i < sm_class->fixed_count;
       i++, att = (SM_ATTRIBUTE *) att->header.next)
    {
      if (bits != NULL && !OR_GET_BOUND_BIT (bits, i))
	{
	  /* its a NULL value, skip it */
	  db_make_null (&value);
	  or_advance (buf, tp_domain_disk_size (att->domain));
	}
      else
	{
	  /* read the disk value into the db_value */
	  (*(att->type->readval)) (buf, &value, att->domain, -1, true,
				   NULL, 0);
	}

      /* skip cache object attribute for foreign key */
      if (att->is_fk_cache_attr)
	{
	  continue;
	}

      /* update the column */
      error = dbt_put_internal (def, att->header.name, &value);
      if (error != NO_ERROR)
	{
	  if (vars)
	    {
	      free_and_init (vars);
	    }
	  return error;
	}
      if (debug_Dump_info & REPL_DEBUG_VALUE_CHECK)
	{
	  repl_debug_add_value (&debug_record_data, &value);
	}
    }

  /* round up to a to the end of the fixed block */
  pad = (int) (buf->ptr - start);
  if (pad < sm_class->fixed_size)
    {
      or_advance (buf, sm_class->fixed_size - pad);
    }

  /* skip over the bound bits */
  if (bound_bit_flag)
    {
      or_advance (buf, OR_BOUND_BIT_BYTES (sm_class->fixed_count));
    }

  /* process variable length column */
  v_start = buf->ptr;
  for (i = sm_class->fixed_count, j = 0;
       vars && i < sm_class->att_count && j < sm_class->variable_count;
       i++, j++, att = (SM_ATTRIBUTE *) att->header.next)
    {
      (*(att->type->readval)) (buf, &value, att->domain, vars[j], true,
			       NULL, 0);
      v_start += vars[j];
      buf->ptr = v_start;
      /* update the column */
      error = dbt_put_internal (def, att->header.name, &value);
      if (error != NO_ERROR)
	{
	  free_and_init (vars);
	  return error;
	}
      if (debug_Dump_info & REPL_DEBUG_VALUE_CHECK)
	{
	  repl_debug_add_value (&debug_record_data, &value);
	}
    }

  if (vars != NULL)
    {
      free_and_init (vars);
    }
  return error;
}

/*
 * repl_disk_to_obj() - same function with tf_disk_to_obj, but always use
 *                      the current representation.
 *   return: NO_ERROR or error code
 *
 * Note:
 *     Analyze the record description, get the value for each attribute,
 *     call dbt_put_internal() for update...
 */
static int
repl_disk_to_obj (MOBJ classobj, RECDES * record, DB_OTMPL * def,
		  DB_VALUE * key)
{
  OR_BUF orep, *buf;
  int repid, status;
  SM_CLASS *sm_class;
  unsigned int repid_bits;
  int bound_bit_flag;
  int rc = NO_ERROR;
  int error = NO_ERROR;


  /* Kludge, make sure we don't upgrade objects to OID'd during the reading */

  buf = &orep;
  or_init (buf, record->data, record->length);
  buf->error_abort = 1;

  status = setjmp (buf->env);
  if (status == 0)
    {
      sm_class = (SM_CLASS *) classobj;
      /* Skip over the class OID.  Could be doing a comparison of the class OID
       * and the expected OID here.  Domain & size arguments aren't necessary
       * for the object "readval" function.
       */
      (*(tp_Object.readval)) (buf, NULL, NULL, -1, true, NULL, 0);

      repid_bits = or_get_int (buf, &rc);

      (void) or_get_int (buf, &rc);	/* skip chn */
      (void) or_get_int (buf, &rc);	/* skip dummy header word */

      /* mask out the repid & bound bit flag */
      repid = repid_bits & ~OR_BOUND_BIT_FLAG;

      bound_bit_flag = repid_bits & OR_BOUND_BIT_FLAG;

      error = repl_get_current (buf, sm_class, bound_bit_flag, def, key);
    }
  else
    {
      REPL_ERR_LOG (REPL_FILE_AGENT, REPL_AGENT_INTERNAL_ERROR);
    }

  return error;
}

static int
repl_debug_object2string (DB_OBJECT * class_obj, DB_VALUE * key,
			  struct debug_string **record_data)
{
  SM_CLASS *sm_class;
  SM_ATTRIBUTE *att;
  DB_VALUE value;
  DB_OBJECT *object;
  int i, j, error;

  object = obj_repl_find_object_by_pkey (class_obj, key, AU_FETCH_READ);
  if (object == NULL)
    {
      return er_errid ();
    }

  sm_class = (SM_CLASS *) class_obj;
  att = sm_class->attributes;
  /* process the fixed length column */
  for (i = 0; i < sm_class->fixed_count;
       i++, att = (SM_ATTRIBUTE *) att->header.next)
    {
      /* skip cache object attribute for foreign key */
      if (att->is_fk_cache_attr)
	{
	  continue;
	}

      /* get the column */
      error = db_get (object, att->header.name, &value);
      if (error != NO_ERROR)
	{
	  return error;
	}
      repl_debug_add_value (record_data, &value);
    }

  /* process variable length column */
  for (i = sm_class->fixed_count, j = 0; i < sm_class->att_count;
       i++, j++, att = (SM_ATTRIBUTE *) att->header.next)
    {
      /* get the column */
      error = db_get (object, att->header.name, &value);
      if (error != NO_ERROR)
	{
	  return error;
	}
      repl_debug_add_value (record_data, &value);
    }

  return NO_ERROR;
}

static int
repl_ag_apply_get_object (REPL_ITEM * item)
{
  DB_OBJECT *class_obj;
  int error;

  class_obj = db_find_class (item->class_name);
  if (class_obj == NULL)
    {
      REPL_ERR_LOG (REPL_FILE_AGENT, REPL_AGENT_QUERY_ERROR);
      return REPL_AGENT_CANT_CONNECT_TO_SLAVE;
    }

  item->record = obj_repl_find_object_by_pkey (class_obj, &item->key,
					       AU_FETCH_UPDATE);
  return NO_ERROR;
}

/*
 * repl_ag_apply_update_log() - apply the insert/update log to the target slave
 *   return: NO_ERROR or error code
 *   item : replication item
 *   m_idx: the array index of the target master info
 *
 * Note:
 *      Apply the insert/update log to the target slave.
 *      . get the target log page
 *      . get the record description
 *      . fetch the class info
 *      . if op is INSERT
 *         - create a new obect template
 *        else op is UPDATE
 *         - fetch the target object by pk
 *         - create an existing object template
 *      . transform record description to object, and edit the target object
 *        column by columd.
 *      . finalize the editing of object template - dbt_finish
 *
 *      call chain:  <- repl_ag_apply_repl_log()
 *
 *      called by APPLY thread
 */
static int
repl_ag_apply_update_log (REPL_ITEM * item, int m_idx)
{
  SLAVE_INFO *sinfo;
  DB_OBJECT *class_obj;
  MOBJ mclass;
  LOG_PAGE *pgptr;
  PAGEID old_pageid;
  RECDES recdes;
  unsigned int rcvindex;
  int error, au_save;

  DB_OTMPL *inst_tp = NULL;
  bool ovfyn = false;

  sinfo = repl_ag_get_slave_info (NULL);
  if (sinfo == NULL)
    {
      return REPL_AGENT_INTERNAL_ERROR;
    }

  old_pageid = item->lsa.pageid;
  pgptr = repl_ag_get_page (item->lsa.pageid, m_idx);
  REPL_CHECK_ERR_NULL (REPL_FILE_AGENT, REPL_AGENT_INTERNAL_ERROR, pgptr);

  error = repl_ag_get_recdes (&item->lsa, m_idx, pgptr, &recdes, &rcvindex,
			      sinfo->log_data, sinfo->rec_type, &ovfyn);
  if (error == ER_NET_CANT_CONNECT_SERVER || error == ER_OBJ_NO_CONNECT)
    {
      return error;
    }
  REPL_CHECK_ERR_ERROR (REPL_FILE_AGENT, REPL_AGENT_INTERNAL_ERROR);

  AU_SAVE_AND_DISABLE (au_save);
  if (recdes.type == REC_ASSIGN_ADDRESS || recdes.type == REC_RELOCATION)
    {
      error = REPL_AGENT_INTERNAL_ERROR;
      goto error_rtn;
    }

#if 0
  /* Why does a next if-statement need? */
  if (rcvindex != RVHF_INSERT && item->record == NULL)
    {
      rcvindex = RVHF_INSERT;
      REPL_ERR_LOG (REPL_FILE_AGENT, REPL_AGENT_RECORD_TYPE_ERROR);
      repl_error_flush (err_Log_fp, false);
    }
#endif

  class_obj = db_find_class (item->class_name);
  if (class_obj == NULL)
    {
      error = REPL_AGENT_INTERNAL_ERROR;
      goto error_rtn;
    }

  if (rcvindex == RVHF_INSERT)
    {
      inst_tp = dbt_create_object_internal (class_obj);
    }
  else if (rcvindex == RVHF_UPDATE || rcvindex == RVOVF_CHANGE_LINK)
    {
      inst_tp = dbt_edit_object (item->record);
    }
  if (inst_tp == NULL)
    {
      error = REPL_AGENT_INTERNAL_ERROR;
      goto error_rtn;
    }

  mclass = locator_fetch_class (class_obj, DB_FETCH_CLREAD_INSTREAD);
  if (mclass == NULL)
    {
      error = REPL_AGENT_INTERNAL_ERROR;
      goto error_rtn;
    }

  error = repl_disk_to_obj (mclass, &recdes, inst_tp, &item->key);
  if (error != NO_ERROR)
    {
      goto error_rtn;
    }

  if (dbt_finish_object (inst_tp) == NULL)
    {
      error = REPL_AGENT_INTERNAL_ERROR;
      goto error_rtn;
    }
  AU_RESTORE (au_save);

  if (debug_Dump_info & REPL_DEBUG_VALUE_CHECK)
    {
      repl_debug_object2string (class_obj, &item->key, &debug_workspace_data);
      if (debug_workspace_data != NULL)
	{
	  if (strcmp (debug_record_data->data, debug_workspace_data->data) !=
	      0)
	    {
	      fprintf (debug_Log_fd,
		       "VALUE ERROR: PUT VALUE(%s)\nWORKSPACE(%s)\n",
		       debug_record_data->data, debug_workspace_data->data);
	      fflush (debug_Log_fd);
	    }

	  debug_record_data->data[0] = '\0';
	  debug_record_data->length = (size_t) 0;
	  debug_workspace_data->data[0] = '\0';
	  debug_workspace_data->length = 0;
	}
    }

  if (ovfyn)
    {
      free_and_init (recdes.data);
    }

  repl_ag_release_page_buffer (old_pageid, m_idx);
  return NO_ERROR;

error_rtn:
  AU_RESTORE (au_save);

  if (ovfyn)
    {
      free_and_init (recdes.data);
    }

  if (inst_tp)
    {
      dbt_abort_object (inst_tp);
    }

  if (error > 0)
    {
      REPL_ERR_LOG (REPL_FILE_AGENT, error);
    }
  else
    {
      REPL_ERR_LOG (REPL_FILE_AGENT, REPL_AGENT_QUERY_ERROR);
    }
  repl_ag_release_page_buffer (old_pageid, m_idx);
  return error;
}

/*
 * repl_ag_set_repl_log() - insert the replication item into the apply list
 *   return: NO_ERROR or error code
 *   log_pgptr : pointer to the log page
 *   tranid: the target transaction id
 *   lsa  : the target LSA of the log
 *   m_idx: the array index of the target master info
 *
 * Note:
 *     APPLY thread traverses the transaction log pages, and finds out the
 *     REPLICATION LOG record. If it meets the REPLICATION LOG record,
 *     it adds that record to the apply list for later use.
 *     When the APPLY thread meets the LOG COMMIT record, it applies the
 *     inserted REPLICAION LOG records to the slave.
 *
 *    call chain : APPLY thread
 *
 *    called by apply thread
 *
 *     APPLY thread processes the mutex lock
 */
int
repl_ag_set_repl_log (LOG_PAGE * log_pgptr, int log_type, int tranid,
		      LOG_LSA * lsa, int idx)
{
  SLAVE_INFO *sinfo;
  struct log_replication *repl_log;
  LOG_PAGE *log_pgptr2 = log_pgptr;
  PGLENGTH target_offset;
  char *ptr;
  REPL_APPLY *apply;
  int error = NO_ERROR;
  int length;			/* type change PGLENGTH -> int */
  int t_pageid;
  int m_idx;
  char *class_name;
  char *str_value;
  int release_yn = 0;
  char *area;

  sinfo = repl_ag_get_slave_info (NULL);
  if (sinfo == NULL)
    {
      return REPL_AGENT_INTERNAL_ERROR;
    }

  m_idx = repl_ag_get_master_info_index (sinfo->masters[idx].m_id);

  t_pageid = lsa->pageid;
  target_offset = SSIZEOF (struct log_rec) +lsa->offset;
  length = SSIZEOF (struct log_replication);

  REPL_LOG_READ_ALIGN (target_offset, t_pageid, log_pgptr2, release_yn,
		       m_idx);
  REPL_CHECK_ERR_ERROR (REPL_FILE_AGENT, REPL_AGENT_INTERNAL_ERROR);
  REPL_LOG_READ_ADVANCE_WHEN_DOESNT_FIT (length, target_offset, t_pageid,
					 log_pgptr2, release_yn, m_idx);
  REPL_CHECK_ERR_ERROR (REPL_FILE_AGENT, REPL_AGENT_INTERNAL_ERROR);

  repl_log =
    (struct log_replication *) ((char *) log_pgptr2->area + target_offset);
  target_offset += length;
  length = repl_log->length;

  REPL_LOG_READ_ALIGN (target_offset, t_pageid, log_pgptr2, release_yn,
		       m_idx);
  REPL_CHECK_ERR_ERROR (REPL_FILE_AGENT, REPL_AGENT_INTERNAL_ERROR);

  area = (char *) malloc (length);
  REPL_CHECK_ERR_NULL (REPL_FILE_AGENT, REPL_AGENT_MEMORY_ERROR, area);
  repl_log_copy_fromlog (NULL, area, length, t_pageid, target_offset,
			 log_pgptr2, m_idx);

  apply = repl_ag_find_apply_list (sinfo, tranid, idx);
  REPL_CHECK_ERR_NULL_WITH_FREE (REPL_FILE_AGENT, REPL_AGENT_INTERNAL_ERROR,
				 apply, area);

  ptr = area;
  switch (log_type)
    {
    case LOG_REPLICATION_DATA:
      {
	ptr = or_unpack_string (ptr, &class_name);

	if (!sinfo->masters[idx].all_repl
	    && repl_ag_get_class_from_repl_group (sinfo, idx, class_name,
						  lsa) == NULL)
	  {
	    /* This class should not be replicated */
	    db_private_free_and_init (NULL, class_name);
	  }
	else
	  {
	    error = repl_ag_add_repl_item (apply, repl_log->lsa);
	    REPL_CHECK_ERR_ERROR_WITH_FREE (REPL_FILE_AGENT,
					    REPL_AGENT_MEMORY_ERROR, area);
	    ptr = or_unpack_mem_value (ptr, &apply->repl_tail->key);
	    REPL_CHECK_ERR_ERROR_WITH_FREE (REPL_FILE_AGENT,
					    REPL_AGENT_INTERNAL_ERROR, area);
	    apply->repl_tail->class_name = class_name;
	    apply->repl_tail->item_type = repl_log->rcvindex;
	  }
	break;
      }
    case LOG_REPLICATION_SCHEMA:
      {
	if (sinfo->masters[idx].all_repl || sinfo->masters[idx].for_recovery)
	  {
	    error = repl_ag_add_repl_item (apply, repl_log->lsa);
	    REPL_CHECK_ERR_ERROR_WITH_FREE (REPL_FILE_AGENT,
					    REPL_AGENT_INTERNAL_ERROR, area);
	    ptr = or_unpack_int (ptr, &apply->repl_tail->item_type);
	    ptr = or_unpack_string (ptr, &apply->repl_tail->class_name);
	    ptr = or_unpack_string (ptr, &str_value);
	    db_make_string (&apply->repl_tail->key, str_value);
	    apply->repl_tail->key.need_clear = true;
	  }
	break;
      }
    default:
      {
	free_and_init (area);
	REPL_ERR_RETURN (REPL_FILE_AGENT, REPL_AGENT_INTERNAL_ERROR);
      }
    }
  apply->repl_tail->log_type = log_type;

  if (release_yn == 1)
    {
      repl_ag_release_page_buffer (log_pgptr2->hdr.logical_pageid, m_idx);
    }

  free_and_init (area);
  return error;
}

/*
 * repl_ag_retrieve_eot_time() - Retrieve the timestamp of End of Transaction
 *   return: NO_ERROR or error code
 *   log_pgptr : pointer to the log page
 *   m_idx: the array index of the target master info
 *
 * Note:
 *
 *    call chain : APPLY thread
 *
 *    called by apply thread
 *
 *     APPLY thread processes the mutex lock
 */
int
repl_ag_retrieve_eot_time (LOG_PAGE * pgptr, LOG_LSA * lsa, int m_idx,
			   time_t * time)
{
  int error = NO_ERROR;
  struct log_donetime *donetime;
  PAGEID pageid;
  PGLENGTH offset;
  LOG_PAGE *pg;
  int release_yn = 0;

  pageid = lsa->pageid;
  offset = SSIZEOF (struct log_rec) + lsa->offset;

  pg = pgptr;

  REPL_LOG_READ_ALIGN (offset, pageid, pg, release_yn, m_idx);
  if (pg == NULL)
    {
      return error;
    }

  REPL_LOG_READ_ADVANCE_WHEN_DOESNT_FIT (SSIZEOF (*donetime), offset,
					 pageid, pg, release_yn, m_idx);
  if (pg == NULL)
    {
      return error;
    }

  donetime = (struct log_donetime *) ((char *) pg->area + offset);

  if (release_yn == 1)
    {
      repl_ag_release_page_buffer (pg->hdr.logical_pageid, m_idx);
    }

  *time = donetime->at_time;
  return NO_ERROR;
}

/*
 * repl_ag_add_unlock_commit_log() - add the unlock_commit log to the
 *                                   commit list
 *   return: NO_ERROR or error code
 *   tranid: the target transaction id
 *   lsa   : the target LSA of the log
 *   idx   : the array index of the target master info
 *
 * Note:
 *     APPLY thread traverses the transaction log pages, and finds out the
 *     REPLICATION LOG record. If it meets the REPLICATION LOG record,
 *     it adds that record to the apply list for later use.
 *     When the APPLY thread meets the LOG COMMIT record, it applies the
 *     inserted REPLICAION LOG records into the slave.
 *     The APPLY thread applies transaction  not in regular sequence of
 *     LOG_COMMIT record, but in sequence of  LOG_UNLOCK_COMMIT record.
 *     When the APPLY thread meet the LOG_UNLOCK_COMMIT record, It doesn't
 *     apply  REPLICATION LOC record to the slave and insert REPLICATION LOC
 *     record into commit list.
 *
 *    call chain : APPLY thread
 *
 *    called by APPLY thread
 *
 *    APPLY thread processes the mutex lock
 */
int
repl_ag_add_unlock_commit_log (int tranid, LOG_LSA * lsa, int idx)
{
  SLAVE_INFO *sinfo;
  REPL_APPLY *apply;
  REPL_COMMIT *commit, *tmp;
  int error = NO_ERROR;

  sinfo = repl_ag_get_slave_info (NULL);
  if (sinfo == NULL)
    {
      return REPL_AGENT_INTERNAL_ERROR;
    }

  apply = repl_ag_find_apply_list (sinfo, tranid, idx);
  REPL_CHECK_ERR_NULL (REPL_FILE_AGENT, REPL_AGENT_INTERNAL_ERROR, apply);

  for (tmp = sinfo->masters[idx].commit_head; tmp; tmp = tmp->next)
    {
      if (tmp->tranid == tranid)
	return error;
    }

  commit = (REPL_COMMIT *) malloc (sizeof (REPL_COMMIT));
  REPL_CHECK_ERR_NULL (REPL_FILE_AGENT, REPL_AGENT_INTERNAL_ERROR, commit);
  commit->next = NULL;
  commit->type = LOG_UNLOCK_COMMIT;
  LSA_COPY (&commit->log_lsa, lsa);
  commit->tranid = tranid;
  commit->master_time = -1;

  if (sinfo->masters[idx].commit_head == NULL
      && sinfo->masters[idx].commit_tail == NULL)
    {
      sinfo->masters[idx].commit_head = commit;
      sinfo->masters[idx].commit_tail = commit;
    }
  else
    {
      sinfo->masters[idx].commit_tail->next = commit;
      sinfo->masters[idx].commit_tail = commit;
    }

  return error;
}

/*
 * repl_ag_set_commit_log() - update the unlock_commit log to the commit list
 *   return: NO_ERROR or error code
 *   tranid : the target transaction id
 *   lsa : the target LSA of the log
 *   idx : the array index of the target master info
 *
 * Note:
 *     APPLY thread traverses the transaction log pages, and finds out the
 *     REPLICATION LOG record. If it meets the REPLICATION LOG record,
 *     it adds that record to the apply list for later use.
 *     When the APPLY thread meets the LOG COMMIT record, it applies the
 *     inserted REPLICAION LOG records into the slave.
 *     The APPLY thread applies transaction  not in sequence of
 *     LOG_COMMIT record, but in regular sequence of  LOG_UNLOCK_COMMIT record.
 *     When the APPLY thread meet the LOG_COMMIT record, It applies
 *     REPLICATION LOC record to the slave in regular sequence of commit list.
 *
 * NOTE
 *
 *    call chain : APPLY thread
 *
 *    called by APPLY thread
 *
 *    APPLY thread processes the mutex lock
 */
int
repl_ag_set_commit_log (int tranid, LOG_LSA * lsa, int idx,
			time_t master_time)
{
  SLAVE_INFO *sinfo;
  REPL_COMMIT *commit;

  sinfo = repl_ag_get_slave_info (NULL);
  if (sinfo == NULL)
    {
      return REPL_AGENT_INTERNAL_ERROR;
    }

  commit = sinfo->masters[idx].commit_head;
  while (commit)
    {
      if (commit->tranid == tranid)
	{
	  commit->type = LOG_COMMIT;
	  commit->master_time = master_time;
	}
      commit = commit->next;
    }

  return NO_ERROR;
}

/*
 * repl_ag_log_get_file_line()
 *   return: line count
 *   fp(in)
 */
static int
repl_ag_log_get_file_line (FILE * fp)
{
  char line[1024];
  int line_count = 0;

  fseek (fp, 0, SEEK_SET);
  while (fgets (line, 1024, fp) != NULL)
    {
      line_count++;
    }

  return ((line_count < 2) ? (line_count - 2) : 0);
}

/*
 * repl_ag_log_perf_info()
 *   return:
 *   master_db_name(in)
 *   tranid(in)
 *   master_time(in)
 *   slave_time(in)
 */
int
repl_ag_log_perf_info (char *master_dbname, int tranid,
		       const time_t * master_time, const time_t * slave_time)
{
  static int line_count = 0;
  static bool reach_end_of_log = false;
  static char perf_file_path[PATH_MAX], bak_file_path[PATH_MAX];
  struct tm *m_tm_p, *s_tm_p;
  char m_tm_array[256], s_tm_array[256];
  char *time_array_m = m_tm_array;
  char *time_array_s = s_tm_array;
  int error;
  int delay_time;
  bool append_message = false;

  if (perf_Log_fp == NULL)
    {
      snprintf (perf_file_path, PATH_MAX, "%s/%s.perf", dist_LogPath,
		dist_Dbname);
      snprintf (bak_file_path, PATH_MAX, "%s/%s.perf.bak", dist_LogPath,
		dist_Dbname);

      if ((perf_Log_fp = fopen (perf_file_path, "a+")) == NULL)
	{
	  perf_Log_fp = stdout;
	}
      else
	{
	  line_count = repl_ag_log_get_file_line (perf_Log_fp);
	}
    }

  if (line_count > perf_Log_size)
    {
      fclose (perf_Log_fp);
      rename (perf_file_path, bak_file_path);
      if ((perf_Log_fp = fopen (perf_file_path, "w")) == NULL)
	{
	  perf_Log_fp = stdout;
	}
      line_count = 0;
    }

  if (line_count == 0)
    {
      fprintf (perf_Log_fp,
	       "--------------------------------------------------------------------------------\n");
      fprintf (perf_Log_fp,
	       " No. master_db_name     tran_index        master_time         slave_time     delay\n");
      fprintf (perf_Log_fp,
	       "--------------------------------------------------------------------------------\n");
      line_count++;
    }

  if (master_time != NULL)
    {
      m_tm_p = localtime (master_time);
      if (m_tm_p)
	{
	  strftime (time_array_m, 256, "%Y/%m/%d %H:%M:%S", m_tm_p);
	}
    }
  else
    {
      sprintf (time_array_m, "----/--/-- --:--:--");
    }

  if (slave_time != NULL)
    {
      s_tm_p = localtime (slave_time);
      if (s_tm_p)
	{
	  strftime (time_array_s, 256, "%Y/%m/%d %H:%M:%S", s_tm_p);
	}
    }
  else
    {
      sprintf (time_array_s, "----/--/-- --:--:--");
    }

  if (slave_time != NULL)
    {
      if (tranid == -1 || master_time == NULL)
	{
	  if (!reach_end_of_log)
	    {
	      delay_time = 0;
	      reach_end_of_log = true;
	      append_message = true;
	    }
	}
      else
	{
	  delay_time = (int) difftime (*slave_time, *master_time);
	  reach_end_of_log = false;
	  append_message = true;
	}
    }

  if (append_message)
    {
      if (fprintf (perf_Log_fp, "%03d %14s %15d %s %s  %d\n",
		   line_count, master_dbname, tranid, time_array_m,
		   time_array_s, delay_time) < 0)
	{
	  REPL_ERR_LOG (REPL_FILE_AGENT, REPL_AGENT_INTERNAL_ERROR);
	  line_count = perf_Log_size + 1;
	  return NO_ERROR;
	}
      line_count++;
      fflush (perf_Log_fp);
    }

  return NO_ERROR;
}

/*
 * repl_ag_apply_commit_list() - apply the log to the target slave
 *   return: NO_ERROR or error code
 *   lsa  : the target LSA of the log
 *   idx: the array index of the target master info
 *
 * Note:
 *    This function is called when the APPLY thread meets the LOG_COMMIT
 *    record.
 *
 *      call chain:  repl_tr_log_apply() <- APPLY
 *
 *      called by APPLY thread
 */
int
repl_ag_apply_commit_list (LOG_LSA * lsa, int idx, time_t * old_time,
			   bool clear_tran)
{
  SLAVE_INFO *sinfo;
  REPL_COMMIT *commit;
  int error = NO_ERROR;
  time_t slave_time;
  int m_idx;

  sinfo = repl_ag_get_slave_info (NULL);
  if (sinfo == NULL)
    {
      return REPL_AGENT_INTERNAL_ERROR;
    }

  m_idx = repl_ag_get_master_info_index (sinfo->masters[idx].m_id);

  LSA_SET_NULL (lsa);

  commit = sinfo->masters[idx].commit_head;
  if (commit && commit->type == LOG_COMMIT)
    {
      error = repl_ag_apply_repl_log (commit->tranid, idx,
				      &sinfo->masters[idx].total_rows,
				      clear_tran);

      /* compute the delay time and save it ! */
      time (&slave_time);
      if (commit->master_time > 0
	  && (sinfo->masters[idx].perf_poll_interval <= 0
	      || (slave_time - (*old_time)) >
	      sinfo->masters[idx].perf_poll_interval))
	{
	  repl_ag_log_perf_info (mInfo[m_idx]->conn.dbname,
				 commit->tranid,
				 &commit->master_time, &slave_time);
	  time (old_time);
	}

      LSA_COPY (lsa, &commit->log_lsa);

      sinfo->masters[idx].commit_head = commit->next;
      if (sinfo->masters[idx].commit_head == NULL)
	sinfo->masters[idx].commit_tail = NULL;

      free_and_init (commit);
    }

  return error;
}

/*
 * repl_ag_apply_abort()
 *   return: none
 *   idx(in)
 *   tranid(in)
 *   master_time(in)
 *   old_time(in/out)
 */
void
repl_ag_apply_abort (int idx, int tranid, time_t master_time,
		     time_t * old_time)
{
  SLAVE_INFO *sinfo;
  time_t slave_time;
  int m_idx;

  sinfo = repl_ag_get_slave_info (NULL);
  if (sinfo == NULL)
    {
      return;
    }
  m_idx = repl_ag_get_master_info_index (sinfo->masters[idx].m_id);

  time (&slave_time);
  if (sinfo->masters[idx].perf_poll_interval <= 0 ||
      (slave_time - (*old_time)) > sinfo->masters[idx].perf_poll_interval)
    {
      repl_ag_log_perf_info (mInfo[m_idx]->conn.dbname,
			     tranid, &master_time, &slave_time);
      time (old_time);
    }
}

static void
repl_write_debug_item_info (REPL_ITEM * item)
{
  PARSER_VARCHAR *buf = NULL;
  PARSER_CONTEXT *parser;
  const char *type = "unknown";

  if (item->log_type == LOG_REPLICATION_SCHEMA)
    {
      type = "SCHEMA";
    }
  else if (item->log_type == LOG_REPLICATION_DATA)
    {
      switch (item->item_type)
	{
	case RVREPL_DATA_DELETE:
	  type = "DELETE";
	  break;
	case RVREPL_DATA_INSERT:
	  type = "INSERT";
	  break;
	case RVREPL_DATA_UPDATE_START:
	case RVREPL_DATA_UPDATE:
	case RVREPL_DATA_UPDATE_END:
	  type = "UPDATE";
	  break;
	default:
	  type = "unknown";
	  break;
	}
    }

  parser = parser_create_parser ();
  if (parser != NULL)
    {
      buf = describe_value (parser, NULL, &item->key);
    }

  fprintf (debug_Log_fd, "%s:[%s](%d,%d), ", type,
	   buf ? (const char *) pt_get_varchar_bytes (buf) : "",
	   item->lsa.pageid, item->lsa.offset);
  fflush (debug_Log_fd);

  if (parser != NULL)
    {
      parser_free_parser (parser);
    }
}

static void
repl_write_debug_apply_info (REPL_APPLY * apply)
{
  if (apply->repl_head == NULL)
    {
      fprintf (debug_Log_fd, "APPLY TRAN[%10d] : HEADER NULL\n",
	       apply->tranid);
    }
  else
    {
      fprintf (debug_Log_fd, "APPLY TRAN[%10d] : ", apply->tranid);
    }
  fflush (debug_Log_fd);
}

static void
repl_write_error_db_info (REPL_ITEM * item, int error)
{
  PARSER_VARCHAR *buf = NULL;
  PARSER_CONTEXT *parser;
  char error_string[1024];

  parser = parser_create_parser ();
  if (parser != NULL)
    {
      buf = describe_value (parser, NULL, &item->key);
    }

  sprintf (error_string, "[%s,%s] %s", item->class_name,
	   buf ? (const char *) pt_get_varchar_bytes (buf) : "",
	   db_error_string (1));
  if (error > 0)
    {
      REPL_ERR_LOG_ONE_ARG (REPL_FILE_AGENT, error, error_string);
    }
  else
    {
      REPL_ERR_LOG_ONE_ARG (REPL_FILE_AGENT, REPL_AGENT_INTERNAL_ERROR,
			    error_string);
    }
  repl_error_flush (err_Log_fp, false);

  if (parser != NULL)
    {
      parser_free_parser (parser);
    }
}

static int
repl_ag_apply_repl_item (REPL_ITEM * item, int idx, int *update_cnt)
{
  int error = NO_ERROR;

  if (item->log_type == LOG_REPLICATION_SCHEMA)
    {
      error = repl_ag_apply_schema_log (item);
    }
  else if (item->log_type == LOG_REPLICATION_DATA)
    {
      switch (item->item_type)
	{
	case RVREPL_DATA_DELETE:
	  error = repl_ag_apply_delete_log (item);
	  break;
	case RVREPL_DATA_INSERT:
	case RVREPL_DATA_UPDATE_START:
	case RVREPL_DATA_UPDATE:
	case RVREPL_DATA_UPDATE_END:
	  error = repl_ag_apply_update_log (item, idx);
	  break;
	default:
	  error = REPL_AGENT_RECORD_TYPE_ERROR;
	  break;
	}
    }

  if (debug_Dump_info & REPL_DEBUG_AGENT_STATUS)
    {
      repl_write_debug_item_info (item);
    }

  if (error == NO_ERROR)
    {
      (*update_cnt)++;
    }
  else
    {
      repl_write_error_db_info (item, error);
    }

  return error;
}

/*
 * repl_ag_apply_repl_log() - apply the log to the target slave
 *   return: NO_ERROR or error code
 *   tranid: the target transaction id
 *   m_idx: the array index of the target master info
 *
 * Note:
 *    This function is called when the APPLY thread meets the LOG_COMMIT
 *    record.
 *
 *      call chain:  repl_tr_log_apply() <- APPLY
 *
 *      called by APPLY thread
 */
int
repl_ag_apply_repl_log (int tranid, int idx, int *total_rows, bool clear_tran)
{
  SLAVE_INFO *sinfo = repl_ag_get_slave_info (NULL);
  REPL_APPLY *apply;
  REPL_ITEM *item, *multi_update_item = NULL;
  int error = NO_ERROR;
  int update_cnt = 0;
  bool multi_update_mode = false;

  if (sinfo == NULL)
    {
      return REPL_AGENT_INTERNAL_ERROR;
    }

  apply = repl_ag_find_apply_list (sinfo, tranid, idx);
  REPL_CHECK_ERR_NULL (REPL_FILE_AGENT, REPL_AGENT_INTERNAL_ERROR, apply);

  if (debug_Dump_info & REPL_DEBUG_AGENT_STATUS)
    {
      repl_write_debug_apply_info (apply);
    }
  if (apply->repl_head == NULL)
    {
      repl_ag_clear_repl_item (apply, clear_tran);
      return NO_ERROR;
    }

  item = apply->repl_head;
  while (item && error == NO_ERROR)
    {
      if (item->log_type == LOG_REPLICATION_DATA)
	{
	  switch (item->item_type)
	    {
	    case RVREPL_DATA_UPDATE_START:
	      multi_update_mode = true;
	      multi_update_item = item;
	      error = repl_ag_apply_get_object (item);
	      break;
	    case RVREPL_DATA_UPDATE:
	      error = repl_ag_apply_get_object (item);
	      if (!multi_update_mode)
		{
		  error = repl_ag_apply_repl_item (item, idx, &update_cnt);
		}
	      break;
	    case RVREPL_DATA_UPDATE_END:
	      error = repl_ag_apply_get_object (item);
	      if (multi_update_item != NULL)
		{
		  while (multi_update_item != item->next)
		    {
		      error = repl_ag_apply_repl_item (multi_update_item, idx,
						       &update_cnt);
		      multi_update_item = multi_update_item->next;
		    }
		  multi_update_mode = false;
		}
	      break;
	    case RVREPL_DATA_INSERT:
	    case RVREPL_DATA_DELETE:
	    default:
	      error = repl_ag_apply_repl_item (item, idx, &update_cnt);
	      break;
	    }
	}
      else if (item->log_type == LOG_REPLICATION_SCHEMA)
	{
	  error = repl_ag_apply_repl_item (item, idx, &update_cnt);
	}
      else
	{
	  error = REPL_AGENT_RECORD_TYPE_ERROR;
	}
      item = item->next;
    }

  if (debug_Dump_info & REPL_DEBUG_AGENT_STATUS)
    {
      fprintf (debug_Log_fd, "END\n");
      fflush (debug_Log_fd);
    }

  *total_rows += update_cnt;
  repl_ag_clear_repl_item (apply, clear_tran);
  return error;
}

/*
 * repl_init_pb() - initialize the page buffer area
 *   return: the allocated pointer to a page buffer
 *
 * Note:
 *     called by MAIN thread of repl_agent
 *       <- repl_ag_add_master_info (repl_agent.c)
 *     called by MAIN thread of repl_server
 *       <- repl_init_pb_all (repl_server.c)
 */
REPL_PB *
repl_init_pb (void)
{
  REPL_PB *pb;

  pb = (REPL_PB *) malloc (sizeof (REPL_PB));
  if (pb == NULL)
    {
      REPL_ERR_RETURN_NULL (REPL_FILE_AGENT, REPL_AGENT_MEMORY_ERROR);
    }

  pb->log_hdr_buffer = malloc (REPL_DEF_LOG_PAGE_SIZE);
  if (pb->log_hdr_buffer == NULL)
    {
      REPL_ERR_RETURN_NULL_WITH_FREE (REPL_FILE_AGENT,
				      REPL_AGENT_MEMORY_ERROR, pb);
    }

  pb->log_hdr = NULL;
  pb->log_buffer = NULL;

  pb->head = pb->tail = 0;
  pb->log_vdes = NULL_VOLDES;

  pb->start_pageid = 0;
  pb->max_pageid = 0;
  pb->min_pageid = 0;
  pb->need_shutdown = false;
  pb->read_pageid = 0;
  pb->read_invalid_page = false;
  pb->on_demand = false;
  pb->start_to_archive = false;

  PTHREAD_MUTEX_INIT (pb->mutex);
  PTHREAD_COND_INIT (pb->read_cond);
  PTHREAD_COND_INIT (pb->write_cond);
  PTHREAD_COND_INIT (pb->end_cond);

  return (pb);
}

/*
 * repl_init_cache_pb() - initialize the cache page buffer area
 *   return: the allocated pointer to a cache page buffer
 *
 * Note:
 *     called by MAIN thread of repl_agent
 *       <- repl_ag_add_master_info (repl_agent.c)
 */
static REPL_CACHE_PB *
repl_init_cache_pb (void)
{
  REPL_CACHE_PB *cache_pb;

  cache_pb = (REPL_CACHE_PB *) malloc (sizeof (REPL_CACHE_PB));
  if (cache_pb == NULL)
    REPL_ERR_RETURN_NULL (REPL_FILE_AGENT, REPL_AGENT_MEMORY_ERROR);

  cache_pb->hash_table = NULL;
  cache_pb->log_buffer = NULL;
  cache_pb->num_buffers = 0;
  cache_pb->clock_hand = 0;
  cache_pb->buffer_area = NULL;

  PTHREAD_MUTEX_INIT (cache_pb->mutex);

  return (cache_pb);
}

/*
 * repl_init_log_buffer() - Initialize the log buffer area of a page buffer
 *   return: NO_ERROR or REPL_AGENT_MEMORY_ERROR
 *   pb(out)
 *   lb_cnt(in): the # of log buffers per page buffer
 *   lb_size(in)
 *
 * Note:
 *         : allocate the page buffer area
 *         : the size of page buffer area is determined after reading the
 *           log header, so we split the "initialize" and "allocate" phase.
 *
 *     called by MAIN thread  of repl_server
 *        <- repl_init_log_buffer_all
 *     called by RECV thread  of repl_agent
 *        <- repl_tr_log_recv
 */
int
repl_init_log_buffer (REPL_PB * pb, int lb_cnt, int lb_size)
{
  int i, j;
  int error = NO_ERROR;

  pb->log_buffer = malloc (lb_cnt * sizeof (REPL_LOG_BUFFER *));
  REPL_CHECK_ERR_NULL (REPL_FILE_AGENT, REPL_AGENT_MEMORY_ERROR,
		       pb->log_buffer);

  for (i = 0; (error == NO_ERROR) && (i < lb_cnt); i++)
    {
      pb->log_buffer[i] = malloc (lb_size);
      if (pb->log_buffer[i] == NULL)
	REPL_ERR_LOG (REPL_FILE_AGENT, REPL_AGENT_MEMORY_ERROR);
      pb->log_buffer[i]->pageid = 0;
      pb->log_buffer[i]->phy_pageid = 0;
    }

  if (error != NO_ERROR)
    {
      if (pb->log_buffer != NULL)
	{
	  for (j = 0; j < i; j++)
	    free_and_init (pb->log_buffer[j]);
	  free_and_init (pb->log_buffer);
	}
      return error;
    }

  return NO_ERROR;
}

/*
 * repl_init_cache_log_buffer() - Initialize the cache log buffer area of
 *                                a cache page buffer
 *   return: NO_ERROR or REPL_AGENT_MEMORY_ERROR
 *   cache_pb : cache page buffer pointer
 *   slb_cnt : the # of cache log buffers per cache page buffer
 *   slb_size : size of CACHE_LOG_BUFFER
 *   def_buf_size: default size of cache log buffer
 *
 * Note:
 *         : allocate the cache page buffer area
 *         : the size of page buffer area is determined after reading the
 *           log header, so we split the "initialize" and "allocate" phase.
 *
 * NOTE
 *     called by RECV thread  of repl_agent
 *        <- repl_tr_log_recv
 */
int
repl_init_cache_log_buffer (REPL_CACHE_PB * cache_pb, int slb_cnt,
			    int slb_size, int def_buf_size)
{
  int error = NO_ERROR;

  error = repl_expand_cache_log_buffer (cache_pb, slb_cnt, slb_size,
					def_buf_size);

  cache_pb->hash_table =
    mht_create ("Repl agent cache log buffer hash table",
		cache_pb->num_buffers * 8, repl_ag_pid_hash,
		repl_ag_pid_hash_cmpeq);
  if (cache_pb->hash_table == NULL)
    {
      error = REPL_AGENT_MEMORY_ERROR;
    }

  return error;
}

/*
 * repl_expand_cache_log_buffer() - expand cache log buffer
 *   return: NO_ERROR or ER_FAILED
 *   cache_pb : cache page buffer pointer
 *   slb_cnt : the # of new cache log buffers (expansion) or -1.
 *   slb_size : size of CACHE_LOG_BUFFER
 *   def_buf_size: default size of cache log buffer
 *
 * Note:
 *         : Expand the cache log buffer pool with the given number of buffers.
 *         : If a zero or a negative value is given, the function expands
 *           the cache buffer pool with a default porcentage of the currently
 *           size.
 */
static int
repl_expand_cache_log_buffer (REPL_CACHE_PB * cache_pb, int slb_cnt,
			      int slb_size, int def_buf_size)
{
  int i;
  int total_buffers;
  int size;
  int bufid;
  REPL_CACHE_BUFFER_AREA *area = NULL;
  int error = NO_ERROR;

  if (slb_cnt <= 0)
    {
      if (cache_pb->num_buffers > 0)
	{
	  slb_cnt = ((cache_pb->num_buffers > 100)
		     ? (int) ((float) cache_pb->num_buffers * 0.10 + 0.9)
		     : (int) ((float) cache_pb->num_buffers * 0.20 + 0.9));
	}
      else
	{
	  slb_cnt = def_buf_size;
	}
    }

  while (slb_cnt > (int) REPL_MAX_NUM_CONTIGUOUS_BUFFERS (slb_size))
    {
      if ((error = repl_expand_cache_log_buffer (cache_pb,
						 REPL_MAX_NUM_CONTIGUOUS_BUFFERS
						 (slb_size), slb_size,
						 def_buf_size)) != NO_ERROR)
	{
	  return error;
	}
      slb_cnt -= REPL_MAX_NUM_CONTIGUOUS_BUFFERS (slb_size);
    }

  if (slb_cnt > 0)
    {
      total_buffers = cache_pb->num_buffers + slb_cnt;

      cache_pb->log_buffer = realloc (cache_pb->log_buffer,
				      total_buffers *
				      SSIZEOF (REPL_CACHE_LOG_BUFFER *));
      REPL_CHECK_ERR_NULL (REPL_FILE_AGENT, REPL_AGENT_MEMORY_ERROR,
			   cache_pb->log_buffer);

      size = ((slb_cnt * slb_size) + SSIZEOF (REPL_CACHE_BUFFER_AREA));
      area = malloc (size);
      REPL_CHECK_ERR_NULL (REPL_FILE_AGENT, REPL_AGENT_MEMORY_ERROR, area);
      memset (area, 0, size);
      area->buffer_area = ((REPL_CACHE_LOG_BUFFER *) ((char *) area
						      +
						      SSIZEOF
						      (REPL_CACHE_BUFFER_AREA)));
      area->next = cache_pb->buffer_area;
      for (i = 0, bufid = cache_pb->num_buffers; i < slb_cnt; bufid++, i++)
	{
	  cache_pb->log_buffer[bufid]
	    =
	    (REPL_CACHE_LOG_BUFFER *) ((char *) area->buffer_area +
				       slb_size * i);
	}
      cache_pb->buffer_area = area;
      cache_pb->num_buffers = total_buffers;
    }

  return NO_ERROR;
}

/*
 * repl_cache_buffer_replace() - find a page to replace and replace the page
 *   return: REPL_CACHE_LOG_BUFFER *
 *    cache_pb : cache page buffer pointer
 *    fd : page file descriptor
 *    phy_pageid : page id to read
 *    io_pagesize : the size of page buffer area
 *    def_buf_size: the number of pages to be expanded
 *
 * Note:
 *         This function
 *           - finds an element of the cache buffer which can be replaced
 *           - replaces the found element of the cache buffer
 &         If all the elements of the cache buffer cannot be replaced,
 *         the cache buffer pool is expanded with an additional area of
 *         contiguous cache buffers. An element of the cache buffer is
 *         selected for replacement by following the CLOCK algorithm
 *         among the unfixed cache buffers in the cache buffer pool.
 *         Replacement is handled
 *         by maintaining a ring of pointers (the clock) to the page cache
 *         buffers. A pointer (the clock hand) is maintained to a slot in
 *         the ring. When a page need to be replaced, the ring pointer is
 *         used as the starting point for the replacement search. In
 *         addition to the fixed/unfixed status, a recently-freed flag is
 *         associated with ever cache buffer. This flag is set whenever
 *         a cache buffer is reed. This flag when set indicates that a page
 *         was referenced during a cycle of the clock. The page to replace
 *         is determined by finding the first unfixed cache buffer whose
 *         recently-freed flag is not set. If a cache buffer is encountered
 *         during the search whose recently-freed flag is set, the flag is
 *         cleared and the clock hand moves to the next buffer. This method
 *         ensures that a page will always survive replacement if it is
 *         referenced during a full cycle of the clock.
 */
static REPL_CACHE_LOG_BUFFER *
repl_cache_buffer_replace (REPL_CACHE_PB * cache_pb, int fd,
			   PAGEID phy_pageid, int io_pagesize,
			   int def_buf_size)
{
  REPL_CACHE_LOG_BUFFER *cache_buffer = NULL;
  int error = NO_ERROR;
  int i, num_unfixed = 1;

  while (num_unfixed > 0)
    {
      num_unfixed = 0;

      for (i = 0; i < cache_pb->num_buffers; i++)
	{
	  cache_buffer = cache_pb->log_buffer[cache_pb->clock_hand];
	  cache_pb->clock_hand =
	    (cache_pb->clock_hand + 1) % cache_pb->num_buffers;

	  if (cache_buffer->fix_count <= 0)
	    {
	      num_unfixed++;
	      if (cache_buffer->recently_freed)
		{
		  cache_buffer->recently_freed = false;
		}
	      else
		{
		  error = repl_io_read (fd,
					(void *)
					&(cache_buffer->log_buffer.logpage),
					phy_pageid, io_pagesize);
		  if (error != NO_ERROR)
		    {
		      REPL_ERR_LOG (REPL_FILE_AGENT, REPL_AGENT_IO_ERROR);
		      return NULL;
		    }
		  cache_buffer->fix_count = 0;
		  return cache_buffer;
		}
	    }
	}
    }

  error = repl_expand_cache_log_buffer (cache_pb, -1,
					SIZEOF_REPL_CACHE_LOG_BUFFER
					(io_pagesize), def_buf_size);
  if (error != NO_ERROR)
    {
      return NULL;
    }

  return repl_cache_buffer_replace (cache_pb, fd, phy_pageid,
				    io_pagesize, def_buf_size);
}

/*
 * repl_ag_pid_hash() - hash a page identifier
 *   return: hash value
 *   key_pid : page id to hash
 *   htsize: Size of hash table
 */
static unsigned int
repl_ag_pid_hash (const void *key_pid, unsigned int htsize)
{
  const PAGEID *pid = (PAGEID *) key_pid;

  return (*pid) % htsize;

}

/*
 * repl_pid_hash_cmpeq() - Compare two pageid keys for hashing.
 *   return: int (key_pid1 == ey_vpid2 ?)
 *   key_pid1: First key
 *   key_pid2: Second key
 */
static int
repl_ag_pid_hash_cmpeq (const void *key_pid1, const void *key_pid2)
{
  const PAGEID *pid1 = key_pid1;
  const PAGEID *pid2 = key_pid2;

  return ((pid1 == pid2) || (*pid1 == *pid2));

}

/*
 * repl_ag_add_master_info() - create a master database info, initialize it,
 *                             add it to the list
 *   return: NO_ERROR or REPL_AGENT_MEMORY_ERROR
 *   idx : index of master info array
 *
 * Note:
 *     called by MAIN thread
 */
static int
repl_ag_add_master_info (int idx)
{
  int i;
  int error = NO_ERROR;

  /* allocate the memory for the master info array */
  if (mInfo == NULL)
    {
      mInfo = malloc (MAX_NUM_OF_MASTERS * sizeof (MASTER_INFO *));
      REPL_CHECK_ERR_NULL (REPL_FILE_AGENT, REPL_AGENT_MEMORY_ERROR, mInfo);
      for (i = 0; i < MAX_NUM_OF_MASTERS; i++)
	mInfo[i] = NULL;
    }

  mInfo[idx] = (MASTER_INFO *) malloc (sizeof (MASTER_INFO));
  REPL_CHECK_ERR_NULL (REPL_FILE_AGENT, REPL_AGENT_MEMORY_ERROR, mInfo[idx]);

  if ((mInfo[idx]->conn.resp_buffer =
       (char *) malloc (REPL_DEF_LOG_PAGE_SIZE + COMM_RESP_BUF_SIZE)) == NULL)
    {
      free_and_init (mInfo[idx]);
      REPL_ERR_RETURN (REPL_FILE_AGENT, REPL_AGENT_MEMORY_ERROR);
    }

  mInfo[idx]->io_pagesize = REPL_DEF_LOG_PAGE_SIZE;

  mInfo[idx]->copy_log.start_pageid = 0;
  mInfo[idx]->copy_log.first_pageid = 0;
  mInfo[idx]->copy_log.last_pageid = 0;

  mInfo[idx]->pb = repl_init_pb ();
  REPL_CHECK_ERR_NULL (REPL_FILE_AGENT, REPL_AGENT_MEMORY_ERROR,
		       mInfo[idx]->pb);

  mInfo[idx]->cache_pb = repl_init_cache_pb ();
  REPL_CHECK_ERR_NULL (REPL_FILE_AGENT, REPL_AGENT_MEMORY_ERROR,
		       mInfo[idx]->cache_pb);

  return error;
}

/*
 * repl_ag_clear_master_info() - clear all master database info
 *   return: none
 *   dbid : the target database id of master
 *
 * Note:
 *     called by MAIN thread
 */
static void
repl_ag_clear_master_info (int dbid)
{
  MASTER_INFO *minfo;
  REPL_CACHE_BUFFER_AREA *buf_area = NULL;
  REPL_CACHE_BUFFER_AREA *tmp = NULL;
  int i;

  if (mInfo == NULL)
    return;
  minfo = mInfo[dbid];
  if (minfo == NULL)
    return;

  if (minfo->pb)
    {
      for (i = 0; i < minfo->log_buffer_size; i++)
	if (minfo->pb->log_buffer && minfo->pb->log_buffer[i])
	  free_and_init (minfo->pb->log_buffer[i]);

      if (minfo->pb->log_buffer)
	free_and_init (minfo->pb->log_buffer);
      if (minfo->pb->log_hdr_buffer)
	free_and_init (minfo->pb->log_hdr_buffer);
      if (minfo->conn.resp_buffer)
	free_and_init (minfo->conn.resp_buffer);

      free_and_init (minfo->pb);

      /* hash table free */
      if (minfo->cache_pb->hash_table)
	{
	  mht_destroy (minfo->cache_pb->hash_table);
	  minfo->cache_pb->hash_table = NULL;
	}

      /* cache page buffers */
      if (minfo->cache_pb->log_buffer)
	free_and_init (minfo->cache_pb->log_buffer);

      /* buffer area free */
      buf_area = minfo->cache_pb->buffer_area;
      while (buf_area)
	{
	  tmp = buf_area;
	  buf_area = buf_area->next;
	  free_and_init (tmp);
	}
      minfo->cache_pb->buffer_area = NULL;

      free_and_init (minfo->cache_pb);
    }

  free_and_init (minfo);
  mInfo[dbid] = NULL;
}

/*
 * repl_ag_clear_master_info_all()
 *   return: none
 */
static void
repl_ag_clear_master_info_all ()
{
  int i;

  for (i = 0; i < MAX_NUM_OF_MASTERS; i++)
    {
      repl_ag_clear_master_info (i);
    }
  if (mInfo)
    {
      free_and_init (mInfo);
      mInfo = NULL;
    }
}

/*
 * repl_ag_add_slave_info() - add the slave info
 *   return: NO_ERROR or error code
 *   idx : index of slave info array
 *
 * Note:
 *      After the MAIN thread reads the slave info from the distributor
 *      database, it add the slave info to the array area.
 *
 *     called by MAIN thread
 */
static int
repl_ag_add_slave_info (int idx)
{
  int i;
  int error = NO_ERROR;

  /* allocate the memory for the master info array */
  if (sInfo == NULL)
    {
      sInfo = malloc (MAX_NUM_OF_SLAVES * sizeof (SLAVE_INFO *));
      REPL_CHECK_ERR_NULL (REPL_FILE_AGENT, REPL_AGENT_MEMORY_ERROR, sInfo);
      for (i = 0; i < MAX_NUM_OF_SLAVES; i++)
	sInfo[i] = NULL;
    }

  sInfo[idx] = (SLAVE_INFO *) malloc (sizeof (SLAVE_INFO));
  REPL_CHECK_ERR_NULL (REPL_FILE_AGENT, REPL_AGENT_MEMORY_ERROR, sInfo[idx]);

  sInfo[idx]->dbid = -1;
  sInfo[idx]->log_data = NULL;
  sInfo[idx]->rec_type = NULL;
  sInfo[idx]->m_cnt = 0;
  sInfo[idx]->undo_unzip_ptr = NULL;
  sInfo[idx]->redo_unzip_ptr = NULL;
  sInfo[idx]->old_time = 0;

  for (i = 0; i < MAX_NUM_OF_MASTERS; i++)
    {
      sInfo[idx]->masters[i].m_id = 0;
      sInfo[idx]->masters[i].s_id = 0;
      sInfo[idx]->masters[i].repl_lists = NULL;
      sInfo[idx]->masters[i].repl_cnt = 0;
      sInfo[idx]->masters[i].cur_repl = 0;
      sInfo[idx]->masters[i].status = 0;
      sInfo[idx]->masters[i].commit_head = NULL;
      sInfo[idx]->masters[i].commit_tail = NULL;
      LSA_SET_NULL (&sInfo[idx]->masters[i].final_lsa);
      LSA_SET_NULL (&sInfo[idx]->masters[i].last_committed_lsa);
    }


  return error;
}

/*
 * repl_ag_clear_repl_item() - clear replication item
 *   return: none
 *   repl_list : the target list already applied.
 *
 * Note:
 *       clear the applied list area after processing ..
 *
 *      called by MAIN thread or APPLY thread
 */
void
repl_ag_clear_repl_item (REPL_APPLY * repl_list, bool clear_tran)
{
  REPL_ITEM *repl_item;
  PARSER_VARCHAR *buf;
  PARSER_CONTEXT *parser = NULL;

  repl_item = repl_list->repl_head;
  if (repl_item == NULL && !clear_tran)
    {
      return;
    }

  if (debug_Dump_info & REPL_DEBUG_AGENT_STATUS)
    {
      fprintf (debug_Log_fd, "CLEAR TRAN[%10d] : ", repl_list->tranid);
      parser = parser_create_parser ();
    }

  while (repl_item != NULL)
    {
      repl_list->repl_head = repl_item->next;
      if (debug_Dump_info & REPL_DEBUG_AGENT_STATUS)
	{
	  if (parser != NULL)
	    {
	      buf = describe_value (parser, NULL, &repl_item->key);

	      fprintf (debug_Log_fd, "[%s](%d,%d), ",
		       (const char *) pt_get_varchar_bytes (buf),
		       repl_item->lsa.pageid, repl_item->lsa.offset);
	    }
	  else
	    {
	      fprintf (debug_Log_fd, "[](%d,%d), ",
		       repl_item->lsa.pageid, repl_item->lsa.offset);
	    }
	}
      if (repl_item->class_name != NULL)
	{
	  db_private_free_and_init (NULL, repl_item->class_name);
	  pr_clear_value (&repl_item->key);
	}
      free_and_init (repl_item);
      repl_item = repl_list->repl_head;
    }
  repl_list->repl_tail = NULL;

  if (parser != NULL)
    {
      parser_free_parser (parser);
    }

  if (clear_tran)
    {
      repl_list->tranid = 0;
      LSA_SET_NULL (&repl_list->start_lsa);
    }

  if (debug_Dump_info & REPL_DEBUG_AGENT_STATUS)
    {
      fprintf (debug_Log_fd, "END\n");
      fflush (debug_Log_fd);
    }
}

/*
 * repl_ag_clear_repl_item_by_tranid() - clear replication item using tranid
 *   return: none
 *   sinfo: slave info
 *   tranid: transaction id
 *   idx: index of MASTER_MAP
 *
 * Note:
 *       clear the applied list area after processing ..
 *       When we meet the LOG_ABORT_TOPOPE or LOG_ABORT record,
 *       we have to clear the replication items of the target transaction.
 *       In case of LOG_ABORT_TOPOPE, the apply list should be preserved
 *       for the later use (so call repl_ag_clear_repl_item() using
 *       false as the second argument).
 *
 *      called by APPLY thread
 */
void
repl_ag_clear_repl_item_by_tranid (SLAVE_INFO * sinfo, int tranid, int idx,
				   bool clear_tran)
{
  REPL_APPLY *repl_list;
  REPL_COMMIT *commit;
  REPL_COMMIT *tmp;
  REPL_COMMIT dumy;

  repl_list = repl_ag_find_apply_list (sinfo, tranid, idx);
  if (repl_list)
    {
      repl_ag_clear_repl_item (repl_list, clear_tran);
    }

  dumy.next = sinfo->masters[idx].commit_head;
  commit = &dumy;
  while (commit->next)
    {
      if (commit->next->tranid == tranid)
	{
	  tmp = commit->next;
	  if (tmp->next == NULL)
	    {
	      sinfo->masters[idx].commit_tail = commit;
	    }
	  commit->next = tmp->next;
	  free_and_init (tmp);
	}
      else
	{
	  commit = commit->next;
	}
    }
  sinfo->masters[idx].commit_head = dumy.next;
  if (sinfo->masters[idx].commit_head == NULL)
    {
      sinfo->masters[idx].commit_tail = NULL;
    }
}

/*
 * repl_ag_clear_slave_info() - clear slave database info
 *   return: none
 *   sinfo : pointer to the slave info to be cleared
 *
 * Note:
 *     called by MAIN thread
 */
static void
repl_ag_clear_slave_info (SLAVE_INFO * sinfo)
{
  int i, j, k;

  for (j = 0; j < MAX_NUM_OF_MASTERS; j++)
    {
      if (sinfo->masters[j].repl_lists)
	{
	  for (i = 0; i < sinfo->masters[j].repl_cnt; i++)
	    {
	      repl_ag_clear_repl_item (sinfo->masters[j].repl_lists[i], true);
	      free_and_init (sinfo->masters[j].repl_lists[i]);
	    }
	  free_and_init (sinfo->masters[j].repl_lists);
	  sinfo->masters[j].repl_lists = NULL;
	}
      if (sinfo->masters[j].class_list)
	{
	  for (k = 0; k < sinfo->masters[j].class_count; k++)
	    free_and_init (sinfo->masters[j].class_list[k].class_name);
	  free_and_init (sinfo->masters[j].class_list);
	}
    }

  if (sinfo->log_data)
    free_and_init (sinfo->log_data);

  if (sinfo->rec_type)
    free_and_init (sinfo->rec_type);

  if (sinfo->undo_unzip_ptr)
    log_zip_free (sinfo->undo_unzip_ptr);

  if (sinfo->redo_unzip_ptr)
    log_zip_free (sinfo->redo_unzip_ptr);

  free_and_init (sinfo);
}

/*
 * repl_ag_clear_slave_info_all() - clear slave database info
 *   return: none
 *
 * Note:
 *     called by MAIN thread
 */
static void
repl_ag_clear_slave_info_all ()
{
  int i;

  if (sInfo == NULL)
    return;
  for (i = 0; i < MAX_NUM_OF_SLAVES; i++)
    {
      if (sInfo[i])
	{
	  repl_ag_clear_slave_info (sInfo[i]);
	  sInfo[i] = NULL;
	}
    }

  free_and_init (sInfo);
  sInfo = NULL;
}

/*
 * repl_ag_open_env_file() - open the replication trail file
 *   return: the result file descriptor
 *   repl_log_path: full path of trail file
 *
 * Note:
 *     called by MAIN thread
 */
static int
repl_ag_open_env_file (char *repl_log_path)
{
  umask (S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
  return (repl_io_open (repl_log_path, O_RDWR | O_CREAT, FILE_CREATE_MODE));

}

/*
 * repl_ag_shutdown() - Shutdown process of repl_agent
 *   return: none
 *
 * Note:
 *     all resources are released
 *
 *     called by MAIN thread or repl_shutdown_thread
 */
static void
repl_ag_shutdown ()
{
  repl_ag_clear_master_info_all ();
  repl_ag_clear_slave_info_all ();
  repl_error_flush (err_Log_fp, false);
  close (repl_Pipe_to_master);

  if (err_Log_fp && err_Log_fp != stdout && err_Log_fp)
    {
      fclose (err_Log_fp);
      err_Log_fp = NULL;
    }

  if (perf_Log_fp && perf_Log_fp != stdout && perf_Log_fp)
    {
      fclose (perf_Log_fp);
      perf_Log_fp = NULL;
    }

  if (debug_Log_fd && debug_Log_fd != stdout && debug_Log_fd)
    {
      fclose (debug_Log_fd);
      debug_Log_fd = NULL;
    }

  if (history_fp)
    {
      fclose (history_fp);
      history_fp = NULL;
    }

  if (trail_File_vdes != NULL_VOLDES)
    {
      close (trail_File_vdes);
      trail_File_vdes = NULL_VOLDES;
    }
}

/*
 * repl_ag_get_env() - set the parameters of repl_agent
 *   return: NO_ERROR or error code
 *   result : we fetch the parameters from the distributor db.
 *
 * Note:
 *         parameter_name  - trail_log
 *         parameter_value - /home1/cubrid/qadb   (eg)
 *
 *         set the replication trail log file.
 *         The repl_agent has to find out which LSA has been applied to the
 *         slave.
 *
 *         If we have 2 masters and 2 slaves, each master should be replicated
 *         for each slave. Then we have to maintain 4 trail log...
 *           (S1, M1)
 *           (S1, M2)
 *           (S2, M1)
 *           (S2, M2)
 *
 *     called by MAIN thread
 */
static int
repl_ag_get_env (DB_QUERY_RESULT * result)
{
  DB_VALUE *value;
  int col_cnt = db_query_column_count (result);
  int i;
  char *env_name, *env_value;
  char *file_path;
  int error = NO_ERROR;

  if (col_cnt < 2)
    {
      REPL_ERR_RETURN (REPL_FILE_AGENT, REPL_AGENT_INTERNAL_ERROR);
    }

  value = (DB_VALUE *) malloc (sizeof (DB_VALUE) * col_cnt);
  REPL_CHECK_ERR_NULL (REPL_FILE_AGENT, REPL_AGENT_MEMORY_ERROR, value);

  error = db_query_get_tuple_valuelist (result, col_cnt, value);
  REPL_CHECK_ERR_ERROR_WITH_FREE (REPL_FILE_AGENT, REPL_AGENT_INTERNAL_ERROR,
				  value);

  env_name = DB_GET_STRING (&value[0]);
  env_value = DB_GET_STRING (&value[1]);

  if (env_name == NULL || env_value == NULL)
    {
      free_and_init (value);
      return REPL_AGENT_INTERNAL_ERROR;
    }

  file_path =
    (char *) malloc (strlen (env_value) + strlen (dist_Dbname) + 10);
  REPL_CHECK_ERR_NULL_WITH_FREE (REPL_FILE_AGENT, REPL_AGENT_MEMORY_ERROR,
				 file_path, value);

  if (env_name != NULL && strcmp (env_name, "trail_log") == 0)
    {
      dist_LogPath = (char *) malloc (strlen (env_value) + 1);
      if (dist_LogPath == NULL)
	{
	  repl_error_push (REPL_FILE_AGENT, __LINE__, REPL_AGENT_MEMORY_ERROR,
			   NULL);
	  free_and_init (value);
	  free_and_init (file_path);
	  return REPL_AGENT_MEMORY_ERROR;
	}

      strcpy (dist_LogPath, env_value);
      sprintf (file_path, "%s/%s.trail", env_value, dist_Dbname);
      trail_File_vdes = repl_ag_open_env_file (file_path);
      if (trail_File_vdes == NULL_VOLDES)
	{
	  REPL_ERR_LOG_ONE_ARG (REPL_FILE_AGENT,
				REPL_AGENT_CANT_READ_TRAIL_LOG, file_path);
	}

      sprintf (file_path, "%s/%s.hist", env_value, dist_Dbname);
      history_fp = fopen (file_path, "a");
      if (history_fp == NULL)
	{
	  repl_error_push (REPL_FILE_AGENT, __LINE__, REPL_AGENT_IO_ERROR,
			   NULL);
	  free_and_init (value);
	  free_and_init (file_path);
	  return REPL_AGENT_IO_ERROR;
	}

      if (debug_Dump_info != 0)
	{
	  sprintf (file_path, "%s/%s.debug", env_value, dist_Dbname);
	  debug_Log_fd = fopen (file_path, "a");
	  if (debug_Log_fd == NULL)
	    {
	      debug_Log_fd = stdout;
	    }
	}
    }
  else if (env_name != NULL && strcmp (env_name, "error_log") == 0)
    {
      sprintf (file_path, "%s/%s.err", env_value, dist_Dbname);
      err_Log_fp = fopen (file_path, "a");
      if (err_Log_fp == NULL)
	{
	  err_Log_fp = stdout;
	}
    }
  else if (env_name != NULL && strcmp (env_name, "agent_port") == 0)
    {
      agent_status_port = atoi (env_value);
      if (agent_status_port <= 0)
	{
	  agent_status_port = 33333;
	}
    }
  else if (env_name != NULL && strcmp (env_name, "perf_log_size") == 0)
    {
      perf_Log_size = atoi (env_value);
      if (perf_Log_size <= 0)
	{
	  perf_Log_size = 10000;
	}
    }
  else if (env_name != NULL
	   && strcmp (env_name, "commit_interval_msecs") == 0)
    {
      perf_Commit_msec = atoi (env_value);
      if (perf_Commit_msec < 0)
	{
	  perf_Commit_msec = 10000;
	}
    }
  else if (env_name != NULL && strcmp (env_name, "repl_agent_max_size") == 0)
    {
      agent_Max_size = atoi (env_value);
      if (agent_Max_size < 0)
	{
	  agent_Max_size = 50;
	}
    }
  else if (env_name != NULL && strcmp (env_name, "retry_connect") == 0)
    {
      if (env_value[0] == 'y')
	{
	  retry_Connect = true;
	}
    }

  for (i = 0; i < col_cnt; i++)
    {
      db_value_clear (&value[i]);
    }

  free_and_init (value);
  free_and_init (file_path);

  return error;
}

/*
 * repl_ag_get_master() - set the master info
 *   return: NO_ERROR or error code
 *   result : the result of query of getting master db info
 *
 * Note:
 *     get the master database info from the distributor db
 *
 *     called by MAIN thread
 */
static int
repl_ag_get_master (DB_QUERY_RESULT * result)
{
  DB_VALUE *value;
  unsigned int idx = repl_Master_num;
  int i;
  int col_cnt = db_query_column_count (result);
  int error = NO_ERROR;
  char *tmp_str1, *tmp_str2, *tmp_str3;

  if (col_cnt < 11)
    {
      REPL_ERR_RETURN (REPL_FILE_AGENT, REPL_AGENT_INTERNAL_ERROR);
    }

  if (repl_Master_num >= MAX_NUM_OF_MASTERS)
    {
      REPL_ERR_RETURN (REPL_FILE_AGENT, REPL_AGENT_TOO_MANY_MASTERS);
    }

  value = (DB_VALUE *) malloc (sizeof (DB_VALUE) * col_cnt);
  REPL_CHECK_ERR_NULL (REPL_FILE_AGENT, REPL_AGENT_MEMORY_ERROR, value);

  error = db_query_get_tuple_valuelist (result, col_cnt, value);
  REPL_CHECK_ERR_ERROR_WITH_FREE (REPL_FILE_AGENT, REPL_AGENT_INTERNAL_ERROR,
				  value);

  error = repl_ag_add_master_info (idx);
  if (error != NO_ERROR)
    {
      for (i = 0; i < col_cnt; i++)
	{
	  db_value_clear (&value[i]);
	}
      free_and_init (value);
      return error;
    }

  /* set the ID and agent id
   * dbid : the unique number which identify the master db within
   *        distributor system.
   * agent_id : the unique number which identify the repl_agent process
   *            within the repl_server.
   */
  mInfo[idx]->agentid = -1;
  mInfo[idx]->dbid = DB_GET_INTEGER (&value[0]);

  /* set the connection info of the master db */
  tmp_str1 = DB_GET_STRING (&value[1]);
  tmp_str2 = DB_GET_STRING (&value[2]);
  tmp_str3 = DB_GET_STRING (&value[4]);

  if (tmp_str1 == NULL || tmp_str2 == NULL || tmp_str3 == NULL)
    {
      for (i = 0; i < col_cnt; i++)
	{
	  db_value_clear (&value[i]);
	}
      free_and_init (value);
      return REPL_AGENT_INTERNAL_ERROR;
    }

  strcpy (mInfo[idx]->conn.dbname, tmp_str1);
  strcpy (mInfo[idx]->conn.master_IP, tmp_str2);
  mInfo[idx]->conn.portnum = DB_GET_INTEGER (&value[3]);
  memset (mInfo[idx]->conn.req_buf, 0, COMM_REQ_BUF_SIZE);

  /* set the copy log info */
  snprintf (mInfo[idx]->copylog_path, FILE_PATH_LENGTH - 1, "%s/%s.copy",
	    tmp_str3, mInfo[idx]->conn.dbname);

  mInfo[idx]->copy_log.start_pageid = DB_GET_INTEGER (&value[5]);
  mInfo[idx]->copy_log.first_pageid = DB_GET_INTEGER (&value[6]);
  mInfo[idx]->copy_log.last_pageid = DB_GET_INTEGER (&value[7]);

  /* set the parameters */
  mInfo[idx]->log_buffer_size = DB_GET_INTEGER (&value[8]);
  mInfo[idx]->cache_buffer_size = DB_GET_INTEGER (&value[9]);
  mInfo[idx]->copylog_size = DB_GET_INTEGER (&value[10]);
  mInfo[idx]->is_end_of_record = false;

  repl_Master_num++;

  for (i = 0; i < col_cnt; i++)
    {
      db_value_clear (&value[i]);
    }
  free_and_init (value);

  return NO_ERROR;
}

/*
 * repl_ag_init_repl_lists() - Initialize the replication lists
 *   return: NO_ERROR or error code
 *   slave_info : the target slave info
 *   m_idx : the target master index
 *   need_realloc : yes when realloc
 *
 * Note:
 *         repl_lists is an array of replication items to be applied.
 *         We maintain repl_lists for a transaction.
 *         This function initialize the repl_list.
 *
 *     called by MAIN thread(malloc) or APPLY thread (realloc)
 */
static int
repl_ag_init_repl_lists (SLAVE_INFO * sinfo, int idx, bool need_realloc)
{
  int i, j;
  int error = NO_ERROR;

  if (need_realloc == false)
    {
      sinfo->masters[idx].repl_lists
	= malloc (sizeof (REPL_APPLY *) * REPL_LIST_CNT);
      sinfo->masters[idx].repl_cnt = REPL_LIST_CNT;
      sinfo->masters[idx].cur_repl = 0;
      j = 0;
    }
  else
    {
      sinfo->masters[idx].repl_lists
	= realloc (sinfo->masters[idx].repl_lists,
		   SSIZEOF (REPL_APPLY *) *
		   (REPL_LIST_CNT + sinfo->masters[idx].repl_cnt));
      j = sinfo->masters[idx].repl_cnt;
      sinfo->masters[idx].repl_cnt += REPL_LIST_CNT;
    }

  REPL_CHECK_ERR_NULL (REPL_FILE_AGENT, REPL_AGENT_MEMORY_ERROR,
		       sinfo->masters[idx].repl_lists);

  for (i = j; i < sinfo->masters[idx].repl_cnt; i++)
    {
      sinfo->masters[idx].repl_lists[i] = malloc (sizeof (REPL_APPLY));
      if (sinfo->masters[idx].repl_lists[i] == NULL)
	{
	  REPL_ERR_LOG (REPL_FILE_AGENT, REPL_AGENT_MEMORY_ERROR);
	  break;
	}
      sinfo->masters[idx].repl_lists[i]->tranid = 0;
      LSA_SET_NULL (&sinfo->masters[idx].repl_lists[i]->start_lsa);
      sinfo->masters[idx].repl_lists[i]->repl_head = NULL;
      sinfo->masters[idx].repl_lists[i]->repl_tail = NULL;
    }

  if (error != NO_ERROR)
    {
      for (j = 0; j < i; j++)
	free_and_init (sinfo->masters[idx].repl_lists[i]);
      free_and_init (sinfo->masters[idx].repl_lists);
      return error;
    }
  return error;
}

/*
 * repl_ag_get_repl_group() - get the replication group
 *   return: NO_ERROR or error code
 *   sinfo : slave info
 *   m_idx : master map index
 *
 * Note:
 *     Read the class name to be replicated for the target master from the
 *     distributor database, and constuct the hash table.
 *
 * NOTE
 *     called by MAIN thread
 */
static int
repl_ag_get_repl_group (SLAVE_INFO * sinfo, int m_idx)
{
  int error = NO_ERROR;
  char sql_stmt[1000];
  DB_QUERY_RESULT *result = NULL;
  DB_QUERY_ERROR error_stats;
  DB_VALUE value;
  int result_count = 0, i, j;
  char *tmp_str;

  sprintf (sql_stmt,
	   "select class_name, start_pageid, start_offset from repl_group where master_dbid = %d and slave_dbid = %d order by class_name",
	   sinfo->masters[m_idx].m_id, sinfo->masters[m_idx].s_id);

  sinfo->masters[m_idx].class_count = 0;

  if (db_execute (sql_stmt, &result, &error_stats) >= 0)
    {
      result_count = db_query_tuple_count (result);
      if (result_count == 0)
	{
	  sinfo->masters[m_idx].class_list = NULL;
	}
      else
	{
	  sinfo->masters[m_idx].class_list
	    = (REPL_GROUP_INFO *) malloc (sizeof (REPL_GROUP_INFO)
					  * result_count);
	  if (sinfo->masters[m_idx].class_list == NULL)
	    {
	      db_query_end (result);
	      return REPL_AGENT_GET_PARARMETER_FAIL;
	    }

	  for (i = 0; i < result_count; i++)
	    {
	      sinfo->masters[m_idx].class_list[i].class_name
		= (char *) malloc (DB_MAX_IDENTIFIER_LENGTH);
	      if (sinfo->masters[m_idx].class_list[i].class_name == NULL)
		{
		  for (j = 0; j < i; j++)
		    {
		      free_and_init (sinfo->masters[m_idx].class_list[j].
				     class_name);
		    }
		  free_and_init (sinfo->masters[m_idx].class_list);

		  db_query_end (result);
		  return REPL_AGENT_GET_PARARMETER_FAIL;
		}
	    }
	  sinfo->masters[m_idx].class_count = result_count;
	}

      if (result_count > 0)
	{
	  result_count = 0;
	  error = db_query_first_tuple (result);
	  if (error == NO_ERROR)
	    {
	      error = db_query_get_tuple_value (result, 0, &value);
	      tmp_str = DB_GET_STRING (&value);
	      if (tmp_str)
		{
		  strcpy (sinfo->masters[m_idx].class_list[result_count].
			  class_name, tmp_str);
		}
	      db_value_clear (&value);

	      error = db_query_get_tuple_value (result, 1, &value);
	      if (DB_IS_NULL (&value))
		{
		  sinfo->masters[m_idx].class_list[result_count].
		    start_lsa.pageid = -1;
		}
	      else
		{
		  sinfo->masters[m_idx].class_list[result_count].
		    start_lsa.pageid = DB_GET_INT (&value);
		}

	      error = db_query_get_tuple_value (result, 2, &value);
	      if (DB_IS_NULL (&value))
		{
		  sinfo->masters[m_idx].class_list[result_count].
		    start_lsa.offset = -1;
		}
	      else
		{
		  sinfo->masters[m_idx].class_list[result_count].
		    start_lsa.offset = DB_GET_INT (&value);
		}

	      result_count++;
	    }
	  while (error == NO_ERROR)
	    {
	      if (db_query_next_tuple (result) != DB_CURSOR_SUCCESS)
		{
		  break;
		}
	      error = db_query_get_tuple_value (result, 0, &value);
	      tmp_str = DB_GET_STRING (&value);
	      if (tmp_str)
		{
		  strcpy (sinfo->masters[m_idx].class_list[result_count].
			  class_name, tmp_str);
		}
	      db_value_clear (&value);

	      error = db_query_get_tuple_value (result, 1, &value);
	      if (DB_IS_NULL (&value))
		{
		  sinfo->masters[m_idx].class_list[result_count].
		    start_lsa.pageid = -1;
		}
	      else
		{
		  sinfo->masters[m_idx].class_list[result_count].
		    start_lsa.pageid = DB_GET_INT (&value);
		}

	      error = db_query_get_tuple_value (result, 2, &value);
	      if (DB_IS_NULL (&value))
		{
		  sinfo->masters[m_idx].class_list[result_count].
		    start_lsa.offset = -1;
		}
	      else
		{
		  sinfo->masters[m_idx].class_list[result_count].
		    start_lsa.offset = DB_GET_INT (&value);
		}

	      result_count++;
	    }
	  if (error != NO_ERROR)
	    {
	      db_query_end (result);
	      return REPL_AGENT_GET_PARARMETER_FAIL;
	    }
	}
      db_query_end (result);
    }
  else
    {
      return REPL_AGENT_GET_PARARMETER_FAIL;
    }
  return error;

}

#if 0
static int
repl_ag_dump_group_class_list (SLAVE_INFO * sinfo, int m_idx)
{
  int i, class_count;

  class_count = sinfo->masters[m_idx].class_count;
  for (i = 0; i < class_count; i++)
    {
      printf ("ClassName : %s, LSA(%d,%d)\n",
	      sinfo->masters[m_idx].class_list[i].class_name,
	      sinfo->masters[m_idx].class_list[i].start_lsa.pageid,
	      sinfo->masters[m_idx].class_list[i].start_lsa.offset);
    }

  return NO_ERROR;
}
#endif

static char *
repl_ag_get_class_from_repl_group (SLAVE_INFO * sinfo,
				   int m_idx, char *class_name, LOG_LSA * lsa)
{
  int i, result;

  if (sinfo->masters[m_idx].class_list == NULL
      || sinfo->masters[m_idx].class_count == 0 || class_name == NULL)
    {
      return NULL;
    }

  for (i = 0; i < sinfo->masters[m_idx].class_count; i++)
    {
      result = strcmp (sinfo->masters[m_idx].class_list[i].class_name,
		       class_name);
      if (result == 0)
	{
	  if (LSA_LE (lsa, &sinfo->masters[m_idx].class_list[i].start_lsa))
	    {
	      return NULL;
	    }
	  return sinfo->masters[m_idx].class_list[i].class_name;
	}
    }
  return NULL;
}

#if 0
static int
repl_ag_append_class_to_repl_group (SLAVE_INFO * sinfo,
				    int m_idx, char *class_name)
{
  int size;

  size = sinfo->masters[m_idx].class_count;

  sinfo->masters[m_idx].class_list
    = (REPL_GROUP_INFO *) realloc (sinfo->masters[m_idx].class_list,
				   SSIZEOF (REPL_GROUP_INFO) * (size + 1));
  if (sinfo->masters[m_idx].class_list == NULL)
    return ER_FAILED;

  sinfo->masters[m_idx].class_list[size].class_name = malloc (DB_NAME_LENGTH);
  if (sinfo->masters[m_idx].class_list[size].class_name == NULL)
    return ER_FAILED;

  strcpy (sinfo->masters[m_idx].class_list[size].class_name, class_name);
  LSA_SET_NULL (&sinfo->masters[m_idx].class_list[size].start_lsa);

  sinfo->masters[m_idx].class_count++;

  return NO_ERROR;
}
#endif

int
repl_ag_append_partition_class_to_repl_group (SLAVE_INFO * sinfo, int m_idx)
{
  int error = NO_ERROR;
  char sql_stmt[1000];
  DB_QUERY_RESULT *result = NULL;
  DB_QUERY_ERROR error_stats;
  DB_VALUE value;
  int result_count = 0, i;
  int sum = 0, class_count, pos;
  int idx;
  char *tmp_str;

  class_count = sinfo->masters[m_idx].class_count;
  for (i = 0; i < class_count; i++)
    {
      sprintf (sql_stmt,
	       "select count(*) from db_partition where class_name = '%s'",
	       sinfo->masters[m_idx].class_list[i].class_name);
      if (db_execute (sql_stmt, &result, &error_stats) > 0)
	{
	  error = db_query_first_tuple (result);
	  if (error != NO_ERROR)
	    {
	      db_query_end (result);
	      return REPL_AGENT_GET_PARARMETER_FAIL;
	    }
	  error = db_query_get_tuple_value (result, 0, &value);
	  if (error != NO_ERROR)
	    {
	      db_query_end (result);
	      return REPL_AGENT_GET_PARARMETER_FAIL;
	    }
	  sum += DB_GET_INT (&value);
	  db_value_clear (&value);
	}
      db_query_end (result);
    }

  if (sum <= 0)
    {
      return NO_ERROR;
    }

  sinfo->masters[m_idx].class_list
    = (REPL_GROUP_INFO *) realloc (sinfo->masters[m_idx].class_list,
				   SSIZEOF (REPL_GROUP_INFO) *
				   (class_count + sum));
  if (sinfo->masters[m_idx].class_list == NULL)
    {
      return REPL_AGENT_GET_PARARMETER_FAIL;
    }

  pos = 0;
  for (i = 0; i < class_count; i++)
    {
      sprintf (sql_stmt,
	       "select partition_class_name from db_partition where class_name = '%s'",
	       sinfo->masters[m_idx].class_list[i].class_name);
      result_count = db_execute (sql_stmt, &result, &error_stats);
      if (result_count > 0)
	{
	  result_count = 0;
	  error = db_query_first_tuple (result);
	  if (error == NO_ERROR)
	    {
	      idx = class_count + pos;
	      sinfo->masters[m_idx].class_list[idx].class_name =
		(char *) malloc (DB_MAX_IDENTIFIER_LENGTH);
	      if (sinfo->masters[m_idx].class_list[idx].class_name == NULL)
		{
		  db_query_end (result);
		  return REPL_AGENT_GET_PARARMETER_FAIL;
		}

	      error = db_query_get_tuple_value (result, 0, &value);
	      tmp_str = DB_GET_STRING (&value);
	      if (tmp_str != NULL)
		{
		  strcpy (sinfo->masters[m_idx].class_list[idx].class_name,
			  tmp_str);
		}
	      else
		{
		  sinfo->masters[m_idx].class_list[idx].class_name[0] = '\0';
		}
	      db_value_clear (&value);
	      LSA_COPY (&sinfo->masters[m_idx].class_list[idx].start_lsa,
			&sinfo->masters[m_idx].class_list[i].start_lsa);
	      pos++;
	    }
	  while (error == NO_ERROR)
	    {
	      if (db_query_next_tuple (result) != DB_CURSOR_SUCCESS)
		{
		  break;
		}
	      idx = class_count + pos;
	      sinfo->masters[m_idx].class_list[idx].class_name =
		(char *) malloc (DB_MAX_IDENTIFIER_LENGTH);
	      if (sinfo->masters[m_idx].class_list[idx].class_name == NULL)
		{
		  db_query_end (result);
		  return REPL_AGENT_GET_PARARMETER_FAIL;
		}

	      error = db_query_get_tuple_value (result, 0, &value);
	      tmp_str = DB_GET_STRING (&value);
	      if (tmp_str)
		{
		  strcpy (sinfo->masters[m_idx].class_list[idx].class_name,
			  tmp_str);
		}
	      else
		{
		  sinfo->masters[m_idx].class_list[idx].class_name[0] = '\0';
		}
	      db_value_clear (&value);
	      LSA_COPY (&sinfo->masters[m_idx].class_list[idx].start_lsa,
			&sinfo->masters[m_idx].class_list[i].start_lsa);
	      pos++;
	    }
	  if (error != NO_ERROR)
	    {
	      db_query_end (result);
	      return REPL_AGENT_GET_PARARMETER_FAIL;
	    }
	}
      db_query_end (result);
    }
  sinfo->masters[m_idx].class_count = class_count + pos;

  return NO_ERROR;
}

#define REPL_GET_TRAIL_INFO_INT(arg1, arg2, label)                            \
  do {                                                                        \
    error = db_get(mop, arg1, &mid);                                          \
    REPL_CHECK_ERR_ERROR_GOTO(REPL_FILE_AGENT, REPL_AGENT_INVALID_TRAIL_INFO, label); \
    if(DB_IS_NULL(&mid))  arg2 = 0;                                           \
    else arg2 = DB_GET_INTEGER(&mid);                                         \
    db_value_clear(&mid);                                                     \
  } while(0)

#define REPL_GET_TRAIL_INFO_CHAR2BOOL(arg1, arg2, label)                      \
  do {                                                                        \
    error = db_get(mop, arg1, &mid);                                          \
    REPL_CHECK_ERR_ERROR_GOTO(REPL_FILE_AGENT, REPL_AGENT_INVALID_TRAIL_INFO, label); \
    c_bool = DB_GET_CHAR(&mid, &dummy);                                       \
    arg2 = (c_bool && (c_bool[0] == 'y' || c_bool[0] == 'Y')) ? true : false; \
    db_value_clear(&mid);                                                     \
  } while(0)
#define REPL_GET_TRAIL_INFO_CHAR(arg1, arg2, label)                           \
  do {                                                                        \
    error = db_get(mop, arg1, &mid);                                          \
    REPL_CHECK_ERR_ERROR_GOTO(REPL_FILE_AGENT, REPL_AGENT_INVALID_TRAIL_INFO, label); \
    c_bool = DB_GET_CHAR(&mid, &dummy);                                       \
    arg2 = (c_bool) ? c_bool[0] : false;                                      \
    db_value_clear(&mid);                                                     \
  } while(0)

/*
 * repl_ag_get_slave() - Initialize the slave info
 *   return: NO_ERROR or error code
 *   result : we fetch the parameters from the distributor db.
 *
 * Note:
 *     get the slave database info from the distributor db
 *
 *     called by MAIN thread
 */
static int
repl_ag_get_slave (DB_QUERY_RESULT * result)
{
  DB_VALUE *value;
  unsigned int idx = repl_Slave_num;
  int i, m_idx, dummy;
  int col_cnt = db_query_column_count (result);
  DB_SET *masters;
  DB_VALUE mid;
  DB_OBJECT *mop;
  int error = NO_ERROR;
  char *c_bool;
  char sql[1024];
  char *tmp_str1, *tmp_str2, *tmp_str3, *tmp_str4;

  if (col_cnt < 7)
    {
      REPL_ERR_RETURN (REPL_FILE_AGENT, REPL_AGENT_INTERNAL_ERROR);
    }

  if (repl_Slave_num >= MAX_NUM_OF_SLAVES)
    {
      REPL_ERR_RETURN (REPL_FILE_AGENT, REPL_AGENT_TOO_MANY_SLAVES);
    }

  value = malloc (sizeof (DB_VALUE) * col_cnt);
  REPL_CHECK_ERR_NULL (REPL_FILE_AGENT, REPL_AGENT_MEMORY_ERROR, value);

  error = db_query_get_tuple_valuelist (result, col_cnt, value);
  REPL_CHECK_ERR_ERROR_WITH_FREE (REPL_FILE_AGENT, REPL_AGENT_INTERNAL_ERROR,
				  value);

  error = repl_ag_add_slave_info (idx);
  if (error != NO_ERROR)
    {
      goto fail;
    }

  /* set the ID
   * dbid : the unique number which identify the master db within
   *        distributor system.
   */
  sInfo[idx]->dbid = DB_GET_INTEGER (&value[0]);

  tmp_str1 = DB_GET_STRING (&value[1]);
  tmp_str2 = DB_GET_STRING (&value[2]);
  tmp_str3 = DB_GET_STRING (&value[4]);
  tmp_str4 = DB_GET_STRING (&value[5]);

  if (tmp_str1 == NULL || tmp_str2 == NULL || tmp_str3 == NULL
      || tmp_str4 == NULL)
    {
      error = REPL_AGENT_INTERNAL_ERROR;
      goto fail;
    }

  /* set the connection info */
  strcpy (sInfo[idx]->conn.dbname, tmp_str1);
  strcpy (sInfo[idx]->conn.master_IP, tmp_str2);
  sInfo[idx]->conn.portnum = DB_GET_INTEGER (&value[3]);
  strcpy (sInfo[idx]->conn.userid, tmp_str3);
  strcpy (sInfo[idx]->conn.passwd, tmp_str4);

  /* set the trail info
   * trail info : the info which should be maintained per each
   *              master and slave pair.
   *              For example, if
   *              M1 is replicated by S1 and S2,
   *              M2 is replicated by S2..
   *              The trail info should be maintained for
   *              {M1, S1}, {M1, S2}, {M2, S2}
   */
  masters = DB_GET_SET (&value[6]);
  if (masters == NULL)
    {
      repl_error_push (REPL_FILE_AGENT, __LINE__,
		       REPL_AGENT_SLAVE_HAS_NO_MASTER, NULL);
      error = REPL_AGENT_SLAVE_HAS_NO_MASTER;
      goto fail;
    }

  sInfo[idx]->m_cnt = db_set_cardinality (masters);
  if (sInfo[idx]->m_cnt == 0)
    {
      repl_error_push (REPL_FILE_AGENT, __LINE__,
		       REPL_AGENT_SLAVE_HAS_NO_MASTER, NULL);
      error = REPL_AGENT_SLAVE_HAS_NO_MASTER;
      goto fail;
    }

  for (i = 0; i < sInfo[idx]->m_cnt; i++)
    {
      sInfo[idx]->masters[i].s_id = sInfo[idx]->dbid;

      error = db_set_get (masters, i, &mid);
      if (error != NO_ERROR)
	{
	  repl_error_push (REPL_FILE_AGENT, __LINE__,
			   REPL_AGENT_INVALID_TRAIL_INFO, NULL);
	  error = REPL_AGENT_INVALID_TRAIL_INFO;
	  goto fail;
	}

      mop = db_get_object (&mid);
      if (mop == NULL)
	{
	  repl_error_push (REPL_FILE_AGENT, __LINE__,
			   REPL_AGENT_INVALID_TRAIL_INFO, NULL);
	  error = REPL_AGENT_INVALID_TRAIL_INFO;
	  goto fail;
	}
      db_value_clear (&mid);

      REPL_GET_TRAIL_INFO_INT ("master_dbid", sInfo[idx]->masters[i].m_id,
			       fail);

      REPL_GET_TRAIL_INFO_INT ("final_pageid",
			       sInfo[idx]->masters[i].final_lsa.pageid, fail);
      REPL_GET_TRAIL_INFO_INT ("final_offset",
			       sInfo[idx]->masters[i].final_lsa.offset, fail);
      REPL_GET_TRAIL_INFO_INT ("perf_poll_interval",
			       sInfo[idx]->masters[i].perf_poll_interval,
			       fail);
      REPL_GET_TRAIL_INFO_INT ("log_apply_interval",
			       sInfo[idx]->masters[i].log_apply_interval,
			       fail);
      REPL_GET_TRAIL_INFO_INT ("restart_interval",
			       sInfo[idx]->masters[i].restart_interval, fail);

      REPL_GET_TRAIL_INFO_CHAR2BOOL ("all_repl",
				     sInfo[idx]->masters[i].all_repl, fail);
      REPL_GET_TRAIL_INFO_CHAR2BOOL ("index_replication",
				     sInfo[idx]->masters[i].
				     index_replication, fail);
      REPL_GET_TRAIL_INFO_CHAR2BOOL ("for_recovery",
				     sInfo[idx]->masters[i].for_recovery,
				     fail);
      REPL_GET_TRAIL_INFO_CHAR ("status", sInfo[idx]->masters[i].status,
				fail);

      m_idx = repl_ag_get_master_info_index (sInfo[idx]->masters[i].m_id);
      if (mInfo[m_idx]->copy_log.start_pageid < 0 ||	/* first time to start
							 * replication */
	  mInfo[m_idx]->copy_log.start_pageid >
	  sInfo[idx]->masters[i].final_lsa.pageid ||
	  sInfo[idx]->masters[i].status == 'F' ||
	  sInfo[idx]->masters[i].status == 'f')
	{
	  mInfo[m_idx]->copy_log.start_pageid =
	    mInfo[m_idx]->copy_log.first_pageid =
	    mInfo[m_idx]->copy_log.last_pageid =
	    sInfo[idx]->masters[i].final_lsa.pageid;
	  sInfo[idx]->masters[i].total_rows = 0;
	  sprintf (sql,
		   "UPDATE master_info SET start_pageid=%d WHERE dbid=%d;"
		   "UPDATE trail_info SET status='I' WHERE master_dbid=%d and slave_dbid=%d;",
		   mInfo[m_idx]->copy_log.start_pageid,
		   mInfo[m_idx]->dbid, sInfo[idx]->masters[i].m_id,
		   sInfo[idx]->masters[i].s_id);
	  error = repl_ag_update_query_execute (sql);
	  if (error == NO_ERROR)
	    {
	      error = db_commit_transaction ();
	    }
	  if (error == NO_ERROR)
	    {
	      error = repl_io_write (trail_File_vdes, &sInfo[idx]->masters[i],
				     sInfo[idx]->masters[i].s_id *
				     sInfo[idx]->masters[i].m_id,
				     SIZE_OF_TRAIL_LOG);
	    }
	  if (error != NO_ERROR)
	    {
	      return error;
	    }
	}
      else
	{			/* set the previous repl_count as the current repl_count */
	  REPL_GET_TRAIL_INFO_INT ("repl_count",
				   sInfo[idx]->masters[i].total_rows, fail);
	}

      if (sInfo[idx]->masters[i].all_repl != true)
	{
	  /* We have to maintain the replication group for selective replication */
	  error = repl_ag_get_repl_group (sInfo[idx], i);
	  REPL_CHECK_ERR_ERROR (REPL_FILE_AGENT,
				REPL_AGENT_INVALID_TRAIL_INFO);
	}
      else
	{
	  sInfo[idx]->masters[i].class_list = NULL;
	  sInfo[idx]->masters[i].class_count = 0;
	}

      error = repl_ag_init_repl_lists (sInfo[idx], i, false);
      REPL_CHECK_ERR_ERROR (REPL_FILE_AGENT, REPL_AGENT_INVALID_TRAIL_INFO);
    }

  repl_Slave_num++;
  for (i = 0; i < col_cnt; i++)
    {
      db_value_clear (&value[i]);
    }
  free_and_init (value);

  return NO_ERROR;

fail:
  for (i = 0; i < col_cnt; i++)
    {
      db_value_clear (&value[i]);
    }
  free_and_init (value);

  return error;
}

/*
 * repl_ag_get_parameters_internal() - The master function to get needed info.
 *                                     from the distributor db.
 *   return: NO_ERROR or error code
 *   func    : the routine to be executed
 *   query   : the SQL query to get the target info
 *
 * Note:
 *     called by MAIN thread
 */
static int
repl_ag_get_parameters_internal (int (*func) (DB_QUERY_RESULT *),
				 const char *query)
{
  DB_QUERY_RESULT *result = NULL;
  DB_QUERY_ERROR error_stats;
  int error = NO_ERROR;
  int pos;

  error = db_execute (query, &result, &error_stats);
  if (error <= 0)
    {
      error = REPL_AGENT_GET_PARARMETER_FAIL;
      goto end;
    }
  else
    {
      pos = db_query_first_tuple (result);
      while (pos == DB_CURSOR_SUCCESS)
	{
	  error = func (result);
	  if (error != NO_ERROR)
	    {
	      error = REPL_AGENT_GET_PARARMETER_FAIL;
	      goto end;
	    }
	  pos = db_query_next_tuple (result);
	}
    }

  error = NO_ERROR;

end:
  db_query_end (result);

  return error;
}

/*
 * repl_ag_get_parameters()
 *   return: NO_ERROR or error code
 *
 * Note:
 *     called by MAIN thread
 */
static int
repl_ag_get_parameters ()
{
  int error = NO_ERROR;
  bool restarted = false;

  error = db_login ("dba", dist_Passwd);
  REPL_CHECK_ERR_ERROR_ONE_ARG (REPL_FILE_AGENT,
				REPL_AGENT_CANT_CONNECT_TO_DIST,
				(char *) db_error_string (1));

  db_set_client_type (DB_CLIENT_TYPE_LOG_APPLIER);
  error = db_restart ("repl_agent", 0, dist_Dbname);
  REPL_CHECK_ERR_ERROR_ONE_ARG (REPL_FILE_AGENT,
				REPL_AGENT_CANT_CONNECT_TO_DIST,
				(char *) db_error_string (1));

  restarted = true;
  error = repl_ag_get_parameters_internal (repl_ag_get_env,
					   "select e_name, e_value "
					   "from env_info;");
  if (error != NO_ERROR)
    {
      goto end;
    }

  error = repl_ag_get_parameters_internal (repl_ag_get_master,
					   "select dbid, dbname, master_ip, "
					   "portnum, copylog_path, "
					   "start_pageid, first_pageid, "
					   "last_pageid, size_of_log_buffer, "
					   "size_of_cache_buffer, "
					   "size_of_copylog from master_info;");
  if (error != NO_ERROR)
    {
      goto end;
    }

  error = repl_ag_get_parameters_internal (repl_ag_get_slave,
					   "select dbid, dbname, master_ip, "
					   "portnum, userid, passwd, trails "
					   "from slave_info;");
  if (error != NO_ERROR)
    {
      goto end;
    }

  /* Initialize the trail log, if it's empty */
  if (lseek (trail_File_vdes, 0, SEEK_END) == 0)
    {
      int i = 0;
      error = repl_io_write (trail_File_vdes, (void *) &i,
			     MAX_PAGE_OF_TRAIL_LOG, SIZE_OF_TRAIL_LOG);
      if (restarted)
	{
	  db_shutdown ();
	}
      REPL_CHECK_ERR_ERROR (REPL_FILE_AGENT, REPL_AGENT_IO_ERROR);
    }

end:
  if (restarted)
    {
      db_shutdown ();
    }

  return error;
}

static void
usage_cubrid_repl_agent (const char *argv0)
{
  char *exec_name;

  exec_name = basename ((char *) argv0);
  msgcat_init ();
  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS, 1, 29),
	   VERSION, "cubrid", exec_name);
  msgcat_final ();
}

static void
repl_restart_agent (char *agent_pathname, bool print_msg)
{
  int pid;
  int error;

  if (print_msg)
    {
      REPL_ERR_LOG (REPL_FILE_AGENT, REPL_AGENT_RESTART_MESSAGE);
    }

  repl_ag_shutdown ();
  msgcat_final ();

  pid = fork ();
  if (pid < 0)
    {
      perror ("fork");
    }
  else if (pid == 0)
    {
      /* create a new session */
      setsid ();

      pid = execlp (agent_pathname, "repl_agent", "-d", dist_Dbname, NULL);
      if (pid != 0)
	{
	  perror ("exec");
	}
    }
}

/*
 * main() - Main Routine of repl_agent
 *   return: int
 *   argc : the number of arguments
 *      argv : argument list
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
  int dump_fp;
  struct option agent_option[] = {
    {"help", 0, 0, 'h'},
    {"dist-db", 1, 0, 'd'},
    {"password", 1, 0, 'p'},
    {"create-archive", 0, 0, 'a'},
    {"dump-file", 1, 0, 'f'},
    {"page-size", 1, 0, 's'},
    {"retry", 1, 0, 'r'},
    {0, 0, 0, 0}
  };
  const char *env_debug, *env_daemon, *env_first;

  dist_Dbname = strdup ("");
  dist_Passwd = strdup ("");
  log_dump_fn = strdup ("");

  /*
   * reads the replication parameters ..
   *
   * how many masters to be replicated ? --> repl_Master_num
   * for each master, find out connection port, db_name, etc...
   * etc...
   */

  err_Log_fp = stdout;

  /*
   * argument parsing, to get
   *         - db name               (db_Name)
   *         - port num              (port_Num)
   *         - err log file path     (err_Log)
   */

  env_debug = envvar_get ("DEBUG_REPL");
  if (env_debug != NULL)
    {
      debug_Dump_info = atoi (env_debug);
    }
  PTHREAD_MUTEX_INIT (file_Mutex);
  PTHREAD_MUTEX_INIT (error_Mutex);

  /* initialize message catalog for argument parsing and usage() */
  if (utility_initialize () != NO_ERROR)
    {
      REPL_ERR_LOG (REPL_FILE_AGENT, REPL_AGENT_CANT_OPEN_CATALOG);
      error = ER_FAILED;
      goto end;
    }

  while (1)
    {
      int option_index = 0;
      int option_key;

      option_key = getopt_long (argc, argv, "hd:p:af:s:",
				agent_option, &option_index);
      if (option_key == -1)
	{
	  break;
	}

      switch (option_key)
	{
	case 'd':
	  if (dist_Dbname != NULL)
	    {
	      free_and_init (dist_Dbname);
	    }
	  dist_Dbname = strdup (optarg);
	  break;
	case 'p':
	  if (dist_Passwd != NULL)
	    {
	      free_and_init (dist_Passwd);
	    }
	  dist_Passwd = strdup (optarg);
	  break;
	case 'a':
	  create_Arv = true;
	  break;
	case 'f':
	  if (log_dump_fn != NULL)
	    {
	      free_and_init (log_dump_fn);
	    }
	  log_dump_fn = strdup (optarg);
	  break;
	case 's':		/* debugging-purpose option */
	  log_pagesize = atoi (optarg);
	  if (log_pagesize < 1 || log_pagesize >= INT_MAX)
	    {
	      error = ER_FAILED;
	      goto end;
	    }
	  break;
	case 'h':
	default:
	  usage_cubrid_repl_agent (argv[0]);
	  error = ER_FAILED;
	  goto end;
	}
    }

  if (log_dump_fn && strlen (log_dump_fn) > 1)
    {
      dump_fp = repl_io_open (log_dump_fn, O_RDONLY, 0);
      if (dump_fp > 0)
	{
	  repl_ag_log_dump (dump_fp, log_pagesize);
	}
      error = REPL_AGENT_INTERNAL_ERROR;
      goto error_exit;
    }

  if (dist_Dbname && strlen (dist_Dbname) < 1)
    {
      error = REPL_AGENT_INTERNAL_ERROR;
      usage_cubrid_repl_agent (argv[0]);
      goto error_exit;
    }

  env_first = envvar_get (env_First);
  if (env_first == NULL && dist_Passwd != NULL)
    {
      envvar_set (env_First, env_First);
      envvar_set (env_Passwd, dist_Passwd);
    }
  else
    {
      const char *tmp_passwd = NULL;

      if (dist_Passwd != NULL)
	{
	  free_and_init (dist_Passwd);
	}

      tmp_passwd = envvar_get (env_Passwd);
      dist_Passwd = strdup ((tmp_passwd) ? tmp_passwd : "");
    }

  error = repl_ag_get_parameters ();
  if (error != NO_ERROR)
    {
      goto error_exit;
    }

  if (lzo_init () != LZO_E_OK)
    {
      /* may be impossible */
      error = REPL_AGENT_INTERNAL_ERROR;
      goto error_exit;
    }

  if (env_first == NULL)
    {
      /* to be a daemon process */
      env_daemon = envvar_get ("NO_DAEMON");
      if (env_daemon == NULL || strcmp (env_daemon, "no") == 0)
	{
	  repl_restart_agent (argv[0], false);
	  goto end;
	}
    }

  /* enable replication for repl_agent */
  db_Enable_replications++;

  /* connect to the master */
  error = repl_connect_to_master (false, dist_Dbname);
  if (error != NO_ERROR)
    {
      REPL_ERR_LOG (REPL_FILE_AGENT, REPL_AGENT_CANT_CONNECT_TO_MASTER);
      goto error_exit;
    }

  /* signal processing & thread pool init */
  if (repl_ag_thread_init () != NO_ERROR)
    {
      error = REPL_AGENT_INTERNAL_ERROR;
      goto error_exit;
    }

  repl_ag_thread_end ();

error_exit:
  if (restart_Agent)
    {
      repl_restart_agent (argv[0], true);
    }

end:
  if (dist_Dbname != NULL)
    {
      free_and_init (dist_Dbname);
    }
  if (dist_Passwd != NULL)
    {
      free_and_init (dist_Passwd);
    }
  if (log_dump_fn != NULL)
    {
      free_and_init (log_dump_fn);
    }
  return error;
}
