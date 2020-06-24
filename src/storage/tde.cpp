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
 * tde.cpp -
 */

#ident "$Id$"

#include <stdlib.h>
#include <assert.h>

#include <openssl/conf.h>
#include <openssl/evp.h>
#include <openssl/err.h>
#include <openssl/sha.h>
#include <openssl/rand.h>

#if !defined(CS_MODE)
#include "heap_file.h"
#include "btree.h"
#include "system_parameter.h"
#include "boot_sr.h"
#include "file_io.h"
#endif /* !CS_MODE */

#include "error_code.h"
#include "log_storage.hpp"
#include "tde.hpp"

#if defined(CS_MODE)
TDE_DATA_KEY_SET tde_Data_keys; /* data keys for copylogdb, applylogdb from server */
#endif /* CS_MODE */

#if !defined(CS_MODE)
TDE_CIPHER tde_Cipher; // global var for TDE Module

static OID tde_Keys_oid;	/* Location of keys */
static OID *tde_Keyinfo_oid = &tde_Keys_oid;

static int tde_get_keyinfo (THREAD_ENTRY *thread_p, TDE_KEYINFO *keyinfo, OID *keyinfo_oid, const HFID *hfid);
static int tde_update_keyinfo (THREAD_ENTRY *thread_p, const TDE_KEYINFO *keyinfo, const OID keyinfo_oid, HFID *hfid);
static void tde_make_keys_volume_fullname (char *keys_vol_fullname);
static int tde_generate_keyinfo (TDE_KEYINFO *keyinfo);
static int tde_generate_keyinfo_internal (TDE_KEYINFO *keyinfo, int mk_index, const unsigned char *master_key,
    const TDE_DATA_KEY_SET *dks);
static int tde_create_keys_volume (const char *keys_path);
static int tde_load_mk (const TDE_KEYINFO *keyinfo, const char *mk_path);
static bool tde_validate_mk (const unsigned char *master_key, const unsigned char *mk_hash);
static int tde_load_dks (const TDE_KEYINFO *keyinfo, const unsigned char *master_key);
static int tde_make_mk (unsigned char *master_key);
static void tde_make_mk_hash (const unsigned char *master_key, unsigned char *mk_hash);
static int tde_make_dk (unsigned char *data_key);
static int tde_encrypt_dk (const unsigned char *dk_plain, TDE_DATA_KEY_TYPE dk_type, const unsigned char *master_key,
			   unsigned char *dk_cipher);
static int tde_decrypt_dk (const unsigned char *dk_cipher, TDE_DATA_KEY_TYPE dk_type, const unsigned char *master_key,
			   unsigned char *dk_plain);
static void tde_dk_nonce (unsigned char *dk_nonce, TDE_DATA_KEY_TYPE dk_type);
#endif /* !CS_MODE */

static int tde_encrypt_internal (const unsigned char *plain_buffer, int length, TDE_ALGORITHM tde_algo,
				 const unsigned char *key, const unsigned char *nonce, unsigned char *cipher_buffer);
static int tde_decrypt_internal (const unsigned char *cipher_buffer, int length, TDE_ALGORITHM tde_algo,
				 const unsigned char *key, const unsigned char *nonce, unsigned char *plain_buffer);

#if !defined(CS_MODE)


/*
 *
 */
int
tde_initialize (THREAD_ENTRY *thread_p, HFID *keyinfo_hfid)
{
  char mk_path[PATH_MAX] = {0, };
  unsigned char default_mk[TDE_MASTER_KEY_LENGTH] = {0,};
  int err = NO_ERROR;
  RECDES recdes;
  HEAP_OPERATION_CONTEXT heapop_context;
  TDE_KEYINFO keyinfo;
  TDE_DATA_KEY_SET dks;

  tde_make_keys_volume_fullname (mk_path);
  err = tde_create_keys_volume (mk_path);
  if (err != NO_ERROR)
    {
      return err;
    }

  err = tde_make_mk (default_mk);
  if (err != NO_ERROR)
    {
      return err;
    }
  tde_add_mk (TDE_DEFAULT_MK_INDEX, default_mk);

  err = tde_make_dk (dks.perm_key);
  err = tde_make_dk (dks.temp_key);
  err = tde_make_dk (dks.log_key);
  if (err != NO_ERROR)
    {
      return err;
    }

  tde_generate_keyinfo_internal (&keyinfo, 0, default_mk, &dks);

  recdes.area_size = recdes.length = DB_SIZEOF (TDE_KEYINFO);
  recdes.type = REC_HOME;
  recdes.data = (char *) &keyinfo;

  /* Prepare context */
  heap_create_insert_context (&heapop_context, keyinfo_hfid, NULL, &recdes, NULL);

  /* Insert and fetch location */
  err = heap_insert_logical (thread_p, &heapop_context, NULL);
  if (err != NO_ERROR)
    {
      return err;
    }

  COPY_OID (tde_Keyinfo_oid, &heapop_context.res_oid);

  return NO_ERROR;
}

