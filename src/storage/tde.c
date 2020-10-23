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
 * tde.c -
 */

#ident "$Id$"

#include <stdlib.h>
#include <assert.h>

#include <openssl/conf.h>
#include <openssl/evp.h>
#include <openssl/err.h>
#include <openssl/sha.h>
#include <openssl/rand.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#if defined(WINDOWS)
#include <io.h>
#endif /* WINDOWS */

#if !defined(CS_MODE)
#include "heap_file.h"
#include "btree.h"
#include "system_parameter.h"
#include "boot_sr.h"
#include "file_io.h"
#endif /* !CS_MODE */

#include "error_manager.h"
#include "error_code.h"
#include "log_storage.hpp"
#include "log_volids.hpp"
#include "tde.h"

/*
 * It must correspond to TDE_ALGORITHM enum.
 * Each index of tde_Algorithm_str is the value in TDE_ALGORITHM enum.
 */
static const char *tde_Algorithm_str[] = {
  "NONE",			/* TDE_ALGORITHM_NONE */
  "AES",			/* TDE_ALGORITHM_AES */
  "ARIA"			/* TDE_ALGORITHM_ARIA */
};

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
TDE_CIPHER tde_Cipher;		/* global var for TDE Module */

static OID tde_Keyinfo_oid;	/* Location of keys */
static HFID tde_Keyinfo_hfid;

static int tde_generate_keyinfo (TDE_KEYINFO * keyinfo, int mk_index, const unsigned char *master_key,
				 const time_t created_time, const TDE_DATA_KEY_SET * dks);
static int tde_update_keyinfo (THREAD_ENTRY * thread_p, const TDE_KEYINFO * keyinfo, OID * keyinfo_oid, HFID * hfid);

static int tde_create_keys_file (const char *keyfile_fullname);
static bool tde_validate_mk (const unsigned char *master_key, const unsigned char *mk_hash);
static void tde_make_mk_hash (const unsigned char *master_key, unsigned char *mk_hash);
static int tde_load_dks (const TDE_KEYINFO * keyinfo, const unsigned char *master_key);
static int tde_create_dk (unsigned char *data_key);
static int tde_encrypt_dk (const unsigned char *dk_plain, TDE_DATA_KEY_TYPE dk_type, const unsigned char *master_key,
			   unsigned char *dk_cipher);
static int tde_decrypt_dk (const unsigned char *dk_cipher, TDE_DATA_KEY_TYPE dk_type, const unsigned char *master_key,
			   unsigned char *dk_plain);

static void tde_dk_nonce (unsigned char *dk_nonce, TDE_DATA_KEY_TYPE dk_type);

/*
 * TDE internal functions for encrpytion and decryption. All the en/decryption go through it.
 */
static int tde_encrypt_internal (const unsigned char *plain_buffer, int length, TDE_ALGORITHM tde_algo,
				 const unsigned char *key, const unsigned char *nonce, unsigned char *cipher_buffer);
static int tde_decrypt_internal (const unsigned char *cipher_buffer, int length, TDE_ALGORITHM tde_algo,
				 const unsigned char *key, const unsigned char *nonce, unsigned char *plain_buffer);

/*
 *
 */
int
tde_initialize (THREAD_ENTRY * thread_p, HFID * keyinfo_hfid)
{
  char mk_path[PATH_MAX] = { 0, };
  unsigned char default_mk[TDE_MASTER_KEY_LENGTH] = { 0, };
  int mk_index;
  int err = NO_ERROR;
  RECDES recdes;
  HEAP_OPERATION_CONTEXT heapop_context;
  TDE_DATA_KEY_SET dks;
  time_t created_time;
  int vdes = -1;
  TDE_KEYINFO keyinfo;
  char recdes_buffer[sizeof (int) + sizeof (TDE_KEYINFO)];
  int repid_and_flag_bits = 0;

  tde_make_keys_file_fullname (mk_path, boot_db_full_name (), false);
  err = tde_create_keys_file (mk_path);
  if (err == NO_ERROR)
    {
      vdes = fileio_mount (thread_p, boot_db_full_name (), mk_path, LOG_DBTDE_KEYS_VOLID, false, false);
      if (vdes == NULL_VOLDES)
	{
	  ASSERT_ERROR ();
	  err = er_errid ();
	  goto exit;
	}

      err = tde_create_mk (default_mk);
      if (err != NO_ERROR)
	{
	  goto exit;
	}

      created_time = time (NULL);
      err = tde_add_mk (vdes, default_mk, &mk_index, created_time);
      if (err != NO_ERROR)
	{
	  goto exit;
	}
    }
  else if (err == ER_BO_VOLUME_EXISTS)
    {
      vdes = fileio_mount (thread_p, boot_db_full_name (), mk_path, LOG_DBTDE_KEYS_VOLID, false, false);
      if (vdes == NULL_VOLDES)
	{
	  ASSERT_ERROR ();
	  err = er_errid ();
	  goto exit;
	}

      if (tde_validate_keys_file (vdes) == false)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TDE_INVALID_KEYS_VOLUME, 1, mk_path);
	  err = ER_TDE_INVALID_KEYS_VOLUME;
	  goto exit;
	}

      err = tde_find_first_mk (vdes, &mk_index, default_mk, &created_time);
      if (err != NO_ERROR)
	{
	  goto exit;
	}
    }
  else
    {
      goto exit;
    }

  err = tde_create_dk (dks.perm_key);
  err = tde_create_dk (dks.temp_key);
  err = tde_create_dk (dks.log_key);
  if (err != NO_ERROR)
    {
      goto exit;
    }

  err = tde_generate_keyinfo (&keyinfo, mk_index, default_mk, created_time, &dks);
  if (err != NO_ERROR)
    {
      goto exit;
    }

  /* HACK: to prevent the record from adjuestment in vacuum_rv_check_at_undo() while UNDOing */
  memcpy (recdes_buffer, &repid_and_flag_bits, sizeof (int));
  memcpy (recdes_buffer + sizeof (int), &keyinfo, sizeof (TDE_KEYINFO));

  recdes.length = recdes.area_size = sizeof (recdes_buffer);
  recdes.type = REC_HOME;
  recdes.data = (char *) recdes_buffer;

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

