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
 * authenticate_cache.cpp - Authorization manager's cache structure and their routine
 */

#include "authenticate_cache.hpp"

#include "authenticate.h"
#include "authenticate_grant.hpp" /* apply_grants */
#include "dbtype_function.h"
#include "schema_manager.h"
#include "set_object.h"

static void free_user_cache (AU_USER_CACHE *u);

authenticate_cache::authenticate_cache ()
{
  init ();
}

/*
 * init_caches - Called during au_init().  Initialize all of the cache
 *               related variables.
 *   return: none
 */
void
authenticate_cache::init (void)
{
  user_cache = NULL;
  class_caches = NULL;
  cache_depth = 0;
  cache_max = 0;
  cache_increment = 4;
  cache_index = -1;
}


/*
 * flush_caches - Called during au_final(). Free the authorization cache
 *                structures and initialize the global variables
 *                to their default state.
 *   return : none
 */
void
authenticate_cache::flush (void)
{
  AU_USER_CACHE *u, *nextu;
  AU_CLASS_CACHE *c, *nextc;

  for (c = class_caches, nextc = NULL; c != NULL; c = nextc)
    {
      nextc = c->next;
      c->class_->auth_cache = NULL;
      free_class_cache (c);
    }
  for (u = user_cache, nextu = NULL; u != NULL; u = nextu)
    {
      nextu = u->next;
      free_user_cache (u);
    }

  /* clear the associated globals */
  init ();
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
int
authenticate_cache::update (MOP classop, SM_CLASS *sm_class)
{
  int error = NO_ERROR;
  DB_SET *groups = NULL;
  int i, save, card;
  DB_VALUE value;
  MOP group, auth;
  bool is_member = false;
  unsigned int *bits = NULL;
  bool need_pop_er_stack = false;

  /*
   * must disable here because we may be updating the cache of the system
   * objects and we need to read them in order to update etc.
   */
  AU_DISABLE (save);

  er_stack_push ();

  need_pop_er_stack = true;

  bits = get_cache_bits (sm_class);
  if (bits == NULL)
    {
      assert (false);
      return er_errid ();
    }

  /* initialize the cache bits */
  *bits = AU_NO_AUTHORIZATION;

  if (sm_class->owner == NULL)
    {
      /* This shouldn't happen - assign it to the DBA */
      error = ER_AU_CLASS_WITH_NO_OWNER;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);

      goto end;
    }

  is_member = au_is_dba_group_member (Au_user);
  if (is_member)
    {
      *bits = AU_FULL_AUTHORIZATION;
      goto end;
    }

  /* there might be errors */
  error = er_errid ();
  if (error != NO_ERROR)
    {
      goto end;
    }

  if (ws_is_same_object (Au_user, sm_class->owner))
    {
      /* might want to allow grant/revoke on self */
      *bits = AU_FULL_AUTHORIZATION;
      goto end;
    }

  if (au_get_set (Au_user, "groups", &groups) != NO_ERROR)
    {
      error = ER_AU_ACCESS_ERROR;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 2, "groups", AU_USER_CLASS_NAME);

      goto end;
    }

  db_make_object (&value, sm_class->owner);

  is_member = set_ismember (groups, &value);

  /* there might be errors */
  error = er_errid ();
  if (error != NO_ERROR)
    {
      goto end;
    }

  if (is_member)
    {
      /* we're a member of the owning group */
      *bits = AU_FULL_AUTHORIZATION;
    }
  else if (au_get_object (Au_user, "authorization", &auth) != NO_ERROR)
    {
      error = ER_AU_ACCESS_ERROR;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 2, "authorization", AU_USER_CLASS_NAME);
    }
  else
    {
      /* apply local grants */
      error = apply_grants (auth, classop, bits);
      if (error != NO_ERROR)
	{
	  goto end;
	}

      /* apply grants from all groups */
      card = set_cardinality (groups);

      /* there might be errors */
      error = er_errid ();
      if (error != NO_ERROR)
	{
	  goto end;
	}

      for (i = 0; i < card; i++)
	{
	  /* will have to handle deleted groups here ! */
	  error = au_set_get_obj (groups, i, &group);
	  if (error != NO_ERROR)
	    {
	      goto end;
	    }

	  if (ws_is_same_object (group, Au_dba_user))
	    {
	      /* someones on the DBA member list, give them power */
	      *bits = AU_FULL_AUTHORIZATION;
	    }
	  else
	    {
	      error = au_get_object (group, "authorization", &auth);
	      if (error != NO_ERROR)
		{
		  goto end;
		}

	      error = apply_grants (auth, classop, bits);
	      if (error != NO_ERROR)
		{
		  goto end;
		}
	    }
	}
    }

