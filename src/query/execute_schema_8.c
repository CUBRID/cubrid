/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * do_partition.c - Code for partition
 * TODO: rename ??? merge???
 */

#ident "$Id$"

#include "config.h"

#include <stdarg.h>
#include <ctype.h>

#include "error_manager.h"
#include "parser.h"
#include "dbi.h"
#include "xasl_generation_2.h"
#include "msgexec.h"
#include "semantic_check.h"
#include "memory_manager_2.h"
#include "system_parameter.h"
#include "schema_manager_3.h"
#include "transform.h"
#include "set_object_1.h"
#include "object_accessor.h"
#include "memory_hash.h"
#include "transaction_cl.h"
#include "execute_schema_2.h"
#include "execute_schema_8.h"
#include "parser.h"
#include "locator_cl.h"
#include "qp_mem.h"
#include "dbval.h"

enum ATTR_FOUND
{
  PATTR_NOT_FOUND = 0,
  PATTR_NAME,
  PATTR_VALUE,
  PATTR_NAME_VALUE,
  PATTR_COLUMN = 4,
  PATTR_KEY = 8
};

typedef struct part_class_info PART_CLASS_INFO;
struct part_class_info
{
  char *pname;
  DB_CTMPL *temp;
  DB_OBJECT *obj;
  PART_CLASS_INFO *next;
};

typedef struct pruning_info PRUNING_INFO;
struct pruning_info
{
  PARSER_CONTEXT *parser;
  PT_NODE *expr;
  DB_VALUE *attr;
  PT_NODE *ppart;		/* PT_NAME, original, db_object, location(temporary use) */
  SM_CLASS *smclass;
  int type, size, wrkmap, expr_cnt, and_or;
  UINTPTR spec;
};

typedef struct db_value_slist DB_VALUE_SLIST;
struct db_value_slist
{
  struct db_value_slist *next;
  MOP partition_of;
  DB_VALUE *min;
  DB_VALUE *max;
};

typedef enum
{
  RANGES_LESS = -1,		/* less than */
  RANGES_EQUAL = 0,		/* equal */
  RANGES_GREATER = 1,		/* greater than */
  RANGES_ERROR = 2		/* error */
} MERGE_CHECK_RESULT;

static int insert_partition_catalog (PARSER_CONTEXT * parser,
				     DB_CTMPL * clstmpl, PT_NODE * node,
				     char *base_obj, char *cata_obj,
				     DB_VALUE * minval);
static PT_NODE *replace_name_with_value (PARSER_CONTEXT * parser,
					 PT_NODE * node, void *void_arg,
					 int *continue_walk);
static PT_NODE *adjust_name_with_type (PARSER_CONTEXT * parser,
				       PT_NODE * node, void *void_arg,
				       int *continue_walk);
static DB_VALUE *evaluate_partition_expr (DB_VALUE * expr, DB_VALUE * ival);
static int apply_partition_list_search (SM_CLASS * smclass, DB_VALUE * sval,
					char *retbuf);
static int apply_partition_range_search (SM_CLASS * smclass, DB_VALUE * sval,
					 char *retbuf);
static PT_NODE *find_partition_attr (PARSER_CONTEXT * parser, PT_NODE * node,
				     void *void_arg, int *continue_walk);
static int check_same_expr (PARSER_CONTEXT * parser, PT_NODE * p,
			    PT_NODE * q);
static int evaluate_partition_range (PARSER_CONTEXT * parser, PT_NODE * expr);
static PT_NODE *conver_expr_to_constant (PARSER_CONTEXT * parser,
					 PT_NODE * node, void *void_arg,
					 int *continue_walk);
static PT_NODE *get_pruned_partition_spec (PRUNING_INFO * ppi, MOP subobj);
static void add_pruned_partition_part (PT_NODE * subspec, PRUNING_INFO * ppi,
				       MOP subcls, char *cname);
static int adjust_pruned_partition (PT_NODE * spec, PRUNING_INFO * ppi);
static int increase_value (DB_VALUE * val);
static int decrease_value (DB_VALUE * val);
static int check_hash_range (PRUNING_INFO * ppi, char *partmap,
			     PT_OP_TYPE op, PT_NODE * from_expr,
			     PT_NODE * to_expr, int setval);
static int select_hash_partition (PRUNING_INFO * ppi, PT_NODE * expr);
static int select_range_partition (PRUNING_INFO * ppi, PT_NODE * expr);
static int select_list_partition (PRUNING_INFO * ppi, PT_NODE * expr);
static bool select_range_list (PRUNING_INFO * ppi, PT_NODE * cond);
static bool make_attr_search_value (int and_or, PT_NODE * incond,
				    PRUNING_INFO * ppi);
static PT_NODE *apply_no_pruning (PT_NODE * spec, PRUNING_INFO * ppi);
static MERGE_CHECK_RESULT check_range_merge (DB_VALUE * val1, PT_OP_TYPE op1,
					     DB_VALUE * val2, PT_OP_TYPE op2);
static int is_ranges_meetable (DB_VALUE * aval1, PT_OP_TYPE aop1,
			       DB_VALUE * aval2, PT_OP_TYPE aop2,
			       DB_VALUE * bval1, PT_OP_TYPE bop1,
			       DB_VALUE * bval2, PT_OP_TYPE bop2);
static int is_in_range (DB_VALUE * aval1, PT_OP_TYPE aop1,
			DB_VALUE * aval2, PT_OP_TYPE aop2, DB_VALUE * bval);
static int adjust_partition_range (DB_OBJLIST * objs);
static int adjust_partition_size (MOP class_);

/*
 * do_create_partition() -  Creates partitions
 *   return: Error code if partitions are not created
 *   parser(in): Parser context
 *   node(in): The parse tree of a create class
 *   class_obj(in):
 *   clstmpl(in):
 *
 * Note:
 */
int
do_create_partition (PARSER_CONTEXT * parser, PT_NODE * node,
		     DB_OBJECT * class_obj, DB_CTMPL * clstmpl)
{
  int error;
  PT_NODE *pinfo, *hash_parts = NULL, *newparts, *hashtail;
  PT_NODE *parts, *parts_save, *fmin;
  PT_NODE *parttemp, *names;
  PART_CLASS_INFO pci = { NULL, NULL, NULL, NULL };
  PART_CLASS_INFO *newpci, *wpci;
  char class_name[DB_MAX_IDENTIFIER_LENGTH];
  DB_VALUE *minval, *parts_val, *fmin_val, partsize, delval;
  int part_cnt = 0, part_add = -1;
  int size;
  int save;
  SM_CLASS *smclass;

  if (node->node_type == PT_ALTER)
    {
      pinfo = node->info.alter.alter_clause.partition.info;
      if (node->info.alter.code == PT_ADD_PARTITION
	  || node->info.alter.code == PT_REORG_PARTITION)
	{
	  parts = node->info.alter.alter_clause.partition.parts;
	  part_add = parts->info.parts.type;
	}
      else if (node->info.alter.code == PT_ADD_HASHPARTITION)
	{
	  part_add = PT_PARTITION_HASH;
	}
      intl_mbs_lower ((char *) node->info.alter.entity_name->info.name.
		      original, class_name);
    }
  else if (node->node_type == PT_CREATE_ENTITY)
    {
      pinfo = node->info.create_entity.partition_info;
      intl_mbs_lower ((char *) node->info.create_entity.entity_name->info.
		      name.original, class_name);
    }
  else
    {
      return NO_ERROR;
    }

  if (part_add == -1)
    {				/* create or apply partiiton */
      if (!pinfo)
	{
	  return NO_ERROR;
	}

      parts = pinfo->info.partition.parts;
    }

  parts_save = parts;
  parttemp = parser_new_node (parser, PT_CREATE_ENTITY);
  if (parttemp == NULL)
    {
    fail_return:
      error = er_errid ();
      goto end_create;
    }

  error = au_fetch_class (class_obj, &smclass, AU_FETCH_READ, AU_SELECT);
  if (error != NO_ERROR)
    {
      goto fail_return;
    }

  parttemp->info.create_entity.entity_type = PT_CLASS;
  parttemp->info.create_entity.entity_name =
    parser_new_node (parser, PT_NAME);
  parttemp->info.create_entity.supclass_list =
    parser_new_node (parser, PT_NAME);
  if (parttemp->info.create_entity.entity_name == NULL
      || parttemp->info.create_entity.supclass_list == NULL)
    {
      goto fail_return;
    }
  parttemp->info.create_entity.supclass_list->info.name.db_object = class_obj;

  error = NO_ERROR;
  if (part_add == PT_PARTITION_HASH
      || (pinfo && pinfo->node_type != PT_VALUE
	  && pinfo->info.partition.type == PT_PARTITION_HASH))
    {
      int pi, org_hashsize, new_hashsize;

      hash_parts = parser_new_node (parser, PT_PARTS);
      if (hash_parts == NULL)
	{
	  goto fail_return;
	}
      hash_parts->info.parts.name = parser_new_node (parser, PT_NAME);
      if (hash_parts->info.parts.name == NULL)
	{
	  goto fail_return;
	}

      hash_parts->info.parts.type = PT_PARTITION_HASH;
      if (part_add == PT_PARTITION_HASH)
	{
	  org_hashsize = do_get_partition_size (class_obj);
	  if (org_hashsize < 0)
	    {
	      goto fail_return;
	    }
	  new_hashsize
	    = node->info.alter.alter_clause.partition.size->info.value.
	    data_value.i;
	}
      else
	{
	  org_hashsize = 0;
	  new_hashsize =
	    pinfo->info.partition.hashsize->info.value.data_value.i;
	}

      for (pi = 0; pi < new_hashsize; pi++)
	{
	  newpci = (PART_CLASS_INFO *) malloc (sizeof (PART_CLASS_INFO));
	  if (newpci == NULL)
	    {
	      goto fail_return;
	    }

	  memset (newpci, 0x0, sizeof (PART_CLASS_INFO));

	  newpci->next = pci.next;
	  pci.next = newpci;

	  newpci->pname = (char *) malloc (strlen (class_name) + 5 + 13);
	  if (newpci->pname == NULL)
	    {
	      goto fail_return;
	    }

	  sprintf (newpci->pname, "%s" PARTITIONED_SUB_CLASS_TAG "p%d",
		   class_name, pi + org_hashsize);
	  if (strlen (newpci->pname) >= PARTITION_VARCHAR_LEN)
	    {
	      error = ER_INVALID_PARTITION_REQUEST;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
	      goto fail_return;
	    }
	  newpci->temp = dbt_create_class (newpci->pname);
	  if (newpci->temp == NULL)
	    {
	      goto fail_return;
	    }

	  parttemp->info.create_entity.entity_name->info.name.original
	    = newpci->pname;
	  parttemp->info.create_entity.supclass_list->info.name.original
	    = class_name;

	  error = do_create_local (parser, newpci->temp, parttemp);
	  if (error != NO_ERROR)
	    {
	      dbt_abort_class (newpci->temp);
	      goto end_create;
	    }

	  newpci->temp->partition_parent_atts = smclass->attributes;
	  newpci->obj = dbt_finish_class (newpci->temp);
	  if (newpci->obj == NULL)
	    {
	      dbt_abort_class(newpci->temp);
	      goto fail_return;
	    }

	  if (locator_has_heap (newpci->obj) == NULL)
	    {
	      goto fail_return;
	    }

	  hash_parts->info.parts.name->info.name.original
	    = strstr (newpci->pname, PARTITIONED_SUB_CLASS_TAG)
	    + strlen (PARTITIONED_SUB_CLASS_TAG);
	  hash_parts->info.parts.values = NULL;

	  error = insert_partition_catalog (parser, NULL, hash_parts,
					    class_name, newpci->pname, NULL);
	  if (error != NO_ERROR)
	    {
	      goto end_create;
	    }
	  if (part_add == PT_PARTITION_HASH)
	    {
	      hash_parts->next = NULL;
	      hash_parts->info.parts.name->info.name.db_object = newpci->obj;
	      newparts = parser_copy_tree (parser, hash_parts);
	      if (node->info.alter.alter_clause.partition.parts == NULL)
		{
		  node->info.alter.alter_clause.partition.parts = newparts;
		}
	      else
		{
		  hashtail->next = newparts;
		}
	      hashtail = newparts;
	    }
	  error = NO_ERROR;
	}
    }
  else
    {				/* RANGE or LIST */
      char *part_name;

      for (; parts; parts = parts->next, part_cnt++)
	{
	  newpci = (PART_CLASS_INFO *) malloc (sizeof (PART_CLASS_INFO));
	  if (newpci == NULL)
	    {
	      goto fail_return;
	    }

	  memset (newpci, 0x0, sizeof (PART_CLASS_INFO));

	  newpci->next = pci.next;
	  pci.next = newpci;

	  part_name = (char *) parts->info.parts.name->info.name.original;
	  size = strlen (class_name) + 5 + 1 + strlen (part_name);

	  newpci->pname = (char *) malloc (size);
	  if (newpci->pname == NULL)
	    {
	      goto fail_return;
	    }
	  sprintf (newpci->pname, "%s" PARTITIONED_SUB_CLASS_TAG "%s",
		   class_name, part_name);

	  if (strlen (newpci->pname) >= PARTITION_VARCHAR_LEN)
	    {
	      error = ER_INVALID_PARTITION_REQUEST;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
	      goto fail_return;
	    }

	  if (node->info.alter.code == PT_REORG_PARTITION
	      && parts->partition_pruned)
	    {			/* reused partition */
	      error = insert_partition_catalog (parser, NULL, parts,
						class_name,
						newpci->pname, NULL);
	      if (error != NO_ERROR)
		{
		  goto end_create;	/* reorg partition info update */
		}
	      error = NO_ERROR;
	      continue;
	    }

	  newpci->temp = dbt_create_class (newpci->pname);
	  if (newpci->temp == NULL)
	    {
	      goto fail_return;
	    }

	  parttemp->info.create_entity.entity_name->info.name.original
	    = newpci->pname;
	  parttemp->info.create_entity.supclass_list->info.name.original
	    = class_name;

	  error = do_create_local (parser, newpci->temp, parttemp);
	  if (error != NO_ERROR)
	    {
	      dbt_abort_class (newpci->temp);
	      goto end_create;
	    }

	  newpci->temp->partition_parent_atts = smclass->attributes;
	  newpci->obj = dbt_finish_class (newpci->temp);
	  if (newpci->obj == NULL)
	    {
	      dbt_abort_class (newpci->temp);
	      goto fail_return;
	    }

	  if (locator_has_heap (newpci->obj) == NULL
	      || locator_flush_class (newpci->obj) != NO_ERROR)
	    {
	      goto fail_return;
	    }

	  /* RANGE-MIN VALUE search */
	  minval = NULL;
	  if ((pinfo && pinfo->node_type != PT_VALUE
	       && pinfo->info.partition.type == PT_PARTITION_RANGE)
	      || part_add == PT_PARTITION_RANGE)
	    {
	      parts_val = pt_value_to_db (parser, parts->info.parts.values);
	      for (fmin = parts_save; fmin; fmin = fmin->next)
		{
		  if (fmin == parts)
		    {
		      continue;
		    }
		  if (fmin->info.parts.values == NULL)
		    {
		      continue;	/* RANGE-MAXVALUE */
		    }
		  fmin_val = pt_value_to_db (parser, fmin->info.parts.values);
		  if (fmin_val == NULL)
		    {
		      continue;
		    }
		  if (parts->info.parts.values == NULL
		      || db_value_compare (parts_val, fmin_val) == DB_GT)
		    {
		      if (minval == NULL)
			{
			  minval = fmin_val;
			}
		      else
			{
			  if (db_value_compare (minval, fmin_val) == DB_LT)
			    {
			      minval = fmin_val;
			    }
			}
		    }
		}
	    }
	  if (part_add == PT_PARTITION_RANGE
	      && minval == NULL && pinfo && pinfo->node_type == PT_VALUE)
	    {
	      /* set in pt_check_alter_partition */
	      minval = pt_value_to_db (parser, pinfo);
	    }
	  parts->info.parts.name->info.name.db_object = newpci->obj;
	  error = insert_partition_catalog (parser, NULL, parts,
					    class_name, newpci->pname,
					    minval);
	  if (error != NO_ERROR)
	    {
	      goto end_create;
	    }
	  error = NO_ERROR;
	}
    }

  if (part_add != -1)
    {				/* partition size update */
      adjust_partition_size (class_obj);

      if (node->info.alter.code == PT_REORG_PARTITION)
	{
	  AU_DISABLE (save);
	  db_make_string (&delval, "DEL");
	  for (names = node->info.alter.alter_clause.partition.name_list;
	       names; names = names->next)
	    {
	      if (names->partition_pruned)
		{		/* for delete partition */
		  error =
		    db_put_internal (names->info.name.db_object,
				     PARTITION_ATT_PEXPR, &delval);
		  if (error != NO_ERROR)
		    {
		      break;
		    }
		}
	    }
	  pr_clear_value (&delval);
	  AU_ENABLE (save);
	  if (error != NO_ERROR)
	    {
	      goto end_create;
	    }
	  if (part_add == PT_PARTITION_RANGE)
	    {
	      error
		= au_fetch_class (class_obj, &smclass, AU_FETCH_READ,
				  AU_SELECT);
	      if (error != NO_ERROR)
		{
		  goto end_create;
		}
	      adjust_partition_range (smclass->users);
	    }
	}
    }
  else
    {				/* set parent's partition info */
      db_make_int (&partsize, part_cnt);
      error = insert_partition_catalog (parser, clstmpl, pinfo, class_name,
					class_name, &partsize);
    }

end_create:
  for (wpci = pci.next; wpci;)
    {
      if (wpci->pname)
	{
	  free_and_init (wpci->pname);
	}
      newpci = wpci;
      wpci = wpci->next;
      free_and_init (newpci);
    }
  if (parttemp != NULL)
    {
      parser_free_tree (parser, parttemp);
    }
  if (error != NO_ERROR)
    {
      return error;
    }
  return NO_ERROR;
}

