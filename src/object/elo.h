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
 * elo.h - External interface for ELO objects
 *
 */

#ifndef _ELO_H_
#define _ELO_H_

#ident "$Id$"

#include "dbtype_def.h"
#include "porting.h"
#include "system.h"

#include <stdio.h>

#ifdef __cplusplus
extern "C"
{
#endif

  extern void elo_init_structure (DB_ELO * elo);

#ifdef __cplusplus
}
#endif

extern int elo_create (DB_ELO * elo);

extern int elo_copy_structure (const DB_ELO * elo, DB_ELO * dest);
extern void elo_free_structure (DB_ELO * elo);

extern int elo_copy (DB_ELO * elo, DB_ELO * dest);
extern int elo_delete (DB_ELO * elo, bool force_delete);

extern off_t elo_size (DB_ELO * elo);
extern ssize_t elo_read (const DB_ELO * elo, off_t pos, void *buf, size_t count);
extern ssize_t elo_write (DB_ELO * elo, off_t pos, const void *buf, size_t count);

#endif /* _ELO_H_ */
