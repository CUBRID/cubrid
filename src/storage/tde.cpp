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
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#if !defined(CS_MODE)
#include "heap_file.h"
#include "btree.h"
#include "system_parameter.h"
#include "boot_sr.h"
#include "file_io.h"
#endif /* !CS_MODE */

#include "error_code.h"
#include "log_storage.hpp"
#include "log_volids.hpp"
#include "tde.hpp"

TDE_CIPHER tde_Cipher; // global var for TDE Module

#define off_signals(new_mask, old_mask) \
  do {  \
  sigfillset (&(new_mask)); \
  sigdelset (&(new_mask), SIGINT);  \
  sigdelset (&(new_mask), SIGQUIT); \
  sigdelset (&(new_mask), SIGTERM); \
  sigdelset (&(new_mask), SIGHUP);  \
  sigdelset (&(new_mask), SIGABRT);   \
  sigprocmask (SIG_SETMASK, &(new_mask), &(old_mask));  \
  } while (0)

#define restore_signals(old_mask) sigprocmask(SIG_SETMASK, &(old_mask), NULL)



#if !defined(CS_MODE)
static OID tde_Keyinfo_oid;	/* Location of keys */
static HFID tde_Keyinfo_hfid;

static int tde_get_keyinfo (THREAD_ENTRY *thread_p, TDE_KEYINFO *keyinfo, OID *keyinfo_oid, const HFID *hfid);
static int tde_update_keyinfo (THREAD_ENTRY *thread_p, const TDE_KEYINFO *keyinfo, OID *keyinfo_oid, HFID *hfid);
static int tde_generate_keyinfo (TDE_KEYINFO *keyinfo, int mk_index, const unsigned char *master_key,
				 const TDE_DATA_KEY_SET *dks);
static int tde_create_keys_volume (const char *db_fullpath);
static int tde_load_mk (int vdes, const TDE_KEYINFO *keyinfo, unsigned char *master_key);
static bool tde_validate_mk (const unsigned char *master_key, const unsigned char *mk_hash);
static int tde_load_dks (const TDE_KEYINFO *keyinfo, const unsigned char *master_key);
static int tde_make_mk (unsigned char *master_key);
static int tde_find_mk (int vdes, const int mk_index, unsigned char *master_key);
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
  int vdes = -1;

  err = tde_create_keys_volume (boot_db_full_name());
  if (err != NO_ERROR)
    {
      return err;
    }
  tde_make_keys_volume_fullname (mk_path, boot_db_full_name());

  vdes = fileio_mount (thread_p, boot_db_full_name (), mk_path, LOG_DBTDE_KEYS_VOLID, false, false);
  if (vdes == NULL_VOLDES)
    {
      return ER_IO_MOUNT_FAIL;
    }

  err = tde_make_mk (default_mk);
  if (err != NO_ERROR)
    {
      goto exit;
    }
  tde_add_mk (vdes, TDE_DEFAULT_MK_INDEX, default_mk);

  err = tde_make_dk (dks.perm_key);
  err = tde_make_dk (dks.temp_key);
  err = tde_make_dk (dks.log_key);
  if (err != NO_ERROR)
    {
      goto exit;
    }

  err = tde_generate_keyinfo (&keyinfo, TDE_DEFAULT_MK_INDEX, default_mk, &dks);
  if (err != NO_ERROR)
    {
      goto exit;
    }

  recdes.area_size = recdes.length = DB_SIZEOF (TDE_KEYINFO);
  recdes.type = REC_HOME;
  recdes.data = (char *) &keyinfo;

  /* Prepare context */
  heap_create_insert_context (&heapop_context, keyinfo_hfid, NULL, &recdes, NULL);

  /* Insert and fetch location */
  err = heap_insert_logical (thread_p, &heapop_context, NULL);
  if (err != NO_ERROR)
    {
      goto exit;
    }

  HFID_COPY (&tde_Keyinfo_hfid, keyinfo_hfid);
  COPY_OID (&tde_Keyinfo_oid, &heapop_context.res_oid);

// TODO file unmount
exit:
  fileio_dismount (thread_p, vdes);

  return err;
}

