/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * error.h - Error code and message handling module
 *           This file contains exported stuffs from the error code and message
 *           handling modules. 
 */

#ifndef	_ERROR_H_
#define	_ERROR_H_

#ident "$Id$"

#include "error_def.i"

#define	UW_SET_ERROR_CODE(code, os_errno) \
		uw_set_error_code(__FILE__, __LINE__, (code), (os_errno))

extern void uw_set_error_code (const char *file_name, int line_no,
			       int error_code, int os_errno);
extern int uw_get_error_code (void);
extern int uw_get_os_error_code (void);
extern const char *uw_get_error_message (int error_code, int os_errno);
extern const char *uw_error_message (int error_code);
extern void uw_error_message_r (int error_code, char *err_msg);
extern void uw_os_err_msg (int err_code, char *err_msg);

#endif /* _ERROR_H_ */
