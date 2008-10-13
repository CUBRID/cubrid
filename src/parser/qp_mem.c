/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * query_mem.c - Query processor memory management module
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "error_manager.h"
#include "work_space.h"
#include "oid.h"
#include "class_object.h"
#include "memory_manager_2.h"
#include "qp_mem.h"
#include "qo.h"
#include "object_primitive.h"
#include "memory_manager_4.h"
#include "heap_file.h"

static void *regu_bytes_alloc (int length);
static void regu_dbvallist_init (QPROC_DB_VALUE_LIST ptr);
static void regu_var_init (REGU_VARIABLE * ptr);
static void regu_varlist_init (REGU_VARIABLE_LIST ptr);
static void regu_varlist_list_init (REGU_VARLIST_LIST ptr);
static void regu_vallist_init (VAL_LIST * ptr);
static void regu_outlist_init (OUTPTR_LIST * ptr);
static void regu_pred_init (PRED_EXPR * ptr);
static ARITH_TYPE *regu_arith_no_value_alloc (void);
static void regu_arith_init (ARITH_TYPE * ptr);
static FUNCTION_TYPE *regu_function_alloc (void);
static void regu_func_init (FUNCTION_TYPE * ptr);
static AGGREGATE_TYPE *regu_aggregate_alloc (void);
static void regu_agg_init (AGGREGATE_TYPE * ptr);
static XASL_NODE *regu_xasl_alloc (PROC_TYPE type);
static void regu_xasl_node_init (XASL_NODE * ptr, PROC_TYPE type);
static ACCESS_SPEC_TYPE *regu_access_spec_alloc (TARGET_TYPE type);
static void regu_spec_init (ACCESS_SPEC_TYPE * ptr, TARGET_TYPE type);
static SORT_LIST *regu_sort_alloc (void);
static void regu_sort_list_init (SORT_LIST * ptr);
static void regu_init_oid (OID * oidptr);
static QFILE_LIST_ID *regu_listid_alloc (void);
static void regu_listid_init (QFILE_LIST_ID * ptr);
static void regu_srlistid_init (QFILE_SORTED_LIST_ID * ptr);
static void regu_domain_init (SM_DOMAIN * ptr);
static void regu_cache_attrinfo_init (HEAP_CACHE_ATTRINFO * ptr);
static void regu_partition_info_init (XASL_PARTITION_INFO * ptr);
static void regu_parts_info_init (XASL_PARTS_INFO * ptr);
static void regu_selupd_list_init (SELUPD_LIST * ptr);

#define NULL_ATTRID -1

/*
 *       		  MEMORY FUNCTIONS FOR STRINGS
 */

/*
 * regu_bytes_alloc () - Memory allocation function for void *.
 *   return: void *
 *   length(in): length of the bytes to be allocated
 *   length(in) :
 */
static void *
regu_bytes_alloc (int length)
{
  void *ptr;

  if ((ptr = pt_alloc_packing_buf (length)) == (void *) NULL)
    {
      regu_set_error_with_zero_args (ER_REGU_NO_SPACE);
      return NULL;
    }
  else
    return ptr;
}

/*
 * regu_string_alloc () - Memory allocation function for CHAR *.
 *   return: char *
 *   length(in) : length of the string to be allocated
 */
char *
regu_string_alloc (int length)
{
  return (char *) regu_bytes_alloc (length);
}

/*
 * regu_string_db_alloc () -
 *   return: char *
 *   length(in) : length of the string to be allocated
 *
 * Note: Memory allocation function for CHAR * using malloc.
 */
char *
regu_string_db_alloc (int length)
{
  char *ptr;

  return (ptr = (char *) malloc (length));
}

/*
 * regu_string_ws_alloc () -
 *   return: char *
 *   length(in) : length of the string to be allocated
 *
 * Note: Memory allocation function for CHAR * using malloc.
 */

char *
regu_string_ws_alloc (int length)
{
  char *ptr;

  return (ptr = (char *) db_ws_alloc (length));
}

/*
 * regu_strdup () - Duplication function for string.
 *   return: char *
 *   srptr(in)  : pointer to the source string
 *   alloc(in)  : pointer to an allocation function
 */
char *
regu_strdup (const char *srptr, char *(*alloc) (int))
{
  char *dtptr;
  int len;

  if ((dtptr = alloc ((len = strlen (srptr) + 1))) == NULL)
    {
      return NULL;
    }

  /* because alloc may be bound to regu_bytes_alloc (which is a fixed-len
   * buffer allocator), we must guard against copying strings longer than
   * DB_MAX_STRING_LENGTH.  Otherwise, we get a corrupted heap seg fault.
   */
  len = (len > DB_MAX_STRING_LENGTH ? DB_MAX_STRING_LENGTH : len);
  dtptr[0] = '\0';
  strncat (dtptr, srptr, len);
  dtptr[len - 1] = '\0';
  return dtptr;
}

/*
 * regu_strcmp () - String comparison function.
 *   return: int
 *   name1(in)  : pointer to the first string
 *   name2(in)  : pointer to the second string
 *   function_strcmp(in): pointer to the function strcmp or ansisql_strcmp
 */
int
regu_strcmp (const char *name1, const char *name2,
	     int (*function_strcmp) (const char *, const char *))
{
  int i;

  if (name1 == NULL && name2 == NULL)
    {
      return 0;
    }
  else if (name1 == NULL)
    {
      return -2;
    }
  else if (name2 == NULL)
    {
      return 2;
    }
  else if ((i = function_strcmp (name1, name2)) == 0)
    {
      return 0;
    }
  else
    return ((i < 0) ? -1 : 1);
}

/*
 *       		MEMORY FUNCTIONS FOR DB_VALUE
 */

/*
 * regu_dbval_db_alloc () -
 *   return: DB_VALUE *
 *
 * Note: Memory allocation function for DB_VALUE using malloc.
 */
DB_VALUE *
regu_dbval_db_alloc (void)
{
  DB_VALUE *ptr;

  ptr = (DB_VALUE *) malloc (sizeof (DB_VALUE));
  if (ptr == NULL)
    {
      regu_set_error_with_zero_args (ER_REGU_NO_SPACE);
      return NULL;
    }
  else
    {
      regu_dbval_init (ptr);
      return ptr;
    }
}

/*
 * regu_dbval_alloc () -
 *   return: DB_VALUE *
 *
 * Note: Memory allocation function for X_VARIABLE.
 */
