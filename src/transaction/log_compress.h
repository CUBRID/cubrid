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
 * log_compress.h - log compression functions
 *
 * Note: Using lzo2 library
 */

#ifndef _LOG_COMPRESS_H_
#define _LOG_COMPRESS_H_

#ident "$Id$"

#include "memory_alloc.h"
#include "lzoconf.h"
#include "lzo1x.h"

#define MAKE_ZIP_LEN(length)                                                  \
         ((length) | 0x80000000)

#define GET_ZIP_LEN(length)                                                   \
         ((length) & ~(0x80000000))

#define ZIP_CHECK(length)                                                     \
         (((length) & 0x80000000) ? true : false)

/* plus lzo overhead to log_zip data size */
#define LOG_ZIP_BUF_SIZE(length) \
        ((length) + ((length) / 16) + 64 + 3 + sizeof(LOG_ZIP_SIZE_T))

#define LOG_ZIP_SIZE_T int

/*
 * Compressed(zipped) log structure
 */
typedef struct log_zip LOG_ZIP;

struct log_zip
{
  LOG_ZIP_SIZE_T data_length;	/* length of stored
				   (compressed/uncompressed)log_zip data */
  LOG_ZIP_SIZE_T buf_size;	/* size of log_zip data buffer */
  lzo_bytep log_data;		/* compressed/uncompressed log_zip data
				   (used as data buffer) */
  lzo_bytep wrkmem;		/* wokring memory for lzo function */
};

extern LOG_ZIP *log_zip_alloc (LOG_ZIP_SIZE_T size, bool is_zip);
extern void log_zip_free (LOG_ZIP * log_zip);

extern bool log_zip (LOG_ZIP * log_zip, LOG_ZIP_SIZE_T length,
		     const void *data);
extern bool log_unzip (LOG_ZIP * log_unzip, LOG_ZIP_SIZE_T length,
		       void *data);
extern bool log_diff (LOG_ZIP_SIZE_T undo_length, const void *undo_data,
		      LOG_ZIP_SIZE_T redo_length, void *redo_data);

#endif /* _LOG_COMPRESS_H_ */
