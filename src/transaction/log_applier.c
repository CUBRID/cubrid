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
 * log_applier.c : main routine of Transaction Log Applier
 *
 */

#ident "$Id$"

#if !defined (WINDOWS)
#include <unistd.h>
#endif
#include <errno.h>
#include <fcntl.h>
#if !defined (WINDOWS)
#include <sys/time.h>
#endif

#include "porting.h"
#include "utility.h"
#include "environment_variable.h"
#include "message_catalog.h"
#include "log_compress.h"
#include "parser.h"
#include "object_print.h"
#include "db.h"
#include "object_accessor.h"
#include "locator_cl.h"
#include "connection_cl.h"


#define LA_DEFAULT_CACHE_BUFFER_SIZE 100;

#if 0
#define LA_LOG_IS_IN_ARCHIVE(pageid) \
  ((pageid) < la_Info.act_log.log_hdr->nxarv_pageid \
   && ((pageid) + la_Info.act_log.log_hdr->npages) \
   <= la_Info.act_log.log_hdr->append_lsa.pageid)
#else
#define LA_LOG_IS_IN_ARCHIVE(pageid) \
  ((pageid) < la_Info.act_log.log_hdr->nxarv_pageid)
#endif

#define LA_MAX_NUM_CONTIGUOS_BUFFERS(size) \
           ((unsigned int)(INT_MAX / (5*(size))))
	/* the max # of elements of contiguos page cache buffer */

#define SIZEOF_LA_CACHE_LOG_BUFFER(io_pagesize) \
  (offsetof(LA_CACHE_BUFFER, logpage) \
   + (io_pagesize))

#define LA_DEFAULT_LOG_PAGE_SIZE 4096


#define LA_GET_PAGE_RETRY_COUNT 10
#define LA_REPL_LIST_COUNT 50
#define LA_PAGE_DOESNOT_EXIST 0
#define LA_PAGE_EXST_IN_ACTIVE_LOG 1
#define LA_PAGE_EXST_IN_ARCHIVE_LOG 2

#define LA_LOGAREA_SIZE (la_Info.act_log.pgsize - DB_SIZEOF(LOG_HDRPAGE))
#define LA_LOG_READ_ADVANCE_WHEN_DOESNT_FIT(result, length, offset, pageid, pgptr, release) \
  do { \
    if ((offset)+(length) >= LA_LOGAREA_SIZE) { \
      if ((release) == 1) la_release_page_buffer(pageid); \
      if (((pgptr) = la_get_page(++(pageid))) == NULL) { \
	result = ER_IO_READ; \
      } else { release = 1; } \
      (offset) = 0; \
      DB_ALIGN((offset), INT_ALIGNMENT); \
    } \
  } while(0)

#define LA_LOG_READ_ALIGN(result, offset, pageid, log_pgptr, release) \
  do { \
    DB_ALIGN((offset), INT_ALIGNMENT); \
    while ((offset) >= LA_LOGAREA_SIZE) { \
      if (release == 1) la_release_page_buffer(pageid); \
      if (((log_pgptr) = la_get_page(++(pageid))) == NULL) { \
	result = ER_IO_READ; \
      } else { release = 1; } \
      (offset) -= LA_LOGAREA_SIZE; \
      DB_ALIGN((offset), INT_ALIGNMENT); \
    } \
  } while(0)

#define LA_LOG_READ_ADD_ALIGN(result, add, offset, pageid, log_pgptr, release) \
  do { \
    (offset) += (add); \
    LA_LOG_READ_ALIGN(result, (offset), (pageid), (log_pgptr), (release));   \
  } while(0)

#define LA_SLEEP(sec, usec) \
 do { \
   struct timeval sleep_time_val; \
   sleep_time_val.tv_sec = (sec); \
   sleep_time_val.tv_usec = (usec); \
   select (0, 0, 0, 0, &sleep_time_val); \
 } while(0)

typedef struct la_cache_buffer LA_CACHE_BUFFER;
struct la_cache_buffer
{
  int fix_count;
  int recently_freed;		/* Reference value 0/1 used by the clock algorithm */

  PAGEID pageid;		/* Logical page of the log */
  PAGEID phy_pageid;
  bool drop;
  bool in_archive;
  LOG_PAGE logpage;		/* The actual buffered log page */
};

typedef struct la_cache_buffer_area LA_CACHE_BUFFER_AREA;
struct la_cache_buffer_area
{
  LA_CACHE_BUFFER *buffer_area;	/* log buffer area */
  LA_CACHE_BUFFER_AREA *next;	/* next area */
};

typedef struct la_cache_pb LA_CACHE_PB;
struct la_cache_pb
{
  MHT_TABLE *hash_table;	/* hash table for buffers */
  LA_CACHE_BUFFER **log_buffer;	/* buffer pool */
  int num_buffers;		/* $ of buffers */
  int clock_hand;
  LA_CACHE_BUFFER_AREA *buffer_area;	/* contignous area of buffers */
};

typedef struct la_act_log LA_ACT_LOG;
struct la_act_log
{
  char path[PATH_MAX];
  int log_vdes;
  LOG_PAGE *hdr_page;
  struct log_header *log_hdr;
  int pgsize;
};

typedef struct la_arv_log LA_ARV_LOG;
struct la_arv_log
{
  char path[PATH_MAX];
  int log_vdes;
  LOG_PAGE *hdr_page;
  struct log_arv_header *log_hdr;
  int arv_num;
};

typedef struct la_item LA_ITEM;
struct la_item
{
  int type;
  char *class_name;
  char *db_user;
  DB_VALUE key;
  LOG_LSA lsa;
  LA_ITEM *next;
};

typedef struct la_apply LA_APPLY;
struct la_apply
{
  int tranid;
  LOG_LSA start_lsa;
  LA_ITEM *head;
  LA_ITEM *tail;
};

typedef struct la_commit LA_COMMIT;
struct la_commit
{
  int type;			/* transaction state -
				 * LOG_COMMIT or LOG_UNLOCK_COMMIT */
  int tranid;			/* transaction id */
  LOG_LSA log_lsa;		/* LSA of LOG_COMMIT or
				 * LSA of LOG_UNLOCK_COMMIT */
  time_t master_time;		/* commit time at the server site */
  LA_COMMIT *next;
};

/* Log applier info */
typedef struct la_info LA_INFO;
struct la_info
{
  /* log info */
  char log_path[PATH_MAX];
  LA_ACT_LOG act_log;
  LA_ARV_LOG arv_log;
  int last_file_state;

  /* map info */
  LOG_LSA final_lsa;
  LOG_LSA last_committed_lsa;
  LA_APPLY **repl_lists;
  int repl_cnt;			/* the # of elements of repl_lists */
  int cur_repl;			/* the index of the current repl_lists */
  int total_rows;		/* the # of rows that were replicated */
  int prev_total_rows;		/* the previous # of total_rows */
  LA_COMMIT *commit_head;	/* queue list head */
  LA_COMMIT *commit_tail;	/* queue list tail */

  /* slave info */
  char *log_data;
  char *rec_type;
  LOG_ZIP *undo_unzip_ptr;
  LOG_ZIP *redo_unzip_ptr;
  time_t old_time;
  int apply_state;

  /* master info */
  LA_CACHE_PB *cache_pb;
  int cache_buffer_size;
  bool last_is_end_of_record;
  bool is_end_of_record;
  int last_server_state;
};

typedef struct la_ovf_first_part LA_OVF_FIRST_PART;
struct la_ovf_first_part
{
  VPID next_vpid;
  int length;
  char data[1];			/* Really more than one */
};
typedef struct la_ovf_rest_parts LA_OVF_REST_PARTS;
struct la_ovf_rest_parts
{
  VPID next_vpid;
  char data[1];			/* Really more than one */
};
/* use overflow page list to reduce memory copy overhead. */
typedef struct la_ovf_page_list LA_OVF_PAGE_LIST;
struct la_ovf_page_list
{
  char *rec_type;		/* record type */
  char *data;			/* overflow page data: header + real data */
  int length;			/* total length of data */
  LA_OVF_PAGE_LIST *next;	/* next page */
};


/* Global variable for LA */
LA_INFO la_Info;
static bool la_applier_need_shutdown = false;


static void la_shutdown_by_signal ();
static int la_log_phypageid (PAGEID logical_pageid);
static int la_log_io_open (const char *vlabel, int flags, int mode);
static int la_log_io_read (char *vname, int vdes, void *io_pgptr,
			   PAGEID pageid, int pagesize);
static int la_log_fetch_from_archive (PAGEID pageid, char *data);
static int la_log_fetch (PAGEID pageid, LA_CACHE_BUFFER * cache_buffer);
static int la_expand_cache_log_buffer (LA_CACHE_PB * cache_pb, int slb_cnt,
				       int slb_size, int def_buf_size);
static LA_CACHE_BUFFER *la_cache_buffer_replace (LA_CACHE_PB * cache_pb,
						 PAGEID pageid,
						 int io_pagesize,
						 int def_buf_size);
static LA_CACHE_BUFFER *la_get_page_buffer (PAGEID pageid);
static LOG_PAGE *la_get_page (PAGEID pageid);
static void la_release_page_buffer (PAGEID pageid);
static void la_invalidate_page_buffer (LA_CACHE_BUFFER * cache_buf,
				       bool release);
static int la_find_last_applied_lsa (LOG_LSA * lsa, const char *log_path,
				     LA_ACT_LOG * act_log);
static int la_update_last_applied_lsa (LOG_LSA * lsa, const char *log_path);
static unsigned int la_pid_hash (const void *key_pid, unsigned int htsize);
static int la_pid_hash_cmpeq (const void *key_pid1, const void *key_pid2);
static LA_CACHE_PB *la_init_cache_pb (void);
static int la_init_cache_log_buffer (LA_CACHE_PB * cache_pb, int slb_cnt,
				     int slb_size, int def_buf_size);
static int la_fetch_log_hdr (LA_ACT_LOG * act_log);
static int la_find_log_pagesize (LA_ACT_LOG * act_log,
				 const char *logpath, const char *dbname);
static bool la_apply_pre (LOG_LSA * final);
static int la_does_page_exist (PAGEID pageid);
static int la_init_repl_lists (bool need_realloc);
static LA_APPLY *la_find_apply_list (int tranid);
static void la_log_copy_fromlog (char *rec_type, char *area, int length,
				 PAGEID log_pageid, PGLENGTH log_offset,
				 LOG_PAGE * log_pgptr);
static int la_add_repl_item (LA_APPLY * apply, LOG_LSA lsa);
static int la_set_repl_log (LOG_PAGE * log_pgptr, int log_type, int tranid,
			    LOG_LSA * lsa);
static int la_add_unlock_commit_log (int tranid, LOG_LSA * lsa);
static time_t la_retrieve_eot_time (LOG_PAGE * pgptr, LOG_LSA * lsa);
static int la_set_commit_log (int tranid, LOG_LSA * lsa, time_t master_time);
static int la_apply_delete_log (LA_ITEM * item);
static int la_get_current (OR_BUF * buf, SM_CLASS * sm_class,
			   int bound_bit_flag, DB_OTMPL * def,
			   DB_VALUE * key);
static int la_disk_to_obj (MOBJ classobj, RECDES * record, DB_OTMPL * def,
			   DB_VALUE * key);
static int la_get_log_data (struct log_rec *lrec, LOG_LSA * lsa,
			    LOG_PAGE * pgptr, unsigned int match_rcvindex,
			    unsigned int *rcvindex, void **logs,
			    char **rec_type, char **data, int *d_length);
static int la_get_overflow_recdes (struct log_rec *lrec, void *logs,
				   char **area, int *length,
				   unsigned int rcvindex);
static int la_get_next_update_log (struct log_rec *prev_lrec,
				   LOG_PAGE * pgptr, void **logs,
				   char **rec_type, char **data,
				   int *d_length);
static int la_get_relocation_recdes (struct log_rec *lrec, LOG_PAGE * pgptr,
				     unsigned int match_rcvindex, void **logs,
				     char **rec_type, char **data,
				     int *d_length);
static int la_get_recdes (LOG_LSA * lsa, LOG_PAGE * pgptr, RECDES * recdes,
			  unsigned int *rcvindex, char *log_data,
			  char *rec_type, bool * ovfyn);
static int la_apply_update_log (LA_ITEM * item);
static int la_update_query_execute (const char *sql);
static int la_apply_schema_log (LA_ITEM * item);
static void la_clear_repl_item (LA_APPLY * repl_list);
static int la_apply_repl_log (int tranid, int *total_rows);
static int la_apply_commit_list (LOG_LSA * lsa);
static void la_clear_repl_item_by_tranid (int tranid);
static int la_log_record_process (struct log_rec *lrec,
				  LOG_LSA * final, LOG_PAGE * pg_ptr);
static int la_change_state ();
static int la_log_commit ();
static int la_check_time_commit (struct timeval *time,
				 unsigned int threshold);
static void la_init ();
static void la_shutdown ();


/*
 * la_shutdown_by_signal() - When the process catches the SIGTERM signal,
 *                                it does the shutdown process.
 *   return: none
 *
 * Note:
 *        set the "need_shutdown" flag as true, then each threads would
 *        process "shutdown"
 */
static void
la_shutdown_by_signal ()
{
  la_applier_need_shutdown = true;

  er_log_debug (ARG_FILE_LINE, "shutdown requested by signal.");

  return;
}


/*
 * la_log_phypageid() - get the physical page id from the logical pageid
 *   return: physical page id
 *   logical_pageid : logical page id
 *
 * Note:
 *   active log      0, 1, 2, .... 4999   (total 5,000 pages)
 *   archive log0
 */
static int
la_log_phypageid (PAGEID logical_pageid)
{
  PAGEID phy_pageid;
  if (logical_pageid == LOGPB_HEADER_PAGE_ID)
    {
      phy_pageid = 0;
    }
  else
    {
      phy_pageid = logical_pageid - la_Info.act_log.log_hdr->fpageid;
      if (phy_pageid >= la_Info.act_log.log_hdr->npages)
	{
	  phy_pageid %= la_Info.act_log.log_hdr->npages;
	}
      else if (phy_pageid < 0)
	{
	  phy_pageid = la_Info.act_log.log_hdr->npages -
	    ((-phy_pageid) % la_Info.act_log.log_hdr->npages);
	}
      phy_pageid++;
    }

  return phy_pageid;
}


/*
 * la_log_io_open() - open a file
 *   return: File descriptor
 *
 * Note:
 */
static int
la_log_io_open (const char *vlabel, int flags, int mode)
{
  int vdes;

  do
    {
      vdes = open64 (vlabel, flags, mode);
    }
  while (vdes == NULL_VOLDES && errno == EINTR);

  return vdes;
}

/*
 * la_log_io_read() - read a page from the disk
 *   return: error code
 *     vname(in): the volume name of the target file
 *     vdes(in): the volume descriptor of the target file
 *     io_pgptr(out): start pointer to be read
 *     pageid(in): page id to read
 *     pagesize(in): page size to wrea
 *
 * Note:
 *     reads a predefined size of page from the disk
 */
