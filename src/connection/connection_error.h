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
 * connection_error.h -
 */

#ifndef _CONNECTION_ERROR_H_
#define _CONNECTION_ERROR_H_

#ident "$Id$"

#include <stdio.h>
#ifdef SERVER_MODE
#include <errno.h>
#if !defined(WINDOWS)
#include <pthread.h>
#endif /* not WINDOWS */

#include "thread.h"
#include "error_manager.h"
#include "connection_defs.h"
#include "thread_impl.h"
#endif /* SERVER_MODE */

/* TODO: ER_CSS_NOERROR -> NO_ERROR */
#define ER_CSS_NOERROR  0

#if defined(SERVER_MODE)
#define CSS_CHECK(r, e) \
  do \
    { \
      if ((r) != 0) \
	{ \
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, (e), 0); \
	} \
    } \
  while (0)

#define CSS_CHECK_EXIT(r, e) \
  do \
    { \
      if ((r) != 0) \
	{ \
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, (e), 0); \
	  THREAD_EXIT (-1); \
	} \
    } \
  while (0)

#define CSS_CHECK_RETURN(r, e) \
  do \
    { \
      if ((r) != 0) \
	{ \
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, (e), 0); \
	  return; \
	} \
    } \
  while (0)

#define CSS_CHECK_RETURN_ERROR(r, e) \
  do { \
    if ((r) != 0) { \
      er_set_with_oserror(ER_ERROR_SEVERITY, ARG_FILE_LINE, (e), 0); \
      return (e); \
    } \
  } while(0)

#define CSS_CHECK_CONTINUE(r, e) \
  do \
    { \
      if ((r) != 0) \
	{ \
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, (e), 0); \
	  continue; \
	} \
    } \
  while (0)

#if defined(TRACE_LIST)
#define PRINT_INIT_LIST(p) \
	fprintf (stderr, "TID(%2d):%10s(%4d): Initialize LIST  (%p)\n", \
		 THREAD_ID(), __FILE__, __LINE__, (p))

#define PRINT_FINALIZE_LIST(p)	\
	fprintf (stderr, "TID(%2d):%10s(%4d): Finalize   LIST  (%p)\n", \
		 THREAD_ID(), __FILE__, __LINE__, (p))

#define PRINT_TRY_LOCK(p)	\
	fprintf (stderr, "TID(%2d):%10s(%4d): Before LOCK   on (%p)\n", \
		 THREAD_ID(), __FILE__, __LINE__, (p))

#define PRINT_DONE_LOCK(p)	\
	fprintf (stderr, "TID(%2d):%10s(%4d): After  LOCK   on (%p)\n", \
		 THREAD_ID(), __FILE__, __LINE__, (p))

#define PRINT_TRY_UNLOCK(p)	\
	fprintf (stderr, "TID(%2d):%10s(%4d): Before UNLOCK on (%p)\n", \
		 THREAD_ID(), __FILE__, __LINE__, (p))

#define PRINT_DONE_UNLOCK(p)	\
	fprintf (stderr, "TID(%2d):%10s(%4d): After  UNLOCK on (%p)\n", \
		 THREAD_ID(), __FILE__, __LINE__, (p))
#else /* TRACE_LIST */
#define PRINT_INIT_LIST(p)
#define PRINT_FINALIZE_LIST(p)
#define PRINT_TRY_LOCK(p)
#define PRINT_DONE_LOCK(p)
#define PRINT_TRY_UNLOCK(p)
#define PRINT_DONE_UNLOCK(p)
#endif /* TRACE_LIST */

#if defined(PRINTING)
#define CSS_TRACE1(a) \
  do \
    { \
      fprintf (stderr, "TID(%2d):%10s(%4d): " a, \
	       THREAD_ID (), __FILE__, __LINE__); \
    } \
  while (0)

#define CSS_TRACE2(a,b) \
  do \
    { \
      fprintf (stderr, "TID(%2d):%10s(%4d): " a, \
	       THREAD_ID (), __FILE__, __LINE__, (b)); \
    } \
  while (0)

#define CSS_TRACE3(a,b,c) \
  do \
    { \
      fprintf (stderr, "TID(%2d):%10s(%4d): " a, \
	       THREAD_ID (), __FILE__, __LINE__, (b), (c)); \
    } \
  while (0)

#define CSS_TRACE4(a,b,c,d) \
  do \
    { \
      fprintf (stderr, "TID(%2d):%10s(%4d): " a, \
	       THREAD_ID (), __FILE__, __LINE__, (b), (c), (d)); \
    } \
  while (0)

#define CSS_TRACE5(a,b,c,d,e) \
  do \
    { \
      fprintf (stderr, "TID(%2d):%10s(%4d): " a, \
	       THREAD_ID (), __FILE__, __LINE__, (b), (c), (d), (e)); \
    } \
  while (0)

#define CSS_TRACE6(a,b,c,d,e,f) \
  do \
    { \
      fprintf (stderr, "TID(%2d):%10s(%4d): " a, \
	       THREAD_ID (), __FILE__, __LINE__, (b), (c), (d), (e), (f)); \
    } \
  while (0)
#else /* PRINTING */
#define CSS_TRACE1(a)
#define CSS_TRACE2(a,b)
#define CSS_TRACE3(a,b,c)
#define CSS_TRACE4(a,b,c,d)
#define CSS_TRACE5(a,b,c,d,e)
#define CSS_TRACE6(a,b,c,d,e,f)
#endif /* PRINTING */
#endif /* SERVER_MODE */

#endif /* _CONNECTION_ERROR_H_ */
