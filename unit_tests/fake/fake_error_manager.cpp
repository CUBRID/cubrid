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

#include "error_manager.h"

void
er_set (int severity, const char *file_name, const int line_no, int err_id, int num_args, ...)
{
}

void
_er_log_debug (const char *file_name, const int line_no, const char *fmt, ...)
{
}

int
er_errid (void)
{
  return 0;
}

void
er_set_with_oserror (int severity, const char *file_name, const int line_no, int err_id, int num_args, ...) {}

void
er_clear (void) {}

void
er_print_callstack (const char *file_name, const int line_no, const char *fmt, ...) {}