DB_VALUE *
regu_dbval_alloc (void)
{
  DB_VALUE *ptr;

  ptr = (DB_VALUE *) pt_alloc_packing_buf (sizeof (DB_VALUE));
  if (ptr == NULL)
    {
      regu_set_error_with_zero_args (ER_REGU_NO_SPACE);
      return NULL;
    }
  else
    {
      regu_dbval_init (ptr);
      return ptr;
    }
}

/*
 * regu_dbval_init () - Initialization function for DB_VALUE.
 *   return: int
 *   ptr(in)    : pointer to an DB_VALUE
 */
int
regu_dbval_init (DB_VALUE * ptr)
{
  if (db_value_domain_init (ptr, DB_TYPE_NULL,
			    DB_DEFAULT_PRECISION,
			    DB_DEFAULT_SCALE) != NO_ERROR)
    {
      return false;
    }
  else
    {
      return true;
    }
}


/*
 * regu_dbval_type_init () -
 *   return: int
 *   ptr(in)    : pointer to an DB_VALUE
 *   type(in)   : a primitive data type
 *
 * Note: Initialization function for DB_VALUE with type argument.
 */
int
regu_dbval_type_init (DB_VALUE * ptr, DB_TYPE type)
{
  if (db_value_domain_init (ptr, type, DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE)
      != NO_ERROR)
    {
      return false;
    }
  else
    {
      return true;
    }
}

/*
 * regu_dbvalptr_array_alloc () -
 *   return: DB_VALUE **
 *   size(in): size of the array to be allocated
 *
 * Note: Memory allocation function for arrays of DB_VALUE pointers
 *       allocated with the default memory manager.
 */
DB_VALUE **
regu_dbvalptr_array_alloc (int size)
{
  DB_VALUE **ptr;

  if (size == 0)
    return NULL;

  ptr = (DB_VALUE **) pt_alloc_packing_buf (sizeof (DB_VALUE *) * size);
  if (ptr == NULL)
    {
      regu_set_error_with_zero_args (ER_REGU_NO_SPACE);
      return NULL;
    }
  else
    {
      return ptr;
    }
}

/*
 * regu_dbvallist_alloc () -
 *   return: QP_DB_VALUE_LIST
 *
 * Note: Memory allocation function for QP_DB_VALUE_LIST with the
 *              allocation of a DB_VALUE for the value field.
 */
QPROC_DB_VALUE_LIST
regu_dbvallist_alloc (void)
{
  QPROC_DB_VALUE_LIST ptr;

  ptr = regu_dbvlist_alloc ();
  if (ptr == NULL)
    {
      return NULL;
    }
  
  ptr->val = regu_dbval_alloc ();
  if (ptr->val == NULL)
    {
      return NULL;
    }
  
  return ptr;
}

/*
 * regu_dbvlist_alloc () -
 *   return: QPROC_DB_VALUE_LIST 				       
 * 									       
 * Note: Memory allocation function for QPROC_DB_VALUE_LIST.               
 */
QPROC_DB_VALUE_LIST
regu_dbvlist_alloc (void)
{
  QPROC_DB_VALUE_LIST ptr;
  size_t size;

  size = sizeof (struct qproc_db_value_list);
  ptr = (QPROC_DB_VALUE_LIST) pt_alloc_packing_buf (size);

  if (ptr == NULL)
    {
      regu_set_error_with_zero_args (ER_REGU_NO_SPACE);
      return NULL;
    }
  else
    {
      regu_dbvallist_init (ptr);
      return ptr;
    }
}

/*
 * regu_dbvallist_init () -
 *   return:
 *   ptr(in)    : pointer to an QPROC_DB_VALUE_LIST
 *
 * Note: Initialization function for QPROC_DB_VALUE_LIST.
 */
static void
regu_dbvallist_init (QPROC_DB_VALUE_LIST ptr)
{
  ptr->next = NULL;
  ptr->val = NULL;
}

/*
 *       	       MEMORY FUNCTIONS FOR REGU_VARIABLE
 */

/*
 * regu_var_alloc () -
 *   return: REGU_VARIABLE *
 *
 * Note: Memory allocation function for REGU_VARIABLE.
 */
REGU_VARIABLE *
regu_var_alloc (void)
{
  REGU_VARIABLE *ptr;

  ptr = (REGU_VARIABLE *) pt_alloc_packing_buf (sizeof (REGU_VARIABLE));
  if (ptr == NULL)
    {
      regu_set_error_with_zero_args (ER_REGU_NO_SPACE);
      return NULL;
    }
  else
    {
      regu_var_init (ptr);
      return ptr;
    }
}

/*
 * regu_var_init () -
 *   return:
 *   ptr(in)    : pointer to a regu_variable
 *
 * Note: Initialization function for REGU_VARIABLE.
 */
static void
regu_var_init (REGU_VARIABLE * ptr)
{
  ptr->type = TYPE_POS_VALUE;
  ptr->hidden_column = 0;
  ptr->value.val_pos = 0;
  ptr->vfetch_to = NULL;
  REGU_VARIABLE_XASL (ptr) = NULL;
}

/*
 * regu_varlist_alloc () -
 *   return: REGU_VARIABLE_LIST
 *
 * Note: Memory allocation function for REGU_VARIABLE_LIST.
 */
REGU_VARIABLE_LIST
regu_varlist_alloc (void)
{
  REGU_VARIABLE_LIST ptr;
  size_t size;

  size = sizeof (struct regu_variable_list_node);
  ptr = (REGU_VARIABLE_LIST) pt_alloc_packing_buf (size);

  if (ptr == NULL)
    {
      regu_set_error_with_zero_args (ER_REGU_NO_SPACE);
      return NULL;
    }
  else
    {
      regu_varlist_init (ptr);
      return ptr;
    }
}

/*
 * regu_varlist_init () -
 *   return:
 *   ptr(in)    : pointer to a regu_variable_list
 *
 * Note: Initialization function for regu_variable_list.
 */
static void
regu_varlist_init (REGU_VARIABLE_LIST ptr)
{
  ptr->next = NULL;
  regu_var_init (&ptr->value);
}

/*
 * regu_varptr_array_alloc () -
 *   return: REGU_VARIABLE **
 *   size: size of the array to be allocated
 *
 * Note: Memory allocation function for arrays of REGU_VARIABLE
 *       pointers allocated with the default memory manager.
 */
REGU_VARIABLE **
regu_varptr_array_alloc (int size)
{
  REGU_VARIABLE **ptr;

  if (size == 0)
    return NULL;

  ptr = (REGU_VARIABLE **) pt_alloc_packing_buf (sizeof (REGU_VARIABLE *) *
						 size);
  if (ptr == NULL)
    {
      regu_set_error_with_zero_args (ER_REGU_NO_SPACE);
      return NULL;
    }
  else
    {
      return ptr;
    }
}

