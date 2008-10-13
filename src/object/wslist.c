/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 *      wslist.c: General purpose list routines.
 *
 * Note:
 *    These functions support common list operations and can be used
 *    on structures of any type provided that the structure header is
 *    set up to match the list element headers.
 *    These are used primarily by the schema manager but are generally
 *    usefull as well.  The only need to have these under the workspace
 *    module is because the routines that allocate storage assume
 *    allocation within the workspace.
 *
 */

#ident "$Id$"

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "memory_manager_2.h"
#include "memory_manager_1.h"
#include "work_space.h"
#include "error_manager.h"

/*
 * Objlist_area
 *    Area for allocating external object list links.
 */
static AREA *Objlist_area = NULL;

/*
 * ws_area_init
 *    Initialize the areas used by the workspace manager.
 *
 */
#define OBJLIST_AREA_COUNT 4096

/*
 * ws_area_init - initialize area for object list links.
 *    return: void
 */
void
ws_area_init ()
{
  Objlist_area = area_create ("Object list links", sizeof (DB_OBJLIST),
			      OBJLIST_AREA_COUNT, true);
}

/*
 * LIST UTILITIES
 */
/*
 * These operations assume a structure with a single link field at the top.
 *
 * struct link {
 *   struct link *next;
 * };
 *
 */


/*
 * ws_list_append - append element to the end of a list
 *    return: none
 *    root(in/out): pointer to pointer to list head
 *    element(in): element to add
 */
void
ws_list_append (DB_LIST ** root, DB_LIST * element)
{
  DB_LIST *el;

  for (el = *root; (el != NULL) && (el->next != NULL); el = el->next);
  if (el == NULL)
    {
      *root = element;
    }
  else
    {
      el->next = element;
    }
}

/*
 * ws_list_remove - Removes an element from a list if it exists.
 *    return: non-zero if the element was removed
 *    root(): pointer to pointer to list head
 *    element(): element to remove
 */
int
ws_list_remove (DB_LIST ** root, DB_LIST * element)
{
  DB_LIST *el, *prev;
  int removed;

  removed = 0;
  for (el = *root, prev = NULL; el != NULL && el != element; el = el->next)
    {
      prev = el;
    }

  if (el != element)
    {
      return removed;
    }
  if (prev == NULL)
    {
      *root = element->next;
    }
  else
    {
      prev->next = element->next;
    }
  removed = 1;

  return (removed);
}

/*
 * ws_list_length - return the number of elements in a list
 *    return: length of the list (zero if empty)
 *    list(in): list to examine
 */
int
ws_list_length (DB_LIST * list)
{
  DB_LIST *el;
  int length = 0;

  for (el = list; el != NULL; el = el->next)
    {
      length++;
    }

  return (length);
}

/*
 * ws_list_free - apply (free) function over the elements of a list
 *    return: none
 *    list(in): list to free
 *    function(in): function to perform the freeing of elements
 */
void
ws_list_free (DB_LIST * list, LFREEER function)
{
  DB_LIST *link, *next;

  for (link = list, next = NULL; link != NULL; link = next)
    {
      next = link->next;
      (*function) (link);
    }
}


/*
 * ws_list_total - maps a function over the elements of a list and totals up
 * the integers returned by the mapping function.
 *    return: total of all calls to mapping function
 *    list(in): list to examine
 *    function(in): function to call on list elements
 */
int
ws_list_total (DB_LIST * list, LTOTALER function)
{
  DB_LIST *el;
  int total = 0;

  for (el = list; el != NULL; el = el->next)
    {
      total += (*function) (el);
    }

  return (total);
}


/*
 * ws_list_copy - Copies a list by calling a copier function for each element.
 *    return: new list
 *    src(in): list to copy
 *    copier(in): function to copy the elements
 *    freeer(in): function to free the elements
 */
