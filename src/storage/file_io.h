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
 * file_io.h - I/O module at server
 *
 */

#ifndef _FILE_IO_H_
#define _FILE_IO_H_

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include <time.h>

#include "porting.h"
#include "storage_common.h"
#include "release_string.h"
#include "dbtype.h"
#include "memory_hash.h"
#include "lzoconf.h"
#include "lzo1x.h"
#include "thread.h"

#define NULL_VOLDES   (-1)	/* Value of a null (invalid) vol descriptor */

#define FILEIO_INITIAL_BACKUP_UNITS    0

/* Note: this value must be at least as large as PATH_MAX */
#define FILEIO_MAX_USER_RESPONSE_SIZE 2000

#if defined(WINDOWS)
#define S_ISDIR(mode) ((mode) & _S_IFDIR)
#define S_ISREG(mode) ((mode) & _S_IFREG)
#endif /* WINDOWS */

#define FILEIO_FIRST_BACKUP_VOL_INFO      0
#define FILEIO_SECOND_BACKUP_VOL_INFO     1
#define FILEIO_BACKUP_NUM_THREADS_AUTO    0
#define FILEIO_BACKUP_SLEEP_MSECS_AUTO    0

#if defined(WINDOWS)
#define STR_PATH_SEPARATOR "\\"
#else /* WINDOWS */
#define STR_PATH_SEPARATOR "/"
#endif /* WINDOWS */

#define PEEK           true	/* Peek volume label pointer */
#define ALLOC_COPY  false	/* alloc and copy volume label */

/* If the last character of path string is PATH_SEPARATOR, don't append PATH_SEPARATOR */
#define FILEIO_PATH_SEPARATOR(path) \
  (path[strlen(path) - 1] == PATH_SEPARATOR ? "" : STR_PATH_SEPARATOR)

/* Definitions of some log archive and backup names */
#define FILEIO_SUFFIX_LOGACTIVE      "_lgat"
#define FILEIO_SUFFIX_LOGARCHIVE     "_lgar"
#define FILEIO_SUFFIX_TMP_LOGARCHIVE "_lgar_t"
#define FILEIO_SUFFIX_LOGINFO        "_lginf"
#define FILEIO_SUFFIX_BACKUP         "_bk"
#define FILEIO_SUFFIX_BACKUP_VOLINFO "_bkvinf"
#define FILEIO_VOLEXT_PREFIX         "_x"
#define FILEIO_VOLTMP_PREFIX         "_t"
#define FILEIO_VOLINFO_SUFFIX        "_vinf"
#define FILEIO_VOLLOCK_SUFFIX        "__lock"
#define FILEIO_MAX_SUFFIX_LENGTH     7

typedef enum
{
  FILEIO_BACKUP_FULL_LEVEL = 0,	/* Full backup */
  FILEIO_BACKUP_BIG_INCREMENT_LEVEL,	/* Backup since last full backup */
  FILEIO_BACKUP_SMALL_INCREMENT_LEVEL,	/* Backup since last INCRBIG */
  FILEIO_BACKUP_UNDEFINED_LEVEL	/* Undefined (must be highest ordinal value) */
} FILEIO_BACKUP_LEVEL;

typedef enum
{
  FILEIO_ZIP_NONE_METHOD,	/* None */
  FILEIO_ZIP_LZO1X_METHOD,	/* LZO1X */
  FILEIO_ZIP_ZLIB_METHOD,	/* ZLIB */
  FILEIO_ZIP_UNDEFINED_METHOD	/* Undefined (must be highest ordinal value) */
} FILEIO_ZIP_METHOD;

