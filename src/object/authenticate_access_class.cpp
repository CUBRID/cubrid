/*
 *
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
 * authenticate_access_class.cpp -
 */

#include "authenticate.h"

#include "authenticate_cache.hpp"
#include "authenticate_grant.hpp"

#include "boot_cl.h"
#include "dbi.h"
#include "execute_schema.h" /* do_recreate_filter_index_constr () */
#include "locator_cl.h"
#include "schema_manager.h"
#include "transform.h"
#include "transaction_cl.h"
#include "virtual_object.h"
#include "schema_system_catalog.hpp" /* sm_is_system_class () */

#define UNIQUE_SAVEPOINT_CHANGE_OWNER_WITH_RENAME "cHANGEoWNERwITHrENAME"
#define IS_CATALOG_CLASS(name) sm_is_system_class (std::string_view (name))

enum fetch_by
{
  DONT_KNOW,			/* Don't know the mop is a class os an instance */
  BY_INSTANCE_MOP,		/* fetch a class by an instance mop */
  BY_CLASS_MOP			/* fetch a class by the class mop */
};
typedef enum fetch_by FETCH_BY;

static int fetch_class (MOP op, MOP *return_mop, SM_CLASS **return_class, AU_FETCHMODE fetchmode, FETCH_BY fetch_by);
static int fetch_instance (MOP op, MOBJ *obj_ptr, AU_FETCHMODE fetchmode,
			   LC_FETCH_VERSION_TYPE read_fetch_version_type);

static int au_fetch_class_internal (MOP op, SM_CLASS **class_ptr, AU_FETCHMODE fetchmode, DB_AUTH type,
				    FETCH_BY fetch_by);

static int check_authorization (MOP classobj, SM_CLASS *sm_class, DB_AUTH type);
static int is_protected_class (MOP classmop, SM_CLASS *sm_class, DB_AUTH auth);

/*
 * au_change_owner - This changes the owning user of a class.
 *                   This should be called only by the DBA.
 *   return: error code
 *   classmop(in): class whose owner is to change
 *   owner(in): new owner
 */