exit:
  fileio_dismount (thread_p, vdes);

  return err;
}

int
tde_cipher_initialize (THREAD_ENTRY * thread_p, const HFID * keyinfo_hfid, const char *mk_path_given)
{
  char mk_path_buffer[PATH_MAX] = { 0, };
  const char *mk_path = NULL;
  unsigned char master_key[TDE_MASTER_KEY_LENGTH];
  TDE_KEYINFO keyinfo;
  int err = NO_ERROR;
  int vdes = NULL_VOLDES;

  if (mk_path_given != NULL && mk_path_given[0] != '\0')
    {
      mk_path = mk_path_given;
      vdes = fileio_mount (thread_p, boot_db_full_name (), mk_path, LOG_DBTDE_KEYS_VOLID, 1, false);
      /* 
       * When restoredb, backup _keys file is given.
       * if it is fail to mount, try to use normal key file ([db]_keys) below
       */
    }

  if (mk_path == NULL || vdes == NULL_VOLDES)
    {
      tde_make_keys_file_fullname (mk_path_buffer, boot_db_full_name (), false);
      mk_path = mk_path_buffer;
      vdes = fileio_mount (thread_p, boot_db_full_name (), mk_path, LOG_DBTDE_KEYS_VOLID, 1, false);
      if (vdes == NULL_VOLDES)
	{
	  ASSERT_ERROR ();
	  return er_errid ();
	}
    }

  if (tde_validate_keys_file (vdes) == false)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TDE_INVALID_KEYS_VOLUME, 1, mk_path);
      err = ER_TDE_INVALID_KEYS_VOLUME;
      goto exit;
    }

  HFID_COPY (&tde_Keyinfo_hfid, keyinfo_hfid);

  err = tde_get_keyinfo (thread_p, &keyinfo);
  if (err != NO_ERROR)
    {
      goto exit;
    }

  assert (keyinfo.mk_index >= 0);

  err = tde_load_mk (vdes, &keyinfo, master_key);
  if (err != NO_ERROR)
    {
      goto exit;
    }

  err = tde_load_dks (&keyinfo, master_key);
  if (err != NO_ERROR)
    {
      goto exit;
    }

  tde_Cipher.temp_write_counter = 0;

  tde_Cipher.is_loaded = true;

exit:
  fileio_dismount (thread_p, vdes);

  return err;
}

static int
tde_create_keys_file (const char *keyfile_fullname)
{
  int vdes = NULL_VOLDES;
  int err = NO_ERROR;
  char magic[CUBRID_MAGIC_MAX_LENGTH] = { 0, };
#if !defined(WINDOWS)
  sigset_t new_mask, old_mask;
#endif /* !WINDOWS */

#if !defined(WINDOWS)
  off_signals (new_mask, old_mask);
#endif /* !WINDOWS */

  if (fileio_is_volume_exist (keyfile_fullname))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_BO_VOLUME_EXISTS, 1, keyfile_fullname);
      err = ER_BO_VOLUME_EXISTS;
      goto exit;
    }

  if ((vdes = fileio_open (keyfile_fullname, O_CREAT | O_RDWR, 0600)) == NULL_VOLDES)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_BO_CANNOT_CREATE_VOL, 3, keyfile_fullname, boot_db_full_name (),
	      er_get_msglog_filename ());
      err = ER_BO_CANNOT_CREATE_VOL;
      goto exit;
    }

  memcpy (magic, CUBRID_MAGIC_KEYS, sizeof (CUBRID_MAGIC_KEYS));
  write (vdes, magic, CUBRID_MAGIC_MAX_LENGTH);

exit:
  if (vdes != NULL_VOLDES)
    {
      fileio_close (vdes);
    }
#if !defined(WINDOWS)
  restore_signals (old_mask);
#endif /* !WINDOWS */

  return err;
}

bool
tde_validate_keys_file (int vdes)
{
  char magic[CUBRID_MAGIC_MAX_LENGTH] = { 0, };
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
      return false;
    }

  read (vdes, magic, CUBRID_MAGIC_MAX_LENGTH);
  if (memcmp (magic, CUBRID_MAGIC_KEYS, sizeof (CUBRID_MAGIC_MAX_LENGTH)) != 0)
    {
      return false;
    }

