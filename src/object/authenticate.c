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

#include "porting.h"
#include "misc_string.h"
#include "memory_alloc.h"
#include "dbtype.h"
#include "dbdef.h"
#include "error_manager.h"
#include "boot_cl.h"
#include "work_space.h"
#include "object_primitive.h"
#include "class_object.h"
#include "schema_manager.h"
#include "authenticate.h"
#include "set_object.h"
#include "object_accessor.h"
#include "encryption.h"
#include "message_catalog.h"
#include "string_opfunc.h"
#include "locator_cl.h"
#include "virtual_object.h"
#include "db.h"
#include "trigger_manager.h"
#include "transform.h"
#include "environment_variable.h"
#include "execute_schema.h"
#include "jsp_cl.h"
#include "object_print.h"
#include "execute_statement.h"
#include "optimizer.h"
#include "network_interface_cl.h"
#include "dbval.h"		/* this must be the last header file included */

#if defined(SA_MODE)
extern bool catcls_Enable;
#endif /* SA_MODE */

/*
 * Message id in the set MSGCAT_SET_AUTHORIZATION
 * in the message catalog MSGCAT_CATALOG_CUBRID (file cubrid.msg).
 */
#define MSGCAT_AUTH_INVALID_CACHE       1
#define MSGCAT_AUTH_CLASS_NAME          2
#define MSGCAT_AUTH_FROM_USER           3
#define MSGCAT_AUTH_USER_TITLE          4
#define MSGCAT_AUTH_UNDEFINED_USER      5
#define MSGCAT_AUTH_USER_NAME           6
#define MSGCAT_AUTH_USER_ID             7
#define MSGCAT_AUTH_USER_MEMBERS        8
#define MSGCAT_AUTH_USER_GROUPS         9
#define MSGCAT_AUTH_USER_NAME2          10
#define MSGCAT_AUTH_CURRENT_USER        11
#define MSGCAT_AUTH_ROOT_TITLE          12
#define MSGCAT_AUTH_ROOT_USERS          13
#define MSGCAT_AUTH_GRANT_DUMP_ERROR    14
#define MSGCAT_AUTH_AUTH_TITLE          15
#define MSGCAT_AUTH_USER_DIRECT_GROUPS  16

/*
 * Authorization Class Names
 */
const char *AU_ROOT_CLASS_NAME = "db_root";
const char *AU_OLD_ROOT_CLASS_NAME = "db_authorizations";

const char *AU_USER_CLASS_NAME = "db_user";
const char *AU_PASSWORD_CLASS_NAME = "db_password";
const char *AU_AUTH_CLASS_NAME = "db_authorization";
const char *AU_GRANT_CLASS_NAME = "db_grant";

const char *AU_PUBLIC_USER_NAME = "PUBLIC";
const char *AU_DBA_USER_NAME = "DBA";


/*
 * Grant set structure
 *
 * Note :
 *    Grant information is stored packed in a sequence.  These
 *    macros define the length of the "elements" of the sequence and the
 *    offsets to particular fields in each element.  Previously, grants
 *    were stored in their own object but that lead to serious performance
 *    problems as we tried to load each grant object from the server.
 *    This way, grants are stored in the set directly with the authorization
 *    object so only one fetch is required.
 *
 */

#define GRANT_ENTRY_LENGTH 		3
#define GRANT_ENTRY_CLASS(index) 	(index )
#define GRANT_ENTRY_SOURCE(index) 	((index) + 1)
#define GRANT_ENTRY_CACHE(index) 	((index) + 2)

#define PASSWORD_ENCRYPTION_SEED        "U9a$y1@zw~a0%"
#define ENCODE_PREFIX_DEFAULT           (char)0
#define ENCODE_PREFIX_DES               (char)1
#define ENCODE_PREFIX_SHA1              (char)2
#define IS_ENCODED_DES(string)          (string[0] == ENCODE_PREFIX_DES)
#define IS_ENCODED_SHA1(string)         (string[0] == ENCODE_PREFIX_SHA1)


/* Macro to determine if a dbvalue is a character strign type. */
#define IS_STRING(n)    (db_value_type(n) == DB_TYPE_VARCHAR  || \
                         db_value_type(n) == DB_TYPE_CHAR     || \
                         db_value_type(n) == DB_TYPE_VARNCHAR || \
                         db_value_type(n) == DB_TYPE_NCHAR)
/* Macro to determine if a name is system catalog class */
#define IS_CATALOG_CLASS(name) \
        (strcmp(name, CT_CLASS_NAME) == 0 || \
         strcmp(name, CT_ATTRIBUTE_NAME) == 0 || \
         strcmp(name, CT_DOMAIN_NAME) == 0 || \
         strcmp(name, CT_METHOD_NAME) == 0 || \
         strcmp(name, CT_METHSIG_NAME) == 0 || \
         strcmp(name, CT_METHARG_NAME) == 0 || \
         strcmp(name, CT_METHFILE_NAME) == 0 || \
         strcmp(name, CT_QUERYSPEC_NAME) == 0 || \
         strcmp(name, CT_RESOLUTION_NAME) ==0 || \
         strcmp(name, CT_INDEX_NAME) == 0 || \
         strcmp(name, CT_INDEXKEY_NAME) == 0 || \
         strcmp(name, CT_CLASSAUTH_NAME) == 0 || \
         strcmp(name, CT_DATATYPE_NAME) == 0 || \
         strcmp(name, CT_STORED_PROC_NAME) == 0 || \
         strcmp(name, CT_STORED_PROC_ARGS_NAME) == 0 || \
         strcmp(name, CT_PARTITION_NAME) == 0 || \
         strcmp(name, CT_SERIAL_NAME) == 0 || \
         strcmp(name, CT_USER_NAME) == 0 || \
         strcmp(name, CT_COLLATION_NAME) == 0 || \
         strcmp(name, CT_HA_APPLY_INFO_NAME) == 0 || \
         strcmp(name, CT_TRIGGER_NAME) == 0 || \
         strcmp(name, CT_ROOT_NAME) == 0 || \
         strcmp(name, CT_PASSWORD_NAME) == 0 || \
         strcmp(name, CT_AUTHORIZATION_NAME) == 0 || \
         strcmp(name, CT_AUTHORIZATIONS_NAME) == 0 || \
	 strcmp(name, CT_CHARSET_NAME) == 0)

typedef enum fetch_by FETCH_BY;
enum fetch_by
{
  DONT_KNOW,			/* Don't know the mop is a class os an instance */
  BY_INSTANCE_MOP,		/* fetch a class by an instance mop */
  BY_CLASS_MOP			/* fetch a class by the class mop */
};

/*
 * AU_GRANT
 *
 * This is an internal structure used to calculate the recursive
 * effects of a revoke operation.
 */
typedef struct au_grant AU_GRANT;
struct au_grant
{
  struct au_grant *next;

  MOP auth_object;
  MOP user;
  MOP grantor;

  DB_SET *grants;
  int grant_index;

  int grant_option;
  int legal;
};

/*
 * AU_CLASS_CACHE
 *
 * This structure is attached to classes and provides a cache of
 * the authorization bits.  Once authorization is calculated by examining
 * the group/grant hierarchy, the combined vector of bits is stored
 * in the cache for faster access.
 * In releases prior to 2.0, this was a single vector of bits for the
 * active user.  With the introduction of views, it became necessary
 * to relatively quickly perform a "setuid" operation to switch
 * authorization contexts within methods accessed through a view.
 * Because of this, the cache has been extended to be a variable length
 * array of entries, indexed by a user identifier.
 */
typedef struct au_class_cache AU_CLASS_CACHE;
struct au_class_cache
{
  struct au_class_cache *next;

  SM_CLASS *class_;
  unsigned int data[1];
};

/*
 * AU_USER_CACHE
 *
 * This is used to maintain a list of the users that have been
 * registered into the authorization caches.  Each time a "setuid" is
 * performed, the requested user is added to the caches if it is
 * not already present.
 */
typedef struct au_user_cache AU_USER_CACHE;
struct au_user_cache
{
  struct au_user_cache *next;

  DB_OBJECT *user;
  int index;
};

/*
 * CLASS_GRANT
 *
 * Maintains information about a desired grant request.
 */
typedef struct class_grant CLASS_GRANT;
struct class_grant
{
  struct class_grant *next;

  struct class_user *user;
  int cache;
};

/*
 * CLASS_USER
 *
 * Maintains information about a desired grant subject user.
 */
typedef struct class_user CLASS_USER;
struct class_user
{
  struct class_user *next;

  MOP obj;

  CLASS_GRANT *grants;
  int available_auth;
};

/*
 * CLASS_AUTH
 *
 * Maintains information about the grants on a particular class.
 */
typedef struct class_auth CLASS_AUTH;
struct class_auth
{

  MOP class_mop;
  MOP owner;
  CLASS_USER *users;
};

/*
 * Au_root
 *
 * Global MOP of the authorization root object.
 * This is cached here after the database is restarted so we don't
 * have to keep looking for it.
 */
MOP Au_root = NULL;

/*
 * Au_disable
 *
 * Flag to disable authorization checking.  Only for well behaved
 * internal system functions.  Should not set this directly,
 * use the AU_DISABLE, AU_ENABLE macros instead.
 */
int Au_disable = 1;

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
 */
static int Au_ignore_passwords = 0;

/*
 * Au_dba_user, Au_public_user
 *
 * These are the two system defined user objects.
 * All users are automatically a member of the PUBLIC user and hence
 * grants to PUBLIC are visible to everyone.  The DBA is automatically
 * a member of all groups/users and hence will have all permissions.
 */
MOP Au_public_user = NULL;
MOP Au_dba_user = NULL;

/*
 * Au_user
 *
 * This points to the MOP of the user object of the currently
 * logged in user.  Can be overridden in special cases to change
 * system authorizations.
 */
MOP Au_user = NULL;

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
static char Au_user_name[DB_MAX_USER_LENGTH + 4] = { '\0' };
char Au_user_password_des_oldstyle[AU_MAX_PASSWORD_BUF + 4] = { '\0' };
char Au_user_password_sha1[AU_MAX_PASSWORD_BUF + 4] = { '\0' };

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
static DB_OBJECT *Au_authorizations_class;
static DB_OBJECT *Au_authorization_class;
static DB_OBJECT *Au_user_class;
static DB_OBJECT *Au_password_class;

/*
 * Au_user_cache
 *
 * The list of cached users.
 */
static AU_USER_CACHE *Au_user_cache = NULL;

/*
 * Au_class_caches
 *
 * A list of all allocated class caches.  These are maintained on a list
 * so that we can get to all of them easily when they need to be
 * altered.
 */
static AU_CLASS_CACHE *Au_class_caches = NULL;

/*
 * Au_cache_depth
 *
 * These maintain information about the structure of the class caches.
 * Au_cache_depth has largest current index.
 * Au_cache_max has the total size of the allocated arrays.
 * Au_cache_increment has the growth count when the array needs to be
 * extended.
 * The caches are usually allocated larger than actually necessary so
 * we can avoid reallocating all of them when a new user is added.
 * Probably not that big a deal.
 */
static int Au_cache_depth = 0;
static int Au_cache_max = 0;
static int Au_cache_increment = 4;

/*
 * Au_cache_index
 *
 * This is the current index into the class authorization caches.
 * It will be maintained in parallel with the current user.
 * Each user is assigned a particular index, when the user changes,
 * Au_cache_index is changed as well.
 */
static int Au_cache_index = -1;

static const char *auth_type_name[] = {
  "select", "insert", "update", "delete", "alter", "index", "execute"
};


/* 'get_attribute_number' is a statically linked method used only for QA
   scenario */
void get_attribute_number (DB_OBJECT * target, DB_VALUE * result,
			   DB_VALUE * attr_name);


/*
 * au_static_links
 *
 * Since authorization is always linked in with the database, the
 * methods are defined statically.  The linkage will be done
 * during au_init even though it is redundant on subsequent
 * restart calls.
 */
static DB_METHOD_LINK au_static_links[] = {
  {"au_add_user_method", (METHOD_LINK_FUNCTION) au_add_user_method},
  {"au_drop_user_method", (METHOD_LINK_FUNCTION) au_drop_user_method},
  {"au_find_user_method", (METHOD_LINK_FUNCTION) au_find_user_method},
  {"au_add_member_method", (METHOD_LINK_FUNCTION) au_add_member_method},
  {"au_drop_member_method", (METHOD_LINK_FUNCTION) au_drop_member_method},
  {"au_set_password_method", (METHOD_LINK_FUNCTION) au_set_password_method},
  {"au_set_password_encoded_method",
   (METHOD_LINK_FUNCTION) au_set_password_encoded_method},
  {"au_set_password_encoded_sha1_method",
   (METHOD_LINK_FUNCTION) au_set_password_encoded_sha1_method},
  {"au_describe_user_method", (METHOD_LINK_FUNCTION) au_describe_user_method},
  {"au_describe_root_method", (METHOD_LINK_FUNCTION) au_describe_root_method},
  {"au_info_method", (METHOD_LINK_FUNCTION) au_info_method},
  {"au_login_method", (METHOD_LINK_FUNCTION) au_login_method},
  {"au_change_owner_method", (METHOD_LINK_FUNCTION) au_change_owner_method},
  {"au_change_trigger_owner_method",
   (METHOD_LINK_FUNCTION) au_change_trigger_owner_method},
  {"au_get_owner_method", (METHOD_LINK_FUNCTION) au_get_owner_method},
  {"au_check_authorization_method",
   (METHOD_LINK_FUNCTION) au_check_authorization_method},

  /*
   * qo_set_cost
   *
   * This function is exported by optimizer/query_planner.c, and provides a backdoor that
   * allows us some gross manipulation capabilities for the query
   * optimizer.  By adding it to the list of method implementations that
   * are statically linked we make it easy for us to add a method to an
   * arbitrary class for those cases where we need to poke the optimizer.
   * To use this, try
   *
   *      alter class foo add method class set_cost(string, string)
   *              function qo_set_cost;
   *
   * and then utter
   *
   *      call set_cost('iscan', '0') on class foo;
   */
  {"qo_set_cost", (METHOD_LINK_FUNCTION) qo_set_cost},
  {"get_attribute_number", (METHOD_LINK_FUNCTION) get_attribute_number},
  {"dbmeth_class_name", (METHOD_LINK_FUNCTION) dbmeth_class_name},
  {"dbmeth_print", (METHOD_LINK_FUNCTION) dbmeth_print},
  {"au_change_sp_owner_method",
   (METHOD_LINK_FUNCTION) au_change_sp_owner_method},
  {"au_change_serial_owner_method",
   (METHOD_LINK_FUNCTION) au_change_serial_owner_method},

  {NULL, NULL}
};


static int au_get_set (MOP obj, const char *attname, DB_SET ** set);
static int au_get_object (MOP obj, const char *attname, MOP * mop_ptr);
static int au_set_get_obj (DB_SET * set, int index, MOP * obj);
static AU_CLASS_CACHE *au_make_class_cache (int depth);
static void au_free_class_cache (AU_CLASS_CACHE * cache);
static AU_CLASS_CACHE *au_install_class_cache (SM_CLASS * sm_class);

static int au_extend_class_caches (int *index);
static int au_find_user_cache_index (DB_OBJECT * user, int *index,
				     int check_it);
static void free_user_cache (AU_USER_CACHE * u);
static void reset_cache_for_user_and_class (SM_CLASS * sm_class);

static void remove_user_cache_references (MOP user);
static void init_caches (void);
static void flush_caches (void);

static MOP au_make_user (const char *name);
static int au_set_new_auth (MOP au_obj, MOP grantor, MOP user, MOP class_mop,
			    DB_AUTH auth_type, bool grant_option);
static MOP au_get_new_auth (MOP grantor, MOP user, MOP class_mop,
			    DB_AUTH auth_type);
static int au_insert_new_auth (MOP grantor, MOP user, MOP class_mop,
			       DB_AUTH auth_type, int grant_option);
static int au_update_new_auth (MOP grantor, MOP user, MOP class_mop,
			       DB_AUTH auth_type, int grant_option);
static int au_delete_new_auth (MOP grantor, MOP user, MOP class_mop,
			       DB_AUTH auth_type);
static int au_propagate_del_new_auth (AU_GRANT * glist, DB_AUTH mask);

static int check_user_name (const char *name);
static void encrypt_password (const char *pass, int add_prefix, char *dest);
static void encrypt_password_sha1 (const char *pass, int add_prefix,
				   char *dest);
static int io_relseek (const char *pass, int has_prefix, char *dest);
static bool match_password (const char *user, const char *database);
static int au_set_password_internal (MOP user, const char *password,
				     int encode, char encrypt_prefix);

static int au_add_direct_groups (DB_SET * new_groups, DB_VALUE * value);
static int au_compute_groups (MOP member, char *name);
static int au_add_member_internal (MOP group, MOP member, int new_user);

static int find_grant_entry (DB_SET * grants, MOP class_mop, MOP grantor);
static int add_grant_entry (DB_SET * grants, MOP class_mop, MOP grantor);
static void drop_grant_entry (DB_SET * grants, int index);
static int get_grants (MOP auth, DB_SET ** grant_ptr, int filter);
static int apply_grants (MOP auth, MOP class_mop, unsigned int *bits);
static int update_cache (MOP classop, SM_CLASS * sm_class,
			 AU_CLASS_CACHE * cache);
static int appropriate_error (unsigned int bits, unsigned int requested);
static int check_grant_option (MOP classop, SM_CLASS * sm_class,
			       DB_AUTH type);

static void free_grant_list (AU_GRANT * grants);
static int collect_class_grants (MOP class_mop, DB_AUTH type,
				 MOP revoked_auth, int revoked_grant_index,
				 AU_GRANT ** return_grants);
static void map_grant_list (AU_GRANT * grants, MOP grantor);
static int propagate_revoke (AU_GRANT * grant_list, MOP owner, DB_AUTH mask);

static int is_protected_class (MOP classmop, SM_CLASS * sm_class,
			       DB_AUTH auth);
static int check_authorization (MOP classobj, SM_CLASS * sm_class,
				DB_AUTH type);
static int fetch_class (MOP op, MOP * return_mop, SM_CLASS ** return_class,
			AU_FETCHMODE fetchmode, FETCH_BY fetch_by);
static int au_fetch_class_internal (MOP op, SM_CLASS ** class_ptr,
				    AU_FETCHMODE fetchmode, DB_AUTH type,
				    FETCH_BY fetch_by);

static int fetch_instance (MOP op, MOBJ * obj_ptr, AU_FETCHMODE fetchmode);
static int au_perform_login (const char *name, const char *password,
			     bool ignore_dba_privilege);

static CLASS_GRANT *make_class_grant (CLASS_USER * user, int cache);
static CLASS_USER *make_class_user (MOP user_obj);
static void free_class_grants (CLASS_GRANT * grants);
static void free_class_users (CLASS_USER * users);
static CLASS_USER *find_or_add_user (CLASS_AUTH * auth, MOP user_obj);
static int add_class_grant (CLASS_AUTH * auth, MOP source, MOP user,
			    int cache);
static int build_class_grant_list (CLASS_AUTH * cl_auth, MOP class_mop);
static void issue_grant_statement (FILE * fp, CLASS_AUTH * auth,
				   CLASS_GRANT * grant, int authbits);
static int class_grant_loop (CLASS_AUTH * auth, FILE * outfp);

static void au_print_cache (int cache, FILE * fp);
static void au_print_grant_entry (DB_SET * grants, int grant_index,
				  FILE * fp);
static void au_print_auth (MOP auth, FILE * fp);

static int au_change_serial_owner (MOP * object, MOP new_owner);
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
static int
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
	      db_fetch_set (*set, DB_FETCH_READ, 0);
	      set_filter (*set);
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
static int
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
static int
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
 * AUTHORIZATION CACHES
 */

/*
 * au_make_class_cache - Allocates and initializes a new class cache
 *    return: new cache structure
 *    depth(in): number of elements to include in the cache
 */
static AU_CLASS_CACHE *
au_make_class_cache (int depth)
{
  AU_CLASS_CACHE *new_class_cache = NULL;
  int i;
  size_t size;

  if (depth <= 0)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
    }
  else
    {
      size = sizeof (AU_CLASS_CACHE) + ((depth - 1) * sizeof (unsigned int));
      new_class_cache = (AU_CLASS_CACHE *) malloc (size);
      if (new_class_cache == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
		  1, size);
	  return NULL;
	}

      new_class_cache->next = NULL;
      new_class_cache->class_ = NULL;
      for (i = 0; i < depth; i++)
	{
	  new_class_cache->data[i] = AU_CACHE_INVALID;
	}
    }

  return new_class_cache;
}

/*
 * au_free_class_cache - Frees a class cache
 *    return: none
 *    cache(in): cache to free
 */
static void
au_free_class_cache (AU_CLASS_CACHE * cache)
{
  if (cache != NULL)
    {
      free_and_init (cache);
    }
}

/*
 * au_install_class_cache - This allocates a new class cache and attaches
 *                          it to a class.
 *   return: new class cache
 *   class(in): class structure to get the new cache
 *
 * Note: Once a cache/class association has been made, we also put the
 *       cache on the global cache list so we can maintain it consistently.
 */
static AU_CLASS_CACHE *
au_install_class_cache (SM_CLASS * sm_class)
{
  AU_CLASS_CACHE *new_class_cache;

  new_class_cache = au_make_class_cache (Au_cache_max);
  if (new_class_cache != NULL)
    {
      new_class_cache->next = Au_class_caches;
      Au_class_caches = new_class_cache;
      new_class_cache->class_ = sm_class;
      sm_class->auth_cache = new_class_cache;
    }

  return new_class_cache;
}

/*
 * au_free_authorization_cache -  This removes a class cache from the global
 *                                cache list, detaches it from the class
 *                                and frees it.
 *   return: none
 *   cache(in): class cache
 */
void
au_free_authorization_cache (void *cache)
{
  AU_CLASS_CACHE *c, *prev;

  if (cache != NULL)
    {
      for (c = Au_class_caches, prev = NULL;
	   c != NULL && c != cache; c = c->next)
	{
	  prev = c;
	}
      if (c != NULL)
	{
	  if (prev == NULL)
	    {
	      Au_class_caches = c->next;
	    }
	  else
	    {
	      prev->next = c->next;
	    }
	}
      au_free_class_cache ((AU_CLASS_CACHE *) cache);
    }
}

/*
 * au_extend_class_caches - This extends the all existing class caches so they
 *                          can contain an additional element.
 *   return: error code
 *   index(out): next available index
 *
 * Note: If we have already preallocated some extra elements it will use one
 *       and avoid reallocating all the caches. If we have no extra elements,
 *       we grow all the caches by a certain amount.
 */
static int
au_extend_class_caches (int *index)
{
  int error = NO_ERROR;
  AU_CLASS_CACHE *c, *new_list, *new_entry, *next;
  int new_max, i;

  if (Au_cache_depth < Au_cache_max)
    {
      *index = Au_cache_depth;
      Au_cache_depth++;
    }
  else
    {
      new_list = NULL;
      new_max = Au_cache_max + Au_cache_increment;

      for (c = Au_class_caches; c != NULL && !error; c = c->next)
	{
	  new_entry = au_make_class_cache (new_max);
	  if (new_entry == NULL)
	    {
	      assert (er_errid () != NO_ERROR);
	      error = er_errid ();
	    }
	  else
	    {
	      for (i = 0; i < Au_cache_depth; i++)
		{
		  new_entry->data[i] = c->data[i];
		}
	      new_entry->class_ = c->class_;
	      new_entry->next = new_list;
	      new_list = new_entry;
	    }
	}

      if (!error)
	{
	  for (c = Au_class_caches, next = NULL; c != NULL; c = next)
	    {
	      next = c->next;
	      c->class_->auth_cache = NULL;
	      au_free_class_cache (c);
	    }
	  for (c = new_list; c != NULL; c = c->next)
	    {
	      c->class_->auth_cache = c;
	    }

	  Au_class_caches = new_list;
	  Au_cache_max = new_max;
	  *index = Au_cache_depth;
	  Au_cache_depth++;
	}
    }

  return error;
}

/*
 * au_find_user_cache_index - This determines the cache index for the given
 *                            user.
 *   return: error code
 *   user(in): user object
 *   index(out): returned user index
 *   check_it(in):
 *
 * Note: If the user has never been added to the authorization cache,
 *       we reserve a new index for the user.  Reserving the user index may
 *       result in growing all the existing class caches.
 *       This is the primary work function for AU_SET_USER() and it should
 *       be fast.
 */
