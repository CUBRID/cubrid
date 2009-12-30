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
 * message_catalog.h - Message catalog functions with NLS support
 */

#ifndef _MESSAGE_CATALOG_H_
#define _MESSAGE_CATALOG_H_

#ident "$Id$"

#include <stdio.h>

/*
 * System message catalog id; used in msgcat_message()
 */
#define MSGCAT_CATALOG_CUBRID           0
#define MSGCAT_CATALOG_CSQL             1
#define MSGCAT_CATALOG_UTILS            2

/*
 * Message set id in the message catalog MSGCAT_CATALOG_CUBRID (cubrid.msg).
 * These define the $set numbers within the catalog file.
 */
#define MSGCAT_SET_GENERAL              1
#define MSGCAT_SET_PARAMETERS           4
#define MSGCAT_SET_ERROR                5
#define MSGCAT_SET_INTERNAL             6
#define MSGCAT_SET_PARSER_SYNTAX        7
#define MSGCAT_SET_PARSER_SEMANTIC      8
#define MSGCAT_SET_PARSER_RUNTIME       9
#define MSGCAT_SET_AUTHORIZATION        11
#define MSGCAT_SET_HELP                 12
#define MSGCAT_SET_TRIGGER              13
#define MSGCAT_SET_LOCK                 14
#define MSGCAT_SET_IO                   15
#define MSGCAT_SET_LOG                  16

/* Message id in the set MSGCAT_SET_GENERAL */
#define MSGCAT_GENERAL_DATABASE_INIT    1
#define MSGCAT_GENERAL_COPYRIGHT_HEADER 2
#define MSGCAT_GENERAL_COPYRIGHT_BODY   3
#define MSGCAT_GENERAL_ARG_NONINT       5
#define MSGCAT_GENERAL_ARG_NONFLOAT     6
#define MSGCAT_GENERAL_ARG_MISSING      7
#define MSGCAT_GENERAL_ARG_DUPLICATE    8
#define MSGCAT_GENERAL_ARG_MISSINGVAL   9
#define MSGCAT_GENERAL_ARG_UNKNOWN      10
#define MSGCAT_GENERAL_ARG_UNEXPECTED   11
#define MSGCAT_GENERAL_ARG_NODEF        12
#define MSGCAT_GENERAL_ARG_NOMEM        13
#define MSGCAT_GENERAL_ARG_TOO_LONG     14
#define MSGCAT_GENERAL_ARG_INVALIDNUM   15
#define MSGCAT_GENERAL_ARG_OPTIONS      16
#define MSGCAT_GENERAL_ARG_OPTIONS2     17

/* Message id in the set MSGCAT_SET_TRIGGER */
#define MSGCAT_TRIGGER_TRACE_CONDITION  1
#define MSGCAT_TRIGGER_TRACE_ACTION     2

/* Message id in the set MSGCAT_SET_LOCK are defined in the other file. */

/* Message id in the set MSGCAT_SET_IO are defined in the other file. */

/* Message id in the set MSGCAT_SET_LOG are defined in the other file. */


/* functions for use of system message catalog */
extern int msgcat_init (void);
extern int msgcat_final (void);
extern char *msgcat_message (int, int, int);

/* message catalog description  */
typedef struct msg_catd
{
  const char *file;		/* file name of this message catalog */
  void *catd;			/* nl_catd from POSIX catopen() */
} *MSG_CATD;

/* base functions for message catalog support */
extern MSG_CATD msgcat_open (const char *);
extern MSG_CATD msgcat_get_descriptor (int cat_id);
extern char *msgcat_gets (MSG_CATD, int, int, const char *);
extern int msgcat_close (MSG_CATD);

/* a utility function */
extern FILE *msgcat_open_file (const char *name);

#endif /* _MESAGE_CATALOG_H_ */
