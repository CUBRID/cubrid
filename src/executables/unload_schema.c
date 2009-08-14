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
 * unload_schema.c: Utility that emits database schema definitions
 *                  in interpreter format
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#if !defined (WINDOWS)
#include <unistd.h>
#endif
#include <sys/types.h>
#include <sys/stat.h>

#include "db.h"
#include "authenticate.h"
#include "schema_manager.h"
#include "trigger_manager.h"
#include "load_object.h"
#include "object_primitive.h"
#include "parser.h"
#include "message_catalog.h"
#include "utility.h"
#include "unloaddb.h"
#include "execute_schema.h"
#include "parser.h"
#include "set_object.h"
#include "jsp_cl.h"
#include "class_object.h"

/* this must be the last header file included!!! */
#include "dbval.h"

#define CLASS_NAME_MAX 80

/* suffix names */
#define SCHEMA_SUFFIX "_schema"
#define TRIGGER_SUFFIX "_trigger"
#define INDEX_SUFFIX "_indexes"

typedef enum
{
  INSTANCE_ATTRIBUTE,
  SHARED_ATTRIBUTE,
  CLASS_ATTRIBUTE
} ATTRIBUTE_QUALIFIER;

typedef enum
{
  INSTANCE_METHOD,
  CLASS_METHOD
} METHOD_QUALIFIER;

typedef enum
{
  INSTANCE_RESOLUTION,
  CLASS_RESOLUTION
} RESOLUTION_QUALIFIER;

typedef enum
{
  SERIAL_NAME,
  SERIAL_OWNER_NAME,
  SERIAL_CURRENT_VAL,
  SERIAL_INCREMENT_VAL,
  SERIAL_MAX_VAL,
  SERIAL_MIN_VAL,
  SERIAL_CYCLIC,
  SERIAL_STARTED,

  SERIAL_VALUE_INDEX_MAX
} SERIAL_VALUE_INDEX;

static void filter_system_classes (DB_OBJLIST ** class_list);
static void filter_unrequired_classes (DB_OBJLIST ** class_list);
static int is_dependent_class (DB_OBJECT * class_, DB_OBJLIST * unordered,
			       DB_OBJLIST * ordered);
static int check_domain_dependencies (DB_DOMAIN * domain,
				      DB_OBJECT * this_class,
				      DB_OBJLIST * unordered,
				      DB_OBJLIST * ordered);
static int has_dependencies (DB_OBJECT * class_, DB_OBJLIST * unordered,
			     DB_OBJLIST * ordered, int conservative);
static int order_classes (DB_OBJLIST ** class_list, DB_OBJLIST ** order_list,
			  int conservative);
static void emit_cycle_warning (void);
static void force_one_class (DB_OBJLIST ** class_list,
			     DB_OBJLIST ** order_list);
static DB_OBJLIST *get_ordered_classes (MOP * class_table);
static void emit_class_owner (FILE * fp, MOP class_);
static int export_serial (FILE * outfp);
static int emit_indexes (DB_OBJLIST * classes, int has_indexes,
			 DB_OBJLIST * vclass_list_has_using_index);

static int emit_schema (DB_OBJLIST * classes, int do_auth,
			DB_OBJLIST ** vclass_list_has_using_index);
static bool has_vclass_domains (DB_OBJECT * vclass);
static DB_OBJLIST *emit_query_specs (DB_OBJLIST * classes);
static int emit_query_specs_has_using_index (DB_OBJLIST *
					     vclass_list_has_using_index);
static bool emit_superclasses (DB_OBJECT * class_, const char *class_type);
static bool emit_resolutions (DB_OBJECT * class_, const char *class_type);
static void emit_resolution_def (DB_RESOLUTION * resolution,
				 RESOLUTION_QUALIFIER qualifier);
static bool emit_instance_attributes (DB_OBJECT * class_,
				      const char *class_type,
				      int *has_indexes);
static bool emit_class_attributes (DB_OBJECT * class_,
				   const char *class_type);
static bool emit_all_attributes (DB_OBJECT * class_, const char *class_type,
				 int *has_indexes);
static void emit_method_files (DB_OBJECT * class_);
static bool emit_methods (DB_OBJECT * class_, const char *class_type);
static int ex_contains_object_reference (DB_VALUE * value);
static void emit_attribute_def (DB_ATTRIBUTE * attribute,
				ATTRIBUTE_QUALIFIER qualifier);
static void emit_unique_def (DB_OBJECT * class_);
static void emit_reverse_unique_def (DB_OBJECT * class_);
static void emit_index_def (DB_OBJECT * class_);
static void emit_domain_def (DB_DOMAIN * domains);
static int emit_autoincrement_def (DB_ATTRIBUTE * attribute);
static void emit_method_def (DB_METHOD * method, METHOD_QUALIFIER qualifier);
static void emit_methfile_def (DB_METHFILE * methfile);
static void emit_partition_parts (MOP parts, int partcnt);
static void emit_partition_info (MOP clsobj);
static int emit_stored_procedure_args (int arg_cnt, DB_SET * arg_set);
static int emit_stored_procedure (void);
static int emit_foreign_key (DB_OBJLIST * classes);

/*
 * CLASS DEPENDENCY ORDERING
 *
 * This section contains code to calculate an ordered list of classes
 * based on subclass dependencies.  The class definitions must be
 * output in order so that they make sense.
 *
 * Algorithm for dependency checking is pretty dumb.  Speed isn't particularly
 * important however since the number of classes is normally well under 1000.
 *
 * Uses workspace ml_ calls for object list maintenance !
 *
 * THOUGHT:  Now that classes are always defined empty and altered later
 * to add attributes & super classes, I don't believe we can get into
 * a situation where the order of definition is important?  Think
 * about this.  If it is the case then we can skip the dependency ordering
 * step.
 *
 */


/*
 * filter_system_classes - Goes through a class list removing system classes.
 *    return: void
 *    class_list(in): class list to filter
 */
static void
filter_system_classes (DB_OBJLIST ** class_list)
{
  DB_OBJLIST *cl, *prev, *next;

  for (cl = *class_list, prev = NULL, next = NULL; cl != NULL; cl = next)
    {
      next = cl->next;
      if (!db_is_system_class (cl->op))
	prev = cl;
      else
	{
	  if (prev == NULL)
	    *class_list = next;
	  else
	    prev->next = next;
	  /*
	   * class_list links were allocated via ml_ext_alloc_link, so we must
	   * free them via ml_ext_free_link.  Otherwise, we can crash.
	   */
	  ml_ext_free_link (cl);
	}
    }
}

/*
 * filter_unrequired_classes - remove unrequired class from list
 *    return: void
 *    class_list(out): class list
 */
static void
filter_unrequired_classes (DB_OBJLIST ** class_list)
{
  DB_OBJLIST *cl, *prev, *next;

  for (cl = *class_list, prev = NULL, next = NULL; cl != NULL; cl = next)
    {
      next = cl->next;
      if (is_req_class (cl->op))
	prev = cl;
      else
	{
	  if (prev == NULL)
	    *class_list = next;
	  else
	    prev->next = next;
	  ml_ext_free_link (cl);
	}
    }
}


/*
 * is_dependent_class - determines if a particular class results in a
 * dependency.
 *    return: non-zero if a dependency exists
 *    class(in): class to ponder
 *    unordered(in): remaining unordered class list
 *    ordered(in): ordered class list
 * Note:
 *    This is determined by seeint if the class is NOT already on the
 *    ordered list AND the class IS on the unordered list.
 *    If the class is not on the unordered list, then we have to
 *    assume that the user is responsible for defining it at the
 *    appropriate time.  This will also handle the case
 *    where we have dependencies on system defined classes since these
 *    won't be in the original class list.
 */
static int
is_dependent_class (DB_OBJECT * class_,
		    DB_OBJLIST * unordered, DB_OBJLIST * ordered)
{
  return (!ml_find (ordered, class_) && ml_find (unordered, class_));
}


/*
 * check_domain_dependencies - checks for dependencies on the classes that are
 * found in a domain specification.
 *    return: non-zero if there are dependencies
 *    domain(in): domain to ponder
 *    this_class(in): class to ckeck
 *    unordered(in): remaining unordered class list
 *    ordered(in): ordered class list
 */
static int
check_domain_dependencies (DB_DOMAIN * domain,
			   DB_OBJECT * this_class,
			   DB_OBJLIST * unordered, DB_OBJLIST * ordered)
{
  DB_DOMAIN *d, *setdomain;
  DB_OBJECT *class_;
  int dependencies;

  dependencies = 0;
  for (d = domain; d != NULL && !dependencies; d = db_domain_next (d))
    {
      setdomain = db_domain_set (d);
      if (setdomain != NULL)
	dependencies =
	  check_domain_dependencies (setdomain, this_class, unordered,
				     ordered);
      else
	{
	  class_ = db_domain_class (d);
	  if (class_ != NULL && class_ != this_class)
	    dependencies = is_dependent_class (class_, unordered, ordered);
	}
    }
  return (dependencies);
}

/*
 * has_dependencies - checks to see if a class has any remaining dependencnes
 *    return: non zero if there are dependencies remaining
 *    mop(in): class to ponder
 *    unordered(in): remaining unordered list
 *    ordered(in): ordered list being built
 *    conservative(in): if set detect a depencency on any class that is used
 *                      as the domain of an attribute in this class.
 */
static int
has_dependencies (DB_OBJECT * mop,
		  DB_OBJLIST * unordered, DB_OBJLIST * ordered,
		  int conservative)
{
  DB_OBJLIST *supers, *su;
  DB_ATTRIBUTE *att;
  DB_DOMAIN *domain;
  int dependencies;

  dependencies = 0;

  supers = db_get_superclasses (mop);
  for (su = supers; su != NULL && !dependencies; su = su->next)
    dependencies = is_dependent_class (su->op, unordered, ordered);

  /*
   * if we're doing a conservative dependency check, look at the domains
   * of each attribute.
   */
  if (!dependencies && conservative)
    {
      for (att = db_get_attributes (mop); att != NULL && !dependencies;
	   att = db_attribute_next (att))
	{
	  domain = db_attribute_domain (att);
	  dependencies =
	    check_domain_dependencies (domain, mop, unordered, ordered);
	}
    }

  return (dependencies);
}


