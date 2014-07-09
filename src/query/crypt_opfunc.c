/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
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
 *	Crypt_opfunc.c
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <string.h>
#include <math.h>
#include <assert.h>
#include <errno.h>

#include "thread.h"
#include "porting.h"
#include "error_code.h"
#include "error_manager.h"
#include "memory_alloc.h"
#include "crypt_opfunc.h"

#define GCRYPT_NO_MPI_MACROS
#define GCRYPT_NO_DEPRECATED

#include "gcrypt.h"

#define GCRYPT_SECURE_MEMORY_LEN (16*1024)

#define AES128_BLOCK_LEN (128/8)
#define AES128_KEY_LEN (128/8)

#if defined(SERVER_MODE)
static pthread_mutex_t gcrypt_init_mutex = PTHREAD_MUTEX_INITIALIZER;
GCRY_THREAD_OPTION_PTHREAD_IMPL;
#endif

static int gcrypt_initialized = 0;

typedef enum
{
  CRYPT_LIB_INIT_ERR = 0,
  CRYPT_LIB_OPEN_CIPHER_ERR,
  CRYPT_LIB_SET_KEY_ERR,
  CRYPT_LIB_CRYPT_ERR,
  CRYPT_LIB_UNKNOWN_ERR
} CRYPT_LIB_ERROR;

static const char *const crypt_lib_fail_info[] = {
  "Initialization failure!",
  "Open cipher failure!",
  "Set secret key failure!",
  "Encrypt/decrypt failure!",
  "Unknown error!"
};

static int init_gcrypt (void);
static char *str_to_hex (THREAD_ENTRY * thread_p, const char *src,
			 int src_len, char **dest_p, int *dest_len_p);
static void aes_default_gen_key (const char *key, int key_len, char *dest_key,
				 int dest_key_len);

/*
 * init_gcrypt() -- Initialize libgcrypt
 *   return: Success, returns NO_ERROR.
 */
static int
init_gcrypt (void)
{
  /* if gcrypt init success, it doesn't return GPG_ERR_NO_ERROR. It's kind of weird! */
#define GCRYPT_INIT_SUCCESS gcry_error(GPG_ERR_GENERAL)

  gcry_error_t i_gcrypt_err;
  if (gcrypt_initialized == 0)
    {
#if defined(SERVER_MODE)
      pthread_mutex_lock (&gcrypt_init_mutex);
#endif
      gcry_check_version (NULL);

      /* allocate secure memory */
      gcry_control (GCRYCTL_SUSPEND_SECMEM_WARN);
      gcry_control (GCRYCTL_INIT_SECMEM, GCRYPT_SECURE_MEMORY_LEN, 0);
      gcry_control (GCRYCTL_RESUME_SECMEM_WARN);

      gcry_control (GCRYCTL_INITIALIZATION_FINISHED, 0);

      i_gcrypt_err = gcry_control (GCRYCTL_INITIALIZATION_FINISHED_P);
      if (i_gcrypt_err != GCRYPT_INIT_SUCCESS)
	{
#if defined(SERVER_MODE)
	  pthread_mutex_unlock (&gcrypt_init_mutex);
#endif
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ENCRYPTION_LIB_FAILED,
		  1, crypt_lib_fail_info[CRYPT_LIB_INIT_ERR]);
	  return ER_ENCRYPTION_LIB_FAILED;
	}
      gcrypt_initialized = (i_gcrypt_err == GCRYPT_INIT_SUCCESS) ? 1 : 0;
#if defined(SERVER_MODE)
      pthread_mutex_unlock (&gcrypt_init_mutex);
#endif
      return NO_ERROR;
    }
  return NO_ERROR;
}


/*
 * str_to_hex() - convert a string to its hexadecimal expreesion string 
 *   return: 
 *   thread_p(in):
 *   src(in):
 *   src_len(in):
 *   dest_p(out):
 *   dest_len_p(out):
 * Note:
 */
static char *
str_to_hex (THREAD_ENTRY * thread_p, const char *src, int src_len,
	    char **dest_p, int *dest_len_p)
{
  static const char hextable[] = "0123456789ABCDEF";
  int dest_len = 2 * src_len;
  int i = 0;
  unsigned char item_num = 0;
  char *dest;

  assert (src != NULL);

  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
    }

  dest = (char *) db_private_alloc (thread_p, dest_len * sizeof (char));
  if (dest == NULL)
    {
      return NULL;
    }

  while (i < src_len)
    {
      item_num = (unsigned char) src[i];
      dest[2 * i] = hextable[item_num / 16];
      dest[2 * i + 1] = hextable[item_num % 16];
      i++;
    }

  *dest_p = dest;
  *dest_len_p = dest_len;
  return dest;
}


