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
 * tde.c - Transparent Data Encryption (TDE) Module
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
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

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

static OID tde_Keyinfo_oid = OID_INITIALIZER;	/* Location of keys */
static HFID tde_Keyinfo_hfid = HFID_INITIALIZER;

static int tde_generate_keyinfo (TDE_KEYINFO * keyinfo, int mk_index, const unsigned char *master_key,
				 const time_t created_time, const TDE_DATA_KEY_SET * dks);
static int tde_update_keyinfo (THREAD_ENTRY * thread_p, const TDE_KEYINFO * keyinfo);

static int tde_create_keys_file (const char *keyfile_fullname);
static bool tde_validate_mk (const unsigned char *master_key, const unsigned char *mk_hash);
static void tde_make_mk_hash (const unsigned char *master_key, unsigned char *mk_hash);
static int tde_load_dks (const unsigned char *master_key, const TDE_KEYINFO * keyinfo);
static int tde_create_dk (unsigned char *data_key);
static int tde_encrypt_dk (const unsigned char *dk_plain, TDE_DATA_KEY_TYPE dk_type, const unsigned char *master_key,
			   unsigned char *dk_cipher);
static int tde_decrypt_dk (const unsigned char *dk_cipher, TDE_DATA_KEY_TYPE dk_type, const unsigned char *master_key,
			   unsigned char *dk_plain);

static void tde_dk_nonce (TDE_DATA_KEY_TYPE dk_type, unsigned char *dk_nonce);

/*
 * TDE internal functions for encrpytion and decryption. All the en/decryption go through it.
 */
static int tde_encrypt_internal (const unsigned char *plain_buffer, int length, TDE_ALGORITHM tde_algo,
				 const unsigned char *key, const unsigned char *nonce, unsigned char *cipher_buffer);
static int tde_decrypt_internal (const unsigned char *cipher_buffer, int length, TDE_ALGORITHM tde_algo,
				 const unsigned char *key, const unsigned char *nonce, unsigned char *plain_buffer);

/*
 * tde_initialize () - Initialize the tde module, which is called during initializing server.
 *
 * return             : Error code
 * thread_p (in)      : Thread entry
 * keyinfo_hfid (in)  : HFID of the special heap file which stores TDE_KEY_INFO.
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

      err = tde_create_mk (default_mk, &created_time);
      if (err != NO_ERROR)
	{
	  goto exit;
	}

      err = tde_add_mk (vdes, default_mk, created_time, &mk_index);
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
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TDE_INVALID_KEYS_FILE, 1, mk_path);
	  err = ER_TDE_INVALID_KEYS_FILE;
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
  if (err != NO_ERROR)
    {
      goto exit;
    }
  err = tde_create_dk (dks.temp_key);
  if (err != NO_ERROR)
    {
      goto exit;
    }
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

/*
 * tde_cipher_initialize () - Load the tde module, which is called during restarting server.
 *                            It prepares the tde_Cipher to encrpyt and decrypt.
 *
 * return             : Error code
 * thread_p (in)      : Thread entry
 * keyinfo_hfid (in)  : HFID of the special heap file which stores TDE_KEY_INFO.
 * mk_path_given (in) : The path of the master key file. It has higher priority than the default path or that from the system parameter.
 */
int
tde_cipher_initialize (THREAD_ENTRY * thread_p, const HFID * keyinfo_hfid, const char *mk_path_given)
{
  char mk_path_buffer[PATH_MAX] = { 0, };
  const char *mk_path = NULL;
  unsigned char master_key[TDE_MASTER_KEY_LENGTH];
  TDE_KEYINFO keyinfo;
  int err = NO_ERROR;
  int vdes = NULL_VOLDES;

  if (mk_path_given != NULL)
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
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TDE_INVALID_KEYS_FILE, 1, mk_path);
      err = ER_TDE_INVALID_KEYS_FILE;
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

  err = tde_load_dks (master_key, &keyinfo);
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

/*
 * tde_is_loaded () - Is the TDE module initialized correctly?
 *
 * return                 : true or false
 */
bool
tde_is_loaded ()
{
  return tde_Cipher.is_loaded;
}

/*
 * tde_create_keys_file () - Create TDE master key file
 *
 * return                 : Error code
 * keyfile_fullname (in)  : Full name of the key file.
 */
