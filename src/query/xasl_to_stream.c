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
 * xasl_to_stream.c - XASL tree storer
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "error_manager.h"
#include "query_executor.h"
#include "server_interface.h"
#include "class_object.h"
#include "object_primitive.h"
#include "work_space.h"
#include "memory_alloc.h"
#include "heap_file.h"


/* memory alignment unit - to align stored XASL tree nodes */
#define	ALIGN_UNIT	sizeof(double)
#define	ALIGN_MASK	(ALIGN_UNIT - 1)
#define MAKE_ALIGN(x)	(((x) & ~ALIGN_MASK) + \
			 (((x) & ALIGN_MASK) ? ALIGN_UNIT : 0 ))

/* to limit size of XASL trees */
#define	OFFSETS_PER_BLOCK	4096
#define	START_PTR_PER_BLOCK	15
#define MAX_PTR_BLOCKS		256
#define PTR_BLOCK(ptr)  (((UINTPTR) ptr) / __WORDSIZE) % MAX_PTR_BLOCKS

/*
 * the linear byte stream for store the given XASL tree is allocated
 * and expanded dynamically on demand by the following amount of bytes
 */
#define	STREAM_EXPANSION_UNIT	(OFFSETS_PER_BLOCK * sizeof(int))

#define    BYTE_SIZE        OR_INT_SIZE
#define    LONG_SIZE        OR_INT_SIZE
#define    PTR_SIZE         OR_INT_SIZE
#define    pack_char        or_pack_int
#define    pack_long        or_pack_int

/* structure of a visited pointer constant */
typedef struct visited_ptr VISITED_PTR;
struct visited_ptr
{
  const void *ptr;		/* a pointer constant */
  int offset;			/* offset where the node pointed by 'ptr'
				   is stored */
};

/* linear byte stream to store the given XASL tree */
static char *xts_Stream_buffer = NULL;	/* pointer to the stream */
static int xts_Stream_size = 0;	/* # of bytes allocated */
static int xts_Free_offset_in_stream = 0;

/* blocks of visited pointer constants */
static VISITED_PTR *xts_Ptr_blocks[MAX_PTR_BLOCKS] = { 0 };

/* low-water-mark of visited pointers */
static int xts_Ptr_lwm[MAX_PTR_BLOCKS] = { 0 };
static int xts_Ptr_max[MAX_PTR_BLOCKS] = { 0 };

/* error code specific to this file */
static int xts_Xasl_errcode = NO_ERROR;

static int xts_save_aggregate_type (const AGGREGATE_TYPE * aggregate);
static int xts_save_function_type (const FUNCTION_TYPE * function);
static int xts_save_analytic_type (const ANALYTIC_TYPE * analytic);
static int xts_save_analytic_eval_type (const ANALYTIC_EVAL_TYPE * analytic);
static int xts_save_srlist_id (const QFILE_SORTED_LIST_ID * sort_list_id);
static int xts_save_list_id (const QFILE_LIST_ID * list_id);
static int xts_save_arith_type (const ARITH_TYPE * arithmetic);
static int xts_save_indx_info (const INDX_INFO * indx_info);
static int xts_save_outptr_list (const OUTPTR_LIST * outptr_list);
static int xts_save_selupd_list (const SELUPD_LIST * selupd_list);
static int xts_save_pred_expr (const PRED_EXPR * ptr);
static int xts_save_regu_variable (const REGU_VARIABLE * ptr);
static int xts_save_regu_variable_list (const REGU_VARIABLE_LIST ptr);
static int xts_save_regu_varlist_list (const REGU_VARLIST_LIST ptr);
static int xts_save_sort_list (const SORT_LIST * ptr);
static int xts_save_string (const char *str);
static int xts_save_val_list (const VAL_LIST * ptr);
static int xts_save_db_value (const DB_VALUE * ptr);
static int xts_save_xasl_node (const XASL_NODE * ptr);
static int xts_save_filter_pred_node (const PRED_EXPR_WITH_CONTEXT * pred);
static int xts_save_func_pred (const FUNC_PRED * ptr);
static int xts_save_cache_attrinfo (const HEAP_CACHE_ATTRINFO * ptr);
#if 0
/* there are currently no pointers to these type of structures in xasl
 * so there is no need to have a separate restore function.
 */
static int xts_save_merge_list_info (const MERGELIST_PROC_NODE * ptr);
static int xts_save_ls_merge_info (const QFILE_LIST_MERGE_INFO * ptr);
static int xts_save_update_info (const UPDATE_PROC_NODE * ptr);
static int xts_save_delete_info (const DELETE_PROC_NODE * ptr);
static int xts_save_insert_info (const INSERT_PROC_NODE * ptr);
#endif
static int xts_save_db_value_array (DB_VALUE ** ptr, int size);
static int xts_save_int_array (int *ptr, int size);
static int xts_save_hfid_array (HFID * ptr, int size);
static int xts_save_oid_array (OID * ptr, int size);
static int xts_save_method_sig_list (const METHOD_SIG_LIST * ptr);
static int xts_save_method_sig (const METHOD_SIG * ptr, int size);
static int xts_save_key_range_array (const KEY_RANGE * ptr, int size);
static int xts_save_upddel_class_info_array (const UPDDEL_CLASS_INFO *
					     classes, int nelements);
static int xts_save_update_assignment_array (const UPDATE_ASSIGNMENT *
					     assigns, int nelements);
static int xts_save_odku_info (const ODKU_INFO * odku_info);

static char *xts_process_xasl_node (char *ptr, const XASL_NODE * xasl);
static char *xts_process_xasl_header (char *ptr,
				      const XASL_NODE_HEADER header);
static char *xts_process_filter_pred_node (char *ptr,
					   const PRED_EXPR_WITH_CONTEXT *
					   pred);
static char *xts_process_func_pred (char *ptr, const FUNC_PRED * xasl);
static char *xts_process_cache_attrinfo (char *ptr);
static char *xts_process_union_proc (char *ptr,
				     const UNION_PROC_NODE * union_proc);
static char *xts_process_fetch_proc (char *ptr,
				     const FETCH_PROC_NODE *
				     obj_set_fetch_proc);
static char *xts_process_buildlist_proc (char *ptr,
					 const BUILDLIST_PROC_NODE *
					 build_list_proc);
static char *xts_process_buildvalue_proc (char *ptr,
					  const BUILDVALUE_PROC_NODE *
					  build_value_proc);
static char *xts_process_mergelist_proc (char *ptr,
					 const MERGELIST_PROC_NODE *
					 merge_list_info);
static char *xts_process_ls_merge_info (char *ptr,
					const QFILE_LIST_MERGE_INFO *
					qfile_list_merge_info);
static char *xts_save_upddel_class_info (char *ptr,
					 const UPDDEL_CLASS_INFO * upd_cls);
static char *xts_save_update_assignment (char *ptr,
					 const UPDATE_ASSIGNMENT * assign);
static char *xts_process_update_proc (char *ptr,
				      const UPDATE_PROC_NODE * update_info);
static char *xts_process_delete_proc (char *ptr,
				      const DELETE_PROC_NODE * delete_proc);
static char *xts_process_insert_proc (char *ptr,
				      const INSERT_PROC_NODE * insert_proc);
static char *xts_process_merge_proc (char *ptr,
				     const MERGE_PROC_NODE * merge_info);
static char *xts_process_outptr_list (char *ptr,
				      const OUTPTR_LIST * outptr_list);
static char *xts_process_selupd_list (char *ptr,
				      const SELUPD_LIST * selupd_list);
static char *xts_process_pred_expr (char *ptr, const PRED_EXPR * pred_expr);
static char *xts_process_pred (char *ptr, const PRED * pred);
static char *xts_process_eval_term (char *ptr, const EVAL_TERM * eval_term);
static char *xts_process_comp_eval_term (char *ptr,
					 const COMP_EVAL_TERM *
					 comp_eval_term);
static char *xts_process_alsm_eval_term (char *ptr,
					 const ALSM_EVAL_TERM *
					 alsm_eval_term);
static char *xts_process_like_eval_term (char *ptr,
					 const LIKE_EVAL_TERM *
					 like_eval_term);
static char *xts_process_rlike_eval_term (char *ptr,
					  const RLIKE_EVAL_TERM *
					  rlike_eval_term);
static char *xts_process_access_spec_type (char *ptr,
					   const ACCESS_SPEC_TYPE *
					   access_spec);
static char *xts_process_indx_info (char *ptr, const INDX_INFO * indx_info);
static char *xts_process_indx_id (char *ptr, const INDX_ID * indx_id);
static char *xts_process_key_info (char *ptr, const KEY_INFO * key_info);
static char *xts_process_cls_spec_type (char *ptr,
					const CLS_SPEC_TYPE * cls_spec);
static char *xts_process_list_spec_type (char *ptr,
					 const LIST_SPEC_TYPE * list_spec);
static char *xts_process_showstmt_spec_type (char *ptr,
					     const SHOWSTMT_SPEC_TYPE *
					     list_spec);
static char *xts_process_set_spec_type (char *ptr,
					const SET_SPEC_TYPE * set_spec);
static char *xts_process_method_spec_type (char *ptr,
					   const METHOD_SPEC_TYPE *
					   method_spec);
static char *xts_process_rlist_spec_type (char *ptr,
					  const LIST_SPEC_TYPE * list_spec);
static char *xts_process_list_id (char *ptr, const QFILE_LIST_ID * list_id);
static char *xts_process_val_list (char *ptr, const VAL_LIST * val_list);
static char *xts_process_regu_variable (char *ptr,
					const REGU_VARIABLE * regu_var);
static char *xts_pack_regu_variable_value (char *ptr,
					   const REGU_VARIABLE * regu_var);
static char *xts_process_attr_descr (char *ptr,
				     const ATTR_DESCR * attr_descr);
static char *xts_process_pos_descr (char *ptr,
				    const QFILE_TUPLE_VALUE_POSITION *
				    position_descr);
static char *xts_process_db_value (char *ptr, const DB_VALUE * value);
static char *xts_process_arith_type (char *ptr, const ARITH_TYPE * arith);
static char *xts_process_aggregate_type (char *ptr,
					 const AGGREGATE_TYPE * aggregate);
static char *xts_process_analytic_type (char *ptr,
					const ANALYTIC_TYPE * analytic);
static char *xts_process_analytic_eval_type (char *ptr,
					     const ANALYTIC_EVAL_TYPE *
					     analytic);
static char *xts_process_function_type (char *ptr,
					const FUNCTION_TYPE * function);
static char *xts_process_srlist_id (char *ptr,
				    const QFILE_SORTED_LIST_ID *
				    sort_list_id);
static char *xts_process_sort_list (char *ptr, const SORT_LIST * sort_list);
static char *xts_process_method_sig_list (char *ptr,
					  const METHOD_SIG_LIST *
					  method_sig_list);
static char *xts_process_method_sig (char *ptr,
				     const METHOD_SIG * method_sig, int size);
static char *xts_process_connectby_proc (char *ptr,
					 const CONNECTBY_PROC_NODE *
					 connectby_proc);
static char *xts_process_regu_value_list (char *ptr,
					  const REGU_VALUE_LIST *
					  regu_value_list);

static int xts_sizeof_xasl_node (const XASL_NODE * ptr);
static int xts_sizeof_filter_pred_node (const PRED_EXPR_WITH_CONTEXT * ptr);
static int xts_sizeof_func_pred (const FUNC_PRED * ptr);
static int xts_sizeof_cache_attrinfo (const HEAP_CACHE_ATTRINFO * ptr);
static int xts_sizeof_union_proc (const UNION_PROC_NODE * ptr);
static int xts_sizeof_fetch_proc (const FETCH_PROC_NODE * ptr);
static int xts_sizeof_buildlist_proc (const BUILDLIST_PROC_NODE * ptr);
static int xts_sizeof_buildvalue_proc (const BUILDVALUE_PROC_NODE * ptr);
static int xts_sizeof_mergelist_proc (const MERGELIST_PROC_NODE * ptr);
static int xts_sizeof_ls_merge_info (const QFILE_LIST_MERGE_INFO * ptr);
static int xts_sizeof_upddel_class_info (const UPDDEL_CLASS_INFO * upd_cls);
static int xts_sizeof_update_assignment (const UPDATE_ASSIGNMENT * assign);
static int xts_sizeof_odku_info (const ODKU_INFO * odku_info);
static int xts_sizeof_update_proc (const UPDATE_PROC_NODE * ptr);
static int xts_sizeof_delete_proc (const DELETE_PROC_NODE * ptr);
static int xts_sizeof_insert_proc (const INSERT_PROC_NODE * ptr);
static int xts_sizeof_merge_proc (const MERGE_PROC_NODE * ptr);
static int xts_sizeof_outptr_list (const OUTPTR_LIST * ptr);
static int xts_sizeof_selupd_list (const SELUPD_LIST * ptr);
static int xts_sizeof_pred_expr (const PRED_EXPR * ptr);
static int xts_sizeof_pred (const PRED * ptr);
static int xts_sizeof_eval_term (const EVAL_TERM * ptr);
static int xts_sizeof_comp_eval_term (const COMP_EVAL_TERM * ptr);
static int xts_sizeof_alsm_eval_term (const ALSM_EVAL_TERM * ptr);
static int xts_sizeof_like_eval_term (const LIKE_EVAL_TERM * ptr);
static int xts_sizeof_rlike_eval_term (const RLIKE_EVAL_TERM * ptr);
static int xts_sizeof_access_spec_type (const ACCESS_SPEC_TYPE * ptr);
static int xts_sizeof_indx_info (const INDX_INFO * ptr);
static int xts_sizeof_indx_id (const INDX_ID * ptr);
static int xts_sizeof_key_info (const KEY_INFO * ptr);
static int xts_sizeof_cls_spec_type (const CLS_SPEC_TYPE * ptr);
static int xts_sizeof_list_spec_type (const LIST_SPEC_TYPE * ptr);
static int xts_sizeof_showstmt_spec_type (const SHOWSTMT_SPEC_TYPE * ptr);
static int xts_sizeof_set_spec_type (const SET_SPEC_TYPE * ptr);
static int xts_sizeof_method_spec_type (const METHOD_SPEC_TYPE * ptr);
static int xts_sizeof_list_id (const QFILE_LIST_ID * ptr);
static int xts_sizeof_val_list (const VAL_LIST * ptr);
static int xts_sizeof_regu_variable (const REGU_VARIABLE * ptr);
static int xts_get_regu_variable_value_size (const REGU_VARIABLE * ptr);
static int xts_sizeof_attr_descr (const ATTR_DESCR * ptr);
static int xts_sizeof_pos_descr (const QFILE_TUPLE_VALUE_POSITION * ptr);
static int xts_sizeof_db_value (const DB_VALUE * ptr);
static int xts_sizeof_arith_type (const ARITH_TYPE * ptr);
static int xts_sizeof_aggregate_type (const AGGREGATE_TYPE * ptr);
static int xts_sizeof_function_type (const FUNCTION_TYPE * ptr);
static int xts_sizeof_analytic_type (const ANALYTIC_TYPE * ptr);
static int xts_sizeof_analytic_eval_type (const ANALYTIC_EVAL_TYPE * ptr);
static int xts_sizeof_srlist_id (const QFILE_SORTED_LIST_ID * ptr);
static int xts_sizeof_sort_list (const SORT_LIST * ptr);
static int xts_sizeof_method_sig_list (const METHOD_SIG_LIST * ptr);
static int xts_sizeof_method_sig (const METHOD_SIG * ptr);
static int xts_sizeof_connectby_proc (const CONNECTBY_PROC_NODE * ptr);
static int xts_sizeof_regu_value_list (const REGU_VALUE_LIST *
				       regu_value_list);

static int xts_mark_ptr_visited (const void *ptr, int offset);
static int xts_get_offset_visited_ptr (const void *ptr);
static void xts_free_visited_ptrs (void);
static int xts_reserve_location_in_stream (int size);
static int xts_sizeof_regu_variable_list (const REGU_VARIABLE_LIST
					  regu_var_list);
static char *xts_process_regu_variable_list (char *ptr,
					     const REGU_VARIABLE_LIST
					     regu_var_list);

/*
 * xts_map_xasl_to_stream () -
 *   return: if successful, return 0, otherwise non-zero error code
 *   xasl_tree(in)      : pointer to root of XASL tree
 *   stream (out)       : xasl stream & size
 *
 * Note: map the XASL tree into linear byte stream of disk
 * representation. On successful return, `*xasl_stream'
 * will have the address of memory containing the linearly
 * mapped xasl stream, `*xasl_stream_size' will have the
 * # of bytes allocated for the stream
 *
 * Note: the caller should be responsible for free the memory of
 * xasl_stream. the free function should be free_and_init().
 */
int
xts_map_xasl_to_stream (const XASL_NODE * xasl_tree, XASL_STREAM * stream)
{
  int offset, org_offset;
  int header_size, body_size;
  char *p;
  int i;

  if (!xasl_tree || !stream)
    {
      return ER_QPROC_INVALID_XASLNODE;
    }

  xts_Xasl_errcode = NO_ERROR;

  /* reserve space for new XASL format */
  header_size = sizeof (int)	/* xasl->dbval_cnt */
    + sizeof (OID)		/* xasl->creator_oid */
    + sizeof (int)		/* xasl->n_oid_list */
    + sizeof (OID) * xasl_tree->n_oid_list	/* xasl->class_oid_list */
    + sizeof (int) * xasl_tree->n_oid_list;	/* xasl->tcard_list */

  offset = sizeof (int)		/* [size of header data] */
    + header_size		/* [header data] */
    + sizeof (int);		/* [size of body data] */

  org_offset = offset;
  offset = MAKE_ALIGN (offset);

  xts_reserve_location_in_stream (offset);

#if !defined(NDEBUG)
  /* suppress valgrind UMW error */
  if (offset > org_offset)
    {
      memset (xts_Stream_buffer + org_offset, 0, offset - org_offset);
    }
#endif

  /* save XASL tree into body data of the stream buffer */
  if (xts_save_xasl_node (xasl_tree) == ER_FAILED)
    {
      if (xts_Stream_buffer)
	{
	  free_and_init (xts_Stream_buffer);
	}
      goto end;
    }

  /* make header size and data of new XASL format */
  p = or_pack_int (xts_Stream_buffer, header_size);
  p = or_pack_int (p, xasl_tree->dbval_cnt);
  p = or_pack_oid (p, (OID *) (&xasl_tree->creator_oid));
  p = or_pack_int (p, xasl_tree->n_oid_list);
  for (i = 0; i < xasl_tree->n_oid_list; i++)
    {
      p = or_pack_oid (p, &xasl_tree->class_oid_list[i]);
    }
  for (i = 0; i < xasl_tree->n_oid_list; i++)
    {
      p = or_pack_int (p, xasl_tree->tcard_list[i]);
    }

  /* set body size of new XASL format */
  body_size = xts_Free_offset_in_stream - offset;
  p = or_pack_int (p, body_size);

  /* set result */
  stream->xasl_stream = xts_Stream_buffer;
  stream->xasl_stream_size = xts_Free_offset_in_stream;

end:
  /* free all memories */
  xts_free_visited_ptrs ();

  xts_Stream_buffer = NULL;
  xts_Stream_size = 0;
  xts_Free_offset_in_stream = 0;

  return xts_Xasl_errcode;
}

/*
 * xts_map_filter_pred_to_stream () -
 *   return: if successful, return 0, otherwise non-zero error code
 *   pred(in)      : pointer to root of predicate tree
 *   pred_stream(out)   : pointer to predicate stream
 *   pred_stream_size(out): # of bytes in predicate stream
 */
int
xts_map_filter_pred_to_stream (const PRED_EXPR_WITH_CONTEXT * pred,
			       char **pred_stream, int *pred_stream_size)
{
  int offset;
  int header_size, body_size;
  char *p = NULL;

  if (!pred || !pred_stream || !pred_stream_size)
    {
      return ER_QPROC_INVALID_XASLNODE;
    }

  xts_Xasl_errcode = NO_ERROR;
  header_size = 0;		/*could be changed */

  offset = sizeof (int)		/* [size of header data] */
    + header_size		/* [header data] */
    + sizeof (int);		/* [size of body data] */
  offset = MAKE_ALIGN (offset);

  xts_reserve_location_in_stream (offset);

  if (xts_save_filter_pred_node (pred) == ER_FAILED)
    {
      if (xts_Stream_buffer)
	{
	  free_and_init (xts_Stream_buffer);
	}
      goto end;
    }

  /* make header size and data  */
  p = or_pack_int (xts_Stream_buffer, header_size);

  /* set body size of new XASL format */
  body_size = xts_Free_offset_in_stream - offset;
  p = or_pack_int (p, body_size);

  /* set result */
  *pred_stream = xts_Stream_buffer;
  *pred_stream_size = xts_Free_offset_in_stream;

end:
  /* free all memories */
  xts_free_visited_ptrs ();

  xts_Stream_buffer = NULL;
  xts_Stream_size = 0;
  xts_Free_offset_in_stream = 0;

  return xts_Xasl_errcode;
}

/*
 * xts_map_func_pred_to_stream () -
 *   return: if successful, return 0, otherwise non-zero error code
 *   xasl_tree(in)      : pointer to root of XASL tree
 *   xasl_stream(out)   : pointer to xasl stream
 *   xasl_stream_size(out): # of bytes in xasl_stream
 *
 *   Note: the caller should be responsible for free the memory of
 *   xasl_stream. the free function should be free_and_init().
 */
int
xts_map_func_pred_to_stream (const FUNC_PRED * xasl_tree,
			     char **xasl_stream, int *xasl_stream_size)
{
  int offset;
  int header_size, body_size;
  char *p;
  int test;

  if (!xasl_tree || !xasl_stream || !xasl_stream_size)
    {
      return ER_QPROC_INVALID_XASLNODE;
    }

  xts_Xasl_errcode = NO_ERROR;

  /* reserve space for new XASL format */
  header_size = 0;

  offset = sizeof (int)		/* [size of header data] */
    + header_size		/* [header data] */
    + sizeof (int);		/* [size of body data] */
  offset = MAKE_ALIGN (offset);

  xts_reserve_location_in_stream (offset);

  /* save XASL tree into body data of the stream buffer */
  if (xts_save_func_pred (xasl_tree) == ER_FAILED)
    {
      if (xts_Stream_buffer)
	{
	  free_and_init (xts_Stream_buffer);
	}
      goto end;
    }

  /* make header size and data of new XASL format */
  p = or_pack_int (xts_Stream_buffer, header_size);

  /* set body size of new XASL format */
  body_size = xts_Free_offset_in_stream - offset;
  p = or_pack_int (p, body_size);

  /* set result */
  *xasl_stream = xts_Stream_buffer;
  *xasl_stream_size = xts_Free_offset_in_stream;

end:
  /* free all memories */
  xts_free_visited_ptrs ();

  xts_Stream_buffer = NULL;
  xts_Stream_size = 0;
  xts_Free_offset_in_stream = 0;
  or_unpack_int (*xasl_stream, &test);
  return xts_Xasl_errcode;
}

