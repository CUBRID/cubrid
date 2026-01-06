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


/*
 * match_password -  This compares two passwords to see if they match.
 *   return: non-zero if the passwords match
 *   user(in): user supplied password
 *   database(in): stored database password
 *
 * Note: Either the user or database password can be encrypted or unencrypted.
 *       The database password will only be unencrypted if this is a very
 *       old database.  The user password will be unencrypted if we're logging
 *       in to an active session.
 */
bool
match_password (const char *user, const char *database)
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
  else
    {
      /* DB:PLAINTEXT -> SHA2 */
      encrypt_password_sha2_512 (database, buf2);
      if (IS_ENCODED_ANY (user))
	{
	  /* USER : SHA1 */
	  strcpy (buf1, Au_user_password_sha1);
	}
      else
	{
	  /* USER : PLAINTEXT -> SHA1 */
	  encrypt_password_sha1 (user, 1, buf1);
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
  char pbuf[AU_MAX_PASSWORD_BUF + 4];

  AU_DISABLE (save);
  if (!ws_is_same_object (Au_user, user) && !au_is_dba_group_member (Au_user))
    {
      error = ER_AU_UPDATE_FAILURE;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
    }
  else
    {
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
	    }
	}
      if (error == NO_ERROR)
	{
	  if ((error = obj_get (user, "password", &value)) == NO_ERROR)
	    {
	      if (DB_IS_NULL (&value))
		{
		  pass = NULL;
		}
	      else
		{
		  pass = db_get_object (&value);
		}

	      if (pass == NULL)
		{
		  pclass = sm_find_class (AU_PASSWORD_CLASS_NAME);
		  if (pclass != NULL)
		    {
		      pass = obj_create (pclass);
		      if (pass != NULL)
			{
			  db_make_object (&value, pass);
			  error = obj_set (user, "password", &value);
			}
		      else
			{
			  assert (er_errid () != NO_ERROR);
			  error = er_errid ();
			}
		    }
		  else
		    {
		      assert (er_errid () != NO_ERROR);
		      error = er_errid ();
		    }
		}

	      if (error == NO_ERROR && pass != NULL)
		{
		  if (encode && password != NULL)
		    {
		      encrypt_password_sha2_512 (password, pbuf);
		      db_make_string (&value, pbuf);
		      error = obj_set (pass, "password", &value);
		    }
		  else
		    {
		      /*
		       * always add the prefix, the unload process strips it out
		       * so the password can be read by the csql interpreter
		       */
		      if (password == NULL)
			{
			  db_make_null (&value);
			}
		      else
			{
			  strcpy (pbuf + 1, password);
			  pbuf[0] = encrypt_prefix;
			  db_make_string (&value, pbuf);
			}
		      error = obj_set (pass, "password", &value);
		    }
		}
	    }
	}
    }
  AU_ENABLE (save);
  return (error);
}

