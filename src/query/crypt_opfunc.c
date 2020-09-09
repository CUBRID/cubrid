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
#include <memory>
#include <memory.h>
#include <string.h>
#include <math.h>
#include <assert.h>
#include <errno.h>
#if defined (WINDOWS)
#include <winsock2.h>
#else
#include <netinet/in.h>
#include <arpa/inet.h>
#endif

#include "CRC.h"
#include "thread_compat.hpp"
#include "porting.h"
#include "error_code.h"
#include "error_manager.h"
#include "memory_alloc.h"
#include "crypt_opfunc.h"
#if defined (SERVER_MODE)
#include "thread_manager.hpp"	// for thread_get_thread_entry_info
#endif // SERVER_MODE

#include <openssl/evp.h>
#include <openssl/sha.h>
#include <openssl/rand.h>

#define AES128_BLOCK_LEN (128/8)
#define AES128_KEY_LEN (128/8)
#define DES_BLOCK_LEN (8)
#define MD5_CHECKSUM_LEN 16
#define MD5_CHECKSUM_HEX_LEN (32 + 1)

typedef enum
{
  CRYPT_LIB_INIT_ERR = 0,
  CRYPT_LIB_OPEN_CIPHER_ERR,
  CRYPT_LIB_SET_KEY_ERR,
  CRYPT_LIB_CRYPT_ERR,
  CRYPT_LIB_UNKNOWN_ERR
} CRYPT_LIB_ERROR;

typedef enum
{
  SHA_ONE,
  SHA_TWO_224,
  SHA_TWO_256,
  SHA_TWO_384,
  SHA_TWO_512,
} SHA_FUNCTION;

// *INDENT-OFF*
template<typename T>
using deleted_unique_ptr = std::unique_ptr<T, std::function<void (T *)>>;
// *INDENT-ON*

static const char *const crypt_lib_fail_info[] = {
  "Initialization failure!",
  "Open cipher failure!",
  "Set secret key failure!",
  "Encrypt/decrypt failure!",
  "Unknown error!"
};

static const char lower_hextable[] = "0123456789abcdef";
static const char upper_hextable[] = "0123456789ABCDEF";

static int crypt_sha_functions (THREAD_ENTRY * thread_p, const char *src, int src_len, SHA_FUNCTION sha_func,
				char **dest_p, int *dest_len_p);
static int crypt_md5_buffer_binary (const char *buffer, size_t len, char *resblock);
static void aes_default_gen_key (const char *key, int key_len, char *dest_key, int dest_key_len);

