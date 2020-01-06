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
