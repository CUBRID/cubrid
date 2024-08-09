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
 * cm_utils.h -
 */

#ifndef _CM_UTILS_H_
#define _CM_UTILS_H_

#include "cm_dep.h"
#include "chartype.h"

#define ut_trim  trim

typedef enum
{
  TIME_STR_FMT_DATE = NV_ADD_DATE,
  TIME_STR_FMT_TIME = NV_ADD_TIME,
  TIME_STR_FMT_DATE_TIME = NV_ADD_DATE_TIME
} T_TIME_STR_FMT_TYPE;


char *time_to_str (time_t t, const char *fmt, char *buf, int type);

int uStringEqual (const char *str1, const char *str2);
int string_tokenize (char *str, char *tok[], int num_tok);

int make_version_info (char *cli_ver, int *major_ver, int *minor_ver);


#endif /* _CM_UTILS_H_ */
