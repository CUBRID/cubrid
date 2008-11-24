/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution. 
 *
 *   This program is free software; you can redistribute it and/or modify 
 *   it under the terms of the GNU General Public License as published by 
 *   the Free Software Foundation; version 2 of the License. 
 *
 *  This program is distributed in the hope that it will be useful, 
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of 
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the 
 *  GNU General Public License for more details. 
 *
 *  You should have received a copy of the GNU General Public License 
 *  along with this program; if not, write to the Free Software 
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA 
 *
 */


/*
 * cas_schema_info.h -
 */

#ifndef	_CAS_SCHEMA_INFO_H_
#define	_CAS_SCHEMA_INFO_H_

#ident "$Id$"

#include "dbtype.h"
#include "cas_net_buf.h"

#define SCH_STR_LEN	DB_MAX_IDENTIFIER_LENGTH

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

#endif /* _CAS_SCHEMA_INFO_H_ */
