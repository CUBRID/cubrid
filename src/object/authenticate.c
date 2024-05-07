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
 * authenticate.c - Authorization manager
 */

#ident "$Id$"

/*
 * Note:
 * Need to remove calls to the db_ layer since there are some
 * nasty dependency problems during restart and when the server
 * crashes since we need to touch objects before the database is
 * officially open.
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>

#include "porting.h"
#include "misc_string.h"
#include "memory_alloc.h"
#include "dbtype.h"
#include "error_manager.h"
#include "work_space.h"
#include "object_primitive.h"
#include "class_object.h"
#include "schema_manager.h"
#include "authenticate.h"
#include "object_accessor.h"
#include "encryption.h"
#include "crypt_opfunc.h"
#include "message_catalog.h"
#include "string_opfunc.h"
#include "transaction_cl.h"
#include "db.h"
#include "trigger_manager.h"
#include "transform.h"
#include "schema_system_catalog_constants.h"
#include "environment_variable.h"
#include "jsp_cl.h"
#include "object_print.h"
#include "execute_schema.h"	/* UNIQUE_PARTITION_SAVEPOINT_OWNER */
#include "execute_statement.h"	/* do_get_serial_obj_id () */
#include "optimizer.h"
#include "network_interface_cl.h"
#include "printer.hpp"
#include "authenticate_cache.hpp"
#include "authenticate_grant.hpp"

#if defined (SUPPRESS_STRLEN_WARNING)
#define strlen(s1)  ((int) strlen(s1))
#endif /* defined (SUPPRESS_STRLEN_WARNING) */

#if defined(SA_MODE)
extern bool catcls_Enable;
#endif /* SA_MODE */

/* Macro to determine if a name is system catalog class */

/*
 * AU_OBJECT_CLASS_NAME
 *
 * This is list of class names that CUBRID manages as database objects
 * Their existence is checked when dropping an user
 */
const char *AU_OBJECT_CLASS_NAME[] = {
  CT_CLASS_NAME,		/* AU_OBJECT_CLASS */
  CT_TRIGGER_NAME,		/* AU_OBJECT_TRIGGER */
  CT_SERIAL_NAME,		/* AU_OBJECT_SERIAL */
  CT_DB_SERVER_NAME,		/* AU_OBJECT_SERVER */
  CT_SYNONYM_NAME,		/* AU_OBJECT_SYNONYM */
  CT_STORED_PROC_NAME,		/* AU_OBJECT_PROCEDURE */
  NULL
};

/*
 *
 *
 * 
 */
authenticate_context *au_ctx = nullptr;

void
au_init (void)
{
  if (au_ctx == nullptr)
    {
      au_ctx = new authenticate_context ();
    }
}

void
au_final (void)
{
  if (au_ctx != nullptr)
    {
      delete au_ctx;
      au_ctx = nullptr;
    }
}

int
au_login (const char *name, const char *password, bool ignore_dba_privilege)
{
  au_init ();
  return au_ctx->login (name, password, ignore_dba_privilege);
}

static void au_print_grant_entry (DB_SET * grants, int grant_index, FILE * fp);
static void au_print_auth (MOP auth, FILE * fp);

/*
 * DB_ EXTENSION FUNCTIONS
 */


/*
 * au_get_set
 *   return: error code
 *   obj(in):
 *   attname(in):
 *   set(in):
 */
int
au_get_set (MOP obj, const char *attname, DB_SET ** set)
{
  int error = NO_ERROR;
  DB_VALUE value;

  *set = NULL;
  error = obj_get (obj, attname, &value);
  if (error == NO_ERROR)
    {
      if (!TP_IS_SET_TYPE (DB_VALUE_TYPE (&value)))
	{
	  error = ER_OBJ_DOMAIN_CONFLICT;
	}
      else
	{
	  if (DB_IS_NULL (&value))
	    {
	      *set = NULL;
	    }
	  else
	    {
	      *set = db_get_set (&value);
	    }

	  /*
	   * since we almost ALWAYS end up iterating through the sets fetching
	   * objects, do a vector fetch immediately to avoid
	   * multiple server calls.
	   * Should have a sub db_ function for doing this.
	   */
	  if (*set != NULL)
	    {
	      error = db_fetch_set (*set, DB_FETCH_READ, 0);
	      if (error == NO_ERROR)
		{
		  error = set_filter (*set);
		}
	      /*
	       * shoudl be detecting the filtered elements and marking the
	       * object dirty if possible
	       */
	    }
	}
    }
  return (error);
}

