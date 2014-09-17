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
 * object_primitive.h: Definitions related to primitive types.
 */

#ifndef _OBJECT_PRIMITIVE_H_
#define _OBJECT_PRIMITIVE_H_

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include "error_manager.h"
#include "object_representation.h"
#include "object_domain.h"
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
  void (*initmem) (void *memptr, struct tp_domain * domain);
  /* initialize DB_VALUE */
  void (*initval) (DB_VALUE * value, int precision, int scale);
  int (*setmem) (void *memptr, struct tp_domain * domain, DB_VALUE * value);
  /* zero to avoid copying if possible */
  int (*getmem) (void *memptr, struct tp_domain * domain,
		 DB_VALUE * value, bool copy);
  /* set DB_VALUE from DB_VALUE */
  int (*setval) (DB_VALUE * dest, const DB_VALUE * src, bool copy);
  /* return memory size */
  int (*data_lengthmem) (void *memptr, struct tp_domain * domain, int disk);
  /* return DB_VALUE size */
  int (*data_lengthval) (DB_VALUE * value, int disk);
  /* write disk rep from memory */
  void (*data_writemem) (OR_BUF * buf, void *memptr,
			 struct tp_domain * domain);
  /* read disk rep to memory */
  void (*data_readmem) (OR_BUF * buf, void *memptr, struct tp_domain * domain,
			int size);
  /* write disk rep from DB_VALUE */
  int (*data_writeval) (OR_BUF * buf, DB_VALUE * value);
  /* read disk rep to DB_VALUE */
  int (*data_readval) (OR_BUF * buf, DB_VALUE * value,
		       struct tp_domain * domain, int size, bool copy,
		       char *copy_buf, int copy_buf_len);
  /* btree memory size */
  int (*index_lengthmem) (void *memptr, struct tp_domain * domain);
  /* return DB_VALUE size */
  int (*index_lengthval) (DB_VALUE * value);
  /* write btree rep from DB_VALUE */
  int (*index_writeval) (OR_BUF * buf, DB_VALUE * value);
  /* read btree rep to DB_VALUE */
  int (*index_readval) (OR_BUF * buf, DB_VALUE * value,
			struct tp_domain * domain, int size, bool copy,
			char *copy_buf, int copy_buf_len);
  /* btree value compare */
  int (*index_cmpdisk) (void *memptr1, void *memptr2,
			struct tp_domain * domain,
			int do_coercion, int total_order, int *start_colp);
  /* free memory for swap or GC */
  void (*freemem) (void *memptr);
  /* memory value compare */
  int (*data_cmpdisk) (void *memptr1, void *memptr2,
		       struct tp_domain * domain,
		       int do_coercion, int total_order, int *start_colp);
  /* db value compare */
  int (*cmpval) (DB_VALUE * value, DB_VALUE * value2,
		 int do_coercion, int total_order, int *start_colp,
		 int collation);
} PR_TYPE, *PRIM;


/*
 * PRIMITIVE TYPE STRUCTURES
 */
/* The number of following types */
#define PR_TYPE_TOTAL 32

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
extern PR_TYPE tp_Datetime;
extern PR_TYPE tp_Monetary;
extern PR_TYPE tp_Elo;
extern PR_TYPE tp_Blob;
extern PR_TYPE tp_Clob;
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
extern PR_TYPE tp_Bigint;
extern PR_TYPE tp_Enumeration;
extern PR_TYPE tp_Timestamptz;
extern PR_TYPE tp_Timestampltz;
extern PR_TYPE tp_Datetimetz;
extern PR_TYPE tp_Datetimeltz;
extern PR_TYPE tp_Timetz;
extern PR_TYPE tp_Timeltz;

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
extern PR_TYPE *tp_Type_blob;
extern PR_TYPE *tp_Type_clob;
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
extern PR_TYPE *tp_Type_bigint;
extern PR_TYPE *tp_Type_datetime;

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
#define PRIM_INITMEM(type, mem, domain) (*((type)->initmem))(mem, domain)

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
  (*((type)->data_readmem))(tr, mem, domain, len)

/* PRIM_WRITE
 * Write a value from instance memory into a disk buffer.
 */
#define PRIM_WRITE(type, domain, tr, mem) (*((type)->data_writemem))(tr, mem, domain)

#define PRIM_WRITEVAL(type, buf, val) (*((type)->writeval))(buf, val)
#define PRIM_LENGTHVAL(type, val, size) (*((type)->lengthval))(val, size)

/* PRIM_SET_NULL
 * set a db_value to NULL
 */
#define PRIM_SET_NULL(db_value_ptr) \
    ((db_value_ptr)->domain.general_info.is_null = 1, \
     (db_value_ptr)->need_clear = false)

/*
 * EXTERNAL FUNCTIONS
 */