static int
la_log_io_read (char *vname, int vdes,
		void *io_pgptr, PAGEID pageid, int pagesize)
{
  int nbytes;
  int remain_bytes = pagesize;
  off64_t offset = ((off64_t) pagesize) * ((off64_t) pageid);
  char *current_ptr = (char *) io_pgptr;

  if (lseek64 (vdes, offset, SEEK_SET) == -1)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			   ER_IO_READ, 2, pageid, vname);
      return ER_FAILED;
    }

  while (remain_bytes > 0)
    {
      /* Read the desired page */
      nbytes = read (vdes, current_ptr, pagesize);

      if (nbytes == 0)
	{
	  /*
	   * This is an end of file.
	   * We are trying to read beyond the allocated disk space
	   */
	  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_PB_BAD_PAGEID, 2, pageid, vname);
	  /* TODO: wait until exist? */
	  LA_SLEEP (0, 100 * 1000);
	  continue;
	}
      else if (nbytes < 0)
	{
	  if (errno == EINTR)
	    {
	      continue;
	    }
	  else
	    {
	      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
				   ER_IO_READ, 2, pageid, vname);
	      return ER_FAILED;
	    }
	}

      remain_bytes -= nbytes;
      current_ptr += nbytes;
    }

  return NO_ERROR;
}


/*
 * la_log_fetch_from_archive() - read the log page from archive
 *   return: error code
 *   pageid: requested pageid
 */
static int
la_log_fetch_from_archive (PAGEID pageid, char *data)
{
  int error = NO_ERROR;
  int guess_num = 0;

  guess_num = pageid / la_Info.act_log.log_hdr->npages;

  if (la_Info.arv_log.arv_num != guess_num)
    {
      if (la_Info.arv_log.log_vdes > 0)
	{
	  close (la_Info.arv_log.log_vdes);
	  la_Info.arv_log.log_vdes = NULL_VOLDES;
	}
      la_Info.arv_log.arv_num = guess_num;
    }

  if (la_Info.arv_log.log_vdes == NULL_VOLDES)
    {
      /* make archive_name */
      fileio_make_log_archive_name (la_Info.arv_log.path,
				    la_Info.log_path,
				    la_Info.act_log.log_hdr->prefix_name,
				    la_Info.arv_log.arv_num);
      /* open the archive file */
      la_Info.arv_log.log_vdes = la_log_io_open (la_Info.arv_log.path,
						 O_RDONLY, 0);
      if (la_Info.arv_log.log_vdes == NULL_VOLDES)
	{
	  er_log_debug (ARG_FILE_LINE,
			"cannot open %s archive for %d page.",
			la_Info.arv_log.path, pageid);
	  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE,
		  ER_LOG_READ, 3, pageid, 0, la_Info.arv_log.path);
	  return ER_LOG_READ;
	}
#if defined (LA_VERBOSE_DEBUG)
      else
	{
	  er_log_debug (ARG_FILE_LINE,
			"archive (%s) has been opened for %d page",
			la_Info.arv_log.path, pageid);
	}
#endif
    }

  /* If this is the frist time to read archive log,
   * read the header info of the target archive
   */
  if (la_Info.arv_log.hdr_page == NULL)
    {
      la_Info.arv_log.hdr_page = (LOG_PAGE *) malloc (la_Info.act_log.pgsize);
      if (la_Info.arv_log.hdr_page == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_OUT_OF_VIRTUAL_MEMORY, 1, la_Info.act_log.pgsize);
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}
    }

  error =
    la_log_io_read (la_Info.arv_log.path,
		    la_Info.arv_log.log_vdes,
		    la_Info.arv_log.hdr_page, 0, la_Info.act_log.pgsize);
  if (error != NO_ERROR)
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE,
	      ER_LOG_READ, 3, pageid, 0, la_Info.arv_log.path);
      error = ER_LOG_READ;
      return error;
    }
  la_Info.arv_log.log_hdr =
    (struct log_arv_header *) la_Info.arv_log.hdr_page->area;

  /* is the right archive file ? */
  if (pageid >= la_Info.arv_log.log_hdr->fpageid
      && pageid <
      la_Info.arv_log.log_hdr->fpageid + la_Info.arv_log.log_hdr->npages)
    {
      error = la_log_io_read (la_Info.arv_log.path,
			      la_Info.arv_log.log_vdes, data,
			      pageid - la_Info.arv_log.log_hdr->fpageid +
			      1, la_Info.act_log.pgsize);
      if (error != NO_ERROR)
	{
	  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE,
		  ER_LOG_READ, 3, pageid,
		  pageid - la_Info.arv_log.log_hdr->fpageid + 1,
		  la_Info.arv_log.path);
	  error = ER_LOG_READ;
	}

      return error;
    }
  /* we have to try the next archive file */
  else
    {
      close (la_Info.arv_log.log_vdes);
      la_Info.arv_log.log_vdes = NULL_VOLDES;
      error = ER_LOG_READ;
    }

  /* Oh Oh, there is no archive to save this page */
  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_NOTIN_ARCHIVE, 1, pageid);

  return ER_LOG_READ;
}

static int
la_log_fetch (PAGEID pageid, LA_CACHE_BUFFER * cache_buffer)
{
  int error = NO_ERROR;
  PAGEID phy_pageid = NULL_PAGEID;
  int retry = 5;

  assert (cache_buffer);

  /* get the physical page id */
  phy_pageid = la_log_phypageid (pageid);

  if (la_Info.act_log.log_hdr->append_lsa.pageid < pageid)
    {
      /* check it again */
      error = la_fetch_log_hdr (&la_Info.act_log);
      if (error != NO_ERROR)
	{
	  return error;
	}

      /* check it again */
      if (la_Info.act_log.log_hdr->append_lsa.pageid < pageid)
	{
	  return ER_LOG_NOTIN_ARCHIVE;
	}
    }

  /* TODO: refactor read the target page */
  if (LA_LOG_IS_IN_ARCHIVE (pageid))
    {
      /* read from the archive log file */
      error =
	la_log_fetch_from_archive (pageid, (char *) &cache_buffer->logpage);
      /* mark that the page is in archive log */
      cache_buffer->in_archive = true;
      if (error != NO_ERROR)
	{
	  return error;
	}
    }
  else
    {
      /* read from the active log file */
      error =
	la_log_io_read (la_Info.act_log.path,
			la_Info.act_log.log_vdes, &cache_buffer->logpage,
			phy_pageid, la_Info.act_log.pgsize);
      /* mark that the page is in active log */
      cache_buffer->in_archive = false;
      if (error != NO_ERROR)
	{
	  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE,
		  ER_LOG_READ, 3, pageid, phy_pageid, la_Info.act_log.path);
	  return ER_LOG_READ;
	}
    }

  /* check the fetched page is not the target page ? */
  while (error == NO_ERROR && retry > 0 &&
	 cache_buffer->logpage.hdr.logical_pageid != pageid)
    {
      /* if the master generates the log archive,
       * re-fetch the log header, try again */
      error = la_fetch_log_hdr (&la_Info.act_log);
      if (error != NO_ERROR)
	{
	  return error;
	}

      /* TODO: refactor read the target page */
      if (LA_LOG_IS_IN_ARCHIVE (pageid))
	{
	  /* read from the archive log file */
	  error =
	    la_log_fetch_from_archive (pageid,
				       (char *) &cache_buffer->logpage);
	  /* mark that the page is in archive log */
	  cache_buffer->in_archive = true;
	  if (error != NO_ERROR)
	    {
	      return error;
	    }
	}
      else
	{
	  /* read from the active log file */
	  error =
	    la_log_io_read (la_Info.act_log.path,
			    la_Info.act_log.log_vdes, &cache_buffer->logpage,
			    phy_pageid, la_Info.act_log.pgsize);
	  /* mark that the page is in active log */
	  cache_buffer->in_archive = false;
	  if (error != NO_ERROR)
	    {
	      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE,
		      ER_LOG_READ, 3, pageid, phy_pageid,
		      la_Info.act_log.path);
	      return ER_LOG_READ;
	    }
	}

      if (cache_buffer->logpage.hdr.logical_pageid != pageid)
	{
	  LA_SLEEP (0, 100 * 1000);
	}

      retry--;
    }

  if (retry <= 0 || la_Info.act_log.log_hdr->append_lsa.pageid < pageid)
    {
#if defined (LA_VERBOSE_DEBUG)
      /* it will nagging you */
      er_log_debug (ARG_FILE_LINE, "log pageid %d is not exist", pageid);
#endif
      return ER_LOG_NOTIN_ARCHIVE;
    }

  /* now here, we got log page : */
  cache_buffer->pageid = pageid;
  cache_buffer->phy_pageid = phy_pageid;

  return error;
}

/*
 * la_expand_cache_log_buffer() - expand cache log buffer
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
la_expand_cache_log_buffer (LA_CACHE_PB * cache_pb, int slb_cnt,
			    int slb_size, int def_buf_size)
{
  int i;
  int total_buffers;
  int size;
  int bufid;
  LA_CACHE_BUFFER_AREA *area = NULL;
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

  while (slb_cnt > (int) LA_MAX_NUM_CONTIGUOS_BUFFERS (slb_size))
    {
      if ((error = la_expand_cache_log_buffer (cache_pb,
					       LA_MAX_NUM_CONTIGUOS_BUFFERS
					       (slb_size), slb_size,
					       def_buf_size)) != NO_ERROR)
	{
	  return error;
	}
      slb_cnt -= LA_MAX_NUM_CONTIGUOS_BUFFERS (slb_size);
    }

  if (slb_cnt > 0)
    {
      total_buffers = cache_pb->num_buffers + slb_cnt;

      cache_pb->log_buffer = realloc (cache_pb->log_buffer,
				      total_buffers *
				      DB_SIZEOF (LA_CACHE_BUFFER *));
      if (cache_pb->log_buffer == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
		  1, total_buffers * DB_SIZEOF (LA_CACHE_BUFFER *));
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}

      size = ((slb_cnt * slb_size) + DB_SIZEOF (LA_CACHE_BUFFER_AREA));
      area = malloc (size);
      if (area == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
		  1, size);
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}

      memset (area, 0, size);
      area->buffer_area = ((LA_CACHE_BUFFER *) ((char *) area
						+
						DB_SIZEOF
						(LA_CACHE_BUFFER_AREA)));
      area->next = cache_pb->buffer_area;
      for (i = 0, bufid = cache_pb->num_buffers; i < slb_cnt; bufid++, i++)
	{
	  cache_pb->log_buffer[bufid]
	    = (LA_CACHE_BUFFER *) ((char *) area->buffer_area + slb_size * i);
	}
      cache_pb->buffer_area = area;
      cache_pb->num_buffers = total_buffers;
    }

  return NO_ERROR;
}

static LA_CACHE_BUFFER *
la_cache_buffer_replace (LA_CACHE_PB * cache_pb, PAGEID pageid,
			 int io_pagesize, int def_buf_size)
{
  LA_CACHE_BUFFER *cache_buffer = NULL;
  int error = NO_ERROR;
  int i, num_unfixed = 1;

  while (num_unfixed > 0)
    {
      num_unfixed = 0;

      for (i = 0; i < cache_pb->num_buffers; i++)
	{
	  /* set the target buffer */
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
		  error = la_log_fetch (pageid, cache_buffer);
		  if (error != NO_ERROR)
		    {
		      /* TODO: error */
		      return NULL;
		    }

		  cache_buffer->fix_count = 0;
		  return cache_buffer;
		}
	    }
	}
    }

  error = la_expand_cache_log_buffer (cache_pb, -1,
				      SIZEOF_LA_CACHE_LOG_BUFFER
				      (io_pagesize), def_buf_size);
  if (error != NO_ERROR)
    {
      return NULL;
    }

  return la_cache_buffer_replace (cache_pb, pageid, io_pagesize,
				  def_buf_size);
}

static LA_CACHE_BUFFER *
la_get_page_buffer (PAGEID pageid)
{
  LA_CACHE_PB *cache_pb = la_Info.cache_pb;
  LA_CACHE_BUFFER *cache_buf = NULL;

  /* find the target page in the cache buffer */
  cache_buf = (LA_CACHE_BUFFER *) mht_get (cache_pb->hash_table,
					   (void *) &pageid);

  if (cache_buf != NULL && cache_buf->drop == false)
    {
      cache_buf->fix_count++;
    }
  else
    {
      /* replace cache */
      cache_buf = la_cache_buffer_replace (cache_pb, pageid,
					   la_Info.act_log.pgsize,
					   la_Info.cache_buffer_size);
      if (cache_buf == NULL
	  || cache_buf->logpage.hdr.logical_pageid != pageid)
	{
	  return NULL;
	}

      if (cache_buf->pageid != 0)
	{
	  (void) mht_rem (cache_pb->hash_table,
			  &cache_buf->pageid, NULL, NULL);
	}

      if (mht_put (cache_pb->hash_table,
		   &cache_buf->pageid, cache_buf) == NULL)
	{
	  return NULL;
	}

      /* set data */
      cache_buf->fix_count = 1;
      cache_buf->drop = false;
    }

  return cache_buf;
}

static LOG_PAGE *
la_get_page (PAGEID pageid)
{
  LA_CACHE_BUFFER *buf = NULL;

  do
    {
      buf = la_get_page_buffer (pageid);
    }
  while (buf == NULL);		/* we must get this page */

  return &buf->logpage;
}

/*
 * la_release_page_buffer() - decrease the fix_count of the target buffer
 *   return: none
 *   pageid(in): the target page id
 *
 * Note:
 *   if cache buffer's fix_count < 0 then programing error.
 */
static void
la_release_page_buffer (PAGEID pageid)
{
  LA_CACHE_PB *cache_pb = la_Info.cache_pb;
  LA_CACHE_BUFFER *cache_buf = NULL;

  cache_buf = (LA_CACHE_BUFFER *) mht_get (cache_pb->hash_table,
					   (void *) &pageid);
  if (cache_buf != NULL)
    {
      if (cache_buf->fix_count-- <= 0)
	{
	  cache_buf->fix_count = 0;
	}
      cache_buf->recently_freed = true;
    }
}

/*
 * la_invalidate_page_buffer() - decrease the fix_count and drop the target buffer from cache
 *   return: none
 *   cache_buf(in): cached page buffer
 *   release(in): release page flag
 *
 * Note:
 */
static void
la_invalidate_page_buffer (LA_CACHE_BUFFER * cache_buf, bool release)
{
  assert (cache_buf);

  /* it should be refetched */
  cache_buf->drop = true;

  if (release == true)
    {
      if (cache_buf->fix_count-- <= 0)
	{
	  cache_buf->fix_count = 0;
	}
      cache_buf->recently_freed = true;
    }
}

/*
 * la_find_last_applied_lsa() - Find out the last updated LSA
 *   return: NO_ERROR or error code
 *
 * Note:
 *   Find out the last LSA applied of the target slave.
 */
