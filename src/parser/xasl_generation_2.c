/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * TODO: merge to xasl_generation_3.c
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include <assert.h>

#include "error_manager.h"
#include "db.h"
#include "environment_variable.h"
#include "parser.h"
#include "qp_mem.h"
#include "xasl_generation_2.h"
#include "schema_manager_3.h"
#include "view_transform_2.h"
#include "locator_cl.h"
#include "qo.h"
#include "msgexec.h"
#include "virtual_object_1.h"
#include "set_object_1.h"
#include "object_print_1.h"
#include "object_representation.h"
#include "heap_file.h"
#include "intl.h"
#include "system_parameter.h"
#include "execute_schema_8.h"

/* this must be the last header file included!!! */
#include "dbval.h"

typedef enum
{ SORT_LIST_AFTER_ISCAN = 1,
  SORT_LIST_ORDERBY,
  SORT_LIST_GROUPBY,
  SORT_LIST_AFTER_GROUPBY
} SORT_LIST_MODE;

static int pt_Hostvar_sno = 1;

static int hhhhmmss (const DB_TIME * time, char *buf, int buflen);
static int hhmiss (const DB_TIME * time, char *buf, int buflen);
static int yyyymmdd (const DB_DATE * date, char *buf, int buflen);
static int yymmdd (const DB_DATE * date, char *buf, int buflen);
static int yymmddhhmiss (const DB_UTIME * utime, char *buf, int buflen);
static int mmddyyyyhhmiss (const DB_UTIME * utime, char *buf, int buflen);
static char *host_var_name (unsigned int custom_print);
static PT_NODE *pt_is_query_node (PARSER_CONTEXT * parser, PT_NODE * tree,
				  void *arg, int *continue_walk);
static void pt_flush_object_info (PARSER_CONTEXT * parser,
				  PT_NODE * node_list);
static PT_NODE *pt_table_compatible_node (PARSER_CONTEXT * parser,
					  PT_NODE * tree, void *void_info,
					  int *continue_walk);
static int pt_table_compatible (PARSER_CONTEXT * parser, PT_NODE * node,
				PT_NODE * spec);
static TABLE_INFO *pt_table_info_alloc (void);
static PT_NODE *pt_filter_psuedo_specs (PARSER_CONTEXT * parser,
					PT_NODE * spec);
static PT_NODE *pt_to_aggregate_node (PARSER_CONTEXT * parser, PT_NODE * tree,
				      void *arg, int *continue_walk);
static SYMBOL_INFO *pt_push_fetch_spec_info (PARSER_CONTEXT * parser,
					     SYMBOL_INFO * symbols,
					     PT_NODE * fetch_spec);
static ACCESS_SPEC_TYPE *pt_make_access_spec (TARGET_TYPE spec_type,
					      ACCESS_METHOD access,
					      INDX_INFO * indexptr,
					      PRED_EXPR * where_key,
					      PRED_EXPR * where_pred);
static int pt_cnt_attrs (const REGU_VARIABLE_LIST attr_list);
static void pt_fill_in_attrid_array (REGU_VARIABLE_LIST attr_list,
				     ATTR_ID * attr_array, int *next_pos);
static SORT_LIST *pt_to_sort_list (PARSER_CONTEXT * parser,
				   PT_NODE * node_list, PT_NODE * root,
				   SORT_LIST_MODE sort_mode);

static PARSER_VARCHAR *pt_print_db_value_as_paren_list (PARSER_CONTEXT *
							parser,
							const struct db_value
							*val);
static int *pt_to_method_arglist (PARSER_CONTEXT * parser, PT_NODE * target,
				  PT_NODE * node_list,
				  PT_NODE * subquery_as_attr_list);

/*
 * look_for_unique_btid () - Search for a UNIQUE constraint B-tree ID
 *   return: 1 on a UNIQUE BTID is found
 *   classop(in): Class object pointer
 *   name(in): Attribute name
 *   btid(in): BTID pointer (BTID is returned)
 */
int
look_for_unique_btid (DB_OBJECT * classop, const char *name, BTID * btid)
{
  SM_CLASS *class_;
  SM_ATTRIBUTE *att;
  int error = NO_ERROR;
  int ok = 0;

  error = au_fetch_class (classop, &class_, AU_FETCH_READ, AU_SELECT);
  if (error == NO_ERROR)
    {
      att = classobj_find_attribute (class_, name, 0);
      if (att != NULL)
	{
	  if (classobj_get_cached_constraint (att->constraints,
					      SM_CONSTRAINT_UNIQUE,
					      btid) ||
	      classobj_get_cached_constraint (att->constraints,
					      SM_CONSTRAINT_PRIMARY_KEY,
					      btid))
	    {
	      ok = 1;
	    }
	}
    }

  return ok;
}				/* look_for_unique_btid */



/*
 * pt_xasl_type_enum_to_domain () - Given a PT_TYPE_ENUM generate a domain
 *                                  for it and cache it
 *   return:
 *   type(in):
 */
TP_DOMAIN *
pt_xasl_type_enum_to_domain (const PT_TYPE_ENUM type)
{
  TP_DOMAIN *dom;

  dom = pt_type_enum_to_db_domain (type);
  if (dom)
    return tp_domain_cache (dom);
  else
    return NULL;
}


/*
 * pt_xasl_node_to_domain () - Given a PT_NODE generate a domain
 *                             for it and cache it
 *   return:
 *   parser(in):
 *   node(in):
 */
TP_DOMAIN *
pt_xasl_node_to_domain (PARSER_CONTEXT * parser, const PT_NODE * node)
{
  TP_DOMAIN *dom;

  dom = pt_node_to_db_domain (parser, (PT_NODE *) node, NULL);
  if (dom)
    return tp_domain_cache (dom);
  else
    {
      PT_ERRORc (parser, node, er_msg ());
      return NULL;
    }
}				/* pt_xasl_node_to_domain */


/*
 * pt_xasl_data_type_to_domain () - Given a PT_DATA_TYPE node generate
 *                                  a domain for it and cache it
 *   return:
 *   parser(in):
 *   node(in):
 */
TP_DOMAIN *
pt_xasl_data_type_to_domain (PARSER_CONTEXT * parser, const PT_NODE * node)
{
  TP_DOMAIN *dom;

  dom = pt_data_type_to_db_domain (parser, (PT_NODE *) node, NULL);
  if (dom)
    return tp_domain_cache (dom);
  else
    return NULL;

}				/* pt_xasl_data_type_to_domain */


/*
 * hhhhmmss () - print a time value as 'hhhhmmss'
 *   return:
 *   time(in):
 *   buf(out):
 *   buflen(in):
 */
static int
hhhhmmss (const DB_TIME * time, char *buf, int buflen)
{
  const char date_fmt[] = "00%H%M%S";
  DB_DATE date;

  /* pick any valid date, even though we're interested only in time,
   * to pacify db_strftime */
  db_date_encode (&date, 12, 31, 1970);

  return db_strftime (buf, buflen, date_fmt, &date, (DB_TIME *) time);
}

/*
 * hhmiss () - print a time value as 'hh:mi:ss'
 *   return:
 *   time(in):
 *   buf(out):
 *   buflen(in):
 */
static int
hhmiss (const DB_TIME * time, char *buf, int buflen)
{
  const char date_fmt[] = "%H:%M:%S";
  DB_DATE date;

  /* pick any valid date, even though we're interested only in time,
   * to pacify db_strftime */
  db_date_encode (&date, 12, 31, 1970);

  return db_strftime (buf, buflen, date_fmt, &date, (DB_TIME *) time);
}


/*
 * yyyymmdd () - print a date as 'yyyymmdd'
 *   return:
 *   date(in):
 *   buf(out):
 *   buflen(in):
 */
static int
yyyymmdd (const DB_DATE * date, char *buf, int buflen)
{
  const char date_fmt[] = "%Y%m%d";
  DB_TIME time = 0;

  return db_strftime (buf, buflen, date_fmt, (DB_DATE *) date, &time);
}


/*
 * yymmdd () - print a date as 'yyyy-mm-dd'
 *   return:
 *   date(in):
 *   buf(out):
 *   buflen(in):
 */
static int
yymmdd (const DB_DATE * date, char *buf, int buflen)
{
  const char date_fmt[] = "%Y-%m-%d";
  DB_TIME time = 0;

  return db_strftime (buf, buflen, date_fmt, (DB_DATE *) date, &time);
}


/*
 * yymmddhhmiss () - print utime as 'yyyy-mm-dd:hh:mi:ss'
 *   return:
 *   utime(in):
 *   buf(out):
 *   buflen(in):
 */
static int
yymmddhhmiss (const DB_UTIME * utime, char *buf, int buflen)
{
  DB_DATE date;
  DB_TIME time;
  const char fmt[] = "%Y-%m-%d:%H:%M:%S";

  /* extract date & time from utime */
  db_utime_decode ((DB_UTIME *) utime, &date, &time);

  return db_strftime (buf, buflen, fmt, &date, &time);
}


/*
 * mmddyyyyhhmiss () - print utime as 'mm/dd/yyyy hh:mi:ss'
 *   return:
 *   utime(in):
 *   buf(in):
 *   buflen(in):
 */
static int
mmddyyyyhhmiss (const DB_UTIME * utime, char *buf, int buflen)
{
  DB_DATE date;
  DB_TIME time;
  const char fmt[] = "%m/%d/%Y %H:%M:%S";

  /* extract date & time from utime */
  db_utime_decode ((DB_UTIME *) utime, &date, &time);

  return db_strftime (buf, buflen, fmt, &date, &time);
}


/*
 * pt_print_db_value_as_paren_list () - Returns const sql string customized
 *                                      to the ldb connection
 *   return:
 *   parser(in):
 *   val(in):
 */
static PARSER_VARCHAR *
pt_print_db_value_as_paren_list (PARSER_CONTEXT * parser,
				 const struct db_value *val)
{
  PARSER_VARCHAR *temp = NULL, *result = NULL, *elem;
  int i, size = 0;
  DB_VALUE element;
  int error = NO_ERROR;

  switch (DB_VALUE_TYPE (val))
    {
    case DB_TYPE_SET:
    case DB_TYPE_SEQUENCE:
    case DB_TYPE_MULTISET:
      temp = pt_append_nulstring (parser, temp, "(");

      size = db_set_size (db_get_set ((DB_VALUE *) val));
      if (size > 0)
	{
	  error = db_set_get (db_get_set ((DB_VALUE *) val), 0, &element);
	  elem = describe_value (parser, NULL, &element);
	  temp = pt_append_varchar (parser, temp, elem);
	  for (i = 1; i < size; i++)
	    {
	      error = db_set_get (db_get_set ((DB_VALUE *) val), i, &element);
	      temp = pt_append_nulstring (parser, temp, ",");
	      elem = describe_value (parser, NULL, &element);
	      temp = pt_append_varchar (parser, temp, elem);
	    }
	}
      temp = pt_append_nulstring (parser, temp, ")");
      result = temp;
      break;
    default:
      break;
    }

  return result;
}


/*
 * pt_print_db_value () -
 *   return: const sql string customized to the ldb connection
 *   parser(in):
 *   val(in):
 */