/*
 * insert_partition_catalog() -
 *   return: Error code
 *   parser(in): Parser context
 *   clstmpl(in/out): Template of sm_class
 *   node(in): Parser tree of an partition class
 *   base_obj(in): Partition class name
 *   cata_obj(in): Catalog class name
 *   minval(in):
 *
 * Note:
 */
static int
insert_partition_catalog (PARSER_CONTEXT * parser, DB_CTMPL * clstmpl,
			  PT_NODE * node, char *base_obj,
			  char *cata_obj, DB_VALUE * minval)
{
  MOP partcata, classcata, newpart, newclass;
  DB_OTMPL *otmpl;
  DB_CTMPL *ctmpl;
  DB_VALUE val, *ptval, *hashsize;
  PT_NODE *parts;
  char *query, *query_str = NULL, *p;
  DB_COLLECTION *dbc = NULL;
  int save;
  bool au_disable_flag = false;

  AU_DISABLE (save);
  au_disable_flag = true;

  classcata = sm_find_class (CT_CLASS_NAME);
  if (classcata == NULL)
    {
      goto fail_return;
    }
  db_make_varchar (&val, PARTITION_VARCHAR_LEN, base_obj, strlen (base_obj));
  newclass = db_find_unique (classcata, CLASS_ATT_NAME, &val);
  if (newclass == NULL)
    {
      goto fail_return;
    }
  pr_clear_value (&val);

  partcata = sm_find_class (PARTITION_CATALOG_CLASS);
  if (partcata == NULL)
    {
      goto fail_return;
    }
  otmpl = dbt_create_object_internal (partcata);
  if (otmpl == NULL)
    {
      goto fail_return;
    }
  db_make_object (&val, newclass);
  if (dbt_put_internal (otmpl, PARTITION_ATT_CLASSOF, &val) < 0)
    {
      goto fail_return;
    }
  pr_clear_value (&val);

  if (node->node_type == PT_PARTITION)
    {
      db_make_null (&val);
    }
  else
    {
      p = (char *) node->info.parts.name->info.name.original;
      db_make_varchar (&val, PARTITION_VARCHAR_LEN, p, strlen (p));
    }
  if (dbt_put_internal (otmpl, PARTITION_ATT_PNAME, &val) < 0)
    {
      goto fail_return;
    }
  pr_clear_value (&val);

  if (node->node_type == PT_PARTITION)
    {
      db_make_int (&val, node->info.partition.type);
    }
  else
    {
      db_make_int (&val, node->info.parts.type);
    }
  if (dbt_put_internal (otmpl, PARTITION_ATT_PTYPE, &val) < 0)
    {
      goto fail_return;
    }
  pr_clear_value (&val);

  if (node->node_type == PT_PARTITION)
    {
      query = parser_print_tree (parser, node->info.partition.expr);
      if (query == NULL)
	{
	  goto fail_return;
	}

      query_str = (char *) malloc (strlen (query) + strlen (base_obj) +
				   7 /* strlen("SELECT ") */  +
				   6 /* strlen(" FROM ") */  +
				   2 /* two \" */  +
				   1 /* terminating null */ );
      if (query_str == NULL)
	{
	  goto fail_return;
	}
      sprintf (query_str, "SELECT %s FROM \"%s\"", query, base_obj);
      db_make_varchar (&val, PARTITION_VARCHAR_LEN, query_str,
		       strlen (query_str));
    }
  else
    {
      db_make_null (&val);
    }
  if (dbt_put_internal (otmpl, PARTITION_ATT_PEXPR, &val) < 0)
    {
      goto fail_return;
    }
  pr_clear_value (&val);
  if (query_str)
    {
      free_and_init (query_str);
    }

  dbc = set_create_sequence (0);
  if (dbc == NULL)
    {
      goto fail_return;
    }
  if (node->node_type == PT_PARTITION)
    {
      p = (char *) node->info.partition.keycol->info.name.original;
      db_make_varchar (&val, PARTITION_VARCHAR_LEN, p, strlen (p));
      set_add_element (dbc, &val);
      if (node->info.partition.type == PT_PARTITION_HASH)
	{
	  hashsize = pt_value_to_db (parser, node->info.partition.hashsize);
	  set_add_element (dbc, hashsize);
	}
      else
	{
	  set_add_element (dbc, minval);
	}
    }
  else
    {
      if (node->info.parts.type == PT_PARTITION_RANGE)
	{
	  if (minval == NULL)
	    {
	      db_make_null (&val);
	      set_add_element (dbc, &val);
	    }
	  else
	    {
	      set_add_element (dbc, minval);
	    }
	}
      if (node->info.parts.values == NULL)
	{			/* RANGE-MAXVALUE */
	  db_make_null (&val);
	  set_add_element (dbc, &val);
	}
      else
	{
	  for (parts = node->info.parts.values; parts; parts = parts->next)
	    {
	      ptval = pt_value_to_db (parser, parts);
	      if (ptval == NULL)
		{
		  goto fail_return;
		}
	      set_add_element (dbc, ptval);
	    }
	}
    }
  db_make_sequence (&val, dbc);
  if (dbt_put_internal (otmpl, PARTITION_ATT_PVALUES, &val) < 0)
    {
      goto fail_return;
    }
  newpart = dbt_finish_object (otmpl);
  if (newpart == NULL)
    {
      goto fail_return;
    }

  /* SM_CLASS's partition_of update */
  if (clstmpl)
    {
      clstmpl->partition_of = newpart;
    }
  else
    {
      newclass = sm_find_class (cata_obj);
      if (newclass == NULL)
	{
	  goto fail_return;
	}
      ctmpl = dbt_edit_class (newclass);
      if (ctmpl == NULL)
	{
	  goto fail_return;
	}
      ctmpl->partition_of = newpart;
      if (dbt_finish_class (ctmpl) == NULL)
	{
	  dbt_abort_class (ctmpl);
	  goto fail_return;
	}
    }

  AU_ENABLE (save);
  au_disable_flag = false;
  set_free (dbc);
  return NO_ERROR;

fail_return:
  if (au_disable_flag == true)
    {
      AU_ENABLE (save);
    }
  if (dbc != NULL)
    {
      set_free (dbc);
    }
  return er_errid ();
}

/*
 * replace_name_with_value() -
 *   return: PT_NODE pointer
 *   parser(in): Parser context
 *   node(in):
 *   void_arg(in):
 *   continue_walk(in):
 *
 * Note:
 */

static PT_NODE *
replace_name_with_value (PARSER_CONTEXT * parser, PT_NODE * node,
			 void *void_arg, int *continue_walk)
{
  PT_NODE *newval;
  DB_VALUE *ival = (DB_VALUE *) void_arg;
  *continue_walk = PT_CONTINUE_WALK;

  if (node->node_type == PT_NAME)
    {
      newval = pt_dbval_to_value (parser, ival);
      if (newval)
	{
	  newval->next = node->next;
	  node->next = NULL;
	  parser_free_tree (parser, node);
	  node = newval;
	  *continue_walk = PT_STOP_WALK;
	}
    }

  return node;
}

/*
 * adjust_name_with_type() -
 *   return: PT_NODE pointer
 *   parser(in): Parser context
 *   node(in):
 *   void_arg(in):
 *   continue_walk(in):
 *
 * Note:
 */

static PT_NODE *
adjust_name_with_type (PARSER_CONTEXT * parser, PT_NODE * node,
		       void *void_arg, int *continue_walk)
{
  PT_TYPE_ENUM *key_type = (PT_TYPE_ENUM *) void_arg;

  *continue_walk = PT_CONTINUE_WALK;

  if (node->node_type == PT_NAME)
    {
      node->type_enum = (PT_TYPE_ENUM) * key_type;
      node->data_type = pt_domain_to_data_type (parser,
						pt_type_enum_to_db_domain
						(node->type_enum));
    }

  return node;
}

/*
 * evaluate_partition_expr() -
 *   return: DB_VALUE pointer
 *   expr(in): Expression to evaluate
 *   ival(in):
 *
 * Note:
 */
static DB_VALUE *
evaluate_partition_expr (DB_VALUE * expr, DB_VALUE * ival)
{
  PT_NODE **newnode;
  PT_NODE *rstnode, *pcol, *expr_type = NULL;
  PARSER_CONTEXT *expr_parser = NULL;

  if (expr == NULL || ival == NULL)
    return NULL;

  expr_parser = parser_create_parser ();
  if (expr_parser == NULL)
    {
      return NULL;
    }

  newnode = parser_parse_string (expr_parser, DB_GET_STRING (expr));
  if (newnode && *newnode)
    {
      pcol = (*newnode)->info.query.q.select.list;
      if (pcol->node_type == PT_NAME)
	{
	  parser_free_parser (expr_parser);
	  return ival;
	}

      rstnode =
	parser_walk_tree (expr_parser, pcol, replace_name_with_value, ival,
			  NULL, NULL);

      /* expression type check and constant evaluation */
      expr_type = pt_semantic_type (expr_parser, pcol, NULL);
      if (!expr_type)
	{
	  parser_free_parser (expr_parser);
	  return NULL;
	}

      pr_clear_value (ival);
      if (expr_type->node_type == PT_EXPR)
	{
	  pt_evaluate_tree (expr_parser, pcol, ival);
	}
      else
	{
	  db_value_clone (pt_value_to_db (expr_parser, expr_type), ival);
	}

      parser_free_parser (expr_parser);
      return ival;
    }

  parser_free_parser (expr_parser);
  return NULL;
}

/*
 * apply_partition_list_search() -
 *   return: Error code
 *   smclass(in):
 *   sval(in):
 *   retbuf(in):
 *
 * Note:
 */
static int
apply_partition_list_search (SM_CLASS * smclass, DB_VALUE * sval,
			     char *retbuf)
{
  int error = NO_ERROR;
  DB_OBJLIST *objs;
  DB_VALUE pname, pval, element;
  int setsize, i1;
  SM_CLASS *subcls;
  char *pname_str;

  db_make_null (&pname);
  db_make_null (&pval);
  db_make_null (&element);

  for (objs = smclass->users; objs; objs = objs->next)
    {
      error = au_fetch_class (objs->op, &subcls, AU_FETCH_READ, AU_SELECT);
      if (error != NO_ERROR)
	{
	  goto end_return;
	}
      if (!subcls->partition_of)
	{
	  continue;		/* not partitioned */
	}

      error = db_get (subcls->partition_of, PARTITION_ATT_PVALUES, &pval);
      if (error != NO_ERROR)
	{
	  goto end_return;
	}
      error = db_get (subcls->partition_of, PARTITION_ATT_PNAME, &pname);
      if (error != NO_ERROR
	  || DB_IS_NULL (&pname)
	  || (pname_str = DB_GET_STRING (&pname)) == NULL)
	{
	  goto end_return;
	}

      setsize = set_size (pval.data.set);
      if (setsize <= 0)
	{
	  error = -1;
	  goto end_return;
	}

      for (i1 = 0; i1 < setsize; i1++)
	{
	  error = set_get_element (pval.data.set, i1, &element);
	  if (error != NO_ERROR)
	    {
	      return error;
	    }

	  /* null element matching */
	  if ((DB_IS_NULL (sval) && DB_IS_NULL (&element))
	      || db_value_compare (sval, &element) == DB_EQ)
	    {
	      strcpy (retbuf, pname_str);
	      error = NO_ERROR;
	      goto end_return;
	    }
	  pr_clear_value (&element);
	}
      pr_clear_value (&pname);
      pr_clear_value (&pval);
    }

  error = -1;			/* not found */

end_return:
  pr_clear_value (&pname);
  pr_clear_value (&pval);
  pr_clear_value (&element);

  return error;
}

/*
 * apply_partition_range_search() -
 *   return: Error code
 *   smclass(in):
 *   sval(in):
 *   retbuf(in):
 *
 * Note:
 */
static int
apply_partition_range_search (SM_CLASS * smclass, DB_VALUE * sval,
			      char *retbuf)
{
  MOP max = NULL, fit = NULL;
  int error = NO_ERROR;
  DB_OBJLIST *objs;
  DB_VALUE pname, pval, minele, maxele, *fitval = NULL;
  SM_CLASS *subcls;
  char *p = NULL;

  db_make_null (&pname);
  db_make_null (&pval);
  db_make_null (&minele);
  db_make_null (&maxele);

  for (objs = smclass->users; objs; objs = objs->next)
    {
      error = au_fetch_class (objs->op, &subcls, AU_FETCH_READ, AU_SELECT);
      if (error != NO_ERROR)
	{
	  goto clear_end;
	}
      if (!subcls->partition_of)
	{
	  continue;		/* not partitioned */
	}

      error = db_get (subcls->partition_of, PARTITION_ATT_PVALUES, &pval);
      if (error != NO_ERROR)
	{
	  goto clear_end;
	}
      error = set_get_element (pval.data.set, 0, &minele);
      if (error != NO_ERROR)
	{
	  goto clear_end;
	}
      error = set_get_element (pval.data.set, 1, &maxele);
      if (error != NO_ERROR)
	{
	  goto clear_end;
	}

      if (DB_IS_NULL (&maxele))
	{			/* MAXVALUE */
	  max = subcls->partition_of;
	}
      else
	{
	  if (DB_IS_NULL (sval) || db_value_compare (sval, &maxele) == DB_LT)
	    {
	      if (fit == NULL)
		{
		  fit = subcls->partition_of;
		  fitval = db_value_copy (&maxele);
		}
	      else
		{
		  if (db_value_compare (fitval, &maxele) == DB_GT)
		    {
		      db_value_free (fitval);
		      fit = subcls->partition_of;
		      fitval = db_value_copy (&maxele);
		    }
		}
	    }
	}

      pr_clear_value (&pval);
      pr_clear_value (&minele);
      pr_clear_value (&maxele);
    }

  if (fit == NULL)
    {
      if (max == NULL)
	{
	  error = -1;
	  goto clear_end;
	}
      fit = max;
    }

  error = db_get (fit, PARTITION_ATT_PNAME, &pname);
  if (error != NO_ERROR
      || DB_IS_NULL (&pname) || (p = DB_GET_STRING (&pname)) == NULL)
    {
      goto clear_end;
    }
  strcpy (retbuf, p);

  error = NO_ERROR;

clear_end:
  pr_clear_value (&pname);
  pr_clear_value (&pval);
  pr_clear_value (&minele);
  pr_clear_value (&maxele);
  if (fitval)
    {
      db_value_free (fitval);
    }

  return error;
}

/*
 * get_partition_parts() -
 *   return: Error code
 *   class_obj(out):
 *   smclass(in):
 *   ptype(in):
 *   pattr(in):
 *   sval(in):
 *
 * Note:
 */

static int
get_partition_parts (MOP * class_obj, SM_CLASS * smclass, int ptype,
		     DB_VALUE * pattr, DB_VALUE * sval)
{
  DB_VALUE ele;
  char pname[PARTITION_VARCHAR_LEN + 1];
  char pclass[PARTITION_VARCHAR_LEN + 1];
  int error = NO_ERROR;

  if (smclass == NULL ||
      ptype < PT_PARTITION_HASH || ptype > PT_PARTITION_LIST ||
      pattr == NULL || sval == NULL)
    {
      *class_obj = NULL;
      return error;
    }

  switch (ptype)
    {
    case PT_PARTITION_HASH:
      error = set_get_element (pattr->data.set, 1, &ele);
      if (error != NO_ERROR)
	{
	  *class_obj = NULL;
	  return error;
	}
      if (ele.data.i <= 0)
	{
	  pr_clear_value (&ele);
	  *class_obj = NULL;
	  return error;
	}

      sprintf (pname, "p%d", mht_get_hash_number (ele.data.i, sval));
      pr_clear_value (&ele);
      break;
    case PT_PARTITION_LIST:
      error = apply_partition_list_search (smclass, sval, pname);
      if (error != NO_ERROR)
	{
	  *class_obj = NULL;
	  return error;
	}
      break;
    case PT_PARTITION_RANGE:
      error = apply_partition_range_search (smclass, sval, pname);
      if (error != NO_ERROR)
	{
	  *class_obj = NULL;
	  return error;
	}
      break;
    }

  sprintf (pclass, "%s" PARTITIONED_SUB_CLASS_TAG "%s",
	   smclass->header.name, pname);

  *class_obj = sm_find_class (pclass);
  return NO_ERROR;
}

