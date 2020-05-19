/*
 * Copyright (C) 2020 CUBRID Corporation. All rights reserved by CUBRID.
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
 * tde.cpp - 
 */

#ident "$Id$"

#include <stdlib.h>
#include <assert.h>

#include <openssl/conf.h>
#include <openssl/evp.h>
#include <openssl/err.h>

#include "error_code.h"
#include "tde.hpp"

TDE_CIPHER tde_Cipher; // global var for TDE Module


static int tde_load_master_key (const char* key_path);
static int tde_load_data_keys (void);
static int tde_store_data_keys (void);

static int tde_encrypt_internal (const unsigned char * plain_buffer, int length, TDE_ALGORITHM tde_algo,  
    const unsigned char * key, const unsigned char * nonce, unsigned char * cipher_buffer);
static int tde_decrypt_internal (const unsigned char * cipher_buffer, int length, TDE_ALGORITHM tde_algo, 
    const unsigned char * key, const unsigned char * nonce, unsigned char * plain_buffer);


int 
tde_initialize (void)
{
  // TODO
  // 1. tde_Cipher init 
  // 2. tde_Cipher update from boot_db_parm 
  // 3. master key loading
  // 4. data key loading
  //
  
  // for test
  memcpy (tde_Cipher.data_keys.perm_key, "01234567890123456789012345678901", TDE_DATA_KEY_LENGTH);
  memcpy (tde_Cipher.data_keys.temp_key, "12345678901234567890123456789012", TDE_DATA_KEY_LENGTH);
  memcpy (tde_Cipher.data_keys.log_key, "23456789012345678901234567890123", TDE_DATA_KEY_LENGTH);
  tde_Cipher.data_keys.is_loaded = true;

  tde_Cipher.temp_write_counter.store(0);

  return NO_ERROR;

}

int
tde_encrypt_data_page (const unsigned char * iopage_plain, unsigned char * iopage_cipher, TDE_ALGORITHM tde_algo,  bool is_temp)
{
  int err = NO_ERROR;
  unsigned char nonce[TDE_DATA_PAGE_NONCE_LENGTH];
  FILEIO_PAGE* iopage = (FILEIO_PAGE*) iopage_plain;
  const unsigned char * data_key;
  
  memset (nonce, 0, TDE_DATA_PAGE_NONCE_LENGTH); 

  if (is_temp)
  {
    // temporary file: p_reserve_1 for nonce TODO: p_reserve_3 -> ?
    data_key = tde_Cipher.data_keys.temp_key;
    iopage->prv.p_reserve_3 = tde_Cipher.temp_write_counter.fetch_add(1);
    memcpy (nonce, &iopage->prv.p_reserve_3, sizeof(iopage->prv.p_reserve_3)); 
  }
  else 
  {
    // permanent file: page lsa for nonce 
    data_key = tde_Cipher.data_keys.perm_key;
    memcpy (nonce, &iopage->prv.lsa, sizeof(iopage->prv.lsa)); 
  }

  memcpy (iopage_cipher, iopage_plain, IO_PAGESIZE);
   
  err = tde_encrypt_internal (iopage_plain + TDE_DATA_PAGE_ENC_OFFSET, 
     TDE_DATA_PAGE_ENC_LENGTH, tde_algo, data_key, nonce, 
     iopage_cipher + TDE_DATA_PAGE_ENC_OFFSET); 
  
  return err;
}

int
tde_decrypt_data_page (const unsigned char * iopage_cipher, unsigned char * iopage_plain, TDE_ALGORITHM tde_algo, bool is_temp)
{
  int err = NO_ERROR;
  unsigned char nonce[TDE_DATA_PAGE_NONCE_LENGTH];
  FILEIO_PAGE* iopage = (FILEIO_PAGE*) iopage_cipher;
  const unsigned char * data_key;
  
  memset (nonce, 0, TDE_DATA_PAGE_NONCE_LENGTH); 

  if (is_temp)
  {
    // temporary file: p_reserve_1 for nonce TODO: p_reserve_3 -> ?
    data_key = tde_Cipher.data_keys.temp_key;
    memcpy (nonce, &iopage->prv.p_reserve_3, sizeof(iopage->prv.p_reserve_3)); 
  }
  else 
  {
    // permanent file: page lsa for nonce 
    data_key = tde_Cipher.data_keys.perm_key;
    memcpy (nonce, &iopage->prv.lsa, sizeof(iopage->prv.lsa)); 
  }

  memcpy (iopage_plain, iopage_cipher, IO_PAGESIZE);
   
  err = tde_decrypt_internal (iopage_cipher + TDE_DATA_PAGE_ENC_OFFSET, 
     TDE_DATA_PAGE_ENC_LENGTH, tde_algo, data_key, nonce, 
     iopage_plain + TDE_DATA_PAGE_ENC_OFFSET); 
  
  return err;
}

