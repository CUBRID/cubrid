/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * util_func.h : miscellaneous utility functions interface
 *
 */

#ifndef _UTIL_FUNC_H_
#define _UTIL_FUNC_H_

#ident "$Id$"

#include <sys/types.h>
#include <math.h>

#define infinity()     (HUGE_VAL)

extern unsigned int hashpjw (const char *);

extern int util_compare_filepath (const char *file1, const char *file2);


typedef void (*SIG_HANDLER) (void);
extern void util_arm_signal_handlers (SIG_HANDLER DB_INT32_handler,
				      SIG_HANDLER quit_handler);
extern void util_disarm_signal_handlers (void);

#endif /* _UTIL_FUNC_H_ */
