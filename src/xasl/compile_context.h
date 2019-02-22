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

//
// compile_context.h - client/server common context used for prepare phase
//

#ifndef _COMPILE_CONTEXT_H_
#define _COMPILE_CONTEXT_H_

// forward definitions
struct xasl_node;

// note - file should be compatible to C language

#include "sha1.h"

/*
 * COMPILE_CONTEXT cover from user input query string to generated xasl
 */
typedef struct compile_context COMPILE_CONTEXT;
struct compile_context
{
  struct xasl_node *xasl;

  char *sql_user_text;		/* original query statement that user input */
  int sql_user_text_len;	/* length of sql_user_text */

  char *sql_hash_text;		/* rewritten query string which is used as hash key */

  char *sql_plan_text;		/* plans for this query */
  int sql_plan_alloc_size;	/* query_plan alloc size */
  bool is_xasl_pinned_reference;	/* to pin xasl cache entry */
  bool recompile_xasl_pinned;	/* whether recompile again after xasl cache entry has been pinned */
  bool recompile_xasl;
  SHA1Hash sha1;
};
#endif // _COMPILE_CONTEXT_H_