static int
tde_create_keys_file (const char *keyfile_fullname)
{
  int vdes = NULL_VOLDES;
  int err = NO_ERROR;
  char magic[CUBRID_MAGIC_MAX_LENGTH] = { 0, };
#if !defined(WINDOWS)
  sigset_t new_mask, old_mask;
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

  fsync (vdes);

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

/*
 * tde_validate_keys_file () - Validate the master key file
 *
 * return       : Whether or not it is valid
 * vdes (in)    : Key file descriptor
 */
bool
tde_validate_keys_file (int vdes)
{
  char magic[CUBRID_MAGIC_MAX_LENGTH] = { 0, };
  bool valid = true;
#if !defined(WINDOWS)
  sigset_t new_mask, old_mask;
  off_signals (new_mask, old_mask);
#endif /* !WINDOWS */

  if (lseek (vdes, 0L, SEEK_SET) != 0L)
    {
      valid = false;
      goto exit;
    }

  read (vdes, magic, CUBRID_MAGIC_MAX_LENGTH);
  if (memcmp (magic, CUBRID_MAGIC_KEYS, sizeof (CUBRID_MAGIC_MAX_LENGTH)) != 0)
    {
      valid = false;
      goto exit;
    }

exit:
#if !defined(WINDOWS)
  restore_signals (old_mask);
#endif /* !WINDOWS */
  return valid;
}

/*
 * tde_copy_keys_file () - Copy the master key file
 *
 * return                 : Error code
 * thread_p (in)          : Thread entry
 * dest_fullname (in)     : Copy to
 * src_fullname (in)      : Copy from
 * keep_dest_mount (in)   : Whether to keep the destination file mount after this function finishes
 * keep_src_mount (in)    : Whether to keep the source file mount after this function finishes
 */
int
tde_copy_keys_file (THREAD_ENTRY * thread_p, const char *dest_fullname, const char *src_fullname, bool keep_dest_mount,
		    bool keep_src_mount)
{
  char buffer[4096];
  int from_vdes = -1;
  int to_vdes = -1;
  int err = NO_ERROR;
  int nread = -1;

  from_vdes = fileio_mount (thread_p, boot_db_full_name (), src_fullname, LOG_DBTDE_KEYS_VOLID, 1, false);
  if (from_vdes == NULL_VOLDES)
    {
      ASSERT_ERROR ();
      return er_errid ();
    }

  if (tde_validate_keys_file (from_vdes) == false)
    {
      fileio_dismount (thread_p, from_vdes);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TDE_INVALID_KEYS_FILE, 1, src_fullname);
      return ER_TDE_INVALID_KEYS_FILE;
    }

  if (fileio_is_volume_exist (dest_fullname))
    {
      fileio_unformat_and_rename (thread_p, dest_fullname, NULL);
    }

  err = tde_create_keys_file (dest_fullname);
  if (err != NO_ERROR)
    {
      fileio_dismount (thread_p, from_vdes);
      return err;
    }

  to_vdes = fileio_mount (thread_p, boot_db_full_name (), dest_fullname, LOG_DBCOPY_VOLID, 1, false);
  if (to_vdes == NULL_VOLDES)
    {
      ASSERT_ERROR_AND_SET (err);
      fileio_dismount (thread_p, from_vdes);
      return err;
    }

  if (lseek (from_vdes, 0L, SEEK_SET) != 0L)
    {
      fileio_dismount (thread_p, from_vdes);
      fileio_unformat_and_rename (thread_p, dest_fullname, NULL);
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
	      fileio_unformat_and_rename (thread_p, dest_fullname, NULL);
	      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IO_WRITE, 0);
	      return ER_IO_WRITE;
	    }
	}
      while (nread > 0);
    }

  if (!keep_src_mount)
    {
      fileio_dismount (thread_p, from_vdes);
    }
  if (!keep_dest_mount)
    {
      fileio_dismount (thread_p, to_vdes);
    }
  return err;
}

/*
 * tde_make_keys_file_fullname () - Make the full name of TDE master key file. I can be set by a system parameter
 *
 * keys_vol_fullname (out)  : Full path of the key file  
 * db_full_name (in)        : Full paht of the data volume 
 * ignore_parm (in)         : Wwhether to ignore the system parameter or not
 */
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

/*
 * tde_generate_keyinfo () - Generate a keyinfo with a master key and data keys information
 *
 * return             : Error code
 * keyinfo (out)      : keyinfo 
 * mk_index (in)      : Index in the master key file
 * master_key (in)    : Master key
 * created_time (in)  : Creation time of the master key
 * dks (in)           : Data key set
 */
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

/*
 * tde_get_keyinfo () - Get keyinfo from key info heap file 
 *
 * return             : Error code
 * thread_p (in)          : Thread entry
 * keyinfo (out)      : keyinfo from the keyinfo heap
 */
