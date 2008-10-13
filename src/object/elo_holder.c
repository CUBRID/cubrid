/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * elo_holder.c - Holder of the elo.
 *
 * Note : Holder of the elo. This object is tho only object in the system that 
 *        can have an elo primitive type. This is because we want to make sure 
 *        that any user of GLOs will get the one and only Holder that points 
 *        to a particular file. This means that Holders cannot be inherited 
 *        or copied, oreven known by the user.
 * TODO: include elo_holder2.c
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>

#include "glo_class.h"
#include "elo_holder.h"
#include "db.h"
#include "elo_class.h"
/* this must be the last header file included!!! */
#include "dbval.h"

static DB_OBJECT *Glo_create_name (const char *path_name,
				   DB_OBJECT * glo_holder);

/*
 * Glo_create_name() - create glo object
 *      return: return_value contains the newly created pathname object
 *  path_name(in) : Pathname for an fbo
 *  glo_holder(in) : the glo_holder object associated with this name.  
 * 
 * Note : 
 *   returns a unique pathname object. If a filename is passed as an argument 
 *   we check if a pathname object already exists with that filename. If so,  
 *   that pathname object is returned. Otherwise, a new pathname object is    
 *   created and returned. If the filename is NULL, we will return an LO.     
 */
static DB_OBJECT *
Glo_create_name (const char *path_name, DB_OBJECT * glo_holder)
{
  int rc;
  DB_VALUE value;
  DB_OBJECT *GLO_name_class, *name_object = NULL;
  int save;

  db_make_string (&value, path_name);
  AU_DISABLE (save);
  GLO_name_class = db_find_class (GLO_NAME_CLASS_NAME);
  if (GLO_name_class == NULL)
    {
      goto end;
    }
  name_object = db_create_internal (GLO_name_class);

  if (name_object == NULL)
    {
      goto end;
    }

  rc = db_put_internal (name_object, GLO_NAME_PATHNAME, &value);

  if (rc != 0)
    {
      db_drop (name_object);
      name_object = NULL;
    }
  else
    {
      db_make_object (&value, glo_holder);
      rc = db_put_internal (name_object, GLO_NAME_HOLDER_PTR, &value);
      if (rc != 0)
	{
	  db_drop (name_object);
	  name_object = NULL;
	}
    }
end:
  AU_ENABLE (save);
  return (name_object);
}

/*
 * Glo_holder_create() - create glo object
 *      return: return_value contains the newly created pathname object
 *  this_p(in) : glo object 
 *  return_argument_p(out) : arguments (none)    
 *  path_name(in) : Pathname for an fbo, NULL for an LO
 * 
 * Note : 
 *   returns a unique pathname object. If a filename is passed as an argument 
 *   we check if a pathname object already exists with that filename. If so,  
 *   that pathname object is returned. Otherwise, a new pathname object is    
 *   created and returned. If the filename is NULL, we will return an LO.     
 */
void
Glo_create_holder (DB_OBJECT * this_p, DB_VALUE * return_argument_p,
		   const DB_VALUE * path_name)
{
  DB_ELO *glo_p;
  DB_OBJECT *holder_object_p, *Glo_name_object_p;
  char *pathname = NULL;
  DB_VALUE value;
  int save;

  if (path_name != NULL)
    {
      if (DB_VALUE_TYPE (path_name) != DB_TYPE_NULL)
	{
	  if ((path_name == NULL) || !IS_STRING (path_name))
	    {
	      esm_set_error (INVALID_STRING_INPUT_ARGUMENT);
	      return;
	    }

	}
    }

  if ((path_name == NULL) ||
      (DB_VALUE_TYPE (path_name) == DB_TYPE_NULL) ||
      ((pathname = (char *) DB_GET_STRING (path_name)) == NULL))
    {
      glo_p = elo_create (NULL);
    }
  else
    {
      holder_object_p = esm_find_holder_object (pathname);
      if (holder_object_p != NULL)
	{
	  db_make_object (return_argument_p, holder_object_p);
	  return;
	}
      else
	{
	  glo_p = elo_create (pathname);
	}
    }
  AU_DISABLE (save);
  holder_object_p = db_create_internal (this_p);

  if (holder_object_p == NULL)
    {
      goto error;
    }
  /* Make the name object and save the pathname in the GLO object */
  if (pathname)
    {
      Glo_name_object_p = Glo_create_name (pathname, holder_object_p);
      if (Glo_name_object_p != NULL)
	{
	  db_make_object (&value, Glo_name_object_p);
	}
      db_put_internal (holder_object_p, GLO_HOLDER_NAME_PTR, &value);
    }
  /* Save the glo primitive type */
  db_make_elo (&value, glo_p);
  db_put_internal (holder_object_p, GLO_HOLDER_GLO_NAME, &value);

  db_make_object (return_argument_p, holder_object_p);
  AU_ENABLE (save);
  return;

error:
  db_make_object (return_argument_p, NULL);
  AU_ENABLE (save);

}

/*
 * Glo_holder_lock()  - lock glo object
 *      return: none
 *  this_p(in) : glo object  
 *  return_argument_p(out) : if lock success then return true 
 *                            else return Error CODE    
 * 
 */
void
Glo_lock_holder (DB_OBJECT * this_p, DB_VALUE * return_argument_p)
{
  DB_VALUE value;
  int save;
  int error;

  db_make_int (return_argument_p, true);

  AU_DISABLE (save);
  db_make_int (&value, 1);
  error = db_lock_write (this_p);

  if (error != NO_ERROR)
    {
      /* return a DB_TYPE_ERROR to indicate a problem occurred */
      db_make_error (return_argument_p, error);
    }
  AU_ENABLE (save);
}