typedef enum
{
  FILEIO_ZIP_NONE_LEVEL,	/* None */
  FILEIO_ZIP_1_LEVEL,		/* best speed */
  FILEIO_ZIP_2_LEVEL,
  FILEIO_ZIP_3_LEVEL,
  FILEIO_ZIP_4_LEVEL,
  FILEIO_ZIP_5_LEVEL,
  FILEIO_ZIP_6_LEVEL,
  FILEIO_ZIP_7_LEVEL,
  FILEIO_ZIP_8_LEVEL,
  FILEIO_ZIP_9_LEVEL,		/* best compression */
  FILEIO_ZIP_UNDEFINED_LEVEL,	/* Undefined (must be highest ordinal value) */
  FILEIO_ZIP_LZO1X_999_LEVEL = FILEIO_ZIP_9_LEVEL,
  FILEIO_ZIP_LZO1X_DEFAULT_LEVEL = FILEIO_ZIP_1_LEVEL,
  FILEIO_ZIP_ZLIB_DEFAULT_LEVEL = FILEIO_ZIP_6_LEVEL
} FILEIO_ZIP_LEVEL;

typedef enum
{
  FILEIO_BACKUP_VOL_UNKNOWN,
  FILEIO_BACKUP_VOL_DIRECTORY,
  FILEIO_BACKUP_VOL_DEVICE
} FILEIO_BACKUP_VOL_TYPE;

typedef enum
{
  FILEIO_PROMPT_UNKNOWN,
  FILEIO_PROMPT_RANGE_TYPE,
  FILEIO_PROMPT_BOOLEAN_TYPE,
  FILEIO_PROMPT_STRING_TYPE,
  FILEIO_PROMPT_RANGE_WITH_SECONDARY_STRING_TYPE,
  FILEIO_PROMPT_DISPLAY_ONLY
} FILEIO_REMOTE_PROMPT_TYPE;

typedef enum
{
  FILEIO_ERROR_INTERRUPT,	/* error/interrupt */
  FILEIO_READ,			/* access device for read */
  FILEIO_WRITE			/* access device for write */
} FILEIO_TYPE;

typedef enum
{
  FILEIO_BACKUP_WRITE,		/* access backup device for write */
  FILEIO_BACKUP_READ		/* access backup device for read */
} FILEIO_BACKUP_TYPE;

typedef enum
{
  FILEIO_LOCKF,
  FILEIO_RUN_AWAY_LOCKF,
  FILEIO_NOT_LOCKF
} FILEIO_LOCKF_TYPE;

/* Reserved area of FILEIO_PAGE */
typedef struct fileio_page_reserved FILEIO_PAGE_RESERVED;
struct fileio_page_reserved
{
  LOG_LSA lsa;			/* Log Sequence number of page, Page recovery stuff */
  INT32 pageid;			/* Page identifier */
  INT16 volid;			/* Volume identifier where the page reside */
  unsigned char ptype;		/* Page type */
  unsigned char pflag_reserve_1;	/* unused - Reserved field */
  INT64 p_reserve_2;		/* unused - Reserved field */
  INT64 p_reserve_3;		/* unused - Reserved field */
};

/* The FILEIO_PAGE */
typedef struct fileio_page FILEIO_PAGE;
struct fileio_page
{
  FILEIO_PAGE_RESERVED prv;	/* System page area. Reserved */
  char page[1];			/* The user page area */
};


typedef struct fileio_backup_page FILEIO_BACKUP_PAGE;
struct fileio_backup_page
{
  PAGEID iopageid;		/* Identifier of page to buffer */
  INT32 dummy;			/* Dummy field for 8byte align */
  FILEIO_PAGE iopage;		/* The content of the page */
  PAGEID iopageid_dup;		/* Copy of pageid for redundant checking during restore. Note: that the offset of this
				 * field cannot be used, because the size of an iopage is not know until run-time.
				 * Take care when trying to access this value. */
};

/*
 * During incremental restores, this structure helps keep track of
 * which pages have already been restored, so we do not overwrite newer
 * pages from an earlier backup.
 */
typedef struct page_bitmap FILEIO_RESTORE_PAGE_BITMAP;
struct page_bitmap
{
  FILEIO_RESTORE_PAGE_BITMAP *next;
  int vol_id;
  int size;
  unsigned char *bitmap;
};

typedef struct page_bitmap_list FILEIO_RESTORE_PAGE_BITMAP_LIST;
struct page_bitmap_list
{
  FILEIO_RESTORE_PAGE_BITMAP *head;
  FILEIO_RESTORE_PAGE_BITMAP *tail;
};