int
tde_get_keyinfo (THREAD_ENTRY * thread_p, TDE_KEYINFO * keyinfo)
{
  RECDES recdes;
  HEAP_SCANCACHE scan_cache;
  SCAN_CODE scan = S_SUCCESS;
  char recdes_buffer[sizeof (int) + sizeof (TDE_KEYINFO)];
  int error_code = NO_ERROR;

  assert (!HFID_IS_NULL (&tde_Keyinfo_hfid));

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

  /* HACK: the front of the record if dummy int to prevent the record from adjuestment
   *  in vacuum_rv_check_at_undo() while UNDOing, refer to tde_insert_keyinfo() */
  memcpy (keyinfo, recdes_buffer + sizeof (int), sizeof (TDE_KEYINFO));

  return NO_ERROR;
}

/*
 * tde_get_keyinfo () - Get keyinfo from key info heap file 
 *
 * return           : Error code
 * thread_p (in)    : Thread entry
 * keyinfo (in)     : keyinfo to update that in the keyinfo heap
 */
static int
tde_update_keyinfo (THREAD_ENTRY * thread_p, const TDE_KEYINFO * keyinfo)
{
  HEAP_SCANCACHE scan_cache;
  HEAP_OPERATION_CONTEXT update_context;
  RECDES recdes;
  char recdes_buffer[sizeof (int) + sizeof (TDE_KEYINFO)];
  int repid_and_flag_bits = 0;
  int error_code = NO_ERROR;

  assert (!HFID_IS_NULL (&tde_Keyinfo_hfid));
  assert (!OID_ISNULL (&tde_Keyinfo_oid));

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
  error_code = heap_scancache_start_modify (thread_p, &scan_cache, &tde_Keyinfo_hfid, NULL, SINGLE_ROW_UPDATE, NULL);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      return error_code;
    }

  /* hack the class to avoid heap_scancache_check_with_hfid. */
  scan_cache.node.class_oid = *oid_Root_class_oid;
  heap_create_update_context (&update_context, &tde_Keyinfo_hfid, &tde_Keyinfo_oid, oid_Root_class_oid, &recdes,
			      &scan_cache, UPDATE_INPLACE_CURRENT_MVCCID);
  error_code = heap_update_logical (thread_p, &update_context);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
    }
  heap_scancache_end (thread_p, &scan_cache);
  return error_code;
}

/*
 * tde_chnage_mk () - Change the master key set on the database
 *
 * return             : Error code
 * thread_p (in)      : Thread entry
 * mk_index (in)      : The index in the master key file
 * master_key (in)    : The master key
 * created_time (in)  : The creation time of the master key
 */
int
tde_change_mk (THREAD_ENTRY * thread_p, const int mk_index, const unsigned char *master_key, const time_t created_time)
{
  TDE_KEYINFO keyinfo;
  TDE_DATA_KEY_SET dks;
  int err = NO_ERROR;

  if (!tde_is_loaded ())
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

  err = tde_update_keyinfo (thread_p, &keyinfo);
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

/*
 * tde_load_mk () - Read a master key specified by keyinfo from the master key file and validate it
 *
 * return             : Error code
 * vdes (in)          : Key file descriptor
 * keyinfo (in)       : Key info which contains the master key information to load
 * master_key (out)   : Master key
 */
int
tde_load_mk (int vdes, const TDE_KEYINFO * keyinfo, unsigned char *master_key)
{
  int err = NO_ERROR;
  unsigned char mk[TDE_MASTER_KEY_LENGTH];
  time_t created_time;

  assert (keyinfo->mk_index >= 0);

  err = tde_find_mk (vdes, keyinfo->mk_index, mk, &created_time);
  if (err != NO_ERROR)
    {
      return err;
    }

  /* MK has found */

  if (!(tde_validate_mk (mk, keyinfo->mk_hash) && created_time == keyinfo->created_time))
    {
      err = ER_TDE_INVALID_MASTER_KEY;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TDE_INVALID_MASTER_KEY, 1, keyinfo->mk_index);
      return err;
    }
  /* MK has validated */

  memcpy (master_key, mk, TDE_MASTER_KEY_LENGTH);

  return err;
}

/*
 * tde_load_dks () - Load data keys from keyinfo onto tde_Cipher
 *
 * return             : Error code
 * master_key (in)    : Master key
 * keyinfo (in)       : Keyinfo which contains encrypted data keys
 */
