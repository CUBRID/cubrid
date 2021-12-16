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


class print_output;

/*
 * Authorization Class Names
 */

extern const char *AU_ROOT_CLASS_NAME;
extern const char *AU_OLD_ROOT_CLASS_NAME;
extern const char *AU_USER_CLASS_NAME;
extern const char *AU_PASSWORD_CLASS_NAME;
extern const char *AU_AUTH_CLASS_NAME;
extern const char *AU_GRANT_CLASS_NAME;
extern const char *AU_PUBLIC_USER_NAME;
extern const char *AU_DBA_USER_NAME;

/*
 * Authorization Types
 */
/* obsolete, should be using the definition from dbdef.h */

#define AU_TYPE         DB_AUTH
#define AU_NONE         DB_AUTH_NONE
#define AU_SELECT       DB_AUTH_SELECT
#define AU_INSERT       DB_AUTH_INSERT
#define AU_UPDATE       DB_AUTH_UPDATE
#define AU_DELETE       DB_AUTH_DELETE
#define AU_ALTER        DB_AUTH_ALTER
#define AU_INDEX        DB_AUTH_INDEX
#define AU_EXECUTE      DB_AUTH_EXECUTE

/*
 * Mask to extract only the authorization bits from a cache.  This can also
 * be used as an absolute value to see if all possible authorizations have
 * been given
 * TODO : LP64
 */

#define AU_TYPE_MASK            0x7F
#define AU_GRANT_MASK           0x7F00
#define AU_FULL_AUTHORIZATION   0x7F7F
#define AU_NO_AUTHORIZATION     0

/*
 * the grant option for any particular authorization type is cached in the
 * same integer, shifted up eight bits.
 */

#define AU_GRANT_SHIFT          8

/* Invalid cache is identified when the high bit is on. */

#define AU_CACHE_INVALID        0x80000000


int au_disable (void);
int au_sysadm_disable (void);
void au_enable (int save);
MOP au_get_public_user (void);
MOP au_get_dba_user (void);

#define AU_DISABLE(save) \
  do \
    { \
      save = Au_disable; \
      Au_disable = 1; \
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
      save = Au_disable; \
      Au_disable = 0; \
    } \
  while (0)
#define AU_SAVE_AND_DISABLE(save) \
  do \
    { \
      save = Au_disable; \
      Au_disable = 1; \
    } \
  while (0)
#define AU_RESTORE(save) \
  do \
    { \
      Au_disable = save; \
    } \
  while (0)

#define AU_DISABLE_PASSWORDS    au_disable_passwords
#define AU_SET_USER     au_set_user

#define AU_MAX_PASSWORD_CHARS   31
#define AU_MAX_PASSWORD_BUF     2048
#define AU_MAX_COMMENT_CHARS    SM_MAX_COMMENT_LENGTH

/* free_and_init routine */
#define au_free_authorization_cache_and_init(cache) \
  do \
    { \
      au_free_authorization_cache ((cache)); \
      (cache) = NULL; \
    } \
  while (0)

/*
 * Global Variables
 */
extern MOP Au_root;
extern MOP Au_user;
extern MOP Au_dba_user;
extern MOP Au_public_user;
extern char Au_user_password[AU_MAX_PASSWORD_BUF + 4];
extern int Au_disable;


extern void au_init (void);
extern void au_final (void);

extern int au_install (void);
extern int au_force_write_new_auth (void);
extern int au_add_method_check_authorization (void);
extern int au_start (void);
extern int au_login (const char *name, const char *password, bool ignore_dba_privilege);

extern void au_disable_passwords (void);
extern int au_set_user (MOP newuser);

/* user/group hierarchy maintenance */
extern MOP au_find_user (const char *user_name);
extern int au_find_user_to_drop (const char *user_name, MOP * user);
extern MOP au_add_user (const char *name, int *exists);
extern int au_add_member (MOP group, MOP member);
extern int au_drop_member (MOP group, MOP member);
extern int au_drop_user (MOP user);
extern int au_set_password (MOP user, const char *password);
extern int au_set_user_comment (MOP user, const char *comment);

extern const char *au_user_name (void);
extern char *au_user_name_dup (void);
extern bool au_has_user_name (void);

/* grant/revoke */
extern int au_grant (MOP user, MOP class_mop, DB_AUTH type, bool grant_option);
extern int au_revoke (MOP user, MOP class_mop, DB_AUTH type);

extern int au_delete_auth_of_dropping_table (const char *class_name);

