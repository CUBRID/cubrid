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
 * cm_errmsg.h - 
 */

#ifndef _CM_ERRMSG_H_
#define _CM_ERRMSG_H_

#include "cm_stat.h"

#define ER(X) cm_errors[(X)-CM_MIN_ERROR]

extern const char *cm_errors[];	/* Error messages */

void cm_set_error (T_CM_ERROR * err_buf, int err_code);

void cm_err_buf_reset (T_CM_ERROR * err_buf);

#endif /* _CM_ERRMSG_H_ */