/*
 * order_classes - transfers class list to ordered list if they have no
 * dependencies
 *    return: number of classes transfered
 *    class_list(in): remaining unordered list
 *    order_list(out): order list we're building
 *    conservative(in): if set detect a depencency on any class that is used
 *                      as the domain of an attribute in this class.
 * Note:
 *    This makes a pass on the unordered class list transfering
 *    classes to the ordered list if they have no dependencies.
 *    We may make many passes over this list but there will be at least
 *    one class moved on each pass.  If a pass is made and no classes
 *    can be moved, in indicates a circular depenency that cannot be
 *    handled by this algorithm.  For normal classes, this would
 *    be an error condition since it indicates a cycle in the class
 *    hierarcy which isn't allowed.
 *    For proxy classes, it indicates circular references between the
 *    attributes that make up the OBJECT_ID which also isn't allowed.
 *    It can also indicate a bug in the dependency algorighm.
 */
static int
order_classes (DB_OBJLIST ** class_list, DB_OBJLIST ** order_list,
	       int conservative)
{
  DB_OBJLIST *cl, *o, *next, *prev, *last;
  int add_count;

  add_count = 0;

  for (cl = *class_list, prev = NULL, next = NULL; cl != NULL; cl = next)
    {
      next = cl->next;

      if (has_dependencies (cl->op, *class_list, *order_list, conservative))
	prev = cl;
      else
	{
	  /* no dependencies, move it to the other list */
	  if (prev == NULL)
	    *class_list = next;
	  else
	    prev->next = next;

	  /* append it on the order list */
	  cl->next = NULL;
	  for (o = *order_list, last = NULL; o != NULL; o = o->next)
	    last = o;
	  if (last == NULL)
	    *order_list = cl;
	  else
	    last->next = cl;
	  add_count++;
	}
    }
  return (add_count);
}


/*
 * emit_cycle_warning - emit cyclic dependency warning
 *    return: void
 * Note:
 *    Dump a warning message enclosed in a comment to the output file
 *    indicating that cycles were encountered in the schema that either
 *    represent error conditions or schema we're not prepared
 *    to handle yet.
 */
static void
emit_cycle_warning (void)
{
  fprintf (output_file, "/* Error calculating class dependency order.\n");
  fprintf (output_file, "   This indicates one of the following:\n");
  fprintf (output_file, "     - bug in dependency algorithm\n");
  fprintf (output_file, "     - cycle in class hierarchy\n");
  fprintf (output_file,
	   "     - cycle in proxy attribute used as object ids\n");
  fprintf (output_file,
	   "   The next class may not be in the proper definition order.\n");
  fprintf (output_file,
	   "   Hand editing of the schema may be required before loading.\n");
  fprintf (output_file, "   */\n");
}


/*
 * force_one_class - pick the top class off the 'class_list' and append to
 * the 'order_list'
 *    return: void
 *    class_list(out): remaining unordered classes
 *    order_list(out): ordered class lsit
 * Note:
 *    This is called when a depencency cycle is detected that wa can't
 *    figure out.  So we at least get the full schema dumped to the
 *    output file, pick the top class off the list and hopefully this
 *    will break the cycle.
 *    The user will then have to go in and hand edit the output file
 *    so that it can be loaded.
 */
static void
force_one_class (DB_OBJLIST ** class_list, DB_OBJLIST ** order_list)
{
  DB_OBJLIST *cl, *o, *last;

  emit_cycle_warning ();

  cl = *class_list;
  *class_list = cl->next;

  cl->next = NULL;
  for (o = *order_list, last = NULL; o != NULL; o = o->next)
    last = o;
  if (last == NULL)
    *order_list = cl;
  else
    last->next = cl;
}


/*
 * get_ordered_classes - takes a list of classes to dump and returns a list
 * of classes ordered according to their definition dependencies.
 *    return: ordered class list
 *    class_table(in): classes to dump
 */
static DB_OBJLIST *
get_ordered_classes (MOP * class_table)
{
  DB_OBJLIST *classes, *ordered;
  int count, i;

  ordered = NULL;

  /*
   * if class_table is passed, use it to initialize the list, otherwise
   * get it from the API.
   */
  if (class_table == NULL)
    {
      classes = sm_fetch_all_classes (1, DB_FETCH_READ);
      if (classes == NULL)
	{
	  return NULL;
	}

      filter_system_classes (&classes);
      if (classes == NULL)
	{			/* no user class */
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_NODATA_TOBE_UNLOADED,
		  0);
	  return NULL;
	}

      if (input_filename && required_class_only)
	{
	  filter_unrequired_classes (&classes);
	}
    }
  else
    {
      classes = NULL;
      for (i = 0; class_table[i] != NULL; i++)
	{
	  if (ml_ext_add (&classes, class_table[i], NULL))
	    {
	      /* memory error */
	      ml_ext_free (classes);
	      return NULL;
	    }
	}
    }

  while (classes != NULL)
    {

      count = order_classes (&classes, &ordered, 1);
      if (count == 0)
	{
	  /*
	   * didn't find any using the conservative ordering, try the
	   * more relaxed one.
	   */
	  count = order_classes (&classes, &ordered, 0);
	  if (count == 0)
	    {
	      force_one_class (&classes, &ordered);
	    }
	}
    }
  return (ordered);
}


/*
 * emit_class_owner - Emits a change_owner statement for a class that has been
 * created.
 *    return:  void
 *    fp(in/out):  FILE pointer
 *    class(in): class MOP
 */
static void
emit_class_owner (FILE * fp, MOP class_)
{
  const char *classname;
  MOP owner;
  DB_VALUE value;

  classname = db_get_class_name (class_);
  if (classname != NULL)
    {
      owner = au_get_class_owner (class_);
      if (owner != NULL)
	{
	  if (db_get (owner, "name", &value) == NO_ERROR)
	    {
	      if (DB_VALUE_TYPE (&value) == DB_TYPE_STRING &&
		  DB_GET_STRING (&value) != NULL)
		{
		  fprintf (fp,
			   "call change_owner('%s', '%s') on class db_root;\n",
			   classname, DB_GET_STRING (&value));
		}
	      db_value_clear (&value);
	    }
	}
    }
}

/*
 * export_serial - export db_serial
 *    return: NO_ERROR if successful, error code otherwise
 *    outfp(in/out): out FILE pointer
 */
static int
export_serial (FILE * outfp)
{
  int error = NO_ERROR;
  int i;
  DB_QUERY_RESULT *query_result;
  DB_QUERY_ERROR query_error;
  DB_VALUE values[SERIAL_VALUE_INDEX_MAX], diff_value, answer_value;

  /*
   * You must check SERIAL_VALUE_INDEX enum defined on the top of this file
   * when changing the following query. Notice the order of the result.
   */
  const char *query = "select name, owner.name, "
    "current_val, "
    "increment_val, "
    "max_val, "
    "min_val, "
    "cast(cyclic as integer), "
    "cast(started as integer) "
    "from db_serial where class_name is null and att_name is null";

  if ((error = db_execute (query, &query_result, &query_error)) < 0)
    goto err;

  if ((error = db_query_first_tuple (query_result)) != DB_CURSOR_SUCCESS)
    goto err;

  do
    {
      for (i = 0; i < SERIAL_VALUE_INDEX_MAX; i++)
	{
	  if ((error = db_query_get_tuple_value (query_result,
						 i, &values[i])) < 0)
	    {
	      goto err;
	    }

	  /* Validation of the result value */
	  switch (i)
	    {
	    case SERIAL_OWNER_NAME:
	      {
		if (DB_IS_NULL (&values[i])
		    || DB_VALUE_TYPE (&values[i]) != DB_TYPE_STRING)
		  {
		    db_make_string (&values[i], "PUBLIC");
		  }
	      }
	      break;

	    case SERIAL_NAME:
	      {
		if (DB_IS_NULL (&values[i])
		    || DB_VALUE_TYPE (&values[i]) != DB_TYPE_STRING)
		  {
		    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			    ER_INVALID_SERIAL_VALUE, 0);
		    error = ER_INVALID_SERIAL_VALUE;
		    goto err;
		  }
	      }
	      break;

	    case SERIAL_CURRENT_VAL:
	    case SERIAL_INCREMENT_VAL:
	    case SERIAL_MAX_VAL:
	    case SERIAL_MIN_VAL:
	      {
		if (DB_IS_NULL (&values[i])
		    || DB_VALUE_TYPE (&values[i]) != DB_TYPE_NUMERIC)
		  {
		    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			    ER_INVALID_SERIAL_VALUE, 0);
		    error = ER_INVALID_SERIAL_VALUE;
		    goto err;
		  }
	      }
	      break;

	    case SERIAL_CYCLIC:
	    case SERIAL_STARTED:
	      {
		if (DB_IS_NULL (&values[i])
		    || DB_VALUE_TYPE (&values[i]) != DB_TYPE_INTEGER)
		  {
		    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			    ER_INVALID_SERIAL_VALUE, 0);
		    error = ER_INVALID_SERIAL_VALUE;
		    goto err;
		  }
	      }
	      break;

	    default:
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_INVALID_SERIAL_VALUE, 0);
	      error = ER_INVALID_SERIAL_VALUE;
	      goto err;
	    }
	}

      if (DB_GET_INTEGER (&values[SERIAL_STARTED]) == 1)
	{
	  /* Calculate next value of serial */
	  error = numeric_db_value_sub (&values[SERIAL_MAX_VAL],
					&values[SERIAL_CURRENT_VAL],
					&diff_value);
	  if (error != NO_ERROR)
	    {
	      goto err;
	    }

	  error =
	    numeric_db_value_compare (&values[SERIAL_INCREMENT_VAL],
				      &diff_value, &answer_value);
	  if (error != NO_ERROR)
	    {
	      goto err;
	    }
	  /* increment > diff */
	  if (DB_GET_INTEGER (&answer_value) > 0)
	    {
	      /* no cyclic case */
	      if (DB_GET_INTEGER (&values[SERIAL_CYCLIC]) == 0)
		{
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_NUM_OVERFLOW,
			  0);
		  error = ER_NUM_OVERFLOW;
		  goto err;
		}

	      db_value_clear (&values[SERIAL_CURRENT_VAL]);
	      values[SERIAL_CURRENT_VAL] = values[SERIAL_MIN_VAL];
	    }
	  /* increment <= diff */
	  else
	    {
	      error = numeric_db_value_add (&values[SERIAL_CURRENT_VAL],
					    &values[SERIAL_INCREMENT_VAL],
					    &answer_value);
	      if (error != NO_ERROR)
		{
		  goto err;
		}

	      db_value_clear (&values[SERIAL_CURRENT_VAL]);
	      values[SERIAL_CURRENT_VAL] = answer_value;
	    }
	}

      fprintf (outfp, "call find_user('%s') on class db_user to auser;\n",
	       DB_PULL_STRING (&values[SERIAL_OWNER_NAME]));
      fprintf (outfp, "create serial %s%s%s\n",
	       PRINT_IDENTIFIER (DB_PULL_STRING (&values[SERIAL_NAME])));
      fprintf (outfp, "\t start with %s\n",
	       numeric_db_value_print (&values[SERIAL_CURRENT_VAL]));
      fprintf (outfp, "\t increment by %s\n",
	       numeric_db_value_print (&values[SERIAL_INCREMENT_VAL]));
      fprintf (outfp, "\t minvalue %s\n",
	       numeric_db_value_print (&values[SERIAL_MIN_VAL]));
      fprintf (outfp, "\t maxvalue %s\n",
	       numeric_db_value_print (&values[SERIAL_MAX_VAL]));
      fprintf (outfp, "\t %scycle;\n",
	       (DB_GET_INTEGER (&values[SERIAL_CYCLIC]) == 0 ? "no" : ""));
      fprintf (outfp,
	       "call change_serial_owner ('%s', '%s') on class db_serial;\n\n",
	       DB_PULL_STRING (&values[SERIAL_NAME]),
	       DB_PULL_STRING (&values[SERIAL_OWNER_NAME]));

      db_value_clear (&diff_value);
      db_value_clear (&answer_value);
      for (i = 0; i < SERIAL_VALUE_INDEX_MAX; i++)
	{
	  db_value_clear (&values[i]);
	}
    }
  while (db_query_next_tuple (query_result) == DB_CURSOR_SUCCESS);