#if !defined(WINDOWS)
  restore_signals (old_mask);
#endif /* !WINDOWS */
  return true;
}

int
tde_copy_keys_file (THREAD_ENTRY * thread_p, const char *to_keyfile_fullname, const char *from_keyfile_fullname,
		    bool keep_to_mount, bool keep_from_mount)
{
  char buffer[4096];
  int from_vdes = -1;
  int to_vdes = -1;
  int err = NO_ERROR;
  int nread = -1;

  from_vdes = fileio_mount (thread_p, boot_db_full_name (), from_keyfile_fullname, LOG_DBTDE_KEYS_VOLID, 1, false);
  if (from_vdes == NULL_VOLDES)
    {
      ASSERT_ERROR ();
      return er_errid ();
    }

  if (tde_validate_keys_file (from_vdes) == false)
    {
      fileio_dismount (thread_p, from_vdes);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TDE_INVALID_KEYS_VOLUME, 1, from_keyfile_fullname);
      return ER_TDE_INVALID_KEYS_VOLUME;
    }

  if (!fileio_is_volume_exist (to_keyfile_fullname))
    {
      err = tde_create_keys_file (to_keyfile_fullname);
      if (err != NO_ERROR)
	{
	  fileio_dismount (thread_p, from_vdes);
	  return err;
	}
    }

  to_vdes = fileio_mount (thread_p, boot_db_full_name (), to_keyfile_fullname, LOG_DBCOPY_VOLID, 2, false);
  if (to_vdes == NULL_VOLDES)
    {
      ASSERT_ERROR ();
      err = er_errid ();
      fileio_dismount (thread_p, from_vdes);
      return err;
    }

  if (lseek (from_vdes, 0L, SEEK_SET) != 0L)
    {
      fileio_dismount (thread_p, from_vdes);
      fileio_unformat_and_rename (thread_p, to_keyfile_fullname, NULL);
      return ER_FAILED;
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
	      fileio_unformat_and_rename (thread_p, to_keyfile_fullname, NULL);
	      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IO_WRITE, 0);
	      return ER_IO_WRITE;
	    }
	}
      while (nread > 0);
    }

  if (!keep_from_mount)
    {
      fileio_dismount (thread_p, from_vdes);
    }
  if (!keep_to_mount)
    {
      fileio_dismount (thread_p, to_vdes);
    }
  return err;
}

int
tde_change_mk (THREAD_ENTRY * thread_p, const int mk_index, const unsigned char *master_key, const time_t created_time)
{
  TDE_KEYINFO keyinfo;
  TDE_DATA_KEY_SET dks;
  int err = NO_ERROR;

  if (!tde_Cipher.is_loaded)
    {
      err = ER_TDE_CIPHER_IS_NOT_LOADED;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TDE_CIPHER_IS_NOT_LOADED, 0);
      return err;
    }

  /* generate keyinfo from tde_Cipher and update heap (on Disk) */
  err = tde_generate_keyinfo (&keyinfo, mk_index, master_key, created_time, &tde_Cipher.data_keys);
  if (err != NO_ERROR)
    {
      return err;
    }

  err = tde_update_keyinfo (thread_p, &keyinfo, &tde_Keyinfo_oid, &tde_Keyinfo_hfid);
  if (err != NO_ERROR)
    {
      return err;
    }

  /* heap_flush() is mandatory. Without this, it cannot be guaranteed
   * that the master key corresponding key info in heap exists in _keys file.
   * By calling heap_flush at end of changing key, DBA can remove the previous key after it.
   */
  heap_flush (thread_p, &tde_Keyinfo_oid);

  return err;
}

int
tde_get_keyinfo (THREAD_ENTRY * thread_p, TDE_KEYINFO * keyinfo)
{
  RECDES recdes;
  HEAP_SCANCACHE scan_cache;
  SCAN_CODE scan = S_SUCCESS;
  char recdes_buffer[sizeof (int) + sizeof (TDE_KEYINFO)];
  int repid_and_flag_bits = 0;
  int error_code = NO_ERROR;

  /* HACK: to prevent the record from adjuestment in vacuum_rv_check_at_undo() while UNDOing */
  memcpy (recdes_buffer, &repid_and_flag_bits, sizeof (int));
  memcpy (recdes_buffer + sizeof (int), &keyinfo, sizeof (TDE_KEYINFO));

  recdes.length = recdes.area_size = sizeof (recdes_buffer);
  recdes.data = (char *) recdes_buffer;

  heap_scancache_quick_start_with_class_hfid (thread_p, &scan_cache, &tde_Keyinfo_hfid);
  scan = heap_first (thread_p, &tde_Keyinfo_hfid, NULL, &tde_Keyinfo_oid, &recdes, &scan_cache, COPY);
  heap_scancache_end (thread_p, &scan_cache);

  if (scan != S_SUCCESS)
    {
      assert (false);
      return ER_FAILED;
    }

  memcpy (keyinfo, recdes_buffer + sizeof (int), sizeof (TDE_KEYINFO));

  return NO_ERROR;
}