typedef struct fileio_backup_record_info FILEIO_BACKUP_RECORD_INFO;
struct fileio_backup_record_info
{
  INT64 at_time;
  LOG_LSA lsa;
};

/* Backup header */

typedef struct fileio_backup_header FILEIO_BACKUP_HEADER;
struct fileio_backup_header
{
  PAGEID iopageid;		/* Must be the same as start of an FILEIO_BACKUP_PAGE NOTE: a union would be better. */
  char magic[CUBRID_MAGIC_MAX_LENGTH];	/* Magic value for file/magic Unix utility */
  float db_compatibility;	/* Compatibility of the database against the current release of CUBRID */
  int bk_hdr_version;		/* For future compatibility checking */
  INT64 db_creation;		/* Database creation time */
  INT64 start_time;		/* Time of backup start */
  INT64 end_time;		/* Time of backup end */
  char db_release[REL_MAX_RELEASE_LENGTH];	/* CUBRID Release */
  char db_fullname[PATH_MAX];	/* Fullname of backed up database. Really more than one byte */
  PGLENGTH db_iopagesize;	/* Size of database pages */
  FILEIO_BACKUP_LEVEL level;	/* Backup level: one of the following level 0: Full backup, every database page that
				 * has been allocated. level 1: All database pages that have changed since last level 0 
				 * backup level 2: All database pages that have changed since last level 0 or 1. */
  LOG_LSA start_lsa;		/* A page with a LSA greater than this value is going to be backed up. */
  LOG_LSA chkpt_lsa;		/* LSA for next incremental backup */

  /* remember lsa's for every backup level */
  int unit_num;			/* Part # of removable backup vol, count from 1 */
  int bkup_iosize;		/* Buffered io size when backup was taken */
  FILEIO_BACKUP_RECORD_INFO previnfo[FILEIO_BACKUP_UNDEFINED_LEVEL];

  /* Backward chain to preceding backup volume. */
  char db_prec_bkvolname[PATH_MAX];

  /* Forward chain to next backup volume. Note not implemented yet. */
  char db_next_bkvolname[PATH_MAX];

  int bkpagesize;		/* size of backup page */
  FILEIO_ZIP_METHOD zip_method;	/* compression method */
  FILEIO_ZIP_LEVEL zip_level;	/* compression level */
  int skip_activelog;
};

/* Shouldn't this structure should use int and such? */
typedef struct fileio_backup_buffer FILEIO_BACKUP_BUFFER;
struct fileio_backup_buffer
{
  char loc_db_fullname[PATH_MAX];	/* Fullname specified in the database-loc-file */
  char log_path[PATH_MAX];	/* for restore */
  LOG_LSA last_chkpt_lsa;	/* The chkpt_lsa of the highest level backup volume in the restore session. */
  int vdes;			/* Open descriptor of backup device */
  const char *vlabel;		/* Pointer to current backup device name */
  char name[PATH_MAX];		/* Name of the current backup volume: either a file, or a raw device. */

  /* Original source location to backup (restore) volumes.  Can be a directory or raw device. Used for mult. volumes. */
  char current_path[PATH_MAX];

  int dtype;			/* Set to the type (dir, file, dev) */
  int iosize;			/* Optimal I/O pagesize for backup device */
  int count;			/* Number of current buffered bytes */
  INT64 voltotalio;		/* Total number of bytes that have been either read or written (current volume) */
  INT64 alltotalio;		/* total for all volumes */
  char *buffer;			/* Pointer to the buffer */
  char *ptr;			/* Pointer to the first buffered byte when reading and pointer to the next byte to
				 * buffer when writing */
  FILEIO_BACKUP_HEADER *bkuphdr;	/* pointer to header information */
};

