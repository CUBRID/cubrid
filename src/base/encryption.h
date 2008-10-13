/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * encryption.h - Encryption utilities
 */

#ifndef _ENCRYPTION_H_
#define _ENCRYPTION_H_

#ident "$Id$"

#include "porting.h"

extern void crypt_seed (const char *seed);

extern int crypt_encrypt_printable (const char *line, char *crypt,
				    int maxlen);
extern int crypt_decrypt_printable (const char *crypt, char *decrypt,
				    int maxlen) __attribute__ ((deprecated));

#endif /* _ENCRYPTION_H_ */