int
au_change_owner (MOP class_mop, MOP owner_mop)
{
  SM_CLASS *class_ = NULL;
  SM_ATTRIBUTE *attr = NULL;
  SM_CLASS_CONSTRAINT *constraints = NULL;
  SM_CONSTRAINT_INFO *save_constraints = NULL;
  SM_CONSTRAINT_INFO *saved = NULL;
  MOBJ obj = NULL;
  char *class_old_name = NULL;
  char *class_new_name = NULL;
  char *owner_name = NULL;
  char downcase_owner_name[DB_MAX_USER_LENGTH] = { '\0' };
  char buf[DB_MAX_SERIAL_NAME_LENGTH] = { '\0' };
  bool has_savepoint = true;
  int save = 0;
  int error = NO_ERROR;

  if (class_mop == NULL || owner_mop == NULL)
    {
      ERROR_SET_WARNING (error, ER_AU_INVALID_ARGUMENTS);
      return error;
    }

  if (!au_is_dba_group_member (Au_user))
    {
      ERROR_SET_WARNING_1ARG (error, ER_AU_DBA_ONLY, "change_owner");
      return error;
    }

  AU_DISABLE (save);

  error = au_fetch_class_force (class_mop, &class_, AU_FETCH_UPDATE);
  if (error != NO_ERROR)
    {
      ASSERT_ERROR ();
      goto end;
    }

  if (ws_is_same_object (class_->owner, owner_mop))
    {
      goto end;
    }

  error = tran_system_savepoint (UNIQUE_SAVEPOINT_CHANGE_OWNER_WITH_RENAME);
  if (error != NO_ERROR)
    {
      ASSERT_ERROR ();
      goto end;
    }

  has_savepoint = true;

  /* Change serial object's owner when the class has auto_increment attribute column. */
  for (attr = class_->attributes; attr; attr = (SM_ATTRIBUTE *) attr->header.next)
    {
      if (attr->auto_increment)
	{
	  error = au_change_serial_owner (attr->auto_increment, owner_mop, true);
	  if (error != NO_ERROR)
	    {
	      ASSERT_ERROR ();
	      goto end;
	    }
	}
    }

  /* Change the owner of the class. */
  class_->owner = owner_mop;

  /* unique_name contains owner_name. if owner of class is changed, unique_name must be changed as well. */
  class_old_name = CONST_CAST (char *, sm_ch_name ((MOBJ) class_));

  /* unique_name of system class does not contain owner_name. unique_name does not need to be changed. */
  if (sm_check_system_class_by_name (class_old_name))
    {
      error = locator_flush_class (class_mop);
      if (error != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  goto end;
	}

      goto end;
    }

  owner_name = au_get_user_name (owner_mop);
  if (!owner_name)
    {
      ASSERT_ERROR_AND_SET (error);
      goto end;
    }
  sm_downcase_name (owner_name, downcase_owner_name, DB_MAX_USER_LENGTH);
  db_ws_free_and_init (owner_name);

  snprintf (buf, DB_MAX_IDENTIFIER_LENGTH, "%s.%s", downcase_owner_name, sm_remove_qualifier_name (class_old_name));
  class_new_name = db_private_strdup (NULL, buf);
  if (class_new_name == NULL)
    {
      ASSERT_ERROR_AND_SET (error);
      return error;
    }

  obj = locator_prepare_rename_class (class_mop, class_old_name, class_new_name);
  if (obj == NULL)
    {
      ASSERT_ERROR_AND_SET (error);
      goto end;
    }

  class_->header.ch_name = class_new_name;

  if (class_->class_type == SM_CLASS_CT && class_->constraints != NULL)
    {
      for (constraints = class_->constraints; constraints; constraints = constraints->next)
	{
	  if (constraints->type != SM_CONSTRAINT_INDEX
	      && constraints->type != SM_CONSTRAINT_REVERSE_INDEX
	      && constraints->type != SM_CONSTRAINT_UNIQUE && constraints->type != SM_CONSTRAINT_REVERSE_UNIQUE)
	    {
	      continue;
	    }

	  if (constraints->func_index_info || constraints->filter_predicate)
	    {
	      error = sm_save_constraint_info (&save_constraints, constraints);
	      if (error != NO_ERROR)
		{
		  ASSERT_ERROR ();
		  goto end;
		}

	      if (!save_constraints)
		{
		  ASSERT_ERROR_AND_SET (error);
		  goto end;
		}

	      saved = save_constraints;
	      while (saved->next)
		{
		  saved = saved->next;
		}

	      if (constraints->func_index_info)
		{
		  /* recompile function index expression */
		  error = do_recreate_func_index_constr (NULL, saved, NULL, NULL, class_old_name, class_new_name);
		  if (error != NO_ERROR)
		    {
		      ASSERT_ERROR ();
		      goto end;
		    }

		  continue;
		}

	      if (constraints->filter_predicate)
		{
		  /* recompile filter index expression */
		  error =
			  do_recreate_filter_index_constr (NULL, saved->filter_predicate, NULL, class_old_name,
			      class_new_name);
		  if (error != NO_ERROR)
		    {
		      ASSERT_ERROR ();
		      goto end;
		    }

		  continue;
		}
	    }
	}

      /* drop indexes */
      for (saved = save_constraints; saved; saved = saved->next)
	{
	  if (SM_IS_CONSTRAINT_UNIQUE_FAMILY ((SM_CONSTRAINT_TYPE) saved->constraint_type))
	    {
	      error = sm_drop_constraint (class_mop, saved->constraint_type, saved->name,
					  (const char **) saved->att_names, false, false);
	      if (error != NO_ERROR)
		{
		  ASSERT_ERROR ();
		  goto end;
		}
	    }
	  else
	    {
	      error = sm_drop_index (class_mop, saved->name);
	      if (error != NO_ERROR)
		{
		  ASSERT_ERROR ();
		  goto end;
		}
	    }
	}

      /* add indexes */
      for (saved = save_constraints; saved != NULL; saved = saved->next)
	{
	  error = sm_add_constraint (class_mop, saved->constraint_type, saved->name, (const char **) saved->att_names,
				     saved->asc_desc, saved->prefix_length, false, saved->filter_predicate,
				     saved->func_index_info, saved->comment, saved->index_status);
	  if (error != NO_ERROR)
	    {
	      ASSERT_ERROR ();
	      goto end;
	    }
	}
    }

  error = locator_flush_class (class_mop);
  if (error != NO_ERROR)
    {
      ASSERT_ERROR ();
      goto end;
    }

  if (class_old_name)
    {
      db_private_free_and_init (NULL, class_old_name);
    }

  class_new_name = NULL;

end:
  AU_ENABLE (save);

  if (class_new_name)
    {
      db_private_free_and_init (NULL, class_new_name);
    }

  if (save_constraints)
    {
      sm_free_constraint_info (&save_constraints);
    }

  if (has_savepoint && error != NO_ERROR && error != ER_LK_UNILATERALLY_ABORTED)
    {
      tran_abort_upto_system_savepoint (UNIQUE_SAVEPOINT_CHANGE_OWNER_WITH_RENAME);
    }

  return (error);
}