/*
 * regu_varlist_list_init () -
 *   return:
 *   ptr(in)    : pointer to a regu_varlist_list
 *
 * Note: Initialization function for regu_varlist_list.
 */
static void
regu_varlist_list_init (REGU_VARLIST_LIST ptr)
{
  ptr->next = NULL;
  ptr->list = NULL;
}

/*
 * regu_varlist_list_alloc () -
 *   return:
 */
REGU_VARLIST_LIST
regu_varlist_list_alloc (void)
{
  REGU_VARLIST_LIST ptr;
  size_t size;

  size = sizeof (struct regu_variable_list_node);
  ptr = (REGU_VARLIST_LIST) pt_alloc_packing_buf (size);

  if (ptr == NULL)
    {
      regu_set_error_with_zero_args (ER_REGU_NO_SPACE);
      return NULL;
    }
  else
    {
      regu_varlist_list_init (ptr);
      return ptr;
    }
}

/*
 *       	       MEMORY FUNCTIONS FOR POINTER LISTS
 */
/*
 * regu_vallist_alloc () -
 *   return: VAL_LIST
 *
 * Note: Memory allocation function for VAL_LIST.
 */
VAL_LIST *
regu_vallist_alloc (void)
{
  VAL_LIST *ptr;

  ptr = (VAL_LIST *) pt_alloc_packing_buf (sizeof (VAL_LIST));
  if (ptr == NULL)
    {
      regu_set_error_with_zero_args (ER_REGU_NO_SPACE);
      return NULL;
    }
  else
    {
      regu_vallist_init (ptr);
      return ptr;
    }
}

/*
 * regu_vallist_init () -
 *   return:
 *   ptr(in)    : pointer to a value list
 *
 * Note: Initialization function for VAL_LIST.
 */
static void
regu_vallist_init (VAL_LIST * ptr)
{
  ptr->val_cnt = 0;
  ptr->valp = NULL;
}

/*
 * regu_outlist_alloc () -
 *   return: OUTPTR_LIST *
 *
 * Note: Memory allocation function for OUTPTR_LIST.
 */
OUTPTR_LIST *
regu_outlist_alloc (void)
{
  OUTPTR_LIST *ptr;

  ptr = (OUTPTR_LIST *) pt_alloc_packing_buf (sizeof (OUTPTR_LIST));

  if (ptr == NULL)
    {
      regu_set_error_with_zero_args (ER_REGU_NO_SPACE);
      return NULL;
    }
  else
    {
      regu_outlist_init (ptr);
      return ptr;
    }
}

/*
 * regu_outlist_init () -
 *   return:
 *   ptr(in)    : pointer to an output pointer list
 *
 * Note: Initialization function for OUTPTR_LIST.
 */
static void
regu_outlist_init (OUTPTR_LIST * ptr)
{
  ptr->valptr_cnt = 0;
  ptr->valptrp = NULL;
}

/*
 *       	   MEMORY FUNCTIONS FOR EXPRESSION STRUCTURES
 */

/*
 * regu_pred_alloc () -
 *   return: PRED_EXPR *
 *
 * Note: Memory allocation function for PRED_EXPR.
 */
PRED_EXPR *
regu_pred_alloc (void)
{
  PRED_EXPR *ptr;

  ptr = (PRED_EXPR *) pt_alloc_packing_buf (sizeof (PRED_EXPR));

  if (ptr == NULL)
    {
      regu_set_error_with_zero_args (ER_REGU_NO_SPACE);
      return NULL;
    }
  else
    {
      regu_pred_init (ptr);
      return ptr;
    }
}

/*
 * regu_pred_init () -
 *   return:
 *   ptr(in)    : pointer to a predicate expression
 *
 * Note: Initialization function for PRED_EXPR.
 */
static void
regu_pred_init (PRED_EXPR * ptr)
{
  ptr->type = T_NOT_TERM;
  ptr->pe.not_term = NULL;
}

/*
 * regu_arith_alloc () -
 *   return: ARITH_TYPE *
 *
 * Note: Memory allocation function for ARITH_TYPE with the allocation
 *       of a db_value for the value field.
 */
ARITH_TYPE *
regu_arith_alloc (void)
{
  ARITH_TYPE *arithptr;

  arithptr = regu_arith_no_value_alloc ();
  if (arithptr == NULL)
    {
      return NULL;
    }
  
  arithptr->value = regu_dbval_alloc ();
  if (arithptr->value == NULL)
    {
      return NULL;
    }
  
  return arithptr;
}

/*
 * regu_arith_no_value_alloc () -
 *   return: ARITH_TYPE *
 *
 * Note: Memory allocation function for ARITH_TYPE.
 */
static ARITH_TYPE *
regu_arith_no_value_alloc (void)
{
  ARITH_TYPE *ptr;

  ptr = (ARITH_TYPE *) pt_alloc_packing_buf (sizeof (ARITH_TYPE));
  if (ptr == NULL)
    {
      regu_set_error_with_zero_args (ER_REGU_NO_SPACE);
      return NULL;
    }
  else
    {
      regu_arith_init (ptr);
      return ptr;
    }
}

/*
 * regu_arith_init () -
 *   return:
 *   ptr(in)    : pointer to an arithmetic node
 *
 * Note: Initialization function for ARITH_TYPE.
 */
static void
regu_arith_init (ARITH_TYPE * ptr)
{
  ptr->next = NULL;
  ptr->domain = NULL;
  ptr->value = NULL;
  ptr->opcode = T_ADD;
  ptr->leftptr = NULL;
  ptr->rightptr = NULL;
  ptr->thirdptr = NULL;
  ptr->misc_operand = LEADING;
}

/*
 * regu_func_alloc () -
 *   return: FUNCTION_TYPE *
 *
 * Note: Memory allocation function for FUNCTION_TYPE with the
 *       allocation of a db_value for the value field
 */
FUNCTION_TYPE *
regu_func_alloc (void)
{
  FUNCTION_TYPE *funcp;

  funcp = regu_function_alloc ();
  if (funcp == NULL)
    {
      return NULL;
    }
  
  funcp->value = regu_dbval_alloc ();
  if (funcp->value == NULL)
    {
      return NULL;
    }
  
  return funcp;
}

/*
 * regu_function_alloc () -
 *   return: FUNCTION_TYPE *
 *
 * Note: Memory allocation function for FUNCTION_TYPE.
 */
static FUNCTION_TYPE *
regu_function_alloc (void)
{
  FUNCTION_TYPE *ptr;

  ptr = (FUNCTION_TYPE *) pt_alloc_packing_buf (sizeof (FUNCTION_TYPE));
  if (ptr == NULL)
    {
      regu_set_error_with_zero_args (ER_REGU_NO_SPACE);
      return NULL;
    }
  else
    {
      regu_func_init (ptr);
      return ptr;
    }
}

