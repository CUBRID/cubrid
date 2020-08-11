/*
 * Copyright (C) 2020 CUBRID Corporation. All rights reserved by CUBRID.
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
 * tde.hpp -
 */

#ifndef _TDE_HPP_
#define _TDE_HPP_

#ident "$Id$"

#include "storage_common.h"

struct fileio_page;
typedef fileio_page FILEIO_PAGE;
struct log_page;
typedef log_page LOG_PAGE;

/*
 * Each value is also used to be index of tde_Algorithm_str[].
 * These must be changed togeter
 */
typedef enum
{
  TDE_ALGORITHM_NONE = 0,
  TDE_ALGORITHM_AES = 1,
  TDE_ALGORITHM_ARIA = 2,
} TDE_ALGORITHM;

#define TDE_DK_ALGORITHM TDE_ALGORITHM_AES

/* ENCRYPTION AREA */
#define TDE_DATA_PAGE_ENC_OFFSET    sizeof (FILEIO_PAGE_RESERVED)
#define TDE_DATA_PAGE_ENC_LENGTH    DB_PAGESIZE
#define TDE_LOG_PAGE_ENC_OFFSET     sizeof (LOG_HDRPAGE)
#define TDE_LOG_PAGE_ENC_LENGTH     ((LOG_PAGESIZE) - (TDE_LOG_PAGE_ENC_OFFSET))

/* 128 bit nonce */
#define TDE_DATA_PAGE_NONCE_LENGTH  16
#define TDE_LOG_PAGE_NONCE_LENGTH   16
#define TDE_DK_NONCE_LENGTH         16

/* TDE Keys - 256 bit */
#define TDE_MASTER_KEY_LENGTH 32
#define TDE_DATA_KEY_LENGTH   32

typedef struct tde_mk_file_item
{
  time_t created_time;              /* If it is -1, it is invalid and avaliable for new key */
  unsigned  char master_key[TDE_MASTER_KEY_LENGTH];
} TDE_MK_FILE_ITEM;

#define TDE_MK_FILE_CONTENTS_START  CUBRID_MAGIC_MAX_LENGTH
#define TDE_MK_FILE_ITEM_SIZE       (sizeof (TDE_MK_FILE_ITEM))
#define TDE_MK_FILE_ITEM_OFFSET(index) \
  (TDE_MK_FILE_CONTENTS_START + TDE_MK_FILE_ITEM_SIZE * (index))
#define TDE_MK_FILE_ITEM_INDEX(offset) \
  (((offset) - TDE_MK_FILE_CONTENTS_START) / TDE_MK_FILE_ITEM_SIZE)

typedef struct tde_data_key_set
{
  unsigned char perm_key[TDE_DATA_KEY_LENGTH];
  unsigned char temp_key[TDE_DATA_KEY_LENGTH];
  unsigned char log_key[TDE_DATA_KEY_LENGTH];
} TDE_DATA_KEY_SET;

enum tde_data_key_type
{
  TDE_DATA_KEY_TYPE_PERM,
  TDE_DATA_KEY_TYPE_TEMP,
  TDE_DATA_KEY_TYPE_LOG
};
typedef tde_data_key_type TDE_DATA_KEY_TYPE;

#if 0
#if defined(CS_MODE)
#define TDE_HA_SOCK_NAME ".ha_sock"
#endif /* CS_MODE */
#endif /* TDE for replication log is disabled */

#if !defined(CS_MODE)
/*
 * TDE module
 *
 * Note: Now TDE for replication log is disabled,
 * so CS_MODE version tde_cipher is not needed.
 */
typedef struct tde_cipher
{
  bool is_loaded;
  TDE_DATA_KEY_SET data_keys;
  int64_t temp_write_counter; /* used as nonce for temp file page, it has to be dealt atomically */
} TDE_CIPHER;

extern TDE_CIPHER tde_Cipher;

/*
 * TDE module stores key information with all tha data keys encrypted and master key hashed.
 */
typedef struct tde_keyinfo
{
  int mk_index;
  time_t created_time;
  time_t set_time;
  unsigned char mk_hash[TDE_MASTER_KEY_LENGTH];
  unsigned char dk_perm[TDE_DATA_KEY_LENGTH];
  unsigned char dk_temp[TDE_DATA_KEY_LENGTH];
  unsigned char dk_log[TDE_DATA_KEY_LENGTH];
} TDE_KEYINFO;

