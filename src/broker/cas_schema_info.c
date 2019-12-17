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
 * cas_schema_info.c -
 */

#ident "$Id$"

#include <stdio.h>
#if defined(WINDOWS)
#include <winsock2.h>
#include <windows.h>
#endif

#include "cas.h"
#include "cas_schema_info.h"
#include "cas_common.h"
#include "cas_net_buf.h"
#include "language_support.h"

void
schema_table_meta (T_NET_BUF * net_buf)
{
  net_buf_cp_int (net_buf, 3, NULL);
  net_buf_column_info_set (net_buf, CCI_U_TYPE_STRING, 0, SCH_STR_LEN, CAS_SCHEMA_DEFAULT_CHARSET, "NAME");
  net_buf_column_info_set (net_buf, CCI_U_TYPE_SHORT, 0, 0, CAS_SCHEMA_DEFAULT_CHARSET, "TYPE");
  net_buf_column_info_set (net_buf, CCI_U_TYPE_STRING, 0, SCH_CLASS_REMARKS_STR_LEN, CAS_SCHEMA_DEFAULT_CHARSET,
			   "REMARKS");
}

void
schema_query_spec_meta (T_NET_BUF * net_buf)
{
  net_buf_cp_int (net_buf, 1, NULL);
  net_buf_column_info_set (net_buf, CCI_U_TYPE_STRING, 0, SCH_STR_LEN, CAS_SCHEMA_DEFAULT_CHARSET, "QUERY_SPEC");
}

void
schema_attr_meta (T_NET_BUF * net_buf)
{
  net_buf_cp_int (net_buf, 14, NULL);
  net_buf_column_info_set (net_buf, CCI_U_TYPE_STRING, 0, SCH_STR_LEN, CAS_SCHEMA_DEFAULT_CHARSET, "ATTR_NAME");
  net_buf_column_info_set (net_buf, CCI_U_TYPE_SHORT, 0, 0, CAS_SCHEMA_DEFAULT_CHARSET, "DOMAIN");
  net_buf_column_info_set (net_buf, CCI_U_TYPE_SHORT, 0, 0, CAS_SCHEMA_DEFAULT_CHARSET, "SCALE");
  net_buf_column_info_set (net_buf, CCI_U_TYPE_INT, 0, 0, CAS_SCHEMA_DEFAULT_CHARSET, "PRECISION");
  net_buf_column_info_set (net_buf, CCI_U_TYPE_SHORT, 0, 0, CAS_SCHEMA_DEFAULT_CHARSET, "INDEXED");
  net_buf_column_info_set (net_buf, CCI_U_TYPE_SHORT, 0, 0, CAS_SCHEMA_DEFAULT_CHARSET, "NON_NULL");
  net_buf_column_info_set (net_buf, CCI_U_TYPE_SHORT, 0, 0, CAS_SCHEMA_DEFAULT_CHARSET, "SHARED");
  net_buf_column_info_set (net_buf, CCI_U_TYPE_SHORT, 0, 0, CAS_SCHEMA_DEFAULT_CHARSET, "UNIQUE");
  net_buf_column_info_set (net_buf, CCI_U_TYPE_STRING, 0, 0, CAS_SCHEMA_DEFAULT_CHARSET, "DEFAULT");
  net_buf_column_info_set (net_buf, CCI_U_TYPE_INT, 0, 0, CAS_SCHEMA_DEFAULT_CHARSET, "ATTR_ORDER");
  net_buf_column_info_set (net_buf, CCI_U_TYPE_STRING, 0, SCH_STR_LEN, CAS_SCHEMA_DEFAULT_CHARSET, "CLASS_NAME");
  net_buf_column_info_set (net_buf, CCI_U_TYPE_STRING, 0, SCH_STR_LEN, CAS_SCHEMA_DEFAULT_CHARSET, "SOURCE_CLASS");
  net_buf_column_info_set (net_buf, CCI_U_TYPE_SHORT, 0, 0, CAS_SCHEMA_DEFAULT_CHARSET, "IS_KEY");
  net_buf_column_info_set (net_buf, CCI_U_TYPE_STRING, 0, SCH_REMARKS_STR_LEN, CAS_SCHEMA_DEFAULT_CHARSET, "REMARKS");
}

void
schema_method_meta (T_NET_BUF * net_buf)
{
  net_buf_cp_int (net_buf, 3, NULL);
  net_buf_column_info_set (net_buf, CCI_U_TYPE_STRING, 0, SCH_STR_LEN, CAS_SCHEMA_DEFAULT_CHARSET, "NAME");
  net_buf_column_info_set (net_buf, CCI_U_TYPE_SHORT, 0, 0, CAS_SCHEMA_DEFAULT_CHARSET, "RET_DOMAIN");
  net_buf_column_info_set (net_buf, CCI_U_TYPE_STRING, 0, SCH_STR_LEN, CAS_SCHEMA_DEFAULT_CHARSET, "ARG_DOMAIN");
}

