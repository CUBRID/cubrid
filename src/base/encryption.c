/*
 * Copyright 2008 Search Solution Corporation
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
 * encryption.c - Encryption utilities
 */

#include "config.h"

#if defined (WINDOWS)
#include <windows.h>
#include <tchar.h>
#include <stdio.h>
#elif defined (HAVE_RPC_DES_CRYPT_H)
#include <rpc/des_crypt.h>
#else // OPENSSL
#include "crypt_opfunc.h"
#include "error_code.h"
#endif

#include "encryption.h"
#include "memory_alloc.h"
#include "sha1.h"
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

#if defined (WINDOWS)
static BYTE des_Keyblob[] = {
  0x08, 0x02, 0x00, 0x00, 0x01, 0x66, 0x00, 0x00,	// BLOB header
  0x08, 0x00, 0x00, 0x00,	// key length, in bytes
  'U', '9', 'a', '$', 'y', '1', '@', 'z'	// DES key with parity
};
#else
static char crypt_Key[8];
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
  char default_key[] = { 10, 7, 1, 9, 12, 2, 11, 2, 12, 19, 1, 12, 9, 7, 11, 15 };

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
#else
  memcpy (crypt_Key, key, 8);
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
  if (CryptAcquireContext (&hProv, NULL, MS_ENHANCED_PROV, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT))
    {
      /* Import PlainText Key */
      if (!CryptImportKey (hProv, des_Keyblob, sizeof (des_Keyblob), 0, CRYPT_EXPORTABLE, &hKey))
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

	  dwLength = (int) strlen (line);
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
#elif defined (HAVE_RPC_DES_CRYPT_H)
  int outlen, inlen;
  int i;
  int padlen;
  unsigned char inbuf[2048];
  unsigned char padchar;

  inlen = strnlen (line, sizeof (inbuf));
  strncpy ((char *) inbuf, line, inlen);

  /* Insert PKCS style padding */
  padchar = 8 - (inlen % 8);
  padlen = (int) padchar;

  for (i = 0; i < padlen; i++)
    {
      inbuf[inlen + i] = padchar;
    }

  inlen += padlen;

  if (DES_FAILED (ecb_crypt (crypt_Key, (char *) inbuf, inlen, DES_ENCRYPT)))
    {
      return -1;
    }
  memcpy (crypt, inbuf, inlen);
  outlen = inlen;

  crypt[outlen] = '\0';
  return (outlen);
#else
  int outlen = 0;
  int line_len = (int) strlen (line);
  char *dest = NULL;

  int ec = crypt_default_encrypt (NULL, line, line_len, crypt_Key, (int) sizeof crypt_Key, &dest, &outlen, DES_ECB);
  if (ec != NO_ERROR)
    {
      return -1;
    }

  memcpy (crypt, dest, outlen);
  db_private_free_and_init (NULL, dest);

  crypt[outlen] = '\0';
  return (outlen);
#endif
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
  SHA1Input (&sha, (const unsigned char *) line, (int) strlen (line));

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