static int
au_find_user_cache_index (DB_OBJECT * user, int *index, int check_it)
{
  int error = NO_ERROR;
  AU_USER_CACHE *u, *new_user_cache;
  DB_OBJECT *class_mop;

  for (u = Au_user_cache; u != NULL && !ws_is_same_object (u->user, user);
       u = u->next)
    ;

  if (u != NULL)
    {
      *index = u->index;
    }
  else
    {
      /*
       * User wasn't in the cache, add it and extend the existing class
       * caches.  First do a little sanity check just to make sure this
       * is a user object.
       */
      if (check_it)
	{
	  class_mop = sm_get_class (user);
	  if (class_mop == NULL)
	    {
	      assert (er_errid () != NO_ERROR);
	      return er_errid ();
	    }
	  else if (class_mop != Au_user_class)
	    {
	      error = ER_AU_CORRUPTED;	/* need a better error */
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
	      return er_errid ();
	    }
	}

      new_user_cache = (AU_USER_CACHE *) malloc (sizeof (AU_USER_CACHE));
      if (new_user_cache != NULL)
	{
	  if ((error = au_extend_class_caches (index)))
	    {
	      free_and_init (new_user_cache);
	    }
	  else
	    {
	      new_user_cache->next = Au_user_cache;
	      Au_user_cache = new_user_cache;
	      new_user_cache->user = user;
	      new_user_cache->index = *index;
	    }
	}
    }

  return error;
}

/*
 * free_user_cache - Frees a user cache. Make sure to clear the MOP pointer.
 *   returns: none
 *   u(in): user cache
 */
static void
free_user_cache (AU_USER_CACHE * u)
{
  if (u != NULL)
    {
      u->user = NULL;		/* clear GC roots */
      free_and_init (u);
    }
}


/*
 * reset_cache_for_user_and_class - This is called whenever a grant or revoke
 *                                  operation is performed. It resets the
 *                                  caches only for a particular
 *                                  user/class pair
 *   return : none
 *   class(in): class structure
 *
 * Note: This was originally written so that only the authorization
 *       cache for the given user was cleared.  This does not work however
 *       if we're changing authorization for a group and there are members
 *       of that group already cached.
 *       We could be smart and try to invalidate the caches of all
 *       members of this user but instead just go ahead and invalidate
 *       everyone's cache for this class.  This isn't optimal but doesn't really
 *       matter that much.  grant/revoke don't happen very often.
 */
static void
reset_cache_for_user_and_class (SM_CLASS * sm_class)
{
  AU_USER_CACHE *u;
  AU_CLASS_CACHE *c;

  for (c = Au_class_caches; c != NULL && c->class_ != sm_class; c = c->next);
  if (c != NULL)
    {
      /*
       * invalide every user's cache for this class_, could be more
       * selective and do only the given user and its members
       */
      for (u = Au_user_cache; u != NULL; u = u->next)
	{
	  c->data[u->index] = AU_CACHE_INVALID;
	}
    }
}

/*
 * au_reset_authorization_caches - This is called by ws_clear_all_hints()
 *                                 and ws_abort_mops() on transaction
 *                                 boundaries.
 *   return: none
 *
 * Note: We reset all the authorization caches at this point.
 *       This sets all of the authorization entries in a cache to be invalid.
 *       Normally this is done when the authorization for this
 *       class changes in some way.  The next time the cache is used, it
 *       will force the recomputation of the authorization bits.
 *       We should try to be smarter and flush the caches only when we know
 *       that the authorization catalogs have changed in some way.
 */

void
au_reset_authorization_caches (void)
{
  AU_CLASS_CACHE *c;
  int i;

  for (c = Au_class_caches; c != NULL; c = c->next)
    {
      for (i = 0; i < Au_cache_depth; i++)
	{
	  c->data[i] = AU_CACHE_INVALID;
	}
    }
}

/*
 * remove_user_cache_reference - This is called when a user object is deleted.
 *   return: none
 *   user(in): user object
 *
 * Note: If there is an authorization cache entry for this user, we NULL
 *       the user pointer so it will no longer be used.  We could to in
 *       and restructure all the caches to remove the deleted user but user
 *       deletion isn't that common.  Just leave an unused entry in the
 *       cache array.
 */
static void
remove_user_cache_references (MOP user)
{
  AU_USER_CACHE *u;

  for (u = Au_user_cache; u != NULL; u = u->next)
    {
      if (ws_is_same_object (u->user, user))
	{
	  u->user = NULL;
	}
    }
}

/*
 * init_caches - Called during au_init().  Initialize all of the cache
 *               related global variables.
 *   return: none
 */
static void
init_caches (void)
{
  Au_user_cache = NULL;
  Au_class_caches = NULL;
  Au_cache_depth = 0;
  Au_cache_max = 0;
  Au_cache_increment = 4;
  Au_cache_index = -1;
}

/*
 * flush_caches - Called during au_final(). Free the authorization cache
 *                structures and initialize the global variables
 *                to their default state.
 *   return : none
 */
static void
flush_caches (void)
{
  AU_USER_CACHE *u, *nextu;
  AU_CLASS_CACHE *c, *nextc;

  for (c = Au_class_caches, nextc = NULL; c != NULL; c = nextc)
    {
      nextc = c->next;
      c->class_->auth_cache = NULL;
      au_free_class_cache (c);
    }
  for (u = Au_user_cache, nextu = NULL; u != NULL; u = nextu)
    {
      nextu = u->next;
      free_user_cache (u);
    }

  /* clear the associated globals */
  init_caches ();
}


/*
 * COMPARISON
 */

#if defined(ENABLE_UNUSED_FUNCTION)
/*
 * toupper_string -  This is used to add a user or compare two user names.
 *   return: convert a string to upper case
 *   name1: user name
 *   name2: user name
 *
 * Note: User names are stored in upper case.
 *       This is split into a separate function in case we need to make
 *       modifications to this for Internationalization.
 */
char *
toupper_string (const char *name1, char *name2)
{
  char *buffer, *ptr;

  buffer = (char *) malloc (strlen (name1) * 2 + 1);
  if (buffer == NULL)
    {
      return NULL;
    }

  intl_mbs_upper (name1, buffer);
  ptr = buffer;
  while (*ptr != '\0')
    {
      *name2++ = *ptr++;
    }
  *name2 = '\0';

  free (buffer);
  return name2;
}
#endif

/*
 * USER/GROUP ACCESS
 */

/*
 * au_find_user - Find a user object by name.
 *   return: user object
 *   user_name(in): name
 *
 * Note: The db_root class used to have a users attribute which was a set
 *       containing the object-id for all users.
 *       The users attribute has been eliminated for performance reasons.
 *       A query is now used to find the user.  Since the user name is not
 *       case insensitive, it is set to upper case in the query.  This forces
 *       user names to be set to upper case when users are added.
 */
MOP
au_find_user (const char *user_name)
{
  MOP obj, user = NULL;
  int save;
  char *query;
  DB_QUERY_RESULT *query_result;
  DB_QUERY_ERROR query_error;
  int error = NO_ERROR;
  DB_VALUE user_val;
  const char *qp1 =
    "select [%s] from [%s] where [name] = '%s' using index none";
  MOP user_class;
  char *upper_case_name;
  size_t upper_case_name_size;
  DB_VALUE user_name_string;

  if (user_name == NULL)
    {
      return NULL;
    }

  /* disable checking of internal authorization object access */
  AU_DISABLE (save);

  user = NULL;

  upper_case_name_size = intl_identifier_upper_string_size (user_name);
  upper_case_name = (char *) malloc (upper_case_name_size + 1);
  if (upper_case_name == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
	      upper_case_name_size);
      return NULL;
    }
  intl_identifier_upper (user_name, upper_case_name);

  /*
   * first try to find the user id by index. This is faster than
   * a query, and will not get blocked out as a server request
   * if the query processing resources are all used up at the moment.
   * This is primarily of importance during logging in.
   */
  user_class = db_find_class ("db_user");
  if (user_class)
    {
      db_make_string (&user_name_string, upper_case_name);
      user = obj_find_unique (user_class, "name", &user_name_string,
			      AU_FETCH_READ);
    }
  error = er_errid ();

  if (error != NO_ERROR)
    {
      if (error == ER_OBJ_OBJECT_NOT_FOUND)
	{
	  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_AU_INVALID_USER, 1,
		  user_name);
	}
      goto exit;
    }

  if (error == NO_ERROR && !user)
    {
      /* proceed with the query version of the function */
      query = (char *) malloc (strlen (qp1) +
			       (2 * strlen (AU_USER_CLASS_NAME)) +
			       strlen (upper_case_name) + 1);
      if (query)
	{
	  sprintf (query, qp1, AU_USER_CLASS_NAME, AU_USER_CLASS_NAME,
		   upper_case_name);

	  lang_set_parser_use_client_charset (false);
	  error =
	    db_compile_and_execute_local (query, &query_result, &query_error);
	  lang_set_parser_use_client_charset (true);
	  /* error is row count if not negative. */
	  if (error > 0)
	    {
	      if (db_query_first_tuple (query_result) == DB_CURSOR_SUCCESS)
		{
		  if (db_query_get_tuple_value (query_result, 0, &user_val) ==
		      NO_ERROR)
		    {
		      if (DB_IS_NULL (&user_val))
			{
			  obj = NULL;
			}
		      else
			{
			  obj = db_get_object (&user_val);
			}
		      if (obj)
			{
			  user = obj;
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

exit:
  AU_ENABLE (save);

  if (upper_case_name)
    {
      free_and_init (upper_case_name);
    }
  return (user);
}

/*
 * au_find_user_method - Method interface to au_find_user.
 *   return: none
 *   class(in):
 *   returnval(out):
 *   name(in):
 */
void
au_find_user_method (MOP class_mop, DB_VALUE * returnval, DB_VALUE * name)
{
  MOP user;
  int error = NO_ERROR;

  db_make_null (returnval);
  if (name != NULL && IS_STRING (name)
      && !DB_IS_NULL (name) && db_get_string (name) != NULL)
    {
      user = au_find_user (db_get_string (name));
      if (user != NULL)
	{
	  db_make_object (returnval, user);
	}
    }
  else
    {
      error = ER_AU_INVALID_USER;
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, error, 1, "");
      db_make_error (returnval, error);
    }
}

/*
 * au_make_user -  Create a new user object. Convert the name to upper case
 *                 so that au_find_user can use a query.
 *   return: new user object
 *   name(in): user name
 */
static MOP
au_make_user (const char *name)
{
  MOP uclass, aclass, user, auth;
  DB_VALUE value;
  DB_SET *set;
  char *lname;
  int error;

  user = NULL;
  uclass = sm_find_class (AU_USER_CLASS_NAME);
  if (uclass == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_AU_MISSING_CLASS, 1,
	      AU_USER_CLASS_NAME);
    }
  else
    {
      aclass = sm_find_class (AU_AUTH_CLASS_NAME);
      if (aclass == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_AU_MISSING_CLASS, 1,
		  AU_AUTH_CLASS_NAME);
	}
      else
	{
	  int name_size;

	  user = obj_create (uclass);
	  name_size = intl_identifier_upper_string_size (name);
	  lname = (char *) malloc (name_size + 1);
	  if (lname)
	    {
	      intl_identifier_upper (name, lname);
	      db_make_string (&value, lname);
	      error = obj_set (user, "name", &value);
	      free_and_init (lname);
	      if (error != NO_ERROR)
		{
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			  ER_AU_ACCESS_ERROR, 2, AU_USER_CLASS_NAME, "name");
		  obj_delete (user);
		  user = NULL;
		}
	      else
		{
		  /* flattened group list */
		  set = set_create_basic ();
		  if (set == NULL)
		    {
		      goto memory_error;
		    }
		  db_make_set (&value, set);
		  obj_set (user, "groups", &value);
		  set_free (set);

		  /* direct group list */
		  set = set_create_basic ();
		  if (set == NULL)
		    {
		      goto memory_error;
		    }
		  db_make_set (&value, set);
		  obj_set (user, "direct_groups", &value);
		  set_free (set);

		  /* authorization object */
		  auth = obj_create (aclass);
		  if (auth == NULL)
		    {
		      goto memory_error;
		    }
		  db_make_object (&value, user);
		  /* back pointer to user object */
		  obj_set (auth, "owner", &value);
		  set = set_create_sequence (0);
		  if (set == NULL)
		    {
		      goto memory_error;
		    }
		  db_make_sequence (&value, set);
		  obj_set (auth, "grants", &value);
		  set_free (set);

		  db_make_object (&value, auth);
		  obj_set (user, "authorization", &value);
		}
	    }
	  else
	    {
	      goto memory_error;
	    }
	}
    }
  return (user);

memory_error:
  if (user != NULL)
    {
      obj_delete (user);
    }
  return NULL;
}

/*
 * au_set_new_auth -
 *   return: error code
 *   au_obj(in):
 *   grantor(in):
 *   user(in):
 *   class(in):
 *   auth_type(in):
 *   grant_option(in):
 */
static int
au_set_new_auth (MOP au_obj, MOP grantor, MOP user, MOP class_mop,
		 DB_AUTH auth_type, bool grant_option)
{
  int error = NO_ERROR;
  MOP au_class, db_class = NULL, db_class_inst = NULL;
  DB_VALUE value, class_name_val;
  DB_AUTH type;
  int i;
  const char *type_set[] = { "SELECT", "INSERT", "UPDATE", "DELETE",
    "ALTER", "INDEX", "EXECUTE"
  };

  if (au_obj == NULL)
    {
      au_class = sm_find_class (CT_CLASSAUTH_NAME);
      if (au_class == NULL)
	{
	  error = ER_AU_MISSING_CLASS;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1,
		  CT_CLASSAUTH_NAME);
	  return error;
	}
      au_obj = db_create_internal (au_class);
      if (au_obj == NULL)
	{
	  assert (er_errid () != NO_ERROR);
	  return er_errid ();
	}
    }

  db_make_object (&value, grantor);
  obj_set (au_obj, "grantor", &value);

  db_make_object (&value, user);
  obj_set (au_obj, "grantee", &value);

  db_class = sm_find_class (CT_CLASS_NAME);
  if (db_class == NULL)
    {
      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }

  db_make_string (&class_name_val, sm_class_name (class_mop));
  db_class_inst = obj_find_unique (db_class, "class_name", &class_name_val,
				   AU_FETCH_READ);
  if (db_class_inst == NULL)
    {
      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }

  db_make_object (&value, db_class_inst);
  obj_set (au_obj, "class_of", &value);

  for (type = DB_AUTH_SELECT, i = 0; type != auth_type;
       type = (DB_AUTH) (type << 1), i++)
    {
      ;
    }

  db_make_varchar (&value, 7, (char *) type_set[i], strlen (type_set[i]),
		   LANG_SYS_CODESET, LANG_SYS_COLLATION);
  obj_set (au_obj, "auth_type", &value);

  db_make_int (&value, (int) grant_option);
  obj_set (au_obj, "is_grantable", &value);

  return NO_ERROR;
}

/*
 * au_get_new_auth -
 *   return:
 *   grantor(in):
 *   user(in):
 *   class_mop(in):
 *   auth_type(in):
 */
static MOP
au_get_new_auth (MOP grantor, MOP user, MOP class_mop, DB_AUTH auth_type)
{
  MOP au_class, au_obj, db_class_inst;
  MOP ret_obj = NULL;
  DB_OBJLIST *list, *mop;
  DB_AUTH type;
  DB_VALUE value, inst_value;
  int i;
  const char *type_set[] = { "SELECT", "INSERT", "UPDATE", "DELETE",
    "ALTER", "INDEX", "EXECUTE"
  };
  char *tmp;

  au_class = sm_find_class (CT_CLASSAUTH_NAME);
  if (au_class == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_AU_NO_AUTHORIZATION, 0);
      return (ret_obj);
    }

  list = sm_fetch_all_objects (au_class, DB_FETCH_CLREAD_INSTWRITE);
  if (list == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_AU_NO_AUTHORIZATION, 0);
      return (ret_obj);
    }

  for (mop = list; mop != NULL; mop = mop->next)
    {
      au_obj = mop->op;
      if ((obj_get (au_obj, "grantor", &value) != NO_ERROR)
	  || (db_get_object (&value) != grantor))
	{
	  continue;
	}

      if ((obj_get (au_obj, "grantee", &value) != NO_ERROR)
	  || !ws_is_same_object (db_get_object (&value), user))
	{
	  continue;
	}

      if ((obj_get (au_obj, "class_of", &inst_value) != NO_ERROR)
	  || ((db_class_inst = db_get_object (&inst_value)) == NULL)
	  || (obj_get (db_class_inst, "class_of", &value) != NO_ERROR)
	  || (db_get_object (&value) != class_mop))
	{
	  continue;
	}

      for (type = DB_AUTH_SELECT, i = 0; type != auth_type;
	   type = (DB_AUTH) (type << 1), i++)
	{
	  ;
	}

      if (obj_get (au_obj, "auth_type", &value) != NO_ERROR)
	{
	  continue;
	}

      tmp = db_get_string (&value);
      if (tmp && strcmp (tmp, type_set[i]) == 0)
	{
	  ret_obj = au_obj;
	  pr_clear_value (&value);
	  break;
	}
      pr_clear_value (&value);
    }

  ml_ext_free (list);
  return (ret_obj);
}

/*
 * au_insert_new_auth -
 *   return: error code
 *   grantor(in):
 *   user(in):
 *   class_mop(in):
 *   auth_type(in):
 *   grant_option(in):
 */
static int
au_insert_new_auth (MOP grantor, MOP user, MOP class_mop,
		    DB_AUTH auth_type, int grant_option)
{
  int index;
  int error = NO_ERROR;

  for (index = DB_AUTH_EXECUTE; index; index >>= 1)
    {
      if (auth_type & index)
	{
	  error = au_set_new_auth (NULL, grantor, user, class_mop,
				   (DB_AUTH) index,
				   ((grant_option & index) ? true : false));
	  if (error != NO_ERROR)
	    {
	      break;
	    }
	}
    }

  return error;
}

/*
 * au_update_new_auth -
 *   return: error code
 *   grantor(in):
 *   user(in):
 *   class_mop(in):
 *   auth_type(in):
 *   grant_option(in):
 */
static int
au_update_new_auth (MOP grantor, MOP user, MOP class_mop,
		    DB_AUTH auth_type, int grant_option)
{
  MOP au_obj;
  int index;
  int error = NO_ERROR;

  for (index = DB_AUTH_EXECUTE; index; index >>= 1)
    {
      if (auth_type & index)
	{
	  au_obj = au_get_new_auth (grantor, user, class_mop,
				    (DB_AUTH) index);
	  if (au_obj == NULL)
	    {
	      assert (er_errid () != NO_ERROR);
	      return er_errid ();
	    }

	  error = obj_lock (au_obj, 1);
	  if (error != NO_ERROR)
	    {
	      return error;
	    }

	  error = au_set_new_auth (au_obj, grantor, user, class_mop,
				   (DB_AUTH) index,
				   ((grant_option & index) ? true : false));
	  if (error != NO_ERROR)
	    {
	      return error;
	    }
	}
    }

  return error;
}

/*
 * au_delete_new_auth -
 *   return: error code
 *   grantor(in):
 *   user(in):
 *   class_mop(in):
 *   auth_type(in):
 */
static int
au_delete_new_auth (MOP grantor, MOP user, MOP class_mop, DB_AUTH auth_type)
{
  MOP au_obj;
  int index;
  int error = NO_ERROR;

  for (index = DB_AUTH_EXECUTE; index; index >>= 1)
    {
      if (auth_type & index)
	{
	  au_obj = au_get_new_auth (grantor, user, class_mop,
				    (DB_AUTH) index);
	  if (au_obj == NULL)
	    {
	      assert (er_errid () != NO_ERROR);
	      return er_errid ();
	    }

	  error = obj_lock (au_obj, 1);
	  if (error != NO_ERROR)
	    {
	      return error;
	    }

	  error = obj_delete (au_obj);
	  if (error != NO_ERROR)
	    {
	      return error;
	    }
	}
    }

  return error;
}

/*
 * au_propagate_del_new_auth -
 *   return: error code
 *   glist(in):
 *   mask(in):
 */
static int
au_propagate_del_new_auth (AU_GRANT * glist, DB_AUTH mask)
{
  AU_GRANT *g;
  DB_SET *grants;
  DB_VALUE class_, type;
  int error = NO_ERROR;

  for (g = glist; g != NULL; g = g->next)
    {
      if (!g->legal)
	{
	  error = get_grants (g->auth_object, &grants, 0);
	  if (error != NO_ERROR)
	    {
	      break;
	    }

	  error = set_get_element (grants, GRANT_ENTRY_CLASS (g->grant_index),
				   &class_);
	  if (error != NO_ERROR)
	    {
	      break;
	    }

	  error = set_get_element (grants, GRANT_ENTRY_CACHE (g->grant_index),
				   &type);
	  if (error != NO_ERROR)
	    {
	      break;
	    }

	  error = au_delete_new_auth (g->grantor, g->user,
				      db_get_object (&class_),
				      (DB_AUTH) (db_get_int (&type) & ~mask));
	  if (error != NO_ERROR)
	    {
	      break;
	    }
	}
    }

  return error;
}

/*
 * au_force_write_new_auth -
 *   return: error code
 */
int
au_force_write_new_auth (void)
{
  DB_OBJLIST *list, *mop;
  MOP au_class, au_obj;
  DB_VALUE grants_val;
  DB_SET *grants;
  DB_VALUE grantor_val, grantee_val, class_val, auth_val;
  MOP grantor, grantee, class_;
  DB_AUTH auth;
  int gindex, gsize;
  int save;
  int error = NO_ERROR;

  list = NULL;

  AU_DISABLE (save);

  au_class = sm_find_class (AU_AUTH_CLASS_NAME);
  if (au_class == NULL)
    {
      error = ER_AU_NO_AUTHORIZATION;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
      goto end;
    }

  list = sm_fetch_all_objects (au_class, DB_FETCH_CLREAD_INSTREAD);
  if (list == NULL)
    {
      error = ER_AU_NO_AUTHORIZATION;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
      goto end;
    }

  for (mop = list; mop != NULL; mop = mop->next)
    {
      au_obj = mop->op;

      error = obj_get (au_obj, "owner", &grantee_val);
      if (error != NO_ERROR)
	{
	  goto end;
	}
      grantee = db_get_object (&grantee_val);

      error = obj_get (au_obj, "grants", &grants_val);
      if (error != NO_ERROR)
	{
	  goto end;
	}
      grants = db_get_set (&grants_val);

      gsize = set_size (grants);
      for (gindex = 0; gindex < gsize; gindex += GRANT_ENTRY_LENGTH)
	{
	  error = set_get_element (grants, GRANT_ENTRY_CLASS (gindex),
				   &class_val);
	  if (error != NO_ERROR)
	    {
	      goto end;
	    }
	  class_ = db_get_object (&class_val);

	  error = set_get_element (grants, GRANT_ENTRY_SOURCE (gindex),
				   &grantor_val);
	  if (error != NO_ERROR)
	    {
	      goto end;
	    }
	  grantor = db_get_object (&grantor_val);

	  error = set_get_element (grants, GRANT_ENTRY_CACHE (gindex),
				   &auth_val);
	  if (error != NO_ERROR)
	    {
	      goto end;
	    }
	  auth = (DB_AUTH) db_get_int (&auth_val);

	  error = au_insert_new_auth (grantor, grantee, class_,
				      (DB_AUTH) (auth & AU_TYPE_MASK),
				      (auth & AU_GRANT_MASK));
	  if (error != NO_ERROR)
	    {
	      goto end;
	    }
	}
    }

end:
  if (list)
    {
      ml_ext_free (list);
    }

  AU_ENABLE (save);

  return error;
}

/*
 * check_user_name
 *   return: error code
 *   name(in): proposed user name
 *
 * Note: This is made void for ansi compatibility. It previously insured
 *       that identifiers which were accepted could be parsed in the
 *       language interface.
 *
 *       ANSI allows any character in an identifier. It also allows reserved
 *       words. In order to parse identifiers with non-alpha characters
 *       or that are reserved words, an escape syntax is definned with double
 *       quotes, "FROM", for example.
 */
static int
check_user_name (const char *name)
{
  return NO_ERROR;
}

/*
 * au_is_dba_group_member -  Determines if a given user is the DBA/a member
 *                           of the DBA group, or not
 *   return: true or false
 *   user(in): user object
 */
bool
au_is_dba_group_member (MOP user)
{
  DB_SET *groups;
  DB_VALUE value;
  bool is_member = false;

  if (!user)
    {
      return false;		/* avoid gratuitous er_set later */
    }

  if (ws_is_same_object (user, Au_dba_user))
    {
      return true;
    }

  if (au_get_set (user, "groups", &groups) == NO_ERROR)
    {
      db_make_object (&value, Au_dba_user);
      is_member = set_ismember (groups, &value);
      set_free (groups);
    }

  return is_member;
}

/*
 * au_add_user -  Add a user object if one does not already exist.
 *   return: new or existing user object
 *   name(in): user name
 *   exists(out): flag set if user already existed
 *
 * Note: If one already exists, return it and set the flag.
 *       The db_root class used to have a user attribute which was a set
 *       containing the object-id for all users.  The users attribute has been
 *       eliminated for performance reasons.
 *
 */