/*
 * aes_default_gen_key() - if aes's key is not equal to 128, the function generate the 128 length key. like mysql.
 *   return: 
 *   key(in):
 *   key_len(in):
 *   dest_key(out):
 *   dest_key_len(in):
 * Note:
 */
static void
aes_default_gen_key (const char *key, int key_len, char *dest_key,
		     int dest_key_len)
{
  int i, j;

  assert (key != NULL);
  assert (dest_key != NULL);

  memset (dest_key, 0, dest_key_len);
  for (i = 0, j = 0; j < key_len; ++i, ++j)
    {
      if (i == dest_key_len)
	{
	  i = 0;
	}
      dest_key[i] = ((unsigned char) dest_key[i]) ^ ((unsigned char) key[j]);
    }
}

/*
 * crypt_aes_default_encrypt() - like mysql's aes_encrypt. Use AES-128/ECB/PKCS7 method.
 *   return:
 *   thread_p(in):
 *   src(in): source string
 *   src_len(in): the length of source string
 *   key(in): the encrypt key
 *   key_len(in): the length of the key
 *   dest_p(out): the encrypted data. The pointer has to be free by db_private_free
 *   dest_len_p(out):
 * Note:
 */
int
crypt_aes_default_encrypt (THREAD_ENTRY * thread_p, const char *src,
			   int src_len, const char *key, int key_len,
			   char **dest_p, int *dest_len_p)
{
  gcry_error_t i_gcrypt_err;
  gcry_cipher_hd_t aes_ctx;
  char new_key[AES128_KEY_LEN];
  int pad;
  int padding_src_len;
  char *padding_src = NULL;
  char *dest = NULL;
  int error_status = NO_ERROR;

  assert (src != NULL);
  assert (key != NULL);

  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
    }

  *dest_p = NULL;
  *dest_len_p = 0;

  if (init_gcrypt () != NO_ERROR)
    {
      return ER_ENCRYPTION_LIB_FAILED;
    }

  i_gcrypt_err = gcry_cipher_open (&aes_ctx, GCRY_CIPHER_AES,
				   GCRY_CIPHER_MODE_ECB, 0);
  if (i_gcrypt_err != GPG_ERR_NO_ERROR)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ENCRYPTION_LIB_FAILED, 1,
	      crypt_lib_fail_info[CRYPT_LIB_OPEN_CIPHER_ERR]);
      return ER_ENCRYPTION_LIB_FAILED;
    }

  /* PKCS7 */
  if ((src_len % AES128_BLOCK_LEN) == 0)
    {
      pad = AES128_BLOCK_LEN;
      padding_src_len = src_len + pad;
    }
  else
    {
      padding_src_len =
	ceil ((double) src_len / AES128_BLOCK_LEN) * AES128_BLOCK_LEN;
      pad = padding_src_len - src_len;
    }

  padding_src = (char *) db_private_alloc (thread_p, padding_src_len);
  if (padding_src == NULL)
    {
      error_status = ER_OUT_OF_VIRTUAL_MEMORY;
      goto exit_and_free;
    }
  memcpy (padding_src, src, src_len);
  memset (padding_src + src_len, pad, pad);

  aes_default_gen_key (key, key_len, new_key, AES128_KEY_LEN);
  i_gcrypt_err = gcry_cipher_setkey (aes_ctx, new_key, AES128_KEY_LEN);
  if (i_gcrypt_err != GPG_ERR_NO_ERROR)
    {
      error_status = ER_ENCRYPTION_LIB_FAILED;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ENCRYPTION_LIB_FAILED, 1,
	      crypt_lib_fail_info[CRYPT_LIB_SET_KEY_ERR]);
      goto exit_and_free;
    }

  dest = (char *) db_private_alloc (thread_p, padding_src_len);
  if (dest == NULL)
    {
      error_status = ER_OUT_OF_VIRTUAL_MEMORY;
      goto exit_and_free;
    }

  i_gcrypt_err =
    gcry_cipher_encrypt (aes_ctx, dest, padding_src_len, padding_src,
			 padding_src_len);
  if (i_gcrypt_err != GPG_ERR_NO_ERROR)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ENCRYPTION_LIB_FAILED, 1,
	      crypt_lib_fail_info[CRYPT_LIB_CRYPT_ERR]);
      error_status = ER_ENCRYPTION_LIB_FAILED;
      goto exit_and_free;
    }

  *dest_len_p = padding_src_len;
  *dest_p = dest;

