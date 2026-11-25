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
 * file_io_backup.c - backup and restore of database volumes (at server)
 */

#ident "$Id$"

#include "config.h"

#include <stdlib.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <limits.h>
#include <sys/stat.h>
#include <assert.h>

#if defined(WINDOWS)
#include <io.h>
#else /* WINDOWS */
#include <unistd.h>
#include <sys/time.h>
#include <sys/param.h>
#include <sys/types.h>
#endif /* WINDOWS */

#include "porting.h"

#include "chartype.h"
#include "file_io.h"
#include "compressor.hpp"
#include "storage_common.h"
#include "memory_alloc.h"
#include "error_manager.h"
#include "system_parameter.h"
#include "message_catalog.h"
#include "msgcat_set_log.hpp"
#include "object_representation.h"
#include "perf_monitor.h"
#include "environment_variable.h"
#include "release_string.h"
#include "log_common_impl.h"
#include "log_volids.hpp"

#if defined(SERVER_MODE)
#include "network_interface_sr.h"
#endif /* SERVER_MODE */

#if !defined (CS_MODE)
#include "double_write_buffer.hpp"
#include "page_buffer.h"
#include "xserver_interface.h"
#endif /* !defined (CS_MODE) */

#include "intl_support.h"

#if defined (SERVER_MODE)
#include "thread_worker_pool_impl.hpp"	// for system_core_count
#include "server_support.h"
#endif // SERVER_MODE
#if defined (SERVER_MODE)
#include "thread_entry_task.hpp"
#endif // SERVER_MODE
#if defined (SERVER_MODE) || defined (SA_MODE)
#include "thread_manager.hpp"	// for thread_get_thread_entry_info and thread_sleep
#endif // SERVER_MODE
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

/* backup and restore run only in the server and standalone builds; this file compiles empty in cubridcs */
#if defined (SERVER_MODE) || defined (SA_MODE)

#if !defined(SERVER_MODE)
#define pthread_mutex_init(a, b)
#define pthread_mutex_destroy(a)
#define pthread_mutex_lock(a)	0
#define pthread_mutex_unlock(a)
#endif

/*
 * Message id in the set MSGCAT_SET_IO
 * in the message catalog MSGCAT_CATALOG_CUBRID (file cubrid.msg).
 */
#define MSGCAT_FILEIO_STARTS                        1
#define MSGCAT_FILEIO_BKUP_NEEDED                   2
#define MSGCAT_FILEIO_BKUP_HDR                      3
#define MSGCAT_FILEIO_BKUP_HDR_MAGICID              4
#define MSGCAT_FILEIO_BKUP_HDR_RELEASES             5
#define MSGCAT_FILEIO_BKUP_HDR_DBINFO               6
#define MSGCAT_FILEIO_BKUP_HDR_LEVEL                7
#define MSGCAT_FILEIO_BKUP_HDR_TIME                 8
#define MSGCAT_FILEIO_BKUP_FILE                     9
#define MSGCAT_FILEIO_REST_RELO_NEEDED              10
#define MSGCAT_FILEIO_REST_RELO_OPTIONS             11
#define MSGCAT_FILEIO_NEWLOCATION                   12
#define MSGCAT_FILEIO_INPUT_RANGE_ERROR             13
#define MSGCAT_FILEIO_INCORRECT_BKVOLUME            14
#define MSGCAT_FILEIO_LEVEL_MISMATCH                15
#define MSGCAT_FILEIO_MAGIC_MISMATCH                16
#define MSGCAT_FILEIO_DB_MISMATCH                   17
#define MSGCAT_FILEIO_UNIT_NUM_MISMATCH             18
#define MSGCAT_FILEIO_BACKUP_TIME_MISMATCH          19
#define MSGCAT_FILEIO_BACKUP_VINF_ERROR             20
#define MSGCAT_FILEIO_BACKUP_LABEL_INFO             21
#define MSGCAT_FILEIO_BKUP_HDR_LX_LSA               22
#define MSGCAT_FILEIO_RESTORE_FIND_REASON           23
#define MSGCAT_FILEIO_BKUP_FIND_REASON              24
#define MSGCAT_FILEIO_BKUP_PREV_BKVOL               25
#define MSGCAT_FILEIO_BKUP_NEXT_BKVOL               26
#define MSGCAT_FILEIO_BKUP_HDR_BKUP_PAGESIZE        27
#define MSGCAT_FILEIO_BKUP_HDR_ZIP_INFO             28
#define MSGCAT_FILEIO_BKUP_HDR_INC_ACTIVELOG        29

#define FILEIO_FULL_LEVEL_EXP              32

/*
 * Define a fixed size for backup and restore input/output of the volume
 * headers.  For most modern devices multiples of 512 or 1024 are needed.
 * A size of the header data is computed in compile.
 */
#define GET_NEXT_1K_SIZE(s)             (((((s) - 1) / 1024) + 1) * 1024)
#define FILEIO_BACKUP_HEADER_IO_SIZE    GET_NEXT_1K_SIZE(sizeof(FILEIO_BACKUP_HEADER))

#define FILEIO_BACKUP_NO_ZIP_HEADER_VERSION        1
#define FILEIO_BACKUP_LOG_END_HEADER_VERSION       3	/* start_log_end_lsa is present */
#define FILEIO_BACKUP_CURRENT_HEADER_VERSION       3

/* the most bytes of formatted pages that a restore writes at once to the pages that no backup level holds */
#define FILEIO_RESTORE_FORMAT_WRITE_SIZE          (1024 * 1024)

#define FILEIO_PAGE_SIZE_FULL_LEVEL (IO_PAGESIZE * FILEIO_FULL_LEVEL_EXP)
#define FILEIO_BACKUP_PAGE_OVERHEAD \
  (offsetof(FILEIO_BACKUP_PAGE, iopage) + sizeof(PAGEID))
#define FILEIO_BACKUP_DBVOLS_IO_PAGE_SIZE \
  (IO_PAGESIZE + FILEIO_BACKUP_PAGE_OVERHEAD)
#define FILEIO_BACKUP_DBVOLS_IO_PAGE_SIZE_FULL_LEVEL \
  (FILEIO_PAGE_SIZE_FULL_LEVEL + FILEIO_BACKUP_PAGE_OVERHEAD)

#define FILEIO_RESTORE_DBVOLS_IO_PAGE_SIZE(sess)  \
  ((sess)->bkup.bkuphdr->bkpagesize + FILEIO_BACKUP_PAGE_OVERHEAD)

#define FILEIO_DBVOLS_IO_PAGE_SIZE(backup_header_p)  \
  ((backup_header_p)->bkpagesize + FILEIO_BACKUP_PAGE_OVERHEAD)

#define FILEIO_BACKUP_FILE_HEADER_PAGE_SIZE  \
  (sizeof(FILEIO_BACKUP_FILE_HEADER) + offsetof(FILEIO_BACKUP_PAGE, iopage))

/* Set just the redundant copy of the pageid to the given page. */
#define FILEIO_SET_BACKUP_PAGE_ID_COPY(area, pageid, psize) \
  *(PAGEID *)(((char *)(area)) + \
  (offsetof(FILEIO_BACKUP_PAGE, iopage) + psize)) = pageid

/* Set the backup page pageid(s) */
#define FILEIO_SET_BACKUP_PAGE_ID(area, pageid, psize) \
  do { \
      ((FILEIO_BACKUP_PAGE *)(area))->iopageid = pageid; \
      /* set the redundant copy of the pageid, alignment is important */ \
      FILEIO_SET_BACKUP_PAGE_ID_COPY(area, pageid, psize); \
  } while (false);

/* Get the backup page primary pageid */
#define FILEIO_GET_BACKUP_PAGE_ID(area)  (((FILEIO_BACKUP_PAGE *)(area))->iopageid)

/*
 * Verify the integrity of the page just read by checking the redundant
 * copy of the pageid.  Remember that the ->iopageid_copy cannot be accessed
 * directly and must be retrieved by pointer offset.  If the two pageid's
 * do not match it is probably a corrupted page.
 */
#define FILEIO_CHECK_RESTORE_PAGE_ID(area, pagesz) \
   (((FILEIO_BACKUP_PAGE *)(area))->iopageid == \
    *(PAGEID *)(((char *)(area)) + offsetof(FILEIO_BACKUP_PAGE, iopage) + pagesz))

/* Define minimum number of pages required for a backup volume
   For now, specify at least 4 pages plus the header. */
#define FILEIO_BACKUP_MINIMUM_NUM_PAGES \
  CEIL_PTVDIV((FILEIO_BACKUP_HEADER_IO_SIZE +   \
              (FILEIO_BACKUP_DBVOLS_IO_PAGE_SIZE) * 4), IO_PAGESIZE)
#define FILEIO_BACKUP_MINIMUM_NUM_PAGES_FULL_LEVEL \
  CEIL_PTVDIV((FILEIO_BACKUP_HEADER_IO_SIZE +   \
              (FILEIO_BACKUP_DBVOLS_IO_PAGE_SIZE_FULL_LEVEL) * 4), IO_PAGESIZE)

/* Some specifications of page identifiers of backup */
#define FILEIO_BACKUP_START_PAGE_ID      (-2)
#define FILEIO_BACKUP_END_PAGE_ID        (-3)
#define FILEIO_BACKUP_FILE_START_PAGE_ID (-4)
#define FILEIO_BACKUP_FILE_END_PAGE_ID   (-5)
#define FILEIO_BACKUP_VOL_CONT_PAGE_ID   (-6)

/* User input states when requesting relocation of a (backup) volume. */
typedef enum
{
  FILEIO_RELOCATION_FIRST = 0,
  FILEIO_RELOCATION_QUIT = FILEIO_RELOCATION_FIRST,
  FILEIO_RELOCATION_RETRY,
  FILEIO_RELOCATION_ALTERNATE,
  FILEIO_RELOCATION_LAST = FILEIO_RELOCATION_ALTERNATE
} FILEIO_RELOCATION_VOLUME;

typedef struct fileio_backup_file_header FILEIO_BACKUP_FILE_HEADER;
typedef struct fileio_bkvinf_entry FILEIO_BACKUP_INFO_ENTRY;
typedef struct fileio_bkvinf_queues FILEIO_BACKUP_INFO_QUEUE;

/* A FILE/VOLUME HEADER IN BACKUP */
struct fileio_backup_file_header
{
  INT64 nbytes;
  VOLID volid;
  short dummy1;			/* Dummy field for 8byte align */
  int dummy2;			/* Dummy field for 8byte align */
  char vlabel[PATH_MAX];
};

/* Some specifications for bkvinf data */

/* Each one of these represents a given backup volume unit */
struct fileio_bkvinf_entry
{
  int unit_num;
  char bkvol_name[PATH_MAX];
  FILEIO_BACKUP_INFO_ENTRY *link;
};

/* Master data structure to retain information about each backup level */
struct fileio_bkvinf_queues
{
  bool initialized;
  FILEIO_BACKUP_INFO_ENTRY *anchors[FILEIO_BACKUP_UNDEFINED_LEVEL];
  FILEIO_BACKUP_INFO_ENTRY *free;
};

/* Records information from the bkvinf file about backup volumes */
static FILEIO_BACKUP_INFO_QUEUE fileio_Backup_vol_info_data[2] =
  { {false, {NULL, NULL, NULL}, NULL}, {false, {NULL, NULL, NULL}, NULL} };

#if defined(CUBRID_DEBUG)
/* Set this to get various levels of io information regarding
 * backup and restore activity.
 * 0         :: no output
 * 1         :: print names and sizes of volumes that are backed-up.
 * 2         :: dump page bitmaps after volume is restored.
 */
static int io_Bkuptrace_debug = -1;
#endif /* CUBRID_DEBUG */

static int fileio_create_backup_volume (THREAD_ENTRY * thread_p, const char *db_fullname, const char *vlabel,
					VOLID volid, bool dolock, bool dosync, int atleast_pages);

static int fileio_flush_backup (THREAD_ENTRY * thread_p, FILEIO_BACKUP_SESSION * session);
static ssize_t fileio_read_backup (THREAD_ENTRY * thread_p, FILEIO_BACKUP_SESSION * session, int pageid);
static int fileio_write_backup (THREAD_ENTRY * thread_p, FILEIO_BACKUP_SESSION * session, ssize_t towrite_nbytes);
static int fileio_write_backup_header (FILEIO_BACKUP_SESSION * session);

static FILEIO_BACKUP_SESSION *fileio_initialize_restore (THREAD_ENTRY * thread_p, const char *db_fullname,
							 char *backup_src, FILEIO_BACKUP_SESSION * session,
							 FILEIO_BACKUP_LEVEL level,
							 const char *restore_verbose_file_path, bool newvolpath);
static int fileio_read_restore (THREAD_ENTRY * thread_p, FILEIO_BACKUP_SESSION * session, int toread_nbytes);
static void *fileio_write_restore (THREAD_ENTRY * thread_p, FILEIO_RESTORE_PAGE_BITMAP * page_bitmap, int vdes,
				   void *io_pgptr, VOLID vol_id, PAGEID page_id);
static int fileio_read_restore_header (FILEIO_BACKUP_SESSION * session);
static FILEIO_RELOCATION_VOLUME fileio_find_restore_volume (THREAD_ENTRY * thread_p, const char *dbname,
							    char *to_volname, int unit_num, FILEIO_BACKUP_LEVEL level,
							    int reason);

static int fileio_get_next_backup_volume (THREAD_ENTRY * thread_p, FILEIO_BACKUP_SESSION * session, bool user_new);
static int fileio_initialize_backup_info (int which_bkvinf);
static FILEIO_BACKUP_INFO_ENTRY *fileio_allocate_backup_info (int which_bkvinf);

static FILEIO_BACKUP_SESSION *fileio_continue_restore (THREAD_ENTRY * thread_p, const char *db_fullname,
						       INT64 db_creation, FILEIO_BACKUP_SESSION * session,
						       bool first_time, bool authenticate, INT64 match_bkupcreation);
static int fileio_fill_hole_during_restore (THREAD_ENTRY * thread_p, int *next_pageid, int stop_pageid,
					    FILEIO_BACKUP_SESSION * session, FILEIO_RESTORE_PAGE_BITMAP * page_bitmap);
static int fileio_decompress_restore_volume (THREAD_ENTRY * thread_p, FILEIO_BACKUP_SESSION * session, int nbytes);
static FILEIO_NODE *fileio_allocate_node (FILEIO_QUEUE * qp, FILEIO_BACKUP_HEADER * backup_hdr);
static FILEIO_NODE *fileio_free_node (FILEIO_QUEUE * qp, FILEIO_NODE * node);
static FILEIO_NODE *fileio_delete_queue_head (FILEIO_QUEUE * qp);
static int fileio_compress_backup_node (FILEIO_NODE * node, FILEIO_BACKUP_HEADER * backup_hdr);
static int fileio_write_backup_node (THREAD_ENTRY * thread_p, FILEIO_BACKUP_SESSION * session, FILEIO_NODE * node,
				     FILEIO_BACKUP_HEADER * backup_hdr);

static const char *fileio_get_backup_level_string (FILEIO_BACKUP_LEVEL level);

static int fileio_initialize_backup_thread (FILEIO_BACKUP_SESSION * session_p, int num_threads);
static void fileio_finalize_backup_thread (FILEIO_BACKUP_SESSION * session_p, FILEIO_ZIP_METHOD zip_method);
static int fileio_write_backup_end_time_to_header (FILEIO_BACKUP_SESSION * session_p, INT64 end_time);
static void fileio_write_backup_end_time_to_last_page (FILEIO_BACKUP_SESSION * session_p, INT64 end_time);
static void fileio_read_backup_end_time_from_last_page (FILEIO_BACKUP_SESSION * session_p);

#if defined(SERVER_MODE)
static void fileio_read_backup_volume (THREAD_ENTRY * thread_p, FILEIO_BACKUP_SESSION * session);
static FILEIO_TYPE fileio_write_backup_volume (THREAD_ENTRY * thread_p, FILEIO_BACKUP_SESSION * session);
static FILEIO_NODE *fileio_append_queue (FILEIO_QUEUE * qp, FILEIO_NODE * node);
#endif /* SERVER_MODE */

static void fileio_page_bitmap_set (FILEIO_RESTORE_PAGE_BITMAP * page_bitmap, int page_id);
static bool fileio_page_bitmap_is_set (FILEIO_RESTORE_PAGE_BITMAP * page_bitmap, int page_id);
static void fileio_page_bitmap_dump (FILE * out_fp, const FILEIO_RESTORE_PAGE_BITMAP * page_bitmap);

/*
 * fileio_create_backup_volume () - CREATE A BACKUP VOLUME (INSURE ENOUGH SPACE EXISTS)
 *   return: volume descriptor identifier on success, NULL_VOLDES on failure
 *   db_fullname(in): Name of the database where the volume belongs
 *   vlabel(in): Volume label
 *   volid(in): Volume identifier
 *   dolock(in): Lock the volume from other Unix processes
 *   dosync(in): synchronize the writes on the volume ?
 *   atleast_npages(in): minimum number of pages required to be free
 *
 * Note: Tests to insure that there is at least the minimum requred amount of
 *       space on the given file system are.  Then calls fileio_create to create
 *       the volume (or file) without initializing it. This is needed for tape
 *       backups since they are not initialized at all plus saves time w/out
 *       formatting.
 *       Note: Space checking does not apply to devices, only files.
 */
static int
fileio_create_backup_volume (THREAD_ENTRY * thread_p, const char *db_full_name_p, const char *vol_label_p, VOLID vol_id,
			     bool is_do_lock, bool is_do_sync, int atleast_npages)
{
  struct stat stbuf;
  int num_free;

  if (stat (vol_label_p, &stbuf) != -1)
    {
#if !defined(WINDOWS)
      /* In WINDOWS platform, FIFO is not supported, until now. FIFO must be existent before backup operation is
       * executed. */
      if (S_ISFIFO (stbuf.st_mode))
	{
	  int vdes;
	  struct timeval to = { 0, 100000 };

	  while (true)
	    {
	      vdes = fileio_open (vol_label_p, O_WRONLY | O_NONBLOCK, 0200);
	      if (vdes != NULL_VOLDES)
		{
		  break;
		}

	      if (errno == ENXIO)
		{
		  /* sleep for 100 milli-seconds : consider cs & sa mode */
		  select (0, NULL, NULL, NULL, &to);

		  if (pgbuf_is_log_check_for_interrupts (thread_p) == true)
		    {
		      return NULL_VOLDES;
		    }
		  continue;
		}
	      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IO_FORMAT_FAIL, 3, vol_label_p, -1, -1LL);
	      return NULL_VOLDES;
	    }
	  return vdes;
	}
#endif /* !WINDOWS */
      /* If there is not enough space in filesystem, then do not bother opening backup volume */
      if (atleast_npages > 0 && S_ISREG (stbuf.st_mode))
	{
	  num_free = fileio_get_number_of_partition_free_pages (vol_label_p, IO_PAGESIZE);
	  if (num_free < atleast_npages)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IO_FORMAT_OUT_OF_SPACE, 5, vol_label_p, atleast_npages,
		      (long long) ((IO_PAGESIZE / 1024) * atleast_npages), num_free,
		      (long long) ((IO_PAGESIZE / 1024) * num_free));
	      return NULL_VOLDES;
	    }
	}
    }

  return (fileio_create (thread_p, db_full_name_p, vol_label_p, vol_id, is_do_lock, is_do_sync));
}

/*
 * fileio_make_temp_log_files_from_backup () - Build the name of volumes
 *   return: void
 *   logactive_name(out):
 *   level(in):
 *   active_name(in):
 *
 * Note: The caller must have enough space to store the name of the volume
 *       that is constructed(sprintf). It is recommended to have at least
 *       DB_MAX_PATH_LENGTH length.
 */
void
fileio_make_temp_log_files_from_backup (char *temp_log_name, VOLID to_volid, FILEIO_BACKUP_LEVEL level,
					const char *base_log_name)
{
  switch (to_volid)
    {
    case LOG_DBLOG_ACTIVE_VOLID:
      sprintf (temp_log_name, "%s_%03d_tmp", base_log_name, level);
      break;
    case LOG_DBLOG_INFO_VOLID:
      sprintf (temp_log_name, "%s_%03d_tmp", base_log_name, level);
      break;
    case LOG_DBLOG_ARCHIVE_VOLID:
      sprintf (temp_log_name, "%s_%03d_tmp", base_log_name, level);
      break;
    default:
      break;
    }
}

/*
 * fileio_make_backup_volume_info_name () - Build the name of volumes
 *   return: void
 *   backup_volinfo_name(out):
 *   backinfo_path(in):
 *   dbname(in):
 *
 * Note: The caller must have enough space to store the name of the volume
 *       that is constructed(sprintf). It is recommended to have at least
 *       DB_MAX_PATH_LENGTH length.
 */
void
fileio_make_backup_volume_info_name (char *backup_volinfo_name_p, const char *backup_info_path_p, const char *db_name_p)
{
  sprintf (backup_volinfo_name_p, "%s%s%s%s", backup_info_path_p, FILEIO_PATH_SEPARATOR (backup_info_path_p), db_name_p,
	   FILEIO_SUFFIX_BACKUP_VOLINFO);
}

/*
 * fileio_make_backup_name () - Build the name of volumes
 *   return: void
 *   backup_name(out):
 *   nopath_volname(in):
 *   backup_path(in):
 *   level(in):
 *   unit_num(in):
 *
 * Note: The caller must have enough space to store the name of the volume
 *       that is constructed(sprintf). It is recommended to have at least
 *       DB_MAX_PATH_LENGTH length.
 */
void
fileio_make_backup_name (char *backup_name_p, const char *no_path_vol_name_p, const char *backup_path_p,
			 FILEIO_BACKUP_LEVEL level, int unit_num)
{
  if (unit_num >= 0)
    {
      sprintf (backup_name_p, "%s%c%s%s%dv%03d", backup_path_p, PATH_SEPARATOR, no_path_vol_name_p,
	       FILEIO_SUFFIX_BACKUP, level, unit_num);
    }
  else
    {
      /* without unit number, usually with FILEIO_NO_BACKUP_UNITS */
      sprintf (backup_name_p, "%s%c%s%s%d", backup_path_p, PATH_SEPARATOR, no_path_vol_name_p, FILEIO_SUFFIX_BACKUP,
	       level);
    }
}

#if 0
  /* currently, disable the following code. DO NOT DELETE ME NEED FUTURE OPTIMIZATION */
static void
fileio_determine_backup_buffer_size (FILEIO_BACKUP_SESSION * session_p, int buf_size)
{
  int vol_size, max_buf_size;
  vol_size = DB_INT32_MAX;	/* 2G */
  if ((int) prm_get_bigint_value (PRM_ID_IO_BACKUP_MAX_VOLUME_SIZE) > 0)
    {
      vol_size = MIN (vol_size, (int) prm_get_bigint_value (PRM_ID_IO_BACKUP_MAX_VOLUME_SIZE));
    }

  vol_size -= (FILEIO_BACKUP_HEADER_IO_SIZE + FILEIO_BACKUP_FILE_HEADER_PAGE_SIZE);
  max_buf_size = buf_size * prm_get_integer_value (PRM_ID_IO_BACKUP_NBUFFERS);
  while (max_buf_size > buf_size)
    {
      if (vol_size % max_buf_size < buf_size)
	{
	  break;		/* OK */
	}
      max_buf_size -= buf_size;
    }

  session_p->bkup.iosize = max_buf_size;
}
#endif

static int
fileio_initialize_backup_thread (FILEIO_BACKUP_SESSION * session_p, int num_threads)
{
  FILEIO_THREAD_INFO *thread_info_p;
  FILEIO_QUEUE *queue_p;
#if defined(SERVER_MODE)
  int num_cpus;
  int rv;
#endif /* SERVER_MODE */

  thread_info_p = &session_p->read_thread_info;
  queue_p = &thread_info_p->io_queue;

#if defined(SERVER_MODE)
  rv = pthread_mutex_init (&thread_info_p->mtx, NULL);
  if (rv != 0)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_PTHREAD_MUTEX_INIT, 0);
      return ER_CSS_PTHREAD_MUTEX_INIT;
    }

  rv = pthread_cond_init (&thread_info_p->rcv, NULL);
  if (rv != 0)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_PTHREAD_COND_INIT, 0);
      return ER_CSS_PTHREAD_COND_INIT;
    }

  rv = pthread_cond_init (&thread_info_p->wcv, NULL);
  if (rv != 0)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_PTHREAD_COND_INIT, 0);
      return ER_CSS_PTHREAD_COND_INIT;
    }

  /* get the number of CPUs */
  // *INDENT-OFF*
  num_cpus = cubthread::system_core_count ();
  // *INDENT-ON*
  /* check for the upper bound of threads */
  if (num_threads == FILEIO_BACKUP_NUM_THREADS_AUTO)
    {
      thread_info_p->num_threads = num_cpus;
    }
  else
    {
      thread_info_p->num_threads = MIN (num_threads, num_cpus);
    }
  thread_info_p->num_threads = MIN (thread_info_p->num_threads, NUM_NORMAL_TRANS);
#else /* SERVER_MODE */
  thread_info_p->num_threads = 1;
#endif /* SERVER_MODE */
#if defined(CUBRID_DEBUG)
  fprintf (stdout, "PRM_CSS_MAX_CLIENTS = %d, tp->num_threads = %d\n", prm_get_integer_value (PRM_ID_CSS_MAX_CLIENTS),
	   thread_info_p->num_threads);
#endif /* CUBRID_DEBUG */
  queue_p->size = 0;
  queue_p->head = NULL;
  queue_p->tail = NULL;
  queue_p->free_list = NULL;

  thread_info_p->initialized = true;

  return NO_ERROR;
}

/*
 * fileio_initialize_backup () - Initialize the backup session structure with the given
 *                     information
 *   return: session or NULL
 *   db_fullname(in):  Name of the database to backup
 *   backup_destination(in): Name of backup device (file or directory)
 *   session(out): The session array
 *   level(in): The presumed backup level
 *   verbose_file_path(in): verbose mode file path
 *   num_threads(in): number of threads
 *   sleep_msecs(in): sleep interval in msecs
 */
