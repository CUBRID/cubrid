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

#include <cassert>
#include <cstdio>
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
    typedef void (*initmem_function_type) (void *memptr, struct tp_domain * domain);
    typedef void (*initval_function_type) (DB_VALUE * value, int precision, int scale);
    typedef int (*setmem_function_type) (void *memptr, struct tp_domain * domain, DB_VALUE * value);
    typedef int (*getmem_function_type) (void *memptr, struct tp_domain * domain, DB_VALUE * value, bool copy);
    typedef int (*setval_function_type) (DB_VALUE * dest, const DB_VALUE * src, bool copy);
    typedef int (*data_lengthmem_function_type) (void *memptr, struct tp_domain * domain, int disk);
    typedef int (*data_lengthval_function_type) (DB_VALUE * value, int disk);
    typedef void (*data_writemem_function_type) (struct or_buf * buf, void *memptr, struct tp_domain * domain);
    typedef void (*data_readmem_function_type) (struct or_buf * buf, void *memptr, struct tp_domain * domain, int size);
    typedef int (*data_writeval_function_type) (struct or_buf * buf, DB_VALUE * value);
    typedef int (*data_readval_function_type) (struct or_buf * buf, DB_VALUE * value, struct tp_domain * domain,
                                               int size, bool copy, char *copy_buf, int copy_buf_len);
    typedef int (*index_lengthmem_function_type) (void *memptr, struct tp_domain * domain);
    typedef int (*index_lengthval_function_type) (DB_VALUE * value);
    typedef int (*index_writeval_function_type) (struct or_buf * buf, DB_VALUE * value);
    typedef int (*index_readval_function_type) (struct or_buf * buf, DB_VALUE * value, struct tp_domain * domain,
                                                int size, bool copy, char *copy_buf, int copy_buf_len);
    typedef DB_VALUE_COMPARE_RESULT (*index_cmpdisk_function_type) (void *memptr1, void *memptr2,
                                                                    struct tp_domain * domain, int do_coercion,
                                                                    int total_order, int *start_colp);
    typedef void (*freemem_function_type) (void *memptr);
    typedef DB_VALUE_COMPARE_RESULT (*data_cmpdisk_function_type) (void *memptr1, void *memptr2, tp_domain * domain,
                                                                   int do_coercion, int total_order, int *start_colp);
    typedef DB_VALUE_COMPARE_RESULT (*cmpval_function_type) (DB_VALUE * value, DB_VALUE * value2, int do_coercion,
                                                             int total_order, int *start_colp, int collation);

    // all member variables:
  public:
    /* internal identifier name */
    const char *name;
    DB_TYPE id;
    int variable_p;
    int size;
    int disksize;
    int alignment;

  protected:
    initmem_function_type f_initmem;
    initval_function_type f_initval;
    setmem_function_type f_setmem;
    getmem_function_type f_getmem;
    setval_function_type f_setval;
    data_lengthmem_function_type f_data_lengthmem;
    data_lengthval_function_type f_data_lengthval;
    data_writemem_function_type f_data_writemem;
    data_readmem_function_type f_data_readmem;
    data_writeval_function_type f_data_writeval;
    data_readval_function_type f_data_readval;
    index_lengthmem_function_type f_index_lengthmem;
    index_lengthval_function_type f_index_lengthval;
    index_writeval_function_type f_index_writeval;
    index_readval_function_type f_index_readval;
    index_cmpdisk_function_type f_index_cmpdisk;
    freemem_function_type f_freemem;
    data_cmpdisk_function_type f_data_cmpdisk;
    cmpval_function_type f_cmpval;

  public:
    pr_type () = delete;
    pr_type (const char *name_arg, DB_TYPE id_arg, int varp_arg, int size_arg, int disksize_arg, int align_arg,
             initmem_function_type initmem_f_arg, initval_function_type initval_f_arg,
             setmem_function_type setmem_f_arg, getmem_function_type getmem_f_arg, setval_function_type setval_f_arg,
             data_lengthmem_function_type data_lengthmem_f_arg, data_lengthval_function_type data_lengthval_f_arg,
             data_writemem_function_type data_writemem_f_arg, data_readmem_function_type data_readmem_f_arg,
             data_writeval_function_type data_writeval_f_arg, data_readval_function_type data_readval_f_arg,
             index_lengthmem_function_type index_lengthmem_f_arg, index_lengthval_function_type index_lengthval_f_arg,
             index_writeval_function_type index_writeval_f_arg, index_readval_function_type index_readval_f_arg,
             index_cmpdisk_function_type index_cmpdisk_f_arg, freemem_function_type freemem_f_arg,
             data_cmpdisk_function_type data_cmpdisk_f_arg, cmpval_function_type cmpval_f_arg);

    // todo - remove all the const stripping code from function implementation; signatures for all internal functions
    //        (prefixed by f_) and their various implementations inside object_primitive.c should be updated.
    //        postponed for a better days and better design

    // setters/getters
    inline const char *get_name () const;
    inline DB_TYPE get_id () const;
    inline size_t get_alignment () const;

    void set_data_cmpdisk_function (data_cmpdisk_function_type data_cmpdisk_arg);
    data_cmpdisk_function_type get_data_cmpdisk_function () const;

    void set_cmpval_function (cmpval_function_type cmpval_arg);
    cmpval_function_type get_cmpval_function () const;

    // is fixed/variable
    inline bool is_always_variable () const;
    inline bool is_size_computed () const;

    // size functions
    inline int get_mem_size_of_mem (const void *mem, const tp_domain * domain = NULL) const;
    inline int get_disk_size_of_mem (const void *mem, const tp_domain * domain = NULL) const;
    inline int get_index_size_of_mem (const void *memptr, const tp_domain * domain) const;
    inline int get_mem_size_of_value (const DB_VALUE * value) const;
    inline int get_disk_size_of_value (const DB_VALUE * value) const;
    inline int get_index_size_of_value (const DB_VALUE * value) const;

    // operations for in-memory representations
    inline void initmem (void *memptr, const tp_domain * domain) const;
    inline int setmem (void *memptr, const tp_domain * domain, const DB_VALUE * value) const;
    inline int getmem (void *memptr, const tp_domain * domain, DB_VALUE * value, bool copy = true) const;
    inline void freemem (void *memptr) const;

    // operations for db_value's
    inline void initval (DB_VALUE * value, int precision, int scale) const;
    inline int setval (DB_VALUE * dest, const DB_VALUE * src, bool copy) const;
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

