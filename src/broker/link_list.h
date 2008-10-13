/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * link_list.h - Linked-list Utility Interface Header File
 *               This file contains exported stuffs from linked list module.
 */

#ifndef _LINK_LIST_H_
#define _LINK_LIST_H_

#ident "$Id$"


#define LINK_LIST_DEFAULT_ASSIGN_FUNC	link_list_default_assign_func
#define LINK_LIST_DEFAULT_COMPARE_FUNC	link_list_default_compare_func

#define LINK_LIST_FIND_VALUE(VALUE, HEAD, KEY, KEY_CMP_FUNC)	\
	do {							\
	  T_LIST	*node;					\
	  node = link_list_find(HEAD, KEY, NULL, KEY_CMP_FUNC, NULL);	\
	  if (node == NULL)					\
	    VALUE = NULL;					\
	  else							\
	    VALUE = node->value;				\
	} while (0)


typedef struct list_tag T_LIST;
struct list_tag
{
  void *key;
  void *value;
  struct list_tag *next;
};


extern int link_list_add (T_LIST **, void *, void *,
			  int (*)(T_LIST *, void *, void *));
extern T_LIST *link_list_find (T_LIST *, void *, void *,
			       int (*)(void *, void *), int (*)(void *,
								void *));
extern int link_list_node_delete2 (T_LIST **, void *, void *,
				   int (*)(void *, void *), int (*)(void *,
								    void *),
				   void (*)(T_LIST *));
extern int link_list_delete (T_LIST **, void (*)(T_LIST *));
extern int link_list_node_delete (T_LIST **, void *, int (*)(void *, void *),
				  void (*)(T_LIST *));

extern int link_list_default_assign_func (T_LIST * node, void *key,
					  void *value);
extern int link_list_default_compare_func (void *key, void *value);
extern void *link_list_traverse (T_LIST * head,
				 void *(*traverse_func) (T_LIST *, void *));

#endif /* _LINK_LIST_H_ */
