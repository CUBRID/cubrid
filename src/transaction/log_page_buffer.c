/*
 * Copyright 2008 Search Solution Corporation
 * Copyright 2016 CUBRID Corporation
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 */

/*
 * log_page_buffer.c -
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/types.h>
#if defined(WINDOWS)
#include <io.h>
#else /* !WINDOWS */
#include <unistd.h>
#endif /* !WINDOWS */
#include <stdarg.h>

#if defined(SOLARIS)
#include <netdb.h>
#endif /* SOLARIS */

#if !defined(WINDOWS)
#include <sys/param.h>
#include <fcntl.h>
#endif /* WINDOWS */

#include <assert.h>

#include "porting.h"
#include "porting_inline.hpp"
#include "connection_defs.h"
#include "language_support.h"
#include "log_append.hpp"
#include "log_impl.h"
#include "log_lsa.hpp"
#include "log_manager.h"
#include "log_comm.h"
#include "log_volids.hpp"
#include "log_writer.h"
#include "lock_manager.h"
#include "log_system_tran.hpp"
#include "boot_sr.h"
#if !defined(SERVER_MODE)
#include "boot_cl.h"
#else /* !SERVER_MODE */
#include "connection_defs.h"
#include "connection_sr.h"
#endif
#include "critical_section.h"
#include "page_buffer.h"
#include "double_write_buffer.h"
#include "file_io.h"
#include "disk_manager.h"
#include "error_manager.h"
#include "xserver_interface.h"
#include "perf_monitor.h"
#include "storage_common.h"
#include "system_parameter.h"
#include "memory_alloc.h"
#include "memory_hash.h"
#include "release_string.h"
#include "message_catalog.h"
#include "msgcat_set_log.hpp"
#include "environment_variable.h"
#include "util_func.h"
#include "errno.h"
#if defined(WINDOWS)
#include "wintcp.h"
#include "connection_error.h"
#else /* WINDOWS */
#include "tcp.h"
#endif /* WINDOWS */
#include "db.h"			/* for db_Connect_status */
#include "log_compress.h"
#include "event_log.h"
#include "tsc_timer.h"
#include "vacuum.h"
#include "thread_entry.hpp"
#include "thread_manager.hpp"
#include "crypt_opfunc.h"
#include "object_representation.h"
#include "flashback.h"
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

#if !defined(SERVER_MODE)
#define pthread_mutex_init(a, b)
#define pthread_mutex_destroy(a)
#define pthread_mutex_lock(a)	0
#define pthread_mutex_unlock(a)
static int rv;
#undef  COND_INIT
#define COND_INIT(a)
#undef  COND_BROADCAST
#define COND_BROADCAST(a)
#undef  COND_DESTROY
#define COND_DESTROY(a)
#endif /* !SERVER_MODE */

#define logpb_log(...) if (logpb_Logging) _er_log_debug (ARG_FILE_LINE, "LOGPB: " __VA_ARGS__)
#define log_archive_er_log(...) \
  if (prm_get_bool_value (PRM_ID_DEBUG_LOG_ARCHIVES)) _er_log_debug (ARG_FILE_LINE, __VA_ARGS__)

#define LOGPB_FIND_BUFPTR(bufid) &log_Pb.buffers[(bufid)]


/* PAGES OF ACTIVE LOG PORTION */
#define LOGPB_HEADER_PAGE_ID             (-9)	/* The first log page in the infinite log sequence. It is always kept
						 * on the active portion of the log. Log records are not stored on this
						 * page. This page is backed up in all archive logs */
#define LOGPB_NEXT_ARCHIVE_PAGE_ID    (log_Gl.hdr.nxarv_pageid)
#define LOGPB_FIRST_ACTIVE_PAGE_ID    (log_Gl.hdr.fpageid)
#define LOGPB_LAST_ACTIVE_PAGE_ID     (log_Gl.hdr.nxarv_pageid + log_Gl.hdr.npages - 1)
#define LOGPB_ACTIVE_NPAGES           (log_Gl.hdr.npages)

/*
 * TRANSLATING LOGICAL LOG PAGES (I.E., PAGES IN THE INFINITE LOG) TO PHYSICAL
 * PAGES IN THE CURRENT LOG FILE
 */
#define LOGPB_PHYSICAL_HEADER_PAGE_ID    0

#define LOGPB_IS_FIRST_PHYSICAL_PAGE(pageid) (logpb_to_physical_pageid(pageid) == 1)

/* ARCHIVE LOG PAGES */
#define LOGPB_IS_ARCHIVE_PAGE(pageid) \
  ((pageid) != LOGPB_HEADER_PAGE_ID && (pageid) < LOGPB_NEXT_ARCHIVE_PAGE_ID)
#define LOGPB_AT_NEXT_ARCHIVE_PAGE_ID(pageid) \
  (logpb_to_physical_pageid(pageid) == log_Gl.hdr.nxarv_phy_pageid)

#define ARV_PAGE_INFO_TABLE_SIZE    256

#define LOG_LAST_APPEND_PTR() ((char *) log_Gl.append.log_pgptr->area + LOGAREA_SIZE)

#define LOG_APPEND_ALIGN(thread_p, current_setdirty) \
  do { \
    if ((current_setdirty) == LOG_SET_DIRTY) \
      { \
        logpb_set_dirty ((thread_p), log_Gl.append.log_pgptr); \
      } \
    log_Gl.hdr.append_lsa.offset = DB_ALIGN (log_Gl.hdr.append_lsa.offset, DOUBLE_ALIGNMENT); \
    if (log_Gl.hdr.append_lsa.offset >= (int) LOGAREA_SIZE) \
      { \
        logpb_next_append_page((thread_p), LOG_DONT_SET_DIRTY); \
      } \
  } while (0)

#define LOG_APPEND_ADVANCE_WHEN_DOESNOT_FIT(thread_p, length) \
  do { \
    if (log_Gl.hdr.append_lsa.offset + (int) (length) >= (int) LOGAREA_SIZE) \
      { \
        logpb_next_append_page ((thread_p), LOG_DONT_SET_DIRTY); \
      } \
  } while (0)

#define LOG_APPEND_SETDIRTY_ADD_ALIGN(thread_p, add) \
  do { \
    log_Gl.hdr.append_lsa.offset += (add); \
    LOG_APPEND_ALIGN ((thread_p), LOG_SET_DIRTY); \
  } while (0)

/* LOG BUFFER STRUCTURE */

typedef struct log_buffer LOG_BUFFER;
struct log_buffer
{
  volatile LOG_PAGEID pageid;	/* Logical page of the log. (Page identifier of the infinite log) */
  volatile LOG_PHY_PAGEID phy_pageid;	/* Physical pageid for the active log portion */
  bool dirty;			/* Is page dirty */
  LOG_PAGE *logpage;		/* The actual buffered log page */
};

/* Status for append record status during logpb_flush_all_append_pages.
 * In normal conditions, only two statuses are used:
 * - LOGPB_APPENDREC_IN_PROGRESS (set when append record is started)
 * - LOGPB_APPENDREC_SUCCESS (set when append record is ended).
 *
 * If a log record append is not ended during flush, then we'll transition the states in the following order:
 * - LOGPB_APPENDREC_IN_PROGRESS => LOGPB_APPENDREC_PARTIAL_FLUSHED_END_OF_LOG
 *   prev_lsa record is overwritten with end of log record and flushed to disk.
 * - LOGPB_APPENDREC_PARTIAL_FLUSHED_END_OF_LOG => LOGPB_APPENDREC_PARTIAL_ENDED
 *   incomplete log record is now completely appended. logpb_flush_all_append_pages is called again.
 * - LOGPB_APPENDREC_PARTIAL_ENDED => LOGPB_APPENDREC_PARTIAL_FLUSHED_ORIGINAL
 *   at the end of last flush, the prev_lsa record is restored and its page is flushed again to disk.
 * - LOGPB_APPENDREC_PARTIAL_FLUSHED_ORIGINAL => LOGPB_APPENDREC_SUCCESS
 *   set the normal state of log record successful append.
 */
typedef enum
{
  LOGPB_APPENDREC_IN_PROGRESS,	/* append record started */

  /* only for partial appended record flush: */
  LOGPB_APPENDREC_PARTIAL_FLUSHED_END_OF_LOG,	/* when flush is forced and record is not fully appended, it is
						 * replaced with end of log and its header page is flushed. */
  LOGPB_APPENDREC_PARTIAL_ENDED,	/* all record has been successfully appended */
  LOGPB_APPENDREC_PARTIAL_FLUSHED_ORIGINAL,	/* original header page is flushed */

  LOGPB_APPENDREC_SUCCESS	/* finished appending record (in a stable way) */
} LOGPB_APPENDREC_STATUS;

/* used to handle records partially written when logpb_flush_all_append_pages is forced */
typedef struct logpb_partial_append LOGPB_PARTIAL_APPEND;
struct logpb_partial_append
{
  LOGPB_APPENDREC_STATUS status;

  char buffer_log_page[IO_MAX_PAGE_SIZE + MAX_ALIGNMENT];
  LOG_PAGE *log_page_record_header;
  LOG_RECORD_HEADER original_record_header;
  LOG_RECORD_HEADER *record_header_p;
};

/* Global structure to trantable, log buffer pool, etc   */
typedef struct log_pb_global_data LOG_PB_GLOBAL_DATA;
struct log_pb_global_data
{
  LOG_BUFFER *buffers;		/* Log buffer pool */
  LOG_PAGE *pages_area;
  LOG_BUFFER header_buffer;
  LOG_PAGE *header_page;
  int num_buffers;		/* Number of log buffers */

  LOGPB_PARTIAL_APPEND partial_append;
};

typedef struct arv_page_info
{
  int arv_num;
  LOG_PAGEID start_pageid;
  LOG_PAGEID end_pageid;
} ARV_PAGE_INFO;

typedef struct
{
  ARV_PAGE_INFO page_info[ARV_PAGE_INFO_TABLE_SIZE];
  int rear;
  int item_count;
} ARV_LOG_PAGE_INFO_TABLE;


#define LOG_MAX_LOGINFO_LINE (PATH_MAX * 4)

/* skip prompting for archive log location */
#if defined(SERVER_MODE)
int log_default_input_for_archive_log_location = 0;
#else
int log_default_input_for_archive_log_location = -1;
#endif

LOG_PB_GLOBAL_DATA log_Pb;

LOG_LOGGING_STAT log_Stat;
static ARV_LOG_PAGE_INFO_TABLE logpb_Arv_page_info_table;

static bool logpb_Initialized = false;
static bool logpb_Logging = false;

/*
 * Functions
 */

static LOG_PAGE *logpb_locate_page (THREAD_ENTRY * thread_p, LOG_PAGEID pageid, PAGE_FETCH_MODE fetch_mode);
static bool logpb_is_dirty (THREAD_ENTRY * thread_p, LOG_PAGE * log_pgptr);
#if !defined(NDEBUG)
static bool logpb_is_any_dirty (THREAD_ENTRY * thread_p);
#endif /* !NDEBUG */
#if defined(CUBRID_DEBUG)
static bool logpb_is_any_fix (THREAD_ENTRY * thread_p);
#endif /* CUBRID_DEBUG */
static void logpb_dump_information (FILE * out_fp);
static void logpb_dump_to_flush_page (FILE * out_fp);
static void logpb_dump_pages (FILE * out_fp);
static void logpb_initialize_backup_info (LOG_HEADER * loghdr);
static LOG_PAGE **logpb_writev_append_pages (THREAD_ENTRY * thread_p, LOG_PAGE ** to_flush, DKNPAGES npages);
static int logpb_get_guess_archive_num (THREAD_ENTRY * thread_p, LOG_PAGEID pageid);
static void logpb_set_unavailable_archive (THREAD_ENTRY * thread_p, int arv_num);
static void logpb_dismount_log_archive (THREAD_ENTRY * thread_p);
static bool logpb_is_archive_available (THREAD_ENTRY * thread_p, int arv_num);
static void logpb_archive_active_log (THREAD_ENTRY * thread_p);
static int logpb_remove_archive_logs_internal (THREAD_ENTRY * thread_p, int first, int last, const char *info_reason);
static void logpb_append_archives_removed_to_log_info (int first, int last, const char *info_reason);
static int logpb_verify_length (const char *db_fullname, const char *log_path, const char *log_prefix);
static int logpb_backup_for_volume (THREAD_ENTRY * thread_p, VOLID volid, LOG_LSA * chkpt_lsa,
				    FILEIO_BACKUP_SESSION * session, bool only_updated);
static int logpb_update_backup_volume_info (const char *bkupinfo_file_name);
static int logpb_start_where_path (const char *to_db_fullname, const char *toext_path, const char **toext_name,
				   char **ext_path, char **alloc_extpath, const char *fileof_vols_and_wherepaths,
				   FILE ** where_paths_fp);
static int logpb_next_where_path (const char *to_db_fullname, const char *toext_path, const char *ext_name,
				  char *ext_path, const char *fileof_vols_and_wherepaths, FILE * where_paths_fp,
				  int num_perm_vols, VOLID volid, char *from_volname, char *to_volname);
static int logpb_copy_volume (THREAD_ENTRY * thread_p, VOLID from_volid, const char *tonew_volname, INT64 * db_creation,
			      LOG_LSA * vol_chkpt_lsa);
static bool logpb_check_if_exists (const char *fname, char *first_vol);
#if defined(SERVER_MODE)
static int logpb_backup_needed_archive_logs (THREAD_ENTRY * thread_p, FILEIO_BACKUP_SESSION * session,
					     int first_arv_num, int last_arv_num);
#endif /* SERVER_MODE */
static bool logpb_remote_ask_user_before_delete_volumes (THREAD_ENTRY * thread_p, const char *volpath);
static int logpb_initialize_flush_info (void);
static void logpb_finalize_flush_info (void);
static void logpb_finalize_writer_info (void);
static void logpb_dump_log_header (FILE * outfp);
static void logpb_dump_parameter (FILE * outfp);
static void logpb_dump_runtime (FILE * outfp);
static void logpb_initialize_log_buffer (LOG_BUFFER * log_buffer_p, LOG_PAGE * log_pg);

static int logpb_check_stop_at_time (FILEIO_BACKUP_SESSION * session, time_t stop_at, time_t backup_time);
static void logpb_write_toflush_pages_to_archive (THREAD_ENTRY * thread_p);
static int logpb_add_archive_page_info (THREAD_ENTRY * thread_p, int arv_num, LOG_PAGEID start_page,
					LOG_PAGEID end_page);
static int logpb_get_archive_num_from_info_table (THREAD_ENTRY * thread_p, LOG_PAGEID page_id);

static int logpb_flush_all_append_pages (THREAD_ENTRY * thread_p);
static int logpb_append_next_record (THREAD_ENTRY * thread_p, LOG_PRIOR_NODE * ndoe);

static void logpb_start_append (THREAD_ENTRY * thread_p, LOG_RECORD_HEADER * header);
static void logpb_end_append (THREAD_ENTRY * thread_p, LOG_RECORD_HEADER * header);
static void logpb_append_data (THREAD_ENTRY * thread_p, int length, const char *data);
static void logpb_append_crumbs (THREAD_ENTRY * thread_p, int num_crumbs, const LOG_CRUMB * crumbs);
static void logpb_next_append_page (THREAD_ENTRY * thread_p, LOG_SETDIRTY current_setdirty);
static LOG_PRIOR_NODE *prior_lsa_remove_prior_list (THREAD_ENTRY * thread_p);
static int logpb_append_prior_lsa_list (THREAD_ENTRY * thread_p, LOG_PRIOR_NODE * list);
static int logpb_copy_page (THREAD_ENTRY * thread_p, LOG_PAGEID pageid, LOG_CS_ACCESS_MODE access_mode,
			    LOG_PAGE * log_pgptr);

static void logpb_fatal_error_internal (THREAD_ENTRY * thread_p, bool log_exit, bool need_flush, const char *file_name,
					const int lineno, const char *fmt, va_list ap);

static int logpb_copy_log_header (THREAD_ENTRY * thread_p, LOG_HEADER * to_hdr, const LOG_HEADER * from_hdr);
STATIC_INLINE LOG_BUFFER *logpb_get_log_buffer (LOG_PAGE * log_pg) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int logpb_get_log_buffer_index (LOG_PAGEID log_pageid) __attribute__ ((ALWAYS_INLINE));
static int logpb_compute_page_checksum (THREAD_ENTRY * thread_p, LOG_PAGE * log_pgptr, int *checksum_crc32);
static int logpb_page_has_valid_checksum (THREAD_ENTRY * thread_p, LOG_PAGE * log_pgptr, bool * has_valid_checksum);

static bool logpb_is_log_active_from_backup_useful (THREAD_ENTRY * thread_p, const char *active_log_path,
						    const char *db_full_name);
static int logpb_peek_header_of_active_log_from_backup (THREAD_ENTRY * thread_p, const char *active_log_path,
							LOG_HEADER * hdr);

/*
 * FUNCTIONS RELATED TO LOG BUFFERING
 *
 */

/*
 * logpb_get_log_buffer_index - get the current index in the log buffer
 * return: index number
 * log_pageid (in) : the pageid number
 */
STATIC_INLINE int
logpb_get_log_buffer_index (LOG_PAGEID log_pageid)
{
  return log_pageid % log_Pb.num_buffers;
}

/*
 * logpb_get_log_buffer - get the buffer from the log page
 * return: the coresponding buffer
 * log_pg (in) : the log page
 * NOTE: the function finds the index of the log page and returns
 * the coresponding buffer
 */
STATIC_INLINE LOG_BUFFER *
logpb_get_log_buffer (LOG_PAGE * log_pg)
{
  int index;

  if (log_pg == log_Pb.header_page)
    {
      return &log_Pb.header_buffer;
    }

  assert ((UINT64) ((char *) log_pg - (char *) log_Pb.pages_area) / LOG_PAGESIZE < INT_MAX);
  index = (int) ((UINT64) ((char *) log_pg - (char *) log_Pb.pages_area) / LOG_PAGESIZE);

  /* Safe guard: index is valid. */
  assert (index >= 0 && index < log_Pb.num_buffers);
  /* Safe guard: log_pg is correctly aligned. */
  assert ((char *) log_Pb.pages_area + (UINT64) LOG_PAGESIZE * index == (char *) log_pg);

  return &log_Pb.buffers[index];
}

/*
 * logpb_initialize_log_buffer -
 *
 * return: nothing
 *
 *   log_buffer_p(in/oiut):
 *
 * NOTE:
 *
 */
static void
logpb_initialize_log_buffer (LOG_BUFFER * log_buffer_p, LOG_PAGE * log_pg)
{
  log_buffer_p->pageid = NULL_PAGEID;
  log_buffer_p->phy_pageid = NULL_PAGEID;
  log_buffer_p->dirty = false;
  log_buffer_p->logpage = log_pg;
  log_buffer_p->logpage->hdr.logical_pageid = NULL_PAGEID;
  log_buffer_p->logpage->hdr.offset = NULL_OFFSET;
  log_buffer_p->logpage->hdr.flags = 0;
}

/*
 * logpb_compute_page_checksum - Computes log page checksum.
 * return: error code
 * thread_p (in) : thread entry
 * log_pgptr (in) : log page pointer
 * checksum_crc32(out): computed checksum
 *   Note: Currently CRC32 is used as checksum.
 *   Note: any changes to this requires changes to logwr_check_page_checksum
 */
static int
logpb_compute_page_checksum (THREAD_ENTRY * thread_p, LOG_PAGE * log_pgptr, int *checksum_crc32)
{
  int error_code = NO_ERROR, saved_checksum_crc32;
  const int block_size = 4096;
  const int max_num_pages = IO_MAX_PAGE_SIZE / block_size;
  const int sample_nbytes = 16;
  int sampling_offset;
  char buf[max_num_pages * sample_nbytes * 2];
  const int num_pages = LOG_PAGESIZE / block_size;
  const size_t sizeof_buf = num_pages * sample_nbytes * 2;

  assert (log_pgptr != NULL && checksum_crc32 != NULL);

  /* Save the old page checksum. */
  saved_checksum_crc32 = log_pgptr->hdr.checksum;

  /* Resets checksum to not affect the new computation. */
  log_pgptr->hdr.checksum = 0;

  char *p = buf;
  for (int i = 0; i < num_pages; i++)
    {
      // first
      sampling_offset = (i * block_size);
      memcpy (p, ((char *) log_pgptr) + sampling_offset, sample_nbytes);
      p += sample_nbytes;

      // last
      sampling_offset = (i * block_size) + (block_size - sample_nbytes);
      memcpy (p, ((char *) log_pgptr) + sampling_offset, sample_nbytes);
      p += sample_nbytes;
    }

  crypt_crc32 ((char *) buf, (int) sizeof_buf, checksum_crc32);

  /* Restores the saved checksum */
  log_pgptr->hdr.checksum = saved_checksum_crc32;

  return error_code;
}

/*
 * logpb_set_page_checksum - Set log page checksum.
 * return: error code
 * thread_p (in) : thread entry
 * log_pgptr (in) : log page pointer
 *   Note: Currently CRC32 is used as checksum.
 */
int
logpb_set_page_checksum (THREAD_ENTRY * thread_p, LOG_PAGE * log_pgptr)
{
  int error_code = NO_ERROR, checksum_crc32;

  assert (log_pgptr != NULL);

  /* Computes the page checksum. */
  error_code = logpb_compute_page_checksum (thread_p, log_pgptr, &checksum_crc32);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  log_pgptr->hdr.checksum = checksum_crc32;
  logpb_log ("logpb_set_page_checksum: log page %lld has checksum = %d\n",
	     (long long int) log_pgptr->hdr.logical_pageid, checksum_crc32);

  return NO_ERROR;
}

/*
 * logpb_page_has_valid_checksum - Check whether the log page checksum is valid.
 *   return: error code
 *   thread_p(in): thread entry
 *   log_pgptr(in): the log page
 *   has_valid_checksum(out): true, if has valid checksum.
 */
static int
logpb_page_has_valid_checksum (THREAD_ENTRY * thread_p, LOG_PAGE * log_pgptr, bool * has_valid_checksum)
{
  int checksum_crc32, error_code = NO_ERROR;

  assert (log_pgptr != NULL && has_valid_checksum != NULL);

  error_code = logpb_compute_page_checksum (thread_p, log_pgptr, &checksum_crc32);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  *has_valid_checksum = (checksum_crc32 == log_pgptr->hdr.checksum);
  if (*has_valid_checksum == false)
    {
      logpb_log ("logpb_page_has_valid_checksum: log page %lld has checksum = %d, computed checksum = %d\n",
		 (long long int) log_pgptr->hdr.logical_pageid, log_pgptr->hdr.checksum, checksum_crc32);
    }

  return NO_ERROR;
}

/*
 * logpb_initialize_pool - Initialize the log buffer pool
 *
 * return: NO_ERROR if all OK, ER_ status otherwise
 *
 * NOTE:Initialize the log buffer pool. All resident pages are invalidated.
 */
int
logpb_initialize_pool (THREAD_ENTRY * thread_p)
{
  int error_code = NO_ERROR;
  int i;
  LOG_GROUP_COMMIT_INFO *group_commit_info = &log_Gl.group_commit_info;
  LOGWR_INFO *writer_info = log_Gl.writer_info;
  size_t size;

  assert (LOG_CS_OWN_WRITE_MODE (thread_p));

  log_append_init_zip ();

  if (logpb_Initialized == true)
    {
      logpb_finalize_pool (thread_p);
    }

  assert (log_Pb.pages_area == NULL);
  assert (logpb_Initialized == false);

  logpb_Logging = prm_get_bool_value (PRM_ID_LOGPB_LOGGING_DEBUG);

  /*
   * Create an area to keep the number of desired buffers
   */
  log_Pb.num_buffers = prm_get_integer_value (PRM_ID_LOG_NBUFFERS);

  /* allocate a pointer array to point to each buffer */
  size = ((size_t) log_Pb.num_buffers * sizeof (*log_Pb.buffers));
  log_Pb.buffers = (LOG_BUFFER *) malloc (size);
  if (log_Pb.buffers == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, size);
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  size = ((size_t) log_Pb.num_buffers * (LOG_PAGESIZE));
  log_Pb.pages_area = (LOG_PAGE *) malloc (size);
  if (log_Pb.pages_area == NULL)
    {
      free_and_init (log_Pb.buffers);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, size);
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  /* Initialize every new buffer */
  memset (log_Pb.pages_area, LOG_PAGE_INIT_VALUE, size);
  for (i = 0; i < log_Pb.num_buffers; i++)
    {
      logpb_initialize_log_buffer (&log_Pb.buffers[i],
				   (LOG_PAGE *) ((char *) log_Pb.pages_area + (UINT64) i * (LOG_PAGESIZE)));
    }

  size = LOG_PAGESIZE;
  log_Pb.header_page = (LOG_PAGE *) malloc (size);
  if (log_Pb.header_page == NULL)
    {
      free_and_init (log_Pb.buffers);
      free_and_init (log_Pb.pages_area);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, size);
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  memset (log_Pb.header_page, LOG_PAGE_INIT_VALUE, size);
  logpb_initialize_log_buffer (&log_Pb.header_buffer, log_Pb.header_page);

  error_code = logpb_initialize_flush_info ();
  if (error_code != NO_ERROR)
    {
      goto error;
    }

  /* Initialize partial append */
  log_Pb.partial_append.status = LOGPB_APPENDREC_SUCCESS;
  log_Pb.partial_append.log_page_record_header =
    (LOG_PAGE *) PTR_ALIGN (log_Pb.partial_append.buffer_log_page, MAX_ALIGNMENT);

#if !defined (NDEBUG)
  // suppress valgrind complaint.
  memset (log_Pb.partial_append.log_page_record_header, LOG_PAGE_INIT_VALUE, IO_MAX_PAGE_SIZE);
#endif // DEBUG

  logpb_Initialized = true;
  pthread_mutex_init (&log_Gl.chkpt_lsa_lock, NULL);

  pthread_cond_init (&group_commit_info->gc_cond, NULL);
  pthread_mutex_init (&group_commit_info->gc_mutex, NULL);

  pthread_mutex_init (&writer_info->wr_list_mutex, NULL);

  pthread_cond_init (&writer_info->flush_start_cond, NULL);
  pthread_mutex_init (&writer_info->flush_start_mutex, NULL);

  pthread_cond_init (&writer_info->flush_wait_cond, NULL);
  pthread_mutex_init (&writer_info->flush_wait_mutex, NULL);

  pthread_cond_init (&writer_info->flush_end_cond, NULL);
  pthread_mutex_init (&writer_info->flush_end_mutex, NULL);

  writer_info->is_init = true;

  return error_code;

error:

  logpb_finalize_pool (thread_p);
  logpb_fatal_error (thread_p, false, ARG_FILE_LINE, "log_pbpool_init");

  return error_code;
}

/*
 * logpb_finalize_pool - TERMINATES THE LOG BUFFER POOL
 *
 * return: nothing
 *
 * NOTE:Terminate the log buffer pool. All log resident pages are invalidated.
 */
void
logpb_finalize_pool (THREAD_ENTRY * thread_p)
{
  assert (LOG_CS_OWN_WRITE_MODE (NULL));

  if (logpb_Initialized == false)
    {
      /* logpb already finalized */
      return;
    }

  if (log_Gl.append.log_pgptr != NULL)
    {
      log_Gl.append.log_pgptr = NULL;
    }
  log_Gl.append.set_nxio_lsa (NULL_LSA);
  LSA_SET_NULL (&log_Gl.append.prev_lsa);
  /* copy log_Gl.append.prev_lsa to log_Gl.prior_info.prev_lsa */
  LOG_RESET_PREV_LSA (&log_Gl.append.prev_lsa);

#if defined(CUBRID_DEBUG)
  if (logpb_is_any_dirty (thread_p) == true || logpb_is_any_fix (thread_p) == true)
    {
      er_log_debug (ARG_FILE_LINE, "log_pbpool_final: Log Buffer pool contains dirty or fixed pages at the end.\n");
      logpb_dump (thread_p, stdout);
    }
#endif /* CUBRID_DEBUG */

  free_and_init (log_Pb.buffers);
  free_and_init (log_Pb.pages_area);
  free_and_init (log_Pb.header_page);
  log_Pb.num_buffers = 0;
  logpb_Initialized = false;
  logpb_finalize_flush_info ();

  pthread_mutex_destroy (&log_Gl.chkpt_lsa_lock);

  pthread_mutex_destroy (&log_Gl.group_commit_info.gc_mutex);
  pthread_cond_destroy (&log_Gl.group_commit_info.gc_cond);

  logpb_finalize_writer_info ();

  log_append_final_zip ();
}

/*
 * logpb_is_pool_initialized - Find out if buffer pool has been initialized
 *
 * return:
 *
 * NOTE:Find out if the buffer pool has been initialized.
 */
bool
logpb_is_pool_initialized (void)
{
  assert (LOG_CS_OWN_WRITE_MODE (NULL));

  return logpb_Initialized;
}

/*
 * logpb_invalidate_pool - Invalidate all buffers in buffer pool
 *
 * return: Pointer to the page or NULL
 *
 * NOTE:Invalidate all unfixed buffers in the buffer pool.
 *              This is needed when we reset the log header information.
 */
void
logpb_invalidate_pool (THREAD_ENTRY * thread_p)
{
  LOG_BUFFER *log_bufptr;	/* A log buffer */
  int i;

  assert (LOG_CS_OWN_WRITE_MODE (thread_p));

  if (logpb_Initialized == false)
    {
      return;
    }

  /*
   * Flush any append dirty buffers at this moment.
   * Then, invalidate any buffer that it is not fixed and dirty
   */
  logpb_flush_pages_direct (thread_p);

  for (i = 0; i < log_Pb.num_buffers; i++)
    {
      log_bufptr = LOGPB_FIND_BUFPTR (i);
      if (log_bufptr->pageid != NULL_PAGEID && !log_bufptr->dirty == false)
	{
	  logpb_initialize_log_buffer (log_bufptr, log_bufptr->logpage);
	}
    }
}


/*
 * logpb_create_page - Create a log page on a log buffer
 *
 * return: Pointer to the page or NULL
 *
 *   pageid(in): Page identifier
 *
 * NOTE:Creates the log page identified by pageid on a log buffer and return such buffer.
 *              Just initializes log buffer hdr,
 *              To read a page from disk is not needed.
 */
LOG_PAGE *
logpb_create_page (THREAD_ENTRY * thread_p, LOG_PAGEID pageid)
{
  return logpb_locate_page (thread_p, pageid, NEW_PAGE);
}

/*
 * logpb_locate_page - Fetch a log page
 *
 * return: Pointer to the page or NULL
 *
 *   pageid(in): Page identifier
 *   fetch_mode(in): Is this a new log page ?. That is, can we avoid the I/O
 *
 * NOTE: first, the function checks if the pageid is the header page id.
 *       if it is not, it brings the coresponding page from the log page buffer.
 *       if the actual pageid differs from the buffer pageid, it means that it should
 *       be invalidated - it contains another page - and it is flushed to disk, if it
 *       is dirty. Now, if the pageid is null , we have a clear log page. If the fetch
 *       mode is NEW_PAGE, it set the fields in the buffer, else, if it is OLD_PAGE,
 *       it brings the page from the disk. The last case is if the page is not NULL, and
 *       it is equal with the pageid. This means it is an OLD_PAGE request, and it returns
 *       that page.
 */
static LOG_PAGE *
logpb_locate_page (THREAD_ENTRY * thread_p, LOG_PAGEID pageid, PAGE_FETCH_MODE fetch_mode)
{
  LOG_BUFFER *log_bufptr = NULL;	/* A log buffer */
  LOG_PHY_PAGEID phy_pageid = NULL_PAGEID;	/* The corresponding physical page */
  bool is_perf_tracking;
  TSC_TICKS start_tick, end_tick;
  TSCTIMEVAL tv_diff;
  UINT64 fix_wait_time;
  PERF_PAGE_MODE stat_page_found = PERF_PAGE_MODE_OLD_IN_BUFFER;
  int index;

  logpb_log ("called logpb_locate_page for pageid %lld, fetch_mode=%s", (long long int) pageid,
	     fetch_mode == NEW_PAGE ? "new_page" : "old_page\n");

  is_perf_tracking = perfmon_is_perf_tracking ();
  if (is_perf_tracking)
    {
      tsc_getticks (&start_tick);
    }

  assert (pageid != NULL_PAGEID);
  assert ((fetch_mode == NEW_PAGE) || (fetch_mode == OLD_PAGE));
  assert (LOG_CS_OWN_WRITE_MODE (thread_p));

  if (pageid == LOGPB_HEADER_PAGE_ID)
    {
      log_bufptr = &log_Pb.header_buffer;
    }
  else
    {
      index = logpb_get_log_buffer_index (pageid);
      if (index >= 0 && index < log_Pb.num_buffers)
	{
	  log_bufptr = &log_Pb.buffers[index];
	}
      else
	{
	  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_PAGE_CORRUPTED, 1, pageid);
	  return NULL;
	}

    }
  assert (log_bufptr != NULL);

  if (log_bufptr->pageid != NULL_PAGEID && log_bufptr->pageid != pageid)
    {
      if (log_bufptr->dirty == true)
	{
	  /* should not happen */
	  assert_release (false);
	  logpb_log ("logpb_locate_page: fatal error, victimizing dirty log page %lld.\n",
		     (long long int) log_bufptr->pageid);

	  if (logpb_write_page_to_disk (thread_p, log_bufptr->logpage, log_bufptr->pageid) != NO_ERROR)
	    {
	      assert_release (false);
	      return NULL;
	    }
	  log_bufptr->dirty = false;
	  perfmon_inc_stat (thread_p, PSTAT_LOG_NUM_REPLACEMENTS_IOWRITES);
	}

      log_bufptr->pageid = NULL_PAGEID;	/* invalidate buffer */
      perfmon_inc_stat (thread_p, PSTAT_LOG_NUM_REPLACEMENTS);
    }

  if (log_bufptr->pageid == NULL_PAGEID)
    {
      if (fetch_mode == NEW_PAGE)
	{
	  /* Fills log page with 0xff, for checksum consistency. */
	  memset (log_bufptr->logpage, LOG_PAGE_INIT_VALUE, LOG_PAGESIZE);
	  log_bufptr->logpage->hdr.logical_pageid = pageid;
	  log_bufptr->logpage->hdr.offset = NULL_OFFSET;
	  log_bufptr->logpage->hdr.flags = 0;
	}
      else
	{
	  stat_page_found = PERF_PAGE_MODE_OLD_LOCK_WAIT;
	  if (logpb_read_page_from_file (thread_p, pageid, LOG_CS_FORCE_USE, log_bufptr->logpage) != NO_ERROR)
	    {
	      return NULL;
	    }
	}
      phy_pageid = logpb_to_physical_pageid (pageid);
      log_bufptr->phy_pageid = phy_pageid;
      log_bufptr->pageid = pageid;
    }
  else
    {
      assert (fetch_mode == OLD_PAGE);
      assert (log_bufptr->pageid == pageid);
      logpb_log ("logpb_locate_page using log buffer entry for pageid = %lld", pageid);
    }

  perfmon_inc_stat (thread_p, PSTAT_LOG_NUM_FETCHES);
  if (is_perf_tracking)
    {
      tsc_getticks (&end_tick);
      tsc_elapsed_time_usec (&tv_diff, end_tick, start_tick);
      fix_wait_time = tv_diff.tv_sec * 1000000LL + tv_diff.tv_usec;
      if (fix_wait_time > 0)
	{
	  perfmon_pbx_fix_acquire_time (thread_p, PAGE_LOG, stat_page_found, PERF_HOLDER_LATCH_READ,
					PERF_UNCONDITIONAL_FIX_WITH_WAIT, fix_wait_time);
	}
    }

  ASSERT_ALIGN (log_bufptr->logpage->area, MAX_ALIGNMENT);
  return log_bufptr->logpage;
}

/*
 * logpb_set_dirty - Mark the current page dirty
 *
 * return: nothing
 *
 *   log_pgptr(in): Log page pointer
 *
 * NOTE:Mark the current log page as dirty.
 */
void
logpb_set_dirty (THREAD_ENTRY * thread_p, LOG_PAGE * log_pgptr)
{
  LOG_BUFFER *bufptr;		/* Log buffer associated with given page */

  /* Get the address of the buffer from the page. */
  bufptr = logpb_get_log_buffer (log_pgptr);
  if (!bufptr->dirty)
    {
      logpb_log ("dirty flag set for pageid = %lld\n", (long long int) bufptr->pageid);
    }
#if defined(CUBRID_DEBUG)
  if (bufptr->pageid != LOGPB_HEADER_PAGE_ID
      && (bufptr->pageid < LOGPB_NEXT_ARCHIVE_PAGE_ID || bufptr->pageid > LOGPB_LAST_ACTIVE_PAGE_ID))
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_LOG_FLUSHING_UNUPDATABLE, 1, bufptr->pageid);
    }
#endif /* CUBRID_DEBUG */

  bufptr->dirty = true;
}

/*
 * logpb_is_dirty - Find if current log page pointer is dirty
 *
 * return:
 *
 *   log_pgptr(in): Log page pointer
 *
 * NOTE:Find if the current log page is dirty.
 */
static bool
logpb_is_dirty (THREAD_ENTRY * thread_p, LOG_PAGE * log_pgptr)
{
  LOG_BUFFER *bufptr;		/* Log buffer associated with given page */
  bool is_dirty;

  assert (LOG_CS_OWN_WRITE_MODE (thread_p));

  /* Get the address of the buffer from the page. */
  bufptr = logpb_get_log_buffer (log_pgptr);
  is_dirty = (bool) bufptr->dirty;

  return is_dirty;
}

#if !defined(NDEBUG)
/*
 * logpb_is_any_dirty - FIND IF ANY LOG BUFFER IS DIRTY
 *
 * return:
 *
 * NOTE:Find if any log buffer is dirty.
 */
static bool
logpb_is_any_dirty (THREAD_ENTRY * thread_p)
{
  LOG_BUFFER *bufptr;		/* A log buffer */
  int i;
  bool ret;

  assert (LOG_CS_OWN_WRITE_MODE (thread_p));

  ret = false;
  for (i = 0; i < log_Pb.num_buffers; i++)
    {
      bufptr = LOGPB_FIND_BUFPTR (i);
      if (bufptr->dirty == true)
	{
	  ret = true;
	  break;
	}
    }

  return ret;
}
#endif /* !NDEBUG || CUBRID_DEBUG */

#if defined(CUBRID_DEBUG)
/*
 * logpb_is_any_fix - Find if any log buffer is fixed
 *
 * return:
 *
 * NOTE:Find if any buffer is fixed
 */
static bool
logpb_is_any_fix (THREAD_ENTRY * thread_p)
{
  LOG_BUFFER *bufptr;		/* A log buffer */
  int i, rv;
  bool ret;

  assert (LOG_CS_OWN_WRITE_MODE (thread_p));

  ret = false;
  for (i = 0; i < log_Pb.num_buffers; i++)
    {
      bufptr = LOGPB_FIND_BUFPTR (i);
      if (bufptr->pageid != NULL_PAGEID)
	{
	  ret = true;
	  break;
	}
    }

  return ret;
}
#endif /* CUBRID_DEBUG */

/*
 * logpb_flush_page - Flush a page of the active portion of the log to disk
 *
 * return: nothing
 *
 *   log_pgptr(in): Log page pointer
 *
 * NOTE:The log page (of the active portion of the log) associated
 *              with pageptr is written out to disk and is optionally freed.
 */
int
logpb_flush_page (THREAD_ENTRY * thread_p, LOG_PAGE * log_pgptr)
{
  LOG_BUFFER *bufptr;		/* Log buffer associated with given page */

  /* Get the address of the buffer from the page. */
  bufptr = logpb_get_log_buffer (log_pgptr);

  assert (LOG_CS_OWN_WRITE_MODE (thread_p));

  logpb_log ("called logpb_flush_page for pageid = %lld\n", (long long int) bufptr->pageid);

#if defined(CUBRID_DEBUG)
  if (bufptr->pageid != LOGPB_HEADER_PAGE_ID
      && (bufptr->pageid < LOGPB_NEXT_ARCHIVE_PAGE_ID || bufptr->pageid > LOGPB_LAST_ACTIVE_PAGE_ID))
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_LOG_FLUSHING_UNUPDATABLE, 1, bufptr->pageid);
      return ER_LOG_FLUSHING_UNUPDATABLE;
    }
  if (bufptr->phy_pageid == NULL_PAGEID || bufptr->phy_pageid != logpb_to_physical_pageid (bufptr->pageid))
    {
      /* Bad physical log page for such logical page */
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_PAGE_CORRUPTED, 1, bufptr->pageid);
      logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "logpb_flush_page");
      return ER_LOG_PAGE_CORRUPTED;
    }
#endif /* CUBRID_DEBUG */

  if (bufptr->dirty == true)
    {
      /*
       * The buffer is dirty, flush it
       */

      /*
       * Even when the log has been open with the o_sync option, force a sync
       * since some Operationg system (HP) seems that does not have the effect
       * of forcing the page to disk without doing fync
       */

      if (logpb_write_page_to_disk (thread_p, log_pgptr, bufptr->pageid) != NO_ERROR)
	{
	  goto error;
	}
      else
	{
	  bufptr->dirty = false;
	}
    }

  return NO_ERROR;

error:

  return ER_FAILED;
}

/*
 * logpb_get_page_id - Logical pageid of log buffer/page
 *
 * return: pageid
 *
 *   log_pgptr(in): Log page pointer
 *
 * NOTE:The page identifier of the given log page/buffer.
 *              The page is always fix when this funtion is called.
 *              In replacement, the page cannot be replaced because fix > 0.
 *              So, it isn't needed to lock mutex.
 */
LOG_PAGEID
logpb_get_page_id (LOG_PAGE * log_pgptr)
{
  LOG_BUFFER *bufptr;		/* Log buffer associated with given page */

  bufptr = logpb_get_log_buffer (log_pgptr);

  return bufptr->pageid;
}

/*
 * logpb_dump - DUMP THE LOG PAGE BUFFER POOL
 *
 * return: nothing
 *
 * NOTE:Dump the log page buffer pool. This function is used for debugging purposes.
 */
void
logpb_dump (THREAD_ENTRY * thread_p, FILE * out_fp)
{
  if (logpb_Initialized == false)
    {
      return;
    }

  assert (LOG_CS_OWN_WRITE_MODE (thread_p));

  logpb_dump_information (out_fp);

  if (log_Gl.flush_info.num_toflush > 0)
    {
      logpb_dump_to_flush_page (out_fp);
    }

  (void) fprintf (out_fp, "\n\n");
  (void) fprintf (out_fp, "Buf Log_Pageid Phy_pageid Drt Rct Bufaddr   Pagearea    HDR:Pageid offset\n");

  logpb_dump_pages (out_fp);
}

/*
 * logpb_dump_information -
 *
 * return: nothing
 *
 * NOTE:
 */
static void
logpb_dump_information (FILE * out_fp)
{
  long long int append;
  int i;

  fprintf (out_fp, "\n\n ** DUMP OF LOG BUFFER POOL INFORMATION **\n\n");

  fprintf (out_fp, "\nHash table dump\n");
  for (i = 0; i < log_Pb.num_buffers; i++)
    {
      fprintf (out_fp, "Pageid = %5lld, Address = %p\n", (long long int) i, (void *) &log_Pb.buffers[i]);
    }
  fprintf (out_fp, "\n\n");

  fprintf (out_fp, " Next IO_LSA = %lld|%d, Current append LSA = %lld|%d, Prev append LSA = %lld|%d\n"
	   " Prior LSA = %lld|%d, Prev prior LSA = %lld|%d\n\n",
	   (long long int) log_Gl.append.get_nxio_lsa ().pageid, (int) log_Gl.append.get_nxio_lsa ().offset,
	   (long long int) log_Gl.hdr.append_lsa.pageid, (int) log_Gl.hdr.append_lsa.offset,
	   (long long int) log_Gl.append.prev_lsa.pageid, (int) log_Gl.append.prev_lsa.offset,
	   (long long int) log_Gl.prior_info.prior_lsa.pageid, (int) log_Gl.prior_info.prior_lsa.offset,
	   (long long int) log_Gl.prior_info.prev_lsa.pageid, (int) log_Gl.prior_info.prev_lsa.offset);

  if (log_Gl.append.log_pgptr == NULL)
    {
      append = NULL_PAGEID;
    }
  else
    {
      append = logpb_get_page_id (log_Gl.append.log_pgptr);
    }

  fprintf (out_fp, " Append to_flush array: max = %d, num_active = %d\n"
	   " Current append page = %lld\n", log_Gl.flush_info.max_toflush, log_Gl.flush_info.num_toflush, append);
}

/*
 * logpb_dump_to_flush_page -
 *
 * return: nothing
 *
 * NOTE:
 */
static void
logpb_dump_to_flush_page (FILE * out_fp)
{
  int i;
  LOG_BUFFER *log_bufptr;
  LOG_FLUSH_INFO *flush_info = &log_Gl.flush_info;

  (void) fprintf (out_fp, " Candidate append pages to flush are:\n");

  for (i = 0; i < flush_info->num_toflush; i++)
    {
      log_bufptr = logpb_get_log_buffer (flush_info->toflush[i]);
      if (i != 0)
	{
	  if ((i % 10) == 0)
	    {
	      fprintf (out_fp, ",\n");
	    }
	  else
	    {
	      fprintf (out_fp, ",");
	    }
	}
      fprintf (out_fp, " %4lld", (long long int) log_bufptr->pageid);
    }

  fprintf (out_fp, "\n");
}

/*
 * logpb_dump_pages -
 *
 * return: nothing
 *
 * NOTE:
 */
static void
logpb_dump_pages (FILE * out_fp)
{
  int i;
  LOG_BUFFER *log_bufptr;

  for (i = 0; i < log_Pb.num_buffers; i++)
    {
      log_bufptr = LOGPB_FIND_BUFPTR (i);
      if (log_bufptr->pageid == NULL_PAGEID)
	{
	  /* *** ** (void)fprintf(stdout, "%3d ..\n", i); */
	  continue;
	}
      else
	{
	  fprintf (out_fp, "%3d %10lld %10d %3d %p %p-%p %4s %5lld %5d\n",
		   i, (long long) log_bufptr->pageid, log_bufptr->phy_pageid, log_bufptr->dirty,
		   (void *) log_bufptr, (void *) (log_bufptr->logpage),
		   (void *) (&log_bufptr->logpage->area[LOGAREA_SIZE - 1]), "",
		   (long long) log_bufptr->logpage->hdr.logical_pageid, log_bufptr->logpage->hdr.offset);
	}
    }
  fprintf (out_fp, "\n");
}

/*
 * logpb_initialize_backup_info - initialized backup information
 *
 * return: nothing
 *
 * NOTE:
 */
static void
logpb_initialize_backup_info (LOG_HEADER * log_hdr)
{
  int i;

  for (i = 0; i < FILEIO_BACKUP_UNDEFINED_LEVEL; i++)
    {
      log_hdr->bkinfo[i].ndirty_pages_post_bkup = 0;
      log_hdr->bkinfo[i].io_baseln_time = 0;
      log_hdr->bkinfo[i].io_numpages = 0;
      log_hdr->bkinfo[i].io_bkuptime = 0;
    }
}

/*
 * logpb_initialize_header - Initialize log header structure
 *
 * return: nothing
 *
 *   loghdr(in/out): Log header structure
 *   prefix_logname(in): Name of the log volumes. It is usually set the same as
 *                      database name. For example, if the value is equal to
 *                      "db", the names of the log volumes created are as
 *                      follow:
 *                      Active_log      = db_logactive
 *                      Archive_logs    = db_logarchive.0
 *                                        db_logarchive.1
 *                                             .
 *                                             .
 *                                             .
 *                                        db_logarchive.n
 *                      Log_information = db_loginfo
 *                      Database Backup = db_backup
 *   npages(in): Size of active log in pages
 *   db_creation(in): Database creation time.
 *
 * NOTE:Initialize a log header structure.
 */
int
logpb_initialize_header (THREAD_ENTRY * thread_p, LOG_HEADER * loghdr, const char *prefix_logname,
			 DKNPAGES npages, INT64 * db_creation)
{
  int i;

  assert (LOG_CS_OWN_WRITE_MODE (thread_p));
  assert (loghdr != NULL);

  /* to also initialize padding bytes */
  memset (loghdr, 0, sizeof (LOG_HEADER));

  strncpy (loghdr->magic, CUBRID_MAGIC_LOG_ACTIVE, CUBRID_MAGIC_MAX_LENGTH);

  if (db_creation != NULL)
    {
      loghdr->db_creation = *db_creation;
    }
  else
    {
      loghdr->db_creation = -1;
    }

  if (strlen (rel_release_string ()) >= REL_MAX_RELEASE_LENGTH)
    {
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_COMPILATION_RELEASE, 2, rel_release_string (),
	      REL_MAX_RELEASE_LENGTH);
      logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "log_init_logheader");
      return ER_LOG_COMPILATION_RELEASE;
    }

  strncpy_bufsize (loghdr->db_release, rel_release_string ());
  loghdr->db_compatibility = rel_disk_compatible ();
  loghdr->db_iopagesize = IO_PAGESIZE;
  loghdr->db_logpagesize = LOG_PAGESIZE;
  loghdr->is_shutdown = true;
  loghdr->next_trid = LOG_SYSTEM_TRANID + 1;
  loghdr->mvcc_next_id = MVCCID_FIRST;
  loghdr->avg_ntrans = LOG_ESTIMATE_NACTIVE_TRANS;
  loghdr->avg_nlocks = LOG_ESTIMATE_NOBJ_LOCKS;
  loghdr->npages = npages - 1;	/* Hdr pg is stolen */
  loghdr->db_charset = lang_charset ();
#if !defined(NDEBUG)
  loghdr->fpageid = (LOG_PAGEID) prm_get_bigint_value (PRM_ID_FIRST_LOG_PAGEID);	/* loghdr->fpageid should always be 0 except for QA or TEST purposes. */
#else
  loghdr->fpageid = 0;
#endif
  loghdr->append_lsa.pageid = loghdr->fpageid;
  loghdr->append_lsa.offset = 0;
  LSA_COPY (&loghdr->chkpt_lsa, &loghdr->append_lsa);
  loghdr->nxarv_pageid = loghdr->fpageid;
  loghdr->nxarv_phy_pageid = 1;
  loghdr->nxarv_num = 0;
  loghdr->last_arv_num_for_syscrashes = -1;
  loghdr->last_deleted_arv_num = -1;
  loghdr->has_logging_been_skipped = false;
  LSA_SET_NULL (&loghdr->bkup_level0_lsa);
  LSA_SET_NULL (&loghdr->bkup_level1_lsa);
  LSA_SET_NULL (&loghdr->bkup_level2_lsa);
  if (prefix_logname != NULL)
    {
      strcpy (loghdr->prefix_name, prefix_logname);
    }
  else
    {
      loghdr->prefix_name[0] = '\0';
    }
  loghdr->vacuum_last_blockid = 0;
  loghdr->perm_status_obsolete = 0;

  for (i = 0; i < FILEIO_BACKUP_UNDEFINED_LEVEL; i++)
    {
      loghdr->bkinfo[i].bkup_attime = 0;
    }
  logpb_initialize_backup_info (loghdr);

  loghdr->ha_server_state = HA_SERVER_STATE_IDLE;
  loghdr->ha_file_status = -1;
  LSA_SET_NULL (&loghdr->eof_lsa);
  LSA_SET_NULL (&loghdr->smallest_lsa_at_last_chkpt);

  logpb_vacuum_reset_log_header_cache (thread_p, loghdr);

  return NO_ERROR;
}

/*
 * logpb_create_header_page - Create log header page
 *
 * return: Pointer to the page or NULL
 *
 * NOTE:Create the log header page.
 */
LOG_PAGE *
logpb_create_header_page (THREAD_ENTRY * thread_p)
{
  assert (LOG_CS_OWN_WRITE_MODE (thread_p));

  return logpb_create_page (thread_p, LOGPB_HEADER_PAGE_ID);
}

/*
 * logpb_copy_log_header - Copy a log header
 *
 * return: NO_ERROR if all OK
 *
 *   to_hdr(in): New log header
 *   from_hdr(in): Source log header
 *
 * NOTE: Copy a log header.
 */
static int
logpb_copy_log_header (THREAD_ENTRY * thread_p, LOG_HEADER * to_hdr, const LOG_HEADER * from_hdr)
{
  assert (LOG_CS_OWN_WRITE_MODE (thread_p));
  assert (to_hdr != NULL);
  assert (from_hdr != NULL);

  to_hdr->was_copied = true;	// should be reset on first restart

  to_hdr->mvcc_next_id = from_hdr->mvcc_next_id;

  /* Add other attributes that need to be copied */

  return NO_ERROR;
}

/*
 * logpb_fetch_header - Fetch log header
 *
 * return: nothing
 *
 *   hdr(in/out): Pointer where log header is stored
 *
 * NOTE:Read the log header into the area pointed by hdr.
 */
void
logpb_fetch_header (THREAD_ENTRY * thread_p, LOG_HEADER * hdr)
{
  assert (hdr != NULL);
  assert (LOG_CS_OWN_WRITE_MODE (thread_p));
  assert (log_Gl.loghdr_pgptr != NULL);

  logpb_fetch_header_with_buffer (thread_p, hdr, log_Gl.loghdr_pgptr);

  /* sync append_lsa to prior_lsa */
  LOG_RESET_APPEND_LSA (&log_Gl.hdr.append_lsa);
}

/*
 * logpb_fetch_header_with_buffer - Fetch log header using given buffer
 *
 * return: nothing
 *
 *   hdr(in/out): Pointer where log header is stored
 *   log_pgptr(in/out): log page buffer ptr
 *
 * NOTE:Read the log header into the area pointed by hdr
 */
void
logpb_fetch_header_with_buffer (THREAD_ENTRY * thread_p, LOG_HEADER * hdr, LOG_PAGE * log_pgptr)
{
  LOG_HEADER *log_hdr;		/* The log header */
  LOG_LSA header_lsa;

  assert (hdr != NULL);
  assert (LOG_CS_OWN_WRITE_MODE (thread_p));
  assert (log_pgptr != NULL);

  header_lsa.pageid = LOGPB_HEADER_PAGE_ID;
  header_lsa.offset = LOG_PAGESIZE;

  if ((logpb_fetch_page (thread_p, &header_lsa, LOG_CS_SAFE_READER, log_pgptr)) != NO_ERROR)
    {
      logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "log_fetch_hdr_with_buf");
      /* This statement should not be reached */
      (void) logpb_initialize_header (thread_p, hdr, NULL, 0, NULL);
      return;
    }

  log_hdr = (LOG_HEADER *) (log_pgptr->area);
  *hdr = *log_hdr;

  assert (log_pgptr->hdr.logical_pageid == LOGPB_HEADER_PAGE_ID);
  assert (log_pgptr->hdr.offset == NULL_OFFSET);
}

/*
 * logpb_fetch_header_from_active_log - Fetch log header directly from active log file
 *
 * return: error code
 *
 *   hdr(in/out): Pointer where log header is stored
 *   log_pgptr(in/out): log page buffer ptr
 *
 * NOTE: Should be used only during boot sequence.
 */
int
logpb_fetch_header_from_active_log (THREAD_ENTRY * thread_p, const char *db_fullname, const char *logpath,
				    const char *prefix_logname, LOG_HEADER * hdr, LOG_PAGE * log_pgptr)
{
  LOG_HEADER *log_hdr;		/* The log header */
  LOG_PHY_PAGEID phy_pageid;
  int error_code = NO_ERROR;

  assert (db_fullname != NULL);
  assert (logpath != NULL);
  assert (prefix_logname != NULL);
  assert (hdr != NULL);
  assert (LOG_CS_OWN_WRITE_MODE (thread_p));
  assert (log_pgptr != NULL);

  error_code = logpb_initialize_log_names (thread_p, db_fullname, logpath, prefix_logname);
  if (error_code != NO_ERROR)
    {
      goto error;
    }

  if (fileio_is_volume_exist (log_Name_active) == false)
    {
      error_code = ER_FAILED;
      goto error;
    }

  log_Gl.append.vdes = fileio_mount (thread_p, db_fullname, log_Name_active, LOG_DBLOG_ACTIVE_VOLID, true, false);
  if (log_Gl.append.vdes == NULL_VOLDES)
    {
      error_code = ER_FAILED;
      goto error;
    }

  phy_pageid = logpb_to_physical_pageid (LOGPB_HEADER_PAGE_ID);
  logpb_log ("reading from active log:%s, physical page is : %lld\n", log_Name_active, (long long int) phy_pageid);

  if (fileio_read (thread_p, log_Gl.append.vdes, log_pgptr, phy_pageid, LOG_PAGESIZE) == NULL)
    {
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_READ, 3, LOGPB_HEADER_PAGE_ID, phy_pageid,
	      log_Name_active);
      error_code = ER_LOG_READ;
      goto error;
    }

  log_hdr = (LOG_HEADER *) (log_pgptr->area);
  *hdr = *log_hdr;

  /* keep active log mounted : this prevents other process to access/change DB parameters */

  if (log_pgptr->hdr.logical_pageid != LOGPB_HEADER_PAGE_ID || log_pgptr->hdr.offset != NULL_OFFSET)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_PAGE_CORRUPTED, 1, phy_pageid);
      error_code = ER_LOG_PAGE_CORRUPTED;
      goto error;
    }

#if !defined(NDEBUG)
  if (log_Gl.rcv_phase == LOG_RESTARTED)
    {
      logpb_debug_check_log_page (thread_p, log_pgptr);
    }
#endif

error:
  return error_code;
}

// it peeks header page of the backuped log active file
static int
logpb_peek_header_of_active_log_from_backup (THREAD_ENTRY * thread_p, const char *active_log_path, LOG_HEADER * hdr)
{
  char log_pgbuf[IO_MAX_PAGE_SIZE + MAX_ALIGNMENT], *aligned_log_pgbuf;
  LOG_HEADER *log_hdr;
  LOG_PHY_PAGEID phy_pageid;
  int error_code = NO_ERROR;
  LOG_PAGE *log_pgptr;

  assert (active_log_path != NULL);
  assert (hdr != NULL);

  aligned_log_pgbuf = PTR_ALIGN (log_pgbuf, MAX_ALIGNMENT);

  if (fileio_is_volume_exist (active_log_path) == false)
    {
      return ER_FAILED;
    }

  int log_vdes = fileio_open (active_log_path, O_RDONLY, 0);
  if (log_vdes == NULL_VOLDES)
    {
      return ER_FAILED;
    }

  phy_pageid = LOGPB_PHYSICAL_HEADER_PAGE_ID;
  logpb_log ("reading from active log:%s, physical page is : %lld\n", active_log_path, (long long int) phy_pageid);

  log_pgptr = (LOG_PAGE *) aligned_log_pgbuf;

  if (fileio_read (thread_p, log_vdes, log_pgptr, phy_pageid, LOG_PAGESIZE) == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_READ, 3, LOGPB_HEADER_PAGE_ID, phy_pageid, active_log_path);
      error_code = ER_LOG_READ;
      goto end;
    }

  log_hdr = (LOG_HEADER *) (log_pgptr->area);
  *hdr = *log_hdr;

  if (log_pgptr->hdr.logical_pageid != LOGPB_HEADER_PAGE_ID || log_pgptr->hdr.offset != NULL_OFFSET)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_PAGE_CORRUPTED, 1, phy_pageid);
      error_code = ER_LOG_PAGE_CORRUPTED;
      goto end;
    }

end:
  fileio_close (log_vdes);

  return error_code;
}

// it probes whether the next archive file exists
// if the case, the backuped log active file is regarded as unuseful and restore will reset log.
static bool
logpb_is_log_active_from_backup_useful (THREAD_ENTRY * thread_p, const char *active_log_path, const char *db_full_name)
{
  LOG_HEADER hdr;

  if (logpb_peek_header_of_active_log_from_backup (thread_p, active_log_path, &hdr) != NO_ERROR)
    {
      // something bad happened
      return false;
    }

  // make next archive name
  char next_archive_file_path[PATH_MAX], log_path[PATH_MAX];

  fileio_get_directory_path (log_path, active_log_path);
  fileio_make_log_archive_name (next_archive_file_path, log_path, fileio_get_base_file_name (db_full_name),
				hdr.nxarv_num);

  if (fileio_is_volume_exist (next_archive_file_path))
    {
      // if the next archive exists, regard the backuped log active is older and useless.
      er_log_debug (ARG_FILE_LINE, "log active from backup is older than available archive (%s).\n",
		    next_archive_file_path);
      return false;
    }

  return true;
}

/*
 * logpb_flush_header - Flush log header
 *
 * return: nothing
 *
 * NOTE:Flush out the log header from the global variable log_Gl.hdr to disk. Note append pages are not flushed.
 */
void
logpb_flush_header (THREAD_ENTRY * thread_p)
{
  LOG_HEADER *log_hdr;
#if defined(CUBRID_DEBUG)
  struct timeval start_time = { 0, 0 };
  struct timeval end_time = { 0, 0 };

  css_gettimeofday (&start_time, NULL);
#endif /* CUBRID_DEBUG */

  assert (LOG_CS_OWN_WRITE_MODE (thread_p));

  if (log_Gl.loghdr_pgptr == NULL)
    {
      assert (log_Gl.loghdr_pgptr != NULL);

      /* This is just a safe guard. log_initialize frees log_Gl.loghdr_pgptr when it fails. It can only happen when
       * deletedb or emergency utilities fail to initialize log. */
      log_Gl.loghdr_pgptr = (LOG_PAGE *) malloc (LOG_PAGESIZE);
      if (log_Gl.loghdr_pgptr == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) LOG_PAGESIZE);
	  logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "logpb_flush_header");
	  return;
	}
      memset (log_Gl.loghdr_pgptr, LOG_PAGE_INIT_VALUE, LOG_PAGESIZE);
    }

  log_hdr = (LOG_HEADER *) (log_Gl.loghdr_pgptr->area);
  *log_hdr = log_Gl.hdr;

  log_Gl.loghdr_pgptr->hdr.logical_pageid = LOGPB_HEADER_PAGE_ID;
  log_Gl.loghdr_pgptr->hdr.offset = NULL_OFFSET;
  log_Gl.loghdr_pgptr->hdr.flags = 0;	/* Now flags in header page has always 0 value */

  logpb_write_page_to_disk (thread_p, log_Gl.loghdr_pgptr, LOGPB_HEADER_PAGE_ID);

  log_Stat.flush_hdr_call_count++;
#if defined(CUBRID_DEBUG)
  gettimeofday (&end_time, NULL);
  log_Stat.last_flush_hdr_sec_by_LFT = LOG_GET_ELAPSED_TIME (end_time, start_time);
  log_Stat.total_flush_hdr_sec_by_LFT += log_Stat.last_flush_hdr_sec_by_LFT;
#endif /* CUBRID_DEBUG */

#if defined(CUBRID_DEBUG)
  er_log_debug (ARG_FILE_LINE, "log_flush_hdr: call count(%ld) avg flush (%f) \n", log_Stat.flush_hdr_call_count,
		(double) log_Stat.total_flush_hdr_sec_by_LFT / log_Stat.flush_hdr_call_count);
#endif /* CUBRID_DEBUG */
}

/*
 * logpb_fetch_page - Fetch a exist_log page using local buffer
 *
 * return: NO_ERROR if everything is ok, else ER_FAILED
 *
 *   pageid(in): Page identifier
 *   log_pgptr(in/out): Page buffer to copy
 *
 * NOTE:Fetch the log page identified by pageid into a log buffer and return such buffer.
 *              If there is the page in the log page buffer, copy it to buffer and return it.
 *              If not, read log page from log.
 */
int
logpb_fetch_page (THREAD_ENTRY * thread_p, const LOG_LSA * req_lsa, LOG_CS_ACCESS_MODE access_mode,
		  LOG_PAGE * log_pgptr)
{
  LOG_LSA append_lsa, append_prev_lsa;
  int rv;

  assert (log_pgptr != NULL);
  assert (req_lsa != NULL);
  assert (req_lsa->pageid != NULL_PAGEID);

  logpb_log ("called logpb_fetch_page with pageid = %lld\n", (long long int) req_lsa->pageid);

  LSA_COPY (&append_lsa, &log_Gl.hdr.append_lsa);
  LSA_COPY (&append_prev_lsa, &log_Gl.append.prev_lsa);

  /*
   * This If block ensure belows,
   *  case 1. log page (of pageid) is in log page buffer (not prior_lsa list)
   *  case 2. EOL record which is written temporarily by
   *          logpb_flush_all_append_pages is cleared so there is no EOL
   *          in log page
   */

  if (LSA_LE (&append_lsa, req_lsa)	/* for case 1 */
      || LSA_LE (&append_prev_lsa, req_lsa))	/* for case 2 */
    {
      LOG_CS_ENTER (thread_p);

      assert (LSA_LE (&log_Gl.append.prev_lsa, &log_Gl.hdr.append_lsa));

      /*
       * copy prior lsa list to log page buffer to ensure that required
       * pageid is in log page buffer
       */
      if (LSA_LE (&log_Gl.hdr.append_lsa, req_lsa))	/* retry with mutex */
	{
	  logpb_prior_lsa_append_all_list (thread_p);
	}

      LOG_CS_EXIT (thread_p);
    }

  /*
   * most of the cases, we don't need calling logpb_copy_page with LOG_CS exclusive access,
   * if needed, we acquire READ mode in logpb_copy_page
   */
  rv = logpb_copy_page (thread_p, req_lsa->pageid, access_mode, log_pgptr);
  if (rv != NO_ERROR)
    {
      ASSERT_ERROR ();
      return ER_FAILED;
    }

  return NO_ERROR;
}

/*
 * logpb_copy_page_from_log_buffer -
 *
 * return: NO_ERROR if everything is ok
 *  pageid(in): Page identifier
 *  log_pgptr(in/out): Page buffer
 */
int
logpb_copy_page_from_log_buffer (THREAD_ENTRY * thread_p, LOG_PAGEID pageid, LOG_PAGE * log_pgptr)
{
  int rv;

  assert (log_pgptr != NULL);
  assert (pageid != NULL_PAGEID);
  assert (pageid <= log_Gl.hdr.append_lsa.pageid);

  logpb_log ("called logpb_copy_page_from_log_buffer with pageid = %lld\n", (long long int) pageid);

  rv = logpb_copy_page (thread_p, pageid, LOG_CS_FORCE_USE, log_pgptr);
  if (rv != NO_ERROR)
    {
      ASSERT_ERROR ();
      return ER_FAILED;
    }

  return NO_ERROR;
}

/*
 * logpb_copy_page_from_file -
 *  pageid(in): Page identifier
 *  log_pgptr(in/out): Page buffer
 * return: NO_ERROR if everything is ok
 *
 */
int
logpb_copy_page_from_file (THREAD_ENTRY * thread_p, LOG_PAGEID pageid, LOG_PAGE * log_pgptr)
{
  int rv;

  assert (log_pgptr != NULL);
  assert (pageid != NULL_PAGEID);
  assert (pageid <= log_Gl.hdr.append_lsa.pageid);

  logpb_log ("called logpb_copy_page_from_file with pageid = %lld\n", (long long int) pageid);

  LOG_CS_ENTER_READ_MODE (thread_p);
  if (log_pgptr != NULL)
    {
      rv = logpb_read_page_from_file (thread_p, pageid, LOG_CS_FORCE_USE, log_pgptr);
      if (rv != NO_ERROR)
	{
	  LOG_CS_EXIT (thread_p);
	  return ER_FAILED;
	}
    }
  LOG_CS_EXIT (thread_p);

  return NO_ERROR;
}

/*
 * logpb_copy_page - copy a exist_log page using local buffer
 *
 * return: NO_ERROR if everything is ok
 *
 *   pageid(in): Page identifier
 *   access_mode(in): access mode (reader, safe reader, writer)
 *   log_pgptr(in/out): Page buffer to copy
 *
 * NOTE:Fetch the log page identified by pageid into a log buffer and return such buffer.
 *              If there is the page in the log page buffer, copy it to buffer and return it.
 *              If not, read log page from log.
 */

static int
logpb_copy_page (THREAD_ENTRY * thread_p, LOG_PAGEID pageid, LOG_CS_ACCESS_MODE access_mode, LOG_PAGE * log_pgptr)
{
  LOG_BUFFER *log_bufptr = NULL;
  bool is_perf_tracking;
  TSC_TICKS start_tick, end_tick;
  TSCTIMEVAL tv_diff;
  UINT64 fix_wait_time;
  PERF_PAGE_MODE stat_page_found = PERF_PAGE_MODE_OLD_IN_BUFFER;
  bool log_csect_entered = false;
  int rv = NO_ERROR, index;

  assert (log_pgptr != NULL);
  assert (pageid != NULL_PAGEID);

  logpb_log ("called logpb_copy_page with pageid = %lld\n", (long long int) pageid);

  is_perf_tracking = perfmon_is_perf_tracking ();
  if (is_perf_tracking)
    {
      tsc_getticks (&start_tick);
    }

  if (access_mode != LOG_CS_SAFE_READER)
    {
      LOG_CS_ENTER_READ_MODE (thread_p);
      log_csect_entered = true;
    }

  if (pageid == LOGPB_HEADER_PAGE_ID)
    {
      /* copy header page into log_pgptr */
      log_bufptr = &log_Pb.header_buffer;

      if (log_bufptr->pageid == NULL_PAGEID)
	{
	  rv = logpb_read_page_from_file (thread_p, pageid, access_mode, log_pgptr);
	  if (rv != NO_ERROR)
	    {
	      rv = ER_FAILED;
	      goto exit;
	    }
	  stat_page_found = PERF_PAGE_MODE_OLD_LOCK_WAIT;
	}
      else
	{
	  memcpy (log_pgptr, log_bufptr->logpage, LOG_PAGESIZE);
	}

      goto exit;
    }

  index = logpb_get_log_buffer_index (pageid);
  if (index >= 0 && index < log_Pb.num_buffers)
    {
      log_bufptr = &log_Pb.buffers[index];
    }
  else
    {
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_PAGE_CORRUPTED, 1, pageid);
      rv = ER_LOG_PAGE_CORRUPTED;
      goto exit;
    }

  if (log_bufptr->pageid == pageid)
    {
      /* Copy page from log_bufptr into log_pgptr */
      memcpy (log_pgptr, log_bufptr->logpage, LOG_PAGESIZE);
      if (log_bufptr->pageid == pageid)
	{
	  goto exit;
	}
    }

  /* Could not get from log page buffer cache */
  rv = logpb_read_page_from_file (thread_p, pageid, access_mode, log_pgptr);
  if (rv != NO_ERROR)
    {
      rv = ER_FAILED;
      goto exit;
    }
  stat_page_found = PERF_PAGE_MODE_OLD_LOCK_WAIT;

  /* Always exit through here */
exit:
  if (log_csect_entered)
    {
      LOG_CS_EXIT (thread_p);
    }
  perfmon_inc_stat (thread_p, PSTAT_LOG_NUM_FETCHES);

  if (is_perf_tracking)
    {
      tsc_getticks (&end_tick);
      tsc_elapsed_time_usec (&tv_diff, end_tick, start_tick);
      fix_wait_time = tv_diff.tv_sec * 1000000LL + tv_diff.tv_usec;
      /* log page fix time : use dummy values for latch type and conditional type; use PERF_PAGE_MODE_OLD_LOCK_WAIT and
       * PERF_PAGE_MODE_OLD_IN_BUFFER for page type : page is not found in log page buffer and must be read from
       * archive vs page is found in log page buffer */
      if (fix_wait_time > 0)
	{
	  perfmon_pbx_fix_acquire_time (thread_p, PAGE_LOG, stat_page_found, PERF_HOLDER_LATCH_READ,
					PERF_UNCONDITIONAL_FIX_WITH_WAIT, fix_wait_time);
	}
    }

  return rv;
}

/*
 * logpb_read_page_from_file - Fetch a exist_log page from log files
 *
 * return: NO_ERROR if everything is ok, else ER_GENERIC_ERROR
 *
 *   pageid(in): Page identifier
 *   log_pgptr(in/out): Page buffer to read
 *
 * NOTE:read the log page identified by pageid into a buffer from from archive or active log.
 */
int
logpb_read_page_from_file (THREAD_ENTRY * thread_p, LOG_PAGEID pageid, LOG_CS_ACCESS_MODE access_mode,
			   LOG_PAGE * log_pgptr)
{
  bool log_csect_entered = false;

  assert (log_pgptr != NULL);
  assert (pageid != NULL_PAGEID);

  logpb_log ("called logpb_read_page_from_file with pageid = %lld, hdr.logical_pageid = %lld, "
	     "LOGPB_ACTIVE_NPAGES = %d\n", (long long int) pageid, (long long int) log_pgptr->hdr.logical_pageid,
	     LOGPB_ACTIVE_NPAGES);

  if (access_mode == LOG_CS_SAFE_READER)
    {
      /* This is added here to block others from creating new archive or mounting/dismounting archives while the vacuum
       * workers is trying to fetch its page from file. */
      /* This was first done only for active pages. This allowed other transaction create a new archive from active log
       * after calling logpb_is_page_in_archive. Therefore the logpb_to_physical_pageid became flawed. */
      LOG_CS_ENTER_READ_MODE (thread_p);
      log_csect_entered = true;
    }
  else
    {
      assert (LOG_CS_OWN (thread_p));
    }

  // some archived pages may be still in active log; check if they can be fetched from active.
  bool fetch_from_archive = logpb_is_page_in_archive (pageid);
  if (fetch_from_archive)
    {
      bool is_archive_page_in_active_log = (pageid + LOGPB_ACTIVE_NPAGES) > log_Gl.hdr.append_lsa.pageid;
      bool dont_fetch_archive_from_active = !LOG_ISRESTARTED () || log_Gl.hdr.was_active_log_reset;

      if (is_archive_page_in_active_log && !dont_fetch_archive_from_active)
	{
	  // can fetch from active
	  fetch_from_archive = false;
	}
    }
  if (fetch_from_archive)
    {
      // fetch from archive
      if (logpb_fetch_from_archive (thread_p, pageid, log_pgptr, NULL, NULL, true) == NULL)
	{
#if defined (SERVER_MODE)
	  if (thread_p != NULL && thread_p->type == TT_VACUUM_MASTER)
	    {
	      vacuum_er_log_error (VACUUM_ER_LOG_MASTER | VACUUM_ER_LOG_ARCHIVES,
				   "Failed to fetch page %lld from archives.", pageid);
	    }
#endif /* SERVER_MODE */
	  goto error;
	}
    }
  else
    {
      // fetch from active
      LOG_PHY_PAGEID phy_pageid;

      /*
       * Page is contained in the active log.
       * Find the corresponding physical page and read the page form disk.
       */
      phy_pageid = logpb_to_physical_pageid (pageid);
      logpb_log ("phy_pageid in logpb_read_page_from_file is %lld\n", (long long int) phy_pageid);

      perfmon_inc_stat (thread_p, PSTAT_LOG_NUM_IOREADS);

      if (fileio_read (thread_p, log_Gl.append.vdes, log_pgptr, phy_pageid, LOG_PAGESIZE) == NULL)
	{
	  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_READ, 3, pageid, phy_pageid, log_Name_active);
	  goto error;
	}
      else
	{
	  if (log_pgptr->hdr.logical_pageid != pageid)
	    {
	      if (log_pgptr->hdr.logical_pageid == pageid + LOGPB_ACTIVE_NPAGES)
		{
		  /* The active part where this archive page belonged was already, overwritten. Fetch the page from
		   * archive. */
		  if (logpb_fetch_from_archive (thread_p, pageid, log_pgptr, NULL, NULL, true) == NULL)
		    {
#if defined (SERVER_MODE)
		      if (thread_p != NULL && thread_p->type == TT_VACUUM_MASTER)
			{
			  vacuum_er_log_error (VACUUM_ER_LOG_MASTER | VACUUM_ER_LOG_ARCHIVES,
					       "Failed to fetch page %lld from archives.", pageid);
			}
#endif /* SERVER_MODE */
		      goto error;
		    }
		}
	      else
		{
		  /* Clean the buffer... since it may be corrupted */
		  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_PAGE_CORRUPTED, 1, pageid);
		  goto error;
		}
	    }
	  else
	    {
	      /* 
	       * fetched from active.
	       * In case of being fetched from archive log, no need to be decrypted 
	       * because it already decrypted in logpb_fetch_from_archive()
	       */
	      TDE_ALGORITHM tde_algo = logpb_get_tde_algorithm ((LOG_PAGE *) log_pgptr);
	      if (tde_algo != TDE_ALGORITHM_NONE)
		{
		  if (tde_decrypt_log_page ((LOG_PAGE *) log_pgptr, tde_algo, (LOG_PAGE *) log_pgptr) != NO_ERROR)
		    {
		      ASSERT_ERROR ();
		      goto error;
		    }
		}
	    }
	}
    }

  if (log_csect_entered)
    {
      LOG_CS_EXIT (thread_p);
    }

  logpb_log ("logpb_read_page_from_file: log page %lld has checksum = %d\n",
	     (long long int) log_pgptr->hdr.logical_pageid, log_pgptr->hdr.checksum);

#if !defined(NDEBUG)
  if (log_Gl.rcv_phase == LOG_RESTARTED)
    {
      logpb_debug_check_log_page (thread_p, log_pgptr);
    }
#endif

  /* keep old function's usage */
  return NO_ERROR;

error:
  if (log_csect_entered)
    {
      LOG_CS_EXIT (thread_p);
    }
  return ER_FAILED;
}

/*
 * logpb_read_page_from_active_log -
 *
 *   return: new num_pages
 *
 *   pageid(in):
 *   num_pages(in):
 *   log_pgptr(in/out):
 */
int
logpb_read_page_from_active_log (THREAD_ENTRY * thread_p, LOG_PAGEID pageid, int num_pages, bool decrypt_needed,
				 LOG_PAGE * log_pgptr)
{
  LOG_PHY_PAGEID phy_start_pageid;

  assert (log_pgptr != NULL);
  assert (pageid != NULL_PAGEID);
  assert (num_pages > 0);

  logpb_log ("called logpb_read_page_from_active_log with pageid = %lld and num_pages = %d\n", (long long int) pageid,
	     num_pages);

  /*
   * Page is contained in the active log.
   * Find the corresponding physical page and read the page from disk.
   */
  phy_start_pageid = logpb_to_physical_pageid (pageid);
  num_pages = MIN (num_pages, LOGPB_ACTIVE_NPAGES - phy_start_pageid + 1);

  perfmon_inc_stat (thread_p, PSTAT_LOG_NUM_IOREADS);

  if (fileio_read_pages (thread_p, log_Gl.append.vdes, (char *) log_pgptr, phy_start_pageid, num_pages, LOG_PAGESIZE) ==
      NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_READ, 3, pageid, phy_start_pageid, log_Name_active);
      return -1;
    }
  else
    {
      if (log_pgptr->hdr.logical_pageid != pageid)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_PAGE_CORRUPTED, 1, pageid);
	  return -1;
	}
    }

  if (decrypt_needed)
    {
      char *ptr = (char *) log_pgptr;
      int i;
      for (i = 0; i < num_pages; i++)
	{
	  TDE_ALGORITHM tde_algo = logpb_get_tde_algorithm ((LOG_PAGE *) ptr);
	  if (tde_algo != TDE_ALGORITHM_NONE)
	    {
	      if (tde_decrypt_log_page ((LOG_PAGE *) ptr, tde_algo, (LOG_PAGE *) ptr) != NO_ERROR)
		{
		  ASSERT_ERROR ();
		  return -1;
		}
	    }
	  ptr += LOG_PAGESIZE;
	}
    }

#if !defined(NDEBUG)
  if (log_Gl.rcv_phase == LOG_RESTARTED)
    {
      char *ptr = NULL;
      int i;
      char log_pgbuf[IO_MAX_PAGE_SIZE + MAX_ALIGNMENT];
      char *aligned_log_pgbuf = PTR_ALIGN (log_pgbuf, MAX_ALIGNMENT);

      ptr = (char *) log_pgptr;
      for (i = 0; i < num_pages; i++)
	{
	  TDE_ALGORITHM tde_algo = logpb_get_tde_algorithm ((LOG_PAGE *) ptr);
	  /* checksum is calculated before tde-encryption */
	  if (!decrypt_needed && tde_algo != TDE_ALGORITHM_NONE)
	    {
	      /* This page is tde-ecnrypted page and has not yet decrypted.
	       * To check consistency, we need to decrypt it */
	      if (!tde_is_loaded ())
		{
		  ptr += LOG_PAGESIZE;
		  continue;	/* no way to check an encrypted page without tde module */
		}

	      if (tde_decrypt_log_page ((LOG_PAGE *) ptr, tde_algo, (LOG_PAGE *) aligned_log_pgbuf) != NO_ERROR)
		{
		  ASSERT_ERROR ();
		  assert (false);
		}
	      logpb_debug_check_log_page (thread_p, (LOG_PAGE *) aligned_log_pgbuf);
	      ptr += LOG_PAGESIZE;
	    }
	  else
	    {
	      logpb_debug_check_log_page (thread_p, (LOG_PAGE *) ptr);
	      ptr += LOG_PAGESIZE;
	    }
	}
    }
#endif

  return num_pages;
}

/*
 * logpb_write_page_to_disk - writes and syncs a log page to disk
 *
 * return: error code
 *
 *   log_pgptr(in/out): Log page pointer
 *   logical_pageid(in): logical page id
 *
 * NOTE:writes and syncs a log page to disk
 */
int
logpb_write_page_to_disk (THREAD_ENTRY * thread_p, LOG_PAGE * log_pgptr, LOG_PAGEID logical_pageid)
{
  int nbytes, error_code;
  LOG_PHY_PAGEID phy_pageid;
  FILEIO_WRITE_MODE write_mode;
  char enc_pgbuf[IO_MAX_PAGE_SIZE + MAX_ALIGNMENT];
  LOG_PAGE *enc_pgptr = NULL;

  enc_pgptr = (LOG_PAGE *) PTR_ALIGN (enc_pgbuf, MAX_ALIGNMENT);

  assert (log_pgptr != NULL);
  assert (log_pgptr->hdr.logical_pageid == logical_pageid);
  /* we allow writing page as long as they do not belong to archive area */
  assert (logical_pageid == LOGPB_HEADER_PAGE_ID
	  || (!LOGPB_IS_ARCHIVE_PAGE (logical_pageid) && logical_pageid <= LOGPB_LAST_ACTIVE_PAGE_ID));

  logpb_log ("called logpb_write_page_to_disk for logical_pageid = %lld\n", (long long int) logical_pageid);

  /* Set page CRC before writing to disk. */
  error_code = logpb_set_page_checksum (thread_p, log_pgptr);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  phy_pageid = logpb_to_physical_pageid (logical_pageid);
  logpb_log ("phy_pageid in logpb_write_page_to_disk is %lld\n", (long long int) phy_pageid);

  /* log_Gl.append.vdes is only changed while starting or finishing or recovering server. So, log cs is not needed. */

  write_mode = dwb_is_created () == true ? FILEIO_WRITE_NO_COMPENSATE_WRITE : FILEIO_WRITE_DEFAULT_WRITE;

  logpb_log ("logpb_write_page_to_disk: The page (%lld) is being tde-encrypted: %d\n", (long long int) logical_pageid,
	     LOG_IS_PAGE_TDE_ENCRYPTED (log_pgptr));

  if (LOG_IS_PAGE_TDE_ENCRYPTED (log_pgptr))
    {
      error_code = tde_encrypt_log_page (log_pgptr, logpb_get_tde_algorithm (log_pgptr), enc_pgptr);
      if (error_code != NO_ERROR)
	{
	  /* 
	   * if encrpytion fails, it just skip it and off the tde flag. The page will never be encrypted in this case.
	   * It menas once it fails, the page always spill user data un-encrypted from then.
	   */
	  logpb_set_tde_algorithm (thread_p, log_pgptr, TDE_ALGORITHM_NONE);
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TDE_ENCRYPTION_LOGPAGE_ERORR_AND_OFF_TDE, 1, logical_pageid);
	}
      else
	{
	  log_pgptr = enc_pgptr;
	}
    }

  if (fileio_write (thread_p, log_Gl.append.vdes, log_pgptr, phy_pageid, LOG_PAGESIZE, write_mode) == NULL)
    {
      if (er_errid () == ER_IO_WRITE_OUT_OF_SPACE)
	{
	  nbytes = log_Gl.hdr.db_logpagesize;
	  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_WRITE_OUT_OF_SPACE, 4, logical_pageid, phy_pageid,
		  log_Name_active, nbytes);
	}
      else
	{
	  er_set_with_oserror (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_WRITE, 3, logical_pageid, phy_pageid,
			       log_Name_active);
	}

      logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "logpb_write_page_to_disk");
      return ER_FAILED;
    }

  perfmon_inc_stat (thread_p, PSTAT_LOG_NUM_IOWRITES);
  return NO_ERROR;
}

/*
 * logpb_find_header_parameters - Find some database creation parameters
 *
 * return: iopagesize or -1
 *
 *   db_fullname(in): Full name of the database
 *   force_read_log_header(in): force to read log header
 *   logpath(in): Directory where the log volumes reside
 *   prefix_logname(in): Name of the log volumes. It is usually set as database
 *                      name. For example, if the value is equal to "db", the
 *                      names of the log volumes created are as follow:
 *                      Active_log      = db_logactive
 *                      Archive_logs    = db_logarchive.0
 *                                        db_logarchive.1
 *                                             .
 *                                             .
 *                                             .
 *                                        db_logarchive.n
 *                      Log_information = db_loginfo
 *                      Database Backup = db_backup
 *   db_iopagesize(in): Set as a side effect to iopagesize
 *   db_creation(in): Set as a side effect to time of database creation
 *   db_compatibility(in): Set as a side effect to database disk compatibility
 *   db_charset(in): Set as a side effect to database charset
 *
 * NOTE:Find some database creation parameters such as pagesize, creation time, and disk compatability.
 */
PGLENGTH
logpb_find_header_parameters (THREAD_ENTRY * thread_p, const bool force_read_log_header, const char *db_fullname,
			      const char *logpath, const char *prefix_logname, PGLENGTH * io_page_size,
			      PGLENGTH * log_page_size, INT64 * creation_time, float *db_compatibility, int *db_charset)
{
  static LOG_HEADER hdr;	/* Log header */
  static bool is_header_read_from_file = false;
  static bool is_log_header_validated = false;
  char log_pgbuf[IO_MAX_PAGE_SIZE + MAX_ALIGNMENT], *aligned_log_pgbuf;
  LOG_PAGE *log_pgptr = NULL;
  int error_code = NO_ERROR;

  if (force_read_log_header)
    {
      is_header_read_from_file = false;
      is_log_header_validated = false;
    }

  aligned_log_pgbuf = PTR_ALIGN (log_pgbuf, MAX_ALIGNMENT);

  assert (LOG_CS_OWN_WRITE_MODE (thread_p));

  /* Is the system restarted ? */
  if (log_Gl.trantable.area != NULL && log_Gl.append.log_pgptr != NULL)
    {
      *io_page_size = log_Gl.hdr.db_iopagesize;
      *log_page_size = log_Gl.hdr.db_logpagesize;
      *creation_time = log_Gl.hdr.db_creation;
      *db_compatibility = log_Gl.hdr.db_compatibility;

      if (IO_PAGESIZE != *io_page_size || LOG_PAGESIZE != *log_page_size)
	{
	  if (db_set_page_size (*io_page_size, *log_page_size) != NO_ERROR)
	    {
	      goto error;
	    }

	  logpb_finalize_pool (thread_p);
	  error_code = logtb_define_trantable_log_latch (thread_p, log_Gl.trantable.num_total_indices);
	  if (error_code != NO_ERROR)
	    {
	      goto error;
	    }
	  error_code = logpb_initialize_pool (thread_p);
	  if (error_code != NO_ERROR)
	    {
	      goto error;
	    }
	  if (logpb_fetch_start_append_page (thread_p) != NO_ERROR)
	    {
	      goto error;
	    }
	}
      return *io_page_size;
    }

  if (!is_header_read_from_file)
    {
      log_pgptr = (LOG_PAGE *) aligned_log_pgbuf;

      error_code = logpb_fetch_header_from_active_log (thread_p, db_fullname, logpath, prefix_logname, &hdr, log_pgptr);
      if (error_code != NO_ERROR)
	{
	  goto error;
	}
      is_header_read_from_file = true;
    }

  *io_page_size = hdr.db_iopagesize;
  *log_page_size = hdr.db_logpagesize;
  *creation_time = hdr.db_creation;
  *db_compatibility = hdr.db_compatibility;
  *db_charset = (int) hdr.db_charset;

  if (is_log_header_validated)
    {
      return *io_page_size;
    }

  /*
   * Make sure that the log is a log file and that it is compatible with the
   * running database and system
   */

  if (strcmp (hdr.prefix_name, prefix_logname) != 0)
    {
      /*
       * This does not look like the log or the log was renamed. Incompatible
       * prefix name with the prefix stored on disk
       */
      er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, ER_LOG_INCOMPATIBLE_PREFIX_NAME, 2, log_Name_active,
	      hdr.prefix_name);
      /* Continue anyhow.. */
    }

  /* only check for incompatibility here, this will be done again in log_xinit which will run the compatibility
   * functions if there are any. */
  /* We added disk compatibility rules to R2.2. Before that release, rel_get_disk_compatible function returned only
   * REL_FULLY_COMPATIBLE or REL_NOT_COMPATIBLE. However, it might return REL_BACKWARD_COMPATIBLE now. */
  if (rel_get_disk_compatible (*db_compatibility, NULL) != REL_FULLY_COMPATIBLE)
    {
      log_Gl.hdr.db_compatibility = *db_compatibility;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_INCOMPATIBLE_DATABASE, 2, rel_name (), rel_release_string ());
      error_code = ER_LOG_INCOMPATIBLE_DATABASE;
      goto error;
    }

  is_log_header_validated = true;

  return *io_page_size;

error:
  *io_page_size = -1;
  *log_page_size = -1;
  *creation_time = 0;
  *db_compatibility = -1.0;

  return *io_page_size;
}

/*
 *
 *       	       FUNCTIONS RELATED TO APPEND PAGES
 *
 */

/*
 * logpb_fetch_start_append_page - FETCH THE START APPEND PAGE
 *
 * return: Pointer to the page or NULL
 *
 * NOTE:Fetch the start append page.
 */
int
logpb_fetch_start_append_page (THREAD_ENTRY * thread_p)
{
  LOG_FLUSH_INFO *flush_info = &log_Gl.flush_info;
  PAGE_FETCH_MODE flag = OLD_PAGE;
  bool need_flush;
#if defined(SERVER_MODE)
  int rv;
#endif /* SERVER_MODE */

  assert (LOG_CS_OWN_WRITE_MODE (thread_p));

  logpb_log ("started logpb_fetch_start_append_page\n");

  /* detect empty log (page and offset of zero) */
#if !defined(NDEBUG)
  if ((log_Gl.hdr.append_lsa.pageid == (LOG_PAGEID) prm_get_bigint_value (PRM_ID_FIRST_LOG_PAGEID))
      && (log_Gl.hdr.append_lsa.offset == 0))
#else
  if ((log_Gl.hdr.append_lsa.pageid == 0) && (log_Gl.hdr.append_lsa.offset == 0))
#endif
    {
      flag = NEW_PAGE;
    }

  if (log_Gl.append.log_pgptr != NULL)
    {
      /*
       * Somehow we already have an append page, flush all current append page
       * and start form scratch
       */
      logpb_invalid_all_append_pages (thread_p);
    }

  /*
   * Fetch the start append page
   */

  log_Gl.append.log_pgptr = logpb_locate_page (thread_p, log_Gl.hdr.append_lsa.pageid, flag);
  if (log_Gl.append.log_pgptr == NULL)
    {
      return ER_FAILED;
    }

  log_Gl.append.set_nxio_lsa (log_Gl.hdr.append_lsa);
  /*
   * Save this log append page as an active page to be flushed at a later
   * time if the page is modified (dirty).
   * We must save the log append pages in the order that they are defined
   * and need to be flushed.
   */

  need_flush = false;

  rv = pthread_mutex_lock (&flush_info->flush_mutex);

  flush_info->toflush[flush_info->num_toflush] = log_Gl.append.log_pgptr;
  flush_info->num_toflush++;

  if (flush_info->num_toflush >= flush_info->max_toflush)
    {
      /*
       * Force the dirty pages including the current one at this moment
       */
      need_flush = true;
    }

  pthread_mutex_unlock (&flush_info->flush_mutex);

  if (need_flush)
    {
      logpb_flush_pages_direct (thread_p);
    }

  return NO_ERROR;
}

/*
 * logpb_fetch_start_append_page_new - FETCH THE NEW START APPEND PAGE
 *
 * return: Pointer to the page or NULL
 */
LOG_PAGE *
logpb_fetch_start_append_page_new (THREAD_ENTRY * thread_p)
{
  assert (LOG_CS_OWN_WRITE_MODE (thread_p));

  logpb_log ("started logpb_fetch_start_append_page_new\n");

  log_Gl.append.log_pgptr = logpb_locate_page (thread_p, log_Gl.hdr.append_lsa.pageid, NEW_PAGE);
  if (log_Gl.append.log_pgptr == NULL)
    {
      return NULL;
    }

  log_Gl.append.set_nxio_lsa (log_Gl.hdr.append_lsa);

  return log_Gl.append.log_pgptr;
}

/*
 * logpb_next_append_page - Fetch next append page
 *
 * return: nothing
 *
 *   current_setdirty(in): Set the current append page dirty ?
 *
 * NOTE:Fetch the next append page.
 *              If the current append page contains the beginning of the log
 *              record being appended (i.e., log record did fit on current
 *              append page), the freeing of this page is delayed until the
 *              record is completely appended/logged. This is needed since
 *              every log record has a forward pointer to next log record
 *              (i.e., next append address). In addition, we must avoid
 *              flushing this page to disk (e.g., page replacement),
 *              otherwise, during crash recovery we could try to read a log
 *              record that has never been finished and the end of the log may
 *              not be detected. That is, the log would be corrupted.
 *
 *              If the current append page does not contain the beginning of
 *              the log record, the page can be freed and flushed at any time.
 *
 *              If the next page to archive is located at the physical
 *              location of the desired append page, a set of log pages is
 *              archived, so we can continue the append operations.
 */
static void
logpb_next_append_page (THREAD_ENTRY * thread_p, LOG_SETDIRTY current_setdirty)
{
  LOG_FLUSH_INFO *flush_info = &log_Gl.flush_info;
  bool need_flush;
#if defined(SERVER_MODE)
  int rv;
#endif /* SERVER_MODE */
#if defined(CUBRID_DEBUG)
  long commit_count = 0;
  static struct timeval start_append_time = { 0, 0 };
  struct timeval end_append_time = { 0, 0 };
  static long prev_commit_count_in_append = 0;
  double elapsed = 0;

  gettimeofday (&end_append_time, NULL);
#endif /* CUBRID_DEBUG */

  assert (LOG_CS_OWN_WRITE_MODE (thread_p));

  logpb_log ("started logpb_next_append_page\n");

  if (current_setdirty == LOG_SET_DIRTY)
    {
      logpb_set_dirty (thread_p, log_Gl.append.log_pgptr);
    }

  log_Gl.append.log_pgptr = NULL;

  log_Gl.hdr.append_lsa.pageid++;
  log_Gl.hdr.append_lsa.offset = 0;

  /*
   * Is the next logical page to archive, currently located at the physical
   * location of the next logical append page ? (Remember the log is a RING).
   * If so, we need to archive the log from the next logical page to archive
   * up to the closest page that does not hold the current append log record.
   */

  if (LOGPB_AT_NEXT_ARCHIVE_PAGE_ID (log_Gl.hdr.append_lsa.pageid))
    {
      /* The log must be archived */
      logpb_archive_active_log (thread_p);
    }

  /*
   * Has the log been cycled ?
   */
  if (LOGPB_IS_FIRST_PHYSICAL_PAGE (log_Gl.hdr.append_lsa.pageid))
    {
      log_Gl.hdr.fpageid += LOGPB_ACTIVE_NPAGES;

      /* Flush the header to save updates by archiving. */
      logpb_flush_header (thread_p);
    }

  /*
   * Fetch the next page as a newly defined append page. Append pages are
   * always new pages
   */

  log_Gl.append.log_pgptr = logpb_create_page (thread_p, log_Gl.hdr.append_lsa.pageid);
  if (log_Gl.append.log_pgptr == NULL)
    {
      logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "log_next_append_page");
      /* This statement should not be reached */
      return;
    }

  if (log_Gl.append.appending_page_tde_encrypted)
    {
      TDE_ALGORITHM tde_algo = (TDE_ALGORITHM) prm_get_integer_value (PRM_ID_TDE_DEFAULT_ALGORITHM);
      logpb_set_tde_algorithm (thread_p, log_Gl.append.log_pgptr, tde_algo);
      logpb_set_dirty (thread_p, log_Gl.append.log_pgptr);
    }

  logpb_log ("logpb_next_append_page: append the new page (%lld), tde_algorithm = %s\n",
	     (long long int) log_Gl.append.log_pgptr->hdr.logical_pageid,
	     tde_get_algorithm_name (logpb_get_tde_algorithm (log_Gl.append.log_pgptr)));

#if defined(CUBRID_DEBUG)
  {
    log_Stat.last_append_pageid = log_Gl.hdr.append_lsa.pageid;
  }
#endif /* CUBRID_DEBUG */

  /*
   * Save this log append page as an active page to be flushed at a later
   * time if the page is modified (dirty).
   * We must save the log append pages in the order that they are defined
   * and need to be flushed.
   */

  rv = pthread_mutex_lock (&flush_info->flush_mutex);

  flush_info->toflush[flush_info->num_toflush] = log_Gl.append.log_pgptr;
  flush_info->num_toflush++;

  need_flush = false;
  if (flush_info->num_toflush >= flush_info->max_toflush)
    {
      need_flush = true;
    }

  pthread_mutex_unlock (&flush_info->flush_mutex);

  if (need_flush)
    {
      logpb_flush_all_append_pages (thread_p);
    }

#if defined(CUBRID_DEBUG)
  if (start_append_time.tv_sec != 0 && start_append_time.tv_usec != 0)
    {
      elapsed = LOG_GET_ELAPSED_TIME (end_append_time, start_append_time);
    }

  log_Stat.use_append_page_sec = elapsed;
  gettimeofday (&start_append_time, NULL);

  commit_count = log_Stat.commit_count - prev_commit_count_in_append;

  prev_commit_count_in_append = log_Stat.commit_count;

  log_Stat.last_commit_count_while_using_a_page = commit_count;
  log_Stat.total_commit_count_while_using_a_page += log_Stat.last_commit_count_while_using_a_page;
#endif /* CUBRID_DEBUG */

  log_Stat.total_append_page_count++;

#if defined(CUBRID_DEBUG)
  er_log_debug (ARG_FILE_LINE,
		"log_next_append_page: new page id(%lld) total_append_page_count(%ld)"
		" num_toflush(%d) use_append_page_sec(%f) need_flush(%d) commit_count(%ld)"
		" total_commit_count(%ld)\n", (int) log_Stat.last_append_pageid, log_Stat.total_append_page_count,
		flush_info->num_toflush, log_Stat.use_append_page_sec, need_flush,
		log_Stat.last_commit_count_while_using_a_page, log_Stat.total_commit_count_while_using_a_page);
#endif /* CUBRID_DEBUG */
}

/*
 * log_writev_append_pages - Write a set of sequential pages
 *
 * return: to_flush or NULL
 *
 *   to_flush(in): Array to address of content of pages to flush
 *   npages(in): Number of pages to flush
 *
 * NOTE:Flush to disk a set of log contiguous pages.
 */
static LOG_PAGE **
logpb_writev_append_pages (THREAD_ENTRY * thread_p, LOG_PAGE ** to_flush, DKNPAGES npages)
{
  LOG_BUFFER *bufptr;
  LOG_PHY_PAGEID phy_pageid;
  int i;
  FILEIO_WRITE_MODE write_mode = FILEIO_WRITE_DEFAULT_WRITE;
  char enc_pgbuf[IO_MAX_PAGE_SIZE + MAX_ALIGNMENT];
  LOG_PAGE *log_pgptr = NULL;
  LOG_PAGE *enc_pgptr = NULL;

  enc_pgptr = (LOG_PAGE *) PTR_ALIGN (enc_pgbuf, MAX_ALIGNMENT);

#if !defined (CS_MODE)
  write_mode = dwb_is_created () == true ? FILEIO_WRITE_NO_COMPENSATE_WRITE : FILEIO_WRITE_DEFAULT_WRITE;
#endif

  /* In this point, flush buffer cannot be replaced by trans. So, bufptr's pageid and phy_pageid are not changed. */
  if (npages > 0)
    {
      bufptr = logpb_get_log_buffer (to_flush[0]);
      phy_pageid = bufptr->phy_pageid;

      logpb_log ("logpb_writev_append_pages: started with pageid = %lld and phy_pageid = %lld\n",
		 (long long int) bufptr->pageid, (long long int) phy_pageid);

      for (i = 0; i < npages; i++)
	{
	  /* Set page CRC before writing to disk. */
	  if (logpb_set_page_checksum (thread_p, to_flush[i]) != NO_ERROR)
	    {
	      return NULL;
	    }
	}
      for (i = 0; i < npages; i++)
	{
	  log_pgptr = to_flush[i];

	  logpb_log ("logpb_writev_append_pages: The page (%lld) is being tde-encrypted: %d\n",
		     (long long int) log_pgptr->hdr.logical_pageid, LOG_IS_PAGE_TDE_ENCRYPTED (log_pgptr));
	  if (LOG_IS_PAGE_TDE_ENCRYPTED (log_pgptr))
	    {
	      if (tde_encrypt_log_page (log_pgptr, logpb_get_tde_algorithm (log_pgptr), enc_pgptr) != NO_ERROR)
		{
		  /* 
		   * if encrpytion fails, it just skip it and off the tde flag. The page will never be encrypted in this case.
		   * It menas once it fails, the page always spill user data un-encrypted from then.
		   */
		  logpb_set_tde_algorithm (thread_p, log_pgptr, TDE_ALGORITHM_NONE);
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TDE_ENCRYPTION_LOGPAGE_ERORR_AND_OFF_TDE, 1,
			  log_pgptr->hdr.logical_pageid);
		}
	      else
		{
		  log_pgptr = enc_pgptr;
		}
	    }

	  if (fileio_write (thread_p, log_Gl.append.vdes, log_pgptr, phy_pageid + i, LOG_PAGESIZE, write_mode) == NULL)
	    {
	      if (er_errid () == ER_IO_WRITE_OUT_OF_SPACE)
		{
		  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_WRITE_OUT_OF_SPACE, 4, bufptr->pageid,
			  phy_pageid, log_Name_active, log_Gl.hdr.db_logpagesize);
		}
	      else
		{
		  er_set_with_oserror (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_WRITE, 3, bufptr->pageid,
				       phy_pageid, log_Name_active);
		}
	      to_flush = NULL;
	      break;
	    }
	}
    }

  return to_flush;
}

/*
 * logpb_write_toflush_pages_to_archive - Background archiving
 *
 * NOTE : write flushed pages to temporary archiving volume
 * (which will be renamed to real archiving volume) at this time.
 * but don't write last page because it will be modified & flushed again.
 * in error case, dismount temp archiving volume and give up background
 * archiving.
 */
static void
logpb_write_toflush_pages_to_archive (THREAD_ENTRY * thread_p)
{
  int i;
  LOG_PAGEID pageid, prev_lsa_pageid;
  LOG_PHY_PAGEID phy_pageid;
  char log_pgbuf[IO_MAX_PAGE_SIZE + MAX_ALIGNMENT];
  char enc_pgbuf[IO_MAX_PAGE_SIZE + MAX_ALIGNMENT];
  LOG_PAGE *log_pgptr = NULL;
  LOG_PAGE *enc_pgptr = NULL;
  LOG_BUFFER *bufptr;
  LOG_FLUSH_INFO *flush_info = &log_Gl.flush_info;
  BACKGROUND_ARCHIVING_INFO *bg_arv_info = &log_Gl.bg_archive_info;
  FILEIO_WRITE_MODE write_mode;

  assert (prm_get_bool_value (PRM_ID_LOG_BACKGROUND_ARCHIVING));

  if (log_Gl.bg_archive_info.vdes == NULL_VOLDES || flush_info->num_toflush <= 1)
    {
      return;
    }

  pageid = bg_arv_info->current_page_id;
  prev_lsa_pageid = log_Gl.append.prev_lsa.pageid;
  i = 0;
  write_mode = dwb_is_created () == true ? FILEIO_WRITE_NO_COMPENSATE_WRITE : FILEIO_WRITE_DEFAULT_WRITE;

  while (pageid < prev_lsa_pageid && i < flush_info->num_toflush)
    {
      bufptr = logpb_get_log_buffer (flush_info->toflush[i]);
      if (pageid > bufptr->pageid)
	{
	  assert_release (pageid <= bufptr->pageid);
	  fileio_dismount (thread_p, bg_arv_info->vdes);
	  bg_arv_info->vdes = NULL_VOLDES;
	  return;
	}
      else if (pageid < bufptr->pageid)
	{
	  LOG_LSA current_lsa;

	  current_lsa.pageid = pageid;
	  current_lsa.offset = LOG_PAGESIZE;
	  /* to flush all omitted pages by the previous archiving */
	  log_pgptr = (LOG_PAGE *) PTR_ALIGN (log_pgbuf, MAX_ALIGNMENT);

	  if (logpb_fetch_page (thread_p, &current_lsa, LOG_CS_FORCE_USE, log_pgptr) != NO_ERROR)
	    {
	      fileio_dismount (thread_p, bg_arv_info->vdes);
	      bg_arv_info->vdes = NULL_VOLDES;
	      return;
	    }
	}
      else
	{
	  log_pgptr = flush_info->toflush[i];
	  i++;
	}

#if !defined(NDEBUG)
      logpb_debug_check_log_page (thread_p, log_pgptr);
#endif
      phy_pageid = (LOG_PHY_PAGEID) (pageid - bg_arv_info->start_page_id + 1);
      assert_release (phy_pageid > 0);

      logpb_log ("logpb_write_toflush_pages_to_archive: The page (%lld) is being tde-encrypted: %d\n",
		 (long long int) pageid, LOG_IS_PAGE_TDE_ENCRYPTED (log_pgptr));

      if (LOG_IS_PAGE_TDE_ENCRYPTED (log_pgptr))
	{
	  enc_pgptr = (LOG_PAGE *) PTR_ALIGN (enc_pgbuf, MAX_ALIGNMENT);
	  if (tde_encrypt_log_page (log_pgptr, logpb_get_tde_algorithm (log_pgptr), enc_pgptr) != NO_ERROR)
	    {
	      /* 
	       * if encrpytion fails, it just skip it and off the tde flag. The page will never be encrypted in this case.
	       * It menas once it fails, the page always spill user data un-encrypted from then.
	       */
	      logpb_set_tde_algorithm (thread_p, log_pgptr, TDE_ALGORITHM_NONE);
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TDE_ENCRYPTION_LOGPAGE_ERORR_AND_OFF_TDE, 1, pageid);
	    }
	  else
	    {
	      log_pgptr = enc_pgptr;
	    }
	}

      if (fileio_write (thread_p, bg_arv_info->vdes, log_pgptr, phy_pageid, LOG_PAGESIZE, write_mode) == NULL)
	{
	  fileio_dismount (thread_p, bg_arv_info->vdes);
	  bg_arv_info->vdes = NULL_VOLDES;
	  return;
	}

      pageid++;
      bg_arv_info->current_page_id = pageid;
    }

  assert_release (bg_arv_info->current_page_id >= bg_arv_info->last_sync_pageid);
  if ((bg_arv_info->current_page_id - bg_arv_info->last_sync_pageid) > prm_get_integer_value (PRM_ID_PB_SYNC_ON_NFLUSH))
    {
      /* System volume. No need to sync DWB. */
      fileio_synchronize (thread_p, bg_arv_info->vdes, log_Name_bg_archive, FILEIO_SYNC_ONLY);
      bg_arv_info->last_sync_pageid = bg_arv_info->current_page_id;
    }
}

/*
 * logpb_append_next_record -
 *
 * return: NO_ERROR
 *
 *   node(in):
 */
static int
logpb_append_next_record (THREAD_ENTRY * thread_p, LOG_PRIOR_NODE * node)
{
  if (!LSA_EQ (&node->start_lsa, &log_Gl.hdr.append_lsa))
    {
      logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "logpb_append_next_record");
    }

  /* forcing flush in the middle of log record append is a complicated business. try to avoid it if possible. */
  if (log_Gl.flush_info.num_toflush + 1 >= log_Gl.flush_info.max_toflush)	/* flush will be forced on next page */
    {
      /* flush early to avoid complicated case */
      logpb_flush_all_append_pages (thread_p);
    }

  /* to tde-encrypt pages which is being created while appending */
  log_Gl.append.appending_page_tde_encrypted = prior_is_tde_encrypted (node);

  logpb_log ("logpb_append_next_record: append a record\n"
	     "log_Gl.hdr.append_lsa.offset = %d, total record size = %d, TDE-encryption = %d\n",
	     log_Gl.hdr.append_lsa.offset,
	     sizeof (LOG_RECORD_HEADER) + node->data_header_length + node->ulength + node->rlength,
	     log_Gl.append.appending_page_tde_encrypted);


  logpb_start_append (thread_p, &node->log_header);

  if (node->data_header != NULL)
    {
      LOG_APPEND_ADVANCE_WHEN_DOESNOT_FIT (thread_p, node->data_header_length);
      logpb_append_data (thread_p, node->data_header_length, node->data_header);
    }

  if (node->udata != NULL)
    {
      logpb_append_data (thread_p, node->ulength, node->udata);
    }

  if (node->rdata != NULL)
    {
      logpb_append_data (thread_p, node->rlength, node->rdata);
    }

  logpb_end_append (thread_p, &node->log_header);

  log_Gl.append.appending_page_tde_encrypted = false;

  logpb_log ("logpb_append_next_record: append a record end.\n");

  return NO_ERROR;
}

/*
 * logpb_append_prior_lsa_list -
 *
 * return: NO_ERROR
 *
 *   list(in/out):
 */
static int
logpb_append_prior_lsa_list (THREAD_ENTRY * thread_p, LOG_PRIOR_NODE * list)
{
  LOG_PRIOR_NODE *node;

  assert (LOG_CS_OWN_WRITE_MODE (thread_p));

  /* append prior_flush_list */
  assert (log_Gl.prior_info.prior_flush_list_header == NULL);
  log_Gl.prior_info.prior_flush_list_header = list;

  /* append log buffer */
  while (log_Gl.prior_info.prior_flush_list_header != NULL)
    {
      node = log_Gl.prior_info.prior_flush_list_header;
      log_Gl.prior_info.prior_flush_list_header = node->next;

      logpb_append_next_record (thread_p, node);

      if (node->data_header != NULL)
	{
	  free_and_init (node->data_header);
	}
      if (node->udata != NULL)
	{
	  free_and_init (node->udata);
	}
      if (node->rdata != NULL)
	{
	  free_and_init (node->rdata);
	}

      free_and_init (node);
    }

  return NO_ERROR;
}

/*
 * prior_lsa_remove_prior_list:
 *
 * return: prior list
 *
 */
static LOG_PRIOR_NODE *
prior_lsa_remove_prior_list (THREAD_ENTRY * thread_p)
{
  LOG_PRIOR_NODE *prior_list;

  assert (LOG_CS_OWN_WRITE_MODE (thread_p));

  prior_list = log_Gl.prior_info.prior_list_header;

  log_Gl.prior_info.prior_list_header = NULL;
  log_Gl.prior_info.prior_list_tail = NULL;
  log_Gl.prior_info.list_size = 0;

  return prior_list;
}

/*
 * logpb_prior_lsa_append_all_list:
 *
 * return: NO_ERROR
 *
 */
int
logpb_prior_lsa_append_all_list (THREAD_ENTRY * thread_p)
{
  LOG_PRIOR_NODE *prior_list;
  INT64 current_size;

  assert (LOG_CS_OWN_WRITE_MODE (thread_p));

  log_Gl.prior_info.prior_lsa_mutex.lock ();
  current_size = log_Gl.prior_info.list_size;
  prior_list = prior_lsa_remove_prior_list (thread_p);
  log_Gl.prior_info.prior_lsa_mutex.unlock ();

  if (prior_list != NULL)
    {
      perfmon_add_stat (thread_p, PSTAT_PRIOR_LSA_LIST_SIZE, (unsigned int) current_size / ONE_K);	/* kbytes */
      perfmon_inc_stat (thread_p, PSTAT_PRIOR_LSA_LIST_REMOVED);

      logpb_append_prior_lsa_list (thread_p, prior_list);
    }

  return NO_ERROR;
}

/*
 * logpb_dump_log_page_area - Dump log page area.
 *
 * return: nothing
 *   log_pgptr(in): log page
 *   offset(in): offset in page area to start logging
 *   length(in): length to log
 */
void
logpb_dump_log_page_area (THREAD_ENTRY * thread_p, LOG_PAGE * log_pgptr, int offset, int length)
{
  const int block_size = 4 * ONE_K;
  char log_block_string[block_size * 4], log_header_string[200], *src_ptr, *dest_ptr;
  int count_remaining_bytes, count_bytes_to_dump, i;
  int line_no = 0;

  if (logpb_Logging == false)
    {
      return;
    }

  assert (log_pgptr != NULL);
  if (offset < 0 || length < 0 || length > LOGAREA_SIZE || offset + length > LOGAREA_SIZE)
    {
      return;
    }

  sprintf (log_header_string, "page_id = %lld, checksum = %d, offset = %d, length = %d\n",
	   (long long int) log_pgptr->hdr.logical_pageid, log_pgptr->hdr.checksum, offset, length);

  count_remaining_bytes = length;
  src_ptr = log_pgptr->area + offset;
  while (count_remaining_bytes > 0)
    {
      dest_ptr = log_block_string;
      count_bytes_to_dump = MIN (count_remaining_bytes, block_size);
      for (i = 0; i < count_bytes_to_dump; i++, src_ptr++)
	{
	  if (i % 32 == 0)
	    {
	      dest_ptr += sprintf (dest_ptr, "\n  %05d: ", line_no++);
	    }

	  dest_ptr += sprintf (dest_ptr, "%02X ", (unsigned char) (*src_ptr));
	}

      dest_ptr += sprintf (dest_ptr, "\n");
      logpb_log ("logpb_dump_log_page_area: header = %s data = %s\n", log_header_string, log_block_string);
      count_remaining_bytes -= count_bytes_to_dump;
    }
}

/*
 * logpb_page_get_first_null_block_lsa - Get LSA of first null block in log page.
 *
 * return: nothing
 *   thread_p(in): thread entry
 *   log_pgptr(in): log page
 *   first_null_block_lsa(out): LSA of first null block.
 */
void
logpb_page_get_first_null_block_lsa (THREAD_ENTRY * thread_p, LOG_PAGE * log_pgptr, LOG_LSA * first_null_block_lsa)
{
  const int block_size = 4 * ONE_K;
  char null_buffer[block_size + MAX_ALIGNMENT], *null_block;
  int i, max_num_blocks = LOG_PAGESIZE / block_size;

  assert (log_pgptr != NULL && first_null_block_lsa != NULL);

  null_block = PTR_ALIGN (null_buffer, MAX_ALIGNMENT);
  memset (null_block, LOG_PAGE_INIT_VALUE, block_size);

  LSA_SET_NULL (first_null_block_lsa);

  /* Set LSA of first NULL block. */
  for (i = 0; i < max_num_blocks; i++)
    {
      /* Search for null blocks. */
      if (memcmp (((char *) log_pgptr) + (i * block_size), null_block, block_size) == 0)
	{
	  /* Found the null block. Computes its LSA. */
	  first_null_block_lsa->pageid = log_pgptr->hdr.logical_pageid;
	  first_null_block_lsa->offset = i * block_size;

	  if (first_null_block_lsa->offset > 0)
	    {
	      /* Skip log header size. */
	      first_null_block_lsa->offset -= sizeof (LOG_HDRPAGE);
	    }

	  assert (first_null_block_lsa->offset >= 0);
	  break;
	}
    }
}

/*
 * logpb_flush_all_append_pages - Flush log append pages
 *
 * return: 1 : log flushed, 0 : do not need log flush, < 0 : error code
 *
 */
static int
logpb_flush_all_append_pages (THREAD_ENTRY * thread_p)
{
  LOG_BUFFER *bufptr = NULL;	/* The current buffer log append page scanned */
  LOG_BUFFER *prv_bufptr = NULL;	/* The previous buffer log append page scanned */
  int idxflush;			/* An index into the first log page buffer to flush */
  bool need_sync;		/* How we flush anything ? */

  int i;
  LOG_PAGEID first_append_pageid = NULL_PAGEID;
  bool need_flush = true;
  int error_code = NO_ERROR;
  int flush_page_count = 0;
#if defined(CUBRID_DEBUG)
  struct timeval start_time = { 0, 0 };
  struct timeval end_time = { 0, 0 };
  int dirty_page_count = 0;
  int curr_flush_count = 0;
  long commit_count = 0;
  static long prev_commit_count_in_flush = 0;
#endif /* CUBRID_DEBUG */
  bool hold_flush_mutex = false;
  LOG_FLUSH_INFO *flush_info = &log_Gl.flush_info;
  LOG_LSA nxio_lsa;

  int rv;
#if defined(SERVER_MODE)
  INT64 flush_start_time = 0;
  INT64 flush_completed_time = 0;
  INT64 all_writer_thr_end_time = 0;

  LOGWR_INFO *writer_info = log_Gl.writer_info;
  LOGWR_ENTRY *entry = NULL;
  THREAD_ENTRY *wait_thread_p = NULL;
#endif /* SERVER_MODE */

  assert (LOG_CS_OWN_WRITE_MODE (thread_p));

  logpb_log ("called logpb_flush_all_append_pages\n");

#if defined(CUBRID_DEBUG)
  er_log_debug (ARG_FILE_LINE, "logpb_flush_all_append_pages: start\n");

  gettimeofday (&start_time, NULL);
#endif /* CUBRID_DEBUG */

  rv = pthread_mutex_lock (&flush_info->flush_mutex);
  hold_flush_mutex = true;

  if (flush_info->num_toflush < 1)
    {
      need_flush = false;
    }
  else if (flush_info->num_toflush == 1)
    {
      /*
       * Don't need to do anything if the page is not dirty.
       *
       * This block is used to avoid updating the last page with an end of file log when it is not needed at all.
       */

      bufptr = logpb_get_log_buffer (flush_info->toflush[0]);
      if (!logpb_is_dirty (thread_p, flush_info->toflush[0]))
	{
	  need_flush = false;
	}
    }

  pthread_mutex_unlock (&flush_info->flush_mutex);
  hold_flush_mutex = false;

  if (!need_flush)
    {
      return 0;
    }

#if !defined(NDEBUG)
  {
    const char *env_value;
    int verbose_dump = -1;

    if (verbose_dump == -1)
      {
	/*
	 * Do we want to dump the buffer pool for future verification
	 */
	if ((env_value = envvar_get ("LOG_FLUSH_VERBOSE_DUMP")) != NULL)
	  {
	    verbose_dump = atoi (env_value) != 0 ? 1 : 0;
	  }
	else
	  {
	    verbose_dump = 0;
	  }
      }

    if (verbose_dump != 0)
      {
	fprintf (stdout, "\n DUMP BEFORE FLUSHING APPEND PAGES\n");
	logpb_dump (thread_p, stdout);
      }
  }
#endif

#if defined(SERVER_MODE)
  if (thread_p && thread_p->type != TT_DAEMON && thread_p->type != TT_VACUUM_MASTER
      && thread_p->type != TT_VACUUM_WORKER)
    {
      /* set event logging parameter */
      thread_p->event_stats.trace_log_flush_time = prm_get_integer_value (PRM_ID_LOG_TRACE_FLUSH_TIME_MSECS);
    }
#endif /* SERVER_MODE */

#if defined(CUBRID_DEBUG)
  if (log_Gl.append.get_nxio_lsa ().pageid != logpb_get_page_id (flush_info->toflush[0]))
    {
      er_log_debug (ARG_FILE_LINE,
		    "logpb_flush_all_append_pages: SYSTEM ERROR\n  NXIO_PAGE %d does not seem the same as next free"
		    " append page %d to flush\n", log_Gl.append.get_nxio_lsa ().pageid,
		    logpb_get_page_id (flush_info->toflush[0]));
      goto error;
    }
#endif /* CUBRID_DEBUG */

  /* how this works:
   * ok, so we might have to flush several pages here. if there is only one page, the implementation is straight
   * forward, just flush the page.
   *
   * however, if there are two or more pages, we really need to make sure that the first page, where previous end of log
   * record resides (where nxio_lsa points), is flushed last! we cannot validate the new end of log record until we are
   * sure all log pages have been flushed!
   * so, we'll do a two-step flushing: in the first step we skip nxio_lsa page and flush all other pages. in the second
   * step we flush the nxio_lsa page.
   *
   * the story becomes a lot more complicated when a log entry is only partially appended before flush is called. this
   * can happen for when log page buffer becomes full. the problem here is that we cannot yet validate the flushed pages
   * before flushing this record entirely; not all pages. what we do here is replace the incomplete record with end of
   * log record (very important! not in log page buffer, but in a copy of the log page; that way we allow others to read
   * the correct version from buffer). the overwritten copy is flushed to disk.
   * when the log record is fully appended (there can be several iterations of flush if we deal with a very large log
   * record and log page buffer is very small), we call again flush again to make sure all remaining record pages are
   * written to disk. at the end of this flush iteration, the original record is written back and page is flushed again,
   * validating new record.
   *
   * to see full implementation, follow references of log_Pb.partial_append.
   *
   * todo: we might think of a better and simpler solution. the most difficult case to handle is very large log record,
   *       larger than log page buffer. since log records can theoretically be any size, the solution will have to
   *       consider this case.
   *       for now I chose a solution similar to the implementation used before the log page buffer redesign.
   */

  /*
   * Add an end of log marker to detect the end of the log.
   * The marker should be added at the end of the log if there is only one page to be flushed.
   * That is, if we are not in the middle of appending a new log record. Otherwise, we need to change the label of
   * the last append record as log end record. Flush and then check it back.
   */

  if (log_Pb.partial_append.status == LOGPB_APPENDREC_IN_PROGRESS)
    {
      /* Flush all log append records on such page except the current log record which has not been finished.
       * Save the log record type of this record, overwrite an eof record on such position, and flush the page.
       * Then, restore the record back on the page and change the current append log sequence address.
       */
      logpb_log ("logpb_flush_all_append_pages: incomplete record at log_Gl.append.prev_lsa=%lld|%d when flush is "
		 "called. we'll overwrite the log record with eof.\n", (long long int) log_Gl.append.prev_lsa.pageid,
		 (int) log_Gl.append.prev_lsa.offset);

      /* first, let's see if this is page is still in log page buffer */
      first_append_pageid = log_Gl.append.prev_lsa.pageid;
      bufptr = &log_Pb.buffers[logpb_get_log_buffer_index (first_append_pageid)];

      if (bufptr->pageid != first_append_pageid)
	{
	  assert_release (false);
	  logpb_log ("logpb_flush_all_append_pages: fatal error, partial page not found in log page buffer.");
	  error_code = ER_FAILED;
	  goto error;
	}

      /* copy from log page buffer */
      memcpy (log_Pb.partial_append.log_page_record_header, bufptr->logpage, LOG_PAGESIZE);

      /* set entry in log page buffer not dirty. we don't want it to get flushed again */
      bufptr->dirty = false;

      /* Overwrite it with an end of log marker */
      log_Pb.partial_append.record_header_p =
	(LOG_RECORD_HEADER *) (log_Pb.partial_append.log_page_record_header->area + log_Gl.append.prev_lsa.offset);
      log_Pb.partial_append.original_record_header = *log_Pb.partial_append.record_header_p;
      LSA_SET_NULL (&log_Pb.partial_append.record_header_p->forw_lsa);
      log_Pb.partial_append.record_header_p->type = LOG_END_OF_LOG;

      /* write page to disk as it is */
      if (logpb_write_page_to_disk (thread_p, log_Pb.partial_append.log_page_record_header, first_append_pageid)
	  != NO_ERROR)
	{
	  error_code = ER_FAILED;
	  goto error;
	}
      LSA_COPY (&log_Gl.hdr.eof_lsa, &log_Gl.append.prev_lsa);

      log_Pb.partial_append.status = LOGPB_APPENDREC_PARTIAL_FLUSHED_END_OF_LOG;
    }
  else if (log_Pb.partial_append.status == LOGPB_APPENDREC_PARTIAL_FLUSHED_END_OF_LOG)
    {
      logpb_log ("logpb_flush_all_append_pages: continue flushing page of partially appended log record.\n");
    }
  else if (log_Pb.partial_append.status == LOGPB_APPENDREC_PARTIAL_ENDED
	   || log_Pb.partial_append.status == LOGPB_APPENDREC_SUCCESS)
    {
      /* Add an end of log marker to detect the end of the log.
       * Don't advance the log address, the log end of file is overwritten at a later point. */
      LOG_RECORD_HEADER eof;

      logpb_log ("logpb_flush_all_append_pages: append end of log record at append_lsa = %lld|%d.\n",
		 (long long int) log_Gl.hdr.append_lsa.pageid, (int) log_Gl.hdr.append_lsa.offset);
      eof.trid = LOG_READ_NEXT_TRANID;
      LSA_SET_NULL (&eof.prev_tranlsa);
      LSA_COPY (&eof.back_lsa, &log_Gl.append.prev_lsa);
      LSA_SET_NULL (&eof.forw_lsa);
      eof.type = LOG_END_OF_LOG;

      logpb_start_append (thread_p, &eof);
    }
  else
    {
      /* unexpected status here */
      assert_release (false);
      error_code = ER_FAILED;
      goto error;
    }

  /*
   * Now flush all contiguous log append dirty pages. The first log append dirty page is flushed at the end,
   * so we can synchronize it with the rest.
   */

#if defined(SERVER_MODE)
  /* It changes the status of waiting log writer threads and wakes them up */
  if (!HA_DISABLED () && !writer_info->skip_flush)
    {
      assert (hold_flush_mutex == false);
      LOG_CS_DEMOTE (thread_p);

      rv = pthread_mutex_lock (&writer_info->flush_start_mutex);
      rv = pthread_mutex_lock (&writer_info->wr_list_mutex);

      if (thread_p != NULL && thread_p->event_stats.trace_log_flush_time > 0)
	{
	  flush_start_time = log_get_clock_msec ();

	  writer_info->trace_last_writer = true;
	  writer_info->last_writer_elapsed_time = 0;
	  writer_info->last_writer_client_info.client_type = DB_CLIENT_TYPE_UNKNOWN;
	  writer_info->last_writer_client_info.client_info.clear ();
	  writer_info->last_writer_client_info.db_user.clear ();
	  writer_info->last_writer_client_info.program_name.clear ();
	  writer_info->last_writer_client_info.login_name.clear ();
	  writer_info->last_writer_client_info.host_name.clear ();
	  writer_info->last_writer_client_info.client_ip_addr.clear ();
	  writer_info->last_writer_client_info.process_id = 0;
	}

      entry = writer_info->writer_list;
      while (entry)
	{
	  if (entry->status == LOGWR_STATUS_WAIT)
	    {
	      wait_thread_p = entry->thread_p;
	      assert (wait_thread_p != thread_p);

	      thread_lock_entry (wait_thread_p);

	      /* If THREAD_RESUME_DUE_TO_INTERRUPT, do not set the entry status to avoid deadlock
	       * between flush_end_cond and CSECT_LOG.
	       */
	      if (thread_p->resume_status != THREAD_RESUME_DUE_TO_INTERRUPT)
		{
		  /* Still waiting for LOGWR. */
		  entry->status = LOGWR_STATUS_FETCH;
		  if (wait_thread_p->resume_status == THREAD_LOGWR_SUSPENDED)
		    {
		      thread_wakeup_already_had_mutex (wait_thread_p, THREAD_LOGWR_RESUMED);
		    }
		}

	      thread_unlock_entry (wait_thread_p);
	    }
	  entry = entry->next;
	}

      rv = pthread_mutex_lock (&writer_info->flush_wait_mutex);
      writer_info->flush_completed = false;
      rv = pthread_mutex_unlock (&writer_info->flush_wait_mutex);

      pthread_mutex_unlock (&writer_info->wr_list_mutex);
      pthread_mutex_unlock (&writer_info->flush_start_mutex);
    }
#endif /* SERVER_MODE */

  idxflush = -1;
  prv_bufptr = NULL;
  need_sync = false;

  rv = pthread_mutex_lock (&flush_info->flush_mutex);
  hold_flush_mutex = true;

#if defined(CUBRID_DEBUG)
  log_scan_flush_info (log_dump_pageid);
  er_log_debug (ARG_FILE_LINE, "\n");
#endif /* CUBRID_DEBUG */

  /* Record number of writes in statistics */
  perfmon_add_stat (thread_p, PSTAT_LOG_NUM_IOWRITES, flush_info->num_toflush);

  /* loop through all to flush list. do a two-step process:
   * 1. skip all pages not dirty. also skip the page of nxio_lsa! it must be flushed last!
   * 2. collect and flush all dirty and successive pages.
   */
  i = 0;
  while (true)
    {
      /* skip all not dirty */
      for (; i < flush_info->num_toflush; i++)
	{
	  bufptr = logpb_get_log_buffer (flush_info->toflush[i]);
	  assert (bufptr->pageid == flush_info->toflush[i]->hdr.logical_pageid);
	  if (bufptr->dirty && bufptr->pageid != log_Gl.append.get_nxio_lsa ().pageid)
	    {
	      /* found dirty */
	      break;
	    }
	  logpb_log ("logpb_flush_all_append_pages: skip flushing not dirty page %lld.\n", bufptr->pageid);
	}
      if (i == flush_info->num_toflush)
	{
	  /* nothing left to flush */
	  break;
	}

      /* we have a dirty record */
      assert (bufptr->dirty);
      prv_bufptr = bufptr;
      idxflush = i;

      /* advance to next */
      i++;

      /* collect all consecutive pages that are dirty */
      for (; i < flush_info->num_toflush; i++)
	{
	  bufptr = logpb_get_log_buffer (flush_info->toflush[i]);

	  assert (bufptr->pageid == flush_info->toflush[i]->hdr.logical_pageid);

	  if (!bufptr->dirty)
	    {
	      /* not dirty */
	      break;
	    }
	  if (bufptr->pageid == log_Gl.append.get_nxio_lsa ().pageid)
	    {
	      /* this must be flushed last! */
	      break;
	    }
	  if (prv_bufptr->pageid + 1 != bufptr->pageid)
	    {
	      /* not successive pages */
	      break;
	    }
	  if (prv_bufptr->phy_pageid + 1 != bufptr->phy_pageid)
	    {
	      /* not successive pages on disk */
	      break;
	    }

	  prv_bufptr = bufptr;
	}

      if (logpb_writev_append_pages (thread_p, &flush_info->toflush[idxflush], i - idxflush) == NULL)
	{
	  /* is this acceptable? */
	  assert_release (false);
	  error_code = ER_FAILED;
	  goto error;
	}
      else
	{
	  int buf_iter;
	  need_sync = true;
	  flush_page_count += i - idxflush;

	  logpb_log ("logpb_flush_all_append_pages: flushed all pages in range [%lld, %lld].\n",
		     (long long int) flush_info->toflush[idxflush]->hdr.logical_pageid,
		     (long long int) flush_info->toflush[idxflush]->hdr.logical_pageid + i - idxflush - 1);

	  /* set not dirty what we have flushed */
	  for (buf_iter = idxflush; buf_iter < i; buf_iter++)
	    {
	      bufptr = logpb_get_log_buffer (flush_info->toflush[buf_iter]);
	      bufptr->dirty = false;
	    }
#if defined (CUBRID_DEBUG)
	  dirty_page_count += i - idxflush;
#endif /* CUBRID_DEBUG */
	}

      if (i == flush_info->num_toflush)
	{
	  /* nothing left to flush */
	  break;
	}
    }

  /* now flush the nxio_lsa page... unless it is the page of header for incomplete log record */
  nxio_lsa = log_Gl.append.get_nxio_lsa ();
  if (log_Pb.partial_append.status == LOGPB_APPENDREC_SUCCESS || (nxio_lsa.pageid != log_Gl.append.prev_lsa.pageid))
    {
      assert (log_Pb.partial_append.status == LOGPB_APPENDREC_SUCCESS
	      || log_Pb.partial_append.status == LOGPB_APPENDREC_PARTIAL_FLUSHED_END_OF_LOG);

      bufptr = &log_Pb.buffers[logpb_get_log_buffer_index (nxio_lsa.pageid)];

      if (bufptr->pageid != nxio_lsa.pageid)
	{
	  /* not expected. */
	  assert_release (false);

	  logpb_log ("logpb_flush_all_append_pages: fatal error, nxio_lsa %lld|%d page not found in buffer. "
		     "bufptr->pageid is %lld instead.\n",
		     (long long int) nxio_lsa.pageid, (int) nxio_lsa.offset, (long long int) bufptr->pageid);

	  error_code = ER_FAILED;
	  goto error;
	}

      if (!bufptr->dirty)
	{
	  /* not expected */
	  assert_release (false);

	  logpb_log ("logpb_flush_all_append_pages: fatal error, nxio_lsa %lld|%d page is not dirty.\n",
		     (long long int) nxio_lsa.pageid, (int) nxio_lsa.offset);

	  error_code = ER_FAILED;
	  goto error;
	}

      logpb_write_page_to_disk (thread_p, bufptr->logpage, bufptr->pageid);
      need_sync = true;
      bufptr->dirty = false;
      flush_page_count += 1;

      logpb_log ("logpb_flush_all_append_pages: flushed nxio_lsa = %lld|%d page to disk.\n",
		 (long long int) log_Gl.append.get_nxio_lsa ().pageid, (int) log_Gl.append.get_nxio_lsa ().offset);

      if (logpb_Logging)
	{
	  /* Dump latest portion of page, for debugging purpose. */
	  logpb_dump_log_page_area (thread_p, bufptr->logpage, (int) (log_Gl.append.get_nxio_lsa ().offset),
				    (int) sizeof (LOG_RECORD_HEADER));
	  logpb_dump_log_page_area (thread_p, bufptr->logpage, (int) (log_Gl.hdr.eof_lsa.offset),
				    (int) sizeof (LOG_RECORD_HEADER));
	}
    }
  else
    {
      logpb_log ("logpb_flush_all_append_pages: skipped flushing nxio_lsa = %lld|%d page to disk because it matches "
		 "the header page for incomplete record (prev_lsa = %lld|%d).\n",
		 (long long int) log_Gl.append.get_nxio_lsa ().pageid, (int) log_Gl.append.get_nxio_lsa ().offset,
		 (long long int) log_Gl.append.prev_lsa.pageid, (int) log_Gl.append.prev_lsa.offset);
    }

  /* Make sure that all of the above log writes are synchronized with any future log writes.
   * That is, the pages should be stored on physical disk.
   */
  if (need_sync == true)
    {
      if (prm_get_integer_value (PRM_ID_SUPPRESS_FSYNC) == 0
	  || (log_Stat.total_sync_count % prm_get_integer_value (PRM_ID_SUPPRESS_FSYNC) == 0))
	{
	  /* System volume. No need to sync DWB. */
	  if (fileio_synchronize (thread_p, log_Gl.append.vdes, log_Name_active, FILEIO_SYNC_ONLY) == NULL_VOLDES)
	    {
	      error_code = ER_FAILED;
	      goto error;
	    }
	  log_Stat.total_sync_count++;
	}
    }

  /* dual writing (Background archiving) */
  if (prm_get_bool_value (PRM_ID_LOG_BACKGROUND_ARCHIVING))
    {
      logpb_write_toflush_pages_to_archive (thread_p);
    }

#if !defined(NDEBUG)
  if (prm_get_bool_value (PRM_ID_LOG_TRACE_DEBUG) && logpb_is_any_dirty (thread_p) == true)
    {
      er_log_debug (ARG_FILE_LINE, "logpb_flush_all_append_pages: Log Buffer contains dirty pages\n");
      logpb_dump (thread_p, stdout);
      fflush (stdout);
    }
#endif

  if (flush_info->num_toflush == flush_info->max_toflush)
    {
      log_Stat.log_buffer_full_count++;
    }
#if defined(CUBRID_DEBUG)
  curr_flush_count = flush_info->num_toflush;
#endif /* CUBRID_DEBUG */

  /*
   * Change the log sequence address to indicate the next append address to flush and synchronize
   */
  if (log_Pb.partial_append.status == LOGPB_APPENDREC_PARTIAL_ENDED)
    {
      /* partially flushed log record is now complete */

      /* overwrite with original log record. */
      *log_Pb.partial_append.record_header_p = log_Pb.partial_append.original_record_header;
      error_code =
	logpb_write_page_to_disk (thread_p, log_Pb.partial_append.log_page_record_header,
				  log_Pb.partial_append.log_page_record_header->hdr.logical_pageid);
      if (error_code != NO_ERROR)
	{
	  goto error;
	}
      ++flush_page_count;

      /* Update checksum. */
      first_append_pageid = log_Pb.partial_append.log_page_record_header->hdr.logical_pageid;
      bufptr = &log_Pb.buffers[logpb_get_log_buffer_index (first_append_pageid)];
      if (bufptr->pageid == first_append_pageid)
	{
	  bufptr->logpage->hdr.checksum = log_Pb.partial_append.log_page_record_header->hdr.checksum;
	  assert (!memcmp (bufptr->logpage, log_Pb.partial_append.log_page_record_header, LOG_PAGESIZE));
#if !defined(NDEBUG)
	  logpb_debug_check_log_page (thread_p, bufptr->logpage);
#endif
	}

      /* we need to also sync again */
      if (fileio_synchronize (thread_p, log_Gl.append.vdes, log_Name_active, FILEIO_SYNC_ONLY) == NULL_VOLDES)
	{
	  error_code = ER_FAILED;
	  goto error;
	}

      /* now we can set the nxio_lsa to append_lsa */
      log_Gl.append.set_nxio_lsa (log_Gl.hdr.append_lsa);

      log_Pb.partial_append.status = LOGPB_APPENDREC_PARTIAL_FLUSHED_ORIGINAL;

      logpb_log ("logpb_flush_all_append_pages: completed partial record and flush again its first page %lld. "
		 "nxio_lsa = %lld|%d.\n",
		 (long long int) log_Pb.partial_append.log_page_record_header->hdr.logical_pageid,
		 (long long int) log_Gl.append.get_nxio_lsa ().pageid, (int) log_Gl.append.get_nxio_lsa ().offset);
    }
  else if (log_Pb.partial_append.status == LOGPB_APPENDREC_PARTIAL_FLUSHED_END_OF_LOG)
    {
      /* we cannot set nxio_lsa to append_lsa yet. set it to append.prev_lsa */
      log_Gl.append.set_nxio_lsa (log_Gl.append.prev_lsa);

      logpb_log ("logpb_flush_all_append_pages: partial record flushed... set nxio_lsa = %lld|%d.\n",
		 (long long int) log_Gl.append.get_nxio_lsa ().pageid, (int) log_Gl.append.get_nxio_lsa ().offset);
    }
  else if (log_Pb.partial_append.status == LOGPB_APPENDREC_SUCCESS)
    {
      log_Gl.append.set_nxio_lsa (log_Gl.hdr.append_lsa);

      logpb_log ("logpb_flush_all_append_pages: set nxio_lsa = %lld|%d.\n",
		 (long long int) log_Gl.append.get_nxio_lsa ().pageid, (int) log_Gl.append.get_nxio_lsa ().offset);
    }
  else
    {
      /* unexpected */
      assert_release (false);
      error_code = ER_FAILED;
      goto error;
    }
  flush_info->num_toflush = 0;

  if (log_Gl.append.log_pgptr != NULL)
    {
      /* Add the append page */
      flush_info->toflush[flush_info->num_toflush] = log_Gl.append.log_pgptr;
      flush_info->num_toflush++;
    }

  log_Stat.flushall_append_pages_call_count++;
  log_Stat.last_flush_count_by_trans = flush_page_count;
  log_Stat.total_flush_count_by_trans += flush_page_count;

#if defined(CUBRID_DEBUG)
  gettimeofday (&end_time, NULL);

  log_Stat.last_flush_sec_by_trans = LOG_GET_ELAPSED_TIME (end_time, start_time);

  log_Stat.total_flush_sec_by_trans += log_Stat.last_flush_sec_by_trans;

  commit_count = log_Stat.commit_count - prev_commit_count_in_flush;
  prev_commit_count_in_flush = log_Stat.commit_count;

  log_Stat.last_commit_count_in_flush_pages = commit_count;
  log_Stat.total_commit_count_in_flush_pages += log_Stat.last_commit_count_in_flush_pages;

  er_log_debug (ARG_FILE_LINE,
		"logpb_flush_all_append_pages: flush page(%ld / %d / %ld) avg flush count(%f), avg flush sec(%f)"
		"commit count(%ld) avg commit count(%f)\n", log_Stat.last_flush_count_by_trans, dirty_page_count,
		curr_flush_count,
		(double) log_Stat.total_flush_count_by_trans / log_Stat.flushall_append_pages_call_count,
		log_Stat.total_flush_sec_by_trans / log_Stat.flushall_append_pages_call_count, commit_count,
		log_Stat.total_commit_count_in_flush_pages / log_Stat.flushall_append_pages_call_count);
#endif /* CUBRID_DEBUG */

  pthread_mutex_unlock (&flush_info->flush_mutex);
  hold_flush_mutex = false;

#if defined(SERVER_MODE)
  if (!HA_DISABLED () && !writer_info->skip_flush)
    {
      /* it sends signal to LWT to notify that flush is completed */
      rv = pthread_mutex_lock (&writer_info->flush_wait_mutex);

      if (thread_p != NULL && thread_p->event_stats.trace_log_flush_time > 0)
	{
	  flush_completed_time = log_get_clock_msec ();
	}

      writer_info->flush_completed = true;
      rv = pthread_cond_broadcast (&writer_info->flush_wait_cond);

      rv = pthread_mutex_unlock (&writer_info->flush_wait_mutex);

      /* It waits until all log writer threads are done */
      rv = pthread_mutex_lock (&writer_info->flush_end_mutex);

      rv = pthread_mutex_lock (&writer_info->wr_list_mutex);
      entry = writer_info->writer_list;
      while (entry != NULL)
	{
	  if (entry->status == LOGWR_STATUS_FETCH)
	    {
	      break;
	    }
	  entry = entry->next;
	}
      pthread_mutex_unlock (&writer_info->wr_list_mutex);

      if (entry != NULL)
	{
	  rv = pthread_cond_wait (&writer_info->flush_end_cond, &writer_info->flush_end_mutex);
	}

      rv = pthread_mutex_lock (&writer_info->wr_list_mutex);
      writer_info->trace_last_writer = false;

      if (thread_p != NULL && thread_p->event_stats.trace_log_flush_time > 0)
	{
	  all_writer_thr_end_time = log_get_clock_msec ();

	  if (all_writer_thr_end_time - flush_start_time > thread_p->event_stats.trace_log_flush_time)
	    {
	      event_log_log_flush_thr_wait (thread_p, flush_page_count, &writer_info->last_writer_client_info,
					    (int) (all_writer_thr_end_time - flush_start_time),
					    (int) (all_writer_thr_end_time - flush_completed_time),
					    (int) writer_info->last_writer_elapsed_time);
	    }
	}

      pthread_mutex_unlock (&writer_info->wr_list_mutex);

      pthread_mutex_unlock (&writer_info->flush_end_mutex);
      assert (hold_flush_mutex == false);
      LOG_CS_PROMOTE (thread_p);
    }
#endif /* SERVER_MODE */

#if defined(SERVER_MODE)
  if (thread_p && thread_p->type != TT_DAEMON && thread_p->type != TT_VACUUM_MASTER
      && thread_p->type != TT_VACUUM_WORKER)
    {
      /* reset event logging parameter */
      thread_p->event_stats.trace_log_flush_time = 0;
    }
#endif /* SERVER_MODE */

  return 1;

error:
  if (hold_flush_mutex)
    {
      pthread_mutex_unlock (&flush_info->flush_mutex);
    }

  logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "logpb_flush_all_append_pages");

#if defined(SERVER_MODE)
  if (thread_p && thread_p->type != TT_DAEMON && thread_p->type != TT_VACUUM_MASTER
      && thread_p->type != TT_VACUUM_WORKER)
    {
      /* reset event logging parameter */
      thread_p->event_stats.trace_log_flush_time = 0;
    }
#endif /* SERVER_MODE */

  return error_code;
}

/*
 * logpb_flush_pages_direct - flush all pages by itself.
 *
 * return: nothing
 *
 */
void
logpb_flush_pages_direct (THREAD_ENTRY * thread_p)
{
#if defined(CUBRID_DEBUG)
  er_log_debug (ARG_FILE_LINE, "logpb_flush_pages_direct: [%d]flush direct\n", (int) THREAD_ID ());
#endif /* CUBRID_DEBUG */

  assert (LOG_CS_OWN_WRITE_MODE (thread_p));

  logpb_prior_lsa_append_all_list (thread_p);
  (void) logpb_flush_all_append_pages (thread_p);
  log_Stat.direct_flush_count++;
}

/*
 * logpb_flush_pages - FLUSH LOG APPEND PAGES
 *
 * return: nothing
 *
 *   flush_lsa(in):
 *
 * NOTE:There are 4 cases to commit.
 *              ASYNC | GROUP COMMIT
 *                X           X         : normal commit, wakeup LFT and wait
 *                X           O         : group commit, wait
 *                O           X         : async commit, wakeup LFT and return
 *                O           O         : async & group commit, just return
 */
void
logpb_flush_pages (THREAD_ENTRY * thread_p, LOG_LSA * flush_lsa)
{
#if !defined(SERVER_MODE)
  LOG_CS_ENTER (thread_p);
  logpb_flush_pages_direct (thread_p);
  LOG_CS_EXIT (thread_p);
#else /* SERVER_MODE */
  int rv;
  struct timeval start_time = { 0, 0 };
  struct timeval tmp_timeval = { 0, 0 };
  struct timespec to = { 0, 0 };
  int max_wait_time_in_msec = 1000;
  bool need_wakeup_LFT, need_wait;
  bool async_commit, group_commit;
  LOG_LSA nxio_lsa;
  LOG_GROUP_COMMIT_INFO *group_commit_info = &log_Gl.group_commit_info;

  assert (flush_lsa != NULL && !LSA_ISNULL (flush_lsa));

  if (!BO_IS_SERVER_RESTARTED () || flush_lsa == NULL || LSA_ISNULL (flush_lsa))
    {
      LOG_CS_ENTER (thread_p);
      logpb_flush_pages_direct (thread_p);
      LOG_CS_EXIT (thread_p);

      return;
    }
  assert (!LOG_CS_OWN_WRITE_MODE (thread_p));

  if (!log_is_log_flush_daemon_available ())
    {
      LOG_CS_ENTER (thread_p);
      logpb_flush_pages_direct (thread_p);
      LOG_CS_EXIT (thread_p);

      return;
    }

  async_commit = prm_get_bool_value (PRM_ID_LOG_ASYNC_COMMIT);
  group_commit = LOG_IS_GROUP_COMMIT_ACTIVE ();

  if (async_commit == false)
    {
      need_wait = true;
      if (group_commit == false)
	{
	  /* Default case: synchorous & non-group commit */
	  need_wakeup_LFT = true;
	}
      else
	{
	  /* synchronous & group commit */
	  need_wakeup_LFT = false;
	  log_Stat.gc_commit_request_count++;
	}
    }
  else
    {
      need_wait = false;
      log_Stat.async_commit_request_count++;

      if (group_commit == false)
	{
	  /* asynchorous & non-group commit */
	  need_wakeup_LFT = true;
	}
      else
	{
	  /* asynchorous & group commit */
	  need_wakeup_LFT = false;
	  log_Stat.gc_commit_request_count++;
	}
    }

  if (need_wakeup_LFT == true && need_wait == false)
    {
      log_wakeup_log_flush_daemon ();
    }
  else if (need_wait == true)
    {
      nxio_lsa = log_Gl.append.get_nxio_lsa ();

      if (need_wakeup_LFT == false && pgbuf_has_perm_pages_fixed (thread_p))
	{
	  need_wakeup_LFT = true;
	}

      while (LSA_LT (&nxio_lsa, flush_lsa))
	{
	  gettimeofday (&start_time, NULL);
	  (void) timeval_add_msec (&tmp_timeval, &start_time, max_wait_time_in_msec);
	  (void) timeval_to_timespec (&to, &tmp_timeval);

	  rv = pthread_mutex_lock (&group_commit_info->gc_mutex);
	  nxio_lsa = log_Gl.append.get_nxio_lsa ();
	  if (LSA_GE (&nxio_lsa, flush_lsa))
	    {
	      pthread_mutex_unlock (&group_commit_info->gc_mutex);
	      break;
	    }

	  if (need_wakeup_LFT == true)
	    {
	      log_wakeup_log_flush_daemon ();
	    }
	  (void) pthread_cond_timedwait (&group_commit_info->gc_cond, &group_commit_info->gc_mutex, &to);
	  pthread_mutex_unlock (&group_commit_info->gc_mutex);

	  need_wakeup_LFT = true;
	  nxio_lsa = log_Gl.append.get_nxio_lsa ();
	}
    }
#endif /* SERVER_MODE */
}

void
logpb_force_flush_pages (THREAD_ENTRY * thread_p)
{
  LOG_CS_ENTER (thread_p);
  logpb_flush_pages_direct (thread_p);
  LOG_CS_EXIT (thread_p);
}

void
logpb_force_flush_header_and_pages (THREAD_ENTRY * thread_p)
{
  LOG_CS_ENTER (thread_p);
  logpb_flush_pages_direct (thread_p);
  logpb_flush_header (thread_p);
  LOG_CS_EXIT (thread_p);
}

/*
 * logpb_invalid_all_append_pages - Invalidate all append pages
 *
 * return: nothing
 *
 * NOTE:Invalidate and free all append pages. Before invalidating the
 *              pages if their are dirty, they are flushed.
 */
void
logpb_invalid_all_append_pages (THREAD_ENTRY * thread_p)
{
  LOG_FLUSH_INFO *flush_info = &log_Gl.flush_info;
#if defined(SERVER_MODE)
  int rv;
#endif /* SERVER_MODE */

  assert (LOG_CS_OWN_WRITE_MODE (thread_p));

  logpb_log ("called logpb_invalid_all_append_pages\n");

  if (log_Gl.append.log_pgptr != NULL)
    {
      /*
       * Somehow we already have an append page, flush all current append page
       * and start form scratch
       */
      logpb_flush_pages_direct (thread_p);
      log_Gl.append.log_pgptr = NULL;
    }

  rv = pthread_mutex_lock (&flush_info->flush_mutex);

  flush_info->num_toflush = 0;
  flush_info->toflush[flush_info->num_toflush] = NULL;

  pthread_mutex_unlock (&flush_info->flush_mutex);
}

/*
 * logpb_flush_log_for_wal - Flush log if needed
 *
 * return: nothing
 *
 *   lsa_ptr(in): Force all log records up to this lsa
 *
 * NOTE:Flush the log up to given log sequence address according to the WAL rule.
 *              The page buffer manager must call this function whenever a
 *              page is about to be flushed due to a page replacement.
 */
void
logpb_flush_log_for_wal (THREAD_ENTRY * thread_p, const LOG_LSA * lsa_ptr)
{
  if (logpb_need_wal (lsa_ptr))
    {
      perfmon_inc_stat (thread_p, PSTAT_LOG_NUM_WALS);

      LOG_CS_ENTER (thread_p);
      if (logpb_need_wal (lsa_ptr))
	{
	  logpb_flush_pages_direct (thread_p);
	}
      else
	{
	  /* was flushed in the meantime */
	}
      LOG_CS_EXIT (thread_p);

      assert (LSA_ISNULL (lsa_ptr) || !logpb_need_wal (lsa_ptr));

#if defined(CUBRID_DEBUG)
      if (logpb_need_wal (lsa_ptr) && !LSA_EQ (&log_Gl.rcv_phase_lsa, lsa_ptr))
	{
	  er_log_debug (ARG_FILE_LINE, "log_wal: SYSTEM ERROR.. DUMP LOG BUFFER\n");
	  logpb_dump (thread_p, stdout);
	}
#endif /* CUBRID_DEBUG */
    }
}

/*
 *
 *       	   FUNCTIONS RELATED TO DATA APPEND
 *
 */

/*
 * logpb_start_append - Start appending a new log record
 *
 * return: nothing
 *
 *   header(in):
 *
 * NOTE:
 */
static void
logpb_start_append (THREAD_ENTRY * thread_p, LOG_RECORD_HEADER * header)
{
  LOG_RECORD_HEADER *log_rec;	/* Log record */

  assert (LOG_CS_OWN_WRITE_MODE (thread_p));

  /* Record number of append log record in statistics */
  perfmon_inc_stat (thread_p, PSTAT_LOG_NUM_APPENDRECS);

  /* Does the new log record fit in this page ? */
  LOG_APPEND_ADVANCE_WHEN_DOESNOT_FIT (thread_p, sizeof (LOG_RECORD_HEADER));

  if (!LSA_EQ (&header->back_lsa, &log_Gl.append.prev_lsa))
    {
      logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "logpb_start_append");
    }

  assert (log_Gl.append.log_pgptr != NULL);

  if (log_Gl.append.appending_page_tde_encrypted)
    {
      if (!LOG_IS_PAGE_TDE_ENCRYPTED (log_Gl.append.log_pgptr))
	{
	  TDE_ALGORITHM tde_algo = (TDE_ALGORITHM) prm_get_integer_value (PRM_ID_TDE_DEFAULT_ALGORITHM);
	  logpb_set_tde_algorithm (thread_p, log_Gl.append.log_pgptr, tde_algo);
	  logpb_set_dirty (thread_p, log_Gl.append.log_pgptr);
	}
    }

  logpb_log ("logpb_start_append: start append on the page (%lld), tde_algorithm = %s\n",
	     (long long int) log_Gl.append.log_pgptr->hdr.logical_pageid,
	     tde_get_algorithm_name (logpb_get_tde_algorithm (log_Gl.append.log_pgptr)));

  log_rec = (LOG_RECORD_HEADER *) LOG_APPEND_PTR ();
  *log_rec = *header;

  /*
   * If the header of the append page does not have the offset set to the
   * first log record, this is the first log record in the page, set to it.
   */

  if (log_Gl.append.log_pgptr->hdr.offset == NULL_OFFSET)
    {
      log_Gl.append.log_pgptr->hdr.offset = (PGLENGTH) log_Gl.hdr.append_lsa.offset;
    }

  if (log_rec->type == LOG_END_OF_LOG)
    {
      /* this comes from logpb_flush_all_append_pages */
      assert (log_Pb.partial_append.status == LOGPB_APPENDREC_SUCCESS
	      || log_Pb.partial_append.status == LOGPB_APPENDREC_PARTIAL_ENDED);

      LSA_COPY (&log_Gl.hdr.eof_lsa, &log_Gl.hdr.append_lsa);

      logpb_set_dirty (thread_p, log_Gl.append.log_pgptr);
    }
  else
    {
      /* no record should be in progress now */
      assert (log_Pb.partial_append.status == LOGPB_APPENDREC_SUCCESS);

      LSA_COPY (&log_Gl.append.prev_lsa, &log_Gl.hdr.append_lsa);

      /*
       * Set the page dirty, increase and align the append offset
       */
      LOG_APPEND_SETDIRTY_ADD_ALIGN (thread_p, sizeof (LOG_RECORD_HEADER));

      log_Pb.partial_append.status = LOGPB_APPENDREC_IN_PROGRESS;
    }
}

/*
 * logpb_append_data - Append data
 *
 * return: nothing
 *
 *   length(in): Length of data to append
 *   data(in):  Data to append
 *
 * NOTE:Append data as part of current log record.
 */
static void
logpb_append_data (THREAD_ENTRY * thread_p, int length, const char *data)
{
  int copy_length;		/* Amount of contiguos data that can be copied */
  char *ptr;			/* Pointer for copy data into log append buffer */
  char *last_ptr;		/* Pointer to last portion available to copy into log append buffer */

  assert (LOG_CS_OWN_WRITE_MODE (thread_p));

  if (length == 0 || data == NULL)
    {
      return;
    }

  /*
   * Align if needed,
   * don't set it dirty since this function has not updated
   */
  LOG_APPEND_ALIGN (thread_p, LOG_DONT_SET_DIRTY);

  ptr = LOG_APPEND_PTR ();
  last_ptr = LOG_LAST_APPEND_PTR ();

  /* Does data fit completely in current page ? */
  if ((ptr + length) >= last_ptr)
    {
      while (length > 0)
	{
	  if (ptr >= last_ptr)
	    {
	      /*
	       * Get next page and set the current one dirty
	       */
	      logpb_next_append_page (thread_p, LOG_SET_DIRTY);
	      ptr = LOG_APPEND_PTR ();
	      last_ptr = LOG_LAST_APPEND_PTR ();
	    }
	  /* Find the amount of contiguous data that can be copied */
	  if (ptr + length >= last_ptr)
	    {
	      copy_length = CAST_BUFLEN (last_ptr - ptr);
	    }
	  else
	    {
	      copy_length = length;
	    }
	  memcpy (ptr, data, copy_length);
	  ptr += copy_length;
	  data += copy_length;
	  length -= copy_length;
	  log_Gl.hdr.append_lsa.offset += copy_length;
	}
    }
  else
    {
      memcpy (ptr, data, length);
      log_Gl.hdr.append_lsa.offset += length;
    }

  /*
   * Align the data for future appends.
   * Indicate that modifications were done
   */
  LOG_APPEND_ALIGN (thread_p, LOG_SET_DIRTY);
}

/*
 * logpb_append_crumbs - Append crumbs of data
 *
 * return: nothing
 *
 *   num_crumbs(in): Number of crumbs
 *   crumbs(in): The crumbs (length + data)
 *
 * NOTE: Append crumbs of data by gluing them. After this the log manager will lose track of what was glued.
 */
static void
logpb_append_crumbs (THREAD_ENTRY * thread_p, int num_crumbs, const LOG_CRUMB * crumbs)
{
  const char *data;		/* Data to copy */
  char *ptr;			/* Pointer for copy data into log append buffer */
  char *last_ptr;		/* Pointer to last portion available to copy into log append buffer */
  int copy_length;		/* Amount of contiguos data that can be copied */
  int length;
  int i;

  assert (LOG_CS_OWN_WRITE_MODE (thread_p));

  if (num_crumbs == 0)
    {
      return;
    }

  /*
   * Align if needed,
   * don't set it dirty since this function has not updated
   */
  LOG_APPEND_ALIGN (thread_p, LOG_DONT_SET_DIRTY);

  ptr = LOG_APPEND_PTR ();
  last_ptr = LOG_LAST_APPEND_PTR ();

  for (i = 0; i < num_crumbs; i++)
    {
      length = crumbs[i].length;
      data = (char *) crumbs[i].data;

      /* Does data fit completely in current page ? */
      if ((ptr + length) >= last_ptr)
	while (length > 0)
	  {
	    if (ptr >= last_ptr)
	      {
		/*
		 * Get next page and set the current one dirty
		 */
		logpb_next_append_page (thread_p, LOG_SET_DIRTY);
		ptr = LOG_APPEND_PTR ();
		last_ptr = LOG_LAST_APPEND_PTR ();
	      }
	    /* Find the amount of contiguous data that can be copied */
	    if ((ptr + length) >= last_ptr)
	      {
		copy_length = CAST_BUFLEN (last_ptr - ptr);
	      }
	    else
	      {
		copy_length = length;
	      }
	    memcpy (ptr, data, copy_length);
	    ptr += copy_length;
	    data += copy_length;
	    length -= copy_length;
	    log_Gl.hdr.append_lsa.offset += copy_length;
	  }
      else
	{
	  memcpy (ptr, data, length);
	  ptr += length;
	  log_Gl.hdr.append_lsa.offset += length;
	}
    }

  /*
   * Align the data for future appends.
   * Indicate that modifications were done
   */
  LOG_APPEND_ALIGN (thread_p, LOG_SET_DIRTY);
}

/*
 * logpb_end_append - Finish appending a log record
 *
 * return: nothing
 *
 *   flush(in): Is it a requirement to flush the log ?
 *   force_flush(in):
 *
 * NOTE:  Finish appending a log record. If the log record was appended
 *              in several log buffers, these buffers are flushed and freed.
 *              Only one append buffer will remain pin (fetched) in memory.
 *              If the log record was appended in only one buffer, the buffer
 *              is not flushed unless the caller requested flushing (e.g.,
 *              for a log_commit record).
 */
static void
logpb_end_append (THREAD_ENTRY * thread_p, LOG_RECORD_HEADER * header)
{
  assert (LOG_CS_OWN_WRITE_MODE (thread_p));

  LOG_APPEND_ALIGN (thread_p, LOG_DONT_SET_DIRTY);
  LOG_APPEND_ADVANCE_WHEN_DOESNOT_FIT (thread_p, sizeof (LOG_RECORD_HEADER));

  /*
   * Find the log_rec portion of the append record, it may not be in the
   * current append buffer since it can be stored in several buffers. Then,
   * make the log_rec point to next future append record, unless it is
   * the special record type used for archives created during backups
   * that cannot have a forward lsa and must waste the remaining space
   * on the current page.
   */
  assert (LSA_EQ (&header->forw_lsa, &log_Gl.hdr.append_lsa));

  if (!LSA_EQ (&log_Gl.append.prev_lsa, &log_Gl.hdr.append_lsa))
    {
      logpb_set_dirty (thread_p, log_Gl.append.log_pgptr);
    }

  if (log_Pb.partial_append.status == LOGPB_APPENDREC_IN_PROGRESS)
    {
      /* success, fall through */
    }
  else if (log_Pb.partial_append.status == LOGPB_APPENDREC_PARTIAL_FLUSHED_END_OF_LOG)
    {
      /* we need to flush the correct version now */
      log_Pb.partial_append.status = LOGPB_APPENDREC_PARTIAL_ENDED;
      logpb_flush_all_append_pages (thread_p);
      assert (log_Pb.partial_append.status == LOGPB_APPENDREC_PARTIAL_FLUSHED_ORIGINAL);
    }
  else
    {
      /* invalid state */
      assert_release (false);
    }
  log_Pb.partial_append.status = LOGPB_APPENDREC_SUCCESS;
}

/*
 *
 *       	   FUNCTIONS RELATED TO LOG INFORMATION FILE
 *
 */

/*
 * logpb_create_log_info - Create a log information file
 *
 * return: nothing
 *
 *   logname_info(in): Name of the log information file
 *   db_fullname(in): Name of the database or NULL (defualt to current one)
 *
 * NOTE: Creates a log information file. This file is used as a help
 *              for the DBA of what things has been archived and what archive
 *              logs are not needed during normal restart recovery (i.e.,
 *              other than media crash).
 */
void
logpb_create_log_info (const char *logname_info, const char *db_fullname)
{
  FILE *fp;			/* Pointer to file */
  const char *catmsg;
  const char *db_name = db_fullname;
  int error_code = NO_ERROR;

  /* Create the information file */
  fp = fopen (logname_info, "w");
  if (fp != NULL)
    {
      fclose (fp);
      catmsg = msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_LOG, MSGCAT_LOG_LOGINFO_COMMENT);
      if (db_name == NULL)
	{
	  db_name = log_Db_fullname;
	}
      if (catmsg == NULL)
	{
	  catmsg = "COMMENT: %s for database %s\n";
	}
      error_code = log_dump_log_info (logname_info, false, catmsg, CUBRID_MAGIC_LOG_INFO, db_name);
      if (error_code != NO_ERROR)
	{
	  return;
	}

      (void) logpb_add_volume (db_fullname, LOG_DBLOG_INFO_VOLID, logname_info, DISK_UNKNOWN_PURPOSE);
    }
}


/*
 * logpb_get_guess_archive_num - Guess archive number
 *
 * return: arvnum or -1
 *
 *   pageid(in): Desired page
 *
 * NOTE: Guess the archive number where the desired page is archived by searching the log information file.
 */
static int
logpb_get_guess_archive_num (THREAD_ENTRY * thread_p, LOG_PAGEID pageid)
{
  FILE *fp;
  char line[LOG_MAX_LOGINFO_LINE];
  int arv_num = -1;
  int last_arvnum = -1;
  int next_arvnum;
  bool isfound = false;
  LOG_PAGEID from_pageid;
  LOG_PAGEID to_pageid;
  long long int f, t;

  assert (LOG_CS_OWN (thread_p));

  arv_num = logpb_get_archive_num_from_info_table (thread_p, pageid);

  if (arv_num >= 0)
    {
      return arv_num;
    }

  /*
   * Guess by looking into the log information file. This is just a guess
   */
  fp = fopen (log_Name_info, "r");
  if (fp != NULL)
    {
      while (fgets (line, LOG_MAX_LOGINFO_LINE, fp) != NULL)
	{
	  if (strstr (line + TIME_SIZE_OF_DUMP_LOG_INFO,
		      msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_LOG, MSGCAT_LOG_LOGINFO_KEYWORD_ARCHIVE))
	      == line + TIME_SIZE_OF_DUMP_LOG_INFO)
	    {
	      /* A candidate for a guess */
	      if (sscanf (line + TIME_SIZE_OF_DUMP_LOG_INFO, "%*s %d %*s %lld %lld", &next_arvnum, &f, &t) == 3)
		{
		  from_pageid = f;
		  to_pageid = t;

		  last_arvnum = next_arvnum;

		  if (pageid < from_pageid)
		    {
		      /*
		       * keep looking.
		       * There is likely a hole in the archive process due to media
		       * crashes off or the log information contains some missing
		       * entries.
		       */
		      continue;
		    }

		  arv_num = next_arvnum;

		  if (pageid >= from_pageid && pageid <= to_pageid)
		    {
		      /* Found the page in this archive */
		      isfound = true;
		      break;
		    }
		}
	    }
	}
      fclose (fp);
    }

  if (arv_num == -1)
    {
      /*
       * If I have a log active, use it to find out a better archive number
       * for initial search
       */
      if (log_Gl.append.vdes != NULL_VOLDES)
	{
	  arv_num = (int) (pageid / LOGPB_ACTIVE_NPAGES);
	}
      else
	{
	  /*
	   * We do not have a clue what it is available. Don't have log active
	   * and likely we did not have backups.
	   * Must trace for available archive volumes
	   */
	  arv_num = 0;
	}
    }
  else if (isfound == false && last_arvnum == arv_num && log_Gl.append.vdes != NULL_VOLDES)
    {
      /*
       * The log archive was chopped somehow.
       */
      arv_num = log_Gl.hdr.nxarv_num - 1;
    }

  /* Insure that we never pick one larger than the next one to be created */
  if (arv_num >= log_Gl.hdr.nxarv_num)
    {
      arv_num = log_Gl.hdr.nxarv_num - 1;
    }

  return arv_num;
}

/*
 * logpb_find_volume_info_exist - Find if volume information exists ?
 *
 * return:
 *
 * NOTE: Find if volume information exist.
 */
bool
logpb_find_volume_info_exist (void)
{
  return fileio_is_volume_exist (log_Name_volinfo);
}

/*
 * logpb_create_volume_info - Create the volume information and add first volume
 *
 * return: NO_ERROR or error code
 *
 *   db_fullname(in): Name of the database or NULL (defualt to current one)
 *
 * NOTE: Create the volume information and add the first volume.
 */
int
logpb_create_volume_info (const char *db_fullname)
{
  char vol_fullname[PATH_MAX];
  char *volinfo_fullname;
  FILE *volinfo_fp = NULL;

  if (db_fullname != NULL)
    {
      fileio_make_volume_info_name (vol_fullname, db_fullname);
      volinfo_fullname = vol_fullname;
    }
  else
    {
      volinfo_fullname = log_Name_volinfo;
    }

  volinfo_fp = fopen (volinfo_fullname, "w");
  if (volinfo_fp == NULL)
    {
      /* Unable to create the database volume information */
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_BO_CANNOT_CREATE_VOL, 2, volinfo_fullname, db_fullname);
      return ER_BO_CANNOT_CREATE_VOL;
    }
  /*
   * Write information about:
   * the active log and the first volume of the database
   * in the volume information file
   */
  fprintf (volinfo_fp, "%4d %s\n", LOG_DBVOLINFO_VOLID, volinfo_fullname);

  fflush (volinfo_fp);
  fclose (volinfo_fp);

  return NO_ERROR;
}

/*
 * logpb_recreate_volume_info - Recreate the database volume information
 *
 * return: NO_ERROR if all OK, ER_ status otherwise
 *
 * NOTE: Recreate the database volume information from the internal information that is stored in each volume.
 */
int
logpb_recreate_volume_info (THREAD_ENTRY * thread_p)
{
  VOLID volid = LOG_DBFIRST_VOLID;	/* Current volume identifier */
  VOLID next_volid = LOG_DBFIRST_VOLID;	/* Next volume identifier */
  char next_vol_fullname[PATH_MAX];	/* Next volume name */
  int error_code = NO_ERROR;

  error_code = logpb_create_volume_info (NULL);
  if (error_code != NO_ERROR)
    {
      goto error;
    }
  if (logpb_add_volume (NULL, LOG_DBLOG_INFO_VOLID, log_Name_info, DISK_UNKNOWN_PURPOSE) != LOG_DBLOG_INFO_VOLID)
    {
      error_code = ER_FAILED;
      goto error;
    }
  if (logpb_add_volume (NULL, LOG_DBLOG_BKUPINFO_VOLID, log_Name_bkupinfo, DISK_UNKNOWN_PURPOSE) !=
      LOG_DBLOG_BKUPINFO_VOLID)
    {
      error_code = ER_FAILED;
      goto error;
    }
  if (logpb_add_volume (NULL, LOG_DBLOG_ACTIVE_VOLID, log_Name_active, DISK_UNKNOWN_PURPOSE) != LOG_DBLOG_ACTIVE_VOLID)
    {
      error_code = ER_FAILED;
      goto error;
    }

  /* First the primary volume, then the rest of the volumes */

  strcpy (next_vol_fullname, log_Db_fullname);

  do
    {
      if (logpb_add_volume (NULL, volid, next_vol_fullname, DB_PERMANENT_DATA_PURPOSE) != volid)
	{
	  error_code = ER_FAILED;
	  goto error;
	}

      if (disk_get_link (thread_p, volid, &next_volid, next_vol_fullname) == NULL)
	{
	  error_code = ER_FAILED;
	  goto error;
	}

      volid = next_volid;
    }
  while (volid != NULL_VOLID);

  return error_code;

  /* ****** */
error:
  (void) remove (log_Name_volinfo);
  return error_code;
}

/*
 * logpb_add_volume - Add a new volume entry to the volume information
 *
 * return: new_volid or NULL_VOLID
 *
 *   db_fullname(in):
 *   new_volid(in): New volume identifier
 *   new_volfullname(in): New volume name
 *   new_volpurpose(in): Purpose of new volume
 *
 * NOTE: Add a new entry to the volume information
 */
/* todo: remove purpose */
VOLID
logpb_add_volume (const char *db_fullname, VOLID new_volid, const char *new_volfullname, DISK_VOLPURPOSE new_volpurpose)
{
  if (new_volpurpose != DB_TEMPORARY_DATA_PURPOSE)
    {
      char vol_fullname[PATH_MAX];
      char *volinfo_fullname;
      FILE *volinfo_fp = NULL;

      if (db_fullname != NULL)
	{
	  fileio_make_volume_info_name (vol_fullname, db_fullname);
	  volinfo_fullname = vol_fullname;
	}
      else
	{
	  volinfo_fullname = log_Name_volinfo;
	}

      volinfo_fp = fopen (volinfo_fullname, "a");
      if (volinfo_fp != NULL)
	{
	  /* Write information about this volume in the volume information file */
	  fprintf (volinfo_fp, "%4d %s\n", new_volid, new_volfullname);
	  fflush (volinfo_fp);
	  fclose (volinfo_fp);

	  return new_volid;
	}
      else
	{
	  return NULL_VOLID;
	}
    }

  return new_volid;
}

/*
 * logpb_scan_volume_info - Scan the volume information entries
 *
 * return: number of entries or -1 in case of error.
 *
 *   db_fullname(in):
 *   ignore_volid(in): Don't call function with this volume
 *   start_volid(in): Scan should start at this point.
 *   fun(in): Function to be called on each entry
 *   args(in): Additional arguments to be passed to function
 *
 * NOTE: Scan the volume information entries calling the given function on each entry.
 */
int
logpb_scan_volume_info (THREAD_ENTRY * thread_p, const char *db_fullname, VOLID ignore_volid, VOLID start_volid,
			int (*fun) (THREAD_ENTRY * thread_p, VOLID xvolid, const char *vlabel, void *args), void *args)
{
  char xxvolinfo_fullname[PATH_MAX];
  char *volinfo_fullname;
  FILE *volinfo_fp = NULL;	/* Pointer to new volinfo */
  char vol_fullname[PATH_MAX];	/* Next volume name */
  VOLID volid = LOG_DBFIRST_VOLID - 1;	/* Next volume identifier */
  int read_int_volid;
  VOLID num_vols = 0;
  bool start_scan = false;
  char format_string[64];

  if (db_fullname != NULL)
    {
      fileio_make_volume_info_name (xxvolinfo_fullname, db_fullname);
      volinfo_fullname = xxvolinfo_fullname;
    }
  else
    {
      volinfo_fullname = log_Name_volinfo;
    }

  volinfo_fp = fopen (volinfo_fullname, "r");
  if (volinfo_fp == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_BO_CANNOT_FINE_VOLINFO, 1, volinfo_fullname);
      return -1;
    }

  sprintf (format_string, "%%d %%%ds", PATH_MAX - 1);
  while (true)
    {
      if (fscanf (volinfo_fp, format_string, &read_int_volid, vol_fullname) != 2)
	{
	  break;
	}

      if ((volid + 1) != NULL_VOLID && (volid + 1) > (VOLID) read_int_volid && num_vols != 0)
	{
	  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_BO_UNSORTED_VOLINFO, 4, volinfo_fullname, num_vols,
		  read_int_volid, vol_fullname);
	  num_vols = -1;
	  break;
	}
      volid = (VOLID) read_int_volid;

      if (volid == NULL_VOLID)
	{
	  continue;
	}

      if (start_scan == false)
	{
	  if (start_volid == read_int_volid)
	    {
	      start_scan = true;
	    }
	  else
	    {
	      continue;
	    }
	}

      if (volid != ignore_volid)
	{
	  if (((*fun) (thread_p, volid, vol_fullname, args)) != NO_ERROR)
	    {
	      num_vols = -1;
	      break;
	    }

	  num_vols++;
	}
    }

  fclose (volinfo_fp);

  return num_vols;
}

/*
 *
 *       	   FUNCTIONS RELATED TO LOG ARCHIVES
 *
 */

/*
 * logpb_to_physical_pageid - Find physical page identifier of given logic page
 *
 * return: phy page identifier
 *
 *   logical_pageid(in): logical_pageid: Logical log page
 *
 * NOTE: Returns the physical page identifier associated with given logical page.
 */
LOG_PHY_PAGEID
logpb_to_physical_pageid (LOG_PAGEID logical_pageid)
{
  LOG_PHY_PAGEID phy_pageid;

  if (logical_pageid == LOGPB_HEADER_PAGE_ID)
    {
      phy_pageid = LOGPB_PHYSICAL_HEADER_PAGE_ID;
    }
  else
    {
      LOG_PAGEID tmp_pageid;

      tmp_pageid = logical_pageid - LOGPB_FIRST_ACTIVE_PAGE_ID;
      if (tmp_pageid >= LOGPB_ACTIVE_NPAGES)
	{
	  tmp_pageid %= LOGPB_ACTIVE_NPAGES;
	}
      else if (tmp_pageid < 0)
	{
	  tmp_pageid = LOGPB_ACTIVE_NPAGES - ((-tmp_pageid) % LOGPB_ACTIVE_NPAGES);
	}

      tmp_pageid++;
      if (tmp_pageid > LOGPB_ACTIVE_NPAGES)
	{
	  tmp_pageid %= LOGPB_ACTIVE_NPAGES;
	}

      assert (tmp_pageid <= PAGEID_MAX);
      phy_pageid = (LOG_PHY_PAGEID) tmp_pageid;
    }

  return phy_pageid;
}

/*
 * logpb_is_page_in_archive - Is the given page an archive page ?
 *
 * return:
 *
 *   pageid(in): Log page identifier
 *
 * NOTE:Find if given page is an archive page identifier.
 */
bool
logpb_is_page_in_archive (LOG_PAGEID pageid)
{
  return LOGPB_IS_ARCHIVE_PAGE (pageid);
}

/*
 * logpb_is_smallest_lsa_in_archive - IS THE SMALLEST ACTIVE OF THE LOG ARCHIVE ?
 *
 * return:
 *
 * NOTE: Returns true if the smallest active LSA is located in an archive log.
 */
bool
logpb_is_smallest_lsa_in_archive (THREAD_ENTRY * thread_p)
{
  LOG_LSA lsa;			/* smallest lsa */

  logtb_find_smallest_lsa (thread_p, &lsa);
  return (!LSA_ISNULL (&lsa) && logpb_is_page_in_archive (lsa.pageid));
}

/*
 * logpb_get_archive_number - Archive location of given page
 *
 * return: archive number
 *
 *   pageid(in): The desired logical page
 *
 * NOTE: Find in what archive the page is located or in what archive the page should have been located.
 */
int
logpb_get_archive_number (THREAD_ENTRY * thread_p, LOG_PAGEID pageid)
{
  int arv_num = 0;

  if (logpb_fetch_from_archive (thread_p, pageid, NULL, &arv_num, NULL, false) == NULL)
    {
      return -1;
    }

  if (arv_num < 0)
    {
      arv_num = 0;
    }

  return arv_num;
}

/*
 * logpb_set_unavailable_archive - Cache that given archive is unavailable
 *
 * return: nothing
 *
 *   arv_num(in): Log archive number
 *
 * NOTE: Record that give archive is unavialble.
 */
static void
logpb_set_unavailable_archive (THREAD_ENTRY * thread_p, int arv_num)
{
  int *ptr;
  int size;

  assert (LOG_ARCHIVE_CS_OWN_WRITE_MODE (thread_p));

  if (log_Gl.archive.unav_archives == NULL)
    {
      size = sizeof (*log_Gl.archive.unav_archives) * 10;
      ptr = (int *) malloc (size);
      if (ptr == NULL)
	{
	  return;
	}
      log_Gl.archive.max_unav = 10;
      log_Gl.archive.next_unav = 0;
      log_Gl.archive.unav_archives = ptr;
    }
  else
    {
      if ((log_Gl.archive.next_unav + 1) >= log_Gl.archive.max_unav)
	{
	  size = (sizeof (*log_Gl.archive.unav_archives) * (log_Gl.archive.max_unav + 10));
	  ptr = (int *) realloc (log_Gl.archive.unav_archives, size);
	  if (ptr == NULL)
	    {
	      return;
	    }
	  log_Gl.archive.max_unav += 10;
	  log_Gl.archive.unav_archives = ptr;
	}
    }

  log_Gl.archive.unav_archives[log_Gl.archive.next_unav++] = arv_num;
}

/*
 * logpb_dismount_log_archive - dismount archive log
 *
 * return: nothing
 *
 * It dismounts and resets log_Gl.archive.vdes
 */
static void
logpb_dismount_log_archive (THREAD_ENTRY * thread_p)
{
  LOG_ARCHIVE_CS_ENTER (thread_p);

  if (log_Gl.archive.vdes != NULL_VOLDES)
    {
      fileio_dismount (thread_p, log_Gl.archive.vdes);
      log_Gl.archive.vdes = NULL_VOLDES;
    }

  LOG_ARCHIVE_CS_EXIT (thread_p);
}

/*
 * logpb_decache_archive_info - Decache any archive log memory information
 *
 * return: nothing
 *
 * NOTE: Decache any archive log memory information.
 */
void
logpb_decache_archive_info (THREAD_ENTRY * thread_p)
{
  LOG_ARCHIVE_CS_ENTER (thread_p);

  if (log_Gl.archive.vdes != NULL_VOLDES)
    {
      logpb_dismount_log_archive (thread_p);
    }

  if (log_Gl.archive.unav_archives != NULL)
    {
      free_and_init (log_Gl.archive.unav_archives);
      log_Gl.archive.max_unav = 0;
      log_Gl.archive.next_unav = 0;
    }

  LOG_ARCHIVE_CS_EXIT (thread_p);
}

/*
 * log_isarchive_available - Is given archive available ?
 *
 * return: true/false
 *        true: means that the archive may be available.
 *       false: it is known that archive is not available.
 *
 *   arv_num(in): Log archive number
 *
 * NOTE:Find if the current archive is available.
 */
static bool
logpb_is_archive_available (THREAD_ENTRY * thread_p, int arv_num)
{
  int i;

  assert (LOG_CS_OWN (thread_p));
  assert (LOG_ARCHIVE_CS_OWN_WRITE_MODE (thread_p));

  if (arv_num >= log_Gl.hdr.nxarv_num || arv_num < 0)
    {
      return false;
    }

  if (log_Gl.archive.unav_archives != NULL)
    {
      for (i = 0; i < log_Gl.archive.next_unav; i++)
	{
	  if (log_Gl.archive.unav_archives[i] == arv_num)
	    {
	      return false;
	    }
	}
    }

  return true;
}

/*
 * log_fetch_from_archive - Fetch a log page from the log archives
 *
 * return: log_pgptr or NULL (in case of error)
 *
 *   pageid(in): The desired logical page
 *   log_pgptr(in): Place to return the log page
 *   arv_num(in): Set to archive number where page was found or where page
 *                 should have been found.
 *
 * NOTE: Fetch a log page from archive logs.
 */
LOG_PAGE *
logpb_fetch_from_archive (THREAD_ENTRY * thread_p, LOG_PAGEID pageid, LOG_PAGE * log_pgptr,
			  int *ret_arv_num, LOG_ARV_HEADER * ret_arv_hdr, bool is_fatal)
{
  char hdr_pgbuf[IO_MAX_PAGE_SIZE + MAX_ALIGNMENT], *aligned_hdr_pgbuf;
  char log_pgbuf[IO_MAX_PAGE_SIZE + MAX_ALIGNMENT], *aligned_log_pgbuf;
  LOG_ARV_HEADER *arv_hdr;
  LOG_PAGE *hdr_pgptr;
  LOG_PHY_PAGEID phy_pageid = NULL_PAGEID;
  char arv_name[PATH_MAX];
  const char *tmp_arv_name;
  int arv_num, vdes;
  int direction = 0, retry;
  bool has_guess_arvnum = false, first_time = true;
  int error_code = NO_ERROR;
  char format_string[64];

  assert (LOG_CS_OWN (thread_p));

  logpb_log ("called logpb_fetch_from_archive for pageid = %lld\n", (long long int) pageid);

  LOG_ARCHIVE_CS_ENTER (thread_p);

  aligned_hdr_pgbuf = PTR_ALIGN (hdr_pgbuf, MAX_ALIGNMENT);
  aligned_log_pgbuf = PTR_ALIGN (log_pgbuf, MAX_ALIGNMENT);

#if !defined(NDEBUG)
  if (prm_get_bool_value (PRM_ID_LOG_TRACE_DEBUG))
    {
      fprintf (stdout, "\n **log_fetch_from_archive has been called on pageid = %lld ** \n", (long long int) pageid);
      fflush (stdout);
    }
#endif

  hdr_pgptr = (LOG_PAGE *) aligned_hdr_pgbuf;
  if (log_pgptr == NULL)
    {
      log_pgptr = (LOG_PAGE *) aligned_log_pgbuf;
    }
  if (ret_arv_num == NULL)
    {
      ret_arv_num = &arv_num;
    }

  if (log_Gl.archive.vdes == NULL_VOLDES)
    {
      if (log_Gl.hdr.nxarv_num <= 0)
	{
	  /* We do not have any archives */
	  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_NOTIN_ARCHIVE, 1, pageid);

	  LOG_ARCHIVE_CS_EXIT (thread_p);
	  return NULL;
	}

      /*
       * Guess the archive where that page is stored
       */

      has_guess_arvnum = true;
      *ret_arv_num = logpb_get_guess_archive_num (thread_p, pageid);
      fileio_make_log_archive_name (arv_name, log_Archive_path, log_Prefix, *ret_arv_num);

      error_code = ER_FAILED;
      if (logpb_is_archive_available (thread_p, *ret_arv_num) == true && fileio_is_volume_exist (arv_name) == true)
	{
	  vdes = fileio_mount (thread_p, log_Db_fullname, arv_name, LOG_DBLOG_ARCHIVE_VOLID, false, false);
	  if (vdes != NULL_VOLDES)
	    {
	      if (fileio_read (thread_p, vdes, hdr_pgptr, 0, LOG_PAGESIZE) == NULL)
		{
		  fileio_dismount (thread_p, vdes);
		  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_READ, 3, 0LL, 0LL, arv_name);

		  LOG_ARCHIVE_CS_EXIT (thread_p);
		  return NULL;
		}
	      error_code = NO_ERROR;
	      arv_hdr = (LOG_ARV_HEADER *) hdr_pgptr->area;
	      if (log_Gl.append.vdes != NULL_VOLDES)
		{
		  if (difftime64 ((time_t) arv_hdr->db_creation, (time_t) log_Gl.hdr.db_creation) != 0)
		    {
		      /*
		       * This volume does not belong to the database. For now, assume
		       * that it is not only. Later, we will give this error to user
		       */
		      vdes = NULL_VOLDES;
		      arv_hdr = NULL;
		    }
		}
	    }
	}

      if (error_code != NO_ERROR)
	{
	  /*
	   * The volume is not online. Ask for it later (below). But first try to
	   * make the best guess for the archive number.
	   */
	  vdes = NULL_VOLDES;
	  arv_hdr = NULL;
	}
    }
  else
    {
      vdes = log_Gl.archive.vdes;
      arv_hdr = &log_Gl.archive.hdr;
      *ret_arv_num = arv_hdr->arv_num;
    }

  sprintf (format_string, "%%%ds", PATH_MAX - 1);

  log_Gl.archive.vdes = NULL_VOLDES;
  while (true)
    {
      /* Is the page in current archive log ? */
      if (arv_hdr != NULL && pageid >= arv_hdr->fpageid && pageid <= arv_hdr->fpageid + arv_hdr->npages - 1)
	{
	  /* Find location of logical page in the archive log */
	  phy_pageid = (LOG_PHY_PAGEID) (pageid - arv_hdr->fpageid + 1);

	  /* Record number of reads in statistics */
	  perfmon_inc_stat (thread_p, PSTAT_LOG_NUM_IOREADS);

	  if (fileio_read (thread_p, vdes, log_pgptr, phy_pageid, LOG_PAGESIZE) == NULL)
	    {
	      /* Error reading archive page */
	      tmp_arv_name = fileio_get_volume_label_by_fd (vdes, PEEK);
	      fileio_dismount (thread_p, vdes);
	      log_Gl.archive.vdes = NULL_VOLDES;
	      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_READ, 3, pageid, phy_pageid, tmp_arv_name);

	      LOG_ARCHIVE_CS_EXIT (thread_p);
	      return NULL;
	    }

	  TDE_ALGORITHM tde_algo = logpb_get_tde_algorithm (log_pgptr);
	  if (tde_algo != TDE_ALGORITHM_NONE)
	    {
	      if (tde_decrypt_log_page (log_pgptr, tde_algo, log_pgptr) != NO_ERROR)
		{
		  ASSERT_ERROR ();
		  LOG_ARCHIVE_CS_EXIT (thread_p);
		  return NULL;
		}
	    }

	  /* Cast the archive information. May be used again */
	  if (arv_hdr != &log_Gl.archive.hdr)
	    {
	      log_Gl.archive.hdr = *arv_hdr;
	    }
	  log_Gl.archive.vdes = vdes;
	  break;
	}
      else
	{
	  /* If any archive dismount it */
	  if (vdes != NULL_VOLDES)
	    {
	      fileio_dismount (thread_p, vdes);
	      vdes = NULL_VOLDES;
	    }

	  if (has_guess_arvnum == false)
	    {
	      has_guess_arvnum = true;
	      retry = logpb_get_guess_archive_num (thread_p, pageid);
	      if (retry != *ret_arv_num)
		{
		  *ret_arv_num = retry;
		}
	    }
	  else
	    {
	      if (direction == 0)
		{
		  /*
		   * Define the direction by looking for desired page
		   */
		  if (arv_hdr != NULL)
		    {
		      if (pageid < arv_hdr->fpageid)
			{
			  /* Try older archives */
			  direction = -1;
			}
		      else
			{
			  /* Try newer archives */
			  direction = 1;
			}
		    }
		  else
		    {
		      if (first_time != true)
			{
			  if (log_Gl.append.vdes == NULL_VOLDES)
			    {
			      direction = 1;
			    }
			  else
			    {
			      /*
			       * Start looking from the last archive.
			       * Optimized for UNDO.. This is not so bad since this branch
			       * will be reached only when the guess archive is not
			       * available.
			       */
			      *ret_arv_num = log_Gl.hdr.nxarv_num;
			      direction = -1;
			    }
			}
		    }
		}

	      if (arv_hdr != NULL)
		{
		  if (direction == -1)
		    {
		      /*
		       * Try an older archive.
		       * The page that I am looking MUST be smaller than the first
		       * page in current archive
		       */
		      if (pageid < arv_hdr->fpageid)
			{
			  *ret_arv_num -= 1;
			}
		      else
			{
			  *ret_arv_num = -1;
			}
		    }
		  else
		    {
		      /* Try a newer archive. The page that I am looking MUST be larger than the last page in current
		       * archive */
		      if (pageid > arv_hdr->fpageid + arv_hdr->npages - 1)
			{
			  *ret_arv_num += 1;
			}
		      else
			{
			  *ret_arv_num = log_Gl.hdr.nxarv_num;
			}
		    }
		}
	      else
		{
		  /*
		   * The archive number is not increased the first time in the loop,
		   * so we can ask for it when it is not available.
		   */
		  if (first_time != true)
		    {
		      /*
		       * If we do not have the log active, we don't really know how to
		       * continue, we could be looping forever.
		       */
		      if (log_Gl.append.vdes == NULL_VOLDES)
			{
			  *ret_arv_num = -1;
			}
		      else
			{
			  *ret_arv_num = *ret_arv_num + direction;
			}
		    }
		}

	      first_time = false;
	      if (*ret_arv_num < 0 || *ret_arv_num == log_Gl.hdr.nxarv_num)
		{
		  /* Unable to find page in archive */
		  if (log_Gl.append.vdes != NULL_VOLDES)
		    {
		      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_NOTIN_ARCHIVE, 1, pageid);
		    }
		  else
		    {
		      /*
		       * This is likely an incomplete recovery (restore).
		       * We do not have the active log and we are looking for a log page
		       */
		      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_LOG_NOTIN_ARCHIVE, 1, pageid);
		    }

		  LOG_ARCHIVE_CS_EXIT (thread_p);

		  return NULL;
		}
	    }

	  if (logpb_is_archive_available (thread_p, *ret_arv_num) == false)
	    {
	      arv_hdr = NULL;
	      continue;
	    }

	  fileio_make_log_archive_name (arv_name, log_Archive_path, log_Prefix, *ret_arv_num);
	  retry = 3;
	  while (retry != 0 && retry != 1
		 && (vdes =
		     fileio_mount (thread_p, log_Db_fullname, arv_name, LOG_DBLOG_ARCHIVE_VOLID, false,
				   false)) == NULL_VOLDES)
	    {
	      char line_buf[PATH_MAX * 2];
	      bool is_in_crash_recovery;

	      is_in_crash_recovery = log_is_in_crash_recovery ();

	      /*
	       * The archive is not online.
	       */
	      if (is_in_crash_recovery == true)
		{
		  fprintf (stdout, "%s\n", er_msg ());
		}

	    retry_prompt:
	      if (log_default_input_for_archive_log_location >= 0)
		{
		  retry = log_default_input_for_archive_log_location;
		  if (retry == 1 && is_in_crash_recovery == true)
		    {
		      fprintf (stdout, "Continue without present archive. (Partial recovery).\n");
		    }
		}
	      else
		{
		  fprintf (stdout, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_LOG, MSGCAT_LOG_STARTS));
		  fprintf (stdout, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_LOG, MSGCAT_LOG_LOGARCHIVE_NEEDED),
			   arv_name);
		  fprintf (stdout, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_LOG, MSGCAT_LOG_STARTS));

		  if (fgets (line_buf, PATH_MAX, stdin) == NULL)
		    {
		      retry = 0;	/* EOF */
		    }
		  else if (sscanf (line_buf, "%d", &retry) != 1)
		    {
		      retry = -1;	/* invalid input */
		    }
		}

	      switch (retry)
		{
		case 0:	/* quit */
		  logpb_set_unavailable_archive (thread_p, *ret_arv_num);
		  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_NOTIN_ARCHIVE, 1, pageid);
		  if (is_fatal)
		    {
		      logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "log_fetch_from_archive");
		    }

		  LOG_ARCHIVE_CS_EXIT (thread_p);

		  return NULL;

		case 1:	/* Not available */
		  logpb_set_unavailable_archive (thread_p, *ret_arv_num);
		  break;

		case 3:	/* Relocate */
		  fprintf (stdout, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_LOG, MSGCAT_LOG_NEWLOCATION));
		  if (fgets (line_buf, PATH_MAX, stdin) == 0 || (sscanf (line_buf, format_string, arv_name) != 1))
		    {
		      fileio_make_log_archive_name (arv_name, log_Archive_path, log_Prefix, *ret_arv_num);
		    }
		  break;

		case 2:	/* Retry */
		  break;

		default:	/* Something strange.  Get user to try again. */
		  fprintf (stdout, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_LOG, MSGCAT_LOG_INPUT_RANGE_ERROR),
			   0, 3);
		  goto retry_prompt;
		}
	    }

	  if (vdes != NULL_VOLDES)
	    {
	      /* Read header page and make sure the page is here */

	      /* Record number of reads in statistics */
	      perfmon_inc_stat (thread_p, PSTAT_LOG_NUM_IOREADS);

	      if (fileio_read (thread_p, vdes, hdr_pgptr, 0, LOG_PAGESIZE) == NULL)
		{
		  fileio_dismount (thread_p, vdes);
		  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_READ, 3, 0LL, 0LL, arv_name);

		  LOG_ARCHIVE_CS_EXIT (thread_p);

		  return NULL;
		}
	      arv_hdr = (LOG_ARV_HEADER *) hdr_pgptr->area;
	      if (log_Gl.append.vdes != NULL_VOLDES)
		{
		  if (difftime64 ((time_t) arv_hdr->db_creation, (time_t) log_Gl.hdr.db_creation) != 0)
		    {
		      /*
		       * This volume does not belong to the database. For now, assume
		       * that it is not only. Later, we will give this error to user
		       */
		      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_DOESNT_CORRESPOND_TO_DATABASE, 1,
			      arv_name);
		      arv_hdr = NULL;
		    }
		}
	    }
	  else
	    {
	      arv_hdr = NULL;
	    }
	}
    }

#if defined(CUBRID_DEBUG)
  if (log_pgptr->hdr.logical_pageid != pageid)
    {
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_PAGE_CORRUPTED, 1, pageid);
      logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "log_fetch_from_archive");

      LOG_ARCHIVE_CS_EXIT (thread_p);

      return NULL;
    }
#endif /* CUBRID_DEBUG */

#if !defined (NDEBUG)
  /* In analysys phase, the page may be corrupted. */
  if (log_Gl.rcv_phase == LOG_RESTARTED)
    {
      logpb_debug_check_log_page (thread_p, log_pgptr);
    }
#endif /* !NDEBUG */

  assert (log_pgptr != NULL && *ret_arv_num != -1 && arv_hdr != NULL);
  if (ret_arv_hdr != NULL)
    {
      *ret_arv_hdr = *arv_hdr;
    }

  LOG_ARCHIVE_CS_EXIT (thread_p);

  return log_pgptr;
}

/*
 * logpb_archive_active_log - Archive the active portion of the log
 *
 * return: nothing
 *
 * NOTE: The active portion of the log is archived from the next log
 *              archive page to the previous log page of the current append
 *              log record, to the next log archive.
 */
static void
logpb_archive_active_log (THREAD_ENTRY * thread_p)
{
  char arv_name[PATH_MAX] = { '\0' };	/* Archive name */
  LOG_PAGE *malloc_arv_hdr_pgptr = NULL;	/* Archive header page PTR */
  LOG_ARV_HEADER *arvhdr;	/* Archive header */
  BACKGROUND_ARCHIVING_INFO *bg_arv_info;
  char log_pgbuf[IO_MAX_PAGE_SIZE * LOGPB_IO_NPAGES + MAX_ALIGNMENT];
  char *aligned_log_pgbuf;
  LOG_PAGE *log_pgptr = NULL;
  LOG_PAGEID pageid, last_pageid;
  LOG_PHY_PAGEID ar_phy_pageid;
  int vdes = NULL_VOLDES;
  const char *catmsg;
  int error_code = NO_ERROR;
  int num_pages = 0;
  FILEIO_WRITE_MODE write_mode;

  aligned_log_pgbuf = PTR_ALIGN (log_pgbuf, MAX_ALIGNMENT);

  assert (LOG_CS_OWN_WRITE_MODE (thread_p));

#if defined(SERVER_MODE)
  log_wakeup_remove_log_archive_daemon ();
#else
  logpb_remove_archive_logs_exceed_limit (thread_p, 0);
#endif

  logpb_log ("Entered logpb_archive_active_log. log_Gl.hdr.nxarv_phy_pageid = %lld , log_Gl.hdr.nxarv_pageid =%lld\n",
	     (long long int) log_Gl.hdr.nxarv_phy_pageid, (long long int) log_Gl.hdr.nxarv_pageid);

  if (log_Gl.hdr.nxarv_pageid >= log_Gl.hdr.append_lsa.pageid)
    {
      er_log_debug (ARG_FILE_LINE,
		    "log_archive_active_log: WARNING Trying to archive ONLY the append page" " which is incomplete\n");
      return;
    }

  bg_arv_info = &log_Gl.bg_archive_info;
  if (log_Gl.archive.vdes != NULL_VOLDES)
    {
      /* A recheck is required after logpb_flush_all_append_pages when LOG_CS is demoted and promoted.
       * log_Gl.archive.vdes may be modified by someone else. Should we remove this dismount? */
      logpb_dismount_log_archive (thread_p);
    }

  malloc_arv_hdr_pgptr = (LOG_PAGE *) malloc (LOG_PAGESIZE);
  if (malloc_arv_hdr_pgptr == NULL)
    {
      goto error;
    }
  memset (malloc_arv_hdr_pgptr, LOG_PAGE_INIT_VALUE, LOG_PAGESIZE);

  /* Must force the log here to avoid nasty side effects */
  logpb_flush_all_append_pages (thread_p);

  malloc_arv_hdr_pgptr->hdr.logical_pageid = LOGPB_HEADER_PAGE_ID;
  malloc_arv_hdr_pgptr->hdr.offset = NULL_OFFSET;
  malloc_arv_hdr_pgptr->hdr.flags = 0;	/* Now flags in header page has always 0 value */

  /* Construct the archive log header */
  arvhdr = (LOG_ARV_HEADER *) malloc_arv_hdr_pgptr->area;
  strncpy (arvhdr->magic, CUBRID_MAGIC_LOG_ARCHIVE, CUBRID_MAGIC_MAX_LENGTH);
  arvhdr->db_creation = log_Gl.hdr.db_creation;
  arvhdr->next_trid = log_Gl.hdr.next_trid;
  arvhdr->arv_num = log_Gl.hdr.nxarv_num;

  /*
   * All pages must be archived... even the ones with unactive log records
   * This is the desired parameter to support multimedia crashes.
   *
   *
   * Note that the npages field does not include the previous lsa page
   *
   */
  arvhdr->fpageid = log_Gl.hdr.nxarv_pageid;
  last_pageid = log_Gl.append.prev_lsa.pageid - 1;

  if (last_pageid < arvhdr->fpageid)
    {
      last_pageid = arvhdr->fpageid;
    }

  arvhdr->npages = (DKNPAGES) (last_pageid - arvhdr->fpageid + 1);

  /*
   * Now create the archive and start copying pages
   */

  perfmon_inc_stat (thread_p, PSTAT_LOG_NUM_ARCHIVES);

  fileio_make_log_archive_name (arv_name, log_Archive_path, log_Prefix, log_Gl.hdr.nxarv_num);

  if (prm_get_bool_value (PRM_ID_LOG_BACKGROUND_ARCHIVING) && bg_arv_info->vdes != NULL_VOLDES)
    {
      vdes = bg_arv_info->vdes;
    }
  else
    {
      vdes = fileio_format (thread_p, log_Db_fullname, arv_name, LOG_DBLOG_ARCHIVE_VOLID, arvhdr->npages + 1, false,
			    false, false, LOG_PAGESIZE, 0, false);
      if (vdes == NULL_VOLDES)
	{
	  /* Unable to create archive log to archive */
	  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_CREATE_LOGARCHIVE_FAIL, 3, arv_name, arvhdr->fpageid,
		  arvhdr->fpageid + arvhdr->npages - 1);
	  goto error;
	}
    }

  log_archive_er_log ("logpb_archive_active_log, arvhdr->fpageid = %lld\n", arvhdr->fpageid);

  error_code = logpb_set_page_checksum (thread_p, malloc_arv_hdr_pgptr);
  if (error_code != NO_ERROR)
    {
      goto error;
    }

  write_mode = dwb_is_created () == true ? FILEIO_WRITE_NO_COMPENSATE_WRITE : FILEIO_WRITE_DEFAULT_WRITE;
  if (fileio_write (thread_p, vdes, malloc_arv_hdr_pgptr, 0, LOG_PAGESIZE, write_mode) == NULL)
    {
      /* Error archiving header page into archive */
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_WRITE, 3, 0LL, 0LL, arv_name);
      goto error;
    }

  if (prm_get_bool_value (PRM_ID_LOG_BACKGROUND_ARCHIVING) && bg_arv_info->vdes != NULL_VOLDES
      && arvhdr->fpageid == bg_arv_info->start_page_id)
    {
      pageid = bg_arv_info->current_page_id;
      ar_phy_pageid = (LOG_PHY_PAGEID) (bg_arv_info->current_page_id - bg_arv_info->start_page_id + 1);
    }
  else
    {
      assert (!prm_get_bool_value (PRM_ID_LOG_BACKGROUND_ARCHIVING) || bg_arv_info->vdes == NULL_VOLDES);

      pageid = arvhdr->fpageid;
      ar_phy_pageid = 1;
    }

  log_pgptr = (LOG_PAGE *) aligned_log_pgbuf;

  /* Now start dumping the current active pages to archive */
  for (; pageid <= last_pageid; pageid += num_pages, ar_phy_pageid += num_pages)
    {
      logpb_log ("Dump page %lld in logpb_archive_active_log, num_pages = %d\n", (long long int) pageid, num_pages);
      num_pages = (int) MIN (LOGPB_IO_NPAGES, last_pageid - pageid + 1);
      num_pages = logpb_read_page_from_active_log (thread_p, pageid, num_pages, false, log_pgptr);
      if (num_pages <= 0)
	{
	  goto error;
	}

      /* no need to encrypt, it is read as not decrypted (TDE) */
      if (fileio_write_pages (thread_p, vdes, (char *) log_pgptr, ar_phy_pageid, num_pages, LOG_PAGESIZE,
			      FILEIO_WRITE_NO_COMPENSATE_WRITE) == NULL)
	{
	  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_WRITE, 3, pageid, ar_phy_pageid, arv_name);
	  goto error;
	}
    }

  if (prm_get_bool_value (PRM_ID_LOG_BACKGROUND_ARCHIVING) && bg_arv_info->vdes != NULL_VOLDES)
    {
      fileio_dismount (thread_p, vdes);
      vdes = NULL_VOLDES;
      bg_arv_info->vdes = NULL_VOLDES;

      /* rename _lgar_t to _lgar[number] name */
      if (fileio_rename (NULL_VOLID, log_Name_bg_archive, arv_name) == NULL)
	{
	  goto error;
	}

      vdes = fileio_mount (thread_p, log_Db_fullname, arv_name, LOG_DBLOG_ARCHIVE_VOLID, 0, false);
      if (vdes == NULL_VOLDES)
	{
	  goto error;
	}
    }
  else
    {
      /*
       * Make sure that the whole log archive is in physical storage at this
       * moment. System volume. No need to sync DWB.
       */
      if (fileio_synchronize (thread_p, vdes, arv_name, FILEIO_SYNC_ONLY) == NULL_VOLDES)
	{
	  goto error;
	}
    }

  /* The last archive needed for system crashes */
  if (log_Gl.hdr.last_arv_num_for_syscrashes == -1)
    {
      log_Gl.hdr.last_arv_num_for_syscrashes = log_Gl.hdr.nxarv_num;
    }

  log_Gl.hdr.nxarv_num++;
  log_Gl.hdr.nxarv_pageid = last_pageid + 1;
  log_Gl.hdr.nxarv_phy_pageid = logpb_to_physical_pageid (log_Gl.hdr.nxarv_pageid);

  log_Gl.hdr.was_active_log_reset = false;

  logpb_log
    ("In logpb_archive_active_log, new values from log_Gl.hdr.nxarv_pageid = %lld and log_Gl.hdr.nxarv_phy_pageid = %lld\n",
     (long long int) log_Gl.hdr.nxarv_pageid, (long long int) log_Gl.hdr.nxarv_phy_pageid);
  /* Flush the log header to reflect the archive */
  logpb_flush_header (thread_p);

#if 0
  if (prm_get_integer_value (PRM_ID_SUPPRESS_FSYNC) != 0)
    {
      fileio_synchronize (thread_p, log_Gl.append.vdes, FILEIO_SYNC_ONLY);
    }
#endif

  er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, ER_LOG_ARCHIVE_CREATED, 3, arv_name, arvhdr->fpageid, last_pageid);

  /* Cast the archive information. May be used again */

  LOG_ARCHIVE_CS_ENTER (thread_p);

  log_Gl.archive.hdr = *arvhdr;	/* Copy of structure */
  if (log_Gl.archive.vdes != NULL_VOLDES)
    {
      logpb_dismount_log_archive (thread_p);
    }
  log_Gl.archive.vdes = vdes;

  LOG_ARCHIVE_CS_EXIT (thread_p);

  catmsg = msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_LOG, MSGCAT_LOG_LOGINFO_ARCHIVE);
  if (catmsg == NULL)
    {
      catmsg = "ARCHIVE: %d %s %lld %lld\n";
    }
  error_code =
    log_dump_log_info (log_Name_info, true, catmsg, log_Gl.hdr.nxarv_num - 1, arv_name, arvhdr->fpageid, last_pageid);
  if (error_code != NO_ERROR && error_code != ER_LOG_MOUNT_FAIL)
    {
      goto error;
    }

  (void) logpb_add_archive_page_info (thread_p, log_Gl.hdr.nxarv_num - 1, arvhdr->fpageid, last_pageid);

#if defined(SERVER_MODE)
  if (!HA_DISABLED ())
    {
      LOG_PAGEID min_fpageid = logwr_get_min_copied_fpageid ();
      if (min_fpageid != NULL_PAGEID)
	{
	  int unneeded_arvnum = -1;
	  if (min_fpageid >= arvhdr->fpageid)
	    {
	      unneeded_arvnum = arvhdr->arv_num - 1;
	    }
	  else
	    {
	      LOG_ARV_HEADER min_arvhdr;
	      if (logpb_fetch_from_archive (thread_p, min_fpageid, NULL, NULL, &min_arvhdr, false) != NULL)
		{
		  unneeded_arvnum = min_arvhdr.arv_num - 1;
		}
	    }
	  if (unneeded_arvnum >= 0)
	    {
	      char unneeded_logarv_name[PATH_MAX];
	      fileio_make_log_archive_name (unneeded_logarv_name, log_Archive_path, log_Prefix, unneeded_arvnum);

	      catmsg =
		msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_LOG, MSGCAT_LOG_LOGINFO_COMMENT_UNUSED_ARCHIVE_NAME);
	      if (catmsg == NULL)
		{
		  catmsg =
		    "29 COMMENT: Log archive %s, which contains log pages before %lld,"
		    " is not needed any longer by any HA utilities.\n";
		}
	      error_code = log_dump_log_info (log_Name_info, true, catmsg, unneeded_logarv_name, min_fpageid);
	      if (error_code != NO_ERROR && error_code != ER_LOG_MOUNT_FAIL)
		{
		  goto error;
		}
	    }
	}
    }

#endif /* SERVER_MODE */


  if (prm_get_bool_value (PRM_ID_LOG_BACKGROUND_ARCHIVING))
    {
      /* rename removed archive log file to reuse it */
      os_rename_file (log_Name_removed_archive, log_Name_bg_archive);

      bg_arv_info->vdes =
	fileio_format (thread_p, log_Db_fullname, log_Name_bg_archive, LOG_DBLOG_BG_ARCHIVE_VOLID, log_Gl.hdr.npages,
		       false, false, false, LOG_PAGESIZE, 0, true);
      if (bg_arv_info->vdes != NULL_VOLDES)
	{
	  bg_arv_info->start_page_id = log_Gl.hdr.nxarv_pageid;
	  bg_arv_info->current_page_id = log_Gl.hdr.nxarv_pageid;
	  bg_arv_info->last_sync_pageid = log_Gl.hdr.nxarv_pageid;
	}
      else
	{
	  bg_arv_info->start_page_id = NULL_PAGEID;
	  bg_arv_info->current_page_id = NULL_PAGEID;
	  bg_arv_info->last_sync_pageid = NULL_PAGEID;

	  er_log_debug (ARG_FILE_LINE, "Unable to create temporary archive log %s\n", log_Name_bg_archive);
	}
    }

  log_archive_er_log ("logpb_archive_active_log end, arvhdr->fpageid = %lld, arvhdr->npages = %d\n", arvhdr->fpageid,
		      arvhdr->npages);

  free_and_init (malloc_arv_hdr_pgptr);

  return;

  /* ********* */
error:

  if (malloc_arv_hdr_pgptr != NULL)
    {
      free_and_init (malloc_arv_hdr_pgptr);
    }

  if (vdes != NULL_VOLDES)
    {
      fileio_dismount (thread_p, vdes);
      fileio_unformat (thread_p, arv_name);
    }

  if (prm_get_bool_value (PRM_ID_LOG_BACKGROUND_ARCHIVING))
    {
      if (bg_arv_info->vdes != NULL_VOLDES && bg_arv_info->vdes != vdes)
	{
	  fileio_dismount (thread_p, bg_arv_info->vdes);
	}
      fileio_unformat (thread_p, log_Name_bg_archive);
      bg_arv_info->vdes = NULL_VOLDES;
    }

  logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "log_archive_active_log");
}

int
logpb_remove_archive_logs_exceed_limit (THREAD_ENTRY * thread_p, int max_count)
{
  int first_arv_num_to_delete = -1;
  int last_arv_num_to_delete = -1;
  int min_arv_required_for_vacuum;
  LOG_PAGEID vacuum_first_pageid = NULL_PAGEID;
#if defined(SERVER_MODE)
  LOG_PAGEID min_copied_pageid;
  int min_copied_arv_num;
#endif /* SERVER_MODE */
  int num_remove_arv_num;
  int log_max_archives = prm_get_integer_value (PRM_ID_LOG_MAX_ARCHIVES);
  char *catmsg;
  int deleted_count = 0;

  LOG_PAGEID cdc_first_pageid = NULL_PAGEID;
  int min_arv_required_for_cdc;

  LOG_PAGEID flashback_first_pageid = NULL_LOG_PAGEID;
  int min_arv_required_for_flashback;

  if (log_max_archives == INT_MAX)
    {
      return 0;			/* none is deleted */
    }

  if (!vacuum_is_safe_to_remove_archives ())
    {
      /* we don't know yet what is the first log page required by vacuum so it is not safe to remove log archives.
         unfortunately, to update the oldest vacuum data log pageid can be done only after loading vacuum data from
         disk, which in turn can only happen after recovery. this will block any log archive removal until vacuum
         is loaded. */
      return 0;
    }

  /* Get first log pageid needed for vacuum before locking LOG_CS. */
  vacuum_first_pageid = vacuum_min_log_pageid_to_keep (thread_p);

  LOG_CS_ENTER (thread_p);

  if (!prm_get_bool_value (PRM_ID_FORCE_REMOVE_LOG_ARCHIVES))
    {
#if defined(SERVER_MODE)
      min_copied_pageid = logwr_get_min_copied_fpageid ();
      if (min_copied_pageid == NULL_PAGEID)
	{
	  LOG_CS_EXIT (thread_p);
	  return 0;		/* none is deleted */
	}

      if (logpb_is_page_in_archive (min_copied_pageid))
	{
	  min_copied_arv_num = logpb_get_archive_number (thread_p, min_copied_pageid);
	  if (min_copied_arv_num == -1)
	    {
	      LOG_CS_EXIT (thread_p);
	      return 0;		/* none is deleted */
	    }
	  else if (min_copied_arv_num > 1)
	    {
	      min_copied_arv_num--;
	    }

	  num_remove_arv_num = MAX (log_max_archives, log_Gl.hdr.nxarv_num - min_copied_arv_num);
	}
      else
	{
	  num_remove_arv_num = log_max_archives;
	}
#else /* SERVER_MODE */
      num_remove_arv_num = log_max_archives;
#endif
    }
  else
    {
      num_remove_arv_num = log_max_archives;
    }

  if ((log_Gl.hdr.nxarv_num - (log_Gl.hdr.last_deleted_arv_num + 1)) > num_remove_arv_num)
    {
      er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, ER_LOG_MAX_ARCHIVES_HAS_BEEN_EXCEEDED, 1, num_remove_arv_num);

      /* Remove the log archives at this point */
      first_arv_num_to_delete = log_Gl.hdr.last_deleted_arv_num + 1;
      last_arv_num_to_delete = log_Gl.hdr.nxarv_num - num_remove_arv_num;

      if (log_Gl.hdr.last_arv_num_for_syscrashes != -1)
	{
	  last_arv_num_to_delete = MIN (last_arv_num_to_delete, log_Gl.hdr.last_arv_num_for_syscrashes);
	}
      vacuum_er_log (VACUUM_ER_LOG_ARCHIVES, "First log pageid in vacuum data is %lld", vacuum_first_pageid);
      if (vacuum_first_pageid != NULL_PAGEID && logpb_is_page_in_archive (vacuum_first_pageid))
	{
	  min_arv_required_for_vacuum = logpb_get_archive_number (thread_p, vacuum_first_pageid);
	  vacuum_er_log (VACUUM_ER_LOG_ARCHIVES, "First archive number used for vacuum is %d",
			 min_arv_required_for_vacuum);
	  if (min_arv_required_for_vacuum >= 0)
	    {
	      last_arv_num_to_delete = MIN (last_arv_num_to_delete, min_arv_required_for_vacuum);
	    }
	  else
	    {
	      /* Page should be in archive. */
	      assert (false);
	    }
	}

      if (prm_get_integer_value (PRM_ID_SUPPLEMENTAL_LOG))
	{
	  cdc_first_pageid = cdc_min_log_pageid_to_keep ();

	  _er_log_debug (ARG_FILE_LINE, "First log pageid in cdc data is %lld", cdc_first_pageid);
	  if (cdc_first_pageid != NULL_PAGEID && logpb_is_page_in_archive (cdc_first_pageid))
	    {
	      min_arv_required_for_cdc = logpb_get_archive_number (thread_p, cdc_first_pageid);

	      _er_log_debug (ARG_FILE_LINE,
			     "First archive number used for cdc is %d , for vacuum is %d, last_arv_num_for_syscrashes : %d",
			     min_arv_required_for_cdc, min_arv_required_for_vacuum,
			     log_Gl.hdr.last_arv_num_for_syscrashes);

	      if (min_arv_required_for_cdc >= 0)
		{
		  last_arv_num_to_delete = MIN (last_arv_num_to_delete, min_arv_required_for_cdc);
		}
	      else
		{
		  /* Page should be in archive. */
		  assert (false);
		}
	    }

	  /* check flashback */
	  if (flashback_is_needed_to_keep_archive ())
	    {
	      flashback_first_pageid = flashback_min_log_pageid_to_keep ();

	      _er_log_debug (ARG_FILE_LINE, "First log pageid for flashback is %lld", flashback_first_pageid);

	      /* NULL check for flashback_first_pageid is done in flashback_is_needed_to_keep_archive () */
	      if (flashback_first_pageid != NULL_LOG_PAGEID && logpb_is_page_in_archive (flashback_first_pageid))
		{
		  min_arv_required_for_flashback = logpb_get_archive_number (thread_p, flashback_first_pageid);

		  _er_log_debug (ARG_FILE_LINE, "First archive number used for flashback is %d",
				 min_arv_required_for_flashback);

		  if (min_arv_required_for_flashback >= 0)
		    {
		      last_arv_num_to_delete = MIN (last_arv_num_to_delete, min_arv_required_for_flashback);
		    }
		  else
		    {
		      /* Page should be in archive. */
		      assert (false);
		    }
		}
	    }
	}

      if (max_count > 0)
	{
	  /* check max count for deletion */
	  last_arv_num_to_delete = MIN (last_arv_num_to_delete, first_arv_num_to_delete + max_count);
	}

      last_arv_num_to_delete--;
      if (last_arv_num_to_delete >= first_arv_num_to_delete)
	{
	  log_Gl.hdr.last_deleted_arv_num = last_arv_num_to_delete;

#if defined (SA_MODE)
	  if (LSA_ISNULL (&log_Gl.hdr.mvcc_op_log_lsa))
	    {
	      /* Update the last_blockid needed for vacuum. Get the first page_id of the previously logged archive */
	      log_Gl.hdr.vacuum_last_blockid = logpb_last_complete_blockid ();
	    }
#endif /* SA_MODE */
	  logpb_flush_header (thread_p);	/* to get rid of archives */
	}

      vacuum_er_log (VACUUM_ER_LOG_ARCHIVES, "last_arv_num_to_delete is %d", last_arv_num_to_delete);
    }

  LOG_CS_EXIT (thread_p);

  if (last_arv_num_to_delete >= 0 && last_arv_num_to_delete >= first_arv_num_to_delete)
    {
      if (prm_get_bool_value (PRM_ID_DEBUG_LOG_ARCHIVES) || VACUUM_IS_ER_LOG_LEVEL_SET (VACUUM_ER_LOG_ARCHIVES))
	{
	  /* this is too problematic not to log in server error log too! */
	  _er_log_debug (ARG_FILE_LINE, "Purge archives starting with %d and up until %d; "
			 "vacuum_first_pageid = %d, last_arv_num_for_syscrashes = %d",
			 first_arv_num_to_delete, last_arv_num_to_delete, vacuum_first_pageid,
			 log_Gl.hdr.last_arv_num_for_syscrashes);
	}

      catmsg = msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_LOG, MSGCAT_LOG_MAX_ARCHIVES_HAS_BEEN_EXCEEDED);
      if (catmsg == NULL)
	{
	  catmsg = (char *) "Number of active log archives has been exceeded the max desired number.";
	}
      deleted_count =
	logpb_remove_archive_logs_internal (thread_p, first_arv_num_to_delete, last_arv_num_to_delete, catmsg);
    }

  return deleted_count;
}

/*
 * logpb_remove_archive_logs - Remove all unactive log archives
 *
 * return: nothing
 *
 *   info_reason(in):
 *
 * NOTE: Archive that are not needed for system crashes, vacuum, cdc, and flashback are removed.
 *       That these archives may be needed for media crash recovery.
 *       Therefore, it is important that the user copy these archives
 *       to tape. Check the log information file.
 */
void
logpb_remove_archive_logs (THREAD_ENTRY * thread_p, const char *info_reason)
{
#if !defined(SERVER_MODE)
  LOG_LSA flush_upto_lsa;	/* Flush data pages up to LSA */
#endif /* !SERVER_MODE */
  LOG_LSA newflush_upto_lsa;	/* Next to be flush */
  int first_deleted_arv_num;
  int last_deleted_arv_num;
  int min_arv_required_for_vacuum;
  LOG_PAGEID vacuum_first_pageid;

  int min_arv_required_for_cdc;
  LOG_PAGEID cdc_first_pageid;

  int min_arv_required_for_flashback;
  LOG_PAGEID flashback_first_pageid;

  if (!vacuum_is_safe_to_remove_archives ())
    {
      /* we don't know yet what is the first log page required by vacuum if vacuum_disable is set to true.
       * this will block any log archive removal until it is set to false. */
      return;
    }

  assert (LOG_CS_OWN_WRITE_MODE (thread_p));

  /* Close any log archives that are opened */
  if (log_Gl.archive.vdes != NULL_VOLDES)
    {
      logpb_dismount_log_archive (thread_p);
    }

#if defined(SERVER_MODE)
  LSA_COPY (&newflush_upto_lsa, &log_Gl.flushed_lsa_lower_bound);
#else /* SERVER_MODE */

  flush_upto_lsa.pageid = LOGPB_NEXT_ARCHIVE_PAGE_ID;
  flush_upto_lsa.offset = NULL_OFFSET;

  pgbuf_flush_checkpoint (thread_p, &flush_upto_lsa, NULL, &newflush_upto_lsa, NULL);

  if ((!LSA_ISNULL (&newflush_upto_lsa) && LSA_LT (&newflush_upto_lsa, &flush_upto_lsa))
      || (fileio_synchronize_all (thread_p, false) != NO_ERROR))
    {
      /* Cannot remove the archives at this moment */
      return;
    }

  if (log_Gl.run_nxchkpt_atpageid != NULL_PAGEID)
    {
      if (LSA_LT (&log_Gl.hdr.chkpt_lsa, &flush_upto_lsa))
	{
	  /*
	   * Reset the checkpoint record to the first possible active page and
	   * flush the log header before the archives are removed
	   */
	  LSA_COPY (&log_Gl.hdr.chkpt_lsa, &flush_upto_lsa);
	  logpb_flush_header (thread_p);
	}
    }
#endif /* SERVER_MODE */

  last_deleted_arv_num = log_Gl.hdr.last_arv_num_for_syscrashes;
  if (last_deleted_arv_num == -1)
    {
      last_deleted_arv_num = log_Gl.hdr.nxarv_num;
    }

  last_deleted_arv_num--;

  vacuum_first_pageid = vacuum_min_log_pageid_to_keep (thread_p);
  if (vacuum_first_pageid != NULL_PAGEID && logpb_is_page_in_archive (vacuum_first_pageid))
    {
      min_arv_required_for_vacuum = logpb_get_archive_number (thread_p, vacuum_first_pageid);
      min_arv_required_for_vacuum--;
      last_deleted_arv_num = MIN (last_deleted_arv_num, min_arv_required_for_vacuum);
    }

  if (prm_get_integer_value (PRM_ID_SUPPLEMENTAL_LOG))
    {
      cdc_first_pageid = cdc_min_log_pageid_to_keep ();
      if (cdc_first_pageid != NULL_PAGEID && logpb_is_page_in_archive (cdc_first_pageid))
	{
	  min_arv_required_for_cdc = logpb_get_archive_number (thread_p, cdc_first_pageid);
	  min_arv_required_for_cdc--;
	  last_deleted_arv_num = MIN (last_deleted_arv_num, min_arv_required_for_cdc);
	}

      /* flashback */
      if (flashback_is_needed_to_keep_archive ())
	{

	  flashback_first_pageid = flashback_min_log_pageid_to_keep ();

	  if (flashback_first_pageid != NULL_LOG_PAGEID && logpb_is_page_in_archive (flashback_first_pageid))
	    {
	      min_arv_required_for_flashback = logpb_get_archive_number (thread_p, flashback_first_pageid);
	      min_arv_required_for_flashback--;
	      last_deleted_arv_num = MIN (last_deleted_arv_num, min_arv_required_for_flashback);

	      _er_log_debug (ARG_FILE_LINE,
			     "FLASHBACK : first log pageid is %d, first archive log number is %d, last_deleted_arv_num is %d",
			     flashback_first_pageid, min_arv_required_for_flashback, last_deleted_arv_num);
	    }
	}
    }

  if (log_Gl.hdr.last_deleted_arv_num + 1 > last_deleted_arv_num)
    {
      /* Nothing to remove */
      return;
    }

  first_deleted_arv_num = log_Gl.hdr.last_deleted_arv_num + 1;
  if (last_deleted_arv_num >= 0)
    {
      logpb_remove_archive_logs_internal (thread_p, first_deleted_arv_num, last_deleted_arv_num, info_reason);

      log_Gl.hdr.last_deleted_arv_num = last_deleted_arv_num;
      logpb_flush_header (thread_p);	/* to get rid of archives */

      _er_log_debug (ARG_FILE_LINE, "first_deleted_arv_num = %d, last_deleted_arv_num = %d", first_deleted_arv_num,
		     last_deleted_arv_num);
    }
}

/*
 * logpb_add_archive_page_info -
 *
 * return: 0
 *
 *   thread_p(in)  :
 *   arv_num(in)   :
 *   start_page(in):
 *   end_page(in)  :
 *
 * NOTE:
 */
static int
logpb_add_archive_page_info (THREAD_ENTRY * thread_p, int arv_num, LOG_PAGEID start_page, LOG_PAGEID end_page)
{
  int rear;

  assert (LOG_CS_OWN (thread_get_thread_entry_info ()));

  rear = logpb_Arv_page_info_table.rear;

  rear = (rear + 1) % ARV_PAGE_INFO_TABLE_SIZE;

  if (logpb_Arv_page_info_table.item_count < ARV_PAGE_INFO_TABLE_SIZE)
    {
      logpb_Arv_page_info_table.item_count++;
    }

  logpb_Arv_page_info_table.rear = rear;
  logpb_Arv_page_info_table.page_info[rear].arv_num = arv_num;
  logpb_Arv_page_info_table.page_info[rear].start_pageid = start_page;
  logpb_Arv_page_info_table.page_info[rear].end_pageid = end_page;

  return 0;
}

/*
 * logpb_get_archive_num_from_info_table -
 *
 * return: archive_number or -1
 *
 *   thread_p(in)  :
 *   page_id(in)   :
 *
 * NOTE:
 */
static int
logpb_get_archive_num_from_info_table (THREAD_ENTRY * thread_p, LOG_PAGEID page_id)
{
  int i, count;

  assert (LOG_CS_OWN (thread_get_thread_entry_info ()));

  for (i = logpb_Arv_page_info_table.rear, count = 0; count < logpb_Arv_page_info_table.item_count;
       i = ((i == 0) ? ARV_PAGE_INFO_TABLE_SIZE - 1 : i - 1), count++)
    {
      if (logpb_Arv_page_info_table.page_info[i].start_pageid <= page_id
	  && page_id <= logpb_Arv_page_info_table.page_info[i].end_pageid)
	{
	  return logpb_Arv_page_info_table.page_info[i].arv_num;
	}
    }

  return -1;
}

/*
 * log_remove_archive_logs_internal - Remove all unactive log archives
 *
 * return: nothing
 *
 *   first(in): first archive to be deleted
 *   last(in): last archive to be deleted
 *   info_reason(in): message describing the reason the archives are being deleted
 *   check_backup(in): when true, avoid deleting archives needed for a restore
 *
 * NOTE: This routine does the actual deletion and notifies the user
 *   via the lginf file.  This routine does not do any logical verification
 *   to insure that the archives are no longer needed, so the caller must be
 *   explicitly careful and insure that there are no circumstances under
 *   which those archives could possibly be needed before they are deleted.
 *   Common reasons why they might be needed include a media crash or a
 *   restore from a fuzzy backup.
 */
static int
logpb_remove_archive_logs_internal (THREAD_ENTRY * thread_p, int first, int last, const char *info_reason)
{
  char logarv_name[PATH_MAX];
  int i;
  bool append_log_info = false;
  int deleted_count = 0;

  /* Decache any archive remaining in the log_Gl.archive. */
  logpb_decache_archive_info (thread_p);

  for (i = first; i <= last; i++)
    {
      fileio_make_log_archive_name (logarv_name, log_Archive_path, log_Prefix, i);

#if defined(SERVER_MODE)
      if (prm_get_bool_value (PRM_ID_LOG_BACKGROUND_ARCHIVING) && boot_Server_status == BOOT_SERVER_UP)
	{
	  fileio_unformat_and_rename (thread_p, logarv_name, log_Name_removed_archive);
	}
      else
	{
	  fileio_unformat (thread_p, logarv_name);
	}
#else
      fileio_unformat (thread_p, logarv_name);
#endif
      append_log_info = true;
      deleted_count++;
    }

  if (append_log_info)
    {
      logpb_append_archives_removed_to_log_info (first, last, info_reason);
    }

  return deleted_count;
}

/*
 * logpb_append_archives_removed_to_log_info - Record deletion of one or more archives
 *
 * return: nothing
 *
 *   first(in): first archive to be deleted
 *   last(in): last archive to be deleted
 *   info_reason(in): message describing the reason the archives are being deleted
 *
 * NOTE: This routine makes an entry into the loginfo file that the
 *   given log archives have been removed.
 */
static void
logpb_append_archives_removed_to_log_info (int first, int last, const char *info_reason)
{
  const char *catmsg;
  char logarv_name[PATH_MAX];
  char logarv_name_first[PATH_MAX];
  int error_code;

  if (info_reason != NULL)
    {
      catmsg = msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_LOG, MSGCAT_LOG_LOGINFO_REMOVE_REASON);
      if (catmsg == NULL)
	{
	  catmsg = "REMOVE: %d %s to \n%d %s.\nREASON: %s\n";
	}

      fileio_make_log_archive_name (logarv_name, log_Archive_path, log_Prefix, last);
      if (first == last)
	{
	  error_code =
	    log_dump_log_info (log_Name_info, true, catmsg, first, logarv_name, last, logarv_name, info_reason);
	}
      else
	{
	  fileio_make_log_archive_name (logarv_name_first, log_Archive_path, log_Prefix, first);
	  error_code =
	    log_dump_log_info (log_Name_info, true, catmsg, first, logarv_name_first, last, logarv_name, info_reason);
	}
      if (error_code != NO_ERROR && error_code != ER_LOG_MOUNT_FAIL)
	{
	  return;
	}
    }
}

/*
 *
 *       	   FUNCTIONS RELATED TO MISCELANEOUS I/O
 *
 */

/*
 * logpb_copy_from_log: Copy a portion of the log
 *
 * arguments:
 *  area: Area where the portion of the log is copied. (Set as a side effect)
 *  area_length: the length to copy
 *  log_lsa: log address of the log data to copy (May be set as a side effect)
 *  log_pgptr: the buffer containing the log page (May be set as a side effect)
 *
 * returns/side-effects: nothing
 *    area is set as a side effect.
 *    log_lsa, and log_pgptr are set as a side effect.
 *
 * description: Copy "length" bytes of the log starting at log_lsa->pageid, log_offset onto the given area.
 * NOTE:        The location of the log is updated to point to the end of the data.
 */
void
logpb_copy_from_log (THREAD_ENTRY * thread_p, char *area, int length, LOG_LSA * log_lsa, LOG_PAGE * log_page_p)
{
  int copy_length;		/* Length to copy into area */
  int area_offset;		/* The area offset */

  /*
   * If the data is contained in only one buffer, copy the data in one
   * statement, otherwise, copy it in parts
   */

  if (log_lsa->offset + length < (int) LOGAREA_SIZE)
    {
      /* The log data is contiguos */
      memcpy (area, (char *) log_page_p->area + log_lsa->offset, length);
      log_lsa->offset += length;
    }
  else
    {
      /* The log data is not contiguos */
      area_offset = 0;
      while (length > 0)
	{
	  LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, 0, log_lsa, log_page_p);
	  if (log_lsa->offset + length < (int) LOGAREA_SIZE)
	    {
	      copy_length = length;
	    }
	  else
	    {
	      copy_length = LOGAREA_SIZE - (int) (log_lsa->offset);
	    }
	  memcpy (area + area_offset, (char *) log_page_p->area + log_lsa->offset, copy_length);
	  length -= copy_length;
	  area_offset += copy_length;
	  log_lsa->offset += copy_length;
	}
    }
}

/*
 * logpb_verify_length - Verify db and log lengths
 *
 * return: NO_ERROR if all OK, ER status otherwise
 *
 *   db_fullname(in): Full name of the database
 *   log_path(in): Directory where the log volumes reside
 *   log_prefix(in): Name of the log volumes. It is usually set as database
 *                   name. For example, if the value is equal to "db", the
 *                   names of the log volumes created are as follow:
 *                      Active_log      = db_logactive
 *                      Archive_logs    = db_logarchive.0
 *                                        db_logarchive.1
 *                                             .
 *                                             .
 *                                             .
 *                                        db_logarchive.n
 *                      Log_information = db_loginfo
 *                      Database Backup = db_backup
 *
 * NOTE: Make sure that any database name will not be overpassed.
 */
static int
logpb_verify_length (const char *db_fullname, const char *log_path, const char *log_prefix)
{
  int volmax_suffix;
  int length;
  const char *dbname;
  long int filename_max;
  long int pathname_max;

  volmax_suffix = fileio_get_volume_max_suffix ();

  dbname = fileio_get_base_file_name (db_fullname);
  if (fileio_get_max_name (db_fullname, &filename_max, &pathname_max) < 0)
    {
      return ER_FAILED;
    }

  if (pathname_max > DB_MAX_PATH_LENGTH)
    {
      pathname_max = DB_MAX_PATH_LENGTH;
    }

  /*
   * Make sure that names of volumes (information and log volumes), that is,
   * OS files will not exceed the maximum allowed value.
   */

  if ((int) (strlen (dbname) + 1 + volmax_suffix) > filename_max)
    {
      /* The name of the volume is too long */
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_NOFULL_DATABASE_NAME_IS_TOO_LONG, 2, dbname,
	      filename_max - volmax_suffix - 1);
      return ER_LOG_NOFULL_DATABASE_NAME_IS_TOO_LONG;
    }

  if ((int) (strlen (log_prefix) + 1) > filename_max || (strlen (log_prefix) + 1) > MAXLOGNAME)
    {
      /* Bad prefix log name */
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_PREFIX_NAME_IS_TOO_LONG, 2, log_prefix,
	      (((int) MAXLOGNAME > filename_max) ? filename_max - 1 : (int) MAXLOGNAME - 1));
      return ER_LOG_PREFIX_NAME_IS_TOO_LONG;
    }

  /*
   * Make sure that the length for the volume is OK
   */

  if ((int) (strlen (db_fullname) + 1 + volmax_suffix) > pathname_max)
    {
      /* The full name of the database is too long */
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_BO_FULL_DATABASE_NAME_IS_TOO_LONG, 3, NULL, db_fullname,
	      strlen (db_fullname) + 1, pathname_max);
      return ER_BO_FULL_DATABASE_NAME_IS_TOO_LONG;
    }

  /*
   * First create a new log for the new database. This log is not a copy of
   * of the old log; it is a newly created one.
   */

  if (log_path != NULL)
    {
      length = (int) (strlen (log_path) + strlen (log_prefix) + 2);
    }
  else
    {
      length = (int) strlen (log_prefix) + 1;
    }

  if (length + volmax_suffix > pathname_max)
    {
      /*
       * Database name is too long.
       * Path + prefix < pathname_max - 2
       */
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_NAME_IS_TOO_LONG, 3, log_path, log_prefix,
	      pathname_max - 2);
      return ER_LOG_NAME_IS_TOO_LONG;
    }

  return NO_ERROR;
}

/*
 * logpb_initialize_log_names - Initialize the names of log volumes and files
 *
 * return: nothing
 *
 *   db_fullname(in): Full name of the database
 *   logpath(in): Directory where the log volumes reside
 *   prefix_logname(in): Name of the log volumes. It is usually set as database
 *                      name. For example, if the value is equal to "db", the
 *                      names of the log volumes created are as follow:
 *                      Active_log      = db_logactive
 *                      Archive_logs    = db_logarchive.0
 *                                        db_logarchive.1
 *                                             .
 *                                             .
 *                                             .
 *                                        db_logarchive.n
 *                      Log_information = db_loginfo
 *                      Database Backup = db_backup
 *
 * NOTE: Initialize name of log volumes and files
 */
int
logpb_initialize_log_names (THREAD_ENTRY * thread_p, const char *db_fullname, const char *logpath,
			    const char *prefix_logname)
{
  int error_code = NO_ERROR;

  error_code = logpb_verify_length (db_fullname, logpath, prefix_logname);
  if (error_code != NO_ERROR)
    {
      /* Names are too long */
      logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "logpb_initialize_log_names");

      return error_code;
    }

  /* Save the log Path */

  if (logpath != NULL)
    {
      strcpy (log_Path, logpath);
    }
  else
    {
      strcpy (log_Path, ".");
    }

  strcpy (log_Archive_path, log_Path);

  /* Save the log Prefix */
  strcpy (log_Prefix, prefix_logname);

  /*
   * Build Name of log active
   */
  fileio_make_log_active_name (log_Name_active, log_Path, log_Prefix);
  fileio_make_log_info_name (log_Name_info, log_Path, log_Prefix);
  fileio_make_backup_volume_info_name (log_Name_bkupinfo, log_Path, log_Prefix);
  fileio_make_volume_info_name (log_Name_volinfo, db_fullname);
  fileio_make_log_archive_temp_name (log_Name_bg_archive, log_Archive_path, log_Prefix);
  fileio_make_removed_log_archive_name (log_Name_removed_archive, log_Archive_path, log_Prefix);
  log_Db_fullname = db_fullname;

  return error_code;
}

/*
 * logpb_exist_log - Find if given log exists
 *
 * return:
 *
 *   db_fullname(in): Full name of the database
 *   logpath(in): Directory where the log volumes reside
 *   prefix_logname(in): Name of the log volumes. It is usually set as database
 *                   name. For example, if the value is equal to "db", the
 *                   names of the log volumes created are as follow:
 *                      Active_log      = db_logactive
 *                      Archive_logs    = db_logarchive.0
 *                                        db_logarchive.1
 *                                             .
 *                                             .
 *                                             .
 *                                        db_logarchive.n
 *                      Log_information = db_loginfo
 *                      Database Backup = db_backup
 *
 * NOTE: Find if the log associated with the aboive database exists.
 */
bool
logpb_exist_log (THREAD_ENTRY * thread_p, const char *db_fullname, const char *logpath, const char *prefix_logname)
{
  /* Is the system restarted ? */
  if (!logpb_is_pool_initialized ())
    {
      if (logpb_initialize_log_names (thread_p, db_fullname, logpath, prefix_logname) != NO_ERROR)
	{
	  return false;
	}
    }

  return fileio_is_volume_exist (log_Name_active);
}

/* logpb_checkpoint_trans - checkpoint a transaction if it is valid for checkpointing
 */
void
logpb_checkpoint_trans (LOG_INFO_CHKPT_TRANS * chkpt_entries, log_tdes * tdes, int &ntrans, int &ntops,
			LOG_LSA & smallest_lsa)
{
  LOG_INFO_CHKPT_TRANS *chkpt_entry = &chkpt_entries[ntrans];
  /* - commit_abort_lsa is filled when either commit or abbort entry is appended to the transaction;
   *    the last part of the condition has the effect that the actual transaction state is ignored by the
   *    checkpoint mechanism as long as either the commit or the abort log records have been appended
   */
  if (tdes != NULL && tdes->trid != NULL_TRANID && !tdes->tail_lsa.is_null () && tdes->commit_abort_lsa.is_null ())
    {
      chkpt_entry->isloose_end = tdes->isloose_end;
      chkpt_entry->trid = tdes->trid;
      chkpt_entry->state = tdes->state;
      LSA_COPY (&chkpt_entry->head_lsa, &tdes->head_lsa);
      LSA_COPY (&chkpt_entry->tail_lsa, &tdes->tail_lsa);
      if (chkpt_entry->state == TRAN_UNACTIVE_ABORTED)
	{
	  /*
	   * Transaction is in the middle of an abort, since rollback does
	   * is not run in a critical section. Set the undo point to be the
	   * same as its tail. The recovery process will read the last
	   * record which is likely a compensating one, and find where to
	   * continue a rollback operation.
	   */
	  LSA_COPY (&chkpt_entry->undo_nxlsa, &tdes->tail_lsa);
	}
      else
	{
	  LSA_COPY (&chkpt_entry->undo_nxlsa, &tdes->undo_nxlsa);
	}

      LSA_COPY (&chkpt_entry->posp_nxlsa, &tdes->posp_nxlsa);
      LSA_COPY (&chkpt_entry->savept_lsa, &tdes->savept_lsa);
      LSA_COPY (&chkpt_entry->tail_topresult_lsa, &tdes->tail_topresult_lsa);
      LSA_COPY (&chkpt_entry->start_postpone_lsa, &tdes->rcv.tran_start_postpone_lsa);
      strncpy (chkpt_entry->user_name, tdes->client.get_db_user (), LOG_USERNAME_MAX);
      ntrans++;
      if (tdes->topops.last >= 0 && (tdes->state == TRAN_UNACTIVE_TOPOPE_COMMITTED_WITH_POSTPONE))
	{
	  ntops += tdes->topops.last + 1;
	}

      if (LSA_ISNULL (&smallest_lsa) || LSA_GT (&smallest_lsa, &tdes->head_lsa))
	{
	  LSA_COPY (&smallest_lsa, &tdes->head_lsa);
	}
    }
}

int
logpb_checkpoint_topops (THREAD_ENTRY * thread_p, LOG_INFO_CHKPT_SYSOP * &chkpt_topops,
			 LOG_INFO_CHKPT_TRANS * chkpt_trans, LOG_REC_CHKPT & tmp_chkpt, log_tdes * tdes, int &ntops,
			 size_t & length_all_tops)
{
  if (tdes != NULL && tdes->trid != NULL_TRANID
      && (!LSA_ISNULL (&tdes->rcv.sysop_start_postpone_lsa) || !LSA_ISNULL (&tdes->rcv.atomic_sysop_start_lsa)))
    {
      /* this transaction is running system operation postpone or an atomic system operation
       * note: we cannot compare tdes->state with TRAN_UNACTIVE_TOPOPE_COMMITTED_WITH_POSTPONE. we are
       *       not synchronizing setting transaction state.
       *       however, setting tdes->rcv.sysop_start_postpone_lsa is protected by
       *       log_Gl.prior_info.prior_lsa_mutex. so we check this instead of state.
       */
      if (ntops >= tmp_chkpt.ntops)
	{
	  tmp_chkpt.ntops += log_Gl.trantable.num_assigned_indices;
	  length_all_tops = sizeof (*chkpt_topops) * tmp_chkpt.ntops;
	  LOG_INFO_CHKPT_SYSOP *ptr = (LOG_INFO_CHKPT_SYSOP *) realloc (chkpt_topops, length_all_tops);
	  if (ptr == NULL)
	    {
	      free_and_init (chkpt_trans);
	      log_Gl.prior_info.prior_lsa_mutex.unlock ();
	      TR_TABLE_CS_EXIT (thread_p);
	      return ER_FAILED;
	    }
	  chkpt_topops = ptr;
	}

      LOG_INFO_CHKPT_SYSOP *chkpt_topop = &chkpt_topops[ntops];
      chkpt_topop->trid = tdes->trid;
      chkpt_topop->sysop_start_postpone_lsa = tdes->rcv.sysop_start_postpone_lsa;
      chkpt_topop->atomic_sysop_start_lsa = tdes->rcv.atomic_sysop_start_lsa;
      ntops++;
    }
  return NO_ERROR;
}

/*
 * logpb_checkpoint - Execute a fuzzy checkpoint
 *
 * return: pageid where a redo will start
 *
 */
LOG_PAGEID
logpb_checkpoint (THREAD_ENTRY * thread_p)
{
#define detailed_er_log(...) if (detailed_logging) _er_log_debug (ARG_FILE_LINE, __VA_ARGS__)

  LOG_TDES *tdes;		/* System transaction descriptor */
  LOG_TDES *act_tdes;		/* Transaction descriptor of an active transaction */
  LOG_REC_CHKPT *chkpt, tmp_chkpt;	/* Checkpoint log records */
  LOG_INFO_CHKPT_TRANS *chkpt_trans;	/* Checkpoint tdes */
  LOG_INFO_CHKPT_TRANS *chkpt_one;	/* Checkpoint tdes for one tran */
  LOG_INFO_CHKPT_SYSOP *chkpt_topops;	/* Checkpoint top system operations that are in commit postpone
					 * mode */
  LOG_INFO_CHKPT_SYSOP *chkpt_topone;	/* One top system ope */
  LOG_LSA chkpt_lsa;		/* copy of log_Gl.hdr.chkpt_lsa */
  LOG_LSA chkpt_redo_lsa;	/* copy of log_Gl.chkpt_redo_lsa */
  LOG_LSA newchkpt_lsa;		/* New address of the checkpoint record */
  LOG_LSA smallest_lsa;
  unsigned int nobj_locks;	/* Avg number of locks */
  char logarv_name[PATH_MAX];	/* Archive name */
  char logarv_name_first[PATH_MAX];	/* Archive name */
  int ntrans;			/* Number of trans */
  int ntops;			/* Number of total active top actions */
  int length_all_chkpt_trans;
  size_t length_all_tops = 0;
  int i;
  const char *catmsg;
  VOLID volid;
  VOLID curr_last_perm_volid;
  int error_code = NO_ERROR;
  LOG_PAGEID smallest_pageid;
  int first_arv_num_not_needed;
  int last_arv_num_not_needed;
  LOG_PRIOR_NODE *node;
  void *ptr;
  int flushed_page_cnt = 0, vdes;
  bool detailed_logging = prm_get_bool_value (PRM_ID_LOG_CHKPT_DETAILED);
  // *INDENT-OFF*
  log_system_tdes::map_func mapper;
  // *INDENT-ON*

  LOG_CS_ENTER (thread_p);

#if defined(SERVER_MODE)
  if (BO_IS_SERVER_RESTARTED () && log_Gl.run_nxchkpt_atpageid == NULL_PAGEID)
    {
      LOG_CS_EXIT (thread_p);
      return NULL_PAGEID;
    }

  /*
   * Critical section is entered several times to allow other transaction to
   * use the log manger
   */
#endif /* SERVER_MODE */

  perfmon_inc_stat (thread_p, PSTAT_LOG_NUM_START_CHECKPOINTS);

  er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, ER_LOG_CHECKPOINT_STARTED, 2, log_Gl.hdr.chkpt_lsa.pageid,
	  log_Gl.chkpt_redo_lsa.pageid);
  er_log_debug (ARG_FILE_LINE, "start checkpoint\n");

  /*
   * Indicate that the checkpoint process is running. Don't run another one,
   * until we are done with the present one.
   */

  log_Gl.run_nxchkpt_atpageid = NULL_PAGEID;

  tdes = LOG_FIND_TDES (LOG_SYSTEM_TRAN_INDEX);
  if (tdes == NULL)
    {
      goto error_cannot_chkpt;
    }

  /*
   * FLUSH all append LOG PAGES and flush all DIRTY DATA PAGES whose LSA
   * are SMALLER OR EQUAL than newchkpt_lsa value and find the next redo
   * point.
   */

  (void) pthread_mutex_lock (&log_Gl.chkpt_lsa_lock);
  LSA_COPY (&chkpt_lsa, &log_Gl.hdr.chkpt_lsa);
  LSA_COPY (&chkpt_redo_lsa, &log_Gl.chkpt_redo_lsa);
  pthread_mutex_unlock (&log_Gl.chkpt_lsa_lock);

  logpb_flush_pages_direct (thread_p);

  /* MARK THE CHECKPOINT PROCESS */
  node = prior_lsa_alloc_and_copy_data (thread_p, LOG_START_CHKPT, RV_NOT_DEFINED, NULL, 0, NULL, 0, NULL);
  if (node == NULL)
    {
      goto error_cannot_chkpt;
    }

  newchkpt_lsa = prior_lsa_next_record (thread_p, node, tdes);
  assert (!LSA_ISNULL (&newchkpt_lsa));

  /*
   * Modify log header to record present checkpoint. The header is flushed
   * later
   */

  LOG_CS_EXIT (thread_p);

  detailed_er_log ("logpb_checkpoint: call logtb_reflect_global_unique_stats_to_btree()\n");
  if (logtb_reflect_global_unique_stats_to_btree (thread_p) != NO_ERROR)
    {
      goto error_cannot_chkpt;
    }

  detailed_er_log ("logpb_checkpoint: call pgbuf_flush_checkpoint()\n");
  if (pgbuf_flush_checkpoint (thread_p, &newchkpt_lsa, &chkpt_redo_lsa, &tmp_chkpt.redo_lsa, &flushed_page_cnt) !=
      NO_ERROR)
    {
      goto error_cannot_chkpt;
    }

  detailed_er_log ("logpb_checkpoint: call fileio_synchronize_all()\n");
  if (fileio_synchronize_all (thread_p, false) != NO_ERROR)
    {
      goto error_cannot_chkpt;
    }

  LOG_CS_ENTER (thread_p);

  if (LSA_ISNULL (&tmp_chkpt.redo_lsa))
    {
      LSA_COPY (&tmp_chkpt.redo_lsa, &newchkpt_lsa);
    }

  assert (LSA_LE (&tmp_chkpt.redo_lsa, &newchkpt_lsa));

#if defined(SERVER_MODE)
  /* Save lower bound of flushed lsa */
  if (!LSA_ISNULL (&tmp_chkpt.redo_lsa))
    {
      if (LSA_ISNULL (&log_Gl.flushed_lsa_lower_bound) || LSA_GT (&tmp_chkpt.redo_lsa, &log_Gl.flushed_lsa_lower_bound))
	{
	  LSA_COPY (&log_Gl.flushed_lsa_lower_bound, &tmp_chkpt.redo_lsa);
	}
    }
#endif /* SERVER_MODE */

  TR_TABLE_CS_ENTER (thread_p);

  /* allocate memory space for the transaction descriptors */
  tmp_chkpt.ntrans = log_Gl.trantable.num_assigned_indices;
  length_all_chkpt_trans = sizeof (*chkpt_trans) * tmp_chkpt.ntrans;

  chkpt_trans = (LOG_INFO_CHKPT_TRANS *) malloc (length_all_chkpt_trans);
  if (chkpt_trans == NULL)
    {
      TR_TABLE_CS_EXIT (thread_p);
      goto error_cannot_chkpt;
    }

  log_Gl.prior_info.prior_lsa_mutex.lock ();

  /* CHECKPOINT THE TRANSACTION TABLE */

  LSA_SET_NULL (&smallest_lsa);
  for (i = 0, ntrans = 0, ntops = 0; i < log_Gl.trantable.num_total_indices; i++)
    {
      /*
       * Don't checkpoint current system transaction. That is, the one of
       * checkpoint process
       */
      if (i == LOG_SYSTEM_TRAN_INDEX)
	{
	  continue;
	}
      act_tdes = LOG_FIND_TDES (i);
      assert (ntrans < tmp_chkpt.ntrans);
      logpb_checkpoint_trans (chkpt_trans, act_tdes, ntrans, ntops, smallest_lsa);
    }

  /*
   * Reset the structure to the correct number of transactions and
   * recalculate the length
   */
  tmp_chkpt.ntrans = ntrans;
  length_all_chkpt_trans = sizeof (*chkpt_trans) * tmp_chkpt.ntrans;

  /*
   * Scan again if there were any top system operations in the process of being committed.
   * NOTE that we checkpoint top system operations only when there are in the
   * process of commit. Not knowledge of top system operations that are not
   * in the process of commit is required since if there is a crash, the system
   * operation is aborted as part of the transaction.
   */

  chkpt_topops = NULL;
  if (ntops > 0)
    {
      tmp_chkpt.ntops = log_Gl.trantable.num_assigned_indices;
      length_all_tops = sizeof (*chkpt_topops) * tmp_chkpt.ntops;
      chkpt_topops = (LOG_INFO_CHKPT_SYSOP *) malloc (length_all_tops);
      if (chkpt_topops == NULL)
	{
	  free_and_init (chkpt_trans);
	  log_Gl.prior_info.prior_lsa_mutex.unlock ();
	  TR_TABLE_CS_EXIT (thread_p);
	  goto error_cannot_chkpt;
	}

      /* CHECKPOINTING THE TOP ACTIONS */
      for (i = 0, ntrans = 0, ntops = 0; i < log_Gl.trantable.num_total_indices; i++)
	{
	  /*
	   * Don't checkpoint current system transaction. That is, the one of
	   * checkpoint process
	   */
	  if (i == LOG_SYSTEM_TRAN_INDEX)
	    {
	      continue;
	    }
	  act_tdes = LOG_FIND_TDES (i);
	  error_code =
	    logpb_checkpoint_topops (thread_p, chkpt_topops, chkpt_trans, tmp_chkpt, act_tdes, ntops, length_all_tops);
	  if (error_code != NO_ERROR)
	    {
	      goto error_cannot_chkpt;
	    }
	}
    }
  else
    {
      tmp_chkpt.ntops = 1;
      length_all_tops = sizeof (*chkpt_topops) * tmp_chkpt.ntops;
      chkpt_topops = (LOG_INFO_CHKPT_SYSOP *) malloc (length_all_tops);
      if (chkpt_topops == NULL)
	{
	  free_and_init (chkpt_trans);
	  log_Gl.prior_info.prior_lsa_mutex.unlock ();
	  TR_TABLE_CS_EXIT (thread_p);
	  goto error_cannot_chkpt;
	}
    }

  // Checkpoint system transactions' topops
  // *INDENT-OFF*
  mapper = [thread_p, &chkpt_topops, &chkpt_trans, &tmp_chkpt, &ntops, &length_all_tops, &error_code] (log_tdes &tdes)
  {
    error_code =
      logpb_checkpoint_topops (thread_p, chkpt_topops, chkpt_trans, tmp_chkpt, &tdes, ntops, length_all_tops);
  };

  log_system_tdes::map_all_tdes (mapper);
  // *INDENT-ON*
  if (error_code != NO_ERROR)
    {
      goto error_cannot_chkpt;
    }

  assert (sizeof (*chkpt_topops) * ntops <= length_all_tops);
  tmp_chkpt.ntops = ntops;
  length_all_tops = sizeof (*chkpt_topops) * tmp_chkpt.ntops;

  node =
    prior_lsa_alloc_and_copy_data (thread_p, LOG_END_CHKPT, RV_NOT_DEFINED, NULL, length_all_chkpt_trans,
				   (char *) chkpt_trans, (int) length_all_tops, (char *) chkpt_topops);
  if (node == NULL)
    {
      free_and_init (chkpt_trans);

      if (chkpt_topops != NULL)
	{
	  free_and_init (chkpt_topops);
	}
      log_Gl.prior_info.prior_lsa_mutex.unlock ();
      TR_TABLE_CS_EXIT (thread_p);
      goto error_cannot_chkpt;
    }

  chkpt = (LOG_REC_CHKPT *) node->data_header;
  *chkpt = tmp_chkpt;

  prior_lsa_next_record_with_lock (thread_p, node, tdes);

  log_Gl.prior_info.prior_lsa_mutex.unlock ();

  TR_TABLE_CS_EXIT (thread_p);

  free_and_init (chkpt_trans);

  /* Any topops to log ? */
  if (chkpt_topops != NULL)
    {
      free_and_init (chkpt_topops);
    }

  /*
   * END append
   * Flush the page since we are going to flush the log header which
   * reflects the new location of the last checkpoint log record
   */
  logpb_flush_pages_direct (thread_p);
  detailed_er_log ("logpb_checkpoint: call logpb_flush_all_append_pages()\n");

  /*
   * Flush the log data header and update all checkpoints in volumes to
   * point to new checkpoint
   */

  /* Average the number of active transactions and locks */
  nobj_locks = lock_get_number_object_locks ();
  log_Gl.hdr.avg_ntrans = (log_Gl.hdr.avg_ntrans + ntrans) >> 1;
  log_Gl.hdr.avg_nlocks = (log_Gl.hdr.avg_nlocks + nobj_locks) >> 1;

  /* Flush the header */
  (void) pthread_mutex_lock (&log_Gl.chkpt_lsa_lock);
  if (LSA_LT (&log_Gl.hdr.chkpt_lsa, &newchkpt_lsa))
    {
      LSA_COPY (&log_Gl.hdr.chkpt_lsa, &newchkpt_lsa);
    }
  LSA_COPY (&chkpt_lsa, &log_Gl.hdr.chkpt_lsa);

  if (LSA_ISNULL (&smallest_lsa))
    {
      LSA_COPY (&log_Gl.hdr.smallest_lsa_at_last_chkpt, &log_Gl.hdr.chkpt_lsa);
    }
  else
    {
      LSA_COPY (&log_Gl.hdr.smallest_lsa_at_last_chkpt, &smallest_lsa);
    }
  LSA_COPY (&log_Gl.chkpt_redo_lsa, &tmp_chkpt.redo_lsa);

  pthread_mutex_unlock (&log_Gl.chkpt_lsa_lock);

  detailed_er_log ("logpb_checkpoint: call logpb_flush_header()\n");
  logpb_flush_header (thread_p);

  /*
   * Exit from the log critical section since we are going to call another
   * module which may be blocked on a lock.
   */

  LOG_CS_EXIT (thread_p);

  /*
   * Record the checkpoint address on every volume header
   */
  curr_last_perm_volid = xboot_find_last_permanent (thread_p);

  for (volid = LOG_DBFIRST_VOLID; volid != NULL_VOLID && volid <= curr_last_perm_volid;
       volid = fileio_find_next_perm_volume (thread_p, volid))
    {
      /* When volid is greater than boot_Db_parm->last_perm_volid, it means that the volume is now adding. We don't
       * need to care for the new volumes in here. */
      if (disk_set_checkpoint (thread_p, volid, &chkpt_lsa) != NO_ERROR)
	{
	  goto error_cannot_chkpt;
	}

      /* When setting the checkpoint, the header page modification is not logged. However, other concurrent
       * transaction may modify meanwhile the header page (with logging). Since writing data page is preceded by
       * log sync, we are sure that WAL is not violated. We only need to sync the data volume. Probably, it is better
       * to sync it now - as soon as possible. If we are postponing volume sync, we may write on disk more data pages
       * twice (the data pages modified before checkpoint - other than volume header page, written on disk at the
       * beginning of the checkpoint, modified again by concurrent transaction, written on disk again in checkpoint
       * due to volume header page modification).
       */
      vdes = fileio_get_volume_descriptor (volid);
      if (fileio_synchronize (thread_p, vdes, fileio_get_volume_label (vdes, PEEK), FILEIO_SYNC_ALSO_FLUSH_DWB) != vdes)
	{
	  goto error_cannot_chkpt;
	}
    }

  /*
   * Get the critical section again, so we can check if any archive can be
   * declare as un-needed
   */

  LOG_CS_ENTER (thread_p);

  /*
   * If the log archives are not needed for any normal undos and redos,
   * indicate so. However, the log archives may be needed during media
   * crash recovery.
   */
  smallest_pageid = NULL_PAGEID;
  first_arv_num_not_needed = last_arv_num_not_needed = -1;

  if (log_Gl.hdr.last_arv_num_for_syscrashes != log_Gl.hdr.nxarv_num && log_Gl.hdr.last_arv_num_for_syscrashes != -1)
    {
      LOG_LSA lsa;

#if defined(SERVER_MODE)
      smallest_pageid = MIN (log_Gl.flushed_lsa_lower_bound.pageid, tmp_chkpt.redo_lsa.pageid);
#else
      smallest_pageid = tmp_chkpt.redo_lsa.pageid;
#endif

      logtb_find_smallest_lsa (thread_p, &lsa);

      if (lsa.pageid != NULL_PAGEID)
	{
	  smallest_pageid = MIN (smallest_pageid, lsa.pageid);
	}

      if (logpb_is_page_in_archive (smallest_pageid))
	{
	  int arv_num;

	  if ((logpb_fetch_from_archive (thread_p, smallest_pageid, NULL, &arv_num, NULL, true) == NULL)
	      || (arv_num <= log_Gl.hdr.last_arv_num_for_syscrashes))
	    {
	      first_arv_num_not_needed = last_arv_num_not_needed = -1;
	    }
	  else
	    {
	      first_arv_num_not_needed = log_Gl.hdr.last_arv_num_for_syscrashes;
	      last_arv_num_not_needed = arv_num - 1;
	    }
	}
      else
	{
	  first_arv_num_not_needed = log_Gl.hdr.last_arv_num_for_syscrashes;
	  last_arv_num_not_needed = log_Gl.hdr.nxarv_num - 1;
	}

      if (first_arv_num_not_needed != -1)
	{
	  log_Gl.hdr.last_arv_num_for_syscrashes = last_arv_num_not_needed + 1;

	  /* Close any log archives that are opened */
	  if (log_Gl.archive.vdes != NULL_VOLDES)
	    {
	      logpb_dismount_log_archive (thread_p);
	    }

	  /* This is OK since we have already flushed the log header page */
	  if (first_arv_num_not_needed == last_arv_num_not_needed)
	    {
	      fileio_make_log_archive_name (logarv_name, log_Archive_path, log_Prefix, first_arv_num_not_needed);
	      catmsg =
		msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_LOG, MSGCAT_LOG_LOGINFO_COMMENT_ARCHIVE_NONEEDED);
	      if (catmsg == NULL)
		{
		  catmsg = "COMMENT: Log archive %s is not needed any longer unless a database media crash occurs.\n";
		}
	      error_code = log_dump_log_info (log_Name_info, true, catmsg, logarv_name);
	    }
	  else
	    {
	      fileio_make_log_archive_name (logarv_name_first, log_Archive_path, log_Prefix, first_arv_num_not_needed);
	      fileio_make_log_archive_name (logarv_name, log_Archive_path, log_Prefix, last_arv_num_not_needed);

	      catmsg =
		msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_LOG,
				MSGCAT_LOG_LOGINFO_COMMENT_MANY_ARCHIVES_NONEEDED);
	      if (catmsg == NULL)
		{
		  catmsg =
		    "COMMENT: Log archives from %s to %s are not"
		    " needed any longer unless a database media crash occurs.\n";
		}
	      error_code = log_dump_log_info (log_Name_info, true, catmsg, logarv_name_first, logarv_name);
	    }
	  if (error_code != NO_ERROR && error_code != ER_LOG_MOUNT_FAIL)
	    {
	      goto error_cannot_chkpt;
	    }

	  logpb_flush_header (thread_p);	/* Yes, one more time, to get rid of archives */
	}
    }

  /*
   * Checkpoint process is not running any longer. Indicate when do we expect
   * it to run.
   */

  log_Gl.run_nxchkpt_atpageid = (log_Gl.hdr.append_lsa.pageid + log_Gl.chkpt_every_npages);
  /*
   * Clear all tail and heads information of current system transaction
   * todo - is it safe to clear though?
   */
  logtb_clear_tdes (thread_p, tdes);

  LOG_CS_EXIT (thread_p);

#if 0
  /* have to sync log vol, data vol */
  fileio_synchronize_all (thread_p, true /* include_log */ );
#endif

  perfmon_inc_stat (thread_p, PSTAT_LOG_NUM_END_CHECKPOINTS);

  er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, ER_LOG_CHECKPOINT_FINISHED, 3, log_Gl.hdr.chkpt_lsa.pageid,
	  log_Gl.chkpt_redo_lsa.pageid, flushed_page_cnt);
  er_log_debug (ARG_FILE_LINE, "end checkpoint\n");

  return tmp_chkpt.redo_lsa.pageid;

  /* ******** */
error_cannot_chkpt:
  if (!LOG_CS_OWN_WRITE_MODE (thread_p))
    {
      LOG_CS_ENTER (thread_p);
    }

  /* to immediately execute the next checkpoint. */
  log_Gl.run_nxchkpt_atpageid = log_Gl.hdr.append_lsa.pageid;

  LOG_CS_EXIT (thread_p);

  return NULL_PAGEID;

#undef detailed_er_log
}

/*
 * logpb_dump_checkpoint_trans - Dump checkpoint transactions
 *
 * return: nothing
 *
 *   length(in): Length to dump in bytes
 *   data(in): The data being logged
 *
 * NOTE: Dump a checkpoint transactions structure.
 */
void
logpb_dump_checkpoint_trans (FILE * out_fp, int length, void *data)
{
  int ntrans, i;
  LOG_INFO_CHKPT_TRANS *chkpt_trans, *chkpt_one;	/* Checkpoint tdes */

  chkpt_trans = (LOG_INFO_CHKPT_TRANS *) data;
  ntrans = length / sizeof (*chkpt_trans);

  /* Start dumping each checkpoint transaction descriptor */

  for (i = 0; i < ntrans; i++)
    {
      chkpt_one = &chkpt_trans[i];
      fprintf (out_fp,
	       "     Trid = %d, State = %s, isloose_end = %d,\n"
	       "        Head_lsa = %lld|%d, Tail_lsa = %lld|%d, UndoNxtLSA = %lld|%d,\n"
	       "        Postpone_lsa = %lld|%d, Save_lsa = %lld|%d, Tail_topresult_lsa = %lld|%d,\n"
	       "	Client_User: name=%s.\n", chkpt_one->trid, log_state_string (chkpt_one->state),
	       chkpt_one->isloose_end, (long long int) chkpt_one->head_lsa.pageid,
	       (int) chkpt_one->head_lsa.offset, (long long int) chkpt_one->tail_lsa.pageid,
	       (int) chkpt_one->tail_lsa.offset, (long long int) chkpt_one->undo_nxlsa.pageid,
	       (int) chkpt_one->undo_nxlsa.offset, (long long int) chkpt_one->posp_nxlsa.pageid,
	       (int) chkpt_one->posp_nxlsa.offset, (long long int) chkpt_one->savept_lsa.pageid,
	       (int) chkpt_one->savept_lsa.offset, (long long int) chkpt_one->tail_topresult_lsa.pageid,
	       (int) chkpt_one->tail_topresult_lsa.offset, chkpt_one->user_name);
    }
  (void) fprintf (out_fp, "\n");
}

/*
 * logpb_backup_for_volume - Execute a full backup for the given volume
 *
 * return: NO_ERROR if all OK, ER status otherwise
 *
 *   volid(in): The identifier of the volume to backup
 *   chkpt_lsa(in): Checkpoint of the backup process
 *   session(in/out): The session array which is set as a side effect.
 *   only_updated(in):
 *
 * NOTE: The volume with the given volume identifier is backed up on
 *              the given fullname_backup. The backup that is taken is a fuzzy
 *              snapshot of the volume since updates to the volume may be in
 *              progress. Thus, the backup may contain some uncommitted data.
 *              Even worse, the backup is performed using the disk directly.
 *              That is, we copy the pages of the disk without going through
 *              the page buffer pool to avoid disrupting the locality of the
 *              page buffers (e.g., a query executed by another transaction)
 *              For temp volumes, incremental backups are not allowed (because
 *              of logging issues) so always save the system pages of temp
 *              volumes.
 */
static int
logpb_backup_for_volume (THREAD_ENTRY * thread_p, VOLID volid, LOG_LSA * chkpt_lsa, FILEIO_BACKUP_SESSION * session,
			 bool only_updated)
{
  DISK_VOLPURPOSE volpurpose;
  PAGEID vol_sys_lastpage;
  int vdes;
  int error_code = NO_ERROR;

  /*
   * Determine the purpose of the volume.  For most volumes we need to
   * backup every page, but for temporary volumes we only need the system
   * pages.
   */
  if (xdisk_get_purpose_and_sys_lastpage (thread_p, volid, &volpurpose, &vol_sys_lastpage) == NULL_VOLID)
    {
      error_code = ER_FAILED;
      return error_code;
    }
  else if (volpurpose == DB_TEMPORARY_DATA_PURPOSE)
    {
      /*
       * Do not allow incremental backups of temp volumes; but since we
       * only backup the system pages, they be only a handfull of pages.
       */
      only_updated = false;
    }
  else
    {
      vol_sys_lastpage = -1;	/* must backup entire volume */
    }

  /*
   * Reset the checkpoint of the volume to backup and flush all its dirty
   * pages, so that the the backup reflects the actual state of the volume
   * as much as possible
   */
  error_code = disk_set_checkpoint (thread_p, volid, chkpt_lsa);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  LOG_CS_ENTER (thread_p);
  logpb_flush_pages_direct (thread_p);
  LOG_CS_EXIT (thread_p);

  error_code = pgbuf_flush_all_unfixed (thread_p, volid);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  vdes = fileio_get_volume_descriptor (volid);
  if (fileio_synchronize (thread_p, vdes, fileio_get_volume_label (vdes, PEEK), FILEIO_SYNC_ALSO_FLUSH_DWB) != vdes)
    {
      return ER_FAILED;
    }

  /*
   * Create the backup file/volume and copy the content onto it.
   *
   * Note that the copy is done from disk to disk. The page buffer pool is
   * not used. Thus, some more recent version of some copied pages may be
   * present in the buffer pool. We copy the database without using the page
   * buffer pool to avoid disrupting the locality of pages in the page
   * buffer pool and avoid the overhead of calling the page buffer manager.
   */

  error_code =
    fileio_backup_volume (thread_p, session, fileio_get_volume_label (volid, PEEK), volid, vol_sys_lastpage,
			  only_updated);

  return error_code;
}

/*
 * logpb_backup - Execute a level backup for the given database volume
 *
 * return: NO_ERROR if all OK, ER status otherwise
 *
 *   num_perm_vols(in): Number of permanent volumes
 *   allbackup_path(in): Location where information volumes are
 *                                 backed up. If NULL is given, the following
 *                                 defaults are assumed to back up each
 *                                 information volume:
 *                                 - If file "fileof_vols_and_backup_paths" is
 *                                   given, the path to backup each volume is
 *                                   found in this file.
 *                                 - All information volumes are backed up on
 *                                   the same location where the log files are
 *                                   located.
 *   backup_level(in): backup levels allowed: 0 - Full (default),
 *                                 1 - Incremental1, 2 - Incremental
 *   delete_unneeded_logarchives(in): Whetear to remove log archives that are
 *                                 not needed any longer to recovery from
 *                                 crashes when the backup just created is
 *                                 used.
 *   backup_verbose_file_path(in): verbose mode file path
 *   num_threads(in): number of threads
 *   zip_method(in): compression method
 *   zip_level(in): compression level
 *   skip_activelog(in):
 *   sleep_msecs(in):
 *
 */
int
logpb_backup (THREAD_ENTRY * thread_p, int num_perm_vols, const char *allbackup_path, FILEIO_BACKUP_LEVEL backup_level,
	      bool delete_unneeded_logarchives, const char *backup_verbose_file_path, int num_threads,
	      FILEIO_ZIP_METHOD zip_method, FILEIO_ZIP_LEVEL zip_level, int skip_activelog, int sleep_msecs,
	      bool separate_keys)
{
  FILEIO_BACKUP_SESSION session;
  const char *from_vlabel;	/* Name of volume to backup (FROM) */
  char vol_backup[PATH_MAX];	/* Name of the backup volume (TO) */
  VOLID volid;			/* Current volume to backup */
  LOG_LSA bkup_start_lsa;	/* Start point of backup */
  LOG_LSA chkpt_lsa;		/* Checkpoint address where the backup process starts */
#if defined(SERVER_MODE)
  LOG_PAGEID saved_run_nxchkpt_atpageid = NULL_PAGEID;
#endif /* SERVER_MODE */
  FILE *backup_volinfo_fp = NULL;	/* Pointer to backup information/directory file */
  const char *catmsg;
  const char *bk_vol;		/* ptr to old bkup volume name */
  int unit_num;
  FILEIO_BACKUP_RECORD_INFO all_bkup_info[FILEIO_BACKUP_UNDEFINED_LEVEL];
  int first_arv_needed = -1;	/* for self contained, consistent */
  int last_arv_needed = -1;	/* backups, some arv are needed */
  bool beenwarned;
  bool isincremental = false;	/* Assume full backups */
  bool bkup_in_progress = false;

  char mk_path[PATH_MAX] = { 0, };
  char separate_mk_path[PATH_MAX] = { 0, };
  char bkpath_without_units[PATH_MAX] = { 0, };
  const char *db_nopath_name_p;
  int keys_vdes = NULL_VOLDES;
#if defined(SERVER_MODE)
  int rv;
  time_t wait_checkpoint_begin_time;
  bool print_backupdb_waiting_reason = false;
#endif /* SERVER_MODE */
  int error_code = NO_ERROR;
  FILEIO_BACKUP_HEADER *io_bkup_hdr_p;

  time_t backup_start_time, backup_end_time;
  char old_bkpath[PATH_MAX];
  const char *str_tmp;
  time_t tmp_time;
  char time_val[CTIME_MAX];

  LOG_PAGEID vacuum_first_pageid = NULL_PAGEID;

#if defined (SERVER_MODE)
  // check whether there is ongoing backup.
  LOG_CS_ENTER (thread_p);
  if (log_Gl.backup_in_progress == true)
    {
      LOG_CS_EXIT (thread_p);
      error_code = ER_LOG_BKUP_DUPLICATE_REQUESTS;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_code, 0);
      return error_code;
    }

  log_Gl.backup_in_progress = true;
  LOG_CS_EXIT (thread_p);
#endif /* SERVER_MODE */

  memset (&session, 0, sizeof (FILEIO_BACKUP_SESSION));

  /*
   * Determine the absolute path that corresponds with the desired location.
   */
  if (allbackup_path == NULL)
    {
      allbackup_path = log_Path;
    }

  /* tde key file has to be mounted to access exclusively with TDE Utility if it exists */
  tde_make_keys_file_fullname (mk_path, log_Db_fullname, false);
  keys_vdes = fileio_mount (thread_p, log_Db_fullname, mk_path, LOG_DBTDE_KEYS_VOLID, 1, false);
  if (keys_vdes == NULL_VOLDES && fileio_is_volume_exist (mk_path))
    {
      ASSERT_ERROR ();
      error_code = er_errid ();
      goto error;
    }

  /* Initialization gives us some useful information about the backup location. */
  session.type = FILEIO_BACKUP_WRITE;	/* access backup device for write */
  if (fileio_initialize_backup (log_Db_fullname, allbackup_path, &session, backup_level, backup_verbose_file_path,
				num_threads, sleep_msecs) == NULL)
    {
      error_code = ER_FAILED;
      goto error;
    }

  /*
   * Determine the first log archive that will be needed to insure
   * consistency if we are forced to restore the database with nothing but this backup.
   * first_arv_needed may need to be based on what archive chkpt_lsa is in.
   */

#if defined (SERVER_MODE)
  print_backupdb_waiting_reason = false;
  wait_checkpoint_begin_time = time (NULL);
loop:
#endif /* SERVER_MODE */

  // actually, it is not really necessary for SA but just for code consistency.
  LOG_CS_ENTER (thread_p);

#if defined (SERVER_MODE)
  /* check if checkpoint is in progress */
  if (log_Gl.run_nxchkpt_atpageid == NULL_PAGEID)
    {
      bool continue_check;
      LOG_CS_EXIT (thread_p);
      if (print_backupdb_waiting_reason == false && session.verbose_fp != NULL)
	{
	  fprintf (session.verbose_fp, "[ Database backup will start after checkpointing is complete. ]\n\n");
	  print_backupdb_waiting_reason = true;
	}
      /* wait until checkpoint process is finished */

      /* interrupt check */
      if (logtb_get_check_interrupt (thread_p) == true)
	{
	  if (logtb_is_interrupted (thread_p, true, &continue_check) == true)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_INTERRUPTED, 0);
	      error_code = ER_INTERRUPTED;
	      goto error;
	    }
	}

      thread_sleep (1000);	/* 1000 msec */
      goto loop;
    }
  if (print_backupdb_waiting_reason == true && session.verbose_fp != NULL)
    {

      fprintf (session.verbose_fp, "[ Database backup has been suspended for %lld seconds due to checkpoint. ]\n\n",
	       (long long int) (time (NULL) - wait_checkpoint_begin_time));
    }

  /* block checkpoint process */
  saved_run_nxchkpt_atpageid = log_Gl.run_nxchkpt_atpageid;
  log_Gl.run_nxchkpt_atpageid = NULL_PAGEID;
#endif /* SERVER_MODE */

  if (log_Gl.hdr.last_arv_num_for_syscrashes > -1)
    {
      first_arv_needed = log_Gl.hdr.last_arv_num_for_syscrashes;
    }
  else
    {
      first_arv_needed = log_Gl.hdr.nxarv_num;
    }

  vacuum_first_pageid = vacuum_min_log_pageid_to_keep (thread_p);
  vacuum_er_log (VACUUM_ER_LOG_ARCHIVES, "First log pageid in vacuum data is %lld\n", vacuum_first_pageid);

  if (vacuum_first_pageid != NULL_PAGEID && logpb_is_page_in_archive (vacuum_first_pageid))
    {
      int min_arv_required_for_vacuum = logpb_get_archive_number (thread_p, vacuum_first_pageid);

      vacuum_er_log (VACUUM_ER_LOG_ARCHIVES, "First archive number used for vacuum is %d\n",
		     min_arv_required_for_vacuum);

      if (min_arv_required_for_vacuum >= 0)
	{
	  if (first_arv_needed >= 0)
	    {
	      first_arv_needed = MIN (first_arv_needed, min_arv_required_for_vacuum);
	    }
	  else
	    {
	      first_arv_needed = min_arv_required_for_vacuum;
	    }
	}
      else
	{
	  /* Page should be in archive. */
	  assert (false);
	}

      vacuum_er_log (VACUUM_ER_LOG_ARCHIVES, "First archive needed for backup is %d\n", first_arv_needed);
    }

  /* Get the current checkpoint address */
  rv = pthread_mutex_lock (&log_Gl.chkpt_lsa_lock);
  LSA_COPY (&chkpt_lsa, &log_Gl.hdr.chkpt_lsa);
  pthread_mutex_unlock (&log_Gl.chkpt_lsa_lock);

  LOG_CS_EXIT (thread_p);

  switch (backup_level)
    {

    case FILEIO_BACKUP_BIG_INCREMENT_LEVEL:
      /* Start at the lower level backup */
      if (LSA_ISNULL (&log_Gl.hdr.bkup_level0_lsa))
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_BACKUP_LEVEL_NOGAPS, 2, backup_level, backup_level - 1);
	  fileio_finalize_backup_info (FILEIO_FIRST_BACKUP_VOL_INFO);
	  error_code = ER_LOG_BACKUP_LEVEL_NOGAPS;
	  goto error;
	}
      else
	{
	  LSA_COPY (&bkup_start_lsa, &log_Gl.hdr.bkup_level0_lsa);
	}
      isincremental = true;
      break;

    case FILEIO_BACKUP_SMALL_INCREMENT_LEVEL:
      /* Start at the lower level backup */
      if (LSA_ISNULL (&log_Gl.hdr.bkup_level1_lsa))
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_BACKUP_LEVEL_NOGAPS, 2, backup_level, backup_level - 1);
	  fileio_finalize_backup_info (FILEIO_FIRST_BACKUP_VOL_INFO);
	  error_code = ER_LOG_BACKUP_LEVEL_NOGAPS;
	  goto error;
	}
      else
	{
	  LSA_COPY (&bkup_start_lsa, &log_Gl.hdr.bkup_level1_lsa);
	}
      isincremental = true;
      break;

    default:
    case FILEIO_BACKUP_FULL_LEVEL:
      /* Always start at -1|-1 */
      LSA_SET_NULL (&bkup_start_lsa);
      backup_level = FILEIO_BACKUP_FULL_LEVEL;
      break;
    }

  /*
   * Check for existing backup volumes in this location, and warn
   * the user that they will be destroyed.
   */
  if (fileio_is_volume_exist (log_Name_bkupinfo))
    {
      /* check for writable regular file */
      if (fileio_is_volume_exist_and_file (log_Name_bkupinfo) != true)
	{
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_CREATE_DBBACKUP_DIRINFO, 1, log_Name_bkupinfo);
	  error_code = ER_LOG_CREATE_DBBACKUP_DIRINFO;
	  goto error;
	}
      /* check permission */
      if (access (log_Name_bkupinfo, W_OK) != 0)
	{
	  error_code = ER_LOG_CANNOT_ACCESS_BACKUP;
	}
      backup_volinfo_fp = fopen (log_Name_bkupinfo, "r");
      if (error_code != NO_ERROR || backup_volinfo_fp == NULL)
	{
	  er_set_with_oserror (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_CANNOT_ACCESS_BACKUP, 1,
			       log_Name_bkupinfo);
	  goto error;
	}
      if (fileio_read_backup_info_entries (backup_volinfo_fp, FILEIO_FIRST_BACKUP_VOL_INFO) != NO_ERROR)
	{
	  er_set_with_oserror (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_CANNOT_ACCESS_BACKUP, 1,
			       log_Name_bkupinfo);
	  fclose (backup_volinfo_fp);
	  error_code = ER_LOG_CANNOT_ACCESS_BACKUP;
	  goto error;
	}
      fclose (backup_volinfo_fp);
    }

  if (session.bkup.dtype != FILEIO_BACKUP_VOL_DEVICE)
    {
      /*
       * Delete previous backup volumes from this level.  The first
       * loop tries to determine if indeed there will be anything to
       * unformat/delete, while the second loop does the actual deletions.
       * Deletion is done only if the backup is taking place
       * in the same location as the previous backup or
       * is the same path-name as the the previous backup.
       */
      beenwarned = false;
      unit_num = FILEIO_INITIAL_BACKUP_UNITS;
      bk_vol = fileio_get_backup_info_volume_name (backup_level, unit_num++, FILEIO_FIRST_BACKUP_VOL_INFO);
      while (bk_vol && !beenwarned)
	{
	  if (fileio_is_volume_exist_and_file (bk_vol))
	    {
	      fileio_get_directory_path (old_bkpath, bk_vol);
	      if (strcmp (old_bkpath, allbackup_path) == 0 || strcmp (bk_vol, allbackup_path) == 0)
		{
		  if (!logpb_remote_ask_user_before_delete_volumes (thread_p, allbackup_path))
		    {
		      io_bkup_hdr_p = session.bkup.bkuphdr;
		      tmp_time = (time_t) io_bkup_hdr_p->start_time;
		      (void) ctime_r (&tmp_time, time_val);
		      snprintf (old_bkpath, PATH_MAX,
				msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_LOG,
						MSGCAT_LOG_BACKUP_HALTED_BY_USER), io_bkup_hdr_p->level,
				fileio_get_base_file_name (io_bkup_hdr_p->db_fullname), time_val,
				io_bkup_hdr_p->unit_num);
		      fileio_request_user_response (thread_p, FILEIO_PROMPT_DISPLAY_ONLY, old_bkpath, NULL, NULL, -1,
						    -1, NULL, -1);
		      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_DBBACKUP_FAIL, 1, log_Gl.hdr.prefix_name);
		      error_code = ER_LOG_DBBACKUP_FAIL;
		      goto error;
		    }
		  beenwarned = true;
		  break;
		}
	    }			/* if (fileio_is_volume_exist_and_file(bk_vol)) */
	  bk_vol = fileio_get_backup_info_volume_name (backup_level, unit_num++, FILEIO_FIRST_BACKUP_VOL_INFO);
	}			/* while */

      /* try to delete the superceded backup volumes */
      if (beenwarned)
	{
	  fileio_remove_all_backup (thread_p, backup_level);
	}
    }

  /* Throw away the old names and make room for new names */
  fileio_clear_backup_info_level (backup_level, false, FILEIO_FIRST_BACKUP_VOL_INFO);

  /*
   * Aside: all backup level related log hdr stuff should be in its own
   * array, including log_Gl (but I am hesitant to change it to avoid subtly
   * changing the disk rep). So for now, only have lower levels think array.
   * Arrays would allow future expansion to more than 3 levels.
   */
  LSA_COPY (&all_bkup_info[0].lsa, &log_Gl.hdr.bkup_level0_lsa);
  all_bkup_info[0].at_time = log_Gl.hdr.bkinfo[0].bkup_attime;
  LSA_COPY (&all_bkup_info[1].lsa, &log_Gl.hdr.bkup_level1_lsa);
  all_bkup_info[1].at_time = log_Gl.hdr.bkinfo[1].bkup_attime;
  LSA_COPY (&all_bkup_info[2].lsa, &log_Gl.hdr.bkup_level2_lsa);
  all_bkup_info[2].at_time = log_Gl.hdr.bkinfo[2].bkup_attime;

  if (session.verbose_fp)
    {
      if (backup_level != FILEIO_BACKUP_FULL_LEVEL)
	{
	  fprintf (session.verbose_fp, "\n\n\n");
	}

      switch (backup_level)
	{
	case FILEIO_BACKUP_FULL_LEVEL:
	  str_tmp = "Full";
	  break;
	case FILEIO_BACKUP_BIG_INCREMENT_LEVEL:
	  str_tmp = "Incremental Level 1";
	  break;
	default:
	  assert_release (backup_level == FILEIO_BACKUP_SMALL_INCREMENT_LEVEL);
	  str_tmp = "Incremental Level 2";
	  break;
	}
      fprintf (session.verbose_fp, "[ Database(%s) %s Backup start ]\n\n", boot_db_name (), str_tmp);

      fprintf (session.verbose_fp, "- num-threads: %d\n\n", session.read_thread_info.num_threads);

      if (zip_method == FILEIO_ZIP_NONE_METHOD)
	{
	  fprintf (session.verbose_fp, "- compression method: %s\n\n", fileio_get_zip_method_string (zip_method));
	}
      else
	{
	  fprintf (session.verbose_fp, "- compression method: %d (%s), compression level: %d (%s)\n\n", zip_method,
		   fileio_get_zip_method_string (zip_method), zip_level, fileio_get_zip_level_string (zip_level));
	}

      if (skip_activelog)
	{
	  // unreachable. skip_activelog option is obsoleted.
	  assert (!skip_activelog);
	  fprintf (session.verbose_fp, "- not include active log.\n\n");
	}

#if defined(SERVER_MODE)
      if (sleep_msecs > 0 || prm_get_integer_value (PRM_ID_IO_BACKUP_SLEEP_MSECS) > 0)
	{
	  int sleep_nsecs;

	  if (sleep_msecs > 0)	/* priority 1 */
	    {
	      sleep_nsecs = sleep_msecs * 1000;
	    }
	  else if (prm_get_integer_value (PRM_ID_IO_BACKUP_SLEEP_MSECS) > 0)	/* priority 2 */
	    {
	      sleep_nsecs = prm_get_integer_value (PRM_ID_IO_BACKUP_SLEEP_MSECS) * 1000;
	    }
	  else
	    {
	      sleep_nsecs = 0;
	    }

	  if (sleep_nsecs > 0)
	    {
	      fprintf (session.verbose_fp, "- sleep %d millisecond per 1M read.\n\n", sleep_nsecs / 1000);
	    }
	}
#endif

      backup_start_time = time (NULL);
      (void) ctime_r (&backup_start_time, time_val);
      fprintf (session.verbose_fp, "- backup start time: %s\n", time_val);
      fprintf (session.verbose_fp, "- number of permanent volumes: %d\n\n", num_perm_vols);

      fprintf (session.verbose_fp, "- HA apply info: %s %lld %lld %d\n\n", log_Gl.hdr.prefix_name,
	       (long long int) log_Gl.hdr.db_creation, (long long int) log_Gl.hdr.smallest_lsa_at_last_chkpt.pageid,
	       (int) log_Gl.hdr.smallest_lsa_at_last_chkpt.offset);

      fprintf (session.verbose_fp, "- backup progress status\n\n");
      fprintf (session.verbose_fp, "-----------------------------------------------------------------------------\n");
      fprintf (session.verbose_fp, " volume name                  | # of pages | backup progress status    | done \n");
      fprintf (session.verbose_fp, "-----------------------------------------------------------------------------\n");
    }

  /* Begin backing up in earnest */
  assert (!skip_activelog);
  session.bkup.bkuphdr->skip_activelog = skip_activelog;

  if (fileio_start_backup (thread_p, log_Db_fullname, &log_Gl.hdr.db_creation, backup_level, &bkup_start_lsa,
			   &chkpt_lsa, all_bkup_info, &session, zip_method, zip_level) == NULL)
    {
      error_code = ER_FAILED;
      goto error;
    }

  if (separate_keys)
    {
      db_nopath_name_p = fileio_get_base_file_name (log_Db_fullname);
      fileio_make_backup_name (bkpath_without_units, db_nopath_name_p, session.bkup.current_path, backup_level,
			       FILEIO_NO_BACKUP_UNITS);
      tde_make_keys_file_fullname (separate_mk_path, bkpath_without_units, true);
      /* Keep mounting mk file to be exclusive with other tools */
      error_code = tde_copy_keys_file (thread_p, separate_mk_path, mk_path, false, true);
      if (error_code != NO_ERROR)
	{
	  goto error;
	}
    }

  /* Backup every volume */
  volid = LOG_DBTDE_KEYS_VOLID;
  do
    {
      switch (volid)
	{
	case LOG_DBTDE_KEYS_VOLID:
	  if (separate_keys)
	    {
	      /* already backuped up as a separate file */
	      volid++;
	      continue;
	    }
	  else
	    {
	      from_vlabel = mk_path;
	    }
	  break;
	case LOG_DBVOLINFO_VOLID:
	  fileio_make_volume_info_name (vol_backup, log_Db_fullname);
	  from_vlabel = vol_backup;
	  break;
	case LOG_DBLOG_INFO_VOLID:
	case LOG_DBLOG_BKUPINFO_VOLID:
	  /*
	   * These information volumes are backed-up at the very end.
	   */
	  volid++;
	  continue;
	case LOG_DBLOG_ACTIVE_VOLID:
	  /*
	   * Archiving log active must be done after all data volumes
	   * have been backed up (in order to insure we get all of the
	   * log records needed to restore consistency).
	   */
	  volid = LOG_DBFIRST_VOLID;
	  continue;
	default:
	  from_vlabel = fileio_get_volume_label (volid, PEEK);
	  break;
	}

      if (volid >= LOG_DBFIRST_VOLID)
	{
	  error_code = logpb_backup_for_volume (thread_p, volid, &chkpt_lsa, &session, isincremental);
	  if (error_code != NO_ERROR)
	    {
	      goto error;
	    }
	  disk_lock_extend ();
	  volid = fileio_find_next_perm_volume (thread_p, volid);
	  disk_unlock_extend ();
	}
      else
	{
	  error_code = fileio_backup_volume (thread_p, &session, from_vlabel, volid, -1, false);
	  if (error_code != NO_ERROR)
	    {
	      /* keys file can be omitted */
	      if (volid != LOG_DBTDE_KEYS_VOLID)
		{
		  goto error;
		}
	    }
	  volid++;
	}
      /* in case an error occurs, we must destroy our partial backup */
      bkup_in_progress = true;
    }
  while (volid != NULL_VOLID);

#if defined(SERVER_MODE)
  /*
   * Only when in client/server, we may need to force an archive
   * of the current log active if there were any active transactions
   * before or during the backup.
   * This is to insure we have enough log records to restore consistency
   * to the database in the event a restore with no other log archives
   * is needed.
   */
  LOG_CS_ENTER (thread_p);
  if (LSA_LT (&chkpt_lsa, &log_Gl.hdr.append_lsa) || log_Gl.hdr.append_lsa.pageid > LOGPB_NEXT_ARCHIVE_PAGE_ID)
    {
      logpb_archive_active_log (thread_p);
    }

  last_arv_needed = log_Gl.hdr.nxarv_num - 1;
  LOG_CS_EXIT (thread_p);

  if (last_arv_needed >= first_arv_needed)
    {
      error_code = logpb_backup_needed_archive_logs (thread_p, &session, first_arv_needed, last_arv_needed);
      if (error_code != NO_ERROR)
	{
	  goto error;
	}
    }
#else /* SERVER_MODE */
  /*
   * In stand-alone, there can be no other activity modifying the log
   * so we do not have to keep any log records to be consistent.
   */
  first_arv_needed = -1;
  last_arv_needed = -1;
#endif /* SERVER_MODE */

  /* at here, diable multi-thread usage for fast Log copy */
  session.read_thread_info.num_threads = 1;

  er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, ER_LOG_BACKUP_CS_ENTER, 1, log_Name_active);

  LOG_CS_ENTER (thread_p);

#if defined(SERVER_MODE)

  if (last_arv_needed < log_Gl.hdr.nxarv_num - 1)
    {
      error_code = logpb_backup_needed_archive_logs (thread_p, &session, last_arv_needed + 1, log_Gl.hdr.nxarv_num - 1);
      if (error_code != NO_ERROR)
	{
	  LOG_CS_EXIT (thread_p);
	  er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, ER_LOG_BACKUP_CS_EXIT, 1, log_Name_active);
	  goto error;
	}

      last_arv_needed = log_Gl.hdr.nxarv_num - 1;
    }
#endif

  if (fileio_is_volume_exist (log_Name_info) == true)
    {
      error_code = fileio_backup_volume (thread_p, &session, log_Name_info, LOG_DBLOG_INFO_VOLID, -1, false);
      if (error_code != NO_ERROR)
	{
	  LOG_CS_EXIT (thread_p);
	  er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, ER_LOG_BACKUP_CS_EXIT, 1, log_Name_active);
	  goto error;
	}
    }

  /*
   * We must store the final bkvinf file at the very end of the backup
   * to have the best chance of having all of the information in it.
   * Note: that there is a window that the last bkvinf entry still not being
   * there if a new backup volume is needed while writing this volume.
   * However, in this case, then restore will ask the user for the
   * subsequent backup unit num.
   */
  error_code = logpb_update_backup_volume_info (log_Name_bkupinfo);
  if (error_code != NO_ERROR)
    {
      LOG_CS_EXIT (thread_p);
      er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, ER_LOG_BACKUP_CS_EXIT, 1, log_Name_active);
      goto error;
    }

  /* Clear log header information regarding previous backups */
  logpb_initialize_backup_info (&log_Gl.hdr);

  /* Save additional info and metrics from this backup */
  log_Gl.hdr.bkinfo[backup_level].bkup_attime = session.bkup.bkuphdr->start_time;

  switch (backup_level)
    {
    case FILEIO_BACKUP_FULL_LEVEL:
    default:
      LSA_COPY (&log_Gl.hdr.bkup_level0_lsa, &chkpt_lsa);
      LSA_SET_NULL (&log_Gl.hdr.bkup_level1_lsa);
      LSA_SET_NULL (&log_Gl.hdr.bkup_level2_lsa);
      log_Gl.hdr.bkinfo[FILEIO_BACKUP_BIG_INCREMENT_LEVEL].bkup_attime = 0;
      log_Gl.hdr.bkinfo[FILEIO_BACKUP_SMALL_INCREMENT_LEVEL].bkup_attime = 0;
      break;

    case FILEIO_BACKUP_BIG_INCREMENT_LEVEL:
      LSA_COPY (&log_Gl.hdr.bkup_level1_lsa, &chkpt_lsa);
      LSA_SET_NULL (&log_Gl.hdr.bkup_level2_lsa);
      log_Gl.hdr.bkinfo[FILEIO_BACKUP_SMALL_INCREMENT_LEVEL].bkup_attime = 0;
      break;

    case FILEIO_BACKUP_SMALL_INCREMENT_LEVEL:
      LSA_COPY (&log_Gl.hdr.bkup_level2_lsa, &chkpt_lsa);
      break;
    }

  /* Now indicate how many volumes were backed up */
  logpb_flush_header (thread_p);

  /* Include active log always. Skipping log active is obsolete. */
  error_code = fileio_backup_volume (thread_p, &session, log_Name_active, LOG_DBLOG_ACTIVE_VOLID, -1, false);
  if (error_code != NO_ERROR)
    {
      LOG_CS_EXIT (thread_p);
      er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, ER_LOG_BACKUP_CS_EXIT, 1, log_Name_active);
      goto error;
    }

  if (fileio_finish_backup (thread_p, &session) == NULL)
    {
      LOG_CS_EXIT (thread_p);
      er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, ER_LOG_BACKUP_CS_EXIT, 1, log_Name_active);
      error_code = ER_FAILED;
      goto error;
    }

  if (delete_unneeded_logarchives != false)
    {
      catmsg = msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_LOG, MSGCAT_LOG_DATABASE_BACKUP_WAS_TAKEN);
      if (catmsg)
	{
	  logpb_remove_archive_logs (thread_p, catmsg);
	}
    }

  LOG_CS_EXIT (thread_p);

  er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, ER_LOG_BACKUP_CS_EXIT, 1, log_Name_active);

  error_code = logpb_update_backup_volume_info (log_Name_bkupinfo);
  if (error_code != NO_ERROR)
    {
      goto error;
    }

  if (session.verbose_fp)
    {
      fprintf (session.verbose_fp, "-----------------------------------------------------------------------------\n\n");
      backup_end_time = time (NULL);
      (void) ctime_r (&backup_end_time, time_val);
      fprintf (session.verbose_fp, "# backup end time: %s\n", time_val);
      switch (backup_level)
	{
	case FILEIO_BACKUP_FULL_LEVEL:
	  str_tmp = "Full";
	  break;
	case FILEIO_BACKUP_BIG_INCREMENT_LEVEL:
	  str_tmp = "Incremental Level 1";
	  break;
	default:
	  assert_release (backup_level == FILEIO_BACKUP_SMALL_INCREMENT_LEVEL);
	  str_tmp = "Incremental Level 2";
	  break;
	}
      fprintf (session.verbose_fp, "[ Database(%s) %s Backup end ]\n", boot_db_name (), str_tmp);
    }

  fileio_abort_backup (thread_p, &session, false);

#if defined(SERVER_MODE)
  LOG_CS_ENTER (thread_p);
  log_Gl.run_nxchkpt_atpageid = saved_run_nxchkpt_atpageid;
  log_Gl.backup_in_progress = false;
  LOG_CS_EXIT (thread_p);
#endif /* SERVER_MODE */

  if (keys_vdes != NULL_VOLDES)
    {
      fileio_dismount (thread_p, keys_vdes);
    }

  return NO_ERROR;

  /* ********* */
error:

  /*
   * Destroy the backup that has been created.
   */
  fileio_abort_backup (thread_p, &session, bkup_in_progress);

#if defined(SERVER_MODE)
  LOG_CS_ENTER (thread_p);
  if (saved_run_nxchkpt_atpageid != NULL_PAGEID)
    {
      log_Gl.run_nxchkpt_atpageid = saved_run_nxchkpt_atpageid;
    }
  log_Gl.backup_in_progress = false;
  LOG_CS_EXIT (thread_p);
#endif /* SERVER_MODE */

  if (keys_vdes != NULL_VOLDES)
    {
      fileio_dismount (thread_p, keys_vdes);
    }

  return error_code;
}

/*
 * log_update_backup_volinfo - UPDATE DISK VERSION OF BKUP VOLINFO FILE
 *
 * return: NO_ERROR if all OK, ER status otherwise
 *
 *   bkupinfo_file_name(in): file name to write info to
 *
 * NOTE: Save the in-memory cache of backup volume names to a file.
 */
static int
logpb_update_backup_volume_info (const char *bkupinfo_file_name)
{
  FILE *backup_volinfo_fp;
  int error_code = NO_ERROR;

  backup_volinfo_fp = fopen (bkupinfo_file_name, "w");
  if (backup_volinfo_fp == NULL)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_CREATE_DBBACKUP_DIRINFO, 1, bkupinfo_file_name);
      error_code = ER_LOG_CREATE_DBBACKUP_DIRINFO;
      return error_code;
    }

  /*
   * If an error occurs while writing, should allow the user to try again.
   */
  error_code = fileio_write_backup_info_entries (backup_volinfo_fp, FILEIO_FIRST_BACKUP_VOL_INFO);
  if (error_code != NO_ERROR)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_CREATE_DBBACKUP_DIRINFO, 1, bkupinfo_file_name);
      fclose (backup_volinfo_fp);
      return error_code;
    }
  fflush (backup_volinfo_fp);
  fclose (backup_volinfo_fp);

  return error_code;
}

/*
 * logpb_check_stop_at_time - Check if the stopat time is valid
 *
 * return: NO_ERROR if valid, ER_FAILED otherwise
 *
 *   session(in):
 *   stop_at(in):
 *   backup_time(in):
 */
static int
logpb_check_stop_at_time (FILEIO_BACKUP_SESSION * session, time_t stop_at, time_t backup_time)
{
  char ctime_buf1[CTIME_MAX], ctime_buf2[CTIME_MAX];
  size_t time_str_len;

  if (stop_at < backup_time)
    {
      ctime_r (&stop_at, ctime_buf1);
      ctime_r (&backup_time, ctime_buf2);

      /* strip '\n' */
      time_str_len = strlen (ctime_buf1);
      if (time_str_len > 0)
	{
	  ctime_buf1[time_str_len - 1] = 0;
	}
      time_str_len = strlen (ctime_buf2);
      if (time_str_len > 0)
	{
	  ctime_buf2[time_str_len - 1] = 0;
	}

      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_UPTODATE_ERROR, 2, ctime_buf1, ctime_buf2);

      return ER_LOG_UPTODATE_ERROR;
    }

  return NO_ERROR;
}

using namespace std;

/*
 * logpb_restore - Restore volume from its backup
 *
 * return: NO_ERROR if all OK, ER status otherwise
 *
 *   db_fullname(in): Full name of the database
 *   logpath(in): Directory where the log volumes reside
 *   prefix_logname(in):
 *   newall_path(in):
 *   ask_forpath(in):
 *   r_args(in):
 *
 * NOTE:Restore a database from its backup files. This function is run
 *              without recovery. The logs must be applied to the restored
 *              volume to finish the full restore process. This is not done
 *              here since the database may have other volumes to restore.
 *              We must lock the active log during the restore to keep other
 *              utilities from trying to use the db files being restored.
 *              This routine leaves the database locked, as there will
 *              probably be a restart following the restore.
 *
 *     This function must be run offline.
 *
 *     Note this function is incomplete.. How to change location of files
 *     during restore... The main problem here is the VOLINFO which has to
 *     be recreated without updating the header of the volumes.
 */
int
logpb_restore (THREAD_ENTRY * thread_p, const char *db_fullname, const char *logpath, const char *prefix_logname,
	       bo_restart_arg * r_args)
{
  FILEIO_BACKUP_SESSION session_storage;
  FILEIO_BACKUP_SESSION *session = NULL;
  const char *nopath_name;	/* Name without path */
  char to_volname[PATH_MAX];	/* Name of a volume (TO) */
  char verbose_to_volname[PATH_MAX];	/* Printable name of a volume (TO) */
  char prev_volname[PATH_MAX];	/* Name of a prev volume (TO) */
  char from_volbackup[PATH_MAX];	/* Name of the backup volume (FROM) */
  VOLID to_volid;
  FILE *backup_volinfo_fp = NULL;	/* Pointer to backup information/directory file */
  int another_vol;
  INT64 db_creation;
  INT64 bkup_match_time = 0;
  PGLENGTH db_iopagesize;
  PGLENGTH log_page_size;
  float db_compatibility;
  PGLENGTH bkdb_iopagesize;
  float bkdb_compatibility;

  FILEIO_RESTORE_PAGE_BITMAP_LIST page_bitmap_list;
  FILEIO_RESTORE_PAGE_BITMAP *page_bitmap = NULL;
  DKNPAGES total_pages;

  FILEIO_BACKUP_LEVEL try_level, start_level;
  bool first_time = true;
  bool remember_pages = false;
  bool error_expected = false;
  bool restore_in_progress = false;	/* true if any vols restored */
  int lgat_vdes = NULL_VOLDES;
  time_t restore_start_time, restore_end_time;
  char time_val[CTIME_MAX];
  int loop_cnt = 0;
  char tmp_logfiles_from_backup[PATH_MAX];
  char bk_mk_path[PATH_MAX];
  char bkpath_without_units[PATH_MAX];
  char backup_dir_path[PATH_MAX];
  char *volume_name_p;
  struct stat stat_buf;
  int error_code = NO_ERROR, success = NO_ERROR;
  bool printtoc;
  INT64 backup_time;
  REL_COMPATIBILITY compat;
  int dummy;

  bool is_prev_volheader_restored = false;
  FILEIO_UNLINKED_VOLINFO_MAP unlinked_volinfo;

  try_level = (FILEIO_BACKUP_LEVEL) r_args->level;
  start_level = try_level;

  memset (&session_storage, 0, sizeof (FILEIO_BACKUP_SESSION));
  memset (verbose_to_volname, 0, PATH_MAX);
  memset (tmp_logfiles_from_backup, 0, PATH_MAX);

  LOG_CS_ENTER (thread_p);

  if (logpb_find_header_parameters (thread_p, true, db_fullname, logpath, prefix_logname, &db_iopagesize,
				    &log_page_size, &db_creation, &db_compatibility, &dummy) == -1)
    {
      db_iopagesize = IO_PAGESIZE;
      log_page_size = LOG_PAGESIZE;
      db_creation = 0;
      db_compatibility = rel_disk_compatible ();
    }

  fileio_page_bitmap_list_init (&page_bitmap_list);

  /*
   * Must lock the database if possible. Would be nice to have a way
   * to lock the db somehow even when the lgat file does not exist.
   */
  if (fileio_is_volume_exist (log_Name_active))
    {
      lgat_vdes = fileio_mount (thread_p, db_fullname, log_Name_active, LOG_DBLOG_ACTIVE_VOLID, true, false);
      if (lgat_vdes == NULL_VOLDES)
	{
	  error_code = ER_FAILED;
	  LOG_CS_EXIT (thread_p);
	  goto error;
	}
    }

  nopath_name = fileio_get_base_file_name (db_fullname);

  /* The enum type can be negative in Windows. */
  while (success == NO_ERROR && try_level >= FILEIO_BACKUP_FULL_LEVEL && try_level < FILEIO_BACKUP_UNDEFINED_LEVEL)
    {
      if (!first_time)
	{
	  /* Prepare to reread bkvinf file restored by higher level */
	  fileio_clear_backup_info_level (FILEIO_BACKUP_FULL_LEVEL, false, FILEIO_FIRST_BACKUP_VOL_INFO);

	  /* Have to match timestamps of lower levels when restoring */
	  bkup_match_time = session->bkup.bkuphdr->previnfo[try_level].at_time;

	  /* Clean up previous restore of higher level */
	  if (fileio_finish_restore (thread_p, session) == NO_ERROR)
	    {
	      success = NO_ERROR;
	    }
	  else
	    {
	      success = ER_FAILED;
	    }

	  assert (try_level != (FILEIO_BACKUP_LEVEL) r_args->level);
	}

      error_code =
	fileio_get_backup_volume (thread_p, db_fullname, logpath, r_args->backuppath, try_level, from_volbackup);
      if (error_code == ER_LOG_CANNOT_ACCESS_BACKUP)
	{
	  error_expected = true;
	  LOG_CS_EXIT (thread_p);
	  goto error;
	}
      else if (error_code != NO_ERROR)
	{
	  LOG_CS_EXIT (thread_p);
	  goto error;
	}

      printtoc = (r_args->printtoc) ? false : true;
      if (fileio_start_restore (thread_p, db_fullname, from_volbackup, db_creation, &bkdb_iopagesize,
				&bkdb_compatibility, &session_storage, try_level, printtoc, bkup_match_time,
				r_args->verbose_file, r_args->newvolpath) == NULL)
	{
	  /* Cannot access backup file.. Restore from backup is cancelled */
	  if (er_errid () == ER_GENERIC_ERROR)
	    {
	      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_CANNOT_ACCESS_BACKUP, 1, from_volbackup);
	    }
	  error_code = ER_LOG_CANNOT_ACCESS_BACKUP;
	  LOG_CS_EXIT (thread_p);
	  goto error;
	}

      memset (session_storage.bkup.log_path, 0, PATH_MAX);
      strncpy_bufsize (session_storage.bkup.log_path, logpath);

      session = &session_storage;

      if (first_time)
	{
	  if (r_args->restore_upto_bktime)
	    {
	      r_args->stopat = (time_t) session->bkup.bkuphdr->end_time;
	    }
	  else if (r_args->stopat > 0)
	    {
	      if (session->bkup.bkuphdr->end_time > 0)
		{
		  backup_time = session->bkup.bkuphdr->end_time;
		}
	      else
		{
		  backup_time = session->bkup.bkuphdr->start_time;
		}
	      error_code = logpb_check_stop_at_time (session, r_args->stopat, (time_t) backup_time);
	      if (error_code != NO_ERROR)
		{
		  LOG_CS_EXIT (thread_p);
		  error_expected = true;
		  goto error;
		}
	    }
	}

      if (first_time && db_iopagesize != bkdb_iopagesize)
	{
	  /*
	   * Pagesize is incorrect. We need to undefine anything that has been
	   * created with old pagesize and start again.
	   * If we do not have a log, we should reset the pagesize and start the
	   * restore process.
	   */
	  if (log_Gl.append.vdes == NULL_VOLDES)
	    {
	      /*
	       * Reset the page size
	       */
	      if (db_set_page_size (bkdb_iopagesize, log_page_size) != NO_ERROR)
		{
		  error_code = ER_FAILED;
		  LOG_CS_EXIT (thread_p);
		  goto error;
		}

	      error_code = logtb_define_trantable_log_latch (thread_p, -1);
	      if (error_code != NO_ERROR)
		{
		  LOG_CS_EXIT (thread_p);
		  goto error;
		}
	    }
	}

      /* Can this be moved to restore_continue? */
      /* Removed strict condition for checking disk compatibility. Check it according to the predefined rules. */
      compat = rel_get_disk_compatible (bkdb_compatibility, NULL);
      if (compat != REL_FULLY_COMPATIBLE && compat != REL_BACKWARD_COMPATIBLE)
	{
	  /* Database is incompatible with current release */
	  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_BKUP_INCOMPATIBLE, 2, rel_name (),
		  rel_release_string ());
	  error_code = ER_LOG_BKUP_INCOMPATIBLE;
	  LOG_CS_EXIT (thread_p);
	  goto error;
	}

      if (session->verbose_fp)
	{
	  if (first_time)
	    {
	      fprintf (session->verbose_fp, "\n[ Database(%s) Restore (level = %d) start ]\n\n", boot_db_name (),
		       r_args->level);

	      restore_start_time = time (NULL);
	      (void) ctime_r (&restore_start_time, time_val);
	      fprintf (session->verbose_fp, "- restore start time: %s\n", time_val);
	      fprintf (session->verbose_fp, "- restore steps: %d \n", r_args->level + 1);
	    }

	  fprintf (session->verbose_fp, " step %1d) restore using (level = %d) backup data\n", ++loop_cnt, try_level);
	  fprintf (session->verbose_fp, "\n");

	  fprintf (session->verbose_fp, "- restore progress status (using level = %d backup data)\n", try_level);
	  fprintf (session->verbose_fp,
		   " -----------------------------------------------------------------------------\n");
	  fprintf (session->verbose_fp,
		   " volume name                  | # of pages |  restore progress status  | done \n");
	  fprintf (session->verbose_fp,
		   " -----------------------------------------------------------------------------\n");
	}

      /* add new bkvinf entry into cache data */
      fileio_add_volume_to_backup_info (session->bkup.vlabel, try_level, session->bkup.bkuphdr->unit_num,
					FILEIO_SECOND_BACKUP_VOL_INFO);

      if (first_time)
	{
	  LSA_COPY (&session->bkup.last_chkpt_lsa, &session->bkup.bkuphdr->chkpt_lsa);

	  /* 
	   * The tde key file (_keys) which is going to be used during restart
	   * is the thing in the first time (the highest level).
	   */

	  /* If backup path is not a directory, extract backup key file on the directory that contains the backup path */
	  if (stat (session->bkup.current_path, &stat_buf) != 0)
	    {
	      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_CANNOT_ACCESS_BACKUP, 1,
		      session->bkup.current_path);
	      error_code = ER_LOG_CANNOT_ACCESS_BACKUP;
	      LOG_CS_EXIT (thread_p);
	      goto error;
	    }
	  else if (S_ISDIR (stat_buf.st_mode))
	    {
	      memcpy (backup_dir_path, session->bkup.current_path, PATH_MAX);
	    }
	  else
	    {
	      /* it might be pipe, and others like raw device is not tested */
	      fileio_get_directory_path (backup_dir_path, session->bkup.current_path);
	    }

	  fileio_make_backup_name (bkpath_without_units, nopath_name, backup_dir_path,
				   (FILEIO_BACKUP_LEVEL) r_args->level, FILEIO_NO_BACKUP_UNITS);
	  tde_make_keys_file_fullname (bk_mk_path, bkpath_without_units, true);
	  if (r_args->keys_file_path[0] == '\0')	/* the path given by user is prioritized */
	    {
	      memcpy (r_args->keys_file_path, bk_mk_path, PATH_MAX);
	    }
	  else
	    {
	      /* If the keys file is given, check if it is valid. */
	      int vdes =
		fileio_mount (thread_p, boot_db_full_name (), r_args->keys_file_path, LOG_DBTDE_KEYS_VOLID, false,
			      false);
	      if (vdes == NULL_VOLDES)
		{
		  ASSERT_ERROR_AND_SET (error_code);
		  LOG_CS_EXIT (thread_p);
		  error_expected = true;
		  goto error;
		}

	      if (tde_validate_keys_file (vdes) == false)
		{
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TDE_INVALID_KEYS_FILE, 1, r_args->keys_file_path);
		  error_code = ER_TDE_INVALID_KEYS_FILE;
		  fileio_dismount (thread_p, vdes);
		  LOG_CS_EXIT (thread_p);
		  error_expected = true;
		  goto error;
		}

	      fileio_dismount (thread_p, vdes);
	    }
	}

      while (success == NO_ERROR)
	{
	  another_vol = fileio_get_next_restore_file (thread_p, session, to_volname, &to_volid);
	  if (another_vol == 1)
	    {
	      if (session->verbose_fp)
		{
		  strcpy (verbose_to_volname, to_volname);
		}

	      if (to_volid == LOG_DBLOG_ACTIVE_VOLID || to_volid == LOG_DBLOG_INFO_VOLID
		  || to_volid == LOG_DBLOG_ARCHIVE_VOLID)
		{
		  /* rename _lgat to _lgat_tmp name */
		  fileio_make_temp_log_files_from_backup (tmp_logfiles_from_backup, to_volid,
							  (FILEIO_BACKUP_LEVEL) r_args->level, to_volname);
		  volume_name_p = tmp_logfiles_from_backup;
		}
	      else if (to_volid == LOG_DBTDE_KEYS_VOLID)
		{
		  /* backup mk file is extracted on the backup volume path */
		  volume_name_p = bk_mk_path;
		}
	      else
		{
		  volume_name_p = to_volname;
		}

	      restore_in_progress = true;
	      if (start_level > FILEIO_BACKUP_FULL_LEVEL)
		{
		  remember_pages = true;
		}

	      /*
	       * Another volume/file to restore
	       */
	      switch (to_volid)
		{
		case LOG_DBLOG_BKUPINFO_VOLID:
		case LOG_DBLOG_ACTIVE_VOLID:
		case LOG_DBLOG_INFO_VOLID:
		case LOG_DBVOLINFO_VOLID:
		case LOG_DBLOG_ARCHIVE_VOLID:
		case LOG_DBTDE_KEYS_VOLID:

		  is_prev_volheader_restored = false;

		  /* We can only take the most recent information, and we do not want to overwrite it with out of data
		   * information from earlier backups.  This is because we are applying the restoration in reverse time
		   * order. */
		  if (!first_time)
		    {
		      /* Need to skip over this volume in the backup */
		      success = fileio_skip_restore_volume (thread_p, session);
		      if (success != NO_ERROR)
			{
			  success = ER_FAILED;
			  error_code = ER_FAILED;
			  LOG_CS_EXIT (thread_p);
			  goto error;
			}
		      else
			{
			  success = NO_ERROR;
			  continue;
			}
		    }
		  break;

		default:
		  break;
		}

	      /* we need to tell fileio_restore_volume to avoid tracking the pages for some volids. */
	      if (to_volid < LOG_DBFIRST_VOLID)
		{
		  remember_pages = false;
		}
	      else
		{
		  total_pages = (DKNPAGES) CEIL_PTVDIV (session->dbfile.nbytes, IO_PAGESIZE);
		  /*
		   * Create a page_bitmap to remember the id's of pages
		   * that have been written. We only need to write the page
		   * (once) from the most recent backup.
		   */
		  page_bitmap = fileio_page_bitmap_list_find (&page_bitmap_list, to_volid);
		  if (page_bitmap == NULL)
		    {
		      page_bitmap = fileio_page_bitmap_create (to_volid, total_pages);
		      if (page_bitmap == NULL)
			{
			  goto error;
			}
		      fileio_page_bitmap_list_add (&page_bitmap_list, page_bitmap);
		    }
		}

	      success =
		fileio_restore_volume (thread_p, session, volume_name_p, verbose_to_volname, prev_volname, page_bitmap,
				       remember_pages, is_prev_volheader_restored, unlinked_volinfo);

	      if (success != NO_ERROR)
		{
		  break;
		}

	      if (volume_name_p == tmp_logfiles_from_backup)
		{
		  // when an archive exists, always respect it.
		  // when the active exists and the next archive of it does not, use it to restore.
		  bool is_backup_log_useful = false;

		  if (stat (to_volname, &stat_buf) != 0 && stat (tmp_logfiles_from_backup, &stat_buf) == 0)
		    {
		      is_backup_log_useful = true;

		      if (to_volid == LOG_DBLOG_ACTIVE_VOLID
			  && !logpb_is_log_active_from_backup_useful (thread_p, tmp_logfiles_from_backup, db_fullname))
			{
			  // log active from backup is useless since it is older than the available log archives
			  is_backup_log_useful = false;
			}
		    }

		  if (is_backup_log_useful)
		    {
		      if (to_volid == LOG_DBLOG_ACTIVE_VOLID && lgat_vdes != NULL_VOLDES)
			{
			  fileio_dismount (thread_p, lgat_vdes);
			  lgat_vdes = NULL_VOLDES;
			}

		      os_rename_file (tmp_logfiles_from_backup, to_volname);
		    }
		  else
		    {
		      unlink (tmp_logfiles_from_backup);
		    }

		  tmp_logfiles_from_backup[0] = '\0';
		}

	      volume_name_p = NULL;
	    }
	  else if (another_vol == 0)
	    {
	      break;
	    }
	  else
	    {
	      success = ER_FAILED;
	      break;
	    }
	}

      /* if the device type is FILEIO_BACKUP_VOL_DEVICE, the end time of backup is loaded during the last
       * fileio_get_next_restore_file() call, so we must check if the stopat time is valid. */
      if (first_time && session->bkup.dtype == FILEIO_BACKUP_VOL_DEVICE)
	{
	  if (r_args->restore_upto_bktime)
	    {
	      r_args->stopat = (time_t) session->bkup.bkuphdr->end_time;
	    }
	  else if (r_args->stopat > 0)
	    {
	      error_code = logpb_check_stop_at_time (session, r_args->stopat, (time_t) session->bkup.bkuphdr->end_time);
	      if (error_code != NO_ERROR)
		{
		  LOG_CS_EXIT (thread_p);
		  error_expected = true;
		  goto error;
		}
	    }
	}

      first_time = false;

      if (session->verbose_fp)
	{
	  fprintf (session->verbose_fp,
		   " -----------------------------------------------------------------------------\n\n");
	}

      try_level = (FILEIO_BACKUP_LEVEL) (try_level - 1);
    }

  // Incremental backup volumes often do not include volume header pages.
  // Accordingly, it is necessary to set unlinked volumes after all volume header pages are restored.
  // *INDENT-OFF*
  for (const auto &[volid, volnames] : unlinked_volinfo)
    {
      VOLID prev_volid;
      int prev_vdes;
      const char *prev_volname, *volname;

      volname = volnames.first.c_str();
      prev_volname = volnames.second.c_str();

      prev_volid = fileio_find_previous_perm_volume (thread_p, volid);
      prev_vdes = fileio_mount (thread_p, NULL, prev_volname, prev_volid, false, false);
      if (prev_vdes == NULL_VOLDES)
	{
	  goto error;
	}

      if (disk_set_link (thread_p, prev_volid, volid, volname, false, DISK_FLUSH_AND_INVALIDATE) != NO_ERROR)
	{
	  fileio_dismount (thread_p, prev_vdes);
	  goto error;
	}

      fileio_dismount (thread_p, prev_vdes);

      _er_log_debug (ARG_FILE_LINE, "RESTOREDB: [FIXED UNLINK] volid=%d, vol=%s, prev_vol=%s", volid, volname,
		     prev_volname);
    }
  // *INDENT-ON*

  /* make bkvinf file */
  fileio_make_backup_volume_info_name (from_volbackup, logpath, nopath_name);
  backup_volinfo_fp = fopen (from_volbackup, "w");
  if (backup_volinfo_fp != NULL)
    {
      fileio_write_backup_info_entries (backup_volinfo_fp, FILEIO_SECOND_BACKUP_VOL_INFO);
      fclose (backup_volinfo_fp);
    }

  if (session != NULL)
    {
      if (session->verbose_fp)
	{
	  restore_end_time = time (NULL);
	  (void) ctime_r (&restore_end_time, time_val);
	  fprintf (session->verbose_fp, "- restore end time: %s\n", time_val);
	  fprintf (session->verbose_fp, "[ Database(%s) Restore (level = %d) end ]\n", boot_db_name (), r_args->level);
	}

      if (fileio_finish_restore (thread_p, session) == NO_ERROR)
	{
	  error_code = NO_ERROR;
	}
      else
	{
	  error_code = ER_FAILED;
	}
    }

  LOG_CS_EXIT (thread_p);

  fileio_page_bitmap_list_destroy (&page_bitmap_list);

  fileio_finalize_backup_info (FILEIO_SECOND_BACKUP_VOL_INFO);

  if (success != NO_ERROR)
    {
      return success;
    }
  else
    {
      return error_code;
    }

  /* **** */
error:
  if (restore_in_progress)
    {
      /*
       * We have probably already restored something to their database
       * and therefore they need to be sure and try another restore until
       * they succeed.
       */
      fprintf (stdout, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_LOG, MSGCAT_LOG_READ_ERROR_DURING_RESTORE),
	       session->bkup.name,
	       ((session->bkup.bkuphdr == NULL) ? FILEIO_INITIAL_BACKUP_UNITS : session->bkup.bkuphdr->unit_num),
	       to_volname, session->dbfile.volid);

      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_MAYNEED_MEDIA_RECOVERY, 1, log_Name_bkupinfo);
      error_code = ER_LOG_MAYNEED_MEDIA_RECOVERY;
    }

  if (backup_volinfo_fp != NULL)
    {
      fclose (backup_volinfo_fp);
    }

  if (session != NULL)
    {
      fileio_abort_restore (thread_p, session);
    }

  fileio_page_bitmap_list_destroy (&page_bitmap_list);

  if (lgat_vdes != NULL_VOLDES)
    {
      fileio_dismount (thread_p, lgat_vdes);
    }

  if (!error_expected)
    {
      logpb_fatal_error (thread_p, false, ARG_FILE_LINE, "logpb_restore");
    }

  if (tmp_logfiles_from_backup[0] != '\0')
    {
      unlink (tmp_logfiles_from_backup);
    }

  return error_code;
}

/*
 * logpb_start_where_path - Start where paths for copy/rename volumes
 *
 * return: NO_ERROR if all OK, ER status otherwise
 *
 *   to_db_fullname(in): Full name of the new database
 *   toext_path(in): A path if all volumes are placed in one
 *                               place. If NULL is given,
 *                                 - If file "fileof_vols_and_wherepaths" is
 *                                   given, the path is found in this file.
 *                                 - Each volume is copied to same place where
 *                                   the volume reside.
 *                               This parameter should be NULL, if the above
 *                               file is given.
 *   toext_name(in/out): Name to be used for volume extensions when
 *                               individual volume entries are not given in
 *                               file "fileof_vols_and_wherepaths".
 *   ext_path(in/out): Location for entries
 *                               Set as a side effect to either:
 *                                 - toext_path when all vols are placed in
 *                                   the same location.
 *                                 - alloc_extpath when volumes are copied or
 *                                   renamed at the same location as the
 *                                   original volumes.
 *                                 - NULL when file of "fileof_vols_and_
 *                                   wherepaths" is given.
 *   alloc_extpath(in/out): Set as a side effect to newly malloced area
 *                               when the volumes are copied/renamed in place.
 *                               If an area is allocated, it should be freed
 *                               using free_and_init.
 *   fileof_vols_and_wherepaths(in): A file is given when the user decides to
 *                               control the copy/rename of the volume by
 *                               individual bases. That is, user decides to
 *                               spread the volumes over several locations and
 *                               or to name the volumes.
 *                               Each volume entry consists of:
 *                                 volid from_fullvolname to_fullvolname
 *   where_paths_fp(in/out): Set as a side effect to file descriptor of
 *                               the given file. The caller must close the
 *                               file, when a pointer is returned.
 *
 * NOTE: Prepare variables to start finding paths for copying or
 *              renaming volumes.
 */
static int
logpb_start_where_path (const char *to_db_fullname, const char *toext_path, const char **toext_name, char **ext_path,
			char **alloc_extpath, const char *fileof_vols_and_wherepaths, FILE ** where_paths_fp)
{
  /*
   * Get the name of extensions to be used when "fileof_vols_and_wherepaths"
   * is not given.
   */

  *toext_name = fileio_get_base_file_name (to_db_fullname);
  *alloc_extpath = NULL;
  *ext_path = NULL;
  *where_paths_fp = NULL;

  /*
   * Where are the volumes going to be placed ?
   *   - All or them at same place: toext_path or the same as database
   *   - The user indicated where to create each individual copy
   */

  if (fileof_vols_and_wherepaths != NULL)
    {
      /*
       * The user seems to want to spread the volumes over several locations.
       * User must indicate where each individual database volume is going to
       * be placed.
       */
      *where_paths_fp = fopen (fileof_vols_and_wherepaths, "r");
      if (*where_paths_fp == NULL)
	{
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_USER_FILE_UNKNOWN, 1,
			       fileof_vols_and_wherepaths);
	  return ER_LOG_USER_FILE_UNKNOWN;
	}
    }
  /*
   * Either all copies are created at the same place, or each individual
   * volumes is copied at the original volume location
   */
  if (toext_path == NULL)
    {
      /*
       * Each volume is copied to the same place where the original volume
       * resides
       */
      *alloc_extpath = (char *) malloc (PATH_MAX);
      if (*alloc_extpath == NULL)
	{
	  return ER_FAILED;
	}
      *ext_path = *alloc_extpath;
    }
  else
    {
      /* All the volumes are created in one location */
      *ext_path = (char *) toext_path;
    }

  return NO_ERROR;
}

/*
 * logpb_next_where_path - Find next path for copy/rename next volume
 *
 * return: NO_ERROR if all OK, ER status otherwise
 *
 *   to_db_fullname(in): Full name of the new database
 *   toext_path(in): A path if all volumes are placed in one
 *                               place. If NULL is given,
 *                                 - If file "fileof_vols_and_wherepaths" is
 *                                   given, the path is found in this file.
 *                                 - Each volume is copied to same place where
 *                                   the volume reside.
 *                               This parameter should be NULL, if the above
 *                               file is given.
 *   ext_name(in): Name to be used for volume extensions when
 *                               individual volume entries are not given in
 *                               file "fileof_vols_and_wherepaths".
 *   ext_path(in): Location for entries
 *   fileof_vols_and_wherepaths(in): A file is given when the user decides to
 *                               control the copy/rename of the volume by
 *                               individual bases. That is, user decides to
 *                               spread the volumes over several locations and
 *                               or to name the volumes.
 *                               Each volume entry consists of:
 *                                 volid from_fullvolname to_fullvolname
 *   where_paths_fp(in): Pointer to above file if any or NULL
 *   num_perm_vols(in): Number of permanent volumes in the database.
 *   volid(in): Next volume that must be processes.
 *   from_volname(in/out): Current name of volume.
 *   to_volname(in): New name to be used for the volume.
 *
 * NOTE: Get the name of next volume to be processed.
 */
static int
logpb_next_where_path (const char *to_db_fullname, const char *toext_path, const char *ext_name, char *ext_path,
		       const char *fileof_vols_and_wherepaths, FILE * where_paths_fp, int num_perm_vols,
		       VOLID volid, char *from_volname, char *to_volname)
{
  const char *current_vlabel;
  int from_volid;
#if !defined(WINDOWS)
  char link_path[PATH_MAX];
  struct stat stat_buf;
#endif
  int error_code = NO_ERROR;
  char format_string[64];

  current_vlabel = fileio_get_volume_label (volid, PEEK);
  sprintf (format_string, "%%d %%%ds %%%ds", PATH_MAX - 1, PATH_MAX - 1);

  /*
   * If a file for paths was given, get the name of the "to" volume from it
   */
  if (where_paths_fp != NULL)
    {
      if (fscanf (where_paths_fp, format_string, &from_volid, from_volname, to_volname) != 3)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_USER_FILE_WITHOUT_ENOUGH_ENTRIES, 2,
		  fileof_vols_and_wherepaths, num_perm_vols);
	  return ER_LOG_USER_FILE_WITHOUT_ENOUGH_ENTRIES;
	}
      /*
       * Primary volume must be identical to database name
       */
      if (volid == LOG_DBFIRST_VOLID && strcmp (to_volname, to_db_fullname) != 0)
	{

#if defined(WINDOWS)
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_USER_FILE_INCORRECT_PRIMARY_VOLNAME, 8,
		  fileof_vols_and_wherepaths, volid + 1, from_volid, from_volname, to_volname, (int) volid,
		  current_vlabel, to_db_fullname);
	  return ER_ERROR_SEVERITY;
#else /* WINDOWS */
	  error_code = fileio_symlink (to_volname, to_db_fullname, true);
	  if (error_code != NO_ERROR)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_USER_FILE_INCORRECT_PRIMARY_VOLNAME, 8,
		      fileof_vols_and_wherepaths, volid + 1, from_volid, from_volname, to_volname, (int) volid,
		      current_vlabel, to_db_fullname);
	      return error_code;
	    }

	  strcpy (to_volname, to_db_fullname);
#endif /* WINDOWS */
	}
      else
	{
	  if (volid != from_volid || util_compare_filepath (current_vlabel, from_volname) != 0)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_USER_FILE_UNORDERED_ENTRIES, 7,
		      fileof_vols_and_wherepaths, volid + 1, from_volid, from_volname, to_volname, (int) volid,
		      current_vlabel);
	      return ER_LOG_USER_FILE_UNORDERED_ENTRIES;
	    }

#if !defined(WINDOWS)
	  if (stat (to_volname, &stat_buf) != -1)
	    {
	      if (S_ISCHR (stat_buf.st_mode))
		{
		  fileio_get_directory_path (ext_path, to_db_fullname);
		  fileio_make_volume_ext_name (link_path, ext_path, ext_name, volid);
		  error_code = fileio_symlink (to_volname, link_path, true);
		  if (error_code != NO_ERROR)
		    {
		      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_BO_CANNOT_CREATE_LINK, 2, to_volname, link_path);
		      return error_code;
		    }

		  strcpy (to_volname, link_path);
		}
	    }
#endif /* !WINDOWS */
	}
    }
  else
    {
      /*
       * The decision is done consulting the arguments of the function
       */

      /*
       * Primary volume must be identical to database name
       */
      if (volid == LOG_DBFIRST_VOLID)
	{
	  strcpy (to_volname, to_db_fullname);
	}
      else
	{
	  if (toext_path == NULL)
	    {
	      /*
	       * The volume is copied to the same place where the original volume
	       * resides
	       */
	      if (fileio_get_directory_path (ext_path, current_vlabel) == NULL)
		{
		  ext_path[0] = '\0';
		}
	    }
	  fileio_make_volume_ext_name (to_volname, ext_path, ext_name, volid);
	}
      strcpy (from_volname, current_vlabel);
    }

  return NO_ERROR;
}

/*
 * logpb_copy_volume - Copy a volume
 *
 * return: NO_ERROR if all OK, ER status otherwise
 *
 *   from_volid(in): The identifier of the volume to be copied
 *   to_volname(in): Name of the new volume
 *   db_creation(in): Creation timestamp for the volume
 *   to_volchkpt_lsa(in): Checkpoint location to be used in the new volume
 *
 * NOTE: Copy the volume identified by "from_volid" to "tonew_volname".
 */
static int
logpb_copy_volume (THREAD_ENTRY * thread_p, VOLID from_volid, const char *to_volname, INT64 * db_creation,
		   LOG_LSA * to_volchkpt_lsa)
{
  int from_vdes, to_vdes;	/* Descriptor for "from" and "to" volumes */
  DKNPAGES npages;		/* Size of the volume */
  int error_code = NO_ERROR;

  /* Find the current pages of the volume and its descriptor */

  npages = xdisk_get_total_numpages (thread_p, from_volid);
  from_vdes = fileio_get_volume_descriptor (from_volid);

  /* Flush all dirty pages */
  logpb_flush_pages_direct (thread_p);

  error_code = pgbuf_flush_all_unfixed (thread_p, from_volid);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  if (fileio_synchronize (thread_p, from_vdes, fileio_get_volume_label (from_vdes, PEEK),
			  FILEIO_SYNC_ALSO_FLUSH_DWB) != from_vdes)
    {
      return ER_FAILED;
    }

  /* Copy the database volume and initialize recovery information on it */

  to_vdes = fileio_copy_volume (thread_p, from_vdes, npages, to_volname, LOG_DBCOPY_VOLID, true);
  if (to_vdes == NULL_VOLDES)
    {
      return ER_FAILED;
    }

  /*
   * Change the name of the volume, its database creation time and its
   * checkpoint lsa
   */

  (void) disk_set_creation (thread_p, LOG_DBCOPY_VOLID, to_volname, db_creation, to_volchkpt_lsa, false,
			    DISK_DONT_FLUSH);

  logpb_flush_pages_direct (thread_p);
  (void) pgbuf_flush_all_unfixed_and_set_lsa_as_null (thread_p, LOG_DBCOPY_VOLID);

  /*
   * To set the LSA of temp volume to special temp LSA (-2,-2).
   * Especially, in case of copydb.
   */
  (void) logpb_check_and_reset_temp_lsa (thread_p, LOG_DBCOPY_VOLID);

  (void) pgbuf_invalidate_all (thread_p, LOG_DBCOPY_VOLID);
  fileio_dismount (thread_p, to_vdes);

  return NO_ERROR;
}

/*
 * logpb_copy_database - Copy a database
 *
 * return: NO_ERROR if all OK, ER status otherwise
 *
 *   num_perm_vols(in): Number of permanent volume for the database
 *   to_db_fullname(in): Full name of the new database
 *   to_logpath(in): Directory where the log volumes reside
 *   to_prefix_logname(in): Name of the log volumes. It is usually set
 *                      the same as database name. For example, if the value
 *                      is equal to "db", the names of the log volumes created
 *                      are as follow:
 *                      Active_log      = db_logactive
 *                      Archive_logs    = db_logarchive.0
 *                                        db_logarchive.1
 *                                             .
 *                                             .
 *                                             .
 *                                        db_logarchive.n
 *                      Log_information = db_loginfo
 *                      Database Backup = db_backup
 *   toext_path(in): A path is included if all volumes are placed in one
 *                      place/directory. If NULL is given,
 *                      - If file "fileof_vols_and_wherepaths" is given, the
 *                        path is found in this file.
 *                      - Each volume is copied to same place where the volume
 *                        resides.
 *                      NOTE: This parameter should be NULL, if the above file
 *                            is given.
 *   fileof_vols_and_copypaths(in): A file is given when the user decides to
 *                               control the copy/rename of the volume by
 *                               individual bases. That is, user decides to
 *                               spread the volumes over several locations and
 *                               or to label the volumes with specific names.
 *                               Each volume entry consists of:
 *                                 volid from_fullvolname to_fullvolname
 *
 * NOTE: A new log volume is created for the new database, and each
 *              data volume of the database is copied to the new database.
 *              Recovery information from the old database is not included
 *              onto the copy (i.e., new database). Backups of the old
 *              are not included either. Thus, a backup of the new database is
 *              recommended after the copy is done.
 *
 * NOTE:        This function must be run offline. That is, should not be run
 *              when there are multiusers in the system.
 */
int
logpb_copy_database (THREAD_ENTRY * thread_p, VOLID num_perm_vols, const char *to_db_fullname, const char *to_logpath,
		     const char *to_prefix_logname, const char *toext_path, const char *fileof_vols_and_copypaths)
{
  LOG_RECORD_HEADER *eof;	/* End of log record */
  char from_volname[PATH_MAX];	/* Name of new volume */
  FILE *fromfile_paths_fp = NULL;	/* Pointer to open file for location of copy files */
  int fromfile_volid;		/* Volume identifier as an integer */
  int to_vdes;			/* A volume descriptor */
  char to_volname[PATH_MAX];	/* Name of "to" volume */
  LOG_PAGE *to_malloc_log_pgptr = NULL;	/* Log header page "to" log */
  LOG_HEADER *to_hdr;		/* Log header for "to" log */
  FILE *to_volinfo_fp = NULL;	/* Pointer to new volinfo file */
  char *alloc_extpath = NULL;	/* Copy path for specific volume */
  const char *ext_name;
  char *ext_path;
  VOLID volid;
  INT64 db_creation;
  LOG_PHY_PAGEID phy_pageid;
  bool stop_eof = false;
  const char *catmsg;
  int error_code;
  char format_string[64];
  FILEIO_WRITE_MODE write_mode;
  char from_mk_path[PATH_MAX] = { 0, };
  char to_mk_path[PATH_MAX] = { 0, };

  db_creation = time (NULL);

  /*
   * Make sure that the length name of the volumes are OK
   */

  error_code = logpb_verify_length (to_db_fullname, to_logpath, to_prefix_logname);
  if (error_code != NO_ERROR)
    {
      /* Names are too long */
      return error_code;
    }

  /* Do not use DWB at copy DB. In case of crash the data may be recreated from log. */
  dwb_destroy (thread_p);

  /*
   * Create the DATABASE VOLUME INFORMATION file
   */
  error_code = logpb_create_volume_info (to_db_fullname);
  if (error_code != NO_ERROR)
    {
      goto error;
    }

  /*
   * Create and Copy the TDE master key file (_keys)
   */
  tde_make_keys_file_fullname (from_mk_path, boot_db_full_name (), false);
  tde_make_keys_file_fullname (to_mk_path, to_db_fullname, false);
  error_code = tde_copy_keys_file (thread_p, to_mk_path, from_mk_path, false, false);
  if (error_code != NO_ERROR)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TDE_COPY_KEYS_FILE_FAIL, 0);
      /* keep going with out master key file */
    }

  /*
   * Create a LOG INFORMATION FILE
   */

  fileio_make_log_info_name (to_volname, to_logpath, to_prefix_logname);
  logpb_create_log_info (to_volname, to_db_fullname);

  catmsg = msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_LOG, MSGCAT_LOG_LOGINFO_ACTIVE);
  if (catmsg == NULL)
    {
      catmsg = "ACTIVE: %s %d pages\n";
    }
  error_code = log_dump_log_info (to_volname, false, catmsg, to_volname, log_Gl.hdr.npages + 1);
  if (error_code != NO_ERROR && error_code != ER_LOG_MOUNT_FAIL)
    {
      goto error;
    }

  fileio_make_backup_volume_info_name (to_volname, to_logpath, to_prefix_logname);
  if (logpb_add_volume (to_db_fullname, LOG_DBLOG_BKUPINFO_VOLID, to_volname, DISK_UNKNOWN_PURPOSE) !=
      LOG_DBLOG_BKUPINFO_VOLID)
    {
      error_code = ER_FAILED;
      goto error;
    }

  /*
   * FIRST CREATE A NEW LOG FOR THE NEW DATABASE. This log is not a copy of
   * of the old log; it is a newly created one.
   * Compose the LOG name for the ACTIVE portion of the log.
   * Make sure that nobody else is using this database
   */

  to_malloc_log_pgptr = (LOG_PAGE *) malloc (LOG_PAGESIZE);
  if (to_malloc_log_pgptr == NULL)
    {
      error_code = ER_FAILED;
      goto error;
    }

#if !defined (NDEBUG)
  // suppress valgrind complaint.
  memset (to_malloc_log_pgptr, LOG_PAGE_INIT_VALUE, LOG_PAGESIZE);
#endif // DEBUG

  fileio_make_log_active_name (to_volname, to_logpath, to_prefix_logname);
  if (logpb_add_volume (to_db_fullname, LOG_DBLOG_ACTIVE_VOLID, to_volname, DISK_UNKNOWN_PURPOSE) !=
      LOG_DBLOG_ACTIVE_VOLID)
    {
      error_code = ER_FAILED;
      goto error;
    }
  to_vdes =
    fileio_format (thread_p, to_db_fullname, to_volname, LOG_DBCOPY_VOLID, log_Gl.hdr.npages + 1, false, true, false,
		   LOG_PAGESIZE, 0, false);
  if (to_vdes == NULL_VOLDES)
    {
      error_code = ER_FAILED;
      goto error;
    }

  /*
   * Write an end of log mark at first append page. This is used to detect the
   * end of new log
   */

  phy_pageid = LOGPB_PHYSICAL_HEADER_PAGE_ID + 1;
  to_malloc_log_pgptr->hdr.logical_pageid = 0;
  to_malloc_log_pgptr->hdr.offset = NULL_OFFSET;
  to_malloc_log_pgptr->hdr.flags = 0;

  eof = (LOG_RECORD_HEADER *) to_malloc_log_pgptr->area;
  eof->trid = LOG_SYSTEM_TRANID + 1;
  LSA_SET_NULL (&eof->prev_tranlsa);
  LSA_SET_NULL (&eof->back_lsa);
  LSA_SET_NULL (&eof->forw_lsa);
  eof->type = LOG_END_OF_LOG;

  error_code = logpb_set_page_checksum (thread_p, to_malloc_log_pgptr);
  if (error_code != NO_ERROR)
    {
      fileio_dismount (thread_p, to_vdes);
      goto error;
    }

  log_Gl.hdr.eof_lsa.pageid = to_malloc_log_pgptr->hdr.logical_pageid;
  log_Gl.hdr.eof_lsa.offset = 0;

  write_mode = dwb_is_created () == true ? FILEIO_WRITE_NO_COMPENSATE_WRITE : FILEIO_WRITE_DEFAULT_WRITE;
  if (fileio_write (thread_p, to_vdes, to_malloc_log_pgptr, phy_pageid, LOG_PAGESIZE, write_mode) == NULL)
    {
      error_code = ER_FAILED;
      fileio_dismount (thread_p, to_vdes);
      goto error;
    }

  /*
   * Initialize the active log header from the old log.
   */

  /*
   * Now, modify the log header. Similar than log_create
   */
  to_malloc_log_pgptr->hdr.logical_pageid = LOGPB_HEADER_PAGE_ID;
  to_malloc_log_pgptr->hdr.offset = NULL_OFFSET;
  to_malloc_log_pgptr->hdr.flags = 0;

  to_hdr = (LOG_HEADER *) to_malloc_log_pgptr->area;
  error_code = logpb_initialize_header (thread_p, to_hdr, to_prefix_logname, log_Gl.hdr.npages + 1, &db_creation);
  if (error_code != NO_ERROR)
    {
      fileio_dismount (thread_p, to_vdes);
      goto error;
    }

  if (logpb_copy_log_header (thread_p, to_hdr, &log_Gl.hdr) != NO_ERROR)
    {
      fileio_dismount (thread_p, to_vdes);
      goto error;
    }

  error_code = logpb_set_page_checksum (thread_p, to_malloc_log_pgptr);
  if (error_code != NO_ERROR)
    {
      fileio_dismount (thread_p, to_vdes);
      goto error;
    }

  /* Now write the log header */
  phy_pageid = logpb_to_physical_pageid (to_malloc_log_pgptr->hdr.logical_pageid);
  if (fileio_write (thread_p, to_vdes, to_malloc_log_pgptr, phy_pageid, LOG_PAGESIZE, write_mode) == NULL)
    {
      error_code = ER_FAILED;
      fileio_dismount (thread_p, to_vdes);
      goto error;
    }

  /* Dismount the copy of the log */
  fileio_dismount (thread_p, to_vdes);

  /*
   * Start the COPYING all INFORMATION VOLUMES
   */

  /*
   * Prepare the where path for the volumes according to the input
   */

  error_code =
    logpb_start_where_path (to_db_fullname, toext_path, &ext_name, &ext_path, &alloc_extpath, fileof_vols_and_copypaths,
			    &fromfile_paths_fp);
  if (error_code != NO_ERROR)
    {
      goto error;
    }

  for (volid = LOG_DBFIRST_VOLID; volid != NULL_VOLID; volid = fileio_find_next_perm_volume (thread_p, volid))
    {
      error_code =
	logpb_next_where_path (to_db_fullname, toext_path, ext_name, ext_path, fileof_vols_and_copypaths,
			       fromfile_paths_fp, num_perm_vols, volid, from_volname, to_volname);
      if (error_code != NO_ERROR)
	{
	  goto error;
	}
      error_code = logpb_copy_volume (thread_p, volid, to_volname, &to_hdr->db_creation, &to_hdr->chkpt_lsa);
      if (error_code != NO_ERROR)
	{
	  goto error;
	}

      /* Write information about this volume in the volume information file */
      if (logpb_add_volume (to_db_fullname, volid, to_volname, DB_PERMANENT_DATA_PURPOSE) != volid)
	{
	  error_code = ER_FAILED;
	  goto error;
	}
    }

  /*
   * We need to change the name of the volumes in our internal tables.
   * That is, first volume points to second volume
   *          second volume points to third volume
   *          and so on..
   *          last volume points to nobody
   */

  fileio_make_volume_info_name (to_volname, to_db_fullname);
  sprintf (format_string, "%%d %%%ds", PATH_MAX - 1);

  to_volinfo_fp = fopen (to_volname, "r");
  if (to_volinfo_fp != NULL)
    {
      volid = NULL_VOLID;
      while (true)
	{
	  if (fscanf (to_volinfo_fp, format_string, &fromfile_volid, to_volname) != 2)
	    {
	      stop_eof = true;
	      fromfile_volid = NULL_VOLID;
	      to_volname[0] = '\0';
	    }
	  else
	    {
	      if ((VOLID) fromfile_volid < LOG_DBFIRST_VOLID)
		{
		  continue;
		}
	    }

	  /*
	   * The previous volume must point to new volume
	   */

	  if (volid != NULL_VOLID)
	    {
	      error_code = disk_set_link (thread_p, LOG_DBCOPY_VOLID, fromfile_volid, to_volname, false, DISK_FLUSH);
	      if (error_code != NO_ERROR)
		{
		  fileio_dismount (thread_p, to_vdes);
		  goto error;
		}
	      logpb_flush_pages_direct (thread_p);
	      error_code = pgbuf_flush_all_unfixed_and_set_lsa_as_null (thread_p, LOG_DBCOPY_VOLID);
	      if (error_code != NO_ERROR)
		{
		  fileio_dismount (thread_p, to_vdes);
		  goto error;
		}
	      if (fileio_synchronize (thread_p, to_vdes, to_volname, FILEIO_SYNC_ALSO_FLUSH_DWB) != to_vdes)
		{
		  fileio_dismount (thread_p, to_vdes);
		  error_code = ER_FAILED;
		  goto error;
		}
	      (void) pgbuf_invalidate_all (thread_p, LOG_DBCOPY_VOLID);
	      fileio_dismount (thread_p, to_vdes);
	    }

	  if (stop_eof == true)
	    {
	      break;
	    }

	  /*
	   * Now, mount the current volume
	   */
	  volid = (VOLID) fromfile_volid;

	  to_vdes = fileio_mount (thread_p, log_Db_fullname, to_volname, LOG_DBCOPY_VOLID, false, false);
	  if (to_vdes == NULL_VOLDES)
	    {
	      error_code = ER_FAILED;
	      goto error;
	    }
	}
      fclose (to_volinfo_fp);
    }

  if (fromfile_paths_fp != NULL)
    {
      fclose (fromfile_paths_fp);
    }

  if (alloc_extpath != NULL)
    {
      free_and_init (alloc_extpath);
    }

  free_and_init (to_malloc_log_pgptr);

  return NO_ERROR;

  /* ******** */
error:

  if (to_malloc_log_pgptr)
    {
      free_and_init (to_malloc_log_pgptr);
    }

  if (fromfile_paths_fp != NULL)
    {
      fclose (fromfile_paths_fp);
    }

  if (alloc_extpath != NULL)
    {
      free_and_init (alloc_extpath);
    }

  /* Destroy the log */

  fileio_make_log_active_name (to_volname, to_logpath, to_prefix_logname);
  fileio_unformat (thread_p, to_volname);
  fileio_make_log_info_name (to_volname, to_logpath, to_prefix_logname);
  fileio_unformat (thread_p, to_volname);

  /*
   * Rewind the volume information to destroy any created volumes if any
   */

  sprintf (format_string, "%%*d %%%ds", PATH_MAX - 1);
  if (to_volinfo_fp != NULL)
    {
      fclose (to_volinfo_fp);
      fileio_make_volume_info_name (to_volname, to_db_fullname);
      if ((to_volinfo_fp = fopen (to_volname, "r")) != NULL)
	{
	  while (true)
	    {
	      if (fscanf (to_volinfo_fp, format_string, to_volname) != 1)
		{
		  break;
		}
	      fileio_unformat (thread_p, to_volname);
	    }
	  fclose (to_volinfo_fp);
	  /* Destroy the volinfo file */
	  fileio_make_volume_info_name (to_volname, to_db_fullname);
	  fileio_unformat (thread_p, to_volname);
	}
    }

  return error_code;
}

/*
 * logpb_rename_all_volumes_files - Rename all volumes/files of the database
 *
 * return: NO_ERROR if all OK, ER status otherwise
 *
 *   num_perm_vols(in):
 *   to_db_fullname(in): New full name of the database
 *   to_logpath(in): Directory where the log volumes reside
 *   to_prefix_logname(in): New prefix name for log volumes. It is usually set
 *                      as database name. For example, if the value is equal to
 *                      "db", the names of the log volumes created are as
 *                      follow:
 *                      Active_log      = db_logactive
 *                      Archive_logs    = db_logarchive.0
 *                                        db_logarchive.1
 *                                             .
 *                                             .
 *                                             .
 *                                        db_logarchive.n
 *                      Log_information = db_loginfo
 *                      Database Backup = db_backup
 *   toext_path(in):
 *   fileof_vols_and_renamepaths(in):
 *   extern_rename(in): Rename the volumes/files at OS too.
 *   force_delete(in): Force delete backup volumes and information file
 *
 * NOTE:All volumes/files of the database are renamed according to the
 *              new specifications. This function performs a soft rename, it
 *              will no copy files.
 *
 *              This function must be run offline. That is, it should not be
 *              run when there are multiusers in the system.
 */
int
logpb_rename_all_volumes_files (THREAD_ENTRY * thread_p, VOLID num_perm_vols, const char *to_db_fullname,
				const char *to_logpath, const char *to_prefix_logname, const char *toext_path,
				const char *fileof_vols_and_renamepaths, bool extern_rename, bool force_delete)
{
  char from_volname[PATH_MAX];	/* Name of new volume */
  char to_volname[PATH_MAX];	/* Name of "to" volume */
  char from_mk_path[PATH_MAX];
  char to_mk_path[PATH_MAX];
  char *alloc_extpath = NULL;	/* Copy path for specific volume */
  FILE *to_volinfo_fp = NULL;	/* Pointer to new volinfo file */
  const char *ext_name;
  char *ext_path;
  VOLID volid, prev_volid;
  FILE *fromfile_paths_fp = NULL;	/* Pointer to open file for location of rename files */
  int i;
  const char *catmsg;

  struct stat ext_path_stat;
  struct stat vol_stat;
  char real_pathbuf[PATH_MAX];
  int error_code = NO_ERROR;

  if (toext_path != NULL && realpath ((char *) toext_path, real_pathbuf) != NULL)
    {
      toext_path = real_pathbuf;
    }

  /*
   * Make sure that the length name of the volumes are OK
   */

  error_code = logpb_verify_length (to_db_fullname, to_logpath, to_prefix_logname);
  if (error_code != NO_ERROR)
    {
      /* Names are too long */
      return error_code;
    }

  /* toext_path validation check. */
  if (toext_path != NULL)
    {
      if (stat (toext_path, &ext_path_stat))
	{
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TM_GET_STAT_FAIL, 1, toext_path);
	  error_code = ER_TM_GET_STAT_FAIL;
	  goto error;
	}
      if ((access (toext_path, W_OK) < 0) || !(S_ISDIR (ext_path_stat.st_mode)))
	{
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TM_IS_NOT_WRITEABLE, 1, toext_path);
	  error_code = ER_TM_IS_NOT_WRITEABLE;
	  goto error;
	}

      error_code =
	logpb_start_where_path (to_db_fullname, toext_path, &ext_name, &ext_path, &alloc_extpath,
				fileof_vols_and_renamepaths, &fromfile_paths_fp);
      if (error_code != NO_ERROR)
	{
	  goto error;
	}

      for (volid = LOG_DBFIRST_VOLID; volid != NULL_VOLID; volid = fileio_find_next_perm_volume (thread_p, volid))
	{
	  error_code =
	    logpb_next_where_path (to_db_fullname, toext_path, ext_name, ext_path, fileof_vols_and_renamepaths,
				   fromfile_paths_fp, num_perm_vols, volid, from_volname, to_volname);
	  if (error_code != NO_ERROR)
	    {
	      goto error;
	    }
	  if (stat (from_volname, &vol_stat))
	    {
	      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TM_GET_STAT_FAIL, 1, from_volname);
	      error_code = ER_TM_GET_STAT_FAIL;
	      goto error;
	    }
	  if ((volid > 0) && (ext_path_stat.st_dev != vol_stat.st_dev))
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TM_CROSS_DEVICE_LINK, 2, from_volname, toext_path);
	      error_code = ER_TM_CROSS_DEVICE_LINK;
	      goto error;
	    }
	}
    }

  if (log_Gl.archive.vdes != NULL_VOLDES)
    {
      logpb_dismount_log_archive (thread_p);
    }

  if (prm_get_bool_value (PRM_ID_LOG_BACKGROUND_ARCHIVING))
    {
      /* Destroy temporary log archive */
      fileio_unformat (thread_p, log_Name_bg_archive);
      log_Gl.bg_archive_info.vdes = NULL_VOLDES;
      /* Destroy temporary removed log archived */
      fileio_unformat (thread_p, log_Name_removed_archive);
    }

  if (force_delete)
    {
      /*
       * REMOVE ONLINE BACKUPS OF PRESENT DATABASE
       * Obtain the name of the backups from the backup volume information file.
       */

      to_volinfo_fp = fopen (log_Name_bkupinfo, "r");
      if (to_volinfo_fp != NULL)
	{

	  if (fileio_read_backup_info_entries (to_volinfo_fp, FILEIO_FIRST_BACKUP_VOL_INFO) == NO_ERROR)
	    {
	      /* Remove any backups that have been created up to now */
	      fileio_remove_all_backup (thread_p, -1);
	      fileio_finalize_backup_info (FILEIO_FIRST_BACKUP_VOL_INFO);
	    }

	  fclose (to_volinfo_fp);

	  /* Destroy the backup volume information */
	  fileio_unformat (thread_p, log_Name_bkupinfo);
	}
    }

  /*
   * REMOVE ANY LOG ARCHIVES from present database
   */

  for (i = log_Gl.hdr.last_deleted_arv_num + 1; i < log_Gl.hdr.nxarv_num; i++)
    {
      fileio_make_log_archive_name (from_volname, log_Archive_path, log_Prefix, i);
      /*
       * Just to avoid the warning, the volume is check first
       */
      if (fileio_is_volume_exist (from_volname) == true)
	{
	  fileio_unformat (thread_p, from_volname);
	}
    }

  /*
   * RENAME LOG ACTIVE
   */

  /*
   * Modify the name in the log header. Similar from log_create
   */

  log_Gl.hdr.nxarv_num = 0;
  log_Gl.hdr.last_arv_num_for_syscrashes = -1;
  log_Gl.hdr.last_deleted_arv_num = -1;
  LSA_SET_NULL (&log_Gl.hdr.bkup_level0_lsa);
  LSA_SET_NULL (&log_Gl.hdr.bkup_level1_lsa);
  LSA_SET_NULL (&log_Gl.hdr.bkup_level2_lsa);
  strcpy (log_Gl.hdr.prefix_name, to_prefix_logname);

  logpb_flush_pages_direct (thread_p);
  logpb_flush_header (thread_p);

  if (extern_rename == true)
    {
      logpb_finalize_pool (thread_p);
      fileio_dismount (thread_p, log_Gl.append.vdes);

      fileio_make_log_active_name (to_volname, to_logpath, to_prefix_logname);
      if (fileio_rename (LOG_DBLOG_ACTIVE_VOLID, log_Name_active, to_volname) != NULL)
	{
	  log_Gl.append.vdes = fileio_mount (thread_p, to_db_fullname, to_volname, LOG_DBLOG_ACTIVE_VOLID, true, false);
	}
      else
	{
	  log_Gl.append.vdes =
	    fileio_mount (thread_p, log_Db_fullname, log_Name_active, LOG_DBLOG_ACTIVE_VOLID, true, false);
	  error_code = ER_FAILED;
	  goto error;
	}

      /* Get the append page */
      error_code = logpb_initialize_pool (thread_p);
      if (error_code != NO_ERROR)
	{
	  goto error;
	}
      if (logpb_fetch_start_append_page (thread_p) != NO_ERROR)
	{
	  error_code = ER_FAILED;
	  goto error;
	}
    }

  /*
   * Create the DATABASE VOLUME INFORMATION file
   */

  /*
   * Destroy the old VOLUME INFORMATION AND LOG INFORMATION. Then, create
   * them back.
   */
  fileio_unformat (thread_p, log_Name_volinfo);
  fileio_unformat (thread_p, log_Name_info);

  error_code = logpb_create_volume_info (to_db_fullname);
  if (error_code != NO_ERROR)
    {
      goto error;
    }

  tde_make_keys_file_fullname (from_mk_path, boot_db_full_name (), false);
  tde_make_keys_file_fullname (to_mk_path, to_db_fullname, false);

  if (fileio_rename (LOG_DBTDE_KEYS_VOLID, from_mk_path, to_mk_path) != NULL)
    {
      /* Nothing, tde keys file can be unavailable */
    }


  fileio_make_log_info_name (to_volname, to_logpath, to_prefix_logname);
  logpb_create_log_info (to_volname, to_db_fullname);

  catmsg = msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_LOG, MSGCAT_LOG_LOGINFO_COMMENT_FROM_RENAMED);
  if (catmsg == NULL)
    {
      catmsg = "COMMENT: from renamed database = %s\n";
    }
  error_code = log_dump_log_info (to_volname, false, catmsg, log_Db_fullname);
  if (error_code != NO_ERROR && error_code != ER_LOG_MOUNT_FAIL)
    {
      goto error;
    }

  catmsg = msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_LOG, MSGCAT_LOG_LOGINFO_ACTIVE);
  if (catmsg == NULL)
    {
      catmsg = "ACTIVE: %s %d pages\n";
    }
  error_code = log_dump_log_info (to_volname, false, catmsg, to_volname, log_Gl.hdr.npages + 1);
  if (error_code != NO_ERROR && error_code != ER_LOG_MOUNT_FAIL)
    {
      goto error;
    }

  /*
   * Add the backup information and the log active to the volume
   * information
   */
  fileio_make_backup_volume_info_name (to_volname, to_logpath, to_prefix_logname);
  if (logpb_add_volume (to_db_fullname, LOG_DBLOG_BKUPINFO_VOLID, to_volname, DISK_UNKNOWN_PURPOSE) !=
      LOG_DBLOG_BKUPINFO_VOLID)
    {
      error_code = ER_FAILED;
      goto error;
    }

  fileio_make_log_active_name (to_volname, to_logpath, to_prefix_logname);
  if (logpb_add_volume (to_db_fullname, LOG_DBLOG_ACTIVE_VOLID, to_volname, DISK_UNKNOWN_PURPOSE) !=
      LOG_DBLOG_ACTIVE_VOLID)
    {
      error_code = ER_FAILED;
      goto error;
    }

  /*
   * Start the RENAMING all DATA VOLUMES
   */

  /*
   * Prepare the where path for the volumes according to the input
   */

  error_code =
    logpb_start_where_path (to_db_fullname, toext_path, &ext_name, &ext_path, &alloc_extpath,
			    fileof_vols_and_renamepaths, &fromfile_paths_fp);
  if (error_code != NO_ERROR)
    {
      goto error;
    }

  for (volid = LOG_DBFIRST_VOLID; volid != NULL_VOLID; volid = fileio_find_next_perm_volume (thread_p, volid))
    {
      /* Change the name of the volume */
      error_code =
	logpb_next_where_path (to_db_fullname, toext_path, ext_name, ext_path, fileof_vols_and_renamepaths,
			       fromfile_paths_fp, num_perm_vols, volid, from_volname, to_volname);
      if (error_code != NO_ERROR)
	{
	  goto error;
	}

      error_code =
	disk_set_creation (thread_p, volid, to_volname, &log_Gl.hdr.db_creation, &log_Gl.hdr.chkpt_lsa, true,
			   DISK_DONT_FLUSH);
      if (error_code != NO_ERROR)
	{
	  goto error;
	}

      /*
       * We need to change the name of the volumes in our internal tables.
       * That is, first volume points to second volume
       *          second volume points to third volume
       *          and so on..
       *          last volume points to nobody
       */

      if (volid != LOG_DBFIRST_VOLID)
	{
	  prev_volid = fileio_find_previous_perm_volume (thread_p, volid);
	  error_code = disk_set_link (thread_p, prev_volid, volid, to_volname, false, DISK_FLUSH);
	  if (error_code != NO_ERROR)
	    {
	      goto error;
	    }
	}

      /*
       * Now flush every single page of this volume, dismount the volume, rename
       * the volume, and mount the volume
       */
      logpb_flush_pages_direct (thread_p);
      error_code = pgbuf_flush_all (thread_p, volid);
      if (error_code != NO_ERROR)
	{
	  goto error;
	}
      if (fileio_synchronize (thread_p, fileio_get_volume_descriptor (volid), fileio_get_volume_label (volid, PEEK),
			      FILEIO_SYNC_ALSO_FLUSH_DWB) == NULL_VOLDES)
	{
	  error_code = ER_FAILED;
	  goto error;
	}

      (void) pgbuf_invalidate_all (thread_p, volid);

      if (extern_rename == true)
	{
	  fileio_dismount (thread_p, fileio_get_volume_descriptor (volid));
	  if (fileio_rename (volid, from_volname, to_volname) != NULL)
	    {
	      (void) fileio_mount (thread_p, to_db_fullname, to_volname, volid, false, false);
	    }
	  else
	    {
	      (void) fileio_mount (thread_p, log_Db_fullname, from_volname, volid, false, false);
	      error_code = ER_FAILED;
	      goto error;
	    }
	}

      /* Write information about this volume in the volume information file */
      if (logpb_add_volume (to_db_fullname, volid, to_volname, DB_PERMANENT_DATA_PURPOSE) != volid)
	{
	  error_code = ER_FAILED;
	  goto error;
	}
    }

  if (fromfile_paths_fp != NULL)
    {
      fclose (fromfile_paths_fp);
    }

  if (alloc_extpath != NULL)
    {
      free_and_init (alloc_extpath);
    }

  /* Indicate the new names */
  error_code = logpb_initialize_log_names (thread_p, to_db_fullname, to_logpath, to_prefix_logname);
  return error_code;

  /* ******* */
error:
  /* May need to rename some volumes back */

  if (to_volinfo_fp != NULL)
    {
      fclose (to_volinfo_fp);
    }

  if (fromfile_paths_fp != NULL)
    {
      fclose (fromfile_paths_fp);
    }

  if (alloc_extpath != NULL)
    {
      free_and_init (alloc_extpath);
    }

  /* May need to rename back whatever was renamed */

  return error_code;
}

/*
 * logpb_delete - Delete all log files and database backups
 *
 * return: NO_ERROR if all OK, ER status otherwise
 *
 *   num_perm_vols(in):
 *   db_fullname(in): Full name of the database
 *   logpath(in): Directory where the log volumes reside
 *   prefix_logname(in): Name of the log volumes. It is usually set as database
 *                      name. For example, if the value is equal to "db", the
 *                      names of the log volumes created are as follow:
 *                      Active_log      = db_logactive
 *                      Archive_logs    = db_logarchive.0
 *                                        db_logarchive.1
 *                                             .
 *                                             .
 *                                             .
 *                                        db_logarchive.n
 *                      Log_information = db_loginfo
 *                      Database Backup = db_backup
 *   force_delete(in):
 *
 * NOTE:All log volumes (active, archives) and database backups that
 *              are accessible (i.e., located on disk) are removed from the
 *              system. This is a very dangerous operation since the database
 *              cannot be recovered after this operation is done. It is
 *              recommended to backup the database and put the backup on tape
 *              or outside the log and backup directories before this
 *              operation is done.
 *
 *              This function must be run offline. That is, it should not be
 *              run when there are multiusers in the system.
 */
int
logpb_delete (THREAD_ENTRY * thread_p, VOLID num_perm_vols, const char *db_fullname, const char *logpath,
	      const char *prefix_logname, bool force_delete)
{
  char *vlabel;			/* Name of volume */
  char vol_fullname[PATH_MAX];	/* Name of volume */
  LOG_HEADER disk_hdr;		/* Log header area */
  LOG_HEADER *loghdr;		/* Log header pointer */
  VOLID volid;
  FILE *db_volinfo_fp = NULL;
  int read_int_volid;
  int i;
  int error_code = NO_ERROR;
  char format_string[64];

  /*
   * FIRST: Destroy data volumes of the database.
   * That is, the log, and information files are not removed at this point.
   */

  /* If the system is not restarted, read the header directly from disk */
  if (num_perm_vols < 0 || log_Gl.trantable.area == NULL || log_Pb.buffers == NULL)
    {
      /*
       * The system is not restarted. Read the log header from disk and remove
       * the data volumes by reading the database volume information
       */

      er_clear ();
      error_code = logpb_initialize_log_names (thread_p, db_fullname, logpath, prefix_logname);
      if (error_code != NO_ERROR)
	{
	  return error_code;
	}

      if (fileio_is_volume_exist (log_Name_active) == false
	  || (log_Gl.append.vdes =
	      fileio_mount (thread_p, db_fullname, log_Name_active, LOG_DBLOG_ACTIVE_VOLID, true,
			    false)) == NULL_VOLDES)
	{
	  /* Unable to mount the active log */
	  if (er_errid () == ER_IO_MOUNT_LOCKED)
	    {
	      return ER_IO_MOUNT_LOCKED;
	    }
	  else
	    {
	      loghdr = NULL;
	    }
	}
      else
	{
	  char log_pgbuf[IO_MAX_PAGE_SIZE + MAX_ALIGNMENT], *aligned_log_pgbuf;
	  LOG_PAGE *log_pgptr;

	  aligned_log_pgbuf = PTR_ALIGN (log_pgbuf, MAX_ALIGNMENT);
	  log_pgptr = (LOG_PAGE *) aligned_log_pgbuf;

	  /* Initialize the buffer pool, so we can read the header */
	  if (logpb_Initialized == false)
	    {
	      error_code = logpb_initialize_pool (thread_p);
	      if (error_code != NO_ERROR)
		{
		  return error_code;
		}
	    }
	  logpb_fetch_header_with_buffer (thread_p, &disk_hdr, log_pgptr);
	  logpb_finalize_pool (thread_p);
	  fileio_dismount (thread_p, log_Gl.append.vdes);
	  log_Gl.append.vdes = NULL_VOLDES;
	  loghdr = &disk_hdr;
	  /*
	   * Make sure that the log is a log file and that it is compatible
	   * with the running database and system
	   */
	  if (loghdr->db_compatibility != rel_disk_compatible ())
	    {
	      loghdr = NULL;
	    }
	  else if (loghdr->db_iopagesize != IO_PAGESIZE || loghdr->db_logpagesize != LOG_PAGESIZE)
	    {
	      /* Pagesize is incorrect,...reset it and call again... */
	      if (db_set_page_size (loghdr->db_iopagesize, loghdr->db_logpagesize) != NO_ERROR)
		{
		  loghdr = NULL;
		}
	      else
		{
		  error_code = logtb_define_trantable_log_latch (thread_p, -1);
		  if (error_code != NO_ERROR)
		    {
		      return error_code;
		    }
		  error_code =
		    logpb_delete (thread_p, num_perm_vols, db_fullname, logpath, prefix_logname, force_delete);
		  return error_code;
		}
	    }
	}

      /*
       * DESTROY DATA VOLUMES using the database volume information since
       * the database system is not restarted.
       *
       * NOTE: only data volumes are removed, logs, and information files
       *       are not removed at this point.
       */

      fileio_make_volume_info_name (vol_fullname, db_fullname);
      sprintf (format_string, "%%d %%%ds", PATH_MAX - 1);

      db_volinfo_fp = fopen (vol_fullname, "r");
      if (db_volinfo_fp != NULL)
	{
	  while (true)
	    {
	      if (fscanf (db_volinfo_fp, format_string, &read_int_volid, vol_fullname) != 2)
		{
		  break;
		}

	      volid = (VOLID) read_int_volid;
	      /*
	       * Remove data volumes at this point
	       */
	      switch (volid)
		{
		case LOG_DBVOLINFO_VOLID:
		case LOG_DBLOG_INFO_VOLID:
		case LOG_DBLOG_BKUPINFO_VOLID:
		case LOG_DBLOG_ACTIVE_VOLID:
		case LOG_DBTDE_KEYS_VOLID:
		  continue;
		default:
		  fileio_unformat (thread_p, vol_fullname);
		}
	    }

	  fclose (db_volinfo_fp);
	}
      else
	{
	  /* Destory at least the database main volume */
	  fileio_unformat (thread_p, db_fullname);
	}
    }
  else
    {
      loghdr = &log_Gl.hdr;
      /*
       * DESTROY DATA VOLUMES
       */
      for (volid = LOG_DBFIRST_VOLID; volid != NULL_VOLID; volid = fileio_find_next_perm_volume (thread_p, volid))
	{
	  vlabel = fileio_get_volume_label (volid, ALLOC_COPY);
	  if (vlabel != NULL)
	    {
	      (void) pgbuf_invalidate_all (thread_p, volid);
	      fileio_dismount (thread_p, fileio_get_volume_descriptor (volid));
	      fileio_unformat (thread_p, vlabel);
	      free (vlabel);
	    }
	}
    }

  /* destroy the database volume information */
  fileio_make_volume_info_name (vol_fullname, db_fullname);
  fileio_unformat (thread_p, vol_fullname);

  /* destroy the TDE keys volume information */
  tde_make_keys_file_fullname (vol_fullname, db_fullname, true);
  fileio_unformat (thread_p, vol_fullname);

  /* Destroy DWB, if still exists. */
  fileio_make_dwb_name (vol_fullname, log_Path, log_Prefix);
  if (fileio_is_volume_exist (vol_fullname))
    {
      fileio_unformat (thread_p, vol_fullname);
    }

  if (force_delete)
    {
      /*
       * SECOND: Destroy backups of data volumes, and the backup log information
       *         The backups are located by reading the backup info log.
       */

      db_volinfo_fp = fopen (log_Name_bkupinfo, "r");
      if (db_volinfo_fp != NULL)
	{

	  if (fileio_read_backup_info_entries (db_volinfo_fp, FILEIO_FIRST_BACKUP_VOL_INFO) == NO_ERROR)
	    {
	      /* Remove any backups that have been created up to now */
	      fileio_remove_all_backup (thread_p, -1);
	      fileio_finalize_backup_info (FILEIO_FIRST_BACKUP_VOL_INFO);
	    }

	  fclose (db_volinfo_fp);

	  /* Now, destroy the backup volume information */
	  fileio_unformat (thread_p, log_Name_bkupinfo);
	}
    }

  /*
   * THIRD: Destroy log active, online log archives, and log information
   */

  /* If there is any archive current mounted, dismount the archive */
  if (log_Gl.trantable.area != NULL && log_Gl.append.log_pgptr != NULL && log_Gl.archive.vdes != NULL_VOLDES)
    {
      logpb_dismount_log_archive (thread_p);
    }

  /* Destroy online log archives */
  if (loghdr != NULL)
    {
      for (i = loghdr->last_deleted_arv_num + 1; i < loghdr->nxarv_num; i++)
	{
	  fileio_make_log_archive_name (vol_fullname, log_Archive_path, log_Prefix, i);
	  fileio_unformat (thread_p, vol_fullname);
	}
    }

  if (prm_get_bool_value (PRM_ID_LOG_BACKGROUND_ARCHIVING))
    {
      /* Destroy temporary log archive */
      fileio_unformat (thread_p, log_Name_bg_archive);
      log_Gl.bg_archive_info.vdes = NULL_VOLDES;
      /* Destroy temporary removed log archived */
      fileio_unformat (thread_p, log_Name_removed_archive);
    }

  /* Now undefine all pages */
  if (log_Gl.trantable.area != NULL && log_Gl.append.log_pgptr != NULL)
    {
      logpb_finalize_pool (thread_p);
      (void) pgbuf_invalidate_all (thread_p, NULL_VOLID);
      logtb_undefine_trantable (thread_p);
      if (log_Gl.append.vdes != NULL_VOLDES)
	{
	  fileio_dismount (thread_p, log_Gl.append.vdes);
	  log_Gl.append.vdes = NULL_VOLDES;
	}
      log_Gl.archive.vdes = NULL_VOLDES;
    }

  fileio_unformat (thread_p, log_Name_active);
  fileio_unformat (thread_p, log_Name_info);

  return NO_ERROR;
}

/*
 * logpb_check_if_exists -
 *
 * return:
 *
 *   fname(in):
 *   first_vol(in):
 *
 * NOTE:
 */
static bool
logpb_check_if_exists (const char *fname, char *first_vol)
{
  struct stat stat_buf;

  if (stat (fname, &stat_buf) != 0)
    {
      return false;		/* not exist */
    }
  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_BO_VOLUME_EXISTS, 1, fname);
  if (first_vol[0] == 0)
    {
      strcpy (first_vol, fname);
    }
  return true;
}

/*
 * logpb_check_exist_any_volumes - check existence of DB files
 *
 * return: NO_ERROR or error code
 *
 *   db_fullname(in): Full name of the database
 *   logpath(in): Directory where the log volumes reside
 *   prefix_logname(in): Name of the log volumes.
 *   first_vol(in):
 *
 * NOTE: All log volumes (active, archives) and database backups that
 *              are accessible (i.e., located on disk) are checked
 */
int
logpb_check_exist_any_volumes (THREAD_ENTRY * thread_p, const char *db_fullname, const char *logpath,
			       const char *prefix_logname, char *first_vol, bool * is_exist)
{
  int exist_cnt;
  int error_code = NO_ERROR;

  exist_cnt = 0;
  first_vol[0] = 0;

  *is_exist = false;

  error_code = logpb_initialize_log_names (thread_p, db_fullname, logpath, prefix_logname);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }
  exist_cnt += logpb_check_if_exists (db_fullname, first_vol) ? 1 : 0;
  exist_cnt += logpb_check_if_exists (log_Name_active, first_vol) ? 1 : 0;
  exist_cnt += logpb_check_if_exists (log_Name_info, first_vol) ? 1 : 0;
  exist_cnt += logpb_check_if_exists (log_Name_volinfo, first_vol) ? 1 : 0;

  if (exist_cnt > 0)
    {
      *is_exist = true;
    }
  else
    {
      *is_exist = false;
    }

  return error_code;
}

/*
 *
 *       		       LOG FATAL ERRORS
 *
 */

/*
 * logpb_fatal_error - Log error
 *
 * return: nothing
 *
 *   log_exit(in):
 *   file_name(in):
 *   lineno(in):
 *   fmt(in):
 *   va_alist(in): Variable number of arguments (just like fprintf)
 *
 * NOTE: An error was found during logging. A short error message is
 *              produced on the stderr describing the error. Currently, the
 *              database is exited.
 */
void
logpb_fatal_error (THREAD_ENTRY * thread_p, bool log_exit, const char *file_name, const int lineno, const char *fmt,
		   ...)
{
  va_list ap;

  va_start (ap, fmt);
  logpb_fatal_error_internal (thread_p, log_exit, true, file_name, lineno, fmt, ap);
  va_end (ap);
}

void
logpb_fatal_error_exit_immediately_wo_flush (THREAD_ENTRY * thread_p, const char *file_name, const int lineno,
					     const char *fmt, ...)
{
  va_list ap;

  va_start (ap, fmt);
  logpb_fatal_error_internal (thread_p, true, false, file_name, lineno, fmt, ap);
  va_end (ap);
}

static void
logpb_fatal_error_internal (THREAD_ENTRY * thread_p, bool log_exit, bool need_flush, const char *file_name,
			    const int lineno, const char *fmt, va_list ap)
{
  const char *msglog;
  char msg[LINE_MAX];

  /* call er_set() to print call stack to the log */
  vsnprintf (msg, LINE_MAX, fmt, ap);
  er_set (ER_FATAL_ERROR_SEVERITY, file_name, lineno, ER_LOG_FATAL_ERROR, 1, msg);

  /*
   * Flush any unfixed, dirty pages before the system exits. This is done
   * to make sure that all committed actions are reflected on disk.
   * Unfortunately, we may be placing some uncommitted action od disk. This
   * will be fixed by our recovery process. Note if the user runs the pathdb,
   * utility after this, the uncommitted actions will be considered as
   * committed.
   */

  if (log_exit == true && need_flush == true && log_Gl.append.log_pgptr != NULL)
    {
      /* Flush up to the smaller of the previous LSA record or the previous flushed append page. */
      LOG_LSA tmp_lsa1, tmp_lsa2;
      static int in_fatal = false;

      if (in_fatal == false)
	{
	  in_fatal = true;

	  if (log_Gl.append.prev_lsa.pageid < log_Gl.append.get_nxio_lsa ().pageid)
	    {
	      LSA_COPY (&tmp_lsa1, &log_Gl.append.prev_lsa);
	    }
	  else
	    {
	      /* TODO : valid code ?? */
	      /*
	       * if ((tmp_lsa1.pageid = log_Gl.append.nxio_lsa.pageid - 1) < 0) tmp_lsa1.pageid = 0; */
	      tmp_lsa1.pageid = 0;
	    }

	  /*
	   * Flush as much as you can without forcing the current unfinish log
	   * record.
	   */
	  (void) pgbuf_flush_checkpoint (thread_p, &tmp_lsa1, NULL, &tmp_lsa2, NULL);
	  in_fatal = false;
	}
    }

  fileio_synchronize_all (thread_p, false);

  fflush (stderr);
  fflush (stdout);

#if defined(CUBRID_DEBUG)
  fprintf (stderr, "\n--->>>\n*** LOG FATAL ERROR *** file %s - line %d\n", file_name, lineno);
  /* Print out remainder of message */
  vfprintf (stderr, fmt, ap);
  fprintf (stderr, "\n");
#else /* CUBRID_DEBUG */
  fprintf (stderr, "\n--->>>\n*** FATAL ERROR *** \n");
#endif /* CUBRID_DEBUG */

  fprintf (stderr, "%s\n", er_msg ());

  /*
   * If error message log is different from terminal or /dev/null..indicate
   * that additional information can be found in the error log file
   */
  msglog = er_get_msglog_filename ();
  if (msglog != NULL && strcmp (msglog, "/dev/null") != 0)
    {
      fprintf (stderr, "Please consult error_log file = %s for additional information\n", msglog);
    }

  fflush (stderr);
  fflush (stdout);

  if (log_exit == true)
    {
      fprintf (stderr, "... ABORT/EXIT IMMEDIATELY ...<<<---\n");

#if defined(SERVER_MODE)
      boot_donot_shutdown_server_at_exit ();
      boot_server_status (BOOT_SERVER_DOWN);
#else /* SERVER_MODE */
      /*
       * The following crap is added to the standalone version to avoid the
       * client to continue accessing the database system in presence of
       * call on exit functions of the applications.
       */
      boot_donot_shutdown_client_at_exit ();
      tran_cache_tran_settings (NULL_TRAN_INDEX, -1, TRAN_DEFAULT_ISOLATION_LEVEL ());
      db_Connect_status = DB_CONNECTION_STATUS_NOT_CONNECTED;
#endif /* SERVER_MODE */

#if defined(NDEBUG)
      exit (EXIT_FAILURE);
#else /* NDEBUG */
      /* debugging purpose */
      abort ();
#endif /* NDEBUG */
    }
}

#if defined(SERVER_MODE)
/*
 * logpb_backup_needed_archive_logs - Backup active log archives
 *
 * return: NO_ERROR if all OK, ER status otherwise
 *
 *   session(in): The session array which is set as a side effect.
 *   first_arv_num(in): last arv num to archive (inclusive)
 *   last_arv_num(in): last arv num to archive (inclusive)
 *
 * NOTE: Determine which active log records will be required to fully restore
 *   this backup in the event recovery is needed.  This probably includes
 *   the active log archives as well as at least some portion of the
 *   log active.  Force a log archive of the active log, to insure that we
 *   have the necessary log records to restore if this backup is "fuzzy".
 */
static int
logpb_backup_needed_archive_logs (THREAD_ENTRY * thread_p, FILEIO_BACKUP_SESSION * session, int first_arv_num,
				  int last_arv_num)
{
  int i;
  char logarv_name[PATH_MAX];	/* Archive name */
  int error_code = NO_ERROR;

  for (i = first_arv_num; i >= 0 && i <= last_arv_num; i++)
    {
      /* Backup this archive volume */
      fileio_make_log_archive_name (logarv_name, log_Archive_path, log_Prefix, i);

      error_code = fileio_backup_volume (thread_p, session, logarv_name, LOG_DBLOG_ARCHIVE_VOLID, -1, false);
      if (error_code != NO_ERROR)
	{
	  break;
	}
    }

  return error_code;
}
#endif /* SERVER_MODE */

/*
 * logpb_remote_ask_user_before_delete_volumes - Remote prompt before arv deletion
 *
 * return: true if ok to proceed, false if user declines
 *
 *   volpath(in): the pathname to location where deletion will occur
 *
 * NOTE:Ask the user if it is ok to proceed with a destructive operation, namely
 *   deleting one or more prior backups.
 */
static bool
logpb_remote_ask_user_before_delete_volumes (THREAD_ENTRY * thread_p, const char *volpath)
{
  char *ptr1 = NULL, *ptr2 = NULL, *ptr3 = NULL;
  char *fullmsg = NULL;
  char user_response[FILEIO_MAX_USER_RESPONSE_SIZE];
  bool r;

  if (asprintf (&ptr1, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_LOG, MSGCAT_LOG_STARTS)) < 0
      || asprintf (&ptr2, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_LOG, MSGCAT_LOG_DELETE_BKVOLS), volpath) < 0
      || asprintf (&ptr3, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_LOG, MSGCAT_LOG_ENTER_Y2_CONFIRM)) < 0
      || asprintf (&fullmsg, "%s%s%s%s", ptr1, ptr2, ptr3, ptr1) < 0)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      r = false;
      goto end;
    }

  if (fileio_request_user_response (thread_p, FILEIO_PROMPT_BOOLEAN_TYPE, fullmsg, user_response, NULL, -1, -1, NULL,
				    -1) != NO_ERROR)
    {
      r = false;
      goto end;
    }

  /* process the return */
  r = (user_response[0] == '1');

end:
  if (ptr1 != NULL)
    {
      free (ptr1);
    }
  if (ptr2 != NULL)
    {
      free (ptr2);
    }
  if (ptr3 != NULL)
    {
      free (ptr3);
    }
  if (fullmsg != NULL)
    {
      free (fullmsg);
    }

  return r;
}

/*
 * logpb_check_and_reset_temp_lsa -
 *
 * return:
 *
 *   volid(in):
 *
 * NOTE:
 */
int
logpb_check_and_reset_temp_lsa (THREAD_ENTRY * thread_p, VOLID volid)
{
  VPID vpid;
  PAGE_PTR pgptr;

  vpid.volid = volid;
  vpid.pageid = 0;
  pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
  if (pgptr == NULL)
    {
      return ER_FAILED;
    }

  if (LOG_DBFIRST_VOLID <= volid && xdisk_get_purpose (thread_p, volid) == DB_TEMPORARY_DATA_PURPOSE)
    {
      pgbuf_reset_temp_lsa (pgptr);
      pgbuf_set_dirty (thread_p, pgptr, FREE);
    }
  else
    {
      pgbuf_unfix (thread_p, pgptr);
    }

  return NO_ERROR;
}

/*
 * logpb_initialize_flush_info - initialize flush information
 *
 * return: nothing
 *
 * NOTE:
 */
static int
logpb_initialize_flush_info (void)
{
  int error = NO_ERROR;
  LOG_FLUSH_INFO *flush_info = &log_Gl.flush_info;

  if (flush_info->toflush != NULL)
    {
      logpb_finalize_flush_info ();
    }
  assert (flush_info->toflush == NULL);

  flush_info->max_toflush = log_Pb.num_buffers - 1;
  flush_info->num_toflush = 0;
  flush_info->toflush = (LOG_PAGE **) calloc (log_Pb.num_buffers, sizeof (flush_info->toflush));
  if (flush_info->toflush == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
	      log_Pb.num_buffers * sizeof (flush_info->toflush));
      error = ER_OUT_OF_VIRTUAL_MEMORY;
    }

  pthread_mutex_init (&flush_info->flush_mutex, NULL);

  return error;
}

/*
 * logpb_finalize_flush_info - Destroy flush information
 *
 * return: nothing
 *
 * NOTE:
 */
static void
logpb_finalize_flush_info (void)
{
#if defined(SERVER_MODE)
  int rv;
#endif /* SERVER_MODE */
  LOG_FLUSH_INFO *flush_info = &log_Gl.flush_info;

  if (flush_info->toflush != NULL)
    {
      rv = pthread_mutex_lock (&flush_info->flush_mutex);
      free_and_init (flush_info->toflush);

      flush_info->max_toflush = 0;
      flush_info->num_toflush = 0;

      pthread_mutex_unlock (&flush_info->flush_mutex);
      pthread_mutex_destroy (&flush_info->flush_mutex);
    }

  return;
}

/*
 * logpb_finalize_writer_info - Destroy writer information
 *
 * return: nothing
 *
 * NOTE:
 */
static void
logpb_finalize_writer_info (void)
{
#if defined (SERVER_MODE)
  int rv;
#endif
  LOGWR_ENTRY *entry, *next_entry;
  LOGWR_INFO *writer_info = log_Gl.writer_info;

  if (writer_info->is_init == true)
    {
      rv = pthread_mutex_lock (&writer_info->wr_list_mutex);
      entry = writer_info->writer_list;
      while (entry)
	{
	  next_entry = entry->next;
	  free (entry);
	  entry = next_entry;
	}
      writer_info->writer_list = NULL;
      writer_info->is_init = false;
      pthread_mutex_unlock (&writer_info->wr_list_mutex);

      pthread_mutex_destroy (&writer_info->wr_list_mutex);

      pthread_mutex_destroy (&writer_info->flush_start_mutex);
      pthread_cond_destroy (&writer_info->flush_start_cond);

      pthread_mutex_destroy (&writer_info->flush_wait_mutex);
      pthread_cond_destroy (&writer_info->flush_wait_cond);

      pthread_mutex_destroy (&writer_info->flush_end_mutex);
      pthread_cond_destroy (&writer_info->flush_end_cond);
    }

  return;
}

/*
 * logpb_initialize_arv_page_info_table - Initialize archive log page table
 *
 * return: nothing
 *
 * NOTE:
 */
void
logpb_initialize_arv_page_info_table (void)
{
  memset (&logpb_Arv_page_info_table, 0, sizeof (ARV_LOG_PAGE_INFO_TABLE));
  logpb_Arv_page_info_table.rear = -1;
}

/*
 * logpb_initialize_logging_statistics - Initialize logging statistics
 *
 * return: nothing
 *
 * NOTE:
 */
void
logpb_initialize_logging_statistics (void)
{
  memset (&log_Stat, 0, sizeof (LOG_LOGGING_STAT));
}

/*
 * logpb_background_archiving -
 *
 * return:
 *
 * NOTE: this function is called by log_initialize_internal only
 *       (in server startup time)
 */
int
logpb_background_archiving (THREAD_ENTRY * thread_p)
{
  char log_pgbuf[IO_MAX_PAGE_SIZE * LOGPB_IO_NPAGES + MAX_ALIGNMENT];
  char *aligned_log_pgbuf;
  LOG_PAGE *log_pgptr;
  LOG_PAGEID page_id, last_page_id;
  LOG_PHY_PAGEID phy_pageid;
  int num_pages = 0;
  int vdes;
  int error_code = NO_ERROR;
  BACKGROUND_ARCHIVING_INFO *bg_arv_info;

  assert (prm_get_bool_value (PRM_ID_LOG_BACKGROUND_ARCHIVING));

  aligned_log_pgbuf = PTR_ALIGN (log_pgbuf, MAX_ALIGNMENT);
  log_pgptr = (LOG_PAGE *) aligned_log_pgbuf;

  bg_arv_info = &log_Gl.bg_archive_info;
  vdes = bg_arv_info->vdes;
  if (vdes == NULL_VOLDES)
    {
      return NO_ERROR;
    }

  last_page_id = log_Gl.hdr.chkpt_lsa.pageid - 1;
  page_id = bg_arv_info->current_page_id;
  phy_pageid = (LOG_PHY_PAGEID) (page_id - bg_arv_info->start_page_id + 1);

  /* Now start dumping the current active pages to archive */
  for (; page_id <= last_page_id; page_id += num_pages, phy_pageid += num_pages)
    {
      num_pages = MIN (LOGPB_IO_NPAGES, (int) (last_page_id - page_id + 1));

      num_pages = logpb_read_page_from_active_log (thread_p, page_id, num_pages, false, log_pgptr);
      if (num_pages <= 0)
	{
	  assert (er_errid () != NO_ERROR);
	  error_code = er_errid ();
	  goto error;
	}

      /* no need to encrypt, it is read as not decrypted (TDE) */
      if (fileio_write_pages (thread_p, vdes, (char *) log_pgptr, phy_pageid, num_pages, LOG_PAGESIZE,
			      FILEIO_WRITE_NO_COMPENSATE_WRITE) == NULL)
	{
	  error_code = ER_LOG_WRITE;
	  goto error;
	}

      bg_arv_info->current_page_id = page_id + num_pages;
    }

error:
  if (error_code == ER_LOG_WRITE || error_code == ER_LOG_READ)
    {
      fileio_dismount (thread_p, bg_arv_info->vdes);
      bg_arv_info->vdes = NULL_VOLDES;
      bg_arv_info->start_page_id = NULL_PAGEID;
      bg_arv_info->current_page_id = NULL_PAGEID;
      bg_arv_info->last_sync_pageid = NULL_PAGEID;

      er_log_debug (ARG_FILE_LINE,
		    "background archiving error, hdr->start_page_id = %d, hdr->current_page_id = %d, error:%d\n",
		    bg_arv_info->start_page_id, bg_arv_info->current_page_id, error_code);
    }

  log_archive_er_log ("logpb_background_archiving end, hdr->start_page_id = %d, hdr->current_page_id = %d\n",
		      bg_arv_info->start_page_id, bg_arv_info->current_page_id);

  return error_code;
}

/*
 * logpb_dump_log_header - dump log header
 *
 * return: Nothing
 *
 *   outfp(in):  file descriptor
 *
 * NOTE:
 */
static void
logpb_dump_log_header (FILE * outfp)
{
  fprintf (outfp, "Log Header:\n");

  fprintf (outfp, "\tfirst log page id : %lld\n", (long long int) log_Gl.hdr.fpageid);

  fprintf (outfp, "\tcurrent log append lsa : (%lld|%d)\n", LSA_AS_ARGS (&log_Gl.hdr.append_lsa));

  fprintf (outfp, "\tlast log append lsa : (%lld|%d)\n", LSA_AS_ARGS (&log_Gl.append.prev_lsa));

  fprintf (outfp, "\tlowest lsa which hasn't been written to disk : (%lld|%d)\n",
	   (long long int) log_Gl.append.get_nxio_lsa ().pageid, (int) log_Gl.append.get_nxio_lsa ().offset);

  fprintf (outfp, "\tcheckpoint lsa : (%lld|%d)\n", LSA_AS_ARGS (&log_Gl.hdr.chkpt_lsa));

  fprintf (outfp, "\tnext archive page id : %lld\n", (long long int) log_Gl.hdr.nxarv_pageid);

  fprintf (outfp, "\tnext archive physical page id : %lld\n", (long long int) log_Gl.hdr.nxarv_phy_pageid);

  fprintf (outfp, "\tnext archive number : %d\n", log_Gl.hdr.nxarv_num);

  fprintf (outfp, "\tlast archive number needed for system crashes : %d\n", log_Gl.hdr.last_arv_num_for_syscrashes);

  fprintf (outfp, "\tlast archive number deleted : %d\n", log_Gl.hdr.last_deleted_arv_num);

  fprintf (outfp, "\tbackup level 0 lsa : (%lld|%d)\n", LSA_AS_ARGS (&log_Gl.hdr.bkup_level0_lsa));

  fprintf (outfp, "\tbackup level 1 lsa : (%lld|%d)\n", LSA_AS_ARGS (&log_Gl.hdr.bkup_level1_lsa));

  fprintf (outfp, "\tbackup level 2 lsa : (%lld|%d)\n", LSA_AS_ARGS (&log_Gl.hdr.bkup_level2_lsa));

  fprintf (outfp, "\tMVCC op lsa : (%lld|%d)\n", LSA_AS_ARGS (&log_Gl.hdr.mvcc_op_log_lsa));

  fprintf (outfp, "\tLast block oldest MVCCID : (%lld)\n", (long long int) log_Gl.hdr.oldest_visible_mvccid);

  fprintf (outfp, "\tLast block newest MVCCID : (%lld)\n", (long long int) log_Gl.hdr.newest_block_mvccid);
}

/*
 * logpb_dump_parameter - dump logging parameter
 *
 * return: Nothing
 *
 *   outfp(in): file descriptor
 *
 * NOTE:
 */
static void
logpb_dump_parameter (FILE * outfp)
{
  fprintf (outfp, "Log Parameters:\n");

  fprintf (outfp, "\tgroup_commit_interval_msec : %d\n",
	   prm_get_integer_value (PRM_ID_LOG_GROUP_COMMIT_INTERVAL_MSECS));

  fprintf (outfp, "\tasync_commit : %s\n", prm_get_bool_value (PRM_ID_LOG_ASYNC_COMMIT) ? "on" : "off");
}

/*
 * logpb_dump_runtime - dump runtime logging information
 *
 * return: Nothing
 *
 *   outfp(in): file descriptor
 *
 * NOTE:
 */
static void
logpb_dump_runtime (FILE * outfp)
{
  long temp = 1;

  fprintf (outfp, "Log Statistics:\n");

  fprintf (outfp, "\ttotal flush count = %ld\n", log_Stat.flushall_append_pages_call_count);

  fprintf (outfp, "\tgroup commit flush count= %ld\n", log_Stat.gc_flush_count);

  fprintf (outfp, "\tdirect flush count= %ld\n", log_Stat.direct_flush_count);

  fprintf (outfp, "\tgroup commit request count = %ld\n", log_Stat.gc_commit_request_count);

  fprintf (outfp, "\tasync commit request count = %ld\n", log_Stat.async_commit_request_count);

  if (log_Stat.flushall_append_pages_call_count != 0)
    {
      temp = (log_Stat.flushall_append_pages_call_count - log_Stat.direct_flush_count);
    }

  fprintf (outfp, "\tgroup commit grouping rate = %f\n", (double) log_Stat.gc_commit_request_count / temp);

  fprintf (outfp, "\tasync commit grouping rate = %f\n", (double) log_Stat.async_commit_request_count / temp);

  temp = 1;
  if (log_Stat.gc_commit_request_count != 0)
    {
      temp = log_Stat.gc_commit_request_count;
    }

  fprintf (outfp, "\tavg group commit wait time = %f\n", log_Stat.gc_total_wait_time / temp);

  fprintf (outfp, "\ttotal commit count = %ld\n", log_Stat.commit_count);

  fprintf (outfp, "\ttotal allocated log pages count = %ld\n", log_Stat.total_append_page_count);

  fprintf (outfp, "\tlog buffer full count = %ld\n", log_Stat.log_buffer_full_count);

  fprintf (outfp, "\tlog buffer flush count by replacement = %ld\n", log_Stat.log_buffer_flush_count_by_replacement);

}

/*
 * xlogpb_dump_stat - dump logging information
 *
 * return: Nothing
 *
 *   outfp(in): file descriptor
 *
 * NOTE:
 */
void
xlogpb_dump_stat (FILE * outfp)
{
  logpb_dump_parameter (outfp);
  logpb_dump_log_header (outfp);
  logpb_dump_runtime (outfp);
}

/*
 * logpb_need_wal -
 */
bool
logpb_need_wal (const LOG_LSA * lsa)
{
  LOG_LSA nxio_lsa = log_Gl.append.get_nxio_lsa ();

  if (LSA_LE (&nxio_lsa, lsa))
    {
      return true;
    }
  else
    {
      return false;
    }
}

/*
 * logpb_backup_level_info_to_string () - format LOG_HDR_BKUP_LEVEL_INFO as string
 *
 *   return: the buffer passed to first argument
 *
 *   buf(out):
 *   buf_size(in):
 *   info(in):
 */
char *
logpb_backup_level_info_to_string (char *buf, int buf_size, const LOG_HDR_BKUP_LEVEL_INFO * info)
{
  char time_str[64];
  time_t time_val = (time_t) info->bkup_attime;

  if (time_val == 0)
    {
      snprintf (buf, buf_size, "time: N/A");
      buf[buf_size - 1] = 0;
    }
  else
    {
      ctime_r (&time_val, time_str);
      /* ctime_r() will padding one '\n' character to buffer, we need truncate it */
      time_str[strlen (time_str) - 1] = 0;
      snprintf (buf, buf_size, "time: %s", time_str);
      buf[buf_size - 1] = 0;
    }

  return buf;
}

/*
 * logpb_find_oldest_available_page_id() - return the oldest log pageid
 *
 *   return: log pageid
 */
LOG_PAGEID
logpb_find_oldest_available_page_id (THREAD_ENTRY * thread_p)
{
  LOG_PAGEID page_id = NULL_PAGEID;
  int vdes = NULL_VOLDES;
  int arv_num;
  LOG_ARV_HEADER *arv_hdr;
  char arv_hdr_pgbuf[IO_MAX_PAGE_SIZE + MAX_ALIGNMENT], *aligned_arv_hdr_pgbuf;
  LOG_PAGE *arv_hdr_pgptr;
  char arv_name[PATH_MAX];

  assert (LOG_CS_OWN (thread_get_thread_entry_info ()));

  LOG_ARCHIVE_CS_ENTER (thread_p);
  arv_num = logpb_find_oldest_available_arv_num (thread_p);
  if (arv_num < 0)
    {
      LOG_ARCHIVE_CS_EXIT (thread_p);

      /* return first logical page of active log */
      return log_Gl.hdr.nxarv_pageid;
    }

  /* before opening a new archive log, close the archive log opened earlier */
  if (log_Gl.archive.vdes != NULL_VOLDES)
    {
      logpb_dismount_log_archive (thread_p);
    }

  aligned_arv_hdr_pgbuf = PTR_ALIGN (arv_hdr_pgbuf, MAX_ALIGNMENT);
  arv_hdr_pgptr = (LOG_PAGE *) aligned_arv_hdr_pgbuf;

  fileio_make_log_archive_name (arv_name, log_Archive_path, log_Prefix, arv_num);

  vdes = fileio_mount (thread_p, log_Db_fullname, arv_name, LOG_DBLOG_ARCHIVE_VOLID, false, false);
  if (vdes != NULL_VOLDES)
    {
      if (fileio_read (thread_p, vdes, arv_hdr_pgptr, 0, LOG_PAGESIZE) == NULL)
	{
	  fileio_dismount (thread_p, vdes);
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_READ, 3, 0LL, 0LL, arv_name);

	  LOG_ARCHIVE_CS_EXIT (thread_p);
	  return NULL_PAGEID;
	}

      arv_hdr = (LOG_ARV_HEADER *) arv_hdr_pgptr->area;
      if (log_Gl.append.vdes != NULL_VOLDES)
	{
	  if (difftime64 ((time_t) arv_hdr->db_creation, (time_t) log_Gl.hdr.db_creation) != 0)
	    {
	      fileio_dismount (thread_p, vdes);

	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_DOESNT_CORRESPOND_TO_DATABASE, 1, arv_name);

	      LOG_ARCHIVE_CS_EXIT (thread_p);
	      return NULL_PAGEID;
	    }
	}
      page_id = arv_hdr->fpageid;

#if !defined(NDEBUG)
      /* In analysys phase, the page may be corrupted. */
      if (log_Gl.rcv_phase == LOG_RESTARTED)
	{
	  logpb_debug_check_log_page (thread_p, arv_hdr_pgptr);
	}
#endif

      fileio_dismount (thread_p, vdes);
    }

  LOG_ARCHIVE_CS_EXIT (thread_p);
  return page_id;
}

/*
 * logpb_find_oldest_available_arv_num() - return oldest archive log number
 *
 *   return: archive log number
 */
int
logpb_find_oldest_available_arv_num (THREAD_ENTRY * thread_p)
{
  char arv_name[PATH_MAX];
  int arv_num;
  int ret_arv_num = -1;

  assert (LOG_CS_OWN (thread_get_thread_entry_info ()));

  arv_num = log_Gl.hdr.nxarv_num - 1;

  while (arv_num >= 0)
    {
      fileio_make_log_archive_name (arv_name, log_Archive_path, log_Prefix, arv_num);

      if (fileio_is_volume_exist (arv_name) == true)
	{
	  ret_arv_num = arv_num;

	  if (arv_num == 0)
	    {
	      break;
	    }

	  arv_num--;
	}
      else
	{
	  break;
	}
    }

  return ret_arv_num;
}

/*
 * logpb_remove_all_in_log_path() - Delete all log volumes and files in log path
 *
 *   return: NO_ERROR if all OK, ER status otherwise
 */
int
logpb_remove_all_in_log_path (THREAD_ENTRY * thread_p, const char *db_fullname, const char *logpath,
			      const char *prefix_logname)
{
  int i, error_code = NO_ERROR;
  char vol_fullname[PATH_MAX];
  LOG_HEADER disk_hdr;
  LOG_HEADER *loghdr = NULL;

  er_clear ();
  error_code = logpb_initialize_log_names (thread_p, db_fullname, logpath, prefix_logname);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  if (fileio_is_volume_exist (log_Name_active)
      && (log_Gl.append.vdes =
	  fileio_mount (thread_p, db_fullname, log_Name_active, LOG_DBLOG_ACTIVE_VOLID, true, false)) != NULL_VOLDES)
    {
      char log_pgbuf[IO_MAX_PAGE_SIZE + MAX_ALIGNMENT], *aligned_log_pgbuf;
      LOG_PAGE *log_pgptr;

      aligned_log_pgbuf = PTR_ALIGN (log_pgbuf, MAX_ALIGNMENT);
      log_pgptr = (LOG_PAGE *) aligned_log_pgbuf;

      if (logpb_Initialized == false)
	{
	  error_code = logpb_initialize_pool (thread_p);
	  if (error_code != NO_ERROR)
	    {
	      goto delete_fixed_logs;
	    }
	}
      logpb_fetch_header_with_buffer (thread_p, &disk_hdr, log_pgptr);
      logpb_finalize_pool (thread_p);
      fileio_dismount (thread_p, log_Gl.append.vdes);
      log_Gl.append.vdes = NULL_VOLDES;
      loghdr = &disk_hdr;
    }

  if (loghdr != NULL)
    {
      for (i = loghdr->last_deleted_arv_num + 1; i < loghdr->nxarv_num; i++)
	{
	  fileio_make_log_archive_name (vol_fullname, log_Archive_path, log_Prefix, i);
	  fileio_unformat (thread_p, vol_fullname);
	}
    }

delete_fixed_logs:

  if (prm_get_bool_value (PRM_ID_LOG_BACKGROUND_ARCHIVING))
    {
      fileio_unformat (thread_p, log_Name_bg_archive);
      fileio_unformat (thread_p, log_Name_removed_archive);
    }

  fileio_unformat (thread_p, log_Name_active);
  fileio_unformat (thread_p, log_Name_info);

  return NO_ERROR;
}

/*
 * logpb_vacuum_reset_log_header_cache () - reset vacuum data cached in log global header.
 */
void
logpb_vacuum_reset_log_header_cache (THREAD_ENTRY * thread_p, LOG_HEADER * loghdr)
{
  vacuum_er_log (VACUUM_ER_LOG_VACUUM_DATA, "Reset vacuum info in loghdr (%p)", loghdr);
  LSA_SET_NULL (&loghdr->mvcc_op_log_lsa);
  loghdr->oldest_visible_mvccid = MVCCID_FIRST;
  loghdr->newest_block_mvccid = MVCCID_NULL;
  loghdr->does_block_need_vacuum = false;
}

/*
 * logpb_last_complete_blockid () - get blockid of last completely logged block
 *
 * return    : blockid
 */
VACUUM_LOG_BLOCKID
logpb_last_complete_blockid (void)
{
  LOG_PAGEID prev_pageid = log_Gl.append.prev_lsa.pageid;
  VACUUM_LOG_BLOCKID blockid = vacuum_get_log_blockid (prev_pageid);

  if (blockid < 0)
    {
      assert (blockid == VACUUM_NULL_LOG_BLOCKID);
      assert (LSA_ISNULL (&log_Gl.append.prev_lsa));
      return VACUUM_NULL_LOG_BLOCKID;
    }

  /* the previous block is the one completed */
  return blockid - 1;
}

/*
 * logpb_page_check_corruption - Check whether the log page is corrupted.
 *   return: error code
 *   thread_p(in): thread entry
 *   log_pgptr(in): the log page
 *   is_page_corrupted(out): true, if the log page is corrupted.
 */
int
logpb_page_check_corruption (THREAD_ENTRY * thread_p, LOG_PAGE * log_pgptr, bool * is_page_corrupted)
{
  int error_code;
  bool has_valid_checksum;

  assert (log_pgptr != NULL && is_page_corrupted != NULL);

  error_code = logpb_page_has_valid_checksum (thread_p, log_pgptr, &has_valid_checksum);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  *is_page_corrupted = !has_valid_checksum;
  return NO_ERROR;
}

#if !defined(NDEBUG)
void
logpb_debug_check_log_page (THREAD_ENTRY * thread_p, void *log_pgptr_ptr)
{
  int err;
  bool is_log_page_corrupted;
  LOG_PAGE *log_pgptr = (LOG_PAGE *) log_pgptr_ptr;

  assert (log_pgptr != NULL);
  if (boot_Server_status != BOOT_SERVER_UP && log_pgptr->hdr.logical_pageid == LOGPB_HEADER_PAGE_ID)
    {
      /* Do not check here since log page size may be not available */
      return;
    }

  if (log_pgptr->hdr.logical_pageid == -1)
    {
      /* Skip checking for null logical pageid. */
      return;
    }

  err = logpb_page_check_corruption (thread_p, log_pgptr, &is_log_page_corrupted);

  assert (err == NO_ERROR && is_log_page_corrupted == false);
}
#endif

size_t
logpb_get_memsize ()
{
  return (size_t) log_Pb.num_buffers * (size_t) LOG_PAGESIZE;
}

/*
 * logpb_set_tde_algorithm () - set tde encryption algorithm to the log page
 * 
 * return         : encryption algorithm  
 * log_pgptr(in)  : Log page pointer
 */
TDE_ALGORITHM
logpb_get_tde_algorithm (const LOG_PAGE * log_pgptr)
{
  /* exclusive */
  assert (!((log_pgptr->hdr.flags & LOG_HDRPAGE_FLAG_ENCRYPTED_AES)
	    && (log_pgptr->hdr.flags & LOG_HDRPAGE_FLAG_ENCRYPTED_ARIA)));

  if (log_pgptr->hdr.flags & LOG_HDRPAGE_FLAG_ENCRYPTED_AES)
    {
      return TDE_ALGORITHM_AES;
    }
  else if (log_pgptr->hdr.flags & LOG_HDRPAGE_FLAG_ENCRYPTED_ARIA)
    {
      return TDE_ALGORITHM_ARIA;
    }
  else
    {
      return TDE_ALGORITHM_NONE;
    }
}

/*
 * logpb_set_tde_algorithm () - set tde encryption algorithm to the log page
 *   
 * thread_p (in)  : Thread entry
 * log_pgptr(in)  : Log page pointer
 * tde_algo (in)  : Encryption algorithm
 */
void
logpb_set_tde_algorithm (THREAD_ENTRY * thread_p, LOG_PAGE * log_pgptr, const TDE_ALGORITHM tde_algo)
{
  assert (tde_is_loaded () || tde_algo == TDE_ALGORITHM_NONE);
  /* clear encrypted flag */
  log_pgptr->hdr.flags &= ~LOG_HDRPAGE_FLAG_ENCRYPTED_MASK;

  switch (tde_algo)
    {
    case TDE_ALGORITHM_AES:
      log_pgptr->hdr.flags |= LOG_HDRPAGE_FLAG_ENCRYPTED_AES;
      break;
    case TDE_ALGORITHM_ARIA:
      log_pgptr->hdr.flags |= LOG_HDRPAGE_FLAG_ENCRYPTED_ARIA;
      break;
    case TDE_ALGORITHM_NONE:
      /* already cleared */
      break;
    }
}
