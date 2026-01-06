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
 * db_temp.c - API functions for schema definition templates.
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <ctype.h>
#include <assert.h>

#include "authenticate.h"
#include "system_parameter.h"
#include "storage_common.h"
#include "db.h"
#include "class_object.h"
#include "object_print.h"
#include "server_interface.h"
#include "boot_cl.h"
#include "locator_cl.h"
#include "schema_manager.h"
#include "schema_template.h"
#include "object_accessor.h"
#include "set_object.h"
#include "virtual_object.h"
#include "parser.h"
#include "execute_statement.h"
#include "execute_schema.h"
#include "network_interface_cl.h"

#define ERROR_SET(error, code) \
  do {                     \
    error = code;          \
    er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, code, 0); \
  } while (0)

#define ATTR_RENAME_SAVEPOINT          "aTTrNAMeSAVE"

static DB_CTMPL *dbt_reserve_name (DB_CTMPL * def, const char *name);

/*
 * SCHEMA TEMPLATES
 */

/*
 * dbt_create_class() - This function creates a class template for a new class.
 *    A class with the given name cannot already exist.
 * return : class template
 * name(in): new class name
 *
 * note : When the template is no longer needed, it should be applied using
 *    dbt_finish_class() or destroyed using dbt_abort_class().
 */
DB_CTMPL *
dbt_create_class (const char *name)
{
  DB_CTMPL *def = NULL;

  CHECK_CONNECT_NULL ();
  CHECK_1ARG_NULL (name);
  CHECK_MODIFICATION_NULL ();

  def = smt_def_class (name);

  if (def != NULL)
    {
      def = dbt_reserve_name (def, name);
    }

  return (def);
}

/*
 * dbt_create_vclass() - This function creates a class template for a new
 *    virtual class. A class with the specified name cannot already exist.
 * return : schema template
 * name(in): the name of a virtual class
 *
 * note : The template can be applied using dbt_finish_class() or destroyed
 *   using dbt_abort_class().
 */
DB_CTMPL *
dbt_create_vclass (const char *name)
{
  DB_CTMPL *def = NULL;

  CHECK_CONNECT_NULL ();
  CHECK_1ARG_NULL (name);
  CHECK_MODIFICATION_NULL ();

  def = smt_def_typed_class (name, SM_VCLASS_CT);

  if (def != NULL)
    {
      def = dbt_reserve_name (def, name);
    }

  return (def);
}

/*
 * dbt_edit_class() - This function creates a class template for an existing
 *    class. The template is initialized with the current definition of the
 *    class, and it is edited with the other class template functions.
 * return : class template
 * classobj(in): class object pointer
 *
 * note : When finished, the class template can be applied with
 *    dbt_finish_class() or destroyed with dbt_abort_class().
 */
DB_CTMPL *
dbt_edit_class (MOP classobj)
{
  DB_CTMPL *def = NULL;

  CHECK_CONNECT_NULL ();
  CHECK_1ARG_NULL (classobj);
  CHECK_MODIFICATION_NULL ();

  def = smt_edit_class_mop (classobj, AU_ALTER);

  return (def);
}

/*
 * dbt_copy_class() - This function creates a class template based on an
 *                    existing class.
 * return : class template
 * new_name(in): name of the class to be created
 * existing_name(in): name of the class to be duplicated
 * class_(out): the current definition of the duplicated class is returned
 *              in order to be used for subsequent operations (such as
 *              duplicating indexes).
 *
 * Note : When finished, the class template can be applied with
 *        dbt_finish_class() or destroyed with dbt_abort_class().
 */
DB_CTMPL *
dbt_copy_class (const char *new_name, const char *existing_name, SM_CLASS ** class_)
{
  DB_CTMPL *def = NULL;

  CHECK_CONNECT_NULL ();
  CHECK_2ARGS_NULL (new_name, existing_name);
  CHECK_MODIFICATION_NULL ();

  def = smt_copy_class (new_name, existing_name, class_);

  if (def != NULL)
    {
      def = dbt_reserve_name (def, new_name);
    }

  return (def);
}

/*
 * dbt_reserve_name () - Reserve new class or view name.
 *
 * return    : Class template.
 * def (in)  : Class template.
 * name (in) : Class name.
 */
