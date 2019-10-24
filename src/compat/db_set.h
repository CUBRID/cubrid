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
// db_set structure
//

#ifndef _DB_SET_H_
#define _DB_SET_H_

#include "dbtype_def.h"

#include <stdio.h>

#ifdef __cplusplus
extern "C"
{
#endif				// __cplusplus

  struct db_set
  {
    /*
     * a garbage collector ticket is not required for the "owner" field as
     * the entire set references area is registered for scanning in area_grow.
     */
    struct db_object *owner;
    struct db_set *ref_link;
    struct setobj *set;
    char *disk_set;
    DB_DOMAIN *disk_domain;
    int attribute;
    int ref_count;
    int disk_size;
    need_clear_type need_clear;
  };

#ifdef __cplusplus
}				// extern "C"
#endif				// __cplusplus

#endif				// !_DB_SET_H_