end:

  if (groups != NULL)
    {
      set_free (groups);
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

  AU_ENABLE (save);

  return (error);
}

// setter/getter of cache_index
int
authenticate_cache::get_cache_index ()
{
  return cache_index;
}

void
authenticate_cache::set_cache_index (int idx)
{
  cache_index = idx;
}

/*
 * au_free_authorization_cache -  This removes a class cache from the global
 *                                cache list, detaches it from the class
 *                                and frees it.
 *   return: none
 *   cache(in): class cache
 */
void
authenticate_cache::free_authorization_cache (void *cache)
{
  AU_CLASS_CACHE *c, *prev;

  if (cache != NULL)
    {
      for (c = class_caches, prev = NULL; c != NULL && c != cache; c = c->next)
	{
	  prev = c;
	}
      if (c != NULL)
	{
	  if (prev == NULL)
	    {
	      class_caches = c->next;
	    }
	  else
	    {
	      prev->next = c->next;
	    }
	}
      free_class_cache ((AU_CLASS_CACHE *) cache);
    }
}


/*
 * AUTHORIZATION CACHES
 */

/*
 * au_make_class_cache - Allocates and initializes a new class cache
 *    return: new cache structure
 *    depth(in): number of elements to include in the cache
 */
AU_CLASS_CACHE *
authenticate_cache::make_class_cache (int depth)
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
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, size);
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
void
authenticate_cache::free_class_cache (AU_CLASS_CACHE *cache)
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
AU_CLASS_CACHE *
authenticate_cache::install_class_cache (SM_CLASS *sm_class)
{
  AU_CLASS_CACHE *new_class_cache;

  new_class_cache = make_class_cache (cache_max);
  if (new_class_cache != NULL)
    {
      new_class_cache->next = class_caches;
      class_caches = new_class_cache;
      new_class_cache->class_ = sm_class;
      sm_class->auth_cache = new_class_cache;
    }

  return new_class_cache;
}

unsigned int *
authenticate_cache::get_cache_bits (SM_CLASS *sm_class)
{
  AU_CLASS_CACHE *cache = (AU_CLASS_CACHE *) sm_class->auth_cache;
  if (cache == NULL)
    {
      cache = install_class_cache (sm_class);
      if (cache == NULL)
	{
	  assert (er_errid () != NO_ERROR);
	  return NULL;
	}
    }

  return &cache->data[cache_index];
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
int
authenticate_cache::extend_class_caches (int *index)
{
  int error = NO_ERROR;
  AU_CLASS_CACHE *c, *new_list, *new_entry, *next;
  int new_max, i;

  if (cache_depth < cache_max)
    {
      *index = cache_depth;
      cache_depth++;
    }
  else
    {
      new_list = NULL;
      new_max = cache_max + cache_increment;

      for (c = class_caches; c != NULL && !error; c = c->next)
	{
	  new_entry = make_class_cache (new_max);
	  if (new_entry == NULL)
	    {
	      assert (er_errid () != NO_ERROR);
	      error = er_errid ();
	    }
	  else
	    {
	      for (i = 0; i < cache_depth; i++)
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
	  for (c = class_caches, next = NULL; c != NULL; c = next)
	    {
	      next = c->next;
	      c->class_->auth_cache = NULL;
	      free_class_cache (c);
	    }
	  for (c = new_list; c != NULL; c = c->next)
	    {
	      c->class_->auth_cache = c;
	    }

	  class_caches = new_list;
	  cache_max = new_max;
	  *index = cache_depth;
	  cache_depth++;
	}
    }

  return error;
}

/*
 * make_user_cache - This creates a new user cache and appends it to the user cache list
 *   return: user cache
 *   name(in): user name
 *   user(in): user MOP
 *
 */
AU_USER_CACHE *
authenticate_cache::make_user_cache (const char *name, DB_OBJECT *user, bool sanity_check)
{
  AU_USER_CACHE *new_user_cache = nullptr;
  assert (name != nullptr);
  assert (user != nullptr);

  if (sanity_check)
    {
      /*
      * User wasn't in the cache, add it and extend the existing class
      * caches.  First do a little sanity check just to make sure this
      * is a user object.
      */
      DB_OBJECT *class_mop = sm_get_class (user);
      if (class_mop == NULL)
	{
	  assert (er_errid () != NO_ERROR);
	  return NULL;
	}
      else if (class_mop != Au_user_class)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_AU_CORRUPTED, 0); /* need a better error */
	  return NULL;
	}
    }

  if ((new_user_cache = find_user_cache_by_name (name)) != nullptr)
    {
      return new_user_cache;
    }

  std::string upper_case_name (name);
  std::transform (upper_case_name.begin(), upper_case_name.end(), upper_case_name.begin(), ::toupper);

  new_user_cache = new AU_USER_CACHE ();
  if (new_user_cache != nullptr)
    {
      new_user_cache->next = user_cache;
      user_cache = new_user_cache;

      new_user_cache->name = upper_case_name;
      new_user_cache->user = user;
      new_user_cache->index = -1;

      user_name_cache[upper_case_name] = new_user_cache;
    }

  return new_user_cache;
}

