/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution. 
 *
 *   This program is free software; you can redistribute it and/or modify 
 *   it under the terms of the GNU General Public License as published by 
 *   the Free Software Foundation; version 2 of the License. 
 *
 *  This program is distributed in the hope that it will be useful, 
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of 
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the 
 *  GNU General Public License for more details. 
 *
 *  You should have received a copy of the GNU General Public License 
 *  along with this program; if not, write to the Free Software 
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA 
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

/*
 * Compressed(zipped) log structure
 */
typedef struct log_zip LOG_ZIP;

struct log_zip
{
  size_t data_length;		/* length of stored
				   (compressed/uncompressed)log_zip data */
  size_t buf_size;		/* size of log_zip data buffer */
  lzo_bytep log_data;		/* compressed/uncompressed log_zip data
				   (used as data buffer) */
  lzo_bytep wrkmem;		/* wokring memory for lzo function */
};

extern LOG_ZIP *log_zip_alloc (size_t size, bool is_zip);
extern void log_zip_free (LOG_ZIP * log_zip);

extern bool log_zip (LOG_ZIP * log_zip, size_t length, const void *data);
extern bool log_unzip (LOG_ZIP * log_unzip, size_t length, void *data);
extern bool log_diff (size_t undo_length, const void *undo_data,
		      size_t redo_length, void *redo_data);

#endif /* _LOG_COMPRESS_H_ */
