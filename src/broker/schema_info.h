/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * schema_info.h -
 */

#ifndef	_SCHEMA_INFO_H_
#define	_SCHEMA_INFO_H_

#ident "$Id$"

#include "dbtype.h"
#include "net_buf.h"

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

#endif /* _SCHEMA_INFO_H_ */