static DB_CTMPL *
dbt_reserve_name (DB_CTMPL * def, const char *name)
{
  LC_FIND_CLASSNAME reserved;
  OID class_oid = OID_INITIALIZER;

  assert (def != NULL);
  assert (name != NULL);

  reserved = locator_reserve_class_name (def->name, &class_oid);
  if (reserved != LC_CLASSNAME_RESERVED)
    {
      if (reserved == LC_CLASSNAME_EXIST)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LC_CLASSNAME_EXIST, 1, name);
	}
      else
	{
	  ASSERT_ERROR ();
	}
      smt_quit (def);
      return NULL;
    }
  return def;
}

/*
 * dbt_finish_class() - This function applies a class template. If the template
 *    is applied without error, a pointer to the class object is returned.
 *    If the template is for a new class, a new object pointer is returned.
 *    If the template is for an existing class, the same pointer that was
 *    passed to dbt_edit_class() is returned.
 * return : class pointer
 * def: class template
 *
 * note : If there are no errors, the template is freed and cannot be reused.
 *    If an error is detected, NULL is returned, the global error code is set,
 *    and the template is not freed. The template can either be corrected and
 *    reapplied, or destroyed.
 */
DB_OBJECT *
dbt_finish_class (DB_CTMPL * def)
{
  MOP classmop = NULL;

  CHECK_CONNECT_NULL ();
  CHECK_1ARG_NULL (def);
  CHECK_MODIFICATION_NULL ();

  if (sm_finish_class (def, &classmop) != NO_ERROR)
    {
      classmop = NULL;		/* probably not necessary but be safe */
    }

  return (classmop);
}

/*
 * dbt_abort_class() - This function destroys a class template and frees all
 *    memory allocated for the template.
 * return : none
 * def(in): class template
 */
void
dbt_abort_class (DB_CTMPL * def)
{
  if (def != NULL)
    {
      smt_quit (def);
    }
}

/*
 * SCHEMA TEMPLATE OPERATIONS
 * The descriptions of these functions is the same as that for the
 * non-template versions.
 */

/*
 * dbt_add_attribute() -
 * return:
 * def(in) :
 * name(in) :
 * domain(in) :
 * default_value(in) :
 */
int
dbt_add_attribute (DB_CTMPL * def, const char *name, const char *domain, DB_VALUE * default_value)
{
  int error = NO_ERROR;

  CHECK_CONNECT_ERROR ();
  CHECK_3ARGS_ERROR (def, name, domain);
  CHECK_MODIFICATION_ERROR ();

  error = smt_add_attribute_w_dflt (def, name, domain, (DB_DOMAIN *) 0, default_value, ID_ATTRIBUTE, NULL, NULL, NULL);

  return (error);
}

/*
 * dbt_add_shared_attribute() -
 * return:
 * def(in) :
 * name(in) :
 * domain(in) :
 * default_value(in) :
 */
int
dbt_add_shared_attribute (DB_CTMPL * def, const char *name, const char *domain, DB_VALUE * default_value)
{
  int error = NO_ERROR;

  CHECK_CONNECT_ERROR ();
  CHECK_3ARGS_ERROR (def, name, domain);
  CHECK_MODIFICATION_ERROR ();

  error =
    smt_add_attribute_w_dflt (def, name, domain, (DB_DOMAIN *) 0, default_value, ID_SHARED_ATTRIBUTE, NULL, NULL, NULL);

  return (error);
}

/*
 * dbt_add_class_attribute() -
 * return:
 * def(in) :
 * name(in) :
 * domain(in) :
 * default_value(in) :
 */
int
dbt_add_class_attribute (DB_CTMPL * def, const char *name, const char *domain, DB_VALUE * default_value)
{
  int error = NO_ERROR;

  CHECK_CONNECT_ERROR ();
  CHECK_3ARGS_ERROR (def, name, domain);
  CHECK_MODIFICATION_ERROR ();

  error =
    smt_add_attribute_w_dflt (def, name, domain, (DB_DOMAIN *) 0, default_value, ID_CLASS_ATTRIBUTE, NULL, NULL, NULL);

  return (error);
}