static int
tde_load_dks (const unsigned char *master_key, const TDE_KEYINFO * keyinfo)
{
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

/*
 * tde_validate_mk () - Validate a master key by comparing with the hash value, 
 *                      usually with the hash value stored in tde key info heap
 *
 * return             : Valid or not
 * master_key (in)    : Master key
 * mk_hash (in)       : Hash to be compared with the master key
 */
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

/*
 * tde_make_mk_hash () - Make a hash value to validate master key later
 *
 * master_key (in)    : Master key
 * mk_hash (out)      : Hash value created with the master key
 */
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

/*
 * tde_create_dk () - Create a data key
 *
 * return             : Error code
 * data_key (out)      : Data key created
 */
static int
tde_create_dk (unsigned char *data_key)
{
  assert (data_key != NULL);

  if (RAND_bytes (data_key, TDE_DATA_KEY_LENGTH) != 1)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TDE_KEY_CREATION_FAIL, 0);
      return ER_TDE_KEY_CREATION_FAIL;
    }

  return NO_ERROR;
}

/*
 * tde_encrypt_dk () - encrypt a data key to store a data key in stable storage, 
 *                     it has to be encrypted.
 *
 * return             : error code
 * dk_plain (in)      : data key to encrypt
 * dk_type (in)       : data key type
 * master_key (in)    : a master key to encrypt the data key
 * dk_cipher (out)    : encrypted data key
 */
static int
tde_encrypt_dk (const unsigned char *dk_plain, TDE_DATA_KEY_TYPE dk_type, const unsigned char *master_key,
		unsigned char *dk_cipher)
{
  unsigned char dk_nonce[TDE_DK_NONCE_LENGTH] = { 0, };

  tde_dk_nonce (dk_type, dk_nonce);

  return tde_encrypt_internal (dk_plain, TDE_DATA_KEY_LENGTH, TDE_DK_ALGORITHM, master_key, dk_nonce, dk_cipher);
}

/*
 * tde_decrypt_dk () - Decrypt a data key
 *
 * return             : Error code
 * dk_cipher (in)     : Data key to decrypt
 * dk_type (in)       : Data key type
 * master_key (in)    : A master key to encrypt the data key
 * dk_plain (out)     : Decrypted data key
 */
static int
tde_decrypt_dk (const unsigned char *dk_cipher, TDE_DATA_KEY_TYPE dk_type, const unsigned char *master_key,
		unsigned char *dk_plain)
{
  unsigned char dk_nonce[TDE_DK_NONCE_LENGTH] = { 0, };

  tde_dk_nonce (dk_type, dk_nonce);

  return tde_decrypt_internal (dk_cipher, TDE_DATA_KEY_LENGTH, TDE_DK_ALGORITHM, master_key, dk_nonce, dk_plain);
}

/*
 * tde_dk_nonce () - Get a data key nonce according to dk_type
 *
 * dk_type (in)       : Data key type
 * dk_nonce (out)     : A nonce for dk_type
 */
static inline void
tde_dk_nonce (TDE_DATA_KEY_TYPE dk_type, unsigned char *dk_nonce)
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

/*
 * tde_encrypt_data_page () - Encrypt a data page. 
 *
 * return               : Error code
 * iopage_plain (in)    : Data page to encrypt
 * tde_algo (in)        : Encryption algorithm
 * is_temp (in)         : Whether the page is for temp file
 * iopage_cipher (out)  : Encrpyted data page
 *
 * Nonce value for temporary file and permanent file are different.
 */
int
tde_encrypt_data_page (const FILEIO_PAGE * iopage_plain, TDE_ALGORITHM tde_algo, bool is_temp,
		       FILEIO_PAGE * iopage_cipher)
{
  int err = NO_ERROR;
  unsigned char nonce[TDE_DATA_PAGE_NONCE_LENGTH] = { 0, };
  const unsigned char *data_key;
  int64_t tmp_nonce;

  if (tde_is_loaded () == false)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TDE_CIPHER_IS_NOT_LOADED, 0);
      return ER_TDE_CIPHER_IS_NOT_LOADED;
    }

  if (is_temp)
    {
      // temporary file: atomic counter for nonce
      data_key = tde_Cipher.data_keys.temp_key;
      tmp_nonce = ATOMIC_INC_64 (&tde_Cipher.temp_write_counter, 1);
      memcpy (nonce, &tmp_nonce, sizeof (tmp_nonce));
    }
  else
    {
      // permanent file: page lsa as nonce
      data_key = tde_Cipher.data_keys.perm_key;
      memcpy (nonce, &iopage_plain->prv.lsa, sizeof (iopage_plain->prv.lsa));
    }

  /* copy FILEIO_PAGE_RESERVED */
  memcpy (iopage_cipher, iopage_plain, TDE_DATA_PAGE_ENC_OFFSET);
  /* copy FILEIO_PAGE_WATERMARK */
  memcpy ((char *) iopage_cipher + TDE_DATA_PAGE_ENC_OFFSET + TDE_DATA_PAGE_ENC_LENGTH,
	  (char *) iopage_plain + TDE_DATA_PAGE_ENC_OFFSET + TDE_DATA_PAGE_ENC_LENGTH, sizeof (FILEIO_PAGE_WATERMARK));

  memcpy (&iopage_cipher->prv.tde_nonce, nonce, sizeof (iopage_cipher->prv.tde_nonce));

  err = tde_encrypt_internal (((const unsigned char *) iopage_plain) + TDE_DATA_PAGE_ENC_OFFSET,
			      TDE_DATA_PAGE_ENC_LENGTH, tde_algo, data_key, nonce,
			      ((unsigned char *) iopage_cipher) + TDE_DATA_PAGE_ENC_OFFSET);

  return err;
}