DB_LIST *
ws_list_copy (DB_LIST * src, LCOPIER copier, LFREEER freeer)
{
  DB_LIST *list, *last, *new_;

  list = last = NULL;
  for (; src != NULL; src = src->next)
    {
      new_ = (DB_LIST *) (*copier) (src);
      if (new_ == NULL)
	{
	  goto memory_error;
	}

      new_->next = NULL;
      if (list == NULL)
	{
	  list = new_;
	}
      else
	{
	  last->next = new_;
	}
      last = new_;
    }
  return (list);

memory_error:
  if (freeer != NULL)
    {
      ws_list_free (list, freeer);
    }
  return NULL;
}


/*
 * ws_list_nconc - concatenate list2 to list1
 *    return: list pointer
 *    list1(out): first list
 *    list2(in): list to concatenate
 * Note:
 *    If list1 was NULL, it returns a pointer to list2.
 */
DB_LIST *
ws_list_nconc (DB_LIST * list1, DB_LIST * list2)
{
  DB_LIST *el, *result;

  if (list1 == NULL)
    {
      result = list2;
    }
  else
    {
      result = list1;
      for (el = list1; el->next != NULL; el = el->next);
      el->next = list2;
    }
  return (result);
}

/*
 * NAMED LIST UTILITIES
 */
/*
 * These utilities assume elements with a link field and a name.
 * struct named_link {
 *   struct named_link *next;
 *   const char *name;
 * }
 */


/*
 * nlist_find - Search a name list for an entry with the given name.
 *    return: namelist entry
 *    list(in): list to search
 *    name(in): element name to look for
 *    fcn(in): compare function
 */
DB_NAMELIST *
nlist_find (DB_NAMELIST * list, const char *name, NLSEARCHER fcn)
{
  DB_NAMELIST *el, *found;

  found = NULL;

  if (fcn == NULL)
    {
      fcn = (NLSEARCHER) strcmp;
    }

  for (el = list; el != NULL && found == NULL; el = el->next)
    {
      if ((el->name == name) ||
	  ((el->name != NULL) && (name != NULL)
	   && (*fcn) (el->name, name) == 0))
	{
	  found = el;
	}
    }
  return (found);
}


/*
 * nlist_remove - Removes a named element from a list.
 *    return: removed element (if found), NULL otherwise
 *    root(in/out): pointer to pointer to list head
 *    name(in): name of entry to remove
 *    fcn(in): compare function
 * Note:
 *    If an element with the given name was found it is removed and returned.
 *    If an element was not found, NULL is returned.
 */
DB_NAMELIST *
nlist_remove (DB_NAMELIST ** root, const char *name, NLSEARCHER fcn)
{
  DB_NAMELIST *el, *prev, *found;

  if (fcn == NULL)
    {
      fcn = (NLSEARCHER) strcmp;
    }

  found = NULL;

  for (el = *root, prev = NULL; el != NULL && found == NULL; el = el->next)
    {
      if ((el->name == name) ||
	  ((el->name != NULL) && (name != NULL)
	   && (*fcn) (el->name, name) == 0))
	{
	  found = el;
	}
      else
	{
	  prev = el;
	}
    }
  if (found != NULL)
    {
      if (prev == NULL)
	{
	  *root = found->next;
	}
      else
	{
	  prev->next = found->next;
	}
    }

  return (found);
}


/*
 * nlist_add - Adds an element to a namelist if it does not already exist.
 *    return: NO_ERROR if the element was added , error code otherwise
 *    list(in/out): pointer to pointer to list head
 *    name(in): element name to add
 *    fcn(in):  compare function
 *    added_ptr(out): set to 1 if added
 */
int
nlist_add (DB_NAMELIST ** list, const char *name, NLSEARCHER fcn,
	   int *added_ptr)
{
  DB_NAMELIST *found, *new_;
  int status = 0;

  found = nlist_find (*list, name, fcn);

  if (found != NULL)
    {
      goto error;
    }

  new_ = (DB_NAMELIST *) db_ws_alloc (sizeof (DB_NAMELIST));
  if (new_ == NULL)
    {
      return er_errid ();
    }

  new_->name = ws_copy_string (name);
  if (new_->name == NULL)
    {
      db_ws_free (new_);
      return er_errid ();
    }

  new_->next = *list;
  *list = new_;
  status = 1;

error:
  if (added_ptr != NULL)
    {
      *added_ptr = status;
    }
  return NO_ERROR;
}


