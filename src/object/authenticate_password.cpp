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
 * authenticate_password.cpp -
 */
#include <random>

#include "authenticate_password.hpp"

#include "authenticate.h"
#include "dbtype.h"
#include "encryption.h" /* crypt_seed () */
#include "crypt_opfunc.h" /* crypt_sha_two () */
#include "object_accessor.h"
#include "schema_manager.h" /* sm_find_class */

/*
 * Password Encoding
 */

/*
 * Password encoding is a bit kludgey to support older databases where the
 * password was stored in an unencoded format.  Since we don't want
 * to invalidate existing databases unless absolutely necessary, we
 * need a way to recognize if the password in the database is encoded or not.
 *
 * The kludge is to store encoded passwords with a special prefix character
 * that could not normally be typed as part of a password.  This will
 * be the binary char \001 or Control-A.  The prefix could be used
 * in the future to identify other encoding schemes in case we find
 * a better way to store passwords.
 *
 * If the password string has this prefix character, we can assume that it
 * has been encoded, otherwise it is assumed to be an older unencoded password.
 *
 */

/*
 * encrypt_password -  Encrypts a password string using DES
 *   return: none
 *   pass(in): string to encrypt
 *   add_prefix(in): non-zero to add the prefix char
 *   dest(out): destination buffer
 */
void
encrypt_password (const char *pass, int add_prefix, char *dest)
{
  if (pass == NULL)
    {
      strcpy (dest, "");
    }
  else
    {
      crypt_seed (PASSWORD_ENCRYPTION_SEED);
      if (!add_prefix)
	{
	  crypt_encrypt_printable (pass, dest, AU_MAX_PASSWORD_BUF);
	}
      else
	{
	  crypt_encrypt_printable (pass, dest + 1, AU_MAX_PASSWORD_BUF);
	  dest[0] = ENCODE_PREFIX_DES;
	}
    }
}

/*
 * encrypt_password_sha1 -  hashing a password string using SHA1
 *   return: none
 *   pass(in): string to encrypt
 *   add_prefix(in): non-zero to add the prefix char
 *   dest(out): destination buffer
 */
void
encrypt_password_sha1 (const char *pass, int add_prefix, char *dest)
{
  if (pass == NULL)
    {
      strcpy (dest, "");
    }
  else
    {
      if (!add_prefix)
	{
	  crypt_encrypt_sha1_printable (pass, dest, AU_MAX_PASSWORD_BUF);
	}
      else
	{
	  crypt_encrypt_sha1_printable (pass, dest + 1, AU_MAX_PASSWORD_BUF);
	  dest[0] = ENCODE_PREFIX_SHA1;
	}
    }
}

/*
 * encrypt_password_sha2_512 -  hashing a password string using SHA2 512
 *   return: none
 *   pass(in): string to encrypt
 *   dest(out): destination buffer
 */
void
encrypt_password_sha2_512 (const char *pass, char *dest)
{
  int error_status = NO_ERROR;
  char *result_strp = NULL;
  int result_len = 0;

  if (pass == NULL)
    {
      strcpy (dest, "");
    }
  else
    {
      error_status = crypt_sha_two (NULL, pass, strlen (pass), 512, &result_strp, &result_len);
      if (error_status == NO_ERROR)
	{
	  assert (result_strp != NULL);
	  assert (result_len == ENCRYPT_SHA2_512_HEX_SIZE);

	  memcpy (dest + 1, result_strp, result_len);
	  dest[result_len + 1] = '\0';	/* null termination for match_password () */
	  dest[0] = ENCODE_PREFIX_SHA2_512;

	  db_private_free_and_init (NULL, result_strp);
	}
      else
	{
	  strcpy (dest, "");
	}
    }
}

static void
encrypt_salt_generate (char *salt, int salt_size)
{
  char master_salt[ENCRYPT_SALT_SIZE + 1];
  assert (salt != NULL && salt_size > (ENCRYPT_SALT_SIZE * 2));

  if (crypt_generate_random_bytes (master_salt, ENCRYPT_SALT_SIZE) != NO_ERROR)
    {
      // fallback to generate salt using random_device
      int i = 0;
      size_t copy_size;
      uint64_t random_val;
      thread_local std::mt19937_64 engine (std::random_device{}());

      while (i < ENCRYPT_SALT_SIZE)
	{
	  random_val = engine();
	  copy_size = (ENCRYPT_SALT_SIZE - i) < (int)sizeof (uint64_t) ? (ENCRYPT_SALT_SIZE - i) : sizeof (uint64_t);
	  memcpy (master_salt + i, &random_val, copy_size);
	  i += copy_size;
	}
    }

  str_to_hex_prealloced (master_salt, ENCRYPT_SALT_SIZE, salt, salt_size, HEX_UPPERCASE);
  assert (strlen (salt) == ENCRYPT_SALT_SIZE_HEX);
}

/*
 * encrypt_password_sha2_512_salt -  hashing a password string using SHA2 512 with salt
 *   return: none
 *   name(in): user name
 *   salt(in): salt string to encrypt
 *   pass(in): string to encrypt
 *   dest(out): destination buffer
 *
 *   Notice: If salt is null, pass is the user's raw input and a new salt is generated;
 *           otherwise, pass is the user's raw input or formatted as ENCODE_PREFIX_SHA2_512.
 */