void
schema_methodfile_meta (T_NET_BUF * net_buf)
{
  net_buf_cp_int (net_buf, 1, NULL);
  net_buf_column_info_set (net_buf, CCI_U_TYPE_STRING, 0, SCH_STR_LEN, CAS_SCHEMA_DEFAULT_CHARSET, "METHOD_FILE");
}

void
schema_superclasss_meta (T_NET_BUF * net_buf)
{
  net_buf_cp_int (net_buf, 2, NULL);
  net_buf_column_info_set (net_buf, CCI_U_TYPE_STRING, 0, SCH_STR_LEN, CAS_SCHEMA_DEFAULT_CHARSET, "CLASS_NAME");
  net_buf_column_info_set (net_buf, CCI_U_TYPE_SHORT, 0, 0, CAS_SCHEMA_DEFAULT_CHARSET, "TYPE");
}

void
schema_constraint_meta (T_NET_BUF * net_buf)
{
  net_buf_cp_int (net_buf, 8, NULL);
  net_buf_column_info_set (net_buf, CCI_U_TYPE_SHORT, 0, 0, CAS_SCHEMA_DEFAULT_CHARSET, "TYPE");
  net_buf_column_info_set (net_buf, CCI_U_TYPE_STRING, 0, SCH_STR_LEN, CAS_SCHEMA_DEFAULT_CHARSET, "NAME");
  net_buf_column_info_set (net_buf, CCI_U_TYPE_STRING, 0, SCH_STR_LEN, CAS_SCHEMA_DEFAULT_CHARSET, "ATTR_NAME");
  net_buf_column_info_set (net_buf, CCI_U_TYPE_INT, 0, 0, CAS_SCHEMA_DEFAULT_CHARSET, "NUM_PAGES");
  net_buf_column_info_set (net_buf, CCI_U_TYPE_INT, 0, 0, CAS_SCHEMA_DEFAULT_CHARSET, "NUM_KEYS");
  net_buf_column_info_set (net_buf, CCI_U_TYPE_SHORT, 0, 0, CAS_SCHEMA_DEFAULT_CHARSET, "PRIMARY_KEY");
  net_buf_column_info_set (net_buf, CCI_U_TYPE_SHORT, 0, 0, CAS_SCHEMA_DEFAULT_CHARSET, "KEY_ORDER");
  net_buf_column_info_set (net_buf, CCI_U_TYPE_STRING, 0, 0, CAS_SCHEMA_DEFAULT_CHARSET, "ASC_DESC");
}

void
schema_trigger_meta (T_NET_BUF * net_buf)
{
  net_buf_cp_int (net_buf, 11, NULL);
  net_buf_column_info_set (net_buf, CCI_U_TYPE_STRING, 0, SCH_STR_LEN, CAS_SCHEMA_DEFAULT_CHARSET, "NAME");
  net_buf_column_info_set (net_buf, CCI_U_TYPE_STRING, 0, SCH_STR_LEN, CAS_SCHEMA_DEFAULT_CHARSET, "STATUS");
  net_buf_column_info_set (net_buf, CCI_U_TYPE_STRING, 0, SCH_STR_LEN, CAS_SCHEMA_DEFAULT_CHARSET, "EVENT");
  net_buf_column_info_set (net_buf, CCI_U_TYPE_STRING, 0, SCH_STR_LEN, CAS_SCHEMA_DEFAULT_CHARSET, "TARGET_CLASS");
  net_buf_column_info_set (net_buf, CCI_U_TYPE_STRING, 0, SCH_STR_LEN, CAS_SCHEMA_DEFAULT_CHARSET, "TARGET_ATTR");
  net_buf_column_info_set (net_buf, CCI_U_TYPE_STRING, 0, SCH_STR_LEN, CAS_SCHEMA_DEFAULT_CHARSET, "ACTION_TIME");
  net_buf_column_info_set (net_buf, CCI_U_TYPE_STRING, 0, SCH_STR_LEN, CAS_SCHEMA_DEFAULT_CHARSET, "ACTION");
  net_buf_column_info_set (net_buf, CCI_U_TYPE_FLOAT, 0, 0, CAS_SCHEMA_DEFAULT_CHARSET, "PRIORITY");
  net_buf_column_info_set (net_buf, CCI_U_TYPE_STRING, 0, SCH_STR_LEN, CAS_SCHEMA_DEFAULT_CHARSET, "CONDITION_TIME");
  net_buf_column_info_set (net_buf, CCI_U_TYPE_STRING, 0, SCH_STR_LEN, CAS_SCHEMA_DEFAULT_CHARSET, "CONDITION");
  net_buf_column_info_set (net_buf, CCI_U_TYPE_STRING, 0, SCH_REMARKS_STR_LEN, CAS_SCHEMA_DEFAULT_CHARSET, "REMARKS");
}