/*
 * EXTERNAL FUNCTIONS
 */

inline void
PRIM_SET_NULL (DB_VALUE * value)
{
  value->domain.general_info.is_null = 1;
  value->need_clear = false;
}

/* Type structure accessors */
extern PR_TYPE *pr_type_from_id (DB_TYPE id);
extern PR_TYPE *pr_find_type (const char *name);
extern const char *pr_type_name (DB_TYPE id);

extern bool pr_is_set_type (DB_TYPE type);
extern int pr_is_string_type (DB_TYPE type);
extern int pr_is_prefix_key_type (DB_TYPE type);
extern int pr_is_variable_type (DB_TYPE type);

/* Size calculators */

extern int pr_mem_size (const PR_TYPE * type);
extern int pr_value_mem_size (const DB_VALUE * value);

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
extern int pr_get_compression_length (const char *string, int str_length);
extern int pr_get_compressed_data_from_buffer (struct or_buf *buf, char *data, int compressed_size,
					       int expected_decompressed_size);
extern int pr_get_size_and_write_string_to_buffer (struct or_buf *buf, char *val_p, DB_VALUE * value, int *val_size,
						   int align);

extern int pr_data_compress_string (const char *string, int str_length, char *compressed_string,
				    int compress_buffer_size, int *compressed_length);