/*
 * au_check_class_authorization - This is similar to au_fetch_class except that
 *                          it only checks for available authorization and
 *                          doesn't actually return the fetched class
 *   return: error code
 *   op(in): class object
 *   auth(in): authorization type biits
 *
 * Note: Where it differs most is that it doesn't check  the Au_disable flag.
 *       This means that it can be used when authorization
 *       is temporarily disabled to get the true status for a class.
 *       This is important for modules like the trigger manager that often
 *       run with authorization disabled but which need to verify access
 *       rights for the current user.
 */
int
au_check_class_authorization (MOP op, DB_AUTH auth)
{
  int error = NO_ERROR;
  SM_CLASS *class_;
  int save;

  /*
   * It seems to be simplest to just override the Au_disable
   * flag and call au_fetch_class normally.  If this turns
   * out to be a problem, will have to duplicate some of
   * au_fetch_class here.
   */

  save = Au_disable;
  Au_disable = 0;
  error = au_fetch_class (op, &class_, AU_FETCH_READ, auth);
  Au_disable = save;

  return error;
}

/*
 * CLASS ACCESSING
 */

/*
 * fetch_class - Work function for au_fetch_class.
 *   return: error code
 *   op(in): class or instance MOP
 *   return_mop(out): returned class MOP
 *   return_class(out): returned class structure
 *   fetchmode(in): desired fetch/locking mode
 */
