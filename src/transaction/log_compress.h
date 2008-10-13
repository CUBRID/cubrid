/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * zip_util.h - log compression functions
 *
 * Note: Using lzo2 library
 */

#ifndef _ZIP_UTIL_H_
#define _ZIP_UTIL_H_

#ident "$Id$"

#include "memory_manager_2.h"
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

#endif /* _ZIP_UTIL_H_ */
