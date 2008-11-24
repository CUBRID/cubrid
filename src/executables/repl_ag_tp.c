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
 * repl_ag_tp.c - implementation of replication agent
 */

#ident "$Id$"

#include <sys/stat.h>
#if defined(LINUX)
#include <sys/resource.h>
#include <asm/page.h>
#endif

#include "porting.h"
#include "utility.h"
#include "dbi.h"
#include "repl_agent.h"
#include "repl_tp.h"
#include "log_compress.h"
#include "error_code.h"

extern FILE *debug_Log_fd;
extern int debug_Dump_info;
extern bool restart_Agent;
extern int agent_Max_size;

static pthread_t *repl_recv_thread;	/* for log reception */
static pthread_t *repl_flush_thread;	/* for log flushing */
static pthread_t *repl_apply_thread;	/* for log application */
static pthread_t *repl_status_thread;	/* for status info of repl_agent */

static bool ag_status_need_shutdown = false;

/*
 * mutex lock for accessing the error log file, trail log file.
 * file_mutex is shared beween all threads, while the pb->mutex is
 * shared between only {recv, flush} thread pair and apply thread.
 *
 * To avoid the deadlock cases, get the pb->mutex before file_mutex.
 * If you get pb->mutex after getting file_mutex lock, deadlock may
 * be occurred.
 */
pthread_mutex_t file_Mutex;
pthread_key_t slave_Key;

#define APPLY_THREAD             1
#define RECV_THREAD              2
#define FLUSH_THREAD             3
#define STATUS_THREAD            4

#define RECONNECT_COUNT          3
#define RECONNECT_SLEEP_SEC      3

/* Macros to processing final errors of each thread ..
 * So, you have to use these things only in the top routine of a thread
 */

/* check the error code */
#define REPL_CHECK_THREAD_ERROR(ttype, unlock, code, msg, arg)                \
  do {                                                                        \
    if(error != NO_ERROR) {                                                   \
      if(unlock == true) PTHREAD_MUTEX_UNLOCK(pb->mutex);                     \
      if(msg == NULL) REPL_ERR_LOG(REPL_FILE_AG_TP, code);                    \
      else REPL_ERR_LOG_ONE_ARG(REPL_FILE_AG_TP, code, msg);                  \
      switch(ttype) {                                                         \
        case APPLY_THREAD:                                                    \
          repl_tr_log_apply_fail(arg);                                        \
          break;                                                              \
        case RECV_THREAD:                                                     \
          repl_tr_log_recv_fail(arg);                                         \
          break;                                                              \
        case FLUSH_THREAD:                                                    \
          repl_tr_log_flush_fail(arg);                                        \
          break;                                                              \
      }                                                                       \
    }                                                                         \
  }while (0)                                                                  \

/* check whether the target pointer value is null */
#define REPL_CHECK_THREAD_NULL(ptr, ttype, unlock, code, msg, arg)           \
  do {                                                                       \
    if(ptr == NULL) {                                                        \
      if(unlock == true) PTHREAD_MUTEX_UNLOCK(pb->mutex);                    \
      if(msg == NULL) REPL_ERR_LOG(REPL_FILE_AG_TP, code);                   \
      else REPL_ERR_LOG_ONE_ARG(REPL_FILE_AG_TP, code, msg);                 \
      switch(ttype) {                                                        \
        case APPLY_THREAD:                                                   \
          repl_tr_log_apply_fail(arg);                                       \
          break;                                                             \
        case RECV_THREAD:                                                    \
          repl_tr_log_recv_fail(arg);                                        \
          break;                                                             \
        case FLUSH_THREAD:                                                   \
          repl_tr_log_flush_fail(arg);                                       \
          break;                                                             \
      }                                                                      \
    }                                                                        \
  }while (0)                                                                 \

/* when the repl_agent reads the incomplete log(case 1) or reads the final log
 *  record of the archive log(case 2), it has to process the target log record
 *  which forward lsa is -1/-1 or 0/0.
 *  The repl_agent should distinguish case1 from case2.
 *  The only way to do is re-fetch the transaction log.
 *  After the repl_agent refetch the log several times, it still reads
 *  the log record having forward las as (-1, -1). The target record would be
 *  a final record of archive.
 */
#define REPL_REFETCH_COUNT               5

#define REPL_PARTIAL_RECORD(lrec, final, first_pageid)                        \
          (!LSA_ISNULL(&lrec->forw_lsa) &&                                    \
           !LSA_ISNULL(&lrec->back_lsa) &&                                    \
           (lrec->trid < 0 ||                                                 \
            LSA_LT(&lrec->forw_lsa, final) ||                                 \
            lrec->forw_lsa.pageid > final->pageid + 5 ||                      \
            lrec->forw_lsa.pageid < first_pageid ||                           \
            lrec->forw_lsa.pageid < -1        ||                              \
            lrec->back_lsa.pageid < -1        ||                              \
            lrec->type > LOG_LARGER_LOGREC_TYPE ||                            \
            lrec->type < LOG_SMALLER_LOGREC_TYPE))

#define REPL_PREFETCH_PAGE_COUNT         1000


static void *repl_tr_log_apply_fail (void *arg);
static void *repl_tr_log_recv_fail (void *arg);
static void *repl_tr_log_flush_fail (void *arg);
static int repl_ag_set_trail_log (MASTER_MAP * masters);
static int
repl_log_volume_open (REPL_PB * pb, const char *vlabel, bool * create);
static void repl_ag_set_slave_info (int idx);
static void repl_ag_slave_key_delete (void *value);
static void repl_ag_shutdown_by_signal ();
static int repl_ag_find_last_applied_lsa ();
static int
repl_ag_truncate_copy_log (MASTER_INFO * minfo, PAGEID flushed, char *buf,
			   PAGEID last_pageid);
static int
repl_ag_archive_by_file_copy (MASTER_INFO * minfo, PAGEID flushed, char *buf,
			      char *archive_path, PAGEID last_pageid);
static int
repl_ag_archive_by_file_rename (MASTER_INFO * minfo, PAGEID flushed,
				char *buf, char *archive_path,
				PAGEID last_pageid);
static void repl_ag_delete_min_trantable (MASTER_INFO * minfo);
static int repl_ag_archive_copy_log (MASTER_INFO * minfo, PAGEID flushed);
static int repl_update_distributor (SLAVE_INFO * sinfo, int idx);
static bool
repl_tr_log_apply_pre (void *arg, SLAVE_INFO * sinfo, MASTER_INFO * minfo,
		       int m_idx, REPL_PB * pb, REPL_CACHE_PB * cache_pb,
		       bool * restart, LOG_LSA * final);
static bool
repl_tr_log_check_final_page (MASTER_INFO * minfo, REPL_PB * pb,
			      PAGEID pageid);
static const char *repl_rectype_string (LOG_RECTYPE type);
static bool
repl_tr_log_record_process (void *arg, struct log_rec *lrec, int m_idx,
			    LOG_LSA * final, MASTER_INFO * minfo,
			    SLAVE_INFO * sinfo, LOG_PAGE * pg_ptr,
			    REPL_PB * pb);
static int repl_tr_log_commit (SLAVE_INFO * sinfo, int m_idx, REPL_PB * pb);
static void *repl_tr_log_apply (void *arg);
static unsigned long repl_ag_get_resource_size ();
static void *repl_tr_log_flush (void *arg);
static void *repl_tr_log_recv (void *arg);
static void *repl_ag_status ();
static int repl_ag_thread_alloc ();
static int
repl_ag_log_dump_node_insert (REPL_DUMP_NODE ** head, REPL_DUMP_NODE ** tail,
			      REPL_DUMP_NODE * dump_node);
static int
repl_ag_log_dump_node_delete (REPL_DUMP_NODE ** head, REPL_DUMP_NODE ** tail,
			      REPL_DUMP_NODE * dump_node);
static int
repl_ag_log_dump_node_all_free (REPL_DUMP_NODE ** head,
				REPL_DUMP_NODE ** tail,
				REPL_DUMP_NODE * dump_node);

/*
 * repl_tr_log_apply_fail() - error processing when the apply thread fails
 *   return: void *
 *   arg(in/out)
 *
 * Note:
 *    Although an APPLY thread fails, the other APPLY threads should run,
 *    so we clear only the target slave info.
 */
static void *
repl_tr_log_apply_fail (void *arg)
{
  MASTER_INFO *minfo;
  REPL_PB *pb;
  int *id;

  id = (int *) arg;
  minfo = mInfo[*id];
  pb = minfo->pb;

  db_shutdown ();

  PTHREAD_MUTEX_LOCK (pb->mutex);
  pb->need_shutdown = true;
  ag_status_need_shutdown = true;
  PTHREAD_MUTEX_UNLOCK (pb->mutex);

  if (arg)
    free_and_init (arg);

  repl_error_flush (err_Log_fp, 0);

  PTHREAD_EXIT;

  return NULL;
}

/*
 * repl_tr_log_recv_fail() - error processing when the RECV thread fails
 *   return: void *
 *   arg   : the pointer to the index of the target master info array
 *
 * Note:
 *    If the RECV thread fails, we can't fetch the log pages from the master,
 *    So, the all the APPLY & FLUSH threads related the target master db
 *    should stop. But, the other threads related to the othre mater dbs,
 *    should run.
 */
static void *
repl_tr_log_recv_fail (void *arg)
{
  int *id;
  MASTER_INFO *minfo;
  REPL_PB *pb;

  id = (int *) arg;
  minfo = mInfo[*id];
  pb = minfo->pb;

  PTHREAD_MUTEX_LOCK (pb->mutex);
  pb->need_shutdown = true;
  ag_status_need_shutdown = true;
  PTHREAD_COND_BROADCAST (pb->write_cond);
  PTHREAD_COND_BROADCAST (pb->read_cond);
  PTHREAD_MUTEX_UNLOCK (pb->mutex);

  repl_error_flush (err_Log_fp, 0);
  repl_ag_sock_shutdown (*id);
  if (arg)
    free_and_init (arg);

  PTHREAD_EXIT;

  return NULL;
}

/*
 * repl_tr_log_flush_fail() - error processing when the FLUSH thread fails
 *   return: void *
 *   arg   : the pointer to the index of the target master info array
 *
 * Note:
 *    If the FLUSH thread fails, we can't flush the log pages to the disk.
 *    So, the all the APPLY & RECV threads related the target master db
 *    should stop. But, the other threads related to the othre mater dbs,
 *    should run.
 *
 * TO DO : we can drop this function, and use repl_tr_log_recv_fail..
 */
static void *
repl_tr_log_flush_fail (void *arg)
{
  int *id;
  MASTER_INFO *minfo;
  REPL_PB *pb;

  id = (int *) arg;
  minfo = mInfo[*id];
  pb = minfo->pb;

  PTHREAD_MUTEX_LOCK (pb->mutex);
  pb->need_shutdown = true;
  ag_status_need_shutdown = true;
  PTHREAD_COND_BROADCAST (pb->write_cond);
  PTHREAD_COND_BROADCAST (pb->read_cond);
  PTHREAD_MUTEX_UNLOCK (pb->mutex);

  if (arg)
    free_and_init (arg);
  repl_error_flush (err_Log_fp, 0);

  PTHREAD_EXIT;

  return NULL;

}

/*
 * repl_ag_set_trail_log() - set the trail log and flush it
 *   return: NO_ERROR or error code
 *   masters : master map info
 *
 * Note:
 *      When the APPLY thread processes the target record,
 *      it has to log the processed lsa to the sinfo->masters[i].
 */