int
tde_cipher_initialize (THREAD_ENTRY *thread_p, const HFID *keyinfo_hfid)
{
  char mk_path[PATH_MAX] = {0, };
  unsigned char master_key[TDE_MASTER_KEY_LENGTH];
  TDE_KEYINFO keyinfo;
  int err = NO_ERROR;
  int vdes = NULL_VOLDES;

  tde_make_keys_volume_fullname (mk_path, boot_db_full_name());
  vdes = fileio_mount (thread_p, boot_db_full_name (), mk_path, LOG_DBTDE_KEYS_VOLID, 2, false);
  if (vdes == NULL_VOLDES)
    {
      return ER_IO_MOUNT_FAIL;
    }

  if (tde_get_keyinfo (thread_p, &keyinfo, &tde_Keyinfo_oid, keyinfo_hfid) != NO_ERROR)
    {
      err = ER_FAILED;
      goto exit;
    }
  HFID_COPY (&tde_Keyinfo_hfid, keyinfo_hfid);

  assert (keyinfo.mk_index >= 0);

  if (tde_load_mk (vdes, &keyinfo, master_key) != NO_ERROR)
    {
      // TODO: failed to load, it is not worng behavior, how to handle it?
      err = ER_FAILED;
      goto exit;
    }

  if (tde_load_dks (&keyinfo, master_key) != NO_ERROR)
    {
      // TODO: failed to load, it is wrong behavior, how to handle it?
      err = ER_FAILED;
      goto exit;
    }

  tde_Cipher.temp_write_counter.store (0);

  tde_Cipher.is_loaded = true;

exit:
  fileio_dismount (thread_p, vdes);

  return err;
}

static int
tde_create_keys_volume (const char *db_full_name)
{
  char mk_path[PATH_MAX] = {0,};
  int vdes = -1;
  tde_make_keys_volume_fullname (mk_path, db_full_name);
  if (fileio_is_volume_exist (mk_path))
    {
      // er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_BO_VOLUME_EXISTS, 1, extinfo->name);
      return ER_BO_VOLUME_EXISTS;
    }

  if ((vdes = fileio_open (mk_path, O_CREAT | O_RDWR, 0600)) == -1)
    {
      return ER_BO_CANNOT_CREATE_VOL;
    }
  fileio_close (vdes);

  return NO_ERROR;
}

int
tde_copy_keys_volume (THREAD_ENTRY *thread_p, const char *to_db_fullname, const char *from_db_fullname)
{
  char mk_path[PATH_MAX] = {0,};
  char buffer[4096];
  int from_vdes = -1;
  int to_vdes = -1;
  int err = NO_ERROR;
  int nread = -1;

  tde_make_keys_volume_fullname (mk_path, from_db_fullname);

  if (!fileio_is_volume_exist (mk_path))
    {
      // er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_BO_VOLUME_EXISTS, 1, extinfo->name);
      return ER_BO_VOLUME_EXISTS;
    }
  from_vdes = fileio_mount (thread_p, from_db_fullname, mk_path, LOG_DBTDE_KEYS_VOLID, 2, false);
  if (from_vdes == NULL_VOLDES)
    {
      return ER_IO_MOUNT_FAIL;
    }

  tde_make_keys_volume_fullname (mk_path, to_db_fullname);

  if (!fileio_is_volume_exist (mk_path))
    {
      err = tde_create_keys_volume (to_db_fullname);
      if (err != NO_ERROR)
	{
	  fileio_dismount (thread_p, from_vdes);
	  return err;
	}
    }
  to_vdes = fileio_mount (thread_p, to_db_fullname, mk_path, LOG_DBTDE_KEYS_VOLID, 2, false);
  if (to_vdes == NULL_VOLDES)
    {
      fileio_dismount (thread_p, from_vdes);
      return ER_IO_MOUNT_FAIL;
    }

  while ((nread = read (from_vdes, buffer, 4096)) > 0)
    {
      char *out_ptr = buffer;
      ssize_t nwritten = -1;

      do
	{
	  nwritten = write (to_vdes, out_ptr, nread);

	  if (nwritten >= 0)
	    {
	      nread -= nwritten;
	      out_ptr += nwritten;
	    }
	  else if (errno != EINTR)
	    {
	      fileio_dismount (thread_p, from_vdes);
	      fileio_unformat_and_rename (thread_p, mk_path, NULL);
	      err = ER_IO_WRITE;
	      return err;
	    }
	}
      while (nread > 0);
    }

exit:
  fileio_dismount (thread_p, from_vdes);
  fileio_dismount (thread_p, to_vdes);
  return err;
}

