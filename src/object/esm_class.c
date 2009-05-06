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
 * esm_class.c - Class and method definition file for the system-defined
 *               classes
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>

#include "dbi.h"
#include "error_manager.h"
#include "elo_holder.h"
#include "glo_class.h"
#include "memory_alloc.h"
#include "db.h"

/* this must be the last header file included!!! */
#include "dbval.h"

/* Macros for defining attriubtes, methods and class methods for mm classes */
#define ADD_ATTRIBUTE(class_obj, att_name, att_type, default_value)          \
  do {                                                                       \
    int x;                                                                   \
    if ((x = db_add_attribute(class_obj, att_name,                           \
                              att_type, default_value)) != 0)                \
      er_set(ER_WARNING_SEVERITY, ARG_FILE_LINE, ERR_MM_ADDING_ATTRIBUTE, 2, \
             x, att_name);                                                   \
  }                                                                          \
  while(0)

#define ADD_METHOD(class_obj, method_name, implementation_name)              \
  do {                                                                       \
    int x;                                                                   \
    if ((x = db_add_method(class_obj,                                        \
                           method_name,                                      \
                           implementation_name)) != 0)                       \
      er_set(ER_WARNING_SEVERITY, ARG_FILE_LINE, ERR_MM_ADDING_METHOD, 3,    \
     	     x, method_name, implementation_name);                           \
  }                                                                          \
  while(0)

#define ADD_CLASS_METHOD(class_obj, method_name, imp_name)                   \
  do {                                                                       \
    int x;                                                                   \
    if ((x = db_add_class_method(class_obj, method_name, imp_name)) != 0)    \
      er_set(ER_WARNING_SEVERITY, ARG_FILE_LINE, ERR_MM_ADDING_METHOD, 3,    \
             x, method_name, imp_name);                                      \
  }                                                                          \
  while(0)

#define ADD_SUPER(class_obj, super_obj)                                      \
  do {                                                                       \
  int x;                                                                     \
    if ((x = db_add_super_internal(class_obj, super_obj)) != 0)                       \
      er_set(ER_WARNING_SEVERITY, ARG_FILE_LINE, ERR_MM_ADDING_SUPER, 1, x); \
  }                                                                          \
  while(0)

typedef void (*DEF_SIGNATURE_FUNC) (DB_OBJECT *, const char *,
				    const char *, const char *,
				    const char *, const char *,
				    const char *, const char *,
				    const char *, const char *, const char *);

DEF_SIGNATURE_FUNC define_instance_signature = def_instance_signature;
DEF_SIGNATURE_FUNC define_class_signature = def_class_signature;

static void grant_authorization (DB_OBJECT * user, DB_OBJECT * class_);


/*
 * def_instance_signature() -
 *      return:
 *  class_obj(in) :
 *  method_name(in) :
 *  return_domain(in) :
 *  arg1(in) :
 *  arg2(in) :
 *  arg3(in) :
 *  arg4(in) :
 *  arg5(in) :
 *  arg6(in) :
 *  arg7(in) :
 *  arg8(in) :
 */
void
def_instance_signature (DB_OBJECT * class_obj, const char *method_name,
			const char *return_domain, const char *arg1,
			const char *arg2, const char *arg3,
			const char *arg4, const char *arg5,
			const char *arg6, const char *arg7, const char *arg8)
{
  if (class_obj == NULL || method_name == NULL || return_domain == NULL)
    {
      return;
    }
  db_set_method_arg_domain (class_obj, method_name, 0, return_domain);
  if (arg1 == NULL)
    {
      return;
    }
  db_set_method_arg_domain (class_obj, method_name, 1, arg1);
  if (arg2 == NULL)
    {
      return;
    }
  db_set_method_arg_domain (class_obj, method_name, 2, arg2);
  if (arg3 == NULL)
    {
      return;
    }
  db_set_method_arg_domain (class_obj, method_name, 3, arg3);
  if (arg4 == NULL)
    {
      return;
    }
  db_set_method_arg_domain (class_obj, method_name, 4, arg4);
  if (arg5 == NULL)
    {
      return;
    }
  db_set_method_arg_domain (class_obj, method_name, 5, arg5);
  if (arg6 == NULL)
    {
      return;
    }
  db_set_method_arg_domain (class_obj, method_name, 6, arg6);
  if (arg7 == NULL)
    {
      return;
    }
  db_set_method_arg_domain (class_obj, method_name, 7, arg7);
  if (arg8 == NULL)
    {
      return;
    }
  db_set_method_arg_domain (class_obj, method_name, 8, arg8);
}

