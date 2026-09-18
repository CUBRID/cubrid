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
 *	Crypt_opfunc.h:
 */

#ifndef _CRYPT_OPFUNC_H_
#define _CRYPT_OPFUNC_H_

#ident "$Id$"

#include "thread_compat.hpp"

typedef enum
{
  HEX_LOWERCASE,
  HEX_UPPERCASE
} HEX_LETTERCASE;

typedef enum
{
  AES_128_ECB,
  DES_ECB
} CIPHER_ENCRYPTION_TYPE;

/* Digest and cipher algorithms that are prefetched once per process.
 * See crypt_get_md() in crypt_opfunc.c for why the fetch is hoisted out of the
 * per-call path. */
typedef enum
{
  CRYPT_MD_MD5 = 0,
  CRYPT_MD_SHA1,
  CRYPT_MD_SHA224,
  CRYPT_MD_SHA256,
  CRYPT_MD_SHA384,
  CRYPT_MD_SHA512,
  CRYPT_MD_COUNT
} CRYPT_MD_TYPE;

typedef enum
{
  CRYPT_CIPHER_AES_128_ECB = 0,
  CRYPT_CIPHER_DES_ECB,
  CRYPT_CIPHER_AES_256_CTR,
  CRYPT_CIPHER_ARIA_256_CTR,
  CRYPT_CIPHER_COUNT
} CRYPT_CIPHER_TYPE;

/* Opaque OpenSSL handles: the callers that use the returned pointers include
 * <openssl/evp.h> themselves, so this header does not pull the OpenSSL headers
 * into every file that includes it. */
struct evp_md_st;
struct evp_cipher_st;

extern const struct evp_md_st *crypt_get_md (CRYPT_MD_TYPE md_type);
extern const struct evp_cipher_st *crypt_get_cipher (CRYPT_CIPHER_TYPE cipher_type);

extern int crypt_default_encrypt (THREAD_ENTRY * thread_p, const char *src, int src_len, const char *key,
				  int key_len, char **dest_p, int *dest_len_p, CIPHER_ENCRYPTION_TYPE enc_type);
extern int crypt_default_decrypt (THREAD_ENTRY * thread_p, const char *src, int src_len, const char *key,
				  int key_len, char **dest_p, int *dest_len_p, CIPHER_ENCRYPTION_TYPE enc_type);
extern int crypt_sha_one (THREAD_ENTRY * thread_p, const char *src, int src_len, char **dest_p, int *dest_len_p);
extern int crypt_sha_two (THREAD_ENTRY * thread_p, const char *src, int src_len, int need_hash_len, char **dest_p,
			  int *dest_len_p, bool reuse_ctx);
extern int crypt_md5_buffer_hex (const char *buffer, size_t len, char *resblock);
extern char *str_to_hex (THREAD_ENTRY * thread_p, const char *src, int src_len, char **dest_p, int *dest_len_p,
			 HEX_LETTERCASE lettercase);
extern void str_to_hex_prealloced (const char *src, int src_len, char *dest, int dest_len, HEX_LETTERCASE lettercase);

extern int crypt_generate_random_bytes (char *dest, int length);
extern void crypt_crc32 (const char *src, int src_len, int *dest);

extern int crypt_dblink_encrypt (const unsigned char *str, int str_len, unsigned char *cipher_buffer,
				 unsigned char *mk);
extern int crypt_dblink_decrypt (const unsigned char *cipher, int cipher_len, unsigned char *str_buffer,
				 unsigned char *mk);

extern int shake_dblink_password (const char *passwd, char *confused, int confused_size, struct timeval *chk_time);
extern int reverse_shake_dblink_password (char *confused, int length, char *passwd);
extern int crypt_dblink_bin_to_str (const char *src, int src_len, char *dest, int dest_len, unsigned char *key,
				    long tm);
extern int crypt_dblink_str_to_bin (const char *src, int src_len, char *dest, int *dest_len, unsigned char *key);

/* dblink password cipher sizing (shared by DDL handling and _db_global_tran catalog) */
#define DBLINK_PASSWORD_MAX_LENGTH      (128)
#define DBLINK_PASSWORD_CONFUSED_LENGTH (6)	/* include 4(int) + 1(unsigned char) + 1(unsigned char) */
/* Valid data size is the largest multiple of 3 less than or equal to DBLINK_PASSWORD_CIPHER_LENGTH. */
#define DBLINK_PASSWORD_CIPHER_LENGTH   (DBLINK_PASSWORD_MAX_LENGTH + DBLINK_PASSWORD_CONFUSED_LENGTH)
#define DBLINK_PASSWORD_PAD_LENGTH      (40)	/* include 2(length) + 2(mk) + 2(length) + 32(mk), Must be 4 or more */
#define DBLINK_PASSWORD_MAX_BUFSIZE     ((int)(DBLINK_PASSWORD_CIPHER_LENGTH / 3 * 4) + DBLINK_PASSWORD_PAD_LENGTH)

/* Encrypt a raw dblink password into a self-contained cipher string (bounded by cipher_buf_size).
 * Returns NO_ERROR on success, ER_* on failure. cipher_buf must be > DBLINK_PASSWORD_MAX_BUFSIZE. */
extern int crypt_dblink_password_encrypt (const char *passwd, char *cipher_buf, int cipher_buf_size);
/* Decrypt a cipher string produced by crypt_dblink_password_encrypt back to the raw password (bounded). */
extern int crypt_dblink_password_decrypt (const char *cipher, char *raw_buf, int raw_buf_size);

#include "tde.h"
#define  DBLINK_CRYPT_KEY_LENGTH   TDE_DATA_KEY_LENGTH

#endif
