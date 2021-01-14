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
 * log_top_string.h -
 */

#ifndef	_LOG_TOP_STRING_H_
#define	_LOG_TOP_STRING_H_

#ident "$Id$"

typedef struct t_string T_STRING;
struct t_string
{
  char *data;
  int data_len;
  int alloc_size;
  int bind_len;
};


T_STRING *t_string_make (int init_size);
void t_string_clear (T_STRING * t_str);
int t_string_add (T_STRING * t_str, char *str, int str_len);
int t_bind_string_add (T_STRING * t_str, char *str, int str_len, int bind_len);
void t_string_free (T_STRING * t_str);
char *t_string_str (T_STRING * t_str);
int t_string_len (T_STRING * t_str);
int t_string_bind_len (T_STRING * t_str);

#endif /* _LOG_TOP_STRING_H_ */