MOP
au_add_user (const char *name, int *exists)
{
  MOP user;
  DB_VALUE value;
  int save;

  user = NULL;
  if (Au_dba_user != NULL && !au_is_dba_group_member (Au_user))
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_AU_DBA_ONLY, 1,
	      "add_user");
    }
  else if (!check_user_name (name))
    {
      AU_DISABLE (save);
      user = NULL;
      if (exists != NULL)
	{
	  *exists = 0;
	}
      if (name == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_AU_INVALID_USER_NAME,
		  1, "");
	}
      else
	{
	  user = au_find_user (name);
	  if (user != NULL)
	    {
	      if (exists != NULL)
		{
		  *exists = 1;
		}
	    }
	  else
	    {
	      if (er_errid () != ER_AU_INVALID_USER)
		{
		  AU_ENABLE (save);
		  return NULL;
		}
	      user = au_make_user (name);
	      if (user != NULL)
		{
		  db_make_object (&value, user);
		  if (Au_public_user != NULL)
		    {
		      /*
		       * every user is a member of the PUBLIC group,
		       * must make sure that the exported routines can't
		       * be used to violate this internal connection
		       */
		      if (au_add_member_internal (Au_public_user, user, 1) !=
			  NO_ERROR)
			{
			  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE,
				  ER_AU_CANT_ADD_MEMBER, 2, name, "PUBLIC");
			}
		    }

		  /*
		   * do we want to do this ?? - logically it is ok but this
		   * means we can't have DBA members since this would
		   * cause user hierarchy cycles.
		   */
#if 0
		  if (Au_dba_user != NULL)
		    {
		      if (au_get_set (Au_dba_user, "groups", &dba_groups) ==
			  NO_ERROR)
			{
			  db_make_object (&value, user);
			  if (!set_ismember (dba_groups, &value))
			    {
			      db_set_add (dba_groups, &value);
			    }
			  set_free (dba_groups);
			}
		    }
#endif /* 0 */
		}
	    }
	}
      AU_ENABLE (save);
    }
  return (user);
}

/*
 * au_add_user_method
 *   return: none
 *   class(in): class object
 *   returnval(out): return value of this method
 *   name(in):
 *   password(in):
 */
void
au_add_user_method (MOP class_mop, DB_VALUE * returnval,
		    DB_VALUE * name, DB_VALUE * password)
{
  int error;
  int exists;
  MOP user;
  char *tmp;

  if (name != NULL && IS_STRING (name)
      && !DB_IS_NULL (name) && ((tmp = db_get_string (name)) != NULL))
    {
      if (intl_identifier_upper_string_size (tmp) >= DB_MAX_USER_LENGTH)
	{
	  error = ER_USER_NAME_TOO_LONG;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
	  db_make_error (returnval, error);
	  return;
	}
      /*
       * although au_set_password will check this, check it out here before
       * we bother creating the user object
       */
      if (password != NULL && IS_STRING (password) && !DB_IS_NULL (password)
	  && (tmp = db_get_string (password))
	  && strlen (tmp) > AU_MAX_PASSWORD_CHARS)
	{
	  error = ER_AU_PASSWORD_OVERFLOW;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
	  db_make_error (returnval, error);
	}
      else
	{
	  user = au_add_user (db_get_string (name), &exists);
	  if (user == NULL)
	    {
	      /* return the error that was set */
	      db_make_error (returnval, er_errid ());
	    }
	  else if (exists)
	    {
	      error = ER_AU_USER_EXISTS;
	      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, error, 1,
		      db_get_string (name));
	      db_make_error (returnval, error);
	    }
	  else
	    {
	      if (password != NULL && IS_STRING (password)
		  && !DB_IS_NULL (password))
		{
		  error = au_set_password (user, db_get_string (password));
		  if (error != NO_ERROR)
		    {
		      db_make_error (returnval, error);
		    }
		  else
		    {
		      db_make_object (returnval, user);
		    }
		}
	      else
		{
		  db_make_object (returnval, user);
		}
	    }
	}
    }
  else
    {
      error = ER_AU_INVALID_USER;
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, error, 1, "");
      db_make_error (returnval, error);
    }
}

/*
 * Password Encoding
 */

/*
 * Password encoding is a bit kludgey to support older databases where the
 * password was stored in an unencoded format.  Since we don't want
 * to invalidate existing databases unless absolutely necessary, we
 * need a way to recognize if the password in the database is encoded or not.
 *
 * The kludge is to store encoded passwords with a special prefix character
 * that could not normally be typed as part of a password.  This will
 * be the binary char \001 or Control-A.  The prefix could be used
 * in the future to identify other encoding schemes in case we find
 * a better way to store passwords.
 *
 * If the password string has this prefix character, we can assume that it
 * has been encoded, otherwise it is assumed to be an older unencoded password.
 *
 */

/*
 * encrypt_password -  Encrypts a password string using DES
 *   return: none
 *   pass(in): string to encrypt
 *   add_prefix(in): non-zero to add the prefix char
 *   dest(out): destination buffer
 */
static void
encrypt_password (const char *pass, int add_prefix, char *dest)
{
  if (pass == NULL)
    {
      strcpy (dest, "");
    }
  else
    {
      crypt_seed (PASSWORD_ENCRYPTION_SEED);
      if (!add_prefix)
	{
	  crypt_encrypt_printable (pass, dest, AU_MAX_PASSWORD_BUF);
	}
      else
	{
	  crypt_encrypt_printable (pass, dest + 1, AU_MAX_PASSWORD_BUF);
	  dest[0] = ENCODE_PREFIX_DES;
	}
    }
}

/*
 * encrypt_password_sha1 -  hashing a password string using SHA1
 *   return: none
 *   pass(in): string to encrypt
 *   add_prefix(in): non-zero to add the prefix char
 *   dest(out): destination buffer
 */
static void
encrypt_password_sha1 (const char *pass, int add_prefix, char *dest)
{
  if (pass == NULL)
    {
      strcpy (dest, "");
    }
  else
    {
      if (!add_prefix)
	{
	  crypt_encrypt_sha1_printable (pass, dest, AU_MAX_PASSWORD_BUF);
	}
      else
	{
	  crypt_encrypt_sha1_printable (pass, dest + 1, AU_MAX_PASSWORD_BUF);
	  dest[0] = ENCODE_PREFIX_SHA1;
	}
    }
}

/*
 * au_user_name_dup -  Returns the duplicated string of the name of the current
 *                     user. The string must be freed after use.
 *   return: user name (strdup)
 */
char *
au_user_name_dup (void)
{
  return strdup (Au_user_name);
}

/* mangle the name so it isn't so obvious in nm */
#define unencrypt_password              io_relseek

/*
 * really unencrypt_password
 * io_relseek - Decode encrypted password
 *   return: error code
 *   pass(in): encrypted password
 *   has_prefix(in):
 *   dest(out): destination buffer
 */
static int
io_relseek (const char *pass, int has_prefix, char *dest)
{
  int error = NO_ERROR;
  char buf[AU_MAX_PASSWORD_BUF];
  int len;

  if (pass == NULL || !strlen (pass))
    {
      strcpy (dest, "");
    }
  else
    {
      crypt_seed (PASSWORD_ENCRYPTION_SEED);
      /*
       * Make sure the destination buffer is larger than actually required,
       * the decryption stuff is sensitive about this. Basically for the
       * scrambled strings, the destination buffer has to be the actual
       * length rounded up to 8 plus another 8.
       */
      if (has_prefix)
	{
	  len = crypt_decrypt_printable (pass + 1, buf, AU_MAX_PASSWORD_BUF);
	}
      else
	{
	  len = crypt_decrypt_printable (pass, buf, AU_MAX_PASSWORD_BUF);
	}
      if (len != -1 && strlen (buf) <= AU_MAX_PASSWORD_CHARS)
	{
	  strcpy (dest, buf);
	}
      else
	{
	  error = ER_AU_CORRUPTED;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
	}
    }
  return error;
}

/*
 * match_password -  This compares two passwords to see if they match.
 *   return: non-zero if the passwords match
 *   user(in): user supplied password
 *   database(in): stored database password
 *
 * Note: Either the user or database password can be encrypted or unencrypted.
 *       The database password will only be unencrypted if this is a very
 *       old database.  The user password will be unencrypted if we're logging
 *       in to an active session.
 */
static bool
match_password (const char *user, const char *database)
{
  char buf1[AU_MAX_PASSWORD_BUF + 4];
  char buf2[AU_MAX_PASSWORD_BUF + 4];

  if (user == NULL || database == NULL)
    {
      return false;
    }

  /* get both passwords into an encrypted format */
  /* if database's password was encrypted with DES,
   * then, user's password should be encrypted with DES,
   */
  if (IS_ENCODED_DES (database))
    {
      /* DB: DES */
      strcpy (buf2, database);
      if (IS_ENCODED_DES (user) || IS_ENCODED_SHA1 (user))
	{
	  /* USER : DES */
	  strcpy (buf1, Au_user_password_des_oldstyle);
	}
      else
	{
	  /* USER : PLAINTEXT -> DES */
	  encrypt_password (user, 1, buf1);
	}
    }
  else if (IS_ENCODED_SHA1 (database))
    {
      /* DB: SHA1 */
      strcpy (buf2, database);
      if (IS_ENCODED_DES (user) || IS_ENCODED_SHA1 (user))
	{
	  /* USER:SHA1 */
	  strcpy (buf1, Au_user_password_sha1);
	}
      else
	{
	  /* USER:PLAINTEXT -> SHA1 */
	  encrypt_password_sha1 (user, 1, buf1);
	}
    }
  else
    {
      /* DB:PLAINTEXT -> SHA1 */
      encrypt_password_sha1 (database, 1, buf2);
      if (IS_ENCODED_DES (user) || IS_ENCODED_SHA1 (user))
	{
	  /* USER : SHA1 */
	  strcpy (buf1, Au_user_password_sha1);
	}
      else
	{
	  /* USER : PLAINTEXT -> SHA1 */
	  encrypt_password_sha1 (user, 1, buf1);
	}
    }

  return strcmp (buf1, buf2) == 0;
}

/*
 * au_set_password_internal -  Set the password string for a user.
 *                             This should be using encrypted strings.
 *   return:error code
 *   user(in):  user object
 *   password(in): new password
 *   encode(in): flag to enable encryption of the string in the database
 *   encrypt_prefix(in): If encode flag is 0, then we assume that the
 *                      given password have been encrypted. So, All I have
 *                      to do is add prefix(DES or SHA1) to given password.
 *                       If encode flag is 1, then we should encrypt
 *                      password with sha1 and add prefix (SHA1) to it.
 *                      So, I don't care what encrypt_prefix value is.
 */
static int
au_set_password_internal (MOP user, const char *password, int encode,
			  char encrypt_prefix)
{
  int error = NO_ERROR;
  DB_VALUE value;
  MOP pass, pclass;
  int save, len;
  char pbuf[AU_MAX_PASSWORD_BUF + 4];

  AU_DISABLE (save);
  if (!ws_is_same_object (Au_user, user) && !au_is_dba_group_member (Au_user))
    {
      error = ER_AU_UPDATE_FAILURE;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
    }
  else
    {
      /* convert empty password strings to NULL passwords */
      if (password != NULL)
	{
	  len = strlen (password);
	  if (len == 0)
	    password = NULL;
	  /*
	   * check for large passwords, only do this
	   * if the encode flag is on !
	   */
	  else if (len > AU_MAX_PASSWORD_CHARS && encode)
	    {
	      error = ER_AU_PASSWORD_OVERFLOW;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
	    }
	}
      if (error == NO_ERROR)
	{
	  if ((error = obj_get (user, "password", &value)) == NO_ERROR)
	    {
	      if (DB_IS_NULL (&value))
		{
		  pass = NULL;
		}
	      else
		{
		  pass = db_get_object (&value);
		}
	      if (pass == NULL)
		{
		  pclass = sm_find_class (AU_PASSWORD_CLASS_NAME);
		  if (pclass != NULL)
		    {
		      pass = obj_create (pclass);
		      if (pass != NULL)
			{
			  db_make_object (&value, pass);
			  obj_set (user, "password", &value);
			}
		      else
			{
			  assert (er_errid () != NO_ERROR);
			  error = er_errid ();
			}
		    }
		  else
		    {
		      assert (er_errid () != NO_ERROR);
		      error = er_errid ();
		    }
		}

	      if (pass != NULL)
		{
		  if (encode && password != NULL)
		    {
		      encrypt_password_sha1 (password, 1, pbuf);
		      db_make_string (&value, pbuf);
		      error = obj_set (pass, "password", &value);
		    }
		  else
		    {
		      /*
		       * always add the prefix, the unload process strips it out
		       * so the password can be read by the csql interpreter
		       */
		      if (password == NULL)
			{
			  db_make_null (&value);
			}
		      else
			{
			  strcpy (pbuf + 1, password);
			  pbuf[0] = encrypt_prefix;
			  db_make_string (&value, pbuf);
			}
		      error = obj_set (pass, "password", &value);
		    }
		}
	    }
	}
    }
  AU_ENABLE (save);
  return (error);
}

/*
 * au_set_password -  Set the password string for a user.
 *   return: error code
 *   user(in): user object
 *   password(in): new password
 */
int
au_set_password (MOP user, const char *password)
{
  return (au_set_password_internal
	  (user, password, 1, ENCODE_PREFIX_DEFAULT));
}

/*
 * au_set_password_method -  Method interface for au_set_password.
 *   return: none
 *   user(in): user object
 *   returnval(out): return value of this method
 *   password(in): new password
 */
void
au_set_password_method (MOP user, DB_VALUE * returnval, DB_VALUE * password)
{
  int error;
  const char *string = NULL;

  db_make_null (returnval);
  if (password != NULL)
    {
      if (IS_STRING (password) && !DB_IS_NULL (password))
	{
	  string = db_get_string (password);
	}

      error = au_set_password (user, string);
      if (error != NO_ERROR)
	{
	  db_make_error (returnval, error);
	}
    }
  else
    {
      error = ER_AU_INVALID_PASSWORD;
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, error, 0);
      db_make_error (returnval, error);
    }
}

/*
 * au_set_password_encoded_method - Method interface for setting
 *                                  encoded passwords.
 *   return: none
 *   user(in): user object
 *   returnval(out): return value of this object
 *   password(in): new password
 *
 * Note: We don't check for the 8 character limit here because this is intended
 *       to be used only by the schema generated by unloaddb.  For this
 *       application, the password length was validated when it was first
 *       created.
 */
void
au_set_password_encoded_method (MOP user, DB_VALUE * returnval,
				DB_VALUE * password)
{
  int error;
  const char *string = NULL;

  db_make_null (returnval);
  if (password != NULL)
    {
      if (IS_STRING (password))
	{
	  if (DB_IS_NULL (password))
	    {
	      string = NULL;
	    }
	  else
	    {
	      string = db_get_string (password);
	    }
	}

      error = au_set_password_internal (user, string, 0, ENCODE_PREFIX_DES);
      if (error != NO_ERROR)
	{
	  db_make_error (returnval, error);
	}
    }
  else
    {
      error = ER_AU_INVALID_PASSWORD;
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, error, 0);
      db_make_error (returnval, error);
    }
}

/*
 * au_set_password_encoded_sha1_method - Method interface for setting
 *                                      sha1 passwords.
 *   return: none
 *   user(in): user object
 *   returnval(out): return value of this object
 *   password(in): new password
 *
 * Note: We don't check for the 8 character limit here because this is intended
 *       to be used only by the schema generated by unloaddb.  For this
 *       application, the password length was validated when it was first
 *       created.
 */
void
au_set_password_encoded_sha1_method (MOP user, DB_VALUE * returnval,
				     DB_VALUE * password)
{
  int error;
  const char *string = NULL;

  db_make_null (returnval);
  if (password != NULL)
    {
      if (IS_STRING (password))
	{
	  if (DB_IS_NULL (password))
	    {
	      string = NULL;
	    }
	  else
	    {
	      string = db_get_string (password);
	    }
	}

      error = au_set_password_internal (user, string, 0, ENCODE_PREFIX_SHA1);
      if (error != NO_ERROR)
	{
	  db_make_error (returnval, error);
	}
    }
  else
    {
      error = ER_AU_INVALID_PASSWORD;
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, error, 0);
      db_make_error (returnval, error);
    }
}

/*
 * GROUP HIERARCHY MAINTENANCE
 */

/*
 * au_add_direct_groups - Add the group to the new_groups and then add
 *                        the group's groups.
 *   return: error status
 *   new_groups(in):the set to add to
 *   value(in): the group to add
 */
static int
au_add_direct_groups (DB_SET * new_groups, DB_VALUE * value)
{
  int error;
  MOP group;
  DB_SET *direct_groups;
  int gcard, g;
  DB_VALUE gvalue;

  if ((error = db_set_add (new_groups, value)) == NO_ERROR)
    {
      if (DB_IS_NULL (value))
	{
	  group = NULL;
	}
      else
	{
	  group = db_get_object (value);
	}
      if ((error = au_get_set (group, "direct_groups", &direct_groups))
	  == NO_ERROR)
	{
	  gcard = set_cardinality (direct_groups);
	  for (g = 0; g < gcard && !error; g++)
	    {
	      if ((error =
		   set_get_element (direct_groups, g, &gvalue)) == NO_ERROR)
		{
		  error = au_add_direct_groups (new_groups, &gvalue);
		}
	    }
	  set_free (direct_groups);
	}
    }

  return error;
}

/*
 * au_compute_groups - Compute the groups attribute from the direct_groups
 *                     attribute for those users that have a particular
 *                     user/group in their groups attribute.
 *   return: error status
 *   member(in): the new member
 *   name(in): the new member name
 */
static int
au_compute_groups (MOP member, char *name)
{
  int error = NO_ERROR;
  DB_SET *new_groups, *direct_groups;
  DB_VALUE value, gvalue, user_val;
  MOP user;
  int g, gcard;
  DB_SESSION *session;
  DB_VALUE val[3];
  STATEMENT_ID stmt_id;
  DB_QUERY_RESULT *result = (DB_QUERY_RESULT *) 0;
  const char *qstr =
    "select [d] from [db_user] [d] where ? in [d].[groups] or [d].[name] = ?;";

  db_make_object (&val[0], member);
  db_make_string (&val[1], name);

  session = db_open_buffer (qstr);
  if (!session)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
      goto ret;
    }

  db_push_values (session, 2, val);

  stmt_id = db_compile_statement (session);
  if (stmt_id != 1)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
      goto ret;
    }

  error = db_execute_statement_local (session, stmt_id, &result);
  if (error < 0)
    goto ret;

  /* error is row count if not negative. */
  if (error > 0)
    {
      error = NO_ERROR;
      while (db_query_next_tuple (result) == DB_CURSOR_SUCCESS)
	{
	  if ((error = db_query_get_tuple_value (result, 0, &user_val))
	      == NO_ERROR)
	    {
	      if (DB_IS_NULL (&user_val))
		{
		  user = NULL;
		}
	      else
		{
		  user = db_get_object (&user_val);
		}
	      new_groups = set_create_basic ();
	      if (new_groups)
		{
		  if ((error =
		       au_get_set (user, "direct_groups",
				   &direct_groups)) == NO_ERROR)
		    {
		      /* compute closure */
		      gcard = set_cardinality (direct_groups);
		      for (g = 0; g < gcard && !error; g++)
			{
			  if ((error =
			       set_get_element (direct_groups, g,
						&gvalue)) == NO_ERROR)
			    {
			      error =
				au_add_direct_groups (new_groups, &gvalue);
			    }
			}
		      set_free (direct_groups);
		    }
		}
	      else
		{
		  assert (er_errid () != NO_ERROR);
		  error = er_errid ();
		}
	      if (error == NO_ERROR)
		{
		  db_make_set (&value, new_groups);
		  obj_set (user, "groups", &value);
		}
	      if (new_groups)
		{
		  set_free (new_groups);
		}
	    }
	}
    }

ret:
  if (result)
    {
      db_query_end (result);
    }
  if (session)
    {
      db_close_session (session);
    }

  return error;
}

/*
 * au_add_member_internal - Add a member to a group and propagate the member
 *                          to all affected	sub-groups.  If the call is
 *                          for a new user, then no other user can be part of
 *                          this user(group)
 *    return: error status
 *    group(in): group to get new member
 *    member(in): the new member
 *    new_user(in): whether the call is for a new user
 *
 * Note:
 *    the db_user class used to have a groups and a members attribute.  the
 *    members attribute was eliminated as a performance improvement, but the
 *    direct_groups attribute has been added.  both groups and direct_groups
 *    are sets.  the direct_groups attribute indicates which groups the user/
 *    group is an immediate member of.  the groups attribute indicates which
 *    groups the user/group is a member of (immediate or otherwise).  the
 *    group attribute is a flattened set.  when a user/group is added to a
 *    new group, the new group is added to both the direct_groups and groups
 *    attributes for the user/group.  then that change is propagated to other
 *    users/groups.
 *    for example,  if u1 is in g1 and g1 is added to g2, g2 is added to g1's
 *    direct_groups and groups attributes and g2 is also added to u1's groups
 *    attributes.
 */
static int
au_add_member_internal (MOP group, MOP member, int new_user)
{
  int error = NO_ERROR;
  DB_VALUE membervalue, member_name_val, groupvalue;
  DB_SET *group_groups = NULL, *member_groups, *member_direct_groups;
  int save;
  char *member_name;

  AU_DISABLE (save);
  db_make_object (&membervalue, member);
  db_make_object (&groupvalue, group);

  /*
   * Skip some checks and processing for a new user/group because it
   * can't have any members yet.
   */
  if ((!new_user) && (group == member))
    {
      error = ER_AU_MEMBER_CAUSES_CYCLES;
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, error, 0);
    }
  else
    {
      if ((!new_user)
	  && ((error = au_get_set (group, "groups", &group_groups)) !=
	      NO_ERROR))
	{
	  ;
	}
      else
	{
	  if ((!new_user) && (set_ismember (group_groups, &membervalue)))
	    {
	      error = ER_AU_MEMBER_CAUSES_CYCLES;
	      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, error, 0);
	    }
	  else
	    {
	      error = obj_lock (member, 1);
	      if (error == NO_ERROR)
		{
		  error = au_get_set (member, "groups", &member_groups);
		}

	      if (error == NO_ERROR)
		{
		  error = au_get_set (member, "direct_groups",
				      &member_direct_groups);
		  if (error == NO_ERROR)
		    {
		      if (new_user)
			{
			  error = db_set_add (member_groups, &groupvalue);
			  if (error == NO_ERROR)
			    {
			      error = db_set_add (member_direct_groups,
						  &groupvalue);
			    }
			}
		      else
			if (!set_ismember (member_direct_groups,
					   &membervalue))
			{
			  error = db_get (member, "name", &member_name_val);
			  if (error == NO_ERROR)
			    {
			      if (DB_IS_NULL (&member_name_val))
				{
				  member_name = NULL;
				}
			      else
				{
				  member_name =
				    (char *) db_get_string (&member_name_val);
				}

			      error = db_set_add (member_direct_groups,
						  &groupvalue);
			      if (error == NO_ERROR)
				{
				  error = au_compute_groups (member,
							     member_name);
				}
			      db_value_clear (&member_name_val);
			    }
			}
		      set_free (member_direct_groups);
		    }
		  set_free (member_groups);
		}

	      if (!new_user)
		{
		  set_free (group_groups);
		}
	    }
	}
    }
  AU_ENABLE (save);
  return (error);
}

/*
 * au_add_member - Add a member to a group and propagate the member to
 *                 all affected sub-groups.
 *   return: error status
 *   group(in):  group to get new member
 *   member(in): the new member
 */
int
au_add_member (MOP group, MOP member)
{
  return au_add_member_internal (group, member, 0);
}

/*
 * au_add_member_method -  Method interface to au_add_member.
 *   return: none
 *   user(in): user object
 *   returnval(out): return value of this method
 *   memval(in):
 */
void
au_add_member_method (MOP user, DB_VALUE * returnval, DB_VALUE * memval)
{
  int error = NO_ERROR;
  MOP member;

  if (memval != NULL)
    {
      member = NULL;
      if (DB_VALUE_TYPE (memval) == DB_TYPE_OBJECT
	  && !DB_IS_NULL (memval) && db_get_object (memval) != NULL)
	{
	  member = db_get_object (memval);
	}
      else if (IS_STRING (memval) && !DB_IS_NULL (memval)
	       && db_get_string (memval) != NULL)
	{
	  member = au_find_user (db_get_string (memval));
	  if (member == NULL)
	    {
	      assert (er_errid () != NO_ERROR);
	      error = er_errid ();
	      goto error;
	    }
	}

      if (member != NULL)
	{
	  if (ws_is_same_object (user, Au_user)
	      || au_is_dba_group_member (Au_user))
	    {
	      error = au_add_member (user, member);
	    }
	  else
	    {
	      error = ER_AU_NOT_OWNER;
	      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, error, 0);
	    }
	}
      else
	{
	  error = ER_AU_INVALID_USER;
	  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, error, 1, "");
	}
    }
  else
    {
      error = ER_AU_INVALID_USER;
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, error, 1, "");
    }

