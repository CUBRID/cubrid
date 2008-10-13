/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * qp_regu.c - Query process regulator
 *
 * Note: if you feel the need
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#if defined (_AIX)
#include <stdarg.h>
#endif
#include <string.h>

#include "error_manager.h"
#include "object_representation.h"
#include "qp_xdata.h"

#define DEFAULT_VAR "."

/*
 * regu_set_error_with_zero_args () -
 *   return: 
 *   err_type(in)       : error code
 * 									       
 * Note: Error reporting function for error messages with no arguments. 
 */
void
regu_set_error_with_zero_args (int err_type)
{
  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, err_type, 0);
  qp_Packing_er_code = err_type;
}

/*
 * regu_set_error_with_one_args () -
 *   return: 
 *   err_type(in)       : error code
 *   infor(in)  : message
 * 									       
 * Note: Error reporting function for error messages with one string argument.
 */
void
regu_set_error_with_one_args (int err_type, const char *infor)
{
  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, err_type, 1, infor);
  qp_Packing_er_code = err_type;
}

/*
 * regu_set_global_error () -
 *   return: 
 * 									       
 * Note: Set the client side query processor global error code.	       
 */
void
regu_set_global_error (void)
{
  qp_Packing_er_code = ER_REGU_SYSTEM;
}