/*
 * nlist_append - appends an element to a namelist if it does not exist.
 *    return: NO_ERROR if the element was added , error code otherwise
 *    list(in/out): pointer to pointer to list head
 *    name(in): entry name to append
 *    fcn(in): compare function
 *    added_ptr(out): set to 1 if added
 */
int
nlist_append (DB_NAMELIST ** list, const char *name, NLSEARCHER fcn,
	      int *added_ptr)
{
  DB_NAMELIST *el, *found, *last, *new_;
  int status = 0;

  if (fcn == NULL)
    {
      fcn = (NLSEARCHER) strcmp;
    }

  if (name == NULL)
    {
      goto error;
    }

  found = NULL;
  last = NULL;

  for (el = *list; el != NULL && found == NULL; el = el->next)
    {
      if ((el->name == name) ||
	  ((el->name != NULL) && (name != NULL)
	   && (*fcn) (el->name, name) == 0))
	{
	  found = el;
	}
      last = el;
    }
  if (found != NULL)
    {
      goto error;
    }
  new_ = (DB_NAMELIST *) db_ws_alloc (sizeof (DB_NAMELIST));

  if (new_ == NULL)
    {
      return er_errid ();
    }

  new_->name = ws_copy_string (name);

  if (new_->name == NULL)
    {
      db_ws_free (new_);
      return er_errid ();
    }

  new_->next = NULL;

  if (last == NULL)
    {
      *list = new_;
    }
  else
    {
      last->next = new_;
    }
  status = 1;

error:
  if (added_ptr != NULL)
    {
      *added_ptr = status;
    }
  return NO_ERROR;
}


/*
 * nlist_find_or_append - searches for a name or appends the element.
 *    return: error code
 *    list(in/out): pointer to pointer to list head
 *    name(in): name of element to add
 *    fcn(in): compare funciont
 *    position(out): position of element if found or inserted
 */
int
nlist_find_or_append (DB_NAMELIST ** list, const char *name,
		      NLSEARCHER fcn, int *position)
{
  DB_NAMELIST *el, *found, *last, *new_;
  int psn = -1;

  if (fcn == NULL)
    {
      fcn = (NLSEARCHER) strcmp;
    }

  if (name != NULL)
    {
      found = last = NULL;
      for (el = *list, psn = 0; el != NULL && found == NULL; el = el->next)
	{
	  if ((el->name == name) ||
	      ((el->name != NULL) && (*fcn) (el->name, name) == 0))
	    {
	      found = el;
	    }
	  else
	    {
	      psn++;
	    }
	  last = el;
	}
      if (found == NULL)
	{
	  new_ = (DB_NAMELIST *) db_ws_alloc (sizeof (DB_NAMELIST));
	  if (new_ == NULL)
	    {
	      return er_errid ();
	    }

	  new_->name = ws_copy_string (name);
	  if (new_->name == NULL)
	    {
	      db_ws_free (new_);
	      return er_errid ();
	    }

	  new_->next = NULL;
	  if (last == NULL)
	    {
	      *list = new_;
	    }
	  else
	    {
	      last->next = new_;
	    }
	}
    }
  *position = psn;
  return NO_ERROR;
}


/*
 * nlist_free - frees a name list
 *    return: none
 *    list(in/out): list to free
 */
void
nlist_free (DB_NAMELIST * list)
{
  DB_NAMELIST *el, *next;

  for (el = list, next = NULL; el != NULL; el = next)
    {
      next = el->next;
      db_ws_free ((char *) el->name);
      db_ws_free (el);
    }
}


