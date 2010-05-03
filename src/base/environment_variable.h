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

extern char *envvar_bindir_file (char *path, size_t size,
				 const char *filename);
#if defined(ENABLE_UNUSED_FUNCTION)
extern char *envvar_libdir_file (char *path, size_t size,
				 const char *filename);
#endif
extern char *envvar_javadir_file (char *path, size_t size,
				  const char *filename);
extern char *envvar_localedir_file (char *path, size_t size,
				    const char *langpath,
				    const char *filename);
extern char *envvar_confdir_file (char *path, size_t size,
				  const char *filename);
extern char *envvar_vardir_file (char *path, size_t size,
				 const char *filename);
extern char *envvar_tmpdir_file (char *path, size_t size,
				 const char *filename);
extern char *envvar_logdir_file (char *path, size_t size,
				 const char *filename);

#endif /* _ENVIRONMENT_VARIABLE_H_ */
