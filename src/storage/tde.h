/*
 * 
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
 * tde.hpp - TDE (Transparent Data Encryption) Module
 */

#ifndef _TDE_HPP_
#define _TDE_HPP_

#ident "$Id$"

#include "storage_common.h"

#if defined (SA_MODE)
#include "boot_sr.h"
#endif

/* forward declarations */
struct fileio_page;
typedef fileio_page FILEIO_PAGE;
struct log_page;
typedef log_page LOG_PAGE;

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

/* TDE Key file item locations */
#define TDE_MK_FILE_CONTENTS_START  CUBRID_MAGIC_MAX_LENGTH
#define TDE_MK_FILE_ITEM_SIZE       (sizeof (TDE_MK_FILE_ITEM))
#define TDE_MK_FILE_ITEM_OFFSET(index) \
  (TDE_MK_FILE_CONTENTS_START + TDE_MK_FILE_ITEM_SIZE * (index))
#define TDE_MK_FILE_ITEM_INDEX(offset) \
  (((offset) - TDE_MK_FILE_CONTENTS_START) / TDE_MK_FILE_ITEM_SIZE)

#define TDE_MK_FILE_ITEM_COUNT_MAX 128

/*
 * Each value is also used to be index of tde_Algorithm_str[].
 * These must be changed togeter
 */
typedef enum
{
  TDE_ALGORITHM_NONE = 0,
  TDE_ALGORITHM_AES = 1,	/* AES 256 */
  TDE_ALGORITHM_ARIA = 2,	/* ARIA 256 */
} TDE_ALGORITHM;

typedef enum tde_data_key_type
{
  TDE_DATA_KEY_TYPE_PERM,
  TDE_DATA_KEY_TYPE_TEMP,
  TDE_DATA_KEY_TYPE_LOG
} TDE_DATA_KEY_TYPE;

typedef struct tde_data_key_set
{
  unsigned char perm_key[TDE_DATA_KEY_LENGTH];
  unsigned char temp_key[TDE_DATA_KEY_LENGTH];
  unsigned char log_key[TDE_DATA_KEY_LENGTH];
} TDE_DATA_KEY_SET;

typedef struct tde_mk_file_item
{
  time_t created_time;		/* If it is -1, it is invalid and avaliable for a new key */
  unsigned char master_key[TDE_MASTER_KEY_LENGTH];
} TDE_MK_FILE_ITEM;

#ifdef UNSTABLE_TDE_FOR_REPLICATION_LOG
#if defined(CS_MODE)
#define TDE_HA_SOCK_NAME ".ha_sock"
#endif /* CS_MODE */
#endif /* UNSTABLE_TDE_FOR_REPLICATION_LOG */

#if !defined(CS_MODE)

/* Is log record contains User Data */
#define LOG_MAY_CONTAIN_USER_DATA(rcvindex) \
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
   || (rcvindex) == RVBT_INS_PGRECORDS \
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
   || (rcvindex) == RVREPL_DATA_INSERT \
   || (rcvindex) == RVREPL_DATA_UPDATE \
   || (rcvindex) == RVREPL_DATA_DELETE \
   || (rcvindex) == RVREPL_DATA_UPDATE_START \
   || (rcvindex) == RVREPL_DATA_UPDATE_END \
   || (rcvindex) == RVBT_ONLINE_INDEX_UNDO_TRAN_INSERT \
   || (rcvindex) == RVBT_ONLINE_INDEX_UNDO_TRAN_DELETE)

#define tde_er_log(...) if (prm_get_bool_value (PRM_ID_ER_LOG_TDE)) _er_log_debug (ARG_FILE_LINE, "TDE: " __VA_ARGS__)

/*
 * TDE Cipher, the core object on memory, which is loaded at restart 
 * and used everywhere encryption or decription is requested.
 *
 * Note: Now TDE for replication log is disabled,
 * so CS_MODE version tde_cipher is not needed.
 */
typedef struct tde_cipher
{
  bool is_loaded;
  TDE_DATA_KEY_SET data_keys;	/* data keys decrypted from tde keyinfo heap, which is constant */
  int64_t temp_write_counter;	/* used as nonce for temp file page, it has to be dealt atomically */
} TDE_CIPHER;

extern TDE_CIPHER tde_Cipher;	/* global var for TDE Module */

/*
 * TDE module stores key information with all the data keys encrypted and master key hashed.
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

extern int tde_initialize (THREAD_ENTRY * thread_p, HFID * keyinfo_hfid);
extern int tde_cipher_initialize (THREAD_ENTRY * thread_p, const HFID * keyinfo_hfid, const char *mk_path_given);
extern int tde_get_keyinfo (THREAD_ENTRY * thread_p, TDE_KEYINFO * keyinfo);
extern bool tde_is_loaded ();

/*
 * tde functions for the master key management
 */
extern void tde_make_keys_file_fullname (char *keys_vol_fullname, const char *db_full_name, bool ignore_parm);
extern bool tde_validate_keys_file (int vdes);
extern int tde_copy_keys_file (THREAD_ENTRY * thread_p, const char *dest_fullname, const char *src_fullname,
			       bool keep_dest_mount, bool keep_src_mount);
extern int tde_load_mk (int vdes, const TDE_KEYINFO * keyinfo, unsigned char *master_key);
extern int tde_change_mk (THREAD_ENTRY * thread_p, const int mk_index, const unsigned char *master_key,
			  const time_t created_time);

/*
 * TDE functions for encrpytion and decryption
 */
extern int tde_encrypt_data_page (const FILEIO_PAGE * iopage_plain, TDE_ALGORITHM tde_algo, bool is_temp,
				  FILEIO_PAGE * iopage_cipher);
extern int tde_decrypt_data_page (const FILEIO_PAGE * iopage_cipher, TDE_ALGORITHM tde_algo, bool is_temp,
				  FILEIO_PAGE * iopage_plain);
/* 
 * Encryption/Decryption functions for logpage are also needed for applylogdb, copylogdb (CS_MODE),
 * but TDE for replication log is disabled now 
 */
extern int tde_encrypt_log_page (const LOG_PAGE * logpage_plain, TDE_ALGORITHM tde_algo, LOG_PAGE * logpage_cipher);
extern int tde_decrypt_log_page (const LOG_PAGE * logpage_cipher, TDE_ALGORITHM tde_algo, LOG_PAGE * logpage_plain);

#endif /* !CS_MODE */

/*
 * tde functions for the master key management
 */
extern int tde_create_mk (unsigned char *master_key, time_t * created_time);
extern int tde_add_mk (int vdes, const unsigned char *master_key, time_t created_time, int *mk_index);
extern int tde_find_mk (int vdes, int mk_index, unsigned char *master_key, time_t * created_time);
extern int tde_find_first_mk (int vdes, int *mk_index, unsigned char *master_key, time_t * created_time);
extern int tde_delete_mk (int vdes, const int mk_index);
extern void tde_print_mk (const unsigned char *master_key);
extern int tde_dump_mks (int vdes, bool print_value);
extern const char *tde_get_algorithm_name (TDE_ALGORITHM tde_algo);
#endif /* _TDE_HPP_ */