/*
 * nlist_copy - makes a copy of a named list
 *    return: new namelist
 *    list(in): namelist to copy
 */
DB_NAMELIST *
nlist_copy (DB_NAMELIST * list)
{
  DB_NAMELIST *first, *last, *el, *new_;

  first = last = NULL;
  for (el = list; el != NULL; el = el->next)
    {
      new_ = (DB_NAMELIST *) db_ws_alloc (sizeof (DB_NAMELIST));
      if (new_ == NULL)
	{
	  goto memory_error;
	}

      new_->name = ws_copy_string (el->name);
      if (new_->name == NULL)
	{
	  db_ws_free (new_);
	  goto memory_error;
	}

      new_->next = NULL;
      if (first == NULL)
	{
	  first = new_;
	}
      else
	{
	  last->next = new_;
	}
      last = new_;
    }
  return first;

memory_error:
  nlist_free (first);
  return NULL;
}


/*
 * nlist_filter - remove all elements with the given name from a list
 * and return a list of the removed elements.
 *    return: filtered list of elements
 *    root(in/out): pointer to pointer to list head
 *    name(in): name of elements to filter
 *    fcn(in): compare function
 */
DB_NAMELIST *
nlist_filter (DB_NAMELIST ** root, const char *name, NLSEARCHER fcn)
{
  DB_NAMELIST *el, *prev, *next, *head, *filter;

  if (fcn == NULL)
    {
      fcn = (NLSEARCHER) strcmp;
    }

  filter = NULL;
  head = *root;

  for (el = head, prev = NULL, next = NULL; el != NULL; el = next)
    {
      next = el->next;
      if ((el->name == name) ||
	  ((el->name != NULL) && (name != NULL)
	   && (*fcn) (el->name, name) == 0))
	{
	  if (prev == NULL)
	    {
	      head = next;
	    }
	  else
	    {
	      prev->next = next;
	    }
	  el->next = filter;
	  filter = el;
	}
      else
	{
	  prev = el;
	}
    }

  *root = head;
  return (filter);
}

/*
 * MOP LIST UTILITIES
 */
/*
 * These utilities operate on a list of MOP links.
 * This is such a common operation for the workspace and schema manager that
 * it merits its own optimized implementation.
 *
 */


/*
 * ml_find - searches a list for the given mop.
 *    return: non-zero if mop was in the list
 *    list(in): list to search
 *    mop(in): mop we're looking for
 */
int
ml_find (DB_OBJLIST * list, MOP mop)
{
  DB_OBJLIST *l;
  int found;

  found = 0;
  for (l = list; l != NULL && found == 0; l = l->next)
    {
      if (l->op == mop)
	found = 1;
    }
  return (found);
}


/*
 * ml_add - Adds a MOP to the list if it isn't already present.
 *    return: NO_ERROR or error code
 *    list(in/out): pointer to pointer to list head
 *    mop(in): mop to add to the list
 *    added_ptr(out): set to 1 if added
 * Note:
 *    There is no guarentee where the MOP will be added in the list although
 *    currently it will push it at the head of the list.  Use ml_append
 *    if you must ensure ordering.
 */
int
ml_add (DB_OBJLIST ** list, MOP mop, int *added_ptr)
{
  DB_OBJLIST *l, *found, *new_;
  int added;

  added = 0;
  if (mop == NULL)
    {
      goto error;
    }

  for (l = *list, found = NULL; l != NULL && found == NULL; l = l->next)
    {
      if (l->op == mop)
	{
	  found = l;
	}
    }
  /* since we can get the end of list easily, may want to append here */
  if (found != NULL)
    {
      goto error;
    }

  new_ = (DB_OBJLIST *) db_ws_alloc (sizeof (DB_OBJLIST));
  if (new_ == NULL)
    {
      return er_errid ();
    }
  new_->op = mop;
  new_->next = *list;
  *list = new_;
  added = 1;

error:
  if (added_ptr != NULL)
    {
      *added_ptr = added;
    }
  return NO_ERROR;
}


