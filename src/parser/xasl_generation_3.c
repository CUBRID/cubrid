/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * xasl_generation.c - Generate XASL from the parse tree
 * TODO: include xasl_generation.c, xasl_generation_1.c, xasl_generation_2.c
 * TODO: rename this file to xasl_generation.c
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include <assert.h>
#include <search.h>

#include "porting.h"
#include "xasl_generation_2.h"
#include "db.h"
#include "error_manager.h"
#include "environment_variable.h"
#include "system_parameter.h"
#include "parser.h"
#include "msgexec.h"
#include "list_file.h"
#include "qp_mem.h"
#include "qo.h"
#include "schema_manager_3.h"
#include "virtual_object_1.h"
#include "set_object_1.h"
#include "object_print_1.h"
#include "object_representation.h"
#include "execute_schema_8.h"
#include "locator_cl.h"
#include "heap_file.h"
#include "view_transform_1.h"
#include "execute_statement_10.h"

/* this must be the last header file included!!! */
#include "dbval.h"

/*
 * Regular Variable
 */

typedef struct set_numbering_node_etc_info
{
  DB_VALUE **instnum_valp;
  DB_VALUE **ordbynum_valp;
} SET_NUMBERING_NODE_ETC_INFO;

static int regu_make_constant_vid (DB_VALUE * val, DB_VALUE ** dbvalptr);
static int set_has_objs (DB_SET * seq);
static int setof_mop_to_setof_vobj (PARSER_CONTEXT * parser, DB_SET * seq,
				    DB_VALUE * new_val);
static REGU_VARIABLE *pt_make_regu_hostvar (PARSER_CONTEXT * parser,
					    const PT_NODE * node);
static REGU_VARIABLE *pt_make_regu_constant (PARSER_CONTEXT * parser,
					     DB_VALUE * db_value,
					     const DB_TYPE db_type,
					     const PT_NODE * node);
static REGU_VARIABLE *pt_make_regu_arith (const REGU_VARIABLE * arg1,
					  const REGU_VARIABLE * arg2,
					  const REGU_VARIABLE * arg3,
					  const OPERATOR_TYPE op,
					  const TP_DOMAIN * domain);
static REGU_VARIABLE *pt_make_function (PARSER_CONTEXT * parser,
					int function_code,
					const REGU_VARIABLE_LIST arg_list,
					const DB_TYPE result_type,
					const PT_NODE * node);
static REGU_VARIABLE *pt_function_to_regu (PARSER_CONTEXT * parser,
					   PT_NODE * function);
static REGU_VARIABLE *pt_make_regu_subquery (PARSER_CONTEXT * parser,
					     XASL_NODE * xasl,
					     const UNBOX unbox,
					     const PT_NODE * node);
static PT_NODE *pt_set_numbering_node_etc_pre (PARSER_CONTEXT * parser,
					       PT_NODE * node, void *arg,
					       int *continue_walk);
static REGU_VARIABLE *pt_make_regu_numbering (PARSER_CONTEXT * parser,
					      const PT_NODE * node);
static void pt_to_misc_operand (REGU_VARIABLE * regu,
				PT_MISC_TYPE misc_specifier);
static REGU_VARIABLE *pt_make_position_regu_variable (PARSER_CONTEXT * parser,
						      const PT_NODE * node,
						      int i);
static REGU_VARIABLE *pt_to_regu_attr_descr (PARSER_CONTEXT * parser,
					     DB_OBJECT * class_object,
					     HEAP_CACHE_ATTRINFO *
					     cache_attrinfo, PT_NODE * attr);
static REGU_VARIABLE *pt_make_vid (PARSER_CONTEXT * parser,
				   const PT_NODE * data_type,
				   const REGU_VARIABLE * regu3);
static PT_NODE *pt_make_empty_string (PARSER_CONTEXT * parser,
				      const PT_NODE * node);


#define APPEND_TO_XASL(xasl_head, list, xasl_tail)                      \
    if (xasl_head) {                                                    \
        /* append xasl_tail to end of linked list denoted by list */    \
        XASL_NODE **NAME2(list,ptr) = &xasl_head->list;                 \
        while ( (*NAME2(list,ptr)) ) {                                  \
            NAME2(list,ptr) = &(*NAME2(list,ptr))->list;                \
        }                                                               \
        (*NAME2(list,ptr)) = xasl_tail;                                 \
    } else {                                                            \
        xasl_head = xasl_tail;                                          \
    }

#define VALIDATE_REGU_KEY(r) ((r)->type == TYPE_CONSTANT  || \
                              (r)->type == TYPE_DBVAL     || \
                              (r)->type == TYPE_POS_VALUE || \
                              (r)->type == TYPE_INARITH)

typedef struct xasl_supp_info
{
  PT_NODE *query_list;		/* ??? */

  /* XASL cache related information */
  OID *class_oid_list;		/* list of class OIDs referenced in the XASL */
  int *repr_id_list;		/* representation ids of the classes
				   in the class OID list */
  int n_oid_list;		/* number OIDs in the list */
  int oid_list_size;		/* size of the list */
} XASL_SUPP_INFO;

typedef struct uncorr_info
{
  XASL_NODE *xasl;
  int level;
} UNCORR_INFO;

typedef struct corr_info
{
  XASL_NODE *xasl_head;
  UINTPTR id;
} CORR_INFO;

FILE *query_plan_dump_fp = NULL;
char *query_plan_dump_filename = NULL;

static XASL_SUPP_INFO xasl_supp_info = { NULL, NULL, NULL, 0, 0 };
static const int OID_LIST_GROWTH = 10;


static RANGE op_type_to_range (const PT_OP_TYPE op_type, const int nterms);
static int pt_to_single_key (PARSER_CONTEXT * parser, PT_NODE ** term_exprs,
			     int nterms, bool multi_col,
			     KEY_INFO * key_infop);
static int pt_to_range_key (PARSER_CONTEXT * parser, PT_NODE ** term_exprs,
			    int nterms, bool multi_col, KEY_INFO * key_infop);
static int pt_to_list_key (PARSER_CONTEXT * parser, PT_NODE ** term_exprs,
			   int nterms, bool multi_col, KEY_INFO * key_infop);
static int pt_to_rangelist_key (PARSER_CONTEXT * parser,
				PT_NODE ** term_exprs, int nterms,
				bool multi_col, KEY_INFO * key_infop);
static INDX_INFO *pt_to_index_info (PARSER_CONTEXT * parser,
				    DB_OBJECT * class_,
				    QO_XASL_INDEX_INFO * qo_index_infop);
static ACCESS_SPEC_TYPE *pt_to_class_spec_list (PARSER_CONTEXT * parser,
						PT_NODE * spec,
						PT_NODE * where_key_part,
						PT_NODE * where_part,
						QO_XASL_INDEX_INFO *
						index_pred);
static ACCESS_SPEC_TYPE *pt_to_subquery_table_spec_list (PARSER_CONTEXT *
							 parser,
							 PT_NODE * spec,
							 PT_NODE * subquery,
							 PT_NODE *
							 where_part);
static ACCESS_SPEC_TYPE *pt_to_set_expr_table_spec_list (PARSER_CONTEXT *
							 parser,
							 PT_NODE * spec,
							 PT_NODE * set_expr,
							 PT_NODE *
							 where_part);
static ACCESS_SPEC_TYPE *pt_to_cselect_table_spec_list (PARSER_CONTEXT *
							parser,
							PT_NODE * spec,
							PT_NODE * cselect,
							PT_NODE *
							src_derived_tbl);
static XASL_NODE *pt_find_xasl (XASL_NODE * list, XASL_NODE * match);
static void pt_set_aptr (PARSER_CONTEXT * parser, PT_NODE * select_node,
			 XASL_NODE * xasl);
static XASL_NODE *pt_append_scan (const XASL_NODE * to,
				  const XASL_NODE * from);
static PT_NODE *pt_uncorr_pre (PARSER_CONTEXT * parser, PT_NODE * node,
			       void *arg, int *continue_walk);
static PT_NODE *pt_uncorr_post (PARSER_CONTEXT * parser, PT_NODE * node,
				void *arg, int *continue_walk);
static XASL_NODE *pt_to_uncorr_subquery_list (PARSER_CONTEXT * parser,
					      PT_NODE * node);
static PT_NODE *pt_corr_pre (PARSER_CONTEXT * parser, PT_NODE * node,
			     void *arg, int *continue_walk);
static XASL_NODE *pt_to_corr_subquery_list (PARSER_CONTEXT * parser,
					    PT_NODE * node, UINTPTR id);
static SELUPD_LIST *pt_link_regu_to_selupd_list (PARSER_CONTEXT * parser,
						 REGU_VARIABLE_LIST regulist,
						 SELUPD_LIST * selupd_list,
						 DB_OBJECT * target_class);
static OUTPTR_LIST *pt_to_outlist (PARSER_CONTEXT * parser,
				   PT_NODE * node_list,
				   SELUPD_LIST ** selupd_list_ptr,
				   UNBOX unbox);
static void pt_to_fetch_proc_list_recurse (PARSER_CONTEXT * parser,
					   PT_NODE * spec, XASL_NODE * root);
static void pt_to_fetch_proc_list (PARSER_CONTEXT * parser, PT_NODE * spec,
				   XASL_NODE * root);
static XASL_NODE *pt_to_scan_proc_list (PARSER_CONTEXT * parser,
					PT_NODE * node, XASL_NODE * root);
static XASL_NODE *pt_gen_optimized_plan (PARSER_CONTEXT * parser,
					 XASL_NODE * xasl,
					 PT_NODE * select_node,
					 QO_PLAN * plan);
static XASL_NODE *pt_gen_simple_plan (PARSER_CONTEXT * parser,
				      XASL_NODE * xasl,
				      PT_NODE * select_node);
static XASL_NODE *pt_to_buildlist_proc (PARSER_CONTEXT * parser,
					PT_NODE * select_node,
					QO_PLAN * qo_plan);
static XASL_NODE *pt_to_buildvalue_proc (PARSER_CONTEXT * parser,
					 PT_NODE * select_node,
					 QO_PLAN * qo_plan);
static XASL_NODE *pt_to_union_proc (PARSER_CONTEXT * parser, PT_NODE * node,
				    PROC_TYPE type);
static XASL_NODE *pt_plan_set_query (PARSER_CONTEXT * parser, PT_NODE * node,
				     PROC_TYPE proc_type);
static XASL_NODE *pt_plan_query (PARSER_CONTEXT * parser,
				 PT_NODE * select_node);
static XASL_NODE *pt_to_xasl_proc (PARSER_CONTEXT * parser, PT_NODE * node,
				   PT_NODE * query_list);
static PT_NODE *pt_to_xasl_pre (PARSER_CONTEXT * parser, PT_NODE * node,
				void *arg, int *continue_walk);
static int pt_spec_to_xasl_class_oid_list (const PT_NODE * spec,
					   OID ** oid_listp, int **rep_listp,
					   int *nump, int *sizep);
static PT_NODE *pt_to_xasl_post (PARSER_CONTEXT * parser, PT_NODE * node,
				 void *arg, int *continue_walk);
static XASL_NODE *pt_make_aptr_parent_node (PARSER_CONTEXT * parser,
					    PT_NODE * node, PROC_TYPE type);
static int pt_to_constraint_pred (PARSER_CONTEXT * parser, XASL_NODE * xasl,
				  PT_NODE * spec, PT_NODE * non_null_attrs,
				  PT_NODE * attr_list, int attr_offset);
static XASL_NODE *pt_to_fetch_as_scan_proc (PARSER_CONTEXT * parser,
					    PT_NODE * spec,
					    PT_NODE * join_term,
					    XASL_NODE * xasl_to_scan);


/*
 * regu_make_constant_vid () - convert a vmop into a db_value
 *   return: NO_ERROR on success, non-zero for ERROR
 *   val(in): a virtual object instance
 *   dbvalptr(out): pointer to a db_value
 */
static int
regu_make_constant_vid (DB_VALUE * val, DB_VALUE ** dbvalptr)
{
  DB_OBJECT *vmop, *cls, *proxy, *real_instance;
  DB_VALUE *keys = NULL, *virt_val, *proxy_val;
  OID virt_oid, proxy_oid;
  DB_IDENTIFIER *dbid;
  DB_SEQ *seq;

  assert (val != NULL);

  /* make sure we got a virtual MOP and a db_value */
  if (DB_VALUE_TYPE (val) != DB_TYPE_OBJECT
      || !(vmop = DB_GET_OBJECT (val)) || !WS_ISVID (vmop))
    {
      return ER_GENERIC_ERROR;
    }

  if (((*dbvalptr = regu_dbval_alloc ()) == NULL) ||
      ((virt_val = regu_dbval_alloc ()) == NULL) ||
      ((proxy_val = regu_dbval_alloc ()) == NULL) ||
      ((keys = regu_dbval_alloc ()) == NULL))
    {
      return ER_GENERIC_ERROR;
    }

  /* compute vmop's three canonical values: virt, proxy, keys */
  cls = db_get_class (vmop);
  if (!db_is_vclass (cls))
    {
      OID_SET_NULL (&virt_oid);
      real_instance = vmop;
      OID_SET_NULL (&proxy_oid);
      *keys = *val;
    }
  else
    {
      /* make sure its oid is a good one */
      dbid = db_identifier (cls);
      if (!dbid)
	{
	  return ER_GENERIC_ERROR;
	}

      virt_oid = *dbid;
      real_instance = db_real_instance (vmop);
      if (!real_instance)
	{
	  OID_SET_NULL (&proxy_oid);
	  vid_get_keys (vmop, keys);
	}
      else
	{
	  proxy = db_get_class (real_instance);
	  OID_SET_NULL (&proxy_oid);
	  vid_get_keys (vmop, keys);
	}
    }

  DB_MAKE_OID (virt_val, &virt_oid);
  DB_MAKE_OID (proxy_val, &proxy_oid);

  /* the DB_VALUE form of a VMOP is given a type of DB_TYPE_VOBJ
   * and takes the form of a 3-element sequence: virt, proxy, keys
   * (Oh what joy to find out the secret encoding of a virtual object!)
   */
  if ((seq = db_seq_create (NULL, NULL, 3)) == NULL)
    {
      goto error_cleanup;
    }

  if (db_seq_put (seq, 0, virt_val) != NO_ERROR)
    {
      goto error_cleanup;
    }

  if (db_seq_put (seq, 1, proxy_val) != NO_ERROR)
    {
      goto error_cleanup;
    }

  /* this may be a nested sequence, so turn on nested sets */
  if (db_seq_put (seq, 2, keys) != NO_ERROR)
    {
      goto error_cleanup;
    }

  db_make_sequence (*dbvalptr, seq);
  db_value_alter_type (*dbvalptr, DB_TYPE_VOBJ);

  return NO_ERROR;

error_cleanup:
  pr_clear_value (keys);
  return ER_GENERIC_ERROR;
}

/*
 * set_has_objs () - set dbvalptr to the DB_VALUE form of val
 *   return: nonzero if set has some objs, zero otherwise
 *   seq(in): a set/seq db_value
 */
static int
set_has_objs (DB_SET * seq)
{
  int found = 0, i, siz;
  DB_VALUE elem;

  siz = db_seq_size (seq);
  for (i = 0; i < siz && !found; i++)
    {
      if (db_set_get (seq, i, &elem) < 0)
	{
	  return 0;
	}

      if (DB_VALUE_DOMAIN_TYPE (&elem) == DB_TYPE_OBJECT)
	{
	  found = 1;
	}

      db_value_clear (&elem);
    }

  return found;
}


/*
 * setof_mop_to_setof_vobj () - creates & fill new set/seq with converted
 *                              vobj elements of val
 *   return: NO_ERROR on success, non-zero for ERROR
 *   parser(in):
 *   seq(in): a set/seq of mop-bearing elements
 *   new_val(out):
 */
static int
setof_mop_to_setof_vobj (PARSER_CONTEXT * parser, DB_SET * seq,
			 DB_VALUE * new_val)
{
  size_t i, siz;
  DB_VALUE elem, *new_elem;
  DB_SET *new_set;
  DB_OBJECT *obj;
  OID *oid;
  DB_TYPE typ;

  /* make sure we got a set/seq */
  typ = db_set_type (seq);
  if (!pr_is_set_type (typ))
    {
      goto failure;
    }

  /* create a new set/seq */
  siz = db_seq_size (seq);
  if (typ == DB_TYPE_SET)
    {
      new_set = db_set_create_basic (NULL, NULL);
    }
  else if (typ == DB_TYPE_MULTISET)
    {
      new_set = db_set_create_multi (NULL, NULL);
    }
  else
    {
      new_set = db_seq_create (NULL, NULL, siz);
    }

  /* fill the new_set with the vobj form of val's mops */
  for (i = 0; i < siz; i++)
    {
      if (db_set_get (seq, i, &elem) < 0)
	{
	  goto failure;
	}

      if (DB_IS_NULL (&elem))
	{
	  new_elem = regu_dbval_alloc ();
	  if (!new_elem)
	    {
	      goto failure;
	    }
	  db_value_domain_init (new_elem, DB_TYPE_OBJECT,
				DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE);
	}
      else if (DB_VALUE_DOMAIN_TYPE (&elem) != DB_TYPE_OBJECT
	       || (obj = DB_GET_OBJECT (&elem)) == NULL)
	{
	  /* the set has mixed object and non-object types. */
	  new_elem = &elem;
	}
      else
	{
	  /* convert val's mop into a vobj */
	  if (WS_ISVID (obj))
	    {
	      if (regu_make_constant_vid (&elem, &new_elem) != NO_ERROR)
		{
		  goto failure;
		}

	      /* we need to register the constant vid as an orphaned
	       * db_value that the parser should free later.  We can't
	       * free it until after the xasl has been packed.
	       */
	      pt_register_orphan_db_value (parser, new_elem);
	    }
	  else
	    {
	      new_elem = regu_dbval_alloc ();
	      if (!new_elem)
		{
		  goto failure;
		}

	      if (WS_MARKED_DELETED (obj))
		{
		  db_value_domain_init (new_elem, DB_TYPE_OBJECT,
					DB_DEFAULT_PRECISION,
					DB_DEFAULT_SCALE);
		}
	      else
		{
		  oid = db_identifier (obj);
		  if (oid == NULL)
		    {
		      goto failure;
		    }

		  db_make_object (new_elem, ws_mop (oid, NULL));
		}
	    }
	}

      /* stuff the vobj form of the mop into new_set */
      if (typ == DB_TYPE_SET || typ == DB_TYPE_MULTISET)
	{
	  if (db_set_add (new_set, new_elem) < 0)
	    {
	      goto failure;
	    }
	}
      else if (db_seq_put (new_set, i, new_elem) < 0)
	{
	  goto failure;
	}

      db_value_clear (&elem);
    }

  /* stuff new_set into new_val */
  if (typ == DB_TYPE_SET)
    {
      db_make_set (new_val, new_set);
    }
  else if (typ == DB_TYPE_MULTISET)
    {
      db_make_multiset (new_val, new_set);
    }
  else
    {
      db_make_sequence (new_val, new_set);
    }

  return NO_ERROR;

failure:
  PT_INTERNAL_ERROR (parser, "generate var");
  return ER_FAILED;
}


/*
 * pt_make_regu_hostvar () - takes a pt_node of host variable and make
 *                           a regu_variable of host variable reference
 *   return:
 *   parser(in/out):
 *   node(in):
 */
static REGU_VARIABLE *
pt_make_regu_hostvar (PARSER_CONTEXT * parser, const PT_NODE * node)
{
  REGU_VARIABLE *regu;
  DB_VALUE *val;
  DB_TYPE typ, exptyp;

  regu = regu_var_alloc ();
  if (regu)
    {
      val = &parser->host_variables[node->info.host_var.index];
      typ = DB_VALUE_DOMAIN_TYPE (val);

      regu->type = TYPE_POS_VALUE;
      regu->value.val_pos = node->info.host_var.index;
      if (parser->dbval_cnt < node->info.host_var.index)
	parser->dbval_cnt = node->info.host_var.index;

      /* determine the domain of this host var */
      regu->domain = NULL;

      if (node->data_type)
	{
	  /* try to get domain info from its data_type */
	  regu->domain = pt_xasl_node_to_domain (parser, node);
	}

      if (regu->domain == NULL &&
	  (parser->set_host_var == 1 || typ != DB_TYPE_NULL))
	{
	  /* if the host var was given before by the user,
	     use its domain for regu varaible */
	  regu->domain =
	    pt_xasl_type_enum_to_domain ((PT_TYPE_ENUM)
					 pt_db_to_type_enum (typ));
	}

      if (regu->domain == NULL && node->expected_domain)
	{
	  /* try to get domain infor from its expected_domain */
	  regu->domain = node->expected_domain;
	}

      if (regu->domain == NULL)
	{
	  /* try to get domain info from its type_enum */
	  regu->domain = pt_xasl_type_enum_to_domain (node->type_enum);
	}

      if (regu->domain == NULL)
	{
	  PT_INTERNAL_ERROR (parser, "unresolved data type of host var");
	  regu = NULL;
	}
      else
	{
	  exptyp = regu->domain->type->id;
	  if (parser->set_host_var == 0 && typ == DB_TYPE_NULL)
	    {
	      /* If the host variable was not given before by the user,
	         preset it by the expected domain.
	         When the user set the host variable,
	         its value will be casted to this domain if necessary. */
	      (void) db_value_domain_init (val, exptyp,
					   regu->domain->precision,
					   regu->domain->scale);
	    }
	  else if (typ != exptyp)
	    {
	      if (tp_value_cast (val, val,
				 regu->domain, false) != DOMAIN_COMPATIBLE)
		{
		  PT_INTERNAL_ERROR (parser, "cannot coerce host var");
		  regu = NULL;
		}
	    }
	}
    }
  else
    {
      regu = NULL;
    }

  return regu;
}


/*
 * pt_make_regu_constant () - takes a db_value and db_type and makes
 *                            a regu_variable constant
 *   return: A NULL return indicates an error occured
 *   parser(in):
 *   db_value(in/out):
 *   db_type(in):
 *   node(in):
 */
static REGU_VARIABLE *
pt_make_regu_constant (PARSER_CONTEXT * parser, DB_VALUE * db_value,
		       const DB_TYPE db_type, const PT_NODE * node)
{
  REGU_VARIABLE *regu = NULL;
  DB_VALUE *dbvalptr = NULL;
  DB_VALUE tmp_val;
  DB_TYPE typ;
  int is_null;
  DB_SET *set = NULL;

  db_make_null (&tmp_val);
  if (db_value)
    {
      regu = regu_var_alloc ();
      if (regu)
	{
	  if (node)
	    {
	      regu->domain = pt_xasl_node_to_domain (parser, node);
	    }
	  else
	    {
	      /* just use the type to create the domain,
	       * this is a special case */
	      regu->domain = tp_Domains[db_type];
	    }

	  regu->type = TYPE_CONSTANT;
	  typ = DB_VALUE_DOMAIN_TYPE (db_value);
	  is_null = DB_IS_NULL (db_value);
	  if (is_null)
	    {
	      regu->value.dbvalptr = db_value;
	    }
	  else if (typ == DB_TYPE_OBJECT)
	    {
	      if (WS_ISVID (DB_GET_OBJECT (db_value)))
		{
		  if (regu_make_constant_vid (db_value,
					      &dbvalptr) != NO_ERROR)
		    {
		      return NULL;
		    }
		  else
		    {
		      regu->value.dbvalptr = dbvalptr;
		      regu->domain = &tp_Vobj_domain;

		      /* we need to register the constant vid as an orphaned
		       * db_value that the parser should free later. We can't
		       * free it until after the xasl has been packed.
		       */
		      pt_register_orphan_db_value (parser, dbvalptr);
		    }
		}
	      else
		{
		  OID *oid;

		  oid = db_identifier (DB_GET_OBJECT (db_value));
		  if (oid == NULL)
		    {
		      db_value_put_null (db_value);
		      regu->value.dbvalptr = db_value;
		    }
		  else
		    {
		      db_make_object (db_value, ws_mop (oid, NULL));
		      regu->value.dbvalptr = db_value;
		    }
		}
	    }
	  else if (pr_is_set_type (typ) &&
		   (set = db_get_set (db_value)) != NULL &&
		   set_has_objs (set))
	    {
	      if (setof_mop_to_setof_vobj (parser, set, &tmp_val) != NO_ERROR)
		{
		  return NULL;
		}
	      regu->value.dbvalptr = &tmp_val;
	    }
	  else
	    {
	      regu->value.dbvalptr = db_value;
	    }

	  /* db_value may be in a pt_node that will be freed before mapping
	   * the xasl to a stream. This makes sure that we have captured
	   * the contents of the variable. It also uses the in-line db_value
	   * of a regu variable, saving xasl space.
	   */
	  db_value = regu->value.dbvalptr;
	  regu->type = TYPE_DBVAL;
	  db_value_clone (db_value, &regu->value.dbval);

	  /* we need to register the dbvalue within the regu constant
	   * as an orphan that the parser should free later. We can't
	   * free it until after the xasl has been packed.
	   */
	  pt_register_orphan_db_value (parser, &regu->value.dbval);

	  /* if setof_mop_to_setof_vobj() was called, then a new
	   * set was created.  The dbvalue needs to be cleared.
	   */
	  pr_clear_value (&tmp_val);
	}
    }

  return regu;
}


/*
 * pt_make_regu_arith () - takes a regu_variable pair,
 *                         and makes an regu arith type
 *   return: A NULL return indicates an error occured
 *   arg1(in):
 *   arg2(in):
 *   arg3(in):
 *   op(in):
 *   domain(in):
 */
static REGU_VARIABLE *
pt_make_regu_arith (const REGU_VARIABLE * arg1, const REGU_VARIABLE * arg2,
		    const REGU_VARIABLE * arg3, const OPERATOR_TYPE op,
		    const TP_DOMAIN * domain)
{
  REGU_VARIABLE *regu = NULL;
  ARITH_TYPE *arith;
  DB_VALUE *dbval;

  arith = regu_arith_alloc ();
  dbval = regu_dbval_alloc ();
  regu = regu_var_alloc ();

  if (arith && dbval && regu)
    {
      regu_dbval_type_init (dbval, domain->type->id);
      arith->domain = (TP_DOMAIN *) domain;
      arith->value = dbval;
      arith->opcode = op;
      arith->next = NULL;
      arith->leftptr = (REGU_VARIABLE *) arg1;
      arith->rightptr = (REGU_VARIABLE *) arg2;
      arith->thirdptr = (REGU_VARIABLE *) arg3;
      arith->pred = NULL;
      regu->type = TYPE_INARITH;
      regu->value.arithptr = arith;
    }

  return regu;
}


/*
 * pt_make_vid () - takes a pt_data_type and a regu variable and makes
 *                  a regu vid function
 *   return: A NULL return indicates an error occured
 *   parser(in):
 *   data_type(in):
 *   regu3(in):
 */
static REGU_VARIABLE *
pt_make_vid (PARSER_CONTEXT * parser, const PT_NODE * data_type,
	     const REGU_VARIABLE * regu3)
{
  REGU_VARIABLE *regu = NULL;
  REGU_VARIABLE *regu1 = NULL;
  REGU_VARIABLE *regu2 = NULL;
  DB_VALUE *value1, *value2;
  DB_OBJECT *virt;
  OID virt_oid, proxy_oid;
  DB_IDENTIFIER *dbid;

  if (!data_type || !regu3)
    {
      return NULL;
    }

  virt = data_type->info.data_type.virt_object;
  if (virt)
    {
      /* make sure its oid is a good one */
      dbid = db_identifier (virt);
      if (!dbid)
	{
	  return NULL;
	}
      virt_oid = *dbid;
    }
  else
    {
      OID_SET_NULL (&virt_oid);
    }

  OID_SET_NULL (&proxy_oid);

  value1 = regu_dbval_alloc ();
  value2 = regu_dbval_alloc ();
  if (!value1 || !value2)
    {
      return NULL;
    }

  DB_MAKE_OID (value1, &virt_oid);
  DB_MAKE_OID (value2, &proxy_oid);

  regu1 = pt_make_regu_constant (parser, value1, DB_TYPE_OID, NULL);
  regu2 = pt_make_regu_constant (parser, value2, DB_TYPE_OID, NULL);

  regu = regu_var_alloc ();

  if (!regu)
    {
      PT_ERROR (parser, data_type,
		msgcat_message (MSGCAT_CATALOG_CUBRID,
				MSGCAT_SET_PARSER_SEMANTIC,
				MSGCAT_SEMANTIC_OUT_OF_MEMORY));
      return NULL;
      return NULL;
    }

  regu->type = TYPE_FUNC;

  /* we just use the standard vanilla vobj domain */
  regu->domain = &tp_Vobj_domain;
  regu->value.funcp = regu_func_alloc ();
  regu->value.funcp->ftype = F_VID;
  regu->value.funcp->operand = regu_varlist_alloc ();
  if (!regu->value.funcp->operand)
    {
      PT_ERROR (parser, data_type,
		msgcat_message (MSGCAT_CATALOG_CUBRID,
				MSGCAT_SET_PARSER_SEMANTIC,
				MSGCAT_SEMANTIC_OUT_OF_MEMORY));
      return NULL;
    }

  regu->value.funcp->operand->value = *regu1;
  regu->value.funcp->operand->next = regu_varlist_alloc ();
  if (!regu->value.funcp->operand->next)
    {
      PT_ERROR (parser, data_type,
		msgcat_message (MSGCAT_CATALOG_CUBRID,
				MSGCAT_SET_PARSER_SEMANTIC,
				MSGCAT_SEMANTIC_OUT_OF_MEMORY));
      return NULL;
    }

  regu->value.funcp->operand->next->value = *regu2;
  regu->value.funcp->operand->next->next = regu_varlist_alloc ();
  if (!regu->value.funcp->operand->next->next)
    {
      PT_ERROR (parser, data_type,
		msgcat_message (MSGCAT_CATALOG_CUBRID,
				MSGCAT_SET_PARSER_SEMANTIC,
				MSGCAT_SEMANTIC_OUT_OF_MEMORY));
      return NULL;
    }

  regu->value.funcp->operand->next->next->value = *regu3;
  regu->value.funcp->operand->next->next->next = NULL;

  regu->hidden_column = regu3->hidden_column;

  regu_dbval_type_init (regu->value.funcp->value, DB_TYPE_VOBJ);

  return regu;
}


/*
 * pt_make_function () - takes a pt_data_type and a regu variable and makes
 *                       a regu function
 *   return: A NULL return indicates an error occured
 *   parser(in):
 *   function_code(in):
 *   arg_list(in):
 *   result_type(in):
 *   node(in):
 */
static REGU_VARIABLE *
pt_make_function (PARSER_CONTEXT * parser, int function_code,
		  const REGU_VARIABLE_LIST arg_list,
		  const DB_TYPE result_type, const PT_NODE * node)
{
  REGU_VARIABLE *regu;
  TP_DOMAIN *domain;

  regu = regu_var_alloc ();

  if (!regu)
    {
      return NULL;
    }

  domain = pt_xasl_node_to_domain (parser, node);
  regu->type = TYPE_FUNC;
  regu->domain = domain;
  regu->value.funcp = regu_func_alloc ();
  regu->value.funcp->operand = arg_list;
  regu->value.funcp->ftype = (FUNC_TYPE) function_code;
  regu->hidden_column = node->info.function.hidden_column;

  regu_dbval_type_init (regu->value.funcp->value, result_type);

  return regu;
}


/*
 * pt_function_to_regu () - takes a PT_FUNCTION and converts to a regu_variable
 *   return: A NULL return indicates an error occured
 *   parser(in):
 *   function(in/out):
 *
 * Note :
 * currently only aggregate functions are known and handled
 */
