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

//#define USE_LOW_IO_FUNC               // ctshim
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
#if defined(USE_LOW_IO_FUNC)
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


class CTEXT_OUTPUT
{
private:
  char *m_buffer;		/* pointer to the buffer */
  char *m_ptr;			/* pointer to the next byte to buffer when writing */
  int m_iosize;			/* optimal I/O pagesize for device */
  int m_count;			/* number of current buffered bytes */

  char *_buf_base;		/* Start of reserve area. */
  char *_buf_end;		/* End of reserve area. */

  int m_fd;			/* output file */

public:
    CTEXT_OUTPUT ()
  {
    m_buffer = NULL;
    m_fd = -1;
    m_iosize = 0;
    m_count = 0;
  }
   ~CTEXT_OUTPUT ()
  {
    if (m_buffer)
      {
	free (m_buffer);
      }
    close_file ();
  }
  bool create_file (const char *fname)
  {
    m_fd = open (fname, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    return (m_fd != -1);
  }
  void close_file ()
  {
    if (m_fd != -1)
      {
	close (m_fd);
      }
  }
};
#endif /* _LOAD_OBJECT_H_ */
