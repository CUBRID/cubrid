/*
 * hashtable.h
 *
 * Interface for chained hash table.
 * Copyright (C) 2001 Farooq Mela.
 *
 * $Id: hashtable.h,v 1.4 2001/11/14 05:25:39 farooq Exp $
 */

#ifndef _HASHTABLE_H_
#define _HASHTABLE_H_

#include "dict.h"

BEGIN_DECL

struct hashtable;
typedef struct hashtable hashtable;

hashtable *hashtable_new __P((dict_cmp_func key_cmp, dict_hsh_func key_hsh,
							  dict_del_func key_del, dict_del_func dat_del,
							  unsigned size));
dict *hashtable_dict_new __P((dict_cmp_func key_cmp, dict_hsh_func key_hsh,
							  dict_del_func key_del, dict_del_func dat_del,
							  unsigned size));
void hashtable_destroy __P((hashtable *table, int del));

int hashtable_insert __P((hashtable *table, void *key, void *dat,
						  int overwrite));
int hashtable_probe __P((hashtable *table, void *key, void **dat));
void *hashtable_search __P((hashtable *table, const void *key));
const void *hashtable_csearch __P((const hashtable *table, const void *key));
int hashtable_remove __P((hashtable *table, const void *key, int del));
void hashtable_empty __P((hashtable *table, int del));
void hashtable_walk __P((hashtable *table, dict_vis_func visit));
unsigned hashtable_count __P((const hashtable *table));
unsigned hashtable_size __P((const hashtable *table));
unsigned hashtable_slots_used __P((const hashtable *table));
int hashtable_resize __P((hashtable *table, unsigned size));

struct hashtable_itor;
typedef struct hashtable_itor hashtable_itor;

hashtable_itor *hashtable_itor_new __P((hashtable *table));
dict_itor *hashtable_dict_itor_new __P((hashtable *table));
void hashtable_itor_destroy __P((hashtable_itor *));

int hashtable_itor_valid __P((const hashtable_itor *itor));
void hashtable_itor_invalidate __P((hashtable_itor *itor));
int hashtable_itor_next __P((hashtable_itor *itor));
int hashtable_itor_prev __P((hashtable_itor *itor));
int hashtable_itor_nextn __P((hashtable_itor *itor, unsigned count));
int hashtable_itor_prevn __P((hashtable_itor *itor, unsigned count));
int hashtable_itor_first __P((hashtable_itor *itor));
int hashtable_itor_last __P((hashtable_itor *itor));
int hashtable_itor_search __P((hashtable_itor *itor, const void *key));
const void *hashtable_itor_key __P((const hashtable_itor *itor));
void *hashtable_itor_data __P((hashtable_itor *itor));
const void *hashtable_itor_cdata __P((const hashtable_itor *itor));
int hashtable_itor_set_data __P((hashtable_itor *itor, void *dat, int del));
int hashtable_itor_remove __P((hashtable_itor *itor, int del));

END_DECL

#endif /* !_HASHTABLE_H_ */