static REGU_VARIABLE *
pt_function_to_regu (PARSER_CONTEXT * parser, PT_NODE * function)
{
  REGU_VARIABLE *regu = NULL;
  DB_VALUE *dbval;
  bool is_aggregate;
  REGU_VARIABLE_LIST args;
  DB_TYPE result_type = DB_TYPE_SET;

  is_aggregate = pt_is_aggregate_function (parser, function);

  if (is_aggregate)
    {
      /* This procedure assumes that pt_to_aggregate has already
       * run, setting up the DB_VALUE for the aggregate value. */
      dbval = (DB_VALUE *) function->etc;
      if (dbval)
	{
	  regu = regu_var_alloc ();

	  if (regu)
	    {
	      regu->type = TYPE_CONSTANT;
	      regu->domain = pt_xasl_node_to_domain (parser, function);
	      regu->value.dbvalptr = dbval;
	    }
	  else
	    {
	      PT_ERROR (parser, function,
			msgcat_message (MSGCAT_CATALOG_CUBRID,
					MSGCAT_SET_PARSER_SEMANTIC,
					MSGCAT_SEMANTIC_OUT_OF_MEMORY));
	      return NULL;
	    }
	}
      else
	{
	  PT_ERRORm (parser, function, MSGCAT_SET_PARSER_RUNTIME,
		     MSGCAT_RUNTIME_NESTED_AGGREGATE);
	}
    }
  else
    {
      /* change the generic code to the server side generic code */
      if (function->info.function.function_type == PT_GENERIC)
	{
	  function->info.function.function_type = F_GENERIC;
	}

      if (function->info.function.function_type < F_TOP_TABLE_FUNC)
	{
	  args = pt_to_regu_variable_list (parser,
					   function->info.function.arg_list,
					   UNBOX_AS_TABLE, NULL, NULL);
	}
      else
	{
	  args = pt_to_regu_variable_list (parser,
					   function->info.function.arg_list,
					   UNBOX_AS_VALUE, NULL, NULL);
	}

      switch (function->info.function.function_type)
	{
	case F_SET:
	case F_TABLE_SET:
	  result_type = DB_TYPE_SET;
	  break;
	case F_SEQUENCE:
	case F_TABLE_SEQUENCE:
	  result_type = DB_TYPE_SEQUENCE;
	  break;
	case F_MULTISET:
	case F_TABLE_MULTISET:
	  result_type = DB_TYPE_MULTISET;
	  break;
	case F_MIDXKEY:
	  result_type = DB_TYPE_MIDXKEY;
	  break;
	case F_VID:
	  result_type = DB_TYPE_VOBJ;
	  break;
	case F_GENERIC:
	  result_type = pt_node_to_db_type (function);
	  break;
	case F_CLASS_OF:
	  result_type = DB_TYPE_OID;
	  break;
	default:
	  PT_ERRORf (parser, function,
		     "Internal error in generate(%d)", __LINE__);
	}

      if (args)
	{
	  regu = pt_make_function (parser,
				   function->info.function.function_type,
				   args, result_type, function);
	  if (DB_TYPE_VOBJ == pt_node_to_db_type (function)
	      && function->info.function.function_type != F_VID)
	    {
	      regu = pt_make_vid (parser, function->data_type, regu);
	    }
	}
    }

  return regu;
}


/*
 * pt_make_regu_subquery () - takes a db_value and db_type and makes
 *                            a regu_variable constant
 *   return: A NULL return indicates an error occured
 *   parser(in):
 *   xasl(in/out):
 *   unbox(in):
 *   db_type(in):
 *   node(in):
 */
static REGU_VARIABLE *
pt_make_regu_subquery (PARSER_CONTEXT * parser, XASL_NODE * xasl,
		       const UNBOX unbox, const PT_NODE * node)
{
  REGU_VARIABLE *regu = NULL;
  QFILE_SORTED_LIST_ID *srlist_id = NULL;
  int is_single_tuple;

  is_single_tuple = (unbox != UNBOX_AS_TABLE);

  if (xasl)
    {
      regu = regu_var_alloc ();
      regu->domain = pt_xasl_node_to_domain (parser, node);

      /* set as linked to regu var */
      XASL_SET_FLAG (xasl, XASL_LINK_TO_REGU_VARIABLE);
      REGU_VARIABLE_XASL (regu) = xasl;

      if ((xasl->is_single_tuple = is_single_tuple))
	{
	  if (!xasl->single_tuple)
	    {
	      xasl->single_tuple = pt_make_val_list ((PT_NODE *) node);
	    }

	  if (xasl->single_tuple)
	    {
	      regu->type = TYPE_CONSTANT;
	      regu->value.dbvalptr = xasl->single_tuple->valp->val;
	    }
	  else
	    {
	      PT_ERRORm (parser, node, MSGCAT_SET_PARSER_SEMANTIC,
			 MSGCAT_SEMANTIC_OUT_OF_MEMORY);
	      regu = NULL;
	    }
	}
      else
	{
	  srlist_id = regu_srlistid_alloc ();
	  if (srlist_id)
	    {
	      regu->type = TYPE_LIST_ID;
	      regu->value.srlist_id = srlist_id;
	      srlist_id->list_id = xasl->list_id;
	    }
	  else
	    {
	      regu = NULL;
	    }
	}
    }

  return regu;
}


/*
 * pt_set_numbering_node_etc_pre () -
 *   return:
 *   parser(in):
 *   node(in/out):
 *   arg(in/out):
 *   continue_walk(in):
 */
static PT_NODE *
pt_set_numbering_node_etc_pre (PARSER_CONTEXT * parser, PT_NODE * node,
			       void *arg, int *continue_walk)
{
  SET_NUMBERING_NODE_ETC_INFO *info = (SET_NUMBERING_NODE_ETC_INFO *) arg;

  if (node->node_type == PT_EXPR)
    {
      if (info->instnum_valp && (node->info.expr.op == PT_INST_NUM ||
				 node->info.expr.op == PT_ROWNUM))
	{
	  if (*info->instnum_valp == NULL)
	    {
	      *info->instnum_valp = regu_dbval_alloc ();
	    }

	  node->etc = *info->instnum_valp;
	}

      if (info->ordbynum_valp && node->info.expr.op == PT_ORDERBY_NUM)
	{
	  if (*info->ordbynum_valp == NULL)
	    {
	      *info->ordbynum_valp = regu_dbval_alloc ();
	    }

	  node->etc = *info->ordbynum_valp;
	}
    }

  return node;
}

/*
 * pt_set_numbering_node_etc () -
 *   return:
 *   parser(in):
 *   node_list(in):
 *   instnum_valp(out):
 *   ordbynum_valp(out):
 */
void
pt_set_numbering_node_etc (PARSER_CONTEXT * parser, PT_NODE * node_list,
			   DB_VALUE ** instnum_valp,
			   DB_VALUE ** ordbynum_valp)
{
  PT_NODE *node, *save_node, *save_next;
  SET_NUMBERING_NODE_ETC_INFO info;

  if (node_list)
    {
      info.instnum_valp = instnum_valp;
      info.ordbynum_valp = ordbynum_valp;

      for (node = node_list; node; node = node->next)
	{
	  save_node = node;

	  CAST_POINTER_TO_NODE (node);

	  /* save and cut-off node link */
	  save_next = node->next;
	  node->next = NULL;

	  (void) parser_walk_tree (parser, node,
				   pt_set_numbering_node_etc_pre, &info, NULL,
				   NULL);

	  if (node)
	    {
	      node->next = save_next;
	    }

	  node = save_node;
	}			/* for (node = ...) */
    }
}


/*
 * pt_make_regu_numbering () - make a regu_variable constant for
 *                             inst_num() and orderby_num()
 *   return:
 *   parser(in):
 *   node(in):
 */
static REGU_VARIABLE *
pt_make_regu_numbering (PARSER_CONTEXT * parser, const PT_NODE * node)
{
  REGU_VARIABLE *regu = NULL;
  DB_VALUE *dbval;

  /* 'etc' field of PT_NODEs which belong to inst_num() or orderby_num()
     expression was set to points to XASL_INSTNUM_VAL() or XASL_ORDBYNUM_VAL()
     by pt_regu.c:pt_set_numbering_node_etc() */
  dbval = (DB_VALUE *) node->etc;

  if (dbval)
    {
      regu = regu_var_alloc ();
      if (regu)
	{
	  regu->type = TYPE_CONSTANT;
	  regu->domain = pt_xasl_node_to_domain (parser, node);
	  regu->value.dbvalptr = dbval;
	}
    }
  else
    {
      PT_INTERNAL_ERROR (parser, "generate inst_num or orderby_num");
    }

  return regu;
}


/*
 * pt_to_misc_operand () - maps PT_MISC_TYPE of PT_LEADING, PT_TRAILING,
 *      PT_BOTH, PT_YEAR, PT_MONTH, PT_DAY, PT_HOUR, PT_MINUTE, and PT_SECOND
 *      to the corresponding MISC_OPERAND
 *   return:
 *   regu(in/out):
 *   misc_specifier(in):
 */
static void
pt_to_misc_operand (REGU_VARIABLE * regu, PT_MISC_TYPE misc_specifier)
{
  if (regu && regu->value.arithptr)
    {
      regu->value.arithptr->misc_operand =
	pt_misc_to_qp_misc_operand (misc_specifier);
    }
}

/*
 * pt_make_prim_data_type_fortonum () -
 *   return:
 *   parser(in):
 *   prec(in):
 *   scale(in):
 */
PT_NODE *
pt_make_prim_data_type_fortonum (PARSER_CONTEXT * parser, int prec, int scale)
{
  PT_NODE *dt = NULL;

  dt = parser_new_node (parser, PT_DATA_TYPE);
  if (dt == NULL)
    {
      return NULL;
    }

  if (prec > DB_MAX_NUMERIC_PRECISION ||
      scale > DB_MAX_NUMERIC_PRECISION || prec < 0 || scale < 0)
    {
      parser_free_tree (parser, dt);
      dt = NULL;
      return NULL;
    }

  dt->type_enum = PT_TYPE_NUMERIC;
  dt->info.data_type.precision = prec;
  dt->info.data_type.dec_precision = scale;

  return dt;
}

/*
 * pt_make_prim_data_type () -
 *   return:
 *   parser(in):
 *   e(in):
 */
PT_NODE *
pt_make_prim_data_type (PARSER_CONTEXT * parser, PT_TYPE_ENUM e)
{
  PT_NODE *dt = NULL;

  dt = parser_new_node (parser, PT_DATA_TYPE);

  dt->type_enum = e;
  switch (e)
    {
    case PT_TYPE_INTEGER:
    case PT_TYPE_SMALLINT:
    case PT_TYPE_DOUBLE:
    case PT_TYPE_DATE:
    case PT_TYPE_TIME:
    case PT_TYPE_TIMESTAMP:
    case PT_TYPE_MONETARY:
      dt->data_type = NULL;
      break;

    case PT_TYPE_CHAR:
      dt->info.data_type.precision = DB_MAX_CHAR_PRECISION;
      dt->info.data_type.units = INTL_CODESET_ISO88591;
      break;

    case PT_TYPE_NCHAR:
      dt->info.data_type.precision = DB_MAX_NCHAR_PRECISION;
      dt->info.data_type.units = (int) lang_charset ();
      break;

    case PT_TYPE_VARCHAR:
      dt->info.data_type.precision = DB_MAX_VARCHAR_PRECISION;
      dt->info.data_type.units = INTL_CODESET_ISO88591;
      break;

    case PT_TYPE_VARNCHAR:
      dt->info.data_type.precision = DB_MAX_VARNCHAR_PRECISION;
      dt->info.data_type.units = (int) lang_charset ();
      break;

    case PT_TYPE_NUMERIC:
      dt->info.data_type.precision = DB_MAX_NUMERIC_PRECISION;
      dt->info.data_type.dec_precision = DB_DEFAULT_NUMERIC_SCALE;
      break;

    default:
      /* error handling is required.. */
      parser_free_tree (parser, dt);
      dt = NULL;
    }

  return dt;
}

/*
 * pt_to_regu_resolve_domain () -
 *   return:
 *   p_precision(out):
 *   p_scale(out):
 *   node(in):
 */
void
pt_to_regu_resolve_domain (int *p_precision, int *p_scale,
			   const PT_NODE * node)
{
  char *format_buf;
  char *fbuf_end_ptr;
  int format_sz;
  int precision, scale, maybe_sci_notation = 0;

  if (node == NULL)
    {
      *p_precision = DB_MAX_NUMERIC_PRECISION;
      *p_scale = DB_DEFAULT_NUMERIC_SCALE;
    }
  else
    {
      switch (node->info.value.db_value.data.ch.info.style)
	{
	case SMALL_STRING:
	  format_sz = node->info.value.db_value.data.ch.sm.size;
	  format_buf = (char *) node->info.value.db_value.data.ch.sm.buf;
	  break;

	case MEDIUM_STRING:
	  format_sz = node->info.value.db_value.data.ch.medium.size;
	  format_buf = node->info.value.db_value.data.ch.medium.buf;
	  break;

	default:
	  format_sz = 0;
	  format_buf = NULL;
	}

      fbuf_end_ptr = format_buf + format_sz - 1;

      precision = scale = 0;

      /* analyze format string */
      if (format_sz > 0)
	{
	  /* skip white space or CR prefix */
	  while (format_buf < fbuf_end_ptr &&
		 (*format_buf == ' ' || *format_buf == '\t' ||
		  *format_buf == '\n'))
	    {
	      format_buf++;
	    }

	  precision = 0;
	  scale = 0;

	  while (*format_buf != '.' && format_buf <= fbuf_end_ptr)
	    {
	      switch (*format_buf)
		{
		case '9':
		case '0':
		  precision++;
		  break;
		case '+':
		case '-':
		case ',':
		case ' ':
		case '\t':
		case '\n':
		  break;

		case 'c':
		case 'C':
		case 's':
		case 'S':
		  if (precision == 0)
		    {
		      break;
		    }

		default:
		  maybe_sci_notation = 1;
		}
	      format_buf++;
	    }

	  if (*format_buf == '.')
	    {
	      format_buf++;
	      while (format_buf <= fbuf_end_ptr)
		{
		  switch (*format_buf)
		    {
		    case '9':
		    case '0':
		      scale++;
		    case '+':
		    case '-':
		    case ',':
		    case ' ':
		    case '\t':
		    case '\n':
		      break;

		    default:
		      maybe_sci_notation = 1;
		    }
		  format_buf++;
		}
	    }

	  precision += scale;
	}

      if (!maybe_sci_notation &&
	  (precision + scale) < DB_MAX_NUMERIC_PRECISION)
	{
	  *p_precision = precision;
	  *p_scale = scale;
	}
      else
	{
	  *p_precision = DB_MAX_NUMERIC_PRECISION;
	  *p_scale = DB_DEFAULT_NUMERIC_PRECISION;
	}
    }
}

/*
 * pt_make_empty_string() -
 *   return:
 *   parser(in):
 *   node(in):
 */
static PT_NODE *
pt_make_empty_string (PARSER_CONTEXT * parser, const PT_NODE * node)
{
  PT_TYPE_ENUM arg1_type;
  PT_NODE *empty_str;

  empty_str = parser_new_node (parser, PT_VALUE);
  arg1_type = node->info.expr.arg1->type_enum;
  empty_str->type_enum = (arg1_type == PT_TYPE_MAYBE)
    ? PT_TYPE_VARCHAR : arg1_type;
  switch (empty_str->type_enum)
    {
    case PT_TYPE_NCHAR:
    case PT_TYPE_VARNCHAR:
      empty_str->info.value.string_type = 'N';
      break;
    default:
      empty_str->info.value.string_type = ' ';
    }
  empty_str->info.value.data_value.str = pt_append_nulstring (parser,
							      NULL, "");
  empty_str->info.value.text =
    (char *) empty_str->info.value.data_value.str->bytes;

  return empty_str;
}

/*
 * pt_to_regu_variable () - converts a parse expression tree to regu_variables
 *   return:
 *   parser(in):
 *   node(in): should be something that will evaluate to an expression
 *             of names and constant
 *   unbox(in):
 */
REGU_VARIABLE *
pt_to_regu_variable (PARSER_CONTEXT * parser, PT_NODE * node, UNBOX unbox)
{
  REGU_VARIABLE *regu = NULL;
  XASL_NODE *xasl;
  DB_VALUE *value, *val = NULL;
  TP_DOMAIN *domain;
  PT_NODE *data_type = NULL;
  PT_NODE *param_empty = NULL;
  PT_NODE *save_node, *save_next;

  if (!node)
    {
      val = regu_dbval_alloc ();
      if (db_value_domain_init (val, DB_TYPE_VARCHAR,
				DB_DEFAULT_PRECISION,
				DB_DEFAULT_SCALE) == NO_ERROR)
	{
	  regu = pt_make_regu_constant (parser, val, DB_TYPE_VARCHAR, NULL);
	}
    }
  else
    {
      save_node = node;

      CAST_POINTER_TO_NODE (node);

      /* save and cut-off node link */
      save_next = node->next;
      node->next = NULL;

      switch (node->node_type)
	{
	case PT_DOT_:
	  /* a path expression. XASL fetch procs or equivalent should
	   * already be done for it
	   * return the regu variable for the right most name in the
	   * path expression.
	   */
	  switch (node->info.dot.arg2->info.name.meta_class)
	    {
	    case PT_PARAMETER:
	      val = regu_dbval_alloc ();
	      pt_evaluate_tree (parser, node, val);
	      if (!parser->error_msgs)
		{
		  regu = pt_make_regu_constant (parser, val,
						pt_node_to_db_type (node),
						node);
		}
	      break;
	    case PT_META_ATTR:
	    case PT_NORMAL:
	    case PT_SHARED:
	    default:
	      regu = pt_attribute_to_regu (parser, node->info.dot.arg2);
	      break;
	    }
	  break;

	case PT_METHOD_CALL:
	  /* a method call that can be evaluated as a constant expression. */
	  val = regu_dbval_alloc ();
	  pt_evaluate_tree (parser, node, val);
	  if (!parser->error_msgs)
	    {
	      regu = pt_make_regu_constant (parser, val,
					    pt_node_to_db_type (node), node);
	    }
	  break;

	case PT_EXPR:
	  {
	    REGU_VARIABLE *r1, *r2, *r3;

	    domain = NULL;
	    if (node->info.expr.op == PT_PLUS ||
		node->info.expr.op == PT_MINUS ||
		node->info.expr.op == PT_TIMES ||
		node->info.expr.op == PT_DIVIDE ||
		node->info.expr.op == PT_MODULUS ||
		node->info.expr.op == PT_POWER ||
		node->info.expr.op == PT_ROUND ||
		node->info.expr.op == PT_LOG ||
		node->info.expr.op == PT_TRUNC ||
		node->info.expr.op == PT_POSITION ||
		node->info.expr.op == PT_LPAD ||
		node->info.expr.op == PT_RPAD ||
		node->info.expr.op == PT_REPLACE ||
		node->info.expr.op == PT_TRANSLATE ||
		node->info.expr.op == PT_ADD_MONTHS ||
		node->info.expr.op == PT_MONTHS_BETWEEN ||
		node->info.expr.op == PT_TO_NUMBER ||
		node->info.expr.op == PT_LEAST ||
		node->info.expr.op == PT_GREATEST ||
		node->info.expr.op == PT_CASE ||
		node->info.expr.op == PT_NULLIF ||
		node->info.expr.op == PT_COALESCE ||
		node->info.expr.op == PT_NVL ||
		node->info.expr.op == PT_DECODE ||
		node->info.expr.op == PT_STRCAT)
	      {
		r1 = pt_to_regu_variable (parser,
					  node->info.expr.arg1, unbox);
		r2 = pt_to_regu_variable (parser,
					  node->info.expr.arg2, unbox);

		if (node->info.expr.op != PT_ADD_MONTHS &&
		    node->info.expr.op != PT_MONTHS_BETWEEN &&
		    node->info.expr.op != PT_TO_NUMBER)
		  {
		    domain = pt_xasl_node_to_domain (parser, node);
		    if (domain == NULL)
		      {
			goto end_expr_op_switch;
		      }
		  }
	      }
	    else if (node->info.expr.op == PT_UNARY_MINUS ||
		     node->info.expr.op == PT_RANDOM ||
		     node->info.expr.op == PT_DRANDOM ||
		     node->info.expr.op == PT_FLOOR ||
		     node->info.expr.op == PT_CEIL ||
		     node->info.expr.op == PT_SIGN ||
		     node->info.expr.op == PT_EXP ||
		     node->info.expr.op == PT_SQRT ||
		     node->info.expr.op == PT_ABS ||
		     node->info.expr.op == PT_CHR ||
		     node->info.expr.op == PT_OCTET_LENGTH ||
		     node->info.expr.op == PT_BIT_LENGTH ||
		     node->info.expr.op == PT_CHAR_LENGTH ||
		     node->info.expr.op == PT_LOWER ||
		     node->info.expr.op == PT_UPPER ||
		     node->info.expr.op == PT_LAST_DAY ||
		     node->info.expr.op == PT_CAST ||
		     node->info.expr.op == PT_EXTRACT ||
		     node->info.expr.op == PT_ENCRYPT ||
		     node->info.expr.op == PT_DECRYPT)
	      {
		r1 = NULL;
		r2 = pt_to_regu_variable (parser,
					  node->info.expr.arg1, unbox);

		if (node->info.expr.op != PT_LAST_DAY &&
		    node->info.expr.op != PT_CAST)
		  {
		    domain = pt_xasl_node_to_domain (parser, node);
		    if (domain == NULL)
		      {
			goto end_expr_op_switch;
		      }
		  }
	      }
	    else if (node->info.expr.op == PT_INCR ||
		     node->info.expr.op == PT_DECR ||
		     node->info.expr.op == PT_INSTR ||
		     node->info.expr.op == PT_SUBSTRING ||
		     node->info.expr.op == PT_NVL2)
	      {
		r1 = pt_to_regu_variable (parser,
					  node->info.expr.arg1, unbox);
		r2 = pt_to_regu_variable (parser,
					  node->info.expr.arg2, unbox);
		r3 = pt_to_regu_variable (parser,
					  node->info.expr.arg3, unbox);
		domain = pt_xasl_node_to_domain (parser, node);
		if (domain == NULL)
		  {
		    goto end_expr_op_switch;
		  }
	      }
	    else if (node->info.expr.op == PT_TO_CHAR ||
		     node->info.expr.op == PT_TO_DATE ||
		     node->info.expr.op == PT_TO_TIME ||
		     node->info.expr.op == PT_TO_TIMESTAMP)
	      {
		r1 = pt_to_regu_variable (parser,
					  node->info.expr.arg1, unbox);
		r2 = pt_to_regu_variable (parser,
					  node->info.expr.arg2, unbox);
		r3 = pt_to_regu_variable (parser,
					  node->info.expr.arg3, unbox);
	      }
	    else if (node->info.expr.op == PT_RAND ||
		     node->info.expr.op == PT_DRAND ||
		     node->info.expr.op == PT_SYS_DATE ||
		     node->info.expr.op == PT_SYS_TIME ||
		     node->info.expr.op == PT_SYS_TIMESTAMP ||
		     node->info.expr.op == PT_LOCAL_TRANSACTION_ID)
	      {
		domain = pt_xasl_node_to_domain (parser, node);
		if (domain == NULL)
		  {
		    goto end_expr_op_switch;
		  }
	      }

	    switch (node->info.expr.op)
	      {
	      case PT_PLUS:
		regu = pt_make_regu_arith (r1, r2, NULL, T_ADD, domain);
		break;

	      case PT_MINUS:
		regu = pt_make_regu_arith (r1, r2, NULL, T_SUB, domain);
		break;

	      case PT_TIMES:
		regu = pt_make_regu_arith (r1, r2, NULL, T_MUL, domain);
		break;

	      case PT_DIVIDE:
		regu = pt_make_regu_arith (r1, r2, NULL, T_DIV, domain);
		break;

	      case PT_UNARY_MINUS:
		regu = pt_make_regu_arith (r1, r2, NULL, T_UNMINUS, domain);
		break;

	      case PT_MODULUS:
		regu = pt_make_regu_arith (r1, r2, NULL, T_MOD, domain);
		parser->etc = (void *) 1;
		break;

	      case PT_RAND:
		regu = pt_make_regu_arith (NULL, NULL, NULL, T_RAND, domain);
		break;

	      case PT_DRAND:
		regu = pt_make_regu_arith (NULL, NULL, NULL, T_DRAND, domain);
		break;

	      case PT_RANDOM:
		regu = pt_make_regu_arith (r1, r2, NULL, T_RANDOM, domain);
		break;

	      case PT_DRANDOM:
		regu = pt_make_regu_arith (r1, r2, NULL, T_DRANDOM, domain);
		break;

	      case PT_FLOOR:
		regu = pt_make_regu_arith (r1, r2, NULL, T_FLOOR, domain);
		break;

	      case PT_CEIL:
		regu = pt_make_regu_arith (r1, r2, NULL, T_CEIL, domain);
		break;

	      case PT_SIGN:
		regu = pt_make_regu_arith (r1, r2, NULL, T_SIGN, domain);
		break;

	      case PT_POWER:
		regu = pt_make_regu_arith (r1, r2, NULL, T_POWER, domain);
		break;

	      case PT_ROUND:
		regu = pt_make_regu_arith (r1, r2, NULL, T_ROUND, domain);
		break;

	      case PT_LOG:
		regu = pt_make_regu_arith (r1, r2, NULL, T_LOG, domain);
		break;

	      case PT_EXP:
		regu = pt_make_regu_arith (r1, r2, NULL, T_EXP, domain);
		break;

	      case PT_SQRT:
		regu = pt_make_regu_arith (r1, r2, NULL, T_SQRT, domain);
		break;

	      case PT_TRUNC:
		regu = pt_make_regu_arith (r1, r2, NULL, T_TRUNC, domain);
		break;

	      case PT_INCR:
		regu = pt_make_regu_arith (r1, r2, r3, T_INCR, domain);
		break;

	      case PT_DECR:
		regu = pt_make_regu_arith (r1, r2, r3, T_DECR, domain);
		break;

	      case PT_ABS:
		regu = pt_make_regu_arith (r1, r2, NULL, T_ABS, domain);
		break;

	      case PT_CHR:
		regu = pt_make_regu_arith (r1, r2, NULL, T_CHR, domain);
		break;

	      case PT_INSTR:
		regu = pt_make_regu_arith (r1, r2, r3, T_INSTR, domain);
		break;

	      case PT_POSITION:
		regu = pt_make_regu_arith (r1, r2, NULL, T_POSITION, domain);
		break;

	      case PT_SUBSTRING:
		regu = pt_make_regu_arith (r1, r2, r3, T_SUBSTRING, domain);
		pt_to_misc_operand (regu, node->info.expr.qualifier);
		break;

	      case PT_OCTET_LENGTH:
		regu = pt_make_regu_arith (r1, r2, NULL,
					   T_OCTET_LENGTH, domain);
		break;

	      case PT_BIT_LENGTH:
		regu = pt_make_regu_arith (r1, r2, NULL,
					   T_BIT_LENGTH, domain);
		break;

	      case PT_CHAR_LENGTH:
		regu = pt_make_regu_arith (r1, r2, NULL,
					   T_CHAR_LENGTH, domain);
		break;

	      case PT_LOWER:
		regu = pt_make_regu_arith (r1, r2, NULL, T_LOWER, domain);
		break;

	      case PT_UPPER:
		regu = pt_make_regu_arith (r1, r2, NULL, T_UPPER, domain);
		break;

	      case PT_LTRIM:
		{
		  PT_NODE *empty_str;

		  if (node->info.expr.arg2 == NULL)
		    {
		      empty_str = pt_make_empty_string (parser, node);
		    }
		  r1 = pt_to_regu_variable (parser,
					    node->info.expr.arg1, unbox);
		  r2 = (node->info.expr.arg2)
		    ? pt_to_regu_variable (parser, node->info.expr.arg2,
					   unbox)
		    : pt_to_regu_variable (parser, empty_str, unbox);
		  domain = pt_xasl_node_to_domain (parser, node);
		  if (domain == NULL)
		    {
		      break;
		    }
		  regu = pt_make_regu_arith (r1, r2, NULL, T_LTRIM, domain);
		  if (node->info.expr.arg2 == NULL)
		    {
		      parser_free_tree (parser, empty_str);
		    }
		}
		break;

	      case PT_RTRIM:
		{
		  PT_NODE *empty_str;

		  if (node->info.expr.arg2 == NULL)
		    {
		      empty_str = pt_make_empty_string (parser, node);
		    }

		  r1 = pt_to_regu_variable (parser,
					    node->info.expr.arg1, unbox);
		  r2 = (node->info.expr.arg2)
		    ? pt_to_regu_variable (parser, node->info.expr.arg2,
					   unbox)
		    : pt_to_regu_variable (parser, empty_str, unbox);
		  domain = pt_xasl_node_to_domain (parser, node);
		  if (domain == NULL)
		    {
		      break;
		    }
		  regu = pt_make_regu_arith (r1, r2, NULL, T_RTRIM, domain);
		  if (node->info.expr.arg2 == NULL)
		    {
		      parser_free_tree (parser, empty_str);
		    }
		}
		break;

	      case PT_LPAD:
		{
		  PT_NODE *empty_str;

		  if (node->info.expr.arg3 == NULL)
		    {
		      empty_str = pt_make_empty_string (parser, node);
		    }

		  r3 = (node->info.expr.arg3)
		    ? pt_to_regu_variable (parser, node->info.expr.arg3,
					   unbox)
		    : pt_to_regu_variable (parser, empty_str, unbox);
		  regu = pt_make_regu_arith (r1, r2, r3, T_LPAD, domain);
		  if (node->info.expr.arg3 == NULL)
		    {
		      parser_free_tree (parser, empty_str);
		    }
		  break;
		}

	      case PT_RPAD:
		{
		  PT_NODE *empty_str;

		  if (node->info.expr.arg3 == NULL)
		    {
		      empty_str = pt_make_empty_string (parser, node);
		    }

		  r3 = (node->info.expr.arg3)
		    ? pt_to_regu_variable (parser, node->info.expr.arg3,
					   unbox)
		    : pt_to_regu_variable (parser, empty_str, unbox);
		  regu = pt_make_regu_arith (r1, r2, r3, T_RPAD, domain);
		  if (node->info.expr.arg3 == NULL)
		    {
		      parser_free_tree (parser, empty_str);
		    }
		  break;
		}

	      case PT_REPLACE:
		{
		  PT_NODE *empty_str;

		  if (node->info.expr.arg3 == NULL)
		    {
		      empty_str = pt_make_empty_string (parser, node);
		    }

		  r3 = (node->info.expr.arg3)
		    ? pt_to_regu_variable (parser, node->info.expr.arg3,
					   unbox)
		    : pt_to_regu_variable (parser, empty_str, unbox);
		  regu = pt_make_regu_arith (r1, r2, r3, T_REPLACE, domain);
		  if (node->info.expr.arg3 == NULL)
		    {
		      parser_free_tree (parser, empty_str);
		    }
		  break;
		}

	      case PT_TRANSLATE:
		{
		  PT_NODE *empty_str;

		  if (node->info.expr.arg3 == NULL)
		    {
		      empty_str = pt_make_empty_string (parser, node);
		    }

		  r3 = (node->info.expr.arg3)
		    ? pt_to_regu_variable (parser, node->info.expr.arg3,
					   unbox)
		    : pt_to_regu_variable (parser, empty_str, unbox);
		  regu = pt_make_regu_arith (r1, r2, r3, T_TRANSLATE, domain);
		  if (node->info.expr.arg3 == NULL)
		    {
		      parser_free_tree (parser, empty_str);
		    }
		  break;
		}

	      case PT_ADD_MONTHS:
		data_type = pt_make_prim_data_type (parser, PT_TYPE_DATE);
		domain = pt_xasl_data_type_to_domain (parser, data_type);

		regu = pt_make_regu_arith (r1, r2, NULL,
					   T_ADD_MONTHS, domain);
		parser_free_tree (parser, data_type);
		break;

	      case PT_LAST_DAY:
		data_type = pt_make_prim_data_type (parser, PT_TYPE_DATE);
		domain = pt_xasl_data_type_to_domain (parser, data_type);

		regu = pt_make_regu_arith (r1, r2, NULL, T_LAST_DAY, domain);
		parser_free_tree (parser, data_type);
		break;

	      case PT_MONTHS_BETWEEN:
		data_type = pt_make_prim_data_type (parser, PT_TYPE_DOUBLE);
		domain = pt_xasl_data_type_to_domain (parser, data_type);

		regu = pt_make_regu_arith (r1, r2, NULL,
					   T_MONTHS_BETWEEN, domain);

		parser_free_tree (parser, data_type);
		break;

	      case PT_SYS_DATE:
		regu = pt_make_regu_arith (NULL, NULL, NULL,
					   T_SYS_DATE, domain);
		break;

	      case PT_SYS_TIME:
		regu = pt_make_regu_arith (NULL, NULL, NULL,
					   T_SYS_TIME, domain);
		break;

	      case PT_SYS_TIMESTAMP:
		regu = pt_make_regu_arith (NULL, NULL, NULL,
					   T_SYS_TIMESTAMP, domain);
		break;

	      case PT_LOCAL_TRANSACTION_ID:
		regu = pt_make_regu_arith (NULL, NULL, NULL,
					   T_LOCAL_TRANSACTION_ID, domain);
		break;

	      case PT_CURRENT_USER:
		{
		  PT_NODE *current_user_val;

		  current_user_val = parser_new_node (parser, PT_VALUE);

		  current_user_val->type_enum = PT_TYPE_VARCHAR;
		  current_user_val->info.value.string_type = ' ';

		  current_user_val->info.value.data_value.str =
		    pt_append_nulstring (parser, NULL, au_user_name ());
		  current_user_val->info.value.text =
		    (char *) current_user_val->info.value.data_value.str->
		    bytes;

		  regu = pt_to_regu_variable (parser,
					      current_user_val, unbox);
		  break;
		}

	      case PT_TO_CHAR:
		data_type = pt_make_prim_data_type (parser, PT_TYPE_VARCHAR);
		domain = pt_xasl_data_type_to_domain (parser, data_type);

		regu = pt_make_regu_arith (r1, r2, r3, T_TO_CHAR, domain);
		parser_free_tree (parser, data_type);
		break;

	      case PT_TO_DATE:
		data_type = pt_make_prim_data_type (parser, PT_TYPE_DATE);
		domain = pt_xasl_data_type_to_domain (parser, data_type);

		regu = pt_make_regu_arith (r1, r2, r3, T_TO_DATE, domain);

		parser_free_tree (parser, data_type);
		break;

	      case PT_TO_TIME:
		data_type = pt_make_prim_data_type (parser, PT_TYPE_TIME);
		domain = pt_xasl_data_type_to_domain (parser, data_type);

		regu = pt_make_regu_arith (r1, r2, r3, T_TO_TIME, domain);
		parser_free_tree (parser, data_type);
		break;

	      case PT_TO_TIMESTAMP:
		data_type = pt_make_prim_data_type (parser,
						    PT_TYPE_TIMESTAMP);
		domain = pt_xasl_data_type_to_domain (parser, data_type);

		regu = pt_make_regu_arith (r1, r2, r3,
					   T_TO_TIMESTAMP, domain);
		parser_free_tree (parser, data_type);
		break;

	      case PT_TO_NUMBER:
		{
		  int precision, scale;

		  param_empty = parser_new_node (parser, PT_VALUE);
		  param_empty->type_enum = PT_TYPE_INTEGER;
		  param_empty->info.value.data_value.i =
		    node->info.expr.arg2 ? 0 : 1;

		  /* If 2nd agrument of to_number() exists, modify domain. */
		  pt_to_regu_resolve_domain (&precision, &scale,
					     node->info.expr.arg2);
		  data_type = pt_make_prim_data_type_fortonum (parser,
							       precision,
							       scale);

		  /* create NUMERIC domain with defalut precisin and scale. */
		  domain = pt_xasl_data_type_to_domain (parser, data_type);

		  /* If 2nd agrument of to_number() exists, modify domain. */
		  pt_to_regu_resolve_domain (&domain->precision,
					     &domain->scale,
					     node->info.expr.arg2);

		  r3 = pt_to_regu_variable (parser, param_empty, unbox);

		  /* Note that use the new domain */
		  regu = pt_make_regu_arith (r1, r2, r3, T_TO_NUMBER, domain);
		  parser_free_tree (parser, data_type);
		  parser_free_tree (parser, param_empty);
		  break;
		}

	      case PT_CURRENT_VALUE:
		{
		  DB_IDENTIFIER serial_obj_id;
		  PT_NODE *oid_str_val;
		  int found = 0, r = 0;
		  char *serial_name = NULL, *t = NULL;
		  char oid_str[32];

		  data_type = pt_make_prim_data_type (parser,
						      PT_TYPE_NUMERIC);
		  domain = pt_xasl_data_type_to_domain (parser, data_type);

		  /* convert node->info.expr.arg1 into serial object's OID */
		  serial_name = (char *)
		    node->info.expr.arg1->info.value.data_value.str->bytes;

		  t = strchr (serial_name, '.');
		  serial_name = (t != NULL) ? t + 1 : serial_name;

		  r = do_get_serial_obj_id (&serial_obj_id,
					    &found, serial_name);
		  if (r == 0 && found)
		    {
		      sprintf (oid_str, "%d %d %d", serial_obj_id.pageid,
			       serial_obj_id.slotid, serial_obj_id.volid);
		      oid_str_val = parser_new_node (parser, PT_VALUE);
		      oid_str_val->type_enum = PT_TYPE_CHAR;
		      oid_str_val->info.value.string_type = ' ';
		      oid_str_val->info.value.data_value.str =
			pt_append_bytes (parser, NULL, oid_str,
					 strlen (oid_str) + 1);
		      oid_str_val->info.value.text =
			(char *) oid_str_val->info.value.data_value.str->
			bytes;

		      r1 = NULL;
		      r2 = pt_to_regu_variable (parser, oid_str_val, unbox);

		      regu = pt_make_regu_arith (r1, r2, NULL,
						 T_CURRENT_VALUE, domain);
		      parser_free_tree (parser, oid_str_val);
		    }
		  else
		    {
		      PT_ERRORmf (parser, node, MSGCAT_SET_PARSER_SEMANTIC,
				  MSGCAT_SEMANTIC_SERIAL_NOT_DEFINED,
				  serial_name);
		    }

		  parser_free_tree (parser, data_type);
		  break;
		}

	      case PT_NEXT_VALUE:
		{
		  DB_IDENTIFIER serial_obj_id;
		  PT_NODE *oid_str_val;
		  int found = 0, r = 0;
		  char *serial_name = NULL, *t = NULL;
		  char oid_str[36];

		  data_type = pt_make_prim_data_type (parser,
						      PT_TYPE_NUMERIC);
		  domain = pt_xasl_data_type_to_domain (parser, data_type);

		  /* convert node->info.expr.arg1 into serial object's OID */
		  serial_name =
		    (char *) node->info.expr.arg1->info.value.data_value.str->
		    bytes;
		  t = strchr (serial_name, '.');
		  serial_name = (t != NULL) ? t + 1 : serial_name;
		  r = do_get_serial_obj_id (&serial_obj_id,
					    &found, serial_name);
		  if (r == 0 && found)
		    {
		      sprintf (oid_str, "%d %d %d", serial_obj_id.pageid,
			       serial_obj_id.slotid, serial_obj_id.volid);
		      oid_str_val = parser_new_node (parser, PT_VALUE);
		      oid_str_val->type_enum = PT_TYPE_CHAR;
		      oid_str_val->info.value.string_type = ' ';
		      oid_str_val->info.value.data_value.str =
			pt_append_bytes (parser, NULL, oid_str,
					 strlen (oid_str) + 1);
		      oid_str_val->info.value.text =
			(char *) oid_str_val->info.value.data_value.str->
			bytes;

		      r1 = NULL;
		      r2 = pt_to_regu_variable (parser, oid_str_val, unbox);
		      regu = pt_make_regu_arith (r1, r2, NULL,
						 T_NEXT_VALUE, domain);
		      parser_free_tree (parser, oid_str_val);
		    }
		  else
		    {
		      PT_ERRORmf (parser, node, MSGCAT_SET_PARSER_SEMANTIC,
				  MSGCAT_SEMANTIC_SERIAL_NOT_DEFINED,
				  serial_name);
		    }

		  parser_free_tree (parser, data_type);
		  break;
		}

	      case PT_TRIM:
		{
		  PT_NODE *empty_str;

		  if (node->info.expr.arg2 == NULL)
		    {
		      empty_str = pt_make_empty_string (parser, node);
		    }

		  r1 = pt_to_regu_variable (parser,
					    node->info.expr.arg1, unbox);
		  r2 = (node->info.expr.arg2)
		    ? pt_to_regu_variable (parser, node->info.expr.arg2,
					   unbox)
		    : pt_to_regu_variable (parser, empty_str, unbox);
		  domain = pt_xasl_node_to_domain (parser, node);
		  if (domain == NULL)
		    {
		      break;
		    }
		  regu = pt_make_regu_arith (r1, r2, NULL, T_TRIM, domain);

		  pt_to_misc_operand (regu, node->info.expr.qualifier);
		  if (node->info.expr.arg2 == NULL)
		    {
		      parser_free_tree (parser, empty_str);
		    }
		}
		break;

	      case PT_INST_NUM:
	      case PT_ROWNUM:
	      case PT_ORDERBY_NUM:
		regu = pt_make_regu_numbering (parser, node);
		break;

	      case PT_LEAST:
		regu = pt_make_regu_arith (r1, r2, NULL, T_LEAST, domain);
		break;

	      case PT_GREATEST:
		regu = pt_make_regu_arith (r1, r2, NULL, T_GREATEST, domain);
		break;

	      case PT_CAST:
		domain = pt_xasl_data_type_to_domain
		  (parser, node->info.expr.cast_type);
		regu = pt_make_regu_arith (r1, r2, NULL, T_CAST, domain);
		break;

	      case PT_CASE:
		regu = pt_make_regu_arith (r1, r2, NULL, T_CASE, domain);
		if (regu == NULL)
		  {
		    break;
		  }
		regu->value.arithptr->pred =
		  pt_to_pred_expr (parser, node->info.expr.arg3);
		break;

	      case PT_NULLIF:
		regu = pt_make_regu_arith (r1, r2, NULL, T_NULLIF, domain);
		break;

	      case PT_COALESCE:
		regu = pt_make_regu_arith (r1, r2, NULL, T_COALESCE, domain);
		break;

	      case PT_NVL:
		regu = pt_make_regu_arith (r1, r2, NULL, T_NVL, domain);
		break;

	      case PT_NVL2:
		regu = pt_make_regu_arith (r1, r2, r3, T_NVL2, domain);
		break;

	      case PT_DECODE:
		regu = pt_make_regu_arith (r1, r2, NULL, T_DECODE, domain);
		if (regu == NULL)
		  {
		    break;
		  }
		regu->value.arithptr->pred =
		  pt_to_pred_expr (parser, node->info.expr.arg3);
		break;

	      case PT_EXTRACT:
		regu = pt_make_regu_arith (r1, r2, NULL, T_EXTRACT, domain);
		pt_to_misc_operand (regu, node->info.expr.qualifier);
		break;

	      case PT_STRCAT:
		regu = pt_make_regu_arith (r1, r2, NULL, T_STRCAT, domain);
		break;

	      default:
		break;
	      }

	  end_expr_op_switch:

	    if (regu && domain)
	      {
		regu->domain = domain;
	      }
	    break;
	  }

	case PT_HOST_VAR:
	  regu = pt_make_regu_hostvar (parser, node);
	  break;

	case PT_VALUE:
	  value = pt_value_to_db (parser, node);

	  if (value)
	    {
	      regu = pt_make_regu_constant (parser, value,
					    pt_node_to_db_type (node), node);
	    }
	  break;

	case PT_NAME:
	  if (node->info.name.meta_class == PT_PARAMETER)
	    {
	      value = pt_find_value_of_label (node->info.name.original);
	      if (value)
		{
		  /* Note that the value in the label table will be destroyed
		   * if another assignment is made with the same name !
		   * be sure that the lifetime of this regu node will
		   * not overlap the processing of another statement
		   * that may result in label assignment.  If this can happen,
		   * we'll have to copy the value and remember to free
		   * it when the regu node goes away
		   */
		  regu = pt_make_regu_constant (parser, value,
						pt_node_to_db_type (node),
						node);
		}
	      else
		{
		  PT_ERRORmf (parser, node, MSGCAT_SET_PARSER_SEMANTIC,
			      MSGCAT_SEMANTIC_IS_NOT_DEFINED,
			      parser_print_tree (parser, node));
		}
	    }
	  else if (node->info.name.db_object &&
		   node->info.name.meta_class != PT_SHARED &&
		   node->info.name.meta_class != PT_META_ATTR &&
		   node->info.name.meta_class != PT_META_CLASS &&
		   node->info.name.meta_class != PT_OID_ATTR &&
		   node->info.name.meta_class != PT_CLASSOID_ATTR)
	    {
	      val = regu_dbval_alloc ();
	      pt_evaluate_tree (parser, node, val);
	      if (!parser->error_msgs)
		{
		  regu = pt_make_regu_constant (parser, val,
						pt_node_to_db_type (node),
						node);
		}
	    }
	  else
	    {
	      regu = pt_attribute_to_regu (parser, node);
	    }

	  break;

	case PT_FUNCTION:
	  regu = pt_function_to_regu (parser, node);
	  break;

	case PT_SELECT:
	case PT_UNION:
	case PT_DIFFERENCE:
	case PT_INTERSECTION:
	  xasl = (XASL_NODE *) node->info.query.xasl;
	  if (xasl)
	    {
	      PT_NODE *select_list = pt_get_select_list (parser, node);
	      if (unbox != UNBOX_AS_TABLE &&
		  pt_length_of_select_list (select_list,
					    EXCLUDE_HIDDEN_COLUMNS) != 1)
		{
		  PT_ERRORmf (parser, node, MSGCAT_SET_PARSER_RUNTIME,
			      MSGCAT_RUNTIME_WANT_ONE_COL,
			      parser_print_tree (parser, node));
		}

	      regu = pt_make_regu_subquery (parser, xasl, unbox, node);
	    }
	  break;

	default:
	  /* force error */
	  regu = NULL;
	}

      /* restore node link */
      if (node)
	{
	  node->next = save_next;
	}

      node = save_node;		/* restore */
    }

  if (regu == NULL)
    {
      if (!parser->error_msgs)
	{
	  PT_INTERNAL_ERROR (parser, "generate var");
	}
    }

  if (val)
    {
      pr_clear_value (val);
    }

  return regu;
}


