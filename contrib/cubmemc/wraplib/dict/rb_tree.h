/*
 * rb_tree.h
 *
 * Definitions for red-black binary search tree.
 * Copyright (C) 2001 Farooq Mela.
 *
 * $Id: rb_tree.h,v 1.3 2001/09/10 06:49:35 farooq Exp $
 */

#ifndef _RB_TREE_H_
#define _RB_TREE_H_

#include "dict.h"

BEGIN_DECL

struct rb_tree;
typedef struct rb_tree rb_tree;

rb_tree	*rb_tree_new __P((dict_cmp_func key_cmp, dict_del_func key_del,
						  dict_del_func dat_del));
dict	*rb_dict_new __P((dict_cmp_func key_cmp, dict_del_func key_del,
						  dict_del_func dat_del));
void rb_tree_destroy __P((rb_tree *tree, int del));

int rb_tree_insert __P((rb_tree *tree, void *key, void *dat, int overwrite));
int rb_tree_probe __P((rb_tree *tree, void *key, void **dat));
void *rb_tree_search __P((rb_tree *tree, const void *key));
const void *rb_tree_csearch __P((const rb_tree *tree, const void *key));
int rb_tree_remove __P((rb_tree *tree, const void *key, int del));
void rb_tree_empty __P((rb_tree *tree, int del));
void rb_tree_walk __P((rb_tree *tree, dict_vis_func visit));
unsigned rb_tree_count __P((const rb_tree *tree));
unsigned rb_tree_height __P((const rb_tree *tree));
unsigned rb_tree_mheight __P((const rb_tree *tree));
unsigned rb_tree_pathlen __P((const rb_tree *tree));
const void *rb_tree_min __P((const rb_tree *tree));
const void *rb_tree_max __P((const rb_tree *tree));

struct rb_itor;
typedef struct rb_itor rb_itor;

rb_itor *rb_itor_new __P((rb_tree *tree));
dict_itor *rb_dict_itor_new __P((rb_tree *tree));
void rb_itor_destroy __P((rb_itor *tree));

int rb_itor_valid __P((const rb_itor *itor));
void rb_itor_invalidate __P((rb_itor *itor));
int rb_itor_next __P((rb_itor *itor));
int rb_itor_prev __P((rb_itor *itor));
int rb_itor_nextn __P((rb_itor *itor, unsigned count));
int rb_itor_prevn __P((rb_itor *itor, unsigned count));
int rb_itor_first __P((rb_itor *itor));
int rb_itor_last __P((rb_itor *itor));
int rb_itor_search __P((rb_itor *itor, const void *key));
const void *rb_itor_key __P((const rb_itor *itor));
void *rb_itor_data __P((rb_itor *itor));
const void *rb_itor_cdata __P((const rb_itor *itor));
int rb_itor_set_data __P((rb_itor *itor, void *dat, int del));
int rb_itor_remove __P((rb_itor *itor, int del));

END_DECL

#endif /* !_RB_TREE_H_ */