static int
la_find_last_applied_lsa (LOG_LSA * lsa,
			  const char *log_path, LA_ACT_LOG * act_log)
{
  int error = NO_ERROR;
  bool new_row = false;
  DB_QUERY_RESULT *result = NULL;
  DB_QUERY_ERROR error_stats;
  DB_VALUE *value;
  int col_cnt, i;
  char *sql = NULL;

  LSA_SET_NULL (lsa);

  if (db_find_class ("log_applier_info") == NULL)
    {
      /* class does not exist; create it */
      asprintf (&sql, "CREATE CLASS log_applier_info "
		"(pageid INT, offset INT, logpath STRING);");
      error = db_execute (sql, NULL, NULL);
      free_and_init (sql);
      if (error < 0 && error != ER_LC_CLASSNAME_EXIST)
	{
	  return error;
	}
    }

  asprintf (&sql, "SELECT pageid, offset "
	    "FROM log_applier_info WHERE logpath = '%s';", log_path);

  error = db_execute (sql, &result, &error_stats);
  free_and_init (sql);
  if (error > 0)
    {
      int pos;
      pos = db_query_first_tuple (result);

      switch (pos)
	{
	case DB_CURSOR_SUCCESS:
	  col_cnt = db_query_column_count (result);
	  if (col_cnt < 2)
	    {
	      return er_errid ();
	    }

	  value = malloc (DB_SIZEOF (DB_VALUE) * col_cnt);
	  if (value == NULL)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_OUT_OF_VIRTUAL_MEMORY, 1,
		      DB_SIZEOF (DB_VALUE) * col_cnt);
	      return ER_OUT_OF_VIRTUAL_MEMORY;
	    }

	  error = db_query_get_tuple_valuelist (result, col_cnt, value);
	  if (error != NO_ERROR)
	    {
	      return error;
	    }

	  lsa->pageid = DB_GET_INTEGER (&value[0]);
	  lsa->offset = DB_GET_INTEGER (&value[1]);

	  for (i = 0; i < col_cnt; i++)
	    {
	      db_value_clear (&value[i]);
	    }
	  free_and_init (value);
	  break;
	case DB_CURSOR_END:
	case DB_CURSOR_ERROR:
	  /* not found; insert a row */
	  new_row = true;
	  break;
	default:
	  /* error */
	  error = pos;
	  break;
	}
    }
  else if (error == 0)
    {
      /* not found; insert a row */
      new_row = true;
    }

  db_query_end (result);

  if (error >= 0 && new_row == true)
    {
      asprintf (&sql,
		"INSERT INTO log_applier_info (pageid, offset, logpath) "
		"VALUES (%d, %d, '%s')", act_log->log_hdr->eof_lsa.pageid,
		act_log->log_hdr->eof_lsa.offset, log_path);

      error = la_update_query_execute (sql);
      free_and_init (sql);
    }

  if (error < 0)
    {
      return error;
    }

  db_commit_transaction ();

  if (LSA_ISNULL (lsa))
    {
      LSA_COPY (lsa, &act_log->log_hdr->eof_lsa);
    }

  return error;
}

/*
 * la_update_last_applied_lsa()
 *   return: NO_ERROR or error code
 *
 * Note:
 *     called by APPLY thread
 */
static int
la_update_last_applied_lsa (LOG_LSA * lsa, const char *log_path)
{
  int error = NO_ERROR;
  char *sql = NULL;

  er_clear ();
  asprintf (&sql, "UPDATE log_applier_info "
	    "SET pageid=%d,offset=%d WHERE logpath='%s';",
	    lsa->pageid, lsa->offset, log_path);
  error = la_update_query_execute (sql);
  free_and_init (sql);
  if (error == NO_ERROR)
    {
      error = db_commit_transaction ();
    }

  return error;
}

/*
 * la_pid_hash() - hash a page identifier
 *   return: hash value
 *   key_pid : page id to hash
 *   htsize: Size of hash table
 */
static unsigned int
la_pid_hash (const void *key_pid, unsigned int htsize)
{
  const PAGEID *pid = (PAGEID *) key_pid;

  return (*pid) % htsize;
}

/*
 * la_pid_hash_cmpeq() - Compare two pageid keys for hashing.
 *   return: int (key_pid1 == ey_vpid2 ?)
 *   key_pid1: First key
 *   key_pid2: Second key
 */
static int
la_pid_hash_cmpeq (const void *key_pid1, const void *key_pid2)
{
  const PAGEID *pid1 = key_pid1;
  const PAGEID *pid2 = key_pid2;

  return ((pid1 == pid2) || (*pid1 == *pid2));
}

/*
 * la_init_cache_pb() - initialize the cache page buffer area
 *   return: the allocated pointer to a cache page buffer
 *
 * Note:
 */
static LA_CACHE_PB *
la_init_cache_pb (void)
{
  LA_CACHE_PB *cache_pb;

  cache_pb = (LA_CACHE_PB *) malloc (DB_SIZEOF (LA_CACHE_PB));
  if (cache_pb == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
	      DB_SIZEOF (LA_CACHE_PB));
      return NULL;
    }

  cache_pb->hash_table = NULL;
  cache_pb->log_buffer = NULL;
  cache_pb->num_buffers = 0;
  cache_pb->clock_hand = 0;
  cache_pb->buffer_area = NULL;

  return (cache_pb);
}

/*
 * la_init_cache_log_buffer() - Initialize the cache log buffer area of
 *                                a cache page buffer
 *   return: NO_ERROR or ER_OUT_OF_VIRTUAL_MEMORY
 *   cache_pb : cache page buffer pointer
 *   slb_cnt : the # of cache log buffers per cache page buffer
 *   slb_size : size of CACHE_LOG_BUFFER
 *   def_buf_size: default size of cache log buffer
 *
 * Note:
 *         : allocate the cache page buffer area
 *         : the size of page buffer area is determined after reading the
 *           log header, so we split the "initialize" and "allocate" phase.
 */
static int
la_init_cache_log_buffer (LA_CACHE_PB * cache_pb, int slb_cnt,
			  int slb_size, int def_buf_size)
{
  int error = NO_ERROR;

  error = la_expand_cache_log_buffer (cache_pb, slb_cnt, slb_size,
				      def_buf_size);

  cache_pb->hash_table =
    mht_create ("log applier cache log buffer hash table",
		cache_pb->num_buffers * 8, la_pid_hash, la_pid_hash_cmpeq);
  if (cache_pb->hash_table == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
	      cache_pb->num_buffers * 8, la_pid_hash, la_pid_hash_cmpeq);
      error = ER_OUT_OF_VIRTUAL_MEMORY;
    }

  return error;
}

static int
la_fetch_log_hdr (LA_ACT_LOG * act_log)
{
  int error = NO_ERROR;

  error = la_log_io_read (act_log->path, act_log->log_vdes,
			  (void *) act_log->hdr_page, 0, act_log->pgsize);
  if (error != NO_ERROR)
    {
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_LOG_READ, 3, 0, 0, act_log->path);
      return ER_LOG_READ;
    }

  act_log->log_hdr = (struct log_header *) (act_log->hdr_page->area);

  return error;
}

static int
la_find_log_pagesize (LA_ACT_LOG * act_log,
		      const char *logpath, const char *dbname)
{
  int error = NO_ERROR;

  /* set active log name */
  fileio_make_log_active_name (act_log->path, logpath, dbname);

  /* read active log file to get the io page size */
  /* wait until act_log is opened */
  do
    {
      act_log->log_vdes = la_log_io_open (act_log->path, O_RDONLY, 0);
      if (act_log->log_vdes == NULL_VOLDES)
	{
	  er_log_debug (ARG_FILE_LINE,
			"Active log file(%s) is not exist. waiting...",
			act_log->path);
	  /* TODO: is it error? */
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			       ER_LOG_MOUNT_FAIL, 1, act_log->path);
	  error = ER_LOG_MOUNT_FAIL;

	  LA_SLEEP (1, 1);
	}
      else
	{
	  error = NO_ERROR;
	  break;
	}
    }
  while (!la_applier_need_shutdown);

  if (error != NO_ERROR)
    {
      return error;
    }

  act_log->hdr_page = (LOG_PAGE *) malloc (LA_DEFAULT_LOG_PAGE_SIZE);
  if (act_log->hdr_page == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_OUT_OF_VIRTUAL_MEMORY, 1, LA_DEFAULT_LOG_PAGE_SIZE);
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  error = la_log_io_read (act_log->path, act_log->log_vdes,
			  (char *) act_log->hdr_page, 0,
			  LA_DEFAULT_LOG_PAGE_SIZE);
  if (error != NO_ERROR)
    {
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_LOG_READ, 3, 0, 0, act_log->path);
      return error;
    }

  act_log->log_hdr = (struct log_header *) act_log->hdr_page->area;
  act_log->pgsize = act_log->log_hdr->db_iopagesize;
  /* TODO: check iopage size is valid */
  if (act_log->pgsize > LA_DEFAULT_LOG_PAGE_SIZE)
    {
      act_log->hdr_page =
	(LOG_PAGE *) realloc (act_log->hdr_page, act_log->pgsize);
    }

  return error;
}


static bool
la_apply_pre (LOG_LSA * final)
{
  LSA_COPY (final, &la_Info.final_lsa);

  if (la_Info.log_data == NULL)
    {
      la_Info.log_data = (char *) malloc (la_Info.act_log.pgsize);
      if (la_Info.log_data == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_OUT_OF_VIRTUAL_MEMORY, 1, la_Info.act_log.pgsize);
	  return false;
	}
    }

  if (la_Info.rec_type == NULL)
    {
      la_Info.rec_type = (char *) malloc (DB_SIZEOF (INT16));
      if (la_Info.rec_type == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_OUT_OF_VIRTUAL_MEMORY, 1, DB_SIZEOF (INT16));
	  return false;
	}
    }

  if (la_Info.undo_unzip_ptr == NULL)
    {
      la_Info.undo_unzip_ptr = log_zip_alloc (la_Info.act_log.pgsize, false);
      if (la_Info.undo_unzip_ptr == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_OUT_OF_VIRTUAL_MEMORY, 1, la_Info.act_log.pgsize);
	  return false;
	}
    }

  if (la_Info.redo_unzip_ptr == NULL)
    {
      la_Info.redo_unzip_ptr = log_zip_alloc (la_Info.act_log.pgsize, false);
      if (la_Info.undo_unzip_ptr == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_OUT_OF_VIRTUAL_MEMORY, 1, la_Info.act_log.pgsize);
	  return false;
	}
    }

  return true;
}

/*
 * la_does_page_exist() - check whether page is exist
 *   return:
 *     pageid(in): the target page id
 *
 * Note
 */
static int
la_does_page_exist (PAGEID pageid)
{
  LA_CACHE_BUFFER *log_buffer;
  int log_exist = LA_PAGE_DOESNOT_EXIST;

  log_buffer = la_get_page_buffer (pageid);
  if (log_buffer != NULL)
    {
      if (log_buffer->pageid == pageid &&
	  log_buffer->logpage.hdr.logical_pageid == pageid &&
	  log_buffer->logpage.hdr.offset > NULL_OFFSET)
	{
	  log_exist = log_buffer->in_archive ?
	    LA_PAGE_EXST_IN_ARCHIVE_LOG : LA_PAGE_EXST_IN_ACTIVE_LOG;
	  la_release_page_buffer (pageid);
	}
      else
	{
	  la_invalidate_page_buffer (log_buffer, true);
	}
    }

  return log_exist;
}

/*
 * la_init_repl_lists() - Initialize the replication lists
 *   return: NO_ERROR or error code
 *   need_realloc : yes when realloc
 *
 * Note:
 *         repl_lists is an array of replication items to be applied.
 *         We maintain repl_lists for a transaction.
 *         This function initialize the repl_list.
 */
static int
la_init_repl_lists (bool need_realloc)
{
  int i, j;
  int error = NO_ERROR;

  if (need_realloc == false)
    {
      la_Info.repl_lists =
	malloc (DB_SIZEOF (LA_APPLY *) * LA_REPL_LIST_COUNT);
      la_Info.repl_cnt = LA_REPL_LIST_COUNT;
      la_Info.cur_repl = 0;
      j = 0;
    }
  else
    {
      la_Info.repl_lists
	= realloc (la_Info.repl_lists,
		   DB_SIZEOF (LA_APPLY *) *
		   (LA_REPL_LIST_COUNT + la_Info.repl_cnt));
      j = la_Info.repl_cnt;
      la_Info.repl_cnt += LA_REPL_LIST_COUNT;
    }

  if (la_Info.repl_lists == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_OUT_OF_VIRTUAL_MEMORY, 1,
	      DB_SIZEOF (LA_APPLY *) * (LA_REPL_LIST_COUNT +
					la_Info.repl_cnt));
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  for (i = j; i < la_Info.repl_cnt; i++)
    {
      la_Info.repl_lists[i] = malloc (DB_SIZEOF (LA_APPLY));
      if (la_Info.repl_lists[i] == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_OUT_OF_VIRTUAL_MEMORY, 1, DB_SIZEOF (LA_APPLY));
	  error = ER_OUT_OF_VIRTUAL_MEMORY;
	  break;
	}
      la_Info.repl_lists[i]->tranid = 0;
      LSA_SET_NULL (&la_Info.repl_lists[i]->start_lsa);
      la_Info.repl_lists[i]->head = NULL;
      la_Info.repl_lists[i]->tail = NULL;
    }

  if (error != NO_ERROR)
    {
      for (j = 0; j < i; j++)
	{
	  free_and_init (la_Info.repl_lists[i]);
	}
      free_and_init (la_Info.repl_lists);
      return error;
    }

  return error;
}

/*
 * la_find_apply_list() - return the apply list for the target
 *                             transaction id
 *   return: pointer to the target apply list
 *   tranid(in): the target transaction id
 *
 * Note:
 *     When we apply the transaction logs to the slave, we have to take them
 *     in turns of commit order.
 *     So, each slave maintains the apply list per transaction.
 *     And an apply list has one or more replication item.
 *     When the APPLY thread meets the "LOG COMMIT" record, it finds out
 *     the apply list of the target transaction, and apply the replication
 *     items to the slave orderly.
 */
static LA_APPLY *
la_find_apply_list (int tranid)
{
  int i;
  int free_index = -1;

  /* find out the matched index */
  for (i = 0; i < la_Info.cur_repl; i++)
    {
      if (la_Info.repl_lists[i]->tranid == tranid)
	{
	  return la_Info.repl_lists[i];
	}

      /* retreive the free index  for the laster use */
      else if ((free_index < 0) && (la_Info.repl_lists[i]->tranid == 0))
	{
	  free_index = i;
	}
    }

  /* not matched, but we have free space */
  if (free_index >= 0)
    {
      la_Info.repl_lists[free_index]->tranid = tranid;
      return la_Info.repl_lists[free_index];
    }

  /* not matched, no free space */
  if (la_Info.cur_repl == la_Info.repl_cnt)
    {
      /* array is full --> realloc */
      if (la_init_repl_lists (true) == NO_ERROR)
	{
	  la_Info.repl_lists[la_Info.cur_repl]->tranid = tranid;
	  la_Info.cur_repl++;
	  return la_Info.repl_lists[la_Info.cur_repl - 1];
	}
      return NULL;
    }

  /* mot matched, no free space, array is not full */
  la_Info.repl_lists[la_Info.cur_repl]->tranid = tranid;
  la_Info.cur_repl++;
  return la_Info.repl_lists[la_Info.cur_repl - 1];
}

