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
 * condition_handler.h : condition handling interfaces
 *
 */

#ifndef _CONDITION_HANDLER_H_
#define _CONDITION_HANDLER_H_

#ident "$Id$"

#include <stdio.h>
#include <stdarg.h>

/* condition severity level */
typedef enum
{
  CO_ERROR_SEVERITY,
  CO_FATAL_SEVERITY,
  CO_WARNING_SEVERITY
} CO_SEVERITY;

/*
 * current condition message detail level
 * the detail level must start with 1 for message catalog set id
 */
typedef enum
{
  CO_DETAIL_USER = 1,
  CO_DETAIL_DBA,
  CO_DETAIL_DEBUG,
  CO_DETAIL_MAX
} CO_DETAIL;

extern int co_signal (int code, const char *format, ...);
#if defined(ENABLE_UNUSED_FUNCTION)
extern int co_code_module (int code);
extern int co_code_id (int code);
extern void co_report (FILE * file, CO_SEVERITY severity);
extern int co_put_detail (CO_DETAIL level);
extern const char *co_message (void);
#endif
extern int co_code (void);
extern void co_final (void);

#endif /* _CONDITION_HANDLER_H_ */
