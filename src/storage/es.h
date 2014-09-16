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
 * esi.h - header for external storage API  (at client and server)
 */

#ifndef _ES_H_
#define _ES_H_

#include <sys/types.h>

#include "porting.h"
#include "es_common.h"
#include "recovery.h"

#define ES_URI_PREFIX_MAX	8
#define ES_MAX_URI_LEN		(PATH_MAX + ES_URI_PREFIX_MAX)

typedef char ES_URI[ES_MAX_URI_LEN];

/* APIs */
extern int es_init (const char *uri);
extern void es_final (void);
extern int es_create_file (char *out_uri);
extern ssize_t es_write_file (const char *uri, const void *buf, size_t count,
			      off_t offset);
extern ssize_t es_read_file (const char *uri, void *buf, size_t count,
			     off_t offset);
extern int es_delete_file (const char *uri);
extern int es_copy_file (const char *in_uri, const char *metaname,
			 char *out_uri);
extern int es_rename_file (const char *in_uri, const char *metaname,
			   char *out_uri);
extern off_t es_get_file_size (const char *uri);

extern int es_rv_nop (THREAD_ENTRY * thread_p, LOG_RCV * rcv);

#if defined (SERVER_MODE)
extern void es_notify_vacuum_for_delete (THREAD_ENTRY * thread_p,
					 const char *uri);
#endif /* SERVER_MODE */
#endif /* _ES_H_ */