extern int pr_clear_compressed_string (DB_VALUE * value);
extern int pr_do_db_value_string_compression (DB_VALUE * value);

#define PRIM_TEMPORARY_DISK_SIZE 256
#define PRIM_COMPRESSION_LENGTH_OFFSET 4

extern int pr_Enable_string_compression;

/* 1 size byte, 4 bytes the compressed size, 4 bytes the decompressed size, length and the max alignment */
#define PRIM_STRING_MAXIMUM_DISK_SIZE(length) (OR_BYTE_SIZE + OR_INT_SIZE + OR_INT_SIZE + (length) + MAX_ALIGNMENT)

// *INDENT-OFF*
#define MIDXKEY_BOUNDBITS_INIT(bufptr, nbytes)  do { if(nbytes > 0) { memset ((bufptr), 0x00, (nbytes)); } } while(0)
// *INDENT-ON*

//////////////////////////////////////////////////////////////////////////
// Inline/template implementation
//////////////////////////////////////////////////////////////////////////
// *INDENT-OFF*
const char *
pr_type::get_name () const
{
  return name;
}

DB_TYPE
pr_type::get_id () const
{
  return id;
}

size_t
pr_type::get_alignment () const
{
  return (size_t) alignment;
}

bool
pr_type::is_always_variable () const
{
  // NOTE - this is not reliable to determine if a type is always fixed size or not. Numeric type is not variable, but
  // it has size computing functions!
  return variable_p != 0;
}

bool
pr_type::is_size_computed () const
{
  assert ((f_data_lengthmem == NULL) == (f_data_lengthval == NULL));
  return f_data_lengthmem != NULL && f_data_lengthval != NULL;
}

int
pr_type::get_disk_size_of_mem (const void *mem, const tp_domain * domain) const
{
  if (f_data_lengthmem != NULL)
    {
      return (*f_data_lengthmem) (const_cast<void *> (mem), const_cast<tp_domain *> (domain), 1);
    }
  else
    {
      assert (disksize != 0 || id == DB_TYPE_NULL);
      return disksize;
    }
}

int
pr_type::get_mem_size_of_mem (const void *mem, const tp_domain * domain) const
{
  if (f_data_lengthmem != NULL)
    {
      return (*f_data_lengthmem) (const_cast<void *> (mem), const_cast<tp_domain *> (domain), 0);
    }
  else
    {
      assert (size != 0 || id == DB_TYPE_NULL);
      return size;
    }
}

int
pr_type::get_mem_size_of_value (const DB_VALUE * value) const
{
  if (f_data_lengthval != NULL)
    {
      return (*f_data_lengthval) (const_cast<DB_VALUE *> (value), 0);
    }
  else
    {
      assert (size != 0 || id == DB_TYPE_NULL);
      return size;
    }
}

int
pr_type::get_disk_size_of_value (const DB_VALUE * value) const
{
  if (f_data_lengthval != NULL)
    {
      return (*f_data_lengthval) (const_cast<DB_VALUE *> (value), 1);
    }
  else
    {
      assert (disksize != 0 || id == DB_TYPE_NULL);
      return disksize;
    }
}

void
pr_type::initmem (void * memptr, const tp_domain * domain) const
{
  /*
   * Initialize an attribute value in instance memory
   * For fixed width attributes, it tries to zero out the memory. This isn't a requirement but it makes the object
   * block look cleaner in the debugger.
   * For variable width attributes, it makes sure the pointers are NULL.
   */
  (*f_initmem) (memptr, const_cast<tp_domain *> (domain));
}

void
pr_type::initval (DB_VALUE * value, int precision, int scale) const
{
  (*f_initval) (value, precision, scale);
}

int
pr_type::setmem (void * memptr, const tp_domain * domain, const DB_VALUE * value) const
{
  // Assign a value into instance memory, copy the value.
  return (*f_setmem) (memptr, const_cast<tp_domain *> (domain), const_cast<DB_VALUE *> (value));
}