err:
  db_query_end (query_result);
  return error;
}

#define EX_ERROR_CHECK(c,d,m)                                          \
           do {                                                        \
             if (c) {                                                  \
               if (db_error_code() != NO_ERROR) {                      \
                 goto error;                                           \
               }                                                       \
               else {                                                  \
                 if (!d) {  /* if it is not db error check only, */    \
                   if (m != NULL) {                                    \
                     fprintf(stderr, "%s: %s.\n\n",                    \
                                     exec_name, m);                    \
                   }                                                   \
                   else {                                              \
                     fprintf(stderr, "%s: Unknown database error occurs but may not be database error.\n\n",                                           \
                                     exec_name);                       \
                   }                                                   \
                   goto error;                                         \
                 }                                                     \
               }                                                       \
             }                                                         \
           } while (0)




/*
 * extractschema - exports schema to file
 *    return: 0 if successful, error count otherwise
 *    exec_name(in): utility name
 *    do_auth(in): if set do authorization as well
 * Note:
 *    Always output the entire schema.
 */
int
extractschema (const char *exec_name, int do_auth)
{
  char output_filename[PATH_MAX * 2];
  DB_OBJLIST *classes = NULL;
  int has_indexes, total;
  DB_OBJLIST *vclass_list_has_using_index = NULL;
  int err_count = 0;

  if (output_dirname == NULL)
    output_dirname = ".";
  total =
    strlen (output_dirname) + strlen (output_prefix) +
    strlen (SCHEMA_SUFFIX) + 8;
  if ((size_t) total > sizeof (output_filename))
    return 1;

  sprintf (output_filename, "%s/%s%s", output_dirname, output_prefix,
	   SCHEMA_SUFFIX);
  if ((output_file = fopen_ex (output_filename, "w")) == NULL)
    {
      (void) fprintf (stderr, "%s: %s.\n\n", exec_name, strerror (errno));
      return errno;
    }

  /*
   * convert the class table into an ordered class list, would be better
   * if we just built the initial list rather than using the table.
   */
  classes = get_ordered_classes (NULL);
  if (classes == NULL)
    {
      if (db_error_code () != NO_ERROR)
	{
	  goto error;
	}
      else
	{
	  fprintf (stderr,
		   "%s: Unknown database error occurs "
		   "but may not be database error.\n\n", exec_name);
	  goto error;
	}
    }

  /*
   * Schema
   */
  if (!required_class_only && do_auth)
    if (au_export_users (output_file) != NO_ERROR)
      err_count++;

  if (!required_class_only && export_serial (output_file) < 0)
    {
      fprintf (stderr, "%s", db_error_string (3));
      if (db_error_code () == ER_INVALID_SERIAL_VALUE)
	{
	  fprintf (stderr, " Check the value of db_serial object.\n");
	}
    }

  has_indexes = emit_schema (classes, do_auth, &vclass_list_has_using_index);
  if (er_errid () != NO_ERROR)
    err_count++;

  if (emit_foreign_key (classes) != NO_ERROR)
    err_count++;

  if (emit_stored_procedure () != NO_ERROR)
    err_count++;

  fprintf (output_file, "\n");
  fprintf (output_file, "COMMIT WORK;\n");
  fclose (output_file);
  output_file = NULL;

  /*
   * Trigger
   * emit the triggers last, they will have no mutual dependencies so
   * it doesn't really matter what order they're in.
   */
  total = strlen (output_dirname) + strlen (output_prefix) +
    strlen (TRIGGER_SUFFIX) + 8;
  if ((size_t) total > sizeof (output_filename))
    return 1;

  sprintf (output_filename, "%s/%s%s",
	   output_dirname, output_prefix, TRIGGER_SUFFIX);

  if ((output_file = fopen_ex (output_filename, "w")) == NULL)
    {
      (void) fprintf (stderr, "%s: %s.\n\n", exec_name, strerror (errno));
      return errno;
    }

  if (tr_dump_selective_triggers (output_file, delimited_id_flag, classes) !=
      NO_ERROR)
    err_count++;

  fflush (output_file);

  if (ftell (output_file) == 0)
    {
      /* file is empty (database has no trigger to be emitted) */
      fclose (output_file);
      output_file = NULL;
      remove (output_filename);
    }
  else
    {				/* not empty */
      fprintf (output_file, "\n\n");
      fprintf (output_file, "COMMIT WORK;\n");
      fclose (output_file);
      output_file = NULL;
    }

  /*
   * Index
   */
  if (emit_indexes (classes, has_indexes, vclass_list_has_using_index) !=
      NO_ERROR)
    err_count++;

  if (vclass_list_has_using_index != NULL)
    db_objlist_free (vclass_list_has_using_index);

  db_objlist_free (classes);

  return err_count;

error:
  if (output_file != NULL)
    fclose (output_file);
  if (vclass_list_has_using_index != NULL)
    db_objlist_free (vclass_list_has_using_index);
  db_objlist_free (classes);

  return 1;
}


/*
 * emit_indexes - Emit SQL statements to define indexes for all attributes
 * that have them.
 *    return:
 *    classes(in):
 *    has_indexes(in):
 *    vclass_list_has_using_index(in):
 */
static int
emit_indexes (DB_OBJLIST * classes, int has_indexes,
	      DB_OBJLIST * vclass_list_has_using_index)
{
  char output_filename[PATH_MAX * 2];
  DB_OBJLIST *cl;
  FILE *fp;
  int total;

  if (output_dirname == NULL)
    output_dirname = ".";
  total =
    strlen (output_dirname) + strlen (output_prefix) + strlen (INDEX_SUFFIX) +
    8;
  if ((size_t) total > sizeof (output_filename))
    return 1;
  sprintf (output_filename, "%s/%s%s", output_dirname, output_prefix,
	   INDEX_SUFFIX);

  if (!has_indexes)
    {
      /*
       * don't have anything to emit but to avoid confusion with old
       * files that might be lying around, make sure that we delete
       * any existing index file
       */
      if ((fp = fopen_ex (output_filename, "r")) != NULL)
	{
	  fclose (fp);
	  if (unlink (output_filename))
	    {
	      (void) fprintf (stderr, "%s.\n\n", strerror (errno));
	      return 1;
	    }
	}
    }
  else
    {
      if ((fp = fopen_ex (output_filename, "w")) == NULL)
	{
	  (void) fprintf (stderr, "%s.\n\n", strerror (errno));
	  return 1;
	}
      output_file = fp;
      for (cl = classes; cl != NULL; cl = cl->next)
	{
	  /* if its some sort of vclass then it can't have indexes */
	  if (!db_is_vclass (cl->op))
	    {
	      emit_index_def (cl->op);
	    }
	}

      if (vclass_list_has_using_index != NULL)
	{
	  emit_query_specs_has_using_index (vclass_list_has_using_index);
	}

      fprintf (fp, "\n");
      fprintf (fp, "COMMIT WORK;\n");
      fclose (fp);
    }
  output_file = NULL;
  return 0;
}

/*
 * emit_schema -
 *    return:
 *    classes():
 *    do_auth():
 *    vclass_list_has_using_index():
 */
static int
emit_schema (DB_OBJLIST * classes, int do_auth,
	     DB_OBJLIST ** vclass_list_has_using_index)
{
  DB_OBJLIST *cl;
  bool is_vclass;
  const char *class_type;
  int has_indexes = 0;
  const char *name;
  int is_partitioned = 0;

  /*
   * First create all the classes
   */
  for (cl = classes; cl != NULL; cl = cl->next)
    {
      is_vclass = db_is_vclass (cl->op);

      name = db_get_class_name (cl->op);
      if (do_is_partitioned_subclass (&is_partitioned, name, NULL))
	{
	  continue;
	}
      if (is_vclass)
	{
	  fprintf (output_file, "CREATE VCLASS %s%s%s",
		   PRINT_IDENTIFIER (name));
	  if (sm_get_class_flag (cl->op, SM_CLASSFLAG_WITHCHECKOPTION))
	    fprintf (output_file, " WITH CHECK OPTION");
	  else if (sm_get_class_flag (cl->op, SM_CLASSFLAG_LOCALCHECKOPTION))
	    fprintf (output_file, " WITH LOCAL CHECK OPTION");
	  fprintf (output_file, ";\n");
	}
      else
	{
	  fprintf (output_file, "CREATE CLASS %s%s%s;\n",
		   PRINT_IDENTIFIER (name));
	}

      fprintf (output_file, "\n");
    }

  fprintf (output_file, "\n");

  /* emit super classes without resolutions for non-proxies */
  for (cl = classes; cl != NULL; cl = cl->next)
    {
      is_vclass = db_is_vclass (cl->op);
      class_type = (is_vclass) ? "VCLASS" : "CLASS";
      (void) emit_superclasses (cl->op, class_type);
    }

  fprintf (output_file, "\n\n");

  /*
   * Now fill out the class definitions for the non-proxy classes.
   */
  for (cl = classes; cl != NULL; cl = cl->next)
    {
      bool found = false;

      is_vclass = db_is_vclass (cl->op);
      name = db_get_class_name (cl->op);
      if (do_is_partitioned_subclass (&is_partitioned, name, NULL))
	{
	  continue;
	}

      class_type = (is_vclass) ? "VCLASS" : "CLASS";

      if (emit_all_attributes (cl->op, class_type, &has_indexes))
	{
	  found = true;
	}

      if (emit_methods (cl->op, class_type))
	{
	  found = true;
	}

      if (found)
	{
	  (void) fprintf (output_file, "\n");
	}
      if (is_partitioned)
	{
	  emit_partition_info (cl->op);
	}

      /*
       * change_owner method should be called after adding all columns.
       * If some column has auto_increment attribute, change_owner method
       * will change serial object's owner related to that attribute.
       */
      if (do_auth)
	{
	  emit_class_owner (output_file, cl->op);
	}
    }

  fprintf (output_file, "\n");

  /* emit super class resolutions for non-proxies */
  for (cl = classes; cl != NULL; cl = cl->next)
    {
      is_vclass = db_is_vclass (cl->op);
      class_type = (is_vclass) ? "VCLASS" : "CLASS";
      (void) emit_resolutions (cl->op, class_type);
    }

  /*
   * do query specs LAST after we're sure that all potentially
   * referenced classes have their full definitions.
   */
  *vclass_list_has_using_index = emit_query_specs (classes);

  /*
   * Dump authorizations.
   */
  if (do_auth)
    {
      fprintf (output_file, "\n");
      for (cl = classes; cl != NULL; cl = cl->next)
	{
	  name = db_get_class_name (cl->op);
	  if (do_is_partitioned_subclass (&is_partitioned, name, NULL))
	    {
	      continue;
	    }
	  au_export_grants (output_file, cl->op, delimited_id_flag);
	}
    }

  return has_indexes;

}


