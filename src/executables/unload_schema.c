/*
 * Copyright 2008 Search Solution Corporation
 * Copyright 2016 CUBRID Corporation
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
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
#include <assert.h>

#include "db.h"
#include "extract_schema.hpp"
#include "authenticate.h"
#include "schema_manager.h"
#include "trigger_description.hpp"
#include "load_object.h"
#include "object_primitive.h"
#include "parser.h"
#include "printer.hpp"
#include "message_catalog.h"
#include "utility.h"
#include "unloaddb.h"
#include "execute_schema.h"
#include "parser.h"
#include "set_object.h"
#include "jsp_cl.h"
#include "class_object.h"
#include "object_print.h"
#include "dbtype.h"
#include "tde.h"

#define CLASS_NAME_MAX 80

#define SCHEMA_NAME	  "_schema"
#define TRIGGER_NAME	  "_trigger"
#define INDEX_NAME	  "_indexes"
#define SCHEMA_INFO        "_schema_info"

#define CLASS_SUFFIX          "_class"
#define FK_SUFFIX             "_fk"
#define GRANT_SUFFIX          "_grant"
#define PK_SUFFIX             "_pk"
#define PROCEDURE_SUFFIX      "_procedure"
#define SERIAL_SUFFIX         "_serial"
#define SERVER_SUFFIX         "_server"
#define SYNONYM_SUFFIX        "_synonym"
#define USER_SUFFIX           "_user"
#define VCLASS_SUFFIX         "_vclass"


#define EX_ERROR_CHECK(c,d,m)                                 \
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
            fprintf(stderr, "%s: Unknown database error occurs but may not be database error.\n\n", \
                            exec_name);                       \
          }                                                   \
          goto error;                                         \
        }                                                     \
      }                                                       \
    }                                                         \
  } while (0)

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
  SERIAL_UNIQUE_NAME,
  SERIAL_NAME,
  SERIAL_OWNER_NAME,
  SERIAL_CURRENT_VAL,
  SERIAL_INCREMENT_VAL,
  SERIAL_MAX_VAL,
  SERIAL_MIN_VAL,
  SERIAL_CYCLIC,
  SERIAL_STARTED,
  SERIAL_CACHED_NUM,
  SERIAL_COMMENT,

  SERIAL_VALUE_INDEX_MAX
} SERIAL_VALUE_INDEX;

typedef enum
{
  SYNONYM_NAME,
  SYNONYM_OWNER,
  SYNONYM_OWNER_NAME,
  SYNONYM_IS_PUBLIC,
  SYNONYM_TARGET_NAME,
  SYNONYM_TARGET_OWNER_NAME,
  SYNONYM_COMMENT,

  SYNONYM_VALUE_INDEX_MAX
} SYNONYM_VALUE_INDEX;

typedef enum
{
  EXTRACT_CLASS_ALL,
  EXTRACT_CLASS,
  EXTRACT_VCLASS
} EXTRACT_CLASS_TYPE;

static void filter_system_classes (DB_OBJLIST ** class_list);
static void filter_unrequired_classes (DB_OBJLIST ** class_list);
static int is_dependent_class (DB_OBJECT * class_, DB_OBJLIST * unordered, DB_OBJLIST * ordered);
static int check_domain_dependencies (DB_DOMAIN * domain, DB_OBJECT * this_class, DB_OBJLIST * unordered,
				      DB_OBJLIST * ordered);
static int has_dependencies (DB_OBJECT * class_, DB_OBJLIST * unordered, DB_OBJLIST * ordered, int conservative);
static int order_classes (DB_OBJLIST ** class_list, DB_OBJLIST ** order_list, int conservative);
static void emit_cycle_warning (print_output & output_ctx);
static void force_one_class (print_output & output_ctx, DB_OBJLIST ** class_list, DB_OBJLIST ** order_list);
static DB_OBJLIST *get_ordered_classes (print_output & output_ctx, MOP * class_table);
static void emit_class_owner (extract_context & ctxt, print_output & output_ctx, MOP class_);
static int export_serial (extract_context & ctxt, print_output & output_ctx);
static int emit_indexes (extract_context & ctxt, print_output & output_ctx, DB_OBJLIST * classes, int has_indexes,
			 DB_OBJLIST * vclass_list_has_using_index);

static void emit_schema (extract_context & ctxt, print_output & output_ctx, EXTRACT_CLASS_TYPE extract_class);
static bool has_vclass_domains (DB_OBJECT * vclass);
static DB_OBJLIST *emit_query_specs (extract_context & ctxt, print_output & output_ctx, DB_OBJLIST * classes);
static int emit_query_specs_has_using_index (extract_context & ctxt, print_output & output_ctx,
					     DB_OBJLIST * vclass_list_has_using_index);
static bool emit_superclasses (extract_context & ctxt, print_output & output_ctx, DB_OBJECT * class_,
			       const char *class_type);
static bool emit_resolutions (extract_context & ctxt, print_output & output_ctx, DB_OBJECT * class_,
			      const char *class_type);
static void emit_resolution_def (extract_context & ctxt, print_output & output_ctx, DB_RESOLUTION * resolution,
				 RESOLUTION_QUALIFIER qualifier);
static bool emit_instance_attributes (extract_context & ctxt, print_output & output_ctx, DB_OBJECT * class_,
				      const char *class_type, int *has_indexes, EMIT_STORAGE_ORDER storage_order);
static bool emit_class_attributes (extract_context & ctxt, print_output & output_ctx, DB_OBJECT * class_,
				   const char *class_type);
static bool emit_all_attributes (extract_context & ctxt, print_output & output_ctx, DB_OBJECT * class_,
				 const char *class_type, int *has_indexes, EMIT_STORAGE_ORDER storage_order);
static bool emit_class_meta (print_output & output_ctx, DB_OBJECT * table);
static void emit_method_files (print_output & output_ctx, DB_OBJECT * class_);
static bool emit_methods (extract_context & ctxt, print_output & output_ctx, DB_OBJECT * class_,
			  const char *class_type);
static int ex_contains_object_reference (DB_VALUE * value);
static void emit_attribute_def (extract_context & ctxt, print_output & output_ctx, DB_ATTRIBUTE * attribute,
				ATTRIBUTE_QUALIFIER qualifier);
static void emit_unique_def (extract_context & ctxt, print_output & output_ctx, DB_OBJECT * class_,
			     const char *class_type);
static void emit_primary_key_def (extract_context & ctxt, print_output & output_ctx, DB_OBJECT * class_,
				  const char *class_type);
static void emit_primary_and_unique_def (extract_context & ctxt, print_output & output_ctx, DB_OBJECT * class_,
					 const char *class_type);
static void emit_reverse_unique_def (extract_context & ctxt, print_output & output_ctx, DB_OBJECT * class_);
static void emit_index_def (extract_context & ctxt, print_output & output_ctx, DB_OBJECT * class_);
static void emit_domain_def (extract_context & ctxt, print_output & output_ctx, DB_DOMAIN * domains);
static int emit_autoincrement_def (print_output & output_ctx, DB_ATTRIBUTE * attribute);
static void emit_method_def (extract_context & ctxt, print_output & output_ctx, DB_METHOD * method,
			     METHOD_QUALIFIER qualifier);
static void emit_methfile_def (print_output & output_ctx, DB_METHFILE * methfile);
static void emit_partition_parts (print_output & output_ctx, SM_PARTITION * partition_info, int partcnt);
static void emit_partition_info (extract_context & ctxt, print_output & output_ctx, MOP clsobj);
static int emit_stored_procedure_args (print_output & output_ctx, int arg_cnt, DB_SET * arg_set);
static int emit_stored_procedure (extract_context & ctxt, print_output & output_ctx);
static int emit_foreign_key (extract_context & ctxt, print_output & output_ctx, DB_OBJLIST * classes);
static int emit_grant (extract_context & ctxt, print_output & output_ctx, DB_OBJLIST * classes);
static int create_filename (const char *output_dirname, const char *output_prefix, const char *suffix,
			    char *output_filename_p, const size_t filename_size);
static int create_filename (const char *output_dirname, const char *output_prefix, const char *infix,
			    const char *suffix, char *output_filename_p, const size_t filename_size);
static int export_server (extract_context & ctxt, print_output & output_ctx);
static int extract_all_schema_file (extract_context & ctxt, const char *output_filename);
static int extract_split_schema_files (extract_context & ctxt);
static int extract_schema (extract_context & ctxt, print_output & schema_output_ctx);
static int extract_user (extract_context & ctxt);
static int extract_serial (extract_context & ctxt);
static int extract_synonym (extract_context & ctxt);
static int extract_procedure (extract_context & ctxt);
static int extract_server (extract_context & ctxt);
static int extract_class (extract_context & ctxt);
static int extract_vclass (extract_context & ctxt);
static int extract_pk (extract_context & ctxt);
static int extract_fk (extract_context & ctxt);
static int extract_grant (extract_context & ctxt);
static int get_classes (extract_context & ctxt, print_output & output_ctx);
static void filter_user_classes (DB_OBJLIST ** class_list, const char *user_name);
static void emit_primary_key (extract_context & ctxt, print_output & output_ctx, DB_OBJLIST * classes);
static int create_schema_info (extract_context & ctxt);
static int create_filename_schema_info (const char *output_dirname, const char *output_prefix, char *output_filename_p,
					const size_t filename_size);

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
	{
	  prev = cl;
	}
      else
	{
	  if (prev == NULL)
	    {
	      *class_list = next;
	    }
	  else
	    {
	      prev->next = next;
	    }
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
	{
	  prev = cl;
	}
      else
	{
	  if (prev == NULL)
	    {
	      *class_list = next;
	    }
	  else
	    {
	      prev->next = next;
	    }
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
is_dependent_class (DB_OBJECT * class_, DB_OBJLIST * unordered, DB_OBJLIST * ordered)
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
check_domain_dependencies (DB_DOMAIN * domain, DB_OBJECT * this_class, DB_OBJLIST * unordered, DB_OBJLIST * ordered)
{
  DB_DOMAIN *d, *setdomain;
  DB_OBJECT *class_;
  int dependencies;

  dependencies = 0;
  for (d = domain; d != NULL && !dependencies; d = db_domain_next (d))
    {
      setdomain = db_domain_set (d);
      if (setdomain != NULL)
	{
	  dependencies = check_domain_dependencies (setdomain, this_class, unordered, ordered);
	}
      else
	{
	  class_ = db_domain_class (d);
	  if (class_ != NULL && class_ != this_class)
	    {
	      dependencies = is_dependent_class (class_, unordered, ordered);
	    }
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
has_dependencies (DB_OBJECT * mop, DB_OBJLIST * unordered, DB_OBJLIST * ordered, int conservative)
{
  DB_OBJLIST *supers, *su;
  DB_ATTRIBUTE *att;
  DB_DOMAIN *domain;
  int dependencies;

  dependencies = 0;

  supers = db_get_superclasses (mop);
  for (su = supers; su != NULL && !dependencies; su = su->next)
    {
      dependencies = is_dependent_class (su->op, unordered, ordered);
    }

  /*
   * if we're doing a conservative dependency check, look at the domains
   * of each attribute.
   */
  if (!dependencies && conservative)
    {
      for (att = db_get_attributes (mop); att != NULL && !dependencies; att = db_attribute_next (att))
	{
	  domain = db_attribute_domain (att);
	  dependencies = check_domain_dependencies (domain, mop, unordered, ordered);
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
 *    It can also indicate a bug in the dependency algorithm.
 */
static int
order_classes (DB_OBJLIST ** class_list, DB_OBJLIST ** order_list, int conservative)
{
  DB_OBJLIST *cl, *o, *next, *prev, *last;
  int add_count;

  add_count = 0;

  for (cl = *class_list, prev = NULL, next = NULL; cl != NULL; cl = next)
    {
      next = cl->next;

      if (has_dependencies (cl->op, *class_list, *order_list, conservative))
	{
	  prev = cl;
	}
      else
	{
	  /* no dependencies, move it to the other list */
	  if (prev == NULL)
	    {
	      *class_list = next;
	    }
	  else
	    {
	      prev->next = next;
	    }

	  /* append it on the order list */
	  cl->next = NULL;
	  for (o = *order_list, last = NULL; o != NULL; o = o->next)
	    {
	      last = o;
	    }

	  if (last == NULL)
	    {
	      *order_list = cl;
	    }
	  else
	    {
	      last->next = cl;
	    }

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
emit_cycle_warning (print_output & output_ctx)
{
  output_ctx ("/* Error calculating class dependency order.\n");
  output_ctx ("   This indicates one of the following:\n");
  output_ctx ("     - bug in dependency algorithm\n");
  output_ctx ("     - cycle in class hierarchy\n");
  output_ctx ("     - cycle in proxy attribute used as object ids\n");
  output_ctx ("   The next class may not be in the proper definition order.\n");
  output_ctx ("   Hand editing of the schema may be required before loading.\n");
  output_ctx ("   */\n");
}


/*
 * force_one_class - pick the top class off the 'class_list' and append to
 * the 'order_list'
 *    return: void
 *    class_list(out): remaining unordered classes
 *    order_list(out): ordered class list
 * Note:
 *    This is called when a depencency cycle is detected that we can't
 *    figure out.  So we at least get the full schema dumped to the
 *    output file, pick the top class off the list and hopefully this
 *    will break the cycle.
 *    The user will then have to go in and hand edit the output file
 *    so that it can be loaded.
 */
static void
force_one_class (print_output & output_ctx, DB_OBJLIST ** class_list, DB_OBJLIST ** order_list)
{
  DB_OBJLIST *cl, *o, *last;

  emit_cycle_warning (output_ctx);

  cl = *class_list;
  *class_list = cl->next;

  cl->next = NULL;
  for (o = *order_list, last = NULL; o != NULL; o = o->next)
    {
      last = o;
    }

  if (last == NULL)
    {
      *order_list = cl;
    }
  else
    {
      last->next = cl;
    }
}


/*
 * get_ordered_classes - takes a list of classes to dump and returns a list
 * of classes ordered according to their definition dependencies.
 *    return: ordered class list
 *    class_table(in): classes to dump
 */
static DB_OBJLIST *
get_ordered_classes (print_output & output_ctx, MOP * class_table)
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
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_NODATA_TOBE_UNLOADED, 0);
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
	      force_one_class (output_ctx, &classes, &ordered);
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
emit_class_owner (extract_context & ctxt, print_output & output_ctx, MOP class_)
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
	      if (DB_VALUE_TYPE (&value) == DB_TYPE_STRING && db_get_string (&value) != NULL)
		{
		  if (ctxt.is_dba_user || ctxt.is_dba_group_member)
		    {
		      output_ctx ("call [change_owner]('%s', '%s') on class [db_root];\n",
				  sm_remove_qualifier_name (classname), db_get_string (&value));
		    }
		}
	      db_value_clear (&value);
	    }
	}
    }
}

/*
 * export_serial - export db_serial
 *    return: NO_ERROR if successful, error code otherwise
 *    output_ctx(in/out): output context
 */
