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
 * file_io.h - I/O module at server
 *
 */

#ifndef _FILE_IO_H_
#define _FILE_IO_H_

#ident "$Id$"

#include "config.h"
#include "dbtype_def.h"
#include "log_lsa.hpp"
#include "memory_hash.h"
#include "porting.h"
#include "porting_inline.hpp"
#include "release_string.h"
#include "storage_common.h"
#include "thread_compat.hpp"

#include <stdio.h>
#include <time.h>
#include <map>

#define NULL_VOLDES   (-1)	/* Value of a null (invalid) vol descriptor */

/* Note: this value must be at least as large as PATH_MAX */
#define FILEIO_MAX_USER_RESPONSE_SIZE 2000

#if defined(WINDOWS)
#define S_ISDIR(mode) ((mode) & _S_IFDIR)
#define S_ISREG(mode) ((mode) & _S_IFREG)
#endif /* WINDOWS */

#define FILEIO_BACKUP_NUM_THREADS_AUTO    0
#define FILEIO_BACKUP_SLEEP_MSECS_AUTO    0

/* FILEIO_PAGE_FLAG (pflag in FILEIO_PAGE_RESERVED) */
#define FILEIO_PAGE_FLAG_ENCRYPTED_AES 0x1
#define FILEIO_PAGE_FLAG_ENCRYPTED_ARIA 0x2

#define FILEIO_PAGE_FLAG_ENCRYPTED_MASK 0x3

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
#define FILEIO_SUFFIX_DWB            "_dwb"
#define FILEIO_SUFFIX_KEYS           "_keys"
#define FILEIO_MAX_SUFFIX_LENGTH     7

/* used by both file_io.c and file_io_backup.c */
#define FILEIO_DISK_FORMAT_MODE            (O_RDWR | O_CREAT)
#define FILEIO_DISK_PROTECTION_MODE        0600
#define FILEIO_GET_FILE_SIZE(pagesize, npages)  \
  (((off_t)(pagesize)) * ((off_t)(npages)))
#define FILEIO_CHECK_FOR_INTERRUPT_INTERVAL       100

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
  FILEIO_ZIP_LZO1X_METHOD,	/* LZO1X - Unsupported */
  FILEIO_ZIP_ZLIB_METHOD,	/* ZLIB */
  FILEIO_ZIP_LZ4_METHOD,	/* LZ4X */
  FILEIO_ZIP_UNDEFINED_METHOD	/* Undefined (must be highest ordinal value) */
} FILEIO_ZIP_METHOD;

typedef enum
{
  FILEIO_ZIP_NONE_LEVEL,	/* None */
  FILEIO_ZIP_1_LEVEL,
  FILEIO_ZIP_UNDEFINED_LEVEL,	/* Undefined (must be highest ordinal value) */
  FILEIO_ZIP_LZ4_DEFAULT_LEVEL = FILEIO_ZIP_1_LEVEL
} FILEIO_ZIP_LEVEL;

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
  FILEIO_LOCKF,
  FILEIO_RUN_AWAY_LOCKF,
  FILEIO_NOT_LOCKF
} FILEIO_LOCKF_TYPE;

typedef enum
{
  FILEIO_WRITE_DEFAULT_WRITE,	/* default write mode does compensate write including sync */
  FILEIO_WRITE_NO_COMPENSATE_WRITE	/* skips */
} FILEIO_WRITE_MODE;

/* Reserved area of FILEIO_PAGE */
typedef struct fileio_page_reserved FILEIO_PAGE_RESERVED;
struct fileio_page_reserved
{
  LOG_LSA lsa;			/* Log Sequence number of page, Page recovery stuff */
  INT32 pageid;			/* Page identifier */
  INT16 volid;			/* Volume identifier where the page reside */
  unsigned char ptype;		/* Page type */
  unsigned char pflag;
  INT32 p_reserve_1;
  INT32 p_reserve_2;		/* unused - Reserved field */
  INT64 tde_nonce;		/* tde nonce. atomic counter for temp pages, lsa for perm pages */
};

typedef struct fileio_page_watermark FILEIO_PAGE_WATERMARK;
struct fileio_page_watermark
{
  LOG_LSA lsa;			/* duplication of prv.lsa */
};

/* The FILEIO_PAGE */
typedef struct fileio_page FILEIO_PAGE;
struct fileio_page
{
  FILEIO_PAGE_RESERVED prv;	/* System page area. Reserved */
  char page[1];			/* The user page area */