/*
 * has_vclass_domains -
 *    return: non-zero if the class has vclasses as attribute domains
 *    vclass(in): class MOP
 * Note:
 *    This is a helper function for emit_query_specs().
 *    Look through the attribute list of this class to see if there are
 *    any attributes defined with domsins that are other vclasses.  If so
 *    we have to adjust the way we emit the query spec lists for
 *    this class.
 */
static bool
has_vclass_domains (DB_OBJECT * vclass)
{
  /*
   * this doesn't seem to be enough, always return 1 so we make two full passes
   * on the query specs of all vclasses
   */
  return 1;
}


/*
 * emit_query_specs - Emit the object ids for a virtual class on an ldb.
 *    return:
 *    classes(in):
 */
static DB_OBJLIST *
emit_query_specs (DB_OBJLIST * classes)
{
  DB_QUERY_SPEC *specs, *s;
  DB_OBJLIST *cl;
  DB_OBJLIST *vclass_list_has_using_index = NULL;
  PARSER_CONTEXT *parser;
  PT_NODE **query_ptr;
  const char *name;
  const char *null_spec;
  bool has_using_index;
  bool change_vclass_spec;
  int i;

  /*
   * pass 1, emit NULL spec lists for vclasses that have attribute
   * domains which are other vclasses
   */
  for (cl = classes; cl != NULL; cl = cl->next)
    {
      if (db_is_vclass (cl->op))
	{
	  name = db_get_class_name (cl->op);
	  specs = db_get_query_specs (cl->op);
	  if (specs != NULL)
	    {
	      if (has_vclass_domains (cl->op))
		{
		  has_using_index = false;
		  for (s = specs;
		       s && has_using_index == false;
		       s = db_query_spec_next (s))
		    {
		      /*
		       * convert the query spec into one containing NULLs for
		       * each column
		       */
		      parser = parser_create_parser ();
		      if (parser == NULL)
			{
			  continue;
			}

		      query_ptr =
			parser_parse_string (parser,
					     db_query_spec_string (s));
		      if (query_ptr != NULL)
			{
			  parser_walk_tree (parser, *query_ptr,
					    pt_has_using_index_clause,
					    &has_using_index, NULL, NULL);

			  if (has_using_index == true)
			    {
			      /* all view specs should be emitted at index file */
			      ml_append (&vclass_list_has_using_index, cl->op,
					 NULL);
			    }
			}
		      parser_free_parser (parser);
		    }

		  if (has_using_index == false)
		    {
		      for (s = specs; s; s = db_query_spec_next (s))
			{
			  parser = parser_create_parser ();
			  if (parser == NULL)
			    {
			      continue;
			    }

			  query_ptr =
			    parser_parse_string (parser,
						 db_query_spec_string (s));
			  if (query_ptr != NULL)
			    {
			      null_spec =
				pt_print_query_spec_no_list (parser,
							     *query_ptr);

			      fprintf (output_file,
				       "ALTER VCLASS %s%s%s ADD QUERY %s ; \n",
				       PRINT_IDENTIFIER (name), null_spec);
			    }
			  parser_free_parser (parser);
			}
		    }
		}
	    }
	}
    }

  /*
   * pass 2, emit full spec lists
   */
  for (cl = classes; cl != NULL; cl = cl->next)
    {
      if (db_is_vclass (cl->op))
	{
	  name = db_get_class_name (cl->op);
	  specs = db_get_query_specs (cl->op);
	  if (specs != NULL)
	    {
	      if (ml_find (vclass_list_has_using_index, cl->op))
		continue;

	      change_vclass_spec = has_vclass_domains (cl->op);

	      for (s = specs, i = 1; s != NULL;
		   s = db_query_spec_next (s), i++)
		{
		  if (change_vclass_spec)
		    {		/* change the existing spec lists */
		      fprintf (output_file,
			       "ALTER VCLASS %s%s%s CHANGE QUERY %d %s ;\n",
			       PRINT_IDENTIFIER (name), i,
			       db_query_spec_string (s));
		    }
		  else
		    {		/* emit the usual statements */
		      fprintf (output_file,
			       "ALTER VCLASS %s%s%s ADD QUERY %s ;\n",
			       PRINT_IDENTIFIER (name),
			       db_query_spec_string (s));
		    }
		}
	    }
	}
    }

  return vclass_list_has_using_index;
}


/*
 * emit_query_specs_has_using_index - Emit the object ids for a virtual class
 * on an ldb.
 *    return:
 *    vclass_list_has_using_index():
 */
static int
emit_query_specs_has_using_index (DB_OBJLIST * vclass_list_has_using_index)
{
  DB_QUERY_SPEC *specs, *s;
  DB_OBJLIST *cl;
  PARSER_CONTEXT *parser;
  PT_NODE **query_ptr;
  const char *name;
  const char *null_spec;
  bool change_vclass_spec;
  int i;

  fprintf (output_file, "\n\n");

  /*
   * pass 1, emit NULL spec lists for vclasses that have attribute
   * domains which are other vclasses
   */

  for (cl = vclass_list_has_using_index; cl != NULL; cl = cl->next)
    {
      if (db_is_vclass (cl->op))
	{
	  name = db_get_class_name (cl->op);
	  specs = db_get_query_specs (cl->op);
	  if (specs != NULL)
	    {
	      if (has_vclass_domains (cl->op))
		{
		  for (s = specs; s != NULL; s = db_query_spec_next (s))
		    {
		      /*
		       * convert the query spec into one containing NULLs for
		       * each column
		       */
		      parser = parser_create_parser ();
		      if (parser == NULL)
			{
			  continue;
			}
		      query_ptr =
			parser_parse_string (parser,
					     db_query_spec_string (s));
		      if (query_ptr != NULL)
			{
			  null_spec =
			    pt_print_query_spec_no_list (parser, *query_ptr);
			  fprintf (output_file,
				   "ALTER VCLASS %s%s%s ADD QUERY %s ; \n",
				   PRINT_IDENTIFIER (name), null_spec);
			}
		      parser_free_parser (parser);
		    }
		}
	    }
	}
    }

  /* pass 2, emit full spec lists */
  for (cl = vclass_list_has_using_index; cl != NULL; cl = cl->next)
    {
      if (db_is_vclass (cl->op))
	{
	  name = db_get_class_name (cl->op);
	  specs = db_get_query_specs (cl->op);
	  if (specs != NULL)
	    {
	      change_vclass_spec = has_vclass_domains (cl->op);

	      for (s = specs, i = 1; s; s = db_query_spec_next (s), i++)
		{
		  if (change_vclass_spec)
		    {		/* change the existing spec lists */
		      fprintf (output_file,
			       "ALTER VCLASS %s%s%s CHANGE QUERY %d %s ;\n",
			       PRINT_IDENTIFIER (name), i,
			       db_query_spec_string (s));
		    }
		  else
		    {		/* emit the usual statements */
		      fprintf (output_file,
			       "ALTER VCLASS %s%s%s ADD QUERY %s ;\n",
			       PRINT_IDENTIFIER (name),
			       db_query_spec_string (s));
		    }
		}
	    }
	}
    }

  return NO_ERROR;
}


/*
 * emit_superclasses - emit queries for adding superclass for the class given
 *    return: true if there are any superclasses or conflict resolutions
 *    class(in): the class to emit the superclasses for
 *    class_type(in): CLASS or VCLASS
 */
static bool
emit_superclasses (DB_OBJECT * class_, const char *class_type)
{
  DB_OBJLIST *supers, *s;
  const char *name;

  supers = db_get_superclasses (class_);
  if (supers != NULL)
    {
      /* create class alter string */
      name = db_get_class_name (class_);
      if (do_is_partitioned_subclass (NULL, name, NULL))
	return (supers != NULL);

      fprintf (output_file, "ALTER %s %s%s%s ADD SUPERCLASS ",
	       class_type, PRINT_IDENTIFIER (name));

      for (s = supers; s != NULL; s = s->next)
	{
	  name = db_get_class_name (s->op);
	  if (s != supers)
	    fprintf (output_file, ", ");
	  fprintf (output_file, "%s%s%s", PRINT_IDENTIFIER (name));
	}

      fprintf (output_file, ";\n");

    }

  return (supers != NULL);
}


/*
 * emit_resolutions - emit queries for the resolutions (instance, shared and
 * class) for the class given
 *    return: true if any resolutions are emitted, false otherwise
 *    class(in): the class to emit resolutions for
 *    class_type(in): CLASS or VCLASS
 * Note:
 *     Calls emit_resolution_def for each resolution.  This function will
 *     only emit those resolutions defined in this class and NOT any of the
 *     inherited resolutions.
 */
static bool
emit_resolutions (DB_OBJECT * class_, const char *class_type)
{
  DB_RESOLUTION *resolution_list;
  bool return_value = false;
  const char *name;

  if ((resolution_list = db_get_resolutions (class_)) != NULL)
    {
      name = db_get_class_name (class_);
      fprintf (output_file, "ALTER %s %s%s%s INHERIT",
	       class_type, PRINT_IDENTIFIER (name));

      for (; resolution_list != NULL;
	   resolution_list = db_resolution_next (resolution_list))
	{
	  if (return_value == true)
	    fprintf (output_file, ",\n");
	  else
	    {
	      fprintf (output_file, "\n");
	      return_value = true;
	    }
	  emit_resolution_def (resolution_list,
			       (db_resolution_isclass (resolution_list) ?
				CLASS_RESOLUTION : INSTANCE_RESOLUTION));
	}

      fprintf (output_file, ";\n");
    }				/* if  */

  return (return_value);
}				/* emit_resolutions */


