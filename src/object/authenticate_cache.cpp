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
#include "schema_manager.h"

static AU_CLASS_CACHE *au_make_class_cache (int depth);
static void au_free_class_cache (AU_CLASS_CACHE *cache);

static void free_user_cache (AU_USER_CACHE *u);
static int au_extend_class_caches (int *index);

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
int Au_cache_index = -1;

/*
 * init_caches - Called during au_init().  Initialize all of the cache
 *               related global variables.
 *   return: none
 */
void
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
static void
au_free_class_cache (AU_CLASS_CACHE *cache)
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
au_install_class_cache (SM_CLASS *sm_class)
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
      for (c = Au_class_caches, prev = NULL; c != NULL && c != cache; c = c->next)
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
int
au_find_user_cache_index (DB_OBJECT *user, int *index, int check_it)
{
  int error = NO_ERROR;
  AU_USER_CACHE *u, *new_user_cache;
  DB_OBJECT *class_mop;

  for (u = Au_user_cache; u != NULL && !ws_is_same_object (u->user, user); u = u->next)
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
free_user_cache (AU_USER_CACHE *u)
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
void
reset_cache_for_user_and_class (SM_CLASS *sm_class)
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
void
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
 * flush_caches - Called during au_final(). Free the authorization cache
 *                structures and initialize the global variables
 *                to their default state.
 *   return : none
 */
void
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
