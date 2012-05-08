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
 * encryption.c - Encryption utilities
 */

#include "config.h"

#if defined (WINDOWS)
#include <windows.h>
#include <tchar.h>
#include <stdio.h>
#elif defined (HAVE_RPC_DES_CRYPT_H)
#include <rpc/des_crypt.h>
#elif defined (HAVE_LIBGCRYPT)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <gcrypt.h>
#else
#error "libgcrypt or rpc/des_crypt.h file required"
#endif

#include "encryption.h"
#include "sha1.h"

#if defined (WINDOWS)
static BYTE des_Keyblob[] = {
  0x08, 0x02, 0x00, 0x00, 0x01, 0x66, 0x00, 0x00,	// BLOB header 
  0x08, 0x00, 0x00, 0x00,	// key length, in bytes
  'U', '9', 'a', '$', 'y', '1', '@', 'z'	// DES key with parity
};
#elif defined (HAVE_RPC_DES_CRYPT_H)
static char crypt_Key[8];
#elif defined (HAVE_LIBGCRYPT)
/* libgcrypt cipher handle */
static gcry_cipher_hd_t cipher_Hd;
#endif /* WINDOWS */

/*
 * crypt_seed - Prepares a set of seeds for the encryption algorithm
 *   return:
 *   key(in): key string
 *
 * Note: If no seed key is given, the built in default seed key is used.
 *       This must be called prior to calling any of the other encryption
 *       functions.  For decryption, the seed key must match the key used to
 *       encrypt the original string.
 */
void
crypt_seed (const char *key)
{
  char default_key[] =
    { 10, 7, 1, 9, 12, 2, 11, 2, 12, 19, 1, 12, 9, 7, 11, 15 };

  /* use default key if none supplied */
  if (key == NULL)
    {
      key = default_key;
    }
#if defined(WINDOWS)
  /* key size must be large than 8 byte */
  memcpy (des_Keyblob + 12, key, 8);
#elif defined (HAVE_RPC_DES_CRYPT_H)
  memcpy (crypt_Key, key, 8);
  des_setparity (crypt_Key);
#elif defined (HAVE_LIBGCRYPT)
  char iv[] = { 1, 4, 2, 2, 3, 1, 1, 9 };
  gcry_error_t err;

  gcry_check_version (NULL);
  err =
    gcry_cipher_open (&cipher_Hd, GCRY_CIPHER_DES, GCRY_CIPHER_MODE_ECB, 0);
  assert (err == 0);
  err = gcry_cipher_setkey (cipher_Hd, key, 8);	/* 56 bits from 8 bytes */
  assert (err == 0);
  err = gcry_cipher_setiv (cipher_Hd, iv, 8);	/* 64 bits */
  assert (err == 0);
#endif /* WINDOWS */
}

/*
 * crypt_encrypt_printable - combines the basic crypt_line encryption with a
 *                           conversion to all capitol letters for printability
 *   return: number of chars in encrypted string
 *   line(in): line to encrypt
 *   crypt(in): encrypted output buffer
 *   maxlen(in): maximum length of buffer
 */
