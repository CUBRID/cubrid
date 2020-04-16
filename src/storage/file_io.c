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
 * file_io.c - input/output module (at server)
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
#include <share.h>
#else /* WINDOWS */
#include <unistd.h>
#include <sys/time.h>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/vfs.h>
#if defined (SERVER_MODE)
#include <syslog.h>
#endif
#endif /* WINDOWS */

#ifdef _AIX
#include <sys/statfs.h>
#endif /* _AIX */

#if defined(SOLARIS)
#include <sys/statvfs.h>
#include <netdb.h>
#endif /* SOLARIS */

#if defined(HPUX)
#include <sys/scsi.h>
#include <aio.h>
#endif /* HPUX */

#if defined(USE_AIO)
#include <aio.h>
#endif /* USE_AIO */

#include "porting.h"

#include "chartype.h"
#include "file_io.h"
#include "storage_common.h"
#include "memory_alloc.h"
#include "error_manager.h"
#include "critical_section.h"
#include "system_parameter.h"
#include "message_catalog.h"
#include "util_func.h"
#include "perf_monitor.h"
#include "environment_variable.h"
#include "page_buffer.h"
#include "connection_error.h"
#include "release_string.h"
#include "xserver_interface.h"
#include "log_manager.h"
#include "perf_monitor.h"
#include "fault_injection.h"

#if defined(WINDOWS)
#include "wintcp.h"
#endif /* WINDOWS */

#if defined(SERVER_MODE)
#include "connection_error.h"
#include "network_interface_sr.h"
#include "job_queue.h"
#endif /* SERVER_MODE */

#include "intl_support.h"
#include "tsc_timer.h"


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

#ifdef L_cuserid
#define FILEIO_USER_NAME_SIZE L_cuserid
#else /* L_cuserid */
#define FILEIO_USER_NAME_SIZE 9
#endif /* L_cuserid */

#if defined(WINDOWS)
#define GETPID()  GetCurrentProcessId()
#else /* WINDOWS */
#define GETPID()  getpid()
#endif /* WINDOWS */

#define FILEIO_DISK_FORMAT_MODE            (O_RDWR | O_CREAT)
#define FILEIO_DISK_PROTECTION_MODE        0600
#define FILEIO_MAX_WAIT_DBTXT              300
#define FILEIO_FULL_LEVEL_EXP              32

/*
 * Define a fixed size for backup and restore input/output of the volume
 * headers.  For most modern devices multiples of 512 or 1024 are needed.
 * A size of the header data is computed in compile.
 */
#define GET_NEXT_1K_SIZE(s)             (((((s) - 1) / 1024) + 1) * 1024)
#define FILEIO_BACKUP_HEADER_IO_SIZE    GET_NEXT_1K_SIZE(sizeof(FILEIO_BACKUP_HEADER))
#define FILEIO_GET_FILE_SIZE(pagesize, npages)  \
  (((off_t)(pagesize)) * ((off_t)(npages)))

#define FILEIO_BACKUP_NO_ZIP_HEADER_VERSION        1
#define FILEIO_BACKUP_CURRENT_HEADER_VERSION       2
#define FILEIO_CHECK_FOR_INTERRUPT_INTERVAL       100

#define FILEIO_PAGE_SIZE_FULL_LEVEL (IO_PAGESIZE * FILEIO_FULL_LEVEL_EXP)
#define FILEIO_BACKUP_PAGE_OVERHEAD \
  (offsetof(FILEIO_BACKUP_PAGE, iopage) + sizeof(PAGEID))
#define FILEIO_BACKUP_DBVOLS_IO_PAGE_SIZE \
  (IO_PAGESIZE + FILEIO_BACKUP_PAGE_OVERHEAD)
#define FILEIO_BACKUP_DBVOLS_IO_PAGE_SIZE_FULL_LEVEL \
  (FILEIO_PAGE_SIZE_FULL_LEVEL + FILEIO_BACKUP_PAGE_OVERHEAD)

#define FILEIO_RESTORE_DBVOLS_IO_PAGE_SIZE(sess)  \
  ((sess)->bkup.bkuphdr->bkpagesize + FILEIO_BACKUP_PAGE_OVERHEAD)

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

#define FILEIO_CHECK_AND_INITIALIZE_VOLUME_HEADER_CACHE(rtn) \
  do { \
    if (fileio_Vol_info_header.volinfo == NULL \
        && fileio_initialize_volume_info_cache () < 0) \
      return (rtn); \
  } while (0)

/* Some specifications of page identifiers of backup */
#define FILEIO_BACKUP_START_PAGE_ID      (-2)
#define FILEIO_BACKUP_END_PAGE_ID        (-3)
#define FILEIO_BACKUP_FILE_START_PAGE_ID (-4)
#define FILEIO_BACKUP_FILE_END_PAGE_ID   (-5)
#define FILEIO_BACKUP_VOL_CONT_PAGE_ID   (-6)

#define FILEIO_END_OF_FILE                (1)

/* Minimum flush rate 40MB/s */
#define FILEIO_MIN_FLUSH_PAGES_PER_SEC    (41943040 / IO_PAGESIZE)
/* TODO: Growth/drop flush rate values can be tweaked. They have been set to
 * meet the needs of stressful workload. They have been set to drop slowly and
 * grow back quickly.
 * Please consider that token consumption depend on two factors:
 * 1. System IO capabilities.
 * 2. Flush thinking time.
 * If flush thinking time prevents it from consuming tokens, we might reduce
 * the numbers of token for next iterations unwillingly. A fast drop rate and
 * slow growth rate can make it impossible to recover from a "missed"
 * iteration (e.g. flush has been blocked on AIN list mutex).
 */
/* Rate of growing flush rate when tokens are consumed. */
#define FILEIO_PAGE_FLUSH_GROW_RATE            0.5
/* Rate of reducing flush rate when tokens are not consumed. */
#define FILEIO_PAGE_FLUSH_DROP_RATE	       0.1

#if defined(WINDOWS)
#define fileio_lock_file_write(fd, offset, whence, len) \
  fileio_lock_region(fd, F_TLOCK, offset, len)
#define fileio_lock_file_writew(fd, offset, whence, len) \
  fileio_lock_region(fd, F_LOCK, offset, len)
#define fileio_lock_file_read(fd, offset, whence, len) \
  fileio_lock_region(fd, F_TLOCK, offset, len)
#define fileio_lock_file_readw(fd, offset, whence, len) \
  fileio_lock_region(fd, F_LOCK, offset, len)
#define fileio_unlock_file(fd, offset, whence, len) \
  fileio_lock_region(fd, F_ULOCK, offset, len)
#else /* WINDOWS */
#define fileio_lock_file_read(fd, offset, whence, len) \
  fileio_lock_region(fd, F_SETLK, F_RDLCK, offset, whence, len)
#define fileio_lock_file_readw(fd, offset, whence, len) \
  fileio_lock_region(fd, F_SETLKW, F_RDLCK, offset, whence, len)
#define fileio_lock_file_write(fd, offset, whence, len) \
  fileio_lock_region(fd, F_SETLK, F_WRLCK, offset, whence, len)
#define fileio_lock_file_writew(fd, offset, whence, len) \
  fileio_lock_region(fd, F_SETLKW, F_WRLCK, offset, whence, len)
#define fileio_unlock_file(fd, offset, whence, len) \
  fileio_lock_region(fd, F_SETLK, F_UNLCK, offset, whence, len)
#endif /* WINDOWS */

#define FILEIO_VOLINFO_INCREMENT        32

#if !defined(SERVER_MODE)
#define pthread_mutex_init(a, b)
#define pthread_mutex_destroy(a)
#define pthread_mutex_lock(a)	0
#define pthread_mutex_unlock(a)
static int rv;
#endif

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
typedef struct fileio_sys_volinfo FILEIO_SYSTEM_VOLUME_INFO;
typedef struct fileio_sys_volinfo_header FILEIO_SYSTEM_VOLUME_HEADER;
typedef struct fileio_volinfo FILEIO_VOLUME_INFO;
typedef struct fileio_volinfo_header FILEIO_VOLUME_HEADER;

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

/* Volume information structure for system volumes(volid < NULL_VOLID) */
struct fileio_sys_volinfo
{
  VOLID volid;
  int vdes;
  FILEIO_LOCKF_TYPE lockf_type;
  char vlabel[PATH_MAX];
#if defined(SERVER_MODE) && defined(WINDOWS)
  pthread_mutex_t sysvol_mutex;
#endif				/* SERVER_MODE && WINDOWS */
  FILEIO_SYSTEM_VOLUME_INFO *next;
};

/* System volume informations are linked as a list */
struct fileio_sys_volinfo_header
{
#if defined(SERVER_MODE)
  pthread_mutex_t mutex;
#endif				/* SERVER_MODE */
  int num_vols;
  FILEIO_SYSTEM_VOLUME_INFO anchor;
};

/* Volume information structure for perm/temp volumes */
struct fileio_volinfo
{
  VOLID volid;
  int vdes;
  FILEIO_LOCKF_TYPE lockf_type;
#if defined(SERVER_MODE) && defined(WINDOWS)
  pthread_mutex_t vol_mutex;	/* for fileio_read()/fileio_write() */
#endif				/* SERVER_MODE && WINDOWS */
  char vlabel[PATH_MAX];
};

typedef union fileio_apply_function_arg
{
  int vol_id;
  int vdes;
  const char *vol_label;
} APPLY_ARG;

/* Perm/temp volume informations are stored on array.
 * Direct access by volid is possible */
struct fileio_volinfo_header
{
#if defined(SERVER_MODE)
  pthread_mutex_t mutex;
#endif				/* SERVER_MODE */
  int max_perm_vols;		/* # of max. io_volinfo entries for perm. vol */
  int next_perm_volid;		/* # of used io_volinfo entries for perm. vol */
  int max_temp_vols;		/* # of max. io_volinfo entries for temp. vol */
  int next_temp_volid;		/* # of used io_volinfo entries for temp. vol */
  /* if volid of volume is equal to this value, */
  /* it is temp. volume */
  int num_volinfo_array;	/* # of io_volinfo entry chunks */
  FILEIO_VOLUME_INFO **volinfo;	/* array of pointer for io_volinfo chunks */
};

typedef bool (*VOLINFO_APPLY_FN) (THREAD_ENTRY * thread_p, FILEIO_VOLUME_INFO * vol_info_p, APPLY_ARG * arg);
typedef bool (*SYS_VOLINFO_APPLY_FN) (THREAD_ENTRY * thread_p, FILEIO_SYSTEM_VOLUME_INFO * sys_vol_info_p,
				      APPLY_ARG * arg);

static FILEIO_SYSTEM_VOLUME_HEADER fileio_Sys_vol_info_header = {
#if defined(SERVER_MODE)
  PTHREAD_MUTEX_INITIALIZER,
#endif /* SERVER_MODE */
  0,
  {
   NULL_VOLID, NULL_VOLDES, FILEIO_NOT_LOCKF, "",
#if defined(SERVER_MODE) && defined(WINDOWS)
   PTHREAD_MUTEX_INITIALIZER,
#endif /* SERVER_MODE && WINDOWS */
   NULL}
};

static FILEIO_VOLUME_HEADER fileio_Vol_info_header = {
#if defined(SERVER_MODE)
  PTHREAD_MUTEX_INITIALIZER,
#endif /* SERVER_MODE */
  0, 0, 0, LOG_MAX_DBVOLID, 0, NULL
};

/* Records information from the bkvinf file about backup volumes */
static FILEIO_BACKUP_INFO_QUEUE fileio_Backup_vol_info_data[2] =
  { {false, {NULL, NULL, NULL}, NULL}, {false, {NULL, NULL, NULL}, NULL} };

/* Flush Control */
#if !defined(HAVE_ATOMIC_BUILTINS)
static pthread_mutex_t fileio_Flushed_page_counter_mutex = PTHREAD_MUTEX_INITIALIZER;
#endif
static int fileio_Flushed_page_count = 0;

static TOKEN_BUCKET fc_Token_bucket_s;
static TOKEN_BUCKET *fc_Token_bucket = NULL;
static FLUSH_STATS fc_Stats;

#if defined(CUBRID_DEBUG)
/* Set this to get various levels of io information regarding
 * backup and restore activity.
 * 0         :: no output
 * 1         :: print names and sizes of volumes that are backed-up.
 * 2         :: dump page bitmaps after volume is restored.
 */
static int io_Bkuptrace_debug = -1;
#endif /* CUBRID_DEBUG */

#if defined(SERVER_MODE) && defined(WINDOWS)
static pthread_mutex_t *fileio_get_volume_mutex (THREAD_ENTRY * thread_p, int vdes);
#endif
static int fileio_initialize_volume_info_cache (void);
static void fileio_make_volume_lock_name (char *vol_lockname, const char *vol_fullname);
static int fileio_create (THREAD_ENTRY * thread_p, const char *db_fullname, const char *vlabel, VOLID volid,
			  bool dolock, bool dosync);
static int fileio_create_backup_volume (THREAD_ENTRY * thread_p, const char *db_fullname, const char *vlabel,
					VOLID volid, bool dolock, bool dosync, int atleast_pages);
static void fileio_dismount_without_fsync (THREAD_ENTRY * thread_p, int vdes);
static int fileio_max_permanent_volumes (int index, int num_permanent_volums);
static int fileio_min_temporary_volumes (int index, int num_temp_volums, int num_volinfo_array);
static FILEIO_SYSTEM_VOLUME_INFO *fileio_traverse_system_volume (THREAD_ENTRY * thread_p,
								 SYS_VOLINFO_APPLY_FN apply_function, APPLY_ARG * arg);
static FILEIO_VOLUME_INFO *fileio_traverse_permanent_volume (THREAD_ENTRY * thread_p, VOLINFO_APPLY_FN apply_function,
							     APPLY_ARG * arg);
static FILEIO_VOLUME_INFO *fileio_reverse_traverse_permanent_volume (THREAD_ENTRY * thread_p,
								     VOLINFO_APPLY_FN apply_function, APPLY_ARG * arg);
static FILEIO_VOLUME_INFO *fileio_traverse_temporary_volume (THREAD_ENTRY * thread_p, VOLINFO_APPLY_FN apply_function,
							     APPLY_ARG * arg);
static FILEIO_VOLUME_INFO *fileio_reverse_traverse_temporary_volume (THREAD_ENTRY * thread_p,
								     VOLINFO_APPLY_FN apply_function, APPLY_ARG * arg);

static bool fileio_dismount_volume (THREAD_ENTRY * thread_p, FILEIO_VOLUME_INFO * vol_info_p, APPLY_ARG * ignore_arg);
static bool fileio_is_volume_descriptor_equal (THREAD_ENTRY * thread_p, FILEIO_VOLUME_INFO * vol_info_p,
					       APPLY_ARG * arg);
static bool fileio_is_volume_id_gt (THREAD_ENTRY * thread_p, FILEIO_VOLUME_INFO * vol_info_p, APPLY_ARG * arg);
static bool fileio_is_volume_id_lt (THREAD_ENTRY * thread_p, FILEIO_VOLUME_INFO * vol_info_p, APPLY_ARG * arg);
static FILEIO_SYSTEM_VOLUME_INFO *fileio_find_system_volume (THREAD_ENTRY * thread_p,
							     SYS_VOLINFO_APPLY_FN apply_function, APPLY_ARG * arg);
static bool fileio_is_system_volume_descriptor_equal (THREAD_ENTRY * thread_p,
						      FILEIO_SYSTEM_VOLUME_INFO * sys_vol_info_p, APPLY_ARG * arg);
static bool fileio_is_system_volume_id_equal (THREAD_ENTRY * thread_p, FILEIO_SYSTEM_VOLUME_INFO * sys_vol_info_p,
					      APPLY_ARG * arg);
static bool fileio_is_system_volume_label_equal (THREAD_ENTRY * thread_p, FILEIO_SYSTEM_VOLUME_INFO * sys_vol_info_p,
						 APPLY_ARG * arg);
static bool fileio_synchronize_sys_volume (THREAD_ENTRY * thread_p, FILEIO_SYSTEM_VOLUME_INFO * vol_sys_info_p,
					   APPLY_ARG * arg);
static bool fileio_synchronize_volume (THREAD_ENTRY * thread_p, FILEIO_VOLUME_INFO * vol_info_p, APPLY_ARG * arg);
static int fileio_cache (VOLID volid, const char *vlabel, int vdes, FILEIO_LOCKF_TYPE lockf_type);
static void fileio_decache (THREAD_ENTRY * thread_p, int vdes);
static VOLID fileio_get_volume_id (int vdes);
static bool fileio_is_volume_label_equal (THREAD_ENTRY * thread_p, FILEIO_VOLUME_INFO * vol_info_p, APPLY_ARG * arg);
static int fileio_expand_permanent_volume_info (FILEIO_VOLUME_HEADER * header, int volid);
static int fileio_expand_temporary_volume_info (FILEIO_VOLUME_HEADER * header, int volid);
static bool fileio_is_terminated_process (int pid);

#if !defined(WINDOWS)
static FILEIO_LOCKF_TYPE fileio_lock (const char *db_fullname, const char *vlabel, int vdes, bool dowait);
static void fileio_unlock (const char *vlabel, int vdes, FILEIO_LOCKF_TYPE lockf_type);
static FILEIO_LOCKF_TYPE fileio_get_lockf_type (int vdes);
#endif /* !WINDOWS */

static int fileio_get_primitive_way_max (const char *path, long int *filename_max, long int *pathname_max);
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
				   void *io_pgptr, VOLID vol_id, PAGEID page_id, FILEIO_BACKUP_LEVEL level);
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
static char *fileio_ctime (INT64 * clock, char *buf);
static const char *fileio_get_backup_level_string (FILEIO_BACKUP_LEVEL level);

static int fileio_initialize_backup_thread (FILEIO_BACKUP_SESSION * session_p, int num_threads);
static void fileio_finalize_backup_thread (FILEIO_BACKUP_SESSION * session_p, FILEIO_ZIP_METHOD zip_method);
static int fileio_write_backup_end_time_to_header (FILEIO_BACKUP_SESSION * session_p, INT64 end_time);
static void fileio_write_backup_end_time_to_last_page (FILEIO_BACKUP_SESSION * session_p, INT64 end_time);
static void fileio_read_backup_end_time_from_last_page (FILEIO_BACKUP_SESSION * session_p);

#if !defined(WINDOWS)
static int fileio_get_lock (int fd, const char *vlabel);
static int fileio_release_lock (int fd);
static int fileio_lock_region (int fd, int cmd, int type, off_t offset, int whence, off_t len);
#endif /* !WINDOWS */

#if defined(SERVER_MODE)
static void fileio_read_backup_volume (THREAD_ENTRY * thread_p, FILEIO_BACKUP_SESSION * session);
static FILEIO_TYPE fileio_write_backup_volume (THREAD_ENTRY * thread_p, FILEIO_BACKUP_SESSION * session);
static FILEIO_NODE *fileio_append_queue (FILEIO_QUEUE * qp, FILEIO_NODE * node);
#endif /* SERVER_MODE */

static void fileio_compensate_flush (THREAD_ENTRY * thread_p, int fd, int npage);
static int fileio_increase_flushed_page_count (int npages);
static int fileio_flush_control_get_token (THREAD_ENTRY * thread_p, int ntoken);
static int fileio_flush_control_get_desired_rate (TOKEN_BUCKET * tb);
static int fileio_synchronize_bg_archive_volume (THREAD_ENTRY * thread_p);

static void fileio_page_bitmap_set (FILEIO_RESTORE_PAGE_BITMAP * page_bitmap, int page_id);
static bool fileio_page_bitmap_is_set (FILEIO_RESTORE_PAGE_BITMAP * page_bitmap, int page_id);
static void fileio_page_bitmap_dump (FILE * out_fp, const FILEIO_RESTORE_PAGE_BITMAP * page_bitmap);

static int
fileio_increase_flushed_page_count (int npages)
{
  int flushed_page_count;

#if defined(HAVE_ATOMIC_BUILTINS)
  flushed_page_count = ATOMIC_INC_32 (&fileio_Flushed_page_count, npages);
#else
  (void) pthread_mutex_lock (&fileio_Flushed_page_counter_mutex);
  fileio_Flushed_page_count += npages;
  flushed_page_count = fileio_Flushed_page_count;
  pthread_mutex_unlock (&fileio_Flushed_page_counter_mutex);
#endif /* HAVE_ATOMIC_BUILTINS */

  return flushed_page_count;
}

static void
fileio_compensate_flush (THREAD_ENTRY * thread_p, int fd, int npage)
{
#if !defined(SERVER_MODE)
  return;
#else
  int rv;
  bool need_sync;
  int flushed_page_count;

  assert (npage > 0);

  if (npage <= 0)
    {
      return;
    }

  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
    }

  rv = fileio_flush_control_get_token (thread_p, npage);
  if (rv != NO_ERROR)
    {
      return;
    }

  need_sync = false;

  flushed_page_count = fileio_increase_flushed_page_count (npage);
  if (flushed_page_count > prm_get_integer_value (PRM_ID_PB_SYNC_ON_NFLUSH))
    {
      need_sync = true;
      fileio_Flushed_page_count = 0;
    }

  if (need_sync)
    {
      fileio_synchronize_all (thread_p, false);
    }
#endif /* SERVER_MODE */
}


/*
 * fileio_flush_control_initialize():
 *
 *   returns:
 *
 * Note:
 */
int
fileio_flush_control_initialize (void)
{
#if !defined(SERVER_MODE)
  return NO_ERROR;
#else
  TOKEN_BUCKET *tb;
  int rv = NO_ERROR;

  assert (fc_Token_bucket == NULL);
  tb = &fc_Token_bucket_s;

  rv = pthread_mutex_init (&tb->token_mutex, NULL);
  if (rv != NO_ERROR)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_PTHREAD_MUTEX_INIT, 0);
      return ER_CSS_PTHREAD_MUTEX_INIT;
    }
  tb->tokens = 0;
  tb->token_consumed = 0;

  rv = pthread_cond_init (&tb->waiter_cond, NULL);
  if (rv != 0)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_PTHREAD_COND_INIT, 0);
      return ER_CSS_PTHREAD_COND_INIT;
    }

  fc_Stats.num_tokens = 0;
  fc_Stats.num_log_pages = 0;
  fc_Stats.num_pages = 0;

  fc_Token_bucket = tb;
  return rv;
#endif
}

/*
 * fileio_flush_control_finalize():
 *
 *   returns:
 *
 * Note:
 */
void
fileio_flush_control_finalize (void)
{
#if !defined(SERVER_MODE)
  return;
#else
  TOKEN_BUCKET *tb;

  assert (fc_Token_bucket != NULL);
  if (fc_Token_bucket == NULL)
    {
      return;
    }

  tb = fc_Token_bucket;
  fc_Token_bucket = NULL;

  (void) pthread_mutex_destroy (&tb->token_mutex);
  (void) pthread_cond_destroy (&tb->waiter_cond);
#endif
}

/*
 * fileio_flush_control_get_token():
 *
 *   returns:
 *
 * Note:
 */
static int
fileio_flush_control_get_token (THREAD_ENTRY * thread_p, int ntoken)
{
#if !defined(SERVER_MODE)
  return NO_ERROR;
#else
  TOKEN_BUCKET *tb = fc_Token_bucket;
  int rv = NO_ERROR;
  int retry_count = 0;
  int nreq;
  bool log_cs_own = false;

  PERF_UTIME_TRACKER time_tracker = PERF_UTIME_TRACKER_INITIALIZER;

  if (tb == NULL)
    {
      return NO_ERROR;
    }

  assert (ntoken > 0);

  if (LOG_CS_OWN (thread_p))
    {
      log_cs_own = true;
    }

  nreq = ntoken;
  while (nreq > 0 && retry_count < 10)
    {
      /* try to get a token from share tokens */
      rv = pthread_mutex_lock (&tb->token_mutex);
      assert (rv == NO_ERROR);

      if (log_cs_own == true)
	{
	  fc_Stats.num_log_pages += nreq;
	}
      else
	{
	  fc_Stats.num_pages += nreq;
	}

      if (tb->tokens >= nreq)
	{
	  tb->tokens -= nreq;
	  tb->token_consumed += nreq;
	  pthread_mutex_unlock (&tb->token_mutex);
	  return NO_ERROR;
	}
      else if (tb->tokens > 0)
	{
	  nreq -= tb->tokens;
	  tb->token_consumed += tb->tokens;
	  tb->tokens = 0;
	}

      assert (nreq > 0);

      if (log_cs_own == true)
	{
	  pthread_mutex_unlock (&tb->token_mutex);
	  return NO_ERROR;
	}

      PERF_UTIME_TRACKER_START (thread_p, &time_tracker);

      /* Wait for signal */
      rv = pthread_cond_wait (&tb->waiter_cond, &tb->token_mutex);

      pthread_mutex_unlock (&tb->token_mutex);
      retry_count++;

      PERF_UTIME_TRACKER_BULK_TIME (thread_p, &time_tracker, PSTAT_PB_COMPENSATE_FLUSH, nreq);
    }

  /* I am very very unlucky (unlikely to happen) */
  er_log_debug (ARG_FILE_LINE, "Failed to get token within %d trial (req=%d, remained=%d)", retry_count, ntoken, nreq);
  return NO_ERROR;
#endif
}

/*
 * fileio_flush_control_add_tokens():
 *
 *   returns:
 *
 * Note:
 */
int
fileio_flush_control_add_tokens (THREAD_ENTRY * thread_p, INT64 diff_usec, int *token_gen, int *token_consumed)
{
#if !defined(SERVER_MODE)
  return NO_ERROR;
#else
  TOKEN_BUCKET *tb = fc_Token_bucket;
  int gen_tokens;
  int rv = NO_ERROR;

  assert (token_gen != NULL);

  if (tb == NULL)
    {
      return NO_ERROR;
    }

  /* add remaining tokens to shared tokens */
  rv = pthread_mutex_lock (&tb->token_mutex);

  *token_consumed = tb->token_consumed;
  tb->token_consumed = 0;

  perfmon_add_stat (thread_p, PSTAT_FC_NUM_PAGES, fc_Stats.num_pages);
  perfmon_add_stat (thread_p, PSTAT_FC_NUM_LOG_PAGES, fc_Stats.num_log_pages);
  perfmon_add_stat (thread_p, PSTAT_FC_TOKENS, fc_Stats.num_tokens);


  if (prm_get_bool_value (PRM_ID_ADAPTIVE_FLUSH_CONTROL) == true)
    {
      /* Get desired rate from evaluating changes in last iteration. */
      gen_tokens = fileio_flush_control_get_desired_rate (tb);
      /* Check new rate is not below minimum required. */
      gen_tokens = (int) MAX (gen_tokens, (double) FILEIO_MIN_FLUSH_PAGES_PER_SEC * (double) diff_usec / 1000000.0);
    }
  else
    {
      /* Always set maximum rate. */
      gen_tokens = (int) (prm_get_integer_value (PRM_ID_MAX_FLUSH_PAGES_PER_SECOND) * (double) diff_usec / 1000000.0);
    }

  *token_gen = gen_tokens;

  /* initialization statistics */
  fc_Stats.num_pages = 0;
  fc_Stats.num_log_pages = 0;
  fc_Stats.num_tokens = gen_tokens;

  tb->tokens = gen_tokens;

  /* signal to waiters */
  pthread_cond_broadcast (&tb->waiter_cond);
  pthread_mutex_unlock (&tb->token_mutex);
  return rv;

#endif
}

/*
 * fileio_flush_control_get_desired_rate () -
 *
 */
static int
fileio_flush_control_get_desired_rate (TOKEN_BUCKET * tb)
{
#if !defined (SERVER_MODE)
  return 0;
#else
  int dirty_rate = pgbuf_flush_control_from_dirty_ratio ();
  int adjust_rate = fc_Stats.num_tokens;	/* Start with previous rate. */

  if (tb->tokens > 0)
    {
      if (dirty_rate > 0)
	{
	  /* This is difficult situation. We did not consume all tokens but dirty rate goes up. Let's keep the number
	   * of tokens until dirty rate is no longer an issue. */
	}
      else
	{
	  /* Do not drop the tokens too fast. If for any reason flush has been completely stopped, tokens drop to
	   * minimum directly. */
	  adjust_rate -= (int) (tb->tokens * FILEIO_PAGE_FLUSH_DROP_RATE);
	}
    }
  else
    {
      /* We need to increase the rate. */
      adjust_rate += MAX (dirty_rate, (int) (fc_Stats.num_tokens * FILEIO_PAGE_FLUSH_GROW_RATE));
    }
  return adjust_rate;
#endif
}