typedef struct fileio_backup_db_buffer FILEIO_BACKUP_DB_BUFFER;
struct fileio_backup_db_buffer
{
  FILEIO_BACKUP_LEVEL level;	/* Backup level: one of the following level 0: Full backup, every database page that
				 * has been allocated. level 1: All database pages that have changed since last level 0 
				 * backup level 2: All database pages that have changed since last level 0 or 1. */
  LOG_LSA lsa;			/* A page with a LSA greater than this value is going to be backed up. */
  int vdes;			/* Open file descriptor of device name for writing purposes */
  VOLID volid;			/* Identifier of volume to backup/restore */
  INT64 nbytes;			/* Number of bytes of file */
  const char *vlabel;		/* Pointer to file name to backup */
#if (__WORDSIZE == 32)
  int dummy;			/* Dummy field for 8byte align */
#endif
  FILEIO_BACKUP_PAGE *area;	/* Area to read/write the page */
};

typedef struct file_zip_page FILEIO_ZIP_PAGE;
struct file_zip_page
{
  lzo_uint buf_len;		/* compressed block size */
  lzo_byte buf[1];		/* data block */
};

typedef struct fileio_node FILEIO_NODE;
struct fileio_node
{
  struct fileio_node *prev;
  struct fileio_node *next;
  int pageid;
  bool writeable;
  ssize_t nread;
  FILEIO_BACKUP_PAGE *area;	/* Area to read/write the page */
  FILEIO_ZIP_PAGE *zip_page;	/* Area to compress/decompress the page */
  lzo_bytep wrkmem;
};

typedef struct fileio_queue FILEIO_QUEUE;
struct fileio_queue
{
  int size;
  FILEIO_NODE *head;
  FILEIO_NODE *tail;
  FILEIO_NODE *free_list;
};

typedef struct fileio_thread_info FILEIO_THREAD_INFO;
struct fileio_thread_info
{
#if defined(SERVER_MODE)
  pthread_mutex_t mtx;
  pthread_cond_t rcv;		/* condition variable of read_thread */
  pthread_cond_t wcv;		/* condition variable of write_thread */
#endif				/* SERVER_MODE */

  int tran_index;

  int num_threads;		/* number of read threads plus one write thread */
  int act_r_threads;		/* number of activated read threads */
  int end_r_threads;		/* number of ended read threads */

  int pageid;
  int from_npages;

  FILEIO_TYPE io_type;
  int errid;

  bool only_updated_pages;
  bool initialized;

  int check_ratio;
  int check_npages;

  FILEIO_QUEUE io_queue;
};

typedef struct io_backup_session FILEIO_BACKUP_SESSION;
struct io_backup_session
{
  FILEIO_BACKUP_TYPE type;
  FILEIO_BACKUP_BUFFER bkup;	/* Buffering area for backup device */
  FILEIO_BACKUP_DB_BUFFER dbfile;	/* Buffer area for database files */
  FILEIO_THREAD_INFO read_thread_info;	/* read-threads info */
  FILE *verbose_fp;		/* Backupdb/Restoredb status msg */
  int sleep_msecs;		/* sleep internval in msecs */
};

typedef struct token_bucket TOKEN_BUCKET;
struct token_bucket
{
  pthread_mutex_t token_mutex;
  int tokens;			/* shared tokens between all lines */
  int token_consumed;		/* TODO: Remove me? This seems to server no true purpose. */

  pthread_cond_t waiter_cond;
};

typedef struct flush_stats FLUSH_STATS;
struct flush_stats
{
  unsigned int num_log_pages;
  unsigned int num_pages;
  unsigned int num_tokens;
};

extern int fileio_open (const char *vlabel, int flags, int mode);
extern void fileio_close (int vdes);
extern int fileio_format (THREAD_ENTRY * thread_p, const char *db_fullname, const char *vlabel, VOLID volid,
			  DKNPAGES npages, bool sweep_clean, bool dolock, bool dosync, size_t page_size,
			  int kbytes_to_be_written_per_sec, bool reuse_file);
extern DKNPAGES fileio_expand (THREAD_ENTRY * threda_p, VOLID volid, DKNPAGES npages_toadd, DB_VOLTYPE voltype);
extern void *fileio_initialize_pages (THREAD_ENTRY * thread_p, int vdes, void *io_pgptr, DKNPAGES start_pageid,
				      DKNPAGES npages, size_t page_size, int kbytes_to_be_written_per_sec);