/*
 * emit_resolution_def - emit the resolution qualifier
 *    return: void
 *    resolution(in): the resolution
 *    qualifier(in): the qualifier for this resolution (instance or class)
 */
static void
emit_resolution_def (DB_RESOLUTION * resolution,
		     RESOLUTION_QUALIFIER qualifier)
{
  const char *name, *alias, *class_name;
  DB_OBJECT *class_;

  if ((class_ = db_resolution_class (resolution)) == NULL)
    return;
  if ((name = db_resolution_name (resolution)) == NULL)
    return;
  if ((class_name = db_get_class_name (class_)) == NULL)
    return;
  alias = db_resolution_alias (resolution);

  switch (qualifier)
    {
    case INSTANCE_RESOLUTION:
      {
	fprintf (output_file, "       %s%s%s OF %s%s%s",
		 PRINT_IDENTIFIER (name), PRINT_IDENTIFIER (class_name));
	break;
      }
    case CLASS_RESOLUTION:
      {
	fprintf (output_file, "CLASS  %s%s%s OF %s%s%s",
		 PRINT_IDENTIFIER (name), PRINT_IDENTIFIER (class_name));
	break;
      }
    }
  if (alias != NULL)
    fprintf (output_file, " AS %s%s%s", PRINT_IDENTIFIER (alias));

  class_ = NULL;
}


/*
 * emit_instance_attributes - emit quries for adding the attributes (instance,
 * shared and class) for the class given
 *    return: true if any locally defined attributes are found, false
 *            otherwise
 *    class(in): the class to emit the attributes for
 *    class_type(in):
 *    has_indexes(in):
 * Note:
 *    Calls emit_attribute_def for each attribute.  This function will only
 *    emit those attributes defined in this class and NOT any of the
 *    inherited attributes.
 *
 *    If its a proxy, only dump those attributes whose domains
 *    are other classes, the attributes with primitive type domains will
 *    have been dumped in the main class definition.
 */
static bool
emit_instance_attributes (DB_OBJECT * class_, const char *class_type,
			  int *has_indexes)
{
  DB_ATTRIBUTE *attribute_list, *first_attribute, *a;
  int unique_flag = 0;
  int reverse_unique_flag = 0;
  int index_flag = 0;
  DB_VALUE cur_val, started_val, min_val, max_val, inc_val, sr_name;
  const char *name, *start_with;

  attribute_list = db_get_attributes (class_);

  /* see if we have an index or unique defined on any attribute */
  for (a = attribute_list; a != NULL; a = db_attribute_next (a))
    {
      if (db_attribute_class (a) == class_)
	{
	  if (db_attribute_is_unique (a))
	    unique_flag = 1;
	  else if (db_attribute_is_reverse_unique (a))
	    reverse_unique_flag = 1;
	}

      if (db_attribute_is_indexed (a))
	index_flag = 1;

      if ((unique_flag || reverse_unique_flag) && index_flag)
	break;
    }

  /*
   * We call this function many times, so be careful not to clobber
   * (i.e. overwrite) the has_index parameter
   */
  if (has_indexes != NULL)
    *has_indexes |= index_flag;


  /* see if we have any locally defined components on either list */
  first_attribute = NULL;
  for (a = attribute_list; a != NULL && first_attribute == NULL;
       a = db_attribute_next (a))
    {
      if (db_attribute_class (a) == class_)
	first_attribute = a;
    }

  if (first_attribute != NULL)
    {
      name = db_get_class_name (class_);
      fprintf (output_file, "ALTER %s %s%s%s ADD ATTRIBUTE\n",
	       class_type, PRINT_IDENTIFIER (name));
      for (a = first_attribute; a != NULL; a = db_attribute_next (a))
	{
	  if (db_attribute_class (a) == class_)
	    {
	      if (a != first_attribute)
		fprintf (output_file, ",\n");
	      if (db_attribute_is_shared (a))
		emit_attribute_def (a, SHARED_ATTRIBUTE);
	      else
		emit_attribute_def (a, INSTANCE_ATTRIBUTE);
	    }
	}

      fprintf (output_file, ";\n");
      for (a = first_attribute; a != NULL; a = db_attribute_next (a))
	{
	  if (db_attribute_class (a) == class_)
	    {
	      /* update attribute's auto increment serial object */
	      if (a->auto_increment != NULL)
		{
		  int sr_error = NO_ERROR;

		  DB_MAKE_NULL (&sr_name);
		  DB_MAKE_NULL (&cur_val);
		  DB_MAKE_NULL (&started_val);
		  DB_MAKE_NULL (&min_val);
		  DB_MAKE_NULL (&max_val);
		  DB_MAKE_NULL (&inc_val);

		  sr_error = db_get (a->auto_increment, "name", &sr_name);
		  if (sr_error < 0)
		    continue;

		  sr_error =
		    db_get (a->auto_increment, "current_val", &cur_val);
		  if (sr_error < 0)
		    {
		      pr_clear_value (&sr_name);
		      continue;
		    }

		  sr_error =
		    db_get (a->auto_increment, "increment_val", &inc_val);
		  if (sr_error < 0)
		    {
		      pr_clear_value (&sr_name);
		      continue;
		    }

		  sr_error = db_get (a->auto_increment, "min_val", &min_val);
		  if (sr_error < 0)
		    {
		      pr_clear_value (&sr_name);
		      continue;
		    }

		  sr_error = db_get (a->auto_increment, "max_val", &max_val);
		  if (sr_error < 0)
		    {
		      pr_clear_value (&sr_name);
		      continue;
		    }

		  sr_error =
		    db_get (a->auto_increment, "started", &started_val);
		  if (sr_error < 0)
		    {
		      pr_clear_value (&sr_name);
		      continue;
		    }

		  if (DB_GET_INTEGER (&started_val) == 1)
		    {
		      DB_VALUE diff_val, answer_val;

		      sr_error =
			numeric_db_value_sub (&max_val, &cur_val, &diff_val);
		      if (sr_error != NO_ERROR)
			{
			  pr_clear_value (&sr_name);
			  continue;
			}
		      sr_error =
			numeric_db_value_compare (&inc_val, &diff_val,
						  &answer_val);
		      if (sr_error != NO_ERROR)
			{
			  pr_clear_value (&sr_name);
			  continue;
			}
		      /* auto_increment is always non-cyclic */
		      if (DB_GET_INTEGER (&answer_val) > 0)
			{
			  pr_clear_value (&sr_name);
			  continue;
			}

		      sr_error =
			numeric_db_value_add (&cur_val, &inc_val,
					      &answer_val);
		      if (sr_error != NO_ERROR)
			{
			  pr_clear_value (&sr_name);
			  continue;
			}

		      pr_clear_value (&cur_val);
		      cur_val = answer_val;
		    }

		  start_with = numeric_db_value_print (&cur_val);
		  if (start_with[0] == '\0')
		    {
		      start_with = "NULL";
		    }

		  fprintf (output_file,
			   "ALTER SERIAL %s%s%s START WITH %s;\n",
			   PRINT_IDENTIFIER (DB_PULL_STRING (&sr_name)),
			   start_with);

		  pr_clear_value (&sr_name);
		}
	    }
	}

      fprintf (output_file, "\n");
      if (unique_flag)
	{
	  name = db_get_class_name (class_);
	  fprintf (output_file, "\nALTER %s %s%s%s ADD ATTRIBUTE\n",
		   class_type, PRINT_IDENTIFIER (name));
	  emit_unique_def (class_);
	}
      if (reverse_unique_flag)
	{
	  emit_reverse_unique_def (class_);
	}
    }

  return (first_attribute != NULL);
}


/*
 * emit_class_attributes - emit ALTER statements for the class attributes
 *    return: non-zero if something was emitted
 *    class(in): class
 *    class_type(in): class type
 */
static bool
emit_class_attributes (DB_OBJECT * class_, const char *class_type)
{
  DB_ATTRIBUTE *class_attribute_list, *first_class_attribute, *a;
  const char *name;

  class_attribute_list = db_get_class_attributes (class_);
  first_class_attribute = NULL;
  for (a = class_attribute_list; a != NULL && first_class_attribute == NULL;
       a = db_attribute_next (a))
    {
      if (db_attribute_class (a) == class_)
	first_class_attribute = a;
    }

  if (first_class_attribute != NULL)
    {
      name = db_get_class_name (class_);
      fprintf (output_file, "ALTER %s %s%s%s ADD CLASS ATTRIBUTE \n",
	       class_type, PRINT_IDENTIFIER (name));
      for (a = first_class_attribute; a != NULL; a = db_attribute_next (a))
	{
	  if (db_attribute_class (a) == class_)
	    {
	      if (a != first_class_attribute)
		fprintf (output_file, ",\n");
	      emit_attribute_def (a, CLASS_ATTRIBUTE);
	    }
	}
      fprintf (output_file, ";\n");
    }

  return (first_class_attribute != NULL);
}


/*
 * emit_all_attributes - Emit both the instance and class attributes.
 *    return: non-zero if something was emmitted
 *    class(in): class to dump
 *    class_type(in): class type string
 *    has_indexes(in):
 */
static bool
emit_all_attributes (DB_OBJECT * class_, const char *class_type,
		     int *has_indexes)
{
  bool istatus, cstatus;

  istatus = emit_instance_attributes (class_, class_type, has_indexes);
  cstatus = emit_class_attributes (class_, class_type);

  return istatus || cstatus;
}


/*
 * emit_method_files - emit all methods files
 *    return: void
 *    class(in): class object
 */
static void
emit_method_files (DB_OBJECT * class_mop)
{
  DB_METHFILE *files, *f;
  bool printed_once = false;

  /* should clean this list ! */
  files = db_get_method_files (class_mop);

  if (files != NULL)
    {
      for (f = files; f != NULL; f = db_methfile_next (f))
	{
	  if (f->class_mop == class_mop)
	    {
	      if (printed_once == false)
		{
		  printed_once = true;
		  fprintf (output_file, "\nFILE");
		}
	      else
		{
		  (void) fprintf (output_file, ",\n");
		}
	      emit_methfile_def (f);
	    }
	}

      if (printed_once)
	{
	  fprintf (output_file, "\n");
	}
    }
}


/*
 * emit_methods - emit quries for adding the methods (instance, shared and
 * class) for the class given
 *    return: true if any locally defined methods are emitted, false
 *            otherwise
 *    class(in): the class to emit the method definitions for
 *    class_type(in): class type
 * Note:
 *    Calls emit_method_def for each method.  This function will only
 *    emit those methods defined in this class and NOT any of the
 *    inherited methods.
 */
