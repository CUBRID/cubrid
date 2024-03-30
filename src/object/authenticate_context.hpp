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
 * authenticate_context.hpp - Define authenticate context
 *
 */

#ifndef _AUTHENTICATE_CONTEXT_HPP_
#define _AUTHENTICATE_CONTEXT_HPP_

#include "dbtype_def.h"

#define AU_MAX_PASSWORD_CHARS   31
#define AU_MAX_PASSWORD_BUF     2048
#define AU_MAX_COMMENT_CHARS    SM_MAX_COMMENT_LENGTH

class authenticate_context
{
  private:
    /*
     * Global MOP of the authorization root object.
     * This is cached here after the database is restarted so we don't
     * have to keep looking for it.
     * legacy name: Au_root
     */
    MOP root;

    /* db_root */
    MOP authorizations_class;

    /* db_authorization */
    MOP authorization_class;

    /* db_user */
    MOP user_class;

    /* db_password */
    MOP password_class;

    /*
     * Flag to disable authorization checking.  Only for well behaved
     * internal system functions.  Should not set this directly,
     * use the AU_DISABLE, AU_ENABLE macros instead.
     * legacy name: Au_disable
     */
    bool disable_auth_check;

    /*
    * Au_ignore_passwords
    *
    * When this flag is set, the authorization system will ignore passwords
    * when logging in users.  This is intended for use only be system
    * defined utility programs that want to run as the DBA user but don't
    * want to require a password entry each time.  These would be protected
    * through OS file protected by the DBA rather than by the database
    * authorization mechanism.
    * This initializes to zero and is changed only by calling the function
    * au_enable_system_login().
    * legacy name: Au_ignore_passwords
    */
    bool ignore_passwords;

    /*
    * Au_dba_user, Au_public_user
    *
    * These are the two system defined user objects.
    * All users are automatically a member of the PUBLIC user and hence
    * grants to PUBLIC are visible to everyone.  The DBA is automatically
    * a member of all groups/users and hence will have all permissions.
    */
    MOP public_user;
    MOP dba_user;

    /*
    * Au_user
    *
    * This points to the MOP of the user object of the currently
    * logged in user.  Can be overridden in special cases to change
    * system authorizations.
    */
    MOP current_user;
    /*
     * Au_user_name, Au_user_password
     *
     * Saves the registered user name and password.
     * Login normally ocurrs before the database is restarted so all we
     * do is register the user name.  The user name will be validated
     * against the database at the time of restart.
     * Once the database has been started, we get the user information
     * directly from the user object and no longer use these variables.
     *
     * NOTE: Need to be storing the password in an encrypted string.
     */
    char user_name[DB_MAX_USER_LENGTH + 4] = { '\0' };
    char user_password_des_oldstyle[AU_MAX_PASSWORD_BUF + 4] = { '\0' };
    char user_password_sha1[AU_MAX_PASSWORD_BUF + 4] = { '\0' };
    char user_password_sha2_512[AU_MAX_PASSWORD_BUF + 4] = { '\0' };

    bool is_started;

    void reset (void);

  public:

    authenticate_context (void); // au_init ()
    ~authenticate_context (); // au_final ()

    int start (void); // au_start ()

    int login (const char *name, const char *password, bool ignore_dba_privilege);


    // for backward compatability
    const char *get_public_user_name (void);
    const char *get_user_class_name (void);
};

#define Au_root

#endif // _AUTHENTICATE_CONTEXT_HPP_