/*
 * au_get_object
 *   return: error code
 *   obj(in):
 *   attname(in):
 *   mop_ptr(in):
 */
int
au_get_object (MOP obj, const char *attname, MOP * mop_ptr)
{
  int error = NO_ERROR;
  DB_VALUE value;

  *mop_ptr = NULL;
  error = obj_get (obj, attname, &value);
  if (error == NO_ERROR)
    {
      if (DB_VALUE_TYPE (&value) != DB_TYPE_OBJECT)
	{
	  error = ER_OBJ_DOMAIN_CONFLICT;
	}
      else
	{
	  if (DB_IS_NULL (&value))
	    {
	      *mop_ptr = NULL;
	    }
	  else
	    {
	      *mop_ptr = db_get_object (&value);
	    }
	}
    }
  return (error);
}

/*
 * au_set_get_obj -
 *   return: error code
 *   set(in):
 *   index(in):
 *   obj(out):
 */
int
au_set_get_obj (DB_SET * set, int index, MOP * obj)
{
  int error = NO_ERROR;
  DB_VALUE value;

  *obj = NULL;

  error = set_get_element (set, index, &value);
  if (error == NO_ERROR)
    {
      if (DB_VALUE_TYPE (&value) != DB_TYPE_OBJECT)
	{
	  error = ER_OBJ_DOMAIN_CONFLICT;
	}
      else
	{
	  if (DB_IS_NULL (&value))
	    {
	      *obj = NULL;
	    }
	  else
	    {
	      *obj = db_get_object (&value);
	    }
	}
    }

  return error;
}

/*
 * au_delete_auth_of_dropping_table - delete _db_auth records refers to the given table.
 *   return: error code
 *   class_name(in): the class name to be dropped
 */
int
au_delete_auth_of_dropping_table (const char *class_name)
{
  int error = NO_ERROR, save;
  const char *sql_query =
    "DELETE FROM [" CT_CLASSAUTH_NAME "] [au]" " WHERE [au].[class_of] IN" " (SELECT [cl] FROM " CT_CLASS_NAME
    " [cl] WHERE [unique_name] = ?);";
  DB_VALUE val;
  DB_QUERY_RESULT *result = NULL;
  DB_SESSION *session = NULL;
  int stmt_id;

  db_make_null (&val);

  /* Disable the checking for internal authorization object access */
  AU_DISABLE (save);

  assert (class_name != NULL);

  session = db_open_buffer_local (sql_query);
  if (session == NULL)
    {
      ASSERT_ERROR_AND_SET (error);
      goto exit;
    }

  error = db_set_system_generated_statement (session);
  if (error != NO_ERROR)
    {
      goto release;
    }

  stmt_id = db_compile_statement_local (session);
  if (stmt_id < 0)
    {
      ASSERT_ERROR_AND_SET (error);
      goto release;
    }

  db_make_string (&val, class_name);
  error = db_push_values (session, 1, &val);
  if (error != NO_ERROR)
    {
      goto release;
    }

  error = db_execute_statement_local (session, stmt_id, &result);
  if (error < 0)
    {
      goto release;
    }

  error = db_query_end (result);

release:
  if (session != NULL)
    {
      db_close_session (session);
    }

exit:
  pr_clear_value (&val);

  AU_ENABLE (save);

  return error;
}

/*
 * MISC UTILITIES
 */

/*
 * au_change_serial_owner() - Change serial object's owner
 *   return: error code
 *   object(in/out): serial object whose owner is to be changed
 *   new_owner(in): new owner
 *   is_auto_increment(in): check if auto increment serial name change is necessary
 */
