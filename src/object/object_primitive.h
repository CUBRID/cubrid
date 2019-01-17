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
 *
 *    Basic operations with primitive types supported by database - numerics, strings, time, sets, json, so on
 *
 *    It includes compare functions for various representations (db_value, in-memory, disk data and index) and
 *    conversions between db_value and other representations.
 */

#ifndef _OBJECT_PRIMITIVE_H_
#define _OBJECT_PRIMITIVE_H_

#ident "$Id$"

#include "dbtype_def.h"

#include <stdio.h>
#include <vector>

// forward definitions
class string_buffer;
struct tp_domain;
struct or_buf;

/*
 * PR_TYPE
 *    This structure defines the information and operations available
 *    for primitive types.
 */
// *INDENT-OFF*
typedef struct pr_type
{
  public:
    /* internal identifier name */
    const char *name;
    DB_TYPE id;
    int variable_p;
    int size;
    int disksize;
    int alignment;


    typedef DB_VALUE_COMPARE_RESULT (*data_cmpdisk_function_type) (void *memptr1, void *memptr2, tp_domain * domain,
                                                                   int do_coercion, int total_order, int *start_colp);
    typedef DB_VALUE_COMPARE_RESULT (*cmpval_function_type) (DB_VALUE * value, DB_VALUE * value2, int do_coercion,
                                                             int total_order, int *start_colp, int collation);

  public:
    /* initialize memory */
    void (*f_initmem) (void *memptr, struct tp_domain * domain);
    /* initialize DB_VALUE */
    void (*f_initval) (DB_VALUE * value, int precision, int scale);
    int (*f_setmem) (void *memptr, struct tp_domain * domain, DB_VALUE * value);
    /* zero to avoid copying if possible */
    int (*f_getmem) (void *memptr, struct tp_domain * domain, DB_VALUE * value, bool copy);
    /* set DB_VALUE from DB_VALUE */
    int (*f_setval) (DB_VALUE * dest, const DB_VALUE * src, bool copy);
    /* return memory size */
    int (*f_data_lengthmem) (void *memptr, struct tp_domain * domain, int disk);
    /* return DB_VALUE size */
    int (*f_data_lengthval) (DB_VALUE * value, int disk);
    /* write disk rep from memory */
    void (*f_data_writemem) (struct or_buf * buf, void *memptr, struct tp_domain * domain);
    /* read disk rep to memory */
    void (*f_data_readmem) (struct or_buf * buf, void *memptr, struct tp_domain * domain, int size);
    /* write disk rep from DB_VALUE */
    int (*f_data_writeval) (struct or_buf * buf, DB_VALUE * value);
    /* read disk rep to DB_VALUE */
    int (*f_data_readval) (struct or_buf * buf, DB_VALUE * value, struct tp_domain * domain, int size, bool copy,
		         char *copy_buf, int copy_buf_len);
    /* btree memory size */
    int (*f_index_lengthmem) (void *memptr, struct tp_domain * domain);
    /* return DB_VALUE size */
    int (*f_index_lengthval) (DB_VALUE * value);
    /* write btree rep from DB_VALUE */
    int (*f_index_writeval) (struct or_buf * buf, DB_VALUE * value);
    /* read btree rep to DB_VALUE */
    int (*f_index_readval) (struct or_buf * buf, DB_VALUE * value, struct tp_domain * domain, int size, bool copy,
			    char *copy_buf, int copy_buf_len);
    /* btree value compare */
    DB_VALUE_COMPARE_RESULT (*f_index_cmpdisk) (void *memptr1, void *memptr2, struct tp_domain * domain,
                                                int do_coercion, int total_order, int *start_colp);
    /* free memory for swap or GC */
    void (*f_freemem) (void *memptr);
    /* memory value compare */
    data_cmpdisk_function_type f_data_cmpdisk;
    cmpval_function_type f_cmpval;

  public:

    // todo - remove all the const stripping code from function implementation; signatures for all internal functions
    //        (prefixed by f_) and their various implementations inside object_primitive.c should be updated.
    //        postponed for a better days and better design

    // setters/getters
    inline void set_data_cmpdisk_function (data_cmpdisk_function_type data_cmpdisk_arg);
    inline data_cmpdisk_function_type get_data_cmpdisk_function () const;

    void set_cmpval_function (cmpval_function_type cmpval_arg);
    cmpval_function_type get_cmpval_function () const;

    // checking fixed size - actually all functions should return same value, but this is a legacy issue
    // todo - replace with a single fixed size check function
    inline bool is_data_lengthmem_fixed () const;
    inline bool is_data_lengthval_fixed () const;

    // operations for in-memory representations
    inline void initmem (void *memptr, const tp_domain * domain) const;
    inline int setmem (void *memptr, const tp_domain * domain, const DB_VALUE * value) const;
    inline int getmem (void *memptr, const tp_domain * domain, DB_VALUE * value, bool copy) const;
    inline int data_lengthmem (const void *memptr, const tp_domain * domain, int disk) const;
    inline void freemem (void *memptr) const;

    // operations for db_value's
    inline void initval (DB_VALUE * value, int precision, int scale) const;
    inline int setval (DB_VALUE * dest, const DB_VALUE * src, bool copy) const;
    inline int data_lengthval (const DB_VALUE * value, int disk) const;
    inline DB_VALUE_COMPARE_RESULT cmpval (const DB_VALUE * value, const DB_VALUE * value2, int do_coercion,
                                           int total_order, int *start_colp, int collation) const;

    // operations for disk representation (as usually stored in heap files and list files)
    inline void data_writemem (struct or_buf * buf, const void *memptr, const tp_domain * domain) const;
    inline void data_readmem (struct or_buf * buf, void *memptr, const tp_domain * domain, int size) const;
    inline int data_writeval (struct or_buf * buf, const DB_VALUE * value) const;
    inline int data_readval (struct or_buf * buf, DB_VALUE * value, const tp_domain * domain, int size, bool copy,
                             char *copy_buf, int copy_buf_len) const;
    inline DB_VALUE_COMPARE_RESULT data_cmpdisk (const void *memptr1, const void *memptr2, const tp_domain * domain,
                                                 int do_coercion, int total_order, int *start_colp) const;

    // operations for index representations (ordered)
    inline int index_lengthmem (const void *memptr, const tp_domain * domain) const;
    inline bool is_index_lengthval_fixed () const;
    inline int index_lengthval (const DB_VALUE * value) const;
    inline int index_writeval (struct or_buf * buf, const DB_VALUE * value) const;
    inline int index_readval (struct or_buf * buf, DB_VALUE * value, const tp_domain * domain, int size, bool copy,
                              char *copy_buf, int copy_buf_len) const;
    inline DB_VALUE_COMPARE_RESULT index_cmpdisk (const void *memptr1, const void *memptr2, const tp_domain * domain,
                                                  int do_coercion, int total_order, int *start_colp) const;
    
} PR_TYPE, *PRIM;
// *INDENT-ON*