static int
fetch_class (MOP op, MOP *return_mop, SM_CLASS **return_class, AU_FETCHMODE fetchmode, FETCH_BY fetch_by)
{
  int error = NO_ERROR;
  MOP classmop = NULL;
  SM_CLASS *class_ = NULL;

  *return_mop = NULL;
  *return_class = NULL;

  if (op == NULL)
    {
      return ER_FAILED;
    }

  classmop = NULL;
  class_ = NULL;

  if (fetch_by == BY_CLASS_MOP)
    {
      /* already know classmop */
      classmop = op;
    }
  else
    {
      int is_class;
      DB_FETCH_MODE purpose;

      if (fetchmode == AU_FETCH_READ)
	{
	  purpose = DB_FETCH_READ;
	}
      else
	{
	  purpose = DB_FETCH_WRITE;
	}

      is_class = locator_is_class (op, purpose);

      if (is_class < 0)
	{
	  return is_class;
	}
      else if (is_class > 0)
	{
	  classmop = op;
	}
      else
	{
	  classmop = ws_class_mop (op);
	}
    }

  /* the locator_fetch_class_of_instance doesn't seem to be working right now */
#if 0
  if (classmop == NULL)
    {
      if ((error = classmop_from_instance (op, &classmop)) != NO_ERROR)
	{
	  return (error);
	}
    }
#endif /* 0 */

  if (classmop != NULL)
    {
      switch (fetchmode)
	{
	case AU_FETCH_READ:
	  class_ = (SM_CLASS *) locator_fetch_class (classmop, DB_FETCH_READ);
	  break;
	case AU_FETCH_SCAN:
	  class_ = (SM_CLASS *) locator_fetch_class (classmop, DB_FETCH_SCAN);
	  break;
	case AU_FETCH_EXCLUSIVE_SCAN:
	  class_ = (SM_CLASS *) locator_fetch_class (classmop, DB_FETCH_EXCLUSIVE_SCAN);
	  break;
	case AU_FETCH_WRITE:
	  class_ = (SM_CLASS *) locator_fetch_class (classmop, DB_FETCH_WRITE);
	  break;
	case AU_FETCH_UPDATE:
	  class_ = (SM_CLASS *) locator_update_class (classmop);
	  break;
	}
    }
  else
    {
      switch (fetchmode)
	{
	case AU_FETCH_READ:
	  class_ = (SM_CLASS *) locator_fetch_class_of_instance (op, &classmop, DB_FETCH_READ);
	  break;
	case AU_FETCH_SCAN:
	case AU_FETCH_EXCLUSIVE_SCAN:
	  /* AU_FETCH_SCAN, AU_FETCH_EXCLUSIVE_SCAN are allowed only for class mops. */
	  assert (0);
	  break;
	case AU_FETCH_WRITE:
	  class_ = (SM_CLASS *) locator_fetch_class_of_instance (op, &classmop, DB_FETCH_WRITE);
	  break;
	case AU_FETCH_UPDATE:
	  class_ = (SM_CLASS *) locator_fetch_class_of_instance (op, &classmop, DB_FETCH_WRITE);
	  if (class_ != NULL)
	    {
	      /*
	       * all this appreciably does is set the dirty flag in the MOP
	       * should have the "dirty after getting write lock" operation
	       * separated
	       */
	      class_ = (SM_CLASS *) locator_update_class (classmop);
	    }
	  break;
	}
    }

  /* I've seen cases where locator_fetch has an error but doesn't return NULL ?? */
  /* this is debug only, take out in production */
  /*
   * if (class_ != NULL && Db_error != NO_ERROR)
   * au_log(Db_error, "Inconsistent error handling ?");
   */

  if (class_ == NULL)
    {
      /* does it make sense to check WS_IS_DELETED here ? */
      error = er_errid ();
      /* !!! do we need to mask the error here ? */

      /*
       * if the object was deleted, set the workspace bit so we can avoid this
       * in the future
       */
      if (error == ER_HEAP_UNKNOWN_OBJECT)
	{
	  if (classmop != NULL)
	    {
	      WS_SET_DELETED (classmop);
	    }
	  else
	    {
	      WS_SET_DELETED (op);
	    }
	}
      else if (error == NO_ERROR)
	{
	  /* return NO_ERROR only if class_ is not null. */
	  error = ER_FAILED;
	}
    }
  else
    {
      *return_mop = classmop;
      *return_class = class_;
    }

  return error;
}

/*
 * au_fetch_class_internal - helper function for au_fetch_class families
 *   return: error code
 *   op(in): class or instance
 *   class_ptr(out): returned pointer to class structure
 *   fetchmode(in): type of fetch/lock to obtain
 *   type(in): authorization type to check
 *   fetch_by(in): DONT_KNOW, BY_INSTANCE_MOP or BY_CLASS_MOP
 */