void
str_to_hex_prealloced (const char *src, int src_len, char *dest, int dest_len, HEX_LETTERCASE lettercase)
{
  int i = src_len;
  unsigned char item_num = 0;
  const char *hextable;

  assert (src != NULL && dest != NULL);
  assert (dest_len >= (src_len * 2 + 1));

  if (lettercase == HEX_UPPERCASE)
    {
      hextable = upper_hextable;
    }
  else
    {
      hextable = lower_hextable;
    }
  while (i > 0)
    {
      --i;
      item_num = (unsigned char) src[i];
      dest[2 * i] = hextable[item_num / 16];
      dest[2 * i + 1] = hextable[item_num % 16];
    }
  dest[src_len * 2] = '\0';
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
char *
str_to_hex (THREAD_ENTRY * thread_p, const char *src, int src_len, char **dest_p, int *dest_len_p,
	    HEX_LETTERCASE lettercase)
{
  int dest_len = 2 * src_len + 1;
  int i = 0;
  unsigned char item_num = 0;
  char *dest;

  assert (src != NULL);

#if defined (SERVER_MODE)
  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
    }
#endif // SERVER_MODE

  dest = (char *) db_private_alloc (thread_p, dest_len * sizeof (char));
  if (dest == NULL)
    {
      return NULL;
    }

  str_to_hex_prealloced (src, src_len, dest, dest_len, lettercase);
  *dest_p = dest;
  *dest_len_p = dest_len - 1;
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
aes_default_gen_key (const char *key, int key_len, char *dest_key, int dest_key_len)
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
 * crypt_default_encrypt() - like mysql's aes_encrypt. Use (AES-128/DES)/ECB/PKCS7 method.
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
crypt_default_encrypt (THREAD_ENTRY * thread_p, const char *src, int src_len, const char *key, int key_len,
		       char **dest_p, int *dest_len_p, CIPHER_ENCRYPTION_TYPE enc_type)
{
  int pad;
  int padding_src_len;
  int ciphertext_len = 0;
  char *padding_src = NULL;
  char *dest = NULL;
  int error_status = NO_ERROR;

  assert (src != NULL);
  assert (key != NULL);
  const EVP_CIPHER *cipher;
  int block_len;
  char new_key[AES128_KEY_LEN + 1];
  const char *key_arg = NULL;
  switch (enc_type)
    {
    case AES_128_ECB:
      cipher = EVP_aes_128_ecb ();
      block_len = AES128_BLOCK_LEN;
      aes_default_gen_key (key, key_len, new_key, AES128_KEY_LEN);
      new_key[AES128_KEY_LEN] = '\0';
      key_arg = new_key;
      break;
    case DES_ECB:
      cipher = EVP_des_ecb ();
      block_len = DES_BLOCK_LEN;
      key_arg = key;
      break;
    default:
      return ER_FAILED;
    }

#if defined (SERVER_MODE)
  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
    }
#endif // SERVER_MODE

  *dest_p = NULL;
  *dest_len_p = 0;

  // *INDENT-OFF*
  deleted_unique_ptr<EVP_CIPHER_CTX> context (EVP_CIPHER_CTX_new (), [] (EVP_CIPHER_CTX *ctxt_ptr)
    {
      if (ctxt_ptr != NULL)
	{
	  EVP_CIPHER_CTX_free (ctxt_ptr);
	}
    });
  // *INDENT-ON*

  if (context == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ENCRYPTION_LIB_FAILED, 1, crypt_lib_fail_info[CRYPT_LIB_INIT_ERR]);
      return ER_ENCRYPTION_LIB_FAILED;
    }

  if (EVP_EncryptInit (context.get (), cipher, (const unsigned char *) key_arg, NULL) != 1)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ENCRYPTION_LIB_FAILED, 1, crypt_lib_fail_info[CRYPT_LIB_INIT_ERR]);
      return ER_ENCRYPTION_LIB_FAILED;
    }

  /* PKCS7 */
  if ((src_len % block_len) == 0)
    {
      pad = block_len;
      padding_src_len = src_len + pad;
    }
  else
    {
      padding_src_len = (int) ceil ((double) src_len / block_len) * block_len;
      pad = padding_src_len - src_len;
    }

  padding_src = (char *) db_private_alloc (thread_p, padding_src_len);
  if (padding_src == NULL)
    {
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }
  memcpy (padding_src, src, src_len);
  memset (padding_src + src_len, pad, pad);

  dest = (char *) db_private_alloc (thread_p, padding_src_len);
  if (dest == NULL)
    {
      db_private_free_and_init (thread_p, padding_src);
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  if (EVP_EncryptUpdate (context.get (), (unsigned char *) dest, &ciphertext_len, (const unsigned char *) padding_src,
			 padding_src_len) != 1)
    {
      db_private_free_and_init (thread_p, padding_src);
      db_private_free_and_init (thread_p, dest);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ENCRYPTION_LIB_FAILED, 1, crypt_lib_fail_info[CRYPT_LIB_CRYPT_ERR]);
      return ER_ENCRYPTION_LIB_FAILED;
    }

  *dest_len_p = ciphertext_len;
  *dest_p = dest;

  db_private_free_and_init (thread_p, padding_src);
  return NO_ERROR;
}