/*
 * regu_func_init () -
 *   return:
 *   ptr(in)    : pointer to a function structure
 *
 * Note: Initialization function for FUNCTION_TYPE.
 */
static void
regu_func_init (FUNCTION_TYPE * ptr)
{
  ptr->value = NULL;
  ptr->ftype = (FUNC_TYPE) 0;
  ptr->operand = NULL;
}

/*
 * regu_agg_alloc () -
 *   return: AGGREGATE_TYPE *
 *
 * Note: Memory allocation function for AGGREGATE_TYPE with the
 *       allocation of a DB_VALUE for the value field and a list id
 *       structure for the list_id field.
 */
AGGREGATE_TYPE *
regu_agg_alloc (void)
{
  AGGREGATE_TYPE *aggptr;

  aggptr = regu_aggregate_alloc ();
  if (aggptr == NULL)
    {
      return NULL;
    }
  
  aggptr->value = regu_dbval_alloc ();
  if (aggptr->value == NULL)
    {
      return NULL;
    }
  
  aggptr->value2 = regu_dbval_alloc ();
  if (aggptr->value2 == NULL)
    {
      return NULL;
    }
  
  aggptr->list_id = regu_listid_alloc ();
  if (aggptr->list_id == NULL)
    {
      return NULL;
    }
  
  return aggptr;
}

/*
 * regu_agg_grbynum_alloc () -
 *   return:
 */
AGGREGATE_TYPE *
regu_agg_grbynum_alloc (void)
{
  AGGREGATE_TYPE *aggptr;

  aggptr = regu_aggregate_alloc ();
  if (aggptr == NULL)
    {
      return NULL;
    }
  
  aggptr->value = NULL;
  aggptr->value2 = NULL;
  aggptr->list_id = NULL;
  
  return aggptr;
}

/*
 * regu_aggregate_alloc () -
 *   return: AGGREGATE_TYPE *
 *
 * Note: Memory allocation function for AGGREGATE_TYPE.
 */
static AGGREGATE_TYPE *
regu_aggregate_alloc (void)
{
  AGGREGATE_TYPE *ptr;

  ptr = (AGGREGATE_TYPE *) pt_alloc_packing_buf (sizeof (AGGREGATE_TYPE));
  if (ptr == NULL)
    {
      regu_set_error_with_zero_args (ER_REGU_NO_SPACE);
      return NULL;
    }
  else
    {
      regu_agg_init (ptr);
      return ptr;
    }
}

/*
 * regu_agg_init () -
 *   return:
 *   ptr(in)    : pointer to an aggregate structure
 *
 * Note: Initialization function for AGGREGATE_TYPE.
 */
static void
regu_agg_init (AGGREGATE_TYPE * ptr)
{
  ptr->next = NULL;
  ptr->value = NULL;
  ptr->value2 = NULL;
  ptr->curr_cnt = 0;
  ptr->function = (FUNC_TYPE) 0;
  ptr->option = (QUERY_OPTIONS) 0;
  regu_var_init (&ptr->operand);
  ptr->list_id = NULL;
}

/*
 *       		 MEMORY FUNCTIONS FOR XASL TREE
 */

/*
 * regu_xasl_node_alloc () -
 *   return: XASL_NODE *
 *   type(in)   : xasl proc type
 * 									       
 * Note: Memory allocation function for XASL_NODE with the allocation   
 *       a QFILE_LIST_ID structure for the list id field.		       
 */
XASL_NODE *
regu_xasl_node_alloc (PROC_TYPE type)
{
  XASL_NODE *xasl;

  xasl = regu_xasl_alloc (type);
  if (xasl == NULL)
    {
      return NULL;
    }
  
  xasl->list_id = regu_listid_alloc ();
  if (xasl->list_id == NULL)
    {
      return NULL;
    }
  
  return xasl;
}

/*
 * regu_xasl_alloc () -
 *   return: XASL_NODE *
 *   type(in): xasl proc type
 *
 * Note: Memory allocation function for XASL_NODE.
 */
static XASL_NODE *
regu_xasl_alloc (PROC_TYPE type)
{
  XASL_NODE *ptr;

  ptr = (XASL_NODE *) pt_alloc_packing_buf (sizeof (XASL_NODE));
  if (ptr == NULL)
    {
      regu_set_error_with_zero_args (ER_REGU_NO_SPACE);
      return NULL;
    }
  else
    {
      regu_xasl_node_init (ptr, type);
      return ptr;
    }
}

/*
 * regu_xasl_node_init () -
 *   return:
 *   ptr(in)    : pointer to an xasl structure
 *   type(in)   : xasl proc type
 *
 * Note: Initialization function for XASL_NODE.
 */
static void
regu_xasl_node_init (XASL_NODE * ptr, PROC_TYPE type)
{
  memset ((char *) ptr, 0x00, sizeof (XASL_NODE));

  ptr->type = type;
  ptr->option = Q_ALL;
  
  switch (type)
    {
    case UNION_PROC:
    case DIFFERENCE_PROC:
    case INTERSECTION_PROC:
      ptr->option = Q_DISTINCT;
      break;

    case OBJFETCH_PROC:
    case SETFETCH_PROC:
      break;

    case BUILDLIST_PROC:
      break;

    case BUILDVALUE_PROC:
      break;

    case MERGELIST_PROC:
      break;

    case SCAN_PROC:
    case READ_MPROC:
    case READ_PROC:
      break;

    case UPDATE_PROC:
      break;

    case DELETE_PROC:
      break;

    case INSERT_PROC:
      break;
    }
}

/*
 * regu_spec_alloc () -
 *   return: ACCESS_SPEC_TYPE *
 *   type(in)   : target type: TARGET_CLASS/TARGET_LIST/TARGET_SET
 * 									       
 * Note: Memory allocation function for ACCESS_SPEC_TYPE with the       
 *       allocation of a QFILE_LIST_ID structure for the list_id field of     
 *       list file target.       				       
 */
ACCESS_SPEC_TYPE *
regu_spec_alloc (TARGET_TYPE type)
{
  ACCESS_SPEC_TYPE *ptr;

  ptr = regu_access_spec_alloc (type);
  if (ptr == NULL)
    {
      return NULL;
    }
  
  return ptr;
}

/*
 * regu_access_spec_alloc () -
 *   return: ACCESS_SPEC_TYPE *
 *   type(in): TARGET_CLASS/TARGET_LIST/TARGET_SET
 *
 * Note: Memory allocation function for ACCESS_SPEC_TYPE.
 */
