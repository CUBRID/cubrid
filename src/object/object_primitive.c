/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * object_primitive.c - This file contains code for handling the values of
 *                      primitive types in memory and for conversion between
 *                      the disk representation.
 */

#ident "$Id$"

#include "config.h"

/*
 * NOTE NOTE NOTE
 * This text is old and not accurate in light of recent change to the
 * type vectors.  It can be used as a rough guide but probably contains
 * many errors
 *
 * Primitive types each have a descriptor structure that contains several
 * type specific pieces of information along with a number of handler functions
 * for operations that can be performed on a value of that type.  This
 * is similar to a "class" definition for primitive types where the handler
 * functions are "methods".  Defining type operations in this way avoids
 * the propogation of large switch statements throughout the upper layers
 * to handle type specific operations.
 *
 * ATTRIBUTE VALUE ACCESSORS
 *
 * The current set of accessor functions deals with two similar but not
 * necessarily identical representations of attribute values.  The first
 * type called "memory" values are stored in the memory image for an
 * object.  The second type called "data" values are represented with
 * the C union DATAVALUE.  You can think of DATAVALUEs as the
 * "communications packet" between this layer and the higher level layers.
 *
 * Since we don't generally want direct pointers into an objects memory
 * image to get out to the upper layers, we perform all attribute
 * access through DATAVALUES.  When an attribute value is requested,
 * its memory value is copied into a DATAVALUE and returned.  When
 * an attribute value is to be changed, the new value contained
 * in a DATAVALUE is copied into the memory image.
 *
 * A few functions that have been carefully coded for performance may
 * directly access the memory image but most will communicate through
 * DATAVALUEs.  All access from the user application level will be
 * done with DATAVALUEs.  This helps to prevent corruption of the
 * memory image and also allows the memory representation to be
 * different than the DATAVALUE representation.
 *
 * Currently, the memory and DATAVALUE representatins are exactly
 * the same for all primitive types except OBJECT REFERENCES.  In
 * memory, an object reference will be stored with a memory_oid
 * structure that contains the OID and a pointer to the MOP if
 * one has been created.  In the DATAVALUE an object reference
 * is represented only with a pointer to a MOP.  This is because
 * we don't want OID's to be surfaced to the user application layer.
 * All user object references must be performed with MOPs.
 *
 * If the memory representation and the DATAVALUE representation for
 * the value of a particular type are identical, it is not necessary
 * to specify the DATAVALUE specific accessors described below.  Instead
 * the memory accessors may be passed DATAVALUEs without error.  ANSI
 * declares that a pointer to a union may legally be cast as a pointer
 * to any of its constituent elements.
 *
 * The following accessor functions are defined for each primitive type:
 *
 * void initmem(void *mem)
 *
 * Initializes the contents of a memory value to a reasonable default.
 *
 * void initval(DB_DATA *value)
 *
 * Initializes the contents of a DB_DATA to a reasonable default.
 * This function is optional and specified only if the DB_DATA
 * representation is different than the memory representation.  If
 * they are the same, you may use "initmem" on a DB_DATA.
 *
 * void getmem(void *mem, TP_DOMAIN *domain, DB_VALUE *value, bool copy)
 *
 * This extracts memory value and places it in a DB_DATA.  The copy
 * flag may be set for primitive types such as strings that have an
 * associated block of storage that is external to the object.
 * If the copy flag is not set, the DB_DATA will contain a direct
 * pointer to the external storage.  If the flag is set, a copy of
 * the external storage will be returned in the DB_DATA.  The copy
 * flag should almost ALWAYS be set.
 *
 * void setmem(void *mem, DB_DATA *value, bool copy)
 *
 * This takes a DB_DATA and copies it into the object's memory.
 * If the primitive type has external storage and the copy flag is
 * set, the following operations will be performed:
 *
 * - if memory currently contains an external allocation, it is freed
 * - the external allocation in DB_DATA is copied before being
 * placed in memory
 *
 * It is permissible for the passed DB_DATA to be NULL.  In this case
 * the memory value will be initialized as if "initmem" were called.  In
 * addition, if the copy flag is set, any external storage will be freed.
 * This behavior eliminates the need for a seperate "free" accessor function.
 *
 * void setval(DB_DATA *dest, DB_DATA *src, bool copy)
 *
 * This is used to assign DB_DATAS to other DB_DATAS.  This function
 * is optional and specified only if the DB_DATA and memory representations
 * are different.  If the representations are the same, "setmem" may
 * be used to assign DB_DATAS.  It is permissible for "src" to be NULL
 * in which case the "dest" value will be initialized and if the copy flag
 * is set, any external storage will be freed.
 *
 *
 * MEMORY/DISK TRANSFORMATION FUCNTIONS
 *
 * The following accessor functions are used primarily by the transformer and
 * will not be of general use to the upper layers.
 *
 * int length(void *mem, int disk)
 *
 * This is used to calculate the amount of storage necessary for the
 * representation of a value.  It is specified only for primitive types
 * that have externally allocated storage.  If the disk flag is not set
 * the size returned is the amount of memory storage.  If the disk flag
 * is set the size returned is the amount of storage requred for the
 * disk representation.  If the primitive type does not have external
 * storage, then this function is not required and the two sizes may
 * be found in the "size" and "disksize" fields of the PR_TYPE structure.
 *
 * void write(OR_BUF *buf, void *mem)
 *
 * This writes the disk representation of a memory value.  The "buf"
 * is an object representation buffer that is accessed using the
 * or_ functions.
 *
 * void read(OR_BUF *buf, void *mem)
 *
 * This reads the disk representation of a value and places it in memory.
 *
 * TODO: include primch.c and remove it
 */

#include <stdlib.h>
#include <string.h>

#include "system_parameter.h"
#include "ustring.h"
#include "memory_manager_1.h"
#include "db.h"
#if !defined (SERVER_MODE)
#include "work_space.h"
#include "virtual_object_1.h"
#endif /* !SERVER_MODE */
#include "object_representation.h"
#include "object_primitive.h"
#include "set_object_1.h"
#if !defined (SERVER_MODE)
#include "elo_class.h"
#include "locator_cl.h"
#endif /* !SERVER_MODE */
#include "object_print_1.h"
#include "memory_manager_4.h"
#include "intl.h"
#include "language_support.h"
#include "qp_str.h"
#include "memory_manager_2.h"
#include "object_accessor.h"
#if !defined (SERVER_MODE)
#include "transform_sky.h"
#endif /* !SERVER_MODE */
#include "server.h"

#if defined (SERVER_MODE)
#include "thread_impl.h"
#endif

/* this must be the last header file included!!! */
#include "dbval.h"

#define MR_CMP(d1, d2, rev, dom)                                        \
    ((rev || ((dom) && (dom)->is_desc))                                 \
         ? ((d1) < (d2)) ? DB_GT : ((d1) > (d2)) ? DB_LT : DB_EQ        \
         : ((d1) < (d2)) ? DB_LT : ((d1) > (d2)) ? DB_GT : DB_EQ)

/*
 * MR_OID_SIZE
 * Hack so we don't have to conditionalize the type vector.
 * On the server we don't have the structure definition for this.
 */
#if !defined (SERVER_MODE)
#define MR_OID_SIZE sizeof(WS_MEMOID)
#else
#define MR_OID_SIZE 0
#endif

#if defined (SERVER_MODE)
#define VALUE_AREA_COUNT 32
#else
#define VALUE_AREA_COUNT 1024
#endif

/*
 * PR_OID_PROMOTION_DEFAULT
 *
 * Kludge, this can be set only by desc.c in the migration utilities.
 * It is intended to allow some control over whether or not OIDs read
 * by "readval" are automatically promoted to MOPs or not.  This
 * is used to prevent cluttering up the workspace with MOPs when we
 * don't really need them.  When this is enabled, callers of "readval"
 * had better be prepared to see DB_VALUE structures containing unswizzled
 * OIDs rather than MOPs.  Not intended for use by by real applications.
 * Think about this more, this may also be the mechanism by which the server
 * can control reading of object values.
 *
 * Note that this makes sense only for the "readval" function, the "readmem
 * function must be building a workspace instance memory representation
 * and oid promotion cannot be controlled there.
 *
 */
#if defined (SERVER_MODE)
#define PR_INHIBIT_OID_PROMOTION_DEFAULT 1
#else
#define PR_INHIBIT_OID_PROMOTION_DEFAULT 0
#endif

/*
 * These are currently fixed length widets of length DB_NUMERIC_BUF_SIZE.
 * Ultimately they may be of variable size based on the precision and scale,
 * in antipication of that move, use the followign function for copying.
 */
#define OR_NUMERIC_SIZE(precision) DB_NUMERIC_BUF_SIZE
#define MR_NUMERIC_SIZE(precision) DB_NUMERIC_BUF_SIZE

#if !defined (SERVER_MODE)
static const char *pr_copy_string (const char *str);
#endif
static void mr_initmem_null (void *memptr);
static int mr_setmem_null (void *memptr, TP_DOMAIN * domain,
			   DB_VALUE * value);
static int mr_getmem_null (void *memptr, TP_DOMAIN * domain,
			   DB_VALUE * value, bool copy);
static void mr_writemem_null (OR_BUF * buf, void *memptr, TP_DOMAIN * domain);
static void mr_readmem_null (OR_BUF * buf, void *memptr, TP_DOMAIN * domain,
			     int size);
static void mr_initval_null (DB_VALUE * value, int precision, int scale);
static int mr_setval_null (DB_VALUE * dest, DB_VALUE * src, bool copy);
static int mr_writeval_null (OR_BUF * buf, DB_VALUE * value);
static int mr_readval_null (OR_BUF * buf, DB_VALUE * value,
			    TP_DOMAIN * domain, int size, bool copy,
			    char *copy_buf, int copy_buf_len);
static int mr_cmpdisk_null (void *mem1, void *mem2, TP_DOMAIN * domain,
			    int do_reverse, int do_coercion, int total_order,
			    int *start_colp);
static int mr_cmpval_null (DB_VALUE * value1, DB_VALUE * value2,
			   TP_DOMAIN * domain, int do_reverse,
			   int do_coercion, int total_order, int *start_colp);
static void mr_initmem_int (void *mem);
static int mr_setmem_int (void *mem, TP_DOMAIN * domain, DB_VALUE * value);
static int mr_getmem_int (void *mem, TP_DOMAIN * domain, DB_VALUE * value,
			  bool copy);
static void mr_writemem_int (OR_BUF * buf, void *mem, TP_DOMAIN * domain);
static void mr_readmem_int (OR_BUF * buf, void *mem, TP_DOMAIN * domain,
			    int size);
static void mr_initval_int (DB_VALUE * value, int precision, int scale);
static int mr_setval_int (DB_VALUE * dest, DB_VALUE * src, bool copy);
static int mr_writeval_int (OR_BUF * buf, DB_VALUE * value);
static int mr_readval_int (OR_BUF * buf, DB_VALUE * value, TP_DOMAIN * domain,
			   int size, bool copy, char *copy_buf,
			   int copy_buf_len);
static int mr_cmpdisk_int (void *mem1, void *mem2, TP_DOMAIN * domain,
			   int do_reverse, int do_coercion, int total_order,
			   int *start_colp);
static int mr_cmpval_int (DB_VALUE * value1, DB_VALUE * value2,
			  TP_DOMAIN * domain, int do_reverse, int do_coercion,
			  int total_order, int *start_colp);
static void mr_initmem_short (void *mem);
static int mr_setmem_short (void *mem, TP_DOMAIN * domain, DB_VALUE * value);
static int mr_getmem_short (void *mem, TP_DOMAIN * domain,
			    DB_VALUE * value, bool copy);
static void mr_writemem_short (OR_BUF * buf, void *memptr,
			       TP_DOMAIN * domain);
static void mr_readmem_short (OR_BUF * buf, void *mem, TP_DOMAIN * domain,
			      int size);
static void mr_initval_short (DB_VALUE * value, int precision, int scale);
static int mr_setval_short (DB_VALUE * dest, DB_VALUE * src, bool copy);
static int mr_writeval_short (OR_BUF * buf, DB_VALUE * value);
static int mr_readval_short (OR_BUF * buf, DB_VALUE * value,
			     TP_DOMAIN * domain, int size, bool copy,
			     char *copy_buf, int copy_buf_len);
static int mr_cmpdisk_short (void *mem1, void *mem2, TP_DOMAIN * domain,
			     int do_reverse, int do_coercion, int total_order,
			     int *start_colp);
static int mr_cmpval_short (DB_VALUE * value1, DB_VALUE * value2,
			    TP_DOMAIN * domain, int do_reverse,
			    int do_coercion, int total_order,
			    int *start_colp);
static void mr_initmem_float (void *mem);
static int mr_setmem_float (void *mem, TP_DOMAIN * domain, DB_VALUE * value);
static int mr_getmem_float (void *mem, TP_DOMAIN * domain,
			    DB_VALUE * value, bool copy);
static void mr_writemem_float (OR_BUF * buf, void *mem, TP_DOMAIN * domain);
static void mr_readmem_float (OR_BUF * buf, void *mem, TP_DOMAIN * domain,
			      int size);
static void mr_initval_float (DB_VALUE * value, int precision, int scale);
static int mr_setval_float (DB_VALUE * dest, DB_VALUE * src, bool copy);
static int mr_writeval_float (OR_BUF * buf, DB_VALUE * value);
static int mr_readval_float (OR_BUF * buf, DB_VALUE * value,
			     TP_DOMAIN * domain, int size, bool copy,
			     char *copy_buf, int copy_buf_len);
static int mr_cmpdisk_float (void *mem1, void *mem2, TP_DOMAIN * domain,
			     int do_reverse, int do_coercion, int total_order,
			     int *start_colp);
static int mr_cmpval_float (DB_VALUE * value1, DB_VALUE * value2,
			    TP_DOMAIN * domain, int do_reverse,
			    int do_coercion, int total_order,
			    int *start_colp);
static void mr_initmem_double (void *mem);
static int mr_setmem_double (void *mem, TP_DOMAIN * domain, DB_VALUE * value);
static int mr_getmem_double (void *mem, TP_DOMAIN * domain,
			     DB_VALUE * value, bool copy);
static void mr_writemem_double (OR_BUF * buf, void *mem, TP_DOMAIN * domain);
static void mr_readmem_double (OR_BUF * buf, void *mem, TP_DOMAIN * domain,
			       int size);
static void mr_initval_double (DB_VALUE * value, int precision, int scale);
static int mr_setval_double (DB_VALUE * dest, DB_VALUE * src, bool copy);
static int mr_writeval_double (OR_BUF * buf, DB_VALUE * value);
static int mr_readval_double (OR_BUF * buf, DB_VALUE * value,
			      TP_DOMAIN * domain, int size, bool copy,
			      char *copy_buf, int copy_buf_len);
static int mr_cmpdisk_double (void *mem1, void *mem2, TP_DOMAIN * domain,
			      int do_reverse, int do_coercion,
			      int total_order, int *start_colp);
static int mr_cmpval_double (DB_VALUE * value1, DB_VALUE * value2,
			     TP_DOMAIN * domain, int do_reverse,
			     int do_coercion, int total_order,
			     int *start_colp);
static void mr_initmem_time (void *mem);
static int mr_setmem_time (void *mem, TP_DOMAIN * domain, DB_VALUE * value);
static int mr_getmem_time (void *mem, TP_DOMAIN * domain,
			   DB_VALUE * value, bool copy);
static void mr_writemem_time (OR_BUF * buf, void *mem, TP_DOMAIN * domain);
static void mr_readmem_time (OR_BUF * buf, void *mem, TP_DOMAIN * domain,
			     int size);
static void mr_initval_time (DB_VALUE * value, int precision, int scale);
static int mr_setval_time (DB_VALUE * dest, DB_VALUE * src, bool copy);
static int mr_writeval_time (OR_BUF * buf, DB_VALUE * value);
static int mr_readval_time (OR_BUF * buf, DB_VALUE * value,
			    TP_DOMAIN * domain, int size, bool copy,
			    char *copy_buf, int copy_buf_len);
static int mr_cmpdisk_time (void *mem1, void *mem2, TP_DOMAIN * domain,
			    int do_reverse, int do_coercion, int total_order,
			    int *start_colp);
static int mr_cmpval_time (DB_VALUE * value1, DB_VALUE * value2,
			   TP_DOMAIN * domain, int do_reverse,
			   int do_coercion, int total_order, int *start_colp);
static void mr_initmem_utime (void *mem);
static int mr_setmem_utime (void *mem, TP_DOMAIN * domain, DB_VALUE * value);
static int mr_getmem_utime (void *mem, TP_DOMAIN * domain,
			    DB_VALUE * value, bool copy);
static void mr_writemem_utime (OR_BUF * buf, void *mem, TP_DOMAIN * domain);
static void mr_readmem_utime (OR_BUF * buf, void *mem, TP_DOMAIN * domain,
			      int size);
static void mr_initval_utime (DB_VALUE * value, int precision, int scale);
static int mr_setval_utime (DB_VALUE * dest, DB_VALUE * src, bool copy);
static int mr_writeval_utime (OR_BUF * buf, DB_VALUE * value);
static int mr_readval_utime (OR_BUF * buf, DB_VALUE * value,
			     TP_DOMAIN * domain, int size, bool copy,
			     char *copy_buf, int copy_buf_len);
static int mr_cmpdisk_utime (void *mem1, void *mem2, TP_DOMAIN * domain,
			     int do_reverse, int do_coercion, int total_order,
			     int *start_colp);
static int mr_cmpval_utime (DB_VALUE * value1, DB_VALUE * value2,
			    TP_DOMAIN * domain, int do_reverse,
			    int do_coercion, int total_order,
			    int *start_colp);
static void mr_initmem_money (void *memptr);
static int mr_setmem_money (void *memptr, TP_DOMAIN * domain,
			    DB_VALUE * value);
static int mr_getmem_money (void *memptr, TP_DOMAIN * domain,
			    DB_VALUE * value, bool copy);
static void mr_writemem_money (OR_BUF * buf, void *mem, TP_DOMAIN * domain);
static void mr_readmem_money (OR_BUF * buf, void *mem, TP_DOMAIN * domain,
			      int size);
static void mr_initval_money (DB_VALUE * value, int precision, int scale);
static int mr_setval_money (DB_VALUE * dest, DB_VALUE * src, bool copy);
static int mr_writeval_money (OR_BUF * buf, DB_VALUE * value);
static int mr_readval_money (OR_BUF * buf, DB_VALUE * value,
			     TP_DOMAIN * domain, int size, bool copy,
			     char *copy_buf, int copy_buf_len);
static int mr_cmpdisk_money (void *mem1, void *mem2, TP_DOMAIN * domain,
			     int do_reverse, int do_coercion, int total_order,
			     int *start_colp);
static int mr_cmpval_money (DB_VALUE * value1, DB_VALUE * value2,
			    TP_DOMAIN * domain, int do_reverse,
			    int do_coercion, int total_order,
			    int *start_colp);
static void mr_initmem_date (void *mem);
static int mr_setmem_date (void *mem, TP_DOMAIN * domain, DB_VALUE * value);
static int mr_getmem_date (void *mem, TP_DOMAIN * domain,
			   DB_VALUE * value, bool copy);
static void mr_writemem_date (OR_BUF * buf, void *mem, TP_DOMAIN * domain);
static void mr_readmem_date (OR_BUF * buf, void *mem, TP_DOMAIN * domain,
			     int size);
static void mr_initval_date (DB_VALUE * value, int precision, int scale);
static int mr_setval_date (DB_VALUE * dest, DB_VALUE * src, bool copy);
static int mr_writeval_date (OR_BUF * buf, DB_VALUE * value);
static int mr_readval_date (OR_BUF * buf, DB_VALUE * value,
			    TP_DOMAIN * domain, int size, bool copy,
			    char *copy_buf, int copy_buf_len);
static int mr_cmpdisk_date (void *mem1, void *mem2, TP_DOMAIN * domain,
			    int do_reverse, int do_coercion, int total_order,
			    int *start_colp);
static int mr_cmpval_date (DB_VALUE * value1, DB_VALUE * value2,
			   TP_DOMAIN * domain, int do_reverse,
			   int do_coercion, int total_order, int *start_colp);
static void mr_null_oid (OID * oid);
static void mr_initmem_object (void *memptr);
static void mr_initval_object (DB_VALUE * value, int precision, int scale);
static int mr_setmem_object (void *memptr, TP_DOMAIN * domain,
			     DB_VALUE * value);
static int mr_getmem_object (void *memptr, TP_DOMAIN * domain,
			     DB_VALUE * value, bool copy);
static int mr_setval_object (DB_VALUE * dest, DB_VALUE * src, bool copy);
static int mr_lengthval_object (DB_VALUE * value, int disk);
static void mr_writemem_object (OR_BUF * buf, void *memptr,
				TP_DOMAIN * domain);
static void mr_readmem_object (OR_BUF * buf, void *memptr, TP_DOMAIN * domain,
			       int size);
static int mr_writeval_object (OR_BUF * buf, DB_VALUE * value);
static int mr_readval_object (OR_BUF * buf, DB_VALUE * value,
			      TP_DOMAIN * domain, int size, bool copy,
			      char *copy_buf, int copy_buf_len);
static int mr_cmpdisk_object (void *mem1, void *mem2, TP_DOMAIN * domain,
			      int do_reverse, int do_coercion,
			      int total_order, int *start_colp);
static int mr_cmpval_object (DB_VALUE * value1, DB_VALUE * value2,
			     TP_DOMAIN * domain, int do_reverse,
			     int do_coercion, int total_order,
			     int *start_colp);
static void mr_initmem_elo (void *memptr);
static void mr_initval_elo (DB_VALUE * value, int precision, int scale);
static int mr_setmem_elo (void *memptr, TP_DOMAIN * domain, DB_VALUE * value);
static int mr_getmem_elo (void *memptr, TP_DOMAIN * domain,
			  DB_VALUE * value, bool copy);
static int mr_setval_elo (DB_VALUE * dest, DB_VALUE * src, bool copy);
static int mr_lengthmem_elo (void *memptr, TP_DOMAIN * domain, int disk);
static int mr_lengthval_elo (DB_VALUE * value, int disk);
static void mr_writemem_elo (OR_BUF * buf, void *memptr, TP_DOMAIN * domain);
static void mr_readmem_elo (OR_BUF * buf, void *memptr, TP_DOMAIN * domain,
			    int size);
static int mr_writeval_elo (OR_BUF * buf, DB_VALUE * value);
static int mr_readval_elo (OR_BUF * buf, DB_VALUE * value, TP_DOMAIN * domain,
			   int size, bool copy, char *copy_buf,
			   int copy_buf_len);
static void mr_freemem_elo (void *memptr);
static int mr_cmpdisk_elo (void *mem1, void *mem2, TP_DOMAIN * domain,
			   int do_reverse, int do_coercion, int total_order,
			   int *start_colp);
static int mr_cmpval_elo (DB_VALUE * value1, DB_VALUE * value2,
			  TP_DOMAIN * domain, int do_reverse, int do_coercion,
			  int total_order, int *start_colp);
static void mr_initval_variable (DB_VALUE * value, int precision, int scale);
static int mr_setval_variable (DB_VALUE * dest, DB_VALUE * src, bool copy);
static int mr_lengthval_variable (DB_VALUE * value, int disk);
static int mr_writeval_variable (OR_BUF * buf, DB_VALUE * value);
static int mr_readval_variable (OR_BUF * buf, DB_VALUE * value,
				TP_DOMAIN * domain, int size, bool copy,
				char *copy_buf, int copy_buf_len);
static int mr_cmpdisk_variable (void *mem1, void *mem2, TP_DOMAIN * domain,
				int do_reverse, int do_coercion,
				int total_order, int *start_colp);
static int mr_cmpval_variable (DB_VALUE * value1, DB_VALUE * value2,
			       TP_DOMAIN * domain, int do_reverse,
			       int do_coercion, int total_order,
			       int *start_colp);
static void mr_initmem_sub (void *mem);
static void mr_initval_sub (DB_VALUE * value, int precision, int scale);
static int mr_setmem_sub (void *mem, TP_DOMAIN * domain, DB_VALUE * value);
static int mr_getmem_sub (void *mem, TP_DOMAIN * domain, DB_VALUE * value,
			  bool copy);
static int mr_setval_sub (DB_VALUE * dest, DB_VALUE * src, bool copy);
static int mr_lengthmem_sub (void *mem, TP_DOMAIN * domain, int disk);
static int mr_lengthval_sub (DB_VALUE * value, int disk);
static void mr_writemem_sub (OR_BUF * buf, void *mem, TP_DOMAIN * domain);
static void mr_readmem_sub (OR_BUF * buf, void *mem, TP_DOMAIN * domain,
			    int size);
static int mr_writeval_sub (OR_BUF * buf, DB_VALUE * value);
static int mr_readval_sub (OR_BUF * buf, DB_VALUE * value, TP_DOMAIN * domain,
			   int size, bool copy, char *copy_buf,
			   int copy_buf_len);