/*
 * crypt_default_decrypt() - like mysql's aes_decrypt. Use AES-128/ECB/PKCS7 method.
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
crypt_default_decrypt (THREAD_ENTRY * thread_p, const char *src, int src_len, const char *key, int key_len,
		       char **dest_p, int *dest_len_p, CIPHER_ENCRYPTION_TYPE enc_type)
{
  char *dest = NULL;
  int dest_len;
  int error_status = NO_ERROR;
  int pad, pad_len;
  int i;

  const EVP_CIPHER *cipher;
  int block_len;
  char new_key[AES128_KEY_LEN + 1];
  const char *key_arg = NULL;
  switch (enc_type)
    {
    case AES_128_ECB:
      cipher = EVP_aes_128_ecb ();
      block_len = AES128_BLOCK_LEN;
      aes_default_gen_key (key, key_len, new_key, AES128_KEY_LEN);
      new_key[AES128_KEY_LEN] = '\0';
      key_arg = new_key;
      break;
    case DES_ECB:
      cipher = EVP_des_ecb ();
      block_len = DES_BLOCK_LEN;
      key_arg = key;
      break;
    default:
      return ER_FAILED;
    }

#if defined (SERVER_MODE)
  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
    }
#endif // SERVER_MODE

  assert (src != NULL);
  assert (key != NULL);

  *dest_p = NULL;
  *dest_len_p = 0;

  /* src is not a string encrypted by aes_default_encrypt, return NULL */
  if (src_len % block_len)
    {
      return NO_ERROR;
    }

  // *INDENT-OFF*
  deleted_unique_ptr<EVP_CIPHER_CTX> context (EVP_CIPHER_CTX_new (), [] (EVP_CIPHER_CTX *ctxt_ptr)
    {
      if (ctxt_ptr != NULL)
	{
	  EVP_CIPHER_CTX_free (ctxt_ptr);
	}
    });
  // *INDENT-ON*

  if (context == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ENCRYPTION_LIB_FAILED, 1, crypt_lib_fail_info[CRYPT_LIB_INIT_ERR]);
      return ER_ENCRYPTION_LIB_FAILED;
    }

  if (EVP_DecryptInit (context.get (), cipher, (const unsigned char *) key_arg, NULL) != 1)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ENCRYPTION_LIB_FAILED, 1, crypt_lib_fail_info[CRYPT_LIB_INIT_ERR]);
      return ER_ENCRYPTION_LIB_FAILED;
    }

  dest = (char *) db_private_alloc (thread_p, src_len * sizeof (char));
  if (dest == NULL)
    {
      error_status = ER_OUT_OF_VIRTUAL_MEMORY;
      return error_status;
    }

  int len;
  if (EVP_DecryptUpdate (context.get (), (unsigned char *) dest, &len, (const unsigned char *) src, src_len) != 1)
    {
      db_private_free_and_init (thread_p, dest);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ENCRYPTION_LIB_FAILED, 1, crypt_lib_fail_info[CRYPT_LIB_CRYPT_ERR]);
      return ER_ENCRYPTION_LIB_FAILED;
    }

  if (EVP_DecryptFinal (context.get (), (unsigned char *) dest + len, &len) != 1)
    {
      db_private_free_and_init (thread_p, dest);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ENCRYPTION_LIB_FAILED, 1, crypt_lib_fail_info[CRYPT_LIB_CRYPT_ERR]);
      return ER_ENCRYPTION_LIB_FAILED;
    }

  /* PKCS7 */
  if (src_len != 0)
    {
      pad = dest[src_len - 1];
      if (pad > AES128_BLOCK_LEN)
	{
	  /* src is not a string encrypted by aes_default_encrypt, return NULL */
	  db_private_free_and_init (thread_p, dest);
	  return ER_FAILED;
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
	  db_private_free_and_init (thread_p, dest);
	  return ER_FAILED;
	}
    }

  *dest_p = dest;
  *dest_len_p = dest_len;
  return NO_ERROR;
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
crypt_sha_one (THREAD_ENTRY * thread_p, const char *src, int src_len, char **dest_p, int *dest_len_p)
{
  return crypt_sha_functions (thread_p, src, src_len, SHA_ONE, dest_p, dest_len_p);
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
crypt_sha_two (THREAD_ENTRY * thread_p, const char *src, int src_len, int need_hash_len, char **dest_p, int *dest_len_p)
{
  SHA_FUNCTION sha_func;

  switch (need_hash_len)
    {
    case 0:
    case 256:
      sha_func = SHA_TWO_256;
      break;
    case 224:
      sha_func = SHA_TWO_224;
      break;
    case 384:
      sha_func = SHA_TWO_384;
      break;
    case 512:
      sha_func = SHA_TWO_512;
      break;
    default:
      return NO_ERROR;
    }
  return crypt_sha_functions (thread_p, src, src_len, sha_func, dest_p, dest_len_p);
}

static int
crypt_sha_functions (THREAD_ENTRY * thread_p, const char *src, int src_len, SHA_FUNCTION sha_func, char **dest_p,
		     int *dest_len_p)
{
  char *dest_hex = NULL;
  int dest_hex_len;
  assert (src != NULL);

#if defined (SERVER_MODE)
  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
    }
#endif // SERVER_MODE

  *dest_p = NULL;

  // *INDENT-OFF*
  deleted_unique_ptr<EVP_MD_CTX> context (EVP_MD_CTX_new (), [] (EVP_MD_CTX * ctxt_ptr)
    {
      if (ctxt_ptr != NULL)
	{
	  EVP_MD_CTX_free (ctxt_ptr);
	}
    });
  // *INDENT-ON*

  if (context == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ENCRYPTION_LIB_FAILED, 1, crypt_lib_fail_info[CRYPT_LIB_INIT_ERR]);
      return ER_ENCRYPTION_LIB_FAILED;
    }

  int rc;
  switch (sha_func)
    {
    case SHA_ONE:
      rc = EVP_DigestInit (context.get (), EVP_sha1 ());
      break;
    case SHA_TWO_256:
      rc = EVP_DigestInit (context.get (), EVP_sha256 ());
      break;
    case SHA_TWO_224:
      rc = EVP_DigestInit (context.get (), EVP_sha224 ());
      break;
    case SHA_TWO_384:
      rc = EVP_DigestInit (context.get (), EVP_sha384 ());
      break;
    case SHA_TWO_512:
      rc = EVP_DigestInit (context.get (), EVP_sha512 ());
      break;
    default:
      assert (false);
      return ER_FAILED;
    }
  if (rc == 0)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ENCRYPTION_LIB_FAILED, 1, crypt_lib_fail_info[CRYPT_LIB_INIT_ERR]);
      return ER_ENCRYPTION_LIB_FAILED;
    }

  if (EVP_DigestUpdate (context.get (), src, src_len) == 0)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ENCRYPTION_LIB_FAILED, 1, crypt_lib_fail_info[CRYPT_LIB_CRYPT_ERR]);
      return ER_ENCRYPTION_LIB_FAILED;
    }

  unsigned char hash[EVP_MAX_MD_SIZE];
  unsigned int lengthOfHash = 0;
  if (EVP_DigestFinal (context.get (), hash, &lengthOfHash) == 0)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ENCRYPTION_LIB_FAILED, 1, crypt_lib_fail_info[CRYPT_LIB_CRYPT_ERR]);
      return ER_ENCRYPTION_LIB_FAILED;
    }

  dest_hex = str_to_hex (thread_p, (char *) hash, lengthOfHash, &dest_hex, &dest_hex_len, HEX_UPPERCASE);
  if (dest_hex == NULL)
    {
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  *dest_p = dest_hex;
  *dest_len_p = dest_hex_len;

  return NO_ERROR;
}

