/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution. 
 *
 *   This program is free software; you can redistribute it and/or modify 
 *   it under the terms of the GNU General Public License as published by 
 *   the Free Software Foundation; version 2 of the License. 
 *
 *  This program is distributed in the hope that it will be useful, 
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of 
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the 
 *  GNU General Public License for more details. 
 *
 *  You should have received a copy of the GNU General Public License 
 *  along with this program; if not, write to the Free Software 
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA 
 *
 */


/*
 * elo_holder.h - Attribute names for the Elo hierarchy
 */

#ifndef _ELO_HOLDER_H_
#define _ELO_HOLDER_H_

#ident "$Id$"

#include "db.h"

/* Class names */
#define GLO_HOLDER_CLASS_NAME      "glo_holder"
#define GLO_NAME_CLASS_NAME        "glo_name"

/* Glo name holder attribute names */
#define GLO_NAME_PATHNAME          "pathname"
#define GLO_NAME_HOLDER_PTR        "holder_ptr"

/* Glo holder attribute names */
#define GLO_HOLDER_NAME_PTR        "name"
#define GLO_HOLDER_LOCK_NAME       "lock"
#define GLO_HOLDER_GLO_NAME        "glo"

/*
 * Method names
 */
#define GLO_HOLDER_CREATE_METHOD   "new"
#define GLO_HOLDER_LOCK_METHOD     "lock_method"

extern void Glo_create_holder (DB_OBJECT * this_p,
			       DB_VALUE * return_argument_p,
			       const DB_VALUE * path_name);
extern void Glo_lock_holder (DB_OBJECT * this_p,
			     DB_VALUE * return_argument_p);

#endif /* _ELO_HOLDER_H_ */