FILEIO_BACKUP_SESSION *
fileio_initialize_backup (const char *db_full_name_p, const char *backup_destination_p,
			  FILEIO_BACKUP_SESSION * session_p, FILEIO_BACKUP_LEVEL level, const char *verbose_file_path,
			  int num_threads, int sleep_msecs)
{
  int vol_fd;
  int size;
  const char *db_nopath_name_p;
  struct stat stbuf;
  int buf_size;
  int io_page_size;
  const char *verbose_fp_mode;

  /*
   * First assume that backup device is a regular file or a raw device.
   * Adjustments are made at a later point, if the backup_destination is
   * a directory.
   */
  strncpy_bufsize (session_p->bkup.name, backup_destination_p);
  strncpy_bufsize (session_p->bkup.current_path, backup_destination_p);
  session_p->bkup.vlabel = session_p->bkup.name;
  session_p->bkup.vdes = NULL_VOLDES;
  session_p->bkup.dtype = FILEIO_BACKUP_VOL_UNKNOWN;
  session_p->dbfile.level = level;
  session_p->bkup.buffer = NULL;
  session_p->bkup.bkuphdr = NULL;
  session_p->dbfile.area = NULL;

  /* Now find out the type of backup_destination and the best page I/O for the backup. The accepted types are either
   * file, directory, or raw device. */
  while (stat (backup_destination_p, &stbuf) == -1)
    {
      /*
       * Could not stat or backup_destination is a file or directory that does not exist.
       * If the backup_destination does not exist, try to create it to make sure that we can write at this backup
       * destination.
       */
      vol_fd = fileio_open (backup_destination_p, FILEIO_DISK_FORMAT_MODE, FILEIO_DISK_PROTECTION_MODE);
      if (vol_fd == NULL_VOLDES)
	{
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IO_MOUNT_FAIL, 1, backup_destination_p);
	  return NULL;
	}
      fileio_close (vol_fd);
      continue;
    }

  if (S_ISDIR (stbuf.st_mode))
    {
      /*
       * This is a DIRECTORY where the backup is going to be sent.
       * The name of the backup file in this directory is labeled as databasename.bkLvNNN (Unix).
       * In this case, we may destroy any previous backup in this directory.
       */
      session_p->bkup.dtype = FILEIO_BACKUP_VOL_DIRECTORY;
      db_nopath_name_p = fileio_get_base_file_name (db_full_name_p);
      fileio_make_backup_name (session_p->bkup.name, db_nopath_name_p, backup_destination_p, level,
			       FILEIO_INITIAL_BACKUP_UNITS);
    }
  else if (S_ISREG (stbuf.st_mode))
    {
      /* Regular file. Remember the directory of this file, in case more volumes are needed. */
      session_p->bkup.dtype = FILEIO_BACKUP_VOL_DIRECTORY;
      fileio_get_directory_path (session_p->bkup.current_path, backup_destination_p);
    }
  else
    {
      /*
       * ASSUME that everything else is a special file such as a FIFO file, a device(character or block special file)
       * which is not named for I/O purposes. That is, the name of the device or regular file is used as the backup.
       */
      session_p->bkup.dtype = FILEIO_BACKUP_VOL_DEVICE;
    }

#if defined(WINDOWS)
  buf_size = 4096;
#else /* WINDOWS */
  buf_size = stbuf.st_blksize;
#endif /* WINDOWS */
  /* User may override the default size by specifying a multiple of the natural block size for the device. */
  session_p->bkup.iosize = buf_size * prm_get_integer_value (PRM_ID_IO_BACKUP_NBUFFERS);
#if 0
  /* currently, disable the following code. DO NOT DELETE ME NEED FUTURE OPTIMIZATION */
  fileio_determine_backup_buffer_size (session_p, buf_size);
#endif
  if (prm_get_bigint_value (PRM_ID_IO_BACKUP_MAX_VOLUME_SIZE) > 0
      && ((UINT64) session_p->bkup.iosize >=
	  MIN (prm_get_bigint_value (PRM_ID_IO_BACKUP_MAX_VOLUME_SIZE), DB_INT32_MAX)))
    {
      er_log_debug (ARG_FILE_LINE,
		    "Backup block buffer size %ld must be less "
		    "than backup volume size %ld, resetting buffer size to %d\n", session_p->bkup.iosize,
		    prm_get_bigint_value (PRM_ID_IO_BACKUP_MAX_VOLUME_SIZE), buf_size);
      session_p->bkup.iosize = MIN (buf_size, DB_INT32_MAX);
    }

#if defined(CUBRID_DEBUG)
  /* These print statements are candidates for part of a "verbose" option to backupdb. */
  fprintf (stdout, "NATURAL BUFFER SIZE %ld (%d IO buffer blocks)\n", session_p->bkup.iosize,
	   session_p->bkup.iosize / buf_size);
  fprintf (stdout, "BACKUP_MAX_VOLUME_SIZE = %ld\n", prm_get_bigint_value (PRM_ID_IO_BACKUP_MAX_VOLUME_SIZE));
#endif /* CUBRID_DEBUG */
  /*
   * Initialize backup device related information.
   *
   * Make sure it is large enough to hold various headers and pages.
   * Beware that upon restore, both the backup buffer size and the
   * database io pagesize may be different.
   */
  io_page_size = IO_PAGESIZE;
  if (session_p->dbfile.level == FILEIO_BACKUP_FULL_LEVEL)
    {
      io_page_size *= FILEIO_FULL_LEVEL_EXP;
    }

  size = MAX (io_page_size + FILEIO_BACKUP_PAGE_OVERHEAD, FILEIO_BACKUP_FILE_HEADER_PAGE_SIZE);

  session_p->bkup.buffer = (char *) malloc (session_p->bkup.iosize);
  if (session_p->bkup.buffer == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, session_p->bkup.iosize);

      goto error;
    }

  session_p->dbfile.area = (FILEIO_BACKUP_PAGE *) malloc (size);
  if (session_p->dbfile.area == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) size);

      goto error;
    }

  session_p->bkup.bkuphdr = (FILEIO_BACKUP_HEADER *) malloc (FILEIO_BACKUP_HEADER_IO_SIZE);
  if (session_p->bkup.bkuphdr == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, FILEIO_BACKUP_HEADER_IO_SIZE);

      goto error;
    }

  session_p->bkup.ptr = session_p->bkup.buffer;
  session_p->bkup.count = 0;
  session_p->bkup.voltotalio = 0;
  session_p->bkup.alltotalio = 0;
  session_p->bkup.bkuphdr->unit_num = FILEIO_INITIAL_BACKUP_UNITS;
  session_p->bkup.bkuphdr->level = level;
  session_p->bkup.bkuphdr->bkup_iosize = session_p->bkup.iosize;
  session_p->bkup.bkuphdr->bk_hdr_version = FILEIO_BACKUP_CURRENT_HEADER_VERSION;
  LSA_SET_NULL (&session_p->bkup.bkuphdr->start_log_end_lsa);
  session_p->bkup.bkuphdr->start_time = 0;
  session_p->bkup.bkuphdr->end_time = -1;
  memset (session_p->bkup.bkuphdr->db_prec_bkvolname, 0, sizeof (session_p->bkup.bkuphdr->db_prec_bkvolname));
  memset (session_p->bkup.bkuphdr->db_next_bkvolname, 0, sizeof (session_p->bkup.bkuphdr->db_next_bkvolname));
  session_p->bkup.bkuphdr->zip_method = FILEIO_ZIP_NONE_METHOD;
  session_p->bkup.bkuphdr->zip_level = FILEIO_ZIP_NONE_LEVEL;
  /* Initialize database file related information */
  LSA_SET_NULL (&session_p->dbfile.lsa);
  session_p->dbfile.vlabel = NULL;
  session_p->dbfile.volid = NULL_VOLID;
  session_p->dbfile.vdes = NULL_VOLDES;
  session_p->dbfile.nbytes = -1;
  FILEIO_SET_BACKUP_PAGE_ID (session_p->dbfile.area, NULL_PAGEID, io_page_size);

#if defined(CUBRID_DEBUG)
  fprintf (stdout, "fileio_initialize_backup: %d\t%d,\t%d\n",
	   ((FILEIO_BACKUP_PAGE *) (session_p->dbfile.area))->iopageid,
	   *(PAGEID *) (((char *) (session_p->dbfile.area)) + offsetof (FILEIO_BACKUP_PAGE, iopage) + io_page_size),
	   io_page_size);
#endif

  if (fileio_initialize_backup_thread (session_p, num_threads) != NO_ERROR)
    {
      goto error;
    }

  if (verbose_file_path && *verbose_file_path)
    {
      if (session_p->type == FILEIO_BACKUP_WRITE && level == 0)
	{
	  verbose_fp_mode = "w";
	}
      else
	{
	  verbose_fp_mode = "a";
	}

      session_p->verbose_fp = fopen (verbose_file_path, verbose_fp_mode);
      if (session_p->verbose_fp == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IO_CANNOT_OPEN_VERBOSE_FILE, 1, verbose_file_path);

	  goto error;
	}

      setbuf (session_p->verbose_fp, NULL);
    }
  else
    {
      session_p->verbose_fp = NULL;
    }

  session_p->sleep_msecs = sleep_msecs;

  return session_p;

error:
  if (session_p->bkup.buffer != NULL)
    {
      free_and_init (session_p->bkup.buffer);
    }
  if (session_p->dbfile.area != NULL)
    {
      free_and_init (session_p->dbfile.area);
    }
  if (session_p->bkup.bkuphdr != NULL)
    {
      free_and_init (session_p->bkup.bkuphdr);
    }
  if (session_p->verbose_fp != NULL)
    {
      fclose (session_p->verbose_fp);
      session_p->verbose_fp = NULL;
    }

  return NULL;
}

/*
 * fileio_finalize_backup_thread() -
 *    return: void
 *
 *    session_p(in/out):
 *    zip_method(in):
 */
static void
fileio_finalize_backup_thread (FILEIO_BACKUP_SESSION * session_p, FILEIO_ZIP_METHOD zip_method)
{
  FILEIO_THREAD_INFO *tp;
  FILEIO_QUEUE *qp;
  FILEIO_NODE *node, *node_next;
#if defined(SERVER_MODE)
  int rv;
#endif /* SERVER_MODE */

  tp = &session_p->read_thread_info;
  qp = &tp->io_queue;

  if (tp->initialized == false)
    {
      return;
    }

#if defined(SERVER_MODE)
  rv = pthread_mutex_destroy (&tp->mtx);
  if (rv != 0)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_PTHREAD_MUTEX_DESTROY, 0);
    }

  rv = pthread_cond_destroy (&tp->rcv);
  if (rv != 0)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_PTHREAD_COND_DESTROY, 0);
    }

  rv = pthread_cond_destroy (&tp->wcv);
  if (rv != 0)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_PTHREAD_COND_DESTROY, 0);
    }
#endif /* SERVER_MODE */

  while (qp->size > 0)
    {
      node = fileio_delete_queue_head (qp);
      (void) fileio_free_node (qp, node);
    }

  for (node = qp->free_list; node; node = node_next)
    {
      node_next = node->next;
      switch (zip_method)
	{
	case FILEIO_ZIP_LZ4_METHOD:
	  if (node->zip_info != NULL)
	    {
	      free_and_init (node->zip_info);
	    }
	  break;
	case FILEIO_ZIP_LZO1X_METHOD:
	  break;
	case FILEIO_ZIP_ZLIB_METHOD:
	  break;
	default:
	  break;
	}

      if (node->area != NULL)
	{
	  free_and_init (node->area);
	}

      if (node != NULL)
	{
	  free_and_init (node);
	}
    }

  qp->free_list = NULL;

  tp->initialized = false;
}

/*
 * fileio_abort_backup () - The backup session is aborted
 *   return: void
 *   session(in/out):  The session array
 *   does_unformat_bk(in): set to TRUE to delete backup volumes we know about
 *
 * Note: The currently created backup file can be removed if desired. This
 *       routine is called in for normal cleanup as well as to handle
 *       exceptions.
 */
void
fileio_abort_backup (THREAD_ENTRY * thread_p, FILEIO_BACKUP_SESSION * session_p, bool does_unformat_bk)
{
  FILEIO_BACKUP_HEADER *backup_header_p = session_p->bkup.bkuphdr;
  FILEIO_ZIP_METHOD zip_method;

  zip_method = backup_header_p ? backup_header_p->zip_method : FILEIO_ZIP_NONE_METHOD;
  /* Remove the currently created backup */
  if (session_p->bkup.vdes != NULL_VOLDES)
    {
      if (session_p->bkup.dtype == FILEIO_BACKUP_VOL_DIRECTORY)
	{
	  if (session_p->type == FILEIO_BACKUP_READ)
	    {
	      /* access backup device for read */
	      fileio_dismount_without_fsync (thread_p, session_p->bkup.vdes);
	    }
	  else
	    {
	      /* access backup device for write */
	      fileio_dismount (thread_p, session_p->bkup.vdes);
	    }
	}
      else
	{
	  fileio_dismount_without_fsync (thread_p, session_p->bkup.vdes);
	}
    }

  /* Destroy the current backup volumes */
  if (does_unformat_bk)
    {
      /* Remove current backup volume */
      if (session_p->bkup.dtype == FILEIO_BACKUP_VOL_DIRECTORY
	  && fileio_is_volume_exist_and_file (session_p->bkup.vlabel))
	{
	  fileio_unformat (thread_p, session_p->bkup.vlabel);
	}

      /* Remove backup volumes previous to this one */
      if (session_p->bkup.bkuphdr)
	{
	  fileio_remove_all_backup (thread_p, session_p->bkup.bkuphdr->level);
	}
    }

  if (session_p->verbose_fp)
    {
      fclose (session_p->verbose_fp);
      session_p->verbose_fp = NULL;
    }

  /* Deallocate memory space */
  if (session_p->bkup.buffer != NULL)
    {
      free_and_init (session_p->bkup.buffer);
    }

  if (session_p->dbfile.area != NULL)
    {
      free_and_init (session_p->dbfile.area);
    }

  if (session_p->bkup.bkuphdr != NULL)
    {
      free_and_init (session_p->bkup.bkuphdr);
    }

  fileio_finalize_backup_info (FILEIO_FIRST_BACKUP_VOL_INFO);
  LSA_SET_NULL (&session_p->dbfile.lsa);
  session_p->dbfile.level = FILEIO_BACKUP_UNDEFINED_LEVEL;
  session_p->dbfile.vdes = NULL_VOLDES;
  session_p->dbfile.volid = NULL_VOLID;
  session_p->dbfile.vlabel = NULL;
  session_p->dbfile.nbytes = -1;
  session_p->dbfile.area = NULL;
  session_p->bkup.vdes = NULL_VOLDES;
  session_p->bkup.vlabel = NULL;
  session_p->bkup.iosize = -1;
  session_p->bkup.count = 0;
  session_p->bkup.voltotalio = 0;
  session_p->bkup.alltotalio = 0;
  session_p->bkup.buffer = session_p->bkup.ptr = NULL;
  session_p->bkup.bkuphdr = NULL;
  fileio_finalize_backup_thread (session_p, zip_method);
}

/*
 * fileio_start_backup () - Start a backup session
 *   return: session or NULL
 *   db_fullname(in): Name of the database to backup
 *   db_creation(in): Creation time of database
 *   backup_level(in): Backup level
 *   backup_start_lsa(in): start lsa for backup
 *   backup_chkpt_lsa(in): checkpoint lsa for backup
 *   all_levels_info(in): previous backup info per level
 *   session(in/out): The session array
 *   zip_method(in): compression method
 *   zip_level(in): compression evel
 *
 * Note: Note that fileio_initialize_backup must have already been invoked on the
 *       session.
 */
FILEIO_BACKUP_SESSION *
fileio_start_backup (THREAD_ENTRY * thread_p, const char *db_full_name_p, INT64 * db_creation_time_p,
		     FILEIO_BACKUP_LEVEL backup_level, LOG_LSA * backup_start_lsa_p, LOG_LSA * backup_checkpoint_lsa_p,
		     FILEIO_BACKUP_RECORD_INFO * all_levels_info_p, FILEIO_BACKUP_SESSION * session_p,
		     FILEIO_ZIP_METHOD zip_method, FILEIO_ZIP_LEVEL zip_level)
{
  FILEIO_BACKUP_HEADER *backup_header_p;
  int i;

  /* Complete the session array initialization and create/open the backup destination device. */
  LSA_COPY (&session_p->dbfile.lsa, backup_start_lsa_p);
  session_p->bkup.vdes =
    fileio_create_backup_volume (thread_p, db_full_name_p, session_p->bkup.vlabel, LOG_DBCOPY_VOLID, false, false,
				 (session_p->dbfile.level == FILEIO_BACKUP_FULL_LEVEL)
				 ? FILEIO_BACKUP_MINIMUM_NUM_PAGES_FULL_LEVEL : FILEIO_BACKUP_MINIMUM_NUM_PAGES);
  if (session_p->bkup.vdes == NULL_VOLDES)
    {
      goto error;
    }

  /* Remember name of new backup volume */
  if (fileio_add_volume_to_backup_info (session_p->bkup.name, session_p->dbfile.level,
					session_p->bkup.bkuphdr->unit_num, FILEIO_FIRST_BACKUP_VOL_INFO) != NO_ERROR)
    {
      goto error;
    }

  /* Write the description/header of the backup to the backup device */

  backup_header_p = session_p->bkup.bkuphdr;
  backup_header_p->iopageid = FILEIO_BACKUP_START_PAGE_ID;
  strncpy (backup_header_p->magic, CUBRID_MAGIC_DATABASE_BACKUP, CUBRID_MAGIC_MAX_LENGTH);
  strncpy_bufsize (backup_header_p->db_release, rel_release_string ());
  strncpy_bufsize (backup_header_p->db_fullname, db_full_name_p);
  backup_header_p->db_creation = *db_creation_time_p;
  backup_header_p->db_iopagesize = IO_PAGESIZE;
  backup_header_p->db_compatibility = rel_disk_compatible ();
  backup_header_p->level = backup_level;
  LSA_COPY (&backup_header_p->start_lsa, backup_start_lsa_p);
  LSA_COPY (&backup_header_p->chkpt_lsa, backup_checkpoint_lsa_p);

  for (i = FILEIO_BACKUP_FULL_LEVEL; i < FILEIO_BACKUP_UNDEFINED_LEVEL; i++)
    {
      if (all_levels_info_p)
	{
	  if (i == FILEIO_BACKUP_FULL_LEVEL)
	    {
	      LSA_SET_NULL (&backup_header_p->previnfo[i].lsa);
	    }
	  else
	    {
	      LSA_COPY (&backup_header_p->previnfo[i].lsa, &all_levels_info_p[i].lsa);
	    }
	  backup_header_p->previnfo[i].at_time = all_levels_info_p[i].at_time;
	}
      else
	{
	  LSA_SET_NULL (&backup_header_p->previnfo[i].lsa);
	  backup_header_p->previnfo[i].at_time = 0;
	}
    }

  backup_header_p->start_time = time (NULL);
  backup_header_p->unit_num = FILEIO_INITIAL_BACKUP_UNITS;
  backup_header_p->bkpagesize = backup_header_p->db_iopagesize;

  if (backup_level == FILEIO_BACKUP_FULL_LEVEL)
    {
      backup_header_p->bkpagesize *= FILEIO_FULL_LEVEL_EXP;
    }

  backup_header_p->zip_method = zip_method;
  backup_header_p->zip_level = zip_level;

  if (backup_header_p->zip_method == FILEIO_ZIP_LZ4_METHOD
      && FILEIO_DBVOLS_IO_PAGE_SIZE (backup_header_p) > LZ4_MAX_INPUT_SIZE)
    {
      goto error;
    }

  /* Now write this information to the backup volume. */
  if (fileio_write_backup_header (session_p) != NO_ERROR)
    {
      goto error;
    }

  return session_p;

error:
  fileio_abort_backup (thread_p, session_p, true);
  return NULL;
}

/*
 * fileio_write_backup_end_time_to_header () - Write the end time of backup
 *                                             to backup volume header
 *   return: error status
 *   session(in): backup session
 *   end_time: the end time of backup
 */
static int
fileio_write_backup_end_time_to_header (FILEIO_BACKUP_SESSION * session_p, INT64 end_time)
{
  const char *first_bkvol_name;
  int vdes, nbytes;

  first_bkvol_name =
    fileio_get_backup_info_volume_name (session_p->dbfile.level, FILEIO_INITIAL_BACKUP_UNITS,
					FILEIO_FIRST_BACKUP_VOL_INFO);

  if (first_bkvol_name == NULL)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IO_MOUNT_FAIL, 1, "(backup volume name is null)");
      return ER_IO_MOUNT_FAIL;
    }

  if (strncmp (first_bkvol_name, session_p->bkup.vlabel, PATH_MAX) == 0)
    {
      session_p->bkup.bkuphdr->end_time = end_time;
      lseek (session_p->bkup.vdes, 0, SEEK_SET);
      fileio_write_backup_header (session_p);
    }
  else
    {
      vdes = fileio_open (first_bkvol_name, O_RDWR, 0);
      if (vdes == NULL_VOLDES)
	{
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IO_MOUNT_FAIL, 1, first_bkvol_name);
	  return ER_IO_MOUNT_FAIL;
	}

      lseek (vdes, offsetof (FILEIO_BACKUP_HEADER, end_time), SEEK_SET);
      nbytes = write (vdes, (char *) &end_time, sizeof (INT64));
      if (nbytes != sizeof (INT64))
	{
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IO_WRITE, 2, 1, first_bkvol_name);
	  fileio_close (vdes);
	  return ER_IO_WRITE;
	}
      fileio_close (vdes);
    }

  return NO_ERROR;
}

/*
 * fileio_write_backup_end_time_to_last_page () - Write the end time of backup
 *                                                to the last page of backup
 *   return: void
 *   session(in): backup session
 *   end_time: the end time of backup
 */
static void
fileio_write_backup_end_time_to_last_page (FILEIO_BACKUP_SESSION * session_p, INT64 end_time)
{
  char *write_to;

  write_to = ((char *) &(session_p->dbfile.area->iopageid)) + offsetof (FILEIO_BACKUP_PAGE, iopage);
  memcpy (write_to, (char *) &end_time, sizeof (INT64));
}

/*
 * fileio_read_backup_end_time_from_last_page () - Read the end time of backup
 *                                                 from the last page of backup
 *   return: void
 *   session(in): backup session
 */
static void
fileio_read_backup_end_time_from_last_page (FILEIO_BACKUP_SESSION * session_p)
{
  char *read_from;

  read_from = ((char *) &(session_p->dbfile.area->iopageid)) + offsetof (FILEIO_BACKUP_PAGE, iopage);
  memcpy ((char *) &(session_p->bkup.bkuphdr->end_time), read_from, sizeof (INT64));
}

/*
 * fileio_finish_backup () - Finish the backup session successfully
 *   return: session or NULL
 *   session(in/out): The session array
 */
FILEIO_BACKUP_SESSION *
fileio_finish_backup (THREAD_ENTRY * thread_p, FILEIO_BACKUP_SESSION * session_p)
{
  int nbytes;
  char *msg_area = NULL;
  char io_time_val[CTIME_MAX];
  INT64 end_time;
  bool make_end_time_strict = false;

  /* logpb_backup () stamps this when the log content of the backup is frozen - the point a restore can actually
   * reach - and does the strictness wait there, while transactions are still held off. Fall back to "now" only
   * if nobody stamped it. */
  end_time = session_p->bkup.bkuphdr->end_time;
  if (end_time == -1)
    {
      end_time = (INT64) time (NULL);
      make_end_time_strict = true;
    }

  /*
   * Indicate end of backup and flush any buffered data.
   * Note that only the end of backup marker is written,
   * so callers of io_restore_read must check for the appropriate
   * end of backup condition.
   */
  session_p->dbfile.area->iopageid = FILEIO_BACKUP_END_PAGE_ID;
  nbytes = offsetof (FILEIO_BACKUP_PAGE, iopage);

  if (session_p->bkup.dtype == FILEIO_BACKUP_VOL_DEVICE)
    {
      fileio_write_backup_end_time_to_last_page (session_p, end_time);
      nbytes += sizeof (INT64);
    }

  if (fileio_write_backup (thread_p, session_p, nbytes) != NO_ERROR)
    {
      return NULL;
    }

  if (session_p->bkup.count > 0)
    {
#if defined(CUBRID_DEBUG)
      fprintf (stdout, "io_backup_end: iosize = %ld, count = %ld, voltotalio = %ld : EOF JUNK\n",
	       session_p->bkup.iosize, session_p->bkup.count, session_p->bkup.voltotalio);
#endif /* CUBRID_DEBUG */
      /*
       * We must add some junk at the end of the buffered area since some
       * backup devices (e.g., Fixed-length I/O tape devices such as
       * 1/4" cartridge tape devices), requires number of bytes to write
       * to be a multiple of the physical record size (io_size).
       */
      nbytes = CAST_BUFLEN (session_p->bkup.iosize - session_p->bkup.count);
      memset (session_p->bkup.ptr, '\0', nbytes);
      session_p->bkup.count = session_p->bkup.iosize;
      /* Flush any buffered information */
      if (fileio_flush_backup (thread_p, session_p) != NO_ERROR)
	{
	  return NULL;
	}
    }

  /*
   * Now, make sure that all the information is physically written to
   * the backup device. That is, make sure that nobody (e.g., backup
   * device controller or OS) is caching data.
   */

  if (session_p->bkup.dtype == FILEIO_BACKUP_VOL_DIRECTORY)
    {
      if (fileio_write_backup_end_time_to_header (session_p, end_time) != NO_ERROR)
	{
	  return NULL;
	}

      if (fileio_synchronize (thread_p, session_p->bkup.vdes, session_p->bkup.name, true) != session_p->bkup.vdes)
	{
	  return NULL;
	}
    }

  /* Tell user that current backup volume just completed */
#if defined(SERVER_MODE) && !defined(WINDOWS)
  if (asprintf (&msg_area, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_IO, MSGCAT_FILEIO_BACKUP_LABEL_INFO),
		session_p->bkup.bkuphdr->level, session_p->bkup.bkuphdr->unit_num,
		fileio_get_base_file_name (session_p->bkup.bkuphdr->db_fullname),
		fileio_ctime (&session_p->bkup.bkuphdr->start_time, io_time_val)) < 0)
#else /* SERVER_MODE && !WINDOWS */
  if (asprintf (&msg_area, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_IO, MSGCAT_FILEIO_BACKUP_LABEL_INFO),
		session_p->bkup.bkuphdr->level, session_p->bkup.bkuphdr->unit_num,
		fileio_get_base_file_name (session_p->bkup.bkuphdr->db_fullname),
		fileio_ctime (&session_p->bkup.bkuphdr->start_time, io_time_val)) < 0)
