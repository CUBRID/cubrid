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

#include <stdio.h>

#define API_ACTIVE_CHECKS

#include "db.h"			// must be before dbtype_function.h for bool definition
#include "dbtype_function.h"

#include "db_set.h"
#include "error_manager.h"
#include "elo.h"
#include "intl_support.h"
#include "language_support.h"
#include "memory_alloc.h"
#include "oid.h"
#include "set_object.h"
#include "system_parameter.h"

// hidden functions (suppress -Wmissing-prototypes and -Wimplicit-function-declaration)
int db_make_db_char (DB_VALUE * value, const INTL_CODESET codeset, const int collation_id, const char *str,
		     const int size);
DB_TYPE setobj_type (struct setobj *set);

#include "dbtype_function.i"
