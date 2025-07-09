/*
 *
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
 * hide_password.h -
 */

#ifndef	_HIDE_PASSWORD_H_
#define	_HIDE_PASSWORD_H_

#ident "$Id$"

#ifdef __cplusplus
extern "C"
{
#endif

  typedef enum
  {
    en_none_password = 0,
    en_user_password = 1,
    en_server_password = 2
  } EN_ADD_PWD_STRING;

  typedef struct
  {
#define DEFAULT_PWD_INFO_CNT  (5)
    int pwd_info[DEFAULT_PWD_INFO_CNT * 2];
    int *pwd_info_ptr;
    int size;
    int used;
  } HIDE_PWD_INFO;
  typedef HIDE_PWD_INFO *HIDE_PWD_INFO_PTR;

#define INIT_HIDE_PASSWORD_INFO(ptr)      do {     \
       (ptr)->size = (DEFAULT_PWD_INFO_CNT * 2);   \
       (ptr)->used = 0;                            \
       (ptr)->pwd_info_ptr = (ptr)->pwd_info;      \
} while(0)

#define QUIT_HIDE_PASSWORD_INFO(ptr)     do {                            \
    if((ptr)->pwd_info_ptr && ((ptr)->pwd_info_ptr != (ptr)->pwd_info))  \
      {                                                                  \
        free((ptr)->pwd_info_ptr);                                       \
      }                                                                  \
    INIT_HIDE_PASSWORD_INFO((ptr));                                      \
} while(0)

  void password_add_offset (HIDE_PWD_INFO_PTR hide_pwd_info_ptr, int start, int end, bool is_add_comma,
			    EN_ADD_PWD_STRING en_add_pwd_string);
  bool password_remake_offset_for_one_query (HIDE_PWD_INFO_PTR new_hide_pwd_info_ptr,
					     HIDE_PWD_INFO_PTR orig_hide_pwd_info_ptr, int start_pos, int end_pos);
  void password_fprintf (FILE * fp, char *query, HIDE_PWD_INFO_PTR hide_pwd_info_ptr,
			 int (*cas_fprintf) (FILE *, const char *, ...));
  int password_snprint (char *msg, int size, char *query, HIDE_PWD_INFO_PTR hide_pwd_info_ptr);

#ifdef __cplusplus
}
#endif

#endif				/* _HIDE_PASSWORD_H_ */