/*
 * xts_final () -
 *   return:
 *
 * Note: Added for the PC so we have a way to free up all the storage that
 * is currently in use when a database "connection" is closed.
 * Called by qp_final() on the PC but not for the workstations.
 */
void
xts_final ()
{
  int i;

  for (i = 0; i < MAX_PTR_BLOCKS; i++)
    {
      if (xts_Ptr_blocks[i] != NULL)
	{
	  free_and_init (xts_Ptr_blocks[i]);
	}
      xts_Ptr_blocks[i] = NULL;
      xts_Ptr_lwm[i] = 0;
      xts_Ptr_max[i] = 0;
    }
}

static int
xts_save_aggregate_type (const AGGREGATE_TYPE * aggregate)
{
  int offset;
  int size;
  OR_ALIGNED_BUF (sizeof (*aggregate) * 2) a_buf;
  char *buf = OR_ALIGNED_BUF_START (a_buf);
  char *buf_p = NULL;
  bool is_buf_alloced = false;

  if (aggregate == NULL)
    {
      return NO_ERROR;
    }

  offset = xts_get_offset_visited_ptr (aggregate);
  if (offset != ER_FAILED)
    {
      return offset;
    }

  size = xts_sizeof_aggregate_type (aggregate);
  if (size == ER_FAILED)
    {
      return ER_FAILED;
    }

  offset = xts_reserve_location_in_stream (size);
  if (offset == ER_FAILED
      || xts_mark_ptr_visited (aggregate, offset) == ER_FAILED)
    {
      return ER_FAILED;
    }

  if (size <= (int) OR_ALIGNED_BUF_SIZE (a_buf))
    {
      buf_p = buf;
    }
  else
    {
      buf_p = (char *) malloc (size);
      if (buf_p == NULL)
	{
	  xts_Xasl_errcode = ER_OUT_OF_VIRTUAL_MEMORY;
	  return ER_FAILED;
	}

      is_buf_alloced = true;
    }

  buf = xts_process_aggregate_type (buf_p, aggregate);
  if (buf == NULL)
    {
      offset = ER_FAILED;
      goto end;
    }
  assert (buf <= buf_p + size);

  memcpy (&xts_Stream_buffer[offset], buf_p, size);

end:
  if (is_buf_alloced)
    {
      free_and_init (buf_p);
    }

  return offset;
}

static int
xts_save_function_type (const FUNCTION_TYPE * function)
{
  int offset;
  int size;
  OR_ALIGNED_BUF (sizeof (*function) * 2) a_buf;
  char *buf = OR_ALIGNED_BUF_START (a_buf);
  char *buf_p = NULL;
  bool is_buf_alloced = false;

  if (function == NULL)
    {
      return NO_ERROR;
    }

  offset = xts_get_offset_visited_ptr (function);
  if (offset != ER_FAILED)
    {
      return offset;
    }

  size = xts_sizeof_function_type (function);
  if (size == ER_FAILED)
    {
      return ER_FAILED;
    }

  offset = xts_reserve_location_in_stream (size);
  if (offset == ER_FAILED
      || xts_mark_ptr_visited (function, offset) == ER_FAILED)
    {
      return ER_FAILED;
    }

  if (size <= (int) OR_ALIGNED_BUF_SIZE (a_buf))
    {
      buf_p = buf;
    }
  else
    {
      buf_p = (char *) malloc (size);
      if (buf_p == NULL)
	{
	  xts_Xasl_errcode = ER_OUT_OF_VIRTUAL_MEMORY;
	  return ER_FAILED;
	}

      is_buf_alloced = true;
    }

  buf = xts_process_function_type (buf_p, function);
  if (buf == NULL)
    {
      offset = ER_FAILED;
      goto end;
    }
  assert (buf <= buf_p + size);

  memcpy (&xts_Stream_buffer[offset], buf_p, size);

end:
  if (is_buf_alloced)
    {
      free_and_init (buf_p);
    }

  return offset;
}

static int
xts_save_analytic_type (const ANALYTIC_TYPE * analytic)
{
  int offset;
  int size;
  OR_ALIGNED_BUF (sizeof (*analytic) * 2) a_buf;
  char *buf = OR_ALIGNED_BUF_START (a_buf);
  char *buf_p = NULL;
  bool is_buf_alloced = false;

  if (analytic == NULL)
    {
      return NO_ERROR;
    }

  offset = xts_get_offset_visited_ptr (analytic);
  if (offset != ER_FAILED)
    {
      return offset;
    }

  size = xts_sizeof_analytic_type (analytic);
  if (size == ER_FAILED)
    {
      return ER_FAILED;
    }

  offset = xts_reserve_location_in_stream (size);
  if (offset == ER_FAILED
      || xts_mark_ptr_visited (analytic, offset) == ER_FAILED)
    {
      return ER_FAILED;
    }

  if (size <= (int) OR_ALIGNED_BUF_SIZE (a_buf))
    {
      buf_p = buf;
    }
  else
    {
      buf_p = (char *) malloc (size);
      if (buf_p == NULL)
	{
	  xts_Xasl_errcode = ER_OUT_OF_VIRTUAL_MEMORY;
	  return ER_FAILED;
	}

      is_buf_alloced = true;
    }

  if (xts_process_analytic_type (buf_p, analytic) == NULL)
    {
      offset = ER_FAILED;
      goto end;
    }

  memcpy (&xts_Stream_buffer[offset], buf_p, size);

end:
  if (is_buf_alloced)
    {
      free_and_init (buf_p);
    }

  return offset;
}

static int
xts_save_analytic_eval_type (const ANALYTIC_EVAL_TYPE * analytic_eval)
{
  int offset;
  int size;
  OR_ALIGNED_BUF (sizeof (*analytic_eval) * 2) a_buf;
  char *buf = OR_ALIGNED_BUF_START (a_buf);
  char *buf_p = NULL;
  bool is_buf_alloced = false;

  if (analytic_eval == NULL)
    {
      return NO_ERROR;
    }

  offset = xts_get_offset_visited_ptr (analytic_eval);
  if (offset != ER_FAILED)
    {
      return offset;
    }

  size = xts_sizeof_analytic_eval_type (analytic_eval);
  if (size == ER_FAILED)
    {
      return ER_FAILED;
    }

  offset = xts_reserve_location_in_stream (size);
  if (offset == ER_FAILED
      || xts_mark_ptr_visited (analytic_eval, offset) == ER_FAILED)
    {
      return ER_FAILED;
    }

  if (size <= (int) OR_ALIGNED_BUF_SIZE (a_buf))
    {
      buf_p = buf;
    }
  else
    {
      buf_p = (char *) malloc (size);
      if (buf_p == NULL)
	{
	  xts_Xasl_errcode = ER_OUT_OF_VIRTUAL_MEMORY;
	  return ER_FAILED;
	}

      is_buf_alloced = true;
    }

  if (xts_process_analytic_eval_type (buf_p, analytic_eval) == NULL)
    {
      offset = ER_FAILED;
      goto end;
    }

  memcpy (&xts_Stream_buffer[offset], buf_p, size);

end:
  if (is_buf_alloced)
    {
      free_and_init (buf_p);
    }

  return offset;
}

static int
xts_save_srlist_id (const QFILE_SORTED_LIST_ID * sort_list_id)
{
  int offset;
  int size;
  OR_ALIGNED_BUF (sizeof (*sort_list_id) * 2) a_buf;
  char *buf = OR_ALIGNED_BUF_START (a_buf);
  char *buf_p = NULL;
  bool is_buf_alloced = false;

  if (sort_list_id == NULL)
    {
      return NO_ERROR;
    }

  offset = xts_get_offset_visited_ptr (sort_list_id);
  if (offset != ER_FAILED)
    {
      return offset;
    }

  size = xts_sizeof_srlist_id (sort_list_id);
  if (size == ER_FAILED)
    {
      return ER_FAILED;
    }

  offset = xts_reserve_location_in_stream (size);
  if (offset == ER_FAILED
      || xts_mark_ptr_visited (sort_list_id, offset) == ER_FAILED)
    {
      return ER_FAILED;
    }

  if (size <= (int) OR_ALIGNED_BUF_SIZE (a_buf))
    {
      buf_p = buf;
    }
  else
    {
      buf_p = (char *) malloc (size);
      if (buf_p == NULL)
	{
	  xts_Xasl_errcode = ER_OUT_OF_VIRTUAL_MEMORY;
	  return ER_FAILED;
	}

      is_buf_alloced = true;
    }

  buf = xts_process_srlist_id (buf_p, sort_list_id);
  if (buf == NULL)
    {
      offset = ER_FAILED;
      goto end;
    }
  assert (buf <= buf_p + size);

  memcpy (&xts_Stream_buffer[offset], buf_p, size);

end:
  if (is_buf_alloced)
    {
      free_and_init (buf_p);
    }

  return offset;
}

static int
xts_save_list_id (const QFILE_LIST_ID * list_id)
{
  int offset;
  int size;
  OR_ALIGNED_BUF (sizeof (*list_id) * 2) a_buf;
  char *buf = OR_ALIGNED_BUF_START (a_buf);
  char *buf_p = NULL;
  bool is_buf_alloced = false;

  if (list_id == NULL)
    {
      return NO_ERROR;
    }

  offset = xts_get_offset_visited_ptr (list_id);
  if (offset != ER_FAILED)
    {
      return offset;
    }

  size = xts_sizeof_list_id (list_id);
  if (size == ER_FAILED)
    {
      return ER_FAILED;
    }

  offset = xts_reserve_location_in_stream (size);
  if (offset == ER_FAILED
      || xts_mark_ptr_visited (list_id, offset) == ER_FAILED)
    {
      return ER_FAILED;
    }

  if (size <= (int) OR_ALIGNED_BUF_SIZE (a_buf))
    {
      buf_p = buf;
    }
  else
    {
      buf_p = (char *) malloc (size);
      if (buf_p == NULL)
	{
	  xts_Xasl_errcode = ER_OUT_OF_VIRTUAL_MEMORY;
	  return ER_FAILED;
	}

      is_buf_alloced = true;
    }

  buf = xts_process_list_id (buf_p, list_id);
  if (buf == NULL)
    {
      offset = ER_FAILED;
      goto end;
    }
  assert (buf <= buf_p + size);

  memcpy (&xts_Stream_buffer[offset], buf_p, size);

end:
  if (is_buf_alloced)
    {
      free_and_init (buf_p);
    }

  return offset;
}

static int
xts_save_arith_type (const ARITH_TYPE * arithmetic)
{
  int offset;
  int size;
  OR_ALIGNED_BUF (sizeof (*arithmetic) * 2) a_buf;
  char *buf = OR_ALIGNED_BUF_START (a_buf);
  char *buf_p = NULL;
  bool is_buf_alloced = false;

  if (arithmetic == NULL)
    {
      return NO_ERROR;
    }

  offset = xts_get_offset_visited_ptr (arithmetic);
  if (offset != ER_FAILED)
    {
      return offset;
    }

  size = xts_sizeof_arith_type (arithmetic);
  if (size == ER_FAILED)
    {
      return ER_FAILED;
    }

  offset = xts_reserve_location_in_stream (size);
  if (offset == ER_FAILED
      || xts_mark_ptr_visited (arithmetic, offset) == ER_FAILED)
    {
      return ER_FAILED;
    }

  if (size <= (int) OR_ALIGNED_BUF_SIZE (a_buf))
    {
      buf_p = buf;
    }
  else
    {
      buf_p = (char *) malloc (size);
      if (buf_p == NULL)
	{
	  xts_Xasl_errcode = ER_OUT_OF_VIRTUAL_MEMORY;
	  return ER_FAILED;
	}

      is_buf_alloced = true;
    }

  buf = xts_process_arith_type (buf_p, arithmetic);
  if (buf == NULL)
    {
      offset = ER_FAILED;
      goto end;
    }
  assert (buf <= buf_p + size);

  memcpy (&xts_Stream_buffer[offset], buf_p, size);

end:
  if (is_buf_alloced)
    {
      free_and_init (buf_p);
    }

  return offset;
}

static int
xts_save_indx_info (const INDX_INFO * indx_info)
{
  int offset;
  int size;
  OR_ALIGNED_BUF (sizeof (*indx_info) * 2) a_buf;
  char *buf = OR_ALIGNED_BUF_START (a_buf);
  char *buf_p = NULL;
  bool is_buf_alloced = false;

  if (indx_info == NULL)
    {
      return NO_ERROR;
    }

  offset = xts_get_offset_visited_ptr (indx_info);
  if (offset != ER_FAILED)
    {
      return offset;
    }

  size = xts_sizeof_indx_info (indx_info);
  if (size == ER_FAILED)
    {
      return ER_FAILED;
    }

  offset = xts_reserve_location_in_stream (size);
  if (offset == ER_FAILED
      || xts_mark_ptr_visited (indx_info, offset) == ER_FAILED)
    {
      return ER_FAILED;
    }

  if (size <= (int) OR_ALIGNED_BUF_SIZE (a_buf))
    {
      buf_p = buf;
    }
  else
    {
      buf_p = (char *) malloc (size);
      if (buf_p == NULL)
	{
	  xts_Xasl_errcode = ER_OUT_OF_VIRTUAL_MEMORY;
	  return ER_FAILED;
	}

      is_buf_alloced = true;
    }

  buf = xts_process_indx_info (buf_p, indx_info);
  if (buf == NULL)
    {
      offset = ER_FAILED;
      goto end;
    }
  assert (buf <= buf_p + size);

  memcpy (&xts_Stream_buffer[offset], buf_p, size);

end:
  if (is_buf_alloced)
    {
      free_and_init (buf_p);
    }

  return offset;
}

static int
xts_save_outptr_list (const OUTPTR_LIST * outptr_list)
{
  int offset;
  int size;
  OR_ALIGNED_BUF (sizeof (*outptr_list) * 2) a_buf;
  char *buf = OR_ALIGNED_BUF_START (a_buf);
  char *buf_p = NULL;
  bool is_buf_alloced = false;

  if (outptr_list == NULL)
    {
      return NO_ERROR;
    }

  offset = xts_get_offset_visited_ptr (outptr_list);
  if (offset != ER_FAILED)
    {
      return offset;
    }

  size = xts_sizeof_outptr_list (outptr_list);
  if (size == ER_FAILED)
    {
      return ER_FAILED;
    }

  offset = xts_reserve_location_in_stream (size);
  if (offset == ER_FAILED
      || xts_mark_ptr_visited (outptr_list, offset) == ER_FAILED)
    {
      return ER_FAILED;
    }

  if (size <= (int) OR_ALIGNED_BUF_SIZE (a_buf))
    {
      buf_p = buf;
    }
  else
    {
      buf_p = (char *) malloc (size);
      if (buf_p == NULL)
	{
	  xts_Xasl_errcode = ER_OUT_OF_VIRTUAL_MEMORY;
	  return ER_FAILED;
	}

      is_buf_alloced = true;
    }

  buf = xts_process_outptr_list (buf_p, outptr_list);
  if (buf == NULL)
    {
      offset = ER_FAILED;
      goto end;
    }
  assert (buf <= buf_p + size);

  memcpy (&xts_Stream_buffer[offset], buf_p, size);

end:
  if (is_buf_alloced)
    {
      free_and_init (buf_p);
    }

  return offset;
}

static int
xts_save_selupd_list (const SELUPD_LIST * selupd_list)
{
  int offset;
  int size;
  OR_ALIGNED_BUF (sizeof (*selupd_list) * 2) a_buf;
  char *buf = OR_ALIGNED_BUF_START (a_buf);
  char *buf_p = NULL;
  bool is_buf_alloced = false;

  if (selupd_list == NULL)
    {
      return NO_ERROR;
    }

  offset = xts_get_offset_visited_ptr (selupd_list);
  if (offset != ER_FAILED)
    {
      return offset;
    }

  size = xts_sizeof_selupd_list (selupd_list);
  if (size == ER_FAILED)
    {
      return ER_FAILED;
    }

  offset = xts_reserve_location_in_stream (size);
  if (offset == ER_FAILED
      || xts_mark_ptr_visited (selupd_list, offset) == ER_FAILED)
    {
      return ER_FAILED;
    }

  if (size <= (int) OR_ALIGNED_BUF_SIZE (a_buf))
    {
      buf_p = buf;
    }
  else
    {
      buf_p = (char *) malloc (size);
      if (buf_p == NULL)
	{
	  xts_Xasl_errcode = ER_OUT_OF_VIRTUAL_MEMORY;
	  return ER_FAILED;
	}

      is_buf_alloced = true;
    }

  buf = xts_process_selupd_list (buf_p, selupd_list);
  if (buf == NULL)
    {
      offset = ER_FAILED;
      goto end;
    }
  assert (buf <= buf_p + size);

  memcpy (&xts_Stream_buffer[offset], buf_p, size);

end:
  if (is_buf_alloced)
    {
      free_and_init (buf_p);
    }

  return offset;
}

static int
xts_save_pred_expr (const PRED_EXPR * pred_expr)
{
  int offset;
  int size;
  OR_ALIGNED_BUF (sizeof (*pred_expr) * 2) a_buf;
  char *buf = OR_ALIGNED_BUF_START (a_buf);
  char *buf_p = NULL;
  bool is_buf_alloced = false;

  if (pred_expr == NULL)
    {
      return NO_ERROR;
    }

  offset = xts_get_offset_visited_ptr (pred_expr);
  if (offset != ER_FAILED)
    {
      return offset;
    }

  size = xts_sizeof_pred_expr (pred_expr);
  if (size == ER_FAILED)
    {
      return ER_FAILED;
    }

  offset = xts_reserve_location_in_stream (size);
  if (offset == ER_FAILED
      || xts_mark_ptr_visited (pred_expr, offset) == ER_FAILED)
    {
      return ER_FAILED;
    }

  if (size <= (int) OR_ALIGNED_BUF_SIZE (a_buf))
    {
      buf_p = buf;
    }
  else
    {
      buf_p = (char *) malloc (size);
      if (buf_p == NULL)
	{
	  xts_Xasl_errcode = ER_OUT_OF_VIRTUAL_MEMORY;
	  return ER_FAILED;
	}

      is_buf_alloced = true;
    }

  buf = xts_process_pred_expr (buf_p, pred_expr);
  if (buf == NULL)
    {
      offset = ER_FAILED;
      goto end;
    }
  assert (buf <= buf_p + size);

  memcpy (&xts_Stream_buffer[offset], buf_p, size);

end:
  if (is_buf_alloced)
    {
      free_and_init (buf_p);
    }

  return offset;
}

static int
xts_save_regu_variable (const REGU_VARIABLE * regu_var)
{
  int offset;
  int size;
  OR_ALIGNED_BUF (sizeof (*regu_var) * 2) a_buf;
  char *buf = OR_ALIGNED_BUF_START (a_buf);
  char *buf_p = NULL;
  bool is_buf_alloced = false;

  if (regu_var == NULL)
    {
      return NO_ERROR;
    }

  offset = xts_get_offset_visited_ptr (regu_var);
  if (offset != ER_FAILED)
    {
      return offset;
    }

  size = xts_sizeof_regu_variable (regu_var);
  if (size == ER_FAILED)
    {
      return ER_FAILED;
    }

  offset = xts_reserve_location_in_stream (size);
  if (offset == ER_FAILED
      || xts_mark_ptr_visited (regu_var, offset) == ER_FAILED)
    {
      return ER_FAILED;
    }

  if (size <= (int) OR_ALIGNED_BUF_SIZE (a_buf))
    {
      buf_p = buf;
    }
  else
    {
      buf_p = (char *) malloc (size);
      if (buf_p == NULL)
	{
	  xts_Xasl_errcode = ER_OUT_OF_VIRTUAL_MEMORY;
	  return ER_FAILED;
	}

      is_buf_alloced = true;
    }

  buf = xts_process_regu_variable (buf_p, regu_var);
  if (buf == NULL)
    {
      offset = ER_FAILED;
      goto end;
    }
  assert (buf <= buf_p + size);

  /*
   * OR_VALUE_ALIGNED_SIZE may reserve more bytes
   * suppress valgrind UMW (uninitialized memory write)
   */
#if !defined(NDEBUG)
  do
    {
      int margin = size - (buf - buf_p);
      if (margin > 0)
	{
	  memset (buf, 0, margin);
	}
    }
  while (0);
#endif

  memcpy (&xts_Stream_buffer[offset], buf_p, size);

end:
  if (is_buf_alloced)
    {
      free_and_init (buf_p);
    }

  return offset;
}

static int
xts_save_sort_list (const SORT_LIST * sort_list)
{
  int offset;
  int size;
  OR_ALIGNED_BUF (sizeof (*sort_list) * 2) a_buf;
  char *buf = OR_ALIGNED_BUF_START (a_buf);
  char *buf_p = NULL;
  bool is_buf_alloced = false;

  if (sort_list == NULL)
    {
      return NO_ERROR;
    }

  offset = xts_get_offset_visited_ptr (sort_list);
  if (offset != ER_FAILED)
    {
      return offset;
    }

  size = xts_sizeof_sort_list (sort_list);
  if (size == ER_FAILED)
    {
      return ER_FAILED;
    }

  offset = xts_reserve_location_in_stream (size);
  if (offset == ER_FAILED
      || xts_mark_ptr_visited (sort_list, offset) == ER_FAILED)
    {
      return ER_FAILED;
    }

  if (size <= (int) OR_ALIGNED_BUF_SIZE (a_buf))
    {
      buf_p = buf;
    }
  else
    {
      buf_p = (char *) malloc (size);
      if (buf_p == NULL)
	{
	  xts_Xasl_errcode = ER_OUT_OF_VIRTUAL_MEMORY;
	  return ER_FAILED;
	}

      is_buf_alloced = true;
    }

  buf = xts_process_sort_list (buf_p, sort_list);
  if (buf == NULL)
    {
      offset = ER_FAILED;
      goto end;
    }
  assert (buf <= buf_p + size);

  memcpy (&xts_Stream_buffer[offset], buf_p, size);

end:
  if (is_buf_alloced)
    {
      free_and_init (buf_p);
    }

  return offset;
}