/*
 * do_insert_partition_cache() -
 *   return: Error code
 *   pic(in):
 *   attr(in):
 *   desc(in):
 *   val(in):
 *
 * Note:
 */
int
do_insert_partition_cache (PARTITION_INSERT_CACHE ** pic, PT_NODE * attr,
			   DB_ATTDESC * desc, DB_VALUE * val)
{
  PARTITION_INSERT_CACHE *picnext;

  if (*pic == NULL)
    {
      *pic =
	(PARTITION_INSERT_CACHE *) malloc (sizeof (PARTITION_INSERT_CACHE));
      if (*pic == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_PARTITION_WORK_FAILED,
		  0);
	  return er_errid ();
	}
      picnext = *pic;
    }
  else
    {
      for (picnext = *pic; picnext && picnext->next; picnext = picnext->next)
	;

      picnext->next =
	(PARTITION_INSERT_CACHE *) malloc (sizeof (PARTITION_INSERT_CACHE));
      if (picnext->next == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_PARTITION_WORK_FAILED,
		  0);
	  return er_errid ();
	}
      picnext = picnext->next;
    }

  picnext->next = NULL;
  picnext->attr = attr;
  picnext->desc = desc;
  picnext->val = pr_copy_value (val);

  return NO_ERROR;
}

/*
 * do_insert_partition_cache() -
 *   return: Error code
 *   pic(in):
 *   attr(in):
 *   desc(in):
 *   val(in):
 *
 * Note:
 */
void
do_clear_partition_cache (PARTITION_INSERT_CACHE * pic)
{
  PARTITION_INSERT_CACHE *picnext, *tmp;

  picnext = pic;
  while (picnext)
    {
      if (picnext->val)
	{
	  pr_clear_value (picnext->val);
	}
      tmp = picnext;
      picnext = picnext->next;
      if (tmp)
	{
	  free_and_init (tmp);
	}
    }
}

/*
 * do_init_partition_select() -
 *   return: Error code
 *   classobj(in):
 *   psi(in):
 *
 * Note:
 */
int
do_init_partition_select (MOP classobj, PARTITION_SELECT_INFO ** psi)
{
  DB_VALUE ptype, pname, pexpr, pattr;
  int error = NO_ERROR;
  SM_CLASS *smclass;
  int au_save;
  bool au_disable_flag = false;

  if (classobj == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_PARTITION_WORK_FAILED, 0);
      return er_errid ();
    }

  db_make_null (&ptype);
  db_make_null (&pname);
  db_make_null (&pexpr);
  db_make_null (&pattr);

  AU_DISABLE (au_save);
  au_disable_flag = true;

  error = au_fetch_class (classobj, &smclass, AU_FETCH_READ, AU_SELECT);
  if (error != NO_ERROR)
    {
      goto end_partition;
    }

  if (smclass->partition_of == NULL)
    {
      error = ER_PARTITION_WORK_FAILED;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
      goto end_partition;
    }

  error = db_get (smclass->partition_of, PARTITION_ATT_PNAME, &pname);
  if (error != NO_ERROR)
    {
      goto end_partition;
    }

  /* adjust only partition parent class */
  if (DB_IS_NULL (&pname))
    {
      error = db_get (smclass->partition_of, PARTITION_ATT_PTYPE, &ptype);
      if (error != NO_ERROR
	  ||
	  ((error =
	    db_get (smclass->partition_of, PARTITION_ATT_PEXPR,
		    &pexpr)) != NO_ERROR)
	  ||
	  ((error =
	    db_get (smclass->partition_of, PARTITION_ATT_PVALUES,
		    &pattr)) != NO_ERROR))
	{
	  goto end_partition;
	}

      *psi =
	(PARTITION_SELECT_INFO *) malloc (sizeof (PARTITION_SELECT_INFO));
      if (*psi == NULL)
	{
	  error = ER_PARTITION_WORK_FAILED;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
	}
      else
	{
	  error = NO_ERROR;
	  (*psi)->ptype = pr_copy_value (&ptype);
	  (*psi)->pexpr = pr_copy_value (&pexpr);
	  (*psi)->pattr = pr_copy_value (&pattr);
	  (*psi)->smclass = smclass;
	}
    }
  else
    {
      error = ER_PARTITION_WORK_FAILED;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
    }

  AU_ENABLE (au_save);
  au_disable_flag = false;

end_partition:
  pr_clear_value (&ptype);
  pr_clear_value (&pname);
  pr_clear_value (&pexpr);
  pr_clear_value (&pattr);

  if (au_disable_flag == true)
    AU_ENABLE (au_save);

  return error;
}

/*
 * do_clear_partition_select() -
 *   return: Error code
 *   psi(in):
 *
 * Note:
 */
void
do_clear_partition_select (PARTITION_SELECT_INFO * psi)
{
  if (psi == NULL)
    {
      return;
    }

  pr_clear_value (psi->ptype);
  pr_clear_value (psi->pattr);
  pr_clear_value (psi->pexpr);

  free_and_init (psi);
}

/*
 * do_select_partition() -
 *   return: Error code
 *   psi(in):
 *   val(in):
 *   retobj(in):
 *
 * Note:
 */
int
do_select_partition (PARTITION_SELECT_INFO * psi, DB_VALUE * val,
		     MOP * retobj)
{
  int error = NO_ERROR;
  DB_VALUE retval;
  int au_save;

  /* expr eval */
  db_make_null (&retval);
  error = db_value_clone (val, &retval);
  if (error != NO_ERROR)
    {
      return error;
    }
  if (evaluate_partition_expr (psi->pexpr, &retval) == NULL)
    {
      pr_clear_value (&retval);
      error = ER_PARTITION_WORK_FAILED;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
      return error;
    }

  AU_DISABLE (au_save);

  /* _db_partition object search */
  error = get_partition_parts (retobj, psi->smclass, psi->ptype->data.i,
			       psi->pattr, &retval);
  if (*retobj == NULL)
    {
      pr_clear_value (&retval);
      AU_ENABLE (au_save);
      error = ER_PARTITION_NOT_EXIST;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
      return error;
    }

  pr_clear_value (&retval);

  AU_ENABLE (au_save);
  return NO_ERROR;
}

/*
 * find_partition_attr() -
 *   return: PT_NODE pointer
 *   parser(in): Parser context
 *   node(in):
 *   void_arg(in):
 *   continue_walk(in):
 * Note:
 */
static PT_NODE *
find_partition_attr (PARSER_CONTEXT * parser, PT_NODE * node,
		     void *void_arg, int *continue_walk)
{
  PRUNING_INFO *ppi = (PRUNING_INFO *) void_arg;
  *continue_walk = PT_CONTINUE_WALK;

  if (node->node_type == PT_NAME)
    {
      if (node->info.name.spec_id == ppi->spec)
	{
	  if (intl_mbs_casecmp
	      (node->info.name.original, DB_GET_STRING (ppi->attr)) == 0)
	    {
	      ppi->wrkmap |= PATTR_KEY;
	    }
	  else
	    {
	      ppi->wrkmap |= PATTR_COLUMN;
	    }
	}
      else
	{
	  ppi->wrkmap |= PATTR_NAME;
	}
    }
  else if (node->node_type == PT_VALUE)
    {
      ppi->wrkmap |= PATTR_VALUE;
    }

  return node;
}

/*
 * find_partition_attr() -
 *   return: 1 if it is same, else 0
 *   parser(in): Parser context
 *   p(in):
 *   q(in):
 *
 * Note:
 */
static int
check_same_expr (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE * q)
{
  DB_VALUE *vp, *vq;

  if (!p || !q || !parser)
    {
      return 0;
    }

  if (p->node_type != q->node_type)
    {
      return 0;
    }

  switch (p->node_type)
    {
    case PT_EXPR:
      if (p->info.expr.op != q->info.expr.op)
	{
	  return 0;
	}

      if (p->info.expr.arg1)
	{
	  if (check_same_expr
	      (parser, p->info.expr.arg1, q->info.expr.arg1) == 0)
	    {
	      return 0;
	    }
	}
      if (p->info.expr.arg2)
	{
	  if (check_same_expr
	      (parser, p->info.expr.arg2, q->info.expr.arg2) == 0)
	    {
	      return 0;
	    }
	}
      break;

    case PT_VALUE:
      vp = pt_value_to_db (parser, p);
      vq = pt_value_to_db (parser, q);
      if (!vp || !vq)
	{
	  return 0;
	}
      if (!tp_value_equal (vp, vq, 1))
	{
	  return 0;
	}
      break;

    case PT_NAME:
      if (intl_mbs_casecmp (p->info.name.original, q->info.name.original) !=
	  0)
	{
	  return 0;
	}
      break;

    default:
      break;
    }

  return 1;			/* same expr */
}

/*
 * evaluate_partition_range() -
 *   return:
 *   parser(in): Parser context
 *   expr(in):
 *
 * Note:
 */
static int
evaluate_partition_range (PARSER_CONTEXT * parser, PT_NODE * expr)
{
  int cmprst = 0, optype;
  PT_NODE *elem, *llim, *ulim;
  DB_VALUE *orgval, *llimval, *ulimval;
  DB_VALUE_COMPARE_RESULT cmp1;
  DB_VALUE_COMPARE_RESULT cmp2;

  if (!expr || expr->node_type != PT_EXPR
      || expr->info.expr.op != PT_RANGE
      || expr->info.expr.arg1->node_type != PT_VALUE
      || expr->info.expr.arg2->node_type != PT_EXPR)
    {
      return 0;
    }

  for (elem = expr->info.expr.arg2; elem; elem = elem->or_next)
    {
      optype = elem->info.expr.op;

      switch (optype)
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
	}			/* switch (op_type) */

      if (llim != NULL &&
	  (llim->node_type != PT_VALUE
	   || (llimval = pt_value_to_db (parser, llim)) == NULL))
	{
	  return 0;
	}

      if (ulim != NULL &&
	  (ulim->node_type != PT_VALUE
	   || (ulimval = pt_value_to_db (parser, ulim)) == NULL))
	{
	  return 0;
	}

      orgval = pt_value_to_db (parser, expr->info.expr.arg1);
      if (orgval == NULL)
	{
	  return 0;
	}

      if (llim != NULL)
	{
	  cmp1 = (DB_VALUE_COMPARE_RESULT) db_value_compare (llimval, orgval);
	}
      if (ulim != NULL)
	{
	  cmp2 = (DB_VALUE_COMPARE_RESULT) db_value_compare (orgval, ulimval);
	}

      switch (elem->info.expr.op)
	{
	case PT_BETWEEN_EQ_NA:
	  if (cmp1 == DB_EQ)
	    {
	      cmprst = 1;
	    }
	  break;
	case PT_BETWEEN_INF_LE:
	  if (cmp2 == DB_EQ || cmp2 == DB_LT)
	    {
	      cmprst = 1;
	    }
	  break;
	case PT_BETWEEN_INF_LT:
	  if (cmp2 == DB_LT)
	    {
	      cmprst = 1;
	    }
	  break;
	case PT_BETWEEN_GE_INF:
	  if (cmp1 == DB_EQ || cmp1 == DB_LT)
	    {
	      cmprst = 1;
	    }
	  break;
	case PT_BETWEEN_GT_INF:
	  if (cmp1 == DB_LT)
	    {
	      cmprst = 1;
	    }
	  break;
	default:
	  if ((optype == PT_BETWEEN_GE_LE || optype == PT_BETWEEN_GE_LT)
	      && cmp1 != DB_EQ && cmp1 != DB_LT)
	    {
	      break;
	    }

	  if ((optype == PT_BETWEEN_GE_LE || optype == PT_BETWEEN_GT_LE)
	      && cmp2 != DB_EQ && cmp2 != DB_LT)
	    {
	      break;
	    }

	  if ((optype == PT_BETWEEN_GT_LE || optype == PT_BETWEEN_GT_LT)
	      && cmp1 != DB_LT)
	    {
	      break;
	    }

	  if ((optype == PT_BETWEEN_GE_LT || optype == PT_BETWEEN_GT_LT)
	      && cmp2 != DB_LT)
	    {
	      break;
	    }

	  cmprst = 1;
	  break;
	}			/* switch (op_type) */

      if (cmprst)
	break;			/* true find */
    }				/* end for */

  return cmprst;
}

/*
 * conver_expr_to_constant() -
 *   return: PT_NODE pointer
 *   parser(in): Parser context
 *   node(in):
 *   void_arg(in):
 *   continue_walk(in)
 *
 * Note:
 */
static PT_NODE *
conver_expr_to_constant (PARSER_CONTEXT * parser, PT_NODE * node,
			 void *void_arg, int *continue_walk)
{
  DB_VALUE retval, *host_var, *castval;
  PT_NODE *newval;
  bool *support_op = (bool *) void_arg;
  *continue_walk = PT_CONTINUE_WALK;

  if (node->node_type == PT_EXPR)
    {
      switch (node->info.expr.op)
	{
	case PT_SYS_DATE:
	  db_sys_date (&retval);
	  break;
	case PT_SYS_TIME:
	  db_sys_time (&retval);
	  break;
	case PT_SYS_TIMESTAMP:
	  db_sys_timestamp (&retval);
	  break;
	case PT_PLUS:
	case PT_MINUS:
	case PT_MODULUS:
	case PT_TIMES:
	case PT_DIVIDE:
	case PT_UNARY_MINUS:
	case PT_POSITION:
	case PT_SUBSTRING:
	case PT_OCTET_LENGTH:
	case PT_BIT_LENGTH:
	case PT_CHAR_LENGTH:
	case PT_LOWER:
	case PT_UPPER:
	case PT_TRIM:
	case PT_LTRIM:
	case PT_RTRIM:
	case PT_LPAD:
	case PT_RPAD:
	case PT_REPLACE:
	case PT_TRANSLATE:
	case PT_ADD_MONTHS:
	case PT_LAST_DAY:
	case PT_MONTHS_BETWEEN:
	case PT_TO_DATE:
	case PT_TO_NUMBER:
	case PT_TO_TIME:
	case PT_TO_TIMESTAMP:
	case PT_EXTRACT:
	case PT_TO_CHAR:
	case PT_STRCAT:
	case PT_FLOOR:
	case PT_CEIL:
	case PT_POWER:
	case PT_ROUND:
	case PT_ABS:
	case PT_LOG:
	case PT_EXP:
	case PT_SQRT:
	case PT_TRUNC:
	  /* PT_RANGE - sub type */
	case PT_BETWEEN_GE_LE:
	case PT_BETWEEN_GE_LT:
	case PT_BETWEEN_GT_LE:
	case PT_BETWEEN_GT_LT:
	case PT_BETWEEN_EQ_NA:
	case PT_BETWEEN_INF_LE:
	case PT_BETWEEN_INF_LT:
	case PT_BETWEEN_GE_INF:
	case PT_BETWEEN_GT_INF:
	case PT_BETWEEN_AND:
	case PT_INCR:
	case PT_DECR:
	  return node;

	case PT_CAST:
	  castval = pt_value_to_db (parser, node->info.expr.arg1);
	  if (castval != NULL)
	    {
	      if (tp_value_cast (castval, &retval,
				 pt_type_enum_to_db_domain (node->info.expr.
							    cast_type->
							    type_enum),
				 false) == NO_ERROR)
		{
		  break;
		}
	    }

	default:
	  *support_op = false;
	  return node;
	}

      newval = pt_dbval_to_value (parser, &retval);
      if (newval)
	{
	  newval->next = node->next;
	  node->next = NULL;
	  parser_free_tree (parser, node);
	  node = newval;
	}
    }
  else if (node->node_type == PT_HOST_VAR)
    {
      host_var = pt_host_var_db_value (parser, node);
      if (host_var)
	{
	  parser_free_tree (parser, node);
	  node = pt_dbval_to_value (parser, host_var);
	}
    }
  return node;
}

/*
 * get_pruned_partition_spec() -
 *   return: PT_NODE pointer
 *   ppi(in):
 *   subobj(in):
 *
 * Note:
 */
static PT_NODE *
get_pruned_partition_spec (PRUNING_INFO * ppi, MOP subobj)
{
  PT_NODE *subspec;

  for (subspec = ppi->ppart; subspec; subspec = subspec->next)
    {
      if (ws_mop_compare (subspec->info.name.db_object, subobj) == 0)
	{
	  return subspec;
	}
    }
  return NULL;
}