/*
 * fileio_initialize_volume_info_cache () - Allocate/initialize
 *                                          volinfo_header.volinfo array
 *   return: 0 if success, or -1
 *
 * Note: This function is usually first called by
 *       fileio_find_volume_descriptor_with_label()(normal startup,
 *       backup/restore etc.) or fileio_mount()(database creation time)
 */
static int
fileio_initialize_volume_info_cache (void)
{
  int i, n;
  int rv;

  rv = pthread_mutex_lock (&fileio_Vol_info_header.mutex);

  if (fileio_Vol_info_header.volinfo == NULL)
    {
      n = (VOLID_MAX - 1) / FILEIO_VOLINFO_INCREMENT + 1;
      fileio_Vol_info_header.volinfo = (FILEIO_VOLUME_INFO **) malloc (sizeof (FILEIO_VOLUME_INFO *) * n);
      if (fileio_Vol_info_header.volinfo == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (FILEIO_VOLUME_INFO *) * n);
	  pthread_mutex_unlock (&fileio_Vol_info_header.mutex);
	  return -1;
	}
      fileio_Vol_info_header.num_volinfo_array = n;

      for (i = 0; i < fileio_Vol_info_header.num_volinfo_array; i++)
	{
	  fileio_Vol_info_header.volinfo[i] = NULL;
	}
    }

  pthread_mutex_unlock (&fileio_Vol_info_header.mutex);
  return 0;
}

/* TODO: check not use */
#if 0
/*
 * fileio_final_volinfo_cache () - Free volinfo_header.volinfo array
 *   return: void
 */
void
fileio_final_volinfo_cache (void)
{
  int i;
#if defined(WINDOWS) && defined(SERVER_MODE)
  int j;
  FILEIO_VOLUME_INFO *vf;
#endif /* WINDOWS && SERVER_MODE */
  if (fileio_Vol_info_header.volinfo != NULL)
    {
      for (i = 0; i < fileio_Vol_info_header.num_volinfo_array; i++)
	{
#if defined(WINDOWS) && defined(SERVER_MODE)
	  vf = fileio_Vol_info_header.volinfo[i];
	  for (j = 0; j < FILEIO_VOLINFO_INCREMENT; j++)
	    {
	      if (vf[j].vol_mutex != NULL)
		{
		  pthread_mutex_destroy (&vf[j].vol_mutex);
		}
	    }
#endif /* WINDOWS && SERVER_MODE */
	  free_and_init (fileio_Vol_info_header.volinfo[i]);
	}
      free_and_init (fileio_Vol_info_header.volinfo);
      fileio_Vol_info_header.num_volinfo_array = 0;
    }
}
#endif

static int
fileio_allocate_and_initialize_volume_info (FILEIO_VOLUME_HEADER * header_p, int idx)
{
  FILEIO_VOLUME_INFO *vol_info_p;
  int i;

  header_p->volinfo[idx] = NULL;
  vol_info_p = (FILEIO_VOLUME_INFO *) malloc (sizeof (FILEIO_VOLUME_INFO) * FILEIO_VOLINFO_INCREMENT);
  if (vol_info_p == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
	      sizeof (FILEIO_VOLUME_INFO) * FILEIO_VOLINFO_INCREMENT);
      return ER_FAILED;
    }

  for (i = 0; i < FILEIO_VOLINFO_INCREMENT; i++)
    {
      vol_info_p[i].volid = NULL_VOLID;
      vol_info_p[i].vdes = NULL_VOLDES;
      vol_info_p[i].lockf_type = FILEIO_NOT_LOCKF;
      vol_info_p[i].vlabel[0] = '\0';
#if defined(WINDOWS)
      pthread_mutex_init (&vol_info_p[i].vol_mutex, NULL);
#endif /* WINDOWS */
    }

  header_p->volinfo[idx] = vol_info_p;
  return NO_ERROR;
}

/*
 * fileio_expand_permanent_volume_info () - Expand io_volinfo chunks to cache
 *                                          volid volume information
 *   return: 0 if success, or -1
 *   header(in):
 *   volid(in):
 *
 * Note: Permanent volume informations are stored from volid 0 to
 *       header->max_perm_vols. If header->max_perm_vols is less than volid,
 *       allocate new io_volinfo chunk.
 */
static int
fileio_expand_permanent_volume_info (FILEIO_VOLUME_HEADER * header_p, int volid)
{
  int from_idx, to_idx;
  int rv;

  rv = pthread_mutex_lock (&header_p->mutex);

  from_idx = (header_p->max_perm_vols / FILEIO_VOLINFO_INCREMENT);
  to_idx = (volid + 1) / FILEIO_VOLINFO_INCREMENT;

  /* check if to_idx chunks are used for temp volume information */
  if (to_idx >= (header_p->num_volinfo_array - 1 - header_p->max_temp_vols / FILEIO_VOLINFO_INCREMENT))
    {
      pthread_mutex_unlock (&header_p->mutex);
      return -1;
    }

  for (; from_idx <= to_idx; from_idx++)
    {
      if (fileio_allocate_and_initialize_volume_info (header_p, from_idx) != NO_ERROR)
	{
	  pthread_mutex_unlock (&header_p->mutex);
	  return -1;
	}

      header_p->max_perm_vols = (from_idx + 1) * FILEIO_VOLINFO_INCREMENT;
    }

  pthread_mutex_unlock (&header_p->mutex);
  return 0;
}

/*
 * fileio_expand_temporary_volume_info () - Expand io_volinfo chunks to cache
 *                                          volid volume information
 *   return: 0 if success, or -1
 *   header(in):
 *   volid(in):
 *
 * Note: Temporary volume informations are stored from volid LOG_MAX_DBVOLID to
 *       LOG_MAX_DBVOLID-header->max_temp_vols.
 *       If LOG_MAX_DBVOLID-header->max_temp_vols is greater than volid,
 *       allocate new io_volinfo chunk.
 */
static int
fileio_expand_temporary_volume_info (FILEIO_VOLUME_HEADER * header_p, int volid)
{
  int from_idx, to_idx;
  int rv;

  rv = pthread_mutex_lock (&header_p->mutex);

  from_idx = header_p->num_volinfo_array - 1 - (header_p->max_temp_vols / FILEIO_VOLINFO_INCREMENT);
  to_idx = header_p->num_volinfo_array - 1 - ((LOG_MAX_DBVOLID - volid) / FILEIO_VOLINFO_INCREMENT);

  /* check if to_idx chunks are used for perm. volume information */
  if (to_idx <= (header_p->max_perm_vols - 1) / FILEIO_VOLINFO_INCREMENT)
    {
      pthread_mutex_unlock (&header_p->mutex);
      return -1;
    }

  for (; from_idx >= to_idx; from_idx--)
    {
      if (fileio_allocate_and_initialize_volume_info (header_p, from_idx) != NO_ERROR)
	{
	  pthread_mutex_unlock (&header_p->mutex);
	  return -1;
	}

      header_p->max_temp_vols = (header_p->num_volinfo_array - from_idx) * FILEIO_VOLINFO_INCREMENT;
    }

  pthread_mutex_unlock (&header_p->mutex);
  return 0;
}

/* TODO: recoding to use APR
 *
 * fileio_ctime() - VARIANT OF NORMAL CTIME THAT ALWAYS REMOVES THE NEWLINE
 *   return: ptr to time string returned by ctime
 *   time_t(in):
 *   buf(in):
 *
 * Note: Strips the \n off the end of the string returned by ctime.
 *       this routine is really general purpose, there may be other users
 *       of ctime.
 */
static char *
fileio_ctime (INT64 * clock_p, char *buffer_p)
{
  char *p, *t;
  time_t tmp_time;

  tmp_time = (time_t) (*clock_p);
  t = ctime_r (&tmp_time, buffer_p);

  p = strchr (t, '\n');
  if (p)
    {
      *p = '\0';
    }

  return (t);
}

/*
 * fileio_is_terminated_process () -
 *   return:
 *   pid(in):
 */
static bool
fileio_is_terminated_process (int pid)
{
#if defined(WINDOWS)
  HANDLE h_process;

  h_process = OpenProcess (PROCESS_QUERY_INFORMATION, FALSE, pid);
  if (h_process == NULL)
    {
      return true;
    }
  else
    {
      CloseHandle (h_process);
      return false;
    }
#else /* WINDOWS */
  if (kill (pid, 0) == -1)
    {
      return true;
    }
  else
    {
      return false;
    }
#endif /* WINDOWS */
}

#if !defined(WINDOWS)
/*
 * fileio_lock () - LOCKF A DATABASE VOLUME
 *   return:
 *   db_fullname(in): Name of the database where the volume belongs
 *   vlabel(in): Volume label
 *   vdes(in): Volume descriptor
 *   dowait(in): true when it is ok to wait for the lock (databases.txt)
 *
 */
static FILEIO_LOCKF_TYPE
fileio_lock (const char *db_full_name_p, const char *vol_label_p, int vol_fd, bool dowait)
{
  FILE *fp;
  char name_info_lock[PATH_MAX];
  char host[MAXHOSTNAMELEN];
  char host2[MAXHOSTNAMELEN];
  char user[FILEIO_USER_NAME_SIZE];
  char login_name[FILEIO_USER_NAME_SIZE];
  INT64 lock_time;
  long long tmp_lock_time;
  int pid;
  bool retry = true;
  int lockf_errno;
  FILEIO_LOCKF_TYPE result = FILEIO_LOCKF;
  int total_num_loops = 0;
  int num_loops = 0;
  int max_num_loops;
  char io_timeval[CTIME_MAX], format_string[32];

  if (prm_get_bool_value (PRM_ID_IO_LOCKF_ENABLE) != true)
    {
      return FILEIO_LOCKF;
    }

#if defined(CUBRID_DEBUG)
  struct stat stbuf;

  /* 
   * Make sure that advisory locks are used. An advisory lock is desired
   * since we are observing a voluntarily locking scheme.
   * Mandatory locks are know to be dangerous. If a runaway or otherwise
   * out-of-control process should hold a mandatory lock on the database
   * and fail to release that lock,  the entire database system could hang
   */
  if (fstat (vol_fd, &stbuf) != -1)
    {
      if ((stbuf.st_mode & S_ISGID) != 0 && (stbuf.st_mode & S_IRWXG) != S_IXGRP)
	{
	  er_log_debug (ARG_FILE_LINE, "A mandatory lock will be set on file = %s", vol_label_p);
	}
    }
#endif /* CUBRID_DEBUG */

  if (vol_label_p == NULL)
    {
      vol_label_p = "";
    }

  max_num_loops = FILEIO_MAX_WAIT_DBTXT;
  fileio_make_volume_lock_name (name_info_lock, vol_label_p);

  /* 
   * NOTE: The lockby auxiliary file is created only after we have acquired
   *       the lock. This is important to avoid a possible synchronization
   *       problem with this secundary technique
   */

  sprintf (format_string, "%%%ds %%d %%%ds %%lld", FILEIO_USER_NAME_SIZE - 1, MAXHOSTNAMELEN - 1);

again:
  while (retry == true && fileio_lock_file_write (vol_fd, 0, SEEK_SET, 0) < 0)
    {
      if (errno == EINTR)
	{
	  /* Retry if the an interruption was signed */
	  retry = true;
	  continue;
	}
      lockf_errno = errno;
      retry = false;

      /* Volume seems to be mounted by someone else. Find out who has it. */
      fp = fopen (name_info_lock, "r");
      if (fp == NULL)
	{
	  (void) sleep (3);
	  num_loops += 3;
	  total_num_loops += 3;
	  fp = fopen (name_info_lock, "r");
	  if (fp == NULL && num_loops <= 3)
	    {
	      /* 
	       * Note that we try to check for the lock only one more time,
	       * unless we have been waiting for a while
	       * (Case of dowait == false,
	       * note that num_loops is set to 0 when waiting for a lock).
	       */
	      retry = true;
	      continue;
	    }
	}

      if (fp == NULL || fscanf (fp, format_string, user, &pid, host, &tmp_lock_time) != 4)
	{
	  strcpy (user, "???");
	  strcpy (host, "???");
	  pid = 0;
	  lock_time = 0;
	}
      else
	{
	  lock_time = tmp_lock_time;
	}
      /* Make sure that the process holding the lock is not a run away process. A run away process is one of the
       * following: 1) If the lockby file exist and the following is true: same user, same host, and lockby process
       * does not exist any longer */
      if (fp == NULL)
	{
	  /* It is no more true that if the lockby file does not exist, then it is the run away process. When the user
	   * cannot get the file lock, it means that the another process who owns the database exists. */
	  fileio_ctime (&lock_time, io_timeval);
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IO_MOUNT_LOCKED, 6, vol_label_p, db_full_name_p, user, pid, host,
		  (lock_time == 0) ? "???" : io_timeval);
	  return FILEIO_NOT_LOCKF;
	}
      else
	{
	  (void) fclose (fp);
	  *host2 = '\0';
	  cuserid ((char *) login_name);

	  login_name[FILEIO_USER_NAME_SIZE - 1] = '\0';

	  if (!
	      (strcmp (user, login_name) == 0 && GETHOSTNAME (host2, MAXHOSTNAMELEN) == 0 && strcmp (host, host2) == 0
	       && fileio_is_terminated_process (pid) != 0 && errno == ESRCH))
	    {
	      if (dowait != false)
		{
		  /* 
		   * NOBODY USES dowait EXPECT DATABASE.TXT
		   *
		   * It would be nice if we could use a wait function to wait on a
		   * process that is not a child process.
		   * Wait until the process is gone if we are in the same machine,
		   * otherwise, continue looping.
		   */
		  while (fileio_is_volume_exist (name_info_lock) == true && num_loops < 60
			 && total_num_loops < max_num_loops)
		    {
		      if (strcmp (host, host2) == 0 && fileio_is_terminated_process (pid) != 0)
			{
			  break;
			}

		      (void) sleep (3);
		      num_loops += 3;
		      total_num_loops += 3;
		    }

		  if (total_num_loops < max_num_loops)
		    {
		      retry = true;
		      num_loops = 0;
		      goto again;
		    }
		}

	      /* not a run away process */
	      fileio_ctime (&lock_time, io_timeval);
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IO_MOUNT_LOCKED, 6, vol_label_p, db_full_name_p, user, pid,
		      host, io_timeval);
	      return FILEIO_NOT_LOCKF;
	    }
	}
#if defined(CUBRID_DEBUG)
      er_log_debug (ARG_FILE_LINE,
		    "io_lock: WARNING ignoring a run away lock on volume = %s\n. lockd deamon may not be"
		    " working right.\n UNIX error = %s", vol_label_p, strerror (lockf_errno));
#endif /* CUBRID_DEBUG */
    }

  /* Create the information lock file and write the information about the lock */
  fp = fopen (name_info_lock, "w");
  if (fp != NULL)
    {
      if (GETHOSTNAME (host, MAXHOSTNAMELEN) != 0)
	{
	  strcpy (host, "???");
	}

      if (getuserid (login_name, FILEIO_USER_NAME_SIZE) == NULL)
	{
	  strcpy (login_name, "???");
	}

      (void) fprintf (fp, "%s %d %s %ld", login_name, (int) GETPID (), host, time (NULL));
      (void) fclose (fp);
    }
  else
    {
      /* Unable to create the lockf file. */
      if (result == FILEIO_LOCKF)
	{
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IO_MOUNT_FAIL, 1, name_info_lock);
	  fileio_unlock (vol_label_p, vol_fd, result);
	  result = FILEIO_NOT_LOCKF;
	}
    }

  return result;
}

/*
 * fileio_lock_la_log_path () - LOCKF A applylogdb logpath lock
 *   return:
 *   db_fullname(in): Name of the database where the volume belongs
 *   lock_path(in): Lock file path
 *   vdes(in): Volume descriptor
 *   last_deleted_arv_num(out):
 *
 */
FILEIO_LOCKF_TYPE
fileio_lock_la_log_path (const char *db_full_name_p, const char *lock_path_p, int vol_fd, int *last_deleted_arv_num)
{
  FILE *fp;
  char host[MAXHOSTNAMELEN];
  char user[FILEIO_USER_NAME_SIZE];
  char login_name[FILEIO_USER_NAME_SIZE];
  INT64 lock_time;
  long long tmp_lock_time;
  int pid;
  bool retry = true;
  int lockf_errno;
  FILEIO_LOCKF_TYPE result = FILEIO_LOCKF;
  int num_loops = 0;
  char io_timeval[64], format_string[32];

#if defined(CUBRID_DEBUG)
  struct stat stbuf;

  /* 
   * Make sure that advisory locks are used. An advisory lock is desired
   * since we are observing a voluntarily locking scheme.
   * Mandatory locks are know to be dangerous. If a runaway or otherwise
   * out-of-control process should hold a mandatory lock on the database
   * and fail to release that lock,  the entire database system could hang
   */
  if (fstat (vol_fd, &stbuf) != -1)
    {
      if ((stbuf.st_mode & S_ISGID) != 0 && (stbuf.st_mode & S_IRWXG) != S_IXGRP)
	{
	  er_log_debug (ARG_FILE_LINE, "A mandatory lock will be set on file = %s", vol_label_p);
	}
    }
#endif /* CUBRID_DEBUG */

  if (lock_path_p == NULL)
    {
      lock_path_p = "";
    }

  /* 
   * NOTE: The lockby auxiliary file is created only after we have acquired
   *       the lock. This is important to avoid a possible synchronization
   *       problem with this secundary technique
   */
  sprintf (format_string, "%%d %%%ds %%d %%%ds %%lld", FILEIO_USER_NAME_SIZE - 1, MAXHOSTNAMELEN - 1);

  while (retry == true && fileio_lock_file_write (vol_fd, 0, SEEK_SET, 0) < 0)
    {
      if (errno == EINTR)
	{
	  /* Retry if the an interruption was signed */
	  retry = true;
	  continue;
	}
      lockf_errno = errno;
      retry = false;

      /* Volume seems to be mounted by someone else. Find out who has it. */
      fp = fopen (lock_path_p, "r");
      if (fp == NULL)
	{
	  (void) sleep (3);
	  num_loops += 3;
	  fp = fopen (lock_path_p, "r");
	  if (fp == NULL && num_loops <= 3)
	    {
	      retry = true;
	      continue;
	    }
	}

      if (fp == NULL || fscanf (fp, format_string, last_deleted_arv_num, user, &pid, host, &tmp_lock_time) != 5)
	{
	  strcpy (user, "???");
	  strcpy (host, "???");
	  pid = 0;
	  lock_time = 0;
	  *last_deleted_arv_num = -1;
	}
      else
	{
	  lock_time = tmp_lock_time;
	}

      if (fp != NULL)
	{
	  (void) fclose (fp);
	}
#if defined(CUBRID_DEBUG)
      er_log_debug (ARG_FILE_LINE,
		    "io_lock: WARNING ignoring a run away lock on volume = %s\n. lockd deamon may not be"
		    " working right.\n UNIX error = %s", lock_path_p, strerror (lockf_errno));
#endif /* CUBRID_DEBUG */

      memset (io_timeval, 0, sizeof (io_timeval));
      fileio_ctime (&lock_time, io_timeval);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IO_MOUNT_LOCKED, 6, lock_path_p, db_full_name_p, user, pid, host,
	      (lock_time == 0) ? "???" : io_timeval);
      return FILEIO_NOT_LOCKF;
    }

  /* Create the information lock file and write the information about the lock */
  fp = fdopen (vol_fd, "w+");
  if (fp != NULL)
    {
      if (fscanf (fp, format_string, last_deleted_arv_num, user, &pid, host, &tmp_lock_time) != 5)
	{
	  *last_deleted_arv_num = -1;
	}

      lseek (vol_fd, (off_t) 0, SEEK_SET);

      if (GETHOSTNAME (host, MAXHOSTNAMELEN) != 0)
	{
	  strcpy (host, "???");
	}

      if (getuserid (login_name, FILEIO_USER_NAME_SIZE) == NULL)
	{
	  strcpy (login_name, "???");
	}

      if (*last_deleted_arv_num < 0)
	{
	  *last_deleted_arv_num = -1;
	}

      (void) fprintf (fp, "%-10d %s %d %s %ld", *last_deleted_arv_num, login_name, (int) GETPID (), host, time (NULL));
      fflush (fp);
    }
  else
    {
      /* Unable to create the lockf file. */
      if (result == FILEIO_LOCKF)
	{
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IO_MOUNT_FAIL, 1, lock_path_p);
	  result = FILEIO_NOT_LOCKF;
	}
    }

  return result;
}

/*
 * fileio_lock_la_dbname () - LOCKF A applylogdb database lock
 *   return:
 *
 *   lockf_vdes(in): lock file descriptor
 *   db_name(in): database name
 *   log_path(in): log file path
 *
 */
FILEIO_LOCKF_TYPE
fileio_lock_la_dbname (int *lockf_vdes, char *db_name, char *log_path)
{
  int error = NO_ERROR;
  int fd = NULL_VOLDES;
  int pid;
  int r;
  FILEIO_LOCKF_TYPE result = FILEIO_LOCKF;
  FILE *fp = NULL;
  char lock_dir[PATH_MAX], lock_path[PATH_MAX];
  char tmp_db_name[DB_MAX_IDENTIFIER_LENGTH], tmp_log_path[PATH_MAX];
  char format_string[PATH_MAX];

  envvar_vardir_file (lock_dir, sizeof (lock_dir), "APPLYLOGDB");
  snprintf (lock_path, sizeof (lock_path), "%s/%s", lock_dir, db_name);

  if (access (lock_dir, F_OK) < 0)
    {
      /* create parent directory if not exist */
      if (mkdir (lock_dir, 0777) < 0 && errno == ENOENT)
	{
	  char pdir[PATH_MAX];

	  if (cub_dirname_r (lock_dir, pdir, PATH_MAX) > 0 && access (pdir, F_OK) < 0)
	    {
	      mkdir (pdir, 0777);
	    }
	}
    }

  if (access (lock_dir, F_OK) < 0)
    {
      if (mkdir (lock_dir, 0777) < 0)
	{
	  er_log_debug (ARG_FILE_LINE, "unable to create dir (%s)", lock_dir);
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_BO_DIRECTORY_DOESNOT_EXIST, 1, lock_dir);
	  result = FILEIO_NOT_LOCKF;
	  goto error_return;
	}
    }

  snprintf (format_string, sizeof (format_string), "%%d %%%ds %%%ds", DB_MAX_IDENTIFIER_LENGTH - 1, PATH_MAX - 1);

  fd = fileio_open (lock_path, O_RDWR | O_CREAT, 0644);
  if (fd == NULL_VOLDES)
    {
      er_log_debug (ARG_FILE_LINE, "unable to open lock_file (%s)", lock_path);
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IO_MOUNT_FAIL, 1, lock_path);

      result = FILEIO_NOT_LOCKF;
      goto error_return;
    }

  fp = fopen (lock_path, "r");
  if (fp)
    {
      fseek (fp, (off_t) 0, SEEK_SET);

      r = fscanf (fp, format_string, &pid, tmp_db_name, tmp_log_path);
      if (r == 3)
	{
	  assert_release (strcmp (db_name, tmp_db_name) == 0);

	  if (strcmp (db_name, tmp_db_name) || strcmp (log_path, tmp_log_path))
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IO_MOUNT_LOCKED, 6, lock_path, db_name, "-", pid, "-", "-");

	      fclose (fp);

	      result = FILEIO_NOT_LOCKF;
	      goto error_return;
	    }
	}

      fclose (fp);
    }
  else
    {
      er_log_debug (ARG_FILE_LINE, "unable to open lock_file (%s)", lock_path);
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IO_MOUNT_FAIL, 1, lock_path);

      result = FILEIO_NOT_LOCKF;
      goto error_return;
    }

  if (fileio_lock_file_write (fd, 0, SEEK_SET, 0) < 0)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IO_MOUNT_LOCKED, 6, lock_path, db_name, "-", 0, "-", "-");

      result = FILEIO_NOT_LOCKF;
      goto error_return;
    }

  fp = fopen (lock_path, "w+");
  if (fp)
    {
      fseek (fp, (off_t) 0, SEEK_SET);

      pid = getpid ();
      fprintf (fp, "%-10d %s %s", pid, db_name, log_path);
      fflush (fp);
      fclose (fp);
    }
  else
    {
      error = fileio_release_lock (fd);
      assert_release (error == NO_ERROR);

      er_log_debug (ARG_FILE_LINE, "unable to open lock_file (%s)", lock_path);
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IO_MOUNT_FAIL, 1, lock_path);

      result = FILEIO_NOT_LOCKF;
      goto error_return;
    }

  (*lockf_vdes) = fd;

  return result;

error_return:

  if (fd != NULL_VOLDES)
    {
      fileio_close (fd);
      fd = NULL_VOLDES;
    }

  (*lockf_vdes) = fd;

  return result;
}

/*
 * fileio_unlock_la_dbname () - UNLOCKF A applylogdb database lock
 *   return:
 *
 *   lockf_vdes(in): lock file descriptor
 *   db_name(in): database name
 *   clear_owner(in): clear lock owner
 *
 */
FILEIO_LOCKF_TYPE
fileio_unlock_la_dbname (int *lockf_vdes, char *db_name, bool clear_owner)
{
  int result;
  int error;
  off_t end_offset;
  FILE *fp = NULL;
  char lock_dir[PATH_MAX], lock_path[PATH_MAX];

  envvar_vardir_file (lock_dir, sizeof (lock_dir), "APPLYLOGDB");
  snprintf (lock_path, sizeof (lock_path), "%s/%s", lock_dir, db_name);

  if (access (lock_dir, F_OK) < 0)
    {
      er_log_debug (ARG_FILE_LINE, "lock directory does not exist (%s)", lock_dir);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_BO_DIRECTORY_DOESNOT_EXIST, 1, lock_dir);
      return FILEIO_NOT_LOCKF;
    }

  assert_release ((*lockf_vdes) != NULL_VOLDES);
  if ((*lockf_vdes) == NULL_VOLDES)
    {
      return FILEIO_NOT_LOCKF;
    }

  if (clear_owner)
    {
      fp = fopen (lock_path, "w+");
      if (fp == NULL)
	{
	  er_log_debug (ARG_FILE_LINE, "unable to open lock_file (%s)", lock_path);
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IO_MOUNT_FAIL, 1, lock_path);

	  return FILEIO_LOCKF;
	}

      fseek (fp, (off_t) 0, SEEK_END);
      end_offset = ftell (fp);
      fseek (fp, (off_t) 0, SEEK_SET);

      if (end_offset > 0)
	{
	  fprintf (fp, "%*s", (int) end_offset, " ");
	}
      fflush (fp);
      fclose (fp);
    }

  error = fileio_release_lock ((*lockf_vdes));
  if (error == NO_ERROR)
    {
      result = FILEIO_NOT_LOCKF;
    }
  else
    {
      assert_release (error == NO_ERROR);
      result = FILEIO_LOCKF;
    }

  if (result == FILEIO_NOT_LOCKF)
    {
      fileio_close ((*lockf_vdes));
      (*lockf_vdes) = NULL_VOLDES;
    }

  return result;
}

static void
fileio_check_lockby_file (char *name_info_lock_p)
{
  /* 
   * Either we did not acuire the lock through flock or seek has failed.
   * Use secundary technique for verification.
   * Make sure that current process has the lock, that is, check if
   * these are the consecuences of a run away process. If the lockby file
   * indicates that current process has the lock, remove the lockby file
   * to indicate that the process does not have the lock any longer.
   */
  FILE *fp;
  int pid;
  char login_name[FILEIO_USER_NAME_SIZE];
  char user[FILEIO_USER_NAME_SIZE];
  char host[MAXHOSTNAMELEN];
  char host2[MAXHOSTNAMELEN];
  char format_string[32];

  fp = fopen (name_info_lock_p, "r");
  if (fp != NULL)
    {
      sprintf (format_string, "%%%ds %%d %%%ds", FILEIO_USER_NAME_SIZE - 1, MAXHOSTNAMELEN - 1);
      if (fscanf (fp, format_string, user, &pid, host) != 3)
	{
	  strcpy (user, "???");
	  strcpy (host, "???");
	  pid = 0;
	}
      (void) fclose (fp);

      /* Check for same process, same user, same host */
      getuserid (login_name, FILEIO_USER_NAME_SIZE);

      if (pid == GETPID () && strcmp (user, login_name) == 0 && GETHOSTNAME (host2, MAXHOSTNAMELEN) == 0
	  && strcmp (host, host2) == 0)
	{
	  (void) remove (name_info_lock_p);
	}
    }
}