static int mr_cmpdisk_sub (void *mem1, void *mem2, TP_DOMAIN * domain,
			   int do_reverse, int do_coercion, int total_order,
			   int *start_colp);
static int mr_cmpval_sub (DB_VALUE * value1, DB_VALUE * value2,
			  TP_DOMAIN * domain, int do_reverse, int do_coercion,
			  int total_order, int *start_colp);
static void mr_initmem_ptr (void *memptr);
static void mr_initval_ptr (DB_VALUE * value, int precision, int scale);
static int mr_setmem_ptr (void *memptr, TP_DOMAIN * domain, DB_VALUE * value);
static int mr_getmem_ptr (void *memptr, TP_DOMAIN * domain,
			  DB_VALUE * value, bool copy);
static int mr_setval_ptr (DB_VALUE * dest, DB_VALUE * src, bool copy);
static int mr_lengthmem_ptr (void *memptr, TP_DOMAIN * domain, int disk);
static int mr_lengthval_ptr (DB_VALUE * value, int disk);
static void mr_writemem_ptr (OR_BUF * buf, void *memptr, TP_DOMAIN * domain);
static void mr_readmem_ptr (OR_BUF * buf, void *memptr, TP_DOMAIN * domain,
			    int size);
static int mr_writeval_ptr (OR_BUF * buf, DB_VALUE * value);
static int mr_readval_ptr (OR_BUF * buf, DB_VALUE * value, TP_DOMAIN * domain,
			   int size, bool copy, char *copy_buf,
			   int copy_buf_len);
static int mr_cmpdisk_ptr (void *mem1, void *mem2, TP_DOMAIN * domain,
			   int do_reverse, int do_coercion, int total_order,
			   int *start_colp);
static int mr_cmpval_ptr (DB_VALUE * value1, DB_VALUE * value2,
			  TP_DOMAIN * domain, int do_reverse, int do_coercion,
			  int total_order, int *start_colp);
static void mr_initmem_error (void *memptr);
static void mr_initval_error (DB_VALUE * value, int precision, int scale);
static int mr_setmem_error (void *memptr, TP_DOMAIN * domain,
			    DB_VALUE * value);
static int mr_getmem_error (void *memptr, TP_DOMAIN * domain,
			    DB_VALUE * value, bool copy);
static int mr_setval_error (DB_VALUE * dest, DB_VALUE * src, bool copy);
static int mr_lengthmem_error (void *memptr, TP_DOMAIN * domain, int disk);
static int mr_lengthval_error (DB_VALUE * value, int disk);
static void mr_writemem_error (OR_BUF * buf, void *memptr,
			       TP_DOMAIN * domain);
static void mr_readmem_error (OR_BUF * buf, void *memptr, TP_DOMAIN * domain,
			      int size);
static int mr_writeval_error (OR_BUF * buf, DB_VALUE * value);
static int mr_readval_error (OR_BUF * buf, DB_VALUE * value,
			     TP_DOMAIN * domain, int size, bool copy,
			     char *copy_buf, int copy_buf_len);
static int mr_cmpdisk_error (void *mem1, void *mem2, TP_DOMAIN * domain,
			     int do_reverse, int do_coercion, int total_order,
			     int *start_colp);
static int mr_cmpval_error (DB_VALUE * value1, DB_VALUE * value2,
			    TP_DOMAIN * domain, int do_reverse,
			    int do_coercion, int total_order,
			    int *start_colp);
static void mr_initmem_oid (void *memptr);
static void mr_initval_oid (DB_VALUE * value, int precision, int scale);
static int mr_setmem_oid (void *memptr, TP_DOMAIN * domain, DB_VALUE * value);
static int mr_getmem_oid (void *memptr, TP_DOMAIN * domain,
			  DB_VALUE * value, bool copy);
static int mr_setval_oid (DB_VALUE * dest, DB_VALUE * src, bool copy);
static void mr_writemem_oid (OR_BUF * buf, void *memptr, TP_DOMAIN * domain);
static void mr_readmem_oid (OR_BUF * buf, void *memptr, TP_DOMAIN * domain,
			    int size);
static int mr_writeval_oid (OR_BUF * buf, DB_VALUE * value);
static int mr_readval_oid (OR_BUF * buf, DB_VALUE * value, TP_DOMAIN * domain,
			   int size, bool copy, char *copy_buf,
			   int copy_buf_len);
static int mr_cmpdisk_oid (void *mem1, void *mem2, TP_DOMAIN * domain,
			   int do_reverse, int do_coercion, int total_order,
			   int *start_colp);
static int mr_cmpval_oid (DB_VALUE * value1, DB_VALUE * value2,
			  TP_DOMAIN * domain, int do_reverse, int do_coercion,
			  int total_order, int *start_colp);
static void mr_initmem_set (void *memptr);
static void mr_initval_set (DB_VALUE * value, int precision, int scale);
static int mr_setmem_set (void *memptr, TP_DOMAIN * domain, DB_VALUE * value);
static int mr_getmem_set (void *memptr, TP_DOMAIN * domain,
			  DB_VALUE * value, bool copy);
static int mr_setval_set_internal (DB_VALUE * dest, DB_VALUE * src,
				   bool copy, DB_TYPE set_type);
static int mr_setval_set (DB_VALUE * dest, DB_VALUE * src, bool copy);
static int mr_lengthmem_set (void *memptr, TP_DOMAIN * domain, int disk);
static int mr_lengthval_set (DB_VALUE * value, int disk);
static void mr_writemem_set (OR_BUF * buf, void *memptr, TP_DOMAIN * domain);
static int mr_writeval_set (OR_BUF * buf, DB_VALUE * value);
static void mr_readmem_set (OR_BUF * buf, void *memptr, TP_DOMAIN * domain,
			    int size);
static int mr_readval_set (OR_BUF * buf, DB_VALUE * value, TP_DOMAIN * domain,
			   int size, bool copy, char *copy_buf,
			   int copy_buf_len);
static void mr_freemem_set (void *memptr);
static int mr_cmpdisk_set (void *mem1, void *mem2, TP_DOMAIN * domain,
			   int do_reverse, int do_coercion, int total_order,
			   int *start_colp);
static int mr_cmpval_set (DB_VALUE * value1, DB_VALUE * value2,
			  TP_DOMAIN * domain, int do_reverse, int do_coercion,
			  int total_order, int *start_colp);
static void mr_initval_multiset (DB_VALUE * value, int precision, int scale);
static int mr_getmem_multiset (void *memptr, TP_DOMAIN * domain,
			       DB_VALUE * value, bool copy);
static int mr_setval_multiset (DB_VALUE * dest, DB_VALUE * src, bool copy);
static void mr_initval_sequence (DB_VALUE * value, int precision, int scale);
static int mr_getmem_sequence (void *memptr, TP_DOMAIN * domain,
			       DB_VALUE * value, bool copy);
static int mr_setval_sequence (DB_VALUE * dest, DB_VALUE * src, bool copy);
static int mr_cmpdisk_sequence (void *mem1, void *mem2, TP_DOMAIN * domain,
				int do_reverse, int do_coercion,
				int total_order, int *start_colp);
static int mr_cmpval_sequence (DB_VALUE * value1, DB_VALUE * value2,
			       TP_DOMAIN * domain, int do_reverse,
			       int do_coercion, int total_order,
			       int *start_colp);
static void mr_initval_midxkey (DB_VALUE * value, int precision, int scale);
static int mr_setval_midxkey (DB_VALUE * dest, DB_VALUE * src, bool copy);
static int mr_writeval_midxkey (OR_BUF * buf, DB_VALUE * value);
static int mr_readval_midxkey (OR_BUF * buf, DB_VALUE * value,
			       TP_DOMAIN * domain, int size, bool copy,
			       char *copy_buf, int copy_buf_len);
static int compare_diskvalue (char *mem1, char *mem2, TP_DOMAIN * dom1,
			      TP_DOMAIN * dom2, int do_coercion,
			      int total_order);
static int compare_midxkey (DB_MIDXKEY * mul1, DB_MIDXKEY * mul2,
			    TP_DOMAIN * domain, int do_reverse,
			    int do_coercion, int total_order,
			    int *start_colp);
static int mr_cmpval_midxkey (DB_VALUE * value1, DB_VALUE * value2,
			      TP_DOMAIN * domain, int do_reverse,
			      int do_coercion, int total_order,
			      int *start_colp);
static int mr_cmpdisk_midxkey (void *mem1, void *mem2, TP_DOMAIN * domain,
			       int do_reverse, int do_coercion,
			       int total_order, int *start_colp);
static int mr_lengthmem_midxkey (void *memptr, TP_DOMAIN * domain, int disk);
static int mr_lengthval_midxkey (DB_VALUE * value, int disk);
static void mr_initval_vobj (DB_VALUE * value, int precision, int scale);
static int mr_setval_vobj (DB_VALUE * dest, DB_VALUE * src, bool copy);
static int mr_readval_vobj (OR_BUF * buf, DB_VALUE * value,
			    TP_DOMAIN * domain, int size, bool copy,
			    char *copy_buf, int copy_buf_len);
static int mr_cmpdisk_vobj (void *mem1, void *mem2, TP_DOMAIN * domain,
			    int do_reverse, int do_coercion, int total_order,
			    int *start_colp);
static int mr_cmpval_vobj (DB_VALUE * value1, DB_VALUE * value2,
			   TP_DOMAIN * domain, int do_reverse,
			   int do_coercion, int total_order, int *start_colp);
static void mr_initmem_numeric (void *memptr);
static int mr_setmem_numeric (void *mem, TP_DOMAIN * domain,
			      DB_VALUE * value);
static int mr_getmem_numeric (void *mem, TP_DOMAIN * domain,
			      DB_VALUE * value, bool copy);
static void mr_writemem_numeric (OR_BUF * buf, void *mem, TP_DOMAIN * domain);
static void mr_readmem_numeric (OR_BUF * buf, void *mem, TP_DOMAIN * domain,
				int size);
static int mr_lengthmem_numeric (void *mem, TP_DOMAIN * domain, int disk);
static void mr_initval_numeric (DB_VALUE * value, int precision, int scale);
static int mr_setval_numeric (DB_VALUE * dest, DB_VALUE * src, bool copy);
static int mr_lengthval_numeric (DB_VALUE * value, int disk);
static int mr_writeval_numeric (OR_BUF * buf, DB_VALUE * value);
static int mr_readval_numeric (OR_BUF * buf, DB_VALUE * value,
			       TP_DOMAIN * domain, int size, bool copy,
			       char *copy_buf, int copy_buf_len);
static int mr_cmpdisk_numeric (void *mem1, void *mem2, TP_DOMAIN * domain,
			       int do_reverse, int do_coercion,
			       int total_order, int *start_colp);
static int mr_cmpval_numeric (DB_VALUE * value1, DB_VALUE * value2,
			      TP_DOMAIN * domain, int do_reverse,
			      int do_coercion, int total_order,
			      int *start_colp);
static void pr_init_ordered_mem_sizes (void);
static void mr_initmem_resultset (void *mem);
static int mr_setmem_resultset (void *mem, TP_DOMAIN * domain,
				DB_VALUE * value);
static int mr_getmem_resultset (void *mem, TP_DOMAIN * domain,
				DB_VALUE * value, bool copy);
static void mr_writemem_resultset (OR_BUF * buf, void *mem,
				   TP_DOMAIN * domain);
static void mr_readmem_resultset (OR_BUF * buf, void *mem, TP_DOMAIN * domain,
				  int size);
static void mr_initval_resultset (DB_VALUE * value, int precision, int scale);
static int mr_setval_resultset (DB_VALUE * dest, DB_VALUE * src, bool copy);
static int mr_writeval_resultset (OR_BUF * buf, DB_VALUE * value);
static int mr_readval_resultset (OR_BUF * buf, DB_VALUE * value,
				 TP_DOMAIN * domain, int size, bool copy,
				 char *copy_buf, int copy_buf_len);
static int mr_cmpdisk_resultset (void *mem1, void *mem2, TP_DOMAIN * domain,
				 int do_reverse, int do_coercion,
				 int total_order, int *start_colp);
static int mr_cmpval_resultset (DB_VALUE * value1, DB_VALUE * value2,
				TP_DOMAIN * domain, int do_reverse,
				int do_coercion, int total_order,
				int *start_colp);
/*
 * Value_area
 *    Area used for allocation of value containers that may be given out
 *    to database applications.
 */
static AREA *Value_area = NULL;

int pr_Inhibit_oid_promotion = PR_INHIBIT_OID_PROMOTION_DEFAULT;
/* The sizes of the primitive types in descending order */
int pr_ordered_mem_sizes[PR_TYPE_TOTAL];
/* The number of items in pr_ordered_mem_sizes */
int pr_ordered_mem_size_total = 0;

PR_TYPE tp_Null = {
  "*NULL*", DB_TYPE_NULL, 0, 0, 0, 0,
  help_fprint_value,
  help_sprint_value,
  mr_initmem_null,
  mr_initval_null,
  mr_setmem_null,
  mr_getmem_null,
  mr_setval_null,
  NULL, NULL,
  mr_writemem_null,
  mr_readmem_null,
  mr_writeval_null,
  mr_readval_null,
  NULL,
  mr_cmpdisk_null,
  mr_cmpval_null
};

PR_TYPE *tp_Type_null = &tp_Null;

PR_TYPE tp_Integer = {
  "integer", DB_TYPE_INTEGER, 0, sizeof (int), sizeof (int), 4,
  help_fprint_value,
  help_sprint_value,
  mr_initmem_int,
  mr_initval_int,
  mr_setmem_int,
  mr_getmem_int,
  mr_setval_int,
  NULL, NULL,
  mr_writemem_int,
  mr_readmem_int,
  mr_writeval_int,
  mr_readval_int,
  NULL,
  mr_cmpdisk_int,
  mr_cmpval_int
};

PR_TYPE *tp_Type_integer = &tp_Integer;

PR_TYPE tp_Short = {
  "smallint", DB_TYPE_SHORT, 0, sizeof (short), sizeof (short), 2,
  help_fprint_value,
  help_sprint_value,
  mr_initmem_short,
  mr_initval_short,
  mr_setmem_short,
  mr_getmem_short,
  mr_setval_short,
  NULL, NULL,
  mr_writemem_short,
  mr_readmem_short,
  mr_writeval_short,
  mr_readval_short,
  NULL,
  mr_cmpdisk_short,
  mr_cmpval_short
};

PR_TYPE *tp_Type_short = &tp_Short;

PR_TYPE tp_Float = {
  "float", DB_TYPE_FLOAT, 0, sizeof (float), sizeof (float), 4,
  help_fprint_value,
  help_sprint_value,
  mr_initmem_float,
  mr_initval_float,
  mr_setmem_float,
  mr_getmem_float,
  mr_setval_float,
  NULL, NULL,
  mr_writemem_float,
  mr_readmem_float,
  mr_writeval_float,
  mr_readval_float,
  NULL,
  mr_cmpdisk_float,
  mr_cmpval_float
};

PR_TYPE *tp_Type_float = &tp_Float;

PR_TYPE tp_Double = {
  "double", DB_TYPE_DOUBLE, 0, sizeof (double), sizeof (double), 8,
  help_fprint_value,
  help_sprint_value,
  mr_initmem_double,
  mr_initval_double,
  mr_setmem_double,
  mr_getmem_double,
  mr_setval_double,
  NULL, NULL,
  mr_writemem_double,
  mr_readmem_double,
  mr_writeval_double,
  mr_readval_double,
  NULL,
  mr_cmpdisk_double,
  mr_cmpval_double
};

PR_TYPE *tp_Type_double = &tp_Double;

PR_TYPE tp_Time = {
  "time", DB_TYPE_TIME, 0, sizeof (DB_TIME), OR_TIME_SIZE, 4,
  help_fprint_value,
  help_sprint_value,
  mr_initmem_time,
  mr_initval_time,
  mr_setmem_time,
  mr_getmem_time,
  mr_setval_time,
  NULL, NULL,
  mr_writemem_time,
  mr_readmem_time,
  mr_writeval_time,
  mr_readval_time,
  NULL,
  mr_cmpdisk_time,
  mr_cmpval_time
};

PR_TYPE *tp_Type_time = &tp_Time;

PR_TYPE tp_Utime = {
  "timestamp", DB_TYPE_UTIME, 0, sizeof (DB_UTIME), OR_UTIME_SIZE, 4,
  help_fprint_value,
  help_sprint_value,
  mr_initmem_utime,
  mr_initval_utime,
  mr_setmem_utime,
  mr_getmem_utime,
  mr_setval_utime,
  NULL, NULL,
  mr_writemem_utime,
  mr_readmem_utime,
  mr_writeval_utime,
  mr_readval_utime,
  NULL,
  mr_cmpdisk_utime,
  mr_cmpval_utime
};

PR_TYPE *tp_Type_utime = &tp_Utime;

PR_TYPE tp_Monetary = {
  "monetary", DB_TYPE_MONETARY, 0, sizeof (DB_MONETARY), OR_MONETARY_SIZE, 8,
  help_fprint_value,
  help_sprint_value,
  mr_initmem_money,
  mr_initval_money,
  mr_setmem_money,
  mr_getmem_money,
  mr_setval_money,
  NULL, NULL,
  mr_writemem_money,
  mr_readmem_money,
  mr_writeval_money,
  mr_readval_money,
  NULL,
  mr_cmpdisk_money,
  mr_cmpval_money
};

PR_TYPE *tp_Type_monetary = &tp_Monetary;

PR_TYPE tp_Date = {
  "date", DB_TYPE_DATE, 0, sizeof (DB_DATE), OR_DATE_SIZE, 4,
  help_fprint_value,
  help_sprint_value,
  mr_initmem_date,
  mr_initval_date,
  mr_setmem_date,
  mr_getmem_date,
  mr_setval_date,
  NULL, NULL,
  mr_writemem_date,
  mr_readmem_date,
  mr_writeval_date,
  mr_readval_date,
  NULL,
  mr_cmpdisk_date,
  mr_cmpval_date
};

PR_TYPE *tp_Type_date = &tp_Date;

/*
 * tp_Object
 *
 * ALERT!!! We set the alignment for OIDs to 8 even though they only have an
 * int and two shorts.  This is done because the WS_MEMOID has a pointer
 * following the OID and it must be on an 8 byte boundary for the Alpha boxes.
 */

PR_TYPE tp_Object = {
  "object", DB_TYPE_OBJECT, 0, MR_OID_SIZE, OR_OID_SIZE, 8,
  help_fprint_value,
  help_sprint_value,
  mr_initmem_object,
  mr_initval_object,
  mr_setmem_object,
  mr_getmem_object,
  mr_setval_object,
  NULL,
  mr_lengthval_object,
  mr_writemem_object,
  mr_readmem_object,
  mr_writeval_object,
  mr_readval_object,
  NULL,
  mr_cmpdisk_object,
  mr_cmpval_object
};

PR_TYPE *tp_Type_object = &tp_Object;

PR_TYPE tp_Elo = {
  "*elo*", DB_TYPE_ELO, 1, sizeof (ELO *), 0, 4,
  help_fprint_value,
  help_sprint_value,
  mr_initmem_elo,
  mr_initval_elo,
  mr_setmem_elo,
  mr_getmem_elo,
  mr_setval_elo,
  mr_lengthmem_elo,
  mr_lengthval_elo,
  mr_writemem_elo,
  mr_readmem_elo,
  mr_writeval_elo,
  mr_readval_elo,
  mr_freemem_elo,
  mr_cmpdisk_elo,
  mr_cmpval_elo
};

PR_TYPE *tp_Type_elo = &tp_Elo;

PR_TYPE tp_Variable = {
  "*variable*", DB_TYPE_VARIABLE, 1, sizeof (DB_VALUE), 0, 4,
  help_fprint_value,
  help_sprint_value,
  NULL,
  mr_initval_variable,
  NULL,
  NULL,
  mr_setval_variable,
  NULL,
  mr_lengthval_variable,
  NULL,
  NULL,
  mr_writeval_variable,
  mr_readval_variable,
  NULL,
  mr_cmpdisk_variable,
  mr_cmpval_variable
};

PR_TYPE *tp_Type_variable = &tp_Variable;

PR_TYPE tp_Substructure = {
  "*substructure*", DB_TYPE_SUB, 1, sizeof (void *), 0, 8,
  help_fprint_value,
  help_sprint_value,
  mr_initmem_sub,
  mr_initval_sub,
  mr_setmem_sub,
  mr_getmem_sub,
  mr_setval_sub,
  mr_lengthmem_sub,
  mr_lengthval_sub,
  mr_writemem_sub,
  mr_readmem_sub,
  mr_writeval_sub,
  mr_readval_sub,
  NULL,
  mr_cmpdisk_sub,
  mr_cmpval_sub
};

PR_TYPE *tp_Type_substructure = &tp_Substructure;

PR_TYPE tp_Pointer = {
  "*pointer*", DB_TYPE_POINTER, 0, sizeof (void *), 0, 4,
  help_fprint_value,
  help_sprint_value,
  mr_initmem_ptr,
  mr_initval_ptr,
  mr_setmem_ptr,
  mr_getmem_ptr,
  mr_setval_ptr,
  mr_lengthmem_ptr,
  mr_lengthval_ptr,
  mr_writemem_ptr,
  mr_readmem_ptr,
  mr_writeval_ptr,
  mr_readval_ptr,
  NULL,
  mr_cmpdisk_ptr,
  mr_cmpval_ptr
};

PR_TYPE *tp_Type_pointer = &tp_Pointer;

PR_TYPE tp_Error = {
  "*error*", DB_TYPE_ERROR, 0, sizeof (int), 0, 4,
  help_fprint_value,
  help_sprint_value,
  mr_initmem_error,
  mr_initval_error,
  mr_setmem_error,
  mr_getmem_error,
  mr_setval_error,
  mr_lengthmem_error,
  mr_lengthval_error,
  mr_writemem_error,
  mr_readmem_error,
  mr_writeval_error,
  mr_readval_error,
  NULL,
  mr_cmpdisk_error,
  mr_cmpval_error
};

PR_TYPE *tp_Type_error = &tp_Error;

/*
 * tp_Oid
 *
 * ALERT!!! We set the alignment for OIDs to 8 even though they only have an
 * int and two shorts.  This is done because the WS_MEMOID has a pointer
 * following the OID and it must be on an 8 byte boundary for the Alpha boxes.
 */
PR_TYPE tp_Oid = {
  "*oid*", DB_TYPE_OID, 0, sizeof (OID), OR_OID_SIZE, 8,
  help_fprint_value,
  help_sprint_value,
  mr_initmem_oid,
  mr_initval_oid,
  mr_setmem_oid,
  mr_getmem_oid,
  mr_setval_oid,
  NULL, NULL,
  mr_writemem_oid,
  mr_readmem_oid,
  mr_writeval_oid,
  mr_readval_oid,
  NULL,
  mr_cmpdisk_oid,
  mr_cmpval_oid
};

PR_TYPE *tp_Type_oid = &tp_Oid;

PR_TYPE tp_Set = {
  "set", DB_TYPE_SET, 1, sizeof (SETOBJ *), 0, 4,
  help_fprint_value,
  help_sprint_value,
  mr_initmem_set,
  mr_initval_set,
  mr_setmem_set,
  mr_getmem_set,
  mr_setval_set,
  mr_lengthmem_set,
  mr_lengthval_set,
  mr_writemem_set,
  mr_readmem_set,
  mr_writeval_set,
  mr_readval_set,
  mr_freemem_set,
  mr_cmpdisk_set,
  mr_cmpval_set
};

PR_TYPE *tp_Type_set = &tp_Set;

PR_TYPE tp_Multiset = {
  "multiset", DB_TYPE_MULTISET, 1, sizeof (SETOBJ *), 0, 4,
  help_fprint_value,
  help_sprint_value,
  mr_initmem_set,
  mr_initval_multiset,
  mr_setmem_set,
  mr_getmem_multiset,
  mr_setval_multiset,
  mr_lengthmem_set,
  mr_lengthval_set,
  mr_writemem_set,
  mr_readmem_set,
  mr_writeval_set,
  mr_readval_set,
  mr_freemem_set,
  mr_cmpdisk_set,
  mr_cmpval_set
};

PR_TYPE *tp_Type_multiset = &tp_Multiset;

PR_TYPE tp_Sequence = {
  "sequence", DB_TYPE_SEQUENCE, 1, sizeof (SETOBJ *), 0, 4,
  help_fprint_value,
  help_sprint_value,
  mr_initmem_set,
  mr_initval_sequence,
  mr_setmem_set,
  mr_getmem_sequence,
  mr_setval_sequence,
  mr_lengthmem_set,
  mr_lengthval_set,
  mr_writemem_set,
  mr_readmem_set,
  mr_writeval_set,
  mr_readval_set,
  mr_freemem_set,
  mr_cmpdisk_sequence,
  mr_cmpval_sequence
};

