/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
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