  // You cannot directly access prv2 like page_ptr.prv2, since it does not point to the real location */
  FILEIO_PAGE_WATERMARK prv2;	/* system page area. It should be located at the end of page. */
};

STATIC_INLINE FILEIO_PAGE_WATERMARK *
fileio_get_page_watermark_pos (FILEIO_PAGE * io_page, PGLENGTH page_size)
{
  return (FILEIO_PAGE_WATERMARK *) (((char *) io_page) + (page_size - sizeof (FILEIO_PAGE_WATERMARK)));
}

STATIC_INLINE void
fileio_init_lsa_of_page (FILEIO_PAGE * io_page, PGLENGTH page_size)
{
  LSA_SET_NULL (&io_page->prv.lsa);

  FILEIO_PAGE_WATERMARK *prv2 = fileio_get_page_watermark_pos (io_page, page_size);
  LSA_SET_NULL (&prv2->lsa);
}

STATIC_INLINE void
fileio_reset_page_lsa (FILEIO_PAGE * io_page, PGLENGTH page_size)
{
  LSA_SET_NULL (&io_page->prv.lsa);

  FILEIO_PAGE_WATERMARK *prv2 = fileio_get_page_watermark_pos (io_page, page_size);

  LSA_SET_NULL (&prv2->lsa);
}

STATIC_INLINE void
fileio_set_page_lsa (FILEIO_PAGE * io_page, const LOG_LSA * lsa, PGLENGTH page_size)
{
  LSA_COPY (&io_page->prv.lsa, lsa);

  FILEIO_PAGE_WATERMARK *prv2 = fileio_get_page_watermark_pos (io_page, page_size);

  LSA_COPY (&prv2->lsa, lsa);
}

STATIC_INLINE int
fileio_is_page_sane (FILEIO_PAGE * io_page, PGLENGTH page_size)
{
  FILEIO_PAGE_WATERMARK *prv2 = fileio_get_page_watermark_pos (io_page, page_size);

  return (LSA_EQ (&io_page->prv.lsa, &prv2->lsa));
}

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
#if !defined (CS_MODE)
extern int fileio_expand_to (THREAD_ENTRY * threda_p, VOLID volid, DKNPAGES npages_toadd, DB_VOLTYPE voltype);
#endif /* not CS_MODE */
extern void *fileio_initialize_pages (THREAD_ENTRY * thread_p, int vdes, FILEIO_PAGE * io_pgptr, DKNPAGES start_pageid,
				      DKNPAGES npages, size_t page_size, bool ensure_metadata,
				      int kbytes_to_be_written_per_sec);
extern void fileio_initialize_res (THREAD_ENTRY * thread_p, FILEIO_PAGE * io_page, PGLENGTH page_size);
#if defined (ENABLE_UNUSED_FUNCTION)
extern DKNPAGES fileio_truncate (VOLID volid, DKNPAGES npages_to_resize);
#endif
extern void fileio_unformat (THREAD_ENTRY * thread_p, const char *vlabel);
extern void fileio_unformat_and_rename (THREAD_ENTRY * thread_p, const char *vlabel, const char *new_vlabel);
extern int fileio_copy_volume (THREAD_ENTRY * thread_p, int from_vdes, DKNPAGES npages, const char *to_vlabel,
			       VOLID to_volid, bool reset_recvinfo);
extern int fileio_reset_volume (THREAD_ENTRY * thread_p, int vdes, const char *vlabel, DKNPAGES npages,
				const LOG_LSA * reset_lsa);
extern int fileio_mount (THREAD_ENTRY * thread_p, const char *db_fullname, const char *vlabel, VOLID volid,
			 int lockwait, bool dosync);
extern void fileio_dismount (THREAD_ENTRY * thread_p, int vdes);
extern void fileio_dismount_without_fsync (THREAD_ENTRY * thread_p, int vdes);
extern void fileio_dismount_all (THREAD_ENTRY * thread_p);
extern void *fileio_read (THREAD_ENTRY * thread_p, int vol_fd, void *io_page_p, PAGEID page_id, size_t page_size);
extern void *fileio_write_or_add_to_dwb (THREAD_ENTRY * thread_p, int vol_fd, FILEIO_PAGE * io_page_p, PAGEID page_id,
					 size_t page_size, bool ensure_metadata);
extern void *fileio_write (THREAD_ENTRY * thread_p, int vol_fd, void *io_page_p, PAGEID page_id, size_t page_size,
			   FILEIO_WRITE_MODE write_mode);
