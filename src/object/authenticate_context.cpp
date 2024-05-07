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
 * authenticate_context.cpp -
 */

#include "authenticate_context.hpp"

#include "authenticate.h"
#include "authenticate_cache.hpp" /* init_caches */
#include "boot_cl.h" /* BOOT_IS_CLIENT_RESTARTED () */

#include "db.h"
#include "dbi.h"
#include "dbtype.h"
#include "error_manager.h"
#include "object_accessor.h" /* obj_ */
#include "object_primitive.h"
#include "schema_manager.h" /* sm_find_class() */
#include "schema_system_catalog_constants.h"
#include "locator_cl.h" /* locator_create_heap_if_needed () */

/*
 * Authorization Class Names
 */
#define AU_ROOT_CLASS_NAME      CT_ROOT_NAME
#define AU_OLD_ROOT_CLASS_NAME  CT_AUTHORIZATIONS_NAME
#define AU_USER_CLASS_NAME      CT_USER_NAME
#define AU_PASSWORD_CLASS_NAME  CT_PASSWORD_NAME
#define AU_AUTH_CLASS_NAME      CT_AUTHORIZATION_NAME

#define AU_PUBLIC_USER_NAME     "PUBLIC"
#define AU_DBA_USER_NAME        "DBA"

// static functions
static int au_add_method_check_authorization (void);

void
authenticate_context::reset (void)
{
  root = nullptr;
  authorizations_class = nullptr;
  authorization_class = nullptr;
  user_class = nullptr;
  password_class = nullptr;
  current_user = nullptr;
  public_user = nullptr;
  dba_user = nullptr;
  disable_auth_check = true;
}

authenticate_context::authenticate_context (void) // au_init ()
  : caches {}
{
  reset ();
  is_started = false;
}

authenticate_context::~authenticate_context ()
{
  reset ();

  /*
   * could remove the static links here but it isn't necessary and
   * we may need them again the next time we restart
   */
  caches.flush ();
}

/*
 * start - This is called during the bo_resteart initialization sequence
 *            after the database has been successfully opened
 *   return: error code
 *
 * Note: Here we initialize the authorization system by finding the system
 *       objects and validating the registered user.
 */
int
authenticate_context::start (void)
{
  int error = NO_ERROR;
  if (is_started == true)
    {
      return error;
    }

  MOPLIST mops;
  MOP class_mop;

  /*
   * NEED TO MAKE SURE THIS IS 1 IF THE SERVER CRASHED BECAUSE WE'RE
   * GOING TO CALL db_ FUNCTIONS
   */
  db_Connect_status = DB_CONNECTION_STATUS_CONNECTED;

  /*
   * It is important not to enable authorization until after the
   * login is finished, otherwise the system will stop when it tries
   * to validate the user when accessing the authorization objects.
   */
  disable_auth_check = true;

  /* locate the various system classes */
  class_mop = sm_find_class (AU_ROOT_CLASS_NAME);
  if (class_mop == NULL)
    {
      error = ER_AU_NO_AUTHORIZATION;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
      return (error);
    }
  authorizations_class = class_mop;

  class_mop = sm_find_class (AU_AUTH_CLASS_NAME);
  if (class_mop == NULL)
    {
      error = ER_AU_NO_AUTHORIZATION;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
      return (error);
    }
  authorization_class = class_mop;

  class_mop = sm_find_class (AU_USER_CLASS_NAME);
  if (class_mop == NULL)
    {
      error = ER_AU_NO_AUTHORIZATION;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
      return (error);
    }
  user_class = class_mop;

  class_mop = sm_find_class (AU_PASSWORD_CLASS_NAME);
  if (class_mop == NULL)
    {
      error = ER_AU_NO_AUTHORIZATION;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
      return (error);
    }
  password_class = class_mop;

  mops = db_get_all_objects (authorizations_class);
  if (mops == NULL)
    {
      error = ER_AU_NO_AUTHORIZATION;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
    }
  else
    {
      /* this shouldn't happen, not sure what to do */
      if (mops->next != NULL)
	{
	  error = ER_AU_MULTIPLE_ROOTS;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
	}

      root = mops->op;
      db_objlist_free (mops);

      public_user = au_find_user (AU_PUBLIC_USER_NAME);
      dba_user = au_find_user (AU_DBA_USER_NAME);
      if (public_user == NULL || dba_user == NULL)
	{
	  error = er_errid ();
	  if (error != ER_LK_UNILATERALLY_ABORTED)
	    {
	      error = ER_AU_INCOMPLETE_AUTH;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
	    }
	}
      else
	{
	  /*
	   * If you try to start the authorization system and
	   * there is no user logged in, you will automatically be logged in
	   * as "PUBLIC".  Optionally, we could get a name from the
	   * cubrid.conf file or use the name of the current Unix user.
	   */
	  if (strlen (user_name) == 0)
	    {
	      strcpy (user_name, "PUBLIC");
	    }

	  error = perform_login (user_name, user_password_sha2_512, false);
	}
    }

  /* make sure this is off */
  disable_auth_check = false;

  if (error == NO_ERROR)
    {
      is_started = true;
    }

  return (error);
}

