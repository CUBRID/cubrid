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
 * file_io_backup.h - backup and restore of database volumes (at server)
 *
 */

#ifndef _FILE_IO_BACKUP_H_
#define _FILE_IO_BACKUP_H_

#ident "$Id$"

#include "file_io.h"

/* backup and restore run only in the server and standalone builds */
#if defined (SERVER_MODE) || defined (SA_MODE)

#define FILEIO_INITIAL_BACKUP_UNITS    0
#define FILEIO_NO_BACKUP_UNITS         -1

#define FILEIO_FIRST_BACKUP_VOL_INFO      0
#define FILEIO_SECOND_BACKUP_VOL_INFO     1

typedef enum
{
  FILEIO_BACKUP_VOL_UNKNOWN,
  FILEIO_BACKUP_VOL_DIRECTORY,
  FILEIO_BACKUP_VOL_DEVICE
} FILEIO_BACKUP_VOL_TYPE;

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
  int npages;			/* pages of the volume, from the most recent backup level that holds it */
  char vol_label[PATH_MAX];	/* restored volume */
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
  LOG_LSA start_log_end_lsa;	/* Log end (next LSA to be assigned) when this backup started copying pages.  Valid
				 * from bk_hdr_version 3, NULL_LSA in older headers.  Media recovery lets a no-logging
				 * index build's barrier pass when it precedes this LSA of the top restored level. */
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
  INT64 iosize;			/* Optimal I/O pagesize for backup device */
  INT64 count;			/* Number of current buffered bytes */
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
  int buf_len;			/* compressed block size */
  char buf[1];			/* data block */
};

typedef struct file_zip_info FILEIO_ZIP_INFO;
struct file_zip_info
{
  int buf_size;			/* allocated block size */
  FILEIO_ZIP_PAGE zip_page;	/* zip page */
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
  FILEIO_ZIP_INFO *zip_info;	/* Zip info containing area to compress/decompress the page */
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

// *INDENT-OFF*
using FILEIO_UNLINKED_VOLINFO_MAP = std::map <int, std::pair<std::string, std::string>>;
// *INDENT-ON*

extern void fileio_make_temp_log_files_from_backup (char *temp_log_name, VOLID volid, FILEIO_BACKUP_LEVEL level,
						    const char *base_log_name);
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
				  bool remember_pages, bool & is_prev_vheader_restored,
				  FILEIO_UNLINKED_VOLINFO_MAP & unlinked_vol_info);
extern int fileio_skip_restore_volume (THREAD_ENTRY * thread_p, FILEIO_BACKUP_SESSION * session);

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

extern void fileio_page_bitmap_list_init (FILEIO_RESTORE_PAGE_BITMAP_LIST * page_bitmap_list);
extern FILEIO_RESTORE_PAGE_BITMAP *fileio_page_bitmap_create (int vol_id, int total_pages, const char *vol_label);
extern int fileio_restore_format_unrestored_pages (THREAD_ENTRY * thread_p,
						   FILEIO_RESTORE_PAGE_BITMAP_LIST * page_bitmap_list,
						   FILEIO_RESTORE_PAGE_BITMAP ** failed_bitmap);
extern FILEIO_RESTORE_PAGE_BITMAP *fileio_page_bitmap_list_find (FILEIO_RESTORE_PAGE_BITMAP_LIST * page_bitmap_list,
								 int vol_id);
extern void fileio_page_bitmap_list_add (FILEIO_RESTORE_PAGE_BITMAP_LIST * page_bitmap_list,
					 FILEIO_RESTORE_PAGE_BITMAP * page_bitmap);
extern void fileio_page_bitmap_list_destroy (FILEIO_RESTORE_PAGE_BITMAP_LIST * page_bitmap_list);

#endif /* defined (SERVER_MODE) || (SA_MODE) */

#endif /* _FILE_IO_BACKUP_H_ */
