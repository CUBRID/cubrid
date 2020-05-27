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

#include <atomic>

#include "storage_common.h"
#include "file_io.h"
#include "log_storage.hpp"

typedef enum 
{
    TDE_ALGORITHM_NONE,
    TDE_ALGORITHM_AES,
    TDE_ALGORITHM_ARIA,     
} TDE_ALGORITHM;


/* ENCRYPTION AREA */
#define TDE_DATA_PAGE_ENC_OFFSET    sizeof (FILEIO_PAGE_RESERVED)
#define TDE_DATA_PAGE_ENC_LENGTH    DB_PAGESIZE
#define TDE_LOG_PAGE_ENC_OFFSET     sizeof (LOG_HDRPAGE)
#define TDE_LOG_PAGE_ENC_LENGTH     ((LOG_PAGESIZE) - (TDE_LOG_PAGE_ENC_OFFSET))

/* 128 bit nonce */
#define TDE_DATA_PAGE_NONCE_LENGTH  16
#define TDE_LOG_PAGE_NONCE_LENGTH   16

/* TDE Keys - 256 bit */
#define TDE_MASTER_KEY_LENGTH 32
#define TDE_DATA_KEY_LENGTH   32

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

typedef struct tde_data_key_chain
{
  bool is_loaded;
  unsigned char perm_key[TDE_DATA_KEY_LENGTH];
  unsigned char temp_key[TDE_DATA_KEY_LENGTH];
  unsigned char log_key[TDE_DATA_KEY_LENGTH];
} TDE_DATA_KEY_CHAIN;

/*
 * TDE module 
 */
typedef struct tde_cipher
{
  bool is_master_key_loaded;
  unsigned char master_key[TDE_MASTER_KEY_LENGTH];
  TDE_DATA_KEY_CHAIN data_keys;
  
  std::atomic<std::int64_t> temp_write_counter; // used as nonce for temp file page 
} TDE_CIPHER;

extern TDE_CIPHER tde_Cipher;

/*
 * TDE functions for key management
 */

extern int tde_initialize (void); // is gonna be called in boot_restart_server()
extern int tde_set_master_key (int key_idx); // it must be transaction 
extern int tde_generate_data_keys (void); // is gonna be called in xboot_initialize_server()

/*
 * TDE functions for encrpytion and decryption
 */

extern int tde_encrypt_data_page (const unsigned char * iopage_plain, unsigned char * iopage_cipher, TDE_ALGORITHM tde_algo, bool is_temp);
extern int tde_decrypt_data_page (const unsigned char * iopage_cipher, unsigned char * iopage_plain, TDE_ALGORITHM tde_algo, bool is_temp);
extern int tde_encrypt_log_page (const unsigned char * iopage_plain, unsigned char * iopage_cipher, TDE_ALGORITHM tde_algo);
extern int tde_decrypt_log_page (const unsigned char * iopage_cipher, unsigned char * iopage_plain, TDE_ALGORITHM tde_algo);






#endif /* _TDE_HPP_ */