/*
 * la_log_copy_fromlog() - copy a portion of the log
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
la_log_copy_fromlog (char *rec_type, char *area, int length,
		     PAGEID log_pageid, PGLENGTH log_offset,
		     LOG_PAGE * log_pgptr)
{
  int rec_length = DB_SIZEOF (INT16);
  int copy_length;		/* Length to copy into area */
  int t_length;			/* target length  */
  int area_offset = 0;		/* The area offset */
  int error = NO_ERROR;
  LOG_PAGE *pg;
  int release_yn = 0;

  pg = log_pgptr;

  /* filter the record type */
  /* NOTES : in case of overflow page, we don't need to fetch the rectype */
  while (rec_type != NULL && rec_length > 0)
    {
      LA_LOG_READ_ADVANCE_WHEN_DOESNT_FIT (error, 0, log_offset, log_pageid,
					   pg, release_yn);
      copy_length =
	((log_offset + rec_length <
	  LOGAREA_SIZE) ? rec_length : LOGAREA_SIZE - log_offset);
      memcpy (rec_type + area_offset, (char *) (pg)->area + log_offset,
	      copy_length);
      rec_length -= copy_length;
      area_offset += copy_length;
      log_offset += copy_length;
      length = length - DB_SIZEOF (INT16);
    }

  area_offset = 0;
  t_length = length;

  /* The log data is not contiguous */
  while (t_length > 0)
    {
      LA_LOG_READ_ADVANCE_WHEN_DOESNT_FIT (error, 0, log_offset, log_pageid,
					   pg, release_yn);
      if (pg == NULL)
	{
	  /* TODO: huh? what happend */
	  break;
	}
      copy_length = ((log_offset + t_length < LOGAREA_SIZE) ? t_length
		     : LOGAREA_SIZE - log_offset);
      memcpy (area + area_offset, (char *) (pg)->area + log_offset,
	      copy_length);
      t_length -= copy_length;
      area_offset += copy_length;
      log_offset += copy_length;
    }

  if (release_yn == 1)
    {
      la_release_page_buffer (pg->hdr.logical_pageid);
    }

}

/*
 * la_add_repl_item() - add the replication item into the apply list
 *   return: NO_ERROR or REPL_IO_ERROR or REPL_SOCK_ERROR
 *   apply(in/out): log apply list
 *   lsa(in): the target LSA of the log
 *
 * Note:
 */
static int
la_add_repl_item (LA_APPLY * apply, LOG_LSA lsa)
{
  LA_ITEM *item;
  int error = NO_ERROR;

  item = malloc (DB_SIZEOF (LA_ITEM));
  if (item == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_OUT_OF_VIRTUAL_MEMORY, 1, DB_SIZEOF (LA_ITEM));
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  LSA_COPY (&item->lsa, &lsa);
  item->next = NULL;

  if (apply->head == NULL)
    {
      apply->head = apply->tail = item;
    }
  else
    {
      apply->tail->next = item;
      apply->tail = item;
    }

  return error;
}

/*
 * la_set_repl_log() - insert the replication item into the apply list
 *   return: NO_ERROR or error code
 *   log_pgptr : pointer to the log page
 *   tranid: the target transaction id
 *   lsa  : the target LSA of the log
 *
 * Note:
 *     APPLY thread traverses the transaction log pages, and finds out the
 *     REPLICATION LOG record. If it meets the REPLICATION LOG record,
 *     it adds that record to the apply list for later use.
 *     When the APPLY thread meets the LOG COMMIT record, it applies the
 *     inserted REPLICAION LOG records to the slave.
 */
static int
la_set_repl_log (LOG_PAGE * log_pgptr, int log_type, int tranid,
		 LOG_LSA * lsa)
{
  struct log_replication *repl_log;
  LOG_PAGE *log_pgptr2 = log_pgptr;
  PGLENGTH target_offset;
  char *ptr;
  LA_APPLY *apply;
  int error = NO_ERROR;
  int length;			/* type change PGLENGTH -> int */
  int t_pageid;
  char *class_name;
  char *str_value;
  int release_yn = 0;
  char *area;

  t_pageid = lsa->pageid;
  target_offset = DB_SIZEOF (struct log_rec) + lsa->offset;
  length = DB_SIZEOF (struct log_replication);

  LA_LOG_READ_ALIGN (error, target_offset, t_pageid, log_pgptr2, release_yn);
  if (error != NO_ERROR)
    {
      return error;
    }
  LA_LOG_READ_ADVANCE_WHEN_DOESNT_FIT (error, length, target_offset, t_pageid,
				       log_pgptr2, release_yn);
  if (error != NO_ERROR)
    {
      return error;
    }

  repl_log =
    (struct log_replication *) ((char *) log_pgptr2->area + target_offset);
  target_offset += length;
  length = repl_log->length;

  LA_LOG_READ_ALIGN (error, target_offset, t_pageid, log_pgptr2, release_yn);
  if (error != NO_ERROR)
    {
      return error;
    }

  area = (char *) malloc (length);
  la_log_copy_fromlog (NULL, area, length, t_pageid, target_offset,
		       log_pgptr2);

  apply = la_find_apply_list (tranid);
  if (apply == NULL)
    {
      free_and_init (area);
      return er_errid ();
    }

  switch (log_type)
    {
    case LOG_REPLICATION_DATA:
      {
	ptr = or_unpack_string (area, &class_name);

	error = la_add_repl_item (apply, repl_log->lsa);
	apply->tail->type = PT_UPDATE;
	if (error != NO_ERROR)
	  {
	    free_and_init (area);
	    return error;
	  }
	ptr = or_unpack_value (ptr, &apply->tail->key);
	apply->tail->class_name = class_name;
	break;
      }
    case LOG_REPLICATION_SCHEMA:
      {
	error = la_add_repl_item (apply, repl_log->lsa);
	if (error != NO_ERROR)
	  {
	    free_and_init (area);
	    return error;;
	  }
	ptr = or_unpack_int (area, &apply->tail->type);
	ptr = or_unpack_string (ptr, &apply->tail->class_name);
	ptr = or_unpack_string (ptr, &str_value);
	db_make_string (&apply->tail->key, str_value);
	ptr = or_unpack_string (ptr, &apply->tail->db_user);
	apply->tail->key.need_clear = true;
	break;
      }
    default:
      /* unknown log type */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      free_and_init (area);
      return ER_GENERIC_ERROR;
    }

  if (release_yn == 1)
    {
      la_release_page_buffer (log_pgptr2->hdr.logical_pageid);
    }

  free_and_init (area);
  return error;
}

/*
 * la_add_unlock_commit_log() - add the unlock_commit log to the
 *                                   commit list
 *   return: NO_ERROR or error code
 *   tranid: the target transaction id
 *   lsa   : the target LSA of the log
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
 */
static int
la_add_unlock_commit_log (int tranid, LOG_LSA * lsa)
{
  LA_APPLY *apply;
  LA_COMMIT *commit, *tmp;
  int error = NO_ERROR;

  apply = la_find_apply_list (tranid);
  if (apply == NULL)
    {
      return er_errid ();
    }

  for (tmp = la_Info.commit_head; tmp; tmp = tmp->next)
    {
      if (tmp->tranid == tranid)
	return error;
    }

  commit = malloc (DB_SIZEOF (LA_COMMIT));
  if (apply == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_OUT_OF_VIRTUAL_MEMORY, 1, DB_SIZEOF (LA_COMMIT));
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }
  commit->next = NULL;
  commit->type = LOG_UNLOCK_COMMIT;
  LSA_COPY (&commit->log_lsa, lsa);
  commit->tranid = tranid;

  if (la_Info.commit_head == NULL && la_Info.commit_tail == NULL)
    {
      la_Info.commit_head = commit;
      la_Info.commit_tail = commit;
    }
  else
    {
      la_Info.commit_tail->next = commit;
      la_Info.commit_tail = commit;
    }

  return error;
}

/*
 * la_retrieve_eot_time() - Retrieve the timestamp of End of Transaction
 *   return: NO_ERROR or error code
 *   log_pgptr : pointer to the log page
 *
 * Note:
 */
static time_t
la_retrieve_eot_time (LOG_PAGE * pgptr, LOG_LSA * lsa)
{
  int error = NO_ERROR;
  struct log_donetime *donetime;
  PAGEID pageid;
  PGLENGTH offset;
  LOG_PAGE *pg;
  int release_yn = 0;

  pageid = lsa->pageid;
  offset = DB_SIZEOF (struct log_rec) + lsa->offset;

  pg = pgptr;

  LA_LOG_READ_ALIGN (error, offset, pageid, pg, release_yn);
  if (error != NO_ERROR)
    {
      /* cannot get eot time */
      return 0;
    }

  LA_LOG_READ_ADVANCE_WHEN_DOESNT_FIT (error, DB_SIZEOF (*donetime), offset,
				       pageid, pg, release_yn);
  if (error != NO_ERROR)
    {
      /* cannot get eot time */
      return 0;
    }
  donetime = (struct log_donetime *) ((char *) pg->area + offset);

  if (release_yn == 1)
    {
      la_release_page_buffer (pg->hdr.logical_pageid);
    }

  return donetime->at_time;
}

/*
 * la_set_commit_log() - update the unlock_commit log to the commit list
 *   return: NO_ERROR or error code
 *   tranid : the target transaction id
 *   lsa : the target LSA of the log
 *
 * Note:
 *     APPLY thread traverses the transaction log pages, and finds out the
 *     REPLICATION LOG record. If it meets the REPLICATION LOG record,
 *     it adds that record to the apply list for later use.
 *     When the APPLY thread meets the LOG COMMIT record, it applies the
 *     inserted REPLICAION LOG records into the slave.
 *     The APPLY thread applies transaction not in sequence of
 *     LOG_COMMIT record, but in regular sequence of LOG_UNLOCK_COMMIT record.
 *     When the APPLY thread meet the LOG_COMMIT record, It applies
 *     REPLICATION LOC record to the slave in regular sequence of commit list.
 *
 * NOTE
 */
static int
la_set_commit_log (int tranid, LOG_LSA * lsa, time_t master_time)
{
  LA_COMMIT *commit;

  commit = la_Info.commit_head;
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
 * la_apply_delete_log() - apply the delete log to the target slave
 *   return: NO_ERROR or error code
 *   item(in): replication item
 *
 * Note:
 */
static int
la_apply_delete_log (LA_ITEM * item)
{
  DB_OBJECT *class_obj, *obj;
  int error = NO_ERROR;

  /* find out class object by class name */
  class_obj = db_find_class (item->class_name);
  if (class_obj == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      return ER_GENERIC_ERROR;
    }

  /* find out object by primary key */
  obj = obj_find_object_by_pkey (class_obj, &item->key, AU_FETCH_UPDATE);

  /* delete this object */
  if (obj)
    {
      error = db_drop (obj);
      if (error == ER_NET_CANT_CONNECT_SERVER || error == ER_OBJ_NO_CONNECT)
	{
	  return ER_NET_CANT_CONNECT_SERVER;
	}
      if (error != NO_ERROR)
	{
	  return ER_GENERIC_ERROR;
	}
    }

  return error;
}

/*
 * la_get_current()
 *   return: NO_ERROR or error code
 *
 * Note:
 *     Analyze the record description, get the value for each attribute,
 *     call dbt_put_internal() for update...
 */
static int
la_get_current (OR_BUF * buf, SM_CLASS * sm_class,
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
      vars = (int *) malloc (DB_SIZEOF (int) * sm_class->variable_count);
      if (vars == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
		  1, DB_SIZEOF (int) * sm_class->variable_count);
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}
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
	  return error;
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
  for (i = sm_class->fixed_count, j = 0; i < sm_class->att_count;
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
	  return error;
	}
    }

  if (vars != NULL)
    {
      free_and_init (vars);
    }

  return error;
}

/*
 * la_disk_to_obj() - same function with tf_disk_to_obj, but always use
 *                      the current representation.
 *   return: NO_ERROR or error code
 *
 * Note:
 *     Analyze the record description, get the value for each attribute,
 *     call dbt_put_internal() for update...
 */
static int
la_disk_to_obj (MOBJ classobj, RECDES * record, DB_OTMPL * def,
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

      error = la_get_current (buf, sm_class, bound_bit_flag, def, key);
    }
  else
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			   ER_GENERIC_ERROR, 0);
      error = ER_GENERIC_ERROR;
    }

  return error;
}

/*
 * la_get_log_data() - get the data area of log record
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
 *   match_rcvindex
 *
 * Note: get the data area, and rcvindex, length of data for the
 *              given log record
 */
static int
la_get_log_data (struct log_rec *lrec,
		 LOG_LSA * lsa, LOG_PAGE * pgptr,
		 unsigned int match_rcvindex, unsigned int *rcvindex,
		 void **logs, char **rec_type, char **data, int *d_length)
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

  bool is_overflow = false;
  bool is_diff = false;

  pg = pgptr;

  offset = DB_SIZEOF (struct log_rec) + lsa->offset;
  pageid = lsa->pageid;

  LA_LOG_READ_ALIGN (error, offset, pageid, pg, release_yn);
  if (error != NO_ERROR)
    {
      return error;
    }

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

      length = DB_SIZEOF (struct log_undoredo);
      LA_LOG_READ_ADVANCE_WHEN_DOESNT_FIT (error, length, offset, pageid,
					   pg, release_yn);

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

	  LA_LOG_READ_ADD_ALIGN (error, DB_SIZEOF (*undoredo), offset,
				 pageid, pg, release_yn);

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

		  /* get undo data for XOR process */
		  la_log_copy_fromlog (NULL, undo_data, undo_length,
				       pageid, offset, pg);

		  if (is_undo_zip && undo_length > 0)
		    {
		      if (!log_unzip (la_Info.undo_unzip_ptr,
				      undo_length, undo_data))
			{
			  if (release_yn == 1)
			    {
			      la_release_page_buffer (pg->hdr.logical_pageid);
			    }
			  if (undo_data)
			    {
			      free_and_init (undo_data);
			    }
			  return ER_IO_LZO_DECOMPRESS_FAIL;
			}
		    }

		  LA_LOG_READ_ADD_ALIGN (error, undo_length, temp_offset,
					 temp_pageid, temp_pg, release_yn);
		  pg = temp_pg;
		  pageid = temp_pageid;
		  offset = temp_offset;
		}
	      else
		{
		  LA_LOG_READ_ADD_ALIGN (error, GET_ZIP_LEN (undo_length),
					 offset, pageid, pg, release_yn);
		}
	    }
	}
      break;

    case LOG_UNDO_DATA:
      length = DB_SIZEOF (struct log_undo);
      LA_LOG_READ_ADVANCE_WHEN_DOESNT_FIT (error, length, offset,
					   pageid, pg, release_yn);
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
	  LA_LOG_READ_ADD_ALIGN (error, DB_SIZEOF (*undo), offset, pageid,
				 pg, release_yn);
	}
      break;

    case LOG_REDO_DATA:
      length = DB_SIZEOF (struct log_redo);
      LA_LOG_READ_ADVANCE_WHEN_DOESNT_FIT (error, length, offset,
					   pageid, pg, release_yn);
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
	  LA_LOG_READ_ADD_ALIGN (error, DB_SIZEOF (*redo), offset, pageid,
				 pg, release_yn);
	}
      break;

    default:
      if (logs)
	{
	  *logs = NULL;
	}
      if (release_yn == 1)
	{
	  la_release_page_buffer (pg->hdr.logical_pageid);
	}

      return error;
    }

  if (error != NO_ERROR)
    {
      return error;
    }

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
	      la_release_page_buffer (pg->hdr.logical_pageid);
	    }
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_OUT_OF_VIRTUAL_MEMORY, 1, length);
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}
    }

  if (is_zip)
    {
      /* Get Zip Data */
      la_log_copy_fromlog (NULL, *data, nLength, pageid, offset, pg);
    }
  else
    {
      /* Get Redo Data */
      la_log_copy_fromlog (rec_type ? *rec_type : NULL, *data, length,
			   pageid, offset, pg);
    }

  if (is_zip && nLength != 0)
    {
      if (!log_unzip (la_Info.redo_unzip_ptr, nLength, *data))
	{
	  if (release_yn == 1)
	    {
	      la_release_page_buffer (pg->hdr.logical_pageid);
	    }
	  if (undo_data)
	    {
	      free_and_init (undo_data);
	    }
	  return ER_IO_LZO_DECOMPRESS_FAIL;
	}
    }

  if (is_zip)
    {
      if (is_diff)
	{
	  if (is_undo_zip)
	    {
	      undo_length = (la_Info.undo_unzip_ptr)->data_length;
	      redo_length = (la_Info.redo_unzip_ptr)->data_length;
	      (void) log_diff (undo_length,
			       (la_Info.undo_unzip_ptr)->log_data,
			       redo_length,
			       (la_Info.redo_unzip_ptr)->log_data);
	    }
	  else
	    {
	      redo_length = (la_Info.redo_unzip_ptr)->data_length;
	      (void) log_diff (undo_length, undo_data,
			       redo_length,
			       (la_Info.redo_unzip_ptr)->log_data);
	    }
	}
      else
	{
	  redo_length = (la_Info.redo_unzip_ptr)->data_length;
	}

      if (rec_type)
	{
	  rec_len = DB_SIZEOF (INT16);
	  length = redo_length - rec_len;
	}
      else
	{
	  length = redo_length;
	}

      if (is_overflow)
	{
	  free_and_init (*data);
	  if ((*data = malloc (length)) == NULL)
	    {
	      *d_length = 0;
	      if (release_yn == 1)
		{
		  la_release_page_buffer (pg->hdr.logical_pageid);
		}
	      if (undo_data)
		{
		  free_and_init (undo_data);
		}
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_OUT_OF_VIRTUAL_MEMORY, 1, length);
	      return ER_OUT_OF_VIRTUAL_MEMORY;
	    }
	}

      if (rec_type)
	{
	  memcpy (*rec_type, (la_Info.redo_unzip_ptr)->log_data, rec_len);
	  memcpy (*data,
		  (la_Info.redo_unzip_ptr)->log_data + rec_len, length);
	}
      else
	{
	  memcpy (*data, (la_Info.redo_unzip_ptr)->log_data, redo_length);
	}
    }

  *d_length = length;

  if (release_yn == 1)
    {
      la_release_page_buffer (pg->hdr.logical_pageid);
    }

  if (undo_data)
    {
      free_and_init (undo_data);
    }

  return error;
}

