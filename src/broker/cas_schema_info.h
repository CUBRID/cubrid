/*
 * Copyright 2008 Search Solution Corporation
 * Copyright 2016 CUBRID Corporation
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 */


/*
 * cas_schema_info.h -
 */

#ifndef	_CAS_SCHEMA_INFO_H_
#define	_CAS_SCHEMA_INFO_H_

#ident "$Id$"

#if !defined(CAS_FOR_ORACLE) && !defined(CAS_FOR_MYSQL)
#include "dbtype_def.h"
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