static int
export_serial (extract_context & ctxt, print_output & output_ctx)
{
  int error = NO_ERROR;
  int i;
  DB_QUERY_RESULT *query_result;
  DB_QUERY_ERROR query_error;
  DB_VALUE values[SERIAL_VALUE_INDEX_MAX], diff_value, answer_value;
  DB_DOMAIN *domain;
  char str_buf[NUMERIC_MAX_STRING_SIZE] = { '\0' };
  char *uppercase_user = NULL;
  size_t uppercase_user_size = 0;
  size_t query_size = 0;
  char *query = NULL;

  /*
   * You must check SERIAL_VALUE_INDEX enum defined on the top of this file
   * when changing the following query. Notice the order of the result.
   */
  const char *query_all =
    "select [unique_name], [name], [owner].[name], " "[current_val], " "[increment_val], " "[max_val], " "[min_val], "
    "[cyclic], " "[started], " "[cached_num], " "[comment] "
    "from [db_serial] where [class_name] is null and [att_name] is null";

  const char *query_user =
    "select [unique_name], [name], [owner].[name], " "[current_val], " "[increment_val], " "[max_val], " "[min_val], "
    "[cyclic], " "[started], " "[cached_num], " "[comment] "
    "from [db_serial] where [class_name] is null and [att_name] is null and owner.name='%s'";

  output_ctx ("\n");

  if (ctxt.is_dba_user == false && ctxt.is_dba_group_member == false)
    {
      uppercase_user_size = intl_identifier_upper_string_size (ctxt.login_user);
      uppercase_user = (char *) malloc (uppercase_user_size + 1);
      if (uppercase_user == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, uppercase_user_size);
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}

      intl_identifier_upper (ctxt.login_user, uppercase_user);

      query_size = strlen (query_user) + strlen (uppercase_user) + 1;
      query = (char *) malloc (query_size);
      if (query_user == NULL)
	{
	  if (uppercase_user != NULL)
	    {
	      free_and_init (uppercase_user);
	    }

	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, query_size);
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}

      sprintf (query, query_user, uppercase_user);
    }

  db_make_null (&diff_value);
  db_make_null (&answer_value);

  error = db_compile_and_execute_local (((query == NULL) ? query_all : query), &query_result, &query_error);
  if (error < 0)
    {
      goto err;
    }



  if (db_query_first_tuple (query_result) == DB_CURSOR_SUCCESS)
    {
      do
	{
	  for (i = 0; i < SERIAL_VALUE_INDEX_MAX; i++)
	    {
	      error = db_query_get_tuple_value (query_result, i, &values[i]);
	      if (error != NO_ERROR)
		{
		  goto err;
		}

	      /* Validation of the result value */
	      switch (i)
		{
		case SERIAL_OWNER_NAME:
		  {
		    if (DB_IS_NULL (&values[i]) || DB_VALUE_TYPE (&values[i]) != DB_TYPE_STRING)
		      {
			db_make_string (&values[i], "PUBLIC");
		      }
		  }
		  break;

		case SERIAL_UNIQUE_NAME:
		case SERIAL_NAME:
		  {
		    if (DB_IS_NULL (&values[i]) || DB_VALUE_TYPE (&values[i]) != DB_TYPE_STRING)
		      {
			er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_INVALID_SERIAL_VALUE, 0);
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
		    if (DB_IS_NULL (&values[i]) || DB_VALUE_TYPE (&values[i]) != DB_TYPE_NUMERIC)
		      {
			er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_INVALID_SERIAL_VALUE, 0);
			error = ER_INVALID_SERIAL_VALUE;
			goto err;
		      }
		  }
		  break;

		case SERIAL_CYCLIC:
		case SERIAL_STARTED:
		case SERIAL_CACHED_NUM:
		  {
		    if (DB_IS_NULL (&values[i]) || DB_VALUE_TYPE (&values[i]) != DB_TYPE_INTEGER)
		      {
			er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_INVALID_SERIAL_VALUE, 0);
			error = ER_INVALID_SERIAL_VALUE;
			goto err;
		      }
		  }
		  break;

		case SERIAL_COMMENT:
		  {
		    if (DB_IS_NULL (&values[i]) == false && DB_VALUE_TYPE (&values[i]) != DB_TYPE_STRING)
		      {
			er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_INVALID_SERIAL_VALUE, 0);
			error = ER_INVALID_SERIAL_VALUE;
			goto err;
		      }
		  }
		  break;

		default:
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_INVALID_SERIAL_VALUE, 0);
		  error = ER_INVALID_SERIAL_VALUE;
		  goto err;
		}
	    }

	  if (db_get_int (&values[SERIAL_STARTED]) == 1)
	    {
	      /* Calculate next value of serial */
	      db_make_null (&diff_value);
	      error = numeric_db_value_sub (&values[SERIAL_MAX_VAL], &values[SERIAL_CURRENT_VAL], &diff_value);
	      if (error == ER_IT_DATA_OVERFLOW)
		{
		  // max - curr might be flooded.
		  diff_value = values[SERIAL_MAX_VAL];
		  er_clear ();
		}
	      else if (error != NO_ERROR)
		{
		  goto err;
		}

	      error = numeric_db_value_compare (&values[SERIAL_INCREMENT_VAL], &diff_value, &answer_value);
	      if (error != NO_ERROR)
		{
		  goto err;
		}
	      /* increment > diff */
	      if (db_get_int (&answer_value) > 0)
		{
		  /* no cyclic case */
		  if (db_get_int (&values[SERIAL_CYCLIC]) == 0)
		    {
		      domain = tp_domain_resolve_default (DB_TYPE_NUMERIC);
		      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IT_DATA_OVERFLOW, 1,
			      pr_type_name (TP_DOMAIN_TYPE (domain)));
		      error = ER_IT_DATA_OVERFLOW;
		      goto err;
		    }

		  db_value_clear (&values[SERIAL_CURRENT_VAL]);
		  values[SERIAL_CURRENT_VAL] = values[SERIAL_MIN_VAL];
		}
	      /* increment <= diff */
	      else
		{
		  error =
		    numeric_db_value_add (&values[SERIAL_CURRENT_VAL], &values[SERIAL_INCREMENT_VAL], &answer_value);
		  if (error != NO_ERROR)
		    {
		      goto err;
		    }

		  db_value_clear (&values[SERIAL_CURRENT_VAL]);
		  values[SERIAL_CURRENT_VAL] = answer_value;
		}
	    }

	  output_ctx ("create serial %s%s%s\n", PRINT_IDENTIFIER (db_get_string (&values[SERIAL_NAME])));
	  output_ctx ("\t start with %s\n", numeric_db_value_print (&values[SERIAL_CURRENT_VAL], str_buf));
	  output_ctx ("\t increment by %s\n", numeric_db_value_print (&values[SERIAL_INCREMENT_VAL], str_buf));
	  output_ctx ("\t minvalue %s\n", numeric_db_value_print (&values[SERIAL_MIN_VAL], str_buf));
	  output_ctx ("\t maxvalue %s\n", numeric_db_value_print (&values[SERIAL_MAX_VAL], str_buf));
	  output_ctx ("\t %scycle\n", (db_get_int (&values[SERIAL_CYCLIC]) == 0 ? "no" : ""));
	  if (db_get_int (&values[SERIAL_CACHED_NUM]) <= 1)
	    {
	      output_ctx ("\t nocache\n");
	    }
	  else
	    {
	      output_ctx ("\t cache %d\n", db_get_int (&values[SERIAL_CACHED_NUM]));
	    }
	  if (DB_IS_NULL (&values[SERIAL_COMMENT]) == false)
	    {
	      output_ctx ("\t comment ");
	      desc_value_print (output_ctx, &values[SERIAL_COMMENT]);
	    }
	  output_ctx (";\n");

	  if (ctxt.is_dba_user || ctxt.is_dba_group_member)
	    {
	      output_ctx ("call [change_serial_owner] ('%s', '%s') on class [db_serial];\n\n",
			  db_get_string (&values[SERIAL_NAME]), db_get_string (&values[SERIAL_OWNER_NAME]));
	    }

	  db_value_clear (&diff_value);
	  db_value_clear (&answer_value);
	  for (i = 0; i < SERIAL_VALUE_INDEX_MAX; i++)
	    {
	      db_value_clear (&values[i]);
	    }
	}
      while (db_query_next_tuple (query_result) == DB_CURSOR_SUCCESS);
    }

err:
  db_query_end (query_result);

  if (uppercase_user != NULL)
    {
      free_and_init (uppercase_user);
    }
  return error;
}

/*
 * export_synonym - export _db_synonym
 *    return: NO_ERROR if successful, error code otherwise
 *    output_ctx(in/out): output context
 */
static int
export_synonym (extract_context & ctxt, print_output & output_ctx)
{
  DB_QUERY_RESULT *query_result;
  DB_QUERY_ERROR query_error;
  DB_VALUE values[SYNONYM_VALUE_INDEX_MAX];
  const char *synonym_name = NULL;
  DB_OBJECT *synonym_owner = NULL;
  const char *synonym_owner_name = NULL;
  int is_public = 0;
  const char *target_name = NULL;
  const char *target_owner_name = NULL;
  const char *comment = NULL;
  bool is_dba_group_member = false;
  int i = 0;
  int save = 0;
  int error = NO_ERROR;
  char *uppercase_user = NULL;
  size_t uppercase_user_size = 0;
  size_t query_size = 0;
  char *query = NULL;
  char output_owner[DB_MAX_USER_LENGTH + 4] = { '\0' };

  // *INDENT-OFF*
  const char *query_all = "SELECT [name], "
			     "[owner], "
			     "[owner].[name], "
			     "[is_public], "
			     "[target_name], "
			     "[target_owner].[name], "
			     "[comment] "
			"FROM [_db_synonym]";

  const char *query_user = "SELECT [name], "
                               "[owner], "
                               "[owner].[name], "
                               "[is_public], "
                               "[target_name], "
                               "[target_owner].[name], "
                               "[comment] "
                           "FROM [_db_synonym]"
                           "WHERE [owner].[name] = '%s'";
  // *INDENT-ON*

  query_error.err_lineno = 0;
  query_error.err_posno = 0;

  AU_DISABLE (save);

  output_ctx ("\n");

  if (ctxt.is_dba_user == false && ctxt.is_dba_group_member == false)
    {
      uppercase_user_size = intl_identifier_upper_string_size (ctxt.login_user);
      uppercase_user = (char *) malloc (uppercase_user_size + 1);
      if (uppercase_user == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, uppercase_user_size);
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}

      intl_identifier_upper (ctxt.login_user, uppercase_user);

      query_size = strlen (query_user) + strlen (uppercase_user) + 1;
      query = (char *) malloc (query_size);
      if (query_user == NULL)
	{
	  if (uppercase_user != NULL)
	    {
	      free_and_init (uppercase_user);
	    }

	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, query_size);
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}

      sprintf (query, query_user, uppercase_user);
    }

  error = db_compile_and_execute_local (((query == NULL) ? query_all : query), &query_result, &query_error);
  if (error < 0)
    {
      ASSERT_ERROR ();
      goto end;
    }

  is_dba_group_member = au_is_dba_group_member (Au_user);

  if (db_query_first_tuple (query_result) == DB_CURSOR_SUCCESS)
    {
      do
	{
	  for (i = 0; i < SYNONYM_VALUE_INDEX_MAX; i++)
	    {
	      error = db_query_get_tuple_value (query_result, i, &values[i]);
	      if (error != NO_ERROR)
		{
		  ASSERT_ERROR ();
		  goto end;
		}

	      /* Validation of the result value */
	      switch (i)
		{
		case SYNONYM_NAME:
		case SYNONYM_OWNER_NAME:
		case SYNONYM_TARGET_NAME:
		case SYNONYM_TARGET_OWNER_NAME:
		  {
		    if (DB_IS_NULL (&values[i]) || DB_VALUE_TYPE (&values[i]) != DB_TYPE_STRING)
		      {
			ERROR_SET_ERROR (error, ER_SYNONYM_INVALID_VALUE);
			goto end;
		      }
		  }
		  break;

		case SYNONYM_OWNER:
		  {
		    if (DB_IS_NULL (&values[i]) || DB_VALUE_TYPE (&values[i]) != DB_TYPE_OBJECT)
		      {
			ERROR_SET_ERROR (error, ER_SYNONYM_INVALID_VALUE);
			goto end;
		      }
		  }
		  break;

		case SYNONYM_IS_PUBLIC:
		  {
		    if (DB_IS_NULL (&values[i]) || DB_VALUE_TYPE (&values[i]) != DB_TYPE_INTEGER)
		      {
			ERROR_SET_ERROR (error, ER_SYNONYM_INVALID_VALUE);
			goto end;
		      }
		  }
		  break;

		case SYNONYM_COMMENT:
		  {
		    if (DB_IS_NULL (&values[i]) == false && DB_VALUE_TYPE (&values[i]) != DB_TYPE_STRING)
		      {
			ERROR_SET_ERROR (error, ER_SYNONYM_INVALID_VALUE);
			goto end;
		      }
		  }
		  break;

		default:
		  ERROR_SET_ERROR (error, ER_SYNONYM_INVALID_VALUE);
		  goto end;
		}
	    }

	  synonym_name = db_get_string (&values[SYNONYM_NAME]);
	  synonym_owner = db_get_object (&values[SYNONYM_OWNER]);
	  synonym_owner_name = db_get_string (&values[SYNONYM_OWNER_NAME]);
	  is_public = db_get_int (&values[SYNONYM_IS_PUBLIC]);
	  target_name = db_get_string (&values[SYNONYM_TARGET_NAME]);
	  target_owner_name = db_get_string (&values[SYNONYM_TARGET_OWNER_NAME]);

	  if (!is_dba_group_member && !ws_is_same_object (Au_user, synonym_owner))
	    {
	      continue;
	    }

	  if (is_public == 1)
	    {
	      output_ctx ("CREATE PUBLIC");
	    }
	  else
	    {
	      output_ctx ("CREATE PRIVATE");
	    }

	  PRINT_OWNER_NAME (synonym_owner_name, (ctxt.is_dba_user || ctxt.is_dba_group_member), output_owner,
			    sizeof (output_owner));

	  output_ctx (" SYNONYM %s%s%s%s FOR %s%s%s.%s%s%s", output_owner,
		      PRINT_IDENTIFIER (synonym_name), PRINT_IDENTIFIER (target_owner_name),
		      PRINT_IDENTIFIER (target_name));

	  if (DB_IS_NULL (&values[SYNONYM_COMMENT]) == false)
	    {
	      output_ctx (" COMMENT ");
	      desc_value_print (output_ctx, &values[SYNONYM_COMMENT]);
	    }
	  output_ctx (";\n");

	  for (i = 0; i < SYNONYM_VALUE_INDEX_MAX; i++)
	    {
	      db_value_clear (&values[i]);
	    }
	}
      while (db_query_next_tuple (query_result) == DB_CURSOR_SUCCESS);
    }

end:
  db_query_end (query_result);

  if (uppercase_user != NULL)
    {
      free_and_init (uppercase_user);
    }

  AU_ENABLE (save);

  return error;
}

/*
 * extract_classes_to_file - exports schema to file
 *    return: 0 if successful, error count otherwise
 *    ctxt(in/out): extract context
 */
int
extract_classes_to_file (extract_context & ctxt)
{
  int err_count = 0;

  if (split_schema_files)
    {
      err_count = extract_split_schema_files (ctxt);
    }
  else
    {
      char output_filename_schema[PATH_MAX * 2] = { '\0' };

      if (create_filename_schema (ctxt.output_dirname, ctxt.output_prefix, output_filename_schema,
				  sizeof (output_filename_schema)) == 0)
	{
	  err_count = extract_all_schema_file (ctxt, output_filename_schema);
	}
      else
	{
	  util_log_write_errid (MSGCAT_UTIL_GENERIC_INVALID_ARGUMENT);
	  err_count = 1;
	}
    }

  return err_count;
}

/*
 * extract_schema - exports schema to output
 *    return: 0 if successful, error count otherwise
 *    ctxt(in/out): extract context
 *    schema_output_ctx(in/out) : output countext
 * Note:
 *    Always output the entire schema.
 */
static int
extract_schema (extract_context & ctxt, print_output & schema_output_ctx)
{
  DB_OBJLIST *classes = NULL;
  DB_OBJLIST *vclass_list_has_using_index = NULL;
  int err_count = 0;

  /*
   * convert the class table into an ordered class list, would be better
   * if we just built the initial list rather than using the table.
   */
  ctxt.classes = get_ordered_classes (schema_output_ctx, NULL);
  if (ctxt.classes == NULL)
    {
      if (db_error_code () != NO_ERROR)
	{
	  return 1;
	}
      else
	{
	  fprintf (stderr, "%s: Unknown database error occurs " "but may not be database error.\n\n", ctxt.exec_name);
	  return 1;
	}
    }

  if (ctxt.is_dba_user == false && ctxt.is_dba_group_member == false)
    {
      filter_user_classes (&ctxt.classes, ctxt.login_user);
    }

  /*
   * Schema
   */
  if (!required_class_only && ctxt.do_auth)
    {
      if (au_export_users (ctxt, schema_output_ctx) < NO_ERROR)
	{
	  err_count++;
	}
    }

  if (!required_class_only && export_serial (ctxt, schema_output_ctx) < NO_ERROR)
    {
      fprintf (stderr, "%s", db_error_string (3));
      if (db_error_code () == ER_INVALID_SERIAL_VALUE)
	{
	  fprintf (stderr, " Check the value of db_serial object.\n");
	}
    }

  /*
   * If there is a view using synonym, the synonym must be created first.
   * Since a synonym is like an alias, it can be created even if the target does not exist.
   * So, unload the synonym before class/vclass.
   */
  if (export_synonym (ctxt, schema_output_ctx) < NO_ERROR)
    {
      fprintf (stderr, "%s", db_error_string (3));
      if (db_error_code () == ER_SYNONYM_INVALID_VALUE)
	{
	  fprintf (stderr, " Check the value of _db_synonym object.\n");
	}
    }

  if (emit_stored_procedure (ctxt, schema_output_ctx) != NO_ERROR)
    {
      err_count++;
    }

  if (export_server (ctxt, schema_output_ctx) < NO_ERROR)
    {
      err_count++;
    }

  emit_schema (ctxt, schema_output_ctx, EXTRACT_CLASS_ALL);
  if (er_errid () != NO_ERROR)
    {
      err_count++;
    }

  if (emit_foreign_key (ctxt, schema_output_ctx, ctxt.classes) != NO_ERROR)
    {
      err_count++;
    }

  if (emit_grant (ctxt, schema_output_ctx, ctxt.classes) != NO_ERROR)
    {
      err_count++;
    }

  return err_count;
}

/*
 * extract_triggers_to_file - exports triggers to file
 *    return: 0 if successful, error count otherwise
 *    ctxt(in/out): extract context
 *    output_filename(in/out) : output filename
 */
int
extract_triggers_to_file (extract_context & ctxt, const char *output_filename)
{
  FILE *output_file;
  int error = NO_ERROR;

  output_file = fopen_ex (output_filename, "w");
  if (output_file == NULL)
    {
      (void) fprintf (stderr, "%s: %s.\n\n", ctxt.exec_name, strerror (errno));
      return 1;
    }

  file_print_output output_ctx (output_file);

  error = extract_triggers (ctxt, output_ctx);

  fflush (output_file);

  if (ftell (output_file) == 0)
    {
      /* file is empty (database has no trigger to be emitted) */
      fclose (output_file);
      output_file = NULL;
      remove (output_filename);
    }
  else
    {
      /* not empty */
      if (error == NO_ERROR)
	{
	  output_ctx ("\n");
	  output_ctx ("COMMIT WORK;\n");
	}
      fclose (output_file);
      output_file = NULL;
    }

  return error;
}

/*
 * extract_triggers - exports triggers to output
 *    return: 0 if successful, error count otherwise
 *    ctxt(in/out): extract context
 *    schema_output_ctx(in/out) : output countext
 * Note:
 *    Always output the entire schema.
 */
int
extract_triggers (extract_context & ctxt, print_output & output_ctx)
{
  /*
   * Trigger
   * emit the triggers last, they will have no mutual dependencies so
   * it doesn't really matter what order they're in.
   */
  assert (ctxt.classes != NULL);
  if (tr_dump_selective_triggers (ctxt, output_ctx, ctxt.classes) != NO_ERROR)
    {
      return ER_FAILED;
    }

  return NO_ERROR;
}

/*
 * extract_indexes_to_file - exports indexes to file
 *    return: 0 if successful, error count otherwise
 *    ctxt(in/out): extract context
 *    output_filename(in/out) : output filename
 */