/*
 * fileio_unlock () - UNLOCK A DATABASE VOLUME
 *   return: void
 *   vlabel(in): Volume label
 *   vdes(in): Volume descriptor
 *   lockf_type(in): Type of lock
 *
 * Note: The volume associated with the given name is unlocked and the
 *       lock information file is removed.
 *       If the Unix system complains that the volume is not locked by
 *       the requested process, the information lock file is consulted
 *       to verify for run a way process. If the requested process is
 *       identical to the one recorded in the information lock, the
 *       function returns without any error, otherwise, an error is set
 *       and an error condition is returned.
 */
static void
fileio_unlock (const char *vol_label_p, int vol_fd, FILEIO_LOCKF_TYPE lockf_type)
{
  char name_info_lock[PATH_MAX];

  if (prm_get_bool_value (PRM_ID_IO_LOCKF_ENABLE) == true)
    {
      if (vol_label_p == NULL)
	{
	  vol_label_p = "";
	}

      strcpy (name_info_lock, vol_label_p);
      fileio_make_volume_lock_name (name_info_lock, vol_label_p);

      /* 
       * We must remove the lockby file before we call flock to unlock the file.
       * Otherwise, we may remove the file when is locked by another process
       * Case of preemption and another process acquiring the lock.
       */

      if (lockf_type != FILEIO_LOCKF)
	{
	  fileio_check_lockby_file (name_info_lock);
	}
      else
	{
	  (void) remove (name_info_lock);
	  fileio_unlock_file (vol_fd, 0, SEEK_SET, 0);
	}
    }
}
#endif /* !WINDOWS */

/*
 * fileio_initialize_pages () - Initialize the first npages of the given volume with the
 *                    content of given page
 *   return: io_pgptr on success, NULL on failure
 *   vdes(in): Volume descriptor
 *   io_pgptr(in): Initialization content of all pages
 *   npages(in): Number of pages to initialize
 *   kbytes_to_be_written_per_sec : size to add volume per sec
 */
void *
fileio_initialize_pages (THREAD_ENTRY * thread_p, int vol_fd, void *io_page_p, DKNPAGES start_pageid, DKNPAGES npages,
			 size_t page_size, int kbytes_to_be_written_per_sec)
{
  PAGEID page_id;
#if defined (SERVER_MODE)
  int count_of_page_for_a_sleep = 10;
  INT64 allowed_millis_for_a_sleep = 0;	/* time which is time for writing unit of page and sleeping in a sleep */
  INT64 previous_elapsed_millis;	/* time which is previous time for writing unit of page and sleep */
  INT64 time_to_sleep;

  TSC_TICKS start_tick, end_tick;
  TSCTIMEVAL tv_diff;

  INT64 page_count_per_sec;
#endif

#if defined (SERVER_MODE)
  if (kbytes_to_be_written_per_sec > 0)
    {
      page_count_per_sec = kbytes_to_be_written_per_sec / (IO_PAGESIZE / ONE_K);

      if (page_count_per_sec < count_of_page_for_a_sleep)
	{
	  page_count_per_sec = count_of_page_for_a_sleep;
	}
      allowed_millis_for_a_sleep = count_of_page_for_a_sleep * 1000 / page_count_per_sec;

      tsc_getticks (&start_tick);
    }
#endif

  for (page_id = start_pageid; page_id < npages + start_pageid; page_id++)
    {
#if !defined(CS_MODE)
      /* check for interrupts from user (i.e. Ctrl-C) */
      if ((page_id % FILEIO_CHECK_FOR_INTERRUPT_INTERVAL) == 0)
	{
	  if (thread_get_check_interrupt (thread_p) && pgbuf_is_log_check_for_interrupts (thread_p))
	    {
	      return NULL;
	    }
	}
#endif /* !CS_MODE */

#if !defined(NDEBUG)
      /* skip volume header page to find abnormal update */
      if (page_id == 0)
	{
	  continue;
	}
#endif

      if (fileio_write (thread_p, vol_fd, io_page_p, page_id, page_size) == NULL)
	{
	  return NULL;
	}

#if defined (SERVER_MODE)
      if (kbytes_to_be_written_per_sec > 0 && (page_id + 1) % count_of_page_for_a_sleep == 0)
	{
	  tsc_getticks (&end_tick);
	  tsc_elapsed_time_usec (&tv_diff, end_tick, start_tick);

	  previous_elapsed_millis = (tv_diff.tv_sec * 1000LL) + (tv_diff.tv_usec / 1000LL);

	  /* calculate time to sleep through subtracting */
	  time_to_sleep = allowed_millis_for_a_sleep - previous_elapsed_millis;
	  if (time_to_sleep > 0)
	    {
	      thread_sleep ((int) time_to_sleep);
	    }

	  tsc_getticks (&start_tick);
	}
#endif
    }

  return io_page_p;
}

/*
 * fileio_open () - Same as Unix open, but with retry during interrupts
 *   return: volume descriptor identifier on success, NULL_VOLDES on failure
 *   vlabel(in): Volume label
 *   flags(in): open the volume as specified by the flags
 *   mode(in): used when the volume is created
 */
int
fileio_open (const char *vol_label_p, int flags, int mode)
{
  int vol_fd;

  do
    {
#if defined(WINDOWS)
      vol_fd = open (vol_label_p, flags | _O_BINARY, mode);
#else /* WINDOWS */
      vol_fd = open (vol_label_p, flags, mode);
#endif /* WINDOWS */
    }
  while (vol_fd == NULL_VOLDES && errno == EINTR);

#if !defined(WINDOWS)
  if (vol_fd > NULL_VOLDES)
    {
      int high_vol_fd;
      int range = MAX_NTRANS + 10;

      /* move fd to the over max_clients range */
      high_vol_fd = fcntl (vol_fd, F_DUPFD, range);
      if (high_vol_fd != -1)
	{
	  close (vol_fd);
	  vol_fd = high_vol_fd;
	}
    }

  if (prm_get_bool_value (PRM_ID_DBFILES_PROTECT) == true && vol_fd > 0)
    {
      fileio_get_lock (vol_fd, vol_label_p);
    }
#endif /* !WINDOWS */

  return vol_fd;
}

#if !defined(WINDOWS)
/*
 * fileio_set_permission () -
 *   return:
 *   vlabel(in):
 */
int
fileio_set_permission (const char *vol_label_p)
{
  int mode;
  struct stat buf;
  int error = NO_ERROR;

  if (stat (vol_label_p, &buf) < 0)
    {
      error = ER_IO_CANNOT_GET_PERMISSION;
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, vol_label_p);
      return error;
    }

  /* get currently set mode */
  mode = buf.st_mode;
  /* remove group execute permission from mode */
  mode &= ~(S_IEXEC >> 3);
  /* set 'set group id bit' in mode */
  mode |= S_ISGID;

  if (chmod (vol_label_p, mode) < 0)
    {
      error = ER_IO_CANNOT_CHANGE_PERMISSION;
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, vol_label_p);
      return error;
    }

  return error;
}

/*
 * fileio_get_lock () -
 *   return:
 *   fd(in):
 *   vlabel(in):
 */
static int
fileio_get_lock (int fd, const char *vol_label_p)
{
  int error = NO_ERROR;

  if (fileio_lock_file_read (fd, 0, SEEK_SET, 0) < 0)
    {
      error = ER_IO_GET_LOCK_FAIL;
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 2, vol_label_p, fd);
    }

  return error;
}

/*
 * fileio_release_lock () -
 *   return:
 *   fd(in):
 */
static int
fileio_release_lock (int fd)
{
  int error = NO_ERROR;

  if (fileio_unlock_file (fd, 0, SEEK_SET, 0) < 0)
    {
      error = ER_IO_RELEASE_LOCK_FAIL;
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, fd);
    }

  return error;
}
#endif /* !WINDOWS */

/*
 * fileio_close () - Close the volume associated with the given volume descriptor
 *   return: void
 *   vdes(in): Volume descriptor
 */
void
fileio_close (int vol_fd)
{
#if !defined(WINDOWS)
  if (prm_get_bool_value (PRM_ID_DBFILES_PROTECT) == true)
    {
      fileio_release_lock (vol_fd);
    }
#endif /* !WINDOWS */

  if (close (vol_fd) != 0)
    {
      er_set_with_oserror (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_IO_DISMOUNT_FAIL, 1,
			   fileio_get_volume_label_by_fd (vol_fd, PEEK));
    }
}

/*
 * fileio_create () - Create the volume (or file) without initializing it
 *   return: volume descriptor identifier on success, NULL_VOLDES on failure
 *   db_fullname(in): Name of the database where the volume belongs
 *   vlabel(in): Volume label
 *   volid(in): Volume identifier
 *   dolock(in): Lock the volume from other Unix processes
 *   dosync(in): synchronize the writes on the volume ?
 */
static int
fileio_create (THREAD_ENTRY * thread_p, const char *db_full_name_p, const char *vol_label_p, VOLID vol_id,
	       bool is_do_lock, bool is_do_sync)
{
  int tmp_vol_desc = NULL_VOLDES;
  int vol_fd;
  FILEIO_LOCKF_TYPE lockf_type = FILEIO_NOT_LOCKF;
#if defined(WINDOWS)
  int sh_flag;
#else
  int o_sync;
#endif /* WINDOWS */

#if !defined(CS_MODE)
  /* Make sure that the volume is not already mounted. if it is, dismount the volume. */
  vol_fd = fileio_find_volume_descriptor_with_label (vol_label_p);
  if (vol_fd != NULL_VOLDES)
    {
      fileio_dismount (thread_p, vol_fd);
    }
#endif /* !CS_MODE */

#if defined(WINDOWS)
  sh_flag = is_do_lock ? _SH_DENYWR : _SH_DENYNO;

  vol_fd = _sopen (vol_label_p, FILEIO_DISK_FORMAT_MODE | O_BINARY, sh_flag, FILEIO_DISK_PROTECTION_MODE);
  if (vol_fd == NULL_VOLDES)
    {
      if (sh_flag == _SH_DENYRW && errno != ENOENT)
	{
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IO_MOUNT_LOCKED, 6, vol_label_p, db_full_name_p,
			       "-", 0, "-", "-");
	}
      else
	{
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IO_FORMAT_FAIL, 3, vol_label_p, -1, -1LL);
	}
    }

#else /* !WINDOWS */

  o_sync = (is_do_sync != false) ? O_SYNC : 0;

  /* If the file exist make sure that nobody else is using it, before it is truncated */
  if (is_do_lock != false)
    {
      tmp_vol_desc = fileio_open (vol_label_p, O_RDWR | o_sync, 0);
      if (tmp_vol_desc != NULL_VOLDES)
	{
	  /* The volume (file) already exist. Make sure that nobody is using it before the old one is destroyed */
	  lockf_type = fileio_lock (db_full_name_p, vol_label_p, tmp_vol_desc, false);
	  if (lockf_type == FILEIO_NOT_LOCKF)
	    {
	      /* Volume seems to be mounted by someone else */
	      fileio_close (tmp_vol_desc);
	      return NULL_VOLDES;
	    }
	}
    }

  vol_fd = fileio_open (vol_label_p, FILEIO_DISK_FORMAT_MODE | o_sync, FILEIO_DISK_PROTECTION_MODE);
  if (vol_fd == NULL_VOLDES)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IO_FORMAT_FAIL, 3, vol_label_p, -1, -1LL);
    }

  if (tmp_vol_desc != NULL_VOLDES)
    {
      if (lockf_type != FILEIO_NOT_LOCKF)
	{
	  fileio_unlock (vol_label_p, tmp_vol_desc, lockf_type);
	}
      fileio_close (tmp_vol_desc);
    }
#endif /* WINDOWS */

  perfmon_inc_stat (thread_p, PSTAT_FILE_NUM_CREATES);

  if (vol_fd != NULL_VOLDES)
    {
#if !defined(WINDOWS)
      if (is_do_lock == true)
	{
	  lockf_type = fileio_lock (db_full_name_p, vol_label_p, vol_fd, false);

	  if (lockf_type == FILEIO_NOT_LOCKF)
	    {
	      /* This should not happen, the volume seems to be mounted by someone else */
	      fileio_dismount (thread_p, vol_fd);
	      fileio_unformat (thread_p, vol_label_p);
	      vol_fd = NULL_VOLDES;

	      return vol_fd;
	    }
	}
#endif /* !WINDOWS */

#if !defined(CS_MODE)
      if (fileio_cache (vol_id, vol_label_p, vol_fd, lockf_type) != vol_fd)
	{
	  /* This should not happen, the volume seems to be mounted by someone else */
	  fileio_dismount (thread_p, vol_fd);
	  fileio_unformat (thread_p, vol_label_p);
	  vol_fd = NULL_VOLDES;

	  return vol_fd;
	}
#endif /* !CS_MODE */
    }

  return vol_fd;
}

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

#if !defined(CS_MODE)
		  if (pgbuf_is_log_check_for_interrupts (thread_p) == true)
		    {
		      return NULL_VOLDES;
		    }
#endif
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
 * fileio_format () - Format a volume of npages and mount the volume
 *   return: volume descriptor identifier on success, NULL_VOLDES on failure
 *   db_fullname(in): Name of the database where the volume belongs
 *   vlabel(in): Volume label
 *   volid(in): Volume identifier
 *   npages(in): Number of pages
 *   sweep_clean(in): Clean the newly formatted volume
 *   dolock(in): Lock the volume from other Unix processes
 *   dosync(in): synchronize the writes on the volume ?
 *   kbytes_to_be_written_per_sec : size to add volume per sec
 *
 * Note: If sweep_clean is true, every page is initialized with recovery
 *       information. In addition a volume can be optionally locked.
 *       For example, the active log volume is locked to prevent
 *       several server processes from accessing the same database.
 */
int
fileio_format (THREAD_ENTRY * thread_p, const char *db_full_name_p, const char *vol_label_p, VOLID vol_id,
	       DKNPAGES npages, bool is_sweep_clean, bool is_do_lock, bool is_do_sync, size_t page_size,
	       int kbytes_to_be_written_per_sec, bool reuse_file)
{
  int vol_fd;
  FILEIO_PAGE *malloc_io_page_p;
  off_t offset;
  DKNPAGES max_npages;
#if !defined(WINDOWS)
  struct stat buf;
#endif
  bool is_raw_device = false;

  /* Check for bad number of pages...and overflow */
  if (npages <= 0)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IO_FORMAT_BAD_NPAGES, 2, vol_label_p, npages);
      return NULL_VOLDES;
    }

  if (fileio_is_volume_exist (vol_label_p) == true && reuse_file == false)
    {
      /* The volume that we are trying to create already exist. Remove it and try again */
#if !defined(WINDOWS)
      if (lstat (vol_label_p, &buf) != 0)
	{
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IO_MOUNT_FAIL, 1, vol_label_p);
	}

      if (!S_ISLNK (buf.st_mode))
	{
	  fileio_unformat (thread_p, vol_label_p);
	}
      else
	{
	  if (stat (vol_label_p, &buf) != 0)
	    {
	      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IO_MOUNT_FAIL, 1, vol_label_p);
	    }

	  is_raw_device = S_ISCHR (buf.st_mode);
	}
#else /* !WINDOWS */
      fileio_unformat (thread_p, vol_label_p);
      is_raw_device = false;
#endif /* !WINDOWS */
    }

  if (is_raw_device)
    {
      max_npages = (DKNPAGES) VOL_MAX_NPAGES (page_size);
    }
  else
    {
      max_npages = fileio_get_number_of_partition_free_pages (vol_label_p, page_size);
    }

  offset = FILEIO_GET_FILE_SIZE (page_size, npages - 1);

  /* 
   * Make sure that there is enough pages on the given partition before we
   * create and initialize the volume.
   * We should also check for overflow condition.
   */
  if (npages > max_npages || (offset < npages && npages > 1))
    {
      if (offset < npages)
	{
	  /* Overflow */
	  offset = FILEIO_GET_FILE_SIZE (page_size, VOL_MAX_NPAGES (page_size));
	}

      if (max_npages >= 0)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IO_FORMAT_OUT_OF_SPACE, 5, vol_label_p, npages, (offset / 1024),
		  max_npages, FILEIO_GET_FILE_SIZE (page_size / 1024, max_npages));
	}
      else
	{
	  /* There was an error in fileio_get_number_of_partition_free_pages */
	  ;
	}

      return NULL_VOLDES;
    }

  malloc_io_page_p = (FILEIO_PAGE *) malloc (page_size);
  if (malloc_io_page_p == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, page_size);
      return NULL_VOLDES;
    }

  memset ((char *) malloc_io_page_p, 0, page_size);
  (void) fileio_initialize_res (thread_p, &(malloc_io_page_p->prv));

  vol_fd = fileio_create (thread_p, db_full_name_p, vol_label_p, vol_id, is_do_lock, is_do_sync);
  FI_TEST (thread_p, FI_TEST_FILE_IO_FORMAT, 0);
  if (vol_fd != NULL_VOLDES)
    {
      /* initialize the pages of the volume. */

      /* initialize at least two pages, the header page and the last page. in case of is_sweep_clean == true, every
       * page of the volume will be written. */
      if (fileio_write (thread_p, vol_fd, malloc_io_page_p, 0, page_size) == NULL)
	{
	  fileio_dismount (thread_p, vol_fd);
	  fileio_unformat (thread_p, vol_label_p);
	  free_and_init (malloc_io_page_p);

	  if (er_errid () != ER_INTERRUPTED)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IO_WRITE, 2, 0, vol_id);
	    }

	  vol_fd = NULL_VOLDES;
	  return vol_fd;
	}

#if defined(HPUX)
      if ((is_sweep_clean == true
	   && !fileio_initialize_pages (vol_fd, malloc_io_page_p, npages, page_size, kbytes_to_be_written_per_sec))
	  || (is_sweep_clean == false && !fileio_write (vol_fd, malloc_io_page_p, npages - 1, page_size)))
#else /* HPUX */
      if (!((fileio_write (thread_p, vol_fd, malloc_io_page_p, npages - 1, page_size) == malloc_io_page_p)
	    && (is_sweep_clean == false
		|| fileio_initialize_pages (thread_p, vol_fd, malloc_io_page_p, 0, npages, page_size,
					    kbytes_to_be_written_per_sec) == malloc_io_page_p)))
#endif /* HPUX */
	{
	  /* It is likely that we run of space. The partition where the volume was created has been used since we
	   * checked above. */

	  max_npages = fileio_get_number_of_partition_free_pages (vol_label_p, page_size);

	  fileio_dismount (thread_p, vol_fd);
	  fileio_unformat (thread_p, vol_label_p);
	  free_and_init (malloc_io_page_p);
	  if (er_errid () != ER_INTERRUPTED)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IO_FORMAT_OUT_OF_SPACE, 5, vol_label_p, npages,
		      (offset / 1024), max_npages, (long long) ((page_size / 1024) * max_npages));
	    }
	  vol_fd = NULL_VOLDES;
	  return vol_fd;
	}

#if defined(WINDOWS)
      fileio_dismount (thread_p, vol_fd);
      vol_fd = fileio_mount (thread_p, NULL, vol_label_p, vol_id, false, false);
#endif /* WINDOWS */
    }
  else
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_BO_CANNOT_CREATE_VOL, 2, vol_label_p, db_full_name_p);
    }

  free_and_init (malloc_io_page_p);
  return vol_fd;
}

#if !defined (CS_MODE)
/*
 * fileio_expand_to () -  Expand a volume to the given number of pages.
 *
 *  return:
 *
 *    error code or NO_ERROR
 *
 *
 *  arguments:
 *
 *    vol_id      : Volume identifier
 *    size_npages : New size in pages
 *    voltype     : temporary or permanent volume type
 *
 *
 *  How it works:
 *
 *    A new size for file is provided. The new size is expected to be bigger than current size (with the exception of
 *    recovery cases). This approach replaced extending by a given size to fix recovery errors (file extension could
 *    be executed twice).
 *
 *    Enough disk space is checked first before doing extend.
 *
 *  Notes:
 *
 *    Pages are not sweep_clean/initialized if they are part of temporary volumes.
 *
 *    No checking for temporary volumes is performed by this function.
 *
 *    On WINDOWS && SERVER MODE io_mutex lock must be obtained before calling lseek. Otherwise, expanding can
 *    interfere with fileio_read and fileio_write calls. This caused corruptions in the temporary file, random pages
 *    being written at the end of file instead of being written at their designated places.
 */
int
fileio_expand_to (THREAD_ENTRY * thread_p, VOLID vol_id, DKNPAGES size_npages, DB_VOLTYPE voltype)
{
  int vol_fd;
  const char *vol_label_p;
  FILEIO_PAGE *io_page_p;
  DKNPAGES max_npages;
  size_t max_size;
  PAGEID start_pageid;
  size_t current_size;
  PAGEID last_pageid;
  size_t new_size;
  size_t desired_extend_size;
  size_t max_extend_size;
#if defined(WINDOWS) && defined(SERVER_MODE)
  int rv;
  pthread_mutex_t *io_mutex;
  static pthread_mutex_t io_mutex_instance = PTHREAD_MUTEX_INITIALIZER;
#endif /* WINDOWS && SERVER_MODE */

  int error_code = NO_ERROR;

  assert (size_npages > 0);

  vol_fd = fileio_get_volume_descriptor (vol_id);
  vol_label_p = fileio_get_volume_label (vol_id, PEEK);

  if (vol_fd == NULL_VOLDES || vol_label_p == NULL)
    {
      assert (false);		/* I don't think we can accept this case */
      return ER_FAILED;
    }

  max_npages = fileio_get_number_of_partition_free_pages (vol_label_p, IO_PAGESIZE);
  if (max_npages < 0)
    {
      ASSERT_ERROR_AND_SET (error_code);
      return error_code;
    }

#if defined(WINDOWS) && defined(SERVER_MODE)
  io_mutex = fileio_get_volume_mutex (thread_p, vol_fd);
  if (io_mutex == NULL)
    {
      io_mutex = &io_mutex_instance;
    }

  rv = pthread_mutex_lock (io_mutex);
  if (rv != 0)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      return ER_FAILED;
    }
#endif /* WINDOWS && SERVER_MODE */

  /* get current size */
  current_size = lseek (vol_fd, 0, SEEK_END);
#if defined(WINDOWS) && defined(SERVER_MODE)
  pthread_mutex_unlock (io_mutex);
#endif /* WINDOWS && SERVER_MODE */
  /* safe-guard: current size is rounded to IO_PAGESIZE... unless it crashed during an expand */
  assert (!LOG_ISRESTARTED () || (current_size % IO_PAGESIZE) == 0);

  /* compute new size */
  new_size = ((size_t) size_npages) * IO_PAGESIZE;

  if (new_size <= current_size)
    {
      /* this must be recovery. */
      assert (!LOG_ISRESTARTED ());
      er_log_debug (ARG_FILE_LINE, "skip extending volume %d with current size %zu to new size %zu\n",
		    vol_id, current_size, new_size);
      return NO_ERROR;
    }

  /* overflow safety check */
  /* is this necessary? we dropped support for 32-bits systems. for now, I'll leave this check */
  max_size = ((size_t) VOL_MAX_NPAGES (IO_PAGESIZE)) * IO_PAGESIZE;
  new_size = MIN (new_size, max_size);

  /* consider disk free space */
  desired_extend_size = new_size - current_size;
  max_extend_size = ((size_t) max_npages) * IO_PAGESIZE;

  if (max_extend_size < desired_extend_size)
    {
      const size_t ONE_KILO = 1024;
      error_code = ER_IO_EXPAND_OUT_OF_SPACE;

      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IO_EXPAND_OUT_OF_SPACE, 5, vol_label_p,
	      desired_extend_size / IO_PAGESIZE, new_size / ONE_KILO, max_npages,
	      FILEIO_GET_FILE_SIZE (IO_PAGESIZE / ONE_KILO, max_npages));
    }

  /* init page */
  io_page_p = (FILEIO_PAGE *) db_private_alloc (thread_p, IO_PAGESIZE);
  if (io_page_p == NULL)
    {
      /* TBD: remove memory allocation manual checks. */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) IO_PAGESIZE);
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  memset (io_page_p, 0, IO_PAGESIZE);
  (void) fileio_initialize_res (thread_p, &(io_page_p->prv));

  start_pageid = (PAGEID) (current_size / IO_PAGESIZE);
  last_pageid = ((PAGEID) (new_size / IO_PAGESIZE) - 1);

  if (voltype == DB_TEMPORARY_VOLTYPE)
    {
      /* Write the last page */
      if (fileio_write (thread_p, vol_fd, io_page_p, last_pageid, IO_PAGESIZE) != io_page_p)
	{
	  ASSERT_ERROR_AND_SET (error_code);
	}
    }
  else
    {
      /* support generic volume only */
      assert_release (voltype == DB_PERMANENT_VOLTYPE);

      if (fileio_initialize_pages (thread_p, vol_fd, io_page_p, start_pageid, last_pageid - start_pageid + 1,
				   IO_PAGESIZE, -1) == NULL)
	{
	  ASSERT_ERROR_AND_SET (error_code);
	}
    }

  if (error_code != NO_ERROR && error_code != ER_INTERRUPTED)
    {
      /* I don't like below assumption, it can be misleading. From what I have seen, errors are already set. */
#if 0
      /* It is likely that we run of space. The partition where the volume was created has been used since we checked
       * above.
       */
      max_npages = fileio_get_number_of_partition_free_pages (vol_label_p, IO_PAGESIZE);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IO_EXPAND_OUT_OF_SPACE, 5, vol_label_p, size_npages,
	      last_offset / 1024, max_npages, FILEIO_GET_FILE_SIZE (IO_PAGESIZE / 1024, max_npages));
#endif /* 0 */
    }

  db_private_free (thread_p, io_page_p);

  return error_code;
}
#endif /* not CS_MODE */

#if defined(ENABLE_UNUSED_FUNCTION)
/*
 * fileio_truncate () -  TRUNCATE A TEMPORARY VOLUME
 *   return: npages
 *   volid(in):  Volume identifier
 *   npages_to_resize(in):  Number of pages to resize
 */
DKNPAGES
fileio_truncate (VOLID vol_id, DKNPAGES npages_to_resize)
{
  int vol_fd;
  const char *vol_label_p;
  off_t length;
  bool is_retry = true;

  vol_fd = fileio_get_volume_descriptor (vol_id);
  vol_label_p = fileio_get_volume_label (vol_id, PEEK);

  if (vol_fd == NULL_VOLDES || vol_label_p == NULL)
    {
      return -1;
    }

  if (npages_to_resize <= 0)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IO_FORMAT_BAD_NPAGES, 2, vol_label_p, npages_to_resize);
      return -1;
    }

  length = FILEIO_GET_FILE_SIZE (IO_PAGESIZE, npages_to_resize);
  while (is_retry == true)
    {
      is_retry = false;
      if (ftruncate (vol_fd, length))
	{
	  if (errno == EINTR)
	    {
	      is_retry = true;
	    }
	  else
	    {
	      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IO_TRUNCATE, 2, npages_to_resize,
				   fileio_get_volume_label_by_fd (vol_fd, PEEK));
	      return -1;
	    }
	}
    }
  return npages_to_resize;
}
#endif

/*
 * fileio_unformat () - DESTROY A VOLUME
 *   return: void
 *   vlabel(in): Label of volume to unformat
 *
 * Note: If the volume is mounted, it is dismounted. Then, the volume is
 *       destroyed/unformatted.
 */
void
fileio_unformat (THREAD_ENTRY * thread_p, const char *vol_label_p)
{
  fileio_unformat_and_rename (thread_p, vol_label_p, NULL);
}

/*
 * fileio_unformat_and_rename () - DESTROY A VOLUME
 *   return: void
 *   vol_label(in): Label of volume to unformat
 *   new_vlabel(in): New volume label. if NULL, volume will be deleted
 *
 * Note: If the volume is mounted, it is dismounted. Then, the volume is
 *       destroyed/unformatted.
 */
void
fileio_unformat_and_rename (THREAD_ENTRY * thread_p, const char *vol_label_p, const char *new_label_p)
{
#if defined (EnableThreadMonitoring)
  TSC_TICKS start_tick, end_tick;
  TSCTIMEVAL elapsed_time;
#endif
#if !defined(CS_MODE)
  int vol_fd;
  char vlabel_p[PATH_MAX];

  /* Dismount the volume if it is mounted */
  vol_fd = fileio_find_volume_descriptor_with_label (vol_label_p);
  if (vol_fd != NULL_VOLDES)
    {
      /* if vol_label_p is a pointer of global vinfo->vlabel, It can be reset in fileio_dismount */
      strcpy (vlabel_p, vol_label_p);
      vol_label_p = vlabel_p;
      fileio_dismount (thread_p, vol_fd);
    }
#endif /* !CS_MODE */

#if defined (EnableThreadMonitoring)
  if (0 < prm_get_integer_value (PRM_ID_MNT_WAITING_THREAD))
    {
      tsc_getticks (&start_tick);
    }
#endif

  if (new_label_p == NULL)
    {
      (void) remove (vol_label_p);
    }
  else
    {
      if (os_rename_file (vol_label_p, new_label_p) != NO_ERROR)
	{
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IO_RENAME_FAIL, 2, vol_label_p, new_label_p);
	}
    }

