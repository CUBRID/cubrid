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
 * link_list.c - This module contains operations for singly linked list. 
 *               The list header is a recent appended node.
 */

#ident "$Id$"


#include <stdio.h>
#include <stdlib.h>


#include "cas_common.h"
#include "broker_list.h"


static T_LIST *delete_node (T_LIST * head, T_LIST * del_node,
			    void (*node_dealloc) (T_LIST *));
static int true_func (void *key, void *value);
static void swap_node (T_LIST * node1, T_LIST * node2);


/*
 * name:        link_list_add - append a node to a designated list
 *
 * arguments:   cur_head    - the list header(IN/OUT)
 *              add_key - the key of the node
 *              add_val - the value of the node
 *              assign_func - a function for assigning key and value
 *
 * returns/side-effects:
 *              0 : success
 *		< 0 : error code
 *
 * description: add new node to the list.
 *              created node is new list header.
 *
 * NOTE:
 *  Since the type of 'key' and 'value' of the node is 'void*',
 *  it is only possible to assign value which size is same as void ptr.
 *  For example, if string is copied to 'key', you must allocate memory
 *  in assigning function.
 */
int
link_list_add (T_LIST ** cur_head, void *add_key, void *add_val,
	       int (*assign_func) (T_LIST *, void *, void *))
{
  T_LIST *tmp;
  T_LIST *new_node;
  int error;
  T_LIST *head = *cur_head;

  tmp = (T_LIST *) malloc (sizeof (T_LIST));
  if (tmp == NULL)
    return -1;

  if (head == NULL)
    {
      new_node = tmp;
    }
  else
    {
      *tmp = *head;
      new_node = head;
    }

  error = (*assign_func) (new_node, add_key, add_val);
  if (error < 0)
    {
      FREE_MEM (tmp);
      return error;
    }
  new_node->next = tmp;

  *cur_head = tmp;
  return 0;
}

/*
 * name:        link_list_find
 *
 * arguments:   head - list header
 *              key - key value for list search
 *              cmp_func - comparing function
 *
 * returns/side-effects:
 *              node ptr if exist.
 *		NULL ptr otherwise.
 *
 * description: search list with key value.
 *
 * NOTE:
 */
T_LIST *
link_list_find (T_LIST * head, void *key, void *val,
		int (*key_cmp_func) (void *, void *),
		int (*val_cmp_func) (void *, void *))
{
  T_LIST *tmp;

  if (head == NULL)
    return NULL;

  if (key_cmp_func == NULL)
    key_cmp_func = true_func;
  if (val_cmp_func == NULL)
    val_cmp_func = true_func;

  tmp = head;
  do
    {
      if ((*key_cmp_func) (tmp->key, key)
	  && (*val_cmp_func) (tmp->value, val))
	return tmp;
      tmp = tmp->next;
    }
  while (tmp != head);

  return NULL;			/* not found */
}

/*
 * name:        link_list_node_delete - delete a node
 *
 * arguments:   head	- list header
 *              key     - key value
 *		cmp_func - comparing function
 *		node_dealloc - 'key', 'value' deallocation function
 *
 * returns/side-effects:
 *              list header ptr after the node is deleted.
 *
 * description: delete one node from list.
 *
 * NOTE:
 *  If 'key' or 'value' has its own memory, the memory must be freed
 *  in 'node_dealloc' function. But, the node container is freed in
 *  this module.
 */
int
link_list_node_delete (T_LIST ** cur_head, void *key,
		       int (*cmp_func) (void *, void *),
		       void (*node_dealloc) (T_LIST *))
{
  T_LIST *tmp;
  T_LIST *del;
  T_LIST *head = *cur_head;

  while (1)
    {
      del = link_list_find (head, key, NULL, cmp_func, NULL);
      if (del == NULL)
	break;

      tmp = delete_node (head, del, node_dealloc);
      *cur_head = head = tmp;
    }


  return 0;
}

/*
 * name:        link_list_node-delete2 - delete a node
 *
 * arguments:   head	- list header
 *              key     - key value
 *		value
 *		key_cmp_func - key comparing function
 *		val_cmp_func - value comparing function
 *		node_dealloc - 'key', 'value' deallocation function
 *
 * returns/side-effects:
 *              list header ptr after the node is deleted.
 *
 * description: delete one node from list.
 *
 * NOTE:
 *  If 'key' or 'value' has its own memory, the memory must be freed
 *  in 'node_dealloc' function. But, the node container is freed in
 *  this module.
 */
int
link_list_node_delete2 (T_LIST ** cur_head, void *key, void *value,
			int (*key_cmp_func) (void *, void *),
			int (*val_cmp_func) (void *, void *),
			void (*node_dealloc) (T_LIST *))
{
  T_LIST *tmp;
  T_LIST *del;
  T_LIST *head = *cur_head;

  del = link_list_find (head, key, value, key_cmp_func, val_cmp_func);
  if (del == NULL)
    return -1;

  tmp = delete_node (head, del, node_dealloc);
  *cur_head = tmp;
  return 0;
}

/*
 * name:        link_list_delete - delete list
 *
 * arguments:   head    - list header
 *              node_dealloc - 'key', 'value' deallocation function
 *
 * returns/side-effects:
 *              (T_LIST*) NULL
 *
 * description: delete all nodes of list.
 *
 * NOTE:
 *  If 'key' or 'value' has its own memory, the memory must be freed
 *  in 'node_dealloc' function. But, the node container is freed in
 *  this module.
 */
int
link_list_delete (T_LIST ** cur_head, void (*node_dealloc) (T_LIST *))
{
  T_LIST *head = *cur_head;

  while (head)
    {
      head = delete_node (head, head, node_dealloc);
    }
  *cur_head = NULL;

  return 0;
}

void *
link_list_traverse (T_LIST * head, void *(*traverse_func) (T_LIST *, void *))
{
  T_LIST *tmp;
  void *result = NULL;

  if (head == NULL)
    return NULL;

  tmp = head;
  do
    {
      result = (*traverse_func) (tmp, result);
      tmp = tmp->next;
    }
  while (tmp != head);
  return result;
}

int
link_list_default_assign_func (T_LIST * node, void *key, void *value)
{
  node->key = key;
  node->value = value;
  return 0;
}

int
link_list_default_compare_func (void *key, void *value)
{
  if (key == value)
    return 1;
  return 0;
}


static T_LIST *
delete_node (T_LIST * head, T_LIST * del_node,
	     void (*node_dealloc) (T_LIST *))
{
  T_LIST *new_head;
  T_LIST *free_node;

  if (head == NULL)
    return NULL;

  if (del_node->next == head)
    {
      if (del_node == head)
	{
	  free_node = del_node;
	  new_head = NULL;
	}
      else
	{
	  free_node = del_node->next;
	  swap_node (del_node, del_node->next);
	  new_head = del_node;
	}
    }
  else
    {
      free_node = del_node->next;
      swap_node (del_node, del_node->next);
      new_head = head;
    }

  if (node_dealloc)
    (*node_dealloc) (free_node);
  FREE_MEM (free_node);

  return new_head;
}

static int
true_func (void *key, void *value)
{
  return 1;
}

static void
swap_node (T_LIST * node1, T_LIST * node2)
{
  T_LIST tmp;

  tmp = *node1;
  *node1 = *node2;
  *node2 = tmp;
}