int
tde_cipher_initialize (THREAD_ENTRY *thread_p, const HFID *keyinfo_hfid)
{
  char mk_path[PATH_MAX] = {0, };
  TDE_KEYINFO keyinfo;

  tde_get_keyinfo (thread_p, &keyinfo, tde_Keyinfo_oid, keyinfo_hfid);

  assert (keyinfo.mk_index > 0);

  tde_make_keys_volume_fullname (mk_path);

  if (tde_load_mk (&keyinfo, mk_path) != NO_ERROR)
    {
      // TODO: failed to load, it is not worng behavior, how to handle it?
      return -1;
    }

  if (tde_load_dks (&keyinfo, tde_Cipher.master_key) != NO_ERROR)
    {
      // TODO: failed to load, it is wrong behavior, how to handle it?
      return -1;
    }

  tde_Cipher.temp_write_counter.store (0);

  tde_Cipher.is_loaded = true;

  return NO_ERROR;
}

int
tde_add_mk (int mk_index, const unsigned char *master_key)
{
  FILE *keyfile_fp = NULL;
  char mk_path[PATH_MAX] = {0, };
  char format_string[36] = "%d%32s";   /* [keyindex(4byte)][key(32byte)] */
  int err = NO_ERROR;
  int ret = 0;
  int searched_index = -1;
  char mk[TDE_MASTER_KEY_LENGTH] = {0,};

  keyfile_fp = fopen (mk_path, "r+");
  if (keyfile_fp == NULL)
    {
      //TODO error; ER_KEYS_NO_WRITE_ACCESS ?
      return -1;
    }

  /* TODO: search  */
  while (true)
    {
      ret = fscanf (keyfile_fp, format_string, &searched_index, mk);
      if (ret == EOF)
	{
	  break;
	}
      if (ret != 2)
	{
	  //TODO error; invalid file format
	  return -1;
	}
      else if (searched_index == -1)
	{
	  fseek (keyfile_fp, -36, SEEK_CUR); // TODO Definition으로..
	  break;
	}
    }

  /* add key */
  err = fprintf (keyfile_fp, format_string, mk_index, master_key);
  if (err < 0)
    {
      //TODO error;
      return -1 ;
    }

  fflush (keyfile_fp);
  fsync (fileno (keyfile_fp));

  fclose (keyfile_fp);

  return NO_ERROR;
}

static int
tde_get_keyinfo (THREAD_ENTRY *thread_p, TDE_KEYINFO *keyinfo, OID *keyinfo_oid, const HFID *hfid)
{
  RECDES recdes;
  HEAP_SCANCACHE scan_cache;
  SCAN_CODE scan = S_SUCCESS;

  recdes.area_size = recdes.length = DB_SIZEOF (TDE_KEYINFO);
  recdes.data = (char *) keyinfo;

  heap_scancache_quick_start_with_class_hfid (thread_p, &scan_cache, hfid);
  scan = heap_first (thread_p, hfid, NULL, keyinfo_oid, &recdes, &scan_cache, COPY);
  heap_scancache_end (thread_p, &scan_cache);

  if (scan != S_SUCCESS)
    {
      assert (false);
      return ER_FAILED;
    }
  return NO_ERROR;
}

static int
tde_update_keyinfo (THREAD_ENTRY *thread_p, const TDE_KEYINFO *keyinfo, OID *keyinfo_oid, HFID *hfid)
{
  HEAP_SCANCACHE scan_cache;
  HEAP_OPERATION_CONTEXT update_context;
  RECDES recdes;

  int error_code = NO_ERROR;

  recdes.length = recdes.area_size = sizeof (TDE_KEYINFO);
  recdes.data = (char *) keyinfo;

  /* note that we start a scan cache with NULL class_oid. That's because boot_Db_parm_oid doesn't really have a class!
   * we have to start the scan cache this way so it can cache also cache file type for heap_update_logical.
   * otherwise it will try to read it from cache using root class OID. which actually has its own heap file and its own
   * heap file type.
   */
  error_code = heap_scancache_start_modify (thread_p, &scan_cache, hfid, NULL, SINGLE_ROW_UPDATE, NULL);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      return error_code;
    }

  /* hack the class to avoid heap_scancache_check_with_hfid. */
  scan_cache.node.class_oid = *oid_Root_class_oid;
  heap_create_update_context (&update_context, hfid, keyinfo_oid, oid_Root_class_oid,
			      &recdes, &scan_cache, UPDATE_INPLACE_CURRENT_MVCCID);
  error_code = heap_update_logical (thread_p, &update_context);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
    }
  heap_scancache_end (thread_p, &scan_cache);
  return error_code;
}

