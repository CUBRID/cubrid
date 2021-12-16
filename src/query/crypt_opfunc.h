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

extern int crypt_default_encrypt (THREAD_ENTRY * thread_p, const char *src, int src_len, const char *key,
				  int key_len, char **dest_p, int *dest_len_p, CIPHER_ENCRYPTION_TYPE enc_type);
extern int crypt_default_decrypt (THREAD_ENTRY * thread_p, const char *src, int src_len, const char *key,
				  int key_len, char **dest_p, int *dest_len_p, CIPHER_ENCRYPTION_TYPE enc_type);
extern int crypt_sha_one (THREAD_ENTRY * thread_p, const char *src, int src_len, char **dest_p, int *dest_len_p);
extern int crypt_sha_two (THREAD_ENTRY * thread_p, const char *src, int src_len, int need_hash_len, char **dest_p,
			  int *dest_len_p);
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
extern int crypt_dblink_bin_to_str (const char *src, int src_len, char *dest, int dest_len, unsigned char *pk, long tm);
extern int crypt_dblink_str_to_bin (const char *src, int src_len, char *dest, int *dest_len, unsigned char *pk);


#include "tde.h"
#define  DBLINK_CRYPT_KEY_LENGTH   TDE_DATA_KEY_LENGTH

typedef struct dblink_cipher
{
  bool is_loaded;
  unsigned char crypt_key[DBLINK_CRYPT_KEY_LENGTH];
} DBLINK_CHPHER_KEY;
extern DBLINK_CHPHER_KEY dblink_Cipher;

#if !defined(CS_MODE)
extern int dblink_get_encrypt_key (unsigned char *key_buf, int key_buf_sz);
#endif
#endif