void
encrypt_password_sha2_512_salt (const char *name, const char *salt, const char *pass, char *dest)
{
  char sha512[AU_MAX_PASSWORD_BUF + 4];
  char salt_in[ENCRYPT_SALT_SIZE_HEX + 1];
  char buf[AU_MAX_PASSWORD_BUF + 4];

  assert (name != NULL && strlen (name) > 0);

  if (pass == NULL)
    {
      strcpy (dest, "");
      return;
    }

  if (salt == NULL)
    {
      encrypt_password_sha2_512 (pass, sha512);
      encrypt_salt_generate (salt_in, sizeof (salt_in));
      salt = salt_in;
    }
  else if (IS_ENCODED_SHA2_512 (pass) && strlen (pass + 1) == ENCRYPT_SHA2_512_HEX_SIZE)
    {
      strcpy (sha512, pass);
    }
  else
    {
      encrypt_password_sha2_512 (pass, sha512);
    }

  if (sha512[0] == '\0')
    {
      strcpy (dest, "");
      return;
    }

  assert (strlen (salt) == ENCRYPT_SALT_SIZE_HEX);
  snprintf (buf, sizeof (buf), "%s%s%s", salt, sha512 + 1, name);
  encrypt_password_sha2_512 (buf, sha512);
  if (sha512[0] == '\0')
    {
      strcpy (dest, "");
      return;
    }

  dest[0] = ENCODE_PREFIX_SHA2_512_SALT; // set the prefix to SHA2_512_SALT
  dest++;
  memcpy (dest, salt, ENCRYPT_SALT_SIZE_HEX);
  strcpy (dest + ENCRYPT_SALT_SIZE_HEX, sha512 + 1);
}

/*
 * match_password -  This compares two passwords to see if they match.
 *   return: non-zero if the passwords match
 *   name(in): user name(uppercase only)
 *   user(in): user supplied password
 *   database(in): stored database password
 *
 * Note: Either the user or database password can be encrypted or unencrypted.
 *       The database password will only be unencrypted if this is a very
 *       old database.  The user password will be unencrypted if we're logging
 *       in to an active session.
 */
bool
match_password (const char *name, const char *user, const char *database)
{
  char buf1[AU_MAX_PASSWORD_BUF + 4];
  char buf2[AU_MAX_PASSWORD_BUF + 4];

  if (user == NULL || database == NULL)
    {
      return false;
    }

  /* get both passwords into an encrypted format */
  /* if database's password was encrypted with DES, then, user's password should be encrypted with DES, */
  if (IS_ENCODED_DES (database))
    {
      /* DB: DES */
      strcpy (buf2, database);
      if (IS_ENCODED_ANY (user))
	{
	  /* USER : DES */
	  strcpy (buf1, Au_user_password_des_oldstyle);
	}
      else
	{
	  /* USER : PLAINTEXT -> DES */
	  encrypt_password (user, 1, buf1);
	}
    }
  else if (IS_ENCODED_SHA1 (database))
    {
      /* DB: SHA1 */
      strcpy (buf2, database);
      if (IS_ENCODED_ANY (user))
	{
	  /* USER:SHA1 */
	  strcpy (buf1, Au_user_password_sha1);
	}
      else
	{
	  /* USER:PLAINTEXT -> SHA1 */
	  encrypt_password_sha1 (user, 1, buf1);
	}
    }
  else if (IS_ENCODED_SHA2_512 (database))
    {
      /* DB: SHA2 */
      strcpy (buf2, database);
      if (IS_ENCODED_ANY (user))
	{
	  /* USER:SHA2 */
	  strcpy (buf1, Au_user_password_sha2_512);
	}
      else
	{
	  /* USER:PLAINTEXT -> SHA2 */
	  encrypt_password_sha2_512 (user, buf1);
	}
    }
  else if (IS_ENCODED_SHA2_512_SALT (database))
    {
      /* DB: SHA2 with salt */
      char salt[ENCRYPT_SALT_SIZE_HEX + 1];

      strcpy (buf2, database);

      if (strlen (database + 1) != (ENCRYPT_SALT_SIZE_HEX + ENCRYPT_SHA2_512_HEX_SIZE))
	{
	  assert (false);
	  return false;
	}

      memcpy (salt, database + 1, ENCRYPT_SALT_SIZE_HEX);
      salt[ENCRYPT_SALT_SIZE_HEX] = '\0';

      encrypt_password_sha2_512_salt (name, salt, user, buf1);
    }
  else
    {
      /* DB:PLAINTEXT -> SHA2 */
      encrypt_password_sha2_512 (database, buf2);
      if (IS_ENCODED_ANY (user))
	{
	  /* USER : SHA2 */
	  strcpy (buf1, Au_user_password_sha2_512);
	}
      else
	{
	  /* USER : PLAINTEXT -> SHA2 */
	  encrypt_password_sha2_512 (user, buf1);
	}
    }

  return strcmp (buf1, buf2) == 0;
}

