/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 */

/*
 * es_list.h -
 *
 * Simple doubly linked list implementation.
 *
 * Some of the internal functions ("__xxx") are useful when
 * manipulating whole lists rather than single entries, as
 * sometimes we already know the next/prev entries and we can
 * generate better code by using them directly rather than
 * using the generic single-entry routines.
 */

#ifndef _ES_LIST_H_
#define _ES_LIST_H_

#ifdef  __cplusplus
extern "C"
{
#endif

#if defined(WINDOWS)
#define inline
#endif
	
struct es_list_head
{
  struct es_list_head *next, *prev;
};
typedef struct es_list_head es_list_head_t;

#define ES_LIST_HEAD_INIT(name) { &(name), &(name) }

#define ES_LIST_HEAD(name) \
	struct es_list_head name = ES_LIST_HEAD_INIT(name)

#define ES_INIT_LIST_HEAD(ptr) do { \
	(ptr)->next = (ptr); (ptr)->prev = (ptr); \
} while (0)

#define ES_LIST_FIRST(name)	(name)->next
#define ES_LIST_LAST(name)	(name)->prev

/*
 * Insert a new entry between two known consecutive entries.
 *
 * This is only for internal list manipulation where we know
 * the prev/next entries already!
 */
static inline void __list_add (struct es_list_head *new_item,
				     struct es_list_head *prev,
				     struct es_list_head *next)
{
  next->prev = new_item;
  new_item->next = next;
  new_item->prev = prev;
  prev->next = new_item;
}

/**
 * es_list_add - add a new entry
 * @new: new entry to be added
 * @head: list head to add it after
 *
 * Insert a new entry after the specified head.
 * This is good for implementing stacks.
 */
static inline void es_list_add (struct es_list_head *new_item,
				      struct es_list_head *head)
{
  __list_add (new_item, head, head->next);
}

/**
 * es_list_add_tail - add a new entry
 * @new: new entry to be added
 * @head: list head to add it before
 *
 * Insert a new entry before the specified head.
 * This is useful for implementing queues.
 */
static inline void es_list_add_tail (struct es_list_head *new_item,
					   struct es_list_head *head)
{
  __list_add (new_item, head->prev, head);
}

/*
 * Delete a list entry by making the prev/next entries
 * point to each other.
 *
 * This is only for internal list manipulation where we know
 * the prev/next entries already!
 */
static inline void __list_del (struct es_list_head *prev,
				     struct es_list_head *next)
{
  next->prev = prev;
  prev->next = next;
}

/**
 * es_list_del - deletes entry from list.
 * @entry: the element to delete from the list.
 * Note: es_list_empty on entry does not return true after this, the entry is in an undefined state.
 */
static inline void es_list_del (struct es_list_head *entry)
{
  __list_del (entry->prev, entry->next);
  entry->next = entry->prev = 0;
}

/**
 * es_list_del_init - deletes entry from list and reinitialize it.
 * @entry: the element to delete from the list.
 */
static inline void es_list_del_init (struct es_list_head *entry)
{
  __list_del (entry->prev, entry->next);
  ES_INIT_LIST_HEAD (entry);
}

/**
 * es_list_empty - tests whether a list is empty
 * @head: the list to test.
 */
static inline int es_list_empty (struct es_list_head *head)
{
  return head->next == head;
}

/**
 * LIST_SPLICE - join two lists
 * @list: the new list to add.
 * @head: the place to add it in the first list.
 */
static inline void es_list_splice (struct es_list_head *list,
					 struct es_list_head *head)
{
  struct es_list_head *first = list->next;

  if (first != list)
    {
      struct es_list_head *last = list->prev;
      struct es_list_head *at = head->next;

      first->prev = head;
      head->next = first;

      last->next = at;
      at->prev = last;
    }
}

/**
 * ES_LIST_ENTRY - get the struct for this entry
 * @ptr:	the &struct es_list_head pointer.
 * @type:	the type of the struct this is embedded in.
 * @member:	the name of the list_struct within the struct.
 */
#define ES_LIST_ENTRY(ptr, type, member) \
	((type *)((char *)(ptr)-(unsigned long)(&((type *)0)->member)))

/**
 * ES_LIST_FOR_EACH	-	iterate over a list
 * @pos:	the &struct es_list_head to use as a loop counter.
 * @head:	the head for your list.
 */
#define ES_LIST_FOR_EACH(pos, head) \
	for (pos = (head)->next; pos != (head); pos = pos->next)

/**
 * LIST_FOR_REVERSE_EACH	-	iterate over a list with reverse order
 * @pos:	the &struct es_list_head to use as a loop counter.
 * @head:	the head for your list.
 */
#define ES_LIST_FOR_REVERSE_EACH(pos, head) \
	for (pos = (head)->prev; pos != (head); pos = pos->prev)

/**
 * LIST_FOR_EACH_SAFE	-	iterate over a list safe against removal of list entry
 * @pos:	the &struct es_list_head to use as a loop counter.
 * @n:		another &struct es_list_head to use as temporary storage
 * @head:	the head for your list.
 */
#define ES_LIST_FOR_EACH_SAFE(pos, n, head) \
	for (pos = (head)->next, n = pos->next; pos != (head); \
		pos = n, n = pos->next)

#ifdef  __cplusplus
}
#endif

#endif /* _ES_LIST_H_ */
