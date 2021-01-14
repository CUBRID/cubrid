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
 * esql_gadget.h - Definitions for gadget interface
 */

#ifndef _ESQL_GADGET_H_
#define _ESQL_GADGET_H_

#ident "$Id$"

#include "config.h"

#include "dbtype_def.h"

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

extern DB_GADGET *db_gadget_create (const char *class_name, const char *attribute_names[]);
extern void db_gadget_destroy (DB_GADGET * gadget);
extern int db_gadget_bind (DB_GADGET * gadget, const char *attribute_name, DB_VALUE * dbval);
extern DB_OBJECT *db_gadget_exec (DB_GADGET * gadget, int num_dbvals, DB_VALUE dbvals[]);

#endif /* _ESQL_GADGET_H_ */
