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
 * encryption.h - Encryption utilities
 */

#ifndef _ENCRYPTION_H_
#define _ENCRYPTION_H_

#ident "$Id$"

#include "porting.h"

extern void crypt_seed (const char *seed);

extern int crypt_encrypt_printable (const char *line, char *crypt, int maxlen);
extern int crypt_encrypt_sha1_printable (const char *line, char *crypt, int maxlen);

#endif /* _ENCRYPTION_H_ */