/*
* login - Registers a user name and password for a database.
*   return: error code
*   name(in): user name
*   password(in): password
*   ignore_dba_privilege(in) : whether ignore DBA's privilege or not in login
*
* Note: If a database has already been restarted, the user will be validated
*       immediately, otherwise the name and password are simply saved
*       in global variables and the validation will ocurr the next time
*       bo_restart is called.
*/

int
authenticate_context::login (const char *name, const char *password, bool ignore_dba_privilege)
{
  int error = NO_ERROR;
  int save;

  /*
   * because the database can be left open after authorization failure,
   * checking Au_root for NULL isn't a reliable way to see of the database
   * is in an "un-restarted" state.  Instead, look at BOOT_IS_CLIENT_RESTARTED
   * which is defined to return non-zero if a valid transaction is in
   * progress.
   */
  if (root == NULL || !BOOT_IS_CLIENT_RESTARTED ())
    {
      /*
       * Save the name & password for later.  Allow the name to be NULL
       * and leave it unmodified.
       */
      if (name != NULL)
	{
	  if (strlen (name) >= DB_MAX_USER_LENGTH)
	    {
	      error = ER_USER_NAME_TOO_LONG;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
	      return error;
	    }
	  strcpy (user_name, name);
	}

      if (password == NULL || strlen (password) == 0)
	{
	  strcpy (user_password_des_oldstyle, "");
	  strcpy (user_password_sha1, "");
	  strcpy (user_password_sha2_512, "");
	}
      else
	{
	  /* store the password encrypted(DES and SHA1 both) so we don't have buffers lying around with the obvious
	   * passwords in it. */
	  encrypt_password (password, 1, user_password_des_oldstyle);
	  encrypt_password_sha1 (password, 1, user_password_sha1);
	  encrypt_password_sha2_512 (password, user_password_sha2_512);
	}
    }
  else
    {
      /* Change users within an active database. */
      AU_DISABLE (save);
      error = perform_login (name, password, ignore_dba_privilege);
      AU_ENABLE (save);
    }
  return (error);
}

/*
 * au_install() - This is used to initialize the authorization system in a
 *                freshly created database.
 *                It should only be called within the createdb tool.
 *   return: error code
 */