/* Is log record contains User Data */
#define LOG_CONTAINS_USER_DATA(rcvindex) \
  ((rcvindex) == RVHF_INSERT \
   || (rcvindex) == RVHF_DELETE \
   || (rcvindex) == RVHF_UPDATE \
   || (rcvindex) == RVHF_MVCC_INSERT \
   || (rcvindex) == RVHF_MVCC_DELETE_MODIFY_HOME \
   || (rcvindex) == RVHF_UPDATE_NOTIFY_VACUUM \
   || (rcvindex) == RVHF_INSERT_NEWHOME \
   || (rcvindex) == RVHF_MVCC_REDISTRIBUTE \
   || (rcvindex) == RVHF_MVCC_UPDATE_OVERFLOW \
   || (rcvindex) == RVOVF_NEWPAGE_INSERT \
   || (rcvindex) == RVOVF_PAGE_UPDATE \
   || (rcvindex) == RVBT_NDRECORD_UPD \
   || (rcvindex) == RVBT_NDRECORD_INS \
   || (rcvindex) == RVBT_NDRECORD_DEL \
   || (rcvindex) == RVBT_COPYPAGE \
   || (rcvindex) == RVBT_DELETE_OBJECT_PHYSICAL \
   || (rcvindex) == RVBT_NON_MVCC_INSERT_OBJECT \
   || (rcvindex) == RVBT_MVCC_INSERT_OBJECT \
   || (rcvindex) == RVBT_MVCC_INSERT_OBJECT_UNQ \
   || (rcvindex) == RVBT_RECORD_MODIFY_UNDOREDO \
   || (rcvindex) == RVBT_RECORD_MODIFY_NO_UNDO \
   || (rcvindex) == RVBT_RECORD_MODIFY_COMPENSATE \
   || (rcvindex) == RVBT_DELETE_OBJECT_POSTPONE \
   || (rcvindex) == RVBT_MARK_DELETED \
   || (rcvindex) == RVREPL_DATA_INSERT \
   || (rcvindex) == RVREPL_DATA_UPDATE \
   || (rcvindex) == RVREPL_DATA_DELETE \
   || (rcvindex) == RVREPL_DATA_UPDATE_START \
   || (rcvindex) == RVREPL_DATA_UPDATE_END \
   || (rcvindex) == RVBT_ONLINE_INDEX_UNDO_TRAN_INSERT \
   || (rcvindex) == RVBT_ONLINE_INDEX_UNDO_TRAN_DELETE)

/*
 * TDE functions for key management
 */
extern int tde_initialize (THREAD_ENTRY *thread_p, HFID *keyinfo_hfid);
extern int tde_cipher_initialize (THREAD_ENTRY *thread_p, const HFID *keyinfo_hfid, const char *mk_path_given);
extern bool tde_validate_keys_volume (int vdes);
extern int tde_copy_keys_volume (THREAD_ENTRY *thread_p, const char *to_db_fullname, const char *from_db_fullname,
				 bool keep_to_mount, bool keep_from_mount);
extern void tde_make_keys_volume_fullname (char *keys_vol_fullname, const char *db_full_name, bool ignore_parm);
/*
 * TDE functions for encrpytion and decryption
 */
extern int tde_encrypt_data_page (FILEIO_PAGE *iopage_plain, FILEIO_PAGE *iopage_cipher, TDE_ALGORITHM tde_algo,
				  bool is_temp);
extern int tde_decrypt_data_page (const FILEIO_PAGE *iopage_cipher, FILEIO_PAGE *iopage_plain, TDE_ALGORITHM tde_algo,
				  bool is_temp);

/* Encryption/Decryption functions for logpage are also needed for applylogdb, copylogdb,
 * but TDE for replication log is disabled now */
extern int tde_encrypt_log_page (const LOG_PAGE *logpage_plain, LOG_PAGE *logpage_cipher, TDE_ALGORITHM tde_algo);
extern int tde_decrypt_log_page (const LOG_PAGE *logpage_cipher, LOG_PAGE *logpage_plain, TDE_ALGORITHM tde_algo);

#endif /* !CS_MODE */

extern int tde_create_mk (unsigned char *master_key);
extern void tde_print_mk (const unsigned char *master_key);
extern int tde_add_mk (int vdes, const unsigned char *master_key, int *mk_index, time_t created_time);
extern int tde_find_mk (int vdes, int mk_index, unsigned char *master_key, time_t *created_time);
extern int tde_delete_mk (int vdes, const int mk_index);
extern int tde_dump_mks (int vdes, bool print_value);
extern const char *tde_get_algorithm_name (TDE_ALGORITHM tde_algo);
#endif /* _TDE_HPP_ */
