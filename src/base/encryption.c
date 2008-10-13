/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * encryption.c - Encryption utilities
 *
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "openssl/evp.h"

#include "encryption.h"


/* EVP cipher context for openssl */

static EVP_CIPHER_CTX crypt_Ctx;

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
  char iv[] = { 1, 4, 2, 2, 3, 1, 1, 9 };

  /* use default key if none supplied */
  if (key == NULL)
    key = default_key;

  EVP_CIPHER_CTX_init (&crypt_Ctx);

  EVP_EncryptInit_ex (&crypt_Ctx, EVP_des_ecb (), NULL,
		      (unsigned char *) key, (unsigned char *) iv);
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
  int outlen, tmplen;
  unsigned char outbuf[1024];

  if (!EVP_EncryptUpdate (&crypt_Ctx, outbuf, &outlen,
			  (unsigned char *) line, strlen (line)))
    {
      /* Error */
      return -1;
    }

  /* Buffer passed to EVP_EncryptFinal() must be after data just
   * encrypted to avoid overwriting it.
   */
  if (!EVP_EncryptFinal_ex (&crypt_Ctx, outbuf + outlen, &tmplen))
    {
      /* Error */
      return -1;
    }
  outlen += tmplen;
  EVP_CIPHER_CTX_cleanup (&crypt_Ctx);

  if (maxlen < outlen)
    return -1;

  strncpy (crypt, (char *) outbuf, outlen);
  crypt[outlen] = '\0';

  return (outlen);
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
  int outlen, tmplen;
  unsigned char outbuf[1024];

  if (!EVP_DecryptUpdate (&crypt_Ctx, outbuf, &outlen,
			  (unsigned char *) crypt, strlen (crypt)))
    {
      /* Error */
      return -1;
    }

  /* Buffer passed to EVP_EncryptFinal() must be after data just
   * encrypted to avoid overwriting it.
   */
  if (!EVP_DecryptFinal_ex (&crypt_Ctx, outbuf + outlen, &tmplen))
    {
      /* Error */
      return -1;
    }
  outlen += tmplen;
  EVP_CIPHER_CTX_cleanup (&crypt_Ctx);

  if (maxlen < outlen)
    return -1;

  strncpy (decrypt, (char *) outbuf, outlen);
  decrypt[outlen] = '\0';

  return (outlen);
}