int
au_change_serial_owner (MOP serial_mop, MOP owner_mop, bool by_class_owner_change)
{
  DB_OBJECT *serial_owner_obj = NULL;
  DB_OBJECT *serial_class_mop = NULL;
  DB_OBJECT *serial_obj = NULL;
  DB_IDENTIFIER serial_obj_id;
  DB_OTMPL *obj_tmpl = NULL;
  DB_VALUE value;
  const char *serial_name = NULL;
  char serial_new_name[DB_MAX_SERIAL_NAME_LENGTH] = { '\0' };
  const char *att_name = NULL;
  char *owner_name = NULL;
  char downcase_owner_name[DB_MAX_USER_LENGTH] = { '\0' };
  bool is_abort = false;
  int save = 0;
  int error = NO_ERROR;

  if (!serial_mop || !owner_mop)
    {
      ERROR_SET_WARNING (error, ER_OBJ_INVALID_ARGUMENTS);
      return error;
    }

  OID_SET_NULL (&serial_obj_id);

  if (!au_is_dba_group_member (Au_user))
    {
      ERROR_SET_WARNING_1ARG (error, ER_AU_DBA_ONLY, "change_serial_owner");
      return error;
    }

  AU_DISABLE (save);

  /*
   * class, serial, and trigger distinguish user schema by unique_name (user_specified_name).
   * so if the owner of class, serial, trigger changes, the unique_name must also change.
   */

  /*
   * after serial.next_value, the currect value maybe changed, but cub_cas
   * still hold the old value. To get the new value. we need decache it
   * then refetch it from server again.
   */
  assert (WS_ISDIRTY (serial_mop) == false);

  ws_decache (serial_mop);

  /* no need to get last version for serial - actually, the purpose is AU_FETCH_WRITE, so fetch type is not relevant;
   * the last version will be locked and it will be considered visibile only if delid is not set */
  error = au_fetch_instance_force (serial_mop, NULL, AU_FETCH_WRITE, LC_FETCH_MVCC_VERSION);
  if (error != NO_ERROR)
    {
      ASSERT_ERROR ();
      goto end;
    }

  if (!by_class_owner_change)
    {
      /* It can be checked as one of unique_name, class_name, and att_name. */
      error = obj_get (serial_mop, SERIAL_ATTR_ATT_NAME, &value);
      if (error != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  goto end;
	}

      if (!DB_IS_NULL (&value))
	{
	  ERROR_SET_WARNING (error, ER_AU_CANT_ALTER_OWNER_OF_AUTO_INCREMENT);
	  goto end;
	}
    }

  /* Check if the owner to be changed is the same. */
  error = obj_get (serial_mop, SERIAL_ATTR_OWNER, &value);
  if (error != NO_ERROR)
    {
      ASSERT_ERROR ();
      goto end;
    }

  if (DB_VALUE_DOMAIN_TYPE (&value) != DB_TYPE_OBJECT || (serial_owner_obj = db_get_object (&value)) == NULL)
    {
      /* Unable to get attribute value. */
      ERROR_SET_WARNING (error, ER_OBJ_INVALID_ARGUMENTS);
      goto end;
    }

  if (ws_is_same_object (serial_owner_obj, owner_mop))
    {
      goto end;
    }

  error = obj_get (serial_mop, SERIAL_ATTR_NAME, &value);
  if (error != NO_ERROR)
    {
      ASSERT_ERROR ();
      goto end;
    }

  if (!DB_IS_STRING (&value) || (serial_name = db_get_string (&value)) == NULL)
    {
      /* Unable to get attribute value. */
      ERROR_SET_WARNING (error, ER_OBJ_INVALID_ARGUMENTS);
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

  sprintf (serial_new_name, "%s.%s", downcase_owner_name, serial_name);

  serial_class_mop = sm_find_class (CT_SERIAL_NAME);
  if (serial_class_mop == NULL)
    {
      ERROR_SET_ERROR (error, ER_QPROC_DB_SERIAL_NOT_FOUND);
      goto end;
    }

  if (do_get_serial_obj_id (&serial_obj_id, serial_class_mop, serial_new_name) != NULL)
    {
      ERROR_SET_ERROR_1ARG (error, ER_QPROC_SERIAL_ALREADY_EXIST, serial_new_name);
      goto end;
    }

  obj_tmpl = dbt_edit_object (serial_mop);
  if (!obj_tmpl)
    {
      ASSERT_ERROR ();
      goto end;
    }

  /* unique_name */
  db_make_string (&value, serial_new_name);
  error = dbt_put_internal (obj_tmpl, SERIAL_ATTR_UNIQUE_NAME, &value);
  if (error != NO_ERROR)
    {
      ASSERT_ERROR ();
      is_abort = true;
      goto end;
    }

  /* owner */
  db_make_object (&value, owner_mop);
  error = dbt_put_internal (obj_tmpl, SERIAL_ATTR_OWNER, &value);
  if (error != NO_ERROR)
    {
      ASSERT_ERROR ();
      is_abort = true;
      goto end;
    }

  serial_obj = dbt_finish_object (obj_tmpl);
  if (!serial_obj)
    {
      ASSERT_ERROR_AND_SET (error);
      is_abort = true;
      goto end;
    }

end:
  if (is_abort && obj_tmpl)
    {
      dbt_abort_object (obj_tmpl);
    }

  AU_ENABLE (save);

  return error;
}

/*
 * au_change_trigger_owner - This changes the owning user of a trigger.
 *                           This should be called only by the DBA.
 *   return: error code
 *   trigger(in): trigger whose owner is to change
 *   owner(in):new owner
 */
int
au_change_trigger_owner (MOP trigger_mop, MOP owner_mop)
{
  DB_OBJECT *trigger_owner_obj = NULL;
  DB_OBJECT *target_class_obj = NULL;
  SM_CLASS *target_class = NULL;
  DB_VALUE value;
  const char *trigger_old_name = NULL;
  char trigger_new_name[DB_MAX_IDENTIFIER_LENGTH] = { '\0' };
  char *owner_name = NULL;
  char downcase_owner_name[DB_MAX_USER_LENGTH] = { '\0' };
  int save = 0;
  int error = NO_ERROR;

  if (!trigger_mop || !owner_mop)
    {
      ERROR_SET_WARNING (error, ER_OBJ_INVALID_ARGUMENTS);
      return error;
    }

  if (!au_is_dba_group_member (Au_user))
    {
      ERROR_SET_WARNING_1ARG (error, ER_AU_DBA_ONLY, "change_trigger_owner");
      return error;
    }

  AU_DISABLE (save);

  /*
   * class, serial, and trigger distinguish user schema by unique_name (user_specified_name).
   * so if the owner of class, serial, trigger changes, the unique_name must also change.
   */

  error = obj_get (trigger_mop, TR_ATT_UNIQUE_NAME, &value);
  if (error != NO_ERROR)
    {
      ASSERT_ERROR ();
      goto end;
    }

  if (!DB_IS_STRING (&value) || (trigger_old_name = db_get_string (&value)) == NULL)
    {
      ERROR_SET_WARNING_1ARG (error, ER_TR_TRIGGER_NOT_FOUND, "");
      goto end;
    }

  /* Check if the owner to be changed is the same. */
  error = obj_get (trigger_mop, TR_ATT_OWNER, &value);
  if (error != NO_ERROR)
    {
      ASSERT_ERROR ();
      goto end;
    }

  if (DB_VALUE_DOMAIN_TYPE (&value) != DB_TYPE_OBJECT || (trigger_owner_obj = db_get_object (&value)) == NULL)
    {
      /* Unable to get attribute value. */
      ERROR_SET_WARNING (error, ER_OBJ_INVALID_ARGUMENTS);
      goto end;
    }

  if (ws_is_same_object (trigger_owner_obj, owner_mop))
    {
      goto end;
    }

  /* TO BE: It is necessary to check the permission of the target class of the owner to be changed. */
#if 0
  error = obj_get (target_class_obj, TR_ATT_CLASS, &value);
  if (error != NO_ERROR)
    {
      ASSERT_ERROR ();
      goto end;
    }

  error = au_fetch_class (target_class_obj, &target_class, AU_FETCH_READ, AU_SELECT);
  if (error != NO_ERROR)
    {
      ASSERT_ERROR ();
      goto end;
    }
#endif

  owner_name = au_get_user_name (owner_mop);
  if (!owner_name)
    {
      ASSERT_ERROR_AND_SET (error);
      goto end;
    }
  sm_downcase_name (owner_name, downcase_owner_name, DB_MAX_USER_LENGTH);
  db_ws_free_and_init (owner_name);

  snprintf (trigger_new_name, SM_MAX_IDENTIFIER_LENGTH, "%s.%s", downcase_owner_name,
	    sm_remove_qualifier_name (trigger_old_name));

  error = tr_rename_trigger (trigger_mop, trigger_new_name, false, true);
  if (error != NO_ERROR)
    {
      ASSERT_ERROR ();
      goto end;
    }

  /* owner */
  db_make_object (&value, owner_mop);
  error = obj_set (trigger_mop, TR_ATT_OWNER, &value);
  if (error != NO_ERROR)
    {
      ASSERT_ERROR ();

      /* abort on error */
      if (tr_rename_trigger (trigger_mop, trigger_old_name, false, true) != NO_ERROR)
	{
	  assert (false);
	}
    }

end:
  AU_ENABLE (save);

  return error;
}

/*
 * au_get_class_owner - This access the user object that is the owner of
 *                      the class.
 *   return: user object (owner of class_)
 *   classmop(in): class object
 */
MOP
au_get_class_owner (MOP classmop)
{
  MOP owner = NULL;
  SM_CLASS *class_;

  /* should we disable authorization here ? */
  /*
   * should we allow the class owner to be known if the active user doesn't
   * have select authorization ?
   */

  if (au_fetch_class_force (classmop, &class_, AU_FETCH_READ) == NO_ERROR)
    {
      owner = class_->owner;
      if (owner == NULL)
	{
	  /* shouln't we try to update the class if the owner wasn't set ? */
	  owner = Au_dba_user;
	}
    }

  return (owner);
}

int
au_change_class_owner (MOP class_mop, MOP owner_mop)
{
  int error = NO_ERROR;
  int i;
  MOP *sub_partitions = NULL;
  int is_partition = DB_NOT_PARTITIONED_CLASS;
  bool has_savepoint = false;

  error = sm_partitioned_class_type (class_mop, &is_partition, NULL, &sub_partitions);
  if (error != NO_ERROR)
    {
      ASSERT_ERROR ();
      return error;
    }

  if (is_partition == DB_PARTITION_CLASS)	/* if partition; error */
    {
      ERROR_SET_ERROR_1ARG (error, ER_NOT_ALLOWED_ACCESS_TO_PARTITION, "");
      goto end;
    }

  if (is_partition == DB_PARTITIONED_CLASS)	/* if partitioned class; do actions to all partitions */
    {
      error = tran_system_savepoint (UNIQUE_PARTITION_SAVEPOINT_OWNER);
      if (error != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  goto end;
	}

      has_savepoint = true;

      for (i = 0; sub_partitions[i]; i++)
	{
	  error = au_change_owner (sub_partitions[i], owner_mop);
	  if (error != NO_ERROR)
	    {
	      ASSERT_ERROR ();
	      goto end;
	    }
	}
    }

  error = au_change_owner (class_mop, owner_mop);
  if (error != NO_ERROR)
    {
      ASSERT_ERROR ();
    }

end:
  if (sub_partitions)
    {
      free_and_init (sub_partitions);
    }

  if (has_savepoint && error != NO_ERROR && error != ER_LK_UNILATERALLY_ABORTED)
    {
      tran_abort_upto_system_savepoint (UNIQUE_PARTITION_SAVEPOINT_OWNER);
    }

  return error;
}

/*
 * au_change_sp_owner -
 *   return: error code
 *   sp(in):
 *   owner(in):
 */
int
au_change_sp_owner (MOP sp, MOP owner)
{
  int error = NO_ERROR;
  int save;
  DB_VALUE value;

  AU_DISABLE (save);
  if (!au_is_dba_group_member (Au_user))
    {
      error = ER_AU_DBA_ONLY;
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, error, 1, "change_sp_owner");
    }
  else
    {
      db_make_object (&value, owner);
      error = obj_set (sp, SP_ATTR_OWNER, &value);
      if (error < 0)
	{
	  goto end;
	}
    }

end:
  AU_ENABLE (save);
  return (error);
}

