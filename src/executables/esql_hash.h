/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution. 
 *
 *   This program is free software; you can redistribute it and/or modify 
 *   it under the terms of the GNU General Public License as published by 
 *   the Free Software Foundation; version 2 of the License. 
 *
 *  This program is distributed in the hope that it will be useful, 
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of 
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the 
 *  GNU General Public License for more details. 
 *
 *  You should have received a copy of the GNU General Public License 
 *  along with this program; if not, write to the Free Software 
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA 
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

extern HASH_TAB *es_ht_make_table (unsigned maxsym, HT_HASH_FN hash,
				   HT_CMP_FN cmp);
extern void *es_ht_alloc_new_symbol (int size);
extern void es_ht_free_symbol (void *sym);

#endif /* _ESQL_HASH_H_ */