#endif /* SERVER_MODE && !WINDOWS */
    {
      /* Note: we do not know the exact malloc size that failed */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) FILEIO_MAX_USER_RESPONSE_SIZE);
      return NULL;
    }
  else
    {
      (void) fileio_request_user_response (thread_p, FILEIO_PROMPT_DISPLAY_ONLY, msg_area, NULL, NULL, -1, -1, NULL,
					   -1);
      /* Note: Not free_and_init */
      free (msg_area);
    }

  /*
   * -------------------------------------------------------------------------
   * WORKAROUND: Preventing Restore of Post-Backup Transactions
   * -------------------------------------------------------------------------
   *
   * Problem:
   * The restore process ('restoredb -d backuptime') compares the backup end time
   * (stored in the backup volume header) with each transaction’s commit timestamp
   * (LOG_COMMIT / LOG_ABORT).
   *
   * Issue:
   * Since time(NULL) does not guarantee monotonic increase, the commit time
   * obtained after backup completion can be equal to or earlier than the backup
   * end time. In such cases, point-in-time restore may include transactions that
   * were executed after the backup, which should have been excluded.
   *
   * Workaround:
   * Enforce a 1-second delay after setting the backup end time so that any
   * subsequent transaction obtains a strictly later commit timestamp.
   *
   * Limitation:
   * This workaround resolves timing overlaps for transactions executed
   * immediately after a backup in the same session, but cannot fully separate
   * concurrent transactions across sessions due to second-level timestamp limits.
   *
   * TODO (Permanent Fix):
   * - Use millisecond-level precision in LOG_COMMIT / LOG_ABORT timestamps to
   *   accurately separate concurrent transactions.
   * -------------------------------------------------------------------------
   */
  if (make_end_time_strict)
    {
      /* Only when the end time was stamped here. Waiting after the caller stamped it would guarantee nothing:
       * transactions are already running again by now. */
      do
	{
	  thread_sleep (1000);
	}
      while (end_time >= time (NULL));
    }

  return session_p;
}

/*
 * fileio_remove_all_backup () - REMOVE ALL BACKUP VOLUMES
 *   return: void
 *   start_level(in): the starting level to remove
 *
 * Note: Initialize the backup session structure with the given information.
 *       Remove backup. Cleanup backup. This routine deletes all backups of
 *       a higher level as well. Furthermore, this routine assumes that the
 *       bkvinf cache has already been read in from the bkvinf file.
 */
void
fileio_remove_all_backup (THREAD_ENTRY * thread_p, int start_level)
{
  int level;
  int unit_num;
  const char *vol_name_p;

  level = start_level;
  if (level >= FILEIO_BACKUP_UNDEFINED_LEVEL)
    {
      return;
    }

  if (level < FILEIO_BACKUP_FULL_LEVEL)
    {
      level = FILEIO_BACKUP_FULL_LEVEL;
    }

  do
    {
      unit_num = FILEIO_INITIAL_BACKUP_UNITS;
      while (true)
	{
	  vol_name_p =
	    fileio_get_backup_info_volume_name ((FILEIO_BACKUP_LEVEL) level, unit_num++, FILEIO_FIRST_BACKUP_VOL_INFO);
	  if (vol_name_p == NULL)
	    {
	      break;
	    }

	  if (fileio_is_volume_exist_and_file (vol_name_p))
	    {
	      fileio_unformat (thread_p, vol_name_p);
	    }
	}

      level++;
    }
  while (level < FILEIO_BACKUP_UNDEFINED_LEVEL);
  /* Remove all names just deleted from memory */
  fileio_clear_backup_info_level (start_level, false, FILEIO_FIRST_BACKUP_VOL_INFO);
}

/*
 * fileio_allocate_node () -
 *   return:
 *   qp(in):
 *   backup_hdr(in):
 */
static FILEIO_NODE *
fileio_allocate_node (FILEIO_QUEUE * queue_p, FILEIO_BACKUP_HEADER * backup_header_p)
{
  FILEIO_NODE *node_p;
  int size;
  int zip_info_size, buf_size;

  if (queue_p->free_list)	/* re-use already alloced nodes */
    {
      node_p = queue_p->free_list;
      queue_p->free_list = node_p->next;	/* cut-off */
      return node_p;
    }

  /* at here, need to alloc */
  node_p = (FILEIO_NODE *) malloc (sizeof (FILEIO_NODE));
  if (node_p == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (FILEIO_NODE));
      goto exit_on_error;
    }

  node_p->area = NULL;
  node_p->zip_info = NULL;
  size = FILEIO_DBVOLS_IO_PAGE_SIZE (backup_header_p);
  node_p->area = (FILEIO_BACKUP_PAGE *) malloc (size);
  if (node_p == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) size);
      goto exit_on_error;
    }

  switch (backup_header_p->zip_method)
    {
    case FILEIO_ZIP_LZ4_METHOD:
      assert (size <= LZ4_MAX_INPUT_SIZE);
      // *INDENT-OFF*
      buf_size = cubcompress::bound<cubcompress::LZ4> (size);
      // *INDENT-ON*
      zip_info_size = offsetof (FILEIO_ZIP_INFO, zip_page) + sizeof (int) + buf_size;
      node_p->zip_info = (FILEIO_ZIP_INFO *) malloc (zip_info_size);
      if (node_p->zip_info == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, zip_info_size);
	  goto exit_on_error;
	}
      node_p->zip_info->buf_size = buf_size;

      break;
    case FILEIO_ZIP_LZO1X_METHOD:
      break;
    case FILEIO_ZIP_ZLIB_METHOD:
      break;
    default:
      break;
    }

exit_on_end:

  return node_p;
exit_on_error:

  if (node_p)
    {
      if (node_p->zip_info)
	{
	  free_and_init (node_p->zip_info);
	}

      if (node_p->area)
	{
	  free_and_init (node_p->area);
	}

      free_and_init (node_p);
    }

  node_p = NULL;
  goto exit_on_end;
}

/*
 * fileio_free_node () -
 *   return:
 *   qp(in):
 *   node(in):
 */
static FILEIO_NODE *
fileio_free_node (FILEIO_QUEUE * queue_p, FILEIO_NODE * node_p)
{
  if (node_p)
    {
      node_p->prev = node_p->next = NULL;
      node_p->next = queue_p->free_list;	/* add to free list */
      queue_p->free_list = node_p;
    }

  return node_p;
}

/*
 * fileio_append_queue () -
 *   return:
 *   qp(in):
 *   node(in):
 */
#if defined(SERVER_MODE)
static FILEIO_NODE *
fileio_append_queue (FILEIO_QUEUE * queue_p, FILEIO_NODE * node_p)
{
  if (node_p)
    {
      node_p->prev = node_p->next = NULL;
      node_p->next = queue_p->tail;	/* add to tail */
      if (queue_p->tail)
	{
	  queue_p->tail->prev = node_p;
	}
      queue_p->tail = node_p;
      if (queue_p->head == NULL)
	{
	  /* the first */
	  queue_p->head = node_p;
	}

      queue_p->size++;
    }

  return node_p;
}
#endif /* SERVER_MODE */

/*
 * fileio_delete_queue_head () -
 *   return:
 *   qp(in):
 */
static FILEIO_NODE *
fileio_delete_queue_head (FILEIO_QUEUE * queue_p)
{
  FILEIO_NODE *node;

  node = queue_p->head;
  if (node)
    {
      if (node == queue_p->tail)	/* only one node */
	{
	  queue_p->tail = NULL;
	}
      else
	{
	  node->prev->next = NULL;	/* cut-off */
	}

      queue_p->head = node->prev;
      queue_p->size--;
    }

  return node;
}

/*
 * fileio_compress_backup_node () -
 *   return:
 *   node(in):
 *   backup_hdr(in):
 */
static int
fileio_compress_backup_node (FILEIO_NODE * node_p, FILEIO_BACKUP_HEADER * backup_header_p)
{
  int error = NO_ERROR, local_buf_len;
  FILEIO_ZIP_PAGE *zip_page;

  if (!node_p || !node_p->zip_info || !backup_header_p)
    {
      goto exit_on_error;
    }

  assert (node_p->nread >= 0);

  zip_page = &node_p->zip_info->zip_page;

  switch (backup_header_p->zip_method)
    {
    case FILEIO_ZIP_LZ4_METHOD:
      /* The alternative is compress faster - best speed, but, require more memory alloc */
      // *INDENT-OFF*
      local_buf_len =
	cubcompress::compress<cubcompress::LZ4> ((char *) node_p->area, (int) node_p->nread, zip_page->buf,
						 node_p->zip_info->buf_size);
      // *INDENT-ON*
      if (local_buf_len <= 0)
	{
	  /* best reduction */
	  error = ER_IO_LZ4_COMPRESS_FAIL;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 4, backup_header_p->zip_method,
		  fileio_get_zip_method_string (backup_header_p->zip_method), backup_header_p->zip_level,
		  fileio_get_zip_level_string (backup_header_p->zip_level));
#if defined(CUBRID_DEBUG)
	  fprintf (stdout,
		   "internal error - compression failed: node->pageid = %d, node->nread = %d, "
		   "buf_len = %d, buf_size = %d\n",
		   node_p->pageid, node_p->nread, local_buf_len, node_p->zip_info->buf_size);
#endif /* CUBRID_DEBUG */
	  goto exit_on_error;
	}

      assert (local_buf_len < node_p->zip_info->buf_size);

      if ((ssize_t) local_buf_len < node_p->nread)
	{
	  /* already write compressed block */
	  zip_page->buf_len = local_buf_len;
	}
      else
	{
	  /* not compressible - write uncompressed block */
	  zip_page->buf_len = (int) node_p->nread;
	  memcpy (zip_page->buf, node_p->area, node_p->nread);
	}
      break;
    case FILEIO_ZIP_LZO1X_METHOD:
      error = ER_LOG_DBBACKUP_FAIL;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 4, backup_header_p->zip_method,
	      fileio_get_zip_method_string (backup_header_p->zip_method), backup_header_p->zip_level,
	      fileio_get_zip_level_string (backup_header_p->zip_level));
      goto exit_on_error;
    case FILEIO_ZIP_ZLIB_METHOD:
      break;
    default:
      break;
    }

exit_on_end:

  return error;
exit_on_error:

  if (error == NO_ERROR)
    {
      error = ER_FAILED;
    }
  goto exit_on_end;
}

/*
 * fileio_write_backup_node () -
 *   return:
 *   thread_p(in):
 *   session_p(in/out):
 *   node_p(in):
 *   backup_header_p(in):
 */
static int
fileio_write_backup_node (THREAD_ENTRY * thread_p, FILEIO_BACKUP_SESSION * session_p, FILEIO_NODE * node_p,
			  FILEIO_BACKUP_HEADER * backup_header_p)
{
  int error = NO_ERROR;

  if (!session_p || !node_p || !backup_header_p)
    {
      goto exit_on_error;
    }

  switch (backup_header_p->zip_method)
    {
    case FILEIO_ZIP_LZ4_METHOD:
      /* Skip allocated block size inside of FILEIO_ZIP_PAGE */

      session_p->dbfile.area = (FILEIO_BACKUP_PAGE *) & node_p->zip_info->zip_page;
      node_p->nread = sizeof (int) + node_p->zip_info->zip_page.buf_len;
      break;
    case FILEIO_ZIP_LZO1X_METHOD:
      error = ER_LOG_DBBACKUP_FAIL;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 4, backup_header_p->zip_method,
	      fileio_get_zip_method_string (backup_header_p->zip_method), backup_header_p->zip_level,
	      fileio_get_zip_level_string (backup_header_p->zip_level));
      goto exit_on_error;
    case FILEIO_ZIP_ZLIB_METHOD:
      break;
    default:
      session_p->dbfile.area = node_p->area;
      break;
    }

  if (fileio_write_backup (thread_p, session_p, node_p->nread) != NO_ERROR)
    {
      goto exit_on_error;
    }

exit_on_end:

  return error;

exit_on_error:

  if (error == NO_ERROR)
    {
      error = ER_FAILED;
    }
  goto exit_on_end;
}

/*
 * fileio_read_backup_volume () -
 *   return:
 *   session(in/out):
 */
#if defined(SERVER_MODE)
static void
fileio_read_backup_volume (THREAD_ENTRY * thread_p, FILEIO_BACKUP_SESSION * session_p)
{
  FILEIO_THREAD_INFO *thread_info_p;
  FILEIO_QUEUE *queue_p;
  FILEIO_NODE *node_p = NULL;
  int rv;
  bool need_unlock = false;
  FILEIO_BACKUP_HEADER *backup_header_p;
  FILEIO_BACKUP_PAGE *save_area_p;

  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
      if (thread_p == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	  return;
	}
    }

  if (!session_p)
    {
      return;
    }

  thread_info_p = &session_p->read_thread_info;
  queue_p = &thread_info_p->io_queue;
  thread_p->tran_index = thread_info_p->tran_index;
#if defined(CUBRID_DEBUG)
  fprintf (stdout, "start io_backup_volume_read, session = %p\n", session_p);
#endif /* CUBRID_DEBUG */
  backup_header_p = session_p->bkup.bkuphdr;
  node_p = NULL;		/* init */
  while (1)
    {
      rv = pthread_mutex_lock (&thread_info_p->mtx);
      while (thread_info_p->io_type == FILEIO_WRITE)
	{
	  pthread_cond_wait (&thread_info_p->rcv, &thread_info_p->mtx);
	}

      if (thread_info_p->io_type == FILEIO_ERROR_INTERRUPT)
	{
	  need_unlock = true;
	  node_p = NULL;
	  goto exit_on_error;
	}

      /* get one page from queue head and do write */
      if (node_p)
	{
	  node_p->writeable = true;
	  thread_info_p->io_type = FILEIO_WRITE;
	  pthread_cond_signal (&thread_info_p->wcv);	/* wake up write thread */
	  while (thread_info_p->io_type == FILEIO_WRITE)
	    {
	      pthread_cond_wait (&thread_info_p->rcv, &thread_info_p->mtx);
	    }

	  if (thread_info_p->io_type == FILEIO_ERROR_INTERRUPT)
	    {
	      need_unlock = true;
	      node_p = NULL;
	      goto exit_on_error;
	    }
	}

      /* check EOF */
      if (thread_info_p->pageid >= thread_info_p->from_npages)
	{
	  thread_info_p->end_r_threads++;
	  if (thread_info_p->end_r_threads >= thread_info_p->act_r_threads)
	    {
	      thread_info_p->io_type = FILEIO_WRITE;
	      pthread_cond_signal (&thread_info_p->wcv);	/* wake up write thread */
	    }
	  pthread_mutex_unlock (&thread_info_p->mtx);
	  break;
	}

      /* alloc queue node */
      node_p = fileio_allocate_node (queue_p, backup_header_p);
      if (node_p == NULL)
	{
	  thread_info_p->io_type = FILEIO_ERROR_INTERRUPT;
	  need_unlock = true;
	  goto exit_on_error;
	}

      /* read one page from Disk sequentially */

      save_area_p = session_p->dbfile.area;	/* save link */
      session_p->dbfile.area = node_p->area;
      node_p->pageid = thread_info_p->pageid;
      node_p->writeable = false;	/* init */
      node_p->nread = fileio_read_backup (thread_p, session_p, node_p->pageid);
      session_p->dbfile.area = save_area_p;	/* restore link */
      if (node_p->nread == -1)
	{
	  thread_info_p->io_type = FILEIO_ERROR_INTERRUPT;
	  need_unlock = true;
	  goto exit_on_error;
	}
      else if (node_p->nread == 0)
	{
	  /* This could be an error since we estimated more pages. End of file/volume. */
	  thread_info_p->io_type = FILEIO_ERROR_INTERRUPT;
	  need_unlock = true;
	  goto exit_on_error;
	}

      /* Have to allow other threads to run and check for interrupts from the user (i.e. Ctrl-C ) */
      if ((thread_info_p->pageid % FILEIO_CHECK_FOR_INTERRUPT_INTERVAL) == 0
	  && pgbuf_is_log_check_for_interrupts (thread_p) == true)
	{
#if defined(CUBRID_DEBUG)
	  fprintf (stdout, "io_backup_volume_read interrupt\n");
#endif /* CUBRID_DEBUG */
	  thread_info_p->io_type = FILEIO_ERROR_INTERRUPT;
	  need_unlock = true;
	  goto exit_on_error;
	}

      /*
       * Do we need to backup this page ?
       * In other words, has it been changed since either the previous backup
       * of this level or a lower level.
       */

      if (thread_info_p->only_updated_pages == false || LSA_ISNULL (&session_p->dbfile.lsa)
	  || LSA_LT (&session_p->dbfile.lsa, &node_p->area->iopage.prv.lsa))
	{
	  /* Backup the content of this page along with its page identifier add alloced node to the queue */
	  (void) fileio_append_queue (queue_p, node_p);
	}
      else
	{
	  /* free node */
	  (void) fileio_free_node (queue_p, node_p);
	  node_p = NULL;
	}

#if defined(CUBRID_DEBUG)
      fprintf (stdout, "read_thread from_npages = %d, pageid = %d\n", thread_info_p->from_npages,
	       thread_info_p->pageid);
#endif /* CUBRID_DEBUG */
      thread_info_p->pageid++;
      pthread_mutex_unlock (&thread_info_p->mtx);
      if (node_p)
	{
	  node_p->nread += FILEIO_BACKUP_PAGE_OVERHEAD;
	  FILEIO_SET_BACKUP_PAGE_ID_COPY (node_p->area, node_p->pageid, backup_header_p->bkpagesize);

#if defined(CUBRID_DEBUG)
	  fprintf (stdout, "fileio_read_backup_volume: %d\t%d,\t%d\n",
		   ((FILEIO_BACKUP_PAGE *) (node_p->area))->iopageid,
		   *(PAGEID *) (((char *) (node_p->area)) + offsetof (FILEIO_BACKUP_PAGE, iopage) +
				backup_header_p->bkpagesize), backup_header_p->bkpagesize);
#endif

	  if (backup_header_p->zip_method != FILEIO_ZIP_NONE_METHOD
	      && fileio_compress_backup_node (node_p, backup_header_p) != NO_ERROR)
	    {
	      thread_info_p->io_type = FILEIO_ERROR_INTERRUPT;
	      need_unlock = false;
	      node_p = NULL;
	      goto exit_on_error;
	    }
	}
    }

exit_on_end:

#if defined(CUBRID_DEBUG)
  fprintf (stdout, "end io_backup_volume_read\n");
#endif /* CUBRID_DEBUG */
  return;
exit_on_error:

  /* set error info */
  if (thread_info_p->errid == NO_ERROR)
    {
      assert (er_errid () != NO_ERROR);
      thread_info_p->errid = er_errid ();
    }

  thread_info_p->end_r_threads++;
  if (thread_info_p->end_r_threads >= thread_info_p->act_r_threads)
    {
      pthread_cond_signal (&thread_info_p->wcv);	/* wake up write thread */
    }

  if (need_unlock)
    {
      pthread_mutex_unlock (&thread_info_p->mtx);
    }

  if (node_p != NULL)
    {
      (void) fileio_free_node (queue_p, node_p);
    }

  goto exit_on_end;
}

/*
 * fileio_write_backup_volume () -
 *   return:
 *   session(in/out):
 */
static FILEIO_TYPE
fileio_write_backup_volume (THREAD_ENTRY * thread_p, FILEIO_BACKUP_SESSION * session_p)
{
  FILEIO_THREAD_INFO *thread_info_p;
  FILEIO_QUEUE *queue_p;
  FILEIO_NODE *node_p;
  int rv;
  bool need_unlock = false;
  FILEIO_BACKUP_HEADER *backup_header_p;
  FILEIO_BACKUP_PAGE *save_area_p;

  if (!session_p)
    {
      return FILEIO_WRITE;
    }

  thread_info_p = &session_p->read_thread_info;
  queue_p = &thread_info_p->io_queue;
#if defined(CUBRID_DEBUG)
  fprintf (stdout, "start io_backup_volume_write\n");
#endif /* CUBRID_DEBUG */
  backup_header_p = session_p->bkup.bkuphdr;
  rv = pthread_mutex_lock (&thread_info_p->mtx);
  while (1)
    {
      while (thread_info_p->io_type == FILEIO_READ)
	{
	  pthread_cond_wait (&thread_info_p->wcv, &thread_info_p->mtx);
	}

      if (thread_info_p->io_type == FILEIO_ERROR_INTERRUPT)
	{
	  need_unlock = true;
	  goto exit_on_error;
	}

      /* do write */
      while (queue_p->head && queue_p->head->writeable == true)
	{
	  /* delete the head node of the queue */
	  node_p = fileio_delete_queue_head (queue_p);
	  if (node_p == NULL)
	    {
	      thread_info_p->io_type = FILEIO_ERROR_INTERRUPT;
	    }
	  else
	    {
	      save_area_p = session_p->dbfile.area;	/* save link */
	      rv = fileio_write_backup_node (thread_p, session_p, node_p, backup_header_p);
	      if (rv != NO_ERROR)
		{
		  thread_info_p->io_type = FILEIO_ERROR_INTERRUPT;
		}
	      session_p->dbfile.area = save_area_p;	/* restore link */
#if defined(CUBRID_DEBUG)
	      fprintf (stdout, "write_thread node->pageid = %d, node->nread = %d\n", node_p->pageid, node_p->nread);
#endif /* CUBRID_DEBUG */
	      if (session_p->verbose_fp && thread_info_p->from_npages >= 25
		  && node_p->pageid >= thread_info_p->check_npages)
		{
		  fprintf (session_p->verbose_fp, "#");
		  thread_info_p->check_ratio++;
		  thread_info_p->check_npages =
		    (int) (((float) thread_info_p->from_npages / 25.0) * thread_info_p->check_ratio);
		}

	      /* free node */
	      (void) fileio_free_node (queue_p, node_p);
	    }

	  if (thread_info_p->io_type == FILEIO_ERROR_INTERRUPT)
	    {
	      need_unlock = true;
	      goto exit_on_error;
	    }
	}

      thread_info_p->io_type = FILEIO_READ;	/* reset */
      /* check EOF */
      if (thread_info_p->end_r_threads >= thread_info_p->act_r_threads)
	{
	  /* only write thread alive */
	  pthread_mutex_unlock (&thread_info_p->mtx);
	  break;
	}

      pthread_cond_broadcast (&thread_info_p->rcv);	/* wake up all read threads */
    }

#if defined(CUBRID_DEBUG)
  fprintf (stdout, "end io_backup_volume_write\n");
#endif /* CUBRID_DEBUG */
exit_on_end:

  return thread_info_p->io_type;
exit_on_error:

  /* set error info */
  if (er_errid () == NO_ERROR)
    {
      switch (thread_info_p->errid)
	{
	case ER_INTERRUPTED:
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_INTERRUPTED, 0);
	  break;
	default:		/* give up to handle this case */
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_DBBACKUP_FAIL, 1,
		  fileio_get_base_file_name (backup_header_p->db_fullname));
	  break;
	}
    }

  if (thread_info_p->end_r_threads >= thread_info_p->act_r_threads)
    {
      /* only write thread alive an error (i.e, INTERRUPT) was broken out, so terminate the all threads. But, I am the
       * last one, and all the readers are terminated */
      pthread_mutex_unlock (&thread_info_p->mtx);
      goto exit_on_end;
    }

  /* wake up all read threads and wait for all killed */
  pthread_cond_broadcast (&thread_info_p->rcv);
  pthread_cond_wait (&thread_info_p->wcv, &thread_info_p->mtx);
  pthread_mutex_unlock (&thread_info_p->mtx);
  goto exit_on_end;
}

// *INDENT-OFF*
static void
fileio_read_backup_volume_execute (cubthread::entry &thread_ref, FILEIO_BACKUP_SESSION *back_session)
{
  fileio_read_backup_volume (&thread_ref, back_session);
}
// *INDENT-ON*

static int
fileio_start_backup_thread (THREAD_ENTRY * thread_p, FILEIO_BACKUP_SESSION * session_p,
			    FILEIO_THREAD_INFO * thread_info_p, int from_npages, bool is_only_updated_pages,
			    int check_ratio, int check_npages, FILEIO_QUEUE * queue_p)
{
  CSS_CONN_ENTRY *conn_p;
  int i;

  /* Initialize global MT variables */
  thread_info_p->end_r_threads = 0;
  thread_info_p->pageid = 0;
  thread_info_p->from_npages = from_npages;
  thread_info_p->io_type = FILEIO_READ;
  thread_info_p->errid = NO_ERROR;
  thread_info_p->only_updated_pages = is_only_updated_pages;
  thread_info_p->check_ratio = check_ratio;
  thread_info_p->check_npages = check_npages;
  thread_info_p->tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  /* start read threads */
  conn_p = css_get_current_conn_entry ();
  for (i = 1; i <= thread_info_p->act_r_threads; i++)
    {
      // *INDENT-OFF
      auto exec_f = std::bind (fileio_read_backup_volume_execute, std::placeholders::_1, session_p);
      logpb_push_backup_read_task (new cubthread::entry_callable_task (exec_f));
      // *INDENT-ON
    }

  /* work as write thread */
  (void) fileio_write_backup_volume (thread_p, session_p);
  /* at here, finished all read threads check error, interrupt */
  if (thread_info_p->io_type == FILEIO_ERROR_INTERRUPT || queue_p->size != 0)
    {
      return ER_FAILED;
    }

  return NO_ERROR;
}
#endif /* SERVER_MODE */

/*
 * fileio_backup_volume () - Include the given database volume/file as part of
 *                       the backup
 *   return:
 *   session(in/out): The session array
 *   from_vlabel(in): Name of the database volume/file to include
 *   from_volid(in): Identifier of the database volume/file to include
 *   last_page(in): stop backing up this volume after this page
 *   only_updated_pages(in): If we are backing up the database with a specific
 *                           backup level. We may opt to backup only the
 *                           updated pages since previous backup.
 *
 * Note: Information about the database volume/file is recorded, so it
 *       can be recreated (e.g., name and space).
 *       If this is an incremental backup, only pages that have been
 *       updated since the previous backup are backed up, unless a
 *       specific request is given to backup all pages.
 *       Last_page can shorten the number of pages saved (i.e. for
 *       temp volumes, we do not need to backup the entire volume).
 *
 *       1)   The pages are backed up as they are currently stored on disk,
 *            that is, we do not use the page buffer pool for this operation
 *            since we do not want to disturbe the normal access patern of
 *            clients in the page buffer pool.
 *       2)   We open the file/volume instead of using the actual vdes, so
 *            that we avoid a bunch of lseeks.
 */
