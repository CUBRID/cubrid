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
 * cas_schema_info.h -
 */

#ifndef	_CAS_SCHEMA_INFO_H_
#define	_CAS_SCHEMA_INFO_H_

#ident "$Id$"

#if !defined(CAS_FOR_ORACLE) && !defined(CAS_FOR_MYSQL)
#include "dbtype.h"
#endif /* !CAS_FOR_ORACLE && !CAS_FOR_MYSQL */
#include "cas_net_buf.h"

#if defined(CAS_FOR_ORACLE) || defined(CAS_FOR_MYSQL)
#define SCH_STR_LEN	255
#define SCH_CLASS_REMARKS_STR_LEN       2048
#define SCH_REMARKS_STR_LEN     1024
#else
#define SCH_STR_LEN	DB_MAX_IDENTIFIER_LENGTH
#define SCH_CLASS_REMARKS_STR_LEN       DB_MAX_CLASS_COMMENT_LENGTH
#define SCH_REMARKS_STR_LEN             DB_MAX_COMMENT_LENGTH
#endif

typedef void (*T_SCHEMA_META_F) (T_NET_BUF *);

extern void schema_table_meta (T_NET_BUF * net_buf);
extern void schema_query_spec_meta (T_NET_BUF * net_buf);
extern void schema_attr_meta (T_NET_BUF * net_buf);
extern void schema_method_meta (T_NET_BUF * net_buf);
extern void schema_methodfile_meta (T_NET_BUF * net_buf);
extern void schema_superclasss_meta (T_NET_BUF * net_buf);
extern void schema_constraint_meta (T_NET_BUF * net_buf);
extern void schema_trigger_meta (T_NET_BUF * net_buf);
extern void schema_classpriv_meta (T_NET_BUF * net_buf);
extern void schema_attrpriv_meta (T_NET_BUF * net_buf);
extern void schema_directsuper_meta (T_NET_BUF * net_buf);
extern void schema_primarykey_meta (T_NET_BUF * net_buf);
extern void schema_fk_info_meta (T_NET_BUF * net_buf);

#endif /* _CAS_SCHEMA_INFO_H_ */
