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
 * parser_kv_pair.h
 */

#ifndef _PARSER_KV_PAIR_H_
#define _PARSER_KV_PAIR_H_

#include "parse_tree.h"

typedef struct kv_pair
{
  PT_NODE *key;
  PT_NODE *value;
  struct kv_pair *next;
} kv_pair;

kv_pair *kv_pair_make (PT_NODE * k, PT_NODE * v);

kv_pair *kv_pair_push_back (kv_pair * list, kv_pair * item);

kv_pair *kv_pair_push_front (kv_pair * list, kv_pair * item);

int kv_pair_count (const kv_pair * list);

PT_NODE *kv_pair_lookup (const kv_pair * list, const char *key_name);

#endif /* _PARSER_KV_PAIR_H_ */