/*
 * add_pruned_partition_part() -
 *   return: PT_NODE pointer
 *   ppi(in):
 *   subobj(in):
 *
 * Note:
 */
static void
add_pruned_partition_part (PT_NODE * subspec, PRUNING_INFO * ppi, MOP subcls,
			   char *cname)
{
  if (subspec == NULL)
    {				/* new node */
      subspec = pt_name (ppi->parser, cname);
      subspec->info.name.db_object = subcls;
      subspec->info.name.location = 1;
      subspec->next = NULL;
      if (ppi->ppart == NULL)
	{
	  ppi->ppart = subspec;
	}
      else
	{
	  parser_append_node (subspec, ppi->ppart);
	}
    }
  else
    {
      subspec->info.name.location++;
    }
}

/*
 * adjust_pruned_partition() -
 *   return: Number of parts
 *   spec(in):
 *   ppi(in):
 *
 * Note:
 */
static int
adjust_pruned_partition (PT_NODE * spec, PRUNING_INFO * ppi)
{
  PT_NODE *subspec, *pre, *tmp;
  int partcnt = 0;

  pre = NULL;
  for (subspec = ppi->ppart; subspec;)
    {
      if (subspec->info.name.location == ppi->expr_cnt)
	{
	  partcnt++;

	  if (spec)
	    {
	      subspec->line_number = spec->line_number;
	      subspec->column_number = spec->column_number;
	      subspec->info.name.spec_id = spec->info.spec.id;
	      subspec->info.name.meta_class = spec->info.spec.meta_class;
	      subspec->info.name.partition_of = NULL;
	      subspec->info.name.location = 0;
	    }
	  pre = subspec;
	  subspec = subspec->next;
	}
      else
	{
	  tmp = subspec->next;
	  if (pre != NULL)
	    {
	      pre->next = tmp;
	    }
	  else
	    {
	      ppi->ppart = tmp;
	    }
	  subspec->next = NULL;
	  parser_free_tree (ppi->parser, subspec);
	  subspec = tmp;
	}
    }

  return partcnt;
}

/*
 * increase_value() -
 *   return: 1 if it is increased, else 0
 *   val(in):
 *
 * Note:
 */
static int
increase_value (DB_VALUE * val)
{
  int month, day, year;

  if (!val || DB_IS_NULL (val))
    {
      return 0;
    }

  switch (DB_VALUE_TYPE (val))
    {
    case DB_TYPE_INTEGER:
      val->data.i++;
      break;
    case DB_TYPE_SHORT:
      val->data.i++;
      break;
    case DB_TYPE_TIME:
      val->data.time++;
      break;
    case DB_TYPE_UTIME:
      val->data.utime++;
      break;
    case DB_TYPE_DATE:
      val->data.date++;
      db_date_decode (&val->data.date, &month, &day, &year);
      db_make_date (val, month, day, year);
      break;
    default:
      return 0;
    }

  return 1;
}

/*
 * decrease_value() -
 *   return: 1 if it is increased, else 0
 *   val(in):
 *
 * Note:
 */
static int
decrease_value (DB_VALUE * val)
{
  int month, day, year;

  if (!val || DB_IS_NULL (val))
    {
      return 0;
    }

  switch (DB_VALUE_TYPE (val))
    {
    case DB_TYPE_INTEGER:
      val->data.i--;
      break;
    case DB_TYPE_SHORT:
      val->data.i--;
      break;
    case DB_TYPE_TIME:
      val->data.time--;
      break;
    case DB_TYPE_UTIME:
      val->data.utime--;
      break;
    case DB_TYPE_DATE:
      val->data.date--;
      db_date_decode (&val->data.date, &month, &day, &year);
      db_make_date (val, month, day, year);
      break;
    default:
      return 0;
    }

  return 1;
}

/*
 * check_hash_range() -
 *   return:
 *   ppi(in):
 *   partmap(in):
 *   op(in):
 *   from_expr(in):
 *   to_expr(in):
 *   setval(in):
 *
 * Note:
 */
static int
check_hash_range (PRUNING_INFO * ppi, char *partmap, PT_OP_TYPE op,
		  PT_NODE * from_expr, PT_NODE * to_expr, int setval)
{
  int addcnt = 0, ret, hashnum;
  DB_VALUE *fromval, *toval;

  if (!from_expr ||
      (from_expr->type_enum != PT_TYPE_INTEGER &&
       from_expr->type_enum != PT_TYPE_SMALLINT &&
       from_expr->type_enum != PT_TYPE_DATE &&
       from_expr->type_enum != PT_TYPE_TIME &&
       from_expr->type_enum != PT_TYPE_TIMESTAMP))
    {
      return -1;
    }

  if (!to_expr ||
      (to_expr->type_enum != PT_TYPE_INTEGER &&
       to_expr->type_enum != PT_TYPE_SMALLINT &&
       to_expr->type_enum != PT_TYPE_DATE &&
       to_expr->type_enum != PT_TYPE_TIME &&
       to_expr->type_enum != PT_TYPE_TIMESTAMP))
    {
      return -1;
    }

  /* GE_LT adjust */
  fromval = pt_value_to_db (ppi->parser, from_expr);
  if (fromval == NULL)
    {
      return -1;
    }
  if (op == PT_BETWEEN_GT_LE || op == PT_BETWEEN_GT_LT)
    {
      if (!increase_value (fromval))
	{
	  return -1;
	}
    }

  toval = pt_value_to_db (ppi->parser, to_expr);
  if (toval == NULL)
    {
      return -1;
    }
  if (op == PT_BETWEEN_GE_LE || op == PT_BETWEEN_GT_LE)
    {
      if (!increase_value (toval))
	{
	  return -1;
	}
    }

  while (1)
    {
      ret = db_value_compare (fromval, toval);
      if (ret == DB_EQ || ret == DB_GT)
	{
	  break;
	}
      hashnum = mht_get_hash_number (ppi->size, fromval);
      if (partmap[hashnum] != setval)
	{
	  partmap[hashnum] = setval;
	  addcnt++;
	  if (addcnt >= ppi->size)
	    {
	      return -1;	/* all partitions */
	    }
	}
      if (!increase_value (fromval))
	{
	  return -1;
	}
    }

  return addcnt;
}

/*
 * select_hash_partition() -
 *   return:
 *   ppi(in):
 *   expr(in):
 *
 * Note:
 */
static int
select_hash_partition (PRUNING_INFO * ppi, PT_NODE * expr)
{
  DB_OBJLIST *objs;
  PT_NODE *elem, *pruned;
  int rst, setsize, i1, hashnum, sval, target_cnt;
  int ret;
  char *partmap;
  DB_VALUE *hval, ele;
  SM_CLASS *subcls;
  DB_VALUE temp;
  db_make_null (&temp);

  pt_evaluate_tree (ppi->parser, expr->info.expr.arg2, &temp);
  if (pt_has_error (ppi->parser))
    {
      pt_report_to_ersys (ppi->parser, PT_SEMANTIC);
      return 0;
    }
  hval = &temp;

  partmap = (char *) malloc (ppi->size);
  if (partmap == NULL)
    {
      db_value_clear (&temp);
      return 0;
    }

  memset (partmap, 0, ppi->size);

  switch (expr->info.expr.op)
    {
    case PT_RANGE:
      for (rst = 0, elem = expr->info.expr.arg2; elem; elem = elem->or_next)
	{
	  if (elem->info.expr.op == PT_BETWEEN_EQ_NA)
	    {
	      hval = pt_value_to_db (ppi->parser, elem->info.expr.arg1);
	      if (hval == NULL)
		{
		  continue;
		}

	      hashnum = mht_get_hash_number (ppi->size, hval);
	      if (partmap[hashnum] != 1)
		{
		  partmap[hashnum] = 1;
		  rst++;
		}
	    }
	  else
	    {
	      switch (elem->info.expr.op)
		{
		case PT_BETWEEN_INF_LE:
		case PT_BETWEEN_INF_LT:
		case PT_BETWEEN_GE_INF:
		case PT_BETWEEN_GT_INF:
		  ret = -1;
		default:
		  ret = check_hash_range (ppi, partmap, elem->info.expr.op,
					  elem->info.expr.arg1,
					  elem->info.expr.arg2, 1);
		  break;
		}

	      if (ret == -1)
		{
		  rst = -1;	/* range -> no pruning */
		  break;
		}
	      else
		{
		  rst += ret;
		}
	    }
	}
      break;

    case PT_BETWEEN:
      rst = check_hash_range (ppi, partmap, PT_BETWEEN_GE_LE,
			      expr->info.expr.arg2->info.expr.arg1,
			      expr->info.expr.arg2->info.expr.arg2, 1);
      break;

    case PT_GE:
    case PT_GT:
    case PT_LT:
    case PT_LE:
      rst = -1;			/* range -> no pruning */
      break;

    case PT_IS_IN:
      setsize = set_size (hval->data.set);
      if (setsize <= 0)
	{
	  rst = -1;
	  break;
	}

      for (i1 = 0, rst = 0; i1 < setsize; i1++)
	{
	  if (set_get_element (hval->data.set, i1, &ele) != NO_ERROR)
	    {
	      rst = -1;
	      break;
	    }

	  hashnum = mht_get_hash_number (ppi->size, &ele);
	  if (partmap[hashnum] != 0)
	    {
	      partmap[hashnum] = 1;
	      rst++;
	    }

	  pr_clear_value (&ele);
	}
      break;

    case PT_IS_NULL:
      partmap[0] = 1;		/* first partition */
      rst = 1;
      break;
    case PT_EQ:
      hashnum = mht_get_hash_number (ppi->size, hval);
      partmap[hashnum] = 1;
      rst = 1;
      break;

    default:
      break;
    }

  if (rst <= 0)
    {
      free_and_init (partmap);
      db_value_clear (&temp);
      return 0;
    }

  target_cnt = ppi->expr_cnt + 1;
  for (hashnum = 0, sval = 0, objs = ppi->smclass->users;
       objs && sval < rst; objs = objs->next, hashnum++)
    {
      if (!partmap[hashnum])
	{
	  continue;
	}

      sval++;

      pruned = get_pruned_partition_spec (ppi, objs->op);
      if (ppi->expr_cnt == 0)
	{
	  if (pruned != NULL)
	    {
	      continue;
	    }
	}
      else
	{
	  if (pruned == NULL)
	    {
	      continue;
	    }
	  if (pruned->info.name.location == target_cnt)
	    {
	      continue;
	    }
	}

      if (pruned)
	{
	  add_pruned_partition_part (pruned, ppi, objs->op, NULL);
	}
      else
	{
	  if (au_fetch_class (objs->op, &subcls, AU_FETCH_READ, AU_SELECT) !=
	      NO_ERROR)
	    {
	      continue;
	    }
	  if (!subcls->partition_of)
	    {
	      continue;
	    }
	  add_pruned_partition_part (pruned, ppi, objs->op,
				     (char *) subcls->header.name);
	}
    }

  free_and_init (partmap);
  db_value_clear (&temp);
  return 1;
}

/*
 * select_range_partition() -
 *   return:
 *   ppi(in):
 *   expr(in):
 *
 * Note:
 */
static int
select_range_partition (PRUNING_INFO * ppi, PT_NODE * expr)
{
  DB_OBJLIST *objs;
  DB_VALUE pval, minele, maxele;
  SM_CLASS *subcls;
  PT_NODE *elem, *pruned;
  DB_VALUE *minval, *maxval, *lval, *uval, ele;
  PT_OP_TYPE minop, maxop, lop, uop;
  int rst, optype, setsize, i1, target_cnt;
  DB_TYPE range_type;

  target_cnt = ppi->expr_cnt + 1;
  if (expr->info.expr.arg2 && expr->info.expr.arg2->node_type == PT_VALUE)
    {
      lval = pt_value_to_db (ppi->parser, expr->info.expr.arg2);
      if (lval == NULL)
	{
	  return 0;		/* expr skip */
	}
    }

  db_make_null (&maxele);
  db_make_null (&minele);

  for (objs = ppi->smclass->users; objs; objs = objs->next)
    {

      pruned = get_pruned_partition_spec (ppi, objs->op);
      if (ppi->expr_cnt == 0)
	{
	  if (pruned != NULL)
	    {
	      continue;
	    }
	}
      else
	{
	  if (pruned == NULL)
	    {
	      continue;
	    }
	  if (pruned->info.name.location == target_cnt)
	    {
	      continue;
	    }
	}

      if (au_fetch_class (objs->op, &subcls, AU_FETCH_READ, AU_SELECT) !=
	  NO_ERROR || !subcls->partition_of
	  || db_get (subcls->partition_of, PARTITION_ATT_PVALUES,
		     &pval) != NO_ERROR)
	{
	  continue;
	}

      pr_clear_value (&maxele);
      pr_clear_value (&minele);

      if (set_get_element (pval.data.set, 0, &minele) != NO_ERROR ||
	  set_get_element (pval.data.set, 1, &maxele) != NO_ERROR)
	{
	  continue;
	}

      pr_clear_value (&pval);

      /* min/max conversion for is_ranges_meetable */
      if (DB_IS_NULL (&minele))
	{
	  minval = NULL;
	  minop = PT_GT_INF;
	}
      else
	{
	  minval = &minele;
	  minop = PT_GE;
	}

      if (DB_IS_NULL (&maxele))
	{
	  maxval = NULL;
	  maxop = PT_LT_INF;
	}
      else
	{
	  maxval = &maxele;
	  maxop = (decrease_value (maxval)) ? PT_LE : PT_LT;
	}

      rst = 0;

      /* expr's op conversion for is_ranges_meetable */
      switch (expr->info.expr.op)
	{
	case PT_RANGE:
	  for (elem = expr->info.expr.arg2; elem; elem = elem->or_next)
	    {

	      optype = elem->info.expr.op;
	      if (optype == PT_BETWEEN_EQ_NA)
		{
		  lval = pt_value_to_db (ppi->parser, elem->info.expr.arg1);
		  if (lval == NULL)
		    {
		      continue;
		    }
		  if (is_in_range (minval, minop, maxval, maxop, lval))
		    {
		      break;
		    }
		  else
		    {
		      continue;
		    }
		}

	      if (pt_between_to_comp_op ((PT_OP_TYPE) optype, &lop, &uop) !=
		  0)
		{
		  continue;
		}

	      switch (optype)
		{
		case PT_BETWEEN_INF_LE:
		case PT_BETWEEN_INF_LT:
		  lval = NULL;
		  uval = pt_value_to_db (ppi->parser, elem->info.expr.arg1);
		  if (uval == NULL)
		    {
		      continue;
		    }
		  break;

		case PT_BETWEEN_GE_INF:
		case PT_BETWEEN_GT_INF:
		  lval = pt_value_to_db (ppi->parser, elem->info.expr.arg1);
		  if (lval == NULL)
		    {
		      continue;
		    }
		  uval = NULL;
		  break;

		default:
		  lval = pt_value_to_db (ppi->parser, elem->info.expr.arg1);
		  if (lval == NULL)
		    {
		      continue;
		    }
		  uval = pt_value_to_db (ppi->parser, elem->info.expr.arg2);
		  if (uval == NULL)
		    {
		      continue;
		    }
		  break;
		}		/* switch (op_type) */

	      if (is_ranges_meetable (minval, minop, maxval, maxop,
				      lval, lop, uval, uop))
		{
		  break;
		}
	    }
	  rst = (elem == NULL) ? 0 : 1;
	  break;

	case PT_BETWEEN:
	case PT_NOT_BETWEEN:
	  lval = pt_value_to_db (ppi->parser,	/* BETWEEN .. AND */
				 expr->info.expr.arg2->info.expr.arg1);
	  if (lval == NULL)
	    {
	      return 0;
	    }
	  uval = pt_value_to_db (ppi->parser,
				 expr->info.expr.arg2->info.expr.arg2);
	  if (uval == NULL)
	    {
	      return 0;
	    }
	  if (expr->info.expr.op == PT_BETWEEN)
	    {
	      rst = is_ranges_meetable (minval, minop, maxval, maxop,
					lval, PT_GE, uval, PT_LE);
	    }
	  else
	    {
	      rst = 0;
	      if (is_ranges_meetable (minval, minop, maxval, maxop,
				      NULL, PT_GT_INF, lval, PT_LT) ||
		  is_ranges_meetable (minval, minop, maxval, maxop,
				      uval, PT_GT, NULL, PT_LT_INF))
		{
		  rst = 1;
		}
	    }
	  break;

	case PT_GE:
	case PT_GT:
	  rst = is_ranges_meetable (minval, minop, maxval, maxop,
				    lval, expr->info.expr.op, NULL,
				    PT_LT_INF);
	  break;

	case PT_LT:
	case PT_LE:
	  rst = is_ranges_meetable (minval, minop, maxval, maxop,
				    NULL, PT_GT_INF, lval,
				    expr->info.expr.op);
	  break;

	case PT_IS_IN:
	  setsize = set_size (lval->data.set);
	  if (setsize <= 0)
	    {
	      return 0;
	    }

	  for (i1 = 0; i1 < setsize; i1++)
	    {
	      if (set_get_element (lval->data.set, i1, &ele) != NO_ERROR)
		{
		  return 0;
		}
	      if (is_in_range (minval, minop, maxval, maxop, &ele))
		{
		  pr_clear_value (&ele);
		  break;
		}
	      pr_clear_value (&ele);
	    }

	  if (i1 >= setsize)
	    {			/* not found */
	      rst = 0;
	    }
	  else
	    {
	      rst = 1;
	    }
	  break;

	case PT_IS_NOT_IN:
	  if (maxval == NULL || minval == NULL)
	    {
	      rst = 1;		/* not prune : min/max-infinite */
	      break;
	    }

	  range_type = DB_VALUE_TYPE (maxval);
	  if (range_type != DB_TYPE_INTEGER &&
	      range_type != DB_TYPE_SMALLINT &&
	      range_type != DB_TYPE_DATE &&
	      range_type != DB_TYPE_TIME && range_type != DB_TYPE_TIMESTAMP)
	    {
	      rst = 1;
	      break;
	    }

	  rst = 1;
	  while (1)
	    {
	      if (db_value_compare (minval, maxval) == DB_GT)
		{
		  rst = 0;
		  break;
		}
	      if (set_find_seq_element (lval->data.set, minval, 0) < 0)
		{
		  break;	/* not found */
		}
	      if (!increase_value (minval))
		{
		  break;
		}
	    }
	  break;

	case PT_IS_NULL:
	  rst = (minval == NULL) ? 1 : 0;
	  break;

	case PT_EQ:
	  rst = is_in_range (minval, minop, maxval, maxop, lval);
	  break;

	case PT_NE:
	  rst = 0;
	  if (is_ranges_meetable (minval, minop, maxval, maxop,
				  NULL, PT_GT_INF, lval, PT_LT) ||
	      is_ranges_meetable (minval, minop, maxval, maxop,
				  lval, PT_GT, NULL, PT_LT_INF))
	    {
	      rst = 1;
	    }
	  break;

	default:
	  break;
	}

      if (rst)
	{
	  add_pruned_partition_part (pruned, ppi, objs->op,
				     (char *) subcls->header.name);
	}
    }				/* end of for */

  pr_clear_value (&maxele);
  pr_clear_value (&minele);

  return 1;
}

