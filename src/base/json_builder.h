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
 *       Scope. This builds a document, writes it out and drops it, all on one
 *       thread - which is what a query trace does. It has no read side, and a
 *       tree cannot be handed to another thread. Nodes come from a pool per
 *       thread that is given back once nothing on that thread is owned, so a
 *       tree kept alive for a long time keeps every other tree built beside it
 *       alive too. For JSON that has to be read back, walked, or passed around,
 *       use db_json in src/compat, which is what the JSON data type uses.
 *
 *       Ownership. trace_json_object () and friends hand back a node the caller
 *       owns. The _set_new () and _append_new () functions take that node over
 *       whether they succeed or fail, so a caller never has to release a value
 *       it has already passed in. trace_json_decref () releases a node the caller
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
   * "struct trace_json_t" rather than including this header */
  typedef struct trace_json_t trace_json_t;
  typedef long long trace_json_int_t;

  extern trace_json_t *trace_json_object (void);
  extern trace_json_t *trace_json_array (void);
  extern trace_json_t *trace_json_true (void);
  extern trace_json_t *trace_json_false (void);
  extern trace_json_t *trace_json_boolean (int value);
  extern trace_json_t *trace_json_integer (trace_json_int_t value);

  /* NULL for a value JSON cannot carry, that is NaN or an infinity */
  extern trace_json_t *trace_json_real (double value);

  /* NULL for a NULL pointer or for bytes that are not valid UTF-8 */
  extern trace_json_t *trace_json_string (const char *value);

  /* Both take the value over, on success and on failure alike. A key that is
   * already present has its value replaced. */
  extern int trace_json_object_set_new (trace_json_t * object, const char *key, trace_json_t * value);
  extern int trace_json_array_append_new (trace_json_t * array, trace_json_t * value);

  /* releases the tree this node belongs to, once no node of it is still owned */
  extern void trace_json_decref (trace_json_t * node);

  /* two-space indent, members in insertion order; the caller frees the result
   * with free (). NULL if the tree holds something that cannot be written. */
  extern char *trace_json_dumps (const trace_json_t * node);

  extern trace_json_t *trace_json_loads (const char *text);

  /* supports the subset of the pack language the callers use: an object of "s"
   * keys whose values are o, s, i, I, f, b or an array of o. Every "o" argument
   * is taken over, on success and on failure alike. */
  extern trace_json_t *trace_json_pack (const char *fmt, ...);

  /* How many nodes on this thread the caller still owns. It is zero once every
   * tree has been released, and that is the invariant the node pool is freed
   * on, so a caller that wants to prove it did not lose a node can check it. */
  extern long trace_json_owned_count (void);

#ifdef __cplusplus
}
#endif

#endif				/* _JSON_BUILDER_H_ */
