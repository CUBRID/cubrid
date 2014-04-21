/*
 * wb_tree.h
 *
 * Interface for weight balanced tree.
 * Copyright (C) 2001 Farooq Mela.
 *
 * $Id: wb_tree.h,v 1.2 2001/09/10 06:52:25 farooq Exp $
 */

#ifndef _WB_TREE_H_
#define _WB_TREE_H_

#include "dict.h"

BEGIN_DECL

struct wb_tree;
typedef struct wb_tree wb_tree;

wb_tree *wb_tree_new __P((dict_cmp_func key_cmp, dict_del_func key_del,
						  dict_del_func dat_del));
dict	*wb_dict_new __P((dict_cmp_func key_cmp, dict_del_func key_del,
						  dict_del_func dat_del));
void	 wb_tree_destroy __P((wb_tree *tree, int del));

int wb_tree_insert __P((wb_tree *tree, void *key, void *dat, int overwrite));
int wb_tree_probe __P((wb_tree *tree, void *key, void **dat));
void *wb_tree_search __P((wb_tree *tree, const void *key));
const void *wb_tree_csearch __P((const wb_tree *tree, const void *key));
int wb_tree_remove __P((wb_tree *tree, const void *key, int del));
void wb_tree_empty __P((wb_tree *tree, int del));
void wb_tree_walk __P((wb_tree *tree, dict_vis_func visit));
unsigned wb_tree_count __P((const wb_tree *tree));
unsigned wb_tree_height __P((const wb_tree *tree));
unsigned wb_tree_mheight __P((const wb_tree *tree));
unsigned wb_tree_pathlen __P((const wb_tree *tree));
const void *wb_tree_min __P((const wb_tree *tree));
const void *wb_tree_max __P((const wb_tree *tree));

struct wb_itor;
typedef struct wb_itor wb_itor;

wb_itor *wb_itor_new __P((wb_tree *tree));
dict_itor *wb_dict_itor_new __P((wb_tree *tree));
void wb_itor_destroy __P((wb_itor *tree));

int wb_itor_valid __P((const wb_itor *itor));
void wb_itor_invalidate __P((wb_itor *itor));
int wb_itor_next __P((wb_itor *itor));
int wb_itor_prev __P((wb_itor *itor));
int wb_itor_nextn __P((wb_itor *itor, unsigned count));
int wb_itor_prevn __P((wb_itor *itor, unsigned count));
int wb_itor_first __P((wb_itor *itor));
int wb_itor_last __P((wb_itor *itor));
int wb_itor_search __P((wb_itor *itor, const void *key));
const void *wb_itor_key __P((const wb_itor *itor));
void *wb_itor_data __P((wb_itor *itor));
const void *wb_itor_cdata __P((const wb_itor *itor));
int wb_itor_set_data __P((wb_itor *itor, void *dat, int del));
int wb_itor_remove __P((wb_itor *itor, int del));

END_DECL

#endif /* !_WB_TREE_H_ */
