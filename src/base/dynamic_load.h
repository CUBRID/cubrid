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
 * dynamic_load.h - Dynamic loader for run-time inclusion of object code
 *
 */

#ifndef _DYNAMIC_LOAD_H_
#define _DYNAMIC_LOAD_H_

#ident "$Id$"

#if defined(SOLARIS)
#include <dlfcn.h>
#include <nlist.h>
#endif /* SOLARIS */
#if defined(sun)
#include <nlist.h>
#endif /* sun */
#if defined(HPUX)
#include <filehdr.h>
#include <dl.h>
#include <nlist.h>
#endif /* UPUX */
#if defined(_AIX)
#include <filehdr.h>
#include <aouthdr.h>
#include <sys/ldr.h>
#include <nlist.h>
#endif /* _AIX */
#if defined(LINUX)
#include <dlfcn.h>
#include <nlist.h>
#endif /* LINUX */

#if defined(SOLARIS) || defined(LINUX)
/* the nlist types from a.out.h */
#if !defined(N_UNDF)
#define N_UNDF  0x0		/* undefined */
#endif
#if !defined(N_EXT)
#define N_EXT   01		/* external bit, or'ed in */
#endif
#if !defined(N_TEXT)
#define N_TEXT  0x4		/* text */
#endif
#endif /* SOLARIS || LINUX */

/* Sun has valloc, HP doesn't.  The VALLOC macro hides this.
   Use free, not free_and_init to free the result of VALLOC. */
#if (defined(sun) || defined(sparc)) && !defined(SOLARIS)
#define VALLOC valloc
#elif defined(HPUX)
#define VALLOC malloc
#endif /* (sun || sparc) && !SOLARIS */

enum dl_estimate_mode
{ DL_RELATIVE, DL_ABSOLUTE };

#ifndef HPUX
struct nlist;
#endif /* HPUX */

extern int dl_Errno;

#if !defined(LINUX) && !defined(AIX)
extern const char *sys_errlist[];
#endif /* !LINUX && !AIX */


#if !defined(SOLARIS) && !defined(HPUX) && !defined(AIX) && !defined(LINUX)
extern int nlist (char *, struct nlist *);
#endif /* !SOLARIS && !HPUX && !AIX && !LINUX */

#if !defined (SOLARIS) && !defined(LINUX)
extern int dl_initiate_module (const char *);
#else /* SOLARIS || LINUX */
extern int dl_initiate_module (void);
#endif /* SOLARIS || LINUX */

extern int dl_destroy_module (void);

#if defined (sun) || defined(SOLARIS) || defined(LINUX) || defined(HPUX)
extern int dl_resolve_object_symbol (struct nlist *syms);
#elif defined(_AIX)
extern int dl_load_and_resolve (const char **,
				const char **, const char **, struct nlist *);
#endif /* _AIX */

#if defined(HPUX) || defined(SOLARIS) || defined(LINUX)
#if defined (ENABLE_UNUSED_FUNCTION)
extern int dl_load_object_with_estimate (const char **obj_files,
					 const char **msgp);
#endif /* ENABLE_UNUSED_FUNCTION */
extern int dl_load_object_module (const char **, const char **);
#elif (defined(sun) || defined(sparc)) && !defined(SOLARIS)
extern int dl_load_object_module (const char **, const char **,
				  const char **);
#if defined (ENABLE_UNUSED_FUNCTION)
extern int dl_load_object_with_estimate (size_t * actual_size,
					 const char **obj_files,
					 const char **msgp, const char **libs,
					 const size_t estimated_size,
					 enum dl_estimate_mode mode);
#endif /* ENABLE_UNUSED_FUNCTION */
#endif /* (defined(sun) || defined(sparc)) && !defined(SOLARIS) */

#endif /* _DYNAMIC_LOAD_H_ */
