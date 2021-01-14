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
 * stack_dump.h - call stack dump
 */

#ifndef _STACK_DUMP_H_
#define _STACK_DUMP_H_

#ident "$Id$"

#if defined(LINUX)
#include "memory_hash.h"
extern MHT_TABLE *fname_table;
#endif
#include <string>

extern void er_dump_call_stack (FILE * outfp);
extern char *er_dump_call_stack_to_string (void);

#endif /* _STACK_DUMP_H_ */