static int
xts_save_val_list (const VAL_LIST * val_list)
{
  int offset;
  int size;
  OR_ALIGNED_BUF (sizeof (*val_list) * 2) a_buf;
  char *buf = OR_ALIGNED_BUF_START (a_buf);
  char *buf_p = NULL;
  bool is_buf_alloced = false;

  if (val_list == NULL)
    {
      return NO_ERROR;
    }

  offset = xts_get_offset_visited_ptr (val_list);
  if (offset != ER_FAILED)
    {
      return offset;
    }

  size = xts_sizeof_val_list (val_list);
  if (size == ER_FAILED)
    {
      return ER_FAILED;
    }

  offset = xts_reserve_location_in_stream (size);
  if (offset == ER_FAILED
      || xts_mark_ptr_visited (val_list, offset) == ER_FAILED)
    {
      return ER_FAILED;
    }

  if (size <= (int) OR_ALIGNED_BUF_SIZE (a_buf))
    {
      buf_p = buf;
    }
  else
    {
      buf_p = (char *) malloc (size);
      if (buf_p == NULL)
	{
	  xts_Xasl_errcode = ER_OUT_OF_VIRTUAL_MEMORY;
	  return ER_FAILED;
	}

      is_buf_alloced = true;
    }

  buf = xts_process_val_list (buf_p, val_list);
  if (buf == NULL)
    {
      offset = ER_FAILED;
      goto end;
    }
  assert (buf <= buf_p + size);

  memcpy (&xts_Stream_buffer[offset], buf_p, size);

end:
  if (is_buf_alloced)
    {
      free_and_init (buf_p);
    }

  return offset;
}

static int
xts_save_db_value (const DB_VALUE * value)
{
  int offset;
  int size;
  OR_ALIGNED_BUF (sizeof (*value) * 2) a_buf;
  char *buf = OR_ALIGNED_BUF_START (a_buf);
  char *buf_p = NULL;
  bool is_buf_alloced = false;

  if (value == NULL)
    {
      return NO_ERROR;
    }

  offset = xts_get_offset_visited_ptr (value);
  if (offset != ER_FAILED)
    {
      return offset;
    }

  size = xts_sizeof_db_value (value);
  if (size == ER_FAILED)
    {
      return ER_FAILED;
    }

  offset = xts_reserve_location_in_stream (size);
  if (offset == ER_FAILED
      || xts_mark_ptr_visited (value, offset) == ER_FAILED)
    {
      return ER_FAILED;
    }

  if (size <= (int) OR_ALIGNED_BUF_SIZE (a_buf))
    {
      buf_p = buf;
    }
  else
    {
      buf_p = (char *) malloc (size);
      if (buf_p == NULL)
	{
	  xts_Xasl_errcode = ER_OUT_OF_VIRTUAL_MEMORY;
	  return ER_FAILED;
	}

      is_buf_alloced = true;
    }

  buf = xts_process_db_value (buf_p, value);
  if (buf == NULL)
    {
      offset = ER_FAILED;
      goto end;
    }
  assert (buf <= buf_p + size);

  memcpy (&xts_Stream_buffer[offset], buf_p, size);

end:
  if (is_buf_alloced)
    {
      free_and_init (buf_p);
    }

  return offset;
}

static int
xts_save_xasl_node (const XASL_NODE * xasl)
{
  int offset;
  int size;
  OR_ALIGNED_BUF (sizeof (*xasl) * 2) a_buf;
  char *buf = OR_ALIGNED_BUF_START (a_buf);
  char *buf_p = NULL;
  bool is_buf_alloced = false;

  if (xasl == NULL)
    {
      return NO_ERROR;
    }

  offset = xts_get_offset_visited_ptr (xasl);
  if (offset != ER_FAILED)
    {
      return offset;
    }

  size = xts_sizeof_xasl_node (xasl);
  if (size == ER_FAILED)
    {
      return ER_FAILED;
    }

  offset = xts_reserve_location_in_stream (size);
  if (offset == ER_FAILED || xts_mark_ptr_visited (xasl, offset) == ER_FAILED)
    {
      return ER_FAILED;
    }

  if (size <= (int) OR_ALIGNED_BUF_SIZE (a_buf))
    {
      buf_p = buf;
    }
  else
    {
      buf_p = (char *) malloc (size);
      if (buf_p == NULL)
	{
	  xts_Xasl_errcode = ER_OUT_OF_VIRTUAL_MEMORY;
	  return ER_FAILED;
	}

      is_buf_alloced = true;
    }

  buf = xts_process_xasl_node (buf_p, xasl);
  if (buf == NULL)
    {
      offset = ER_FAILED;
      goto end;
    }

  assert (buf <= buf_p + size);

  /*
   * OR_DOUBLE_ALIGNED_SIZE may reserve more bytes
   * suppress valgrind UMW (uninitialized memory write)
   */
#if !defined(NDEBUG)
  do
    {
      int margin = size - (buf - buf_p);
      if (margin > 0)
	{
	  memset (buf, 0, margin);
	}
    }
  while (0);
#endif

  memcpy (&xts_Stream_buffer[offset], buf_p, size);

end:
  if (is_buf_alloced)
    {
      free_and_init (buf_p);
    }

  return offset;
}

static int
xts_save_filter_pred_node (const PRED_EXPR_WITH_CONTEXT * pred)
{
  int offset;
  int size;
  OR_ALIGNED_BUF (sizeof (*pred) * 2) a_buf;
  char *buf = OR_ALIGNED_BUF_START (a_buf);
  char *buf_p = NULL;
  bool is_buf_alloced = false;

  if (pred == NULL)
    {
      return NO_ERROR;
    }

  offset = xts_get_offset_visited_ptr (pred);
  if (offset != ER_FAILED)
    {
      return offset;
    }

  size = xts_sizeof_filter_pred_node (pred);
  if (size == ER_FAILED)
    {
      return ER_FAILED;
    }

  offset = xts_reserve_location_in_stream (size);
  if (offset == ER_FAILED || xts_mark_ptr_visited (pred, offset) == ER_FAILED)
    {
      return ER_FAILED;
    }

  if (size <= (int) OR_ALIGNED_BUF_SIZE (a_buf))
    {
      buf_p = buf;
    }
  else
    {
      buf_p = (char *) malloc (size);
      if (buf_p == NULL)
	{
	  xts_Xasl_errcode = ER_OUT_OF_VIRTUAL_MEMORY;
	  return ER_FAILED;
	}

      is_buf_alloced = true;
    }

  buf = xts_process_filter_pred_node (buf_p, pred);
  if (buf == NULL)
    {
      offset = ER_FAILED;
      goto end;
    }
  assert (buf <= buf_p + size);

  memcpy (&xts_Stream_buffer[offset], buf_p, size);

end:
  if (is_buf_alloced)
    {
      free_and_init (buf_p);
    }

  return offset;
}

static int
xts_save_func_pred (const FUNC_PRED * func_pred)
{
  int offset;
  int size;
  OR_ALIGNED_BUF (sizeof (*func_pred) * 2) a_buf;
  char *buf = OR_ALIGNED_BUF_START (a_buf);
  char *buf_p = NULL;
  bool is_buf_alloced = false;

  if (func_pred == NULL)
    {
      return NO_ERROR;
    }

  offset = xts_get_offset_visited_ptr (func_pred);
  if (offset != ER_FAILED)
    {
      return offset;
    }

  size = xts_sizeof_func_pred (func_pred);
  if (size == ER_FAILED)
    {
      return ER_FAILED;
    }

  offset = xts_reserve_location_in_stream (size);
  if (offset == ER_FAILED
      || xts_mark_ptr_visited (func_pred, offset) == ER_FAILED)
    {
      return ER_FAILED;
    }

  if (size <= (int) OR_ALIGNED_BUF_SIZE (a_buf))
    {
      buf_p = buf;
    }
  else
    {
      buf_p = (char *) malloc (size);
      if (buf_p == NULL)
	{
	  xts_Xasl_errcode = ER_OUT_OF_VIRTUAL_MEMORY;
	  return ER_FAILED;
	}

      is_buf_alloced = true;
    }

  buf = xts_process_func_pred (buf_p, func_pred);
  if (buf == NULL)
    {
      offset = ER_FAILED;
      goto end;
    }
  assert (buf <= buf_p + size);

  memcpy (&xts_Stream_buffer[offset], buf_p, size);

end:
  if (is_buf_alloced)
    {
      free_and_init (buf_p);
    }

  return offset;
}

static int
xts_save_cache_attrinfo (const HEAP_CACHE_ATTRINFO * attrinfo)
{
  int offset;
  int size;
  OR_ALIGNED_BUF (sizeof (*attrinfo) * 2) a_buf;
  char *buf = OR_ALIGNED_BUF_START (a_buf);
  char *buf_p = NULL;
  bool is_buf_alloced = false;

  if (attrinfo == NULL)
    {
      return NO_ERROR;
    }

  offset = xts_get_offset_visited_ptr (attrinfo);
  if (offset != ER_FAILED)
    {
      return offset;
    }

  size = xts_sizeof_cache_attrinfo (attrinfo);
  if (size == ER_FAILED)
    {
      return ER_FAILED;
    }

  offset = xts_reserve_location_in_stream (size);
  if (offset == ER_FAILED
      || xts_mark_ptr_visited (attrinfo, offset) == ER_FAILED)
    {
      return ER_FAILED;
    }

  if (size <= (int) OR_ALIGNED_BUF_SIZE (a_buf))
    {
      buf_p = buf;
    }
  else
    {
      buf_p = (char *) malloc (size);
      if (buf_p == NULL)
	{
	  xts_Xasl_errcode = ER_OUT_OF_VIRTUAL_MEMORY;
	  return ER_FAILED;
	}

      is_buf_alloced = true;
    }

  buf = xts_process_cache_attrinfo (buf_p);
  if (buf == NULL)
    {
      offset = ER_FAILED;
      goto end;
    }
  assert (buf <= buf_p + size);

  memcpy (&xts_Stream_buffer[offset], buf_p, size);

end:
  if (is_buf_alloced)
    {
      free_and_init (buf_p);
    }

  return offset;
}

#if 0
/*
 * there are currently no pointers to these type of structures in xasl
 * so there is no need to have a separate restore function.
 */

static int
xts_save_merge_list_info (const MERGELIST_PROC_NODE * mergelist_proc)
{
  int offset;
  int size;
  OR_ALIGNED_BUF (sizeof (*mergelist_proc) * 2) a_buf;
  char *buf = OR_ALIGNED_BUF_START (a_buf);
  char *buf_p = NULL;
  bool is_buf_alloced = false;

  if (mergelist_proc == NULL)
    {
      return NO_ERROR;
    }

  offset = xts_get_offset_visited_ptr (mergelist_proc);
  if (offset != ER_FAILED)
    {
      return offset;
    }

  size = xts_sizeof_merge_list_info (mergelist_proc);
  if (size == ER_FAILED)
    {
      return ER_FAILED;
    }

  offset = xts_reserve_location_in_stream (size);
  if (offset == ER_FAILED
      || xts_mark_ptr_visited (mergelist_proc, offset) == ER_FAILED)
    {
      return ER_FAILED;
    }

  if (size <= (int) OR_ALIGNED_BUF_SIZE (a_buf))
    {
      buf_p = buf;
    }
  else
    {
      buf_p = (char *) malloc (size);
      if (buf_p == NULL)
	{
	  xts_Xasl_errcode = ER_OUT_OF_VIRTUAL_MEMORY;
	  return ER_FAILED;
	}

      is_buf_alloced = true;
    }

  if (xts_process_merge_list_info (buf_p, mergelist_proc) == NULL)
    {
      offset = ER_FAILED;
      goto end;
    }

  memcpy (&xts_Stream_buffer[offset], buf_p, size);

end:
  if (is_buf_alloced)
    {
      free_and_init (buf_p);
    }

  return offset;
}

static int
xts_save_ls_merge_info (const QFILE_LIST_MERGE_INFO * list_merge_info)
{
  int offset;
  int size;
  OR_ALIGNED_BUF (sizeof (*list_merge_info) * 2) a_buf;
  char *buf = OR_ALIGNED_BUF_START (a_buf);
  char *buf_p = NULL;
  bool is_buf_alloced = false;

  if (list_merge_info == NULL)
    {
      return NO_ERROR;
    }

  offset = xts_get_offset_visited_ptr (list_merge_info);
  if (offset != ER_FAILED)
    {
      return offset;
    }

  size = xts_sizeof_ls_merge_info (list_merge_info);
  if (size == ER_FAILED)
    {
      return ER_FAILED;
    }

  offset = xts_reserve_location_in_stream (size);
  if (offset == ER_FAILED
      || xts_mark_ptr_visited (list_merge_info, offset) == ER_FAILED)
    {
      return ER_FAILED;
    }

  if (size <= (int) OR_ALIGNED_BUF_SIZE (a_buf))
    {
      buf_p = buf;
    }
  else
    {
      buf_p = (char *) malloc (size);
      if (buf_p == NULL)
	{
	  xts_Xasl_errcode = ER_OUT_OF_VIRTUAL_MEMORY;
	  return ER_FAILED;
	}

      is_buf_alloced = true;
    }

  if (xts_process_ls_merge_info (buf_p, list_merge_info) == NULL)
    {
      offset = ER_FAILED;
      goto end;
    }

  memcpy (&xts_Stream_buffer[offset], buf_p, size);

end:
  if (is_buf_alloced)
    {
      free_and_init (buf_p);
    }

  return offset;
}

static int
xts_save_update_info (const UPDATE_PROC_NODE * update_proc)
{
  int offset;
  int size;
  OR_ALIGNED_BUF (sizeof (*update_proc) * 2) a_buf;
  char *buf = OR_ALIGNED_BUF_START (a_buf);
  char *buf_p = NULL;
  bool is_buf_alloced = false;

  if (update_proc == NULL)
    {
      return NO_ERROR;
    }

  offset = xts_get_offset_visited_ptr (update_proc);
  if (offset != ER_FAILED)
    {
      return offset;
    }

  size = xts_sizeof_update_info (update_proc);
  if (size == ER_FAILED)
    {
      return ER_FAILED;
    }

  offset = xts_reserve_location_in_stream (size);
  if (offset == ER_FAILED
      || xts_mark_ptr_visited (update_proc, offset) == ER_FAILED)
    {
      return ER_FAILED;
    }

  if (size <= (int) OR_ALIGNED_BUF_SIZE (a_buf))
    {
      buf_p = buf;
    }
  else
    {
      buf_p = (char *) malloc (size);
      if (buf_p == NULL)
	{
	  xts_Xasl_errcode = ER_OUT_OF_VIRTUAL_MEMORY;
	  return ER_FAILED;
	}

      is_buf_alloced = true;
    }

  if (xts_process_update_info (buf_p, update_proc) == NULL)
    {
      offset = ER_FAILED;
      goto end;
    }

  memcpy (&xts_Stream_buffer[offset], buf_p, size);

end:
  if (is_buf_alloced)
    {
      free_and_init (buf_p);
    }

  return offset;
}

static int
xts_save_delete_info (const DELETE_PROC_NODE * delete_proc)
{
  int offset;
  int size;
  OR_ALIGNED_BUF (sizeof (*delete_proc) * 2) a_buf;
  char *buf = OR_ALIGNED_BUF_START (a_buf);
  char *buf_p = NULL;
  bool is_buf_alloced = false;

  if (delete_proc == NULL)
    {
      return NO_ERROR;
    }

  offset = xts_get_offset_visited_ptr (delete_proc);
  if (offset != ER_FAILED)
    {
      return offset;
    }

  size = xts_sizeof_delete_info (delete_proc);
  if (size == ER_FAILED)
    {
      return ER_FAILED;
    }

  offset = xts_reserve_location_in_stream (size);
  if (offset == ER_FAILED
      || xts_mark_ptr_visited (delete_proc, offset) == ER_FAILED)
    {
      return ER_FAILED;
    }

  if (size <= (int) OR_ALIGNED_BUF_SIZE (a_buf))
    {
      buf_p = buf;
    }
  else
    {
      buf_p = (char *) malloc (size);
      if (buf_p == NULL)
	{
	  xts_Xasl_errcode = ER_OUT_OF_VIRTUAL_MEMORY;
	  return ER_FAILED;
	}

      is_buf_alloced = true;
    }

  if (xts_process_delete_info (buf_p, delete_proc) == NULL)
    {
      offset = ER_FAILED;
      goto end;
    }

  memcpy (&xts_Stream_buffer[offset], buf_p, size);

end:
  if (is_buf_alloced)
    {
      free_and_init (buf_p);
    }

  return offset;
}

static int
xts_save_insert_info (const INSERT_PROC_NODE * insert_proc)
{
  int offset;
  int size;
  OR_ALIGNED_BUF (sizeof (*insert_proc) * 2) a_buf;
  char *buf = OR_ALIGNED_BUF_START (a_buf);
  char *buf_p = NULL;
  bool is_buf_alloced = false;

  if (insert_proc == NULL)
    {
      return NO_ERROR;
    }

  offset = xts_get_offset_visited_ptr (insert_proc);
  if (offset != ER_FAILED)
    {
      return offset;
    }

  size = xts_sizeof_insert_proc (insert_proc);
  if (size == ER_FAILED)
    {
      return ER_FAILED;
    }

  offset = xts_reserve_location_in_stream (size);
  if (offset == ER_FAILED
      || xts_mark_ptr_visited (insert_proc, offset) == ER_FAILED)
    {
      return ER_FAILED;
    }

  if (size <= (int) OR_ALIGNED_BUF_SIZE (a_buf))
    {
      buf_p = buf;
    }
  else
    {
      buf_p = (char *) malloc (size);
      if (buf_p == NULL)
	{
	  xts_Xasl_errcode = ER_OUT_OF_VIRTUAL_MEMORY;
	  return ER_FAILED;
	}

      is_buf_alloced = true;
    }

  if (xts_process_insert_info (buf_p, insert_proc) == NULL)
    {
      offset = ER_FAILED;
      goto end;
    }

  memcpy (&xts_Stream_buffer[offset], buf_p, size);

end:
  if (is_buf_alloced)
    {
      free_and_init (buf_p);
    }

  return offset;
}
#endif

static int
xts_save_method_sig_list (const METHOD_SIG_LIST * method_sig_list)
{
  int offset;
  int size;
  OR_ALIGNED_BUF (sizeof (*method_sig_list) * 2) a_buf;
  char *buf = OR_ALIGNED_BUF_START (a_buf);
  char *buf_p = NULL;
  bool is_buf_alloced = false;

  if (method_sig_list == NULL)
    {
      return NO_ERROR;
    }

  offset = xts_get_offset_visited_ptr (method_sig_list);
  if (offset != ER_FAILED)
    {
      return offset;
    }

  size = xts_sizeof_method_sig_list (method_sig_list);
  if (size == ER_FAILED)
    {
      return ER_FAILED;
    }

  offset = xts_reserve_location_in_stream (size);
  if (offset == ER_FAILED
      || xts_mark_ptr_visited (method_sig_list, offset) == ER_FAILED)
    {
      return ER_FAILED;
    }

  if (size <= (int) OR_ALIGNED_BUF_SIZE (a_buf))
    {
      buf_p = buf;
    }
  else
    {
      buf_p = (char *) malloc (size);
      if (buf_p == NULL)
	{
	  xts_Xasl_errcode = ER_OUT_OF_VIRTUAL_MEMORY;
	  return ER_FAILED;
	}

      is_buf_alloced = true;
    }

  buf = xts_process_method_sig_list (buf_p, method_sig_list);
  if (buf == NULL)
    {
      offset = ER_FAILED;
      goto end;
    }
  assert (buf <= buf_p + size);

  memcpy (&xts_Stream_buffer[offset], buf_p, size);

end:
  if (is_buf_alloced)
    {
      free_and_init (buf_p);
    }

  return offset;
}

static int
xts_save_method_sig (const METHOD_SIG * method_sig, int count)
{
  int offset;
  int size;
  OR_ALIGNED_BUF (sizeof (*method_sig) * 2) a_buf;
  char *buf = OR_ALIGNED_BUF_START (a_buf);
  char *buf_p = NULL;
  bool is_buf_alloced = false;

  if (method_sig == NULL)
    {
      assert (count == 0);
      return NO_ERROR;
    }

  assert (count > 0);

  offset = xts_get_offset_visited_ptr (method_sig);
  if (offset != ER_FAILED)
    {
      return offset;
    }

  size = xts_sizeof_method_sig (method_sig);
  if (size == ER_FAILED)
    {
      return ER_FAILED;
    }

  offset = xts_reserve_location_in_stream (size);
  if (offset == ER_FAILED
      || xts_mark_ptr_visited (method_sig, offset) == ER_FAILED)
    {
      return ER_FAILED;
    }

  if (size <= (int) OR_ALIGNED_BUF_SIZE (a_buf))
    {
      buf_p = buf;
    }
  else
    {
      buf_p = (char *) malloc (size);
      if (buf_p == NULL)
	{
	  xts_Xasl_errcode = ER_OUT_OF_VIRTUAL_MEMORY;
	  return ER_FAILED;
	}

      is_buf_alloced = true;
    }

  buf = xts_process_method_sig (buf_p, method_sig, count);
  if (buf == NULL)
    {
      offset = ER_FAILED;
      goto end;
    }
  assert (buf <= buf_p + size);

  memcpy (&xts_Stream_buffer[offset], buf_p, size);

end:
  if (is_buf_alloced)
    {
      free_and_init (buf_p);
    }

  return offset;
}

static int
xts_save_string (const char *string)
{
  int offset;
  int packed_length, length;

  offset = xts_get_offset_visited_ptr (string);
  if (offset != ER_FAILED)
    {
      return offset;
    }

  packed_length = or_packed_string_length (string, &length);

  assert ((string != NULL && length > 0) || (string == NULL && length == 0));

  offset = xts_reserve_location_in_stream (packed_length);
  if (offset == ER_FAILED
      || xts_mark_ptr_visited (string, offset) == ER_FAILED)
    {
      return ER_FAILED;
    }

  or_pack_string_with_length (&xts_Stream_buffer[offset], string, length);

  return offset;
}

#if defined(ENABLE_UNUSED_FUNCTION)
static int
xts_save_string_with_length (const char *string, int length)
{
  int offset;

  offset = xts_get_offset_visited_ptr (string);
  if (offset != ER_FAILED)
    {
      return offset;
    }

  offset = xts_reserve_location_in_stream (or_align_length (length));
  if (offset == ER_FAILED
      || xts_mark_ptr_visited (string, offset) == ER_FAILED)
    {
      return ER_FAILED;
    }

  or_pack_string_with_length (&xts_Stream_buffer[offset], (char *) string,
			      length);

  return offset;
}

static int
xts_save_input_vals (const char *input_vals_p, int length)
{
  int offset;

  offset = xts_get_offset_visited_ptr (input_vals_p);
  if (offset != ER_FAILED)
    {
      return offset;
    }

  offset = xts_reserve_location_in_stream (length);
  if (offset == ER_FAILED
      || xts_mark_ptr_visited (input_vals_p, offset) == ER_FAILED)
    {
      return ER_FAILED;
    }

  memmove (&xts_Stream_buffer[offset], (char *) input_vals_p, length);

  return offset;
}
#endif