PR_TYPE *tp_Type_sequence = &tp_Sequence;

PR_TYPE tp_Midxkey = {
  "midxkey", DB_TYPE_MIDXKEY, 1, 0, 0, 4,
  help_fprint_value,
  help_sprint_value,
  NULL,				/* mr_initmem_set,              */
  mr_initval_midxkey,
  NULL,				/* mr_setmem_set,               */
  NULL,				/* mr_getmem_midxkey,  */
  mr_setval_midxkey,
  mr_lengthmem_midxkey,
  mr_lengthval_midxkey,
  NULL,				/* mr_writemem_set,             */
  NULL,				/* mr_readmem_set,              */
  mr_writeval_midxkey,
  mr_readval_midxkey,
  NULL,				/* mr_freemem_set,              */
  mr_cmpdisk_midxkey,
  mr_cmpval_midxkey
};

PR_TYPE *tp_Type_midxkey = &tp_Midxkey;

PR_TYPE tp_Vobj = {
  "*vobj*", DB_TYPE_VOBJ, 1, sizeof (SETOBJ *), 0, 8,
  help_fprint_value,
  help_sprint_value,
  mr_initmem_set,
  mr_initval_vobj,
  mr_setmem_set,
  mr_getmem_sequence,
  mr_setval_vobj,
  mr_lengthmem_set,
  mr_lengthval_set,
  mr_writemem_set,
  mr_readmem_set,
  mr_writeval_set,
  mr_readval_vobj,
  mr_freemem_set,
  mr_cmpdisk_vobj,
  mr_cmpval_vobj
};

PR_TYPE *tp_Type_vobj = &tp_Vobj;

PR_TYPE tp_Numeric = {
  "numeric", DB_TYPE_NUMERIC, 0, 0, 0, 1,
  help_fprint_value,
  help_sprint_value,
  mr_initmem_numeric,
  mr_initval_numeric,
  mr_setmem_numeric,
  mr_getmem_numeric,
  mr_setval_numeric,
  mr_lengthmem_numeric,
  mr_lengthval_numeric,
  mr_writemem_numeric,
  mr_readmem_numeric,
  mr_writeval_numeric,
  mr_readval_numeric,
  NULL,
  mr_cmpdisk_numeric,
  mr_cmpval_numeric
};

PR_TYPE *tp_Type_numeric = &tp_Numeric;

/*
 * tp_Type_id_map
 *    This quickly maps a type identifier to a type structure.
 *    This is dependent on the ordering of the DB_TYPE union so take
 *    care when modifying either of these.  It would be safer to build
 *    this at run time.
 */
PR_TYPE *tp_Type_id_map[] = {
  &tp_Null,
  &tp_Integer,
  &tp_Float,
  &tp_Double,
  &tp_String,
  &tp_Object,
  &tp_Set,
  &tp_Multiset,
  &tp_Sequence,
  &tp_Elo,
  &tp_Time,
  &tp_Utime,
  &tp_Date,
  &tp_Monetary,
  &tp_Variable,
  &tp_Substructure,
  &tp_Pointer,
  &tp_Error,
  &tp_Short,
  &tp_Vobj,
  &tp_Oid,
  &tp_Null,			/* place holder for DB_TYPE_DB_VALUE */
  &tp_Numeric,
  &tp_Bit,
  &tp_VarBit,
  &tp_Char,
  &tp_NChar,
  &tp_VarNChar,
  &tp_ResultSet,
  &tp_Midxkey,
  &tp_Null
};

PR_TYPE tp_ResultSet = {
  "resultset", DB_TYPE_RESULTSET, 0, sizeof (DB_RESULTSET),
  sizeof (DB_RESULTSET), 4,
  help_fprint_value,
  help_sprint_value,
  mr_initmem_resultset,
  mr_initval_resultset,
  mr_setmem_resultset,
  mr_getmem_resultset,
  mr_setval_resultset,
  NULL, NULL,
  mr_writemem_resultset,
  mr_readmem_resultset,
  mr_writeval_resultset,
  mr_readval_resultset,
  NULL,
  mr_cmpdisk_resultset,
  mr_cmpval_resultset
};

PR_TYPE *tp_Type_resultset = &tp_ResultSet;

/*
 * DB_VALUE MAINTENANCE
 */
/*
 * There are two types of DB_VALUEs that are identical in contents but
 * different in the way they are treated by the garbage collector.
 * An "internal" DB_VALUE is allocated within the workspace and is used by
 * the structures
 * that represent instances objects and class objects.  Since it is allocated
 * within the workspace, it will not serve as a root for the garbage collector.
 * This is necessary so the garbage collector will not keep objects that
 * reference each other within the workspace but have no external references
 * from the application memory space.
 *
 * An "external" DB_VALUE is allocated outside the workspace in a seperate
 * area called the Value_area.  Allocation areas such as this are not
 * considered part of the workspace (although they are also managed by the
 * workspace manager).  These allocation areas are allocated using the
 * normal OS heap allocator (malloc) but are not registered as a weak
 * set for the garbage collector.  This will make the garbage collector
 * sweep through them looking for MOP pointers.  External DB_VALUEs are
 * used when a value container is passed out of the database interface
 * layer and can be placed in a structure or on the stack of the application.
 * Since references to these values is now beyond the control of the
 * database, the garbage collector must recognize them in order to prevent
 * collecting the MOPs that may be in the DB_VALUEs.
 *
 * External DB_VALUEs are created only by the db_ layer when a value is
 * created through db_value_create or db_value_copy.  Usually, application
 * DB_VALUEs are statically allocated on the stack and therefore are
 * automatically seen by the garbage collector without any special preparation.
 * It is only when an application needs to allocate DB_VALUEs on the heap
 * that we have to be careful to create an external DB_VALUE.
 */



/*
 * pr_area_init - Initialize the area for allocation of value containers.
 *    return: void
 * Note:
 *    This must be called at a suitable point in system initialization.
 */
void
pr_area_init (void)
{
  Value_area = area_create ("Value containers", sizeof (DB_VALUE),
			    VALUE_AREA_COUNT, false);
  pr_init_ordered_mem_sizes ();
}

/*
 * pr_make_value - create an internal value container
 *    return: new value container
 * Note:
 *    The value is allocated within the main workspace blocks so that
 *    it will not serve as a root for the garbage collector.
 */
DB_VALUE *
pr_make_value (void)
{
  DB_VALUE *value;

  value = db_private_alloc (NULL, sizeof (DB_VALUE));

  if (value != NULL)
    {
      db_value_domain_init (value, DB_TYPE_NULL, 0, 0);
    }

  return (value);
}


/*
 * pr_make_ext_value - reates an external value container suitable for passing
 * beyond the interface layer to application programs.
 *    return: new value container
 * Note:
 *    It is allocated in the special Value_area so that it will serve as
 *    a root to the garbage collector if the value contains a MOP.
 */
DB_VALUE *
pr_make_ext_value (void)
{
  DB_VALUE *value;

  value = (DB_VALUE *) area_alloc (Value_area);
  if (value != NULL)
    {
      db_value_domain_init (value, DB_TYPE_NULL, 0, 0);
    }
  return (value);
}


/*
 * pr_clear_value - clear an internal or external DB_VALUE and initialize
 * it to the NULL state.
 *    return: NO_ERROR if successful, error code otherwise
 *    value(in/out): value to initialize
 * Note:
 *    Any external allocations (strings, sets, etc) will be freed.
 *
 *    There is too much type specific stuff in here.  We need to create a
 *    "freeval" type vector function to do this work.
 */
int
pr_clear_value (DB_VALUE * value)
{
  int error = NO_ERROR;
  unsigned char *data;
  bool need_clear;
  DB_TYPE type;

  need_clear = (value != NULL) ? true : false;

  if (need_clear)
    {
      if (DB_IS_NULL (value))
	{
	  need_clear = false;
	  if (PRM_ORACLE_STYLE_EMPTY_STRING)
	    {
	      if (value->need_clear)
		{		/* need to check */
		  type = DB_VALUE_DOMAIN_TYPE (value);
		  if (QSTR_IS_ANY_CHAR_OR_BIT (type)
		      && value->data.ch.medium.buf != NULL)
		    {
		      need_clear = true;	/* need to free Empty-string */
		    }
		}
	    }
	}			/* if (DB_IS_NULL(value)) */
    }

  if (need_clear)
    {
      switch (DB_VALUE_DOMAIN_TYPE (value))
	{
	case DB_TYPE_OBJECT:
	  /* we need to be sure to NULL the object pointer so that this
	   * db_value does not cause garbage collection problems by
	   * retaining an object pointer.
	   */
	  value->data.op = NULL;
	  break;

	case DB_TYPE_SET:
	case DB_TYPE_MULTISET:
	case DB_TYPE_SEQUENCE:
	case DB_TYPE_VOBJ:
	  set_free (db_get_set (value));
	  value->data.set = NULL;
	  break;

	case DB_TYPE_MIDXKEY:
	  data = (unsigned char *) value->data.midxkey.buf;
	  if (data != NULL)
	    {
	      if (value->need_clear)
		{
		  db_private_free_and_init (NULL, data);
		}
	      /*
	       * Ack, phfffft!!! why should we have to know about the
	       * internals here?
	       */
	      value->data.midxkey.buf = NULL;
	    }
	  break;

	case DB_TYPE_VARCHAR:
	case DB_TYPE_CHAR:
	case DB_TYPE_NCHAR:
	case DB_TYPE_VARNCHAR:
	case DB_TYPE_BIT:
	case DB_TYPE_VARBIT:
	  data = (unsigned char *) value->data.ch.medium.buf;
	  if (data != NULL)
	    {
	      if (value->need_clear)
		{
		  db_private_free_and_init (NULL, data);
		}
	      /*
	       * Ack, phfffft!!! why should we have to know about the
	       * internals here?
	       */
	      value->data.ch.medium.buf = NULL;
	    }
	  break;

	case DB_TYPE_ELO:
	  /* Need to move some of the basic elo_ structure support in elo.c
	   * over to the server.
	   */
#if !defined (SERVER_MODE)
	  elo_free (db_get_elo (value));
	  value->data.elo = NULL;
#endif
	  break;

	default:
	  break;
	}

      /* always make sure the value gets cleared */
      PRIM_SET_NULL (value);
      value->need_clear = false;
    }

  return error;
}


/*
 * pr_free_value - free an internval value container any anything that it
 * references
 *    return: NO_ERROR if successful, error code otherwise
 *    value(in/out): value to clear & free
 */
int
pr_free_value (DB_VALUE * value)
{
  int error = NO_ERROR;

  if (value != NULL)
    {
      /* some redundant checking but I want the semantics isolated */
      error = pr_clear_value (value);
      db_private_free_and_init (NULL, value);
    }
  return (error);
}


/*
 * pr_free_ext_value - free an external value container and anything that it
 * references.
 *    return:
 *    value(in/out): external value to clear & free
 * Note:
 *    Identical to pr_free_value except that it frees the value to the
 * Value_area instead of to the workspace.
 */
int
pr_free_ext_value (DB_VALUE * value)
{
  int error = NO_ERROR;

  if (value != NULL)
    {
      /* some redundant checking but I wan't the semantics isolated */
      error = pr_clear_value (value);
      area_free (Value_area, (void *) value);
    }
  return (error);
}


/*
 * pr_clone_value - copy the contents of one value container to another.
 *    return: none
 *    src(in): source value
 *    dest(out): destination value
 * Note:
 *    For types that contain external allocations (like strings).
 *    The contents are copied.
 */
int
pr_clone_value (DB_VALUE * src, DB_VALUE * dest)
{
  PR_TYPE *type;
  DB_TYPE src_dbtype;

  if (dest != NULL)
    {
      if (src == NULL)
	{
	  db_make_null (dest);
	}
      else
	{
	  src_dbtype = DB_VALUE_DOMAIN_TYPE (src);

	  if (DB_IS_NULL (src))
	    db_value_domain_init (dest, src_dbtype,
				  DB_VALUE_PRECISION (src),
				  DB_VALUE_SCALE (src));

	  else if (src != dest)
	    {
	      type = PR_TYPE_FROM_ID (src_dbtype);
	      if (type != NULL)
		{
		  /*
		   * Formerly called "initval" here but that was removed as
		   * "setval" is supposed to properly initialize the
		   * destination domain.  No need to do it twice.
		   * Make sure the COPY flag is set in the setval call.
		   */
		  (*(type->setval)) (dest, src, true);
		}
	      else
		{
		  /*
		   * can only get here in error conditions, initialize to NULL
		   */
		  db_make_null (dest);
		}
	    }
	}
    }
  return NO_ERROR;
}


/*
 * pr_copy_value - This creates a new internal value container with a copy
 * of the contents of the source container.
 *    return: new value
 *    value(in): value to copy
 */
DB_VALUE *
pr_copy_value (DB_VALUE * value)
{
  DB_VALUE *new_ = NULL;

  if (value != NULL)
    {
      new_ = pr_make_value ();
      if (pr_clone_value (value, new_) != NO_ERROR)
	{
	  /*
	   * oh well, couldn't allocate storage for the clone.
	   * Note that pr_free_value can only return errors in the
	   * case where the value has been initialized so it won't
	   * stomp on the error code if we call it here
	   */
	  pr_free_value (new_);
	  new_ = NULL;
	}
    }
  return (new_);
}



/*
 * pr_share_value - This is used to copy the contents of one value
 * container to another WITHOUT introducing a new copy of any indirect
 * data (like strings or sets).
 *    return: new value
 *    src(in): source value
 *    dest(out): destination value
 * Note:
 *    However, everything is set up properly so that the dst value
 *    can follow the normal db_value_clear protocol.
 *
 *    WARNING: src will be valid only as long as dst is.  That is, after dst
 *    gets cleared, src will have dangling pointers unless it has also been
 *    cleared.  Use with care.
 */
int
pr_share_value (DB_VALUE * src, DB_VALUE * dst)
{
  if (src && dst && src != dst)
    {
      *dst = *src;
      dst->need_clear = false;
      if (pr_is_set_type (DB_VALUE_DOMAIN_TYPE (src)) && !DB_IS_NULL (src))
	{
	  /*
	   * This bites... isn't there a function for adding a
	   * reference to a set?
	   */
	  src->data.set->ref_count += 1;
	}
    }
  return NO_ERROR;
}

#if !defined (SERVER_MODE)
/*
 * pr_copy_string - copy string
 *    return: copied string
 *    str(in): string to copy
 */
static const char *
pr_copy_string (const char *str)
{
  const char *copy;

  copy = NULL;
  if (str != NULL)
    {
      copy = (const char *) db_private_alloc (NULL, strlen (str) + 1);
      if (copy != NULL)
	strcpy ((char *) copy, (char *) str);
    }

  return (copy);
}
#endif

/*
 * TYPE NULL
 */

/*
 * This is largely a placeholder type that does nothing except assert
 * that the size of a NULL value is zero.
 * The "mem" functions are no-ops since NULL is not a valid domain type.
 * The "value" functions don't do much, they just make sure the value
 * domain is initialized.
 *
 */

/*
 * mr_initmem_null - dummy function
 *    return:
 *    memptr():
 */
static void
mr_initmem_null (void *memptr)
{
}

/*
 * mr_setmem_null - dummy function
 *    return:
 *    memptr():
 *    domain():
 *    value():
 */
static int
mr_setmem_null (void *memptr, TP_DOMAIN * domain, DB_VALUE * value)
{
  return (NO_ERROR);
}

/*
 * mr_getmem_null - dummy function
 *    return:
 *    memptr():
 *    domain():
 *    value():
 *    copy():
 */
static int
mr_getmem_null (void *memptr, TP_DOMAIN * domain, DB_VALUE * value, bool copy)
{
  db_make_null (value);
  return NO_ERROR;
}

/*
 * mr_writemem_null - dummy function
 *    return:
 *    buf():
 *    memptr():
 *    domain():
 */
static void
mr_writemem_null (OR_BUF * buf, void *memptr, TP_DOMAIN * domain)
{
}

/*
 * mr_readmem_null - dummy function
 *    return:
 *    buf():
 *    memptr():
 *    domain():
 *    size():
 */
static void
mr_readmem_null (OR_BUF * buf, void *memptr, TP_DOMAIN * domain, int size)
{
}

static void
mr_initval_null (DB_VALUE * value, int precision, int scale)
{
  db_value_domain_init (value, DB_TYPE_NULL, precision, scale);
}

/*
 * mr_setval_null - dummy function
 *    return:
 *    dest():
 *    src():
 *    copy():
 */
static int
mr_setval_null (DB_VALUE * dest, DB_VALUE * src, bool copy)
{
  mr_initval_null (dest, 0, 0);
  return (NO_ERROR);
}

/*
 * mr_writeval_null - dummy function
 *    return:
 *    buf():
 *    value():
 */
static int
mr_writeval_null (OR_BUF * buf, DB_VALUE * value)
{
  return NO_ERROR;
}

/*
 * mr_readval_null - dummy function
 *    return:
 *    buf():
 *    value():
 *    domain():
 *    size():
 *    copy():
 *    copy_buf():
 *    copy_buf_len():
 */
static int
mr_readval_null (OR_BUF * buf, DB_VALUE * value,
		 TP_DOMAIN * domain, int size, bool copy,
		 char *copy_buf, int copy_buf_len)
{
  if (value)
    {
      db_make_null (value);
      value->need_clear = false;
    }
  return NO_ERROR;
}

/*
 * mr_cmpdisk_null - dummy function
 *    return:
 *    mem1():
 *    mem2():
 *    domain():
 *    do_reverse():
 *    do_coercion():
 *    total_order():
 *    start_colp():
 */
static int
mr_cmpdisk_null (void *mem1, void *mem2,
		 TP_DOMAIN * domain, int do_reverse,
		 int do_coercion, int total_order, int *start_colp)
{
  return DB_UNK;
}

/*
 * mr_cmpval_null - dummy function
 *    return:
 *    value1():
 *    value2():
 *    domain():
 *    do_reverse():
 *    do_coercion():
 *    total_order():
 *    start_colp():
 */
static int
mr_cmpval_null (DB_VALUE * value1, DB_VALUE * value2,
		TP_DOMAIN * domain, int do_reverse,
		int do_coercion, int total_order, int *start_colp)
{
  return DB_UNK;
}


/*
 * TYPE INTEGER
 *
 * Your basic 32 bit signed integral value.
 * At the storage level, we don't really care whether it is signed or unsigned.
 */

static void
mr_initmem_int (void *mem)
{
  *(int *) mem = 0;
}

static int
mr_setmem_int (void *mem, TP_DOMAIN * domain, DB_VALUE * value)
{
  if (value != NULL)
    *(int *) mem = db_get_int (value);
  else
    mr_initmem_int (mem);

  return NO_ERROR;
}

static int
mr_getmem_int (void *mem, TP_DOMAIN * domain, DB_VALUE * value, bool copy)
{
  return db_make_int (value, *(int *) mem);
}

static void
mr_writemem_int (OR_BUF * buf, void *mem, TP_DOMAIN * domain)
{
  or_put_int (buf, *(int *) mem);
}

static void
mr_readmem_int (OR_BUF * buf, void *mem, TP_DOMAIN * domain, int size)
{
  int rc = NO_ERROR;

  if (mem == NULL)
    {
      or_advance (buf, tp_Integer.disksize);
    }
  else
    {
      *(int *) mem = or_get_int (buf, &rc);
    }
}

static void
mr_initval_int (DB_VALUE * value, int precision, int scale)
{
  db_value_domain_init (value, DB_TYPE_INTEGER, precision, scale);
  db_make_int (value, 0);
}

static int
mr_setval_int (DB_VALUE * dest, DB_VALUE * src, bool copy)
{
  if (src && !DB_IS_NULL (src))
    return db_make_int (dest, db_get_int (src));
  else
    return db_value_domain_init (dest, DB_TYPE_INTEGER,
				 DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE);
}

static int
mr_writeval_int (OR_BUF * buf, DB_VALUE * value)
{
  return or_put_int (buf, db_get_int (value));
}

static int
mr_readval_int (OR_BUF * buf, DB_VALUE * value,
		TP_DOMAIN * domain, int size, bool copy,
		char *copy_buf, int copy_buf_len)
{
  int temp_int, rc = NO_ERROR;

  if (value == NULL)
    {
      rc = or_advance (buf, tp_Integer.disksize);
    }
  else
    {
      temp_int = or_get_int (buf, &rc);
      db_make_int (value, temp_int);
      value->need_clear = false;
    }
  return rc;
}

static int
mr_cmpdisk_int (void *mem1, void *mem2,
		TP_DOMAIN * domain, int do_reverse,
		int do_coercion, int total_order, int *start_colp)
{
  int i1, i2;

  i1 = OR_GET_INT (mem1);
  i2 = OR_GET_INT (mem2);

  return MR_CMP (i1, i2, do_reverse, domain);
}

static int
mr_cmpval_int (DB_VALUE * value1, DB_VALUE * value2,
	       TP_DOMAIN * domain, int do_reverse,
	       int do_coercion, int total_order, int *start_colp)
{
  int i1, i2;

  i1 = DB_GET_INT (value1);
  i2 = DB_GET_INT (value2);

  return MR_CMP (i1, i2, do_reverse, domain);
}

/*
 * TYPE SHORT
 *
 * Your basic 16 bit signed integral value.
 */

static void
mr_initmem_short (void *mem)
{
  *(short *) mem = 0;
}

static int
mr_setmem_short (void *mem, TP_DOMAIN * domain, DB_VALUE * value)
{
  if (value == NULL)
    mr_initmem_short (mem);
  else
    *(short *) mem = db_get_short (value);

  return NO_ERROR;
}

static int
mr_getmem_short (void *mem, TP_DOMAIN * domain, DB_VALUE * value, bool copy)
{
  return db_make_short (value, *(short *) mem);
}

static void
mr_writemem_short (OR_BUF * buf, void *memptr, TP_DOMAIN * domain)
{
  short *mem = (short *) memptr;

  or_put_short (buf, *mem);
}

static void
mr_readmem_short (OR_BUF * buf, void *mem, TP_DOMAIN * domain, int size)
{
  int rc = NO_ERROR;
  if (mem == NULL)
    {
      or_advance (buf, tp_Short.disksize);
    }
  else
    {
      *(short *) mem = or_get_short (buf, &rc);
    }
}

static void
mr_initval_short (DB_VALUE * value, int precision, int scale)
{
  db_value_domain_init (value, DB_TYPE_SHORT, precision, scale);
  db_make_short (value, 0);
}

static int
mr_setval_short (DB_VALUE * dest, DB_VALUE * src, bool copy)
{
  if (src && !DB_IS_NULL (src))
    return db_make_short (dest, db_get_short (src));
  else
    return db_value_domain_init (dest, DB_TYPE_SHORT,
				 DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE);
}

static int
mr_writeval_short (OR_BUF * buf, DB_VALUE * value)
{
  return or_put_short (buf, db_get_short (value));
}

static int
mr_readval_short (OR_BUF * buf, DB_VALUE * value,
		  TP_DOMAIN * domain, int size, bool copy,
		  char *copy_buf, int copy_buf_len)
{
  int rc = NO_ERROR;

  if (value == NULL)
    {
      rc = or_advance (buf, tp_Short.disksize);
    }
  else
    {
      db_make_short (value, (short) or_get_short (buf, &rc));
      value->need_clear = false;
    }
  return rc;
}

static int
mr_cmpdisk_short (void *mem1, void *mem2,
		  TP_DOMAIN * domain, int do_reverse,
		  int do_coercion, int total_order, int *start_colp)
{
  short s1, s2;

  s1 = OR_GET_SHORT (mem1);
  s2 = OR_GET_SHORT (mem2);

  return MR_CMP (s1, s2, do_reverse, domain);
}

static int
mr_cmpval_short (DB_VALUE * value1, DB_VALUE * value2,
		 TP_DOMAIN * domain, int do_reverse,
		 int do_coercion, int total_order, int *start_colp)
{
  short s1, s2;

  s1 = DB_GET_SHORT (value1);
  s2 = DB_GET_SHORT (value2);

  return MR_CMP (s1, s2, do_reverse, domain);
}

/*
 * TYPE FLOAT
 *
 * IEEE single precision floating point values.
 */

static void
mr_initmem_float (void *mem)
{
  *(float *) mem = 0.0;
}

static int
mr_setmem_float (void *mem, TP_DOMAIN * domain, DB_VALUE * value)
{
  if (value == NULL)
    mr_initmem_float (mem);
  else
    *(float *) mem = db_get_float (value);

  return NO_ERROR;
}

static int
mr_getmem_float (void *mem, TP_DOMAIN * domain, DB_VALUE * value, bool copy)
{
  return db_make_float (value, *(float *) mem);
}

static void
mr_writemem_float (OR_BUF * buf, void *mem, TP_DOMAIN * domain)
{
  or_put_float (buf, *(float *) mem);
}

