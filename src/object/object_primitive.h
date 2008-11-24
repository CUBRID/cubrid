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
 * object_primitive.h: Definitions related to primitive types.
 */

#ifndef _OBJECT_PRIMITIVE_H_
#define _OBJECT_PRIMITIVE_H_

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include "error_manager.h"
#include "object_representation.h"
#if !defined (SERVER_MODE)
#include "work_space.h"
#endif

/*
 * PR_TYPE
 *    This structure defines the information and operations available
 *    for primitive types.
 */
typedef struct pr_type
{
  /* internal identifier name */
  const char *name;
  DB_TYPE id;
  int variable_p;
  int size;
  int disksize;
  int alignment;
  /* print dbvalue to file */
  void (*fptrfunc) (FILE * fp, const DB_VALUE * value);
  /* print dbvalue to buffer */
  int (*sptrfunc) (const DB_VALUE * value, char *buffer, int buflen);
  /* initialize memory */
  void (*initmem) (void *memptr);
  /* initialize DB_VALUE */
  void (*initval) (DB_VALUE * value, int precision, int scale);
  int (*setmem) (void *memptr, struct tp_domain * domain, DB_VALUE * value);
  /* zero to avoid copying if possible */
  int (*getmem) (void *memptr, struct tp_domain * domain,
		 DB_VALUE * value, bool copy);
  /* set DB_VALUE from DB_VALUE */
  int (*setval) (DB_VALUE * dest, DB_VALUE * src, bool copy);
  /* return memory size */
  int (*lengthmem) (void *memptr, struct tp_domain * domain, int disk);
  /* return DB_VALUE size */
  int (*lengthval) (DB_VALUE * value, int disk);
  /* write disk rep from memory */
  void (*writemem) (OR_BUF * buf, void *memptr, struct tp_domain * domain);
  /* read disk rep to memory */
  void (*readmem) (OR_BUF * buf, void *memptr, struct tp_domain * domain,
		   int size);
  /* write disk rep from DB_VALUE */
  int (*writeval) (OR_BUF * buf, DB_VALUE * value);
  /* read disk rep to DB_VALUE */
  int (*readval) (OR_BUF * buf, DB_VALUE * value, struct tp_domain * domain,
		  int size, bool copy, char *copy_buf, int copy_buf_len);
  /* free memory for swap or GC */
  void (*freemem) (void *memptr);
  /* memory value compare */
  int (*cmpdisk) (void *memptr1, void *memptr2, struct tp_domain * domain,
		  int do_reverse, int do_coercion,
		  int total_order, int *start_colp);
  /* db value compare */
  int (*cmpval) (DB_VALUE * value, DB_VALUE * value2,
		 struct tp_domain * domain, int do_reverse,
		 int do_coercion, int total_order, int *start_colp);
} PR_TYPE, *PRIM;


/*
 * PRIMITIVE TYPE STRUCTURES
 */
/* The number of following types */
#define PR_TYPE_TOTAL 30

extern PR_TYPE tp_Null;
extern PR_TYPE tp_Integer;
extern PR_TYPE tp_Short;
extern PR_TYPE tp_Float;
extern PR_TYPE tp_Double;
extern PR_TYPE tp_String;
extern PR_TYPE tp_Set;
extern PR_TYPE tp_Multiset;
extern PR_TYPE tp_Sequence;
extern PR_TYPE tp_Object;
extern PR_TYPE tp_Midxkey;
extern PR_TYPE tp_Time;
extern PR_TYPE tp_Utime;
extern PR_TYPE tp_Date;
extern PR_TYPE tp_Monetary;
extern PR_TYPE tp_Elo;
extern PR_TYPE tp_Variable;
extern PR_TYPE tp_Substructure;
extern PR_TYPE tp_Pointer;
extern PR_TYPE tp_Error;
extern PR_TYPE tp_Vobj;
extern PR_TYPE tp_Oid;
extern PR_TYPE tp_Numeric;
extern PR_TYPE tp_Bit;
extern PR_TYPE tp_VarBit;
extern PR_TYPE tp_Char;
extern PR_TYPE tp_NChar;
extern PR_TYPE tp_VarNChar;
extern PR_TYPE tp_ResultSet;