static int
xts_save_db_value_array (DB_VALUE ** db_value_array_p, int nelements)
{
  int offset;
  int *offset_array;
  int i;

  if (db_value_array_p == NULL)
    {
      return NO_ERROR;
    }

  offset_array = (int *) malloc (sizeof (int) * nelements);
  if (offset_array == NULL)
    {
      return ER_FAILED;
    }

  for (i = 0; i < nelements; i++)
    {
      offset_array[i] = xts_save_db_value (db_value_array_p[i]);
      if (offset_array[i] == ER_FAILED)
	{
	  offset = ER_FAILED;
	  goto end;
	}
    }

  offset = xts_save_int_array (offset_array, nelements);

end:
  free_and_init (offset_array);

  return offset;
}

#if defined (ENABLE_UNUSED_FUNCTION)
static int
xts_save_domain_array (TP_DOMAIN ** domain_array_p, int nelements)
{
  int offset, i, len;
  char *ptr;

  if (domain_array_p == NULL)
    {
      return 0;
    }

  for (len = 0, i = 0; i < nelements; i++)
    {
      len += or_packed_domain_size (domain_array_p[i], 0);
    }

  offset = xts_reserve_location_in_stream (len);
  if (offset == ER_FAILED)
    {
      return ER_FAILED;
    }

  ptr = &xts_Stream_buffer[offset];
  for (i = 0; i < nelements; i++)
    {
      ptr = or_pack_domain (ptr, domain_array_p[i], 0, 0);
    }

  return offset;
}
#endif

static int
xts_save_int_array (int *int_array, int nelements)
{
  int offset, i;
  char *ptr;

  if (int_array == NULL)
    {
      return 0;
    }

  offset = xts_reserve_location_in_stream (OR_INT_SIZE * nelements);
  if (offset == ER_FAILED)
    {
      return ER_FAILED;
    }

  ptr = &xts_Stream_buffer[offset];
  for (i = 0; i < nelements; ++i)
    {
      ptr = or_pack_int (ptr, int_array[i]);
    }

  return offset;
}

static int
xts_save_hfid_array (HFID * hfid_array, int nelements)
{
  int offset, i;
  char *ptr;

  if (hfid_array == NULL)
    {
      return 0;
    }

  offset = xts_reserve_location_in_stream (OR_HFID_SIZE * nelements);
  if (offset == ER_FAILED)
    {
      return ER_FAILED;
    }

  ptr = &xts_Stream_buffer[offset];
  for (i = 0; i < nelements; ++i)
    {
      ptr = or_pack_hfid (ptr, &hfid_array[i]);
    }

  return offset;
}

static int
xts_save_oid_array (OID * oid_array, int nelements)
{
  int offset, i;
  char *ptr;

  if (oid_array == NULL)
    {
      return 0;
    }

  offset = xts_reserve_location_in_stream (OR_OID_SIZE * nelements);
  if (offset == ER_FAILED)
    {
      return ER_FAILED;
    }

  ptr = &xts_Stream_buffer[offset];
  for (i = 0; i < nelements; ++i)
    {
      ptr = or_pack_oid (ptr, &oid_array[i]);
    }

  return offset;
}

/*
 * Save the regu_variable_list as an array to avoid recursion in the server.
 * Pack the array size first, then the array.
 */
#define OFFSET_BUFFER_SIZE 32

static int
xts_save_regu_variable_list (const REGU_VARIABLE_LIST regu_var_list)
{
  int offset;
  int *regu_var_offset_table;
  int offset_local_buffer[OFFSET_BUFFER_SIZE];
  REGU_VARIABLE_LIST regu_var_p;
  int nelements, i;

  if (regu_var_list == NULL)
    {
      return 0;
    }

  offset = xts_get_offset_visited_ptr (regu_var_list);
  if (offset != ER_FAILED)
    {
      return offset;
    }

  nelements = 0;
  for (regu_var_p = regu_var_list; regu_var_p; regu_var_p = regu_var_p->next)
    {
      ++nelements;
    }

  if (OFFSET_BUFFER_SIZE <= nelements)
    {
      regu_var_offset_table = (int *) malloc (sizeof (int) * (nelements + 1));
      if (regu_var_offset_table == NULL)
	{
	  return ER_FAILED;
	}
    }
  else
    {
      regu_var_offset_table = offset_local_buffer;
    }

  i = 0;
  regu_var_offset_table[i++] = nelements;
  for (regu_var_p = regu_var_list; regu_var_p;
       ++i, regu_var_p = regu_var_p->next)
    {
      regu_var_offset_table[i] = xts_save_regu_variable (&regu_var_p->value);
      if (regu_var_offset_table[i] == ER_FAILED)
	{
	  if (regu_var_offset_table != offset_local_buffer)
	    {
	      free_and_init (regu_var_offset_table);
	    }
	  return ER_FAILED;
	}
    }

  offset = xts_save_int_array (regu_var_offset_table, nelements + 1);

  if (regu_var_offset_table != offset_local_buffer)
    {
      free_and_init (regu_var_offset_table);
    }

  if (offset == ER_FAILED
      || xts_mark_ptr_visited (regu_var_list, offset) == ER_FAILED)
    {
      return ER_FAILED;
    }

  return offset;
}

static int
xts_save_regu_varlist_list (const REGU_VARLIST_LIST regu_var_list_list)
{
  int offset;
  int *regu_var_list_offset_table;
  int offset_local_buffer[OFFSET_BUFFER_SIZE];
  REGU_VARLIST_LIST regu_var_list_p;
  int nelements, i;

  if (regu_var_list_list == NULL)
    {
      return 0;
    }

  offset = xts_get_offset_visited_ptr (regu_var_list_list);
  if (offset != ER_FAILED)
    {
      return offset;
    }

  nelements = 0;
  for (regu_var_list_p = regu_var_list_list; regu_var_list_p;
       regu_var_list_p = regu_var_list_p->next)
    {
      ++nelements;
    }

  if (OFFSET_BUFFER_SIZE <= nelements)
    {
      regu_var_list_offset_table =
	(int *) malloc (sizeof (int) * (nelements + 1));
      if (regu_var_list_offset_table == NULL)
	{
	  return ER_FAILED;
	}
    }
  else
    {
      regu_var_list_offset_table = offset_local_buffer;
    }

  i = 0;
  regu_var_list_offset_table[i++] = nelements;
  for (regu_var_list_p = regu_var_list_list; regu_var_list_p;
       ++i, regu_var_list_p = regu_var_list_p->next)
    {
      regu_var_list_offset_table[i] =
	xts_save_regu_variable_list (regu_var_list_p->list);
      if (regu_var_list_offset_table[i] == ER_FAILED)
	{
	  if (regu_var_list_offset_table != offset_local_buffer)
	    {
	      free_and_init (regu_var_list_offset_table);
	    }
	  return ER_FAILED;
	}
    }

  offset = xts_save_int_array (regu_var_list_offset_table, nelements + 1);

  if (regu_var_list_offset_table != offset_local_buffer)
    {
      free_and_init (regu_var_list_offset_table);
    }

  if (offset == ER_FAILED
      || xts_mark_ptr_visited (regu_var_list_list, offset) == ER_FAILED)
    {
      return ER_FAILED;
    }

  return offset;
}

static int
xts_save_key_range_array (const KEY_RANGE * key_range_array, int nelements)
{
  int offset, i, j;
  int *key_range_offset_table;

  if (key_range_array == NULL)
    {
      return 0;
    }

  offset = xts_get_offset_visited_ptr (key_range_array);
  if (offset != ER_FAILED)
    {
      return offset;
    }

  key_range_offset_table = (int *) malloc (sizeof (int) * 3 * nelements);
  if (key_range_offset_table == NULL)
    {
      return ER_FAILED;
    }

  for (i = 0, j = 0; i < nelements; i++, j++)
    {
      key_range_offset_table[j] = key_range_array[i].range;

      if (key_range_array[i].key1)
	{
	  key_range_offset_table[++j] =
	    xts_save_regu_variable (key_range_array[i].key1);
	  if (key_range_offset_table[j] == ER_FAILED)
	    {
	      free_and_init (key_range_offset_table);
	      return ER_FAILED;
	    }
	}
      else
	{
	  key_range_offset_table[++j] = 0;
	}

      if (key_range_array[i].key2)
	{
	  key_range_offset_table[++j] =
	    xts_save_regu_variable (key_range_array[i].key2);
	  if (key_range_offset_table[j] == ER_FAILED)
	    {
	      free_and_init (key_range_offset_table);
	      return ER_FAILED;
	    }
	}
      else
	{
	  key_range_offset_table[++j] = 0;
	}
    }

  offset = xts_save_int_array (key_range_offset_table, 3 * nelements);

  free_and_init (key_range_offset_table);

  return offset;
}

/*
 * xts_process_xasl_header () - Pack XASL node header in buffer.
 *
 * return      : buffer pointer after the packed XASL node header.
 * ptr (in)    : buffer pointer where XASL node header should be packed.
 * header (in) : XASL node header.
 */
static char *
xts_process_xasl_header (char *ptr, const XASL_NODE_HEADER header)
{
  if (ptr == NULL)
    {
      return NULL;
    }
  ASSERT_ALIGN (ptr, INT_ALIGNMENT);

  OR_PACK_XASL_NODE_HEADER (ptr, &header);
  return ptr;
};

static char *
xts_process_xasl_node (char *ptr, const XASL_NODE * xasl)
{
  int offset;
  int cnt;
  ACCESS_SPEC_TYPE *access_spec = NULL;

  assert (PTR_ALIGN (ptr, MAX_ALIGNMENT) == ptr);

  /* pack header first */
  ptr = xts_process_xasl_header (ptr, xasl->header);

  ptr = or_pack_int (ptr, xasl->type);

  ptr = or_pack_int (ptr, xasl->flag);

  if (xasl->list_id == NULL)
    {
      ptr = or_pack_int (ptr, 0);
    }
  else
    {
      offset = xts_save_list_id (xasl->list_id);
      if (offset == ER_FAILED)
	{
	  return NULL;
	}
      ptr = or_pack_int (ptr, offset);
    }

  offset = xts_save_sort_list (xasl->after_iscan_list);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  offset = xts_save_sort_list (xasl->orderby_list);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  offset = xts_save_pred_expr (xasl->ordbynum_pred);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  offset = xts_save_db_value (xasl->ordbynum_val);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  offset = xts_save_regu_variable (xasl->orderby_limit);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  ptr = or_pack_int (ptr, xasl->ordbynum_flag);

  offset = xts_save_regu_variable (xasl->limit_row_count);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  offset = xts_save_val_list (xasl->single_tuple);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  ptr = or_pack_int (ptr, xasl->is_single_tuple);

  ptr = or_pack_int (ptr, xasl->option);

  offset = xts_save_outptr_list (xasl->outptr_list);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  offset = xts_save_selupd_list (xasl->selected_upd_list);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  for (cnt = 0, access_spec = xasl->spec_list; access_spec;
       access_spec = access_spec->next, cnt++)
    ;				/* empty */
  ptr = or_pack_int (ptr, cnt);

  for (access_spec = xasl->spec_list; access_spec;
       access_spec = access_spec->next)
    {
      ptr = xts_process_access_spec_type (ptr, access_spec);
    }

  for (cnt = 0, access_spec = xasl->merge_spec; access_spec;
       access_spec = access_spec->next, cnt++)
    ;				/* empty */
  ptr = or_pack_int (ptr, cnt);

  for (access_spec = xasl->merge_spec; access_spec;
       access_spec = access_spec->next)
    {
      ptr = xts_process_access_spec_type (ptr, access_spec);
    }

  offset = xts_save_val_list (xasl->val_list);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  offset = xts_save_val_list (xasl->merge_val_list);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  offset = xts_save_xasl_node (xasl->aptr_list);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  offset = xts_save_xasl_node (xasl->bptr_list);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  offset = xts_save_xasl_node (xasl->dptr_list);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  offset = xts_save_pred_expr (xasl->after_join_pred);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  offset = xts_save_pred_expr (xasl->if_pred);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  offset = xts_save_pred_expr (xasl->instnum_pred);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  offset = xts_save_db_value (xasl->instnum_val);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  offset = xts_save_db_value (xasl->save_instnum_val);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  ptr = or_pack_int (ptr, xasl->instnum_flag);

  offset = xts_save_xasl_node (xasl->fptr_list);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  offset = xts_save_xasl_node (xasl->scan_ptr);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  offset = xts_save_xasl_node (xasl->connect_by_ptr);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  offset = xts_save_db_value (xasl->level_val);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  offset = xts_save_regu_variable (xasl->level_regu);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  offset = xts_save_db_value (xasl->isleaf_val);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  offset = xts_save_regu_variable (xasl->isleaf_regu);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  offset = xts_save_db_value (xasl->iscycle_val);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  offset = xts_save_regu_variable (xasl->iscycle_regu);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  for (cnt = 0, access_spec = xasl->curr_spec; access_spec;
       access_spec = access_spec->next, cnt++)
    ;				/* empty */
  ptr = or_pack_int (ptr, cnt);

  for (access_spec = xasl->curr_spec; access_spec;
       access_spec = access_spec->next)
    {
      ptr = xts_process_access_spec_type (ptr, access_spec);
    }

  ptr = or_pack_int (ptr, xasl->next_scan_on);

  ptr = or_pack_int (ptr, xasl->next_scan_block_on);

  ptr = or_pack_int (ptr, xasl->cat_fetched);

  ptr = or_pack_int (ptr, (int) xasl->scan_op_type);

  ptr = or_pack_int (ptr, xasl->upd_del_class_cnt);

  ptr = or_pack_int (ptr, xasl->mvcc_reev_extra_cls_cnt);

  /*
   * NOTE that the composite lock block is strictly a server side block
   * and is not packed.
   */

  switch (xasl->type)
    {
    case BUILDLIST_PROC:
      ptr = xts_process_buildlist_proc (ptr, &xasl->proc.buildlist);
      break;

    case BUILDVALUE_PROC:
      ptr = xts_process_buildvalue_proc (ptr, &xasl->proc.buildvalue);
      break;

    case MERGELIST_PROC:
      ptr = xts_process_mergelist_proc (ptr, &xasl->proc.mergelist);
      break;

    case CONNECTBY_PROC:
      ptr = xts_process_connectby_proc (ptr, &xasl->proc.connect_by);
      break;

    case UPDATE_PROC:
      ptr = xts_process_update_proc (ptr, &xasl->proc.update);
      break;

    case DELETE_PROC:
      ptr = xts_process_delete_proc (ptr, &xasl->proc.delete_);
      break;

    case INSERT_PROC:
      ptr = xts_process_insert_proc (ptr, &xasl->proc.insert);
      break;

    case UNION_PROC:
    case DIFFERENCE_PROC:
    case INTERSECTION_PROC:
      ptr = xts_process_union_proc (ptr, &xasl->proc.union_);
      break;

    case OBJFETCH_PROC:
      ptr = xts_process_fetch_proc (ptr, &xasl->proc.fetch);
      break;

    case SCAN_PROC:
      break;

    case DO_PROC:
      break;

    case MERGE_PROC:
      ptr = xts_process_merge_proc (ptr, &xasl->proc.merge);
      break;

    case BUILD_SCHEMA_PROC:
      break;

    default:
      xts_Xasl_errcode = ER_QPROC_INVALID_XASLNODE;
      return NULL;
    }

  if (ptr == NULL)
    {
      return NULL;
    }

  ptr = or_pack_int (ptr, xasl->projected_size);
  ptr = or_pack_double (ptr, xasl->cardinality);

  ptr = or_pack_int (ptr, (int) xasl->iscan_oid_order);

  if (xasl->query_alias)
    {
      offset = xts_save_string (xasl->query_alias);
    }
  else
    {
      offset = xts_save_string ("*** EMPTY QUERY ***");
    }
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  offset = xts_save_xasl_node (xasl->next);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  return ptr;
}

static char *
xts_process_filter_pred_node (char *ptr, const PRED_EXPR_WITH_CONTEXT * pred)
{
  int offset;

  offset = xts_save_pred_expr (pred->pred);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  ptr = or_pack_int (ptr, pred->num_attrs_pred);
  offset = xts_save_int_array (pred->attrids_pred, pred->num_attrs_pred);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  offset = xts_save_cache_attrinfo (pred->cache_pred);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  return ptr;
}

static char *
xts_process_func_pred (char *ptr, const FUNC_PRED * func_pred)
{
  int offset;

  offset = xts_save_regu_variable (func_pred->func_regu);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  offset = xts_save_cache_attrinfo (func_pred->cache_attrinfo);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  return ptr;
}

static char *
xts_process_cache_attrinfo (char *ptr)
{
  /*
   * We don't need to pack anything here, it is strictly a server side
   * structure.  Unfortunately, we must send something or else the ptrs
   * to this structure might conflict with a structure that might be
   * packed after it.  To avoid this we'll pack a single zero.
   */
  ptr = or_pack_int (ptr, 0);

  return ptr;
}

static char *
xts_process_union_proc (char *ptr, const UNION_PROC_NODE * union_proc)
{
  int offset;

  offset = xts_save_xasl_node (union_proc->left);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  offset = xts_save_xasl_node (union_proc->right);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  return ptr;
}

static char *
xts_process_fetch_proc (char *ptr, const FETCH_PROC_NODE * obj_set_fetch_proc)
{
  int offset;

  offset = xts_save_db_value (obj_set_fetch_proc->arg);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  ptr = or_pack_int (ptr, (int) obj_set_fetch_proc->fetch_res);

  offset = xts_save_pred_expr (obj_set_fetch_proc->set_pred);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  ptr = or_pack_int (ptr, (int) obj_set_fetch_proc->ql_flag);

  return ptr;
}

static char *
xts_process_buildlist_proc (char *ptr,
			    const BUILDLIST_PROC_NODE * build_list_proc)
{
  int offset;

  /* ptr->output_columns not sent to server */

  offset = xts_save_xasl_node (build_list_proc->eptr_list);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  offset = xts_save_sort_list (build_list_proc->groupby_list);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  offset = xts_save_sort_list (build_list_proc->after_groupby_list);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  if (build_list_proc->push_list_id == NULL)
    {
      ptr = or_pack_int (ptr, 0);
    }
  else
    {
      offset = xts_save_list_id (build_list_proc->push_list_id);
      if (offset == ER_FAILED)
	{
	  return NULL;
	}
      ptr = or_pack_int (ptr, offset);
    }

  offset = xts_save_outptr_list (build_list_proc->g_outptr_list);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  offset = xts_save_regu_variable_list (build_list_proc->g_regu_list);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  offset = xts_save_val_list (build_list_proc->g_val_list);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  offset = xts_save_pred_expr (build_list_proc->g_having_pred);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  offset = xts_save_pred_expr (build_list_proc->g_grbynum_pred);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  offset = xts_save_db_value (build_list_proc->g_grbynum_val);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  ptr = or_pack_int (ptr, build_list_proc->g_hash_eligible);

  ptr = or_pack_int (ptr, build_list_proc->g_output_first_tuple);
  ptr = or_pack_int (ptr, build_list_proc->g_hkey_size);

  offset = xts_save_regu_variable_list (build_list_proc->g_hk_scan_regu_list);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  offset = xts_save_regu_variable_list (build_list_proc->g_hk_sort_regu_list);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  offset = xts_save_regu_variable_list (build_list_proc->g_scan_regu_list);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  ptr = or_pack_int (ptr, build_list_proc->g_func_count);
  ptr = or_pack_int (ptr, build_list_proc->g_grbynum_flag);
  ptr = or_pack_int (ptr, build_list_proc->g_with_rollup);

  offset = xts_save_aggregate_type (build_list_proc->g_agg_list);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  offset = xts_save_arith_type (build_list_proc->g_outarith_list);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  offset = xts_save_analytic_eval_type (build_list_proc->a_eval_list);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  offset = xts_save_regu_variable_list (build_list_proc->a_regu_list);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  offset = xts_save_outptr_list (build_list_proc->a_outptr_list);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  offset = xts_save_outptr_list (build_list_proc->a_outptr_list_ex);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  offset = xts_save_outptr_list (build_list_proc->a_outptr_list_interm);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  offset = xts_save_val_list (build_list_proc->a_val_list);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  offset = xts_save_pred_expr (build_list_proc->a_instnum_pred);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  offset = xts_save_db_value (build_list_proc->a_instnum_val);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  ptr = or_pack_int (ptr, build_list_proc->a_instnum_flag);

  return ptr;
}

static char *
xts_process_buildvalue_proc (char *ptr,
			     const BUILDVALUE_PROC_NODE * build_value_proc)
{
  int offset;

  offset = xts_save_pred_expr (build_value_proc->having_pred);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  offset = xts_save_db_value (build_value_proc->grbynum_val);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  offset = xts_save_aggregate_type (build_value_proc->agg_list);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  offset = xts_save_arith_type (build_value_proc->outarith_list);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  ptr = or_pack_int (ptr, build_value_proc->is_always_false);

  return ptr;
}

static char *
xts_process_mergelist_proc (char *ptr,
			    const MERGELIST_PROC_NODE * merge_list_info)
{
  int offset;
  int cnt;
  ACCESS_SPEC_TYPE *access_spec = NULL;

  offset = xts_save_xasl_node (merge_list_info->outer_xasl);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  for (cnt = 0, access_spec = merge_list_info->outer_spec_list; access_spec;
       access_spec = access_spec->next, cnt++)
    ;				/* empty */
  ptr = or_pack_int (ptr, cnt);

  for (access_spec = merge_list_info->outer_spec_list; access_spec;
       access_spec = access_spec->next)
    {
      ptr = xts_process_access_spec_type (ptr, access_spec);
    }

  offset = xts_save_val_list (merge_list_info->outer_val_list);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  offset = xts_save_xasl_node (merge_list_info->inner_xasl);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  for (cnt = 0, access_spec = merge_list_info->inner_spec_list; access_spec;
       access_spec = access_spec->next, cnt++)
    ;				/* empty */
  ptr = or_pack_int (ptr, cnt);

  for (access_spec = merge_list_info->inner_spec_list; access_spec;
       access_spec = access_spec->next)
    {
      ptr = xts_process_access_spec_type (ptr, access_spec);
    }

  offset = xts_save_val_list (merge_list_info->inner_val_list);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  return xts_process_ls_merge_info (ptr, &merge_list_info->ls_merge);
}

