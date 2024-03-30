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

#include "authenticate_cache.hpp" /* init_caches */
#include "authenticate_user_access.hpp" /* au_find_user () */

#include "db.h"
#include "dbi.h"
#include "error_manager.h"
#include "schema_manager.h" /* sm_find_class() */
#include "schema_system_catalog_constants.h"

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
{
  reset ();
  init_caches ();
  is_started = false;
}

authenticate_context::~authenticate_context ()
{
  reset ();

  /*
   * could remove the static links here but it isn't necessary and
   * we may need them again the next time we restart
   */
  flush_caches ();
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
  // Au_disable = 1;
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
	  if (strlen (Au_user_name) == 0)
	    {
	      strcpy (Au_user_name, "PUBLIC");
	    }

	  error = au_perform_login (Au_user_name, Au_user_password_sha2_512, false);
	}
    }

  /* make sure this is off */
  // Au_disable = 0;
  disable_auth_check = false;

  if (error != NO_ERROR)
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
	  strcpy (Au_user_name, name);
	}

      if (password == NULL || strlen (password) == 0)
	{
	  strcpy (Au_user_password_des_oldstyle, "");
	  strcpy (Au_user_password_sha1, "");
	  strcpy (Au_user_password_sha2_512, "");
	}
      else
	{
	  /* store the password encrypted(DES and SHA1 both) so we don't have buffers lying around with the obvious
	   * passwords in it. */
	  encrypt_password (password, 1, Au_user_password_des_oldstyle);
	  encrypt_password_sha1 (password, 1, Au_user_password_sha1);
	  encrypt_password_sha2_512 (password, Au_user_password_sha2_512);
	}
    }
  else
    {
      /* Change users within an active database. */
      AU_DISABLE (save);
      error = au_perform_login (name, password, ignore_dba_privilege);
      AU_ENABLE (save);
    }
  return (error);
}