extern void fileio_initialize_res (THREAD_ENTRY * thread_p, FILEIO_PAGE_RESERVED * prv_p);
#if defined (ENABLE_UNUSED_FUNCTION)
extern DKNPAGES fileio_truncate (VOLID volid, DKNPAGES npages_to_resize);
#endif
extern void fileio_unformat (THREAD_ENTRY * thread_p, const char *vlabel);
extern void fileio_unformat_and_rename (THREAD_ENTRY * thread_p, const char *vlabel, const char *new_vlabel);
extern int fileio_copy_volume (THREAD_ENTRY * thread_p, int from_vdes, DKNPAGES npages, const char *to_vlabel,
			       VOLID to_volid, bool reset_recvinfo);
extern int fileio_reset_volume (THREAD_ENTRY * thread_p, int vdes, const char *vlabel, DKNPAGES npages,
				LOG_LSA * reset_lsa);
extern int fileio_mount (THREAD_ENTRY * thread_p, const char *db_fullname, const char *vlabel, VOLID volid,
			 int lockwait, bool dosync);
extern void fileio_dismount (THREAD_ENTRY * thread_p, int vdes);
extern void fileio_dismount_all (THREAD_ENTRY * thread_p);
extern void *fileio_read (THREAD_ENTRY * thread_p, int vol_fd, void *io_page_p, PAGEID page_id, size_t page_size);
extern void *fileio_write (THREAD_ENTRY * thread_p, int vol_fd, void *io_page_p, PAGEID page_id, size_t page_size);
extern void *fileio_read_pages (THREAD_ENTRY * thread_p, int vol_fd, char *io_pages_p, PAGEID page_id, int num_pages,
				size_t page_size);
extern void *fileio_write_pages (THREAD_ENTRY * thread_p, int vol_fd, char *io_pages_p, PAGEID page_id, int num_pages,
				 size_t page_size);
extern void *fileio_writev (THREAD_ENTRY * thread_p, int vdes, void **arrayof_io_pgptr, PAGEID start_pageid,
			    DKNPAGES npages, size_t page_size);
extern int fileio_synchronize (THREAD_ENTRY * thread_p, int vdes, const char *vlabel);
extern int fileio_synchronize_all (THREAD_ENTRY * thread_p, bool include_log);
#if defined (ENABLE_UNUSED_FUNCTION)
extern void *fileio_read_user_area (THREAD_ENTRY * thread_p, int vdes, PAGEID pageid, off_t start_offset, size_t nbytes,
				    void *area);
extern void *fileio_write_user_area (THREAD_ENTRY * thread_p, int vdes, PAGEID pageid, off_t start_offset, int nbytes,
				     void *area);
#endif
extern bool fileio_is_volume_exist_and_file (const char *vlabel);
extern DKNPAGES fileio_get_number_of_volume_pages (int vdes, size_t page_size);
extern char *fileio_get_volume_label (VOLID volid, bool is_peek);
extern char *fileio_get_volume_label_by_fd (int vol_fd, bool is_peek);
extern VOLID fileio_find_volume_id_with_label (THREAD_ENTRY * thread_p, const char *vlabel);
extern bool fileio_is_temp_volume (THREAD_ENTRY * thread_p, VOLID volid);
extern VOLID fileio_find_next_perm_volume (THREAD_ENTRY * thread_p, VOLID volid);
extern VOLID fileio_find_previous_perm_volume (THREAD_ENTRY * thread_p, VOLID volid);
extern VOLID fileio_find_previous_temp_volume (THREAD_ENTRY * thread_p, VOLID volid);

extern int fileio_get_volume_descriptor (VOLID volid);
extern bool fileio_map_mounted (THREAD_ENTRY * thread_p, bool (*fun) (THREAD_ENTRY * thread_p, VOLID volid, void *args),
				void *args);