static void
mr_readmem_float (OR_BUF * buf, void *mem, TP_DOMAIN * domain, int size)
{
  int rc = NO_ERROR;
  if (mem == NULL)
    {
      or_advance (buf, tp_Float.disksize);
    }
  else
    {
      *(float *) mem = or_get_float (buf, &rc);
    }
}

static void
mr_initval_float (DB_VALUE * value, int precision, int scale)
{
  db_make_float (value, 0.0);
  value->need_clear = false;
}

static int
mr_setval_float (DB_VALUE * dest, DB_VALUE * src, bool copy)
{
  if (src && !DB_IS_NULL (src))
    return db_make_float (dest, db_get_float (src));
  else
    return db_value_domain_init (dest, DB_TYPE_FLOAT,
				 DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE);
}

static int
mr_writeval_float (OR_BUF * buf, DB_VALUE * value)
{
  return or_put_float (buf, db_get_float (value));
}

static int
mr_readval_float (OR_BUF * buf, DB_VALUE * value,
		  TP_DOMAIN * domain, int size, bool copy,
		  char *copy_buf, int copy_buf_len)
{
  float temp;
  int rc = NO_ERROR;

  if (value == NULL)
    {
      rc = or_advance (buf, tp_Float.disksize);
    }
  else
    {
      temp = or_get_float (buf, &rc);
      db_make_float (value, temp);
      value->need_clear = false;
    }
  return rc;
}

static int
mr_cmpdisk_float (void *mem1, void *mem2,
		  TP_DOMAIN * domain, int do_reverse,
		  int do_coercion, int total_order, int *start_colp)
{
  float f1, f2;

  OR_GET_FLOAT (mem1, &f1);
  OR_GET_FLOAT (mem2, &f2);

  return MR_CMP (f1, f2, do_reverse, domain);
}

static int
mr_cmpval_float (DB_VALUE * value1, DB_VALUE * value2,
		 TP_DOMAIN * domain, int do_reverse,
		 int do_coercion, int total_order, int *start_colp)
{
  float f1, f2;

  f1 = DB_GET_FLOAT (value1);
  f2 = DB_GET_FLOAT (value2);

  return MR_CMP (f1, f2, do_reverse, domain);
}

/*
 * TYPE DOUBLE
 *
 * IEEE double precision floating vlaues.
 * Remember the pointer here isn't necessarily valid as a "double*"
 * because the value may be packed into the object such that it
 * doesn't fall on a double word boundary.
 *
 */

static void
mr_initmem_double (void *mem)
{
  double d = 0.0;

  OR_MOVE_DOUBLE (&d, mem);
}

static int
mr_setmem_double (void *mem, TP_DOMAIN * domain, DB_VALUE * value)
{
  double d;

  if (value == NULL)
    mr_initmem_double (mem);
  else
    {
      d = db_get_double (value);
      OR_MOVE_DOUBLE (&d, mem);
    }
  return NO_ERROR;
}

static int
mr_getmem_double (void *mem, TP_DOMAIN * domain, DB_VALUE * value, bool copy)
{
  double d;

  OR_MOVE_DOUBLE (mem, &d);
  return db_make_double (value, d);
}

static void
mr_writemem_double (OR_BUF * buf, void *mem, TP_DOMAIN * domain)
{
  double d;

  OR_MOVE_DOUBLE (mem, &d);
  or_put_double (buf, d);
}

static void
mr_readmem_double (OR_BUF * buf, void *mem, TP_DOMAIN * domain, int size)
{
  double d;
  int rc = NO_ERROR;

  if (mem == NULL)
    {
      or_advance (buf, tp_Double.disksize);
    }
  else
    {
      d = or_get_double (buf, &rc);
      OR_MOVE_DOUBLE (&d, mem);
    }
}

static void
mr_initval_double (DB_VALUE * value, int precision, int scale)
{
  db_value_domain_init (value, DB_TYPE_DOUBLE, precision, scale);
  db_make_double (value, 0.0);
}

static int
mr_setval_double (DB_VALUE * dest, DB_VALUE * src, bool copy)
{
  if (src && !DB_IS_NULL (src))
    return db_make_double (dest, db_get_double (src));
  else
    return db_value_domain_init (dest, DB_TYPE_DOUBLE,
				 DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE);
}

static int
mr_writeval_double (OR_BUF * buf, DB_VALUE * value)
{
  return or_put_double (buf, db_get_double (value));
}

static int
mr_readval_double (OR_BUF * buf, DB_VALUE * value,
		   TP_DOMAIN * domain, int size, bool copy,
		   char *copy_buf, int copy_buf_len)
{
  double temp;
  int rc = NO_ERROR;

  if (value == NULL)
    {
      rc = or_advance (buf, tp_Double.disksize);
      if (rc == NO_ERROR)
	{
	  rc = NO_ERROR;
	}
    }
  else
    {
      temp = or_get_double (buf, &rc);
      db_make_double (value, temp);
      value->need_clear = false;
    }
  return rc;
}

static int
mr_cmpdisk_double (void *mem1, void *mem2,
		   TP_DOMAIN * domain, int do_reverse,
		   int do_coercion, int total_order, int *start_colp)
{
  double d1, d2;

  OR_GET_DOUBLE (mem1, &d1);
  OR_GET_DOUBLE (mem2, &d2);

  return MR_CMP (d1, d2, do_reverse, domain);
}

static int
mr_cmpval_double (DB_VALUE * value1, DB_VALUE * value2,
		  TP_DOMAIN * domain, int do_reverse,
		  int do_coercion, int total_order, int *start_colp)
{
  double d1, d2;

  d1 = DB_GET_DOUBLE (value1);
  d2 = DB_GET_DOUBLE (value2);

  return MR_CMP (d1, d2, do_reverse, domain);
}

/*
 * TYPE TIME
 *
 * 32 bit seconds counter.  Interpreted as an offset within a given day.
 * Probably not general enough currently to be used an an interval type?
 *
 */

static void
mr_initmem_time (void *mem)
{
  *(DB_TIME *) mem = 0;
}

static int
mr_setmem_time (void *mem, TP_DOMAIN * domain, DB_VALUE * value)
{
  if (value == NULL)
    mr_initmem_time (mem);
  else
    *(DB_TIME *) mem = *db_get_time (value);

  return NO_ERROR;
}

static int
mr_getmem_time (void *mem, TP_DOMAIN * domain, DB_VALUE * value, bool copy)
{
  (void) db_value_put_encoded_time (value, (DB_TIME *) mem);
  value->need_clear = false;
  return NO_ERROR;
}

static void
mr_writemem_time (OR_BUF * buf, void *mem, TP_DOMAIN * domain)
{
  or_put_time (buf, (DB_TIME *) mem);
}

static void
mr_readmem_time (OR_BUF * buf, void *mem, TP_DOMAIN * domain, int size)
{
  if (mem == NULL)
    {
      or_advance (buf, tp_Time.disksize);
    }
  else
    {
      or_get_time (buf, (DB_TIME *) mem);
    }
}

static void
mr_initval_time (DB_VALUE * value, int precision, int scale)
{
  DB_TIME tm = 0;

  db_value_put_encoded_time (value, &tm);
  value->need_clear = false;
}

static int
mr_setval_time (DB_VALUE * dest, DB_VALUE * src, bool copy)
{
  int error;

  if (src == NULL || DB_IS_NULL (src))
    error = db_value_domain_init (dest, DB_TYPE_TIME,
				  DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE);
  else
    {
      error = db_value_put_encoded_time (dest, db_get_time (src));
    }
  return error;
}

static int
mr_writeval_time (OR_BUF * buf, DB_VALUE * value)
{
  return or_put_time (buf, db_get_time (value));
}

static int
mr_readval_time (OR_BUF * buf, DB_VALUE * value,
		 TP_DOMAIN * domain, int size, bool copy,
		 char *copy_buf, int copy_buf_len)
{
  DB_TIME tm;
  int rc = NO_ERROR;

  if (value == NULL)
    {
      rc = or_advance (buf, tp_Time.disksize);
    }
  else
    {
      rc = or_get_time (buf, &tm);
      db_value_put_encoded_time (value, &tm);
      value->need_clear = false;
    }
  return rc;
}

static int
mr_cmpdisk_time (void *mem1, void *mem2,
		 TP_DOMAIN * domain, int do_reverse,
		 int do_coercion, int total_order, int *start_colp)
{
  DB_TIME t1, t2;

  OR_GET_TIME (mem1, &t1);
  OR_GET_TIME (mem2, &t2);

  return MR_CMP (t1, t2, do_reverse, domain);
}

static int
mr_cmpval_time (DB_VALUE * value1, DB_VALUE * value2,
		TP_DOMAIN * domain, int do_reverse,
		int do_coercion, int total_order, int *start_colp)
{
  const DB_TIME *t1, *t2;

  t1 = DB_GET_TIME (value1);
  t2 = DB_GET_TIME (value2);

  return MR_CMP (*t1, *t2, do_reverse, domain);
}

/*
 * TYPE UTIME
 *
 * "Universal" time, more recently known as a "timestamp".
 * These are 32 bit encoded values that contain both a date and time
 * identification.
 * The encoding is the standard Unix "time_t" foramt.
 */

static void
mr_initmem_utime (void *mem)
{
  *(DB_UTIME *) mem = 0;
}

static int
mr_setmem_utime (void *mem, TP_DOMAIN * domain, DB_VALUE * value)
{
  if (value == NULL)
    mr_initmem_utime (mem);
  else
    *(DB_UTIME *) mem = *db_get_utime (value);

  return NO_ERROR;
}

static int
mr_getmem_utime (void *mem, TP_DOMAIN * domain, DB_VALUE * value, bool copy)
{
  int error;

  error = db_make_utime (value, *(DB_UTIME *) mem);
  value->need_clear = false;
  return error;
}

static void
mr_writemem_utime (OR_BUF * buf, void *mem, TP_DOMAIN * domain)
{
  or_put_utime (buf, (DB_UTIME *) mem);
}

static void
mr_readmem_utime (OR_BUF * buf, void *mem, TP_DOMAIN * domain, int size)
{
  if (mem == NULL)
    {
      or_advance (buf, tp_Utime.disksize);
    }
  else
    {
      or_get_utime (buf, (DB_UTIME *) mem);
    }
}

static void
mr_initval_utime (DB_VALUE * value, int precision, int scale)
{
  db_make_utime (value, 0);
  value->need_clear = false;
}

static int
mr_setval_utime (DB_VALUE * dest, DB_VALUE * src, bool copy)
{
  int error;

  if (src == NULL || DB_IS_NULL (src))
    error = db_value_domain_init (dest, DB_TYPE_UTIME,
				  DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE);
  else
    {
      error = db_make_utime (dest, *db_get_utime (src));
    }
  return error;
}

static int
mr_writeval_utime (OR_BUF * buf, DB_VALUE * value)
{
  return or_put_utime (buf, db_get_utime (value));
}

static int
mr_readval_utime (OR_BUF * buf, DB_VALUE * value,
		  TP_DOMAIN * domain, int size, bool copy,
		  char *copy_buf, int copy_buf_len)
{
  DB_UTIME utm;
  int rc = NO_ERROR;

  if (value == NULL)
    {
      rc = or_advance (buf, tp_Utime.disksize);
    }
  else
    {
      rc = or_get_utime (buf, &utm);
      db_make_utime (value, utm);
      value->need_clear = false;
    }
  return rc;
}

static int
mr_cmpdisk_utime (void *mem1, void *mem2,
		  TP_DOMAIN * domain, int do_reverse,
		  int do_coercion, int total_order, int *start_colp)
{
  DB_TIMESTAMP ts1, ts2;

  OR_GET_UTIME (mem1, &ts1);
  OR_GET_UTIME (mem2, &ts2);

  return MR_CMP (ts1, ts2, do_reverse, domain);
}

static int
mr_cmpval_utime (DB_VALUE * value1, DB_VALUE * value2,
		 TP_DOMAIN * domain, int do_reverse,
		 int do_coercion, int total_order, int *start_colp)
{
  const DB_TIMESTAMP *ts1, *ts2;

  ts1 = DB_GET_UTIME (value1);
  ts2 = DB_GET_UTIME (value2);

  return MR_CMP (*ts1, *ts2, do_reverse, domain);
}

/*
 * TYPE MONETARY
 *
 * Practically useless combination of an IEEE double with a currency tag.
 * Should consider re-implementing this using fixed precision numerics
 * now that we have them.
 * Because of double allignment problems, never access the amount field
 * with a structure dereference.
 */

static void
mr_initmem_money (void *memptr)
{
  DB_MONETARY *mem = (DB_MONETARY *) memptr;

  double d = 0.0;

  mem->type = DB_CURRENCY_DEFAULT;
  OR_MOVE_DOUBLE (&d, &mem->amount);
}

static int
mr_setmem_money (void *memptr, TP_DOMAIN * domain, DB_VALUE * value)
{
  DB_MONETARY *mem = (DB_MONETARY *) memptr;
  DB_MONETARY *money;

  if (value == NULL)
    mr_initmem_money (mem);
  else
    {
      money = db_get_monetary (value);
      mem->type = money->type;
      OR_MOVE_DOUBLE (&money->amount, &mem->amount);
    }
  return (NO_ERROR);
}

static int
mr_getmem_money (void *memptr, TP_DOMAIN * domain, DB_VALUE * value,
		 bool copy)
{
  int error = NO_ERROR;
  DB_MONETARY *mem = (DB_MONETARY *) memptr;
  double amt;

  OR_MOVE_DOUBLE (&mem->amount, &amt);
  error = db_make_monetary (value, mem->type, amt);
  value->need_clear = false;
  return error;
}

static void
mr_writemem_money (OR_BUF * buf, void *mem, TP_DOMAIN * domain)
{
  or_put_monetary (buf, (DB_MONETARY *) mem);
}

static void
mr_readmem_money (OR_BUF * buf, void *mem, TP_DOMAIN * domain, int size)
{
  if (mem == NULL)
    {
      or_advance (buf, tp_Monetary.disksize);
    }
  else
    {
      or_get_monetary (buf, (DB_MONETARY *) mem);
    }
}

static void
mr_initval_money (DB_VALUE * value, int precision, int scale)
{
  db_make_monetary (value, DB_CURRENCY_DEFAULT, 0.0);
  value->need_clear = false;
}

static int
mr_setval_money (DB_VALUE * dest, DB_VALUE * src, bool copy)
{
  int error;
  DB_MONETARY *money;

  if ((src == NULL) || DB_IS_NULL (src) ||
      ((money = db_get_monetary (src)) == NULL))
    error = db_value_domain_init (dest, DB_TYPE_MONETARY,
				  DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE);
  else
    {
      error = db_make_monetary (dest, money->type, money->amount);
    }
  return error;
}

static int
mr_writeval_money (OR_BUF * buf, DB_VALUE * value)
{
  return or_put_monetary (buf, db_get_monetary (value));
}

static int
mr_readval_money (OR_BUF * buf, DB_VALUE * value,
		  TP_DOMAIN * domain, int size, bool copy,
		  char *copy_buf, int copy_buf_len)
{
  DB_MONETARY money;
  int rc = NO_ERROR;

  if (value == NULL)
    {
      rc = or_advance (buf, tp_Monetary.disksize);
    }
  else
    {
      rc = or_get_monetary (buf, &money);
      db_make_monetary (value, money.type, money.amount);
      value->need_clear = false;
    }
  return rc;
}

static int
mr_cmpdisk_money (void *mem1, void *mem2,
		  TP_DOMAIN * domain, int do_reverse,
		  int do_coercion, int total_order, int *start_colp)
{
  DB_MONETARY m1, m2;

  OR_GET_MONETARY (mem1, &m1);
  OR_GET_MONETARY (mem2, &m2);

  return MR_CMP (m1.amount, m2.amount, do_reverse, domain);
}

static int
mr_cmpval_money (DB_VALUE * value1, DB_VALUE * value2,
		 TP_DOMAIN * domain, int do_reverse,
		 int do_coercion, int total_order, int *start_colp)
{
  const DB_MONETARY *m1, *m2;

  m1 = DB_GET_MONETARY (value1);
  m2 = DB_GET_MONETARY (value2);

  return MR_CMP (m1->amount, m2->amount, do_reverse, domain);
}

/*
 * TYPE DATE
 *
 * 32 bit day counter, commonly called a "julian" date.
 */

static void
mr_initmem_date (void *mem)
{
  *(DB_DATE *) mem = 0;
}

static int
mr_setmem_date (void *mem, TP_DOMAIN * domain, DB_VALUE * value)
{
  if (value == NULL)
    mr_initmem_date (mem);
  else
    *(DB_DATE *) mem = *db_get_date (value);

  return NO_ERROR;
}

static int
mr_getmem_date (void *mem, TP_DOMAIN * domain, DB_VALUE * value, bool copy)
{
  return db_value_put_encoded_date (value, (DB_DATE *) mem);
}

static void
mr_writemem_date (OR_BUF * buf, void *mem, TP_DOMAIN * domain)
{
  or_put_date (buf, (DB_DATE *) mem);
}

static void
mr_readmem_date (OR_BUF * buf, void *mem, TP_DOMAIN * domain, int size)
{
  if (mem == NULL)
    {
      or_advance (buf, tp_Date.disksize);
    }
  else
    {
      or_get_date (buf, (DB_DATE *) mem);
    }
}

static void
mr_initval_date (DB_VALUE * value, int precision, int scale)
{
  db_value_put_encoded_date (value, 0);
  value->need_clear = false;
}

static int
mr_setval_date (DB_VALUE * dest, DB_VALUE * src, bool copy)
{
  int error;

  if (src == NULL || DB_IS_NULL (src))
    error = db_value_domain_init (dest, DB_TYPE_DATE,
				  DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE);
  else
    {
      error = db_value_put_encoded_date (dest, db_get_date (src));
    }
  return error;
}

static int
mr_writeval_date (OR_BUF * buf, DB_VALUE * value)
{
  return or_put_date (buf, db_get_date (value));
}

static int
mr_readval_date (OR_BUF * buf, DB_VALUE * value,
		 TP_DOMAIN * domain, int size, bool copy,
		 char *copy_buf, int copy_buf_len)
{
  DB_DATE dt;
  int rc = NO_ERROR;

  if (value == NULL)
    {
      rc = or_advance (buf, tp_Date.disksize);
    }
  else
    {
      rc = or_get_date (buf, &dt);
      db_value_put_encoded_date (value, &dt);
      value->need_clear = false;
    }
  return rc;
}

static int
mr_cmpdisk_date (void *mem1, void *mem2,
		 TP_DOMAIN * domain, int do_reverse,
		 int do_coercion, int total_order, int *start_colp)
{
  DB_DATE d1, d2;

  OR_GET_DATE (mem1, &d1);
  OR_GET_DATE (mem2, &d2);

  return MR_CMP (d1, d2, do_reverse, domain);
}

static int
mr_cmpval_date (DB_VALUE * value1, DB_VALUE * value2,
		TP_DOMAIN * domain, int do_reverse,
		int do_coercion, int total_order, int *start_colp)
{
  const DB_DATE *d1, *d2;

  d1 = DB_GET_DATE (value1);
  d2 = DB_GET_DATE (value2);

  return MR_CMP (*d1, *d2, do_reverse, domain);
}

/*
 * TYPE OBJECT
 */

/*
 * This is a bit different than the other primitive types in that the memory
 * value and the DB_VALUE representations are not the same.  The memory value
 * will be represented with a WS_MEMOID structure to avoid creating MOPs until
 * they are actually needed.
 *
 * These types are not available on the server since there is no workspace
 * over there.  Although in standalone mode, we could promote OIDs to MOPs
 * on both sides, use the db_on_server flag to control this so we make
 * both sides behave similarly even though we're in standalone mode.
 * The "mem" functions will in general be commented out since they
 * call things that don't exist on the server.  THe "val" functions will
 * exist so that things tagged as DB_TYPE_OBJECT can be read as OID values.
 *
 */


/*
 * mr_null_oid - This is used to set an OID to the NULL state.
 *    return:  void
 *    oid(out): oid to initialize
 * Note:
 *    SET_OID_NULL does the actual work by setting the volid to -1 but it
 *    leaves garbage in the other fields which can be stored on disk and
 *    generally looks alarming when you encounter it later.  Before
 *    calling SET_OID_NULL, initialize the fields to nice zero values.
 *    Should be an inline function.
 */
static void
mr_null_oid (OID * oid)
{
  oid->pageid = 0;
  oid->volid = 0;
  oid->slotid = 0;

  OID_SET_NULL (oid);
}

static void
mr_initmem_object (void *memptr)
{
  /* there is no use for initmem on the server */
#if !defined (SERVER_MODE)
  WS_MEMOID *mem = (WS_MEMOID *) memptr;

  mr_null_oid (&mem->oid);
  mem->pointer = NULL;
#endif
}

/*
 * Can get here on the server when dispatching from set element domains.
 * Always represent object values as OIDs on the server.
 */

static void
mr_initval_object (DB_VALUE * value, int precision, int scale)
{
  OID oid;

#if !defined (SERVER_MODE)
  if (db_on_server)
    {
      db_value_domain_init (value, DB_TYPE_OID, precision, scale);
      OID_SET_NULL (&oid);
      db_make_oid (value, &oid);
    }
  else
    {
      db_value_domain_init (value, DB_TYPE_OBJECT, precision, scale);
      db_make_object (value, NULL);
    }
#else /* SERVER_MODE */
  db_value_domain_init (value, DB_TYPE_OID, precision, scale);
  OID_SET_NULL (&oid);
  db_make_oid (value, &oid);
#endif /* !SERVER_MODE */
}

static int
mr_setmem_object (void *memptr, TP_DOMAIN * domain, DB_VALUE * value)
{
  /* there is no need for setmem on the server */
#if !defined (SERVER_MODE)
  WS_MEMOID *mem = (WS_MEMOID *) memptr;
  OID *oid;
  MOP op;

  if (value == NULL)
    {
      mr_null_oid (&mem->oid);
      mem->pointer = NULL;
    }
  else
    {
      op = db_get_object (value);
      if (op == NULL)
	{
	  mr_initmem_object (mem);
	}
      else
	{
	  oid = WS_OID (op);
	  mem->oid.volid = oid->volid;
	  mem->oid.pageid = oid->pageid;
	  mem->oid.slotid = oid->slotid;
	  mem->pointer = op;
	}
    }
#endif /* !SERVER_MODE */
  return (NO_ERROR);
}

static int
mr_getmem_object (void *memptr, TP_DOMAIN * domain,
		  DB_VALUE * value, bool copy)
{
  int error = NO_ERROR;

  /* there is no need for getmem on the server */
#if !defined (SERVER_MODE)
  WS_MEMOID *mem = (WS_MEMOID *) memptr;
  MOP op;

  op = mem->pointer;
  if (op == NULL)
    {
      if (!OID_ISNULL (&mem->oid))
	{
	  op = ws_mop (&mem->oid, NULL);
	  if (op != NULL)
	    {
	      mem->pointer = op;
	      error = db_make_object (value, op);
	    }
	  else
	    {
	      error = er_errid ();
	      (void) db_make_object (value, NULL);
	    }
	}
    }
  else
    error = db_make_object (value, op);
#endif /* !SERVER_MODE */

  return (error);
}


static int
mr_setval_object (DB_VALUE * dest, DB_VALUE * src, bool copy)
{
  int error = NO_ERROR;

#if !defined (SERVER_MODE)
  if (src == NULL || DB_IS_NULL (src))

    PRIM_SET_NULL (dest);

  /* can get here on the server when dispatching through set element domains */
  else if (DB_VALUE_TYPE (src) == DB_TYPE_OID)
    {
      /* make sure that the target type is set properly */
      db_value_domain_init (dest, DB_TYPE_OID,
			    DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE);
      error = db_make_oid (dest, db_get_oid (src));
    }
  else if (DB_VALUE_TYPE (src) == DB_TYPE_OBJECT)
    {
      /* If we're logically on the server, we probably shouldn't have gotten
       * here but if we do, don't continue with the object representation,
       * de-swizzle it back to an OID.
       */
      if (db_on_server)
	{
	  DB_OBJECT *obj;
	  /* what should this do for ISVID mops? */
	  obj = db_get_object (src);
	  db_value_domain_init (dest, DB_TYPE_OID,
				DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE);
	  error = db_make_oid (dest, WS_OID (obj));
	}
      else
	{
	  db_value_domain_init (dest, DB_TYPE_OBJECT,
				DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE);
	  error = db_make_object (dest, db_get_object (src));
	}
    }
#else /* SERVER_MODE */
  /*
   * If we're really on the server, we can only get here when dispatching
   * through set element domains.  The value must contain an OID.
   */
  if (src == NULL || DB_IS_NULL (src) || DB_VALUE_TYPE (src) != DB_TYPE_OID)
    PRIM_SET_NULL (dest);
  else
    error = db_make_oid (dest, db_get_oid (src));
#endif /* !SERVER_MODE */

  return error;
}



