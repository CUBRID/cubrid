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
 * load_object_table.h: Object table definitions
 */

#ifndef _LOAD_OBJECT_TABLE_H_
#define _LOAD_OBJECT_TABLE_H_

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

#endif /* _LOAD_OBJECT_TABLE_H_ */