int
extract_indexes_to_file (extract_context & ctxt, const char *output_filename)
{
  FILE *output_file = NULL;
  int err_count = 0;

  if (!ctxt.has_indexes)
    {
      /* remove any indexes file from previous attempt */
      output_file = fopen_ex (output_filename, "r");
      if (output_file != NULL)
	{
	  fclose (output_file);
	  output_file = NULL;
	  if (unlink (output_filename))
	    {
	      (void) fprintf (stderr, "%s.\n\n", strerror (errno));
	      return 1;
	    }
	}
      return 0;
    }

  output_file = fopen_ex (output_filename, "w");
  if (output_file == NULL)
    {
      (void) fprintf (stderr, "%s: %s.\n\n", ctxt.exec_name, strerror (errno));
      return 1;
    }

  file_print_output output_ctx (output_file);

  err_count = emit_indexes (ctxt, output_ctx, ctxt.classes, ctxt.has_indexes, ctxt.vclass_list_has_using_index);

  fflush (output_file);

  if (ftell (output_file) == 0)
    {
      /* file is empty (database has no indexes to be emitted) */
      fclose (output_file);
      output_file = NULL;
      remove (output_filename);
    }
  else
    {				/* not empty */
      if (err_count == 0)
	{
	  output_ctx ("\n");
	  output_ctx ("COMMIT WORK;\n");
	}
      fclose (output_file);
      output_file = NULL;
    }

  return err_count;
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
emit_indexes (extract_context & ctxt, print_output & output_ctx, DB_OBJLIST * classes, int has_indexes,
	      DB_OBJLIST * vclass_list_has_using_index)
{
  DB_OBJLIST *cl;

  for (cl = classes; cl != NULL; cl = cl->next)
    {
      /* if its some sort of vclass then it can't have indexes */
      if (db_is_vclass (cl->op) <= 0)
	{
	  emit_index_def (ctxt, output_ctx, cl->op);
	}
    }

  if (vclass_list_has_using_index != NULL)
    {
      emit_query_specs_has_using_index (ctxt, output_ctx, vclass_list_has_using_index);
    }

  return 0;
}

/*
 * emit_schema -
 *    return:
 *    classes():
 *    do_auth():
 *    vclass_list_has_using_index():
 */
static void
emit_schema (extract_context & ctxt, print_output & output_ctx, EXTRACT_CLASS_TYPE extract_class)
{
  DB_OBJLIST *cl = NULL;
  int is_vclass = 0;
  const char *class_type = NULL;
  const char *name = NULL;
  char owner_name[DB_MAX_IDENTIFIER_LENGTH] = { '\0' };
  char *class_name = NULL;
  const char *tde_algo_name = NULL;
  int is_partitioned = 0;
  SM_CLASS *class_ = NULL;

  output_ctx ("\n\n");

  /*
   * First create all the classes
   */
  for (cl = ctxt.classes; cl != NULL; cl = cl->next)
    {
      is_vclass = db_is_vclass (cl->op);

      name = db_get_class_name (cl->op);
      if (do_is_partitioned_subclass (&is_partitioned, name, NULL))
	{
	  continue;
	}

      if (au_fetch_class_force (cl->op, &class_, AU_FETCH_READ) != NO_ERROR)
	{
	  class_ = NULL;
	}
      else
	{
	  if (extract_class == EXTRACT_CLASS_ALL)
	    {
	      output_ctx ("CREATE %s %s%s%s", is_vclass ? "VCLASS" : "CLASS",
			  PRINT_IDENTIFIER (sm_remove_qualifier_name (name)));
	    }
	  else
	    {
	      if (is_vclass == TRUE && extract_class == EXTRACT_VCLASS)
		{
		  output_ctx ("CREATE VCLASS %s%s%s", PRINT_IDENTIFIER (sm_remove_qualifier_name (name)));
		}
	      else if (is_vclass == FALSE && extract_class == EXTRACT_CLASS)
		{
		  output_ctx ("CREATE CLASS %s%s%s", PRINT_IDENTIFIER (sm_remove_qualifier_name (name)));
		}
	      else
		{
		  continue;
		}
	    }
	}

      if (is_vclass > 0)
	{
	  if (sm_get_class_flag (cl->op, SM_CLASSFLAG_WITHCHECKOPTION) > 0)
	    {
	      output_ctx (" WITH CHECK OPTION");
	    }
	  else if (sm_get_class_flag (cl->op, SM_CLASSFLAG_LOCALCHECKOPTION) > 0)
	    {
	      output_ctx (" WITH LOCAL CHECK OPTION");
	    }
	}
      else
	{
	  if (sm_get_class_flag (cl->op, SM_CLASSFLAG_REUSE_OID) > 0)
	    {
	      output_ctx (" REUSE_OID");
	    }
	  else
	    {
	      output_ctx (" DONT_REUSE_OID");
	    }

	  if (class_ != NULL)
	    {
	      output_ctx (", COLLATE %s", lang_get_collation_name (class_->collation_id));
	    }

	  if (class_ != NULL)
	    {
	      tde_algo_name = tde_get_algorithm_name ((TDE_ALGORITHM) class_->tde_algorithm);
	      assert (tde_algo_name != NULL);
	      if (strcmp (tde_algo_name, tde_get_algorithm_name (TDE_ALGORITHM_NONE)) != 0)
		{
		  output_ctx (" ENCRYPT=%s", tde_algo_name);
		}
	    }
	}

      if (class_ != NULL && class_->comment != NULL && class_->comment[0] != '\0')
	{
	  output_ctx (" ");
	  help_print_describe_comment (output_ctx, class_->comment);
	}

      output_ctx (";\n");
      if (is_vclass <= 0 && ctxt.storage_order == FOLLOW_STORAGE_ORDER)
	{
	  emit_class_meta (output_ctx, cl->op);
	}

      /*
       * Before version 11.2, if an auto_increment column was added after changing the class owner,
       * owner mismatch occurred.
       * 
       * e.g. create user u1;
       *      create table t1;
       *      call change_owner ('t1', 'u1') on class db_root;
       *      alter table t1 add attribute c1 int auto_increment;
       *      select c.clasS_name, c.owner.name, s.name, s.owner.name
       *      from _db_class c, db_serial s
       *      where c.class_name = s.class_name;
       *
       *        class_name            owner.name            name                  owner.name
       *      ========================================================================================
       *        't1'                  'U1'                  't1_ai_c1'            'DBA'
       *
       * After version 11.2, when adding an auto_increment column, there is no problem
       * because it sets the owner in unique_name.
       * 
       * There is a problem if the DBA does not change the owner immediately when creating multiple classes
       * with the same name. This is because a DBA cannot own multiple classes with the same name at the same time.
       * Therefore, the owner must be changed immediately after class creation.
       */
      if (ctxt.do_auth)
	{
	  emit_class_owner (ctxt, output_ctx, cl->op);
	}

      output_ctx ("\n");
    }

  output_ctx ("\n");

  /* emit super classes without resolutions for non-proxies */
  for (cl = ctxt.classes; cl != NULL; cl = cl->next)
    {
      is_vclass = db_is_vclass (cl->op);

      if (extract_class == EXTRACT_CLASS_ALL)
	{
	  class_type = (is_vclass > 0) ? "VCLASS" : "CLASS";
	}
      else
	{
	  if (is_vclass == TRUE && extract_class == EXTRACT_VCLASS)
	    {
	      class_type = "VCLASS";
	    }
	  else if (is_vclass == FALSE && extract_class == EXTRACT_CLASS)
	    {
	      class_type = "CLASS";
	    }
	  else
	    {
	      continue;
	    }
	}

      (void) emit_superclasses (ctxt, output_ctx, cl->op, class_type);
    }

  output_ctx ("\n");

  /*
   * Now fill out the class definitions for the non-proxy classes.
   */
  for (cl = ctxt.classes; cl != NULL; cl = cl->next)
    {
      bool found = false;

      is_vclass = db_is_vclass (cl->op);
      if (extract_class == EXTRACT_CLASS_ALL)
	{
	  class_type = (is_vclass > 0) ? "VCLASS" : "CLASS";
	}
      else
	{
	  if (is_vclass == TRUE && extract_class == EXTRACT_VCLASS)
	    {
	      class_type = "VCLASS";
	    }
	  else if (is_vclass == FALSE && extract_class == EXTRACT_CLASS)
	    {
	      class_type = "CLASS";
	    }
	  else
	    {
	      continue;
	    }
	}

      name = db_get_class_name (cl->op);
      if (do_is_partitioned_subclass (&is_partitioned, name, NULL))
	{
	  continue;
	}

      output_ctx ("\n");

      if (emit_all_attributes (ctxt, output_ctx, cl->op, class_type, &ctxt.has_indexes, ctxt.storage_order))
	{
	  found = true;
	}

      if (emit_methods (ctxt, output_ctx, cl->op, class_type))
	{
	  found = true;
	}

      if (is_partitioned)
	{
	  emit_partition_info (ctxt, output_ctx, cl->op);
	}
    }

  output_ctx ("\n\n");

  /* emit super class resolutions for non-proxies */
  for (cl = ctxt.classes; cl != NULL; cl = cl->next)
    {
      is_vclass = db_is_vclass (cl->op);
      if (extract_class == EXTRACT_CLASS_ALL)
	{
	  class_type = (is_vclass > 0) ? "VCLASS" : "CLASS";
	}
      else
	{
	  if (is_vclass == TRUE && extract_class == EXTRACT_VCLASS)
	    {
	      class_type = "VCLASS";
	    }
	  else if (is_vclass == FALSE && extract_class == EXTRACT_CLASS)
	    {
	      class_type = "CLASS";
	    }
	  else
	    {
	      continue;
	    }
	}

      (void) emit_resolutions (ctxt, output_ctx, cl->op, class_type);
    }

  output_ctx ("\n");

  /*
   * do query specs LAST after we're sure that all potentially
   * referenced classes have their full definitions.
   */
  if (extract_class != EXTRACT_CLASS)
    {
      ctxt.vclass_list_has_using_index = emit_query_specs (ctxt, output_ctx, ctxt.classes);
    }

  if (er_errid () == ER_OBJ_NO_COMPONENTS)
    {
      er_clear ();
    }

  output_ctx ("\n");
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
   * Previously, it force two full passes on the query specs of all vclasses,
   * Now, force one pass
   */
  return 0;
}


/*
 * emit_query_specs - Emit the object ids for a virtual class.
 *    return:
 *    classes(in):
 */
static DB_OBJLIST *
emit_query_specs (extract_context & ctxt, print_output & output_ctx, DB_OBJLIST * classes)
{
  DB_QUERY_SPEC *specs, *s;
  DB_OBJLIST *cl;
  DB_OBJLIST *vclass_list_has_using_index = NULL;
  PARSER_CONTEXT *parser;
  PT_NODE **query_ptr;
  const char *name;
  char owner_name[DB_MAX_IDENTIFIER_LENGTH] = { '\0' };
  char *class_name = NULL;
  const char *null_spec;
  bool has_using_index;
  bool change_vclass_spec;
  int i;
  char output_owner[DB_MAX_USER_LENGTH + 4] = { '\0' };

  /*
   * pass 1, emit NULL spec lists for vclasses that have attribute
   * domains which are other vclasses
   */
  for (cl = classes; cl != NULL; cl = cl->next)
    {
      if (db_is_vclass (cl->op) <= 0)
	{
	  continue;
	}

      name = db_get_class_name (cl->op);
      specs = db_get_query_specs (cl->op);
      if (specs == NULL)
	{
	  continue;
	}

      if (!has_vclass_domains (cl->op))
	{
	  continue;
	}

      output_ctx ("\n");

      has_using_index = false;
      for (s = specs; s && has_using_index == false; s = db_query_spec_next (s))
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

	  query_ptr = parser_parse_string (parser, db_query_spec_string (s));
	  if (query_ptr != NULL)
	    {
	      parser_walk_tree (parser, *query_ptr, pt_has_using_index_clause, &has_using_index, NULL, NULL);

	      if (has_using_index == true)
		{
		  /* all view specs should be emitted at index file */
		  ml_append (&vclass_list_has_using_index, cl->op, NULL);
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

	      query_ptr = parser_parse_string (parser, db_query_spec_string (s));
	      if (query_ptr != NULL)
		{
		  null_spec = pt_print_query_spec_no_list (parser, *query_ptr);
		  SPLIT_USER_SPECIFIED_NAME (name, owner_name, class_name);

		  PRINT_OWNER_NAME (output_owner, (ctxt.is_dba_user || ctxt.is_dba_group_member), output_owner,
				    sizeof (output_owner));

		  output_ctx ("ALTER VCLASS %s%s%s%s ADD QUERY %s ; \n", output_owner,
			      PRINT_IDENTIFIER (class_name), null_spec);
		}
	      parser_free_parser (parser);
	    }
	}
    }

  /*
   * pass 2, emit full spec lists
   */
  for (cl = classes; cl != NULL; cl = cl->next)
    {
      if (db_is_vclass (cl->op) <= 0)
	{
	  continue;
	}

      name = db_get_class_name (cl->op);
      specs = db_get_query_specs (cl->op);
      if (specs == NULL)
	{
	  continue;
	}

      if (ml_find (vclass_list_has_using_index, cl->op))
	{
	  continue;
	}

      output_ctx ("\n");

      change_vclass_spec = has_vclass_domains (cl->op);

      for (s = specs, i = 1; s != NULL; s = db_query_spec_next (s), i++)
	{
	  SPLIT_USER_SPECIFIED_NAME (name, owner_name, class_name);
	  if (change_vclass_spec)
	    {			/* change the existing spec lists */
	      PRINT_OWNER_NAME (owner_name, (ctxt.is_dba_user || ctxt.is_dba_group_member), output_owner,
				sizeof (output_owner));

	      output_ctx ("ALTER VCLASS %s%s%s%s CHANGE QUERY %d %s ;\n", output_owner,
			  PRINT_IDENTIFIER (class_name), i, db_query_spec_string (s));
	    }
	  else
	    {			/* emit the usual statements */
	      PRINT_OWNER_NAME (owner_name, (ctxt.is_dba_user || ctxt.is_dba_group_member), output_owner,
				sizeof (output_owner));

	      output_ctx ("ALTER VCLASS %s%s%s%s ADD QUERY %s ;\n", output_owner,
			  PRINT_IDENTIFIER (class_name), db_query_spec_string (s));
	    }
	}
    }

  return vclass_list_has_using_index;
}


/*
 * emit_query_specs_has_using_index - Emit the object ids for a virtual class
 *    return:
 *    vclass_list_has_using_index():
 */
static int
emit_query_specs_has_using_index (extract_context & ctxt, print_output & output_ctx,
				  DB_OBJLIST * vclass_list_has_using_index)
{
  DB_QUERY_SPEC *specs, *s;
  DB_OBJLIST *cl;
  PARSER_CONTEXT *parser;
  PT_NODE **query_ptr;
  const char *name;
  char owner_name[DB_MAX_IDENTIFIER_LENGTH] = { '\0' };
  char *class_name = NULL;
  const char *null_spec;
  bool change_vclass_spec;
  int i;
  char output_owner[DB_MAX_USER_LENGTH + 4] = { '\0' };

  output_ctx ("\n\n");

  /*
   * pass 1, emit NULL spec lists for vclasses that have attribute
   * domains which are other vclasses
   */

  for (cl = vclass_list_has_using_index; cl != NULL; cl = cl->next)
    {
      if (db_is_vclass (cl->op) <= 0)
	{
	  continue;
	}

      name = db_get_class_name (cl->op);
      specs = db_get_query_specs (cl->op);
      if (specs == NULL)
	{
	  continue;
	}

      if (!has_vclass_domains (cl->op))
	{
	  continue;
	}

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
	  query_ptr = parser_parse_string (parser, db_query_spec_string (s));
	  if (query_ptr != NULL)
	    {
	      null_spec = pt_print_query_spec_no_list (parser, *query_ptr);
	      SPLIT_USER_SPECIFIED_NAME (name, owner_name, class_name);

	      PRINT_OWNER_NAME (owner_name, (ctxt.is_dba_user || ctxt.is_dba_group_member), output_owner,
				sizeof (output_owner));

	      output_ctx ("ALTER VCLASS %s%s%s%s ADD QUERY %s ; \n", output_owner,
			  PRINT_IDENTIFIER (class_name), null_spec);
	    }
	  parser_free_parser (parser);
	}
    }

  /* pass 2, emit full spec lists */
  for (cl = vclass_list_has_using_index; cl != NULL; cl = cl->next)
    {
      if (db_is_vclass (cl->op) <= 0)
	{
	  continue;
	}
      name = db_get_class_name (cl->op);
      specs = db_get_query_specs (cl->op);
      if (specs == NULL)
	{
	  continue;
	}

      change_vclass_spec = has_vclass_domains (cl->op);

      for (s = specs, i = 1; s; s = db_query_spec_next (s), i++)
	{
	  SPLIT_USER_SPECIFIED_NAME (name, owner_name, class_name);
	  if (change_vclass_spec)
	    {			/* change the existing spec lists */
	      PRINT_OWNER_NAME (owner_name, (ctxt.is_dba_user || ctxt.is_dba_group_member), output_owner,
				sizeof (output_owner));

	      output_ctx ("ALTER VCLASS %s%s%s%s CHANGE QUERY %d %s ;\n", output_owner,
			  PRINT_IDENTIFIER (class_name), i, db_query_spec_string (s));
	    }
	  else
	    {			/* emit the usual statements */
	      PRINT_OWNER_NAME (owner_name, (ctxt.is_dba_user || ctxt.is_dba_group_member), output_owner,
				sizeof (output_owner));

	      output_ctx ("ALTER VCLASS %s%s%s%s ADD QUERY %s ;\n", output_owner,
			  PRINT_IDENTIFIER (class_name), db_query_spec_string (s));
	    }
	}
    }

  return NO_ERROR;
}


/*
 * emit_superclasses - emit queries for adding superclass for the class given
 *    return: true if there are any superclasses or conflict resolutions
 *    output_ctx(in/out): output context
 *    class(in): the class to emit the superclasses for
 *    class_type(in): CLASS or VCLASS
 */
