/*
 * tr_tree.h
 *
 * Interface for treap.
 * Copyright (C) 2001 Farooq Mela.
 *
 * $Id: tr_tree.h,v 1.2 2001/09/10 06:51:39 farooq Exp $
 */

#ifndef _TR_TREE_H_
#define _TR_TREE_H_

#include "dict.h"

BEGIN_DECL

struct tr_tree;
typedef struct tr_tree tr_tree;

tr_tree *tr_tree_new __P((dict_cmp_func key_cmp, dict_del_func key_del,
						  dict_del_func dat_del));
dict	*tr_dict_new __P((dict_cmp_func key_cmp, dict_del_func key_del,
						  dict_del_func dat_del));
void	 tr_tree_destroy __P((tr_tree *tree, int del));

int tr_tree_insert __P((tr_tree *tree, void *key, void *dat, int overwrite));
int tr_tree_probe __P((tr_tree *tree, void *key, void **dat));
void *tr_tree_search __P((tr_tree *tree, const void *key));
const void *tr_tree_csearch __P((const tr_tree *tree, const void *key));
int tr_tree_remove __P((tr_tree *tree, const void *key, int del));
void tr_tree_empty __P((tr_tree *tree, int del));
void tr_tree_walk __P((tr_tree *tree, dict_vis_func visit));
unsigned tr_tree_count __P((const tr_tree *tree));
unsigned tr_tree_height __P((const tr_tree *tree));
unsigned tr_tree_mheight __P((const tr_tree *tree));
unsigned tr_tree_pathlen __P((const tr_tree *tree));
const void *tr_tree_min __P((const tr_tree *tree));
const void *tr_tree_max __P((const tr_tree *tree));

struct tr_itor;
typedef struct tr_itor tr_itor;

tr_itor *tr_itor_new __P((tr_tree *tree));
dict_itor *tr_dict_itor_new __P((tr_tree *tree));
void tr_itor_destroy __P((tr_itor *tree));

int tr_itor_valid __P((const tr_itor *itor));
void tr_itor_invalidate __P((tr_itor *itor));
int tr_itor_next __P((tr_itor *itor));
int tr_itor_prev __P((tr_itor *itor));
int tr_itor_nextn __P((tr_itor *itor, unsigned count));
int tr_itor_prevn __P((tr_itor *itor, unsigned count));
int tr_itor_first __P((tr_itor *itor));
int tr_itor_last __P((tr_itor *itor));
int tr_itor_search __P((tr_itor *itor, const void *key));
const void *tr_itor_key __P((const tr_itor *itor));
void *tr_itor_data __P((tr_itor *itor));
const void *tr_itor_cdata __P((const tr_itor *itor));
int tr_itor_set_data __P((tr_itor *itor, void *dat, int del));
int tr_itor_remove __P((tr_itor *itor, int del));

END_DECL

#endif /* !_TR_TREE_H_ */