/*
 * pt_to_regu_variable_list () - converts a parse expression tree list
 *                               to regu_variable_list
 *   return: A NULL return indicates an error occured
 *   parser(in):
 *   node_list(in):
 *   unbox(in):
 *   value_list(in):
 *   attr_offsets(in):
 */
REGU_VARIABLE_LIST
pt_to_regu_variable_list (PARSER_CONTEXT * parser,
			  PT_NODE * node_list,
			  UNBOX unbox,
			  VAL_LIST * value_list, int *attr_offsets)
{
  REGU_VARIABLE_LIST regu_list = NULL;
  REGU_VARIABLE_LIST *tail = NULL;
  REGU_VARIABLE *regu;
  PT_NODE *node;
  int i = 0;

  tail = &regu_list;

  for (node = node_list; node != NULL; node = node->next)
    {
      (*tail) = regu_varlist_alloc ();
      regu = pt_to_regu_variable (parser, node, unbox);

      if (attr_offsets && value_list && regu)
	{
	  regu->vfetch_to = pt_index_value (value_list, attr_offsets[i]);
	}
      i++;

      if (regu && *tail)
	{
	  (*tail)->value = *regu;
	  tail = &(*tail)->next;
	}
      else
	{
	  regu_list = NULL;
	  break;
	}
    }

  return regu_list;
}


/*
 * pt_regu_to_dbvalue () -
 *   return:
 *   parser(in):
 *   regu(in):
 */
DB_VALUE *
pt_regu_to_dbvalue (PARSER_CONTEXT * parser, REGU_VARIABLE * regu)
{
  DB_VALUE *val = NULL;

  if (regu->type == TYPE_CONSTANT)
    {
      val = regu->value.dbvalptr;
    }
  else if (regu->type == TYPE_DBVAL)
    {
      val = &regu->value.dbval;
    }
  else
    {
      if (!parser->error_msgs)
	{
	  PT_INTERNAL_ERROR (parser, "generate val");
	}
    }

  return val;
}


/*
 * pt_make_position_regu_variable () - converts a parse expression tree list
 *                                     to regu_variable_list
 *   return:
 *   parser(in):
 *   node(in):
 *   i(in):
 */
static REGU_VARIABLE *
pt_make_position_regu_variable (PARSER_CONTEXT * parser,
				const PT_NODE * node, int i)
{
  REGU_VARIABLE *regu = NULL;
  TP_DOMAIN *domain;

  domain = pt_xasl_node_to_domain (parser, node);

  regu = regu_var_alloc ();

  regu->type = TYPE_POSITION;
  regu->domain = domain;
  regu->value.pos_descr.pos_no = i;
  regu->value.pos_descr.dom = domain;

  return regu;
}


/*
 * pt_to_position_regu_variable_list () - converts a parse expression tree
 *                                        list to regu_variable_list
 *   return:
 *   parser(in):
 *   node_list(in):
 *   value_list(in):
 *   attr_offsets(in):
 */
REGU_VARIABLE_LIST
pt_to_position_regu_variable_list (PARSER_CONTEXT * parser,
				   PT_NODE * node_list, VAL_LIST * value_list,
				   int *attr_offsets)
{
  REGU_VARIABLE_LIST regu_list = NULL;
  REGU_VARIABLE_LIST *tail = NULL;
  PT_NODE *node;
  int i = 0;

  tail = &regu_list;

  for (node = node_list; node != NULL; node = node->next)
    {
      (*tail) = regu_varlist_alloc ();

      /* it would be better form to call pt_make_position_regu_variable,
       * but this avoids additional allocation do to regu variable
       * and regu_variable_list bizareness.
       */
      if (*tail)
	{
	  TP_DOMAIN *domain = pt_xasl_node_to_domain (parser, node);

	  (*tail)->value.type = TYPE_POSITION;
	  (*tail)->value.domain = domain;

	  if (attr_offsets)
	    {
	      (*tail)->value.value.pos_descr.pos_no = attr_offsets[i];
	    }
	  else
	    {
	      (*tail)->value.value.pos_descr.pos_no = i;
	    }

	  (*tail)->value.value.pos_descr.dom = domain;

	  if (attr_offsets && value_list)
	    {
	      (*tail)->value.vfetch_to =
		pt_index_value (value_list, attr_offsets[i]);
	    }

	  tail = &(*tail)->next;
	  i++;
	}
      else
	{
	  PT_ERRORm (parser, node, MSGCAT_SET_PARSER_SEMANTIC,
		     MSGCAT_SEMANTIC_OUT_OF_MEMORY);
	  regu_list = NULL;
	  break;
	}
    }

  return regu_list;
}

/*
 * pt_to_regu_attr_descr () -
 *   return: int
 *   attr_descr(in): pointer to an attribute descriptor
 *   attr_id(in): attribute id
 *   type(in): attribute type
 */

static REGU_VARIABLE *
pt_to_regu_attr_descr (PARSER_CONTEXT * parser, DB_OBJECT * class_object,
		       HEAP_CACHE_ATTRINFO * cache_attrinfo, PT_NODE * attr)
{
  const char *attr_name = attr->info.name.original;
  int attr_id;
  SM_DOMAIN *smdomain = NULL;
  int sharedp;
  REGU_VARIABLE *regu;
  ATTR_DESCR *attr_descr;

  if ((sm_att_info (class_object, attr_name, &attr_id, &smdomain, &sharedp,
		    (attr->info.name.meta_class == PT_META_ATTR) != NO_ERROR))
      || (smdomain == NULL) || (!(regu = regu_var_alloc ())))
    {
      return NULL;
    }

  attr_descr = &regu->value.attr_descr;
  UT_CLEAR_ATTR_DESCR (attr_descr);

  regu->type = (sharedp) ? TYPE_SHARED_ATTR_ID
    : (attr->info.name.meta_class == PT_META_ATTR)
    ? TYPE_CLASS_ATTR_ID : TYPE_ATTR_ID;

  regu->domain = (TP_DOMAIN *) smdomain;
  attr_descr->id = attr_id;
  attr_descr->cache_attrinfo = cache_attrinfo;

  if (smdomain)
    {
      attr_descr->type = smdomain->type->id;
    }

  return regu;
}


/*
 * pt_attribute_to_regu () - Convert an attribute spec into a REGU_VARIABLE
 *   return:
 *   parser(in):
 *   attr(in):
 *
 * Note :
 * If "current_class" is non-null, use it to create a TYPE_ATTRID REGU_VARIABLE
 * Otherwise, create a TYPE_CONSTANT REGU_VARIABLE pointing to the symbol
 * table's value_list DB_VALUE, in the position matching where attr is
 * found in attribute_list.
 */
REGU_VARIABLE *
pt_attribute_to_regu (PARSER_CONTEXT * parser, PT_NODE * attr)
{
  REGU_VARIABLE *regu = NULL;
  SYMBOL_INFO *symbols;
  DB_VALUE *dbval = NULL;
  TABLE_INFO *table_info;
  int list_index;

  CAST_POINTER_TO_NODE (attr);

  if (attr && attr->node_type == PT_NAME)
    {
      symbols = parser->symbols;
    }
  else
    {
      symbols = NULL;		/* error */
    }

  if (symbols && attr)
    {
      /* check the current scope first */
      table_info = pt_find_table_info (attr->info.name.spec_id,
				       symbols->table_info);

      if (table_info)
	{
	  /* We have found the attribute at this scope.
	   * If we had not, the attribute must have been a correlated
	   * reference to an attribute at an outer scope. The correlated
	   * case is handled below in this "if" statements "else" clause.
	   * determine if this is relative to a particular class
	   * or if the attribute should be relative to the placeholder.
	   */

	  if (symbols->current_class
	      && (table_info->spec_id
		  == symbols->current_class->info.name.spec_id))
	    {
	      /* determine if this is an attribute, or an oid identifier */
	      if (PT_IS_OID_NAME (attr))
		{
		  regu = regu_var_alloc ();
		  if (regu)
		    {
		      regu->type = TYPE_OID;
		      regu->domain = pt_xasl_node_to_domain (parser, attr);
		    }
		}
	      else if (attr->info.name.meta_class == PT_META_CLASS)
		{
		  regu = regu_var_alloc ();
		  if (regu)
		    {
		      regu->type = TYPE_CLASSOID;
		      regu->domain = pt_xasl_node_to_domain (parser, attr);
		    }
		}
	      else
		{
		  /* this is an attribute reference */
		  if (symbols->current_class->info.name.db_object)
		    {
		      regu = pt_to_regu_attr_descr
			(parser,
			 symbols->current_class->info.name.db_object,
			 symbols->cache_attrinfo, attr);
		    }
		  else
		    {
		      /* system error, we should have understood this name. */
		      if (!parser->error_msgs)
			{
			  PT_INTERNAL_ERROR (parser, "generate attr");
			}
		      regu = NULL;
		    }
		}

	      if (DB_TYPE_VOBJ == pt_node_to_db_type (attr))
		{
		  regu = pt_make_vid (parser, attr->data_type, regu);
		}

	    }
	  else if (symbols->current_listfile
		   && (list_index = pt_find_attribute
		       (parser, attr, symbols->current_listfile)) >= 0)
	    {
	      /* add in the listfile attribute offset.  This is used
	       * primarily for server update and insert constraint predicates
	       * because the server update prepends two columns onto the
	       * select list of the listfile.
	       */
	      list_index += symbols->listfile_attr_offset;

	      if (symbols->listfile_value_list)
		{
		  regu = regu_var_alloc ();
		  regu->domain = pt_xasl_node_to_domain (parser, attr);
		  regu->type = TYPE_CONSTANT;
		  dbval = pt_index_value (symbols->listfile_value_list,
					  list_index);

		  if (dbval)
		    {
		      regu->value.dbvalptr = dbval;
		    }
		  else
		    {
		      regu = NULL;
		    }
		}
	      else
		{
		  /* here we need the position regu variable to access
		   * the list file directly, as in list access spec predicate
		   * evaluation.
		   */
		  regu = pt_make_position_regu_variable (parser, attr,
							 list_index);
		}
	    }
	  else
	    {
	      /* Here, we are determining attribute reference information
	       * relative to the list of attribute placeholders
	       * which will be fetched from the class(es). The "type"
	       * of the attribute no longer affects how the placeholder
	       * is referenced.
	       */
	      regu = regu_var_alloc ();
	      if (regu)
		{
		  regu->type = TYPE_CONSTANT;
		  regu->domain = pt_xasl_node_to_domain (parser, attr);
		  dbval = pt_index_value
		    (table_info->value_list,
		     pt_find_attribute (parser,
					attr, table_info->attribute_list));
		  if (dbval)
		    {
		      regu->value.dbvalptr = dbval;
		    }
		  else
		    {
		      if (PT_IS_OID_NAME (attr))
			{
			  if (regu)
			    {
			      regu->type = TYPE_OID;
			      regu->domain = pt_xasl_node_to_domain (parser,
								     attr);
			    }
			}
		      else
			{
			  regu = NULL;
			}
		    }
		}
	    }
	}
      else if ((regu = regu_var_alloc ()))
	{
	  /* The attribute is correlated variable.
	   * Find it in an enclosing scope(s).
	   * Note that this subquery has also just been determined to be
	   * a correlated subquery.
	   */
	  if (!symbols->stack)
	    {
	      if (!parser->error_msgs)
		{
		  PT_INTERNAL_ERROR (parser, "generate attr");
		}

	      regu = NULL;
	    }
	  else
	    {
	      while (symbols->stack && !table_info)
		{
		  symbols = symbols->stack;
		  /* mark succesive enclosing scopes correlated,
		   * until the attributes "home" is found. */
		  table_info = pt_find_table_info (attr->info.name.spec_id,
						   symbols->table_info);
		}

	      if (table_info)
		{
		  regu->type = TYPE_CONSTANT;
		  regu->domain = pt_xasl_node_to_domain (parser, attr);
		  dbval = pt_index_value
		    (table_info->value_list,
		     pt_find_attribute (parser,
					attr, table_info->attribute_list));
		  if (dbval)
		    {
		      regu->value.dbvalptr = dbval;
		    }
		  else
		    {
		      regu = NULL;
		    }
		}
	      else
		{
		  if (!parser->error_msgs)
		    {
		      PT_INTERNAL_ERROR (parser, "generate attr");
		    }

		  regu = NULL;
		}
	    }
	}
    }
  else
    {
      regu = NULL;
    }

  if (!regu && !parser->error_msgs)
    {
      const char *p = "unknown";

      if (attr)
	{
	  p = attr->info.name.original;
	}

      PT_INTERNAL_ERROR (parser, "generate attr");
    }

  return regu;
}


/*
 * pt_join_term_to_regu_variable () - Translate a PT_NODE path join term
 *      to the regu_variable to follow from (left hand side of path)
 *   return:
 *   parser(in):
 *   join_term(in):
 */
REGU_VARIABLE *
pt_join_term_to_regu_variable (PARSER_CONTEXT * parser, PT_NODE * join_term)
{
  REGU_VARIABLE *regu = NULL;

  if (join_term &&
      join_term->node_type == PT_EXPR && join_term->info.expr.op == PT_EQ)
    {
      regu = pt_to_regu_variable (parser, join_term->info.expr.arg1,
				  UNBOX_AS_VALUE);
    }

  return regu;
}


/*
 * op_type_to_range () -
 *   return:
 *   op_type(in):
 *   nterms(in):
 */
static RANGE
op_type_to_range (const PT_OP_TYPE op_type, const int nterms)
{
  switch (op_type)
    {
    case PT_EQ:
      return EQ_NA;
    case PT_GT:
      return (nterms > 1) ? GT_LE : GT_INF;
    case PT_GE:
      return (nterms > 1) ? GE_LE : GE_INF;
    case PT_LT:
      return (nterms > 1) ? GE_LT : INF_LT;
    case PT_LE:
      return (nterms > 1) ? GE_LE : INF_LE;
    case PT_BETWEEN:
      return GE_LE;
    case PT_EQ_SOME:
    case PT_IS_IN:
      return EQ_NA;
    case PT_BETWEEN_AND:
    case PT_BETWEEN_GE_LE:
      return GE_LE;
    case PT_BETWEEN_GE_LT:
      return GE_LT;
    case PT_BETWEEN_GT_LE:
      return GT_LE;
    case PT_BETWEEN_GT_LT:
      return GT_LT;
    case PT_BETWEEN_EQ_NA:
      return EQ_NA;
    case PT_BETWEEN_INF_LE:
      return (nterms > 1) ? GE_LE : INF_LE;
    case PT_BETWEEN_INF_LT:
      return (nterms > 1) ? GE_LT : INF_LT;
    case PT_BETWEEN_GE_INF:
      return (nterms > 1) ? GE_LE : GE_INF;
    case PT_BETWEEN_GT_INF:
      return (nterms > 1) ? GT_LE : GT_INF;
    default:
      return NA_NA;		/* error */
    }
}


/*
 * pt_to_single_key () - Create an key information(KEY_INFO) in INDX_INFO
 *      structure for index scan with range spec of R_ON, R_FROM and R_TO.
 *   return: 0 on success
 *   parser(in):
 *   term_exprs(in):
 *   nterms(in):
 *   multi_col(in):
 *   key_infop(out):
 */
static int
pt_to_single_key (PARSER_CONTEXT * parser,
		  PT_NODE ** term_exprs, int nterms, bool multi_col,
		  KEY_INFO * key_infop)
{
  PT_NODE *lhs, *rhs, *tmp, *midx_key;
  PT_OP_TYPE op_type;
  REGU_VARIABLE *regu_var;
  int i;

  midx_key = NULL;
  regu_var = NULL;
  key_infop->key_cnt = 0;
  key_infop->key_ranges = NULL;
  key_infop->is_constant = 1;

  for (i = 0; i < nterms; i++)
    {
      /* If nterms > 1, then it should be multi-column index and
         all term_exprs[0 .. nterms - 1] are equality expression.
         (Even though nterms == 1, it can be multi-column index.) */

      /* op type, LHS side and RHS side of this term expression */
      op_type = term_exprs[i]->info.expr.op;
      lhs = term_exprs[i]->info.expr.arg1;
      rhs = term_exprs[i]->info.expr.arg2;
      /* only PT_EQ */

      /* make sure the key value(RHS) can actually be compared against the
         index attribute(LHS) */
      if (pt_coerce_value (parser, rhs, rhs, lhs->type_enum, lhs->data_type))
	{
	  goto error;
	}

      regu_var = pt_to_regu_variable (parser, rhs, UNBOX_AS_VALUE);
      if (regu_var == NULL)
	{
	  goto error;
	}
      if (!VALIDATE_REGU_KEY (regu_var))
	{
	  /* correlared join index case swap LHS and RHS */
	  tmp = rhs;
	  rhs = lhs;
	  lhs = tmp;

	  /* make sure the key value(RHS) can actually be compared against the
	     index attribute(LHS) */
	  if (pt_coerce_value
	      (parser, rhs, rhs, lhs->type_enum, lhs->data_type))
	    {
	      goto error;
	    }

	  /* try on RHS */
	  regu_var = pt_to_regu_variable (parser, rhs, UNBOX_AS_VALUE);
	  if (regu_var == NULL || !VALIDATE_REGU_KEY (regu_var))
	    {
	      goto error;
	    }
	}

      /* is the key value constant(value or host variable)? */
      key_infop->is_constant &= (rhs->node_type == PT_VALUE ||
				 rhs->node_type == PT_HOST_VAR);

      /* if it is multi-column index, make one PT_NODE for midx key value
         by concatenating all RHS of the terms */
      if (multi_col)
	{
	  midx_key = parser_append_node (pt_point (parser, rhs), midx_key);
	}
    }				/* for (i = 0; i < nterms; i++) */

  if (midx_key)
    {
      /* make a midxkey regu variable for multi-column index */
      tmp = parser_new_node (parser, PT_FUNCTION);
      tmp->type_enum = PT_TYPE_MIDXKEY;
      tmp->info.function.function_type = F_MIDXKEY;
      tmp->info.function.arg_list = midx_key;
      regu_var = pt_to_regu_variable (parser, tmp, UNBOX_AS_VALUE);
      parser_free_tree (parser, tmp);
      midx_key = NULL;		/* already free */
    }

  /* set KEY_INFO structure */
  key_infop->key_cnt = 1;	/* single range */
  key_infop->key_ranges = regu_keyrange_array_alloc (1);
  if (!key_infop->key_ranges)
    {
      goto error;
    }
  key_infop->key_ranges[0].range = EQ_NA;
  key_infop->key_ranges[0].key1 = regu_var;
  key_infop->key_ranges[0].key2 = NULL;

  return 0;

/* error handling */
error:
  if (midx_key)
    {
      parser_free_tree (parser, midx_key);
    }

  return -1;
}


