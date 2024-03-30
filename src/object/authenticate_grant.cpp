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
 * authenticate_grant.cpp -
 */

#include "authenticate_grant.hpp"

#include "authenticate.h"
#include "authenticate_cache.hpp"
#include "authenticate_auth_access.hpp"

#include "db.h" /* db_compile_and_execute_local () */
#include "dbtype.h" /* DB_IS_STRING */
// #include "dbtype_function.h"
#include "class_object.h" /* SM_CLASS */
#include "schema_manager.h" /* sm_partitioned_class_type () */
#include "transaction_cl.h" /* tran_system_savepoint () */
#include "execute_schema.h" /* UNIQUE_PARTITION_SAVEPOINT_GRANT */
#include "set_object.h" /* set_free () */
#include "object_accessor.h" /* obj_inst_lock () */
#include "object_primitive.h"


#if defined(SA_MODE)
extern bool catcls_Enable;
#endif /* SA_MODE */

static int check_grant_option (MOP classop, SM_CLASS *sm_class, DB_AUTH type);


static int collect_class_grants (MOP class_mop, DB_AUTH type, MOP revoked_auth, int revoked_grant_index,
				 AU_GRANT **return_grants);
static int propagate_revoke (AU_GRANT *grant_list, MOP owner, DB_AUTH mask);

static int au_propagate_del_new_auth (AU_GRANT *glist, DB_AUTH mask);


/*
 * GRANT STRUCTURE OPERATION
 */
static void free_grant_list (AU_GRANT *grants);
static void map_grant_list (AU_GRANT *grants, MOP grantor);


// static int get_grants (MOP auth, DB_SET **grant_ptr, int filter);
static int find_grant_entry (DB_SET *grants, MOP class_mop, MOP grantor);
static int add_grant_entry (DB_SET *grants, MOP class_mop, MOP grantor);
static void drop_grant_entry (DB_SET *grants, int index);

