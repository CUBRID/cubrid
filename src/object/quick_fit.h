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
 *      quick_fit.h: Definitions for the Qick Fit allocation system.
 */

#ifndef _QUICK_FIT_H_
#define _QUICK_FIT_H_

#ident "$Id$"

extern HL_HEAPID db_create_workspace_heap (void);
extern void db_destroy_workspace_heap (void);

extern void db_ws_free (void *obj);
extern void *db_ws_alloc (size_t bytes);
extern void *db_ws_realloc (void *obj, size_t newsize);

#endif /* _QUICK_FIT_H_ */
