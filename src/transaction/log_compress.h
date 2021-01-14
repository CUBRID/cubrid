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
 * log_compress.h - log compression functions
 *
 * Note: Using lz4 library
 */

#ifndef _LOG_COMPRESS_H_
#define _LOG_COMPRESS_H_

#ident "$Id$"

#include "lz4.h"

#define MAKE_ZIP_LEN(length)                                                  \
         ((length) | 0x80000000)

#define GET_ZIP_LEN(length)                                                   \
         ((length) & ~(0x80000000))

#define ZIP_CHECK(length)                                                     \
         (((length) & 0x80000000) ? true : false)

/* plus lz4 overhead to log_zip data size */
#define LOG_ZIP_BUF_SIZE(length) \
        (LZ4_compressBound(length) + sizeof(LOG_ZIP_SIZE_T))

#define LOG_ZIP_SIZE_T int

/*
 * Compressed(zipped) log structure
 */
typedef struct log_zip LOG_ZIP;

struct log_zip
{
  LOG_ZIP_SIZE_T data_length;	/* length of stored (compressed/uncompressed)log_zip data */
  LOG_ZIP_SIZE_T buf_size;	/* size of log_zip data buffer */
  char *log_data;		/* compressed/uncompressed log_zip data (used as data buffer) */
};

extern LOG_ZIP *log_zip_alloc (LOG_ZIP_SIZE_T size);
extern void log_zip_free (LOG_ZIP * log_zip);

extern bool log_zip (LOG_ZIP * log_zip, LOG_ZIP_SIZE_T length, const void *data);
extern bool log_unzip (LOG_ZIP * log_unzip, LOG_ZIP_SIZE_T length, void *data);
extern bool log_diff (LOG_ZIP_SIZE_T undo_length, const void *undo_data, LOG_ZIP_SIZE_T redo_length, void *redo_data);

#endif /* _LOG_COMPRESS_H_ */