static char *
xts_process_ls_merge_info (char *ptr,
			   const QFILE_LIST_MERGE_INFO *
			   qfile_list_merge_info)
{
  int offset;

  ptr = or_pack_int (ptr, qfile_list_merge_info->join_type);

  ptr = or_pack_int (ptr, qfile_list_merge_info->single_fetch);

  ptr = or_pack_int (ptr, qfile_list_merge_info->ls_column_cnt);

  offset = xts_save_int_array (qfile_list_merge_info->ls_outer_column,
			       qfile_list_merge_info->ls_column_cnt);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  offset = xts_save_int_array (qfile_list_merge_info->ls_outer_unique,
			       qfile_list_merge_info->ls_column_cnt);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  offset = xts_save_int_array (qfile_list_merge_info->ls_inner_column,
			       qfile_list_merge_info->ls_column_cnt);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  offset = xts_save_int_array (qfile_list_merge_info->ls_inner_unique,
			       qfile_list_merge_info->ls_column_cnt);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  ptr = or_pack_int (ptr, qfile_list_merge_info->ls_pos_cnt);

  offset = xts_save_int_array (qfile_list_merge_info->ls_pos_list,
			       qfile_list_merge_info->ls_pos_cnt);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  offset = xts_save_int_array (qfile_list_merge_info->ls_outer_inner_list,
			       qfile_list_merge_info->ls_pos_cnt);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  return ptr;
}

static char *
xts_save_upddel_class_info (char *ptr, const UPDDEL_CLASS_INFO * upd_cls)
{
  int offset = 0;
  int i;
  char *p;

  /* no_subclasses */
  ptr = or_pack_int (ptr, upd_cls->no_subclasses);

  /* class_oid */
  offset = xts_save_oid_array (upd_cls->class_oid, upd_cls->no_subclasses);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  /* class_hfid */
  offset = xts_save_hfid_array (upd_cls->class_hfid, upd_cls->no_subclasses);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  /* no_attrs */
  ptr = or_pack_int (ptr, upd_cls->no_attrs);

  /* att_id */
  offset =
    xts_save_int_array (upd_cls->att_id,
			upd_cls->no_attrs * upd_cls->no_subclasses);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);
  /* pruning info */
  ptr = or_pack_int (ptr, upd_cls->needs_pruning);
  /* has_uniques */
  ptr = or_pack_int (ptr, upd_cls->has_uniques);

  /* make sure no_lob_attrs and lob_attr_ids are both NULL or are both not
   * NULL
   */
  assert ((upd_cls->no_lob_attrs && upd_cls->lob_attr_ids)
	  || (!upd_cls->no_lob_attrs && !upd_cls->lob_attr_ids));
  /* no_lob_attrs */
  offset = xts_save_int_array (upd_cls->no_lob_attrs, upd_cls->no_subclasses);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);
  /* lob_attr_ids */
  if (upd_cls->lob_attr_ids)
    {
      offset =
	xts_reserve_location_in_stream (upd_cls->no_subclasses *
					sizeof (int));
      if (offset == ER_FAILED)
	{
	  return NULL;
	}
      ptr = or_pack_int (ptr, offset);
      p = &xts_Stream_buffer[offset];
      for (i = 0; i < upd_cls->no_subclasses; i++)
	{
	  offset = xts_save_int_array (upd_cls->lob_attr_ids[i],
				       upd_cls->no_lob_attrs[i]);
	  if (offset == ER_FAILED)
	    {
	      return NULL;
	    }
	  p = or_pack_int (p, offset);
	}
    }
  else
    {
      ptr = or_pack_int (ptr, 0);
    }

  /* no of indexes in mvcc assignments extra classes */
  ptr = or_pack_int (ptr, upd_cls->no_extra_assign_reev);

  /* mvcc assignments extra classes */
  offset =
    xts_save_int_array (upd_cls->mvcc_extra_assign_reev,
			upd_cls->no_extra_assign_reev);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  return ptr;
}

static int
xts_save_upddel_class_info_array (const UPDDEL_CLASS_INFO * classes,
				  int nelements)
{
  char *ptr = NULL, *buf = NULL;
  int idx, offset = ER_FAILED;
  int size = xts_sizeof_upddel_class_info (classes) * nelements;

  assert (nelements > 0);
  assert (size > 0);

  offset = xts_reserve_location_in_stream (size);
  if (offset == ER_FAILED)
    {
      offset = ER_FAILED;
      goto end;
    }

  ptr = buf = (char *) malloc (size);
  if (buf == NULL)
    {
      offset = ER_OUT_OF_VIRTUAL_MEMORY;
      goto end;
    }

  for (idx = 0; idx < nelements; idx++)
    {
      ptr = xts_save_upddel_class_info (ptr, &classes[idx]);
      if (ptr == NULL)
	{
	  offset = ER_FAILED;
	  goto end;
	}
    }
  memcpy (&xts_Stream_buffer[offset], buf, size);

end:
  if (buf != NULL)
    {
      free_and_init (buf);
    }

  return offset;
}

static char *
xts_save_update_assignment (char *ptr, const UPDATE_ASSIGNMENT * assign)
{
  int offset = 0;

  /* cls_idx */
  ptr = or_pack_int (ptr, assign->cls_idx);

  /* att_idx */
  ptr = or_pack_int (ptr, assign->att_idx);

  /* constant */
  if (assign->constant)
    {
      offset = xts_save_db_value (assign->constant);
      if (offset == ER_FAILED)
	{
	  return NULL;
	}
    }
  else
    {
      offset = 0;
    }
  ptr = or_pack_int (ptr, offset);

  /* regu_var */
  if (assign->regu_var)
    {
      offset = xts_save_regu_variable (assign->regu_var);
      if (offset == ER_FAILED)
	{
	  return NULL;
	}
    }
  else
    {
      offset = 0;
    }
  ptr = or_pack_int (ptr, offset);

  return ptr;
}

static int
xts_save_update_assignment_array (const UPDATE_ASSIGNMENT * assigns,
				  int nelements)
{
  char *ptr = NULL, *buf = NULL;
  int offset = ER_FAILED, idx;
  int size = xts_sizeof_update_assignment (assigns) * nelements;

  assert (nelements > 0);
  assert (size > 0);

  offset = xts_reserve_location_in_stream (size);
  if (offset == ER_FAILED)
    {
      offset = ER_FAILED;
      goto end;
    }

  ptr = buf = (char *) malloc (size);
  if (buf == NULL)
    {
      offset = ER_OUT_OF_VIRTUAL_MEMORY;
      goto end;
    }

  for (idx = 0; idx < nelements; idx++)
    {
      ptr = xts_save_update_assignment (ptr, &assigns[idx]);
      if (ptr == NULL)
	{
	  offset = ER_FAILED;
	  goto end;
	}
    }
  memcpy (&xts_Stream_buffer[offset], buf, size);

end:
  if (buf != NULL)
    {
      free_and_init (buf);
    }

  return offset;
}

static int
xts_save_odku_info (const ODKU_INFO * odku_info)
{
  int offset, return_offset;
  int size;
  char *ptr = NULL, *buf = NULL;
  int error = NO_ERROR;

  if (odku_info == NULL)
    {
      return 0;
    }

  size = xts_sizeof_odku_info (odku_info);

  return_offset = xts_reserve_location_in_stream (size);
  if (return_offset == ER_FAILED)
    {
      return ER_FAILED;
    }

  ptr = buf = (char *) malloc (size);
  if (!buf)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
	      size);
      return ER_FAILED;
    }

  /* no_assigns */
  ptr = or_pack_int (ptr, odku_info->no_assigns);

  /* attr_ids */
  offset = xts_save_int_array (odku_info->attr_ids, odku_info->no_assigns);
  if (offset == ER_FAILED)
    {
      goto end;
    }
  ptr = or_pack_int (ptr, offset);

  /* assignments */
  offset =
    xts_save_update_assignment_array (odku_info->assignments,
				      odku_info->no_assigns);
  if (offset == ER_FAILED)
    {
      goto end;
    }
  ptr = or_pack_int (ptr, offset);

  /* constraint predicate */
  if (odku_info->cons_pred)
    {
      offset = xts_save_pred_expr (odku_info->cons_pred);
      if (offset == ER_FAILED)
	{
	  goto end;
	}
    }
  else
    {
      offset = 0;
    }
  ptr = or_pack_int (ptr, offset);

  /* heap cache attr info */
  if (odku_info->attr_info)
    {
      offset = xts_save_cache_attrinfo (odku_info->attr_info);
      if (offset == ER_FAILED)
	{
	  goto end;
	}
    }
  else
    {
      offset = 0;
    }
  ptr = or_pack_int (ptr, offset);

  memcpy (&xts_Stream_buffer[return_offset], buf, size);
  offset = return_offset;

end:
  if (buf)
    {
      free_and_init (buf);
    }
  return offset;
}

static char *
xts_process_update_proc (char *ptr, const UPDATE_PROC_NODE * update_info)
{
  int offset;

  /* classes */
  ptr = or_pack_int (ptr, update_info->no_classes);
  offset =
    xts_save_upddel_class_info_array (update_info->classes,
				      update_info->no_classes);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  /* assigns */
  ptr = or_pack_int (ptr, update_info->no_assigns);
  offset =
    xts_save_update_assignment_array (update_info->assigns,
				      update_info->no_assigns);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  /* cons_pred */
  offset = xts_save_pred_expr (update_info->cons_pred);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  /* wait_msecs */
  ptr = or_pack_int (ptr, update_info->wait_msecs);

  /* no_logging */
  ptr = or_pack_int (ptr, update_info->no_logging);

  /* release_lock */
  ptr = or_pack_int (ptr, update_info->release_lock);

  /* no_orderby_keys */
  ptr = or_pack_int (ptr, update_info->no_orderby_keys);

  /* no_assign_reev_classes */
  ptr = or_pack_int (ptr, update_info->no_assign_reev_classes);

  /* mvcc condition reevaluation data */
  ptr = or_pack_int (ptr, update_info->no_reev_classes);
  if (update_info->no_reev_classes)
    {
      offset =
	xts_save_int_array (update_info->mvcc_reev_classes,
			    update_info->no_reev_classes);
      if (offset == ER_FAILED)
	{
	  return NULL;
	}
    }
  else
    {
      offset = 0;
    }
  ptr = or_pack_int (ptr, offset);

  return ptr;
}

static char *
xts_process_delete_proc (char *ptr, const DELETE_PROC_NODE * delete_info)
{
  int offset;

  ptr = or_pack_int (ptr, delete_info->no_classes);

  offset =
    xts_save_upddel_class_info_array (delete_info->classes,
				      delete_info->no_classes);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  ptr = or_pack_int (ptr, delete_info->wait_msecs);

  ptr = or_pack_int (ptr, delete_info->no_logging);

  ptr = or_pack_int (ptr, delete_info->release_lock);

  /* mvcc condition reevaluation data */
  ptr = or_pack_int (ptr, delete_info->no_reev_classes);
  if (delete_info->no_reev_classes)
    {
      offset =
	xts_save_int_array (delete_info->mvcc_reev_classes,
			    delete_info->no_reev_classes);
      if (offset == ER_FAILED)
	{
	  return NULL;
	}
    }
  else
    {
      offset = 0;
    }
  ptr = or_pack_int (ptr, offset);

  return ptr;
}

static char *
xts_process_insert_proc (char *ptr, const INSERT_PROC_NODE * insert_info)
{
  int offset, i;

  ptr = or_pack_oid (ptr, (OID *) & insert_info->class_oid);

  ptr = or_pack_hfid (ptr, &insert_info->class_hfid);

  ptr = or_pack_int (ptr, insert_info->no_vals);

  ptr = or_pack_int (ptr, insert_info->no_default_expr);

  offset = xts_save_int_array (insert_info->att_id, insert_info->no_vals);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  offset = xts_save_pred_expr (insert_info->cons_pred);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  ptr = or_pack_int (ptr, insert_info->has_uniques);

  ptr = or_pack_int (ptr, insert_info->wait_msecs);

  ptr = or_pack_int (ptr, insert_info->no_logging);

  ptr = or_pack_int (ptr, insert_info->release_lock);

  ptr = or_pack_int (ptr, insert_info->do_replace);

  ptr = or_pack_int (ptr, insert_info->pruning_type);

  offset = xts_save_odku_info (insert_info->odku);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  ptr = or_pack_int (ptr, insert_info->no_val_lists);

  for (i = 0; i < insert_info->no_val_lists; i++)
    {
      offset = xts_save_outptr_list (insert_info->valptr_lists[i]);
      if (offset == ER_FAILED)
	{
	  return NULL;
	}
      ptr = or_pack_int (ptr, offset);
    }

  offset = xts_save_db_value (insert_info->obj_oid);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  return ptr;
}

static char *
xts_process_merge_proc (char *ptr, const MERGE_PROC_NODE * merge_info)
{
  int offset;

  offset = xts_save_xasl_node (merge_info->update_xasl);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  offset = xts_save_xasl_node (merge_info->insert_xasl);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  ptr = or_pack_int (ptr, merge_info->has_delete);

  return ptr;
}

static char *
xts_process_outptr_list (char *ptr, const OUTPTR_LIST * outptr_list)
{
  int offset;

  ptr = or_pack_int (ptr, outptr_list->valptr_cnt);

  offset = xts_save_regu_variable_list (outptr_list->valptrp);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  return ptr;
}

static char *
xts_process_selupd_list (char *ptr, const SELUPD_LIST * selupd_list)
{
  int offset;

  ptr = or_pack_oid (ptr, (OID *) & selupd_list->class_oid);

  ptr = or_pack_hfid (ptr, &selupd_list->class_hfid);

  ptr = or_pack_int (ptr, selupd_list->select_list_size);

  ptr = or_pack_int (ptr, selupd_list->wait_msecs);

  offset = xts_save_regu_varlist_list (selupd_list->select_list);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  offset = xts_save_selupd_list (selupd_list->next);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  return ptr;
}

static char *
xts_process_pred_expr (char *ptr, const PRED_EXPR * pred_expr)
{
  int offset;

  ptr = or_pack_int (ptr, pred_expr->type);

  switch (pred_expr->type)
    {
    case T_PRED:
      ptr = xts_process_pred (ptr, &pred_expr->pe.pred);
      break;

    case T_EVAL_TERM:
      ptr = xts_process_eval_term (ptr, &pred_expr->pe.eval_term);
      break;

    case T_NOT_TERM:
      offset = xts_save_pred_expr (pred_expr->pe.not_term);
      if (offset == ER_FAILED)
	{
	  return NULL;
	}
      ptr = or_pack_int (ptr, offset);
      break;

    default:
      xts_Xasl_errcode = ER_QPROC_INVALID_XASLNODE;
      return NULL;
    }

  return ptr;
}

static char *
xts_process_pred (char *ptr, const PRED * pred)
{
  int offset;
  PRED_EXPR *rhs;

  offset = xts_save_pred_expr (pred->lhs);	/* lhs */
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  ptr = or_pack_int (ptr, pred->bool_op);	/* bool_op */

  rhs = pred->rhs;
  ptr = or_pack_int (ptr, rhs->type);	/* rhs-type */

  /* Traverse right-linear chains of AND/OR terms */
  while (rhs->type == T_PRED)
    {
      pred = &rhs->pe.pred;

      offset = xts_save_pred_expr (pred->lhs);	/* lhs */
      if (offset == ER_FAILED)
	{
	  return NULL;
	}
      ptr = or_pack_int (ptr, offset);

      ptr = or_pack_int (ptr, pred->bool_op);	/* bool_op */

      rhs = pred->rhs;
      ptr = or_pack_int (ptr, rhs->type);	/* rhs-type */
    }

  offset = xts_save_pred_expr (pred->rhs);	/* rhs */
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  return ptr;
}

static char *
xts_process_eval_term (char *ptr, const EVAL_TERM * eval_term)
{
  ptr = or_pack_int (ptr, eval_term->et_type);

  switch (eval_term->et_type)
    {
    case T_COMP_EVAL_TERM:
      ptr = xts_process_comp_eval_term (ptr, &eval_term->et.et_comp);
      break;

    case T_ALSM_EVAL_TERM:
      ptr = xts_process_alsm_eval_term (ptr, &eval_term->et.et_alsm);
      break;

    case T_LIKE_EVAL_TERM:
      ptr = xts_process_like_eval_term (ptr, &eval_term->et.et_like);
      break;

    case T_RLIKE_EVAL_TERM:
      ptr = xts_process_rlike_eval_term (ptr, &eval_term->et.et_rlike);
      break;

    default:
      xts_Xasl_errcode = ER_QPROC_INVALID_XASLNODE;
      return NULL;
    }

  return ptr;
}

static char *
xts_process_comp_eval_term (char *ptr, const COMP_EVAL_TERM * comp_eval_term)
{
  int offset;

  offset = xts_save_regu_variable (comp_eval_term->lhs);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  offset = xts_save_regu_variable (comp_eval_term->rhs);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  ptr = or_pack_int (ptr, comp_eval_term->rel_op);

  ptr = or_pack_int (ptr, comp_eval_term->type);

  return ptr;
}

static char *
xts_process_alsm_eval_term (char *ptr, const ALSM_EVAL_TERM * alsm_eval_term)
{
  int offset;

  offset = xts_save_regu_variable (alsm_eval_term->elem);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  offset = xts_save_regu_variable (alsm_eval_term->elemset);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  ptr = or_pack_int (ptr, alsm_eval_term->eq_flag);

  ptr = or_pack_int (ptr, alsm_eval_term->rel_op);

  ptr = or_pack_int (ptr, alsm_eval_term->item_type);

  return ptr;
}

static char *
xts_process_like_eval_term (char *ptr, const LIKE_EVAL_TERM * like_eval_term)
{
  int offset;

  offset = xts_save_regu_variable (like_eval_term->src);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  offset = xts_save_regu_variable (like_eval_term->pattern);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  offset = xts_save_regu_variable (like_eval_term->esc_char);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  return ptr;
}

static char *
xts_process_rlike_eval_term (char *ptr,
			     const RLIKE_EVAL_TERM * rlike_eval_term)
{
  int offset;

  offset = xts_save_regu_variable (rlike_eval_term->src);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  offset = xts_save_regu_variable (rlike_eval_term->pattern);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  offset = xts_save_regu_variable (rlike_eval_term->case_sensitive);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  return ptr;
}

static char *
xts_process_access_spec_type (char *ptr, const ACCESS_SPEC_TYPE * access_spec)
{
  int offset;

  if (ptr == NULL)
    {
      return NULL;
    }

  ptr = or_pack_int (ptr, access_spec->type);

  ptr = or_pack_int (ptr, access_spec->access);

  if (access_spec->access == SEQUENTIAL
      || access_spec->access == SEQUENTIAL_RECORD_INFO
      || access_spec->access == SEQUENTIAL_PAGE_SCAN)
    {
      ptr = or_pack_int (ptr, 0);
    }
  else
    {
      offset = xts_save_indx_info (access_spec->indexptr);
      if (offset == ER_FAILED)
	{
	  return NULL;
	}

      ptr = or_pack_int (ptr, offset);
    }

  offset = xts_save_pred_expr (access_spec->where_key);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  offset = xts_save_pred_expr (access_spec->where_pred);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  offset = xts_save_pred_expr (access_spec->where_range);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  switch (access_spec->type)
    {
    case TARGET_CLASS:
    case TARGET_CLASS_ATTR:
      ptr = xts_process_cls_spec_type (ptr,
				       &ACCESS_SPEC_CLS_SPEC (access_spec));
      break;

    case TARGET_LIST:
      ptr = xts_process_list_spec_type (ptr,
					&ACCESS_SPEC_LIST_SPEC (access_spec));
      break;

    case TARGET_SHOWSTMT:
      ptr = xts_process_showstmt_spec_type (ptr,
					    &ACCESS_SPEC_SHOWSTMT_SPEC
					    (access_spec));
      break;

    case TARGET_REGUVAL_LIST:
      ptr =
	xts_process_rlist_spec_type (ptr,
				     &ACCESS_SPEC_LIST_SPEC (access_spec));
      break;

    case TARGET_SET:
      ptr = xts_process_set_spec_type (ptr,
				       &ACCESS_SPEC_SET_SPEC (access_spec));
      break;

    case TARGET_METHOD:
      ptr = xts_process_method_spec_type (ptr,
					  &ACCESS_SPEC_METHOD_SPEC
					  (access_spec));
      break;

    default:
      xts_Xasl_errcode = ER_QPROC_INVALID_XASLNODE;
      return NULL;
    }

  if (ptr == NULL)
    {
      return NULL;
    }

  /* ptr->s_id not sent to server */

  ptr = or_pack_int (ptr, access_spec->grouped_scan);

  ptr = or_pack_int (ptr, access_spec->fixed_scan);

  ptr = or_pack_int (ptr, access_spec->qualified_block);

  ptr = or_pack_int (ptr, access_spec->single_fetch);

  ptr = or_pack_int (ptr, access_spec->pruning_type);

  offset = xts_save_db_value (access_spec->s_dbval);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  ptr = or_pack_int (ptr, access_spec->flags);

  return ptr;
}

static char *
xts_process_indx_info (char *ptr, const INDX_INFO * indx_info)
{
  ptr = xts_process_indx_id (ptr, &indx_info->indx_id);
  if (ptr == NULL)
    {
      return NULL;
    }

  ptr = or_pack_int (ptr, indx_info->coverage);

  ptr = or_pack_int (ptr, indx_info->range_type);

  ptr = xts_process_key_info (ptr, &indx_info->key_info);

  ptr = or_pack_int (ptr, indx_info->orderby_desc);

  ptr = or_pack_int (ptr, indx_info->groupby_desc);

  ptr = or_pack_int (ptr, indx_info->use_desc_index);

  ptr = or_pack_int (ptr, indx_info->orderby_skip);

  ptr = or_pack_int (ptr, indx_info->groupby_skip);

  ptr = or_pack_int (ptr, indx_info->use_iss);

  ptr = or_pack_int (ptr, indx_info->ils_prefix_len);

  ptr = or_pack_int (ptr, indx_info->func_idx_col_id);

  if (indx_info->use_iss)
    {
      int offset;

      ptr = or_pack_int (ptr, (int) indx_info->iss_range.range);

      offset = xts_save_regu_variable (indx_info->iss_range.key1);
      if (offset == ER_FAILED)
	{
	  return NULL;
	}
      ptr = or_pack_int (ptr, offset);

      /* Key 2 is ALWAYS NULL (see pt_create_iss_range(), so we do not
       * stream it */
    }
  else
    {
#if !defined(NDEBUG)
      /* suppress valgrind UMW error */
      ptr = or_pack_int (ptr, 0);	/* dummy indx_info->iss_range.range */
      ptr = or_pack_int (ptr, 0);	/* dummp offset of iss_range.key1 */
#endif
    }

  return ptr;
}

static char *
xts_process_indx_id (char *ptr, const INDX_ID * indx_id)
{
  ptr = or_pack_int (ptr, indx_id->type);

  switch (indx_id->type)
    {
    case T_BTID:
      ptr = or_pack_btid (ptr, (BTID *) & indx_id->i.btid);
      break;

    case T_EHID:
      ptr = or_pack_ehid (ptr, (EHID *) & indx_id->i.ehid);
      break;

    default:
      xts_Xasl_errcode = ER_QPROC_INVALID_XASLNODE;
      return NULL;
    }

  return ptr;
}