extern int fileio_get_number_of_partition_free_pages (const char *path, size_t page_size);	/* remove me */
extern DKNSECTS fileio_get_number_of_partition_free_sectors (const char *path_p);
extern const char *fileio_rename (VOLID volid, const char *old_vlabel, const char *new_vlabel);
extern bool fileio_is_volume_exist (const char *vlabel);
extern int fileio_find_volume_descriptor_with_label (const char *vol_label_p);
extern int fileio_get_max_name (const char *path, long int *filename_max, long int *pathname_max);
extern const char *fileio_get_base_file_name (const char *fullname);
extern char *fileio_get_directory_path (char *path, const char *fullname);
extern int fileio_get_volume_max_suffix (void);
extern void fileio_make_volume_info_name (char *volinfo_name, const char *db_fullname);
extern void fileio_make_volume_ext_name (char *volext_fullname, const char *ext_path, const char *ext_name,
					 VOLID volid);
extern void fileio_make_volume_ext_given_name (char *volext_fullname, const char *ext_path, const char *ext_name);
extern void fileio_make_volume_temp_name (char *voltmp_fullname, const char *tmp_path, const char *tmp_name,
					  VOLID volid);
extern void fileio_make_log_active_name (char *logactive_name, const char *log_path, const char *dbname);
extern void fileio_make_log_active_temp_name (char *logactive_tmpname, FILEIO_BACKUP_LEVEL level,
					      const char *active_name);
extern void fileio_make_log_archive_name (char *logarchive_name, const char *log_path, const char *dbname, int arvnum);
extern void fileio_make_removed_log_archive_name (char *logarchive_name, const char *log_path, const char *dbname);
extern void fileio_make_log_archive_temp_name (char *log_archive_temp_name_p, const char *log_path_p,
					       const char *db_name_p);
extern void fileio_make_log_info_name (char *loginfo_name, const char *log_path, const char *dbname);
extern void fileio_make_backup_volume_info_name (char *backup_volinfo_name, const char *backinfo_path,
						 const char *dbname);
extern void fileio_make_backup_name (char *backup_name, const char *nopath_volname, const char *backup_path,
				     FILEIO_BACKUP_LEVEL level, int unit_num);
extern void fileio_remove_all_backup (THREAD_ENTRY * thread_p, int level);
extern FILEIO_BACKUP_SESSION *fileio_initialize_backup (const char *db_fullname, const char *backup_destination,
							FILEIO_BACKUP_SESSION * session, FILEIO_BACKUP_LEVEL level,
							const char *verbose_file_path, int num_threads,
							int sleep_msecs);
extern FILEIO_BACKUP_SESSION *fileio_start_backup (THREAD_ENTRY * thread_p, const char *db_fullname,
						   INT64 * db_creation, FILEIO_BACKUP_LEVEL backup_level,
						   LOG_LSA * backup_start_lsa, LOG_LSA * backup_ckpt_lsa,
						   FILEIO_BACKUP_RECORD_INFO * all_levels_info,
						   FILEIO_BACKUP_SESSION * session, FILEIO_ZIP_METHOD zip_method,
						   FILEIO_ZIP_LEVEL zip_level);
extern FILEIO_BACKUP_SESSION *fileio_finish_backup (THREAD_ENTRY * thread_p, FILEIO_BACKUP_SESSION * session);
extern void fileio_abort_backup (THREAD_ENTRY * thread_p, FILEIO_BACKUP_SESSION * session, bool does_unformat_bk);
extern int fileio_backup_volume (THREAD_ENTRY * thread_p, FILEIO_BACKUP_SESSION * session, const char *from_vlabel,
				 VOLID from_volid, PAGEID last_page, bool only_updated_pages);
extern FILEIO_BACKUP_SESSION *fileio_start_restore (THREAD_ENTRY * thread_p, const char *db_fullname,
						    char *backup_source, INT64 match_dbcreation,
						    PGLENGTH * db_iopagesize, float *db_compatibility,
						    FILEIO_BACKUP_SESSION * session, FILEIO_BACKUP_LEVEL level,
						    bool authenticate, INT64 match_bkupcreation,
						    const char *restore_verbose_file_path, bool newvolpath);
extern int fileio_finish_restore (THREAD_ENTRY * thread_p, FILEIO_BACKUP_SESSION * session);
extern void fileio_abort_restore (THREAD_ENTRY * thread_p, FILEIO_BACKUP_SESSION * session);
extern int fileio_list_restore (THREAD_ENTRY * thread_p, const char *db_fullname, char *backup_source,
				FILEIO_BACKUP_LEVEL level, bool newvolpath);