static bool
emit_superclasses (extract_context & ctxt, print_output & output_ctx, DB_OBJECT * class_, const char *class_type)
{
  DB_OBJLIST *supers, *s;
  const char *name;
  char owner_name[DB_MAX_IDENTIFIER_LENGTH] = { '\0' };
  char *class_name = NULL;
  char output_owner[DB_MAX_USER_LENGTH + 4] = { '\0' };

  supers = db_get_superclasses (class_);
  if (supers != NULL)
    {
      /* create class alter string */
      name = db_get_class_name (class_);
      if (do_is_partitioned_subclass (NULL, name, NULL))
	{
	  return (supers != NULL);
	}

      SPLIT_USER_SPECIFIED_NAME (name, owner_name, class_name);

      PRINT_OWNER_NAME (owner_name, (ctxt.is_dba_user || ctxt.is_dba_group_member), output_owner,
			sizeof (output_owner));

      output_ctx ("ALTER %s %s%s%s%s ADD SUPERCLASS ", class_type, output_owner, PRINT_IDENTIFIER (class_name));

      for (s = supers; s != NULL; s = s->next)
	{
	  name = db_get_class_name (s->op);
	  if (s != supers)
	    {
	      output_ctx (", ");
	    }

	  SPLIT_USER_SPECIFIED_NAME (name, owner_name, class_name);

	  PRINT_OWNER_NAME (owner_name, (ctxt.is_dba_user || ctxt.is_dba_group_member), output_owner,
			    sizeof (output_owner));

	  output_ctx ("%s%s%s%s", output_owner, PRINT_IDENTIFIER (class_name));
	}

      output_ctx (";\n");

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
emit_resolutions (extract_context & ctxt, print_output & output_ctx, DB_OBJECT * class_, const char *class_type)
{
  DB_RESOLUTION *resolution_list;
  bool return_value = false;
  const char *name;
  char owner_name[DB_MAX_IDENTIFIER_LENGTH] = { '\0' };
  char *class_name = NULL;
  char output_owner[DB_MAX_USER_LENGTH + 4] = { '\0' };

  resolution_list = db_get_resolutions (class_);
  if (resolution_list != NULL)
    {
      name = db_get_class_name (class_);
      SPLIT_USER_SPECIFIED_NAME (name, owner_name, class_name);

      PRINT_OWNER_NAME (owner_name, (ctxt.is_dba_user || ctxt.is_dba_group_member), output_owner,
			sizeof (output_owner));

      output_ctx ("ALTER %s %s%s%s%s INHERIT", class_type, output_owner, PRINT_IDENTIFIER (class_name));

      for (; resolution_list != NULL; resolution_list = db_resolution_next (resolution_list))
	{
	  if (return_value == true)
	    {
	      output_ctx (",\n");
	    }
	  else
	    {
	      output_ctx ("\n");
	      return_value = true;
	    }
	  emit_resolution_def (ctxt, output_ctx, resolution_list,
			       (db_resolution_isclass (resolution_list) ? CLASS_RESOLUTION : INSTANCE_RESOLUTION));
	}

      output_ctx (";\n");
    }				/* if */

  return (return_value);
}


/*
 * emit_resolution_def - emit the resolution qualifier
 *    return: void
 *    resolution(in): the resolution
 *    qualifier(in): the qualifier for this resolution (instance or class)
 */
static void
emit_resolution_def (extract_context & ctxt, print_output & output_ctx, DB_RESOLUTION * resolution,
		     RESOLUTION_QUALIFIER qualifier)
{
  const char *name, *alias, *class_name;
  char owner_name[DB_MAX_IDENTIFIER_LENGTH] = { '\0' };
  char *class_name_p = NULL;
  DB_OBJECT *class_;
  char output_owner[DB_MAX_USER_LENGTH + 4] = { '\0' };

  class_ = db_resolution_class (resolution);
  if (class_ == NULL)
    {
      return;
    }

  name = db_resolution_name (resolution);
  if (name == NULL)
    {
      return;
    }

  class_name = db_get_class_name (class_);
  if (class_name == NULL)
    {
      return;
    }
  SPLIT_USER_SPECIFIED_NAME (class_name, owner_name, class_name_p);

  alias = db_resolution_alias (resolution);

  switch (qualifier)
    {
    case INSTANCE_RESOLUTION:
      {
	PRINT_OWNER_NAME (owner_name, (ctxt.is_dba_user || ctxt.is_dba_group_member), output_owner,
			  sizeof (output_owner));

	output_ctx ("       %s%s%s OF %s%s%s%s", PRINT_IDENTIFIER (name), output_owner,
		    PRINT_IDENTIFIER (class_name_p));
	break;
      }
    case CLASS_RESOLUTION:
      {
	PRINT_OWNER_NAME (owner_name, (ctxt.is_dba_user || ctxt.is_dba_group_member), output_owner,
			  sizeof (output_owner));

	output_ctx ("CLASS  %s%s%s OF %s%s%s%s", PRINT_IDENTIFIER (name), output_owner,
		    PRINT_IDENTIFIER (class_name_p));
	break;
      }
    }

  if (alias != NULL)
    {
      output_ctx (" AS %s%s%s", PRINT_IDENTIFIER (alias));
    }

  class_ = NULL;
}


/*
 * emit_instance_attributes - emit quries for adding the attributes (instance,
 * shared and class) for the class given
 *    return: true if any locally defined attributes are found, false
 *            otherwise
 *    output_ctx(in/out): output context
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
emit_instance_attributes (extract_context & ctxt, print_output & output_ctx, DB_OBJECT * class_, const char *class_type,
			  int *has_indexes, EMIT_STORAGE_ORDER storage_order)
{
  DB_ATTRIBUTE *attribute_list, *first_attribute, *a;
  int unique_flag = 0;
  int reverse_unique_flag = 0;
  int index_flag = 0;
  const char *name;
  char owner_name[DB_MAX_IDENTIFIER_LENGTH] = { '\0' };
  char *class_name = NULL;
  char *serial_name = NULL;
  char output_owner[DB_MAX_USER_LENGTH + 4] = { '\0' };

  attribute_list = db_get_attributes (class_);

  /* see if we have an index or unique defined on any attribute */
  for (a = attribute_list; a != NULL; a = db_attribute_next (a))
    {
      if (db_attribute_class (a) == class_)
	{
	  if (db_attribute_is_unique (a))
	    {
	      unique_flag = 1;
	    }
	  else if (db_attribute_is_reverse_unique (a))
	    {
	      reverse_unique_flag = 1;
	    }
	}

      if (db_attribute_is_indexed (a))
	{
	  index_flag = 1;
	}

      if (unique_flag && reverse_unique_flag && index_flag)
	{
	  /* Since we already found all, no need to go further. */
	  break;
	}
    }

  /*
   * We call this function many times, so be careful not to clobber
   * (i.e. overwrite) the has_index parameter
   */
  if (has_indexes != NULL)
    {
      *has_indexes |= index_flag;
    }


  /* see if we have any locally defined components on either list */
  first_attribute = NULL;
  for (a = attribute_list; a != NULL && first_attribute == NULL; a = db_attribute_next (a))
    {
      if (db_attribute_class (a) == class_)
	{
	  first_attribute = a;
	}
    }

  if (first_attribute == NULL)
    {
      return false;
    }

  name = db_get_class_name (class_);
  if (storage_order == FOLLOW_STORAGE_ORDER)
    {
      DB_ATTRIBUTE **ordered_attributes, **storage_attributes;
      int i, j, max_order, shared_pos;
      const char *option, *old_attribute_name;

      max_order = 0;
      for (a = first_attribute; a != NULL; a = db_attribute_next (a))
	{
	  if (db_attribute_class (a) != class_)
	    {
	      continue;
	    }

	  if (a->order > max_order)
	    {
	      max_order = a->order;
	    }
	}
      max_order++;

      ordered_attributes = (DB_ATTRIBUTE **) calloc (max_order * 2, sizeof (DB_ATTRIBUTE *));
      if (ordered_attributes == NULL)
	{
	  return false;
	}
      storage_attributes = &ordered_attributes[max_order];

      shared_pos = 1;
      for (a = first_attribute; a != NULL; a = db_attribute_next (a))
	{
	  if (db_attribute_class (a) != class_)
	    {
	      continue;
	    }

	  if (db_attribute_is_shared (a))
	    {
	      storage_attributes[max_order - shared_pos] = a;
	      shared_pos++;
	    }
	  else
	    {
	      assert (storage_attributes[a->storage_order] == NULL);
	      storage_attributes[a->storage_order] = a;
	    }
	}

      for (i = 0; i < max_order; i++)
	{
	  a = storage_attributes[i];
	  if (a == NULL)
	    {
	      continue;
	    }

	  assert (ordered_attributes[a->order] == NULL);
	  ordered_attributes[a->order] = a;

	  for (j = a->order - 1; j >= 0; j--)
	    {
	      if (ordered_attributes[j])
		{
		  option = "AFTER ";
		  old_attribute_name = db_attribute_name (ordered_attributes[j]);
		  break;
		}
	    }

	  if (j < 0)
	    {
	      option = "FIRST";
	      old_attribute_name = "";
	    }

	  SPLIT_USER_SPECIFIED_NAME (name, owner_name, class_name);

	  PRINT_OWNER_NAME (owner_name, (ctxt.is_dba_user || ctxt.is_dba_group_member), output_owner,
			    sizeof (output_owner));

	  output_ctx ("ALTER %s %s%s%s%s ADD ATTRIBUTE ", class_type, output_owner, PRINT_IDENTIFIER (class_name));

	  if (db_attribute_is_shared (a))
	    {
	      emit_attribute_def (ctxt, output_ctx, a, SHARED_ATTRIBUTE);
	    }
	  else
	    {
	      emit_attribute_def (ctxt, output_ctx, a, INSTANCE_ATTRIBUTE);
	    }
	  output_ctx (" %s", option);
	  if (old_attribute_name[0] == '\0')
	    {
	      output_ctx (";\n");
	    }
	  else
	    {
	      output_ctx (" %s%s%s;\n", PRINT_IDENTIFIER (old_attribute_name));
	    }
	}

      free (ordered_attributes);
    }
  else
    {
      SPLIT_USER_SPECIFIED_NAME (name, owner_name, class_name);

      PRINT_OWNER_NAME (owner_name, (ctxt.is_dba_user || ctxt.is_dba_group_member), output_owner,
			sizeof (output_owner));

      output_ctx ("ALTER %s %s%s%s%s ADD ATTRIBUTE\n", class_type, output_owner, PRINT_IDENTIFIER (class_name));

      for (a = first_attribute; a != NULL; a = db_attribute_next (a))
	{
	  if (db_attribute_class (a) == class_)
	    {
	      if (a != first_attribute)
		{
		  output_ctx (",\n");
		}

	      if (db_attribute_is_shared (a))
		{
		  emit_attribute_def (ctxt, output_ctx, a, SHARED_ATTRIBUTE);
		}
	      else
		{
		  emit_attribute_def (ctxt, output_ctx, a, INSTANCE_ATTRIBUTE);
		}
	    }
	}

      output_ctx (";\n");
    }

  if (unique_flag)
    {
      if (split_schema_files)
	{
	  emit_unique_def (ctxt, output_ctx, class_, class_type);
	}
      else
	{
	  emit_primary_and_unique_def (ctxt, output_ctx, class_, class_type);
	}
    }

  if (reverse_unique_flag)
    {
      emit_reverse_unique_def (ctxt, output_ctx, class_);
    }

  return true;
}


/*
 * emit_class_attributes - emit ALTER statements for the class attributes
 *    return: non-zero if something was emitted
 *    output_ctx(in/out): output context
 *    class(in): class
 *    class_type(in): class type
 */
static bool
emit_class_attributes (extract_context & ctxt, print_output & output_ctx, DB_OBJECT * class_, const char *class_type)
{
  DB_ATTRIBUTE *class_attribute_list, *first_class_attribute, *a;
  const char *name;
  char owner_name[DB_MAX_IDENTIFIER_LENGTH] = { '\0' };
  char *class_name = NULL;
  char output_owner[DB_MAX_USER_LENGTH + 4] = { '\0' };

  class_attribute_list = db_get_class_attributes (class_);
  first_class_attribute = NULL;

  for (a = class_attribute_list; a != NULL && first_class_attribute == NULL; a = db_attribute_next (a))
    {
      if (db_attribute_class (a) == class_)
	{
	  first_class_attribute = a;
	}
    }

  if (first_class_attribute != NULL)
    {
      name = db_get_class_name (class_);
      SPLIT_USER_SPECIFIED_NAME (name, owner_name, class_name);

      PRINT_OWNER_NAME (owner_name, (ctxt.is_dba_user || ctxt.is_dba_group_member), output_owner,
			sizeof (output_owner));

      output_ctx ("ALTER %s %s%s%s%s ADD CLASS ATTRIBUTE \n", class_type, output_owner, PRINT_IDENTIFIER (class_name));

      for (a = first_class_attribute; a != NULL; a = db_attribute_next (a))
	{
	  if (db_attribute_class (a) == class_)
	    {
	      if (a != first_class_attribute)
		{
		  output_ctx (",\n");
		}
	      emit_attribute_def (ctxt, output_ctx, a, CLASS_ATTRIBUTE);
	    }
	}
      output_ctx (";\n");
    }

  return (first_class_attribute != NULL);
}

static bool
emit_class_meta (print_output & output_ctx, DB_OBJECT * table)
{
  DB_ATTRIBUTE *attribute_list, *a;
  const char *table_name;
  bool first_print = true;

  table_name = db_get_class_name (table);
  output_ctx ("-- !META! %s%s%s:", PRINT_IDENTIFIER (table_name));

  attribute_list = db_get_attributes (table);
  for (a = attribute_list; a != NULL; a = db_attribute_next (a))
    {
      if (db_attribute_class (a) != table)
	{
	  continue;
	}
      if (!first_print)
	{
	  output_ctx (",");
	}
      output_ctx ("%s%s%s", PRINT_IDENTIFIER (db_attribute_name (a)));
      output_ctx ("(%d)", db_attribute_type (a));
      output_ctx ("(%d)", db_attribute_order (a));
      output_ctx ("(%d)", a->storage_order);
      output_ctx ("(%c)", db_attribute_is_shared (a) ? 'S' : 'I');
      first_print = false;
    }

  output_ctx ("\n");

  return true;
}

/*
 * emit_all_attributes - Emit both the instance and class attributes.
 *    return: non-zero if something was emmitted
 *    output_ctx(in/out): output context
 *    class(in): class to dump
 *    class_type(in): class type string
 *    has_indexes(in):
 */
static bool
emit_all_attributes (extract_context & ctxt, print_output & output_ctx, DB_OBJECT * class_, const char *class_type,
		     int *has_indexes, EMIT_STORAGE_ORDER storage_order)
{
  bool istatus, cstatus;

  istatus = emit_instance_attributes (ctxt, output_ctx, class_, class_type, has_indexes, storage_order);
  cstatus = emit_class_attributes (ctxt, output_ctx, class_, class_type);

  return istatus || cstatus;
}


/*
 * emit_method_files - emit all methods files
 *    return: void
 *    class(in): class object
 */
static void
emit_method_files (print_output & output_ctx, DB_OBJECT * class_mop)
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
		  output_ctx ("FILE");
		}
	      else
		{
		  output_ctx (",\n");
		  output_ctx ("    ");
		}
	      emit_methfile_def (output_ctx, f);
	    }
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
emit_methods (extract_context & ctxt, print_output & output_ctx, DB_OBJECT * class_, const char *class_type)
{
  DB_METHOD *method_list, *class_method_list, *m;
  DB_METHOD *first_method, *first_class_method;
  const char *name;
  char owner_name[DB_MAX_IDENTIFIER_LENGTH] = { '\0' };
  char *class_name = NULL;
  char output_owner[DB_MAX_USER_LENGTH + 4] = { '\0' };

  method_list = db_get_methods (class_);
  class_method_list = db_get_class_methods (class_);

  /* see if we have any locally defined components on either list */
  first_method = first_class_method = NULL;
  for (m = method_list; m != NULL && first_method == NULL; m = db_method_next (m))
    {
      if (db_method_class (m) == class_)
	{
	  first_method = m;
	}
    }

  for (m = class_method_list; m != NULL && first_class_method == NULL; m = db_method_next (m))
    {
      if (db_method_class (m) == class_)
	{
	  first_class_method = m;
	}
    }

  if (first_method != NULL)
    {
      name = db_get_class_name (class_);
      SPLIT_USER_SPECIFIED_NAME (name, owner_name, class_name);

      PRINT_OWNER_NAME (owner_name, (ctxt.is_dba_user || ctxt.is_dba_group_member), output_owner,
			sizeof (output_owner));

      output_ctx ("ALTER %s %s%s%s%s ADD METHOD\n", class_type, output_owner, PRINT_IDENTIFIER (class_name));

      for (m = first_method; m != NULL; m = db_method_next (m))
	{
	  if (db_method_class (m) == class_)
	    {
	      if (m != first_method)
		{
		  output_ctx (",\n");
		}
	      emit_method_def (ctxt, output_ctx, m, INSTANCE_METHOD);
	    }
	}

      output_ctx ("\n");
      emit_method_files (output_ctx, class_);
      output_ctx (";\n");
    }

  /* eventually, this may merge with the statement above */
  if (first_class_method != NULL)
    {
      name = db_get_class_name (class_);
      SPLIT_USER_SPECIFIED_NAME (name, owner_name, class_name);

      PRINT_OWNER_NAME (owner_name, (ctxt.is_dba_user || ctxt.is_dba_group_member), output_owner,
			sizeof (output_owner));

      output_ctx ("ALTER %s %s%s%s%s ADD METHOD\n", class_type, output_owner, PRINT_IDENTIFIER (class_name));

      for (m = first_class_method; m != NULL; m = db_method_next (m))
	{
	  if (db_method_class (m) == class_)
	    {
	      if (m != first_class_method)
		{
		  output_ctx (",\n");
		}
	      emit_method_def (ctxt, output_ctx, m, CLASS_METHOD);
	    }
	}

      if (first_method == NULL)
	{
	  emit_method_files (output_ctx, class_);
	}
      output_ctx (";\n");
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
	{
	  has_object = db_get_object (value) != NULL;
	}
      else if (TP_IS_SET_TYPE (DB_VALUE_TYPE (value)))
	{
	  set = db_get_set (value);
	  size = db_set_size (set);
	  type = db_set_type (set);

	  for (i = 0; i < size && !has_object; i++)
	    {
	      if (type == DB_TYPE_SEQUENCE)
		{
		  error = db_seq_get (set, i, &setval);
		}
	      else
		{
		  error = db_set_get (set, i, &setval);
		}

	      if (error)
		{
		  /*
		   * shouldn't happen, return 1 so we don't try to dump this
		   * value
		   */
		  has_object = 1;
		}
	      else
		{
		  has_object = ex_contains_object_reference (&setval);
		}

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
emit_attribute_def (extract_context & ctxt, print_output & output_ctx, DB_ATTRIBUTE * attribute,
		    ATTRIBUTE_QUALIFIER qualifier)
{
  DB_VALUE *default_value;
  const char *name;

  name = db_attribute_name (attribute);
  switch (qualifier)
    {
    case CLASS_ATTRIBUTE:
      /*
       * NOTE: The parser no longer recognizes a CLASS prefix for class
       * attributes, this will have been encoded in the surrounding
       * "ADD CLASS ATTRIBUTE" clause
       */
    case INSTANCE_ATTRIBUTE:
    case SHARED_ATTRIBUTE:
      {
	if (strchr (name, ']') != NULL)
	  {
	    output_ctx ("       %s%s%s ", PRINT_IDENTIFIER_WITH_QUOTE (name));
	  }
	else
	  {
	    output_ctx ("       %s%s%s ", PRINT_IDENTIFIER (name));
	  }
	break;
      }
    }

  emit_domain_def (ctxt, output_ctx, db_attribute_domain (attribute));

  if (emit_autoincrement_def (output_ctx, attribute) != NO_ERROR)
    {
      ;				/* just continue */
    }

  if (qualifier == SHARED_ATTRIBUTE)
    {
      output_ctx (" SHARED ");
    }

  default_value = db_attribute_default (attribute);
  if ((default_value != NULL && !DB_IS_NULL (default_value))
      || attribute->default_value.default_expr.default_expr_type != DB_DEFAULT_NONE)
    {
      const char *default_expr_type_str;

      if (qualifier != SHARED_ATTRIBUTE)
	{
	  output_ctx (" DEFAULT ");
	}

      if (attribute->default_value.default_expr.default_expr_op == T_TO_CHAR)
	{
	  output_ctx ("TO_CHAR(");
	}

      default_expr_type_str = db_default_expression_string (attribute->default_value.default_expr.default_expr_type);
      if (default_expr_type_str != NULL)
	{
	  output_ctx ("%s", default_expr_type_str);
	}
      else
	{
	  /* these are set during the object load phase */
	  if (ex_contains_object_reference (default_value))
	    {
	      output_ctx ("NULL");
	    }
	  else
	    {
	      /* use the desc_ printer, need to have this in a better place */
	      desc_value_print (output_ctx, default_value);
	    }
	}

      if (attribute->default_value.default_expr.default_expr_op == T_TO_CHAR)
	{
	  if (attribute->default_value.default_expr.default_expr_format != NULL)
	    {
	      output_ctx (", \'");
	      output_ctx ("%s", attribute->default_value.default_expr.default_expr_format);
	      output_ctx ("\'");
	    }

	  output_ctx (")");
	}
    }

  if (attribute->on_update_default_expr != DB_DEFAULT_NONE)
    {
      const char *default_expr_type_str;

      output_ctx (" ON UPDATE ");

      default_expr_type_str = db_default_expression_string (attribute->on_update_default_expr);
      if (default_expr_type_str != NULL)
	{
	  output_ctx ("%s", default_expr_type_str);
	}
    }

  /* emit constraints */
  if (db_attribute_is_non_null (attribute))
    {
      output_ctx (" NOT NULL");
    }

  /* emit comment */
  if (attribute->comment != NULL && attribute->comment[0] != '\0')
    {
      output_ctx (" ");
      help_print_describe_comment (output_ctx, attribute->comment);
    }
}

/*
 * emit_unique_def - emit the unique constraint definitions for this class
 *    return: void
 *    class(in): the class to emit the attributes for
 */
static void
emit_unique_def (extract_context & ctxt, print_output & output_ctx, DB_OBJECT * class_, const char *class_type)
{
  DB_CONSTRAINT *constraint_list, *constraint;
  DB_ATTRIBUTE **atts, **att;
  bool has_inherited_atts;
  int num_printed = 0;
  const char *name, *class_name;
  char owner_name[DB_MAX_IDENTIFIER_LENGTH] = { '\0' };
  char *class_name_p = NULL;
  int not_online = 0;
  char output_owner[DB_MAX_USER_LENGTH + 4] = { '\0' };

  class_name = db_get_class_name (class_);

  /* First we must check if there is a unique one without the online index tag. */

  constraint_list = db_get_constraints (class_);
  if (constraint_list == NULL)
    {
      return;
    }

  for (constraint = constraint_list; constraint != NULL && not_online == 0;
       constraint = db_constraint_next (constraint))
    {
      if (db_constraint_type (constraint) != DB_CONSTRAINT_UNIQUE)
	{
	  continue;
	}

      if (constraint->index_status == SM_ONLINE_INDEX_BUILDING_IN_PROGRESS)
	{
	  /* Skip the unique index definitions for online indexes. */
	  continue;
	}
      not_online++;
    }

  if (not_online == 0)
    {
      /* We need to return and not print anything. */
      return;
    }

  SPLIT_USER_SPECIFIED_NAME (class_name, owner_name, class_name_p);

  PRINT_OWNER_NAME (owner_name, (ctxt.is_dba_user || ctxt.is_dba_group_member), output_owner, sizeof (output_owner));

  output_ctx ("ALTER %s %s%s%s%s ADD ATTRIBUTE\n", class_type, output_owner, PRINT_IDENTIFIER (class_name_p));

  for (constraint = constraint_list; constraint != NULL; constraint = db_constraint_next (constraint))
    {
      if (db_constraint_type (constraint) != DB_CONSTRAINT_UNIQUE)
	{
	  continue;
	}

      if (constraint->index_status == SM_ONLINE_INDEX_BUILDING_IN_PROGRESS)
	{
	  /* Skip the unique index definitions for online indexes. */
	  continue;
	}

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
	      output_ctx (",\n");
	    }

	  if (constraint->type == SM_CONSTRAINT_UNIQUE)
	    {
	      output_ctx ("       CONSTRAINT [%s] UNIQUE(", constraint->name);

	      int i;
	      for (att = atts, i = 0; *att != NULL; att++, i++)
		{
		  name = db_attribute_name (*att);
		  if (att != atts)
		    {
		      output_ctx (", ");
		    }

		  output_ctx ("%s%s%s", PRINT_IDENTIFIER (name));

		  if (constraint->asc_desc != NULL && constraint->asc_desc[i] != 0)
		    {
		      output_ctx ("%s", " DESC");
		    }
		}
	      output_ctx (")");

	      if (constraint->comment != NULL && constraint->comment[0] != '\0')
		{
		  output_ctx (" ");
		  help_print_describe_comment (output_ctx, constraint->comment);
		}
	      ++num_printed;
	    }
	}
    }
  output_ctx (";\n");
}

/*
 * emit_primary_key_def - emit the primary key constraint definitions for this class
 *    return: void
 *    class(in): the class to emit the attributes for
 */
static void
emit_primary_key_def (extract_context & ctxt, print_output & output_ctx, DB_OBJECT * class_, const char *class_type)
{
  DB_CONSTRAINT *constraint_list, *constraint;
  DB_ATTRIBUTE **atts, **att;
  bool has_inherited_atts;
  int num_printed = 0;
  const char *name, *class_name;
  char owner_name[DB_MAX_IDENTIFIER_LENGTH] = { '\0' };
  char *class_name_p = NULL;
  int not_online = 0;
  int i = 0;
  char output_owner[DB_MAX_USER_LENGTH + 4] = { '\0' };

  class_name = db_get_class_name (class_);

  /* First we must check if there is a unique one without the online index tag. */

  constraint_list = db_get_constraints (class_);
  if (constraint_list == NULL)
    {
      return;
    }

  for (constraint = constraint_list; constraint != NULL && not_online == 0;
       constraint = db_constraint_next (constraint))
    {
      if (db_constraint_type (constraint) != DB_CONSTRAINT_PRIMARY_KEY)
	{
	  continue;
	}

      if (constraint->index_status == SM_ONLINE_INDEX_BUILDING_IN_PROGRESS)
	{
	  /* Skip the unique index definitions for online indexes. */
	  continue;
	}
      not_online++;
    }

  if (not_online == 0)
    {
      /* We need to return and not print anything. */
      return;
    }

  output_ctx ("\n");

  SPLIT_USER_SPECIFIED_NAME (class_name, owner_name, class_name_p);

  PRINT_OWNER_NAME (owner_name, (ctxt.is_dba_user || ctxt.is_dba_group_member), output_owner, sizeof (output_owner));

  output_ctx ("ALTER %s %s%s%s%s ADD ATTRIBUTE\n", class_type, output_owner, PRINT_IDENTIFIER (class_name_p));

  for (constraint = constraint_list; constraint != NULL; constraint = db_constraint_next (constraint))
    {
      if (db_constraint_type (constraint) != DB_CONSTRAINT_UNIQUE
	  && db_constraint_type (constraint) != DB_CONSTRAINT_PRIMARY_KEY)
	{
	  continue;
	}

      if (constraint->index_status == SM_ONLINE_INDEX_BUILDING_IN_PROGRESS)
	{
	  /* Skip the unique index definitions for online indexes. */
	  continue;
	}

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
	  if (constraint->type == SM_CONSTRAINT_PRIMARY_KEY)
	    {
	      if (num_printed > 0)
		{
		  output_ctx (",\n");
		}

	      output_ctx ("       CONSTRAINT [%s] PRIMARY KEY(", constraint->name);

	      for (att = atts, i = 0; *att != NULL; att++, i++)
		{
		  name = db_attribute_name (*att);
		  if (att != atts)
		    {
		      output_ctx (", ");
		    }

		  output_ctx ("%s%s%s", PRINT_IDENTIFIER (name));

		  if (constraint->asc_desc != NULL && constraint->asc_desc[i] != 0)
		    {
		      output_ctx ("%s", " DESC");
		    }
		}
	      output_ctx (")");
	      if (constraint->comment != NULL && constraint->comment[0] != '\0')
		{
		  output_ctx (" ");
		  help_print_describe_comment (output_ctx, constraint->comment);
		}
	      ++num_printed;
	    }
	}
    }
  output_ctx (";\n");
}

/*
 * emit_primary_and_unique_def - emit the primary key and unique constraint definitions for this class
 *    return: void
 *    class(in): the class to emit the attributes for
 */
static void
emit_primary_and_unique_def (extract_context & ctxt, print_output & output_ctx, DB_OBJECT * class_,
			     const char *class_type)
{
  DB_CONSTRAINT *constraint_list, *constraint;
  DB_ATTRIBUTE **atts, **att;
  bool has_inherited_atts;
  int num_printed = 0;
  const char *name, *class_name;
  char owner_name[DB_MAX_IDENTIFIER_LENGTH] = { '\0' };
  char *class_name_p = NULL;
  int not_online = 0;
  char output_owner[DB_MAX_USER_LENGTH + 4] = { '\0' };

  class_name = db_get_class_name (class_);

  /* First we must check if there is a unique one without the online index tag. */

  constraint_list = db_get_constraints (class_);
  if (constraint_list == NULL)
    {
      return;
    }

  for (constraint = constraint_list; constraint != NULL && not_online == 0;
       constraint = db_constraint_next (constraint))
    {
      if (db_constraint_type (constraint) != DB_CONSTRAINT_UNIQUE
	  && db_constraint_type (constraint) != DB_CONSTRAINT_PRIMARY_KEY)
	{
	  continue;
	}

      if (constraint->index_status == SM_ONLINE_INDEX_BUILDING_IN_PROGRESS)
	{
	  /* Skip the unique index definitions for online indexes. */
	  continue;
	}
      not_online++;
    }

  if (not_online == 0)
    {
      /* We need to return and not print anything. */
      return;
    }

  SPLIT_USER_SPECIFIED_NAME (class_name, owner_name, class_name_p);

  PRINT_OWNER_NAME (owner_name, (ctxt.is_dba_user || ctxt.is_dba_group_member), output_owner, sizeof (output_owner));

  output_ctx ("ALTER %s %s%s%s%s ADD ATTRIBUTE\n", class_type, output_owner, PRINT_IDENTIFIER (class_name_p));

  for (constraint = constraint_list; constraint != NULL; constraint = db_constraint_next (constraint))
    {
      if (db_constraint_type (constraint) != DB_CONSTRAINT_UNIQUE
	  && db_constraint_type (constraint) != DB_CONSTRAINT_PRIMARY_KEY)
	{
	  continue;
	}

      if (constraint->index_status == SM_ONLINE_INDEX_BUILDING_IN_PROGRESS)
	{
	  /* Skip the unique index definitions for online indexes. */
	  continue;
	}

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
	      output_ctx (",\n");
	    }

	  if (constraint->type == SM_CONSTRAINT_PRIMARY_KEY)
	    {
	      output_ctx ("       CONSTRAINT [%s] PRIMARY KEY(", constraint->name);
	    }
	  else
	    {
	      output_ctx ("       CONSTRAINT [%s] UNIQUE(", constraint->name);
	    }

	  int i;
	  for (att = atts, i = 0; *att != NULL; att++, i++)
	    {
	      name = db_attribute_name (*att);
	      if (att != atts)
		{
		  output_ctx (", ");
		}

	      output_ctx ("%s%s%s", PRINT_IDENTIFIER (name));

	      if (constraint->asc_desc != NULL && constraint->asc_desc[i] != 0)
		{
		  output_ctx ("%s", " DESC");
		}
	    }
	  output_ctx (")");

	  if (constraint->comment != NULL && constraint->comment[0] != '\0')
	    {
	      output_ctx (" ");
	      help_print_describe_comment (output_ctx, constraint->comment);
	    }

	  ++num_printed;
	}
    }

  output_ctx (";\n");
}

