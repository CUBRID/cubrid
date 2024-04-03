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

extern int Au_cache_index;

extern void init_caches (void);
extern void flush_caches (void);
extern int update_cache (MOP classop, SM_CLASS *sm_class);

// Au_cache_index
extern int au_get_cache_index ();
extern void au_set_cache_index (int idx);

extern int au_find_user_cache_index (DB_OBJECT *user, int *index, int check_it);
extern void reset_cache_for_user_and_class (SM_CLASS *sm_class);
extern void remove_user_cache_references (MOP user);

extern unsigned int *get_cache_bits (SM_CLASS *sm_class);

extern void au_print_cache (int cache, FILE *fp);  // for debugging
#endif // _AUTHENTICATE_CACHE_HPP_