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
				    int maxlen);
extern int crypt_encrypt_sha1_printable (const char *line, char *crypt,
					 int maxlen);

#endif /* _ENCRYPTION_H_ */