/*
 * emit_reverse_unique_def - emit a reverse unique index definition query part
 *    return: void
 *    class(in): class object
 */
static void
emit_reverse_unique_def (extract_context & ctxt, print_output & output_ctx, DB_OBJECT * class_)
{
  DB_CONSTRAINT *constraint_list, *constraint;
  DB_ATTRIBUTE **atts, **att;
  bool has_inherited_atts;
  const char *name;
  char owner_name[DB_MAX_IDENTIFIER_LENGTH] = { '\0' };
  char *class_name = NULL;
  char output_owner[DB_MAX_USER_LENGTH + 4] = { '\0' };

  constraint_list = db_get_constraints (class_);
  if (constraint_list == NULL)
    {
      return;
    }

  for (constraint = constraint_list; constraint != NULL; constraint = db_constraint_next (constraint))
    {
      if (db_constraint_type (constraint) != DB_CONSTRAINT_REVERSE_UNIQUE)
	{
	  continue;
	}

      if (constraint->index_status == SM_ONLINE_INDEX_BUILDING_IN_PROGRESS)
	{
	  /* We skip definitions for unique indexes during online loading. */
	  continue;
	}

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
	  SPLIT_USER_SPECIFIED_NAME (name, owner_name, class_name);

	  PRINT_OWNER_NAME (owner_name, (ctxt.is_dba_user || ctxt.is_dba_group_member), output_owner,
			    sizeof (output_owner));

	  output_ctx ("CREATE REVERSE UNIQUE INDEX %s%s%s on %s%s%s%s (", PRINT_IDENTIFIER (constraint->name),
		      output_owner, PRINT_IDENTIFIER (class_name));

	  for (att = atts; *att != NULL; att++)
	    {
	      name = db_attribute_name (*att);
	      if (att != atts)
		{
		  output_ctx (", ");
		}
	      output_ctx ("%s%s%s", PRINT_IDENTIFIER (name));

	      // reverse unique does not care for direction of the column.
	    }
	  output_ctx (");\n");
	}
    }
}


/*
 * emit_index_def - emit the index constraint definitions for this class
 *    return: void
 *    class(in): the class to emit the indexes for
 */
static void
emit_index_def (extract_context & ctxt, print_output & output_ctx, DB_OBJECT * class_)
{
  DB_CONSTRAINT *constraint_list, *constraint;
  DB_CONSTRAINT_TYPE ctype;
  DB_ATTRIBUTE **atts, **att;
  const char *cls_name, *att_name;
  char owner_name[DB_MAX_IDENTIFIER_LENGTH] = { '\0' };
  char *class_name = NULL;
  int partitioned_subclass = 0, au_save;
  SM_CLASS *supclass = NULL;
  const int *asc_desc;
  const int *prefix_length;
  int k, n_attrs = 0;
  char output_owner[DB_MAX_USER_LENGTH + 4] = { '\0' };
#if defined(SUPPORT_COMPRESS_MODE)
  char reserved_col_buf[RESERVED_INDEX_ATTR_NAME_BUF_SIZE] = { 0x00, };
#endif

  constraint_list = db_get_constraints (class_);
  if (constraint_list == NULL)
    {
      return;
    }

  cls_name = db_get_class_name (class_);
  if (cls_name != NULL)
    {
      partitioned_subclass = do_is_partitioned_subclass (NULL, cls_name, NULL);
    }
  else
    {
      cls_name = "";
    }

  if (partitioned_subclass)
    {
      DB_OBJECT *root_op = NULL;
      AU_DISABLE (au_save);
      if (do_get_partition_parent (class_, &root_op) == NO_ERROR)
	{
	  if (au_fetch_class (root_op, &supclass, AU_FETCH_READ, AU_SELECT) != NO_ERROR)
	    {
	      supclass = NULL;
	    }
	}

      AU_ENABLE (au_save);
    }

  for (constraint = constraint_list; constraint != NULL; constraint = db_constraint_next (constraint))
    {
      ctype = db_constraint_type (constraint);
      if ((constraint->index_status != SM_ONLINE_INDEX_BUILDING_IN_PROGRESS)
	  && (ctype != DB_CONSTRAINT_INDEX && ctype != DB_CONSTRAINT_REVERSE_INDEX))
	{
	  continue;
	}

      if (supclass && classobj_find_class_index (supclass, constraint->name) != NULL)
	{
	  continue;		/* same index skip */
	}

      SPLIT_USER_SPECIFIED_NAME (cls_name, owner_name, class_name);
      if (constraint->func_index_info)
	{
	  PRINT_OWNER_NAME (owner_name, (ctxt.is_dba_user || ctxt.is_dba_group_member), output_owner,
			    sizeof (output_owner));

	  output_ctx ("CREATE %s%sINDEX %s%s%s ON %s%s%s%s (",
		      (ctype == DB_CONSTRAINT_REVERSE_INDEX
		       || ctype == DB_CONSTRAINT_REVERSE_UNIQUE) ? "REVERSE " : "", (ctype == DB_CONSTRAINT_UNIQUE
										     || ctype ==
										     DB_CONSTRAINT_REVERSE_UNIQUE) ?
		      "UNIQUE " : "", PRINT_FUNCTION_INDEX_NAME (constraint->name), output_owner,
		      PRINT_IDENTIFIER (class_name));
	}
      else
	{
	  PRINT_OWNER_NAME (owner_name, (ctxt.is_dba_user || ctxt.is_dba_group_member), output_owner,
			    sizeof (output_owner));

	  output_ctx ("CREATE %s%sINDEX %s%s%s ON %s%s%s%s (",
		      (ctype == DB_CONSTRAINT_REVERSE_INDEX
		       || ctype == DB_CONSTRAINT_REVERSE_UNIQUE) ? "REVERSE " : "", (ctype == DB_CONSTRAINT_UNIQUE
										     || ctype ==
										     DB_CONSTRAINT_REVERSE_UNIQUE) ?
		      "UNIQUE " : "", PRINT_IDENTIFIER (constraint->name), output_owner, PRINT_IDENTIFIER (class_name));
	}

      asc_desc = NULL;		/* init */
      prefix_length = NULL;
      if (ctype == DB_CONSTRAINT_INDEX)
	{			/* is not reverse index */
	  /* need to get asc/desc info */
	  asc_desc = db_constraint_asc_desc (constraint);
	  prefix_length = db_constraint_prefix_length (constraint);
	}
      else if (ctype == DB_CONSTRAINT_UNIQUE)
	{			/* is not reverse unique index */
	  /* need to get asc/desc info */
	  asc_desc = db_constraint_asc_desc (constraint);
	}

      atts = db_constraint_attributes (constraint);

      if (constraint->func_index_info)
	{
	  n_attrs = constraint->func_index_info->attr_index_start + 1;
	}
      else
	{
	  n_attrs = 0;
	  for (att = atts; *att != NULL; att++)
	    {
	      n_attrs++;
	    }
	}

#if defined(SUPPORT_COMPRESS_MODE)
      reserved_col_buf[0] = '\0';
      if ((n_attrs > 1) && IS_RESERVED_INDEX_ATTR_ID (atts[n_attrs - 1]->id))
	{
	  dk_print_reserved_index_info (reserved_col_buf, sizeof (reserved_col_buf), COMPRESS_INDEX_MODE_SET,
					GET_RESERVED_INDEX_ATTR_LEVEL (atts[n_attrs - 1]->id));
	  n_attrs--;		/* Hidden column should not be displayed. */
	}
      else if (!DB_IS_CONSTRAINT_UNIQUE_FAMILY (ctype))
	{
	  dk_print_reserved_index_info (reserved_col_buf, sizeof (reserved_col_buf), COMPRESS_INDEX_MODE_NONE,
					COMPRESS_INDEX_MOD_LEVEL_ZERO);
	}
#endif

      k = 0;
      for (att = atts; k < n_attrs; att++)
	{
	  if (constraint->func_index_info)
	    {
	      if (k == constraint->func_index_info->col_id)
		{
		  if (k > 0)
		    {
		      output_ctx (", ");
		    }
		  output_ctx ("%s", constraint->func_index_info->expr_str);
		  if (constraint->func_index_info->fi_domain->is_desc)
		    {
		      output_ctx ("%s", " DESC");
		    }

		  k++;
		  if (k == n_attrs)
		    {
		      break;
		    }
		}
	    }
	  att_name = db_attribute_name (*att);
	  if (k > 0)
	    {
	      output_ctx (", ");
	    }
	  output_ctx ("%s%s%s", PRINT_IDENTIFIER (att_name));

	  if (prefix_length)
	    {
	      if (*prefix_length >= 0)
		{
		  output_ctx (" (%d)", *prefix_length);
		}
	      prefix_length++;
	    }

	  if (asc_desc)
	    {
	      if (*asc_desc == 1)
		{
		  output_ctx ("%s", " DESC");
		}
	      asc_desc++;
	    }
	  k++;
	}

      if (constraint->filter_predicate)
	{
	  if (constraint->filter_predicate->pred_string)
	    {
	      output_ctx (") where %s", constraint->filter_predicate->pred_string);
	    }
	}
#if defined(SUPPORT_COMPRESS_MODE)
      if (reserved_col_buf[0])
	{
	  output_ctx (") %s", reserved_col_buf);
	}
#endif
      else
	{
	  output_ctx (")");
	}

      if (constraint->comment != NULL && constraint->comment[0] != '\0')
	{
	  output_ctx (" ");
	  help_print_describe_comment (output_ctx, constraint->comment);
	}

      /* Safeguard. */
      /* If it's unique then it must surely be with online flag. */
      assert ((constraint->index_status == SM_ONLINE_INDEX_BUILDING_IN_PROGRESS)
	      || (ctype != DB_CONSTRAINT_UNIQUE && ctype != DB_CONSTRAINT_REVERSE_UNIQUE));
      if (constraint->index_status == SM_ONLINE_INDEX_BUILDING_IN_PROGRESS)
	{
	  output_ctx (" WITH ONLINE");
	}
      output_ctx (";\n");
    }
}