static int
tde_update_keyinfo (THREAD_ENTRY * thread_p, const TDE_KEYINFO * keyinfo, OID * keyinfo_oid, HFID * hfid)
{
  HEAP_SCANCACHE scan_cache;
  HEAP_OPERATION_CONTEXT update_context;
  RECDES recdes;
  char recdes_buffer[sizeof (int) + sizeof (TDE_KEYINFO)];
  int repid_and_flag_bits = 0;
  int error_code = NO_ERROR;

  /* HACK: to prevent the record from adjuestment in vacuum_rv_check_at_undo() while UNDOing */
  memcpy (recdes_buffer, &repid_and_flag_bits, sizeof (int));
  memcpy (recdes_buffer + sizeof (int), keyinfo, sizeof (TDE_KEYINFO));

  recdes.length = recdes.area_size = sizeof (recdes_buffer);
  recdes.data = (char *) recdes_buffer;

  /* note that we start a scan cache with NULL class_oid. That's because ketinfo_oid doesn't really have a class!
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
tde_make_keys_file_fullname (char *keys_vol_fullname, const char *db_full_name, bool ignore_parm)
{
  char *mk_path = NULL;
  const char *base_name = NULL;

  mk_path = (char *) prm_get_string_value (PRM_ID_TDE_KEYS_FILE_PATH);
  if (ignore_parm || mk_path == NULL || mk_path[0] == '\0')
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
tde_generate_keyinfo (TDE_KEYINFO * keyinfo, int mk_index, const unsigned char *master_key,
		      const time_t created_time, const TDE_DATA_KEY_SET * dks)
{
  int err = NO_ERROR;

  keyinfo->mk_index = mk_index;
  tde_make_mk_hash (master_key, keyinfo->mk_hash);

  err = tde_encrypt_dk (dks->perm_key, TDE_DATA_KEY_TYPE_PERM, master_key, keyinfo->dk_perm);
  if (err != NO_ERROR)
    {
      return err;
    }
  err = tde_encrypt_dk (dks->temp_key, TDE_DATA_KEY_TYPE_TEMP, master_key, keyinfo->dk_temp);
  if (err != NO_ERROR)
    {
      return err;
    }
  err = tde_encrypt_dk (dks->log_key, TDE_DATA_KEY_TYPE_LOG, master_key, keyinfo->dk_log);
  if (err != NO_ERROR)
    {
      return err;
    }

  keyinfo->created_time = created_time;
  keyinfo->set_time = time (NULL);
  return err;
}

int
tde_load_mk (int vdes, const TDE_KEYINFO * keyinfo, unsigned char *master_key)
{
  int location;
  TDE_MK_FILE_ITEM item;
  int err = NO_ERROR;
#if !defined(WINDOWS)
  sigset_t new_mask, old_mask;
#endif /* !WINDOWS */

#if !defined(WINDOWS)
  off_signals (new_mask, old_mask);
#endif /* !WINDOWS */

  assert (keyinfo->mk_index >= 0);

  location = TDE_MK_FILE_ITEM_OFFSET (keyinfo->mk_index);

  if (lseek (vdes, location, SEEK_SET) != location)
    {
      err = ER_TDE_MASTER_KEY_NOT_FOUND;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TDE_MASTER_KEY_NOT_FOUND, 1, keyinfo->mk_index);
      goto exit;
    }

  if (read (vdes, &item, TDE_MK_FILE_ITEM_SIZE) != TDE_MK_FILE_ITEM_SIZE)
    {
      err = ER_TDE_MASTER_KEY_NOT_FOUND;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TDE_MASTER_KEY_NOT_FOUND, 1, keyinfo->mk_index);
      goto exit;
    }

  if (item.created_time == -1)
    {
      err = ER_TDE_MASTER_KEY_NOT_FOUND;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TDE_MASTER_KEY_NOT_FOUND, 1, keyinfo->mk_index);
      goto exit;
    }

  /* MK has found */

  if (tde_validate_mk (item.master_key, keyinfo->mk_hash) == false)
    {
      err = ER_TDE_INVALID_MASTER_KEY;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TDE_INVALID_MASTER_KEY, 1, keyinfo->mk_index);
      goto exit;
    }
  /* MK has validated */

  memcpy (master_key, item.master_key, TDE_MASTER_KEY_LENGTH);

exit:
#if !defined(WINDOWS)
  restore_signals (old_mask);
#endif /* !WINDOWS */

  return err;
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
tde_load_dks (const TDE_KEYINFO * keyinfo, const unsigned char *master_key)
{
  unsigned char dk_nonce[TDE_DK_NONCE_LENGTH] = { 0, };
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
tde_create_dk (unsigned char *data_key)
{
  assert (data_key != NULL);

  if (1 != RAND_bytes (data_key, TDE_DATA_KEY_LENGTH))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TDE_KEY_CREATION_FAIL, 0);
      return ER_TDE_KEY_CREATION_FAIL;
    }

  return NO_ERROR;
}

static int
tde_encrypt_dk (const unsigned char *dk_plain, TDE_DATA_KEY_TYPE dk_type, const unsigned char *master_key,
		unsigned char *dk_cipher)
{
  unsigned char dk_nonce[TDE_DK_NONCE_LENGTH] = { 0, };

  tde_dk_nonce (dk_nonce, dk_type);

  return tde_encrypt_internal (dk_plain, TDE_DATA_KEY_LENGTH, TDE_DK_ALGORITHM, master_key, dk_nonce, dk_cipher);
}