static ACCESS_SPEC_TYPE *
regu_access_spec_alloc (TARGET_TYPE type)
{
  ACCESS_SPEC_TYPE *ptr;

  ptr = (ACCESS_SPEC_TYPE *) pt_alloc_packing_buf (sizeof (ACCESS_SPEC_TYPE));
  if (ptr == NULL)
    {
      regu_set_error_with_zero_args (ER_REGU_NO_SPACE);
      return NULL;
    }
  else
    {
      regu_spec_init (ptr, type);
      return ptr;
    }
}

/*
 * regu_spec_init () -
 *   return:
 *   ptr(in)    : pointer to an access specification structure
 *   type(in)   : TARGET_CLASS/TARGET_LIST
 *
 * Note: Initialization function for ACCESS_SPEC_TYPE.
 */
static void
regu_spec_init (ACCESS_SPEC_TYPE * ptr, TARGET_TYPE type)
{
  ptr->type = type;
  ptr->access = SEQUENTIAL;
  ptr->indexptr = NULL;
  ptr->where_key = NULL;
  ptr->where_pred = NULL;
  
  if ((type == TARGET_CLASS) || (type == TARGET_CLASS_ATTR))
    {
      ptr->s.cls_node.cls_regu_list_key = NULL;
      ptr->s.cls_node.cls_regu_list_pred = NULL;
      ptr->s.cls_node.cls_regu_list_rest = NULL;
      ACCESS_SPEC_HFID (ptr).vfid.fileid = NULL_FILEID;
      ACCESS_SPEC_HFID (ptr).vfid.volid = NULL_VOLID;
      ACCESS_SPEC_HFID (ptr).hpgid = NULL_PAGEID;
      regu_init_oid (&ACCESS_SPEC_CLS_OID (ptr));
    }
  else if (type == TARGET_LIST)
    {
      ptr->s.list_node.list_regu_list_pred = NULL;
      ptr->s.list_node.list_regu_list_rest = NULL;
      ACCESS_SPEC_XASL_NODE (ptr) = NULL;
    }
  else if (type == TARGET_SET)
    {
      ACCESS_SPEC_SET_REGU_LIST (ptr) = NULL;
      ACCESS_SPEC_SET_PTR (ptr) = NULL;
    }
  else if (type == TARGET_METHOD)
    {
      ACCESS_SPEC_METHOD_REGU_LIST (ptr) = NULL;
      ACCESS_SPEC_XASL_NODE (ptr) = NULL;
      ACCESS_SPEC_METHOD_SIG_LIST (ptr) = NULL;
    }

  memset ((void *) &ptr->s_id, 0, sizeof (SCAN_ID));
  ptr->grouped_scan = false;
  ptr->fixed_scan = false;
  ptr->qualified_block = false;
  ptr->single_fetch = (QPROC_SINGLE_FETCH) false;
  ptr->s_dbval = NULL;
  ptr->next = NULL;
}

/*
 * regu_index_alloc () -
 *   return: INDX_INFO *
 *
 * Note: Memory allocation function for INDX_INFO.
 */
INDX_INFO *
regu_index_alloc (void)
{
  INDX_INFO *ptr;

  ptr = (INDX_INFO *) pt_alloc_packing_buf (sizeof (INDX_INFO));
  if (ptr == NULL)
    {
      regu_set_error_with_zero_args (ER_REGU_NO_SPACE);
      return NULL;
    }
  else
    {
      regu_index_init (ptr);
      return ptr;
    }
}

/*
 * regu_index_init () -
 *   return:
 *   ptr(in)    : pointer to an index structure
 *
 * Note: Initialization function for INDX_INFO.
 */
void
regu_index_init (INDX_INFO * ptr)
{
  ptr->range_type = R_KEY;
  ptr->key_info.key_cnt = 0;
  ptr->key_info.key_ranges = NULL;
  ptr->key_info.is_constant = false;
}

/*
 * regu_keyrange_init () -
 *   return:
 *   ptr(in)    : pointer to an key range structure
 *
 * Note: Initialization function for KEY_RANGE.
 */
void
regu_keyrange_init (KEY_RANGE * ptr)
{
  ptr->range = NA_NA;
  ptr->key1 = NULL;
  ptr->key2 = NULL;
}

/*
 * regu_keyrange_array_alloc () -
 *   return: KEY_RANGE *
 *
 * Note: Memory allocation function for KEY_RANGE.
 */
KEY_RANGE *
regu_keyrange_array_alloc (int size)
{
  KEY_RANGE *ptr;
  int i;

  if (size == 0)
    return NULL;

  ptr = (KEY_RANGE *) pt_alloc_packing_buf (sizeof (KEY_RANGE) * size);
  if (ptr == NULL)
    {
      regu_set_error_with_zero_args (ER_REGU_NO_SPACE);
      return NULL;
    }
  else
    {
      for (i = 0; i < size; i++)
	{
	  regu_keyrange_init (ptr + i);
	}
      return ptr;
    }
}

/*
 * regu_sort_list_alloc () -
 *   return: SORT_LIST *
 *
 * Note: Memory allocation function for SORT_LIST.
 */
SORT_LIST *
regu_sort_list_alloc (void)
{
  SORT_LIST *ptr;

  ptr = regu_sort_alloc ();
  if (ptr == NULL)
    {
      return NULL;
    }
  return ptr;
}

/*
 * regu_sort_alloc () -
 *   return: SORT_LIST *
 *
 * Note: Memory allocation function for SORT_LIST.
 */
static SORT_LIST *
regu_sort_alloc (void)
{
  SORT_LIST *ptr;

  ptr = (SORT_LIST *) pt_alloc_packing_buf (sizeof (SORT_LIST));
  if (ptr == NULL)
    {
      regu_set_error_with_zero_args (ER_REGU_NO_SPACE);
      return NULL;
    }
  else
    {
      regu_sort_list_init (ptr);
      return ptr;
    }
}

/*
 * regu_sort_list_init () -
 *   return:
 *   ptr(in)    : pointer to a list of sorting specifications
 *
 * Note: Initialization function for SORT_LIST.
 */
static void
regu_sort_list_init (SORT_LIST * ptr)
{
  ptr->next = NULL;
  ptr->pos_descr.pos_no = 0;
  ptr->pos_descr.dom = &tp_Integer_domain;
  ptr->s_order = S_ASC;
}

/*
 *       	       MEMORY FUNCTIONS FOR PHYSICAL ID'S
 */

/*
 * regu_init_oid () -
 *   return:
 *   oidptr(in) : pointer to an oid structure
 *
 * Note: Initialization function for OID.
 */