/*
 * pt_to_range_key () - Create an key information(KEY_INFO) in INDX_INFO
 *      structure for index scan with range spec of R_RANGE.
 *   return: 0 on success
 *   parser(in):
 *   term_exprs(in):
 *   nterms(in):
 *   multi_col(in):
 *   key_infop(out): Construct two key values
 */
static int
pt_to_range_key (PARSER_CONTEXT * parser,
		 PT_NODE ** term_exprs, int nterms, bool multi_col,
		 KEY_INFO * key_infop)
{
  PT_NODE *lhs, *rhs, *llim, *ulim, *tmp, *midxkey1, *midxkey2;
  PT_OP_TYPE op_type = (PT_OP_TYPE) 0;
  REGU_VARIABLE *regu_var1, *regu_var2;
  int i;

  midxkey1 = midxkey2 = NULL;
  regu_var1 = regu_var2 = NULL;
  key_infop->key_cnt = 0;
  key_infop->key_ranges = NULL;
  key_infop->is_constant = 1;

  for (i = 0; i < nterms; i++)
    {
      /* If nterms > 1, then it should be multi-column index and
         all term_exprs[0 .. nterms - 1] are equality expression.
         (Even though nterms == 1, it can be multi-column index.) */

      /* op type, LHS side and RHS side of this term expression */
      op_type = term_exprs[i]->info.expr.op;
      lhs = term_exprs[i]->info.expr.arg1;
      rhs = term_exprs[i]->info.expr.arg2;

      if (op_type != PT_BETWEEN)
	{
	  /* PT_EQ, PT_LT, PT_LE, PT_GT, or PT_GE */

	  /* make sure the key value(RHS) can actually be compared against the
	     index attribute(LHS) */
	  if (pt_coerce_value (parser, rhs, rhs,
			       lhs->type_enum, lhs->data_type))
	    {
	      goto error;
	    }

	  regu_var1 = pt_to_regu_variable (parser, rhs, UNBOX_AS_VALUE);
	  if (regu_var1 == NULL)
	    {
	      goto error;
	    }
	  if (!VALIDATE_REGU_KEY (regu_var1))
	    {
	      /* correlared join index case swap LHS and RHS */
	      tmp = rhs;
	      rhs = lhs;
	      lhs = tmp;

	      /* make sure the key value(RHS) can actually be compared against the
	         index attribute(LHS) */
	      if (pt_coerce_value (parser, rhs, rhs,
				   lhs->type_enum, lhs->data_type))
		{
		  goto error;
		}

	      /* try on RHS */
	      regu_var1 = pt_to_regu_variable (parser, rhs, UNBOX_AS_VALUE);
	      if (regu_var1 == NULL || !VALIDATE_REGU_KEY (regu_var1))
		{
		  goto error;
		}
	      /* converse op type for the case of PT_LE, ... */
	      op_type = pt_converse_op (op_type);
	    }
	  /* according to the 'op_type', adjust 'regu_var1' and 'regu_var2' */
	  if (op_type == PT_LT || op_type == PT_LE)
	    {
	      /* but, 'regu_var1' and 'regu_var2' will be replaced with
	         sequence values if it is multi-column index */
	      regu_var2 = regu_var1;
	      regu_var1 = NULL;
	    }
	  else
	    {
	      regu_var2 = NULL;
	    }

	  /* is the key value constant(value or host variable)? */
	  key_infop->is_constant &= (rhs->node_type == PT_VALUE ||
				     rhs->node_type == PT_HOST_VAR);

	  /* if it is multi-column index, make one PT_NODE for sequence key
	     value by concatenating all RHS of the terms */
	  if (multi_col)
	    {
	      if (op_type == PT_EQ || op_type == PT_GT || op_type == PT_GE)
		midxkey1 =
		  parser_append_node (pt_point (parser, rhs), midxkey1);
	      if (op_type == PT_EQ || op_type == PT_LT || op_type == PT_LE)
		midxkey2 =
		  parser_append_node (pt_point (parser, rhs), midxkey2);
	    }
	}
      else
	{			/* if (op_type != PT_BETWEEN) */
	  /* PT_BETWEEN */
	  op_type = rhs->info.expr.op;

	  /* range spec(lower limit and upper limit) from operands of BETWEEN
	     expression */
	  llim = rhs->info.expr.arg1;
	  ulim = rhs->info.expr.arg2;

	  /* make sure the key values(both limits) can actually be compared
	     against the index attribute(LHS) */
	  if (pt_coerce_value (parser, llim, llim,
			       lhs->type_enum, lhs->data_type) ||
	      pt_coerce_value (parser, ulim, ulim,
			       lhs->type_enum, lhs->data_type))
	    {
	      goto error;
	    }

	  regu_var1 = pt_to_regu_variable (parser, llim, UNBOX_AS_VALUE);
	  regu_var2 = pt_to_regu_variable (parser, ulim, UNBOX_AS_VALUE);
	  if (regu_var1 == NULL || !VALIDATE_REGU_KEY (regu_var1) ||
	      regu_var2 == NULL || !VALIDATE_REGU_KEY (regu_var2))
	    {
	      goto error;
	    }

	  /* is the key value constant(value or host variable)? */
	  key_infop->is_constant &= (llim->node_type == PT_VALUE ||
				     llim->node_type == PT_HOST_VAR) &&
	    (ulim->node_type == PT_VALUE || ulim->node_type == PT_HOST_VAR);

	  /* if it is multi-column index, make one PT_NODE for sequence key
	     value by concatenating all RHS of the terms */
	  if (multi_col)
	    {
	      midxkey1 =
		parser_append_node (pt_point (parser, llim), midxkey1);
	      midxkey2 =
		parser_append_node (pt_point (parser, ulim), midxkey2);
	    }

	}			/* if (op_type != PT_BETWEEN) */
    }				/* for (i = 0; i < nterms; i++) */

  if (midxkey1)
    {
      /* make a midxkey regu variable for multi-column index */
      tmp = parser_new_node (parser, PT_FUNCTION);
      tmp->type_enum = PT_TYPE_MIDXKEY;
      tmp->info.function.function_type = F_MIDXKEY;
      tmp->info.function.arg_list = midxkey1;
      regu_var1 = pt_to_regu_variable (parser, tmp, UNBOX_AS_VALUE);
      parser_free_tree (parser, tmp);
      midxkey1 = NULL;		/* already free */
    }
  if (midxkey2)
    {
      /* make a midxkey regu variable for multi-column index */
      tmp = parser_new_node (parser, PT_FUNCTION);
      tmp->type_enum = PT_TYPE_MIDXKEY;
      tmp->info.function.function_type = F_MIDXKEY;
      tmp->info.function.arg_list = midxkey2;
      regu_var2 = pt_to_regu_variable (parser, tmp, UNBOX_AS_VALUE);
      parser_free_tree (parser, tmp);
      midxkey2 = NULL;		/* already free */
    }


  /* set KEY_INFO structure */
  key_infop->key_cnt = 1;	/* single range */
  key_infop->key_ranges = regu_keyrange_array_alloc (1);
  if (!key_infop->key_ranges)
    {
      goto error;
    }
  key_infop->key_ranges[0].range = op_type_to_range (op_type, nterms);
  key_infop->key_ranges[0].key1 = regu_var1;
  key_infop->key_ranges[0].key2 = regu_var2;

  return 0;

/* error handling */
error:

  if (midxkey1)
    {
      parser_free_tree (parser, midxkey1);
    }
  if (midxkey2)
    {
      parser_free_tree (parser, midxkey2);
    }

  return -1;
}


/*
 * pt_to_list_key () - Create an key information(KEY_INFO) in INDX_INFO
 * 	structure for index scan with range spec of R_LIST
 *   return: 0 on success
 *   parser(in):
 *   term_exprs(in):
 *   nterms(in):
 *   multi_col(in):
 *   key_infop(out): Construct a list of key values
 */
static int
pt_to_list_key (PARSER_CONTEXT * parser,
		PT_NODE ** term_exprs, int nterms, bool multi_col,
		KEY_INFO * key_infop)
{
  PT_NODE *lhs, *rhs, *elem, *tmp, **midxkey_list;
  PT_OP_TYPE op_type;
  REGU_VARIABLE **regu_var_list, *regu_var;
  int i, j, n_elem;
  DB_VALUE db_value, *p;
  DB_COLLECTION *db_collectionp = NULL;

  midxkey_list = NULL;
  regu_var_list = NULL;
  key_infop->key_cnt = 0;
  key_infop->key_ranges = NULL;
  key_infop->is_constant = 1;
  n_elem = 0;

  /* get number of elements of the IN predicate */
  rhs = term_exprs[nterms - 1]->info.expr.arg2;
  switch (rhs->node_type)
    {
    case PT_FUNCTION:
      switch (rhs->info.function.function_type)
	{
	case F_SET:
	case F_MULTISET:
	case F_SEQUENCE:
	  break;
	default:
	  goto error;
	}
      for (elem = rhs->info.function.arg_list, n_elem = 0; elem;
	   elem = elem->next, n_elem++)
	{
	  ;
	}
      break;
    case PT_NAME:
      if (rhs->info.name.meta_class != PT_PARAMETER)
	{
	  goto error;
	}
      /* fall through into next case PT_VALUE */
    case PT_VALUE:
      p = (rhs->node_type == PT_NAME)
	? pt_find_value_of_label (rhs->info.name.original)
	: &rhs->info.value.db_value;
      switch (DB_VALUE_TYPE (p))
	{
	case DB_TYPE_MULTISET:
	case DB_TYPE_SET:
	case DB_TYPE_SEQUENCE:
	  break;
	default:
	  goto error;
	}
      db_collectionp = db_get_collection (p);
      n_elem = db_col_size (db_collectionp);
      break;
    case PT_HOST_VAR:
      p = pt_value_to_db (parser, rhs);
      switch (DB_VALUE_TYPE (p))
	{
	case DB_TYPE_MULTISET:
	case DB_TYPE_SET:
	case DB_TYPE_SEQUENCE:
	  break;
	default:
	  goto error;
	}
      db_collectionp = db_get_collection (p);
      n_elem = db_col_size (db_collectionp);
      break;
    default:
      goto error;
    }
  if (n_elem <= 0)
    {
      goto error;
    }

  /* allocate regu variable list and sequence value list */
  regu_var_list = regu_varptr_array_alloc (n_elem);
  if (!regu_var_list)
    {
      goto error;
    }

  if (multi_col)
    {
      midxkey_list = (PT_NODE **) malloc (sizeof (PT_NODE *) * n_elem);
      if (!midxkey_list)
	{
	  goto error;
	}
      memset (midxkey_list, 0, sizeof (PT_NODE *) * n_elem);
    }

  for (i = 0; i < nterms; i++)
    {
      /* If nterms > 1, then it should be multi-column index and
         all term_exprs[0 .. nterms - 1] are equality expression.
         (Even though nterms == 1, it can be multi-column index.) */

      /* op type, LHS side and RHS side of this term expression */
      op_type = term_exprs[i]->info.expr.op;
      lhs = term_exprs[i]->info.expr.arg1;
      rhs = term_exprs[i]->info.expr.arg2;

      if (op_type != PT_IS_IN && op_type != PT_EQ_SOME)
	{
	  /* PT_EQ */

	  /* make sure the key value(RHS) can actually be compared against the
	     index attribute(LHS) */
	  if (pt_coerce_value (parser, rhs, rhs,
			       lhs->type_enum, lhs->data_type))
	    {
	      goto error;
	    }

	  regu_var = pt_to_regu_variable (parser, rhs, UNBOX_AS_VALUE);
	  if (regu_var == NULL)
	    {
	      goto error;
	    }
	  if (!VALIDATE_REGU_KEY (regu_var))
	    {
	      /* correlared join index case swap LHS and RHS */
	      tmp = rhs;
	      rhs = lhs;
	      lhs = tmp;

	      /* make sure the key value(RHS) can actually be compared against the
	         index attribute(LHS) */
	      if (pt_coerce_value (parser, rhs, rhs,
				   lhs->type_enum, lhs->data_type))
		{
		  goto error;
		}

	      /* try on RHS */
	      regu_var = pt_to_regu_variable (parser, rhs, UNBOX_AS_VALUE);
	      if (regu_var == NULL || !VALIDATE_REGU_KEY (regu_var))
		{
		  goto error;
		}
	    }

	  /* is the key value constant(value or host variable)? */
	  key_infop->is_constant &= (rhs->node_type == PT_VALUE ||
				     rhs->node_type == PT_HOST_VAR);

	  /* if it is multi-column index, make one PT_NODE for sequence key
	     value by concatenating all RHS of the terms */
	  if (multi_col)
	    {
	      for (j = 0; j < n_elem; j++)
		{
		  midxkey_list[j] =
		    parser_append_node (pt_point (parser, rhs),
					midxkey_list[j]);
		}
	    }

	}
      else
	{
	  /* PT_IS_IN or PT_EQ_SOME */

	  if (rhs->node_type == PT_FUNCTION)
	    {
	      /* PT_FUNCTION */

	      for (j = 0, elem = rhs->info.function.arg_list;
		   j < n_elem && elem; j++, elem = elem->next)
		{

		  /* make sure the key value(RHS) can actually be compared
		     against the index attribute(LHS) */
		  if (pt_coerce_value (parser, elem, elem,
				       lhs->type_enum, lhs->data_type))
		    {
		      goto error;
		    }

		  regu_var_list[j] = pt_to_regu_variable (parser, elem,
							  UNBOX_AS_VALUE);
		  if (regu_var_list[j] == NULL ||
		      !VALIDATE_REGU_KEY (regu_var_list[j]))
		    goto error;

		  /* is the key value constant(value or host variable)? */
		  key_infop->is_constant &= (elem->node_type == PT_VALUE ||
					     elem->node_type == PT_HOST_VAR);

		  /* if it is multi-column index, make one PT_NODE for
		     sequence key value by concatenating all RHS of the
		     terms */
		  if (multi_col)
		    {
		      midxkey_list[j] =
			parser_append_node (pt_point (parser, elem),
					    midxkey_list[j]);
		    }
		}		/* for (j = 0, = ...) */
	    }
	  else
	    {
	      /* PT_NAME or PT_VALUE */
	      for (j = 0; j < n_elem; j++)
		{
		  if (db_col_get (db_collectionp, j, &db_value) < 0)
		    goto error;
		  if ((elem = pt_dbval_to_value (parser, &db_value)) == NULL)
		    goto error;
		  pr_clear_value (&db_value);

		  /* make sure the key value(RHS) can actually be compared
		     against the index attribute(LHS) */
		  if (pt_coerce_value (parser, elem, elem,
				       lhs->type_enum, lhs->data_type))
		    {
		      parser_free_tree (parser, elem);
		      goto error;
		    }

		  regu_var_list[j] = pt_to_regu_variable (parser, elem,
							  UNBOX_AS_VALUE);
		  if (regu_var_list[j] == NULL ||
		      !VALIDATE_REGU_KEY (regu_var_list[j]))
		    {
		      parser_free_tree (parser, elem);
		      goto error;
		    }

		  /* if it is multi-column index, make one PT_NODE for
		     midxkey value by concatenating all RHS of the terms */
		  if (multi_col)
		    {
		      midxkey_list[j] =
			parser_append_node (elem, midxkey_list[j]);
		    }
		}		/* for (j = 0; ...) */
	    }			/* else (rhs->node_type == PT_FUNCTION) */
	}
    }				/* for (i = 0; i < nterms; i++) */

  if (multi_col)
    {
      /* make a midxkey regu variable for multi-column index */
      for (i = 0; i < n_elem; i++)
	{
	  if (!midxkey_list[i])
	    {
	      goto error;
	    }

	  tmp = parser_new_node (parser, PT_FUNCTION);
	  tmp->type_enum = PT_TYPE_MIDXKEY;
	  tmp->info.function.function_type = F_MIDXKEY;
	  tmp->info.function.arg_list = midxkey_list[i];
	  regu_var_list[i] = pt_to_regu_variable (parser, tmp,
						  UNBOX_AS_VALUE);
	  parser_free_tree (parser, tmp);
	  midxkey_list[i] = NULL;	/* already free */
	}
    }

  /* set KEY_INFO structure */
  key_infop->key_cnt = n_elem;	/* n_elem ranges */
  key_infop->key_ranges = regu_keyrange_array_alloc (n_elem);
  if (!key_infop->key_ranges)
    {
      goto error;
    }
  for (i = 0; i < n_elem; i++)
    {
      key_infop->key_ranges[i].range = EQ_NA;
      key_infop->key_ranges[i].key1 = regu_var_list[i];
      key_infop->key_ranges[i].key2 = NULL;
    }

  if (midxkey_list)
    {
      free_and_init (midxkey_list);
    }

  return 0;

/* error handling */
error:

  if (midxkey_list)
    {
      for (i = 0; i < n_elem; i++)
	{
	  if (midxkey_list[i])
	    {
	      parser_free_tree (parser, midxkey_list[i]);
	    }
	}
      free_and_init (midxkey_list);
    }

  return -1;
}


/*
 * pt_to_rangelist_key () - Create an key information(KEY_INFO) in INDX_INFO
 * 	structure for index scan with range spec of R_RANGELIST
 *   return:
 *   parser(in):
 *   term_exprs(in):
 *   nterms(in):
 *   multi_col(in):
 *   key_infop(out): Construct a list of search range values
 */
static int
pt_to_rangelist_key (PARSER_CONTEXT * parser,
		     PT_NODE ** term_exprs, int nterms,
		     bool multi_col, KEY_INFO * key_infop)
{
  PT_NODE *lhs, *rhs, *llim, *ulim, *elem, *tmp;
  PT_NODE **midxkey_list1, **midxkey_list2;
  PT_OP_TYPE op_type;
  REGU_VARIABLE **regu_var_list1, **regu_var_list2, *regu_var;
  RANGE *range_list = NULL;
  int i, j, n_elem;

  midxkey_list1 = midxkey_list2 = NULL;
  regu_var_list1 = regu_var_list2 = NULL;
  key_infop->key_cnt = 0;
  key_infop->key_ranges = NULL;
  key_infop->is_constant = 1;
  n_elem = 0;

  /* get number of elements of the RANGE predicate */
  rhs = term_exprs[nterms - 1]->info.expr.arg2;
  for (elem = rhs, n_elem = 0; elem; elem = elem->or_next, n_elem++)
    {
      ;
    }
  if (n_elem <= 0)
    {
      goto error;
    }

  /* allocate regu variable list and sequence value list */
  regu_var_list1 = regu_varptr_array_alloc (n_elem);
  regu_var_list2 = regu_varptr_array_alloc (n_elem);
  range_list = (RANGE *) malloc (sizeof (RANGE) * n_elem);
  if (!regu_var_list1 || !regu_var_list2 || !range_list)
    {
      goto error;
    }

  memset (range_list, 0, sizeof (RANGE) * n_elem);

  if (multi_col)
    {
      midxkey_list1 = (PT_NODE **) malloc (sizeof (PT_NODE) * n_elem);
      midxkey_list2 = (PT_NODE **) malloc (sizeof (PT_NODE) * n_elem);
      if (!midxkey_list1 || !midxkey_list2)
	{
	  goto error;
	}

      memset (midxkey_list1, 0, sizeof (PT_NODE) * n_elem);
      memset (midxkey_list2, 0, sizeof (PT_NODE) * n_elem);
    }

  /* for each term */
  for (i = 0; i < nterms; i++)
    {
      /* If nterms > 1, then it should be multi-column index and
         all term_expr[0 .. nterms - 1] are equality expression.
         (Even though nterms == 1, it can be multi-column index.) */

      /* op type, LHS side and RHS side of this term expression */
      op_type = term_exprs[i]->info.expr.op;
      lhs = term_exprs[i]->info.expr.arg1;
      rhs = term_exprs[i]->info.expr.arg2;

      if (op_type != PT_RANGE)
	{
	  /* PT_EQ */

	  /* make sure the key value(RHS) can actually be compared against the
	     index attribute(LHS) */
	  if (pt_coerce_value (parser, rhs, rhs,
			       lhs->type_enum, lhs->data_type))
	    {
	      goto error;
	    }

	  regu_var = pt_to_regu_variable (parser, rhs, UNBOX_AS_VALUE);
	  if (regu_var == NULL)
	    goto error;
	  if (!VALIDATE_REGU_KEY (regu_var))
	    {
	      /* correlared join index case swap LHS and RHS */
	      tmp = rhs;
	      rhs = lhs;
	      lhs = tmp;

	      /* make sure the key value(RHS) can actually be compared against the
	         index attribute(LHS) */
	      if (pt_coerce_value (parser, rhs, rhs,
				   lhs->type_enum, lhs->data_type))
		{
		  goto error;
		}

	      /* try on RHS */
	      regu_var = pt_to_regu_variable (parser, rhs, UNBOX_AS_VALUE);
	      if (regu_var == NULL || !VALIDATE_REGU_KEY (regu_var))
		goto error;
	    }

	  /* is the key value constant(value or host variable)? */
	  key_infop->is_constant &= (rhs->node_type == PT_VALUE ||
				     rhs->node_type == PT_HOST_VAR);

	  /* if it is multi-column index, make one PT_NODE for sequence key
	     value by concatenating all RHS of the terms */
	  if (multi_col)
	    {
	      for (j = 0; j < n_elem; j++)
		{
		  midxkey_list1[j] =
		    parser_append_node (pt_point (parser, rhs),
					midxkey_list1[j]);
		  midxkey_list2[j] =
		    parser_append_node (pt_point (parser, rhs),
					midxkey_list2[j]);
		}
	    }
	}
      else
	{
	  /* PT_RANGE */

	  for (j = 0, elem = rhs; j < n_elem && elem;
	       j++, elem = elem->or_next)
	    {
	      /* range type and spec(lower limit and upper limit) from
	         operands of RANGE expression */
	      op_type = elem->info.expr.op;
	      range_list[j] = op_type_to_range (op_type, nterms);
	      switch (op_type)
		{
		case PT_BETWEEN_EQ_NA:
		  llim = elem->info.expr.arg1;
		  ulim = llim;
		  break;
		case PT_BETWEEN_INF_LE:
		case PT_BETWEEN_INF_LT:
		  llim = NULL;
		  ulim = elem->info.expr.arg1;
		  break;
		case PT_BETWEEN_GE_INF:
		case PT_BETWEEN_GT_INF:
		  llim = elem->info.expr.arg1;
		  ulim = NULL;
		  break;
		default:
		  llim = elem->info.expr.arg1;
		  ulim = elem->info.expr.arg2;
		  break;
		}

	      if (llim)
		{
		  /* make sure the key value can actually be compared against
		     the index attributes */
		  if (pt_coerce_value (parser, llim, llim,
				       lhs->type_enum, lhs->data_type))
		    {
		      goto error;
		    }

		  regu_var_list1[j] = pt_to_regu_variable (parser, llim,
							   UNBOX_AS_VALUE);
		  if (regu_var_list1[j] == NULL ||
		      !VALIDATE_REGU_KEY (regu_var_list1[j]))
		    goto error;

		  /* is the key value constant(value or host variable)? */
		  key_infop->is_constant &= (llim->node_type == PT_VALUE ||
					     llim->node_type == PT_HOST_VAR);
		}
	      else
		{
		  regu_var_list1[j] = NULL;
		}		/* if (llim) */

	      if (ulim)
		{
		  /* make sure the key value can actually be compared against
		     the index attributes */
		  if (pt_coerce_value (parser, ulim, ulim,
				       lhs->type_enum, lhs->data_type))
		    {
		      goto error;
		    }

		  regu_var_list2[j] = pt_to_regu_variable (parser, ulim,
							   UNBOX_AS_VALUE);
		  if (regu_var_list2[j] == NULL ||
		      !VALIDATE_REGU_KEY (regu_var_list2[j]))
		    goto error;

		  /* is the key value constant(value or host variable)? */
		  key_infop->is_constant &= (ulim->node_type == PT_VALUE ||
					     ulim->node_type == PT_HOST_VAR);
		}
	      else
		{
		  regu_var_list2[j] = NULL;
		}		/* if (ulim) */

	      /* if it is multi-column index, make one PT_NODE for sequence
	         key value by concatenating all RHS of the terms */
	      if (multi_col)
		{
		  if (llim)
		    {
		      midxkey_list1[j] =
			parser_append_node (pt_point (parser, llim),
					    midxkey_list1[j]);
		    }
		  if (ulim)
		    {
		      midxkey_list2[j] =
			parser_append_node (pt_point (parser, ulim),
					    midxkey_list2[j]);
		    }
		}
	    }			/* for (j = 0, elem = rhs; ... ) */
	}			/* else (op_type != PT_RANGE) */
    }				/* for (i = 0; i < nterms; i++) */

  if (multi_col)
    {
      /* make a midxkey regu variable for multi-column index */
      for (i = 0; i < n_elem; i++)
	{
	  if (midxkey_list1[i])
	    {
	      tmp = parser_new_node (parser, PT_FUNCTION);
	      tmp->type_enum = PT_TYPE_MIDXKEY;
	      tmp->info.function.function_type = F_MIDXKEY;
	      tmp->info.function.arg_list = midxkey_list1[i];
	      regu_var_list1[i] = pt_to_regu_variable (parser, tmp,
						       UNBOX_AS_VALUE);
	      parser_free_tree (parser, tmp);
	      midxkey_list1[i] = NULL;	/* already free */
	    }
	}
      free_and_init (midxkey_list1);

      /* make a midxkey regu variable for multi-column index */
      for (i = 0; i < n_elem; i++)
	{
	  if (midxkey_list2[i])
	    {
	      tmp = parser_new_node (parser, PT_FUNCTION);
	      tmp->type_enum = PT_TYPE_MIDXKEY;
	      tmp->info.function.function_type = F_MIDXKEY;
	      tmp->info.function.arg_list = midxkey_list2[i];
	      regu_var_list2[i] = pt_to_regu_variable (parser, tmp,
						       UNBOX_AS_VALUE);
	      parser_free_tree (parser, tmp);
	      midxkey_list2[i] = NULL;	/* already free */
	    }
	}
      free_and_init (midxkey_list2);
    }


  /* set KEY_INFO structure */
  key_infop->key_cnt = n_elem;	/* n_elem ranges */
  key_infop->key_ranges = regu_keyrange_array_alloc (n_elem);
  if (!key_infop->key_ranges)
    {
      goto error;
    }
  for (i = 0; i < n_elem; i++)
    {
      key_infop->key_ranges[i].range = range_list[i];
      key_infop->key_ranges[i].key1 = regu_var_list1[i];
      key_infop->key_ranges[i].key2 = regu_var_list2[i];
    }

  if (range_list)
    {
      free_and_init (range_list);
    }

  return 0;

/* error handling */
error:

  if (midxkey_list1)
    {
      for (i = 0; i < n_elem; i++)
	{
	  if (midxkey_list1[i])
	    {
	      parser_free_tree (parser, midxkey_list1[i]);
	    }
	}
      free_and_init (midxkey_list1);
    }
  if (midxkey_list2)
    {
      for (i = 0; i < n_elem; i++)
	{
	  if (midxkey_list2[i])
	    {
	      parser_free_tree (parser, midxkey_list2[i]);
	    }
	}
      free_and_init (midxkey_list2);
    }

  if (range_list)
    {
      free_and_init (range_list);
    }

  return -1;
}


/*
 * pt_to_index_info () - Create an INDX_INFO structure for communication
 * 	to a class access spec for eventual incorporation into an index scan
 *   return:
 *   parser(in):
 *   class(in):
 *   qo_index_infop(in):
 */
static INDX_INFO *
pt_to_index_info (PARSER_CONTEXT * parser, DB_OBJECT * class_,
		  QO_XASL_INDEX_INFO * qo_index_infop)
{
  PT_NODE **term_exprs;
  int nterms;
  bool multi_col;
  BTID *btidp;
  PT_OP_TYPE op_type;
  INDX_INFO *indx_infop;
  int rc;

  assert (parser != NULL);

  /* get array of term expressions and number of them which are associated
     with this index */
  term_exprs = qo_xasl_get_terms (qo_index_infop);
  nterms = qo_xasl_get_num_terms (qo_index_infop);
  multi_col = qo_xasl_get_multi_col (class_, qo_index_infop);
  btidp = qo_xasl_get_btid (class_, qo_index_infop);
  if (!class_ || !term_exprs || nterms <= 0 || !btidp)
    {
      PT_INTERNAL_ERROR (parser, "index plan generation - invalid arg");
      return NULL;
    }

  /* The last term expression in the array(that is, [nterms - 1]) is
     interesting because the multi-column index scan depends on it. For
     multi-column index, the other terms except the last one should be
     equality expression. */
  op_type = term_exprs[nterms - 1]->info.expr.op;

  /* make INDX_INFO strucutre and fill it up using information in
     QO_XASL_INDEX_INFO structure */
  indx_infop = regu_index_alloc ();
  if (indx_infop == NULL)
    {
      PT_INTERNAL_ERROR (parser, "index plan generation - memory alloc");
      return NULL;
    }

  /* BTID */
  indx_infop->indx_id.type = T_BTID;
  indx_infop->indx_id.i.btid = *btidp;

  /* scan range spec and index key information */
  switch (op_type)
    {
    case PT_EQ:
      indx_infop->range_type = R_KEY;
      rc = pt_to_single_key (parser, term_exprs, nterms, multi_col,
			     &indx_infop->key_info);
      break;
    case PT_GT:
    case PT_GE:
    case PT_LT:
    case PT_LE:
    case PT_BETWEEN:
      indx_infop->range_type = R_RANGE;
      rc = pt_to_range_key (parser, term_exprs, nterms, multi_col,
			    &indx_infop->key_info);
      break;
    case PT_IS_IN:
    case PT_EQ_SOME:
      indx_infop->range_type = R_KEYLIST;
      rc = pt_to_list_key (parser, term_exprs, nterms, multi_col,
			   &indx_infop->key_info);
      break;
    case PT_RANGE:
      indx_infop->range_type = R_RANGELIST;
      rc = pt_to_rangelist_key (parser, term_exprs, nterms, multi_col,
				&indx_infop->key_info);
      break;
    default:
      /* the other operators are not applicable to index scan */
      rc = -1;
    }
  if (rc < 0)
    {
      PT_INTERNAL_ERROR (parser, "index plan generation - invalid key value");
      return NULL;
    }

  return indx_infop;
}