static bool
emit_methods (DB_OBJECT * class_, const char *class_type)
{
  DB_METHOD *method_list, *class_method_list, *m;
  DB_METHOD *first_method, *first_class_method;
  const char *name;

  method_list = db_get_methods (class_);
  class_method_list = db_get_class_methods (class_);

  /* see if we have any locally defined components on either list */
  first_method = first_class_method = NULL;
  for (m = method_list; m != NULL && first_method == NULL;
       m = db_method_next (m))
    {
      if (db_method_class (m) == class_)
	first_method = m;
    }
  for (m = class_method_list; m != NULL && first_class_method == NULL;
       m = db_method_next (m))
    {
      if (db_method_class (m) == class_)
	first_class_method = m;
    }

  if (first_method != NULL)
    {
      name = db_get_class_name (class_);
      fprintf (output_file, "ALTER %s %s%s%s ADD METHOD\n",
	       class_type, PRINT_IDENTIFIER (name));
      for (m = first_method; m != NULL; m = db_method_next (m))
	{
	  if (db_method_class (m) == class_)
	    {
	      if (m != first_method)
		fprintf (output_file, ",\n");
	      emit_method_def (m, INSTANCE_METHOD);
	    }
	}
      fprintf (output_file, "\n");
      emit_method_files (class_);
      fprintf (output_file, ";\n");
    }

  /* eventually, this may merge with the statement above */
  if (first_class_method != NULL)
    {
      name = db_get_class_name (class_);
      fprintf (output_file, "ALTER %s %s%s%s ADD METHOD\n",
	       class_type, PRINT_IDENTIFIER (name));
      for (m = first_class_method; m != NULL; m = db_method_next (m))
	{
	  if (db_method_class (m) == class_)
	    {
	      if (m != first_class_method)
		fprintf (output_file, ",\n");
	      emit_method_def (m, CLASS_METHOD);
	    }
	}
      fprintf (output_file, "\n");
      if (first_method == NULL)
	emit_method_files (class_);
      fprintf (output_file, ";\n");
    }

  return ((first_method != NULL || first_class_method != NULL));
}

/*
 * ex_contains_object_reference - see if a value contains a reference
 * to a database object.
 *    return: non-zero if there is an object reference in the value
 *    value(in): value to examine
 * Note:
 *    This is used during the dumping of the default values for attribute
 *    definitions in the schema file.
 *    When we encounter object references, we don't include them in
 *    the schema file, they must be included in the object file.
 *    This is public so it can be used by unload_object.c to tell when to
 *    dump the values in the object file.
 */
static int
ex_contains_object_reference (DB_VALUE * value)
{
  DB_TYPE type;
  DB_SET *set;
  int error;
  DB_VALUE setval;
  int has_object, size, i;

  has_object = 0;
  if (value != NULL)
    {
      if (DB_VALUE_TYPE (value) == DB_TYPE_OBJECT)
	has_object = DB_GET_OBJECT (value) != NULL;

      else if (TP_IS_SET_TYPE (DB_VALUE_TYPE (value)))
	{
	  set = DB_GET_SET (value);
	  size = db_set_size (set);
	  type = db_set_type (set);
	  for (i = 0; i < size && !has_object; i++)
	    {
	      if (type == DB_TYPE_SEQUENCE)
		error = db_seq_get (set, i, &setval);
	      else
		error = db_set_get (set, i, &setval);

	      if (error)
		/*
		 * shouldn't happen, return 1 so we don't try to dump this
		 * value
		 */
		has_object = 1;
	      else
		has_object = ex_contains_object_reference (&setval);
	      db_value_clear (&setval);
	    }
	}
    }
  return has_object;
}

/*
 * emit_attribute_def - emit attribute definition
 *    return: void
 *    attribute(in): attribute descriptor
 *    qualifier(in): the qualifier for the attribute (default, class or shared)
 */
static void
emit_attribute_def (DB_ATTRIBUTE * attribute, ATTRIBUTE_QUALIFIER qualifier)
{
  DB_VALUE *default_value;
  const char *name;

  name = db_attribute_name (attribute);
  switch (qualifier)
    {
    case INSTANCE_ATTRIBUTE:
      {
	fprintf (output_file, "       %s%s%s ", PRINT_IDENTIFIER (name));
	break;
      }				/* case INSTANCE_ATTRIBUTE */
    case SHARED_ATTRIBUTE:
      {
	fprintf (output_file, "       %s%s%s ", PRINT_IDENTIFIER (name));
	break;
      }				/* case SHARED_ATTRIBUTE */
    case CLASS_ATTRIBUTE:
      {
	/*
	 * NOTE: The parser no longer recognizes a CLASS prefix for class
	 * attributes, this will have been encoded in the surrounding
	 * "ADD CLASS ATTRIBUTE" clause
	 */
	fprintf (output_file, "       %s%s%s ", PRINT_IDENTIFIER (name));
	break;
      }				/* case CLASS_ATTRIBUTE */
    }

  emit_domain_def (db_attribute_domain (attribute));

  if (emit_autoincrement_def (attribute) != NO_ERROR)
    {
      ;				/* just continue */
    }

  if (qualifier == SHARED_ATTRIBUTE)
    fprintf (output_file, " SHARED ");

  if (((default_value = db_attribute_default (attribute)) != NULL)
      && (!DB_IS_NULL (default_value)))
    {

      if (qualifier != SHARED_ATTRIBUTE)
	fprintf (output_file, " DEFAULT ");

      /* these are set during the object load phase */
      if (ex_contains_object_reference (default_value))
	fprintf (output_file, "NULL");
      else
	/* use the desc_ printer, need to have this in a better place */
	desc_value_fprint (output_file, default_value);
    }

  /* emit constraints */
  if (db_attribute_is_non_null (attribute))
    fprintf (output_file, " NOT NULL");

}



/*
 * emit_unique_def - emit the unique constraint definitions for this class
 *    return: void
 *    class(in): the class to emit the attributes for
 */
static void
emit_unique_def (DB_OBJECT * class_)
{
  DB_CONSTRAINT *constraint_list, *constraint;
  DB_ATTRIBUTE **atts, **att;
  bool has_inherited_atts;
  int num_printed = 0;
  const char *name;

  constraint_list = db_get_constraints (class_);
  if (constraint_list != NULL)
    {

      for (constraint = constraint_list;
	   constraint != NULL; constraint = db_constraint_next (constraint))
	{
	  if (db_constraint_type (constraint) == DB_CONSTRAINT_UNIQUE
	      || db_constraint_type (constraint) == DB_CONSTRAINT_PRIMARY_KEY)
	    {
	      atts = db_constraint_attributes (constraint);
	      has_inherited_atts = false;
	      for (att = atts; *att != NULL; att++)
		{
		  if (db_attribute_class (*att) != class_)
		    {
		      has_inherited_atts = true;
		      break;
		    }
		}
	      if (!has_inherited_atts)
		{
		  if (num_printed > 0)
		    {
		      (void) fprintf (output_file, ",\n");
		    }

		  if (constraint->type == SM_CONSTRAINT_PRIMARY_KEY)
		    (void) fprintf (output_file,
				    "       CONSTRAINT \"%s\" PRIMARY KEY(",
				    constraint->name);
		  else
		    (void) fprintf (output_file,
				    "       CONSTRAINT \"%s\" UNIQUE(",
				    constraint->name);

		  for (att = atts; *att != NULL; att++)
		    {
		      name = db_attribute_name (*att);
		      if (att != atts)
			fprintf (output_file, ", ");
		      fprintf (output_file, "%s%s%s",
			       PRINT_IDENTIFIER (name));
		    }
		  (void) fprintf (output_file, ")");

		  ++num_printed;
		}
	    }
	}
      (void) fprintf (output_file, ";\n");
    }
}

/*
 * emit_reverse_unique_def - emit a reverse unique index definition query part
 *    return: void
 *    class(in): class object
 */
static void
emit_reverse_unique_def (DB_OBJECT * class_)
{
  DB_CONSTRAINT *constraint_list, *constraint;
  DB_ATTRIBUTE **atts, **att;
  bool has_inherited_atts;
  const char *name;

  constraint_list = db_get_constraints (class_);
  if (constraint_list != NULL)
    {
      for (constraint = constraint_list;
	   constraint != NULL; constraint = db_constraint_next (constraint))
	{
	  if (db_constraint_type (constraint) == DB_CONSTRAINT_REVERSE_UNIQUE)
	    {
	      atts = db_constraint_attributes (constraint);
	      has_inherited_atts = false;
	      for (att = atts; *att != NULL; att++)
		{
		  if (db_attribute_class (*att) != class_)
		    {
		      has_inherited_atts = true;
		      break;
		    }
		}
	      if (!has_inherited_atts)
		{
		  name = db_get_class_name (class_);
		  fprintf (output_file,
			   "CREATE REVERSE UNIQUE INDEX %s%s%s on %s%s%s (",
			   PRINT_IDENTIFIER (constraint->name),
			   PRINT_IDENTIFIER (name));

		  for (att = atts; *att != NULL; att++)
		    {
		      name = db_attribute_name (*att);
		      if (att != atts)
			fprintf (output_file, ", ");
		      fprintf (output_file, "%s%s%s",
			       PRINT_IDENTIFIER (name));
		    }
		  fprintf (output_file, ");\n");
		}
	    }
	}
    }
}


/*
 * emit_index_def - emit the index constraint definitions for this class
 *    return: void
 *    class(in): the class to emit the indeses for
 */