/*
 * dbt_constrain_non_null() -
 * return:
 * def(in) :
 * name(in) :
 * class_attribute(in) :
 * on_or_off(in) :
 *
 * note : Please consider using the newer functions dbt_add_constraint()
 *    and dbt_drop_constraint().
 */
int
dbt_constrain_non_null (DB_CTMPL * def, const char *name, int class_attribute, int on_or_off)
{
  const char *names[2];
  int error = NO_ERROR;

  CHECK_CONNECT_ERROR ();
  CHECK_2ARGS_ERROR (def, name);
  CHECK_MODIFICATION_ERROR ();

  names[0] = name;
  names[1] = NULL;
  if (on_or_off)
    {
      error = dbt_add_constraint (def, DB_CONSTRAINT_NOT_NULL, NULL, names, class_attribute, NULL);
      if (error == NO_ERROR)
	{
	  error = do_check_fk_constraints (def, NULL);
	}
    }
  else
    {
      error = dbt_drop_constraint (def, DB_CONSTRAINT_NOT_NULL, NULL, names, class_attribute);
    }

  return (error);
}

/*
 * dbt_constrain_unique() -
 * return:
 * def(in) :
 * attname(in) :
 * on_or_off(in) :
 *
 * note : Please consider using the newer functions dbt_add_constraint()
 *    and dbt_drop_constraint().
 */
int
dbt_constrain_unique (DB_CTMPL * def, const char *attname, int on_or_off)
{
  int error = NO_ERROR;
  const char *attnames[2];

  CHECK_CONNECT_ERROR ();
  CHECK_2ARGS_ERROR (def, attnames);
  CHECK_MODIFICATION_ERROR ();

  attnames[0] = attname;
  attnames[1] = NULL;
  if (on_or_off)
    {
      error = dbt_add_constraint (def, DB_CONSTRAINT_UNIQUE, NULL, attnames, 0, NULL);
    }
  else
    {
      error = dbt_drop_constraint (def, DB_CONSTRAINT_UNIQUE, NULL, attnames, 0);
    }

  return (error);
}

/*
 * dbt_add_constraint() - This function adds a constraint to one or more
 *    attributes if one does not already exist. This function is similar
 *    to the db_add_constraint() function, except that it operates on a
 *    schema template. Since INDEX'es are not manipulated via templates,
 *    this function should only be called for DB_CONSTRAINT_UNIQUE,
 *    DB_CONSTRAINT_NOT_NULL, and DB_CONSTRAINT_PRIMARY_KEY constraint types.
 * return : error code
 * def(in): class template
 * constraint_type(in): constraint type.
 * constraint_name(in): optional name for constraint.
 * attnames(in): NULL terminated array of attribute names.
 * class_attributes(in): non-zero if the attributes are class attributes and
 *                   zero otherwise.  Unique constraints cannot be applied to
 *                   class attributes.
 * comment(in): constraint comment
 */
int
dbt_add_constraint (DB_CTMPL * def, DB_CONSTRAINT_TYPE constraint_type, const char *constraint_name,
		    const char **attnames, int class_attributes, const char *comment)
{
  int error = NO_ERROR;
  char *name = NULL;

  CHECK_CONNECT_ERROR ();
  CHECK_2ARGS_ERROR (def, attnames);
  CHECK_MODIFICATION_ERROR ();

  if (!DB_IS_CONSTRAINT_UNIQUE_FAMILY (constraint_type) && constraint_type != DB_CONSTRAINT_NOT_NULL)
    {
      ERROR_SET (error, ER_SM_INVALID_CONSTRAINT);
    }

  if (error == NO_ERROR)
    {
      name = sm_produce_constraint_name_tmpl (def, constraint_type, attnames, NULL, constraint_name);
      if (name == NULL)
	{
	  assert (er_errid () != NO_ERROR);
	  error = er_errid ();
	}
      else
	{
	  error = smt_add_constraint (def, constraint_type, name, attnames, NULL, NULL, class_attributes, NULL, NULL,
				      NULL, comment, SM_NORMAL_INDEX);
	  free_and_init (name);
	}
    }

  return (error);
}