static char *
xts_process_key_info (char *ptr, const KEY_INFO * key_info)
{
  int offset;

  ptr = or_pack_int (ptr, key_info->key_cnt);

  if (key_info->key_cnt > 0)
    {
      offset = xts_save_key_range_array (key_info->key_ranges,
					 key_info->key_cnt);
      if (offset == ER_FAILED)
	{
	  return NULL;
	}
      ptr = or_pack_int (ptr, offset);
    }
  else
    {
      ptr = or_pack_int (ptr, 0);
    }

  ptr = or_pack_int (ptr, key_info->is_constant);

  offset = xts_save_regu_variable (key_info->key_limit_l);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  offset = xts_save_regu_variable (key_info->key_limit_u);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  ptr = or_pack_int (ptr, key_info->key_limit_reset);

  return ptr;
}

static char *
xts_process_cls_spec_type (char *ptr, const CLS_SPEC_TYPE * cls_spec)
{
  int offset;

  ptr = or_pack_hfid (ptr, &cls_spec->hfid);

  ptr = or_pack_oid (ptr, (OID *) & cls_spec->cls_oid);

  offset = xts_save_regu_variable_list (cls_spec->cls_regu_list_key);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  offset = xts_save_regu_variable_list (cls_spec->cls_regu_list_pred);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  offset = xts_save_regu_variable_list (cls_spec->cls_regu_list_rest);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  offset = xts_save_regu_variable_list (cls_spec->cls_regu_list_range);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  offset = xts_save_regu_variable_list (cls_spec->cls_regu_list_last_version);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  offset = xts_save_outptr_list (cls_spec->cls_output_val_list);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  offset = xts_save_regu_variable_list (cls_spec->cls_regu_val_list);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  ptr = or_pack_int (ptr, cls_spec->num_attrs_key);

  offset = xts_save_int_array (cls_spec->attrids_key,
			       cls_spec->num_attrs_key);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  offset = xts_save_cache_attrinfo (cls_spec->cache_key);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  ptr = or_pack_int (ptr, cls_spec->num_attrs_pred);

  offset = xts_save_int_array (cls_spec->attrids_pred,
			       cls_spec->num_attrs_pred);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  offset = xts_save_cache_attrinfo (cls_spec->cache_pred);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  ptr = or_pack_int (ptr, cls_spec->num_attrs_rest);

  offset = xts_save_int_array (cls_spec->attrids_rest,
			       cls_spec->num_attrs_rest);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  offset = xts_save_cache_attrinfo (cls_spec->cache_rest);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  ptr = or_pack_int (ptr, cls_spec->schema_type);

  ptr = or_pack_int (ptr, cls_spec->num_attrs_reserved);

  offset =
    xts_save_db_value_array (cls_spec->cache_reserved,
			     cls_spec->num_attrs_reserved);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  offset = xts_save_regu_variable_list (cls_spec->cls_regu_list_reserved);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  ptr = or_pack_int (ptr, cls_spec->num_attrs_range);

  offset = xts_save_int_array (cls_spec->attrids_range,
			       cls_spec->num_attrs_range);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  offset = xts_save_cache_attrinfo (cls_spec->cache_range);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  return ptr;
}

static char *
xts_process_list_spec_type (char *ptr, const LIST_SPEC_TYPE * list_spec)
{
  int offset;

  offset = xts_save_xasl_node (list_spec->xasl_node);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  offset = xts_save_regu_variable_list (list_spec->list_regu_list_pred);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  offset = xts_save_regu_variable_list (list_spec->list_regu_list_rest);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  return ptr;
}

static char *
xts_process_showstmt_spec_type (char *ptr,
				const SHOWSTMT_SPEC_TYPE * showstmt_spec)
{
  int offset;

  ptr = or_pack_int (ptr, showstmt_spec->show_type);

  offset = xts_save_regu_variable_list (showstmt_spec->arg_list);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  return ptr;
}

static char *
xts_process_rlist_spec_type (char *ptr, const LIST_SPEC_TYPE * list_spec)
{
  /* here, currently empty implementation,
   * actually, it can do some extra info save.
   */
  return ptr;
}

static char *
xts_process_set_spec_type (char *ptr, const SET_SPEC_TYPE * set_spec)
{
  int offset;

  offset = xts_save_regu_variable (set_spec->set_ptr);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  offset = xts_save_regu_variable_list (set_spec->set_regu_list);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  return ptr;
}

static char *
xts_process_method_spec_type (char *ptr, const METHOD_SPEC_TYPE * method_spec)
{
  int offset;

  offset = xts_save_xasl_node (method_spec->xasl_node);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  offset = xts_save_regu_variable_list (method_spec->method_regu_list);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  offset = xts_save_method_sig_list (method_spec->method_sig_list);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  return ptr;
}

static char *
xts_process_list_id (char *ptr, const QFILE_LIST_ID * list_id)
{
  /* is from client to server */
  assert_release (list_id->type_list.type_cnt == 0);
  assert_release (list_id->type_list.domp == NULL);

  return or_pack_listid (ptr, (void *) list_id);
}

static char *
xts_process_val_list (char *ptr, const VAL_LIST * val_list)
{
  int offset;
  int i;
  QPROC_DB_VALUE_LIST p;

  ptr = or_pack_int (ptr, val_list->val_cnt);

  for (i = 0, p = val_list->valp; i < val_list->val_cnt; i++, p = p->next)
    {
      offset = xts_save_db_value (p->val);
      if (offset == ER_FAILED)
	{
	  return NULL;
	}

      ptr = or_pack_int (ptr, offset);
    }

  return ptr;
}

static char *
xts_process_regu_variable (char *ptr, const REGU_VARIABLE * regu_var)
{
  int offset;

  /* we prepend the domain before we pack the regu_variable */
  ptr = OR_PACK_DOMAIN_OBJECT_TO_OID (ptr, regu_var->domain, 0, 0);

  ptr = or_pack_int (ptr, regu_var->type);

  ptr = or_pack_int (ptr, regu_var->flags);

  offset = xts_save_db_value (regu_var->vfetch_to);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  offset = xts_save_xasl_node (REGU_VARIABLE_XASL (regu_var));
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  ptr = xts_pack_regu_variable_value (ptr, regu_var);

  return ptr;
}

static char *
xts_pack_regu_variable_value (char *ptr, const REGU_VARIABLE * regu_var)
{
  int offset;

  assert (ptr != NULL && regu_var != NULL);

  switch (regu_var->type)
    {
    case TYPE_REGU_VAR_LIST:
      ptr =
	xts_process_regu_variable_list (ptr, regu_var->value.regu_var_list);
      if (ptr == NULL)
	{
	  return NULL;
	}
      break;

    case TYPE_REGUVAL_LIST:
      ptr = xts_process_regu_value_list (ptr, regu_var->value.reguval_list);
      if (ptr == NULL)
	{
	  return NULL;
	}
      break;

    case TYPE_DBVAL:
      ptr = xts_process_db_value (ptr, &regu_var->value.dbval);
      if (ptr == NULL)
	{
	  return NULL;
	}
      break;

    case TYPE_CONSTANT:
    case TYPE_ORDERBY_NUM:
      offset = xts_save_db_value (regu_var->value.dbvalptr);
      if (offset == ER_FAILED)
	{
	  return NULL;
	}
      ptr = or_pack_int (ptr, offset);
      break;

    case TYPE_INARITH:
    case TYPE_OUTARITH:
      offset = xts_save_arith_type (regu_var->value.arithptr);
      if (offset == ER_FAILED)
	{
	  return NULL;
	}
      ptr = or_pack_int (ptr, offset);
      break;

    case TYPE_AGGREGATE:
      offset = xts_save_aggregate_type (regu_var->value.aggptr);
      if (offset == ER_FAILED)
	{
	  return NULL;
	}
      ptr = or_pack_int (ptr, offset);
      break;

    case TYPE_FUNC:
      offset = xts_save_function_type (regu_var->value.funcp);
      if (offset == ER_FAILED)
	{
	  return NULL;
	}
      ptr = or_pack_int (ptr, offset);
      break;

    case TYPE_ATTR_ID:
    case TYPE_SHARED_ATTR_ID:
    case TYPE_CLASS_ATTR_ID:
      ptr = xts_process_attr_descr (ptr, &regu_var->value.attr_descr);
      if (ptr == NULL)
	{
	  return NULL;
	}
      break;

    case TYPE_LIST_ID:
      offset = xts_save_srlist_id (regu_var->value.srlist_id);
      if (offset == ER_FAILED)
	{
	  return NULL;
	}
      ptr = or_pack_int (ptr, offset);
      break;

    case TYPE_POSITION:
      ptr = xts_process_pos_descr (ptr, &regu_var->value.pos_descr);
      if (ptr == NULL)
	{
	  return NULL;
	}
      break;

    case TYPE_POS_VALUE:
      ptr = or_pack_int (ptr, regu_var->value.val_pos);
      break;

    case TYPE_OID:
    case TYPE_CLASSOID:
      break;

    default:
      xts_Xasl_errcode = ER_QPROC_INVALID_XASLNODE;
      return NULL;
    }

  return ptr;
}

static char *
xts_process_attr_descr (char *ptr, const ATTR_DESCR * attr_descr)
{
  int offset;

  ptr = or_pack_int (ptr, attr_descr->id);

  ptr = or_pack_int (ptr, attr_descr->type);

  offset = xts_save_cache_attrinfo (attr_descr->cache_attrinfo);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  return ptr;
}

static char *
xts_process_pos_descr (char *ptr,
		       const QFILE_TUPLE_VALUE_POSITION * position_descr)
{
  ptr = or_pack_int (ptr, position_descr->pos_no);
  ptr = OR_PACK_DOMAIN_OBJECT_TO_OID (ptr, position_descr->dom, 0, 0);

  return ptr;
}

static char *
xts_process_db_value (char *ptr, const DB_VALUE * value)
{
  ptr = or_pack_db_value (ptr, (DB_VALUE *) value);

  return ptr;
}

static char *
xts_process_arith_type (char *ptr, const ARITH_TYPE * arith)
{
  int offset;

  ptr = OR_PACK_DOMAIN_OBJECT_TO_OID (ptr, arith->domain, 0, 0);

  offset = xts_save_db_value (arith->value);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  ptr = or_pack_int (ptr, arith->opcode);

  offset = xts_save_arith_type (arith->next);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  offset = xts_save_regu_variable (arith->leftptr);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  offset = xts_save_regu_variable (arith->rightptr);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  offset = xts_save_regu_variable (arith->thirdptr);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  ptr = or_pack_int (ptr, arith->misc_operand);

  if (arith->opcode == T_CASE || arith->opcode == T_DECODE
      || arith->opcode == T_PREDICATE || arith->opcode == T_IF)
    {
      offset = xts_save_pred_expr (arith->pred);
      if (offset == ER_FAILED)
	{
	  return NULL;
	}
      ptr = or_pack_int (ptr, offset);
    }

  return ptr;
}

static char *
xts_process_aggregate_type (char *ptr, const AGGREGATE_TYPE * aggregate)
{
  int offset;

  ptr = OR_PACK_DOMAIN_OBJECT_TO_OID (ptr, aggregate->domain, 0, 0);

  offset = xts_save_db_value (aggregate->accumulator.value);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  offset = xts_save_db_value (aggregate->accumulator.value2);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  ptr = or_pack_int (ptr, aggregate->accumulator.curr_cnt);

  offset = xts_save_aggregate_type (aggregate->next);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  ptr = or_pack_int (ptr, aggregate->function);

  ptr = or_pack_int (ptr, aggregate->option);

  ptr = or_pack_int (ptr, (int) aggregate->opr_dbtype);

  ptr = xts_process_regu_variable (ptr, &aggregate->operand);
  if (ptr == NULL)
    {
      return NULL;
    }

  if (aggregate->list_id == NULL)
    {
      ptr = or_pack_int (ptr, 0);
    }
  else
    {
      offset = xts_save_list_id (aggregate->list_id);
      if (offset == ER_FAILED)
	{
	  return NULL;
	}
      ptr = or_pack_int (ptr, offset);
    }

  ptr = or_pack_int (ptr, aggregate->flag_agg_optimize);

  ptr = or_pack_btid (ptr, (BTID *) (&aggregate->btid));
  if (ptr == NULL)
    {
      return NULL;
    }

  offset = xts_save_sort_list (aggregate->sort_list);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  return ptr;
}

static char *
xts_process_function_type (char *ptr, const FUNCTION_TYPE * function)
{
  int offset;

  offset = xts_save_db_value (function->value);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  ptr = or_pack_int (ptr, function->ftype);

  offset = xts_save_regu_variable_list (function->operand);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  return ptr;
}

static char *
xts_process_analytic_type (char *ptr, const ANALYTIC_TYPE * analytic)
{
  int offset;

  ptr = OR_PACK_DOMAIN_OBJECT_TO_OID (ptr, analytic->domain, 0, 0);

  offset = xts_save_db_value (analytic->value);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  offset = xts_save_db_value (analytic->value2);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  offset = xts_save_db_value (analytic->out_value);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  ptr = or_pack_int (ptr, analytic->offset_idx);

  ptr = or_pack_int (ptr, analytic->default_idx);

  offset = xts_save_analytic_type (analytic->next);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  ptr = or_pack_int (ptr, analytic->function);

  ptr = or_pack_int (ptr, analytic->option);

  ptr = or_pack_int (ptr, (int) analytic->opr_dbtype);

  ptr = xts_process_regu_variable (ptr, &analytic->operand);
  if (ptr == NULL)
    {
      return NULL;
    }

  if (analytic->list_id == NULL)
    {
      ptr = or_pack_int (ptr, 0);
    }
  else
    {
      offset = xts_save_list_id (analytic->list_id);
      if (offset == ER_FAILED)
	{
	  return NULL;
	}
      ptr = or_pack_int (ptr, offset);
    }

  ptr = or_pack_int (ptr, analytic->sort_prefix_size);

  ptr = or_pack_int (ptr, analytic->sort_list_size);

  ptr = or_pack_int (ptr, analytic->flag);

  ptr = or_pack_int (ptr, analytic->from_last);

  ptr = or_pack_int (ptr, analytic->ignore_nulls);

  ptr = or_pack_int (ptr, analytic->is_const_operand);

  return ptr;
}

static char *
xts_process_analytic_eval_type (char *ptr,
				const ANALYTIC_EVAL_TYPE * analytic_eval)
{
  int offset;

  offset = xts_save_analytic_type (analytic_eval->head);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  offset = xts_save_sort_list (analytic_eval->sort_list);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  offset = xts_save_analytic_eval_type (analytic_eval->next);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  return ptr;
}

static char *
xts_process_srlist_id (char *ptr, const QFILE_SORTED_LIST_ID * sort_list_id)
{
  int offset;

  ptr = or_pack_int (ptr, sort_list_id->sorted);

  offset = xts_save_list_id (sort_list_id->list_id);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  return ptr;
}

static char *
xts_process_sort_list (char *ptr, const SORT_LIST * sort_list)
{
  int offset;

  offset = xts_save_sort_list (sort_list->next);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  ptr = xts_process_pos_descr (ptr, &sort_list->pos_descr);
  if (ptr == NULL)
    {
      return NULL;
    }

  ptr = or_pack_int (ptr, sort_list->s_order);
  ptr = or_pack_int (ptr, sort_list->s_nulls);

  /* others (not sent to server) */

  return ptr;
}

/* 
 * xts_process_method_sig_list ( ) -
 *
 * Note: do not use or_pack_method_sig_list
 */
static char *
xts_process_method_sig_list (char *ptr,
			     const METHOD_SIG_LIST * method_sig_list)
{
  int offset;

#if !defined(NDEBUG)
  {
    int i = 0;
    METHOD_SIG *sig;

    for (sig = method_sig_list->method_sig; sig; sig = sig->next)
      {
	i++;
      }
    assert (method_sig_list->no_methods == i);
  }
#endif

  ptr = or_pack_int (ptr, method_sig_list->no_methods);

  offset =
    xts_save_method_sig (method_sig_list->method_sig,
			 method_sig_list->no_methods);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  return ptr;
}

static char *
xts_process_method_sig (char *ptr, const METHOD_SIG * method_sig, int count)
{
  int offset;
  int n;

  assert (method_sig->method_name != NULL);

  offset = xts_save_string (method_sig->method_name);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  offset = xts_save_string (method_sig->class_name);	/* is can be null */
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  ptr = or_pack_int (ptr, method_sig->method_type);
  ptr = or_pack_int (ptr, method_sig->no_method_args);

  for (n = 0; n < method_sig->no_method_args + 1; n++)
    {
      ptr = or_pack_int (ptr, method_sig->method_arg_pos[n]);
    }

  offset = xts_save_method_sig (method_sig->next, count - 1);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  return ptr;
}

static char *
xts_process_connectby_proc (char *ptr,
			    const CONNECTBY_PROC_NODE * connectby_proc)
{
  int offset;

  offset = xts_save_pred_expr (connectby_proc->start_with_pred);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  offset = xts_save_pred_expr (connectby_proc->after_connect_by_pred);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  offset = xts_save_list_id (connectby_proc->input_list_id);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  offset = xts_save_list_id (connectby_proc->start_with_list_id);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  offset = xts_save_regu_variable_list (connectby_proc->regu_list_pred);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  offset = xts_save_regu_variable_list (connectby_proc->regu_list_rest);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  offset = xts_save_val_list (connectby_proc->prior_val_list);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  offset = xts_save_outptr_list (connectby_proc->prior_outptr_list);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  offset = xts_save_regu_variable_list (connectby_proc->prior_regu_list_pred);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  offset = xts_save_regu_variable_list (connectby_proc->prior_regu_list_rest);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  offset =
    xts_save_regu_variable_list (connectby_proc->after_cb_regu_list_pred);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  offset =
    xts_save_regu_variable_list (connectby_proc->after_cb_regu_list_rest);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  ptr = or_pack_int (ptr, (int) connectby_proc->single_table_opt);

  return ptr;
}

/*
 * xts_process_regu_value_list () -
 *   return:
 *   ptr(in)                    :
 *   regu_value_list(int)        :
 */
static char *
xts_process_regu_value_list (char *ptr,
			     const REGU_VALUE_LIST * regu_value_list)
{
  REGU_VALUE_ITEM *regu_value_item;
  REGU_DATATYPE type;

  assert (regu_value_list);

  ptr = or_pack_int (ptr, regu_value_list->count);
  for (regu_value_item = regu_value_list->regu_list; regu_value_item;
       regu_value_item = regu_value_item->next)
    {
      type = regu_value_item->value->type;
      if (type != TYPE_DBVAL && type != TYPE_INARITH
	  && type != TYPE_POS_VALUE)
	{
	  xts_Xasl_errcode = ER_QPROC_INVALID_XASLNODE;
	  return NULL;
	}
      ptr = or_pack_int (ptr, type);

      ptr = xts_pack_regu_variable_value (ptr, regu_value_item->value);
      if (ptr == NULL)
	{
	  return NULL;
	}
    }

  return ptr;
}

/*
 * xts_sizeof_xasl_node () -
 *   return:
 *   xasl(in)    :
 */
static int
xts_sizeof_xasl_node (const XASL_NODE * xasl)
{
  int size = 0;
  int tmp_size = 0;
  ACCESS_SPEC_TYPE *access_spec = NULL;

  size += XASL_NODE_HEADER_SIZE +	/* header */
    OR_INT_SIZE +		/* type */
    OR_INT_SIZE +		/* flag */
    PTR_SIZE +			/* list_id */
    PTR_SIZE +			/* after_iscan_list */
    PTR_SIZE +			/* orderby_list */
    PTR_SIZE +			/* ordbynum_pred */
    PTR_SIZE +			/* ordbynum_val */
    PTR_SIZE +			/* orderby_limit */
    OR_INT_SIZE +		/* ordbynum_flag */
    PTR_SIZE +			/* single_tuple */
    OR_INT_SIZE +		/* is_single_tuple */
    OR_INT_SIZE +		/* option */
    PTR_SIZE +			/* outptr_list */
    PTR_SIZE +			/* selected_upd_list */
    PTR_SIZE +			/* val_list */
    PTR_SIZE +			/* merge_val_list */
    PTR_SIZE +			/* aptr_list */
    PTR_SIZE +			/* bptr_list */
    PTR_SIZE +			/* dptr_list */
    PTR_SIZE +			/* after_join_pred */
    PTR_SIZE +			/* if_pred */
    PTR_SIZE +			/* instnum_pred */
    PTR_SIZE +			/* instnum_val */
    PTR_SIZE +			/* save_instnum_val */
    OR_INT_SIZE +		/* instnum_flag */
    PTR_SIZE +			/* limit_row_count */
    PTR_SIZE +			/* fptr_list */
    PTR_SIZE +			/* scan_ptr */
    PTR_SIZE +			/* connect_by_ptr */
    PTR_SIZE +			/* level_val */
    PTR_SIZE +			/* level_regu */
    PTR_SIZE +			/* isleaf_val */
    PTR_SIZE +			/* isleaf_regu */
    PTR_SIZE +			/* iscycle_val */
    PTR_SIZE +			/* iscycle_regu */
    OR_INT_SIZE +		/* next_scan_on */
    OR_INT_SIZE +		/* next_scan_block_on */
    OR_INT_SIZE +		/* cat_fetched */
    OR_INT_SIZE +		/* scan_op_type */
    OR_INT_SIZE +		/* upd_del_class_cnt */
    OR_INT_SIZE;		/* mvcc_reev_extra_cls_cnt */

  size += OR_INT_SIZE;		/* number of access specs in spec_list */
  for (access_spec = xasl->spec_list; access_spec;
       access_spec = access_spec->next)
    {
      size += xts_sizeof_access_spec_type (access_spec);
    }

  size += OR_INT_SIZE;		/* number of access specs in merge_spec */
  for (access_spec = xasl->merge_spec; access_spec;
       access_spec = access_spec->next)
    {
      size += xts_sizeof_access_spec_type (access_spec);
    }

  size += OR_INT_SIZE;		/* number of access specs in curr_spec */
  for (access_spec = xasl->curr_spec; access_spec;
       access_spec = access_spec->next)
    {
      size += xts_sizeof_access_spec_type (access_spec);
    }

  switch (xasl->type)
    {
    case BUILDLIST_PROC:
      tmp_size = xts_sizeof_buildlist_proc (&xasl->proc.buildlist);
      if (tmp_size == ER_FAILED)
	{
	  return ER_FAILED;
	}
      size += tmp_size;
      break;

    case BUILDVALUE_PROC:
      tmp_size = xts_sizeof_buildvalue_proc (&xasl->proc.buildvalue);
      if (tmp_size == ER_FAILED)
	{
	  return ER_FAILED;
	}
      size += tmp_size;
      break;

    case MERGELIST_PROC:
      tmp_size = xts_sizeof_mergelist_proc (&xasl->proc.mergelist);
      if (tmp_size == ER_FAILED)
	{
	  return ER_FAILED;
	}
      size += tmp_size;
      break;

    case CONNECTBY_PROC:
      tmp_size = xts_sizeof_connectby_proc (&xasl->proc.connect_by);
      if (tmp_size == ER_FAILED)
	{
	  return ER_FAILED;
	}
      size += tmp_size;
      break;

    case UPDATE_PROC:
      tmp_size = xts_sizeof_update_proc (&xasl->proc.update);
      if (tmp_size == ER_FAILED)
	{
	  return ER_FAILED;
	}
      size += tmp_size;
      break;

    case DELETE_PROC:
      tmp_size = xts_sizeof_delete_proc (&xasl->proc.delete_);
      if (tmp_size == ER_FAILED)
	{
	  return ER_FAILED;
	}
      size += tmp_size;
      break;

    case INSERT_PROC:
      tmp_size = xts_sizeof_insert_proc (&xasl->proc.insert);
      if (tmp_size == ER_FAILED)
	{
	  return ER_FAILED;
	}
      size += tmp_size;
      break;

    case UNION_PROC:
    case DIFFERENCE_PROC:
    case INTERSECTION_PROC:
      tmp_size = xts_sizeof_union_proc (&xasl->proc.union_);
      if (tmp_size == ER_FAILED)
	{
	  return ER_FAILED;
	}
      size += tmp_size;
      break;

    case OBJFETCH_PROC:
      tmp_size = xts_sizeof_fetch_proc (&xasl->proc.fetch);
      if (tmp_size == ER_FAILED)
	{
	  return ER_FAILED;
	}
      size += tmp_size;
      break;

    case SCAN_PROC:
      break;

    case DO_PROC:
      break;

    case BUILD_SCHEMA_PROC:
      break;

    case MERGE_PROC:
      size += xts_sizeof_merge_proc (&xasl->proc.merge);
      break;

    default:
      xts_Xasl_errcode = ER_QPROC_INVALID_XASLNODE;
      return ER_FAILED;
    }

  size += OR_INT_SIZE;		/* projected_size */

  size += OR_DOUBLE_ALIGNED_SIZE;	/* cardinality */

  size += OR_INT_SIZE;		/* iscan_oid_order */

  size += PTR_SIZE;		/* query_alias */

  size += PTR_SIZE;		/* next */
  return size;
}