/*
 * find_user_cache_by_name - This determines the cache for the given user name.
 *   return: user cache
 *   name(in): user name
 *
 */
AU_USER_CACHE *
authenticate_cache::find_user_cache_by_name (const char *name)
{
  if (name == nullptr)
    {
      return nullptr;
    }

  AU_USER_CACHE *user_cache = nullptr;

  std::string upper_case_name (name);
  std::transform (upper_case_name.begin(), upper_case_name.end(), upper_case_name.begin(), ::toupper);

  const auto &it = user_name_cache.find (upper_case_name);
  if (it != user_name_cache.end())
    {
      user_cache = it->second;
    }

  return user_cache;
}

/*
 * find_user_cache_by_mop - This determines the cache for the given user MOP.
 *   return: user cache
 *   user(in): user mop
 *
 */
AU_USER_CACHE *
authenticate_cache::find_user_cache_by_mop (DB_OBJECT *user)
{
  AU_USER_CACHE *u;

  for (u = user_cache; u != NULL && !ws_is_same_object (u->user, user); u = u->next)
    ;

  return u;
}

int
authenticate_cache::get_user_cache_index (AU_USER_CACHE *cache, int *index)
{
  int error = NO_ERROR;

  if (cache->index == -1)
    {
      error = extend_class_caches (index);
      if (error = NO_ERROR)
	{
	  cache->index = *index;
	}
    }
  else
    {
      *index = cache->index;
    }

  return error;
}

/*
 * free_user_cache - Frees a user cache. Make sure to clear the MOP pointer.
 *   returns: none
 *   u(in): user cache
 */
void
authenticate_cache::free_user_cache (AU_USER_CACHE *u)
{
  if (u != NULL)
    {
      u->user = NULL;		/* clear GC roots */

      user_name_cache.erase (u->name);
      u->name.clear ();

      delete u;
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
void
authenticate_cache::reset_cache_for_user_and_class (SM_CLASS *sm_class)
{
  AU_USER_CACHE *u;
  AU_CLASS_CACHE *c;

  for (c = class_caches; c != NULL && c->class_ != sm_class; c = c->next);
  if (c != NULL)
    {
      /*
       * invalide every user's cache for this class_, could be more
       * selective and do only the given user and its members
       */
      for (u = user_cache; u != NULL; u = u->next)
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
authenticate_cache::reset_authorization_caches (void)
{
  AU_CLASS_CACHE *c;
  int i;

  for (c = class_caches; c != NULL; c = c->next)
    {
      for (i = 0; i < cache_depth; i++)
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
void
authenticate_cache::remove_user_cache_references (MOP user)
{
  AU_USER_CACHE *u;

  for (u = user_cache; u != NULL; u = u->next)
    {
      if (ws_is_same_object (u->user, user))
	{
	  u->user = NULL;

	  user_name_cache.erase (u->name);
	  u->name.clear ();
	}
    }
}

/*
 * au_print_cache() -
 *   return: none
 *   cache(in):
 *   fp(in):
 */
void
authenticate_cache::print_cache (int cache, FILE *fp)
{
  int i, mask, auth, option;
  const char *auth_type_name[] =
  {
    "select", "insert", "update", "delete", "alter", "index", "execute"
  };

  if (cache < 0)
    {
      fprintf (fp, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_AUTHORIZATION, MSGCAT_AUTH_INVALID_CACHE));
    }
  else
    {
      for (i = 0; i < 7; i++)
	{
	  mask = 1 << i;
	  auth = cache & mask;
	  option = cache & (mask << AU_GRANT_SHIFT);
	  if (option)
	    {
	      fprintf (fp, "*");
	    }
	  if (auth)
	    {
	      fprintf (fp, "%s ", auth_type_name[i]);
	    }
	}
    }
  fprintf (fp, "\n");
}