int
authenticate_context::install (void)
{
  MOP root_cls = NULL, user_cls = NULL, pass_cls = NULL, auth_cls = NULL, old_cls = NULL;
  SM_TEMPLATE *def;
  int exists, save, index;

  AU_DISABLE (save);

  /*
   * create the system authorization objects, add attributes later since they
   * have domain dependencies
   */
  root_cls = db_create_class (AU_ROOT_CLASS_NAME);
  user_cls = db_create_class (AU_USER_CLASS_NAME);
  pass_cls = db_create_class (AU_PASSWORD_CLASS_NAME);
  auth_cls = db_create_class (AU_AUTH_CLASS_NAME);
  old_cls = db_create_class (AU_OLD_ROOT_CLASS_NAME);

  if (root_cls == NULL || user_cls == NULL || pass_cls == NULL || auth_cls == NULL || old_cls == NULL)
    {
      goto exit_on_error;
    }

  sm_mark_system_class (root_cls, 1);
  sm_mark_system_class (user_cls, 1);
  sm_mark_system_class (pass_cls, 1);
  sm_mark_system_class (auth_cls, 1);
  sm_mark_system_class (old_cls, 1);

  /*
   * db_root
   */

  /*
   * Authorization root, might not need this if we restrict the generation of
   * user and  group objects but could be useful in other ways.
   */
  def = smt_edit_class_mop (root_cls, AU_ALTER);
  if (def == NULL)
    {
      goto exit_on_error;
    }
  smt_add_attribute (def, "triggers", "sequence of (string, object)", (DB_DOMAIN *) 0);
  smt_add_attribute (def, "charset", "integer", NULL);
  smt_add_attribute (def, "lang", "string", NULL);
  smt_add_attribute (def, "timezone_checksum", "string", NULL);


  /* need signatures for these ! */
  smt_add_class_method (def, "add_user", "au_add_user_method");
  smt_add_class_method (def, "drop_user", "au_drop_user_method");

  smt_add_class_method (def, "find_user", "au_find_user_method");
  smt_assign_argument_domain (def, "find_user", true, NULL, 0, "string", (DB_DOMAIN *) 0);

  smt_add_class_method (def, "print_authorizations", "au_describe_root_method");
  smt_add_class_method (def, "info", "au_info_method");
  smt_add_class_method (def, "change_owner", "au_change_owner_method");
  smt_add_class_method (def, "change_trigger_owner", "au_change_trigger_owner_method");
  smt_add_class_method (def, "get_owner", "au_get_owner_method");
  smt_add_class_method (def, "change_sp_owner", "au_change_sp_owner_method");

  if (sm_update_class (def, NULL) != NO_ERROR || locator_create_heap_if_needed (root_cls, false) == NULL)
    {
      goto exit_on_error;
    }

  /*
   * db_authorizations
   */

  /*
   * temporary support for the old name, need to migrate
   * users over to db_root
   */
  def = smt_edit_class_mop (old_cls, AU_ALTER);
  if (def == NULL)
    {
      goto exit_on_error;
    }
  smt_add_class_method (def, "add_user", "au_add_user_method");
  smt_add_class_method (def, "drop_user", "au_drop_user_method");

  smt_add_class_method (def, "find_user", "au_find_user_method");
  smt_assign_argument_domain (def, "find_user", true, NULL, 0, "string", (DB_DOMAIN *) 0);

  smt_add_class_method (def, "print_authorizations", "au_describe_root_method");
  smt_add_class_method (def, "info", "au_info_method");
  smt_add_class_method (def, "change_owner", "au_change_owner_method");
  smt_add_class_method (def, "change_trigger_owner", "au_change_trigger_owner_method");
  smt_add_class_method (def, "get_owner", "au_get_owner_method");

  if (sm_update_class (def, NULL) != NO_ERROR || locator_create_heap_if_needed (old_cls, false) == NULL)
    {
      goto exit_on_error;
    }

  /*
   * db_user
   */

  def = smt_edit_class_mop (user_cls, AU_ALTER);
  if (def == NULL)
    {
      goto exit_on_error;
    }
  /* If the attribute configuration is changed, the CATCLS_USER_ATTR_IDX_NAME also be changed.
   *   - CATCLS_USER_ATTR_IDX_NAME is defined in the cubload::server_class_installer::locate_class () function.
   */
  smt_add_attribute (def, "name", "string", (DB_DOMAIN *) 0);
  smt_add_attribute (def, "id", "integer", (DB_DOMAIN *) 0);
  smt_add_attribute (def, "password", AU_PASSWORD_CLASS_NAME, (DB_DOMAIN *) 0);
  smt_add_attribute (def, "direct_groups", "set of (db_user)", (DB_DOMAIN *) 0);
  smt_add_attribute (def, "groups", "set of (db_user)", (DB_DOMAIN *) 0);
  smt_add_attribute (def, "authorization", AU_AUTH_CLASS_NAME, (DB_DOMAIN *) 0);
  smt_add_attribute (def, "triggers", "sequence of object", (DB_DOMAIN *) 0);
  smt_add_attribute (def, "comment", "varchar(1024)", NULL);
  /* need signatures for these */
  smt_add_method (def, "set_password", "au_set_password_method");
  smt_add_method (def, "set_password_encoded", "au_set_password_encoded_method");
  smt_add_method (def, "set_password_encoded_sha1", "au_set_password_encoded_sha1_method");
  smt_add_method (def, "add_member", "au_add_member_method");
  smt_add_method (def, "drop_member", "au_drop_member_method");
  smt_add_method (def, "print_authorizations", "au_describe_user_method");
  smt_add_class_method (def, "add_user", "au_add_user_method");
  smt_add_class_method (def, "drop_user", "au_drop_user_method");

  smt_add_class_method (def, "find_user", "au_find_user_method");
  smt_assign_argument_domain (def, "find_user", true, NULL, 0, "string", (DB_DOMAIN *) 0);
  smt_add_class_method (def, "login", "au_login_method");

  if (sm_update_class (def, NULL) != NO_ERROR || locator_create_heap_if_needed (user_cls, false) == NULL)
    {
      goto exit_on_error;
    }

  /* Add Unique Index */
  {
    const char *names[] = { "name", NULL };

    if (db_add_constraint (user_cls, DB_CONSTRAINT_UNIQUE, NULL, names, 0) != NO_ERROR)
      {
	goto exit_on_error;
      }
  }

  /*
   * db_password
   */

  def = smt_edit_class_mop (pass_cls, AU_ALTER);
  if (def == NULL)
    {
      goto exit_on_error;
    }
  smt_add_attribute (def, "password", "string", (DB_DOMAIN *) 0);

  if (sm_update_class (def, NULL) != NO_ERROR || locator_create_heap_if_needed (pass_cls, false) == NULL)
    {
      goto exit_on_error;
    }

  /*
   * db_authorization
   */

  /*
   * Authorization object, the grant set could go directly in the user object
   * but it might be better to keep it separate in order to use the special
   * read-once lock for the authorization object only.
   */

  def = smt_edit_class_mop (auth_cls, AU_ALTER);
  if (def == NULL)
    {
      goto exit_on_error;
    }
  smt_add_attribute (def, "owner", AU_USER_CLASS_NAME, (DB_DOMAIN *) 0);
  smt_add_attribute (def, "grants", "sequence", (DB_DOMAIN *) 0);

  if (sm_update_class (def, NULL) != NO_ERROR || locator_create_heap_if_needed (auth_cls, false) == NULL)
    {
      goto exit_on_error;
    }


  /* Create the single authorization root object */
  root = obj_create (root_cls);
  if (root == NULL)
    {
      goto exit_on_error;
    }

  /* create the DBA user and assign ownership of the system classes */
  dba_user = au_add_user ("DBA", &exists);
  if (dba_user == NULL)
    {
      goto exit_on_error;
    }

  /* establish the DBA as the current user */
  if (caches.find_user_cache_index (dba_user, &index, 0) != NO_ERROR)
    {
      goto exit_on_error;
    }
  current_user = dba_user;
  Au_cache.set_cache_index (index);

  set_user (dba_user);

  au_change_owner (root_cls, current_user);
  au_change_owner (old_cls, current_user);
  au_change_owner (user_cls, current_user);
  au_change_owner (pass_cls, current_user);
  au_change_owner (auth_cls, current_user);

  /* create the PUBLIC user */
  public_user = au_add_user ("PUBLIC", &exists);
  if (public_user == NULL)
    {
      goto exit_on_error;
    }

  /*
   * grant browser access to the authorization objects
   * note that the password class cannot be read by anyone except the DBA
   */
  au_grant (public_user, root_cls, (DB_AUTH) (AU_SELECT | AU_EXECUTE), false);
  au_grant (public_user, old_cls, (DB_AUTH) (AU_SELECT | AU_EXECUTE), false);
  au_grant (public_user, user_cls, AU_SELECT, false);
  au_grant (public_user, user_cls, (DB_AUTH) (AU_SELECT | AU_EXECUTE), false);
  au_grant (public_user, auth_cls, AU_SELECT, false);

  au_add_method_check_authorization ();

  AU_ENABLE (save);

  this->authorizations_class = root_cls;
  this->authorization_class = auth_cls;
  this->user_class = user_cls;
  this->password_class = pass_cls;

  return NO_ERROR;

exit_on_error:
  if (public_user != NULL)
    {
      au_drop_user (public_user);
      public_user = NULL;
    }
  if (dba_user != NULL)
    {
      au_drop_user (dba_user);
      dba_user = NULL;
    }
  if (root != NULL)
    {
      obj_delete (root);
      root = NULL;
    }
  if (old_cls != NULL)
    {
      db_drop_class (old_cls);
    }
  if (auth_cls != NULL)
    {
      db_drop_class (auth_cls);
    }
  if (pass_cls != NULL)
    {
      db_drop_class (pass_cls);
    }
  if (user_cls != NULL)
    {
      db_drop_class (user_cls);
    }
  if (root_cls != NULL)
    {
      db_drop_class (root_cls);
    }

  AU_ENABLE (save);
  return (er_errid () == NO_ERROR ? ER_FAILED : er_errid ());
}