/*
 * xts_sizeof_filter_pred_node () -
 *   return:
 *   pred(in): PRED_EXPR_WITH_CONTEXT
 */
static int
xts_sizeof_filter_pred_node (const PRED_EXPR_WITH_CONTEXT * pred)
{
  int size = 0;
  size += PTR_SIZE +		/* PRED_EXPR pointer: pred */
    OR_INT_SIZE +		/* num_attrs_pred */
    PTR_SIZE +			/* array pointer : */
    PTR_SIZE;			/* HEAP_CACHE_ATTRINFO pointer: cache_pred */

  return size;
}

/*
 * xts_sizeof_func_pred () -
 *   return:
 *   xasl(in):
 */
static int
xts_sizeof_func_pred (const FUNC_PRED * xasl)
{
  int size = 0;
  size += PTR_SIZE;		/* REGU_VAR pointer */
  size += PTR_SIZE;		/* HEAP_CACHE_ATTRINFO pointer */

  return size;
}

/*
 * xts_sizeof_cache_attrinfo () -
 *   return:
 *   ptr(in)    :
 */
static int
xts_sizeof_cache_attrinfo (const HEAP_CACHE_ATTRINFO * cache_attrinfo)
{
  return OR_INT_SIZE;		/* dummy 0 */
}

/*
 * xts_sizeof_union_proc () -
 *   return:
 *   ptr(in)    :
 */
static int
xts_sizeof_union_proc (const UNION_PROC_NODE * union_proc)
{
  int size = 0;

  size += PTR_SIZE +		/* left */
    PTR_SIZE;			/* right */

  return size;
}

/*
 * xts_sizeof_fetch_proc () -
 *   return:
 *   ptr(in)    :
 */
static int
xts_sizeof_fetch_proc (const FETCH_PROC_NODE * obj_set_fetch_proc)
{
  int size = 0;

  size += PTR_SIZE +		/* arg */
    OR_INT_SIZE +		/* fetch_res */
    PTR_SIZE +			/* set_pred */
    OR_INT_SIZE;		/* ql_flag */

  return size;
}

/*
 * xts_sizeof_buildlist_proc () -
 *   return:
 *   ptr(in)    :
 */
static int
xts_sizeof_buildlist_proc (const BUILDLIST_PROC_NODE * build_list)
{
  int size = 0;

  size +=
    /* output_columns (not sent to server) */
    PTR_SIZE +			/* eptr_list */
    PTR_SIZE +			/* groupby_list */
    PTR_SIZE +			/* after_groupby_list */
    PTR_SIZE +			/* push_list_id */
    PTR_SIZE +			/* g_outptr_list */
    PTR_SIZE +			/* g_regu_list */
    PTR_SIZE +			/* g_val_list */
    PTR_SIZE +			/* g_having_pred */
    PTR_SIZE +			/* g_grbynum_pred */
    PTR_SIZE +			/* g_grbynum_val */
    PTR_SIZE +			/* g_hk_scan_regu_list */
    PTR_SIZE +			/* g_hk_sort_regu_list */
    PTR_SIZE +			/* g_scan_regu_list */
    OR_INT_SIZE +		/* g_grbynum_flag */
    OR_INT_SIZE +		/* g_with_rollup */
    OR_INT_SIZE +		/* g_hash_eligible */
    OR_INT_SIZE +		/* g_output_first_tuple */
    OR_INT_SIZE +		/* g_hkey_size */
    OR_INT_SIZE +		/* g_func_count */
    PTR_SIZE +			/* g_agg_list */
    PTR_SIZE +			/* g_outarith_list */
    PTR_SIZE +			/* a_func_list */
    PTR_SIZE +			/* a_regu_list */
    PTR_SIZE +			/* a_outptr_list */
    PTR_SIZE +			/* a_outptr_list_ex */
    PTR_SIZE +			/* a_outptr_list_interm */
    PTR_SIZE +			/* a_val_list */
    PTR_SIZE +			/* a_instnum_pred */
    PTR_SIZE +			/* a_instnum_val */
    OR_INT_SIZE;		/* a_instnum_flag */

  return size;
}

/*
 * xts_sizeof_buildvalue_proc () -
 *   return:
 *   ptr(in)    :
 */
static int
xts_sizeof_buildvalue_proc (const BUILDVALUE_PROC_NODE * build_value)
{
  int size = 0;

  size += PTR_SIZE +		/* having_pred */
    PTR_SIZE +			/* grbynum_val */
    PTR_SIZE +			/* agg_list */
    PTR_SIZE +			/* outarith_list */
    OR_INT_SIZE;		/* is_always_false */

  return size;
}

/*
 * xts_sizeof_ls_merge_info () -
 *   return:
 *   ptr(in)    :
 */
static int
xts_sizeof_ls_merge_info (const QFILE_LIST_MERGE_INFO * qfile_list_merge_info)
{
  int size = 0;

  size += OR_INT_SIZE +		/* join_type */
    OR_INT_SIZE +		/* single_fetch */
    OR_INT_SIZE +		/* ls_column_cnt */
    PTR_SIZE +			/* ls_outer_column */
    PTR_SIZE +			/* ls_outer_unique */
    PTR_SIZE +			/* ls_inner_column */
    PTR_SIZE +			/* ls_inner_unique */
    OR_INT_SIZE +		/* ls_pos_cnt */
    PTR_SIZE +			/* ls_outer_inner_list */
    PTR_SIZE;			/* ls_pos_list */

  return size;
}

/*
 * xts_sizeof_mergelist_proc () -
 *   return:
 *   ptr(in)    :
 */
static int
xts_sizeof_mergelist_proc (const MERGELIST_PROC_NODE * merge_list_info)
{
  int size = 0;
  ACCESS_SPEC_TYPE *access_spec = NULL;

  size += PTR_SIZE +		/* outer_xasl */
    PTR_SIZE +			/* outer_val_list */
    PTR_SIZE +			/* inner_xasl */
    PTR_SIZE +			/* inner_val_list */
    xts_sizeof_ls_merge_info (&merge_list_info->ls_merge);	/* ls_merge_info */

  size += OR_INT_SIZE;		/* count of access specs in outer_spec_list */
  for (access_spec = merge_list_info->outer_spec_list; access_spec;
       access_spec = access_spec->next)
    {
      size += xts_sizeof_access_spec_type (access_spec);
    }

  size += OR_INT_SIZE;		/* count of access specs in inner_spec_list */
  for (access_spec = merge_list_info->inner_spec_list; access_spec;
       access_spec = access_spec->next)
    {
      size += xts_sizeof_access_spec_type (access_spec);
    }

  return size;
}

/*
 * xts_sizeof_upddel_class_info () -
 *   return:
 *   ptr(in)    :
 */
static int
xts_sizeof_upddel_class_info (const UPDDEL_CLASS_INFO * upd_cls)
{
  int size = 0;

  size += OR_INT_SIZE +		/* no_subclasses */
    PTR_SIZE +			/* class_oid */
    PTR_SIZE +			/* class_hfid */
    OR_INT_SIZE +		/* no_attrs */
    PTR_SIZE +			/* att_id */
    OR_INT_SIZE +		/* needs pruning */
    OR_INT_SIZE +		/* has_uniques */
    PTR_SIZE +			/* no_lob_attrs */
    PTR_SIZE +			/* lob_attr_ids */
    OR_INT_SIZE +		/* no_extra_assign_reev */
    PTR_SIZE;			/* mvcc_extra_assign_reev */

  return size;
}

/*
 * xts_sizeof_update_assignment () -
 *   return:
 *   ptr(in)    :
 */
static int
xts_sizeof_update_assignment (const UPDATE_ASSIGNMENT * assign)
{
  int size = 0;

  size += OR_INT_SIZE +		/* cls_idx */
    OR_INT_SIZE +		/* att_idx */
    PTR_SIZE +			/* constant */
    PTR_SIZE;			/* regu_var */

  return size;
}

static int
xts_sizeof_odku_info (const ODKU_INFO * odku_info)
{
  int size = 0;

  size += OR_INT_SIZE +		/* no_assigns */
    PTR_SIZE +			/* attr_ids */
    PTR_SIZE +			/* assignments */
    PTR_SIZE +			/* cons_pred */
    PTR_SIZE;			/* attr_info */

  return size;
}

/*
 * xts_sizeof_update_proc () -
 *   return:
 *   ptr(in)    :
 */
static int
xts_sizeof_update_proc (const UPDATE_PROC_NODE * update_info)
{
  int size = 0;

  size += OR_INT_SIZE +		/* no_classes */
    PTR_SIZE +			/* classes */
    PTR_SIZE +			/* cons_pred */
    OR_INT_SIZE +		/* no_assigns */
    PTR_SIZE +			/* assignments */
    OR_INT_SIZE +		/* wait_msecs */
    OR_INT_SIZE +		/* no_logging */
    OR_INT_SIZE +		/* release_lock */
    OR_INT_SIZE +		/* no_orderby_keys */
    OR_INT_SIZE +		/* no_assign_reev_classes */
    OR_INT_SIZE +		/* no_cond_reev_classes */
    PTR_SIZE;			/* mvcc_cond_reev_classes */

  return size;
}

/*
 * xts_sizeof_delete_proc () -
 *   return:
 *   ptr(in)    :
 */
static int
xts_sizeof_delete_proc (const DELETE_PROC_NODE * delete_info)
{
  int size = 0;

  size += PTR_SIZE +		/* classes */
    OR_INT_SIZE +		/* no_classes */
    OR_INT_SIZE +		/* wait_msecs */
    OR_INT_SIZE +		/* no_logging */
    OR_INT_SIZE +		/* release_lock */
    OR_INT_SIZE +		/* no_cond_reev_classes */
    PTR_SIZE;			/* mvcc_cond_reev_classes */

  return size;
}

/*
 * xts_sizeof_insert_proc () -
 *   return:
 *   ptr(in)    :
 */
static int
xts_sizeof_insert_proc (const INSERT_PROC_NODE * insert_info)
{
  int size = 0;

  size += OR_OID_SIZE +		/* class_oid */
    OR_HFID_SIZE +		/* class_hfid */
    OR_INT_SIZE +		/* no_vals */
    OR_INT_SIZE +		/* no_default_expr */
    PTR_SIZE +			/* array pointer: att_id */
    PTR_SIZE +			/* constraint predicate: cons_pred */
    PTR_SIZE +			/* odku */
    OR_INT_SIZE +		/* has_uniques */
    OR_INT_SIZE +		/* wait_msecs */
    OR_INT_SIZE +		/* no_logging */
    OR_INT_SIZE +		/* release_lock */
    OR_INT_SIZE +		/* do_replace */
    OR_INT_SIZE +		/* needs pruning */
    OR_INT_SIZE +		/* no_val_lists */
    PTR_SIZE;			/* obj_oid */

  size += insert_info->no_val_lists * PTR_SIZE;	/* valptr_lists */

  return size;
}

/*
 * xts_sizeof_merge_proc () -
 *   return:
 *   ptr(in)    :
 */
static int
xts_sizeof_merge_proc (const MERGE_PROC_NODE * merge_info)
{
  int size = 0;

  size += PTR_SIZE +		/* update_xasl */
    PTR_SIZE +			/* insert_xasl */
    OR_INT_SIZE;		/* has_delete */

  return size;
}

/*
 * xts_sizeof_outptr_list () -
 *   return:
 *   ptr(in)    :
 */
static int
xts_sizeof_outptr_list (const OUTPTR_LIST * outptr_list)
{
  int size = 0;

  size += OR_INT_SIZE +		/* valptr_cnt */
    PTR_SIZE;			/* valptrp */

  return size;
}

/*
 * xts_sizeof_pred_expr () -
 *   return:
 *   ptr(in)    :
 */
static int
xts_sizeof_pred_expr (const PRED_EXPR * pred_expr)
{
  int size = 0;
  int tmp_size = 0;

  size += OR_INT_SIZE;		/* type_node */
  switch (pred_expr->type)
    {
    case T_PRED:
      tmp_size = xts_sizeof_pred (&pred_expr->pe.pred);
      if (tmp_size == ER_FAILED)
	{
	  return ER_FAILED;
	}
      size += tmp_size;
      break;

    case T_EVAL_TERM:
      tmp_size = xts_sizeof_eval_term (&pred_expr->pe.eval_term);
      if (tmp_size == ER_FAILED)
	{
	  return ER_FAILED;
	}
      size += tmp_size;
      break;

    case T_NOT_TERM:
      size += PTR_SIZE;
      break;

    default:
      xts_Xasl_errcode = ER_QPROC_INVALID_XASLNODE;
      return ER_FAILED;
    }

  return size;
}

/*
 * xts_sizeof_selupd_list () -
 *   return:
 *   ptr(in)    :
 */
static int
xts_sizeof_selupd_list (const SELUPD_LIST * selupd_list)
{
  int size = 0;

  size = OR_OID_SIZE +		/* class_oid */
    OR_HFID_SIZE +		/* class_hfid */
    OR_INT_SIZE +		/* select_list_size */
    PTR_SIZE +			/* select_list */
    OR_INT_SIZE +		/* wait_msecs */
    PTR_SIZE;			/* next */

  return size;
}

/*
 * xts_sizeof_pred () -
 *   return:
 *   ptr(in)    :
 */
static int
xts_sizeof_pred (const PRED * pred)
{
  int size = 0;
  int tmp_size = 0;
  PRED_EXPR *rhs;

  size += PTR_SIZE +		/* lhs */
    OR_INT_SIZE;		/* bool_op */

  rhs = pred->rhs;
  size += OR_INT_SIZE;		/* rhs-type */

  /* Traverse right-linear chains of AND/OR terms */
  while (rhs->type == T_PRED)
    {
      pred = &rhs->pe.pred;

      size += PTR_SIZE +	/* lhs */
	OR_INT_SIZE;		/* bool_op */

      rhs = pred->rhs;
      size += OR_INT_SIZE;	/* rhs-type */
    }

  size += PTR_SIZE;

  return size;
}

/*
 * xts_sizeof_eval_term () -
 *   return:
 *   ptr(in)    :
 */
static int
xts_sizeof_eval_term (const EVAL_TERM * eval_term)
{
  int size = 0;
  int tmp_size = 0;

  size += OR_INT_SIZE;		/* et_type */
  switch (eval_term->et_type)
    {
    case T_COMP_EVAL_TERM:
      tmp_size = xts_sizeof_comp_eval_term (&eval_term->et.et_comp);
      if (tmp_size == ER_FAILED)
	{
	  return ER_FAILED;
	}
      size += tmp_size;
      break;

    case T_ALSM_EVAL_TERM:
      tmp_size = xts_sizeof_alsm_eval_term (&eval_term->et.et_alsm);
      if (tmp_size == ER_FAILED)
	{
	  return ER_FAILED;
	}
      size += tmp_size;
      break;

    case T_LIKE_EVAL_TERM:
      tmp_size = xts_sizeof_like_eval_term (&eval_term->et.et_like);
      if (tmp_size == ER_FAILED)
	{
	  return ER_FAILED;
	}
      size += tmp_size;
      break;

    case T_RLIKE_EVAL_TERM:
      tmp_size = xts_sizeof_rlike_eval_term (&eval_term->et.et_rlike);
      if (tmp_size == ER_FAILED)
	{
	  return ER_FAILED;
	}
      size += tmp_size;
      break;

    default:
      xts_Xasl_errcode = ER_QPROC_INVALID_XASLNODE;
      return ER_FAILED;
    }

  return size;
}

/*
 * xts_sizeof_comp_eval_term () -
 *   return:
 *   ptr(in)    :
 */
static int
xts_sizeof_comp_eval_term (const COMP_EVAL_TERM * comp_eval_term)
{
  int size = 0;

  size += PTR_SIZE +		/* lhs */
    PTR_SIZE +			/* rhs */
    OR_INT_SIZE +		/* rel_op */
    OR_INT_SIZE;		/* type */

  return size;
}

/*
 * xts_sizeof_alsm_eval_term () -
 *   return:
 *   ptr(in)    :
 */
static int
xts_sizeof_alsm_eval_term (const ALSM_EVAL_TERM * alsm_eval_term)
{
  int size = 0;

  size += PTR_SIZE +		/* elem */
    PTR_SIZE +			/* elemset */
    OR_INT_SIZE +		/* eq_flag */
    OR_INT_SIZE +		/* rel_op */
    OR_INT_SIZE;		/* item_type */

  return size;
}

/*
 * xts_sizeof_like_eval_term () -
 *   return:
 *   ptr(in)    :
 */
static int
xts_sizeof_like_eval_term (const LIKE_EVAL_TERM * like_eval_term)
{
  int size = 0;

  size += PTR_SIZE +		/* src */
    PTR_SIZE +			/* pattern */
    PTR_SIZE;			/* esc_char */

  return size;
}

/*
 * xts_sizeof_rlike_eval_term () -
 *   return: size of rlike eval term
 *   rlike_eval_term(in):
 */
static int
xts_sizeof_rlike_eval_term (const RLIKE_EVAL_TERM * rlike_eval_term)
{
  int size = 0;

  size += PTR_SIZE +		/* src */
    PTR_SIZE +			/* pattern */
    PTR_SIZE;			/* case_sensitive */

  return size;
}

/*
 * xts_sizeof_access_spec_type () -
 *   return:
 *   ptr(in)    :
 */
static int
xts_sizeof_access_spec_type (const ACCESS_SPEC_TYPE * access_spec)
{
  int size = 0;
  int tmp_size = 0;

  size += OR_INT_SIZE +		/* type */
    OR_INT_SIZE +		/* access */
    OR_INT_SIZE +		/* flags */
    PTR_SIZE +			/* index_ptr */
    PTR_SIZE +			/* where_key */
    PTR_SIZE +			/* where_pred */
    PTR_SIZE;			/* where_range */

  switch (access_spec->type)
    {
    case TARGET_CLASS:
    case TARGET_CLASS_ATTR:
      tmp_size =
	xts_sizeof_cls_spec_type (&ACCESS_SPEC_CLS_SPEC (access_spec));
      if (tmp_size == ER_FAILED)
	{
	  return ER_FAILED;
	}
      size += tmp_size;
      break;

    case TARGET_LIST:
      tmp_size =
	xts_sizeof_list_spec_type (&ACCESS_SPEC_LIST_SPEC (access_spec));
      if (tmp_size == ER_FAILED)
	{
	  return ER_FAILED;
	}
      size += tmp_size;
      break;

    case TARGET_SHOWSTMT:
      tmp_size =
	xts_sizeof_showstmt_spec_type (&ACCESS_SPEC_SHOWSTMT_SPEC
				       (access_spec));
      if (tmp_size == ER_FAILED)
	{
	  return ER_FAILED;
	}
      size += tmp_size;
      break;

    case TARGET_REGUVAL_LIST:
      /* currently do nothing */
      break;

    case TARGET_SET:
      tmp_size =
	xts_sizeof_set_spec_type (&ACCESS_SPEC_SET_SPEC (access_spec));
      if (tmp_size == ER_FAILED)
	{
	  return ER_FAILED;
	}
      size += tmp_size;
      break;

    case TARGET_METHOD:
      tmp_size =
	xts_sizeof_method_spec_type (&ACCESS_SPEC_METHOD_SPEC (access_spec));
      if (tmp_size == ER_FAILED)
	{
	  return ER_FAILED;
	}
      size += tmp_size;
      break;

    default:
      xts_Xasl_errcode = ER_QPROC_INVALID_XASLNODE;
      return ER_FAILED;
    }

  size +=			/* s_id (not sent to server) */
    OR_INT_SIZE +		/* grouped_scan */
    OR_INT_SIZE +		/* fixed_scan */
    OR_INT_SIZE +		/* qualified_scan */
    OR_INT_SIZE +		/* single_fetch */
    OR_INT_SIZE +		/* needs pruning */
    PTR_SIZE;			/* s_dbval */

  return size;
}

/*
 * xts_sizeof_indx_info () -
 *   return:
 *   ptr(in)    :
 */
static int
xts_sizeof_indx_info (const INDX_INFO * indx_info)
{
  int size = 0;
  int tmp_size = 0;

  tmp_size = xts_sizeof_indx_id (&indx_info->indx_id);
  if (tmp_size == ER_FAILED)
    {
      return ER_FAILED;
    }
  size += tmp_size;

  size += OR_INT_SIZE;		/* coverage */

  size += OR_INT_SIZE;		/* range_type */

  tmp_size = xts_sizeof_key_info (&indx_info->key_info);
  if (tmp_size == ER_FAILED)
    {
      return ER_FAILED;
    }
  size += tmp_size;

  size += OR_INT_SIZE;		/* orderby_desc */

  size += OR_INT_SIZE;		/* groupby_desc */

  size += OR_INT_SIZE;		/* use_desc_index */

  size += OR_INT_SIZE;		/* orderby_skip */

  size += OR_INT_SIZE;		/* groupby_skip */

  size += OR_INT_SIZE;		/* use_iss boolean (int) */

  size += OR_INT_SIZE;		/* ils_prefix_len (int) */

  size += OR_INT_SIZE;		/* func_idx_col_id (int) */

  size += OR_INT_SIZE;		/* iss_range's range */

  size += PTR_SIZE;		/* iss_range's key1 */

  return size;
}

