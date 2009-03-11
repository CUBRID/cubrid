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
 * fbo_class.h -
 */

#ifndef _FBO_CLASS_H_
#define _FBO_CLASS_H_

#ident "$Id$"

#if defined(WINDOWS)
#include <io.h>
#endif

extern int esm_create (char *pathname);
extern void esm_destroy (char *pathname);
extern int esm_get_size (char *pathname);
extern int esm_read (char *pathname, const off_t offset, const int size,
		     char *buffer);
extern int esm_write (char *pathname, const off_t offset, const int size,
		      char *buffer);
extern int esm_insert (char *pathname, const off_t offset, const int size,
		       char *buffer);
extern int esm_delete (char *pathname, off_t offset, const int size);
extern int esm_truncate (char *pathname, const int offset);
extern int esm_append (char *pathname, const int size, char *buffer);

#endif /* _FBO_CLASS_H_ */
