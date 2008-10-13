/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * list.h - 
 */

#ifndef _LIST_H_
#define _LIST_H_

#ident "$Id$"

typedef struct css_list_entry CSS_LIST_ENTRY;
struct css_list_entry
{				/* data list entry in the list  */
  CSS_LIST_ENTRY *next;		/* point to the next css_list_entry */
  void *data;			/* point to data */
};

typedef struct css_list CSS_LIST;
struct css_list
{
  CSS_LIST_ENTRY *front;	/* point to the first css_list_entry */
  CSS_LIST_ENTRY **back;	/* point to addr. of pointer to next */

  int count;
  CSS_LIST_ENTRY *free_list;
  int free_count;
};

enum
{ TRAV_CONT = 0,
  TRAV_STOP = 1,
  TRAV_STOP_DELETE = 2,
  TRAV_CONT_DELETE = 3
};

extern int css_initialize_list (CSS_LIST * ptr, int free_count);
extern int css_finalize_list (CSS_LIST * ptr);
extern bool css_list_isempty (CSS_LIST * ptr);
extern int css_add_list (CSS_LIST * ptr, void *item);
extern int css_add_list_to_head (CSS_LIST * ptr, void *item);
extern int css_remove_list (CSS_LIST * ptr, void *item);
extern void *css_remove_list_from_head (CSS_LIST * ptr);
extern int css_traverse_list (CSS_LIST * ptr, int (*func) (void *, void *),
			      void *arg);

#endif /* _LIST_H_ */