/*
 * perform_login - This changes the current user using the supplied
 *                    name & password.
 *   return: error code
 *   name(in): user name
 *   password(in): user password
 *
 * Note: It is called both by login() and start().
 *       Once the name/password have been validated, it calls au_set_user()
 *       to set the user object and calculate the authorization cache index.
 *       Assumes authorization has been disabled.
 */
int
authenticate_context::perform_login (const char *name, const char *password, bool ignore_dba_privilege)
{
  int error = NO_ERROR;
  MOP user;
  DB_VALUE value;
  const char *pass;
  char *dbuser = NULL, *dbpassword = NULL;

  dbuser = (char *) name;
  dbpassword = (char *) password;

  if (dbuser == NULL || strlen (dbuser) == 0)
    {
      error = ER_AU_NO_USER_LOGGED_IN;
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, error, 0);
    }
  else
    {
      public_user = au_find_user (AU_PUBLIC_USER_NAME);
      dba_user = au_find_user (AU_DBA_USER_NAME);
      if (public_user == NULL || dba_user == NULL)
	{
	  error = er_errid ();
	  if (error != ER_LK_UNILATERALLY_ABORTED)
	    {
	      error = ER_AU_INCOMPLETE_AUTH;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
	    }
	}
      user = au_find_user (dbuser);
      if (user == NULL)
	{
	  error = er_errid ();
	  if (error != ER_LK_UNILATERALLY_ABORTED)
	    {
	      error = ER_AU_INVALID_USER;
	      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, error, 1, dbuser);
	    }
	}
      else
	{
	  if (obj_get (user, "password", &value) != NO_ERROR)
	    {
	      error = ER_AU_CORRUPTED;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
	    }
	  else
	    {
	      /*
	       * hack, allow password checking to be turned off in certain
	       * cases, like the utility programs that will always run in DBA
	       * mode but don't want to have to enter a password, also
	       * if a sucessful DBA login ocurred, allow users to be
	       * changed without entering passwords
	       */

	      if (!ignore_passwords && (!au_is_dba_group_member (current_user) || ignore_dba_privilege))
		{
		  pass = NULL;
		  if (!DB_IS_NULL (&value) && db_get_object (&value) != NULL)
		    {
		      if (obj_get (db_get_object (&value), "password", &value))
			{
			  assert (er_errid () != NO_ERROR);
			  return er_errid ();
			}
		      if (DB_IS_STRING (&value))
			{
			  if (DB_IS_NULL (&value))
			    {
			      pass = NULL;
			    }
			  else
			    {
			      pass = db_get_string (&value);
			    }
			}
		    }

		  if (pass != NULL && strlen (pass))
		    {
		      /* the password is present and must match */
		      if ((dbpassword == NULL) || (strlen (dbpassword) == 0)
			  || !match_password (dbpassword, db_get_string (&value)))
			{
			  error = ER_AU_INVALID_PASSWORD;
			  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
			}
		    }
		  else
		    {
		      /*
		       * the password in the user object is effectively NULL,
		       * only accept the login if the user supplied an empty
		       * password.
		       * Formerly any password string was accepted
		       * if the stored password was NULL which is
		       * not quite right.
		       */
		      if (dbpassword != NULL && strlen (dbpassword))
			{
			  error = ER_AU_INVALID_PASSWORD;
			  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
			}
		    }
		  if (pass != NULL)
		    {
		      ws_free_string (db_get_string (&value));
		    }
		}

	      if (error == NO_ERROR)
		{
		  error = AU_SET_USER (user);

		  /* necessary to invalidate vclass cache */
		  sm_bump_local_schema_version ();
		}
	    }
	}
    }
  return (error);
}