extern PR_TYPE *tp_Type_null;
extern PR_TYPE *tp_Type_integer;
extern PR_TYPE *tp_Type_short;
extern PR_TYPE *tp_Type_float;
extern PR_TYPE *tp_Type_double;
extern PR_TYPE *tp_Type_string;
extern PR_TYPE *tp_Type_error;
extern PR_TYPE *tp_Type_pointer;
extern PR_TYPE *tp_Type_oid;
extern PR_TYPE *tp_Type_set;
extern PR_TYPE *tp_Type_multiset;
extern PR_TYPE *tp_Type_sequence;
extern PR_TYPE *tp_Type_object;
extern PR_TYPE *tp_Type_time;
extern PR_TYPE *tp_Type_utime;
extern PR_TYPE *tp_Type_date;
extern PR_TYPE *tp_Type_monetary;
extern PR_TYPE *tp_Type_elo;
extern PR_TYPE *tp_Type_variable;
extern PR_TYPE *tp_Type_substructure;
extern PR_TYPE *tp_Type_vobj;
extern PR_TYPE *tp_Type_numeric;
extern PR_TYPE *tp_Type_bit;
extern PR_TYPE *tp_Type_varbit;
extern PR_TYPE *tp_Type_char;
extern PR_TYPE *tp_Type_nchar;
extern PR_TYPE *tp_Type_varnchar;
extern PR_TYPE *tp_Type_resultset;
extern PR_TYPE *tp_Type_midxkey;

extern PR_TYPE *tp_Type_id_map[];

/* The sizes of the primitive types in descending order */
extern int pr_ordered_mem_sizes[PR_TYPE_TOTAL];

/* The number of items in pr_ordered_mem_sizes */
extern int pr_ordered_mem_size_total;


/* PRIMITIVE TYPE MACROS
 *
 * These provide a shell around the calling of primitive type operators
 * so they look nicer.  We used to have "VAL" oriented functions here too
 * but with the new DB_VALUE system, these aren't really necessary any more.
 * It probably wouldn't hurt to phase these out either.
 */

/* PRIM_INITMEM
 *
 * Initialize an attribute value in instance memory
 * For fixed witdh attributes, it tries to zero out the memory.  This isn't a
 * requirement but it makes the object block look cleaner in the debugger.
 * For variable width attributes, it makes sure the pointers are NULL.
 */
#define PRIM_INITMEM(type, mem) (*((type)->initmem))(mem)

/* PRIM_SETMEM
 * Assign a value into instance memory, copy the value.
 */
#define PRIM_SETMEM(type, domain, mem, value) (*((type)->setmem))(mem, domain, value)

/* PRIM_GETMEM
 * Get a value from instance memory, copy the value.
 */
#define PRIM_GETMEM(type, domain, mem, value) \
  (*((type)->getmem))(mem, domain, value, true)

/* PRIM_GETMEM_NOCOPY
 * The "nocopy" option for "getmem" is intended only for internal processes
 * that need to access the values but don't want the overhead of copying,
 * particularly for strings.  The only user of this right now is the UNIQUE
 * register logic.
 */
#define PRIM_GETMEM_NOCOPY(type, domain, mem, value) \
  (*((type)->getmem))(mem, domain, value, false)

/* PRIM_FREEMEM
 * Free any external storage in instance memory that may have been allocated on
 * behalf of a type.
 */
#define PRIM_FREEMEM(type, mem) \
  if (type->freemem != NULL) (*((type)->freemem))(mem)

/* PRIM_READ
 * Read a value from a disk buffer into instance memory.
 */
#define PRIM_READ(type, domain, tr, mem, len) \
  (*((type)->readmem))(tr, mem, domain, len)

/* PRIM_WRITE
 * Write a vlaue from instance memory into a disk buffer.
 */
#define PRIM_WRITE(type, domain, tr, mem) (*((type)->writemem))(tr, mem, domain)

#define PRIM_WRITEVAL(type, buf, val) (*((type)->writeval))(buf, val)
#define PRIM_LENGTHVAL(type, val, size) (*((type)->lengthval))(val, size)