int
fileio_backup_volume (THREAD_ENTRY * thread_p, FILEIO_BACKUP_SESSION * session_p, const char *from_vol_label_p,
		      VOLID from_vol_id, PAGEID last_page, bool is_only_updated_pages)
{
  struct stat from_stbuf;
  int from_npages, npages;
  int page_id;
  int nread;
  FILEIO_BACKUP_FILE_HEADER *file_header_p;
  int check_ratio = 0;
  int check_npages = 0;
  FILEIO_THREAD_INFO *thread_info_p;
  FILEIO_QUEUE *queue_p = NULL;
  FILEIO_BACKUP_PAGE *save_area_p;
  FILEIO_NODE *node_p = NULL;
  FILEIO_BACKUP_HEADER *backup_header_p;
  int rv;
  bool is_need_vol_closed;

#if (defined(WINDOWS) || !defined(SERVER_MODE))
  off_t saved_act_log_fp = (off_t) - 1;
#endif /* WINDOWS || !SERVER_MODE */

  /*
   * Backup the pages as they are stored on disk (i.e., don't care if they
   * are stored on the page buffer pool any longer). We do not use the page
   * buffer pool since we do not want to remove important pages that are
   * used by clients.
   * We also open the file/volume instead of using the one currently
   * available since we do not want to be doing a lot of seeks.
   * Remember that we can be preempted.
   */
  session_p->dbfile.vlabel = from_vol_label_p;
  session_p->dbfile.volid = from_vol_id;
  session_p->dbfile.vdes = NULL_VOLDES;
  is_need_vol_closed = false;
  if (from_vol_id == LOG_DBLOG_ACTIVE_VOLID)
    {
      session_p->dbfile.vdes = fileio_get_volume_descriptor (LOG_DBLOG_ACTIVE_VOLID);
#if (defined(WINDOWS) || !defined(SERVER_MODE))
      if (session_p->dbfile.vdes != NULL_VOLDES)
	{
	  /* save current file pointer */
	  saved_act_log_fp = lseek (session_p->dbfile.vdes, (off_t) 0, SEEK_CUR);
	  /* reset file pointer */
	  if (saved_act_log_fp == (off_t) - 1)
	    {
	      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IO_READ, 1, session_p->dbfile.vlabel);
	      goto error;
	    }

	  if (lseek (session_p->dbfile.vdes, (off_t) 0, SEEK_SET) == (off_t) - 1)
	    {
	      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IO_READ, 1, session_p->dbfile.vlabel);
	      goto error;
	    }
	}
#endif /* WINDOWS || !SERVER_MODE */
    }

  if (session_p->dbfile.vdes == NULL_VOLDES)
    {
      session_p->dbfile.vdes = fileio_open (session_p->dbfile.vlabel, O_RDONLY, 0);
      if (session_p->dbfile.vdes == NULL_VOLDES)
	{
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IO_MOUNT_FAIL, 1, session_p->dbfile.vlabel);
	  goto error;
	}
      is_need_vol_closed = true;
    }

  if (fstat (session_p->dbfile.vdes, &from_stbuf) == -1)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IO_MOUNT_FAIL, 1, session_p->dbfile.vlabel);
      goto error;
    }

  if (S_ISREG (from_stbuf.st_mode))
    {
      /* regular file */
      session_p->dbfile.nbytes = from_stbuf.st_size;
    }
  else
    {
      /* raw device??? */
      session_p->dbfile.nbytes = (INT64) xdisk_get_total_numpages (thread_p, from_vol_id) * (INT64) IO_PAGESIZE;
    }

  /* print the number divided by volume pagesize */
  npages = (int) CEIL_PTVDIV (session_p->dbfile.nbytes, IO_PAGESIZE);
  backup_header_p = session_p->bkup.bkuphdr;
  if (session_p->verbose_fp)
    {
      fprintf (session_p->verbose_fp, " %-28s | %10d | ", fileio_get_base_file_name (from_vol_label_p), npages);
    }

  /* set the number divied by backup pagesize */
  if (last_page >= 0 && last_page < npages)
    {
      from_npages = CEIL_PTVDIV ((last_page + 1) * IO_PAGESIZE, backup_header_p->bkpagesize);
    }
  else
    {
      from_npages = (int) CEIL_PTVDIV (session_p->dbfile.nbytes, backup_header_p->bkpagesize);
    }

  /* Write a backup file header which identifies this volume/file on the backup.  File headers do not use the extra
   * pageid_copy field. */
  session_p->dbfile.area->iopageid = FILEIO_BACKUP_FILE_START_PAGE_ID;
  file_header_p = (FILEIO_BACKUP_FILE_HEADER *) (&session_p->dbfile.area->iopage);
  file_header_p->volid = session_p->dbfile.volid;
  file_header_p->nbytes = session_p->dbfile.nbytes;
  strncpy (file_header_p->vlabel, session_p->dbfile.vlabel, PATH_MAX);
  nread = FILEIO_BACKUP_FILE_HEADER_PAGE_SIZE;
  if (fileio_write_backup (thread_p, session_p, nread) != NO_ERROR)
    {
      goto error;
    }


#if defined(CUBRID_DEBUG)
  /* How about adding a backup verbose option ... to print this sort of information as the backup is progressing? A DBA
   * could later compare the information thus gathered with a restore -t option to verify the integrity of the archive. */
  if (io_Bkuptrace_debug > 0)
    {
      fprintf (stdout, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_IO, MSGCAT_FILEIO_BKUP_FILE),
	       file_header_p->vlabel, file_header_p->volid, file_header_p->nbytes, CEIL_PTVDIV (file_header_p->nbytes,
												IO_PAGESIZE));
      fprintf (stdout, "\n");
    }
#endif /* CUBRID_DEBUG */

  /* Now start reading each page and writing each page to the backup. */
  if (session_p->verbose_fp)
    {
      check_ratio = 1;
      check_npages = (int) (((float) from_npages / 25.0) * check_ratio);
    }

  thread_info_p = &session_p->read_thread_info;
  queue_p = &thread_info_p->io_queue;
  /* set the number of activated read threads */
  thread_info_p->act_r_threads = MAX (thread_info_p->num_threads - 1, 0);
  thread_info_p->act_r_threads = MIN (thread_info_p->act_r_threads, from_npages);
  if (thread_info_p->act_r_threads > 0)
    {
#if defined(SERVER_MODE)
      if (fileio_start_backup_thread (thread_p, session_p, thread_info_p, from_npages, is_only_updated_pages,
				      check_ratio, check_npages, queue_p) != NO_ERROR)
	{
	  goto error;
	}

      if (session_p->verbose_fp)
	{
	  check_ratio = thread_info_p->check_ratio;
	}
#endif /* SERVER_MODE */
    }
  else
    {
      for (page_id = 0; page_id < from_npages; page_id++)
	{
	  /* Have to allow other threads to run and check for interrupts from the user (i.e. Ctrl-C ). check for
	   * standalone-mode too. */
	  if ((page_id % FILEIO_CHECK_FOR_INTERRUPT_INTERVAL) == 0
	      && pgbuf_is_log_check_for_interrupts (thread_p) == true)
	    {
	      goto error;
	    }

	  /* alloc queue node */
	  node_p = fileio_allocate_node (queue_p, backup_header_p);
	  if (node_p == NULL)
	    {
	      goto error;
	    }

	  /* read one page sequentially */
	  save_area_p = session_p->dbfile.area;	/* save link */
	  session_p->dbfile.area = node_p->area;
	  node_p->pageid = page_id;
	  node_p->nread = fileio_read_backup (thread_p, session_p, node_p->pageid);
	  session_p->dbfile.area = save_area_p;	/* restore link */
	  if (node_p->nread == -1)
	    {
	      goto error;
	    }
	  else if (node_p->nread == 0)
	    {
	      /* This could be an error since we estimated more pages. End of file/volume. */
	      (void) fileio_free_node (queue_p, node_p);
	      node_p = NULL;
	      break;
	    }

	  /*
	   * Do we need to backup this page ?
	   * In other words, has it been changed since either the previous backup
	   * of this level or a lower level.
	   */

	  if (is_only_updated_pages == false || LSA_ISNULL (&session_p->dbfile.lsa)
	      || LSA_LT (&session_p->dbfile.lsa, &node_p->area->iopage.prv.lsa))
	    {
	      /* Backup the content of this page along with its page identifier */

	      node_p->nread += FILEIO_BACKUP_PAGE_OVERHEAD;
	      FILEIO_SET_BACKUP_PAGE_ID_COPY (node_p->area, node_p->pageid, backup_header_p->bkpagesize);

#if defined(CUBRID_DEBUG)
	      fprintf (stdout, "fileio_backup_volume: %d\t%d,\t%d\n", ((FILEIO_BACKUP_PAGE *) (node_p->area))->iopageid,
		       *(PAGEID *) (((char *) (node_p->area)) + offsetof (FILEIO_BACKUP_PAGE, iopage) +
				    backup_header_p->bkpagesize), backup_header_p->bkpagesize);
#endif

	      if (backup_header_p->zip_method != FILEIO_ZIP_NONE_METHOD
		  && fileio_compress_backup_node (node_p, backup_header_p) != NO_ERROR)
		{
		  goto error;
		}

	      save_area_p = session_p->dbfile.area;	/* save link */
	      rv = fileio_write_backup_node (thread_p, session_p, node_p, backup_header_p);
	      session_p->dbfile.area = save_area_p;	/* restore link */
	      if (rv != NO_ERROR)
		{
		  goto error;
		}
	    }

	  if (session_p->verbose_fp && from_npages >= 25 && page_id >= check_npages)
	    {
	      fprintf (session_p->verbose_fp, "#");
	      check_ratio++;
	      check_npages = (int) (((float) from_npages / 25.0) * check_ratio);
	    }

	  /* free node */
	  (void) fileio_free_node (queue_p, node_p);
	  node_p = NULL;
	}
    }

  /* End of FILE */

  /* alloc queue node */
  node_p = fileio_allocate_node (queue_p, backup_header_p);
  if (node_p == NULL)
    {
      goto error;
    }

  node_p->nread = FILEIO_DBVOLS_IO_PAGE_SIZE (backup_header_p);
  memset (&node_p->area->iopage, '\0', backup_header_p->bkpagesize);
  FILEIO_SET_BACKUP_PAGE_ID (node_p->area, FILEIO_BACKUP_FILE_END_PAGE_ID, backup_header_p->bkpagesize);

#if defined(CUBRID_DEBUG)
  fprintf (stdout, "io_backup_volume: %d\t%d,\t%d\n", ((FILEIO_BACKUP_PAGE *) (node_p->area))->iopageid,
	   *(PAGEID *) (((char *) (node_p->area)) + offsetof (FILEIO_BACKUP_PAGE, iopage) +
			backup_header_p->bkpagesize), backup_header_p->bkpagesize);
#endif

  if (backup_header_p->zip_method != FILEIO_ZIP_NONE_METHOD
      && fileio_compress_backup_node (node_p, backup_header_p) != NO_ERROR)
    {
      goto error;
    }

  save_area_p = session_p->dbfile.area;	/* save link */
  rv = fileio_write_backup_node (thread_p, session_p, node_p, backup_header_p);
  session_p->dbfile.area = save_area_p;	/* restore link */
  if (rv != NO_ERROR)
    {
      goto error;
    }

  /* free node */
  (void) fileio_free_node (queue_p, node_p);
  node_p = NULL;
#if defined(CUBRID_DEBUG)
  fprintf (stdout, "volume EOF : bkpagesize = %d, voltotalio = %ld\n", backup_header_p->bkpagesize,
	   session_p->bkup.voltotalio);
#endif /* CUBRID_DEBUG */
  /* Close the database volume/file */
  if (is_need_vol_closed == true)
    {
      fileio_close (session_p->dbfile.vdes);
    }
#if (defined(WINDOWS) || !defined(SERVER_MODE))
  else
    {
      if (from_vol_id == LOG_DBLOG_ACTIVE_VOLID && session_p->dbfile.vdes != NULL_VOLDES && saved_act_log_fp >= 0)
	{
	  /* restore file pointer */
	  lseek (session_p->dbfile.vdes, saved_act_log_fp, SEEK_SET);
	}
    }
#endif /* WINDOWS || !SERVER_MODE */

  session_p->dbfile.vdes = NULL_VOLDES;
  session_p->dbfile.volid = NULL_VOLID;
  session_p->dbfile.nbytes = -1;
  session_p->dbfile.vlabel = NULL;
  if (session_p->verbose_fp)
    {
      if (from_npages < 25)
	{
	  fprintf (session_p->verbose_fp, "######################### | done\n");
	}
      else
	{
	  while (check_ratio <= 25)
	    {
	      fprintf (session_p->verbose_fp, "#");
	      check_ratio++;
	    }
	  fprintf (session_p->verbose_fp, " | done\n");
	}
    }

  return NO_ERROR;

error:
  if (is_need_vol_closed == true)
    {
      fileio_close (session_p->dbfile.vdes);
    }
#if (defined(WINDOWS) || !defined(SERVER_MODE))
  else
    {
      if (from_vol_id == LOG_DBLOG_ACTIVE_VOLID && session_p->dbfile.vdes != NULL_VOLDES && saved_act_log_fp >= 0)
	{
	  /* restore file pointer */
	  lseek (session_p->dbfile.vdes, saved_act_log_fp, SEEK_SET);
	}
    }
#endif /* WINDOWS || !SERVER_MODE */

  if (node_p != NULL)
    {
      (void) fileio_free_node (queue_p, node_p);
    }

  session_p->dbfile.vdes = NULL_VOLDES;
  session_p->dbfile.volid = NULL_VOLID;
  session_p->dbfile.nbytes = -1;
  session_p->dbfile.vlabel = NULL;
  return ER_FAILED;
}

/*
 * fileio_flush_backup () - Flush any buffered data
 *   return:
 *   session(in/out): The session array
 *
 * Note: When the output fills up, we prompt for another volume or more space.
 *       Incomplete blocks are repeated at the start of the following archive,
 *       in order to insure that we do not try to read from incomplete tape
 *       blocks.
 */
static int
fileio_flush_backup (THREAD_ENTRY * thread_p, FILEIO_BACKUP_SESSION * session_p)
{
  char *buffer_p;
  ssize_t nbytes;
  INT64 count;
  bool is_interactive_need_new = false;
  bool is_force_new_bkvol = false;

  if (prm_get_bigint_value (PRM_ID_IO_BACKUP_MAX_VOLUME_SIZE) > 0
      && (UINT64) session_p->bkup.count > prm_get_bigint_value (PRM_ID_IO_BACKUP_MAX_VOLUME_SIZE))
    {
      er_log_debug (ARG_FILE_LINE, "Backup_flush: Backup aborted because count %d larger than max volume size %ld\n",
		    session_p->bkup.count, prm_get_bigint_value (PRM_ID_IO_BACKUP_MAX_VOLUME_SIZE));
      return ER_FAILED;
    }

#if defined(CUBRID_DEBUG)
  fprintf (stdout, "io_backup_flush: bkup.count = %ld, voltotalio = %ld\n", session_p->bkup.count,
	   session_p->bkup.voltotalio);
#endif /* CUBRID_DEBUG */
  /*
   * Flush any buffered bytes.
   * NOTE that we do not call fileio_write since it will try to do lseek and some
   *      backup devices do not seek.
   */
  if (session_p->bkup.count > 0)
    {
    restart_newvol:
      /*
       * Determine number of bytes we can safely write to this volume
       * being mindful of the max specified by the user.
       */
      is_interactive_need_new = false;
      is_force_new_bkvol = false;
      count = session_p->bkup.count;
      if (prm_get_bigint_value (PRM_ID_IO_BACKUP_MAX_VOLUME_SIZE) > 0)
	{
	  /* count was just loaded from session_p->bkup.count, which the enclosing "> 0" test keeps positive, so
	   * comparing it as unsigned is value preserving and keeps the existing remaining-space semantics. */
	  count =
	    (int) MIN ((UINT64) count,
		       prm_get_bigint_value (PRM_ID_IO_BACKUP_MAX_VOLUME_SIZE) - session_p->bkup.voltotalio);
	}
      buffer_p = session_p->bkup.buffer;
      do
	{
	  /* disk file size check */


	  if (session_p->bkup.voltotalio >= OFF_T_MAX && session_p->bkup.dtype == FILEIO_BACKUP_VOL_DIRECTORY)
	    {
	      /* New volume is needed */
	      is_force_new_bkvol = true;
	    }
	  else
	    {
	      /* Write the data */
	      nbytes = write (session_p->bkup.vdes, buffer_p, count);
	      if (nbytes <= 0)
		{
		  if (nbytes == 0)
		    {
		      is_interactive_need_new = true;	/* For raw partitions */
		    }
		  else
		    {
		      switch (errno)
			{
			  /* equiv to try again */
			case EINTR:
			case EAGAIN:
			  continue;
			  /* New volume is needed and no user interaction needed */
#if !defined(WINDOWS)
			case EDQUOT:
#endif /* !WINDOWS */
			case EFBIG:
			case EIO:
			case EINVAL:
			  is_force_new_bkvol = true;
			  break;
			  /* New volume is needed and requires user interaction */
			case ENXIO:
			case ENOSPC:
			case EPIPE:
			  is_interactive_need_new = true;
			  break;
			  /* equiv -- Failure */
			default:
			  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IO_WRITE, 2,
					       CEIL_PTVDIV (session_p->bkup.voltotalio, IO_PAGESIZE),
					       session_p->bkup.vlabel);
			  return ER_FAILED;
			}
		    }
		}
	      else
		{
		  session_p->bkup.voltotalio += nbytes;
		  count -= (int) nbytes;
		  buffer_p += nbytes;
		}
	    }

	  if (is_interactive_need_new || is_force_new_bkvol
	      || (prm_get_bigint_value (PRM_ID_IO_BACKUP_MAX_VOLUME_SIZE) > 0
		  && ((UINT64) session_p->bkup.voltotalio >= prm_get_bigint_value (PRM_ID_IO_BACKUP_MAX_VOLUME_SIZE))))
	    {
#if defined(CUBRID_DEBUG)
	      fprintf (stdout, "open a new backup volume\n");
#endif /* CUBRID_DEBUG */
	      /* Finish this volume, fixup session and open a new volume */
	      if ((fileio_get_next_backup_volume (thread_p, session_p, is_interactive_need_new) != NO_ERROR)
		  || (fileio_write_backup_header (session_p) != NO_ERROR))
		{
		  return ER_FAILED;
		}
	      else
		{
		  /* Because the buffer may have been incompletely written on a raw device (i.e. tape), we start a new
		   * volume by repeating the current buffer block in its entirety immediately after the header. Upon
		   * restore, the incomplete part will be ignored. */
		  session_p->bkup.ptr = session_p->bkup.buffer;
		  goto restart_newvol;
		}
	    }
	}
      while (count > 0);
      /* Update session to reflect that flush completed. */
      session_p->bkup.count = 0;	/* can only update after flush completes */
      session_p->bkup.ptr = session_p->bkup.buffer;
    }

  return NO_ERROR;
}

/*
 * fileio_read_backup () - Read a database page from the current database
 *                     volume/file that is backed up
 *   return:
 *   session(in/out): The session array
 *   pageid(in): The page from which we are reading
 *
 * Note: If we run into an end of file, we filled the page with nulls. This is
 *       needed since we write full pages to back up destination. Without this,
 *       we will not be able to know how much to read since not necessarily
 *       the whole volume/file is backed up.
 */
static ssize_t
fileio_read_backup (THREAD_ENTRY * thread_p, FILEIO_BACKUP_SESSION * session_p, int page_id)
{
  int io_page_size = session_p->bkup.bkuphdr->bkpagesize;
#if defined(WINDOWS)
  int nread, nbytes;
#else
  ssize_t nread, nbytes;
#endif
  char *buffer_p;

  /* Read until you acumulate io_pagesize or the EOF mark is reached. */
  nread = 0;
  FILEIO_SET_BACKUP_PAGE_ID (session_p->dbfile.area, page_id, io_page_size);

#if defined(CUBRID_DEBUG)
  fprintf (stdout, "fileio_read_backup: %d\t%d,\t%d\n", ((FILEIO_BACKUP_PAGE *) (session_p->dbfile.area))->iopageid,
	   *(PAGEID *) (((char *) (session_p->dbfile.area)) + offsetof (FILEIO_BACKUP_PAGE, iopage) + io_page_size),
	   io_page_size);
#endif

  buffer_p = (char *) &session_p->dbfile.area->iopage;
  while (nread < io_page_size)
    {
      /* Read the desired amount of bytes */
#if !defined(SERVER_MODE)
      nbytes = read (session_p->dbfile.vdes, buffer_p, io_page_size - nread);
#elif defined(WINDOWS)
      nbytes = read (session_p->dbfile.vdes, buffer_p, io_page_size - nread);
#else
      nbytes =
	pread (session_p->dbfile.vdes, buffer_p, io_page_size - nread,
	       FILEIO_GET_FILE_SIZE (io_page_size, page_id) + nread);
#endif
      if (nbytes == -1)
	{
	  if (errno != EINTR)
	    {
	      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IO_READ, 2,
				   FILEIO_GET_BACKUP_PAGE_ID (session_p->dbfile.area), session_p->dbfile.vlabel);
	      return -1;
	    }
	}
      else if (nbytes == 0)
	{
	  if (nread > 0)
	    {
#if defined(CUBRID_DEBUG)
	      fprintf (stdout, "io_backup_read: io_pagesize = %d, nread = %d, voltotalio = %d : ADD FILLER\n",
		       io_page_size, nread, session_p->bkup.voltotalio);
#endif /* CUBRID_DEBUG */
	      /*
	       * We have a file that it is not multiples of io_pagesize.
	       * We need to add a filler. otherwise, we will not be able to
	       * find other files.
	       */
	      memset (buffer_p, '\0', io_page_size - nread);
	      nread = io_page_size;
	    }
	  break;
	}
      nread += nbytes;
      buffer_p += nbytes;
    }

#if !defined (CS_MODE)
  if (session_p->dbfile.volid == LOG_DBLOG_ACTIVE_VOLID && page_id == 0)
    {
      /* The active log header names the archive volume cdc still needs. That belongs to this database and the
       * session attached to it, not to whatever is restored from this backup, so it is taken out of the copy.
       * The log on disk is not touched. Only a server or a stand-alone utility takes a backup, and only those
       * link the log module. */
      logpb_strip_cdc_arv_num_from_header_page (&session_p->dbfile.area->iopage);
    }
#endif /* !defined (CS_MODE) */

#if defined(SERVER_MODE)
  /* Backup Thread is reading data/log pages slowly to avoid IO burst */
  if (session_p->dbfile.volid == LOG_DBLOG_ACTIVE_VOLID || session_p->dbfile.volid == LOG_DBLOG_ARCHIVE_VOLID)
    {
      /* Log volumes are never paced. The archive arm also required LOG_CS_OWN_WRITE_MODE (); CBRD-27166 moved the
       * transfer out of that critical section, and pacing it there would only stretch the window where archives
       * cannot be removed. */
      ;				/* go ahead */
    }
  else
    {
      int sleep_msecs;

      if (session_p->sleep_msecs > 0)	/* priority 1 */
	{
	  sleep_msecs = session_p->sleep_msecs;
	}
      else if (prm_get_integer_value (PRM_ID_IO_BACKUP_SLEEP_MSECS) > 0)	/* priority 2 */
	{
	  sleep_msecs = prm_get_integer_value (PRM_ID_IO_BACKUP_SLEEP_MSECS);
	}
      else
	{
	  sleep_msecs = 0;
	}

      if (sleep_msecs > 0)
	{
	  sleep_msecs = (int) (((double) sleep_msecs) / (ONE_M / io_page_size));

	  if (sleep_msecs > 0)
	    {
	      thread_sleep (sleep_msecs);
	    }
	}
    }
#endif

  return nread;
}

/*
 * fileio_write_backup () - Write the number of indicated bytes from the dbfile
 *                      area to to the backup destination
 *   return:
 *   session(in/out): The session array
 *   towrite_nbytes(in): Number of bytes that must be written
 */
static int
fileio_write_backup (THREAD_ENTRY * thread_p, FILEIO_BACKUP_SESSION * session_p, ssize_t to_write_nbytes)
{
  char *buffer_p;
  ssize_t nbytes;

  buffer_p = (char *) session_p->dbfile.area;
  while (to_write_nbytes > 0)
    {
      /*
       * Buffer as much as you can, so that the device I/O will go through
       * without any problem. Remember some backup devices work only with
       * a fixed I/O length.  We cannot use io_backup_write because we may
       * have been called recursively from there after the old volume filled.
       */
      nbytes = CAST_BUFLEN (session_p->bkup.iosize - session_p->bkup.count);
      if (nbytes > to_write_nbytes)
	{
	  nbytes = to_write_nbytes;
	}

      memcpy (session_p->bkup.ptr, buffer_p, nbytes);
      session_p->bkup.count += nbytes;
      session_p->bkup.ptr += nbytes;
      buffer_p += nbytes;
      to_write_nbytes -= nbytes;
      if (session_p->bkup.count >= session_p->bkup.iosize)
	{
	  if (fileio_flush_backup (thread_p, session_p) != NO_ERROR)
	    {
	      return ER_FAILED;
	    }
	}
    }

  return NO_ERROR;
}

/*
 * fileio_write_backup_header () - Immediately write the backup header to the
 *                             destination
 *   return:
 *   session(in/out): The session array
 *
 * Note: Note that unlike io_backup_write, we do not buffer, instead we
 *       write directly to the output destination the number of bytes
 *       in a bkuphdr.  This insures that headers all have the same
 *       physical block size so we can read them properly.  The main
 *       purpose of this routine is to write the headers in a tape
 *       friendly blocking factor such that we can be sure we can read
 *       them back in without knowing how the tape was written in the
 *       first place.
 */