#if defined (EnableThreadMonitoring)
  if (0 < prm_get_integer_value (PRM_ID_MNT_WAITING_THREAD))
    {
      tsc_getticks (&end_tick);
      tsc_elapsed_time_usec (&elapsed_time, end_tick, start_tick);
    }

  if (MONITOR_WAITING_THREAD (elapsed_time))
    {
      er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, ER_MNT_WAITING_THREAD, 2, "file remove",
	      prm_get_integer_value (PRM_ID_MNT_WAITING_THREAD));
      er_log_debug (ARG_FILE_LINE, "fileio_unformat: %6d.%06d\n", elapsed_time.tv_sec, elapsed_time.tv_usec);
    }
#endif
}

/*
 * fileio_copy_volume () - COPY A DISK
 *   return: volume descriptor identifier on success, NULL_VOLDES on failure
 *   from_vdes(in): From Volume descriptor
 *   npages(in): From Volume descriptor
 *   to_vlabel(in): To Volume label
 *   to_volid(in): Volume identifier assigned to the copy
 *   reset_rcvinfo(in): Reset recovery information?
 *
 * Note: Format a new volume with the number of given pages and copy
 *       the contents of the volume associated with from_vdes onto the
 *       new generated volume. The recovery information kept in every
 *       page may be optionally initialized.
 */
int
fileio_copy_volume (THREAD_ENTRY * thread_p, int from_vol_desc, DKNPAGES npages, const char *to_vol_label_p,
		    VOLID to_vol_id, bool is_reset_recovery_info)
{
  PAGEID page_id;
  FILEIO_PAGE *malloc_io_page_p = NULL;
  int to_vol_desc;

  /* 
   * Create the to_volume. Don't initialize the volume with recovery
   * information since it generated/created when the content of the pages are
   * copied.
   */

#if defined(HPUX)
  /* HP-UX shows the poor performance in fileio_format(). */
  to_vol_desc = fileio_create (NULL, to_vol_label_p, to_vol_id, false, false);
#else /* HPUX */
  to_vol_desc =
    fileio_format (thread_p, NULL, to_vol_label_p, to_vol_id, npages, false, false, false, IO_PAGESIZE, 0, false);
#endif /* HPUX */
  if (to_vol_desc == NULL_VOLDES)
    {
      return NULL_VOLDES;
    }

  /* Don't read the pages from the page buffer pool but directly from disk */
  malloc_io_page_p = (FILEIO_PAGE *) malloc (IO_PAGESIZE);
  if (malloc_io_page_p == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) IO_PAGESIZE);
      goto error;
    }

  if (is_reset_recovery_info == false)
    {
      /* Copy the volume as it is */
      for (page_id = 0; page_id < npages; page_id++)
	{
	  if (fileio_read (thread_p, from_vol_desc, malloc_io_page_p, page_id, IO_PAGESIZE) == NULL
	      || fileio_write (thread_p, to_vol_desc, malloc_io_page_p, page_id, IO_PAGESIZE) == NULL)
	    {
	      goto error;
	    }
	}
    }
  else
    {
      /* Reset the recovery information. Just like if this was a formatted volume */
      for (page_id = 0; page_id < npages; page_id++)
	{
	  if (fileio_read (thread_p, from_vol_desc, malloc_io_page_p, page_id, IO_PAGESIZE) == NULL)
	    {
	      goto error;
	    }
	  else
	    {
	      LSA_SET_NULL (&malloc_io_page_p->prv.lsa);
	      if (fileio_write (thread_p, to_vol_desc, malloc_io_page_p, page_id, IO_PAGESIZE) == NULL)
		{
		  goto error;
		}
	    }
	}
    }

  if (fileio_synchronize (thread_p, to_vol_desc, to_vol_label_p) != to_vol_desc)
    {
      goto error;
    }

  free_and_init (malloc_io_page_p);
  return to_vol_desc;

error:
  fileio_dismount (thread_p, to_vol_desc);
  fileio_unformat (thread_p, to_vol_label_p);
  if (malloc_io_page_p != NULL)
    {
      free_and_init (malloc_io_page_p);
    }

  return NULL_VOLDES;
}

/*
 * fileio_reset_volume () - Reset the recovery information (LSA) of all pages of given
 *                  volume with given reset_lsa
 *   return:
 *   vdes(in): Volume descriptor
 *   vlabel(in): Volume label
 *   npages(in): Number of pages of volume to reset
 *   reset_lsa(in): The reset recovery information LSA
 */
int
fileio_reset_volume (THREAD_ENTRY * thread_p, int vol_fd, const char *vlabel, DKNPAGES npages, LOG_LSA * reset_lsa_p)
{
  PAGEID page_id;
  FILEIO_PAGE *malloc_io_page_p;
  int success = NO_ERROR;

  malloc_io_page_p = (FILEIO_PAGE *) malloc (IO_PAGESIZE);
  if (malloc_io_page_p == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) IO_PAGESIZE);
      return ER_FAILED;
    }

  for (page_id = 0; page_id < npages; page_id++)
    {
      if (fileio_read (thread_p, vol_fd, malloc_io_page_p, page_id, IO_PAGESIZE) != NULL)
	{
	  LSA_COPY (&malloc_io_page_p->prv.lsa, reset_lsa_p);
	  if (fileio_write (thread_p, vol_fd, malloc_io_page_p, page_id, IO_PAGESIZE) == NULL)
	    {
	      success = ER_FAILED;
	      break;
	    }
	}
      else
	{
	  success = ER_FAILED;
	  break;
	}
    }
  free_and_init (malloc_io_page_p);

  if (fileio_synchronize (thread_p, vol_fd, vlabel) != vol_fd)
    {
      success = ER_FAILED;
    }

  return success;
}

/*
 * fileio_mount () - Mount the volume associated with the given name and permanent
 *               identifier
 *   return: volume descriptor identifier on success, NULL_VOLDES on failure
 *   db_fullname(in): Name of the database where the volume belongs
 *   vlabel(in): Volume label
 *   volid(in): Permanent Volume identifier
 *   lockwait(in): Lock the volume from other Unix processes
 *   dosync(in): synchronize the writes on the volume ?
 */
int
fileio_mount (THREAD_ENTRY * thread_p, const char *db_full_name_p, const char *vol_label_p, VOLID vol_id, int lock_wait,
	      bool is_do_sync)
{
#if defined(WINDOWS)
  int vol_fd;
  int sh_flags;

#if !defined(CS_MODE)
  FILEIO_CHECK_AND_INITIALIZE_VOLUME_HEADER_CACHE (NULL_VOLDES);

  /* Is volume already mounted ? */
  vol_fd = fileio_find_volume_descriptor_with_label (vol_label_p);
  if (vol_fd != NULL_VOLDES)
    {
      return vol_fd;
    }
#endif /* !CS_MODE */

  sh_flags = lock_wait > 0 ? _SH_DENYWR : _SH_DENYNO;

  vol_fd = _sopen (vol_label_p, _O_RDWR | _O_BINARY, sh_flags, 0600);
  if (vol_fd == NULL_VOLDES)
    {
      if (sh_flags == _SH_DENYWR && errno != ENOENT)
	{
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IO_MOUNT_LOCKED, 6, vol_label_p, db_full_name_p,
			       "-", 0, "-", "-");
	}
      else
	{
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IO_MOUNT_FAIL, 1, vol_label_p);
	}
      return NULL_VOLDES;
    }

#if !defined(CS_MODE)
  /* Cache mounting information */
  if (fileio_cache (vol_id, vol_label_p, vol_fd, FILEIO_NOT_LOCKF) != vol_fd)
    {
      fileio_dismount (thread_p, vol_fd);
      return NULL_VOLDES;
    }
#endif /* !CS_MODE */

  return vol_fd;

#else /* WINDOWS */
  int vol_fd;
  int o_sync;
  FILEIO_LOCKF_TYPE lockf_type = FILEIO_NOT_LOCKF;
  bool is_do_wait;
  struct stat stat_buf;
  time_t last_modification_time = 0;
  off_t last_size = 0;

#if !defined(CS_MODE)
  FILEIO_CHECK_AND_INITIALIZE_VOLUME_HEADER_CACHE (NULL_VOLDES);

  /* Is volume already mounted ? */
  vol_fd = fileio_find_volume_descriptor_with_label (vol_label_p);
  if (vol_fd != NULL_VOLDES)
    {
      return vol_fd;
    }
#endif /* !CS_MODE */

  o_sync = (is_do_sync != false) ? O_SYNC : 0;

  /* OPEN THE DISK VOLUME PARTITION OR FILE SIMULATED VOLUME */
start:
  vol_fd = fileio_open (vol_label_p, O_RDWR | o_sync, 0600);
  if (vol_fd == NULL_VOLDES)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IO_MOUNT_FAIL, 1, vol_label_p);
      return NULL_VOLDES;
    }

  is_do_wait = (lock_wait > 1) ? true : false;
  if (is_do_wait)
    {
      if (fstat (vol_fd, &stat_buf) != 0)
	{
	  fileio_close (vol_fd);
	  return NULL_VOLDES;
	}
      last_modification_time = stat_buf.st_mtime;
      last_size = stat_buf.st_size;
    }

  /* LOCK THE DISK */
  if (lock_wait != 0)
    {
      lockf_type = fileio_lock (db_full_name_p, vol_label_p, vol_fd, is_do_wait);
      if (lockf_type == FILEIO_NOT_LOCKF)
	{
	  /* Volume seems to be mounted by someone else */
	  fileio_close (vol_fd);
	  return NULL_VOLDES;
	}
      else if (lockf_type == FILEIO_LOCKF && is_do_wait == true)
	{
	  /* may need to reopen the file */
	  if (fstat (vol_fd, &stat_buf) != 0)
	    {
	      fileio_dismount (thread_p, vol_fd);
	      return NULL_VOLDES;
	    }

	  if (last_modification_time != stat_buf.st_mtime || last_size != stat_buf.st_size)
	    {
	      /* somebody changed the file before the file lock was acquired */
	      fileio_dismount (thread_p, vol_fd);
	      goto start;
	    }
	}
    }

#if !defined(CS_MODE)
  /* Cache mounting information */
  if (fileio_cache (vol_id, vol_label_p, vol_fd, lockf_type) != vol_fd)
    {
      fileio_dismount (thread_p, vol_fd);
      return NULL_VOLDES;
    }
#endif /* !CS_MODE */

  if (prm_get_bool_value (PRM_ID_DBFILES_PROTECT) == true && vol_fd > 0)
    {
      fileio_set_permission (vol_label_p);
    }

  return vol_fd;
#endif /* WINDOWS */
}

/*
 * fileio_dismount () - Dismount the volume associated with the given volume
 *                  descriptor
 *   return: void
 *   vdes(in): Volume descriptor
 */
void
fileio_dismount (THREAD_ENTRY * thread_p, int vol_fd)
{
  const char *vlabel;
#if !defined(WINDOWS)
  FILEIO_LOCKF_TYPE lockf_type;
#endif /* !WINDOWS */
  /* 
   * Make sure that all dirty pages of the volume are forced to disk. This
   * is needed since a close of a file and program exist, does not imply
   * that the dirty pages of the file (or files that the program opened) are
   * forced to disk.
   */
  vlabel = fileio_get_volume_label_by_fd (vol_fd, PEEK);

  (void) fileio_synchronize (thread_p, vol_fd, vlabel);

#if !defined(WINDOWS)
  lockf_type = fileio_get_lockf_type (vol_fd);
  if (lockf_type != FILEIO_NOT_LOCKF)
    {
      fileio_unlock (vlabel, vol_fd, lockf_type);
    }
#endif /* !WINDOWS */

  fileio_close (vol_fd);

  /* Decache volume information even during errors */
  fileio_decache (thread_p, vol_fd);
}

/*
 * fileio_dismount_without_fsync () -
 *   return:
 *   vdes(in):
 */
static void
fileio_dismount_without_fsync (THREAD_ENTRY * thread_p, int vol_fd)
{
#if !defined (WINDOWS)
  FILEIO_LOCKF_TYPE lockf_type;

  lockf_type = fileio_get_lockf_type (vol_fd);
  if (lockf_type != FILEIO_NOT_LOCKF)
    {
      fileio_unlock (fileio_get_volume_label_by_fd (vol_fd, PEEK), vol_fd, lockf_type);
    }
#endif /* !WINDOWS */

  fileio_close (vol_fd);

  /* Decache volume information even during errors */
  fileio_decache (thread_p, vol_fd);
}

static int
fileio_max_permanent_volumes (int index, int num_permanent_volums)
{
  if (index < (num_permanent_volums - 1) / FILEIO_VOLINFO_INCREMENT)
    {
      return FILEIO_VOLINFO_INCREMENT - 1;
    }
  else
    {
      return (num_permanent_volums - 1) % FILEIO_VOLINFO_INCREMENT;
    }
}

static int
fileio_min_temporary_volumes (int index, int num_temp_volums, int num_volinfo_array)
{
  if (index > (num_volinfo_array - 1 - (num_temp_volums - 1) / FILEIO_VOLINFO_INCREMENT))
    {
      return 0;
    }
  else
    {
      return FILEIO_VOLINFO_INCREMENT - 1 - (num_temp_volums - 1) % FILEIO_VOLINFO_INCREMENT;
    }
}

static FILEIO_SYSTEM_VOLUME_INFO *
fileio_traverse_system_volume (THREAD_ENTRY * thread_p, SYS_VOLINFO_APPLY_FN apply_function, APPLY_ARG * arg)
{
  FILEIO_SYSTEM_VOLUME_INFO *sys_vol_info_p;
  int rv;

  rv = pthread_mutex_lock (&fileio_Sys_vol_info_header.mutex);

  for (sys_vol_info_p = &fileio_Sys_vol_info_header.anchor;
       sys_vol_info_p != NULL && sys_vol_info_p->vdes != NULL_VOLDES; sys_vol_info_p = sys_vol_info_p->next)
    {
      if ((*apply_function) (thread_p, sys_vol_info_p, arg) == true)
	{
	  pthread_mutex_unlock (&fileio_Sys_vol_info_header.mutex);
	  return sys_vol_info_p;
	}
    }
  pthread_mutex_unlock (&fileio_Sys_vol_info_header.mutex);

  return NULL;
}

static FILEIO_VOLUME_INFO *
fileio_traverse_permanent_volume (THREAD_ENTRY * thread_p, VOLINFO_APPLY_FN apply_function, APPLY_ARG * arg)
{
  int i, j, max_j;
  FILEIO_VOLUME_HEADER *header_p;
  FILEIO_VOLUME_INFO *vol_info_p;

  header_p = &fileio_Vol_info_header;

  for (i = 0; i <= (header_p->next_perm_volid - 1) / FILEIO_VOLINFO_INCREMENT; i++)
    {
      max_j = fileio_max_permanent_volumes (i, header_p->next_perm_volid);

      for (j = 0; j <= max_j; j++)
	{
	  vol_info_p = &header_p->volinfo[i][j];
	  if ((*apply_function) (thread_p, vol_info_p, arg) == true)
	    {
	      return vol_info_p;
	    }
	}
    }

  return NULL;
}

static FILEIO_VOLUME_INFO *
fileio_reverse_traverse_permanent_volume (THREAD_ENTRY * thread_p, VOLINFO_APPLY_FN apply_function, APPLY_ARG * arg)
{
  int i, j, max_j;
  FILEIO_VOLUME_HEADER *header_p;
  FILEIO_VOLUME_INFO *vol_info_p;

  header_p = &fileio_Vol_info_header;

  for (i = (header_p->next_perm_volid - 1) / FILEIO_VOLINFO_INCREMENT; i >= 0; i--)
    {
      max_j = fileio_max_permanent_volumes (i, header_p->next_perm_volid);

      for (j = max_j; j >= 0; j--)
	{
	  vol_info_p = &header_p->volinfo[i][j];
	  if ((*apply_function) (thread_p, vol_info_p, arg) == true)
	    {
	      return vol_info_p;
	    }
	}
    }

  return NULL;
}

static FILEIO_VOLUME_INFO *
fileio_traverse_temporary_volume (THREAD_ENTRY * thread_p, VOLINFO_APPLY_FN apply_function, APPLY_ARG * arg)
{
  int i, j, min_j, num_temp_vols;
  FILEIO_VOLUME_HEADER *header_p;
  FILEIO_VOLUME_INFO *vol_info_p;

  header_p = &fileio_Vol_info_header;
  num_temp_vols = LOG_MAX_DBVOLID - header_p->next_temp_volid;

  for (i = header_p->num_volinfo_array - 1;
       i > (header_p->num_volinfo_array - 1
	    - (num_temp_vols + FILEIO_VOLINFO_INCREMENT - 1) / FILEIO_VOLINFO_INCREMENT); i--)
    {
      min_j = fileio_min_temporary_volumes (i, num_temp_vols, header_p->num_volinfo_array);

      for (j = FILEIO_VOLINFO_INCREMENT - 1; j >= min_j; j--)
	{
	  vol_info_p = &header_p->volinfo[i][j];
	  if ((*apply_function) (thread_p, vol_info_p, arg) == true)
	    {
	      return vol_info_p;
	    }
	}
    }

  return NULL;
}

static FILEIO_VOLUME_INFO *
fileio_reverse_traverse_temporary_volume (THREAD_ENTRY * thread_p, VOLINFO_APPLY_FN apply_function, APPLY_ARG * arg)
{
  int i, j, min_j, num_temp_vols;
  FILEIO_VOLUME_HEADER *header_p;
  FILEIO_VOLUME_INFO *vol_info_p;

  header_p = &fileio_Vol_info_header;
  num_temp_vols = (LOG_MAX_DBVOLID) - header_p->next_temp_volid;

  for (i = (header_p->num_volinfo_array - ((num_temp_vols + FILEIO_VOLINFO_INCREMENT - 1) / FILEIO_VOLINFO_INCREMENT));
       i < header_p->num_volinfo_array; i++)
    {
      min_j = fileio_min_temporary_volumes (i, num_temp_vols, header_p->num_volinfo_array);

      for (j = min_j; j < FILEIO_VOLINFO_INCREMENT; j++)
	{
	  vol_info_p = &header_p->volinfo[i][j];
	  if ((*apply_function) (thread_p, vol_info_p, arg) == true)
	    {
	      return vol_info_p;
	    }
	}
    }

  return NULL;
}

static bool
fileio_dismount_volume (THREAD_ENTRY * thread_p, FILEIO_VOLUME_INFO * vol_info_p, APPLY_ARG * ignore_arg)
{
  if (vol_info_p->vdes != NULL_VOLDES)
    {
      (void) fileio_synchronize (thread_p, vol_info_p->vdes, vol_info_p->vlabel);

#if !defined(WINDOWS)
      if (vol_info_p->lockf_type != FILEIO_NOT_LOCKF)
	{
	  fileio_unlock (vol_info_p->vlabel, vol_info_p->vdes, vol_info_p->lockf_type);
	}
#endif /* !WINDOWS */

      fileio_close (vol_info_p->vdes);
    }

#if defined(WINDOWS) && defined(SERVER_MODE)
  pthread_mutex_destroy (&vol_info_p->vol_mutex);
#endif /* WINDOWS && SERVER_MODE */

  return false;
}

/*
 * fileio_dismount_all () - Dismount all mounted volumes
 *   return: void
 */
void
fileio_dismount_all (THREAD_ENTRY * thread_p)
{
  FILEIO_SYSTEM_VOLUME_HEADER *sys_header_p;
  FILEIO_SYSTEM_VOLUME_INFO *sys_vol_info_p, *tmp_sys_vol_info_p;
  FILEIO_VOLUME_HEADER *vol_header_p;
  int i, num_perm_vols, num_temp_vols;
  int rv;
  APPLY_ARG ignore_arg = { 0 };

  /* First, traverse sys volumes */
  sys_header_p = &fileio_Sys_vol_info_header;
  rv = pthread_mutex_lock (&sys_header_p->mutex);

  for (sys_vol_info_p = &sys_header_p->anchor; sys_vol_info_p != NULL;)
    {
      if (sys_vol_info_p->vdes != NULL_VOLDES)
	{
	  (void) fileio_synchronize (thread_p, sys_vol_info_p->vdes, sys_vol_info_p->vlabel);

#if !defined(WINDOWS)
	  if (sys_vol_info_p->lockf_type != FILEIO_NOT_LOCKF)
	    {
	      fileio_unlock (sys_vol_info_p->vlabel, sys_vol_info_p->vdes, sys_vol_info_p->lockf_type);
	    }
#endif /* !WINDOWS */

	  fileio_close (sys_vol_info_p->vdes);
	}

      tmp_sys_vol_info_p = sys_vol_info_p;
      sys_vol_info_p = sys_vol_info_p->next;
      if (tmp_sys_vol_info_p != &sys_header_p->anchor)
	{
#if defined(SERVER_MODE) && defined(WINDOWS)
	  pthread_mutex_destroy (&tmp_sys_vol_info_p->sysvol_mutex);

#endif /* WINDOWS */
	  free_and_init (tmp_sys_vol_info_p);
	}
    }

  pthread_mutex_unlock (&sys_header_p->mutex);

  /* Second, traverse perm/temp volumes */
  vol_header_p = &fileio_Vol_info_header;
  rv = pthread_mutex_lock (&vol_header_p->mutex);
  num_perm_vols = vol_header_p->next_perm_volid;

  (void) fileio_traverse_permanent_volume (thread_p, fileio_dismount_volume, &ignore_arg);

  vol_header_p->max_perm_vols = 0;
  vol_header_p->next_perm_volid = 0;

  num_temp_vols = LOG_MAX_DBVOLID - vol_header_p->next_temp_volid;

  (void) fileio_traverse_temporary_volume (thread_p, fileio_dismount_volume, &ignore_arg);

  vol_header_p->max_temp_vols = 0;
  vol_header_p->next_temp_volid = LOG_MAX_DBVOLID;

  if (vol_header_p->volinfo != NULL)
    {
      for (i = 0; i <= (VOLID_MAX - 1) / FILEIO_VOLINFO_INCREMENT; i++)
	{
	  if (vol_header_p->volinfo[i] != NULL)
	    {
	      free_and_init (vol_header_p->volinfo[i]);
	    }

	}
    }

  free_and_init (vol_header_p->volinfo);

  pthread_mutex_unlock (&vol_header_p->mutex);
}

/*
 * fileio_map_mounted () - Map over the data volumes
 *   return:
 *   fun(in): Function to call on volid and args
 *   args(in): argumemts for fun
 *
 * Note : Map over all data volumes (i.e., the log volumes are skipped),
 *        by calling the given function on every volume. If the function
 *        returns false the mapping is stopped.
 */
bool
fileio_map_mounted (THREAD_ENTRY * thread_p, bool (*fun) (THREAD_ENTRY * thread_p, VOLID vol_id, void *args),
		    void *args)
{
  FILEIO_VOLUME_INFO *vol_info_p;
  FILEIO_VOLUME_HEADER *header_p;
  int i, j, max_j, min_j, num_temp_vols;

  FILEIO_CHECK_AND_INITIALIZE_VOLUME_HEADER_CACHE (false);

  header_p = &fileio_Vol_info_header;
  for (i = 0; i <= (header_p->next_perm_volid - 1) / FILEIO_VOLINFO_INCREMENT; i++)
    {
      max_j = fileio_max_permanent_volumes (i, header_p->next_perm_volid);

      for (j = 0; j <= max_j; j++)
	{
	  vol_info_p = &header_p->volinfo[i][j];
	  if (vol_info_p->vdes != NULL_VOLDES)
	    {
	      if (((*fun) (thread_p, vol_info_p->volid, args)) == false)
		{
		  return false;
		}
	    }
	}
    }

  num_temp_vols = LOG_MAX_DBVOLID - header_p->next_temp_volid;
  for (i = header_p->num_volinfo_array - 1;
       i > (header_p->num_volinfo_array - 1
	    - (num_temp_vols + FILEIO_VOLINFO_INCREMENT - 1) / FILEIO_VOLINFO_INCREMENT); i--)
    {
      min_j = fileio_min_temporary_volumes (i, num_temp_vols, header_p->num_volinfo_array);

      for (j = FILEIO_VOLINFO_INCREMENT - 1; j >= min_j; j--)
	{
	  vol_info_p = &header_p->volinfo[i][j];
	  if (vol_info_p->vdes != NULL_VOLDES)
	    {
	      if (((*fun) (thread_p, vol_info_p->volid, args)) == false)
		{
		  return false;
		}
	    }
	}
    }

  return true;
}

static bool
fileio_is_volume_descriptor_equal (THREAD_ENTRY * thread_p, FILEIO_VOLUME_INFO * vol_info_p, APPLY_ARG * arg)
{
  if (vol_info_p->vdes == NULL_VOLDES)
    {
      return false;
    }
  return (vol_info_p->vdes == arg->vdes);
}

static bool
fileio_is_volume_id_equal (THREAD_ENTRY * thread_p, FILEIO_VOLUME_INFO * vol_info_p, APPLY_ARG * arg)
{
  if (vol_info_p->volid == NULL_VOLID)
    {
      return false;
    }
  return (vol_info_p->volid == arg->vol_id);
}

static bool
fileio_is_volume_id_gt (THREAD_ENTRY * thread_p, FILEIO_VOLUME_INFO * vol_info_p, APPLY_ARG * arg)
{
  if (vol_info_p->volid == NULL_VOLID)
    {
      return false;
    }
  return (vol_info_p->volid > arg->vol_id);
}

static bool
fileio_is_volume_id_lt (THREAD_ENTRY * thread_p, FILEIO_VOLUME_INFO * vol_info_p, APPLY_ARG * arg)
{
  if (vol_info_p->volid == NULL_VOLID)
    {
      return false;
    }
  return (vol_info_p->volid < arg->vol_id);
}

static FILEIO_SYSTEM_VOLUME_INFO *
fileio_find_system_volume (THREAD_ENTRY * thread_p, SYS_VOLINFO_APPLY_FN apply_function, APPLY_ARG * arg)
{
  FILEIO_SYSTEM_VOLUME_INFO *sys_vol_info_p;

  for (sys_vol_info_p = &fileio_Sys_vol_info_header.anchor;
       sys_vol_info_p != NULL && sys_vol_info_p->vdes != NULL_VOLDES; sys_vol_info_p = sys_vol_info_p->next)
    {
      if ((*apply_function) (thread_p, sys_vol_info_p, arg) == true)
	{
	  return sys_vol_info_p;
	}
    }

  return NULL;
}

static bool
fileio_is_system_volume_descriptor_equal (THREAD_ENTRY * thread_p, FILEIO_SYSTEM_VOLUME_INFO * sys_vol_info_p,
					  APPLY_ARG * arg)
{
  return (sys_vol_info_p->vdes == arg->vdes);
}

static bool
fileio_is_system_volume_id_equal (THREAD_ENTRY * thread_p, FILEIO_SYSTEM_VOLUME_INFO * sys_vol_info_p, APPLY_ARG * arg)
{
  return (sys_vol_info_p->volid == arg->vol_id);
}

static bool
fileio_is_system_volume_label_equal (THREAD_ENTRY * thread_p, FILEIO_SYSTEM_VOLUME_INFO * sys_vol_info_p,
				     APPLY_ARG * arg)
{
  return (util_compare_filepath (sys_vol_info_p->vlabel, arg->vol_label) == 0);
}

#if defined(HPUX) && !defined(IA64)
/*
 * pread () -
 *   return:
 *   fd(in):
 *   buf(in):
 *   nbytes(in):
 *   offset(in):
 *
 * Note: Like HP-UX 11, the positioned I/O may not directly available in some
 *       systems. In that case, use the following simulated positioned I/O
 *       routines.
 */
ssize_t
pread (int fd, void *buf, size_t nbytes, off_t offset)
{
  struct aiocb io;
  const struct aiocb *list[1];
  int err;

  io.aio_fildes = fd;
  io.aio_offset = offset;
  io.aio_buf = buf;
  io.aio_nbytes = nbytes;
  io.aio_reqprio = 0;
  io.aio_sigevent.sigev_notify = SIGEV_NONE;

  err = aio_read (&io);		/* atomically reads at offset */
  if (err != 0)
    {
      return (err);
    }

  list[0] = &io;

  err = aio_suspend (list, 1, NULL);	/* wait for IO to complete */
  if (err != 0)
    {
      return (err);
    }

  return aio_return (&io);
}