/*
 * pt_to_class_spec_list () - Convert a PT_NODE flat class list to
 *     an ACCESS_SPEC_LIST list of representing the classes to be selected from
 *   return:
 *   parser(in):
 *   spec(in):
 *   where_key_part(in):
 *   where_part(in):
 *   index_pred(in):
 */
static ACCESS_SPEC_TYPE *
pt_to_class_spec_list (PARSER_CONTEXT * parser, PT_NODE * spec,
		       PT_NODE * where_key_part, PT_NODE * where_part,
		       QO_XASL_INDEX_INFO * index_pred)
{
  SYMBOL_INFO *symbols;
  ACCESS_SPEC_TYPE *access;
  ACCESS_SPEC_TYPE *access_list = NULL;
  PT_NODE *flat;
  PT_NODE *class_;
  PRED_EXPR *where_key = NULL;
  REGU_VARIABLE_LIST regu_attributes_key;
  HEAP_CACHE_ATTRINFO *cache_key = NULL;
  PT_NODE *key_attrs = NULL;
  int *key_offsets = NULL;
  PRED_EXPR *where = NULL;
  REGU_VARIABLE_LIST regu_attributes_pred, regu_attributes_rest;
  TABLE_INFO *table_info;
  INDX_INFO *index_info;
  HEAP_CACHE_ATTRINFO *cache_pred = NULL, *cache_rest = NULL;
  PT_NODE *pred_attrs = NULL, *rest_attrs = NULL;
  int *pred_offsets = NULL, *rest_offsets = NULL;

  assert (parser != NULL);

  flat = spec->info.spec.flat_entity_list;
  if (spec == NULL || flat == NULL)
    return NULL;

  symbols = parser->symbols;
  table_info = pt_find_table_info (flat->info.name.spec_id,
				   symbols->table_info);

  if (symbols && table_info)
    {
      /* Determine if this flat list is in a remote ldb or this db
       * This presumes we do not generate flat lists which contain
       * a mix of classes from different ldb's or the gdb.
       */
      for (class_ = flat; class_ != NULL; class_ = class_->next)
	{
	  /* The scans have changed to grab the val list before
	   * predicate evaluation since evaluation now does comparisons
	   * using DB_VALUES instead of disk rep.  Thus, the where
	   * predicate does NOT want to generate TYPE_ATTR_ID regu
	   * variables, but rather TYPE_CONSTANT regu variables.
	   * This is driven off the symbols->current class variable
	   * so we need to generate the where pred first.
	   */

	  if (index_pred == NULL)
	    {
	      TARGET_TYPE scan_type;
	      if (spec->info.spec.meta_class == PT_META_CLASS)
		scan_type = TARGET_CLASS_ATTR;
	      else
		scan_type = TARGET_CLASS;

	      if (!pt_split_attrs (parser, table_info, where_part,
				   &pred_attrs, &rest_attrs,
				   &pred_offsets, &rest_offsets))
		{
		  return NULL;
		}

	      cache_pred = regu_cache_attrinfo_alloc ();
	      cache_rest = regu_cache_attrinfo_alloc ();

	      /* see qp_eval.c:eval_data_filter() */
	      symbols->current_class = (scan_type == TARGET_CLASS_ATTR)
		? NULL : class_;
	      symbols->cache_attrinfo = cache_pred;

	      where = pt_to_pred_expr (parser, where_part);

	      if (scan_type == TARGET_CLASS_ATTR)
		symbols->current_class = class_;

	      regu_attributes_pred =
		pt_to_regu_variable_list (parser, pred_attrs,
					  UNBOX_AS_VALUE,
					  table_info->value_list,
					  pred_offsets);

	      symbols->cache_attrinfo = cache_rest;

	      regu_attributes_rest =
		pt_to_regu_variable_list (parser, rest_attrs,
					  UNBOX_AS_VALUE,
					  table_info->value_list,
					  rest_offsets);

	      parser_free_tree (parser, pred_attrs);
	      parser_free_tree (parser, rest_attrs);
	      free_and_init (pred_offsets);
	      free_and_init (rest_offsets);

	      access =
		pt_make_class_access_spec (parser, flat,
					   class_->info.name.db_object,
					   scan_type, SEQUENTIAL,
					   spec->info.spec.lock_hint,
					   NULL, NULL, where, NULL,
					   regu_attributes_pred,
					   regu_attributes_rest,
					   NULL, cache_pred, cache_rest);
	    }
	  else
	    {
	      if (!pt_to_index_attrs (parser, table_info, index_pred,
				      where_key_part, &key_attrs,
				      &key_offsets))
		{
		  return NULL;
		}
	      if (!pt_split_attrs (parser, table_info, where_part,
				   &pred_attrs, &rest_attrs,
				   &pred_offsets, &rest_offsets))
		{
		  return NULL;
		}

	      cache_key = regu_cache_attrinfo_alloc ();
	      cache_pred = regu_cache_attrinfo_alloc ();
	      cache_rest = regu_cache_attrinfo_alloc ();

	      symbols->current_class = class_;
	      symbols->cache_attrinfo = cache_key;

	      where_key = pt_to_pred_expr (parser, where_key_part);

	      regu_attributes_key =
		pt_to_regu_variable_list (parser, key_attrs,
					  UNBOX_AS_VALUE,
					  table_info->value_list,
					  key_offsets);

	      symbols->cache_attrinfo = cache_pred;

	      where = pt_to_pred_expr (parser, where_part);

	      regu_attributes_pred =
		pt_to_regu_variable_list (parser, pred_attrs,
					  UNBOX_AS_VALUE,
					  table_info->value_list,
					  pred_offsets);

	      symbols->cache_attrinfo = cache_rest;

	      regu_attributes_rest =
		pt_to_regu_variable_list (parser, rest_attrs,
					  UNBOX_AS_VALUE,
					  table_info->value_list,
					  rest_offsets);

	      parser_free_tree (parser, key_attrs);
	      parser_free_tree (parser, pred_attrs);
	      parser_free_tree (parser, rest_attrs);
	      free_and_init (key_offsets);
	      free_and_init (pred_offsets);
	      free_and_init (rest_offsets);

	      /*
	       * pt_make_class_spec() will return NULL if passed a
	       * NULL INDX_INFO *, so there isn't any need to check
	       * return values here.
	       */
	      index_info = pt_to_index_info (parser,
					     class_->info.name.db_object,
					     index_pred);
	      access =
		pt_make_class_access_spec (parser, flat,
					   class_->info.name.db_object,
					   TARGET_CLASS, INDEX,
					   spec->info.spec.lock_hint,
					   index_info, where_key, where,
					   regu_attributes_key,
					   regu_attributes_pred,
					   regu_attributes_rest, cache_key,
					   cache_pred, cache_rest);
	    }			/* else (index_pred == NULL */

	  if (!access
	      || (!regu_attributes_pred &&
		  !regu_attributes_rest && table_info->attribute_list)
	      || parser->error_msgs)
	    {
	      /* an error condition */
	      access = NULL;
	    }

	  if (access)
	    {
	      access->next = access_list;
	      access_list = access;
	    }
	  else
	    {
	      /* an error condition */
	      access_list = NULL;
	      break;
	    }
	}

      symbols->current_class = NULL;
      symbols->cache_attrinfo = NULL;

    }

  return access_list;
}


/*
 * pt_to_subquery_table_spec_list () - Convert a QUERY PT_NODE
 * 	an ACCESS_SPEC_LIST list for its list file
 *   return:
 *   parser(in):
 *   spec(in):
 *   subquery(in):
 *   where_part(in):
 */
static ACCESS_SPEC_TYPE *
pt_to_subquery_table_spec_list (PARSER_CONTEXT * parser,
				PT_NODE * spec,
				PT_NODE * subquery, PT_NODE * where_part)
{
  XASL_NODE *subquery_proc;
  PT_NODE *tmp;
  REGU_VARIABLE_LIST regu_attributes_pred, regu_attributes_rest;
  ACCESS_SPEC_TYPE *access;
  PRED_EXPR *where = NULL;
  TABLE_INFO *tbl_info;
  PT_NODE *pred_attrs = NULL, *rest_attrs = NULL;
  int *pred_offsets = NULL, *rest_offsets = NULL;

  subquery_proc = (XASL_NODE *) subquery->info.query.xasl;

  tbl_info = pt_find_table_info (spec->info.spec.id,
				 parser->symbols->table_info);

  if (!pt_split_attrs (parser, tbl_info, where_part,
		       &pred_attrs, &rest_attrs,
		       &pred_offsets, &rest_offsets))
    {
      return NULL;
    }

  /* This generates a list of TYPE_POSITION regu_variables
   * There information is stored in a QFILE_TUPLE_VALUE_POSITION, which
   * describes a type and index into a list file.
   */
  regu_attributes_pred =
    pt_to_position_regu_variable_list (parser,
				       pred_attrs,
				       tbl_info->value_list, pred_offsets);
  regu_attributes_rest =
    pt_to_position_regu_variable_list (parser,
				       rest_attrs,
				       tbl_info->value_list, rest_offsets);

  parser_free_tree (parser, pred_attrs);
  parser_free_tree (parser, rest_attrs);
  free_and_init (pred_offsets);
  free_and_init (rest_offsets);

  parser->symbols->listfile_unbox = UNBOX_AS_VALUE;
  parser->symbols->current_listfile = NULL;

  /* The where predicate is now evaluated after the val list has been
   * fetched.  This means that we want to generate "CONSTANT" regu
   * variables instead of "POSITION" regu variables which would happen
   * if parser->symbols->current_listfile != NULL.
   * pred should never user the current instance for fetches
   * either, so we turn off the current_class, if there is one.
   */
  tmp = parser->symbols->current_class;
  parser->symbols->current_class = NULL;
  where = pt_to_pred_expr (parser, where_part);
  parser->symbols->current_class = tmp;

  access = pt_make_list_access_spec (subquery_proc, SEQUENTIAL,
				     NULL, where,
				     regu_attributes_pred,
				     regu_attributes_rest);

  if (access && subquery_proc
      && (regu_attributes_pred || regu_attributes_rest
	  || !spec->info.spec.as_attr_list))
    {
      return access;
    }

  return NULL;
}


/*
 * pt_to_set_expr_table_spec_list () - Convert a PT_NODE flat class list
 * 	to an ACCESS_SPEC_LIST list of representing the classes
 * 	to be selected from
 *   return:
 *   parser(in):
 *   spec(in):
 *   set_expr(in):
 *   where_part(in):
 */
static ACCESS_SPEC_TYPE *
pt_to_set_expr_table_spec_list (PARSER_CONTEXT * parser,
				PT_NODE * spec,
				PT_NODE * set_expr, PT_NODE * where_part)
{
  REGU_VARIABLE_LIST regu_attributes;
  REGU_VARIABLE *regu_set_expr;
  PRED_EXPR *where = NULL;

  ACCESS_SPEC_TYPE *access;

  regu_set_expr = pt_to_regu_variable (parser, set_expr, UNBOX_AS_VALUE);

  /* This generates a list of TYPE_POSITION regu_variables
   * There information is stored in a QFILE_TUPLE_VALUE_POSITION, which
   * describes a type and index into a list file.
   */
  regu_attributes =
    pt_to_position_regu_variable_list (parser,
				       spec->info.spec.as_attr_list,
				       NULL, NULL);

  where = pt_to_pred_expr (parser, where_part);

  access = pt_make_set_access_spec (regu_set_expr, SEQUENTIAL, NULL,
				    where, regu_attributes);

  if (access && regu_set_expr
      && (regu_attributes || !spec->info.spec.as_attr_list))
    {
      return access;
    }

  return NULL;
}


/*
 * pt_to_cselect_table_spec_list () - Convert a PT_NODE flat class list to
 *     an ACCESS_SPEC_LIST list of representing the classes to be selected from
 *   return:
 *   parser(in):
 *   spec(in):
 *   cselect(in):
 *   src_derived_tbl(in):
 */
static ACCESS_SPEC_TYPE *
pt_to_cselect_table_spec_list (PARSER_CONTEXT * parser, PT_NODE * spec,
			       PT_NODE * cselect, PT_NODE * src_derived_tbl)
{
  XASL_NODE *subquery_proc;
  REGU_VARIABLE_LIST regu_attributes;
  ACCESS_SPEC_TYPE *access;
  METHOD_SIG_LIST *method_sig_list;

  /* every cselect must have a subquery for its source list file,
   * this is pointed to by the methods of the cselect */
  if (!cselect
      || !(cselect->node_type == PT_METHOD_CALL)
      || !src_derived_tbl || !src_derived_tbl->info.spec.derived_table)
    {
      return NULL;
    }

  subquery_proc =
    (XASL_NODE *) src_derived_tbl->info.spec.derived_table->info.query.xasl;

  method_sig_list = pt_to_method_sig_list (parser, cselect,
					   src_derived_tbl->info.spec.
					   as_attr_list);

  /* This generates a list of TYPE_POSITION regu_variables
   * There information is stored in a QFILE_TUPLE_VALUE_POSITION, which
   * describes a type and index into a list file.
   */

  regu_attributes =
    pt_to_position_regu_variable_list (parser,
				       spec->info.spec.as_attr_list,
				       NULL, NULL);

  access = pt_make_cselect_access_spec (subquery_proc, method_sig_list,
					SEQUENTIAL, NULL, NULL,
					regu_attributes);

  if (access && subquery_proc && method_sig_list
      && (regu_attributes || !spec->info.spec.as_attr_list))
    {
      return access;
    }

  return NULL;
}


/*
 * pt_to_spec_list () - Convert a PT_NODE spec to an ACCESS_SPEC_LIST list of
 *      representing the classes to be selected from
 *   return:
 *   parser(in):
 *   spec(in):
 *   where_key_part(in):
 *   where_part(in):
 *   index_part(in):
 *   src_derived_tbl(in):
 */
ACCESS_SPEC_TYPE *
pt_to_spec_list (PARSER_CONTEXT * parser, PT_NODE * spec,
		 PT_NODE * where_key_part, PT_NODE * where_part,
		 QO_XASL_INDEX_INFO * index_part, PT_NODE * src_derived_tbl)
{
  ACCESS_SPEC_TYPE *access = NULL;

  if (spec->info.spec.flat_entity_list)
    {
      access = pt_to_class_spec_list (parser, spec,
				      where_key_part, where_part, index_part);
    }
  else
    {
      /* derived table
         index_part better be NULL here! */
      if (spec->info.spec.derived_table_type == PT_IS_SUBQUERY)
	{
	  access = pt_to_subquery_table_spec_list
	    (parser, spec, spec->info.spec.derived_table, where_part);
	}
      else if (spec->info.spec.derived_table_type == PT_IS_SET_EXPR)
	{
	  /* a set expression derived table */
	  access = pt_to_set_expr_table_spec_list
	    (parser, spec, spec->info.spec.derived_table, where_part);
	}
      else
	{
	  /* a CSELECT derived table */
	  access = pt_to_cselect_table_spec_list
	    (parser, spec, spec->info.spec.derived_table, src_derived_tbl);
	}
    }

  return access;
}


/*
 * pt_to_val_list () -
 *   return: val_list corresponding to the entity spec
 *   parser(in):
 *   id(in):
 */
VAL_LIST *
pt_to_val_list (PARSER_CONTEXT * parser, UINTPTR id)
{
  SYMBOL_INFO *symbols;
  VAL_LIST *val_list = NULL;
  TABLE_INFO *table_info;

  if (parser)
    {
      symbols = parser->symbols;
      table_info = pt_find_table_info (id, symbols->table_info);

      if (table_info)
	{
	  val_list = table_info->value_list;
	}
    }

  return val_list;
}


/*
 * pt_find_xasl () - appends the from list to the end of the to list
 *   return:
 *   list(in):
 *   match(in):
 */
static XASL_NODE *
pt_find_xasl (XASL_NODE * list, XASL_NODE * match)
{
  XASL_NODE *xasl = list;

  while (xasl && xasl != match)
    {
      xasl = xasl->next;
    }

  return xasl;
}


/*
 * pt_append_xasl () - appends the from list to the end of the to list
 *   return:
 *   to(in):
 *   from_list(in):
 */
XASL_NODE *
pt_append_xasl (XASL_NODE * to, XASL_NODE * from_list)
{
  XASL_NODE *xasl = to;
  XASL_NODE *next;
  XASL_NODE *from = from_list;

  if (!xasl)
    {
      return from_list;
    }

  while (xasl->next)
    {
      xasl = xasl->next;
    }

  while (from)
    {
      next = from->next;

      if (pt_find_xasl (to, from))
	{
	  /* already on list, do nothing
	   * necessarily, the rest of the nodes are on the list,
	   * since they are linked to from.
	   */
	  from = NULL;
	}
      else
	{
	  xasl->next = from;
	  xasl = from;
	  from->next = NULL;
	  from = next;
	}
    }

  return to;
}


/*
 * pt_remove_xasl () - removes an xasl node from an xasl list
 *   return:
 *   xasl_list(in):
 *   remove(in):
 */
XASL_NODE *
pt_remove_xasl (XASL_NODE * xasl_list, XASL_NODE * remove)
{
  XASL_NODE *list = xasl_list;

  if (!list)
    {
      return list;
    }

  if (list == remove)
    {
      xasl_list = remove->next;
      remove->next = NULL;
    }
  else
    {
      while (list->next && list->next != remove)
	{
	  list = list->next;
	}

      if (list->next == remove)
	{
	  list->next = remove->next;
	  remove->next = NULL;
	}
    }

  return xasl_list;
}



/*
 * pt_set_dptr () - If this xasl node should have a dptr list from
 * 	"correlated == 1" queries, they will be set
 *   return:
 *   parser(in):
 *   node(in):
 *   xasl(in):
 *   id(in):
 */
void
pt_set_dptr (PARSER_CONTEXT * parser, PT_NODE * node, XASL_NODE * xasl,
	     UINTPTR id)
{
  if (xasl)
    {
      xasl->dptr_list =
	pt_remove_xasl (pt_append_xasl (xasl->dptr_list,
					pt_to_corr_subquery_list
					(parser, node, id)), xasl);
    }
}


/*
 * pt_set_aptr () - If this xasl node should have an aptr list from
 * 	"correlated > 1" queries, they will be set
 *   return:
 *   parser(in):
 *   select_node(in):
 *   xasl(in):
 */
static void
pt_set_aptr (PARSER_CONTEXT * parser, PT_NODE * select_node, XASL_NODE * xasl)
{
  if (xasl)
    {
      xasl->aptr_list = pt_remove_xasl (pt_append_xasl
					(xasl->aptr_list,
					 pt_to_uncorr_subquery_list
					 (parser, select_node)), xasl);
    }
}


/*
 * pt_append_scan () - appends the from list to the end of the to list
 *   return:
 *   to(in):
 *   from(in):
 */
static XASL_NODE *
pt_append_scan (const XASL_NODE * to, const XASL_NODE * from)
{
  XASL_NODE *xasl = (XASL_NODE *) to;

  if (!xasl)
    {
      return (XASL_NODE *) from;
    }

  while (xasl->scan_ptr)
    {
      xasl = xasl->scan_ptr;
    }
  xasl->scan_ptr = (XASL_NODE *) from;

  return (XASL_NODE *) to;
}



/*
 * pt_uncorr_pre () - builds xasl list of locally correlated (level 1) queries
 *   return:
 *   parser(in):
 *   node(in):
 *   arg(in/out):
 *   continue_walk(in/out):
 */
static PT_NODE *
pt_uncorr_pre (PARSER_CONTEXT * parser, PT_NODE * node,
	       void *arg, int *continue_walk)
{
  UNCORR_INFO *info = (UNCORR_INFO *) arg;

  *continue_walk = PT_CONTINUE_WALK;

  if (!PT_IS_QUERY_NODE_TYPE (node->node_type))
    {
      return node;
    }

  /* Can not increment level for list portion of walk.
   * Since those queries are not sub-queries of this query.
   * Consequently, we recurse seperately for the list leading
   * from a query.  Can't just call pt_to_uncorr_subquery_list()
   * directly since it needs to do a leaf walk and we want to do a full
   * walk on the next list.
   */
  if (node->next)
    {
      node->next = parser_walk_tree (parser, node->next, pt_uncorr_pre, info,
				     pt_uncorr_post, info);
    }

  *continue_walk = PT_LEAF_WALK;

  /* increment level as we dive into subqueries */
  info->level++;

  return node;
}


/*
 * pt_uncorr_post () - decrement level of correlation after passing selects
 *   return:
 *   parser(in):
 *   node(in):
 *   arg(in/out):
 *   continue_walk(in/out):
 */
static PT_NODE *
pt_uncorr_post (PARSER_CONTEXT * parser, PT_NODE * node,
		void *arg, int *continue_walk)
{
  UNCORR_INFO *info = (UNCORR_INFO *) arg;
  XASL_NODE *xasl;

  switch (node->node_type)
    {
    case PT_SELECT:
    case PT_UNION:
    case PT_DIFFERENCE:
    case PT_INTERSECTION:
      info->level--;
      xasl = (XASL_NODE *) node->info.query.xasl;

      if (xasl && pt_is_subquery (node))
	{
	  if (node->info.query.correlation_level == 0)
	    {
	      /* add to this level */
	      node->info.query.correlation_level = info->level;
	    }

	  if (node->info.query.correlation_level == info->level)
	    {
	      /* order is important. we are on the way up, so putting things
	       * at the tail of the list will end up deeper nested queries
	       * being first, which is required.
	       */
	      info->xasl = pt_append_xasl (info->xasl, xasl);
	    }
	}

    default:
      break;
    }

  return node;
}


/*
 * pt_to_uncorr_subquery_list () - Gather the correlated level > 1 subqueries
 * 	include nested queries, such that nest level + 2 = correlation level
 *	exclude the node being passed in
 *   return:
 *   parser(in):
 *   node(in):
 */
static XASL_NODE *
pt_to_uncorr_subquery_list (PARSER_CONTEXT * parser, PT_NODE * node)
{
  UNCORR_INFO info;

  info.xasl = NULL;
  info.level = 2;

  node = parser_walk_leaves (parser, node, pt_uncorr_pre, &info,
			     pt_uncorr_post, &info);

  return info.xasl;
}


/*
 * pt_corr_pre () - builds xasl list of locally correlated (level 1) queries
 * 	directly reachable. (no nested queries, which are already handled)
 *   return:
 *   parser(in):
 *   node(in):
 *   arg(in):
 *   continue_walk(in):
 */
static PT_NODE *
pt_corr_pre (PARSER_CONTEXT * parser, PT_NODE * node,
	     void *arg, int *continue_walk)
{
  XASL_NODE *xasl;
  CORR_INFO *info = (CORR_INFO *) arg;

  *continue_walk = PT_CONTINUE_WALK;

  switch (node->node_type)
    {
    case PT_SELECT:
    case PT_UNION:
    case PT_DIFFERENCE:
    case PT_INTERSECTION:
      *continue_walk = PT_LIST_WALK;
      xasl = (XASL_NODE *) node->info.query.xasl;

      if (xasl
	  && node->info.query.correlation_level == 1
	  && (info->id == MATCH_ALL || node->spec_ident == info->id))
	{
	  info->xasl_head = pt_append_xasl (xasl, info->xasl_head);
	}

    default:
      break;
    }

  return node;
}



/*
 * pt_to_corr_subquery_list () - Gather the correlated level == 1 subqueries.
 *	exclude nested queries. including the node being passed in
 *   return:
 *   parser(in):
 *   node(in):
 *   id(in):
 */
static XASL_NODE *
pt_to_corr_subquery_list (PARSER_CONTEXT * parser, PT_NODE * node, UINTPTR id)
{
  CORR_INFO info;

  info.xasl_head = NULL;
  info.id = id;

  node =
    parser_walk_tree (parser, node, pt_corr_pre, &info, pt_continue_walk,
		      NULL);

  return info.xasl_head;
}


/*
 * pt_link_regu_to_selupd_list () - Link update related regu list from outlist
 *                                  into selupd list of XASL tree
 *   return:
 *   parser(in):
 *   regulist(in):
 *   selupd_list(in):
 *   target_class(in):
 */
static SELUPD_LIST *
pt_link_regu_to_selupd_list (PARSER_CONTEXT * parser,
			     REGU_VARIABLE_LIST regulist,
			     SELUPD_LIST * selupd_list,
			     DB_OBJECT * target_class)
{
  SELUPD_LIST *node;
  REGU_VARLIST_LIST l_regulist;
  OID *oid_ptr;
  HFID *hfid_ptr;
  int is_partition = 0;

  oid_ptr = db_identifier (target_class);
  hfid_ptr = sm_get_heap (target_class);

  if (oid_ptr == NULL || hfid_ptr == NULL)
    {
      return NULL;
    }

  /* find a related info node for the target class */
  for (node = selupd_list; node != NULL; node = node->next)
    {
      if (OID_EQ (&node->class_oid, oid_ptr))
	break;
    }
  if (node == NULL)
    {
      if ((node = regu_selupd_list_alloc ()) == NULL)
	{
	  return NULL;
	}
      if (do_is_partitioned_classobj (&is_partition, target_class, NULL, NULL)
	  != NO_ERROR)
	{
	  return NULL;
	}
      if (is_partition == 1)
	{
	  /* if target class is a partitioned class,
	   * the class to access will be determimed
	   * at execution time. so do not set class oid and hfid */
	  OID_SET_NULL (&node->class_oid);
	  HFID_SET_NULL (&node->class_hfid);
	}
      else
	{
	  /* setup class info */
	  COPY_OID (&node->class_oid, oid_ptr);
	  HFID_COPY (&node->class_hfid, hfid_ptr);
	}

      /* insert the node into the selupd list */
      if (selupd_list == NULL)
	{
	  selupd_list = node;
	}
      else
	{
	  node->next = selupd_list;
	  selupd_list = node;
	}
    }

  l_regulist = regu_varlist_list_alloc ();
  if (l_regulist == NULL)
    {
      return NULL;
    }

  /* link the regulist of outlist to the node */
  l_regulist->list = regulist;

  /* add the regulist pointer to the current node */
  l_regulist->next = node->select_list;
  node->select_list = l_regulist;
  node->select_list_size++;

  return selupd_list;
}


/*
 * pt_to_outlist () - Convert a pt_node list to an outlist (of regu_variables)
 *   return:
 *   parser(in):
 *   node_list(in):
 *   selupd_list_ptr(in):
 *   unbox(in):
 */
static OUTPTR_LIST *
pt_to_outlist (PARSER_CONTEXT * parser, PT_NODE * node_list,
	       SELUPD_LIST ** selupd_list_ptr, UNBOX unbox)
{
  OUTPTR_LIST *outlist;
  PT_NODE *node, *node_next, *col;
  int count = 0;
  REGU_VARIABLE *regu;
  REGU_VARIABLE_LIST *regulist;
  PT_NODE *save_node, *save_next;
  XASL_NODE *xasl = NULL;
  QFILE_SORTED_LIST_ID *srlist_id;
  QPROC_DB_VALUE_LIST value_list = NULL;
  int i;

  outlist = regu_outlist_alloc ();
  if (outlist == NULL)
    {
      PT_ERRORm (parser, node_list, MSGCAT_SET_PARSER_SEMANTIC,
		 MSGCAT_SEMANTIC_OUT_OF_MEMORY);
      goto exit_on_error;
    }

  regulist = &outlist->valptrp;

  for (node = node_list, node_next = node ? node->next : NULL;
       node != NULL; node = node_next, node_next = node ? node->next : NULL)
    {
      save_node = node;		/* save */

      CAST_POINTER_TO_NODE (node);

      /* save and cut-off node link */
      save_next = node->next;
      node->next = NULL;

      /* get column list */
      col = node;
      if (PT_IS_QUERY_NODE_TYPE (node->node_type))
	{
	  xasl = (XASL_NODE *) node->info.query.xasl;
	  if (xasl == NULL)
	    {
	      goto exit_on_error;
	    }

	  xasl->is_single_tuple = unbox != UNBOX_AS_TABLE;
	  if (xasl->is_single_tuple)
	    {
	      col = pt_get_select_list (parser, node);
	      if (!xasl->single_tuple)
		{
		  xasl->single_tuple = pt_make_val_list (col);
		  if (xasl->single_tuple == NULL)
		    {
		      PT_ERRORm (parser, col, MSGCAT_SET_PARSER_SEMANTIC,
				 MSGCAT_SEMANTIC_OUT_OF_MEMORY);
		      goto exit_on_error;
		    }
		}

	      value_list = xasl->single_tuple->valp;
	    }
	}

      /* make outlist */
      for (i = 0; col; col = col->next, i++)
	{
	  *regulist = regu_varlist_alloc ();
	  if (*regulist == NULL)
	    {
	      goto exit_on_error;
	    }

	  if (PT_IS_QUERY_NODE_TYPE (node->node_type))
	    {
	      regu = regu_var_alloc ();
	      if (regu == NULL)
		{
		  goto exit_on_error;
		}

	      if (i == 0)
		{
		  /* set as linked to regu var */
		  XASL_SET_FLAG (xasl, XASL_LINK_TO_REGU_VARIABLE);
		  REGU_VARIABLE_XASL (regu) = xasl;
		}

	      if (xasl->is_single_tuple)
		{
		  regu->type = TYPE_CONSTANT;
		  regu->domain = pt_xasl_node_to_domain (parser, col);
		  regu->value.dbvalptr = value_list->val;
		  /* move to next db_value holder */
		  value_list = value_list->next;
		}
	      else
		{
		  srlist_id = regu_srlistid_alloc ();
		  if (srlist_id == NULL)
		    {
		      goto exit_on_error;
		    }

		  regu->type = TYPE_LIST_ID;
		  regu->value.srlist_id = srlist_id;
		  srlist_id->list_id = xasl->list_id;
		}
	    }
	  else if (col->node_type == PT_EXPR &&
		   col->info.expr.op == PT_ORDERBY_NUM)
	    {
	      regu = regu_var_alloc ();
	      if (regu == NULL)
		{
		  goto exit_on_error;
		}

	      regu->type = TYPE_ORDERBY_NUM;
	      regu->domain = pt_xasl_node_to_domain (parser, col);
	      regu->value.dbvalptr = (DB_VALUE *) col->etc;
	    }
	  else
	    {
	      regu = pt_to_regu_variable (parser, col, unbox);
	    }

	  if (regu == NULL)
	    {
	      goto exit_on_error;
	    }

	  /* append to outlist */
	  (*regulist)->value = *regu;

	  /* in case of increment expr, find a target class to do the expr,
	     and link the regulist to a node which contains update info
	     for the target class */
	  if (selupd_list_ptr != NULL && col->node_type == PT_EXPR &&
	      (col->info.expr.op == PT_INCR || col->info.expr.op == PT_DECR))
	    {
	      PT_NODE *upd_obj = col->info.expr.arg2;
	      PT_NODE *upd_dom = (upd_obj)
		? (upd_obj->node_type == PT_DOT_)
		? upd_obj->info.dot.arg2->data_type : upd_obj->
		data_type : NULL;
	      PT_NODE *upd_dom_nm;
	      DB_OBJECT *upd_dom_cls;
	      OID nulloid;

	      if (upd_obj == NULL || upd_obj->type_enum != PT_TYPE_OBJECT ||
		  (upd_dom
		   && upd_dom->info.data_type.virt_type_enum !=
		   PT_TYPE_OBJECT))
		{
		  goto exit_on_error;
		}

	      upd_dom_nm = upd_dom->info.data_type.entity;
	      upd_dom_cls = upd_dom_nm->info.name.db_object;

	      /* initialize result of regu expr */
	      OID_SET_NULL (&nulloid);
	      DB_MAKE_OID (regu->value.arithptr->value, &nulloid);

	      (*selupd_list_ptr) =
		pt_link_regu_to_selupd_list (parser,
					     *regulist,
					     (*selupd_list_ptr), upd_dom_cls);
	      if ((*selupd_list_ptr) == NULL)
		{
		  goto exit_on_error;
		}
	    }
	  regulist = &(*regulist)->next;

	  count++;
	}			/* for (i = 0; ...) */

      /* restore node link */
      if (node)
	{
	  node->next = save_next;
	}

      node = save_node;		/* restore */
    }

  outlist->valptr_cnt = count;

  return outlist;

exit_on_error:

  /* restore node link */
  if (node)
    {
      node->next = save_next;
    }

  node = save_node;		/* restore */

  return NULL;
}