static int
fileio_write_backup_header (FILEIO_BACKUP_SESSION * session_p)
{
  char *buffer_p;
  int count, nbytes;

  /* Write immediately to the backup.  We do not use fileio_write for the same reason io_backup_flush does not. */
  count = FILEIO_BACKUP_HEADER_IO_SIZE;
  buffer_p = (char *) session_p->bkup.bkuphdr;
  do
    {
      nbytes = write (session_p->bkup.vdes, buffer_p, count);
      if (nbytes == -1)
	{
	  if (errno == EINTR || errno == EAGAIN)
	    {
	      continue;
	    }

	  if (errno == ENOSPC)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IO_WRITE_OUT_OF_SPACE, 2,
		      CEIL_PTVDIV (session_p->bkup.voltotalio, IO_PAGESIZE),
		      fileio_get_volume_label_by_fd (session_p->bkup.vdes, PEEK));
	    }
	  else
	    {
	      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IO_WRITE, 2,
				   CEIL_PTVDIV (session_p->bkup.voltotalio, IO_PAGESIZE), session_p->bkup.vlabel);
	    }
	  return ER_FAILED;
	}
      else
	{
	  count -= nbytes;
	  buffer_p += nbytes;
	}
    }
  while (count > 0);
  session_p->bkup.voltotalio += FILEIO_BACKUP_HEADER_IO_SIZE;
  return NO_ERROR;
}

/*
 * fileio_initialize_restore () - Initialize the restore session structure with the given information
 *   return: session or NULL
 *   db_fullname(in): Name of the database to backup
 *   backup_src(in): Name of backup device (file or directory)
 *   session(in/out): The session array
 *   level(in): The presumed backup level
 *   restore_verbose_file_path(in):
 *   newvolpath(in): restore the database and log volumes to the path
 *                   specified in the database-loc-file
 *
 * Note: Note that the user may choose a new location for the volume, so the
 *       contents of the backup source path may be set as a side effect.
 */
static FILEIO_BACKUP_SESSION *
fileio_initialize_restore (THREAD_ENTRY * thread_p, const char *db_full_name_p, char *backup_source_p,
			   FILEIO_BACKUP_SESSION * session_p, FILEIO_BACKUP_LEVEL level,
			   const char *restore_verbose_file_path, bool is_new_vol_path)
{
  char orig_name[PATH_MAX];

  strcpy (orig_name, backup_source_p);
  /* First, make sure the volume given exists and we can access it. */
  while (!fileio_is_volume_exist (backup_source_p))
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IO_MOUNT_FAIL, 1, backup_source_p);
      fprintf (stdout, "%s\n", er_msg ());
      /* Let user see original prompt name until good one is chosen */
      strcpy (backup_source_p, orig_name);
      if (fileio_find_restore_volume (thread_p, db_full_name_p, backup_source_p, FILEIO_INITIAL_BACKUP_UNITS, level,
				      MSGCAT_FILEIO_RESTORE_FIND_REASON) == FILEIO_RELOCATION_QUIT)
	{
	  return NULL;
	}
    }

  /* Backup session initialization is the same as restore for now, as long as the first backup volume already exists,
   * which we just checked. */
  session_p->type = FILEIO_BACKUP_READ;	/* access backup device for read */
  /* save database full-pathname specified in the database-loc-file */
  strncpy_bufsize (session_p->bkup.loc_db_fullname, is_new_vol_path ? db_full_name_p : "");
  return (fileio_initialize_backup (db_full_name_p, (const char *) backup_source_p, session_p, level,
				    restore_verbose_file_path, 0 /* no multi-thread */ , 0 /* no sleep */ ));
}

/*
 * fileio_abort_restore () - The restore session is aborted
 *   return: void
 *   session(in/out): The session array
 */
void
fileio_abort_restore (THREAD_ENTRY * thread_p, FILEIO_BACKUP_SESSION * session_p)
{
  fileio_abort_backup (thread_p, session_p, false);
}

/*
 * fileio_read_restore () - The number of bytes to read from the backup destination
 *   return:
 *   session(in/out): The session array
 *   toread_nbytes(in): Number of bytes to read
 *
 * Note: Now handles reads which span volumes, as well as reading incomplete
 *       blocks at the end of one volume.  See fileio_flush_backup for details
 *       about how the final block is repeated at the start of the new volumes.
 */
static int
fileio_read_restore (THREAD_ENTRY * thread_p, FILEIO_BACKUP_SESSION * session_p, int to_read_nbytes)
{
#if defined(WINDOWS)
  int nbytes;
#else
  ssize_t nbytes;
#endif
  char *buffer_p;
  const char *next_vol_p;
  bool is_end_of_backup = false;
  bool is_need_next_vol = false;

  /* Read until you acumulate the desired number of bytes (a database page) or the EOF mark is reached. */
  buffer_p = (char *) session_p->dbfile.area;
  while (to_read_nbytes > 0 && is_end_of_backup == false)
    {
      if (session_p->bkup.count <= 0)
	{
	  /*
	   * Read and buffer another backup page from the backup volume.
	   * Note that a backup page is not necessarily the same size as the
	   * database page.
	   */
	restart_newvol:
	  is_need_next_vol = false;
	  session_p->bkup.ptr = session_p->bkup.buffer;
	  session_p->bkup.count = session_p->bkup.iosize;
	  while (session_p->bkup.count > 0)
	    {
	      /* Read a backup I/O page. */
	      nbytes = read (session_p->bkup.vdes, session_p->bkup.ptr, (int) session_p->bkup.count);
	      if (nbytes <= 0)
		{
		  /* An error or EOF was found */
		  if (nbytes == 0)
		    {
		      /* Perhaps the last page read in was the very last one */
		      if (FILEIO_GET_BACKUP_PAGE_ID (session_p->dbfile.area) == FILEIO_BACKUP_END_PAGE_ID)
			{
			  is_end_of_backup = true;
			  break;
			}
		      else
			{
			  is_need_next_vol = true;
			}
		    }
		  else
		    {
		      switch (errno)
			{
			case EINTR:
			case EAGAIN:
			  continue;
			case EINVAL:
			case ENXIO:
#if !defined(WINDOWS)
			case EOVERFLOW:
#endif /* !WINDOWS */
			  is_need_next_vol = true;
			  break;
			default:
			  {

			    er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IO_READ, 2,
						 CEIL_PTVDIV (session_p->bkup.voltotalio, IO_PAGESIZE),
						 session_p->bkup.vlabel);
			    return ER_FAILED;
			  }
			}
		    }

		  if (is_need_next_vol)
		    {
		      next_vol_p =
			fileio_get_backup_info_volume_name (session_p->bkup.bkuphdr->level,
							    session_p->bkup.bkuphdr->unit_num + 1,
							    FILEIO_FIRST_BACKUP_VOL_INFO);
		      if (next_vol_p == NULL)
			{
			  if (session_p->bkup.dtype != FILEIO_BACKUP_VOL_DEVICE)
			    {
			      fileio_get_directory_path (session_p->bkup.current_path, session_p->bkup.vlabel);
			      next_vol_p = session_p->bkup.current_path;
			    }
			}
		      else
			{
			  if (!fileio_is_volume_exist (next_vol_p)
			      && strncmp (next_vol_p, session_p->bkup.current_path,
					  strlen (session_p->bkup.current_path)) != 0)
			    {
			      /* assume the changed path, not bkvinf file */
			      next_vol_p = session_p->bkup.current_path;
			    }
			}

		      /* Unmount current backup volume */
		      fileio_dismount_without_fsync (thread_p, session_p->bkup.vdes);
		      session_p->bkup.vdes = NULL_VOLDES;
		      /* Find and mount the next volume and continue. */
		      if (session_p->bkup.dtype == FILEIO_BACKUP_VOL_DEVICE || next_vol_p == NULL)
			{
			  /* Probably a tape device, let user mount new one */
			  if (next_vol_p != NULL)
			    {
			      if (snprintf (session_p->bkup.name, PATH_MAX, "%s", next_vol_p) >= PATH_MAX)
				{
				  return ER_FAILED;
				}
			    }
			  if (fileio_find_restore_volume (thread_p, session_p->bkup.bkuphdr->db_fullname,
							  session_p->bkup.name, session_p->bkup.bkuphdr->unit_num + 1,
							  session_p->bkup.bkuphdr->level,
							  MSGCAT_FILEIO_RESTORE_FIND_REASON) == FILEIO_RELOCATION_QUIT)
			    {
			      return ER_FAILED;
			    }
			}
		      else
			{
			  if (snprintf (session_p->bkup.name, PATH_MAX, "%s", next_vol_p) >= PATH_MAX)
			    {
			      return ER_FAILED;
			    }
			}

		      /* Reset session count, etc */
		      session_p->bkup.alltotalio += session_p->bkup.voltotalio;
		      session_p->bkup.voltotalio = 0;
		      session_p->bkup.count = 0;
		      /* Bump the unit number to find the next volume */
		      session_p->bkup.bkuphdr->unit_num++;
		      /* Open the next volume */
		      if (fileio_continue_restore (thread_p, session_p->bkup.bkuphdr->db_fullname,
						   session_p->bkup.bkuphdr->db_creation, session_p, false, true,
						   session_p->bkup.bkuphdr->start_time) == NULL)
			{
			  return ER_FAILED;
			}
		      /* reset ptr */
		      session_p->bkup.ptr = session_p->bkup.buffer;
		      /* add new backup volume info info new_bkvinf */
		      fileio_add_volume_to_backup_info (session_p->bkup.vlabel, session_p->bkup.bkuphdr->level,
							session_p->bkup.bkuphdr->unit_num,
							FILEIO_SECOND_BACKUP_VOL_INFO);
		      /* Retry the buffered read from the new volume */
		      goto restart_newvol;
		    }
		}
	      else
		{
		  /* Increase the amount of read bytes */
		  session_p->bkup.ptr += nbytes;
		  session_p->bkup.count -= nbytes;
		  session_p->bkup.voltotalio += nbytes;
		}
	    }

	  /* Increase the buffered information */
	  session_p->bkup.ptr = session_p->bkup.buffer;
	  session_p->bkup.count = session_p->bkup.iosize - session_p->bkup.count;
	}

      /* Now copy the desired bytes */
      nbytes = session_p->bkup.count;
      if (nbytes > to_read_nbytes)
	{
	  nbytes = to_read_nbytes;
	}
      memcpy (buffer_p, session_p->bkup.ptr, nbytes);
      session_p->bkup.count -= nbytes;
      to_read_nbytes -= nbytes;
      session_p->bkup.ptr += nbytes;
      buffer_p += nbytes;
    }

  if (to_read_nbytes > 0 && !is_end_of_backup)
    {
      return ER_FAILED;
    }
  else
    {
      return NO_ERROR;
    }
}

/*
 * fileio_read_restore_header () - READ A BACKUP VOLUME HEADER
 *   return:
 *   session(in/out): The session array
 *
 * Note: This routine should be the first read from a backup volume or device.
 *       It reads the backup volume header that was written with the
 *       fileio_write_backup_header routine.  The header was written with a
 *       specific buffer block size to be more compatible with tape devices so
 *       we can read it in without knowing how the rest of the data was
 *       buffered. Note this also means that backup volume headers are not the
 *       same size as FILEIO_BACKUP_PAGE anymore.
 */
static int
fileio_read_restore_header (FILEIO_BACKUP_SESSION * session_p)
{
  int to_read_nbytes;
  int nbytes;
  char *buffer_p;
  FILEIO_BACKUP_HEADER *backup_header_p;

  backup_header_p = session_p->bkup.bkuphdr;
  to_read_nbytes = FILEIO_BACKUP_HEADER_IO_SIZE;
  buffer_p = (char *) backup_header_p;
  while (to_read_nbytes > 0)
    {
      nbytes = read (session_p->bkup.vdes, buffer_p, to_read_nbytes);
      if (nbytes == -1)
	{
	  if (errno == EINTR)
	    {
	      continue;
	    }
	  else
	    {
	      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IO_READ, 2,
				   CEIL_PTVDIV (session_p->bkup.voltotalio, IO_PAGESIZE), session_p->bkup.vlabel);
	      return ER_FAILED;
	    }
	}
      else if (nbytes == 0)
	{
	  /* EOF should not happen when reading the header. */
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IO_READ, 2,
			       CEIL_PTVDIV (session_p->bkup.voltotalio, IO_PAGESIZE), session_p->bkup.vlabel);
	  return ER_FAILED;
	}
      to_read_nbytes -= nbytes;
      session_p->bkup.voltotalio += nbytes;
      buffer_p += nbytes;
    }

  /* TODO: check for OLD version: no compression */
  if (backup_header_p->bk_hdr_version == FILEIO_BACKUP_NO_ZIP_HEADER_VERSION)
    {
      backup_header_p->bkpagesize = backup_header_p->db_iopagesize;
      backup_header_p->zip_method = FILEIO_ZIP_NONE_METHOD;
      backup_header_p->zip_level = FILEIO_ZIP_NONE_LEVEL;
    }

  if (backup_header_p->bk_hdr_version < FILEIO_BACKUP_LOG_END_HEADER_VERSION)
    {
      /* the field did not exist; the bytes there are whatever followed the old struct in the header page */
      LSA_SET_NULL (&backup_header_p->start_log_end_lsa);
    }

  if (to_read_nbytes > 0)
    {
      return ER_FAILED;
    }
  else
    {
      return NO_ERROR;
    }
}

/*
 * fileio_start_restore () - Start a restore session
 *   return: session or NULL
 *   db_fullname(in): Name of the database to backup
 *   backup_source(in): Name of backup destination device (file or directory)
 *   match_dbcreation(out): Creation of data base of backup
 *   db_iopagesize(out): Database size of database in backup
 *   db_compatibility(out): Disk compatibility of database in backup
 *   session(in/out): The session array
 *   level(in): The presumed backup level
 *   authenticate(in): true when validation of new bkup volume header needed
 *   match_bkupcreation(in): explicit timestamp to match in new backup volume
 *   restore_verbose_file_path(in):
 *   newvolpath(in): restore the database and log volumes to the path
 *                   specified in the database-loc-file
 */
FILEIO_BACKUP_SESSION *
fileio_start_restore (THREAD_ENTRY * thread_p, const char *db_full_name_p, char *backup_source_p,
		      INT64 match_db_creation_time, PGLENGTH * db_io_page_size_p, float *db_compatibility_p,
		      FILEIO_BACKUP_SESSION * session_p, FILEIO_BACKUP_LEVEL level, bool is_authenticate,
		      INT64 match_backup_creation_time, const char *restore_verbose_file_path, bool is_new_vol_path)
{
  FILEIO_BACKUP_SESSION *temp_session_p;

  /* Initialize the session array and open the backup source device. */
  if (fileio_initialize_restore (thread_p, db_full_name_p, backup_source_p, session_p, level,
				 restore_verbose_file_path, is_new_vol_path) == NULL)
    {
      return NULL;
    }

  temp_session_p =
    fileio_continue_restore (thread_p, db_full_name_p, match_db_creation_time, session_p, true, is_authenticate,
			     match_backup_creation_time);
  if (temp_session_p != NULL)
    {
      *db_io_page_size_p = session_p->bkup.bkuphdr->db_iopagesize;
      *db_compatibility_p = session_p->bkup.bkuphdr->db_compatibility;
    }

  return (temp_session_p);
}

static int
fileio_make_error_message (THREAD_ENTRY * thread_p, char *error_message_p)
{
  char *header_message_p = NULL;
  char *remote_message_p = NULL;

  if (asprintf (&header_message_p, "%s", msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_IO,
							 MSGCAT_FILEIO_INCORRECT_BKVOLUME)) < 0)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      return ER_FAILED;
    }

  if (asprintf (&remote_message_p, "%s%s", header_message_p, error_message_p) < 0)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      free (header_message_p);
      return ER_FAILED;
    }

  (void) fileio_request_user_response (thread_p, FILEIO_PROMPT_DISPLAY_ONLY, remote_message_p, NULL, NULL, -1, -1, NULL,
				       -1);
  free (header_message_p);
  free (remote_message_p);
  return NO_ERROR;
}

/*
 * fileio_continue_restore () - CONTINUE A RESTORE SESSION
 *   return: session or NULL
 *   db_fullname(in): Name of the database to backup
 *   db_creation(in): Creation of time data base
 *   session(in/out): The session array
 *   first_time(in): true the first time called during restore
 *   authenticate(in): true when validation of new bkup volume header needed
 *   match_bkupcreation(in): Creation time of backup
 *
 * Note: Called when a new backup volume is needed, this routine locates and
 *       opens the desired backup volume for this database. Also authenticates
 *       that it has the right timestamp, level, unit_num etc.
 *       The match_dbcreation parameter specifies an explicit bkup timestamp
 *       that must be matched. This is useful when one level is restored and
 *       the next is required. A zero for this variable will ignore the test.
 */
static FILEIO_BACKUP_SESSION *
fileio_continue_restore (THREAD_ENTRY * thread_p, const char *db_full_name_p, INT64 db_creation_time,
			 FILEIO_BACKUP_SESSION * session_p, bool is_first_time, bool is_authenticate,
			 INT64 match_backup_creation_time)
{
  FILEIO_BACKUP_HEADER *backup_header_p;
  int unit_num = FILEIO_INITIAL_BACKUP_UNITS;
  PAGEID expect_page_id;
  bool is_need_retry;
  bool is_original_header = true;
  char *error_message_p = NULL;
  struct stat stbuf;
  const char *db_nopath_name_p;
  char copy_name[PATH_MAX];
  char orig_name[PATH_MAX];
  FILEIO_BACKUP_LEVEL level;
  int exists;
  int search_loop_count = 0;
  char io_timeval[CTIME_MAX];

  memset (io_timeval, 0, sizeof (io_timeval));

  /* Note that for the first volume to be restored, bkuphdr must have been initialized with sensible defaults for these
   * variables. */
  unit_num = session_p->bkup.bkuphdr->unit_num;
  level = session_p->bkup.bkuphdr->level;
  do
    {
      is_need_retry = false;
      /* Have to locate and open the desired volume */
      while (session_p->bkup.vdes == NULL_VOLDES)
	{

	  /* If the name chosen is a actually a directory, then append correct backup volume name here. */
	  exists = stat (session_p->bkup.vlabel, &stbuf) != -1;
	  if (session_p->bkup.dtype != FILEIO_BACKUP_VOL_DEVICE && (exists && S_ISDIR (stbuf.st_mode)))
	    {
	      db_nopath_name_p = fileio_get_base_file_name (db_full_name_p);
	      strcpy (copy_name, session_p->bkup.vlabel);
	      fileio_make_backup_name (session_p->bkup.name, db_nopath_name_p, copy_name, level, unit_num);
	      session_p->bkup.vlabel = session_p->bkup.name;
	    }

	  if (search_loop_count == 0)
	    {
	      strcpy (orig_name, session_p->bkup.vlabel);
	    }

	  session_p->bkup.vdes = fileio_open (session_p->bkup.vlabel, O_RDONLY, 0);
	  if (session_p->bkup.vdes == NULL_VOLDES)
	    {

	      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IO_MOUNT_FAIL, 1, session_p->bkup.vlabel);
	      fprintf (stdout, "%s\n", er_msg ());
	      /* Since we cannot find what they were just looking for, reset the name to what we started looking for in
	       * the first place. */
	      strcpy (session_p->bkup.name, orig_name);
	      /* Attempt to locate the desired volume */
	      if (fileio_find_restore_volume (thread_p, db_full_name_p, session_p->bkup.name, unit_num, level,
					      MSGCAT_FILEIO_RESTORE_FIND_REASON) == FILEIO_RELOCATION_QUIT)
		{
		  /* Cannot access backup file. Restore from backup is cancelled. */
		  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_CANNOT_ACCESS_BACKUP, 1,
			  session_p->bkup.vlabel);
		  return NULL;
		}
	    }
	  search_loop_count++;
	}

      /* Read description of the backup file. */
      if (fileio_read_restore_header (session_p) != NO_ERROR)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IO_NOT_A_BACKUP, 1, session_p->bkup.vlabel);
	  is_need_retry = true;
	  goto retry_newvol;
	}

      backup_header_p = session_p->bkup.bkuphdr;
      /* check for restoring the database and log volumes to the path specified in the database-loc-file */
      if (session_p->bkup.loc_db_fullname[0] != '\0')
	{
	  /* replace db_fullname with the databases.txt info */
	  strncpy (backup_header_p->db_fullname, session_p->bkup.loc_db_fullname, PATH_MAX);
	}

      /* Always check for a valid magic number, regardless of whether we need to check other authentications. */
      if (strcmp (backup_header_p->magic, CUBRID_MAGIC_DATABASE_BACKUP) != 0)
	{
	  if (strcmp (backup_header_p->magic, CUBRID_MAGIC_DATABASE_BACKUP_OLD) == 0)
	    {
	      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_BKUP_INCOMPATIBLE, 3, rel_name (),
		      rel_release_string (), "unknown");
	      return NULL;
	    }

	  if (is_first_time)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IO_NOT_A_BACKUP, 1, session_p->bkup.vlabel);
	      return NULL;
	    }
	  else
	    {
	      if (asprintf (&error_message_p,
			    msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_IO, MSGCAT_FILEIO_MAGIC_MISMATCH),
			    session_p->bkup.vlabel) < 0)
		{
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
		  return NULL;
		}

	      if (fileio_make_error_message (thread_p, error_message_p) != NO_ERROR)
		{
		  free (error_message_p);
		  return NULL;
		}

	      free (error_message_p);
	      is_need_retry = true;
	      goto retry_newvol;
	    }
	}

      /* Should check the release version before we do anything */
      if (is_first_time && rel_is_log_compatible (backup_header_p->db_release, rel_release_string ()) != true)
	{
	  /*
	   * First time this database is restarted using the current version of
	   * CUBRID. Recovery should be done using the old version of the
	   * system
	   */
	  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_RECOVER_ON_OLD_RELEASE, 4, rel_name (),
		  backup_header_p->db_release, rel_release_string (), rel_release_string ());
	  return NULL;
	}

      if (is_authenticate)
	{
	  if (is_first_time)
	    {
	      LSA_COPY (&session_p->dbfile.lsa, &backup_header_p->start_lsa);
	    }

	  if (level != backup_header_p->level)
	    {
	      if (asprintf (&error_message_p,
			    msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_IO, MSGCAT_FILEIO_LEVEL_MISMATCH),
			    session_p->bkup.vlabel, backup_header_p->level, level) < 0)
		{
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
		  return NULL;
		}

	      if (fileio_make_error_message (thread_p, error_message_p) != NO_ERROR)
		{
		  free (error_message_p);
		  return NULL;
		}

	      free (error_message_p);
	      is_need_retry = true;
	      goto retry_newvol;
	    }

	  /* Test the timestamp of when the backup was taken. */
	  if (match_backup_creation_time != 0
	      && difftime ((time_t) match_backup_creation_time, (time_t) backup_header_p->start_time))
	    {
	      char save_time1[64];

	      fileio_ctime (&backup_header_p->start_time, io_timeval);
	      strcpy (save_time1, io_timeval);

	      fileio_ctime (&match_backup_creation_time, io_timeval);
	      if (asprintf (&error_message_p,
			    msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_IO, MSGCAT_FILEIO_BACKUP_TIME_MISMATCH),
			    session_p->bkup.vlabel, save_time1, io_timeval) < 0)
		{
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
		  return NULL;
		}

	      if (fileio_make_error_message (thread_p, error_message_p) != NO_ERROR)
		{
		  free (error_message_p);
		  return NULL;
		}

	      free (error_message_p);
	      is_need_retry = true;
	      goto retry_newvol;
	    }

	  /* Need to match the expected unit_num */
	  if (unit_num != backup_header_p->unit_num)
	    {
	      if (asprintf (&error_message_p,
			    msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_IO, MSGCAT_FILEIO_UNIT_NUM_MISMATCH),
			    session_p->bkup.vlabel, backup_header_p->unit_num, unit_num) < 0)
		{
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
		  return NULL;
		}

	      if (fileio_make_error_message (thread_p, error_message_p) != NO_ERROR)
		{
		  free (error_message_p);
		  return NULL;
		}

	      free (error_message_p);
	      is_need_retry = true;
	      goto retry_newvol;
	    }

	  /* Should this one be treated as fatal? */
	  expect_page_id = (is_first_time) ? FILEIO_BACKUP_START_PAGE_ID : FILEIO_BACKUP_VOL_CONT_PAGE_ID;
	  if (backup_header_p->iopageid != expect_page_id)
	    {
	      if (asprintf (&error_message_p,
			    msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_IO, MSGCAT_FILEIO_MAGIC_MISMATCH),
			    session_p->bkup.vlabel) < 0)
		{
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
		  return NULL;
		}

	      if (fileio_make_error_message (thread_p, error_message_p) != NO_ERROR)
		{
		  free (error_message_p);
		  return NULL;
		}
	      free (error_message_p);
	      is_need_retry = true;
	      goto retry_newvol;
	    }

	  /* NOTE: This could mess with restoring to a new location */
	  if (strcmp (backup_header_p->db_fullname, db_full_name_p) != 0
	      || (db_creation_time > 0 && difftime ((time_t) db_creation_time, (time_t) backup_header_p->db_creation)))
	    {
	      if (is_first_time)
		{
		  char save_time1[64];
		  char save_time2[64];

		  fileio_ctime (&backup_header_p->db_creation, io_timeval);
		  strcpy (save_time1, io_timeval);

		  fileio_ctime (&db_creation_time, io_timeval);
		  strcpy (save_time2, io_timeval);

		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IO_NOT_A_BACKUP_OF_GIVEN_DATABASE, 5,
			  session_p->bkup.vlabel, backup_header_p->db_fullname, save_time1, db_full_name_p, save_time2);
		  return NULL;
		}
	      else
		{
		  fileio_ctime (&backup_header_p->db_creation, io_timeval);
		  if (asprintf (&error_message_p,
				msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_IO, MSGCAT_FILEIO_DB_MISMATCH),
				session_p->bkup.vlabel, backup_header_p->db_fullname, io_timeval) < 0)
		    {
		      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
		      return NULL;
		    }

		  if (fileio_make_error_message (thread_p, error_message_p) != NO_ERROR)
		    {
		      free (error_message_p);
		      return NULL;
		    }

		  free (error_message_p);
		  is_need_retry = true;
		  goto retry_newvol;
		}
	    }
	}
      /* Passed all tests above */
      break;
    retry_newvol:
      is_original_header = false;
      /* close it, in case it was opened previously */
      if (session_p->bkup.vdes != NULL_VOLDES)
	{
	  fileio_close (session_p->bkup.vdes);
	  session_p->bkup.vdes = NULL_VOLDES;
	}

      /* Since there was a problem, let the user try again */
      if (fileio_find_restore_volume (thread_p, db_full_name_p, session_p->bkup.name, unit_num, level,
				      MSGCAT_FILEIO_RESTORE_FIND_REASON) == FILEIO_RELOCATION_QUIT)
	{
	  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_CANNOT_ACCESS_BACKUP, 1, session_p->bkup.vlabel);
	  return NULL;
	}
    }
  while (is_need_retry);
  backup_header_p = session_p->bkup.bkuphdr;
  /*
   * If we read an archive header and notice that the buffer size
   * was different than our current bkup.iosize then we will have
   * to REALLOC the io areas set up in _init.  Same for the
   * when the database IO pagesize changes.
   */
  if (backup_header_p->bkup_iosize > session_p->bkup.iosize)
    {
      session_p->bkup.buffer = (char *) realloc (session_p->bkup.buffer, backup_header_p->bkup_iosize);
      if (session_p->bkup.buffer == NULL)
	{
	  return NULL;
	}
      session_p->bkup.ptr = session_p->bkup.buffer;	/* reinit in case it moved */
    }
  /* Always use the saved size from the backup to restore with */
  session_p->bkup.iosize = backup_header_p->bkup_iosize;
  /* backuped page is bigger than the current DB pagesize. must resize read buffer */
  if (is_first_time)
    {
      if (backup_header_p->db_iopagesize > IO_PAGESIZE)
	{
	  int io_pagesize, size;
	  io_pagesize = backup_header_p->db_iopagesize;
	  if (session_p->dbfile.level == FILEIO_BACKUP_FULL_LEVEL)
	    {
	      io_pagesize *= FILEIO_FULL_LEVEL_EXP;
	    }

	  size = MAX (io_pagesize + FILEIO_BACKUP_PAGE_OVERHEAD, FILEIO_BACKUP_FILE_HEADER_PAGE_SIZE);
	  free_and_init (session_p->dbfile.area);
	  session_p->dbfile.area = (FILEIO_BACKUP_PAGE *) malloc (size);
	  if (session_p->dbfile.area == NULL)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) size);
	      return NULL;
	    }
	}
    }

  return session_p;
}

