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
 * json_builder.h - JSON tree building for execution plan and trace output
 *
 * Note: This is the interface the plan and trace dump code uses to build a JSON
 *       document and serialize it. It is implemented over RapidJSON, the same
 *       library the JSON data type uses.
 *
 *       Ownership. cub_json_object () and friends hand back a node the caller
 *       owns. The _set_new () and _append_new () functions take that node over
 *       whether they succeed or fail, so a caller never has to release a value
 *       it has already passed in. cub_json_decref () releases a node the caller
 *       still owns, and with it the tree hanging off it.
 *
 *       Every node has to end up in one of those two places. A node that is
 *       neither stored nor released holds the whole thread's node pool open,
 *       so error paths matter more here than a plain leak would suggest.
 *
 *       A node stays usable after it has been stored, which is what the plan
 *       dump relies on: it puts an empty object in its parent and then keeps
 *       adding members through the handle it already has.
 */

#ifndef _JSON_BUILDER_H_
#define _JSON_BUILDER_H_

#ident "$Id$"

#include <stdlib.h>

#ifdef __cplusplus
extern "C"
{
#endif

  /* the tag matters: parse_tree.h and query_dump.h forward-declare
   * "struct cub_json_t" rather than including this header */
  typedef struct cub_json_t cub_json_t;
  typedef long long cub_json_int_t;

  extern cub_json_t *cub_json_object (void);
  extern cub_json_t *cub_json_array (void);
  extern cub_json_t *cub_json_true (void);
  extern cub_json_t *cub_json_false (void);
  extern cub_json_t *cub_json_boolean (int value);
  extern cub_json_t *cub_json_integer (cub_json_int_t value);

  /* NULL for a value JSON cannot carry, that is NaN or an infinity */
  extern cub_json_t *cub_json_real (double value);

  /* NULL for a NULL pointer or for bytes that are not valid UTF-8 */
  extern cub_json_t *cub_json_string (const char *value);

  /* Both take the value over, on success and on failure alike. A key that is
   * already present has its value replaced, which costs a scan of the object,
   * so these are meant for objects with a handful of members - which is what
   * the plan dump builds. */
  extern int cub_json_object_set_new (cub_json_t * object, const char *key, cub_json_t * value);
  extern int cub_json_array_append_new (cub_json_t * array, cub_json_t * value);

  /* releases the tree this node belongs to, once no node of it is still owned */
  extern void cub_json_decref (cub_json_t * node);

  /* two-space indent, members in insertion order; the caller frees the result
   * with free (). NULL if the tree holds something that cannot be written. */
  extern char *cub_json_dumps (const cub_json_t * node);

  extern cub_json_t *cub_json_loads (const char *text);

  /* supports the subset of the pack language the callers use: an object of "s"
   * keys whose values are o, s, i, I, f, b or an array of o. Every "o" argument
   * is taken over, on success and on failure alike. */
  extern cub_json_t *cub_json_pack (const char *fmt, ...);

  /* How many nodes on this thread the caller still owns. It is zero once every
   * tree has been released, and that is the invariant the node pool is freed
   * on, so a caller that wants to prove it did not lose a node can check it. */
  extern long cub_json_owned_count (void);

#ifdef __cplusplus
}
#endif

#endif				/* _JSON_BUILDER_H_ */