static void
tde_make_keys_volume_fullname (char *keys_vol_fullname)
{
  char *mk_path = NULL;
  const char *base_name = NULL;

  mk_path = (char *) prm_get_string_value (PRM_ID_IO_KEYS_VOLUME_PATH);
  if (mk_path == NULL || mk_path[0] == '\0')
    {
      fileio_make_keys_name (keys_vol_fullname, boot_db_full_name());
    }
  else
    {
      base_name = fileio_get_base_file_name (boot_db_full_name());
      fileio_make_keys_name_given_path (keys_vol_fullname, mk_path, base_name);
    }
}

/*
 * tde_key_info (out) :
 */
static int
tde_generate_keyinfo (TDE_KEYINFO *keyinfo)
{
  int err = NO_ERROR;

  assert (keyinfo != NULL);
  assert (tde_Cipher.is_loaded);

  return tde_generate_keyinfo_internal (keyinfo, tde_Cipher.mk_index, tde_Cipher.master_key, &tde_Cipher.data_keys);
}

static int
tde_generate_keyinfo_internal (TDE_KEYINFO *keyinfo, int mk_index, const unsigned char *master_key,
			       const TDE_DATA_KEY_SET *dks)
{
  int err = NO_ERROR;

  keyinfo->mk_index = mk_index;
  tde_make_mk_hash (master_key, keyinfo->mk_hash);

  tde_encrypt_dk (dks->perm_key, TDE_DATA_KEY_TYPE_PERM, master_key, keyinfo->dk_perm);
  if (err != NO_ERROR)
    {
      return err;
    }
  tde_encrypt_dk (dks->temp_key, TDE_DATA_KEY_TYPE_TEMP, master_key, keyinfo->dk_temp);
  if (err != NO_ERROR)
    {
      return err;
    }
  tde_encrypt_dk (dks->log_key, TDE_DATA_KEY_TYPE_LOG, master_key, keyinfo->dk_log);
  if (err != NO_ERROR)
    {
      return err;
    }

  return err;
}

static int
tde_create_keys_volume (const char *mk_path)
{
  FILE *keyfile_fp = NULL;
  char format_string[64] = "%d%32s";   /* [keyindex(4byte)][key(32byte)] */
  int err = NO_ERROR;

  keyfile_fp = fopen (mk_path, "w");
  if (keyfile_fp == NULL)
    {
      //TODO error; ER_KEYS_NO_WRITE_ACCESS ?
      return -1;
    }
  fclose (keyfile_fp);

  return NO_ERROR;
}

static int
tde_load_mk (const TDE_KEYINFO *keyinfo, const char *mk_path)
{
  FILE *keyfile_fp = NULL;
  char format_string[64] = "%d%32s";   /* [keyindex(4byte)][key(32byte)] */
  int searched_idx = -1;
  unsigned char searched_key[TDE_MASTER_KEY_LENGTH] = {0,};
  bool found = false;

  assert (keyinfo->mk_index > 0);

  keyfile_fp = fopen (mk_path, "r");
  if (keyfile_fp == NULL)
    {
      //TODO error;
      return -1;
    }

  while (true)
    {
      if (fscanf (keyfile_fp, format_string, &searched_idx, searched_key) != 2)
	{
	  break;
	}
      if (searched_idx == keyinfo->mk_index)
	{
	  found = true;
	  break;
	}
    }
  fclose (keyfile_fp);
  if (!found)
    {
      // TODO error
      return -1;
    }
  /* MK has found */

  if (tde_validate_mk (searched_key, keyinfo->mk_hash) == false)
    {
      // TODO error
      return -1;
    }
  /* MK has validated */

  memcpy (tde_Cipher.master_key, searched_key, TDE_MASTER_KEY_LENGTH);

  return NO_ERROR;
}

