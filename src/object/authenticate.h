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
 * authenticate.h - Authorization manager
 *
 */

#ifndef _AUTHENTICATE_H_
#define _AUTHENTICATE_H_

#ident "$Id$"

#if defined (SERVER_MODE)
#error Does not belong to server module
#endif /* defined (SERVER_MODE) */

#ifndef __cplusplus
#error Requires C++
#endif // not c++

#include <stdio.h>
#include <stdlib.h>

#include "error_manager.h"
#include "class_object.h"
#include "databases_file.h"
#include "object_fetch.h"
#include "extract_schema.hpp"
#include "schema_system_catalog_constants.h"
#include "set_object.h"
#include "authenticate_constants.h"
#include "authenticate_context.hpp"

class print_output;

extern EXPORT_IMPORT authenticate_context *au_ctx;

/* Backward compatability */
// Instead of using global variables, use authenticate_context's member variables/functions

// Variables
#define Au_root                         au_ctx->root
#define Au_user                         au_ctx->current_user
#define Au_dba_user                     au_ctx->dba_user
#define Au_public_user                  au_ctx->public_user
#define Au_disable                      au_ctx->disable_auth_check

#define Au_authorizations_class         au_ctx->authorizations_class
#define Au_authorization_class          au_ctx->authorization_class
#define Au_user_class                   au_ctx->user_class
#define Au_password_class               au_ctx->password_class

#define Au_user_name                    au_ctx->user_name

#define Au_user_password                au_ctx->user_password
#define Au_user_password_des_oldstyle   au_ctx->user_password_des_oldstyle
#define Au_user_password_sha1           au_ctx->user_password_sha1
#define Au_user_password_sha2_512       au_ctx->user_password_sha2_512

#define Au_cache                        au_ctx->caches

/* Functions */
#define au_install                      au_ctx->install
#define au_start                        au_ctx->start

#define au_get_public_user_name         au_ctx->get_public_user_name
#define au_get_user_class_name          au_ctx->get_user_class_name

#define au_set_user                     au_ctx->set_user
#define au_set_password_encrypt         au_ctx->set_password

#define au_get_current_user_name        au_ctx->get_current_user_name

#define au_check_user                   au_ctx->check_user
#define au_has_user_name                au_ctx->has_user_name

#define AU_SET_USER     au_set_user

// FIXME: To migrate legacy
// AU_DISABLE_PASSWORDS () is called in serveral places without calling au_init ()
#define AU_DISABLE_PASSWORDS() \
  do \
    { \
      if (au_ctx == nullptr) \
      { \
        au_init (); \
      } \
      au_ctx->disable_passwords (); \
    } \
  while (0)

#define AU_DISABLE(save) \
  do \
    { \
      save = Au_disable ? 1 : 0; \
      Au_disable = true; \
    } \
  while (0)
#define AU_ENABLE(save) \
  do \
    { \
      Au_disable = save; \
    } \
  while (0)
#define AU_SAVE_AND_ENABLE(save) \
  do \
    { \
      save = Au_disable ? 1 : 0; \
      Au_disable = false; \
    } \
  while (0)
#define AU_SAVE_AND_DISABLE(save) \
  do \
    { \
      save = Au_disable ? 1 : 0; \
      Au_disable = true; \
    } \
  while (0)
#define AU_RESTORE(save) \
  do \
    { \
      Au_disable = save; \
    } \
  while (0)

extern void au_init (void);
extern void au_final (void);
extern int au_login (const char *name, const char *password, bool ignore_dba_privilege);

/*
 * GRANT/REVOKE OPERATIONS (authenticate_grant.cpp)
 */

extern int au_grant (MOP user, MOP class_mop, DB_AUTH type, bool grant_option);
extern int au_revoke (MOP user, MOP class_mop, DB_AUTH type);

#if defined (SA_MODE)
extern int au_force_write_new_auth (void);
#endif

// get authenticate info of the given class mop
extern int au_get_class_privilege (DB_OBJECT * mop, unsigned int *auth);

/*
 * USER OPERATIONS (authenticate_access_user.cpp)
 */