/*
 * mr_lengthval_object - checks if the object is virtual or not. and returns
 * propery type size.
 *    return: If it is virtual object returns caclulated the DB_TYPE_VOBJ
 *    packed size. returns DB_TYPE_OID otherwise
 *    value(in): value to get length
 *    disk(in): indicator that it is disk object
 */
static int
mr_lengthval_object (DB_VALUE * value, int disk)
{
#if !defined (SERVER_MODE)
  MOP mop;
#endif
  long size;
  if (disk)
    {
      size = OR_OID_SIZE;
    }
  else
    {
      size = MR_OID_SIZE;
    }

#if !defined (SERVER_MODE)
  if (DB_VALUE_TYPE (value) == DB_TYPE_OBJECT && disk)
    {
      mop = db_get_object (value);
      if ((mop == NULL) || (mop->deleted))
	{
	  /* The size of a NULL is OR_OID_SIZE, which is already set
	   * (from Jeff L.) */
	}
      else if (WS_ISVID (mop))
	{
	  DB_VALUE vmop_seq;
	  int error;

	  error = vid_object_to_vobj (mop, &vmop_seq);
	  if (error >= 0)
	    {
	      size = mr_lengthval_set (&vmop_seq, disk);
	      pr_clear_value (&vmop_seq);
	    }
	}
    }
#endif

  return size;
}

static void
mr_writemem_object (OR_BUF * buf, void *memptr, TP_DOMAIN * domain)
{
#if !defined (SERVER_MODE)	/* there is no need for writemem on the server */
  WS_MEMOID *mem = (WS_MEMOID *) memptr;
  OID *oidp;

  oidp = NULL;
  if (mem != NULL)
    oidp = &mem->oid;

  if (oidp == NULL)
    {
      /* construct an unbound oid */
      oidp = (OID *) & oid_Null_oid;
    }
  else if (OID_ISTEMP (oidp))
    {
      /* Temporary oid, must get a permanent one.
         This should only happen if the MOID has a valid MOP.
         Check for deletion */
      if ((mem->pointer == NULL) || (mem->pointer->deleted))
	{
	  oidp = (OID *) & oid_Null_oid;
	  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE,
		  ER_MR_TEMP_OID_WITHOUT_MOP, 0);
	}
      else
	{
	  oidp = WS_OID (mem->pointer);
	  if (OID_ISTEMP (oidp))
	    {
	      /* a MOP with a temporary OID, make an entry in the OID fixup
	       * table if we have one, otherwise, stop and assign a permanent one.
	       */
	      oidp = tf_need_permanent_oid (buf, mem->pointer);
	      if (oidp == NULL)
		/* normally would have used or_abort by now */
		oidp = (OID *) & oid_Null_oid;
	    }
	}
    }
  else
    {
      /* normal OID check for deletion */
      if ((mem->pointer != NULL) && (mem->pointer->deleted))
	oidp = (OID *) & oid_Null_oid;
    }

  or_put_oid (buf, oidp);

#else /* SERVER_MODE */
  /* should never get here but in case we do, dump a NULL OID into
   * the buffer.
   */
  printf ("mr_writemem_object: called on the server !\n");
  or_put_oid (buf, (OID *) & oid_Null_oid);
#endif /* !SERVER_MODE */
}


static void
mr_readmem_object (OR_BUF * buf, void *memptr, TP_DOMAIN * domain, int size)
{
#if !defined (SERVER_MODE)	/* there is no need for readmem on the server ??? */
  WS_MEMOID *mem = (WS_MEMOID *) memptr;

  if (mem != NULL)
    {
      or_get_oid (buf, &mem->oid);
      mem->pointer = NULL;
    }
  else
    {
      or_advance (buf, tp_Object.disksize);
    }
#else
  /* shouldn't get here but if we do, just skip over it */
  printf ("mr_readmem_object: called on the server !\n");
  or_advance (buf, tp_Object.disksize);
#endif

}

static int
mr_writeval_object (OR_BUF * buf, DB_VALUE * value)
{
#if !defined (SERVER_MODE)
  MOP mop;
#endif
  OID *oidp = NULL;

  int rc = NO_ERROR;

#if !defined (SERVER_MODE)
  if (db_on_server || pr_Inhibit_oid_promotion)
    {
      if (DB_VALUE_TYPE (value) == DB_TYPE_OID)
	{
	  oidp = db_get_oid (value);
	  rc = or_put_oid (buf, oidp);
	  return rc;
	}
      else
	{
	  return ER_FAILED;
	}
    }
  if (DB_VALUE_TYPE (value) == DB_TYPE_OBJECT)
    {
      mop = db_get_object (value);
      if ((mop == NULL) || (mop->deleted))
	{
	  rc = or_put_oid (buf, (OID *) & oid_Null_oid);
	}
      else if (WS_ISVID (mop))
	{
	  DB_VALUE vmop_seq;
	  int error;

	  error = vid_object_to_vobj (mop, &vmop_seq);
	  if (error >= 0)
	    {
	      rc = mr_writeval_set (buf, &vmop_seq);
	      pr_clear_value (&vmop_seq);
	    }
	  else
	    {
	      rc = ER_FAILED;
	    }
	}
      else
	{
	  oidp = WS_OID (mop);
	  if (OID_ISTEMP (oidp))
	    {
	      /* a MOP with a temporary OID, make an entry in the OID fixup
	       * table if we have one, otherwise, stop and assign a permanent one.
	       */
	      oidp = tf_need_permanent_oid (buf, mop);
	      if (oidp == NULL)
		/* normally would have used or_abort by now */
		oidp = (OID *) & oid_Null_oid;
	    }
	  rc = or_put_oid (buf, oidp);
	}
    }
  else if (DB_VALUE_TYPE (value) == DB_TYPE_OID)
    {
      oidp = db_get_oid (value);
      rc = or_put_oid (buf, oidp);
    }
  else
    {
      /* should never get here ! */
      rc = or_put_oid (buf, (OID *) & oid_Null_oid);
    }
#else /* SERVER_MODE */
  /* on the server, the value must contain an OID */
  oidp = db_get_oid (value);
  rc = or_put_oid (buf, oidp);
#endif /* !SERVER_MODE */
  return rc;
}



static int
mr_readval_object (OR_BUF * buf, DB_VALUE * value,
		   TP_DOMAIN * domain, int size, bool copy,
		   char *copy_buf, int copy_buf_len)
{
  OID oid;
  int rc = NO_ERROR;

#if !defined (SERVER_MODE)
  if (value == NULL)
    {
      rc = or_advance (buf, tp_Object.disksize);
    }
  else
    {
      if (db_on_server || pr_Inhibit_oid_promotion)
	{
	  /* basically the same as mr_readval_server_oid, don't promote OIDs */
	  db_value_domain_init (value, DB_TYPE_OID,
				DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE);
	  rc = or_get_oid (buf, &oid);
	  db_make_oid (value, &oid);
	}
      else
	{
	  db_value_domain_init (value, DB_TYPE_OBJECT,
				DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE);

	  rc = or_get_oid (buf, &oid);
	  /*
	   * if the OID is NULL, leave the value with the NULL bit set
	   * and don't bother to put the OID inside.
	   * I added this because it seemed logical, does it break anything ?
	   */
	  if (!OID_ISNULL (&oid))
	    {
	      db_make_object (value, ws_mop (&oid, NULL));
	      if (db_get_object (value) == NULL)
		{
		  or_abort (buf);
		  return ER_FAILED;
		}
	    }
	}
    }
#else /* SERVER_MODE */
  /* on the server, we only read OIDs */
  if (value == NULL)
    {
      rc = or_advance (buf, tp_Object.disksize);
    }
  else
    {
      db_value_domain_init (value, DB_TYPE_OID,
			    DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE);
      rc = or_get_oid (buf, &oid);
      /* should we be checking for the NULL OID here ? */
      db_make_oid (value, &oid);
    }
#endif /* !SERVER_MODE */
  return rc;
}

static int
mr_cmpdisk_object (void *mem1, void *mem2,
		   TP_DOMAIN * domain, int do_reverse,
		   int do_coercion, int total_order, int *start_colp)
{
  int c;
  OID o1, o2;

  OR_GET_OID (mem1, &o1);
  OR_GET_OID (mem2, &o2);
  /* if we ever store virtual objects, this will need to be
   * changed. However, since its known the only disk representation
   * of objects is an OID, this is a valid optimization
   */

  c = oid_compare (&o1, &o2);
  if (do_reverse || (domain && domain->is_desc))
    {
      c = -c;
    }

  return c;
}

static int
mr_cmpval_object (DB_VALUE * value1, DB_VALUE * value2,
		  TP_DOMAIN * domain, int do_reverse,
		  int do_coercion, int total_order, int *start_colp)
{
  int c;
#if defined (SERVER_MODE)
  OID *o1, *o2;
  DB_OBJECT *obj;

  /*
   * we need to be careful here because even though the domain may
   * say object, it may really be an OID (especially on the server).
   */
  if (db_value_domain_type (value1) == DB_TYPE_OID)
    {
      o1 = DB_GET_OID (value1);
    }
  else
    {
      obj = DB_GET_OBJECT (value1);
      o1 = (obj) ? WS_OID (obj) : (OID *) & oid_Null_oid;
    }

  if (db_value_domain_type (value2) == DB_TYPE_OID)
    {
      o2 = DB_GET_OID (value2);
    }
  else
    {
      obj = DB_GET_OBJECT (value2);
      o2 = (obj) ? WS_OID (obj) : (OID *) & oid_Null_oid;
    }

  c = oid_compare (o1, o2);
#else /* !SERVER_MODE */
  /* on the client, we must also handle virtual db_object types */
  OID *o1 = NULL, *o2 = NULL;
  DB_OBJECT *mop1 = NULL, *mop2 = NULL, *class1, *class2;
  int virtual_ = 0;
  int nonupdate = 0;
  DB_VALUE keys1, keys2;

  /*
   * we need to be careful here because even though the domain may
   * say object, it may really be an OID (especially on the server).
   */
  if (db_value_domain_type (value1) == DB_TYPE_OID)
    o1 = DB_GET_OID (value1);
  else
    {
      mop1 = DB_GET_OBJECT (value1);
      if (WS_ISVID (mop1))
	{
	  if (db_is_updatable_object (mop1))
	    {
	      mop1 = db_real_instance (mop1);
	    }
	  else
	    {
	      nonupdate = 1;
	    }
	  if (WS_ISVID (mop1))
	    {
	      /* non updateble object or proxy object */
	      virtual_ = 1;
	    }
	  else
	    {
	      o1 = WS_OID (mop1);
	    }
	}
      else
	{
	  o1 = WS_OID (mop1);
	}
    }

  if (db_value_domain_type (value2) == DB_TYPE_OID)
    o2 = DB_GET_OID (value2);
  else
    {
      mop2 = DB_GET_OBJECT (value2);
      if (WS_ISVID (mop2))
	{
	  if (db_is_updatable_object (mop2))
	    {
	      mop2 = db_real_instance (mop2);
	    }
	  else
	    {
	      nonupdate += 2;
	    }
	  if (WS_ISVID (mop2))
	    {
	      /* non updateble object or proxy object */
	      virtual_ += 2;
	    }
	  else
	    {
	      o2 = WS_OID (mop2);
	    }
	}
      else
	{
	  o2 = WS_OID (mop2);
	}
    }

  if (virtual_ == 0)
    {
      c = oid_compare (o1, o2);
      goto exit_on_end;
    }

  if (mop1 == mop2)
    {
      c = DB_EQ;		/* an optimization */
      goto exit_on_end;
    }

  if (virtual_ == 1)
    {
      c = DB_LT;		/* consistent comparison of oids and  */
      goto exit_on_end;
    }
  if (virtual_ == 2)
    {
      c = DB_GT;		/* non-oid based vobjs, they are never equal */
      goto exit_on_end;
    }

  /*
   * virtual must be 3, impling both the objects are wither proxies
   * or non-updatable objects
   */

  if (nonupdate == 1)
    {
      c = DB_LT;		/* again, a consistent comparison */
      goto exit_on_end;
    }

  if (nonupdate == 2)
    {
      c = DB_GT;		/* for proxy mop and non-updatable mop */
      goto exit_on_end;
    }

  if (nonupdate == 0)
    {
      /*
       * comparing two proxy mops, the must both be from the
       * same proxy class. Compare the proxy classes oids.
       * Note class mops are never virtual mops.
       */
      class1 = db_get_class (mop1);
      class2 = db_get_class (mop2);
      o1 = WS_OID (class1);
      o2 = WS_OID (class2);
      c = oid_compare (o1, o2);
      /*
       * as long as the result is not equal, we are done
       * If its equal, we need to continue with a key test below.
       */
      if (c != DB_EQ)
	{
	  goto exit_on_end;
	}
    }

  /*
   * here, nonupdate must be 3 or 0 and
   * we must have two non-updatable mops, or two proxy mops
   * from the same proxy. Consequently, their keys are comparable
   * to identify the object.
   */
  vid_get_keys (mop1, &keys1);
  vid_get_keys (mop2, &keys2);
  c = tp_value_compare (&keys1, &keys2, do_coercion, total_order);

exit_on_end:
#endif /* SERVER_MODE */

  if (do_reverse || (domain && domain->is_desc))
    {
      c = -c;
    }

  return c;
}




#if !defined (SERVER_MODE)

/*
 * pr_write_mop - write an OID to a disk representation buffer given a MOP
 * instead of a WS_MEMOID.
 *    return:
 *    buf(): transformer buffer
 *    mop(): mop to transform
 * Note:
 *    mr_write_object can't be used because it takes a WS_MEMOID as is the
 *    case for object references in instances.
 *    This must stay in sync with mr_write_object above !
 */
void
pr_write_mop (OR_BUF * buf, MOP mop)
{
  DB_VALUE value;

  mr_initval_object (&value, 0, 0);
  db_make_object (&value, mop);
  mr_writeval_object (buf, &value);
  mr_setval_object (&value, NULL, false);
}
#endif /* !SERVER_MODE */

/*
 * TYPE ELO
 *
 * Primitive type interface for elos.  These can either reference a long
 * object (LO) or a file base object (FBO).
 * Currently, the attribute is simple a pointer to an externally allocated
 * ELO structure.  We may want to keep just the LOID and pathname in the
 * attribute and do the ELO expansion only when the attribute is referenced.
 * Since the ELO's aren't very large, don't worry about this right now.
 * If we do do this later, we will need to rethink the pr_vartype_
 * functions below because they make some unpleasant assumptions about
 * the attribute representation and the DB_VALUE representations being
 * the same for most primitive types.
 *
 * NOTE: 10/8/91  ELO's are now manipulated solely by the GLO class and
 * associated methods, they will be responsible for maintaing ownership
 * of the elo's.  Because of this, the get and set functions for elo's
 * will be nothing more than pointer copies without the calls to the
 * structure copy and free routines that is found in the set accessors.
 *
 */

static void
mr_initmem_elo (void *memptr)
{
  DB_ELO **mem = (DB_ELO **) memptr;

  *mem = NULL;
}

static void
mr_initval_elo (DB_VALUE * value, int precision, int scale)
{
  db_value_domain_init (value, DB_TYPE_ELO, precision, scale);
  db_make_elo (value, NULL);
}

static int
mr_setmem_elo (void *memptr, TP_DOMAIN * domain, DB_VALUE * value)
{
  DB_ELO **mem = (DB_ELO **) memptr;

  if (value == NULL)
    mr_initmem_elo (mem);
  else
    *mem = db_get_elo (value);
  return (NO_ERROR);
}

static int
mr_getmem_elo (void *memptr, TP_DOMAIN * domain, DB_VALUE * value, bool copy)
{
  DB_ELO **mem = (DB_ELO **) memptr;

  return db_make_elo (value, *mem);
}

static int
mr_setval_elo (DB_VALUE * dest, DB_VALUE * src, bool copy)
{
  if (src == NULL || DB_IS_NULL (src))
    {
      PRIM_SET_NULL (dest);
      return NO_ERROR;
    }
  else
    return db_make_elo (dest, db_get_elo (src));
}

static int
mr_lengthmem_elo (void *memptr, TP_DOMAIN * domain, int disk)
{
  int len;

  len = 0;
  if (!disk)
    {
      len = tp_Elo.size;
    }
  else
    {
      DB_ELO **mem = (DB_ELO **) memptr;
      ELO *elo = *mem;

      len = OR_ELO_HEADER_SIZE;	/* size without pathname string */
      if (elo != NULL && elo->original_pathname != NULL)
	{
	  int namelen, bits;

	  namelen = strlen (elo->original_pathname) + 1;
	  /* add padding for disk representation */
	  bits = namelen & 3;
	  if (bits)
	    {
	      namelen += 4 - bits;
	    }
	  len += namelen;
	}
    }
  return len;
}

static int
mr_lengthval_elo (DB_VALUE * value, int disk)
{
  DB_ELO *elo;

  if (value != NULL)
    {
      elo = db_get_elo (value);
      return mr_lengthmem_elo (&elo, NULL, disk);
    }
  else
    {
      return NO_ERROR;
    }
}

static void
mr_writemem_elo (OR_BUF * buf, void *memptr, TP_DOMAIN * domain)
{
  DB_ELO **mem = (DB_ELO **) memptr;
  ELO *elo;

  elo = *mem;
  if (elo == NULL)
    {
      or_put_loid (buf, NULL);	/* will generate NULL loid */
    }
  else
    {
      or_put_loid (buf, &elo->loid);
      if (elo->original_pathname != NULL)
	{
	  or_put_string (buf, (char *) elo->original_pathname);
	}
    }
}

static void
mr_readmem_elo (OR_BUF * buf, void *memptr, TP_DOMAIN * domain, int size)
{
#if !defined (SERVER_MODE)
  DB_ELO **mem = (DB_ELO **) memptr;
  ELO *elo;
  char *str;
  int len;

  if (mem == NULL)
    {
      or_advance (buf, size);
    }
  else
    {
      if (size == 0)
	{
	  *mem = NULL;
	}
      else
	{
	  elo = elo_new_elo ();
	  if (elo == NULL)
	    {
	      or_abort (buf);
	    }
	  else
	    {
	      or_get_loid (buf, &elo->loid);
	      elo->type = ELO_LO;
	      if (size > OR_ELO_HEADER_SIZE)
		{
		  /* we had a pathname string */
		  len = size - OR_ELO_HEADER_SIZE;
		  str = db_private_alloc (NULL, len);
		  if (str == NULL)
		    {
		      or_abort (buf);
		    }
		  else
		    {
		      or_get_data (buf, str, len);
		      elo->pathname = str;
		      elo->original_pathname = pr_copy_string (str);
		      if (elo->original_pathname == NULL)
			{
			  or_abort (buf);
			}
		    }
		  /* else, should abort transformation, out of memory */
		  elo->type = ELO_FBO;
		}
	      *mem = elo;
	    }
	}
    }
#endif /* !SERVER_MODE */
}

static int
mr_writeval_elo (OR_BUF * buf, DB_VALUE * value)
{
  DB_ELO *elo;

  elo = db_get_elo (value);
  /* could create a domain for writemem, but we don't need it in this case */
  mr_writemem_elo (buf, &elo, NULL);
  return NO_ERROR;
}

static int
mr_readval_elo (OR_BUF * buf, DB_VALUE * value,
		TP_DOMAIN * domain, int size, bool copy,
		char *copy_buf, int copy_buf_len)
{
#if !defined (SERVER_MODE)
  DB_ELO *elo;
#endif
  int rc = NO_ERROR;

  /* we shouldn't be finding these in list files or packed arrays yet ! */
  if (size == -1)
    {
      return ER_FAILED;
    }

  if (value == NULL)
    {
      rc = or_advance (buf, size);
    }
  else
    {
      db_value_domain_init (value, DB_TYPE_ELO,
			    DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE);
      if (size)
	{
	  /*
	   * Avoid reading these on the server until we have somethig over there
	   * that is able to free the value.
	   */
#if defined (SERVER_MODE)
	  rc = or_advance (buf, size);
#else
	  /*
	   * Even in standalone, loaddb has a problem because the index update
	   * stuff doesn't clear ELO values after it reads them.
	   * Avoid reading ELO's
	   * even if we're only "logically" on the server.
	   */
	  if (db_on_server)
	    {
	      rc = or_advance (buf, size);
	    }
	  else
	    {
	      /*
	       * domain is left NULL in this case though we could synthesize
	       * one
	       */
	      mr_readmem_elo (buf, &elo, NULL, size);
	      db_make_elo (value, elo);
	    }
#endif
	}
    }
  return rc;
}

static void
mr_freemem_elo (void *memptr)
{
#if !defined (SERVER_MODE)
  DB_ELO **mem = (DB_ELO **) memptr;

  if (mem != NULL && *mem != NULL)
    {
      elo_free (*mem);
    }
#endif
}

static int
mr_cmpdisk_elo (void *mem1, void *mem2,
		TP_DOMAIN * domain, int do_reverse,
		int do_coercion, int total_order, int *start_colp)
{
  /*
   * don't know how to do this since elo's should find their way into
   * listfiles and such.
   */
  return DB_UNK;
}

static int
mr_cmpval_elo (DB_VALUE * value1, DB_VALUE * value2,
	       TP_DOMAIN * domain, int do_reverse,
	       int do_coercion, int total_order, int *start_colp)
{
  DB_ELO *elo1, *elo2;

  elo1 = DB_GET_ELO (value1);
  elo2 = DB_GET_ELO (value2);

  /* use address for collating sequence */
  return MR_CMP ((unsigned long) elo1, (unsigned long) elo2, do_reverse,
		 domain);
}

/*
 * TYPE VARIABLE
 *
 * Currently this can only be used internaly for class objects.  I think
 * this is usefull enough to make a general purpose thing.
 * Implemented with the DB_VALUE (like set elements) which means that we
 * will always create MOPs for variable values that are object references.
 * If this gets to be a big deal, will need to define another union
 * like DB_MEMORY_VALUE that has a local OID cache like the attribute
 * values do.
 * These were once just stubs that didn't do anything since the class
 * transformer called the pr_write_va/rtype etc. functions directly.  If
 * they can be regular types for object attributes, we need to support
 * an mr_ interface as well.
 *
 * NOTE: These are still stubs, need to think about other ramifications
 * in the schema level before making these public.
 */

static void
mr_initval_variable (DB_VALUE * value, int precision, int scale)
{
  mr_initval_null (value, precision, scale);
}

static int
mr_setval_variable (DB_VALUE * dest, DB_VALUE * src, bool copy)
{
  mr_initval_null (dest, 0, 0);
  return (NO_ERROR);
}

static int
mr_lengthval_variable (DB_VALUE * value, int disk)
{
  return (0);
}

static int
mr_writeval_variable (OR_BUF * buf, DB_VALUE * value)
{
  return NO_ERROR;
}

static int
mr_readval_variable (OR_BUF * buf, DB_VALUE * value,
		     TP_DOMAIN * domain, int size, bool copy,
		     char *copy_buf, int copy_buf_len)
{
  return NO_ERROR;
}

static int
mr_cmpdisk_variable (void *mem1, void *mem2,
		     TP_DOMAIN * domain, int do_reverse,
		     int do_coercion, int total_order, int *start_colp)
{
  return DB_UNK;
}

static int
mr_cmpval_variable (DB_VALUE * value1, DB_VALUE * value2,
		    TP_DOMAIN * domain, int do_reverse,
		    int do_coercion, int total_order, int *start_colp)
{
  return DB_UNK;
}

/*
 * TYPE SUBSTRUCTURE
 *
 * Only for meta objects.  Might want to extend.
 * This really only serves as a placeholder in the type table.  These
 * functions should never be referenced through the usual channels.
 */

static void
mr_initmem_sub (void *mem)
{
}

static void
mr_initval_sub (DB_VALUE * value, int precision, int scale)
{
  db_value_domain_init (value, DB_TYPE_SUB, precision, scale);
}

static int
mr_setmem_sub (void *mem, TP_DOMAIN * domain, DB_VALUE * value)
{
  return (NO_ERROR);
}

static int
mr_getmem_sub (void *mem, TP_DOMAIN * domain, DB_VALUE * value, bool copy)
{
  return (NO_ERROR);
}

static int
mr_setval_sub (DB_VALUE * dest, DB_VALUE * src, bool copy)
{
  return (NO_ERROR);
}

static int
mr_lengthmem_sub (void *mem, TP_DOMAIN * domain, int disk)
{
  return (0);
}

static int
mr_lengthval_sub (DB_VALUE * value, int disk)
{
  return (0);
}

static void
mr_writemem_sub (OR_BUF * buf, void *mem, TP_DOMAIN * domain)
{
}

static void
mr_readmem_sub (OR_BUF * buf, void *mem, TP_DOMAIN * domain, int size)
{
}

static int
mr_writeval_sub (OR_BUF * buf, DB_VALUE * value)
{
  return NO_ERROR;
}

static int
mr_readval_sub (OR_BUF * buf, DB_VALUE * value,
		TP_DOMAIN * domain, int size, bool copy,
		char *copy_buf, int copy_buf_len)
{
  return NO_ERROR;
}