PARSER_VARCHAR *
pt_print_db_value (PARSER_CONTEXT * parser, const struct db_value * val)
{
  PARSER_VARCHAR *temp = NULL, *result = NULL, *todate, *elem, *todatetime;
  int i, size = 0, rc;
  DB_VALUE element;
  int error = NO_ERROR;
  char dt[40], *p, *ptr;
  PT_NODE foo;
  unsigned int save_custom = parser->custom_print;

  memset (&foo, 0, sizeof (foo));

  /* set custom_print here so describe_data() will know to pad bit
   * strings to full bytes for the ldb. */
  parser->custom_print = parser->custom_print | PT_PAD_BYTE;
  switch (DB_VALUE_TYPE (val))
    {
    case DB_TYPE_SET:
    case DB_TYPE_MULTISET:
      temp = pt_append_nulstring (parser, NULL,
				  pt_show_type_enum ((PT_TYPE_ENUM)
						     pt_db_to_type_enum
						     (DB_VALUE_TYPE (val))));
      /* fall thru */
    case DB_TYPE_SEQUENCE:
      temp = pt_append_nulstring (parser, temp, "{");

      size = db_set_size (db_get_set ((DB_VALUE *) val));
      if (size > 0)
	{
	  error = db_set_get (db_get_set ((DB_VALUE *) val), 0, &element);
	  elem = describe_value (parser, NULL, &element);
	  temp = pt_append_varchar (parser, temp, elem);
	  for (i = 1; i < size; i++)
	    {
	      error = db_set_get (db_get_set ((DB_VALUE *) val), i, &element);
	      temp = pt_append_nulstring (parser, temp, ", ");
	      elem = describe_value (parser, NULL, &element);
	      temp = pt_append_varchar (parser, temp, elem);
	    }
	}
      temp = pt_append_nulstring (parser, temp, "}");
      result = temp;
      break;

    case DB_TYPE_OBJECT:
      /* no printable representation!, should not get here */
      result = pt_append_nulstring (parser, NULL, "NULL");
      break;

    case DB_TYPE_MONETARY:
      /* This is handled explicitly because describe_value will
         add a currency symbol, and it isn't needed here. */
      result = pt_append_varchar (parser, NULL,
				  describe_money
				  (parser, NULL,
				   DB_GET_MONETARY ((DB_VALUE *) val)));
      break;

    case DB_TYPE_BIT:
    case DB_TYPE_VARBIT:
      if (parser->custom_print & PT_SYBASE_PRINT)
	{
	  temp = pt_append_nulstring (parser, temp, "0x");
	  result = describe_data (parser, temp, val);
	}
      else if (parser->custom_print & PT_ORACLE_PRINT)
	{
	  temp = pt_append_nulstring (parser, temp, "'");
	  temp = describe_data (parser, temp, val);
	  result = pt_append_nulstring (parser, temp, "'");
	}
      else
	{
	  /* sqlx & everyone else get X'some_hex_string' */
	  result = describe_value (parser, NULL, val);
	}
      break;

    case DB_TYPE_DATE:
      if (parser->custom_print & PT_ORACLE_PRINT)
	{
	  todate = pt_append_nulstring (parser, NULL, "to_date('");
	  result = describe_data (parser, todate, val);
	  result = pt_append_nulstring (parser, result, "','MM/DD/YYYY')");
	}
      else if (parser->custom_print & PT_RDB_PRINT)
	{
	  /* print date value as DATE'yyyy-mm-dd' */
	  todate = pt_append_nulstring (parser, NULL, "DATE'");
	  dt[0] = '\0';
	  if (yymmdd (DB_GET_DATE (val), dt, 40) < 0)
	    {
	      /* a date/time conversion error has occurred in db_strftime */
	      PT_ERRORc (parser, &foo, er_msg ());
	    }
	  else
	    {
	      result = pt_append_nulstring (parser, todate, dt);
	      result = pt_append_nulstring (parser, result, "'");
	    }
	}
      else if (parser->custom_print & PT_INGRES_PRINT)
	{
	  temp = pt_append_nulstring (parser, temp, "'");
	  temp = describe_data (parser, temp, val);
	  result = pt_append_nulstring (parser, temp, "'");
	  /* Result already has the printed db_value. this stuff is just trying
	   * to detect bad ingres date values and report them as such. */
	  p = (char *) pt_get_varchar_bytes (temp);

	  while (*p && *p != '/')
	    p++;
	  if (*p)
	    p++;
	  if (*p)
	    while (*p && *p != '/')
	      p++;
	  if (*p)
	    p++;

	  /* p should point to year now */
	  if (*p && strncmp (p, "1582", 4) < 0)
	    {
	      PT_NODE foo;

	      memset (&foo, 0, sizeof (foo));
	      PT_ERRORmf (parser,
			  (&foo),
			  MSGCAT_SET_PARSER_RUNTIME,
			  MSGCAT_RUNTIME_INGRES_DATE_LIMIT,
			  pt_get_varchar_bytes (result));
	    }
	}
      else if (parser->custom_print & PT_SUPRA_PRINT)
	{
	  /* print date value as 'yyyymmdd' */
	  todate = pt_append_nulstring (parser, NULL, "'");
	  dt[0] = '\0';
	  if (yyyymmdd (DB_GET_DATE (val), dt, 40) < 0)
	    {
	      /* a date/time conversion error has occurred in db_strftime */
	      PT_ERRORc (parser, &foo, er_msg ());
	    }
	  else
	    {
	      result = pt_append_nulstring (parser, todate, dt);
	      result = pt_append_nulstring (parser, result, "'");
	    }
	}
      else if (parser->custom_print & PT_SYBASE_PRINT)
	{
	  temp = pt_append_nulstring (parser, temp, "'");
	  temp = describe_data (parser, temp, val);
	  result = pt_append_nulstring (parser, temp, "'");
	}
      else
	{
	  /* sqlx & everyone else want DATE'mm/dd/yyyy' */
	  result = describe_value (parser, NULL, val);
	}
      break;

    case DB_TYPE_TIME:
      rc = 0;
      if (parser->custom_print & PT_SUPRA_PRINT)
	{
	  todate = pt_append_nulstring (parser, NULL, "'");
	  rc = hhhhmmss (DB_GET_TIME (val), dt, 40);
	  result = pt_append_nulstring (parser, todate, dt);
	  result = pt_append_nulstring (parser, result, "'");
	}
      else if (parser->custom_print & PT_INFORMIX_PRINT)
	{
	  todate = pt_append_nulstring (parser, NULL, "DATETIME(");
	  rc = hhmiss (DB_GET_TIME (val), dt, 40);
	  result = pt_append_nulstring (parser, todate, dt);
	  result = pt_append_nulstring (parser, result, ") hour to second");
	}
      else if (parser->custom_print & PT_SYBASE_PRINT)
	{
	  temp = pt_append_nulstring (parser, temp, "'");
	  temp = describe_data (parser, temp, val);
	  result = pt_append_nulstring (parser, temp, "'");
	}
      else if (parser->custom_print & PT_RDB_PRINT)
	{
	  dt[0] = '\0';
	  /* translate 'hh:mi:ss' into TIME'hh:mi:ss' */
	  todate = pt_append_nulstring (parser, NULL, "TIME'");
	  rc = hhmiss (DB_GET_TIME (val), dt, 40);
	  result = pt_append_nulstring (parser, todate, dt);
	  result = pt_append_nulstring (parser, result, "'");
	}
      else if (parser->custom_print & PT_ORACLE_PRINT)
	{
	  todate = pt_append_nulstring (parser, NULL, "to_date('");
	  result = describe_data (parser, todate, val);
	  result = pt_append_nulstring (parser, result, "','HH:MI:SS AM')");
	}
      else
	{
	  /* sqlx & everyone else get time 'hh:mi:ss' */
	  result = describe_value (parser, NULL, val);
	}
      if (rc < 0)
	{
	  /* a date/time conversion error has occurred in db_strftime */
	  PT_ERRORc (parser, &foo, er_msg ());
	}
      break;

    case DB_TYPE_UTIME:
      rc = 0;
      if (parser->custom_print & PT_INFORMIX_PRINT)
	{
	  todate = pt_append_nulstring (parser, NULL, "DATETIME(");
	  rc = yymmddhhmiss (DB_GET_UTIME (val), dt, 40);

	  if (strncmp (dt, "1970", 4) < 0 || strncmp (dt, "2038", 4) > 0)
	    {
	      PT_NODE foo;

	      memset (&foo, 0, sizeof (foo));
	      PT_ERRORmf (parser,
			  (&foo),
			  MSGCAT_SET_PARSER_RUNTIME,
			  MSGCAT_RUNTIME_BAD_UTIME,
			  pt_get_varchar_bytes (result));
	    }

	  /* Get rid of the : between date and time. Informix chokes on it */
	  if ((ptr = strchr (dt, ':')) != NULL)
	    {
	      *ptr = ' ';
	    }

	  result = pt_append_nulstring (parser, todate, dt);
	  result = pt_append_nulstring (parser, result, ") year to second");
	}
      else if (parser->custom_print & PT_SYBASE_PRINT)
	{
	  temp = pt_append_nulstring (parser, temp, "'");
	  temp = describe_data (parser, temp, val);
	  result = pt_append_nulstring (parser, temp, "'");
	}
      else if (parser->custom_print & PT_RDB_PRINT)
	{
	  /* print utime as TIMESTAMP'yy-mm-dd:hh:mi:ss' */
	  dt[0] = '\0';
	  todate = pt_append_nulstring (parser, NULL, "TIMESTAMP'");
	  rc = yymmddhhmiss (DB_GET_UTIME (val), dt, 40);
	  result = pt_append_nulstring (parser, todate, dt);
	  result = pt_append_nulstring (parser, result, "'");
	}
      else if (parser->custom_print & PT_INGRES_PRINT)
	{
	  rc = mmddyyyyhhmiss (DB_GET_UTIME (val), dt, 40);

	  todatetime = pt_append_nulstring (parser, NULL, "'");
	  result = pt_append_nulstring (parser, todatetime, dt);
	  result = pt_append_nulstring (parser, result, "'");
	}
      else if (parser->custom_print & PT_ORACLE_PRINT)
	{
	  todatetime = pt_append_nulstring (parser, NULL, "to_date('");
	  result = describe_data (parser, todatetime, val);
	  result = pt_append_nulstring
	    (parser, result, "','HH:MI:SS AM MM/DD/YYYY')");
	}
      else
	{
	  /* everyone else gets sqlx's utime format */
	  result = describe_value (parser, NULL, val);
	}
      if (rc < 0)
	{
	  /* a date/time conversion error has occurred in db_strftime */
	  PT_ERRORc (parser, &foo, er_msg ());
	}
      break;

    default:
      result = describe_value (parser, NULL, val);
      break;
    }
  /* restore custom print */
  parser->custom_print = save_custom;
  return result;
}

/*
 * host_var_name () -  manufacture a host variable name
 *   return:  a host variable name
 *   custom_print(in): a custom_print member
 */
static char *
host_var_name (unsigned int custom_print)
{
  static char nam[14];

  if (custom_print & PT_ORACLE_PRINT)
    {
      sprintf (nam, ":h%d", pt_Hostvar_sno++);
      return nam;
    }
  else if (custom_print & PT_SYBASE_PRINT)
    {
      sprintf (nam, "@h%d", pt_Hostvar_sno++);
      return nam;
    }
  else
    return (char *) "?";
}


/*
 * pt_print_node_value () -
 *   return: const sql string customized to the ldb connection
 *   parser(in):
 *   val(in):
 */