/*
 * LOGIN/LOGOUT
 */


/*
 * DEBUGGING FUNCTIONS
 */

/*
 * au_print_grant_entry() -
 *   return: none
 *   grants(in):
 *   grant_index(in):
 *   fp(in):
 */
static void
au_print_grant_entry (DB_SET * grants, int grant_index, FILE * fp)
{
  DB_VALUE value;

  set_get_element (grants, GRANT_ENTRY_CLASS (grant_index), &value);
  fprintf (fp, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_AUTHORIZATION, MSGCAT_AUTH_CLASS_NAME),
	   sm_get_ch_name (db_get_object (&value)));
  fprintf (fp, " ");

  set_get_element (grants, GRANT_ENTRY_SOURCE (grant_index), &value);
  obj_get (db_get_object (&value), "name", &value);

  fprintf (fp, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_AUTHORIZATION, MSGCAT_AUTH_FROM_USER),
	   db_get_string (&value));

  pr_clear_value (&value);

  set_get_element (grants, GRANT_ENTRY_CACHE (grant_index), &value);
  Au_cache.print_cache (db_get_int (&value), fp);
}

/*
 * au_print_auth() -
 *   return: none
 *   auth(in):
 *   fp(in):
 */
static void
au_print_auth (MOP auth, FILE * fp)
{
  DB_VALUE value;
  DB_SET *grants;
  int i, gsize;
  int error;

  /* kludge, some older databases used the name "user", rather than "owner" */
  error = obj_get (auth, "owner", &value);
  if (error != NO_ERROR)
    {
      error = obj_get (auth, "user", &value);
      if (error != NO_ERROR)
	return;			/* punt */
    }

  if (db_get_object (&value) != NULL)
    {
      obj_get (db_get_object (&value), "name", &value);
      fprintf (fp, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_AUTHORIZATION, MSGCAT_AUTH_USER_TITLE),
	       db_get_string (&value));
      pr_clear_value (&value);
    }
  else
    {
      fprintf (fp, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_AUTHORIZATION, MSGCAT_AUTH_UNDEFINED_USER));
    }

  get_grants (auth, &grants, 1);
  if (grants != NULL)
    {
      gsize = set_size (grants);
      for (i = 0; i < gsize; i += GRANT_ENTRY_LENGTH)
	{
	  au_print_grant_entry (grants, i, fp);
	}
      set_free (grants);
    }
}