static int
mr_cmpdisk_sub (void *mem1, void *mem2,
		TP_DOMAIN * domain, int do_reverse,
		int do_coercion, int total_order, int *start_colp)
{
  return DB_UNK;
}

static int
mr_cmpval_sub (DB_VALUE * value1, DB_VALUE * value2,
	       TP_DOMAIN * domain, int do_reverse,
	       int do_coercion, int total_order, int *start_colp)
{
  return DB_UNK;
}

/*
 * TYPE POINTER
 *
 * These exist only so that method arguments can have arbitrary pointer
 * values.  You cannot create an attribute that has a pointer value since
 * these are not persistent values.  Pointer values are used internally
 * by the object templates to keep place holders to other templates
 * that need to be expanded into objects.
 *
 */

static void
mr_initmem_ptr (void *memptr)
{
  void **mem = (void **) memptr;

  *mem = NULL;
}

static void
mr_initval_ptr (DB_VALUE * value, int precision, int scale)
{
  db_value_domain_init (value, DB_TYPE_POINTER, precision, scale);
  db_make_pointer (value, NULL);
}

static int
mr_setmem_ptr (void *memptr, TP_DOMAIN * domain, DB_VALUE * value)
{
  void **mem = (void **) memptr;

  if (value == NULL)
    {
      mr_initmem_ptr (mem);
    }
  else
    {
      *mem = db_get_pointer (value);
    }
  return (NO_ERROR);
}

static int
mr_getmem_ptr (void *memptr, TP_DOMAIN * domain, DB_VALUE * value, bool copy)
{
  void **mem = (void **) memptr;

  return db_make_pointer (value, *mem);
}

static int
mr_setval_ptr (DB_VALUE * dest, DB_VALUE * src, bool copy)
{
  if (src == NULL || DB_IS_NULL (src))
    {
      PRIM_SET_NULL (dest);
      return NO_ERROR;
    }
  else
    {
      return db_make_pointer (dest, db_get_pointer (src));
    }
}

static int
mr_lengthmem_ptr (void *memptr, TP_DOMAIN * domain, int disk)
{
  return (0);
}

static int
mr_lengthval_ptr (DB_VALUE * value, int disk)
{
  void *ptr;

  if (value != NULL)
    {
      ptr = db_get_pointer (value);
      return mr_lengthmem_ptr (&ptr, NULL, disk);
    }
  else
    {
      return NO_ERROR;
    }
}

static void
mr_writemem_ptr (OR_BUF * buf, void *memptr, TP_DOMAIN * domain)
{
}

static void
mr_readmem_ptr (OR_BUF * buf, void *memptr, TP_DOMAIN * domain, int size)
{
  void **mem = (void **) memptr;

  *mem = NULL;
}

static int
mr_writeval_ptr (OR_BUF * buf, DB_VALUE * value)
{
  return NO_ERROR;
}

static int
mr_readval_ptr (OR_BUF * buf, DB_VALUE * value,
		TP_DOMAIN * domain, int size, bool copy,
		char *copy_buf, int copy_buf_len)
{
  if (value)
    {
      db_value_domain_init (value, DB_TYPE_POINTER,
			    DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE);
    }
  return NO_ERROR;
}

static int
mr_cmpdisk_ptr (void *mem1, void *mem2,
		TP_DOMAIN * domain, int do_reverse,
		int do_coercion, int total_order, int *start_colp)
{
  /* don't know how to unpack pointers */
  return DB_UNK;
}

static int
mr_cmpval_ptr (DB_VALUE * value1, DB_VALUE * value2,
	       TP_DOMAIN * domain, int do_reverse,
	       int do_coercion, int total_order, int *start_colp)
{
  void *p1, *p2;

  p1 = DB_GET_POINTER (value1);
  p2 = DB_GET_POINTER (value2);

  /* use address for collating sequence */
  return MR_CMP ((unsigned long) p1, (unsigned long) p2, do_reverse, domain);
}

/*
 * TYPE ERROR
 *
 * This is used only for method arguments, they cannot be attribute values.
 */

static void
mr_initmem_error (void *memptr)
{
  int *mem = (int *) memptr;

  *mem = NO_ERROR;
}

static void
mr_initval_error (DB_VALUE * value, int precision, int scale)
{
  db_value_domain_init (value, DB_TYPE_ERROR, precision, scale);
  db_make_error (value, NO_ERROR);
}

static int
mr_setmem_error (void *memptr, TP_DOMAIN * domain, DB_VALUE * value)
{
  int *mem = (int *) memptr;

  if (value == NULL)
    {
      mr_initmem_error (mem);
    }
  else
    {
      *mem = db_get_error (value);
    }
  return (NO_ERROR);
}

static int
mr_getmem_error (void *memptr, TP_DOMAIN * domain, DB_VALUE * value,
		 bool copy)
{
  int *mem = (int *) memptr;

  return db_make_error (value, *mem);
}

static int
mr_setval_error (DB_VALUE * dest, DB_VALUE * src, bool copy)
{
  if (src == NULL || DB_IS_NULL (src))
    {
      PRIM_SET_NULL (dest);
      return NO_ERROR;
    }
  else
    {
      return db_make_error (dest, db_get_error (src));
    }
}

static int
mr_lengthmem_error (void *memptr, TP_DOMAIN * domain, int disk)
{
  return (0);
}

static int
mr_lengthval_error (DB_VALUE * value, int disk)
{
  int error;

  if (value != NULL)
    {
      error = db_get_error (value);
      return mr_lengthmem_error (&error, NULL, disk);
    }
  else
    {
      return NO_ERROR;
    }
}

static void
mr_writemem_error (OR_BUF * buf, void *memptr, TP_DOMAIN * domain)
{
}

static void
mr_readmem_error (OR_BUF * buf, void *memptr, TP_DOMAIN * domain, int size)
{
  int *mem = (int *) memptr;

  *mem = NO_ERROR;
}

static int
mr_writeval_error (OR_BUF * buf, DB_VALUE * value)
{
  return NO_ERROR;
}

static int
mr_readval_error (OR_BUF * buf, DB_VALUE * value,
		  TP_DOMAIN * domain, int size, bool copy,
		  char *copy_buf, int copy_buf_len)
{
  if (value)
    {
      db_value_domain_init (value, DB_TYPE_ERROR,
			    DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE);
      db_make_error (value, NO_ERROR);
    }
  return NO_ERROR;
}

static int
mr_cmpdisk_error (void *mem1, void *mem2,
		  TP_DOMAIN * domain, int do_reverse,
		  int do_coercion, int total_order, int *start_colp)
{
  /* don't know how to unpack errors */
  return DB_UNK;
}

static int
mr_cmpval_error (DB_VALUE * value1, DB_VALUE * value2,
		 TP_DOMAIN * domain, int do_reverse,
		 int do_coercion, int total_order, int *start_colp)
{
  int e1, e2;

  e1 = DB_GET_ERROR (value1);
  e2 = DB_GET_ERROR (value2);

  return MR_CMP (e1, e2, do_reverse, domain);
}


/*
 * TYPE OID
 *
 * DB_TYPE_OID is not really a "domain" type, it is rather a physical
 * representation of an object domain.  Due to the way we dispatch
 * on DB_TYPE_ codes however, we need a fleshed out type vector
 * for this.
 *
 * This is used by the server where we have no DB_OBJECT handles.
 * It can also be used on the client in places where we defer
 * the "swizzling" of OID references (e.g. inside sets).
 *
 * We don't have to handle the case where values come in with DB_TYPE_OBJECT
 * as we do in the _object handlers, true ?
 *
 */

static void
mr_initmem_oid (void *memptr)
{
  OID *mem = (OID *) memptr;

  mr_null_oid (mem);
}

static void
mr_initval_oid (DB_VALUE * value, int precision, int scale)
{
  OID oid;

  mr_null_oid (&oid);
  db_value_domain_init (value, DB_TYPE_OID, precision, scale);
  db_make_oid (value, &oid);
}

static int
mr_setmem_oid (void *memptr, TP_DOMAIN * domain, DB_VALUE * value)
{
  OID *mem = (OID *) memptr;
  OID *oid;

  if (value == NULL)
    {
      mr_initmem_oid (mem);
    }
  else
    {
      oid = db_get_oid (value);
      mem->volid = oid->volid;
      mem->pageid = oid->pageid;
      mem->slotid = oid->slotid;
    }
  return NO_ERROR;
}

static int
mr_getmem_oid (void *memptr, TP_DOMAIN * domain, DB_VALUE * value, bool copy)
{
  OID *mem = (OID *) memptr;
  OID oid;

  oid.volid = mem->volid;
  oid.pageid = mem->pageid;
  oid.slotid = mem->slotid;
  return db_make_oid (value, &oid);
}

static int
mr_setval_oid (DB_VALUE * dest, DB_VALUE * src, bool copy)
{
  if (src == NULL || DB_IS_NULL (src))
    {
      PRIM_SET_NULL (dest);
      return NO_ERROR;
    }
  else
    {
      return db_make_oid (dest, db_get_oid (src));
    }
}

static void
mr_writemem_oid (OR_BUF * buf, void *memptr, TP_DOMAIN * domain)
{
  OID *mem = (OID *) memptr;

  or_put_oid (buf, mem);
}

static void
mr_readmem_oid (OR_BUF * buf, void *memptr, TP_DOMAIN * domain, int size)
{
  OID *mem = (OID *) memptr;
  OID oid;

  if (mem != NULL)
    {
      or_get_oid (buf, mem);
    }
  else
    or_get_oid (buf, &oid);	/* skip over it */
}

static int
mr_writeval_oid (OR_BUF * buf, DB_VALUE * value)
{
  return (or_put_oid (buf, db_get_oid (value)));
}

static int
mr_readval_oid (OR_BUF * buf, DB_VALUE * value,
		TP_DOMAIN * domain, int size, bool copy,
		char *copy_buf, int copy_buf_len)
{
  OID oid;
  int rc = NO_ERROR;

  if (value != NULL)
    {
      db_value_domain_init (value, DB_TYPE_OID,
			    DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE);
      rc = or_get_oid (buf, &oid);
      db_make_oid (value, &oid);
    }
  else
    {
      rc = or_advance (buf, tp_Oid.disksize);
    }

  return rc;
}

static int
mr_cmpdisk_oid (void *mem1, void *mem2,
		TP_DOMAIN * domain, int do_reverse,
		int do_coercion, int total_order, int *start_colp)
{
  int c;
  OID o1, o2;

  OR_GET_OID (mem1, &o1);
  OR_GET_OID (mem2, &o2);

  c = oid_compare (&o1, &o2);
  if (do_reverse || (domain && domain->is_desc))
    {
      c = -c;
    }

  return c;
}

static int
mr_cmpval_oid (DB_VALUE * value1, DB_VALUE * value2,
	       TP_DOMAIN * domain, int do_reverse,
	       int do_coercion, int total_order, int *start_colp)
{
  int c;

  c = oid_compare (DB_GET_OID (value1), DB_GET_OID (value2));
  if (do_reverse || (domain && domain->is_desc))
    {
      c = -c;
    }

  return c;
}

/*
 * TYPE SET
 *
 * This is easily the most complicated primitive type.
 * Sets are defined to be owned by an object.
 * They may be checked out of an object and accessed directly through the
 * set structure.  We need to move maintenance of set ownership down
 * from the object layer to this layer.  This will avoid a lot of special
 * cases for sets that now exist in the pr_ and obj_ layers.
 */


static void
mr_initmem_set (void *memptr)
{
  SETOBJ **mem = (SETOBJ **) memptr;

  *mem = NULL;
}

static void
mr_initval_set (DB_VALUE * value, int precision, int scale)
{
  db_value_domain_init (value, DB_TYPE_SET, precision, scale);
  db_make_set (value, NULL);
}

static int
mr_setmem_set (void *memptr, TP_DOMAIN * domain, DB_VALUE * value)
{
  SETOBJ **mem = (SETOBJ **) memptr;
  int error = NO_ERROR;
  SETOBJ *set;
  SETREF *ref;

  /*
   * NOTE: assumes ownership info has already been placed
   * in the set reference by the caller
   */
  if ((value != NULL) && ((ref = db_get_set (value)) != NULL))
    {
      set = ref->set;
      if (*mem != set)
	{
	  if (*mem != NULL)
	    {
	      error = setobj_release (*mem);
	    }
	  *mem = set;
	  setobj_assigned (set);
	}
    }
  else
    {
      if (*mem != NULL)
	{
	  error = setobj_release (*mem);
	}
      mr_initmem_set (mem);
    }
  return (error);
}

static int
mr_getmem_set (void *memptr, TP_DOMAIN * domain, DB_VALUE * value, bool copy)
{
  SETOBJ **mem = (SETOBJ **) memptr;
  int error = NO_ERROR;
  SETOBJ *set;
  SETREF *ref;

  set = *mem;
  if (set == NULL)
    {
      error = db_make_set (value, NULL);
    }
  else
    {
      ref = setobj_get_reference (set);
      if (ref)
	{
	  error = db_make_set (value, ref);
	}
      else
	{
	  error = er_errid ();
	  (void) db_make_set (value, NULL);
	}
    }
  /*
   * NOTE: assumes that ownership info will already have been set or will
   * be set by the caller
   */

  return (error);
}

static int
mr_setval_set_internal (DB_VALUE * dest, DB_VALUE * src,
			bool copy, DB_TYPE set_type)
{
  int error = NO_ERROR;
  SETREF *src_ref, *ref;

  if ((src != NULL) && !DB_IS_NULL (src) &&
      ((src_ref = db_get_set (src)) != NULL))
    {
      if (!copy)
	{
	  ref = src_ref;
	  /* must increment the reference count */
	  ref->ref_count++;
	}
      else
	{
	  /* need to check if we have a disk_image, if so we just copy it */
	  if (src_ref->disk_set)
	    {
	      ref = set_make_reference ();
	      if (ref == NULL)
		goto err_set;
	      else
		{
		  /* Copy the bits into a freshly allocated buffer. */
		  ref->disk_set =
		    (char *) db_private_alloc (NULL, src_ref->disk_size);
		  if (ref->disk_set == NULL)
		    {
		      goto err_set;
		    }
		  else
		    {
		      ref->need_clear = true;
		      ref->disk_size = src_ref->disk_size;
		      ref->disk_domain = src_ref->disk_domain;
		      memcpy (ref->disk_set, src_ref->disk_set,
			      src_ref->disk_size);
		    }
		}
	    }
	  else
	    {
	      ref = set_copy (src_ref);
	      if (ref == NULL)
		{
		  goto err_set;
		}
	    }
	}

      switch (set_type)
	{
	case DB_TYPE_MULTISET:
	  db_make_multiset (dest, ref);
	  break;
	case DB_TYPE_SET:
	  db_make_set (dest, ref);
	  break;
	case DB_TYPE_SEQUENCE:
	  db_make_sequence (dest, ref);
	  break;
	default:
	  break;
	}
    }
  else
    {
      db_value_domain_init (dest, set_type, 0, 0);
    }
  return (error);

err_set:
  /* couldn't allocate storage for set */
  error = er_errid ();
  switch (set_type)
    {
    case DB_TYPE_MULTISET:
      db_make_multiset (dest, NULL);
      break;
    case DB_TYPE_SET:
      db_make_set (dest, NULL);
      break;
    case DB_TYPE_SEQUENCE:
      db_make_sequence (dest, NULL);
      break;
    default:
      break;
    }
  PRIM_SET_NULL (dest);
  return error;
}

static int
mr_setval_set (DB_VALUE * dest, DB_VALUE * src, bool copy)
{
  return mr_setval_set_internal (dest, src, copy, DB_TYPE_SET);
}

static int
mr_lengthmem_set (void *memptr, TP_DOMAIN * domain, int disk)
{
  int size;

  if (!disk)
    {
      size = tp_Set.size;
    }
  else
    {
      SETOBJ **mem = (SETOBJ **) memptr;

      size = or_packed_set_length (*mem, 0);
    }

  return size;
}

static int
mr_lengthval_set (DB_VALUE * value, int disk)
{
  SETREF *ref;
  SETOBJ *set;
  int size;
#if !defined (SERVER_MODE)
  int pin;
#endif

  size = 0;

  if (!disk)
    {
      size = sizeof (DB_SET *);
    }
  else
    {
      ref = db_get_set (value);
      if (ref != NULL)
	{
	  /* should have a set_ function for this ! */
	  if (ref->disk_set)
	    {
	      size = ref->disk_size;
	    }
	  else if (set_get_setobj (ref, &set, 0) == NO_ERROR)
	    {
	      if (set != NULL)
		{
		  /* probably no need to pin here but it can't hurt */
#if !defined (SERVER_MODE)
		  pin = ws_pin (ref->owner, 1);
#endif
		  size = or_packed_set_length (set, 1);
#if !defined (SERVER_MODE)
		  (void) ws_pin (ref->owner, pin);
#endif
		}
	    }
	}
    }
  return size;
}

static void
mr_writemem_set (OR_BUF * buf, void *memptr, TP_DOMAIN * domain)
{
  SETOBJ **mem = (SETOBJ **) memptr;

  if (*mem != NULL)
    {
      /* note that we don't have to pin the object here since that will have
       * been handled above this leve.
       */
      or_put_set (buf, *mem, 0);
    }
}

static int
mr_writeval_set (OR_BUF * buf, DB_VALUE * value)
{
  SETREF *ref;
  SETOBJ *set;
  int size;
#if !defined (SERVER_MODE)
  int pin;
#endif
  int rc = NO_ERROR;
  ref = db_get_set (value);
  if (ref != NULL)
    {
      /* If we have a disk image of the set, we can just copy those bits
       * here.  This assumes very careful maintenance of the disk and memory
       * images.  Currently, we only have one or the other.  That is, when
       * we transform the disk image to memory, we clear the disk image.
       * See tform_disk_set() in set.c for details.
       */
      if (ref->disk_set)
	{
	  /* TODO: LLP64 problem */
	  /* check for overflow */
	  if (((unsigned long) buf->endptr - (unsigned long) buf->ptr)
	      < (unsigned long) ref->disk_size)
	    {
	      return or_overflow (buf);
	    }
	  else
	    {
	      memcpy (buf->ptr, ref->disk_set, ref->disk_size);
	      rc = or_advance (buf, ref->disk_size);
	    }
	}
      else if (set_get_setobj (ref, &set, 0) == NO_ERROR)
	{
	  if (set != NULL)
	    {
	      if (ref->owner == NULL)
		{
		  or_put_set (buf, set, 1);
		}
	      else
		{
		  /*
		   * We have to be careful and not cause an overflow abort while
		   * we have the owner pinned as this will throw control
		   * out past this function and we won't be able to release
		   * the pin.
		   * To prevent this, check the packed set size first before
		   * we try to pack it, this is annoying and may result in
		   * duplicated set size traversals.
		   * If this becomes a problem, we may have to find a way to
		   * temporarily override the or_abort() environment
		   * similar to the "unwind-protect" concept in lisp.
		   */
#if !defined (SERVER_MODE)
		  pin = ws_pin (ref->owner, 1);
#endif
		  size = or_packed_set_length (set, 1);
		  /* remember the Windows pointer problem ! */
		  if (((unsigned long) buf->endptr -
		       (unsigned long) buf->ptr) < (unsigned long) size)
		    {
		      /* unpin the owner before we abort ! */
#if !defined (SERVER_MODE)
		      (void) ws_pin (ref->owner, pin);
#endif
		      return or_overflow (buf);
		    }
		  else
		    {
		      /* the buffer is ok, do the transformation */
		      or_put_set (buf, set, 1);
		    }
#if !defined (SERVER_MODE)
		  (void) ws_pin (ref->owner, pin);
#endif
		}
	    }
	}
    }
  return rc;
}

static void
mr_readmem_set (OR_BUF * buf, void *memptr, TP_DOMAIN * domain, int size)
{
  SETOBJ **mem = (SETOBJ **) memptr;
  SETOBJ *set;

  if (mem == NULL)
    {
      if (size >= 0)
	{
	  or_advance (buf, size);
	}
      else
	{
	  set = or_get_set (buf, domain);
	  if (set != NULL)
	    {
	      setobj_free (set);
	    }
	}
    }
  else
    {
      if (!size)
	{
	  *mem = NULL;
	}
      else
	{
	  set = or_get_set (buf, domain);
	  if (set != NULL)
	    {
	      *mem = set;
	      setobj_assigned (set);
	    }
	  else
	    {
	      or_abort (buf);
	    }
	}
    }
}

static int
mr_readval_set (OR_BUF * buf, DB_VALUE * value,
		TP_DOMAIN * domain, int size, bool copy,
		char *copy_buf, int copy_buf_len)
{
  SETOBJ *set;
  SETREF *ref;
  int rc = NO_ERROR;

  if (value == NULL)
    {
      if (size == -1)
	{
	  /* don't know the true size, must unpack the set and throw it away */
	  set = or_get_set (buf, domain);
	  if (set != NULL)
	    {
	      setobj_free (set);
	    }
	  else
	    {
	      or_abort (buf);
	      return ER_FAILED;
	    }
	}
      else
	{
	  if (size)
	    {
	      rc = or_advance (buf, size);
	    }
	}
    }
  else
    {
      /* In some cases, like VOBJ reading, the domain passed is NULL here so
       * be careful when initializing the value.  If it is NULL, it will be
       * read when the set is unpacked.
       */
      if (!domain)
	{
	  db_value_domain_init (value, DB_TYPE_SET,
				DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE);
	}
      else
	{
	  db_value_domain_init (value, domain->type->id,
				domain->precision, domain->scale);
	}

      /* If size is zero, we have nothing to do, if size is -1,
       * just go ahead and unpack the set.
       */
      if (!size)
	{
	  db_make_set (value, NULL);
	}
      else if (copy)
	{
	  set = or_get_set (buf, domain);
	  if (set == NULL)
	    {
	      or_abort (buf);
	      return ER_FAILED;
	    }
	  else
	    {
	      ref = setobj_get_reference (set);
	      if (ref == NULL)
		{
		  or_abort (buf);
		  return ER_FAILED;
		}
	      else
		{
		  switch (set_get_type (ref))
		    {
		    case DB_TYPE_MULTISET:
		      db_make_multiset (value, ref);
		      break;
		    case DB_TYPE_SET:
		      db_make_set (value, ref);
		      break;
		    case DB_TYPE_SEQUENCE:
		      db_make_sequence (value, ref);
		      break;
		    default:
		      break;
		    }
		}
	    }
	}
      else
	{
	  /* copy == false, which means don't translate it into memory rep */
	  ref = set_make_reference ();
	  if (ref == NULL)
	    {
	      or_abort (buf);
	      return ER_FAILED;
	    }
	  else
	    {
	      int disk_size;
	      DB_TYPE set_type;

	      if (size != -1)
		{
		  char *set_st;
		  int num_elements, has_domain, bound_bits, offset_tbl,
		    el_tags;

		  disk_size = size;

		  /* unfortunately, we still need to look at the header to
		   * find out the set type.
		   */
		  set_st = buf->ptr;
		  or_get_set_header (buf, &set_type, &num_elements,
				     &has_domain, &bound_bits, &offset_tbl,
				     &el_tags, NULL);

		  /* reset the OR_BUF */
		  buf->ptr = set_st;
		}
	      else
		{
		  /* we have to go figure the size out */
		  disk_size = or_disk_set_size (buf, domain, &set_type);
		}

	      /* Record the pointer to the disk bits */
	      ref->disk_set = buf->ptr;
	      ref->need_clear = false;
	      ref->disk_size = disk_size;
	      ref->disk_domain = domain;

	      /* advance the buffer as if we had read the set */
	      rc = or_advance (buf, disk_size);

	      switch (set_type)
		{
		case DB_TYPE_MULTISET:
		  db_make_multiset (value, ref);
		  break;
		case DB_TYPE_SET:
		  db_make_set (value, ref);
		  break;
		case DB_TYPE_SEQUENCE:
		  db_make_sequence (value, ref);
		  break;
		default:
		  break;
		}
	    }
	}
    }
  return rc;
}

static void
mr_freemem_set (void *memptr)
{
  /* since we aren't explicitly setting the set to NULL,
     we must set up the reference structures so they will get
     the new set when it is brought back in, this is the
     only primitive type for which the free function is
     semantically different than using the setmem function
     with a NULL value
   */

  SETOBJ **mem = (SETOBJ **) memptr;

  if (*mem != NULL)
    {
      setobj_free (*mem);	/* free storage, NULL references */
    }
}

static int
mr_cmpdisk_set (void *mem1, void *mem2,
		TP_DOMAIN * domain, int do_reverse,
		int do_coercion, int total_order, int *start_colp)
{
  int c;
  SETOBJ *set1, *set2;

  mem1 = or_unpack_set ((char *) mem1, &set1, domain);
  mem2 = or_unpack_set ((char *) mem2, &set2, domain);

  c = setobj_compare_order (set1, set2, do_coercion, total_order);
  if (do_reverse || (domain && domain->is_desc))
    {
      c = -c;
    }

  setobj_free (set1);
  setobj_free (set2);

  return c;
}