/*
 * def_class_signature() -
 *      return:
 *  class_obj(in) :
 *  method_name(in) :
 *  return_domain(in) :
 *  arg1(in) :
 *  arg2(in) :
 *  arg3(in) :
 *  arg4(in) :
 *  arg5(in) :
 *  arg6(in) :
 *  arg7(in) :
 *  arg8(in) :
 */
void
def_class_signature (DB_OBJECT * class_obj, const char *method_name,
		     const char *return_domain, const char *arg1,
		     const char *arg2, const char *arg3,
		     const char *arg4, const char *arg5,
		     const char *arg6, const char *arg7, const char *arg8)
{
  if (class_obj == NULL || method_name == NULL || return_domain == NULL)
    {
      return;
    }
  db_set_class_method_arg_domain (class_obj, method_name, 0, return_domain);
  if (arg1 == NULL)
    {
      return;
    }
  db_set_class_method_arg_domain (class_obj, method_name, 1, arg1);
  if (arg2 == NULL)
    {
      return;
    }
  db_set_class_method_arg_domain (class_obj, method_name, 2, arg2);
  if (arg3 == NULL)
    {
      return;
    }
  db_set_class_method_arg_domain (class_obj, method_name, 3, arg3);
  if (arg4 == NULL)
    {
      return;
    }
  db_set_class_method_arg_domain (class_obj, method_name, 4, arg4);
  if (arg5 == NULL)
    {
      return;
    }
  db_set_class_method_arg_domain (class_obj, method_name, 5, arg5);
  if (arg6 == NULL)
    {
      return;
    }
  db_set_class_method_arg_domain (class_obj, method_name, 6, arg6);
  if (arg7 == NULL)
    {
      return;
    }
  db_set_class_method_arg_domain (class_obj, method_name, 7, arg7);
  if (arg8 == NULL)
    {
      return;
    }
  db_set_class_method_arg_domain (class_obj, method_name, 8, arg8);
}

/* This kludge is to prevent warnings for wrong number of arguments passed. */

/*
 * grant_authorization() -
 *      return: none
 *  user(in) :
 *  class(in) :
 */
static void
grant_authorization (DB_OBJECT * user, DB_OBJECT * class_)
{
  db_grant (user, class_, (DB_AUTH) (DB_AUTH_SELECT |
				     DB_AUTH_INSERT |
				     DB_AUTH_UPDATE | DB_AUTH_DELETE |
				     DB_AUTH_EXECUTE), false);
}

/*
 * esm_define_esm_classes() -
 *      return: none
 */
void
esm_define_esm_classes (void)
{
  DB_OBJECT *glo_holder_class, *glo_class, *glo_name_class;
#if 0				/* to disable TEXT */
  DB_OBJECT *text_class;
#endif
  DB_VALUE val;
  int err;
  DB_OBJECT *user;
  DB_OBJECT *trigger;
  char tmp[80];

/* Class definition for the GLO_HOLDER class */

  glo_name_class = db_create_class (GLO_NAME_CLASS_NAME);
  if (glo_name_class == NULL)
    {
      glo_name_class = db_find_class (GLO_NAME_CLASS_NAME);
    }

  glo_holder_class = db_create_class (GLO_HOLDER_CLASS_NAME);
  if (glo_holder_class == NULL)
    {
      glo_holder_class = db_find_class (GLO_HOLDER_CLASS_NAME);
    }

  user = db_find_user ("public");
  if (user == NULL)
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ERR_MM_FINDING_PUBLIC, 0);
    }
  err = db_add_attribute_internal (glo_holder_class,
				   GLO_HOLDER_GLO_NAME, "*elo*", NULL,
				   ID_ATTRIBUTE);
  if (err != 0)
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ERR_MM_ADDING_ATTRIBUTE, 2,
	      err, GLO_HOLDER_GLO_NAME);
    }

  ADD_ATTRIBUTE (glo_holder_class, GLO_HOLDER_LOCK_NAME,
		 db_get_type_name (DB_TYPE_INTEGER), NULL);
  ADD_ATTRIBUTE (glo_holder_class, GLO_HOLDER_NAME_PTR,
		 GLO_NAME_CLASS_NAME, NULL);

  ADD_METHOD (glo_holder_class, GLO_HOLDER_LOCK_METHOD, "Glo_lock_holder");
  (*define_instance_signature) (glo_holder_class, GLO_HOLDER_LOCK_METHOD,
				db_get_type_name (DB_TYPE_INTEGER), NULL,
				NULL, NULL, NULL, NULL, NULL, NULL, NULL);

  ADD_CLASS_METHOD (glo_holder_class, GLO_HOLDER_CREATE_METHOD,
		    "Glo_create_holder");
  (*define_class_signature) (glo_holder_class, GLO_HOLDER_CREATE_METHOD,
			     db_get_type_name (DB_TYPE_OBJECT),
			     db_get_type_name (DB_TYPE_STRING), NULL,
			     NULL, NULL, NULL, NULL, NULL, NULL);