/* PRIM_SET_NULL
 * set a db_value to NULL
 */
#define PRIM_SET_NULL(db_value_ptr) \
    ((db_value_ptr)->domain.general_info.is_null = 1, \
     (db_value_ptr)->need_clear = false)

/* PRIM_IS_NULL
 * test if a db_value is NULL
 */
#define PRIM_IS_NULL(db_value_ptr) ((db_value_ptr)->domain.general_info.is_null != 0)

/* PRIM_TYPE
 * get a db_value's DB_TYPE
 */
#define PRIM_TYPE(db_value_ptr) ((DB_TYPE) (db_value_ptr)->domain.general_info.type)

/* PRIM_INIT_NULL
 * Initialize necessary fields of a DB_VALUE
 */
#define PRIM_INIT_NULL(db_value_ptr) \
  do { \
    PRIM_SET_NULL(db_value_ptr); \
    (db_value_ptr)->domain.general_info.type = DB_TYPE_NULL; \
  } while (0)

/*
 * EXTERNAL FUNCTIONS
 */

/* Type structure accessors */
#define PR_TYPE_FROM_ID(type) \
((type) <= DB_TYPE_MIDXKEY ? tp_Type_id_map[(int)(type)] : NULL)

extern PR_TYPE *pr_type_from_id (DB_TYPE id);
extern PR_TYPE *pr_find_type (const char *name);
extern const char *pr_type_name (DB_TYPE id);

extern int pr_is_set_type (DB_TYPE type);
extern int pr_is_string_type (DB_TYPE type);
extern int pr_is_variable_type (DB_TYPE type);

/* Size calculators */

extern int pr_disk_size (PR_TYPE * type, void *mem);
extern int pr_mem_size (PR_TYPE * type);
extern int pr_total_mem_size (PR_TYPE * type, void *mem);
extern int pr_value_mem_size (DB_VALUE * value);

/* DB_VALUE constructors */

extern DB_VALUE *pr_make_value (void);
extern DB_VALUE *pr_copy_value (DB_VALUE * var);
extern int pr_clone_value (DB_VALUE * src, DB_VALUE * dest);
extern int pr_share_value (DB_VALUE * src, DB_VALUE * dest);
#define PR_SHARE_VALUE(src, dst)                                            \
    {                                                                       \
	if (src && dst && src != dst) {					    \
            *(dst) = *(src);                                                \
            (dst)->need_clear = false;                                      \
            if (pr_is_set_type((DB_TYPE) ((src)->domain.general_info.type)) &&          \
                (src)->domain.general_info.is_null == 0)                    \
                (src)->data.set->ref_count++;                               \
        }								    \
    }
extern int pr_clone_value_nocopy (DB_VALUE * src, DB_VALUE * dest);
extern int pr_clear_value (DB_VALUE * var);
extern int pr_free_value (DB_VALUE * var);
extern DB_VALUE *pr_make_ext_value (void);
extern int pr_free_ext_value (DB_VALUE * value);

/* Special transformation functions */

extern int pr_writemem_disk_size (char *mem, DB_DOMAIN * domain);
extern int pr_writeval_disk_size (DB_VALUE * value);
extern void pr_writeval (OR_BUF * buf, DB_VALUE * value);
extern int pr_estimate_size (DB_DOMAIN * domain, int avg_key_len);

extern int pr_Inhibit_oid_promotion;

#if !defined (SERVER_MODE)
extern void pr_write_mop (OR_BUF * buf, MOP mop);
#endif

/* Helper function for DB_VALUE printing; caller must free_and_init result. */
extern char *pr_valstring (DB_VALUE *);

/* Garbage collection support */

extern void pr_gc_set (SETOBJ * set, void (*gcmarker) (MOP));
extern void pr_gc_setref (SETREF * set, void (*gcmarker) (MOP));
extern void pr_gc_value (DB_VALUE * value, void (*gcmarker) (MOP));
extern void pr_gc_type (PR_TYPE * type, char *mem, void (*gcmarker) (MOP));

/* area init */
extern void pr_area_init (void);

#endif /* _OBJECT_PRIMITIVE_H_ */