int
authenticate_context::set_user (MOP newuser)
{
  int error = NO_ERROR;
  int index;

  if (newuser != NULL && !ws_is_same_object (newuser, current_user))
    {
      if (! (error = caches.find_user_cache_index (newuser, &index, 1)))
	{

	  current_user = newuser;
	  caches.set_cache_index (index);

	  /*
	   * it is important that we don't call sm_bump_local_schema_version() here
	   * because this function is called during the compilation of vclasses
	   */

	  /*
	   * Entry-level SQL specifies that the schema name is the same as
	   * the current user authorization name.  In any case, this is
	   * the place to set the current schema since the user just changed.
	   */
	  error = sc_set_current_schema (current_user);
	}
    }
  return (error);
}


/*
 * au_set_password_encrypt -  Set the password string for a user.
 *   return: error code
 *   user(in): user object
 *   password(in): new password
 */
int
authenticate_context::set_password (MOP user, const char *password, int encode = 1,
				    char encrypt_prefix = ENCODE_PREFIX_SHA2_512)
{
  return (au_set_password_internal (user, password, encode, encrypt_prefix));
}

/*
 * au_set_password_encrypt -  Set the password string for a user.
 *   return: error code
 *   user(in): user object
 *   password(in): new password
 */
int
authenticate_context::set_password (MOP user, const char *password)
{
  return (au_set_password_internal (user, password, 1, ENCODE_PREFIX_SHA2_512));
}

