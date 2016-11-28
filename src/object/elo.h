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
 * elo.h - External interface for ELO objects
 *
 */

#ifndef _ELO_H_
#define _ELO_H_

#ident "$Id$"

#include <stdio.h>
#include "object_representation.h"

extern int elo_create (DB_ELO * elo);

extern void elo_init_structure (DB_ELO * elo);
extern int elo_copy_structure (const DB_ELO * elo, DB_ELO * dest);
extern void elo_free_structure (DB_ELO * elo);

extern int elo_copy (DB_ELO * elo, DB_ELO * dest);
extern int elo_delete (DB_ELO * elo, bool force_delete);

extern off_t elo_size (DB_ELO * elo);
extern ssize_t elo_read (const DB_ELO * elo, off_t pos, void *buf, size_t count);
extern ssize_t elo_write (DB_ELO * elo, off_t pos, const void *buf, size_t count);

#if defined(ENABLE_UNUSED_FUNCTION)
extern int elo_get_meta (const DB_ELO * elo, const char *key, char *buf, int bufsz);
extern int elo_set_meta (DB_ELO * elo, const char *key, const char *val);
#endif

#endif /* _ELO_H_ */