extern void *fileio_read_pages (THREAD_ENTRY * thread_p, int vol_fd, char *io_pages_p, PAGEID page_id, int num_pages,
				size_t page_size);
extern void *fileio_write_pages (THREAD_ENTRY * thread_p, int vol_fd, char *io_pages_p, PAGEID page_id, int num_pages,
				 size_t page_size, FILEIO_WRITE_MODE write_mode);
extern void *fileio_writev (THREAD_ENTRY * thread_p, int vdes, void **arrayof_io_pgptr, PAGEID start_pageid,
			    DKNPAGES npages, size_t page_size);
extern bool fileio_fsync_pending (void);
extern int fileio_synchronize_directory (THREAD_ENTRY * thread_p, const char *label);
extern int fileio_synchronize (THREAD_ENTRY * thread_p, int vdes, const char *vlabel, bool ensure_metadata);
extern void fileio_read_ahead_hint (int vol_fd, PAGEID page_id, int num_pages, size_t page_size);
extern int fileio_synchronize_all (THREAD_ENTRY * thread_p);
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
extern bool fileio_is_permanent_volume_descriptor (THREAD_ENTRY * thread_p, int vol_fd);
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
extern void fileio_make_log_archive_name (char *logarchive_name, const char *log_path, const char *dbname, int arvnum);
extern void fileio_make_removed_log_archive_name (char *logarchive_name, const char *log_path, const char *dbname);
extern void fileio_make_log_archive_temp_name (char *log_archive_temp_name_p, const char *log_path_p,
					       const char *db_name_p);
extern void fileio_make_log_info_name (char *loginfo_name, const char *log_path, const char *dbname);
extern void fileio_make_dwb_name (char *dwb_name_p, const char *dwb_path_p, const char *db_name_p);
extern void fileio_make_keys_name (char *keys_name_p, const char *db_name_p);
extern void fileio_make_keys_name_given_path (char *keys_name_p, const char *keys_path_p, const char *db_name_p);
#ifdef UNSTABLE_TDE_FOR_REPLICATION_LOG
extern void fileio_make_ha_sock_name (char *sock_path_p, const char *base_path_p, const char *sock_name_p);
#endif /* UNSTABLE_TDE_FOR_REPLICATION_LOG */
extern const char *fileio_get_zip_method_string (FILEIO_ZIP_METHOD zip_method);
extern const char *fileio_get_zip_level_string (FILEIO_ZIP_LEVEL zip_level);


/* defined in file_io.c, also used by file_io_backup.c */
extern int fileio_create (THREAD_ENTRY * thread_p, const char *db_fullname, const char *vlabel, VOLID volid,
			  bool dolock, bool dosync);
extern char *fileio_ctime (INT64 * clock, char *buf);

#if !defined(WINDOWS)
extern FILEIO_LOCKF_TYPE fileio_lock_la_log_path (const char *db_fullname, const char *lock_path, int vdes,
						  int *last_deleted_arv_num);
extern FILEIO_LOCKF_TYPE fileio_lock_la_dbname (int *lockf_vdes, char *db_name, char *log_path);
extern FILEIO_LOCKF_TYPE fileio_unlock_la_dbname (int *lockf_vdes, char *db_name, bool clear_owner);
extern int fileio_symlink (const char *src, const char *dest, int overwrite);
extern int fileio_set_permission (const char *vlabel);
#endif /* !WINDOWS */

#if defined(ENABLE_UNUSED_FUNCTION)
#if defined(SERVER_MODE)
int fileio_os_sysconf (void);
#endif /* SERVER_MODE */
#endif

/* flush control related */
extern int fileio_flush_control_initialize (void);
extern void fileio_flush_control_finalize (void);

/* flush token management */
extern int fileio_flush_control_add_tokens (THREAD_ENTRY * thread_p, INT64 diff_usec, int *token_gen,
					    int *token_consumed);

extern int fileio_set_page_checksum (THREAD_ENTRY * thread_p, FILEIO_PAGE * io_page);
extern int fileio_page_check_corruption (THREAD_ENTRY * thread_p, FILEIO_PAGE * io_page, bool * is_page_corrupted);
extern void fileio_page_hexa_dump (const char *data, int length);
extern bool fileio_is_formatted_page (THREAD_ENTRY * thread_p, const char *io_page);

/* lob_dir */
extern int fileio_lob_remove_dir (char *lob_path);
extern int fileio_lob_remove_matching_dir (const char *keyword);
#endif /* _FILE_IO_H_ */