extern MOP au_find_user (const char *user_name);
extern int au_find_user_to_drop (const char *user_name, MOP * user);
extern MOP au_add_user (const char *name, int *exists);

/* user/group hierarchy maintenance */
extern int au_add_member (MOP group, MOP member);
extern int au_drop_member (MOP group, MOP member);
extern int au_drop_user (MOP user);
extern int au_set_user_comment (MOP user, const char *comment);

extern char *au_get_user_name (MOP obj);
extern bool au_is_dba_group_member (MOP user);
extern bool au_is_user_group_member (MOP group_user, MOP user);
//

/*
 * CLASS ACCESS OPERATIONS (authenticate_access_class.cpp)
 */
/* class & instance accessors */
extern int au_fetch_class (MOP op, SM_CLASS ** class_ptr, AU_FETCHMODE fetchmode, DB_AUTH type);
extern int au_fetch_class_by_classmop (MOP op, SM_CLASS ** class_ptr, AU_FETCHMODE fetchmode, DB_AUTH type);
extern int au_fetch_class_by_instancemop (MOP op, SM_CLASS ** class_ptr, AU_FETCHMODE fetchmode, DB_AUTH type);
extern int au_fetch_class_force (MOP op, SM_CLASS ** class_, AU_FETCHMODE fetchmode);

extern int au_fetch_instance (MOP op, MOBJ * obj_ptr, AU_FETCHMODE mode, LC_FETCH_VERSION_TYPE fetch_version_type,
			      DB_AUTH type);
extern int au_fetch_instance_force (MOP op, MOBJ * obj_ptr, AU_FETCHMODE fetchmode,
				    LC_FETCH_VERSION_TYPE fetch_version_type);
//

/*
 * CHECK AUTHORIZATION OPERATIONS
 */
extern int au_check_class_authorization (MOP op, DB_AUTH auth);	// legacy name - au_check_authorization
extern int au_check_serial_authorization (MOP serial_object);
extern int au_check_server_authorization (MOP server_object);
extern bool au_is_server_authorized_user (DB_VALUE * owner_val);
//

/*
 * AUTHENTICATE CACHE OPERATIONS (authenticate_cache.cpp)
 */
/* class cache support */
/* free_and_init routine */
#define au_free_authorization_cache_and_init(cache) \
  do \
    { \
      Au_cache.free_authorization_cache ((cache)); \
      (cache) = NULL; \
    } \
  while (0)

#define au_reset_authorization_caches() \
  do \
    { \
      Au_cache.reset_authorization_caches (); \
    } \
  while (0)
//

/*
 * MIGRATION OPERATIONS (authenticate_migration.cpp)
 */
extern int au_export_users (extract_context & ctxt, print_output & output_ctx);
extern int au_export_grants (extract_context & ctxt, print_output & output_ctx, MOP class_mop);
//

/*
 * OWNER OPERATIONS
 */
extern int au_check_owner (DB_VALUE * creator_val);

extern int au_change_owner (MOP class_mop, MOP owner_mop);
extern int au_change_class_owner (MOP class_mop, MOP owner_mop);
extern int au_change_serial_owner (MOP serial_mop, MOP owner_mop, bool by_class_owner_change);
extern int au_change_trigger_owner (MOP trigger_mop, MOP owner_mop);
extern int au_change_sp_owner (MOP sp, MOP owner);
extern MOP au_get_class_owner (MOP classmop);
//

/*
 * DEBUGGING PURPOSE FUNCTIONS
 */
extern void au_dump (void);
extern void au_dump_to_file (FILE * fp);
extern void au_dump_user (MOP user, FILE * fp);
extern void au_dump_auth (FILE * fp);
//

/*
 * SET TYPE OPERATIONS
 */
extern int au_get_set (MOP obj, const char *attname, DB_SET ** set);
extern int au_get_object (MOP obj, const char *attname, MOP * mop_ptr);
extern int au_set_get_obj (DB_SET * set, int index, MOP * obj);
//

#endif /* _AUTHENTICATE_H_ */