/*
 * xts_sizeof_indx_id () -
 *   return:
 *   ptr(in)    :
 */
static int
xts_sizeof_indx_id (const INDX_ID * indx_id)
{
  int size = 0;

  size += OR_INT_SIZE;		/* type */
  switch (indx_id->type)
    {
    case T_BTID:
      size += OR_BTID_ALIGNED_SIZE;
      break;

    case T_EHID:
      size += OR_EHID_SIZE;
      break;

    default:
      xts_Xasl_errcode = ER_QPROC_INVALID_XASLNODE;
      return ER_FAILED;
    }

  return size;
}

/*
 * xts_sizeof_key_info () -
 *   return:
 *   ptr(in)    :
 */
static int
xts_sizeof_key_info (const KEY_INFO * key_info)
{
  int size = 0;

  size += OR_INT_SIZE +		/* key_cnt */
    PTR_SIZE +			/* key_ranges */
    OR_INT_SIZE +		/* is_constant */
    PTR_SIZE +			/* key_limit_l */
    PTR_SIZE +			/* key_limit_u */
    OR_INT_SIZE;		/* key_limit_reset */

  return size;
}

/*
 * xts_sizeof_cls_spec_type () -
 *   return:
 *   ptr(in)    :
 */
static int
xts_sizeof_cls_spec_type (const CLS_SPEC_TYPE * cls_spec)
{
  int size = 0;

  size += PTR_SIZE +		/* cls_regu_list_key */
    PTR_SIZE +			/* cls_regu_list_pred */
    PTR_SIZE +			/* cls_regu_list_rest */
    PTR_SIZE +			/* cls_regu_list_range */
    PTR_SIZE +			/* cls_regu_list_last_version */
    PTR_SIZE +			/* cls_output_val_list  */
    PTR_SIZE +			/* regu_val_list     */
    OR_HFID_SIZE +		/* hfid */
    OR_OID_SIZE +		/* cls_oid */
    OR_INT_SIZE +		/* num_attrs_key */
    PTR_SIZE +			/* attrids_key */
    PTR_SIZE +			/* cache_key */
    OR_INT_SIZE +		/* num_attrs_pred */
    PTR_SIZE +			/* attrids_pred */
    PTR_SIZE +			/* cache_pred */
    OR_INT_SIZE +		/* num_attrs_rest */
    PTR_SIZE +			/* attrids_rest */
    PTR_SIZE +			/* cache_rest */
    OR_INT_SIZE +		/* schema_type */
    OR_INT_SIZE +		/* num_attrs_reserved */
    PTR_SIZE +			/* cache_reserved */
    PTR_SIZE +			/* cls_regu_list_reserved */
    PTR_SIZE +			/* atrtrids_range */
    PTR_SIZE +			/* cache_range */
    OR_INT_SIZE;		/* num_attrs_range */

  return size;
}

/*
 * xts_sizeof_list_spec_type () -
 *   return:
 *   ptr(in)    :
 */
static int
xts_sizeof_list_spec_type (const LIST_SPEC_TYPE * list_spec)
{
  int size = 0;

  size += PTR_SIZE +		/* list_regu_list_pred */
    PTR_SIZE +			/* list_regu_list_rest */
    PTR_SIZE;			/* xasl_node */

  return size;
}

/*
 * xts_sizeof_showstmt_spec_type () -
 *   return:
 *   ptr(in)    :
 */
static int
xts_sizeof_showstmt_spec_type (const SHOWSTMT_SPEC_TYPE * showstmt_spec)
{
  int size = 0;

  size += OR_INT_SIZE +		/* show_type */
    PTR_SIZE;			/* arg_list */

  return size;
}

/*
 * xts_sizeof_set_spec_type () -
 *   return:
 *   ptr(in)    :
 */
static int
xts_sizeof_set_spec_type (const SET_SPEC_TYPE * set_spec)
{
  int size = 0;

  size += PTR_SIZE +		/* set_regu_list */
    PTR_SIZE;			/* set_ptr */

  return size;
}

/*
 * xts_sizeof_method_spec_type () -
 *   return:
 *   ptr(in)    :
 */
static int
xts_sizeof_method_spec_type (const METHOD_SPEC_TYPE * method_spec)
{
  int size = 0;

  size += PTR_SIZE +		/* method_regu_list */
    PTR_SIZE +			/* xasl_node */
    PTR_SIZE;			/* method_sig_list */

  return size;
}

/*
 * xts_sizeof_list_id () -
 *   return:xts_process_db_value
 *   ptr(in)    :
 */
static int
xts_sizeof_list_id (const QFILE_LIST_ID * list_id)
{
  int size = 0;

  size = or_listid_length ((void *) list_id);

  return size;
}

/*
 * xts_sizeof_val_list () -
 *   return:
 *   ptr(in)    :
 */
static int
xts_sizeof_val_list (const VAL_LIST * val_list)
{
  int size = 0;
  QPROC_DB_VALUE_LIST p = NULL;

  size += OR_INT_SIZE;		/* val_cnt */

  for (p = val_list->valp; p; p = p->next)
    {
      size += PTR_SIZE;		/* p->val */
    }

  return size;
}

/*
 * xts_sizeof_regu_variable () -
 *   return:
 *   ptr(in)    :
 */
static int
xts_sizeof_regu_variable (const REGU_VARIABLE * regu_var)
{
  int size = 0;
  int tmp_size = 0;

  /* we prepend the domain before we pack the regu_variable */
  size += or_packed_domain_size (regu_var->domain, 0);
  size += OR_INT_SIZE;		/* type */
  size += OR_INT_SIZE;		/* flags */
  size += PTR_SIZE;		/* vfetch_to */
  size += PTR_SIZE;		/* REGU_VARIABLE_XASL */

  tmp_size = xts_get_regu_variable_value_size (regu_var);
  if (tmp_size == ER_FAILED)
    {
      return ER_FAILED;
    }
  size += tmp_size;

  return size;
}

/*
 * xts_get_regu_variable_value_size () -
 *   return:
 *   regu_var(in)    :
 */
static int
xts_get_regu_variable_value_size (const REGU_VARIABLE * regu_var)
{
  int size = ER_FAILED;

  assert (regu_var);

  switch (regu_var->type)
    {
    case TYPE_REGU_VAR_LIST:
      size = xts_sizeof_regu_variable_list (regu_var->value.regu_var_list);
      break;

    case TYPE_REGUVAL_LIST:
      size = xts_sizeof_regu_value_list (regu_var->value.reguval_list);
      break;

    case TYPE_DBVAL:
      size = OR_VALUE_ALIGNED_SIZE ((DB_VALUE *) (&regu_var->value.dbval));
      break;

    case TYPE_CONSTANT:
    case TYPE_ORDERBY_NUM:
      size = PTR_SIZE;		/* dbvalptr */
      break;

    case TYPE_INARITH:
    case TYPE_OUTARITH:
      size = PTR_SIZE;		/* arithptr */
      break;

    case TYPE_AGGREGATE:
      size = PTR_SIZE;		/* aggptr */
      break;

    case TYPE_FUNC:
      size = PTR_SIZE;		/* funcp */
      break;

    case TYPE_ATTR_ID:
    case TYPE_SHARED_ATTR_ID:
    case TYPE_CLASS_ATTR_ID:
      size = xts_sizeof_attr_descr (&regu_var->value.attr_descr);
      break;

    case TYPE_LIST_ID:
      size = PTR_SIZE;		/* srlist_id */
      break;

    case TYPE_POSITION:
      size = xts_sizeof_pos_descr (&regu_var->value.pos_descr);
      break;

    case TYPE_POS_VALUE:
      size = OR_INT_SIZE;	/* val_pos */
      break;

    case TYPE_OID:
    case TYPE_CLASSOID:
      size = 0;
      break;
    default:
      xts_Xasl_errcode = ER_QPROC_INVALID_XASLNODE;
    }

  return size;
}

/*
 * xts_sizeof_attr_descr () -
 *   return:
 *   ptr(in)    :
 */
static int
xts_sizeof_attr_descr (const ATTR_DESCR * attr_descr)
{
  int size = 0;

  size += OR_INT_SIZE +		/* id */
    OR_INT_SIZE +		/* type */
    PTR_SIZE;			/* cache_attrinfo */

  return size;
}

/*
 * xts_sizeof_pos_descr () -
 *   return:
 *   ptr(in)    :
 */
static int
xts_sizeof_pos_descr (const QFILE_TUPLE_VALUE_POSITION * position_descr)
{
  int size = 0;

  size += OR_INT_SIZE +		/* pos_no */
    or_packed_domain_size (position_descr->dom, 0);	/* type */

  return size;
}

/*
 * xts_sizeof_db_value () -
 *   return:
 *   ptr(in)    :
 */
static int
xts_sizeof_db_value (const DB_VALUE * value)
{
  return or_db_value_size ((DB_VALUE *) value);
}

/*
 * xts_sizeof_arith_type () -
 *   return:
 *   ptr(in)    :
 */
static int
xts_sizeof_arith_type (const ARITH_TYPE * arith)
{
  int size = 0;

  size += PTR_SIZE +		/* next */
    PTR_SIZE +			/* value */
    OR_INT_SIZE +		/* operator */
    PTR_SIZE +			/* leftptr */
    PTR_SIZE +			/* rightptr */
    PTR_SIZE +			/* thirdptr */
    OR_INT_SIZE +		/* misc_operand */
    ((arith->opcode == T_CASE || arith->opcode == T_DECODE
      || arith->opcode == T_PREDICATE
      || arith->opcode == T_IF) ? PTR_SIZE : 0 /* case pred */ ) +
    or_packed_domain_size (arith->domain, 0);

  return size;
}

/*
 * xts_sizeof_aggregate_type () -
 *   return:
 *   ptr(in)    :
 */
static int
xts_sizeof_aggregate_type (const AGGREGATE_TYPE * aggregate)
{
  int size = 0;
  int tmp_size = 0;

  size += or_packed_domain_size (aggregate->domain, 0) + PTR_SIZE +	/* next */
    PTR_SIZE +			/* value */
    PTR_SIZE +			/* value2 */
    OR_INT_SIZE +		/* curr_cnt */
    OR_INT_SIZE +		/* function */
    OR_INT_SIZE +		/* option */
    OR_INT_SIZE;		/* opr_dbtype */

  tmp_size = xts_sizeof_regu_variable (&aggregate->operand);
  if (tmp_size == ER_FAILED)
    {
      return ER_FAILED;
    }
  size += tmp_size;

  size += PTR_SIZE		/* list_id */
    + OR_INT_SIZE + OR_BTID_ALIGNED_SIZE;

  size += PTR_SIZE;		/* sort_info */

  return size;
}

/*
 * xts_sizeof_function_type () -
 *   return:
 *   ptr(in)    :
 */
static int
xts_sizeof_function_type (const FUNCTION_TYPE * function)
{
  int size = 0;

  size += PTR_SIZE +		/* value */
    OR_INT_SIZE +		/* ftype */
    PTR_SIZE;			/* operand */

  return size;
}

/*
 * xts_sizeof_analytic_type () -
 *   return:
 *   ptr(in)    :
 */
static int
xts_sizeof_analytic_type (const ANALYTIC_TYPE * analytic)
{
  int size = 0;
  int tmp_size = 0;

  size += or_packed_domain_size (analytic->domain, 0) + PTR_SIZE +	/* next */
    PTR_SIZE +			/* value */
    PTR_SIZE +			/* value2 */
    PTR_SIZE +			/* valptr_value */
    PTR_SIZE +			/* list_id */
    OR_INT_SIZE +		/* function */
    OR_INT_SIZE +		/* offset_idx */
    OR_INT_SIZE +		/* default_idx */
    OR_INT_SIZE +		/* option */
    OR_INT_SIZE +		/* opr_dbtype */
    OR_INT_SIZE +		/* sort_prefix_size */
    OR_INT_SIZE +		/* sort_list_size */
    OR_INT_SIZE +		/* flag */
    OR_INT_SIZE +		/* from_last */
    OR_INT_SIZE +		/* ignore_nulls */
    OR_INT_SIZE;		/* is_const_opr */

  tmp_size = xts_sizeof_regu_variable (&analytic->operand);
  if (tmp_size == ER_FAILED)
    {
      return ER_FAILED;
    }
  size += tmp_size;

  return size;
}

/*
 * xts_sizeof_analytic_eval_type () -
 *   return:
 *   ptr(in)    :
 */
static int
xts_sizeof_analytic_eval_type (const ANALYTIC_EVAL_TYPE * analytic_eval)
{
  int size = 0;
  int tmp_size = 0;

  size = PTR_SIZE +		/* next */
    PTR_SIZE +			/* head */
    PTR_SIZE;			/* sort_list */

  return size;
}

/*
 * xts_sizeof_srlist_id () -
 *   return:
 *   ptr(in)    :
 */
static int
xts_sizeof_srlist_id (const QFILE_SORTED_LIST_ID * sort_list_id)
{
  int size = 0;

  size += OR_INT_SIZE +		/* sorted */
    PTR_SIZE;			/* list_id */

  return size;
}

/*
 * xts_sizeof_sort_list () -
 *   return:
 *   ptr(in)    :
 */
static int
xts_sizeof_sort_list (const SORT_LIST * sort_lis)
{
  int size = 0;
  int tmp_size = 0;

  size += PTR_SIZE;		/* next */

  tmp_size = xts_sizeof_pos_descr (&sort_lis->pos_descr);
  if (tmp_size == ER_FAILED)
    {
      return ER_FAILED;
    }
  size += tmp_size;

  size +=
    /* other (not sent to server) */
    OR_INT_SIZE;		/* s_order */

  size += OR_INT_SIZE;		/* s_nulls */

  return size;
}

/*
 * xts_sizeof_method_sig_list () -
 *   return:
 *   ptr(in)    :
 */
static int
xts_sizeof_method_sig_list (const METHOD_SIG_LIST * method_sig_list)
{
  int size = 0;

  size += OR_INT_SIZE +		/* no_methods */
    PTR_SIZE;			/* method_sig */

  return size;
}

static int
xts_sizeof_method_sig (const METHOD_SIG * method_sig)
{
  int size = 0;

  size += PTR_SIZE +		/* method_name */
    PTR_SIZE +			/* class_name */
    OR_INT_SIZE +		/* method_type */
    OR_INT_SIZE +		/* no_method_args */
    (OR_INT_SIZE * (method_sig->no_method_args + 1)) +	/* method_arg_pos */
    PTR_SIZE;			/* next */

  return size;
}

/*
 * xts_sizeof_connectby_proc () -
 *   return:
 *   ptr(in)    :
 */
static int
xts_sizeof_connectby_proc (const CONNECTBY_PROC_NODE * connectby)
{
  int size = 0;

  size += PTR_SIZE +		/* start_with_pred */
    PTR_SIZE +			/* after_connect_by_pred */
    PTR_SIZE +			/* input_list_id */
    PTR_SIZE +			/* start_with_list_id */
    PTR_SIZE +			/* regu_list_pred */
    PTR_SIZE +			/* regu_list_rest */
    PTR_SIZE +			/* prior_val_list */
    PTR_SIZE +			/* prior_outptr_list */
    PTR_SIZE +			/* prior_regu_list_pred */
    PTR_SIZE +			/* prior_regu_list_rest */
    PTR_SIZE +			/* after_cb_regu_list_pred */
    PTR_SIZE +			/* after_cb_regu_list_rest */
    OR_INT_SIZE;		/* single_table_opt */

  return size;
}

/*
 * xts_sizeof_regu_value_list () -
 *   return:
 *   regu_value_list(in)    :
 */
static int
xts_sizeof_regu_value_list (const REGU_VALUE_LIST * regu_value_list)
{
  int size, tmp_size;
  REGU_VALUE_ITEM *regu_value_item;

  assert (regu_value_list);

  size = tmp_size = 0;

  size += OR_INT_SIZE;
  for (regu_value_item = regu_value_list->regu_list; regu_value_item;
       regu_value_item = regu_value_item->next)
    {
      tmp_size = xts_get_regu_variable_value_size (regu_value_item->value);

      if (tmp_size == ER_FAILED)
	{
	  return ER_FAILED;
	}

      size += OR_INT_SIZE + tmp_size;	/* OR_INT_SIZE for type */
    }

  return size;
}

/*
 * xts_sizeof_regu_variable_list () -
 *   return: size or ER_FAILED
 *   regu_value_list(in)    :
 */
static int
xts_sizeof_regu_variable_list (const REGU_VARIABLE_LIST regu_var_list)
{
  int size = 0, tmp_size = 0;
  REGU_VARIABLE_LIST regu_var = regu_var_list;

  assert (regu_var_list != NULL);

  size += OR_INT_SIZE;
  while (regu_var)
    {
      tmp_size = xts_get_regu_variable_value_size (&regu_var->value);
      regu_var = regu_var->next;

      if (tmp_size == ER_FAILED)
	{
	  return ER_FAILED;
	}

      size += OR_INT_SIZE + tmp_size;	/* OR_INT_SIZE for type */
    }

  return size;
}

/*
 * xts_mark_ptr_visited () -
 *   return: if successful, return NO_ERROR, otherwise
 *           ER_FAILED and error code is set to xasl_errcode
 *   ptr(in)    : pointer constant to be marked visited
 *   offset(in) : where the node pointed by 'ptr' is stored
 *
 * Note: mark the given pointer constant as visited to avoid
 * duplicated stored of a node which is pointed by more	than one node
 */
static int
xts_mark_ptr_visited (const void *ptr, int offset)
{
  int new_lwm;
  int block_no;

  block_no = PTR_BLOCK (ptr);

  new_lwm = xts_Ptr_lwm[block_no];

  if (xts_Ptr_max[block_no] == 0)
    {
      xts_Ptr_max[block_no] = START_PTR_PER_BLOCK;
      xts_Ptr_blocks[block_no] = (VISITED_PTR *)
	malloc (sizeof (VISITED_PTR) * xts_Ptr_max[block_no]);
    }
  else if (xts_Ptr_max[block_no] <= new_lwm)
    {
      xts_Ptr_max[block_no] *= 2;
      xts_Ptr_blocks[block_no] = (VISITED_PTR *)
	realloc (xts_Ptr_blocks[block_no],
		 sizeof (VISITED_PTR) * xts_Ptr_max[block_no]);
    }

  if (xts_Ptr_blocks[block_no] == (VISITED_PTR *) NULL)
    {
      xts_Xasl_errcode = ER_OUT_OF_VIRTUAL_MEMORY;
      return ER_FAILED;
    }

  xts_Ptr_blocks[block_no][new_lwm].ptr = ptr;
  xts_Ptr_blocks[block_no][new_lwm].offset = offset;

  xts_Ptr_lwm[block_no]++;

  return NO_ERROR;
}

/*
 * xts_get_offset_visited_ptr () -
 *   return: if the ptr is already visited, the offset of
 *           position where the node pointed by 'ptr' is stored,
 *           otherwise, ER_FAILED (xasl_errcode is NOT set)
 *   ptr(in)    : pointer constant to be checked if visited or not
 *
 * Note: check if the node pointed by `ptr` is already stored or
 * not to avoid multiple store of the same node
 */
static int
xts_get_offset_visited_ptr (const void *ptr)
{
  int block_no;
  int element_no;

  block_no = PTR_BLOCK (ptr);

  if (xts_Ptr_lwm[block_no] <= 0)
    {
      return ER_FAILED;
    }

  for (element_no = 0; element_no < xts_Ptr_lwm[block_no]; element_no++)
    {
      if (ptr == xts_Ptr_blocks[block_no][element_no].ptr)
	{
	  return xts_Ptr_blocks[block_no][element_no].offset;
	}
    }

  return ER_FAILED;
}

/*
 * xts_free_visited_ptrs () -
 *   return:
 *
 * Note: free memory allocated to manage visited ptr constants
 */
static void
xts_free_visited_ptrs (void)
{
  int i;

  for (i = 0; i < MAX_PTR_BLOCKS; i++)
    {
      xts_Ptr_lwm[i] = 0;
    }
}

/*
 * xts_reserve_location_in_stream () -
 *   return: if successful, return the offset of position
 *           where the given item is to be stored, otherwise ER_FAILED
 *           and error code is set to xasl_errcode
 *   size(in)   : # of bytes of the node
 *
 * Note: reserve size bytes in the stream
 */
static int
xts_reserve_location_in_stream (int size)
{
  int needed;
  int grow;
  int org_size = size;

  size = MAKE_ALIGN (size);
  needed = size - (xts_Stream_size - xts_Free_offset_in_stream);

  if (needed >= 0)
    {
      /* expansion is needed */
      grow = needed;

      if (grow < (int) STREAM_EXPANSION_UNIT)
	{
	  grow = STREAM_EXPANSION_UNIT;
	}
      if (grow < (xts_Stream_size / 2))
	{
	  grow = xts_Stream_size / 2;
	}

      xts_Stream_size += grow;

      if (xts_Stream_buffer == NULL)
	{
	  xts_Stream_buffer = (char *) malloc (xts_Stream_size);
	}
      else
	{
	  xts_Stream_buffer = (char *) realloc (xts_Stream_buffer,
						xts_Stream_size);
	}

      if (xts_Stream_buffer == NULL)
	{
	  xts_Xasl_errcode = ER_OUT_OF_VIRTUAL_MEMORY;
	  return ER_FAILED;
	}
    }

#if !defined(NDEBUG)
  /* suppress valgrind UMW error */
  if (size > org_size)
    {
      memset (xts_Stream_buffer + xts_Free_offset_in_stream + org_size,
	      0, size - org_size);
    }
#endif

  xts_Free_offset_in_stream += size;
  assert ((xts_Free_offset_in_stream - size) % MAX_ALIGNMENT == 0);

  return (xts_Free_offset_in_stream - size);
}


/*
 * xts_process_regu_variable_list () -
 *   return:
 *   ptr(in):
 *   regu_value_list(in):
 */
static char *
xts_process_regu_variable_list (char *ptr,
				const REGU_VARIABLE_LIST regu_var_list)
{
  int offset = 0;

  assert (regu_var_list);
  /* save regu variable list */
  offset = xts_save_regu_variable_list (regu_var_list);
  if (offset == ER_FAILED)
    {
      return NULL;
    }
  ptr = or_pack_int (ptr, offset);

  return ptr;
}