/*
 * GRANT/REVOKE OPERATION
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

  error = sm_partitioned_class_type (class_mop, &is_partition, NULL, &sub_partitions);
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
  else if ((error = au_fetch_class_force (class_mop, &classobj, AU_FETCH_READ)) == NO_ERROR)
    {
      if (ws_is_same_object (classobj->owner, user))
	{
	  error = ER_AU_CANT_GRANT_OWNER;
	  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, error, 0);
	}
      else if ((error = check_grant_option (class_mop, classobj, type)) == NO_ERROR)
	{
	  if (au_get_object (user, "authorization", &auth) != NO_ERROR)
	    {
	      error = ER_AU_ACCESS_ERROR;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 2, AU_USER_CLASS_NAME, "authorization");
	    }
	  /* lock authorization for write & mark dirty */
	  else if (au_fetch_instance (auth, NULL, AU_FETCH_UPDATE, LC_FETCH_MVCC_VERSION, AU_UPDATE) != NO_ERROR)
	    {
	      error = ER_AU_CANT_UPDATE;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
	    }
	  else if ((error = obj_inst_lock (auth, 1)) == NO_ERROR && (error = get_grants (auth, &grants, 1)) == NO_ERROR)
	    {
	      gindex = find_grant_entry (grants, class_mop, Au_user);
	      if (gindex == -1)
		{
		  current = AU_NO_AUTHORIZATION;
		}
	      else
		{
		  /* already granted, get current cache */
		  error = set_get_element (grants, GRANT_ENTRY_CACHE (gindex), &value);
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
		  au_auth_accessor accessor;
		  DB_AUTH ins_bits, upd_bits;

		  ins_bits = (DB_AUTH) ((~current & AU_TYPE_MASK) & (int) type);
		  if (ins_bits)
		    {
		      error =
			      accessor.insert_auth (Au_user, user, class_mop, ins_bits,
						    (grant_option) ? ins_bits : DB_AUTH_NONE);
		    }
		  upd_bits = (DB_AUTH) (~ins_bits & (int) type);
		  if ((error == NO_ERROR) && upd_bits)
		    {
		      error =
			      accessor.update_auth (Au_user, user, class_mop, upd_bits,
						    (grant_option) ? upd_bits : DB_AUTH_NONE);
		    }
		}

	      /* Fail to insert/update, never change the grant entry set. */
	      if (error != NO_ERROR)
		{
		  set_free (grants);
		  goto fail_end;
		}

	      current |= (int) type;
	      if (grant_option)
		{
		  current |= ((int) type << AU_GRANT_SHIFT);
		}

	      db_make_int (&value, current);
	      if (gindex == -1)
		{
		  /* There is no grant entry, add a new one. */
		  gindex = add_grant_entry (grants, class_mop, Au_user);
		}
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
  if (savepoint_grant && error != NO_ERROR && error != ER_LK_UNILATERALLY_ABORTED)
    {
      (void) tran_abort_upto_system_savepoint (UNIQUE_PARTITION_SAVEPOINT_GRANT);
    }
  if (sub_partitions)
    {
      free_and_init (sub_partitions);
    }
  AU_ENABLE (save);
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

  error = sm_partitioned_class_type (class_mop, &is_partition, NULL, &sub_partitions);
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
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 2, AU_USER_CLASS_NAME, "authorization");
	  goto fail_end;
	}
      else if (au_fetch_instance (auth, NULL, AU_FETCH_UPDATE, LC_FETCH_MVCC_VERSION, AU_UPDATE) != NO_ERROR)
	{
	  error = ER_AU_CANT_UPDATE;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
	  goto fail_end;
	}
      else if ((error = obj_inst_lock (auth, 1)) == NO_ERROR && (error = get_grants (auth, &grants, 1)) == NO_ERROR)
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
	      else if ((error = collect_class_grants (class_mop, type, auth, gindex, &grant_list)) == NO_ERROR)
		{

		  /* calculate the mask to turn off the grant */
		  mask = (int) ~ (type | (type << AU_GRANT_SHIFT));

		  /* propagate the revoke to the affected classes */
		  if ((error = propagate_revoke (grant_list, classobj->owner, (DB_AUTH) mask)) == NO_ERROR)
		    {

		      /*
		       * finally, update the local grant for the
		       * original object
		       */
		      current &= mask;
		      if (current)
			{
			  db_make_int (&cache_element, current);
			  set_put_element (grants, gindex + 2, &cache_element);
			}
		      else
			{
			  /* no remaining grants, remove it from the grant set */
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
			{
#endif /* SA_MODE */
			  au_auth_accessor accessor;
			  error = accessor.delete_auth (Au_user, user, class_mop, type);
#if defined(SA_MODE)
			}
#endif /* SA_MODE */
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
  if (savepoint_revoke && error != NO_ERROR && error != ER_LK_UNILATERALLY_ABORTED)
    {
      (void) tran_abort_upto_system_savepoint (UNIQUE_PARTITION_SAVEPOINT_REVOKE);
    }
  if (sub_partitions)
    {
      free_and_init (sub_partitions);
    }
  AU_ENABLE (save);
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
check_grant_option (MOP classop, SM_CLASS *sm_class, DB_AUTH type)
{
  int error = NO_ERROR;
  unsigned int *cache_bits;
  unsigned int mask;

  /*
   * this potentially can be called during initialization when we don't
   * actually have any users yet.  If so, assume its ok
   */
  if (au_get_cache_index () < 0)
    {
      return NO_ERROR;
    }

  cache_bits = get_cache_bits (sm_class);
  if (cache_bits == NULL)
    {
      return er_errid ();
    }

  if (*cache_bits == AU_CACHE_INVALID)
    {
      if (update_cache (classop, sm_class))
	{
	  assert (er_errid () != NO_ERROR);
	  return er_errid ();
	}
      cache_bits = get_cache_bits (sm_class);
      if (cache_bits == NULL)
	{
	  assert (false);
	  return er_errid ();
	}
    }

  /* build the complete bit mask */
  mask = type | (type << AU_GRANT_SHIFT);
  if ((*cache_bits & mask) != mask)
    {
      error = appropriate_error (*cache_bits, mask);
      if (error)
	{
	  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, error, 0);
	}
    }

  return error;
}

/*
 * appropriate_error -  This selects an appropriate error code to correspond
 *                      with an authorization failure of a some type
 *   return: error code
 *   bits(in): authorization type
 *   requested(in):
 * TODO : LP64
 */
int
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
 * GRANT STRUCTURE OPERATION
 */

/*
 * free_grant_list - Frees a list of temporary grant flattening structures.
 *    return: none
 *    grants(in): list of grant structures
 */
static void
free_grant_list (AU_GRANT *grants)
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
map_grant_list (AU_GRANT *grants, MOP grantor)
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
 * add_grant_entry - This adds a grant on a class from a particular user to
 *                   a sequence of grant elemetns.
 *                   It returns the index of the new grant element.
 *   return: sequence index to new grant entry
 *   grants(in): grant sequence to extend
 *   class_mop(in): class being granted
 *   grantor(in): user doing the granting
 */
static int
add_grant_entry (DB_SET *grants, MOP class_mop, MOP grantor)
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
drop_grant_entry (DB_SET *grants, int index)
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
find_grant_entry (DB_SET *grants, MOP class_mop, MOP grantor)
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
	  if (ws_is_same_object (db_get_object (&element), grantor))
	    {
	      position = i;
	    }
	}
    }

  return position;
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
int
get_grants (MOP auth, DB_SET **grant_ptr, int filter)
{
  int error = NO_ERROR;
  DB_VALUE value;
  DB_SET *grants = NULL;
  MOP grantor, gowner, class_;
  int gsize, i, j, existing, cache;
  bool need_pop_er_stack = false;

  assert (grant_ptr != NULL);

  *grant_ptr = NULL;

  er_stack_push ();

  need_pop_er_stack = true;

  error = obj_get (auth, "grants", &value);
  if (error != NO_ERROR)
    {
      goto end;
    }

  if (DB_VALUE_TYPE (&value) != DB_TYPE_SEQUENCE || DB_IS_NULL (&value) || db_get_set (&value) == NULL)
    {
      error = ER_AU_CORRUPTED;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);

      goto end;
    }

  if (!filter)
    {
      goto end;
    }

  grants = db_get_set (&value);
  gsize = set_size (grants);

  /* there might be errors */
  error = er_errid ();
  if (error != NO_ERROR)
    {
      goto end;
    }

  for (i = 0; i < gsize; i += GRANT_ENTRY_LENGTH)
    {
      error = set_get_element (grants, GRANT_ENTRY_SOURCE (i), &value);
      if (error != NO_ERROR)
	{
	  goto end;
	}

      grantor = NULL;
      if (DB_VALUE_TYPE (&value) == DB_TYPE_OBJECT && !DB_IS_NULL (&value))
	{
	  grantor = db_get_object (&value);
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
	      goto end;
	    }

	  if (DB_VALUE_TYPE (&value) == DB_TYPE_OBJECT && !DB_IS_NULL (&value))
	    {
	      class_ = db_get_object (&value);
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
	      for (j = 0; j < gsize && existing == -1; j += GRANT_ENTRY_LENGTH)
		{
		  error = set_get_element (grants, GRANT_ENTRY_SOURCE (j), &value);
		  if (error != NO_ERROR)
		    {
		      goto end;
		    }

		  if (DB_VALUE_TYPE (&value) == DB_TYPE_OBJECT && ws_is_same_object (db_get_object (&value), gowner))
		    {
		      existing = j;
		    }
		}

	      if (existing == -1)
		{
		  /*
		   * no previous entry for the owner,
		   * use the current one
		   */
		  db_make_object (&value, gowner);
		  error = set_put_element (grants, GRANT_ENTRY_SOURCE (i), &value);
		  if (error != NO_ERROR)
		    {
		      goto end;
		    }
		}
	      else
		{
		  /*
		   * update the previous entry with the extra bits
		   * and delete the current entry
		   */
		  error = set_get_element (grants, GRANT_ENTRY_CACHE (i), &value);
		  if (error != NO_ERROR)
		    {
		      goto end;
		    }

		  cache = db_get_int (&value);
		  error = set_get_element (grants, GRANT_ENTRY_CACHE (existing), &value);
		  if (error != NO_ERROR)
		    {
		      goto end;
		    }

		  db_make_int (&value, db_get_int (&value) | cache);
		  error = set_put_element (grants, GRANT_ENTRY_CACHE (existing), &value);
		  if (error != NO_ERROR)
		    {
		      goto end;
		    }

		  drop_grant_entry (grants, i);
		  gsize -= GRANT_ENTRY_LENGTH;
		}
	    }
	}
    }

