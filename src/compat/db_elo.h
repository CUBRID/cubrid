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
 * db_elo.h -
 */

#ifndef _DB_ELO_H_
#define _DB_ELO_H_

#ident "$Id$"

#include <sys/types.h>

#include "dbtype_def.h"

extern int db_create_fbo (DB_VALUE * value, DB_TYPE type);
/* */
extern int db_elo_copy_structure (const DB_ELO * src, DB_ELO * dest);
extern void db_elo_free_structure (DB_ELO * elo);

extern int db_elo_copy (DB_ELO * src, DB_ELO * dest);
extern int db_elo_delete (DB_ELO * elo);

extern DB_BIGINT db_elo_size (DB_ELO * elo);
extern int db_elo_read (const DB_ELO * elo, off_t pos, void *buf, size_t count, DB_BIGINT * read_bytes);
extern int db_elo_write (DB_ELO * elo, off_t pos, const void *buf, size_t count, DB_BIGINT * written_bytes);

#endif /* _DB_ELO_H_ */