/*
 * la_get_overflow_update_recdes() - prepare the overflow page update
 *   return: NO_ERROR or error code
 *
 */
static int
la_get_overflow_recdes (struct log_rec *log_record, void *logs,
			char **area, int *length, unsigned int rcvindex)
{
  LOG_LSA current_lsa;
  LOG_PAGE *current_log_page;
  struct log_rec *current_log_record;
  LA_OVF_PAGE_LIST *ovf_list_head = NULL;
  LA_OVF_PAGE_LIST *ovf_list_tail = NULL;
  LA_OVF_PAGE_LIST *ovf_list_data = NULL;
  struct log_redo *redo_log;
  VPID *temp_vpid;
  VPID prev_vpid;
  bool first = true;
  bool error_status = true;
  bool is_end_of_record = false;
  int copyed_len;
  int area_len;
  int area_offset;
  int error;

  LSA_COPY (&current_lsa, &log_record->prev_tranlsa);
  prev_vpid.pageid = ((struct log_undoredo *) logs)->data.pageid;
  prev_vpid.volid = ((struct log_undoredo *) logs)->data.volid;

  while (!is_end_of_record && !LSA_ISNULL (&current_lsa))
    {
      current_log_page = la_get_page (current_lsa.pageid);
      current_log_record =
	(struct log_rec *) ((char *) current_log_page->area +
			    current_lsa.offset);

      if (current_log_record->trid != log_record->trid)
	{
	  la_release_page_buffer (current_lsa.pageid);
	  break;
	}

      /* process only LOG_REDO_DATA */
      if (current_log_record->type == LOG_REDO_DATA)
	{
	  ovf_list_data =
	    (LA_OVF_PAGE_LIST *) malloc (DB_SIZEOF (LA_OVF_PAGE_LIST));
	  if (ovf_list_data == NULL)
	    {
	      /* malloc failed */
	      la_release_page_buffer (current_lsa.pageid);
	      return ER_OUT_OF_VIRTUAL_MEMORY;
	    }
	  memset (ovf_list_data, 0, DB_SIZEOF (LA_OVF_PAGE_LIST));
	  error =
	    la_get_log_data (current_log_record, &current_lsa,
			     current_log_page, rcvindex, NULL,
			     (void **) &redo_log, NULL,
			     &ovf_list_data->data, &ovf_list_data->length);

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
		  free_and_init (ovf_list_data);
		}
	    }
	  else
	    {
	      free_and_init (ovf_list_data);
	    }
	}
      la_release_page_buffer (current_lsa.pageid);
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
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  /* make record description */
  copyed_len = 0;
  while (ovf_list_head)
    {
      ovf_list_data = ovf_list_head;
      ovf_list_head = ovf_list_head->next;

      if (first)
	{
	  area_offset = offsetof (LA_OVF_FIRST_PART, data);
	  first = false;
	}
      else
	{
	  area_offset = offsetof (LA_OVF_REST_PARTS, data);
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

/*
 * la_get_next_update_log() - get the right update log
 *   return: NO_ERROR or error code
 *   prev_lrec(in):  prev log record
 *   pgptr(in):  the start log page pointer
 *   logs(out) : the specialized log info
 *   rec_type(out) : the type of RECDES
 *   data(out) : the log data
 *   d_length(out): the length of data
 *
 * Note:
 *      When the applier meets the REC_ASSIGN_ADDRESS or REC_RELOCATION
 *      record, it should fetch the real UPDATE log record to be processed.
 */
static int
la_get_next_update_log (struct log_rec *prev_lrec,
			LOG_PAGE * pgptr, void **logs,
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
  int nLength = 0;
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

  bool bIsDiff = false;

  pg = pgptr;
  LSA_COPY (&lsa, &prev_lrec->forw_lsa);
  prev_log = *(struct log_undoredo **) logs;

  log_undo_data = la_Info.undo_unzip_ptr;
  log_unzip_data = la_Info.redo_unzip_ptr;

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

	      offset = DB_SIZEOF (struct log_rec) + lsa.offset;
	      pageid = lsa.pageid;
	      LA_LOG_READ_ALIGN (error, offset, pageid, pg, release_yn);
	      length = DB_SIZEOF (struct log_undoredo);
	      LA_LOG_READ_ADVANCE_WHEN_DOESNT_FIT (error, length, offset,
						   pageid, pg, release_yn);
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
		      LA_LOG_READ_ADD_ALIGN (error, DB_SIZEOF (*undoredo),
					     offset, pageid, pg, release_yn);
		      if (bIsDiff)
			{
			  if (ZIP_CHECK (undo_length))
			    {
			      is_undo_zip = true;
			      undo_length = GET_ZIP_LEN (undo_length);
			    }

			  undo_data = (char *) malloc (undo_length);

			  la_log_copy_fromlog (NULL, undo_data,
					       undo_length, pageid,
					       offset, pg);

			  if (is_undo_zip)
			    {
			      if (!log_unzip
				  (log_undo_data, undo_length, undo_data))
				{
				  if (release_yn == 1)
				    {
				      la_release_page_buffer (pg->hdr.
							      logical_pageid);
				    }
				  if (undo_data)
				    {
				      free_and_init (undo_data);
				    }
				  return ER_IO_LZO_DECOMPRESS_FAIL;
				}
			    }
			  LA_LOG_READ_ADD_ALIGN (error, undo_length, offset,
						 pageid, pg, release_yn);
			}
		      else
			{
			  LA_LOG_READ_ADD_ALIGN (error, GET_ZIP_LEN
						 (undo_length),
						 offset, pageid, pg,
						 release_yn);
			}

		      if (ZIP_CHECK (temp_length))
			{
			  is_zip = true;
			  nLength = GET_ZIP_LEN (temp_length);
			  la_log_copy_fromlog (NULL, *data, nLength,
					       pageid, offset, pg);
			}
		      else
			{
			  la_log_copy_fromlog (*rec_type, *data,
					       length, pageid, offset, pg);
			  is_zip = false;
			}

		      if (is_zip && nLength != 0)
			{
			  if (!log_unzip (log_unzip_data, nLength, *data))
			    {
			      if (release_yn == 1)
				{
				  la_release_page_buffer (pg->hdr.
							  logical_pageid);
				}
			      if (undo_data)
				{
				  free_and_init (undo_data);
				}
			      return ER_IO_LZO_DECOMPRESS_FAIL;
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
			      rec_len = DB_SIZEOF (INT16);
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
		      if (release_yn == 1)
			{
			  la_release_page_buffer (pg->hdr.logical_pageid);
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
	      if (release_yn == 1)
		{
		  la_release_page_buffer (pg->hdr.logical_pageid);
		}
	      return ER_GENERIC_ERROR;
	    }
	  LSA_COPY (&lsa, &lrec->forw_lsa);
	}

      if (release_yn == 1)
	{
	  la_release_page_buffer (pg->hdr.logical_pageid);
	}
      pg = la_get_page (lsa.pageid);
      release_yn = 1;
    }

  if (release_yn == 1)
    {
      la_release_page_buffer (pg->hdr.logical_pageid);
    }

  return error;
}

static int
la_get_relocation_recdes (struct log_rec *lrec, LOG_PAGE * pgptr,
			  unsigned int match_rcvindex, void **logs,
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
      pg = la_get_page (lsa.pageid);
      if (pg != pgptr)
	{
	  release_yn = 1;
	}
      tmp_lrec = (struct log_rec *) ((char *) pg->area + lsa.offset);
      if (tmp_lrec->trid != lrec->trid)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_PAGE_CORRUPTED,
		  1, lsa.pageid);
	  error = ER_LOG_PAGE_CORRUPTED;
	}
      else
	{
	  error =
	    la_get_log_data (tmp_lrec, &lsa, pg,
			     RVHF_INSERT, &rcvindex, logs,
			     rec_type, data, d_length);
	}
    }
  else
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_PAGE_CORRUPTED,
	      1, lsa.pageid);
      error = ER_LOG_PAGE_CORRUPTED;
    }

  if (release_yn == 1)
    {
      la_release_page_buffer (lsa.pageid);
    }

  return error;
}

/*
 * la_get_recdes() - get the record description from the log file
 *   return: NO_ERROR or error code
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
 */