exit_and_free:
  if (padding_src != NULL)
    {
      db_private_free_and_init (thread_p, padding_src);
    }
  if ((dest != NULL) && (error_status != NO_ERROR))
    {
      db_private_free_and_init (thread_p, dest);
    }
  gcry_cipher_close (aes_ctx);
  return error_status;
}

/*
 * crypt_aes_default_decrypt() - like mysql's aes_decrypt. Use AES-128/ECB/PKCS7 method.
 *   return:
 *   thread_p(in):
 *   src(in): source string
 *   src_len(in): the length of source string
 *   key(in): the encrypt key
 *   key_len(in): the length of the key
 *   dest_p(out): the encrypted data. The pointer has to be free by db_private_free
 *   dest_len_p(out):
 * Note:
 */
int
crypt_aes_default_decrypt (THREAD_ENTRY * thread_p, const char *src,
			   int src_len, const char *key, int key_len,
			   char **dest_p, int *dest_len_p)
{
  gcry_error_t i_gcrypt_err;
  gcry_cipher_hd_t aes_ctx;
  char *dest = NULL;
  int dest_len = 0;
  char new_key[AES128_KEY_LEN];
  int pad, pad_len;
  int i;
  int error_status = NO_ERROR;

  assert (src != NULL);
  assert (key != NULL);

  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
    }

  *dest_p = NULL;
  *dest_len_p = 0;

  /* src is not a string encrypted by aes_default_encrypt, return NULL */
  if (src_len % AES128_BLOCK_LEN)
    {
      return NO_ERROR;
    }

  if (init_gcrypt () != NO_ERROR)
    {
      return ER_ENCRYPTION_LIB_FAILED;
    }

  i_gcrypt_err = gcry_cipher_open (&aes_ctx, GCRY_CIPHER_AES,
				   GCRY_CIPHER_MODE_ECB, 0);
  if (i_gcrypt_err != GPG_ERR_NO_ERROR)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ENCRYPTION_LIB_FAILED, 1,
	      crypt_lib_fail_info[CRYPT_LIB_OPEN_CIPHER_ERR]);
      return ER_ENCRYPTION_LIB_FAILED;
    }

  dest = (char *) db_private_alloc (thread_p, src_len * sizeof (char));
  if (dest == NULL)
    {
      error_status = ER_OUT_OF_VIRTUAL_MEMORY;
      goto error_and_free;
    }

  aes_default_gen_key (key, key_len, new_key, AES128_KEY_LEN);
  i_gcrypt_err = gcry_cipher_setkey (aes_ctx, new_key, AES128_KEY_LEN);
  if (i_gcrypt_err != GPG_ERR_NO_ERROR)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ENCRYPTION_LIB_FAILED, 1,
	      crypt_lib_fail_info[CRYPT_LIB_SET_KEY_ERR]);
      error_status = ER_ENCRYPTION_LIB_FAILED;
      goto error_and_free;
    }

  i_gcrypt_err = gcry_cipher_decrypt (aes_ctx, dest, src_len, src, src_len);
  if (i_gcrypt_err != GPG_ERR_NO_ERROR)
    {
      error_status = ER_ENCRYPTION_LIB_FAILED;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ENCRYPTION_LIB_FAILED, 1,
	      crypt_lib_fail_info[CRYPT_LIB_CRYPT_ERR]);
      goto error_and_free;
    }

  /* PKCS7 */
  if (src_len != 0)
    {
      pad = dest[src_len - 1];
      if (pad > AES128_BLOCK_LEN)
	{
	  /* src is not a string encrypted by aes_default_encrypt, return NULL */
	  if (dest != NULL)
	    {
	      db_private_free_and_init (thread_p, dest);
	    }
	  goto error_and_free;
	}
      i = src_len - 2;
      pad_len = 1;
      while ((i >= 0) && (dest[i] == pad))
	{
	  pad_len++;
	  i--;
	}
      if ((pad_len >= pad))
	{
	  pad_len = pad;
	  dest_len = src_len - pad_len;
	}
      else
	{
	  /* src is not a string encrypted by aes_default_encrypt, return NULL */
	  if (dest != NULL)
	    {
	      db_private_free_and_init (thread_p, dest);
	    }
	  goto error_and_free;
	}
    }

  *dest_p = dest;
  *dest_len_p = dest_len;