static int
mr_cmpval_set (DB_VALUE * value1, DB_VALUE * value2,
	       TP_DOMAIN * domain, int do_reverse,
	       int do_coercion, int total_order, int *start_colp)
{
  int c;

  c =
    set_compare_order (db_get_set (value1), db_get_set (value2), do_coercion,
		       total_order);
  if (do_reverse || (domain && domain->is_desc))
    {
      c = -c;
    }

  return c;
}

/*
 * TYPE MULTISET
 */

static void
mr_initval_multiset (DB_VALUE * value, int precision, int scale)
{
  db_value_domain_init (value, DB_TYPE_MULTISET, precision, scale);
  db_make_multiset (value, NULL);
}

static int
mr_getmem_multiset (void *memptr, TP_DOMAIN * domain,
		    DB_VALUE * value, bool copy)
{
  SETOBJ **mem = (SETOBJ **) memptr;
  int error = NO_ERROR;
  SETOBJ *set;
  SETREF *ref;

  set = *mem;
  if (set == NULL)
    {
      error = db_make_multiset (value, NULL);
    }
  else
    {
      ref = setobj_get_reference (set);
      if (ref)
	{
	  error = db_make_multiset (value, ref);
	}
      else
	{
	  error = er_errid ();
	  (void) db_make_multiset (value, NULL);
	}
    }
  /* NOTE: assumes that ownership info will already have been set or will
     be set by the caller */

  return (error);
}

static int
mr_setval_multiset (DB_VALUE * dest, DB_VALUE * src, bool copy)
{
  return mr_setval_set_internal (dest, src, copy, DB_TYPE_MULTISET);
}

/*
 * TYPE SEQUENCE
 */

static void
mr_initval_sequence (DB_VALUE * value, int precision, int scale)
{
  db_value_domain_init (value, DB_TYPE_SEQUENCE, precision, scale);
  db_make_sequence (value, NULL);
}

static int
mr_getmem_sequence (void *memptr, TP_DOMAIN * domain,
		    DB_VALUE * value, bool copy)
{
  SETOBJ **mem = (SETOBJ **) memptr;
  int error = NO_ERROR;
  SETOBJ *set;
  SETREF *ref;

  set = *mem;
  if (set == NULL)
    {
      error = db_make_sequence (value, NULL);
    }
  else
    {
      ref = setobj_get_reference (set);
      if (ref)
	{
	  error = db_make_sequence (value, ref);
	}
      else
	{
	  error = er_errid ();
	  (void) db_make_sequence (value, NULL);
	}
    }
  /*
   * NOTE: assumes that ownership info will already have been set or will
   * be set by the caller
   */

  return (error);
}

static int
mr_setval_sequence (DB_VALUE * dest, DB_VALUE * src, bool copy)
{
  return mr_setval_set_internal (dest, src, copy, DB_TYPE_SEQUENCE);
}

static int
mr_cmpdisk_sequence (void *mem1, void *mem2,
		     TP_DOMAIN * domain, int do_reverse,
		     int do_coercion, int total_order, int *start_colp)
{
  int c;
  SETOBJ *seq1, *seq2;

  mem1 = or_unpack_set ((char *) mem1, &seq1, domain);
  mem2 = or_unpack_set ((char *) mem2, &seq2, domain);

  c = setobj_compare_order (seq1, seq2, do_coercion, total_order);
  if (do_reverse || (domain && domain->is_desc))
    {
      c = -c;
    }

  setobj_free (seq1);
  setobj_free (seq2);

  return c;
}

static int
mr_cmpval_sequence (DB_VALUE * value1, DB_VALUE * value2,
		    TP_DOMAIN * domain, int do_reverse,
		    int do_coercion, int total_order, int *start_colp)
{
  int c;

  c = set_seq_compare (db_get_set (value1), db_get_set (value2), do_coercion,
		       total_order);
  if (do_reverse || (domain && domain->is_desc))
    {
      c = -c;
    }

  return c;
}

/*
 * TYPE MIDXKEY
 */

static void
mr_initval_midxkey (DB_VALUE * value, int precision, int scale)
{
  DB_MIDXKEY *midxkey = NULL;
  db_value_domain_init (value, DB_TYPE_MIDXKEY, precision, scale);
  db_make_midxkey (value, midxkey);
}

static int
mr_setval_midxkey (DB_VALUE * dest, DB_VALUE * src, bool copy)
{
  int error = NO_ERROR;
  int src_precision;

  DB_MIDXKEY dst_idx;
  DB_MIDXKEY *src_idx;

  if (src == NULL || DB_IS_NULL (src))
    {
      return db_value_domain_init (dest, DB_TYPE_MIDXKEY,
				   DB_DEFAULT_PRECISION, 0);
    }

  /* Get information from the value. */
  src_idx = DB_GET_MIDXKEY (src);
  src_precision = db_value_precision (src);
  if (src_idx == NULL)
    {
      return db_value_domain_init (dest, DB_TYPE_MIDXKEY, src_precision, 0);
    }

  if (src_idx->size < 0)
    {
      dst_idx.size = strlen (src_idx->buf);
    }
  else
    {
      dst_idx.size = src_idx->size;
    }

  dst_idx.ncolumns = src_idx->ncolumns;

  dst_idx.domain = src_idx->domain;

  /* should we be paying attention to this? it is extremely dangerous */
  if (!copy)
    {
      dst_idx.buf = src_idx->buf;

      error = db_make_midxkey (dest, &dst_idx);
      dest->need_clear = false;
    }
  else
    {
      dst_idx.buf = db_private_alloc (NULL, dst_idx.size);
      if (dst_idx.buf == NULL)
	{
	  db_value_domain_init (dest, DB_TYPE_MIDXKEY, src_precision, 0);
	  return er_errid ();
	}

      memcpy (dst_idx.buf, src_idx->buf, dst_idx.size);
      error = db_make_midxkey (dest, &dst_idx);
      dest->need_clear = true;
    }

  return error;
}

static int
mr_writeval_midxkey (OR_BUF * buf, DB_VALUE * value)
{
  DB_MIDXKEY *midxkey;
  int rc;

  midxkey = DB_GET_MIDXKEY (value);
  if (midxkey == NULL)
    {
      return ER_FAILED;
    }

  rc = or_put_data (buf, (char *) midxkey->buf, midxkey->size);

  return rc;
}

static int
mr_readval_midxkey (OR_BUF * buf, DB_VALUE * value,
		    TP_DOMAIN * domain, int size, bool copy,
		    char *copy_buf, int copy_buf_len)
{
  char *new_;
  DB_MIDXKEY midxkey;
  int rc = NO_ERROR;
  TP_DOMAIN *dom;

  if (value == NULL)
    {
      if (size <= 0)
	{
	  return ER_FAILED;
	}
      return or_advance (buf, size);
    }

  if (size == -1)
    {				/* unknown size */
      if (domain->type->lengthmem != NULL)
	{
	  size = (*(domain->type->lengthmem)) (buf->ptr, domain, 1);
	}
    }

  if (size <= 0)
    {
      return ER_FAILED;
    }

  midxkey.size = size;
  midxkey.ncolumns = 0;
  midxkey.domain = domain;

  for (dom = domain->setdomain; dom; dom = dom->next)
    {
      midxkey.ncolumns += 1;
    }

  if (!copy)
    {
      midxkey.buf = buf->ptr;
      db_make_midxkey (value, &midxkey);
      value->need_clear = false;
      rc = or_advance (buf, size);
    }
  else
    {
      if (copy_buf && copy_buf_len >= size)
	{
	  /* read buf image into the copy_buf */
	  new_ = copy_buf;
	}
      else
	{
	  /*
	   * Allocate storage for the string
	   * do not include the kludge NULL terminator
	   */
	  new_ = db_private_alloc (NULL, size);
	}

      if (new_ == NULL)
	{
	  /* need to be able to return errors ! */
	  db_value_domain_init (value, domain->type->id,
				TP_FLOATING_PRECISION_VALUE, 0);
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
		  0);
	  or_abort (buf);
	  return ER_FAILED;
	}
      else
	{
	  rc = or_get_data (buf, new_, size);
	  if (rc == NO_ERROR)
	    {
	      /* round up to a word boundary */
	      rc = or_get_align32 (buf);
	    }
	  if (rc != NO_ERROR)
	    {
	      if (new_ != copy_buf)
		{
		  db_private_free_and_init (NULL, new_);
		}
	      return rc;
	    }
	  midxkey.buf = new_;
	  db_make_midxkey (value, &midxkey);
	  value->need_clear = (new_ != copy_buf) ? true : false;
	}
    }

  return rc;
}

static int
compare_diskvalue (char *mem1, char *mem2,
		   TP_DOMAIN * dom1, TP_DOMAIN * dom2,
		   int do_coercion, int total_order)
{
  int c;
  DB_VALUE val1, val2;
  OR_BUF buf_val1, buf_val2;

  if (dom1->is_desc != dom2->is_desc)
    {
      return DB_UNK;		/* impossible case */
    }

  OR_BUF_INIT (buf_val1, mem1, -1);
  OR_BUF_INIT (buf_val2, mem2, -1);

  if ((*(dom1->type->readval)) (&buf_val1, &val1, dom1, -1, false,
				NULL, 0) != NO_ERROR)
    {
      return DB_UNK;
    }

  if ((*(dom2->type->readval)) (&buf_val2, &val2, dom2, -1, false,
				NULL, 0) != NO_ERROR)
    {
      return DB_UNK;
    }

  c = tp_value_compare (&val1, &val2, do_coercion, total_order);

  return c;
}

static int
compare_midxkey (DB_MIDXKEY * mul1, DB_MIDXKEY * mul2,
		 TP_DOMAIN * domain, int do_reverse,
		 int do_coercion, int total_order, int *start_colp)
{
  int c = DB_UNK;
  bool need_reverse = true;	/* guess as true */
  int i;
  int adv_size1 = 0;
  int adv_size2 = 0;
  TP_DOMAIN *dom1, *dom2;

  char *buf1, *buf2;
  char *bitptr1, *bitptr2;
  char *mem1, *mem2;

  /* domain can be NULL; is partial-key cmp or EQ check */
  if (domain)
    {
      if (domain->type->id != DB_TYPE_MIDXKEY)
	{			/* safe guard */
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_MR_NULL_DOMAIN, 0);
	  return DB_UNK;
	}
      domain = domain->setdomain;
    }
  else
    {
      if (do_reverse)
	{			/* safe guard */
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_MR_NULL_DOMAIN, 0);
	  return DB_UNK;
	}
    }

  mem1 = bitptr1 = mul1->buf;
  mem2 = bitptr2 = mul2->buf;

  if (mul1->ncolumns != mul2->ncolumns)
    {
      return DB_UNK;
    }

  adv_size1 = OR_MULTI_BOUND_BIT_BYTES (mul1->ncolumns);
  mem1 += adv_size1;
  mem2 += adv_size1;

  dom1 = mul1->domain->setdomain;
  dom2 = mul2->domain->setdomain;

  for (i = 0; i < mul1->ncolumns; i++)
    {
      need_reverse = true;	/* guess as true */

      c = DB_EQ;		/* init */

      /* consume equal-value columns */
      if (start_colp)
	{
	  if (i < *start_colp)
	    {
	      if (OR_MULTI_ATT_IS_UNBOUND (bitptr1, i)
		  && OR_MULTI_ATT_IS_UNBOUND (bitptr2, i))
		{
		  /* check for val1, val2 is NULL for total_order */
		  if (total_order)
		    {
		      goto check_done;	/* skip and go ahead */
		    }
		}
	      goto check_equal;
	    }
	}

      /* val1 or val2 is NULL */
      if (OR_MULTI_ATT_IS_UNBOUND (bitptr1, i))
	{			/* element val is null? */
	  if (OR_MULTI_ATT_IS_UNBOUND (bitptr2, i))
	    {
	      c = (total_order ? DB_EQ : DB_UNK);
	      /* check for val1, val2 is NULL for total_order */
	      if (total_order)
		{
		  goto check_done;	/* skip and go ahead */
		}
	    }
	  else
	    {
	      c = (total_order ? DB_LT : DB_UNK);
	    }
	  goto check_equal;
	}
      else if (OR_MULTI_ATT_IS_UNBOUND (bitptr2, i))
	{
	  c = (total_order ? DB_GT : DB_UNK);
	  goto check_equal;
	}

      /* at here, val1 and val2 is non-NULL */

      /* check for val1 and val2 same domain */
      if (dom1 == dom2 || tp_domain_match (dom1, dom2, TP_EXACT_MATCH))
	{
	  c = (*(dom1->type->cmpdisk)) (mem1, mem2,
					dom1, do_reverse,
					do_coercion, total_order, NULL);
	  if (domain)
	    {			/* is full-key range cmp */
	      ;			/* OK */
	    }
	  else
	    {			/* is partial-key cmp or EQ check */
	      if (dom1->is_desc)
		{
		  c = -c;	/* do not consider desc order */
		}
	    }
	  need_reverse = false;	/* already done */
	}
      else
	{			/* coercion and comparision */
	  /* val1 and val2 have different domain */
	  c = compare_diskvalue (mem1, mem2, dom1, dom2,
				 do_coercion, total_order);
	}

    check_equal:

      if (c != DB_EQ)
	{
	  break;		/* exit for-loop */
	}

      adv_size1 = pr_writemem_disk_size (mem1, dom1);
      adv_size2 = pr_writemem_disk_size (mem2, dom2);

      mem1 += (DB_ALIGN (adv_size1, OR_INT_SIZE));
      mem2 += (DB_ALIGN (adv_size2, OR_INT_SIZE));

    check_done:

      /* at here, both val1 and val2 can be NULL */

      if (domain)
	{			/* is full-key range cmp */
	  domain = domain->next;
	}
      dom1 = dom1->next;
      dom2 = dom2->next;
    }				/* for */

  if (need_reverse)
    {
      if (do_reverse || (domain && domain->is_desc))
	{
	  c = -c;
	}
    }

  if (start_colp)
    {
      if (c != DB_EQ)
	{
	  /* save the start position of non-equal-value column */
	  *start_colp = i;
	}
    }

  return c;
}

static int
mr_cmpval_midxkey (DB_VALUE * value1, DB_VALUE * value2,
		   TP_DOMAIN * domain, int do_reverse,
		   int do_coercion, int total_order, int *start_colp)
{
  int c = DB_UNK;
  bool need_reverse = true;	/* guess as true */
  DB_MIDXKEY *midxkey1;
  DB_MIDXKEY *midxkey2;

  midxkey1 = DB_GET_MIDXKEY (value1);
  midxkey2 = DB_GET_MIDXKEY (value2);

  if (midxkey1 == NULL)
    {
      if (midxkey2 == NULL)
	{
	  if (total_order)
	    {
	      c = DB_EQ;
	    }
	  else
	    {
	      c = DB_UNK;
	    }
	}
      else
	{
	  if (total_order)
	    {
	      c = DB_LT;
	    }
	  else
	    {
	      c = DB_UNK;
	    }
	}
    }
  else if (midxkey2 == NULL)
    {
      if (total_order)
	{
	  c = DB_GT;
	}
      else
	{
	  c = DB_UNK;
	}
    }
  else if (midxkey1 == midxkey2)
    {
      if (total_order)
	{
	  c = DB_EQ;
	}
      else
	{
	  /* error */
	}
    }
  else
    {
      c = compare_midxkey (midxkey1, midxkey2,
			   domain, do_reverse,
			   do_coercion, total_order, start_colp);
      need_reverse = false;	/* already done */
    }

  if (need_reverse)
    {
      if (do_reverse || (domain && domain->is_desc))
	{
	  c = -c;
	}
    }

  return c;
}


static int
mr_cmpdisk_midxkey (void *mem1, void *mem2,
		    TP_DOMAIN * domain, int do_reverse,
		    int do_coercion, int total_order, int *start_colp)
{
  int c = DB_UNK;
  bool need_reverse = true;	/* guess as true */
  DB_MIDXKEY midxkey1;
  DB_MIDXKEY midxkey2;
  TP_DOMAIN *cmp_dom;
  int n_atts = 0;

  if (mem1 == NULL)
    {
      if (mem2 == NULL)
	{
	  if (total_order)
	    {
	      c = DB_EQ;
	    }
	  else
	    {
	      c = DB_UNK;
	    }
	}
      else
	{
	  if (total_order)
	    {
	      c = DB_LT;
	    }
	  else
	    {
	      c = DB_UNK;
	    }
	}
    }
  else if (mem2 == NULL)
    {
      if (total_order)
	{
	  c = DB_GT;
	}
      else
	{
	  c = DB_UNK;
	}
    }
  else if (mem1 == mem2)
    {
      if (total_order)
	{
	  c = DB_EQ;
	}
    }
  else
    {
      midxkey1.buf = (char *) mem1;
      midxkey2.buf = (char *) mem2;

      for (cmp_dom = domain->setdomain; cmp_dom; cmp_dom = cmp_dom->next)
	{
	  n_atts += 1;
	}

      midxkey1.size = midxkey2.size = -1;
      midxkey1.ncolumns = midxkey2.ncolumns = n_atts;
      midxkey1.domain = midxkey2.domain = domain;

      c = compare_midxkey (&midxkey1, &midxkey2,
			   domain, do_reverse,
			   do_coercion, total_order, start_colp);
      need_reverse = false;	/* already done */
    }

  if (need_reverse)
    {
      if (do_reverse || (domain && domain->is_desc))
	{
	  c = -c;
	}
    }

  return c;
}

static int
mr_lengthmem_midxkey (void *memptr, TP_DOMAIN * domain, int disk)
{
  char *buf, *bitptr;
  TP_DOMAIN *dom;
  int i, ncolumns, adv_size;
  int len;

  /* There is no difference between the disk & memory sizes. */

  buf = (char *) memptr;

  ncolumns = 0;			/* init */
  for (dom = domain->setdomain; dom; dom = dom->next)
    {
      ncolumns++;
    }

  if (ncolumns <= 0)
    {
      goto exit_on_error;	/* give up */
    }
  adv_size = OR_MULTI_BOUND_BIT_BYTES (ncolumns);

  bitptr = buf;
  buf += adv_size;

  for (i = 0, dom = domain->setdomain; i < ncolumns; i++, dom = dom->next)
    {
      /* check for val is NULL */
      if (OR_MULTI_ATT_IS_UNBOUND (bitptr, i))
	{
	  continue;		/* zero size; go ahead */
	}

      /* at here, val is non-NULL */

      adv_size = pr_writemem_disk_size (buf, dom);

      DB_ALIGN (adv_size, OR_INT_SIZE);
      buf += adv_size;
    }				/* for (i = 0, dom = domain->setdomain; ...) */

  /* set buf size */
  len = buf - bitptr;

exit_on_end:

  return len;

exit_on_error:

  len = -1;			/* set error */
  goto exit_on_end;
}

static int
mr_lengthval_midxkey (DB_VALUE * value, int disk)
{
  int len;

  if (!value || value->domain.general_info.is_null)
    {
      return 0;
    }
  len = value->data.midxkey.size;

  return len;
}

/*
 * TYPE VOBJ
 *
 * This is used only for virtual object keys in SQL/M.
 * Internal structures identical to sequences.
 */

static void
mr_initval_vobj (DB_VALUE * value, int precision, int scale)
{
  db_value_domain_init (value, DB_TYPE_VOBJ, precision, scale);
}

static int
mr_setval_vobj (DB_VALUE * dest, DB_VALUE * src, bool copy)
{
  int error;

  error = mr_setval_sequence (dest, src, copy);
  db_value_alter_type (dest, DB_TYPE_VOBJ);

  return error;
}

static int
mr_readval_vobj (OR_BUF * buf, DB_VALUE * value,
		 TP_DOMAIN * domain, int size, bool copy,
		 char *copy_buf, int copy_buf_len)
{
  if (mr_readval_set (buf, value, &tp_Sequence_domain, size, copy,
		      copy_buf, copy_buf_len) != NO_ERROR)
    {
      return ER_FAILED;
    }

  if (value)
    {
      db_value_alter_type (value, DB_TYPE_VOBJ);
    }
  return NO_ERROR;
}

static int
mr_cmpdisk_vobj (void *mem1, void *mem2,
		 TP_DOMAIN * domain, int do_reverse,
		 int do_coercion, int total_order, int *start_colp)
{
  int c;
  SETOBJ *seq1, *seq2;

  mem1 = or_unpack_set ((char *) mem1, &seq1, domain);
  mem2 = or_unpack_set ((char *) mem2, &seq2, domain);

  c = setvobj_compare (seq1, seq2, do_coercion, total_order);
  if (do_reverse || (domain && domain->is_desc))
    {
      c = -c;
    }

  setobj_free (seq1);
  setobj_free (seq2);

  return c;
}

static int
mr_cmpval_vobj (DB_VALUE * value1, DB_VALUE * value2,
		TP_DOMAIN * domain, int do_reverse,
		int do_coercion, int total_order, int *start_colp)
{
  int c;

  c = vobj_compare (db_get_set (value1), db_get_set (value2), do_coercion,
		    total_order);
  if (do_reverse || (domain && domain->is_desc))
    {
      c = -c;
    }

  return c;
}

/*
 * TYPE NUMERIC
 */

static void
mr_initmem_numeric (void *memptr)
{
  /*
   * could wip through the buffer and set the bytes to zero, need a domain
   * here !
   */
}

/*
 * Due to the "within tolerance" domain comparison used during attribute
 * assignment validation, we may receive a numeric whose precision is less
 * then the actual precision of the attribute.  In that case we should be doing
 * an on-the-fly coercion here.
 */
static int
mr_setmem_numeric (void *mem, TP_DOMAIN * domain, DB_VALUE * value)
{
  int error = NO_ERROR;
  int src_precision, src_scale, byte_size;
  DB_C_NUMERIC num, src_num;

  if (value == NULL)
    {
      mr_initmem_numeric (mem);
    }
  else
    {
      src_num = DB_GET_NUMERIC (value);

      src_precision = db_value_precision (value);
      src_scale = db_value_scale (value);

      /* this should have been handled by now */
      if (src_precision != domain->precision || src_scale != domain->scale)
	{
	  error = ER_OBJ_DOMAIN_CONFLICT;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, "");
	}
      else
	{
	  num = (DB_C_NUMERIC) mem;
	  byte_size = MR_NUMERIC_SIZE (src_precision);
	  memcpy (num, src_num, byte_size);
	}
    }
  return error;
}

static int
mr_getmem_numeric (void *mem, TP_DOMAIN * domain, DB_VALUE * value, bool copy)
{
  int error = NO_ERROR;
  DB_C_NUMERIC num;

  if (value == NULL)
    {
      return error;
    }

  num = (DB_C_NUMERIC) mem;
  error = db_make_numeric (value, num, domain->precision, domain->scale);
  value->need_clear = false;

  return error;
}

static void
mr_writemem_numeric (OR_BUF * buf, void *mem, TP_DOMAIN * domain)
{
  int disk_size;

  disk_size = OR_NUMERIC_SIZE (domain->precision);
  or_put_data (buf, (char *) mem, disk_size);
}

static void
mr_readmem_numeric (OR_BUF * buf, void *mem, TP_DOMAIN * domain, int size)
{

  /* if stored size is unknown, the domain precision must be set correctly */
  if (size < 0)
    {
      size = OR_NUMERIC_SIZE (domain->precision);
    }

  if (mem == NULL)
    {
      if (size)
	{
	  or_advance (buf, size);
	}
    }
  else if (size)
    {
      if (size != OR_NUMERIC_SIZE (domain->precision))
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SM_CORRUPTED, 0);
	  or_abort (buf);
	}
      else
	{
	  or_get_data (buf, (char *) mem, size);
	}
    }
}

static int
mr_lengthmem_numeric (void *mem, TP_DOMAIN * domain, int disk)
{
  int len;

  /* think about caching this in the domain so we don't have to calculate it */
  if (disk)
    {
      len = OR_NUMERIC_SIZE (domain->precision);
    }
  else
    {
      len = MR_NUMERIC_SIZE (domain->precision);
    }

  return len;
}

static void
mr_initval_numeric (DB_VALUE * value, int precision, int scale)
{
  db_value_domain_init (value, DB_TYPE_NUMERIC, precision, scale);
}

static int
mr_setval_numeric (DB_VALUE * dest, DB_VALUE * src, bool copy)
{
  int error = NO_ERROR;
  int src_precision, src_scale;
  DB_C_NUMERIC src_numeric;

  if (src == NULL)
    {
      db_value_domain_init (dest, DB_TYPE_NUMERIC,
			    DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE);
    }
  else
    {
      src_precision = db_value_precision (src);
      src_scale = db_value_scale (src);
      src_numeric = DB_GET_NUMERIC (src);

      if (DB_IS_NULL (src) || src_numeric == NULL)
	{
	  db_value_domain_init (dest, DB_TYPE_NUMERIC, src_precision,
				src_scale);
	}
      else
	{
	  /*
	   * Because numerics are stored in an inline buffer, there is no
	   * difference between the copy and non-copy operations, this may
	   * need to change.
	   */
	  error =
	    db_make_numeric (dest, src_numeric, src_precision, src_scale);
	}
    }
  return error;
}