PARSER_VARCHAR *
pt_print_node_value (PARSER_CONTEXT * parser, const PT_NODE * val)
{
  PARSER_VARCHAR *q = NULL;
  DB_VALUE *db_val, new_db_val;
  DB_TYPE db_typ;
  int error = NO_ERROR;
  SETOBJ *setobj;
  PT_NODE *temp;

  if (!(val->node_type == PT_VALUE ||
	val->node_type == PT_HOST_VAR ||
	(val->node_type == PT_NAME &&
	 val->info.name.meta_class == PT_PARAMETER)))
    {
      return NULL;
    }

  db_val = pt_value_to_db (parser, (PT_NODE *) val);
  if (!db_val)
    {
      return NULL;
    }
  db_typ = PRIM_TYPE (db_val);

  /* handle "in" clause sets as a special case.
   * Print them as parenthesized lists for ANSI compatibility.
   * Must do this before host var check in case we have an "in ?" construct. */
  if (val->spec_ident && PT_IS_COLLECTION_TYPE (val->type_enum) &&
      (parser->custom_print & PT_SUPPRESS_SETS))
    {
      if (!parser->dont_prt)
	{
	  q = pt_print_db_value_as_paren_list (parser, db_val);
	}
      return q;
    }

  if (val->type_enum == PT_TYPE_OBJECT)
    {
      /* convert db_val to its underlying ldb value */
      switch (db_typ)
	{
	case DB_TYPE_OBJECT:
	  vid_get_keys (db_get_object (db_val), &new_db_val);
	  db_val = &new_db_val;
	  break;
	case DB_TYPE_VOBJ:
	  /* don't want a clone of the db_value, so use lower level functions */
	  error = set_get_setobj (db_get_set (db_val), &setobj, 0);
	  if (error >= 0)
	    {
	      error = setobj_get_element_ptr (setobj, 2, &db_val);
	    }
	  break;
	default:
	  break;
	}

      if (error < 0)
	{
	  PT_ERRORc (parser, val, er_msg ());
	}
      db_typ = PRIM_TYPE (db_val);
    }

  if ((parser->custom_print & PT_DYNAMIC_SQL) && db_val)
    {
      if ((val->node_type == PT_HOST_VAR &&
	   val->info.host_var.var_type == PT_HOST_IN) ||
	  (val->node_type == PT_NAME &&
	   val->info.name.meta_class == PT_PARAMETER) ||
	  (db_typ == DB_TYPE_DOUBLE &&
	   (parser->custom_print & PT_INGRES_PRINT)) ||
	  (db_typ == DB_TYPE_FLOAT &&
	   (parser->custom_print & PT_INGRES_PRINT)) ||
	  db_typ == DB_TYPE_OID ||
	  db_typ == DB_TYPE_OBJECT || db_typ == DB_TYPE_VOBJ)
	{
	  /* add host var value to array */
	  temp = parser_new_node (parser, PT_VALUE);
	  temp->info.value.db_value = *db_val;
	  /* we don't own the memory. This does require that db_val's
	   * underlying value be valid until we use the value.
	   * Since this routine is only used to make parsable SQL
	   * for immediate conversion to XASL, or immediate
	   * sending to an ldb, this precondition is met for
	   * avoiding a copy */
	  temp->info.value.db_value.need_clear = false;
	  temp->info.value.db_value_is_in_workspace = 0;
	  parser->input_values =
	    parser_append_node (temp, parser->input_values);

	  if (!parser->dont_prt)
	    {
	      q = pt_append_nulstring (parser, q,
				       host_var_name (parser->custom_print));
	    }
	  return q;
	}
    }
  if (!parser->dont_prt)
    {
      q = pt_print_db_value (parser, db_val);
    }
  return q;
}

/*
 * pt_is_query_node () -
 *   return: true if node is a query node
 *   p(in):
 *   tree(in):
 *   arg(in):
 *   continue_walk(in):
 */
static PT_NODE *
pt_is_query_node (PARSER_CONTEXT * parser, PT_NODE * tree,
		  void *arg, int *continue_walk)
{
  if (tree->node_type == PT_SELECT
      || tree->node_type == PT_UNION
      || tree->node_type == PT_DIFFERENCE
      || tree->node_type == PT_INTERSECTION)
    {
      *continue_walk = PT_STOP_WALK;
      *(int *) arg = true;
    }

  return tree;
}

/*
 * pt_flush_object_info () -
 *   return: true if select list has a select statement
 *   parser(in):
 *   node_list(in):
 */
static void
pt_flush_object_info (PARSER_CONTEXT * parser, PT_NODE * node_list)
{
  while (node_list)
    {
      /* and look for sets of objects */
      pt_flush_object_info (parser, node_list->data_type);

      node_list = node_list->next;
    }
}


/*
 * pt_table_compatible_node () - Returns compatible if node is non-subquery
 *                               and has matching spec id
 *   return:
 *   parser(in):
 *   tree(in):
 *   void_info(in/out):
 *   continue_walk(in/out):
 */
static PT_NODE *
pt_table_compatible_node (PARSER_CONTEXT * parser, PT_NODE * tree,
			  void *void_info, int *continue_walk)
{
  COMPATIBLE_INFO *info = (COMPATIBLE_INFO *) void_info;

  if (info && tree)
    {
      switch (tree->node_type)
	{
	case PT_SELECT:
	case PT_UNION:
	case PT_DIFFERENCE:
	case PT_INTERSECTION:
	  info->compatible = NOT_COMPATIBLE;
	  *continue_walk = PT_STOP_WALK;
	  break;

	case PT_NAME:
	  /* check ids match */
	  if (tree->info.name.spec_id != info->spec_id)
	    {
	      info->compatible = NOT_COMPATIBLE;
	      *continue_walk = PT_STOP_WALK;
	    }
	  break;

	case PT_EXPR:
	  if (tree->info.expr.op == PT_INST_NUM ||
	      tree->info.expr.op == PT_ROWNUM)
	    {
	      info->compatible = NOT_COMPATIBLE;
	      *continue_walk = PT_STOP_WALK;
	    }
	  break;

	default:
	  break;
	}
    }

  return tree;
}


/*
 * pt_table_compatible () - Tests the compatibility of the given sql tree
 *                          with a given class specification
 *   return:
 *   parser(in):
 *   node(in):
 *   spec(in):
 */
static int
pt_table_compatible (PARSER_CONTEXT * parser, PT_NODE * node, PT_NODE * spec)
{
  COMPATIBLE_INFO info;
  info.compatible = ENTITY_COMPATIBLE;

  info.spec_id = spec->info.spec.id;

  parser_walk_tree (parser, node, pt_table_compatible_node, &info,
		    pt_continue_walk, NULL);

  return info.compatible;
}

PT_NODE *
pt_query_set_reference (PARSER_CONTEXT * parser, PT_NODE * node)
{
  PT_NODE *query, *spec, *temp;

  query = node;
  while (query &&
	 (query->node_type == PT_UNION ||
	  query->node_type == PT_INTERSECTION ||
	  query->node_type == PT_DIFFERENCE))
    {
      query = query->info.query.q.union_.arg1;
    }

  spec = query->info.query.q.select.from;
  if (query && spec)
    {
      /* recalculate referenced attributes */
      for (temp = spec; temp; temp = temp->next)
	{
	  node = mq_set_references (parser, node, temp);
	}
    }

  return node;
}


/*
 * pt_split_where_part () - Make a two lists of predicates,
 *                          one ldb compatible, one not
 *   return:
 *   parser(in):
 *   spec(in):
 *   where(in/out):
 *   ldb_part(out):
 *   gdb_part(out):
 *
 * Note :
 * the compatible predicate will be shipped down in a read proc,
 * the remaining predicate will be used in the sql/m query processor
 * to further restrict the read proc's rows when read in
 */
void
pt_split_where_part (PARSER_CONTEXT * parser, PT_NODE * spec, PT_NODE * where,
		     PT_NODE ** ldb_part, PT_NODE ** gdb_part)
{
  PT_NODE *next;

  *ldb_part = NULL;
  *gdb_part = NULL;

  while (where)
    {
      next = where->next;
      where->next = NULL;

      where->next = *gdb_part;
      *gdb_part = where;

      where = next;
    }
}


/*
 * pt_split_access_if_instnum () - Make a two lists of predicates,
 *       one "simply" compatible with the given table,
 *       one containing any other constructs, one instnum predicates
 *   return:
 *   parser(in):
 *   spec(in):
 *   where(in/out):
 *   access_part(out):
 *   if_part(out):
 *   instnum_part(out):
 */
void
pt_split_access_if_instnum (PARSER_CONTEXT * parser, PT_NODE * spec,
			    PT_NODE * where, PT_NODE ** access_part,
			    PT_NODE ** if_part, PT_NODE ** instnum_part)
{
  PT_NODE *next;
  bool inst_num;

  *access_part = NULL;
  *if_part = NULL;
  *instnum_part = NULL;

  while (where)
    {
      next = where->next;
      where->next = NULL;
      if (pt_table_compatible (parser, where, spec) == ENTITY_COMPATIBLE)
	{
	  where->next = *access_part;
	  *access_part = where;
	}
      else
	{
	  /* check for instnum_predicate */
	  inst_num = false;
	  (void) parser_walk_tree (parser, where, pt_check_instnum_pre, NULL,
				   pt_check_instnum_post, &inst_num);
	  if (inst_num)
	    {
	      where->next = *instnum_part;
	      *instnum_part = where;
	    }
	  else
	    {
	      where->next = *if_part;
	      *if_part = where;
	    }
	}
      where = next;
    }
}

/*
 * pt_split_if_instnum
 *
 * Description:
 *
 *  Make a two lists of predicates, one containing any other constructs
 *  (subqueries, other tables, etc. except for instnum predicates),
 *  one instnum predicates.
 */

/*
 * pt_split_if_instnum () - Make a two lists of predicates, one containing
 *                          any other constructs, one instnum predicates
 *   return:
 *   parser(in):
 *   where(in/out):
 *   if_part(out):
 *   instnum_part(out):
 */
void
pt_split_if_instnum (PARSER_CONTEXT * parser, PT_NODE * where,
		     PT_NODE ** if_part, PT_NODE ** instnum_part)
{
  PT_NODE *next;
  bool inst_num;

  *if_part = NULL;
  *instnum_part = NULL;

  while (where)
    {
      next = where->next;
      where->next = NULL;

      /* check for instnum_predicate */
      inst_num = false;
      (void) parser_walk_tree (parser, where, pt_check_instnum_pre, NULL,
			       pt_check_instnum_post, &inst_num);
      if (inst_num)
	{
	  where->next = *instnum_part;
	  *instnum_part = where;
	}
      else
	{
	  where->next = *if_part;
	  *if_part = where;
	}
      where = next;
    }
}

/*
 * pt_split_having_grbynum
 *
 * Description:
 *
 *      Make a two lists of predicates, one "simply" having predicates,
 *      and one containing groupby_num() function.
 */

/*
 * pt_split_having_grbynum () - Make a two lists of predicates, one "simply"
 *      having predicates, and one containing groupby_num() function
 *   return:
 *   parser(in):
 *   having(in/out):
 *   having_part(out):
 *   grbynum_part(out):
 */
void
pt_split_having_grbynum (PARSER_CONTEXT * parser, PT_NODE * having,
			 PT_NODE ** having_part, PT_NODE ** grbynum_part)
{
  PT_NODE *next;
  bool grbynum_flag;

  *having_part = NULL;
  *grbynum_part = NULL;

  while (having)
    {
      next = having->next;
      having->next = NULL;

      grbynum_flag = false;
      (void) parser_walk_tree (parser, having, pt_check_groupbynum_pre, NULL,
			       pt_check_groupbynum_post, &grbynum_flag);

      if (grbynum_flag)
	{
	  having->next = *grbynum_part;
	  *grbynum_part = having;
	}
      else
	{
	  having->next = *having_part;
	  *having_part = having;
	}

      having = next;
    }
}


/*
 * pt_make_identity_offsets () - Create an attr_offset array that
 *                               has 0 for position 0, 1 for position 1, etc
 *   return:
 *   attr_list(in):
 */
int *
pt_make_identity_offsets (PT_NODE * attr_list)
{
  int *offsets;
  int num_attrs, i;

  if ((num_attrs = pt_length_of_list (attr_list)) == 0)
    {
      return NULL;
    }

  if ((offsets = (int *) malloc ((num_attrs + 1) * sizeof (int))) == NULL)
    {
      return NULL;
    }

  for (i = 0; i < num_attrs; i++)
    {
      offsets[i] = i;
    }
  offsets[i] = -1;

  return offsets;
}


/*
 * pt_split_attrs () - Split the attr_list into two lists without destroying
 *      the original list
 *   return:
 *   parser(in):
 *   table_info(in):
 *   pred(in):
 *   pred_attrs(out):
 *   rest_attrs(out):
 *   pred_offsets(out):
 *   rest_offsets(out):
 *
 * Note :
 * Those attrs that are found in the pred are put on the pred_attrs list,
 * those attrs not found in the pred are put on the rest_attrs list
 */