int
au_fetch_class_internal (MOP op, SM_CLASS **class_ptr, AU_FETCHMODE fetchmode, DB_AUTH type, FETCH_BY fetch_by)
{
  int error = NO_ERROR;
  SM_CLASS *class_;
  MOP classmop;

  if (class_ptr != NULL)
    {
      *class_ptr = NULL;
    }

  if (op == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OBJ_INVALID_ARGUMENTS, 0);
      return ER_OBJ_INVALID_ARGUMENTS;
    }

  if (Au_user == NULL && !Au_disable)
    {
      error = ER_AU_INVALID_USER;
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, error, 1, "");
      return error;
    }

  if (fetchmode != AU_FETCH_READ	/* not just reading */
      || WS_IS_DELETED (op)	/* marked deleted */
      || op->object == NULL	/* never been fetched */
      || op->class_mop != sm_Root_class_mop	/* not a class */
      || op->lock < SCH_S_LOCK)	/* don't have the lowest level lock */
    {
      /* go through the usual fetch process */
      error = fetch_class (op, &classmop, &class_, fetchmode, fetch_by);
      if (error != NO_ERROR)
	{
	  return error;
	}
    }
  else
    {
      /* looks like a basic read fetch, check authorization only */
      classmop = op;
      class_ = (SM_CLASS *) op->object;
    }

  /* check class representation directory */
  if (OID_ISNULL (sm_ch_rep_dir ((MOBJ) class_)))
    {
      if (!locator_is_root (classmop))
	{
	  error = sm_check_catalog_rep_dir (classmop, class_);
	  if (error != NO_ERROR)
	    {
	      return error;
	    }

#if !defined(NDEBUG)
	  if (!OID_ISTEMP (WS_OID (classmop)))
	    {
	      assert (!OID_ISNULL (sm_ch_rep_dir ((MOBJ) class_)));
	    }
#endif
	}
    }

  if ((Au_disable && type != DB_AUTH_ALTER) || ! (error = check_authorization (classmop, class_, type)))
    {
      if (class_ptr != NULL)
	{
	  *class_ptr = class_;
	}
    }

  return error;
}

/*
 * au_fetch_class - This is the primary function for accessing a class
 *   return: error code
 *   op(in): class or instance
 *   class_ptr(out): returned pointer to class structure
 *   fetchmode(in): type of fetch/lock to obtain
 *   type(in): authorization type to check
 *
 */
int
au_fetch_class (MOP op, SM_CLASS **class_ptr, AU_FETCHMODE fetchmode, DB_AUTH type)
{
  return au_fetch_class_internal (op, class_ptr, fetchmode, type, DONT_KNOW);
}

/*
 * au_fetch_class_by_classmop - This is the primary function
 *                  for accessing a class by an instance mop.
 *   return: error code
 *   op(in): class or instance
 *   class_ptr(out): returned pointer to class structure
 *   fetchmode(in): type of fetch/lock to obtain
 *   type(in): authorization type to check
 *
 */
int
au_fetch_class_by_instancemop (MOP op, SM_CLASS **class_ptr, AU_FETCHMODE fetchmode, DB_AUTH type)
{
  return au_fetch_class_internal (op, class_ptr, fetchmode, type, BY_INSTANCE_MOP);
}

/*
 * au_fetch_class_by_classmop - This is the primary function
 *                  for accessing a class by a class mop.
 *   return: error code
 *   op(in): class or instance
 *   class_ptr(out): returned pointer to class structure
 *   fetchmode(in): type of fetch/lock to obtain
 *   type(in): authorization type to check
 *
 */
int
au_fetch_class_by_classmop (MOP op, SM_CLASS **class_ptr, AU_FETCHMODE fetchmode, DB_AUTH type)
{
  return au_fetch_class_internal (op, class_ptr, fetchmode, type, BY_CLASS_MOP);
}

/*
 * au_fetch_class_force - This is like au_fetch_class except that it will
 *                        not check for authorization.
 *   return: error code
 *   op(in): class or instance MOP
 *   class(out): returned pointer to class structure
 *   fetchmode(in): desired operation
 *
 * Note: Used in a few special cases where authorization checking is
 *       to be disabled.
 */
int
au_fetch_class_force (MOP op, SM_CLASS **class_, AU_FETCHMODE fetchmode)
{
  MOP classmop;

  return (fetch_class (op, &classmop, class_, fetchmode, DONT_KNOW));
}

/*
 * INSTANCE ACCESSING
 */

/*
 * au_fetch_instance - This is the primary interface function for accessing
 *                     an instance.
 *   return: error code
 *   op(in): instance MOP
 *   obj_ptr(in):returned pointer to instance memory
 *   mode(in): access type
 *   type(in): authorization type
 *   fetch_version_type(in): fetch version type
 *
 * Note: Fetch the object from the database if necessary, update the class
 *       authorization cache if necessary and check authorization for the
 *       desired operation.
 *
 * Note: If op is a VMOP au_fetch_instance will return set obj_ptr as a
 *       pointer to the BASE INSTANCE memory which is not the instance
 *       associated with op. Therefore, the object returned is not necessarily
 *       the contents of the supplied MOP.
 */
