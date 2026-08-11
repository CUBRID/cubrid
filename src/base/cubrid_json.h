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
 * cubrid_json.h - JSON tree building for execution plan output
 *
 * Note: This is the interface the plan and trace dump code uses to build a JSON
 *       document and serialize it. It is implemented over RapidJSON, the same
 *       library the JSON data type uses, and replaces the jansson dependency.
 *
 *       The names and signatures are kept as they were so the call sites do not
 *       change. Ownership follows the same rule: the _new variants take over the
 *       value they are given, and json_decref () releases the whole tree.
 */

#ifndef _CUBRID_JSON_H_
#define _CUBRID_JSON_H_

#ident "$Id$"

#include <stdlib.h>

#ifdef __cplusplus
extern "C"
{
#endif

  /* the tag matters: parse_tree.h forward-declares "struct json_t" */
  typedef struct json_t json_t;
  typedef long long json_int_t;

/* accepted for source compatibility; formatting is fixed at two-space indent
 * with members emitted in insertion order, which is what the callers ask for */
#define JSON_INDENT(n)      (n)
#define JSON_PRESERVE_ORDER 0

  extern json_t *json_object (void);
  extern json_t *json_array (void);
  extern json_t *json_null (void);
  extern json_t *json_true (void);
  extern json_t *json_false (void);
  extern json_t *json_boolean (int value);
  extern json_t *json_integer (json_int_t value);
  extern json_t *json_real (double value);
  extern json_t *json_string (const char *value);

  /* both take over the reference to value */
  extern int json_object_set_new (json_t * object, const char *key, json_t * value);
  extern int json_array_append_new (json_t * array, json_t * value);

  extern int json_object_clear (json_t * object);

  /* releases the tree this node belongs to, once no root of it is left */
  extern void json_decref (json_t * node);
  extern void json_delete (json_t * node);

  /* caller frees the result with free () */
  extern char *json_dumps (const json_t * node, size_t flags);
  extern json_t *json_loads (const char *text, size_t flags, void *error);

  /* supports the subset of the pack language the callers use:
   * an object of "s" keys whose values are o, s, i, I, f, b or an array of o */
  extern json_t *json_pack (const char *fmt, ...);

  /* accepted and ignored; RapidJSON manages its own allocation */
  extern void json_set_alloc_funcs (void *(*malloc_fn) (size_t), void (*free_fn) (void *));

#ifdef __cplusplus
}
#endif

#endif				/* _CUBRID_JSON_H_ */