/*
 * tde_decrypt_data_page () - Decrypt a data page. 
 *
 * return               : Error code
 * iopage_cipher (in)   : Data page to decrypt
 * tde_algo (in)        : Encryption algorithm
 * is_temp (in)         : Whether the page is for temp file
 * iopage_plain (out)   : Decrypted data page
 */
int
tde_decrypt_data_page (const FILEIO_PAGE * iopage_cipher, TDE_ALGORITHM tde_algo, bool is_temp,
		       FILEIO_PAGE * iopage_plain)
{
  int err = NO_ERROR;
  unsigned char nonce[TDE_DATA_PAGE_NONCE_LENGTH] = { 0, };
  const unsigned char *data_key;

  if (tde_is_loaded () == false)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TDE_CIPHER_IS_NOT_LOADED, 0);
      return ER_TDE_CIPHER_IS_NOT_LOADED;
    }

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

  /* copy FILEIO_PAGE_RESERVED */
  memcpy (iopage_plain, iopage_cipher, TDE_DATA_PAGE_ENC_OFFSET);
  /* copy FILEIO_PAGE_WATERMARK */
  memcpy ((char *) iopage_plain + TDE_DATA_PAGE_ENC_OFFSET + TDE_DATA_PAGE_ENC_LENGTH,
	  (char *) iopage_cipher + TDE_DATA_PAGE_ENC_OFFSET + TDE_DATA_PAGE_ENC_LENGTH, sizeof (FILEIO_PAGE_WATERMARK));

  memcpy (nonce, &iopage_cipher->prv.tde_nonce, sizeof (iopage_cipher->prv.tde_nonce));

  err = tde_decrypt_internal (((const unsigned char *) iopage_cipher) + TDE_DATA_PAGE_ENC_OFFSET,
			      TDE_DATA_PAGE_ENC_LENGTH, tde_algo, data_key, nonce,
			      ((unsigned char *) iopage_plain) + TDE_DATA_PAGE_ENC_OFFSET);

  return err;
}

/*
 * tde_encrypt_log_page () - Encrypt a log page. 
 *
 * return               : Error code
 * iopage_plain (in)    : Log page to encrypt
 * tde_algo (in)        : Encryption algorithm
 * iopage_cipher (out)  : Encrpyted log page
 */
int
tde_encrypt_log_page (const LOG_PAGE * logpage_plain, TDE_ALGORITHM tde_algo, LOG_PAGE * logpage_cipher)
{
  unsigned char nonce[TDE_LOG_PAGE_NONCE_LENGTH] = { 0, };
  const unsigned char *data_key;

  if (tde_is_loaded () == false)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TDE_CIPHER_IS_NOT_LOADED, 0);
      return ER_TDE_CIPHER_IS_NOT_LOADED;
    }

  data_key = tde_Cipher.data_keys.log_key;

  memcpy (nonce, &logpage_plain->hdr.logical_pageid, sizeof (logpage_plain->hdr.logical_pageid));
  memcpy (logpage_cipher, logpage_plain, TDE_LOG_PAGE_ENC_OFFSET);

  return tde_encrypt_internal (((const unsigned char *) logpage_plain) + TDE_LOG_PAGE_ENC_OFFSET,
			       TDE_LOG_PAGE_ENC_LENGTH, tde_algo, data_key, nonce,
			       ((unsigned char *) logpage_cipher) + TDE_LOG_PAGE_ENC_OFFSET);
}