/* Do validation by comparing boot_Db_parm->tde_mk_hash with SHA-256 */
static bool
tde_validate_mk (const unsigned char *master_key, const unsigned char *mk_hash)
{
  unsigned char hash[SHA256_DIGEST_LENGTH];

  tde_make_mk_hash (master_key, hash);

  if (memcmp (mk_hash, hash, TDE_MASTER_KEY_LENGTH) != 0)
    {
      return false;
    }
  return true;
}

/* MK MUST be loaded earlier */
static int
tde_load_dks (const TDE_KEYINFO *keyinfo, const unsigned char *master_key)
{
  unsigned char dk_nonce[TDE_DK_NONCE_LENGTH] = {0,};
  int err = NO_ERROR;

  err = tde_decrypt_dk (keyinfo->dk_perm, TDE_DATA_KEY_TYPE_PERM, master_key, tde_Cipher.data_keys.perm_key);
  if (err != NO_ERROR)
    {
      return err;
    }
  err = tde_decrypt_dk (keyinfo->dk_temp, TDE_DATA_KEY_TYPE_TEMP, master_key, tde_Cipher.data_keys.temp_key);
  if (err != NO_ERROR)
    {
      return err;
    }
  err = tde_decrypt_dk (keyinfo->dk_log, TDE_DATA_KEY_TYPE_LOG, master_key, tde_Cipher.data_keys.log_key);
  if (err != NO_ERROR)
    {
      return err;
    }

  return err;
}

static int
tde_make_mk (unsigned char *master_key)
{
  int err = NO_ERROR;

  assert (master_key != NULL);

  err = RAND_bytes (master_key, TDE_MASTER_KEY_LENGTH);
  if (err != 1 )
    {
      //TODO error;
      return -1;
    }

  return err;
}

static void
tde_make_mk_hash (const unsigned char *master_key, unsigned char *mk_hash)
{
  SHA256_CTX sha_ctx;

  assert (SHA256_DIGEST_LENGTH == TDE_MASTER_KEY_LENGTH);
  assert (master_key != NULL);
  assert (mk_hash != NULL);

  SHA256_Init (&sha_ctx);
  SHA256_Update (&sha_ctx, master_key, TDE_MASTER_KEY_LENGTH);
  SHA256_Final (mk_hash, &sha_ctx);
}

static int
tde_make_dk (unsigned char *data_key)
{
  int err = NO_ERROR;

  assert (data_key != NULL);

  err = RAND_bytes (data_key, TDE_DATA_KEY_LENGTH);
  if (err != 1 )
    {
      //TODO error;
      return -1;
    }

  return err;
}

static int
tde_encrypt_dk (const unsigned char *dk_plain, TDE_DATA_KEY_TYPE dk_type, const unsigned char *master_key,
		unsigned char *dk_cipher)
{
  unsigned char dk_nonce[TDE_DK_NONCE_LENGTH] = {0,};

  tde_dk_nonce (dk_nonce, dk_type);

  return tde_encrypt_internal (dk_plain, TDE_DATA_KEY_LENGTH, TDE_DK_ALGORITHM,
			       tde_Cipher.master_key, dk_nonce, dk_cipher);
}

static int
tde_decrypt_dk (const unsigned char *dk_cipher, TDE_DATA_KEY_TYPE dk_type, const unsigned char *master_key,
		unsigned char *dk_plain)
{
  unsigned char dk_nonce[TDE_DK_NONCE_LENGTH] = {0,};

  tde_dk_nonce (dk_nonce, dk_type);

  return tde_decrypt_internal (dk_cipher, TDE_DATA_KEY_LENGTH, TDE_DK_ALGORITHM,
			       tde_Cipher.master_key, dk_nonce, dk_plain);
}

static inline void
tde_dk_nonce (unsigned char *dk_nonce, TDE_DATA_KEY_TYPE dk_type)
{
  assert (dk_nonce != NULL);

  switch (dk_type)
    {
    case TDE_DATA_KEY_TYPE_PERM:
      memset (dk_nonce, 0, TDE_DK_NONCE_LENGTH);
      break;
    case TDE_DATA_KEY_TYPE_TEMP:
      memset (dk_nonce, 1, TDE_DK_NONCE_LENGTH);
      break;
    case TDE_DATA_KEY_TYPE_LOG:
      memset (dk_nonce, 2, TDE_DK_NONCE_LENGTH);
      break;
    default:
      assert (false);
      break;
    }
}