static void
regu_init_oid (OID * oidptr)
{
  OID_SET_NULL (oidptr);
}

/*
 *       	     MEMORY FUNCTIONS FOR LIST ID
 */

/*
 * regu_listid_alloc () -
 *   return: QFILE_LIST_ID *
 * 									       
 * Note: Memory allocation function for QFILE_LIST_ID.			       
 */
static QFILE_LIST_ID *
regu_listid_alloc (void)
{
  QFILE_LIST_ID *ptr;

  ptr = (QFILE_LIST_ID *) pt_alloc_packing_buf (sizeof (QFILE_LIST_ID));
  if (ptr == NULL)
    {
      regu_set_error_with_zero_args (ER_REGU_NO_SPACE);
      return NULL;
    }
  else
    {
      regu_listid_init (ptr);
      return ptr;
    }
}

/*
 * regu_listid_db_alloc () -
 *   return: QFILE_LIST_ID *
 * 									       
 * Note: Memory allocation function for QFILE_LIST_ID using malloc.	       
 */
QFILE_LIST_ID *
regu_listid_db_alloc (void)
{
  QFILE_LIST_ID *ptr;

  ptr = (QFILE_LIST_ID *) malloc (sizeof (QFILE_LIST_ID));
  if (ptr == NULL)
    {
      regu_set_error_with_zero_args (ER_REGU_NO_SPACE);
      return NULL;
    }
  else
    {
      regu_listid_init (ptr);
      return ptr;
    }
}

/*
 * regu_listid_init () -
 *   return:
 *   ptr(in)    : pointer to a list_id structure
 * 									       
 * Note: Initialization function for QFILE_LIST_ID.			       
 */
static void
regu_listid_init (QFILE_LIST_ID * ptr)
{
  QFILE_CLEAR_LIST_ID (ptr);
}

/*
 * regu_cp_listid () -
 *   return: bool
 *   dst_list_id(in)    : pointer to the destination list_id
 *   src_list_id(in)    : pointer to the source list_id
 * 									       
 * Note: Copy function for QFILE_LIST_ID.				       
 */
int
regu_cp_listid (QFILE_LIST_ID * dst_list_id, QFILE_LIST_ID * src_list_id)
{
  return cursor_copy_list_id (dst_list_id, src_list_id);
}

/*
 * regu_free_listid () -
 *   return:
 *   list_id(in)        : pointer to a list_id structure
 * 									       
 * Note: Free function for QFILE_LIST_ID using free_and_init.        	       
 */
void
regu_free_listid (QFILE_LIST_ID * list_id)
{
  if (list_id != NULL)
    {
      cursor_free_list_id (list_id, true);
    }
}

/*
 * regu_srlistid_alloc () -
 *   return: QFILE_SORTED_LIST_ID *
 * 									       
 * Note: Memory allocation function for QFILE_SORTED_LIST_ID.		       
 */
QFILE_SORTED_LIST_ID *
regu_srlistid_alloc (void)
{
  QFILE_SORTED_LIST_ID *ptr;
  size_t size;

  size = sizeof (QFILE_SORTED_LIST_ID);
  ptr = (QFILE_SORTED_LIST_ID *) pt_alloc_packing_buf (size);

  if (ptr == NULL)
    {
      regu_set_error_with_zero_args (ER_REGU_NO_SPACE);
      return NULL;
    }
  else
    {
      regu_srlistid_init (ptr);
      return ptr;
    }
}

/*
 * regu_srlistid_init () -
 *   return:
 *   ptr(in)    : pointer to a srlist_id structure
 * 									       
 * Note: Initialization function for QFILE_SORTED_LIST_ID.			       
 */
static void
regu_srlistid_init (QFILE_SORTED_LIST_ID * ptr)
{
  ptr->sorted = false;
  ptr->list_id = NULL;
}

/*
 *       		 MEMORY FUNCTIONS FOR SM_DOMAIN
 */

/*
 * regu_domain_db_alloc () -
 *   return: SM_DOMAIN *
 *
 * Note: Memory allocation function for SM_DOMAIN using malloc.
 */
SM_DOMAIN *
regu_domain_db_alloc (void)
{
  SM_DOMAIN *ptr;

  ptr = (SM_DOMAIN *) malloc (sizeof (SM_DOMAIN));
  if (ptr == NULL)
    {
      regu_set_error_with_zero_args (ER_REGU_NO_SPACE);
      return NULL;
    }
  else
    {
      regu_domain_init (ptr);
      return ptr;
    }
}

/*
 * regu_domain_init () -
 *   return:
 *   ptr(in)    : pointer to a schema manager domain
 *
 * Note: Initialization function for SM_DOMAIN.
 */
static void
regu_domain_init (SM_DOMAIN * ptr)
{
  ptr->type = PR_TYPE_FROM_ID (DB_TYPE_INTEGER);
  ptr->class_mop = NULL;
  ptr->next = NULL;
  ptr->setdomain = NULL;
}

/*
 * regu_free_domain () -
 *   return:
 *   ptr(in)    : pointer to a schema manager domain
 *
 * Note: Free function for SM_DOMAIN using free_and_init.
 */
void
regu_free_domain (SM_DOMAIN * ptr)
{
  if (ptr != NULL)
    {
      regu_free_domain (ptr->next);
      regu_free_domain (ptr->setdomain);
      free_and_init (ptr);
    }
}

/*
 * regu_cp_domain () -
 *   return: SM_DOMAIN *
 *   ptr(in)    : pointer to a schema manager domain
 *
 * Note: Copy function for SM_DOMAIN.
 */
SM_DOMAIN *
regu_cp_domain (SM_DOMAIN * ptr)
{
  SM_DOMAIN *new_ptr;

  if (ptr == NULL)
    {
      return NULL;
    }
  
  new_ptr = regu_domain_db_alloc ();
  if (new_ptr == NULL)
    {
      return NULL;
    }
  *new_ptr = *ptr;
  
  if (ptr->next != NULL)
    {
      new_ptr->next = regu_cp_domain (ptr->next);
      if (new_ptr->next == NULL)
	{
	  return NULL;
	}
    }
  
  if (ptr->setdomain != NULL)
    {
      new_ptr->setdomain = regu_cp_domain (ptr->setdomain);
      if (new_ptr->setdomain == NULL)
	{
	  return NULL;
	}
    }
  
  return new_ptr;
}

/*
 * regu_int_init () -
 *   return:
 *   ptr(in)    : pointer to an int
 *
 * Note: Initialization function for int.
 */
void
regu_int_init (int *ptr)
{
  *ptr = 0;
}