int
pr_type::getmem (void * memptr, const tp_domain * domain, DB_VALUE * value, bool copy) const
{
  return (*f_getmem) (memptr, const_cast<tp_domain *> (domain), value, copy);
}

int
pr_type::setval (DB_VALUE * dest, const DB_VALUE * src, bool copy) const
{
  return (*f_setval) (dest, src, copy);
}

void
pr_type::data_writemem (or_buf * buf, const void * memptr, const tp_domain * domain) const
{
  (*f_data_writemem) (buf, const_cast<void *> (memptr), const_cast <tp_domain *> (domain));
}

void
pr_type::data_readmem (or_buf * buf, void * memptr, const tp_domain * domain, int size) const
{
  (*f_data_readmem) (buf, memptr, const_cast<tp_domain *> (domain), size);
}

int
pr_type::data_writeval (or_buf * buf, const DB_VALUE * value) const
{
  return (*f_data_writeval) (buf, const_cast<DB_VALUE *> (value));
}

int
pr_type::data_readval (or_buf * buf, DB_VALUE * value, const tp_domain * domain, int size, bool copy, char * copy_buf,
                       int copy_buf_len) const
{
  return (*f_data_readval) (buf, value, const_cast<tp_domain *> (domain), size, copy, copy_buf, copy_buf_len);
}

int
pr_type::get_index_size_of_mem (const void * memptr, const tp_domain * domain) const
{
  if (f_index_lengthmem != NULL)
    {
      return (*f_index_lengthmem) (const_cast<void *> (memptr), const_cast<tp_domain *> (domain));
    }
  else
    {
      assert (disksize != 0 || id == DB_TYPE_NULL);
      return disksize;
    }
}

int
pr_type::get_index_size_of_value (const DB_VALUE * value) const
{
  if (f_index_lengthval != NULL)
    {
      return (*f_index_lengthval) (const_cast<DB_VALUE *> (value));
    }
  else
    {
      assert (disksize != 0 || id == DB_TYPE_NULL);
      return disksize;
    }
}

int
pr_type::index_writeval (or_buf * buf, const DB_VALUE * value) const
{
  return (*f_index_writeval) (buf, const_cast<DB_VALUE *> (value));
}

int
pr_type::index_readval (or_buf * buf, DB_VALUE * value, const tp_domain * domain, int size, bool copy, char * copy_buf,
                        int copy_buf_len) const
{
  return (*f_index_readval) (buf, value, const_cast<tp_domain *> (domain), size, copy, copy_buf, copy_buf_len);
}

DB_VALUE_COMPARE_RESULT
pr_type::index_cmpdisk (const void * memptr1, const void * memptr2, const tp_domain * domain, int do_coercion,
                        int total_order, int * start_colp) const
{
  return (*f_index_cmpdisk) (const_cast<void *> (memptr1), const_cast<void *> (memptr2),
                             const_cast<tp_domain *> (domain), do_coercion, total_order, start_colp);
}

void
pr_type::freemem (void * memptr) const
{
  if (f_freemem != NULL)
    {
      (*f_freemem) (memptr);
    }
 }

DB_VALUE_COMPARE_RESULT
pr_type::data_cmpdisk (const void * memptr1, const void * memptr2, const tp_domain * domain, int do_coercion,
                       int total_order, int * start_colp) const
{
  return (*f_data_cmpdisk) (const_cast<void *> (memptr1), const_cast<void *> (memptr2),
                            const_cast<tp_domain *> (domain), do_coercion, total_order, start_colp);
}

DB_VALUE_COMPARE_RESULT
pr_type::cmpval (const DB_VALUE * value, const DB_VALUE * value2, int do_coercion, int total_order, int * start_colp,
                 int collation) const
{
  return (*f_cmpval) (const_cast<DB_VALUE *> (value), const_cast<DB_VALUE *> (value2), do_coercion, total_order,
                      start_colp, collation);
}

// *INDENT-ON*

#endif /* _OBJECT_PRIMITIVE_H_ */
