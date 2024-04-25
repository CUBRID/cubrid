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
 * load_object.h: simplified object definitions
 */

#ifndef _LOAD_OBJECT_H_
#define _LOAD_OBJECT_H_

#include <fcntl.h>

#include "dbtype_def.h"
#include "class_object.h"
#include <vector>

class print_output;

#define CHECK_PRINT_ERROR(print_fnc)            \
  do {                                          \
    if ((error = print_fnc) != NO_ERROR)        \
      goto exit_on_error;                       \
  } while (0)

#define CHECK_EXIT_ERROR(e)                     \
  do {                                          \
    if ((e) == NO_ERROR) {                      \
      if (((e) = er_errid()) == NO_ERROR)       \
        (e) = ER_GENERIC_ERROR;                 \
    }                                           \
  } while (0)

/*
 * DESC_OBJ
 *    This is a simplified description of an object that is used in cases
 *    where we do not want or need to build complete memory representations
 *    of an instance.  This was developed primarily to support the
 *    loader/import/export utility but could be used for other things as
 *    well.
 */

#if defined(SUPPORT_THREAD_UNLOAD)

#if defined (WINDOWS)
#include <io.h>

#define open       _open
#define close      _close
#define read       _read
#define write      _write
#define lseek      _lseeki64
#define O_RDONLY   _O_RDONLY
#define O_WRONLY   _O_WRONLY
#define O_APPEND   _O_APPEND
#define O_WRONLY   _O_WRONLY
#define O_CREAT    _O_CREAT
#define O_TRUNC    _O_TRUNC

#define OPEN_MODE_VALUE   (_S_IREAD | _S_IWRITE)
#else
#define OPEN_MODE_VALUE   0666
#endif

typedef struct dbval_buf
{
  char *buf;
  int buf_size;
} DBVAL_BUF;
#endif

typedef struct desc_obj
{
  MOP classop;
  SM_CLASS *class_;
  int updated_flag;
  int count;
  SM_ATTRIBUTE **atts;
  DB_VALUE *values;
#if defined(SUPPORT_THREAD_UNLOAD)
  DBVAL_BUF *dbval_bufs;
#endif
} DESC_OBJ;

typedef struct text_output
{
  /* pointer to the buffer */
  char *buffer;
  /* pointer to the next byte to buffer when writing */
  char *ptr;
  /* optimal I/O pagesize for device */
  int iosize;
  /* number of current buffered bytes */
  int count;
  /* output file */
#if defined(SUPPORT_THREAD_UNLOAD)
#define INVALID_FILE_NO  (-1)
  int fd;
#else
  FILE *fp;
#endif
} TEXT_OUTPUT;

extern int text_print_flush (TEXT_OUTPUT * tout);
extern int text_print (TEXT_OUTPUT * tout, const char *buf, int buflen, char const *fmt, ...);
#if defined(SUPPORT_THREAD_UNLOAD)
extern DESC_OBJ *make_desc_obj (SM_CLASS * class_, int alloc_size);
#else
extern DESC_OBJ *make_desc_obj (SM_CLASS * class_);
#endif

extern int desc_obj_to_disk (DESC_OBJ * obj, RECDES * record, bool * index_flag);
extern int desc_disk_to_obj (MOP classop, SM_CLASS * class_, RECDES * record, DESC_OBJ * obj);
extern void desc_free (DESC_OBJ * obj);

extern int desc_value_special_fprint (TEXT_OUTPUT * tout, DB_VALUE * value);
extern void desc_value_print (print_output & output_ctx, DB_VALUE * value);
extern int er_filter_fileset (FILE * ef);
extern int er_filter_errid (bool ignore_warning);

/* *INDENT-OFF* */
extern void get_ignored_errors (std::vector<int> &vec);
/* *INDENT-ON* */

#endif /* _LOAD_OBJECT_H_ */
