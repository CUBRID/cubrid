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
 * connection_list_sr.h - 
 */

#ifndef _CONNECTION_LIST_SR_H_
#define _CONNECTION_LIST_SR_H_

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

#endif /* _CONNECTION_LIST_SR_H_ */