/*
 * fileio_finish_restore () - Finish the restore session
 *   return:
 *   session(in/out): The session array
 */
int
fileio_finish_restore (THREAD_ENTRY * thread_p, FILEIO_BACKUP_SESSION * session_p)
{
  int success;

  success = fileio_synchronize_all (thread_p);
  fileio_abort_restore (thread_p, session_p);

  return success;
}

/*
 * fileio_list_restore () - List description of current backup source
 *   return: session or NULL
 *   db_fullname(in): Name of the database to backup
 *   backup_source(out): Name of backup source device (file or directory)
 *   level(in): The presumed backup level
 *   newvolpath(in): restore the database and log volumes to the path
 *                   specified in the database-loc-file
 */
int
fileio_list_restore (THREAD_ENTRY * thread_p, const char *db_full_name_p, char *backup_source_p,
		     FILEIO_BACKUP_LEVEL level, bool is_new_vol_path)
{
  FILEIO_BACKUP_SESSION backup_session;
  FILEIO_BACKUP_SESSION *session_p = &backup_session;
  FILEIO_BACKUP_HEADER *backup_header_p;
  FILEIO_BACKUP_FILE_HEADER *file_header_p;
  PGLENGTH db_iopagesize;
  float db_compatibility;
  int nbytes, i;
  INT64 db_creation_time = 0;
  char file_name[PATH_MAX];
  time_t tmp_time;
  char time_val[CTIME_MAX];

  if (fileio_start_restore (thread_p, db_full_name_p, backup_source_p, db_creation_time, &db_iopagesize,
			    &db_compatibility, session_p, level, false, 0, NULL, is_new_vol_path) == NULL)
    {
      /* Cannot access backup file.. Restore from backup is cancelled */
      if (er_errid () == ER_GENERIC_ERROR)
	{
	  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_CANNOT_ACCESS_BACKUP, 1, backup_source_p);
	}
      return ER_FAILED;
    }

  /* First backup header was just read */
  backup_header_p = session_p->bkup.bkuphdr;
  /* this check is probably redundant */
  if (backup_header_p->iopageid != FILEIO_BACKUP_START_PAGE_ID
      && backup_header_p->iopageid != FILEIO_BACKUP_VOL_CONT_PAGE_ID)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IO_NOT_A_BACKUP, 1, session_p->bkup.vlabel);
      goto error;
    }

  /* Show the backup volume header information. */
  fprintf (stdout, "%s", msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_IO, MSGCAT_FILEIO_BKUP_HDR));

  tmp_time = (time_t) backup_header_p->db_creation;
  (void) ctime_r (&tmp_time, time_val);
  fprintf (stdout, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_IO, MSGCAT_FILEIO_BKUP_HDR_DBINFO),
	   backup_header_p->db_fullname, time_val, backup_header_p->db_iopagesize);
  fprintf (stdout, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_IO, MSGCAT_FILEIO_BKUP_HDR_LEVEL),
	   backup_header_p->level, fileio_get_backup_level_string (backup_header_p->level),
	   backup_header_p->start_lsa.pageid, backup_header_p->start_lsa.offset, backup_header_p->chkpt_lsa.pageid,
	   backup_header_p->chkpt_lsa.offset);

  tmp_time = (time_t) backup_header_p->start_time;
  (void) ctime_r (&tmp_time, time_val);
  fprintf (stdout, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_IO, MSGCAT_FILEIO_BKUP_HDR_TIME), time_val,
	   backup_header_p->unit_num);
  fprintf (stdout, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_IO, MSGCAT_FILEIO_BKUP_HDR_RELEASES),
	   backup_header_p->db_release, backup_header_p->db_compatibility);
  fprintf (stdout, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_IO, MSGCAT_FILEIO_BKUP_HDR_BKUP_PAGESIZE),
	   backup_header_p->bkpagesize);
  fprintf (stdout, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_IO, MSGCAT_FILEIO_BKUP_HDR_ZIP_INFO),
	   backup_header_p->zip_method, fileio_get_zip_method_string (backup_header_p->zip_method),
	   backup_header_p->zip_level, fileio_get_zip_level_string (backup_header_p->zip_level));
  fprintf (stdout, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_IO, MSGCAT_FILEIO_BKUP_HDR_INC_ACTIVELOG),
	   backup_header_p->skip_activelog ? "NO" : "YES");

  for (i = FILEIO_BACKUP_FULL_LEVEL; i < FILEIO_BACKUP_UNDEFINED_LEVEL && backup_header_p->previnfo[i].at_time > 0; i++)
    {
      tmp_time = (time_t) backup_header_p->previnfo[i].at_time;
      (void) ctime_r (&tmp_time, time_val);
      fprintf (stdout, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_IO, MSGCAT_FILEIO_BKUP_HDR_LX_LSA), i,
	       time_val, backup_header_p->previnfo[i].lsa.pageid, backup_header_p->previnfo[i].lsa.offset);
    }

  if (strlen (backup_header_p->db_prec_bkvolname) > 0)
    {
      fprintf (stdout, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_IO, MSGCAT_FILEIO_BKUP_PREV_BKVOL),
	       backup_header_p->db_prec_bkvolname);
    }

  /* Reminder this is not implemented yet, so no need to show it */
  if (strlen (backup_header_p->db_next_bkvolname) > 0)
    {
      fprintf (stdout, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_IO, MSGCAT_FILEIO_BKUP_NEXT_BKVOL),
	       backup_header_p->db_next_bkvolname);
    }

  fprintf (stdout, "\n");
  /* If this is not the first tape, then the header information of the backup is all we show. */
  if (backup_header_p->unit_num != FILEIO_INITIAL_BACKUP_UNITS)
    {
      return fileio_finish_restore (thread_p, session_p);
    }

  /* Start reading information of every database volumes/files of the database which is in backup. */
  file_header_p = (FILEIO_BACKUP_FILE_HEADER *) (&session_p->dbfile.area->iopage);
  while (true)
    {
      nbytes = FILEIO_BACKUP_FILE_HEADER_PAGE_SIZE;
      if (fileio_read_restore (thread_p, session_p, nbytes) != NO_ERROR)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IO_RESTORE_READ_ERROR, 1, session_p->bkup.bkuphdr->unit_num);
	  goto error;
	}

      if (FILEIO_GET_BACKUP_PAGE_ID (session_p->dbfile.area) == FILEIO_BACKUP_END_PAGE_ID)
	{
	  break;
	}

      if (FILEIO_GET_BACKUP_PAGE_ID (session_p->dbfile.area) != FILEIO_BACKUP_FILE_START_PAGE_ID)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IO_BKUP_DATABASE_VOLUME_OR_FILE_EXPECTED, 0);
	  goto error;
	}

      fprintf (stdout, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_IO, MSGCAT_FILEIO_BKUP_FILE),
	       file_header_p->vlabel, file_header_p->volid, file_header_p->nbytes, CEIL_PTVDIV (file_header_p->nbytes,
												IO_PAGESIZE));
      session_p->dbfile.volid = file_header_p->volid;
      session_p->dbfile.nbytes = file_header_p->nbytes;
      strncpy (file_name, file_header_p->vlabel, PATH_MAX);
      session_p->dbfile.vlabel = file_name;
      /* Read all file pages until the end of the file */
      if (fileio_skip_restore_volume (thread_p, session_p) != NO_ERROR)
	{
	  goto error;
	}
    }

  fprintf (stdout, "\n");
  return fileio_finish_restore (thread_p, session_p);
error:
  fileio_abort_restore (thread_p, session_p);
  return ER_FAILED;
}

/*
 * fileio_get_backup_volume () - Get backup volume
 *   return: session or NULL
 *   db_fullname(in): Name of the database to backup
 *   logpath(in): Directory where the log volumes reside
 *   user_backuppath(in): Backup path that user specified
 *   from_volbackup (out) : Name of the backup volume
 *
 */
int
fileio_get_backup_volume (THREAD_ENTRY * thread_p, const char *db_fullname, const char *logpath,
			  const char *user_backuppath, int try_level, char *from_volbackup)
{
  FILE *backup_volinfo_fp = NULL;	/* Pointer to backup */
  const char *nopath_name;	/* Name without path */
  const char *volnameptr;
  int retry;
  int error_code = NO_ERROR;
  char format_string[64];
  struct stat stbuf;

  sprintf (format_string, "%%%ds", PATH_MAX - 1);

  nopath_name = fileio_get_base_file_name (db_fullname);
  fileio_make_backup_volume_info_name (from_volbackup, logpath, nopath_name);

  while ((stat (from_volbackup, &stbuf) == -1) || (backup_volinfo_fp = fopen (from_volbackup, "r")) == NULL)
    {
      /*
       * When user specifies an explicit location, the backup vinf
       * file is optional.
       */
      if (user_backuppath != NULL)
	{
	  break;
	}

      /*
       * Backup volume information is not online
       */
      fprintf (stdout, "%s", msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_LOG, MSGCAT_LOG_STARTS));
      fprintf (stdout, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_LOG, MSGCAT_LOG_BACKUPINFO_NEEDED),
	       from_volbackup);
      fprintf (stdout, "%s", msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_LOG, MSGCAT_LOG_STARTS));

      if (scanf ("%d", &retry) != 1)
	{
	  retry = 0;
	}

      switch (retry)
	{
	case 0:		/* quit */
	  /* Cannot access backup file.. Restore from backup is cancelled */
	  error_code = ER_LOG_CANNOT_ACCESS_BACKUP;
	  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, error_code, 1, from_volbackup);
	  return error_code;

	case 2:
	  fprintf (stdout, "%s", msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_LOG, MSGCAT_LOG_NEWLOCATION));
	  if (scanf (format_string, from_volbackup) != 1)
	    {
	      /* Cannot access backup file.. Restore from backup is cancelled */
	      error_code = ER_LOG_CANNOT_ACCESS_BACKUP;
	      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, error_code, 1, from_volbackup);
	      return error_code;
	    }
	  break;

	case 1:
	default:
	  break;
	}
    }

  /*
   * If we get to here, we can read the bkvinf file, OR one does not
   * exist and it is not required.
   */
  if (backup_volinfo_fp != NULL)
    {
      if (fileio_read_backup_info_entries (backup_volinfo_fp, FILEIO_FIRST_BACKUP_VOL_INFO) == NO_ERROR)
	{
	  volnameptr =
	    fileio_get_backup_info_volume_name ((FILEIO_BACKUP_LEVEL) try_level, FILEIO_INITIAL_BACKUP_UNITS,
						FILEIO_FIRST_BACKUP_VOL_INFO);
	  if (volnameptr != NULL)
	    {
	      strcpy (from_volbackup, volnameptr);
	    }
	  else
	    {
	      fileio_make_backup_name (from_volbackup, nopath_name, logpath, (FILEIO_BACKUP_LEVEL) try_level,
				       FILEIO_INITIAL_BACKUP_UNITS);
	    }
	}
      else
	{
	  fclose (backup_volinfo_fp);
	  return ER_FAILED;
	}

      fclose (backup_volinfo_fp);
    }

  if (user_backuppath != NULL)
    {
      strncpy (from_volbackup, user_backuppath, PATH_MAX - 1);
    }

  return NO_ERROR;
}


/*
 * fileio_get_next_restore_file () - Find information of next file to restore
 *   return: -1 A failure, 0 No more files to restore (End of BACKUP),
 *           1 There is a file to restore
 *   session(in/out): The session array
 *   filename(out): the name of next file to restore
 *   volid(out): Identifier of the database volume/file to restore
 *   vol_nbytes(out): Nbytes of the database volume/file to restore
 */
int
fileio_get_next_restore_file (THREAD_ENTRY * thread_p, FILEIO_BACKUP_SESSION * session_p, char *file_name_p,
			      VOLID * vol_id_p)
{
  FILEIO_BACKUP_FILE_HEADER *file_header_p;
  int nbytes;
  char file_path[PATH_MAX];

  /* Read the next database volume and/or file to restore. */
  file_header_p = (FILEIO_BACKUP_FILE_HEADER *) (&session_p->dbfile.area->iopage);
  nbytes = FILEIO_BACKUP_FILE_HEADER_PAGE_SIZE;
  if (fileio_read_restore (thread_p, session_p, nbytes) != NO_ERROR)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IO_RESTORE_READ_ERROR, 1, session_p->bkup.bkuphdr->unit_num);
      return -1;
    }

  if (FILEIO_GET_BACKUP_PAGE_ID (session_p->dbfile.area) == FILEIO_BACKUP_END_PAGE_ID)
    {
      if (session_p->bkup.dtype == FILEIO_BACKUP_VOL_DEVICE)
	{
	  fileio_read_backup_end_time_from_last_page (session_p);
	}
      return 0;
    }

  if (FILEIO_GET_BACKUP_PAGE_ID (session_p->dbfile.area) != FILEIO_BACKUP_FILE_START_PAGE_ID)
    {
      return -1;
    }

  session_p->dbfile.volid = file_header_p->volid;
  session_p->dbfile.nbytes = file_header_p->nbytes;
  session_p->dbfile.level = session_p->bkup.bkuphdr->level;

  /* check for restoring the database and log volumes to the path specified in the database-loc-file */
  if (session_p->bkup.loc_db_fullname[0] != '\0')
    {
      /* replace filename with the databases.txt info */
      if ((session_p->dbfile.volid == LOG_DBLOG_BKUPINFO_VOLID) || (session_p->dbfile.volid == LOG_DBLOG_INFO_VOLID)
	  || (session_p->dbfile.volid == LOG_DBLOG_ARCHIVE_VOLID)
	  || (session_p->dbfile.volid == LOG_DBLOG_ACTIVE_VOLID))
	{
	  sprintf (file_name_p, "%s%c%s", session_p->bkup.log_path, PATH_SEPARATOR,
		   fileio_get_base_file_name (file_header_p->vlabel));
	}
      else
	{
	  fileio_get_directory_path (file_path, session_p->bkup.loc_db_fullname);
	  sprintf (file_name_p, "%s%c%s", file_path, PATH_SEPARATOR, fileio_get_base_file_name (file_header_p->vlabel));
	}
    }
  else
    {
      strncpy (file_name_p, file_header_p->vlabel, PATH_MAX);
    }

  *vol_id_p = session_p->dbfile.volid;
  return 1;
}

/*
 * fileio_fill_hole_during_restore () - Fill in a hole found in the backup during
 *                           a restore
 *   return:
 *   next_pageid(out):
 *   stop_pageid(in):
 *   session(in/out): The session array
 *   page_bitmap(in): Page bitmap to record which pages have already
 *                    been restored
 *
 * Note: A hole is likely only for 2 reasons. After the system pages in
 *       permament temp volumes, or at the end of a volume if we stop backing
 *       up unallocated pages.
 */
static int
fileio_fill_hole_during_restore (THREAD_ENTRY * thread_p, int *next_page_id_p, int stop_page_id,
				 FILEIO_BACKUP_SESSION * session_p, FILEIO_RESTORE_PAGE_BITMAP * page_bitmap)
{
  FILEIO_PAGE *malloc_io_pgptr = NULL;

  if (malloc_io_pgptr == NULL)
    {
      malloc_io_pgptr = (FILEIO_PAGE *) malloc (IO_PAGESIZE);
      if (malloc_io_pgptr == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) IO_PAGESIZE);
	  return ER_FAILED;
	}
      memset ((char *) malloc_io_pgptr, 0, IO_PAGESIZE);
      (void) fileio_initialize_res (thread_p, malloc_io_pgptr, IO_PAGESIZE);
    }

  while (*next_page_id_p < stop_page_id)
    {
      /*
       * We did not back up a page since it was deallocated, or there
       * is a hole of some kind that must be filled in with correctly
       * formatted pages.
       */
      if (fileio_write_restore (thread_p, page_bitmap, session_p->dbfile.vdes, malloc_io_pgptr, session_p->dbfile.volid,
				*next_page_id_p) == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IO_RESTORE_READ_ERROR, 1, session_p->bkup.bkuphdr->unit_num);
	  return ER_FAILED;
	}
      *next_page_id_p += 1;
    }

  if (malloc_io_pgptr != NULL)
    {
      free_and_init (malloc_io_pgptr);
    }

  return NO_ERROR;
}

/*
 * fileio_decompress_restore_volume () - The number of bytes to decompress/read
 *                                        from the backup destination
 *   return:
 *   session(in/out): The session array
 *   nbytes(in): Number of bytes to read
 */
static int
fileio_decompress_restore_volume (THREAD_ENTRY * thread_p, FILEIO_BACKUP_SESSION * session_p, int nbytes)
{
  int error = NO_ERROR;
  FILEIO_THREAD_INFO *thread_info_p;
  FILEIO_QUEUE *queue_p;
  FILEIO_BACKUP_HEADER *backup_header_p;
  FILEIO_BACKUP_PAGE *save_area_p;
  FILEIO_NODE *node;

  assert (nbytes >= 0);

  thread_info_p = &session_p->read_thread_info;
  queue_p = &thread_info_p->io_queue;
  backup_header_p = session_p->bkup.bkuphdr;
  node = NULL;

  switch (backup_header_p->zip_method)
    {
    case FILEIO_ZIP_NONE_METHOD:
      if (fileio_read_restore (thread_p, session_p, nbytes) != NO_ERROR)
	{
	  error = ER_IO_RESTORE_READ_ERROR;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, backup_header_p->unit_num);
	  goto exit_on_error;
	}
      break;

    case FILEIO_ZIP_LZ4_METHOD:
      {
	int rv;
	FILEIO_ZIP_PAGE *zip_page;

	/* alloc queue node */
	node = fileio_allocate_node (queue_p, backup_header_p);
	if (node == NULL)
	  {
	    goto exit_on_error;
	  }

	assert (node->zip_info != NULL);
	zip_page = &node->zip_info->zip_page;

	save_area_p = session_p->dbfile.area;	/* save link */
	session_p->dbfile.area = (FILEIO_BACKUP_PAGE *) zip_page;

	rv = fileio_read_restore (thread_p, session_p, sizeof (int));
	session_p->dbfile.area = save_area_p;	/* restore link */
	if (rv != NO_ERROR)
	  {
	    error = ER_IO_RESTORE_READ_ERROR;
	    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, backup_header_p->unit_num);
	    goto exit_on_error;
	  }

	/* sanity check of the size values */
	if (zip_page->buf_len > nbytes || zip_page->buf_len == 0)
	  {
	    error = ER_IO_LZ4_COMPRESS_FAIL;	/* may be compress fail */
	    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 4, backup_header_p->zip_method,
		    fileio_get_zip_method_string (backup_header_p->zip_method), backup_header_p->zip_level,
		    fileio_get_zip_level_string (backup_header_p->zip_level));
#if defined(CUBRID_DEBUG)
	    fprintf (stdout, "io_restore_volume_decompress_read: block size error - data corrupted\n");
#endif /* CUBRID_DEBUG */
	    goto exit_on_error;
	  }
	else if (zip_page->buf_len < nbytes)
	  {
	    /* read compressed block data */
	    int unzip_len;

	    save_area_p = session_p->dbfile.area;	/* save link */
	    session_p->dbfile.area = (FILEIO_BACKUP_PAGE *) zip_page->buf;

	    rv = fileio_read_restore (thread_p, session_p, zip_page->buf_len);
	    session_p->dbfile.area = save_area_p;	/* restore link */
	    if (rv != NO_ERROR)
	      {
		error = ER_IO_RESTORE_READ_ERROR;
		er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, backup_header_p->unit_num);
		goto exit_on_error;
	      }

	    /* decompress - use safe decompressor as data might be corrupted during a file transfer */
	    // *INDENT-OFF*
	    unzip_len =
	      cubcompress::decompress<cubcompress::LZ4> ((const char *) zip_page->buf, zip_page->buf_len,
							 (char *) session_p->dbfile.area, nbytes);
	    // *INDENT-ON*

	    if (unzip_len < 0 || unzip_len != nbytes)
	      {
		error = ER_IO_LZ4_DECOMPRESS_FAIL;
		er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
#if defined(CUBRID_DEBUG)
		fprintf (stdout, "io_restore_volume_decompress_read: compressed data violation\n");
#endif /* CUBRID_DEBUG */
		goto exit_on_error;
	      }
	  }
	else
	  {
	    /* no compressed block */
	    rv = fileio_read_restore (thread_p, session_p, zip_page->buf_len);
	    if (rv != NO_ERROR)
	      {
		error = ER_IO_RESTORE_READ_ERROR;
		er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, backup_header_p->unit_num);
		goto exit_on_error;
	      }
	  }

      }
      break;

    case FILEIO_ZIP_LZO1X_METHOD:
    case FILEIO_ZIP_ZLIB_METHOD:
    default:
      error = ER_IO_RESTORE_READ_ERROR;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, backup_header_p->unit_num);
      goto exit_on_error;
    }

exit_on_end:

  /* free node */
  if (node)
    {
      (void) fileio_free_node (queue_p, node);
    }

  return error;
exit_on_error:

  if (error == NO_ERROR)
    {
      error = ER_FAILED;
    }
  goto exit_on_end;
}

/*
 * fileio_restore_volume () - Restore a volume/file of given database
 *   return:
 *   session_p(in/out):  The session array
 *   to_vlabel_p(in): Restore the next file using this name
 *   verbose_to_vlabel_p(in): Printable volume name
 *   prev_vlabel_p(out): Previous restored file name
 *   page_bitmap(in): Page bitmap to record which pages have already
 *                    been restored
 *   is_remember_pages(in): true if we need to track which pages are restored
 */
int
fileio_restore_volume (THREAD_ENTRY * thread_p, FILEIO_BACKUP_SESSION * session_p, char *to_vol_label_p,
		       char *verbose_to_vol_label_p, char *prev_vol_label_p, FILEIO_RESTORE_PAGE_BITMAP * page_bitmap,
		       bool is_remember_pages, bool & is_prev_vol_header_restored,
		       FILEIO_UNLINKED_VOLINFO_MAP & unlinked_volinfo)
{
  int next_page_id = 0;
  INT64 total_nbytes = 0;
  int nbytes;
  int from_npages, npages;
  FILEIO_RESTORE_PAGE_BITMAP *bitmap;
  int check_ratio = 0, check_npages = 0;
  FILEIO_BACKUP_HEADER *backup_header_p = session_p->bkup.bkuphdr;
  int unit;
  int i;
  char *buffer_p;
  bool incremental_includes_volume_header = false;

  npages = (int) CEIL_PTVDIV (session_p->dbfile.nbytes, IO_PAGESIZE);
  session_p->dbfile.vlabel = to_vol_label_p;
  nbytes = (int) MIN (backup_header_p->bkpagesize, session_p->dbfile.nbytes);
  unit = nbytes / IO_PAGESIZE;
  if (nbytes % IO_PAGESIZE)
    {
      unit++;
    }

#if defined(CUBRID_DEBUG)
  if (io_Bkuptrace_debug > 0)
    {
      fprintf (stdout, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_IO, MSGCAT_FILEIO_BKUP_FILE),
	       session_p->dbfile.vlabel, session_p->dbfile.volid, session_p->dbfile.nbytes,
	       CEIL_PTVDIV (session_p->dbfile.nbytes, IO_PAGESIZE));
      fprintf (stdout, "\n");
    }