/*
 * dbt_drop_constraint() - This is a version of db_drop_constraint() which is
 *    designed to operate on templates.  Since INDEX'es are not manipulated via
 *    templates, this function should only be called for DB_CONSTRAINT_UNIQUE,
 *    DB_CONSTRAINT_NOT_NULL, and DB_CONSTRAINT_PRIMARY_KEY constraint types.
 * return : error code
 * def(in): Class template
 * constraint_type(in): Constraint type.
 * constraint_name(in): Constraint name. NOT NULL constraints are not named
 *                   so this parameter should be NULL in that case.
 * attnames(in): NULL terminated array of attribute names.
 * class_attributes(in): non-zero if the attributes are class attributes and
 *                       zero otherwise.  Unique constraints cannot be applied
 *                       to class attributes.
 */
int
dbt_drop_constraint (DB_CTMPL * def, DB_CONSTRAINT_TYPE constraint_type, const char *constraint_name,
		     const char **attnames, int class_attributes)
{
  int error = NO_ERROR;
  char *name = NULL;
  SM_ATTRIBUTE_FLAG attflag = SM_ATTFLAG_UNIQUE;

  CHECK_CONNECT_ERROR ();
  CHECK_1ARG_ERROR (def);
  CHECK_MODIFICATION_ERROR ();

  if (!DB_IS_CONSTRAINT_FAMILY (constraint_type))
    {
      ERROR_SET (error, ER_SM_INVALID_CONSTRAINT);
    }

  attflag = SM_MAP_CONSTRAINT_TO_ATTFLAG (constraint_type);

  if (error == NO_ERROR)
    {
      name = sm_produce_constraint_name_tmpl (def, constraint_type, attnames, NULL, constraint_name);

      if (name == NULL)
	{
	  assert (er_errid () != NO_ERROR);
	  error = er_errid ();
	}
      else
	{
	  /* TODO We might want to check that the dropped constraint really had the type indicated by the
	   * constraint_type parameter. */
	  error = smt_drop_constraint (def, attnames, name, class_attributes, attflag);
	  free_and_init (name);
	}
    }

  return (error);
}

/*
 * dbt_add_foreign_key() -
 * return:
 * def(in) :
 * constraint_name(in) :
 * attnames(in) :
 * ref_class(in) :
 * ref_attrs(in) :
 * del_action(in) :
 * upd_action(in) :
 */
int
dbt_add_foreign_key (DB_CTMPL * def, const char *constraint_name, const char **attnames, const char *ref_class,
		     const char **ref_attrs, int del_action, int upd_action, const char *comment)
{
  int error = NO_ERROR;
  char *name;
  SM_FOREIGN_KEY_INFO fk_info;

  name = sm_produce_constraint_name_tmpl (def, DB_CONSTRAINT_FOREIGN_KEY, attnames, NULL, constraint_name);

  fk_info.ref_class = ref_class;
  fk_info.ref_attrs = ref_attrs;
  fk_info.delete_action = (SM_FOREIGN_KEY_ACTION) del_action;
  fk_info.update_action = (SM_FOREIGN_KEY_ACTION) upd_action;
  fk_info.is_dropped = false;

  if (name == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
    }
  else
    {
      error = smt_add_constraint (def, DB_CONSTRAINT_FOREIGN_KEY, name, attnames, NULL, NULL, 0, &fk_info, NULL, NULL,
				  comment, SM_NORMAL_INDEX);
      free_and_init (name);
    }

  return error;
}

/*
 * dbt_add_set_attribute_domain() -
 * return:
 * def(in) :
 * name(in) :
 * class_attribute(in) :
 * domain(in) :
 */
int
dbt_add_set_attribute_domain (DB_CTMPL * def, const char *name, int class_attribute, const char *domain)
{
  int error = NO_ERROR;

  CHECK_CONNECT_ERROR ();
  CHECK_2ARGS_ERROR (def, name);
  CHECK_MODIFICATION_ERROR ();

  error = smt_add_set_attribute_domain (def, name, class_attribute, domain, (DB_DOMAIN *) 0);

  return (error);
}

/*
 * dbt_change_domain() -
 * return:
 * def(in) :
 * name(in) :
 * class_attribute(in) :
 * domain(in) :
 */