/*
 * emit_domain_def - emit a domain defintion part
 *    return: void
 *    domains(in): domain list
 */
static void
emit_domain_def (extract_context & ctxt, print_output & output_ctx, DB_DOMAIN * domains)
{
  DB_TYPE type;
  PR_TYPE *prtype;
  DB_DOMAIN *domain;
  DB_OBJECT *class_;
  int precision;
  int has_collation;
  const char *name;
  char owner_name[DB_MAX_IDENTIFIER_LENGTH] = { '\0' };
  char *class_name = NULL;
  const char *json_schema;
  char output_owner[DB_MAX_USER_LENGTH + 4] = { '\0' };

  for (domain = domains; domain != NULL; domain = db_domain_next (domain))
    {
      type = TP_DOMAIN_TYPE (domain);
      prtype = pr_type_from_id (type);
      if (prtype == NULL)
	{
	  continue;
	}

      if (type == DB_TYPE_OBJECT)
	{
	  class_ = db_domain_class (domain);
	  if (class_ == NULL)
	    {
	      output_ctx ("%s", prtype->name);
	    }
	  else
	    {
	      name = db_get_class_name (class_);
	      SPLIT_USER_SPECIFIED_NAME (name, owner_name, class_name);

	      PRINT_OWNER_NAME (owner_name, (ctxt.is_dba_user || ctxt.is_dba_group_member), output_owner,
				sizeof (output_owner));

	      output_ctx ("%s%s%s%s", output_owner, PRINT_IDENTIFIER (class_name));
	    }
	}
      else
	{
	  has_collation = 0;
	  output_ctx ("%s", prtype->name);

	  switch (type)
	    {
	    case DB_TYPE_VARCHAR:
	    case DB_TYPE_CHAR:
	    case DB_TYPE_NCHAR:
	    case DB_TYPE_VARNCHAR:
	      has_collation = 1;
	      /* FALLTHRU */
	    case DB_TYPE_BIT:
	    case DB_TYPE_VARBIT:
	      precision = db_domain_precision (domain);
	      output_ctx ("(%d)", precision == TP_FLOATING_PRECISION_VALUE ? DB_MAX_STRING_LENGTH : precision);
	      break;

	    case DB_TYPE_ENUMERATION:
	      {
		int i = 0;
		DB_ENUM_ELEMENT *elem = NULL;
		int count = DOM_GET_ENUM_ELEMS_COUNT (domain);

		if (count == 0)
		  {
		    /* empty enumeration */
		    output_ctx ("()");
		    break;
		  }

		output_ctx ("(");
		for (i = 1; i < count; i++)
		  {
		    elem = &DOM_GET_ENUM_ELEM (domain, i);
		    output_ctx ("'%s', ", DB_GET_ENUM_ELEM_STRING (elem));
		  }
		elem = &DOM_GET_ENUM_ELEM (domain, count);
		output_ctx ("'%s')", DB_GET_ENUM_ELEM_STRING (elem));
		has_collation = 1;
		break;
	      }

	    case DB_TYPE_NUMERIC:
	      output_ctx ("(%d,%d)", db_domain_precision (domain), db_domain_scale (domain));
	      break;

	    case DB_TYPE_SET:
	    case DB_TYPE_MULTISET:
	    case DB_TYPE_SEQUENCE:
	      output_ctx ("(");
	      emit_domain_def (ctxt, output_ctx, db_domain_set (domain));
	      output_ctx (")");
	      break;

	    case DB_TYPE_JSON:
	      json_schema = db_domain_raw_json_schema (domain);
	      if (json_schema != NULL)
		{
		  output_ctx ("('%s')", json_schema);
		}
	      break;

	    default:
	      break;
	    }

	  if (has_collation)
	    {
	      (void) output_ctx (" COLLATE %s", lang_get_collation_name (domain->collation_id));
	    }
	}

      if (db_domain_next (domain) != NULL)
	{
	  output_ctx (",");
	}
    }
}

/*
 * emit_autoincrement_def - emit a auto-increment query part
 *    return: void
 *    attribute(in): attribute to add query part for
 */
static int
emit_autoincrement_def (print_output & output_ctx, DB_ATTRIBUTE * attribute)
{
  int error = NO_ERROR;
  DB_VALUE min_val, inc_val;
  char str_buf[NUMERIC_MAX_STRING_SIZE];

  if (attribute->auto_increment != NULL)
    {
      db_make_null (&min_val);
      db_make_null (&inc_val);

      error = db_get (attribute->auto_increment, "min_val", &min_val);
      if (error < 0)
	{
	  return error;
	}

      error = db_get (attribute->auto_increment, "increment_val", &inc_val);
      if (error < 0)
	{
	  pr_clear_value (&min_val);
	  return error;
	}

      output_ctx (" AUTO_INCREMENT(%s", numeric_db_value_print (&min_val, str_buf));
      output_ctx (", %s)", numeric_db_value_print (&inc_val, str_buf));

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
emit_method_def (extract_context & ctxt, print_output & output_ctx, DB_METHOD * method, METHOD_QUALIFIER qualifier)
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
	if (name != NULL)
	  {
	    output_ctx ("       %s%s%s(", PRINT_IDENTIFIER (name));
	  }
	break;
      }				/* case INSTANCE_METHOD */
    case CLASS_METHOD:
      {
	if (name != NULL)
	  {
	    output_ctx ("CLASS  %s%s%s(", PRINT_IDENTIFIER (name));
	  }
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
      emit_domain_def (ctxt, output_ctx, db_method_arg_domain (method, i));
      output_ctx (", ");
    }

  if (arg_count)
    {
      emit_domain_def (ctxt, output_ctx, db_method_arg_domain (method, i));
    }

  output_ctx (") ");

  /*
   * Emit method return domain
   */
  method_return_domain = db_method_return_domain (method);
  if (method_return_domain != NULL)
    {
      emit_domain_def (ctxt, output_ctx, db_method_return_domain (method));
    }

  /*
   * Emit method function implementation
   */
  method_function_name = db_method_function (method);
  if (method_function_name != NULL)
    {
      name = db_method_function (method);
      if (name != NULL)
	{
	  output_ctx (" FUNCTION %s%s%s", PRINT_IDENTIFIER (name));
	}
    }
}


/*
 * emit_methfile_def - emit method file name
 *    return: nothing
 *    methfile(in): method file
 */
static void
emit_methfile_def (print_output & output_ctx, DB_METHFILE * methfile)
{
  output_ctx ("   '%s'", db_methfile_name (methfile));
}

/*
 * emit_partition_parts - emit PARTITION query part
 *    return: void
 *    parts(in): part MOP
 *    partcnt(in): relative position of 'parts'
 */
static void
emit_partition_parts (print_output & output_ctx, SM_PARTITION * partition_info, int partcnt)
{
  DB_VALUE ele;
  int setsize, i1;

  if (partition_info == NULL)
    {
      return;
    }

  if (partcnt > 0)
    {
      output_ctx (",\n ");
    }

  output_ctx ("PARTITION %s%s%s ", PRINT_IDENTIFIER (partition_info->pname));

  switch (partition_info->partition_type)
    {
    case PT_PARTITION_RANGE:
      output_ctx (" VALUES LESS THAN ");
      if (!set_get_element_nocopy (partition_info->values, 1, &ele))
	{			/* 0:MIN, 1:MAX */
	  if (DB_IS_NULL (&ele))
	    {
	      output_ctx ("MAXVALUE");
	    }
	  else
	    {
	      output_ctx ("(");
	      desc_value_print (output_ctx, &ele);
	      output_ctx (")");
	    }
	}
      break;
    case PT_PARTITION_LIST:
      output_ctx (" VALUES IN (");
      setsize = set_size (partition_info->values);

      for (i1 = 0; i1 < setsize; i1++)
	{
	  if (i1 > 0)
	    {
	      output_ctx (", ");
	    }

	  if (!set_get_element_nocopy (partition_info->values, i1, &ele))
	    {
	      desc_value_print (output_ctx, &ele);
	    }
	}

      output_ctx (")");
      break;
    }

  if (partition_info->comment != NULL && partition_info->comment[0] != '\0')
    {
      output_ctx (" ");
      help_print_describe_comment (output_ctx, partition_info->comment);
    }
}

/*
 * emit_partition_info - emit PARTINTION query for a class
 *    return: void
 *    clsobj(in): class object
 */
static void
emit_partition_info (extract_context & ctxt, print_output & output_ctx, MOP clsobj)
{
  DB_VALUE ele;
  int partcnt = 0;
  char *ptr, *ptr2;
  const char *name;
  char owner_name[DB_MAX_IDENTIFIER_LENGTH] = { '\0' };
  char *class_name = NULL;
  SM_CLASS *class_, *subclass;
  DB_OBJLIST *user;
  char output_owner[DB_MAX_USER_LENGTH + 4] = { '\0' };

  if (clsobj == NULL)
    {
      return;
    }

  name = db_get_class_name (clsobj);
  if (au_fetch_class (clsobj, &class_, AU_FETCH_READ, AU_SELECT) != NO_ERROR)
    {
      return;
    }

  SPLIT_USER_SPECIFIED_NAME (name, owner_name, class_name);

  PRINT_OWNER_NAME (owner_name, (ctxt.is_dba_user || ctxt.is_dba_group_member), output_owner, sizeof (output_owner));

  output_ctx ("\nALTER CLASS %s%s%s%s ", output_owner, PRINT_IDENTIFIER (class_name));
  output_ctx ("\nPARTITION BY ");

  if (class_->partition->expr != NULL)
    {
      switch (class_->partition->partition_type)
	{
	case PT_PARTITION_HASH:
	  output_ctx ("HASH ( ");
	  break;
	case PT_PARTITION_RANGE:
	  output_ctx ("RANGE ( ");
	  break;
	case PT_PARTITION_LIST:
	  output_ctx ("LIST ( ");
	  break;
	}

      ptr = (char *) strstr (class_->partition->expr, "SELECT ");
      if (ptr)
	{
	  ptr2 = strstr (ptr + 7, " FROM ");
	  if (ptr2)
	    {
	      *ptr2 = 0;
	      output_ctx ("%s", ptr + 7);
	      output_ctx (" ) \n ");
	    }
	}

      if (class_->partition->partition_type == PT_PARTITION_HASH)
	{
	  if (!set_get_element_nocopy (class_->partition->values, 1, &ele))
	    {
	      output_ctx (" PARTITIONS %d", db_get_int (&ele));
	    }
	}
      else
	{
	  output_ctx (" ( ");
	  for (user = class_->users; user != NULL; user = user->next)
	    {
	      if (au_fetch_class (user->op, &subclass, AU_FETCH_READ, AU_SELECT) == NO_ERROR)
		{
		  if (subclass->partition)
		    {
		      emit_partition_parts (output_ctx, subclass->partition, partcnt);
		      partcnt++;
		    }
		}
	    }
	  output_ctx (" ) ");
	}
    }
  else
    {
      /* FIXME */
    }
  output_ctx (";\n");
}

/*
 * emit_stored_procedure_args - emit stored procedure arguments
 *    return: 0 if success, error count otherwise
 *    arg_cnt(in): argument count
 *    arg_set(in): set containg argument DB_VALUE
 */
static int
emit_stored_procedure_args (print_output & output_ctx, int arg_cnt, DB_SET * arg_set)
{
  MOP arg;
  DB_VALUE arg_val, arg_name_val, arg_mode_val, arg_type_val, arg_comment_val;
  int arg_mode, arg_type, i, save;
  int err;
  int err_count = 0;

  AU_DISABLE (save);

  for (i = 0; i < arg_cnt; i++)
    {
      err = set_get_element (arg_set, i, &arg_val);
      if (err != NO_ERROR)
	{
	  err_count++;
	  continue;
	}

      arg = db_get_object (&arg_val);

      if ((err = db_get (arg, SP_ATTR_ARG_NAME, &arg_name_val)) != NO_ERROR
	  || (err = db_get (arg, SP_ATTR_MODE, &arg_mode_val)) != NO_ERROR
	  || (err = db_get (arg, SP_ATTR_DATA_TYPE, &arg_type_val)) != NO_ERROR
	  || (err = db_get (arg, SP_ATTR_ARG_COMMENT, &arg_comment_val)) != NO_ERROR)
	{
	  err_count++;
	  continue;
	}

      output_ctx ("%s%s%s ", PRINT_IDENTIFIER (db_get_string (&arg_name_val)));

      arg_mode = db_get_int (&arg_mode_val);
      output_ctx ("%s ", arg_mode == SP_MODE_IN ? "IN" : arg_mode == SP_MODE_OUT ? "OUT" : "INOUT");

      arg_type = db_get_int (&arg_type_val);

      if (arg_type == DB_TYPE_RESULTSET)
	{
	  output_ctx ("CURSOR");
	}
      else
	{
	  output_ctx ("%s", db_get_type_name ((DB_TYPE) arg_type));
	}

      if (!DB_IS_NULL (&arg_comment_val))
	{
	  output_ctx (" COMMENT ");
	  desc_value_print (output_ctx, &arg_comment_val);
	}

      if (i < arg_cnt - 1)
	{
	  output_ctx (", ");
	}

      pr_clear_value (&arg_val);
    }

  AU_ENABLE (save);
  return err_count;
}

/*
 * emit_stored_procedure - emit stored procedure
 *    return: void
 *    output_ctx(in/out): output context
 */
static int
emit_stored_procedure (extract_context & ctxt, print_output & output_ctx)
{
  MOP cls, obj, owner;
  DB_OBJLIST *sp_list = NULL, *cur_sp;
  DB_VALUE sp_name_val, sp_type_val, arg_cnt_val, args_val, rtn_type_val, method_val, comment_val;
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
	  || ((err = db_get (obj, SP_ATTR_RETURN_TYPE, &rtn_type_val)) != NO_ERROR)
	  || (err = db_get (obj, SP_ATTR_TARGET, &method_val)) != NO_ERROR
	  || (err = db_get (obj, SP_ATTR_OWNER, &owner_val)) != NO_ERROR
	  || (err = db_get (obj, SP_ATTR_COMMENT, &comment_val)) != NO_ERROR)
	{
	  err_count++;
	  continue;
	}

      if (ctxt.is_dba_user == false && ctxt.is_dba_group_member == false)
	{
	  owner = db_get_object (&owner_val);
	  err = db_get (owner, "name", &owner_name_val);
	  if (err != NO_ERROR)
	    {
	      err_count++;
	      continue;
	    }

	  if (strcasecmp (ctxt.login_user, db_get_string (&owner_name_val)) != 0)
	    {
	      continue;
	    }
	}

      sp_type = db_get_int (&sp_type_val);
      output_ctx ("\nCREATE %s", sp_type == SP_TYPE_PROCEDURE ? "PROCEDURE" : "FUNCTION");

      output_ctx (" %s%s%s (", PRINT_IDENTIFIER (db_get_string (&sp_name_val)));

      arg_cnt = db_get_int (&arg_cnt_val);
      arg_set = db_get_set (&args_val);
      if (emit_stored_procedure_args (output_ctx, arg_cnt, arg_set) > 0)
	{
	  err_count++;
	  output_ctx (";\n");
	  continue;
	}
      output_ctx (") ");

      if (sp_type == SP_TYPE_FUNCTION)
	{
	  rtn_type = db_get_int (&rtn_type_val);

	  if (rtn_type == DB_TYPE_RESULTSET)
	    {
	      output_ctx ("RETURN CURSOR ");
	    }
	  else
	    {
	      output_ctx ("RETURN %s ", db_get_type_name ((DB_TYPE) rtn_type));
	    }
	}

      output_ctx ("AS LANGUAGE JAVA NAME '%s'", db_get_string (&method_val));

      if (!DB_IS_NULL (&comment_val))
	{
	  output_ctx (" COMMENT ");
	  desc_value_print (output_ctx, &comment_val);
	}

      output_ctx (";\n");

      owner = db_get_object (&owner_val);
      err = db_get (owner, "name", &owner_name_val);
      if (err != NO_ERROR)
	{
	  err_count++;
	  continue;
	}

      if (ctxt.is_dba_user || ctxt.is_dba_group_member)
	{
	  output_ctx ("call [change_sp_owner]('%s', '%s') on class [db_root];\n", db_get_string (&sp_name_val),
		      db_get_string (&owner_name_val));
	}

      db_value_clear (&owner_name_val);
    }

  db_objlist_free (sp_list);
  AU_ENABLE (save);

  return err_count;
}