/*
 * au_dump_auth() - Prints authorization info for all users.
 *   return: none
 *   fp(in): output file
 *
 * Note: The db_root class used to have a user attribute which was a set
 *       containing the object-id for all users.  The users attribute has been
 *       eliminated for performance reasons.  A query on the db_user class is
 *       new used to find all users.
 */
void
au_dump_auth (FILE * fp)
{
  MOP user, auth;
  char *query;
  DB_QUERY_RESULT *query_result;
  DB_QUERY_ERROR query_error;
  int error;
  DB_VALUE user_val;
  const char *qp1 = "select [%s] from [%s];";

  query = (char *) malloc (strlen (qp1) + strlen (AU_USER_CLASS_NAME) * 2);

  if (query)
    {
      sprintf (query, qp1, AU_USER_CLASS_NAME, AU_USER_CLASS_NAME);

      error = db_compile_and_execute_local (query, &query_result, &query_error);
      /* error is row count if not negative. */
      if (error > 0)
	{
	  while (db_query_next_tuple (query_result) == DB_CURSOR_SUCCESS)
	    {
	      if (db_query_get_tuple_value (query_result, 0, &user_val) == NO_ERROR)
		{
		  user = db_get_object (&user_val);
		  if (au_get_object (user, "authorization", &auth) == NO_ERROR)
		    {
		      au_print_auth (auth, fp);
		    }
		}
	    }
	}
      if (error >= 0)
	{
	  db_query_end (query_result);
	}
      free_and_init (query);
    }
}