/*
 * pt_to_fetch_as_scan_proc () - Translate a PT_NODE path entity spec to an
 *      a left outer scan proc on a list file from an xasl proc
 *   return:
 *   parser(in):
 *   spec(in):
 *   pred(in):
 *   join_term(in):
 *   xasl_to_scan(in):
 */
static XASL_NODE *
pt_to_fetch_as_scan_proc (PARSER_CONTEXT * parser, PT_NODE * spec,
			  PT_NODE * join_term, XASL_NODE * xasl_to_scan)
{
  XASL_NODE *xasl;
  PT_NODE *tmp;
  REGU_VARIABLE *regu;
  REGU_VARIABLE_LIST regu_attributes_pred, regu_attributes_rest;
  ACCESS_SPEC_TYPE *access;
  UNBOX unbox;
  TABLE_INFO *tbl_info;
  PRED_EXPR *where = NULL;
  PT_NODE *pred_attrs = NULL, *rest_attrs = NULL;
  int *pred_offsets = NULL, *rest_offsets = NULL;

  xasl = regu_xasl_node_alloc (SCAN_PROC);
  if (!xasl)
    {
      PT_ERROR (parser, spec,
		msgcat_message (MSGCAT_CATALOG_CUBRID,
				MSGCAT_SET_PARSER_SEMANTIC,
				MSGCAT_SEMANTIC_OUT_OF_MEMORY));
      return NULL;
    }

  unbox = UNBOX_AS_VALUE;

  xasl->val_list = pt_to_val_list (parser, spec->info.spec.id);

  tbl_info = pt_find_table_info (spec->info.spec.id,
				 parser->symbols->table_info);

  if (!pt_split_attrs (parser, tbl_info, join_term,
		       &pred_attrs, &rest_attrs,
		       &pred_offsets, &rest_offsets))
    {
      return NULL;
    }

  /* This generates a list of TYPE_POSITION regu_variables
   * There information is stored in a QFILE_TUPLE_VALUE_POSITION, which
   * describes a type and index into a list file.
   */
  regu_attributes_pred =
    pt_to_position_regu_variable_list (parser,
				       pred_attrs,
				       tbl_info->value_list, pred_offsets);
  regu_attributes_rest =
    pt_to_position_regu_variable_list (parser,
				       rest_attrs,
				       tbl_info->value_list, rest_offsets);

  parser_free_tree (parser, pred_attrs);
  parser_free_tree (parser, rest_attrs);
  free_and_init (pred_offsets);
  free_and_init (rest_offsets);

  parser->symbols->listfile_unbox = unbox;
  parser->symbols->current_listfile = NULL;

  /* The where predicate is now evaluated after the val list has been
   * fetched.  This means that we want to generate "CONSTANT" regu
   * variables instead of "POSITION" regu variables which would happen
   * if parser->symbols->current_listfile != NULL.
   * pred should never user the current instance for fetches
   * either, so we turn off the current_class, if there is one.
   */
  tmp = parser->symbols->current_class;
  parser->symbols->current_class = NULL;
  where = pt_to_pred_expr (parser, join_term);
  parser->symbols->current_class = tmp;

  access = pt_make_list_access_spec (xasl_to_scan, SEQUENTIAL,
				     NULL, where,
				     regu_attributes_pred,
				     regu_attributes_rest);

  if (access)
    {
      xasl->spec_list = access;

      access->single_fetch = QPROC_SINGLE_OUTER;

      regu = pt_join_term_to_regu_variable (parser, join_term);

      if (regu)
	{
	  if (regu->type == TYPE_CONSTANT || regu->type == TYPE_DBVAL)
	    access->s_dbval = pt_regu_to_dbvalue (parser, regu);
	}
    }
  parser->symbols->listfile_unbox = UNBOX_AS_VALUE;

  return xasl;
}


/*
 * pt_to_fetch_proc () - Translate a PT_NODE path entity spec to
 *                       an OBJFETCH proc(SETFETCH disabled for now)
 *   return:
 *   parser(in):
 *   spec(in):
 *   pred(in):
 */
XASL_NODE *
pt_to_fetch_proc (PARSER_CONTEXT * parser, PT_NODE * spec, PT_NODE * pred)
{
  XASL_NODE *xasl = NULL;
  PT_NODE *oid_name = NULL;
  int proc_type = OBJFETCH_PROC;	/* SETFETCH_PROC not used for now */
  REGU_VARIABLE *regu;
  PT_NODE *flat;
  PT_NODE *conjunct;
  PT_NODE *derived;

  if (!spec)
    {
      return NULL;		/* no error */
    }

  if (spec->node_type == PT_SPEC
      && (conjunct = spec->info.spec.path_conjuncts)
      && (conjunct->node_type == PT_EXPR)
      && (oid_name = conjunct->info.expr.arg1))
    {
      flat = spec->info.spec.flat_entity_list;
      if (flat)
	{
	  xasl = regu_xasl_node_alloc ((PROC_TYPE) proc_type);

	  if (xasl)
	    {
	      FETCH_PROC_NODE *fetch = &xasl->proc.fetch;

	      xasl->next = NULL;

	      xasl->outptr_list =
		pt_to_outlist (parser, spec->info.spec.referenced_attrs,
			       NULL, UNBOX_AS_VALUE);

	      if (xasl->outptr_list == NULL)
		{
		  goto exit_on_error;
		}

	      xasl->spec_list = pt_to_class_spec_list (parser, spec, NULL,
						       pred, NULL);

	      if (xasl->spec_list == NULL)
		{
		  goto exit_on_error;
		}

	      xasl->val_list = pt_to_val_list (parser, spec->info.spec.id);

	      /* done in last if_pred, for now */
	      fetch->set_pred = NULL;

	      /* set flag for INNER path fetches */
	      fetch->ql_flag =
		(QL_FLAG) (spec->info.spec.meta_class == PT_PATH_INNER);

	      /* fill in xasl->proc.fetch
	       * set oid argument to DB_VALUE of left side
	       * of dot expression */
	      regu = pt_attribute_to_regu (parser, oid_name);
	      fetch->arg = NULL;
	      if (regu)
		{
		  fetch->arg = pt_regu_to_dbvalue (parser, regu);
		}
	    }
	  else
	    {
	      PT_ERROR (parser, spec,
			msgcat_message (MSGCAT_CATALOG_CUBRID,
					MSGCAT_SET_PARSER_SEMANTIC,
					MSGCAT_SEMANTIC_OUT_OF_MEMORY));
	      return NULL;
	    }
	}
      else if ((derived = spec->info.spec.derived_table))
	{
	  /* this is a derived table path spec */
	  xasl = pt_to_fetch_as_scan_proc (parser, spec, conjunct,
					   (XASL_NODE *) derived->info.query.
					   xasl);
	}
    }

  return xasl;

exit_on_error:

  return NULL;
}


/*
 * pt_to_fetch_proc_list_recurse () - Translate a PT_NODE path (dot) expression
 * 	to a XASL OBJFETCH or SETFETCH proc
 *   return:
 *   parser(in):
 *   spec(in):
 *   root(in):
 */
static void
pt_to_fetch_proc_list_recurse (PARSER_CONTEXT * parser, PT_NODE * spec,
			       XASL_NODE * root)
{
  XASL_NODE *xasl = NULL;

  xasl = pt_to_fetch_proc (parser, spec, NULL);

  if (!xasl)
    {
      return;
    }

  if (xasl->type == SCAN_PROC)
    {
      APPEND_TO_XASL (root, scan_ptr, xasl);
    }
  else
    {
      APPEND_TO_XASL (root, bptr_list, xasl);
    }

  /* get the rest of the fetch procs at this level */
  if (spec->next)
    {
      pt_to_fetch_proc_list_recurse (parser, spec->next, root);
    }

  if (xasl && spec->info.spec.path_entities)
    {
      pt_to_fetch_proc_list_recurse (parser, spec->info.spec.path_entities,
				     root);
    }

  return;
}


/*
 * pt_to_fetch_proc_list () - Translate a PT_NODE path (dot) expression to
 * 	a XASL OBJFETCH or SETFETCH proc
 *   return: none
 *   parser(in):
 *   spec(in):
 *   root(in):
 */
static void
pt_to_fetch_proc_list (PARSER_CONTEXT * parser, PT_NODE * spec,
		       XASL_NODE * root)
{
  XASL_NODE *xasl = NULL;

  pt_to_fetch_proc_list_recurse (parser, spec, root);

  xasl = root->scan_ptr;
  if (xasl)
    {
      while (xasl->scan_ptr)
	{
	  xasl = xasl->scan_ptr;
	}

      /* we must promote the if_pred to the fetch as scan proc
         Only do this once, not recursively */
      xasl->if_pred = root->if_pred;
      root->if_pred = NULL;
      xasl->dptr_list = root->dptr_list;
      root->dptr_list = NULL;
    }

  return;
}


/*
 * ptqo_to_scan_proc () - Convert a spec pt_node to a SCAN_PROC
 *   return:
 *   parser(in):
 *   xasl(in):
 *   spec(in):
 *   where_key_part(in):
 *   where_part(in):
 *   info(in):
 */
XASL_NODE *
ptqo_to_scan_proc (PARSER_CONTEXT * parser,
		   XASL_NODE * xasl,
		   PT_NODE * spec,
		   PT_NODE * where_key_part,
		   PT_NODE * where_part, QO_XASL_INDEX_INFO * info)
{
  if (xasl == NULL)
    {
      xasl = regu_xasl_node_alloc (SCAN_PROC);
    }

  if (!xasl)
    {
      PT_ERROR (parser, spec,
		msgcat_message (MSGCAT_CATALOG_CUBRID,
				MSGCAT_SET_PARSER_SEMANTIC,
				MSGCAT_SEMANTIC_OUT_OF_MEMORY));
      return NULL;
    }

  if (spec != NULL)
    {
      xasl->spec_list = pt_to_spec_list (parser, spec,
					 where_key_part, where_part,
					 info, NULL);
      if (xasl->spec_list == NULL)
	{
	  goto exit_on_error;
	}

      xasl->val_list = pt_to_val_list (parser, spec->info.spec.id);
    }

  return xasl;

exit_on_error:

  return NULL;
}


/*
 * pt_skeleton_buildlist_proc () - Construct a partly
 *                                 initialized BUILDLIST_PROC
 *   return:
 *   parser(in):
 *   namelist(in):
 */
XASL_NODE *
pt_skeleton_buildlist_proc (PARSER_CONTEXT * parser, PT_NODE * namelist)
{
  XASL_NODE *xasl;

  assert (parser != NULL);

  xasl = regu_xasl_node_alloc (BUILDLIST_PROC);
  if (xasl == NULL)
    {
      goto exit_on_error;
    }

  xasl->outptr_list = pt_to_outlist (parser, namelist, NULL, UNBOX_AS_VALUE);
  if (xasl->outptr_list == NULL)
    {
      goto exit_on_error;
    }

  return xasl;

exit_on_error:

  return NULL;
}


/*
 * ptqo_to_list_scan_proc () - Convert an spec pt_node to a SCAN_PROC
 *   return:
 *   parser(in):
 *   xasl(in):
 *   proc_type(in):
 *   listfile(in):
 *   namelist(in):
 *   pred(in):
 *   poslist(in):
 */
XASL_NODE *
ptqo_to_list_scan_proc (PARSER_CONTEXT * parser,
			XASL_NODE * xasl,
			PROC_TYPE proc_type,
			XASL_NODE * listfile,
			PT_NODE * namelist, PT_NODE * pred, int *poslist)
{
  if (xasl == NULL)
    {
      xasl = regu_xasl_node_alloc (proc_type);
    }

  if (xasl && listfile)
    {
      PRED_EXPR *pred_expr = NULL;
      REGU_VARIABLE_LIST regu_attributes = NULL;
      PT_NODE *tmp;
      int *attr_offsets;

      parser->symbols->listfile_unbox = UNBOX_AS_VALUE;
      parser->symbols->current_listfile = NULL;

      /* The where predicate is now evaluated after the val list has been
       * fetched.  This means that we want to generate "CONSTANT" regu
       * variables instead of "POSITION" regu variables which would happen
       * if parser->symbols->current_listfile != NULL.
       * pred should never user the current instance for fetches
       * either, so we turn off the current_class, if there is one.
       */
      tmp = parser->symbols->current_class;
      parser->symbols->current_class = NULL;
      pred_expr = pt_to_pred_expr (parser, pred);
      parser->symbols->current_class = tmp;

      /* Need to create a value list using the already allocated
       * DB_VALUE data buckets on some other XASL_PROC's val list.
       * Actually, these should be simply global, but aren't.
       */
      xasl->val_list = pt_clone_val_list (parser, namelist);

      /* handle the buildlist case.
       * append regu to the out_list, and create a new value
       * to append to the value_list
       */
      attr_offsets = pt_make_identity_offsets (namelist);
      regu_attributes =
	pt_to_position_regu_variable_list (parser, namelist,
					   xasl->val_list, attr_offsets);

      /* hack for the case of list scan in merge join */
      if (poslist)
	{
	  REGU_VARIABLE_LIST p;
	  int i;

	  for (p = regu_attributes, i = 0; p; p = p->next, i++)
	    {
	      p->value.value.pos_descr.pos_no = poslist[i];
	    }
	}
      free_and_init (attr_offsets);

      xasl->spec_list = pt_make_list_access_spec (listfile, SEQUENTIAL, NULL,
						  pred_expr, regu_attributes,
						  NULL);

      if (xasl->spec_list == NULL || xasl->val_list == NULL)
	{
	  xasl = NULL;
	}
    }
  else
    {
      xasl = NULL;
    }

  return xasl;
}


/*
 * ptqo_to_merge_list_proc () - Make a MERGELIST_PROC to merge an inner
 *                              and outer list
 *   return:
 *   parser(in):
 *   left(in):
 *   right(in):
 *   join_type(in):
 */
XASL_NODE *
ptqo_to_merge_list_proc (PARSER_CONTEXT * parser,
			 XASL_NODE * left,
			 XASL_NODE * right, JOIN_TYPE join_type)
{
  XASL_NODE *xasl;

  assert (parser != NULL);

  if (left == NULL || right == NULL)
    {
      return NULL;
    }

  xasl = regu_xasl_node_alloc (MERGELIST_PROC);

  if (!xasl)
    {
      PT_NODE dummy;

      memset (&dummy, 0, sizeof (dummy));
      PT_ERROR (parser, &dummy,
		msgcat_message (MSGCAT_CATALOG_CUBRID,
				MSGCAT_SET_PARSER_SEMANTIC,
				MSGCAT_SEMANTIC_OUT_OF_MEMORY));
      return NULL;
    }

  xasl->proc.mergelist.outer_xasl = left;
  xasl->proc.mergelist.inner_xasl = right;

  if (join_type == JOIN_RIGHT)
    {
      right->next = left;
      xasl->aptr_list = right;
    }
  else
    {
      left->next = right;
      xasl->aptr_list = left;
    }

  return xasl;
}


/*
 * ptqo_single_orderby () - Make a SORT_LIST that will sort the given column
 * 	according to the type of the given name
 *   return:
 *   parser(in):
 */
SORT_LIST *
ptqo_single_orderby (PARSER_CONTEXT * parser)
{
  SORT_LIST *list;

  list = regu_sort_list_alloc ();
  if (list)
    {
      list->next = NULL;
    }

  return list;
}


/*
 * pt_to_scan_proc_list () - Convert a SELECT pt_node to an XASL_NODE
 * 	                     list of SCAN_PROCs
 *   return:
 *   parser(in):
 *   node(in):
 *   root(in):
 */
static XASL_NODE *
pt_to_scan_proc_list (PARSER_CONTEXT * parser, PT_NODE * node,
		      XASL_NODE * root)
{
  XASL_NODE *xasl = NULL;
  XASL_NODE *list = NULL;
  XASL_NODE *last = root;
  PT_NODE *from;

  from = node->info.query.q.select.from->next;

  while (from)
    {
      xasl = ptqo_to_scan_proc (parser, NULL, from, NULL, NULL, NULL);

      pt_to_pred_terms (parser,
			node->info.query.q.select.where,
			from->info.spec.id, &xasl->if_pred);

      pt_set_dptr (parser, node->info.query.q.select.where, xasl,
		   from->info.spec.id);
      pt_set_dptr (parser, node->info.query.q.select.list, xasl,
		   from->info.spec.id);

      if (!xasl)
	{
	  return NULL;
	}

      if (from->info.spec.path_entities)
	{
	  pt_to_fetch_proc_list (parser, from->info.spec.path_entities, xasl);
	}

      pt_set_dptr (parser, from->info.spec.derived_table, last, MATCH_ALL);

      last = xasl;

      from = from->next;

      /* preserve order for maintenance & sanity */
      list = pt_append_scan (list, xasl);
    }

  return list;
}

/*
 * pt_gen_optimized_plan () - Translate a PT_SELECT node to a XASL plan
 *   return:
 *   parser(in):
 *   xasl(in):
 *   select_node(in):
 *   plan(in):
 */
static XASL_NODE *
pt_gen_optimized_plan (PARSER_CONTEXT * parser, XASL_NODE * xasl,
		       PT_NODE * select_node, QO_PLAN * plan)
{
  XASL_NODE *ret = NULL;

  assert (parser != NULL);

  if (xasl && select_node && !parser->error_msgs)
    {
      ret = qo_to_xasl (plan, xasl);
      if (ret == NULL)
	{
	  xasl->spec_list = NULL;
	  xasl->scan_ptr = NULL;
	}
    }

  return ret;
}


/*
 * pt_gen_simple_plan () - Translate a PT_SELECT node to a XASL plan
 *   return:
 *   parser(in):
 *   xasl(in):
 *   select_node(in):
 */
static XASL_NODE *
pt_gen_simple_plan (PARSER_CONTEXT * parser, XASL_NODE * xasl,
		    PT_NODE * select_node)
{
  PT_NODE *from, *where;
  PT_NODE *access_part, *if_part, *instnum_part;
  XASL_NODE *lastxasl;
  int flag;

  assert (parser != NULL);

  if (xasl && select_node && !parser->error_msgs)
    {
      from = select_node->info.query.q.select.from;

      /* copy so as to preserve parse tree */
      where =
	parser_copy_tree_list (parser,
			       select_node->info.query.q.select.where);

      pt_split_access_if_instnum (parser, from, where,
				  &access_part, &if_part, &instnum_part);

      xasl->spec_list = pt_to_spec_list (parser, from, NULL, access_part,
					 NULL, NULL);
      if (xasl->spec_list == NULL)
	{
	  goto exit_on_error;
	}

      /* save where part to restore tree later */
      where = select_node->info.query.q.select.where;
      select_node->info.query.q.select.where = if_part;

      pt_to_pred_terms (parser, if_part, from->info.spec.id, &xasl->if_pred);

      /* and pick up any uncorrelated terms */
      pt_to_pred_terms (parser, if_part, 0, &xasl->if_pred);

      /* set 'etc' field of PT_NODEs which belong to inst_num() expression
         in order to use at pt_regu.c:pt_make_regu_numbering() */
      pt_set_numbering_node_etc (parser, instnum_part,
				 &xasl->instnum_val, NULL);

      flag = 0;
      xasl->instnum_pred = pt_to_pred_expr_with_arg (parser,
						     instnum_part, &flag);

      if (flag & PT_PRED_ARG_INSTNUM_CONTINUE)
	{
	  xasl->instnum_flag = XASL_INSTNUM_FLAG_SCAN_CONTINUE;
	}

      if (from->info.spec.path_entities)
	{
	  pt_to_fetch_proc_list (parser, from->info.spec.path_entities, xasl);
	}

      /* Find the last scan proc. Some psuedo-fetch procs may be on
       * this list */
      lastxasl = xasl;
      while (lastxasl && lastxasl->scan_ptr)
	{
	  lastxasl = lastxasl->scan_ptr;
	}

      /* if pseudo fetch procs are there, the dptr must be attached to
       * the last xasl scan proc. */
      pt_set_dptr (parser, select_node->info.query.q.select.where,
		   lastxasl, from->info.spec.id);

      /* this also correctly places correlated subqueries for derived tables */
      lastxasl->scan_ptr = pt_to_scan_proc_list (parser,
						 select_node, lastxasl);

      while (lastxasl && lastxasl->scan_ptr)
	{
	  lastxasl = lastxasl->scan_ptr;
	}

      /* make sure all scan_ptrs are found before putting correlated
       * subqueries from the select list on the last (inner) scan_ptr.
       * because they may be correlated to specs later in the from list.
       */
      pt_set_dptr (parser, select_node->info.query.q.select.list,
		   lastxasl, 0);

      xasl->val_list = pt_to_val_list (parser, from->info.spec.id);

      parser_free_tree (parser, access_part);
      parser_free_tree (parser, if_part);
      parser_free_tree (parser, instnum_part);
      select_node->info.query.q.select.where = where;
    }

  return xasl;

exit_on_error:

  return NULL;
}


/*
 * pt_gen_simple_merge_plan () - Translate a PT_SELECT node to a XASL plan
 *   return:
 *   parser(in):
 *   xasl(in):
 *   select_node(in):
 */
XASL_NODE *
pt_gen_simple_merge_plan (PARSER_CONTEXT * parser, XASL_NODE * xasl,
			  PT_NODE * select_node)
{
  PT_NODE *table1, *table2;
  PT_NODE *where;
  PT_NODE *if_part, *instnum_part;
  int flag;

  assert (parser != NULL);

  if (xasl && select_node && !parser->error_msgs &&
      (table1 = select_node->info.query.q.select.from) &&
      (table2 = select_node->info.query.q.select.from->next) &&
      !select_node->info.query.q.select.from->next->next)
    {
      xasl->spec_list = pt_to_spec_list (parser, table1, NULL,
					 NULL, NULL, NULL);
      if (xasl->spec_list == NULL)
	{
	  goto exit_on_error;
	}

      xasl->merge_spec = pt_to_spec_list (parser, table2, NULL,
					  NULL, NULL, table1);
      if (xasl->merge_spec == NULL)
	{
	  goto exit_on_error;
	}

      if (table1->info.spec.path_entities)
	{
	  pt_to_fetch_proc_list (parser,
				 table1->info.spec.path_entities, xasl);
	}

      if (table2->info.spec.path_entities)
	{
	  pt_to_fetch_proc_list (parser,
				 table2->info.spec.path_entities, xasl);
	}

      /* Correctly place correlated subqueries for derived tables. */
      if (table1->info.spec.derived_table)
	{
	  pt_set_dptr (parser, table1->info.spec.derived_table,
		       xasl, table1->info.spec.id);
	}

      /* There are two cases for table2:
       *   1) if table1 is a derived table, then if table2 is correlated
       *      then it is correlated to table1.
       *   2) if table1 is not derived then if table2 is correlated, then
       *      it correlates to the merge block.
       * Case 2 should never happen for rewritten queries that contain
       * method calls, but we include it here for completeness.
       */
      if (table1->info.spec.derived_table &&
	  table1->info.spec.derived_table_type == PT_IS_SUBQUERY)
	{
	  XASL_NODE *t_xasl;

	  if (!(t_xasl = (XASL_NODE *)
		table1->info.spec.derived_table->info.query.xasl))
	    {
	      PT_INTERNAL_ERROR (parser, "generate plan");
	      goto exit_on_error;
	    }

	  pt_set_dptr (parser, table2->info.spec.derived_table,
		       t_xasl, table2->info.spec.id);
	}
      else
	{
	  pt_set_dptr (parser, table2->info.spec.derived_table,
		       xasl, table2->info.spec.id);
	}

      xasl->val_list = pt_to_val_list (parser, table1->info.spec.id);
      xasl->merge_val_list = pt_to_val_list (parser, table2->info.spec.id);

      /* copy so as to preserve parse tree */
      where =
	parser_copy_tree_list (parser,
			       select_node->info.query.q.select.where);

      pt_split_if_instnum (parser, where, &if_part, &instnum_part);

      /* This is NOT temporary till where clauses get sorted out!!!
       * We never want predicates on the scans of the tables because merge
       * depend on both tables having the same cardinality which would get
       * screwed up if we pushed predicates down into the table scans.
       */
      pt_to_pred_terms (parser, if_part, table1->info.spec.id,
			&xasl->if_pred);
      pt_to_pred_terms (parser, if_part, table2->info.spec.id,
			&xasl->if_pred);

      /* set 'etc' field of PT_NODEs which belong to inst_num() expression
         in order to use at pt_regu.c:pt_make_regu_numbering() */
      pt_set_numbering_node_etc (parser, instnum_part,
				 &xasl->instnum_val, NULL);
      flag = 0;
      xasl->instnum_pred = pt_to_pred_expr_with_arg (parser,
						     instnum_part, &flag);
      if (flag & PT_PRED_ARG_INSTNUM_CONTINUE)
	{
	  xasl->instnum_flag = XASL_INSTNUM_FLAG_SCAN_CONTINUE;
	}
      pt_set_dptr (parser, if_part, xasl, MATCH_ALL);

      pt_set_dptr (parser, select_node->info.query.q.select.list,
		   xasl, MATCH_ALL);

      parser_free_tree (parser, if_part);
      parser_free_tree (parser, instnum_part);
    }

  return xasl;

exit_on_error:

  return NULL;
}


/*
 * pt_to_buildlist_proc () - Translate a PT_SELECT node to
 *                           a XASL buildlist proc
 *   return:
 *   parser(in):
 *   select_node(in):
 *   qo_plan(in):
 */