/*
 * au_disable_passwords -
 *    return: none
 */
void
authenticate_context::disable_passwords (void)
{
  ignore_passwords = 1;
}

/*
 * au_check_user - This is used to check for a currently valid user for some
 *                 operations that are not strictly based on
 *                 any particular class.
 *    return: error code
 */
int
authenticate_context::check_user (void)
{
  int error = NO_ERROR;

  if (current_user == NULL)
    {
      error = ER_AU_INVALID_USER;
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, error, 1, "");
    }

  return (error);
}


bool
authenticate_context::has_user_name (void)
{
  return current_user != NULL || strlen (user_name) > 0;
}

/*
 * au_public_user
 *   return: Au_public_user
 */
MOP
authenticate_context::get_public_user (void)
{
  return public_user;
}

/*
 * au_public_user
 *   return: Au_public_user
 */
MOP
authenticate_context::get_dba_user (void)
{
  return dba_user;
}

/*
 * au_get_current_user_name - Returns the name of the current user, the string must be
 *                freed with ws_free_string (db_string_free).
 *   return: user name (NULL if error)
 *
 * Note: Note that this is what should always be used to get the active user.
 *       Au_user_name is only used as a temporary storage area during login.
 *       Once the database is open, the active user is determined by Au_user
 *       and Au_user_name doesn't necessarily track this.
 */