int
dbt_change_domain (DB_CTMPL * def, const char *name, int class_attribute, const char *domain)
{
  CHECK_CONNECT_ERROR ();
  CHECK_2ARGS_ERROR (def, name);
  CHECK_MODIFICATION_ERROR ();

  /* need a function for this ! */
  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_DB_NO_DOMAIN_CHANGE, 0);
  return ER_DB_NO_DOMAIN_CHANGE;
}

/*
 * dbt_change_default() -
 * return:
 * def(in) :
 * name(in) :
 * class_attribute(in) :
 * value(in) :
 */
int
dbt_change_default (DB_CTMPL * def, const char *name, int class_attribute, DB_VALUE * value)
{
  int error = NO_ERROR;

  CHECK_CONNECT_ERROR ();
  CHECK_2ARGS_ERROR (def, name);
  CHECK_MODIFICATION_ERROR ();

  error = smt_set_attribute_default (def, name, class_attribute, value, NULL);

  return (error);
}

/*
 * dbt_drop_set_attribute_domain() -
 * return:
 * def(in) :
 * name(in) :
 * class_attribute(in) :
 * domain(in) :
 */
int
dbt_drop_set_attribute_domain (DB_CTMPL * def, const char *name, int class_attribute, const char *domain)
{
  int error = NO_ERROR;

  CHECK_CONNECT_ERROR ();
  CHECK_2ARGS_ERROR (def, name);
  CHECK_MODIFICATION_ERROR ();

  error = smt_delete_set_attribute_domain (def, name, class_attribute, domain, (DB_DOMAIN *) 0);

  return (error);
}

/*
 * dbt_drop_attribute() -
 * return:
 * def(in) :
 * name(in) :
 */
int
dbt_drop_attribute (DB_CTMPL * def, const char *name)
{
  int error = NO_ERROR;
  int sr_error = NO_ERROR;
  int au_save;
  SM_ATTRIBUTE *att;
  MOP auto_increment_obj = NULL;

  CHECK_CONNECT_ERROR ();
  CHECK_2ARGS_ERROR (def, name);
  CHECK_MODIFICATION_ERROR ();

  sr_error = smt_find_attribute (def, name, false, &att);
  if ((sr_error == NO_ERROR) && (att != NULL))
    {
      auto_increment_obj = att->auto_increment;
    }

  error = smt_delete_any (def, name, ID_ATTRIBUTE);
  if (error == ER_SM_ATTRIBUTE_NOT_FOUND)
    {
      error = smt_delete_any (def, name, ID_SHARED_ATTRIBUTE);
    }

  /* remove related auto_increment serial obj if exist */
  if ((error == NO_ERROR) && (auto_increment_obj != NULL))
    {
      OID *oidp, serial_obj_id;
      /*
       * check if user is creator or DBA
       */
      error = au_check_serial_authorization (auto_increment_obj);
      if (error != NO_ERROR)
	{
	  goto exit_on_error;
	}

      oidp = ws_identifier (auto_increment_obj);
      COPY_OID (&serial_obj_id, oidp);

      AU_DISABLE (au_save);

      error = obj_delete (auto_increment_obj);

      AU_ENABLE (au_save);
      if (error == NO_ERROR)
	{
	  (void) serial_decache (&serial_obj_id);
	}
    }
exit_on_error:
  return (error);
}

/*
 * dbt_drop_shared_attribute() -
 * return:
 * def(in) :
 * name(in) :
 */
int
dbt_drop_shared_attribute (DB_CTMPL * def, const char *name)
{
  int error = NO_ERROR;

  CHECK_CONNECT_ERROR ();
  CHECK_2ARGS_ERROR (def, name);
  CHECK_MODIFICATION_ERROR ();

  error = smt_delete_any (def, name, ID_SHARED_ATTRIBUTE);

  return (error);
}

/*
 * dbt_drop_class_attribute() -
 * return:
 * def(in) :
 * name(in) :
 */
int
dbt_drop_class_attribute (DB_CTMPL * def, const char *name)
{
  int error = NO_ERROR;

  CHECK_CONNECT_ERROR ();
  CHECK_2ARGS_ERROR (def, name);
  CHECK_MODIFICATION_ERROR ();

  error = smt_delete_any (def, name, ID_CLASS_ATTRIBUTE);

  return (error);
}


/*
 * dbt_add_method() -
 * return:
 * def(in) :
 * name(in) :
 * implementation(in) :
 */