/*
 * au_dump_user() - Dumps authorization information for a
 *                  particular user to a file.
 *   return: none
 *   user(in): user object
 *   fp(in): file pointer
 */
void
au_dump_user (MOP user, FILE * fp)
{
  DB_VALUE value;
  DB_SET *groups = NULL;
  MOP auth;
  int i, card;

  if (obj_get (user, "name", &value) == NO_ERROR)
    {
      fprintf (fp, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_AUTHORIZATION, MSGCAT_AUTH_USER_NAME),
	       db_get_string (&value));
      pr_clear_value (&value);
    }

  groups = NULL;
  if (au_get_set (user, "direct_groups", &groups) == NO_ERROR)
    {
      fprintf (fp, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_AUTHORIZATION, MSGCAT_AUTH_USER_DIRECT_GROUPS));
      card = set_cardinality (groups);
      for (i = 0; i < card; i++)
	{
	  if (set_get_element (groups, i, &value) == NO_ERROR)
	    {
	      if (obj_get (db_get_object (&value), "name", &value) == NO_ERROR)
		{
		  fprintf (fp, "%s ", db_get_string (&value));
		  pr_clear_value (&value);
		}
	    }
	}
      fprintf (fp, "\n");
      set_free (groups);
    }

  groups = NULL;
  if (au_get_set (user, "groups", &groups) == NO_ERROR)
    {
      fprintf (fp, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_AUTHORIZATION, MSGCAT_AUTH_USER_GROUPS));
      card = set_cardinality (groups);
      for (i = 0; i < card; i++)
	{
	  if (set_get_element (groups, i, &value) == NO_ERROR)
	    {
	      if (obj_get (db_get_object (&value), "name", &value) == NO_ERROR)
		{
		  fprintf (fp, "%s ", db_get_string (&value));
		  pr_clear_value (&value);
		}
	    }
	}
      fprintf (fp, "\n");
      set_free (groups);
    }

  /* dump local grants */
  if (au_get_object (user, "authorization", &auth) == NO_ERROR)
    {
      au_print_auth (auth, fp);
    }

  /*
   * need to do a walk back through the group hierarchy and collect all
   * inherited grants and their origins
   */
}