void
schema_classpriv_meta (T_NET_BUF * net_buf)
{
  net_buf_cp_int (net_buf, 3, NULL);
  net_buf_column_info_set (net_buf, CCI_U_TYPE_STRING, 0, SCH_STR_LEN, CAS_SCHEMA_DEFAULT_CHARSET, "CLASS_NAME");
  net_buf_column_info_set (net_buf, CCI_U_TYPE_STRING, 0, 10, CAS_SCHEMA_DEFAULT_CHARSET, "PRIVILEGE");
  net_buf_column_info_set (net_buf, CCI_U_TYPE_STRING, 0, 5, CAS_SCHEMA_DEFAULT_CHARSET, "GRANTABLE");
}

void
schema_attrpriv_meta (T_NET_BUF * net_buf)
{
  net_buf_cp_int (net_buf, 3, NULL);
  net_buf_column_info_set (net_buf, CCI_U_TYPE_STRING, 0, SCH_STR_LEN, CAS_SCHEMA_DEFAULT_CHARSET, "ATTR_NAME");
  net_buf_column_info_set (net_buf, CCI_U_TYPE_STRING, 0, 10, CAS_SCHEMA_DEFAULT_CHARSET, "PRIVILEGE");
  net_buf_column_info_set (net_buf, CCI_U_TYPE_STRING, 0, 5, CAS_SCHEMA_DEFAULT_CHARSET, "GRANTABLE");
}

void
schema_directsuper_meta (T_NET_BUF * net_buf)
{
  net_buf_cp_int (net_buf, 2, NULL);
  net_buf_column_info_set (net_buf, CCI_U_TYPE_STRING, 0, SCH_STR_LEN, CAS_SCHEMA_DEFAULT_CHARSET, "CLASS_NAME");
  net_buf_column_info_set (net_buf, CCI_U_TYPE_STRING, 0, SCH_STR_LEN, CAS_SCHEMA_DEFAULT_CHARSET, "SUPER_CLASS_NAME");
}

void
schema_primarykey_meta (T_NET_BUF * net_buf)
{
  net_buf_cp_int (net_buf, 4, NULL);
  net_buf_column_info_set (net_buf, CCI_U_TYPE_STRING, 0, SCH_STR_LEN, CAS_SCHEMA_DEFAULT_CHARSET, "CLASS_NAME");
  net_buf_column_info_set (net_buf, CCI_U_TYPE_STRING, 0, SCH_STR_LEN, CAS_SCHEMA_DEFAULT_CHARSET, "ATTR_NAME");
  net_buf_column_info_set (net_buf, CCI_U_TYPE_INT, 0, 0, CAS_SCHEMA_DEFAULT_CHARSET, "KEY_SEQ");
  net_buf_column_info_set (net_buf, CCI_U_TYPE_STRING, 0, SCH_STR_LEN, CAS_SCHEMA_DEFAULT_CHARSET, "KEY_NAME");
}

void
schema_fk_info_meta (T_NET_BUF * net_buf)
{
  net_buf_cp_int (net_buf, 9, NULL);
  net_buf_column_info_set (net_buf, CCI_U_TYPE_STRING, 0, SCH_STR_LEN, CAS_SCHEMA_DEFAULT_CHARSET, "PKTABLE_NAME");
  net_buf_column_info_set (net_buf, CCI_U_TYPE_STRING, 0, SCH_STR_LEN, CAS_SCHEMA_DEFAULT_CHARSET, "PKCOLUMN_NAME");
  net_buf_column_info_set (net_buf, CCI_U_TYPE_STRING, 0, SCH_STR_LEN, CAS_SCHEMA_DEFAULT_CHARSET, "FKTABLE_NAME");
  net_buf_column_info_set (net_buf, CCI_U_TYPE_STRING, 0, SCH_STR_LEN, CAS_SCHEMA_DEFAULT_CHARSET, "FKCOLUMN_NAME");
  net_buf_column_info_set (net_buf, CCI_U_TYPE_SHORT, 0, 0, CAS_SCHEMA_DEFAULT_CHARSET, "KEY_SEQ");
  net_buf_column_info_set (net_buf, CCI_U_TYPE_SHORT, 0, 0, CAS_SCHEMA_DEFAULT_CHARSET, "UPDATE_RULE");
  net_buf_column_info_set (net_buf, CCI_U_TYPE_SHORT, 0, 0, CAS_SCHEMA_DEFAULT_CHARSET, "DELETE_RULE");
  net_buf_column_info_set (net_buf, CCI_U_TYPE_STRING, 0, SCH_STR_LEN, CAS_SCHEMA_DEFAULT_CHARSET, "FK_NAME");
  net_buf_column_info_set (net_buf, CCI_U_TYPE_STRING, 0, SCH_STR_LEN, CAS_SCHEMA_DEFAULT_CHARSET, "PK_NAME");
}
