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
 * load_object_threading.h: simplified object definitions
 */
#ifndef _LOAD_OBJECT_THREADING_H_
#define _LOAD_OBJECT_THREADING_H_

#include "load_object.h"


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

#define INVALID_FILE_NO  (-1)
  int fd;
  struct text_output *next;
  struct text_output *head;
} TEXT_OUTPUT;

extern int g_fd_handle;
void init_blk_queue (int size);
bool put_blk_queue (TEXT_OUTPUT * tout);
TEXT_OUTPUT *get_blk_queue ();

extern int text_print_flush (TEXT_OUTPUT * tout);
extern int text_print (TEXT_OUTPUT * tout, const char *buf, int buflen, char const *fmt, ...);

#if defined(SUPPORT_THREAD_UNLOAD)
extern int text_print_end (TEXT_OUTPUT * tout);

extern bool init_text_output_mem (int size);
extern void quit_text_output_mem ();
extern TEXT_OUTPUT *get_text_output_mem (TEXT_OUTPUT * head_ptr);
extern void release_text_output_mem (TEXT_OUTPUT * to);
#endif



extern int desc_value_special_fprint (TEXT_OUTPUT * tout, DB_VALUE * value);
extern void desc_value_print (print_output & output_ctx, DB_VALUE * value);

#endif // _LOAD_OBJECT_THREADING_H_
