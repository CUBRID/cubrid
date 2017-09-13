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
 * parser_support.h - Query processor memory management module
 */

#ifndef _PARSER_SUPPORT_H_
#define _PARSER_SUPPORT_H_

#ident "$Id$"

#if defined (SERVER_MODE)
#error Does not belong to server module
#endif /* SERVER_MODE */

#include "config.h"

extern int qp_Packing_er_code;

/* Memory Buffer Related Routines */
extern char *pt_alloc_packing_buf (int size);
extern void pt_final_packing_buf (void);
extern void pt_enter_packing_buf (void);
extern void pt_exit_packing_buf (void);

extern void regu_set_error_with_zero_args (int err_type);

#endif /* _PARSER_SUPPORT_H_ */