/*
 * tde_decrypt_log_page () - Decrypt a log page. 
 *
 * return               : Error code
 * iopage_cipher (in)   : Log page to decrypt
 * tde_algo (in)        : Encryption algorithm
 * iopage_plain (out)   : Decrpyted log page
 */
int
tde_decrypt_log_page (const LOG_PAGE * logpage_cipher, TDE_ALGORITHM tde_algo, LOG_PAGE * logpage_plain)
{
  unsigned char nonce[TDE_LOG_PAGE_NONCE_LENGTH] = { 0, };
  const unsigned char *data_key;

  if (tde_is_loaded () == false)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TDE_CIPHER_IS_NOT_LOADED, 0);
      return ER_TDE_CIPHER_IS_NOT_LOADED;
    }

  data_key = tde_Cipher.data_keys.log_key;

  memcpy (nonce, &logpage_cipher->hdr.logical_pageid, sizeof (logpage_cipher->hdr.logical_pageid));
  memcpy (logpage_plain, logpage_cipher, TDE_LOG_PAGE_ENC_OFFSET);

  return tde_decrypt_internal (((const unsigned char *) logpage_cipher) + TDE_LOG_PAGE_ENC_OFFSET,
			       TDE_LOG_PAGE_ENC_LENGTH, tde_algo, data_key, nonce,
			       ((unsigned char *) logpage_plain) + TDE_LOG_PAGE_ENC_OFFSET);
}

/*
 * tde_encrypt_internal () - Gerneral encryption function
 *
 * return               : Error code
 * plain_buffer (in)    : Data to encrypt
 * length (in)          : The length of data
 * tde_algo (in)        : Encryption algorithm
 * key (in)             : key
 * nonce (in)           : nonce, which has to be unique in time and space
 * cipher_buffer (out)  : Encrypted data
 *
 * plain_buffer and cipher_buffer has more space than length
 */
static int
tde_encrypt_internal (const unsigned char *plain_buffer, int length, TDE_ALGORITHM tde_algo, const unsigned char *key,
		      const unsigned char *nonce, unsigned char *cipher_buffer)
{
  EVP_CIPHER_CTX *ctx;
  const EVP_CIPHER *cipher_type;
  int len;
  int cipher_len;
  int err = ER_TDE_ENCRYPTION_ERROR;

  if ((ctx = EVP_CIPHER_CTX_new ()) == NULL)
    {
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
      assert (false);
      goto cleanup;
    }

  if (EVP_EncryptInit_ex (ctx, cipher_type, NULL, key, nonce) != 1)
    {
      goto cleanup;
    }

  if (EVP_EncryptUpdate (ctx, cipher_buffer, &len, plain_buffer, length) != 1)
    {
      goto cleanup;
    }
  cipher_len = len;

  // Further ciphertext bytes may be written at finalizing (Partial block).

  if (EVP_EncryptFinal_ex (ctx, cipher_buffer + len, &len) != 1)
    {
      goto cleanup;
    }

  cipher_len += len;

  // CTR_MODE is stream mode so that there is no need to check,
  // but check it for safe.
  assert (cipher_len == length);

  err = NO_ERROR;

cleanup:
  EVP_CIPHER_CTX_free (ctx);

exit:
  if (err != NO_ERROR)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TDE_ENCRYPTION_ERROR, 0);
    }
  return err;
}

/*
 * tde_decrypt_internal () - Gerneral decryption function
 *
 * return               : Error code
 * cipher_buffer (in)   : Data to decrypt
 * length (in)          : The length of data
 * tde_algo (in)        : Encryption algorithm
 * key (in)             : key
 * nonce (in)           : nonce used during encryption
 * plain_buffer (out)   : Decrypted data
 *
 * plain_buffer and cipher_buffer has more space than length
 */
static int
tde_decrypt_internal (const unsigned char *cipher_buffer, int length, TDE_ALGORITHM tde_algo, const unsigned char *key,
		      const unsigned char *nonce, unsigned char *plain_buffer)
{
  EVP_CIPHER_CTX *ctx;
  const EVP_CIPHER *cipher_type;
  int len;
  int plain_len;
  int err = ER_TDE_DECRYPTION_ERROR;

  if ((ctx = EVP_CIPHER_CTX_new ()) == NULL)
    {
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
      assert (false);
      goto cleanup;
    }

  if (EVP_DecryptInit_ex (ctx, cipher_type, NULL, key, nonce) != 1)
    {
      goto cleanup;
    }

  if (EVP_DecryptUpdate (ctx, plain_buffer, &len, cipher_buffer, length) != 1)
    {
      goto cleanup;
    }
  plain_len = len;

  // Further plaintext bytes may be written at finalizing (Partial block).
  if (EVP_DecryptFinal_ex (ctx, plain_buffer + len, &len) != 1)
    {
      goto cleanup;
    }
  plain_len += len;

  // CTR_MODE is stream mode so that there is no need to check,
  // but check it for safe.
  assert (plain_len == length);

  err = NO_ERROR;

cleanup:
  EVP_CIPHER_CTX_free (ctx);

exit:
  if (err != NO_ERROR)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TDE_DECRYPTION_ERROR, 0);
    }
  return err;
}