/*
 * au_set_password_internal -  Set the password string for a user.
 *                             This should be using encrypted strings.
 *   return:error code
 *   user(in):  user object
 *   password(in): new password
 *   encode(in): flag to enable encryption of the string in the database
 *   encrypt_prefix(in): If encode flag is 0, then we assume that the given password have been encrypted. So, All I have
 *                       to do is add prefix(SHA2) to given password.
 *                       If encode flag is 1, then we should encrypt password with sha2 and add prefix (SHA2) to it.
 *                       So, I don't care what encrypt_prefix value is.
 */
int
au_set_password_internal (MOP user, const char *password, int encode, char encrypt_prefix)
{
  int error = NO_ERROR;
  DB_VALUE value;
  MOP pass, pclass;
  int save, len;
  bool is_new_pass = false;
  char pbuf[AU_MAX_PASSWORD_BUF + 4];

  /* check if the current user has permission to change the password */
  AU_SAVE_AND_DISABLE (save);
  if (!ws_is_same_object (Au_user, user) && !au_is_dba_group_member (Au_user))
    {
      error = ER_AU_UPDATE_FAILURE;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
      goto end;
    }

  /* convert empty password strings to NULL passwords */
  if (password != NULL)
    {
      len = strlen (password);
      if (len == 0)
	{
	  password = NULL;
	}
      /*
       * check for large passwords, only do this
       * if the encode flag is on !
       */
      else if (len > AU_MAX_PASSWORD_CHARS && encode)
	{
	  error = ER_AU_PASSWORD_OVERFLOW;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
	  goto end;
	}
    }

  /* get the existing password object from the user */
  error = obj_get (user, "password", &value);
  if (error != NO_ERROR)
    {
      goto end;
    }

  /* create a new password object and link it to the user */
  pass = (DB_IS_NULL (&value)) ? NULL : db_get_object (&value);
  if (pass == NULL)
    {
      is_new_pass = true;
      pclass = sm_find_class (AU_PASSWORD_CLASS_NAME);
      if (pclass == NULL)
	{
	  ASSERT_ERROR_AND_SET (error);
	  goto end;
	}

      pass = obj_create (pclass);
      if (pass == NULL)
	{
	  ASSERT_ERROR_AND_SET (error);
	  goto end;
	}

      db_make_object (&value, pass);
      error = obj_set (user, "password", &value);
      if (error != NO_ERROR)
	{
	  goto end;
	}
    }

  /* encrypt the password or attach the prefix depending on the encode flag */
  if (password == NULL)
    {
      db_make_null (&value);
    }
  else if (encode)
    {
      DB_VALUE nm_value;
      error = obj_get (user, "name", &nm_value);
      if (error != NO_ERROR)
	{
	  goto end;
	}

      if (DB_IS_STRING (&nm_value) && !DB_IS_NULL (&nm_value) && db_get_string (&nm_value) != NULL)
	{
#if defined(CUBRID_ENABLE_LEGACY_PASSWORD_TEST)
#ifdef NDEBUG
#error "Notice: CUBRID_ENABLE_LEGACY_PASSWORD_TEST is enabled."
#else
#warning "Notice: CUBRID_ENABLE_LEGACY_PASSWORD_TEST is enabled."
#endif
	  const char *user_nm = db_get_string (&nm_value);
	  if (strncmp (user_nm, "##PLAIN_", strlen ("##PLAIN_")) == 0)
	    {
	      strcpy ( pbuf, password);
	    }
	  else if (strncmp (user_nm, "##DES_", strlen ("##DES_")) == 0)
	    {
	      encrypt_password (password, 1, pbuf);
	    }
	  else if (strncmp (user_nm, "##SHA1_", strlen ("##SHA1_")) == 0)
	    {
	      encrypt_password_sha1 (password, 1, pbuf);
	    }
	  else if (strncmp (user_nm, "##SHA2_", strlen ("##SHA2_")) == 0)
	    {
	      encrypt_password_sha2_512 (password, pbuf);
	    }
	  else
#endif
	    {
	      encrypt_password_sha2_512_salt (db_get_string (&nm_value), NULL, password, pbuf);
	    }
	}
      else
	{
	  assert_release (false);
	  error = ER_AU_INVALID_USER_NAME;
	  goto end;
	}

      db_value_clear (&nm_value);
      db_make_string (&value, pbuf);
    }
  else
    {
      strcpy (pbuf + 1, password);
      pbuf[0] = encrypt_prefix;
      db_make_string (&value, pbuf);
    }

  /* store the prepared password value into the password object */
  error = obj_set (pass, "password", &value);
  if (error != NO_ERROR)
    {
      goto end;
    }

  /* update timestamps of the password object */
  error = is_new_pass ? au_set_new_timestamps (pass) : au_update_timestamps (pass);
  if (error != NO_ERROR)
    {
      goto end;
    }

  /* update timestamps of the user object */
  error = au_update_timestamps (user);
  if (error != NO_ERROR)
    {
      goto end;
    }

end:
  AU_RESTORE (save);
  return error;
}