static int
tde_decrypt_dk (const unsigned char *dk_cipher, TDE_DATA_KEY_TYPE dk_type, const unsigned char *master_key,
		unsigned char *dk_plain)
{
  unsigned char dk_nonce[TDE_DK_NONCE_LENGTH] = { 0, };

  tde_dk_nonce (dk_nonce, dk_type);

  return tde_decrypt_internal (dk_cipher, TDE_DATA_KEY_LENGTH, TDE_DK_ALGORITHM, master_key, dk_nonce, dk_plain);
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
tde_encrypt_data_page (FILEIO_PAGE * iopage_plain, FILEIO_PAGE * iopage_cipher, TDE_ALGORITHM tde_algo, bool is_temp)
{
  int err = NO_ERROR;
  unsigned char nonce[TDE_DATA_PAGE_NONCE_LENGTH];
  const unsigned char *data_key;

  if (tde_Cipher.is_loaded == false)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TDE_CIPHER_IS_NOT_LOADED, 0);
      return ER_TDE_CIPHER_IS_NOT_LOADED;
    }


  memset (nonce, 0, TDE_DATA_PAGE_NONCE_LENGTH);

  if (is_temp)
    {
      // temporary file: atomic counter for nonce
      data_key = tde_Cipher.data_keys.temp_key;
      iopage_plain->prv.tde_nonce = ATOMIC_INC_64 (&tde_Cipher.temp_write_counter, 1);
    }
  else
    {
      // permanent file: page lsa for nonce
      data_key = tde_Cipher.data_keys.perm_key;
      memcpy (&iopage_plain->prv.tde_nonce, &iopage_plain->prv.lsa, sizeof (iopage_plain->prv.lsa));
    }
  memcpy (nonce, &iopage_plain->prv.tde_nonce, sizeof (iopage_plain->prv.tde_nonce));

  memcpy (iopage_cipher, iopage_plain, IO_PAGESIZE);

  err = tde_encrypt_internal (((const unsigned char *) iopage_plain) + TDE_DATA_PAGE_ENC_OFFSET,
			      TDE_DATA_PAGE_ENC_LENGTH, tde_algo, data_key, nonce,
			      ((unsigned char *) iopage_cipher) + TDE_DATA_PAGE_ENC_OFFSET);

  return err;
}

int
tde_decrypt_data_page (const FILEIO_PAGE * iopage_cipher, FILEIO_PAGE * iopage_plain, TDE_ALGORITHM tde_algo,
		       bool is_temp)
{
  int err = NO_ERROR;
  unsigned char nonce[TDE_DATA_PAGE_NONCE_LENGTH];
  const unsigned char *data_key;

  if (tde_Cipher.is_loaded == false)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TDE_CIPHER_IS_NOT_LOADED, 0);
      return ER_TDE_CIPHER_IS_NOT_LOADED;
    }

  memset (nonce, 0, TDE_DATA_PAGE_NONCE_LENGTH);

  if (is_temp)
    {
      // temporary file: atomic counter for nonce
      data_key = tde_Cipher.data_keys.temp_key;
    }
  else
    {
      // permanent file: page lsa for nonce
      data_key = tde_Cipher.data_keys.perm_key;
    }
  memcpy (nonce, &iopage_cipher->prv.tde_nonce, sizeof (iopage_cipher->prv.tde_nonce));

  memcpy (iopage_plain, iopage_cipher, IO_PAGESIZE);

  err = tde_decrypt_internal (((const unsigned char *) iopage_cipher) + TDE_DATA_PAGE_ENC_OFFSET,
			      TDE_DATA_PAGE_ENC_LENGTH, tde_algo, data_key, nonce,
			      ((unsigned char *) iopage_plain) + TDE_DATA_PAGE_ENC_OFFSET);

  return err;
}

int
xtde_get_set_mk_info (THREAD_ENTRY * thread_p, int *mk_index, time_t * created_time, time_t * set_time)
{
  TDE_KEYINFO keyinfo;
  int err = NO_ERROR;

  if (!tde_Cipher.is_loaded)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TDE_CIPHER_IS_NOT_LOADED, 0);
      return ER_TDE_CIPHER_IS_NOT_LOADED;
    }

  err = tde_get_keyinfo (thread_p, &keyinfo);
  if (err != NO_ERROR)
    {
      return err;
    }
  *mk_index = keyinfo.mk_index;
  *created_time = keyinfo.created_time;
  *set_time = keyinfo.set_time;

  return err;
}

