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
 * environment_variable.h : Functions for manipulating the environment variable
 *
 */

#ifndef _ENVIRONMENT_VARIABLE_H_
#define _ENVIRONMENT_VARIABLE_H_

#ident "$Id$"

extern const char *envvar_prefix (void);
extern const char *envvar_root (void);
extern const char *envvar_name (char *, size_t, const char *);
extern const char *envvar_get (const char *);
extern int envvar_set (const char *, const char *);
extern int envvar_expand (const char *, char *, size_t);

#endif /* _ENVIRONMENT_VARIABLE_H_ */
