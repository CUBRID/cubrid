/*
 * pr_tree.h
 *
 * Interface for path reduction tree.
 * Copyright (C) 2001 Farooq Mela.
 *
 * $Id: pr_tree.h,v 1.2 2001/09/10 06:48:02 farooq Exp $
 */

#ifndef _PR_TREE_H_
#define _PR_TREE_H_

#include "dict.h"

BEGIN_DECL

struct pr_tree;
typedef struct pr_tree pr_tree;

pr_tree *pr_tree_new __P((dict_cmp_func key_cmp, dict_del_func key_del,
						  dict_del_func dat_del));
dict	*pr_dict_new __P((dict_cmp_func key_cmp, dict_del_func key_del,
						  dict_del_func dat_del));
void	 pr_tree_destroy __P((pr_tree *tree, int del));

int pr_tree_insert __P((pr_tree *tree, void *key, void *dat, int overwrite));
int pr_tree_probe __P((pr_tree *tree, void *key, void **dat));
void *pr_tree_search __P((pr_tree *tree, const void *key));
const void *pr_tree_csearch __P((const pr_tree *tree, const void *key));
int pr_tree_remove __P((pr_tree *tree, const void *key, int del));
void pr_tree_empty __P((pr_tree *tree, int del));
void pr_tree_walk __P((pr_tree *tree, dict_vis_func visit));
unsigned pr_tree_count __P((const pr_tree *tree));
unsigned pr_tree_height __P((const pr_tree *tree));
unsigned pr_tree_mheight __P((const pr_tree *tree));
unsigned pr_tree_pathlen __P((const pr_tree *tree));
const void *pr_tree_min __P((const pr_tree *tree));
const void *pr_tree_max __P((const pr_tree *tree));

struct pr_itor;
typedef struct pr_itor pr_itor;

pr_itor *pr_itor_new __P((pr_tree *tree));
dict_itor *pr_dict_itor_new __P((pr_tree *tree));
void pr_itor_destroy __P((pr_itor *tree));

int pr_itor_valid __P((const pr_itor *itor));
void pr_itor_invalidate __P((pr_itor *itor));
int pr_itor_next __P((pr_itor *itor));
int pr_itor_prev __P((pr_itor *itor));
int pr_itor_nextn __P((pr_itor *itor, unsigned count));
int pr_itor_prevn __P((pr_itor *itor, unsigned count));
int pr_itor_first __P((pr_itor *itor));
int pr_itor_last __P((pr_itor *itor));
int pr_itor_search __P((pr_itor *itor, const void *key));
const void *pr_itor_key __P((const pr_itor *itor));
void *pr_itor_data __P((pr_itor *itor));
const void *pr_itor_cdata __P((const pr_itor *itor));
int pr_itor_set_data __P((pr_itor *itor, void *dat, int del));
int pr_itor_remove __P((pr_itor *itor, int del));

END_DECL

#endif /* !_PR_TREE_H_ */