int
dbt_add_method (DB_CTMPL * def, const char *name, const char *implementation)
{
  int error = NO_ERROR;

  CHECK_CONNECT_ERROR ();
  CHECK_2ARGS_ERROR (def, name);
  CHECK_MODIFICATION_ERROR ();

  error = smt_add_method_any (def, name, implementation, ID_METHOD);

  return (error);
}

/*
 * dbt_add_class_method() -
 * return:
 * def(in) :
 * name(in) :
 * implementation(in) :
 */
int
dbt_add_class_method (DB_CTMPL * def, const char *name, const char *implementation)
{
  int error = NO_ERROR;

  CHECK_CONNECT_ERROR ();
  CHECK_2ARGS_ERROR (def, name);
  CHECK_MODIFICATION_ERROR ();

  error = smt_add_method_any (def, name, implementation, ID_CLASS_METHOD);

  return (error);
}

/*
 * dbt_add_argument() -
 * return:
 * def(in) :
 * name(in) :
 * class_method(in) :
 * index(in) :
 * domain(in) :
 */
int
dbt_add_argument (DB_CTMPL * def, const char *name, int class_method, int index, const char *domain)
{
  int error = NO_ERROR;

  CHECK_CONNECT_ERROR ();
  CHECK_2ARGS_ERROR (def, name);
  CHECK_MODIFICATION_ERROR ();

  error = smt_assign_argument_domain (def, name, class_method, NULL, index, domain, (DB_DOMAIN *) 0);

  return (error);
}

/*
 * dbt_add_set_argument_domain() -
 * return:
 * def(in) :
 * name(in) :
 * class_method(in) :
 * index(in) :
 * domain(in) :
 */
int
dbt_add_set_argument_domain (DB_CTMPL * def, const char *name, int class_method, int index, const char *domain)
{
  int error = NO_ERROR;

  CHECK_CONNECT_ERROR ();
  CHECK_2ARGS_ERROR (def, name);
  CHECK_MODIFICATION_ERROR ();

  error = smt_add_set_argument_domain (def, name, class_method, NULL, index, domain, (DB_DOMAIN *) 0);

  return (error);
}

/*
 * dbt_change_method_implementation() -
 * return:
 * def(in) :
 * name(in) :
 * class_method(in) :
 * newname(in) :
 */
int
dbt_change_method_implementation (DB_CTMPL * def, const char *name, int class_method, const char *newname)
{
  int error = NO_ERROR;

  CHECK_CONNECT_ERROR ();
  CHECK_3ARGS_ERROR (def, name, newname);
  CHECK_MODIFICATION_ERROR ();

  error = smt_change_method_implementation (def, name, class_method, newname);

  return (error);
}

/*
 * dbt_drop_method() -
 * return:
 * def(in) :
 * name(in) :
 */
int
dbt_drop_method (DB_CTMPL * def, const char *name)
{
  int error = NO_ERROR;

  CHECK_CONNECT_ERROR ();
  CHECK_2ARGS_ERROR (def, name);
  CHECK_MODIFICATION_ERROR ();

  error = smt_delete_any (def, name, ID_METHOD);

  return (error);
}

/*
 * dbt_drop_class_method() -
 * return:
 * def(in) :
 * name(in) :
 */
int
dbt_drop_class_method (DB_CTMPL * def, const char *name)
{
  int error = NO_ERROR;

  CHECK_CONNECT_ERROR ();
  CHECK_2ARGS_ERROR (def, name);
  CHECK_MODIFICATION_ERROR ();

  error = smt_delete_any (def, name, ID_CLASS_METHOD);

  return (error);
}

/*
 * dbt_add_super() -
 * return:
 * def(in) :
 * super(in) :
 */
int
dbt_add_super (DB_CTMPL * def, MOP super)
{
  int error = NO_ERROR;

  CHECK_CONNECT_ERROR ();
  CHECK_2ARGS_ERROR (def, super);
  CHECK_MODIFICATION_ERROR ();

  error = smt_add_super (def, super);

  return (error);
}

/*
 * dbt_drop_super() -
 * return:
 * def(in) :
 * super(in) :
 */
