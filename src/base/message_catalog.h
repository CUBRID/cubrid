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

#ifdef __cplusplus
extern "C"
{
#endif
/* functions for use of system message catalog */
  extern int msgcat_init (void);
  extern int msgcat_final (void);
  extern char *msgcat_message (int, int, int);

/* message catalog description  */
  typedef struct msg_catd
  {
    const char *file;		/* file name of this message catalog */
    void *catd;			/* cub_nl_catd from POSIX cub_catopen() */
  } *MSG_CATD;

/* base functions for message catalog support */
  extern MSG_CATD msgcat_open (const char *);
  extern MSG_CATD msgcat_get_descriptor (int cat_id);
  extern char *msgcat_gets (MSG_CATD, int, int, const char *);
  extern int msgcat_close (MSG_CATD);

/* a utility function */
  extern FILE *msgcat_open_file (const char *name);

#ifdef __cplusplus
}
#endif

#endif				/* _MESSAGE_CATALOG_H_ */