/*
 * select_list_partition() -
 *   return:
 *   ppi(in):
 *   expr(in):
 *
 * Note:
 */
static int
select_list_partition (PRUNING_INFO * ppi, PT_NODE * expr)
{
  DB_OBJLIST *objs;
  DB_VALUE pval, ele;
  int setsize, i1, rst, target_cnt, check_all_flag, check_cnt;
  SM_CLASS *subcls;
  PT_NODE *actexpr, *actval, *pruned;

  target_cnt = ppi->expr_cnt + 1;
  db_make_null (&pval);

  check_all_flag = (expr->info.expr.op == PT_NOT_BETWEEN
		    || expr->info.expr.op == PT_IS_NOT_IN
		    || expr->info.expr.op == PT_IS_NOT_NULL
		    || expr->info.expr.op == PT_NE) ? 1 : 0;
  for (objs = ppi->smclass->users; objs; objs = objs->next)
    {
      pruned = get_pruned_partition_spec (ppi, objs->op);

      if (ppi->expr_cnt == 0)
	{
	  if (pruned != NULL)
	    {
	      continue;
	    }
	}
      else
	{
	  if (pruned == NULL)
	    {
	      continue;
	    }
	  if (pruned->info.name.location == target_cnt)
	    {
	      continue;
	    }
	}

      if (au_fetch_class (objs->op, &subcls, AU_FETCH_READ, AU_SELECT)
	  != NO_ERROR || !subcls->partition_of
	  || db_get (subcls->partition_of, PARTITION_ATT_PVALUES,
		     &pval) != NO_ERROR)
	{
	  continue;
	}

      setsize = set_size (pval.data.set);
      if (setsize <= 0)
	{
	  pr_clear_value (&pval);
	  continue;
	}

      check_cnt = 0;
      for (i1 = 0; i1 < setsize; i1++)
	{
	  if (set_get_element (pval.data.set, i1, &ele) != NO_ERROR)
	    {
	      continue;
	    }

	  actexpr = parser_copy_tree_list (ppi->parser, expr);
	  if (actexpr == NULL)
	    {
	      pr_clear_value (&ele);
	      continue;
	    }
	  actval = pt_dbval_to_value (ppi->parser, &ele);
	  if (actval == NULL)
	    {
	      pr_clear_value (&ele);
	      continue;
	    }

	  actval->next = expr->info.expr.arg1->next;
	  actexpr->info.expr.arg1->next = NULL;
	  parser_free_tree (ppi->parser, actexpr->info.expr.arg1);
	  actexpr->info.expr.arg1 = actval;

	  if (actexpr->info.expr.op == PT_RANGE)
	    {
	      rst = evaluate_partition_range (ppi->parser, actexpr);
	      parser_free_tree (ppi->parser, actexpr);
	    }
	  else
	    {
	      actval = pt_semantic_type (ppi->parser, actexpr, NULL);
	      if (actval == NULL)
		{
		  pr_clear_value (&ele);
		  continue;
		}
	      rst = actval->info.value.data_value.i;
	      parser_free_tree (ppi->parser, actval);
	    }
	  pr_clear_value (&ele);

	  if (check_all_flag)
	    {
	      if (rst)
		{
		  check_cnt++;
		}
	      if (expr->info.expr.op == PT_IS_NOT_NULL
		  || expr->info.expr.op == PT_NE || check_cnt > 0)
		{
		  break;
		}
	    }
	  else
	    {
	      if (rst)
		{
		  add_pruned_partition_part (pruned, ppi, objs->op,
					     (char *) subcls->header.name);
		  break;
		}
	    }
	}			/* for i1 */
      pr_clear_value (&pval);

      if (check_all_flag)
	{
	  if ((expr->info.expr.op == PT_IS_NOT_NULL
	       || expr->info.expr.op == PT_NE))
	    {
	      if (setsize != 1 || check_cnt > 0)
		{
		  add_pruned_partition_part (pruned, ppi, objs->op,
					     (char *) subcls->header.name);
		}
	    }
	  else
	    {
	      if (check_cnt > 0)
		{
		  add_pruned_partition_part (pruned, ppi, objs->op,
					     (char *) subcls->header.name);
		}
	    }
	}
    }				/* for objs */

  return 1;
}

/*
 * select_range_list() -
 *   return:
 *   ppi(in):
 *   cond(in):
 *
 * Note:
 */
static bool
select_range_list (PRUNING_INFO * ppi, PT_NODE * cond)
{
  PT_NODE *condeval = NULL;
  bool support_op = true;

  condeval = parser_copy_tree_list (ppi->parser, cond);

  if (condeval->info.expr.arg2
      && condeval->info.expr.arg2->node_type != PT_VALUE)
    {
      condeval->info.expr.arg2 = parser_walk_tree (ppi->parser,	/*  SYS_DATE... */
						   condeval->info.expr.arg2,
						   NULL, NULL,
						   conver_expr_to_constant,
						   &support_op);
      if (support_op == false)
	{
	  return false;
	}
      if (condeval->info.expr.arg2->node_type != PT_VALUE)
	{
	  condeval->info.expr.arg2 = pt_semantic_type (ppi->parser,
						       condeval->info.expr.
						       arg2, NULL);
	}
      if (condeval->info.expr.arg2->node_type == PT_HOST_VAR)
	{
	  return true;
	}
    }

  /* eval. fail-ignore constant type mismatch etc... */
  if (ppi->parser->error_msgs)
    {
      parser_free_tree (ppi->parser, ppi->parser->error_msgs);
      ppi->parser->error_msgs = NULL;
      if (condeval)
	{
	  parser_free_tree (ppi->parser, condeval);
	}
      return false;
    }

  if (condeval->info.expr.arg2
      && condeval->info.expr.arg1->type_enum
      != condeval->info.expr.arg2->type_enum
      && condeval->info.expr.arg2->type_enum != PT_TYPE_SET
      && (!TP_IS_CHAR_TYPE (condeval->info.expr.arg1->type_enum)
	  || !TP_IS_CHAR_TYPE (condeval->info.expr.arg2->type_enum)))
    {
      if (pt_coerce_value (ppi->parser, condeval->info.expr.arg2,
			   condeval->info.expr.arg2,
			   condeval->info.expr.arg1->type_enum,
			   condeval->info.expr.arg1->data_type) != NO_ERROR)
	{
	  return false;
	}
    }

  switch (ppi->type)
    {
    case PT_PARTITION_RANGE:
      if (select_range_partition (ppi, condeval) && !ppi->and_or)
	{
	  ppi->expr_cnt++;
	}
      break;
    case PT_PARTITION_LIST:
      if (select_list_partition (ppi, condeval) && !ppi->and_or)
	{
	  ppi->expr_cnt++;
	}
      break;
    case PT_PARTITION_HASH:
      if (select_hash_partition (ppi, condeval) && !ppi->and_or)
	{
	  ppi->expr_cnt++;
	}
      break;
    }

  if (condeval)
    {
      parser_free_tree (ppi->parser, condeval);
    }
  return false;
}

/*
 * make_attr_search_value() -
 *   return:
 *   and_or(in):
 *   incond(in):
 *   ppi(in):
 *
 * Note:
 */
static bool
make_attr_search_value (int and_or, PT_NODE * incond, PRUNING_INFO * ppi)
{
  int a1, a2, befcnt;
  PT_NODE *cond;
  bool unbound_hostvar = false;

  if (incond == NULL || incond->node_type != PT_EXPR)
    {
      return unbound_hostvar;
    }

  if (incond->or_next)
    {				/* OR link */
      if (make_attr_search_value (1, incond->or_next, ppi))
	{
	  unbound_hostvar = true;
	}
    }

  befcnt = ppi->expr_cnt;
  cond = parser_copy_tree (ppi->parser, incond);
  if (cond == NULL)
    {
      return unbound_hostvar;
    }

  switch (cond->info.expr.op)
    {
    case PT_NOT_BETWEEN:
    case PT_IS_NOT_IN:
    case PT_IS_NOT_NULL:
    case PT_NE:
      if (ppi->type == PT_PARTITION_HASH
	  || (cond->info.expr.op == PT_IS_NOT_NULL
	      && ppi->type == PT_PARTITION_RANGE))
	{
	  break;		/* not prune */
	}

    case PT_BETWEEN:
    case PT_RANGE:
    case PT_GE:
    case PT_GT:
    case PT_LT:
    case PT_LE:
    case PT_IS_IN:
    case PT_IS_NULL:
    case PT_EQ:
      /* key column-constant search */
      ppi->wrkmap = 0;
      parser_walk_tree (ppi->parser, cond->info.expr.arg1,
			find_partition_attr, ppi, NULL, NULL);
      a1 = ppi->wrkmap;

      ppi->wrkmap = 0;
      parser_walk_tree (ppi->parser, cond->info.expr.arg2,
			find_partition_attr, ppi, NULL, NULL);
      a2 = ppi->wrkmap;

      if (a1 == PATTR_NOT_FOUND	/* not prune */
	  || !(a1 & PATTR_KEY) || (a1 & (PATTR_NAME | PATTR_COLUMN)))
	{
	  break;
	}
      if ((a2 != PATTR_NOT_FOUND && a2 != PATTR_VALUE))
	{
	  break;
	}

      ppi->and_or = and_or;
      if (ppi->expr->node_type == PT_NAME)
	{
	  if (cond->info.expr.arg1->node_type == PT_EXPR)
	    {
	      break;
	    }
	  if (select_range_list (ppi, cond))
	    {
	      unbound_hostvar = true;
	    }
	}
      else
	{			/* expression matching */
	  if (cond->info.expr.arg1->node_type == PT_EXPR)
	    {
	      if (check_same_expr
		  (ppi->parser, ppi->expr, cond->info.expr.arg1))
		{
		  if (select_range_list (ppi, cond))
		    {
		      unbound_hostvar = true;
		    }
		}
	      else
		{
		  break;	/* different expr - not prune */
		}
	    }
	  else
	    {
	      break;
	    }
	}
      break;

    default:
      break;

    }				/* switch */

  if (cond != NULL)
    {
      parser_free_tree (ppi->parser, cond);
    }

  if (and_or)
    {
      return unbound_hostvar;	/* OR node */
    }

  if (ppi->expr_cnt > 0 && befcnt != ppi->expr_cnt)
    {
      if (!adjust_pruned_partition (NULL, ppi))
	{
	  return unbound_hostvar;	/* No partition */
	}
    }

  if (incond->next)
    {				/* AND link */
      if (make_attr_search_value (0, incond->next, ppi))
	{
	  unbound_hostvar = true;
	}
    }
  return unbound_hostvar;
}

/*
 * make_attr_search_value() -
 *   return:
 *   spec(in):
 *   ppi(in):
 *
 * Note:
 */
static PT_NODE *
apply_no_pruning (PT_NODE * spec, PRUNING_INFO * ppi)
{
  DB_OBJLIST *objs;
  PT_NODE *rst = NULL, *newname;
  SM_CLASS *subcls;

  for (objs = ppi->smclass->users; objs; objs = objs->next)
    {
      if (au_fetch_class (objs->op, &subcls, AU_FETCH_READ, AU_SELECT)
	  != NO_ERROR)
	{
	  continue;
	}
      if (!subcls->partition_of)
	{
	  continue;
	}

      newname = pt_name (ppi->parser, subcls->header.name);
      newname->info.name.db_object = objs->op;
      newname->info.name.location = 0;
      newname->line_number = spec->line_number;
      newname->column_number = spec->column_number;
      newname->info.name.spec_id = spec->info.spec.id;
      newname->info.name.meta_class = spec->info.spec.meta_class;
      newname->info.name.partition_of = NULL;
      newname->next = NULL;

      if (rst == NULL)
	{
	  rst = newname;
	}
      else
	{
	  parser_append_node (newname, rst);
	}
    }

  return rst;
}

/*
 * do_apply_partition_pruning() -
 *   return:
 *   parser(in): Parser context
 *   stmt(in):
 *
 * Note:
 */