int
dbt_drop_super (DB_CTMPL * def, MOP super)
{
  int error = NO_ERROR;

  CHECK_CONNECT_ERROR ();
  CHECK_2ARGS_ERROR (def, super);
  CHECK_MODIFICATION_ERROR ();

  error = smt_delete_super (def, super);

  return (error);
}

/*
 * dbt_drop_super_connect() -
 * return:
 * def(in) :
 * super(in) :
 */
int
dbt_drop_super_connect (DB_CTMPL * def, MOP super)
{
  int error = NO_ERROR;

  CHECK_CONNECT_ERROR ();
  CHECK_2ARGS_ERROR (def, super);
  CHECK_MODIFICATION_ERROR ();

  error = smt_delete_super_connect (def, super);

  return (error);
}

/*
 * dbt_rename() -
 * return:
 * def(in) :
 * name(in) :
 * class_namespace(in) :
 * newname(in) :
 */
int
dbt_rename (DB_CTMPL * def, const char *name, int class_namespace, const char *newname)
{
  int error = NO_ERROR;
  SM_ATTRIBUTE *att;
  MOP auto_increment_obj = NULL;

  CHECK_CONNECT_ERROR ();
  CHECK_3ARGS_ERROR (def, name, newname);
  CHECK_MODIFICATION_ERROR ();

  if (!class_namespace)
    {
      att = (SM_ATTRIBUTE *) SM_FIND_NAME_IN_COMPONENT_LIST (def->attributes, name);
      if (att != NULL)
	auto_increment_obj = att->auto_increment;
    }

  if (auto_increment_obj != NULL)
    {
      error = tran_system_savepoint (ATTR_RENAME_SAVEPOINT);
    }

  if (error == NO_ERROR)
    {
      error = smt_rename_any (def, name, class_namespace, newname);

      /* rename related auto_increment serial obj if exist */
      if ((error == NO_ERROR) && (auto_increment_obj != NULL))
	{
	  error = do_update_auto_increment_serial_on_rename (att->auto_increment, def->name, newname);

	  if (error != NO_ERROR)
	    {
	      tran_abort_upto_system_savepoint (ATTR_RENAME_SAVEPOINT);
	    }
	}
    }

  return (error);
}

/*
 * dbt_add_method_file() -
 * return:
 * def(in) :
 * name(in) :
 */
int
dbt_add_method_file (DB_CTMPL * def, const char *name)
{
  int error = NO_ERROR;

  CHECK_CONNECT_ERROR ();
  CHECK_2ARGS_ERROR (def, name);
  CHECK_MODIFICATION_ERROR ();

  error = smt_add_method_file (def, name);

  return (error);
}

/*
 * dbt_drop_method_file() -
 * return:
 * def(in) :
 * name(in) :
 */
int
dbt_drop_method_file (DB_CTMPL * def, const char *name)
{
  int error = NO_ERROR;

  CHECK_CONNECT_ERROR ();
  CHECK_2ARGS_ERROR (def, name);
  CHECK_MODIFICATION_ERROR ();

  error = smt_drop_method_file (def, name);

  return (error);
}

/*
 * dbt_drop_method_files() -
 * return:
 * def(in) :
 */
int
dbt_drop_method_files (DB_CTMPL * def)
{
  int error = NO_ERROR;

  CHECK_CONNECT_ERROR ();
  CHECK_1ARG_ERROR (def);
  CHECK_MODIFICATION_ERROR ();

  error = smt_reset_method_files (def);

  return (error);
}

/*
 * dbt_rename_method_file() -
 * return:
 * def(in) :
 * old_name(in) :
 * new_name(in) :
 */
int
dbt_rename_method_file (DB_CTMPL * def, const char *old_name, const char *new_name)
{
  int error = NO_ERROR;

  CHECK_CONNECT_ERROR ();
  CHECK_3ARGS_ERROR (def, old_name, new_name);
  CHECK_MODIFICATION_ERROR ();

  error = smt_rename_method_file (def, old_name, new_name);

  return (error);
}

/*
 * dbt_set_loader_commands() -
 * return:
 * def(in) :
 * commands(in) :
 */
int
dbt_set_loader_commands (DB_CTMPL * def, const char *commands)
{
  int error = NO_ERROR;

  CHECK_CONNECT_ERROR ();
  CHECK_2ARGS_ERROR (def, commands);
  CHECK_MODIFICATION_ERROR ();

  error = smt_set_loader_commands (def, commands);

  return (error);
}