/* class & instance accessors */
extern int au_fetch_class (MOP op, SM_CLASS ** class_ptr, AU_FETCHMODE fetchmode, DB_AUTH type);
extern int au_fetch_class_by_classmop (MOP op, SM_CLASS ** class_ptr, AU_FETCHMODE fetchmode, DB_AUTH type);
extern int au_fetch_class_by_instancemop (MOP op, SM_CLASS ** class_ptr, AU_FETCHMODE fetchmode, DB_AUTH type);
extern int au_fetch_class_force (MOP op, SM_CLASS ** class_, AU_FETCHMODE fetchmode);

extern int au_fetch_instance (MOP op, MOBJ * obj_ptr, AU_FETCHMODE mode, LC_FETCH_VERSION_TYPE fetch_version_type,
			      DB_AUTH type);
extern int au_fetch_instance_force (MOP op, MOBJ * obj_ptr, AU_FETCHMODE fetchmode,
				    LC_FETCH_VERSION_TYPE fetch_version_type);

extern int au_check_authorization (MOP op, DB_AUTH auth);

/* class cache support */
extern void au_free_authorization_cache (void *cache);
extern void au_reset_authorization_caches (void);

/* misc utilities */
extern int au_change_owner (MOP classmop, MOP owner);
extern MOP au_get_class_owner (MOP classmop);
extern int au_check_user (void);
extern char *au_get_user_name (MOP obj);
extern bool au_is_dba_group_member (MOP user);
extern void au_change_serial_owner_method (MOP obj, DB_VALUE * returnval, DB_VALUE * serial, DB_VALUE * owner);

/* debugging functions */
extern void au_dump (void);
extern void au_dump_to_file (FILE * fp);
extern void au_dump_user (MOP user, FILE * fp);

#if defined(ENABLE_UNUSED_FUNCTION)
/* used by test code, should be changed to au_dump . . . */
extern void au_print_class_auth (MOP class_mop);
#endif

/* called only at initialization time to get the static methods linked */
extern void au_link_static_methods (void);

/* migration utilities */

extern int au_export_users (print_output & output_ctx);
extern int au_export_grants (print_output & output_ctx, MOP class_mop);

extern int au_get_class_privilege (DB_OBJECT * mop, unsigned int *auth);

/*
 * Etc
 */

extern void au_find_user_method (MOP class_mop, DB_VALUE * returnval, DB_VALUE * name);
extern void au_add_user_method (MOP class_mop, DB_VALUE * returnval, DB_VALUE * name, DB_VALUE * password);
extern void au_set_password_method (MOP user, DB_VALUE * returnval, DB_VALUE * password);
extern void au_set_password_encoded_method (MOP user, DB_VALUE * returnval, DB_VALUE * password);
extern void au_set_password_encoded_sha1_method (MOP user, DB_VALUE * returnval, DB_VALUE * password);
extern void au_add_member_method (MOP user, DB_VALUE * returnval, DB_VALUE * memval);
extern void au_drop_member_method (MOP user, DB_VALUE * returnval, DB_VALUE * memval);
extern void au_drop_user_method (MOP root, DB_VALUE * returnval, DB_VALUE * name);
extern void au_change_owner_method (MOP obj, DB_VALUE * returnval, DB_VALUE * class_, DB_VALUE * owner);
extern int au_change_trigger_owner (MOP trigger, MOP owner);
extern void au_change_trigger_owner_method (MOP obj, DB_VALUE * returnval, DB_VALUE * trigger, DB_VALUE * owner);
extern void au_get_owner_method (MOP obj, DB_VALUE * returnval, DB_VALUE * class_);
extern void au_check_authorization_method (MOP obj, DB_VALUE * returnval, DB_VALUE * class_, DB_VALUE * auth);
extern int au_change_sp_owner (MOP sp, MOP owner);
extern void au_change_sp_owner_method (MOP obj, DB_VALUE * returnval, DB_VALUE * sp, DB_VALUE * owner);
extern void au_login_method (MOP class_mop, DB_VALUE * returnval, DB_VALUE * user, DB_VALUE * password);
extern void au_dump_auth (FILE * fp);
extern void au_describe_user_method (MOP user, DB_VALUE * returnval);
extern void au_info_method (MOP class_mop, DB_VALUE * returnval, DB_VALUE * info);
extern void au_describe_root_method (MOP class_mop, DB_VALUE * returnval, DB_VALUE * info);
extern int au_check_serial_authorization (MOP serial_object);
extern int au_check_server_authorization (MOP server_object);
extern bool au_is_server_authorized_user (DB_VALUE * owner_val);
extern const char *au_get_public_user_name (void);
extern const char *au_get_user_class_name (void);
#if defined(ENABLE_UNUSED_FUNCTION)
extern char *toupper_string (const char *name1, char *name2);
#endif
#endif /* _AUTHENTICATE_H_ */