/*
 * PRIMITIVE TYPE STRUCTURES
 */

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
extern PR_TYPE tp_Json;

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
extern PR_TYPE *tp_Type_json;

extern PR_TYPE *tp_Type_id_map[];


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
#define PRIM_INITMEM(type, mem, domain) ((type)->initmem (mem, domain))

/* PRIM_SETMEM
 * Assign a value into instance memory, copy the value.
 */
#define PRIM_SETMEM(type, domain, mem, value) ((type)->setmem (mem, domain, value))

/* PRIM_GETMEM
 * Get a value from instance memory, copy the value.
 */
#define PRIM_GETMEM(type, domain, mem, value) \
  ((type)->getmem (mem, domain, value, true))

/* PRIM_GETMEM_NOCOPY
 * The "nocopy" option for "getmem" is intended only for internal processes
 * that need to access the values but don't want the overhead of copying,
 * particularly for strings.  The only user of this right now is the UNIQUE
 * register logic.
 */
#define PRIM_GETMEM_NOCOPY(type, domain, mem, value) \
  ((type)->getmem (mem, domain, value, false))

/* PRIM_FREEMEM
 * Free any external storage in instance memory that may have been allocated on
 * behalf of a type.
 */
#define PRIM_FREEMEM(type, mem) ((type)->freemem (mem))

/* PRIM_READ
 * Read a value from a disk buffer into instance memory.
 */
#define PRIM_READ(type, domain, tr, mem, len) \
  ((type)->data_readmem (tr, mem, domain, len))

/* PRIM_WRITE
 * Write a value from instance memory into a disk buffer.
 */
#define PRIM_WRITE(type, domain, tr, mem) ((type)->data_writemem (tr, mem, domain))

#define PRIM_WRITEVAL(type, buf, val) ((type)->writeval) (buf, val))
#define PRIM_LENGTHVAL(type, val, size) ((type)->lengthval (val, size))

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
extern PR_TYPE *pr_type_from_id (DB_TYPE id);
extern PR_TYPE *pr_find_type (const char *name);
extern const char *pr_type_name (DB_TYPE id);

extern bool pr_is_set_type (DB_TYPE type);
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
extern int pr_clear_value (DB_VALUE * var);
/* *INDENT-OFF* */
void pr_clear_value_vector (std::vector<DB_VALUE> &value_vector);
/* *INDENT-ON* */

extern int pr_free_value (DB_VALUE * var);
extern DB_VALUE *pr_make_ext_value (void);
extern int pr_free_ext_value (DB_VALUE * value);

