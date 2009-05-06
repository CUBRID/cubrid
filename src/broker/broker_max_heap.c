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
 * broker_max_heap.c - 
 */

#ident "$Id$"

#include "broker_max_heap.h"

int
max_heap_insert (T_MAX_HEAP_NODE * max_heap, int max_heap_size,
		 T_MAX_HEAP_NODE * item)
{
  int i;

  if (max_heap->id >= max_heap_size)
    {
      return -1;
    }

  i = ++(max_heap->id);
  while ((i != 1) && (item->priority > max_heap[i / 2].priority))
    {
      max_heap[i] = max_heap[i / 2];
      i /= 2;
    }
  max_heap[i] = *item;
  return 0;
}

int
max_heap_change_priority (T_MAX_HEAP_NODE * max_heap, int id,
			  int new_priority)
{
  T_MAX_HEAP_NODE item;
  int i, k;

  k = -1;
  for (i = 1; i <= max_heap[0].id; i++)
    {
      if (max_heap[i].id == id)
	{
	  item = max_heap[i];
	  item.priority = new_priority;
	  k = i;
	  break;
	}
    }
  if (k < 0)
    return -1;

  while ((k != 1) && (item.priority > max_heap[k / 2].priority))
    {
      max_heap[k] = max_heap[k / 2];
      k /= 2;
    }
  max_heap[k] = item;
  return 0;
}

int
max_heap_delete (T_MAX_HEAP_NODE * max_heap, T_MAX_HEAP_NODE * ret)
{
  T_MAX_HEAP_NODE temp;
  int parent, child;

  if (max_heap[0].id <= 0)
    return -1;

  *ret = max_heap[1];

  temp = max_heap[(max_heap[0].id)--];
  parent = 1;
  child = 2;

  while (child <= max_heap[0].id)
    {
      if ((child < max_heap[0].id)
	  && (max_heap[child].priority < max_heap[child + 1].priority))
	child++;
      if (temp.priority > max_heap[child].priority)
	break;
      max_heap[parent] = max_heap[child];
      parent = child;
      child *= 2;
    }
  max_heap[parent] = temp;
  return 0;
}

void
max_heap_incr_priority (T_MAX_HEAP_NODE * max_heap)
{
  int i;

  for (i = 1; i <= max_heap[0].id; i++)
    {
      (max_heap[i].priority)++;
    }
}