int
tde_encrypt_data_page (FILEIO_PAGE *iopage_plain, FILEIO_PAGE *iopage_cipher, TDE_ALGORITHM tde_algo,
		       bool is_temp)
{
  int err = NO_ERROR;
  unsigned char nonce[TDE_DATA_PAGE_NONCE_LENGTH];
  const unsigned char *data_key;

  memset (nonce, 0, TDE_DATA_PAGE_NONCE_LENGTH);

  if (is_temp)
    {
      // temporary file: p_reserve_1 for nonce TODO: p_reserve_3 -> ?
      data_key = tde_Cipher.data_keys.temp_key;
      iopage_plain->prv.p_reserve_3 = tde_Cipher.temp_write_counter.fetch_add (1);
      memcpy (nonce, &iopage_plain->prv.p_reserve_3, sizeof (iopage_plain->prv.p_reserve_3));
    }
  else
    {
      // permanent file: page lsa for nonce
      data_key = tde_Cipher.data_keys.perm_key;
      memcpy (nonce, &iopage_plain->prv.lsa, sizeof (iopage_plain->prv.lsa));
    }

  memcpy (iopage_cipher, iopage_plain, IO_PAGESIZE);

  err = tde_encrypt_internal (((const unsigned char *)iopage_plain) + TDE_DATA_PAGE_ENC_OFFSET,
			      TDE_DATA_PAGE_ENC_LENGTH, tde_algo, data_key, nonce,
			      ((unsigned char *)iopage_cipher) + TDE_DATA_PAGE_ENC_OFFSET);

  return err;
}

int
tde_decrypt_data_page (const FILEIO_PAGE *iopage_cipher, FILEIO_PAGE *iopage_plain, TDE_ALGORITHM tde_algo,
		       bool is_temp)
{
  int err = NO_ERROR;
  unsigned char nonce[TDE_DATA_PAGE_NONCE_LENGTH];
  const unsigned char *data_key;

  memset (nonce, 0, TDE_DATA_PAGE_NONCE_LENGTH);

  if (is_temp)
    {
      // temporary file: p_reserve_1 for nonce TODO: p_reserve_3 -> ?
      data_key = tde_Cipher.data_keys.temp_key;
      memcpy (nonce, &iopage_cipher->prv.p_reserve_3, sizeof (iopage_cipher->prv.p_reserve_3));
    }
  else
    {
      // permanent file: page lsa for nonce
      data_key = tde_Cipher.data_keys.perm_key;
      memcpy (nonce, &iopage_cipher->prv.lsa, sizeof (iopage_cipher->prv.lsa));
    }

  memcpy (iopage_plain, iopage_cipher, IO_PAGESIZE);

  err = tde_decrypt_internal (((const unsigned char *)iopage_cipher) + TDE_DATA_PAGE_ENC_OFFSET,
			      TDE_DATA_PAGE_ENC_LENGTH, tde_algo, data_key, nonce,
			      ((unsigned char *)iopage_plain) + TDE_DATA_PAGE_ENC_OFFSET);

  return err;
}

#endif /* !CS_MODE */

int
tde_encrypt_log_page (const LOG_PAGE *logpage_plain, LOG_PAGE *logpage_cipher, TDE_ALGORITHM tde_algo)
{
  int err = NO_ERROR;
  unsigned char nonce[TDE_LOG_PAGE_NONCE_LENGTH];
  const unsigned char *data_key;

  memset (nonce, 0, TDE_LOG_PAGE_NONCE_LENGTH);

#if !defined(CS_MODE)
  data_key = tde_Cipher.data_keys.log_key;
#else
  data_key = tde_Data_keys.log_key;
#endif /* !CS_MODE */

  memcpy (nonce, &logpage_plain->hdr.logical_pageid, sizeof (logpage_plain->hdr.logical_pageid));

  memcpy (logpage_cipher, logpage_plain, LOG_PAGESIZE);

  err = tde_encrypt_internal (((const unsigned char *)logpage_plain) + TDE_LOG_PAGE_ENC_OFFSET,
			      TDE_LOG_PAGE_ENC_LENGTH, tde_algo, data_key, nonce,
			      ((unsigned char *)logpage_cipher) + TDE_LOG_PAGE_ENC_OFFSET);

  return err;
}

