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
 * authenticate_cache.hpp - Define cache structures for authorzation module
 *
 */

#ifndef _AUTHENTICATE_CACHE_HPP_
#define _AUTHENTICATE_CACHE_HPP_

#include "class_object.h" /* SM_CLASS */

/* Invalid cache is identified when the high bit is on. */
#define AU_CACHE_INVALID        0x80000000

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
 * AUTHORIZATION CACHING
 */

class authenticate_cache
{
  public:

    /*
     * Au_user_cache
     *
     * The list of cached users.
     */
    AU_USER_CACHE *user_cache;

    /*
     * Au_class_caches
     *
     * A list of all allocated class caches.  These are maintained on a list
     * so that we can get to all of them easily when they need to be
     * altered.
     */
    AU_CLASS_CACHE *class_caches;

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
    int cache_depth = 0;
    int cache_max = 0;
    int cache_increment = 4;

    /*
    * Au_cache_index
    *
    * This is the current index into the class authorization caches.
    * It will be maintained in parallel with the current user.
    * Each user is assigned a particular index, when the user changes,
    * Au_cache_index is changed as well.
    */
    int cache_index;

    authenticate_cache ();

    void init (void);
    void flush (void);
    int update (MOP classop, SM_CLASS *sm_class);

    // setter/getter of cache_index
    int get_cache_index ();
    void set_cache_index (int idx);

    unsigned int *get_cache_bits (SM_CLASS *sm_class);

    void free_authorization_cache (void *cache);
    int find_user_cache_index (DB_OBJECT *user, int *index, int check_it);

    void reset_cache_for_user_and_class (SM_CLASS *sm_class);
    void reset_authorization_caches (void);

    void remove_user_cache_references (MOP user);

    void print_cache (int cache, FILE *fp);  // for debugging

  private:
    // migrate static methods
    AU_CLASS_CACHE *make_class_cache (int depth);
    void free_class_cache (AU_CLASS_CACHE *cache);
    AU_CLASS_CACHE *install_class_cache (SM_CLASS *sm_class);
    int extend_class_caches (int *index);
    void free_user_cache (AU_USER_CACHE *u);
};

#endif // _AUTHENTICATE_CACHE_HPP_