/* Type structure accessors */
#define PR_TYPE_FROM_ID(type) ((type) <= DB_TYPE_LAST && (type) != DB_TYPE_TABLE \
                               ? tp_Type_id_map[(int)(type)] : NULL)

extern PR_TYPE *pr_type_from_id (DB_TYPE id);
extern PR_TYPE *pr_find_type (const char *name);
extern const char *pr_type_name (DB_TYPE id);

extern int pr_is_set_type (DB_TYPE type);
extern int pr_is_string_type (DB_TYPE type);
extern int pr_is_prefix_key_type (DB_TYPE type);
extern int pr_is_variable_type (DB_TYPE type);

/* Size calculators */

#if defined(ENABLE_UNUSED_FUNCTION)
extern int pr_disk_size (PR_TYPE * type, void *mem);
#endif
extern int pr_mem_size (PR_TYPE * type);
extern int pr_total_mem_size (PR_TYPE * type, void *mem);
extern int pr_value_mem_size (DB_VALUE * value);

/* DB_VALUE constructors */

extern DB_VALUE *pr_make_value (void);
extern DB_VALUE *pr_copy_value (DB_VALUE * var);
extern int pr_clone_value (const DB_VALUE * src, DB_VALUE * dest);
#if defined(ENABLE_UNUSED_FUNCTION)
extern int pr_share_value (DB_VALUE * src, DB_VALUE * dest);
#endif
#define PR_SHARE_VALUE(src, dst) \
  do \
    { \
      if (src && dst && src != dst) \
	{ \
	  *(dst) = *(src); \
	  (dst)->need_clear = false; \
	  if (pr_is_set_type (DB_VALUE_DOMAIN_TYPE(src)) \
	      && !DB_IS_NULL(src)) \
	    { \
	      (src)->data.set->ref_count++; \
	    } \
	} \
    } \
  while (0)

extern int pr_clear_value (DB_VALUE * var);
extern int pr_free_value (DB_VALUE * var);
extern DB_VALUE *pr_make_ext_value (void);
extern int pr_free_ext_value (DB_VALUE * value);

/* Special transformation functions */

extern int pr_midxkey_compare (DB_MIDXKEY * mul1, DB_MIDXKEY * mul2,
			       int do_coercion, int total_order,
			       int num_index_term,
			       int *start_colp,
			       int *result_size1, int *result_size2,
			       int *diff_column, bool * dom_is_desc,
			       bool * next_dom_is_desc);
extern int pr_midxkey_element_disk_size (char *mem, DB_DOMAIN * domain);
extern int pr_midxkey_get_element_nocopy (const DB_MIDXKEY * midxkey,
					  int index, DB_VALUE * value,
					  int *prev_indexp, char **prev_ptrp);
extern int pr_midxkey_add_elements (DB_VALUE * keyval, DB_VALUE * dbvals,
				    int num_dbvals,
				    TP_DOMAIN * dbvals_domain_list);
extern int pr_midxkey_init_boundbits (char *bufptr, int n_atts);
extern int pr_index_writeval_disk_size (DB_VALUE * value);
extern int pr_data_writeval_disk_size (DB_VALUE * value);
extern void pr_data_writeval (OR_BUF * buf, DB_VALUE * value);
extern int pr_midxkey_unique_prefix (const DB_VALUE * db_midxkey1,
				     const DB_VALUE * db_midxkey2,
				     DB_VALUE * db_result);
extern int pr_midxkey_get_element_offset (const DB_MIDXKEY * midxkey,
					  int index);
extern int pr_midxkey_add_prefix (DB_VALUE * result, DB_VALUE * prefix,
				  DB_VALUE * postfix, int n_prefix);
extern int pr_midxkey_remove_prefix (DB_VALUE * key, int prefix);
extern int pr_midxkey_common_prefix (DB_VALUE * key1, DB_VALUE * key2);

extern int pr_Inhibit_oid_promotion;

#if !defined (SERVER_MODE)
extern void pr_write_mop (OR_BUF * buf, MOP mop);
#endif
#if defined (ENABLE_UNUSED_FUNCTION)
/* Utility functions */
extern char *pr_copy_string (const char *str);
extern void pr_free_string (char *str);
#endif

/* Helper function for DB_VALUE printing; caller must free_and_init result. */
extern char *pr_valstring (DB_VALUE *);

/* Garbage collection support */

#if defined(ENABLE_UNUSED_FUNCTION)
extern void pr_gc_set (SETOBJ * set, void (*gcmarker) (MOP));
extern void pr_gc_setref (SETREF * set, void (*gcmarker) (MOP));
extern void pr_gc_value (DB_VALUE * value, void (*gcmarker) (MOP));
extern void pr_gc_type (PR_TYPE * type, char *mem, void (*gcmarker) (MOP));
#endif

/* area init */
extern void pr_area_init (void);

extern int pr_complete_enum_value (DB_VALUE * value, TP_DOMAIN * domain);

#endif /* _OBJECT_PRIMITIVE_H_ */