end:

  if (error != NO_ERROR && grants != NULL)
    {
      set_free (grants);
      grants = NULL;
    }

  if (need_pop_er_stack)
    {
      if (error == NO_ERROR)
	{
	  er_stack_pop ();
	}
      else
	{
	  er_stack_pop_and_keep_error ();
	}
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
int
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
collect_class_grants (MOP class_mop, DB_AUTH type, MOP revoked_auth, int revoked_grant_index, AU_GRANT **return_grants)
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
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, query_size);
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
	      error = er_errid ();
	      if (error == ER_HEAP_UNKNOWN_OBJECT)
		{
		  error = NO_ERROR;
		}
	      else
		{
		  error = ER_AU_ACCESS_ERROR;
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 2, AU_USER_CLASS_NAME, "authorization");
		  break;
		}
	    }
	  else if ((error = get_grants (auth, &grants, 1)) == NO_ERROR)
	    {

	      gsize = set_size (grants);
	      for (j = 0; j < gsize && error == NO_ERROR; j += GRANT_ENTRY_LENGTH)
		{
		  /* ignore the grant entry that is being revoked */
		  if (auth == revoked_auth && j == revoked_grant_index)
		    {
		      continue;
		    }

		  /* see if grant is for the class in question */
		  if (set_get_element (grants, GRANT_ENTRY_CLASS (j), &element))
		    {
		      assert (er_errid () != NO_ERROR);
		      error = er_errid ();
		      break;
		    }

		  if (db_get_object (&element) == class_mop)
		    {
		      cache = AU_NO_AUTHORIZATION;
		      if (set_get_element (grants, GRANT_ENTRY_CACHE (j), &element))
			{
			  assert (er_errid () != NO_ERROR);
			  error = er_errid ();
			  break;
			}

		      cache = db_get_int (&element);
		      if ((cache & (int) type))
			{
			  new_grant = (AU_GRANT *) db_ws_alloc (sizeof (AU_GRANT));
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
			  new_grant->grant_option = (((int) type << AU_GRANT_SHIFT) & cache);
			  if (set_get_element (grants, GRANT_ENTRY_SOURCE (j), &element))
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
				  new_grant->grantor = db_get_object (&element);
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
 * propagate_revoke - Propagates a revoke operation to all affected users.
 *   return: error code
 *   grant_list(in):  list of grant nodes
 *   owner(in): owner of the class
 *   mask(in): authorization type mask
 */
static int
propagate_revoke (AU_GRANT *grant_list, MOP owner, DB_AUTH mask)
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
	{
	  return error;
	}
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
	  if (au_fetch_instance (g->auth_object, NULL, AU_FETCH_UPDATE, LC_FETCH_MVCC_VERSION, AU_UPDATE) != NO_ERROR)
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
	      if ((error = obj_inst_lock (g->auth_object, 1)) == NO_ERROR
		  && (error = get_grants (g->auth_object, &grants, 0)) == NO_ERROR)
		{
		  if ((error = set_get_element (grants, GRANT_ENTRY_CACHE (g->grant_index), &element)) == NO_ERROR)
		    {
		      db_make_int (&element, db_get_int (&element) & mask);
		      error = set_put_element (grants, GRANT_ENTRY_CACHE (g->grant_index), &element);
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
	      if ((error = obj_inst_lock (g->auth_object, 1)) == NO_ERROR
		  && (error = get_grants (g->auth_object, &grants, 0)) == NO_ERROR)
		{
		  length = set_size (grants);
		  for (i = 0; i < length; i += GRANT_ENTRY_LENGTH)
		    {
		      if ((error = set_get_element (grants, GRANT_ENTRY_CACHE (i), &element)) != NO_ERROR)
			{
			  break;
			}
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
 * au_propagate_del_new_auth -
 *   return: error code
 *   glist(in):
 *   mask(in):
 */
static int
au_propagate_del_new_auth (AU_GRANT *glist, DB_AUTH mask)
{
  AU_GRANT *g;
  DB_SET *grants;
  DB_VALUE class_, type;
  int error = NO_ERROR;

  au_auth_accessor accessor;
  for (g = glist; g != NULL; g = g->next)
    {
      if (!g->legal)
	{
	  error = get_grants (g->auth_object, &grants, 0);
	  if (error != NO_ERROR)
	    {
	      break;
	    }

	  error = set_get_element (grants, GRANT_ENTRY_CLASS (g->grant_index), &class_);
	  if (error != NO_ERROR)
	    {
	      break;
	    }

	  error = set_get_element (grants, GRANT_ENTRY_CACHE (g->grant_index), &type);
	  if (error != NO_ERROR)
	    {
	      break;
	    }

	  error =
		  accessor.delete_auth (g->grantor, g->user, db_get_object (&class_), (DB_AUTH) (db_get_int (&type) & ~mask));
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

      au_auth_accessor accessor;
      for (gindex = 0; gindex < gsize; gindex += GRANT_ENTRY_LENGTH)
	{
	  error = set_get_element (grants, GRANT_ENTRY_CLASS (gindex), &class_val);
	  if (error != NO_ERROR)
	    {
	      goto end;
	    }
	  class_ = db_get_object (&class_val);

	  error = set_get_element (grants, GRANT_ENTRY_SOURCE (gindex), &grantor_val);
	  if (error != NO_ERROR)
	    {
	      goto end;
	    }
	  grantor = db_get_object (&grantor_val);

	  error = set_get_element (grants, GRANT_ENTRY_CACHE (gindex), &auth_val);
	  if (error != NO_ERROR)
	    {
	      goto end;
	    }
	  auth = (DB_AUTH) db_get_int (&auth_val);

	  error = accessor.insert_auth (grantor, grantee, class_, (DB_AUTH) (auth & AU_TYPE_MASK), (auth & AU_GRANT_MASK));
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