int
pt_split_attrs (PARSER_CONTEXT * parser, TABLE_INFO * table_info,
		PT_NODE * pred, PT_NODE ** pred_attrs, PT_NODE ** rest_attrs,
		int **pred_offsets, int **rest_offsets)
{
  PT_NODE *tmp, *pointer, *real_attrs;
  PT_NODE *pred_nodes;
  int cur_pred, cur_rest, num_attrs, i;
  PT_NODE *attr_list = table_info->attribute_list;
  PT_NODE *node, *save_node, *save_next, *ref_node;

  pred_nodes = NULL;		/* init */
  *pred_attrs = NULL;
  *rest_attrs = NULL;
  *pred_offsets = NULL;
  *rest_offsets = NULL;
  cur_pred = 0;
  cur_rest = 0;

  if (!attr_list)
    return 1;			/* nothing to do */

  num_attrs = pt_length_of_list (attr_list);
  if ((*pred_offsets = (int *) malloc (num_attrs * sizeof (int))) == NULL)
    {
      goto exit_on_error;
    }

  if ((*rest_offsets = (int *) malloc (num_attrs * sizeof (int))) == NULL)
    {
      goto exit_on_error;
    }

  if (!pred)
    {
      *rest_attrs = pt_point_l (parser, attr_list);
      for (i = 0; i < num_attrs; i++)
	{
	  (*rest_offsets)[i] = i;
	}
      return 1;
    }

  /* mq_get_references() is destructive to the real set of referenced
   * attrs, so we need to squirrel it away. */
  real_attrs = table_info->class_spec->info.spec.referenced_attrs;
  table_info->class_spec->info.spec.referenced_attrs = NULL;

  /* Traverse pred */
  for (node = pred; node; node = node->next)
    {
      save_node = node;		/* save */

      CAST_POINTER_TO_NODE (node);

      /* save and cut-off node link */
      save_next = node->next;
      node->next = NULL;

      ref_node = mq_get_references (parser, node, table_info->class_spec);
      pred_nodes = parser_append_node (ref_node, pred_nodes);

      /* restore node link */
      if (node)
	{
	  node->next = save_next;
	}

      node = save_node;		/* restore */
    }				/* for (node = ...) */

  table_info->class_spec->info.spec.referenced_attrs = real_attrs;

  tmp = attr_list;
  i = 0;
  while (tmp)
    {
      if ((pointer = pt_point (parser, tmp)) == NULL)
	{
	  goto exit_on_error;
	}

      if (pt_find_attribute (parser, tmp, pred_nodes) != -1)
	{
	  *pred_attrs = parser_append_node (pointer, *pred_attrs);
	  (*pred_offsets)[cur_pred++] = i;
	}
      else
	{
	  *rest_attrs = parser_append_node (pointer, *rest_attrs);
	  (*rest_offsets)[cur_rest++] = i;
	}
      tmp = tmp->next;
      i++;
    }

  if (pred_nodes)
    {
      parser_free_tree (parser, pred_nodes);
    }

  return 1;

exit_on_error:

  parser_free_tree (parser, *pred_attrs);
  parser_free_tree (parser, *rest_attrs);
  free_and_init (*pred_offsets);
  free_and_init (*rest_offsets);
  if (pred_nodes)
    {
      parser_free_tree (parser, pred_nodes);
    }

  return 0;
}


/*
 * pt_to_index_attrs () - Those attrs that are found in the key-range pred
 *                        and key-filter pred are put on the pred_attrs list
 *   return:
 *   parser(in):
 *   table_info(in):
 *   index_pred(in):
 *   key_filter_pred(in):
 *   pred_attrs(out):
 *   pred_offsets(out):
 */
int
pt_to_index_attrs (PARSER_CONTEXT * parser, TABLE_INFO * table_info,
		   QO_XASL_INDEX_INFO * index_pred,
		   PT_NODE * key_filter_pred, PT_NODE ** pred_attrs,
		   int **pred_offsets)
{
  PT_NODE *tmp, *pointer, *real_attrs;
  PT_NODE *pred_nodes;
  int cur_pred, num_attrs, i;
  PT_NODE *attr_list = table_info->attribute_list;
  PT_NODE **term_exprs;
  int nterms;
  PT_NODE *node, *save_node, *save_next, *ref_node;

  pred_nodes = NULL;		/* init */
  *pred_attrs = NULL;
  *pred_offsets = NULL;
  cur_pred = 0;

  if (!attr_list)
    return 1;			/* nothing to do */

  num_attrs = pt_length_of_list (attr_list);
  *pred_offsets = (int *) malloc (num_attrs * sizeof (int));
  if (*pred_offsets == NULL)
    {
      goto exit_on_error;
    }

  /* mq_get_references() is destructive to the real set of referenced
     attrs, so we need to squirrel it away. */
  real_attrs = table_info->class_spec->info.spec.referenced_attrs;
  table_info->class_spec->info.spec.referenced_attrs = NULL;

  if (PRM_ORACLE_STYLE_EMPTY_STRING)
    {
      term_exprs = qo_xasl_get_terms (index_pred);
      nterms = qo_xasl_get_num_terms (index_pred);

      /* Traverse key-range pred */
      for (i = 0; i < nterms; i++)
	{
	  save_node = node = term_exprs[i];

	  CAST_POINTER_TO_NODE (node);

	  /* save and cut-off node link */
	  save_next = node->next;
	  node->next = NULL;

	  /* exclude path entities */
	  ref_node = mq_get_references_helper (parser, node,
					       table_info->class_spec, false);

	  /* need to check zero-length empty string */
	  if (ref_node->type_enum == PT_TYPE_VARCHAR
	      || ref_node->type_enum == PT_TYPE_VARNCHAR
	      || ref_node->type_enum == PT_TYPE_VARBIT)
	    {
	      pred_nodes = parser_append_node (ref_node, pred_nodes);
	    }

	  /* restore node link */
	  if (node)
	    {
	      node->next = save_next;
	    }

	  term_exprs[i] = save_node;	/* restore */
	}			/* for (i = 0; ...) */
    }

  /* Traverse key-filter pred */
  for (node = key_filter_pred; node; node = node->next)
    {
      save_node = node;		/* save */

      CAST_POINTER_TO_NODE (node);

      /* save and cut-off node link */
      save_next = node->next;
      node->next = NULL;

      /* exclude path entities */
      ref_node = mq_get_references_helper (parser, node,
					   table_info->class_spec, false);
      pred_nodes = parser_append_node (ref_node, pred_nodes);

      /* restore node link */
      if (node)
	{
	  node->next = save_next;
	}

      node = save_node;		/* restore */
    }				/* for (node = ...) */

  table_info->class_spec->info.spec.referenced_attrs = real_attrs;

  if (!pred_nodes)		/* there is not key-filter pred */
    {
      return 1;
    }

  tmp = attr_list;
  i = 0;
  while (tmp)
    {
      if (pt_find_attribute (parser, tmp, pred_nodes) != -1)
	{
	  if ((pointer = pt_point (parser, tmp)) == NULL)
	    {
	      goto exit_on_error;
	    }
	  *pred_attrs = parser_append_node (pointer, *pred_attrs);
	  (*pred_offsets)[cur_pred++] = i;
	}
      tmp = tmp->next;
      i++;
    }

  if (pred_nodes)
    {
      parser_free_tree (parser, pred_nodes);
    }

  return 1;

exit_on_error:

  parser_free_tree (parser, *pred_attrs);
  free_and_init (*pred_offsets);
  if (pred_nodes)
    {
      parser_free_tree (parser, pred_nodes);
    }
  return 0;
}


/*
 * pt_flush_classes () - Flushes each class encountered
 *   return:
 *   parser(in):
 *   node(in):
 *   void_arg(in):
 *   continue_walk(in/out):
 */
PT_NODE *
pt_flush_classes (PARSER_CONTEXT * parser, PT_NODE * node,
		  void *arg, int *continue_walk)
{
  PT_NODE *class_;
  int isvirt;

  if (node->node_type == PT_SPEC)
    {
      for (class_ = node->info.spec.flat_entity_list;
	   class_; class_ = class_->next)
	{
	  /* some caller (probably from vid2.c) is trying to fetch a
	     (possibly xlocked) relational proxy instance and using
	     pt_to_xasl to translate the proxy's "keys" from globaldb
	     input host variables into localdb input host variables.
	     If parser->dont_flush is asserted, skip the flushing of
	     this relational proxy. Otherwise, we get into infinite
	     recursion. */

	  if (!parser->dont_flush &&
	      /* if class object is not dirty and doesn't contain any
	       * dirty instances, do not flush the class and it's instances */
	      (WS_ISDIRTY (class_->info.name.db_object) ||
	       ws_has_dirty_objects (class_->info.name.db_object, &isvirt)))
	    {
	      if (sm_flush_objects (class_->info.name.db_object) != NO_ERROR)
		{
		  PT_ERRORc (parser, class_, er_msg ());
		}
	    }

	}
    }

  return node;
}


/*
 * pt_pruning_and_flush_class_and_null_xasl () - Flushes each class encountered
 * 	Partition pruning is applied to PT_SELECT nodes
 *   return:
 *   parser(in):
 *   tree(in):
 *   void_arg(in):
 *   continue_walk(in):
 */
PT_NODE *
pt_pruning_and_flush_class_and_null_xasl (PARSER_CONTEXT * parser,
					  PT_NODE * tree,
					  void *void_arg, int *continue_walk)
{
  tree = pt_flush_classes (parser, tree, void_arg, continue_walk);

  if (PT_IS_QUERY_NODE_TYPE (tree->node_type))
    {
      if (!tree->partition_pruned && tree->node_type == PT_SELECT)
	{
	  do_apply_partition_pruning (parser, tree);
	}
      tree->info.query.xasl = NULL;
    }
  else if (tree->node_type == PT_DATA_TYPE)
    {
      PT_NODE *entity;

      /* guard against proxies & views not correctly tagged
         in data type nodes */
      entity = tree->info.data_type.entity;
      if (entity)
	{
	  if (entity->info.name.meta_class != PT_META_CLASS
	      && db_is_vclass (entity->info.name.db_object)
	      && !tree->info.data_type.virt_object)
	    {
	      tree->info.data_type.virt_object = entity->info.name.db_object;
	    }
	}
    }

  return tree;
}



/*
 * pt_is_subquery () -
 *   return: true if symbols comes from a subquery of a UNION-type thing
 *   node(in):
 */
int
pt_is_subquery (PT_NODE * node)
{
  PT_MISC_TYPE subquery_type = node->info.query.is_subquery;

  return (subquery_type != 0);
}


/*
 * pt_table_info_alloc () - Allocates and inits an TABLE_INFO structure
 * 	                    from temporary memory
 *   return:
 *   pt_table_info_alloc(in):
 */
static TABLE_INFO *
pt_table_info_alloc (void)
{
  TABLE_INFO *table_info;

  table_info = (TABLE_INFO *) pt_alloc_packing_buf (sizeof (TABLE_INFO));

  if (table_info)
    {
      table_info->next = NULL;
      table_info->class_spec = NULL;
      table_info->exposed = NULL;
      table_info->spec_id = 0;
      table_info->attribute_list = NULL;
      table_info->value_list = NULL;
      table_info->is_fetch = 0;
    }

  return table_info;
}


/*
 * pt_symbol_info_alloc () - Allocates and inits an SYMBOL_INFO structure
 *                           from temporary memory
 *   return:
 */
SYMBOL_INFO *
pt_symbol_info_alloc (void)
{
  SYMBOL_INFO *symbols;

  symbols = (SYMBOL_INFO *) pt_alloc_packing_buf (sizeof (SYMBOL_INFO));

  if (symbols)
    {
      symbols->stack = NULL;
      symbols->table_info = NULL;
      symbols->current_class = NULL;
      symbols->cache_attrinfo = NULL;
      symbols->current_listfile = NULL;
      symbols->listfile_unbox = UNBOX_AS_VALUE;
      symbols->listfile_value_list = NULL;

      /* only used for server inserts and updates */
      symbols->listfile_attr_offset = 0;
    }

  return symbols;
}


/*
 * pt_is_single_tuple () -
 *   return: true if select can be determined to return exactly one tuple
 *           This means an aggregate function was used with no group_by clause
 *   parser(in):
 *   select_node(in):
 */
int
pt_is_single_tuple (PARSER_CONTEXT * parser, PT_NODE * select_node)
{
  if (select_node->info.query.q.select.group_by != NULL)
    return false;

  return pt_has_aggregate (parser, select_node);
}


/*
 * pt_filter_psuedo_specs () - Returns list of specs to participate
 *                             in a join cross product
 *   return:
 *   parser(in):
 *   spec(in/out):
 */