#endif /* CUBRID_DEBUG */

  if (session_p->verbose_fp)
    {
      fprintf (session_p->verbose_fp, " %-28s | %10d | ", fileio_get_base_file_name (verbose_to_vol_label_p), npages);
      check_ratio = 1;
      check_npages = (int) (((float) npages / 25.0) * check_ratio);
    }

  /*
   * Reformatting the volume guarantees no pollution from old contents.
   * Note that for incremental restores, one can only reformat the volume
   * once ... the first time that volume is replaced.  This is needed
   * because we are applying the restoration in reverse time order.
   */
  if (!fileio_is_volume_exist (session_p->dbfile.vlabel))
    {
      session_p->dbfile.vdes =
	fileio_format (thread_p, NULL, session_p->dbfile.vlabel, session_p->dbfile.volid, npages, false, false, false,
		       IO_PAGESIZE, 0, false);
    }
  else
    {
      session_p->dbfile.vdes =
	fileio_mount (thread_p, NULL, session_p->dbfile.vlabel, session_p->dbfile.volid, false, false);
    }

  if (session_p->dbfile.vdes == NULL_VOLDES)
    {
      goto error;
    }

  /* For some volumes we do not keep track of the individual pages restored. */
  bitmap = (is_remember_pages) ? page_bitmap : NULL;
  /* Read all file pages until the end of the volume/file. */
  from_npages = (int) CEIL_PTVDIV (session_p->dbfile.nbytes, backup_header_p->bkpagesize);
  nbytes = FILEIO_RESTORE_DBVOLS_IO_PAGE_SIZE (session_p);

  while (true)
    {
      if (fileio_decompress_restore_volume (thread_p, session_p, nbytes) != NO_ERROR)
	{
	  goto error;
	}

      if (FILEIO_GET_BACKUP_PAGE_ID (session_p->dbfile.area) == FILEIO_BACKUP_FILE_END_PAGE_ID)
	{
	  /*
	   * End of File marker in backup, but may not be true end of file being
	   * restored so we have to continue filling in pages until the
	   * restored volume is finished.
	   */
	  if (session_p->dbfile.level == FILEIO_BACKUP_FULL_LEVEL && next_page_id < npages)
	    {
	      if (fileio_fill_hole_during_restore (thread_p, &next_page_id, npages, session_p, bitmap) != NO_ERROR)
		{
		  goto error;
		}
	    }
	  break;
	}

      if (FILEIO_GET_BACKUP_PAGE_ID (session_p->dbfile.area) > from_npages)
	{
	  /* Too many pages for this volume according to the file header */
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IO_RESTORE_PAGEID_OUTOF_BOUNDS, 4, backup_header_p->unit_num,
		  FILEIO_GET_BACKUP_PAGE_ID (session_p->dbfile.area), from_npages, session_p->dbfile.volid);
	  goto error;
	}

#if defined(CUBRID_DEBUG)
      fprintf (stdout, "fileio_restore_volume: %d\t%d,\t%d\n",
	       ((FILEIO_BACKUP_PAGE *) (session_p->dbfile.area))->iopageid,
	       *(PAGEID *) (((char *) (session_p->dbfile.area)) + offsetof (FILEIO_BACKUP_PAGE, iopage) +
			    backup_header_p->bkpagesize), backup_header_p->bkpagesize);
#endif

      if (!FILEIO_CHECK_RESTORE_PAGE_ID (session_p->dbfile.area, backup_header_p->bkpagesize))
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IO_RESTORE_READ_ERROR, 1, backup_header_p->unit_num);
	  goto error;
	}

      /* Check for holes and fill them (only for full backup level) */
      if (session_p->dbfile.level == FILEIO_BACKUP_FULL_LEVEL
	  && (next_page_id < FILEIO_GET_BACKUP_PAGE_ID (session_p->dbfile.area)))
	{
	  if (fileio_fill_hole_during_restore (thread_p, &next_page_id, session_p->dbfile.area->iopageid, session_p,
					       bitmap) != NO_ERROR)
	    {
	      goto error;
	    }
	}

      /* Restore the page we just read in */
      if (session_p->dbfile.level != FILEIO_BACKUP_FULL_LEVEL)
	{
	  next_page_id = FILEIO_GET_BACKUP_PAGE_ID (session_p->dbfile.area);
	  if (next_page_id == DISK_VOLHEADER_PAGE)
	    {
	      incremental_includes_volume_header = true;
	    }
	}

      buffer_p = (char *) &session_p->dbfile.area->iopage;
      for (i = 0; i < unit && next_page_id < npages; i++)
	{
	  if (fileio_write_restore (thread_p, bitmap, session_p->dbfile.vdes, buffer_p + i * IO_PAGESIZE,
				    session_p->dbfile.volid, next_page_id) == NULL)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IO_RESTORE_READ_ERROR, 1, backup_header_p->unit_num);
	      goto error;
	    }

	  next_page_id += 1;
	  total_nbytes += IO_PAGESIZE;
	  if (session_p->verbose_fp && npages >= 25 && next_page_id >= check_npages)
	    {
	      fprintf (session_p->verbose_fp, "#");
	      check_ratio++;
	      check_npages = (int) (((float) npages / 25.0) * check_ratio);
	    }
	}
    }

  if (total_nbytes > session_p->dbfile.nbytes && session_p->dbfile.volid < LOG_DBFIRST_VOLID)
    {
      (void) ftruncate (session_p->dbfile.vdes, session_p->dbfile.nbytes);
    }

#if defined(CUBRID_DEBUG)
  if (io_Bkuptrace_debug >= 2 && bitmap)
    {
      fileio_page_bitmap_dump (stdout, bitmap);
      (void) fprintf (stdout, "\n\n");
    }
#endif /* CUBRID_DEBUG */

  /* check for restoring the database and log volumes to the path specified in the database-loc-file */
  if (session_p->bkup.loc_db_fullname[0] != '\0' && session_p->dbfile.volid >= LOG_DBFIRST_VOLID)
    {
      /* Volume header page may not be included in incremental backup volumes.
       * This means that volume header of a partially restoredb volume may not exist.
       */
      if (session_p->dbfile.level == FILEIO_BACKUP_FULL_LEVEL || incremental_includes_volume_header == true)
	{
	  VOLID volid;

	  volid = session_p->dbfile.volid;
	  if (disk_set_creation (thread_p, volid, to_vol_label_p, &backup_header_p->db_creation,
				 &session_p->bkup.last_chkpt_lsa, false, DISK_FLUSH_AND_INVALIDATE) != NO_ERROR)
	    {
	      goto error;
	    }

	  if (volid != LOG_DBFIRST_VOLID && is_prev_vol_header_restored)
	    {
	      VOLID prev_volid;
	      int prev_vdes;

	      /* previous vol */
	      prev_volid = fileio_find_previous_perm_volume (thread_p, volid);
	      prev_vdes = fileio_mount (thread_p, NULL, prev_vol_label_p, prev_volid, false, false);
	      if (prev_vdes == NULL_VOLDES)
		{
		  goto error;
		}

	      if (disk_set_link (thread_p, prev_volid, volid, to_vol_label_p, false, DISK_FLUSH_AND_INVALIDATE) !=
		  NO_ERROR)
		{
		  fileio_dismount (thread_p, prev_vdes);
		  goto error;
		}

	      fileio_dismount (thread_p, prev_vdes);

	      if (unlinked_volinfo.count (volid))
		{
		  // The volume headers of both previous and current volumes are in the full or big incremental backup volumes.
		  // Therefore, the link between the two volumes is naturally established during the restoration process.
		  // So, there is no need to explicitly set it.
		  unlinked_volinfo.erase (volid);

		  _er_log_debug (ARG_FILE_LINE, "RESTOREDB: [UNSAVE UNLINK] [lv%d] volid=%d, vol=%s, prev_vol=%s\n",
				 session_p->dbfile.level, volid, to_vol_label_p, prev_vol_label_p);
		}
	    }

	  if (incremental_includes_volume_header == true)
	    {
	      if (volid != LOG_DBFIRST_VOLID && is_prev_vol_header_restored == false)
		{
		  _er_log_debug (ARG_FILE_LINE, "RESTOREDB: [FOUND UNLINK] [lv%d] volid=%d, vol=%s, prev_vol=%s\n",
				 session_p->dbfile.level, volid, to_vol_label_p, prev_vol_label_p);

		  if (!unlinked_volinfo.count (volid))
		    {
		      unlinked_volinfo[volid] =
			std::make_pair (std::string (to_vol_label_p), std::string (prev_vol_label_p));

		      _er_log_debug (ARG_FILE_LINE, "RESTOREDB: [SAVE UNLINK] [lv%d] volid=%d, vol=%s, prev_vol=%s\n",
				     session_p->dbfile.level, volid, to_vol_label_p, prev_vol_label_p);
		    }
		}
	    }

	  is_prev_vol_header_restored = true;
	}
      else
	{
	  is_prev_vol_header_restored = false;
	}

      /* save current volname */
      strncpy (prev_vol_label_p, to_vol_label_p, PATH_MAX);
    }

  fileio_dismount (thread_p, session_p->dbfile.vdes);
  session_p->dbfile.vdes = NULL_VOLDES;
  session_p->dbfile.volid = NULL_VOLID;
  session_p->dbfile.vlabel = NULL;

  if (session_p->verbose_fp)
    {
      if (next_page_id < 25)
	{
	  fprintf (session_p->verbose_fp, "######################### | done\n");
	}
      else
	{
	  while (check_ratio <= 25)
	    {
	      fprintf (session_p->verbose_fp, "#");
	      check_ratio++;
	    }
	  fprintf (session_p->verbose_fp, " | done\n");
	}
    }

  return NO_ERROR;

error:
  if (session_p->dbfile.vdes != NULL_VOLDES)
    {
      fileio_dismount (thread_p, session_p->dbfile.vdes);
    }

  session_p->dbfile.vdes = NULL_VOLDES;
  session_p->dbfile.volid = NULL_VOLID;
  session_p->dbfile.vlabel = NULL;

  return ER_FAILED;
}

/*
 * fileio_write_restore () - Write the content of the page described by pageid
 *                           to disk
 *   return: o_pgptr on success, NULL on failure
 *   page_bitmap(in): Page bitmap to record which pages have already
 *                    been restored
 *   vdes(in): Volume descriptor
 *   io_pgptr(in): In-memory address where the current content of page resides
 *   vol_id(in): volume identifier
 *   page_id(in): Page identifier
 *
 * Note: The contents of the page stored on io_pgptr buffer which is
 *       IO_PAGESIZE long are sent to disk using fileio_write. The restore pageid
 *       cache is updated.
 */
static void *
fileio_write_restore (THREAD_ENTRY * thread_p, FILEIO_RESTORE_PAGE_BITMAP * page_bitmap, int vol_fd, void *io_page_p,
		      VOLID vol_id, PAGEID page_id)
{
  bool is_set;
  FILEIO_WRITE_MODE write_mode = FILEIO_WRITE_DEFAULT_WRITE;

  write_mode = dwb_is_created () == true ? FILEIO_WRITE_NO_COMPENSATE_WRITE : FILEIO_WRITE_DEFAULT_WRITE;

  if (page_bitmap == NULL)
    {
      /* don't care about ht for this volume */
      if (fileio_write (thread_p, vol_fd, io_page_p, page_id, IO_PAGESIZE, write_mode) == NULL)
	{
	  return NULL;
	}
    }
  else
    {
#if !defined(NDEBUG)
      assert (page_bitmap->vol_id == vol_id);
#endif
      is_set = fileio_page_bitmap_is_set (page_bitmap, page_id);

      if (!is_set)
	{
	  if (fileio_write (thread_p, vol_fd, io_page_p, page_id, IO_PAGESIZE, write_mode) == NULL)
	    {
	      return NULL;
	    }

	  /* the full level is restored last; its pages are recorded too, for fileio_restore_format_unrestored_pages */
	  fileio_page_bitmap_set (page_bitmap, page_id);
	}
    }

  return io_page_p;
}

/*
 * fileio_skip_restore_volume () - Skip over the next db volume from the backup
 *                             during a restore
 *   return:
 *   session(in/out): The session array
 *
 * Note: Basically have to read all of the pages until we get to the end of
 *       the current backup file.  It is necessary to "fast forward" to the
 *       next backup meta-data.
 */
int
fileio_skip_restore_volume (THREAD_ENTRY * thread_p, FILEIO_BACKUP_SESSION * session_p)
{
  int nbytes;
  FILEIO_BACKUP_HEADER *backup_header_p = session_p->bkup.bkuphdr;

  /* Read all file pages until the end of the volume/file. */
  nbytes = FILEIO_RESTORE_DBVOLS_IO_PAGE_SIZE (session_p);
  while (true)
    {
      if (fileio_decompress_restore_volume (thread_p, session_p, nbytes) != NO_ERROR)
	{
	  goto error;
	}

      if (FILEIO_GET_BACKUP_PAGE_ID (session_p->dbfile.area) == FILEIO_BACKUP_FILE_END_PAGE_ID)
	{
	  /* End of FILE */
	  break;
	}

#if defined(CUBRID_DEBUG)
      fprintf (stdout, "fileio_skip_restore_volume: %d\t%d,\t%d\n",
	       ((FILEIO_BACKUP_PAGE *) (session_p->dbfile.area))->iopageid,
	       *(PAGEID *) (((char *) (session_p->dbfile.area)) + offsetof (FILEIO_BACKUP_PAGE, iopage) +
			    backup_header_p->bkpagesize), backup_header_p->bkpagesize);
#endif

      if (!FILEIO_CHECK_RESTORE_PAGE_ID (session_p->dbfile.area, backup_header_p->bkpagesize))
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IO_RESTORE_READ_ERROR, 1, backup_header_p->unit_num);
	  goto error;
	}
    }

  session_p->dbfile.vdes = NULL_VOLDES;
  session_p->dbfile.volid = NULL_VOLID;
  session_p->dbfile.vlabel = NULL;

  return NO_ERROR;

error:

  session_p->dbfile.vdes = NULL_VOLDES;
  session_p->dbfile.volid = NULL_VOLID;
  session_p->dbfile.vlabel = NULL;

  return ER_FAILED;
}

/*
 * fileio_find_restore_volume () - FIND NEW LOCATION OF A VOLUME TO RESTORE
 *   return:
 *   dbname(in): The name of the database to which the volume belongs
 *   to_volname(in): Name we think the volume is supposed to be
 *   unit_num(in):
 *   level(in): the backup level needed for restoration
 *   reason(in):
 *
 * Note: Prompt the user to tell us the path to backup volume we cannot seem
 *       to find.  Note that validation is not done here it must be done by
 *       the caller.
 */
static FILEIO_RELOCATION_VOLUME
fileio_find_restore_volume (THREAD_ENTRY * thread_p, const char *db_name_p, char *to_vol_name_p, int unit_num,
			    FILEIO_BACKUP_LEVEL level, int reason)
{
  char *ptr1 = NULL, *ptr2 = NULL, *ptr3 = NULL, *ptr4 = NULL;
  char new_vol_name[FILEIO_MAX_USER_RESPONSE_SIZE];
  char *fail_prompt_p = NULL;
  char *reprompt_p = NULL;
  char *full_message_p = NULL;
  FILEIO_RELOCATION_VOLUME rval;

  /* Try to build up the outgoing message */
  if (asprintf (&ptr1, "%s", msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_IO, MSGCAT_FILEIO_STARTS)) < 0
      || asprintf (&ptr2, "%s", msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_IO, reason)) < 0
      || asprintf (&ptr3, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_IO, MSGCAT_FILEIO_REST_RELO_NEEDED),
		   db_name_p, to_vol_name_p, unit_num, level, fileio_get_backup_level_string (level)) < 0
      || asprintf (&ptr4, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_IO, MSGCAT_FILEIO_REST_RELO_OPTIONS),
		   FILEIO_RELOCATION_QUIT, FILEIO_RELOCATION_RETRY, FILEIO_RELOCATION_ALTERNATE) < 0)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      rval = FILEIO_RELOCATION_QUIT;
      goto end;
    }

  if (asprintf (&fail_prompt_p, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_IO, MSGCAT_FILEIO_INPUT_RANGE_ERROR),
		(int) FILEIO_RELOCATION_FIRST, (int) FILEIO_RELOCATION_LAST) < 0
      || asprintf (&reprompt_p, "%s",
		   msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_IO, MSGCAT_FILEIO_NEWLOCATION)) < 0
      || asprintf (&full_message_p, "%s%s%s%s%s", ptr1, ptr2, ptr3, ptr4, ptr1) < 0)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      rval = FILEIO_RELOCATION_QUIT;
      goto end;
    }

  if (fileio_request_user_response (thread_p, FILEIO_PROMPT_RANGE_WITH_SECONDARY_STRING_TYPE, full_message_p,
				    new_vol_name, fail_prompt_p, FILEIO_RELOCATION_QUIT, FILEIO_RELOCATION_ALTERNATE,
				    reprompt_p, FILEIO_RELOCATION_ALTERNATE) != NO_ERROR)
    {
      rval = FILEIO_RELOCATION_QUIT;
      goto end;
    }

  /* interpret the user responses. */
  if (new_vol_name[0] - '0' == FILEIO_RELOCATION_RETRY)
    {
      rval = FILEIO_RELOCATION_RETRY;
    }
  else if (new_vol_name[0] - '0' == FILEIO_RELOCATION_ALTERNATE)
    {
      rval = FILEIO_RELOCATION_ALTERNATE;
      strcpy (to_vol_name_p, &new_vol_name[1]);
    }
  else
    {
      rval = FILEIO_RELOCATION_QUIT;
    }

end:
  if (ptr1)
    {
      free (ptr1);
    }

  if (ptr2)
    {
      free (ptr2);
    }

  if (ptr3)
    {
      free (ptr3);
    }

  if (ptr4)
    {
      free (ptr4);
    }

  if (fail_prompt_p)
    {
      free (fail_prompt_p);
    }

  if (reprompt_p)
    {
      free (reprompt_p);
    }

  if (full_message_p)
    {
      free (full_message_p);
    }

  return rval;
}

/*
 * fileio_get_backup_level_string () - return the string name of the backup level
 *   return: pointer to string containing name of level
 *   level(in): the backup level to convert
 */
static const char *
fileio_get_backup_level_string (FILEIO_BACKUP_LEVEL level)
{
  switch (level)
    {
    case FILEIO_BACKUP_FULL_LEVEL:
      return ("FULL LEVEL");

    case FILEIO_BACKUP_BIG_INCREMENT_LEVEL:
      return ("INCREMENTAL LEVEL 1");

    case FILEIO_BACKUP_SMALL_INCREMENT_LEVEL:
      return ("INCREMENTAL LEVEL 2");

    default:
      return ("UNKNOWN");
    }
}

/*
 * fileio_get_next_backup_volume () - FIND LOCATION OR NEW VOLUME NAME TO CONTINUE BACKUP
 *   return:
 *   session(in/out): The session array
 *   user_new(in): true if user must be involved in the switch
 *
 * Note: This routine halts output to the current backup volume and opens the
 *       next backup volume, creating it if it is a file. User interaction may
 *       be necessary in cases where the backup volume is really a device
 *       (i.e. because a tape must be mounted).  User interaction may also be
 *       required if the disk space is full on the current disk. We must
 *       insure that new location chosen is large enough.
 */
static int
fileio_get_next_backup_volume (THREAD_ENTRY * thread_p, FILEIO_BACKUP_SESSION * session_p, bool is_new_user)
{
  const char *db_nopath_name_p = NULL;
  char copy_name[PATH_MAX];
  char orig_name[PATH_MAX];
  char *message_area_p = NULL;
  char io_timeval[CTIME_MAX];

  if (session_p->bkup.dtype == FILEIO_BACKUP_VOL_DIRECTORY)
    {
      fileio_dismount (thread_p, session_p->bkup.vdes);
    }
  else
    {
      fileio_dismount_without_fsync (thread_p, session_p->bkup.vdes);
    }
  session_p->bkup.vdes = NULL_VOLDES;
  /* Always force a new one for devices */
  if (session_p->bkup.dtype == FILEIO_BACKUP_VOL_DEVICE)
    {
      is_new_user = true;
    }

  /* Keep the backup info correct */
  session_p->bkup.alltotalio += session_p->bkup.voltotalio;
  session_p->bkup.voltotalio = 0;
  /* Tell user that current backup volume just completed */
  fileio_ctime (&session_p->bkup.bkuphdr->start_time, io_timeval);
  if (asprintf (&message_area_p, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_IO, MSGCAT_FILEIO_BACKUP_LABEL_INFO),
		session_p->bkup.bkuphdr->level, session_p->bkup.bkuphdr->unit_num,
		fileio_get_base_file_name (session_p->bkup.bkuphdr->db_fullname), io_timeval) < 0)
    {
      /* Note: we do not know the exact malloc size that failed */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) FILEIO_MAX_USER_RESPONSE_SIZE);
      return ER_FAILED;
    }
  else
    {
      (void) fileio_request_user_response (thread_p, FILEIO_PROMPT_DISPLAY_ONLY, message_area_p, NULL, NULL, -1, -1,
					   NULL, -1);
      /* Note: Not free_and_init */
      free (message_area_p);
    }

  /* Some initializations for start of next backup volume. */
  session_p->bkup.bkuphdr->iopageid = FILEIO_BACKUP_VOL_CONT_PAGE_ID;
  session_p->bkup.bkuphdr->unit_num++;
  memset (session_p->bkup.bkuphdr->db_prec_bkvolname, 0, sizeof (session_p->bkup.bkuphdr->db_prec_bkvolname));
  strcpy (session_p->bkup.bkuphdr->db_prec_bkvolname, session_p->bkup.vlabel);
  memset (session_p->bkup.bkuphdr->db_next_bkvolname, 0, sizeof (session_p->bkup.bkuphdr->db_next_bkvolname));

  /* Guess new path name in same dir as current volume. For devices, just repeat the device name. */
  if (session_p->bkup.dtype == FILEIO_BACKUP_VOL_DIRECTORY)
    {
      /* First, get the path of the just finished volume. */
      fileio_get_directory_path (orig_name, session_p->bkup.name);
      strcpy (copy_name, orig_name);
      if (is_new_user)
	{
	  /* Fill in the expected volume name to show the user */
	  db_nopath_name_p = fileio_get_base_file_name (session_p->bkup.bkuphdr->db_fullname);
	  fileio_make_backup_name (session_p->bkup.name, db_nopath_name_p, orig_name, session_p->bkup.bkuphdr->level,
				   session_p->bkup.bkuphdr->unit_num + 1);
	  strcpy (copy_name, session_p->bkup.name);
	}
    }
  else
    {
      strcpy (copy_name, session_p->bkup.name);
      strcpy (orig_name, session_p->bkup.name);
    }

  do
    {
      if (is_new_user)
	{
	  if (fileio_find_restore_volume (thread_p, session_p->bkup.bkuphdr->db_fullname, copy_name,
					  session_p->bkup.bkuphdr->unit_num, session_p->dbfile.level,
					  MSGCAT_FILEIO_BKUP_FIND_REASON) == FILEIO_RELOCATION_QUIT)
	    {
	      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_CANNOT_ACCESS_BACKUP, 1, session_p->bkup.vlabel);
	      return ER_FAILED;
	    }
	}

      /* Append backup volume name onto directory. */
      if (session_p->bkup.dtype == FILEIO_BACKUP_VOL_DIRECTORY)
	{
	  db_nopath_name_p = fileio_get_base_file_name (session_p->bkup.bkuphdr->db_fullname);
	  fileio_make_backup_name (session_p->bkup.name, db_nopath_name_p, copy_name, session_p->dbfile.level,
				   session_p->bkup.bkuphdr->unit_num);
	}
      else
	{
	  strcpy (session_p->bkup.name, copy_name);
	}


      session_p->bkup.vlabel = session_p->bkup.name;
      /* Create the new volume and go on with our lives */
      session_p->bkup.vdes =
	fileio_create_backup_volume (thread_p, session_p->bkup.bkuphdr->db_fullname, session_p->bkup.vlabel,
				     LOG_DBCOPY_VOLID, false, false,
				     ((session_p->dbfile.level == FILEIO_BACKUP_FULL_LEVEL)
				      ? FILEIO_BACKUP_MINIMUM_NUM_PAGES_FULL_LEVEL : FILEIO_BACKUP_MINIMUM_NUM_PAGES));
      if (session_p->bkup.vdes < 0)
	{

	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IO_MOUNT_FAIL, 1, session_p->bkup.vlabel);
	  if (asprintf (&message_area_p, "%s\n", er_msg ()) < 0)
	    {
	      fileio_request_user_response (thread_p, FILEIO_PROMPT_DISPLAY_ONLY, message_area_p, NULL, NULL, -1, -1,
					    NULL, -1);
	      free (message_area_p);
	    }
	}

      is_new_user = true;
      /* reset to the original name until acceptable alternative is chosen */
      strcpy (copy_name, orig_name);
    }
  while (session_p->bkup.vdes == NULL_VOLDES);

  /* Remember name of new backup volume */
  if (fileio_add_volume_to_backup_info (session_p->bkup.name, session_p->dbfile.level,
					session_p->bkup.bkuphdr->unit_num, FILEIO_FIRST_BACKUP_VOL_INFO) != NO_ERROR)
    {
      return ER_FAILED;
    }

  return NO_ERROR;
}


/*
 * BKVINF RELATED FUNCTIONS
 *
 * These functions collectively manage the bkvinf file.  They are responsible
 * for reading, writing, and maintaining the in memory cache of backup
 * volume related information.
 */

/*
 * fileio_add_volume_to_backup_info () - Add a backup volume name to the internal cache
 *   return:
 *   name(in): Filename of backup volume
 *   level(in): backup level for that volume
 *   unit_num(in): unit (or sequence) number of that volume
 *   which_bkvinf(in):
 */
int
fileio_add_volume_to_backup_info (const char *name_p, FILEIO_BACKUP_LEVEL level, int unit_num, int which_bkvinf)
{
  FILEIO_BACKUP_INFO_ENTRY *node_p, *back_p;
  struct stat stbuf;
  char real_path_buf[PATH_MAX];
  FILEIO_BACKUP_INFO_QUEUE *data_p;

  data_p = &(fileio_Backup_vol_info_data[which_bkvinf]);
  if ((!(*data_p).initialized) && fileio_initialize_backup_info (which_bkvinf) != NO_ERROR)
    {
      return ER_FAILED;
    }

  /* Get a node */
  node_p = fileio_allocate_backup_info (which_bkvinf);
  if (node_p == NULL)
    {
      return ER_FAILED;
    }

  if (stat (name_p, &stbuf) != -1)
    {
      if (S_ISREG (stbuf.st_mode))
	{
	  /* for regular file, convert to the real-path */
	  if (realpath (name_p, real_path_buf) != NULL)
	    {
	      name_p = real_path_buf;
	    }
	}
    }

  if (snprintf (node_p->bkvol_name, PATH_MAX, "%s", name_p) >= PATH_MAX)
    {
      return ER_FAILED;
    }
  node_p->unit_num = unit_num;

  /* Put it on the queue for that level */
  if ((*data_p).anchors[level] == NULL)
    {
      /* this is the first one */
      (*data_p).anchors[level] = node_p;
    }
  else
    {
      /* Put it at the end of the chain */
      back_p = (*data_p).anchors[level];
      while (back_p->link != NULL)
	{
	  if (back_p->unit_num == unit_num)
	    {
	      er_log_debug (ARG_FILE_LINE, "bkvinf inconsistency, duplicate unit num %d found for level %d\n",
			    unit_num, level);
	    }
	  back_p = back_p->link;
	}
      /* check the last entry */
      if (back_p->unit_num == unit_num)
	{
	  er_log_debug (ARG_FILE_LINE, "bkvinf inconsistency, duplicate unit num %d found for level %d\n", unit_num,
			level);
	}

      back_p->link = node_p;
    }

  return NO_ERROR;
}