/*
 * regu_int_array_alloc () -
 *   return: int *
 *   size(in): size of the array to be allocated
 *
 * Note: Memory allocation function for arrays of int
 */
int *
regu_int_array_alloc (int size)
{
  int *ptr;
  int i;

  if (size == 0)
    return NULL;

  ptr = (int *) pt_alloc_packing_buf (sizeof (int) * size);
  if (ptr == NULL)
    {
      regu_set_error_with_zero_args (ER_REGU_NO_SPACE);
      return NULL;
    }
  else
    {
      for (i = 0; i < size; i++)
	{
	  regu_int_init (ptr + i);
	}
      return ptr;
    }
}

/*
 * regu_int_array_db_alloc () -
 *   return: int *
 *   size(in): size of the array to be allocated
 *
 * Note: Memory allocation function for arrays of int using malloc.
 */
int *
regu_int_array_db_alloc (int size)
{
  int *ptr;
  int i;

  if (size == 0)
    return NULL;

  ptr = (int *) malloc (sizeof (int) * size);
  if (ptr == NULL)
    {
      regu_set_error_with_zero_args (ER_REGU_NO_SPACE);
      return NULL;
    }
  else
    {
      for (i = 0; i < size; i++)
	{
	  regu_int_init (ptr + i);
	}
      return ptr;
    }
}

/*
 * regu_cache_attrinfo_alloc () -
 *   return: HEAP_CACHE_ATTRINFO *
 * 									       
 * Note: Memory allocation function for HEAP_CACHE_ATTRINFO   
 */
HEAP_CACHE_ATTRINFO *
regu_cache_attrinfo_alloc (void)
{
  HEAP_CACHE_ATTRINFO *ptr;
  size_t size;

  size = sizeof(HEAP_CACHE_ATTRINFO);
  ptr = (HEAP_CACHE_ATTRINFO *) pt_alloc_packing_buf (size);
  if (ptr == NULL)
    {
      regu_set_error_with_zero_args (ER_REGU_NO_SPACE);
      return NULL;
    }
  else
    {
      regu_cache_attrinfo_init (ptr);
      return ptr;
    }
}

/*
 * regu_cache_attrinfo_init () -
 *   return:
 *   ptr(in)    : pointer to a cache_attrinfo structure
 *
 * Note: Initialization function for HEAP_CACHE_ATTRINFO.
 */
static void
regu_cache_attrinfo_init (HEAP_CACHE_ATTRINFO * ptr)
{
  memset (ptr, 0, sizeof (HEAP_CACHE_ATTRINFO));
}

/*
 * regu_oid_init () -
 *   return:
 *   ptr(in)    : pointer to a OID
 *
 * Note: Initialization function for OID.
 */
void
regu_oid_init (OID * ptr)
{
  OID_SET_NULL (ptr);
}

/*
 * regu_oid_array_alloc () -
 *   return: OID *
 *   size(in): size of the array to be allocated
 *
 * Note: Memory allocation function for arrays of OID
 */
OID *
regu_oid_array_alloc (int size)
{
  OID *ptr;
  int i;

  if (size == 0)
    return NULL;

  ptr = (OID *) pt_alloc_packing_buf (sizeof (OID) * size);
  if (ptr == NULL)
    {
      regu_set_error_with_zero_args (ER_REGU_NO_SPACE);
      return NULL;
    }
  else
    {
      for (i = 0; i < size; i++)
	{
	  regu_oid_init (ptr + i);
	}
      return ptr;
    }
}

/*
 * regu_hfid_init () -
 *   return:
 *   ptr(in)    : pointer to a HFID
 *
 * Note: Initialization function for HFID.
 */
void
regu_hfid_init (HFID * ptr)
{
  HFID_SET_NULL (ptr);
}

/*
 * regu_hfid_array_alloc () -
 *   return: HFID *
 *   size(in): size of the array to be allocated
 *
 * Note: Memory allocation function for arrays of HFID
 */
HFID *
regu_hfid_array_alloc (int size)
{
  HFID *ptr;
  int i;

  if (size == 0)
    return NULL;

  ptr = (HFID *) pt_alloc_packing_buf (sizeof (HFID) * size);
  if (ptr == NULL)
    {
      regu_set_error_with_zero_args (ER_REGU_NO_SPACE);
      return NULL;
    }
  else
    {
      for (i = 0; i < size; i++)
	{
	  regu_hfid_init (ptr + i);
	}
      return ptr;
    }
}

/*
 * regu_method_sig_init () -
 *   return:
 *   ptr(in)    : pointer to a method_sig
 *
 * Note: Initialization function for METHOD_SIG.
 */
void
regu_method_sig_init (METHOD_SIG * ptr)
{
  ptr->next = NULL;
  ptr->method_name = NULL;
  ptr->class_name = NULL;
  ptr->method_type = 0;
  ptr->no_method_args = 0;
  ptr->method_arg_pos = NULL;
}

/*
 * regu_method_sig_alloc () -
 *   return: METHOD_SIG *
 *
 * Note: Memory allocation function for METHOD_SIG.
 */
METHOD_SIG *
regu_method_sig_alloc (void)
{
  METHOD_SIG *ptr;

  ptr = (METHOD_SIG *) pt_alloc_packing_buf (sizeof (METHOD_SIG));
  if (ptr == NULL)
    {
      regu_set_error_with_zero_args (ER_REGU_NO_SPACE);
      return NULL;
    }
  else
    {
      regu_method_sig_init (ptr);
      return ptr;
    }
}

/*
 * regu_method_sig_db_alloc () -
 *   return: METHOD_SIG *
 *
 * Note: Memory allocation function for METHOD_SIG using malloc.
 */
METHOD_SIG *
regu_method_sig_db_alloc (void)
{
  METHOD_SIG *ptr;

  ptr = (METHOD_SIG *) malloc (sizeof (METHOD_SIG));
  if (ptr == NULL)
    {
      regu_set_error_with_zero_args (ER_REGU_NO_SPACE);
      return NULL;
    }
  else
    {
      regu_method_sig_init (ptr);
      return ptr;
    }
}

/*
 * regu_free_method_sig () -
 *   return:
 *   method_sig(in)     : pointer to a method_sig
 *
 * Note: Free function for METHOD_SIG using free_and_init.
 */
void
regu_free_method_sig (METHOD_SIG * method_sig)
{
  if (method_sig != NULL)
    {
      regu_free_method_sig (method_sig->next);
      db_private_free_and_init (NULL, method_sig->method_name);
      db_private_free_and_init (NULL, method_sig->class_name);
      db_private_free_and_init (NULL, method_sig->method_arg_pos);
      db_private_free_and_init (NULL, method_sig);
    }
}

