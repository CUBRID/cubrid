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
 * authenticate_context.hpp - Define authenticate context
 *
 */

#ifndef _AUTHENTICATE_PASSWORD_HPP_
#define _AUTHENTICATE_PASSWORD_HPP_

#include "dbtype_def.h"

#define AU_MAX_PASSWORD_CHARS   31
#define AU_MAX_PASSWORD_BUF     2048
#define AU_MAX_COMMENT_CHARS    SM_MAX_COMMENT_LENGTH

#define PASSWORD_ENCRYPTION_SEED        "U9a$y1@zw~a0%"
#define ENCODE_PREFIX_DEFAULT           (char)0
#define ENCODE_PREFIX_DES               (char)1
#define ENCODE_PREFIX_SHA1              (char)2
#define ENCODE_PREFIX_SHA2_512          (char)3
#define IS_ENCODED_DES(string)          (string[0] == ENCODE_PREFIX_DES)
#define IS_ENCODED_SHA1(string)         (string[0] == ENCODE_PREFIX_SHA1)
#define IS_ENCODED_SHA2_512(string)     (string[0] == ENCODE_PREFIX_SHA2_512)
#define IS_ENCODED_ANY(string) \
  (IS_ENCODED_SHA2_512 (string) || IS_ENCODED_SHA1 (string) || IS_ENCODED_DES (string))

void encrypt_password (const char *pass, int add_prefix, char *dest);
void encrypt_password_sha1 (const char *pass, int add_prefix, char *dest);
void encrypt_password_sha2_512 (const char *pass, char *dest);

bool match_password (const char *user, const char *database);
int au_set_password_internal (MOP user, const char *password, int encode, char encrypt_prefix);

#endif