error:
  if (error == NO_ERROR)
    {
      db_make_null (returnval);
    }
  else
    {
      db_make_error (returnval, error);
    }
}

/*
 * au_drop_member - Remove a member from a group.
 *   return: error code
 *   group(in): group with member to drop
 *   member(in): member to drop
 *
 * Note:
 *
 *    The db_user class used to have a groups and a members attribute.  The
 *    members attribute was eliminated as a performance improvement, but the
 *    direct_groups attribute has been added.  Both groups and direct_groups
 *    are sets.  The direct_groups attribute indicates which groups the user/
 *    group is an immediate member of.  The groups attribute indicates which
 *    groups the user/group is a member of (immediate or otherwise).  The
 *    groups attribute is a flattened set.  When a user/group is dropped from
 *    a group, the group is removed from both the direct_groups and groups
 *    attributes for the user/group.  Then that change is propagated to other
 *    users/groups.
 *    For example,  if U1 is directly in G1 and G1 is directly in G2 and G1
 *    is dropped from G2, G2 is removed from G1's direct_groups and groups
 *    attributes and G2 is also removed from U1's groups attribute.
 */
int
au_drop_member (MOP group, MOP member)
{
  int syserr = NO_ERROR, error = NO_ERROR;
  DB_VALUE groupvalue, member_name_val;
  DB_SET *groups, *member_groups, *member_direct_groups;
  int save;
  char *member_name;

  AU_DISABLE (save);
  db_make_object (&groupvalue, group);

  if ((syserr = au_get_set (member, "groups", &member_groups)) == NO_ERROR)
    {
      if (!set_ismember (member_groups, &groupvalue))
	{
	  error = ER_AU_MEMBER_NOT_FOUND;
	  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, error, 0);
	}
      else if ((error = au_get_set (group, "groups", &groups)) == NO_ERROR)
	{
	  if (set_ismember (groups, &groupvalue))
	    {
	      error = ER_AU_MEMBER_NOT_FOUND;
	      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, error, 0);
	    }
	  else
	    {
	      error = obj_lock (member, 1);
	      if (error == NO_ERROR)
		{
		  error = au_get_set (member, "direct_groups",
				      &member_direct_groups);
		}

	      if (error == NO_ERROR)
		{
		  if ((error = db_get (member, "name", &member_name_val))
		      == NO_ERROR)
		    {
		      if (DB_IS_NULL (&member_name_val))
			{
			  member_name = NULL;
			}
		      else
			{
			  member_name =
			    (char *) db_get_string (&member_name_val);
			}
		      if ((error =
			   db_set_drop (member_direct_groups,
					&groupvalue)) == NO_ERROR)
			{
			  error = au_compute_groups (member, member_name);
			}
		      db_value_clear (&member_name_val);
		    }
		  set_free (member_direct_groups);
		}
	    }
	  set_free (groups);
	}
      set_free (member_groups);
    }
  AU_ENABLE (save);
  return (error);
}

/*
 * au_drop_member_method -  Method interface for au_drop_member.
 *   return: none
 *   user(in): user object
 *   returnval(in): return value of this method
 *   memval(in):
 */
void
au_drop_member_method (MOP user, DB_VALUE * returnval, DB_VALUE * memval)
{
  int error = NO_ERROR;
  MOP member;

  if (memval != NULL)
    {
      member = NULL;
      if (DB_VALUE_TYPE (memval) == DB_TYPE_OBJECT
	  && !DB_IS_NULL (memval) && db_get_object (memval) != NULL)
	{
	  member = db_get_object (memval);
	}
      else if (IS_STRING (memval)
	       && !DB_IS_NULL (memval) && db_get_string (memval) != NULL)
	{
	  member = au_find_user (db_get_string (memval));
	  if (member == NULL)
	    {
	      assert (er_errid () != NO_ERROR);
	      error = er_errid ();
	      goto error;
	    }
	}

      if (member != NULL)
	{
	  if (ws_is_same_object (user, Au_user)
	      || au_is_dba_group_member (Au_user))
	    {
	      error = au_drop_member (user, member);
	    }
	  else
	    {
	      error = ER_AU_NOT_OWNER;
	      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, error, 0);
	    }
	}
      else
	{
	  error = ER_AU_INVALID_USER;
	  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, error, 1, "");
	}
    }
  else
    {
      error = ER_AU_INVALID_USER;
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, error, 1, "");
    }

error:
  if (error == NO_ERROR)
    {
      db_make_null (returnval);
    }
  else
    {
      db_make_error (returnval, error);
    }
}

/*
 * au_drop_user - Drop a user from the database.
 *   return: error code
 *   user(in): user object
 *
 * Note:
 *
 *    This should only be called with DBA privilidges.
 *    The db_user class used to have a groups and a members attribute.  The
 *    members attribute was eliminated as a performance improvement, but the
 *    direct_groups attribute has been added.  Both groups and direct_groups
 *    are sets.  The direct_groups attribute indicates which groups the user/
 *    group is an immediate member of.  The groups attribute indicates which
 *    groups the user/group is a member of (immediate or otherwise).  The
 *    groups attribute is a flattened set.  When a user/group is dropped,
 *    the user/group is removed from both the direct_groups and groups
 *    attributes for all users.  For example,  if U1 is directly in G1 and G1
 *    is directly in G2 and G1 is dropped, G1 & G2 are removed from U1's
 *    groups attribute and G1 is also removed from U1's direct_groups
 *    attribute.
 */
int
au_drop_user (MOP user)
{
  int save;
  DB_SESSION *session = NULL;
  DB_VALUE val[2], user_val, gvalue, value, password_val;
  STATEMENT_ID stmt_id;
  int error = NO_ERROR;
  DB_QUERY_RESULT *result;
  MOP auser, password;
  DB_SET *new_groups, *direct_groups;
  int g, gcard, i;
  DB_VALUE name;
  const char *class_name[] = {
    /*
     * drop user command can be called only by DBA group,
     * so we can use query for _db_class directly
     */
    "_db_class", "db_trigger", "db_serial", NULL
  };
  char query_buf[1024];

  AU_DISABLE (save);

  if (Au_dba_user != NULL && !au_is_dba_group_member (Au_user))
    {
      error = ER_AU_DBA_ONLY;
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, error, 1, "drop_user");
      goto error;
    }

  /* check if user is dba/public or current user */
  if (ws_is_same_object (user, Au_dba_user)
      || ws_is_same_object (user, Au_public_user)
      || ws_is_same_object (user, Au_user))
    {
      db_make_null (&name);
      error = obj_get (user, "name", &name);
      if (error != NO_ERROR)
	{
	  goto error;
	}

      error = ER_AU_CANT_DROP_USER;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1,
	      db_get_string (&name));
      goto error;
    }

  /* check if user owns class/vclass/trigger/serial */
  for (i = 0; class_name[i] != NULL; i++)
    {
      sprintf (query_buf,
	       "select count(*) from [%s] where [owner] = ?;", class_name[i]);
      session = db_open_buffer (query_buf);
      if (session == NULL)
	{
	  goto error;
	}

      db_make_object (&val[0], user);
      db_push_values (session, 1, &val[0]);
      stmt_id = db_compile_statement (session);
      if (stmt_id != 1)
	{
	  assert (er_errid () != NO_ERROR);
	  error = er_errid ();
	  db_close_session (session);
	  goto error;
	}

      error = db_execute_statement_local (session, stmt_id, &result);
      if (error < 0)
	{
	  db_close_session (session);
	  goto error;
	}

      error = db_query_first_tuple (result);
      if (error < 0)
	{
	  db_query_end (result);
	  db_close_session (session);
	  goto error;
	}

      db_make_int (&value, 0);
      error = db_query_get_tuple_value (result, 0, &value);
      if (error != NO_ERROR)
	{
	  db_query_end (result);
	  db_close_session (session);
	  goto error;
	}

      if (db_get_int (&value) > 0)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_AU_USER_HAS_DATABASE_OBJECTS, 0);
	  db_query_end (result);
	  db_close_session (session);
	  error = ER_AU_USER_HAS_DATABASE_OBJECTS;
	  goto error;
	}

      db_query_end (result);
      db_close_session (session);
      pr_clear_value (&val[0]);
    }


  /* propagate user deletion to groups */
  db_make_object (&val[1], user);

  session = db_open_buffer ("update [db_user] [d] set "
			    "[d].[direct_groups] = [d].[direct_groups] - ? where ? in [d].[direct_groups];");
  if (session == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
      goto error;
    }

  new_groups = set_create_basic ();
  if (new_groups)
    {
      error = db_set_add (new_groups, &val[1]);
      if (error == NO_ERROR)
	{
	  db_make_set (&val[0], new_groups);
	  db_push_values (session, 2, val);
	  stmt_id = db_compile_statement (session);
	  if (stmt_id == 1)
	    {
	      error = db_execute_statement_local (session, stmt_id, &result);
	      db_query_end (result);
	    }
	  else
	    {
	      assert (er_errid () != NO_ERROR);
	      error = er_errid ();
	    }
	}
      /*
       * We need to clear the host variable here to free the set.  set_free()
       * is not sufficient since the set referenced by new_groups may have
       * be replaced as a result of tp_value_cast().
       */
      pr_clear_value (&val[0]);
    }

  db_close_session (session);
  if (error < NO_ERROR)
    {
      goto error;
    }

  session =
    db_open_buffer ("select [d] from [db_user] [d] where ? in [d].[groups];");
  if (session == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
      goto error;
    }

  db_push_values (session, 1, &val[1]);
  stmt_id = db_compile_statement (session);
  if (stmt_id == 1)
    {
      error = db_execute_statement_local (session, stmt_id, &result);
      if (error > 0)
	{
	  error = NO_ERROR;
	  while (error == NO_ERROR
		 && db_query_next_tuple (result) == DB_CURSOR_SUCCESS)
	    {
	      error = db_query_get_tuple_value (result, 0, &user_val);
	      if (error == NO_ERROR)
		{
		  if (DB_IS_NULL (&user_val))
		    {
		      auser = NULL;
		    }
		  else
		    {
		      auser = db_get_object (&user_val);
		    }

		  new_groups = set_create_basic ();
		  if (new_groups)
		    {
		      error = au_get_set (auser, "direct_groups",
					  &direct_groups);
		      if (error == NO_ERROR)
			{
			  /* compute closure */
			  gcard = set_cardinality (direct_groups);
			  for (g = 0; g < gcard && !error; g++)
			    {
			      error = set_get_element (direct_groups, g,
						       &gvalue);
			      if (error == NO_ERROR)
				{
				  error = au_add_direct_groups (new_groups,
								&gvalue);
				}
			    }
			  set_free (direct_groups);
			}
		    }
		  else
		    {
		      assert (er_errid () != NO_ERROR);
		      error = er_errid ();
		    }

		  if (error == NO_ERROR)
		    {
		      db_make_set (&value, new_groups);
		      obj_set (auser, "groups", &value);
		    }

		  if (new_groups)
		    {
		      set_free (new_groups);
		    }
		}
	    }
	}
      db_query_end (result);
    }
  else
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
    }

  db_close_session (session);
  if (error < NO_ERROR)
    {
      goto error;
    }

  db_make_null (&password_val);
  error = obj_get (user, "password", &password_val);
  if (!DB_IS_NULL (&password_val))
    {
      password = db_get_object (&password_val);
      error = obj_delete (password);
      if (error == NO_ERROR)
	{
	  db_make_null (&password_val);
	  error = obj_set (user, "password", &password_val);
	}

      if (error != NO_ERROR)
	{
	  goto error;
	}
    }

  /*
   * could go through classes created by this user and change ownership
   * to the DBA ? - do this as the classes are referenced instead
   */

  error = obj_delete (user);
  if (error == NO_ERROR)
    {
      remove_user_cache_references (user);
    }

error:
  AU_ENABLE (save);
  return error;
}

/*
 * au_drop_user_method - Method interface for au_drop_user.
 *   return: none
 *   root(in):
 *   returnval(out): return value of this method
 *   name(in):
 */
void
au_drop_user_method (MOP root, DB_VALUE * returnval, DB_VALUE * name)
{
  int error;
  MOP user;

  db_make_null (returnval);

  if (Au_dba_user != NULL && !au_is_dba_group_member (Au_user))
    {
      error = ER_AU_DBA_ONLY;
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, error, 1, "drop_user");
      db_make_error (returnval, error);
    }
  else
    {
      user = NULL;
      if (name != NULL && IS_STRING (name) && db_get_string (name) != NULL)
	{
	  user = au_find_user (db_get_string (name));
	  if (user != NULL)
	    {
	      error = au_drop_user (user);
	      if (error != NO_ERROR)
		{
		  db_make_error (returnval, error);
		}
	    }
	}
      else
	{
	  error = ER_AU_INVALID_USER;
	  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, error, 1, "");
	  db_make_error (returnval, error);
	}
    }
}


/*
 * AUTHORIZATION CACHING
 */

/*
 * find_grant_entry -  This searches a sequence of grant elements looking for
 *                     a grant	from a particular user on a particular class.
 *   return: sequence index to grant entry
 *   grants(in): sequence of grant information
 *   class_mop(in): class to look for
 *   grantor(in): user who made the grant
 *
 * Note: It returns the index into the sequence where the grant information
 *       is found.  If the grant was not found it returns -1.
 */
static int
find_grant_entry (DB_SET * grants, MOP class_mop, MOP grantor)
{
  DB_VALUE element;
  int i, gsize, position;

  position = -1;
  gsize = set_size (grants);
  for (i = 0; i < gsize && position == -1; i += GRANT_ENTRY_LENGTH)
    {
      set_get_element (grants, GRANT_ENTRY_CLASS (i), &element);
      if (db_get_object (&element) == class_mop)
	{
	  set_get_element (grants, GRANT_ENTRY_SOURCE (i), &element);
	  if (db_get_object (&element) == grantor)
	    {
	      position = i;
	    }
	}
    }

  return position;
}

/*
 * add_grant_entry - This adds a grant on a class from a particular user to
 *                   a sequence of grant elemetns.
 *                   It returns the index of the new grant element.
 *   return: sequence index to new grant entry
 *   grants(in): grant sequence to extend
 *   class_mop(in): class being granted
 *   grantor(in): user doing the granting
 */
static int
add_grant_entry (DB_SET * grants, MOP class_mop, MOP grantor)
{
  DB_VALUE value;
  int index;

  index = set_size (grants);

  db_make_object (&value, class_mop);
  set_put_element (grants, GRANT_ENTRY_CLASS (index), &value);

  db_make_object (&value, grantor);
  set_put_element (grants, GRANT_ENTRY_SOURCE (index), &value);

  db_make_int (&value, 0);
  set_put_element (grants, GRANT_ENTRY_CACHE (index), &value);

  return (index);
}

/*
 * drop_grant_entry - This removes a grant element at a particular location
 *                    and shifts all subsequent grant elements up to fill
 *                    in the empty space.
 *   return: none
 *   grants(in): grant sequence
 *   index(in): index of grant element to remove
 */
static void
drop_grant_entry (DB_SET * grants, int index)
{
  int i;

  /* not particularly fast but doesn't happen very often */
  if (set_size (grants) >= (index + GRANT_ENTRY_LENGTH))
    {
      for (i = 0; i < GRANT_ENTRY_LENGTH; i++)
	{
	  set_drop_seq_element (grants, index);
	}
    }
}

/*
 * get_grants -  This gets the grant set from an authorization object,
 *               VERY CAREFULLY.
 *   return: error code
 *   auth(in): authorization object
 *   grant_ptr(out): return grant set
 *   filter(in):
 *
 */
static int
get_grants (MOP auth, DB_SET ** grant_ptr, int filter)
{
  int error;
  DB_VALUE value;
  DB_SET *grants;
  MOP grantor, gowner, class_;
  int gsize, i, j, existing, cache;

  *grant_ptr = NULL;

  error = obj_get (auth, "grants", &value);
  if (error != NO_ERROR)
    {
      return error;
    }

  grants = NULL;

  if (DB_VALUE_TYPE (&value) != DB_TYPE_SEQUENCE
      || DB_IS_NULL (&value) || db_get_set (&value) == NULL)
    {
      error = ER_AU_CORRUPTED;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
      return error;
    }

  if (!filter)
    {
      return NO_ERROR;
    }

  grants = db_get_set (&value);
  gsize = set_size (grants);

  for (i = 0; i < gsize; i += GRANT_ENTRY_LENGTH)
    {
      error = set_get_element (grants, GRANT_ENTRY_SOURCE (i), &value);
      if (error != NO_ERROR)
	{
	  break;
	}

      grantor = NULL;
      if (DB_VALUE_TYPE (&value) == DB_TYPE_OBJECT && !DB_IS_NULL (&value))
	{
	  grantor = db_pull_object (&value);
	  if (WS_IS_DELETED (grantor))
	    {
	      grantor = NULL;
	    }
	}

      if (grantor == NULL)
	{
	  class_ = NULL;
	  error = set_get_element (grants, GRANT_ENTRY_CLASS (i), &value);
	  if (error != NO_ERROR)
	    {
	      break;
	    }

	  if (DB_VALUE_TYPE (&value) == DB_TYPE_OBJECT
	      && !DB_IS_NULL (&value))
	    {
	      class_ = db_pull_object (&value);
	      if (WS_IS_DELETED (class_))
		{
		  class_ = NULL;
		}
	    }

	  if (class_ == NULL)
	    {
	      /* class is bad too, remove this entry */
	      drop_grant_entry (grants, i);
	      gsize -= GRANT_ENTRY_LENGTH;
	    }
	  else
	    {
	      /* this will at least be DBA */
	      gowner = au_get_class_owner (class_);

	      /* see if there's already an entry for this */
	      existing = -1;
	      for (j = 0; j < gsize && existing == -1;
		   j += GRANT_ENTRY_LENGTH)
		{
		  error = set_get_element (grants, GRANT_ENTRY_SOURCE (j),
					   &value);
		  if (error != NO_ERROR)
		    {
		      break;
		    }

		  if (DB_VALUE_TYPE (&value) == DB_TYPE_OBJECT
		      && db_get_object (&value) == gowner)
		    {
		      existing = j;
		    }
		}

	      if (error != NO_ERROR)
		{
		  break;
		}

	      if (existing == -1)
		{
		  /*
		   * no previous entry for the owner,
		   * use the current one
		   */
		  db_make_object (&value, gowner);
		  set_put_element (grants, GRANT_ENTRY_SOURCE (i), &value);
		}
	      else
		{
		  /*
		   * update the previous entry with the extra bits
		   * and delete the current entry
		   */
		  set_get_element (grants, GRANT_ENTRY_CACHE (i), &value);
		  cache = db_get_int (&value);
		  set_get_element (grants, GRANT_ENTRY_CACHE (existing),
				   &value);
		  db_make_int (&value, db_get_int (&value) | cache);
		  set_put_element (grants, GRANT_ENTRY_CACHE (existing),
				   &value);
		  drop_grant_entry (grants, i);
		  gsize -= GRANT_ENTRY_LENGTH;
		}
	    }
	}
    }

  if (error != NO_ERROR && grants != NULL)
    {
      set_free (grants);
      grants = NULL;
    }

  *grant_ptr = grants;
  return (error);
}

/*
 * apply_grants -  Work function for update_cache.
 *   return: error code
 *   auth(in):  authorization object
 *   class(in):  class being authorized
 *   bits(in):
 *
 * Note: Get the grant information for an authorization object and update
 *       the cache for any grants that apply to the class.
 */
static int
apply_grants (MOP auth, MOP class_mop, unsigned int *bits)
{
  int error = NO_ERROR;
  DB_SET *grants;
  DB_VALUE grvalue;
  int i, gsize;

  error = get_grants (auth, &grants, 1);
  if (error == NO_ERROR)
    {
      gsize = set_size (grants);
      for (i = 0; i < gsize; i += GRANT_ENTRY_LENGTH)
	{
	  set_get_element (grants, GRANT_ENTRY_CLASS (i), &grvalue);
	  if (db_get_object (&grvalue) == class_mop)
	    {
	      set_get_element (grants, GRANT_ENTRY_CACHE (i), &grvalue);
	      *bits |= db_get_int (&grvalue);
	    }
	}
      set_free (grants);
    }

  return (error);
}

/*
 * update_cache -  This is the main function for parsing all of
 *                 the authorization information for a particular class and
 *                 caching it in the class structure.
 *                 This will be called once after each successful
 *                 read/write lock. It needs to be fast.
 *   return: error code
 *   classop(in):  class MOP to authorize
 *   sm_class(in): direct pointer to class structure
 *   cache(in):
 */
static int
update_cache (MOP classop, SM_CLASS * sm_class, AU_CLASS_CACHE * cache)
{
  int error = NO_ERROR;
  DB_SET *groups;
  int i, save, card;
  DB_VALUE value;
  MOP group, auth;
  unsigned int *bits;

  /*
   * must disable here because we may be updating the cache of the system
   * objects and we need to read them in order to update etc.
   */
  AU_DISABLE (save);

  bits = &cache->data[Au_cache_index];

  /* initialize the cache bits */
  *bits = AU_NO_AUTHORIZATION;

  if (sm_class->owner == NULL)
    {
      /* This shouldn't happen - assign it to the DBA */
      error = ER_AU_CLASS_WITH_NO_OWNER;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
    }
  else if (au_is_dba_group_member (Au_user))
    {
      *bits = AU_FULL_AUTHORIZATION;
    }
  else if (ws_is_same_object (Au_user, sm_class->owner))
    {
      /* might want to allow grant/revoke on self */
      *bits = AU_FULL_AUTHORIZATION;
    }
  else if (au_get_set (Au_user, "groups", &groups) != NO_ERROR)
    {
      error = ER_AU_ACCESS_ERROR;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 2, "groups",
	      AU_USER_CLASS_NAME);
    }
  else
    {
      db_make_object (&value, sm_class->owner);
      if (set_ismember (groups, &value))
	{
	  /* we're a member of the owning group */
	  *bits = AU_FULL_AUTHORIZATION;
	}
      else if (au_get_object (Au_user, "authorization", &auth) != NO_ERROR)
	{
	  error = ER_AU_ACCESS_ERROR;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 2, "authorization",
		  AU_USER_CLASS_NAME);
	}
      else
	{
	  /* apply local grants */
	  error = apply_grants (auth, classop, bits);
	  if (error == NO_ERROR)
	    {
	      /* apply grants from all groups */
	      card = set_cardinality (groups);
	      for (i = 0; i < card; i++)
		{
		  /* will have to handle deleted groups here ! */
		  error = au_set_get_obj (groups, i, &group);
		  if (error == NO_ERROR)
		    {
		      if (ws_is_same_object (group, Au_dba_user))
			{
			  /* someones on the DBA member list, give them power */
			  *bits = AU_FULL_AUTHORIZATION;
			}
		      else
			{
			  error = au_get_object (group, "authorization",
						 &auth);
			  if (error == NO_ERROR)
			    {
			      /* abort on errors ?? */
			      (void) apply_grants (auth, classop, bits);
			    }
			}
		    }
		}
	    }
	}
      set_free (groups);
    }

  AU_ENABLE (save);

  return (error);
}

/*
 * appropriate_error -  This selects an appropriate error code to correspond
 *                      with an authorization failure of a some type
 *   return: error code
 *   bits(in): authorization type
 *   requested(in):
 * TODO : LP64
 */
static int
appropriate_error (unsigned int bits, unsigned int requested)
{
  int error;
  unsigned int mask, atype;
  int i;

  /*
   * Since we don't currently have a way of indicating multiple
   * authorization failures, select the first one in the
   * bit vector that causes problems.
   * Order the switch statement so that its roughly in dependency order
   * to result in better error message.  The main thing is that
   * SELECT should be first.
   */

  error = NO_ERROR;
  mask = 1;
  for (i = 0; i < AU_GRANT_SHIFT && !error; i++)
    {
      if (requested & mask)
	{
	  /* we asked for this one */
	  if ((bits & mask) == 0)
	    {
	      /* but it wasn't available */
	      switch (mask)
		{
		case AU_SELECT:
		  error = ER_AU_SELECT_FAILURE;
		  break;
		case AU_ALTER:
		  error = ER_AU_ALTER_FAILURE;
		  break;
		case AU_UPDATE:
		  error = ER_AU_UPDATE_FAILURE;
		  break;
		case AU_INSERT:
		  error = ER_AU_INSERT_FAILURE;
		  break;
		case AU_DELETE:
		  error = ER_AU_DELETE_FAILURE;
		  break;
		case AU_INDEX:
		  error = ER_AU_INDEX_FAILURE;
		  break;
		case AU_EXECUTE:
		  error = ER_AU_EXECUTE_FAILURE;
		  break;
		default:
		  error = ER_AU_AUTHORIZATION_FAILURE;
		  break;
		}
	    }
	}
      mask = mask << 1;
    }

  if (!error)
    {
      /* we seemed to have all the basic authorizations, check grant options */
      mask = 1 << AU_GRANT_SHIFT;
      for (i = 0; i < AU_GRANT_SHIFT && !error; i++)
	{
	  if (requested & mask)
	    {
	      /* we asked for this one */
	      if ((bits & mask) == 0)
		{
		  /*
		   * But it wasn't available, convert this back down to the
		   * corresponding basic type and select an appropriate error.
		   *
		   * NOTE: We need to add type specific errors here !
		   *
		   */
		  atype = mask >> AU_GRANT_SHIFT;
		  switch (atype)
		    {
		    case AU_SELECT:
		      error = ER_AU_NO_GRANT_OPTION;
		      break;
		    case AU_ALTER:
		      error = ER_AU_NO_GRANT_OPTION;
		      break;
		    case AU_UPDATE:
		      error = ER_AU_NO_GRANT_OPTION;
		      break;
		    case AU_INSERT:
		      error = ER_AU_NO_GRANT_OPTION;
		      break;
		    case AU_DELETE:
		      error = ER_AU_NO_GRANT_OPTION;
		      break;
		    case AU_INDEX:
		      error = ER_AU_NO_GRANT_OPTION;
		      break;
		    case AU_EXECUTE:
		      error = ER_AU_NO_GRANT_OPTION;
		      break;
		    default:
		      error = ER_AU_NO_GRANT_OPTION;
		      break;
		    }
		}
	    }
	  mask = mask << 1;
	}
    }

  return (error);
}