/*
 * pwrite () -
 *   return:
 *   fd(in):
 *   buf(in):
 *   nbytes(in):
 *   offset(in):
 */
ssize_t
pwrite (int fd, const void *buf, size_t nbytes, off_t offset)
{
  struct aiocb io;
  const struct aiocb *list[1];
  int err;

  io.aio_fildes = fd;
  io.aio_offset = offset;
  io.aio_buf = buf;
  io.aio_nbytes = nbytes;
  io.aio_reqprio = 0;
  io.aio_sigevent.sigev_notify = SIGEV_NONE;

  err = aio_write (&io);	/* atomically writes at offset */
  if (err != 0)
    {
      return (err);
    }

  list[0] = &io;

  err = aio_suspend (list, 1, NULL);	/* wait for IO to complete */
  if (err != 0)
    {
      return (err);
    }

  return aio_return (&io);
}
#elif defined(WINDOWS) && defined(SERVER_MODE)
/*
 * fileio_get_volume_mutex () - FIND VOLUME MUTEX GIVEN DESCRIPTOR
 *   return: I/O volume mutex
 *   vdes(in): Volume descriptor
 */
static pthread_mutex_t *
fileio_get_volume_mutex (THREAD_ENTRY * thread_p, int vdes)
{
  FILEIO_VOLUME_INFO *volinfo;
  FILEIO_SYSTEM_VOLUME_INFO *sys_volinfo;
  int rv;
  APPLY_ARG arg = { 0 };

  FILEIO_CHECK_AND_INITIALIZE_VOLUME_HEADER_CACHE (NULL);

  /* perm/temp volume ? */

  arg.vdes = vdes;
  volinfo = fileio_traverse_permanent_volume (thread_p, fileio_is_volume_descriptor_equal, &arg);
  if (volinfo)
    {
      return &volinfo->vol_mutex;
    }

  arg.vdes = vdes;
  volinfo = fileio_traverse_temporary_volume (thread_p, fileio_is_volume_descriptor_equal, &arg);
  if (volinfo)
    {
      return &volinfo->vol_mutex;
    }

  /* sys volume ? */
  rv = pthread_mutex_lock (&fileio_Sys_vol_info_header.mutex);

  arg.vdes = vdes;
  sys_volinfo = fileio_find_system_volume (thread_p, fileio_is_system_volume_descriptor_equal, &arg);
  if (sys_volinfo)
    {
      pthread_mutex_unlock (&fileio_Sys_vol_info_header.mutex);
      return &sys_volinfo->sysvol_mutex;
    }

  pthread_mutex_unlock (&fileio_Sys_vol_info_header.mutex);
  return NULL;
}
#endif /* WINDOWS && SERVER_MODE */

/*
 * fileio_read () - READ A PAGE FROM DISK
 *   return:
 *   vol_fd(in): Volume descriptor
 *   io_page_p(out): Address where content of page is stored. Must be of
 *                   page_size long
 *   page_id(in): Page identifier
 *   page_size(in): Page size
 *
 * Note: Read the content of the page described by page_id onto the
 *       given io_page_p buffer. The io_page_p must be page_size long.
 */
void *
fileio_read (THREAD_ENTRY * thread_p, int vol_fd, void *io_page_p, PAGEID page_id, size_t page_size)
{
#if defined (EnableThreadMonitoring)
  TSC_TICKS start_tick, end_tick;
  TSCTIMEVAL elapsed_time;
#endif
  off_t offset = FILEIO_GET_FILE_SIZE (page_size, page_id);
  ssize_t nbytes;
  bool is_retry = true;

#if defined(WINDOWS) && defined(SERVER_MODE)
  int rv;
  pthread_mutex_t *io_mutex;
  static pthread_mutex_t io_mutex_instance = PTHREAD_MUTEX_INITIALIZER;
#endif /* WINDOWS && SERVER_MODE */
#if defined(USE_AIO)
  struct aiocb cb;
  const struct aiocb *cblist[1];
#endif /* USE_AIO */

#if defined (EnableThreadMonitoring)
  if (0 < prm_get_integer_value (PRM_ID_MNT_WAITING_THREAD))
    {
      tsc_getticks (&start_tick);
    }
#endif

  while (is_retry == true)
    {
      is_retry = false;

#if !defined(SERVER_MODE)
      /* Locate the desired page */
      if (lseek (vol_fd, offset, SEEK_SET) != offset)
	{
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IO_READ, 2, page_id,
			       fileio_get_volume_label (fileio_get_volume_id (vol_fd), PEEK));
	  return NULL;
	}

      /* Read the desired page */
      nbytes = read (vol_fd, io_page_p, (unsigned int) page_size);
      if (nbytes != page_size)
#elif defined(WINDOWS)
      io_mutex = fileio_get_volume_mutex (thread_p, vol_fd);
      if (io_mutex == NULL)
	{
	  io_mutex = &io_mutex_instance;
	}

      rv = pthread_mutex_lock (io_mutex);
      if (rv != 0)
	{
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, rv, 0);
	  return NULL;
	}

      /* Locate the desired page */
      if (lseek (vol_fd, offset, SEEK_SET) != offset)
	{
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IO_READ, 2, page_id,
			       fileio_get_volume_label (fileio_get_volume_id (vol_fd), PEEK));
	  pthread_mutex_unlock (io_mutex);
	  return NULL;
	}

      /* Read the desired page */
      nbytes = read (vol_fd, io_page_p, (unsigned int) page_size);
      if (pthread_mutex_unlock (io_mutex) != 0)
	{
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_PTHREAD_MUTEX_UNLOCK, 0);
	  return NULL;
	}

      if (nbytes != page_size)
#else /* WINDOWS */
#if defined(USE_AIO)
      bzero (&cb, sizeof (cb));
      cb.aio_fildes = vol_fd;
      cb.aio_lio_opcode = LIO_READ;
      cb.aio_buf = io_page_p;
      cb.aio_nbytes = page_size;
      cb.aio_offset = offset;
      cblist[0] = &cb;

      if (aio_read (&cb) < 0)
#else /* USE_AIO */
      nbytes = pread (vol_fd, io_page_p, page_size, offset);
      if (nbytes != (ssize_t) page_size)
#endif /* USE_AIO */
#endif /* WINDOWS */
	{
	  if (nbytes == 0)
	    {
	      /* This is an end of file. We are trying to read beyond the allocated disk space */
	      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_PB_BAD_PAGEID, 2, page_id,
		      fileio_get_volume_label_by_fd (vol_fd, PEEK));

#if defined (SERVER_MODE) && !defined (WINDOWS)
	      syslog (LOG_ALERT, "[CUBRID] %s () at %s:%d %m", __func__, __FILE__, __LINE__);
#endif
	      return NULL;
	    }

	  if (errno == EINTR)
	    {
	      is_retry = true;
	    }
	  else
	    {
	      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IO_READ, 2, page_id,
				   fileio_get_volume_label_by_fd (vol_fd, PEEK));
	      return NULL;
	    }
	}
    }

#if defined (EnableThreadMonitoring)
  if (0 < prm_get_integer_value (PRM_ID_MNT_WAITING_THREAD))
    {
      tsc_getticks (&end_tick);
      tsc_elapsed_time_usec (&elapsed_time, end_tick, start_tick);
    }

  if (MONITOR_WAITING_THREAD (elapsed_time))
    {
      er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, ER_MNT_WAITING_THREAD, 2, "file read",
	      prm_get_integer_value (PRM_ID_MNT_WAITING_THREAD));
      er_log_debug (ARG_FILE_LINE, "fileio_read: %6d.%06d\n", elapsed_time.tv_sec, elapsed_time.tv_usec);
    }
#endif

#if defined(SERVER_MODE) && !defined(WINDOWS) && defined(USE_AIO)
  if (aio_suspend (cblist, 1, NULL) < 0 || aio_return (&cb) != page_size)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IO_READ, 2, page_id,
			   fileio_get_volume_label_by_fd (vol_fd, PEEK));
      return NULL;
    }
#endif

  perfmon_inc_stat (thread_p, PSTAT_FILE_NUM_IOREADS);
  return io_page_p;
}

/*
 * fileio_write () - WRITE A PAGE TO DISK
 *   return: io_page_p on success, NULL on failure
 *   vol_fd(in): Volume descriptor
 *   io_page_p(in): In-memory address where the current content of page resides
 *   page_id(in): Page identifier
 *   page_size(in): Page size
 *
 * Note:  Write the content of the page described by page_id to disk. The
 *        content of the page is stored onto io_page_p buffer which is
 *        page_size long.
 */
void *
fileio_write (THREAD_ENTRY * thread_p, int vol_fd, void *io_page_p, PAGEID page_id, size_t page_size)
{
#if defined (EnableThreadMonitoring)
  TSC_TICKS start_tick, end_tick;
  TSCTIMEVAL elapsed_time;
#endif
  off_t offset = FILEIO_GET_FILE_SIZE (page_size, page_id);
  bool is_retry = true;
#if defined(WINDOWS) && defined(SERVER_MODE)
  int rv, nbytes;
  pthread_mutex_t *io_mutex;
  static pthread_mutex_t io_mutex_instance = PTHREAD_MUTEX_INITIALIZER;
#endif /* WINDOWS && SERVER_MODE */
#if defined(USE_AIO)
  struct aiocb cb;
  const struct aiocb *cblist[1];
#endif /* USE_AIO */

#if defined (EnableThreadMonitoring)
  if (0 < prm_get_integer_value (PRM_ID_MNT_WAITING_THREAD))
    {
      tsc_getticks (&start_tick);
    }
#endif

  while (is_retry == true)
    {
      is_retry = false;

#if !defined(SERVER_MODE)
      if (lseek (vol_fd, offset, SEEK_SET) != offset)
	{
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IO_WRITE, 2, page_id,
			       fileio_get_volume_label (fileio_get_volume_id (vol_fd), PEEK));
	  return NULL;
	}

      /* write the page */
      if (write (vol_fd, io_page_p, (unsigned int) page_size) != page_size)
#elif defined(WINDOWS)
      io_mutex = fileio_get_volume_mutex (thread_p, vol_fd);
      if (io_mutex == NULL)
	{
	  io_mutex = &io_mutex_instance;
	}

      rv = pthread_mutex_lock (io_mutex);
      if (rv != 0)
	{
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, rv, 0);
	  return NULL;
	}

      if (lseek (vol_fd, offset, SEEK_SET) != offset)
	{
	  pthread_mutex_unlock (io_mutex);
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IO_WRITE, 2, page_id,
			       fileio_get_volume_label (fileio_get_volume_id (vol_fd), PEEK));
	  return NULL;
	}

      /* write the page */
      nbytes = write (vol_fd, io_page_p, (unsigned int) page_size);
      pthread_mutex_unlock (io_mutex);

      if (nbytes != page_size)
#else /* WINDOWS */
#if defined(USE_AIO)
      bzero (&cb, sizeof (cb));
      cb.aio_fildes = vol_fd;
      cb.aio_lio_opcode = LIO_WRITE;
      cb.aio_buf = io_page_p;
      cb.aio_nbytes = page_size;
      cb.aio_offset = offset;
      cblist[0] = &cb;

      if (aio_write (&cb) < 0)
#else /* USE_AIO */
      if (pwrite (vol_fd, io_page_p, page_size, offset) != (ssize_t) page_size)
#endif /* USE_AIO */
#endif /* WINDOWS */
	{
	  if (errno == EINTR)
	    {
	      is_retry = true;
	    }
	  else
	    {
	      if (errno == ENOSPC)
		{
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IO_WRITE_OUT_OF_SPACE, 2, page_id,
			  fileio_get_volume_label_by_fd (vol_fd, PEEK));
		}
	      else
		{
		  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IO_WRITE, 2, page_id,
				       fileio_get_volume_label_by_fd (vol_fd, PEEK));
		}
	      return NULL;
	    }
	}
    }

#if defined (EnableThreadMonitoring)
  if (0 < prm_get_integer_value (PRM_ID_MNT_WAITING_THREAD))
    {
      tsc_getticks (&end_tick);
      tsc_elapsed_time_usec (&elapsed_time, end_tick, start_tick);
    }

  if (MONITOR_WAITING_THREAD (elapsed_time))
    {
      er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, ER_MNT_WAITING_THREAD, 2, "file write",
	      prm_get_integer_value (PRM_ID_MNT_WAITING_THREAD));
      er_log_debug (ARG_FILE_LINE, "fileio_write: %6d.%06d\n", elapsed_time.tv_sec, elapsed_time.tv_usec);
    }
#endif

#if defined(SERVER_MODE) && !defined(WINDOWS) && defined(USE_AIO)
  if (aio_suspend (cblist, 1, NULL) < 0 || aio_return (&cb) != page_size)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IO_WRITE, 2, page_id,
			   fileio_get_volume_label_by_fd (vol_fd, PEEK));
      return NULL;
    }
#endif

  fileio_compensate_flush (thread_p, vol_fd, 1);
  perfmon_inc_stat (thread_p, PSTAT_FILE_NUM_IOWRITES);
  return io_page_p;
}

/*
 * fileio_read_pages () -
 */
void *
fileio_read_pages (THREAD_ENTRY * thread_p, int vol_fd, char *io_pages_p, PAGEID page_id, int num_pages,
		   size_t page_size)
{
#if defined (EnableThreadMonitoring)
  TSC_TICKS start_tick, end_tick;
  TSCTIMEVAL elapsed_time;
#endif
  off_t offset;
  ssize_t nbytes;
  size_t read_bytes;
  bool is_retry = true;

#if defined(WINDOWS) && defined(SERVER_MODE)
  int rv;
  pthread_mutex_t *io_mutex;
  static pthread_mutex_t io_mutex_instance = PTHREAD_MUTEX_INITIALIZER;
#endif /* WINDOWS && SERVER_MODE */
#if defined(USE_AIO)
  struct aiocb cb;
  const struct aiocb *cblist[1];
#endif /* USE_AIO */

  assert (num_pages > 0);

  offset = FILEIO_GET_FILE_SIZE (page_size, page_id);
  read_bytes = ((size_t) page_size) * ((size_t) num_pages);

#if defined (EnableThreadMonitoring)
  if (0 < prm_get_integer_value (PRM_ID_MNT_WAITING_THREAD))
    {
      tsc_getticks (&start_tick);
    }
#endif

  while (read_bytes > 0)
    {
#if !defined(SERVER_MODE)
      /* Locate the desired page */
      if (lseek (vol_fd, offset, SEEK_SET) != offset)
	{
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IO_READ, 2, page_id,
			       fileio_get_volume_label (fileio_get_volume_id (vol_fd), PEEK));
	  return NULL;
	}

      /* Read the desired page */
      nbytes = read (vol_fd, io_pages_p, (unsigned int) read_bytes);
#elif defined(WINDOWS)
      io_mutex = fileio_get_volume_mutex (thread_p, vol_fd);
      if (io_mutex == NULL)
	{
	  io_mutex = &io_mutex_instance;
	}

      rv = pthread_mutex_lock (io_mutex);
      if (rv != 0)
	{
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, rv, 0);
	  return NULL;
	}

      /* Locate the desired page */
      if (lseek (vol_fd, offset, SEEK_SET) != offset)
	{
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IO_READ, 2, page_id,
			       fileio_get_volume_label (fileio_get_volume_id (vol_fd), PEEK));
	  pthread_mutex_unlock (io_mutex);
	  return NULL;
	}

      /* Read the desired page */
      nbytes = read (vol_fd, io_pages_p, (unsigned int) read_bytes);
      if (pthread_mutex_unlock (io_mutex) != 0)
	{
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_PTHREAD_MUTEX_UNLOCK, 0);
	  return NULL;
	}
#else /* WINDOWS */
#if defined (USE_AIO)
      bzero (&cb, sizeof (cb));
      cb.aio_fildes = vol_fd;
      cb.aio_lio_opcode = LIO_READ;
      cb.aio_buf = io_pages_p;
      cb.aio_nbytes = read_bytes;
      cb.aio_offset = offset;
      cblist[0] = &cb;

      if (aio_read (&cb) < 0 || aio_suspend (cblist, 1, NULL) < 0)
	{
	  nbytes = -1;
	}
      nbytes = aio_return (&cb);
#else /* USE_AIO */
      nbytes = pread (vol_fd, io_pages_p, read_bytes, offset);
#endif /* USE_AIO */
#endif /* WINDOWS */
      if (nbytes <= 0)
	{
	  if (nbytes == 0)
	    {
	      return NULL;
	    }

	  switch (errno)
	    {
	    case EINTR:
	    case EAGAIN:
	      continue;
#if !defined(WINDOWS)
	    case EOVERFLOW:
	      return NULL;
#endif /* !WINDOWS */
	    default:
	      {
		er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IO_READ, 2, page_id,
				     fileio_get_volume_label_by_fd (vol_fd, PEEK));
		return NULL;
	      }
	    }
	}

      offset += nbytes;
      io_pages_p += nbytes;
      read_bytes -= nbytes;
    }

#if defined (EnableThreadMonitoring)
  if (0 < prm_get_integer_value (PRM_ID_MNT_WAITING_THREAD))
    {
      tsc_getticks (&end_tick);
      tsc_elapsed_time_usec (&elapsed_time, end_tick, start_tick);
    }

  if (MONITOR_WAITING_THREAD (elapsed_time))
    {
      er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, ER_MNT_WAITING_THREAD, 2, "file read",
	      prm_get_integer_value (PRM_ID_MNT_WAITING_THREAD));
      er_log_debug (ARG_FILE_LINE, "fileio_read_pages: %6d.%06d\n", elapsed_time.tv_sec, elapsed_time.tv_usec);
    }
#endif

  perfmon_inc_stat (thread_p, PSTAT_FILE_NUM_IOREADS);
  return io_pages_p;
}

/*
 * fileio_write_pages () -
 */
void *
fileio_write_pages (THREAD_ENTRY * thread_p, int vol_fd, char *io_pages_p, PAGEID page_id, int num_pages,
		    size_t page_size)
{
#if defined (EnableThreadMonitoring)
  TSC_TICKS start_tick, end_tick;
  TSCTIMEVAL elapsed_time;
#endif
  off_t offset;
  ssize_t nbytes;
  size_t write_bytes;

#if defined(WINDOWS) && defined(SERVER_MODE)
  int rv;
  pthread_mutex_t *io_mutex;
  static pthread_mutex_t io_mutex_instance = PTHREAD_MUTEX_INITIALIZER;
#endif /* WINDOWS && SERVER_MODE */
#if defined(USE_AIO)
  struct aiocb cb;
  const struct aiocb *cblist[1];
#endif /* USE_AIO */

  assert (num_pages > 0);

  offset = FILEIO_GET_FILE_SIZE (page_size, page_id);
  write_bytes = ((size_t) page_size) * ((size_t) num_pages);

#if defined (EnableThreadMonitoring)
  if (0 < prm_get_integer_value (PRM_ID_MNT_WAITING_THREAD))
    {
      tsc_getticks (&start_tick);
    }
#endif

  while (write_bytes > 0)
    {
#if !defined(SERVER_MODE)
      if (lseek (vol_fd, offset, SEEK_SET) != offset)
	{
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IO_WRITE, 2, page_id,
			       fileio_get_volume_label (fileio_get_volume_id (vol_fd), PEEK));
	  return NULL;
	}

      /* write the page */
      nbytes = write (vol_fd, io_pages_p, (unsigned int) write_bytes);
#elif defined(WINDOWS)
      io_mutex = fileio_get_volume_mutex (thread_p, vol_fd);
      if (io_mutex == NULL)
	{
	  io_mutex = &io_mutex_instance;
	}

      rv = pthread_mutex_lock (io_mutex);
      if (rv != 0)
	{
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, rv, 0);
	  return NULL;
	}

      /* Locate the desired page */
      if (lseek (vol_fd, offset, SEEK_SET) != offset)
	{
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IO_WRITE, 2, page_id,
			       fileio_get_volume_label (fileio_get_volume_id (vol_fd), PEEK));
	  pthread_mutex_unlock (io_mutex);
	  return NULL;
	}

      /* Write the desired page */
      nbytes = write (vol_fd, io_pages_p, (unsigned int) write_bytes);
      if (pthread_mutex_unlock (io_mutex) != 0)
	{
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_PTHREAD_MUTEX_UNLOCK, 0);
	  return NULL;
	}
#else /* WINDOWS */
#if defined (USE_AIO)
      bzero (&cb, sizeof (cb));
      cb.aio_fildes = vol_fd;
      cb.aio_lio_opcode = LIO_WRITE;
      cb.aio_buf = io_pages_p;
      cb.aio_nbytes = write_bytes;
      cb.aio_offset = offset;
      cblist[0] = &cb;

      if (aio_write (&cb) < 0 || aio_suspend (cblist, 1, NULL) < 0)
	{
	  nbytes = -1;
	}
      nbytes = aio_return (&cb);
#else /* USE_AIO */
      nbytes = pwrite (vol_fd, io_pages_p, write_bytes, offset);
#endif /* USE_AIO */
#endif /* WINDOWS */
      if (nbytes <= 0)
	{
	  if (nbytes == 0)
	    {
	      return NULL;
	    }

	  switch (errno)
	    {
	    case EINTR:
	    case EAGAIN:
	      continue;
#if !defined(WINDOWS)
	    case EOVERFLOW:
	      return NULL;
#endif /* !WINDOWS */
	    default:
	      {
		er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IO_WRITE, 2, page_id,
				     fileio_get_volume_label_by_fd (vol_fd, PEEK));
		return NULL;
	      }
	    }
	}

      offset += nbytes;
      io_pages_p += nbytes;
      write_bytes -= nbytes;
    }

#if defined (EnableThreadMonitoring)
  if (0 < prm_get_integer_value (PRM_ID_MNT_WAITING_THREAD))
    {
      tsc_getticks (&end_tick);
      tsc_elapsed_time_usec (&elapsed_time, end_tick, start_tick);
    }

  if (MONITOR_WAITING_THREAD (elapsed_time))
    {
      er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, ER_MNT_WAITING_THREAD, 2, "file write",
	      prm_get_integer_value (PRM_ID_MNT_WAITING_THREAD));
      er_log_debug (ARG_FILE_LINE, "fileio_write_pages: %6d.%06d\n", elapsed_time.tv_sec, elapsed_time.tv_usec);
    }
#endif

  fileio_compensate_flush (thread_p, vol_fd, num_pages);
  perfmon_add_stat (thread_p, PSTAT_FILE_NUM_IOWRITES, num_pages);
  return io_pages_p;
}

/*
 * fileio_writev () - WRITE A SET OF CONTIGUOUS PAGES TO DISK
 *   return: io_pgptr on success, NULL on failure
 *   vol_fd(in): Volume descriptor
 *   arrayof_io_pgptr(in): An array address to address where the current
 *                         content of pages reside
 *   start_page_id(in): Page identifier of first page
 *   npages(in): Number of consecutive pages
 *   page_size(in): Page size
 *
 * Note: Write the content of the consecutive pages described by
 *       start_pageid to disk. The content of the pages are address
 *       by the io_pgptr array. Each io_pgptr buffer is page size
 *       long.
 *
 *            io_pgptr[0]  -->> start_pageid
 *            io_pgptr[1]  -->> start_pageid + 1
 *                        ...
 *            io_pgptr[npages - 1] -->> start_pageid + npages - 1
 */
void *
fileio_writev (THREAD_ENTRY * thread_p, int vol_fd, void **io_page_array, PAGEID start_page_id, DKNPAGES npages,
	       size_t page_size)
{
  int i;

  for (i = 0; i < npages; i++)
    {
      if (fileio_write (thread_p, vol_fd, io_page_array[i], start_page_id + i, page_size) == NULL)
	{
	  return NULL;
	}
    }

  return io_page_array[0];
}

/*
 * fileio_synchronize () - Synchronize a database volume's state with that on disk
 *   return: vdes or NULL_VOLDES
 *   vol_fd(in): Volume descriptor
 *   vlabel(in): Volume label
 */
int
fileio_synchronize (THREAD_ENTRY * thread_p, int vol_fd, const char *vlabel)
{
  int ret;
#if defined (EnableThreadMonitoring)
  TSC_TICKS start_tick, end_tick;
  TSCTIMEVAL elapsed_time;
#endif
#if defined (SERVER_MODE)
  static pthread_mutex_t inc_cnt_mutex = PTHREAD_MUTEX_INITIALIZER;
  int r;
#endif
  static int inc_cnt = 0;
#if defined(USE_AIO)
  struct aiocb cb;
#endif /* USE_AIO */

  if (prm_get_integer_value (PRM_ID_SUPPRESS_FSYNC) > 0)
    {
#if defined (SERVER_MODE)
      r = pthread_mutex_lock (&inc_cnt_mutex);
#endif
      if (++inc_cnt >= prm_get_integer_value (PRM_ID_SUPPRESS_FSYNC))
	{
	  inc_cnt = 0;
	}
      else
	{
#if defined (SERVER_MODE)
	  pthread_mutex_unlock (&inc_cnt_mutex);
#endif
	  return vol_fd;
	}
#if defined (SERVER_MODE)
      pthread_mutex_unlock (&inc_cnt_mutex);
#endif
    }

#if defined (EnableThreadMonitoring)
  if (0 < prm_get_integer_value (PRM_ID_MNT_WAITING_THREAD))
    {
      tsc_getticks (&start_tick);
    }
#endif

#if defined(SERVER_MODE) && !defined(WINDOWS) && defined(USE_AIO)
  bzero (&cb, sizeof (cb));
  cb.aio_fildes = vol_fd;
  ret = aio_fsync (O_SYNC, &cb);
#else /* USE_AIO */
  ret = fsync (vol_fd);
#endif /* USE_AIO */

#if defined (EnableThreadMonitoring)
  if (0 < prm_get_integer_value (PRM_ID_MNT_WAITING_THREAD))
    {
      tsc_getticks (&end_tick);
      tsc_elapsed_time_usec (&elapsed_time, end_tick, start_tick);
    }
#endif

  if (ret != 0)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IO_SYNC, 1, (vlabel ? vlabel : "Unknown"));
      return NULL_VOLDES;
    }
  else
    {
#if defined (EnableThreadMonitoring)
      if (MONITOR_WAITING_THREAD (elapsed_time))
	{
	  er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, ER_MNT_WAITING_THREAD, 2, "file sync",
		  prm_get_integer_value (PRM_ID_MNT_WAITING_THREAD));
	  er_log_debug (ARG_FILE_LINE, "fileio_synchronize: %6d.%06d\n", elapsed_time.tv_sec, elapsed_time.tv_usec);
	}
#endif

#if defined(SERVER_MODE) && !defined(WINDOWS) && defined(USE_AIO)
      while (aio_error (&cb) == EINPROGRESS)
	;
      if (aio_return (&cb) != 0)
	{
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IO_SYNC, 1, (vlabel ? vlabel : "Unknown"));
	  return NULL_VOLDES;
	}
#endif

      perfmon_inc_stat (thread_p, PSTAT_FILE_NUM_IOSYNCHES);
      return vol_fd;
    }
}

/*
 * fileio_synchronize_bg_archive_volume () -
 *   return:
 */
static int
fileio_synchronize_bg_archive_volume (THREAD_ENTRY * thread_p)
{
  APPLY_ARG arg = { 0 };

  arg.vol_id = LOG_DBLOG_BG_ARCHIVE_VOLID;
  (void) fileio_traverse_system_volume (thread_p, fileio_synchronize_sys_volume, &arg);
  return NO_ERROR;
}

/*
 * fileio_synchronize_sys_volume () -
 *   return:
 *   vol_info_p(in):
 */
static bool
fileio_synchronize_sys_volume (THREAD_ENTRY * thread_p, FILEIO_SYSTEM_VOLUME_INFO * sys_vol_info_p, APPLY_ARG * arg)
{
  bool found = false;

  if (sys_vol_info_p->vdes != NULL_VOLDES)
    {
      /* sync when match is found or arg.vol_id is given as NULL_VOLID for all sys volumes. */
      if (arg->vol_id == NULL_VOLID)
	{
	  /* fall through */
	  ;
	}
      else if (sys_vol_info_p->volid == arg->vol_id)
	{
	  found = true;
	}
      else
	{
	  /* irrelevant volume */
	  return false;
	}

      fileio_synchronize (thread_p, sys_vol_info_p->vdes, sys_vol_info_p->vlabel);
    }

  return found;
}