int
tde_add_mk (int vdes, int mk_index, const unsigned char *master_key)
{
  int searched_idx = -1;
  int deleted_offset = -1;
#if !defined(WINDOWS)
  sigset_t new_mask, old_mask;
#endif /* !WINDOWS */

#if !defined(WINDOWS)
  off_signals (new_mask, old_mask);
#endif /* !WINDOWS */

  if (lseek (vdes, 0L, SEEK_SET) != 0L)
    {
#if !defined(WINDOWS)
      restore_signals (old_mask);
#endif /* !WINDOWS */
      return -1; //error
    }

  while (true)
    {
      if (read (vdes, &searched_idx, sizeof (searched_idx)) != sizeof (searched_idx))
	{
	  break; /* EOF */
	}

      if (searched_idx == mk_index)
	{
	  // TODO error: mk index already exists
	  return -1;
	}
      else if (searched_idx == -1 && deleted_offset == -1)
	{
	  /* insert mk into deleted offset */
	  deleted_offset = lseek (vdes, 0, SEEK_CUR) - sizeof (searched_idx);
	}
      lseek (vdes, TDE_MASTER_KEY_LENGTH, SEEK_CUR);
    }

  if (deleted_offset != -1)
    {
      lseek (vdes, deleted_offset, SEEK_SET);
    }

  /* add key */
  write (vdes, &mk_index, sizeof (mk_index));
  write (vdes, master_key, TDE_MASTER_KEY_LENGTH);

  fsync (vdes);

#if !defined(WINDOWS)
  restore_signals (old_mask);
#endif /* !WINDOWS */

  return NO_ERROR;
}

int
tde_delete_mk (int vdes, int mk_index)
{
  FILE *keyfile_fp = NULL;
  char mk_path[PATH_MAX] = {0, };
  int err = NO_ERROR;
  int searched_idx = -1;
  char mk[TDE_MASTER_KEY_LENGTH] = {0,};
  bool found = false;
  const int deleted_mark = -1;
#if !defined(WINDOWS)
  sigset_t new_mask, old_mask;
#endif /* !WINDOWS */

#if !defined(WINDOWS)
  off_signals (new_mask, old_mask);
#endif /* !WINDOWS */

  if (lseek (vdes, 0L, SEEK_SET) != 0L)
    {
#if !defined(WINDOWS)
      restore_signals (old_mask);
#endif /* !WINDOWS */
      return -1; //error
    }

  while (true)
    {
      if (read (vdes, &searched_idx, sizeof (searched_idx)) != sizeof (searched_idx))
	{
	  break; /* EOF */
	}
      if (searched_idx == mk_index)
	{
	  lseek (vdes, -sizeof (searched_idx), SEEK_CUR);
	  write (vdes, &deleted_mark, sizeof (deleted_mark));
	  fsync (vdes);
	  found = true;
	  break;
	}
      lseek (vdes, TDE_MASTER_KEY_LENGTH, SEEK_CUR);
    }

#if !defined(WINDOWS)
  restore_signals (old_mask);
#endif /* !WINDOWS */

  if (!found)
    {
      return ER_FAILED; // TODO: not found mk
    }

  return NO_ERROR;
}

static int
tde_find_mk (int vdes, int mk_index, unsigned char *master_key)
{
  char mk_path[PATH_MAX] = {0, };
  int err = NO_ERROR;
  int searched_idx = -1;
  bool found = false;
#if !defined(WINDOWS)
  sigset_t new_mask, old_mask;
#endif /* !WINDOWS */

#if !defined(WINDOWS)
  off_signals (new_mask, old_mask);
#endif /* !WINDOWS */
  if (lseek (vdes, 0L, SEEK_SET) != 0L)
    {
#if !defined(WINDOWS)
      restore_signals (old_mask);
#endif /* !WINDOWS */
      return -1; //error
    }

  while (true)
    {
      if (read (vdes, &searched_idx, sizeof (searched_idx)) != sizeof (searched_idx))
	{
	  break;  /* EOF */
	}
      if (searched_idx == mk_index)
	{
	  if (read (vdes, master_key, TDE_MASTER_KEY_LENGTH) != TDE_MASTER_KEY_LENGTH)
	    {
	      return -1;  /* Error, file is not consistent */
	    }
	  found = true;
	  break;
	}
      lseek (vdes, TDE_MASTER_KEY_LENGTH, SEEK_CUR);
    }

#if !defined(windows)
  restore_signals (old_mask);
#endif /* !windows */

  if (!found)
    {
      err = ER_FAILED; // TODO err not found mk
    }

  return NO_ERROR; // TODO not found error
}