/*
 * check_grant_option - Checks to see if a class has the grant option for
 *                      a particular authorization type.
 *                      Called by au_grant and au_revoke
 *   return: error code
 *   classop(in):  MOP of class being examined
 *   class(in): direct pointer to class structure
 *   type(in): type of authorization being checked
 *
 * TODO: LP64
 */
static int
check_grant_option (MOP classop, SM_CLASS * sm_class, DB_AUTH type)
{
  int error = NO_ERROR;
  AU_CLASS_CACHE *cache;
  unsigned int cache_bits;
  unsigned int mask;

  /*
   * this potentially can be called during initialization when we don't
   * actually have any users yet.  If so, assume its ok
   */
  if (Au_cache_index < 0)
    return NO_ERROR;

  cache = (AU_CLASS_CACHE *) sm_class->auth_cache;
  if (sm_class->auth_cache == NULL)
    {
      cache = au_install_class_cache (sm_class);
      if (cache == NULL)
	{
	  assert (er_errid () != NO_ERROR);
	  return er_errid ();
	}
    }
  cache_bits = cache->data[Au_cache_index];

  if (cache_bits == AU_CACHE_INVALID)
    {
      if (update_cache (classop, sm_class, cache))
	{
	  assert (er_errid () != NO_ERROR);
	  return er_errid ();
	}
      cache_bits = cache->data[Au_cache_index];
    }

  /* build the complete bit mask */
  mask = type | (type << AU_GRANT_SHIFT);
  if ((cache_bits & mask) != mask)
    {
      error = appropriate_error (cache_bits, mask);
      if (error)
	{
	  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, error, 0);
	}
    }

  return error;
}


/*
 * GRANT OPERATION
 */

/*
 * au_grant - This is the primary interface function for granting authorization
 *            on classes.
 *   return: error code
 *   user(in): user receiving the grant
 *   class_mop(in): class being authorized
 *   type(in): type of authorization
 *   grant_option(in): non-zero if grant option is also being given
 */
int
au_grant (MOP user, MOP class_mop, DB_AUTH type, bool grant_option)
{
  int error = NO_ERROR;
  MOP auth;
  DB_SET *grants;
  DB_VALUE value;
  int current, save = 0, gindex;
  SM_CLASS *classobj;
  int is_partition = DB_NOT_PARTITIONED_CLASS, i, savepoint_grant = 0;
  MOP *sub_partitions = NULL;

  error = sm_partitioned_class_type (class_mop, &is_partition, NULL,
				     &sub_partitions);
  if (error != NO_ERROR)
    {
      return error;
    }

  if (is_partition == DB_PARTITIONED_CLASS)
    {
      error = tran_system_savepoint (UNIQUE_PARTITION_SAVEPOINT_GRANT);
      if (error != NO_ERROR)
	{
	  goto fail_end;
	}

      savepoint_grant = 1;
      for (i = 0; sub_partitions[i]; i++)
	{
	  error = au_grant (user, sub_partitions[i], type, grant_option);
	  if (error != NO_ERROR)
	    {
	      break;
	    }
	}

      free_and_init (sub_partitions);
      if (error != NO_ERROR)
	{
	  goto fail_end;
	}
    }

  AU_DISABLE (save);
  if (ws_is_same_object (user, Au_user))
    {
      /*
       * Treat grant to self condition as a success only. Although this
       * statement is a no-op, it is not an indication of no-success.
       * The "privileges" are indeed already granted to self.
       * Note: Revoke from self is an error, because this cannot be done.
       */
    }
  else if ((error = au_fetch_class_force (class_mop, &classobj,
					  AU_FETCH_READ)) == NO_ERROR)
    {
      if (ws_is_same_object (classobj->owner, user))
	{
	  error = ER_AU_CANT_GRANT_OWNER;
	  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, error, 0);
	}
      else if ((error = check_grant_option (class_mop, classobj,
					    type)) == NO_ERROR)
	{
	  if (au_get_object (user, "authorization", &auth) != NO_ERROR)
	    {
	      error = ER_AU_ACCESS_ERROR;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 2,
		      AU_USER_CLASS_NAME, "authorization");
	    }
	  /* lock authorization for write & mark dirty */
	  else if (au_fetch_instance (auth, NULL, AU_FETCH_UPDATE, AU_UPDATE)
		   != NO_ERROR)
	    {
	      error = ER_AU_CANT_UPDATE;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
	    }
	  else if ((error = obj_lock (auth, 1)) == NO_ERROR
		   && (error = get_grants (auth, &grants, 1)) == NO_ERROR)
	    {
	      gindex = find_grant_entry (grants, class_mop, Au_user);
	      if (gindex == -1)
		{
		  gindex = add_grant_entry (grants, class_mop, Au_user);
		  current = AU_NO_AUTHORIZATION;
		}
	      else
		{
		  /* already granted, get current cache */
		  error = set_get_element (grants, GRANT_ENTRY_CACHE (gindex),
					   &value);
		  if (error != NO_ERROR)
		    {
		      set_free (grants);
		      if (sub_partitions)
			{
			  free_and_init (sub_partitions);
			}
		      AU_ENABLE (save);
		      return (error);
		    }
		  current = db_get_int (&value);
		}

#if defined(SA_MODE)
	      if (catcls_Enable == true)
#endif /* SA_MODE */
		{
		  DB_AUTH ins_bits, upd_bits;

		  ins_bits =
		    (DB_AUTH) ((~current & AU_TYPE_MASK) & (int) type);
		  if (ins_bits)
		    {
		      error =
			au_insert_new_auth (Au_user, user, class_mop,
					    ins_bits,
					    (grant_option) ? ins_bits :
					    false);
		    }
		  upd_bits = (DB_AUTH) (~ins_bits & (int) type);
		  if ((error == NO_ERROR) && upd_bits)
		    {
		      error =
			au_update_new_auth (Au_user, user, class_mop,
					    upd_bits,
					    (grant_option) ? upd_bits :
					    false);
		    }
		}

	      current |= (int) type;
	      if (grant_option)
		{
		  current |= ((int) type << AU_GRANT_SHIFT);
		}

	      db_make_int (&value, current);
	      set_put_element (grants, GRANT_ENTRY_CACHE (gindex), &value);
	      set_free (grants);

	      /*
	       * clear the cache for this user/class pair to make sure we
	       * recalculate it the next time it is referenced
	       */
	      reset_cache_for_user_and_class (classobj);

	      /*
	       * Make sure any cached parse trees are rebuild.  This proabably
	       * isn't necessary for GRANT, only REVOKE.
	       */
	      sm_bump_local_schema_version ();
	    }
	}
    }

fail_end:
  if (savepoint_grant && error != NO_ERROR
      && error != ER_LK_UNILATERALLY_ABORTED)
    {
      (void)
	tran_abort_upto_system_savepoint (UNIQUE_PARTITION_SAVEPOINT_GRANT);
    }
  if (sub_partitions)
    {
      free_and_init (sub_partitions);
    }
  AU_ENABLE (save);
  return (error);
}


/*
 * REVOKE OPERATION
 */

/*
 * free_grant_list - Frees a list of temporary grant flattening structures.
 *    return: none
 *    grants(in): list of grant structures
 */
static void
free_grant_list (AU_GRANT * grants)
{
  AU_GRANT *next;

  for (next = NULL; grants != NULL; grants = next)
    {
      next = grants->next;

      /* always make sure object pointers are NULL in the freed stuff */
      grants->auth_object = NULL;
      grants->user = NULL;
      grants->grantor = NULL;

      db_ws_free (grants);
    }
}

/*
 * collect_class_grants - Collects information about every grant of
 *                        a particular type made on a class.
 *   return: error code
 *   class_mop(in): class for which we're gathering all grants
 *   type(in): type of grant we're interested in
 *   revoked_auth(in): authorization object containing revoked grant
 *   revoked_grant_index(in): index of revoked grant element
 *   return_grants(in): returned list of grant structures
 *
 * Note:
 *    Since we don't keep the grants associated with the class
 *    object, we have to visit every user object and collect the grants for
 *    that class.  This could be a lot more effecient if we had a
 *    "granted to" set in the user object so we can have a more directed
 *    search.
 *    The revoked_auth & revoked_grant_index arguments identify a grant
 *    on some user that is being revoked.  When this grant is encountered
 *    it is not included in the resulting grant list.
 *    The db_root class used to have a user attribute which was a set
 *    containing the object-id for all users.  The users attribute has been
 *    eliminated for performance reasons.  A query on the db_user class is
 *    now used to find all users.
 */
static int
collect_class_grants (MOP class_mop, DB_AUTH type, MOP revoked_auth,
		      int revoked_grant_index, AU_GRANT ** return_grants)
{
  int error = NO_ERROR;
  MOP user, auth;
  DB_VALUE element;
  DB_SET *grants;
  AU_GRANT *grant_list, *new_grant;
  int cache, j, gsize;
  char *query;
  size_t query_size;
  DB_QUERY_RESULT *query_result;
  DB_QUERY_ERROR query_error;
  DB_VALUE user_val;
  const char *qp1 = "select [%s] from [%s];";

  *return_grants = NULL;

  query_size = strlen (qp1) + strlen (AU_USER_CLASS_NAME) * 2;
  query = (char *) malloc (query_size);
  if (query == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
	      query_size);
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  sprintf (query, qp1, AU_USER_CLASS_NAME, AU_USER_CLASS_NAME);
  error = db_compile_and_execute_local (query, &query_result, &query_error);
  if (error < 0)
    /* error is row count if not negative. */
    {
      free_and_init (query);
      return error;
    }

  grant_list = NULL;

  while (db_query_next_tuple (query_result) == DB_CURSOR_SUCCESS)
    {
      if (db_query_get_tuple_value (query_result, 0, &user_val) == NO_ERROR)
	{
	  if (DB_IS_NULL (&user_val))
	    {
	      user = NULL;
	    }
	  else
	    {
	      user = db_get_object (&user_val);
	    }

	  /* should remove deleted users when encountered ! */
	  if (au_get_object (user, "authorization", &auth) != NO_ERROR)
	    {
	      /* If this is the "deleted object" error, ignore it */
	      assert (er_errid () != NO_ERROR);
	      error = er_errid ();
	      if (error == ER_HEAP_UNKNOWN_OBJECT)
		{
		  error = NO_ERROR;
		}
	    }
	  else if ((error = get_grants (auth, &grants, 1)) == NO_ERROR)
	    {

	      gsize = set_size (grants);
	      for (j = 0; j < gsize && error == NO_ERROR;
		   j += GRANT_ENTRY_LENGTH)
		{
		  /* ignore the grant entry that is being revoked */
		  if (auth == revoked_auth && j == revoked_grant_index)
		    continue;

		  /* see if grant is for the class in question */
		  if (set_get_element (grants, GRANT_ENTRY_CLASS (j),
				       &element))
		    {
		      assert (er_errid () != NO_ERROR);
		      error = er_errid ();
		      break;
		    }

		  if (db_get_object (&element) == class_mop)
		    {
		      cache = AU_NO_AUTHORIZATION;
		      if (set_get_element (grants, GRANT_ENTRY_CACHE (j),
					   &element))
			{
			  assert (er_errid () != NO_ERROR);
			  error = er_errid ();
			  break;
			}

		      cache = db_get_int (&element);
		      if ((cache & (int) type))
			{
			  new_grant =
			    (AU_GRANT *) db_ws_alloc (sizeof (AU_GRANT));
			  if (new_grant == NULL)
			    {
			      assert (er_errid () != NO_ERROR);
			      error = er_errid ();
			      break;
			    }

			  new_grant->next = grant_list;
			  grant_list = new_grant;
			  new_grant->legal = 0;
			  new_grant->auth_object = auth;
			  new_grant->grant_index = j;
			  new_grant->user = user;
			  new_grant->grant_option =
			    (((int) type << AU_GRANT_SHIFT) & cache);
			  if (set_get_element (grants, GRANT_ENTRY_SOURCE (j),
					       &element))
			    {
			      assert (er_errid () != NO_ERROR);
			      error = er_errid ();
			    }
			  else
			    {
			      if (DB_IS_NULL (&element))
				{
				  new_grant->grantor = NULL;
				}
			      else
				{
				  new_grant->grantor =
				    db_get_object (&element);
				}
			    }
			}
		    }
		}
	      set_free (grants);
	    }
	}
    }

  db_query_end (query_result);
  free_and_init (query);

  if (error != NO_ERROR && grant_list != NULL)
    {
      free_grant_list (grant_list);
      grant_list = NULL;
    }
  *return_grants = grant_list;

  return (error);
}

/*
 * map_grant_list - Work function for propagate_revoke.
 *   return: none
 *   grants(in): grant list
 *   grantor(in): owner object
 *
 * Note: Recursively maps over the elements in a grant list marking all
 *       grants that have a valid path from the owner.
 *       If we need to get fancy, this could take timestamp information
 *       into account.
 */
static void
map_grant_list (AU_GRANT * grants, MOP grantor)
{
  AU_GRANT *g;

  for (g = grants; g != NULL; g = g->next)
    {
      if (!g->legal)
	{
	  if (g->grantor == grantor)
	    {
	      g->legal = 1;
	      if (g->grant_option)
		{
		  map_grant_list (grants, g->user);
		}
	    }
	}
    }
}

/*
 * propagate_revoke - Propagates a revoke operation to all affected users.
 *   return: error code
 *   grant_list(in):  list of grant nodes
 *   owner(in): owner of the class
 *   mask(in): authorization type mask
 */
static int
propagate_revoke (AU_GRANT * grant_list, MOP owner, DB_AUTH mask)
{
  int error = NO_ERROR;
  DB_VALUE element;
  DB_SET *grants;
  AU_GRANT *g;
  int i, length;

  /* determine invalid grants */
  map_grant_list (grant_list, owner);

#if defined(SA_MODE)
  if (catcls_Enable == true)
#endif /* SA_MODE */
    {
      error = au_propagate_del_new_auth (grant_list, mask);
      if (error != NO_ERROR)
	return error;
    }

  /* make sure we can get locks on the affected authorization objects */
  for (g = grant_list; g != NULL && error == NO_ERROR; g = g->next)
    {
      if (!g->legal)
	{
	  /*
	   * lock authorization for write & mark dirty,
	   * don't need to pin because we'll be going through the usual
	   * set interface, this just ensures that the locks can be obtained
	   */
	  if (au_fetch_instance
	      (g->auth_object, NULL, AU_FETCH_UPDATE, AU_UPDATE) != NO_ERROR)
	    {
	      error = ER_AU_CANT_UPDATE;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
	    }
	}
    }

  /* if the locks are available, perform the revoke */
  if (error == NO_ERROR)
    {
      for (g = grant_list; g != NULL && error == NO_ERROR; g = g->next)
	{
	  if (!g->legal)
	    {
	      if ((error = obj_lock (g->auth_object, 1)) == NO_ERROR
		  && (error =
		      get_grants (g->auth_object, &grants, 0)) == NO_ERROR)
		{
		  if ((error =
		       set_get_element (grants,
					GRANT_ENTRY_CACHE (g->grant_index),
					&element)) == NO_ERROR)
		    {
		      db_make_int (&element, db_get_int (&element) & mask);
		      error =
			set_put_element (grants,
					 GRANT_ENTRY_CACHE (g->grant_index),
					 &element);
		    }
		  /*
		   * if cache bits are zero, we can't remove it because
		   * there may be other entries in the grant list that
		   * have indexes into this set -
		   * must wait until all have been processed before
		   * compressing the set
		   */
		  set_free (grants);
		}
	    }
	}

      /*
       * now go back through and remove any grant entries that have no
       * bits set
       */
      for (g = grant_list; g != NULL && error == NO_ERROR; g = g->next)
	{
	  if (!g->legal)
	    {
	      if ((error = obj_lock (g->auth_object, 1)) == NO_ERROR
		  && (error
		      = get_grants (g->auth_object, &grants, 0)) == NO_ERROR)
		{
		  length = set_size (grants);
		  for (i = 0; i < length; i += GRANT_ENTRY_LENGTH)
		    {
		      if ((error =
			   set_get_element (grants, GRANT_ENTRY_CACHE (i),
					    &element)) != NO_ERROR)
			break;
		      if (db_get_int (&element) == 0)
			{
			  /* remove this entry */
			  drop_grant_entry (grants, i);
			  /* must adjust loop termination counter */
			  length = set_size (grants);
			}
		    }
		  set_free (grants);
		}
	    }
	}
    }

  return (error);
}

/*
 * au_revoke - This is the primary interface function for
 *             revoking authorization
 *   return: error code
 *   user(in): user being revoked
 *   class_mop(in): class being revoked
 *   type(in): type of authorization being revoked
 *
 * Note: The authorization of the given type on the given class is removed
 *       from the authorization info stored with the given user.  If this
 *       user has the grant option for this type and has granted authorization
 *       to other users, the revoke will be recursively propagated to all
 *       affected users.
 *
 * TODO : LP64
 */
int
au_revoke (MOP user, MOP class_mop, DB_AUTH type)
{
  int error;
  MOP auth;
  DB_SET *grants = NULL;
  DB_VALUE cache_element;
  int current, mask, save = 0, gindex;
  AU_GRANT *grant_list;
  SM_CLASS *classobj;
  int is_partition = DB_NOT_PARTITIONED_CLASS, i = 0, savepoint_revoke = 0;
  MOP *sub_partitions = NULL;

  error = sm_partitioned_class_type (class_mop, &is_partition, NULL,
				     &sub_partitions);
  if (error != NO_ERROR)
    {
      return error;
    }

  if (is_partition == DB_PARTITIONED_CLASS)
    {
      error = tran_system_savepoint (UNIQUE_PARTITION_SAVEPOINT_REVOKE);
      if (error != NO_ERROR)
	{
	  goto fail_end;
	}
      savepoint_revoke = 1;

      for (i = 0; sub_partitions[i]; i++)
	{
	  error = au_revoke (user, sub_partitions[i], type);
	  if (error != NO_ERROR)
	    {
	      break;
	    }
	}

      free_and_init (sub_partitions);
      if (error != NO_ERROR)
	{
	  goto fail_end;
	}
    }

  AU_DISABLE (save);
  if (ws_is_same_object (user, Au_user))
    {
      error = ER_AU_CANT_REVOKE_SELF;
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, error, 0);
      goto fail_end;
    }

  error = au_fetch_class_force (class_mop, &classobj, AU_FETCH_READ);
  if (error == NO_ERROR)
    {
      if (ws_is_same_object (classobj->owner, user))
	{
	  error = ER_AU_CANT_REVOKE_OWNER;
	  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, error, 0);
	  goto fail_end;
	}

      error = check_grant_option (class_mop, classobj, type);
      if (error != NO_ERROR)
	{
	  goto fail_end;
	}

      if (au_get_object (user, "authorization", &auth) != NO_ERROR)
	{
	  error = ER_AU_ACCESS_ERROR;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 2,
		  AU_USER_CLASS_NAME, "authorization");
	  goto fail_end;
	}
      else if (au_fetch_instance (auth, NULL, AU_FETCH_UPDATE, AU_UPDATE)
	       != NO_ERROR)
	{
	  error = ER_AU_CANT_UPDATE;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
	  goto fail_end;
	}
      else if ((error = obj_lock (auth, 1)) == NO_ERROR
	       && (error = get_grants (auth, &grants, 1)) == NO_ERROR)
	{
	  gindex = find_grant_entry (grants, class_mop, Au_user);
	  if (gindex == -1)
	    {
	      error = ER_AU_GRANT_NOT_FOUND;
	      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, error, 0);
	      goto fail_end;
	    }
	  else
	    {
	      /* get current cache bits */
	      error = set_get_element (grants, gindex + 2, &cache_element);
	      if (error != NO_ERROR)
		{
		  set_free (grants);
		  if (sub_partitions)
		    {
		      free_and_init (sub_partitions);
		    }
		  AU_ENABLE (save);
		  return (error);
		}
	      current = db_get_int (&cache_element);

	      /*
	       * If all the bits are set, assume we wan't to
	       * revoke everything previously granted, makes it a bit
	       * easier but muddies the semantics too much ?
	       */
	      if (type == DB_AUTH_ALL)
		{
		  type = (DB_AUTH) (current & AU_TYPE_MASK);
		}

	      /*
	       * this test could be more sophisticated, right now,
	       * if there are any valid grants that fit in
	       * the specified bit mask, the operation will proceed,
	       * we could make sure that every bit in the supplied
	       * mask is also present in the cache and if not abort
	       * the whole thing
	       */

	      if ((current & (int) type) == 0)
		{
		  error = ER_AU_GRANT_NOT_FOUND;
		  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, error, 0);
		}
	      else
		if ((error =
		     collect_class_grants (class_mop, type, auth,
					   gindex, &grant_list)) == NO_ERROR)
		{

		  /* calculate the mask to turn off the grant */
		  mask = (int) ~(type | (type << AU_GRANT_SHIFT));

		  /* propagate the revoke to the affected classes */
		  if ((error =
		       propagate_revoke (grant_list, classobj->owner,
					 (DB_AUTH) mask)) == NO_ERROR)
		    {

		      /*
		       * finally, update the local grant for the
		       * original object
		       */
		      current &= mask;
		      if (current)
			{
			  db_make_int (&cache_element, current);
			  set_put_element (grants, gindex + 2,
					   &cache_element);
			}
		      else
			{
			  /* no remaining grants, remove it from
			   * the grant set
			   */
			  drop_grant_entry (grants, gindex);
			}
		      /*
		       * clear the cache for this user/class pair
		       * to make sure we recalculate it the next time
		       * it is referenced
		       */
		      reset_cache_for_user_and_class (classobj);

#if defined(SA_MODE)
		      if (catcls_Enable == true)
#endif /* SA_MODE */
			error =
			  au_delete_new_auth (Au_user, user, class_mop, type);

		      /*
		       * Make sure that we don't keep any parse trees
		       * around that rely on obsolete authorization.
		       * This may not be necessary.
		       */
		      sm_bump_local_schema_version ();
		    }
		  free_grant_list (grant_list);
		}
	    }
	}
    }

fail_end:
  if (grants != NULL)
    {
      set_free (grants);
    }
  if (savepoint_revoke && error != NO_ERROR
      && error != ER_LK_UNILATERALLY_ABORTED)
    {
      (void)
	tran_abort_upto_system_savepoint (UNIQUE_PARTITION_SAVEPOINT_REVOKE);
    }
  if (sub_partitions)
    {
      free_and_init (sub_partitions);
    }
  AU_ENABLE (save);
  return (error);
}

/*
 * MISC UTILITIES
 */

/*
 * au_change_owner - This changes the owning user of a class.
 *                   This should be called only by the DBA.
 *   return: error code
 *   classmop(in): class whose owner is to change
 *   owner(in): new owner
 */
int
au_change_owner (MOP classmop, MOP owner)
{
  int error = NO_ERROR;
  SM_CLASS *class_;
  int save;
  SM_ATTRIBUTE *attr;

  AU_DISABLE (save);
  if (!au_is_dba_group_member (Au_user))
    {
      error = ER_AU_DBA_ONLY;
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, error, 1, "change_owner");
    }
  else
    {
      error = au_fetch_class_force (classmop, &class_, AU_FETCH_UPDATE);
      if (error == NO_ERROR)
	{
	  /*
	   * Change serial object's owner when the class has auto_increment
	   * attribute column.
	   */
	  for (attr = class_->attributes; attr != NULL;
	       attr = (SM_ATTRIBUTE *) attr->header.next)
	    {
	      if (attr->auto_increment != NULL)
		{
		  error =
		    au_change_serial_owner (&attr->auto_increment, owner);
		  if (error != NO_ERROR)
		    {
		      goto exit_on_error;
		    }
		}
	    }

	  /* Change class owner */
	  class_->owner = owner;
	  error = locator_flush_class (classmop);
	}
    }
