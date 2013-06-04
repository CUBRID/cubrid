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
 *	Crypt_opfunc.h: 
 */

#ifndef _CRYPT_OPFUNC_H_
#define _CRYPT_OPFUNC_H_

#ident "$Id$"

#include "config.h"

extern int aes_default_encrypt (THREAD_ENTRY * thread_p, const char *src,
				int src_len, const char *key, int key_len,
				char **dest_p, int *dest_len_p);
extern int aes_default_decrypt (THREAD_ENTRY * thread_p, const char *src,
				int src_len, const char *key, int key_len,
				char **dest_p, int *dest_len_p);
extern int sha_one (THREAD_ENTRY * thread_p, const char *src, int src_len,
		    char **dest_p, int *dest_len_p);
extern int sha_two (THREAD_ENTRY * thread_p, const char *src, int src_len,
		    int need_hash_len, char **dest_p, int *dest_len_p);

#endif