/*
 * fileio_synchronize_volume () -
 *   return:
 *   vol_info_p(in):
 */
static bool
fileio_synchronize_volume (THREAD_ENTRY * thread_p, FILEIO_VOLUME_INFO * vol_info_p, APPLY_ARG * arg)
{
  bool found = false;

  if (vol_info_p->vdes != NULL_VOLDES)
    {
      /* sync when match is found or arg.vol_id is given as NULL_VOLID for all sys volumes. */
      if (arg->vol_id == NULL_VOLID)
	{
	  /* fall through */
	  ;
	}
      else if (vol_info_p->volid == arg->vol_id)
	{
	  found = true;
	}
      else
	{
	  /* irrelevant volume */
	  return false;
	}

      fileio_synchronize (thread_p, vol_info_p->vdes, vol_info_p->vlabel);
    }

  return found;
}

/*
 * fileio_synchronize_all () - Synchronize all database volumes with disk
 *   return:
 *   include_log(in):
 */
int
fileio_synchronize_all (THREAD_ENTRY * thread_p, bool is_include)
{
  int success = NO_ERROR;
  APPLY_ARG arg = { 0 };
  PERF_UTIME_TRACKER time_track;

  PERF_UTIME_TRACKER_START (thread_p, &time_track);

  arg.vol_id = NULL_VOLID;

  er_stack_push ();

  if (is_include)
    {
      (void) fileio_traverse_system_volume (thread_p, fileio_synchronize_sys_volume, &arg);
    }

  (void) fileio_traverse_permanent_volume (thread_p, fileio_synchronize_volume, &arg);

  if (er_errid () == ER_IO_SYNC)
    {
      success = ER_FAILED;
    }

  er_stack_pop ();

  PERF_UTIME_TRACKER_TIME (thread_p, &time_track, PSTAT_FILE_IOSYNC_ALL);

  return success;
}

#if defined(ENABLE_UNUSED_FUNCTION)
/*
 * fileio_read_user_area () - READ A PORTION OF THE USER AREA OF THE GIVEN PAGE
 *   return: area on success, NULL on failure
 *   vdes(in): Volume descriptor
 *   pageid(in): Page identifier
 *   start_offset(in): Start offset of interested content in page
 *   nbytes(in): Length of the content of page to copy
 *   area(out):
 *
 * Note: Copy a portion of the content of the user area of the page described
 *       by pageid onto the given area. The area must be big enough to hold
 *       the needed content
 */
void *
fileio_read_user_area (THREAD_ENTRY * thread_p, int vol_fd, PAGEID page_id, off_t start_offset, size_t nbytes,
		       void *area_p)
{
  off_t offset;
  bool is_retry = true;
  FILEIO_PAGE *io_page_p;
#if defined(WINDOWS) && defined(SERVER_MODE)
  pthread_mutex_t io_mutex;
  int rv;
  int actual_nread;
#endif /* WINDOWS && SERVER_MODE */

  io_page_p = (FILEIO_PAGE *) malloc (IO_PAGESIZE);
  if (io_page_p == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) IO_PAGESIZE);
      return NULL;
    }

  /* Find the offset intop the user area on the desired page */
  offset = FILEIO_GET_FILE_SIZE (IO_PAGESIZE, page_id);

  while (is_retry == true)
    {
      is_retry = false;

#if !defined(SERVER_MODE)
      /* Locate the desired page */
      if (lseek (vol_fd, offset, SEEK_SET) != offset)
	{
	  if (io_page_p != NULL)
	    {
	      free_and_init (io_page_p);
	    }

	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IO_READ, 2, page_id,
			       fileio_get_volume_label (fileio_get_volume_id (vol_fd), PEEK));
	  return NULL;
	}

      /* Read the desired page */
      if (read (vol_fd, io_page_p, IO_PAGESIZE) != IO_PAGESIZE)
#elif defined(WINDOWS)
      io_mutex = fileio_get_volume_mutex (thread_p, vol_fd);
      rv = pthread_mutex_lock (&io_mutex);
      if (rv != 0)
	{
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, rv, 0);
	  return NULL;
	}

      /* Locate the desired page */
      if (lseek (vol_fd, offset, SEEK_SET) != offset)
	{
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IO_READ, 2, page_id,
			       fileio_get_volume_label (fileio_get_volume_id (vol_fd), PEEK));
	  pthread_mutex_unlock (&io_mutex);
	  return NULL;
	}

      /* Read the desired page */
      actual_nread = read (vol_fd, io_page_p, IO_PAGESIZE);
      if (pthread_mutex_unlock (&io_mutex) != 0)
	{
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_PTHREAD_MUTEX_UNLOCK, 0);
	  return NULL;
	}
      if (actual_nread != IO_PAGESIZE)
#else /* WINDOWS */
      if (pread (vol_fd, io_page_p, IO_PAGESIZE, offset) != IO_PAGESIZE)
#endif /* WINDOWS */
	{
	  if (errno == EINTR)
	    {
	      is_retry = true;
	    }
	  else
	    {
	      if (io_page_p != NULL)
		{
		  free_and_init (io_page_p);
		}

	      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IO_READ, 2, page_id,
				   fileio_get_volume_label_by_fd (vol_fd, PEEK));
	      return NULL;
	    }
	}
    }

  memcpy (area_p, io_page_p->page + start_offset, nbytes);

  if (io_page_p != NULL)
    {
      free_and_init (io_page_p);
    }

  perfmon_inc_stat (thread_p, PSTAT_FILE_NUM_IOREADS);
  return area_p;
}

/*
 * fileio_write_user_area () - READ A PORTION OF THE USER AREA OF THE GIVEN PAGE
 *   return: area on success, NULL on failure
 *   vdes(in): Volume descriptor
 *   pageid(in): Page identifier
 *   start_offset(in): Start offset of interested content in page
 *   nbytes(in): Length of the content of page to copy
 *   area(out):
 *
 * Note: Copy a portion of the content of the user area of the page described
 *       by pageid onto the given area. The area must be big enough to hold
 *       the needed content
 */
void *
fileio_write_user_area (THREAD_ENTRY * thread_p, int vol_fd, PAGEID page_id, off_t start_offset, int nbytes,
			void *area_p)
{
  off_t offset;
  bool is_retry = true;
  FILEIO_PAGE *io_page_p = NULL;
  void *write_p;
  struct stat stat_buf;
#if defined(WINDOWS) && defined(SERVER_MODE)
  int actual_nwrite, rv;
  pthread_mutex_t io_mutex;
#endif /* WINDOWS && SERVER_MODE */

  if (fstat (vol_fd, &stat_buf) != 0)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IO_WRITE, 2, page_id,
			   fileio_get_volume_label_by_fd (vol_fd, PEEK));
      return NULL;
    }

#if defined(WINDOWS)
  /* Find the offset intop the user area on the desired page */
  offset = (FILEIO_GET_FILE_SIZE (IO_PAGESIZE, page_id) + offsetof (FILEIO_PAGE, page));

  /* Add the starting offset */
  offset += start_offset;

  write_p = area_p;
#else /* WINDOWS */
  if (S_ISREG (stat_buf.st_mode))	/* regular file */
    {
      /* Find the offset intop the user area on the desired page */
      offset = (FILEIO_GET_FILE_SIZE (IO_PAGESIZE, page_id) + offsetof (FILEIO_PAGE, page));

      /* Add the starting offset */
      offset += start_offset;

      write_p = area_p;

    }
  else if (S_ISCHR (stat_buf.st_mode))	/* Raw device */
    {
      offset = FILEIO_GET_FILE_SIZE (IO_PAGESIZE, page_id);
      if (nbytes != DB_PAGESIZE)
	{
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IO_WRITE, 2, page_id,
			       fileio_get_volume_label_by_fd (vol_fd, PEEK));
	  return NULL;

	}

      io_page_p = (FILEIO_PAGE *) malloc (IO_PAGESIZE);
      if (io_page_p == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) IO_PAGESIZE);
	  return NULL;
	}

      (void) fileio_initialize_res (thread_p, &(io_page_p->prv));
      memcpy (io_page_p->page, area_p, nbytes);

      write_p = (void *) io_page_p;
      nbytes = IO_PAGESIZE;

    }
  else
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IO_WRITE, 2, page_id,
			   fileio_get_volume_label_by_fd (vol_fd, PEEK));
      return NULL;
    }
#endif /* WINDOWS */

  while (is_retry == true)
    {
      is_retry = false;

#if !defined(SERVER_MODE)
      /* Locate the desired page */
      if (lseek (vol_fd, offset, SEEK_SET) != offset)
	{
	  if (io_page_p != NULL)
	    {
	      free_and_init (io_page_p);
	    }

	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IO_WRITE, 2, page_id,
			       fileio_get_volume_label (fileio_get_volume_id (vol_fd), PEEK));
	  return NULL;
	}

      /* Write desired portion to page */
      if (write (vol_fd, write_p, nbytes) != nbytes)
#elif defined(WINDOWS)
      io_mutex = fileio_get_volume_mutex (thread_p, vol_fd);
      rv = pthread_mutex_lock (&io_mutex);
      if (rv != 0)
	{
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, rv, 0);
	  return NULL;
	}
      /* Locate the desired page */
      if (lseek (vol_fd, offset, SEEK_SET) != offset)
	{
	  pthread_mutex_unlock (&io_mutex);
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IO_WRITE, 2, page_id,
			       fileio_get_volume_label (fileio_get_volume_id (vol_fd), PEEK));
	  return NULL;
	}

      /* write the page */

      actual_nwrite = write (vol_fd, write_p, (int) nbytes);
      pthread_mutex_unlock (&io_mutex);
      if (actual_nwrite != nbytes)
#else /* WINDOWS */
      if (pwrite (vol_fd, write_p, nbytes, offset) != nbytes)
#endif /* WINDOWS */
	{
	  if (errno == EINTR)
	    {
	      is_retry = true;
	    }
	  else
	    {
	      if (errno == ENOSPC)
		{
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IO_WRITE_OUT_OF_SPACE, 2, page_id,
			  fileio_get_volume_label_by_fd (vol_fd, PEEK));
		}
	      else
		{
		  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IO_WRITE, 2, page_id,
				       fileio_get_volume_label_by_fd (vol_fd, PEEK));
		}

	      if (io_page_p != NULL)
		{
		  free_and_init (io_page_p);
		}

	      return NULL;
	    }
	}
    }

  if (io_page_p != NULL)
    {
      free_and_init (io_page_p);
    }

  fileio_compensate_flush (thread_p, vol_fd, 1);
  perfmon_inc_stat (thread_p, PSTAT_FILE_NUM_IOWRITES);
  return area_p;
}
#endif

/*
 * fileio_get_number_of_volume_pages () - Find the size of the volume in number of pages
 *   return: Num pages
 *   vol_fd(in): Volume descriptor
 */
DKNPAGES
fileio_get_number_of_volume_pages (int vol_fd, size_t page_size)
{
  off_t offset;

  offset = lseek (vol_fd, 0L, SEEK_END);
  return (DKNPAGES) (offset / page_size);

}

/*
 * fileio_get_number_of_partition_free_pages () - Find the number of free pages in the given
 *                               OS disk partition
 *   return: number of free pages
 *   path(in): Path to disk partition
 *
 * Note: The number of pages is in the size of the database system not
 *       the size of the OS system.
 */
int
fileio_get_number_of_partition_free_pages (const char *path_p, size_t page_size)
{
#if defined(WINDOWS)
  return (free_space (path_p, (int) IO_PAGESIZE));
#else /* WINDOWS */
  int vol_fd;
  INT64 npages_of_partition = -1;
#if defined(SOLARIS)
  struct statvfs buf;
#else /* SOLARIS */
  struct statfs buf;
#endif /* SOLARIS */

#if defined(SOLARIS)
  if (statvfs (path_p, &buf) == -1)
#elif defined(AIX)
  if (statfs ((char *) path_p, &buf) == -1)
#else /* AIX */
  if (statfs (path_p, &buf) == -1)
#endif /* AIX */
    {
      if (errno == ENOENT
	  && ((vol_fd = fileio_open (path_p, FILEIO_DISK_FORMAT_MODE, FILEIO_DISK_PROTECTION_MODE)) != NULL_VOLDES))
	{
	  /* The given file did not exist. We create it for temporary consumption then it is removed */
	  npages_of_partition = fileio_get_number_of_partition_free_pages (path_p, page_size);

	  /* Close the file and remove it */
	  fileio_close (vol_fd);
	  (void) remove (path_p);
	}
      else
	{
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IO_MOUNT_FAIL, 1, path_p);
	}
    }
  else
    {
      const size_t f_avail_size = buf.f_bsize * buf.f_bavail;
      npages_of_partition = f_avail_size / page_size;
      if (npages_of_partition < 0 || npages_of_partition > INT_MAX)
	{
	  npages_of_partition = INT_MAX;
	}
    }

  if (npages_of_partition < 0)
    {
      return -1;
    }
  else
    {
      assert (npages_of_partition <= INT_MAX);

      return (int) npages_of_partition;
    }
#endif /* WINDOWS */
}

/*
 * fileio_get_number_of_partition_free_pages () - document me!
 *
 * return      : Number of free sectors
 * path_p (in) : Path to disk partition
 */
DKNSECTS
fileio_get_number_of_partition_free_sectors (const char *path_p)
{
#if defined(WINDOWS)
  return (DKNSECTS) free_space (path_p, IO_SECTORSIZE);
#else /* WINDOWS */
  int vol_fd;
  INT64 nsectors_of_partition = -1;
#if defined(SOLARIS)
  struct statvfs buf;
#else /* SOLARIS */
  struct statfs buf;
#endif /* SOLARIS */

#if defined(SOLARIS)
  if (statvfs (path_p, &buf) == -1)
#elif defined(AIX)
  if (statfs ((char *) path_p, &buf) == -1)
#else /* AIX */
  if (statfs (path_p, &buf) == -1)
#endif /* AIX */
    {
      if (errno == ENOENT
	  && ((vol_fd = fileio_open (path_p, FILEIO_DISK_FORMAT_MODE, FILEIO_DISK_PROTECTION_MODE)) != NULL_VOLDES))
	{
	  /* The given file did not exist. We create it for temporary consumption then it is removed */
	  nsectors_of_partition = fileio_get_number_of_partition_free_sectors (path_p);

	  /* Close the file and remove it */
	  fileio_close (vol_fd);
	  (void) remove (path_p);
	}
      else
	{
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IO_MOUNT_FAIL, 1, path_p);
	}
    }
  else
    {
      const size_t io_sectorsize_in_block = IO_SECTORSIZE / buf.f_bsize;
      nsectors_of_partition = buf.f_bavail / io_sectorsize_in_block;
      if (nsectors_of_partition < 0 || nsectors_of_partition > INT_MAX)
	{
	  nsectors_of_partition = INT_MAX;
	}
    }

  if (nsectors_of_partition < 0)
    {
      return -1;
    }
  else
    {
      assert (nsectors_of_partition <= INT_MAX);

      return (DKNSECTS) nsectors_of_partition;
    }
#endif /* WINDOWS */
}

/*
 * fileio_rename () - Rename the volume from "old_vlabel" to "new_vlabel"
 *   return: new_vlabel or NULL in case of error
 *   volid(in): Volume Identifier
 *   old_vlabel(in): Old volume label
 *   new_vlabel(in): New volume label
 */
const char *
fileio_rename (VOLID vol_id, const char *old_label_p, const char *new_label_p)
{
#if defined(CUBRID_DEBUG)
  if (fileio_get_volume_descriptor (vol_id) != NULL_VOLDES)
    {
      er_log_debug (ARG_FILE_LINE, "io_rename: SYSTEM ERROR..The volume %s must be dismounted to rename a volume...");
      return NULL;
    }
#endif /* CUBRID_DEBUG */

  if (os_rename_file (old_label_p, new_label_p) != NO_ERROR)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IO_RENAME_FAIL, 2, old_label_p, new_label_p);
      return NULL;
    }
  return new_label_p;
}

/*
 * fileio_is_volume_exist () - Find if a volume exist
 *   return: true/false
 *   vlabel(in): Volume label
 */
bool
fileio_is_volume_exist (const char *vol_label_p)
{
  int vol_fd;

#if !defined(CS_MODE)
  /* Is volume already mounted ? */
  vol_fd = fileio_find_volume_descriptor_with_label (vol_label_p);
  if (vol_fd != NULL_VOLDES)
    {
      return true;
    }
#endif /* !CS_MODE */

  /* Check the existance of the file by opening the file */
  vol_fd = fileio_open (vol_label_p, O_RDONLY, 0);
  if (vol_fd == NULL_VOLDES)
    {
      if (errno == ENOENT)
	{
	  return false;
	}
    }
  else
    {
      fileio_close (vol_fd);
    }

  return true;
}

/*
 * fileio_is_volume_exist_and_file () - Find if a volume exist and is a regular file
 *   return: true/false
 *   vlabel(in): Volume label
 *
 * Note:  This is to differentiate between directories, raw devices, and files.
 */
bool
fileio_is_volume_exist_and_file (const char *vol_label_p)
{
  int vol_fd;
  struct stat stbuf;

  /* Is volume already mounted ? */
  vol_fd = fileio_find_volume_descriptor_with_label (vol_label_p);
  if (vol_fd != NULL_VOLDES)
    {
      return true;
    }

  if (stat (vol_label_p, &stbuf) != -1 && S_ISREG (stbuf.st_mode))
    {
      return true;
    }

  return false;
}

static char *
fileio_check_file_exist (char *name_p, char *new_guess_path_p, int check_size, int *max_name_size_p)
{
  char *tmp_name_p;
  int vol_fd = NULL_VOLDES;

  tmp_name_p = name_p - (*max_name_size_p - check_size + 1);
  *tmp_name_p = '\0';

  vol_fd = fileio_open (new_guess_path_p, O_RDONLY, 0);
  if (vol_fd == NULL_VOLDES)
    {
      vol_fd = fileio_open (new_guess_path_p, FILEIO_DISK_FORMAT_MODE, FILEIO_DISK_PROTECTION_MODE);
      if (vol_fd == NULL_VOLDES && errno == ENAMETOOLONG)
	{
	  *max_name_size_p = check_size + 1;
	  name_p = tmp_name_p;
	}
      else
	{
	  if (vol_fd != NULL_VOLDES)
	    {
	      fileio_close (vol_fd);
	      (void) remove (new_guess_path_p);
	    }
	  *tmp_name_p = 'x';
	}
    }
  else
    {
      *tmp_name_p = 'x';
      if (vol_fd != NULL_VOLDES)
	{
	  fileio_close (vol_fd);
	}
    }

  return name_p;
}

static char *
fileio_check_file_is_same (char *name_p, char *new_guess_path_p, int check_size, int *max_name_size_p, struct stat *buf)
{
  char *tmp_name_p;

  tmp_name_p = name_p - (*max_name_size_p - check_size + 1);
  *tmp_name_p = '\0';

  if (stat (new_guess_path_p, &buf[1]) == 0 && buf[0].st_ino == buf[1].st_ino)
    {
      *max_name_size_p = check_size + 1;
      name_p = tmp_name_p;
    }
  else
    {
      *tmp_name_p = 'x';
    }

  return name_p;
}

/*
 * fileio_get_primitive_way_max () - Find the longest names of files and
 *                                          path names that can be given for
 *                                          the given file system (path) in a
 *                                          primitive way
 *   return: filename max
 *   path(in): Path to directory or file name
 *   filename_max(out): the longest name that could be given
 *   pathname_max(out): the longest path that could be given
 *
 * Note: This function should only be used when the values cannot be
 *       determine using pathconf.
 */
static int
fileio_get_primitive_way_max (const char *path_p, long int *file_name_max_p, long int *path_name_max_p)
{
  static char last_guess_path[PATH_MAX] = { '\0' };
  static int max_name_size = -1;
  char new_guess_path[PATH_MAX];
  char *name_p;
  int check256, check14;
  bool is_remove = false;
  int vol_fd = NULL_VOLDES;
  struct stat buf[2];
  int i;
  int success;

  *file_name_max_p = NAME_MAX;
  *path_name_max_p = PATH_MAX;

  if (*file_name_max_p > *path_name_max_p)
    {
      *file_name_max_p = *path_name_max_p;
    }

  /* Verify the above compilation guesses */

  strncpy (new_guess_path, path_p, PATH_MAX);
  name_p = strrchr (new_guess_path, '/');
#if defined(WINDOWS)
  {
    char *tmp_name = strrchr (new_guess_path, '\\');
    if (name_p < tmp_name)
      name_p = tmp_name;
  }
#endif /* WINDOWS */

  if (name_p != NULL)
    {
      *++name_p = '\0';
    }
  else
    {
      name_p = new_guess_path;
    }

  if (max_name_size != -1 && strcmp (last_guess_path, new_guess_path) == 0)
    {
      return *file_name_max_p = max_name_size;
    }

  for (max_name_size = 1, i = (int) strlen (new_guess_path) + 1;
       max_name_size < *file_name_max_p && i < *path_name_max_p; max_name_size++, i++)
    {
      *name_p++ = 'x';
    }

  *name_p++ = '\0';

  /* Start from the back until you find a file which is different. The assumption is that the files do not exist. */

  check256 = 1;
  check14 = 1;
  while (max_name_size > 1)
    {
      vol_fd = fileio_open (new_guess_path, O_RDONLY, 0);
      if (vol_fd != NULL_VOLDES)
	{
	  /* The file already exist */
	  is_remove = false;
	  break;
	}
      else
	{
	  /* The file did not exist. Create the file and at the end remove the file */
	  is_remove = true;
	  vol_fd = fileio_open (new_guess_path, FILEIO_DISK_FORMAT_MODE, FILEIO_DISK_PROTECTION_MODE);
	  if (vol_fd != NULL_VOLDES)
	    {
	      break;
	    }

	  if (errno != ENAMETOOLONG)
	    {
	      goto error;
	    }

	  /* 
	   * Name truncation is not allowed. Most Unix systems accept
	   * filename of 256 or 14.
	   * Assume one of this for now
	   */
	  if (max_name_size > 257 && check256 == 1)
	    {
	      check256 = 0;
	      name_p = fileio_check_file_exist (name_p, new_guess_path, 256, &max_name_size);
	    }
	  else if (max_name_size > 15 && check14 == 1)
	    {
	      check14 = 0;
	      name_p = fileio_check_file_exist (name_p, new_guess_path, 14, &max_name_size);
	    }
	  *name_p-- = '\0';
	  max_name_size--;
	}
    }

  strncpy (last_guess_path, new_guess_path, PATH_MAX);

  if (vol_fd != NULL_VOLDES)
    {
      fileio_close (vol_fd);
      if (stat (new_guess_path, &buf[0]) == -1)
	{
	  goto error;
	}
    }
  else
    {
      goto error;
    }

  /* 
   * Most Unix system are either 256 or 14. Do a quick check to see if 15
   * is the same than current value. If it is, set maxname to 15 and decrement
   * name.
   */

  check256 = 1;
  check14 = 1;
  for (; max_name_size > 1; max_name_size--)
    {
      *name_p-- = '\0';
      if ((success = stat (new_guess_path, &buf[1])) == 0 && buf[0].st_ino == buf[1].st_ino)
	{
	  /* 
	   * Same file. Most Unix system allow either 256 or 14 for filenames.
	   * Perform a quick check to see if we can speed up the checking
	   * process
	   */

	  if (max_name_size > 257 && check256 == 1)
	    {
	      check256 = 0;
	      name_p = fileio_check_file_is_same (name_p, new_guess_path, 256, &max_name_size, buf);
	      /* Check if the name with 257 is the same. If it is advance the to 256 */
	    }
	  else if (max_name_size > 15 && check14 == 1)
	    {
	      check14 = 0;
	      name_p = fileio_check_file_is_same (name_p, new_guess_path, 14, &max_name_size, buf);
	    }
	}
      else
	{
	  if (success == 0)
	    {
	      continue;
	    }
	  else if (errno == ENOENT)
	    {
	      /* The file did not exist or the file is different. Therefore, previous maxname is the maximum name */
	      max_name_size++;
	      break;
	    }

	  goto error;
	}
    }

  /* The length has been found */
  if (is_remove == true)
    {
      (void) remove (last_guess_path);
    }

  name_p = strrchr (last_guess_path, '/');
#if defined(WINDOWS)
  {
    char *tmp_name = strrchr (last_guess_path, '\\');
    if (name_p < tmp_name)
      {
	name_p = tmp_name;
      }
  }
#endif /* WINDOWS */
  if (name_p != NULL)
    {
      *++name_p = '\0';
    }

  /* Plus 2 since we start with zero and we need to include null character */
  max_name_size = max_name_size + 2;

  return *file_name_max_p = max_name_size;

error:
  if (is_remove == true)
    {
      (void) remove (last_guess_path);
    }

  max_name_size = -1;
  *path_name_max_p = -1;
  *file_name_max_p = -1;

  return -1;
}

/*
 * fileio_get_max_name () - Find the longest names of files and path
 *                                 names that can be given for the given file
 *                                 system (path)
 *   return: filename max
 *   path(in): Path to directory or file name
 *   filename_max(out): the longest name that could be given
 *   pathname_max(out): the longest path that could be given
 *
 * Note: The main goal of this function is to respect the limits that
 *       the database system is using at compile time (e.g., variables
 *       defined with PATH_MAX) and at run time. For example, if
 *       the constant FILENAME_MAX cannot be used to detect long names
 *       since this value at compilation time may be different from the
 *       value at execution time (e.g., at other installation). If we
 *       use the compiled value, it may be possible that we will be
 *       removing a file when a new one is created when truncation of
 *       filenames is allowed at the running file system.
 *       In addition, it is possible that such limits may differ across
 *       file systems, device boundaries. For example, Unix System V
 *       uses a maximum of 14 characters for file names, and Unix BSD
 *       uses 255. On this implementations, we are forced to use 14
 *       characters.
 *       The functions returns the minimum of the compilation and run
 *       time for both filename and pathname.
 */
int
fileio_get_max_name (const char *given_path_p, long int *file_name_max_p, long int *path_name_max_p)
{
  char new_path[PATH_MAX];
  const char *path_p;
  char *name_p;
  struct stat stbuf;

  /* Errno need to be reset to find out if the values are not handle */
  errno = 0;
  path_p = given_path_p;

  *file_name_max_p = pathconf ((char *) path_p, _PC_NAME_MAX);
  *path_name_max_p = pathconf ((char *) path_p, _PC_PATH_MAX);

  if ((*file_name_max_p < 0 || *path_name_max_p < 0) && (errno == ENOENT || errno == EINVAL))
    {
      /* 
       * The above values may not be accepted for that path. The path may be
       * a file instead of a directory, try it with the directory since some
       * implementations cannot answer the above question when the path is a
       * file
       */

      if (stat (path_p, &stbuf) != -1 && ((stbuf.st_mode & S_IFMT) != S_IFDIR))
	{
	  /* Try it with the directory instead */
	  strncpy (new_path, given_path_p, PATH_MAX);
	  name_p = strrchr (new_path, '/');
#if defined(WINDOWS)
	  {
	    char *tmp_name = strrchr (new_path, '\\');
	    if (name_p < tmp_name)
	      name_p = tmp_name;
	  }
#endif /* WINDOWS */
	  if (name_p != NULL)
	    {
	      *name_p = '\0';
	    }
	  path_p = new_path;

	  *file_name_max_p = pathconf ((char *) path_p, _PC_NAME_MAX);
	  *path_name_max_p = pathconf ((char *) path_p, _PC_PATH_MAX);

	  path_p = given_path_p;
	}
    }

  if (*file_name_max_p < 0 || *path_name_max_p < 0)
    {
      /* If errno is zero, the values are indeterminate */
      (void) fileio_get_primitive_way_max (path_p, file_name_max_p, path_name_max_p);
    }

  /* Make sure that we do not overpass compilation structures */
  if (*file_name_max_p < 0 || *file_name_max_p > NAME_MAX)
    {
      *file_name_max_p = NAME_MAX;
    }

  if (*path_name_max_p < 0 || *path_name_max_p > PATH_MAX)
    {
      *path_name_max_p = PATH_MAX;
    }

  return *file_name_max_p;
}

/*
 * fileio_get_base_file_name () - Find start of basename in given filename
 *   return: basename
 *   fullname(in): Fullname of file
 */