/*
 * au_dump_to_file() - Dump all authorization information including
 *                     user/group hierarchy.
 *   return: none
 *   fp(in): file pointer
 *
 * Note: The db_root class used to have a user attribute which was a set
 *       containing the object-id for all users.  The users attribute has been
 *       eliminated for performance reasons.  A query on the db_user class is
 *       new used to find all users.
 */
void
au_dump_to_file (FILE * fp)
{
  MOP user;
  DB_VALUE value;
  char *query = NULL;
  DB_QUERY_RESULT *query_result = NULL;
  DB_QUERY_ERROR query_error;
  int error = NO_ERROR;
  DB_VALUE user_val;
  const char *qp1 = "select [%s] from [%s];";

  /* NOTE: We should be getting the real user name here ! */

  fprintf (fp, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_AUTHORIZATION, MSGCAT_AUTH_CURRENT_USER),
	   Au_user_name);

  query = (char *) malloc (strlen (qp1) + strlen (AU_USER_CLASS_NAME) * 2);

  if (query)
    {
      sprintf (query, qp1, AU_USER_CLASS_NAME, AU_USER_CLASS_NAME);

      error = db_compile_and_execute_local (query, &query_result, &query_error);
      /* error is row count if not negative. */
      if (error > 0)
	{
	  fprintf (fp, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_AUTHORIZATION, MSGCAT_AUTH_ROOT_USERS));
	  while (db_query_next_tuple (query_result) == DB_CURSOR_SUCCESS)
	    {
	      if (db_query_get_tuple_value (query_result, 0, &user_val) == NO_ERROR)
		{
		  user = db_get_object (&user_val);
		  if (obj_get (user, "name", &value) == NO_ERROR)
		    {
		      fprintf (fp, "%s ", db_get_string (&value));
		      pr_clear_value (&value);
		    }
		}
	    }
	  fprintf (fp, "\n");

	  if (db_query_first_tuple (query_result) == DB_CURSOR_SUCCESS)
	    {
	      do
		{
		  if (db_query_get_tuple_value (query_result, 0, &user_val) == NO_ERROR)
		    {
		      user = db_get_object (&user_val);
		      au_dump_user (user, fp);
		    }
		}
	      while (db_query_next_tuple (query_result) == DB_CURSOR_SUCCESS);
	    }
	  fprintf (fp, "\n");
	}
    }
  if (error >= 0 && query)
    {
      db_query_end (query_result);
    }
  if (query)
    {
      free_and_init (query);
    }

  fprintf (fp, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_AUTHORIZATION, MSGCAT_AUTH_AUTH_TITLE));
  au_dump_auth (fp);
}

/*
 * au_dump() -
 *   return: none
 */
void
au_dump (void)
{
  au_dump_to_file (stdout);
}

/*
 * AUTHORIZATION CLASSES
 */


/*
 * RESTART/SHUTDOWN
 */

/*
 * au_get_class_privilege() -
 *   return: error code
 *   mop(in):
 *   auth(in):
 */