/*
 * regu_method_sig_list_init () -
 *   return:
 *   ptr(in)    : pointer to a method_sig_list
 *
 * Note: Initialization function for METHOD_SIG_LIST.
 */
void
regu_method_sig_list_init (METHOD_SIG_LIST * ptr)
{
  ptr->no_methods = 0;
  ptr->method_sig = (METHOD_SIG *) 0;
}

/*
 * regu_method_sig_list_alloc () -
 *   return: METHOD_SIG_LIST *
 *
 * Note: Memory allocation function for METHOD_SIG_LIST
 */
METHOD_SIG_LIST *
regu_method_sig_list_alloc (void)
{
  METHOD_SIG_LIST *ptr;

  ptr = (METHOD_SIG_LIST *) pt_alloc_packing_buf (sizeof (METHOD_SIG_LIST));
  if (ptr == NULL)
    {
      regu_set_error_with_zero_args (ER_REGU_NO_SPACE);
      return NULL;
    }
  else
    {
      regu_method_sig_list_init (ptr);
      return ptr;
    }
}

/*
 * regu_method_sig_list_db_alloc () -
 *   return: METHOD_SIG_LIST *
 *
 * Note: Memory allocation function for METHOD_SIG_LIST using malloc.
 */
METHOD_SIG_LIST *
regu_method_sig_list_db_alloc (void)
{
  METHOD_SIG_LIST *ptr;

  ptr = (METHOD_SIG_LIST *) malloc (sizeof (METHOD_SIG_LIST));
  if (ptr == NULL)
    {
      regu_set_error_with_zero_args (ER_REGU_NO_SPACE);
      return NULL;
    }
  else
    {
      regu_method_sig_list_init (ptr);
      return ptr;
    }
}


/*
 * regu_free_method_sig_list () -
 *   return:
 *   method_sig_list(in)        : pointer to a method_sig_list
 *
 * Note: Free function for METHOD_SIG_LIST using free_and_init.
 */
void
regu_free_method_sig_list (METHOD_SIG_LIST * method_sig_list)
{
  if (method_sig_list != NULL)
    {
      regu_free_method_sig (method_sig_list->method_sig);
      db_private_free_and_init (NULL, method_sig_list);
    }
}

/*
 * regu_partition_array_alloc () -
 *   return:
 *   nelements(in)   :
 */
XASL_PARTITION_INFO **
regu_partition_array_alloc (int nelements)
{
  XASL_PARTITION_INFO **ptr;
  size_t size;

  if (nelements == 0)
    return NULL;

  size = sizeof (XASL_PARTITION_INFO *) * nelements;
  ptr = (XASL_PARTITION_INFO **) pt_alloc_packing_buf (size);

  if (ptr == NULL)
    {
      regu_set_error_with_zero_args (ER_REGU_NO_SPACE);
      return NULL;
    }
  else
    {
      return ptr;
    }
}

/*
 * regu_parts_array_alloc () -
 *   return:
 *   nelements(in)   :
 */
XASL_PARTS_INFO **
regu_parts_array_alloc (int nelements)
{
  XASL_PARTS_INFO **ptr;
  size_t size;

  if (nelements == 0)
    return NULL;

  size = sizeof (XASL_PARTS_INFO *) * nelements;
  ptr = (XASL_PARTS_INFO **) pt_alloc_packing_buf (size);
  if (ptr == NULL)
    {
      regu_set_error_with_zero_args (ER_REGU_NO_SPACE);
      return NULL;
    }
  else
    {
      return ptr;
    }
}

/*
 * regu_partition_info_alloc () -
 *   return:
 */
XASL_PARTITION_INFO *
regu_partition_info_alloc (void)
{
  XASL_PARTITION_INFO *ptr;
  size_t size;

  size = sizeof (XASL_PARTITION_INFO);
  ptr = (XASL_PARTITION_INFO *) pt_alloc_packing_buf (size);
  if (ptr == NULL)
    {
      regu_set_error_with_zero_args (ER_REGU_NO_SPACE);
      return NULL;
    }
  else
    {
      regu_partition_info_init (ptr);
      return ptr;
    }
}

/*
 * regu_parts_info_alloc () -
 *   return:
 */
XASL_PARTS_INFO *
regu_parts_info_alloc (void)
{
  XASL_PARTS_INFO *ptr;

  ptr = (XASL_PARTS_INFO *) pt_alloc_packing_buf (sizeof (XASL_PARTS_INFO));
  if (ptr == NULL)
    {
      regu_set_error_with_zero_args (ER_REGU_NO_SPACE);
      return NULL;
    }
  else
    {
      regu_parts_info_init (ptr);
      return ptr;
    }
}

/*
 * regu_partition_info_init () -
 *   return:
 *   ptr(in)    :
 */
static void
regu_partition_info_init (XASL_PARTITION_INFO * ptr)
{
  ptr->key_attr = 0;
  ptr->type = 0;
  ptr->no_parts = 0;
  ptr->act_parts = 0;
  ptr->expr = NULL;
  ptr->parts = NULL;
}

/*
 * regu_parts_info_init () -
 *   return:
 *   ptr(in)    :
 */
static void
regu_parts_info_init (XASL_PARTS_INFO * ptr)
{
  ptr->class_hfid.vfid.fileid = NULL_FILEID;
  ptr->class_hfid.vfid.volid = NULL_VOLID;
  ptr->class_hfid.hpgid = NULL_PAGEID;
  regu_init_oid (&ptr->class_oid);
  ptr->vals = NULL;
}

/*
 * regu_selupd_list_alloc () -
 *   return:
 */
SELUPD_LIST *
regu_selupd_list_alloc (void)
{
  SELUPD_LIST *ptr;

  ptr = (SELUPD_LIST *) pt_alloc_packing_buf (sizeof (SELUPD_LIST));
  if (ptr == NULL)
    {
      regu_set_error_with_zero_args (ER_REGU_NO_SPACE);
      return NULL;
    }
  else
    {
      regu_selupd_list_init (ptr);
      return ptr;
    }
}

/*
 * regu_selupd_list_init () -
 *   return:
 *   ptr(in)    :
 */
static void
regu_selupd_list_init (SELUPD_LIST * ptr)
{
  ptr->next = NULL;
  regu_init_oid (&ptr->class_oid);
  ptr->class_hfid.vfid.fileid = NULL_FILEID;
  ptr->class_hfid.vfid.volid = NULL_VOLID;
  ptr->class_hfid.hpgid = NULL_PAGEID;
  ptr->select_list_size = 0;
  ptr->select_list = NULL;
}