/*
 * emit_foreign_key - emit foreign key
 *    return: NO_ERROR if successful, error code otherwise
 *    output_ctx(in/out): output context
 *    classes(in): MOP list for dump foreign key
 */
static int
emit_foreign_key (extract_context & ctxt, print_output & output_ctx, DB_OBJLIST * classes)
{
  DB_OBJLIST *cl;
  DB_CONSTRAINT *constraint_list, *constraint;
  DB_ATTRIBUTE **atts, **att;
  bool has_inherited_atts;
  const char *cls_name, *att_name;
  char owner_name[DB_MAX_IDENTIFIER_LENGTH] = { '\0' };
  char *class_name = NULL;
  MOP ref_clsop;
  char output_owner[DB_MAX_USER_LENGTH + 4] = { '\0' };
#if defined(SUPPORT_COMPRESS_MODE)
  char reserved_col_buf[RESERVED_INDEX_ATTR_NAME_BUF_SIZE] = { 0x00, };
#endif

  for (cl = classes; cl != NULL; cl = cl->next)
    {
      constraint_list = db_get_constraints (cl->op);
      cls_name = db_get_class_name (cl->op);

      for (constraint = constraint_list; constraint != NULL; constraint = db_constraint_next (constraint))
	{
	  if (db_constraint_type (constraint) != DB_CONSTRAINT_FOREIGN_KEY)
	    {
	      continue;
	    }

	  atts = db_constraint_attributes (constraint);
	  has_inherited_atts = false;
	  for (att = atts; *att != NULL; att++)
	    {
	      if (db_attribute_class (*att) != cl->op)
		{
#if defined(SUPPORT_COMPRESS_MODE)	// ctshim
		  att_name = db_attribute_name (*att);
		  if (IS_RESERVED_INDEX_ATTR_NAME (att_name))
		    {
		      break;
		    }
#endif
		  has_inherited_atts = true;
		  break;
		}
	    }

	  if (has_inherited_atts)
	    {
	      continue;
	    }

	  SPLIT_USER_SPECIFIED_NAME (cls_name, owner_name, class_name);

	  PRINT_OWNER_NAME (owner_name, (ctxt.is_dba_user || ctxt.is_dba_group_member), output_owner,
			    sizeof (output_owner));

	  output_ctx ("ALTER CLASS %s%s%s%s ADD", output_owner, PRINT_IDENTIFIER (class_name));
	  output_ctx (" CONSTRAINT [%s] FOREIGN KEY(", constraint->name);

#if defined(SUPPORT_COMPRESS_MODE)
	  reserved_col_buf[0] = '\0';
#endif
	  for (att = atts; *att != NULL; att++)
	    {
	      att_name = db_attribute_name (*att);
#if defined(SUPPORT_COMPRESS_MODE)
	      if (IS_RESERVED_INDEX_ATTR_NAME (att_name))
		{
		  int level;
		  int mode = COMPRESS_INDEX_MODE_SET;

		  assert (att[1] == NULL);

		  GET_RESERVED_INDEX_ATTR_MODE_LEVEL_FROM_NAME (att_name, level);
		  dk_print_reserved_index_info (reserved_col_buf, sizeof (reserved_col_buf), mode, level);
		  break;
		}
#endif
	      if (att != atts)
		{
		  output_ctx (", ");
		}

	      output_ctx ("%s%s%s", PRINT_IDENTIFIER (att_name));
	    }
	  output_ctx (")");

	  ref_clsop = ws_mop (&(constraint->fk_info->ref_class_oid), NULL);
	  SPLIT_USER_SPECIFIED_NAME (db_get_class_name (ref_clsop), owner_name, class_name);

#if defined(SUPPORT_COMPRESS_MODE)	// ctshim
	  if (reserved_col_buf[0] == '\0')
	    {
	      dk_print_reserved_index_info (reserved_col_buf, sizeof (reserved_col_buf), COMPRESS_INDEX_MODE_NONE,
					    COMPRESS_INDEX_MOD_LEVEL_ZERO);
	    }
	  output_ctx (" %s", reserved_col_buf);
#endif
	  PRINT_OWNER_NAME (owner_name, (ctxt.is_dba_user || ctxt.is_dba_group_member), output_owner,
			    sizeof (output_owner));

	  output_ctx (" REFERENCES %s%s%s%s ", output_owner, PRINT_IDENTIFIER (class_name));
	  output_ctx ("ON DELETE %s ", classobj_describe_foreign_key_action (constraint->fk_info->delete_action));
	  output_ctx ("ON UPDATE %s ", classobj_describe_foreign_key_action (constraint->fk_info->update_action));

	  if (constraint->comment != NULL && constraint->comment[0] != '\0')
	    {
	      output_ctx (" ");
	      help_print_describe_comment (output_ctx, constraint->comment);
	    }

	  (void) output_ctx (";\n\n");
	}
    }

  return NO_ERROR;
}

static int
export_server (extract_context & ctxt, print_output & output_ctx)
{
  int error = NO_ERROR;
  int i;
  DB_QUERY_RESULT *query_result;
  DB_QUERY_ERROR query_error;
#define SERVER_VALUE_INDEX_MAX   (10)
  DB_VALUE values[SERVER_VALUE_INDEX_MAX];
  DB_VALUE passwd_val;
  char *srv_name, *owner_name, *str;
  char *uppercase_user = NULL;
  size_t uppercase_user_size = 0;
  size_t query_size = 0;
  char *query = NULL;
  char output_owner[DB_MAX_USER_LENGTH + 4] = { '\0' };

  const char *attr_names[SERVER_VALUE_INDEX_MAX] = {
    "link_name", "host", "port", "db_name", "user_name", "password", "properties", "comment", "owner_name", "owner_obj"
  };

  const char *query_all =
    "SELECT [link_name], [host], [port], [db_name], [user_name], [password], [properties], [comment],"
    "[owner].[name] [owner_name], [owner] [owner_obj] FROM [_db_server] WHERE [link_name] IS NOT NULL";

  const char *query_user =
    "SELECT [link_name], [host], [port], [db_name], [user_name], [password], [properties], [comment],"
    "[owner].[name] [owner_name], [owner] [owner_obj] FROM [_db_server] WHERE [link_name] IS NOT NULL and [owner].[name]='%s'";

  output_ctx ("\n");

  if (ctxt.is_dba_user == false && ctxt.is_dba_group_member == false)
    {
      uppercase_user_size = intl_identifier_upper_string_size (ctxt.login_user);
      uppercase_user = (char *) malloc (uppercase_user_size + 1);
      if (uppercase_user == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, uppercase_user_size);
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}

      intl_identifier_upper (ctxt.login_user, uppercase_user);

      query_size = strlen (query_user) + strlen (uppercase_user) + 1;
      query = (char *) malloc (query_size);
      if (query_user == NULL)
	{
	  if (uppercase_user != NULL)
	    {
	      free_and_init (uppercase_user);
	    }

	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, query_size);
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}

      sprintf (query, query_user, uppercase_user);
    }

  PARSER_CONTEXT *parser = parser_create_parser ();
  if (parser == NULL)
    {
      fprintf (stderr, "Failed to parser_create_parser().\n");
      return ER_FAILED;
    }

  db_make_null (&passwd_val);
  for (i = 0; i < SERVER_VALUE_INDEX_MAX; i++)
    {
      db_make_null (&values[i]);
    }

  int au_save;
  AU_DISABLE (au_save);

  error = db_compile_and_execute_local (((query == NULL) ? query_all : query), &query_result, &query_error);
  if (error <= 0)
    {
      goto err;
    }

  error = db_query_first_tuple (query_result);
  if (error != DB_CURSOR_SUCCESS)
    {
      goto err;
    }

  do
    {
      for (i = 0; i < SERVER_VALUE_INDEX_MAX; i++)
	{
	  error = db_query_get_tuple_value_by_name (query_result, (char *) attr_names[i], &values[i]);
	  if (error != NO_ERROR)
	    {
	      goto err;
	    }
	}

      if (au_is_server_authorized_user (values + (SERVER_VALUE_INDEX_MAX - 1)))
	{
	  srv_name = (char *) db_get_string (values + 0);
	  str = (char *) db_get_string (values + 5);
	  error = pt_remake_dblink_password (str, &passwd_val, true);
	  if (error != NO_ERROR)
	    {			// TODO: error handling       
	      if (er_errid_if_has_error () != NO_ERROR)
		{
		  fprintf (stderr, "Failed to re-encryption password for %s. error=%d(%s)\n",
			   srv_name, error, (char *) er_msg ());
		}
	      else
		{
		  fprintf (stderr, "Failed to re-encryption password for %s. error=%d\n", srv_name, error);
		}
	    }
	  else
	    {
	      owner_name = (char *) db_get_string (values + 8);
	      PRINT_OWNER_NAME (owner_name, (ctxt.is_dba_user || ctxt.is_dba_group_member), output_owner,
				sizeof (output_owner));

	      output_ctx ("CREATE SERVER %s[%s] (", output_owner, srv_name);
	      output_ctx ("\n\t HOST= '%s'", (char *) db_get_string (values + 1));
	      output_ctx (",\n\t PORT= %d", db_get_int (values + 2));

	      output_ctx (",\n\t DBNAME= ");
	      desc_value_print (output_ctx, values + 3);

	      output_ctx (",\n\t USER= ");
	      desc_value_print (output_ctx, values + 4);

	      output_ctx (",\n\t PASSWORD= '%s'", (char *) db_get_string (&passwd_val));

	      str = (char *) db_get_string (values + 6);
	      if (str)
		{
		  output_ctx (",\n\t PROPERTIES= '%s'", str);
		}

	      str = (char *) db_get_string (values + 7);
	      if (str)
		{
		  output_ctx (",\n\t COMMENT= ");
		  desc_value_print (output_ctx, values + 7);
		}
	      output_ctx (" );\n");
	    }

	  db_value_clear (&passwd_val);
	  db_make_null (&passwd_val);
	}

      for (i = 0; i < SERVER_VALUE_INDEX_MAX; i++)
	{
	  db_value_clear (&values[i]);
	  db_make_null (&values[i]);
	}
    }
  while (db_query_next_tuple (query_result) == DB_CURSOR_SUCCESS);

  error = NO_ERROR;

err:
  parser_free_parser (parser);
  db_query_end (query_result);

  db_value_clear (&passwd_val);
  if (error != NO_ERROR)
    {
      if (er_has_error ())
	{
	  fprintf (stderr, "Failed: %s\n", er_msg ());
	}

      for (i = 0; i < SERVER_VALUE_INDEX_MAX; i++)
	{
	  db_value_clear (&values[i]);
	}
    }

  if (uppercase_user != NULL)
    {
      free_and_init (uppercase_user);
    }

  AU_ENABLE (au_save);
  return error;
}


int
create_filename_schema (const char *output_dirname, const char *output_prefix,
			char *output_filename_p, const size_t filename_size)
{
  return create_filename (output_dirname, output_prefix, SCHEMA_NAME, output_filename_p, filename_size);
}

int
create_filename_trigger (const char *output_dirname, const char *output_prefix,
			 char *output_filename_p, const size_t filename_size)
{
  return create_filename (output_dirname, output_prefix, TRIGGER_NAME, output_filename_p, filename_size);
}

int
create_filename_indexes (const char *output_dirname, const char *output_prefix,
			 char *output_filename_p, const size_t filename_size)
{
  return create_filename (output_dirname, output_prefix, INDEX_NAME, output_filename_p, filename_size);
}

static int
create_filename_schema_info (const char *output_dirname, const char *output_prefix,
			     char *output_filename_p, const size_t filename_size)
{
  return create_filename (output_dirname, output_prefix, SCHEMA_INFO, output_filename_p, filename_size);
}

static int
create_filename (const char *output_dirname, const char *output_prefix, const char *suffix,
		 char *output_filename_p, const size_t filename_size)
{
  if (output_dirname == NULL)
    {
      output_dirname = ".";
    }

  size_t total = strlen (output_dirname) + strlen (output_prefix) + strlen (suffix) + 8;

  if (total > filename_size)
    {
      return -1;
    }

  snprintf (output_filename_p, filename_size - 1, "%s/%s%s", output_dirname, output_prefix, suffix);

  return 0;
}

static int
create_filename (const char *output_dirname, const char *output_prefix, const char *infix, const char *suffix,
		 char *output_filename_p, const size_t filename_size)
{
  if (output_dirname == NULL)
    {
      output_dirname = ".";
    }

  size_t total = strlen (output_dirname) + strlen (output_prefix) + strlen (infix) + strlen (suffix) + 8;

  if (total > filename_size)
    {
      return -1;
    }

  snprintf (output_filename_p, filename_size - 1, "%s/%s%s%s", output_dirname, output_prefix, infix, suffix);

  return 0;
}

static int
extract_user (extract_context & ctxt)
{
  FILE *output_file = NULL;
  int err = NO_ERROR;
  char output_filename[PATH_MAX * 2] = { '\0' };
  char output_schema_info[PATH_MAX * 2] = { '\0' };

  if (required_class_only == true && ctxt.do_auth)
    {
      return NO_ERROR;
    }

  if (create_filename
      (ctxt.output_dirname, ctxt.output_prefix, SCHEMA_NAME, USER_SUFFIX, output_filename,
       sizeof (output_filename)) != 0)
    {
      util_log_write_errid (MSGCAT_UTIL_GENERIC_INVALID_ARGUMENT);
      return ER_FAILED;
    }

  if (snprintf
      (output_schema_info, sizeof (output_schema_info) - 1, "%s%s%s", ctxt.output_prefix, SCHEMA_NAME, USER_SUFFIX) > 0)
    {
      ctxt.schema_file_list.push_back (output_schema_info);
    }

  output_file = fopen_ex (output_filename, "w");
  if (output_file == NULL)
    {
      (void) fprintf (stderr, "%s: %s.\n\n", ctxt.exec_name, strerror (errno));
      return ER_FAILED;
    }

  file_print_output output_ctx (output_file);

  /* error is row count if not negative. */
  err = au_export_users (ctxt, output_ctx);
  if (err >= NO_ERROR)
    {
      output_ctx ("\n");
      output_ctx ("COMMIT WORK;\n");
    }

  if (output_file != NULL)
    {
      fclose (output_file);
      output_file = NULL;
    }

  return (err < 0) ? ER_FAILED : NO_ERROR;
}

static int
extract_serial (extract_context & ctxt)
{
  FILE *output_file = NULL;
  char output_filename[PATH_MAX * 2] = { '\0' };
  char output_schema_info[PATH_MAX * 2] = { '\0' };

  if (required_class_only == true)
    {
      return NO_ERROR;
    }

  if (create_filename
      (ctxt.output_dirname, ctxt.output_prefix, SCHEMA_NAME, SERIAL_SUFFIX, output_filename,
       sizeof (output_filename)) != 0)
    {
      util_log_write_errid (MSGCAT_UTIL_GENERIC_INVALID_ARGUMENT);
      return ER_FAILED;
    }

  if (snprintf
      (output_schema_info, sizeof (output_schema_info) - 1, "%s%s%s", ctxt.output_prefix, SCHEMA_NAME,
       SERIAL_SUFFIX) > 0)
    {
      ctxt.schema_file_list.push_back (output_schema_info);
    }

  output_file = fopen_ex (output_filename, "w");
  if (output_file == NULL)
    {
      (void) fprintf (stderr, "%s: %s.\n\n", ctxt.exec_name, strerror (errno));
      return ER_FAILED;
    }

  file_print_output output_ctx (output_file);

  if (export_serial (ctxt, output_ctx) != NO_ERROR)
    {
      fprintf (stderr, "%s", db_error_string (3));
      if (db_error_code () == ER_INVALID_SERIAL_VALUE)
	{
	  fprintf (stderr, " Check the value of db_serial object.\n");
	}
    }

  output_ctx ("\n");
  output_ctx ("COMMIT WORK;\n");

  if (output_file != NULL)
    {
      fclose (output_file);
      output_file = NULL;
    }

  return NO_ERROR;
}

static int
extract_synonym (extract_context & ctxt)
{
  FILE *output_file = NULL;
  char output_filename[PATH_MAX * 2] = { '\0' };
  char output_schema_info[PATH_MAX * 2] = { '\0' };

  if (create_filename
      (ctxt.output_dirname, ctxt.output_prefix, SCHEMA_NAME, SYNONYM_SUFFIX, output_filename,
       sizeof (output_filename)) != 0)
    {
      util_log_write_errid (MSGCAT_UTIL_GENERIC_INVALID_ARGUMENT);
      return ER_FAILED;
    }

  if (snprintf
      (output_schema_info, sizeof (output_schema_info) - 1, "%s%s%s", ctxt.output_prefix, SCHEMA_NAME,
       SYNONYM_SUFFIX) > 0)
    {
      ctxt.schema_file_list.push_back (output_schema_info);
    }

  output_file = fopen_ex (output_filename, "w");
  if (output_file == NULL)
    {
      (void) fprintf (stderr, "%s: %s.\n\n", ctxt.exec_name, strerror (errno));
      return ER_FAILED;
    }

  file_print_output output_ctx (output_file);

  if (export_synonym (ctxt, output_ctx) != NO_ERROR)
    {
      fprintf (stderr, "%s", db_error_string (3));
      if (db_error_code () == ER_SYNONYM_INVALID_VALUE)
	{
	  fprintf (stderr, " Check the value of _db_synonym object.\n");
	}
    }

  output_ctx ("\n");
  output_ctx ("COMMIT WORK;\n");

  if (output_file != NULL)
    {
      fclose (output_file);
      output_file = NULL;
    }

  return NO_ERROR;
}