exit_on_error:
  AU_ENABLE (save);
  return (error);
}

/*
 * au_change_owner_method - Method interface to au_change_owner
 *   return: none
 *   obj(in): class whose owner is to change
 *   returnval(out): return value of this method
 *   class(in):
 *   owner(in): new owner
 */
void
au_change_owner_method (MOP obj, DB_VALUE * returnval, DB_VALUE * class_,
			DB_VALUE * owner)
{
  MOP user, classmop;
  int error = NO_ERROR;
  int is_partition = DB_NOT_PARTITIONED_CLASS, i, savepoint_owner = 0;
  MOP *sub_partitions = NULL;
  char *class_name = NULL, *owner_name = NULL;
  SM_CLASS *clsobj;

  db_make_null (returnval);

  if (DB_IS_NULL (class_) || !IS_STRING (class_)
      || (class_name = db_get_string (class_)) == NULL)
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_AU_INVALID_CLASS, 1, "");
      db_make_error (returnval, ER_AU_INVALID_CLASS);
      return;
    }
  if (DB_IS_NULL (owner) || !IS_STRING (owner)
      || (owner_name = db_get_string (owner)) == NULL)
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_AU_INVALID_USER, 1, "");
      db_make_error (returnval, ER_AU_INVALID_USER);
      return;
    }

  classmop = sm_find_class (class_name);
  if (classmop == NULL)
    {
      db_make_error (returnval, er_errid ());
      return;
    }

  error = au_fetch_class_force (classmop, &clsobj, AU_FETCH_UPDATE);
  if (error != NO_ERROR)
    {
      goto fail_return;
    }

  /* To change the owner of a system class is not allowed. */
  if (sm_issystem (clsobj))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_AU_CANT_ALTER_OWNER_OF_SYSTEM_CLASS, 0);
      db_make_error (returnval, er_errid ());
      return;
    }

  user = au_find_user (owner_name);
  if (user == NULL)
    {
      db_make_error (returnval, er_errid ());
      return;
    }

  error = sm_partitioned_class_type (classmop, &is_partition, NULL,
				     &sub_partitions);
  if (error != NO_ERROR)
    {
      goto fail_return;
    }

  if (is_partition != DB_NOT_PARTITIONED_CLASS)
    {
      if (is_partition == DB_PARTITION_CLASS)	/* if partition; error */
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_NOT_ALLOWED_ACCESS_TO_PARTITION, 0);
	  error = ER_NOT_ALLOWED_ACCESS_TO_PARTITION;
	  goto fail_return;
	}
      else			/* if partitioned class; do actions to all partitions */
	{
	  error = tran_system_savepoint (UNIQUE_PARTITION_SAVEPOINT_OWNER);
	  if (error != NO_ERROR)
	    goto fail_return;
	  savepoint_owner = 1;
	  for (i = 0; sub_partitions[i]; i++)
	    {
	      error = au_change_owner (sub_partitions[i], user);
	      if (error != NO_ERROR)
		break;
	    }
	  if (error != NO_ERROR)
	    goto fail_return;
	}
    }

  error = au_change_owner (classmop, user);

fail_return:
  if (savepoint_owner && error != NO_ERROR
      && error != ER_LK_UNILATERALLY_ABORTED)
    {
      (void)
	tran_abort_upto_system_savepoint (UNIQUE_PARTITION_SAVEPOINT_OWNER);
    }
  if (sub_partitions)
    {
      free_and_init (sub_partitions);
    }
  if (error != NO_ERROR)
    {
      db_make_error (returnval, error);
    }
}

/*
 * au_change_serial_owner() - Change serial object's owner
 *   return: error code
 *   object(in/out): serial object whose owner is to be changed
 *   new_owner(in): new owner
 */
int
au_change_serial_owner (MOP * object, MOP new_owner)
{
  DB_OTMPL *obt_p = NULL;
  DB_VALUE value;
  int au_save, error;

  assert (object != NULL);

  if (!au_is_dba_group_member (Au_user))
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_AU_DBA_ONLY, 1,
	      "change_serial_owner");
      return ER_AU_DBA_ONLY;
    }

  AU_DISABLE (au_save);

  obt_p = dbt_edit_object (*object);
  if (obt_p == NULL)
    {
      goto exit_on_error;
    }

  db_make_object (&value, new_owner);
  error = dbt_put_internal (obt_p, "owner", &value);
  pr_clear_value (&value);
  if (error != NO_ERROR)
    {
      goto exit_on_error;
    }

  *object = dbt_finish_object (obt_p);
  if (*object == NULL)
    {
      goto exit_on_error;
    }

  AU_ENABLE (au_save);
  return NO_ERROR;

exit_on_error:
  AU_ENABLE (au_save);

  assert (er_errid () != NO_ERROR);
  return er_errid ();
}

/*
 * au_change_serial_owner_method() - Method interface to au_change_serial_owner
 *   return: none
 *   obj(in): class whose owner is to change
 *   returnval(out): return value of this method
 *   serial(in): serial name
 *   owner(in): new owner
 */
void
au_change_serial_owner_method (MOP obj, DB_VALUE * returnval,
			       DB_VALUE * serial, DB_VALUE * owner)
{
  MOP user = NULL, serial_object = NULL;
  MOP serial_class_mop;
  DB_IDENTIFIER serial_obj_id;
  char *serial_name, *owner_name;
  int error = NO_ERROR, found = 0;

  db_make_null (returnval);

  if (DB_IS_NULL (serial) || !IS_STRING (serial)
      || (serial_name = db_get_string (serial)) == NULL)
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_OBJ_INVALID_ARGUMENT, 1,
	      "");
      db_make_error (returnval, ER_OBJ_INVALID_ARGUMENT);
      return;
    }

  if (DB_IS_NULL (owner) || !IS_STRING (owner)
      || (owner_name = db_get_string (owner)) == NULL)
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_AU_INVALID_USER, 1, "");
      db_make_error (returnval, ER_AU_INVALID_USER);
      return;
    }

  serial_class_mop = sm_find_class (CT_SERIAL_NAME);

  serial_object = do_get_serial_obj_id (&serial_obj_id, serial_class_mop,
					serial_name);
  if (serial_object == NULL)
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE,
	      ER_QPROC_SERIAL_NOT_FOUND, 1, serial_name);
      db_make_error (returnval, ER_QPROC_SERIAL_NOT_FOUND);
      return;
    }

  user = au_find_user (owner_name);
  if (user == NULL)
    {
      db_make_error (returnval, er_errid ());
      return;
    }

  error = au_change_serial_owner (&serial_object, user);
  if (error != NO_ERROR)
    {
      db_make_error (returnval, error);
    }
}

/*
 * au_change_trigger_owner - This changes the owning user of a trigger.
 *                           This should be called only by the DBA.
 *   return: error code
 *   trigger(in): trigger whose owner is to change
 *   owner(in):new owner
 */
int
au_change_trigger_owner (MOP trigger, MOP owner)
{
  int error = NO_ERROR;
  int save;
  DB_VALUE value;

  AU_DISABLE (save);
  if (!au_is_dba_group_member (Au_user))
    {
      error = ER_AU_DBA_ONLY;
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, error, 1,
	      "change_trigger_owner");
    }
  else
    {
      db_make_object (&value, owner);
      if ((error = obj_set (trigger, TR_ATT_OWNER, &value)) < 0)
	{
	  goto end;
	}
    }

end:
  AU_ENABLE (save);
  return (error);
}

/*
 * au_change_trigger_owner_method - Method interface to au_change_trigger_owner
 *   return: none
 *   obj(in):
 *   returnval(out): return value of this method
 *   trigger(in): trigger whose owner is to change
 *   owner(in): new owner
 */
void
au_change_trigger_owner_method (MOP obj, DB_VALUE * returnval,
				DB_VALUE * trigger, DB_VALUE * owner)
{
  MOP user, trigger_mop;
  int error;
  int ok = 0;

  db_make_null (returnval);
  if (trigger != NULL && IS_STRING (trigger)
      && !DB_IS_NULL (trigger) && db_get_string (trigger) != NULL)
    {
      if (owner != NULL && IS_STRING (owner)
	  && !DB_IS_NULL (owner) && db_get_string (owner) != NULL)
	{

	  trigger_mop = tr_find_trigger (db_get_string (trigger));
	  if (trigger_mop != NULL)
	    {
	      user = au_find_user (db_get_string (owner));
	      if (user != NULL)
		{
		  error = au_change_trigger_owner (trigger_mop, user);
		  if (error == NO_ERROR)
		    ok = 1;
		}
	    }
	}
      else
	{
	  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_AU_INVALID_USER, 1,
		  "");
	}
    }
  else
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_TR_TRIGGER_NOT_FOUND, 1,
	      "");
    }

  if (!ok)
    {
      db_make_error (returnval, er_errid ());
    }
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

/*
 * au_get_owner_method - Method interface to au_change_owner
 *   return: none
 *   obj(in):
 *   returnval(out): return value of this method
 *   class(in): class object
 */
void
au_get_owner_method (MOP obj, DB_VALUE * returnval, DB_VALUE * class_)
{
  MOP user;
  MOP classmop;
  int error = NO_ERROR;

  db_make_null (returnval);
  if (class_ != NULL && IS_STRING (class_)
      && !DB_IS_NULL (class_) && db_get_string (class_) != NULL)
    {
      classmop = sm_find_class (db_pull_string (class_));
      if (classmop != NULL)
	{
	  user = au_get_class_owner (classmop);
	  if (user != NULL)
	    {
	      db_make_object (returnval, user);
	    }
	  else
	    {
	      assert (er_errid () != NO_ERROR);
	      error = er_errid ();
	    }
	}
      else
	{
	  error = ER_AU_INVALID_CLASS;
	  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, error, 1,
		  db_get_string (class_));
	}
    }
  else
    {
      error = ER_AU_INVALID_CLASS;
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, error, 1, "");
    }

  if (error != NO_ERROR)
    {
      db_make_error (returnval, error);
    }
}

/*
 * au_check_authorization_method -
 *    return: none
 *    obj(in):
 *    returnval(out): return value of this method
 *    class(in):
 *    auth(in):
 */
void
au_check_authorization_method (MOP obj, DB_VALUE * returnval,
			       DB_VALUE * class_, DB_VALUE * auth)
{
  MOP classmop;
  int error = NO_ERROR;

  db_make_null (returnval);
  if (class_ != NULL && IS_STRING (class_)
      && !DB_IS_NULL (class_) && db_get_string (class_) != NULL)
    {

      classmop = sm_find_class (db_pull_string (class_));
      if (classmop != NULL)
	{
	  error =
	    au_check_authorization (classmop, (DB_AUTH) db_get_int (auth));
	  db_make_int (returnval, (error == NO_ERROR) ? true : false);
	}
      else
	{
	  error = ER_AU_INVALID_CLASS;
	  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, error, 1,
		  db_get_string (class_));
	}
    }
  else
    {
      error = ER_AU_INVALID_CLASS;
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, error, 1, "");
    }
}

/*
 * au_add_method_check_authorization -
 *    return: error code
 */
int
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

  smt_add_class_method (def, "check_authorization",
			"au_check_authorization_method");
  smt_assign_argument_domain (def, "check_authorization", true, NULL, 0,
			      "integer", (DB_DOMAIN *) 0);
  smt_assign_argument_domain (def, "check_authorization", true, NULL, 1,
			      "varchar(255)", (DB_DOMAIN *) 0);
  smt_assign_argument_domain (def, "check_authorization", true, NULL, 2,
			      "integer", (DB_DOMAIN *) 0);
  sm_update_class (def, NULL);

  au_grant (Au_public_user, auth, AU_EXECUTE, false);

  AU_ENABLE (save);
  return NO_ERROR;

exit_on_error:
  AU_ENABLE (save);
  return ER_FAILED;
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
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, error, 1,
	      "change_sp_owner");
    }
  else
    {
      db_make_object (&value, owner);
      if ((error = obj_set (sp, SP_ATTR_OWNER, &value)) < 0)
	{
	  goto end;
	}
    }

end:
  AU_ENABLE (save);
  return (error);
}

/*
 * au_change_sp_owner_method -
 *   return: none
 *   obj(in):
 *   returnval(in):
 *   sp(in):
 *   owner(in):
 */
void
au_change_sp_owner_method (MOP obj, DB_VALUE * returnval,
			   DB_VALUE * sp, DB_VALUE * owner)
{
  MOP user, sp_mop;
  int error;
  int ok = 0;

  db_make_null (returnval);
  if (sp != NULL && IS_STRING (sp)
      && !DB_IS_NULL (sp) && db_get_string (sp) != NULL)
    {
      if (owner != NULL && IS_STRING (owner)
	  && !DB_IS_NULL (owner) && db_get_string (owner) != NULL)
	{

	  sp_mop = jsp_find_stored_procedure (db_get_string (sp));
	  if (sp_mop != NULL)
	    {
	      user = au_find_user (db_get_string (owner));
	      if (user != NULL)
		{
		  if ((error = au_change_sp_owner (sp_mop, user)) == NO_ERROR)
		    ok = 1;
		}
	    }
	}
      else
	{
	  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_AU_INVALID_USER, 1,
		  "");
	}
    }
  else
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_SP_NOT_EXIST, 1, "");
    }

  if (!ok)
    {
      db_make_error (returnval, er_errid ());
    }
}

/*
 * au_check_user - This is used to check for a currently valid user for some
 *                 operations that are not strictly based on
 *                 any particular class.
 *    return: error code
 */
int
au_check_user (void)
{
  int error = NO_ERROR;

  if (Au_user == NULL)
    {
      error = ER_AU_INVALID_USER;
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, error, 1, "");
    }

  return (error);
}

/*
 * au_user_name - Returns the name of the current user, the string must be
 *                freed with ws_free_string (db_string_free).
 *   return: user name (NULL if error)
 *
 * Note: Note that this is what should always be used to get the active user.
 *       Au_user_name is only used as a temporary storage area during login.
 *       Once the database is open, the active user is determined by Au_user
 *       and Au_user_name doesn't necessarily track this.
 */
const char *
au_user_name (void)
{
  DB_VALUE value;
  const char *name = NULL;

  if (Au_user == NULL)
    {
      /*
       * Database hasn't been started yet, return the registered name
       * if any.
       */
      if (strlen (Au_user_name) == 0)
	{
	  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_AU_NO_USER_LOGGED_IN,
		  0);
	}
      else
	{
	  name = ws_copy_string (Au_user_name);
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
      if (obj_get (Au_user, "name", &value) == NO_ERROR)
	{
	  if (!IS_STRING (&value))
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
    }

  return name;
}

#if defined(ENABLE_UNUSED_FUNCTION)
/*
 * au_user_password - This returns the unencrypted password for the active user.
 *    return: error code
 *    buffer(in): output password buffer (must be at least 9 chars)
 *
 * Note:
 *    Note that this doesn't necessarily track the contents of
 *    Au_user_password_des_oldstyle.
 *    Note, we may only allow this to be called for the "original" user
 *    that logged in to the system.  If we allow it for all users,
 *    there is a potential hole where we could access the password
 *    while another user is temporarily active.
 */
int
au_user_password (char *buffer)
{
  int error;
  DB_VALUE value;
  int save;

  strcpy (buffer, "");

  if (Au_user == NULL)
    {
      /*
       * Database hasn't been started yet, return the registered password
       * if any. Probably don't really have to handle this condition.
       */
      return unencrypt_password (Au_user_password_des_oldstyle, 1, buffer);
    }

  AU_DISABLE (save);

  if (!(error = obj_get (Au_user, "password", &value)))
    {
      if (DB_VALUE_TYPE (&value) == DB_TYPE_OBJECT)
	{
	  if (!DB_IS_NULL (&value) && db_get_object (&value) != NULL
	      && !(error =
		   obj_get (db_get_object (&value), "password", &value)))
	    {
	      if (IS_STRING (&value))
		{
		  if (!DB_IS_NULL (&value) && db_get_string (&value) != NULL)
		    {
		      error = unencrypt_password (db_get_string (&value), 1,
						  buffer);
		    }
		}
	      pr_clear_value (&value);
	    }
	}
    }
  AU_ENABLE (save);

  return error;
}
#endif /* ENABLE_UNUSED_FUNCTION */

/*
 * CLASS ACCESSING
 */

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
is_protected_class (MOP classmop, SM_CLASS * sm_class, DB_AUTH auth)
{
  int illegal = 0;

  if (classmop == Au_authorizations_class
      || IS_CATALOG_CLASS (sm_class->header.name))
    {
      illegal =
	auth & (AU_ALTER | AU_DELETE | AU_INSERT | AU_UPDATE | AU_INDEX);
    }
  else if (sm_issystem (sm_class))
    {
      /* if the class is a system class_, can't alter */
      illegal = auth & (AU_ALTER);
    }
  return illegal;
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
check_authorization (MOP classobj, SM_CLASS * sm_class, DB_AUTH type)
{
  int error = NO_ERROR;
  AU_CLASS_CACHE *cache;
  unsigned int bits;

  /*
   * Callers generally check Au_disable already to avoid the function call.
   * Check it again to be safe, at this point, it isn't going to add anything.
   */
  if (Au_disable)
    return NO_ERROR;

  /* try to catch attempts by even the DBA to update a protected class */
  if ((sm_class->flags & SM_CLASSFLAG_SYSTEM)
      && is_protected_class (classobj, sm_class, type))
    {
      error = appropriate_error (0, type);
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, error, 0);
    }
  else
    {
      cache = (AU_CLASS_CACHE *) sm_class->auth_cache;
      if (cache == NULL)
	{
	  cache = au_install_class_cache (sm_class);
	  if (cache == NULL)
	    {
	      assert (er_errid () != NO_ERROR);
	      return er_errid ();
	    }
	}
      bits = cache->data[Au_cache_index];

      if ((bits & type) != type)
	{
	  if (bits == AU_CACHE_INVALID)
	    {
	      /* update the cache and try again */
	      error = update_cache (classobj, sm_class, cache);
	      if (error == NO_ERROR)
		{
		  bits = cache->data[Au_cache_index];
		  if ((bits & type) != type)
		    {
		      error = appropriate_error (bits, type);
		      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, error, 0);
		    }
		}
	    }
	  else
	    {
	      error = appropriate_error (bits, type);
	      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, error, 0);
	    }
	}
    }

  return error;
}

/*
 * fetch_class - Work function for au_fetch_class.
 *   return: error code
 *   op(in): class or instance MOP
 *   return_mop(out): returned class MOP
 *   return_class(out): returned class structure
 *   fetchmode(in): desired fetch/locking mode
 */
static int
fetch_class (MOP op, MOP * return_mop, SM_CLASS ** return_class,
	     AU_FETCHMODE fetchmode, FETCH_BY fetch_by)
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

  op = ws_mvcc_latest_version (op);

  classmop = NULL;
  class_ = NULL;

  if (fetch_by == BY_CLASS_MOP
      || locator_is_class (op, ((fetchmode == AU_FETCH_READ)
				? DB_FETCH_READ : DB_FETCH_WRITE)))
    {
      classmop = op;
    }
  else
    {
      classmop = ws_class_mop (op);
    }

  /* the locator_fetch_class_of_instance doesn't seem to be working right now */
