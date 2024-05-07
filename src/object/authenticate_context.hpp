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

#include "authenticate_cache.hpp"
#include "authenticate_password.hpp" /* AU_MAX_PASSWORD_BUF */

class authenticate_context
{
  public:

    // TODO: these variables should be encapsulated
    //       However for backward compatiblity, define them as public member variables
    // ================================================================
    // public member variables
    // ================================================================

    /*
     * Global MOP of the authorization root object.
     * This is cached here after the database is restarted so we don't
     * have to keep looking for it.
     * legacy name: Au_root
     */
    MOP root;


    MOP authorizations_class;     /* db_root */


    MOP authorization_class;     /* db_authorization */


    MOP user_class;     /* db_user */

    /*
     * Au_password_class
     *
     * This is a hack until we get a proper "system" authorization
     * level.  We need to detect attempts to update or delete
     * system classes by the DBA when there is no approved
     * direct update mechanism.  This is the case for all the
     * authorization classes.  The only way they can be updated is
     * through the authorization functions or methods.
     * To avoid searching for these classes all the time, cache
     * them here.
     */
    MOP password_class; /* db_password */

    /*
     * Flag to disable authorization checking.  Only for well behaved
     * internal system functions.  Should not set this directly,
     * use the AU_DISABLE, AU_ENABLE macros instead.
     * legacy name: Au_disable
     */
    bool disable_auth_check;

    /*
    * ignore_passwords
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

    char Au_user_password[AU_MAX_PASSWORD_BUF + 4]; // unused

    char user_name[DB_MAX_USER_LENGTH + 4] = { '\0' };
    char user_password_des_oldstyle[AU_MAX_PASSWORD_BUF + 4] = { '\0' };
    char user_password_sha1[AU_MAX_PASSWORD_BUF + 4] = { '\0' };
    char user_password_sha2_512[AU_MAX_PASSWORD_BUF + 4] = { '\0' };

    authenticate_cache caches;

    bool is_started;

    // ================================================================
    // public member functions
    // ================================================================

    authenticate_context (void); // au_init ()
    ~authenticate_context (); // au_final ()

    void reset (void);

    int install (void); // au_install ()
    int start (void); // au_start ()

    int login (const char *name, const char *password, bool ignore_dba_privilege); // au_login ()

    int set_user (MOP newuser); // au_set_user ()

    int set_password (MOP user, const char *password); // au_set_password_encrypt ()
    int set_password (MOP user, const char *password, int encode, char encrypt_prefix); // au_set_password_encrypt ()

    void disable_passwords (void); // au_disable_passwords ()

    // checks
    int check_user (void);
    bool has_user_name (void);

    // getters
    MOP get_public_user (void);
    MOP get_dba_user (void);

    const char *get_current_user_name (void);

    const char *get_public_user_name (void);
    const char *get_user_class_name (void);

  private:
    int perform_login (const char *name, const char *password, bool ignore_dba_privilege);
};

#endif // _AUTHENTICATE_CONTEXT_HPP_