const char *
authenticate_context::get_current_user_name (void)
{
  DB_VALUE value;
  const char *name = NULL;

  if (current_user == NULL)
    {
      /*
       * Database hasn't been started yet, return the registered name
       * if any.
       */
      if (strlen (user_name) == 0)
	{
	  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_AU_NO_USER_LOGGED_IN, 0);
	}
      else
	{
	  name = ws_copy_string (user_name);
	  /*
	   * When this function is called before the workspace memory
	   * manager was not initialized in the case of client
	   * initialization(db_restart()), ws_copy_string() will return
	   * NULL.
	   */
	}
    }
  else
    {
      int save;

      /*
       * To reduce unnecessary code execution,
       * the current schema name can be used instead of the current user name.
       *
       * Au_user_name cannot be used because it does not always store the current user name.
       * When au_login_method () is called, Au_user_name is not changed.
       */
      const char *sc_name = sc_current_schema_name ();
      char upper_sc_name[DB_MAX_USER_LENGTH];
      if (sc_name && sc_name[0] != '\0')
	{
	  intl_identifier_upper (sc_name, upper_sc_name);
	  return ws_copy_string (upper_sc_name);
	}

      AU_DISABLE (save);

      if (obj_get (current_user, "name", &value) == NO_ERROR)
	{
	  if (!DB_IS_STRING (&value))
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_AU_CORRUPTED, 0);
	      pr_clear_value (&value);
	    }
	  else if (DB_IS_NULL (&value))
	    {
	      name = NULL;
	    }
	  else
	    {
	      name = db_get_string (&value);
	      name = ws_copy_string (name);
	      pr_clear_value (&value);
	    }
	}

      AU_ENABLE (save);
    }

  return name;
}

const char *
authenticate_context::get_public_user_name (void)
{
  return AU_PUBLIC_USER_NAME;
}

const char *
authenticate_context::get_user_class_name (void)
{
  return AU_USER_CLASS_NAME;
}

//
// STATIC FUNCTIONS
//

/*
 * au_add_method_check_authorization -
 *    return: error code
 */
static int
au_add_method_check_authorization (void)
{
  MOP auth;
  SM_TEMPLATE *def;
  int save;

  AU_DISABLE (save);

  auth = db_find_class (AU_AUTH_CLASS_NAME);
  if (auth == NULL)
    {
      goto exit_on_error;
    }

  def = smt_edit_class_mop (auth, AU_ALTER);
  if (def == NULL)
    {
      goto exit_on_error;
    }

  smt_add_class_method (def, "check_authorization", "au_check_authorization_method");
  smt_assign_argument_domain (def, "check_authorization", true, NULL, 0, "integer", (DB_DOMAIN *) 0);
  smt_assign_argument_domain (def, "check_authorization", true, NULL, 1, "varchar(255)", (DB_DOMAIN *) 0);
  smt_assign_argument_domain (def, "check_authorization", true, NULL, 2, "integer", (DB_DOMAIN *) 0);
  sm_update_class (def, NULL);

  au_grant (Au_public_user, auth, AU_EXECUTE, false);

  AU_ENABLE (save);
  return NO_ERROR;

exit_on_error:
  AU_ENABLE (save);
  return ER_FAILED;
}