#if 0
  if (classmop == NULL)
    {
      if ((error = classmop_from_instance (op, &classmop)) != NO_ERROR)
	return (error);
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
	  class_ = (SM_CLASS *) locator_fetch_class (classmop,
						     DB_FETCH_EXCLUSIVE_SCAN);
	  break;
	case AU_FETCH_WRITE:
	  class_ =
	    (SM_CLASS *) locator_fetch_class (classmop, DB_FETCH_WRITE);
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
	  class_ =
	    (SM_CLASS *) locator_fetch_class_of_instance (op, &classmop,
							  DB_FETCH_READ);
	  break;
	case AU_FETCH_SCAN:
	case AU_FETCH_EXCLUSIVE_SCAN:
	  /* AU_FETCH_SCAN, AU_FETCH_EXCLUSIVE_SCAN are allowed only for class mops. */
	  assert (0);
	  break;
	case AU_FETCH_WRITE:
	  class_ =
	    (SM_CLASS *) locator_fetch_class_of_instance (op, &classmop,
							  DB_FETCH_WRITE);
	  break;
	case AU_FETCH_UPDATE:
	  class_ =
	    (SM_CLASS *) locator_fetch_class_of_instance (op, &classmop,
							  DB_FETCH_WRITE);
	  if (class_ != NULL)
	    /*
	     * all this appreciably does is set the dirty flag in the MOP
	     * should have the "dirty after getting write lock" operation
	     * separated
	     */
	    class_ = (SM_CLASS *) locator_update_class (classmop);
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
      assert (er_errid () != NO_ERROR);
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
au_fetch_class_internal (MOP op, SM_CLASS ** class_ptr,
			 AU_FETCHMODE fetchmode, DB_AUTH type,
			 FETCH_BY fetch_by)
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
  op = ws_mvcc_latest_version (op);

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

  if (Au_disable || !(error = check_authorization (classmop, class_, type)))
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
au_fetch_class (MOP op, SM_CLASS ** class_ptr, AU_FETCHMODE fetchmode,
		DB_AUTH type)
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
au_fetch_class_by_instancemop (MOP op, SM_CLASS ** class_ptr,
			       AU_FETCHMODE fetchmode, DB_AUTH type)
{
  return au_fetch_class_internal (op, class_ptr, fetchmode, type,
				  BY_INSTANCE_MOP);
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
au_fetch_class_by_classmop (MOP op, SM_CLASS ** class_ptr,
			    AU_FETCHMODE fetchmode, DB_AUTH type)
{
  return au_fetch_class_internal (op, class_ptr, fetchmode, type,
				  BY_CLASS_MOP);
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
au_fetch_class_force (MOP op, SM_CLASS ** class_, AU_FETCHMODE fetchmode)
{
  MOP classmop;

  return (fetch_class (op, &classmop, class_, fetchmode, DONT_KNOW));
}

/*
 * au_check_authorization - This is similar to au_fetch_class except that
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
au_check_authorization (MOP op, DB_AUTH auth)
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
 * INSTANCE ACCESSING
 */

/*
 * fetch_instance - Work function for au_fetch_instance.
 *   return: error code
 *   op(in): instance MOP
 *   obj_ptr(out): returned pointer to object
 *   fetchmode(in): desired operation
 */
static int
fetch_instance (MOP op, MOBJ * obj_ptr, AU_FETCHMODE fetchmode)
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

  op = ws_mvcc_latest_version (op);
  pin = ws_pin (op, 1);
  if (op->is_vid)
    {
      switch (fetchmode)
	{
	case AU_FETCH_READ:
	  obj = vid_fetch_instance (op, DB_FETCH_READ);
	  break;
	case AU_FETCH_WRITE:
	  obj = vid_fetch_instance (op, DB_FETCH_WRITE);
	  break;
	case AU_FETCH_UPDATE:
	  obj = vid_upd_instance (op);
	  break;
	case AU_FETCH_SCAN:
	case AU_FETCH_EXCLUSIVE_SCAN:
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OBJ_INVALID_ARGUMENTS,
		  0);
	  assert (0);
	  break;
	}
    }
  else
    {
      switch (fetchmode)
	{
	case AU_FETCH_READ:
	  obj = locator_fetch_instance (op, DB_FETCH_READ);
	  break;
	case AU_FETCH_WRITE:
	  obj = locator_fetch_instance (op, DB_FETCH_WRITE);
	  break;
	case AU_FETCH_UPDATE:
	  obj = locator_update_instance (op);
	  break;
	case AU_FETCH_SCAN:
	case AU_FETCH_EXCLUSIVE_SCAN:
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OBJ_INVALID_ARGUMENTS,
		  0);
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
 * au_fetch_instance - This is the primary interface function for accessing
 *                     an instance.
 *   return: error code
 *   op(in): instance MOP
 *   obj_ptr(in):returned pointer to instance memory
 *   mode(in): access type
 *   type(in): authorization type
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
au_fetch_instance (MOP op, MOBJ * obj_ptr, AU_FETCHMODE mode, DB_AUTH type)
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

  op = ws_mvcc_latest_version (op);
  error =
    fetch_class (op, &classmop, &class_, AU_FETCH_READ, BY_INSTANCE_MOP);
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

  if (Au_disable || !(error = check_authorization (classmop, class_, type)))
    {
      error = fetch_instance (op, obj_ptr, mode);
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
 */
int
au_fetch_instance_force (MOP op, MOBJ * obj_ptr, AU_FETCHMODE fetchmode)
{
  return (fetch_instance (op, obj_ptr, fetchmode));
}


/*
 * LOGIN/LOGOUT
 */

/*
 * au_disable_passwords -
 *    return: none
 */
void
au_disable_passwords (void)
{
  Au_ignore_passwords = 1;
}

/*
 * au_set_user -
 *   return: error code
 *   newuser(in):
 */
int
au_set_user (MOP newuser)
{
  int error = NO_ERROR;
  int index;

  if (newuser != NULL && !ws_is_same_object (newuser, Au_user))
    {
      if (!(error = au_find_user_cache_index (newuser, &index, 1)))
	{

	  Au_user = newuser;
	  Au_cache_index = index;

	  /*
	   * it is important that we don't call sm_bump_local_schema_version() here
	   * because this function is called during the compilation of vclasses
	   */

	  /*
	   * Entry-level SQL specifies that the schema name is the same as
	   * the current user authorization name.  In any case, this is
	   * the place to set the current schema since the user just changed.
	   */
	  error = sc_set_current_schema (Au_user);
	}
    }
  return (error);
}

/*
 * au_perform_login - This changes the current user using the supplied
 *                    name & password.
 *   return: error code
 *   name(in): user name
 *   password(in): user password
 *
 * Note: It is called both by au_login() and au_start().
 *       Once the name/password have been validated, it calls au_set_user()
 *       to set the user object and calculate the authorization cache index.
 *       Assumes authorization has been disabled.
 */
static int
au_perform_login (const char *name, const char *password,
		  bool ignore_dba_privilege)
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
      Au_public_user = au_find_user (AU_PUBLIC_USER_NAME);
      Au_dba_user = au_find_user (AU_DBA_USER_NAME);
      if (Au_public_user == NULL || Au_dba_user == NULL)
	{
	  error = ER_AU_INCOMPLETE_AUTH;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
	}
      user = au_find_user (dbuser);
      if (user == NULL)
	{
	  error = ER_AU_INVALID_USER;
	  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, error, 1, dbuser);
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

	      if (!Au_ignore_passwords
		  && (!au_is_dba_group_member (Au_user)
		      || ignore_dba_privilege))
		{
		  pass = NULL;
		  if (!DB_IS_NULL (&value) && db_get_object (&value) != NULL)
		    {
		      if (obj_get (db_get_object (&value),
				   "password", &value))
			{
			  assert (er_errid () != NO_ERROR);
			  return er_errid ();
			}
		      if (IS_STRING (&value))
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
			  || !match_password (dbpassword,
					      db_get_string (&value)))
			{
			  error = ER_AU_INVALID_PASSWORD;
			  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, error,
				  0);
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
			  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, error,
				  0);
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

/*
 * au_login - Registers a user name and password for a database.
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
au_login (const char *name, const char *password, bool ignore_dba_privilege)
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
  if (Au_root == NULL || !BOOT_IS_CLIENT_RESTARTED ())
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
	}
      else
	{
	  /* store the password encrypted(DES and SHA1 both) so we don't
	   * have buffers lying around with the obvious passwords in it.
	   */
	  encrypt_password (password, 1, Au_user_password_des_oldstyle);
	  encrypt_password_sha1 (password, 1, Au_user_password_sha1);
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

/*
 * au_login_method - Method interface to au_login.
 *   return: none
 *   class_mop(in): class object
 *   returnval(out): return value of this method
 *   user(in): user name
 *   password(in): password
 */
void
au_login_method (MOP class_mop, DB_VALUE * returnval, DB_VALUE * user,
		 DB_VALUE * password)
{
  int error = NO_ERROR;
  char *user_name;

  if (user != NULL)
    {
      if (IS_STRING (user) && !DB_IS_NULL (user)
	  && db_get_string (user) != NULL)
	{
	  if (password != NULL && IS_STRING (password))
	    {
	      error =
		au_login (db_get_string (user), db_get_string (password),
			  false);
	    }
	  else
	    {
	      error = au_login (db_get_string (user), NULL, false);
	    }
	}
    }
  else
    {
      error = ER_AU_INVALID_USER;
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, error, 1, "");
    }

  if (error == NO_ERROR)
    {
      user_name = db_get_user_name ();
      error = clogin_user (user_name);

      if (error == NO_ERROR)
	{
	  db_make_null (returnval);
	}
      else
	{
	  db_make_error (returnval, error);
	}

      db_string_free (user_name);
    }
  else
    {
      db_make_error (returnval, error);
    }
}

/*
 * au_start - This is called during the bo_resteart initialization sequence
 *            after the database has been successfully opened
 *   return: error code
 *
 * Note: Here we initialize the authorization system by finding the system
 *       objects and validating the registered user.
 */
int
au_start (void)
{
  int error = NO_ERROR;
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
  Au_disable = 1;

  /* locate the various system classes */
  class_mop = sm_find_class (AU_ROOT_CLASS_NAME);
  if (class_mop == NULL)
    {
      error = ER_AU_NO_AUTHORIZATION;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
      return (error);
    }
  Au_authorizations_class = class_mop;

  class_mop = sm_find_class (AU_AUTH_CLASS_NAME);
  if (class_mop == NULL)
    {
      error = ER_AU_NO_AUTHORIZATION;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
      return (error);
    }
  Au_authorization_class = class_mop;

  class_mop = sm_find_class (AU_USER_CLASS_NAME);
  if (class_mop == NULL)
    {
      error = ER_AU_NO_AUTHORIZATION;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
      return (error);
    }
  Au_user_class = class_mop;

  class_mop = sm_find_class (AU_PASSWORD_CLASS_NAME);
  if (class_mop == NULL)
    {
      error = ER_AU_NO_AUTHORIZATION;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
      return (error);
    }
  Au_password_class = class_mop;

  mops = db_get_all_objects (Au_authorizations_class);
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

      Au_root = mops->op;
      db_objlist_free (mops);

      Au_public_user = au_find_user (AU_PUBLIC_USER_NAME);
      Au_dba_user = au_find_user (AU_DBA_USER_NAME);
      if (Au_public_user == NULL || Au_dba_user == NULL)
	{
	  error = ER_AU_INCOMPLETE_AUTH;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
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

	  error =
	    au_perform_login (Au_user_name, Au_user_password_des_oldstyle,
			      false);
	}
    }

  /* make sure this is off */
  Au_disable = 0;

  return (error);
}


/*
 * MIGRATION SUPPORT
 *
 * These functions provide a way to dump the authorization catalog
 * as a sequence of SQL/X statements.  When the statements are evaluated
 * by the interpreter, it will reconstruct the authorization catalog.
 */

/*
 * au_get_user_name - Shorthand function for getting name from user object.
 *                    Must remember to free the string
 *   return: user name string
 *   obj(in): user object
 */
char *
au_get_user_name (MOP obj)
{
  DB_VALUE value;
  int error;
  char *name;

  name = NULL;
  error = obj_get (obj, "name", &value);
  if (error == NO_ERROR)
    {
      if (IS_STRING (&value) && !DB_IS_NULL (&value)
	  && db_get_string (&value) != NULL)
	{
	  name = db_get_string (&value);
	}
    }
  return (name);
}

/*
 * au_export_users - Generates a sequence of add_user and add_member method
 *                   calls that when evaluated, will re-create the current
 *                   user/group hierarchy.
 *   return: error code
 *   outfp(in): output file
 */
int
au_export_users (FILE * outfp)
{
  int error;
  DB_SET *direct_groups;
  DB_VALUE value, gvalue;
  MOP user, pwd;
  int g, gcard;
  char *uname, *str, *gname;
  char passbuf[AU_MAX_PASSWORD_BUF];
  char *query;
  size_t query_size;
  DB_QUERY_RESULT *query_result;
  DB_QUERY_ERROR query_error;
  DB_VALUE user_val;
  const char *qp1 = "select [%s] from [%s];";
  char encrypt_mode = 0x00;

  query_size = strlen (qp1) + strlen (AU_USER_CLASS_NAME) * 2;
  query = (char *) malloc (query_size);
  if (query == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
	      query_size);
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  sprintf (query, qp1, AU_USER_CLASS_NAME, AU_USER_CLASS_NAME);

  error = db_compile_and_execute_local (query, &query_result, &query_error);
  /* error is row count if not negative. */
  if (error < 0)
    {
      free_and_init (query);
      return error;
    }

  while (db_query_next_tuple (query_result) == DB_CURSOR_SUCCESS)
    {
      if (db_query_get_tuple_value (query_result, 0, &user_val) != NO_ERROR)
	{
	  continue;
	}

      if (DB_IS_NULL (&user_val))
	{
	  user = NULL;
	}
      else
	{
	  user = db_get_object (&user_val);
	}

      uname = au_get_user_name (user);
      strcpy (passbuf, "");
      encrypt_mode = 0x00;

      error = obj_get (user, "password", &value);
      if (error == NO_ERROR)
	{
	  if (DB_IS_NULL (&value))
	    {
	      pwd = NULL;
	    }
	  else
	    {
	      pwd = db_get_object (&value);
	    }

	  if (pwd != NULL)
	    {
	      error = obj_get (pwd, "password", &value);
	      if (error == NO_ERROR)
		{
		  if (!DB_IS_NULL (&value) && IS_STRING (&value))
		    {
		      /*
		       * copy password string using malloc
		       * to be consistent with encrypt_password
		       */
		      str = db_pull_string (&value);
		      if (IS_ENCODED_DES (str))
			{
			  /* strip off the prefix so its readable */
			  snprintf (passbuf, AU_MAX_PASSWORD_BUF - 1, "%s",
				    str + 1);
			  encrypt_mode = ENCODE_PREFIX_DES;
			}
		      else if (IS_ENCODED_SHA1 (str))
			{
			  /* strip off the prefix so its readable */
			  snprintf (passbuf, AU_MAX_PASSWORD_BUF - 1, "%s",
				    str + 1);
			  encrypt_mode = ENCODE_PREFIX_SHA1;
			}
		      else if (strlen (str))
			{
			  /* sha1 hashing without prefix */
			  encrypt_password_sha1 (str, 0, passbuf);
			}
		      ws_free_string (str);
		    }
		}
	    }
	}

      if (error == NO_ERROR)
	{
	  if (!ws_is_same_object (user, Au_dba_user)
	      && !ws_is_same_object (user, Au_public_user))
	    {
	      if (!strlen (passbuf))
		{
		  fprintf (outfp,
			   "call [add_user]('%s', '') on class [db_root];\n",
			   uname);
		}
	      else
		{
		  fprintf (outfp,
			   "call [add_user]('%s', '') on class [db_root] to [auser];\n",
			   uname);
		  if (encrypt_mode == ENCODE_PREFIX_DES)
		    {
		      fprintf (outfp,
			       "call [set_password_encoded]('%s') on [auser];\n",
			       passbuf);
		    }
		  else
		    {
		      fprintf (outfp,
			       "call [set_password_encoded_sha1]('%s') on [auser];\n",
			       passbuf);
		    }
		}
	    }
	  else
	    {
	      if (strlen (passbuf))
		{
		  fprintf (outfp,
			   "call [find_user]('%s') on class [db_user] to [auser];\n",
			   uname);
		  if (encrypt_mode == ENCODE_PREFIX_DES)
		    {
		      fprintf (outfp,
			       "call [set_password_encoded]('%s') on [auser];\n",
			       passbuf);
		    }
		  else
		    {
		      fprintf (outfp,
			       "call [set_password_encoded_sha1]('%s') on [auser];\n",
			       passbuf);
		    }
		}
	    }
	}

      /* remember, these were allocated in the workspace */
      if (uname != NULL)
	{
	  ws_free_string (uname);
	}
    }

  /* group hierarchy */
  if (db_query_first_tuple (query_result) == DB_CURSOR_SUCCESS)
    {
      fprintf (outfp,
	       "call [find_user]('PUBLIC') on class [db_user] to [g_public];\n");
      do
	{
	  if (db_query_get_tuple_value (query_result, 0, &user_val) !=
	      NO_ERROR)
	    {
	      continue;
	    }

	  if (DB_IS_NULL (&user_val))
	    {
	      user = NULL;
	    }
	  else
	    {
	      user = db_get_object (&user_val);
	    }

	  uname = au_get_user_name (user);
	  if (uname == NULL)
	    {
	      continue;
	    }

	  if (au_get_set (user, "direct_groups", &direct_groups) != NO_ERROR)
	    {
	      ws_free_string (uname);
	      continue;
	    }

	  gcard = set_cardinality (direct_groups);
	  for (g = 0; g < gcard && !error; g++)
	    {
	      if (set_get_element (direct_groups, g, &gvalue) != NO_ERROR)
		{
		  continue;
		}

	      if (db_get_object (&gvalue) == Au_public_user)
		{
		  continue;
		}

	      error = obj_get (db_get_object (&gvalue), "name", &value);
	      if (error != NO_ERROR)
		{
		  continue;
		}

	      if (DB_IS_NULL (&value))
		{
		  gname = NULL;
		}
	      else
		{
		  gname = (char *) (db_get_string (&value));
		}

	      if (gname != NULL)
		{
		  fprintf (outfp,
			   "call [find_user]('%s') on class [db_user] to [g_%s];\n",
			   gname, gname);
		  fprintf (outfp,
			   "call [add_member]('%s') on [g_%s];\n", uname,
			   gname);
		  ws_free_string (gname);
		}
	    }

	  set_free (direct_groups);
	  ws_free_string (uname);
	}
      while (db_query_next_tuple (query_result) == DB_CURSOR_SUCCESS);
    }

  db_query_end (query_result);
  free_and_init (query);

  return (error);
}

/*
 * GRANT EXPORT
 *
 * This is in support of the authorization migration utilities.  We build
 * hierarchy of grant information and then generate a sequence of
 * SQL/X statemenets to recreate the grants.  Note that the grants have
 * to be done in order of dependencies.
 */

/*
 * make_class_grant - Create a temporary class grant structure.
 *   return: new class grant
 *   user(in): subject user
 *   cache(in): authorization bits to grant
 */
static CLASS_GRANT *
make_class_grant (CLASS_USER * user, int cache)
{
  CLASS_GRANT *grant;

  if ((grant = (CLASS_GRANT *) malloc (sizeof (CLASS_GRANT))) != NULL)
    {
      grant->next = NULL;
      grant->user = user;
      grant->cache = cache;
    }
  return (grant);
}

/*
 * make_class_user - Create a temporary class user structure.
 *   return: new class user structure
 *   user_obj(in): pointer to actual database object for this user
 */
static CLASS_USER *
make_class_user (MOP user_obj)
{
  CLASS_USER *u;

  if ((u = (CLASS_USER *) malloc (sizeof (CLASS_USER))) != NULL)
    {
      u->next = NULL;
      u->obj = user_obj;
      u->grants = NULL;

      /*
       * This authorization of this user class structure would normally
       * be filled in by examining authorizations granted by other users.
       * The DBA user is special in that it should have full authorization
       * without being granted it by any users.  Therefore we need to set
       * the authorization explicitly before any code checks it.
       */
      if (ws_is_same_object (user_obj, Au_dba_user))
	{
	  u->available_auth = AU_FULL_AUTHORIZATION;
	}
      else
	{
	  u->available_auth = 0;
	}
    }
  return (u);
}

/*
 * free_class_grants - Frees list of temporary class grant structures.
 *   return: none
 *   grants(in): list of class grant structures
 */
static void
free_class_grants (CLASS_GRANT * grants)
{
  CLASS_GRANT *g, *next;

  for (g = grants, next = NULL; g != NULL; g = next)
    {
      next = g->next;
      free_and_init (g);
    }
}

/*
 * free_class_users - Frees list of class user objects.
 *   return: none
 *   users(in): class user list
 */
static void
free_class_users (CLASS_USER * users)
{
  CLASS_USER *u, *next;

  for (u = users, next = NULL; u != NULL; u = next)
    {
      next = u->next;
      free_class_grants (u->grants);
      free_and_init (u);
    }
}

/*
 * find_or_add_user - Adds an entry in the user list of a class authorization
 *                    structure for the user object.
 *   return: class user structures
 *   auth(in):class authorization state
 *   user_obj(in):database user object to add
 *
 * Note: If there is already an entry in the list, it returns the found entry
 */
static CLASS_USER *
find_or_add_user (CLASS_AUTH * auth, MOP user_obj)
{
  CLASS_USER *u, *last;

  for (u = auth->users, last = NULL;
       u != NULL && !ws_is_same_object (u->obj, user_obj); u = u->next)
    {
      last = u;
    }

  if (u == NULL)
    {
      u = make_class_user (user_obj);
      if (last == NULL)
	{
	  auth->users = u;
	}
      else
	{
	  last->next = u;
	}
    }
  return (u);
}

/*
 * add_class_grant - Makes an entry in the class authorization state
 *                   for a desired grant.
 *   return: error code
 *   auth(in): class authorization state
 *   source(in): source user object
 *   user(in): subject user object
 *   cache(in): authorization cache bits
 */
static int
add_class_grant (CLASS_AUTH * auth, MOP source, MOP user, int cache)
{
  CLASS_USER *u, *gu;
  CLASS_GRANT *g;

  u = find_or_add_user (auth, source);

  for (g = u->grants; g != NULL && !ws_is_same_object (g->user->obj, user);
       g = g->next)
    ;

  if (g == NULL)
    {
      if (!ws_is_same_object (source, user))
	{
	  gu = find_or_add_user (auth, user);
	  g = make_class_grant (gu, cache);
	  if (g == NULL)
	    {
	      assert (er_errid () != NO_ERROR);
	      return er_errid ();
	    }
	  g->next = u->grants;
	  u->grants = g;
	}
    }
  else
    {
      /*
       * this shouldn't happen, multiple grants from source should already have
       * been combined
       */
      g->cache |= cache;
    }
  return NO_ERROR;
}

/*
 * build_class_grant_list - Adds grant entries in cl_auth for every grant entry
 *                          found in the authorization catalog for
 *                          the class "class".
 *   return: error code
 *   cl_auth(in):  class authorization state
 *   class(in): class object
 *
 * Note: The db_root class used to have a user attribute which was a set
 *       containing the object-id for all users.  The users attribute has been
 *       eliminated for performance reasons.  A query on the db_user class is
 *       now used to find all users.
 */
static int
build_class_grant_list (CLASS_AUTH * cl_auth, MOP class_mop)
{
  int error;
  MOP user, auth, source;
  DB_SET *grants;
  DB_VALUE value;
  int j, gsize, cache;
  char *query;
  size_t query_size;
  DB_QUERY_RESULT *query_result;
  DB_QUERY_ERROR query_error;
  DB_VALUE user_val;
  const char *qp1 = "select [%s] from [%s];";

  query_size = strlen (qp1) + strlen (AU_USER_CLASS_NAME) * 2;
  query = (char *) malloc (query_size);
  if (query == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
	      query_size);
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  sprintf (query, qp1, AU_USER_CLASS_NAME, AU_USER_CLASS_NAME);

  error = db_compile_and_execute_local (query, &query_result, &query_error);
  if (error < 0)
    /* error is row count if not negative. */
    {
      free_and_init (query);
      return error;
    }

  while (db_query_next_tuple (query_result) == DB_CURSOR_SUCCESS)
    {
      if (db_query_get_tuple_value (query_result, 0, &user_val) == NO_ERROR)
	{
	  if (DB_IS_NULL (&user_val))
	    {
	      user = NULL;
	    }
	  else
	    {
	      user = db_get_object (&user_val);
	    }

	  error = au_get_object (user, "authorization", &auth);
	  /* ignore the deleted object errors */
	  if (error != NO_ERROR)
	    {
	      if (error == ER_HEAP_UNKNOWN_OBJECT)
		error = NO_ERROR;
	    }
	  else
	    {
	      if ((error = get_grants (auth, &grants, 1)) == NO_ERROR)
		{
		  gsize = set_size (grants);
		  for (j = 0; j < gsize; j += GRANT_ENTRY_LENGTH)
		    {
		      error =
			set_get_element (grants, GRANT_ENTRY_CLASS (j),
					 &value);
		      if (error == NO_ERROR
			  && DB_VALUE_TYPE (&value) == DB_TYPE_OBJECT
			  && db_get_object (&value) == class_mop)
			{
			  error =
			    set_get_element (grants, GRANT_ENTRY_SOURCE (j),
					     &value);
			  if (error == NO_ERROR
			      && DB_VALUE_TYPE (&value) == DB_TYPE_OBJECT
			      && !DB_IS_NULL (&value)
			      && (source = db_get_object (&value)) != NULL)
			    {
			      error =
				set_get_element (grants,
						 GRANT_ENTRY_CACHE (j),
						 &value);
			      if (error == NO_ERROR)
				{
				  cache = db_get_int (&value);
				  error =
				    add_class_grant (cl_auth, source, user,
						     cache);
				}
			    }
			}
		    }
		  set_free (grants);
		}
	    }
	}			/* if */
    }				/* while */

  db_query_end (query_result);
  free_and_init (query);

  return (error);
}

/*
 * issue_grant_statement - Generates an SQL/X "grant" statement.
 *   return: none
 *   fp(in): output file
 *   auth(in): class authorization state
 *   grant(in): desired grant
 *   authbits(in): specific authorization to grant
 *   quoted_id_flag(in):
 */
static void
issue_grant_statement (FILE * fp, CLASS_AUTH * auth, CLASS_GRANT * grant,
		       int authbits)
{
  const char *gtype, *classname;
  char *username;
  int typebit;

  typebit = authbits & AU_TYPE_MASK;
  switch (typebit)
    {
    case AU_SELECT:
      gtype = "SELECT";
      break;
    case AU_INSERT:
      gtype = "INSERT";
      break;
    case AU_UPDATE:
      gtype = "UPDATE";
      break;
    case AU_DELETE:
      gtype = "DELETE";
      break;
    case AU_ALTER:
      gtype = "ALTER";
      break;
    case AU_INDEX:
      gtype = "INDEX";
      break;
    case AU_EXECUTE:
      gtype = "EXECUTE";
      break;
    default:
      gtype = "???";
      break;
    }
  classname = sm_class_name (auth->class_mop);
  username = au_get_user_name (grant->user->obj);

  fprintf (fp, "GRANT %s ON ", gtype);
  fprintf (fp, "[%s]", classname);

  if (username != NULL)
    {
      fprintf (fp, " TO [%s]", username);
    }
  else
    {
      fprintf (fp, " TO %s", "???");
    }

  if (authbits & (typebit << AU_GRANT_SHIFT))
    {
      fprintf (fp, " WITH GRANT OPTION");
    }
  fprintf (fp, ";\n");

  ws_free_string (username);
}

/*
 * class_grant_loop - Makes a pass on the authorization user list and
 *                    issues grant statements for any users that are able.
 *                    Returns the number of statements issued
 *   return: number of statements issued
 *   auth(in): class authorization state
 *   outfp(in): output file
 *   quoted_id_flag(in):
 *
 * Note:
 * If this resturns zero and the user list is not empty, it indicates
 * that there are illegal grants in the hierarchy that were not rooted
 * in the class owner object.
 *
 * It would likely be more efficient if rather than making a full pass
 * on the list we evaluate the first node in the list and then recursively
 * evaluate every mode affected by the first evaluation.  If the first
 * node results in no evaluations, we move to the next node in the list.
 *
 * This will tend to get grants to come out "depth first" which may be
 * more logical when examining the resulting statements.  It will probably
 * result in fewer traversals of the user list as well ?
 *
 * TODO : LP64
 */
static int
class_grant_loop (CLASS_AUTH * auth, FILE * outfp)
{
#define AU_MIN_BIT 1		/* AU_SELECT */
#define AU_MAX_BIT 0x40		/* AU_EXECUTE */

  CLASS_USER *user;
  CLASS_GRANT *grant, *prev_grant, *next_grant;
  int statements = 0;
  int mask, authbits;

  for (user = auth->users; user != NULL; user = user->next)
    {
      for (grant = user->grants, prev_grant = NULL, next_grant = NULL;
	   grant != NULL; grant = next_grant)
	{
	  next_grant = grant->next;
	  mask = AU_SELECT;
	  for (mask = AU_MIN_BIT; mask <= AU_MAX_BIT; mask = mask << 1)
	    {
	      if (grant->cache & mask)
		{
		  /* combine auth type & grant option bit */
		  authbits = mask | (grant->cache & (mask << AU_GRANT_SHIFT));
		  /*
		   * if the user has these same bits available,
		   * issue the grant
		   */
		  if ((user->available_auth & authbits) == authbits)
		    {
		      issue_grant_statement (outfp, auth, grant, authbits);
		      /* turn on grant bits in the granted user */
		      grant->user->available_auth |= authbits;
		      /* turn off the pending grant bits in granting user */
		      grant->cache &= ~authbits;
		      statements++;
		    }
		}
	    }
	  if (grant->cache == 0)
	    {
	      /* no more grants, remove it from the list */
	      if (prev_grant == NULL)
		{
		  user->grants = grant->next;
		}
	      else
		{
		  prev_grant->next = grant->next;
		}
	      grant->next = NULL;
	      free_class_grants (grant);
	    }
	  else
	    {
	      prev_grant = grant;
	    }
	}
      /*
       * could remove user from the list but can't free it because
       * structure may be referenced by a grant inside another user
       */
    }
  return (statements);
}

/*
 * au_export_grants() - Issues a sequence of SQL/X grant statements related
 *                      to the given class.
 *   return: error code
 *   outfp(in): output file
 *   class_mop(in): class of interest
 *   quoted_id_flag(in):
 */
int
au_export_grants (FILE * outfp, MOP class_mop)
{
  int error = NO_ERROR;
  CLASS_AUTH cl_auth;
  CLASS_USER *u;
  int statements, ecount;
  char *uname;

  cl_auth.class_mop = class_mop;
  cl_auth.owner = au_get_class_owner (class_mop);
  cl_auth.users = NULL;

  /* make an entry for the owner with complete authorization */
  u = find_or_add_user (&cl_auth, cl_auth.owner);
  u->available_auth = AU_FULL_AUTHORIZATION;

  /* add entries for the other users with authorization on this class */
  error = build_class_grant_list (&cl_auth, class_mop);
  if (error == NO_ERROR)
    {
      /* loop through the grant list, issuing grant statements */
      while ((statements = class_grant_loop (&cl_auth, outfp)));

      for (u = cl_auth.users, ecount = 0; u != NULL; u = u->next)
	{
	  if (u->grants != NULL)
	    {
	      uname = au_get_user_name (u->obj);

	      /*
	       * should this be setting an error condition ?
	       * for now, leave a comment in the output file
	       */
	      fprintf (outfp, "/*");
	      fprintf (outfp,
		       msgcat_message (MSGCAT_CATALOG_CUBRID,
				       MSGCAT_SET_AUTHORIZATION,
				       MSGCAT_AUTH_GRANT_DUMP_ERROR), uname);
	      fprintf (outfp, "*/\n");
	      ws_free_string (uname);
	      ecount++;
	    }
	}
      if (ecount)
	{
	  error = ER_GENERIC_ERROR;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
	}
    }

  free_class_users (cl_auth.users);

  return (error);
}


/*
 * DEBUGGING FUNCTIONS
 */

/*
 * au_print_cache() -
 *   return: none
 *   cache(in):
 *   fp(in):
 */
static void
au_print_cache (int cache, FILE * fp)
{
  int i, mask, auth, option;

  if (cache < 0)
    {
      fprintf (fp, msgcat_message (MSGCAT_CATALOG_CUBRID,
				   MSGCAT_SET_AUTHORIZATION,
				   MSGCAT_AUTH_INVALID_CACHE));
    }
  else
    {
      for (i = 0; i < 7; i++)
	{
	  mask = 1 << i;
	  auth = cache & mask;
	  option = cache & (mask << AU_GRANT_SHIFT);
	  if (option)
	    fprintf (fp, "*");
	  if (auth)
	    fprintf (fp, "%s ", auth_type_name[i]);
	}
    }
  fprintf (fp, "\n");
}

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
  fprintf (fp, msgcat_message (MSGCAT_CATALOG_CUBRID,
			       MSGCAT_SET_AUTHORIZATION,
			       MSGCAT_AUTH_CLASS_NAME),
	   sm_class_name (db_get_object (&value)));
  fprintf (fp, " ");

  set_get_element (grants, GRANT_ENTRY_SOURCE (grant_index), &value);
  obj_get (db_get_object (&value), "name", &value);

  fprintf (fp, msgcat_message (MSGCAT_CATALOG_CUBRID,
			       MSGCAT_SET_AUTHORIZATION,
			       MSGCAT_AUTH_FROM_USER),
	   db_get_string (&value));

  pr_clear_value (&value);

  set_get_element (grants, GRANT_ENTRY_CACHE (grant_index), &value);
  au_print_cache (db_get_int (&value), fp);
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
      fprintf (fp, msgcat_message (MSGCAT_CATALOG_CUBRID,
				   MSGCAT_SET_AUTHORIZATION,
				   MSGCAT_AUTH_USER_TITLE),
	       db_get_string (&value));
      pr_clear_value (&value);
    }
  else
    {
      fprintf (fp, msgcat_message (MSGCAT_CATALOG_CUBRID,
				   MSGCAT_SET_AUTHORIZATION,
				   MSGCAT_AUTH_UNDEFINED_USER));
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

      error =
	db_compile_and_execute_local (query, &query_result, &query_error);
      /* error is row count if not negative. */
      if (error > 0)
	{
	  while (db_query_next_tuple (query_result) == DB_CURSOR_SUCCESS)
	    {
	      if (db_query_get_tuple_value (query_result, 0, &user_val) ==
		  NO_ERROR)
		{
		  user = db_get_object (&user_val);
		  if (au_get_object (user, "authorization", &auth) ==
		      NO_ERROR)
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
  DB_SET *groups;
  MOP auth;
  int i, card;

  if (obj_get (user, "name", &value) == NO_ERROR)
    {
      fprintf (fp, msgcat_message (MSGCAT_CATALOG_CUBRID,
				   MSGCAT_SET_AUTHORIZATION,
				   MSGCAT_AUTH_USER_NAME),
	       db_get_string (&value));
      pr_clear_value (&value);
    }

  groups = NULL;
  if (au_get_set (user, "direct_groups", &groups) == NO_ERROR)
    {
      fprintf (fp, msgcat_message (MSGCAT_CATALOG_CUBRID,
				   MSGCAT_SET_AUTHORIZATION,
				   MSGCAT_AUTH_USER_DIRECT_GROUPS));
      card = set_cardinality (groups);
      for (i = 0; i < card; i++)
	{
	  if (set_get_element (groups, i, &value) == NO_ERROR)
	    {
	      if (obj_get (db_get_object (&value), "name", &value) ==
		  NO_ERROR)
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
      fprintf (fp, msgcat_message (MSGCAT_CATALOG_CUBRID,
				   MSGCAT_SET_AUTHORIZATION,
				   MSGCAT_AUTH_USER_GROUPS));
      card = set_cardinality (groups);
      for (i = 0; i < card; i++)
	{
	  if (set_get_element (groups, i, &value) == NO_ERROR)
	    {
	      if (obj_get (db_get_object (&value), "name", &value) ==
		  NO_ERROR)
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

#if defined(ENABLE_UNUSED_FUNCTION)
/*
 * au_print_class_auth() - Dumps authorization information for
 *                         a particular class.
 *   return: none
 *   class(in): class object
 *
 * Note: Used by the test program, should pass in a file pointer here !
 *       The db_root class used to have a user attribute which was a set
 *       containing the object-id for all users.  The users attribute has been
 *       eliminated for performance reasons.  A query on the db_user class is
 *       new used to find all users.
 */
void
au_print_class_auth (MOP class_mop)
{
  MOP user;
  DB_SET *grants;
  int j, title, gsize;
  DB_VALUE element, value;
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

      error =
	db_compile_and_execute_local (query, &query_result, &query_error);
      /* error is row count if not negative. */
      if (error > 0)
	{
	  while (db_query_next_tuple (query_result) == DB_CURSOR_SUCCESS)
	    {
	      if (db_query_get_tuple_value (query_result, 0, &user_val) ==
		  NO_ERROR)
		{
		  user = db_get_object (&user_val);
		  title = 0;
		  obj_get (user, "authorization", &value);
		  get_grants (db_get_object (&value), &grants, 1);
		  gsize = set_size (grants);
		  for (j = 0; j < gsize; j += GRANT_ENTRY_LENGTH)
		    {
		      set_get_element (grants, GRANT_ENTRY_CLASS (j),
				       &element);
		      if (db_get_object (&element) == class_mop)
			{
			  if (!title)
			    {
			      obj_get (user, "name", &value);
			      if (db_get_string (&value) != NULL)
				{
				  fprintf (stdout,
					   msgcat_message
					   (MSGCAT_CATALOG_CUBRID,
					    MSGCAT_SET_AUTHORIZATION,
					    MSGCAT_AUTH_USER_NAME2),
					   db_get_string (&value));
				}
			      pr_clear_value (&value);
			      title = 1;
			    }
			  au_print_grant_entry (grants, j, stdout);
			}
		    }
		  set_free (grants);
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
#endif /* ENABLE_UNUSED_FUNCTION */

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
  char *query;
  DB_QUERY_RESULT *query_result;
  DB_QUERY_ERROR query_error;
  int error = NO_ERROR;
  DB_VALUE user_val;
  const char *qp1 = "select [%s] from [%s];";

  /* NOTE: We should be getting the real user name here ! */

  fprintf (fp, msgcat_message (MSGCAT_CATALOG_CUBRID,
			       MSGCAT_SET_AUTHORIZATION,
			       MSGCAT_AUTH_CURRENT_USER), Au_user_name);

  query = (char *) malloc (strlen (qp1) + strlen (AU_USER_CLASS_NAME) * 2);

  if (query)
    {
      sprintf (query, qp1, AU_USER_CLASS_NAME, AU_USER_CLASS_NAME);

      error =
	db_compile_and_execute_local (query, &query_result, &query_error);
      /* error is row count if not negative. */
      if (error > 0)
	{
	  fprintf (fp, msgcat_message (MSGCAT_CATALOG_CUBRID,
				       MSGCAT_SET_AUTHORIZATION,
				       MSGCAT_AUTH_ROOT_USERS));
	  while (db_query_next_tuple (query_result) == DB_CURSOR_SUCCESS)
	    {
	      if (db_query_get_tuple_value (query_result, 0, &user_val) ==
		  NO_ERROR)
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
		  if (db_query_get_tuple_value (query_result, 0, &user_val) ==
		      NO_ERROR)
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

  fprintf (fp, msgcat_message (MSGCAT_CATALOG_CUBRID,
			       MSGCAT_SET_AUTHORIZATION,
			       MSGCAT_AUTH_AUTH_TITLE));
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
 * au_describe_user_method() - Method interface to au_dump_user.
 *                             Can only be called when using the csql
 *                             interpreter since it dumps to stdout
 *   return: none
 *   user(in): user object
 *   returnval(out): return value of this method
 */
void
au_describe_user_method (MOP user, DB_VALUE * returnval)
{
  db_make_null (returnval);
  if (user != NULL)
    {
      au_dump_user (user, stdout);
    }
}

/*
 * au_info_method() - Method interface for authorization dump utilities.
 *   return: none
 *   class_mop(in): class object
 *   returnval(out): return value of this method
 *   info(in):
 *
 * Note: This should be conditionalized so it knows what kind of environment
 *       this is (csql, esql).
 *       For now this is documented to only work within csql because it dumps
 *       to stdout.
 *       There are some hidden parameters that trigger other dump routines.
 *       This is a convenient hook for these things until we
 *       have a more formal way to call them (if we ever do).  These
 *       are not documented in the user manual.
 */
void
au_info_method (MOP class_mop, DB_VALUE * returnval, DB_VALUE * info)
{
  db_make_null (returnval);

  if (info != NULL && IS_STRING (info)
      && !DB_IS_NULL (info) && db_get_string (info) != NULL)
    {
      /* this dumps stuff to stdout */
      help_print_info (db_get_string (info), stdout);
    }
}

/*
 * au_describe_root_method() - Method interface for authorization dump
 *                             utilities
 *   return: none
 *   class_mop(in): class object
 *   returnval(out): return value of this method
 *   info(in):
 *
 * Note: This should be conditionalized so it knows what kind of environment
 *       this is (csql, esql).  For now this is documented to
 *       only work within csql because it dumps to stdout.
 *       There are some hidden parameters that trigger other dump routines.
 *       This is a convenient hook for these things until we
 *       have a more formal way to call them (if we ever do).  These
 *       are not documented in the user manual.
 */
void
au_describe_root_method (MOP class_mop, DB_VALUE * returnval, DB_VALUE * info)
{
  db_make_null (returnval);

  if (info == NULL)
    {
      au_dump ();
    }
  else
    {
      /*
       * temporary, pass this through for older databases that still
       * have the "info" method pointing to this function
       */
      au_info_method (class_mop, returnval, info);
    }
}


/*
 * AUTHORIZATION CLASSES
 */

/*
 * au_link_static_methods() - Called during the restart sequence to statically
 *                            link the authorization methods.
 *    return: none
 */
void
au_link_static_methods (void)
{
  db_link_static_methods (&au_static_links[0]);
}

/*
 * au_install() - This is used to initialize the authorization system in a
 *                freshly created database.
 *                It should only be called within the createdb tool.
 *   return: error code
 */
int
au_install (void)
{
  MOP root = NULL, user = NULL, pass = NULL, auth = NULL, old = NULL;
  SM_TEMPLATE *def;
  int exists, save, index;

  AU_DISABLE (save);

  /*
   * create the system authorization objects, add attributes later since they
   * have domain dependencies
   */
  root = db_create_class (AU_ROOT_CLASS_NAME);
  user = db_create_class (AU_USER_CLASS_NAME);
  pass = db_create_class (AU_PASSWORD_CLASS_NAME);
  auth = db_create_class (AU_AUTH_CLASS_NAME);
  old = db_create_class (AU_OLD_ROOT_CLASS_NAME);
  if (root == NULL || user == NULL || pass == NULL || auth == NULL
      || old == NULL)
    {
      goto exit_on_error;
    }

  sm_mark_system_class (root, 1);
  sm_mark_system_class (user, 1);
  sm_mark_system_class (pass, 1);
  sm_mark_system_class (auth, 1);
  sm_mark_system_class (old, 1);

  /*
   * Authorization root, might not need this if we restrict the generation of
   * user and  group objects but could be useful in other ways - nice to
   * have the methods here for adding/dropping user
   */
  def = smt_edit_class_mop (root, AU_ALTER);
  if (def == NULL)
    {
      goto exit_on_error;
    }
  smt_add_attribute (def, "triggers", "sequence of (string, object)",
		     (DB_DOMAIN *) 0);
  smt_add_attribute (def, "charset", "integer", NULL);
  smt_add_attribute (def, "lang", "string", NULL);


  /* need signatures for these ! */
  smt_add_class_method (def, "add_user", "au_add_user_method");
  smt_add_class_method (def, "drop_user", "au_drop_user_method");

  smt_add_class_method (def, "find_user", "au_find_user_method");
  smt_assign_argument_domain (def, "find_user", true, NULL, 0, "string",
			      (DB_DOMAIN *) 0);

  smt_add_class_method (def, "print_authorizations",
			"au_describe_root_method");
  smt_add_class_method (def, "info", "au_info_method");
  smt_add_class_method (def, "change_owner", "au_change_owner_method");
  smt_add_class_method (def, "change_trigger_owner",
			"au_change_trigger_owner_method");
  smt_add_class_method (def, "get_owner", "au_get_owner_method");
  smt_add_class_method (def, "change_sp_owner", "au_change_sp_owner_method");
  sm_update_class (def, NULL);

  /*
   * temporary support for the old name, need to migrate
   * users over to db_root
   */
  def = smt_edit_class_mop (old, AU_ALTER);
  if (def == NULL)
    {
      goto exit_on_error;
    }
  smt_add_class_method (def, "add_user", "au_add_user_method");
  smt_add_class_method (def, "drop_user", "au_drop_user_method");

  smt_add_class_method (def, "find_user", "au_find_user_method");
  smt_assign_argument_domain (def, "find_user", true, NULL, 0, "string",
			      (DB_DOMAIN *) 0);

  smt_add_class_method (def, "print_authorizations",
			"au_describe_root_method");
  smt_add_class_method (def, "info", "au_info_method");
  smt_add_class_method (def, "change_owner", "au_change_owner_method");
  smt_add_class_method (def, "change_trigger_owner",
			"au_change_trigger_owner_method");
  smt_add_class_method (def, "get_owner", "au_get_owner_method");
  sm_update_class (def, NULL);

  /* User/group objects */
  def = smt_edit_class_mop (user, AU_ALTER);
  if (def == NULL)
    {
      goto exit_on_error;
    }
  smt_add_attribute (def, "name", "string", (DB_DOMAIN *) 0);
  smt_add_attribute (def, "id", "integer", (DB_DOMAIN *) 0);
  smt_add_attribute (def, "password", AU_PASSWORD_CLASS_NAME,
		     (DB_DOMAIN *) 0);
  smt_add_attribute (def, "direct_groups", "set of (db_user)",
		     (DB_DOMAIN *) 0);
  smt_add_attribute (def, "groups", "set of (db_user)", (DB_DOMAIN *) 0);
  smt_add_attribute (def, "authorization", AU_AUTH_CLASS_NAME,
		     (DB_DOMAIN *) 0);
  smt_add_attribute (def, "triggers", "sequence of object", (DB_DOMAIN *) 0);
  /* need signatures for these */
  smt_add_method (def, "set_password", "au_set_password_method");
  smt_add_method (def, "set_password_encoded",
		  "au_set_password_encoded_method");
  smt_add_method (def, "set_password_encoded_sha1",
		  "au_set_password_encoded_sha1_method");
  smt_add_method (def, "add_member", "au_add_member_method");
  smt_add_method (def, "drop_member", "au_drop_member_method");
  smt_add_method (def, "print_authorizations", "au_describe_user_method");
  smt_add_class_method (def, "add_user", "au_add_user_method");
  smt_add_class_method (def, "drop_user", "au_drop_user_method");

  smt_add_class_method (def, "find_user", "au_find_user_method");
  smt_assign_argument_domain (def, "find_user", true, NULL, 0, "string",
			      (DB_DOMAIN *) 0);
  smt_add_class_method (def, "login", "au_login_method");
  sm_update_class (def, NULL);

  /* Add Index */
  {
    const char *names[] = { "name", NULL };
    int ret_code;

    ret_code = db_add_constraint (user, DB_CONSTRAINT_INDEX, NULL, names, 0);

    if (ret_code)
      {
	goto exit_on_error;
      }
  }

  /* Password objects */
  def = smt_edit_class_mop (pass, AU_ALTER);
  if (def == NULL)
    {
      goto exit_on_error;
    }
  smt_add_attribute (def, "password", "string", (DB_DOMAIN *) 0);
  sm_update_class (def, NULL);

  /*
   * Authorization object, the grant set could go directly in the user object
   * but it might be better to keep it separate in order to use the special
   * read-once lock for the authorization object only.
   */

  def = smt_edit_class_mop (auth, AU_ALTER);
  if (def == NULL)
    {
      goto exit_on_error;
    }
  smt_add_attribute (def, "owner", AU_USER_CLASS_NAME, (DB_DOMAIN *) 0);
  smt_add_attribute (def, "grants", "sequence", (DB_DOMAIN *) 0);
  sm_update_class (def, NULL);

  /* Create the single authorization root object */
  Au_root = obj_create (root);
  if (Au_root == NULL)
    {
      goto exit_on_error;
    }

  /* create the DBA user and assign ownership of the system classes */
  Au_dba_user = au_add_user ("DBA", &exists);
  if (Au_dba_user == NULL)
    {
      goto exit_on_error;
    }

  /* establish the DBA as the current user */
  if (au_find_user_cache_index (Au_dba_user, &index, 0) != NO_ERROR)
    {
      goto exit_on_error;
    }
  Au_user = Au_dba_user;
  Au_cache_index = index;

  AU_SET_USER (Au_dba_user);

  au_change_owner (root, Au_user);
  au_change_owner (old, Au_user);
  au_change_owner (user, Au_user);
  au_change_owner (pass, Au_user);
  au_change_owner (auth, Au_user);

  /* create the PUBLIC user */
  Au_public_user = au_add_user ("PUBLIC", &exists);
  if (Au_public_user == NULL)
    {
      goto exit_on_error;
    }

  /*
   * grant browser access to the authorization objects
   * note that the password class cannot be read by anyone except the DBA
   */
  au_grant (Au_public_user, root, (DB_AUTH) (AU_SELECT | AU_EXECUTE), false);
  au_grant (Au_public_user, old, (DB_AUTH) (AU_SELECT | AU_EXECUTE), false);
  au_grant (Au_public_user, user, AU_SELECT, false);
  au_grant (Au_public_user, user, (DB_AUTH) (AU_SELECT | AU_EXECUTE), false);
  au_grant (Au_public_user, auth, AU_SELECT, false);

  au_add_method_check_authorization ();

  AU_ENABLE (save);
  return NO_ERROR;

exit_on_error:
  if (Au_public_user != NULL)
    {
      au_drop_user (Au_public_user);
      Au_public_user = NULL;
    }
  if (Au_dba_user != NULL)
    {
      au_drop_user (Au_dba_user);
      Au_dba_user = NULL;
    }
  if (Au_root != NULL)
    {
      obj_delete (Au_root);
      Au_root = NULL;
    }
  if (old != NULL)
    {
      db_drop_class (old);
    }
  if (auth != NULL)
    {
      db_drop_class (auth);
    }
  if (pass != NULL)
    {
      db_drop_class (pass);
    }
  if (user != NULL)
    {
      db_drop_class (user);
    }
  if (root != NULL)
    {
      db_drop_class (root);
    }

  AU_ENABLE (save);
  return (er_errid () == NO_ERROR ? ER_FAILED : er_errid ());
}


/*
 * RESTART/SHUTDOWN
 */

/*
 * au_init() - This is called by bo_restart when the database
 *             is being restarted.
 *             It must only be called once.
 *   return: none
 */
void
au_init (void)
{
  Au_root = NULL;
  Au_authorizations_class = NULL;
  Au_authorization_class = NULL;
  Au_user_class = NULL;
  Au_password_class = NULL;

  Au_user = NULL;
  Au_public_user = NULL;
  Au_dba_user = NULL;
  Au_disable = 1;

  init_caches ();
}

/*
 * au_final() - Called during the bo_shutdown sequence.
 *   return: none
 */
void
au_final (void)
{
  Au_root = NULL;
  Au_authorizations_class = NULL;
  Au_authorization_class = NULL;
  Au_user_class = NULL;
  Au_password_class = NULL;

  Au_user = NULL;
  Au_public_user = NULL;
  Au_dba_user = NULL;
  Au_disable = 1;

  /*
   * could remove the static links here but it isn't necessary and
   * we may need them again the next time we restart
   */

  flush_caches ();
}

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
  AU_CLASS_CACHE *cache;
  unsigned int bits = 0;
  int error = NO_ERROR;

  if (!Au_disable)
    {
      if (mop == NULL)
	{
	  return ER_FAILED;
	}

      class_ = (SM_CLASS *) mop->object;

      cache = (AU_CLASS_CACHE *) class_->auth_cache;
      if (cache == NULL)
	{
	  cache = au_install_class_cache (class_);
	  if (cache == NULL)
	    {
	      assert (er_errid () != NO_ERROR);
	      return er_errid ();
	    }
	}

      bits = cache->data[Au_cache_index];
      if (bits == AU_CACHE_INVALID)
	{
	  error = update_cache (mop, class_, cache);
	  if (error == NO_ERROR)
	    {
	      bits = cache->data[Au_cache_index];
	    }
	}
      *auth = bits;
    }

  return error;
}

/*
 * get_attribute_number - attribute number of the given attribute/class
 *   return:
 *   arg1(in):
 *   arg2(in):
 */
void
get_attribute_number (DB_OBJECT * target, DB_VALUE * result,
		      DB_VALUE * attr_name)
{
  int attrid, shared;
  DB_DOMAIN *dom;

  db_make_null (result);

  if (DB_VALUE_TYPE (attr_name) != DB_TYPE_STRING)
    return;

  /* we will only look for regular attributes and not class attributes.
   * this is a limitation of this method.
   */
  if (sm_att_info (target, db_get_string (attr_name),
		   &attrid, &dom, &shared, 0 /* non-class attrs */ )
      < 0)
    return;

  db_make_int (result, attrid);
}


/*
 * au_disable - set Au_disable true
 *   return: original Au_disable value
 */
int
au_disable (void)
{
  int save = Au_disable;
  Au_disable = 1;
  return save;
}

/*
 * au_enable - restore Au_disable
 *   return:
 *   save(in): original Au_disable value
 */
void
au_enable (int save)
{
  Au_disable = save;
}

/*
 * au_public_user
 *   return: Au_public_user
 */
MOP
au_get_public_user (void)
{
  return Au_public_user;
}

/*
 * au_public_user
 *   return: Au_public_user
 */
MOP
au_get_dba_user (void)
{
  return Au_dba_user;
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
  MOP creator;
  DB_SET *groups;
  int ret_val;

  ret_val = db_get (serial_object, "owner", &creator_val);

  if (ret_val != NO_ERROR || DB_IS_NULL (&creator_val))
    {
      return ret_val;
    }

  creator = DB_GET_OBJECT (&creator_val);

  ret_val = ER_QPROC_CANNOT_UPDATE_SERIAL;

  if (ws_is_same_object (creator, Au_user)
      || au_is_dba_group_member (Au_user))
    {
      ret_val = NO_ERROR;
    }
  else if (au_get_set (Au_user, "groups", &groups) == NO_ERROR)
    {
      if (set_ismember (groups, &creator_val))
	{
	  ret_val = NO_ERROR;
	}
      set_free (groups);
    }

  if (ret_val != NO_ERROR)
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ret_val, 0);
    }

  pr_clear_value (&creator_val);

  return ret_val;
}

const char *
au_get_public_user_name (void)
{
  return AU_PUBLIC_USER_NAME;
}

const char *
au_get_user_class_name (void)
{
  return AU_USER_CLASS_NAME;
}
