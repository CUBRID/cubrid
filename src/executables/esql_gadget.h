/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 * 
 * dbgadget.h - Definitions for gadget interface
 */

#ifndef _DBGADGET_H_
#define _DBGADGET_H_

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

#endif /* _DBGADGET_H_ */