void
do_apply_partition_pruning (PARSER_CONTEXT * parser, PT_NODE * stmt)
{
  PRUNING_INFO pi = { NULL, NULL, NULL, NULL, NULL, 0, 0, 0, 0, 0, 0L };
  PT_NODE *spec, *cond, *name, *retflat;
  PT_NODE **enode;
  DB_VALUE ptype, pname, pexpr, pattr;
  DB_VALUE attr, hashsize;
  int is_all = 0, au_save;
  MOP classop;
  PARSER_CONTEXT *expr_parser = NULL;

  AU_DISABLE (au_save);

  spec = NULL;
  switch (stmt->node_type)
    {
    case PT_SELECT:
      spec = stmt->info.query.q.select.from;
      cond = stmt->info.query.q.select.where;
      break;
    case PT_UPDATE:
      spec = stmt->info.update.spec;
      cond = stmt->info.update.search_cond;
      break;
    case PT_DELETE:
      spec = stmt->info.delete_.spec;
      cond = stmt->info.delete_.search_cond;
      break;
    case PT_SPEC:		/* path expression */
      spec = stmt;
      cond = NULL;
      break;
    default:
      break;
    }

  if (spec == NULL)
    {
      AU_ENABLE (au_save);
      return;
    }

  db_make_null (&ptype);
  db_make_null (&pname);
  db_make_null (&pexpr);
  db_make_null (&pattr);

  /* partitioned table search */
  for (; spec; spec = spec->next)
    {
      for (name = spec->info.spec.flat_entity_list; name; name = name->next)
	{
	  if (name->info.name.partition_of == NULL)
	    {
	      continue;
	    }

	  classop = db_find_class (name->info.name.original);
	  if (classop != NULL)
	    {
	      if (au_fetch_class
		  (classop, &pi.smclass, AU_FETCH_READ,
		   AU_SELECT) != NO_ERROR)
		{
		  goto work_failed;
		}
	    }
	  else
	    {
	      goto work_failed;
	    }

	  db_make_null (&ptype);
	  db_make_null (&pname);
	  db_make_null (&pexpr);
	  db_make_null (&pattr);
	  db_make_null (&attr);
	  db_make_null (&hashsize);

	  if (db_get
	      (name->info.name.partition_of, PARTITION_ATT_PNAME,
	       &pname) != NO_ERROR)
	    {
	      continue;
	    }
	  if (!DB_IS_NULL (&pname))
	    {
	      goto clear_loop;	/* partitioned sub-class */
	    }

	  if (db_get
	      (name->info.name.partition_of, PARTITION_ATT_PTYPE,
	       &ptype) != NO_ERROR)
	    {
	      goto clear_loop;
	    }

	  if (db_get
	      (name->info.name.partition_of, PARTITION_ATT_PEXPR,
	       &pexpr) != NO_ERROR)
	    {
	      goto clear_loop;
	    }
	  if (DB_IS_NULL (&pexpr))
	    {
	      goto clear_loop;
	    }

	  if (db_get
	      (name->info.name.partition_of, PARTITION_ATT_PVALUES,
	       &pattr) != NO_ERROR)
	    {
	      goto clear_loop;
	    }
	  if (DB_IS_NULL (&pattr))
	    {
	      goto clear_loop;
	    }
	  if (set_get_element (pattr.data.set, 0, &attr) != NO_ERROR)
	    {
	      goto clear_loop;
	    }

	  if (ptype.data.i == PT_PARTITION_HASH)
	    {
	      if (set_get_element (pattr.data.set, 1, &hashsize) != NO_ERROR)
		{
		  goto clear_loop;
		}
	      pi.size = hashsize.data.i;
	    }
	  else
	    {
	      pi.size = 0;
	    }

	  expr_parser = parser_create_parser ();
	  if (expr_parser == NULL)
	    {
	      goto clear_loop;
	    }

	  enode = parser_parse_string (expr_parser, DB_GET_STRING (&pexpr));
	  pi.expr = (*enode)->info.query.q.select.list;
	  if (enode && *enode && pi.expr)
	    {
	      pi.parser = parser;
	      pi.attr = &attr;
	      pi.ppart = NULL;
	      pi.type = ptype.data.i;
	      pi.spec = name->info.name.spec_id;
	      pi.expr_cnt = 0;

	      /* search condition search & value list make */
	      if (cond != NULL && cond->node_type == PT_EXPR)
		{
		  if (make_attr_search_value (0, cond, &pi))
		    {
		      stmt->cannot_prepare = 1;	/* unbound HOSTVAR exists */
		      goto clear_loop;
		    }
		}
	      else
		{
		  is_all = 1;
		}

	      if (pi.expr_cnt <= 0)
		{
		  is_all = 1;
		}

	      if (!is_all)
		{		/* pruned partition adjust */
		  if (pi.expr_cnt > 0 && pi.ppart == NULL)
		    {
		      is_all = -1;	/* no partitions */
		    }
		  else
		    {
		      if (!adjust_pruned_partition (spec, &pi))
			{
			  is_all = -1;
			}
		    }
		}

	      if (is_all != -1)
		{
		  if (is_all)
		    {
		      retflat = apply_no_pruning (spec, &pi);
		    }
		  else
		    {
		      retflat = pi.ppart;
		    }
		  parser_append_node (retflat,
				      spec->info.spec.flat_entity_list);
		  spec->partition_pruned = 1;
		  stmt->partition_pruned = 1;
		  if (cond != NULL)
		    {
		      cond->partition_pruned = 1;
		    }
		}
	    }

	clear_loop:
	  pr_clear_value (&ptype);
	  pr_clear_value (&pname);
	  pr_clear_value (&pexpr);
	  pr_clear_value (&pattr);
	  pr_clear_value (&attr);
	  pr_clear_value (&hashsize);

	  if (expr_parser)
	    {
	      parser_free_parser (expr_parser);
	      expr_parser = NULL;
	    }
	}

      for (name = spec->info.spec.path_entities; name; name = name->next)
	{
	  if (name->info.spec.meta_class == PT_PATH_OUTER ||
	      name->info.spec.meta_class == PT_PATH_INNER)
	    {
	      do_apply_partition_pruning (parser, name);
	      if (name->partition_pruned)
		{
		  stmt->partition_pruned = 1;
		}
	    }
	}
    }

  AU_ENABLE (au_save);
  return;

work_failed:
  AU_ENABLE (au_save);

  pr_clear_value (&ptype);
  pr_clear_value (&pname);
  pr_clear_value (&pexpr);
  pr_clear_value (&pattr);

  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_PARTITION_WORK_FAILED, 0);
}

/*
 * check_range_merge() - Compare two DB_VALUEs specified by range operator
 *   return:
 *   val1(in):
 *   op1(in):
 *   val2(in):
 *   op2(in):
 *
 * Note: The function is from qo/rewrite.c
 */
static MERGE_CHECK_RESULT
check_range_merge (DB_VALUE * val1, PT_OP_TYPE op1,
		   DB_VALUE * val2, PT_OP_TYPE op2)
{
  DB_VALUE_COMPARE_RESULT rc;

  switch (op1)
    {
    case PT_EQ:
    case PT_GE:
    case PT_GT:
    case PT_LT:
    case PT_LE:
    case PT_GT_INF:
    case PT_LT_INF:
      break;
    default:
      return RANGES_ERROR;
    }

  switch (op2)
    {
    case PT_EQ:
    case PT_GE:
    case PT_GT:
    case PT_LT:
    case PT_LE:
    case PT_GT_INF:
    case PT_LT_INF:
      break;
    default:
      return RANGES_ERROR;
    }

  if (op1 == PT_GT_INF)		/* val1 is -INF */
    {
      return (op1 == op2) ? RANGES_EQUAL : RANGES_LESS;
    }
  if (op1 == PT_LT_INF)		/* val1 is +INF */
    {
      return (op1 == op2) ? RANGES_EQUAL : RANGES_GREATER;
    }
  if (op2 == PT_GT_INF)		/* val2 is -INF */
    {
      return (op2 == op1) ? RANGES_EQUAL : RANGES_GREATER;
    }
  if (op2 == PT_LT_INF)		/* va2 is +INF */
    {
      return (op2 == op1) ? RANGES_EQUAL : RANGES_LESS;
    }

  rc = (DB_VALUE_COMPARE_RESULT) tp_value_compare (val1, val2, 1, 1);
  if (rc == DB_EQ)
    {
      /* matrix when val1 == val2(ex, a>3 == a<=3)
       * op1/op2| EQ  GE   GT  LT  LE
       * op1/op2| a=3 a>=3 a>3 a<3 a<=3
       * -------|---------------mergable
       *     EQ | eq  eq   la  ga  eq
       *     GE | eq  eq   la  ga  eq
       *     GT | ga  ga   eq  gt  ga
       *     LT | la  la   lt  eq  la
       *     LE | eq  eq   la  ga  eq
       * -------|---------------meetable
       * EQ a=3 | eq  eq   lt  gt  eq
       * GEa>=3 | eq  eq   eq  gt  eq
       * GT a>3 | gt  eq   eq  gt  gt
       * LT a<3 | lt  lt   lt  eq  eq
       * LEa<=3 | eq  eq   lt  eq  eq
       * lt -> (val1, op1) less than (val2, op2)
       * la -> (val1, op1) less than and adjacent to (val2, op2)
       * eq -> (val1, op1) equal (val2, op2)
       * ga -> (val1, op1) greater than and adjacent to (val2, op2)
       * gt -> (val1, op1) greater than (val2, op2) */

      if (op1 == op2)
	{
	  return RANGES_EQUAL;
	}
      if (op1 == PT_EQ || op1 == PT_GE || op1 == PT_LE)
	{
	  if (op2 == PT_EQ || op2 == PT_GE || op2 == PT_LE)
	    {
	      return RANGES_EQUAL;
	    }
	  if (op2 == PT_GT)
	    {
	      return RANGES_LESS;
	    }
	  if (op2 == PT_LT)
	    {
	      return RANGES_GREATER;
	    }
	  return RANGES_EQUAL;
	}
      if (op1 == PT_GT)
	{
	  if (op2 == PT_GT)
	    {
	      return RANGES_EQUAL;
	    }
	  return RANGES_GREATER;
	}
      if (op1 == PT_LT)
	{
	  if (op2 == PT_LT)
	    {
	      return RANGES_EQUAL;
	    }
	  return RANGES_LESS;
	}
    }
  else if (rc == DB_LT)
    {
      return RANGES_LESS;
    }
  else if (rc == DB_GT)
    {
      return RANGES_GREATER;
    }

  return RANGES_ERROR;
}

/*
 * check_range_merge() -
 *   return:
 *   aval1(in):
 *   aop1(in):
 *   aval2(in):
 *   aop2(in):
 *   bval1(in):
 *   bop1(in):
 *   bval2(in):
 *   bop2(in):
 *
 * Note:
 */
static int
is_ranges_meetable (DB_VALUE * aval1, PT_OP_TYPE aop1,
		    DB_VALUE * aval2, PT_OP_TYPE aop2,
		    DB_VALUE * bval1, PT_OP_TYPE bop1,
		    DB_VALUE * bval2, PT_OP_TYPE bop2)
{
  MERGE_CHECK_RESULT cmp1, cmp2, cmp3, cmp4;

  cmp1 = check_range_merge (aval1, aop1, bval1, bop1);
  cmp2 = check_range_merge (aval1, aop1, bval2, bop2);
  cmp3 = check_range_merge (aval2, aop2, bval1, bop1);
  cmp4 = check_range_merge (aval2, aop2, bval2, bop2);

  if (cmp1 == RANGES_ERROR || cmp2 == RANGES_ERROR ||
      cmp3 == RANGES_ERROR || cmp4 == RANGES_ERROR)
    {
      return 0;
    }

  if ((cmp1 == RANGES_LESS || cmp1 == RANGES_GREATER) &&
      cmp1 == cmp2 && cmp1 == cmp3 && cmp1 == cmp4)
    {
      /* they are disjoint ranges */
      return 0;
    }

  return 1;
}

/*
 * is_in_range() - Check if the value is in range
 *   return:
 *   aval1(in):
 *   aop1(in):
 *   aval2(in):
 *   aop2(in):
 *   bval(in):
 *
 * Note:
 */
static int
is_in_range (DB_VALUE * aval1, PT_OP_TYPE aop1,
	     DB_VALUE * aval2, PT_OP_TYPE aop2, DB_VALUE * bval)
{
  MERGE_CHECK_RESULT cmp1, cmp2;

  cmp1 = check_range_merge (aval1, aop1, bval, PT_EQ);
  cmp2 = check_range_merge (aval2, aop2, bval, PT_EQ);

  if (cmp1 == RANGES_ERROR || cmp2 == RANGES_ERROR)
    {
      return 0;
    }

  if ((cmp1 == RANGES_LESS || cmp1 == RANGES_GREATER) && cmp1 == cmp2)
    {
      /* the value is not in range */
      return 0;
    }

  return 1;
}

/*
 * do_build_partition_xasl() -
 *   return: Error code
 *   parser(in): Parser context
 *   xasl(in/out): XASL tree to be built
 *   class_obj(in):
 *   idx(in):
 *
 * Note:
 */
int
do_build_partition_xasl (PARSER_CONTEXT * parser, XASL_NODE * xasl,
			 MOP class_obj, int idx)
{
  DB_VALUE ptype, pname, pexpr, pattr, pval, partname;
  PT_NODE **enode, *expr;
  DB_OBJLIST *objs;
  SM_CLASS *smclass, *subcls;
  DB_VALUE attr, hashsize;
  int is_error = 1, pi, au_save, partition_remove_mode = 0;
  int partition_coalesce_mode = 0, coalesce_part, partnum;
  int partition_reorg_mode = 0;
  XASL_PARTITION_INFO *xpi = NULL;
  OID *class_oid;
  HFID *hfid;
  MOBJ class_;
  PT_TYPE_ENUM key_type;
  PARSER_CONTEXT *expr_parser = NULL;
  int delete_flag;

  if (!parser || !xasl || !class_obj)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_PARTITION_WORK_FAILED, 0);
      return ER_PARTITION_WORK_FAILED;
    }

  if (au_fetch_class (class_obj, &smclass, AU_FETCH_READ, AU_SELECT)
      != NO_ERROR)
    {
      return er_errid ();
    }

  db_make_null (&ptype);
  db_make_null (&pname);
  db_make_null (&pexpr);
  db_make_null (&pattr);
  db_make_null (&attr);
  db_make_null (&hashsize);
  db_make_null (&pval);
  db_make_null (&partname);

  AU_DISABLE (au_save);

  if (db_get (smclass->partition_of, PARTITION_ATT_PNAME, &pname) != NO_ERROR || !DB_IS_NULL (&pname))	/* partitioned sub-class */
    {
      goto work_end;
    }

  if (db_get (smclass->partition_of, PARTITION_ATT_PTYPE, &ptype) != NO_ERROR)
    {
      goto work_end;
    }

  if (db_get (smclass->partition_of, PARTITION_ATT_PEXPR, &pexpr) != NO_ERROR
      || DB_IS_NULL (&pexpr))
    {
      goto work_end;
    }

  if (db_get (smclass->partition_of, PARTITION_ATT_PVALUES, &pattr) !=
      NO_ERROR || DB_IS_NULL (&pattr))
    {
      goto work_end;
    }

  if (set_size (pattr.data.set) >= 3)
    {
      char *p = NULL;

      if (set_get_element (pattr.data.set, 2, &attr) != NO_ERROR
	  || DB_IS_NULL (&attr) || (p = DB_GET_STRING (&attr)) == NULL)
	{
	  goto work_end;
	}
      if (p[0] == '*')
	{
	  partition_remove_mode = 1;
	}
      else if (p[0] == '#')
	{
	  partition_coalesce_mode = 1;
	  coalesce_part = atoi (&p[1]);
	  if (coalesce_part <= 0 ||
	      set_drop_seq_element (pattr.data.set, 2) != NO_ERROR)
	    {
	      goto work_end;
	    }
	  if (db_put_internal (smclass->partition_of,
			       PARTITION_ATT_PVALUES, &pattr) != NO_ERROR)
	    {
	      goto work_end;
	    }
	}
      else if (p[0] == '$')
	{
	  partition_reorg_mode = 1;
	  coalesce_part = atoi (&p[1]);
	  if (coalesce_part < 0
	      || set_drop_seq_element (pattr.data.set, 2) != NO_ERROR)
	    {
	      goto work_end;
	    }
	  if (db_put_internal (smclass->partition_of,
			       PARTITION_ATT_PVALUES, &pattr) != NO_ERROR)
	    goto work_end;
	}
    }

  if (set_get_element (pattr.data.set, 0, &attr) != NO_ERROR
      || set_get_element (pattr.data.set, 1, &hashsize) != NO_ERROR)
    {
      goto work_end;
    }

  if (hashsize.data.i <= 0)
    {
      goto work_end;
    }

  expr_parser = parser_create_parser ();
  if (expr_parser == NULL)
    {
      goto work_end;
    }

  enode = parser_parse_string (expr_parser, DB_GET_STRING (&pexpr));
  expr = (*enode)->info.query.q.select.list;
  if (!enode || !*enode || !expr)
    {
      goto work_end;
    }

  key_type =
    pt_db_to_type_enum (sm_att_type_id (class_obj, DB_GET_STRING (&attr)));

  parser_walk_tree (expr_parser, expr, adjust_name_with_type, &key_type, NULL,
		    NULL);
  pt_semantic_type (expr_parser, expr, NULL);

  xpi = regu_partition_info_alloc ();
  if (xpi == NULL)
    {
      goto work_end;
    }

  xpi->no_parts = hashsize.data.i;
  if (partition_coalesce_mode)
    {
      xpi->act_parts = coalesce_part;
    }
  else if (partition_reorg_mode)
    {
      xpi->act_parts = hashsize.data.i - coalesce_part;
    }
  else
    {
      xpi->act_parts = hashsize.data.i;
    }
  xpi->type = ptype.data.i;
  db_make_null (&pval);

  /* partition key to NULL-value replace */
  if (expr->node_type == PT_NAME)
    {
      parser_free_tree (expr_parser, expr);
      expr = pt_dbval_to_value (parser, &pval);
    }
  else
    {
      parser_walk_tree (expr_parser, expr, replace_name_with_value, &pval,
			NULL, NULL);
    }

  xpi->expr = pt_to_regu_variable (parser, expr, UNBOX_AS_VALUE);
  xpi->parts = regu_parts_array_alloc (xpi->no_parts);
  if (xpi->parts == NULL)
    {
      goto work_end;
    }
  if (partition_remove_mode)
    {
      xpi->key_attr = -1;
    }
  else
    {
      xpi->key_attr = sm_att_id (class_obj, DB_GET_STRING (&attr));
    }

  for (pi = 0, objs = smclass->users; objs; objs = objs->next)
    {
      if (au_fetch_class (objs->op, &subcls, AU_FETCH_READ, AU_SELECT)
	  != NO_ERROR)
	{
	  goto work_end;
	}

      if (!subcls->partition_of)
	{
	  continue;
	}

      delete_flag = 0;
      if (partition_coalesce_mode)
	{
	  if (db_get (subcls->partition_of, PARTITION_ATT_PNAME, &partname)
	      != NO_ERROR)
	    {
	      goto work_end;
	    }
	  partnum = atoi (DB_GET_STRING (&partname) + 1);
	  pr_clear_value (&partname);
	  if (partnum >= coalesce_part)
	    {
	      delete_flag = 1;
	    }
	}

      if (partition_reorg_mode)
	{
	  if (db_get (subcls->partition_of, PARTITION_ATT_PEXPR, &pval)
	      != NO_ERROR)
	    {
	      goto work_end;
	    }
	  if (!DB_IS_NULL (&pval))
	    {
	      delete_flag = 1;
	    }
	  pr_clear_value (&pval);
	}

      class_ = locator_has_heap (objs->op);
      if (class_ == NULL
	  || (hfid = sm_heap (class_)) == NULL
	  || (locator_flush_class (objs->op) != NO_ERROR))
	{
	  goto work_end;
	}

      class_oid = db_identifier (objs->op);
      if (!class_oid)
	{
	  goto work_end;
	}

      if (delete_flag)
	{
	  db_make_null (&pval);
	}
      else
	{
	  if (db_get (subcls->partition_of, PARTITION_ATT_PVALUES, &pval)
	      != NO_ERROR)
	    {
	      goto work_end;
	    }

	  if (DB_IS_NULL (&pval) || set_size (pval.data.set) <= 0)
	    {
	      goto work_end;
	    }
	}

      xpi->parts[pi] = regu_parts_info_alloc ();
      if (xpi->parts[pi] == NULL)
	{
	  goto work_end;
	}

      xpi->parts[pi]->class_oid = *class_oid;
      xpi->parts[pi]->class_hfid = *hfid;
      xpi->parts[pi]->vals = regu_dbval_alloc ();
      regu_dbval_type_init (xpi->parts[pi]->vals, DB_VALUE_TYPE (&pval));
      db_value_clone (&pval, xpi->parts[pi]->vals);
      pr_clear_value (&pval);
      pi++;
    }

  is_error = 0;
  if (idx <= 0)
    {
      xasl->proc.insert.partition = xpi;
    }
  else
    {
      xasl->proc.update.partition[idx - 1] = xpi;
    }