int
au_get_class_privilege (DB_OBJECT * mop, unsigned int *auth)
{
  SM_CLASS *class_;
  unsigned int *bits = NULL;
  int error = NO_ERROR;

  if (!Au_disable)
    {
      if (mop == NULL)
	{
	  return ER_FAILED;
	}

      class_ = (SM_CLASS *) mop->object;

      bits = Au_cache.get_cache_bits (class_);
      if (bits == NULL)
	{
	  return er_errid ();
	}

      if (*bits == AU_CACHE_INVALID)
	{
	  error = Au_cache.update (mop, class_);
	  if (error == NO_ERROR)
	    {
	      bits = Au_cache.get_cache_bits (class_);
	    }
	}
      *auth = *bits;
    }

  return error;
}

static int
au_check_owner (DB_VALUE * creator_val)
{
  MOP creator;
  DB_SET *groups;
  int ret_val = ER_FAILED;

  creator = db_get_object (creator_val);

  if (ws_is_same_object (creator, Au_user) || au_is_dba_group_member (Au_user))
    {
      ret_val = NO_ERROR;
    }
  else if (au_get_set (Au_user, "groups", &groups) == NO_ERROR)
    {
      if (set_ismember (groups, creator_val))
	{
	  ret_val = NO_ERROR;
	}
      set_free (groups);
    }

  return ret_val;
}

/*
 * au_check_serial_authorization - check whether the current user is able to
 *                                 modify serial object or not
 *   return: NO_ERROR if available, otherwise error code
 *   serial_object(in): serial object pointer
 */
int
au_check_serial_authorization (MOP serial_object)
{
  DB_VALUE creator_val;
  int ret_val;

  ret_val = db_get (serial_object, "owner", &creator_val);
  if (ret_val != NO_ERROR)
    {
      return ret_val;
    }

  assert (!DB_IS_NULL (&creator_val));

  ret_val = au_check_owner (&creator_val);
  if (ret_val != NO_ERROR)
    {
      ret_val = ER_QPROC_CANNOT_UPDATE_SERIAL;
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ret_val, 0);
    }

  pr_clear_value (&creator_val);
  return ret_val;
}

int
au_check_server_authorization (MOP server_object)
{
  DB_VALUE creator_val;
  int ret_val;

  ret_val = db_get (server_object, "owner", &creator_val);
  if (ret_val != NO_ERROR || DB_IS_NULL (&creator_val))
    {
      return ret_val;
    }

  ret_val = au_check_owner (&creator_val);
  if (ret_val != NO_ERROR)
    {
      ret_val = ER_DBLINK_CANNOT_UPDATE_SERVER;
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ret_val, 0);
    }

  pr_clear_value (&creator_val);
  return ret_val;
}

bool
au_is_server_authorized_user (DB_VALUE * owner_val)
{
  return (au_check_owner (owner_val) == NO_ERROR);
}

/*
 * au_delete_auth_of_dropping_user - delete _db_auth records refers to the given grantee user.
 *   return: error code
 *   user(in): the grantee user name to be dropped
 */
static int
au_delete_auth_of_dropping_user (MOP user)
{
  int error = NO_ERROR, save;
  const char *sql_query = "DELETE FROM [" CT_CLASSAUTH_NAME "] [au] WHERE [au].[grantee] = ?;";
  DB_VALUE val;
  DB_QUERY_RESULT *result = NULL;
  DB_SESSION *session = NULL;
  int stmt_id;

  db_make_null (&val);

  /* Disable the checking for internal authorization object access */
  AU_DISABLE (save);

  assert (user != NULL);

  session = db_open_buffer_local (sql_query);
  if (session == NULL)
    {
      ASSERT_ERROR_AND_SET (error);
      goto exit;
    }

  error = db_set_system_generated_statement (session);
  if (error != NO_ERROR)
    {
      goto release;
    }

  stmt_id = db_compile_statement_local (session);
  if (stmt_id < 0)
    {
      ASSERT_ERROR_AND_SET (error);
      goto release;
    }

  db_make_object (&val, user);
  error = db_push_values (session, 1, &val);
  if (error != NO_ERROR)
    {
      goto release;
    }

  error = db_execute_statement_local (session, stmt_id, &result);
  if (error < 0)
    {
      goto release;
    }

  error = db_query_end (result);

release:
  if (session != NULL)
    {
      db_close_session (session);
    }

exit:
  pr_clear_value (&val);

  AU_ENABLE (save);

  return error;
}