/* Special transformation functions */

extern DB_VALUE_COMPARE_RESULT pr_midxkey_compare (DB_MIDXKEY * mul1, DB_MIDXKEY * mul2, int do_coercion,
						   int total_order, int num_index_term, int *start_colp,
						   int *result_size1, int *result_size2, int *diff_column,
						   bool * dom_is_desc, bool * next_dom_is_desc);
extern int pr_midxkey_element_disk_size (char *mem, DB_DOMAIN * domain);
extern int pr_midxkey_get_element_nocopy (const DB_MIDXKEY * midxkey, int index, DB_VALUE * value, int *prev_indexp,
					  char **prev_ptrp);
extern int pr_midxkey_add_elements (DB_VALUE * keyval, DB_VALUE * dbvals, int num_dbvals,
				    struct tp_domain *dbvals_domain_list);
extern int pr_midxkey_init_boundbits (char *bufptr, int n_atts);
extern int pr_index_writeval_disk_size (DB_VALUE * value);
extern int pr_data_writeval_disk_size (DB_VALUE * value);
extern void pr_data_writeval (struct or_buf *buf, DB_VALUE * value);
extern int pr_midxkey_unique_prefix (const DB_VALUE * db_midxkey1, const DB_VALUE * db_midxkey2, DB_VALUE * db_result);
extern int pr_midxkey_get_element_offset (const DB_MIDXKEY * midxkey, int index);
extern int pr_midxkey_add_prefix (DB_VALUE * result, DB_VALUE * prefix, DB_VALUE * postfix, int n_prefix);
extern int pr_midxkey_remove_prefix (DB_VALUE * key, int prefix);
extern int pr_midxkey_common_prefix (DB_VALUE * key1, DB_VALUE * key2);

extern int pr_Inhibit_oid_promotion;

/* Helper function for DB_VALUE printing; caller must free_and_init result. */
extern char *pr_valstring (const DB_VALUE *);

/* area init */
extern int pr_area_init (void);
extern void pr_area_final (void);

extern int pr_complete_enum_value (DB_VALUE * value, struct tp_domain *domain);
extern int pr_get_compression_length (const char *string, int charlen);
extern int pr_get_compressed_data_from_buffer (struct or_buf *buf, char *data, int compressed_size,
					       int decompressed_size);
extern int pr_get_size_and_write_string_to_buffer (struct or_buf *buf, char *val_p, DB_VALUE * value, int *val_size,
						   int align);

extern int pr_data_compress_string (char *string, int str_length, char *compressed_string, int *compressed_length);
extern int pr_clear_compressed_string (DB_VALUE * value);
extern int pr_do_db_value_string_compression (DB_VALUE * value);

#define PRIM_TEMPORARY_DISK_SIZE 256
#define PRIM_COMPRESSION_LENGTH_OFFSET 4

extern int pr_Enable_string_compression;

/* 1 size byte, 4 bytes the compressed size, 4 bytes the decompressed size, length and the max alignment */
#define PRIM_STRING_MAXIMUM_DISK_SIZE(length) (OR_BYTE_SIZE + OR_INT_SIZE + OR_INT_SIZE + (length) + MAX_ALIGNMENT)

/* Worst case scenario for compression from their FAQ */
#define LZO_COMPRESSED_STRING_SIZE(str_length) ((str_length) + ((str_length) / 16) + 64 + 3)

//////////////////////////////////////////////////////////////////////////
// Inline/template implementation
//////////////////////////////////////////////////////////////////////////
// *INDENT-OFF*
void
pr_type::set_data_cmpdisk_function (data_cmpdisk_function_type data_cmpdisk_arg)
{
  f_data_cmpdisk = data_cmpdisk_arg;
}

inline pr_type::data_cmpdisk_function_type
pr_type::get_data_cmpdisk_function () const
{
  return f_data_cmpdisk;
}

inline void
pr_type::set_cmpval_function (cmpval_function_type cmpval_arg)
{
  f_cmpval = cmpval_arg;
}

inline pr_type::cmpval_function_type
pr_type::get_cmpval_function () const
{
  return f_cmpval;
}

inline void
pr_type::initmem (void * memptr, const tp_domain * domain) const
{
  (*f_initmem) (memptr, const_cast<tp_domain *> (domain));
}

inline void
pr_type::initval (DB_VALUE * value, int precision, int scale) const
{
  (*f_initval) (value, precision, scale);
}

inline int
pr_type::setmem (void * memptr, const tp_domain * domain, const DB_VALUE * value) const
{
  return (*f_setmem) (memptr, const_cast<tp_domain *> (domain), const_cast<DB_VALUE *> (value));
}