extern int fileio_get_backup_volume (THREAD_ENTRY * thread_p, const char *db_fullname, const char *logpath,
				     const char *user_backuppath, int try_level, char *from_volbackup);
extern int fileio_get_next_restore_file (THREAD_ENTRY * thread_p, FILEIO_BACKUP_SESSION * session, char *filename,
					 VOLID * volid);
extern int fileio_restore_volume (THREAD_ENTRY * thread_p, FILEIO_BACKUP_SESSION * session, char *to_vlabel,
				  char *verbose_to_vlabel, char *prev_vlabel, FILEIO_RESTORE_PAGE_BITMAP * page_bitmap,
				  bool remember_pages);
extern int fileio_skip_restore_volume (THREAD_ENTRY * thread_p, FILEIO_BACKUP_SESSION * session);
extern const char *fileio_get_zip_method_string (FILEIO_ZIP_METHOD zip_method);
extern const char *fileio_get_zip_level_string (FILEIO_ZIP_LEVEL zip_level);


extern int fileio_read_backup_info_entries (FILE * fp, int which_bkvinf);
extern int fileio_write_backup_info_entries (FILE * fp, int which_bkvinf);
extern const char *fileio_get_backup_info_volume_name (FILEIO_BACKUP_LEVEL level, int unit_num, int which_bkvinf);
extern int fileio_add_volume_to_backup_info (const char *name, FILEIO_BACKUP_LEVEL level, int unit_num,
					     int which_bkvinf);
extern int fileio_clear_backup_info_level (int level, bool dealloc, int which_bkvinf);
extern void fileio_finalize_backup_info (int which_bkvinf);

extern int fileio_request_user_response (THREAD_ENTRY * thread_p, FILEIO_REMOTE_PROMPT_TYPE prompt_id,
					 const char *prompt, char *response, const char *failure_prompt, int range_low,
					 int range_high, const char *secondary_prompt, int reprompt_value);

#if !defined(WINDOWS)
extern FILEIO_LOCKF_TYPE fileio_lock_la_log_path (const char *db_fullname, const char *lock_path, int vdes,
						  int *last_deleted_arv_num);
extern FILEIO_LOCKF_TYPE fileio_lock_la_dbname (int *lockf_vdes, char *db_name, char *log_path);
extern FILEIO_LOCKF_TYPE fileio_unlock_la_dbname (int *lockf_vdes, char *db_name, bool clear_owner);
extern int fileio_symlink (const char *src, const char *dest, int overwrite);
extern int fileio_set_permission (const char *vlabel);
#endif /* !WINDOWS */

#if defined(SERVER_MODE)
int fileio_os_sysconf (void);
#endif /* SERVER_MODE */

/* flush control related */
extern int fileio_flush_control_initialize (void);
extern void fileio_flush_control_finalize (void);

/* flush token management */
extern int fileio_flush_control_add_tokens (THREAD_ENTRY * thread_p, INT64 diff_usec, int *token_gen,
					    int *token_consumed);

extern void fileio_page_bitmap_list_init (FILEIO_RESTORE_PAGE_BITMAP_LIST * page_bitmap_list);
extern FILEIO_RESTORE_PAGE_BITMAP *fileio_page_bitmap_create (int vol_id, int total_pages);
extern FILEIO_RESTORE_PAGE_BITMAP *fileio_page_bitmap_list_find (FILEIO_RESTORE_PAGE_BITMAP_LIST * page_bitmap_list,
								 int vol_id);
extern void fileio_page_bitmap_list_add (FILEIO_RESTORE_PAGE_BITMAP_LIST * page_bitmap_list,
					 FILEIO_RESTORE_PAGE_BITMAP * page_bitmap);
extern void fileio_page_bitmap_list_destroy (FILEIO_RESTORE_PAGE_BITMAP_LIST * page_bitmap_list);
#endif /* _FILE_IO_H_ */