const char *
fileio_get_base_file_name (const char *full_name_p)
{
  const char *no_path_name_p;

  no_path_name_p = strrchr (full_name_p, PATH_SEPARATOR);
#if defined(WINDOWS)
  {
    const char *nn_tmp = strrchr (full_name_p, '/');
    if (no_path_name_p < nn_tmp)
      {
	no_path_name_p = nn_tmp;
      }
  }
#endif /* WINDOWS */
  if (no_path_name_p == NULL)
    {
      no_path_name_p = full_name_p;
    }
  else
    {
      no_path_name_p++;		/* Skip to the name */
    }

  return no_path_name_p;
}

/*
 * fileio_get_directory_path () - Find directory path of given file. That is copy all but the
 *                basename of filename
 *   return: path
 *   path(out): The path of the file
 *   fullname(in): Fullname of file
 */
char *
fileio_get_directory_path (char *path_p, const char *full_name_p)
{
  const char *base_p;
  size_t path_size;

  base_p = fileio_get_base_file_name (full_name_p);

  assert (base_p >= full_name_p);

  if (base_p == full_name_p)
    {
      /* Same pointer, the file does not contain a path/directory portion. Use the current directory */
      if (getcwd (path_p, PATH_MAX) == NULL)
	{
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_BO_CWD_FAIL, 0);
	  *path_p = '\0';
	}
    }
  else
    {
      path_size = (size_t) (base_p - full_name_p - 1);
      if (path_size > PATH_MAX)
	{
	  path_size = PATH_MAX;
	}
      memcpy (path_p, full_name_p, path_size);
      path_p[path_size] = '\0';
    }

  return path_p;
}

/*
 * fileio_get_volume_max_suffix () -
 *   return:
 */
int
fileio_get_volume_max_suffix (void)
{
  return FILEIO_MAX_SUFFIX_LENGTH;
}

/*
 * fileio_make_volume_lock_name () - Build the name of volumes
 *   return: void
 *   vol_lockname(out):
 *   vol_fullname(in):
 *
 * Note: The caller must have enough space to store the name of the volume
 *       that is constructed(sprintf). It is recommended to have at least
 *       DB_MAX_PATH_LENGTH length.
 */
static void
fileio_make_volume_lock_name (char *vol_lock_name_p, const char *vol_full_name_p)
{
  sprintf (vol_lock_name_p, "%s%s", vol_full_name_p, FILEIO_VOLLOCK_SUFFIX);
}

/*
 * fileio_make_volume_info_name () - Build the name of volumes
 *   return: void
 *   volinfo_name(out):
 *   db_fullname(in):
 *
 * Note: The caller must have enough space to store the name of the volume
 *       that is constructed(sprintf). It is recommended to have at least
 *       DB_MAX_PATH_LENGTH length.
 */
void
fileio_make_volume_info_name (char *vol_info_name_p, const char *db_full_name_p)
{
  sprintf (vol_info_name_p, "%s%s", db_full_name_p, FILEIO_VOLINFO_SUFFIX);
}

/*
 * fileio_make_volume_ext_name () - Build the name of volumes
 *   return: void
 *   volext_fullname(out):
 *   ext_path(in):
 *   ext_name(in):
 *   volid(in):
 *
 * Note: The caller must have enough space to store the name of the volume
 *       that is constructed(sprintf). It is recommended to have at least
 *       DB_MAX_PATH_LENGTH length.
 */
void
fileio_make_volume_ext_name (char *vol_ext_full_name_p, const char *ext_path_p, const char *ext_name_p, VOLID vol_id)
{
  sprintf (vol_ext_full_name_p, "%s%s%s%s%03d", ext_path_p, FILEIO_PATH_SEPARATOR (ext_path_p), ext_name_p,
	   FILEIO_VOLEXT_PREFIX, vol_id);
}

/*
 * fileio_make_volume_ext_given_name () - Build the name of volumes
 *   return: void
 *   volext_fullname(out):
 *   ext_path(in):
 *   ext_name(in):
 *
 * Note: The caller must have enough space to store the name of the volume
 *       that is constructed(sprintf). It is recommended to have at least
 *       DB_MAX_PATH_LENGTH length.
 */
void
fileio_make_volume_ext_given_name (char *vol_ext_full_name_p, const char *ext_path_p, const char *ext_name_p)
{
  sprintf (vol_ext_full_name_p, "%s%s%s", ext_path_p, FILEIO_PATH_SEPARATOR (ext_path_p), ext_name_p);
}

/*
 * fileio_make_volume_temp_name () - Build the name of volumes
 *   return: void
 *   voltmp_fullname(out):
 *   tmp_path(in):
 *   tmp_name(in):
 *   volid(in):
 *
 * Note: The caller must have enough space to store the name of the volume
 *       that is constructed(sprintf). It is recommended to have at least
 *       DB_MAX_PATH_LENGTH length.
 */
void
fileio_make_volume_temp_name (char *vol_tmp_full_name_p, const char *tmp_path_p, const char *tmp_name_p, VOLID vol_id)
{
  sprintf (vol_tmp_full_name_p, "%s%c%s%s%03d", tmp_path_p, PATH_SEPARATOR, tmp_name_p, FILEIO_VOLTMP_PREFIX, vol_id);
}

/*
 * fileio_make_log_active_name () - Build the name of volumes
 *   return: void
 *   logactive_name(out):
 *   log_path(in):
 *   dbname(in):
 *
 * Note: The caller must have enough space to store the name of the volume
 *       that is constructed(sprintf). It is recommended to have at least
 *       DB_MAX_PATH_LENGTH length.
 */
void
fileio_make_log_active_name (char *log_active_name_p, const char *log_path_p, const char *db_name_p)
{
  sprintf (log_active_name_p, "%s%s%s%s", log_path_p, FILEIO_PATH_SEPARATOR (log_path_p), db_name_p,
	   FILEIO_SUFFIX_LOGACTIVE);

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
 * fileio_make_log_archive_name () - Build the name of volumes
 *   return: void
 *   logarchive_name(out):
 *   log_path(in):
 *   dbname(in):
 *   arvnum(in):
 *
 * Note: The caller must have enough space to store the name of the volume
 *       that is constructed(sprintf). It is recommended to have at least
 *       DB_MAX_PATH_LENGTH length.
 */
void
fileio_make_log_archive_name (char *log_archive_name_p, const char *log_path_p, const char *db_name_p,
			      int archive_number)
{
  sprintf (log_archive_name_p, "%s%s%s%s%03d", log_path_p, FILEIO_PATH_SEPARATOR (log_path_p), db_name_p,
	   FILEIO_SUFFIX_LOGARCHIVE, archive_number);
}

/*
 * fileio_make_removed_log_archive_name () - Build the name of removed volumes
 *   return: void
 *   logarchive_name(out):
 *   log_path(in):
 *   dbname(in):
 *
 * Note: The caller must have enough space to store the name of the volume
 *       that is constructed(sprintf). It is recommended to have at least
 *       DB_MAX_PATH_LENGTH length.
 */
void
fileio_make_removed_log_archive_name (char *log_archive_name_p, const char *log_path_p, const char *db_name_p)
{
  sprintf (log_archive_name_p, "%s%s%s%s.removed", log_path_p, FILEIO_PATH_SEPARATOR (log_path_p), db_name_p,
	   FILEIO_SUFFIX_LOGARCHIVE);
}

/*
 * fileio_make_log_archive_temp_name () -
 *   return: void
 *   logarchive_name_p(out):
 *   log_path_p(in):
 *   db_name_p(in):
 *
 * Note:
 */
void
fileio_make_log_archive_temp_name (char *log_archive_temp_name_p, const char *log_path_p, const char *db_name_p)
{
  const char *fmt_string_p;

  fmt_string_p = "%s%s%s%s";

  snprintf (log_archive_temp_name_p, PATH_MAX - 1, fmt_string_p, log_path_p, FILEIO_PATH_SEPARATOR (log_path_p),
	    db_name_p, FILEIO_SUFFIX_TMP_LOGARCHIVE);
}

/*
 * fileio_make_log_info_name () - Build the name of volumes
 *   return: void
 *   loginfo_name(out):
 *   log_path(in):
 *   dbname(in):
 *
 * Note: The caller must have enough space to store the name of the volume
 *       that is constructed(sprintf). It is recommended to have at least
 *       DB_MAX_PATH_LENGTH length.
 */
void
fileio_make_log_info_name (char *log_info_name_p, const char *log_path_p, const char *db_name_p)
{
  sprintf (log_info_name_p, "%s%s%s%s", log_path_p, FILEIO_PATH_SEPARATOR (log_path_p), db_name_p,
	   FILEIO_SUFFIX_LOGINFO);
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
  sprintf (backup_name_p, "%s%c%s%s%dv%03d", backup_path_p, PATH_SEPARATOR, no_path_vol_name_p, FILEIO_SUFFIX_BACKUP,
	   level, unit_num);
}

/*
 * fileio_cache () - Cache information related to a mounted volume
 *   return: vdes on success, NULL_VOLDES on failure
 *   volid(in): Permanent volume identifier
 *   vlabel(in): Name/label of the volume
 *   vdes(in): I/O volume descriptor
 *   lockf_type(in): Type of lock
 */
static int
fileio_cache (VOLID vol_id, const char *vol_label_p, int vol_fd, FILEIO_LOCKF_TYPE lockf_type)
{
  bool is_permanent_volume;
  FILEIO_VOLUME_INFO *vol_info_p;
  FILEIO_SYSTEM_VOLUME_INFO *sys_vol_info_p;
  int i, j, rv;

  FILEIO_CHECK_AND_INITIALIZE_VOLUME_HEADER_CACHE (NULL_VOLDES);

  if (vol_id > NULL_VOLID)
    {
      /* perm volume */
      if (vol_id < fileio_Vol_info_header.next_temp_volid)
	{
	  i = vol_id / FILEIO_VOLINFO_INCREMENT;
	  j = vol_id % FILEIO_VOLINFO_INCREMENT;
	  if (vol_id >= fileio_Vol_info_header.max_perm_vols
	      && fileio_expand_permanent_volume_info (&fileio_Vol_info_header, vol_id) < 0)
	    {
	      return NULL_VOLDES;
	    }
	  is_permanent_volume = true;
	}
      else
	{
	  /* volid is the next temp volume id */
	  i = (fileio_Vol_info_header.num_volinfo_array - 1 - (LOG_MAX_DBVOLID - vol_id) / FILEIO_VOLINFO_INCREMENT);
	  j = (FILEIO_VOLINFO_INCREMENT - 1 - (LOG_MAX_DBVOLID - vol_id) % FILEIO_VOLINFO_INCREMENT);
	  if (((LOG_MAX_DBVOLID - vol_id) >= fileio_Vol_info_header.max_temp_vols)
	      && fileio_expand_temporary_volume_info (&fileio_Vol_info_header, vol_id) < 0)
	    {
	      return NULL_VOLDES;
	    }
	  is_permanent_volume = false;
	}

      vol_info_p = &fileio_Vol_info_header.volinfo[i][j];
      vol_info_p->volid = vol_id;
      vol_info_p->vdes = vol_fd;
      vol_info_p->lockf_type = lockf_type;
      strncpy (vol_info_p->vlabel, vol_label_p, PATH_MAX);
      /* modify next volume id */
      rv = pthread_mutex_lock (&fileio_Vol_info_header.mutex);
      if (is_permanent_volume)
	{
	  if (fileio_Vol_info_header.next_perm_volid <= vol_id)
	    {
	      fileio_Vol_info_header.next_perm_volid = vol_id + 1;
	    }
	}
      else
	{
	  if (fileio_Vol_info_header.next_temp_volid >= vol_id)
	    {
	      fileio_Vol_info_header.next_temp_volid = vol_id - 1;
	    }
	}
      pthread_mutex_unlock (&fileio_Vol_info_header.mutex);
    }
  else
    {
      /* system volume */
      rv = pthread_mutex_lock (&fileio_Sys_vol_info_header.mutex);
      if (fileio_Sys_vol_info_header.anchor.vdes != NULL_VOLDES)
	{
	  sys_vol_info_p = (FILEIO_SYSTEM_VOLUME_INFO *) malloc (sizeof (FILEIO_SYSTEM_VOLUME_INFO));
	  if (sys_vol_info_p == NULL)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
		      sizeof (FILEIO_SYSTEM_VOLUME_INFO));
	      vol_fd = NULL_VOLDES;
	    }
	  else
	    {
	      sys_vol_info_p->volid = vol_id;
	      sys_vol_info_p->vdes = vol_fd;
	      sys_vol_info_p->lockf_type = lockf_type;
	      strncpy (sys_vol_info_p->vlabel, vol_label_p, PATH_MAX);
	      sys_vol_info_p->next = fileio_Sys_vol_info_header.anchor.next;
	      fileio_Sys_vol_info_header.anchor.next = sys_vol_info_p;
	      fileio_Sys_vol_info_header.num_vols++;
#if defined(WINDOWS)
	      pthread_mutex_init (&sys_vol_info_p->sysvol_mutex, NULL);
#endif /* WINDOWS */
	    }
	}
      else
	{
	  sys_vol_info_p = &fileio_Sys_vol_info_header.anchor;
	  sys_vol_info_p->volid = vol_id;
	  sys_vol_info_p->vdes = vol_fd;
	  sys_vol_info_p->lockf_type = lockf_type;
	  sys_vol_info_p->next = NULL;
	  strncpy (sys_vol_info_p->vlabel, vol_label_p, PATH_MAX);
#if defined(WINDOWS)
	  pthread_mutex_init (&sys_vol_info_p->sysvol_mutex, NULL);
#endif /* WINDOWS */
	  fileio_Sys_vol_info_header.num_vols++;
	}

      pthread_mutex_unlock (&fileio_Sys_vol_info_header.mutex);
    }

  return vol_fd;
}

/*
 * fileio_decache () - Decache volume information. Used when the volume is
 *                 dismounted
 *   return: void
 *   vdes(in): I/O Volume descriptor
 */
static void
fileio_decache (THREAD_ENTRY * thread_p, int vol_fd)
{
  FILEIO_SYSTEM_VOLUME_INFO *sys_vol_info_p, *prev_sys_vol_info_p;
  FILEIO_VOLUME_INFO *vol_info_p;
  int vol_id, prev_vol;
  int rv;
  APPLY_ARG arg = { 0 };

  rv = pthread_mutex_lock (&fileio_Sys_vol_info_header.mutex);
  /* sys volume ? */
  for ((sys_vol_info_p = &fileio_Sys_vol_info_header.anchor, prev_sys_vol_info_p = NULL);
       (sys_vol_info_p != NULL && sys_vol_info_p->vdes != NULL_VOLDES);
       prev_sys_vol_info_p = sys_vol_info_p, sys_vol_info_p = sys_vol_info_p->next)
    {
      if (sys_vol_info_p->vdes == vol_fd)
	{
	  if (prev_sys_vol_info_p == NULL)
	    {
	      if (fileio_Sys_vol_info_header.anchor.next != NULL)
		{
		  /* copy next volinfo to anchor. */
		  sys_vol_info_p = fileio_Sys_vol_info_header.anchor.next;
		  fileio_Sys_vol_info_header.anchor.volid = sys_vol_info_p->volid;
		  fileio_Sys_vol_info_header.anchor.vdes = sys_vol_info_p->vdes;
		  fileio_Sys_vol_info_header.anchor.lockf_type = sys_vol_info_p->lockf_type;
		  strncpy (fileio_Sys_vol_info_header.anchor.vlabel, sys_vol_info_p->vlabel, PATH_MAX);
		  fileio_Sys_vol_info_header.anchor.next = sys_vol_info_p->next;
#if defined(SERVER_MODE) && defined(WINDOWS)
		  pthread_mutex_destroy (&sys_vol_info_p->sysvol_mutex);
#endif /* WINDOWS */
		  free_and_init (sys_vol_info_p);
		}
	      else
		{
		  fileio_Sys_vol_info_header.anchor.volid = NULL_VOLID;
		  fileio_Sys_vol_info_header.anchor.vdes = NULL_VOLDES;
		  fileio_Sys_vol_info_header.anchor.lockf_type = FILEIO_NOT_LOCKF;
		  fileio_Sys_vol_info_header.anchor.vlabel[0] = '\0';
		  fileio_Sys_vol_info_header.anchor.next = NULL;
#if defined(SERVER_MODE) && defined(WINDOWS)
		  pthread_mutex_destroy (&fileio_Sys_vol_info_header.anchor.sysvol_mutex);
#endif /* WINDOWS */
		}
	    }
	  else
	    {
	      prev_sys_vol_info_p->next = sys_vol_info_p->next;
#if defined(SERVER_MODE) && defined(WINDOWS)
	      pthread_mutex_destroy (&sys_vol_info_p->sysvol_mutex);
#endif /* WINDOWS */
	      free_and_init (sys_vol_info_p);
	    }
	  fileio_Sys_vol_info_header.num_vols--;
	  pthread_mutex_unlock (&fileio_Sys_vol_info_header.mutex);
	  return;
	}
    }
  pthread_mutex_unlock (&fileio_Sys_vol_info_header.mutex);
  arg.vdes = vol_fd;
  vol_info_p = fileio_traverse_permanent_volume (thread_p, fileio_is_volume_descriptor_equal, &arg);
  if (vol_info_p)
    {
      vol_id = vol_info_p->volid;

      /* update next_perm_volid, if needed */
      rv = pthread_mutex_lock (&fileio_Vol_info_header.mutex);
      if (fileio_Vol_info_header.next_perm_volid == vol_id + 1)
	{
	  fileio_Vol_info_header.next_perm_volid = fileio_find_previous_perm_volume (thread_p, vol_id) + 1;
	}
      pthread_mutex_unlock (&fileio_Vol_info_header.mutex);

      vol_info_p->volid = NULL_VOLID;
      vol_info_p->vdes = NULL_VOLDES;
      vol_info_p->lockf_type = FILEIO_NOT_LOCKF;
      vol_info_p->vlabel[0] = '\0';
#if defined(SERVER_MODE) && defined(WINDOWS)
      pthread_mutex_destroy (&vol_info_p->vol_mutex);
#endif /* WINDOWS */
      return;
    }

  arg.vdes = vol_fd;
  vol_info_p = fileio_traverse_temporary_volume (thread_p, fileio_is_volume_descriptor_equal, &arg);
  if (vol_info_p)
    {
      vol_id = vol_info_p->volid;

      /* update next_temp_volid, if needed */
      rv = pthread_mutex_lock (&fileio_Vol_info_header.mutex);
      if (fileio_Vol_info_header.next_temp_volid == vol_id - 1)
	{
	  prev_vol = fileio_find_previous_temp_volume (thread_p, vol_id);
	  /* if prev_vol is NULL_VOLID, this volume is last volume */
	  fileio_Vol_info_header.next_temp_volid = (prev_vol != NULL_VOLID) ? (prev_vol - 1) : (LOG_MAX_DBVOLID);
	}

      pthread_mutex_unlock (&fileio_Vol_info_header.mutex);

      vol_info_p->volid = NULL_VOLID;
      vol_info_p->vdes = NULL_VOLDES;
      vol_info_p->lockf_type = FILEIO_NOT_LOCKF;
      vol_info_p->vlabel[0] = '\0';
#if defined(SERVER_MODE) && defined(WINDOWS)
      pthread_mutex_destroy (&vol_info_p->vol_mutex);
#endif /* WINDOWS */
      return;
    }
}

/*
 * fileio_get_volume_label ()
 *  - Find the name of a mounted volume given its permanent volume identifier
 *   return: Volume label
 *   volid(in): Permanent volume identifier
 */
char *
fileio_get_volume_label (VOLID vol_id, bool is_peek)
{
  FILEIO_VOLUME_INFO *vol_info_p;
  FILEIO_SYSTEM_VOLUME_INFO *sys_vol_info_p;
  char *vol_label_p = NULL;
  int i, j, rv;
  APPLY_ARG arg = { 0 };

  FILEIO_CHECK_AND_INITIALIZE_VOLUME_HEADER_CACHE (NULL);
  if (vol_id > NULL_VOLID)
    {
      /* perm volume */
      if (vol_id < fileio_Vol_info_header.next_temp_volid)
	{
	  if (vol_id >= fileio_Vol_info_header.max_perm_vols)
	    {
	      return NULL;
	    }

	  i = vol_id / FILEIO_VOLINFO_INCREMENT;
	  j = vol_id % FILEIO_VOLINFO_INCREMENT;
	}
      else
	{
	  /* volid is the next temp volume id */
	  if ((LOG_MAX_DBVOLID - vol_id) >= fileio_Vol_info_header.max_temp_vols)
	    {
	      return NULL;
	    }

	  i = fileio_Vol_info_header.num_volinfo_array - 1 - (LOG_MAX_DBVOLID - vol_id) / FILEIO_VOLINFO_INCREMENT;
	  j = FILEIO_VOLINFO_INCREMENT - 1 - (LOG_MAX_DBVOLID - vol_id) % FILEIO_VOLINFO_INCREMENT;
	}
      vol_info_p = &fileio_Vol_info_header.volinfo[i][j];
      vol_label_p = (char *) vol_info_p->vlabel;
    }
  else
    {
      /* system volume */
      rv = pthread_mutex_lock (&fileio_Sys_vol_info_header.mutex);
      arg.vol_id = vol_id;
      sys_vol_info_p = fileio_find_system_volume (NULL, fileio_is_system_volume_id_equal, &arg);
      if (sys_vol_info_p)
	{
	  vol_label_p = (char *) sys_vol_info_p->vlabel;
	}

      pthread_mutex_unlock (&fileio_Sys_vol_info_header.mutex);
    }

  if (!is_peek)
    {
      char *ret = strdup (vol_label_p);

      if (ret == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) strlen (vol_label_p));
	}

      return ret;
    }

  return vol_label_p;
}

/*
 * fileio_get_volume_label_by_fd ()
 *   - Find the name of a mounted volume given its file descriptor
 *   return: Volume label
 *   vol_fd(in): volume descriptor
 */
char *
fileio_get_volume_label_by_fd (int vol_fd, bool is_peek)
{
  return fileio_get_volume_label (fileio_get_volume_id (vol_fd), is_peek);
}


/*
 * fileio_get_volume_id () - Find volume identifier of a mounted volume given its
 *               descriptor
 *   return: The volume identifier
 *   vdes(in): I/O volume descriptor
 */
static VOLID
fileio_get_volume_id (int vol_fd)
{
  FILEIO_VOLUME_INFO *vol_info_p;
  FILEIO_SYSTEM_VOLUME_INFO *sys_vol_info_p;
  VOLID vol_id = NULL_VOLID;
  int rv;
  APPLY_ARG arg = { 0 };

  FILEIO_CHECK_AND_INITIALIZE_VOLUME_HEADER_CACHE (NULL_VOLID);
  /* sys volume ? */
  rv = pthread_mutex_lock (&fileio_Sys_vol_info_header.mutex);
  arg.vdes = vol_fd;
  sys_vol_info_p = fileio_find_system_volume (NULL, fileio_is_system_volume_descriptor_equal, &arg);
  if (sys_vol_info_p)
    {
      vol_id = sys_vol_info_p->volid;
      pthread_mutex_unlock (&fileio_Sys_vol_info_header.mutex);
      return vol_id;
    }

  pthread_mutex_unlock (&fileio_Sys_vol_info_header.mutex);

  arg.vdes = vol_fd;
  vol_info_p = fileio_traverse_permanent_volume (NULL, fileio_is_volume_descriptor_equal, &arg);
  if (vol_info_p)
    {
      return vol_info_p->volid;
    }

  arg.vdes = vol_fd;
  vol_info_p = fileio_traverse_temporary_volume (NULL, fileio_is_volume_descriptor_equal, &arg);
  if (vol_info_p)
    {
      return vol_info_p->volid;
    }

  return vol_id;
}

static bool
fileio_is_volume_label_equal (THREAD_ENTRY * thread_p, FILEIO_VOLUME_INFO * vol_info_p, APPLY_ARG * arg)
{
  return (util_compare_filepath (vol_info_p->vlabel, arg->vol_label) == 0);
}

/*
 * fileio_find_volume_id_with_label () - Find the volume identifier given the volume label of
 *                      a mounted volume
 *   return: The volume identifier
 *   vlabel(in): Volume Name/label
 */
VOLID
fileio_find_volume_id_with_label (THREAD_ENTRY * thread_p, const char *vol_label_p)
{
  FILEIO_VOLUME_INFO *vol_info_p;
  FILEIO_SYSTEM_VOLUME_INFO *sys_vol_info_p;
  VOLID vol_id = NULL_VOLID;
  int rv;
  APPLY_ARG arg = { 0 };

  FILEIO_CHECK_AND_INITIALIZE_VOLUME_HEADER_CACHE (NULL_VOLID);
  rv = pthread_mutex_lock (&fileio_Sys_vol_info_header.mutex);
  arg.vol_label = vol_label_p;
  sys_vol_info_p = fileio_find_system_volume (thread_p, fileio_is_system_volume_label_equal, &arg);
  if (sys_vol_info_p)
    {
      vol_id = sys_vol_info_p->volid;
      pthread_mutex_unlock (&fileio_Sys_vol_info_header.mutex);
      return vol_id;
    }

  pthread_mutex_unlock (&fileio_Sys_vol_info_header.mutex);

  arg.vol_label = vol_label_p;
  vol_info_p = fileio_traverse_permanent_volume (thread_p, fileio_is_volume_label_equal, &arg);
  if (vol_info_p)
    {
      return vol_info_p->volid;
    }

  arg.vol_label = vol_label_p;
  vol_info_p = fileio_traverse_temporary_volume (thread_p, fileio_is_volume_label_equal, &arg);
  if (vol_info_p)
    {
      return vol_info_p->volid;
    }

  return vol_id;
}

bool
fileio_is_temp_volume (THREAD_ENTRY * thread_p, VOLID volid)
{
  FILEIO_VOLUME_INFO *vol_info_p;
  APPLY_ARG arg = { 0 };

  if (volid == NULL_VOLID)
    {
      return false;
    }

  FILEIO_CHECK_AND_INITIALIZE_VOLUME_HEADER_CACHE (NULL_VOLID);

  arg.vol_id = volid;
  vol_info_p = fileio_traverse_temporary_volume (thread_p, fileio_is_volume_id_equal, &arg);
  if (vol_info_p)
    {
      assert (fileio_get_volume_descriptor (volid) != NULL_VOLDES);
      return true;
    }

  return false;
}

VOLID
fileio_find_next_perm_volume (THREAD_ENTRY * thread_p, VOLID volid)
{
  FILEIO_VOLUME_INFO *vol_info_p;
  APPLY_ARG arg = { 0 };

  if (volid == NULL_VOLID)
    {
      return NULL_VOLID;
    }

  FILEIO_CHECK_AND_INITIALIZE_VOLUME_HEADER_CACHE (NULL_VOLID);

  arg.vol_id = volid;
  vol_info_p = fileio_traverse_permanent_volume (thread_p, fileio_is_volume_id_gt, &arg);
  if (vol_info_p)
    {
      assert (fileio_get_volume_descriptor (volid) != NULL_VOLDES);
      return vol_info_p->volid;
    }

  return NULL_VOLID;
}

VOLID
fileio_find_previous_perm_volume (THREAD_ENTRY * thread_p, VOLID volid)
{
  FILEIO_VOLUME_INFO *vol_info_p;
  APPLY_ARG arg = { 0 };

  if (volid == NULL_VOLID)
    {
      return NULL_VOLID;
    }

  FILEIO_CHECK_AND_INITIALIZE_VOLUME_HEADER_CACHE (NULL_VOLID);

  arg.vol_id = volid;
  vol_info_p = fileio_reverse_traverse_permanent_volume (thread_p, fileio_is_volume_id_lt, &arg);
  if (vol_info_p)
    {
      assert (fileio_get_volume_descriptor (volid) != NULL_VOLDES);
      return vol_info_p->volid;
    }

  return NULL_VOLID;
}

VOLID
fileio_find_previous_temp_volume (THREAD_ENTRY * thread_p, VOLID volid)
{
  FILEIO_VOLUME_INFO *vol_info_p;
  APPLY_ARG arg = { 0 };

  if (volid == NULL_VOLID)
    {
      return NULL_VOLID;
    }

  FILEIO_CHECK_AND_INITIALIZE_VOLUME_HEADER_CACHE (NULL_VOLID);

  arg.vol_id = volid;
  vol_info_p = fileio_reverse_traverse_temporary_volume (thread_p, fileio_is_volume_id_gt, &arg);
  if (vol_info_p)
    {
      assert (fileio_get_volume_descriptor (volid) != NULL_VOLDES);
      return vol_info_p->volid;
    }

  return NULL_VOLID;
}

/*
 * fileio_get_volume_descriptor () - Find the volume descriptor given the volume permanent
 *              identifier
 *   return: I/O volume descriptor
 *   volid(in): Permanent volume identifier
 */