/*
 * TODO We need to carefully examine all callers of au_fetch_instance and make
 *      sure they know that the object returned is not necessarily the
 *      contents of the supplied MOP.
 */
int
au_fetch_instance (MOP op, MOBJ *obj_ptr, AU_FETCHMODE mode, LC_FETCH_VERSION_TYPE fetch_version_type, DB_AUTH type)
{
  int error = NO_ERROR;
  SM_CLASS *class_;
  MOP classmop;

  if (Au_user == NULL && !Au_disable)
    {
      error = ER_AU_INVALID_USER;
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, error, 1, "");
      return error;
    }

  error = fetch_class (op, &classmop, &class_, AU_FETCH_READ, BY_INSTANCE_MOP);
  if (error != NO_ERROR)
    {
      /*
       * the class was deleted, make sure the instance MOP also gets
       * the deleted bit set so the upper levels can depend on this
       * behavior
       */
      if (error == ER_HEAP_UNKNOWN_OBJECT)
	{
	  WS_SET_DELETED (op);
	}
      return error;
    }

  if (Au_disable || ! (error = check_authorization (classmop, class_, type)))
    {
      error = fetch_instance (op, obj_ptr, mode, fetch_version_type);
    }

  return error;
}

/*
 * au_fetch_instance_force - Like au_fetch_instance but doesn't check
 *                           for authorization.  Used in special circumstances
 *                           when authorization is disabled.
 *   return: error code
 *   op(in): instance MOP
 *   obj_ptr(out): returned instance memory pointer
 *   fetchmode(in): access type
 *   fetch_version_type(in): fetch version type
 */
int
au_fetch_instance_force (MOP op, MOBJ *obj_ptr, AU_FETCHMODE fetchmode, LC_FETCH_VERSION_TYPE fetch_version_type)
{
  return (fetch_instance (op, obj_ptr, fetchmode, fetch_version_type));
}

/*
 * fetch_instance - Work function for au_fetch_instance.
 *   return: error code
 *   op(in): instance MOP
 *   obj_ptr(out): returned pointer to object
 *   fetchmode(in): desired operation
 *   fetch_version_type(in): fetch version type, currently used in case of
 *			    read/write fetch
 */
static int
fetch_instance (MOP op, MOBJ *obj_ptr, AU_FETCHMODE fetchmode, LC_FETCH_VERSION_TYPE fetch_version_type)
{
  int error = NO_ERROR;
  MOBJ obj = NULL;
  int pin;
  int save;

  if (obj_ptr != NULL)
    {
      *obj_ptr = NULL;
    }

  if (op == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OBJ_INVALID_ARGUMENTS, 0);
      return ER_OBJ_INVALID_ARGUMENTS;
    }

  /* refuse attempts to fetch temporary objects */
  if (op->is_temp)
    {
      error = ER_OBJ_INVALID_TEMP_OBJECT;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
      return error;
    }

  /* DO NOT PUT ANY RETURNS FROM HERE UNTIL THE AU_ENABLE */
  AU_DISABLE (save);

  pin = ws_pin (op, 1);
  if (op->is_vid)
    {
      switch (fetchmode)
	{
	case AU_FETCH_READ:
	  obj = vid_fetch_instance (op, DB_FETCH_READ, fetch_version_type);
	  break;
	case AU_FETCH_WRITE:
	  obj = vid_fetch_instance (op, DB_FETCH_WRITE, fetch_version_type);
	  break;
	case AU_FETCH_UPDATE:
	  obj = vid_upd_instance (op);
	  break;
	case AU_FETCH_SCAN:
	case AU_FETCH_EXCLUSIVE_SCAN:
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OBJ_INVALID_ARGUMENTS, 0);
	  assert (0);
	  break;
	}
    }
  else
    {
      switch (fetchmode)
	{
	case AU_FETCH_READ:
	  obj = locator_fetch_instance (op, DB_FETCH_READ, fetch_version_type);
	  break;
	case AU_FETCH_WRITE:
	  obj = locator_fetch_instance (op, DB_FETCH_WRITE, fetch_version_type);
	  break;
	case AU_FETCH_UPDATE:
	  obj = locator_update_instance (op);
	  break;
	case AU_FETCH_SCAN:
	case AU_FETCH_EXCLUSIVE_SCAN:
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OBJ_INVALID_ARGUMENTS, 0);
	  assert (0);
	  break;
	}
    }

  (void) ws_pin (op, pin);

  if (obj == NULL)
    {
      /* does it make sense to check WS_IS_DELETED here ? */
      assert (er_errid () != NO_ERROR);
      error = er_errid ();

      /*
       * if the object was deleted, set the workspace bit so we can avoid this
       * in the future
       */
      if (error == ER_HEAP_UNKNOWN_OBJECT)
	{
	  WS_SET_DELETED (op);
	}
      else if (error == NO_ERROR)
	{
	  /* return NO_ERROR only if obj is not null. */
	  error = ER_FAILED;
	}
    }
  else if (obj_ptr != NULL)
    {
      *obj_ptr = obj;
    }

  AU_ENABLE (save);

  return error;
}