int
xtde_change_mk_without_flock (THREAD_ENTRY * thread_p, const int mk_index)
{
  char mk_path[PATH_MAX] = { 0, };
  TDE_KEYINFO keyinfo;
  unsigned char master_key[TDE_MASTER_KEY_LENGTH] = { 0, };
  time_t created_time;
  int vdes;
  int err = NO_ERROR;

  tde_make_keys_file_fullname (mk_path, boot_db_full_name (), false);

  vdes = fileio_mount (thread_p, boot_db_full_name (), mk_path, LOG_DBTDE_KEYS_VOLID, false, false);
  if (vdes == NULL_VOLDES)
    {
      ASSERT_ERROR ();
      return er_errid ();
    }

  err = tde_find_mk (vdes, mk_index, master_key, &created_time);
  if (err != NO_ERROR)
    {
      goto exit;
    }

  err = tde_get_keyinfo (thread_p, &keyinfo);
  if (err != NO_ERROR)
    {
      goto exit;
    }

  /* if the same key with the key set on the database */
  if (mk_index == keyinfo.mk_index && tde_validate_mk (master_key, keyinfo.mk_hash))
    {
      goto exit;
    }

  /* The previous key has to exist */
  err = tde_find_mk (vdes, keyinfo.mk_index, NULL, NULL);
  if (err != NO_ERROR)
    {
      goto exit;
    }

  err = tde_change_mk (thread_p, mk_index, master_key, created_time);
  if (err != NO_ERROR)
    {
      goto exit;
    }

exit:
  fileio_dismount (thread_p, vdes);
  return err;
}

int
tde_encrypt_log_page (const LOG_PAGE * logpage_plain, LOG_PAGE * logpage_cipher, TDE_ALGORITHM tde_algo)
{
  unsigned char nonce[TDE_LOG_PAGE_NONCE_LENGTH];
  const unsigned char *data_key;

  if (tde_Cipher.is_loaded == false)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TDE_CIPHER_IS_NOT_LOADED, 0);
      return ER_TDE_CIPHER_IS_NOT_LOADED;
    }

  memset (nonce, 0, TDE_LOG_PAGE_NONCE_LENGTH);

  data_key = tde_Cipher.data_keys.log_key;

  memcpy (nonce, &logpage_plain->hdr.logical_pageid, sizeof (logpage_plain->hdr.logical_pageid));

  memcpy (logpage_cipher, logpage_plain, LOG_PAGESIZE);

  return tde_encrypt_internal (((const unsigned char *) logpage_plain) + TDE_LOG_PAGE_ENC_OFFSET,
			       TDE_LOG_PAGE_ENC_LENGTH, tde_algo, data_key, nonce,
			       ((unsigned char *) logpage_cipher) + TDE_LOG_PAGE_ENC_OFFSET);
}

int
tde_decrypt_log_page (const LOG_PAGE * logpage_cipher, LOG_PAGE * logpage_plain, TDE_ALGORITHM tde_algo)
{
  unsigned char nonce[TDE_LOG_PAGE_NONCE_LENGTH];
  const unsigned char *data_key;

  if (tde_Cipher.is_loaded == false)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TDE_CIPHER_IS_NOT_LOADED, 0);
      return ER_TDE_CIPHER_IS_NOT_LOADED;
    }

  memset (nonce, 0, TDE_LOG_PAGE_NONCE_LENGTH);

  data_key = tde_Cipher.data_keys.log_key;

  memcpy (nonce, &logpage_cipher->hdr.logical_pageid, sizeof (logpage_cipher->hdr.logical_pageid));

  memcpy (logpage_plain, logpage_cipher, LOG_PAGESIZE);

  return tde_decrypt_internal (((const unsigned char *) logpage_cipher) + TDE_LOG_PAGE_ENC_OFFSET,
			       TDE_LOG_PAGE_ENC_LENGTH, tde_algo, data_key, nonce,
			       ((unsigned char *) logpage_plain) + TDE_LOG_PAGE_ENC_OFFSET);
}

static int
tde_encrypt_internal (const unsigned char *plain_buffer, int length, TDE_ALGORITHM tde_algo, const unsigned char *key,
		      const unsigned char *nonce, unsigned char *cipher_buffer)
{
  EVP_CIPHER_CTX *ctx;
  const EVP_CIPHER *cipher_type;
  int len;
  int cipher_len;
  int err = NO_ERROR;

  assert (tde_algo == TDE_ALGORITHM_AES || tde_algo == TDE_ALGORITHM_ARIA);

  if (!(ctx = EVP_CIPHER_CTX_new ()))
    {
      err = ER_TDE_ENCRYPTION_ERROR;
      goto exit;
    }

  switch (tde_algo)
    {
    case TDE_ALGORITHM_AES:
      cipher_type = EVP_aes_256_ctr ();
      break;
    case TDE_ALGORITHM_ARIA:
      cipher_type = EVP_aria_256_ctr ();
      break;
    case TDE_ALGORITHM_NONE:
    default:
      cipher_type = NULL;
      assert (false);
    }

  if (1 != EVP_EncryptInit_ex (ctx, cipher_type, NULL, key, nonce))
    {
      err = ER_TDE_ENCRYPTION_ERROR;
      goto cleanup;
    }

  if (1 != EVP_EncryptUpdate (ctx, cipher_buffer, &len, plain_buffer, length))
    {
      err = ER_TDE_ENCRYPTION_ERROR;
      goto cleanup;
    }
  cipher_len = len;

  // Further ciphertext bytes may be written at finalizing (Partial block).

  if (1 != EVP_EncryptFinal_ex (ctx, cipher_buffer + len, &len))
    {
      err = ER_TDE_ENCRYPTION_ERROR;
      goto cleanup;
    }

  cipher_len += len;

  // CTR_MODE is stream mode so that there is no need to check,
  // but check it for safe.
  assert (cipher_len == length);

cleanup:
  EVP_CIPHER_CTX_free (ctx);

exit:
  if (err != NO_ERROR)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TDE_ENCRYPTION_ERROR, 0);
    }
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

  if (!(ctx = EVP_CIPHER_CTX_new ()))
    {
      err = ER_TDE_DECRYPTION_ERROR;
      goto exit;
    }

  switch (tde_algo)
    {
    case TDE_ALGORITHM_AES:
      cipher_type = EVP_aes_256_ctr ();
      break;
    case TDE_ALGORITHM_ARIA:
      cipher_type = EVP_aria_256_ctr ();
      break;
    case TDE_ALGORITHM_NONE:
    default:
      cipher_type = NULL;
      assert (false);
    }

  if (1 != EVP_DecryptInit_ex (ctx, cipher_type, NULL, key, nonce))
    {
      err = ER_TDE_DECRYPTION_ERROR;
      goto cleanup;
    }

  if (1 != EVP_DecryptUpdate (ctx, plain_buffer, &len, cipher_buffer, length))
    {
      err = ER_TDE_DECRYPTION_ERROR;
      goto cleanup;
    }
  plain_len = len;

  // Further plaintext bytes may be written at finalizing (Partial block).
  if (1 != EVP_DecryptFinal_ex (ctx, plain_buffer + len, &len))
    {
      err = ER_TDE_DECRYPTION_ERROR;
      goto cleanup;
    }
  plain_len += len;

  // CTR_MODE is stream mode so that there is no need to check,
  // but check it for safe.
  assert (plain_len == length);

