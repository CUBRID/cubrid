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
 * esql_hash.h - Generic hash table implementation header file.
 */

#ifndef _ESQL_HASH_H_
#define _ESQL_HASH_H_

#ident "$Id$"

typedef unsigned (*HT_HASH_FN) (void *);
typedef int (*HT_CMP_FN) (void *, void *);
typedef void (*HT_FREE_FN) (void *);

typedef struct hash_tab_s HASH_TAB;
struct hash_tab_s
{
  void (*free_table) (HASH_TAB * tbl, HT_FREE_FN free);
  void *(*add_symbol) (HASH_TAB * tbl, void *sym);
  void *(*find_symbol) (HASH_TAB * tbl, void *sym);
  void *(*next_symbol) (HASH_TAB * tbl, void *last);
  void (*remove_symbol) (HASH_TAB * tbl, void *sym);
  int (*print_table) (HASH_TAB * tbl, void (*prnt) (), void *par, int srt);
  int (*get_symbol_count) (HASH_TAB * tbl);
};

extern HASH_TAB *es_ht_make_table (unsigned maxsym, HT_HASH_FN hash, HT_CMP_FN cmp);
extern void *es_ht_alloc_new_symbol (int size);
extern void es_ht_free_symbol (void *sym);

#endif /* _ESQL_HASH_H_ */
