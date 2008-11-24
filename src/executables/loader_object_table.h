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
 *      loader_object_table.h: Object table definitions
 */

#ident "$Id$"

#ifndef _LOADER_OBJECT_TABLE_H_
#define _LOADER_OBJECT_TABLE_H_

#include "oid.h"
#include "work_space.h"

/*
 * INST_FLAG_
 *    These define the allowed values for the flags field of
 *    the INST_INFO structure.
 */
#define INST_FLAG_RESERVED 1
#define INST_FLAG_INSERTED 2
#define INST_FLAG_CLASS_ATT 4

/*
 * INST_INFO
 *    Structure maintained inside a CLASS_TABLE.
 *    The instance table is maintained as an array indexed by instance id.
 *    Each class will have an instance array that grows as necessary.
 *    This really should be replaced with the fh_ file hashing module.
 */
typedef struct instance_info INST_INFO;
struct instance_info
{
  OID oid;
  int flags;
};

/*
 * CLASS_TABLE
 *    This holds information about the classes being populated by the loader.
 *    Each class in the loader input file will have an entry on this list.
 */
typedef struct class_table CLASS_TABLE;
struct class_table
{
  struct class_table *next;

  MOP class_;
  INST_INFO *instances;
  int count;
  int presize;

  int total_inserts;
};

/* mapping */
typedef int (*OTABLE_MAPFUNC) (CLASS_TABLE * class_, OID * oid);

/* The global class table */
extern CLASS_TABLE *Classes;

/* class table maintenance */
extern int otable_presize (MOP class_, int size);
extern void otable_set_presize (CLASS_TABLE * table, int size);
extern int otable_insert (CLASS_TABLE * table, OID * instance, int id);
extern int otable_reserve (CLASS_TABLE * table, OID * instance, int id);
extern int otable_update (CLASS_TABLE * table, int id);
extern void otable_class_att_ref (INST_INFO * inst);
extern CLASS_TABLE *otable_find_class (MOP class_);
extern INST_INFO *otable_find (CLASS_TABLE * table, int id);

/* module control */
extern int otable_prepare (void);
extern int otable_init (void);
extern void otable_final (void);

extern int otable_map_reserved (OTABLE_MAPFUNC mapfunc, int stop_on_error);

#endif /* _LOADER_OBJECT_TABLE_H_ */