error_and_free:
  if ((dest != NULL) && (error_status != NO_ERROR))
    {
      db_private_free_and_init (thread_p, dest);
    }
  gcry_cipher_close (aes_ctx);
  return error_status;
}

/*
 * crypt_sha_one() -
 *   return:
 *   thread_p(in):
 *   src(in): 
 *   src_len(in): 
 *   dest_len_p(out):
 * Note:
 */
int
crypt_sha_one (THREAD_ENTRY * thread_p, const char *src, int src_len,
	       char **dest_p, int *dest_len_p)
{
  int hash_length;
  char *dest = NULL;
  char *dest_hex = NULL;
  int dest_len;
  int dest_hex_len;
  int error_status = NO_ERROR;

  assert (src != NULL);

  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
    }

  *dest_p = NULL;

  if (init_gcrypt () != NO_ERROR)
    {
      return ER_ENCRYPTION_LIB_FAILED;
    }

  hash_length = gcry_md_get_algo_dlen (GCRY_MD_SHA1);
  dest = (char *) db_private_alloc (thread_p, hash_length);
  if (dest == NULL)
    {
      error_status = ER_OUT_OF_VIRTUAL_MEMORY;
      goto exit_and_free;
    }

  dest_len = hash_length;
  gcry_md_hash_buffer (GCRY_MD_SHA1, dest, src, src_len);
  dest_hex = str_to_hex (thread_p, dest, dest_len, &dest_hex, &dest_hex_len);
  if (dest_hex == NULL)
    {
      error_status = ER_OUT_OF_VIRTUAL_MEMORY;
      goto exit_and_free;
    }

  *dest_p = dest_hex;
  *dest_len_p = dest_hex_len;

exit_and_free:
  if (dest != NULL)
    {
      db_private_free_and_init (thread_p, dest);
    }
  return error_status;
}


/*
 * crypt_sha_two() -
 *   return:
 *   thread_p(in):
 *   src(in): 
 *   src_len(in):
 *   need_hash_len(in):
 *   dest_p(out)
 *   dest_len_p(out):
 * Note:
 */
int
crypt_sha_two (THREAD_ENTRY * thread_p, const char *src, int src_len,
	       int need_hash_len, char **dest_p, int *dest_len_p)
{
  int hash_length;
  int algo;
  char *dest = NULL;
  int dest_len;
  char *dest_hex = NULL;
  int dest_hex_len;
  int error_status = NO_ERROR;

  assert (src != NULL);

  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
    }

  *dest_p = NULL;

  switch (need_hash_len)
    {
    case 0:
    case 256:
      algo = GCRY_MD_SHA256;
      break;
    case 224:
      algo = GCRY_MD_SHA224;
      break;
    case 384:
      algo = GCRY_MD_SHA384;
      break;
    case 512:
      algo = GCRY_MD_SHA512;
      break;
    default:
      return NO_ERROR;
    }

  if (init_gcrypt () != NO_ERROR)
    {
      return ER_ENCRYPTION_LIB_FAILED;
    }

  hash_length = gcry_md_get_algo_dlen (algo);
  dest_len = hash_length;
  dest = (char *) db_private_alloc (thread_p, hash_length);
  if (dest == NULL)
    {
      error_status = ER_OUT_OF_VIRTUAL_MEMORY;
      goto exit_and_free;
    }

  gcry_md_hash_buffer (algo, dest, src, src_len);
  dest_hex = str_to_hex (thread_p, dest, dest_len, &dest_hex, &dest_hex_len);
  if (dest_hex == NULL)
    {
      error_status = ER_OUT_OF_VIRTUAL_MEMORY;
      goto exit_and_free;
    }

  *dest_p = dest_hex;
  *dest_len_p = dest_hex_len;

exit_and_free:
  if (dest != NULL)
    {
      db_private_free_and_init (thread_p, dest);
    }
  return error_status;
}

/*
 * crypt_generate_random_bytes() - Generate random number bytes
 *   return: error code or NO_ERROR
 *   thread_p(in): thread context
 *   dest(out): the generated bytes
 *   length(in): the length of bytes to generate
 * Note:
 */
int
crypt_generate_random_bytes (THREAD_ENTRY * thread_p, char *dest, int length)
{
  int error_status = NO_ERROR;

  assert (dest != NULL);

  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
    }

  if (init_gcrypt () != NO_ERROR)
    {
      return ER_ENCRYPTION_LIB_FAILED;
    }

  gcry_randomize (dest, length, GCRY_STRONG_RANDOM);

  return error_status;
}