int
tde_decrypt_log_page (const LOG_PAGE *logpage_cipher, LOG_PAGE *logpage_plain, TDE_ALGORITHM tde_algo)
{
  int err = NO_ERROR;
  unsigned char nonce[TDE_LOG_PAGE_NONCE_LENGTH];
  const unsigned char *data_key;

  memset (nonce, 0, TDE_LOG_PAGE_NONCE_LENGTH);

#if !defined(CS_MODE)
  data_key = tde_Cipher.data_keys.log_key;
#else
  data_key = tde_Data_keys.log_key;
#endif /* !CS_MODE */

  memcpy (nonce, &logpage_cipher->hdr.logical_pageid, sizeof (logpage_cipher->hdr.logical_pageid));

  memcpy (logpage_plain, logpage_cipher, LOG_PAGESIZE);

  err = tde_decrypt_internal (((const unsigned char *)logpage_cipher) + TDE_LOG_PAGE_ENC_OFFSET,
			      TDE_LOG_PAGE_ENC_LENGTH, tde_algo, data_key, nonce,
			      ((unsigned char *)logpage_plain) + TDE_LOG_PAGE_ENC_OFFSET);

  return err;
}

static int
tde_encrypt_internal (const unsigned char *plain_buffer, int length, TDE_ALGORITHM tde_algo, const unsigned char *key,
		      const unsigned char *nonce,
		      unsigned char *cipher_buffer)
{
  EVP_CIPHER_CTX *ctx;
  const EVP_CIPHER *cipher_type;
  int len;
  int cipher_len;
  int err = NO_ERROR;

  assert (tde_algo == TDE_ALGORITHM_AES || tde_algo == TDE_ALGORITHM_ARIA);

  if (! (ctx = EVP_CIPHER_CTX_new()))
    {
      err = ER_FAILED;
      goto exit;
    }

  switch (tde_algo)
    {
    case TDE_ALGORITHM_AES:
      cipher_type = EVP_aes_256_ctr();
      break;
    case TDE_ALGORITHM_ARIA:
      cipher_type = EVP_aria_256_ctr();
      break;
    case TDE_ALGORITHM_NONE:
    default:
      assert (false);
    }

  if (1 != EVP_EncryptInit_ex (ctx, cipher_type, NULL, key, nonce))
    {
      err = ER_FAILED;
      goto cleanup;
    }

  if (1 != EVP_EncryptUpdate (ctx, cipher_buffer, &len, plain_buffer, length))
    {
      err = ER_FAILED;
      goto cleanup;
    }
  cipher_len = len;

  // Further ciphertext bytes may be written at finalizing (Partial block).

  if (1 != EVP_EncryptFinal_ex (ctx, cipher_buffer + len, &len))
    {
      err = ER_FAILED;
      goto cleanup;
    }

  cipher_len += len;

  // CTR_MODE is stream mode so that there is no need to check,
  // but check it for safe.
  assert (cipher_len == length);

cleanup:
  EVP_CIPHER_CTX_free (ctx);

exit:
  return err;
}

static int
tde_decrypt_internal (const unsigned char *cipher_buffer, int length, TDE_ALGORITHM tde_algo, const unsigned char *key,
		      const unsigned char *nonce, unsigned char *plain_buffer)
{
  EVP_CIPHER_CTX *ctx;
  const EVP_CIPHER *cipher_type;
  int len;
  int plain_len;
  int err = NO_ERROR;

  assert (tde_algo == TDE_ALGORITHM_AES || tde_algo == TDE_ALGORITHM_ARIA);

  if (! (ctx = EVP_CIPHER_CTX_new()))
    {
      err = ER_FAILED;
      goto exit;
    }

  switch (tde_algo)
    {
    case TDE_ALGORITHM_AES:
      cipher_type = EVP_aes_256_ctr();
      break;
    case TDE_ALGORITHM_ARIA:
      cipher_type = EVP_aria_256_ctr();
      break;
    case TDE_ALGORITHM_NONE:
    default:
      assert (false);
    }

  if (1 != EVP_DecryptInit_ex (ctx, cipher_type, NULL, key, nonce))
    {
      err = ER_FAILED;
      goto cleanup;
    }

  if (1 != EVP_DecryptUpdate (ctx, plain_buffer, &len, cipher_buffer, length))
    {
      err = ER_FAILED;
      goto cleanup;
    }
  plain_len = len;

  // Further plaintext bytes may be written at finalizing (Partial block).
  if (1 != EVP_DecryptFinal_ex (ctx, plain_buffer + len, &len))
    {
      err = ER_FAILED;
      goto cleanup;
    }
  plain_len += len;

  // CTR_MODE is stream mode so that there is no need to check,
  // but check it for safe.
  assert (plain_len == length);

cleanup:
  EVP_CIPHER_CTX_free (ctx);

exit:
  return err;

}
