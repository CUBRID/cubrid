/*
 *
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
 * authenticate_grant.hpp -
 */

#include "dbtype_def.h" /* MOP */

/*
 * AU_GRANT
 *
 * This is an internal structure used to calculate the recursive
 * effects of a revoke operation.
 */
typedef struct au_grant AU_GRANT;
struct au_grant
{
  struct au_grant *next;

  MOP auth_object;
  MOP user;
  MOP grantor;

  DB_SET *grants;
  int grant_index;

  int grant_option;
  int legal;
};

/*
 * Grant set structure
 *
 * Note :
 *    Grant information is stored packed in a sequence.  These
 *    macros define the length of the "elements" of the sequence and the
 *    offsets to particular fields in each element.  Previously, grants
 *    were stored in their own object but that lead to serious performance
 *    problems as we tried to load each grant object from the server.
 *    This way, grants are stored in the set directly with the authorization
 *    object so only one fetch is required.
 *
 */

#define GRANT_ENTRY_LENGTH 		3
#define GRANT_ENTRY_CLASS(index) 	(index)
#define GRANT_ENTRY_SOURCE(index) 	((index) + 1)
#define GRANT_ENTRY_CACHE(index) 	((index) + 2)

extern int apply_grants (MOP auth, MOP class_mop, unsigned int *bits);
extern int get_grants (MOP auth, DB_SET **grant_ptr, int filter);

extern int appropriate_error (unsigned int bits, unsigned int requested);