static PT_NODE *
pt_filter_psuedo_specs (PARSER_CONTEXT * parser, PT_NODE * spec)
{
  PT_NODE **last, *temp1, *temp2;

  if (spec)
    {
      last = &spec;
      temp2 = *last;
      while (temp2)
	{
	  if ((temp1 = temp2->info.spec.derived_table)
	      && temp1->node_type == PT_VALUE
	      && temp1->type_enum == PT_TYPE_NULL)
	    {
	      /* fix ths derived table up to be generatable */
	      temp1->type_enum = PT_TYPE_SET;
	      temp1->info.value.db_value_is_initialized = 0;
	      temp1->info.value.data_value.set = NULL;
	      temp2->info.spec.derived_table_type = PT_IS_SET_EXPR;
	    }

	  if (temp2->info.spec.derived_table_type == PT_IS_WHACKED_SPEC)
	    {
	      /* remove it */
	      *last = temp2->next;
	    }
	  else
	    {
	      /* keep it */
	      last = &temp2->next;
	    }
	  temp2 = *last;
	}
    }

  if (!spec)
    {
      /* handle single row class.
         make a derived table set with one element so we get one row. */
      spec = parser_new_node (parser, PT_SPEC);
      temp1 = parser_new_node (parser, PT_VALUE);
      spec->info.spec.derived_table = temp1;
      temp1->type_enum = PT_TYPE_SET;
      temp2 = parser_new_node (parser, PT_VALUE);
      temp1->info.value.data_value.set = temp2;
      temp2->type_enum = PT_TYPE_INTEGER;
      spec->info.spec.range_var = pt_name (parser, "one_row");

      /* we need to type this so that domain packing will work */
      spec->info.spec.range_var->type_enum = PT_TYPE_INTEGER;
      spec->info.spec.as_attr_list = pt_name (parser, "one_col");

      /* we need to type this so that domain packing will work */
      spec->info.spec.as_attr_list->type_enum = PT_TYPE_INTEGER;
      spec->info.spec.derived_table_type = PT_IS_SET_EXPR;
    }
  return spec;
}

/*
 * pt_to_method_arglist () - converts a parse expression tree list of
 *                           method call arguments to method argument array
 *   return: A NULL on error occured
 *   parser(in):
 *   target(in):
 *   node_list(in): should be parse name nodes
 *   subquery_as_attr_list(in):
 */
static int *
pt_to_method_arglist (PARSER_CONTEXT * parser,
		      PT_NODE * target,
		      PT_NODE * node_list, PT_NODE * subquery_as_attr_list)
{
  int *arg_list = NULL;
  int i = 1;
  int num_args = pt_length_of_list (node_list) + 1;
  PT_NODE *node;

  arg_list = regu_int_array_alloc (num_args);
  if (!arg_list)
    {
      return NULL;
    }

  if (target != NULL)
    {
      /* the method call target is the first element in the array */
      arg_list[0] = pt_find_attribute (parser, target, subquery_as_attr_list);
      if (arg_list[0] == -1)
	{
	  return NULL;
	}
    }
  else
    {
      i = 0;
    }

  for (node = node_list; node != NULL; node = node->next)
    {
      arg_list[i] = pt_find_attribute (parser, node, subquery_as_attr_list);
      if (arg_list[i] == -1)
	{
	  return NULL;
	}
      i++;
    }

  return arg_list;
}


/*
 * pt_to_method_sig_list () - converts a parse expression tree list of
 *                            method calls to method signature list
 *   return: A NULL return indicates a (memory) error occured
 *   parser(in):
 *   node_list(in): should be parse method nodes
 *   subquery_as_attr_list(in):
 */
METHOD_SIG_LIST *
pt_to_method_sig_list (PARSER_CONTEXT * parser,
		       PT_NODE * node_list, PT_NODE * subquery_as_attr_list)
{
  METHOD_SIG_LIST *sig_list = NULL;
  METHOD_SIG **tail = NULL;
  PT_NODE *node;

  sig_list = regu_method_sig_list_alloc ();
  if (!sig_list)
    {
      return NULL;
    }

  tail = &(sig_list->method_sig);


  for (node = node_list; node != NULL; node = node->next)
    {
      (*tail) = regu_method_sig_alloc ();

      if (*tail && node->node_type == PT_METHOD_CALL &&
	  node->info.method_call.method_name)
	{
	  (sig_list->no_methods)++;

	  (*tail)->method_name = (char *)
	    node->info.method_call.method_name->info.name.original;

	  if (node->info.method_call.on_call_target == NULL)
	    {
	      (*tail)->class_name = NULL;
	    }
	  else
	    {
	      PT_NODE *dt = node->info.method_call.on_call_target->data_type;
	      /* beware of virtual classes */
	      if (dt->info.data_type.virt_object)
		{
		  (*tail)->class_name = (char *)
		    db_get_class_name (dt->info.data_type.virt_object);
		}
	      else
		{
		  (*tail)->class_name = (char *)
		    dt->info.data_type.entity->info.name.original;
		}
	    }

	  (*tail)->method_type =
	    (node->info.method_call.class_or_inst == PT_IS_CLASS_MTHD)
	    ? METHOD_IS_CLASS_METHOD : METHOD_IS_INSTANCE_METHOD;

	  /* no_method_args does not include the target by convention */
	  (*tail)->no_method_args =
	    pt_length_of_list (node->info.method_call.arg_list);
	  (*tail)->method_arg_pos =
	    pt_to_method_arglist (parser,
				  node->info.method_call.on_call_target,
				  node->info.method_call.arg_list,
				  subquery_as_attr_list);

	  tail = &(*tail)->next;
	}
      else
	{
	  /* something failed */
	  sig_list = NULL;
	  break;
	}
    }

  return sig_list;
}


/*
 * pt_make_val_list () - Makes a val list with a DB_VALUE place holder
 *                       for every attribute on an attribute list
 *   return:
 *   attribute_list(in):
 */
VAL_LIST *
pt_make_val_list (PT_NODE * attribute_list)
{
  VAL_LIST *value_list = NULL;
  QPROC_DB_VALUE_LIST dbval_list;
  QPROC_DB_VALUE_LIST *dbval_list_tail;
  PT_NODE *attribute;

  value_list = regu_vallist_alloc ();

  if (value_list)
    {
      value_list->val_cnt = 0;
      value_list->valp = NULL;
      dbval_list_tail = &value_list->valp;

      for (attribute = attribute_list; attribute != NULL;
	   attribute = attribute->next)
	{
	  dbval_list = regu_dbvallist_alloc ();
	  if (dbval_list
	      && regu_dbval_type_init (dbval_list->val,
				       pt_node_to_db_type (attribute)))
	    {
	      value_list->val_cnt++;
	      (*dbval_list_tail) = dbval_list;
	      dbval_list_tail = &dbval_list->next;
	      dbval_list->next = NULL;
	    }
	  else
	    {
	      value_list = NULL;
	      break;
	    }
	}
    }

  return value_list;
}


/*
 * pt_clone_val_list () - Makes a val list with a DB_VALUE place holder
 *                        for every attribute on an attribute list
 *   return:
 *   parser(in):
 *   attribute_list(in):
 */
VAL_LIST *
pt_clone_val_list (PARSER_CONTEXT * parser, PT_NODE * attribute_list)
{
  VAL_LIST *value_list = NULL;
  QPROC_DB_VALUE_LIST dbval_list;
  QPROC_DB_VALUE_LIST *dbval_list_tail;
  PT_NODE *attribute;
  REGU_VARIABLE *regu = NULL;

  value_list = regu_vallist_alloc ();

  if (value_list)
    {
      value_list->val_cnt = 0;
      value_list->valp = NULL;
      dbval_list_tail = &value_list->valp;

      for (attribute = attribute_list; attribute != NULL;
	   attribute = attribute->next)
	{
	  dbval_list = regu_dbvlist_alloc ();
	  regu = pt_attribute_to_regu (parser, attribute);
	  if (dbval_list && regu)
	    {
	      dbval_list->val = pt_regu_to_dbvalue (parser, regu);
	      value_list->val_cnt++;
	      (*dbval_list_tail) = dbval_list;
	      dbval_list_tail = &dbval_list->next;
	      dbval_list->next = NULL;
	    }
	  else
	    {
	      value_list = NULL;
	      break;
	    }
	}
    }

  return value_list;
}


/*
 * pt_find_table_info () - Finds the table_info associated with an exposed name
 *   return:
 *   spec_id(in):
 *   exposed_list(in):
 */
TABLE_INFO *
pt_find_table_info (UINTPTR spec_id, TABLE_INFO * exposed_list)
{
  TABLE_INFO *table_info;

  table_info = exposed_list;

  /* look down list until name matches, or NULL reached */
  while (table_info && table_info->spec_id != spec_id)
    {
      table_info = table_info->next;
    }

  return table_info;
}


/*
 * pt_to_aggregate_node () - test for aggregate function nodes,
 * 	                     convert them to aggregate_list_nodes
 *   return:
 *   parser(in):
 *   tree(in):
 *   arg(in/out):
 *   continue_walk(in/out):
 */