static void
emit_index_def (DB_OBJECT * class_)
{
  DB_CONSTRAINT *constraint_list, *constraint;
  DB_CONSTRAINT_TYPE ctype;
  DB_ATTRIBUTE **atts, **att;
  const char *cls_name, *att_name;
  int partitioned_subclass = 0, au_save;
  SM_CLASS *smclass, *supclass = NULL;
  DB_VALUE classobj, pclass;
  const int *asc_desc;

  constraint_list = db_get_constraints (class_);
  if (constraint_list != NULL)
    {
      cls_name = db_get_class_name (class_);
      if (cls_name != NULL)
	{
	  partitioned_subclass =
	    do_is_partitioned_subclass (NULL, cls_name, NULL);
	}
      else
	{
	  cls_name = "";
	}
      if (partitioned_subclass)
	{
	  DB_MAKE_NULL (&classobj);
	  DB_MAKE_NULL (&pclass);
	  AU_DISABLE (au_save);
	  if (au_fetch_class (class_, &smclass, AU_FETCH_READ, AU_SELECT) ==
	      NO_ERROR && smclass->partition_of
	      && db_get (smclass->partition_of, PARTITION_ATT_CLASSOF,
			 &pclass) == NO_ERROR
	      && DB_VALUE_TYPE (&pclass) == DB_TYPE_OBJECT
	      && db_get (DB_PULL_OBJECT (&pclass), PARTITION_ATT_CLASSOF,
			 &classobj) == NO_ERROR)
	    {
	      if (DB_VALUE_TYPE (&classobj) == DB_TYPE_OBJECT
		  && au_fetch_class (DB_PULL_OBJECT (&classobj), &supclass,
				     AU_FETCH_READ, AU_SELECT) != NO_ERROR)
		{
		  supclass = NULL;
		}
	      pr_clear_value (&classobj);
	    }
	  AU_ENABLE (au_save);
	}

      for (constraint = constraint_list;
	   constraint != NULL; constraint = db_constraint_next (constraint))
	{
	  ctype = db_constraint_type (constraint);
	  if (ctype == DB_CONSTRAINT_INDEX
	      || ctype == DB_CONSTRAINT_REVERSE_INDEX)
	    {
	      if (supclass
		  && classobj_find_class_index (supclass,
						constraint->name) != NULL)
		{
		  continue;	/* same index skip */
		}
	      fprintf (output_file, "CREATE %sINDEX %s%s%s ON %s%s%s (",
		       (ctype ==
			DB_CONSTRAINT_REVERSE_INDEX) ? "REVERSE " : "",
		       PRINT_IDENTIFIER (constraint->name),
		       PRINT_IDENTIFIER (cls_name));

	      asc_desc = NULL;	/* init */
	      if (ctype == DB_CONSTRAINT_INDEX)
		{		/* is not reverse index */
		  /* need to get asc/desc info */
		  asc_desc = db_constraint_asc_desc (constraint);
		}

	      atts = db_constraint_attributes (constraint);
	      for (att = atts; *att != NULL; att++)
		{
		  att_name = db_attribute_name (*att);
		  if (att != atts)
		    fprintf (output_file, ", ");
		  fprintf (output_file, "%s%s%s",
			   PRINT_IDENTIFIER (att_name));
		  if (asc_desc)
		    {
		      if (*asc_desc == 1)
			{
			  fprintf (output_file, "%s", " DESC");
			}
		      asc_desc++;
		    }
		}
	      fprintf (output_file, ");\n");
	    }
	}
    }
}


/*
 * emit_domain_def - emit a domain defintion part
 *    return: void
 *    domains(in): domain list
 */
static void
emit_domain_def (DB_DOMAIN * domains)
{
  DB_TYPE type;
  PR_TYPE *prtype;
  DB_DOMAIN *domain;
  DB_OBJECT *class_;
  int precision;
  const char *name;

  for (domain = domains; domain != NULL; domain = db_domain_next (domain))
    {

      type = db_domain_type (domain);
      prtype = PR_TYPE_FROM_ID (type);
      if (prtype == NULL)
	{
	  continue;
	}

      if (type == DB_TYPE_OBJECT)
	{
	  class_ = db_domain_class (domain);
	  if (class_ == NULL)
	    fprintf (output_file, "%s", prtype->name);
	  else
	    {
	      name = db_get_class_name (class_);
	      fprintf (output_file, "%s%s%s", PRINT_IDENTIFIER (name));
	    }
	}
      else
	{
	  (void) fprintf (output_file, "%s", prtype->name);

	  switch (type)
	    {
	    case DB_TYPE_VARCHAR:
	    case DB_TYPE_CHAR:
	    case DB_TYPE_NCHAR:
	    case DB_TYPE_VARNCHAR:
	    case DB_TYPE_BIT:
	    case DB_TYPE_VARBIT:
	      precision = db_domain_precision (domain);
	      fprintf (output_file, "(%d)",
		       precision == TP_FLOATING_PRECISION_VALUE
		       ? DB_MAX_STRING_LENGTH : precision);
	      break;

	    case DB_TYPE_NUMERIC:
	      fprintf (output_file, "(%d,%d)",
		       db_domain_precision (domain),
		       db_domain_scale (domain));
	      break;

	    case DB_TYPE_SET:
	    case DB_TYPE_MULTISET:
	    case DB_TYPE_SEQUENCE:
	      fprintf (output_file, "(");
	      emit_domain_def (db_domain_set (domain));
	      fprintf (output_file, ")");
	      break;

	    default:
	      break;
	    }
	}
      if (db_domain_next (domain) != NULL)
	fprintf (output_file, ",");
    }
}

/*
 * emit_autoincrement_def - emit a auto-increment query part
 *    return: void
 *    attribute(in): attribute to add query part for
 */
static int
emit_autoincrement_def (DB_ATTRIBUTE * attribute)
{
  int error = NO_ERROR;
  DB_VALUE min_val, inc_val;

  if (attribute->auto_increment != NULL)
    {
      DB_MAKE_NULL (&min_val);
      DB_MAKE_NULL (&inc_val);

      error = db_get (attribute->auto_increment, "min_val", &min_val);
      if (error < 0)
	return error;

      error = db_get (attribute->auto_increment, "increment_val", &inc_val);
      if (error < 0)
	{
	  pr_clear_value (&min_val);
	  return error;
	}

      fprintf (output_file, " AUTO_INCREMENT(%s",
	       numeric_db_value_print (&min_val));
      fprintf (output_file, ", %s)", numeric_db_value_print (&inc_val));

      pr_clear_value (&min_val);
      pr_clear_value (&inc_val);
    }

  return error;
}


/*
 * emit_method_def - emit method definitnio query
 *    return: void
 *    method(in): method
 *    qualifier(in): the qualifier for this method (default or class)
 */
static void
emit_method_def (DB_METHOD * method, METHOD_QUALIFIER qualifier)
{
  int arg_count, i;
  DB_DOMAIN *method_return_domain;
  const char *method_function_name;
  const char *name;

  name = db_method_name (method);
  switch (qualifier)
    {
    case INSTANCE_METHOD:
      {
	fprintf (output_file, "       %s%s%s(", PRINT_IDENTIFIER (name));
	break;
      }				/* case INSTANCE_METHOD */
    case CLASS_METHOD:
      {
	fprintf (output_file, "CLASS  %s%s%s(", PRINT_IDENTIFIER (name));
	break;
      }				/* case CLASS_METHOD */
    }

  /*
   * Emit argument type list
   */
  arg_count = db_method_arg_count (method);
  /* recall that arguments are numbered from 1 */
  for (i = 1; i < arg_count; i++)
    {
      emit_domain_def (db_method_arg_domain (method, i));
      fprintf (output_file, ", ");
    }
  if (arg_count)
    emit_domain_def (db_method_arg_domain (method, i));

  fprintf (output_file, ") ");

  /*
   * Emit method return domain
   */
  if ((method_return_domain = db_method_return_domain (method)) != NULL)
    {
      emit_domain_def (db_method_return_domain (method));
    }

  /*
   * Emit method function implementation
   */
  if ((method_function_name = db_method_function (method)) != NULL)
    {
      name = db_method_function (method);
      fprintf (output_file, " FUNCTION %s%s%s", PRINT_IDENTIFIER (name));
    }
}


/*
 * emit_methfile_def - emit method file name
 *    return: nothing
 *    methfile(in): method file
 */
static void
emit_methfile_def (DB_METHFILE * methfile)
{
  (void) fprintf (output_file, "       '%s'", db_methfile_name (methfile));
}

/*
 * need_quotes - check for quotes needed
 *    return: true if needed, false otherwise
 *    identifier(in): identifier to check for quote-needness
 */
bool
need_quotes (const char *identifier)
{
  return (delimited_id_flag || pt_is_keyword (identifier)
	  || !lang_check_identifier (identifier, strlen (identifier)));
}

/*
 * emit_partition_parts - emit PARTITION query part
 *    return: void
 *    parts(in): part MOP
 *    partcnt(in): relative position of 'parts'
 */
static void
emit_partition_parts (MOP parts, int partcnt)
{
  DB_VALUE ptype, pname, pval, ele;
  int save, setsize, i1;

  if (parts != NULL)
    {
      DB_MAKE_NULL (&ptype);
      DB_MAKE_NULL (&pname);
      DB_MAKE_NULL (&pval);
      if (partcnt > 0)
	fprintf (output_file, ",\n ");

      AU_DISABLE (save);

      if (db_get (parts, PARTITION_ATT_PTYPE, &ptype) == NO_ERROR
	  && db_get (parts, PARTITION_ATT_PNAME, &pname) == NO_ERROR
	  && db_get (parts, PARTITION_ATT_PVALUES, &pval) == NO_ERROR)
	{

	  fprintf (output_file, "PARTITION %s%s%s ",
		   PRINT_IDENTIFIER (DB_PULL_STRING (&pname)));

	  switch (DB_GET_INT (&ptype))
	    {
	    case PT_PARTITION_RANGE:
	      fprintf (output_file, " VALUES LESS THAN ");
	      if (!set_get_element (pval.data.set, 1, &ele))
		{		/* 0:MIN, 1:MAX */
		  if (DB_IS_NULL (&ele))
		    {
		      fprintf (output_file, "MAXVALUE");
		    }
		  else
		    {
		      fprintf (output_file, "(");
		      desc_value_fprint (output_file, &ele);
		      fprintf (output_file, ")");
		    }
		}
	      break;
	    case PT_PARTITION_LIST:
	      fprintf (output_file, " VALUES IN (");
	      setsize = set_size (pval.data.set);
	      for (i1 = 0; i1 < setsize; i1++)
		{
		  if (i1 > 0)
		    fprintf (output_file, ", ");
		  if (!set_get_element (pval.data.set, i1, &ele))
		    desc_value_fprint (output_file, &ele);
		}
	      fprintf (output_file, ")");
	      break;
	    }
	}
      else
	{
	  /* FIXME */
	}

      AU_ENABLE (save);
      pr_clear_value (&ptype);
      pr_clear_value (&pname);
      pr_clear_value (&pval);
    }
}

/*
 * emit_partition_info - emit PARTINTION query for a class
 *    return: void
 *    clsobj(in): class object
 */
