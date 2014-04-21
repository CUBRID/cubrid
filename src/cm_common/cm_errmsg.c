/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
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
  snprintf (err_buf->err_msg, sizeof (err_buf->err_msg) - 1,
	    ER (err_buf->err_code));
}
