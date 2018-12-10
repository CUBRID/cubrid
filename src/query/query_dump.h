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
 * query_dump.h - Query processor printer
 */

#ifndef _QUERY_DUMP_H_
#define _QUERY_DUMP_H_

#include "xasl.h"

extern bool qdump_print_xasl (XASL_NODE * xasl);
#if defined (SERVER_MODE)
extern void qdump_print_stats_json (XASL_NODE * xasl_p, json_t * parent);
extern void qdump_print_stats_text (FILE * fp, XASL_NODE * xasl_p, int indent);
#endif /* SERVER_MODE */
extern const char *qdump_operator_type_string (OPERATOR_TYPE optype);
extern const char *qdump_default_expression_string (DB_DEFAULT_EXPR_TYPE default_expr_type);

#endif /* _QUERY_DUMP_H_ */