static PT_NODE *
pt_to_aggregate_node (PARSER_CONTEXT * parser, PT_NODE * tree,
		      void *arg, int *continue_walk)
{
  bool is_agg = 0;
  REGU_VARIABLE *regu = NULL;
  AGGREGATE_TYPE *aggregate_list;
  AGGREGATE_INFO *info = (AGGREGATE_INFO *) arg;
  REGU_VARIABLE_LIST out_list;
  REGU_VARIABLE_LIST regu_list;
  REGU_VARIABLE_LIST regu_temp;
  VAL_LIST *value_list;
  QPROC_DB_VALUE_LIST value_temp;
  MOP classop;

  *continue_walk = PT_CONTINUE_WALK;

  is_agg = pt_is_aggregate_function (parser, tree);
  if (is_agg)
    {
      FUNC_TYPE code = tree->info.function.function_type;

      if (code == PT_GROUPBY_NUM)
	{
	  if ((aggregate_list = regu_agg_grbynum_alloc ()) == NULL)
	    {
	      PT_ERROR (parser, tree,
			msgcat_message (MSGCAT_CATALOG_CUBRID,
					MSGCAT_SET_PARSER_SEMANTIC,
					MSGCAT_SEMANTIC_OUT_OF_MEMORY));
	      return tree;
	    }
	  aggregate_list->next = info->head_list;
	  aggregate_list->option = Q_ALL;
	  aggregate_list->domain = &tp_Integer_domain;
	  if (info->grbynum_valp)
	    {
	      if (!(*(info->grbynum_valp)))
		{
		  *(info->grbynum_valp) = regu_dbval_alloc ();
		  regu_dbval_type_init (*(info->grbynum_valp),
					DB_TYPE_INTEGER);
		}
	      aggregate_list->value = *(info->grbynum_valp);
	    }
	  aggregate_list->function = code;
	  aggregate_list->opr_dbtype = DB_TYPE_NULL;
	}
      else
	{
	  if ((aggregate_list = regu_agg_alloc ()) == NULL)
	    {
	      PT_ERROR (parser, tree,
			msgcat_message (MSGCAT_CATALOG_CUBRID,
					MSGCAT_SET_PARSER_SEMANTIC,
					MSGCAT_SEMANTIC_OUT_OF_MEMORY));
	      return tree;
	    }
	  aggregate_list->next = info->head_list;
	  aggregate_list->option =
	    (tree->info.function.all_or_distinct == PT_ALL)
	    ? Q_ALL : Q_DISTINCT;
	  aggregate_list->function = code;
	  /* others will be set after resolving arg_list */
	}

      aggregate_list->flag_agg_optimize = false;
      BTID_SET_NULL (&aggregate_list->btid);
      if (info->flag_agg_optimize &&
	  (aggregate_list->function == PT_COUNT_STAR ||
	   aggregate_list->function == PT_COUNT ||
	   aggregate_list->function == PT_MAX ||
	   aggregate_list->function == PT_MIN))
	{
	  bool need_unique_index;

	  classop = sm_find_class (info->class_name);
	  if (aggregate_list->function == PT_COUNT_STAR ||
	      aggregate_list->function == PT_COUNT)
	    {
	      need_unique_index = true;
	    }
	  else
	    {
	      need_unique_index = false;
	    }

	  if (aggregate_list->function == PT_COUNT_STAR)
	    {
	      (void) sm_find_index (classop, NULL, 0,
				    need_unique_index, &aggregate_list->btid);
	      /* If btree does not exist, optimize with heap */
	      aggregate_list->flag_agg_optimize = true;
	    }
	  else
	    {
	      if (tree->info.function.arg_list->node_type == PT_NAME)
		{
		  (void) sm_find_index (classop,
					(char **) &tree->info.function.
					arg_list->info.name.original, 1,
					need_unique_index,
					&aggregate_list->btid);
		  if (!BTID_IS_NULL (&aggregate_list->btid))
		    {
		      aggregate_list->flag_agg_optimize = true;
		    }
		}
	    }
	}

      if (aggregate_list->function != PT_COUNT_STAR &&
	  aggregate_list->function != PT_GROUPBY_NUM)
	{
	  regu = pt_to_regu_variable (parser,
				      tree->info.function.arg_list,
				      UNBOX_AS_VALUE);

	  if (!regu)
	    {
	      return NULL;
	    }

	  aggregate_list->domain = pt_xasl_node_to_domain (parser, tree);
	  regu_dbval_type_init (aggregate_list->value,
				pt_node_to_db_type (tree));
	  regu_dbval_type_init (aggregate_list->value2,
				pt_node_to_db_type (tree));
	  aggregate_list->opr_dbtype =
	    pt_node_to_db_type (tree->info.function.arg_list);

	  if (info->out_list && info->value_list && info->regu_list)
	    {
	      int *attr_offsets;
	      PT_NODE *pt_val;

	      /* handle the buildlist case.
	       * append regu to the out_list, and create a new value
	       * to append to the value_list  */

	      pt_val = parser_new_node (parser, PT_VALUE);
	      pt_val->type_enum = PT_TYPE_INTEGER;
	      pt_val->info.value.data_value.i = 0;
	      parser_append_node (pt_val, info->out_names);

	      attr_offsets =
		pt_make_identity_offsets (tree->info.function.arg_list);
	      value_list = pt_make_val_list (tree->info.function.arg_list);
	      regu_list = pt_to_position_regu_variable_list
		(parser, tree->info.function.arg_list,
		 value_list, attr_offsets);
	      free_and_init (attr_offsets);

	      out_list = regu_varlist_alloc ();
	      if (!value_list || !regu_list || !out_list)
		{
		  PT_ERROR (parser, tree,
			    msgcat_message (MSGCAT_CATALOG_CUBRID,
					    MSGCAT_SET_PARSER_SEMANTIC,
					    MSGCAT_SEMANTIC_OUT_OF_MEMORY));
		  return NULL;
		}

	      aggregate_list->operand.type = TYPE_CONSTANT;
	      aggregate_list->operand.domain = pt_xasl_node_to_domain
		(parser, tree->info.function.arg_list);
	      aggregate_list->operand.value.dbvalptr = value_list->valp->val;

	      regu_list->value.value.pos_descr.pos_no =
		info->out_list->valptr_cnt;

	      /* append value holder to value_list */
	      info->value_list->val_cnt++;
	      value_temp = info->value_list->valp;
	      while (value_temp->next)
		{
		  value_temp = value_temp->next;
		}
	      value_temp->next = value_list->valp;

	      /* append out_list to info->out_list */
	      info->out_list->valptr_cnt++;
	      out_list->next = NULL;
	      out_list->value = *regu;
	      regu_temp = info->out_list->valptrp;
	      while (regu_temp->next)
		{
		  regu_temp = regu_temp->next;
		}
	      regu_temp->next = out_list;

	      /* append regu to info->regu_list */
	      regu_temp = info->regu_list;
	      while (regu_temp->next)
		{
		  regu_temp = regu_temp->next;
		}
	      regu_temp->next = regu_list;
	    }
	  else
	    {
	      /* handle the buildvalue case, simply uses regu as the operand */
	      aggregate_list->operand = *regu;
	    }
	}
      else
	{
	  /* We are set up for count(*).
	   * Make sure that Q_DISTINCT isn't set in this case.  Even
	   * though it is ignored by the query processing proper, it
	   * seems to cause the setup code to build the extendible hash
	   * table it needs for a "select count(distinct foo)" query,
	   * which adds a lot of unnecessary overhead.
	   */
	  aggregate_list->option = Q_ALL;

	  aggregate_list->domain = &tp_Integer_domain;
	  regu_dbval_type_init (aggregate_list->value, DB_TYPE_INTEGER);
	  regu_dbval_type_init (aggregate_list->value2, DB_TYPE_INTEGER);
	  aggregate_list->opr_dbtype = DB_TYPE_INTEGER;

	  /* hack.  we need to pack some domain even though we don't
	   * need one, so we'll pack the int.
	   */
	  aggregate_list->operand.domain = &tp_Integer_domain;
	}

      /* record the value for pt_to_regu_variable to use in "out arith" */
      tree->etc = (void *) aggregate_list->value;

      info->head_list = aggregate_list;

      *continue_walk = PT_LIST_WALK;
    }

  if (tree->node_type == PT_DOT_)
    {
      /* This path must have already appeared in the group-by, and is
       * resolved. Convert it to a name so that we can use it to get
       * the correct list position later.
       */
      PT_NODE *next = tree->next;
      tree = tree->info.dot.arg2;
      tree->next = next;
    }

  if (tree->node_type == PT_SELECT
      || tree->node_type == PT_UNION
      || tree->node_type == PT_INTERSECTION
      || tree->node_type == PT_DIFFERENCE)
    {
      /* this is a sub-query. It has its own aggregation scope.
       * Do not proceed down the leaves. */
      *continue_walk = PT_LIST_WALK;
    }

  if (tree->node_type == PT_NAME)
    {
      if (!pt_find_name (parser, tree, info->out_names)
	  && (info->out_list && info->value_list && info->regu_list))
	{
	  int *attr_offsets;
	  PT_NODE *pointer;

	  /* handle the buildlist case for a name found in an expression
	   * but not by itself on the group by list.
	   * Normally this would result from a view column in
	   * a group by translating to an expression.
	   * We need to add it to the out list for the intermediate
	   * table, so that the expression will be properly evaluated
	   * when re-reading the table. Otherwise, we will silently
	   * resolve this name to a db_value that is not being loaded
	   * from the intemediate table.
	   */
	  pointer = pt_point (parser, tree);

	  /* append the name on the out list */
	  info->out_names = parser_append_node (pointer, info->out_names);

	  attr_offsets = pt_make_identity_offsets (pointer);
	  value_list = pt_make_val_list (pointer);
	  regu_list = pt_to_position_regu_variable_list
	    (parser, pointer, value_list, attr_offsets);
	  free_and_init (attr_offsets);

	  out_list = regu_varlist_alloc ();
	  if (!value_list || !regu_list || !out_list)
	    {
	      PT_ERROR (parser, pointer,
			msgcat_message (MSGCAT_CATALOG_CUBRID,
					MSGCAT_SET_PARSER_SEMANTIC,
					MSGCAT_SEMANTIC_OUT_OF_MEMORY));
	      return NULL;
	    }

	  /* fix count for list position */
	  regu_list->value.value.pos_descr.pos_no =
	    info->out_list->valptr_cnt;

	  /* append value holder to value_list */
	  info->value_list->val_cnt++;
	  value_temp = info->value_list->valp;
	  while (value_temp->next)
	    {
	      value_temp = value_temp->next;
	    }
	  value_temp->next = value_list->valp;

	  regu = pt_to_regu_variable (parser, tree, UNBOX_AS_VALUE);

	  if (!regu)
	    {
	      return NULL;
	    }

	  /* append out_list to info->out_list */
	  info->out_list->valptr_cnt++;
	  out_list->next = NULL;
	  out_list->value = *regu;
	  regu_temp = info->out_list->valptrp;
	  while (regu_temp->next)
	    {
	      regu_temp = regu_temp->next;
	    }
	  regu_temp->next = out_list;

	  /* append regu to info->regu_list */
	  regu_temp = info->regu_list;
	  while (regu_temp->next)
	    {
	      regu_temp = regu_temp->next;
	    }
	  regu_temp->next = regu_list;
	}
      *continue_walk = PT_LIST_WALK;
    }

  if (tree->node_type == PT_SPEC || tree->node_type == PT_DATA_TYPE)
    {
      /* These node types cannot have sub-expressions.
       * Do not proceed down the leaves */
      *continue_walk = PT_LIST_WALK;
    }
  return tree;
}

/*
 * pt_find_attribute () -
 *   return: index of a name in an attribute symbol list,
 *           or -1 if the name is not found in the list
 *   parser(in):
 *   name(in):
 *   attributes(in):
 */
int
pt_find_attribute (PARSER_CONTEXT * parser, const PT_NODE * name,
		   const PT_NODE * attributes)
{
  PT_NODE *attr, *save_attr;
  int i = 0;

  if (name)
    {
      CAST_POINTER_TO_NODE (name);

      if (name->node_type == PT_NAME)
	{
	  for (attr = (PT_NODE *) attributes; attr != NULL; attr = attr->next)
	    {
	      save_attr = attr;	/* save */

	      CAST_POINTER_TO_NODE (attr);

	      /* are we looking up sort_spec list ?
	       * currently only group by causes this case. */
	      if (attr->node_type == PT_SORT_SPEC)
		{
		  attr = attr->info.sort_spec.expr;
		}

	      if (!name->info.name.resolved)
		{
		  /* are we looking up a path expression name?
		   * currently only group by causes this case. */
		  if (attr->node_type == PT_DOT_
		      && pt_name_equal (parser,
					(PT_NODE *) name,
					attr->info.dot.arg2))
		    {
		      return i;
		    }
		}

	      if (pt_name_equal (parser, (PT_NODE *) name, attr))
		{
		  return i;
		}
	      i++;

	      attr = save_attr;	/* restore */
	    }
	}
    }

  return -1;
}

/*
 * pt_index_value () -
 *   return: the DB_VALUE at the index position in a VAL_LIST
 *   value(in):
 *   index(in):
 */
DB_VALUE *
pt_index_value (const VAL_LIST * value, int index)
{
  QPROC_DB_VALUE_LIST dbval_list;
  DB_VALUE *dbval = NULL;

  if (value && index >= 0)
    {
      dbval_list = value->valp;
      while (dbval_list && index)
	{
	  dbval_list = dbval_list->next;
	  index--;
	}

      if (dbval_list)
	{
	  dbval = dbval_list->val;
	}
    }

  return dbval;
}



/*
 * pt_to_aggregate () - Generates an aggregate list from a select node
 *   return:
 *   parser(in):
 *   select_node(in):
 *   out_list(in):
 *   value_list(in):
 *   regu_list(in):
 *   out_names(in):
 *   grbynum_valp(in):
 *   flag_agg_optimize(in):
 */
AGGREGATE_TYPE *
pt_to_aggregate (PARSER_CONTEXT * parser, PT_NODE * select_node,
		 OUTPTR_LIST * out_list,
		 VAL_LIST * value_list,
		 REGU_VARIABLE_LIST regu_list,
		 PT_NODE * out_names,
		 DB_VALUE ** grbynum_valp, int flag_agg_optimize)
{
  PT_NODE *select_list = select_node->info.query.q.select.list;
  PT_NODE *having = select_node->info.query.q.select.having;
  AGGREGATE_INFO info;

  info.head_list = NULL;
  info.value_list = value_list;
  info.out_list = out_list;
  info.regu_list = regu_list;
  info.out_names = out_names;
  info.grbynum_valp = grbynum_valp;
  info.flag_agg_optimize = flag_agg_optimize;

  if (flag_agg_optimize)
    {
      if (select_node->info.query.q.select.from->info.spec.entity_name)
	info.class_name =
	  select_node->info.query.q.select.from->info.spec.entity_name->info.
	  name.original;
      else
	info.flag_agg_optimize = false;
    }

  select_node->info.query.q.select.list =
    parser_walk_tree (parser, select_list, pt_to_aggregate_node, &info,
		      pt_continue_walk, NULL);

  select_node->info.query.q.select.having =
    parser_walk_tree (parser, having, pt_to_aggregate_node, &info,
		      pt_continue_walk, NULL);

  return info.head_list;
}