work_end:
  AU_ENABLE (au_save);

  pr_clear_value (&ptype);
  pr_clear_value (&pname);
  pr_clear_value (&pexpr);
  pr_clear_value (&pattr);
  pr_clear_value (&attr);
  pr_clear_value (&hashsize);
  pr_clear_value (&pval);
  if (expr_parser)
    parser_free_parser (expr_parser);

  if (is_error)
    {
      if (er_errid ())
	{
	  return er_errid ();
	}
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_PARTITION_WORK_FAILED, 0);
      return ER_PARTITION_WORK_FAILED;
    }
  else
    {
      return NO_ERROR;
    }
}

/*
 * do_check_partitioned_class() - Checks partitioned class
 *   return: Error code if check_map or keyattr is checked
 *   classop(in): MOP of class
 *   class_map(in/out): Checking method(CHECK_PARTITION_NONE, _PARTITION_PARENT,
 *			 _PARTITION_SUBS)
 *   keyattr(in): Partition key attribute to check
 *
 * Note:
 */
int
do_check_partitioned_class (DB_OBJECT * classop, int check_map, char *keyattr)
{
  int error = NO_ERROR;
  int is_partition = 0;
  char attr_name[DB_MAX_IDENTIFIER_LENGTH];

  error = do_is_partitioned_classobj (&is_partition, classop,
				      (keyattr) ? attr_name : NULL, NULL);
  if (error != NO_ERROR)
    return error;

  if (is_partition > 0)
    {
      if (((check_map & CHECK_PARTITION_PARENT) && is_partition == 1)
	  || ((check_map & CHECK_PARTITION_SUBS) && is_partition == 2))
	{
	  error = ER_NOT_ALLOWED_ACCESS_TO_PARTITION;
	}
      else if (keyattr)
	{
	  if (intl_mbs_casecmp (keyattr, attr_name) == 0)
	    {
	      error = ER_NOT_ALLOWED_ACCESS_TO_PARTITION;
	    }
	}

      if (error != NO_ERROR)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_NOT_ALLOWED_ACCESS_TO_PARTITION, 0);
	}
    }

  return error;
}


/*
 * do_is_partitioned_classobj() -
 *   return: NO_ERROR or error code
 *   is_partition(in/out): 0 if not partition, 1 if partition and parent,
 *                         2 if sub-partition
 *   classop(in): MOP of class
 *   keyattr(in): Partition key attribute to check
 *   sub_partitions(in): MOP of sub class
 *
 * Note:
 */
int
do_is_partitioned_classobj (int *is_partition,
			    DB_OBJECT * classop, char *keyattr,
			    MOP ** sub_partitions)
{
  enum
  {
    NOT_PARTITION_CLASS = 0, PARTITIONED_CLASS = 1, PARTITION_CLASS = 2
  };
  DB_OBJLIST *objs;
  SM_CLASS *smclass, *subcls;
  DB_VALUE pname, pattr, psize, attrname, pclassof, classobj;
  int au_save, pcnt, i;
  MOP *subobjs = NULL;
  int error;

  assert (classop != NULL);
  assert (is_partition != NULL);

  *is_partition = NOT_PARTITION_CLASS;

  AU_DISABLE (au_save);

  error = au_fetch_class (classop, &smclass, AU_FETCH_READ, AU_SELECT);
  if (error != NO_ERROR)
    {
      AU_ENABLE (au_save);
      return error;
    }
  if (!smclass->partition_of)
    {
      AU_ENABLE (au_save);
      return NO_ERROR;
    }

  db_make_null (&pname);
  db_make_null (&pattr);
  db_make_null (&psize);
  db_make_null (&attrname);
  db_make_null (&pclassof);
  db_make_null (&classobj);

  if (db_get (smclass->partition_of, PARTITION_ATT_PNAME, &pname) != NO_ERROR)
    {
      goto partition_failed;
    }
  *is_partition = (DB_IS_NULL (&pname) ? PARTITIONED_CLASS : PARTITION_CLASS);

  if (keyattr || sub_partitions)
    {
      if (*is_partition == PARTITION_CLASS)
	{			/* sub-partition */
	  if (db_get (smclass->partition_of, PARTITION_ATT_CLASSOF, &pclassof)
	      != NO_ERROR
	      || db_get (DB_GET_OBJECT (&pclassof), PARTITION_ATT_CLASSOF,
			 &classobj) != NO_ERROR
	      || au_fetch_class (DB_GET_OBJECT (&classobj), &smclass,
				 AU_FETCH_READ, AU_SELECT) != NO_ERROR)
	    {
	      goto partition_failed;
	    }
	}

      if (db_get (smclass->partition_of, PARTITION_ATT_PVALUES, &pattr)
	  != NO_ERROR)
	{
	  goto partition_failed;
	}
      if (set_get_element (pattr.data.set, 0, &attrname) != NO_ERROR)
	{
	  goto partition_failed;
	}
      if (set_get_element (pattr.data.set, 1, &psize) != NO_ERROR)
	{
	  goto partition_failed;
	}

      pcnt = psize.data.i;
      if (keyattr)
	{
	  char *p = NULL;

	  keyattr[0] = 0;
	  if (DB_IS_NULL (&attrname)
	      || (p = DB_GET_STRING (&attrname)) == NULL)
	    {
	      goto partition_failed;
	    }
	  strncpy (keyattr, p, DB_MAX_IDENTIFIER_LENGTH);
	}

      if (sub_partitions)
	{
	  subobjs = (MOP *) malloc (sizeof (MOP) * (pcnt + 1));
	  if (subobjs == NULL)
	    {
	      goto partition_failed;
	    }
	  memset (subobjs, 0, sizeof (MOP) * (pcnt + 1));

	  for (objs = smclass->users, i = 0; objs && i < pcnt;
	       objs = objs->next)
	    {
	      if (au_fetch_class (objs->op, &subcls, AU_FETCH_READ, AU_SELECT)
		  != NO_ERROR)
		{
		  goto partition_failed;
		}
	      if (!subcls->partition_of)
		{
		  continue;
		}
	      subobjs[i++] = objs->op;
	    }
	  if (i < pcnt)
	    {
	      goto partition_failed;
	    }

	  *sub_partitions = subobjs;
	}
    }

  AU_ENABLE (au_save);

  pr_clear_value (&pname);
  pr_clear_value (&pattr);
  pr_clear_value (&psize);
  pr_clear_value (&attrname);
  pr_clear_value (&pclassof);
  pr_clear_value (&classobj);

  return NO_ERROR;

partition_failed:

  AU_ENABLE (au_save);

  if (subobjs)
    {
      free_and_init (subobjs);
    }

  pr_clear_value (&pname);
  pr_clear_value (&pattr);
  pr_clear_value (&psize);
  pr_clear_value (&attrname);
  pr_clear_value (&pclassof);
  pr_clear_value (&classobj);

  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_PARTITION_WORK_FAILED, 0);

  return ER_PARTITION_WORK_FAILED;
}

/*
 * do_is_partitioned_subclass() -
 *   return: 1 if success, else error code
 *   is_partitioned(in/out):
 *   classname(in):
 *   keyattr(in):
 *
 * Note:
 */
int
do_is_partitioned_subclass (int *is_partitioned,
			    const char *classname, char *keyattr)
{
  MOP classop;
  SM_CLASS *smclass;
  DB_VALUE pname, pattr, attrname;
  int au_save, ret = 0;

  if (!classname)
    {
      return 0;
    }
  if (is_partitioned)
    {
      *is_partitioned = 0;
    }

  classop = db_find_class (classname);
  if (classop == NULL)
    {
      return 0;
    }

  AU_DISABLE (au_save);

  if (au_fetch_class (classop, &smclass, AU_FETCH_READ, AU_SELECT)
      != NO_ERROR || !smclass->partition_of)
    {
      AU_ENABLE (au_save);
      return 0;
    }

  db_make_null (&pname);
  if (db_get (smclass->partition_of, PARTITION_ATT_PNAME, &pname) != NO_ERROR)
    {
      AU_ENABLE (au_save);
      return 0;
    }

  if (!DB_IS_NULL (&pname))
    {
      ret = 1;			/* partitioned sub-class */
    }
  else
    {
      if (is_partitioned)
	{
	  *is_partitioned = 1;
	}

      if (keyattr)
	{
	  char *p = NULL;

	  keyattr[0] = 0;
	  db_make_null (&pattr);

	  if (db_get (smclass->partition_of, PARTITION_ATT_PVALUES,
		      &pattr) == NO_ERROR &&
	      set_get_element (pattr.data.set, 0, &attrname) == NO_ERROR
	      && !DB_IS_NULL (&attrname) && (p = DB_GET_STRING (&attrname)))
	    {
	      strncpy (keyattr, p, DB_MAX_IDENTIFIER_LENGTH);

	      pr_clear_value (&pattr);
	      pr_clear_value (&attrname);
	    }
	}
    }

  pr_clear_value (&pname);
  AU_ENABLE (au_save);

  return ret;
}

/*
 * do_drop_partition() -
 *   return: Error code
 *   class(in):
 *   drop_sub_flag(in):
 *
 * Note:
 */
int
do_drop_partition (MOP class_, int drop_sub_flag)
{
  DB_OBJLIST *objs;
  SM_CLASS *smclass, *subclass;
  DB_VALUE pname;
  int au_save;
  MOP delobj, delpart;
  int error = NO_ERROR;

  if (!class_)
    {
      return -1;
    }

  AU_DISABLE (au_save);

  db_make_null (&pname);
  error = au_fetch_class (class_, &smclass, AU_FETCH_READ, AU_SELECT);
  if (error != NO_ERROR)
    {
      goto fail_return;
    }
  if (!smclass->partition_of)
    {
      goto fail_return;
    }

  error = db_get (smclass->partition_of, PARTITION_ATT_PNAME, &pname);
  if (error != NO_ERROR)
    {
      goto fail_return;
    }
  if (!DB_IS_NULL (&pname))
    {
      goto fail_return;		/* partitioned sub-class */
    }

  error = obj_delete (smclass->partition_of);
  if (error != NO_ERROR)
    {
      goto fail_return;
    }

  for (objs = smclass->users; objs;)
    {
      error = au_fetch_class (objs->op, &subclass, AU_FETCH_READ, AU_SELECT);
      if (error != NO_ERROR)
	{
	  goto fail_return;
	}
      if (subclass->partition_of)
	{
	  delpart = subclass->partition_of;
	  delobj = objs->op;
	  objs = objs->next;
	  if (drop_sub_flag)
	    {
	      error = sm_delete_class_mop (delobj);
	      if (error != NO_ERROR)
		{
		  goto fail_return;
		}
	    }
	  error = obj_delete (delpart);
	  if (error != NO_ERROR)
	    {
	      goto fail_return;
	    }
	}
      else
	{
	  objs = objs->next;
	}
    }

  error = NO_ERROR;

fail_return:
  AU_ENABLE (au_save);

  pr_clear_value (&pname);
  return error;
}

/*
 * partition_rename() -
 *   return: Error code
 *   old_class(in):
 *   newname(in):
 *
 * Note:
 */
int
do_rename_partition (MOP old_class, const char *newname)
{
  DB_OBJLIST *objs;
  SM_CLASS *smclass, *subclass;
  int au_save, newlen;
  int error;
  char new_subname[PARTITION_VARCHAR_LEN + 1], *ptr;

  if (!old_class || !newname)
    {
      return -1;
    }

  newlen = strlen (newname);

  AU_DISABLE (au_save);

  error = au_fetch_class (old_class, &smclass, AU_FETCH_READ, AU_SELECT);
  if (error != NO_ERROR)
    {
      goto end_rename;
    }

  for (objs = smclass->users; objs; objs = objs->next)
    {
      if (au_fetch_class (objs->op, &subclass, AU_FETCH_READ, AU_SELECT)
	  == NO_ERROR && subclass->partition_of)
	{
	  ptr =
	    strstr ((char *) subclass->header.name,
		    PARTITIONED_SUB_CLASS_TAG);
	  if (ptr == NULL)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_PARTITION_WORK_FAILED, 0);
	      goto end_rename;
	    }

	  if ((newlen + strlen (ptr)) >= PARTITION_VARCHAR_LEN)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_PARTITION_WORK_FAILED, 0);
	      goto end_rename;
	    }
	  sprintf (new_subname, "%s%s", newname, ptr);

	  error = sm_rename_class (objs->op, new_subname);
	  if (error != NO_ERROR)
	    {
	      break;
	    }
	}
    }

end_rename:
  AU_ENABLE (au_save);

  return error;
}

/*
 * do_is_partition_changed() -
 *   return: MOP of class
 *   parser(in): Parser context
 *   smclass(in):
 *   editobj(in):
 *   list_column_names(in):
 *   list_values(in):
 *   const_column_names(in):
 *   const_values(in):
 *
 * Note:
 */
MOP
do_is_partition_changed (PARSER_CONTEXT * parser, SM_CLASS * smclass,
			 MOP editobj, PT_NODE * list_column_names,
			 DB_VALUE * list_values, PT_NODE * const_column_names,
			 DB_VALUE * const_values)
{
  SM_CLASS *supclass;
  DB_VALUE ptype, pname, pexpr, pattr, *retval, attrname, *valptr;
  MOP chgobj = NULL;
  PT_NODE *name;
  char flag_prc, *nameptr;
  int au_save;

  if (!smclass || !editobj || !smclass->partition_of || !smclass->inheritance)
    {
      return NULL;
    }
  if (au_fetch_class (smclass->inheritance->op, &supclass, AU_FETCH_READ,
		      AU_SELECT) != NO_ERROR)
    {
      return NULL;
    }

  db_make_null (&ptype);
  db_make_null (&pname);
  db_make_null (&pexpr);
  db_make_null (&pattr);
  db_make_null (&attrname);

  AU_DISABLE (au_save);

  if (db_get (supclass->partition_of, PARTITION_ATT_PNAME, &pname) !=
      NO_ERROR)
    {
      goto end_partition;
    }

  /* adjust only partition parent class */
  if (DB_IS_NULL (&pname))
    {
      if (db_get (supclass->partition_of, PARTITION_ATT_PTYPE, &ptype)
	  != NO_ERROR
	  || db_get (supclass->partition_of, PARTITION_ATT_PEXPR,
		     &pexpr) != NO_ERROR
	  || db_get (supclass->partition_of, PARTITION_ATT_PVALUES,
		     &pattr) != NO_ERROR)
	{
	  goto end_partition;
	}

      if (set_get_element (pattr.data.set, 0, &attrname) != NO_ERROR)
	{
	  goto end_partition;
	}
      nameptr = DB_GET_STRING (&attrname);

      /* partition key column search */
      valptr = list_values;
      flag_prc = 0;

      for (name = list_column_names; name != NULL; name = name->next)
	{
	  if (SM_COMPARE_NAMES (nameptr, name->info.name.original) == 0)
	    {
	      flag_prc = 1;
	      break;
	    }
	  valptr++;
	}

      if (!flag_prc)
	{
	  valptr = const_values;
	  for (name = const_column_names; name != NULL; name = name->next)
	    {
	      if (SM_COMPARE_NAMES (nameptr, name->info.name.original) == 0)
		{
		  flag_prc = 1;
		  break;
		}
	      valptr++;
	    }
	}
      if (!flag_prc)		/* partition key column not found! */
	{
	  goto end_partition;
	}

      retval = evaluate_partition_expr (&pexpr, valptr);
      if (retval == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_PARTITION_WORK_FAILED,
		  0);
	  goto end_partition;
	}
      get_partition_parts (&chgobj, supclass, ptype.data.i, &pattr, retval);
      if (chgobj && ws_mop_compare (editobj, chgobj) == 0)
	{
	  chgobj = NULL;	/* same partition */
	}
    }
  else
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_PARTITION_WORK_FAILED, 0);
      goto end_partition;
    }