int
tde_encrypt_log_page (const unsigned char * iopage_plain, unsigned char * iopage_cipher, TDE_ALGORITHM tde_algo)
{
  return NO_ERROR;
}

int
tde_decrypt_log_page (const unsigned char * iopage_cipher, unsigned char * iopage_plain, TDE_ALGORITHM tde_algo)
{
  return NO_ERROR;
}

int
tde_encrypt_internal (const unsigned char * plain_buffer, int length, TDE_ALGORITHM tde_algo, const unsigned char * key, const unsigned char * nonce, 
    unsigned char* cipher_buffer)
{
    EVP_CIPHER_CTX *ctx;
    const EVP_CIPHER *cipher_type;
    int len;
    int cipher_len;
    int err = NO_ERROR;

    assert (tde_algo == TDE_ALGORITHM_AES || tde_algo == TDE_ALGORITHM_ARIA);

    if(!(ctx = EVP_CIPHER_CTX_new()))
    {
        err = ER_FAILED; 
        goto exit;
    }

    switch (tde_algo)
    {
      case TDE_ALGORITHM_AES:
        cipher_type = EVP_aes_256_ctr();
        break;
      case TDE_ALGORITHM_ARIA:
        cipher_type = EVP_aria_256_ctr();
        break;
      case TDE_ALGORITHM_NONE:
      deafult:
        assert(false);
    }
    
    if(1 != EVP_EncryptInit_ex(ctx, cipher_type, NULL, key, nonce))
    {
        err = ER_FAILED; 
        goto cleanup;
    }
    
    if(1 != EVP_EncryptUpdate(ctx, cipher_buffer, &len, plain_buffer, length))
    {
        err = ER_FAILED; 
        goto cleanup;
    }
    cipher_len = len;
   
    // Further ciphertext bytes may be written at finalizing (Partial block).
    
    if(1 != EVP_EncryptFinal_ex(ctx, cipher_buffer + len, &len))
    {
        err = ER_FAILED; 
        goto cleanup;
    }
    
    cipher_len += len;
    
    // CTR_MODE is stream mode so that there is no need to check,
    // but check it for safe.
    assert (cipher_len == length);
    
cleanup:
    EVP_CIPHER_CTX_free(ctx);

exit:
    return err;
}

int
tde_decrypt_internal (const unsigned char * cipher_buffer, int length, TDE_ALGORITHM tde_algo, const unsigned char * key, const unsigned char * nonce, 
    unsigned char * plain_buffer)
{
    EVP_CIPHER_CTX *ctx;
    const EVP_CIPHER *cipher_type;
    int len;
    int plain_len;
    int err = NO_ERROR;
    
    assert (tde_algo == TDE_ALGORITHM_AES || tde_algo == TDE_ALGORITHM_ARIA);

    if(!(ctx = EVP_CIPHER_CTX_new()))
    {
        err = ER_FAILED; 
        goto exit;
    }
    
    switch (tde_algo)
    {
      case TDE_ALGORITHM_AES:
        cipher_type = EVP_aes_256_ctr();
        break;
      case TDE_ALGORITHM_ARIA:
        cipher_type = EVP_aria_256_ctr();
        break;
      case TDE_ALGORITHM_NONE:
      deafult:
        assert(false);
    }

    if(1 != EVP_DecryptInit_ex(ctx, cipher_type, NULL, key, nonce))
    {
        err = ER_FAILED; 
        goto cleanup;
    }

    if(1 != EVP_DecryptUpdate(ctx, plain_buffer, &len, cipher_buffer, length))
    {
        err = ER_FAILED; 
        goto cleanup;
    }
    plain_len = len;

    // Further plaintext bytes may be written at finalizing (Partial block).
    if(1 != EVP_DecryptFinal_ex(ctx, plain_buffer + len, &len))
    {
        err = ER_FAILED; 
        goto cleanup;
    }
    plain_len += len;
    
    // CTR_MODE is stream mode so that there is no need to check,
    // but check it for safe.
    assert (plain_len == length); 

cleanup:
    EVP_CIPHER_CTX_free(ctx);

exit:
    return err;

}