static XASL_NODE *
pt_to_buildlist_proc (PARSER_CONTEXT * parser, PT_NODE * select_node,
		      QO_PLAN * qo_plan)
{
  XASL_NODE *xasl;
  PT_NODE *tmp;
  int groupby_ok = 1;
  AGGREGATE_TYPE *aggregate = NULL;
  SYMBOL_INFO *symbols;
  PT_NODE *from;
  UNBOX unbox;
  PT_NODE *having_part, *grbynum_part;
  int grbynum_flag, ordbynum_flag;
  bool orderby_skip = false, orderby_ok = true;

  assert (parser != NULL);

  if (!(symbols = parser->symbols))
    {
      return NULL;
    }

  xasl = regu_xasl_node_alloc (BUILDLIST_PROC);

  if (!xasl || select_node->node_type != PT_SELECT ||
      !(from = select_node->info.query.q.select.from))
    {
      xasl = NULL;
    }
  else
    {
      BUILDLIST_PROC_NODE *buildlist = &xasl->proc.buildlist;

      xasl->next = NULL;

      /* assume parse tree correct, and PT_DISTINCT only other possibility */
      xasl->option = (select_node->info.query.all_distinct == PT_ALL)
	? Q_ALL : Q_DISTINCT;

      unbox = UNBOX_AS_VALUE;

      if (select_node->info.query.q.select.group_by)
	{
	  int *attr_offsets;
	  PT_NODE *group_out_list, *group;

	  group_out_list = NULL;
	  for (group = select_node->info.query.q.select.group_by;
	       group; group = group->next)
	    {
	      /* safe guard: invalid parse tree */
	      if (group->node_type != PT_SORT_SPEC)
		{
		  if (group_out_list)
		    {
		      parser_free_tree (parser, group_out_list);
		    }
		  goto exit_on_error;
		}

	      group_out_list =
		parser_append_node (pt_point
				    (parser, group->info.sort_spec.expr),
				    group_out_list);
	    }

	  xasl->outptr_list = pt_to_outlist (parser, group_out_list,
					     NULL, UNBOX_AS_VALUE);

	  if (xasl->outptr_list == NULL)
	    {
	      if (group_out_list)
		{
		  parser_free_tree (parser, group_out_list);
		}
	      goto exit_on_error;
	    }

	  buildlist->g_val_list = pt_make_val_list (group_out_list);

	  if (buildlist->g_val_list == NULL)
	    {
	      PT_ERRORm (parser, group_out_list, MSGCAT_SET_PARSER_SEMANTIC,
			 MSGCAT_SEMANTIC_OUT_OF_MEMORY);
	      if (group_out_list)
		{
		  parser_free_tree (parser, group_out_list);
		}
	      goto exit_on_error;
	    }

	  attr_offsets = pt_make_identity_offsets (group_out_list);

	  buildlist->g_regu_list =
	    pt_to_position_regu_variable_list (parser,
					       group_out_list,
					       buildlist->g_val_list,
					       attr_offsets);

	  free_and_init (attr_offsets);

	  /* set 'etc' field of PT_NODEs which belong to inst_num() and
	     orderby_num() expression in order to use at
	     pt_regu.c:pt_make_regu_numbering() */
	  pt_set_numbering_node_etc (parser,
				     select_node->info.query.q.select.list,
				     &xasl->instnum_val, &xasl->ordbynum_val);

	  aggregate = pt_to_aggregate (parser, select_node,
				       xasl->outptr_list,
				       buildlist->g_val_list,
				       buildlist->g_regu_list,
				       group_out_list,
				       &buildlist->g_grbynum_val, false);

	  /* set current_listfile only around call to make g_outptr_list
	   * and havein_pred */
	  symbols->current_listfile = group_out_list;
	  symbols->listfile_value_list = buildlist->g_val_list;

	  buildlist->g_outptr_list =
	    pt_to_outlist (parser, select_node->info.query.q.select.list,
			   NULL, unbox);

	  if (buildlist->g_outptr_list == NULL)
	    {
	      if (group_out_list)
		{
		  parser_free_tree (parser, group_out_list);
		}
	      goto exit_on_error;
	    }

	  /* pred should never user the current instance for fetches
	   * either, so we turn off the current_class, if there is one. */
	  tmp = parser->symbols->current_class;
	  parser->symbols->current_class = NULL;
	  pt_split_having_grbynum (parser,
				   select_node->info.query.q.select.having,
				   &having_part, &grbynum_part);
	  buildlist->g_having_pred = pt_to_pred_expr (parser, having_part);
	  grbynum_flag = 0;
	  buildlist->g_grbynum_pred =
	    pt_to_pred_expr_with_arg (parser, grbynum_part, &grbynum_flag);
	  if (grbynum_flag & PT_PRED_ARG_GRBYNUM_CONTINUE)
	    {
	      buildlist->g_grbynum_flag = XASL_G_GRBYNUM_FLAG_SCAN_CONTINUE;
	    }
	  select_node->info.query.q.select.having =
	    parser_append_node (having_part, grbynum_part);

	  parser->symbols->current_class = tmp;
	  symbols->current_listfile = NULL;
	  symbols->listfile_value_list = NULL;
	  if (group_out_list)
	    {
	      parser_free_tree (parser, group_out_list);
	    }

	  buildlist->g_agg_list = aggregate;
	}
      else
	{
	  /* set 'etc' field of PT_NODEs which belong to inst_num() and
	     orderby_num() expression in order to use at
	     pt_regu.c:pt_make_regu_numbering() */
	  pt_set_numbering_node_etc (parser,
				     select_node->info.query.q.select.list,
				     &xasl->instnum_val, &xasl->ordbynum_val);

	  xasl->outptr_list =
	    pt_to_outlist (parser, select_node->info.query.q.select.list,
			   &xasl->selected_upd_list, unbox);

	  /* check if this select statement has click counter */
	  if (xasl->selected_upd_list != NULL)
	    {
	      /* set lock timeout hint if specified */
	      PT_NODE *hint_arg;
	      float waitsecs;

	      xasl->selected_upd_list->waitsecs = XASL_WAITSECS_NOCHANGE;
	      hint_arg = select_node->info.query.q.select.waitsecs_hint;
	      if (select_node->info.query.q.select.hint & PT_HINT_LK_TIMEOUT
		  && PT_IS_HINT_NODE (hint_arg))
		{
		  waitsecs = atof (hint_arg->info.name.original) * 1000;
		  xasl->selected_upd_list->waitsecs =
		    (waitsecs >= -1) ? waitsecs : XASL_WAITSECS_NOCHANGE;
		}
	    }

	  if (xasl->outptr_list == NULL)
	    {
	      goto exit_on_error;
	    }
	}

      /* the calls pt_to_out_list and pt_to_spec_list
       * record information in the "symbol_info" structure
       * used by subsequent calls, and must be done first, before
       * calculating subquery lists, etc.
       */

      pt_set_aptr (parser, select_node, xasl);

      if (!qo_plan ||
	  !pt_gen_optimized_plan (parser, xasl, select_node, qo_plan))
	{
	  while (from)
	    {
	      if (from->info.spec.join_type != PT_JOIN_NONE)
		{
		  PT_ERRORm (parser, from, MSGCAT_SET_PARSER_RUNTIME,
			     MSGCAT_RUNTIME_OUTER_JOIN_OPT_FAILED);
		  goto exit_on_error;
		}
	      from = from->next;
	    }

	  if (select_node->info.query.q.select.flavor == PT_MERGE)
	    {
	      xasl = pt_gen_simple_merge_plan (parser, xasl, select_node);
	    }
	  else
	    {
	      xasl = pt_gen_simple_plan (parser, xasl, select_node);
	    }

	  if (xasl == NULL)
	    {
	      goto exit_on_error;
	    }

	  buildlist = &xasl->proc.buildlist;

	  /* mark as simple plan generation */
	  qo_plan = NULL;
	}

      if (xasl->outptr_list)
	{
	  if (qo_plan)
	    {			/* is optimized plan */
	      xasl->after_iscan_list =
		pt_to_after_iscan (parser,
				   qo_plan_iscan_sort_list (qo_plan),
				   select_node);
	    }
	  else
	    {
	      xasl->after_iscan_list = NULL;
	    }

	  if (select_node->info.query.order_by)
	    {
	      /* set 'etc' field of PT_NODEs which belong to inst_num() and
	         orderby_num() expression in order to use at
	         pt_regu.c:pt_make_regu_numbering() */
	      pt_set_numbering_node_etc (parser,
					 select_node->info.query.orderby_for,
					 NULL, &xasl->ordbynum_val);
	      ordbynum_flag = 0;
	      xasl->ordbynum_pred =
		pt_to_pred_expr_with_arg (parser,
					  select_node->info.query.orderby_for,
					  &ordbynum_flag);
	      if (ordbynum_flag & PT_PRED_ARG_ORDBYNUM_CONTINUE)
		{
		  xasl->ordbynum_flag = XASL_ORDBYNUM_FLAG_SCAN_CONTINUE;
		}

	      /* check order by opt */
	      if (qo_plan && qo_plan_skip_orderby (qo_plan) == true)
		{
		  orderby_skip = true;

		  /* move orderby_num() to inst_num() */
		  if (xasl->ordbynum_val)
		    {
		      xasl->instnum_pred = xasl->ordbynum_pred;
		      xasl->instnum_val = xasl->ordbynum_val;
		      xasl->instnum_flag = xasl->ordbynum_flag;

		      xasl->ordbynum_pred = NULL;
		      xasl->ordbynum_val = NULL;
		      xasl->ordbynum_flag = 0;
		    }
		}
	      else
		{
		  xasl->orderby_list =
		    pt_to_orderby (parser,
				   select_node->info.query.order_by,
				   select_node);
		  /* clear flag */
		  XASL_CLEAR_FLAG (xasl, XASL_SKIP_ORDERBY_LIST);
		}

	      /* sanity check */
	      orderby_ok = ((xasl->orderby_list != NULL) || orderby_skip);
	    }

	  /* union fields for BUILDLIST_PROC_NODE - BUILDLIST_PROC */
	  if (select_node->info.query.q.select.group_by)
	    {
	      /* finish group by processing */
	      buildlist->groupby_list =
		pt_to_groupby (parser,
			       select_node->info.query.q.select.group_by,
			       select_node);

	      /* Build SORT_LIST of the list file created by GROUP BY */
	      buildlist->after_groupby_list =
		pt_to_after_groupby (parser,
				     select_node->info.query.q.select.
				     group_by, select_node);

	      /* this is not useful, set it to NULL
	         it was set by the old parser, but not used anywhere */
	      buildlist->g_outarith_list = NULL;

	      /* This is a having subquery list. If it has correlated
	       * subqueries, they must be run each group */
	      buildlist->eptr_list =
		pt_to_corr_subquery_list (parser,
					  select_node->info.query.q.select.
					  having, 0);

	      /* otherwise should be run once, at beginning.
	         these have already been put on the aptr list above */
	      groupby_ok = (buildlist->groupby_list &&
			    buildlist->g_outptr_list &&
			    (buildlist->g_having_pred ||
			     buildlist->g_grbynum_pred ||
			     !select_node->info.query.q.select.having));
	    }
	  else
	    {
	      /* with no group by, a build-list proc should not be built
	         a build-value proc should be built instead */
	      buildlist->groupby_list = NULL;
	      buildlist->g_regu_list = NULL;
	      buildlist->g_val_list = NULL;
	      buildlist->g_having_pred = NULL;
	      buildlist->g_grbynum_pred = NULL;
	      buildlist->g_grbynum_val = NULL;
	      buildlist->g_grbynum_flag = 0;
	      buildlist->g_agg_list = NULL;
	      buildlist->eptr_list = NULL;
	    }

	  /* set index scan order */
	  xasl->iscan_order = (orderby_skip) ? true
	    : PRM_BT_INDEX_SCAN_OID_ORDER;

	  /* save single tuple info */
	  if (select_node->info.query.single_tuple == 1)
	    {
	      xasl->is_single_tuple = true;
	    }
	}			/* end xasl->outptr_list */

      /* verify everything worked */
      if (!xasl->outptr_list || !xasl->spec_list || !xasl->val_list ||
	  !groupby_ok || !orderby_ok || parser->error_msgs)
	{
	  xasl = NULL;
	}
    }

  return xasl;

exit_on_error:

  return NULL;
}


/*
 * pt_to_buildvalue_proc () - Make a buildvalue xasl proc
 *   return:
 *   parser(in):
 *   select_node(in):
 *   qo_plan(in):
 */
static XASL_NODE *
pt_to_buildvalue_proc (PARSER_CONTEXT * parser, PT_NODE * select_node,
		       QO_PLAN * qo_plan)
{
  XASL_NODE *xasl;
  AGGREGATE_TYPE *aggregate;
  PT_NODE *from, *tmp;
  UNBOX unbox;
  int flag_agg_optimize = false;

  xasl = regu_xasl_node_alloc (BUILDVALUE_PROC);

  if (!xasl || select_node->node_type != PT_SELECT ||
      !(from = select_node->info.query.q.select.from))
    {
      xasl = NULL;
    }
  else
    {
      BUILDVALUE_PROC_NODE *buildvalue = &xasl->proc.buildvalue;

      xasl->next = NULL;

      /* assume parse tree correct, and PT_DISTINCT only other possibility */
      xasl->option = (select_node->info.query.all_distinct == PT_ALL)
	? Q_ALL : Q_DISTINCT;

      /* set 'etc' field of PT_NODEs which belong to inst_num() and
         orderby_num() expression in order to use at
         pt_regu.c:pt_make_regu_numbering() */
      pt_set_numbering_node_etc (parser,
				 select_node->info.query.q.select.list,
				 &xasl->instnum_val, &xasl->ordbynum_val);

      if (select_node->info.query.q.select.where == NULL &&
	  pt_length_of_list (select_node->info.query.q.select.from) == 1 &&
	  pt_length_of_list (from->info.spec.flat_entity_list) == 1 &&
	  select_node->info.query.q.select.from->info.spec.only_all != PT_ALL)
	{
	  flag_agg_optimize = true;
	}

      aggregate = pt_to_aggregate (parser, select_node, NULL, NULL,
				   NULL, NULL, &buildvalue->grbynum_val,
				   flag_agg_optimize);

      /* the calls pt_to_out_list, pt_to_spec_list, and pt_to_if_pred,
       * record information in the "symbol_info" structure
       * used by subsequent calls, and must be done first, before
       * calculating subquery lists, etc.
       */
      unbox = UNBOX_AS_VALUE;
      xasl->outptr_list = pt_to_outlist (parser,
					 select_node->info.query.q.select.
					 list, &xasl->selected_upd_list,
					 unbox);

      /* check if this select statement has click counter */
      if (xasl->selected_upd_list != NULL)
	{
	  /* set lock timeout hint if specified */
	  PT_NODE *hint_arg;
	  float waitsecs;

	  xasl->selected_upd_list->waitsecs = XASL_WAITSECS_NOCHANGE;
	  hint_arg = select_node->info.query.q.select.waitsecs_hint;
	  if (select_node->info.query.q.select.hint & PT_HINT_LK_TIMEOUT
	      && PT_IS_HINT_NODE (hint_arg))
	    {
	      waitsecs = atof (hint_arg->info.name.original) * 1000;
	      xasl->selected_upd_list->waitsecs =
		(waitsecs >= -1) ? waitsecs : XASL_WAITSECS_NOCHANGE;
	    }
	}

      if (xasl->outptr_list == NULL)
	{
	  goto exit_on_error;
	}

      pt_set_aptr (parser, select_node, xasl);

      if (!qo_plan ||
	  !pt_gen_optimized_plan (parser, xasl, select_node, qo_plan))
	{
	  while (from)
	    {
	      if (from->info.spec.join_type != PT_JOIN_NONE)
		{
		  PT_ERRORm (parser, from, MSGCAT_SET_PARSER_RUNTIME,
			     MSGCAT_RUNTIME_OUTER_JOIN_OPT_FAILED);
		  goto exit_on_error;
		}
	      from = from->next;
	    }

	  if (select_node->info.query.q.select.flavor == PT_MERGE)
	    {
	      xasl = pt_gen_simple_merge_plan (parser, xasl, select_node);
	    }
	  else
	    {
	      xasl = pt_gen_simple_plan (parser, xasl, select_node);
	    }

	  if (xasl == NULL)
	    {
	      goto exit_on_error;
	    }
	  buildvalue = &xasl->proc.buildvalue;
	}

      /* save info for derived table size estimation */
      xasl->projected_size = 1;
      xasl->cardinality = 1.0;

      /* pred should never user the current instance for fetches
       * either, so we turn off the current_class, if there is one.
       */
      tmp = parser->symbols->current_class;
      parser->symbols->current_class = NULL;
      buildvalue->having_pred =
	pt_to_pred_expr (parser, select_node->info.query.q.select.having);
      parser->symbols->current_class = NULL;

      {
	XASL_NODE *dptr_head;

	if (xasl->scan_ptr)
	  {
	    dptr_head = xasl->scan_ptr;
	    while (dptr_head->scan_ptr)
	      {
		dptr_head = dptr_head->scan_ptr;
	      }
	  }
	else
	  {
	    dptr_head = xasl;
	  }
	pt_set_dptr (parser, select_node->info.query.q.select.having,
		     dptr_head, MATCH_ALL);
      }

      /*  union fields from BUILDVALUE_PROC_NODE - BUILDVALUE_PROC */
      buildvalue->agg_list = aggregate;

      /* this is not useful, set it to NULL.
       * it was set by the old parser, and apparently used, but the use was
       * apparently redundant.
       */
      buildvalue->outarith_list = NULL;

      /* verify everything worked */
      if (!xasl->outptr_list ||
	  !xasl->spec_list || !xasl->val_list || parser->error_msgs)
	{
	  xasl = NULL;
	}
    }

  return xasl;

exit_on_error:

  return NULL;
}


/*
 * pt_to_union_proc () - converts a PT_NODE tree of a query
 * 	                 union/intersection/difference to an XASL tree
 *   return: XASL_NODE, NULL indicates error
 *   parser(in): context
 *   node(in): a query union/difference/intersection
 *   type(in): xasl PROC type
 */
static XASL_NODE *
pt_to_union_proc (PARSER_CONTEXT * parser, PT_NODE * node, PROC_TYPE type)
{
  XASL_NODE *xasl = NULL;
  XASL_NODE *left, *right = NULL;
  SORT_LIST *orderby = NULL;
  int ordbynum_flag;

  /* note that PT_UNION, PT_DIFFERENCE, and PT_INTERSECTION node types
   * share the same node structure */
  left = (XASL_NODE *) node->info.query.q.union_.arg1->info.query.xasl;
  right = (XASL_NODE *) node->info.query.q.union_.arg2->info.query.xasl;

  /* orderby can legitimately be null */
  orderby = pt_to_orderby (parser, node->info.query.order_by, node);

  if (left && right && (orderby || !node->info.query.order_by))
    {
      /* don't allocate till everything looks ok. */
      xasl = regu_xasl_node_alloc (type);
    }

  if (xasl)
    {
      xasl->proc.union_.left = left;
      xasl->proc.union_.right = right;

      /* assume parse tree correct, and PT_DISTINCT only other possibility */
      xasl->option = (node->info.query.all_distinct == PT_ALL)
	? Q_ALL : Q_DISTINCT;

      xasl->orderby_list = orderby;

      /* clear flag */
      XASL_CLEAR_FLAG (xasl, XASL_SKIP_ORDERBY_LIST);

      /* save single tuple info */
      if (node->info.query.single_tuple == 1)
	{
	  xasl->is_single_tuple = true;
	}

      /* set 'etc' field of PT_NODEs which belong to inst_num() and
         orderby_num() expression in order to use at
         pt_regu.c:pt_make_regu_numbering() */
      pt_set_numbering_node_etc (parser,
				 node->info.query.orderby_for,
				 NULL, &xasl->ordbynum_val);
      ordbynum_flag = 0;
      xasl->ordbynum_pred =
	pt_to_pred_expr_with_arg (parser,
				  node->info.query.orderby_for,
				  &ordbynum_flag);

      if (ordbynum_flag & PT_PRED_ARG_ORDBYNUM_CONTINUE)
	{
	  xasl->ordbynum_flag = XASL_ORDBYNUM_FLAG_SCAN_CONTINUE;
	}

      pt_set_aptr (parser, node, xasl);

      /* save info for derived table size estimation */
      switch (type)
	{
	case PT_UNION:
	  xasl->projected_size =
	    MAX (left->projected_size, right->projected_size);
	  xasl->cardinality = left->cardinality + right->cardinality;
	  break;
	case PT_DIFFERENCE:
	  xasl->projected_size = left->projected_size;
	  xasl->cardinality = left->cardinality;
	  break;
	case PT_INTERSECTION:
	  xasl->projected_size =
	    MAX (left->projected_size, right->projected_size);
	  xasl->cardinality = MIN (left->cardinality, right->cardinality);
	  break;
	default:
	  break;
	}
    }				/* end xasl */
  else
    {
      xasl = NULL;
    }

  return xasl;
}


/*
 * pt_plan_set_query () - converts a PT_NODE tree of
 *                        a query union to an XASL tree
 *   return: XASL_NODE, NULL indicates error
 *   parser(in): context
 *   node(in): a query union/difference/intersection
 *   proc_type(in): xasl PROC type
 */
static XASL_NODE *
pt_plan_set_query (PARSER_CONTEXT * parser, PT_NODE * node,
		   PROC_TYPE proc_type)
{
  XASL_NODE *xasl;

  /* no optimization for now */
  xasl = pt_to_union_proc (parser, node, proc_type);

  return xasl;
}


/*
 * pt_plan_query () -
 *   return: XASL_NODE, NULL indicates error
 *   parser(in): context
 *   select_node(in): of PT_SELECT type
 */
static XASL_NODE *
pt_plan_query (PARSER_CONTEXT * parser, PT_NODE * select_node)
{
  XASL_NODE *xasl;
  QO_PLAN *plan = NULL;
  int level;
  bool hint_ignored = false;

  if (select_node->node_type != PT_SELECT)
    {
      return NULL;
    }

  /* Check for join, path expr, and index optimizations */
  plan = qo_optimize_query (parser, select_node);

  /* optimization fails, ignore join hint and retry optimization */
  if (!plan && select_node->info.query.q.select.hint != PT_HINT_NONE)
    {
      hint_ignored = true;

      /* init hint */
      select_node->info.query.q.select.hint = PT_HINT_NONE;
      if (select_node->info.query.q.select.ordered)
	{
	  parser_free_tree (parser, select_node->info.query.q.select.ordered);
	  select_node->info.query.q.select.ordered = NULL;
	}
      if (select_node->info.query.q.select.use_nl)
	{
	  parser_free_tree (parser, select_node->info.query.q.select.use_nl);
	  select_node->info.query.q.select.use_nl = NULL;
	}
      if (select_node->info.query.q.select.use_idx)
	{
	  parser_free_tree (parser, select_node->info.query.q.select.use_idx);
	  select_node->info.query.q.select.use_idx = NULL;
	}
      if (select_node->info.query.q.select.use_merge)
	{
	  parser_free_tree (parser,
			    select_node->info.query.q.select.use_merge);
	  select_node->info.query.q.select.use_merge = NULL;
	}

      select_node->alias_print = NULL;

#if defined(CUBRID_DEBUG)
      PT_NODE_PRINT_TO_ALIAS (parser, select_node, PT_CONVERT_RANGE);
#endif /* CUBRID_DEBUG */

      plan = qo_optimize_query (parser, select_node);
    }

  if (pt_is_single_tuple (parser, select_node))
    {
      xasl = pt_to_buildvalue_proc (parser, select_node, plan);
    }
  else
    {
      xasl = pt_to_buildlist_proc (parser, select_node, plan);
    }

  /* Print out any needed post-optimization info.  Leave a way to find
   * out about environment info if we aren't able to produce a plan.
   * If this happens in the field at least we'll be able to glean some info */
  qo_get_optimization_param (&level, QO_PARAM_LEVEL);
  if (level >= 0x100 && plan)
    {
      if (query_plan_dump_fp == NULL)
	{
	  query_plan_dump_fp = stdout;
	}
      fputs ("\nQuery plan:\n", query_plan_dump_fp);
      qo_plan_dump (plan, query_plan_dump_fp);
    }

  if (level & 0x200)
    {
      unsigned int save_custom;

      if (query_plan_dump_fp == NULL)
	{
	  query_plan_dump_fp = stdout;
	}

      save_custom = parser->custom_print;
      parser->custom_print |= PT_CONVERT_RANGE;
      fprintf (query_plan_dump_fp, "\nQuery stmt:%s\n\n%s\n\n",
	       ((hint_ignored) ? " [Warning: HINT ignored]" : ""),
	       parser_print_tree (parser, select_node));
      if (select_node->info.query.order_by)
	{
	  if (xasl && xasl->orderby_list == NULL)
	    {
	      fprintf (query_plan_dump_fp, "/* ---> skip ORDER BY */\n");
	    }
	}
      parser->custom_print = save_custom;
    }

  if (plan)
    {
      qo_plan_discard (plan);
    }

  return xasl;
}


/*
 * pt_to_xasl_proc () - Creates xasl proc for parse tree.
 * 	Also used for direct recursion, not for subquery recursion
 *   return:
 *   parser(in):
 *   node(in): pointer to a query structure
 *   query_list(in): pointer to the generated xasl-tree
 */
static XASL_NODE *
pt_to_xasl_proc (PARSER_CONTEXT * parser, PT_NODE * node,
		 PT_NODE * query_list)
{
  XASL_NODE *xasl = NULL;
  PT_NODE *query;

  /* we should propagate abort error from the server */
  if (!parser->abort && PT_IS_QUERY (node))
    {
      /* check for cached query xasl */
      for (query = query_list; query; query = query->next)
	{
	  if (query->info.query.xasl
	      && query->info.query.id == node->info.query.id)
	    {
	      /* found cached query xasl */
	      node->info.query.xasl = query->info.query.xasl;
	      node->info.query.correlation_level
		= query->info.query.correlation_level;

	      return (XASL_NODE *) node->info.query.xasl;
	    }
	}			/* for (query = ... ) */

      /* not found cached query xasl */
      switch (node->node_type)
	{
	case PT_SELECT:
	  if (query_plan_dump_filename != NULL)
	    {
	      query_plan_dump_fp = fopen (query_plan_dump_filename, "a");
	    }
	  if (query_plan_dump_fp == NULL)
	    {
	      query_plan_dump_fp = stdout;
	    }

	  xasl = pt_plan_query (parser, node);
	  node->info.query.xasl = xasl;

	  if (query_plan_dump_fp != NULL && query_plan_dump_fp != stdout)
	    {
	      fclose (query_plan_dump_fp);
	      query_plan_dump_fp = stdout;
	    }
	  break;

	case PT_UNION:
	  xasl = pt_plan_set_query (parser, node, UNION_PROC);
	  node->info.query.xasl = xasl;
	  break;

	case PT_DIFFERENCE:
	  xasl = pt_plan_set_query (parser, node, DIFFERENCE_PROC);
	  node->info.query.xasl = xasl;
	  break;

	case PT_INTERSECTION:
	  xasl = pt_plan_set_query (parser, node, INTERSECTION_PROC);
	  node->info.query.xasl = xasl;
	  break;

	default:
	  if (!parser->error_msgs)
	    PT_INTERNAL_ERROR (parser, "generate xasl");
	  /* should never get here */
	  break;
	}
    }

  if (parser->error_msgs)
    {
      xasl = NULL;		/* signal error occured */
    }

  if (xasl)
    {
      PT_NODE *spec;

      /* Check to see if composite locking needs to be turned on.
       * We do not do composite locking from proxies. */
      if (node->node_type == PT_SELECT
	  && node->info.query.xasl
	  && node->info.query.composite_locking
	  && (spec = node->info.query.q.select.from)
	  && spec->info.spec.flat_entity_list)
	{
	  xasl->composite_locking = 1;
	}

      /* set as zero correlation-level; this uncorrelated subquery need to
       * be executed at most one time */
      if (node->info.query.correlation_level == 0)
	{
	  XASL_SET_FLAG (xasl, XASL_ZERO_CORR_LEVEL);
	}

/* BUG FIX - COMMENT OUT: DO NOT REMOVE ME FOR USE IN THE FUTURE */
#if 0
      /* cache query xasl */
      if (node->info.query.id)
	{
	  query = parser_new_node (parser, node->node_type);
	  query->info.query.id = node->info.query.id;
	  query->info.query.xasl = node->info.query.xasl;
	  query->info.query.correlation_level =
	    node->info.query.correlation_level;

	  query_list = parser_append_node (query, query_list);
	}
#endif /* 0 */
    }
  else
    {
      /* if the previous request to get a driver caused a deadlock
         following message would make confuse */
      if (!parser->abort && !parser->error_msgs)
	{
	  PT_INTERNAL_ERROR (parser, "generate xasl");
	}
    }

  return xasl;
}

/*
 * pt_spec_to_xasl_class_oid_list () - get class OID list
 *                                     from the spec node list
 *   return:
 *   spec(in):
 *   oid_listp(out):
 *   rep_listp(out):
 *   nump(out):
 *   sizep(out):
 */
static int
pt_spec_to_xasl_class_oid_list (const PT_NODE * spec,
				OID ** oid_listp, int **rep_listp,
				int *nump, int *sizep)
{
  PT_NODE *flat;
  OID *oid, *o_list;
  int *r_list;
  size_t t_num, t_size, prev_t_num;

  if (!*oid_listp || !*rep_listp)
    {
      *oid_listp = (OID *) malloc (sizeof (OID) * OID_LIST_GROWTH);
      *rep_listp = (int *) malloc (sizeof (int) * OID_LIST_GROWTH);
      *sizep = OID_LIST_GROWTH;
    }

  if (!*oid_listp || !*rep_listp || *nump >= *sizep)
    {
      goto error;
    }

  t_num = *nump;
  t_size = *sizep;
  o_list = *oid_listp;
  r_list = *rep_listp;

  /* traverse spec list which is a FROM clause */
  for (; spec; spec = spec->next)
    {
      /* traverse flat entity list which are resolved classes */
      for (flat = spec->info.spec.flat_entity_list; flat; flat = flat->next)
	{
	  /* get the OID of the class object which is fetched brefore */
	  if (flat->info.name.db_object &&
	      (oid = db_identifier (flat->info.name.db_object)) != NULL)
	    {
	      prev_t_num = t_num;
	      (void) lsearch (oid, o_list, &t_num, sizeof (OID), oid_compare);
	      if (t_num > prev_t_num && t_num > (size_t) * nump)
		{
		  *(r_list + t_num - 1) =
		    sm_get_class_repid (flat->info.name.db_object);
		}
	      if (t_num >= t_size)
		{
		  t_size += OID_LIST_GROWTH;
		  o_list = (OID *) realloc (o_list, t_size * sizeof (OID));
		  r_list = (int *) realloc (r_list, t_size * sizeof (int));
		  if (!o_list || !r_list)
		    {
		      goto error;
		    }
		}
	    }			/* if (flat->info.name.db_object && ...) */
	}			/* for */
    }				/* for */

  *nump = t_num;
  *sizep = t_size;
  *oid_listp = o_list;
  *rep_listp = r_list;

  return t_num;

error:
  if (*oid_listp)
    {
      free_and_init (*oid_listp);
      *oid_listp = NULL;
    }

  if (*rep_listp)
    {
      free_and_init (*rep_listp);
      *rep_listp = NULL;
    }

  *nump = *sizep = 0;

  return -1;
}


/*
 * pt_make_aptr_parent_node () - Builds a BUILDLIST proc for the query node
 *  	and attaches it as the aptr to the xasl node. Attaches a list scan spec
 * 	for the xasl node from the aptr's list file
 *   return:
 *   parser(in):
 *   node(in): pointer to a query structure
 *   type(in):
 */
static XASL_NODE *
pt_make_aptr_parent_node (PARSER_CONTEXT * parser, PT_NODE * node,
			  PROC_TYPE type)
{
  XASL_NODE *aptr;
  XASL_NODE *xasl;
  REGU_VARIABLE_LIST regu_attributes;

  xasl = regu_xasl_node_alloc (type);
  if (xasl && node)
    {
      if (PT_IS_QUERY_NODE_TYPE (node->node_type))
	{
	  PT_NODE *namelist;

	  namelist = NULL;

	  aptr = parser_generate_xasl (parser, node);
	  if (aptr)
	    {
	      XASL_CLEAR_FLAG (aptr, XASL_TOP_MOST_XASL);

	      if (type == UPDATE_PROC)
		{
		  PT_NODE *col;

		  for (col = pt_get_select_list (parser, node);
		       col; col = col->next)
		    {
		      if (PT_IS_QUERY_NODE_TYPE (col->node_type))
			{
			  namelist = parser_append_node
			    (pt_point_l
			     (parser, pt_get_select_list (parser,
							  col)), namelist);
			}
		      else
			{
			  namelist =
			    parser_append_node (pt_point (parser, col),
						namelist);
			}
		    }
		}
	      else
		{
		  namelist = pt_get_select_list (parser, node);
		}
	      aptr->next = (XASL_NODE *) 0;
	      xasl->aptr_list = aptr;
	      xasl->val_list = pt_make_val_list (namelist);
	      if (xasl->val_list)
		{
		  int *attr_offsets;

		  attr_offsets = pt_make_identity_offsets (namelist);
		  regu_attributes = pt_to_position_regu_variable_list
		    (parser, namelist, xasl->val_list, attr_offsets);
		  free_and_init (attr_offsets);

		  if (regu_attributes)
		    {
		      xasl->spec_list = pt_make_list_access_spec
			(aptr, SEQUENTIAL, NULL, NULL, regu_attributes, NULL);
		    }
		}
	      else
		{
		  PT_ERRORm (parser, namelist, MSGCAT_SET_PARSER_SEMANTIC,
			     MSGCAT_SEMANTIC_OUT_OF_MEMORY);
		}

	      if (type == UPDATE_PROC && namelist)
		{
		  parser_free_tree (parser, namelist);
		}
	    }
	}
      else
	{
	  /* set the outptr and vall lists from a list of expressions */
	  xasl->outptr_list = pt_to_outlist (parser, node,
					     &xasl->selected_upd_list,
					     UNBOX_AS_VALUE);
	  if (xasl->outptr_list == NULL)
	    {
	      goto exit_on_error;
	    }

	  xasl->val_list = pt_make_val_list (node);
	  if (xasl->val_list == NULL)
	    {
	      PT_ERRORm (parser, node, MSGCAT_SET_PARSER_SEMANTIC,
			 MSGCAT_SEMANTIC_OUT_OF_MEMORY);
	    }
	}
    }

  if (pt_has_error (parser))
    {
      pt_report_to_ersys (parser, PT_SEMANTIC);
      goto exit_on_error;
    }

  return xasl;

exit_on_error:

  return NULL;
}


/*
 * pt_to_constraint_pred () - Builds predicate of NOT NULL conjuncts.
 * 	Then generates the corresponding XASL predicate
 *   return: NO_ERROR on success, non-zero for ERROR
 *   parser(in):
 *   xasl(in): value list contains the attributes the predicate must point to
 *   spec(in): spce that generated the list file for the above value list
 *   non_null_attrs(in): list of attributes to make into a constraint pred
 *   attr_list(in): corresponds to the list file's value list positions
 *   attr_offset(in): the additional offset into the value list. This is
 * 		      necessary because the update prepends 2 columns on
 * 		      the select list of the aptr query
 */
static int
pt_to_constraint_pred (PARSER_CONTEXT * parser, XASL_NODE * xasl,
		       PT_NODE * spec, PT_NODE * non_null_attrs,
		       PT_NODE * attr_list, int attr_offset)
{
  PT_NODE *pt_pred = NULL, *node, *conj, *next;
  PRED_EXPR *pred = NULL;

  assert (xasl != NULL && spec != NULL && parser != NULL);

  node = non_null_attrs;
  while (node)
    {
      /* we don't want a DAG so we need to NULL the next pointer as
       * we create a conjunct for each of the non_null_attrs.  Thus
       * we must save the next pointer for the loop.
       */
      next = node->next;
      node->next = NULL;
      if ((conj = parser_new_node (parser, PT_EXPR)) == NULL)
	{
	  goto outofmem;
	}

      conj->next = NULL;
      conj->line_number = node->line_number;
      conj->column_number = node->column_number;
      conj->type_enum = PT_TYPE_LOGICAL;
      conj->info.expr.op = PT_IS_NOT_NULL;
      conj->info.expr.arg1 = node;
      conj->next = pt_pred;
      pt_pred = conj;
      node = next;		/* go to the next node */
    }

  if ((parser->symbols = pt_symbol_info_alloc ()) == NULL)
    {
      goto outofmem;
    }

  parser->symbols->table_info = pt_make_table_info (parser, spec);
  parser->symbols->current_listfile = attr_list;
  parser->symbols->listfile_value_list = xasl->val_list;
  parser->symbols->listfile_attr_offset = attr_offset;

  pred = pt_to_pred_expr (parser, pt_pred);

  conj = pt_pred;
  while (conj)
    {
      conj->info.expr.arg1 = NULL;
      conj = conj->next;
    }
  if (pt_pred)
    {
      parser_free_tree (parser, pt_pred);
    }

  /* symbols are allocted with pt_alloc_packing_buf,
   * and freed at end of xasl generation. */
  parser->symbols = NULL;

  if (xasl->type == INSERT_PROC)
    {
      xasl->proc.insert.cons_pred = pred;
    }
  else if (xasl->type == UPDATE_PROC)
    {
      xasl->proc.update.cons_pred = pred;
    }
  else
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      return ER_GENERIC_ERROR;
    }

  return NO_ERROR;