/*
 * xtde_get_mk_info () - Get some information of the master key set on the database
 *
 * return             : Error code
 * thread_p (in)      : Thread entry
 * mk_index (out)     : Index of the master key in the master key file
 * created_time (out) : Creation time of the master key
 * set_time (out)     : Set time of the master key
 */
int
xtde_get_mk_info (THREAD_ENTRY * thread_p, int *mk_index, time_t * created_time, time_t * set_time)
{
  TDE_KEYINFO keyinfo;
  int err = NO_ERROR;

  if (!tde_is_loaded ())
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

/*
 * xtde_change_mk_without_flock () - Change the master key on the database.
 *                                   While changing, it mounts the key file without file lock. 
 *
 * return             : Error code
 * thread_p (in)      : Thread entry
 * mk_index (in)      : Index of the master key to set
 */
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

  /* Without file lock: It is because it must've already been locked in client-side: tde() */
  vdes = fileio_open (mk_path, O_RDONLY, 0);
  if (vdes == NULL_VOLDES)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IO_MOUNT_FAIL, 1, mk_path);
      return ER_IO_MOUNT_FAIL;
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
  fileio_close (vdes);
  return err;
}

#endif /* !CS_MODE */

/*
 * tde_create_mk () - Create a master key
 *
 * return               : Error code
 * master_key (out)     : Created master key
 * created_time (out)   : The time the key created
 */
int
tde_create_mk (unsigned char *master_key, time_t * created_time)
{
  assert (master_key != NULL);

  if (RAND_bytes (master_key, TDE_MASTER_KEY_LENGTH) != 1)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TDE_KEY_CREATION_FAIL, 0);
      return ER_TDE_KEY_CREATION_FAIL;
    }

  *created_time = time (NULL);

  return NO_ERROR;
}

/*
 * tde_print_mk () - Print a master key value
 *
 * master_key (out)   : Master key to print
 */
void
tde_print_mk (const unsigned char *master_key)
{
  int i;
  for (i = 0; i < TDE_MASTER_KEY_LENGTH; i++)
    {
      printf ("%02x", master_key[i]);
    }
}

/*
 * tde_add_mk () - Add a master key to the master key file
 *
 * return             : Error code
 * vdes (in)          : Key file descriptor
 * master_key (in)    : A master key to add
 * created_time (in)  : When the master key was created
 * mk_index (out)     : The assigend index for the master key to add
 */
int
tde_add_mk (int vdes, const unsigned char *master_key, time_t created_time, int *mk_index)
{
  TDE_MK_FILE_ITEM adding_item;
  TDE_MK_FILE_ITEM reading_item;
  int location = 0;
  int err = NO_ERROR;
  int ret;
#if !defined(WINDOWS)
  sigset_t new_mask, old_mask;
  off_signals (new_mask, old_mask);
#endif /* !WINDOWS */

  if (lseek (vdes, TDE_MK_FILE_CONTENTS_START, SEEK_SET) != TDE_MK_FILE_CONTENTS_START)
    {
      err = ER_FAILED;
      goto exit;
    }

  adding_item.created_time = created_time;
  memcpy (adding_item.master_key, master_key, TDE_MASTER_KEY_LENGTH);

  while (true)
    {
      ret = read (vdes, &reading_item, TDE_MK_FILE_ITEM_SIZE);
      if (ret < 0)
	{
	  err = ER_IO_READ;
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IO_READ, 2,
			       fileio_get_volume_label_by_fd (vdes, PEEK), -1);
	  goto exit;
	}
      else if (ret == 0)
	{
	  break;		/* EOF */
	}
      else if (ret != TDE_MK_FILE_ITEM_SIZE)
	{
	  err = ER_TDE_INVALID_KEYS_FILE;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TDE_INVALID_KEYS_FILE, 1,
		  fileio_get_volume_label_by_fd (vdes, PEEK));
	  goto exit;
	}

      if (reading_item.created_time == -1)
	{
	  /* invalid item, which means available space */
	  lseek (vdes, -TDE_MK_FILE_ITEM_SIZE, SEEK_CUR);
	  break;
	}
    }

  location = lseek (vdes, 0, SEEK_CUR);

  if (TDE_MK_FILE_ITEM_INDEX (location) >= TDE_MK_FILE_ITEM_COUNT_MAX)
    {
      err = ER_TDE_MAX_KEY_FILE;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TDE_MAX_KEY_FILE, 0);
      goto exit;
    }
  else
    {
      *mk_index = TDE_MK_FILE_ITEM_INDEX (location);
    }

  /* add key */
  write (vdes, &adding_item, TDE_MK_FILE_ITEM_SIZE);

  fsync (vdes);