static int
crypt_md5_buffer_binary (const char *buffer, size_t len, char *resblock)
{
  if (buffer == NULL || resblock == NULL)
    {
      assert (false);
      return ER_FAILED;
    }
  // *INDENT-OFF*
  deleted_unique_ptr<EVP_MD_CTX> context (EVP_MD_CTX_new (), [] (EVP_MD_CTX *ctxt_ptr)
    {
      if (ctxt_ptr != NULL)
	{
	  EVP_MD_CTX_free (ctxt_ptr); 
	}
    });
  // *INDENT-ON*

  if (context == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ENCRYPTION_LIB_FAILED, 1, crypt_lib_fail_info[CRYPT_LIB_INIT_ERR]);
      return ER_ENCRYPTION_LIB_FAILED;
    }

  if (EVP_DigestInit (context.get (), EVP_md5 ()) == 0)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ENCRYPTION_LIB_FAILED, 1, crypt_lib_fail_info[CRYPT_LIB_INIT_ERR]);
      return ER_ENCRYPTION_LIB_FAILED;
    }
  if (EVP_DigestUpdate (context.get (), buffer, len) == 0)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ENCRYPTION_LIB_FAILED, 1, crypt_lib_fail_info[CRYPT_LIB_CRYPT_ERR]);
      return ER_ENCRYPTION_LIB_FAILED;
    }
  if (EVP_DigestFinal (context.get (), (unsigned char *) resblock, NULL) == 0)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ENCRYPTION_LIB_FAILED, 1, crypt_lib_fail_info[CRYPT_LIB_CRYPT_ERR]);
      return ER_ENCRYPTION_LIB_FAILED;
    }

  return NO_ERROR;
}

int
crypt_md5_buffer_hex (const char *buffer, size_t len, char *resblock)
{
  int ec = NO_ERROR;
  if (buffer == NULL || resblock == NULL)
    {
      assert (false);
      return ER_FAILED;
    }
  ec = crypt_md5_buffer_binary (buffer, len, resblock);
  if (ec != NO_ERROR)
    {
      return ec;
    }
  str_to_hex_prealloced (resblock, MD5_CHECKSUM_LEN, resblock, MD5_CHECKSUM_HEX_LEN, HEX_LOWERCASE);
  return NO_ERROR;
}

/*
 * crypt_crc32() -
 *   return:
 *   src(in): original message
 *   src_len(in): length of original message
 *   dest(out): crc32 result
 * Note:
 */
void
crypt_crc32 (const char *src, int src_len, int *dest)
{
  assert (src != NULL && dest != NULL);
// *INDENT-OFF*
  *dest = CRC::Calculate (src, src_len, CRC::CRC_32 ());
// *INDENT-ON*
}

/*
 * crypt_generate_random_bytes() - Generate random number bytes
 *   return: error code or NO_ERROR
 *   dest(out): the generated bytes
 *   length(in): the length of bytes to generate
 * Note:
 */
int
crypt_generate_random_bytes (char *dest, int length)
{
  assert (dest != NULL && length > 0);

  if (RAND_bytes ((unsigned char *) dest, length) != 1)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ENCRYPTION_LIB_FAILED, 1, crypt_lib_fail_info[CRYPT_LIB_CRYPT_ERR]);
      return ER_ENCRYPTION_LIB_FAILED;
    }

  return NO_ERROR;
}