cleanup:
  EVP_CIPHER_CTX_free (ctx);

exit:
  if (err != NO_ERROR)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TDE_DECRYPTION_ERROR, 0);
    }
  return err;
}

#endif /* !CS_MODE */

int
tde_create_mk (unsigned char *master_key)
{
  assert (master_key != NULL);

  if (1 != RAND_bytes (master_key, TDE_MASTER_KEY_LENGTH))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TDE_KEY_CREATION_FAIL, 0);
      return ER_TDE_KEY_CREATION_FAIL;
    }

  return NO_ERROR;
}

void
tde_print_mk (const unsigned char *master_key)
{
  int i;
  for (i = 0; i < TDE_MASTER_KEY_LENGTH; i++)
    {
      printf ("%02x", master_key[i]);
    }
}

int
tde_add_mk (int vdes, const unsigned char *master_key, int *mk_index, time_t created_time)
{
  TDE_MK_FILE_ITEM adding_item;
  TDE_MK_FILE_ITEM reading_item;
  int location = 0;
#if !defined(WINDOWS)
  sigset_t new_mask, old_mask;
#endif /* !WINDOWS */

#if !defined(WINDOWS)
  off_signals (new_mask, old_mask);
#endif /* !WINDOWS */

  if (lseek (vdes, TDE_MK_FILE_CONTENTS_START, SEEK_SET) != TDE_MK_FILE_CONTENTS_START)
    {
#if !defined(WINDOWS)
      restore_signals (old_mask);
#endif /* !WINDOWS */
      return ER_FAILED;
    }

  adding_item.created_time = created_time;
  memcpy (adding_item.master_key, master_key, TDE_MASTER_KEY_LENGTH);

  while (true)
    {
      if (read (vdes, &reading_item, TDE_MK_FILE_ITEM_SIZE) != TDE_MK_FILE_ITEM_SIZE)
	{
	  break;		/* EOF */
	}
      if (reading_item.created_time == -1)
	{
	  /* invalid item, which means available space */
	  lseek (vdes, -TDE_MK_FILE_ITEM_SIZE, SEEK_CUR);
	  break;
	}
    }

  location = lseek (vdes, 0, SEEK_CUR);
  *mk_index = TDE_MK_FILE_ITEM_INDEX (location);

  /* add key */
  write (vdes, &adding_item, TDE_MK_FILE_ITEM_SIZE);

  fsync (vdes);

#if !defined(WINDOWS)
  restore_signals (old_mask);
#endif /* !WINDOWS */

  return NO_ERROR;
}

int
tde_find_mk (int vdes, int mk_index, unsigned char *master_key, time_t * created_time)
{
  bool found;
  int location;
  TDE_MK_FILE_ITEM item;

#if !defined(WINDOWS)
  sigset_t new_mask, old_mask;
#endif /* !WINDOWS */

#if !defined(WINDOWS)
  off_signals (new_mask, old_mask);
#endif /* !WINDOWS */

  location = TDE_MK_FILE_ITEM_OFFSET (mk_index);

  if (lseek (vdes, location, SEEK_SET) != location)
    {
      found = false;
      goto exit;		/* not found */
    }

  if (read (vdes, &item, TDE_MK_FILE_ITEM_SIZE) != TDE_MK_FILE_ITEM_SIZE)
    {
      found = false;
      goto exit;
    }

  if (item.created_time == -1)
    {
      found = false;
      goto exit;
    }

  if (master_key != NULL)
    {
      memcpy (master_key, item.master_key, TDE_MASTER_KEY_LENGTH);
    }
  if (created_time != NULL)
    {
      *created_time = item.created_time;
    }

  found = true;

exit:
#if !defined(WINDOWS)
  restore_signals (old_mask);
#endif /* !WINDOWS */

  if (!found)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TDE_MASTER_KEY_NOT_FOUND, 1, mk_index);
      return ER_TDE_MASTER_KEY_NOT_FOUND;
    }

  return NO_ERROR;
}

