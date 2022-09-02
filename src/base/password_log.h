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
 * password_log.h -
 */

#ifndef	_PASSWORD_LOG_H_
#define	_PASSWORD_LOG_H_

#ident "$Id$"

#ifdef __cplusplus
extern "C"
{
#endif

#define INIT_PASSWORD_OFFSET(static_ptr, dynamic_ptr, size)  do { \
   (static_ptr)[0] = (size);                                      \
   (static_ptr)[1] = 2;                                           \
   (dynamic_ptr) = (static_ptr);                                  \
} while(0)

#define QUIT_PASSWORD_OFFSET(static_ptr, dynamic_ptr, size)  do { \
            if((dynamic_ptr) != (static_ptr))                     \
              {                                                   \
                 free((dynamic_ptr));                             \
                 (dynamic_ptr) = (static_ptr);                    \
              }                                                   \
            (static_ptr)[0] = (size);                             \
            (static_ptr)[1] = 2;                                  \
        } while(0)


  int add_offset_password (int *fixed_array, int **pwd_offset_ptr, int start, int end, bool is_add_comma);
  void fprintf_password (FILE * fp, char *query, int *pwd_offset_ptr, int (*cas_fprintf) (FILE *, const char *, ...));
  int snprint_password (char *msg, int size, char *query, int *pwd_offset_ptr);

#ifdef __cplusplus
}
#endif

#endif				/* _PASSWORD_LOG_H_ */