static void
emit_partition_info (MOP clsobj)
{
  DB_VALUE ptype, ele, pexpr, pattr;
  int save, partcnt = 0;
  char *ptr, *ptr2;
  const char *name;
  SM_CLASS *class_, *subclass;
  DB_OBJLIST *user;
  char *pexpr_str = NULL;

  if (clsobj != NULL)
    {
      DB_MAKE_NULL (&ptype);
      DB_MAKE_NULL (&pexpr);
      DB_MAKE_NULL (&pattr);

      AU_DISABLE (save);

      name = db_get_class_name (clsobj);
      if (au_fetch_class (clsobj, &class_, AU_FETCH_READ, AU_SELECT) !=
	  NO_ERROR)
	goto end;

      fprintf (output_file, "\nALTER CLASS %s%s%s ", PRINT_IDENTIFIER (name));
      fprintf (output_file, "\nPARTITION BY ");

      if (db_get (class_->partition_of, PARTITION_ATT_PTYPE, &ptype) ==
	  NO_ERROR
	  && db_get (class_->partition_of, PARTITION_ATT_PEXPR,
		     &pexpr) == NO_ERROR && !DB_IS_NULL (&pexpr)
	  && (pexpr_str = DB_GET_STRING (&pexpr))
	  && db_get (class_->partition_of, PARTITION_ATT_PVALUES,
		     &pattr) == NO_ERROR)
	{
	  switch (DB_GET_INT (&ptype))
	    {
	    case PT_PARTITION_HASH:
	      fprintf (output_file, "HASH ( ");
	      break;
	    case PT_PARTITION_RANGE:
	      fprintf (output_file, "RANGE ( ");
	      break;
	    case PT_PARTITION_LIST:
	      fprintf (output_file, "LIST ( ");
	      break;
	    }
	  ptr = strstr (pexpr_str, "SELECT ");
	  if (ptr)
	    {
	      ptr2 = strstr (ptr + 7, " FROM ");
	      if (ptr2)
		{
		  *ptr2 = 0;
		  fprintf (output_file, ptr + 7);
		  fprintf (output_file, " ) \n ");
		}
	    }
	  if (DB_GET_INT (&ptype) == PT_PARTITION_HASH)
	    {
	      if (!set_get_element (pattr.data.set, 1, &ele))
		{
		  fprintf (output_file, " PARTITIONS %d", db_get_int (&ele));
		}
	    }
	  else
	    {
	      fprintf (output_file, " ( ");
	      for (user = class_->users; user != NULL; user = user->next)
		{
		  if (au_fetch_class
		      (user->op, &subclass, AU_FETCH_READ,
		       AU_SELECT) == NO_ERROR)
		    {
		      if (subclass->partition_of)
			{
			  emit_partition_parts (subclass->partition_of,
						partcnt);
			  partcnt++;
			}
		    }
		}
	      fprintf (output_file, " ) ");
	    }
	}
      else
	{
	  /* FIXME */
	}
      fprintf (output_file, ";\n");

    end:
      AU_ENABLE (save);

      pr_clear_value (&ptype);
      pr_clear_value (&pexpr);
      pr_clear_value (&pattr);
    }
}

/*
 * emit_stored_procedure_args - emit stored procedure arguments
 *    return: 0 if success, error count otherwise
 *    arg_cnt(in): argument count
 *    arg_set(in): set containg argument DB_VALUE
 */
static int
emit_stored_procedure_args (int arg_cnt, DB_SET * arg_set)
{
  MOP arg;
  DB_VALUE arg_val, arg_name_val, arg_mode_val, arg_type_val;
  int arg_mode, arg_type, i, save;
  int err;
  int err_count = 0;

  AU_DISABLE (save);

  for (i = 0; i < arg_cnt; i++)
    {
      if ((err = set_get_element (arg_set, i, &arg_val)) != NO_ERROR)
	{
	  err_count++;
	  continue;
	}
      arg = DB_GET_OBJECT (&arg_val);

      if ((err = db_get (arg, SP_ATTR_ARG_NAME, &arg_name_val)) != NO_ERROR
	  || (err = db_get (arg, SP_ATTR_MODE, &arg_mode_val)) != NO_ERROR
	  || (err = db_get (arg, SP_ATTR_DATA_TYPE,
			    &arg_type_val)) != NO_ERROR)
	{
	  err_count++;
	  continue;
	}
      fprintf (output_file, "%s%s%s ",
	       PRINT_IDENTIFIER (DB_PULL_STRING (&arg_name_val)));

      arg_mode = DB_GET_INT (&arg_mode_val);
      fprintf (output_file, "%s ", arg_mode == SP_MODE_IN ? "IN" :
	       arg_mode == SP_MODE_OUT ? "OUT" : "INOUT");

      arg_type = DB_GET_INT (&arg_type_val);

      if (arg_type == DB_TYPE_RESULTSET)
	{
	  fprintf (output_file, "CURSOR");
	}
      else
	{
	  fprintf (output_file, "%s", db_get_type_name ((DB_TYPE) arg_type));
	}

      if (i < arg_cnt - 1)
	{
	  fprintf (output_file, ", ");
	}

      pr_clear_value (&arg_val);
    }

  AU_ENABLE (save);
  return err_count;
}

/*
 * emit_stored_procedure - emit stored procedure
 *    return: void
 */
static int
emit_stored_procedure (void)
{
  MOP cls, obj, owner;
  DB_OBJLIST *sp_list = NULL, *cur_sp;
  DB_VALUE sp_name_val, sp_type_val, arg_cnt_val, args_val, rtn_type_val,
    method_val;
  DB_VALUE owner_val, owner_name_val;
  int sp_type, rtn_type, arg_cnt, save;
  DB_SET *arg_set;
  int err;
  int err_count = 0;

  AU_DISABLE (save);

  cls = db_find_class (SP_CLASS_NAME);
  if (cls == NULL)
    {
      AU_ENABLE (save);
      return 1;
    }

  sp_list = db_get_all_objects (cls);
  for (cur_sp = sp_list; cur_sp; cur_sp = cur_sp->next)
    {
      obj = cur_sp->op;

      if ((err = db_get (obj, SP_ATTR_SP_TYPE, &sp_type_val)) != NO_ERROR
	  || (err = db_get (obj, SP_ATTR_NAME, &sp_name_val)) != NO_ERROR
	  || (err = db_get (obj, SP_ATTR_ARG_COUNT, &arg_cnt_val)) != NO_ERROR
	  || (err = db_get (obj, SP_ATTR_ARGS, &args_val)) != NO_ERROR
	  || ((err = db_get (obj, SP_ATTR_RETURN_TYPE, &rtn_type_val))
	      != NO_ERROR)
	  || (err = db_get (obj, SP_ATTR_TARGET, &method_val)) != NO_ERROR
	  || (err = db_get (obj, SP_ATTR_OWNER, &owner_val)) != NO_ERROR)
	{
	  err_count++;
	  continue;
	}

      sp_type = DB_GET_INT (&sp_type_val);
      fprintf (output_file, "\nCREATE %s",
	       sp_type == SP_TYPE_PROCEDURE ? "PROCEDURE" : "FUNCTION");

      fprintf (output_file, " %s%s%s (",
	       PRINT_IDENTIFIER (DB_PULL_STRING (&sp_name_val)));

      arg_cnt = DB_GET_INT (&arg_cnt_val);
      arg_set = DB_GET_SET (&args_val);
      if (emit_stored_procedure_args (arg_cnt, arg_set) > 0)
	{
	  err_count++;
	  fprintf (output_file, ";\n");
	  continue;
	}
      fprintf (output_file, ") ");

      if (sp_type == SP_TYPE_FUNCTION)
	{
	  rtn_type = DB_GET_INT (&rtn_type_val);

	  if (rtn_type == DB_TYPE_RESULTSET)
	    {
	      fprintf (output_file, "RETURN CURSOR ");
	    }
	  else
	    {
	      fprintf (output_file, "RETURN %s ",
		       db_get_type_name ((DB_TYPE) rtn_type));
	    }
	}

      fprintf (output_file, "AS LANGUAGE JAVA NAME '%s';\n",
	       DB_GET_STRING (&method_val));

      owner = DB_GET_OBJECT (&owner_val);
      if ((err = db_get (owner, "name", &owner_name_val)) != NO_ERROR)
	{
	  err_count++;
	  continue;
	}

      fprintf (output_file,
	       "call change_sp_owner('%s', '%s') on class db_root;\n",
	       DB_GET_STRING (&sp_name_val), DB_GET_STRING (&owner_name_val));

      db_value_clear (&owner_name_val);
    }

  db_objlist_free (sp_list);
  AU_ENABLE (save);

  return err_count;
}

/*
 * emit_foreign_key - emit foreign key
 *    return: NO_ERROR if successful, error code otherwise
 *    classes(in): MOP list for dump foreign key
 */
static int
emit_foreign_key (DB_OBJLIST * classes)
{
  DB_OBJLIST *cl;
  DB_CONSTRAINT *constraint_list, *constraint;
  DB_ATTRIBUTE **atts, **att;
  bool has_inherited_atts;
  const char *cls_name, *att_name;
  MOP ref_clsop;

  for (cl = classes; cl != NULL; cl = cl->next)
    {
      constraint_list = db_get_constraints (cl->op);
      cls_name = db_get_class_name (cl->op);

      for (constraint = constraint_list;
	   constraint != NULL; constraint = db_constraint_next (constraint))
	{
	  if (db_constraint_type (constraint) != DB_CONSTRAINT_FOREIGN_KEY)
	    continue;

	  atts = db_constraint_attributes (constraint);
	  has_inherited_atts = false;
	  for (att = atts; *att != NULL; att++)
	    {
	      if (db_attribute_class (*att) != cl->op)
		{
		  has_inherited_atts = true;
		  break;
		}
	    }
	  if (has_inherited_atts)
	    continue;

	  (void) fprintf (output_file, "ALTER CLASS \"%s\" ADD", cls_name);

	  (void) fprintf (output_file, " CONSTRAINT \"%s\" FOREIGN KEY(",
			  constraint->name);

	  for (att = atts; *att != NULL; att++)
	    {
	      att_name = db_attribute_name (*att);
	      if (att != atts)
		fprintf (output_file, ", ");
	      fprintf (output_file, "%s%s%s", PRINT_IDENTIFIER (att_name));
	    }
	  (void) fprintf (output_file, ")");

	  ref_clsop = ws_mop (&(constraint->fk_info->ref_class_oid), NULL);
	  fprintf (output_file, " REFERENCES %s%s%s ",
		   PRINT_IDENTIFIER (db_get_class_name (ref_clsop)));
	  fprintf (output_file, "ON DELETE %s ",
		   classobj_describe_foreign_key_action (constraint->fk_info->
							 delete_action));
	  fprintf (output_file, "ON UPDATE %s ",
		   classobj_describe_foreign_key_action (constraint->fk_info->
							 update_action));
	  if (constraint->fk_info->cache_attr)
	    {
	      fprintf (output_file, "ON CACHE OBJECT %s%s%s",
		       PRINT_IDENTIFIER (constraint->fk_info->cache_attr));
	    }

	  (void) fprintf (output_file, ";\n\n");
	}
    }

  return NO_ERROR;
}