/*
 * ml_append - Appends a MOP to the list if it isn't already present.
 *    return: NO_ERROR or error code
 *    list(in/out): pointer to pointer to list head
 *    mop(in): mop to add
 *    added_ptr(out): set to 1 if added
 */
int
ml_append (DB_OBJLIST ** list, MOP mop, int *added_ptr)
{
  DB_OBJLIST *l, *found, *new_, *last;
  int added;

  added = 0;
  if (mop == NULL)
    {
      goto error;
    }

  last = NULL;
  for (l = *list, found = NULL; l != NULL && found == NULL; l = l->next)
    {
      if (l->op == mop)
	{
	  found = l;
	}
      last = l;
    }
  /* since we can get the end of list easily, may want to append here */

  if (found != NULL)
    {
      goto error;
    }

  new_ = (DB_OBJLIST *) db_ws_alloc (sizeof (DB_OBJLIST));
  if (new_ == NULL)
    {
      return er_errid ();
    }
  new_->op = mop;
  new_->next = NULL;
  if (last == NULL)
    {
      *list = new_;
    }
  else
    {
      last->next = new_;
    }
  added = 1;

error:

  if (added_ptr != NULL)
    {
      *added_ptr = added;
    }
  return NO_ERROR;
}


/*
 * ml_remove - removes a mop from a mop list if it is found.
 *    return: non-zero if mop was removed
 *    list(in/out): pointer to pointer to list head
 *    mop(in): mop to remove from the list
 */
int
ml_remove (DB_OBJLIST ** list, MOP mop)
{
  DB_OBJLIST *l, *found, *prev;
  int deleted;

  deleted = 0;
  for (l = *list, found = NULL, prev = NULL; l != NULL && found == NULL;
       l = l->next)
    {
      if (l->op == mop)
	{
	  found = l;
	}
      else
	{
	  prev = l;
	}
    }
  if (found != NULL)
    {
      if (prev == NULL)
	{
	  *list = found->next;
	}
      else
	{
	  prev->next = found->next;
	}
      db_ws_free (found);
      deleted = 1;
    }
  return (deleted);
}


/*
 * ml_free - free a list of MOPs.
 *    return: none
 *    list(in/out): list to free
 */
void
ml_free (DB_OBJLIST * list)
{
  DB_OBJLIST *l, *next;

  for (l = list, next = NULL; l != NULL; l = next)
    {
      next = l->next;
      db_ws_free (l);
    }
}


/*
 * ml_copy - copy a list of mops.
 *    return: new list
 *    list(in): list to copy
 */
DB_OBJLIST *
ml_copy (DB_OBJLIST * list)
{
  DB_OBJLIST *l, *new_, *first, *last;

  first = last = NULL;
  for (l = list; l != NULL; l = l->next)
    {
      new_ = (DB_OBJLIST *) db_ws_alloc (sizeof (DB_OBJLIST));
      if (new_ == NULL)
	{
	  goto memory_error;
	}

      new_->next = NULL;
      new_->op = l->op;
      if (first == NULL)
	{
	  first = new_;
	}
      else
	{
	  last->next = new_;
	}
      last = new_;
    }
  return (first);

memory_error:
  ml_free (first);
  return NULL;
}


/*
 * ml_size - This calculates the number of bytes of memory required for the
 * storage of a MOP list.
 *    return: memory size of list
 *    list(in): list to examine
 */
int
ml_size (DB_OBJLIST * list)
{
  int size = 0;

  size = ws_list_length ((DB_LIST *) list) * sizeof (DB_OBJLIST);

  return (size);
}


/*
 * ml_filter - maps a function over the mops in a list selectively removing
 * elements based on the results of the filter function.
 *    return: void
 *    list(in/out): pointer to pointer to mop list
 *    filter(in): filter function
 *    args(in): args to pass to filter function
 * Note:
 *    If the filter function returns zero, the mop will be removed.
 */
