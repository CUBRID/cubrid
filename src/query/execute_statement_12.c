/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * do_stat.c - Parse tree to update statistics translation.
 */

#ident "$Id$"

#include "config.h"

#include "db.h"
#include "error_manager.h"
#include "parser.h"
#include "schema_manager_3.h"
#include "execute_schema_8.h"
#include "execute_statement_11.h"
#include "dbval.h"


typedef enum
{
  CST_UNDEFINED,
  CST_NOBJECTS, CST_NPAGES, CST_NATTRIBUTES,
  CST_ATTR_MIN, CST_ATTR_MAX, CST_ATTR_NINDEXES,
  CST_BT_NLEAFS, CST_BT_NPAGES, CST_BT_HEIGHT, CST_BT_NKEYS,
  CST_BT_NOIDS, CST_BT_NNULLS, CST_BT_NUKEYS
} CST_ITEM_ENUM;

typedef struct cst_item CST_ITEM;
struct cst_item
{
  CST_ITEM_ENUM item;
  const char *string;
  int att_id;
  int bt_idx;
};

static CST_ITEM cst_item_tbl[] = {
  {CST_NOBJECTS, "#objects", -1, -1},
  {CST_NPAGES, "#pages", -1, -1},
  {CST_NATTRIBUTES, "#attributes", -1, -1},
  {CST_ATTR_MIN, "min", 0, -1},
  {CST_ATTR_MAX, "max", 0, -1},
#if 0
  {CST_ATTR_NINDEXES, "#indexes", 0, -1},
  {CST_BT_NLEAFS, "#leaf_pages", 0, 0},
  {CST_BT_NPAGES, "#index_pages", 0, 0},
  {CST_BT_HEIGHT, "index_height", 0, 0},
#endif
  {CST_BT_NKEYS, "#keys", 0, 0},
#if 0
  {CST_BT_NOIDS, "#oids", 0, 0},
  {CST_BT_NNULLS, "#nulls", 0, 0},
  {CST_BT_NUKEYS, "#unique_keys", 0, 0},
#endif
  {CST_UNDEFINED, "", 0, 0}
};

static char *extract_att_name (const char *str);
static int extract_bt_idx (const char *str);
static int make_cst_item_value (DB_OBJECT * obj, const char *str,
				DB_VALUE * db_val);

/*
 * do_update_stats() - Updates the statistics of a list of classes
 *		       or ALL classes
 *   return: Error code
 *   parser(in): Parser context
 *   statement(in/out): Parse tree of a update statistics statement
 */
int
do_update_stats (PARSER_CONTEXT * parser, PT_NODE * statement)
{
  PT_NODE *cls = NULL;
  int error = NO_ERROR;
  DB_OBJECT *obj;
  int is_partition = 0, i;
  MOP *sub_partitions = NULL;

  if (statement->info.update_stats.all_classes > 0)
    {
      return sm_update_all_statistics ();
    }
  else if (statement->info.update_stats.all_classes < 0)
    {
      return sm_update_all_catalog_statistics ();
    }
  else
    {
      for (cls = statement->info.update_stats.class_list;
	   cls != NULL && error == NO_ERROR; cls = cls->next)
	{
	  obj = db_find_class (cls->info.name.original);
	  if (obj)
	    {
	      cls->info.name.db_object = obj;
	      pt_check_user_owns_class (parser, cls);
	    }
	  else
	    {
	      return er_errid ();
	    }

	  error = sm_update_statistics (obj);
	  error = do_is_partitioned_classobj (&is_partition, obj, NULL,
					      &sub_partitions);
	  if (error != NO_ERROR)
	    {
	      return error;
	    }

	  if (is_partition == 1)
	    {
	      for (i = 0; sub_partitions[i]; i++)
		{
		  error = sm_update_statistics (sub_partitions[i]);
		  if (error != NO_ERROR)
		    break;
		}
	      free_and_init (sub_partitions);
	    }
	}
      return error;
    }
}

/*
 * extract_att_name() -
 *   return:
 *   str(in):
 */
static char *
extract_att_name (const char *str)
{
  char *s, *t, *att = NULL;
  int size;

  s = intl_mbs_chr (str, '(');
  if (s && *(++s))
    {
      t = intl_mbs_chr (s, ')');
      if (!t)
	{
	  t = intl_mbs_chr (s, ':');
	}
      if (t && t != s)
	{
	  size = t - s;
	  att = (char *) malloc (size + 1);
	  if (att)
	    {
	      intl_mbs_ncpy (att, s, size);
	      att[size] = '\0';
	    }
	}
    }
  return att;
}

/*
 * extract_bt_idx() -
 *   return:
 *   str(in):
 */