/*
 * check_authorization - This is the core routine for checking authorization
 *                       on a class.
 *   return: error code
 *   classobj(in): class MOP
 *   class(in): class structure
 *   type(in): authorization type
 *
 * TODO : LP64
 */
static int
check_authorization (MOP classobj, SM_CLASS *sm_class, DB_AUTH type)
{
  int error = NO_ERROR;
  unsigned int *bits;

  /*
   * Callers generally check Au_disable already to avoid the function call.
   * Check it again to be safe, at this point, it isn't going to add anything.
   */
  if (Au_disable)
    {
      int client_type = db_get_client_type ();
      if (!BOOT_ADMIN_CSQL_CLIENT_TYPE (client_type) || ! (sm_class->flags & SM_CLASSFLAG_SYSTEM))
	{
	  return NO_ERROR;
	}
    }

  /* try to catch attempts by even the DBA to update a protected class */
  if ((sm_class->flags & SM_CLASSFLAG_SYSTEM) && is_protected_class (classobj, sm_class, type))
    {
      error = appropriate_error (0, type);
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, error, 0);
    }
  else
    {
      bits = Au_cache.get_cache_bits (sm_class);
      if (bits == NULL)
	{
	  assert (false);
	  return er_errid ();
	}

      if ((*bits & type) != type)
	{
	  if (*bits == AU_CACHE_INVALID)
	    {
	      /* update the cache and try again */
	      error = Au_cache.update (classobj, sm_class);
	      if (error == NO_ERROR)
		{
		  bits = Au_cache.get_cache_bits (sm_class);
		  if (bits == NULL)
		    {
		      return er_errid ();
		    }
		  if ((*bits & type) != type)
		    {
		      error = appropriate_error (*bits, type);
		      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, error, 0);
		    }
		}
	    }
	  else
	    {
	      error = appropriate_error (*bits, type);
	      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, error, 0);
	    }
	}
    }

  return error;
}

/*
 * is_protected_class - This is a hack to detect attempts to modify one
 *                      of the protected system classes.
 *   return: non-zero if class is protected
 *   classmop(in): class MOP
 *   class(in): class structure
 *   auth(in): authorization type
 *
 * Note: This is necessary because normally when DBA is logged in,
 *       authorization is effectively disabled.
 *       This should be handled by another "system" level of authorization.
 */
static int
is_protected_class (MOP classmop, SM_CLASS *sm_class, DB_AUTH auth)
{
  int illegal = 0;

  if (classmop == Au_authorizations_class || IS_CATALOG_CLASS (sm_ch_name ((MOBJ) sm_class)))
    {
      illegal = auth & (AU_ALTER | AU_DELETE | AU_INSERT | AU_UPDATE | AU_INDEX);
    }
  else if (sm_issystem (sm_class))
    {
      /* if the class is a system class_, can't alter */
      illegal = auth & (AU_ALTER);
    }
  return illegal;
}
