/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * co.h : condition handling interfaces
 *
 */

#ifndef _CO_H_
#define _CO_H_

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
extern int co_code_module (int code);
extern int co_code_id (int code);
extern void co_report (FILE * file, CO_SEVERITY severity);
extern const char *co_message (void);
extern int co_code (void);
extern int co_put_detail (CO_DETAIL level);
extern void co_final (void);

#endif /* _CO_H_ */
