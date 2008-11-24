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
 * xasl_support.h - Query processor memory management module
 */

#ifndef _XASL_SUPPORT_H_
#define _XASL_SUPPORT_H_

#ident "$Id$"

#include "oid.h"
#include "storage_common.h"
#include "object_representation.h"
#include "object_domain.h"
#include "query_executor.h"
#include "heap_file.h"

/* for regu_machead () */
extern DB_VALUE *regu_dbval_alloc (void);
extern QPROC_DB_VALUE_LIST regu_dbvlist_alloc (void);
extern REGU_VARIABLE *regu_var_alloc (void);
extern REGU_VARIABLE_LIST regu_varlist_alloc (void);
extern REGU_VARLIST_LIST regu_varlist_list_alloc (void);
extern VAL_LIST *regu_vallist_alloc (void);
extern OUTPTR_LIST *regu_outlist_alloc (void);
extern PRED_EXPR *regu_pred_alloc (void);
extern INDX_INFO *regu_index_alloc (void);
extern QFILE_SORTED_LIST_ID *regu_srlistid_alloc (void);
extern HEAP_CACHE_ATTRINFO *regu_cache_attrinfo_alloc (void);
extern METHOD_SIG *regu_method_sig_alloc (void);
extern METHOD_SIG_LIST *regu_method_sig_list_alloc (void);
extern XASL_PARTITION_INFO *regu_partition_info_alloc (void);
extern XASL_PARTS_INFO *regu_parts_info_alloc (void);
extern SELUPD_LIST *regu_selupd_list_alloc (void);

/* for regu_machead_db () */
extern DB_VALUE *regu_dbval_db_alloc (void);
extern QFILE_LIST_ID *regu_listid_db_alloc (void);
extern SM_DOMAIN *regu_domain_db_alloc (void);
extern METHOD_SIG *regu_method_sig_db_alloc (void);
extern METHOD_SIG_LIST *regu_method_sig_list_db_alloc (void);

/* for regu_machead_array_ptr () */
extern DB_VALUE **regu_dbvalptr_array_alloc (int size);
extern REGU_VARIABLE **regu_varptr_array_alloc (int size);
extern XASL_PARTITION_INFO **regu_partition_array_alloc (int size);
extern XASL_PARTS_INFO **regu_parts_array_alloc (int size);

/* for regu_machead_array_db () */
extern int *regu_int_array_db_alloc (int size);

/* for regu_machead_array () */
extern KEY_RANGE *regu_keyrange_array_alloc (int size);
extern int *regu_int_array_alloc (int size);
extern OID *regu_oid_array_alloc (int size);
extern HFID *regu_hfid_array_alloc (int size);

extern int regu_dbval_init (DB_VALUE * ptr);
extern char *regu_string_alloc (int length);
extern char *regu_string_db_alloc (int length);
extern char *regu_string_ws_alloc (int length);
extern char *regu_strdup (const char *srptr, char *(*alloc) (int));
extern int regu_strcmp (const char *name1, const char *name2,
			int (*function_strcmp) (const char *, const char *));
extern int regu_dbval_type_init (DB_VALUE * ptr, DB_TYPE type);
extern QPROC_DB_VALUE_LIST regu_dbvallist_alloc (void);
extern ARITH_TYPE *regu_arith_alloc (void);
extern FUNCTION_TYPE *regu_func_alloc (void);
extern AGGREGATE_TYPE *regu_agg_alloc (void);
extern AGGREGATE_TYPE *regu_agg_grbynum_alloc (void);
extern XASL_NODE *regu_xasl_node_alloc (PROC_TYPE type);
extern ACCESS_SPEC_TYPE *regu_spec_alloc (TARGET_TYPE type);
extern void regu_index_init (INDX_INFO * ptr);
extern void regu_keyrange_init (KEY_RANGE * ptr);
extern SORT_LIST *regu_sort_list_alloc (void);
extern int regu_cp_listid (QFILE_LIST_ID * dst_list_id,
			   QFILE_LIST_ID * src_list_id);
extern void regu_free_listid (QFILE_LIST_ID * list_id);
extern void regu_free_domain (SM_DOMAIN * ptr);
extern SM_DOMAIN *regu_cp_domain (SM_DOMAIN * ptr);
extern void regu_int_init (int *ptr);
extern void regu_oid_init (OID * ptr);
extern void regu_hfid_init (HFID * ptr);
extern void regu_method_sig_init (METHOD_SIG * ptr);
extern void regu_free_method_sig (METHOD_SIG * method_sig);
extern void regu_method_sig_list_init (METHOD_SIG_LIST * ptr);
extern void regu_free_method_sig_list (METHOD_SIG_LIST * method_sig_list);

#endif /* _XASL_SUPPORT_H_ */

