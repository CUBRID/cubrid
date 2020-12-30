/*
 * Copyright 2008 Search Solution Corporation
 * Copyright 2016 CUBRID Corporation
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
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
{				/* data list entry in the list */
  CSS_LIST_ENTRY *next;		/* point to the next css_list_entry */
  void *data;			/* point to data */
};

typedef struct css_list CSS_LIST;
struct css_list
{
  CSS_LIST_ENTRY *front;	/* point to the first css_list_entry */
  CSS_LIST_ENTRY **back;	/* point to addr. of pointer to next */

  CSS_LIST_ENTRY *free_list;
  int count;
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
#if defined (ENABLE_UNUSED_FUNCTION)
extern bool css_list_isempty (CSS_LIST * ptr);
extern int css_add_list_to_head (CSS_LIST * ptr, void *item);
extern int css_remove_list (CSS_LIST * ptr, void *item);
#endif
extern int css_add_list (CSS_LIST * ptr, void *item);
extern void *css_remove_list_from_head (CSS_LIST * ptr);
extern int css_traverse_list (CSS_LIST * ptr, int (*func) (void *, void *), void *arg);

#endif /* _CONNECTION_LIST_SR_H_ */