int tde_change_mk (THREAD_ENTRY *thread_p, const int mk_index, const unsigned char *master_key)
{
  TDE_KEYINFO keyinfo;
  TDE_DATA_KEY_SET dks;
  int err = NO_ERROR;

  if (!tde_Cipher.is_loaded)
    {
      // TODO error, warning
      return -1;
    }

  /* generate keyinfo from tde_Cipher and update heap (on Disk) */
  err = tde_generate_keyinfo (&keyinfo, mk_index, master_key, &tde_Cipher.data_keys);
  if (err != NO_ERROR)
    {
      return err;
    }

  err = tde_update_keyinfo (thread_p, &keyinfo, &tde_Keyinfo_oid, &tde_Keyinfo_hfid);
  if (err != NO_ERROR)
    {
      return err;
    }
  heap_flush (thread_p, &tde_Keyinfo_oid);

  return err;

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

void
tde_make_keys_volume_fullname (char *keys_vol_fullname, const char *db_full_name)
{
  char *mk_path = NULL;
  const char *base_name = NULL;

  mk_path = (char *) prm_get_string_value (PRM_ID_IO_KEYS_VOLUME_PATH);
  if (mk_path == NULL || mk_path[0] == '\0')
    {
      fileio_make_keys_name (keys_vol_fullname, db_full_name);
    }
  else
    {
      base_name = fileio_get_base_file_name (db_full_name);
      fileio_make_keys_name_given_path (keys_vol_fullname, mk_path, base_name);
    }
}

static int
tde_generate_keyinfo (TDE_KEYINFO *keyinfo, int mk_index, const unsigned char *master_key,
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
tde_load_mk (int vdes, const TDE_KEYINFO *keyinfo, unsigned char *master_key)
{
  int searched_idx = -1;
  unsigned char searched_key[TDE_MASTER_KEY_LENGTH] = {0,};
  bool found = false;
#if !defined(WINDOWS)
  sigset_t new_mask, old_mask;
#endif /* !WINDOWS */

#if !defined(WINDOWS)
  off_signals (new_mask, old_mask);
#endif /* !WINDOWS */
  if (lseek (vdes, 0L, SEEK_SET) != 0L)
    {
#if !defined(WINDOWS)
      restore_signals (old_mask);
#endif /* !WINDOWS */
      return -1; //error
    }

  assert (keyinfo->mk_index >= 0);

  while (true)
    {
      if (read (vdes, &searched_idx, sizeof (searched_idx)) != sizeof (searched_idx))
	{
	  break;  /* EOF */
	}
      if (read (vdes, searched_key, TDE_MASTER_KEY_LENGTH) != TDE_MASTER_KEY_LENGTH)
	{
	  return -1; /* ERROR, the file is not consistent */
	}
      if (searched_idx == keyinfo->mk_index)
	{
	  found = true;
	  break;
	}
    }

#if !defined(windows)
  restore_signals (old_mask);
#endif /* !windows */

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

  memcpy (master_key, searched_key, TDE_MASTER_KEY_LENGTH);

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

  if (1 != RAND_bytes (master_key, TDE_MASTER_KEY_LENGTH))
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

  if (1 != RAND_bytes (data_key, TDE_DATA_KEY_LENGTH))
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
			       master_key, dk_nonce, dk_cipher);
}

static int
tde_decrypt_dk (const unsigned char *dk_cipher, TDE_DATA_KEY_TYPE dk_type, const unsigned char *master_key,
		unsigned char *dk_plain)
{
  unsigned char dk_nonce[TDE_DK_NONCE_LENGTH] = {0,};

  tde_dk_nonce (dk_nonce, dk_type);

  return tde_decrypt_internal (dk_cipher, TDE_DATA_KEY_LENGTH, TDE_DK_ALGORITHM,
			       master_key, dk_nonce, dk_plain);
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

  data_key = tde_Cipher.data_keys.log_key;

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

  data_key = tde_Cipher.data_keys.log_key;

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