inline int
pr_type::getmem (void * memptr, const tp_domain * domain, DB_VALUE * value, bool copy) const
{
  return (*f_getmem) (memptr, const_cast<tp_domain *> (domain), value, copy);
}

inline int
pr_type::setval (DB_VALUE * dest, const DB_VALUE * src, bool copy) const
{
  return (*f_setval) (dest, src, copy);
}

inline bool
pr_type::is_data_lengthmem_fixed () const
{
  return (f_data_lengthmem == NULL);
}

inline int
pr_type::data_lengthmem (const void * memptr, const tp_domain * domain, int disk) const
{
  if (is_data_lengthmem_fixed ())
    {
      return disksize;
    }
  else
    {
      return (*f_data_lengthmem) (const_cast<void *> (memptr), const_cast<tp_domain *> (domain), disk);
    }
}

inline bool
pr_type::is_data_lengthval_fixed () const
{
  return f_data_lengthval == NULL;
}

inline int
pr_type::data_lengthval (const DB_VALUE * value, int disk) const
{
  if (is_data_lengthval_fixed ())
    {
      return disksize;
    }
  else
    {
      return (*f_data_lengthval) (const_cast<DB_VALUE *> (value), disk);
    }
}

inline void
pr_type::data_writemem (or_buf * buf, const void * memptr, const tp_domain * domain) const
{
  (*f_data_writemem) (buf, const_cast<void *> (memptr), const_cast <tp_domain *> (domain));
}

inline void
pr_type::data_readmem (or_buf * buf, void * memptr, const tp_domain * domain, int size) const
{
  (*f_data_readmem) (buf, memptr, const_cast<tp_domain *> (domain), size);
}

inline int
pr_type::data_writeval (or_buf * buf, const DB_VALUE * value) const
{
  return (*f_data_writeval) (buf, const_cast<DB_VALUE *> (value));
}

inline int
pr_type::data_readval (or_buf * buf, DB_VALUE * value, const tp_domain * domain, int size, bool copy, char * copy_buf, int copy_buf_len) const
{
  return (*f_data_readval) (buf, value, const_cast<tp_domain *> (domain), size, copy, copy_buf, copy_buf_len);
}

inline int
pr_type::index_lengthmem (const void * memptr, const tp_domain * domain) const
{
  if (f_index_lengthmem == NULL)
    {
      return disksize;
    }
  else
    {
      return (*f_index_lengthmem) (const_cast<void *> (memptr), const_cast<tp_domain *> (domain));
    }
}

inline bool
pr_type::is_index_lengthval_fixed () const
{
  return f_index_lengthval == NULL;
}

inline int
pr_type::index_lengthval (const DB_VALUE * value) const
{
  if (is_index_lengthval_fixed ())
    {
      return disksize;
    }
  else
    {
      return (*f_index_lengthval) (const_cast<DB_VALUE *> (value));
    }
}

inline int
pr_type::index_writeval (or_buf * buf, const DB_VALUE * value) const
{
  return (*f_index_writeval) (buf, const_cast<DB_VALUE *> (value));
}

inline int
pr_type::index_readval (or_buf * buf, DB_VALUE * value, const tp_domain * domain, int size, bool copy, char * copy_buf, int copy_buf_len) const
{
  return (*f_index_readval) (buf, value, const_cast<tp_domain *> (domain), size, copy, copy_buf, copy_buf_len);
}

inline DB_VALUE_COMPARE_RESULT
pr_type::index_cmpdisk (const void * memptr1, const void * memptr2, const tp_domain * domain, int do_coercion, int total_order, int * start_colp) const
{
  return (*f_index_cmpdisk) (const_cast<void *> (memptr1), const_cast<void *> (memptr2),
                             const_cast<tp_domain *> (domain), do_coercion, total_order, start_colp);
}

inline void
pr_type::freemem (void * memptr) const
{
  if (f_freemem != NULL)
    {
      (*f_freemem) (memptr);
    }
 }

inline DB_VALUE_COMPARE_RESULT
pr_type::data_cmpdisk (const void * memptr1, const void * memptr2, const tp_domain * domain, int do_coercion, int total_order, int * start_colp) const
{
  return (*f_data_cmpdisk) (const_cast<void *> (memptr1), const_cast<void *> (memptr2),
                            const_cast<tp_domain *> (domain), do_coercion, total_order, start_colp);
}

inline DB_VALUE_COMPARE_RESULT
pr_type::cmpval (const DB_VALUE * value, const DB_VALUE * value2, int do_coercion, int total_order, int * start_colp, int collation) const
{
  return (*f_cmpval) (const_cast<DB_VALUE *> (value), const_cast<DB_VALUE *> (value2), do_coercion, total_order,
                      start_colp, collation);
}

// *INDENT-ON*

#endif /* _OBJECT_PRIMITIVE_H_ */