/*
 * fileio_write_backup_info_entries () - Prints internal bkvinf table in a format
 *                             suitable for the bkvinf file as well as human
 *                             consumption
 *   return:
 *   fp(in): Open file handle or else NULL for stdout
 *   which_bkvinf(in):
 */
int
fileio_write_backup_info_entries (FILE * fp, int which_bkvinf)
{
  FILEIO_BACKUP_INFO_ENTRY *node_p;
  int level, n;
  FILEIO_BACKUP_INFO_QUEUE *data_p;

  data_p = &(fileio_Backup_vol_info_data[which_bkvinf]);
  if (!fp)
    {
      fp = stdout;
    }

  for (level = FILEIO_BACKUP_FULL_LEVEL; level < FILEIO_BACKUP_UNDEFINED_LEVEL; level++)
    {
      for (node_p = (*data_p).anchors[level]; node_p != NULL; node_p = node_p->link)
	{
	  n = fprintf (fp, "%3d %d %s\n", level, node_p->unit_num, node_p->bkvol_name);
	  if (n <= 0)
	    {
	      return ER_FAILED;
	    }
	}
    }

  return NO_ERROR;
}

/*
 * fileio_read_backup_info_entries () - Read and parse the entries in a bkvinf file and
 *                            store them in the internal cache
 *   return:
 *   fp(in): Open file handle
 *   which_bkvinf(in):
 */
int
fileio_read_backup_info_entries (FILE * fp, int which_bkvinf)
{
  FILEIO_BACKUP_LEVEL level;
  int tmp, unit_num;
  char vol_name[PATH_MAX];
  int n, line = 0;
  char format_string[32];

  if (fp == NULL)
    {
      return ER_FAILED;
    }

  /* Always throw away old cache (if any) and then start fresh */
  fileio_finalize_backup_info (which_bkvinf);
  if (fileio_initialize_backup_info (which_bkvinf) != NO_ERROR)
    {
      return ER_FAILED;
    }

  sprintf (format_string, "%%d %%d %%%ds", PATH_MAX - 1);
  while ((n = fscanf (fp, format_string, &tmp, &unit_num, vol_name)) > 0)
    {
      level = (FILEIO_BACKUP_LEVEL) tmp;
      line++;
      if ((n != 3) || (level >= FILEIO_BACKUP_UNDEFINED_LEVEL)
	  /* || (level < FILEIO_BACKUP_FULL_LEVEL) */
	  || (unit_num < FILEIO_INITIAL_BACKUP_UNITS))
	{
	  fprintf (stdout, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_IO, MSGCAT_FILEIO_BACKUP_VINF_ERROR),
		   line);
	  return ER_FAILED;
	}

      /* remember this backup volume */
      if (fileio_add_volume_to_backup_info (vol_name, level, unit_num, which_bkvinf) != NO_ERROR)
	{
	  return ER_FAILED;
	}
    }

  return NO_ERROR;
}


/*
 * fileio_get_backup_info_volume_name () - Return the string of volume name
 *   return:
 *   level(in):
 *   unit_num(in):
 *   which_bkvinf(in):
 */
const char *
fileio_get_backup_info_volume_name (FILEIO_BACKUP_LEVEL level, int unit_num, int which_bkvinf)
{
  FILEIO_BACKUP_INFO_ENTRY *node_p;
  FILEIO_BACKUP_INFO_QUEUE *data_p;

  data_p = &(fileio_Backup_vol_info_data[which_bkvinf]);
  for (node_p = (*data_p).anchors[level]; node_p != NULL; node_p = node_p->link)
    {
      if (unit_num == node_p->unit_num)
	{
	  return (node_p->bkvol_name);
	}
    }

  return NULL;
}


/*
 * fileio_finalize_backup_info () - Clean up and free all resources related to the bkvinf
 *                      cache
 *   return: void
 *   which_bkvinf(in):
 */
void
fileio_finalize_backup_info (int which_bkvinf)
{
  FILEIO_BACKUP_INFO_QUEUE *data_p;

  data_p = &(fileio_Backup_vol_info_data[which_bkvinf]);
  if ((*data_p).initialized)
    {
      fileio_clear_backup_info_level (FILEIO_BACKUP_FULL_LEVEL, true, which_bkvinf);
      (*data_p).initialized = false;
    }
  return;
}

/*
 * fileio_initialize_backup_info () - Initialize the bkvinf cache and related structures
 *   return:
 *   which_bkvinf(in):
 *
 * Note: Be sure to call bkvinf_final when the backups are complete.
 */
static int
fileio_initialize_backup_info (int which_bkvinf)
{
  FILEIO_BACKUP_INFO_QUEUE *data_p;

#if defined(CUBRID_DEBUG)
  const char *env_value;
  /* This checking is done here so it can be in one location and because this routine is likely to be called before
   * backup/restore. */
  if (io_Bkuptrace_debug < 0)
    {
      /* Find out if user wants debugging info during backup/restore. */
      env_value = envvar_get ("IO_TRACE_BACKUP");
      io_Bkuptrace_debug = (env_value == NULL ? 0 : atoi (env_value));
    }
#endif /* CUBRID_DEBUG */

  data_p = &(fileio_Backup_vol_info_data[which_bkvinf]);
  if (!(*data_p).initialized)
    {
      (*data_p).free = NULL;
      memset ((*data_p).anchors, 0, sizeof ((*data_p).anchors));
      (*data_p).initialized = true;
    }

  return NO_ERROR;
}

/*
 * fileio_allocate_backup_info () - Allocate a bkvinf_entry
 *   return:  a pointer to the node allocated, or NULL if failure
 *   which_bkvinf(in):
 */
static FILEIO_BACKUP_INFO_ENTRY *
fileio_allocate_backup_info (int which_bkvinf)
{
  FILEIO_BACKUP_INFO_ENTRY *temp_entry_p;
  FILEIO_BACKUP_INFO_QUEUE *data_p;

  data_p = &(fileio_Backup_vol_info_data[which_bkvinf]);
  if (!(*data_p).initialized)
    {
      return NULL;
    }

  /* check free list */
  if ((*data_p).free != NULL)
    {
      temp_entry_p = (*data_p).free;
      (*data_p).free = temp_entry_p->link;
      temp_entry_p->link = NULL;
    }
  else
    {
      /* allocate one */
      temp_entry_p = ((FILEIO_BACKUP_INFO_ENTRY *) malloc (sizeof (FILEIO_BACKUP_INFO_ENTRY)));
      if (temp_entry_p != NULL)
	{
	  temp_entry_p->link = NULL;
	}
      else
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (FILEIO_BACKUP_INFO_ENTRY));
	}
    }

  return temp_entry_p;
}

/*
 * fileio_clear_backup_info_level () - REMOVES ALL VOLUMES OF A BACKUP LEVEL FROM CACHE
 *   return:
 *   level(in): backup level to remove
 *   dealloc(in): true if the nodes should be freed back to the OS
 *   which_bkvinf(in):
 *
 * Note: This routine removes all the nodes for a given level and any levels
 *       above it. Ordinarily the nodes will be placed on the free list for
 *       reuse, but if dealloc is true, then the space will be freed.
 *       Example:  IF level 1 is to be cleared, then level 2 is also cleared.
 */
int
fileio_clear_backup_info_level (int level, bool is_dealloc, int which_bkvinf)
{
  FILEIO_BACKUP_INFO_ENTRY *next_p;
  FILEIO_BACKUP_INFO_QUEUE *data_p;
  int i;

  data_p = &(fileio_Backup_vol_info_data[which_bkvinf]);
  if (level < 0)
    {
      level = FILEIO_BACKUP_FULL_LEVEL;
    }

  /* Delete from the front */
  for (i = FILEIO_BACKUP_UNDEFINED_LEVEL - 1; i >= level; i--)
    {
      while ((*data_p).anchors[i] != NULL)
	{
	  next_p = (*data_p).anchors[i]->link;
	  if (is_dealloc)
	    {
	      free_and_init ((*data_p).anchors[i]);
	    }
	  else
	    {
	      /* stick it on the free list */
	      (*data_p).anchors[i]->link = (*data_p).free;
	      (*data_p).free = (*data_p).anchors[i];
	    }
	  (*data_p).anchors[i] = next_p;
	}
    }

  /* Free the free list nodes if necessary to avoid leaks */
  if (is_dealloc)
    {
      while ((*data_p).free)
	{
	  next_p = (*data_p).free->link;
	  free_and_init ((*data_p).free);
	  (*data_p).free = next_p;
	}
    }

  return NO_ERROR;
}

/*
 * fileio_request_user_response () - REQUEST A RESPONSE VIA REMOVE CLIENT
 *   return:
 *   prompt_id(in):
 *   prompt(in):
 *   response(in):
 *   failure_prompt(in):
 *   range_low(in):
 *   range_high(in):
 *   secondary_prompt(in):
 *   reprompt_value(in):
 */
int
fileio_request_user_response (THREAD_ENTRY * thread_p, FILEIO_REMOTE_PROMPT_TYPE prompt_id, const char *prompt_p,
			      char *response_p, const char *failure_prompt_p, int range_low, int range_high,
			      const char *secondary_prompt_p, int reprompt_value)
{
#if defined(SERVER_MODE)
  char *remote_data_p = NULL;
  char *remote_answer_p = NULL;
  int data_size;
  int remote_status;
  char *ptr;

  /* Send the prompt to the client */
  if (xio_send_user_prompt_to_client (thread_p, prompt_id, prompt_p, failure_prompt_p, range_low, range_high,
				      secondary_prompt_p, reprompt_value) != NO_ERROR)
    {
      return ER_FAILED;
    }

  /* Obtain the user's response from the client, without blocking the server. */
  if (xs_receive_data_from_client (thread_p, &remote_data_p, &data_size) != NO_ERROR)
    {
      if (remote_data_p)
	{
	  thread_p->release_packet (remote_data_p);
	}

      return ER_FAILED;
    }

  ptr = or_unpack_int (remote_data_p, &remote_status);
  if (remote_status != NO_ERROR)
    {
      thread_p->release_packet (remote_data_p);
      return ER_FAILED;
    }
  data_size -= OR_INT_SIZE;
  if (response_p && data_size > 0)
    {
      /* Otherwise prompt appears successful */
      ptr = or_unpack_string_nocopy (ptr, &remote_answer_p);
      if (remote_answer_p != NULL)
	{
	  memcpy (response_p, remote_answer_p, intl_mbs_len (remote_answer_p) + 1);
	}
    }

  thread_p->release_packet (remote_data_p);
  return NO_ERROR;
#else /* SERVER_MODE */
  extern unsigned int db_on_server;

  char new_vol_name[FILEIO_MAX_USER_RESPONSE_SIZE];
  char *user_response_p = new_vol_name;
  const char *display_string_p;
  char line_buf[PATH_MAX * 2];
  int pr_status, pr_len;
  int x;
  int result = 0;
  bool is_retry_in = true;
  int rc;
  char format_string[32];

  /* we're pretending to jump to the client */
  db_on_server = 0;
  /* Interestingly enough, this is basically the same code as in the ASYNC_ callback has to do remotely. */
  display_string_p = prompt_p;
  memset (new_vol_name, 0, sizeof (new_vol_name));

  sprintf (format_string, "%%%ds", FILEIO_MAX_USER_RESPONSE_SIZE - 1);

  while (is_retry_in)
    {
      /* Display prompt, then get user's input. */
      fprintf (stdout, "%s", display_string_p);

      pr_status = ER_FAILED;
      pr_len = 0;
      is_retry_in = false;

      if (prompt_id != FILEIO_PROMPT_DISPLAY_ONLY)
	{
	  rc = -1;
	  if ((fgets (line_buf, PATH_MAX, stdin) != NULL)
	      && ((rc = sscanf (line_buf, format_string, user_response_p)) > 0))
	    {

	      /* Attempt basic input int validation before we send it back */
	      switch (prompt_id)
		{
		case FILEIO_PROMPT_RANGE_TYPE:
		  /* Numeric range checking */
		  result = parse_int (&x, user_response_p, 10);
		  if (result != 0 || x < range_low || x > range_high)
		    {
		      fprintf (stdout, "%s", failure_prompt_p);
		      is_retry_in = true;
		    }
		  else
		    {
		      pr_status = NO_ERROR;
		    }
		  break;

		  /* attempt simply boolean (y, yes, 1, n, no, 0) validation */
		case FILEIO_PROMPT_BOOLEAN_TYPE:
		  if (char_tolower (*user_response_p) == 'y' || *user_response_p == '1'
		      || intl_mbs_casecmp ((const char *) user_response_p, "yes") == 0)
		    {
		      pr_status = NO_ERROR;
		      /* convert all affirmate answers into '1' */
		      strcpy (user_response_p, "1");
		    }
		  else
		    {
		      pr_status = NO_ERROR;
		      /* convert all negative answers into '0' */
		      strcpy (user_response_p, "0");
		    }
		  break;

		  /* no validation to do */
		case FILEIO_PROMPT_STRING_TYPE:
		  pr_status = NO_ERROR;
		  break;

		  /* Validate initial prompt, then post secondary prompt */
		case FILEIO_PROMPT_RANGE_WITH_SECONDARY_STRING_TYPE:
		  /* Numeric range checking on the first promp, but user's answer we really want is the second prompt */
		  result = parse_int (&x, user_response_p, 10);
		  if (result != 0 || x < range_low || x > range_high)
		    {
		      fprintf (stdout, "%s", failure_prompt_p);
		      is_retry_in = true;
		    }
		  else if (x == reprompt_value)
		    {
		      /* The first answer requires another prompt */
		      display_string_p = secondary_prompt_p;
		      is_retry_in = true;
		      prompt_id = FILEIO_PROMPT_STRING_TYPE;
		      /* moving the response buffer ptr forward insures that both the first response and the second are
		       * included in the buffer. (no delimiter or null bytes) */
		      user_response_p += intl_mbs_len (user_response_p);
		    }
		  else
		    {
		      /* This answer was sufficient */
		      pr_status = NO_ERROR;
		    }

		  break;

		default:
		  /* should we treat this as an error? It is really a protocol error.  How do we handle backward
		   * compatibility for future releases? */
		  pr_status = NO_ERROR;
		}
	    }
	  else if (rc == 0)
	    {
	      is_retry_in = true;
	    }
	  else
	    {
	      /* EOF encountered, treat as an error */
	      return ER_FAILED;
	    }
	}
    }

  /* The answer can be returned now. It should be stored in new_vol_name */
  /* check for overflow, could be dangerous */
  pr_len = intl_mbs_len (new_vol_name);
  if (pr_len > FILEIO_MAX_USER_RESPONSE_SIZE)
    {
      pr_status = ER_FAILED;
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_NET_DATA_TRUNCATED, 0);
    }

  /* Copy the answer to the response buffer */
  if (response_p)
    {
      memcpy (response_p, new_vol_name, sizeof (new_vol_name));
    }

  db_on_server = 1;
  return (pr_status);
#endif /* SERVER_MODE */
}

/*
 * PAGE BITMAP FUNCTIONS
 */

/*
 * fileio_restore_format_unrestored_pages - format the pages of the restored volumes that no backup level restored
 *   return: error code
 *   page_bitmap_list(in): the pages each level restored, by volume
 *   failed_bitmap(out): the volume whose pages could not be formatted, on failure
 *
 * Note: an incremental backup holds only the pages changed since the backup of the level below it, and the full
 *       level fills the holes only of the volume as it was then. so a page of a volume added or extended after a
 *       lower level backup, and not written before the next one, is in no backup level, and the restore left it as
 *       the volume was created: zeros, not a formatted page. recovery could not fix such a page to redo its first
 *       changes. format these pages as the full level formats its holes, writing each run of them at once.
 */
int
fileio_restore_format_unrestored_pages (THREAD_ENTRY * thread_p, FILEIO_RESTORE_PAGE_BITMAP_LIST * page_bitmap_list,
					FILEIO_RESTORE_PAGE_BITMAP ** failed_bitmap)
{
  FILEIO_RESTORE_PAGE_BITMAP *page_bitmap;
  FILEIO_WRITE_MODE write_mode;
  char *format_pages = NULL;
  int max_run_pages = MAX (FILEIO_RESTORE_FORMAT_WRITE_SIZE / IO_PAGESIZE, 1);
  int vdes;
  int page_id, run_end, i;
  int error_code = NO_ERROR;

  *failed_bitmap = NULL;
  write_mode = dwb_is_created () == true ? FILEIO_WRITE_NO_COMPENSATE_WRITE : FILEIO_WRITE_DEFAULT_WRITE;

  for (page_bitmap = page_bitmap_list->head; page_bitmap != NULL; page_bitmap = page_bitmap->next)
    {
      for (page_id = 0; page_id < page_bitmap->npages; page_id++)
	{
	  if (!fileio_page_bitmap_is_set (page_bitmap, page_id))
	    {
	      break;
	    }
	}
      if (page_id >= page_bitmap->npages)
	{
	  /* every page was restored */
	  continue;
	}

      if (format_pages == NULL)
	{
	  format_pages = (char *) malloc ((size_t) max_run_pages * IO_PAGESIZE);
	  if (format_pages == NULL)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
		      (size_t) max_run_pages * IO_PAGESIZE);
	      *failed_bitmap = page_bitmap;
	      return ER_OUT_OF_VIRTUAL_MEMORY;
	    }
	  memset (format_pages, 0, (size_t) max_run_pages * IO_PAGESIZE);
	  for (i = 0; i < max_run_pages; i++)
	    {
	      (void) fileio_initialize_res (thread_p, (FILEIO_PAGE *) (format_pages + (size_t) i * IO_PAGESIZE),
					    IO_PAGESIZE);
	    }
	}

      vdes = fileio_mount (thread_p, NULL, page_bitmap->vol_label, page_bitmap->vol_id, false, false);
      if (vdes == NULL_VOLDES)
	{
	  ASSERT_ERROR_AND_SET (error_code);
	  *failed_bitmap = page_bitmap;
	  break;
	}

      while (page_id < page_bitmap->npages)
	{
	  /* the run of pages that no level restored, from page_id up to run_end */
	  for (run_end = page_id + 1; run_end < page_bitmap->npages && run_end - page_id < max_run_pages; run_end++)
	    {
	      if (fileio_page_bitmap_is_set (page_bitmap, run_end))
		{
		  break;
		}
	    }

	  if (fileio_write_pages (thread_p, vdes, format_pages, page_id, run_end - page_id, IO_PAGESIZE, write_mode)
	      == NULL)
	    {
	      ASSERT_ERROR_AND_SET (error_code);
	      *failed_bitmap = page_bitmap;
	      break;
	    }

	  for (page_id = run_end; page_id < page_bitmap->npages; page_id++)
	    {
	      if (!fileio_page_bitmap_is_set (page_bitmap, page_id))
		{
		  break;
		}
	    }
	}

      fileio_dismount (thread_p, vdes);
      if (error_code != NO_ERROR)
	{
	  break;
	}
    }

  if (format_pages != NULL)
    {
      free_and_init (format_pages);
    }

  return error_code;
}

/*
 * fileio_page_bitmap_list_init - initialize a page bitmap list
 *   return: void
 *   page_bitmap_list(in/out): head of the page bitmap list
 */
void
fileio_page_bitmap_list_init (FILEIO_RESTORE_PAGE_BITMAP_LIST * page_bitmap_list)
{
  assert (page_bitmap_list != NULL);
  page_bitmap_list->head = NULL;
  page_bitmap_list->tail = NULL;
}

/*
 * fileio_page_bitmap_create - create a page bitmap
 *   return: page bitmap
 *   vol_id(in): the number of the page bitmap identification
 *   total_pages(in): the number of total pages
 *   vol_label(in): the restored volume
 */
FILEIO_RESTORE_PAGE_BITMAP *
fileio_page_bitmap_create (int vol_id, int total_pages, const char *vol_label)
{
  FILEIO_RESTORE_PAGE_BITMAP *page_bitmap;
  int page_bitmap_size;

  page_bitmap = (FILEIO_RESTORE_PAGE_BITMAP *) malloc (sizeof (FILEIO_RESTORE_PAGE_BITMAP));
  if (page_bitmap == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (FILEIO_RESTORE_PAGE_BITMAP));
      return NULL;
    }

  page_bitmap_size = CEIL_PTVDIV (total_pages, 8);

  page_bitmap->next = NULL;
  page_bitmap->vol_id = vol_id;
  page_bitmap->size = page_bitmap_size;
  page_bitmap->npages = total_pages;
  strncpy_bufsize (page_bitmap->vol_label, vol_label);
  page_bitmap->bitmap = (unsigned char *) malloc (page_bitmap_size);
  if (page_bitmap->bitmap == NULL)
    {
      free_and_init (page_bitmap);

      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) page_bitmap_size);
      return NULL;
    }
  memset (page_bitmap->bitmap, 0x0, page_bitmap_size);

  return page_bitmap;
}

/*
 * fileio_page_bitmap_list_find - find the page bitmap which is matched
 *                                with vol_id
 *   return: pointer of the page bitmap or NULL
 *   page_bitmap_list(in): head of the page bitmap list
 *   vol_id(in): the number of the page bitmap identification
 */
FILEIO_RESTORE_PAGE_BITMAP *
fileio_page_bitmap_list_find (FILEIO_RESTORE_PAGE_BITMAP_LIST * page_bitmap_list, int vol_id)
{
  FILEIO_RESTORE_PAGE_BITMAP *page_bitmap;

  if (page_bitmap_list->head == NULL)
    {
      return NULL;
    }

  assert (page_bitmap_list->tail != NULL);

  page_bitmap = page_bitmap_list->head;

  while (page_bitmap != NULL)
    {
      if (page_bitmap->vol_id == vol_id)
	{
	  return page_bitmap;
	}
      page_bitmap = page_bitmap->next;
    }

  return NULL;
}

/*
 * fileio_page_bitmap_list_add - add a page bitmap to a page bitmap list
 *   page_bitmap_list(in/out): head of the page bitmap list
 *   page_bitmap(in): pointer of the page bitmap
 */
void
fileio_page_bitmap_list_add (FILEIO_RESTORE_PAGE_BITMAP_LIST * page_bitmap_list,
			     FILEIO_RESTORE_PAGE_BITMAP * page_bitmap)
{
#if !defined(NDEBUG)
  FILEIO_RESTORE_PAGE_BITMAP *bitmap;
#endif

  assert (page_bitmap_list != NULL);
  assert (page_bitmap != NULL);

#if !defined(NDEBUG)
  /* Check the uniqueness of vol_id */
  bitmap = page_bitmap_list->head;
  while (bitmap != NULL)
    {
      assert (bitmap->vol_id != page_bitmap->vol_id);
      bitmap = bitmap->next;
    }
#endif

  if (page_bitmap_list->head == NULL)
    {
      assert (page_bitmap_list->tail == NULL);

      page_bitmap_list->head = page_bitmap;
      page_bitmap_list->tail = page_bitmap;
    }
  else
    {
      assert (page_bitmap_list->tail != NULL);

      page_bitmap_list->tail->next = page_bitmap;
      page_bitmap_list->tail = page_bitmap;
    }
}

/*
 * fileio_page_bitmap_list_destroy - destroy a page bitmap list
 *   return: void
 *   page_bitmap_list(in/out): head of the page bitmap list
 */
void
fileio_page_bitmap_list_destroy (FILEIO_RESTORE_PAGE_BITMAP_LIST * page_bitmap_list)
{
  FILEIO_RESTORE_PAGE_BITMAP *page_bitmap = NULL;
  FILEIO_RESTORE_PAGE_BITMAP *page_bitmap_next = NULL;

  assert (page_bitmap_list != NULL);

  page_bitmap = page_bitmap_list->head;

  while (page_bitmap != NULL)
    {
      page_bitmap_next = page_bitmap->next;

      page_bitmap->vol_id = 0;
      page_bitmap->size = 0;
      free_and_init (page_bitmap->bitmap);
      free_and_init (page_bitmap);

      page_bitmap = page_bitmap_next;
    }
  page_bitmap_list->head = NULL;
  page_bitmap_list->tail = NULL;
}

/*
 * fileio_page_bitmap_set - set the bit that represents the exitence of the page
 *   return: void
 *   page_bitmap(in): pointer of the page bitmap
 *   page_id(in): position of the page
 */
static void
fileio_page_bitmap_set (FILEIO_RESTORE_PAGE_BITMAP * page_bitmap, int page_id)
{
  assert (page_bitmap != NULL);
  assert ((page_bitmap->size - 1) >= (page_id / 8));

  page_bitmap->bitmap[page_id / 8] |= 1 << (page_id % 8);
}

/*
 * fileio_page_bitmap_is_set - get the bit that represents the exitence of the page
 *   return: if the bit of page is set then it returns true.
 *   page_bitmap(in): pointer of the page bitmap
 *   page_id(in): position of the page
 */
static bool
fileio_page_bitmap_is_set (FILEIO_RESTORE_PAGE_BITMAP * page_bitmap, int page_id)
{
  bool is_set;

  assert (page_bitmap != NULL);
  assert ((page_bitmap->size - 1) >= (page_id / 8));

  is_set = page_bitmap->bitmap[page_id / 8] & (1 << (page_id % 8)) ? true : false;

  return is_set;
}

/*
 * fileio_page_bitmap_dump - dump a page bitmap
 *   return: void
 *   out_fp(in): FILE stream where to dump; if NULL, stdout
 *   page_bitmap(in): pointer of the page bitmap
 */
static void
fileio_page_bitmap_dump (FILE * out_fp, const FILEIO_RESTORE_PAGE_BITMAP * page_bitmap)
{
  int i;

  assert (page_bitmap != NULL);

  if (out_fp == NULL)
    {
      out_fp = stdout;
    }

  fprintf (out_fp, "BITMAP_ID = %d, BITMAP_SIZE = %d\n", page_bitmap->vol_id, page_bitmap->size);

  for (i = 0; i < page_bitmap->size; i++)
    {
      if ((i % 32) == 0)
	{
	  fprintf (out_fp, "%#08X: ", i);
	}
      else
	{
	  fprintf (out_fp, "%02X ", page_bitmap->bitmap[i]);
	}

      if ((i % 32) == 31)
	{
	  fprintf (out_fp, "\n");
	}
    }
  fprintf (out_fp, "\n");
}

#endif /* defined (SERVER_MODE) || (SA_MODE) */
