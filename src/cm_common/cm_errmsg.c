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
 * cm_errmsg.c -
 */

#include "cm_portable.h"
#include "cm_errmsg.h"

#include <config.h>
#include <stdio.h>

const char *cm_errors[] = {
  "Unknown CM error",
  "Out of memory",
  "stat of database (%s) not found",
  "stat of broker (%s) not found",
  "command execute failed: %s",
  "environment variable (%s) not set",
  "ERROR: NULL pointer parameter",
  "file (%s) open failed: %s",
  "read database (%s) exec stat info error",
};

void
cm_err_buf_reset (T_CM_ERROR * err_buf)
{
  err_buf->err_code = 0;
  err_buf->err_msg[0] = '\0';
}

void
cm_set_error (T_CM_ERROR * err_buf, int err_code)
{
  err_buf->err_code = err_code;
  snprintf (err_buf->err_msg, sizeof (err_buf->err_msg) - 1, ER (err_buf->err_code));
}