end_partition:
  pr_clear_value (&ptype);
  pr_clear_value (&pname);
  pr_clear_value (&pexpr);
  pr_clear_value (&pattr);
  pr_clear_value (&attrname);

  AU_ENABLE (au_save);

  return chgobj;
}

/*
 * do_update_partition_newly() -
 *   return: Error code
 *   classname(in):
 *   keyname(in):
 *
 * Note:
 */
int
do_update_partition_newly (const char *classname, const char *keyname)
{
  int error = NO_ERROR;
  DB_QUERY_RESULT *query_result;
  DB_QUERY_ERROR query_error;
  char *sqlbuf;

  sqlbuf = (char *) malloc (20 + strlen (classname) + strlen (keyname) * 2);
  if (sqlbuf == NULL)
    {
      return -1;
    }
  sprintf (sqlbuf, "UPDATE %s SET %s=%s;", classname, keyname, keyname);

  error = db_execute (sqlbuf, &query_result, &query_error);
  if (error >= 0)
    {
      error = NO_ERROR;
      db_query_end (query_result);
    }
  free_and_init (sqlbuf);

  return error;
}

/*
 * do_remove_partition_pre() -
 *   return: Error code
 *   clstmp(in):
 *   keynattr(in):
 *   magic_word(in):
 *
 * Note:
 */
int
do_remove_partition_pre (DB_CTMPL * clstmpl, char *keyattr,
			 const char *magic_word)
{
  int error = NO_ERROR;
  DB_VALUE pattr, attrname, star;
  int au_save;

  if (clstmpl && keyattr)
    {
      if (clstmpl->partition_of)
	{
	  char *p = NULL;

	  AU_DISABLE (au_save);

	  keyattr[0] = 0;
	  db_make_null (&pattr);
	  error = db_get (clstmpl->partition_of, PARTITION_ATT_PVALUES,
			  &pattr);
	  if (error == NO_ERROR &&
	      (error
	       = set_get_element (pattr.data.set, 0, &attrname)) == NO_ERROR
	      && !DB_IS_NULL (&attrname) && (p = DB_GET_STRING (&attrname)))
	    {
	      strncpy (keyattr, p, DB_MAX_IDENTIFIER_LENGTH);

	      /* '*' set to 3rd element - partition remove mode update */
	      /* '#Number' set to 3rd element - partition coalesce mode update */
	      db_make_string (&star, magic_word);
	      error = set_add_element (pattr.data.set, &star);
	      if (error == NO_ERROR)
		{
		  error = db_put_internal (clstmpl->partition_of,
					   PARTITION_ATT_PVALUES, &pattr);
		}

	      pr_clear_value (&pattr);
	      pr_clear_value (&attrname);
	      pr_clear_value (&star);
	    }

	  AU_ENABLE (au_save);
	}
    }

  return error;
}

/*
 * do_remove_partition_post() -
 *   return: Error code
 *   parser(in): Parser context
 *   classname(in):
 *   keyname(in):
 *
 * Note:
 */
int
do_remove_partition_post (PARSER_CONTEXT * parser, const char *classname,
			  const char *keyname)
{
  int error = NO_ERROR;
  DB_CTMPL *ctmpl;
  MOP vclass;

  error = do_update_partition_newly (classname, keyname);
  if (error == NO_ERROR)
    {
      vclass = db_find_class (classname);
      if (vclass == NULL)
	{
	  error = er_errid ();
	  return error;
	}

      error = do_drop_partition (vclass, 1);
      if (error != NO_ERROR)
	{
	  return error;
	}

      ctmpl = dbt_edit_class (vclass);
      if (ctmpl != NULL)
	{
	  ctmpl->partition_of = NULL;

	  if (dbt_finish_class (ctmpl) == NULL)
	    {
	      error = er_errid ();
	      dbt_abort_class(ctmpl);
	    }
	  else if (locator_flush_class (vclass) != NO_ERROR)
	    {
	      error = er_errid ();
	    }

	}
      else
	{
	  error = er_errid ();
	}
    }

  return error;
}

/*
 * adjust_partition_range() -
 *   return: Error code
 *   objs(in):
 *
 * Note:
 */
static int
adjust_partition_range (DB_OBJLIST * objs)
{
  DB_OBJLIST *subs;
  SM_CLASS *subclass;
  DB_VALUE ptype, pexpr, pattr, minval, maxval, seqval, *wrtval;
  int error = NO_ERROR;
  int au_save;
  char check_flag = 1;
  DB_VALUE_SLIST *ranges = NULL, *rfind, *new_range, *prev_range;
  DB_COLLECTION *dbc = NULL;

  db_make_null (&ptype);
  db_make_null (&pattr);
  db_make_null (&minval);
  db_make_null (&maxval);

  AU_DISABLE (au_save);
  for (subs = objs; subs; subs = subs->next)
    {
      error = au_fetch_class (subs->op, &subclass, AU_FETCH_READ, AU_SELECT);
      if (error != NO_ERROR)
	{
	  break;
	}
      if (!subclass->partition_of)
	{
	  continue;
	}

      if (check_flag)
	{			/* RANGE check */
	  error = db_get (subclass->partition_of, PARTITION_ATT_PTYPE,
			  &ptype);
	  if (error != NO_ERROR)
	    {
	      break;
	    }
	  if (ptype.data.i != PT_PARTITION_RANGE)
	    {
	      break;
	    }
	  check_flag = 0;
	}

      error = db_get (subclass->partition_of, PARTITION_ATT_PVALUES, &pattr);
      if (error != NO_ERROR)
	{
	  break;
	}
      error = db_get (subclass->partition_of, PARTITION_ATT_PEXPR, &pexpr);
      if (error != NO_ERROR)
	{
	  break;
	}
      if (!DB_IS_NULL (&pexpr))
	{
	  pr_clear_value (&pattr);
	  pr_clear_value (&pexpr);
	  continue;		/* reorg deleted partition */
	}

      error = set_get_element (pattr.data.set, 0, &minval);
      if (error != NO_ERROR)
	{
	  break;
	}
      error = set_get_element (pattr.data.set, 1, &maxval);
      if (error != NO_ERROR)
	{
	  break;
	}
      if ((new_range =
	   (DB_VALUE_SLIST *) malloc (sizeof (DB_VALUE_SLIST))) == NULL)
	{
	  error = ER_OUT_OF_VIRTUAL_MEMORY;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
	  break;
	}
      new_range->partition_of = subclass->partition_of;
      new_range->min = db_value_copy (&minval);
      new_range->max = db_value_copy (&maxval);
      new_range->next = NULL;
      pr_clear_value (&minval);
      pr_clear_value (&maxval);
      pr_clear_value (&pattr);

      if (ranges == NULL)
	{
	  ranges = new_range;
	}
      else
	{			/* sort ranges */
	  for (rfind = ranges, prev_range = NULL; rfind; rfind = rfind->next)
	    {
	      if (DB_IS_NULL (rfind->max)
		  || db_value_compare (rfind->max, new_range->max) == DB_GT)
		{
		  if (prev_range == NULL)
		    {
		      new_range->next = ranges;
		      ranges = new_range;
		    }
		  else
		    {
		      new_range->next = prev_range->next;
		      prev_range->next = new_range;
		    }
		  break;
		}
	      prev_range = rfind;
	    }

	  if (rfind == NULL)
	    {
	      prev_range->next = new_range;
	    }
	}
    }

  for (rfind = ranges, prev_range = NULL; rfind; rfind = rfind->next)
    {
      wrtval = NULL;
      if (prev_range == NULL)
	{			/* Min value of first range is low infinite */
	  if (!DB_IS_NULL (rfind->min))
	    {
	      db_make_null (&minval);
	      wrtval = &minval;
	    }
	}
      else
	{
	  if (db_value_compare (prev_range->max, rfind->min) != DB_EQ)
	    {
	      wrtval = prev_range->max;
	    }
	}
      if (wrtval != NULL)
	{			/* adjust min value of range */
	  dbc = set_create_sequence (0);
	  if (dbc != NULL)
	    {
	      set_add_element (dbc, wrtval);
	      set_add_element (dbc, rfind->max);
	      db_make_sequence (&seqval, dbc);
	      error = db_put_internal (rfind->partition_of,
				       PARTITION_ATT_PVALUES, &seqval);
	      set_free (dbc);
	    }
	  if (error != NO_ERROR)
	    {
	      break;
	    }
	}
      prev_range = rfind;
    }

  for (rfind = ranges; rfind;)
    {
      db_value_free (rfind->min);
      db_value_free (rfind->max);
      prev_range = rfind->next;
      free_and_init (rfind);
      rfind = prev_range;
    }
  pr_clear_value (&ptype);
  pr_clear_value (&pattr);
  pr_clear_value (&minval);
  pr_clear_value (&maxval);
  AU_ENABLE (au_save);
  return error;
}

/*
 * adjust_partition_size() -
 *   return: Error code
 *   class(in):
 *
 * Note:
 */
static int
adjust_partition_size (MOP class_)
{
  int error = NO_ERROR;
  SM_CLASS *smclass, *subclass;
  DB_VALUE pattr, keyname, psize;
  DB_OBJLIST *subs;
  int au_save, partcnt;

  if (!class_)
    {
      return -1;
    }
  error = au_fetch_class (class_, &smclass, AU_FETCH_READ, AU_SELECT);
  if (error != NO_ERROR)
    {
      return error;
    }

  if (!smclass->partition_of)
    {
      error = ER_INVALID_PARTITION_REQUEST;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
      return error;
    }

  db_make_null (&psize);
  db_make_null (&pattr);
  db_make_null (&keyname);

  AU_DISABLE (au_save);
  for (subs = smclass->users, partcnt = 0; subs; subs = subs->next)
    {
      error = au_fetch_class (subs->op, &subclass, AU_FETCH_READ, AU_SELECT);
      if (error != NO_ERROR)
	{
	  goto fail_end;
	}
      if (!subclass->partition_of)
	{
	  continue;
	}
      partcnt++;
    }
  error = db_get (smclass->partition_of, PARTITION_ATT_PVALUES, &pattr);
  if (error != NO_ERROR)
    {
      goto fail_end;
    }
  error = set_get_element (pattr.data.set, 0, &keyname);
  if (error != NO_ERROR)
    {
      goto fail_end;
    }
  error = set_get_element (pattr.data.set, 1, &psize);
  if (error != NO_ERROR)
    {
      goto fail_end;
    }
  if (psize.data.i != partcnt)
    {
      psize.data.i = partcnt;
      error = set_put_element (pattr.data.set, 1, &psize);
      if (error != NO_ERROR)
	{
	  goto fail_end;
	}
      error =
	db_put_internal (smclass->partition_of, PARTITION_ATT_PVALUES,
			 &pattr);
      if (error != NO_ERROR)
	{
	  goto fail_end;
	}
    }
  error = NO_ERROR;

fail_end:
  pr_clear_value (&keyname);
  pr_clear_value (&psize);
  pr_clear_value (&pattr);
  AU_ENABLE (au_save);
  return error;
}

/*
 * do_get_partition_size() -
 *   return: Size if success, else error code
 *   class(in):
 *
 * Note:
 */
int
do_get_partition_size (MOP class_)
{
  int error = NO_ERROR;
  SM_CLASS *smclass;
  DB_VALUE pattr, psize;
  int au_save;

  if (!class_)
    {
      return -1;
    }
  error = au_fetch_class (class_, &smclass, AU_FETCH_READ, AU_SELECT);
  if (error != NO_ERROR)
    {
      return error;
    }

  if (!smclass->partition_of)
    {
      error = ER_INVALID_PARTITION_REQUEST;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
      return error;
    }

  db_make_null (&psize);
  db_make_null (&pattr);

  AU_DISABLE (au_save);
  error = db_get (smclass->partition_of, PARTITION_ATT_PVALUES, &pattr);
  if (error != NO_ERROR)
    {
      goto fail_end;
    }
  error = set_get_element (pattr.data.set, 1, &psize);
  if (error != NO_ERROR)
    {
      goto fail_end;
    }
  error = psize.data.i;
  if (error == 0)
    {
      error = -1;
    }

fail_end:
  pr_clear_value (&psize);
  pr_clear_value (&pattr);
  AU_ENABLE (au_save);
  return error;
}

/*
 * do_get_partition_keycol() -
 *   return: Error code
 *   keycol(out):
 *   class(in):
 *
 * Note:
 */
int
do_get_partition_keycol (char *keycol, MOP class_)
{
  int error = NO_ERROR;
  SM_CLASS *smclass;
  DB_VALUE pattr, keyname;
  int au_save;
  char *keyname_str;

  if (!class_ || !keycol)
    {
      return -1;
    }
  error = au_fetch_class (class_, &smclass, AU_FETCH_READ, AU_SELECT);
  if (error != NO_ERROR)
    {
      return error;
    }

  if (!smclass->partition_of)
    {
      error = ER_INVALID_PARTITION_REQUEST;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
      return error;
    }

  db_make_null (&keyname);
  db_make_null (&pattr);

  AU_DISABLE (au_save);
  error = db_get (smclass->partition_of, PARTITION_ATT_PVALUES, &pattr);
  if (error != NO_ERROR)
    {
      goto fail_end;
    }
  error = set_get_element (pattr.data.set, 0, &keyname);
  if (error != NO_ERROR)
    {
      goto fail_end;
    }

  if (DB_IS_NULL (&keyname))
    {
      goto fail_end;
    }
  keyname_str = DB_GET_STRING (&keyname);
  strncpy (keycol, keyname_str, DB_MAX_IDENTIFIER_LENGTH);
  error = NO_ERROR;

fail_end:
  pr_clear_value (&keyname);
  pr_clear_value (&pattr);
  AU_ENABLE (au_save);
  return error;
}

/*
 * do_get_partition_keycol() -
 *   return: Error code
 *   class(in):
 *   name_list(in):
 *
 * Note:
 */
int
do_drop_partition_list (MOP class_, PT_NODE * name_list)
{
  PT_NODE *names;
  int error = NO_ERROR;
  char subclass_name[DB_MAX_IDENTIFIER_LENGTH];
  SM_CLASS *smclass, *subclass;
  int au_save;
  MOP delpart, classcata;

  if (!class_ || !name_list)
    {
      return -1;
    }

  error = au_fetch_class (class_, &smclass, AU_FETCH_READ, AU_SELECT);
  if (error != NO_ERROR)
    {
      return error;
    }

  if (!smclass->partition_of)
    {
      error = ER_INVALID_PARTITION_REQUEST;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
      return error;
    }

  for (names = name_list; names; names = names->next)
    {
      sprintf (subclass_name, "%s" PARTITIONED_SUB_CLASS_TAG "%s",
	       smclass->header.name, names->info.name.original);
      classcata = sm_find_class (subclass_name);
      if (classcata == NULL)
	{
	  return er_errid ();
	}

      error = au_fetch_class (classcata, &subclass, AU_FETCH_READ, AU_SELECT);
      if (error != NO_ERROR)
	{
	  return error;
	}
      if (subclass->partition_of)
	{
	  delpart = subclass->partition_of;
	  error = sm_delete_class_mop (classcata);
	  if (error != NO_ERROR)
	    {
	      return error;
	    }
	  AU_DISABLE (au_save);
	  error = obj_delete (delpart);
	  if (error != NO_ERROR)
	    {
	      AU_ENABLE (au_save);
	      return error;
	    }
	  AU_ENABLE (au_save);
	}
      else
	{
	  error = ER_PARTITION_NOT_EXIST;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
	  return error;
	}
    }

  adjust_partition_range (smclass->users);
  adjust_partition_size (class_);
  return NO_ERROR;
}
