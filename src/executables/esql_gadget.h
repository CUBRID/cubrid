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
 * esql_gadget.h - Definitions for gadget interface
 */

#ifndef _ESQL_GADGET_H_
#define _ESQL_GADGET_H_

#ident "$Id$"

#include "config.h"

#include "dbtype.h"

typedef struct db_gadget DB_GADGET;
typedef struct attr_val ATTR_VAL;
struct attr_val
{
  DB_ATTDESC *attr_desc;
  DB_VALUE *value;
};

struct db_gadget
{
  DB_OBJECT *class_;
  int num_attrs;
  ATTR_VAL *attrs;
};

extern DB_GADGET *db_gadget_create (const char *class_name,
				    const char *attribute_names[]);
extern void db_gadget_destroy (DB_GADGET * gadget);
extern int db_gadget_bind (DB_GADGET * gadget,
			   const char *attribute_name, DB_VALUE * dbval);
extern DB_OBJECT *db_gadget_exec (DB_GADGET * gadget, int num_dbvals,
				  DB_VALUE dbvals[]);

#endif /* _ESQL_GADGET_H_ */
