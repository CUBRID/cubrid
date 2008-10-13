/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 * 
 * dbgadget.c - Gadget Interface
 * 
 * note : The gadget interface provides a shortcut for performing repeated
 *      inserts.  It was designed to improve ESQLX performance, though it
 *      may find other uses.  The interface eliminates the need to compile
 *      a statement and repeatedly coerce input values to the correct data
 *      type. given a class name and a list of attributes, a reusable
 *      "gadget" is created to which values which will remain constant for
 *      all individual inserts may be pre-coerced and bound.  Variable
 *      values may be supplied at execution time via a DB_VALUE array.
 *      For example, if we have this schema:
 * 
 *          create class foo (a numeric, b int, c string);
 * 
 *      and we want to do repeated inserts like this:
 * 
 *          insert into foo(a, ...) values (1.0, ...);
 * 
 *      we would set up something like this:
 * 
 *          DB_GADGET  *gadget  = NULL;
 *          const char *attrs[] = {"a", "b", "c", NULL};
 *          DB_VALUE   dbval, vals[2];
 * 
 *          gadget = db_gadget_create("foo", attrs);
 *          db_make_double(&dbval, 1.0);
 *          db_gadget_bind(gadget, "a", &dbval);
 * 
 *          for (i = 0; i < 20; i++) {
 *              db_make_int(&vals[0], i);
 *              db_make_string(&vals[1], atoi(i));
 *              obj = db_gadget_exec(gadget, 2, vals);
 *          }
 * 
 *          db_gadget_destroy(gadget);
 * 
 *      Execution is performed using attribute descriptors and templates
 *      for maximum performance.
 */
#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include "db.h"
#include "esql_gadget.h"
#include "parser.h"

#define NOT_FOUND -1

static int gadget_attr_index (DB_GADGET * gadget, const char *attr_name);

/*
 * db_gadget_create() - Look up the class and build attribute descriptors for
 *     all of the supplied attributes.  The user is obligated to supply
 *     DB_VALUEs for each of these attributes, either by "binding" them via
 *     db_gadget_bind (so they become, in essence, default values for
 *     db_gadget_exec) or by supplying them explicitly in the call to
 *     db_gadget_exec.
 * 
 *     If no attributes are supplied, assume that the user will be supplying
 *     values for all attributes (i.e. "insert into class values (x, y, z)")
 *     and build descriptors for all attributes of the class.
 * 
 *     Unspecified attributes will receive their appropriate default
 *     values when an instance is created.
 *
 * note : Using gadgets to insert into views is not supported due to
 *     the special casing and extra checks required and our inability to
 *     support check option.
 * return : gadget pointer
 * class_name(in): name of class for which to create gadget
 * attribute_names(in): NULL-terminated array containing names of attributes
 *    the user will supply values for
 */
DB_GADGET *
db_gadget_create (const char *class_name, const char *attribute_names[])
{
  DB_OBJECT *class_;
  DB_GADGET *gadget = NULL;
  const char **att_name = attribute_names;
  int listlen = 0, i;


  if (class_name == NULL || (class_ = db_find_class (class_name)) == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_LC_UNKNOWN_CLASSNAME, 1, class_name);
      goto error;
    }

  if (db_is_vclass (class_))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GADGET_NO_VCLASSES, 0);
      goto error;
    }

  gadget = (DB_GADGET *) malloc (sizeof (DB_GADGET));
  if (gadget == NULL)
    {
      goto error;
    }
  gadget->attrs = NULL;
  gadget->class_ = class_;

  while (att_name[listlen])
    {
      listlen++;
    }
  gadget->num_attrs = listlen;

  /* If no attribute list was supplied, assume all attributes */
  if (listlen == 0)
    {
      DB_ATTRIBUTE *attribute = db_get_attributes (gadget->class_);

      if (attribute == NULL)
	{
	  goto error;
	}
      for (; attribute; attribute = db_attribute_next (attribute))
	{
	  gadget->num_attrs++;
	}
    }

  gadget->attrs = (ATTR_VAL *) malloc ((gadget->num_attrs + 1) *
				       sizeof (ATTR_VAL));
  if (gadget->num_attrs > 0 && gadget->attrs == NULL)
    {
      goto error;
    }

  for (i = 0; i < gadget->num_attrs; i++)
    {
      gadget->attrs[i].attr_desc = NULL;
      gadget->attrs[i].value = NULL;
    }

  if (listlen == 0)
    {
      DB_ATTRIBUTE *attribute = db_get_attributes (gadget->class_);

      if (attribute == NULL)
	{
	  goto error;
	}
      for (i = 0; attribute; i++, attribute = db_attribute_next (attribute))
	{
	  if (db_get_attribute_descriptor
	      (class_, db_attribute_name (attribute), false, true,
	       &gadget->attrs[i].attr_desc))
	    {
	      goto error;
	    }
	}
    }
  else
    {
      for (i = 0, att_name = attribute_names; i < gadget->num_attrs;
	   i++, att_name++)
	{
	  if (gadget_attr_index (gadget, *att_name) != NOT_FOUND)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_OBJ_DUPLICATE_ASSIGNMENT, 1, *att_name);
	      goto error;
	    }
	  if (db_get_attribute_descriptor
	      (class_, *att_name, false, true, &gadget->attrs[i].attr_desc))
	    {
	      goto error;
	    }
	}
    }

  return gadget;

error:
  if (gadget)
    {
      db_gadget_destroy (gadget);
    }
  return NULL;
}

/*
 * db_gadget_destroy() - Tear down the the given gadget.  The gadget owns any
 *    DB_VALUEs that have been supplied via db_gadget_bind, so it should clear
 *    them before returning.
 * return : void
 * gadget(in): gadget pointer to tear down
 */