/*
 * pt_make_table_info () - Sets up symbol table entry for an entity spec
 *   return:
 *   parser(in):
 *   table_spec(in):
 */
TABLE_INFO *
pt_make_table_info (PARSER_CONTEXT * parser, PT_NODE * table_spec)
{
  TABLE_INFO *table_info;

  table_info = pt_table_info_alloc ();

  table_info->class_spec = table_spec;

  if (table_spec->info.spec.range_var)
    {
      table_info->exposed =
	table_spec->info.spec.range_var->info.name.original;
    }

  table_info->spec_id = table_spec->info.spec.id;

  /* for classes, it is safe to prune unreferenced attributes.
   * we do not have the same luxury with derived tables, so get them all
   * (and in order). */
  table_info->attribute_list =
    (table_spec->info.spec.flat_entity_list)
    ? table_spec->info.spec.referenced_attrs
    : table_spec->info.spec.as_attr_list;

  table_info->value_list = pt_make_val_list (table_info->attribute_list);

  if (!table_info->value_list)
    {
      PT_ERRORm (parser, table_info->attribute_list,
		 MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_OUT_OF_MEMORY);
      return NULL;
    }

  return table_info;
}


/*
 * pt_push_fetch_spec_info () - Sets up symbol table information
 *                              for a select statement
 *   return:
 *   parser(in):
 *   symbols(in):
 *   fetch_spec(in):
 */
static SYMBOL_INFO *
pt_push_fetch_spec_info (PARSER_CONTEXT * parser,
			 SYMBOL_INFO * symbols, PT_NODE * fetch_spec)
{
  PT_NODE *spec;
  TABLE_INFO *table_info;

  for (spec = fetch_spec; spec != NULL; spec = spec->next)
    {
      table_info = pt_make_table_info (parser, spec);
      if (!table_info)
	{
	  symbols = NULL;
	  break;
	}
      table_info->next = symbols->table_info;
      table_info->is_fetch = 1;

      symbols->table_info = table_info;
      symbols = pt_push_fetch_spec_info (parser, symbols,
					 spec->info.spec.path_entities);
    }

  return symbols;
}

/*
 * pt_push_symbol_info () - Sets up symbol table information
 *                          for a select statement
 *   return:
 *   parser(in):
 *   select_node(in):
 */
SYMBOL_INFO *
pt_push_symbol_info (PARSER_CONTEXT * parser, PT_NODE * select_node)
{
  PT_NODE *table_spec;
  SYMBOL_INFO *symbols = NULL;
  TABLE_INFO *table_info;
  PT_NODE *from_list;

  symbols = pt_symbol_info_alloc ();

  if (symbols)
    {
      /*  push symbols on stack */
      symbols->stack = parser->symbols;
      parser->symbols = symbols;

      if (select_node->node_type == PT_SELECT)
	{
	  /* remove psuedo specs */
	  select_node->info.query.q.select.from = pt_filter_psuedo_specs
	    (parser, select_node->info.query.q.select.from);

	  from_list = select_node->info.query.q.select.from;

	  for (table_spec = from_list; table_spec != NULL;
	       table_spec = table_spec->next)
	    {
	      table_info = pt_make_table_info (parser, table_spec);
	      if (!table_info)
		{
		  symbols = NULL;
		  break;
		}
	      table_info->next = symbols->table_info;
	      symbols->table_info = table_info;

	      symbols = pt_push_fetch_spec_info
		(parser, symbols, table_spec->info.spec.path_entities);
	      if (!symbols)
		{
		  break;
		}
	    }

	  if (symbols)
	    {
	      symbols->current_class = NULL;
	      symbols->current_listfile = NULL;
	      symbols->listfile_unbox = UNBOX_AS_VALUE;
	    }
	}
    }

  return symbols;
}


/*
 * pt_pop_symbol_info () - Cleans up symbol table information
 *                         for a select statement
 *   return: none
 *   parser(in):
 *   select_node(in):
 */
void
pt_pop_symbol_info (PARSER_CONTEXT * parser)
{
  SYMBOL_INFO *symbols = NULL;

  if (parser->symbols)
    {
      /* allocated from pt_alloc_packing_buf */
      symbols = parser->symbols->stack;
      parser->symbols = symbols;
    }
  else
    {
      if (!parser->error_msgs)
	{
	  PT_INTERNAL_ERROR (parser, "generate");
	}
    }
}


/*
 * pt_make_access_spec () - Create an initialized ACCESS_SPEC_TYPE structure,
 *	                    ready to be specialised for class or list
 *   return:
 *   spec_type(in):
 *   access(in):
 *   indexptr(in):
 *   where_key(in):
 *   where_pred(in):
 */
static ACCESS_SPEC_TYPE *
pt_make_access_spec (TARGET_TYPE spec_type,
		     ACCESS_METHOD access,
		     INDX_INFO * indexptr,
		     PRED_EXPR * where_key, PRED_EXPR * where_pred)
{
  ACCESS_SPEC_TYPE *spec = NULL;

  if (access != INDEX || indexptr)
    {
      spec = regu_spec_alloc (spec_type);
    }

  if (spec)
    {
      spec->type = spec_type;
      spec->access = access;
      spec->lock_hint = LOCKHINT_NONE;
      spec->indexptr = indexptr;
      spec->where_key = where_key;
      spec->where_pred = where_pred;
      spec->next = NULL;
    }

  return spec;
}


/*
 * pt_cnt_attrs () - Count the number of regu variables in the list that
 *                   are coming from the heap (ATTR_ID)
 *   return:
 *   attr_list(in):
 */
static int
pt_cnt_attrs (const REGU_VARIABLE_LIST attr_list)
{
  int cnt = 0;
  REGU_VARIABLE_LIST tmp;

  for (tmp = attr_list; tmp; tmp = tmp->next)
    {
      if ((tmp->value.type == TYPE_ATTR_ID) ||
	  (tmp->value.type == TYPE_SHARED_ATTR_ID) ||
	  (tmp->value.type == TYPE_CLASS_ATTR_ID))
	{
	  cnt++;
	}
      else if (tmp->value.type == TYPE_FUNC)
	{
	  /* need to check all the operands for the function */
	  cnt += pt_cnt_attrs (tmp->value.value.funcp->operand);
	}
    }

  return cnt;
}


/*
 * pt_fill_in_attrid_array () - Fill in the attrids of the regu variables
 *                              in the list that are comming from the heap
 *   return:
 *   attr_list(in):
 *   attr_array(in):
 *   next_pos(in): holds the next spot in the array to be filled in with the
 *                 next attrid
 */
static void
pt_fill_in_attrid_array (REGU_VARIABLE_LIST attr_list, ATTR_ID * attr_array,
			 int *next_pos)
{
  REGU_VARIABLE_LIST tmp;

  for (tmp = attr_list; tmp; tmp = tmp->next)
    {
      if ((tmp->value.type == TYPE_ATTR_ID) ||
	  (tmp->value.type == TYPE_SHARED_ATTR_ID) ||
	  (tmp->value.type == TYPE_CLASS_ATTR_ID))
	{
	  attr_array[*next_pos] = tmp->value.value.attr_descr.id;
	  *next_pos = *next_pos + 1;
	}
      else if (tmp->value.type == TYPE_FUNC)
	{
	  /* need to check all the operands for the function */
	  pt_fill_in_attrid_array (tmp->value.value.funcp->operand,
				   attr_array, next_pos);
	}
    }
}


/*
 * pt_make_class_access_spec () - Create an initialized
 *                                ACCESS_SPEC_TYPE TARGET_CLASS structure
 *   return:
 *   parser(in):
 *   flat(in):
 *   class(in):
 *   scan_type(in):
 *   access(in):
 *   lock_hint(in):
 *   indexptr(in):
 *   where_key(in):
 *   where_pred(in):
 *   attr_list_key(in):
 *   attr_list_pred(in):
 *   attr_list_rest(in):
 *   cache_key(in):
 *   cache_pred(in):
 *   cache_rest(in):
 */
ACCESS_SPEC_TYPE *
pt_make_class_access_spec (PARSER_CONTEXT * parser,
			   PT_NODE * flat,
			   DB_OBJECT * class_,
			   TARGET_TYPE scan_type,
			   ACCESS_METHOD access,
			   int lock_hint,
			   INDX_INFO * indexptr,
			   PRED_EXPR * where_key,
			   PRED_EXPR * where_pred,
			   REGU_VARIABLE_LIST attr_list_key,
			   REGU_VARIABLE_LIST attr_list_pred,
			   REGU_VARIABLE_LIST attr_list_rest,
			   HEAP_CACHE_ATTRINFO * cache_key,
			   HEAP_CACHE_ATTRINFO * cache_pred,
			   HEAP_CACHE_ATTRINFO * cache_rest)
{
  ACCESS_SPEC_TYPE *spec;
  HFID *hfid;
  int attrnum;

  spec = pt_make_access_spec (scan_type, access, indexptr,
			      where_key, where_pred);
  if (spec)
    {
      /* need to lock class for read
       * We may have already locked it for write, but that is ok, isn't it? */
      spec->lock_hint = lock_hint;
      if (!locator_fetch_class (class_, DB_FETCH_CLREAD_INSTREAD))
	{
	  PT_ERRORc (parser, flat, er_msg ());
	  return NULL;
	}
      hfid = sm_get_heap (class_);
      if (!class_ || !hfid)
	{
	  return NULL;
	}

      spec->s.cls_node.cls_regu_list_key = attr_list_key;
      spec->s.cls_node.cls_regu_list_pred = attr_list_pred;
      spec->s.cls_node.cls_regu_list_rest = attr_list_rest;
      spec->s.cls_node.hfid = *hfid;
      spec->s.cls_node.cls_oid = *WS_OID (class_);
      spec->s.cls_node.num_attrs_key = pt_cnt_attrs (attr_list_key);
      spec->s.cls_node.attrids_key =
	regu_int_array_alloc (spec->s.cls_node.num_attrs_key);
      attrnum = 0;
      /* for multi-column index, need to modify attr_id */
      pt_fill_in_attrid_array (attr_list_key,
			       spec->s.cls_node.attrids_key, &attrnum);
      spec->s.cls_node.cache_key = cache_key;
      spec->s.cls_node.num_attrs_pred = pt_cnt_attrs (attr_list_pred);
      spec->s.cls_node.attrids_pred =
	regu_int_array_alloc (spec->s.cls_node.num_attrs_pred);
      attrnum = 0;
      pt_fill_in_attrid_array (attr_list_pred,
			       spec->s.cls_node.attrids_pred, &attrnum);
      spec->s.cls_node.cache_pred = cache_pred;
      spec->s.cls_node.num_attrs_rest = pt_cnt_attrs (attr_list_rest);
      spec->s.cls_node.attrids_rest =
	regu_int_array_alloc (spec->s.cls_node.num_attrs_rest);
      attrnum = 0;
      pt_fill_in_attrid_array (attr_list_rest,
			       spec->s.cls_node.attrids_rest, &attrnum);
      spec->s.cls_node.cache_rest = cache_rest;
    }

  return spec;
}


/*
 * pt_make_list_access_spec () - Create an initialized
 *                               ACCESS_SPEC_TYPE TARGET_LIST structure
 *   return:
 *   xasl(in):
 *   access(in):
 *   indexptr(in):
 *   where_pred(in):
 *   attr_list_pred(in):
 *   attr_list_rest(in):
 */
ACCESS_SPEC_TYPE *
pt_make_list_access_spec (XASL_NODE * xasl,
			  ACCESS_METHOD access,
			  INDX_INFO * indexptr,
			  PRED_EXPR * where_pred,
			  REGU_VARIABLE_LIST attr_list_pred,
			  REGU_VARIABLE_LIST attr_list_rest)
{
  ACCESS_SPEC_TYPE *spec;

  if (!xasl)
    {
      return NULL;
    }

  spec = pt_make_access_spec (TARGET_LIST, access,
			      indexptr, NULL, where_pred);

  if (spec)
    {
      spec->s.list_node.list_regu_list_pred = attr_list_pred;
      spec->s.list_node.list_regu_list_rest = attr_list_rest;
      spec->s.list_node.xasl_node = xasl;
    }

  return spec;
}