exit:
#if !defined(WINDOWS)
  restore_signals (old_mask);
#endif /* !WINDOWS */

  return err;
}

/*
 * tde_find_mk () - Find a master key in the master key file
 *
 * return               : Error code
 * vdes (in)            : Key file descriptor
 * mk_index (in)        : Index of the master key to find
 * master_key (out)     : The master key to add
 * created_time (out)   : When the master key was created
 */
int
tde_find_mk (int vdes, int mk_index, unsigned char *master_key, time_t * created_time)
{
  bool found;
  int location;
  TDE_MK_FILE_ITEM item;

#if !defined(WINDOWS)
  sigset_t new_mask, old_mask;
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

/*
 * tde_find_first_mk () - Find the first master key in the master key file
 *
 * return               : Error code
 * vdes (in)            : Key file descriptor
 * mk_index (out)       : Index of the master key first met
 * master_key (out)     : The master key to be found
 * created_time (out)   : When the master key was created
 */
int
tde_find_first_mk (int vdes, int *mk_index, unsigned char *master_key, time_t * created_time)
{
  TDE_MK_FILE_ITEM item;
  int location;
  int index = 0;
  int err = NO_ERROR;
  int ret;

#if !defined(WINDOWS)
  sigset_t new_mask, old_mask;
  off_signals (new_mask, old_mask);
#endif /* !WINDOWS */

  location = TDE_MK_FILE_ITEM_OFFSET (0);

  if (lseek (vdes, location, SEEK_SET) != location)
    {
      err = ER_FAILED;
      goto exit;		/* not found */
    }

  while (true)
    {
      ret = read (vdes, &item, TDE_MK_FILE_ITEM_SIZE);
      if (ret < 0)
	{
	  err = ER_IO_READ;
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IO_READ, 2,
			       fileio_get_volume_label_by_fd (vdes, PEEK), -1);
	  goto exit;
	}
      else if (ret != TDE_MK_FILE_ITEM_SIZE)
	{
	  /* The file has been crashed, or the key is not found and EOF. */
	  err = ER_TDE_INVALID_KEYS_FILE;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TDE_INVALID_KEYS_FILE, 1,
		  fileio_get_volume_label_by_fd (vdes, PEEK));
	  goto exit;
	}

      if (item.created_time != -1)
	{
	  *mk_index = index;
	  *created_time = item.created_time;
	  memcpy (master_key, item.master_key, TDE_MASTER_KEY_LENGTH);
	  break;
	}
      index++;
    }

exit:
#if !defined(WINDOWS)
  restore_signals (old_mask);
#endif /* !WINDOWS */
  return err;
}

/*
 * tde_delete_mk () - Delete a master key in the master key file
 *
 * return               : Error code
 * vdes (in)            : Key file descriptor
 * mk_index (in)        : Index of the master key to delete
 */
int
tde_delete_mk (int vdes, int mk_index)
{
  bool found;
  int location;
  TDE_MK_FILE_ITEM item;

#if !defined(WINDOWS)
  sigset_t new_mask, old_mask;
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

/*
 * tde_find_mk () - Dump master keys in a master key file
 *
 * return               : Error code
 * vdes (in)            : Key file descriptor
 * print_value (in)     : Wheter to print key value
 */
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
	  printf ("Key Index: %lu ", TDE_MK_FILE_ITEM_INDEX (location));
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

/*
 * tde_get_algorithm_name () - Convert TDE_ALGORITHM to corresponding string
 *
 * return               : String version of tde_algo. NULL means it is not valid
 * tde_algo (in)        : Encryption algorithm
 *
 */
const char *
tde_get_algorithm_name (TDE_ALGORITHM tde_algo)
{
  switch (tde_algo)
    {
    case TDE_ALGORITHM_NONE:
      return "NONE";
    case TDE_ALGORITHM_AES:
      return "AES";
    case TDE_ALGORITHM_ARIA:
      return "ARIA";
    default:
      return NULL;
    }
}