void
db_gadget_destroy (DB_GADGET * gadget)
{
  int i;

  if (gadget == NULL)
    {
      return;
    }

  for (i = 0; gadget->attrs && (i < gadget->num_attrs); i++)
    {
      if (gadget->attrs[i].attr_desc)
	{
	  db_free_attribute_descriptor (gadget->attrs[i].attr_desc);
	}
      if (gadget->attrs[i].value)
	{
	  (void) db_value_clear (gadget->attrs[i].value);
	  free_and_init (gadget->attrs[i].value);
	}
    }

  if (gadget->attrs)
    {
      free_and_init (gadget->attrs);
    }
  free_and_init (gadget);
  gadget = NULL;

  return;
}

/*
 * db_gadget_bind() - Associate the given value with the given attribute.
 *    A NULL DB_VALUE pointer indicates that any previous binding should be
 *    erased.  The incoming DB_VALUE is coerced to the proper domain before
 *    being stored so as to avoid coercion during repeated inserts.
 * return : error code
 * gadget(in): gadget containing insert data
 * attribute_name(in): number of db_values in dbvals array
 * dbval(in): array containing values for unbound attributes
 */
int
db_gadget_bind (DB_GADGET * gadget,
		const char *attribute_name, DB_VALUE * dbval)
{
  int i = 0;

  if (gadget == NULL || gadget->class_ == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GADGET_INVALID, 0);
      return ER_GADGET_INVALID;
    }

  if (attribute_name == NULL
      || (i = gadget_attr_index (gadget, attribute_name)) == NOT_FOUND)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SM_UNKNOWN_ATTRIBUTE,
	      2, attribute_name, db_get_class_name (gadget->class_));
      return ER_SM_UNKNOWN_ATTRIBUTE;
    }

  /* Free any previous value before rebinding */
  if (gadget->attrs[i].value != NULL)
    {
      db_value_clear (gadget->attrs[i].value);
      free_and_init (gadget->attrs[i].value);
    }

  /* A null dbval indicates previous binding should be erased */
  if (dbval == NULL)
    {
      return NO_ERROR;
    }

  gadget->attrs[i].value = (DB_VALUE *) malloc (sizeof (DB_VALUE));
  if (gadget->attrs[i].value == NULL)
    {
      return er_errid ();
    }

  return tp_value_coerce (dbval, gadget->attrs[i].value,
			  db_attdesc_domain (gadget->attrs[i].attr_desc));
}

/*
 * db_gadget_exec() - Creates a new instance using templates and the
 *     descriptors acquired when the DB_GADGET was initialized.  Any
 *     attributes that haven't had a DB_VALUE bound via db_gadget_bind
 *     *must* have a value supplied via the dbvals array.  The values in
 *     the dbvals array are in the same order as the attributes
 *     originally supplied in the db_gadget_create call.  For example,
 *     if a gadget was used like this:
 *          DB_GADGET  *gadget;
 *          const char *attr_names[] = {"a", "b", "c", "d", NULL};
 *          DB_VALUE   vals[4];
 *          gadget = db_gadget_create("foo", attr_names);
 *          db_gadget_bind(gadget, "a", ...);
 *          db_gadget_bind(gadget, "c", ...);
 *          for (...) {
 *              ...
 *              obj = db_gadget_exec(gadget, 2, vals);
 *          }
 *     then the value for "b" would be suppled in vals[0], and the value
 *     for "d" would be supplied in vals[1].
 * 
 * return : pointer to inserted instance object
 * gadget(in): gadget containing insert data
 * num_dbvals(in): number of db_values in dbvals array
 * dbvals(in): array containing values for unbound attributes
 */
DB_OBJECT *
db_gadget_exec (DB_GADGET * gadget, int num_dbvals, DB_VALUE dbvals[])
{
  int dbvals_index = 0;
  DB_VALUE *value = NULL;
  DB_OTMPL *otemplate = NULL;
  DB_OBJECT *obj = NULL;
  int num_vals = num_dbvals;
  int i;

  if (gadget == NULL || gadget->class_ == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GADGET_INVALID, 0);
      goto error;
    }

  otemplate = dbt_create_object_internal (gadget->class_);
  if (otemplate == NULL)
    {
      goto error;
    }

  for (i = 0; i < gadget->num_attrs; i++)
    {
      value = gadget->attrs[i].value;
      if (value == NULL)
	{
	  if (dbvals_index < num_dbvals)
	    {
	      value = &dbvals[dbvals_index++];
	    }
	}

      if (value == NULL)
	{
	  for (i = 0; i < gadget->num_attrs; i++)
	    {
	      if (gadget->attrs[i].value)
		{
		  num_vals++;
		}
	    }
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GADGET_ATTRS_VALS_NE,
		  2, gadget->num_attrs, num_vals);
	  goto error;
	}

      if (dbt_dput_internal (otemplate, gadget->attrs[i].attr_desc, value) !=
	  NO_ERROR)
	{
	  goto error;
	}
    }

  obj = dbt_finish_object (otemplate);
  if (obj == NULL)
    {
      goto error;
    }

  return obj;

error:
  if (otemplate)
    {
      dbt_abort_object (otemplate);
    }
  return NULL;
}

/*
 * gadget_attr_index() - Perform case-insensitive search for given attribute
 *    and return its position within the gadget if found.
 * return : attribute's index
 * gadget(in): gadget to search
 * attr_names(in): name of attributes to find index of
 */
static int
gadget_attr_index (DB_GADGET * gadget, const char *attr_name)
{
  int i;

  for (i = 0; i < gadget->num_attrs; i++)
    {
      if (gadget->attrs[i].attr_desc &&
	  pt_streq (attr_name, gadget->attrs[i].attr_desc->name) == 0)
	{
	  return i;
	}
    }

  return NOT_FOUND;
}