static int
mr_lengthval_numeric (DB_VALUE * value, int disk)
{
  int precision, len;

  len = 0;
  if (value != NULL)
    {
      /* better have a non-NULL value by the time writeval is called ! */
      precision = db_value_precision (value);
      if (disk)
	len = OR_NUMERIC_SIZE (precision);
      else
	len = MR_NUMERIC_SIZE (precision);
    }
  return len;
}

static int
mr_writeval_numeric (OR_BUF * buf, DB_VALUE * value)
{
  DB_C_NUMERIC numeric;
  int precision, disk_size;
  int rc = NO_ERROR;

  if (value != NULL)
    {
      numeric = DB_GET_NUMERIC (value);
      if (numeric != NULL)
	{
	  precision = db_value_precision (value);
	  disk_size = OR_NUMERIC_SIZE (precision);
	  rc = or_put_data (buf, (char *) numeric, disk_size);
	}
    }
  return rc;
}

static int
mr_readval_numeric (OR_BUF * buf, DB_VALUE * value,
		    TP_DOMAIN * domain, int size, bool copy,
		    char *copy_buf, int copy_buf_len)
{
  int rc = NO_ERROR;
  /*
   * If size is -1, the caller doesn't know the size and we must determine
   * it from the domain.
   */
  if (size == -1)
    {
      size = OR_NUMERIC_SIZE (domain->precision);
    }

  /*
   * KLUDGE!
   * There are some spots in qp_xdata.c that I wasn't able to modify that
   * will pass in a size of 1 here when reading tuple values.
   */
  if (size == 1)
    size = OR_NUMERIC_SIZE (domain->precision);

  if (value == NULL)
    {
      if (size)
	{
	  rc = or_advance (buf, size);
	}
    }
  else
    {
      /*
       * the copy and no copy cases are identical because db_make_numeric
       * will copy the bits into its internal buffer.
       */
      (void) db_make_numeric (value, (DB_C_NUMERIC) buf->ptr,
			      domain->precision, domain->scale);
      value->need_clear = false;
      rc = or_advance (buf, size);
    }
  return rc;
}

static int
mr_cmpdisk_numeric (void *mem1, void *mem2,
		    TP_DOMAIN * domain, int do_reverse,
		    int do_coercion, int total_order, int *start_colp)
{
  int c = DB_UNK;
  OR_BUF buf;
  DB_VALUE value1, value2;
  DB_VALUE answer;
  int rc = NO_ERROR;

  or_init (&buf, (char *) mem1, 0);
  rc = mr_readval_numeric (&buf, &value1, domain, -1, 0, NULL, 0);
  if (rc == NO_ERROR)
    {
      or_init (&buf, (char *) mem2, 0);
      rc = mr_readval_numeric (&buf, &value2, domain, -1, 0, NULL, 0);
      if (rc == NO_ERROR)
	{
	  (void) numeric_db_value_compare (&value1, &value2, &answer);

	  if (DB_GET_INT (&answer) < 0)
	    {
	      c = DB_LT;
	    }
	  else
	    {
	      if (DB_GET_INT (&answer) > 0)
		{
		  c = DB_GT;
		}
	      else
		{
		  c = DB_EQ;
		}
	    }
	  if (do_reverse || (domain && domain->is_desc))
	    {
	      c = -c;
	    }
	}
    }

  return c;
}

static int
mr_cmpval_numeric (DB_VALUE * value1, DB_VALUE * value2,
		   TP_DOMAIN * domain, int do_reverse,
		   int do_coercion, int total_order, int *start_colp)
{
  int c;
  DB_VALUE answer;

  (void) numeric_db_value_compare (value1, value2, &answer);

  if (DB_GET_INT (&answer) < 0)
    {
      c = DB_LT;
    }
  else
    {
      if (DB_GET_INT (&answer) > 0)
	{
	  c = DB_GT;
	}
      else
	{
	  c = DB_EQ;
	}
    }

  if (do_reverse || (domain && domain->is_desc))
    {
      c = -c;
    }

  return c;
}

/*
 * PRIMITIVE TYPE SUPPORT ROUTINES
 */



/*
 * pr_init_ordered_mem_sizes - orders the sizes of primitive types in
 * descending order.
 *    return: void
 */
static void
pr_init_ordered_mem_sizes (void)
{
  int t, last_size, cur_size;

  pr_ordered_mem_size_total = 0;

  last_size = 500;
  while (1)
    {
      cur_size = -1;
      for (t = 0; t < PR_TYPE_TOTAL; ++t)
	{
	  if ((tp_Type_id_map[t]->size > cur_size) &&
	      (tp_Type_id_map[t]->size < last_size))
	    {
	      cur_size = tp_Type_id_map[t]->size;
	    }
	}
      pr_ordered_mem_sizes[pr_ordered_mem_size_total++] = last_size =
	cur_size;
      if (cur_size <= 0)
	{
	  break;
	}
    }
}


/*
 * pr_type_from_id - maps a type identifier such as DB_TYPE_INTEGER into its
 * corresponding primitive type descriptor structures.
 *    return: type descriptor
 *    id(in): type identifier constant
 */
PR_TYPE *
pr_type_from_id (DB_TYPE id)
{
  PR_TYPE *type = NULL;

  /* Start ignoring DB_TYPE_LAST until we can build the whole system
   * to accomodate removing the last marker.
   */
/*  if (id >= DB_TYPE_FIRST && id < DB_TYPE_LAST) */
  if (id <= DB_TYPE_MIDXKEY)
    {
      type = tp_Type_id_map[(int) id];
    }

  return (type);
}


/*
 * pr_type_name - Returns the string type name associated with a type constant.
 *    return: type name
 *    id(in): type identifier constant
 * Note:
 *    The string must not be freed after use.
 */
const char *
pr_type_name (DB_TYPE id)
{
  const char *name = NULL;
  PR_TYPE *type;

  type = PR_TYPE_FROM_ID (id);

  if (type != NULL)
    {
      name = type->name;
    }

  return (name);
}


/*
 * pr_is_set_type - Test to see if a type identifier is one of the set types.
 *    return: non-zero if type is one of the set types
 *    type(in):
 * Note:
 *    Since there is an unfortunate amount of special processing for
 *    the set types, this takes care of comparing against all three types.
 */
int
pr_is_set_type (DB_TYPE type)
{
  int status = 0;

  if (type == DB_TYPE_SET || type == DB_TYPE_MULTISET ||
      type == DB_TYPE_SEQUENCE || type == DB_TYPE_VOBJ)
    {
      status = 1;
    }

  return (status);
}


/*
 * pr_is_string_type - Test to see if a type identifier is one of the string
 * types.
 *    return: non-zero if type is one of the string types
 *    type(in):  type to check
 */
int
pr_is_string_type (DB_TYPE type)
{
  int status = 0;

  if (type == DB_TYPE_VARCHAR || type == DB_TYPE_CHAR ||
      type == DB_TYPE_VARNCHAR || type == DB_TYPE_NCHAR ||
      type == DB_TYPE_VARBIT || type == DB_TYPE_BIT)
    {
      status = 1;
    }

  return (status);
}


/*
 * pr_is_variable_type - determine whether or not a type is fixed or variable
 * width on disk.
 *    return: non-zero if this is a variable width type
 *    id(in): type id
 * Note:
 *    With the advent of parameterized types like CHAR(n), NUMERIC(p,s) etc.
 *    this doesn't mean that all values of this type will be the same size,
 *    it means that for any particular attribute of a class, they will all be
 *    the same size and the value will be stored in the "fixed" region of the
 *    disk representation.
 */
int
pr_is_variable_type (DB_TYPE id)
{
  PR_TYPE *type;
  int is_variable = 0;

  type = PR_TYPE_FROM_ID (id);
  if (type != NULL)
    {
      is_variable = type->variable_p;
    }

  return is_variable;
}


/*
 * pr_find_type - Locate a type descriptor given a name.
 *    return: type structure
 *    name(in): type name
 * Note:
 *    Called by the schema manager to map a domain name into a primitive
 *    type.
 *    This now recognizes some alias names for a few of the types.
 *    The aliases should be more centrally defined so the parser can
 *    check for them.
 *
 */
PR_TYPE *
pr_find_type (const char *name)
{
  PR_TYPE *type, *found;
  int i;

  found = NULL;
  if (name != NULL)
    {
      for (i = DB_TYPE_FIRST; i <= DB_TYPE_LAST && found == NULL; i++)
	{
	  type = tp_Type_id_map[i];
	  if (type->name != NULL)
	    {
	      if (intl_mbs_casecmp (name, type->name) == 0)
		{
		  found = type;
		}
	    }
	}
    }

  /* alias kludge */
  if (found == NULL)
    {
      if (intl_mbs_casecmp (name, "int") == 0)
	{
	  found = tp_Type_integer;
	}
      else if (intl_mbs_casecmp (name, "multi_set") == 0)
	{
	  found = tp_Type_multiset;
	}

      else if (intl_mbs_casecmp (name, "short") == 0)
	{
	  found = tp_Type_short;
	}

      else if (intl_mbs_casecmp (name, "string") == 0)
	{
	  found = tp_Type_string;
	}
      else if (intl_mbs_casecmp (name, "utime") == 0)
	{
	  found = tp_Type_utime;
	}

      else if (intl_mbs_casecmp (name, "list") == 0)
	{
	  found = tp_Type_sequence;
	}

    }

  return (found);
}

/*
 * SIZE CALCULATORS
 * These operation on the instance memory format of data values.
 */


/*
 * pr_mem_size - Determine the number of bytes required for the memory
 * representation of a particular type.
 *    return: memory size of type
 *    type(in): PR_TYPE structure
 * Note:
 *    This only determines the size for an attribute value in contiguous
 *    memory storage for an instance.
 *    It does not include the size of any reference memory (like strings.
 *    For strings, it returns the size of the pointer NOT the length
 *    of the string.
 *    Called only by qp_it.c.  Why is this being called ???
 *
 */
int
pr_mem_size (PR_TYPE * type)
{
  return (type->size);
}


/*
 * pr_disk_size - Determine the number of bytes of disk storage required for
 * a value.
 *    return: disk size of an instance attribute
 *    type(in): type identifier
 *    mem(in): pointer to memory for value
 * Note:
 *    The value must be in instance memory format, NOT DB_VALUE format.
 *    If you have a DB_VALUE, use pr_value_disk_size.
 *    This is called by the transformer when calculating sizes and offset
 *    tables for instances.
 */
int
pr_disk_size (PR_TYPE * type, void *mem)
{
  int size;

  if (type->lengthmem != NULL)
    {
      size = (*type->lengthmem) (mem, NULL, 1);
    }
  else
    {
      size = type->disksize;
    }
  return (size);
}


/*
 * pr_total_mem_size - returns the total amount of storage used for a memory
 * attribute including any external allocatons (for strings etc.).
 *    return: total memory size of type
 *    type(in): type identifier
 *    mem(in): pointer to memory for value
 * Note:
 *    The length function is not defined to accept a DB_VALUE so
 *    this had better be in memory format!
 *    Called by sm_object_size to calculate total size for an object.
 *
 */
int
pr_total_mem_size (PR_TYPE * type, void *mem)
{
  int size;

  if (type->lengthmem != NULL)
    {
      size = (*type->lengthmem) (mem, NULL, 0);
    }
  else
    {
      size = type->size;
    }

  return (size);
}

/*
 * DB_VALUE TRANSFORMERS
 *
 * Used in the storage of class objects.
 * Need to fully extend this into the variabe type above so these can
 * be used at attribute values as well.
 *
 * Predicate processor must be able to understand these if we can issue
 * queries on these.
 *
 * This needs to be merged with the partially implemented support
 * for tp_Type_variable above.
 *
 * These functions will be called with a DB_DATA union NOT a pointer to
 * the memory representation of an attribute.
 */


/*
 * pr_value_mem_size - Returns the amount of storage necessary to hold the
 * contents of a DB_VALUE.
 *    return: byte size used by contents of DB_VALUE
 *    value(in): value to examine
 * Note:
 *    Does not include the amount of space necessary for the DB_VALUE.
 *    Used by some statistics modules that calculate memory sizes of strucures.
 */
int
pr_value_mem_size (DB_VALUE * value)
{
  PR_TYPE *type;
  int size;
  DB_TYPE dbval_type;

  size = 0;
  dbval_type = DB_VALUE_DOMAIN_TYPE (value);
  type = PR_TYPE_FROM_ID (dbval_type);
  if (type != NULL)
    {
      if (type->lengthval != NULL)
	{
	  size = (*type->lengthval) (value, 0);
	}
      else
	{
	  size = type->size;
	}
    }

  return (size);
}


/*
 * pr_estimate_size - returns the estimate number of bytes of disk
 * representation.
 *    return: estimate byte size of disk representation
 *    domain(in):  type domain
 *    avg_key_len(in): average key length
 * Note:
 *    It is generally used to pre-calculate the required size.
 */
int
pr_estimate_size (DB_DOMAIN * domain, int avg_key_len)
{
  int size = 0;
  PR_TYPE *type;

  type = domain->type;

  switch (type->id)
    {
    case DB_TYPE_BIT:
      size = or_packed_varbit_length (domain->precision);
      break;
    case DB_TYPE_VARBIT:
      size = or_packed_varbit_length (avg_key_len);
      break;
    case DB_TYPE_CHAR:
      size = domain->precision;
      break;
    case DB_TYPE_NCHAR:
      size = lang_loc_bytes_per_char () * domain->precision;
      break;
    case DB_TYPE_VARNCHAR:
      size = OR_INT_SIZE + (lang_loc_bytes_per_char () * avg_key_len);
      break;
    case DB_TYPE_VARCHAR:
      size = OR_INT_SIZE + avg_key_len;
      break;
    case DB_TYPE_MIDXKEY:
      {
	TP_DOMAIN *dom;
	int n_elements = 0;
	int n_variable_elements = 0;
	int element_size;

	for (dom = domain->setdomain; dom != NULL; dom = dom->next)
	  {
	    n_elements += 1;

	    if (dom->type->variable_p)
	      {
		n_variable_elements += 1;
	      }
	  }

	size = OR_BOUND_BIT_BYTES (n_elements);
	size += (OR_INT_SIZE * n_variable_elements);

	for (dom = domain->setdomain; dom != NULL; dom = dom->next)
	  {
	    if ((element_size = pr_estimate_size (dom, 0)) > -1)
	      {
		size += element_size;
	      }
	  }

	if (n_variable_elements)
	  {
	    size += avg_key_len;
	  }
      }
      break;
    default:
      size = tp_domain_disk_size (domain);
      break;
    }

  return size;
}

/*
 * pr_writemem_disk_size - returns the number of bytes that will be
 * written by the "writeval" type function for this memory buffer.
 *    return: byte size of disk representation
 *    mem(in): memory buffer
 *    domain(in): type domain
 */
int
pr_writemem_disk_size (char *mem, DB_DOMAIN * domain)
{
  int disk_size = 0;

  if (domain->type->variable_p)
    {
      OR_BUF buf;
      int str_size;
      int rc = NO_ERROR;

      or_init (&buf, mem, 0);

      /* 
       * variable types except VARCHAR, VARNCHAR, and VARBIT 
       * cannot be a member of midxkey
       */
      assert (QSTR_IS_VARIABLE_LENGTH (domain->type->id));

      if (domain->type->id == DB_TYPE_STRING
	  || domain->type->id == DB_TYPE_VARNCHAR)
	{
	  str_size = or_get_varchar_length (&buf, &rc);
	  disk_size = or_packed_varchar_length (str_size);
	}
      else			/* domain->type->id == DB_TYPE_VARBIT */
	{
	  str_size = or_get_varbit_length (&buf, &rc);
	  disk_size = or_packed_varbit_length (str_size);
	}
    }
  else
    {
      disk_size = tp_domain_disk_size (domain);
    }

  return disk_size;
}

/*
 * pr_writeval_disk_size - returns the number of bytes that will be
 * written by the "writeval" type function for this value.
 *    return: byte size of disk representation
 *    value(in): db value
 * Note:
 *    It is generally used prior to writing the value to pre-calculate the
 *    required size.
 *    Formerly called pr_value_disk_size.
 *
 *    Note that "writeval" is used for the construction of disk objects,
 *    and it will leave space for fixed width types that are logically
 *    NULL.  If you need a compressed representation for random values,
 *    look at the or_put_value family of functions.
 */
int
pr_writeval_disk_size (DB_VALUE * value)
{
  PR_TYPE *type;
  DB_TYPE dbval_type;

  dbval_type = DB_VALUE_DOMAIN_TYPE (value);
  type = PR_TYPE_FROM_ID (dbval_type);
  if (type->lengthval == NULL)
    {
      return type->disksize;
    }
  else
    {
      return (*(type->lengthval)) (value, 1);
    }
}

void
pr_writeval (OR_BUF * buf, DB_VALUE * value)
{
  PR_TYPE *type;
  DB_TYPE dbval_type;

  dbval_type = DB_VALUE_DOMAIN_TYPE (value);
  type = PR_TYPE_FROM_ID (dbval_type);
  if (type == NULL)
    {
      type = tp_Type_null;	/* handle strange arguments with NULL */
    }
  (*(type->writeval)) (buf, value);
}

/*
 * GARBAGE COLLECTION SUPPORT
 */
#if !defined (SERVER_MODE)

/*
 * pr_gc_set and pr_gc_setref are transition stubs for two new functions
 * that were moved into set.c for better encapsulation of the SETOBJ structure.
 * The callers should be changed
 */

void
pr_gc_set (SETOBJ * set, void (*gcmarker) (MOP))
{
  setobj_gc (set, gcmarker);
}

void
pr_gc_setref (SETREF * set, void (*gcmarker) (MOP))
{
  set_gc (set, gcmarker);
}

/*
 * pr_gc_value - Perform gc marking on the contents of a value.
 *    return: none
 *    value(in): value to check
 *    gcmarker(in): marker function
 * Note:
 *    This could be broken down into type specific handlers in the
 *    PR_TYPE structure but there are only two.
 */
void
pr_gc_value (DB_VALUE * value, void (*gcmarker) (MOP))
{
  if (value != NULL)
    {
      if (DB_VALUE_DOMAIN_TYPE (value) == DB_TYPE_OBJECT)
	{
	  (*gcmarker) (DB_GET_OBJECT (value));
	}
      else if (pr_is_set_type (DB_VALUE_DOMAIN_TYPE (value)))
	{
	  pr_gc_setref (db_get_set (value), gcmarker);
	}
    }
}


/*
 * pr_gc_type - Perform gc marking on an attribute value.
 *    return: none
 *    type(in): type descriptor
 *    mem(in): pointer to attribute memory
 *    gcmarker(in): marker function
 * Note:
 *    Similar to pr_gc_value except we have to handle the
 *    special case MOID.  Could be broken out into the primitive type
 *    handlers structures.
 */
void
pr_gc_type (PR_TYPE * type, char *mem, void (*gcmarker) (MOP))
{
  WS_MEMOID *moid;

  if (pr_is_set_type (type->id))
    {
      pr_gc_set (*((SETOBJ **) mem), gcmarker);
    }
  else if (type == tp_Type_object)
    {
      moid = (WS_MEMOID *) mem;
      if (moid->pointer != NULL)
	(*gcmarker) (moid->pointer);
    }
}
#endif /* !SERVER_MODE */


/*
 * MISCELLANEOUS TYPE-RELATED HELPER FUNCTIONS
 */


/*
 * pr_valstring - Take the value and formats it using the sptrfunc member of
 * the pr_type vector for the appropriate type.
 *    return: a freshly-malloc'ed string with a printed rep of "val" in it
 *    val(in): some DB_VALUE
 * Note:
 *    The caller is responsible for eventually freeing the memory via free_and_init.
 *
 *    This is really just a debugging helper; it probably doesn't do enough
 *    serious formatting for external use.  Use it to get printed DB_VALUE
 *    representations into error messages and the like.
 */
char *
pr_valstring (DB_VALUE * val)
{
  int str_size;
  char *str;
  PR_TYPE *pr_type;
  DB_TYPE dbval_type;

  const char null_str[] = "(null)";
  const char NULL_str[] = "NULL";

  if (val == NULL)
    {
      str = (char *) malloc (sizeof (null_str));
      if (str)
	{
	  strcpy (str, null_str);
	}
      return str;
    }

  if (DB_IS_NULL (val))
    {
      str = (char *) malloc (sizeof (NULL_str));
      if (str)
	{
	  strcpy (str, NULL_str);
	}
      return str;
    }

  dbval_type = DB_VALUE_DOMAIN_TYPE (val);
  pr_type = PR_TYPE_FROM_ID (dbval_type);

  /*
   * Guess a size; if we're wrong, we'll learn about it later and be told
   * how big to make the actual buffer.  Most things are pretty small, so
   * don't worry about this too much.
   */
  str_size = 32;

  str = (char *) malloc (str_size);
  if (str == NULL)
    {
      return NULL;
    }
  str_size = (*(pr_type->sptrfunc)) (val, str, str_size);
  if (str_size < 0)
    {
      /*
       * We didn't allocate enough slop.  However, the sprintf function
       * was kind enough to tell us how much room we really needed, so
       * we can reallocate and try again.
       */
      char *old_str;
      old_str = str;
      str_size = -str_size;
      str_size++;		/* for NULL */
      str = (char *) realloc (str, str_size);
      if (str == NULL)
	{
	  free_and_init (old_str);
	  return NULL;
	}
      if ((*pr_type->sptrfunc) (val, str, str_size) < 0)
	{
	  free_and_init (str);
	  return NULL;
	}
    }

  return str;

}

/*
 * TYPE RESULTSET
 */

static void
mr_initmem_resultset (void *mem)
{
  *(int *) mem = 0;
}

static int
mr_setmem_resultset (void *mem, TP_DOMAIN * domain, DB_VALUE * value)
{
  if (value != NULL)
    {
      *(int *) mem = db_get_resultset (value);
    }
  else
    {
      mr_initmem_resultset (mem);
    }

  return NO_ERROR;
}

static int
mr_getmem_resultset (void *mem, TP_DOMAIN * domain, DB_VALUE * value,
		     bool copy)
{
  return db_make_resultset (value, *(int *) mem);
}

static void
mr_writemem_resultset (OR_BUF * buf, void *mem, TP_DOMAIN * domain)
{
  or_put_int (buf, *(int *) mem);
}

static void
mr_readmem_resultset (OR_BUF * buf, void *mem, TP_DOMAIN * domain, int size)
{
  int rc = NO_ERROR;

  if (mem == NULL)
    {
      or_advance (buf, tp_ResultSet.disksize);
    }
  else
    {
      *(int *) mem = or_get_int (buf, &rc);
    }
}

static void
mr_initval_resultset (DB_VALUE * value, int precision, int scale)
{
  db_value_domain_init (value, DB_TYPE_RESULTSET, precision, scale);
  db_make_resultset (value, 0);
}

static int
mr_setval_resultset (DB_VALUE * dest, DB_VALUE * src, bool copy)
{
  if (src && !DB_IS_NULL (src))
    {
      return db_make_resultset (dest, db_get_resultset (src));
    }
  else
    {
      return db_value_domain_init (dest, DB_TYPE_RESULTSET,
				   DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE);
    }
}

static int
mr_writeval_resultset (OR_BUF * buf, DB_VALUE * value)
{
  return or_put_int (buf, db_get_resultset (value));
}

static int
mr_readval_resultset (OR_BUF * buf, DB_VALUE * value,
		      TP_DOMAIN * domain, int size, bool copy,
		      char *copy_buf, int copy_buf_len)
{
  int temp_int, rc = NO_ERROR;

  if (value == NULL)
    {
      rc = or_advance (buf, tp_ResultSet.disksize);
    }
  else
    {
      temp_int = or_get_int (buf, &rc);
      db_make_resultset (value, temp_int);
      value->need_clear = false;
    }
  return rc;
}

static int
mr_cmpdisk_resultset (void *mem1, void *mem2,
		      TP_DOMAIN * domain, int do_reverse,
		      int do_coercion, int total_order, int *start_colp)
{
  int i1, i2;

  i1 = OR_GET_INT (mem1);
  i2 = OR_GET_INT (mem2);

  return MR_CMP (i1, i2, do_reverse, domain);
}

static int
mr_cmpval_resultset (DB_VALUE * value1, DB_VALUE * value2,
		     TP_DOMAIN * domain, int do_reverse,
		     int do_coercion, int total_order, int *start_colp)
{
  int i1, i2;

  i1 = DB_GET_RESULTSET (value1);
  i2 = DB_GET_RESULTSET (value2);

  return MR_CMP (i1, i2, do_reverse, domain);
}
