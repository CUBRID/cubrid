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
 * object_primitive.c - This file contains code for handling the values of
 *                      primitive types in memory and for conversion between
 *                      the disk representation.
 */

#ident "$Id$"

#include "config.h"


#include <stdlib.h>
#include <string.h>

#include "system_parameter.h"
#include "misc_string.h"
#include "area_alloc.h"
#include "db.h"
#if !defined (SERVER_MODE)
#include "work_space.h"
#include "virtual_object.h"
#endif /* !SERVER_MODE */
#include "object_primitive.h"
#include "object_representation.h"
#include "set_object.h"
#include "elo.h"
#if !defined (SERVER_MODE)
#include "locator_cl.h"
#endif /* !SERVER_MODE */
#include "object_print.h"
#include "memory_alloc.h"
#include "intl_support.h"
#include "language_support.h"
#include "string_opfunc.h"
#include "object_accessor.h"
#if !defined (SERVER_MODE)
#include "transform_cl.h"
#endif /* !SERVER_MODE */
#include "server_interface.h"

#if defined (SERVER_MODE)
#include "thread.h"
#endif

#include "db_elo.h"

/* this must be the last header file included!!! */
#include "dbval.h"

#if !defined(SERVER_MODE)
extern unsigned int db_on_server;
#endif

#define MR_CMP(d1, d2)                                     \
     ((d1) < (d2) ? DB_LT : (d1) > (d2) ? DB_GT : DB_EQ)

#define MR_CMP_RETURN_CODE(c)                              \
     ((c) < 0 ? DB_LT : (c) > 0 ? DB_GT : DB_EQ)

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

#define STR_SIZE(prec, codeset)                                             \
     (((codeset) == INTL_CODESET_RAW_BITS) ? ((prec+7)/8) :		    \
      INTL_CODESET_MULT (codeset) * (prec))


#define BITS_IN_BYTE			8
#define BITS_TO_BYTES(bit_cnt)		(((bit_cnt) + 7) / 8)

/* left for future extension */
#define DO_CONVERSION_TO_SRVR_STR(codeset)  false
#define DO_CONVERSION_TO_SQLTEXT(codeset)   false

#define DB_DOMAIN_INIT_CHAR(value, precision)			 \
  do {								 \
    (value)->domain.general_info.type = DB_TYPE_CHAR;		 \
    (value)->domain.general_info.is_null = 1;			 \
    (value)->domain.char_info.length =				 \
    (precision) == DB_DEFAULT_PRECISION ?			 \
    TP_FLOATING_PRECISION_VALUE : (precision);			 \
    (value)->need_clear = false;				 \
    (value)->data.ch.info.codeset = LANG_SYS_CODESET;            \
    (value)->domain.char_info.collation_id = LANG_SYS_COLLATION; \
  } while (0)


#define IS_FLOATING_PRECISION(prec) \
  ((prec) == TP_FLOATING_PRECISION_VALUE)


static void mr_initmem_string (void *mem, TP_DOMAIN * domain);
static int mr_setmem_string (void *memptr, TP_DOMAIN * domain,
			     DB_VALUE * value);
static int mr_getmem_string (void *memptr, TP_DOMAIN * domain,
			     DB_VALUE * value, bool copy);
static int mr_data_lengthmem_string (void *memptr, TP_DOMAIN * domain,
				     int disk);
static int mr_index_lengthmem_string (void *memptr, TP_DOMAIN * domain);
static void mr_data_writemem_string (OR_BUF * buf, void *memptr,
				     TP_DOMAIN * domain);
static void mr_data_readmem_string (OR_BUF * buf, void *memptr,
				    TP_DOMAIN * domain, int size);
static void mr_freemem_string (void *memptr);
static void mr_initval_string (DB_VALUE * value, int precision, int scale);
static int mr_setval_string (DB_VALUE * dest, const DB_VALUE * src,
			     bool copy);
static int mr_data_lengthval_string (DB_VALUE * value, int disk);
static int mr_data_writeval_string (OR_BUF * buf, DB_VALUE * value);
static int mr_data_readval_string (OR_BUF * buf, DB_VALUE * value,
				   TP_DOMAIN * domain, int size, bool copy,
				   char *copy_buf, int copy_buf_len);
static int mr_index_lengthval_string (DB_VALUE * value);
static int mr_index_writeval_string (OR_BUF * buf, DB_VALUE * value);
static int mr_index_readval_string (OR_BUF * buf, DB_VALUE * value,
				    TP_DOMAIN * domain, int size, bool copy,
				    char *copy_buf, int copy_buf_len);
static int mr_lengthval_string_internal (DB_VALUE * value, int disk,
					 int align);
static int mr_writeval_string_internal (OR_BUF * buf, DB_VALUE * value,
					int align);
static int mr_readval_string_internal (OR_BUF * buf, DB_VALUE * value,
				       TP_DOMAIN * domain, int size,
				       bool copy, char *copy_buf,
				       int copy_buf_len, int align);
static int mr_index_cmpdisk_string (void *mem1, void *mem2,
				    TP_DOMAIN * domain,
				    int do_coercion, int total_order,
				    int *start_colp);
static int mr_data_cmpdisk_string (void *mem1, void *mem2, TP_DOMAIN * domain,
				   int do_coercion,
				   int total_order, int *start_colp);
static int mr_cmpval_string (DB_VALUE * value1, DB_VALUE * value2,
			     int do_coercion, int total_order,
			     int *start_colp, int collation);
#if defined (ENABLE_UNUSED_FUNCTION)
static int mr_cmpval_string2 (DB_VALUE * value1, DB_VALUE * value2,
			      int length, int do_coercion, int total_order,
			      int *start_colp);
#endif
static void mr_initmem_char (void *memptr, TP_DOMAIN * domain);
static int mr_setmem_char (void *memptr, TP_DOMAIN * domain,
			   DB_VALUE * value);
static int mr_getmem_char (void *mem, TP_DOMAIN * domain,
			   DB_VALUE * value, bool copy);
static int mr_data_lengthmem_char (void *memptr, TP_DOMAIN * domain,
				   int disk);
static int mr_index_lengthmem_char (void *memptr, TP_DOMAIN * domain);
static void mr_data_writemem_char (OR_BUF * buf, void *mem,
				   TP_DOMAIN * domain);
static void mr_data_readmem_char (OR_BUF * buf, void *mem, TP_DOMAIN * domain,
				  int size);
static void mr_freemem_char (void *memptr);
static void mr_initval_char (DB_VALUE * value, int precision, int scale);
static int mr_setval_char (DB_VALUE * dest, const DB_VALUE * src, bool copy);
static int mr_data_lengthval_char (DB_VALUE * value, int disk);
static int mr_data_writeval_char (OR_BUF * buf, DB_VALUE * value);
static int mr_writeval_char_internal (OR_BUF * buf, DB_VALUE * value,
				      int align);
static int mr_data_readval_char (OR_BUF * buf, DB_VALUE * value,
				 TP_DOMAIN * domain, int disk_size, bool copy,
				 char *copy_buf, int copy_buf_len);
static int mr_readval_char_internal (OR_BUF * buf, DB_VALUE * value,
				     TP_DOMAIN * domain, int disk_size,
				     bool copy, char *copy_buf,
				     int copy_buf_len, int align);
static int mr_index_lengthval_char (DB_VALUE * value);
static int mr_index_writeval_char (OR_BUF * buf, DB_VALUE * value);
static int mr_index_readval_char (OR_BUF * buf, DB_VALUE * value,
				  TP_DOMAIN * domain, int disk_size,
				  bool copy, char *copy_buf,
				  int copy_buf_len);
static int mr_index_cmpdisk_char (void *mem1, void *mem2, TP_DOMAIN * domain,
				  int do_coercion,
				  int total_order, int *start_colp);
static int mr_data_cmpdisk_char (void *mem1, void *mem2, TP_DOMAIN * domain,
				 int do_coercion,
				 int total_order, int *start_colp);
static int mr_cmpval_char (DB_VALUE * value1, DB_VALUE * value2,
			   int do_coercion, int total_order, int *start_colp,
			   int collation);
static int mr_cmpdisk_char_internal (void *mem1, void *mem2,
				     TP_DOMAIN * domain,
				     int do_coercion, int total_order,
				     int *start_colp, int align);
#if defined (ENABLE_UNUSED_FUNCTION)
static int mr_cmpval_char2 (DB_VALUE * value1, DB_VALUE * value2, int length,
			    int do_coercion, int total_order,
			    int *start_colp);
#endif
static void mr_initmem_nchar (void *memptr, TP_DOMAIN * domain);
static int mr_setmem_nchar (void *memptr, TP_DOMAIN * domain,
			    DB_VALUE * value);
static int mr_getmem_nchar (void *mem, TP_DOMAIN * domain,
			    DB_VALUE * value, bool copy);
static int mr_data_lengthmem_nchar (void *memptr, TP_DOMAIN * domain,
				    int disk);
static int mr_index_lengthmem_nchar (void *memptr, TP_DOMAIN * domain);
static void mr_data_writemem_nchar (OR_BUF * buf, void *mem,
				    TP_DOMAIN * domain);
static void mr_data_readmem_nchar (OR_BUF * buf, void *mem,
				   TP_DOMAIN * domain, int size);
static void mr_freemem_nchar (void *memptr);
static void mr_initval_nchar (DB_VALUE * value, int precision, int scale);
static int mr_setval_nchar (DB_VALUE * dest, const DB_VALUE * src, bool copy);
static int mr_data_lengthval_nchar (DB_VALUE * value, int disk);
static int mr_data_writeval_nchar (OR_BUF * buf, DB_VALUE * value);
static int mr_writeval_nchar_internal (OR_BUF * buf, DB_VALUE * value,
				       int align);
static int mr_data_readval_nchar (OR_BUF * buf, DB_VALUE * value,
				  TP_DOMAIN * domain, int disk_size,
				  bool copy, char *copy_buf,
				  int copy_buf_len);
static int mr_readval_nchar_internal (OR_BUF * buf, DB_VALUE * value,
				      TP_DOMAIN * domain, int disk_size,
				      bool copy, char *copy_buf,
				      int copy_buf_len, int align);
static int mr_index_lengthval_nchar (DB_VALUE * value);
static int mr_index_writeval_nchar (OR_BUF * buf, DB_VALUE * value);
static int mr_index_readval_nchar (OR_BUF * buf, DB_VALUE * value,
				   TP_DOMAIN * domain, int disk_size,
				   bool copy, char *copy_buf,
				   int copy_buf_len);
static int mr_index_cmpdisk_nchar (void *mem1, void *mem2, TP_DOMAIN * domain,
				   int do_coercion,
				   int total_order, int *start_colp);
static int mr_data_cmpdisk_nchar (void *mem1, void *mem2, TP_DOMAIN * domain,
				  int do_coercion,
				  int total_order, int *start_colp);
static int mr_cmpdisk_nchar_internal (void *mem1, void *mem2,
				      TP_DOMAIN * domain,
				      int do_coercion, int total_order,
				      int *start_colp, int align);
static int mr_cmpval_nchar (DB_VALUE * value1, DB_VALUE * value2,
			    int do_coercion, int total_order,
			    int *start_colp, int collation);
#if defined (ENABLE_UNUSED_FUNCTION)
static int mr_cmpval_nchar2 (DB_VALUE * value1, DB_VALUE * value2, int length,
			     int do_coercion, int total_order,
			     int *start_colp);
#endif
static void mr_initmem_varnchar (void *mem, TP_DOMAIN * domain);
static int mr_setmem_varnchar (void *memptr, TP_DOMAIN * domain,
			       DB_VALUE * value);
static int mr_getmem_varnchar (void *memptr, TP_DOMAIN * domain,
			       DB_VALUE * value, bool copy);
static int mr_data_lengthmem_varnchar (void *memptr, TP_DOMAIN * domain,
				       int disk);
static int mr_index_lengthmem_varnchar (void *memptr, TP_DOMAIN * domain);
static void mr_data_writemem_varnchar (OR_BUF * buf, void *memptr,
				       TP_DOMAIN * domain);
static void mr_data_readmem_varnchar (OR_BUF * buf, void *memptr,
				      TP_DOMAIN * domain, int size);
static void mr_freemem_varnchar (void *memptr);
static void mr_initval_varnchar (DB_VALUE * value, int precision, int scale);
static int mr_setval_varnchar (DB_VALUE * dest, const DB_VALUE * src,
			       bool copy);
static int mr_data_lengthval_varnchar (DB_VALUE * value, int disk);
static int mr_data_writeval_varnchar (OR_BUF * buf, DB_VALUE * value);
static int mr_data_readval_varnchar (OR_BUF * buf, DB_VALUE * value,
				     TP_DOMAIN * domain, int size, bool copy,
				     char *copy_buf, int copy_buf_len);
static int mr_index_lengthval_varnchar (DB_VALUE * value);
static int mr_index_writeval_varnchar (OR_BUF * buf, DB_VALUE * value);
static int mr_index_readval_varnchar (OR_BUF * buf, DB_VALUE * value,
				      TP_DOMAIN * domain, int size, bool copy,
				      char *copy_buf, int copy_buf_len);
static int mr_lengthval_varnchar_internal (DB_VALUE * value, int disk,
					   int align);
static int mr_writeval_varnchar_internal (OR_BUF * buf, DB_VALUE * value,
					  int align);
static int mr_readval_varnchar_internal (OR_BUF * buf, DB_VALUE * value,
					 TP_DOMAIN * domain, int size,
					 bool copy, char *copy_buf,
					 int copy_buf_len, int align);
static int mr_index_cmpdisk_varnchar (void *mem1, void *mem2,
				      TP_DOMAIN * domain, int do_coercion,
				      int total_order, int *start_colp);
static int mr_data_cmpdisk_varnchar (void *mem1, void *mem2,
				     TP_DOMAIN * domain,
				     int do_coercion, int total_order,
				     int *start_colp);
static int mr_cmpval_varnchar (DB_VALUE * value1, DB_VALUE * value2,
			       int do_coercion, int total_order,
			       int *start_colp, int collation);
#if defined (ENABLE_UNUSED_FUNCTION)
static int mr_cmpval_varnchar2 (DB_VALUE * value1, DB_VALUE * value2,
				int length, int do_coercion,
				int total_order, int *start_colp);
#endif
static void mr_initmem_bit (void *memptr, TP_DOMAIN * domain);
static int mr_setmem_bit (void *memptr, TP_DOMAIN * domain, DB_VALUE * value);
static int mr_getmem_bit (void *mem, TP_DOMAIN * domain, DB_VALUE * value,
			  bool copy);
static int mr_data_lengthmem_bit (void *memptr, TP_DOMAIN * domain, int disk);
static void mr_data_writemem_bit (OR_BUF * buf, void *mem,
				  TP_DOMAIN * domain);
static void mr_data_readmem_bit (OR_BUF * buf, void *mem, TP_DOMAIN * domain,
				 int size);
static void mr_freemem_bit (void *memptr);
static void mr_initval_bit (DB_VALUE * value, int precision, int scale);
static int mr_setval_bit (DB_VALUE * dest, const DB_VALUE * src, bool copy);
static int mr_data_lengthval_bit (DB_VALUE * value, int disk);
static int mr_data_writeval_bit (OR_BUF * buf, DB_VALUE * value);
static int mr_writeval_bit_internal (OR_BUF * buf, DB_VALUE * value,
				     int align);
static int mr_data_readval_bit (OR_BUF * buf, DB_VALUE * value,
				TP_DOMAIN * domain, int disk_size, bool copy,
				char *copy_buf, int copy_buf_len);
static int mr_readval_bit_internal (OR_BUF * buf, DB_VALUE * value,
				    TP_DOMAIN * domain, int disk_size,
				    bool copy, char *copy_buf,
				    int copy_buf_len, int align);
static int mr_index_lengthmem_bit (void *memptr, TP_DOMAIN * domain);
static int mr_index_lengthval_bit (DB_VALUE * value);
static int mr_index_writeval_bit (OR_BUF * buf, DB_VALUE * value);
static int mr_index_readval_bit (OR_BUF * buf, DB_VALUE * value,
				 TP_DOMAIN * domain, int disk_size, bool copy,
				 char *copy_buf, int copy_buf_len);
static int mr_index_cmpdisk_bit (void *mem1, void *mem2, TP_DOMAIN * domain,
				 int do_coercion,
				 int total_order, int *start_colp);
static int mr_data_cmpdisk_bit (void *mem1, void *mem2, TP_DOMAIN * domain,
				int do_coercion,
				int total_order, int *start_colp);
static int mr_cmpdisk_bit_internal (void *mem1, void *mem2,
				    TP_DOMAIN * domain,
				    int do_coercion, int total_order,
				    int *start_colp, int align);
static int mr_cmpval_bit (DB_VALUE * value1, DB_VALUE * value2,
			  int do_coercion, int total_order, int *start_colp,
			  int collation);
static int mr_cmpval_bit2 (DB_VALUE * value1, DB_VALUE * value2, int length,
			   int do_coercion, int total_order, int *start_colp);
static void mr_initmem_varbit (void *mem, TP_DOMAIN * domain);
static int mr_setmem_varbit (void *memptr, TP_DOMAIN * domain,
			     DB_VALUE * value);
static int mr_getmem_varbit (void *memptr, TP_DOMAIN * domain,
			     DB_VALUE * value, bool copy);
static int mr_data_lengthmem_varbit (void *memptr, TP_DOMAIN * domain,
				     int disk);
static int mr_index_lengthmem_varbit (void *memptr, TP_DOMAIN * domain);
static void mr_data_writemem_varbit (OR_BUF * buf, void *memptr,
				     TP_DOMAIN * domain);
static void mr_data_readmem_varbit (OR_BUF * buf, void *memptr,
				    TP_DOMAIN * domain, int size);
static void mr_freemem_varbit (void *memptr);
static void mr_initval_varbit (DB_VALUE * value, int precision, int scale);
static int mr_setval_varbit (DB_VALUE * dest, const DB_VALUE * src,
			     bool copy);
static int mr_data_lengthval_varbit (DB_VALUE * value, int disk);
static int mr_data_writeval_varbit (OR_BUF * buf, DB_VALUE * value);
static int mr_data_readval_varbit (OR_BUF * buf, DB_VALUE * value,
				   TP_DOMAIN * domain, int size, bool copy,
				   char *copy_buf, int copy_buf_len);
static int mr_index_lengthval_varbit (DB_VALUE * value);
static int mr_index_writeval_varbit (OR_BUF * buf, DB_VALUE * value);
static int mr_index_readval_varbit (OR_BUF * buf, DB_VALUE * value,
				    TP_DOMAIN * domain, int size, bool copy,
				    char *copy_buf, int copy_buf_len);
static int mr_lengthval_varbit_internal (DB_VALUE * value, int disk,
					 int align);
static int mr_writeval_varbit_internal (OR_BUF * buf, DB_VALUE * value,
					int align);
static int mr_readval_varbit_internal (OR_BUF * buf, DB_VALUE * value,
				       TP_DOMAIN * domain, int size,
				       bool copy, char *copy_buf,
				       int copy_buf_len, int align);
static int mr_index_cmpdisk_varbit (void *mem1, void *mem2,
				    TP_DOMAIN * domain,
				    int do_coercion, int total_order,
				    int *start_colp);
static int mr_data_cmpdisk_varbit (void *mem1, void *mem2, TP_DOMAIN * domain,
				   int do_coercion,
				   int total_order, int *start_colp);
static int mr_cmpval_varbit (DB_VALUE * value1, DB_VALUE * value2,
			     int do_coercion, int total_order,
			     int *start_colp, int collation);
static int mr_cmpval_varbit2 (DB_VALUE * value1, DB_VALUE * value2,
			      int length, int do_coercion, int total_order,
			      int *start_colp);

static void mr_initmem_null (void *mem, TP_DOMAIN * domain);
static int mr_setmem_null (void *memptr, TP_DOMAIN * domain,
			   DB_VALUE * value);
static int mr_getmem_null (void *memptr, TP_DOMAIN * domain,
			   DB_VALUE * value, bool copy);
static void mr_data_writemem_null (OR_BUF * buf, void *memptr,
				   TP_DOMAIN * domain);
static void mr_data_readmem_null (OR_BUF * buf, void *memptr,
				  TP_DOMAIN * domain, int size);
static void mr_initval_null (DB_VALUE * value, int precision, int scale);
static int mr_setval_null (DB_VALUE * dest, const DB_VALUE * src, bool copy);
static int mr_data_writeval_null (OR_BUF * buf, DB_VALUE * value);
static int mr_data_readval_null (OR_BUF * buf, DB_VALUE * value,
				 TP_DOMAIN * domain, int size, bool copy,
				 char *copy_buf, int copy_buf_len);
static int mr_data_cmpdisk_null (void *mem1, void *mem2, TP_DOMAIN * domain,
				 int do_coercion,
				 int total_order, int *start_colp);
static int mr_cmpval_null (DB_VALUE * value1, DB_VALUE * value2,
			   int do_coercion, int total_order, int *start_colp,
			   int collation);
static void mr_initmem_int (void *mem, TP_DOMAIN * domain);
static int mr_setmem_int (void *mem, TP_DOMAIN * domain, DB_VALUE * value);
static int mr_getmem_int (void *mem, TP_DOMAIN * domain, DB_VALUE * value,
			  bool copy);
static void mr_data_writemem_int (OR_BUF * buf, void *mem,
				  TP_DOMAIN * domain);
static void mr_data_readmem_int (OR_BUF * buf, void *mem, TP_DOMAIN * domain,
				 int size);
static void mr_initval_int (DB_VALUE * value, int precision, int scale);
static int mr_setval_int (DB_VALUE * dest, const DB_VALUE * src, bool copy);
static int mr_data_writeval_int (OR_BUF * buf, DB_VALUE * value);
static int mr_data_readval_int (OR_BUF * buf, DB_VALUE * value,
				TP_DOMAIN * domain, int size, bool copy,
				char *copy_buf, int copy_buf_len);
static int mr_index_writeval_int (OR_BUF * buf, DB_VALUE * value);
static int mr_index_readval_int (OR_BUF * buf, DB_VALUE * value,
				 TP_DOMAIN * domain, int size, bool copy,
				 char *copy_buf, int copy_buf_len);
static int mr_index_cmpdisk_int (void *mem1, void *mem2, TP_DOMAIN * domain,
				 int do_coercion,
				 int total_order, int *start_colp);
static int mr_data_cmpdisk_int (void *mem1, void *mem2, TP_DOMAIN * domain,
				int do_coercion,
				int total_order, int *start_colp);
static int mr_cmpval_int (DB_VALUE * value1, DB_VALUE * value2,
			  int do_coercion, int total_order, int *start_colp,
			  int collation);
static void mr_initmem_short (void *mem, TP_DOMAIN * domain);
static int mr_setmem_short (void *mem, TP_DOMAIN * domain, DB_VALUE * value);
static int mr_getmem_short (void *mem, TP_DOMAIN * domain,
			    DB_VALUE * value, bool copy);
static void mr_data_writemem_short (OR_BUF * buf, void *memptr,
				    TP_DOMAIN * domain);
static void mr_data_readmem_short (OR_BUF * buf, void *mem,
				   TP_DOMAIN * domain, int size);
static void mr_initval_short (DB_VALUE * value, int precision, int scale);
static int mr_setval_short (DB_VALUE * dest, const DB_VALUE * src, bool copy);
static int mr_data_writeval_short (OR_BUF * buf, DB_VALUE * value);
static int mr_data_readval_short (OR_BUF * buf, DB_VALUE * value,
				  TP_DOMAIN * domain, int size, bool copy,
				  char *copy_buf, int copy_buf_len);
static int mr_index_writeval_short (OR_BUF * buf, DB_VALUE * value);
static int mr_index_readval_short (OR_BUF * buf, DB_VALUE * value,
				   TP_DOMAIN * domain, int size, bool copy,
				   char *copy_buf, int copy_buf_len);
static int mr_index_cmpdisk_short (void *mem1, void *mem2, TP_DOMAIN * domain,
				   int do_coercion,
				   int total_order, int *start_colp);
static int mr_data_cmpdisk_short (void *mem1, void *mem2, TP_DOMAIN * domain,
				  int do_coercion,
				  int total_order, int *start_colp);
static int mr_cmpval_short (DB_VALUE * value1, DB_VALUE * value2,
			    int do_coercion, int total_order,
			    int *start_colp, int collation);
static void mr_initmem_bigint (void *mem, TP_DOMAIN * domain);
static int mr_setmem_bigint (void *mem, TP_DOMAIN * domain, DB_VALUE * value);
static int mr_getmem_bigint (void *mem, TP_DOMAIN * domain, DB_VALUE * value,
			     bool copy);
static void mr_data_writemem_bigint (OR_BUF * buf, void *mem,
				     TP_DOMAIN * domain);
static void mr_data_readmem_bigint (OR_BUF * buf, void *mem,
				    TP_DOMAIN * domain, int size);
static void mr_initval_bigint (DB_VALUE * value, int precision, int scale);
static int mr_setval_bigint (DB_VALUE * dest, const DB_VALUE * src,
			     bool copy);
static int mr_data_writeval_bigint (OR_BUF * buf, DB_VALUE * value);
static int mr_data_readval_bigint (OR_BUF * buf, DB_VALUE * value,
				   TP_DOMAIN * domain, int size, bool copy,
				   char *copy_buf, int copy_buf_len);
static int mr_index_writeval_bigint (OR_BUF * buf, DB_VALUE * value);
static int mr_index_readval_bigint (OR_BUF * buf, DB_VALUE * value,
				    TP_DOMAIN * domain, int size, bool copy,
				    char *copy_buf, int copy_buf_len);
static int mr_index_cmpdisk_bigint (void *mem1, void *mem2,
				    TP_DOMAIN * domain,
				    int do_coercion, int total_order,
				    int *start_colp);
static int mr_data_cmpdisk_bigint (void *mem1, void *mem2, TP_DOMAIN * domain,
				   int do_coercion,
				   int total_order, int *start_colp);
static int mr_cmpval_bigint (DB_VALUE * value1, DB_VALUE * value2,
			     int do_coercion, int total_order,
			     int *start_colp, int collation);
static void mr_initmem_float (void *mem, TP_DOMAIN * domain);
static int mr_setmem_float (void *mem, TP_DOMAIN * domain, DB_VALUE * value);
static int mr_getmem_float (void *mem, TP_DOMAIN * domain,
			    DB_VALUE * value, bool copy);
static void mr_data_writemem_float (OR_BUF * buf, void *mem,
				    TP_DOMAIN * domain);
static void mr_data_readmem_float (OR_BUF * buf, void *mem,
				   TP_DOMAIN * domain, int size);
static void mr_initval_float (DB_VALUE * value, int precision, int scale);
static int mr_setval_float (DB_VALUE * dest, const DB_VALUE * src, bool copy);
static int mr_data_writeval_float (OR_BUF * buf, DB_VALUE * value);
static int mr_data_readval_float (OR_BUF * buf, DB_VALUE * value,
				  TP_DOMAIN * domain, int size, bool copy,
				  char *copy_buf, int copy_buf_len);
static int mr_index_writeval_float (OR_BUF * buf, DB_VALUE * value);
static int mr_index_readval_float (OR_BUF * buf, DB_VALUE * value,
				   TP_DOMAIN * domain, int size, bool copy,
				   char *copy_buf, int copy_buf_len);
static int mr_index_cmpdisk_float (void *mem1, void *mem2, TP_DOMAIN * domain,
				   int do_coercion,
				   int total_order, int *start_colp);
static int mr_data_cmpdisk_float (void *mem1, void *mem2, TP_DOMAIN * domain,
				  int do_coercion,
				  int total_order, int *start_colp);
static int mr_cmpval_float (DB_VALUE * value1, DB_VALUE * value2,
			    int do_coercion, int total_order,
			    int *start_colp, int collation);
static void mr_initmem_double (void *mem, TP_DOMAIN * domain);
static int mr_setmem_double (void *mem, TP_DOMAIN * domain, DB_VALUE * value);
static int mr_getmem_double (void *mem, TP_DOMAIN * domain,
			     DB_VALUE * value, bool copy);
static void mr_data_writemem_double (OR_BUF * buf, void *mem,
				     TP_DOMAIN * domain);
static void mr_data_readmem_double (OR_BUF * buf, void *mem,
				    TP_DOMAIN * domain, int size);
static void mr_initval_double (DB_VALUE * value, int precision, int scale);
static int mr_setval_double (DB_VALUE * dest, const DB_VALUE * src,
			     bool copy);
static int mr_data_writeval_double (OR_BUF * buf, DB_VALUE * value);
static int mr_data_readval_double (OR_BUF * buf, DB_VALUE * value,
				   TP_DOMAIN * domain, int size, bool copy,
				   char *copy_buf, int copy_buf_len);
static int mr_index_writeval_double (OR_BUF * buf, DB_VALUE * value);
static int mr_index_readval_double (OR_BUF * buf, DB_VALUE * value,
				    TP_DOMAIN * domain, int size, bool copy,
				    char *copy_buf, int copy_buf_len);
static int mr_index_cmpdisk_double (void *mem1, void *mem2,
				    TP_DOMAIN * domain,
				    int do_coercion, int total_order,
				    int *start_colp);
static int mr_data_cmpdisk_double (void *mem1, void *mem2, TP_DOMAIN * domain,
				   int do_coercion,
				   int total_order, int *start_colp);
static int mr_cmpval_double (DB_VALUE * value1, DB_VALUE * value2,
			     int do_coercion, int total_order,
			     int *start_colp, int collation);
static void mr_initmem_time (void *mem, TP_DOMAIN * domain);
static int mr_setmem_time (void *mem, TP_DOMAIN * domain, DB_VALUE * value);
static int mr_getmem_time (void *mem, TP_DOMAIN * domain,
			   DB_VALUE * value, bool copy);
static void mr_data_writemem_time (OR_BUF * buf, void *mem,
				   TP_DOMAIN * domain);
static void mr_data_readmem_time (OR_BUF * buf, void *mem, TP_DOMAIN * domain,
				  int size);
static void mr_initval_time (DB_VALUE * value, int precision, int scale);
static int mr_setval_time (DB_VALUE * dest, const DB_VALUE * src, bool copy);
static int mr_data_writeval_time (OR_BUF * buf, DB_VALUE * value);
static int mr_data_readval_time (OR_BUF * buf, DB_VALUE * value,
				 TP_DOMAIN * domain, int size, bool copy,
				 char *copy_buf, int copy_buf_len);
static int mr_index_writeval_time (OR_BUF * buf, DB_VALUE * value);
static int mr_index_readval_time (OR_BUF * buf, DB_VALUE * value,
				  TP_DOMAIN * domain, int size, bool copy,
				  char *copy_buf, int copy_buf_len);
static int mr_index_cmpdisk_time (void *mem1, void *mem2, TP_DOMAIN * domain,
				  int do_coercion,
				  int total_order, int *start_colp);
static int mr_data_cmpdisk_time (void *mem1, void *mem2, TP_DOMAIN * domain,
				 int do_coercion,
				 int total_order, int *start_colp);
static int mr_cmpval_time (DB_VALUE * value1, DB_VALUE * value2,
			   int do_coercion, int total_order, int *start_colp,
			   int collation);
static void mr_initmem_utime (void *mem, TP_DOMAIN * domain);
static int mr_setmem_utime (void *mem, TP_DOMAIN * domain, DB_VALUE * value);
static int mr_getmem_utime (void *mem, TP_DOMAIN * domain,
			    DB_VALUE * value, bool copy);
static void mr_data_writemem_utime (OR_BUF * buf, void *mem,
				    TP_DOMAIN * domain);
static void mr_data_readmem_utime (OR_BUF * buf, void *mem,
				   TP_DOMAIN * domain, int size);
static void mr_initval_utime (DB_VALUE * value, int precision, int scale);
static int mr_setval_utime (DB_VALUE * dest, const DB_VALUE * src, bool copy);
static int mr_data_writeval_utime (OR_BUF * buf, DB_VALUE * value);
static int mr_data_readval_utime (OR_BUF * buf, DB_VALUE * value,
				  TP_DOMAIN * domain, int size, bool copy,
				  char *copy_buf, int copy_buf_len);
static int mr_index_writeval_utime (OR_BUF * buf, DB_VALUE * value);
static int mr_index_readval_utime (OR_BUF * buf, DB_VALUE * value,
				   TP_DOMAIN * domain, int size, bool copy,
				   char *copy_buf, int copy_buf_len);
static int mr_index_cmpdisk_utime (void *mem1, void *mem2, TP_DOMAIN * domain,
				   int do_coercion,
				   int total_order, int *start_colp);
static int mr_data_cmpdisk_utime (void *mem1, void *mem2, TP_DOMAIN * domain,
				  int do_coercion,
				  int total_order, int *start_colp);
static int mr_cmpval_utime (DB_VALUE * value1, DB_VALUE * value2,
			    int do_coercion, int total_order,
			    int *start_colp, int collation);

static void mr_initmem_datetime (void *mem, TP_DOMAIN * domain);
static void mr_initval_datetime (DB_VALUE * value, int precision, int scale);
static int mr_setmem_datetime (void *mem, TP_DOMAIN * domain,
			       DB_VALUE * value);
static int mr_getmem_datetime (void *mem, TP_DOMAIN * domain,
			       DB_VALUE * value, bool copy);
static int mr_setval_datetime (DB_VALUE * dest, const DB_VALUE * src,
			       bool copy);
static void mr_data_writemem_datetime (OR_BUF * buf, void *mem,
				       TP_DOMAIN * domain);
static void mr_data_readmem_datetime (OR_BUF * buf, void *mem,
				      TP_DOMAIN * domain, int size);
static int mr_data_writeval_datetime (OR_BUF * buf, DB_VALUE * value);
static int mr_data_readval_datetime (OR_BUF * buf, DB_VALUE * value,
				     TP_DOMAIN * domain, int size, bool copy,
				     char *copy_buf, int copy_buf_len);
static int mr_index_writeval_datetime (OR_BUF * buf, DB_VALUE * value);
static int mr_index_readval_datetime (OR_BUF * buf, DB_VALUE * value,
				      TP_DOMAIN * domain, int size, bool copy,
				      char *copy_buf, int copy_buf_len);
static int mr_index_cmpdisk_datetime (void *mem1, void *mem2,
				      TP_DOMAIN * domain,
				      int do_coercion, int total_order,
				      int *start_colp);
static int mr_data_cmpdisk_datetime (void *mem1, void *mem2,
				     TP_DOMAIN * domain,
				     int do_coercion, int total_order,
				     int *start_colp);
static int mr_cmpval_datetime (DB_VALUE * value1, DB_VALUE * value2,
			       int do_coercion, int total_order,
			       int *start_colp, int collation);

static void mr_initmem_money (void *mem, TP_DOMAIN * domain);
static int mr_setmem_money (void *memptr, TP_DOMAIN * domain,
			    DB_VALUE * value);
static int mr_getmem_money (void *memptr, TP_DOMAIN * domain,
			    DB_VALUE * value, bool copy);
static void mr_data_writemem_money (OR_BUF * buf, void *mem,
				    TP_DOMAIN * domain);
static void mr_data_readmem_money (OR_BUF * buf, void *mem,
				   TP_DOMAIN * domain, int size);
static void mr_initval_money (DB_VALUE * value, int precision, int scale);
static int mr_setval_money (DB_VALUE * dest, const DB_VALUE * src, bool copy);
static int mr_data_writeval_money (OR_BUF * buf, DB_VALUE * value);
static int mr_data_readval_money (OR_BUF * buf, DB_VALUE * value,
				  TP_DOMAIN * domain, int size, bool copy,
				  char *copy_buf, int copy_buf_len);
static int mr_index_writeval_money (OR_BUF * buf, DB_VALUE * value);
static int mr_index_readval_money (OR_BUF * buf, DB_VALUE * value,
				   TP_DOMAIN * domain, int size, bool copy,
				   char *copy_buf, int copy_buf_len);
static int mr_index_cmpdisk_money (void *mem1, void *mem2, TP_DOMAIN * domain,
				   int do_coercion,
				   int total_order, int *start_colp);
static int mr_data_cmpdisk_money (void *mem1, void *mem2, TP_DOMAIN * domain,
				  int do_coercion,
				  int total_order, int *start_colp);
static int mr_cmpval_money (DB_VALUE * value1, DB_VALUE * value2,
			    int do_coercion, int total_order,
			    int *start_colp, int collation);
static void mr_initmem_date (void *mem, TP_DOMAIN * domain);
static int mr_setmem_date (void *mem, TP_DOMAIN * domain, DB_VALUE * value);
static int mr_getmem_date (void *mem, TP_DOMAIN * domain,
			   DB_VALUE * value, bool copy);
static void mr_data_writemem_date (OR_BUF * buf, void *mem,
				   TP_DOMAIN * domain);
static void mr_data_readmem_date (OR_BUF * buf, void *mem, TP_DOMAIN * domain,
				  int size);
static void mr_initval_date (DB_VALUE * value, int precision, int scale);
static int mr_setval_date (DB_VALUE * dest, const DB_VALUE * src, bool copy);
static int mr_data_writeval_date (OR_BUF * buf, DB_VALUE * value);
static int mr_data_readval_date (OR_BUF * buf, DB_VALUE * value,
				 TP_DOMAIN * domain, int size, bool copy,
				 char *copy_buf, int copy_buf_len);
static int mr_index_writeval_date (OR_BUF * buf, DB_VALUE * value);
static int mr_index_readval_date (OR_BUF * buf, DB_VALUE * value,
				  TP_DOMAIN * domain, int size, bool copy,
				  char *copy_buf, int copy_buf_len);
static int mr_index_cmpdisk_date (void *mem1, void *mem2, TP_DOMAIN * domain,
				  int do_coercion,
				  int total_order, int *start_colp);
static int mr_data_cmpdisk_date (void *mem1, void *mem2, TP_DOMAIN * domain,
				 int do_coercion,
				 int total_order, int *start_colp);
static int mr_cmpval_date (DB_VALUE * value1, DB_VALUE * value2,
			   int do_coercion, int total_order, int *start_colp,
			   int collation);
static void mr_null_oid (OID * oid);
static void mr_initmem_object (void *mem, TP_DOMAIN * domain);
static void mr_initval_object (DB_VALUE * value, int precision, int scale);
static int mr_setmem_object (void *memptr, TP_DOMAIN * domain,
			     DB_VALUE * value);
static int mr_getmem_object (void *memptr, TP_DOMAIN * domain,
			     DB_VALUE * value, bool copy);
static int mr_setval_object (DB_VALUE * dest, const DB_VALUE * src,
			     bool copy);
static int mr_data_lengthval_object (DB_VALUE * value, int disk);
static void mr_data_writemem_object (OR_BUF * buf, void *memptr,
				     TP_DOMAIN * domain);
static void mr_data_readmem_object (OR_BUF * buf, void *memptr,
				    TP_DOMAIN * domain, int size);
static int mr_data_writeval_object (OR_BUF * buf, DB_VALUE * value);
static int mr_data_readval_object (OR_BUF * buf, DB_VALUE * value,
				   TP_DOMAIN * domain, int size, bool copy,
				   char *copy_buf, int copy_buf_len);
static int mr_index_writeval_object (OR_BUF * buf, DB_VALUE * value);
static int mr_index_readval_object (OR_BUF * buf, DB_VALUE * value,
				    TP_DOMAIN * domain, int size, bool copy,
				    char *copy_buf, int copy_buf_len);
static int mr_index_cmpdisk_object (void *mem1, void *mem2,
				    TP_DOMAIN * domain,
				    int do_coercion, int total_order,
				    int *start_colp);
static int mr_data_cmpdisk_object (void *mem1, void *mem2, TP_DOMAIN * domain,
				   int do_coercion,
				   int total_order, int *start_colp);
static int mr_cmpval_object (DB_VALUE * value1, DB_VALUE * value2,
			     int do_coercion, int total_order,
			     int *start_colp, int collation);
static void mr_initmem_elo (void *memptr, TP_DOMAIN * domain);
static void mr_initval_elo (DB_VALUE * value, int precision, int scale);
static int mr_setmem_elo (void *memptr, TP_DOMAIN * domain, DB_VALUE * value);
static int mr_setval_elo (DB_VALUE * dest, const DB_VALUE * src, bool copy);
static int mr_data_lengthmem_elo (void *memptr, TP_DOMAIN * domain, int disk);
static void mr_data_writemem_elo (OR_BUF * buf, void *memptr,
				  TP_DOMAIN * domain);
static void mr_data_readmem_elo (OR_BUF * buf, void *memptr,
				 TP_DOMAIN * domain, int size);
static int mr_getmem_elo (void *memptr, TP_DOMAIN * domain,
			  DB_VALUE * value, bool copy);
static int getmem_elo_with_type (void *memptr, TP_DOMAIN * domain,
				 DB_VALUE * value, bool copy, DB_TYPE type);
static void peekmem_elo (OR_BUF * buf, DB_ELO * elo);
static int mr_data_lengthval_elo (DB_VALUE * value, int disk);
static int mr_data_writeval_elo (OR_BUF * buf, DB_VALUE * value);
static int mr_data_readval_elo (OR_BUF * buf, DB_VALUE * value,
				TP_DOMAIN * domain, int size, bool copy,
				char *copy_buf, int copy_buf_len);
static int setval_elo_with_type (DB_VALUE * dest, const DB_VALUE * src,
				 bool copy, DB_TYPE type);
static int readval_elo_with_type (OR_BUF * buf, DB_VALUE * value,
				  TP_DOMAIN * domain, int size, bool copy,
				  char *copy_buf, int copy_buf_len,
				  DB_TYPE type);
static void mr_freemem_elo (void *memptr);
static int mr_data_cmpdisk_elo (void *mem1, void *mem2, TP_DOMAIN * domain,
				int do_coercion,
				int total_order, int *start_colp);
static int mr_cmpval_elo (DB_VALUE * value1, DB_VALUE * value2,
			  int do_coercion, int total_order, int *start_colp,
			  int collation);

static void mr_initval_blob (DB_VALUE * value, int precision, int scale);
static int mr_getmem_blob (void *memptr, TP_DOMAIN * domain, DB_VALUE * value,
			   bool copy);
static int mr_setval_blob (DB_VALUE * dest, const DB_VALUE * src, bool copy);
static int mr_data_readval_blob (OR_BUF * buf, DB_VALUE * value,
				 TP_DOMAIN * domain, int size, bool copy,
				 char *copy_buf, int copy_buf_len);

static void mr_initval_clob (DB_VALUE * value, int precision, int scale);
static int mr_getmem_clob (void *memptr, TP_DOMAIN * domain,
			   DB_VALUE * value, bool copy);
static int mr_setval_clob (DB_VALUE * dest, const DB_VALUE * src, bool copy);
static int mr_data_readval_clob (OR_BUF * buf, DB_VALUE * value,
				 TP_DOMAIN * domain, int size, bool copy,
				 char *copy_buf, int copy_buf_len);

static void mr_initval_variable (DB_VALUE * value, int precision, int scale);
static int mr_setval_variable (DB_VALUE * dest, const DB_VALUE * src,
			       bool copy);
static int mr_data_lengthval_variable (DB_VALUE * value, int disk);
static int mr_data_writeval_variable (OR_BUF * buf, DB_VALUE * value);
static int mr_data_readval_variable (OR_BUF * buf, DB_VALUE * value,
				     TP_DOMAIN * domain, int size, bool copy,
				     char *copy_buf, int copy_buf_len);
static int mr_data_cmpdisk_variable (void *mem1, void *mem2,
				     TP_DOMAIN * domain,
				     int do_coercion, int total_order,
				     int *start_colp);
static int mr_cmpval_variable (DB_VALUE * value1, DB_VALUE * value2,
			       int do_coercion, int total_order,
			       int *start_colp, int collation);
static void mr_initmem_sub (void *mem, TP_DOMAIN * domain);
static void mr_initval_sub (DB_VALUE * value, int precision, int scale);
static int mr_setmem_sub (void *mem, TP_DOMAIN * domain, DB_VALUE * value);
static int mr_getmem_sub (void *mem, TP_DOMAIN * domain, DB_VALUE * value,
			  bool copy);
static int mr_setval_sub (DB_VALUE * dest, const DB_VALUE * src, bool copy);
static int mr_data_lengthmem_sub (void *mem, TP_DOMAIN * domain, int disk);
static void mr_data_writemem_sub (OR_BUF * buf, void *mem,
				  TP_DOMAIN * domain);
static void mr_data_readmem_sub (OR_BUF * buf, void *mem, TP_DOMAIN * domain,
				 int size);
static int mr_data_lengthval_sub (DB_VALUE * value, int disk);
static int mr_data_writeval_sub (OR_BUF * buf, DB_VALUE * value);
static int mr_data_readval_sub (OR_BUF * buf, DB_VALUE * value,
				TP_DOMAIN * domain, int size, bool copy,
				char *copy_buf, int copy_buf_len);
static int mr_data_cmpdisk_sub (void *mem1, void *mem2, TP_DOMAIN * domain,
				int do_coercion,
				int total_order, int *start_colp);
static int mr_cmpval_sub (DB_VALUE * value1, DB_VALUE * value2,
			  int do_coercion, int total_order, int *start_colp,
			  int collation);
static void mr_initmem_ptr (void *memptr, TP_DOMAIN * domain);
static void mr_initval_ptr (DB_VALUE * value, int precision, int scale);
static int mr_setmem_ptr (void *memptr, TP_DOMAIN * domain, DB_VALUE * value);
static int mr_getmem_ptr (void *memptr, TP_DOMAIN * domain,
			  DB_VALUE * value, bool copy);
static int mr_setval_ptr (DB_VALUE * dest, const DB_VALUE * src, bool copy);
static int mr_data_lengthmem_ptr (void *memptr, TP_DOMAIN * domain, int disk);
static void mr_data_writemem_ptr (OR_BUF * buf, void *memptr,
				  TP_DOMAIN * domain);
static void mr_data_readmem_ptr (OR_BUF * buf, void *memptr,
				 TP_DOMAIN * domain, int size);
static int mr_data_lengthval_ptr (DB_VALUE * value, int disk);
static int mr_data_writeval_ptr (OR_BUF * buf, DB_VALUE * value);
static int mr_data_readval_ptr (OR_BUF * buf, DB_VALUE * value,
				TP_DOMAIN * domain, int size, bool copy,
				char *copy_buf, int copy_buf_len);
static int mr_data_cmpdisk_ptr (void *mem1, void *mem2, TP_DOMAIN * domain,
				int do_coercion,
				int total_order, int *start_colp);
static int mr_cmpval_ptr (DB_VALUE * value1, DB_VALUE * value2,
			  int do_coercion, int total_order, int *start_colp,
			  int collation);
static void mr_initmem_error (void *memptr, TP_DOMAIN * domain);
static void mr_initval_error (DB_VALUE * value, int precision, int scale);
static int mr_setmem_error (void *memptr, TP_DOMAIN * domain,
			    DB_VALUE * value);
static int mr_getmem_error (void *memptr, TP_DOMAIN * domain,
			    DB_VALUE * value, bool copy);
static int mr_setval_error (DB_VALUE * dest, const DB_VALUE * src, bool copy);
static int mr_data_lengthmem_error (void *memptr, TP_DOMAIN * domain,
				    int disk);
static int mr_data_lengthval_error (DB_VALUE * value, int disk);
static void mr_data_writemem_error (OR_BUF * buf, void *memptr,
				    TP_DOMAIN * domain);
static void mr_data_readmem_error (OR_BUF * buf, void *memptr,
				   TP_DOMAIN * domain, int size);
static int mr_data_writeval_error (OR_BUF * buf, DB_VALUE * value);
static int mr_data_readval_error (OR_BUF * buf, DB_VALUE * value,
				  TP_DOMAIN * domain, int size, bool copy,
				  char *copy_buf, int copy_buf_len);
static int mr_data_cmpdisk_error (void *mem1, void *mem2, TP_DOMAIN * domain,
				  int do_coercion,
				  int total_order, int *start_colp);
static int mr_cmpval_error (DB_VALUE * value1, DB_VALUE * value2,
			    int do_coercion, int total_order,
			    int *start_colp, int collation);
static void mr_initmem_oid (void *memptr, TP_DOMAIN * domain);
static void mr_initval_oid (DB_VALUE * value, int precision, int scale);
static int mr_setmem_oid (void *memptr, TP_DOMAIN * domain, DB_VALUE * value);
static int mr_getmem_oid (void *memptr, TP_DOMAIN * domain,
			  DB_VALUE * value, bool copy);
static int mr_setval_oid (DB_VALUE * dest, const DB_VALUE * src, bool copy);
static void mr_data_writemem_oid (OR_BUF * buf, void *memptr,
				  TP_DOMAIN * domain);
static void mr_data_readmem_oid (OR_BUF * buf, void *memptr,
				 TP_DOMAIN * domain, int size);
static int mr_data_writeval_oid (OR_BUF * buf, DB_VALUE * value);
static int mr_data_readval_oid (OR_BUF * buf, DB_VALUE * value,
				TP_DOMAIN * domain, int size, bool copy,
				char *copy_buf, int copy_buf_len);
static int mr_index_writeval_oid (OR_BUF * buf, DB_VALUE * value);
static int mr_index_readval_oid (OR_BUF * buf, DB_VALUE * value,
				 TP_DOMAIN * domain, int size, bool copy,
				 char *copy_buf, int copy_buf_len);
static int mr_index_cmpdisk_oid (void *mem1, void *mem2, TP_DOMAIN * domain,
				 int do_coercion,
				 int total_order, int *start_colp);
static int mr_data_cmpdisk_oid (void *mem1, void *mem2, TP_DOMAIN * domain,
				int do_coercion,
				int total_order, int *start_colp);
static int mr_cmpval_oid (DB_VALUE * value1, DB_VALUE * value2,
			  int do_coercion, int total_order, int *start_colp,
			  int collation);
static void mr_initmem_set (void *memptr, TP_DOMAIN * domain);
static void mr_initval_set (DB_VALUE * value, int precision, int scale);
static int mr_setmem_set (void *memptr, TP_DOMAIN * domain, DB_VALUE * value);
static int mr_getmem_set (void *memptr, TP_DOMAIN * domain,
			  DB_VALUE * value, bool copy);
static int mr_setval_set_internal (DB_VALUE * dest, const DB_VALUE * src,
				   bool copy, DB_TYPE set_type);
static int mr_setval_set (DB_VALUE * dest, const DB_VALUE * src, bool copy);
static int mr_data_lengthmem_set (void *memptr, TP_DOMAIN * domain, int disk);
static void mr_data_writemem_set (OR_BUF * buf, void *memptr,
				  TP_DOMAIN * domain);
static void mr_data_readmem_set (OR_BUF * buf, void *memptr,
				 TP_DOMAIN * domain, int size);
static int mr_data_lengthval_set (DB_VALUE * value, int disk);
static int mr_data_writeval_set (OR_BUF * buf, DB_VALUE * value);
static int mr_data_readval_set (OR_BUF * buf, DB_VALUE * value,
				TP_DOMAIN * domain, int size, bool copy,
				char *copy_buf, int copy_buf_len);
static void mr_freemem_set (void *memptr);
static int mr_data_cmpdisk_set (void *mem1, void *mem2, TP_DOMAIN * domain,
				int do_coercion,
				int total_order, int *start_colp);
static int mr_cmpval_set (DB_VALUE * value1, DB_VALUE * value2,
			  int do_coercion, int total_order, int *start_colp,
			  int collation);
static void mr_initval_multiset (DB_VALUE * value, int precision, int scale);
static int mr_getmem_multiset (void *memptr, TP_DOMAIN * domain,
			       DB_VALUE * value, bool copy);
static int mr_setval_multiset (DB_VALUE * dest, const DB_VALUE * src,
			       bool copy);
static void mr_initval_sequence (DB_VALUE * value, int precision, int scale);
static int mr_getmem_sequence (void *memptr, TP_DOMAIN * domain,
			       DB_VALUE * value, bool copy);
static int mr_setval_sequence (DB_VALUE * dest, const DB_VALUE * src,
			       bool copy);
static int mr_data_cmpdisk_sequence (void *mem1, void *mem2,
				     TP_DOMAIN * domain, int do_coercion,
				     int total_order, int *start_colp);
static int mr_cmpval_sequence (DB_VALUE * value1, DB_VALUE * value2,
			       int do_coercion, int total_order,
			       int *start_colp, int collation);
static void mr_initval_midxkey (DB_VALUE * value, int precision, int scale);
static int mr_setval_midxkey (DB_VALUE * dest, const DB_VALUE * src,
			      bool copy);
static int mr_data_lengthmem_midxkey (void *memptr, TP_DOMAIN * domain,
				      int disk);
static int mr_index_lengthmem_midxkey (void *memptr, TP_DOMAIN * domain);
static int mr_data_lengthval_midxkey (DB_VALUE * value, int disk);
static int mr_index_lengthval_midxkey (DB_VALUE * value);
static int mr_data_writeval_midxkey (OR_BUF * buf, DB_VALUE * value);
static int mr_index_writeval_midxkey (OR_BUF * buf, DB_VALUE * value);
static int mr_data_readval_midxkey (OR_BUF * buf, DB_VALUE * value,
				    TP_DOMAIN * domain, int size, bool copy,
				    char *copy_buf, int copy_buf_len);
static int mr_index_readval_midxkey (OR_BUF * buf, DB_VALUE * value,
				     TP_DOMAIN * domain, int size, bool copy,
				     char *copy_buf, int copy_buf_len);
static int pr_midxkey_compare_element (char *mem1, char *mem2,
				       TP_DOMAIN * dom1, TP_DOMAIN * dom2,
				       int do_coercion, int total_order);
static int mr_index_cmpdisk_midxkey (void *mem1, void *mem2,
				     TP_DOMAIN * domain,
				     int do_coercion, int total_order,
				     int *start_colp);
static int mr_data_cmpdisk_midxkey (void *mem1, void *mem2,
				    TP_DOMAIN * domain,
				    int do_coercion, int total_order,
				    int *start_colp);
static int mr_cmpval_midxkey (DB_VALUE * value1, DB_VALUE * value2,
			      int do_coercion, int total_order,
			      int *start_colp, int collation);
static void mr_initval_vobj (DB_VALUE * value, int precision, int scale);
static int mr_setval_vobj (DB_VALUE * dest, const DB_VALUE * src, bool copy);
static int mr_data_readval_vobj (OR_BUF * buf, DB_VALUE * value,
				 TP_DOMAIN * domain, int size, bool copy,
				 char *copy_buf, int copy_buf_len);
static int mr_data_cmpdisk_vobj (void *mem1, void *mem2, TP_DOMAIN * domain,
				 int do_coercion,
				 int total_order, int *start_colp);
static int mr_cmpval_vobj (DB_VALUE * value1, DB_VALUE * value2,
			   int do_coercion, int total_order, int *start_colp,
			   int collation);
static void mr_initmem_numeric (void *memptr, TP_DOMAIN * domain);
static int mr_setmem_numeric (void *mem, TP_DOMAIN * domain,
			      DB_VALUE * value);
static int mr_getmem_numeric (void *mem, TP_DOMAIN * domain,
			      DB_VALUE * value, bool copy);
static int mr_data_lengthmem_numeric (void *mem, TP_DOMAIN * domain,
				      int disk);
static int mr_index_lengthmem_numeric (void *mem, TP_DOMAIN * domain);
static void mr_data_writemem_numeric (OR_BUF * buf, void *mem,
				      TP_DOMAIN * domain);
static void mr_data_readmem_numeric (OR_BUF * buf, void *mem,
				     TP_DOMAIN * domain, int size);
static void mr_initval_numeric (DB_VALUE * value, int precision, int scale);
static int mr_setval_numeric (DB_VALUE * dest, const DB_VALUE * src,
			      bool copy);
static int mr_data_lengthval_numeric (DB_VALUE * value, int disk);
static int mr_data_writeval_numeric (OR_BUF * buf, DB_VALUE * value);
static int mr_data_readval_numeric (OR_BUF * buf, DB_VALUE * value,
				    TP_DOMAIN * domain, int size, bool copy,
				    char *copy_buf, int copy_buf_len);
static int mr_index_lengthval_numeric (DB_VALUE * value);
static int mr_index_writeval_numeric (OR_BUF * buf, DB_VALUE * value);
static int mr_index_readval_numeric (OR_BUF * buf, DB_VALUE * value,
				     TP_DOMAIN * domain, int size, bool copy,
				     char *copy_buf, int copy_buf_len);
static int mr_index_cmpdisk_numeric (void *mem1, void *mem2,
				     TP_DOMAIN * domain,
				     int do_coercion, int total_order,
				     int *start_colp);
static int mr_data_cmpdisk_numeric (void *mem1, void *mem2,
				    TP_DOMAIN * domain,
				    int do_coercion, int total_order,
				    int *start_colp);
static int mr_cmpval_numeric (DB_VALUE * value1, DB_VALUE * value2,
			      int do_coercion, int total_order,
			      int *start_colp, int collation);
static void pr_init_ordered_mem_sizes (void);
static void mr_initmem_resultset (void *mem, TP_DOMAIN * domain);
static int mr_setmem_resultset (void *mem, TP_DOMAIN * domain,
				DB_VALUE * value);
static int mr_getmem_resultset (void *mem, TP_DOMAIN * domain,
				DB_VALUE * value, bool copy);
static void mr_data_writemem_resultset (OR_BUF * buf, void *mem,
					TP_DOMAIN * domain);
static void mr_data_readmem_resultset (OR_BUF * buf, void *mem,
				       TP_DOMAIN * domain, int size);
static void mr_initval_resultset (DB_VALUE * value, int precision, int scale);
static int mr_setval_resultset (DB_VALUE * dest, const DB_VALUE * src,
				bool copy);
static int mr_data_writeval_resultset (OR_BUF * buf, DB_VALUE * value);
static int mr_data_readval_resultset (OR_BUF * buf, DB_VALUE * value,
				      TP_DOMAIN * domain, int size, bool copy,
				      char *copy_buf, int copy_buf_len);
static int mr_data_cmpdisk_resultset (void *mem1, void *mem2,
				      TP_DOMAIN * domain,
				      int do_coercion, int total_order,
				      int *start_colp);
static int mr_cmpval_resultset (DB_VALUE * value1, DB_VALUE * value2,
				int do_coercion, int total_order,
				int *start_colp, int collation);

static int pr_midxkey_get_vals_size (TP_DOMAIN * domains, DB_VALUE * dbvals,
				     int total);
static int pr_midxkey_get_element_internal (const DB_MIDXKEY * midxkey,
					    int index, DB_VALUE * value,
					    bool copy, int *prev_indexp,
					    char **prev_ptrp);
static void mr_initmem_enumeration (void *mem, TP_DOMAIN * domain);
static void mr_initval_enumeration (DB_VALUE * value, int precision,
				    int scale);
static int mr_setmem_enumeration (void *mem, TP_DOMAIN * domain,
				  DB_VALUE * value);
static int mr_getmem_enumeration (void *mem, TP_DOMAIN * domain,
				  DB_VALUE * value, bool copy);
static int mr_setval_enumeration (DB_VALUE * dest, const DB_VALUE * src,
				  bool copy);
static void mr_data_writemem_enumeration (OR_BUF * buf, void *memptr,
					  TP_DOMAIN * domain);
static void mr_data_readmem_enumeration (OR_BUF * buf, void *mem,
					 TP_DOMAIN * domain, int size);
static int mr_setval_enumeration_internal (DB_VALUE * value,
					   TP_DOMAIN * domain,
					   unsigned short index, int size,
					   bool copy, char *copy_buf,
					   int copy_buf_len);
static int mr_data_readval_enumeration (OR_BUF * buf, DB_VALUE * value,
					TP_DOMAIN * domain, int size,
					bool copy, char *copy_buf,
					int copy_buf_len);
static int mr_data_writeval_enumeration (OR_BUF * buf, DB_VALUE * value);
static int mr_data_cmpdisk_enumeration (void *mem1, void *mem2,
					TP_DOMAIN * domain, int do_coercion,
					int total_order, int *start_colp);
static int mr_cmpval_enumeration (DB_VALUE * value1, DB_VALUE * value2,
				  int do_coercion, int total_order,
				  int *start_colp, int collation);
static int mr_index_cmpdisk_enumeration (void *mem1, void *mem2,
					 TP_DOMAIN * domain, int do_coercion,
					 int total_order, int *start_colp);
static int mr_index_writeval_enumeration (OR_BUF * buf, DB_VALUE * value);
static int mr_index_readval_enumeration (OR_BUF * buf, DB_VALUE * value,
					 TP_DOMAIN * domain, int size,
					 bool copy, char *copy_buf,
					 int copy_buf_len);

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
  NULL,				/* data_lengthmem */
  NULL,				/* data_lengthval */
  mr_data_writemem_null,
  mr_data_readmem_null,
  mr_data_writeval_null,
  mr_data_readval_null,
  NULL,				/* index_lenghmem */
  NULL,				/* index_lenghval */
  NULL,				/* index_writeval */
  NULL,				/* index_readval */
  NULL,				/* index_cmpdisk */
  NULL,				/* freemem */
  mr_data_cmpdisk_null,
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
  NULL,				/* data_lengthmem */
  NULL,				/* data_lengthval */
  mr_data_writemem_int,
  mr_data_readmem_int,
  mr_data_writeval_int,
  mr_data_readval_int,
  NULL,				/* index_lengthmem */
  NULL,				/* index_lengthval */
  mr_index_writeval_int,
  mr_index_readval_int,
  mr_index_cmpdisk_int,
  NULL,				/* freemem */
  mr_data_cmpdisk_int,
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
  NULL,				/* data_lengthmem */
  NULL,				/* data_lengthval */
  mr_data_writemem_short,
  mr_data_readmem_short,
  mr_data_writeval_short,
  mr_data_readval_short,
  NULL,				/* index_lengthmem */
  NULL,				/* index_lengthval */
  mr_index_writeval_short,
  mr_index_readval_short,
  mr_index_cmpdisk_short,
  NULL,				/* freemem */
  mr_data_cmpdisk_short,
  mr_cmpval_short
};

PR_TYPE *tp_Type_short = &tp_Short;

PR_TYPE tp_Bigint = {
  "bigint", DB_TYPE_BIGINT, 0, sizeof (DB_BIGINT), sizeof (DB_BIGINT), 4,
  help_fprint_value,
  help_sprint_value,
  mr_initmem_bigint,
  mr_initval_bigint,
  mr_setmem_bigint,
  mr_getmem_bigint,
  mr_setval_bigint,
  NULL,				/* data_lengthmem */
  NULL,				/* data_lengthval */
  mr_data_writemem_bigint,
  mr_data_readmem_bigint,
  mr_data_writeval_bigint,
  mr_data_readval_bigint,
  NULL,				/* index_lengthmem */
  NULL,				/* index_lengthval */
  mr_index_writeval_bigint,
  mr_index_readval_bigint,
  mr_index_cmpdisk_bigint,
  NULL,				/* freemem */
  mr_data_cmpdisk_bigint,
  mr_cmpval_bigint
};

PR_TYPE *tp_Type_bigint = &tp_Bigint;

PR_TYPE tp_Float = {
  "float", DB_TYPE_FLOAT, 0, sizeof (float), sizeof (float), 4,
  help_fprint_value,
  help_sprint_value,
  mr_initmem_float,
  mr_initval_float,
  mr_setmem_float,
  mr_getmem_float,
  mr_setval_float,
  NULL,				/* data_lengthmem */
  NULL,				/* data_lengthval */
  mr_data_writemem_float,
  mr_data_readmem_float,
  mr_data_writeval_float,
  mr_data_readval_float,
  NULL,				/* index_lengthmem */
  NULL,				/* index_lengthval */
  mr_index_writeval_float,
  mr_index_readval_float,
  mr_index_cmpdisk_float,
  NULL,				/* freemem */
  mr_data_cmpdisk_float,
  mr_cmpval_float
};

PR_TYPE *tp_Type_float = &tp_Float;

PR_TYPE tp_Double = {
  "double", DB_TYPE_DOUBLE, 0, sizeof (double), sizeof (double), 4,
  help_fprint_value,
  help_sprint_value,
  mr_initmem_double,
  mr_initval_double,
  mr_setmem_double,
  mr_getmem_double,
  mr_setval_double,
  NULL,				/* data_lengthmem */
  NULL,				/* data_lenghtval */
  mr_data_writemem_double,
  mr_data_readmem_double,
  mr_data_writeval_double,
  mr_data_readval_double,
  NULL,				/* index_lenghtmem */
  NULL,				/* index_lenghtval */
  mr_index_writeval_double,
  mr_index_readval_double,
  mr_index_cmpdisk_double,
  NULL,				/* freemem */
  mr_data_cmpdisk_double,
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
  NULL,				/* data_lengthmem */
  NULL,				/* data_lengthval */
  mr_data_writemem_time,
  mr_data_readmem_time,
  mr_data_writeval_time,
  mr_data_readval_time,
  NULL,				/* index_lenghmem */
  NULL,				/* index_lenghval */
  mr_index_writeval_time,
  mr_index_readval_time,
  mr_index_cmpdisk_time,
  NULL,				/* freemem */
  mr_data_cmpdisk_time,
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
  NULL,				/* data_lengthmem */
  NULL,				/* data_lengthval */
  mr_data_writemem_utime,
  mr_data_readmem_utime,
  mr_data_writeval_utime,
  mr_data_readval_utime,
  NULL,				/* index_lenghmem */
  NULL,				/* index_lenghval */
  mr_index_writeval_utime,
  mr_index_readval_utime,
  mr_index_cmpdisk_utime,
  NULL,				/* freemem */
  mr_data_cmpdisk_utime,
  mr_cmpval_utime
};

PR_TYPE *tp_Type_utime = &tp_Utime;

PR_TYPE tp_Datetime = {
  "datetime", DB_TYPE_DATETIME, 0, sizeof (DB_DATETIME), OR_DATETIME_SIZE, 4,
  help_fprint_value,
  help_sprint_value,
  mr_initmem_datetime,
  mr_initval_datetime,
  mr_setmem_datetime,
  mr_getmem_datetime,
  mr_setval_datetime,
  NULL,				/* data_lengthmem */
  NULL,				/* data_lengthval */
  mr_data_writemem_datetime,
  mr_data_readmem_datetime,
  mr_data_writeval_datetime,
  mr_data_readval_datetime,
  NULL,				/* index_lenghmem */
  NULL,				/* index_lenghval */
  mr_index_writeval_datetime,
  mr_index_readval_datetime,
  mr_index_cmpdisk_datetime,
  NULL,				/* freemem */
  mr_data_cmpdisk_datetime,
  mr_cmpval_datetime
};

PR_TYPE *tp_Type_datetime = &tp_Datetime;

PR_TYPE tp_Monetary = {
  "monetary", DB_TYPE_MONETARY, 0, sizeof (DB_MONETARY), OR_MONETARY_SIZE, 4,
  help_fprint_value,
  help_sprint_value,
  mr_initmem_money,
  mr_initval_money,
  mr_setmem_money,
  mr_getmem_money,
  mr_setval_money,
  NULL,				/* data_lengthmem */
  NULL,				/* data_lengthval */
  mr_data_writemem_money,
  mr_data_readmem_money,
  mr_data_writeval_money,
  mr_data_readval_money,
  NULL,				/* index_lenghmem */
  NULL,				/* index_lenghval */
  mr_index_writeval_money,
  mr_index_readval_money,
  mr_index_cmpdisk_money,
  NULL,				/* freemem */
  mr_data_cmpdisk_money,
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
  NULL,				/* data_lengthmem */
  NULL,				/* data_lengthval */
  mr_data_writemem_date,
  mr_data_readmem_date,
  mr_data_writeval_date,
  mr_data_readval_date,
  NULL,				/* data_lengthmem */
  NULL,				/* data_lengthval */
  mr_index_writeval_date,
  mr_index_readval_date,
  mr_index_cmpdisk_date,
  NULL,				/* freemem */
  mr_data_cmpdisk_date,
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
  "object", DB_TYPE_OBJECT, 0, MR_OID_SIZE, OR_OID_SIZE, 4,
  help_fprint_value,
  help_sprint_value,
  mr_initmem_object,
  mr_initval_object,
  mr_setmem_object,
  mr_getmem_object,
  mr_setval_object,
  NULL,				/* data_lengthmem */
  mr_data_lengthval_object,
  mr_data_writemem_object,
  mr_data_readmem_object,
  mr_data_writeval_object,
  mr_data_readval_object,
  NULL,				/* data_lengthmem */
  NULL,				/* data_lengthval */
  mr_index_writeval_object,
  mr_index_readval_object,
  mr_index_cmpdisk_object,
  NULL,				/* freemem */
  mr_data_cmpdisk_object,
  mr_cmpval_object
};

PR_TYPE *tp_Type_object = &tp_Object;

PR_TYPE tp_Elo = {
  "*elo*", DB_TYPE_ELO, 1, sizeof (DB_ELO *), 0, 8,
  help_fprint_value,
  help_sprint_value,
  mr_initmem_elo,
  mr_initval_elo,
  mr_setmem_elo,
  mr_getmem_elo,
  mr_setval_elo,
  mr_data_lengthmem_elo,
  mr_data_lengthval_elo,
  mr_data_writemem_elo,
  mr_data_readmem_elo,
  mr_data_writeval_elo,
  mr_data_readval_elo,
  NULL,				/* index_lengthmem */
  NULL,				/* index_lengthval */
  NULL,				/* index_writeval */
  NULL,				/* index_readval */
  NULL,				/* index_cmpdisk */
  mr_freemem_elo,
  mr_data_cmpdisk_elo,
  mr_cmpval_elo
};

PR_TYPE *tp_Type_elo = &tp_Elo;

PR_TYPE tp_Blob = {
  "blob", DB_TYPE_BLOB, 1, sizeof (DB_ELO *), 0, 8,
  help_fprint_value,
  help_sprint_value,
  mr_initmem_elo,
  mr_initval_blob,
  mr_setmem_elo,
  mr_getmem_blob,
  mr_setval_blob,
  mr_data_lengthmem_elo,
  mr_data_lengthval_elo,
  mr_data_writemem_elo,
  mr_data_readmem_elo,
  mr_data_writeval_elo,
  mr_data_readval_blob,
  NULL,				/* index_lengthmem */
  NULL,				/* index_lengthval */
  NULL,				/* index_writeval */
  NULL,				/* index_readval */
  NULL,				/* index_cmpdisk */
  mr_freemem_elo,
  mr_data_cmpdisk_elo,
  mr_cmpval_elo
};

PR_TYPE *tp_Type_blob = &tp_Blob;

PR_TYPE tp_Clob = {
  "clob", DB_TYPE_CLOB, 1, sizeof (DB_ELO *), 0, 8,
  help_fprint_value,
  help_sprint_value,
  mr_initmem_elo,
  mr_initval_clob,
  mr_setmem_elo,
  mr_getmem_clob,
  mr_setval_clob,
  mr_data_lengthmem_elo,
  mr_data_lengthval_elo,
  mr_data_writemem_elo,
  mr_data_readmem_elo,
  mr_data_writeval_elo,
  mr_data_readval_clob,
  NULL,				/* index_lengthmem */
  NULL,				/* index_lengthval */
  NULL,				/* index_writeval */
  NULL,				/* index_readval */
  NULL,				/* index_cmpdisk */
  mr_freemem_elo,
  mr_data_cmpdisk_elo,
  mr_cmpval_elo
};

PR_TYPE *tp_Type_clob = &tp_Clob;

PR_TYPE tp_Variable = {
  "*variable*", DB_TYPE_VARIABLE, 1, sizeof (DB_VALUE), 0, 4,
  help_fprint_value,
  help_sprint_value,
  NULL,				/* initmem */
  mr_initval_variable,
  NULL,				/* setmem */
  NULL,				/* getmem */
  mr_setval_variable,
  NULL,				/* data_lengthmem */
  mr_data_lengthval_variable,
  NULL,				/* data_writemem */
  NULL,				/* data_readmem */
  mr_data_writeval_variable,
  mr_data_readval_variable,
  NULL,				/* index_lengthmem */
  NULL,				/* index_lengthval */
  NULL,				/* index_writeval */
  NULL,				/* index_readval */
  NULL,				/* index_cmpdisk */
  NULL,				/* freemem */
  mr_data_cmpdisk_variable,
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
  mr_data_lengthmem_sub,
  mr_data_lengthval_sub,
  mr_data_writemem_sub,
  mr_data_readmem_sub,
  mr_data_writeval_sub,
  mr_data_readval_sub,
  NULL,				/* index_lengthmem */
  NULL,				/* index_lengthval */
  NULL,				/* index_writeval */
  NULL,				/* index_readval */
  NULL,				/* index_cmpdisk */
  NULL,				/* freemem */
  mr_data_cmpdisk_sub,
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
  mr_data_lengthmem_ptr,
  mr_data_lengthval_ptr,
  mr_data_writemem_ptr,
  mr_data_readmem_ptr,
  mr_data_writeval_ptr,
  mr_data_readval_ptr,
  NULL,				/* index_lengthmem */
  NULL,				/* index_lengthval */
  NULL,				/* index_writeval */
  NULL,				/* index_readval */
  NULL,				/* index_cmpdisk */
  NULL,				/* freeemem */
  mr_data_cmpdisk_ptr,
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
  mr_data_lengthmem_error,
  mr_data_lengthval_error,
  mr_data_writemem_error,
  mr_data_readmem_error,
  mr_data_writeval_error,
  mr_data_readval_error,
  NULL,				/* index_lengthmem */
  NULL,				/* index_lengthval */
  NULL,				/* index_writeval */
  NULL,				/* index_readval */
  NULL,				/* index_cmpdisk */
  NULL,				/* freemem */
  mr_data_cmpdisk_error,
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
  "*oid*", DB_TYPE_OID, 0, sizeof (OID), OR_OID_SIZE, 4,
  help_fprint_value,
  help_sprint_value,
  mr_initmem_oid,
  mr_initval_oid,
  mr_setmem_oid,
  mr_getmem_oid,
  mr_setval_oid,
  NULL,				/* lengthmem */
  NULL,				/* lengthval */
  mr_data_writemem_oid,
  mr_data_readmem_oid,
  mr_data_writeval_oid,
  mr_data_readval_oid,
  NULL,				/* index_lengthmem */
  NULL,				/* index_lengthval */
  mr_index_writeval_oid,
  mr_index_readval_oid,
  mr_index_cmpdisk_oid,
  NULL,				/* freemem */
  mr_data_cmpdisk_oid,
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
  mr_data_lengthmem_set,
  mr_data_lengthval_set,
  mr_data_writemem_set,
  mr_data_readmem_set,
  mr_data_writeval_set,
  mr_data_readval_set,
  NULL,				/* index_lengthmem */
  NULL,				/* index_lengthval */
  NULL,				/* index_writeval */
  NULL,				/* index_readval */
  NULL,				/* index_cmpdisk */
  mr_freemem_set,
  mr_data_cmpdisk_set,
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
  mr_data_lengthmem_set,
  mr_data_lengthval_set,
  mr_data_writemem_set,
  mr_data_readmem_set,
  mr_data_writeval_set,
  mr_data_readval_set,
  NULL,				/* index_lengthmem */
  NULL,				/* index_lengthval */
  NULL,				/* index_writeval */
  NULL,				/* index_readval */
  NULL,				/* index_cmpdisk */
  mr_freemem_set,
  mr_data_cmpdisk_set,
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
  mr_data_lengthmem_set,
  mr_data_lengthval_set,
  mr_data_writemem_set,
  mr_data_readmem_set,
  mr_data_writeval_set,
  mr_data_readval_set,
  NULL,				/* index_lengthmem */
  NULL,				/* index_lengthval */
  NULL,				/* index_writeval */
  NULL,				/* index_readval */
  NULL,				/* index_cmpdisk */
  mr_freemem_set,
  mr_data_cmpdisk_sequence,
  mr_cmpval_sequence
};

PR_TYPE *tp_Type_sequence = &tp_Sequence;

PR_TYPE tp_Midxkey = {
  "midxkey", DB_TYPE_MIDXKEY, 1, 0, 0, 1,
  help_fprint_value,
  help_sprint_value,
  NULL,				/* initmem */
  mr_initval_midxkey,
  NULL,				/* setmem */
  NULL,				/* getmem_midxkey */
  mr_setval_midxkey,
  mr_data_lengthmem_midxkey,
  mr_data_lengthval_midxkey,
  NULL,				/* data_writemem  */
  NULL,				/* data_readmem */
  mr_data_writeval_midxkey,
  mr_data_readval_midxkey,
  mr_index_lengthmem_midxkey,
  mr_index_lengthval_midxkey,
  mr_index_writeval_midxkey,
  mr_index_readval_midxkey,
  mr_index_cmpdisk_midxkey,
  NULL,				/* freemem */
  mr_data_cmpdisk_midxkey,
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
  mr_data_lengthmem_set,
  mr_data_lengthval_set,
  mr_data_writemem_set,
  mr_data_readmem_set,
  mr_data_writeval_set,
  mr_data_readval_vobj,
  NULL,				/* index_lengthmem */
  NULL,				/* index_lengthval */
  NULL,				/* index_writeval */
  NULL,				/* index_readval */
  NULL,				/* index_cmpdisk */
  mr_freemem_set,
  mr_data_cmpdisk_vobj,
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
  mr_data_lengthmem_numeric,
  mr_data_lengthval_numeric,
  mr_data_writemem_numeric,
  mr_data_readmem_numeric,
  mr_data_writeval_numeric,
  mr_data_readval_numeric,
  mr_index_lengthmem_numeric,
  mr_index_lengthval_numeric,
  mr_index_writeval_numeric,
  mr_index_readval_numeric,
  mr_index_cmpdisk_numeric,
  NULL,				/* freemem */
  mr_data_cmpdisk_numeric,
  mr_cmpval_numeric
};

PR_TYPE *tp_Type_numeric = &tp_Numeric;

PR_TYPE tp_Enumeration = {
  "enum", DB_TYPE_ENUMERATION, 0, sizeof (unsigned short),
  sizeof (unsigned short), sizeof (unsigned short),
  help_fprint_value,
  help_sprint_value,
  mr_initmem_enumeration,
  mr_initval_enumeration,
  mr_setmem_enumeration,
  mr_getmem_enumeration,
  mr_setval_enumeration,
  NULL,				/* use disksize */
  NULL,				/* use_disksize */
  mr_data_writemem_enumeration,
  mr_data_readmem_enumeration,
  mr_data_writeval_enumeration,
  mr_data_readval_enumeration,
  NULL,				/* use disksize */
  NULL,				/* use disksize */
  mr_index_writeval_enumeration,
  mr_index_readval_enumeration,
  mr_index_cmpdisk_enumeration,
  NULL,				/* free mem not deeded for short */
  mr_data_cmpdisk_enumeration,
  mr_cmpval_enumeration
};

PR_TYPE *tp_Type_enumeration = &tp_Enumeration;


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
  &tp_Null,
  &tp_Bigint,
  &tp_Datetime,
  &tp_Blob,
  &tp_Clob,
  &tp_Enumeration
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
  NULL,				/* data_lengthmem */
  NULL,				/* data_lengthval */
  mr_data_writemem_resultset,
  mr_data_readmem_resultset,
  mr_data_writeval_resultset,
  mr_data_readval_resultset,
  NULL,				/* index_lengthmem */
  NULL,				/* index_lengthval */
  NULL,				/* index_writeval */
  NULL,				/* index_readval */
  NULL,				/* index_cmpdisk */
  NULL,				/* freemem */
  mr_data_cmpdisk_resultset,
  mr_cmpval_resultset
};

PR_TYPE *tp_Type_resultset = &tp_ResultSet;

/*
 * DB_VALUE MAINTENANCE
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
      db_value_domain_init (value, DB_TYPE_NULL, DB_DEFAULT_PRECISION,
			    DB_DEFAULT_SCALE);
    }

  return value;
}

/*
 * pr_make_ext_value - creates an external value container suitable for
 * passing beyond the interface layer to application programs.
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
      db_value_domain_init (value, DB_TYPE_NULL, DB_DEFAULT_PRECISION,
			    DB_DEFAULT_SCALE);
    }
  return value;
}

/*
 * pr_clear_value - clear an internal or external DB_VALUE and
 *                  initialize it to the NULL state.
 *    return: NO_ERROR
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
  unsigned char *data;
  bool need_clear;
  DB_TYPE db_type;

  if (value == NULL)
    {
      return NO_ERROR;		/* do nothing */
    }

  db_type = DB_VALUE_DOMAIN_TYPE (value);

  need_clear = true;

  if (DB_IS_NULL (value))
    {
      need_clear = false;
      if (value->need_clear)
	{
	  if (prm_get_bool_value (PRM_ID_ORACLE_STYLE_EMPTY_STRING))
	    {			/* need to check */
	      if ((QSTR_IS_ANY_CHAR_OR_BIT (db_type)
		   && value->data.ch.medium.buf != NULL)
		  || db_type == DB_TYPE_ENUMERATION)
		{
		  need_clear = true;	/* need to free Empty-string */
		}
	    }
	}
    }

  if (need_clear == false)
    {
      return NO_ERROR;		/* do nothing */
    }

  switch (db_type)
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
    case DB_TYPE_BLOB:
    case DB_TYPE_CLOB:
      if (value->need_clear)
	{
	  elo_free_structure (db_get_elo (value));
	}
      break;

    case DB_TYPE_ENUMERATION:
      if (value->need_clear)
	{
	  if (DB_GET_ENUM_STRING (value) != NULL)
	    {
	      db_private_free_and_init (NULL, DB_GET_ENUM_STRING (value));
	    }
	}
      db_make_enumeration (value, 0, NULL, 0,
			   DB_GET_ENUM_CODESET (value),
			   DB_GET_ENUM_COLLATION (value));
      break;

    default:
      break;
    }

  /* always make sure the value gets cleared */
  PRIM_SET_NULL (value);
  value->need_clear = false;

  return NO_ERROR;
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
  return error;
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
      /* some redundant checking but I want the semantics isolated */
      error = pr_clear_value (value);
      area_free (Value_area, (void *) value);
    }
  return error;
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
pr_clone_value (const DB_VALUE * src, DB_VALUE * dest)
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
	    {
	      db_value_domain_init (dest, src_dbtype,
				    DB_VALUE_PRECISION (src),
				    DB_VALUE_SCALE (src));
	    }
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
  return new_;
}

#if defined(ENABLE_UNUSED_FUNCTION)
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
#endif /* ENABLE_UNUSED_FUNCTION */

#if defined(ENABLE_UNUSED_FUNCTION)
/*
 * pr_copy_string - copy string
 *    return: copied string
 *    str(in): string to copy
 */
char *
pr_copy_string (const char *str)
{
  char *copy;

  copy = NULL;
  if (str != NULL)
    {
      copy = (char *) db_private_alloc (NULL, strlen (str) + 1);
      if (copy != NULL)
	strcpy (copy, str);
    }

  return copy;
}

 /*
  * pr_free_string - free copied string
  *
  * str(in): copied string
  */
void
pr_free_string (char *str)
{
  if (str != NULL)
    {
      db_private_free (NULL, str);
    }
}
#endif /* ENABLE_UNUSED_FUNCTION */

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
mr_initmem_null (void *memptr, TP_DOMAIN * domain)
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
  return NO_ERROR;
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
mr_data_writemem_null (OR_BUF * buf, void *memptr, TP_DOMAIN * domain)
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
mr_data_readmem_null (OR_BUF * buf, void *memptr, TP_DOMAIN * domain,
		      int size)
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
mr_setval_null (DB_VALUE * dest, const DB_VALUE * src, bool copy)
{
  mr_initval_null (dest, 0, 0);
  return NO_ERROR;
}

/*
 * mr_writeval_null - dummy function
 *    return:
 *    buf():
 *    value():
 */
static int
mr_data_writeval_null (OR_BUF * buf, DB_VALUE * value)
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
mr_data_readval_null (OR_BUF * buf, DB_VALUE * value,
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
 *    do_coercion():
 *    total_order():
 *    start_colp():
 */
static int
mr_data_cmpdisk_null (void *mem1, void *mem2, TP_DOMAIN * domain,
		      int do_coercion, int total_order, int *start_colp)
{
  assert (domain != NULL);

  return DB_UNK;
}

/*
 * mr_cmpval_null - dummy function
 *    return:
 *    value1():
 *    value2():
 *    do_coercion():
 *    total_order():
 *    start_colp():
 */
static int
mr_cmpval_null (DB_VALUE * value1, DB_VALUE * value2,
		int do_coercion, int total_order, int *start_colp,
		int collation)
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
mr_initmem_int (void *mem, TP_DOMAIN * domain)
{
  *(int *) mem = 0;
}

static int
mr_setmem_int (void *mem, TP_DOMAIN * domain, DB_VALUE * value)
{
  if (value != NULL)
    *(int *) mem = db_get_int (value);
  else
    mr_initmem_int (mem, domain);

  return NO_ERROR;
}

static int
mr_getmem_int (void *mem, TP_DOMAIN * domain, DB_VALUE * value, bool copy)
{
  return db_make_int (value, *(int *) mem);
}

static void
mr_data_writemem_int (OR_BUF * buf, void *mem, TP_DOMAIN * domain)
{
  or_put_int (buf, *(int *) mem);
}

static void
mr_data_readmem_int (OR_BUF * buf, void *mem, TP_DOMAIN * domain, int size)
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
mr_setval_int (DB_VALUE * dest, const DB_VALUE * src, bool copy)
{
  if (src && !DB_IS_NULL (src))
    {
      return db_make_int (dest, db_get_int (src));
    }
  else
    {
      return db_value_domain_init (dest, DB_TYPE_INTEGER,
				   DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE);
    }
}

static int
mr_data_writeval_int (OR_BUF * buf, DB_VALUE * value)
{
  return or_put_int (buf, db_get_int (value));
}

static int
mr_data_readval_int (OR_BUF * buf, DB_VALUE * value,
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
      if (rc == NO_ERROR)
	{
	  db_make_int (value, temp_int);
	}
      value->need_clear = false;
    }
  return rc;
}

static int
mr_index_writeval_int (OR_BUF * buf, DB_VALUE * value)
{
  int i;

  i = db_get_int (value);

  return or_put_data (buf, (char *) (&i), tp_Integer.disksize);
}

static int
mr_index_readval_int (OR_BUF * buf, DB_VALUE * value,
		      TP_DOMAIN * domain, int size, bool copy,
		      char *copy_buf, int copy_buf_len)
{
  int i, rc = NO_ERROR;

  if (value == NULL)
    {
      rc = or_advance (buf, tp_Integer.disksize);
    }
  else
    {
      rc = or_get_data (buf, (char *) (&i), tp_Integer.disksize);
      if (rc == NO_ERROR)
	{
	  db_make_int (value, i);
	}
      value->need_clear = false;
    }
  return rc;
}

static int
mr_index_cmpdisk_int (void *mem1, void *mem2, TP_DOMAIN * domain,
		      int do_coercion, int total_order, int *start_colp)
{
  int i1, i2;

  assert (domain != NULL);

  COPYMEM (int, &i1, mem1);
  COPYMEM (int, &i2, mem2);

  return MR_CMP (i1, i2);
}

static int
mr_data_cmpdisk_int (void *mem1, void *mem2, TP_DOMAIN * domain,
		     int do_coercion, int total_order, int *start_colp)
{
  int i1, i2;

  assert (domain != NULL);

  i1 = OR_GET_INT (mem1);
  i2 = OR_GET_INT (mem2);

  return MR_CMP (i1, i2);
}

static int
mr_cmpval_int (DB_VALUE * value1, DB_VALUE * value2,
	       int do_coercion, int total_order, int *start_colp,
	       int collation)
{
  int i1, i2;

  i1 = DB_GET_INT (value1);
  i2 = DB_GET_INT (value2);

  return MR_CMP (i1, i2);
}

/*
 * TYPE SHORT
 *
 * Your basic 16 bit signed integral value.
 */

static void
mr_initmem_short (void *mem, TP_DOMAIN * domain)
{
  *(short *) mem = 0;
}

static int
mr_setmem_short (void *mem, TP_DOMAIN * domain, DB_VALUE * value)
{
  if (value == NULL)
    mr_initmem_short (mem, domain);
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
mr_data_writemem_short (OR_BUF * buf, void *memptr, TP_DOMAIN * domain)
{
  short *mem = (short *) memptr;

  or_put_short (buf, *mem);
}

static void
mr_data_readmem_short (OR_BUF * buf, void *mem, TP_DOMAIN * domain, int size)
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
mr_setval_short (DB_VALUE * dest, const DB_VALUE * src, bool copy)
{
  if (src && !DB_IS_NULL (src))
    {
      return db_make_short (dest, db_get_short (src));
    }
  else
    {
      return db_value_domain_init (dest, DB_TYPE_SHORT,
				   DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE);
    }
}

static int
mr_data_writeval_short (OR_BUF * buf, DB_VALUE * value)
{
  return or_put_short (buf, db_get_short (value));
}

static int
mr_data_readval_short (OR_BUF * buf, DB_VALUE * value,
		       TP_DOMAIN * domain, int size, bool copy,
		       char *copy_buf, int copy_buf_len)
{
  int rc = NO_ERROR;
  short s;

  if (value == NULL)
    {
      rc = or_advance (buf, tp_Short.disksize);
    }
  else
    {
      s = (short) or_get_short (buf, &rc);
      if (rc == NO_ERROR)
	{
	  db_make_short (value, s);
	}
      value->need_clear = false;
    }

  return rc;
}

static int
mr_index_writeval_short (OR_BUF * buf, DB_VALUE * value)
{
  short s;

  s = db_get_short (value);

  return or_put_data (buf, (char *) (&s), tp_Short.disksize);
}

static int
mr_index_readval_short (OR_BUF * buf, DB_VALUE * value,
			TP_DOMAIN * domain, int size, bool copy,
			char *copy_buf, int copy_buf_len)
{
  int rc = NO_ERROR;
  short s;

  if (value == NULL)
    {
      rc = or_advance (buf, tp_Short.disksize);
    }
  else
    {
      rc = or_get_data (buf, (char *) (&s), tp_Short.disksize);
      if (rc == NO_ERROR)
	{
	  db_make_short (value, s);
	}
      value->need_clear = false;
    }

  return rc;
}

static int
mr_index_cmpdisk_short (void *mem1, void *mem2, TP_DOMAIN * domain,
			int do_coercion, int total_order, int *start_colp)
{
  short s1, s2;

  assert (domain != NULL);

  COPYMEM (short, &s1, mem1);
  COPYMEM (short, &s2, mem2);

  return MR_CMP (s1, s2);
}

static int
mr_data_cmpdisk_short (void *mem1, void *mem2, TP_DOMAIN * domain,
		       int do_coercion, int total_order, int *start_colp)
{
  short s1, s2;

  assert (domain != NULL);

  s1 = OR_GET_SHORT (mem1);
  s2 = OR_GET_SHORT (mem2);

  return MR_CMP (s1, s2);
}

static int
mr_cmpval_short (DB_VALUE * value1, DB_VALUE * value2,
		 int do_coercion, int total_order, int *start_colp,
		 int collation)
{
  short s1, s2;

  s1 = DB_GET_SHORT (value1);
  s2 = DB_GET_SHORT (value2);

  return MR_CMP (s1, s2);
}

/*
 * TYPE BIGINT
 *
 * Your basic 64 bit signed integral value.
 * At the storage level, we don't really care whether it is signed or unsigned.
 */

static void
mr_initmem_bigint (void *mem, TP_DOMAIN * domain)
{
  *(DB_BIGINT *) mem = 0;
}

static int
mr_setmem_bigint (void *mem, TP_DOMAIN * domain, DB_VALUE * value)
{
  if (value != NULL)
    *(DB_BIGINT *) mem = db_get_bigint (value);
  else
    mr_initmem_bigint (mem, domain);

  return NO_ERROR;
}

static int
mr_getmem_bigint (void *mem, TP_DOMAIN * domain, DB_VALUE * value, bool copy)
{
  return db_make_bigint (value, *(DB_BIGINT *) mem);
}

static void
mr_data_writemem_bigint (OR_BUF * buf, void *mem, TP_DOMAIN * domain)
{
  or_put_bigint (buf, *(DB_BIGINT *) mem);
}

static void
mr_data_readmem_bigint (OR_BUF * buf, void *mem, TP_DOMAIN * domain, int size)
{
  int rc = NO_ERROR;

  if (mem == NULL)
    {
      or_advance (buf, tp_Bigint.disksize);
    }
  else
    {
      *(DB_BIGINT *) mem = or_get_bigint (buf, &rc);
    }
}

static void
mr_initval_bigint (DB_VALUE * value, int precision, int scale)
{
  db_value_domain_init (value, DB_TYPE_BIGINT, precision, scale);
  db_make_bigint (value, 0);
}

static int
mr_setval_bigint (DB_VALUE * dest, const DB_VALUE * src, bool copy)
{
  if (src && !DB_IS_NULL (src))
    {
      return db_make_bigint (dest, db_get_bigint (src));
    }
  else
    {
      return db_value_domain_init (dest, DB_TYPE_BIGINT,
				   DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE);
    }
}

static int
mr_data_writeval_bigint (OR_BUF * buf, DB_VALUE * value)
{
  return or_put_bigint (buf, db_get_bigint (value));
}

static int
mr_data_readval_bigint (OR_BUF * buf, DB_VALUE * value,
			TP_DOMAIN * domain, int size, bool copy,
			char *copy_buf, int copy_buf_len)
{
  int rc = NO_ERROR;
  DB_BIGINT temp_int;

  if (value == NULL)
    {
      rc = or_advance (buf, tp_Bigint.disksize);
    }
  else
    {
      temp_int = or_get_bigint (buf, &rc);
      if (rc == NO_ERROR)
	{
	  db_make_bigint (value, temp_int);
	}
      value->need_clear = false;
    }
  return rc;
}

static int
mr_index_writeval_bigint (OR_BUF * buf, DB_VALUE * value)
{
  DB_BIGINT bi;

  bi = db_get_bigint (value);

  return or_put_data (buf, (char *) (&bi), tp_Bigint.disksize);
}

static int
mr_index_readval_bigint (OR_BUF * buf, DB_VALUE * value,
			 TP_DOMAIN * domain, int size, bool copy,
			 char *copy_buf, int copy_buf_len)
{
  int rc = NO_ERROR;
  DB_BIGINT bi;

  if (value == NULL)
    {
      rc = or_advance (buf, tp_Bigint.disksize);
    }
  else
    {
      rc = or_get_data (buf, (char *) (&bi), tp_Bigint.disksize);
      if (rc == NO_ERROR)
	{
	  db_make_bigint (value, bi);
	}
      value->need_clear = false;
    }

  return rc;
}

static int
mr_index_cmpdisk_bigint (void *mem1, void *mem2, TP_DOMAIN * domain,
			 int do_coercion, int total_order, int *start_colp)
{
  DB_BIGINT i1, i2;

  assert (domain != NULL);

  COPYMEM (DB_BIGINT, &i1, mem1);
  COPYMEM (DB_BIGINT, &i2, mem2);

  return MR_CMP (i1, i2);
}

static int
mr_data_cmpdisk_bigint (void *mem1, void *mem2, TP_DOMAIN * domain,
			int do_coercion, int total_order, int *start_colp)
{
  DB_BIGINT i1, i2;

  assert (domain != NULL);

  OR_GET_BIGINT (mem1, &i1);
  OR_GET_BIGINT (mem2, &i2);

  return MR_CMP (i1, i2);
}

static int
mr_cmpval_bigint (DB_VALUE * value1, DB_VALUE * value2,
		  int do_coercion, int total_order, int *start_colp,
		  int collation)
{
  DB_BIGINT i1, i2;

  i1 = DB_GET_BIGINT (value1);
  i2 = DB_GET_BIGINT (value2);

  return MR_CMP (i1, i2);
}

/*
 * TYPE FLOAT
 *
 * IEEE single precision floating point values.
 */

static void
mr_initmem_float (void *mem, TP_DOMAIN * domain)
{
  *(float *) mem = 0.0;
}

static int
mr_setmem_float (void *mem, TP_DOMAIN * domain, DB_VALUE * value)
{
  if (value == NULL)
    mr_initmem_float (mem, domain);
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
mr_data_writemem_float (OR_BUF * buf, void *mem, TP_DOMAIN * domain)
{
  or_put_float (buf, *(float *) mem);
}

static void
mr_data_readmem_float (OR_BUF * buf, void *mem, TP_DOMAIN * domain, int size)
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
mr_setval_float (DB_VALUE * dest, const DB_VALUE * src, bool copy)
{
  if (src && !DB_IS_NULL (src))
    {
      return db_make_float (dest, db_get_float (src));
    }
  else
    {
      return db_value_domain_init (dest, DB_TYPE_FLOAT,
				   DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE);
    }
}

static int
mr_data_writeval_float (OR_BUF * buf, DB_VALUE * value)
{
  return or_put_float (buf, db_get_float (value));
}

static int
mr_data_readval_float (OR_BUF * buf, DB_VALUE * value,
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
      if (rc == NO_ERROR)
	{
	  db_make_float (value, temp);
	}
      value->need_clear = false;
    }
  return rc;
}

static int
mr_index_writeval_float (OR_BUF * buf, DB_VALUE * value)
{
  float f;

  f = db_get_float (value);

  return or_put_data (buf, (char *) (&f), tp_Float.disksize);
}

static int
mr_index_readval_float (OR_BUF * buf, DB_VALUE * value,
			TP_DOMAIN * domain, int size, bool copy,
			char *copy_buf, int copy_buf_len)
{
  float f;
  int rc = NO_ERROR;

  if (value == NULL)
    {
      rc = or_advance (buf, tp_Float.disksize);
    }
  else
    {
      rc = or_get_data (buf, (char *) (&f), tp_Float.disksize);
      if (rc == NO_ERROR)
	{
	  db_make_float (value, f);
	}
      value->need_clear = false;
    }

  return rc;
}

static int
mr_index_cmpdisk_float (void *mem1, void *mem2, TP_DOMAIN * domain,
			int do_coercion, int total_order, int *start_colp)
{
  float f1, f2;

  assert (domain != NULL);

  COPYMEM (float, &f1, mem1);
  COPYMEM (float, &f2, mem2);

  return MR_CMP (f1, f2);
}

static int
mr_data_cmpdisk_float (void *mem1, void *mem2, TP_DOMAIN * domain,
		       int do_coercion, int total_order, int *start_colp)
{
  float f1, f2;

  assert (domain != NULL);

  OR_GET_FLOAT (mem1, &f1);
  OR_GET_FLOAT (mem2, &f2);

  return MR_CMP (f1, f2);
}

static int
mr_cmpval_float (DB_VALUE * value1, DB_VALUE * value2,
		 int do_coercion, int total_order, int *start_colp,
		 int collation)
{
  float f1, f2;

  f1 = DB_GET_FLOAT (value1);
  f2 = DB_GET_FLOAT (value2);

  return MR_CMP (f1, f2);
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
mr_initmem_double (void *mem, TP_DOMAIN * domain)
{
  double d = 0.0;

  OR_MOVE_DOUBLE (&d, mem);
}

static int
mr_setmem_double (void *mem, TP_DOMAIN * domain, DB_VALUE * value)
{
  double d;

  if (value == NULL)
    mr_initmem_double (mem, domain);
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
mr_data_writemem_double (OR_BUF * buf, void *mem, TP_DOMAIN * domain)
{
  double d;

  OR_MOVE_DOUBLE (mem, &d);
  or_put_double (buf, d);
}

static void
mr_data_readmem_double (OR_BUF * buf, void *mem, TP_DOMAIN * domain, int size)
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
mr_setval_double (DB_VALUE * dest, const DB_VALUE * src, bool copy)
{
  if (src && !DB_IS_NULL (src))
    {
      return db_make_double (dest, db_get_double (src));
    }
  else
    {
      return db_value_domain_init (dest, DB_TYPE_DOUBLE,
				   DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE);
    }
}

static int
mr_data_writeval_double (OR_BUF * buf, DB_VALUE * value)
{
  return or_put_double (buf, db_get_double (value));
}

static int
mr_data_readval_double (OR_BUF * buf, DB_VALUE * value,
			TP_DOMAIN * domain, int size, bool copy,
			char *copy_buf, int copy_buf_len)
{
  double temp;
  int rc = NO_ERROR;

  if (value == NULL)
    {
      rc = or_advance (buf, tp_Double.disksize);
    }
  else
    {
      temp = or_get_double (buf, &rc);
      if (rc == NO_ERROR)
	{
	  db_make_double (value, temp);
	}
      value->need_clear = false;
    }

  return rc;
}

static int
mr_index_writeval_double (OR_BUF * buf, DB_VALUE * value)
{
  double d;

  d = db_get_double (value);

  return or_put_data (buf, (char *) (&d), tp_Double.disksize);
}

static int
mr_index_readval_double (OR_BUF * buf, DB_VALUE * value,
			 TP_DOMAIN * domain, int size, bool copy,
			 char *copy_buf, int copy_buf_len)
{
  double d;
  int rc = NO_ERROR;

  if (value == NULL)
    {
      rc = or_advance (buf, tp_Double.disksize);
    }
  else
    {
      rc = or_get_data (buf, (char *) (&d), tp_Double.disksize);
      if (rc == NO_ERROR)
	{
	  db_make_double (value, d);
	}
      value->need_clear = false;
    }

  return rc;
}

static int
mr_index_cmpdisk_double (void *mem1, void *mem2, TP_DOMAIN * domain,
			 int do_coercion, int total_order, int *start_colp)
{
  double d1, d2;

  assert (domain != NULL);

  COPYMEM (double, &d1, mem1);
  COPYMEM (double, &d2, mem2);

  return MR_CMP (d1, d2);
}

static int
mr_data_cmpdisk_double (void *mem1, void *mem2, TP_DOMAIN * domain,
			int do_coercion, int total_order, int *start_colp)
{
  double d1, d2;

  assert (domain != NULL);

  OR_GET_DOUBLE (mem1, &d1);
  OR_GET_DOUBLE (mem2, &d2);

  return MR_CMP (d1, d2);
}

static int
mr_cmpval_double (DB_VALUE * value1, DB_VALUE * value2,
		  int do_coercion, int total_order, int *start_colp,
		  int collation)
{
  double d1, d2;

  d1 = DB_GET_DOUBLE (value1);
  d2 = DB_GET_DOUBLE (value2);

  return MR_CMP (d1, d2);
}

/*
 * TYPE TIME
 *
 * 32 bit seconds counter.  Interpreted as an offset within a given day.
 * Probably not general enough currently to be used an an interval type?
 *
 */

static void
mr_initmem_time (void *mem, TP_DOMAIN * domain)
{
  *(DB_TIME *) mem = 0;
}

static int
mr_setmem_time (void *mem, TP_DOMAIN * domain, DB_VALUE * value)
{
  if (value == NULL)
    mr_initmem_time (mem, domain);
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
mr_data_writemem_time (OR_BUF * buf, void *mem, TP_DOMAIN * domain)
{
  or_put_time (buf, (DB_TIME *) mem);
}

static void
mr_data_readmem_time (OR_BUF * buf, void *mem, TP_DOMAIN * domain, int size)
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
mr_setval_time (DB_VALUE * dest, const DB_VALUE * src, bool copy)
{
  int error;

  if (DB_IS_NULL (src))
    {
      error = db_value_domain_init (dest, DB_TYPE_TIME,
				    DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE);
    }
  else
    {
      error = db_value_put_encoded_time (dest, db_get_time (src));
    }
  return error;
}

static int
mr_data_writeval_time (OR_BUF * buf, DB_VALUE * value)
{
  return or_put_time (buf, db_get_time (value));
}

static int
mr_data_readval_time (OR_BUF * buf, DB_VALUE * value,
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
      if (rc == NO_ERROR)
	{
	  db_value_put_encoded_time (value, &tm);
	}
      value->need_clear = false;
    }
  return rc;
}

static int
mr_index_writeval_time (OR_BUF * buf, DB_VALUE * value)
{
  DB_TIME *tm;

  tm = db_get_time (value);

  return or_put_data (buf, (char *) tm, tp_Time.disksize);
}

static int
mr_index_readval_time (OR_BUF * buf, DB_VALUE * value,
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
      rc = or_get_data (buf, (char *) (&tm), tp_Time.disksize);
      if (rc == NO_ERROR)
	{
	  db_value_put_encoded_time (value, &tm);
	}
      value->need_clear = false;
    }

  return rc;
}

static int
mr_index_cmpdisk_time (void *mem1, void *mem2, TP_DOMAIN * domain,
		       int do_coercion, int total_order, int *start_colp)
{
  DB_TIME t1, t2;

  assert (domain != NULL);

  COPYMEM (DB_TIME, &t1, mem1);
  COPYMEM (DB_TIME, &t2, mem2);

  return MR_CMP (t1, t2);
}

static int
mr_data_cmpdisk_time (void *mem1, void *mem2, TP_DOMAIN * domain,
		      int do_coercion, int total_order, int *start_colp)
{
  DB_TIME t1, t2;

  assert (domain != NULL);

  OR_GET_TIME (mem1, &t1);
  OR_GET_TIME (mem2, &t2);

  return MR_CMP (t1, t2);
}

static int
mr_cmpval_time (DB_VALUE * value1, DB_VALUE * value2,
		int do_coercion, int total_order, int *start_colp,
		int collation)
{
  const DB_TIME *t1, *t2;

  t1 = DB_GET_TIME (value1);
  t2 = DB_GET_TIME (value2);

  return MR_CMP (*t1, *t2);
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
mr_initmem_utime (void *mem, TP_DOMAIN * domain)
{
  *(DB_UTIME *) mem = 0;
}

static int
mr_setmem_utime (void *mem, TP_DOMAIN * domain, DB_VALUE * value)
{
  if (value == NULL)
    mr_initmem_utime (mem, domain);
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
mr_data_writemem_utime (OR_BUF * buf, void *mem, TP_DOMAIN * domain)
{
  or_put_utime (buf, (DB_UTIME *) mem);
}

static void
mr_data_readmem_utime (OR_BUF * buf, void *mem, TP_DOMAIN * domain, int size)
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
mr_setval_utime (DB_VALUE * dest, const DB_VALUE * src, bool copy)
{
  int error;

  if (DB_IS_NULL (src))
    {
      error = db_value_domain_init (dest, DB_TYPE_UTIME,
				    DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE);
    }
  else
    {
      error = db_make_utime (dest, *db_get_utime (src));
    }
  return error;
}

static int
mr_data_writeval_utime (OR_BUF * buf, DB_VALUE * value)
{
  return or_put_utime (buf, db_get_utime (value));
}

static int
mr_data_readval_utime (OR_BUF * buf, DB_VALUE * value,
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
      if (rc == NO_ERROR)
	{
	  db_make_utime (value, utm);
	}
      value->need_clear = false;
    }
  return rc;
}

static int
mr_index_writeval_utime (OR_BUF * buf, DB_VALUE * value)
{
  DB_UTIME *utm;

  utm = db_get_utime (value);

  return or_put_data (buf, (char *) utm, tp_Utime.disksize);
}

static int
mr_index_readval_utime (OR_BUF * buf, DB_VALUE * value,
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
      rc = or_get_data (buf, (char *) (&utm), tp_Utime.disksize);
      if (rc == NO_ERROR)
	{
	  db_make_utime (value, utm);
	}
      value->need_clear = false;
    }

  return rc;
}

static int
mr_index_cmpdisk_utime (void *mem1, void *mem2, TP_DOMAIN * domain,
			int do_coercion, int total_order, int *start_colp)
{
  DB_UTIME utm1, utm2;

  assert (domain != NULL);

  COPYMEM (DB_UTIME, &utm1, mem1);
  COPYMEM (DB_UTIME, &utm2, mem2);

  return MR_CMP (utm1, utm2);
}

static int
mr_data_cmpdisk_utime (void *mem1, void *mem2, TP_DOMAIN * domain,
		       int do_coercion, int total_order, int *start_colp)
{
  DB_TIMESTAMP ts1, ts2;

  assert (domain != NULL);

  OR_GET_UTIME (mem1, &ts1);
  OR_GET_UTIME (mem2, &ts2);

  return MR_CMP (ts1, ts2);
}

static int
mr_cmpval_utime (DB_VALUE * value1, DB_VALUE * value2,
		 int do_coercion, int total_order, int *start_colp,
		 int collation)
{
  const DB_TIMESTAMP *ts1, *ts2;

  ts1 = DB_GET_UTIME (value1);
  ts2 = DB_GET_UTIME (value2);

  return MR_CMP (*ts1, *ts2);
}

/*
 * TYPE DATETIME
 *
 */

static void
mr_initmem_datetime (void *memptr, TP_DOMAIN * domain)
{
  DB_DATETIME *mem = (DB_DATETIME *) memptr;

  mem->date = 0;
  mem->time = 0;
}

static void
mr_initval_datetime (DB_VALUE * value, int precision, int scale)
{
  DB_DATETIME dt;

  mr_initmem_datetime (&dt, NULL);
  db_make_datetime (value, &dt);
  value->need_clear = false;
}

static int
mr_setmem_datetime (void *mem, TP_DOMAIN * domain, DB_VALUE * value)
{
  if (value == NULL)
    {
      mr_initmem_datetime (mem, domain);
    }
  else
    {
      *(DB_DATETIME *) mem = *db_get_datetime (value);
    }

  return NO_ERROR;
}

static int
mr_getmem_datetime (void *mem, TP_DOMAIN * domain, DB_VALUE * value,
		    bool copy)
{
  return db_make_datetime (value, (DB_DATETIME *) mem);
}

static int
mr_setval_datetime (DB_VALUE * dest, const DB_VALUE * src, bool copy)
{
  int error;

  if (DB_IS_NULL (src))
    {
      error = db_value_domain_init (dest, DB_TYPE_DATETIME,
				    DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE);
    }
  else
    {
      error = db_make_datetime (dest, db_get_datetime (src));
    }
  return error;
}

static void
mr_data_writemem_datetime (OR_BUF * buf, void *mem, TP_DOMAIN * domain)
{
  or_put_datetime (buf, (DB_DATETIME *) mem);
}

static void
mr_data_readmem_datetime (OR_BUF * buf, void *mem, TP_DOMAIN * domain,
			  int size)
{
  if (mem == NULL)
    {
      or_advance (buf, tp_Datetime.disksize);
    }
  else
    {
      or_get_datetime (buf, (DB_DATETIME *) mem);
    }
}

static int
mr_data_writeval_datetime (OR_BUF * buf, DB_VALUE * value)
{
  return or_put_datetime (buf, db_get_datetime (value));
}

static int
mr_data_readval_datetime (OR_BUF * buf, DB_VALUE * value,
			  TP_DOMAIN * domain, int size, bool copy,
			  char *copy_buf, int copy_buf_len)
{
  DB_DATETIME datetime;
  int rc = NO_ERROR;

  if (value == NULL)
    {
      rc = or_advance (buf, tp_Datetime.disksize);
    }
  else
    {
      rc = or_get_datetime (buf, &datetime);
      if (rc == NO_ERROR)
	{
	  db_make_datetime (value, &datetime);
	}
      value->need_clear = false;
    }
  return rc;
}

static int
mr_index_writeval_datetime (OR_BUF * buf, DB_VALUE * value)
{
  DB_DATETIME *datetime;
  int rc = NO_ERROR;

  datetime = db_get_datetime (value);

  assert (tp_Datetime.disksize == (tp_Date.disksize + tp_Time.disksize));

  rc = or_put_data (buf, (char *) (&datetime->date), tp_Date.disksize);
  if (rc == NO_ERROR)
    {
      rc = or_put_data (buf, (char *) (&datetime->time), tp_Time.disksize);
    }

  return rc;
}

static int
mr_index_readval_datetime (OR_BUF * buf, DB_VALUE * value,
			   TP_DOMAIN * domain, int size, bool copy,
			   char *copy_buf, int copy_buf_len)
{
  DB_DATETIME datetime;
  int rc = NO_ERROR;

  assert (tp_Datetime.disksize == (tp_Date.disksize + tp_Time.disksize));

  if (value == NULL)
    {
      rc = or_advance (buf, tp_Datetime.disksize);
    }
  else
    {
      rc = or_get_data (buf, (char *) (&datetime.date), tp_Date.disksize);
      if (rc == NO_ERROR)
	{
	  rc = or_get_data (buf, (char *) (&datetime.time), tp_Time.disksize);
	}

      if (rc == NO_ERROR)
	{
	  db_make_datetime (value, &datetime);
	}
      value->need_clear = false;
    }

  return rc;
}

static int
mr_index_cmpdisk_datetime (void *mem1, void *mem2, TP_DOMAIN * domain,
			   int do_coercion, int total_order, int *start_colp)
{
  int c;
  DB_DATETIME dt1, dt2;

  assert (domain != NULL);

  if (mem1 == mem2)
    {
      return DB_EQ;
    }

  COPYMEM (unsigned int, &dt1.date, (char *)mem1 + OR_DATETIME_DATE);
  COPYMEM (unsigned int, &dt1.time, (char *) mem1 + OR_DATETIME_TIME);
  COPYMEM (unsigned int, &dt2.date, (char *) mem2 + OR_DATETIME_DATE);
  COPYMEM (unsigned int, &dt2.time, (char *) mem2 + OR_DATETIME_TIME);

  if (dt1.date < dt2.date)
    {
      c = DB_LT;
    }
  else if (dt1.date > dt2.date)
    {
      c = DB_GT;
    }
  else
    {
      if (dt1.time < dt2.time)
	{
	  c = DB_LT;
	}
      else if (dt1.time > dt2.time)
	{
	  c = DB_GT;
	}
      else
	{
	  c = DB_EQ;
	}
    }

  return c;
}

static int
mr_data_cmpdisk_datetime (void *mem1, void *mem2, TP_DOMAIN * domain,
			  int do_coercion, int total_order, int *start_colp)
{
  int c;
  DB_DATETIME dt1, dt2;

  assert (domain != NULL);

  if (mem1 == mem2)
    {
      return DB_EQ;
    }

  OR_GET_DATETIME (mem1, &dt1);
  OR_GET_DATETIME (mem2, &dt2);

  if (dt1.date < dt2.date)
    {
      c = DB_LT;
    }
  else if (dt1.date > dt2.date)
    {
      c = DB_GT;
    }
  else
    {
      if (dt1.time < dt2.time)
	{
	  c = DB_LT;
	}
      else if (dt1.time > dt2.time)
	{
	  c = DB_GT;
	}
      else
	{
	  c = DB_EQ;
	}
    }

  return c;
}

static int
mr_cmpval_datetime (DB_VALUE * value1, DB_VALUE * value2,
		    int do_coercion, int total_order, int *start_colp,
		    int collation)
{
  const DB_DATETIME *dt1, *dt2;
  int c;

  dt1 = DB_GET_DATETIME (value1);
  dt2 = DB_GET_DATETIME (value2);

  if (dt1->date < dt2->date)
    {
      c = DB_LT;
    }
  else if (dt1->date > dt2->date)
    {
      c = DB_GT;
    }
  else
    {
      if (dt1->time < dt2->time)
	{
	  c = DB_LT;
	}
      else if (dt1->time > dt2->time)
	{
	  c = DB_GT;
	}
      else
	{
	  c = DB_EQ;
	}
    }

  return c;
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
mr_initmem_money (void *memptr, TP_DOMAIN * domain)
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
    mr_initmem_money (mem, domain);
  else
    {
      money = db_get_monetary (value);
      mem->type = money->type;
      OR_MOVE_DOUBLE (&money->amount, &mem->amount);
    }
  return NO_ERROR;
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
mr_data_writemem_money (OR_BUF * buf, void *mem, TP_DOMAIN * domain)
{
  or_put_monetary (buf, (DB_MONETARY *) mem);
}

static void
mr_data_readmem_money (OR_BUF * buf, void *mem, TP_DOMAIN * domain, int size)
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
mr_setval_money (DB_VALUE * dest, const DB_VALUE * src, bool copy)
{
  int error;
  DB_MONETARY *money;

  if (DB_IS_NULL (src) || ((money = db_get_monetary (src)) == NULL))
    {
      error = db_value_domain_init (dest, DB_TYPE_MONETARY,
				    DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE);
    }
  else
    {
      error = db_make_monetary (dest, money->type, money->amount);
    }
  return error;
}

static int
mr_data_writeval_money (OR_BUF * buf, DB_VALUE * value)
{
  return or_put_monetary (buf, db_get_monetary (value));
}

static int
mr_data_readval_money (OR_BUF * buf, DB_VALUE * value,
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
      if (rc == NO_ERROR)
	{
	  db_make_monetary (value, money.type, money.amount);
	}
      value->need_clear = false;
    }
  return rc;
}

static int
mr_index_writeval_money (OR_BUF * buf, DB_VALUE * value)
{
  DB_MONETARY *money;
  int rc = NO_ERROR;

  money = db_get_monetary (value);

  rc = or_put_data (buf, (char *) (&money->type), tp_Integer.disksize);
  if (rc == NO_ERROR)
    {
      rc = or_put_data (buf, (char *) (&money->amount), tp_Double.disksize);
    }

  return rc;
}

static int
mr_index_readval_money (OR_BUF * buf, DB_VALUE * value,
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
      rc = or_get_data (buf, (char *) (&money.type), tp_Integer.disksize);
      if (rc == NO_ERROR)
	{
	  rc =
	    or_get_data (buf, (char *) (&money.amount), tp_Double.disksize);
	}

      if (rc == NO_ERROR)
	{
	  db_make_monetary (value, money.type, money.amount);
	}
      value->need_clear = false;
    }

  return rc;
}

static int
mr_index_cmpdisk_money (void *mem1, void *mem2, TP_DOMAIN * domain,
			int do_coercion, int total_order, int *start_colp)
{
  DB_MONETARY m1, m2;

  assert (domain != NULL);

  COPYMEM (double, &m1.amount, (char *) mem1 + tp_Integer.disksize);
  COPYMEM (double, &m2.amount, (char *) mem2 + tp_Integer.disksize);

  return MR_CMP (m1.amount, m2.amount);
}

static int
mr_data_cmpdisk_money (void *mem1, void *mem2, TP_DOMAIN * domain,
		       int do_coercion, int total_order, int *start_colp)
{
  DB_MONETARY m1, m2;

  assert (domain != NULL);

  OR_GET_MONETARY (mem1, &m1);
  OR_GET_MONETARY (mem2, &m2);

  return MR_CMP (m1.amount, m2.amount);
}

static int
mr_cmpval_money (DB_VALUE * value1, DB_VALUE * value2,
		 int do_coercion, int total_order, int *start_colp,
		 int collation)
{
  const DB_MONETARY *m1, *m2;

  m1 = DB_GET_MONETARY (value1);
  m2 = DB_GET_MONETARY (value2);

  return MR_CMP (m1->amount, m2->amount);
}

/*
 * TYPE DATE
 *
 * 32 bit day counter, commonly called a "julian" date.
 */

static void
mr_initmem_date (void *mem, TP_DOMAIN * domain)
{
  *(DB_DATE *) mem = 0;
}

static int
mr_setmem_date (void *mem, TP_DOMAIN * domain, DB_VALUE * value)
{
  if (value == NULL)
    mr_initmem_date (mem, domain);
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
mr_data_writemem_date (OR_BUF * buf, void *mem, TP_DOMAIN * domain)
{
  or_put_date (buf, (DB_DATE *) mem);
}

static void
mr_data_readmem_date (OR_BUF * buf, void *mem, TP_DOMAIN * domain, int size)
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
mr_setval_date (DB_VALUE * dest, const DB_VALUE * src, bool copy)
{
  int error;

  if (DB_IS_NULL (src))
    {
      error = db_value_domain_init (dest, DB_TYPE_DATE,
				    DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE);
    }
  else
    {
      error = db_value_put_encoded_date (dest, db_get_date (src));
    }
  return error;
}

static int
mr_data_writeval_date (OR_BUF * buf, DB_VALUE * value)
{
  return or_put_date (buf, db_get_date (value));
}

static int
mr_data_readval_date (OR_BUF * buf, DB_VALUE * value,
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
      if (rc == NO_ERROR)
	{
	  db_value_put_encoded_date (value, &dt);
	}
      value->need_clear = false;
    }
  return rc;
}

static int
mr_index_writeval_date (OR_BUF * buf, DB_VALUE * value)
{
  DB_DATE *dt;

  dt = db_get_date (value);

  return or_put_data (buf, (char *) dt, tp_Date.disksize);
}

static int
mr_index_readval_date (OR_BUF * buf, DB_VALUE * value,
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
      rc = or_get_data (buf, (char *) (&dt), tp_Date.disksize);
      if (rc == NO_ERROR)
	{
	  db_value_put_encoded_date (value, &dt);
	}
      value->need_clear = false;
    }

  return rc;
}

static int
mr_index_cmpdisk_date (void *mem1, void *mem2, TP_DOMAIN * domain,
		       int do_coercion, int total_order, int *start_colp)
{
  DB_DATE d1, d2;

  assert (domain != NULL);

  COPYMEM (DB_DATE, &d1, mem1);
  COPYMEM (DB_DATE, &d2, mem2);

  return MR_CMP (d1, d2);
}

static int
mr_data_cmpdisk_date (void *mem1, void *mem2, TP_DOMAIN * domain,
		      int do_coercion, int total_order, int *start_colp)
{
  DB_DATE d1, d2;

  assert (domain != NULL);

  OR_GET_DATE (mem1, &d1);
  OR_GET_DATE (mem2, &d2);

  return MR_CMP (d1, d2);
}

static int
mr_cmpval_date (DB_VALUE * value1, DB_VALUE * value2,
		int do_coercion, int total_order, int *start_colp,
		int collation)
{
  const DB_DATE *d1, *d2;

  d1 = DB_GET_DATE (value1);
  d2 = DB_GET_DATE (value2);

  return MR_CMP (*d1, *d2);
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
mr_initmem_object (void *memptr, TP_DOMAIN * domain)
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
	  mr_initmem_object (mem, domain);
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
  return NO_ERROR;
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
	      assert (er_errid () != NO_ERROR);
	      error = er_errid ();
	      (void) db_make_object (value, NULL);
	    }
	}
    }
  else
    error = db_make_object (value, op);
#endif /* !SERVER_MODE */

  return error;
}


static int
mr_setval_object (DB_VALUE * dest, const DB_VALUE * src, bool copy)
{
  int error = NO_ERROR;
  OID *oid;

#if !defined (SERVER_MODE)
  if (DB_IS_NULL (src))
    {
      PRIM_SET_NULL (dest);
    }
  /* can get here on the server when dispatching through set element domains */
  else if (DB_VALUE_TYPE (src) == DB_TYPE_OID)
    {
      /* make sure that the target type is set properly */
      db_value_domain_init (dest, DB_TYPE_OID,
			    DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE);
      oid = (OID *) db_get_oid (src);
      error = db_make_oid (dest, oid);
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
	  obj = db_pull_object (src);
	  db_value_domain_init (dest, DB_TYPE_OID,
				DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE);
	  oid = WS_OID (obj);
	  error = db_make_oid (dest, oid);
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
  if (DB_IS_NULL (src) || DB_VALUE_TYPE (src) != DB_TYPE_OID)
    {
      PRIM_SET_NULL (dest);
    }
  else
    {
      oid = (OID *) db_get_oid (src);
      error = db_make_oid (dest, oid);
    }
#endif /* !SERVER_MODE */

  return error;
}



static int
mr_index_lengthval_object (DB_VALUE * value)
{
  return tp_Oid.disksize;
}

/*
 * mr_lengthval_object - checks if the object is virtual or not. and returns
 * property type size.
 *    return: If it is virtual object returns calculated the DB_TYPE_VOBJ
 *    packed size. returns DB_TYPE_OID otherwise
 *    value(in): value to get length
 *    disk(in): indicator that it is disk object
 */
static int
mr_data_lengthval_object (DB_VALUE * value, int disk)
{
#if !defined (SERVER_MODE)
  MOP mop;
#endif
  int size;

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
      if ((mop == NULL) || (WS_IS_DELETED (mop)))
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
	      size = mr_data_lengthval_set (&vmop_seq, disk);
	      pr_clear_value (&vmop_seq);
	    }
	}
    }
#endif

  return size;
}

static void
mr_data_writemem_object (OR_BUF * buf, void *memptr, TP_DOMAIN * domain)
{
#if !defined (SERVER_MODE)	/* there is no need for writemem on the server */
  WS_MEMOID *mem = (WS_MEMOID *) memptr;
  OID *oidp;

  oidp = NULL;
  if (mem != NULL)
    {
      oidp = &mem->oid;
    }

  if (oidp == NULL)
    {
      /* construct an unbound oid */
      oidp = (OID *) (&oid_Null_oid);
    }
  else if (OID_ISTEMP (oidp))
    {
      /* Temporary oid, must get a permanent one.
         This should only happen if the MOID has a valid MOP.
         Check for deletion */
      if ((mem->pointer == NULL) || (WS_IS_DELETED (mem->pointer)))
	{
	  oidp = (OID *) (&oid_Null_oid);
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
		{
		  /* normally would have used or_abort by now */
		  oidp = (OID *) (&oid_Null_oid);
		}
	    }
	}
    }
  else
    {
      /* normal OID check for deletion */
      if ((mem->pointer != NULL) && (WS_IS_DELETED (mem->pointer)))
	{
	  oidp = (OID *) (&oid_Null_oid);
	}
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
mr_data_readmem_object (OR_BUF * buf, void *memptr, TP_DOMAIN * domain,
			int size)
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
mr_index_writeval_object (OR_BUF * buf, DB_VALUE * value)
{
  return mr_index_writeval_oid (buf, value);
}

static int
mr_data_writeval_object (OR_BUF * buf, DB_VALUE * value)
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
      if ((mop == NULL) || (WS_IS_DELETED (mop)))
	{
	  rc = or_put_oid (buf, (OID *) (&oid_Null_oid));
	}
      else if (WS_ISVID (mop))
	{
	  DB_VALUE vmop_seq;
	  int error;

	  error = vid_object_to_vobj (mop, &vmop_seq);
	  if (error >= 0)
	    {
	      rc = mr_data_writeval_set (buf, &vmop_seq);
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
		oidp = (OID *) (&oid_Null_oid);
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
      rc = or_put_oid (buf, (OID *) (&oid_Null_oid));
    }
#else /* SERVER_MODE */
  /* on the server, the value must contain an OID */
  oidp = db_get_oid (value);
  rc = or_put_oid (buf, oidp);
#endif /* !SERVER_MODE */
  return rc;
}



static int
mr_index_readval_object (OR_BUF * buf, DB_VALUE * value,
			 TP_DOMAIN * domain, int size, bool copy,
			 char *copy_buf, int copy_buf_len)
{
  return mr_index_readval_oid (buf, value, domain, size, copy,
			       copy_buf, copy_buf_len);
}

static int
mr_data_readval_object (OR_BUF * buf, DB_VALUE * value,
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
mr_index_cmpdisk_object (void *mem1, void *mem2, TP_DOMAIN * domain,
			 int do_coercion, int total_order, int *start_colp)
{
  assert (domain != NULL);

  return mr_index_cmpdisk_oid (mem1, mem2, domain, do_coercion,
			       total_order, start_colp);
}

static int
mr_data_cmpdisk_object (void *mem1, void *mem2, TP_DOMAIN * domain,
			int do_coercion, int total_order, int *start_colp)
{
  int c;
  OID o1, o2;

  assert (domain != NULL);

  OR_GET_OID (mem1, &o1);
  OR_GET_OID (mem2, &o2);
  /* if we ever store virtual objects, this will need to be
   * changed. However, since its known the only disk representation
   * of objects is an OID, this is a valid optimization
   */

  c = oid_compare (&o1, &o2);
  c = MR_CMP_RETURN_CODE (c);

  return c;
}

static int
mr_cmpval_object (DB_VALUE * value1, DB_VALUE * value2,
		  int do_coercion, int total_order, int *start_colp,
		  int collation)
{
  int c;
#if defined (SERVER_MODE)
  OID *o1, *o2;
  DB_OBJECT *obj;

  /*
   * we need to be careful here because even though the domain may
   * say object, it may really be an OID (especially on the server).
   */
  if (DB_VALUE_DOMAIN_TYPE (value1) == DB_TYPE_OID)
    {
      o1 = DB_PULL_OID (value1);
    }
  else
    {
      obj = DB_GET_OBJECT (value1);
      o1 = (obj) ? WS_OID (obj) : (OID *) (&oid_Null_oid);
    }

  if (DB_VALUE_DOMAIN_TYPE (value2) == DB_TYPE_OID)
    {
      o2 = DB_PULL_OID (value2);
    }
  else
    {
      obj = DB_GET_OBJECT (value2);
      o2 = (obj) ? WS_OID (obj) : (OID *) (&oid_Null_oid);
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
  if (DB_VALUE_DOMAIN_TYPE (value1) == DB_TYPE_OID)
    {
      o1 = DB_PULL_OID (value1);
    }
  else
    {
      mop1 = DB_PULL_OBJECT (value1);
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

	  if (mop1 != NULL)
	    {
	      if (WS_ISVID (mop1))
		{
		  /* non updateble object or proxy object */
		  virtual_ = 1;
		}
	      else
		{
		  mop1 = ws_mvcc_latest_version (mop1);
		  o1 = WS_OID (mop1);
		}
	    }
	  else
	    {
	      o1 = (OID *) (&oid_Null_oid);
	    }
	}
      else
	{
	  mop1 = ws_mvcc_latest_version (mop1);
	  o1 = WS_OID (mop1);
	}
    }

  if (DB_VALUE_DOMAIN_TYPE (value2) == DB_TYPE_OID)
    {
      o2 = DB_PULL_OID (value2);
    }
  else
    {
      mop2 = DB_PULL_OBJECT (value2);
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

	  if (mop2 != NULL)
	    {
	      if (WS_ISVID (mop2))
		{
		  /* non updateble object or proxy object */
		  virtual_ += 2;
		}
	      else
		{
		  mop2 = ws_mvcc_latest_version (mop2);
		  o2 = WS_OID (mop2);
		}
	    }
	  else
	    {
	      o2 = (OID *) (&oid_Null_oid);
	    }
	}
      else
	{
	  mop2 = ws_mvcc_latest_version (mop2);
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
   * virtual must be 3, impling both the objects are either proxies
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
      o1 = (class1) ? WS_OID (class1) : (OID *) (&oid_Null_oid);
      o2 = (class2) ? WS_OID (class2) : (OID *) (&oid_Null_oid);
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

  c = MR_CMP_RETURN_CODE (c);

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
  mr_data_writeval_object (buf, &value);
  mr_setval_object (&value, NULL, false);
}
#endif /* !SERVER_MODE */

static void
mr_initmem_elo (void *memptr, TP_DOMAIN * domain)
{
  if (memptr != NULL)
    {
      *((DB_ELO **) memptr) = NULL;
    }
}

static void
mr_initval_elo (DB_VALUE * value, int precision, int scale)
{
  /* should not be called */
  assert (0);
}

static void
mr_initval_blob (DB_VALUE * value, int precision, int scale)
{
  DB_ELO *null_elo = NULL;
  db_value_domain_init (value, DB_TYPE_BLOB, precision, scale);
  db_make_elo (value, DB_TYPE_BLOB, null_elo);
}

static void
mr_initval_clob (DB_VALUE * value, int precision, int scale)
{
  DB_ELO *null_elo = NULL;
  db_value_domain_init (value, DB_TYPE_CLOB, precision, scale);
  db_make_elo (value, DB_TYPE_CLOB, null_elo);
}

static int
mr_setmem_elo (void *memptr, TP_DOMAIN * domain, DB_VALUE * value)
{
  int rc;
  DB_ELO *elo, *e;

  if (memptr != NULL)
    {
      mr_freemem_elo (memptr);
      mr_initmem_elo (memptr, domain);

      if (value != NULL && (e = db_get_elo (value)) != NULL)
	{
	  elo = db_private_alloc (NULL, sizeof (DB_ELO));
	  if (elo == NULL)
	    {
	      return ((er_errid () == NO_ERROR) ? ER_FAILED : er_errid ());
	    }
	  rc = elo_copy_structure (e, elo);
	  if (rc != NO_ERROR)
	    {
	      if (elo != NULL)
		{
		  db_private_free_and_init (NULL, elo);
		}
	      return rc;
	    }

	  *((DB_ELO **) memptr) = elo;
	}
    }
  else
    {
      assert_release (0);
    }

  return NO_ERROR;
}

static int
getmem_elo_with_type (void *memptr, TP_DOMAIN * domain, DB_VALUE * value,
		      bool copy, DB_TYPE type)
{
  DB_ELO *elo;
  int r = NO_ERROR;

  if (memptr == NULL)
    {
      PRIM_SET_NULL (value);
      return r;
    }

  elo = *((DB_ELO **) memptr);

  if (elo == NULL || elo->size < 0)
    {
      PRIM_SET_NULL (value);
      return r;
    }

  if (copy)
    {
      DB_ELO e;

      r = elo_copy_structure (elo, &e);
      if (r == NO_ERROR)
	{
	  db_make_elo (value, type, &e);
	  value->need_clear = true;
	}
    }
  else
    {
      db_make_elo (value, type, elo);
    }

  return r;
}

static int
mr_getmem_elo (void *memptr, TP_DOMAIN * domain, DB_VALUE * value, bool copy)
{
  /* should not happen */
  assert (0);
  return ER_FAILED;
}

static int
mr_getmem_blob (void *memptr, TP_DOMAIN * domain, DB_VALUE * value, bool copy)
{
  return getmem_elo_with_type (memptr, domain, value, copy, DB_TYPE_BLOB);
}

static int
mr_getmem_clob (void *memptr, TP_DOMAIN * domain, DB_VALUE * value, bool copy)
{
  return getmem_elo_with_type (memptr, domain, value, copy, DB_TYPE_CLOB);
}

static int
setval_elo_with_type (DB_VALUE * dest, const DB_VALUE * src, bool copy,
		      DB_TYPE type)
{
  int r = NO_ERROR;

  if (DB_IS_NULL (src) || db_get_elo (src) == NULL)
    {
      PRIM_SET_NULL (dest);
      return NO_ERROR;
    }

  if (copy)
    {
      DB_ELO elo;
      DB_ELO *e = db_get_elo (src);

      r = elo_copy_structure (e, &elo);
      if (r == NO_ERROR)
	{
	  db_make_elo (dest, type, &elo);
	  dest->need_clear = true;
	}
    }
  else
    {
      db_make_elo (dest, type, db_get_elo (src));
    }

  return r;
}

static int
mr_setval_elo (DB_VALUE * dest, const DB_VALUE * src, bool copy)
{
  assert (0);
  return ER_FAILED;
}

static int
mr_setval_blob (DB_VALUE * dest, const DB_VALUE * src, bool copy)
{
  return setval_elo_with_type (dest, src, copy, DB_TYPE_BLOB);
}

static int
mr_setval_clob (DB_VALUE * dest, const DB_VALUE * src, bool copy)
{
  return setval_elo_with_type (dest, src, copy, DB_TYPE_CLOB);
}

static int
mr_data_lengthmem_elo (void *memptr, TP_DOMAIN * domain, int disk)
{
  int len = 0;

  if (!disk)
    {
      assert (tp_Elo.size == tp_Blob.size);
      assert (tp_Blob.size == tp_Clob.size);
      len = tp_Elo.size;
    }
  else if (memptr != NULL)
    {
      DB_ELO *elo = *((DB_ELO **) memptr);

      if (elo != NULL && elo->type != ELO_NULL)
	{
	  len =
	    OR_BIGINT_SIZE + OR_LOID_SIZE +
	    or_packed_string_length (elo->locator, NULL) +
	    or_packed_string_length (elo->meta_data, NULL) + OR_INT_SIZE;
	}
    }
  else
    {
      assert_release (0);
    }

  return len;
}

static int
mr_data_lengthval_elo (DB_VALUE * value, int disk)
{
  DB_ELO *elo;

  if (value != NULL)
    {
      elo = db_get_elo (value);
      return mr_data_lengthmem_elo ((void *) &elo, NULL, disk);
    }
  else
    {
      return 0;
    }
}

static void
mr_data_writemem_elo (OR_BUF * buf, void *memptr, TP_DOMAIN * domain)
{
  DB_ELO *elo;

  if (memptr == NULL)
    {
      assert_release (0);
      return;
    }

  elo = *((DB_ELO **) memptr);

  if (elo != NULL && elo->type != ELO_NULL)
    {
      /* size */
      or_put_bigint (buf, elo->size);

      /* loid */
      or_put_loid (buf, &elo->loid);

      /* locator */
      assert (elo->locator != NULL);
      or_put_int (buf,
		  or_packed_string_length (elo->locator, NULL) - OR_INT_SIZE);
      if (elo->locator != NULL)
	{
	  or_put_string (buf, elo->locator);
	}

      /* meta_data */
      or_put_int (buf,
		  or_packed_string_length (elo->meta_data,
					   NULL) - OR_INT_SIZE);
      if (elo->meta_data != NULL)
	{
	  or_put_string (buf, elo->meta_data);
	}

      /* type */
      or_put_int (buf, elo->type);
    }
}

static void
peekmem_elo (OR_BUF * buf, DB_ELO * elo)
{
  int locator_len, meta_data_len;
  int rc = NO_ERROR;

  /* size */
  elo->size = or_get_bigint (buf, &rc);

  if (rc != NO_ERROR)
    {
      assert (false);
      goto error;
    }

  /* loid */
  rc = or_get_loid (buf, &elo->loid);
  if (rc != NO_ERROR)
    {
      assert (false);
      goto error;
    }

  /* locator */
  locator_len = or_get_int (buf, &rc);
  if (rc != NO_ERROR)
    {
      assert (false);
      goto error;
    }
  if (locator_len > 0)
    {
      elo->locator = buf->ptr;
    }
  else
    {
      assert (false);
      goto error;
    }
  rc = or_advance (buf, locator_len);
  if (rc != NO_ERROR)
    {
      assert (false);
      goto error;
    }

  /* meta_data */
  meta_data_len = or_get_int (buf, &rc);
  if (rc != NO_ERROR)
    {
      assert (false);
      goto error;
    }
  if (meta_data_len > 0)
    {
      elo->meta_data = buf->ptr;
    }
  else
    {
      elo->meta_data = NULL;
    }
  rc = or_advance (buf, meta_data_len);
  if (rc != NO_ERROR)
    {
      assert (false);
      goto error;
    }

  /* type */
  elo->type = or_get_int (buf, &rc);
  if (rc != NO_ERROR)
    {
      assert (false);
      goto error;
    }

  return;

error:
  elo->locator = NULL;
  elo->meta_data = NULL;
  elo->size = 0;
  elo->type = ELO_NULL;
}

static void
mr_data_readmem_elo (OR_BUF * buf, void *memptr, TP_DOMAIN * domain, int size)
{
  DB_ELO *elo;
  DB_ELO e;
  int rc = NO_ERROR;

  if (size == 0)
    {
      return;
    }

  if (memptr == NULL)
    {
      or_advance (buf, size);
      return;
    }

  elo = (DB_ELO *) db_private_alloc (NULL, sizeof (DB_ELO));
  if (elo == NULL)
    {
      or_abort (buf);
    }
  else
    {
      peekmem_elo (buf, &e);

      rc = elo_copy_structure (&e, elo);
      if (rc != NO_ERROR)
	{
	  db_private_free_and_init (NULL, elo);
	  or_abort (buf);
	}
    }

  *((DB_ELO **) memptr) = elo;
}

static int
mr_data_writeval_elo (OR_BUF * buf, DB_VALUE * value)
{
  DB_ELO *elo;

  elo = db_get_elo (value);
  mr_data_writemem_elo (buf, (void *) &elo, NULL);
  return NO_ERROR;
}

static int
readval_elo_with_type (OR_BUF * buf, DB_VALUE * value,
		       TP_DOMAIN * domain, int size, bool copy,
		       char *copy_buf, int copy_buf_len, DB_TYPE type)
{
  int rc = NO_ERROR;

  if (value == NULL)
    {
      rc = or_advance (buf, size);
      return rc;
    }

  if (size != 0)
    {
      if (copy)
	{
	  DB_ELO *e = NULL;

	  mr_data_readmem_elo (buf, (void *) &e, NULL, size);
	  /* structure copy - to value->data.elo */
	  rc = db_make_elo (value, type, e);
	  if (e != NULL)
	    {
	      db_private_free_and_init (NULL, e);
	    }

	  value->need_clear = true;
	}
      else
	{
	  DB_ELO elo;

	  peekmem_elo (buf, &elo);
	  /* structure copy - to value->data.elo */
	  rc = db_make_elo (value, type, &elo);
	}
    }

  return rc;
}

static int
mr_data_readval_elo (OR_BUF * buf, DB_VALUE * value,
		     TP_DOMAIN * domain, int size, bool copy,
		     char *copy_buf, int copy_buf_len)
{
  /* should not happen */
  assert (0);
  return ER_FAILED;
}

static int
mr_data_readval_blob (OR_BUF * buf, DB_VALUE * value,
		      TP_DOMAIN * domain, int size, bool copy,
		      char *copy_buf, int copy_buf_len)
{
  return readval_elo_with_type (buf, value, domain, size, copy, copy_buf,
				copy_buf_len, DB_TYPE_BLOB);
}

static int
mr_data_readval_clob (OR_BUF * buf, DB_VALUE * value,
		      TP_DOMAIN * domain, int size, bool copy,
		      char *copy_buf, int copy_buf_len)
{
  return readval_elo_with_type (buf, value, domain, size, copy, copy_buf,
				copy_buf_len, DB_TYPE_CLOB);
}

static void
mr_freemem_elo (void *memptr)
{
  DB_ELO *elo;

  if (memptr != NULL)
    {
      elo = *((DB_ELO **) memptr);

      if (elo != NULL)
	{
	  elo_free_structure (elo);
	  db_private_free_and_init (NULL, elo);
	}
    }
}

static int
mr_data_cmpdisk_elo (void *mem1, void *mem2, TP_DOMAIN * domain,
		     int do_coercion, int total_order, int *start_colp)
{
  assert (domain != NULL);

  /*
   * don't know how to do this since elo's should find their way into
   * listfiles and such.
   */
  return DB_UNK;
}

static int
mr_cmpval_elo (DB_VALUE * value1, DB_VALUE * value2,
	       int do_coercion, int total_order, int *start_colp,
	       int collation)
{
  DB_ELO *elo1, *elo2;

  elo1 = DB_GET_ELO (value1);
  elo2 = DB_GET_ELO (value2);

  /* use address for collating sequence */
  return MR_CMP ((UINTPTR) elo1, (UINTPTR) elo2);
}


/*
 * TYPE VARIABLE
 *
 * Currently this can only be used internally for class objects.  I think
 * this is useful enough to make a general purpose thing.
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
mr_setval_variable (DB_VALUE * dest, const DB_VALUE * src, bool copy)
{
  mr_initval_null (dest, 0, 0);
  return NO_ERROR;
}

static int
mr_data_lengthval_variable (DB_VALUE * value, int disk)
{
  return 0;
}

static int
mr_data_writeval_variable (OR_BUF * buf, DB_VALUE * value)
{
  return NO_ERROR;
}

static int
mr_data_readval_variable (OR_BUF * buf, DB_VALUE * value,
			  TP_DOMAIN * domain, int size, bool copy,
			  char *copy_buf, int copy_buf_len)
{
  return NO_ERROR;
}

static int
mr_data_cmpdisk_variable (void *mem1, void *mem2, TP_DOMAIN * domain,
			  int do_coercion, int total_order, int *start_colp)
{
  assert (domain != NULL);

  return DB_UNK;
}

static int
mr_cmpval_variable (DB_VALUE * value1, DB_VALUE * value2,
		    int do_coercion, int total_order, int *start_colp,
		    int collation)
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
mr_initmem_sub (void *mem, TP_DOMAIN * domain)
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
  return NO_ERROR;
}

static int
mr_getmem_sub (void *mem, TP_DOMAIN * domain, DB_VALUE * value, bool copy)
{
  return NO_ERROR;
}

static int
mr_setval_sub (DB_VALUE * dest, const DB_VALUE * src, bool copy)
{
  return NO_ERROR;
}

static int
mr_data_lengthmem_sub (void *mem, TP_DOMAIN * domain, int disk)
{
  return 0;
}

static int
mr_data_lengthval_sub (DB_VALUE * value, int disk)
{
  return 0;
}

static void
mr_data_writemem_sub (OR_BUF * buf, void *mem, TP_DOMAIN * domain)
{
}

static void
mr_data_readmem_sub (OR_BUF * buf, void *mem, TP_DOMAIN * domain, int size)
{
}

static int
mr_data_writeval_sub (OR_BUF * buf, DB_VALUE * value)
{
  return NO_ERROR;
}

static int
mr_data_readval_sub (OR_BUF * buf, DB_VALUE * value,
		     TP_DOMAIN * domain, int size, bool copy,
		     char *copy_buf, int copy_buf_len)
{
  return NO_ERROR;
}

static int
mr_data_cmpdisk_sub (void *mem1, void *mem2, TP_DOMAIN * domain,
		     int do_coercion, int total_order, int *start_colp)
{
  assert (domain != NULL);

  return DB_UNK;
}

static int
mr_cmpval_sub (DB_VALUE * value1, DB_VALUE * value2,
	       int do_coercion, int total_order, int *start_colp,
	       int collation)
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
mr_initmem_ptr (void *memptr, TP_DOMAIN * domain)
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
      mr_initmem_ptr (mem, domain);
    }
  else
    {
      *mem = db_get_pointer (value);
    }
  return NO_ERROR;
}

static int
mr_getmem_ptr (void *memptr, TP_DOMAIN * domain, DB_VALUE * value, bool copy)
{
  void **mem = (void **) memptr;

  return db_make_pointer (value, *mem);
}

static int
mr_setval_ptr (DB_VALUE * dest, const DB_VALUE * src, bool copy)
{
  if (DB_IS_NULL (src))
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
mr_data_lengthmem_ptr (void *memptr, TP_DOMAIN * domain, int disk)
{
  return 0;
}

static int
mr_data_lengthval_ptr (DB_VALUE * value, int disk)
{
  void *ptr;

  if (value != NULL)
    {
      ptr = db_get_pointer (value);
      return mr_data_lengthmem_ptr (&ptr, NULL, disk);
    }
  else
    {
      return NO_ERROR;
    }
}

static void
mr_data_writemem_ptr (OR_BUF * buf, void *memptr, TP_DOMAIN * domain)
{
}

static void
mr_data_readmem_ptr (OR_BUF * buf, void *memptr, TP_DOMAIN * domain, int size)
{
  void **mem = (void **) memptr;

  *mem = NULL;
}

static int
mr_data_writeval_ptr (OR_BUF * buf, DB_VALUE * value)
{
  return NO_ERROR;
}

static int
mr_data_readval_ptr (OR_BUF * buf, DB_VALUE * value,
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
mr_data_cmpdisk_ptr (void *mem1, void *mem2, TP_DOMAIN * domain,
		     int do_coercion, int total_order, int *start_colp)
{
  assert (domain != NULL);

  /* don't know how to unpack pointers */
  return DB_UNK;
}

static int
mr_cmpval_ptr (DB_VALUE * value1, DB_VALUE * value2,
	       int do_coercion, int total_order, int *start_colp,
	       int collation)
{
  void *p1, *p2;

  p1 = DB_GET_POINTER (value1);
  p2 = DB_GET_POINTER (value2);

  /* use address for collating sequence */
  return MR_CMP ((UINTPTR) p1, (UINTPTR) p2);
}

/*
 * TYPE ERROR
 *
 * This is used only for method arguments, they cannot be attribute values.
 */

static void
mr_initmem_error (void *memptr, TP_DOMAIN * domain)
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
      mr_initmem_error (mem, domain);
    }
  else
    {
      *mem = db_get_error (value);
    }
  return NO_ERROR;
}

static int
mr_getmem_error (void *memptr, TP_DOMAIN * domain, DB_VALUE * value,
		 bool copy)
{
  int *mem = (int *) memptr;

  return db_make_error (value, *mem);
}

static int
mr_setval_error (DB_VALUE * dest, const DB_VALUE * src, bool copy)
{
  if (DB_IS_NULL (src))
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
mr_data_lengthmem_error (void *memptr, TP_DOMAIN * domain, int disk)
{
  return 0;
}

static int
mr_data_lengthval_error (DB_VALUE * value, int disk)
{
  int error;

  if (value != NULL)
    {
      error = db_get_error (value);
      return mr_data_lengthmem_error (&error, NULL, disk);
    }
  else
    {
      return NO_ERROR;
    }
}

static void
mr_data_writemem_error (OR_BUF * buf, void *memptr, TP_DOMAIN * domain)
{
}

static void
mr_data_readmem_error (OR_BUF * buf, void *memptr, TP_DOMAIN * domain,
		       int size)
{
  int *mem = (int *) memptr;

  *mem = NO_ERROR;
}

static int
mr_data_writeval_error (OR_BUF * buf, DB_VALUE * value)
{
  return NO_ERROR;
}

static int
mr_data_readval_error (OR_BUF * buf, DB_VALUE * value,
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
mr_data_cmpdisk_error (void *mem1, void *mem2, TP_DOMAIN * domain,
		       int do_coercion, int total_order, int *start_colp)
{
  assert (domain != NULL);

  /* don't know how to unpack errors */
  return DB_UNK;
}

static int
mr_cmpval_error (DB_VALUE * value1, DB_VALUE * value2,
		 int do_coercion, int total_order, int *start_colp,
		 int collation)
{
  int e1, e2;

  e1 = DB_GET_ERROR (value1);
  e2 = DB_GET_ERROR (value2);

  return MR_CMP (e1, e2);
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
mr_initmem_oid (void *memptr, TP_DOMAIN * domain)
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
      mr_initmem_oid (mem, domain);
    }
  else
    {
      oid = db_get_oid (value);
      if (oid)
	{
	  mem->volid = oid->volid;
	  mem->pageid = oid->pageid;
	  mem->slotid = oid->slotid;
	}
      else
	{
	  return ER_FAILED;
	}
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
mr_setval_oid (DB_VALUE * dest, const DB_VALUE * src, bool copy)
{
  OID *oid;

  if (DB_IS_NULL (src))
    {
      PRIM_SET_NULL (dest);
      return NO_ERROR;
    }
  else
    {
      oid = (OID *) db_get_oid (src);
      return db_make_oid (dest, oid);
    }
}

static void
mr_data_writemem_oid (OR_BUF * buf, void *memptr, TP_DOMAIN * domain)
{
  OID *mem = (OID *) memptr;

  or_put_oid (buf, mem);
}

static void
mr_data_readmem_oid (OR_BUF * buf, void *memptr, TP_DOMAIN * domain, int size)
{
  OID *mem = (OID *) memptr;
  OID oid;

  if (mem != NULL)
    {
      or_get_oid (buf, mem);
    }
  else
    {
      or_get_oid (buf, &oid);	/* skip over it */
    }
}

static int
mr_data_writeval_oid (OR_BUF * buf, DB_VALUE * value)
{
  return (or_put_oid (buf, db_get_oid (value)));
}

static int
mr_data_readval_oid (OR_BUF * buf, DB_VALUE * value,
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
mr_index_writeval_oid (OR_BUF * buf, DB_VALUE * value)
{
  OID *oidp = NULL;
  int rc = NO_ERROR;

  assert (DB_VALUE_TYPE (value) == DB_TYPE_OID
	  || DB_VALUE_TYPE (value) == DB_TYPE_OBJECT);

  oidp = db_get_oid (value);

  rc = or_put_data (buf, (char *) (&oidp->pageid), tp_Integer.disksize);
  if (rc == NO_ERROR)
    {
      rc = or_put_data (buf, (char *) (&oidp->slotid), tp_Short.disksize);
    }
  if (rc == NO_ERROR)
    {
      rc = or_put_data (buf, (char *) (&oidp->volid), tp_Short.disksize);
    }

  return rc;
}

static int
mr_index_readval_oid (OR_BUF * buf, DB_VALUE * value,
		      TP_DOMAIN * domain, int size, bool copy,
		      char *copy_buf, int copy_buf_len)
{
  OID oid;
  int rc = NO_ERROR;

  if (value == NULL)
    {
      rc = or_advance (buf, tp_Object.disksize);
    }
  else
    {
      rc = or_get_data (buf, (char *) (&oid.pageid), tp_Integer.disksize);
      if (rc == NO_ERROR)
	{
	  rc = or_get_data (buf, (char *) (&oid.slotid), tp_Short.disksize);
	}
      if (rc == NO_ERROR)
	{
	  rc = or_get_data (buf, (char *) (&oid.volid), tp_Short.disksize);
	}

      if (rc == NO_ERROR)
	{
	  db_value_domain_init (value, DB_TYPE_OID,
				DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE);
	  db_make_oid (value, &oid);
	}
    }

  return rc;
}

static int
mr_index_cmpdisk_oid (void *mem1, void *mem2, TP_DOMAIN * domain,
		      int do_coercion, int total_order, int *start_colp)
{
  int c;
  OID o1, o2;

  assert (domain != NULL);

  COPYMEM (int, &o1.pageid, (char *) mem1 + OR_OID_PAGEID);
  COPYMEM (short, &o1.slotid, (char *) mem1 + OR_OID_SLOTID);
  COPYMEM (short, &o1.volid, (char *) mem1 + OR_OID_VOLID);

  COPYMEM (int, &o2.pageid, (char *) mem2 + OR_OID_PAGEID);
  COPYMEM (short, &o2.slotid, (char *) mem2 + OR_OID_SLOTID);
  COPYMEM (short, &o2.volid, (char *) mem2 + OR_OID_VOLID);

  c = oid_compare (&o1, &o2);
  c = MR_CMP_RETURN_CODE (c);

  return c;
}

static int
mr_data_cmpdisk_oid (void *mem1, void *mem2, TP_DOMAIN * domain,
		     int do_coercion, int total_order, int *start_colp)
{
  int c;
  OID o1, o2;

  assert (domain != NULL);

  OR_GET_OID (mem1, &o1);
  OR_GET_OID (mem2, &o2);

  c = oid_compare (&o1, &o2);
  c = MR_CMP_RETURN_CODE (c);

  return c;
}

static int
mr_cmpval_oid (DB_VALUE * value1, DB_VALUE * value2,
	       int do_coercion, int total_order, int *start_colp,
	       int collation)
{
  int c;
  OID *oid1, *oid2;

  oid1 = DB_GET_OID (value1);
  oid2 = DB_GET_OID (value2);

  if (oid1 == NULL || oid2 == NULL)
    {
      return DB_UNK;
    }

  c = oid_compare (oid1, oid2);
  c = MR_CMP_RETURN_CODE (c);

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
mr_initmem_set (void *memptr, TP_DOMAIN * domain)
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
      mr_initmem_set (mem, domain);
    }
  return error;
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
	  assert (er_errid () != NO_ERROR);
	  error = er_errid ();
	  (void) db_make_set (value, NULL);
	}
    }
  /*
   * NOTE: assumes that ownership info will already have been set or will
   * be set by the caller
   */

  return error;
}

static int
mr_setval_set_internal (DB_VALUE * dest, const DB_VALUE * src,
			bool copy, DB_TYPE set_type)
{
  int error = NO_ERROR;
  SETREF *src_ref, *ref;

  if (src != NULL && !DB_IS_NULL (src)
      && ((src_ref = db_get_set (src)) != NULL))
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
		{
		  goto err_set;
		}
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
	case DB_TYPE_SET:
	  db_make_set (dest, ref);
	  break;
	case DB_TYPE_MULTISET:
	  db_make_multiset (dest, ref);
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
      db_value_domain_init (dest, set_type, DB_DEFAULT_PRECISION,
			    DB_DEFAULT_SCALE);
    }
  return error;

err_set:
  /* couldn't allocate storage for set */
  assert (er_errid () != NO_ERROR);
  error = er_errid ();
  switch (set_type)
    {
    case DB_TYPE_SET:
      db_make_set (dest, NULL);
      break;
    case DB_TYPE_MULTISET:
      db_make_multiset (dest, NULL);
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
mr_setval_set (DB_VALUE * dest, const DB_VALUE * src, bool copy)
{
  return mr_setval_set_internal (dest, src, copy, DB_TYPE_SET);
}

static int
mr_data_lengthmem_set (void *memptr, TP_DOMAIN * domain, int disk)
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
mr_data_lengthval_set (DB_VALUE * value, int disk)
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
mr_data_writemem_set (OR_BUF * buf, void *memptr, TP_DOMAIN * domain)
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
mr_data_writeval_set (OR_BUF * buf, DB_VALUE * value)
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
       */
      if (ref->disk_set)
	{
	  /* check for overflow */
	  if ((((ptrdiff_t) (buf->endptr - buf->ptr)) <
	       (ptrdiff_t) ref->disk_size))
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
#if !defined (SERVER_MODE)
		  pin = ws_pin (ref->owner, 1);
#endif
		  size = or_packed_set_length (set, 1);
		  /* remember the Windows pointer problem ! */
		  if (((ptrdiff_t) (buf->endptr -
				    buf->ptr)) < (ptrdiff_t) size)
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
mr_data_readmem_set (OR_BUF * buf, void *memptr, TP_DOMAIN * domain, int size)
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
mr_data_readval_set (OR_BUF * buf, DB_VALUE * value,
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
	  db_value_domain_init (value, TP_DOMAIN_TYPE (domain),
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
		    case DB_TYPE_SET:
		      db_make_set (value, ref);
		      break;
		    case DB_TYPE_MULTISET:
		      db_make_multiset (value, ref);
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
		case DB_TYPE_SET:
		  db_make_set (value, ref);
		  break;
		case DB_TYPE_MULTISET:
		  db_make_multiset (value, ref);
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
mr_data_cmpdisk_set (void *mem1, void *mem2, TP_DOMAIN * domain,
		     int do_coercion, int total_order, int *start_colp)
{
  int c;
  SETOBJ *set1 = NULL, *set2 = NULL;

  assert (domain != NULL);

  /* is not index type */
  assert (!domain->is_desc && !tp_valid_indextype (TP_DOMAIN_TYPE (domain)));

  mem1 = or_unpack_set ((char *) mem1, &set1, domain);
  mem2 = or_unpack_set ((char *) mem2, &set2, domain);

  if (set1 == NULL || set2 == NULL)
    {
      return DB_UNK;
    }

  c = setobj_compare_order (set1, set2, do_coercion, total_order);

  setobj_free (set1);
  setobj_free (set2);

  return c;
}

static int
mr_cmpval_set (DB_VALUE * value1, DB_VALUE * value2,
	       int do_coercion, int total_order, int *start_colp,
	       int collation)
{
  int c;

  c = set_compare_order (db_get_set (value1), db_get_set (value2),
			 do_coercion, total_order);

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
	  assert (er_errid () != NO_ERROR);
	  error = er_errid ();
	  (void) db_make_multiset (value, NULL);
	}
    }
  /* NOTE: assumes that ownership info will already have been set or will
     be set by the caller */

  return error;
}

static int
mr_setval_multiset (DB_VALUE * dest, const DB_VALUE * src, bool copy)
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
	  assert (er_errid () != NO_ERROR);
	  error = er_errid ();
	  (void) db_make_sequence (value, NULL);
	}
    }
  /*
   * NOTE: assumes that ownership info will already have been set or will
   * be set by the caller
   */

  return error;
}

static int
mr_setval_sequence (DB_VALUE * dest, const DB_VALUE * src, bool copy)
{
  return mr_setval_set_internal (dest, src, copy, DB_TYPE_SEQUENCE);
}

static int
mr_data_cmpdisk_sequence (void *mem1, void *mem2, TP_DOMAIN * domain,
			  int do_coercion, int total_order, int *start_colp)
{
  int c;
  SETOBJ *seq1 = NULL, *seq2 = NULL;

  assert (domain != NULL);

  /* is not index type */
  assert (!domain->is_desc && !tp_valid_indextype (TP_DOMAIN_TYPE (domain)));

  mem1 = or_unpack_set ((char *) mem1, &seq1, domain);
  mem2 = or_unpack_set ((char *) mem2, &seq2, domain);

  if (seq1 == NULL || seq2 == NULL)
    {
      return DB_UNK;
    }

  c = setobj_compare_order (seq1, seq2, do_coercion, total_order);

  setobj_free (seq1);
  setobj_free (seq2);

  return c;
}

static int
mr_cmpval_sequence (DB_VALUE * value1, DB_VALUE * value2,
		    int do_coercion, int total_order, int *start_colp,
		    int collation)
{
  int c;

  c = set_seq_compare (db_get_set (value1), db_get_set (value2),
		       do_coercion, total_order);

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
mr_setval_midxkey (DB_VALUE * dest, const DB_VALUE * src, bool copy)
{
  int error = NO_ERROR;
  int src_precision;

  DB_MIDXKEY dst_idx;
  DB_MIDXKEY *src_idx;

  if (DB_IS_NULL (src))
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

	  assert (er_errid () != NO_ERROR);
	  return er_errid ();
	}

      memcpy (dst_idx.buf, src_idx->buf, dst_idx.size);
      error = db_make_midxkey (dest, &dst_idx);
      dest->need_clear = true;
    }

  return error;
}

static int
mr_data_writeval_midxkey (OR_BUF * buf, DB_VALUE * value)
{
  return mr_index_writeval_midxkey (buf, value);
}

static int
mr_index_writeval_midxkey (OR_BUF * buf, DB_VALUE * value)
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
mr_data_readval_midxkey (OR_BUF * buf, DB_VALUE * value,
			 TP_DOMAIN * domain, int size, bool copy,
			 char *copy_buf, int copy_buf_len)
{
  return mr_index_readval_midxkey (buf, value, domain, size, copy,
				   copy_buf, copy_buf_len);
}

static int
mr_index_readval_midxkey (OR_BUF * buf, DB_VALUE * value,
			  TP_DOMAIN * domain, int size, bool copy,
			  char *copy_buf, int copy_buf_len)
{
  char *new_;
  DB_MIDXKEY midxkey;
  int rc = NO_ERROR;
  TP_DOMAIN *dom;

  if (size == -1)
    {				/* unknown size */
      size = mr_index_lengthmem_midxkey (buf->ptr, domain);
    }

  if (size <= 0)
    {
      assert (false);
      return ER_FAILED;
    }

  if (value == NULL)
    {
      return or_advance (buf, size);
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
	  db_value_domain_init (value, TP_DOMAIN_TYPE (domain),
				TP_FLOATING_PRECISION_VALUE, 0);
	  or_abort (buf);
	  return ER_FAILED;
	}
      else
	{
	  rc = or_get_data (buf, new_, size);
	  if (rc == NO_ERROR)
	    {
	      /* round up to a word boundary */
	      /*rc = or_get_align32 (buf); *//* need ?? */
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
pr_midxkey_compare_element (char *mem1, char *mem2,
			    TP_DOMAIN * dom1, TP_DOMAIN * dom2,
			    int do_coercion, int total_order)
{
  int c;
  DB_VALUE val1, val2;
  OR_BUF buf_val1, buf_val2;
  bool comparable = true;

  if (dom1->is_desc != dom2->is_desc)
    {
      assert (false);
      return DB_UNK;		/* impossible case */
    }

  OR_BUF_INIT (buf_val1, mem1, -1);
  OR_BUF_INIT (buf_val2, mem2, -1);

  if ((*(dom1->type->index_readval)) (&buf_val1, &val1, dom1, -1, false,
				      NULL, 0) != NO_ERROR)
    {
      return DB_UNK;
    }

  if ((*(dom2->type->index_readval)) (&buf_val2, &val2, dom2, -1, false,
				      NULL, 0) != NO_ERROR)
    {
      return DB_UNK;
    }

  c =
    tp_value_compare_with_error (&val1, &val2, do_coercion, total_order,
				 &comparable);
  if (!comparable)
    {
      return DB_UNK;
    }

  return c;
}


int
pr_midxkey_compare (DB_MIDXKEY * mul1, DB_MIDXKEY * mul2,
		    int do_coercion, int total_order, int num_index_term,
		    int *start_colp, int *result_size1,
		    int *result_size2, int *diff_column,
		    bool * dom_is_desc, bool * next_dom_is_desc)
{
  int c = DB_UNK;
  int i;
  int adv_size1, adv_size2;
  int size1, size2;
  TP_DOMAIN *dom1, *dom2;
  char *bitptr1, *bitptr2;
  char *mem1, *mem2;
  int last;

  assert (total_order == 1);
  if (total_order == 0)
    {
      /* unknown case */
      return DB_UNK;
    }

  assert (mul1->domain != NULL);
  assert (TP_DOMAIN_TYPE (mul1->domain) == DB_TYPE_MIDXKEY);
  assert (mul1->domain->setdomain != NULL);

  assert (mul2->domain != NULL);
  assert (TP_DOMAIN_TYPE (mul2->domain) == DB_TYPE_MIDXKEY);
  assert (mul2->domain->setdomain != NULL);

  assert (mul1->ncolumns == mul2->ncolumns);
  assert (mul1->domain->precision == mul2->domain->precision);

  /* safe guard */
  if (mul1->domain == NULL
      || TP_DOMAIN_TYPE (mul1->domain) != DB_TYPE_MIDXKEY
      || mul1->domain->setdomain == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_MR_NULL_DOMAIN, 0);
      return DB_UNK;
    }

  /* safe guard */
  if (mul2->domain == NULL
      || TP_DOMAIN_TYPE (mul2->domain) != DB_TYPE_MIDXKEY
      || mul2->domain->setdomain == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_MR_NULL_DOMAIN, 0);
      return DB_UNK;
    }

  /* safe guard */
  if (mul1->ncolumns != mul2->ncolumns
      || mul1->domain->precision != mul2->domain->precision)
    {
      return DB_UNK;
    }

#if !defined(NDEBUG)
  {
    int dom_ncols = 0;		/* init */

    for (dom1 = mul1->domain->setdomain; dom1; dom1 = dom1->next)
      {
	dom_ncols++;
      }

    if (dom_ncols <= 0)
      {
	assert (false);
	return DB_UNK;
      }

    assert (dom_ncols == mul1->domain->precision);
  }
#endif /* NDEBUG */

  size1 = size2 = 0;

  mem1 = bitptr1 = mul1->buf;
  mem2 = bitptr2 = mul2->buf;

  adv_size1 = OR_MULTI_BOUND_BIT_BYTES (mul1->domain->precision);

  mem1 += adv_size1;
  mem2 += adv_size1;
  size1 += adv_size1;
  size2 += adv_size1;

  dom1 = mul1->domain->setdomain;
  dom2 = mul2->domain->setdomain;

  if (num_index_term > 0)
    {
      last = num_index_term;
    }
  else
    {
      last = mul1->ncolumns;
    }

  for (i = 0;
       start_colp && i < *start_colp;
       i++, dom1 = dom1->next, dom2 = dom2->next)
    {
      if (dom1 == NULL || dom2 == NULL || dom1->is_desc != dom2->is_desc)
	{
	  assert (false);
	  return DB_UNK;
	}

      if (OR_MULTI_ATT_IS_BOUND (bitptr1, i))
	{
	  adv_size1 = pr_midxkey_element_disk_size (mem1, dom1);
	  mem1 += adv_size1;
	  size1 += adv_size1;
	}

      if (OR_MULTI_ATT_IS_BOUND (bitptr2, i))
	{
	  adv_size2 = pr_midxkey_element_disk_size (mem2, dom2);
	  mem2 += adv_size2;
	  size2 += adv_size2;
	}
    }

  for (c = DB_EQ; i < last; i++, dom1 = dom1->next, dom2 = dom2->next)
    {
      if (dom1 == NULL || dom2 == NULL || dom1->is_desc != dom2->is_desc)
	{
	  assert (false);
	  return DB_UNK;
	}

      if (OR_MULTI_ATT_IS_BOUND (bitptr1, i)
	  && OR_MULTI_ATT_IS_BOUND (bitptr2, i))
	{
	  /* check for val1 and val2 same domain */
	  if (dom1 == dom2 || tp_domain_match (dom1, dom2, TP_EXACT_MATCH))
	    {
	      c = (*(dom1->type->index_cmpdisk)) (mem1, mem2, dom1,
						  do_coercion, total_order,
						  NULL);
	    }
	  else
	    {			/* coercion and comparision */
	      /* val1 and val2 have different domain */
	      c = pr_midxkey_compare_element (mem1, mem2, dom1, dom2,
					      do_coercion, total_order);
	    }
	}
      else
	{
	  if (OR_MULTI_ATT_IS_BOUND (bitptr1, i))
	    {
	      /* val 1 bound, val 2 unbound */
	      c = DB_GT;
	    }
	  else if (OR_MULTI_ATT_IS_BOUND (bitptr2, i))
	    {
	      /* val 1 unbound, val 2 bound */
	      c = DB_LT;
	    }
	  else
	    {
	      /* val 1 unbound, val 2 unbound */
	      c = DB_EQ;
	    }
	}

      if (c != DB_EQ)
	{
	  break;		/* exit for-loop */
	}

      if (OR_MULTI_ATT_IS_BOUND (bitptr1, i))
	{
	  adv_size1 = pr_midxkey_element_disk_size (mem1, dom1);
	  mem1 += adv_size1;
	  size1 += adv_size1;
	}

      if (OR_MULTI_ATT_IS_BOUND (bitptr2, i))
	{
	  adv_size2 = pr_midxkey_element_disk_size (mem2, dom2);
	  mem2 += adv_size2;
	  size2 += adv_size2;
	}
    }

  if (start_colp != NULL)
    {
      if (c != DB_EQ)
	{
	  /* save the start position of non-equal-value column */
	  *start_colp = i;
	}
    }

  adv_size1 = adv_size2 = 0;
  if (c != DB_EQ)
    {
      if (dom1 != NULL && OR_MULTI_ATT_IS_BOUND (bitptr1, i))
	{
	  adv_size1 = pr_midxkey_element_disk_size (mem1, dom1);
	}

      if (dom2 != NULL && OR_MULTI_ATT_IS_BOUND (bitptr2, i))
	{
	  adv_size2 = pr_midxkey_element_disk_size (mem2, dom2);
	}
    }

  *result_size1 = size1 + adv_size1;
  *result_size2 = size2 + adv_size2;

  *diff_column = i;

  *dom_is_desc = *next_dom_is_desc = false;

  if (dom1)
    {
      if (dom1->is_desc)
	{
	  *dom_is_desc = true;
	}

      if (dom1->next && dom1->next->is_desc)
	{
	  *next_dom_is_desc = true;
	}
    }

  return c;
}

static int
mr_cmpval_midxkey (DB_VALUE * value1, DB_VALUE * value2,
		   int do_coercion, int total_order, int *start_colp,
		   int collation)
{
  int c = DB_UNK;
  DB_MIDXKEY *midxkey1;
  DB_MIDXKEY *midxkey2;
  int dummy_size1, dummy_size2, dummy_diff_column;
  bool dummy_dom_is_desc, dummy_next_dom_is_desc;

  midxkey1 = DB_GET_MIDXKEY (value1);
  midxkey2 = DB_GET_MIDXKEY (value2);

  if (midxkey1 == NULL || midxkey2 == NULL)
    {
      assert_release (false);	/* error */
      return DB_UNK;
    }

  if (midxkey1 == midxkey2)
    {
      if (total_order)
	{
	  return DB_EQ;
	}

      assert_release (false);	/* error */
      return DB_UNK;
    }

  assert_release (midxkey1->domain != NULL);
  assert_release (midxkey1->domain->precision == midxkey1->ncolumns);
  assert_release (midxkey2->domain != NULL);
  assert_release (midxkey2->domain->precision == midxkey2->ncolumns);

  c = pr_midxkey_compare (midxkey1, midxkey2, do_coercion,
			  total_order, -1, start_colp,
			  &dummy_size1, &dummy_size2, &dummy_diff_column,
			  &dummy_dom_is_desc, &dummy_next_dom_is_desc);

  assert_release (c == DB_UNK || (DB_LT <= c && c <= DB_GT));

  return c;
}

static int
mr_data_cmpdisk_midxkey (void *mem1, void *mem2, TP_DOMAIN * domain,
			 int do_coercion, int total_order, int *start_colp)
{
  assert (false);

  assert (domain != NULL);

  return mr_index_cmpdisk_midxkey (mem1, mem2, domain,
				   do_coercion, total_order, start_colp);
}

static int
mr_index_cmpdisk_midxkey (void *mem1, void *mem2, TP_DOMAIN * domain,
			  int do_coercion, int total_order, int *start_colp)
{
  int c = DB_UNK;
  DB_MIDXKEY midxkey1;
  DB_MIDXKEY midxkey2;
  TP_DOMAIN *cmp_dom;
  int n_atts = 0;
  int dummy_size1, dummy_size2, dummy_diff_column;
  bool dummy_dom_is_desc = false, dummy_next_dom_is_desc;

  assert (false);

  assert (domain != NULL && !domain->is_desc);

  assert (mem1 != NULL);
  assert (mem2 != NULL);

  if (mem1 == NULL || mem2 == NULL)
    {
      assert (false);		/* error */
      return DB_UNK;
    }

  if (mem1 == mem2)
    {
      if (total_order)
	{
	  return DB_EQ;
	}

      assert (false);		/* error */
      return DB_UNK;
    }

  midxkey1.buf = (char *) mem1;
  midxkey2.buf = (char *) mem2;

  n_atts = 0;
  for (cmp_dom = domain->setdomain; cmp_dom; cmp_dom = cmp_dom->next)
    {
      n_atts++;
    }

  midxkey1.size = midxkey2.size = -1;	/* is dummy */
  midxkey1.ncolumns = midxkey2.ncolumns = n_atts;
  midxkey1.domain = midxkey2.domain = domain;

  c = pr_midxkey_compare (&midxkey1, &midxkey2, do_coercion,
			  total_order, -1, start_colp,
			  &dummy_size1, &dummy_size2, &dummy_diff_column,
			  &dummy_dom_is_desc, &dummy_next_dom_is_desc);
  assert (c == DB_UNK || (DB_LT <= c && c <= DB_GT));

  return c;
}

static int
mr_data_lengthmem_midxkey (void *memptr, TP_DOMAIN * domain, int disk)
{
  return mr_index_lengthmem_midxkey (memptr, domain);
}

static int
mr_index_lengthmem_midxkey (void *memptr, TP_DOMAIN * domain)
{
  char *buf, *bitptr;
  TP_DOMAIN *dom;
  int idx_ncols = 0, i, adv_size;
  int len;

  /* There is no difference between the disk & memory sizes. */
  buf = (char *) memptr;

  idx_ncols = domain->precision;
  if (idx_ncols <= 0)
    {
      assert (false);
      goto exit_on_error;	/* give up */
    }

#if !defined (NDEBUG)
  {
    int dom_ncols = 0;
    for (dom = domain->setdomain; dom; dom = dom->next)
      {
	dom_ncols++;
      }

    if (dom_ncols <= 0)
      {
	assert (false);
	goto exit_on_error;
      }
    assert (dom_ncols == idx_ncols);
  }
#endif /* NDEBUG */

  adv_size = OR_MULTI_BOUND_BIT_BYTES (idx_ncols);

  bitptr = buf;
  buf += adv_size;
  assert (CAST_BUFLEN (buf - bitptr) > 0);

  for (i = 0, dom = domain->setdomain; i < idx_ncols; i++, dom = dom->next)
    {
      /* check for val is NULL */
      if (OR_MULTI_ATT_IS_UNBOUND (bitptr, i))
	{
	  continue;		/* zero size; go ahead */
	}

      /* at here, val is non-NULL */

      adv_size = pr_midxkey_element_disk_size (buf, dom);
      buf += adv_size;
    }

  /* set buf size */
  len = CAST_BUFLEN (buf - bitptr);

exit_on_end:

  return len;

exit_on_error:

  len = -1;			/* set error */
  goto exit_on_end;
}

static int
mr_data_lengthval_midxkey (DB_VALUE * value, int disk)
{
  return mr_index_lengthval_midxkey (value);
}

static int
mr_index_lengthval_midxkey (DB_VALUE * value)
{
  int len;

  if (DB_IS_NULL (value))
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
mr_setval_vobj (DB_VALUE * dest, const DB_VALUE * src, bool copy)
{
  int error;

  error = mr_setval_sequence (dest, src, copy);
  db_value_alter_type (dest, DB_TYPE_VOBJ);

  return error;
}

static int
mr_data_readval_vobj (OR_BUF * buf, DB_VALUE * value,
		      TP_DOMAIN * domain, int size, bool copy,
		      char *copy_buf, int copy_buf_len)
{
  if (mr_data_readval_set (buf, value, &tp_Sequence_domain, size, copy,
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
mr_data_cmpdisk_vobj (void *mem1, void *mem2, TP_DOMAIN * domain,
		      int do_coercion, int total_order, int *start_colp)
{
  int c;
  SETOBJ *seq1 = NULL, *seq2 = NULL;

  assert (domain != NULL);

  /* is not index type */
  assert (!domain->is_desc && !tp_valid_indextype (TP_DOMAIN_TYPE (domain)));

  mem1 = or_unpack_set ((char *) mem1, &seq1, domain);
  mem2 = or_unpack_set ((char *) mem2, &seq2, domain);

  if (seq1 == NULL || seq2 == NULL)
    {
      return DB_UNK;
    }

  c = setvobj_compare (seq1, seq2, do_coercion, total_order);

  setobj_free (seq1);
  setobj_free (seq2);

  return c;
}

static int
mr_cmpval_vobj (DB_VALUE * value1, DB_VALUE * value2,
		int do_coercion, int total_order, int *start_colp,
		int collation)
{
  int c;

  c = vobj_compare (db_get_set (value1), db_get_set (value2), do_coercion,
		    total_order);

  return c;
}

/*
 * TYPE NUMERIC
 */

static void
mr_initmem_numeric (void *memptr, TP_DOMAIN * domain)
{
  assert (!IS_FLOATING_PRECISION (domain->precision));

  memset (memptr, 0, MR_NUMERIC_SIZE (domain->precision));
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
      mr_initmem_numeric (mem, domain);
    }
  else
    {
      src_num = DB_GET_NUMERIC (value);

      src_precision = db_value_precision (value);
      src_scale = db_value_scale (value);

      /* this should have been handled by now */
      if (src_num == NULL || src_precision != domain->precision
	  || src_scale != domain->scale)
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
mr_data_writemem_numeric (OR_BUF * buf, void *mem, TP_DOMAIN * domain)
{
  int disk_size;

  disk_size = OR_NUMERIC_SIZE (domain->precision);
  or_put_data (buf, (char *) mem, disk_size);
}

static void
mr_data_readmem_numeric (OR_BUF * buf, void *mem, TP_DOMAIN * domain,
			 int size)
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
mr_index_lengthmem_numeric (void *mem, TP_DOMAIN * domain)
{
  return mr_data_lengthmem_numeric (mem, domain, 1);
}

static int
mr_data_lengthmem_numeric (void *mem, TP_DOMAIN * domain, int disk)
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
mr_setval_numeric (DB_VALUE * dest, const DB_VALUE * src, bool copy)
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
      src_numeric = (DB_C_NUMERIC) DB_GET_NUMERIC (src);

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
mr_index_lengthval_numeric (DB_VALUE * value)
{
  return mr_data_lengthval_numeric (value, 1);
}

static int
mr_data_lengthval_numeric (DB_VALUE * value, int disk)
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
mr_index_writeval_numeric (OR_BUF * buf, DB_VALUE * value)
{
  return mr_data_writeval_numeric (buf, value);
}

static int
mr_data_writeval_numeric (OR_BUF * buf, DB_VALUE * value)
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
mr_index_readval_numeric (OR_BUF * buf, DB_VALUE * value,
			  TP_DOMAIN * domain, int size, bool copy,
			  char *copy_buf, int copy_buf_len)
{
  return mr_data_readval_numeric (buf, value, domain, size, copy, copy_buf,
				  copy_buf_len);
}

static int
mr_data_readval_numeric (OR_BUF * buf, DB_VALUE * value,
			 TP_DOMAIN * domain, int size, bool copy,
			 char *copy_buf, int copy_buf_len)
{
  int rc = NO_ERROR;

  if (domain == NULL)
    {
      return ER_FAILED;
    }

  /*
   * If size is -1, the caller doesn't know the size and we must determine
   * it from the domain.
   */
  if (size == -1)
    {
      size = OR_NUMERIC_SIZE (domain->precision);
    }

  if (size == 1)
    {
      size = OR_NUMERIC_SIZE (domain->precision);
    }

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
mr_index_cmpdisk_numeric (void *mem1, void *mem2, TP_DOMAIN * domain,
			  int do_coercion, int total_order, int *start_colp)
{
  assert (domain != NULL);

  return mr_data_cmpdisk_numeric (mem1, mem2, domain, do_coercion,
				  total_order, start_colp);
}

static int
mr_data_cmpdisk_numeric (void *mem1, void *mem2, TP_DOMAIN * domain,
			 int do_coercion, int total_order, int *start_colp)
{
  int c = DB_UNK;
  OR_BUF buf;
  DB_VALUE value1, value2;
  DB_VALUE answer;
  int rc = NO_ERROR;

  assert (domain != NULL);

  or_init (&buf, (char *) mem1, 0);
  rc = mr_data_readval_numeric (&buf, &value1, domain, -1, 0, NULL, 0);
  if (rc != NO_ERROR)
    {
      return DB_UNK;
    }

  or_init (&buf, (char *) mem2, 0);
  rc = mr_data_readval_numeric (&buf, &value2, domain, -1, 0, NULL, 0);
  if (rc != NO_ERROR)
    {
      return DB_UNK;
    }

  rc = numeric_db_value_compare (&value1, &value2, &answer);
  if (rc != NO_ERROR)
    {
      return DB_UNK;
    }

  c = MR_CMP_RETURN_CODE (DB_GET_INT (&answer));

  return c;
}

static int
mr_cmpval_numeric (DB_VALUE * value1, DB_VALUE * value2,
		   int do_coercion, int total_order, int *start_colp,
		   int collation)
{
  int c = DB_UNK;
  DB_VALUE answer;

  if (numeric_db_value_compare (value1, value2, &answer) != NO_ERROR)
    {
      return DB_UNK;
    }

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
	  if (tp_Type_id_map[t]->size > cur_size
	      && tp_Type_id_map[t]->size < last_size)
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

  if (id <= DB_TYPE_LAST && id != DB_TYPE_TABLE)
    {
      type = tp_Type_id_map[(int) id];
    }

  return type;
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

  return name;
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

  if (TP_IS_SET_TYPE (type) || type == DB_TYPE_VOBJ)
    {
      status = 1;
    }

  return status;
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

  return status;
}

/*
 * pr_is_prefix_key_type -
 * types.
 *    return:
 *    type(in):  type to check
 */
int
pr_is_prefix_key_type (DB_TYPE type)
{
  return (type == DB_TYPE_MIDXKEY || pr_is_string_type (type));
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

  if (name == NULL)
    {
      return NULL;
    }

  found = NULL;
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

  return found;
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
 *
 */
int
pr_mem_size (PR_TYPE * type)
{
  return type->size;
}

#if defined(ENABLE_UNUSED_FUNCTION)
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
  return size;
}
#endif /* ENABLE_UNUSED_FUNCTION */

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

  if (type->data_lengthmem != NULL)
    {
      size = (*type->data_lengthmem) (mem, NULL, 0);
    }
  else
    {
      size = type->size;
    }

  return size;
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
      if (type->data_lengthval != NULL)
	{
	  size = (*type->data_lengthval) (value, 0);
	}
      else
	{
	  size = type->size;
	}
    }

  return size;
}

/*
 * pr_midxkey_element_disk_size - returns the number of bytes that will be
 * written by the "index_write" type function for this memory buffer.
 *    return: byte size of disk representation
 *    mem(in): memory buffer
 *    domain(in): type domain
 */
int
pr_midxkey_element_disk_size (char *mem, DB_DOMAIN * domain)
{
  int disk_size = 0;

  /*
   * variable types except VARCHAR, VARNCHAR, and VARBIT
   * cannot be a member of midxkey
   */
  assert (!(domain->type->variable_p
	    && !QSTR_IS_VARIABLE_LENGTH (TP_DOMAIN_TYPE (domain))));

  if (domain->type->index_lengthmem != NULL)
    {
      disk_size = (*(domain->type->index_lengthmem)) (mem, domain);
    }
  else
    {
      assert (!domain->type->variable_p);

      disk_size = domain->type->disksize;
    }

  return disk_size;
}

/*
 * pr_midxkey_get_vals_size() -
 *      return: int
 *  domains(in) :
 *  dbvals(in) :
 *  total(in) :
 *
 */

static int
pr_midxkey_get_vals_size (TP_DOMAIN * domains, DB_VALUE * dbvals, int total)
{
  TP_DOMAIN *dom;
  int i;

  for (dom = domains, i = 0; dom; dom = dom->next, i++)
    {
      if (DB_IS_NULL (&dbvals[i]))
	{
	  continue;
	}

      total += pr_index_writeval_disk_size (&dbvals[i]);
    }

  return total;
}


/*
 * pr_midxkey_get_element_offset - Returns element offset of midxkey
 *    return: 
 *    midxkey(in):
 *    index(in):
 */
int
pr_midxkey_get_element_offset (const DB_MIDXKEY * midxkey, int index)
{
  int idx_ncols = 0, i;
  int advance_size;
  int error = NO_ERROR;

  TP_DOMAIN *domain;

  OR_BUF buf_space;
  OR_BUF *buf;
  char *bitptr;

  idx_ncols = midxkey->domain->precision;
  if (idx_ncols <= 0)
    {
      assert (false);
      goto exit_on_error;
    }

  if (index >= midxkey->ncolumns)
    {
      assert (false);
      goto exit_on_error;
    }

  /* get bit-mask */
  bitptr = midxkey->buf;
  /* get domain list, attr number */
  domain = midxkey->domain->setdomain;	/* first element's domain */

  {
    buf = NULL;			/* init */
    i = 0;			/* init */

    /* 2nd phase: need to set buf info */
    if (buf == NULL)
      {
	buf = &buf_space;
	or_init (buf, midxkey->buf, midxkey->size);

	advance_size = OR_MULTI_BOUND_BIT_BYTES (idx_ncols);
	if (or_advance (buf, advance_size) != NO_ERROR)
	  {
	    goto exit_on_error;
	  }
      }

    for (; i < index; i++, domain = domain->next)
      {
	/* check for element is NULL */
	if (OR_MULTI_ATT_IS_UNBOUND (bitptr, i))
	  {
	    continue;		/* skip and go ahead */
	  }

	advance_size = pr_midxkey_element_disk_size (buf->ptr, domain);
	or_advance (buf, advance_size);
      }

    if (error != NO_ERROR)
      {
	goto exit_on_error;
      }
  }

  return buf->ptr - buf->buffer;

exit_on_error:

  assert (false);
  return -1;
}



/*
 * pr_midxkey_add_prefix - 
 *
 *    return: 
 *    prefix(in):
 *    postfix(in):
 *    result(out):
 *    n_prefix(in):
 */
int
pr_midxkey_add_prefix (DB_VALUE * result, DB_VALUE * prefix,
		       DB_VALUE * postfix, int n_prefix)
{
  int i, j, k, offset_postfix, offset_prefix;
  DB_MIDXKEY *midx_postfix, *midx_prefix;
  DB_MIDXKEY midx_result;

  assert (DB_VALUE_TYPE (prefix) == DB_TYPE_MIDXKEY);
  assert (DB_VALUE_TYPE (postfix) == DB_TYPE_MIDXKEY);

  midx_prefix = DB_PULL_MIDXKEY (prefix);
  midx_postfix = DB_PULL_MIDXKEY (postfix);

  offset_prefix = pr_midxkey_get_element_offset (midx_prefix, n_prefix);
  offset_postfix = pr_midxkey_get_element_offset (midx_postfix, n_prefix);

  midx_result.size = offset_prefix + (midx_postfix->size - offset_postfix);
  midx_result.buf = db_private_alloc (NULL, midx_result.size);
  midx_result.domain = midx_postfix->domain;
  midx_result.ncolumns = midx_postfix->ncolumns;

  memcpy (midx_result.buf, midx_prefix->buf, offset_prefix);

#if !defined(NDEBUG)
  for (j = 0; j < n_prefix; j++)
    {
      assert (!OR_MULTI_ATT_IS_BOUND (midx_postfix->buf, j));
    }
#endif

  for (j = n_prefix; j < midx_result.ncolumns; j++)
    {
      if (OR_MULTI_ATT_IS_BOUND (midx_postfix->buf, j))
	{
	  OR_MULTI_ENABLE_BOUND_BIT (midx_result.buf, j);
	}
      else
	{
	  OR_MULTI_CLEAR_BOUND_BIT (midx_result.buf, j);
	}
    }

  memcpy (midx_result.buf + offset_prefix,
	  midx_postfix->buf + offset_postfix,
	  midx_postfix->size - offset_postfix);

  DB_MAKE_MIDXKEY (result, &midx_result);
  result->need_clear = true;

  return NO_ERROR;
}


/*
 * pr_midxkey_remove_prefix - 
 *
 *    return: 
 *    key(in):
 *    prefix(in):
 */
int
pr_midxkey_remove_prefix (DB_VALUE * key, int prefix)
{
  int i, j, k;
  DB_MIDXKEY *midx_key;
  int start, offset, size;

  midx_key = DB_PULL_MIDXKEY (key);

  start = pr_midxkey_get_element_offset (midx_key, 0);
  offset = pr_midxkey_get_element_offset (midx_key, prefix);

  memmove (midx_key->buf + start, midx_key->buf + offset,
	   midx_key->size - offset + start);

  for (j = 0; j < prefix; j++)
    {
      OR_MULTI_CLEAR_BOUND_BIT (midx_key->buf, j);
    }

  midx_key->size = midx_key->size - offset + start;

  return NO_ERROR;
}


/*
 * pr_midxkey_common_prefix - 
 *
 *    return: 
 *    key1(in):
 *    key2(in):
 */
int
pr_midxkey_common_prefix (DB_VALUE * key1, DB_VALUE * key2)
{
  int size1, size2, diff_column, ret;
  bool dom_is_desc = false, next_dom_is_desc = false;
  DB_MIDXKEY *midx_lf_key, *midx_uf_key;

  assert (DB_VALUE_TYPE (key1) == DB_TYPE_MIDXKEY);
  assert (DB_VALUE_TYPE (key2) == DB_TYPE_MIDXKEY);

  diff_column = 0;		/* init */

  midx_lf_key = DB_PULL_MIDXKEY (key1);
  midx_uf_key = DB_PULL_MIDXKEY (key2);

  ret = pr_midxkey_compare (midx_lf_key, midx_uf_key,
			    0, 1, -1,
			    NULL, &size1, &size2,
			    &diff_column, &dom_is_desc, &next_dom_is_desc);

  if (ret == DB_UNK)
    {
      assert (false);
      diff_column = 0;
    }

  return diff_column;
}

/*
 * pr_midxkey_get_element_internal()
 *      return:
 *  midxkey(in) :
 *  index(in) :
 *  value(in) :
 *  copy(in) :
 *  prev_indexp(in) :
 *  prev_ptrp(in) :
 */

static int
pr_midxkey_get_element_internal (const DB_MIDXKEY * midxkey, int index,
				 DB_VALUE * value, bool copy,
				 int *prev_indexp, char **prev_ptrp)
{
  int idx_ncols = 0, i;
  int advance_size;
  int error = NO_ERROR;

  TP_DOMAIN *domain;

  OR_BUF buf_space;
  OR_BUF *buf;
  char *bitptr;

  idx_ncols = midxkey->domain->precision;
  if (idx_ncols <= 0)
    {
      assert (false);
      goto exit_on_error;
    }

  if (index >= midxkey->ncolumns)
    {
      assert (false);
      goto exit_on_error;
    }

#if !defined (NDEBUG)
  {
    int dom_ncols = 0;

    for (domain = midxkey->domain->setdomain; domain; domain = domain->next)
      {
	dom_ncols++;
      }

    if (dom_ncols <= 0)
      {
	assert (false);
	goto exit_on_error;
      }
    assert (dom_ncols == idx_ncols);
  }
#endif /* NDEBUG */

  /* get bit-mask */
  bitptr = midxkey->buf;
  /* get domain list, attr number */
  domain = midxkey->domain->setdomain;	/* first element's domain */

  if (OR_MULTI_ATT_IS_UNBOUND (bitptr, index))
    {
      DB_MAKE_NULL (value);
    }
  else
    {
      buf = NULL;		/* init */
      i = 0;			/* init */

      /* 1st phase: check for prev info */
      if (prev_indexp && prev_ptrp)
	{
	  int j, offset;

	  j = *prev_indexp;
	  offset = CAST_BUFLEN (*prev_ptrp - midxkey->buf);
	  if (j <= 0 || j > index || offset <= 0)
	    {			/* invalid info */
	      /* nop */
	    }
	  else
	    {
	      buf = &buf_space;
	      or_init (buf, *prev_ptrp, midxkey->size - offset);

	      /* consume prev domain */
	      for (; i < j; i++)
		{
		  domain = domain->next;
		}
	    }
	}

      /* 2nd phase: need to set buf info */
      if (buf == NULL)
	{
	  buf = &buf_space;
	  or_init (buf, midxkey->buf, midxkey->size);

	  advance_size = OR_MULTI_BOUND_BIT_BYTES (idx_ncols);
	  if (or_advance (buf, advance_size) != NO_ERROR)
	    {
	      goto exit_on_error;
	    }
	}

      for (; i < index; i++, domain = domain->next)
	{
	  /* check for element is NULL */
	  if (OR_MULTI_ATT_IS_UNBOUND (bitptr, i))
	    {
	      continue;		/* skip and go ahead */
	    }

	  advance_size = pr_midxkey_element_disk_size (buf->ptr, domain);
	  or_advance (buf, advance_size);
	}

      error = (*(domain->type->index_readval)) (buf, value, domain, -1, copy,
						NULL, 0);
      if (error != NO_ERROR)
	{
	  goto exit_on_error;
	}

      /* save the next index info */
      if (prev_indexp && prev_ptrp)
	{
	  *prev_indexp = index + 1;
	  *prev_ptrp = buf->ptr;
	}
    }

exit_on_end:

  return error;

exit_on_error:

  if (error == NO_ERROR)
    {
      error = -1;		/* set error */
    }

  goto exit_on_end;
}

/*
 * pr_midxkey_unique_prefix () -
 *      return: NO_ERROR or error code.
 *
 *  db_midxkey1(in) : Left side of compare.
 *  db_midxkey2(in) : Right side of compare.
 *  db_result(out) : midxkey such that > midxkey1, and <= midxkey2.
 *                                  or < midxkey1, and >= midxkey2 (desc)
 *
 * Note:
 *
 */
int
pr_midxkey_unique_prefix (const DB_VALUE * db_midxkey1,
			  const DB_VALUE * db_midxkey2, DB_VALUE * db_result)
{
  int c = DB_UNK;
  int i;
  int size1, size2, diff_column;
  int result_size = 0;
  char *result_buf;
  DB_MIDXKEY *midxkey1, *midxkey2;
  DB_MIDXKEY result_midxkey;
  bool dom_is_desc = false, next_dom_is_desc = false;

  /* Assertions */
  assert (db_midxkey1 != (DB_VALUE *) NULL);
  assert (db_midxkey2 != (DB_VALUE *) NULL);
  assert (db_result != (DB_VALUE *) NULL);

  midxkey1 = DB_PULL_MIDXKEY (db_midxkey1);
  midxkey2 = DB_PULL_MIDXKEY (db_midxkey2);

  assert (midxkey1->size != -1);
  assert (midxkey2->size != -1);
  assert (midxkey1->ncolumns == midxkey2->ncolumns);
  assert (midxkey1->domain == midxkey2->domain);
  assert (midxkey1->domain->setdomain == midxkey2->domain->setdomain);

  c = pr_midxkey_compare (midxkey1, midxkey2,
			  0, 1, -1, NULL, &size1, &size2,
			  &diff_column, &dom_is_desc, &next_dom_is_desc);
  if (dom_is_desc)
    {
      c = ((c == DB_GT) ? DB_LT : (c == DB_LT) ? DB_GT : c);
    }

  assert (c == DB_LT);
  if (c != DB_LT)
    {
      return (er_errid () == NO_ERROR) ? ER_FAILED : er_errid ();
    }

  if (size1 == midxkey1->size || size2 == midxkey2->size
      || OR_MULTI_ATT_IS_UNBOUND (midxkey1->buf, diff_column + 1)
      || OR_MULTI_ATT_IS_UNBOUND (midxkey2->buf, diff_column + 1))
    {
      /* not found separator: give up */
      pr_clone_value (db_midxkey2, db_result);
    }
  else
    {
      assert (size1 < midxkey1->size);
      assert (size2 < midxkey2->size);

      if (!next_dom_is_desc)
	{
	  result_buf = midxkey2->buf;
	  result_size = size2;
	}
      else
	{
	  result_buf = midxkey1->buf;
	  result_size = size1;
	}

      result_midxkey.buf = db_private_alloc (NULL, result_size);
      if (result_midxkey.buf == NULL)
	{
	  /* will already be set by memory mgr */
	  assert (er_errid () != NO_ERROR);
	  return er_errid ();
	}

      (void) memcpy (result_midxkey.buf, result_buf, result_size);
      result_midxkey.size = result_size;
      result_midxkey.domain = midxkey2->domain;
      result_midxkey.ncolumns = midxkey2->ncolumns;
      for (i = diff_column + 1; i < result_midxkey.ncolumns; i++)
	{
	  OR_MULTI_CLEAR_BOUND_BIT (result_midxkey.buf, i);
	}

      DB_MAKE_MIDXKEY (db_result, &result_midxkey);

      db_result->need_clear = true;

#if !defined(NDEBUG)
      /* midxkey1 < result_midxkey */
      c = pr_midxkey_compare (midxkey1, &result_midxkey,
			      0, 1, -1, NULL, &size1, &size2,
			      &diff_column, &dom_is_desc, &next_dom_is_desc);
      assert (c == DB_UNK || (DB_LT <= c && c <= DB_GT));
      if (dom_is_desc)
	{
	  c = ((c == DB_GT) ? DB_LT : (c == DB_LT) ? DB_GT : c);
	}
      assert (c == DB_LT);

      /* result_midxkey <= midxkey2 */
      c = pr_midxkey_compare (&result_midxkey, midxkey2,
			      0, 1, -1, NULL, &size1, &size2,
			      &diff_column, &dom_is_desc, &next_dom_is_desc);
      assert (c == DB_UNK || (DB_LT <= c && c <= DB_GT));
      if (dom_is_desc)
	{
	  c = ((c == DB_GT) ? DB_LT : (c == DB_LT) ? DB_GT : c);
	}

      assert (c == DB_LT || c == DB_EQ);
#endif
    }

  return NO_ERROR;
}

/*
 * pr_midxkey_get_element_nocopy() -
 *      return: error code
 *  midxkey(in) :
 *  index(in) :
 *  value(in) :
 *  prev_indexp(in) :
 *  prev_ptrp(in) :
 */

int
pr_midxkey_get_element_nocopy (const DB_MIDXKEY * midxkey, int index,
			       DB_VALUE * value,
			       int *prev_indexp, char **prev_ptrp)
{
  return pr_midxkey_get_element_internal (midxkey, index, value,
					  false /* not copy */ ,
					  prev_indexp, prev_ptrp);
}

/*
 * pr_midxkey_init_boundbits() -
 *      return: int
 *  bufptr(in) :
 *  n_atts(in) :
 *
 */

int
pr_midxkey_init_boundbits (char *bufptr, int n_atts)
{
  unsigned char *bits;
  int i, nbytes;

  nbytes = OR_MULTI_BOUND_BIT_BYTES (n_atts);
  bits = (unsigned char *) bufptr;

  for (i = 0; i < nbytes; i++)
    {
      bits[i] = (unsigned char) 0;
    }

  return nbytes;
}

/*
 * pr_midxkey_add_elements() -
 *      return:
 *  keyval(in) :
 *  dbvals(in) :
 *  num_dbvals(in) :
 *  dbvals_domain_list(in) :
 *  domain(in) :
 */

int
pr_midxkey_add_elements (DB_VALUE * keyval, DB_VALUE * dbvals,
			 int num_dbvals, TP_DOMAIN * dbvals_domain_list)
{
  int i;
  TP_DOMAIN *dom;
  DB_MIDXKEY *midxkey;
  int total_size = 0;
  int bitmap_size = 0;
  char *new_IDXbuf;
  char *bound_bits;
  OR_BUF buf;

  /* phase 1: find old */
  midxkey = DB_GET_MIDXKEY (keyval);
  if (midxkey == NULL)
    {
      return ER_FAILED;
    }

  if (midxkey->ncolumns > 0 && midxkey->size > 0)
    {
      /* bitmap is always fully sized */
      bitmap_size = OR_MULTI_BOUND_BIT_BYTES (midxkey->ncolumns);
      total_size = midxkey->size;
    }
  else
    {
      bitmap_size = OR_MULTI_BOUND_BIT_BYTES (num_dbvals);
      total_size = bitmap_size;
    }

  /* phase 2: calculate how many bytes need */
  total_size = pr_midxkey_get_vals_size (dbvals_domain_list, dbvals,
					 total_size);

  /* phase 3: initialize new_IDXbuf */
  new_IDXbuf = db_private_alloc (NULL, total_size);
  if (new_IDXbuf == NULL)
    {
      goto error;
    }

  or_init (&buf, new_IDXbuf, -1);
  bound_bits = buf.ptr;

  /* phase 4: copy new_IDXbuf from old */
  if (midxkey->ncolumns > 0 && midxkey->size > 0)
    {
      or_put_data (&buf, midxkey->buf, midxkey->size);
    }
  else
    {
      /* bound bits */
      (void) pr_midxkey_init_boundbits (bound_bits, bitmap_size);
      or_advance (&buf, bitmap_size);
    }

  for (i = 0, dom = dbvals_domain_list; i < num_dbvals; i++, dom = dom->next)
    {
      /* check for added val is NULL */
      if (DB_IS_NULL (&dbvals[i]))
	{
	  continue;		/* skip and go ahead */
	}

      (*((dom->type)->index_writeval)) (&buf, &dbvals[i]);

      OR_ENABLE_BOUND_BIT (bound_bits, midxkey->ncolumns + i);
    }				/* for (i = 0, ...) */

  assert (total_size == CAST_BUFLEN (buf.ptr - buf.buffer));

  /* phase 5: make new multiIDX */
  if (midxkey->size > 0)
    {
      db_private_free_and_init (NULL, midxkey->buf);
      midxkey->buf = NULL;
    }

  midxkey->buf = buf.buffer;
  midxkey->size = CAST_BUFLEN (buf.ptr - buf.buffer);
  midxkey->ncolumns += num_dbvals;

  return NO_ERROR;

error:

  if (midxkey->buf)
    {
      db_private_free_and_init (NULL, midxkey->buf);
      midxkey->buf = NULL;
    }
  return ER_FAILED;
}

/*
 * pr_data_writeval_disk_size - returns the number of bytes that will be
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
pr_data_writeval_disk_size (DB_VALUE * value)
{
  PR_TYPE *type;
  DB_TYPE dbval_type;

  dbval_type = DB_VALUE_DOMAIN_TYPE (value);
  type = PR_TYPE_FROM_ID (dbval_type);

  assert (type != NULL);

  if (type)
    {
      if (type->data_lengthval == NULL)
	{
	  return type->disksize;
	}
      else
	{
	  return (*(type->data_lengthval)) (value, 1);
	}
    }

  return 0;
}

/*
 * pr_index_writeval_disk_size - returns the number of bytes that will be
 * written by the "index_write" type function for this value.
 *    return: byte size of disk representation
 *    value(in): db value
 * Note:
 */
int
pr_index_writeval_disk_size (DB_VALUE * value)
{
  PR_TYPE *type;
  DB_TYPE dbval_type;

  dbval_type = DB_VALUE_DOMAIN_TYPE (value);
  type = PR_TYPE_FROM_ID (dbval_type);

  assert (type != NULL);

  if (type)
    {
      if (type->index_lengthval == NULL)
	{
	  assert (!type->variable_p);

	  return type->disksize;
	}
      else
	{
	  return (*(type->index_lengthval)) (value);
	}
    }

  return 0;
}

void
pr_data_writeval (OR_BUF * buf, DB_VALUE * value)
{
  PR_TYPE *type;
  DB_TYPE dbval_type;

  dbval_type = DB_VALUE_DOMAIN_TYPE (value);
  type = PR_TYPE_FROM_ID (dbval_type);
  if (type == NULL)
    {
      type = tp_Type_null;	/* handle strange arguments with NULL */
    }
  (*(type->data_writeval)) (buf, value);
}

#if defined(ENABLE_UNUSED_FUNCTION)
/*
 * GARBAGE COLLECTION SUPPORT
 */
#if !defined (SERVER_MODE)

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
#endif /* ENABLE_UNUSED_FUNCTION */

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
      /* space with terminating NULL */
      str = (char *) malloc (sizeof (null_str) + 1);
      if (str)
	{
	  strcpy (str, null_str);
	}
      return str;
    }

  if (DB_IS_NULL (val))
    {
      /* space with terminating NULL */
      str = (char *) malloc (sizeof (NULL_str) + 1);
      if (str)
	{
	  strcpy (str, NULL_str);
	}
      return str;
    }

  dbval_type = DB_VALUE_DOMAIN_TYPE (val);
  pr_type = PR_TYPE_FROM_ID (dbval_type);

  if (pr_type == NULL)
    {
      return NULL;
    }

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
      str_size++;		/* for terminating NULL */
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

      str[str_size - 1] = 0;	/* set terminating NULL */
    }

  return str;
}

/*
 * pr_complete_enum_value - Sets both index and string of a enum value in case
 *    one of them is missing.
 *    return: NO_ERROR or error code.
 *    value(in/out): enumeration value.
 *    domain(in): enumeration domain against which the value is checked. 
 */
int
pr_complete_enum_value (DB_VALUE * value, TP_DOMAIN * domain)
{
  unsigned short short_val;
  char *str_val;
  int enum_count, str_val_size, idx;
  DB_ENUM_ELEMENT *db_elem = 0;

  if (value == NULL || domain == NULL || DB_IS_NULL (value))
    {
      return NO_ERROR;
    }

  if (value->domain.general_info.type != DB_TYPE_ENUMERATION
      || domain->type->id != DB_TYPE_ENUMERATION)
    {
      return ER_FAILED;
    }

  short_val = DB_GET_ENUM_SHORT (value);
  str_val = DB_GET_ENUM_STRING (value);
  str_val_size = DB_GET_ENUM_STRING_SIZE (value);
  enum_count = DOM_GET_ENUM_ELEMS_COUNT (domain);
  if (short_val > enum_count)
    {
      return ER_FAILED;
    }

  if (short_val > 0)
    {
      db_elem = &DOM_GET_ENUM_ELEM (domain, short_val);
      if (str_val != NULL &&
	  DB_GET_ENUM_ELEM_STRING_SIZE (db_elem) == str_val_size
	  && !memcmp (str_val, DB_GET_ENUM_ELEM_STRING (db_elem),
		      str_val_size))
	{
	  DB_MAKE_ENUMERATION (value, short_val, str_val, str_val_size,
			       DB_GET_ENUM_ELEM_CODESET (db_elem),
			       DB_GET_ENUM_COLLATION (value));
	  return NO_ERROR;
	}
      pr_clear_value (value);

      str_val_size = DB_GET_ENUM_ELEM_STRING_SIZE (db_elem);
      str_val = (char *) db_private_alloc (NULL, str_val_size + 1);
      if (str_val == NULL)
	{
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}
      memcpy (str_val, DB_GET_ENUM_ELEM_STRING (db_elem), str_val_size);
      str_val[str_val_size] = 0;
      DB_MAKE_ENUMERATION (value, short_val, str_val, str_val_size,
			   DB_GET_ENUM_ELEM_CODESET (db_elem),
			   DB_GET_ENUM_COLLATION (value));
      value->need_clear = true;

      return NO_ERROR;
    }
  else if (str_val == NULL)
    {
      /* empty string enum value with index 0 */
      return NO_ERROR;
    }

  for (idx = 0; idx < enum_count; idx++)
    {
      db_elem = &DOM_GET_ENUM_ELEM (domain, idx + 1);
      if (DB_GET_ENUM_ELEM_STRING_SIZE (db_elem) == str_val_size &&
	  !memcmp (DB_GET_ENUM_ELEM_STRING (db_elem), str_val, str_val_size))
	{
	  DB_MAKE_ENUMERATION (value, DB_GET_ENUM_ELEM_SHORT (db_elem),
			       str_val, str_val_size,
			       DB_GET_ENUM_ELEM_CODESET (db_elem),
			       DB_GET_ENUM_COLLATION (value));
	  break;
	}
    }

  return NO_ERROR;
}

/*
 * TYPE RESULTSET
 */

static void
mr_initmem_resultset (void *mem, TP_DOMAIN * domain)
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
      mr_initmem_resultset (mem, domain);
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
mr_data_writemem_resultset (OR_BUF * buf, void *mem, TP_DOMAIN * domain)
{
  or_put_int (buf, *(int *) mem);
}

static void
mr_data_readmem_resultset (OR_BUF * buf, void *mem, TP_DOMAIN * domain,
			   int size)
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
mr_setval_resultset (DB_VALUE * dest, const DB_VALUE * src, bool copy)
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
mr_data_writeval_resultset (OR_BUF * buf, DB_VALUE * value)
{
  return or_put_int (buf, db_get_resultset (value));
}

static int
mr_data_readval_resultset (OR_BUF * buf, DB_VALUE * value,
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
mr_data_cmpdisk_resultset (void *mem1, void *mem2, TP_DOMAIN * domain,
			   int do_coercion, int total_order, int *start_colp)
{
  int i1, i2;

  assert (domain != NULL);

  /* is not index type */
  assert (!domain->is_desc && !tp_valid_indextype (TP_DOMAIN_TYPE (domain)));

  i1 = OR_GET_INT (mem1);
  i2 = OR_GET_INT (mem2);

  return MR_CMP (i1, i2);
}

static int
mr_cmpval_resultset (DB_VALUE * value1, DB_VALUE * value2,
		     int do_coercion, int total_order, int *start_colp,
		     int collation)
{
  int i1, i2;

  i1 = DB_GET_RESULTSET (value1);
  i2 = DB_GET_RESULTSET (value2);

  return MR_CMP (i1, i2);
}


/*
 * TYPE STRING
 */

static void
mr_initmem_string (void *mem, TP_DOMAIN * domain)
{
  *(char **) mem = NULL;
}


/*
 * The main difference between "memory" strings and "value" strings is that
 * the length tag is stored as an in-line prefix in the memory block allocated
 * to hold the string characters.
 */
static int
mr_setmem_string (void *memptr, TP_DOMAIN * domain, DB_VALUE * value)
{
  int error = NO_ERROR;
  char *src, *cur, *new_, **mem;
  int src_precision, src_length, new_length;

  /* get the current memory contents */
  mem = (char **) memptr;
  cur = *mem;

  if (value == NULL || (src = DB_GET_STRING (value)) == NULL)
    {
      /* remove the current value */
      if (cur != NULL)
	{
	  db_private_free_and_init (NULL, cur);
	  mr_initmem_string (memptr, domain);
	}
    }
  else
    {
      /*
       * Get information from the value.  Ignore precision for the time being
       * since we really only care about the byte size of the value for varchar.
       * Whether or not the value "fits" should have been checked by now.
       */
      src_precision = DB_GET_STRING_PRECISION (value);
      src_length = DB_GET_STRING_SIZE (value);	/* size in bytes */

      if (src_length < 0)
	{
	  src_length = strlen (src);
	}

      /* Currently we NULL terminate the workspace string.
       * Could try to do the single byte size hack like we have in the
       * disk representation.
       */
      new_length = src_length + sizeof (int) + 1;
      new_ = (char *) db_private_alloc (NULL, new_length);
      if (new_ == NULL)
	{
	  assert (er_errid () != NO_ERROR);
	  error = er_errid ();
	}
      else
	{
	  if (cur != NULL)
	    {
	      db_private_free_and_init (NULL, cur);
	    }

	  /* pack in the length prefix */
	  *(int *) new_ = src_length;
	  cur = new_ + sizeof (int);
	  /* store the string */
	  memcpy (cur, src, src_length);
	  /* NULL terminate the stored string for safety */
	  cur[src_length] = '\0';
	  *mem = new_;
	}
    }

  return error;
}

static int
mr_getmem_string (void *memptr, TP_DOMAIN * domain, DB_VALUE * value,
		  bool copy)
{
  int error = NO_ERROR;
  int mem_length;
  char **mem, *cur, *new_;

  /* get to the current value */
  mem = (char **) memptr;
  cur = *mem;

  if (cur == NULL)
    {
      db_value_domain_init (value, DB_TYPE_VARCHAR, domain->precision, 0);
      value->need_clear = false;
    }
  else
    {
      /* extract the length prefix and the pointer to the actual string data */
      mem_length = *(int *) cur;
      cur += sizeof (int);

      if (TP_DOMAIN_COLLATION_FLAG (domain) != TP_DOMAIN_COLL_NORMAL)
	{
	  assert (false);
	  return ER_FAILED;
	}

      if (!copy)
	{
	  db_make_varchar (value, domain->precision, cur, mem_length,
			   TP_DOMAIN_CODESET (domain),
			   TP_DOMAIN_COLLATION (domain));
	  value->need_clear = false;
	}
      else
	{
	  /* return it with a NULL terminator */
	  new_ = (char *) db_private_alloc (NULL, mem_length + 1);
	  if (new_ == NULL)
	    {
	      assert (er_errid () != NO_ERROR);
	      error = er_errid ();
	    }
	  else
	    {
	      memcpy (new_, cur, mem_length);
	      new_[mem_length] = '\0';
	      db_make_varchar (value, domain->precision, new_, mem_length,
			       TP_DOMAIN_CODESET (domain),
			       TP_DOMAIN_COLLATION (domain));
	      value->need_clear = true;
	    }
	}
    }
  return error;
}


/*
 * For the disk representation, we may be adding pad bytes to round up to a
 * word boundary.
 *
 * We are currently adding a NULL terminator to the disk representation
 * for some code on the server that still manufactures pointers directly into
 * the disk buffer and assumes it is a NULL terminated string.  This terminator
 * can be removed after the server has been updated.  The logic for maintaining
 * the terminator is actuall in the or_put_varchar, family of functions.
 */
static int
mr_data_lengthmem_string (void *memptr, TP_DOMAIN * domain, int disk)
{
  char **mem, *cur;
  int len;

  len = 0;
  if (!disk)
    {
      len = tp_String.size;
    }
  else if (memptr != NULL)
    {
      mem = (char **) memptr;
      cur = *mem;
      if (cur != NULL)
	{
	  len = *(int *) cur;
	  len = or_packed_varchar_length (len);
	}
    }

  return len;
}

static int
mr_index_lengthmem_string (void *memptr, TP_DOMAIN * domain)
{
  OR_BUF buf;
  int charlen;
  int rc = NO_ERROR;

  /* generally, index key-value is short enough
   */
  charlen = OR_GET_BYTE (memptr);
  if (charlen < 0xFF)
    {
      return or_varchar_length (charlen);
    }

  assert (charlen == 0xFF);

  or_init (&buf, memptr, -1);

  charlen = or_get_varchar_length (&buf, &rc);

  return or_varchar_length (charlen);
}

static void
mr_data_writemem_string (OR_BUF * buf, void *memptr, TP_DOMAIN * domain)
{
  char **mem, *cur;
  int len;

  mem = (char **) memptr;
  cur = *mem;
  if (cur != NULL)
    {
      len = *(int *) cur;
      cur += sizeof (int);
      or_packed_put_varchar (buf, cur, len);
    }
}


/*
 * The amount of memory requested is currently calculated based on the
 * stored size prefix.  If we ever go to a system where we avoid storing the
 * size, then we could use the size argument passed in to this function but
 * that may also include any padding byte that added to bring us up to a word
 * boundary.
 * Might want some way to determine which bytes at the end of a string are
 * padding.
 */
static void
mr_data_readmem_string (OR_BUF * buf, void *memptr, TP_DOMAIN * domain,
			int size)
{
  char **mem, *cur, *new_;
  int len;
  int mem_length, pad;
  char *start;
  int rc = NO_ERROR;

  /*
   * we must have an explicit size here as it can't be determined from the
   * domain
   */
  if (size < 0)
    return;

  if (memptr == NULL)
    {
      if (size)
	or_advance (buf, size);
    }
  else
    {
      mem = (char **) memptr;
      cur = *mem;
      /* should we be checking for existing strings ? */
#if 0
      if (cur != NULL)
	db_private_free_and_init (NULL, cur);
#endif

      new_ = NULL;
      if (size)
	{
	  start = buf->ptr;

	  /* KLUDGE, we have some knowledge of how the thing is stored here in
	   * order have some control over the conversion between the packed
	   * length prefix and the full word memory length prefix.
	   * Might want to put this in another specialized or_ function.
	   */

	  /* Get just the length prefix. */
	  len = or_get_varchar_length (buf, &rc);

	  /*
	   * Allocate storage for this string, including our own full word size
	   * prefix and a NULL terminator.
	   */
	  mem_length = len + sizeof (int) + 1;

	  new_ = db_private_alloc (NULL, mem_length);
	  if (new_ == NULL)
	    or_abort (buf);
	  else
	    {
	      /* store the length in our memory prefix */
	      *(int *) new_ = len;
	      cur = new_ + sizeof (int);

	      /*
	       * read the string, INLCUDING the NULL terminator (which is
	       * expected)
	       */
	      or_get_data (buf, cur, len + 1);
	      /* align like or_get_varchar */
	      or_get_align32 (buf);
	    }

	  /* If we were given a size, check to see if for some reason this is
	   * larger than the already word aligned string that we have now
	   * extracted.  This shouldn't be the case but since we've got a
	   * length, we may as well obey it.
	   */
	  pad = size - (int) (buf->ptr - start);
	  if (pad > 0)
	    or_advance (buf, pad);
	}
      *mem = new_;
    }
}

static void
mr_freemem_string (void *memptr)
{
  char *cur;

  if (memptr != NULL)
    {
      cur = *(char **) memptr;
      if (cur != NULL)
	db_private_free_and_init (NULL, cur);
    }
}

static void
mr_initval_string (DB_VALUE * value, int precision, int scale)
{
  db_make_varchar (value, precision, NULL, 0, LANG_SYS_CODESET,
		   LANG_SYS_COLLATION);
  value->need_clear = false;
}

static int
mr_setval_string (DB_VALUE * dest, const DB_VALUE * src, bool copy)
{
  int error = NO_ERROR;
  int src_precision, src_length;
  char *src_str, *new_;

  if (src == NULL || (DB_IS_NULL (src) && db_value_precision (src) == 0))
    {
      error =
	db_value_domain_init (dest, DB_TYPE_VARCHAR, DB_DEFAULT_PRECISION, 0);
    }
  else if (DB_IS_NULL (src) || (src_str = db_get_string (src)) == NULL)
    {
      error =
	db_value_domain_init (dest, DB_TYPE_VARCHAR, db_value_precision (src),
			      0);
    }
  else
    {
      /* Get information from the value. */
      src_precision = db_value_precision (src);
      src_length = db_get_string_size (src);
      if (src_length < 0)
	src_length = strlen (src_str);

      /* should we be paying attention to this? it is extremely dangerous */
      if (!copy)
	{
	  error = db_make_varchar (dest, src_precision, src_str, src_length,
				   DB_GET_STRING_CODESET (src),
				   DB_GET_STRING_COLLATION (src));
	}
      else
	{
	  new_ = db_private_alloc (NULL, src_length + 1);
	  if (new_ == NULL)
	    {
	      db_value_domain_init (dest, DB_TYPE_VARCHAR, src_precision, 0);
	      assert (er_errid () != NO_ERROR);
	      error = er_errid ();
	    }
	  else
	    {
	      memcpy (new_, src_str, src_length);
	      new_[src_length] = '\0';
	      db_make_varchar (dest, src_precision, new_, src_length,
			       DB_GET_STRING_CODESET (src),
			       DB_GET_STRING_COLLATION (src));
	      dest->need_clear = true;
	    }
	}
    }

  return error;
}

static int
mr_index_lengthval_string (DB_VALUE * value)
{
  return mr_lengthval_string_internal (value, 1, CHAR_ALIGNMENT);
}

static int
mr_index_writeval_string (OR_BUF * buf, DB_VALUE * value)
{
  return mr_writeval_string_internal (buf, value, CHAR_ALIGNMENT);
}

static int
mr_index_readval_string (OR_BUF * buf, DB_VALUE * value,
			 TP_DOMAIN * domain, int size, bool copy,
			 char *copy_buf, int copy_buf_len)
{
  return mr_readval_string_internal (buf, value, domain, size, copy, copy_buf,
				     copy_buf_len, CHAR_ALIGNMENT);
}

static int
mr_data_lengthval_string (DB_VALUE * value, int disk)
{
  return mr_lengthval_string_internal (value, disk, INT_ALIGNMENT);
}

static int
mr_data_writeval_string (OR_BUF * buf, DB_VALUE * value)
{
  return mr_writeval_string_internal (buf, value, INT_ALIGNMENT);
}

static int
mr_data_readval_string (OR_BUF * buf, DB_VALUE * value,
			TP_DOMAIN * domain, int size, bool copy,
			char *copy_buf, int copy_buf_len)
{
  return mr_readval_string_internal (buf, value, domain, size, copy, copy_buf,
				     copy_buf_len, INT_ALIGNMENT);
}

/*
 * Ignoring precision as byte size is really the only important thing for
 * varchar.
 */
static int
mr_lengthval_string_internal (DB_VALUE * value, int disk, int align)
{
  int len;
  const char *str;

  if (DB_IS_NULL (value))
    {
      return 0;
    }
  str = value->data.ch.medium.buf;
  len = value->data.ch.medium.size;
  if (!str)
    {
      return 0;
    }
  if (len < 0)
    {
      len = strlen (str);
    }

  if (disk == 0)
    {
      return len;
    }
  else if (align == INT_ALIGNMENT)
    {
      return or_packed_varchar_length (len);
    }
  else
    {
      return or_varchar_length (len);
    }
}


/*
 * Ignoring precision as byte size is really the only important thing for
 * varchar.
 */
static int
mr_writeval_string_internal (OR_BUF * buf, DB_VALUE * value, int align)
{
  int src_length;
  char *str;
  int rc = NO_ERROR;

  if (value != NULL && (str = db_get_string (value)) != NULL)
    {
      src_length = db_get_string_size (value);	/* size in bytes */
      if (src_length < 0)
	{
	  src_length = strlen (str);
	}

      if (align == INT_ALIGNMENT)
	{
	  rc = or_packed_put_varchar (buf, str, src_length);
	}
      else
	{
	  rc = or_put_varchar (buf, str, src_length);
	}
    }
  return rc;
}


static int
mr_readval_string_internal (OR_BUF * buf, DB_VALUE * value,
			    TP_DOMAIN * domain, int size, bool copy,
			    char *copy_buf, int copy_buf_len, int align)
{
  int pad, precision;
  char *new_, *start = NULL;
  int str_length;
  int rc = NO_ERROR;

  if (value == NULL)
    {
      if (size == -1)
	{
	  rc = or_skip_varchar (buf, align);
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
      if (domain != NULL)
	{
	  precision = domain->precision;
	}
      else
	{
	  precision = DB_MAX_VARCHAR_PRECISION;
	}

      if (!copy)
	{
	  str_length = or_get_varchar_length (buf, &rc);
	  if (rc == NO_ERROR)
	    {
	      if (TP_DOMAIN_COLLATION_FLAG (domain) != TP_DOMAIN_COLL_NORMAL)
		{
		  assert (false);
		  return ER_FAILED;
		}
	      db_make_varchar (value, precision, buf->ptr, str_length,
			       TP_DOMAIN_CODESET (domain),
			       TP_DOMAIN_COLLATION (domain));
	      value->need_clear = false;
	      rc = or_skip_varchar_remainder (buf, str_length, align);
	    }
	}
      else
	{
	  if (size == 0)
	    {
	      /* its NULL */
	      db_value_domain_init (value, DB_TYPE_VARCHAR, precision, 0);
	    }
	  else
	    {			/* size != 0 */
	      if (size == -1)
		{
		  /* Standard packed varchar with a size prefix */
		  ;		/* do nothing */
		}
	      else
		{		/* size != -1 */
		  /* Standard packed varchar within an area of fixed size,
		   * usually this means we're looking at the disk
		   * representation of an attribute.
		   * Just like the -1 case except we advance past the additional
		   * padding.
		   */
		  start = buf->ptr;
		}		/* size != -1 */

	      str_length = or_get_varchar_length (buf, &rc);
	      if (rc != NO_ERROR)
		{
		  return ER_FAILED;
		}

	      if (copy_buf && copy_buf_len >= str_length + 1)
		{
		  /* read buf image into the copy_buf */
		  new_ = copy_buf;
		}
	      else
		{
		  /*
		   * Allocate storage for the string including the kludge
		   * NULL terminator
		   */
		  new_ = db_private_alloc (NULL, str_length + 1);
		}

	      if (new_ == NULL)
		{
		  /* need to be able to return errors ! */
		  if (domain)
		    {
		      db_value_domain_init (value, TP_DOMAIN_TYPE (domain),
					    TP_FLOATING_PRECISION_VALUE, 0);
		    }
		  or_abort (buf);
		  return ER_FAILED;
		}
	      else
		{
		  if (align == INT_ALIGNMENT)
		    {
		      /* read the kludge NULL terminator */
		      rc = or_get_data (buf, new_, str_length + 1);

		      /* round up to a word boundary */
		      if (rc == NO_ERROR)
			{
			  rc = or_get_align32 (buf);
			}
		    }
		  else
		    {
		      rc = or_get_data (buf, new_, str_length);
		    }

		  if (rc != NO_ERROR)
		    {
		      if (new_ != copy_buf)
			{
			  db_private_free_and_init (NULL, new_);
			}
		      return ER_FAILED;
		    }

		  new_[str_length] = '\0';	/* append the kludge NULL terminator */
		  if (TP_DOMAIN_COLLATION_FLAG (domain)
		      != TP_DOMAIN_COLL_NORMAL)
		    {
		      assert (false);
		      return ER_FAILED;
		    }
		  db_make_varchar (value, precision, new_, str_length,
				   TP_DOMAIN_CODESET (domain),
				   TP_DOMAIN_COLLATION (domain));
		  value->need_clear = (new_ != copy_buf) ? true : false;

		  if (size == -1)
		    {
		      /* Standard packed varchar with a size prefix */
		      ;		/* do nothing */
		    }
		  else
		    {		/* size != -1 */
		      /* Standard packed varchar within an area of fixed size,
		       * usually this means we're looking at the disk
		       * representation of an attribute. Just like the -1 case
		       * except we advance past the additional padding.
		       */
		      pad = size - (int) (buf->ptr - start);
		      if (pad > 0)
			{
			  rc = or_advance (buf, pad);
			}
		    }		/* size != -1 */
		}		/* else */
	    }			/* size != 0 */
	}
    }
  return rc;
}

static int
mr_index_cmpdisk_string (void *mem1, void *mem2, TP_DOMAIN * domain,
			 int do_coercion, int total_order, int *start_colp)
{
  assert (domain != NULL);

  return mr_data_cmpdisk_string (mem1, mem2, domain, do_coercion,
				 total_order, start_colp);
}

static int
mr_data_cmpdisk_string (void *mem1, void *mem2, TP_DOMAIN * domain,
			int do_coercion, int total_order, int *start_colp)
{
  int c = DB_UNK;
  char *str1, *str2;
  int str_length1, str_length2;
  OR_BUF buf1, buf2;
  int rc = NO_ERROR;

  assert (domain != NULL);

  str1 = (char *) mem1;
  str2 = (char *) mem2;

  /* generally, data is short enough
   */
  str_length1 = OR_GET_BYTE (str1);
  str_length2 = OR_GET_BYTE (str2);
  if (str_length1 < 0xFF && str_length2 < 0xFF)
    {
      str1 += OR_BYTE_SIZE;
      str2 += OR_BYTE_SIZE;
      c = QSTR_COMPARE (domain->collation_id,
			(unsigned char *) str1, str_length1,
			(unsigned char *) str2, str_length2);
      c = MR_CMP_RETURN_CODE (c);
      return c;
    }

  assert (str_length1 == 0xFF || str_length2 == 0xFF);

  or_init (&buf1, str1, 0);
  str_length1 = or_get_varchar_length (&buf1, &rc);
  if (rc == NO_ERROR)
    {
      or_init (&buf2, str2, 0);
      str_length2 = or_get_varchar_length (&buf2, &rc);
      if (rc == NO_ERROR)
	{
	  c = QSTR_COMPARE (domain->collation_id,
			    (unsigned char *) buf1.ptr, str_length1,
			    (unsigned char *) buf2.ptr, str_length2);
	  c = MR_CMP_RETURN_CODE (c);
	  return c;
	}
    }

  return DB_UNK;
}

static int
mr_cmpval_string (DB_VALUE * value1, DB_VALUE * value2,
		  int do_coercion, int total_order, int *start_colp,
		  int collation)
{
  int c;
  unsigned char *string1, *string2;
  int size1, size2;

  string1 = (unsigned char *) DB_GET_STRING (value1);
  string2 = (unsigned char *) DB_GET_STRING (value2);

  if (string1 == NULL || string2 == NULL
      || DB_GET_STRING_CODESET (value1) != DB_GET_STRING_CODESET (value1))
    {
      return DB_UNK;
    }

  size1 = (int) DB_GET_STRING_SIZE (value1);
  size2 = (int) DB_GET_STRING_SIZE (value2);

  if (size1 < 0)
    {
      size1 = strlen ((char *) string1);
    }

  if (size2 < 0)
    {
      size2 = strlen ((char *) string2);
    }

  if (collation == -1)
    {
      assert (false);
      return DB_UNK;
    }

  c = QSTR_COMPARE (collation, string1, size1, string2, size2);
  c = MR_CMP_RETURN_CODE (c);

  return c;
}

#if defined (ENABLE_UNUSED_FUNCTION)
static int
mr_cmpval_string2 (DB_VALUE * value1, DB_VALUE * value2, int length,
		   int do_coercion, int total_order, int *start_colp)
{
  int c;
  unsigned char *string1, *string2;
  int len1, len2, string_size;

  string1 = (unsigned char *) DB_GET_STRING (value1);
  string2 = (unsigned char *) DB_GET_STRING (value2);

  if (string1 == NULL || string2 == NULL)
    {
      return DB_UNK;
    }

  string_size = (int) DB_GET_STRING_SIZE (value1);
  len1 = MIN (string_size, length);
  string_size = (int) DB_GET_STRING_SIZE (value2);
  len2 = MIN (string_size, length);

  c = QSTR_COMPARE (string1, len1, string2, len2);
  c = MR_CMP_RETURN_CODE (c);

  return c;
}
#endif

PR_TYPE tp_String = {
  "character varying", DB_TYPE_STRING, 1, sizeof (const char *), 0, 1,
  help_fprint_value,
  help_sprint_value,
  mr_initmem_string,
  mr_initval_string,
  mr_setmem_string,
  mr_getmem_string,
  mr_setval_string,
  mr_data_lengthmem_string,
  mr_data_lengthval_string,
  mr_data_writemem_string,
  mr_data_readmem_string,
  mr_data_writeval_string,
  mr_data_readval_string,
  mr_index_lengthmem_string,
  mr_index_lengthval_string,
  mr_index_writeval_string,
  mr_index_readval_string,
  mr_index_cmpdisk_string,
  mr_freemem_string,
  mr_data_cmpdisk_string,
  mr_cmpval_string
};

PR_TYPE *tp_Type_string = &tp_String;

/*
 * TYPE CHAR
 */

static void
mr_initmem_char (void *memptr, TP_DOMAIN * domain)
{
#if !defined(NDEBUG)
  int mem_length;

  assert (!IS_FLOATING_PRECISION (domain->precision));

  mem_length = STR_SIZE (domain->precision, TP_DOMAIN_CODESET (domain));
  memset (memptr, 0, mem_length);
#endif
}


/*
 * Note! Due to the "within tolerance" comparison of domains used for
 * assignment validation, we may get values in here whose precision is
 * less than the precision of the actual attribute.   This prevents
 * having to copy a string just to coerce a value into a larger precision.
 * Note that the precision of the source value may also come in as -1 here
 * which is used to mean a "floating" precision that is assumed to be
 * compatible with the destination domain as long as the associated value
 * is within tolerance.  This case is generally only seen for string
 * literals that have been produced by the parser.  These literals will
 * not contain blank padding and strlen() or db_get_string_size() can be
 * used to determine the number of significant characters.
 *
 */
static int
mr_setmem_char (void *memptr, TP_DOMAIN * domain, DB_VALUE * value)
{
  int error = NO_ERROR;
  char *src, *mem;
  int src_precision, src_length, mem_length, pad;

  assert (!IS_FLOATING_PRECISION (domain->precision));

  if (value == NULL)
    {
      return NO_ERROR;
    }

  /* Get information from the value */
  src = DB_GET_STRING (value);
  src_precision = DB_GET_STRING_PRECISION (value);
  src_length = DB_GET_STRING_SIZE (value);	/* size in bytes */

  if (src == NULL)
    {
      return NO_ERROR;
    }

  /* Check for special NTS flag.  This may not be necessary any more. */
  if (src_length < 0)
    {
      src_length = strlen (src);
    }


  /* The only thing we really care about at this point, is the byte
   * length of the string.  The precision could be checked here but it
   * really isn't necessary for this operation.
   * Calculate the maximum number of bytes we have available here.
   * The multiplier is dependent on codeset
   */
  mem_length = STR_SIZE (domain->precision, TP_DOMAIN_CODESET (domain));

  if (mem_length < src_length)
    {
      /*
       * should never get here, this is supposed to be caught during domain
       * validation, need a better error message.
       */
      error = ER_OBJ_DOMAIN_CONFLICT;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, "");
    }
  else
    {
      /* copy the value into memory */
      mem = (char *) memptr;
      memcpy (mem, src, src_length);

      /*
       * Check for space padding, if this were a national string, we would
       * need to be padding with the appropriate space character !
       */
      pad = mem_length - src_length;
      if (pad)
	{
	  int i;
	  for (i = src_length; i < mem_length; i++)
	    mem[i] = ' ';
	}
    }
  return error;
}


static int
mr_getmem_char (void *mem, TP_DOMAIN * domain, DB_VALUE * value, bool copy)
{
  int mem_length;
  char *new_;

  assert (!IS_FLOATING_PRECISION (domain->precision));
  if (TP_DOMAIN_COLLATION_FLAG (domain) != TP_DOMAIN_COLL_NORMAL)
    {
      assert (false);
      return ER_FAILED;
    }

  intl_char_size ((unsigned char *) mem, domain->precision,
		  TP_DOMAIN_CODESET (domain), &mem_length);
  if (mem_length == 0)
    {
      mem_length = STR_SIZE (domain->precision, TP_DOMAIN_CODESET (domain));
    }

  if (!copy)
    {
      new_ = (char *) mem;
    }
  else
    {
      new_ = db_private_alloc (NULL, mem_length + 1);
      if (new_ == NULL)
	{
	  assert (er_errid () != NO_ERROR);
	  return er_errid ();
	}
      memcpy (new_, (char *) mem, mem_length);
      /* make sure that all outgoing strings are NULL terminated */
      new_[mem_length] = '\0';
    }

  db_make_char (value, domain->precision, new_, mem_length,
		TP_DOMAIN_CODESET (domain), TP_DOMAIN_COLLATION (domain));
  if (copy)
    {
      value->need_clear = true;
    }

  return NO_ERROR;
}

static int
mr_data_lengthmem_char (void *memptr, TP_DOMAIN * domain, int disk)
{
  int mem_length;

  assert (!IS_FLOATING_PRECISION (domain->precision));

  mem_length = STR_SIZE (domain->precision, TP_DOMAIN_CODESET (domain));

  return mem_length;
}

static int
mr_index_lengthmem_char (void *memptr, TP_DOMAIN * domain)
{
  int mem_length;

  assert (!(IS_FLOATING_PRECISION (domain->precision) && memptr == NULL));

  if (IS_FLOATING_PRECISION (domain->precision))
    {
      memcpy (&mem_length, memptr, OR_INT_SIZE);
      mem_length += OR_INT_SIZE;
    }
  else
    {
      mem_length = STR_SIZE (domain->precision, TP_DOMAIN_CODESET (domain));
    }

  return mem_length;
}

static void
mr_data_writemem_char (OR_BUF * buf, void *mem, TP_DOMAIN * domain)
{
  int mem_length;

  assert (!IS_FLOATING_PRECISION (domain->precision));

  mem_length = STR_SIZE (domain->precision, TP_DOMAIN_CODESET (domain));

  /*
   * We simply dump the memory image to disk, it will already have been padded.
   * If this were a national character string, at this point, we'd have to
   * decide now to perform a character set conversion.
   */
  or_put_data (buf, (char *) mem, mem_length);
}

static void
mr_data_readmem_char (OR_BUF * buf, void *mem, TP_DOMAIN * domain, int size)
{
  int mem_length, padding;

  assert (!IS_FLOATING_PRECISION (domain->precision));

  if (mem == NULL)
    {
      /*
       * If we passed in a size, then use it.  Otherwise, determine the
       * size from the domain.
       */
      if (size > 0)
	{
	  or_advance (buf, size);
	}
      else
	{
	  mem_length =
	    STR_SIZE (domain->precision, TP_DOMAIN_CODESET (domain));
	  or_advance (buf, mem_length);
	}
    }
  else
    {
      mem_length = STR_SIZE (domain->precision, TP_DOMAIN_CODESET (domain));

      if (size != -1 && mem_length > size)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SM_CORRUPTED, 0);
	  or_abort (buf);
	}
      or_get_data (buf, (char *) mem, mem_length);

      /*
       * We should only see padding if the string is contained within a packed
       * value that had extra padding to ensure alignment.  If we see these,
       * just pop them out of the buffer.  This shouldn't ever happen for the
       * "getmem" function, only for the "getval" function.
       */
      if (size != -1)
	{
	  padding = size - mem_length;
	  if (padding > 0)
	    {
	      or_advance (buf, padding);
	    }
	}
    }
}

static void
mr_freemem_char (void *memptr)
{
}

static void
mr_initval_char (DB_VALUE * value, int precision, int scale)
{
  DB_DOMAIN_INIT_CHAR (value, precision);
}

static int
mr_setval_char (DB_VALUE * dest, const DB_VALUE * src, bool copy)
{
  int error = NO_ERROR;
  int src_precision, src_length;
  char *src_string, *new_;

  if (DB_IS_NULL (src))
    {
      DB_DOMAIN_INIT_CHAR (dest, TP_FLOATING_PRECISION_VALUE);
    }
  else
    {
      src_precision = DB_GET_STRING_PRECISION (src);
      if (src_precision == 0)
	{
	  src_precision = TP_FLOATING_PRECISION_VALUE;
	}
      DB_DOMAIN_INIT_CHAR (dest, src_precision);
      /* Get information from the value */
      src_string = DB_GET_STRING (src);
      src_length = DB_GET_STRING_SIZE (src);	/* size in bytes */

      /* shouldn't see a NULL string at this point, treat as NULL */
      if (src_string != NULL)
	{
	  if (!copy)
	    {
	      db_make_char (dest, src_precision, src_string, src_length,
			    DB_GET_STRING_CODESET (src),
			    DB_GET_STRING_COLLATION (src));
	    }
	  else
	    {
	      /* Check for NTS marker, may not need to do this any more */
	      if (src_length < 0)
		{
		  src_length = strlen (src_string);
		}

	      /* make sure the copy gets a NULL terminator */
	      new_ = db_private_alloc (NULL, src_length + 1);
	      if (new_ == NULL)
		{
		  assert (er_errid () != NO_ERROR);
		  error = er_errid ();
		}
	      else
		{
		  memcpy (new_, src_string, src_length);
		  new_[src_length] = '\0';
		  db_make_char (dest, src_precision, new_, src_length,
				DB_GET_STRING_CODESET (src),
				DB_GET_STRING_COLLATION (src));
		  dest->need_clear = true;
		}
	    }
	}
    }

  return error;
}

static int
mr_index_lengthval_char (DB_VALUE * value)
{
  return mr_data_lengthval_char (value, 1);
}

/*
 */
static int
mr_data_lengthval_char (DB_VALUE * value, int disk)
{
  int packed_length, src_precision;
  char *src;

  src = db_get_string (value);
  if (src == NULL)
    {
      return 0;
    }

  src_precision = db_value_precision (value);
  if (!IS_FLOATING_PRECISION (src_precision))
    {
      packed_length = STR_SIZE (src_precision, DB_GET_STRING_CODESET (value));
    }
  else
    {
      /*
       * Precision is "floating", calculate the effective precision based on the
       * string length.
       * Should be rounding this up so it is a proper multiple of the charset
       * width ?
       */
      packed_length = db_get_string_size (value);
      if (packed_length < 0)
	{
	  packed_length = strlen (src);
	}

      /* add in storage for a size prefix on the packed value. */
      packed_length += OR_INT_SIZE;
    }

  /*
   * NOTE: We do NOT perform padding here, if this is used in the context
   * of a packed value, the or_put_value() family of functions must handle
   * their own padding, this is because "lengthval" and "writeval" can be
   * used to place values into the disk representation of instances and
   * there can be no padding in there.
   */

  return packed_length;
}


static int
mr_index_writeval_char (OR_BUF * buf, DB_VALUE * value)
{
  return mr_writeval_char_internal (buf, value, CHAR_ALIGNMENT);
}

/*
 * See commentary in mr_lengthval_char.
 */
static int
mr_data_writeval_char (OR_BUF * buf, DB_VALUE * value)
{
  return mr_writeval_char_internal (buf, value, INT_ALIGNMENT);
}

static int
mr_writeval_char_internal (OR_BUF * buf, DB_VALUE * value, int align)
{
  int src_precision, src_length, packed_length, pad;
  char *src;
  int rc = NO_ERROR;

  src = db_get_string (value);
  if (src == NULL)
    {
      return rc;
    }

  src_precision = db_value_precision (value);

  if (!IS_FLOATING_PRECISION (src_precision))
    {
      src_length = db_get_string_size (value);	/* size in bytes */

      if (src_length < 0)
	{
	  src_length = strlen (src);
	}

      packed_length = STR_SIZE (src_precision, DB_GET_STRING_CODESET (value));

      if (packed_length < src_length)
	{
	  /* should have caught this by now, truncate silently */
	  rc = or_put_data (buf, src, packed_length);
	}
      else
	{
	  rc = or_put_data (buf, src, src_length);
	  /*
	   * Check for space padding, if this were a national string, we
	   * would need to be padding with the appropriate space character !
	   */
	  pad = packed_length - src_length;
	  if (pad)
	    {
	      int i;
	      for (i = src_length; i < packed_length; i++)
		{
		  rc = or_put_byte (buf, (int) ' ');
		}
	    }
	}
      if (rc != NO_ERROR)
	{
	  return rc;
	}
    }
  else
    {
      /*
       * This is a "floating" precision value. Pack what we can based on the
       * string size.  Note that for this to work, this can only be packed as
       * part of a domain tagged value and we must include a length prefix
       * after the domain.
       */
      packed_length = db_get_string_size (value);
      if (packed_length < 0)
	{
	  packed_length = strlen (src);
	}

      /* store the size prefix */
      if (align == INT_ALIGNMENT)
	{
	  rc = or_put_int (buf, packed_length);
	}
      else
	{
	  rc = or_put_data (buf, (char *) (&packed_length), OR_INT_SIZE);
	}
      if (rc == NO_ERROR)
	{
	  /* store the data */
	  rc = or_put_data (buf, src, packed_length);
	  /* there is no blank padding in this case */
	}
    }
  return rc;
}

static int
mr_index_readval_char (OR_BUF * buf, DB_VALUE * value, TP_DOMAIN * domain,
		       int disk_size, bool copy, char *copy_buf,
		       int copy_buf_len)
{
  return mr_readval_char_internal (buf, value, domain, disk_size, copy,
				   copy_buf, copy_buf_len, CHAR_ALIGNMENT);
}

static int
mr_data_readval_char (OR_BUF * buf, DB_VALUE * value, TP_DOMAIN * domain,
		      int disk_size, bool copy, char *copy_buf,
		      int copy_buf_len)
{
  return mr_readval_char_internal (buf, value, domain, disk_size, copy,
				   copy_buf, copy_buf_len, INT_ALIGNMENT);
}

static int
mr_readval_char_internal (OR_BUF * buf, DB_VALUE * value, TP_DOMAIN * domain,
			  int disk_size, bool copy, char *copy_buf,
			  int copy_buf_len, int align)
{
  int mem_length, padding;
  int str_length, precision;
  char *new_;
  int rc = NO_ERROR;

  precision = domain->precision;
  if (TP_DOMAIN_COLLATION_FLAG (domain) != TP_DOMAIN_COLL_NORMAL)
    {
      assert (false);
      return ER_FAILED;
    }

  if (IS_FLOATING_PRECISION (domain->precision))
    {
      if (align == INT_ALIGNMENT)
	{
	  mem_length = or_get_int (buf, &rc);
	}
      else
	{
	  rc = or_get_data (buf, (char *) (&mem_length), OR_INT_SIZE);
	}
      if (rc != NO_ERROR)
	{
	  return rc;
	}

      if (value == NULL)
	{
	  rc = or_advance (buf, mem_length);
	}
      else if (!copy)
	{
	  db_make_char (value, TP_FLOATING_PRECISION_VALUE, buf->ptr,
			mem_length, TP_DOMAIN_CODESET (domain),
			TP_DOMAIN_COLLATION (domain));
	  value->need_clear = false;
	  rc = or_advance (buf, mem_length);
	}
      else
	{
	  if (copy_buf && copy_buf_len >= mem_length + 1)
	    {
	      /* read buf image into the copy_buf */
	      new_ = copy_buf;
	    }
	  else
	    {
	      /* Allocate storage for the string
	       * including the kludge NULL terminator */
	      new_ = db_private_alloc (NULL, mem_length + 1);
	    }

	  if (new_ == NULL)
	    {
	      /* need to be able to return errors ! */
	      db_value_domain_init (value, TP_DOMAIN_TYPE (domain),
				    TP_FLOATING_PRECISION_VALUE, 0);
	      or_abort (buf);

	      return ER_FAILED;
	    }
	  else
	    {
	      rc = or_get_data (buf, new_, mem_length);
	      if (rc != NO_ERROR)
		{
		  if (new_ != copy_buf)
		    {
		      db_private_free_and_init (NULL, new_);
		    }
		  return rc;
		}
	      new_[mem_length] = '\0';	/* append the kludge NULL terminator */
	      db_make_char (value, TP_FLOATING_PRECISION_VALUE, new_,
			    mem_length, TP_DOMAIN_CODESET (domain),
			    TP_DOMAIN_COLLATION (domain));
	      value->need_clear = (new_ != copy_buf) ? true : false;
	    }
	}
    }
  else
    {
      /*
       * Normal fixed width char(n) whose size can be determined by looking at
       * the domain.
       */
      mem_length = STR_SIZE (domain->precision, TP_DOMAIN_CODESET (domain));

      if (disk_size != -1 && mem_length > disk_size)
	{
	  /*
	   * If we're low here, we could just read what we have and make a
	   * smaller value.  Still the domain should match at this point.
	   */
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SM_CORRUPTED, 0);
	  or_abort (buf);
	  return ER_FAILED;
	}

      if (value == NULL)
	{
	  rc = or_advance (buf, mem_length);
	}
      else if (disk_size == 0)
	{
	  db_value_domain_init (value, DB_TYPE_CHAR, precision, 0);
	}
      else if (!copy)
	{
	  intl_char_size ((unsigned char *) buf->ptr, domain->precision,
			  TP_DOMAIN_CODESET (domain), &str_length);
	  if (str_length == 0)
	    {
	      str_length = mem_length;
	    }
	  db_make_char (value, precision, buf->ptr, str_length,
			TP_DOMAIN_CODESET (domain),
			TP_DOMAIN_COLLATION (domain));
	  value->need_clear = false;
	  rc = or_advance (buf, mem_length);
	}
      else
	{
	  if (copy_buf && copy_buf_len >= mem_length + 1)
	    {
	      /* read buf image into the copy_buf */
	      new_ = copy_buf;
	    }
	  else
	    {
	      /*
	       * Allocate storage for the string including the kludge NULL
	       * terminator
	       */
	      new_ = db_private_alloc (NULL, mem_length + 1);
	    }
	  if (new_ == NULL)
	    {
	      /* need to be able to return errors ! */
	      db_value_domain_init (value, TP_DOMAIN_TYPE (domain),
				    domain->precision, 0);
	      or_abort (buf);

	      return ER_FAILED;
	    }
	  else
	    {
	      int actual_size = 0;
	      rc = or_get_data (buf, new_, mem_length);
	      if (rc != NO_ERROR)
		{
		  if (new_ != copy_buf)
		    {
		      db_private_free_and_init (NULL, new_);
		    }
		  return rc;
		}
	      intl_char_size ((unsigned char *) new_, domain->precision,
			      TP_DOMAIN_CODESET (domain), &actual_size);
	      if (actual_size == 0)
		{
		  actual_size = mem_length;
		}
	      new_[actual_size] = '\0';	/* append the kludge NULL terminator */
	      db_make_char (value, domain->precision, new_, actual_size,
			    TP_DOMAIN_CODESET (domain),
			    TP_DOMAIN_COLLATION (domain));
	      value->need_clear = (new_ != copy_buf) ? true : false;
	    }
	}

      if (rc == NO_ERROR)
	{
	  /*
	   * We should only see padding if the string is contained within a
	   * packed value that had extra padding to ensure alignment.  If we
	   * see these, just pop them out of the buffer.
	   */
	  if (disk_size != -1)
	    {
	      padding = disk_size - mem_length;
	      if (padding > 0)
		{
		  rc = or_advance (buf, padding);
		}
	    }
	}
    }
  return rc;
}

static int
mr_index_cmpdisk_char (void *mem1, void *mem2, TP_DOMAIN * domain,
		       int do_coercion, int total_order, int *start_colp)
{
  assert (domain != NULL);

  return mr_cmpdisk_char_internal (mem1, mem2, domain,
				   do_coercion, total_order, start_colp,
				   CHAR_ALIGNMENT);
}

static int
mr_data_cmpdisk_char (void *mem1, void *mem2, TP_DOMAIN * domain,
		      int do_coercion, int total_order, int *start_colp)
{
  assert (domain != NULL);

  return mr_cmpdisk_char_internal (mem1, mem2, domain,
				   do_coercion, total_order, start_colp,
				   INT_ALIGNMENT);
}

static int
mr_cmpdisk_char_internal (void *mem1, void *mem2, TP_DOMAIN * domain,
			  int do_coercion, int total_order, int *start_colp,
			  int align)
{
  int mem_length1, mem_length2, c;

  if (IS_FLOATING_PRECISION (domain->precision))
    {
      if (align == INT_ALIGNMENT)
	{
	  mem_length1 = OR_GET_INT (mem1);
	  mem_length2 = OR_GET_INT (mem2);
	}
      else
	{
	  memcpy (&mem_length1, mem1, OR_INT_SIZE);
	  memcpy (&mem_length2, mem2, OR_INT_SIZE);
	}
      mem1 = (char *) mem1 + OR_INT_SIZE;
      mem2 = (char *) mem2 + OR_INT_SIZE;
    }
  else
    {
      /*
       * Normal fixed width char(n) whose size can be determined by looking at
       * the domain.
       * Needs NCHAR work here to separate the dependencies on disk_size and
       * mem_size.
       */
      mem_length1 = mem_length2 =
	STR_SIZE (domain->precision, TP_DOMAIN_CODESET (domain));
    }

  c = QSTR_CHAR_COMPARE (domain->collation_id,
			 (unsigned char *) mem1, mem_length1,
			 (unsigned char *) mem2, mem_length2);
  c = MR_CMP_RETURN_CODE (c);

  return c;
}

static int
mr_cmpval_char (DB_VALUE * value1, DB_VALUE * value2,
		int do_coercion, int total_order, int *start_colp,
		int collation)
{
  int c;
  unsigned char *string1, *string2;

  string1 = (unsigned char *) DB_GET_STRING (value1);
  string2 = (unsigned char *) DB_GET_STRING (value2);

  if (string1 == NULL || string2 == NULL
      || DB_GET_STRING_CODESET (value1) != DB_GET_STRING_CODESET (value1))
    {
      return DB_UNK;
    }

  if (collation == -1)
    {
      assert (false);
      return DB_UNK;
    }

  c = QSTR_CHAR_COMPARE (collation,
			 string1, (int) DB_GET_STRING_SIZE (value1),
			 string2, (int) DB_GET_STRING_SIZE (value2));
  c = MR_CMP_RETURN_CODE (c);

  return c;
}

#if defined (ENABLE_UNUSED_FUNCTION)
static int
mr_cmpval_char2 (DB_VALUE * value1, DB_VALUE * value2, int length,
		 int do_coercion, int total_order, int *start_colp)
{
  int c;
  unsigned char *string1, *string2;
  int len1, len2, string_size;

  string1 = (unsigned char *) DB_GET_STRING (value1);
  string2 = (unsigned char *) DB_GET_STRING (value2);

  if (string1 == NULL || string2 == NULL)
    {
      return DB_UNK;
    }

  string_size = (int) DB_GET_STRING_SIZE (value1);
  len1 = MIN (string_size, length);
  string_size = (int) DB_GET_STRING_SIZE (value2);
  len2 = MIN (string_size, length);

  c = QSTR_CHAR_COMPARE (string1, len1, string2, len2);
  c = MR_CMP_RETURN_CODE (c);

  return c;
}
#endif

PR_TYPE tp_Char = {
  "character", DB_TYPE_CHAR, 0, 0, 0, 1,
  help_fprint_value,
  help_sprint_value,
  mr_initmem_char,
  mr_initval_char,
  mr_setmem_char,
  mr_getmem_char,
  mr_setval_char,
  mr_data_lengthmem_char,
  mr_data_lengthval_char,
  mr_data_writemem_char,
  mr_data_readmem_char,
  mr_data_writeval_char,
  mr_data_readval_char,
  mr_index_lengthmem_char,
  mr_index_lengthval_char,
  mr_index_writeval_char,
  mr_index_readval_char,
  mr_index_cmpdisk_char,
  mr_freemem_char,
  mr_data_cmpdisk_char,
  mr_cmpval_char
};

PR_TYPE *tp_Type_char = &tp_Char;

/*
 * TYPE NCHAR
 */

static void
mr_initmem_nchar (void *memptr, TP_DOMAIN * domain)
{
#if !defined(NDEBUG)
  int mem_length;

  assert (!IS_FLOATING_PRECISION (domain->precision));

  mem_length = STR_SIZE (domain->precision, TP_DOMAIN_CODESET (domain));
  memset (memptr, 0, mem_length);
#endif
}


/*
 * Due to the "within tolerance" comparison of domains used for
 * assignment validation, we may get values in here whose precision is
 * less than the precision of the actual attribute.   This prevents
 * having to copy a string just to coerce a value into a larger precision.
 * Note that the precision of the source value may also come in as -1 here
 * which is used to mean a "floating" precision that is assumed to be
 * compatible with the destination domain as long as the associated value
 * is within tolerance.  This case is generally only seen for string
 * literals that have been produced by the parser.  These literals will
 * not contain blank padding and strlen() or db_get_string_size() can be
 * used to determine the number of significant characters.
 */
static int
mr_setmem_nchar (void *memptr, TP_DOMAIN * domain, DB_VALUE * value)
{
  int error = NO_ERROR;
  char *src, *mem;
  int src_precision, src_length, mem_length, pad;

  if (value == NULL)
    return NO_ERROR;

  /* Get information from the value */
  src = db_get_string (value);
  src_precision = db_value_precision (value);
  src_length = db_get_string_size (value);	/* size in bytes */

  if (src == NULL)
    {
      return NO_ERROR;
    }

  /* Check for special NTS flag.  This may not be necessary any more. */
  if (src_length < 0)
    src_length = strlen (src);


  /*
   * The only thing we really care about at this point, is the byte
   * length of the string.  The precision could be checked here but it
   * really isn't necessary for this operation.
   * Calculate the maximum number of bytes we have available here.
   */
  mem_length = STR_SIZE (domain->precision, TP_DOMAIN_CODESET (domain));

  if (mem_length < src_length)
    {
      /*
       * should never get here, this is supposed to be caught during domain
       * validation, need a better error message.
       */
      error = ER_OBJ_DOMAIN_CONFLICT;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, "");
    }
  else
    {
      /* copy the value into memory */
      mem = (char *) memptr;
      memcpy (mem, src, src_length);

      /*
       * Check for space padding, if this were a national string, we would need
       * to be padding with the appropriate space character !
       */
      pad = mem_length - src_length;
      if (pad)
	{
	  int i;
	  for (i = src_length; i < mem_length; i++)
	    mem[i] = ' ';
	}
    }
  return error;
}


/*
 * We always ensure that the string returned here is NULL terminated
 * if the copy flag is on.  This technically shouldn't be necessary but there
 * is a lot of code scattered about (especially applications) that assumes
 * NULL termination.
 */
static int
mr_getmem_nchar (void *mem, TP_DOMAIN * domain, DB_VALUE * value, bool copy)
{
  int mem_length;
  char *new_;

  if (TP_DOMAIN_COLLATION_FLAG (domain) != TP_DOMAIN_COLL_NORMAL)
    {
      assert (false);
      return ER_FAILED;
    }
  intl_char_size ((unsigned char *) mem, domain->precision,
		  TP_DOMAIN_CODESET (domain), &mem_length);
  if (mem_length == 0)
    {
      mem_length = STR_SIZE (domain->precision, TP_DOMAIN_CODESET (domain));
    }

  if (!copy)
    {
      new_ = (char *) mem;
    }
  else
    {
      new_ = db_private_alloc (NULL, mem_length + 1);
      if (new_ == NULL)
	{
	  assert (er_errid () != NO_ERROR);
	  return er_errid ();
	}
      memcpy (new_, (char *) mem, mem_length);
      /* make sure that all outgoing strings are NULL terminated */
      new_[mem_length] = '\0';
    }

  if (TP_DOMAIN_COLLATION_FLAG (domain) != TP_DOMAIN_COLL_NORMAL)
    {
      assert (false);
      return ER_FAILED;
    }
  db_make_nchar (value, domain->precision, new_, mem_length,
		 TP_DOMAIN_CODESET (domain), TP_DOMAIN_COLLATION (domain));
  if (copy)
    {
      value->need_clear = true;
    }

  return NO_ERROR;
}

static int
mr_data_lengthmem_nchar (void *memptr, TP_DOMAIN * domain, int disk)
{
  assert (!IS_FLOATING_PRECISION (domain->precision));

  return STR_SIZE (domain->precision, TP_DOMAIN_CODESET (domain));
}

static int
mr_index_lengthmem_nchar (void *memptr, TP_DOMAIN * domain)
{
  int mem_length;

  assert (!(IS_FLOATING_PRECISION (domain->precision) && memptr == NULL));

  if (!IS_FLOATING_PRECISION (domain->precision))
    {
      mem_length = STR_SIZE (domain->precision, TP_DOMAIN_CODESET (domain));
    }
  else
    {
      memcpy (&mem_length, memptr, OR_INT_SIZE);
      mem_length += OR_INT_SIZE;
    }

  return mem_length;
}

static void
mr_data_writemem_nchar (OR_BUF * buf, void *mem, TP_DOMAIN * domain)
{
  int mem_length;

  mem_length = STR_SIZE (domain->precision, TP_DOMAIN_CODESET (domain));

  /*
   * We simply dump the memory image to disk, it will already have been padded.
   * If this were a national character string, at this point, we'd have to
   * decide now to perform a character set conversion.
   */
  or_put_data (buf, (char *) mem, mem_length);
}

static void
mr_data_readmem_nchar (OR_BUF * buf, void *mem, TP_DOMAIN * domain, int size)
{
  int mem_length, padding;

  if (mem == NULL)
    {
      /*
       * If we passed in a size, then use it.  Otherwise, determine the
       * size from the domain.
       */
      if (size > 0)
	{
	  or_advance (buf, size);
	}
      else
	{
	  mem_length = STR_SIZE (domain->precision,
				 TP_DOMAIN_CODESET (domain));
	  or_advance (buf, mem_length);
	}
    }
  else
    {
      mem_length = STR_SIZE (domain->precision, TP_DOMAIN_CODESET (domain));

      if (size != -1 && mem_length > size)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SM_CORRUPTED, 0);
	  or_abort (buf);
	}
      or_get_data (buf, (char *) mem, mem_length);

      /*
       * We should only see padding if the string is contained within a packed
       * value that had extra padding to ensure alignment.  If we see these,
       * just pop them out of the buffer.  This shouldn't ever happen for
       * the "getmem" function, only for the "getval" function.
       */
      if (size != -1)
	{
	  padding = size - mem_length;
	  if (padding > 0)
	    or_advance (buf, padding);
	}
    }
}

static void
mr_freemem_nchar (void *memptr)
{
}

static void
mr_initval_nchar (DB_VALUE * value, int precision, int scale)
{
  db_value_domain_init (value, DB_TYPE_NCHAR, precision, scale);
}

static int
mr_setval_nchar (DB_VALUE * dest, const DB_VALUE * src, bool copy)
{
  int error = NO_ERROR;
  int src_precision, src_length;
  char *src_string, *new_;

  if (src == NULL || (DB_IS_NULL (src) && db_value_precision (src) == 0))
    db_value_domain_init (dest, DB_TYPE_NCHAR, TP_FLOATING_PRECISION_VALUE,
			  0);
  else if (DB_IS_NULL (src))
    db_value_domain_init (dest, DB_TYPE_NCHAR, db_value_precision (src), 0);
  else
    {
      /* Get information from the value */
      src_string = db_get_string (src);
      src_precision = db_value_precision (src);
      src_length = db_get_string_size (src);	/* size in bytes */

      db_value_domain_init (dest, DB_TYPE_NCHAR, src_precision, 0);

      /* shouldn't see a NULL string at this point, treat as NULL */
      if (src_string != NULL)
	{
	  if (!copy)
	    db_make_nchar (dest, src_precision, src_string, src_length,
			   DB_GET_STRING_CODESET (src),
			   DB_GET_STRING_COLLATION (src));
	  else
	    {
	      /* Check for NTS marker, may not need to do this any more */
	      if (src_length < 0)
		src_length = strlen (src_string);

	      /* make sure the copy gets a NULL terminator */
	      new_ = db_private_alloc (NULL, src_length + 1);
	      if (new_ == NULL)
		{
		  assert (er_errid () != NO_ERROR);
		  error = er_errid ();
		}
	      else
		{
		  memcpy (new_, src_string, src_length);
		  new_[src_length] = '\0';
		  db_make_nchar (dest, src_precision, new_, src_length,
				 DB_GET_STRING_CODESET (src),
				 DB_GET_STRING_COLLATION (src));
		  dest->need_clear = true;
		}
	    }
	}
    }
  return error;
}


static int
mr_index_lengthval_nchar (DB_VALUE * value)
{
  return mr_data_lengthval_nchar (value, 1);
}

static int
mr_data_lengthval_nchar (DB_VALUE * value, int disk)
{
  int packed_length, src_precision;
  char *src;
  INTL_CODESET src_codeset = (INTL_CODESET) db_get_string_codeset (value);

  src = db_get_string (value);
  if (src == NULL)
    {
      return 0;
    }

  src_precision = db_value_precision (value);
  if (!IS_FLOATING_PRECISION (src_precision))
    {
      /* Would have to check for charset conversion here ! */
      packed_length = STR_SIZE (src_precision, src_codeset);
    }
  else
    {
      /* Precision is "floating", calculate the effective precision based on the
       * string length.
       * Should be rounding this up so it is a proper multiple of the charset
       * width ?
       */
      packed_length = db_get_string_size (value);
      if (packed_length < 0)
	{
	  packed_length = strlen (src);
	}

#if !defined (SERVER_MODE)
      /*
       * If this is a client side string, and the disk representation length
       * is requested,  Need to return the length of a converted string.
       *
       * Note: This is only done to support the 'floating precision' style of
       * fixed strings.  In this case, the string is treated similarly to a
       * variable length string.
       */
      if (!db_on_server && packed_length > 0 && disk
	  && DO_CONVERSION_TO_SRVR_STR (src_codeset))
	{
	  int unconverted;
	  int char_count = db_get_string_length (value);
	  char *converted_string = db_private_alloc (NULL,
						     STR_SIZE (char_count,
							       src_codeset));
	  intl_convert_charset ((unsigned char *) src, char_count,
				src_codeset,
				(unsigned char *) converted_string,
				lang_charset (), &unconverted);

	  if (converted_string)
	    {
	      intl_char_size ((unsigned char *) converted_string,
			      (char_count - unconverted),
			      src_codeset, &packed_length);
	      db_private_free_and_init (NULL, converted_string);
	    }
	}
#endif /* !SERVER_MODE */

      /* add in storage for a size prefix on the packed value. */
      packed_length += OR_INT_SIZE;
    }

  /*
   * NOTE: We do NOT perform padding here, if this is used in the context
   * of a packed value, the or_put_value() family of functions must handle
   * their own padding, this is because "lengthval" and "writeval" can be
   * used to place values into the disk representation of instances and
   * there can be no padding in there.
   */

  return packed_length;
}


static int
mr_index_writeval_nchar (OR_BUF * buf, DB_VALUE * value)
{
  return mr_writeval_nchar_internal (buf, value, CHAR_ALIGNMENT);
}

/*
 * See commentary in mr_lengthval_nchar.
 */
static int
mr_data_writeval_nchar (OR_BUF * buf, DB_VALUE * value)
{
  return mr_writeval_nchar_internal (buf, value, INT_ALIGNMENT);
}

static int
mr_writeval_nchar_internal (OR_BUF * buf, DB_VALUE * value, int align)
{
  int src_precision, src_size, packed_size, pad;
  char *src;
  INTL_CODESET src_codeset;
  int pad_charsize;
  char pad_char[2];
  char *converted_string = NULL;
  int rc = NO_ERROR;

  src = db_get_string (value);
  if (src == NULL)
    {
      return rc;
    }

  src_precision = db_value_precision (value);
  src_size = db_get_string_size (value);	/* size in bytes */
  src_codeset = (INTL_CODESET) db_get_string_codeset (value);

  if (src_size < 0)
    {
      src_size = strlen (src);
    }

#if !defined (SERVER_MODE)
  if (!db_on_server && src_size > 0
      && DO_CONVERSION_TO_SRVR_STR (src_codeset))
    {
      int unconverted;
      int char_count = db_get_string_length (value);
      converted_string = (char *) db_private_alloc (NULL,
						    STR_SIZE (char_count,
							      src_codeset));
      (void) intl_convert_charset ((unsigned char *) src, char_count,
				   src_codeset,
				   (unsigned char *) converted_string,
				   lang_charset (), &unconverted);
      /* Reset the 'src' of the string */
      src = converted_string;
      src_precision = src_precision - unconverted;
      intl_char_size ((unsigned char *) converted_string,
		      (char_count - unconverted), src_codeset, &src_size);
      src_codeset = lang_charset ();
    }
  intl_pad_char (src_codeset, (unsigned char *) pad_char, &pad_charsize);
#else
  /*
   * (void) lang_srvr_space_char(pad_char, &pad_charsize);
   *
   * Until this is resolved, set the pad character to be an ASCII space.
   */
  pad_charsize = 1;
  pad_char[0] = ' ';
  pad_char[1] = ' ';
#endif

  if (!IS_FLOATING_PRECISION (src_precision))
    {
      packed_size = STR_SIZE (src_precision, src_codeset);

      if (packed_size < src_size)
	{
	  /* should have caught this by now, truncate silently */
	  rc = or_put_data (buf, src, packed_size);
	}
      else
	{
	  if ((rc = or_put_data (buf, src, src_size)) == NO_ERROR)
	    {
	      /*
	       * Check for space padding, if this were a national string, we
	       * would need to be padding with the appropriate space character!
	       */
	      pad = packed_size - src_size;
	      if (pad)
		{
		  int i;
		  for (i = src_size; i < packed_size; i += pad_charsize)
		    {
		      rc = or_put_byte (buf, pad_char[0]);
		      if (i + 1 < packed_size && pad_charsize == 2)
			{
			  rc = or_put_byte (buf, pad_char[1]);
			}
		    }
		}
	    }
	}
      if (rc != NO_ERROR)
	{
	  goto error;
	}
    }
  else
    {
      /*
       * This is a "floating" precision value. Pack what we can based on the
       * string size.  Note that for this to work, this can only be packed as
       * part of a domain tagged value and we must include a size prefix after
       * the domain.
       */

      /* store the size prefix */
      if (align == INT_ALIGNMENT)
	{
	  rc = or_put_int (buf, src_size);
	}
      else
	{
	  rc = or_put_data (buf, (char *) (&src_size), OR_INT_SIZE);
	}

      if (rc == NO_ERROR)
	{
	  /* store the data */
	  rc = or_put_data (buf, src, src_size);
	  /* there is no blank padding in this case */
	}
    }

error:
  if (converted_string)
    {
      db_private_free_and_init (NULL, converted_string);
    }

  return rc;
}

static int
mr_index_readval_nchar (OR_BUF * buf, DB_VALUE * value,
			TP_DOMAIN * domain, int disk_size, bool copy,
			char *copy_buf, int copy_buf_len)
{
  return mr_readval_nchar_internal (buf, value, domain, disk_size, copy,
				    copy_buf, copy_buf_len, CHAR_ALIGNMENT);
}

static int
mr_data_readval_nchar (OR_BUF * buf, DB_VALUE * value,
		       TP_DOMAIN * domain, int disk_size, bool copy,
		       char *copy_buf, int copy_buf_len)
{
  return mr_readval_nchar_internal (buf, value, domain, disk_size, copy,
				    copy_buf, copy_buf_len, INT_ALIGNMENT);
}

static int
mr_readval_nchar_internal (OR_BUF * buf, DB_VALUE * value,
			   TP_DOMAIN * domain, int disk_size, bool copy,
			   char *copy_buf, int copy_buf_len, int align)
{
  int mem_length, padding;
  char *new_;
  int rc = NO_ERROR;

  if (TP_DOMAIN_COLLATION_FLAG (domain) != TP_DOMAIN_COLL_NORMAL)
    {
      assert (false);
      return ER_FAILED;
    }

  if (IS_FLOATING_PRECISION (domain->precision))
    {
      if (align == INT_ALIGNMENT)
	{
	  mem_length = or_get_int (buf, &rc);
	}
      else
	{
	  rc = or_get_data (buf, (char *) (&mem_length), OR_INT_SIZE);
	}
      if (rc != NO_ERROR)
	{
	  return ER_FAILED;
	}

      if (value == NULL)
	{
	  rc = or_advance (buf, mem_length);
	}
      else if (!copy)
	{
	  db_make_nchar (value, TP_FLOATING_PRECISION_VALUE,
			 buf->ptr, mem_length, TP_DOMAIN_CODESET (domain),
			 TP_DOMAIN_COLLATION (domain));
	  value->need_clear = false;
	  rc = or_advance (buf, mem_length);
	}
      else
	{
	  if (copy_buf && copy_buf_len >= mem_length + 1)
	    {
	      /* read buf image into the copy_buf */
	      new_ = copy_buf;
	    }
	  else
	    {
	      /*
	       * Allocate storage for the string including the kludge NULL
	       * terminator
	       */
	      new_ = db_private_alloc (NULL, mem_length + 1);
	    }

	  if (new_ == NULL)
	    {
	      /* need to be able to return errors ! */
	      db_value_domain_init (value, TP_DOMAIN_TYPE (domain),
				    TP_FLOATING_PRECISION_VALUE, 0);
	      or_abort (buf);

	      return ER_FAILED;
	    }
	  else
	    {
	      if ((rc = or_get_data (buf, new_, mem_length)) != NO_ERROR)
		{
		  if (new_ != copy_buf)
		    {
		      db_private_free_and_init (NULL, new_);
		    }
		  return rc;
		}
	      new_[mem_length] = '\0';	/* append the kludge NULL terminator */
	      db_make_nchar (value, TP_FLOATING_PRECISION_VALUE, new_,
			     mem_length, TP_DOMAIN_CODESET (domain),
			     TP_DOMAIN_COLLATION (domain));
	      value->need_clear = (new_ != copy_buf) ? true : false;
	    }
	}
    }
  else
    {
      mem_length = STR_SIZE (domain->precision, TP_DOMAIN_CODESET (domain));

      if (disk_size != -1 && mem_length > disk_size)
	{
	  /*
	   * If we're low here, we could just read what we have and make a
	   * smaller value.  Still the domain should match at this point.
	   */
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SM_CORRUPTED, 0);
	  or_abort (buf);

	  return ER_FAILED;
	}

      if (value == NULL)
	{
	  rc = or_advance (buf, mem_length);
	}
      else if (!copy)
	{
	  int str_length;
	  intl_char_size ((unsigned char *) buf->ptr, domain->precision,
			  TP_DOMAIN_CODESET (domain), &str_length);
	  if (str_length == 0)
	    {
	      str_length = mem_length;
	    }
	  db_make_nchar (value, domain->precision, buf->ptr, str_length,
			 TP_DOMAIN_CODESET (domain),
			 TP_DOMAIN_COLLATION (domain));
	  value->need_clear = false;
	  rc = or_advance (buf, mem_length);
	}
      else
	{
	  if (copy_buf && copy_buf_len >= mem_length + 1)
	    {
	      /* read buf image into the copy_buf */
	      new_ = copy_buf;
	    }
	  else
	    {
	      /*
	       * Allocate storage for the string including the kludge NULL
	       * terminator
	       */
	      new_ = db_private_alloc (NULL, mem_length + 1);
	    }

	  if (new_ == NULL)
	    {
	      /* need to be able to return errors ! */
	      db_value_domain_init (value, TP_DOMAIN_TYPE (domain),
				    domain->precision, 0);
	      or_abort (buf);

	      return ER_FAILED;
	    }
	  else
	    {
	      int actual_size = 0;
	      if ((rc = or_get_data (buf, new_, mem_length)) != NO_ERROR)
		{
		  if (new_ != copy_buf)
		    {
		      db_private_free_and_init (NULL, new_);
		    }

		  return rc;
		}
	      intl_char_size ((unsigned char *) new_, domain->precision,
			      TP_DOMAIN_CODESET (domain), &actual_size);
	      if (actual_size == 0)
		{
		  actual_size = mem_length;
		}
	      new_[actual_size] = '\0';	/* append the kludge NULL terminator */
	      db_make_nchar (value, domain->precision, new_, actual_size,
			     TP_DOMAIN_CODESET (domain),
			     TP_DOMAIN_COLLATION (domain));
	      value->need_clear = (new_ != copy_buf) ? true : false;
	    }
	}

      if (rc == NO_ERROR)
	{
	  /* We should only see padding if the string is contained within a
	   * packed value that had extra padding to ensure alignment.  If we
	   * see these, just pop them out of the buffer.
	   */
	  if (disk_size != -1)
	    {
	      padding = disk_size - mem_length;
	      if (padding > 0)
		{
		  rc = or_advance (buf, padding);
		}
	    }
	}
    }

  /* Check if conversion needs to be done */
#if !defined (SERVER_MODE)
  if (value && !db_on_server
      && DO_CONVERSION_TO_SQLTEXT (TP_DOMAIN_CODESET (domain))
      && !DB_IS_NULL (value))
    {
      int unconverted;
      int char_count;
      char *temp_string = db_get_nchar (value, &char_count);
      if (char_count > 0)
	{
	  new_ = db_private_alloc (NULL,
				   STR_SIZE (char_count,
					     TP_DOMAIN_CODESET (domain)));
	  (void) intl_convert_charset ((unsigned char *) temp_string,
				       char_count,
				       (INTL_CODESET)
				       TP_DOMAIN_CODESET (domain),
				       (unsigned char *) new_,
				       LANG_SYS_CODESET, &unconverted);
	  db_value_clear (value);
	  db_make_nchar (value, domain->precision, new_,
			 STR_SIZE (char_count, TP_DOMAIN_CODESET (domain)),
			 TP_DOMAIN_CODESET (domain),
			 TP_DOMAIN_COLLATION (domain));
	  value->need_clear = true;
	}
    }
#endif /* !SERVER_MODE */

  return rc;
}

static int
mr_index_cmpdisk_nchar (void *mem1, void *mem2, TP_DOMAIN * domain,
			int do_coercion, int total_order, int *start_colp)
{
  assert (domain != NULL);

  return mr_cmpdisk_nchar_internal (mem1, mem2, domain,
				    do_coercion, total_order, start_colp,
				    CHAR_ALIGNMENT);
}

static int
mr_data_cmpdisk_nchar (void *mem1, void *mem2, TP_DOMAIN * domain,
		       int do_coercion, int total_order, int *start_colp)
{
  assert (domain != NULL);

  return mr_cmpdisk_nchar_internal (mem1, mem2, domain,
				    do_coercion, total_order, start_colp,
				    INT_ALIGNMENT);
}

static int
mr_cmpdisk_nchar_internal (void *mem1, void *mem2, TP_DOMAIN * domain,
			   int do_coercion, int total_order, int *start_colp,
			   int align)
{
  int mem_length1, mem_length2, c;

  if (IS_FLOATING_PRECISION (domain->precision))
    {
      /*
       * This is only allowed if we're unpacking a "floating" domain CHAR(n) in
       * which case there will be a byte size prefix.
       */
      if (align == INT_ALIGNMENT)
	{
	  mem_length1 = OR_GET_INT (mem1);
	  mem_length2 = OR_GET_INT (mem2);
	}
      else
	{
	  memcpy (&mem_length1, mem1, OR_INT_SIZE);
	  memcpy (&mem_length2, mem2, OR_INT_SIZE);
	}
      mem1 = (char *) mem1 + OR_INT_SIZE;
      mem2 = (char *) mem2 + OR_INT_SIZE;
    }
  else
    {
      mem_length1 = mem_length2 =
	STR_SIZE (domain->precision, TP_DOMAIN_CODESET (domain));
    }

  c = QSTR_NCHAR_COMPARE (domain->collation_id,
			  (unsigned char *) mem1, mem_length1,
			  (unsigned char *) mem2, mem_length2,
			  (INTL_CODESET) TP_DOMAIN_CODESET (domain));
  c = MR_CMP_RETURN_CODE (c);

  return c;
}

static int
mr_cmpval_nchar (DB_VALUE * value1, DB_VALUE * value2,
		 int do_coercion, int total_order, int *start_colp,
		 int collation)
{
  int c;
  unsigned char *string1, *string2;

  string1 = (unsigned char *) DB_GET_STRING (value1);
  string2 = (unsigned char *) DB_GET_STRING (value2);

  if (string1 == NULL || string2 == NULL
      || DB_GET_STRING_CODESET (value1) != DB_GET_STRING_CODESET (value2))
    {
      return DB_UNK;
    }

  if (collation == -1)
    {
      assert (false);
      return DB_UNK;
    }

  c = QSTR_NCHAR_COMPARE (collation,
			  string1, (int) DB_GET_STRING_SIZE (value1),
			  string2, (int) DB_GET_STRING_SIZE (value2),
			  (INTL_CODESET) DB_GET_STRING_CODESET (value2));
  c = MR_CMP_RETURN_CODE (c);

  return c;
}

#if defined (ENABLE_UNUSED_FUNCTION)
static int
mr_cmpval_nchar2 (DB_VALUE * value1, DB_VALUE * value2, int length,
		  int do_coercion, int total_order, int *start_colp)
{
  int c;
  unsigned char *string1, *string2;
  int len1, len2, string_size;

  string1 = (unsigned char *) DB_GET_STRING (value1);
  string2 = (unsigned char *) DB_GET_STRING (value2);

  if (string1 == NULL || string2 == NULL)
    {
      return DB_UNK;
    }

  string_size = (int) DB_GET_STRING_SIZE (value1);
  len1 = MIN (string_size, length);
  string_size = (int) DB_GET_STRING_SIZE (value2);
  len2 = MIN (string_size, length);

  c = nchar_compare (string1, len1, string2, len2,
		     (INTL_CODESET) DB_GET_STRING_CODESET (value2));
  c = MR_CMP_RETURN_CODE (c);

  return c;
}
#endif


PR_TYPE tp_NChar = {
  "national character", DB_TYPE_NCHAR, 0, 0, 0, 1,
  help_fprint_value,
  help_sprint_value,
  mr_initmem_nchar,
  mr_initval_nchar,
  mr_setmem_nchar,
  mr_getmem_nchar,
  mr_setval_nchar,
  mr_data_lengthmem_nchar,
  mr_data_lengthval_nchar,
  mr_data_writemem_nchar,
  mr_data_readmem_nchar,
  mr_data_writeval_nchar,
  mr_data_readval_nchar,
  mr_index_lengthmem_nchar,
  mr_index_lengthval_nchar,
  mr_index_writeval_nchar,
  mr_index_readval_nchar,
  mr_index_cmpdisk_nchar,
  mr_freemem_nchar,
  mr_data_cmpdisk_nchar,
  mr_cmpval_nchar
};

PR_TYPE *tp_Type_nchar = &tp_NChar;

/*
 * TYPE VARNCHAR
 */

static void
mr_initmem_varnchar (void *mem, TP_DOMAIN * domain)
{
  *(char **) mem = NULL;
}


/*
 * The main difference between "memory" strings and "value" strings is that
 * the length tag is stored as an in-line prefix in the memory block allocated
 * to hold the string characters.
 */
static int
mr_setmem_varnchar (void *memptr, TP_DOMAIN * domain, DB_VALUE * value)
{
  int error = NO_ERROR;
  char *src, *cur, *new_, **mem;
  int src_precision, src_length, new_length;

  /* get the current memory contents */
  mem = (char **) memptr;
  cur = *mem;

  if (value == NULL || (src = db_get_string (value)) == NULL)
    {
      /* remove the current value */
      if (cur != NULL)
	{
	  db_private_free_and_init (NULL, cur);
	  mr_initmem_varnchar (memptr, domain);
	}
    }
  else
    {
      /*
       * Get information from the value.  Ignore precision for the time being
       * since we really only care about the byte size of the value for
       * varnchar.
       * Whether or not the value "fits" should have been checked by now.
       */
      src_precision = db_value_precision (value);
      src_length = db_get_string_size (value);	/* size in bytes */
      if (src_length < 0)
	{
	  src_length = strlen (src);
	}

      /*
       * Currently we NULL terminate the workspace string.
       * Could try to do the single byte size hack like we have in the
       * disk representation.
       */
      new_length = src_length + sizeof (int) + 1;
      new_ = db_private_alloc (NULL, new_length);
      if (new_ == NULL)
	{
	  assert (er_errid () != NO_ERROR);
	  error = er_errid ();
	}
      else
	{
	  if (cur != NULL)
	    {
	      db_private_free_and_init (NULL, cur);
	    }

	  /* pack in the length prefix */
	  *(int *) new_ = src_length;
	  cur = new_ + sizeof (int);
	  /* store the string */
	  memcpy (cur, src, src_length);
	  /* NULL terminate the stored string for safety */
	  cur[src_length] = '\0';
	  *mem = new_;
	}
    }

  return error;
}

static int
mr_getmem_varnchar (void *memptr, TP_DOMAIN * domain, DB_VALUE * value,
		    bool copy)
{
  int error = NO_ERROR;
  int mem_length;
  char **mem, *cur, *new_;

  /* get to the current value */
  mem = (char **) memptr;
  cur = *mem;

  if (cur == NULL)
    {
      db_value_domain_init (value, DB_TYPE_VARNCHAR, domain->precision, 0);
      value->need_clear = false;
    }
  else
    {
      /* extract the length prefix and the pointer to the actual string data */
      mem_length = *(int *) cur;
      cur += sizeof (int);

      if (TP_DOMAIN_COLLATION_FLAG (domain) != TP_DOMAIN_COLL_NORMAL)
	{
	  assert (false);
	  return ER_FAILED;
	}

      if (!copy)
	{
	  db_make_varnchar (value, domain->precision, cur, mem_length,
			    TP_DOMAIN_CODESET (domain),
			    TP_DOMAIN_COLLATION (domain));
	  value->need_clear = false;
	}
      else
	{
	  /* return it with a NULL terminator */
	  new_ = db_private_alloc (NULL, mem_length + 1);
	  if (new_ == NULL)
	    {
	      assert (er_errid () != NO_ERROR);
	      error = er_errid ();
	    }
	  else
	    {
	      memcpy (new_, cur, mem_length);
	      new_[mem_length] = '\0';
	      db_make_varnchar (value, domain->precision, new_, mem_length,
				TP_DOMAIN_CODESET (domain),
				TP_DOMAIN_COLLATION (domain));
	      value->need_clear = true;
	    }
	}
    }
  return error;
}


/*
 * For the disk representation, we may be adding pad bytes
 * to round up to a word boundary.
 *
 * NOTE: We are currently adding a NULL terminator to the disk representation
 * for some code on the server that still manufactures pointers directly into
 * the disk buffer and assumes it is a NULL terminated string.  This terminator
 * can be removed after the server has been updated.  The logic for maintaining
 * the terminator is actuall in the or_put_varchar, family of functions.
 */
static int
mr_data_lengthmem_varnchar (void *memptr, TP_DOMAIN * domain, int disk)
{
  char **mem, *cur;
  int len;

  len = 0;
  if (!disk)
    {
      len = tp_VarNChar.size;
    }
  else if (memptr != NULL)
    {
      mem = (char **) memptr;
      cur = *mem;
      if (cur != NULL)
	{
	  len = *(int *) cur;
	  len = or_packed_varchar_length (len);
	}
    }

  return len;
}

static int
mr_index_lengthmem_varnchar (void *memptr, TP_DOMAIN * domain)
{
  return mr_index_lengthmem_string (memptr, domain);
}

static void
mr_data_writemem_varnchar (OR_BUF * buf, void *memptr, TP_DOMAIN * domain)
{
  char **mem, *cur;
  int len;

  mem = (char **) memptr;
  cur = *mem;
  if (cur != NULL)
    {
      len = *(int *) cur;
      cur += sizeof (int);
      or_packed_put_varchar (buf, cur, len);
    }
}


/*
 * The amount of memory requested is currently calculated based on the
 * stored size prefix.  If we ever go to a system where we avoid storing
 * the size, then we could use the size argument passed in to this function
 * but that may also include any padding byte that added to bring us up to
 * a word boundary. Might want some way to determine which bytes at the
 * end of a string are padding.
 */
static void
mr_data_readmem_varnchar (OR_BUF * buf, void *memptr, TP_DOMAIN * domain,
			  int size)
{
  char **mem, *cur, *new_;
  int len;
  int mem_length, pad;
  char *start;
  int rc = NO_ERROR;

  /* Must have an explicit size here - can't be determined from the domain */
  if (size < 0)
    return;

  if (memptr == NULL)
    {
      if (size)
	or_advance (buf, size);
    }
  else
    {
      mem = (char **) memptr;
      cur = *mem;
      /* should we be checking for existing strings ? */
#if 0
      if (cur != NULL)
	db_private_free_and_init (NULL, cur);
#endif

      new_ = NULL;
      if (size)
	{
	  start = buf->ptr;

	  /* KLUDGE, we have some knowledge of how the thing is stored here in
	   * order have some control over the conversion between the packed
	   * length prefix and the full word memory length prefix.
	   * Might want to put this in another specialized or_ function.
	   */

	  /* Get just the length prefix. */
	  len = or_get_varchar_length (buf, &rc);

	  /*
	   * Allocate storage for this string, including our own full word size
	   * prefix and a NULL terminator.
	   */
	  mem_length = len + sizeof (int) + 1;

	  new_ = db_private_alloc (NULL, mem_length);
	  if (new_ == NULL)
	    or_abort (buf);
	  else
	    {
	      /* store the length in our memory prefix */
	      *(int *) new_ = len;
	      cur = new_ + sizeof (int);

	      /* read the string, with the NULL terminator(which is expected) */
	      or_get_data (buf, cur, len + 1);
	      /* align like or_get_varchar */
	      or_get_align32 (buf);
	    }

	  /*
	   * If we were given a size, check to see if for some reason this is
	   * larger than the already word aligned string that we have now
	   * extracted.  This shouldn't be the case but since we've got a
	   * length, we may as well obey it.
	   */
	  pad = size - (int) (buf->ptr - start);
	  if (pad > 0)
	    or_advance (buf, pad);
	}
      *mem = new_;
    }
}

static void
mr_freemem_varnchar (void *memptr)
{
  char *cur;

  if (memptr != NULL)
    {
      cur = *(char **) memptr;
      if (cur != NULL)
	db_private_free_and_init (NULL, cur);
    }
}

static void
mr_initval_varnchar (DB_VALUE * value, int precision, int scale)
{
  db_make_varnchar (value, precision, NULL, 0, LANG_SYS_CODESET,
		    LANG_SYS_COLLATION);
  value->need_clear = false;
}

static int
mr_setval_varnchar (DB_VALUE * dest, const DB_VALUE * src, bool copy)
{
  int error = NO_ERROR;
  int src_precision, src_length;
  char *src_str, *new_;

  if (src == NULL || (DB_IS_NULL (src) && db_get_string (src) == 0))
    {
      error = db_value_domain_init (dest,
				    DB_TYPE_VARNCHAR, DB_DEFAULT_PRECISION,
				    0);
    }
  else if (DB_IS_NULL (src) || (src_str = db_get_string (src)) == NULL)
    {
      error = db_value_domain_init (dest,
				    DB_TYPE_VARNCHAR,
				    db_value_precision (src), 0);
    }
  else
    {
      /* Get information from the value. */
      src_precision = db_value_precision (src);
      src_length = db_get_string_size (src);
      if (src_length < 0)
	src_length = strlen (src_str);

      /* should we be paying attention to this? it is extremely dangerous */
      if (!copy)
	{
	  error = db_make_varnchar (dest, src_precision, src_str, src_length,
				    DB_GET_STRING_CODESET (src),
				    DB_GET_STRING_COLLATION (src));
	}
      else
	{
	  new_ = db_private_alloc (NULL, src_length + 1);
	  if (new_ == NULL)
	    {
	      db_value_domain_init (dest, DB_TYPE_VARNCHAR, src_precision, 0);
	      assert (er_errid () != NO_ERROR);
	      error = er_errid ();
	    }
	  else
	    {
	      memcpy (new_, src_str, src_length);
	      new_[src_length] = '\0';
	      db_make_varnchar (dest, src_precision, new_, src_length,
				DB_GET_STRING_CODESET (src),
				DB_GET_STRING_COLLATION (src));
	      dest->need_clear = true;
	    }
	}
    }

  return error;
}

static int
mr_index_lengthval_varnchar (DB_VALUE * value)
{
  return mr_lengthval_varnchar_internal (value, 1, CHAR_ALIGNMENT);
}

static int
mr_index_writeval_varnchar (OR_BUF * buf, DB_VALUE * value)
{
  return mr_writeval_varnchar_internal (buf, value, CHAR_ALIGNMENT);
}

static int
mr_index_readval_varnchar (OR_BUF * buf, DB_VALUE * value,
			   TP_DOMAIN * domain, int size, bool copy,
			   char *copy_buf, int copy_buf_len)
{
  return mr_readval_varnchar_internal (buf, value, domain, size, copy,
				       copy_buf, copy_buf_len,
				       CHAR_ALIGNMENT);
}

static int
mr_data_lengthval_varnchar (DB_VALUE * value, int disk)
{
  return mr_lengthval_varnchar_internal (value, disk, INT_ALIGNMENT);
}

static int
mr_data_writeval_varnchar (OR_BUF * buf, DB_VALUE * value)
{
  return mr_writeval_varnchar_internal (buf, value, INT_ALIGNMENT);
}

static int
mr_data_readval_varnchar (OR_BUF * buf, DB_VALUE * value,
			  TP_DOMAIN * domain, int size, bool copy,
			  char *copy_buf, int copy_buf_len)
{
  return mr_readval_varnchar_internal (buf, value, domain, size, copy,
				       copy_buf, copy_buf_len, INT_ALIGNMENT);
}

/*
 * Ignoring precision as byte size is really the only important thing for
 * varnchar.
 */
static int
mr_lengthval_varnchar_internal (DB_VALUE * value, int disk, int align)
{
  int src_length, len;
  const char *str;
#if !defined (SERVER_MODE)
  INTL_CODESET src_codeset;
#endif

  len = 0;
  if (value != NULL && (str = db_get_string (value)) != NULL)
    {
      src_length = db_get_string_size (value);	/* size in bytes */
      if (src_length < 0)
	src_length = strlen (str);

#if !defined (SERVER_MODE)
      src_codeset = (INTL_CODESET) db_get_string_codeset (value);

      /*
       * If this is a client side string, and the disk representation length
       * is requested,  Need to return the length of a converted string.
       */
      if (!db_on_server && src_length > 0 && disk
	  && DO_CONVERSION_TO_SRVR_STR (src_codeset))
	{
	  int unconverted;
	  int char_count = db_get_string_length (value);
	  char *converted_string = db_private_alloc (NULL,
						     STR_SIZE (char_count,
							       src_codeset));
	  intl_convert_charset ((unsigned char *) str, char_count,
				src_codeset,
				(unsigned char *) converted_string,
				lang_charset (), &unconverted);

	  if (converted_string)
	    {
	      intl_char_size ((unsigned char *) converted_string,
			      (char_count - unconverted), src_codeset, &len);
	      db_private_free_and_init (NULL, converted_string);
	    }
	}
#endif
      if (align == INT_ALIGNMENT)
	{
	  len = or_packed_varchar_length (src_length);
	}
      else
	{
	  len = or_varchar_length (src_length);
	}
    }

  return len;
}

static int
mr_writeval_varnchar_internal (OR_BUF * buf, DB_VALUE * value, int align)
{
  int src_size;
  INTL_CODESET src_codeset;
  char *str;
  int rc = NO_ERROR;

  if (value != NULL && (str = db_get_string (value)) != NULL)
    {
      src_size = db_get_string_size (value);	/* size in bytes */
      if (src_size < 0)
	src_size = strlen (str);

      src_codeset = (INTL_CODESET) db_get_string_codeset (value);
#if !defined (SERVER_MODE)
      if (!db_on_server
	  && src_size > 0 && DO_CONVERSION_TO_SRVR_STR (src_codeset))
	{
	  int unconverted;
	  int char_count = db_get_string_length (value);
	  char *converted_string = db_private_alloc (NULL,
						     STR_SIZE (char_count,
							       src_codeset));
	  (void) intl_convert_charset ((unsigned char *) str, char_count,
				       src_codeset,
				       (unsigned char *) converted_string,
				       lang_charset (), &unconverted);

	  if (converted_string)
	    {
	      intl_char_size ((unsigned char *) converted_string,
			      (char_count - unconverted),
			      src_codeset, &src_size);
	      if (align == INT_ALIGNMENT)
		{
		  or_packed_put_varchar (buf, converted_string, src_size);
		}
	      else
		{
		  or_put_varchar (buf, converted_string, src_size);
		}
	      db_private_free_and_init (NULL, converted_string);
	    }
	}
      else
	{
	  if (align == INT_ALIGNMENT)
	    {
	      rc = or_packed_put_varchar (buf, str, src_size);
	    }
	  else
	    {
	      rc = or_put_varchar (buf, str, src_size);
	    }
	}
#else /* SERVER_MODE */
      if (align == INT_ALIGNMENT)
	{
	  rc = or_packed_put_varchar (buf, str, src_size);
	}
      else
	{
	  rc = or_put_varchar (buf, str, src_size);
	}
#endif /* !SERVER_MODE */
    }
  return rc;
}

static int
mr_readval_varnchar_internal (OR_BUF * buf, DB_VALUE * value,
			      TP_DOMAIN * domain, int size, bool copy,
			      char *copy_buf, int copy_buf_len, int align)
{
  int pad, precision;
#if !defined (SERVER_MODE)
  INTL_CODESET codeset;
#endif
  char *new_, *start = NULL;
  int str_length;
  int rc = NO_ERROR;

  if (value == NULL)
    {
      if (size == -1)
	{
	  rc = or_skip_varchar (buf, align);
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
      if (domain != NULL)
	{
	  precision = domain->precision;
#if !defined (SERVER_MODE)
	  codeset = (INTL_CODESET) TP_DOMAIN_CODESET (domain);
#endif
	}
      else
	{
	  precision = DB_MAX_VARNCHAR_PRECISION;
#if !defined (SERVER_MODE)
	  codeset = LANG_SYS_CODESET;
#endif
	}

      /* Branch according to convention based on size */
      if (!copy)
	{
	  str_length = or_get_varchar_length (buf, &rc);
	  if (TP_DOMAIN_COLLATION_FLAG (domain) != TP_DOMAIN_COLL_NORMAL)
	    {
	      assert (false);
	      return ER_FAILED;
	    }
	  db_make_varnchar (value, precision, buf->ptr, str_length,
			    TP_DOMAIN_CODESET (domain),
			    TP_DOMAIN_COLLATION (domain));
	  value->need_clear = false;
	  or_skip_varchar_remainder (buf, str_length, align);
	}
      else
	{
	  if (size == 0)
	    {
	      /* its NULL */
	      db_value_domain_init (value, DB_TYPE_VARNCHAR, precision, 0);
	    }
	  else
	    {			/* size != 0 */
	      if (size == -1)
		{
		  /* Standard packed varnchar with a size prefix */
		  ;		/* do nothing */
		}
	      else
		{		/* size != -1 */
		  /* Standard packed varnchar within an area of fixed size,
		   * usually this means we're looking at the disk
		   * representation of an attribute.
		   * Just like the -1 case except we advance past the additional
		   * padding.
		   */
		  start = buf->ptr;
		}		/* size != -1 */

	      str_length = or_get_varchar_length (buf, &rc);
	      if (rc != NO_ERROR)
		{
		  return ER_FAILED;
		}

	      if (copy_buf && copy_buf_len >= str_length + 1)
		{
		  /* read buf image into the copy_buf */
		  new_ = copy_buf;
		}
	      else
		{
		  /*
		   * Allocate storage for the string including the kludge
		   * NULL terminator
		   */
		  new_ = (char *) db_private_alloc (NULL, str_length + 1);
		}

	      if (new_ == NULL)
		{
		  /* need to be able to return errors ! */
		  if (domain)
		    {
		      db_value_domain_init (value, TP_DOMAIN_TYPE (domain),
					    TP_FLOATING_PRECISION_VALUE, 0);
		    }
		  or_abort (buf);
		  return ER_FAILED;
		}
	      else
		{
		  if (align == INT_ALIGNMENT)
		    {
		      /* read the kludge NULL terminator */
		      rc = or_get_data (buf, new_, str_length + 1);
		      if (rc == NO_ERROR)
			{
			  /* round up to a word boundary */
			  rc = or_get_align32 (buf);
			}
		    }
		  else
		    {
		      rc = or_get_data (buf, new_, str_length);
		    }

		  if (rc != NO_ERROR)
		    {
		      if (new_ != copy_buf)
			{
			  db_private_free_and_init (NULL, new_);
			}
		      return ER_FAILED;
		    }

		  if (TP_DOMAIN_COLLATION_FLAG (domain)
		      != TP_DOMAIN_COLL_NORMAL)
		    {
		      assert (false);
		      return ER_FAILED;
		    }
		  db_make_varnchar (value, precision, new_, str_length,
				    TP_DOMAIN_CODESET (domain),
				    TP_DOMAIN_COLLATION (domain));
		  value->need_clear = (new_ != copy_buf) ? true : false;

		  if (size == -1)
		    {
		      /* Standard packed varnchar with a size prefix */
		      ;		/* do nothing */
		    }
		  else
		    {		/* size != -1 */
		      /* Standard packed varnchar within an area of fixed
		       * size, usually this means we're looking at the disk
		       * representation of an attribute.
		       * Just like the -1 case except we advance past the
		       * additional padding.
		       */
		      pad = size - (int) (buf->ptr - start);
		      if (pad > 0)
			{
			  rc = or_advance (buf, pad);
			}
		    }		/* size != -1 */
		}		/* else */
	    }			/* size != 0 */
	}

      /* Check if conversion needs to be done */
#if !defined (SERVER_MODE)
      if (!db_on_server
	  && DO_CONVERSION_TO_SQLTEXT (codeset) && !DB_IS_NULL (value))
	{
	  int unconverted;
	  int char_count;
	  char *temp_string = db_get_nchar (value, &char_count);
	  if (char_count > 0)
	    {
	      new_ = (char *)
		db_private_alloc (NULL, STR_SIZE (char_count, codeset));
	      (void) intl_convert_charset ((unsigned char *) temp_string,
					   char_count, codeset,
					   (unsigned char *) new_,
					   LANG_SYS_CODESET, &unconverted);
	      db_value_clear (value);
	      if (TP_DOMAIN_COLLATION_FLAG (domain) != TP_DOMAIN_COLL_NORMAL)
		{
		  assert (false);
		  return ER_FAILED;
		}
	      db_make_varnchar (value, precision, new_,
				STR_SIZE (char_count, codeset),
				codeset, TP_DOMAIN_COLLATION (domain));
	      value->need_clear = true;
	    }
	}
#endif /* !SERVER_MODE */
    }
  return rc;
}

static int
mr_index_cmpdisk_varnchar (void *mem1, void *mem2, TP_DOMAIN * domain,
			   int do_coercion, int total_order, int *start_colp)
{
  assert (domain != NULL);

  return mr_data_cmpdisk_varnchar (mem1, mem2, domain,
				   do_coercion, total_order, start_colp);
}

static int
mr_data_cmpdisk_varnchar (void *mem1, void *mem2, TP_DOMAIN * domain,
			  int do_coercion, int total_order, int *start_colp)
{
  int c = DB_UNK;
  char *str1, *str2;
  int str_length1, str_length2;
  OR_BUF buf1, buf2;
  int rc = NO_ERROR;

  assert (domain != NULL);

  str1 = (char *) mem1;
  str2 = (char *) mem2;

  /* generally, data is short enough
   */
  str_length1 = OR_GET_BYTE (str1);
  str_length2 = OR_GET_BYTE (str2);
  if (str_length1 < 0xFF && str_length2 < 0xFF)
    {
      str1 += OR_BYTE_SIZE;
      str2 += OR_BYTE_SIZE;
      c = QSTR_NCHAR_COMPARE (domain->collation_id,
			      (unsigned char *) str1, str_length1,
			      (unsigned char *) str2, str_length2,
			      (INTL_CODESET) TP_DOMAIN_CODESET (domain));
      c = MR_CMP_RETURN_CODE (c);
      return c;
    }

  assert (str_length1 == 0xFF || str_length2 == 0xFF);

  or_init (&buf1, str1, 0);
  str_length1 = or_get_varchar_length (&buf1, &rc);
  if (rc == NO_ERROR)
    {
      or_init (&buf2, str2, 0);
      str_length2 = or_get_varchar_length (&buf2, &rc);
      if (rc == NO_ERROR)
	{

	  c = QSTR_NCHAR_COMPARE (domain->collation_id,
				  (unsigned char *) buf1.ptr, str_length1,
				  (unsigned char *) buf2.ptr, str_length2,
				  (INTL_CODESET) TP_DOMAIN_CODESET (domain));
	  c = MR_CMP_RETURN_CODE (c);
	  return c;
	}
    }

  return DB_UNK;
}

static int
mr_cmpval_varnchar (DB_VALUE * value1, DB_VALUE * value2,
		    int do_coercion, int total_order, int *start_colp,
		    int collation)
{
  int c;
  unsigned char *string1, *string2;

  string1 = (unsigned char *) DB_GET_STRING (value1);
  string2 = (unsigned char *) DB_GET_STRING (value2);

  if (string1 == NULL || string2 == NULL
      || DB_GET_STRING_CODESET (value1) != DB_GET_STRING_CODESET (value1))
    {
      return DB_UNK;
    }

  if (collation == -1)
    {
      assert (false);
      return DB_UNK;
    }

  c = QSTR_NCHAR_COMPARE (collation,
			  string1, (int) DB_GET_STRING_SIZE (value1),
			  string2, (int) DB_GET_STRING_SIZE (value2),
			  (INTL_CODESET) DB_GET_STRING_CODESET (value2));
  c = MR_CMP_RETURN_CODE (c);

  return c;
}

#if defined (ENABLE_UNUSED_FUNCTION)
static int
mr_cmpval_varnchar2 (DB_VALUE * value1, DB_VALUE * value2,
		     int length,
		     int do_coercion, int total_order, int *start_colp)
{
  int c;
  unsigned char *string1, *string2;
  int len1, len2, string_size;

  string1 = (unsigned char *) DB_GET_STRING (value1);
  string2 = (unsigned char *) DB_GET_STRING (value2);

  if (string1 == NULL || string2 == NULL)
    {
      return DB_UNK;
    }

  string_size = (int) DB_GET_STRING_SIZE (value1);
  len1 = MIN (string_size, length);
  string_size = (int) DB_GET_STRING_SIZE (value2);
  len2 = MIN (string_size, length);

  c = varnchar_compare (string1, len1, string2, len2,
			(INTL_CODESET) DB_GET_STRING_CODESET (value2));
  c = MR_CMP_RETURN_CODE (c);

  return c;
}
#endif

PR_TYPE tp_VarNChar = {
  "national character varying", DB_TYPE_VARNCHAR, 1, sizeof (const char *), 0,
  1,
  help_fprint_value,
  help_sprint_value,
  mr_initmem_varnchar,
  mr_initval_varnchar,
  mr_setmem_varnchar,
  mr_getmem_varnchar,
  mr_setval_varnchar,
  mr_data_lengthmem_varnchar,
  mr_data_lengthval_varnchar,
  mr_data_writemem_varnchar,
  mr_data_readmem_varnchar,
  mr_data_writeval_varnchar,
  mr_data_readval_varnchar,
  mr_index_lengthmem_varnchar,
  mr_index_lengthval_varnchar,
  mr_index_writeval_varnchar,
  mr_index_readval_varnchar,
  mr_index_cmpdisk_varnchar,
  mr_freemem_varnchar,
  mr_data_cmpdisk_varnchar,
  mr_cmpval_varnchar
};

PR_TYPE *tp_Type_varnchar = &tp_VarNChar;

/*
 * TYPE BIT
 */

static void
mr_initmem_bit (void *memptr, TP_DOMAIN * domain)
{
#if !defined(NDEBUG)
  int mem_length;

  assert (!IS_FLOATING_PRECISION (domain->precision));
  assert (TP_DOMAIN_CODESET (domain) == INTL_CODESET_RAW_BITS);

  mem_length = STR_SIZE (domain->precision, TP_DOMAIN_CODESET (domain));
  memset (memptr, 0, mem_length);
#endif
}


/*
 * Due to the "within tolerance" comparison of domains used for
 * assignment validation, we may get values in here whose precision is
 * less than the precision of the actual attribute.   This prevents
 * having to copy a string just to coerce a value into a larger precision.
 * Note that the precision of the source value may also come in as -1 here
 * which is used to mean a "floating" precision that is assumed to be
 * compatible with the destination domain as long as the associated value
 * is within tolerance.  This case is generally only seen for string
 * literals that have been produced by the parser.  These literals will
 * not contain blank padding and strlen() or db_get_string_size() can be
 * used to determine the number of significant characters.
 */
static int
mr_setmem_bit (void *memptr, TP_DOMAIN * domain, DB_VALUE * value)
{
  int error = NO_ERROR;
  char *src, *mem;
  int src_precision, src_length, mem_length, pad;

  if (value == NULL)
    return NO_ERROR;

  /* Get information from the value */
  src = db_get_string (value);
  src_precision = db_value_precision (value);
  src_length = db_get_string_size (value);	/* size in bytes */

  if (src == NULL)
    {
      return NO_ERROR;
    }

  /*
   * The only thing we really care about at this point, is the byte
   * length of the string.  The precision could be checked here but it
   * really isn't necessary for this operation.
   * Calculate the maximum number of bytes we have available here.
   */
  assert (TP_DOMAIN_CODESET (domain) == INTL_CODESET_RAW_BITS);
  mem_length = STR_SIZE (domain->precision, TP_DOMAIN_CODESET (domain));

  if (mem_length < src_length)
    {
      /*
       * should never get here, this is supposed to be caught during domain
       * validation, need a better error message.
       */
      error = ER_OBJ_DOMAIN_CONFLICT;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, "");
    }
  else
    {
      /* copy the value into memory */
      mem = (char *) memptr;
      memcpy (mem, src, src_length);

      /* Check for padding */
      pad = mem_length - src_length;
      if (pad)
	{
	  int i;
	  for (i = src_length; i < mem_length; i++)
	    mem[i] = '\0';
	}
    }
  return error;
}

static int
mr_getmem_bit (void *mem, TP_DOMAIN * domain, DB_VALUE * value, bool copy)
{
  int mem_length;
  char *new_;

  assert (TP_DOMAIN_CODESET (domain) == INTL_CODESET_RAW_BITS);
  mem_length = STR_SIZE (domain->precision, TP_DOMAIN_CODESET (domain));

  if (!copy)
    new_ = (char *) mem;
  else
    {
      new_ = db_private_alloc (NULL, mem_length + 1);
      if (new_ == NULL)
	{
	  assert (er_errid () != NO_ERROR);
	  return er_errid ();
	}
      memcpy (new_, (char *) mem, mem_length);
    }

  db_make_bit (value, domain->precision, new_, domain->precision);
  if (copy)
    value->need_clear = true;

  return NO_ERROR;
}

static int
mr_data_lengthmem_bit (void *memptr, TP_DOMAIN * domain, int disk)
{
  assert (!IS_FLOATING_PRECISION (domain->precision));
  assert (TP_DOMAIN_CODESET (domain) == INTL_CODESET_RAW_BITS);

  /* There is no difference between the disk & memory sizes. */
  return STR_SIZE (domain->precision, TP_DOMAIN_CODESET (domain));
}

static void
mr_data_writemem_bit (OR_BUF * buf, void *mem, TP_DOMAIN * domain)
{
  int mem_length;

  assert (TP_DOMAIN_CODESET (domain) == INTL_CODESET_RAW_BITS);
  mem_length = STR_SIZE (domain->precision, TP_DOMAIN_CODESET (domain));

  /*
   * We simply dump the memory image to disk, it will already have been padded.
   * If this were a national character string, at this point, we'd have to
   * decide now to perform a character set conversion.
   */
  or_put_data (buf, (char *) mem, mem_length);
}

static void
mr_data_readmem_bit (OR_BUF * buf, void *mem, TP_DOMAIN * domain, int size)
{
  int mem_length, padding;

  if (mem == NULL)
    {
      /* If we passed in a size, then use it.  Otherwise, determine the
         size from the domain. */
      if (size > 0)
	{
	  or_advance (buf, size);
	}
      else
	{
	  assert (TP_DOMAIN_CODESET (domain) == INTL_CODESET_RAW_BITS);
	  mem_length = STR_SIZE (domain->precision,
				 TP_DOMAIN_CODESET (domain));
	  or_advance (buf, mem_length);
	}
    }
  else
    {
      assert (TP_DOMAIN_CODESET (domain) == INTL_CODESET_RAW_BITS);
      mem_length = STR_SIZE (domain->precision, TP_DOMAIN_CODESET (domain));
      if (size != -1 && mem_length > size)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SM_CORRUPTED, 0);
	  or_abort (buf);
	}
      or_get_data (buf, (char *) mem, mem_length);

      /*
       * We should only see padding if the string is contained within a packed
       * value that had extra padding to ensure alignment.  If we see these,
       * just pop them out of the buffer.  This shouldn't ever happen for the
       * "getmem" function, only for the "getval" function.
       */
      if (size != -1)
	{
	  padding = size - mem_length;
	  if (padding > 0)
	    or_advance (buf, padding);
	}
    }
}

static void
mr_freemem_bit (void *memptr)
{
}

static void
mr_initval_bit (DB_VALUE * value, int precision, int scale)
{
  db_value_domain_init (value, DB_TYPE_BIT, precision, scale);
}

static int
mr_setval_bit (DB_VALUE * dest, const DB_VALUE * src, bool copy)
{
  int error = NO_ERROR;
  int src_precision, src_length, src_number_of_bits = 0;
  char *src_string, *new_;

  if (src == NULL || (DB_IS_NULL (src) && db_value_precision (src) == 0))
    db_value_domain_init (dest, DB_TYPE_BIT, TP_FLOATING_PRECISION_VALUE, 0);
  else if (DB_IS_NULL (src))
    db_value_domain_init (dest, DB_TYPE_BIT, db_value_precision (src), 0);
  else
    {
      /* Get information from the value */
      src_string = db_get_bit (src, &src_number_of_bits);
      src_precision = db_value_precision (src);
      src_length = db_get_string_size (src);	/* size in bytes */

      db_value_domain_init (dest, DB_TYPE_BIT, src_precision, 0);

      /* shouldn't see a NULL string at this point, treat as NULL */
      if (src_string != NULL)
	{
	  if (!copy)
	    {
	      db_make_bit (dest, src_precision, src_string,
			   src_number_of_bits);
	    }
	  else
	    {

	      /* make sure the copy gets a NULL terminator */
	      new_ = db_private_alloc (NULL, src_length + 1);
	      if (new_ == NULL)
		{
		  assert (er_errid () != NO_ERROR);
		  error = er_errid ();
		}
	      else
		{
		  memcpy (new_, src_string, src_length);
		  db_make_bit (dest, src_precision, new_, src_number_of_bits);
		  dest->need_clear = true;
		}
	    }
	}
    }
  return error;
}

static int
mr_index_lengthmem_bit (void *memptr, TP_DOMAIN * domain)
{
  int mem_length;

  assert (!IS_FLOATING_PRECISION (domain->precision) || memptr != NULL);
  assert (TP_DOMAIN_CODESET (domain) == INTL_CODESET_RAW_BITS);

  if (!IS_FLOATING_PRECISION (domain->precision))
    {
      mem_length = domain->precision;

      return STR_SIZE (mem_length, TP_DOMAIN_CODESET (domain));
    }
  else
    {
      memcpy (&mem_length, memptr, OR_INT_SIZE);

      return STR_SIZE (mem_length, TP_DOMAIN_CODESET (domain)) + OR_INT_SIZE;
    }

}

static int
mr_index_lengthval_bit (DB_VALUE * value)
{
  return mr_data_lengthval_bit (value, 1);
}

static int
mr_data_lengthval_bit (DB_VALUE * value, int disk)
{
  int packed_length, src_precision;
  char *src;

  src = db_get_string (value);
  if (src == NULL)
    {
      return 0;
    }

  src_precision = db_value_precision (value);
  if (!IS_FLOATING_PRECISION (src_precision))
    {
      assert (db_get_string_codeset (value) == INTL_CODESET_RAW_BITS);
      packed_length = STR_SIZE (src_precision, db_get_string_codeset (value));
    }
  else
    {
      /*
       * Precision is "floating", calculate the effective precision based on the
       * string length.
       */
      packed_length = db_get_string_size (value);

      /* add in storage for a size prefix on the packed value. */
      packed_length += OR_INT_SIZE;
    }

  /*
   * NOTE: We do NOT perform padding here, if this is used in the context
   * of a packed value, the or_put_value() family of functions must handle
   * their own padding, this is because "lengthval" and "writeval" can be
   * used to place values into the disk representation of instances and
   * there can be no padding in there.
   */

  return packed_length;
}


static int
mr_index_writeval_bit (OR_BUF * buf, DB_VALUE * value)
{
  return mr_writeval_bit_internal (buf, value, CHAR_ALIGNMENT);
}

/*
 * See commentary in mr_data_lengthval_bit.
 */
static int
mr_data_writeval_bit (OR_BUF * buf, DB_VALUE * value)
{
  return mr_writeval_bit_internal (buf, value, INT_ALIGNMENT);
}

static int
mr_writeval_bit_internal (OR_BUF * buf, DB_VALUE * value, int align)
{
  int src_precision, src_length, packed_length, pad;
  char *src;
  int rc = NO_ERROR;

  src = db_get_string (value);
  if (src == NULL)
    {
      return rc;
    }

  src_precision = db_value_precision (value);
  src_length = db_get_string_size (value);	/* size in bytes */

  if (!IS_FLOATING_PRECISION (src_precision))
    {
      assert (db_get_string_codeset (value) == INTL_CODESET_RAW_BITS);
      packed_length = STR_SIZE (src_precision, db_get_string_codeset (value));

      if (packed_length < src_length)
	{
	  /* should have caught this by now, truncate silently */
	  or_put_data (buf, src, packed_length);
	}
      else
	{
	  rc = or_put_data (buf, src, src_length);
	  if (rc == NO_ERROR)
	    {
	      /* Check for padding */
	      pad = packed_length - src_length;
	      if (pad)
		{
		  int i;
		  for (i = src_length; i < packed_length; i++)
		    {
		      rc = or_put_byte (buf, (int) '\0');
		    }
		}
	    }
	}
    }
  else
    {
      /*
       * This is a "floating" precision value. Pack what we can based on the
       * string size.  Note that for this to work, this can only be packed
       * as part of a domain tagged value and we must include a length
       * prefix after the domain.
       */
      packed_length = db_get_string_length (value);

      /* store the size prefix */
      if (align == INT_ALIGNMENT)
	{
	  rc = or_put_int (buf, packed_length);
	}
      else
	{
	  rc = or_put_data (buf, (char *) (&packed_length), OR_INT_SIZE);
	}
      if (rc == NO_ERROR)
	{
	  /* store the data */
	  rc = or_put_data (buf, src, BITS_TO_BYTES (packed_length));
	  /* there is no blank padding in this case */
	}
    }

  return rc;
}

static int
mr_index_readval_bit (OR_BUF * buf, DB_VALUE * value,
		      TP_DOMAIN * domain, int disk_size, bool copy,
		      char *copy_buf, int copy_buf_len)
{
  return mr_readval_bit_internal (buf, value, domain, disk_size, copy,
				  copy_buf, copy_buf_len, CHAR_ALIGNMENT);
}

static int
mr_data_readval_bit (OR_BUF * buf, DB_VALUE * value,
		     TP_DOMAIN * domain, int disk_size, bool copy,
		     char *copy_buf, int copy_buf_len)
{
  return mr_readval_bit_internal (buf, value, domain, disk_size, copy,
				  copy_buf, copy_buf_len, INT_ALIGNMENT);
}

static int
mr_readval_bit_internal (OR_BUF * buf, DB_VALUE * value,
			 TP_DOMAIN * domain, int disk_size, bool copy,
			 char *copy_buf, int copy_buf_len, int align)
{
  int mem_length, padding;
  int bit_length;
  char *new_;
  int rc = NO_ERROR;

  if (IS_FLOATING_PRECISION (domain->precision))
    {
      if (align == INT_ALIGNMENT)
	{
	  bit_length = or_get_int (buf, &rc);
	}
      else
	{
	  rc = or_get_data (buf, (char *) (&bit_length), OR_INT_SIZE);
	}
      if (rc != NO_ERROR)
	{
	  return ER_FAILED;
	}

      mem_length = BITS_TO_BYTES (bit_length);
      if (value == NULL)
	{
	  rc = or_advance (buf, mem_length);
	}
      else if (!copy)
	{
	  db_make_bit (value, TP_FLOATING_PRECISION_VALUE, buf->ptr,
		       bit_length);
	  value->need_clear = false;
	  rc = or_advance (buf, mem_length);
	}
      else
	{
	  if (copy_buf && copy_buf_len >= mem_length + 1)
	    {
	      /* read buf image into the copy_buf */
	      new_ = copy_buf;
	    }
	  else
	    {
	      /*
	       * Allocate storage for the string including the kludge NULL
	       * terminator
	       */
	      new_ = db_private_alloc (NULL, mem_length + 1);
	    }

	  if (new_ == NULL)
	    {
	      /* need to be able to return errors ! */
	      db_value_domain_init (value, TP_DOMAIN_TYPE (domain),
				    TP_FLOATING_PRECISION_VALUE, 0);
	      or_abort (buf);

	      return ER_FAILED;
	    }
	  else
	    {
	      if ((rc = or_get_data (buf, new_, mem_length)) != NO_ERROR)
		{
		  if (new_ != copy_buf)
		    {
		      db_private_free_and_init (NULL, new_);
		    }

		  return rc;
		}
	      new_[mem_length] = '\0';	/* append the kludge NULL terminator */
	      db_make_bit (value, TP_FLOATING_PRECISION_VALUE, new_,
			   bit_length);
	      value->need_clear = (new_ != copy_buf) ? true : false;
	    }
	}
    }
  else
    {
      assert (TP_DOMAIN_CODESET (domain) == INTL_CODESET_RAW_BITS);
      mem_length = STR_SIZE (domain->precision, TP_DOMAIN_CODESET (domain));

      if (disk_size != -1 && mem_length > disk_size)
	{
	  /*
	   * If we're low here, we could just read what we have and make a
	   * smaller value.  Still the domain should match at this point.
	   */
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SM_CORRUPTED, 0);
	  or_abort (buf);

	  return ER_FAILED;
	}

      if (value == NULL)
	{
	  rc = or_advance (buf, mem_length);
	}
      else if (!copy)
	{
	  db_make_bit (value, domain->precision, buf->ptr, domain->precision);
	  value->need_clear = false;
	  rc = or_advance (buf, mem_length);
	}
      else
	{
	  if (copy_buf && copy_buf_len >= mem_length + 1)
	    {
	      /* read buf image into the copy_buf */
	      new_ = copy_buf;
	    }
	  else
	    {
	      /*
	       * Allocate storage for the string including the kludge NULL
	       * terminator
	       */
	      new_ = db_private_alloc (NULL, mem_length + 1);
	    }

	  if (new_ == NULL)
	    {
	      /* need to be able to return errors ! */
	      db_value_domain_init (value, TP_DOMAIN_TYPE (domain),
				    domain->precision, 0);
	      or_abort (buf);

	      return ER_FAILED;
	    }
	  else
	    {
	      if ((rc = or_get_data (buf, new_, mem_length)) != NO_ERROR)
		{
		  if (new_ != copy_buf)
		    {
		      db_private_free_and_init (NULL, new_);
		    }

		  return rc;
		}
	      new_[mem_length] = '\0';	/* append the kludge NULL terminator */
	      db_make_bit (value, domain->precision, new_, domain->precision);
	      value->need_clear = (new_ != copy_buf) ? true : false;
	    }
	}
      if (rc == NO_ERROR)
	{
	  /*
	   * We should only see padding if the string is contained within a
	   * packed value that had extra padding to ensure alignment.  If we see
	   * these, just pop them out of the buffer.
	   */
	  if (disk_size != -1)
	    {
	      padding = disk_size - mem_length;
	      if (padding > 0)
		{
		  rc = or_advance (buf, padding);
		}
	    }
	}
    }

  return rc;
}

static int
mr_index_cmpdisk_bit (void *mem1, void *mem2,
		      TP_DOMAIN * domain,
		      int do_coercion, int total_order, int *start_colp)
{
  assert (domain != NULL);

  return mr_cmpdisk_bit_internal (mem1, mem2, domain, do_coercion,
				  total_order, start_colp, CHAR_ALIGNMENT);
}

static int
mr_data_cmpdisk_bit (void *mem1, void *mem2, TP_DOMAIN * domain,
		     int do_coercion, int total_order, int *start_colp)
{
  assert (domain != NULL);

  return mr_cmpdisk_bit_internal (mem1, mem2, domain, do_coercion,
				  total_order, start_colp, INT_ALIGNMENT);
}

static int
mr_cmpdisk_bit_internal (void *mem1, void *mem2, TP_DOMAIN * domain,
			 int do_coercion, int total_order, int *start_colp,
			 int align)
{
  int bit_length1, mem_length1, bit_length2, mem_length2, c;

  if (IS_FLOATING_PRECISION (domain->precision))
    {
      if (align == INT_ALIGNMENT)
	{
	  bit_length1 = OR_GET_INT (mem1);
	  bit_length2 = OR_GET_INT (mem2);
	}
      else
	{
	  memcpy (&bit_length1, mem1, OR_INT_SIZE);
	  memcpy (&bit_length2, mem2, OR_INT_SIZE);
	}
      mem1 = (char *) mem1 + OR_INT_SIZE;
      mem_length1 = BITS_TO_BYTES (bit_length1);
      mem2 = (char *) mem2 + OR_INT_SIZE;
      mem_length2 = BITS_TO_BYTES (bit_length2);
    }
  else
    {
      assert (TP_DOMAIN_CODESET (domain) == INTL_CODESET_RAW_BITS);
      mem_length1 = mem_length2 = STR_SIZE (domain->precision,
					    TP_DOMAIN_CODESET (domain));
    }

  c = bit_compare ((unsigned char *) mem1, mem_length1,
		   (unsigned char *) mem2, mem_length2);
  c = MR_CMP_RETURN_CODE (c);

  return c;
}

static int
mr_cmpval_bit (DB_VALUE * value1, DB_VALUE * value2,
	       int do_coercion, int total_order, int *start_colp,
	       int collation)
{
  int c;
  unsigned char *string1, *string2;

  string1 = (unsigned char *) DB_GET_STRING (value1);
  string2 = (unsigned char *) DB_GET_STRING (value2);

  if (string1 == NULL || string2 == NULL)
    {
      return DB_UNK;
    }

  c = bit_compare (string1, (int) DB_GET_STRING_SIZE (value1),
		   string2, (int) DB_GET_STRING_SIZE (value2));
  c = MR_CMP_RETURN_CODE (c);

  return c;
}

static int
mr_cmpval_bit2 (DB_VALUE * value1, DB_VALUE * value2, int length,
		int do_coercion, int total_order, int *start_colp)
{
  int c;
  unsigned char *string1, *string2;
  int len1, len2, string_size;

  string1 = (unsigned char *) DB_GET_STRING (value1);
  string2 = (unsigned char *) DB_GET_STRING (value2);

  if (string1 == NULL || string2 == NULL)
    {
      return DB_UNK;
    }

  string_size = (int) DB_GET_STRING_SIZE (value1);
  len1 = MIN (string_size, length);
  string_size = (int) DB_GET_STRING_SIZE (value2);
  len2 = MIN (string_size, length);

  c = bit_compare (string1, len1, string2, len2);
  c = MR_CMP_RETURN_CODE (c);

  return c;
}


PR_TYPE tp_Bit = {
  "bit", DB_TYPE_BIT, 0, 0, 0, 1,
  help_fprint_value,
  help_sprint_value,
  mr_initmem_bit,
  mr_initval_bit,
  mr_setmem_bit,
  mr_getmem_bit,
  mr_setval_bit,
  mr_data_lengthmem_bit,
  mr_data_lengthval_bit,
  mr_data_writemem_bit,
  mr_data_readmem_bit,
  mr_data_writeval_bit,
  mr_data_readval_bit,
  mr_index_lengthmem_bit,
  mr_index_lengthval_bit,
  mr_index_writeval_bit,
  mr_index_readval_bit,
  mr_index_cmpdisk_bit,
  mr_freemem_bit,
  mr_data_cmpdisk_bit,
  mr_cmpval_bit
};

PR_TYPE *tp_Type_bit = &tp_Bit;

/*
 * TYPE VARBIT
 */

static void
mr_initmem_varbit (void *mem, TP_DOMAIN * domain)
{
  *(char **) mem = NULL;
}


/*
 * The main difference between "memory" strings and "value" strings is that
 * the length tag is stored as an in-line prefix in the memory block allocated
 * to hold the string characters.
 */
static int
mr_setmem_varbit (void *memptr, TP_DOMAIN * domain, DB_VALUE * value)
{
  int error = NO_ERROR;
  char *src, *cur, *new_, **mem;
  int src_precision, src_length, src_length_bits, new_length;

  /* get the current memory contents */
  mem = (char **) memptr;
  cur = *mem;

  if (value == NULL || (src = db_get_string (value)) == NULL)
    {
      /* remove the current value */
      if (cur != NULL)
	{
	  db_private_free_and_init (NULL, cur);
	  mr_initmem_varbit (memptr, domain);
	}
    }
  else
    {
      /*
       * Get information from the value.  Ignore precision for the time being
       * since we really only care about the byte size of the value for varbit.
       * Whether or not the value "fits" should have been checked by now.
       */
      src_precision = db_value_precision (value);
      src_length = db_get_string_size (value);	/* size in bytes */
      src_length_bits = db_get_string_length (value);	/* size in bits */

      new_length = src_length + sizeof (int);
      new_ = db_private_alloc (NULL, new_length);
      if (new_ == NULL)
	{
	  assert (er_errid () != NO_ERROR);
	  error = er_errid ();
	}
      else
	{
	  if (cur != NULL)
	    db_private_free_and_init (NULL, cur);

	  /* pack in the length prefix */
	  *(int *) new_ = src_length_bits;
	  cur = new_ + sizeof (int);
	  /* store the string */
	  memcpy (cur, src, src_length);
	  *mem = new_;
	}
    }

  return error;
}

static int
mr_getmem_varbit (void *memptr, TP_DOMAIN * domain,
		  DB_VALUE * value, bool copy)
{
  int error = NO_ERROR;
  int mem_bit_length;
  char **mem, *cur, *new_;

  /* get to the current value */
  mem = (char **) memptr;
  cur = *mem;

  if (cur == NULL)
    {
      db_value_domain_init (value, DB_TYPE_VARBIT, domain->precision, 0);
      value->need_clear = false;
    }
  else
    {
      /* extract the length prefix and the pointer to the actual string data */
      mem_bit_length = *(int *) cur;
      cur += sizeof (int);

      if (!copy)
	{
	  db_make_varbit (value, domain->precision, cur, mem_bit_length);
	  value->need_clear = false;
	}
      else
	{
	  /* return it */
	  new_ = (char *)
	    db_private_alloc (NULL, BITS_TO_BYTES (mem_bit_length) + 1);
	  if (new_ == NULL)
	    {
	      assert (er_errid () != NO_ERROR);
	      error = er_errid ();
	    }
	  else
	    {
	      memcpy (new_, cur, BITS_TO_BYTES (mem_bit_length));
	      db_make_varbit (value, domain->precision, new_, mem_bit_length);
	      value->need_clear = true;
	    }
	}
    }
  return error;
}


/*
 * For the disk representation, we may be adding pad bytes
 * to round up to a word boundary.
 */
static int
mr_data_lengthmem_varbit (void *memptr, TP_DOMAIN * domain, int disk)
{
  char **mem, *cur;
  int len;

  len = 0;
  if (!disk)
    len = tp_VarBit.size;
  else if (memptr != NULL)
    {
      mem = (char **) memptr;
      cur = *mem;
      if (cur != NULL)
	{
	  len = *(int *) cur;
	  len = or_packed_varbit_length (len);
	}
    }

  return len;
}

static int
mr_index_lengthmem_varbit (void *memptr, TP_DOMAIN * domain)
{
  OR_BUF buf;
  int bitlen;
  int rc = NO_ERROR;

  or_init (&buf, memptr, -1);

  bitlen = or_get_varbit_length (&buf, &rc);

  return or_varbit_length (bitlen);
}

static void
mr_data_writemem_varbit (OR_BUF * buf, void *memptr, TP_DOMAIN * domain)
{
  char **mem, *cur;
  int bitlen;

  mem = (char **) memptr;
  cur = *mem;
  if (cur != NULL)
    {
      bitlen = *(int *) cur;
      cur += sizeof (int);
      or_packed_put_varbit (buf, cur, bitlen);
    }
}


/*
 * The amount of memory requested is currently calculated based on the
 * stored size prefix.  If we ever go to a system where we avoid storing the
 * size, then we could use the size argument passed in to this function but
 * that may also include any padding byte that added to bring us up to a word
 * boundary. Might want some way to determine which bytes at the end of a
 * string are padding.
 */
static void
mr_data_readmem_varbit (OR_BUF * buf, void *memptr, TP_DOMAIN * domain,
			int size)
{
  char **mem, *cur, *new_;
  int bit_len;
  int mem_length, pad;
  char *start;
  int rc = NO_ERROR;

  /* Must have an explicit size here - can't be determined from the domain */
  if (size < 0)
    {
      return;
    }

  if (memptr == NULL)
    {
      if (size)
	{
	  or_advance (buf, size);
	}
    }
  else
    {
      mem = (char **) memptr;
      cur = *mem;
      /* should we be checking for existing strings ? */
#if 0
      if (cur != NULL)
	db_private_free_and_init (NULL, cur);
#endif

      new_ = NULL;
      if (size)
	{
	  start = buf->ptr;

	  /* KLUDGE, we have some knowledge of how the thing is stored here in
	   * order have some control over the conversion between the packed
	   * length prefix and the full word memory length prefix.
	   * Might want to put this in another specialized or_ function.
	   */

	  /* Get just the length prefix. */
	  bit_len = or_get_varbit_length (buf, &rc);

	  /*
	   * Allocate storage for this string, including our own full word size
	   * prefix.
	   */
	  mem_length = BITS_TO_BYTES (bit_len) + sizeof (int);

	  new_ = db_private_alloc (NULL, mem_length);
	  if (new_ == NULL)
	    {
	      or_abort (buf);
	    }
	  else
	    {
	      /* store the length in our memory prefix */
	      *(int *) new_ = bit_len;
	      cur = new_ + sizeof (int);

	      /* read the string */
	      or_get_data (buf, cur, BITS_TO_BYTES (bit_len));
	      /* align like or_get_varchar */
	      or_get_align32 (buf);
	    }

	  /*
	   * If we were given a size, check to see if for some reason this is
	   * larger than the already word aligned string that we have now
	   * extracted.  This shouldn't be the case but since we've got a
	   * length, we may as well obey it.
	   */
	  pad = size - (int) (buf->ptr - start);
	  if (pad > 0)
	    {
	      or_advance (buf, pad);
	    }
	}
      *mem = new_;
    }
}

static void
mr_freemem_varbit (void *memptr)
{
  char *cur;

  if (memptr != NULL)
    {
      cur = *(char **) memptr;
      if (cur != NULL)
	{
	  db_private_free_and_init (NULL, cur);
	}
    }
}

static void
mr_initval_varbit (DB_VALUE * value, int precision, int scale)
{
  db_make_varbit (value, precision, NULL, 0);
  value->need_clear = false;
}

static int
mr_setval_varbit (DB_VALUE * dest, const DB_VALUE * src, bool copy)
{
  int error = NO_ERROR;
  int src_precision, src_length, src_bit_length;
  char *src_str, *new_;

  if (src == NULL || (DB_IS_NULL (src) && db_value_precision (src) == 0))
    {
      error = db_value_domain_init (dest,
				    DB_TYPE_VARBIT, DB_DEFAULT_PRECISION, 0);
    }
  else if (DB_IS_NULL (src) || (src_str = db_get_string (src)) == NULL)
    {
      error = db_value_domain_init (dest,
				    DB_TYPE_VARBIT, db_value_precision (src),
				    0);
    }
  else
    {
      /* Get information from the value. */
      src_precision = db_value_precision (src);
      src_length = db_get_string_size (src);
      src_bit_length = db_get_string_length (src);

      /* should we be paying attention to this? it is extremely dangerous */
      if (!copy)
	{
	  error =
	    db_make_varbit (dest, src_precision, src_str, src_bit_length);
	}
      else
	{
	  new_ = db_private_alloc (NULL, src_length + 1);
	  if (new_ == NULL)
	    {
	      db_value_domain_init (dest, DB_TYPE_VARBIT, src_precision, 0);
	      assert (er_errid () != NO_ERROR);
	      error = er_errid ();
	    }
	  else
	    {
	      memcpy (new_, src_str, src_length);
	      db_make_varbit (dest, src_precision, new_, src_bit_length);
	      dest->need_clear = true;
	    }
	}
    }

  return error;
}

static int
mr_index_lengthval_varbit (DB_VALUE * value)
{
  return mr_lengthval_varbit_internal (value, 1, CHAR_ALIGNMENT);
}

static int
mr_index_writeval_varbit (OR_BUF * buf, DB_VALUE * value)
{
  return mr_writeval_varbit_internal (buf, value, CHAR_ALIGNMENT);
}

static int
mr_index_readval_varbit (OR_BUF * buf, DB_VALUE * value,
			 TP_DOMAIN * domain, int size, bool copy,
			 char *copy_buf, int copy_buf_len)
{
  return mr_readval_varbit_internal (buf, value, domain, size, copy,
				     copy_buf, copy_buf_len, CHAR_ALIGNMENT);
}

static int
mr_data_lengthval_varbit (DB_VALUE * value, int disk)
{
  return mr_lengthval_varbit_internal (value, disk, INT_ALIGNMENT);
}

static int
mr_data_writeval_varbit (OR_BUF * buf, DB_VALUE * value)
{
  return mr_writeval_varbit_internal (buf, value, INT_ALIGNMENT);
}

static int
mr_data_readval_varbit (OR_BUF * buf, DB_VALUE * value,
			TP_DOMAIN * domain, int size, bool copy,
			char *copy_buf, int copy_buf_len)
{
  return mr_readval_varbit_internal (buf, value, domain, size, copy,
				     copy_buf, copy_buf_len, INT_ALIGNMENT);
}

static int
mr_lengthval_varbit_internal (DB_VALUE * value, int disk, int align)
{
  int bit_length, len;
  const char *str;

  len = 0;
  if (value != NULL && (str = db_get_string (value)) != NULL)
    {
      bit_length = db_get_string_length (value);	/* size in bits */

      if (align == INT_ALIGNMENT)
	{
	  len = or_packed_varbit_length (bit_length);
	}
      else
	{
	  len = or_varbit_length (bit_length);
	}
    }
  return len;
}

static int
mr_writeval_varbit_internal (OR_BUF * buf, DB_VALUE * value, int align)
{
  int src_bit_length;
  char *str;
  int rc = NO_ERROR;

  if (value != NULL && (str = db_get_string (value)) != NULL)
    {
      src_bit_length = db_get_string_length (value);	/* size in bits */

      if (align == INT_ALIGNMENT)
	{
	  rc = or_packed_put_varbit (buf, str, src_bit_length);
	}
      else
	{
	  rc = or_put_varbit (buf, str, src_bit_length);
	}
    }
  return rc;
}


/*
 * Size can come in as negative here to create a value with a pointer
 * directly to disk.
 *
 * Note that we have a potential conflict with this as -1 is a valid size
 * to use here when the string has been packed with a domain/length prefix
 * and we can determine the size from there.  In current practice, this
 * isn't a problem because due to word alignment, we'll never get a
 * negative size here that is greater than -4.
 */
static int
mr_readval_varbit_internal (OR_BUF * buf, DB_VALUE * value,
			    TP_DOMAIN * domain, int size, bool copy,
			    char *copy_buf, int copy_buf_len, int align)
{
  int pad, precision;
  int str_bit_length, str_length;
  char *new_, *start = NULL;
  int rc = NO_ERROR;

  if (value == NULL)
    {
      if (size == -1)
	{
	  rc = or_skip_varbit (buf, align);
	}
      else
	{
	  if (size)
	    rc = or_advance (buf, size);
	}
    }
  else
    {
      if (domain != NULL)
	{
	  precision = domain->precision;
	}
      else
	{
	  precision = DB_MAX_VARBIT_PRECISION;
	}

      if (!copy)
	{
	  str_bit_length = or_get_varbit_length (buf, &rc);
	  if (rc == NO_ERROR)
	    {
	      db_make_varbit (value, precision, buf->ptr, str_bit_length);
	      value->need_clear = false;
	      rc = or_skip_varbit_remainder (buf, str_bit_length, align);
	    }
	}
      else
	{
	  if (size == 0)
	    {
	      /* its NULL */
	      db_value_domain_init (value, DB_TYPE_VARBIT, precision, 0);
	    }
	  else
	    {			/* size != 0 */
	      if (size == -1)
		{
		  /* Standard packed varbit with a size prefix */
		  ;		/* do nothing */
		}
	      else
		{		/* size != -1 */
		  /* Standard packed varbit within an area of fixed size,
		   * usually this means we're looking at the disk
		   * representation of an attribute.
		   * Just like the -1 case except we advance past the additional
		   * padding.
		   */
		  start = buf->ptr;
		}		/* size != -1 */

	      str_bit_length = or_get_varbit_length (buf, &rc);
	      if (rc != NO_ERROR)
		{
		  return ER_FAILED;
		}
	      /* get the string byte length */
	      str_length = BITS_TO_BYTES (str_bit_length);

	      if (copy_buf && copy_buf_len >= str_length + 1)
		{
		  /* read buf image into the copy_buf */
		  new_ = copy_buf;
		}
	      else
		{
		  /*
		   * Allocate storage for the string including the kludge NULL
		   * terminator
		   */
		  new_ = db_private_alloc (NULL, str_length + 1);
		}

	      if (new_ == NULL)
		{
		  /* need to be able to return errors ! */
		  if (domain)
		    {
		      db_value_domain_init (value, TP_DOMAIN_TYPE (domain),
					    TP_FLOATING_PRECISION_VALUE, 0);
		    }
		  or_abort (buf);
		  return ER_FAILED;
		}
	      else
		{
		  /* do not read the kludge NULL terminator */
		  rc = or_get_data (buf, new_, str_length);
		  if (rc == NO_ERROR && align == INT_ALIGNMENT)
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
		      return ER_FAILED;
		    }

		  new_[str_length] = '\0';	/* append the kludge NULL terminator */
		  db_make_varbit (value, precision, new_, str_bit_length);
		  value->need_clear = (new_ != copy_buf) ? true : false;

		  if (size == -1)
		    {
		      /* Standard packed varbit with a size prefix */
		      ;		/* do nothing */
		    }
		  else
		    {		/* size != -1 */
		      /* Standard packed varbit within an area of fixed size,
		       * usually this means we're looking at the disk
		       * representation of an attribute.
		       * Just like the -1 case except we advance past the
		       * additional padding.
		       */
		      pad = size - (int) (buf->ptr - start);
		      if (pad > 0)
			{
			  rc = or_advance (buf, pad);
			}
		    }		/* size != -1 */
		}		/* else */
	    }			/* size != 0 */
	}

    }
  return rc;
}

static int
mr_index_cmpdisk_varbit (void *mem1, void *mem2, TP_DOMAIN * domain,
			 int do_coercion, int total_order, int *start_colp)
{
  assert (domain != NULL);

  return mr_data_cmpdisk_varbit (mem1, mem2, domain, do_coercion,
				 total_order, start_colp);
}

static int
mr_data_cmpdisk_varbit (void *mem1, void *mem2, TP_DOMAIN * domain,
			int do_coercion, int total_order, int *start_colp)
{
  int bit_length1, bit_length2;
  int mem_length1, mem_length2, c;
  OR_BUF buf1, buf2;

  assert (domain != NULL);

  or_init (&buf1, (char *) mem1, 0);
  bit_length1 = or_get_varbit_length (&buf1, &c);
  mem_length1 = BITS_TO_BYTES (bit_length1);

  or_init (&buf2, (char *) mem2, 0);
  bit_length2 = or_get_varbit_length (&buf2, &c);
  mem_length2 = BITS_TO_BYTES (bit_length2);

  c = varbit_compare ((unsigned char *) buf1.ptr, mem_length1,
		      (unsigned char *) buf2.ptr, mem_length2);
  c = MR_CMP_RETURN_CODE (c);

  return c;
}

static int
mr_cmpval_varbit (DB_VALUE * value1, DB_VALUE * value2,
		  int do_coercion, int total_order, int *start_colp,
		  int collation)
{
  int c;
  unsigned char *string1, *string2;

  string1 = (unsigned char *) DB_GET_STRING (value1);
  string2 = (unsigned char *) DB_GET_STRING (value2);

  if (string1 == NULL || string2 == NULL)
    {
      return DB_UNK;
    }

  c = varbit_compare (string1, (int) DB_GET_STRING_SIZE (value1),
		      string2, (int) DB_GET_STRING_SIZE (value2));
  c = MR_CMP_RETURN_CODE (c);

  return c;
}

static int
mr_cmpval_varbit2 (DB_VALUE * value1, DB_VALUE * value2, int length,
		   int do_coercion, int total_order, int *start_colp)
{
  int c;
  unsigned char *string1, *string2;
  int len1, len2, string_size;

  string1 = (unsigned char *) DB_GET_STRING (value1);
  string2 = (unsigned char *) DB_GET_STRING (value2);

  if (string1 == NULL || string2 == NULL)
    {
      return DB_UNK;
    }

  string_size = (int) DB_GET_STRING_SIZE (value1);
  len1 = MIN (string_size, length);
  string_size = (int) DB_GET_STRING_SIZE (value2);
  len2 = MIN (string_size, length);

  c = varbit_compare (string1, len1, string2, len2);
  c = MR_CMP_RETURN_CODE (c);

  return c;
}


PR_TYPE tp_VarBit = {
  "bit varying", DB_TYPE_VARBIT, 1, sizeof (const char *), 0, 1,
  help_fprint_value,
  help_sprint_value,
  mr_initmem_varbit,
  mr_initval_varbit,
  mr_setmem_varbit,
  mr_getmem_varbit,
  mr_setval_varbit,
  mr_data_lengthmem_varbit,
  mr_data_lengthval_varbit,
  mr_data_writemem_varbit,
  mr_data_readmem_varbit,
  mr_data_writeval_varbit,
  mr_data_readval_varbit,
  mr_index_lengthmem_varbit,
  mr_index_lengthval_varbit,
  mr_index_writeval_varbit,
  mr_index_readval_varbit,
  mr_index_cmpdisk_varbit,
  mr_freemem_varbit,
  mr_data_cmpdisk_varbit,
  mr_cmpval_varbit
};

PR_TYPE *tp_Type_varbit = &tp_VarBit;


static void
mr_initmem_enumeration (void *mem, TP_DOMAIN * domain)
{
  *(unsigned short *) mem = 0;
}

static void
mr_initval_enumeration (DB_VALUE * value, int precision, int scale)
{
  db_value_domain_init (value, DB_TYPE_ENUMERATION, precision, scale);
  db_make_enumeration (value, 0, NULL, 0,
		       LANG_SYS_CODESET, LANG_SYS_COLLATION);
}

static int
mr_setmem_enumeration (void *mem, TP_DOMAIN * domain, DB_VALUE * value)
{
  if (value == NULL)
    {
      mr_initmem_enumeration (mem, domain);
    }
  else
    {
      *(unsigned short *) mem = DB_GET_ENUM_SHORT (value);
    }

  return NO_ERROR;
}

static int
mr_getmem_enumeration (void *mem, TP_DOMAIN * domain, DB_VALUE * value,
		       bool copy)
{
  unsigned short short_val = 0;
  int str_size = 0;
  char *str_val = NULL, *copy_str = NULL;

  short_val = *(short *) mem;

  return mr_setval_enumeration_internal (value, domain, short_val, 0, copy,
					 NULL, 0);
}

static int
mr_setval_enumeration (DB_VALUE * dest, const DB_VALUE * src, bool copy)
{
  char *str = NULL;
  bool need_clear = false;

  if (src == NULL || DB_IS_NULL (src))
    {
      return db_value_domain_init (dest, DB_TYPE_ENUMERATION,
				   DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE);
    }

  if (DB_GET_ENUM_STRING (src) != NULL)
    {
      if (copy)
	{
	  str = db_private_alloc (NULL, DB_GET_ENUM_STRING_SIZE (src) + 1);
	  if (str == NULL)
	    {
	      assert (er_errid () != NO_ERROR);
	      return er_errid ();
	    }
	  memcpy (str, DB_GET_ENUM_STRING (src),
		  DB_GET_ENUM_STRING_SIZE (src));
	  str[DB_GET_ENUM_STRING_SIZE (src)] = 0;
	  need_clear = true;
	}
      else
	{
	  str = DB_GET_ENUM_STRING (src);
	}
    }

  /* get proper codeset from src */
  db_make_enumeration (dest, DB_GET_ENUM_SHORT (src), str,
		       DB_GET_ENUM_STRING_SIZE (src),
		       DB_GET_ENUM_CODESET (src),
		       DB_GET_ENUM_COLLATION (src));
  dest->need_clear = need_clear;

  return NO_ERROR;
}

static void
mr_data_writemem_enumeration (OR_BUF * buf, void *memptr, TP_DOMAIN * domain)
{
  unsigned short *mem = (unsigned short *) memptr;

  or_put_short (buf, *mem);
}

static void
mr_data_readmem_enumeration (OR_BUF * buf, void *mem, TP_DOMAIN * domain,
			     int size)
{
  int rc = NO_ERROR;
  if (mem == NULL)
    {
      or_advance (buf, tp_Enumeration.disksize);
    }
  else
    {
      *(unsigned short *) mem = (unsigned short) or_get_short (buf, &rc);
    }
}

/*
 * mr_setval_enumeration_internal () - make an enumeration value based on
 *	index and domain.
 * return: NO_ERROR or error code.
 * value(in/out):
 * domain(in):
 * index(in): index of enumeration string
 * size(in):
 * copy(in):
 * copy_buf(in):
 * copy_buf_len(in):
 */
static int
mr_setval_enumeration_internal (DB_VALUE * value,
				TP_DOMAIN * domain, unsigned short index,
				int size, bool copy, char *copy_buf,
				int copy_buf_len)
{
  bool need_clear = false;
  int str_size;
  char *str;
  DB_ENUM_ELEMENT *db_elem = NULL;

  if (domain == NULL || DOM_GET_ENUM_ELEMS_COUNT (domain) == 0 || index == 0
      || index == DB_ENUM_OVERFLOW_VAL)
    {
      db_make_enumeration (value, index, NULL, 0,
			   TP_DOMAIN_CODESET (domain),
			   TP_DOMAIN_COLLATION (domain));
      value->need_clear = false;
      return NO_ERROR;
    }

  if (index > DOM_GET_ENUM_ELEMS_COUNT (domain)
      && DOM_GET_ENUM_ELEMS_COUNT (domain) > 0)
    {
      assert (false);
      return ER_FAILED;
    }

  db_elem = &DOM_GET_ENUM_ELEM (domain, index);
  str_size = DB_GET_ENUM_ELEM_STRING_SIZE (db_elem);
  if (!copy)
    {
      str = DB_GET_ENUM_ELEM_STRING (db_elem);
    }
  else
    {
      if (copy_buf && copy_buf_len >= str_size + 1)
	{
	  /* read buf image into the copy_buf */
	  str = copy_buf;
	  need_clear = false;
	}
      else
	{
	  str = db_private_alloc (NULL, str_size + 1);
	  if (str == NULL)
	    {
	      return ER_FAILED;
	    }
	  need_clear = true;
	}
      memcpy (str, DB_GET_ENUM_ELEM_STRING (db_elem), str_size);
      str[str_size] = 0;
    }

  db_make_enumeration (value, index, str, str_size,
		       TP_DOMAIN_CODESET (domain),
		       TP_DOMAIN_COLLATION (domain));
  value->need_clear = need_clear;

  return NO_ERROR;
}

static int
mr_data_readval_enumeration (OR_BUF * buf, DB_VALUE * value,
			     TP_DOMAIN * domain, int size, bool copy,
			     char *copy_buf, int copy_buf_len)
{
  int rc = NO_ERROR;
  unsigned short s;

  if (value == NULL)
    {
      rc = or_advance (buf, tp_Enumeration.disksize);
      return rc;
    }

  s = (unsigned short) or_get_short (buf, &rc);
  if (rc != NO_ERROR)
    {
      return rc;
    }

  return mr_setval_enumeration_internal (value, domain, s, size, copy,
					 copy_buf, copy_buf_len);
}

static int
mr_data_writeval_enumeration (OR_BUF * buf, DB_VALUE * value)
{
  return or_put_short (buf, DB_GET_ENUM_SHORT (value));
}

static int
mr_index_writeval_enumeration (OR_BUF * buf, DB_VALUE * value)
{
  unsigned short s = DB_GET_ENUM_SHORT (value);

  return or_put_data (buf, (char *) (&s), tp_Enumeration.disksize);
}

static int
mr_index_readval_enumeration (OR_BUF * buf, DB_VALUE * value,
			      TP_DOMAIN * domain, int size, bool copy,
			      char *copy_buf, int copy_buf_len)
{
  int rc = NO_ERROR;
  unsigned short s;

  if (value == NULL)
    {
      rc = or_advance (buf, tp_Enumeration.disksize);
      return rc;
    }

  rc = or_get_data (buf, (char *) (&s), tp_Enumeration.disksize);
  if (rc != NO_ERROR)
    {
      return rc;
    }

  return mr_setval_enumeration_internal (value, domain, s, size, copy,
					 copy_buf, copy_buf_len);
}

static int
mr_index_cmpdisk_enumeration (void *mem1, void *mem2, TP_DOMAIN * domain,
			      int do_coercion, int total_order,
			      int *start_colp)
{
  unsigned short s1, s2;

  assert (domain != NULL);

  COPYMEM (unsigned short, &s1, mem1);
  COPYMEM (unsigned short, &s2, mem2);

  return MR_CMP (s1, s2);
}

static int
mr_data_cmpdisk_enumeration (void *mem1, void *mem2, TP_DOMAIN * domain,
			     int do_coercion, int total_order,
			     int *start_colp)
{
  unsigned short s1, s2;

  assert (domain != NULL);

  s1 = (unsigned short) OR_GET_SHORT (mem1);
  s2 = (unsigned short) OR_GET_SHORT (mem2);

  return MR_CMP (s1, s2);
}

static int
mr_cmpval_enumeration (DB_VALUE * value1, DB_VALUE * value2, int do_coercion,
		       int total_order, int *start_colp, int collation)
{
  unsigned short s1, s2;

  s1 = DB_GET_ENUM_SHORT (value1);
  s2 = DB_GET_ENUM_SHORT (value2);

  return MR_CMP (s1, s2);
}