int
fileio_get_volume_descriptor (VOLID vol_id)
{
  FILEIO_VOLUME_INFO *vol_info_p;
  FILEIO_SYSTEM_VOLUME_INFO *sys_vol_info_p;
  int vol_fd = NULL_VOLDES;
  int i, j, rv;
  APPLY_ARG arg = { 0 };

  FILEIO_CHECK_AND_INITIALIZE_VOLUME_HEADER_CACHE (NULL_VOLDES);
  if (vol_id > NULL_VOLID)
    {
      /* perm volume */
      if (vol_id < fileio_Vol_info_header.next_temp_volid)
	{
	  if (vol_id >= fileio_Vol_info_header.max_perm_vols)
	    {
	      return NULL_VOLDES;
	    }
	  i = vol_id / FILEIO_VOLINFO_INCREMENT;
	  j = vol_id % FILEIO_VOLINFO_INCREMENT;
	}
      else
	{
	  /* volid is the next temp volume id */
	  if ((LOG_MAX_DBVOLID - vol_id) >= fileio_Vol_info_header.max_temp_vols)
	    {
	      return NULL_VOLDES;
	    }
	  i = fileio_Vol_info_header.num_volinfo_array - 1 - (LOG_MAX_DBVOLID - vol_id) / FILEIO_VOLINFO_INCREMENT;
	  j = FILEIO_VOLINFO_INCREMENT - 1 - (LOG_MAX_DBVOLID - vol_id) % FILEIO_VOLINFO_INCREMENT;
	}
      vol_info_p = &fileio_Vol_info_header.volinfo[i][j];
      vol_fd = vol_info_p->vdes;
    }
  else
    {
      rv = pthread_mutex_lock (&fileio_Sys_vol_info_header.mutex);
      arg.vol_id = vol_id;
      sys_vol_info_p = fileio_find_system_volume (NULL, fileio_is_system_volume_id_equal, &arg);
      if (sys_vol_info_p)
	{
	  vol_fd = sys_vol_info_p->vdes;
	}

      pthread_mutex_unlock (&fileio_Sys_vol_info_header.mutex);
    }

  return vol_fd;
}

/*
 * fileio_find_volume_descriptor_with_label () - Find the volume descriptor given the volume label/name
 *   return: Volume Name/label
 *   vlabel(in): I/O volume descriptor
 */
int
fileio_find_volume_descriptor_with_label (const char *vol_label_p)
{
  FILEIO_VOLUME_INFO *vol_info_p;
  FILEIO_SYSTEM_VOLUME_INFO *sys_vol_info_p;
  int vol_fd = NULL_VOLDES;
  int rv;
  APPLY_ARG arg = { 0 };

  FILEIO_CHECK_AND_INITIALIZE_VOLUME_HEADER_CACHE (NULL_VOLDES);
  rv = pthread_mutex_lock (&fileio_Sys_vol_info_header.mutex);
  arg.vol_label = vol_label_p;
  sys_vol_info_p = fileio_find_system_volume (NULL, fileio_is_system_volume_label_equal, &arg);
  if (sys_vol_info_p)
    {
      vol_fd = sys_vol_info_p->vdes;
      pthread_mutex_unlock (&fileio_Sys_vol_info_header.mutex);
      return vol_fd;
    }

  pthread_mutex_unlock (&fileio_Sys_vol_info_header.mutex);

  arg.vol_label = vol_label_p;
  vol_info_p = fileio_traverse_permanent_volume (NULL, fileio_is_volume_label_equal, &arg);
  if (vol_info_p)
    {
      return vol_info_p->vdes;
    }

  arg.vol_label = vol_label_p;
  vol_info_p = fileio_traverse_temporary_volume (NULL, fileio_is_volume_label_equal, &arg);
  if (vol_info_p)
    {
      return vol_info_p->vdes;
    }

  return vol_fd;
}

/*
 * fileio_get_lockf_type () - Find the lock type applied to a mounted volume
 *   return: lockf_type
 *   vdes(in): I/O volume descriptor
 */
static FILEIO_LOCKF_TYPE
fileio_get_lockf_type (int vol_fd)
{
  FILEIO_VOLUME_INFO *vol_info_p;
  FILEIO_SYSTEM_VOLUME_INFO *sys_vol_info_p;
  FILEIO_LOCKF_TYPE lockf_type = FILEIO_NOT_LOCKF;
  int rv;
  APPLY_ARG arg = { 0 };

  FILEIO_CHECK_AND_INITIALIZE_VOLUME_HEADER_CACHE (FILEIO_NOT_LOCKF);
  rv = pthread_mutex_lock (&fileio_Sys_vol_info_header.mutex);
  arg.vdes = vol_fd;
  sys_vol_info_p = fileio_find_system_volume (NULL, fileio_is_system_volume_descriptor_equal, &arg);
  if (sys_vol_info_p)
    {
      lockf_type = sys_vol_info_p->lockf_type;
      pthread_mutex_unlock (&fileio_Sys_vol_info_header.mutex);
      return lockf_type;
    }
  pthread_mutex_unlock (&fileio_Sys_vol_info_header.mutex);

  arg.vdes = vol_fd;
  vol_info_p = fileio_traverse_permanent_volume (NULL, fileio_is_volume_descriptor_equal, &arg);
  if (vol_info_p)
    {
      return vol_info_p->lockf_type;
    }

  arg.vdes = vol_fd;
  vol_info_p = fileio_traverse_temporary_volume (NULL, fileio_is_volume_descriptor_equal, &arg);
  if (vol_info_p)
    {
      return vol_info_p->lockf_type;
    }

  return lockf_type;
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
  num_cpus = fileio_os_sysconf ();
  /* check for the upper bound of threads */
  if (num_threads == FILEIO_BACKUP_NUM_THREADS_AUTO)
    {
      thread_info_p->num_threads = num_cpus;
    }
  else
    {
      thread_info_p->num_threads = MIN (num_threads, num_cpus * 2);
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
  strncpy (session_p->bkup.name, backup_destination_p, PATH_MAX);
  strncpy (session_p->bkup.current_path, backup_destination_p, PATH_MAX);
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
  if ((int) prm_get_bigint_value (PRM_ID_IO_BACKUP_MAX_VOLUME_SIZE) > 0
      && (session_p->bkup.iosize >= MIN ((int) prm_get_bigint_value (PRM_ID_IO_BACKUP_MAX_VOLUME_SIZE), DB_INT32_MAX)))
    {
      er_log_debug (ARG_FILE_LINE,
		    "Backup block buffer size %d must be less "
		    "than backup volume size %d, resetting buffer size to %d\n", session_p->bkup.iosize,
		    (int) prm_get_bigint_value (PRM_ID_IO_BACKUP_MAX_VOLUME_SIZE), buf_size);
      session_p->bkup.iosize = buf_size;
    }

#if defined(CUBRID_DEBUG)
  /* These print statements are candidates for part of a "verbose" option to backupdb. */
  fprintf (stdout, "NATURAL BUFFER SIZE %d (%d IO buffer blocks)\n", session_p->bkup.iosize,
	   session_p->bkup.iosize / buf_size);
  fprintf (stdout, "BACKUP_MAX_VOLUME_SIZE = %d\n", (int) prm_get_bigint_value (PRM_ID_IO_BACKUP_MAX_VOLUME_SIZE));
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
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) session_p->bkup.iosize);

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
	case FILEIO_ZIP_LZO1X_METHOD:
	  if (node->wrkmem != NULL)
	    {
	      free_and_init (node->wrkmem);
	    }

	  if (node->zip_page != NULL)
	    {
	      free_and_init (node->zip_page);
	    }
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
  strncpy (backup_header_p->db_release, rel_release_string (), REL_MAX_RELEASE_LENGTH);
  strncpy (backup_header_p->db_fullname, db_full_name_p, PATH_MAX);
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

  switch (zip_method)
    {
    case FILEIO_ZIP_LZO1X_METHOD:
      if (lzo_init () != LZO_E_OK)
	{
#if defined(CUBRID_DEBUG)
	  fprintf (stdout, "internal error - lzo_init() failed !!!\n");
	  fprintf (stdout,
		   "(this usually indicates a compiler bug - try recompiling\nwithout optimizations, and enable "
		   "`-DLZO_DEBUG' for diagnostics)\n");
#endif /* CUBRID_DEBUG */
	  goto error;
	}
      break;
    case FILEIO_ZIP_ZLIB_METHOD:
      break;
    default:
      break;
    }

  backup_header_p->zip_method = zip_method;
  backup_header_p->zip_level = zip_level;
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

  end_time = (INT64) time (NULL);

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
      fprintf (stdout, "io_backup_end: iosize = %d, count = %d, voltotalio = %ld : EOF JUNK\n", session_p->bkup.iosize,
	       session_p->bkup.count, session_p->bkup.voltotalio);
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

      if (fileio_synchronize (thread_p, session_p->bkup.vdes, session_p->bkup.name) != session_p->bkup.vdes)
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
  size_t zip_page_size, wrkmem_size;

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
  node_p->zip_page = NULL;
  node_p->wrkmem = NULL;
  size = backup_header_p->bkpagesize + FILEIO_BACKUP_PAGE_OVERHEAD;
  node_p->area = (FILEIO_BACKUP_PAGE *) malloc (size);
  if (node_p == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) size);
      goto exit_on_error;
    }

  switch (backup_header_p->zip_method)
    {
    case FILEIO_ZIP_LZO1X_METHOD:
      zip_page_size = sizeof (lzo_uint) + size + size / 16 + 64 + 3;
      node_p->zip_page = (FILEIO_ZIP_PAGE *) malloc (zip_page_size);
      if (node_p->zip_page == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, zip_page_size);
	  goto exit_on_error;
	}

      if (backup_header_p->zip_level == FILEIO_ZIP_LZO1X_999_LEVEL)
	{
	  /* best reduction */
	  wrkmem_size = LZO1X_999_MEM_COMPRESS;
	}
      else
	{
	  /* best speed */
	  wrkmem_size = LZO1X_1_MEM_COMPRESS;
	}

      node_p->wrkmem = (lzo_bytep) malloc (wrkmem_size);
      if (node_p->wrkmem == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, wrkmem_size);
	  goto exit_on_error;
	}
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
      if (node_p->wrkmem)
	{
	  free_and_init (node_p->wrkmem);
	}

      if (node_p->zip_page)
	{
	  free_and_init (node_p->zip_page);
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
  int error = NO_ERROR;
  int rv;

  if (!node_p || !backup_header_p)
    {
      goto exit_on_error;
    }

  assert (node_p->nread >= 0);

  switch (backup_header_p->zip_method)
    {
    case FILEIO_ZIP_LZO1X_METHOD:
      if (backup_header_p->zip_level == FILEIO_ZIP_LZO1X_999_LEVEL)
	{
	  /* best reduction */
	  rv =
	    lzo1x_999_compress ((lzo_bytep) node_p->area, (lzo_uint) node_p->nread, node_p->zip_page->buf,
				&node_p->zip_page->buf_len, node_p->wrkmem);
	}
      else
	{
	  /* best speed */
	  rv =
	    lzo1x_1_compress ((lzo_bytep) node_p->area, (lzo_uint) node_p->nread, node_p->zip_page->buf,
			      &node_p->zip_page->buf_len, node_p->wrkmem);
	}
      if (rv != LZO_E_OK || (node_p->zip_page->buf_len > (size_t) (node_p->nread + node_p->nread / 16 + 64 + 3)))
	{
	  /* this should NEVER happen */
	  error = ER_IO_LZO_COMPRESS_FAIL;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 4, backup_header_p->zip_method,
		  fileio_get_zip_method_string (backup_header_p->zip_method), backup_header_p->zip_level,
		  fileio_get_zip_level_string (backup_header_p->zip_level));
#if defined(CUBRID_DEBUG)
	  fprintf (stdout,
		   "internal error - compression failed: %d, node->pageid = %d, node->nread = %d, "
		   "node->zip_page->buf_len = %d, node->nread + node->nread / 16 + 64 + 3 = %d\n", rv,
		   node_p->pageid, node_p->nread, node_p->zip_page->buf_len,
		   node_p->nread + node_p->nread / 16 + 64 + 3);
#endif /* CUBRID_DEBUG */
	  goto exit_on_error;
	}

      if (node_p->zip_page->buf_len < (size_t) node_p->nread)
	{
	  /* already write compressed block */
	  ;
	}
      else
	{
	  /* not compressible - write uncompressed block */
	  node_p->zip_page->buf_len = (lzo_uint) node_p->nread;
	  memcpy (node_p->zip_page->buf, node_p->area, node_p->nread);
	}
      break;
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
    case FILEIO_ZIP_LZO1X_METHOD:
      session_p->dbfile.area = (FILEIO_BACKUP_PAGE *) node_p->zip_page;
      node_p->nread = sizeof (lzo_uint) + node_p->zip_page->buf_len;
      break;
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
  /* thread service routine has tran_index_lock, and should release before it is working */
  pthread_mutex_unlock (&thread_p->tran_index_lock);
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

static int
fileio_start_backup_thread (THREAD_ENTRY * thread_p, FILEIO_BACKUP_SESSION * session_p,
			    FILEIO_THREAD_INFO * thread_info_p, int from_npages, bool is_only_updated_pages,
			    int check_ratio, int check_npages, FILEIO_QUEUE * queue_p)
{
  CSS_CONN_ENTRY *conn_p;
  int conn_index;
  CSS_JOB_ENTRY *job_entry_p;
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
  conn_p = thread_get_current_conn_entry ();
  conn_index = (conn_p) ? conn_p->idx : 0;
  for (i = 1; i <= thread_info_p->act_r_threads; i++)
    {
      job_entry_p =
	css_make_job_entry (conn_p, (CSS_THREAD_FN) fileio_read_backup_volume, (CSS_THREAD_ARG) session_p,
			    conn_index + i
			    /* explicit job queue index */
	);

      if (job_entry_p == NULL)
	{
	  return ER_FAILED;
	}

      css_add_to_job_queue (job_entry_p);
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

#if !defined(CS_MODE)
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

  node_p->nread = backup_header_p->bkpagesize + FILEIO_BACKUP_PAGE_OVERHEAD;
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
#endif /* !CS_MODE */

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
  int count;
  bool is_interactive_need_new = false;
  bool is_force_new_bkvol = false;

  if ((int) prm_get_bigint_value (PRM_ID_IO_BACKUP_MAX_VOLUME_SIZE) > 0
      && session_p->bkup.count > (int) prm_get_bigint_value (PRM_ID_IO_BACKUP_MAX_VOLUME_SIZE))
    {
      er_log_debug (ARG_FILE_LINE, "Backup_flush: Backup aborted because count %d larger than max volume size %d\n",
		    session_p->bkup.count, (int) prm_get_bigint_value (PRM_ID_IO_BACKUP_MAX_VOLUME_SIZE));
      return ER_FAILED;
    }

#if defined(CUBRID_DEBUG)
  fprintf (stdout, "io_backup_flush: bkup.count = %d, voltotalio = %ld\n", session_p->bkup.count,
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
      if ((int) prm_get_bigint_value (PRM_ID_IO_BACKUP_MAX_VOLUME_SIZE) > 0)
	{
	  count =
	    (int) MIN (count,
		       (int) prm_get_bigint_value (PRM_ID_IO_BACKUP_MAX_VOLUME_SIZE) - session_p->bkup.voltotalio);
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
	      || ((int) prm_get_bigint_value (PRM_ID_IO_BACKUP_MAX_VOLUME_SIZE) > 0
		  && (session_p->bkup.voltotalio >= (int) prm_get_bigint_value (PRM_ID_IO_BACKUP_MAX_VOLUME_SIZE))))
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

#if defined(SERVER_MODE)
  /* Backup Thread is reading data/log pages slowly to avoid IO burst */
  if (session_p->dbfile.volid == LOG_DBLOG_ACTIVE_VOLID
      || (session_p->dbfile.volid == LOG_DBLOG_ARCHIVE_VOLID && LOG_CS_OWN_WRITE_MODE (thread_p)))
    {
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
      nbytes = session_p->bkup.iosize - session_p->bkup.count;
      if (nbytes > to_write_nbytes)
	{
	  nbytes = to_write_nbytes;
	}

      memcpy (session_p->bkup.ptr, buffer_p, nbytes);
      session_p->bkup.count += (int) nbytes;
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
  strncpy (session_p->bkup.loc_db_fullname, is_new_vol_path ? db_full_name_p : "", PATH_MAX);
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
	      nbytes = read (session_p->bkup.vdes, session_p->bkup.ptr, session_p->bkup.count);
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
			      strncpy (session_p->bkup.name, next_vol_p, PATH_MAX - 1);
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
			  strncpy (session_p->bkup.name, next_vol_p, PATH_MAX - 1);
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

  if (asprintf (&header_message_p, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_IO,
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
	      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_BKUP_INCOMPATIBLE, 2, rel_name (),
		      rel_release_string ());
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

	      fileio_ctime (&match_backup_creation_time, io_timeval);
	      strcpy (save_time1, io_timeval);

	      fileio_ctime (&backup_header_p->start_time, io_timeval);
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

  success = fileio_synchronize_all (thread_p, false);
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
  fprintf (stdout, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_IO, MSGCAT_FILEIO_BKUP_HDR));

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
      fprintf (stdout, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_LOG, MSGCAT_LOG_STARTS));
      fprintf (stdout, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_LOG, MSGCAT_LOG_BACKUPINFO_NEEDED),
	       from_volbackup);
      fprintf (stdout, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_LOG, MSGCAT_LOG_STARTS));

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
	  fprintf (stdout, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_LOG, MSGCAT_LOG_NEWLOCATION));
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
	    fileio_get_backup_info_volume_name (try_level, FILEIO_INITIAL_BACKUP_UNITS, FILEIO_FIRST_BACKUP_VOL_INFO);
	  if (volnameptr != NULL)
	    {
	      strcpy (from_volbackup, volnameptr);
	    }
	  else
	    {
	      fileio_make_backup_name (from_volbackup, nopath_name, logpath, try_level, FILEIO_INITIAL_BACKUP_UNITS);
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
      (void) fileio_initialize_res (thread_p, &(malloc_io_pgptr->prv));
    }

  while (*next_page_id_p < stop_page_id)
    {
      /* 
       * We did not back up a page since it was deallocated, or there
       * is a hole of some kind that must be filled in with correctly
       * formatted pages.
       */
      if (fileio_write_restore (thread_p, page_bitmap, session_p->dbfile.vdes, malloc_io_pgptr, session_p->dbfile.volid,
				*next_page_id_p, session_p->dbfile.level) == NULL)
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

    case FILEIO_ZIP_LZO1X_METHOD:
      {
	int rv;
	/* alloc queue node */
	node = fileio_allocate_node (queue_p, backup_header_p);
	if (node == NULL)
	  {
	    goto exit_on_error;
	  }

	save_area_p = session_p->dbfile.area;	/* save link */
	session_p->dbfile.area = (FILEIO_BACKUP_PAGE *) node->zip_page;

	rv = fileio_read_restore (thread_p, session_p, sizeof (lzo_uint));
	session_p->dbfile.area = save_area_p;	/* restore link */
	if (rv != NO_ERROR)
	  {
	    error = ER_IO_RESTORE_READ_ERROR;
	    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, backup_header_p->unit_num);
	    goto exit_on_error;
	  }

	/* sanity check of the size values */
	if (node->zip_page->buf_len > (size_t) nbytes || node->zip_page->buf_len == 0)
	  {
	    error = ER_IO_LZO_COMPRESS_FAIL;	/* may be compress fail */
	    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 4, backup_header_p->zip_method,
		    fileio_get_zip_method_string (backup_header_p->zip_method), backup_header_p->zip_level,
		    fileio_get_zip_level_string (backup_header_p->zip_level));
#if defined(CUBRID_DEBUG)
	    fprintf (stdout, "io_restore_volume_decompress_read: block size error - data corrupted\n");
#endif /* CUBRID_DEBUG */
	    goto exit_on_error;
	  }
	else if (node->zip_page->buf_len < (size_t) nbytes)
	  {
	    /* read compressed block data */
	    lzo_uint unzip_len;

	    save_area_p = session_p->dbfile.area;	/* save link */
	    session_p->dbfile.area = (FILEIO_BACKUP_PAGE *) node->zip_page->buf;

	    rv = fileio_read_restore (thread_p, session_p, (int) node->zip_page->buf_len);
	    session_p->dbfile.area = save_area_p;	/* restore link */
	    if (rv != NO_ERROR)
	      {
		error = ER_IO_RESTORE_READ_ERROR;
		er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, backup_header_p->unit_num);
		goto exit_on_error;
	      }

	    /* decompress - use safe decompressor as data might be corrupted during a file transfer */
	    unzip_len = nbytes;
	    rv =
	      lzo1x_decompress_safe (node->zip_page->buf, node->zip_page->buf_len, (lzo_bytep) session_p->dbfile.area,
				     &unzip_len, NULL);
	    if (rv != LZO_E_OK || unzip_len != (size_t) nbytes)
	      {
		error = ER_IO_LZO_DECOMPRESS_FAIL;
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
	    rv = fileio_read_restore (thread_p, session_p, (int) node->zip_page->buf_len);
	    if (rv != NO_ERROR)
	      {
		error = ER_IO_RESTORE_READ_ERROR;
		er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, backup_header_p->unit_num);
		goto exit_on_error;
	      }
	  }

      }
      break;

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

#if !defined(CS_MODE)
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
		       bool is_remember_pages)
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
				    session_p->dbfile.volid, next_page_id, session_p->dbfile.level) == NULL)
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

	  if (volid != LOG_DBFIRST_VOLID)
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
	    }
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
#endif /* !CS_MODE */

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
 *   level(in): backup level page restored from
 *
 * Note: The contents of the page stored on io_pgptr buffer which is
 *       IO_PAGESIZE long are sent to disk using fileio_write. The restore pageid
 *       cache is updated.
 */
static void *
fileio_write_restore (THREAD_ENTRY * thread_p, FILEIO_RESTORE_PAGE_BITMAP * page_bitmap, int vol_fd, void *io_page_p,
		      VOLID vol_id, PAGEID page_id, FILEIO_BACKUP_LEVEL level)
{
  bool is_set;

  if (page_bitmap == NULL)
    {
      /* don't care about ht for this volume */
      if (fileio_write (thread_p, vol_fd, io_page_p, page_id, IO_PAGESIZE) == NULL)
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
	  if (fileio_write (thread_p, vol_fd, io_page_p, page_id, IO_PAGESIZE) == NULL)
	    {
	      return NULL;
	    }

	  if (level > FILEIO_BACKUP_FULL_LEVEL)
	    {
	      fileio_page_bitmap_set (page_bitmap, page_id);
	    }
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
  if (asprintf (&ptr1, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_IO, MSGCAT_FILEIO_STARTS)) < 0
      || asprintf (&ptr2, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_IO, reason)) < 0
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
      || asprintf (&reprompt_p, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_IO, MSGCAT_FILEIO_NEWLOCATION)) < 0
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
 * fileio_get_zip_method_string () - return the string name of the compression method
 *   return: pointer to string containing name of zip_method
 *   zip_method(in): the compression method to convert
 */
const char *
fileio_get_zip_method_string (FILEIO_ZIP_METHOD zip_method)
{
  switch (zip_method)
    {
    case FILEIO_ZIP_NONE_METHOD:
      return ("NONE");

    case FILEIO_ZIP_LZO1X_METHOD:
      return ("LZO1X");

    case FILEIO_ZIP_ZLIB_METHOD:
      return ("ZLIB");

    default:
      return ("UNKNOWN");
    }
}

/*
 * fileio_get_zip_level_string () - return the string name of the compression level
 *   return: pointer to string containing name of zip_level
 *   zip_level(in): the compression level to convert
 */
const char *
fileio_get_zip_level_string (FILEIO_ZIP_LEVEL zip_level)
{
  switch (zip_level)
    {
    case FILEIO_ZIP_NONE_LEVEL:
      return ("NONE");

    case FILEIO_ZIP_1_LEVEL:	/* case FILEIO_ZIP_LZO1X_DEFAULT_LEVEL: */
      return ("ZIP LEVEL 1 - BEST SPEED");

    case FILEIO_ZIP_2_LEVEL:
      return ("ZIP LEVEL 2");

    case FILEIO_ZIP_3_LEVEL:
      return ("ZIP LEVEL 3");

    case FILEIO_ZIP_4_LEVEL:
      return ("ZIP LEVEL 4");

    case FILEIO_ZIP_5_LEVEL:
      return ("ZIP LEVEL 5");

    case FILEIO_ZIP_6_LEVEL:	/* case FILEIO_ZIP_ZLIB_DEFAULT_LEVEL: */
      return ("ZIP LEVEL 6 - NORMAL");

    case FILEIO_ZIP_7_LEVEL:
      return ("ZIP LEVEL 7");

    case FILEIO_ZIP_8_LEVEL:
      return ("ZIP LEVEL 8");

    case FILEIO_ZIP_9_LEVEL:	/* case FILEIO_ZIP_LZO1X_999_LEVEL: */
      return ("ZIP LEVEL 9 - BEST REDUCTION");

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

  strncpy (node_p->bkvol_name, name_p, PATH_MAX - 1);
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
	  free_and_init (remote_data_p);
	}

      return ER_FAILED;
    }

  ptr = or_unpack_int (remote_data_p, &remote_status);
  if (remote_status != NO_ERROR)
    {
      free_and_init (remote_data_p);
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

  free_and_init (remote_data_p);
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
      fprintf (stdout, display_string_p);

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
		      fprintf (stdout, failure_prompt_p);
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
		      fprintf (stdout, failure_prompt_p);
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

#if !defined(WINDOWS)
/*
 * fileio_symlink () -
 *   return:
 *   src(in):
 *   dest(in):
 *   overwrite(in):
 */
int
fileio_symlink (const char *src_p, const char *dest_p, int overwrite)
{
  if (overwrite && fileio_is_volume_exist (dest_p))
    {
      unlink (dest_p);
    }

  if (symlink (src_p, dest_p) == -1)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_BO_CANNOT_CREATE_LINK, 2, src_p, dest_p);
      return ER_FAILED;
    }

  return NO_ERROR;
}

/*
 * fileio_lock_region () -
 *   return:
 *   fd(in):
 *   cmd(in):
 *   type(in):
 *   offset(in):
 *   whence(in):
 *   len(in):
 */
static int
fileio_lock_region (int fd, int cmd, int type, off_t offset, int whence, off_t len)
{
  struct flock lock;

  lock.l_type = type;		/* F_RDLOCK, F_WRLOCK, F_UNLOCK */
  lock.l_start = offset;	/* byte offset, relative to l_whence */
  lock.l_whence = whence;	/* SEEK_SET, SEEK_CUR, SEEK_END */
  lock.l_len = len;		/* #bytes (O means to EOF) */
  return fcntl (fd, cmd, &lock);
}
#endif /* !WINDOWS */

#if defined(SERVER_MODE)
/*
 * fileio_os_sysconf () -
 *   return:
 */
int
fileio_os_sysconf (void)
{
  long nprocs = -1;

#if defined(_SC_NPROCESSORS_ONLN)
  nprocs = sysconf (_SC_NPROCESSORS_ONLN);
#elif defined(_SC_NPROC_ONLN)
  nprocs = sysconf (_SC_NPROC_ONLN);
#elif defined(_SC_CRAY_NCPU)
  nprocs = sysconf (_SC_CRAY_NCPU);
#elif defined(WINDOWS)
  {
    SYSTEM_INFO sysinfo;
    /* determine the base of virtual memory */
    GetSystemInfo (&sysinfo);
    nprocs = sysinfo.dwNumberOfProcessors;
  }
#else /* WINDOWS */
  ;				/* give up */
#endif /* WINDOWS */
  return (nprocs > 1) ? (int) nprocs : 1;
}
#endif /* SERVER_MODE */

/*
 * fileio_initialize_res () -
 *   return:
 */
void
fileio_initialize_res (THREAD_ENTRY * thread_p, FILEIO_PAGE_RESERVED * prv_p)
{
  LSA_SET_NULL (&(prv_p->lsa));
  prv_p->pageid = -1;
  prv_p->volid = -1;

  prv_p->ptype = '\0';
  prv_p->pflag_reserve_1 = '\0';
  prv_p->p_reserve_2 = 0;
  prv_p->p_reserve_3 = 0;
}


/* 
 * PAGE BITMAP FUNCTIONS 
 */

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
 */
FILEIO_RESTORE_PAGE_BITMAP *
fileio_page_bitmap_create (int vol_id, int total_pages)
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