/*
 * dbt_add_resolution() -
 * return:
 * def(in) :
 * super(in) :
 * name(in) :
 * alias(in) :
 */
int
dbt_add_resolution (DB_CTMPL * def, MOP super, const char *name, const char *alias)
{
  int error = NO_ERROR;

  CHECK_CONNECT_ERROR ();
  CHECK_3ARGS_ERROR (def, super, name);
  CHECK_MODIFICATION_ERROR ();

  error = smt_add_resolution (def, super, name, alias);

  return (error);
}

/*
 * dbt_add_class_resolution() -
 * return:
 * def(in) :
 * super(in) :
 * name(in) :
 * alias(in) :
 */
int
dbt_add_class_resolution (DB_CTMPL * def, MOP super, const char *name, const char *alias)
{
  int error = NO_ERROR;

  CHECK_CONNECT_ERROR ();
  CHECK_3ARGS_ERROR (def, super, name);
  CHECK_MODIFICATION_ERROR ();

  error = smt_add_class_resolution (def, super, name, alias);

  return (error);
}

/*
 * dbt_drop_resolution() -
 * return:
 * def(in) :
 * super(in) :
 * name(in) :
 */
int
dbt_drop_resolution (DB_CTMPL * def, MOP super, const char *name)
{
  int error = NO_ERROR;

  CHECK_CONNECT_ERROR ();
  CHECK_3ARGS_ERROR (def, super, name);
  CHECK_MODIFICATION_ERROR ();

  error = smt_delete_resolution (def, super, name);

  return (error);
}

/*
 * dbt_drop_class_resolution() -
 * return:
 * def(in) :
 * super(in) :
 * name(in) :
 */
int
dbt_drop_class_resolution (DB_CTMPL * def, MOP super, const char *name)
{
  int error = NO_ERROR;

  CHECK_CONNECT_ERROR ();
  CHECK_3ARGS_ERROR (def, super, name);
  CHECK_MODIFICATION_ERROR ();

  error = smt_delete_class_resolution (def, super, name);

  return (error);
}

/*
 * dbt_add_query_spec() -
 * return:
 * def(in) :
 * query(in) :
 */
int
dbt_add_query_spec (DB_CTMPL * def, const char *query)
{
  int error = NO_ERROR;

  CHECK_CONNECT_ERROR ();
  CHECK_2ARGS_ERROR (def, query);
  CHECK_MODIFICATION_ERROR ();

  error = smt_add_query_spec (def, query);

  return (error);
}

/*
 * dbt_drop_query_spec() -
 * return:
 * def(in) :
 * query_no(in) :
 */
int
dbt_drop_query_spec (DB_CTMPL * def, const int query_no)
{
  int error = NO_ERROR;

  CHECK_CONNECT_ERROR ();
  CHECK_1ARG_ERROR (def);
  CHECK_MODIFICATION_ERROR ();

  error = smt_drop_query_spec (def, query_no);

  return (error);
}

/*
 * dbt_reset_query_spec() - delete all query specs from template
 * return:
 * def(in) :
 */
int
dbt_reset_query_spec (DB_CTMPL * def)
{
  int error = NO_ERROR;

  CHECK_CONNECT_ERROR ();
  CHECK_1ARG_ERROR (def);
  CHECK_MODIFICATION_ERROR ();

  error = smt_reset_query_spec (def);

  return (error);
}

/*
 * dbt_change_query_spec() -
 * return:
 * def(in) :
 * new_query(in) :
 * query_no(in) :
 */
int
dbt_change_query_spec (DB_CTMPL * def, const char *new_query, const int query_no)
{
  int error = NO_ERROR;

  CHECK_CONNECT_ERROR ();
  CHECK_2ARGS_ERROR (def, new_query);
  CHECK_MODIFICATION_ERROR ();

  error = smt_change_query_spec (def, new_query, query_no);

  return (error);
}

/*
 * dbt_set_object_id() -
 * return:
 * def(in) :
 * id_list(in) :
 */
int
dbt_set_object_id (DB_CTMPL * def, DB_NAMELIST * id_list)
{
  return NO_ERROR;
}
