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