int
tde_find_first_mk (int vdes, int *mk_index, unsigned char *master_key, time_t * created_time)
{
  TDE_MK_FILE_ITEM item;
  bool found = false;
  int location;
  int index = 0;

#if !defined(WINDOWS)
  sigset_t new_mask, old_mask;
#endif /* !WINDOWS */

#if !defined(WINDOWS)
  off_signals (new_mask, old_mask);
#endif /* !WINDOWS */

  location = TDE_MK_FILE_ITEM_OFFSET (0);

  if (lseek (vdes, location, SEEK_SET) != location)
    {
      found = false;
      goto exit;		/* not found */
    }

  while (read (vdes, &item, TDE_MK_FILE_ITEM_SIZE) == TDE_MK_FILE_ITEM_SIZE)
    {
      if (item.created_time != -1)
	{
	  *mk_index = index;
	  *created_time = item.created_time;
	  memcpy (master_key, item.master_key, TDE_MASTER_KEY_LENGTH);
	  found = true;
	  break;
	}
      index++;
    }

exit:
  assert (found);		/* mk file is supposed to have one key to the least */
#if !defined(WINDOWS)
  restore_signals (old_mask);
#endif /* !WINDOWS */
  return NO_ERROR;
}

int
tde_delete_mk (int vdes, int mk_index)
{
  bool found;
  int location;
  TDE_MK_FILE_ITEM item;

#if !defined(WINDOWS)
  sigset_t new_mask, old_mask;
#endif /* !WINDOWS */

#if !defined(WINDOWS)
  off_signals (new_mask, old_mask);
#endif /* !WINDOWS */

  location = TDE_MK_FILE_ITEM_OFFSET (mk_index);

  if (lseek (vdes, location, SEEK_SET) != location)
    {
      found = false;
      goto exit;		/* not found */
    }

  if (read (vdes, &item, TDE_MK_FILE_ITEM_SIZE) != TDE_MK_FILE_ITEM_SIZE)
    {
      found = false;
      goto exit;
    }

  if (item.created_time == -1)
    {
      found = false;
      goto exit;
    }

  item.created_time = -1;	/* mark it invalid */

  lseek (vdes, -TDE_MK_FILE_ITEM_SIZE, SEEK_CUR);
  write (vdes, &item, TDE_MK_FILE_ITEM_SIZE);
  fsync (vdes);
  found = true;

exit:
#if !defined(WINDOWS)
  restore_signals (old_mask);
#endif /* !WINDOWS */

  if (!found)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TDE_MASTER_KEY_NOT_FOUND, 1, mk_index);
      return ER_TDE_MASTER_KEY_NOT_FOUND;
    }

  return NO_ERROR;
}

int
tde_dump_mks (int vdes, bool print_value)
{
  TDE_MK_FILE_ITEM item;
  int cnt_valid = 0;
  int cnt_invalid = 0;
  int location;
  int i;
  char ctime_buf[CTIME_MAX] = { 0, };
#if !defined(WINDOWS)
  sigset_t new_mask, old_mask;
#endif /* !WINDOWS */

#if !defined(WINDOWS)
  off_signals (new_mask, old_mask);
#endif /* !WINDOWS */

  if ((location = lseek (vdes, TDE_MK_FILE_CONTENTS_START, SEEK_SET)) != TDE_MK_FILE_CONTENTS_START)
    {
      return ER_FAILED;
    }

  printf ("Keys Information: \n");

  while (true)
    {
      if (read (vdes, &item, TDE_MK_FILE_ITEM_SIZE) != TDE_MK_FILE_ITEM_SIZE)
	{
	  break;		/* EOF */
	}

      if (item.created_time == -1)
	{
	  cnt_invalid++;
	}
      else
	{
	  cnt_valid++;
	  ctime_r (&item.created_time, ctime_buf);
	  printf ("Key Index: %ld ", TDE_MK_FILE_ITEM_INDEX (location));
	  printf ("created on %s", ctime_buf);
	  if (print_value)
	    {
	      printf ("Key: ");
	      tde_print_mk (item.master_key);
	      printf ("\n");
	    }
	}
      location += TDE_MK_FILE_ITEM_SIZE;
    }
  printf ("\n");

exit:
  printf ("The number of keys: %d\n", cnt_valid);

#if !defined(WINDOWS)
  restore_signals (old_mask);
#endif /* !WINDOWS */
  return NO_ERROR;
}

const char *
tde_get_algorithm_name (TDE_ALGORITHM tde_algo)
{
  if (!(tde_algo == TDE_ALGORITHM_NONE || tde_algo == TDE_ALGORITHM_AES || tde_algo == TDE_ALGORITHM_ARIA))
    {
      return NULL;
    }
  return tde_Algorithm_str[tde_algo];
}