/*
 * pt_make_set_access_spec () - Create an initialized
 *                              ACCESS_SPEC_TYPE TARGET_SET structure
 *   return:
 *   set_expr(in):
 *   access(in):
 *   indexptr(in):
 *   where_pred(in):
 *   attr_list(in):
 */
ACCESS_SPEC_TYPE *
pt_make_set_access_spec (REGU_VARIABLE * set_expr,
			 ACCESS_METHOD access,
			 INDX_INFO * indexptr,
			 PRED_EXPR * where_pred, REGU_VARIABLE_LIST attr_list)
{
  ACCESS_SPEC_TYPE *spec;

  if (!set_expr)
    {
      return NULL;
    }

  spec = pt_make_access_spec (TARGET_SET, access, indexptr, NULL, where_pred);

  if (spec)
    {
      spec->s.set_node.set_regu_list = attr_list;
      spec->s.set_node.set_ptr = set_expr;
    }

  return spec;
}


/*
 * pt_make_cselect_access_spec () - Create an initialized
 * 				    ACCESS_SPEC_TYPE TARGET_METHOD structure
 *   return:
 *   xasl(in):
 *   method_sig_list(in):
 *   access(in):
 *   indexptr(in):
 *   where_pred(in):
 *   attr_list(in):
 */
ACCESS_SPEC_TYPE *
pt_make_cselect_access_spec (XASL_NODE * xasl,
			     METHOD_SIG_LIST *
			     method_sig_list,
			     ACCESS_METHOD access,
			     INDX_INFO * indexptr,
			     PRED_EXPR * where_pred,
			     REGU_VARIABLE_LIST attr_list)
{
  ACCESS_SPEC_TYPE *spec;

  if (!xasl)
    {
      return NULL;
    }

  spec = pt_make_access_spec (TARGET_METHOD, access,
			      indexptr, NULL, where_pred);

  if (spec)
    {
      spec->s.method_node.method_regu_list = attr_list;
      spec->s.method_node.xasl_node = xasl;
      spec->s.method_node.method_sig_list = method_sig_list;
    }

  return spec;
}


/*
 * pt_to_pos_descr () - Translate PT_SORT_SPEC node to QFILE_TUPLE_VALUE_POSITION node
 *   return:
 *   parser(in):
 *   node(in):
 *   root(in):
 */
QFILE_TUPLE_VALUE_POSITION
pt_to_pos_descr (PARSER_CONTEXT * parser, PT_NODE * node, PT_NODE * root)
{
  QFILE_TUPLE_VALUE_POSITION pos;
  int i;
  PT_NODE *temp;
  char *node_str = NULL;

  pos.pos_no = -1;		/* init */
  pos.dom = NULL;		/* init */

  switch (root->node_type)
    {
    case PT_SELECT:
      i = 1;			/* PT_SORT_SPEC pos_no start from 1 */

      if (node->node_type == PT_EXPR)
	{
	  unsigned int save_custom;

	  save_custom = parser->custom_print;	/* save */
	  parser->custom_print |= PT_CONVERT_RANGE;

	  node_str = parser_print_tree (parser, node);

	  parser->custom_print = save_custom;	/* restore */
	}

      for (temp = root->info.query.q.select.list; temp != NULL;
	   temp = temp->next)
	{
	  if (node->node_type == PT_NAME)
	    {
	      if (pt_name_equal (parser, temp, node))
		{
		  pos.pos_no = i;
		}
	    }
	  else if (node->node_type == PT_EXPR)
	    {
	      if (pt_streq (node_str, parser_print_tree (parser, temp)) == 0)
		{
		  pos.pos_no = i;
		}
	    }
	  else
	    {			/* node type must be an integer */
	      if (node->info.value.data_value.i == i)
		{
		  pos.pos_no = i;
		}
	    }

	  if (pos.pos_no != -1)
	    {			/* found match */
	      if (temp->type_enum != PT_TYPE_NONE &&
		  temp->type_enum != PT_TYPE_MAYBE)
		{		/* is resolved */
		  pos.dom = pt_xasl_node_to_domain (parser, temp);
		}
	      break;
	    }

	  i++;
	}

      break;

    case PT_UNION:
    case PT_INTERSECTION:
    case PT_DIFFERENCE:
      pos = pt_to_pos_descr (parser, node, root->info.query.q.union_.arg1);
      break;

    default:
      /* an error */
      break;
    }

  if (pos.pos_no == -1 || pos.dom == NULL)
    {				/* an error */
      pos.pos_no = -1;
      pos.dom = NULL;
    }

  return pos;
}


/*
 * pt_to_sort_list () - Translate a list of PT_SORT_SPEC nodes
 *                      to SORT_LIST list
 *   return:
 *   parser(in):
 *   node_list(in):
 *   root(in):
 *   sort_mode(in):
 */
static SORT_LIST *
pt_to_sort_list (PARSER_CONTEXT * parser, PT_NODE * node_list,
		 PT_NODE * root, SORT_LIST_MODE sort_mode)
{
  SORT_LIST *sort_list, *sort, *lastsort;
  PT_NODE *node, *expr, *col, *col_list;
  int i, k;

  sort_list = sort = lastsort = NULL;
  i = 0;			/* SORT_LIST pos_no start from 0 */
  col_list = pt_get_select_list (parser, root);

  for (node = node_list; node != NULL; node = node->next)
    {
      /* safe guard: invalid parse tree */
      if (node->node_type != PT_SORT_SPEC ||
	  (expr = node->info.sort_spec.expr) == NULL)
	{
	  regu_set_error_with_zero_args (ER_REGU_SYSTEM);
	  return NULL;
	}

      /* check for end-of-sort */
      if (node->info.sort_spec.pos_descr.pos_no <= 0)
	{
	  if (sort_mode == SORT_LIST_AFTER_ISCAN ||
	      sort_mode == SORT_LIST_ORDERBY)
	    {			/* internal error */
	      if (!parser->error_msgs)
		{
		  PT_INTERNAL_ERROR (parser, "generate order_by");
		}
	      return NULL;
	    }
	  else if (sort_mode == SORT_LIST_AFTER_GROUPBY)
	    {
	      /* i-th GROUP BY element does not appear in the select list.
	       * stop building sort_list */
	      break;
	    }
	}

      /* check for domain info */
      if (node->info.sort_spec.pos_descr.dom == NULL)
	{
	  if (sort_mode == SORT_LIST_GROUPBY)
	    {
	      /* get domain from sort_spec node */
	      if (expr->type_enum != PT_TYPE_NONE &&
		  expr->type_enum != PT_TYPE_MAYBE)
		{		/* is resolved */
		  node->info.sort_spec.pos_descr.dom =
		    pt_xasl_node_to_domain (parser, expr);
		}
	    }
	  else
	    {
	      /* get domain from corresponding column node */
	      for (col = col_list, k = 1; col; col = col->next, k++)
		{
		  if (node->info.sort_spec.pos_descr.pos_no == k)
		    {
		      break;	/* match */
		    }
		}

	      if (col &&
		  col->type_enum != PT_TYPE_NONE &&
		  col->type_enum != PT_TYPE_MAYBE)
		{		/* is resolved */
		  node->info.sort_spec.pos_descr.dom =
		    pt_xasl_node_to_domain (parser, col);
		}
	    }

	  /* internal error */
	  if (node->info.sort_spec.pos_descr.dom == NULL)
	    {
	      if (!parser->error_msgs)
		{
		  PT_INTERNAL_ERROR
		    (parser,
		     (sort_mode == SORT_LIST_AFTER_ISCAN)
		     ? "generate after_iscan"
		     : (sort_mode == SORT_LIST_ORDERBY)
		     ? "generate order_by"
		     : (sort_mode == SORT_LIST_GROUPBY)
		     ? "generate group_by" : "generate after_group_by");
		}
	      return NULL;
	    }
	}

      sort = regu_sort_list_alloc ();
      if (!sort)
	{
	  regu_set_error_with_zero_args (ER_REGU_SYSTEM);
	  return NULL;
	}

      /* set values */
      sort->s_order =
	(node->info.sort_spec.asc_or_desc == PT_ASC) ? S_ASC : S_DESC;
      sort->pos_descr = node->info.sort_spec.pos_descr;

      /* PT_SORT_SPEC pos_no start from 1, SORT_LIST pos_no start from 0 */
      if (sort_mode == SORT_LIST_GROUPBY)
	{
	  /* set i-th position */
	  sort->pos_descr.pos_no = i++;
	}
      else
	{
	  sort->pos_descr.pos_no--;
	}

      /* link up */
      if (sort_list)
	{
	  lastsort->next = sort;
	}
      else
	{
	  sort_list = sort;
	}

      lastsort = sort;
    }

  return sort_list;
}


/*
 * pt_to_after_iscan () - Translate a list of after iscan PT_SORT_SPEC nodes
 *                        to SORT_LIST list
 *   return:
 *   parser(in):
 *   iscan_list(in):
 *   root(in):
 */
SORT_LIST *
pt_to_after_iscan (PARSER_CONTEXT * parser, PT_NODE * iscan_list,
		   PT_NODE * root)
{
  return pt_to_sort_list (parser, iscan_list, root, SORT_LIST_AFTER_ISCAN);
}


/*
 * pt_to_orderby () - Translate a list of order by PT_SORT_SPEC nodes
 *                    to SORT_LIST list
 *   return:
 *   parser(in):
 *   order_list(in):
 *   root(in):
 */
SORT_LIST *
pt_to_orderby (PARSER_CONTEXT * parser, PT_NODE * order_list, PT_NODE * root)
{
  return pt_to_sort_list (parser, order_list, root, SORT_LIST_ORDERBY);
}


/*
 * pt_to_groupby () - Translate a list of group by PT_SORT_SPEC nodes
 *                    to SORT_LIST list.(ALL ascending)
 *   return:
 *   parser(in):
 *   group_list(in):
 *   root(in):
 */
SORT_LIST *
pt_to_groupby (PARSER_CONTEXT * parser, PT_NODE * group_list, PT_NODE * root)
{
  return pt_to_sort_list (parser, group_list, root, SORT_LIST_GROUPBY);
}


/*
 * pt_to_after_groupby () - Translate a list of after group by PT_SORT_SPEC
 *                          nodes to SORT_LIST list.(ALL ascending)
 *   return:
 *   parser(in):
 *   group_list(in):
 *   root(in):
 */
SORT_LIST *
pt_to_after_groupby (PARSER_CONTEXT * parser, PT_NODE * group_list,
		     PT_NODE * root)
{
  return pt_to_sort_list (parser, group_list, root, SORT_LIST_AFTER_GROUPBY);
}


/*
 * pt_to_pred_terms () -
 *   return:
 *   parser(in):
 *   terms(in): CNF tree
 *   id(in): spec id to test term for
 *   pred(in):
 */
void
pt_to_pred_terms (PARSER_CONTEXT * parser,
		  PT_NODE * terms, UINTPTR id, PRED_EXPR ** pred)
{
  PRED_EXPR *pred1;
  PT_NODE *next;

  while (terms)
    {
      /* break links, they are a short-hand for 'AND' in CNF terms */
      next = terms->next;
      terms->next = NULL;

      if (terms->node_type == PT_EXPR && terms->info.expr.op == PT_AND)
	{
	  pt_to_pred_terms (parser, terms->info.expr.arg1, id, pred);
	  pt_to_pred_terms (parser, terms->info.expr.arg2, id, pred);
	}
      else
	{
	  if (terms->spec_ident == (UINTPTR) id)
	    {
	      pred1 = pt_to_pred_expr (parser, terms);
	      if (!*pred)
		{
		  *pred = pred1;
		}
	      else
		{
		  *pred = pt_make_pred_expr_pred (pred1, *pred, B_AND);
		}
	    }
	}

      /* repair link */
      terms->next = next;
      terms = next;
    }
}


/*
 * pt_get_original_name () -
 *   return: the original name at the end of a path expression
 *   expr(in):
 */
char *
pt_get_original_name (const PT_NODE * expr)
{
  while (expr->node_type == PT_DOT_)
    {
      expr = expr->info.dot.arg2;
    }

  return (expr->node_type == PT_NAME)
    ? (char *) expr->info.name.original : NULL;
}