static int
extract_bt_idx (const char *str)
{
  char *s, *t;
  int idx = -1;

  t = intl_mbs_chr (str, '(');
  if (t && *(++t))
    {
      s = intl_mbs_chr (t, ':');
      if (s && s != t && *(++s))
	{
	  t = intl_mbs_chr (s, ')');
	  if (t && t != s)
	    {
	      idx = atoi (s);
	    }
	}
    }
  return idx;
}

/*
 * make_cst_item_value() -
 *   return: Error code
 *   obj(in):
 *   str(in):
 *   db_val(in):
 */
static int
make_cst_item_value (DB_OBJECT * obj, const char *str, DB_VALUE * db_val)
{
  CST_ITEM cst_item = { CST_UNDEFINED, "", -1, -1 };
  char *att_name = NULL;
  int bt_idx;
  CLASS_STATS *class_statsp = NULL;
  ATTR_STATS *attr_statsp = NULL;
  BTREE_STATS *bt_statsp = NULL;
  int i;
  int error;

  for (i = 0; i < (signed) DIM (cst_item_tbl); i++)
    {
      if (intl_mbs_ncasecmp (str, cst_item_tbl[i].string,
			     strlen (cst_item_tbl[i].string)) == 0)
	{
	  cst_item = cst_item_tbl[i];
	  if (cst_item.att_id >= 0)
	    {
	      att_name = extract_att_name (str);
	      if (att_name == NULL)
		{
		  cst_item.item = CST_UNDEFINED;
		  break;
		}
	      cst_item.att_id = sm_att_id (obj, att_name);
	      if (cst_item.att_id < 0)
		{
		  cst_item.item = CST_UNDEFINED;
		  break;
		}
	      free_and_init (att_name);
	      if (cst_item.bt_idx >= 0)
		{
		  bt_idx = extract_bt_idx (str);
		  if (bt_idx <= 0)
		    {
		      cst_item.item = CST_UNDEFINED;
		      break;
		    }
		  cst_item.bt_idx = bt_idx;
		}
	    }
	  break;
	}
    }
  if (cst_item.item == CST_UNDEFINED)
    {
      db_make_null (db_val);
      error = ER_DO_UNDEFINED_CST_ITEM;
      er_set (ER_SYNTAX_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
      return error;
    }

  class_statsp = sm_get_statistics_force (obj);
  if (class_statsp == NULL)
    {
      db_make_null (db_val);
      return er_errid ();
    }
  if (cst_item.att_id >= 0)
    {
      for (i = 0; i < class_statsp->n_attrs; i++)
	{
	  if (class_statsp->attr_stats[i].id == cst_item.att_id)
	    {
	      attr_statsp = &class_statsp->attr_stats[i];
	      break;
	    }
	}
    }
  if (attr_statsp &&
      cst_item.bt_idx > 0 && cst_item.bt_idx <= attr_statsp->n_btstats)
    {
      for (i = 0; i < cst_item.bt_idx; i++)
	{
	  ;
	}
      bt_statsp = &attr_statsp->bt_stats[i];
    }

  switch (cst_item.item)
    {
    case CST_NOBJECTS:
      db_make_int (db_val, class_statsp->num_objects);
      break;
    case CST_NPAGES:
      db_make_int (db_val, class_statsp->heap_size);
      break;
    case CST_NATTRIBUTES:
      db_make_int (db_val, class_statsp->n_attrs);
      break;
    case CST_ATTR_MIN:
      if (!attr_statsp)
	{
	  db_make_null (db_val);
	}
      else
	switch (attr_statsp->type)
	  {
	  case DB_TYPE_INTEGER:
	    db_make_int (db_val, attr_statsp->min_value.i);
	    break;
	  case DB_TYPE_SHORT:
	    db_make_short (db_val, attr_statsp->min_value.i);
	    break;
	  case DB_TYPE_FLOAT:
	    db_make_float (db_val, attr_statsp->min_value.f);
	    break;
	  case DB_TYPE_DOUBLE:
	    db_make_double (db_val, attr_statsp->min_value.d);
	    break;
	  case DB_TYPE_DATE:
	    db_value_put_encoded_date (db_val, &attr_statsp->min_value.date);
	    break;
	  case DB_TYPE_TIME:
	    db_value_put_encoded_time (db_val, &attr_statsp->min_value.time);
	    break;
	  case DB_TYPE_UTIME:
	    db_make_timestamp (db_val, attr_statsp->min_value.utime);
	    break;
	  case DB_TYPE_MONETARY:
	    db_make_monetary (db_val,
			      attr_statsp->min_value.money.type,
			      attr_statsp->min_value.money.amount);
	    break;
	  default:
	    db_make_null (db_val);
	    break;
	  }
      break;
    case CST_ATTR_MAX:
      if (!attr_statsp)
	{
	  db_make_null (db_val);
	}
      else
	{
	  switch (attr_statsp->type)
	    {
	    case DB_TYPE_INTEGER:
	      db_make_int (db_val, attr_statsp->max_value.i);
	      break;
	    case DB_TYPE_SHORT:
	      db_make_short (db_val, attr_statsp->max_value.i);
	      break;
	    case DB_TYPE_FLOAT:
	      db_make_float (db_val, attr_statsp->max_value.f);
	      break;
	    case DB_TYPE_DOUBLE:
	      db_make_double (db_val, attr_statsp->max_value.d);
	      break;
	    case DB_TYPE_DATE:
	      db_value_put_encoded_date (db_val,
					 &attr_statsp->max_value.date);
	      break;
	    case DB_TYPE_TIME:
	      db_value_put_encoded_time (db_val,
					 &attr_statsp->max_value.time);
	      break;
	    case DB_TYPE_UTIME:
	      db_make_timestamp (db_val, attr_statsp->max_value.utime);
	      break;
	    case DB_TYPE_MONETARY:
	      db_make_monetary (db_val,
				attr_statsp->max_value.money.type,
				attr_statsp->max_value.money.amount);
	      break;
	    default:
	      db_make_null (db_val);
	      break;
	    }
	}
      break;
    case CST_ATTR_NINDEXES:
      if (!attr_statsp)
	{
	  db_make_null (db_val);
	}
      else
	{
	  db_make_int (db_val, attr_statsp->n_btstats);
	}
      break;
    case CST_BT_NLEAFS:
      if (!attr_statsp || !bt_statsp)
	{
	  db_make_null (db_val);
	}
      else
	{
	  db_make_int (db_val, bt_statsp->leafs);
	}
      break;
    case CST_BT_NPAGES:
      if (!attr_statsp || !bt_statsp)
	{
	  db_make_null (db_val);
	}
      else
	{
	  db_make_int (db_val, bt_statsp->pages);
	}
      break;
    case CST_BT_HEIGHT:
      if (!attr_statsp || !bt_statsp)
	{
	  db_make_null (db_val);
	}
      else
	{
	  db_make_int (db_val, bt_statsp->height);
	}
      break;
    case CST_BT_NKEYS:
      if (!attr_statsp || !bt_statsp)
	{
	  db_make_null (db_val);
	}
      else
	{
	  db_make_int (db_val, bt_statsp->keys);
	}
      break;
    case CST_BT_NOIDS:
      if (!attr_statsp || !bt_statsp)
	{
	  db_make_null (db_val);
	}
      else
	{
	  db_make_int (db_val, bt_statsp->oids);
	}
      break;
    case CST_BT_NNULLS:
      if (!attr_statsp || !bt_statsp)
	{
	  db_make_null (db_val);
	}
      else
	{
	  db_make_int (db_val, bt_statsp->nulls);
	}
      break;
    case CST_BT_NUKEYS:
      if (!attr_statsp || !bt_statsp)
	{
	  db_make_null (db_val);
	}
      else
	{
	  db_make_int (db_val, bt_statsp->ukeys);
	}
      break;
    default:
      break;
    }

  return NO_ERROR;
}				/* make_cst_item_value() */

/*
 * do_get_stats() -
 *   return: Error code
 *   parser(in): Parser context
 *   statement(in/out): Parse tree of a get statistics statement
 */
int
do_get_stats (PARSER_CONTEXT * parser, PT_NODE * statement)
{
  PT_NODE *cls, *arg, *into;
  DB_OBJECT *obj;
  DB_VALUE *ret_val, db_val;
  int error;

  cls = statement->info.get_stats.class_;
  arg = statement->info.get_stats.args;
  into = statement->info.get_stats.into_var;
  if (!cls || !arg)
    {
      return ER_OBJ_INVALID_ARGUMENTS;
    }

  obj = db_find_class (cls->info.name.original);
  if (!obj)
    {
      return ER_OBJ_INVALID_ARGUMENTS;
    }

  cls->info.name.db_object = obj;
  (void) pt_check_user_owns_class (parser, cls);

  ret_val = db_value_create ();
  if (ret_val == NULL)
    return er_errid ();

  pt_evaluate_tree (parser, arg, &db_val);
  if (parser->error_msgs)
    {
      return ER_OBJ_INVALID_ARGUMENTS;
    }
  error = make_cst_item_value (obj, DB_GET_STRING (&db_val), ret_val);
  pr_clear_value (&db_val);
  if (error != NO_ERROR)
    {
      return error;
    }

  statement->etc = (void *) ret_val;

  if (into && into->node_type == PT_NAME && into->info.name.original)
    {
      return pt_associate_label_with_value (into->info.name.original,
					    db_value_copy (ret_val));
    }

  return NO_ERROR;
}				/* do_get_stats() */