outofmem:
  PT_ERRORm (parser, spec, MSGCAT_SET_PARSER_RUNTIME,
	     MSGCAT_RUNTIME_RESOURCES_EXHAUSTED);
  if (pt_pred)
    {
      parser_free_tree (parser, pt_pred);
    }

  return MSGCAT_RUNTIME_RESOURCES_EXHAUSTED;

}


/*
 * pt_to_insert_xasl () - Converts an insert parse tree to
 *                        an XASL graph for an insert
 *   return:
 *   parser(in): context
 *   statement(in): insert parse tree
 *   has_uniques(in):
 *   non_null_attrs(in):
 */
XASL_NODE *
pt_to_insert_xasl (PARSER_CONTEXT * parser, PT_NODE * statement,
		   int has_uniques, PT_NODE * non_null_attrs)
{
  XASL_NODE *xasl = NULL;
  INSERT_PROC_NODE *insert = NULL;
  PT_NODE *value_clause;
  PT_NODE *attr, *attrs, *list;
  MOBJ class_;
  OID *class_oid;
  DB_OBJECT *class_obj;
  HFID *hfid;
  int no_vals;
  int a;
  int error = NO_ERROR;
  PT_NODE *hint_arg;
  float waitsecs;

  assert (parser != NULL && statement != NULL);

  value_clause = statement->info.insert.value_clause;
  attrs = statement->info.insert.attr_list;

  class_obj = statement->info.insert.spec->
    info.spec.flat_entity_list->info.name.db_object;
  if ((class_ = locator_has_heap (class_obj)) == NULL ||
      (hfid = sm_heap (class_)) == NULL ||
      (locator_flush_class (class_obj) != NO_ERROR))
    {
      return NULL;
    }

  if (statement->info.insert.is_value == PT_IS_SUBQUERY)
    {
      list = pt_get_select_list (parser, value_clause);
      no_vals = pt_length_of_select_list (list, EXCLUDE_HIDDEN_COLUMNS);
    }
  else
    {
      list = value_clause;
      no_vals = pt_length_of_list (list);
    }

  xasl = pt_make_aptr_parent_node (parser, value_clause, INSERT_PROC);
  if (xasl)
    {
      insert = &xasl->proc.insert;
    }

  if (xasl)
    {
      insert->class_hfid = *hfid;
      class_oid = db_identifier (class_obj);
      if (class_oid)
	{
	  insert->class_oid = *class_oid;
	}
      else
	{
	  error = ER_HEAP_UNKNOWN_OBJECT;
	}
      insert->has_uniques = has_uniques;
      insert->waitsecs = XASL_WAITSECS_NOCHANGE;
      hint_arg = statement->info.insert.waitsecs_hint;
      if (statement->info.insert.hint & PT_HINT_LK_TIMEOUT &&
	  PT_IS_HINT_NODE (hint_arg))
	{
	  waitsecs = atof (hint_arg->info.name.original) * 1000;
	  insert->waitsecs =
	    (waitsecs >= -1) ? waitsecs : XASL_WAITSECS_NOCHANGE;
	}
      insert->no_logging = (statement->info.insert.hint & PT_HINT_NO_LOGGING);
      insert->release_lock = (statement->info.insert.hint & PT_HINT_REL_LOCK);

      if (error >= 0 && (no_vals > 0))
	{
	  insert->att_id = regu_int_array_alloc (no_vals);
	  if (insert->att_id)
	    {
	      for (attr = attrs, a = 0;
		   error >= 0 && a < no_vals; attr = attr->next, ++a)
		{
		  if ((insert->att_id[a] =
		       sm_att_id (class_obj, attr->info.name.original)) < 0)
		    {
		      error = er_errid ();
		    }
		}
	      insert->vals = NULL;
	      insert->no_vals = no_vals;
	    }
	  else
	    {
	      error = er_errid ();
	    }
	}
    }
  else
    {
      error = er_errid ();
    }

  if (error >= 0)
    {
      error = pt_to_constraint_pred (parser, xasl,
				     statement->info.insert.spec,
				     non_null_attrs, attrs, 0);
      insert->partition = NULL;
      if (statement->info.insert.spec->
	  info.spec.flat_entity_list->info.name.partition_of)
	{
	  error = do_build_partition_xasl (parser, xasl, class_obj, 0);
	}
    }

  if (pt_has_error (parser))
    {
      pt_report_to_ersys (parser, PT_EXECUTION);
      xasl = NULL;
    }

  /* fill in XASL cache related information */
  if (xasl)
    {
      OID *oid;

      /* OID of the user who is creating this XASL */
      oid = db_identifier (db_get_user ());
      if (oid != NULL)
	{
	  COPY_OID (&xasl->creator_oid, oid);
	}
      else
	{
	  OID_SET_NULL (&xasl->creator_oid);
	}

      /* We know that xasl->aptr_list is the XASL, which is generated by
         pt_make_aptr_parent_node() which calls pt_to_xasl(),
         of the SELECT query, which is statement->info.insert.value_clause.
         So, we move the XASL cache related information
         from the XASL of xasl->ptr to this top most XASL
         because xts_map_xasl_to_stream() will pack the information of
         the top most XASL into the XASL stream.
         As different from DELETE_PROC and UPDATE_PROC,
         insert->class_oid is diffrent from them of xasl->aptr SELECT.
         For example, INSERT into A SELECT * FROM B, C WHERE ... */

      /* list of class OIDs used in this XASL */
      xasl->n_oid_list = xasl->aptr_list->n_oid_list;
      xasl->aptr_list->n_oid_list = 0;
      xasl->class_oid_list = xasl->aptr_list->class_oid_list;
      xasl->aptr_list->class_oid_list = NULL;
      xasl->repr_id_list = xasl->aptr_list->repr_id_list;
      xasl->aptr_list->repr_id_list = NULL;
      xasl->dbval_cnt = xasl->aptr_list->dbval_cnt;
    }

  if (xasl)
    {
      xasl->qstmt = statement->alias_print;
    }

  return xasl;
}


/*
 * pt_to_upd_del_query () - Creates a query based on the given select list,
 * 	from list, and where clause
 *   return: PT_NODE *, query statement or NULL if error
 *   parser(in):
 *   select_list(in):
 *   from(in):
 *   class_specs(in):
 *   where(in):
 *   using_index(in):
 *   server_op(in):
 *
 * Note :
 * Prepends the class oid and the instance oid onto the select list for use
 * during the update or delete operation.
 * If the operation is a server side update, the prepended class oid is
 * put in the list file otherwise the class oid is a hidden column and
 * not put in the list file
 */
PT_NODE *
pt_to_upd_del_query (PARSER_CONTEXT * parser, PT_NODE * select_list,
		     PT_NODE * from, PT_NODE * class_specs,
		     PT_NODE * where, PT_NODE * using_index, int server_op)
{
  PT_NODE *statement = NULL;

  assert (parser != NULL);

  if ((statement = parser_new_node (parser, PT_SELECT)) != NULL)
    {
      statement->info.query.q.select.list =
	parser_copy_tree_list (parser, select_list);

      statement->info.query.q.select.from =
	parser_copy_tree_list (parser, from);
      statement->info.query.q.select.using_index =
	parser_copy_tree_list (parser, using_index);

      /* add in the class specs to the spec list */
      statement->info.query.q.select.from =
	parser_append_node (parser_copy_tree_list (parser, class_specs),
			    statement->info.query.q.select.from);

      statement->info.query.q.select.where =
	parser_copy_tree_list (parser, where);

      /* add the class and instance OIDs to the select list */
      statement = pt_add_row_classoid_name (parser, statement, server_op);
      statement = pt_add_row_oid_name (parser, statement);
      statement->info.query.composite_locking = 1;
    }

  return statement;
}


/*
 * pt_to_delete_xasl () - Converts an delete parse tree to
 *                        an XASL graph for an delete
 *   return:
 *   parser(in): context
 *   statement(in): delete parse tree
 */
XASL_NODE *
pt_to_delete_xasl (PARSER_CONTEXT * parser, PT_NODE * statement)
{
  XASL_NODE *xasl = NULL;
  DELETE_PROC_NODE *delete_ = NULL;
  PT_NODE *aptr_statement = NULL;
  PT_NODE *from;
  PT_NODE *where;
  PT_NODE *using_index;
  PT_NODE *class_specs;
  PT_NODE *cl_name_node;
  HFID *hfid;
  OID *class_oid;
  DB_OBJECT *class_obj;
  int no_classes = 0, cl;
  int error = NO_ERROR;
  PT_NODE *hint_arg;
  float waitsecs;

  assert (parser != NULL && statement != NULL);

  from = statement->info.delete_.spec;
  where = statement->info.delete_.search_cond;
  using_index = statement->info.delete_.using_index;
  class_specs = statement->info.delete_.class_specs;

  if (from && from->node_type == PT_SPEC && from->info.spec.range_var)
    {
      if (((aptr_statement = pt_to_upd_del_query (parser, NULL,
						  from, class_specs,
						  where, using_index,
						  1)) == NULL) ||
	  ((aptr_statement = mq_translate (parser, aptr_statement)) == NULL)
	  || ((xasl = pt_make_aptr_parent_node (parser,
						aptr_statement,
						DELETE_PROC)) == NULL))
	{
	  error = er_errid ();
	}
    }

  if (aptr_statement)
    {
      parser_free_tree (parser, aptr_statement);
    }

  if (!statement->partition_pruned)
    {
      do_apply_partition_pruning (parser, statement);
    }

  if (xasl)
    {
      delete_ = &xasl->proc.delete_;

      for (no_classes = 0, cl_name_node = from->info.spec.flat_entity_list;
	   cl_name_node; cl_name_node = cl_name_node->next)
	{
	  no_classes++;
	}

      if ((delete_->class_oid = regu_oid_array_alloc (no_classes)) == NULL)
	{
	  error = er_errid ();
	}
      else if ((delete_->class_hfid =
		regu_hfid_array_alloc (no_classes)) == NULL)
	{
	  error = er_errid ();
	}

      for (cl = 0, cl_name_node = from->info.spec.flat_entity_list;
	   cl < no_classes && error >= 0;
	   ++cl, cl_name_node = cl_name_node->next)
	{
	  class_obj = cl_name_node->info.name.db_object;
	  class_oid = db_identifier (class_obj);
	  if (!class_oid)
	    {
	      error = ER_HEAP_UNKNOWN_OBJECT;
	    }
	  hfid = sm_get_heap (class_obj);
	  if (!hfid)
	    {
	      error = er_errid ();
	    }
	  else
	    {
	      delete_->class_oid[cl] = *class_oid;
	      delete_->class_hfid[cl] = *hfid;
	    }
	}

      if (error >= 0)
	{
	  delete_->no_classes = no_classes;
	}

      hint_arg = statement->info.delete_.waitsecs_hint;
      delete_->waitsecs = XASL_WAITSECS_NOCHANGE;
      if (statement->info.delete_.hint & PT_HINT_LK_TIMEOUT &&
	  PT_IS_HINT_NODE (hint_arg))
	{
	  waitsecs = atof (hint_arg->info.name.original) * 1000;
	  delete_->waitsecs =
	    (waitsecs >= -1) ? waitsecs : XASL_WAITSECS_NOCHANGE;
	}
      delete_->no_logging =
	(statement->info.delete_.hint & PT_HINT_NO_LOGGING);
      delete_->release_lock =
	(statement->info.delete_.hint & PT_HINT_REL_LOCK);
    }

  if (pt_has_error (parser) || error < 0)
    {
      pt_report_to_ersys (parser, PT_EXECUTION);
      xasl = NULL;
    }

  /* fill in XASL cache related information */
  if (xasl)
    {
      OID *oid;

      /* OID of the user who is creating this XASL */
      if ((oid = db_identifier (db_get_user ())) != NULL)
	{
	  COPY_OID (&xasl->creator_oid, oid);
	}
      else
	{
	  OID_SET_NULL (&xasl->creator_oid);
	}

      /* We know that xasl->aptr_list is the XASL, which is generated by
         pt_make_aptr_parent_node() which calls pt_to_xasl(),
         of the SELECT query, which is made by pt_to_upd_del_query().
         So, we move the XASL cache related information
         from the XASL of xasl->ptr to this top most XASL
         because xts_map_xasl_to_stream() will pack the information of
         the top most XASL into the XASL stream.
         You can easily find out that this DELETE_PROC XASL has
         the same information of delete_->no_classes and
         delete_->class_oid. */

      /* list of class OIDs used in this XASL */
      xasl->n_oid_list = xasl->aptr_list->n_oid_list;
      xasl->aptr_list->n_oid_list = 0;
      xasl->class_oid_list = xasl->aptr_list->class_oid_list;
      xasl->aptr_list->class_oid_list = NULL;
      xasl->repr_id_list = xasl->aptr_list->repr_id_list;
      xasl->aptr_list->repr_id_list = NULL;
      xasl->dbval_cnt = xasl->aptr_list->dbval_cnt;
    }
  if (xasl)
    {
      xasl->qstmt = statement->alias_print;
    }

  return xasl;
}


/*
 * pt_to_update_xasl () - Converts an update parse tree to
 * 			  an XASL graph for an update
 *   return:
 *   parser(in): context
 *   statement(in): update parse tree
 *   select_names(in):
 *   select_values(in):
 *   const_names(in):
 *   const_values(in):
 *   no_vals(in):
 *   no_consts(in):
 *   has_uniques(in):
 *   non_null_attrs(in):
 */
XASL_NODE *
pt_to_update_xasl (PARSER_CONTEXT * parser, PT_NODE * statement,
		   PT_NODE * select_names, PT_NODE * select_values,
		   PT_NODE * const_names, PT_NODE * const_values,
		   int no_vals, int no_consts, int has_uniques,
		   PT_NODE ** non_null_attrs)
{
  XASL_NODE *xasl = NULL;
  UPDATE_PROC_NODE *update = NULL;
  PT_NODE *aptr_statement = NULL;
  PT_NODE *cl_name_node;
  int no_classes;
  PT_NODE *from;
  PT_NODE *where;
  PT_NODE *using_index;
  PT_NODE *class_specs;
  int cl;
  int error = NO_ERROR;
  int a;
  int v;
  PT_NODE *att_name_node;
  PT_NODE *value_node;
  DB_VALUE *val;
  DB_ATTRIBUTE *attr;
  DB_DOMAIN *dom;
  OID *class_oid;
  DB_OBJECT *class_obj;
  HFID *hfid;
  PT_NODE *hint_arg;
  float waitsecs;

  assert (parser != NULL && statement != NULL);

  from = statement->info.update.spec;
  where = statement->info.update.search_cond;
  using_index = statement->info.update.using_index;
  class_specs = statement->info.update.class_specs;
  cl_name_node = from->info.spec.flat_entity_list;

  while (cl_name_node)
    {
      if (locator_flush_class (cl_name_node->info.name.db_object) != NO_ERROR)
	{
	  return NULL;
	}
      cl_name_node = cl_name_node->next;
    }

  if (from && from->node_type == PT_SPEC && from->info.spec.range_var)
    {
      if (((aptr_statement = pt_to_upd_del_query (parser, select_values,
						  from, class_specs,
						  where, using_index,
						  1)) == NULL) ||
	  ((aptr_statement = mq_translate (parser, aptr_statement)) == NULL)
	  || ((xasl = pt_make_aptr_parent_node (parser, aptr_statement,
						UPDATE_PROC)) == NULL))
	{
	  error = er_errid ();
	  if (error == NO_ERROR)
	    {
	      error = ER_GENERIC_ERROR;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
	    }
	}
    }

  if (!statement->partition_pruned)
    {
      do_apply_partition_pruning (parser, statement);
    }

  cl_name_node = from->info.spec.flat_entity_list;
  no_classes = 0;
  while (cl_name_node)
    {
      ++no_classes;
      if (locator_flush_class (cl_name_node->info.name.db_object) != NO_ERROR)
	{
	  return NULL;
	}
      cl_name_node = cl_name_node->next;
    }

  if (xasl)
    {
      update = &xasl->proc.update;
    }

  if (xasl)
    {
      update->class_oid = regu_oid_array_alloc (no_classes);
      if (update->class_oid == NULL)
	{
	  error = er_errid ();
	}
      else if ((update->class_hfid =
		regu_hfid_array_alloc (no_classes)) == NULL)
	{
	  error = er_errid ();
	}
      else if ((update->att_id =
		regu_int_array_alloc (no_classes * no_vals)) == NULL)
	{
	  error = er_errid ();
	}
      else if ((update->partition =
		regu_partition_array_alloc (no_classes)) == NULL)
	{
	  error = er_errid ();
	}

      for (cl = 0, cl_name_node = from->info.spec.flat_entity_list;
	   cl < no_classes && error >= 0;
	   ++cl, cl_name_node = cl_name_node->next)
	{
	  class_obj = cl_name_node->info.name.db_object;
	  class_oid = db_identifier (class_obj);
	  if (!class_oid)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_HEAP_UNKNOWN_OBJECT, 3, 0, 0, 0);
	      error = ER_HEAP_UNKNOWN_OBJECT;
	    }
	  hfid = sm_get_heap (class_obj);
	  if (!hfid)
	    {
	      error = er_errid ();
	    }
	  else
	    {
	      update->class_oid[cl] = *class_oid;
	      update->class_hfid[cl] = *hfid;
	      for (a = 0, v = 0, att_name_node = const_names,
		   value_node = const_values;
		   error >= 0 && att_name_node;
		   ++a, ++v, att_name_node = att_name_node->next,
		   value_node = value_node->next)
		{
		  if ((update->att_id[cl * no_vals + a] =
		       sm_att_id (class_obj,
				  att_name_node->info.name.original)) < 0)
		    {
		      error = er_errid ();
		    }
		}
	      for (att_name_node = select_names;
		   error >= 0 && att_name_node;
		   ++a, att_name_node = att_name_node->next)
		{
		  if ((update->att_id[cl * no_vals + a] =
		       sm_att_id (class_obj,
				  att_name_node->info.name.original)) < 0)
		    {
		      error = er_errid ();
		    }
		}
	      update->partition[cl] = NULL;
	      if (cl_name_node->info.name.partition_of)
		{
		  error = do_build_partition_xasl (parser, xasl,
						   class_obj, cl + 1);
		}
	    }
	}
    }

  if (xasl && error >= 0)
    {
      update->no_classes = no_classes;
      update->no_vals = no_vals;
      update->no_consts = no_consts;
      update->consts = regu_dbvalptr_array_alloc (no_consts);
      update->has_uniques = has_uniques;
      update->waitsecs = XASL_WAITSECS_NOCHANGE;
      hint_arg = statement->info.update.waitsecs_hint;
      if (statement->info.update.hint & PT_HINT_LK_TIMEOUT &&
	  PT_IS_HINT_NODE (hint_arg))
	{
	  waitsecs = atof (hint_arg->info.name.original) * 1000;
	  update->waitsecs =
	    (waitsecs >= -1) ? waitsecs : XASL_WAITSECS_NOCHANGE;
	}
      update->no_logging = (statement->info.update.hint & PT_HINT_NO_LOGGING);
      update->release_lock = (statement->info.update.hint & PT_HINT_REL_LOCK);

      if (update->consts)
	{
	  class_obj = from->info.spec.flat_entity_list->info.name.db_object;

	  /* constants are recorded first because the server will
	     append selected values after the constants per instance */
	  for (a = 0, v = 0, att_name_node = const_names,
	       value_node = const_values;
	       error >= 0 && att_name_node;
	       ++a, ++v, att_name_node = att_name_node->next,
	       value_node = value_node->next)
	    {
	      val = pt_value_to_db (parser, value_node);
	      if (val)
		{
		  PT_NODE *node, *prev, *next;

		  /* Check to see if this is a NON NULL attr.  If so,
		   * check if the value is NULL.  If so, return
		   * constraint error, else remove the attr from
		   * the non_null_attrs list
		   * since we won't have to check for it.
		   */
		  prev = NULL;
		  for (node = *non_null_attrs; node; node = next)
		    {
		      next = node->next;

		      if (!pt_name_equal (parser, node, att_name_node))
			{
			  prev = node;
			  continue;
			}

		      if (DB_IS_NULL (val))
			{
			  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
				  ER_OBJ_ATTRIBUTE_CANT_BE_NULL, 1,
				  att_name_node->info.name.original);
			  error = ER_OBJ_ATTRIBUTE_CANT_BE_NULL;
			}
		      else
			{
			  /* remove the node from the non_null_attrs list since
			   * we've already checked that the attr will be
			   * non-null and the engine need not check again. */
			  if (!prev)
			    {
			      *non_null_attrs = (*non_null_attrs)->next;
			    }
			  else
			    {
			      prev->next = node->next;
			    }

			  /* free the node */
			  node->next = NULL;	/* cut-off link */
			  parser_free_tree (parser, node);
			}
		      break;
		    }

		  if (error < 0)
		    {
		      break;
		    }

		  if ((update->consts[v] = regu_dbval_alloc ())
		      && (attr = db_get_attribute
			  (class_obj, att_name_node->info.name.original))
		      && (dom = db_attribute_domain (attr)))
		    {
		      if (tp_value_coerce (val, update->consts[v], dom)
			  != DOMAIN_COMPATIBLE)
			{
			  error = ER_OBJ_DOMAIN_CONFLICT;
			  er_set (ER_ERROR_SEVERITY, __FILE__,
				  __LINE__, error, 1,
				  att_name_node->info.name.original);
			}
		    }
		  else
		    {
		      error = er_errid ();
		    }
		}
	      else
		{
		  error = ER_GENERIC_ERROR;
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			  ER_GENERIC_ERROR, 0);
		}
	    }
	}
      else
	{
	  if (no_consts)
	    {
	      error = er_errid ();
	    }
	}
    }

  if (error >= 0)
    {
      error = pt_to_constraint_pred (parser, xasl,
				     statement->info.update.spec,
				     *non_null_attrs, select_names, 2);
    }

  if (aptr_statement)
    {
      parser_free_tree (parser, aptr_statement);
    }

  if (pt_has_error (parser))
    {
      pt_report_to_ersys (parser, PT_EXECUTION);
      xasl = NULL;
    }
  else if (error < 0)
    {
      xasl = NULL;
    }

  /* fill in XASL cache related information */
  if (xasl)
    {
      OID *oid;

      /* OID of the user who is creating this XASL */
      if ((oid = db_identifier (db_get_user ())) != NULL)
	{
	  COPY_OID (&xasl->creator_oid, oid);
	}
      else
	{
	  OID_SET_NULL (&xasl->creator_oid);
	}

      /* We know that xasl->aptr_list is the XASL, which is generated by
         pt_make_aptr_parent_node() which calls pt_to_xasl(),
         of the SELECT query, which is made by pt_to_upd_del_query().
         So, we move the XASL cache related information
         from the XASL of xasl->ptr to this top most XASL
         because xts_map_xasl_to_stream() will pack the information of
         the top most XASL into the XASL stream.
         You can easily find out that this UPDATE_PROC XASL has
         the same information of update->no_classes and
         update->class_oid. */

      /* list of class OIDs used in this XASL */
      xasl->n_oid_list = xasl->aptr_list->n_oid_list;
      xasl->aptr_list->n_oid_list = 0;
      xasl->class_oid_list = xasl->aptr_list->class_oid_list;
      xasl->aptr_list->class_oid_list = NULL;
      xasl->repr_id_list = xasl->aptr_list->repr_id_list;
      xasl->aptr_list->repr_id_list = NULL;
      xasl->dbval_cnt = xasl->aptr_list->dbval_cnt;
    }

  if (xasl)
    {
      xasl->qstmt = statement->alias_print;
    }

  return xasl;
}


/*
 * pt_to_xasl_pre () - builds xasl for query nodes,
 *                     and remembers uncorrelated queries
 *   return:
 *   parser(in):
 *   node(in):
 *   arg(in):
 *   continue_walk(in):
 */
static PT_NODE *
pt_to_xasl_pre (PARSER_CONTEXT * parser, PT_NODE * node,
		void *arg, int *continue_walk)
{
  *continue_walk = PT_CONTINUE_WALK;

  if (parser->abort)
    {
      *continue_walk = PT_STOP_WALK;
      return (node);
    }

  switch (node->node_type)
    {
    case PT_SELECT:
#if defined(CUBRID_DEBUG)
      PT_NODE_PRINT_TO_ALIAS (parser, node, PT_CONVERT_RANGE);
#endif /* CUBRID_DEBUG */

      /* fall through */
    case PT_UNION:
    case PT_DIFFERENCE:
    case PT_INTERSECTION:
      if (!node->info.query.xasl)
	{
	  (void) pt_query_set_reference (parser, node);
	  pt_push_symbol_info (parser, node);
	}
      break;

    default:
      break;
    }

  if (parser->error_msgs || er_errid () == ER_LK_UNILATERALLY_ABORTED)
    {
      *continue_walk = PT_STOP_WALK;
    }

  return node;
}


/*
 * pt_to_xasl_post () - builds xasl for query nodes
 *   return:
 *   parser(in):
 *   node(in):
 *   arg(in):
 *   continue_walk(in/out):
 */
static PT_NODE *
pt_to_xasl_post (PARSER_CONTEXT * parser, PT_NODE * node,
		 void *arg, int *continue_walk)
{
  XASL_NODE *xasl;
  XASL_SUPP_INFO *info = (XASL_SUPP_INFO *) arg;

  *continue_walk = PT_CONTINUE_WALK;

  if (parser->abort)
    {
      *continue_walk = PT_STOP_WALK;
      return (node);
    }

  if (node && node->info.query.xasl == NULL)
    {
      switch (node->node_type)
	{
	case PT_SELECT:
	case PT_UNION:
	case PT_DIFFERENCE:
	case PT_INTERSECTION:
	  /* build XASL for the query */
	  xasl = pt_to_xasl_proc (parser, node, info->query_list);
	  pt_pop_symbol_info (parser);
	  if (node->node_type == PT_SELECT)
	    {
	      /* fill in XASL cache related information;
	         list of class OIDs used in this XASL */
	      if (xasl &&
		  pt_spec_to_xasl_class_oid_list (node->info.query.q.select.
						  from,
						  &(info->class_oid_list),
						  &(info->repr_id_list),
						  &(info->n_oid_list),
						  &(info->oid_list_size)) < 0)
		{
		  /* might be memory allocation error */
		  PT_INTERNAL_ERROR (parser, "generate xasl");
		  xasl = NULL;
		}
	    }
	  break;
	default:
	  break;
	}			/* siwtch (node->type) */
    }

  if (parser->error_msgs || er_errid () == ER_LK_UNILATERALLY_ABORTED)
    {
      *continue_walk = PT_STOP_WALK;
    }

  return node;
}


/*
 * pt_to_xasl () - Creates xasl proc for parse tree.
 *   return:
 *   parser(in):
 *   node(in): pointer to a query structure
 */
XASL_NODE *
parser_generate_xasl (PARSER_CONTEXT * parser, PT_NODE * node)
{
  XASL_NODE *xasl = NULL;
  PT_NODE *next;
  struct start_proc *saved_start_list;
  XASL_NODE *saved_read_list;

  assert (parser != NULL && node != NULL);

  next = node->next;
  node->next = NULL;
  saved_start_list = parser->start_list;
  saved_read_list = parser->read_list;
  parser->start_list = NULL;
  parser->read_list = NULL;
  parser->dbval_cnt = 0;

  node = parser_walk_tree (parser, node,
			   pt_pruning_and_flush_class_and_null_xasl, NULL,
			   NULL, NULL);

  /* during above parser_walk_tree, the request to get a driver may cause
     deadlock, then we should exit following steps and propages
     the error messages */
  if (parser->abort || !node)
    {
      return NULL;
    }

  switch (node->node_type)
    {
    case PT_SELECT:
    case PT_UNION:
    case PT_DIFFERENCE:
    case PT_INTERSECTION:
      /* do not treat the top level like a subquery, even if it
       * is a subquery with respect to something else (eg insert). */
      node->info.query.is_subquery = (PT_MISC_TYPE) 0;

      /* translate methods in queries to our internal form */
      if (node)
	{
	  node = meth_translate (parser, node);
	}

      if (node)
	{
	  /* add dummy node at the head of list */
	  xasl_supp_info.query_list = parser_new_node (parser, PT_SELECT);
	  xasl_supp_info.query_list->info.query.xasl = NULL;

	  /* XASL cache related information */
	  xasl_supp_info.class_oid_list = NULL;
	  xasl_supp_info.repr_id_list = NULL;
	  xasl_supp_info.n_oid_list = xasl_supp_info.oid_list_size = 0;

	  node = parser_walk_tree (parser, node, pt_to_xasl_pre, NULL,
				   pt_to_xasl_post, &xasl_supp_info);

	  parser_free_tree (parser, xasl_supp_info.query_list);
	  xasl_supp_info.query_list = NULL;
	}

      if (node && !parser->error_msgs)
	{
	  node->next = next;
	  xasl = (XASL_NODE *) node->info.query.xasl;
	}
      break;

    default:
      break;
    }

  if (xasl)
    {
      xasl->start_list = parser->start_list;
      /*
       * Be sure to append here, and do so in the right order:  the
       * optimizer may have stuck some things on xasl->aptr_list, and
       * they may refer to derived tables that live on aptr.  Nothing
       * on aptr could possibly refer to anything that the optimizer
       * created, so always put the aptr things in front of the
       * optimizer things.
       */
      xasl->aptr_list = pt_append_xasl (parser->read_list, xasl->aptr_list);
      xasl->aptr_list = pt_remove_xasl (xasl->aptr_list, xasl);
    }
  else
    {
      PT_INTERNAL_ERROR (parser, "generate xasl");
    }

  /* fill in XASL cache related information */
  if (xasl)
    {
      OID *oid;
      int n;
      DB_OBJECT *user = NULL;

      /* OID of the user who is creating this XASL */
      if ((user = db_get_user ()) != NULL
	  && (oid = db_identifier (user)) != NULL)
	{
	  COPY_OID (&xasl->creator_oid, oid);
	}
      else
	{
	  OID_SET_NULL (&xasl->creator_oid);
	}

      /* list of class OIDs used in this XASL */
      xasl->n_oid_list = 0;
      xasl->class_oid_list = NULL;
      xasl->repr_id_list = NULL;

      if ((n = xasl_supp_info.n_oid_list) > 0 &&
	  (xasl->class_oid_list = regu_oid_array_alloc (n)) &&
	  (xasl->repr_id_list = regu_int_array_alloc (n)))
	{
	  xasl->n_oid_list = n;
	  (void) memcpy (xasl->class_oid_list,
			 xasl_supp_info.class_oid_list, sizeof (OID) * n);
	  (void) memcpy (xasl->repr_id_list,
			 xasl_supp_info.repr_id_list, sizeof (int) * n);
	}

      xasl->dbval_cnt = parser->dbval_cnt;
    }				/* if (xasl) */

  /* free what were allocated in pt_spec_to_xasl_class_oid_list() */
  if (xasl_supp_info.class_oid_list)
    {
      free_and_init (xasl_supp_info.class_oid_list);
    }
  if (xasl_supp_info.repr_id_list)
    {
      free_and_init (xasl_supp_info.repr_id_list);
    }
  xasl_supp_info.class_oid_list = NULL;
  xasl_supp_info.repr_id_list = NULL;
  xasl_supp_info.n_oid_list = xasl_supp_info.oid_list_size = 0;

  if (xasl)
    {
      xasl->qstmt = node->alias_print;
      XASL_SET_FLAG (xasl, XASL_TOP_MOST_XASL);
    }

  {
    if (PRM_XASL_DEBUG_DUMP)
      {
	if (xasl)
	  {
	    if (xasl->qstmt == NULL)
	      {
		PT_NODE_PRINT_TO_ALIAS (parser, node, 0);
		xasl->qstmt = node->alias_print;
	      }
	    qdump_print_xasl (xasl);
	  }
	else
	  {
	    printf ("<NULL XASL generation>\n");
	  }
      }
  }

  /* restore parser->start_list and ->read_list */
  parser->start_list = saved_start_list;
  parser->read_list = saved_read_list;

  return xasl;
}