static int
repl_ag_set_trail_log (MASTER_MAP * masters)
{
  int error = NO_ERROR;

  PTHREAD_MUTEX_LOCK (file_Mutex);
  error = repl_io_write (trail_File_vdes, masters,
			 masters->s_id * masters->m_id, SIZE_OF_TRAIL_LOG);
  fsync (trail_File_vdes);
  PTHREAD_MUTEX_UNLOCK (file_Mutex);
  return error;
}

/*
 * repl_log_volume_open() - Open or Create a copy log volume
 *   return: NO_ERROR or error code
 *   pb : page buffer
 *   vlabel : volume lable to be opened
 *   create(out): true, if log volume is created
 *                false, if log volume is opened
 *
 * Note:
 *   call chain : <- RECV
 *
 *   called by RECV thread
 *
 *   the caller doesn't need to do "mutex lock"
 */
static int
repl_log_volume_open (REPL_PB * pb, const char *vlabel, bool * create)
{
  int error = NO_ERROR;

  *create = false;

  PTHREAD_MUTEX_LOCK (pb->mutex);

  pb->log_vdes = repl_io_open (vlabel, O_RDWR, 0);
  if (pb->log_vdes == NULL_VOLDES)
    {
      /* this is the first time, we have to create a new file */
      umask (S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
      pb->log_vdes = creat64 (vlabel, FILE_CREATE_MODE);

      /* TO DO : verify why we need to re-open after close */
      close (pb->log_vdes);
      pb->log_vdes = repl_io_open (vlabel, O_RDWR, 0);
      if (pb->log_vdes == NULL_VOLDES)
	error = REPL_AGENT_IO_ERROR;
      else
	*create = true;
    }
  else if (lseek64 (pb->log_vdes, 0, SEEK_END) == 0)
    {
      /* If the file is empty, we have to fetch the log header */
      *create = true;
    }
  PTHREAD_MUTEX_UNLOCK (pb->mutex);

  return error;
}

/*
 * repl_ag_get_slave_info() - get the slave index using thread_key
 *   return: the pointer to the slave info
 *   s_idx : index of the slave info array
 *
 * Note:
 *      called by APPLY threads
 */
SLAVE_INFO *
repl_ag_get_slave_info (int *s_idx)
{
  int *slave_info;

  slave_info = (int *) PTHREAD_GETSPECIFIC (slave_Key);

  if (slave_info == NULL)
    {
      return NULL;
    }
  if (s_idx != NULL)
    *s_idx = *slave_info;
  return sInfo[*slave_info];
}

/*
 * repl_ag_set_slave_info() - set the slave index using thread_key
 *   return: none
 *   idx  : the array index of the target slave info.
 *
 * Note:
 *      called by APPLY threads
 */
static void
repl_ag_set_slave_info (int idx)
{
  int *slave_info;
  int error = NO_ERROR;

  slave_info = (int *) PTHREAD_GETSPECIFIC (slave_Key);

  if (slave_info == NULL)
    {
      slave_info = (int *) malloc (DB_SIZEOF (int));
      if (slave_info == NULL)
	{
	  REPL_ERR_LOG (REPL_FILE_AG_TP, REPL_AGENT_MEMORY_ERROR);
	  return;
	}

      *slave_info = idx;
      PTHREAD_SETSPECIFIC (slave_Key, (void *) slave_info);
    }
  return;
}

/*
 * repl_ag_slave_key_delete() - drop the pthread key
 *   return: none
 *
 * Note:
 *      called by main thread
 */
static void
repl_ag_slave_key_delete (void *value)
{
  free_and_init (value);
}

/*
 * repl_ag_shutdown_by_signal() - When the process catches the SIGTERM signal,
 *                                it does the shutdown process.
 *   return: none
 *
 * Note:
 *        set the "need_shutdown" flag as true, then each threads would
 *        process "shutdown"
 */
static void
repl_ag_shutdown_by_signal ()
{
  int i;

  if (debug_Dump_info & REPL_DEBUG_AGENT_STATUE)
    {
      fprintf (debug_Log_fd, "AGENT EXIT\n");
      fflush (debug_Log_fd);
    }

  ag_status_need_shutdown = true;

  for (i = 0; i < repl_Master_num; i++)
    {
      mInfo[i]->pb->need_shutdown = true;
    }
  return;
}

/*
 * repl_ag_find_last_applied_lsa() - Find out the last updated LSA
 *   return: NO_ERROR or error code
 *
 * Note:
 *     Find out the last LSA applied of the target slave.
 *     The last applied LSA is flushed by the APPLY thread to the trail log
 *     whenever the APPLY thread processed the "LOG COMMIT" record.
 *     This function is needed when the repl_agent starts or restarts.
 *
 *    call chain : <- APPLY
 *
 *    called by APPLY thread
 *
 *    the caller doesn't need to do "mutex lock"
 */
static int
repl_ag_find_last_applied_lsa ()
{
  SLAVE_INFO *sinfo = repl_ag_get_slave_info (NULL);
  int i;
  int error = NO_ERROR;

  /* find out the last page LOG LSA applied */
  for (i = 0; i < sinfo->m_cnt; i++)
    {
      MASTER_MAP temp;
      PTHREAD_MUTEX_LOCK (file_Mutex);
      if ((error = repl_io_read (trail_File_vdes, &temp,
				 (sinfo->masters[i].s_id *
				  sinfo->masters[i].m_id),
				 SIZE_OF_TRAIL_LOG)) != NO_ERROR)
	{
	  PTHREAD_MUTEX_UNLOCK (file_Mutex);
	  break;
	}
      /* If no entry in replication log, add it */
      if (temp.m_id < 1 || temp.s_id < 1)
	{
	  if ((error = repl_io_write (trail_File_vdes, &(sinfo->masters[i]),
				      (sinfo->masters[i].s_id *
				       sinfo->masters[i].m_id),
				      SIZE_OF_TRAIL_LOG)) != NO_ERROR)
	    {
	      PTHREAD_MUTEX_UNLOCK (file_Mutex);
	      break;
	    }
	}
      else
	{
	  LSA_COPY (&sinfo->masters[i].final_lsa, &temp.final_lsa);
	}
      LSA_COPY (&sinfo->masters[i].last_committed_lsa,
		&sinfo->masters[i].final_lsa);
      PTHREAD_MUTEX_UNLOCK (file_Mutex);
    }
  return error;
}

/*
 * repl_ag_truncate_copy_log() - truncate the copy log
 *   return: NO_ERROR or error code
 *    minfo : index of master info
 *    flushed : last flushed page id
 *    buf : temporal buffer
 *    last_pageid : the new start page id after truncate
 */
static int
repl_ag_truncate_copy_log (MASTER_INFO * minfo, PAGEID flushed, char *buf,
			   PAGEID last_pageid)
{
  PAGEID pageid;
  PAGEID phy_pageid;
  PAGEID old_first;
  int error = NO_ERROR;
  REPL_PB *pb = minfo->pb;
  int i;

  old_first = minfo->copy_log.first_pageid;
  pageid = minfo->copy_log.first_pageid = last_pageid;

  /* shift the remaining pages */
  while (error == NO_ERROR && pageid <= flushed)
    {

      phy_pageid = pageid - old_first;
      error = repl_io_read (pb->log_vdes, buf, phy_pageid,
			    minfo->io_pagesize);
      if (error == NO_ERROR)
	{
	  phy_pageid = pageid - minfo->copy_log.first_pageid;
	  error = repl_io_write (pb->log_vdes, buf, phy_pageid,
				 minfo->io_pagesize);
	}
      pageid++;
    }

  if (error == NO_ERROR)
    error = repl_io_truncate (pb->log_vdes, minfo->io_pagesize,
			      (flushed - minfo->copy_log.first_pageid + 1));

  if (error != NO_ERROR)
    {
      REPL_ERR_RETURN_ONE_ARG (REPL_FILE_AG_TP,
			       REPL_AGENT_CANT_CREATE_ARCHIVE, NULL);
    }

  for (i = 0; i < minfo->log_buffer_size; i++)
    {
      if (pb->log_buffer[i]->phy_pageid > 0)
	{
	  pb->log_buffer[i]->phy_pageid
	    = pb->log_buffer[i]->pageid - minfo->copy_log.first_pageid;

	}
    }
  return error;
}

/*
 * repl_ag_archive_by_file_copy() - archive the copy log using file copy
 *   return: NO_ERROR or error code
 *    minfo : index of master info
 *    flushed : last flushed page id
 *    buf : temporal buffer
 *    archive_path : archive file name
 *    last_pageid : the new start page id after truncate
 */
static int
repl_ag_archive_by_file_copy (MASTER_INFO * minfo,
			      PAGEID flushed,
			      char *buf,
			      char *archive_path, PAGEID last_pageid)
{
  PAGEID pageid;
  PAGEID phy_pageid;
  PAGEID old_first;
  int error = NO_ERROR;
  REPL_PB *pb = minfo->pb;
  int i;
  int archive_vol;

  archive_vol = repl_io_open (archive_path,
			      O_RDWR | O_CREAT, FILE_CREATE_MODE);

  /* set the start page id to be archived */
  pageid = minfo->copy_log.first_pageid;

  /* start copying */
  while (error == NO_ERROR && pageid < last_pageid)
    {

      /* set the physical page id of source page */
      phy_pageid = pageid - minfo->copy_log.first_pageid;

      error = repl_io_read (pb->log_vdes, buf, phy_pageid,
			    minfo->io_pagesize);
      if (error == NO_ERROR)
	{
	  /* flush the source page to the archive file */
	  error =
	    repl_io_write (archive_vol, buf, phy_pageid, minfo->io_pagesize);
	}
      /* set the next page id to be copied */
      pageid++;
    }				/* end of while */

  /* post-process : adjust the physical page id of the buffer, flush again */
  if (error == NO_ERROR)
    {

      old_first = minfo->copy_log.first_pageid;
      minfo->copy_log.first_pageid = pageid;

      /* shift the remaining pages */
      while (error == NO_ERROR && pageid <= flushed)
	{

	  phy_pageid = pageid - old_first;
	  error = repl_io_read (pb->log_vdes, buf, phy_pageid,
				minfo->io_pagesize);
	  if (error == NO_ERROR)
	    {
	      phy_pageid = pageid - minfo->copy_log.first_pageid;
	      error =
		repl_io_write (pb->log_vdes, buf, phy_pageid,
			       minfo->io_pagesize);
	    }
	  pageid++;
	}

      if (error == NO_ERROR)
	error = repl_io_truncate (pb->log_vdes, minfo->io_pagesize,
				  (flushed - minfo->copy_log.first_pageid +
				   1));

      if (error != NO_ERROR)
	{
	  REPL_ERR_RETURN_ONE_ARG (REPL_FILE_AG_TP,
				   REPL_AGENT_CANT_CREATE_ARCHIVE,
				   archive_path);
	}

      for (i = 0; i < minfo->log_buffer_size; i++)
	{
	  if (pb->log_buffer[i]->phy_pageid > 0)
	    {
	      pb->log_buffer[i]->phy_pageid
		= pb->log_buffer[i]->pageid - minfo->copy_log.first_pageid;
	    }
	}			/* end of for */
    }
  close (archive_vol);
  return error;
}

/*
 * repl_ag_archive_by_file_rename() - archive the copy log using file rename
 *   return: NO_ERROR or error code
 *    minfo           : index of master info
 *    flushed         : last flushed page id
 *    buf             : temporal buffer
 *    archive_path    : archive file name
 *    last_pageid     : the new start page id after truncate
 */
static int
repl_ag_archive_by_file_rename (MASTER_INFO * minfo,
				PAGEID flushed,
				char *buf,
				char *archive_path, PAGEID last_pageid)
{
  REPL_PB *pb = minfo->pb;
  int error = NO_ERROR;
  PAGEID pageid;
  PAGEID old_first;
  PAGEID phy_pageid;
  int i;
  int archive_vol;

  error = repl_io_rename (minfo->copylog_path, &pb->log_vdes,
			  archive_path, &archive_vol);

  /* post-process : adjust the physical page id of the buffer, flush again */
  if (error == NO_ERROR)
    {

      old_first = minfo->copy_log.first_pageid;
      minfo->copy_log.first_pageid = pageid = last_pageid;

      /* shift the remaining pages */
      while (error == NO_ERROR && pageid <= flushed)
	{

	  phy_pageid = pageid - old_first;
	  error = repl_io_read (archive_vol, buf, phy_pageid,
				minfo->io_pagesize);
	  if (error == NO_ERROR)
	    {
	      phy_pageid = pageid - minfo->copy_log.first_pageid;
	      error =
		repl_io_write (pb->log_vdes, buf, phy_pageid,
			       minfo->io_pagesize);
	    }
	  pageid++;
	}

      if (error == NO_ERROR)
	error = repl_io_truncate (archive_vol, minfo->io_pagesize,
				  minfo->copylog_size);

      if (error != NO_ERROR)
	{
	  close (archive_vol);
	  REPL_ERR_RETURN (REPL_FILE_AG_TP, REPL_AGENT_CANT_CREATE_ARCHIVE);
	}

      for (i = 0; i < minfo->log_buffer_size; i++)
	{
	  if (pb->log_buffer[i]->phy_pageid > 0)
	    {
	      pb->log_buffer[i]->phy_pageid
		= pb->log_buffer[i]->pageid - minfo->copy_log.first_pageid;
	    }
	}			/* end of for */
    }
  close (archive_vol);
  return error;
}

/*
 * repl_ag_delete_min_trantable() - drop the transaction entry which has a
 *                                  minimum lsa to force arciving
 *   return: none
 *   minfo(in)
 *
 * Note:
 *   The max num of cuncurrent transactions would not be large.
 *   So, we don't use any sorting stuff to find out the min lsa.
 *
 *    called by FLUSH thread
 */
static void
repl_ag_delete_min_trantable (MASTER_INFO * minfo)
{
  int i, j, k;
  LOG_LSA min;
  REPL_APPLY *apply = NULL;

  min.pageid = PAGEID_MAX;
  min.offset = PGLENGTH_MAX;

  /* verify whethere all the logs has been processed */
  for (i = 0; i < MAX_NUM_OF_SLAVES; i++)
    {
      for (j = 0; j < MAX_NUM_OF_MASTERS; j++)
	{
	  if (sInfo[i]->masters[j].m_id == minfo->dbid)
	    {
	      for (k = 0; k < sInfo[i]->masters[j].cur_repl; k++)
		{
		  if (sInfo[i]->masters[j].repl_lists[k] &&
		      sInfo[i]->masters[j].repl_lists[k]->tranid > 0 &&
		      !LSA_ISNULL (&sInfo[i]->masters[j].repl_lists[k]->
				   start_lsa)
		      && LSA_LT (&sInfo[i]->masters[j].repl_lists[k]->
				 start_lsa, &min))
		    {
		      LSA_COPY (&min,
				&sInfo[i]->masters[j].repl_lists[k]->
				start_lsa);
		      apply = sInfo[i]->masters[j].repl_lists[k];
		    }
		}
	    }
	}
    }
  if (apply)
    {
      if (debug_Dump_info & REPL_DEBUG_AGENT_STATUE)
	{
	  fprintf (debug_Log_fd, "repl_ag_delete_min_trantable\n");
	  fflush (debug_Log_fd);
	}
      repl_ag_clear_repl_item (apply);
    }
}

/*
 * repl_ag_archive_copy_log() - archive the active copy log
 *   return: NO_ERROR or error code
 *     minfo : master info
 *     flushed : finally flushed pageid
 *
 * Note:
 *    To prevent from growing the size of copy log file, FLUSH thread
 *    archive the active copy log file periodically.
 *
 *    When the # of pages in active file exceeds REPL_LOG_PAGE_COUNT,
 *    FLUSH thread verifies whether the APPLY thread processes all the logs
 *    in pages to be archived. If some logs has not been processed,
 *    this job would be delayed.
 *
 *
 *    called by FLUSH thread
 */
static int
repl_ag_archive_copy_log (MASTER_INFO * minfo, PAGEID flushed)
{

  int i, j, k;
  PAGEID last_pageid;
  int error = NO_ERROR;
  char *buf = NULL;
  char archive_path[FILE_PATH_LENGTH];
  REPL_PB *pb = minfo->pb;
  off64_t file_size;

  /* wait for the APPLY thread to run, not to purge the copy log */
  if (pb->read_pageid == 0)
    {
      return error;
    }

  if (debug_Dump_info & REPL_DEBUG_AGENT_STATUE)
    {
      fprintf (debug_Log_fd,
	       "TRUNCATE ENTER : TRAIL LSA(%d, %d), COPY LOG(%d,%d,%d)\n",
	       sInfo[0]->masters[0].final_lsa.pageid,
	       sInfo[0]->masters[0].final_lsa.offset,
	       mInfo[0]->copy_log.first_pageid,
	       mInfo[0]->copy_log.start_pageid,
	       mInfo[0]->copy_log.last_pageid);
      fflush (debug_Log_fd);
    }

  /* the last page id to be archived */
  last_pageid = minfo->copy_log.first_pageid + minfo->copylog_size;

  /* verify whethere all the logs has been processed */
  for (i = 0; i < MAX_NUM_OF_SLAVES; i++)
    {
      for (j = 0; j < MAX_NUM_OF_MASTERS; j++)
	{
	  if (sInfo[i]->masters[j].m_id == minfo->dbid)
	    {
	      if (sInfo[i]->masters[j].last_committed_lsa.pageid <=
		  last_pageid)
		return error;
	      for (k = 0; k < sInfo[i]->masters[j].cur_repl; k++)
		{
		  if (sInfo[i]->masters[j].repl_lists[k]
		      && sInfo[i]->masters[j].repl_lists[k]->tranid > 0
		      && !LSA_ISNULL (&sInfo[i]->masters[j].repl_lists[k]->
				      start_lsa)
		      && (sInfo[i]->masters[j].repl_lists[k]->start_lsa.
			  pageid <= last_pageid))
		    {
		      return error;
		    }
		}
	    }
	}
    }

  /* Now .. start archiving active log */

  buf = (char *) malloc (minfo->io_pagesize);
  REPL_CHECK_ERR_NULL (REPL_FILE_AG_TP, REPL_AGENT_MEMORY_ERROR, buf);

  if (create_Arv == false
      && ((minfo->copy_log.first_pageid - minfo->copy_log.start_pageid)
	  / minfo->copylog_size) != 0)
    {
      error = repl_ag_truncate_copy_log (minfo, flushed, buf, last_pageid);
    }
  else
    {
      /* create the archive file */
      sprintf (archive_path, "%s.ar%d", minfo->copylog_path,
	       (minfo->copy_log.first_pageid - minfo->copy_log.start_pageid)
	       / minfo->copylog_size);

      file_size = repl_io_file_size (pb->log_vdes);
      if (file_size >= 0)
	{

	  if (file_size < minfo->copylog_size * minfo->io_pagesize * 2)
	    {
	      error = repl_ag_archive_by_file_rename (minfo, flushed, buf,
						      archive_path,
						      last_pageid);
	    }
	  else
	    {
	      error = repl_ag_archive_by_file_copy (minfo, flushed, buf,
						    archive_path,
						    last_pageid);
	    }
	}
      else
	{
	  error = REPL_AGENT_IO_ERROR;
	}
    }

  if (error != NO_ERROR)
    REPL_ERR_LOG_ONE_ARG (REPL_FILE_AG_TP, REPL_AGENT_CANT_CREATE_ARCHIVE,
			  archive_path);

  if (debug_Dump_info & REPL_DEBUG_AGENT_STATUE)
    {
      fprintf (debug_Log_fd,
	       "TRUNCATE EXIT : TRAIL LSA(%d, %d), COPY LOG(%d,%d,%d)\n",
	       sInfo[0]->masters[0].final_lsa.pageid,
	       sInfo[0]->masters[0].final_lsa.offset,
	       mInfo[0]->copy_log.first_pageid,
	       mInfo[0]->copy_log.start_pageid,
	       mInfo[0]->copy_log.last_pageid);
      fflush (debug_Log_fd);
    }

  free_and_init (buf);
  return error;
}

/*
 * repl_update_distributor() - update replication info
 *   return: error code
 *   sinfo : slave info
 *   idx   : index of master map
 */
static int
repl_update_distributor (SLAVE_INFO * sinfo, int idx)
{
  int error = NO_ERROR;
  DB_QUERY_RESULT *result;
  DB_QUERY_ERROR query_error;

  MASTER_MAP *master;
  char sql[1024];

  master = &sinfo->masters[idx];

  error = db_login ("dba", dist_Passwd);
  if (error == NO_ERROR)
    error = db_restart ("repl_agent", 0, dist_Dbname);
  if (error != NO_ERROR)
    return REPL_AGENT_CANT_CONNECT_TO_DIST;

  sprintf (sql, "UPDATE trail_info SET final_pageid=%d, final_offset=%d, "
	   " status='%c' WHERE master_dbid=%d and slave_dbid=%d;",
	   master->final_lsa.pageid, master->final_lsa.offset,
	   master->status, master->m_id, master->s_id);

  if (db_execute (sql, &result, &query_error) < 0)
    {
      db_query_end (result);
      goto error_rtn;
    }
  error = db_query_end (result);
  if (error != NO_ERROR)
    goto error_rtn;
  error = db_commit_transaction ();
  if (error != NO_ERROR)
    goto error_rtn;

  db_shutdown ();
  return NO_ERROR;

error_rtn:
  db_shutdown ();
  return REPL_AGENT_CANT_CONNECT_TO_DIST;
}

/*
 * repl_reconnect() -
 *
 *   return: NO_ERROR or error code
 *
 *     dbname(in) :
 *     userid(in) :
 *     passwd(in) :
 *     first(in)  :
 */
int
repl_reconnect (char *dbname, char *userid, char *passwd, bool first)
{
  int error = NO_ERROR;
  int reconnect_count = RECONNECT_COUNT;

  if (!first)
    db_shutdown ();

  /* connect to slave db */
  error = db_login (userid, passwd);
  if (error != NO_ERROR)
    {
      error = REPL_AGENT_CANT_LOGIN_TO_SLAVE;
      return error;
    }

  while (reconnect_count-- > 0 &&
	 (error = db_restart ("repl_agent", 0, dbname)) != NO_ERROR)
    SLEEP_USEC (RECONNECT_SLEEP_SEC, 0);

  if (error != NO_ERROR)
    {
      error = REPL_AGENT_CANT_CONNECT_TO_SLAVE;
    }

  return error;
}

static bool
repl_tr_log_apply_pre (void *arg,
		       SLAVE_INFO * sinfo,
		       MASTER_INFO * minfo,
		       int m_idx,
		       REPL_PB * pb,
		       REPL_CACHE_PB * cache_pb,
		       bool * restart, LOG_LSA * final)
{
  int error = NO_ERROR;

  /* wait for the signal from the log recv thread  */
  PTHREAD_MUTEX_LOCK (pb->mutex);	/* lock 1 */
  while (!pb->need_shutdown &&	/* it's not a shutdown phase */
	 (pb->log_hdr == NULL ||	/* log header should be fetched */
	  pb->log_buffer == NULL ||	/* log buffer should be initialized */
	  pb->max_pageid < 1 ||	/* at least one page should be fetched */
	  cache_pb->log_buffer == NULL))
    {
      /* cache log buffer should be initialized */
      PTHREAD_COND_TIMEDWAIT (pb->read_cond, pb->mutex);
    }

  /* if the shutdown is progress */
  if (pb->need_shutdown)
    {
      PTHREAD_MUTEX_UNLOCK (pb->mutex);
      return false;
    }

  /* fetch the next lsa to be processed */
  /* When the agent is restart, read the first log of active copy log,
   * in order to avoid archiving the active logs
   */
  if (*restart)
    {
      final->pageid = minfo->copy_log.first_pageid;
      final->offset = 0;
      *restart = false;
    }
  /* If this is the first time */
  else
    {
      LSA_COPY (final, &sinfo->masters[m_idx].final_lsa);
    }

  PTHREAD_MUTEX_UNLOCK (pb->mutex);	/* unlock 1 */

  /* allocate the log data area */
  /* NOTE: we don't know the io_page size before the RECV thread
   *       fetch the log header. So the log data area should be
   *       alloacted here
   */
  if (sinfo->log_data == NULL)
    {
      sinfo->log_data = (char *) malloc (minfo->io_pagesize);
      REPL_CHECK_THREAD_NULL (sinfo->log_data, APPLY_THREAD,
			      0, REPL_AGENT_MEMORY_ERROR, NULL, arg);
    }
  if (sinfo->rec_type == NULL)
    {
      sinfo->rec_type = (char *) malloc (DB_SIZEOF (INT16));
      REPL_CHECK_THREAD_NULL (sinfo->rec_type, APPLY_THREAD,
			      0, REPL_AGENT_MEMORY_ERROR, NULL, arg);
    }

  if (sinfo->undo_unzip_ptr == NULL)
    {
      sinfo->undo_unzip_ptr = log_zip_alloc (minfo->io_pagesize, false);

      REPL_CHECK_THREAD_NULL (sinfo->undo_unzip_ptr, APPLY_THREAD,
			      0, REPL_AGENT_MEMORY_ERROR, NULL, arg);
    }

  if (sinfo->redo_unzip_ptr == NULL)
    {
      sinfo->redo_unzip_ptr = log_zip_alloc (minfo->io_pagesize, false);
      REPL_CHECK_THREAD_NULL (sinfo->redo_unzip_ptr, APPLY_THREAD,
			      0, REPL_AGENT_MEMORY_ERROR, NULL, arg);
    }

  return true;
}

static bool
repl_tr_log_check_final_page (MASTER_INFO * minfo,
			      REPL_PB * pb, PAGEID pageid)
{
  int error = NO_ERROR;

  /* If the target page id is less than the start page id of
   * of the recv thread
   */
  if (pageid < minfo->copy_log.start_pageid)
    {
      REPL_ERR_LOG (REPL_FILE_AG_TP, REPL_AGENT_INTERNAL_ERROR);
      pb->need_shutdown = true;
      return false;
    }

  PTHREAD_MUTEX_LOCK (pb->mutex);
  if (pageid > pb->max_pageid)
    {
      /* the target page is not arrived .. send a request to the
       * RECV thread to read the target page
       */
      pb->read_pageid = pageid;
      PTHREAD_COND_BROADCAST (pb->end_cond);
    }
  PTHREAD_MUTEX_UNLOCK (pb->mutex);

  /* wait for the RECV thread to read the next page */
  PTHREAD_MUTEX_LOCK (pb->mutex);
  while (!pb->need_shutdown && pageid > pb->max_pageid)
    {
      PTHREAD_COND_TIMEDWAIT (pb->read_cond, pb->mutex);
    }
  PTHREAD_MUTEX_UNLOCK (pb->mutex);

  if (pb->need_shutdown)
    return false;

  return true;
}

static const char *
repl_rectype_string (LOG_RECTYPE type)
{
  switch (type)
    {
    case LOG_CLIENT_NAME:
      return "LOG_CLIENT_NAME";

    case LOG_UNDOREDO_DATA:
      return "LOG_UNDOREDO_DATA";

    case LOG_DIFF_UNDOREDO_DATA:	/* LOG DIFF undo and redo data */
      return "LOG_DIFF_UNDOREDO_DATA";

    case LOG_UNDO_DATA:
      return "LOG_UNDO_DATA";

    case LOG_REDO_DATA:
      return "LOG_REDO_DATA";

    case LOG_DBEXTERN_REDO_DATA:
      return "LOG_DBEXTERN_REDO_DATA";

    case LOG_DUMMY_HEAD_POSTPONE:
      return "LOG_DUMMY_HEAD_POSTPONE";

    case LOG_POSTPONE:
      return "LOG_POSTPONE";

    case LOG_RUN_POSTPONE:
      return "LOG_RUN_POSTPONE";

    case LOG_COMPENSATE:
      return "LOG_COMPENSATE";

    case LOG_LCOMPENSATE:
      return "LOG_LCOMPENSATE";

    case LOG_CLIENT_USER_UNDO_DATA:
      return "LOG_CLIENT_USER_UNDO_DATA";

    case LOG_CLIENT_USER_POSTPONE_DATA:
      return "LOG_CLIENT_USER_POSTPONE_DATA";

    case LOG_RUN_NEXT_CLIENT_UNDO:
      return "LOG_RUN_NEXT_CLIENT_UNDO";

    case LOG_RUN_NEXT_CLIENT_POSTPONE:
      return "LOG_RUN_NEXT_CLIENT_POSTPONE";

    case LOG_WILL_COMMIT:
      return "LOG_WILL_COMMIT";

    case LOG_COMMIT_WITH_POSTPONE:
      return "LOG_COMMIT_WITH_POSTPONE";

    case LOG_COMMIT_WITH_CLIENT_USER_LOOSE_ENDS:
      return "LOG_COMMIT_WITH_CLIENT_USER_LOOSE_ENDS";

    case LOG_COMMIT:
      return "LOG_COMMIT";

    case LOG_COMMIT_TOPOPE_WITH_POSTPONE:
      return "LOG_COMMIT_TOPOPE_WITH_POSTPONE";

    case LOG_COMMIT_TOPOPE_WITH_CLIENT_USER_LOOSE_ENDS:
      return "LOG_COMMIT_TOPOPE_WITH_CLIENT_USER_LOOSE_ENDS";

    case LOG_COMMIT_TOPOPE:
      return "LOG_COMMIT_TOPOPE";

    case LOG_ABORT_WITH_CLIENT_USER_LOOSE_ENDS:
      return "LOG_ABORT_WITH_CLIENT_USER_LOOSE_ENDS";

    case LOG_ABORT:
      return "LOG_ABORT";

    case LOG_ABORT_TOPOPE_WITH_CLIENT_USER_LOOSE_ENDS:
      return "LOG_ABORT_TOPOPE_WITH_CLIENT_USER_LOOSE_ENDS";

    case LOG_ABORT_TOPOPE:
      return "LOG_ABORT_TOPOPE";

    case LOG_START_CHKPT:
      return "LOG_START_CHKPT";

    case LOG_END_CHKPT:
      return "LOG_END_CHKPT";

    case LOG_SAVEPOINT:
      return "LOG_SAVEPOINT";

    case LOG_2PC_PREPARE:
      return "LOG_2PC_PREPARE";

    case LOG_2PC_START:
      return "LOG_2PC_START";

    case LOG_2PC_COMMIT_DECISION:
      return "LOG_2PC_COMMIT_DECISION";

    case LOG_2PC_ABORT_DECISION:
      return "LOG_2PC_ABORT_DECISION";

    case LOG_2PC_COMMIT_INFORM_PARTICPS:
      return "LOG_2PC_COMMIT_INFORM_PARTICPS";

    case LOG_2PC_ABORT_INFORM_PARTICPS:
      return "LOG_2PC_ABORT_INFORM_PARTICPS";

    case LOG_2PC_RECV_ACK:
      return "LOG_2PC_RECV_ACK";

    case LOG_DUMMY_CRASH_RECOVERY:
      return "LOG_DUMMY_CRASH_RECOVERY";

    case LOG_DUMMY_FILLPAGE_FORARCHIVE:
      return "LOG_DUMMY_FILLPAGE_FORARCHIVE";

    case LOG_END_OF_LOG:
      return "LOG_END_OF_LOG";

    case LOG_REPLICATION_DATA:
      return "LOG_REPLICATION_DATA";
    case LOG_REPLICATION_SCHEMA:
      return "LOG_REPLICATION_SCHEMA";
    case LOG_UNLOCK_COMMIT:
      return "LOG_UNLOCK_COMMIT";
    case LOG_UNLOCK_ABORT:
      return "LOG_UNLOCK_ABORT";

    case LOG_SMALLER_LOGREC_TYPE:
    case LOG_LARGER_LOGREC_TYPE:
      break;
    }

  return "UNKNOWN_LOG_REC_TYPE";
}

static bool
repl_tr_log_record_process (void *arg,
			    struct log_rec *lrec,
			    int m_idx,
			    LOG_LSA * final,
			    MASTER_INFO * minfo,
			    SLAVE_INFO * sinfo,
			    LOG_PAGE * pg_ptr, REPL_PB * pb)
{
  REPL_APPLY *apply = NULL;
  int error = NO_ERROR;
  LOG_LSA lsa_apply;
  time_t slave_time;

  /* Is this a partial record? refetch the target page */
  if (debug_Dump_info & REPL_DEBUG_LOG_DUMP)
    {
      fprintf (debug_Log_fd, "\nLSA = %3d|%3d, Forw log = %3d|%3d,"
	       " Backw log = %3d|%3d,\n"
	       "     Trid = %3d, Prev tran logrec = %3d|%3d\n"
	       "     Type = %s",
	       final->pageid, final->offset,
	       lrec->forw_lsa.pageid, lrec->forw_lsa.offset,
	       lrec->back_lsa.pageid, lrec->back_lsa.offset,
	       lrec->trid, lrec->prev_tranlsa.pageid,
	       lrec->prev_tranlsa.offset, repl_rectype_string (lrec->type));
      fflush (debug_Log_fd);
    }

  if (REPL_PARTIAL_RECORD (lrec, final, minfo->copy_log.first_pageid))
    {
      pb->read_pageid = final->pageid;
      pb->read_invalid_page = true;
      PTHREAD_COND_BROADCAST (pb->end_cond);
      return false;
    }

  sinfo->masters[m_idx].status = 'A';
  minfo->is_end_of_record = false;
  switch (lrec->type)
    {
    case LOG_DUMMY_FILLPAGE_FORARCHIVE:
      final->pageid++;
      final->offset = 0;
      return false;

    case LOG_END_OF_LOG:
      if (repl_ag_does_page_exist (final->pageid + 1, m_idx)
	  || repl_ag_is_in_archive (final->pageid, m_idx))
	{
	  /* when we meet the END_OF_LOG of archive file, skip log page */
	  final->pageid++;
	  final->offset = 0;
	}
      else
	{
	  /* when we meet the END_OF_LOG, send a signal to the RECV
	   * thread in order to change the RECV mode as on-demand
	   * (RECV thread would not pre-fetch the log page..)
	   */
	  pb->read_pageid = final->pageid;
	  PTHREAD_COND_BROADCAST (pb->end_cond);
	  sinfo->masters[m_idx].status = 'I';
	  minfo->is_end_of_record = true;
	  time (&slave_time);
	  repl_ag_log_perf_info (minfo->conn.dbname, -1, NULL, &slave_time);
	}
      return false;

    case LOG_UNDO_DATA:
    case LOG_REDO_DATA:
    case LOG_DIFF_UNDOREDO_DATA:
    case LOG_UNDOREDO_DATA:
      /* To gurantee the stability of copy log archiving,
       * we manages the start point of all transactions.
       */
      apply = repl_ag_find_apply_list (sinfo, lrec->trid, m_idx);
      if (apply == NULL)
	{
	  pb->need_shutdown = true;
	  error = REPL_AGENT_MEMORY_ERROR;
	}
      REPL_CHECK_THREAD_ERROR (APPLY_THREAD, true,
			       REPL_AGENT_INTERNAL_ERROR, NULL, arg);
      if (LSA_ISNULL (&apply->start_lsa))
	{
	  LSA_COPY (&apply->start_lsa, final);
	}
      break;

    case LOG_REPLICATION_DATA:
    case LOG_REPLICATION_SCHEMA:
      /* add the replication log to the target transaction */
      error = repl_ag_set_repl_log (pg_ptr, lrec->type, lrec->trid,
				    final, m_idx);
      if (error != NO_ERROR)
	{
	  pb->need_shutdown = true;
	}
      REPL_CHECK_THREAD_ERROR (APPLY_THREAD, true,
			       REPL_AGENT_INTERNAL_ERROR, NULL, arg);
      break;

    case LOG_UNLOCK_COMMIT:
    case LOG_COMMIT_TOPOPE:
      /* add the repl_list to the commit_list  */
      error = repl_ag_add_unlock_commit_log (lrec->trid, final, m_idx);
      if (error != NO_ERROR)
	{
	  pb->need_shutdown = true;
	}
      REPL_CHECK_THREAD_ERROR (APPLY_THREAD, true,
			       REPL_AGENT_INTERNAL_ERROR, NULL, arg);

      if (lrec->type != LOG_COMMIT_TOPOPE)
	{
	  break;
	}

    case LOG_COMMIT:
      if (debug_Dump_info & REPL_DEBUG_AGENT_STATUE)
	{
	  fprintf (debug_Log_fd,
		   "COMMIT: TRAIL LSA(%d,%d), FINAL(%d,%d)\n",
		   sinfo->masters[m_idx].final_lsa.pageid,
		   sinfo->masters[m_idx].final_lsa.offset,
		   final->pageid, final->offset);
	  fflush (debug_Log_fd);
	}
      /* apply the replication log to the slave */
      if (LSA_GT (final, &sinfo->masters[m_idx].final_lsa))
	{
	  error = repl_ag_set_commit_log (lrec->trid, final, m_idx,
					  lrec->type ==
					  LOG_COMMIT_TOPOPE ? -1 :
					  repl_ag_retrieve_eot_time
					  (pg_ptr, final, m_idx));
	  if (error != NO_ERROR)
	    {
	      pb->need_shutdown = true;
	    }
	  REPL_CHECK_THREAD_ERROR (APPLY_THREAD, true,
				   REPL_AGENT_INTERNAL_ERROR, NULL, arg);

	  do
	    {
	      error = repl_ag_apply_commit_list (&lsa_apply, m_idx,
						 &sinfo->old_time);
	      if (error == REPL_AGENT_CANT_CONNECT_TO_SLAVE)
		{
		  /* If errors, all threads related this slave db would fail also,
		   * Don't generate archive
		   */
		  pb->start_to_archive = false;
		  switch (error)
		    {
		    case ER_TM_SERVER_DOWN_UNILATERALLY_ABORTED:
		      REPL_CHECK_THREAD_ERROR (APPLY_THREAD, true,
					       REPL_AGENT_SLAVE_STOP,
					       sinfo->conn.dbname, arg);
		      break;
		    case ER_LK_UNILATERALLY_ABORTED:
		      REPL_CHECK_THREAD_ERROR (APPLY_THREAD, true,
					       REPL_AGENT_NEED_MORE_WS,
					       sinfo->conn.dbname, arg);
		      break;
		    default:
		      REPL_CHECK_THREAD_ERROR (APPLY_THREAD, true,
					       REPL_AGENT_REPLICATION_BROKEN,
					       sinfo->conn.dbname, arg);
		      break;

		    }
		}
	      if (!LSA_ISNULL (&lsa_apply))
		{
		  LSA_COPY (&(sinfo->masters[m_idx].final_lsa), &lsa_apply);
		}
	    }
	  while (!LSA_ISNULL (&lsa_apply));	/* if lsa_apply is not null then
						 * there is the replication log
						 * applying to the slave
						 */
	  /* if the apply thread reaches the final LSA previously applied,
	   * it's safe for the FLUSH thread to make an archive log
	   */
	  if (pb->start_to_archive == false)
	    {
	      pb->start_to_archive = true;
	    }
	}
      else
	{
	  repl_ag_clear_repl_item_by_tranid (sinfo, lrec->trid, m_idx);
	}
      break;

      /* you have to check the ABORT LOG to avoid the memory leak */
    case LOG_UNLOCK_ABORT:
      break;

    case LOG_ABORT:
      repl_ag_apply_abort (m_idx, lrec->trid,
			   repl_ag_retrieve_eot_time (pg_ptr, final,
						      m_idx),
			   &sinfo->old_time);
      if (debug_Dump_info & REPL_DEBUG_AGENT_STATUE)
	{
	  fprintf (debug_Log_fd,
		   "ABORT TRAN : TRAIL LSA(%d,%d), FINAL(%d,%d)\n",
		   sinfo->masters[m_idx].final_lsa.pageid,
		   sinfo->masters[m_idx].final_lsa.offset,
		   final->pageid, final->offset);
	  fflush (debug_Log_fd);
	}
      repl_ag_clear_repl_item_by_tranid (sinfo, lrec->trid, m_idx);
      break;

    case LOG_DUMMY_CRASH_RECOVERY:
      LSA_COPY (final, &lrec->forw_lsa);
      return false;

    default:
      break;
    }				/* switch(lrec->type) */

  /* ITRACK 1001906, 1002005
   * if this is the final record of the archive log..
   * we have to fetch the next page. So, increase the pageid,
   * but we don't know the exact offset of the next record.
   * the offset would be adjusted after getting the next log page
   */
  if (lrec->forw_lsa.pageid == 0 ||
      lrec->forw_lsa.pageid == -1 ||
      lrec->type <= LOG_SMALLER_LOGREC_TYPE ||
      lrec->type >= LOG_LARGER_LOGREC_TYPE)
    {
      final->pageid++;
      final->offset = 0;

      er_log_debug (ARG_FILE_LINE,
		    "log record error : forward lsa is null\n"
		    " LSA = %3d|%3d, Forw log = %3d|%3d,"
		    " Backw log = %3d|%3d,\n"
		    "     Trid = %3d, Prev tran logrec = %3d|%3d\n"
		    "     Type = %s",
		    final->pageid, final->offset,
		    lrec->forw_lsa.pageid, lrec->forw_lsa.offset,
		    lrec->back_lsa.pageid, lrec->back_lsa.offset,
		    lrec->trid, lrec->prev_tranlsa.pageid,
		    lrec->prev_tranlsa.offset,
		    repl_rectype_string (lrec->type));

      return false;
    }
  return true;
}

/*
 * repl_tr_log_commit() -
 *   return: NO_ERROR or error code
 *
 * Note:
 *
 */
static int
repl_tr_log_commit (SLAVE_INFO * sinfo, int m_idx, REPL_PB * pb)
{
  int error = NO_ERROR;

  if (db_commit_is_needed () > 0)
    {
      error = db_commit_transaction ();
      if (error == NO_ERROR)
	{
	  fsync (pb->log_vdes);
	  error = repl_ag_set_trail_log (&sinfo->masters[m_idx]);
	  LSA_COPY (&sinfo->masters[m_idx].last_committed_lsa,
		    &sinfo->masters[m_idx].final_lsa);

	  if (debug_Dump_info & REPL_DEBUG_AGENT_STATUE)
	    {
	      fprintf (debug_Log_fd,
		       "COMMIT : TRAIL LSA(%d, %d)\n",
		       sinfo->masters[m_idx].final_lsa.pageid,
		       sinfo->masters[m_idx].final_lsa.offset);
	      fflush (debug_Log_fd);
	    }
	}
    }

  return error;
}

/*
 * repl_tr_log_update_distdb() -
 *
 *   return: NO_ERROR or error code
 *
 *    sinfo(in) :
 *    m_idx(in) :
 *    pb(in) :
 *
 * Note:
 *
 */
static int
repl_tr_log_update_distdb (SLAVE_INFO * sinfo, int m_idx, REPL_PB * pb)
{
  int error = NO_ERROR;

  error = repl_tr_log_commit (sinfo, m_idx, pb);
  if (error != NO_ERROR)
    {
      return error;
    }

  /* update the replication information to the distributor database */
  db_shutdown ();

  error = repl_update_distributor (sinfo, m_idx);
  REPL_CHECK_ERR_LOG_ONE_ARG (REPL_FILE_AG_TP, error,
			      (char *) db_error_string (1));
  if (error != NO_ERROR)
    {
      repl_error_flush (err_Log_fp, 0);
    }

  /* reconnect to the slave */
  error = repl_reconnect (sinfo->conn.dbname, sinfo->conn.userid,
			  sinfo->conn.passwd, true);

  return error;
}

/*
 * repl_tr_log_apply() - apply the transaction log to the slave
 *   return: void *
 *   arg : the pointer to the index of slave info array
 *
 * Note:
 *
 *    . connect to the slave db
 *    . find out the last applied LSA (start point to be applied)
 *      for all masters
 *    while() {
 *       for(master N) {
 *          . wait for recv thread
 *          . set the start point  (lsa = start LSA)
 *          . while(lsa) {
 *               . wait fo the target page buffer
 *                 while(lsa in page) {
 *                    . if(this record is replication log)
 *                         . add repl_list
 *                    . if(this is the COMMIT log)
 *                         . apply repl_list
 *                         . flush trail log
 *                    . lsa = next lsa
 *                 }
 *            }
 *       } -- finish for master N
 *    }
 *    . disconnect to the slave db
 *    . shutdown
 */
static void *
repl_tr_log_apply (void *arg)
{
  struct log_rec *lrec = NULL;
  SLAVE_INFO *sinfo;
  MASTER_INFO *minfo;
  REPL_PB *pb = NULL;
  REPL_CACHE_PB *cache_pb;
  int error;
  int i, m_idx;
  LOG_LSA final;
  REPL_LOG_BUFFER *log_buf = NULL;
  LOG_PAGE *pg_ptr;
  bool restart = true;
  PAGEID old_pageid = -1;
  struct timeval time_reconnect, time_commit, time_now;
  bool is_connect = true;

  /* set slave info (thread specific data) to connect */
  repl_ag_set_slave_info (*(int *) arg);
  sinfo = sInfo[*(int *) arg];

  error = repl_reconnect (sinfo->conn.dbname, sinfo->conn.userid,
			  sinfo->conn.passwd, true);
  REPL_CHECK_THREAD_ERROR (APPLY_THREAD, false, error, sinfo->conn.dbname,
			   arg);

  /* append partition class */
  for (i = 0; i < sinfo->m_cnt; i++)
    {
      repl_ag_append_partition_class_to_repl_group (sinfo, i);
    }

  /* find out the last log applid */
  if (error == NO_ERROR)
    {
      error = repl_ag_find_last_applied_lsa ();
      REPL_CHECK_THREAD_ERROR (APPLY_THREAD, false,
			       REPL_AGENT_CANT_READ_TRAIL_LOG,
			       sinfo->conn.dbname, arg);
    }

  if (debug_Dump_info & REPL_DEBUG_AGENT_STATUE)
    {
      fprintf (debug_Log_fd,
	       "AGENT START : TRAIL LSA(%d, %d), COPY LOG(%d,%d,%d)\n",
	       sinfo->masters[0].final_lsa.pageid,
	       sinfo->masters[0].final_lsa.offset,
	       mInfo[0]->copy_log.first_pageid,
	       mInfo[0]->copy_log.start_pageid,
	       mInfo[0]->copy_log.last_pageid);
      fflush (debug_Log_fd);
    }

  gettimeofday (&time_commit, NULL);
  gettimeofday (&time_reconnect, NULL);
  /* start the main loop */
  do
    {
      /* Now, process TR logs for each master info,
       * A slave db may replicates N master db  */
      for (i = 0; i < sinfo->m_cnt && (pb == NULL || !pb->need_shutdown); i++)
	{
	  /* start to process the target master */
	  m_idx = repl_ag_get_master_info_index (sinfo->masters[i].m_id);
	  minfo = mInfo[m_idx];
	  pb = minfo->pb;
	  cache_pb = minfo->cache_pb;

	  if (error != NO_ERROR)
	    {
	      PTHREAD_MUTEX_LOCK (pb->mutex);
	      pb->need_shutdown = true;
	      pb->start_to_archive = false;
	      PTHREAD_MUTEX_UNLOCK (pb->mutex);
	      break;
	    }

	  /* Is it OK to start the replication ? */
	  if (repl_tr_log_apply_pre (arg, sinfo, minfo, i, pb, cache_pb,
				     &restart, &final) == false)
	    {
	      break;
	    }

	  if (debug_Dump_info & REPL_DEBUG_AGENT_STATUE)
	    {
	      fprintf (debug_Log_fd,
		       "AGENT PRE : FINAL LSA(%d,%d), TRAIL LSA(%d, %d)\n",
		       final.pageid, final.offset,
		       sinfo->masters[0].final_lsa.pageid,
		       sinfo->masters[0].final_lsa.offset);
	      fflush (debug_Log_fd);
	    }

	  /* start to process the target page */
	  while (!LSA_ISNULL (&final))
	    {
	      /* check the final page id */
	      if (repl_tr_log_check_final_page (minfo, pb,
						final.pageid) == false)
		{
		  break;
		}

	      /* get the target page buffer */
	      while (pb->need_shutdown != true)
		{
		  PTHREAD_MUTEX_LOCK (pb->mutex);
		  log_buf = repl_ag_get_page_buffer (final.pageid, m_idx);
		  old_pageid = final.pageid;
		  REPL_CHECK_THREAD_NULL (log_buf, APPLY_THREAD,
					  1, REPL_AGENT_INTERNAL_ERROR,
					  NULL, arg);
		  if (log_buf->logpage.hdr.logical_pageid == final.pageid)
		    {
		      /* is valid page ? */
		      if (log_buf->logpage.hdr.offset < 0)
			{
			  /* invalid page */
			  repl_ag_release_page_buffer (final.pageid, m_idx);
			  final.pageid++;
			  PTHREAD_COND_BROADCAST (pb->end_cond);
			  PTHREAD_MUTEX_UNLOCK (pb->mutex);
			}
		      else
			{
			  /* valid page */
			  break;
			}
		    }
		  else
		    {
		      /* if the log page is not flushed by the master server,
		       * request the target page again
		       */
		      repl_ag_release_page_buffer (final.pageid, m_idx);
		      PTHREAD_COND_BROADCAST (pb->end_cond);
		      PTHREAD_MUTEX_UNLOCK (pb->mutex);
		    }
		}

	      /* Now, we've got the target page */
	      /* set the log page pointer */
	      pg_ptr = &(log_buf->logpage);
	      while (final.pageid == log_buf->pageid
		     && pb->need_shutdown != true)
		{

		  /* adjust the offset when the offset is 0.
		   * If we read final log record from the archive,
		   * we don't know the exact offset of the next record,
		   * In this case, we set the offset as 0, increase the pageid.
		   * So, before getting the log record, check the offset and
		   * adjust it
		   */
		  if (final.offset == 0)
		    {
		      final.offset = log_buf->logpage.hdr.offset;
		    }

		  lrec = (struct log_rec *) ((char *) pg_ptr->area
					     + final.offset);

		  /* process the log record */
		  if (repl_tr_log_record_process (arg, lrec, i, &final,
						  minfo, sinfo, pg_ptr,
						  pb) == false)
		    {
		      break;
		    }

		  LSA_COPY (&final, &lrec->forw_lsa);	/* set the next record */
		}

	      gettimeofday (&time_now, NULL);
	      if (((time_now.tv_sec - time_commit.tv_sec) * 1000 +
		   (time_now.tv_usec / 1000 - time_commit.tv_usec / 1000))
		  > perf_Commit_msec)
		{
		  gettimeofday (&time_commit, NULL);
		  error = repl_tr_log_commit (sinfo, i, pb);
		  REPL_CHECK_THREAD_ERROR (APPLY_THREAD, true,
					   REPL_AGENT_CANT_CONNECT_TO_SLAVE,
					   sinfo->conn.dbname, arg);
		}

	      repl_ag_release_page_buffer (old_pageid, m_idx);
	      PTHREAD_MUTEX_UNLOCK (pb->mutex);

	      if (pb->need_shutdown)
		{
		  break;
		}
	      if (sinfo->masters[i].status == 'I')
		{
		  SLEEP_USEC (0, 100 * 1000);
		}

	      gettimeofday (&time_now, NULL);
	      if ((time_now.tv_sec - time_reconnect.tv_sec)
		  > sinfo->masters[i].restart_interval
		  && repl_ag_is_idle (sinfo, i))
		{
		  gettimeofday (&time_reconnect, NULL);
		  error = repl_tr_log_update_distdb (sinfo, i, pb);
		  REPL_CHECK_THREAD_ERROR (APPLY_THREAD, false,
					   REPL_AGENT_CANT_CONNECT_TO_SLAVE,
					   sinfo->conn.dbname, arg);
		}
	      if (repl_ag_get_resource_size () > agent_Max_size * 1024)
		{
		  restart_Agent = true;
		  pb->need_shutdown = true;
		}
	    }
	}
    }
  while (!pb->need_shutdown);

  if (arg)
    {
      free_and_init (arg);
    }
  db_shutdown ();

  repl_error_flush (err_Log_fp, 0);

  PTHREAD_EXIT;

  return NULL;
}

static char *
repl_ag_skip_token (const char *p)
{
  while (isspace (*p))
    p++;
  while (*p && !isspace (*p))
    p++;
  return (char *) p;
}

/*
 * repl_ag_get_resource_size() -
 *   return: kbyte
 *
 * Note:
 */
static unsigned long
repl_ag_get_resource_size ()
{
#if defined(LINUX)
  int fd;
  unsigned long mem;
  char buffer[4096], *current_p;
  int length;

  sprintf (buffer, "/proc/%d/stat", getpid ());

  fd = open (buffer, O_RDONLY);
  if (fd < 0)
    {
      return 0;
    }
  length = read (fd, buffer, sizeof (buffer) - 1);
  buffer[length] = '\0';
  close (fd);

  current_p = buffer;

  current_p = repl_ag_skip_token (current_p);	/* skip pid */
  current_p = repl_ag_skip_token (current_p);	/* skip procname */
  current_p = repl_ag_skip_token (current_p);	/* skip state */
  current_p = repl_ag_skip_token (current_p);	/* skip ppid */
  current_p = repl_ag_skip_token (current_p);	/* skip pgrp */
  current_p = repl_ag_skip_token (current_p);	/* skip session */
  current_p = repl_ag_skip_token (current_p);	/* skip tty */
  current_p = repl_ag_skip_token (current_p);	/* skip tty pgrp */
  current_p = repl_ag_skip_token (current_p);	/* skip flags */
  current_p = repl_ag_skip_token (current_p);	/* skip min flt */
  current_p = repl_ag_skip_token (current_p);	/* skip cmin flt */
  current_p = repl_ag_skip_token (current_p);	/* skip maj flt */
  current_p = repl_ag_skip_token (current_p);	/* skip cmaj flt */
  current_p = repl_ag_skip_token (current_p);	/* skip utime */
  current_p = repl_ag_skip_token (current_p);	/* skip stime */
  current_p = repl_ag_skip_token (current_p);	/* skip cutime */
  current_p = repl_ag_skip_token (current_p);	/* skip cstime */
  current_p = repl_ag_skip_token (current_p);	/* skip priority */
  current_p = repl_ag_skip_token (current_p);	/* skip nice */
  current_p = repl_ag_skip_token (current_p);	/* skip threads */
  current_p = repl_ag_skip_token (current_p);	/* skip it_real_val */
  current_p = repl_ag_skip_token (current_p);	/* skip start_time */
  current_p = repl_ag_skip_token (current_p);	/* skip vsize */

  mem = strtoul (current_p, &current_p, 10);	/* rss */

  /* page to kbyte */
  mem = mem << (PAGE_SHIFT - 10);

  return mem;

#elif defined(SOLARIS)
  unsigned long mem;

#if defined (__sparc_v9__) || defined (__sparcv9)

  struct stat64 stat_buf;
  char buf[256];

  snprintf (buf, sizeof (buf), "/proc/%d/as", (unsigned int) getpid ());

  if (stat64 (buf, &stat_buf) < 0)
    {
      return 0;
    }

  mem = stat_buf.st_size / 1024;

#else

  struct stat stat_buf;
  char buf[256];

  snprintf (buf, sizeof (buf), "/proc/%d/as", (unsigned int) getpid ());

  if (stat (buf, &stat_buf) < 0)
    {
      return 0;
    }

  mem = stat_buf.st_size / 1024;

#endif

  return mem;

#endif
}

/*
 * repl_tr_log_flush() - flush the replication log buffer
 *   return: void *
 *   arg   : the pointer to the index of the target master info array
 *
 * Note:
 *    To find out which page is flushed finally, we attach the final page id
 *    at the end of file.
 *    The next log page would overwrite the final page id area of the previous
 *    page.
 *
 *    while() {
 *       . wait for "page is not empty" signal
 *       . flush the log page
 *       . flush the end log (final page info)
 *       . if needed, make an archive file
 *    }
 */
static void *
repl_tr_log_flush (void *arg)
{
  int *id;
  MASTER_INFO *minfo;
  REPL_PB *pb;
  int error = NO_ERROR;
  PAGEID flushed_pageid;

  id = (int *) arg;
  minfo = mInfo[*id];
  pb = minfo->pb;

  while (TRUE)
    {
      PTHREAD_MUTEX_LOCK (pb->mutex);
      while (!pb->need_shutdown && pb->head == pb->tail)
	{
	  /* a buffer is empty, wait for the recv's signal */
	  PTHREAD_COND_TIMEDWAIT (pb->read_cond, pb->mutex);
	}

      /* check the shutdown signal */
      if (pb->need_shutdown)
	{
	  PTHREAD_MUTEX_UNLOCK (pb->mutex);
	  break;
	}

      /* flush the log page */
      minfo->copy_log.last_pageid = pb->log_buffer[pb->head]->pageid;
      error =
	repl_io_write (pb->log_vdes, &(pb->log_buffer[pb->head]->logpage),
		       pb->log_buffer[pb->head]->phy_pageid,
		       minfo->io_pagesize);
      REPL_CHECK_THREAD_ERROR (FLUSH_THREAD, true, REPL_AGENT_IO_ERROR,
			       NULL, arg);

      /* flush the final page info */
      error = repl_io_write_copy_log_info (pb->log_vdes, &(minfo->copy_log),
					   pb->log_buffer[pb->head]->
					   phy_pageid + 1,
					   minfo->io_pagesize);
      REPL_CHECK_THREAD_ERROR (FLUSH_THREAD, true, REPL_AGENT_IO_ERROR,
			       NULL, arg);

      pb->head = (pb->head + 1) % minfo->log_buffer_size;
      pb->start_pageid = pb->log_buffer[pb->head]->pageid;

      /* To increase concurrency during the log archiving,
       * we save the final flused pageid, and provide it to
       * repl_ag_archive_copy_log()
       */
      flushed_pageid = minfo->copy_log.last_pageid;

      /* make an archive */
      if (minfo->pb->start_to_archive &&
	  minfo->copy_log.last_pageid - minfo->copy_log.first_pageid >=
	  minfo->copylog_size)
	{
	  error = repl_ag_archive_copy_log (minfo, flushed_pageid);
	  if (error != NO_ERROR)
	    {
	      PTHREAD_MUTEX_UNLOCK (pb->mutex);
	      repl_tr_log_flush_fail (arg);
	      break;
	    }
	}
      PTHREAD_COND_BROADCAST (pb->write_cond);
      PTHREAD_MUTEX_UNLOCK (pb->mutex);

    }

  if (arg)
    free_and_init (arg);
  repl_error_flush (err_Log_fp, 0);
  PTHREAD_EXIT;
  return NULL;
}

/*
 * repl_tr_log_recv() - main routine of TR recv thread.
 *   return: void *
 *   arg   : the pointer to the index of the target master info array
 *
 * Note:
 *   A log recv thread is responsibility for requesting & receiving  TR
 *   logs from a target master db.
 *   We need log recv treads as many as the number of master dbs.
 *
 *     . Initialize the thread specific things
 *        - open the log copy file
 *        - connection for master db
 *        - get the agent id from the repl_server
 *     . Get the log header
 *     . find out the io page size
 *     . initialize the log buffer & socket buffer
 *     . start up the main loop
 *         - request the next log page to the master
 *         - add the page into the page buffer
 *         - if it is a partial page
 *           while(page is not full) {
 *              . get the partial page again
 *           }
 *
 *    if fails with some reason, it should clear resources and die.
 */
static void *
repl_tr_log_recv (void *arg)
{
  int *id;
  bool first_time = false;
  int start_pageid, old_pageid = 0;
  int last_pageid;
  int result;
  bool in_archive;
  MASTER_INFO *minfo;
  REPL_PB *pb;
  REPL_CACHE_PB *cache_pb;
  int error;
  bool on_demand = false;

  id = (int *) arg;

  minfo = mInfo[*id];
  pb = minfo->pb;
  cache_pb = minfo->cache_pb;

  /* open the copy log file */
  error = repl_log_volume_open (pb, minfo->copylog_path, &first_time);
  REPL_CHECK_THREAD_ERROR (RECV_THREAD, false,
			   REPL_AGENT_COPY_LOG_OPEN_ERROR,
			   minfo->copylog_path, arg);

  /* initialize the communication stuffs */
  error = repl_ag_sock_init (*id);
  REPL_CHECK_THREAD_ERROR (RECV_THREAD, false, error, NULL, arg);

  /* get the agent id from the server */
  error = repl_ag_sock_request_agent_id (*id);
  REPL_CHECK_THREAD_ERROR (RECV_THREAD, false, REPL_AGENT_GET_ID_FAIL,
			   NULL, arg);

  /* get the log header from the server */
  error = repl_ag_get_log_header (*(int *) id, first_time);
  REPL_CHECK_THREAD_ERROR (RECV_THREAD, false, REPL_AGENT_GET_LOG_HDR_FAIL,
			   NULL, arg);

  PTHREAD_MUTEX_LOCK (pb->mutex);
  if (first_time == true)
    {
      /* start from the first replication point or backup point */
      start_pageid =
	minfo->copy_log.first_pageid =
	minfo->copy_log.start_pageid =
	(minfo->copy_log.start_pageid - REPL_PREFETCH_PAGE_COUNT > 0 ?
	 minfo->copy_log.start_pageid - REPL_PREFETCH_PAGE_COUNT : 1);

      /* last page to be requested */
      last_pageid = pb->log_hdr->append_lsa.pageid - 1;
    }
  /* The repl_agent is restarted, set the start page as the last
   * page of copy log... */
  else
    {
      start_pageid = minfo->copy_log.last_pageid;
      last_pageid = pb->log_hdr->append_lsa.pageid - 1;
    }
  PTHREAD_MUTEX_UNLOCK (pb->mutex);

  if (start_pageid > last_pageid)
    {
      on_demand = true;
    }

  /* Now, we know the io page size .. initialize the io buffer */
  /* if the size is different with old value.. we have to change the
   * socket option */
  if (minfo->io_pagesize > REPL_DEF_LOG_PAGE_SIZE)
    {
      error =
	repl_ag_sock_reset_recv_buf (minfo->conn.client_sock,
				     minfo->io_pagesize);
      REPL_CHECK_THREAD_ERROR (RECV_THREAD, false, REPL_AGENT_SOCK_ERROR,
			       NULL, arg);

      minfo->conn.resp_buffer = realloc (minfo->conn.resp_buffer,
					 REPL_RESP_BUFFER_SIZE);
      REPL_CHECK_THREAD_NULL (minfo->conn.resp_buffer, RECV_THREAD,
			      false, REPL_AGENT_MEMORY_ERROR, NULL, arg);
    }

  /* Initialize the log buffer area */
  PTHREAD_MUTEX_LOCK (pb->mutex);
  error = repl_init_log_buffer (pb, minfo->log_buffer_size,
				SIZEOF_REPL_LOG_BUFFER);
  pb->start_pageid = pb->min_pageid = start_pageid;
  PTHREAD_MUTEX_UNLOCK (pb->mutex);
  REPL_CHECK_THREAD_ERROR (RECV_THREAD, false, REPL_AGENT_MEMORY_ERROR,
			   NULL, arg);

  /* Initialize the cache log buffer area */
  PTHREAD_MUTEX_LOCK (cache_pb->mutex);
  error = repl_init_cache_log_buffer (cache_pb, minfo->cache_buffer_size,
				      SIZEOF_REPL_CACHE_LOG_BUFFER (minfo->
								    io_pagesize),
				      minfo->cache_buffer_size);
  PTHREAD_MUTEX_UNLOCK (cache_pb->mutex);
  REPL_CHECK_THREAD_ERROR (RECV_THREAD, false, REPL_AGENT_MEMORY_ERROR,
			   NULL, arg);

  /* Start the main loop */
  while (true)
    {

      /*PTHREAD_MUTEX_LOCK(pb->mutex); */
      /* check the shutdown flag */
      if (pb->need_shutdown)
	{
	  /*PTHREAD_MUTEX_UNLOCK(pb->mutex); */
	  break;
	}
      /*PTHREAD_MUTEX_UNLOCK(pb->mutex); */

      if (old_pageid == start_pageid && minfo->is_end_of_record)
	SLEEP_USEC (0, 100 * 1000);
      /* request the next log page */
      error =
	repl_ag_sock_request_next_log_page (*id, start_pageid, on_demand,
					    &result, &in_archive);
      REPL_CHECK_THREAD_ERROR (RECV_THREAD, false,
			       REPL_AGENT_GET_LOG_PAGE_FAIL, NULL, arg);

      old_pageid = start_pageid;
      if (result == REPL_REQUEST_NOPAGE || result == REPL_REQUEST_FAIL)
	{
	  continue;
	}
      if (((LOG_PAGE *) minfo->conn.resp_buffer)->hdr.logical_pageid !=
	  start_pageid)
	{
	  continue;
	}
      if (!repl_ag_valid_page ((LOG_PAGE *) minfo->conn.resp_buffer, *id))
	{
	  continue;
	}

      /* I got it.. Now insert the page to the buffer */
      PTHREAD_MUTEX_LOCK (pb->mutex);
      /* find out the slot to be inserted */
      while (!pb->need_shutdown
	     && (pb->tail + 1) % minfo->log_buffer_size == pb->head)
	{
	  /* the page buffer is full, wait ... */
	  PTHREAD_COND_TIMEDWAIT (pb->write_cond, pb->mutex);
	}
      if (pb->need_shutdown)
	{
	  PTHREAD_MUTEX_UNLOCK (pb->mutex);
	  break;
	}

      if (pb->log_buffer[pb->tail]->pageid != 0
	  && pb->log_buffer[(pb->tail + 1) %
			    minfo->log_buffer_size]->pageid != 0)
	{
	  /* overwrite */
	  pb->min_pageid
	    = pb->log_buffer[(pb->tail + 1) % minfo->log_buffer_size]->pageid;
	  /* maintains the min & max page id for the APPLY
	   * thread to read the page already flushed */
	}
      pb->max_pageid = start_pageid;

      memcpy (&(pb->log_buffer[pb->tail]->logpage),
	      minfo->conn.resp_buffer, minfo->io_pagesize);
      pb->log_buffer[pb->tail]->in_archive = in_archive;

      /* set the logical & physical page id */
      pb->log_buffer[pb->tail]->pageid = start_pageid;
      pb->log_buffer[pb->tail]->phy_pageid
	= start_pageid - minfo->copy_log.first_pageid;

      if (pb->read_invalid_page)
	{
	  start_pageid = pb->read_pageid;
	  pb->read_invalid_page = false;
	  pb->head = pb->tail = 0;
	  pb->min_pageid = start_pageid;
	  pb->max_pageid = start_pageid;
	  minfo->copy_log.last_pageid = start_pageid;

	  error = repl_io_truncate (pb->log_vdes, minfo->io_pagesize,
				    (start_pageid -
				     minfo->copy_log.first_pageid + 1));
	  PTHREAD_MUTEX_UNLOCK (pb->mutex);
	  continue;
	}
      /*
       * if this is not the last page (partial page),
       * fetch the next page
       */
      if (start_pageid < last_pageid)
	{
	  pb->tail = (pb->tail + 1) % minfo->log_buffer_size;
	  start_pageid++;
	  PTHREAD_COND_BROADCAST (pb->read_cond);
	}
      /*
       * if this is the last page (partial page),
       * read again ...
       */
      else
	{
	  /* Now, I'm in on demand mode, and will request to the repl_server
	   * to fetch the log page from the disk
	   */
	  on_demand = true;

	  /* wait for the APPLY thread to set the read page id */
	  while (!pb->need_shutdown && pb->read_pageid == 0)
	    {
	      PTHREAD_COND_TIMEDWAIT (pb->end_cond, pb->mutex);
	    }
	  if (pb->need_shutdown)
	    {
	      PTHREAD_MUTEX_UNLOCK (pb->mutex);
	      break;
	    }

	  /* still same page request */
	  if (pb->read_pageid == start_pageid)
	    {
	      error =
		repl_ag_sock_request_next_log_page (*id, start_pageid,
						    true, &result,
						    &in_archive);
	      REPL_CHECK_THREAD_ERROR (RECV_THREAD, true,
				       REPL_AGENT_GET_LOG_PAGE_FAIL, NULL,
				       arg);
	      if (result == REPL_REQUEST_NOPAGE
		  || result == REPL_REQUEST_FAIL)
		{
		  PTHREAD_MUTEX_UNLOCK (pb->mutex);
		  continue;
		}

	      memcpy (&(pb->log_buffer[pb->tail]->logpage),
		      minfo->conn.resp_buffer, minfo->io_pagesize);
	      last_pageid = start_pageid;
	    }
	  /* now, APPLY thread requests the next new page */
	  else if (pb->read_pageid > start_pageid)
	    {
	      start_pageid++;
	      last_pageid++;
	      /* increase the tail of the buffer to flush this page */
	      pb->tail = (pb->tail + 1) % minfo->log_buffer_size;
	    }
	  PTHREAD_COND_BROADCAST (pb->read_cond);
	}			/* end of read again */
      PTHREAD_MUTEX_UNLOCK (pb->mutex);

    }				/* end of while */

  repl_error_flush (err_Log_fp, 0);
  repl_ag_sock_shutdown (*id);
  free_and_init (arg);
  PTHREAD_EXIT;
  return NULL;
}

/*
 * repl_ag_status() - main routine of repl_agent status thread.
 *   return: void *
 */
static void *
repl_ag_status ()
{
  int error = NO_ERROR;

  /* initialize the communication stuffs */
  error = repl_ag_srv_sock_init ();

  /*
   * loop, processing new connection requests until a client buffer
   * is read in on an existing connection.
   */
  while (ag_status_need_shutdown == false)
    {
      if ((error = repl_ag_srv_wait_request ()) != NO_ERROR)
	ag_status_need_shutdown = true;
    }				/* While not_done */

  repl_ag_srv_sock_shutdown ();
  PTHREAD_EXIT;
  return NULL;
}

/*
 * repl_ag_thread_alloc() - allocate threads
 *   return: error code
 *
 * Note:
 *     allocate the array of threads.
 *     RECV & FLUSH theads will be allocated as the # of master dbs
 *     SLAVE theads will be allocated as the # of slave dbs
 *
 *      called by MAIN thread
 */
static int
repl_ag_thread_alloc ()
{
  int error = NO_ERROR;

  /* for the STATUS thread */
  repl_status_thread = (pthread_t *) malloc (DB_SIZEOF (pthread_t));
  REPL_CHECK_ERR_NULL (REPL_FILE_AG_TP, REPL_AGENT_MEMORY_ERROR,
		       repl_status_thread);

  /* for the RECV thread */
  repl_recv_thread =
    (pthread_t *) malloc (DB_SIZEOF (pthread_t) * repl_Master_num);
  REPL_CHECK_ERR_NULL (REPL_FILE_AG_TP, REPL_AGENT_MEMORY_ERROR,
		       repl_recv_thread);

  /* for the FLUSH thread */
  repl_flush_thread =
    (pthread_t *) malloc (DB_SIZEOF (pthread_t) * repl_Master_num);
  REPL_CHECK_ERR_NULL (REPL_FILE_AG_TP, REPL_AGENT_MEMORY_ERROR,
		       repl_flush_thread);

  /* for the APPLY thread */
  repl_apply_thread =
    (pthread_t *) malloc (DB_SIZEOF (pthread_t) * repl_Slave_num);
  REPL_CHECK_ERR_NULL (REPL_FILE_AG_TP, REPL_AGENT_MEMORY_ERROR,
		       repl_apply_thread);

  return error;
}

/*
 * repl_ag_thread_init() - initialize the thread related things
 *   return: error code
 *   param1(in)
 *
 * Note:
 *      called by main thread of repl_agent
 */
int
repl_ag_thread_init ()
{
  int i;
  int *id1, *id2;
  int error = NO_ERROR;

  /* signal processing */
  repl_signal_process (repl_ag_shutdown_by_signal);


  PTHREAD_KEY_CREATE (slave_Key, repl_ag_slave_key_delete);

  if ((error = repl_ag_thread_alloc ()) != NO_ERROR)
    return error;

  PTHREAD_CREATE (*repl_status_thread, NULL, repl_ag_status, NULL);

  for (i = 0; i < repl_Master_num; i++)
    {
      id1 = (int *) malloc (DB_SIZEOF (int));
      REPL_CHECK_ERR_NULL (REPL_FILE_AG_TP, REPL_AGENT_MEMORY_ERROR, id1);
      *id1 = i;
      id2 = (int *) malloc (DB_SIZEOF (int));
      REPL_CHECK_ERR_NULL (REPL_FILE_AG_TP, REPL_AGENT_MEMORY_ERROR, id2);
      *id2 = i;

      /* create the log receiver and flush thread per master */
      PTHREAD_CREATE (repl_recv_thread[i], NULL, repl_tr_log_recv,
		      (void *) id1);
      PTHREAD_CREATE (repl_flush_thread[i], NULL, repl_tr_log_flush,
		      (void *) id2);
    }
  for (i = 0; i < repl_Slave_num; i++)
    {
      id1 = (int *) malloc (DB_SIZEOF (int));
      REPL_CHECK_ERR_NULL (REPL_FILE_AG_TP, REPL_AGENT_MEMORY_ERROR, id1);
      *id1 = i;
      PTHREAD_CREATE (repl_apply_thread[i], NULL, repl_tr_log_apply,
		      (void *) id1);
    }

  return NO_ERROR;
}

/*
 * repl_ag_thread_end() - finalize all the threads
 *   return: none
 *
 * Note:
 *     When the repl_agent stops, finalizes all the threads
 *
 *    called by main thread or repl_shutdown_thread
 */
void
repl_ag_thread_end (void)
{
  int i;

  for (i = 0; i < repl_Master_num; i++)
    {
      PTHREAD_JOIN (repl_recv_thread[i]);
      PTHREAD_JOIN (repl_flush_thread[i]);
    }
  free_and_init (repl_recv_thread);
  free_and_init (repl_flush_thread);

  for (i = 0; i < repl_Slave_num; i++)
    {
      PTHREAD_JOIN (repl_apply_thread[i]);
    }
  free_and_init (repl_apply_thread);

  PTHREAD_KEY_DELETE (slave_Key);
  for (i = 0; i < repl_Master_num; i++)
    {
      PTHREAD_COND_DESTROY (mInfo[i]->pb->read_cond);
      PTHREAD_COND_DESTROY (mInfo[i]->pb->write_cond);
    }

  ag_status_need_shutdown = true;
  PTHREAD_JOIN (*repl_status_thread);
  free_and_init (repl_status_thread);
}

static int
repl_ag_log_dump_node_insert (REPL_DUMP_NODE ** head, REPL_DUMP_NODE ** tail,
			      REPL_DUMP_NODE * dump_node)
{
  if (*head == NULL)
    {
      *head = *tail = dump_node;
    }
  else if (*head == *tail)
    {

      dump_node->next = *head;
      *head = dump_node;
      (*tail)->prev = dump_node;
    }
  else
    {
      (*head)->prev = dump_node;
      dump_node->next = *head;
      *head = dump_node;
    }
}

static int
repl_ag_log_dump_node_delete (REPL_DUMP_NODE ** head, REPL_DUMP_NODE ** tail,
			      REPL_DUMP_NODE * dump_node)
{
  if (*head == *tail)
    {
      *head = *tail = NULL;
    }
  else if (*head == dump_node)
    {
      *head = dump_node->next;
      (*head)->prev = NULL;
    }
  else if (*tail == dump_node)
    {
      *tail = dump_node->prev;
      (*tail)->next = NULL;
    }
  else
    {
      dump_node->prev->next = dump_node->next;
      dump_node->next->prev = dump_node->prev;
    }
  free_and_init (dump_node);
}

static int
repl_ag_log_dump_node_all_free (REPL_DUMP_NODE ** head,
				REPL_DUMP_NODE ** tail,
				REPL_DUMP_NODE * dump_node)
{

}

/*
 * repl_ag_log_dump()
 *   return: none
 *   log_fd(in)
 *   io_pagesize(in)
 */
void
repl_ag_log_dump (int log_fd, int io_pagesize)
{
  REPL_CACHE_LOG_BUFFER *cache_log_buffer;
  REPL_LOG_BUFFER *log_buffer;
  LOG_PAGE *logpage;
  LOG_LSA lsa;
  PAGEID read_pageid;
  struct log_rec *lrec;
  FILE *log_out_fp = stdout;
  REPL_DUMP_NODE *head, *tail, *dump_node;

  head = tail = NULL;
  LSA_SET_NULL (&lsa);
  cache_log_buffer =
    (REPL_CACHE_LOG_BUFFER *)
    malloc (SIZEOF_REPL_CACHE_LOG_BUFFER (io_pagesize));
  if (cache_log_buffer == NULL)
    {
      fprintf (log_out_fp, "MEMORY MALLOC ERROR\n");
      return;
    }
  log_buffer = &cache_log_buffer->log_buffer;
  logpage = &log_buffer->logpage;

  read_pageid = 0;
  while (repl_io_read (log_fd, logpage, read_pageid, io_pagesize) == NO_ERROR)
    {
      lsa.pageid = logpage->hdr.logical_pageid;
      lsa.offset = logpage->hdr.offset;

      fprintf (log_out_fp, "Page Header (%8d, %4d)\n", lsa.pageid,
	       lsa.offset);
      while (logpage->hdr.logical_pageid == lsa.pageid)
	{
	  lrec = (struct log_rec *) ((char *) logpage->area + lsa.offset);
	  fprintf (log_out_fp, "LSA = %8d|%4d, Forw log = %8d|%4d,"
		   " Backw log = %8d|%4d,"
		   "     Trid = %8d, Prev tran logrec = %8d|%4d"
		   "     Type = %s\n",
		   lsa.pageid, lsa.offset,
		   lrec->forw_lsa.pageid, lrec->forw_lsa.offset,
		   lrec->back_lsa.pageid, lrec->back_lsa.offset,
		   lrec->trid, lrec->prev_tranlsa.pageid,
		   lrec->prev_tranlsa.offset,
		   repl_rectype_string (lrec->type));
	  fflush (log_out_fp);

	  dump_node = head;
	  while (dump_node)
	    {
	      if (dump_node->tranid == lrec->trid)
		{
		  break;
		}
	      dump_node = dump_node->next;
	    }

	  if (dump_node != NULL)
	    {
	      dump_node->type = lrec->type;
	    }
	  else
	    {
	      dump_node = (REPL_DUMP_NODE *) malloc (sizeof (REPL_DUMP_NODE));
	      dump_node->next = NULL;
	      dump_node->prev = NULL;
	      dump_node->tranid = lrec->trid;
	      dump_node->type = lrec->type;
	      repl_ag_log_dump_node_insert (&head, &tail, dump_node);
	    }

	  if (dump_node->type == LOG_COMMIT)
	    {
	      repl_ag_log_dump_node_delete (&head, &tail, dump_node);
	      dump_node = NULL;
	    }

	  LSA_COPY (&lsa, &lrec->forw_lsa);
	}
      read_pageid++;
    }

  while (head)
    {
      dump_node = head;
      head = head->next;

      fprintf (log_out_fp, "NOT COMMIT TRANSACTION : %d, type:%s\n",
	       dump_node->tranid, repl_rectype_string (dump_node->type));
      fflush (log_out_fp);
      free_and_init (dump_node);
    }

  if (cache_log_buffer)
    {
      free_and_init (cache_log_buffer);
      cache_log_buffer = NULL;
    }

}