void
ml_filter (DB_OBJLIST ** list, MOPFILTER filter, void *args)
{
  DB_OBJLIST *l, *prev, *next;
  int keep;

  prev = NULL;
  next = NULL;

  for (l = *list; l != NULL; l = next)
    {
      next = l->next;
      keep = (*filter) (l->op, args);
      if (keep)
	{
	  prev = l;
	}
      else
	{
	  if (prev != NULL)
	    {
	      prev->next = next;
	    }
	  else
	    {
	      *list = next;
	    }
	}
    }
}

/*
 * DB_OBJLIST AREA ALLOCATION
 */


/*
 * ml_ext_alloc_link - This is used to allocate a mop list link for return to
 * the application layer.
 *    return: new mop list link
 * Note:
 *    These links must be allocated in areas outside the workspace
 *    so they serve as roots to the garabage collector.
 */
DB_OBJLIST *
ml_ext_alloc_link (void)
{
  return ((DB_OBJLIST *) area_alloc (Objlist_area));
}


/*
 * ml_ext_free_link - frees a mop list link that was allocated with
 * ml_ext_alloc_link.
 *    return: void
 *    link(in/out): link to free
 */
void
ml_ext_free_link (DB_OBJLIST * link)
{
  if (link != NULL)
    {
      link->op = NULL;		/* this is important */
      area_free (Objlist_area, (void *) link);
    }
}


/*
 * ml_ext_free - frees a complete list of links allocated with the
 * ml_ext_alloc_link function.
 *    return: void
 *    list(in/out): list to free
 */
void
ml_ext_free (DB_OBJLIST * list)
{
  DB_OBJLIST *l, *next;

  if (list == NULL)
    {
      return;
    }

  if (area_validate (Objlist_area, 0, (void *) list) == NO_ERROR)
    {
      for (l = list, next = NULL; l != NULL; l = next)
	{
	  next = l->next;
	  ml_ext_free_link (l);
	}
    }
}


/*
 * ml_ext_copy - Like ml_copy except that it allocates the mop list links using
 * ml_ext_alloc_link so they can be returned to the application level.
 *    return: new mop list
 *    list(in): list to copy
 */
DB_OBJLIST *
ml_ext_copy (DB_OBJLIST * list)
{
  DB_OBJLIST *l, *new_, *first, *last;

  first = NULL;
  last = NULL;

  for (l = list; l != NULL; l = l->next)
    {
      new_ = ml_ext_alloc_link ();
      if (new_ == NULL)
	{
	  goto memory_error;
	}
      new_->next = NULL;
      new_->op = l->op;
      if (first == NULL)
	{
	  first = new_;
	}
      else
	{
	  last->next = new_;
	}
      last = new_;
    }
  return (first);

memory_error:
  ml_ext_free (first);
  return NULL;
}


/*
 * ml_ext_add - same as ml_add except that it allocates a mop in the external
 * area so it serves as a GC root.
 *    return: NO_ERROR or error code
 *    list(in/out): pointer to pointer to list head
 *    mop(in): mop to add to the list
 *    added_ptr(out): set to 1 if added
 */
int
ml_ext_add (DB_OBJLIST ** list, MOP mop, int *added_ptr)
{
  DB_OBJLIST *l, *found, *new_;
  int added;

  added = 0;
  if (mop == NULL)
    {
      goto error;
    }
  for (l = *list, found = NULL; l != NULL && found == NULL; l = l->next)
    {
      if (l->op == mop)
	{
	  found = l;
	}
    }
  /* since we can get the end of list easily, may want to append here */
  if (found == NULL)
    {
      new_ = (DB_OBJLIST *) area_alloc (Objlist_area);
      if (new_ == NULL)
	{
	  return er_errid ();
	}

      new_->op = mop;
      new_->next = *list;
      *list = new_;
      added = 1;
    }

error:
  if (added_ptr != NULL)
    {
      *added_ptr = added;
    }
  return NO_ERROR;
}