static int
extract_procedure (extract_context & ctxt)
{
  FILE *output_file = NULL;
  int err = NO_ERROR;
  char output_filename[PATH_MAX * 2] = { '\0' };
  char output_schema_info[PATH_MAX * 2] = { '\0' };

  if (create_filename
      (ctxt.output_dirname, ctxt.output_prefix, SCHEMA_NAME, PROCEDURE_SUFFIX, output_filename,
       sizeof (output_filename)) != 0)
    {
      util_log_write_errid (MSGCAT_UTIL_GENERIC_INVALID_ARGUMENT);
      return ER_FAILED;
    }

  if (snprintf
      (output_schema_info, sizeof (output_schema_info) - 1, "%s%s%s", ctxt.output_prefix, SCHEMA_NAME,
       PROCEDURE_SUFFIX) > 0)
    {
      ctxt.schema_file_list.push_back (output_schema_info);
    }

  output_file = fopen_ex (output_filename, "w");
  if (output_file == NULL)
    {
      (void) fprintf (stderr, "%s: %s.\n\n", ctxt.exec_name, strerror (errno));
      return ER_FAILED;
    }

  file_print_output output_ctx (output_file);

  err = emit_stored_procedure (ctxt, output_ctx);
  if (err == NO_ERROR)
    {
      output_ctx ("\n");
      output_ctx ("COMMIT WORK;\n");
    }

  if (output_file != NULL)
    {
      fclose (output_file);
      output_file = NULL;
    }

  return err;
}

static int
extract_server (extract_context & ctxt)
{
  FILE *output_file = NULL;
  int err = NO_ERROR;
  char output_filename[PATH_MAX * 2] = { '\0' };
  char output_schema_info[PATH_MAX * 2] = { '\0' };

  if (create_filename
      (ctxt.output_dirname, ctxt.output_prefix, SCHEMA_NAME, SERVER_SUFFIX, output_filename,
       sizeof (output_filename)) != 0)
    {
      util_log_write_errid (MSGCAT_UTIL_GENERIC_INVALID_ARGUMENT);
      return ER_FAILED;
    }

  if (snprintf
      (output_schema_info, sizeof (output_schema_info) - 1, "%s%s%s", ctxt.output_prefix, SCHEMA_NAME,
       SERVER_SUFFIX) > 0)
    {
      ctxt.schema_file_list.push_back (output_schema_info);
    }

  output_file = fopen_ex (output_filename, "w");
  if (output_file == NULL)
    {
      (void) fprintf (stderr, "%s: %s.\n\n", ctxt.exec_name, strerror (errno));
      return ER_FAILED;
    }

  file_print_output output_ctx (output_file);

  err = export_server (ctxt, output_ctx);
  if (err == NO_ERROR)
    {
      output_ctx ("\n");
      output_ctx ("COMMIT WORK;\n");
    }

  if (output_file != NULL)
    {
      fclose (output_file);
      output_file = NULL;
    }

  return err;
}

static int
extract_class (extract_context & ctxt)
{
  FILE *output_file = NULL;
  int err = NO_ERROR;
  char output_filename[PATH_MAX * 2] = { '\0' };
  char output_schema_info[PATH_MAX * 2] = { '\0' };

  if (create_filename
      (ctxt.output_dirname, ctxt.output_prefix, SCHEMA_NAME, CLASS_SUFFIX, output_filename,
       sizeof (output_filename)) != 0)
    {
      util_log_write_errid (MSGCAT_UTIL_GENERIC_INVALID_ARGUMENT);
      return ER_FAILED;
    }

  if (snprintf
      (output_schema_info, sizeof (output_schema_info) - 1, "%s%s%s", ctxt.output_prefix, SCHEMA_NAME,
       CLASS_SUFFIX) > 0)
    {
      ctxt.schema_file_list.push_back (output_schema_info);
    }

  output_file = fopen_ex (output_filename, "w");
  if (output_file == NULL)
    {
      (void) fprintf (stderr, "%s: %s.\n\n", ctxt.exec_name, strerror (errno));
      return ER_FAILED;
    }

  file_print_output output_ctx (output_file);

  if (ctxt.classes == NULL)
    {
      err = get_classes (ctxt, output_ctx);
      if (err != NO_ERROR)
	{
	  if (output_file != NULL)
	    {
	      fclose (output_file);
	      output_file = NULL;
	    }
	}
    }

  emit_schema (ctxt, output_ctx, EXTRACT_CLASS);
  if (er_errid () == NO_ERROR)
    {
      output_ctx ("\n");
      output_ctx ("COMMIT WORK;\n");
    }
  else
    {
      err = ER_FAILED;
    }

  if (output_file != NULL)
    {
      fclose (output_file);
      output_file = NULL;
    }

  return err;
}

static int
extract_vclass (extract_context & ctxt)
{
  FILE *output_file = NULL;
  int err = NO_ERROR;
  char output_filename[PATH_MAX * 2] = { '\0' };
  char output_schema_info[PATH_MAX * 2] = { '\0' };

  if (create_filename
      (ctxt.output_dirname, ctxt.output_prefix, SCHEMA_NAME, VCLASS_SUFFIX, output_filename,
       sizeof (output_filename)) != 0)
    {
      util_log_write_errid (MSGCAT_UTIL_GENERIC_INVALID_ARGUMENT);
      return ER_FAILED;
    }

  if (snprintf
      (output_schema_info, sizeof (output_schema_info) - 1, "%s%s%s", ctxt.output_prefix, SCHEMA_NAME,
       VCLASS_SUFFIX) > 0)
    {
      ctxt.schema_file_list.push_back (output_schema_info);
    }

  output_file = fopen_ex (output_filename, "w");
  if (output_file == NULL)
    {
      (void) fprintf (stderr, "%s: %s.\n\n", ctxt.exec_name, strerror (errno));
      return ER_FAILED;
    }

  file_print_output output_ctx (output_file);

  if (ctxt.classes == NULL)
    {
      err = get_classes (ctxt, output_ctx);
      if (err != NO_ERROR)
	{
	  if (output_file != NULL)
	    {
	      fclose (output_file);
	      output_file = NULL;
	    }
	}
    }

  emit_schema (ctxt, output_ctx, EXTRACT_VCLASS);
  if (er_errid () == NO_ERROR)
    {
      output_ctx ("\n");
      output_ctx ("COMMIT WORK;\n");
    }
  else
    {
      err = ER_FAILED;
    }

  if (output_file != NULL)
    {
      fclose (output_file);
      output_file = NULL;
    }

  return err;
}

static int
extract_pk (extract_context & ctxt)
{
  FILE *output_file = NULL;
  int err = NO_ERROR;
  char output_filename[PATH_MAX * 2] = { '\0' };
  char output_schema_info[PATH_MAX * 2] = { '\0' };

  if (create_filename
      (ctxt.output_dirname, ctxt.output_prefix, SCHEMA_NAME, PK_SUFFIX, output_filename, sizeof (output_filename)) != 0)
    {
      util_log_write_errid (MSGCAT_UTIL_GENERIC_INVALID_ARGUMENT);
      return ER_FAILED;
    }

  if (snprintf
      (output_schema_info, sizeof (output_schema_info) - 1, "%s%s%s", ctxt.output_prefix, SCHEMA_NAME, PK_SUFFIX) > 0)
    {
      ctxt.schema_file_list.push_back (output_schema_info);
    }

  output_file = fopen_ex (output_filename, "w");
  if (output_file == NULL)
    {
      (void) fprintf (stderr, "%s: %s.\n\n", ctxt.exec_name, strerror (errno));
      return ER_FAILED;
    }

  file_print_output output_ctx (output_file);

  if (ctxt.classes == NULL)
    {
      err = get_classes (ctxt, output_ctx);
      if (err != NO_ERROR)
	{
	  if (output_file != NULL)
	    {
	      fclose (output_file);
	      output_file = NULL;
	    }
	}
    }

  emit_primary_key (ctxt, output_ctx, ctxt.classes);
  if (er_errid () == NO_ERROR)
    {
      output_ctx ("\n");
      output_ctx ("COMMIT WORK;\n");
    }
  else
    {
      err = ER_FAILED;
    }

  if (output_file != NULL)
    {
      fclose (output_file);
      output_file = NULL;
    }

  return err;
}

static int
extract_fk (extract_context & ctxt)
{
  FILE *output_file = NULL;
  int err = NO_ERROR;
  char output_filename[PATH_MAX * 2] = { '\0' };
  char output_schema_info[PATH_MAX * 2] = { '\0' };

  if (create_filename
      (ctxt.output_dirname, ctxt.output_prefix, SCHEMA_NAME, FK_SUFFIX, output_filename, sizeof (output_filename)) != 0)
    {
      util_log_write_errid (MSGCAT_UTIL_GENERIC_INVALID_ARGUMENT);
      return ER_FAILED;
    }

  if (snprintf
      (output_schema_info, sizeof (output_schema_info) - 1, "%s%s%s", ctxt.output_prefix, SCHEMA_NAME, FK_SUFFIX) > 0)
    {
      ctxt.schema_file_list.push_back (output_schema_info);
    }

  output_file = fopen_ex (output_filename, "w");
  if (output_file == NULL)
    {
      (void) fprintf (stderr, "%s: %s.\n\n", ctxt.exec_name, strerror (errno));
      return ER_FAILED;
    }

  file_print_output output_ctx (output_file);

  if (ctxt.classes == NULL)
    {
      err = get_classes (ctxt, output_ctx);
      if (err != NO_ERROR)
	{
	  if (output_file != NULL)
	    {
	      fclose (output_file);
	      output_file = NULL;
	    }
	}
    }

  err = emit_foreign_key (ctxt, output_ctx, ctxt.classes);
  if (err == NO_ERROR)
    {
      output_ctx ("\n");
      output_ctx ("COMMIT WORK;\n");
    }

  if (output_file != NULL)
    {
      fclose (output_file);
      output_file = NULL;
    }

  return err;
}

static int
extract_grant (extract_context & ctxt)
{
  FILE *output_file = NULL;
  int err = NO_ERROR;
  char output_filename[PATH_MAX * 2] = { '\0' };
  char output_schema_info[PATH_MAX * 2] = { '\0' };

  if (create_filename
      (ctxt.output_dirname, ctxt.output_prefix, SCHEMA_NAME, GRANT_SUFFIX, output_filename,
       sizeof (output_filename)) != 0)
    {
      util_log_write_errid (MSGCAT_UTIL_GENERIC_INVALID_ARGUMENT);
      return ER_FAILED;
    }

  if (snprintf
      (output_schema_info, sizeof (output_schema_info) - 1, "%s%s%s", ctxt.output_prefix, SCHEMA_NAME,
       GRANT_SUFFIX) > 0)
    {
      ctxt.schema_file_list.push_back (output_schema_info);
    }

  output_file = fopen_ex (output_filename, "w");
  if (output_file == NULL)
    {
      (void) fprintf (stderr, "%s: %s.\n\n", ctxt.exec_name, strerror (errno));
      return ER_FAILED;
    }

  file_print_output output_ctx (output_file);

  if (ctxt.classes == NULL)
    {
      err = get_classes (ctxt, output_ctx);
      if (err != NO_ERROR)
	{
	  if (output_file != NULL)
	    {
	      fclose (output_file);
	      output_file = NULL;
	    }
	}
    }

  err = emit_grant (ctxt, output_ctx, ctxt.classes);
  if (err == NO_ERROR)
    {
      output_ctx ("\n");
      output_ctx ("COMMIT WORK;\n");
    }

  if (output_file != NULL)
    {
      fclose (output_file);
      output_file = NULL;
    }

  return err;
}

static void
emit_primary_key (extract_context & ctxt, print_output & output_ctx, DB_OBJLIST * classes)
{
  DB_OBJLIST *cl = NULL;
  int is_vclass = 0;
  const char *class_type = NULL;
  char *class_name = NULL;
  DB_ATTRIBUTE *attribute_list = NULL, *a = NULL;
  int pk_flag = 0;

  for (cl = classes; cl != NULL; cl = cl->next)
    {
      pk_flag = 0;
      attribute_list = db_get_attributes (cl->op);

      /* see if we have an unique defined on any attribute */
      for (a = attribute_list; a != NULL; a = db_attribute_next (a))
	{
	  if (db_attribute_class (a) == cl->op)
	    {
	      if (pk_flag == 0 && db_attribute_is_unique (a))
		{
		  pk_flag = 1;
		  is_vclass = db_is_vclass (cl->op);
		  class_type = (is_vclass > 0) ? "VCLASS" : "CLASS";
		  emit_primary_key_def (ctxt, output_ctx, cl->op, class_type);
		}
	    }
	}
    }

  if (er_errid () == ER_OBJ_NO_COMPONENTS)
    {
      er_clear ();
    }
}

static int
emit_grant (extract_context & ctxt, print_output & output_ctx, DB_OBJLIST * classes)
{
  int err = NO_ERROR;
  DB_OBJLIST *cl;
  const char *name;
  int is_partitioned = 0;

  if (ctxt.do_auth)
    {
      for (cl = classes; cl != NULL; cl = cl->next)
	{
	  name = db_get_class_name (cl->op);
	  if (do_is_partitioned_subclass (&is_partitioned, name, NULL))
	    {
	      continue;
	    }
	  err = au_export_grants (ctxt, output_ctx, cl->op);
	}
    }

  return err;
}

static int
extract_split_schema_files (extract_context & ctxt)
{
  int err_count = 0;

  if (extract_user (ctxt) != NO_ERROR)
    {
      err_count++;
    }

  if (extract_serial (ctxt) != NO_ERROR)
    {
      err_count++;
    }

  if (extract_synonym (ctxt) != NO_ERROR)
    {
      err_count++;
    }

  if (extract_procedure (ctxt) != NO_ERROR)
    {
      err_count++;
    }

  if (extract_server (ctxt) != NO_ERROR)
    {
      err_count++;
    }

  if (extract_fk (ctxt) != NO_ERROR)
    {
      err_count++;
    }

  if (extract_grant (ctxt) != NO_ERROR)
    {
      err_count++;
    }

  if (extract_vclass (ctxt) != NO_ERROR)
    {
      err_count++;
    }

  if (extract_class (ctxt) != NO_ERROR)
    {
      err_count++;
    }

  if (extract_pk (ctxt) != NO_ERROR)
    {
      err_count++;
    }

  create_schema_info (ctxt);

  return err_count;
}

static int
extract_all_schema_file (extract_context & ctxt, const char *output_filename)
{
  FILE *output_file;
  int err_count = 0;

  output_file = fopen_ex (output_filename, "w");
  if (output_file == NULL)
    {
      (void) fprintf (stderr, "%s: %s.\n\n", ctxt.exec_name, strerror (errno));
      return 1;
    }

  file_print_output output_ctx (output_file);
  err_count = extract_schema (ctxt, output_ctx);

  if (err_count == 0)
    {
      output_ctx ("\n");
      output_ctx ("COMMIT WORK;\n");
    }

  fclose (output_file);

  return err_count;
}

static int
get_classes (extract_context & ctxt, print_output & output_ctx)
{
  int err = NO_ERROR;
  /*
   * convert the class table into an ordered class list, would be better
   * if we just built the initial list rather than using the table.
   */
  ctxt.classes = get_ordered_classes (output_ctx, NULL);
  if (ctxt.classes == NULL)
    {
      if (db_error_code () != NO_ERROR)
	{
	  err = ER_FAILED;
	}
      else
	{
	  fprintf (stderr, "%s: Unknown database error occurs " "but may not be database error.\n\n", ctxt.exec_name);
	  err = ER_FAILED;
	}
      return err;
    }

  if (ctxt.is_dba_user == false && ctxt.is_dba_group_member == false)
    {
      filter_user_classes (&ctxt.classes, ctxt.login_user);
    }

  er_clear ();
  return err;
}

static void
filter_user_classes (DB_OBJLIST ** class_list, const char *user)
{
  DB_OBJLIST *cl, *prev, *next;
  const char *name = NULL;
  char owner_name[DB_MAX_IDENTIFIER_LENGTH] = { '\0' };

  for (cl = *class_list, prev = NULL, next = NULL; cl != NULL; cl = next)
    {
      next = cl->next;
      name = db_get_class_name (cl->op);
      sm_qualifier_name (name, owner_name, DB_MAX_IDENTIFIER_LENGTH);

      if (owner_name != NULL && strcmp (owner_name, user) == 0)
	{
	  prev = cl;
	}
      else
	{
	  if (prev == NULL)
	    {
	      *class_list = next;
	    }
	  else
	    {
	      prev->next = next;
	    }
	  /*
	   * class_list links were allocated via ml_ext_alloc_link, so we must
	   * free them via ml_ext_free_link.  Otherwise, we can crash.
	   */
	  ml_ext_free_link (cl);
	}
    }
}

static int
create_schema_info (extract_context & ctxt)
{
  size_t total_len = 0;
  FILE *output_file = NULL;
  int err = NO_ERROR;
  char output_filename[PATH_MAX * 2] = { '\0' };
  char order_str[PATH_MAX * 2] = { '\0' };
  const char *loading_order[] =
    { "_schema_user", "_schema_class", "_schema_vclass", "_schema_serial", "_schema_procedure", "_schema_server",
    "_schema_pk", "_schema_fk", "_schema_grant", "_schema_synonym"
  };

  const size_t len = sizeof (loading_order) / sizeof (loading_order[0]);

  if (create_filename_schema_info (ctxt.output_dirname, ctxt.output_prefix, output_filename, sizeof (output_filename))
      != 0)
    {
      util_log_write_errid (MSGCAT_UTIL_GENERIC_INVALID_ARGUMENT);
      return ER_FAILED;
    }

  output_file = fopen_ex (output_filename, "w");
  if (output_file == NULL)
    {
      (void) fprintf (stderr, "%s: %s.\n\n", output_filename, strerror (errno));
      return ER_FAILED;
    }

  file_print_output output_ctx (output_file);

  for (size_t i = 0; i < len; i++)
    {
      total_len = strlen (ctxt.output_prefix) + strlen (loading_order[i]) + 1;
      if (total_len > sizeof (order_str))
	{
	  err = ER_FAILED;
	  break;
	}

      order_str[0] = '\0';
      strcat (order_str, ctxt.output_prefix);
      strcat (order_str, loading_order[i]);
      order_str[strlen (ctxt.output_prefix) + strlen (loading_order[i]) + 1] = '\0';

      for (std::size_t j = 0; j < ctxt.schema_file_list.size (); j++)
	{
	  if (strcmp (order_str, ctxt.schema_file_list[j].c_str ()) == 0)
	    {
	      output_ctx ("%s\n", ctxt.schema_file_list[j].c_str ());
	      break;
	    }
	}
    }

  if (output_file != NULL)
    {
      fclose (output_file);
      output_file = NULL;
    }

  ctxt.schema_file_list.clear ();

  return err;
}