int
crypt_encrypt_printable (const char *line, char *crypt, int maxlen)
{
#if defined(WINDOWS)
  HCRYPTPROV hProv = 0;
  HCRYPTKEY hKey = 0;
  DWORD dwLength;
  DWORD dwCount;
  DWORD padding = PKCS5_PADDING;
  DWORD cipher_mode = CRYPT_MODE_ECB;
  BYTE pbBuffer[2048];

  /* Get handle to user default provider. */
  if (CryptAcquireContext (&hProv, NULL, MS_ENHANCED_PROV,
			   PROV_RSA_FULL, CRYPT_VERIFYCONTEXT))
    {
      /* Import PlainText  Key */
      if (!CryptImportKey (hProv, des_Keyblob, sizeof (des_Keyblob),
			   0, CRYPT_EXPORTABLE, &hKey))
	{
	  return -1;
	}
      else
	{
	  /* Set DES ECB MODE */
	  if (!CryptSetKeyParam (hKey, KP_MODE, (PBYTE) & cipher_mode, 0))
	    {
	      return -1;
	    }

	  /* Set Padding PKCS#5 */
	  if (!CryptSetKeyParam (hKey, KP_PADDING, (PBYTE) & padding, 0))
	    {
	      return -1;
	    }

	  dwLength = strlen (line);
	  memcpy (pbBuffer, line, dwLength);
	  dwCount = dwLength;

	  if (!CryptEncrypt (hKey, 0, TRUE, 0, pbBuffer, &dwCount, 2048))
	    {
	      return -1;
	    }
	  memcpy (crypt, pbBuffer, dwCount);
	  crypt[dwCount] = 0x00;
	}

      CryptReleaseContext (hProv, 0);
    }
  else
    {
      return -1;
    }
  return (dwCount);
#elif defined (HAVE_LIBGCRYPT) || defined (HAVE_RPC_DES_CRYPT_H)
  int outlen, inlen, padding_size;
  int i;
  int padlen;
  unsigned char inbuf[2048];
  unsigned char padchar;

  inlen = strnlen (line, sizeof (inbuf));
  strncpy (inbuf, line, inlen);

  /* Insert PKCS style padding */
  padchar = 8 - (inlen % 8);
  padlen = (int) padchar;

  for (i = 0; i < padlen; i++)
    {
      inbuf[inlen + i] = padchar;
    }

  inlen += padlen;

#if defined (HAVE_RPC_DES_CRYPT_H)
  if (DES_FAILED (ecb_crypt (crypt_Key, inbuf, inlen, DES_ENCRYPT)))
    {
      return -1;
    }
  memcpy (crypt, inbuf, inlen);
#elif defined (HAVE_LIBGCRYPT)
  if (gcry_cipher_encrypt (cipher_Hd, crypt, maxlen, inbuf, inlen))
    {
      return -1;
    }
  gcry_cipher_close (cipher_Hd);
#endif /* HAVE_RPC_DES_CRYPT_H */
  outlen = inlen;
  crypt[outlen] = '\0';
  return (outlen);
#endif /* WINDOWS */
}

/*
 * crypt_decrypt_printable - decrypts a line that was encrypted with
 *                           crypt_encrypt_printable 
 *   return: number of chars in decrypted string
 *   crypt(in): buffer to decrypt
 *   decrypt(out): decrypted output buffer
 *   maxlen(in): maximum length of output buffer
 *
 */
int
crypt_decrypt_printable (const char *crypt, char *decrypt, int maxlen)
{
#if defined (WINDOWS)
  /* We don't need to decrypt */
  return -1;
#elif defined (HAVE_LIBGCRYPT) || defined (HAVE_RPC_DES_CRYPT_H)
  int outlen, inlen, padding_size;
  int i;

  inlen = strlen (crypt);
#if defined (HAVE_RPC_DES_CRYPT_H)
  memcpy (decrypt, crypt, inlen);
  if (DES_FAILED (ecb_crypt (crypt_Key, decrypt, inlen, DES_DECRYPT)))
    {
      return -1;
    }
#elif defined (HAVE_LIBGCRYPT)
  if (gcry_cipher_decrypt (cipher_Hd, decrypt, maxlen, crypt, inlen))
    {
      return -1;
    }
  gcry_cipher_close (cipher_Hd);
#endif /* HAVE_RPC_DES_CRYPT_H */

  outlen = inlen;
  /* Check PKCS style padding */
  padding_size = decrypt[outlen - 1];
  if ((padding_size < 1) || (padding_size > 8))
    {
      return -1;
    }

  for (i = 1; i < padding_size; i++)
    {
      if (decrypt[outlen - 1 - i] != padding_size)
	{
	  return -1;
	}
    }

  outlen -= padding_size;
  decrypt[outlen] = '\0';

  return (outlen);
#endif /* WINDOWS */
}

/*
 * crypt_encrypt_sha1_printable - 
 *   return: number of chars in encrypted string
 *   line(in): line to hash
 *   crypt(in): hashed output buffer
 *   maxlen(in): maximum length of buffer
 */
int
crypt_encrypt_sha1_printable (const char *line, char *crypt, int maxlen)
{
  SHA1Context sha;
  int i;

  SHA1Reset (&sha);
  SHA1Input (&sha, (const unsigned char *) line, strlen (line));

  if (!SHA1Result (&sha))
    {
      return -1;
    }
  else
    {
      /* convert to readable format */
      for (i = 0; i < 5; i++)
	{
	  sprintf (crypt + (i * 8), "%08X", sha.Message_Digest[i]);
	}
      crypt[40] = '\0';
    }
  return 40;
}
