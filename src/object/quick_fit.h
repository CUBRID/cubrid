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
 *      quick_fit.h: Definitions for the Qick Fit allocation system.
 */

#ifndef _QUICK_FIT_H_
#define _QUICK_FIT_H_

#ident "$Id$"

/* free_and_init routine */
#define db_ws_free_and_init(obj) \
  do \
    { \
      db_ws_free ((obj)); \
      (obj) = NULL; \
    } \
  while (0)

extern HL_HEAPID db_create_workspace_heap (void);
extern void db_destroy_workspace_heap (void);

extern void db_ws_free (void *obj);
extern void *db_ws_alloc (size_t bytes);
extern void *db_ws_realloc (void *obj, size_t newsize);

#endif /* _QUICK_FIT_H_ */