static int
la_get_recdes (LOG_LSA * lsa, LOG_PAGE * pgptr,
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

  error = la_get_log_data (lrec, lsa, pg, 0, rcvindex,
			   &logs, &rec_type, &log_data, &length);

  if (error == NO_ERROR)
    {
      recdes->type = *(INT16 *) (rec_type);
      recdes->data = log_data;
      recdes->area_size = recdes->length = length;
    }
  else
    {
      er_log_debug (ARG_FILE_LINE,
		    "cannot get log record from LSA(%d|%d)",
		    lsa->pageid, lsa->offset);
    }

  /* Now.. we have to process overflow pages */
  length = 0;
  if (*rcvindex == RVOVF_CHANGE_LINK)
    {
      /* if overflow page update */
      error = la_get_overflow_recdes (lrec, logs, &area, &length,
				      RVOVF_PAGE_UPDATE);
      recdes->type = REC_BIGONE;
    }
  else if (recdes->type == REC_BIGONE)
    {
      /* if overflow page insert */
      error = la_get_overflow_recdes (lrec, logs, &area, &length,
				      RVOVF_NEWPAGE_INSERT);
    }
  else if (*rcvindex == RVHF_INSERT && recdes->type == REC_ASSIGN_ADDRESS)
    {
      error = la_get_next_update_log (lrec,
				      pg, &logs, &rec_type,
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
      error = la_get_relocation_recdes (lrec, pg, 0,
					&logs, &rec_type, &log_data, &length);
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

  if (error != NO_ERROR)
    {
      return ER_GENERIC_ERROR;
    }

  recdes->data = (char *) (area);
  recdes->area_size = recdes->length = length;
  *ovfyn = true;

  return error;
}

/*
 * la_apply_update_log() - apply the insert/update log to the target slave
 *   return: NO_ERROR or error code
 *   item : replication item
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
 */
static int
la_apply_update_log (LA_ITEM * item)
{
  int error = NO_ERROR;
  DB_OBJECT *class_obj;
  DB_OBJECT *object;
  MOBJ mclass;
  LOG_PAGE *pgptr;
  unsigned int rcvindex;
  RECDES recdes;
  DB_OTMPL *inst_tp = NULL;
  bool ovfyn = false;
  PAGEID old_pageid = -1;

  /* get the target log page */
  pgptr = la_get_page (item->lsa.pageid);
  old_pageid = item->lsa.pageid;
  if (pgptr == NULL)
    {
      return er_errid ();
    }

  /* retrieve the target record description */
  error =
    la_get_recdes (&item->lsa, pgptr, &recdes, &rcvindex,
		   la_Info.log_data, la_Info.rec_type, &ovfyn);
  if (error == ER_NET_CANT_CONNECT_SERVER || error == ER_OBJ_NO_CONNECT)
    {
      return ER_NET_CANT_CONNECT_SERVER;
    }
  if (error != NO_ERROR)
    {
      return ER_GENERIC_ERROR;
    }

  if (recdes.type == REC_ASSIGN_ADDRESS || recdes.type == REC_RELOCATION)
    {
      la_release_page_buffer (old_pageid);
      return error;
    }

  /* Now, make the MOBJ from the record description */
  class_obj = db_find_class (item->class_name);
  if (class_obj == NULL)
    {
      goto error_rtn;
    }

  /* get class info */
  if ((mclass =
       locator_fetch_class (class_obj, DB_FETCH_CLREAD_INSTREAD)) == NULL)
    {
      goto error_rtn;
    }

  if (rcvindex != RVHF_INSERT)
    {
      if ((object = obj_find_object_by_pkey (class_obj, &item->key,
					     AU_FETCH_UPDATE)) == NULL)
	{
	  er_clear ();
	  rcvindex = RVHF_INSERT;
	}
    }

  /* start replication */
  /* NOTE: if the master insert a row, and update it within a transaction,
   *       the replication record points the RVHF_UPDATE record
   */
  if (rcvindex == RVHF_INSERT)
    {
      inst_tp = dbt_create_object_internal (class_obj);
    }
  else if (rcvindex == RVHF_UPDATE || rcvindex == RVOVF_CHANGE_LINK)
    {
      inst_tp = dbt_edit_object (object);
    }

  if (inst_tp == NULL)
    {
      error = er_errid ();
      goto error_rtn;
    }

  /* make object using the record rescription */
  if ((error =
       la_disk_to_obj (mclass, &recdes, inst_tp, &item->key)) != NO_ERROR)
    {
      goto error_rtn;
    }

  /* update object */
  if (dbt_finish_object (inst_tp) == NULL)
    {
      goto error_rtn;
    }

  if (ovfyn)
    {
      free_and_init (recdes.data);
    }

  la_release_page_buffer (old_pageid);
  return error;

  /* error */
error_rtn:
  if (ovfyn)
    {
      free_and_init (recdes.data);
    }
  if (inst_tp)
    {
      dbt_abort_object (inst_tp);
    }

  if (er_errid () == ER_NET_CANT_CONNECT_SERVER
      || er_errid () == ER_OBJ_NO_CONNECT)
    {
      return ER_NET_CANT_CONNECT_SERVER;
    }

  er_log_debug (ARG_FILE_LINE, "Error msg : %s", er_msg ());

  la_release_page_buffer (old_pageid);
  /* TODO: need more specific error */
  return ER_FAILED;
}

/*
 * la_update_query_execute()
 *   return: NO_ERROR or error code
 *   sql(in)
 */
static int
la_update_query_execute (const char *sql)
{
  int error = NO_ERROR;
  DB_QUERY_RESULT *result;
  DB_QUERY_ERROR query_error;

  if (db_execute (sql, &result, &query_error) < 0)
    {
      return er_errid ();
    }

  error = db_query_end (result);

  return error;
}

/*
 * la_apply_schema_log() - apply the schema log to the target slave
 *   return: NO_ERROR or error code
 *   item(in): replication item
 *
 * Note:
 */
static int
la_apply_schema_log (LA_ITEM * item)
{
  char *ddl;
  int error = NO_ERROR;
  DB_OBJECT *user = NULL, *save_user = NULL;

  switch (item->type)
    {
    case CUBRID_STMT_CREATE_CLASS:
      if (item->db_user != NULL && strlen (item->db_user) > 1)
	{
	  user = au_find_user (item->db_user);
	  if (user == NULL)
	    {
	      if (er_errid () == ER_NET_CANT_CONNECT_SERVER
		  || er_errid () == ER_OBJ_NO_CONNECT)
		{
		  error = ER_NET_CANT_CONNECT_SERVER;
		}
	      else
		{
		  error = er_errid ();
		}
	      break;
	    }

	  /* change owner */
	  save_user = Au_user;
	  error = AU_SET_USER (user);
	  if (error != NO_ERROR)
	    {
	      save_user = NULL;
	      /* go on with original user */
	    }
	}
    case CUBRID_STMT_ALTER_CLASS:
    case CUBRID_STMT_RENAME_CLASS:
    case CUBRID_STMT_DROP_CLASS:

    case CUBRID_STMT_CREATE_INDEX:
    case CUBRID_STMT_ALTER_INDEX:
    case CUBRID_STMT_DROP_INDEX:

      /* serial replication is not schema replication but data replication
       *
       * case CUBRID_STMT_CREATE_SERIAL:
       * case CUBRID_STMT_ALTER_SERIAL:
       * case CUBRID_STMT_DROP_SERIAL:
       */

    case CUBRID_STMT_DROP_DATABASE:
    case CUBRID_STMT_DROP_LABEL:

    case CUBRID_STMT_CREATE_STORED_PROCEDURE:
    case CUBRID_STMT_DROP_STORED_PROCEDURE:


      /* TODO: check it */
    case CUBRID_STMT_CREATE_USER:
    case CUBRID_STMT_ALTER_USER:
    case CUBRID_STMT_DROP_USER:
    case CUBRID_STMT_GRANT:
    case CUBRID_STMT_REVOKE:

      /* TODO: check it */
    case CUBRID_STMT_CREATE_TRIGGER:
    case CUBRID_STMT_RENAME_TRIGGER:
    case CUBRID_STMT_DROP_TRIGGER:
    case CUBRID_STMT_REMOVE_TRIGGER:
    case CUBRID_STMT_SET_TRIGGER:

      ddl = db_get_string (&item->key);
      if (la_update_query_execute (ddl) != NO_ERROR)
	{
	  if (er_errid () == ER_NET_CANT_CONNECT_SERVER
	      || er_errid () == ER_OBJ_NO_CONNECT)
	    {
	      error = ER_NET_CANT_CONNECT_SERVER;
	    }
	  else
	    {
	      error = er_errid ();
	    }
	}

      if (save_user != NULL)
	{
	  if (AU_SET_USER (save_user))
	    {
	      /* it can be happened */
	      abort ();
	    }
	}
      break;

    default:
      return NO_ERROR;
    }

  return error;
}

/*
 * la_clear_repl_item() - clear replication item
 *   return: none
 *   repl_list : the target list already applied.
 *
 * Note:
 *       clear the applied list area after processing ..
 */
static void
la_clear_repl_item (LA_APPLY * repl_list)
{
  LA_ITEM *repl_item;

  repl_item = repl_list->head;

  while (repl_item != NULL)
    {
      repl_list->head = repl_item->next;
      if (repl_item->class_name != NULL)
	{
	  db_private_free_and_init (NULL, repl_item->class_name);
	  pr_clear_value (&repl_item->key);
	}
      free_and_init (repl_item);
      repl_item = repl_list->head;
    }
  repl_list->tail = NULL;
  LSA_SET_NULL (&repl_list->start_lsa);

  repl_list->tranid = 0;	/* set "free" for the reuse */
}

/*
 * la_apply_repl_log() - apply the log to the target slave
 *   return: NO_ERROR or error code
 *   tranid: the target transaction id
 *
 * Note:
 *    This function is called when the APPLY thread meets the LOG_COMMIT
 *    record.
 */
static int
la_apply_repl_log (int tranid, int *total_rows)
{
  LA_ITEM *item;
  int error = NO_ERROR;
  LA_APPLY *apply;
  int update_cnt = 0;
  char error_string[1024];
  PARSER_VARCHAR *buf;
  PARSER_CONTEXT *parser;

  apply = la_find_apply_list (tranid);
  if (apply == NULL)
    {
      return er_errid ();
    }

  if (apply->head == NULL)
    {
      return NO_ERROR;
    }

  item = apply->head;
  while (item && error == NO_ERROR)
    {
      switch (item->type)
	{
	case PT_INSERT:
	case PT_UPDATE:
	  {
	    if (LSA_ISNULL (&item->lsa))
	      {
		error = la_apply_delete_log (item);
	      }
	    else
	      {
		error = la_apply_update_log (item);
	      }
	    break;
	  }
	default:
	  {
	    error = la_apply_schema_log (item);
	    break;
	  }
	}

      if (error == NO_ERROR)
	{
	  update_cnt++;
	}
      else
	{
	  parser = parser_create_parser ();
	  buf = describe_value (parser, NULL, &item->key);
	  sprintf (error_string, "[%s,%s] %s",
		   item->class_name, pt_get_varchar_bytes (buf),
		   db_error_string (1));
	  parser_free_parser (parser);
	  er_log_debug (ARG_FILE_LINE,
			"Internal system failure: %s", error_string);
	  if (error != ER_NET_CANT_CONNECT_SERVER)
	    {
	      /* force commit for duplication error (index) */
	      if (db_commit_transaction () != NO_ERROR)
		{
		  if (er_errid () == ER_NET_CANT_CONNECT_SERVER
		      || er_errid () == ER_OBJ_NO_CONNECT)
		    {
		      error = ER_NET_CANT_CONNECT_SERVER;
		    }
		}
	      error = ER_GENERIC_ERROR;
	    }
	}
      item = item->next;
    }

  if (error == NO_ERROR)
    {
      *total_rows += update_cnt;
    }

  la_clear_repl_item (apply);

  return error;
}

/*
 * la_apply_commit_list() - apply the log to the target slave
 *   return: NO_ERROR or error code
 *   lsa  : the target LSA of the log
 *
 * Note:
 *    This function is called when the APPLY thread meets the LOG_COMMIT
 *    record.
 */
static int
la_apply_commit_list (LOG_LSA * lsa)
{
  LA_COMMIT *commit;
  int error = NO_ERROR;

  LSA_SET_NULL (lsa);

  commit = la_Info.commit_head;
  if (commit && commit->type == LOG_COMMIT)
    {
      error = la_apply_repl_log (commit->tranid, &la_Info.total_rows);

      LSA_COPY (lsa, &commit->log_lsa);

      la_Info.commit_head = commit->next;
      if (la_Info.commit_head == NULL)
	la_Info.commit_tail = NULL;

      free_and_init (commit);
    }

  return error;
}


/*
 * la_clear_repl_item_by_tranid() - clear replication item using tranid
 *   return: none
 *   tranid: transaction id
 *
 * Note:
 *       clear the applied list area after processing ..
 *       When we meet the LOG_ABORT_TOPOPE or LOG_ABORT record,
 *       we have to clear the replication items of the target transaction.
 *       In case of LOG_ABORT_TOPOPE, the apply list should be preserved
 *       for the later use (so call la_clear_repl_item() using
 *       false as the second argument).
 */
static void
la_clear_repl_item_by_tranid (int tranid)
{
  LA_APPLY *repl_list;
  LA_COMMIT *commit;
  LA_COMMIT *tmp;
  LA_COMMIT dumy;

  repl_list = la_find_apply_list (tranid);
  la_clear_repl_item (repl_list);

  dumy.next = la_Info.commit_head;
  commit = &dumy;
  while (commit->next)
    {
      if (commit->next->tranid == tranid)
	{
	  tmp = commit->next;
	  if (tmp->next == NULL)
	    {
	      la_Info.commit_tail = commit;
	    }
	  commit->next = tmp->next;
	  free_and_init (tmp);
	}
      else
	{
	  commit = commit->next;
	}
    }
  la_Info.commit_head = dumy.next;
  if (la_Info.commit_head == NULL)
    la_Info.commit_tail = NULL;
}

static int
la_log_record_process (struct log_rec *lrec,
		       LOG_LSA * final, LOG_PAGE * pg_ptr)
{
  LA_APPLY *apply = NULL;
  int error = NO_ERROR;
  LOG_LSA lsa_apply;

  if (lrec->trid == NULL_TRANID ||
      LSA_GT (&lrec->prev_tranlsa, final) || LSA_GT (&lrec->back_lsa, final))
    {
      if (lrec->type != LOG_END_OF_LOG &&
	  lrec->type != LOG_DUMMY_FILLPAGE_FORARCHIVE)
	{
	  er_log_debug (ARG_FILE_LINE,
			"log record error : invalid record\n"
			" LSA(%4d|%4d) Forw LSA(%4d|%4d) Backw LSA(%4d|%4d)\n"
			"  Trid = %d, Prev tran logrec LSA(%d|%d) Type = %d\n",
			final->pageid, final->offset,
			lrec->forw_lsa.pageid, lrec->forw_lsa.offset,
			lrec->back_lsa.pageid, lrec->back_lsa.offset,
			lrec->trid,
			lrec->prev_tranlsa.pageid, lrec->prev_tranlsa.offset,
			lrec->type);
	  return ER_LOG_PAGE_CORRUPTED;
	}
    }

  la_Info.is_end_of_record = false;
  switch (lrec->type)
    {
    case LOG_DUMMY_FILLPAGE_FORARCHIVE:
      final->pageid++;
      final->offset = 0;
      return ER_INTERRUPT;

    case LOG_END_OF_LOG:
      if (la_does_page_exist (final->pageid + 1)
	  && la_does_page_exist (final->pageid) ==
	  LA_PAGE_EXST_IN_ARCHIVE_LOG)
	{
	  /* when we meet the END_OF_LOG of archive file, skip log page */
	  er_log_debug (ARG_FILE_LINE,
			"reached END_OF_LOG in archive log. LSA(%d|%d)",
			final->pageid, final->offset);
	  final->pageid++;
	  final->offset = 0;
	}
      else
	{
	  /* we meet the END_OF_LOG */
#if defined (LA_VERBOSE_DEBUG)
	  er_log_debug (ARG_FILE_LINE,
			"reached END_OF_LOG in active log. LSA(%d|%d)",
			final->pageid, final->offset);
#endif
	  la_Info.is_end_of_record = true;
	}
      return ER_INTERRUPT;

    case LOG_UNDO_DATA:
    case LOG_REDO_DATA:
    case LOG_DIFF_UNDOREDO_DATA:
    case LOG_UNDOREDO_DATA:
      /*
       * To gurantee the stability of copy log archiving,
       * we manages the start point of all transactions.
       */
      apply = la_find_apply_list (lrec->trid);
      if (apply == NULL)
	{
	  la_applier_need_shutdown = true;
	  error = er_errid ();
	}
      if (error != NO_ERROR)
	{
	  error = er_errid ();
	  return error;
	}

      if (LSA_ISNULL (&apply->start_lsa))
	{
	  LSA_COPY (&apply->start_lsa, final);
	}
      break;

    case LOG_REPLICATION_DATA:
    case LOG_REPLICATION_SCHEMA:
      /* add the replication log to the target transaction */
      error = la_set_repl_log (pg_ptr, lrec->type, lrec->trid, final);
      if (error != NO_ERROR)
	{
	  la_applier_need_shutdown = true;
	  return error;
	}
      break;

    case LOG_UNLOCK_COMMIT:
    case LOG_COMMIT_TOPOPE:
      /* add the repl_list to the commit_list  */
      error = la_add_unlock_commit_log (lrec->trid, final);
      if (error != NO_ERROR)
	{
	  la_applier_need_shutdown = true;
	  return error;
	}

      if (lrec->type != LOG_COMMIT_TOPOPE)
	{
	  break;
	}

    case LOG_COMMIT:
      /* apply the replication log to the slave */
      if (LSA_GT (final, &la_Info.final_lsa))
	{
	  error = la_set_commit_log (lrec->trid, final,
				     lrec->type ==
				     LOG_COMMIT_TOPOPE ? -1 :
				     la_retrieve_eot_time (pg_ptr, final));
	  if (error != NO_ERROR)
	    {
	      la_applier_need_shutdown = true;
	      return error;
	    }

	  do
	    {
	      error = la_apply_commit_list (&lsa_apply);
	      if (error == ER_NET_CANT_CONNECT_SERVER)
		{
		  switch (er_errid ())
		    {
		    case ER_TM_SERVER_DOWN_UNILATERALLY_ABORTED:
		      break;
		    case ER_LK_UNILATERALLY_ABORTED:
		      break;
		    default:
		      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			      ER_NET_SERVER_COMM_ERROR, 1,
			      "cannot connect with server");
		      la_applier_need_shutdown = true;
		      return error;
		    }
		}
	      if (!LSA_ISNULL (&lsa_apply))
		{
		  LSA_COPY (&(la_Info.final_lsa), &lsa_apply);
		}
	    }
	  while (!LSA_ISNULL (&lsa_apply));	/* if lsa_apply is not null then
						 * there is the replication log
						 * applying to the slave
						 */
	}
      else
	{
	  la_clear_repl_item_by_tranid (lrec->trid);
	}
      break;

      /* you have to check the ABORT LOG to avoid the memory leak */
    case LOG_UNLOCK_ABORT:
      break;

    case LOG_ABORT:
      la_clear_repl_item_by_tranid (lrec->trid);
      break;

    case LOG_DUMMY_CRASH_RECOVERY:
      er_log_debug (ARG_FILE_LINE,
		    "we met LOG_DUMMY_CRASH_RECOVERY. LSA(%d|%d)",
		    final->pageid, final->offset);
      LSA_COPY (final, &lrec->forw_lsa);
      return ER_INTERRUPT;

    default:
      break;
    }				/* switch(lrec->type) */

  /*
   * if this is the final record of the archive log..
   * we have to fetch the next page. So, increase the pageid,
   * but we don't know the exact offset of the next record.
   * the offset would be adjusted after getting the next log page
   */
  if (lrec->forw_lsa.pageid == -1 ||
      lrec->type <= LOG_SMALLER_LOGREC_TYPE ||
      lrec->type >= LOG_LARGER_LOGREC_TYPE)
    {
      if (la_does_page_exist (final->pageid) == LA_PAGE_EXST_IN_ARCHIVE_LOG)
	{
	  er_log_debug (ARG_FILE_LINE,
			"reached end of archive log. LSA(%d|%d)",
			final->pageid, final->offset);
	  final->pageid++;
	  final->offset = 0;
	}

      er_log_debug (ARG_FILE_LINE,
		    "log record error :\n"
		    " LSA(%4d|%4d) Forw LSA(%4d|%4d) Backw LSA(%4d|%4d)\n"
		    "  Trid = %d, Prev tran logrec LSA(%d|%d) Type = %d\n",
		    final->pageid, final->offset,
		    lrec->forw_lsa.pageid, lrec->forw_lsa.offset,
		    lrec->back_lsa.pageid, lrec->back_lsa.offset,
		    lrec->trid,
		    lrec->prev_tranlsa.pageid, lrec->prev_tranlsa.offset,
		    lrec->type);

      return ER_LOG_PAGE_CORRUPTED;
    }

  return NO_ERROR;
}

static int
la_change_state ()
{
  int error = NO_ERROR;
  int new_state = HA_LOG_APPLIER_STATE_NA;

  if (la_Info.last_server_state == la_Info.act_log.log_hdr->ha_server_status
      && la_Info.last_file_state == la_Info.act_log.log_hdr->ha_file_status
      && la_Info.last_is_end_of_record == la_Info.is_end_of_record)
    {
      /* there are no need to change */
      return NO_ERROR;
    }

  if (la_Info.last_server_state != la_Info.act_log.log_hdr->ha_server_status)
    {
      la_Info.last_server_state = la_Info.act_log.log_hdr->ha_server_status;
    }

  if (la_Info.last_file_state != la_Info.act_log.log_hdr->ha_file_status)
    {
      la_Info.last_file_state = la_Info.act_log.log_hdr->ha_file_status;
    }

  if (la_Info.last_is_end_of_record != la_Info.is_end_of_record)
    {
      la_Info.last_is_end_of_record = la_Info.is_end_of_record;
    }

  /* check log file status */
  if (la_Info.is_end_of_record == true &&
      (la_Info.act_log.log_hdr->ha_file_status ==
       LOG_HA_FILESTAT_SYNCHRONIZED))
    {
      /* check server's state with log header */
      switch (la_Info.act_log.log_hdr->ha_server_status)
	{
	case LOG_HA_SRVSTAT_ACTIVE:
	case LOG_HA_SRVSTAT_TO_BE_STANDBY:
	case LOG_HA_SRVSTAT_TO_BE_ACTIVE:
	  if (la_Info.apply_state != HA_LOG_APPLIER_STATE_WORKING)
	    {
	      /* notify to slave db */
	      new_state = HA_LOG_APPLIER_STATE_WORKING;
	    }
	  break;

	case LOG_HA_SRVSTAT_DEAD:
	case LOG_HA_SRVSTAT_STANDBY:
	  if (la_Info.apply_state != HA_LOG_APPLIER_STATE_DONE)
	    {
	      /* notify to slave db */
	      new_state = HA_LOG_APPLIER_STATE_DONE;
	    }
	  break;
	default:
	  er_log_debug (ARG_FILE_LINE, "BUG. Unknown LOG_HA_SRVSTAT (%x)",
			la_Info.act_log.log_hdr->ha_server_status);
	  abort ();
	  break;
	}

    }
  else
    {
      switch (la_Info.act_log.log_hdr->ha_server_status)
	{
	case LOG_HA_SRVSTAT_ACTIVE:
	case LOG_HA_SRVSTAT_TO_BE_STANDBY:
	  if (la_Info.apply_state != HA_LOG_APPLIER_STATE_WORKING)
	    {
	      if (la_Info.apply_state != HA_LOG_APPLIER_STATE_RECOVERING)
		{
		  new_state = HA_LOG_APPLIER_STATE_RECOVERING;
		}
	    }
	  break;
	case LOG_HA_SRVSTAT_TO_BE_ACTIVE:
	case LOG_HA_SRVSTAT_STANDBY:
	case LOG_HA_SRVSTAT_DEAD:
	  if (la_Info.apply_state != HA_LOG_APPLIER_STATE_DONE)
	    {
	      if (la_Info.apply_state != HA_LOG_APPLIER_STATE_RECOVERING)
		{
		  new_state = HA_LOG_APPLIER_STATE_RECOVERING;
		}
	    }
	  break;
	default:
	  er_log_debug (ARG_FILE_LINE, "BUG. Unknown LOG_HA_SRVSTAT (%x)",
			la_Info.act_log.log_hdr->ha_server_status);
	  abort ();
	  break;
	}
    }

  if (new_state != HA_LOG_APPLIER_STATE_NA)
    {
      if (la_Info.apply_state == new_state)
	{
	  return NO_ERROR;
	}

      /* force commit when state is changing */
      error = la_log_commit ();
      if (error != NO_ERROR)
	{
	  return error;
	}

      switch (new_state)
	{
	case HA_LOG_APPLIER_STATE_RECOVERING:
	  error =
	    db_set_system_parameters ("ha_log_applier_state="
				      HA_LOG_APPLIER_STATE_RECOVERING_STR);
	  break;
	case HA_LOG_APPLIER_STATE_WORKING:
	  error =
	    db_set_system_parameters ("ha_log_applier_state="
				      HA_LOG_APPLIER_STATE_WORKING_STR);
	  break;
	case HA_LOG_APPLIER_STATE_DONE:
	  error =
	    db_set_system_parameters ("ha_log_applier_state="
				      HA_LOG_APPLIER_STATE_DONE_STR);
	  break;
	}

      if (error == NO_ERROR)
	{
	  la_Info.apply_state = new_state;
	  er_log_debug (ARG_FILE_LINE,
			"log_applier state is %s. LSA(%d|%d)\n",
			la_Info.apply_state ==
			HA_LOG_APPLIER_STATE_RECOVERING ?
			HA_LOG_APPLIER_STATE_RECOVERING_STR : la_Info.
			apply_state ==
			HA_LOG_APPLIER_STATE_WORKING ?
			HA_LOG_APPLIER_STATE_WORKING_STR : la_Info.
			apply_state ==
			HA_LOG_APPLIER_STATE_DONE ?
			HA_LOG_APPLIER_STATE_DONE_STR : "unknown",
			la_Info.last_committed_lsa.pageid,
			la_Info.last_committed_lsa.offset);
	}
      else
	{
	  er_log_debug (ARG_FILE_LINE, "db_set_system_parameters error. "
			"cannot change state to %d", new_state);
	}
    }

  return error;
}

/*
 * la_log_commit() -
 *   return: NO_ERROR or error code
 *
 * Note:
 *
 */
static int
la_log_commit ()
{
  int error = NO_ERROR;

  if (db_commit_is_needed () > 0)
    {
      error = db_commit_transaction ();
    }
  else
    {
      error = er_errid ();
    }

  if (error == NO_ERROR)
    {
      error = la_update_last_applied_lsa (&la_Info.final_lsa,
					  la_Info.log_path);
      if (error == NO_ERROR)
	{
	  LSA_COPY (&la_Info.last_committed_lsa, &la_Info.final_lsa);
	}
      else
	{
	  er_log_debug (ARG_FILE_LINE,
			"log applied but cannot update last commited LSA "
			"(%d|%d)",
			la_Info.final_lsa.pageid, la_Info.final_lsa.offset);
	  if (error == ER_NET_CANT_CONNECT_SERVER ||
	      error == ER_OBJ_NO_CONNECT)
	    {
	      error = ER_NET_CANT_CONNECT_SERVER;
	    }
	  else
	    {
	      er_log_debug (ARG_FILE_LINE,
			    "Cannot update commited LSA, but ignore it");
	      error = NO_ERROR;
	    }
	}
    }

  return error;
}


int
la_check_time_commit (struct timeval *time_commit, unsigned int threshold)
{
  struct timeval time_now;
  int error = NO_ERROR;

  assert (time_commit);

  /* check interval time for commit */
  gettimeofday (&time_now, NULL);

  if (((time_now.tv_sec - time_commit->tv_sec) * 1000 +
       (time_now.tv_usec / 1000 - time_commit->tv_usec / 1000))
      > threshold /* msec */ )
    {
      gettimeofday (time_commit, NULL);

      /* check server is connected now */
      error = db_ping_server (0, NULL);
      if (error != NO_ERROR)
	{
	  return ER_NET_CANT_CONNECT_SERVER;
	}

      /* check for # of rows to commit */
      if (la_Info.prev_total_rows != la_Info.total_rows)
	{
	  error = la_log_commit ();
	  if (error != NO_ERROR)
	    {
	      return error;
	    }
	  /* sync with new one */
	  la_Info.prev_total_rows = la_Info.total_rows;
	}
    }

  return error;
}


static void
la_init ()
{
  er_log_debug (ARG_FILE_LINE, "log applier will be initialized...");

  memset (&la_Info, 0, sizeof (la_Info));
  la_Info.cache_buffer_size = LA_DEFAULT_CACHE_BUFFER_SIZE;
  la_Info.act_log.pgsize = LA_DEFAULT_LOG_PAGE_SIZE;
  la_Info.act_log.log_vdes = NULL_VOLDES;
  la_Info.arv_log.log_vdes = NULL_VOLDES;
}

static void
la_shutdown ()
{
  er_log_debug (ARG_FILE_LINE, "log applier will be shutting down...");

  /* clean up */
  if (la_Info.arv_log.log_vdes != NULL_VOLDES)
    {
      close (la_Info.arv_log.log_vdes);
      la_Info.arv_log.log_vdes = NULL_VOLDES;
    }
  if (la_Info.act_log.log_vdes != NULL_VOLDES)
    {
      close (la_Info.act_log.log_vdes);
      la_Info.act_log.log_vdes = NULL_VOLDES;
    }
}


/*
 * la_test_log_page() - test the transaction log
 *   return: void
 *   log_path: log path
 *   page_num: test page number
 */
void
la_test_log_page (const char *database_name, const char *log_path,
		  int page_num)
{
  int error = NO_ERROR;
  char *atchar = strchr (database_name, '@');
  if (atchar)
    {
      *atchar = '\0';
    }

  /* init la_Info */
  la_init ();

  if (realpath (log_path, la_Info.log_path) == NULL)
    {
      printf ("Error! cannot find real path of ", log_path);
      return;
    }

  error = la_find_log_pagesize (&la_Info.act_log, la_Info.log_path,
				database_name);
  if (error != NO_ERROR)
    {
      printf ("Error! while reading log header! (error = %d)\n", error);
      exit (EXIT_FAILURE);
    }
  else
    {
      printf ("%30s : %s\n", "magic", la_Info.act_log.log_hdr->magic);
      printf ("%30s : %lu\n", "db_createion",
	      la_Info.act_log.log_hdr->db_creation);
      printf ("%30s : %s\n", "db_release",
	      la_Info.act_log.log_hdr->db_release);
      printf ("%30s : %d\n", "db_iopagesize",
	      la_Info.act_log.log_hdr->db_iopagesize);
      printf ("%30s : %d\n", "is_shutdown",
	      la_Info.act_log.log_hdr->is_shutdown);
      printf ("%30s : %d\n", "next_trid", la_Info.act_log.log_hdr->next_trid);
      printf ("%30s : %d\n", "npages", la_Info.act_log.log_hdr->npages);
      printf ("%30s : %d\n", "fpageid", la_Info.act_log.log_hdr->fpageid);
      printf ("%30s : %d\n", "append_lsa.pageid",
	      la_Info.act_log.log_hdr->append_lsa.pageid);
      printf ("%30s : %d\n", "append_lsa.offset",
	      la_Info.act_log.log_hdr->append_lsa.offset);
      printf ("%30s : %d\n", "chkpt_lsa.pageid",
	      la_Info.act_log.log_hdr->chkpt_lsa.pageid);
      printf ("%30s : %d\n", "chkpt_lsa.offset",
	      la_Info.act_log.log_hdr->chkpt_lsa.offset);
      printf ("%30s : %d\n", "nxarv_pageid",
	      la_Info.act_log.log_hdr->nxarv_pageid);
      printf ("%30s : %d\n", "nxarv_phy_pageid",
	      la_Info.act_log.log_hdr->nxarv_phy_pageid);
      printf ("%30s : %d\n", "nxarv_num", la_Info.act_log.log_hdr->nxarv_num);
      printf ("%30s : %s\n", "prefix_name",
	      la_Info.act_log.log_hdr->prefix_name);
      printf ("%30s : %d\n", "perm_status",
	      la_Info.act_log.log_hdr->perm_status);
      printf ("%30s : %d\n", "ha_server_status",
	      la_Info.act_log.log_hdr->ha_server_status);
      printf ("%30s : %d\n", "ha_file_status",
	      la_Info.act_log.log_hdr->ha_file_status);
      printf ("%30s : %d\n", "eof_lsa.pageid",
	      la_Info.act_log.log_hdr->eof_lsa.pageid);
      printf ("%30s : %d\n", "eof_lsa.offset",
	      la_Info.act_log.log_hdr->eof_lsa.offset);
    }

  if (page_num > 1)
    {
      void *page = malloc (la_Info.act_log.log_hdr->db_iopagesize);
      LOG_PAGE *logpage = (LOG_PAGE *) page;

      if (LA_LOG_IS_IN_ARCHIVE (page_num))
	{
	  /* read from the archive log file */
	  printf (" --- Fetch from archive log ---\n");
	  error = la_log_fetch_from_archive (page_num, (char *) logpage);
	}
      else
	{
	  /* read from the active log file */
	  printf (" --- Fetch from active log ---\n");
	  error =
	    la_log_io_read (la_Info.act_log.path,
			    la_Info.act_log.log_vdes, logpage,
			    la_log_phypageid (page_num),
			    la_Info.act_log.log_hdr->db_iopagesize);
	}
      if (error != NO_ERROR)
	{
	  printf ("Error! while reading log page! (error = %d)\n", error);
	  free_and_init (page);
	  exit (EXIT_FAILURE);
	}
      else
	{
	  struct log_rec *lrec;
	  LOG_LSA lsa;

	  if (LA_LOG_IS_IN_ARCHIVE (page_num))
	    {
	      printf ("%30s : %s\n", "magic", la_Info.arv_log.log_hdr->magic);
	      printf ("%30s : %lu\n", "db_createion",
		      la_Info.arv_log.log_hdr->db_creation);
	      printf ("%30s : %d\n", "next_trid",
		      la_Info.arv_log.log_hdr->next_trid);
	      printf ("%30s : %d\n", "npages",
		      la_Info.arv_log.log_hdr->npages);
	      printf ("%30s : %d\n", "fpageid",
		      la_Info.arv_log.log_hdr->fpageid);
	      printf ("%30s : %d\n", "arv_num",
		      la_Info.arv_log.log_hdr->arv_num);
	    }
	  printf ("  Log page %d (phy: %d pageid: %d, offset %d)\n",
		  page_num, la_log_phypageid (page_num),
		  logpage->hdr.logical_pageid, logpage->hdr.offset);

	  if (logpage->hdr.offset < 0)
	    {
	      printf ("Error! page is exist but it is invalid page!\n");
	      free_and_init (page);
	      exit (EXIT_FAILURE);
	    }

	  lsa.pageid = logpage->hdr.logical_pageid;
	  lsa.offset = logpage->hdr.offset;

	  while (lsa.pageid == page_num)
	    {
	      lrec = (struct log_rec *) ((char *) logpage->area + lsa.offset);

	      printf ("    offset:%04d "
		      "(tid:%d bck p:%d,o:%d frw p:%d,o:%d type:%d)\n",
		      lsa.offset,
		      lrec->trid,
		      lrec->back_lsa.pageid, lrec->back_lsa.offset,
		      lrec->forw_lsa.pageid, lrec->forw_lsa.offset,
		      lrec->type);
	      LSA_COPY (&lsa, &lrec->forw_lsa);
	    }
	}
      free_and_init (page);
    }
}


/*
 * la_apply_log_file() - apply the transaction log to the slave
 *   return: int
 *   database_name: apply database
 *   log_path: log volume path for apply
 *
 * Note:
 *      The main routine.
 *         1. Initialize
 *            . signal process
 *            . get the log file name & IO page size
 *         2. body (loop) - process the request
 *            . catch the request
 *            . if shutdown request --> process
 */
int
la_apply_log_file (const char *database_name, const char *log_path)
{
  int error = NO_ERROR;
  LOG_LSA final;
  LA_CACHE_BUFFER *log_buf = NULL;
  LOG_PAGE *pg_ptr;
  struct log_rec *lrec = NULL;;
  LOG_LSA old_lsa = { -1, -1 };
  struct timeval time_commit, time_now;
  const char *dbname = NULL;	/* the slave db name */
  la_applier_need_shutdown = false;

  /* signal processing */
#if defined(WINDOWS)
  (void) os_set_signal_handler (SIGABRT, la_shutdown_by_signal);
  (void) os_set_signal_handler (SIGINT, la_shutdown_by_signal);
  (void) os_set_signal_handler (SIGTERM, la_shutdown_by_signal);
#else /* ! WINDOWS */
  (void) os_set_signal_handler (SIGSTOP, la_shutdown_by_signal);
  (void) os_set_signal_handler (SIGTERM, la_shutdown_by_signal);
  (void) os_set_signal_handler (SIGPIPE, SIG_IGN);
#endif /* ! WINDOWS */

  if (lzo_init () != LZO_E_OK)
    {
      /* may be impossible */
      er_log_debug (ARG_FILE_LINE, "Cannot initialize LZO");
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      return ER_GENERIC_ERROR;
    }

  /* TODO: refactoring the code */
  dbname = strdup (database_name);
  if (dbname)
    {
      char *atch = strchr (dbname, '@');
      if (atch)
	{
	  *atch = '\0';
	}
    }
  else
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
	      strlen (database_name));
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  /* init la_Info */
  la_init ();

  if (realpath (log_path, la_Info.log_path) == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_DL_PATH, 1, log_path);
      return ER_DL_PATH;
    }

  /* init cache buffer */
  la_Info.cache_pb = la_init_cache_pb ();
  if (la_Info.cache_pb == NULL)
    {
      er_log_debug (ARG_FILE_LINE, "Cannot initialize cache page buffer");
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      return ER_GENERIC_ERROR;
    }
  error =
    la_init_cache_log_buffer (la_Info.cache_pb, la_Info.cache_buffer_size,
			      SIZEOF_LA_CACHE_LOG_BUFFER (la_Info.act_log.
							  pgsize),
			      la_Info.cache_buffer_size);
  if (error != NO_ERROR)
    {
      er_log_debug (ARG_FILE_LINE, "Cannot initialize cache log buffer");
      return error;
    }

  /* get log header info. page size. start_page id, etc */
  error = la_find_log_pagesize (&la_Info.act_log, la_Info.log_path, dbname);
  if (error != NO_ERROR)
    {
      er_log_debug (ARG_FILE_LINE, "Cannot find log page size");
      return error;
    }

  /* find out the last log applied LSA */
  error =
    la_find_last_applied_lsa (&la_Info.last_committed_lsa,
			      la_Info.log_path, &la_Info.act_log);
  if (error != NO_ERROR)
    {
      er_log_debug (ARG_FILE_LINE, "Cannot find last LSA from DB");
      return error;
    }

  /* initialize final_lsa */
  LSA_COPY (&la_Info.final_lsa, &la_Info.last_committed_lsa);

  gettimeofday (&time_commit, NULL);

  /* start the main loop */
  do
    {
      int retry_count = 0;

      /* get next LSA to be processed */
      if (la_apply_pre (&final) == false)
	{
	  error = er_errid ();
	  /* need to shutdown */
	  la_applier_need_shutdown = true;
	  break;
	}

      /* start loop for apply */
      while (!LSA_ISNULL (&final) && !la_applier_need_shutdown)
	{
	  error = la_fetch_log_hdr (&la_Info.act_log);
	  if (error != NO_ERROR)
	    {
	      /* need to shutdown */
	      la_applier_need_shutdown = true;
	      break;
	    }

	  /* check log hdr's master state */
	  if (la_Info.apply_state == HA_LOG_APPLIER_STATE_DONE &&
	      (la_Info.act_log.log_hdr->ha_server_status !=
	       LOG_HA_SRVSTAT_ACTIVE &&
	       la_Info.act_log.log_hdr->ha_server_status !=
	       LOG_HA_SRVSTAT_TO_BE_STANDBY))
	    {
	      /* check server is connected now */
	      error = db_ping_server (0, NULL);
	      if (error != NO_ERROR)
		{
		  er_log_debug (ARG_FILE_LINE,
				"we lost connection with standby DB server.");
		  error = ER_NET_CANT_CONNECT_SERVER;
		  /* need to shutdown */
		  la_applier_need_shutdown = true;
		  break;
		}

	      if (!LSA_EQ (&final, &la_Info.act_log.log_hdr->eof_lsa))
		{
		  /* skip record data and copy final lsa to eof_lsa */
		  LSA_COPY (&final, &la_Info.act_log.log_hdr->eof_lsa);
		  LSA_COPY (&la_Info.final_lsa,
			    &la_Info.act_log.log_hdr->chkpt_lsa);
		}
	      else if (LSA_GT (&la_Info.final_lsa,
			       &la_Info.last_committed_lsa))
		{
		  error = la_log_commit ();
		  if (error == ER_NET_CANT_CONNECT_SERVER ||
		      error == ER_OBJ_NO_CONNECT)
		    {
		      er_log_debug (ARG_FILE_LINE,
				    "we lost connection with DB server.");
		      /* need to shutdown */
		      la_applier_need_shutdown = true;
		      break;
		    }
		}

	      LA_SLEEP (0, 1000 * 1000);
	      continue;
	    }

	  if (la_Info.act_log.log_hdr->eof_lsa.pageid < final.pageid)
	    {
	      LA_SLEEP (0, 100 * 1000);
	      continue;
	    }

	  /* get the target page from log */
	  log_buf = la_get_page_buffer (final.pageid);
	  LSA_COPY (&old_lsa, &final);

	  if (log_buf == NULL)
	    {
	      /* it can be happend when log file is not synced yet */
	      if (la_Info.act_log.log_hdr->ha_file_status ==
		  LOG_HA_FILESTAT_CLEAR)
		{
		  er_log_debug (ARG_FILE_LINE,
				"requested pageid (%d) is not yet exist",
				final.pageid);
		  LA_SLEEP (0, 300 * 1000);
		  continue;
		}
	      /* request page is greater then append_lsa.(in log_header) */
	      else if (la_Info.act_log.log_hdr->append_lsa.pageid <
		       final.pageid)
		{
		  er_log_debug (ARG_FILE_LINE,
				"requested pageid (%d) is greater than "
				"append_las.pageid (%d) in log header",
				final.pageid,
				la_Info.act_log.log_hdr->append_lsa.pageid);
		  LA_SLEEP (0, 100 * 1000);
		  continue;
		}

	      er_log_debug (ARG_FILE_LINE,
			    "requested pageid (%d) may be corrupted ",
			    final.pageid);

	      if (retry_count++ < LA_GET_PAGE_RETRY_COUNT)
		{
		  er_log_debug (ARG_FILE_LINE, "but retry again...",
				final.pageid);
		  LA_SLEEP (0, 300 * 1000 + (retry_count * 100));
		  continue;
		}

	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_PAGE_CORRUPTED,
		      1, final.pageid);
	      error = ER_LOG_PAGE_CORRUPTED;
	      /* need to shutdown */
	      la_applier_need_shutdown = true;
	      break;
	    }
	  else
	    {
	      retry_count = 0;
	    }

	  /* check it and verify it */
	  if (log_buf->logpage.hdr.logical_pageid == final.pageid)
	    {
	      if (log_buf->logpage.hdr.offset < 0)
		{
		  la_invalidate_page_buffer (log_buf, true);
		  if (la_Info.act_log.log_hdr->ha_file_status ==
		      LOG_HA_FILESTAT_SYNCHRONIZED &&
		      (final.pageid + 1) <=
		      la_Info.act_log.log_hdr->eof_lsa.pageid &&
		      la_does_page_exist (final.pageid + 1) !=
		      LA_PAGE_DOESNOT_EXIST)
		    {
		      er_log_debug (ARG_FILE_LINE,
				    "We got a invalid page offset (%d)\n"
				    "  final  LSA(%4d|%4d)"
				    "  append LSA(%4d|%4d)"
				    "  end of LSA(%4d|%4d)\n"
				    "  ha_file_status = %d,"
				    "  is_end_of_record = %d",
				    log_buf->logpage.hdr.offset,
				    final.pageid, final.offset,
				    la_Info.act_log.log_hdr->append_lsa.
				    pageid,
				    la_Info.act_log.log_hdr->append_lsa.
				    offset,
				    la_Info.act_log.log_hdr->eof_lsa.pageid,
				    la_Info.act_log.log_hdr->eof_lsa.offset,
				    la_Info.act_log.log_hdr->ha_file_status,
				    la_Info.is_end_of_record);

		      /* make sure to target page does not exist */
		      if (la_does_page_exist (final.pageid) ==
			  LA_PAGE_DOESNOT_EXIST &&
			  final.pageid <
			  la_Info.act_log.log_hdr->eof_lsa.pageid)
			{
			  er_log_debug (ARG_FILE_LINE,
					"skip this page (pageid=%d/%d/%d)",
					final.pageid,
					la_Info.act_log.log_hdr->eof_lsa.
					pageid,
					la_Info.act_log.log_hdr->append_lsa.
					pageid);
			  /* skip it */
			  final.pageid++;
			  final.offset = 0;
			  continue;
			}
		    }

#if defined (LA_VERBOSE_DEBUG)
		  er_log_debug (ARG_FILE_LINE,
				"refetch this page... (pageid=%d/%d/%d)",
				final.pageid,
				la_Info.act_log.log_hdr->eof_lsa.pageid,
				la_Info.act_log.log_hdr->append_lsa.pageid);
#endif
		  /* wait a moment and retry it */
		  LA_SLEEP (0, 100 * 1000);
		  continue;
		}
	      else
		{
		  /* we get valid page */
		}
	    }
	  else
	    {
	      er_log_debug (ARG_FILE_LINE,
			    "we had a invalid page. "
			    "(expected pageid=%d, but we get %d)",
			    final.pageid,
			    log_buf->logpage.hdr.logical_pageid);

	      la_invalidate_page_buffer (log_buf, true);
	      /* TODO: continue? error ? just sleep and continue? */
	      LA_SLEEP (0, 100 * 1000);

	      continue;
	    }

	  /* apply it */
	  pg_ptr = &(log_buf->logpage);
	  while (final.pageid == log_buf->pageid && !la_applier_need_shutdown)
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

	      /* check for end of log */
	      if (LSA_GT (&final, &la_Info.act_log.log_hdr->eof_lsa))
		{
#if defined (LA_VERBOSE_DEBUG)
		  er_log_debug (ARG_FILE_LINE,
				"this page is grater than eof_lsa. (%d|%d) > "
				"eof (%d|%d). appended (%d|%d)",
				final.pageid, final.offset,
				la_Info.act_log.log_hdr->eof_lsa.pageid,
				la_Info.act_log.log_hdr->eof_lsa.offset,
				la_Info.act_log.log_hdr->append_lsa.pageid,
				la_Info.act_log.log_hdr->append_lsa.offset);
#endif
		  la_Info.is_end_of_record = true;
		  /* it should be refetched and release later */
		  la_invalidate_page_buffer (log_buf, false);
		  break;
		}

	      lrec =
		(struct log_rec *) ((char *) pg_ptr->area + final.offset);

	      /* process the log record */
	      error = la_log_record_process (lrec, &final, pg_ptr);
	      if (error != NO_ERROR)
		{
		  /* check connection error */
		  if (error == ER_NET_CANT_CONNECT_SERVER ||
		      error == ER_OBJ_NO_CONNECT)
		    {
		      /* need to shutdown */
		      la_shutdown ();
		      return ER_NET_CANT_CONNECT_SERVER;
		    }

		  if (error == ER_LOG_PAGE_CORRUPTED)
		    {
		      /* it should be refetched and release later */
		      la_invalidate_page_buffer (log_buf, false);
		    }

		  break;
		}
	      /* set the next record */
	      LSA_COPY (&final, &lrec->forw_lsa);
	    }

	  /* commit */
	  error = la_check_time_commit (&time_commit, 500);
	  if (error != NO_ERROR)
	    {
	      /* check connection error */
	      if (error == ER_NET_CANT_CONNECT_SERVER ||
		  error == ER_OBJ_NO_CONNECT)
		{
		  er_log_debug (ARG_FILE_LINE,
				"we load connection with DB server.");
		  /* need to shutdown */
		  la_applier_need_shutdown = true;
		  break;
		}
	    }

	  /* check and change state */
	  error = la_change_state ();
	  if (error != NO_ERROR)
	    {
	      /* need to shutdown */
	      la_applier_need_shutdown = true;
	      break;
	    }

	  if (final.pageid >= la_Info.act_log.log_hdr->eof_lsa.pageid ||
	      la_Info.is_end_of_record == true)
	    {
	      /* it should be refetched and release */
	      la_invalidate_page_buffer (log_buf, true);
	    }
	  else
	    {
	      la_release_page_buffer (old_lsa.pageid);
	    }

	  /* there is no something new */
	  if (LSA_EQ (&old_lsa, &final))
	    {
	      LA_SLEEP (0, 100 * 1000);
	      continue;
	    }
#if 0
	  /* update applied LSA in db */
	  /* TODO: check interval time and update it */
	  /* TODO: duplicated? */
	  error = la_update_last_applied_lsa (&final, la_Info.log_path);
#endif
	}			/* while (!LSA_ISNULL (&final) && !la_applier_need_shutdown) */
    }
  while (!la_applier_need_shutdown);

  la_shutdown ();

  return error;
}