/* Class definition for the GLO_NAME class */

  ADD_ATTRIBUTE (glo_name_class, GLO_NAME_PATHNAME,
		 db_get_type_name (DB_TYPE_STRING), NULL);
  ADD_ATTRIBUTE (glo_name_class, GLO_NAME_HOLDER_PTR,
		 GLO_HOLDER_CLASS_NAME, NULL);

/* Class definition for the GLO class */

  glo_class = db_create_class (GLO_CLASS_NAME);
  if (glo_class == NULL)
    {
      glo_class = db_find_class (GLO_CLASS_NAME);
    }

  ADD_ATTRIBUTE (glo_class, GLO_CLASS_HOLDER_NAME,
		 GLO_HOLDER_CLASS_NAME, NULL);
  db_make_int (&val, 8);
  ADD_ATTRIBUTE (glo_class, GLO_CLASS_UNIT_SIZE_NAME,
		 db_get_type_name (DB_TYPE_INTEGER), &val);
  ADD_ATTRIBUTE (glo_class, GLO_CLASS_HEADER_SIZE_NAME,
		 db_get_type_name (DB_TYPE_INTEGER), NULL);

  ADD_METHOD (glo_class, GLO_METHOD_READ, "esm_Glo_read");
  (*define_instance_signature) (glo_class, GLO_METHOD_READ, db_get_type_name (DB_TYPE_INTEGER),	/*return arg */
				db_get_type_name (DB_TYPE_INTEGER),	/*length */
				db_get_type_name (DB_TYPE_STRING),	/*buffer */
				NULL, NULL, NULL, NULL, NULL, NULL);
  ADD_METHOD (glo_class, GLO_METHOD_PRINT_READ, "esm_Glo_print_read");
  (*define_instance_signature) (glo_class, GLO_METHOD_PRINT_READ, db_get_type_name (DB_TYPE_INTEGER),	/*return arg */
				db_get_type_name (DB_TYPE_INTEGER),	/*length */
				NULL, NULL, NULL, NULL, NULL, NULL, NULL);
  ADD_METHOD (glo_class, GLO_METHOD_WRITE, "esm_Glo_write");
  (*define_instance_signature) (glo_class, GLO_METHOD_WRITE, db_get_type_name (DB_TYPE_INTEGER),	/*return arg */
				db_get_type_name (DB_TYPE_INTEGER),	/*length */
				db_get_type_name (DB_TYPE_STRING),	/*buffer */
				NULL, NULL, NULL, NULL, NULL, NULL);
  ADD_METHOD (glo_class, GLO_METHOD_SEEK, "esm_Glo_seek");
  (*define_instance_signature) (glo_class, GLO_METHOD_SEEK, db_get_type_name (DB_TYPE_BIGINT),	/*return arg */
				db_get_type_name (DB_TYPE_BIGINT),	/*position */
				NULL, NULL, NULL, NULL, NULL, NULL, NULL);
  ADD_METHOD (glo_class, GLO_METHOD_INSERT, "esm_Glo_insert");
  (*define_instance_signature) (glo_class, GLO_METHOD_INSERT, db_get_type_name (DB_TYPE_INTEGER),	/*return arg */
				db_get_type_name (DB_TYPE_INTEGER),	/*length */
				db_get_type_name (DB_TYPE_STRING),	/*buffer */
				NULL, NULL, NULL, NULL, NULL, NULL);
  ADD_METHOD (glo_class, GLO_METHOD_DELETE, "esm_Glo_delete");
  (*define_instance_signature) (glo_class, GLO_METHOD_DELETE, db_get_type_name (DB_TYPE_INTEGER),	/*return arg */
				db_get_type_name (DB_TYPE_INTEGER),	/*length */
				NULL, NULL, NULL, NULL, NULL, NULL, NULL);
  ADD_METHOD (glo_class, GLO_METHOD_PATHNAME, "esm_Glo_pathname");
  (*define_instance_signature) (glo_class, GLO_METHOD_PATHNAME, db_get_type_name (DB_TYPE_STRING),	/*return arg */
				NULL, NULL, NULL, NULL, NULL, NULL, NULL,
				NULL);
  ADD_METHOD (glo_class, GLO_METHOD_FULL_PATHNAME, "esm_Glo_full_pathname");
  (*define_instance_signature) (glo_class, GLO_METHOD_FULL_PATHNAME, db_get_type_name (DB_TYPE_STRING),	/*return arg */
				NULL, NULL, NULL, NULL, NULL, NULL, NULL,
				NULL);
  ADD_METHOD (glo_class, GLO_METHOD_TRUNCATE, "esm_Glo_truncate");
  (*define_instance_signature) (glo_class, GLO_METHOD_TRUNCATE, db_get_type_name (DB_TYPE_BIGINT),	/*return arg */
				NULL, NULL, NULL, NULL, NULL, NULL, NULL,
				NULL);
  ADD_METHOD (glo_class, GLO_METHOD_APPEND, "esm_Glo_append");
  (*define_instance_signature) (glo_class, GLO_METHOD_APPEND, db_get_type_name (DB_TYPE_INTEGER),	/*return arg */
				db_get_type_name (DB_TYPE_INTEGER),	/*length */
				db_get_type_name (DB_TYPE_STRING),	/*buffer */
				NULL, NULL, NULL, NULL, NULL, NULL);
  ADD_METHOD (glo_class, GLO_METHOD_SIZE, "esm_Glo_size");
  (*define_instance_signature) (glo_class, GLO_METHOD_SIZE, db_get_type_name (DB_TYPE_BIGINT),	/*return arg */
				NULL, NULL, NULL, NULL, NULL, NULL, NULL,
				NULL);
  ADD_METHOD (glo_class, GLO_METHOD_COMPRESS, "esm_Glo_compress");
  (*define_instance_signature) (glo_class, GLO_METHOD_COMPRESS, db_get_type_name (DB_TYPE_INTEGER),	/*return arg */
				NULL, NULL, NULL, NULL, NULL, NULL, NULL,
				NULL);
  ADD_METHOD (glo_class, GLO_METHOD_DESTROY, "esm_Glo_destroy");
  (*define_instance_signature) (glo_class, GLO_METHOD_DESTROY, db_get_type_name (DB_TYPE_INTEGER),	/*return arg */
				NULL, NULL, NULL, NULL, NULL, NULL, NULL,
				NULL);

  ADD_METHOD (glo_class, GLO_METHOD_INITIALIZE, "esm_Glo_init");
  ADD_METHOD (glo_class, GLO_METHOD_COPY_TO, "esm_Glo_copy_to");
  (*define_instance_signature) (glo_class, GLO_METHOD_COPY_TO, db_get_type_name (DB_TYPE_INTEGER),	/*return arg */
				NULL, NULL, NULL, NULL, NULL, NULL, NULL,
				NULL);
  ADD_METHOD (glo_class, GLO_METHOD_COPY_FROM, "esm_Glo_copy_from");
  (*define_instance_signature) (glo_class, GLO_METHOD_COPY_FROM, db_get_type_name (DB_TYPE_INTEGER),	/*return arg */
				NULL, NULL, NULL, NULL, NULL, NULL, NULL,
				NULL);
  ADD_METHOD (glo_class, GLO_METHOD_POSITION, "esm_Glo_position");
  (*define_instance_signature) (glo_class, GLO_METHOD_POSITION, db_get_type_name (DB_TYPE_BIGINT),	/*return arg */
				NULL, NULL, NULL, NULL, NULL, NULL, NULL,
				NULL);
  ADD_METHOD (glo_class, GLO_METHOD_LIKE_SEARCH, "esm_Glo_like_search");
  (*define_instance_signature) (glo_class, GLO_METHOD_LIKE_SEARCH, db_get_type_name (DB_TYPE_INTEGER),	/*return arg */
				db_get_type_name (DB_TYPE_STRING),	/*search str */
				NULL, NULL, NULL, NULL, NULL, NULL, NULL);
  ADD_METHOD (glo_class, GLO_METHOD_REG_SEARCH, "esm_Glo_reg_search");
  (*define_instance_signature) (glo_class, GLO_METHOD_REG_SEARCH, db_get_type_name (DB_TYPE_INTEGER),	/*return arg */
				db_get_type_name (DB_TYPE_STRING),	/*search str */
				NULL, NULL, NULL, NULL, NULL, NULL, NULL);
  ADD_METHOD (glo_class, GLO_METHOD_BINARY_SEARCH, "esm_Glo_binary_search");
  (*define_instance_signature) (glo_class, GLO_METHOD_BINARY_SEARCH, db_get_type_name (DB_TYPE_INTEGER),	/*return arg */
				db_get_type_name (DB_TYPE_STRING),	/*search str */
				db_get_type_name (DB_TYPE_INTEGER),	/*str length */
				NULL, NULL, NULL, NULL, NULL, NULL);
  ADD_METHOD (glo_class, GLO_METHOD_GET_ERROR, "esm_Glo_get_error");
  (*define_instance_signature) (glo_class, GLO_METHOD_GET_ERROR, db_get_type_name (DB_TYPE_INTEGER),	/*return arg */
				NULL, NULL, NULL, NULL, NULL, NULL, NULL,
				NULL);
  ADD_METHOD (glo_class, GLO_METHOD_SET_ERROR, "esm_Glo_set_error");
  (*define_instance_signature) (glo_class, GLO_METHOD_SET_ERROR, db_get_type_name (DB_TYPE_INTEGER),	/*return arg */
				db_get_type_name (DB_TYPE_INTEGER),	/*error arg */
				NULL, NULL, NULL, NULL, NULL, NULL, NULL);
  ADD_CLASS_METHOD (glo_class, GLO_CLASS_METHOD_NEW, "esm_Glo_create");
  (*define_class_signature) (glo_class, GLO_CLASS_METHOD_NEW, db_get_type_name (DB_TYPE_OBJECT),	/*return arg */
			     db_get_type_name (DB_TYPE_STRING),	/*str or NULL */
			     NULL, NULL, NULL, NULL, NULL, NULL, NULL);
  ADD_CLASS_METHOD (glo_class, GLO_CLASS_METHOD_NEW_LO, "esm_Glo_create_lo");
  (*define_class_signature) (glo_class, GLO_CLASS_METHOD_NEW_LO, db_get_type_name (DB_TYPE_OBJECT),	/*return arg */
			     NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
  ADD_CLASS_METHOD (glo_class, GLO_CLASS_METHOD_NEW_LO_IMPORT,
		    "esm_Glo_import_lo");
  (*define_class_signature) (glo_class, GLO_CLASS_METHOD_NEW_LO_IMPORT, db_get_type_name (DB_TYPE_OBJECT),	/*return arg */
			     db_get_type_name (DB_TYPE_STRING),	/*pname str */
			     NULL, NULL, NULL, NULL, NULL, NULL, NULL);
  ADD_CLASS_METHOD (glo_class, GLO_CLASS_METHOD_NEW_FBO,
		    "esm_Glo_create_fbo");
  (*define_class_signature) (glo_class, GLO_CLASS_METHOD_NEW_FBO, db_get_type_name (DB_TYPE_OBJECT),	/*return arg */
			     db_get_type_name (DB_TYPE_STRING),	/*pname str */
			     NULL, NULL, NULL, NULL, NULL, NULL, NULL);

  sprintf (tmp, "call %s(obj)", GLO_METHOD_DESTROY);
  trigger = db_create_trigger ("glo_delete_contents", TR_STATUS_ACTIVE, 1.0,
			       TR_EVENT_DELETE, glo_class, NULL,
			       TR_TIME_BEFORE, NULL,
			       TR_TIME_BEFORE, TR_ACT_EXPRESSION, tmp);

  grant_authorization (user, glo_class);
}

/* This routine just an aid to testing/adding new methods. */

/*
 * esm_add_method() -
 *      return: none
 *  class_name(in) :
 *  method_name(in) :
 *  implementation_name(in) :
 */
void
esm_add_method (char *class_name, char *method_name,
		char *implementation_name)
{
  DB_OBJECT *class_obj;

  class_obj = db_find_class (class_name);
  if (class_obj != NULL)
    {
      ADD_METHOD (class_obj, method_name, implementation_name);
    }
}
