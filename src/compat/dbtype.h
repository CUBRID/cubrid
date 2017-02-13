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
 * dbtype.h - Definitions related to the memory representations of database
 * attribute values. This is an application interface file. It should contain
 * only definitions available to CUBRID customer applications.
 */

#ifndef _DBTYPE_H_
#define _DBTYPE_H_

#ident "$Id$"

#include "config.h"

#include "assert.h"
#include "dbdef.h"
#include "porting.h"
#include "intl_support.h"
#include "error_manager.h"
#include "system_parameter.h"
#include "es_common.h"

/* From string_opfunc.h */
#define QSTR_IS_CHAR(s)          (((s)==DB_TYPE_CHAR) || \
                                 ((s)==DB_TYPE_VARCHAR))
#define QSTR_IS_NATIONAL_CHAR(s) (((s)==DB_TYPE_NCHAR) || \
                                 ((s)==DB_TYPE_VARNCHAR))
#define QSTR_IS_BIT(s)           (((s)==DB_TYPE_BIT) || \
                                 ((s)==DB_TYPE_VARBIT))
#define QSTR_IS_ANY_CHAR(s)	(QSTR_IS_CHAR(s) || QSTR_IS_NATIONAL_CHAR(s))
#define QSTR_IS_ANY_CHAR_OR_BIT(s)		(QSTR_IS_ANY_CHAR(s) \
                                                 || QSTR_IS_BIT(s))

/* From object_accessor.h */
extern char *obj_Method_error_msg;

/* From language_support.h */
/* collation and charset do be used by system : */
enum
{
  LANG_COLL_ISO_BINARY = 0,
  LANG_COLL_UTF8_BINARY = 1,
  LANG_COLL_ISO_EN_CS = 2,
  LANG_COLL_ISO_EN_CI = 3,
  LANG_COLL_UTF8_EN_CS = 4,
  LANG_COLL_UTF8_EN_CI = 5,
  LANG_COLL_UTF8_TR_CS = 6,
  LANG_COLL_UTF8_KO_CS = 7,
  LANG_COLL_EUCKR_BINARY = 8,
  LANG_COLL_BINARY = 9
};
#define LANG_GET_BINARY_COLLATION(c) (((c) == INTL_CODESET_UTF8) \
  ? LANG_COLL_UTF8_BINARY :					 \
  (((c) == INTL_CODESET_KSC5601_EUC) ? LANG_COLL_EUCKR_BINARY :  \
  (((c) == INTL_CODESET_ISO88591) ? LANG_COLL_ISO_BINARY :	 \
  LANG_COLL_BINARY)))

extern INTL_CODESET lang_charset (void);

#define LANG_SYS_COLLATION  (LANG_GET_BINARY_COLLATION(lang_charset()))

#define LANG_SYS_CODESET  lang_charset()

#define LANG_VARIABLE_CHARSET(x) ((x) != INTL_CODESET_ASCII     && \
  (x) != INTL_CODESET_RAW_BITS  && \
  (x) != INTL_CODESET_RAW_BYTES && \
  (x) != INTL_CODESET_ISO88591)

/* From object_domain.h */
/*
 * We probably should make this 0 rather than -1 so that we can more easily
 * represent precisions with unsigned integers.  Zero is not a valid
 * precision.
 */
#define TP_FLOATING_PRECISION_VALUE -1

/* The maximum length of the partition expression after it is processed */
/* TODO: Needs to be moved from here!! */
#define DB_MAX_PARTITION_EXPR_LENGTH 2048

typedef enum
{
  SMALL_STRING,
  MEDIUM_STRING,
  LARGE_STRING
} STRING_STYLE;

/*
 * DB_MAX_IDENTIFIER_LENGTH -
 * This constant defines the maximum length of an identifier
 * in the database.  An identifier is anything that is passed as a string
 * to the db_ functions (other than user attribute values).  This
 * includes such things as class names, attribute names etc.  This
 * isn't strictly enforced right now but applications must be aware that
 * this will be a requirement.
 */
#define DB_MAX_IDENTIFIER_LENGTH 255

/* Maximum allowable user name.*/
#define DB_MAX_USER_LENGTH 32

#define DB_MAX_PASSWORD_LENGTH 8

/* Maximum allowable schema name. */
#define DB_MAX_SCHEMA_LENGTH DB_MAX_USER_LENGTH

/* Maximum allowable class name. */
#define DB_MAX_CLASS_LENGTH (DB_MAX_IDENTIFIER_LENGTH-DB_MAX_SCHEMA_LENGTH-4)

#define DB_MAX_SPEC_LENGTH       4096

/* Maximum allowable class comment length */
#define DB_MAX_CLASS_COMMENT_LENGTH     2048
/* Maximum allowable comment length */
#define DB_MAX_COMMENT_LENGTH    1024

/* This constant defines the maximum length of a character
   string that can be used as the value of an attribute. */
#define DB_MAX_STRING_LENGTH	0x3fffffff

/* This constant defines the maximum length of a bit string
   that can be used as the value of an attribute. */
#define DB_MAX_BIT_LENGTH 0x3fffffff

/* The maximum precision that can be specified for a numeric domain. */
#define DB_MAX_NUMERIC_PRECISION 38

/* The upper limit for a number that can be represented by a numeric type */
#define DB_NUMERIC_OVERFLOW_LIMIT 1e38

/* The lower limit for a number that can be represented by a numeric type */
#define DB_NUMERIC_UNDERFLOW_LIMIT 1e-38

/* The maximum precision that can be specified for a CHAR(n) domain. */
#define DB_MAX_CHAR_PRECISION DB_MAX_STRING_LENGTH

/* The maximum precision that can be specified
   for a CHARACTER VARYING domain.*/
#define DB_MAX_VARCHAR_PRECISION DB_MAX_STRING_LENGTH

/* The maximum precision that can be specified for a NATIONAL CHAR(n)
   domain.
   This probably isn't restrictive enough.  We may need to define
   this functionally as the maximum precision will depend on the size
   multiplier of the codeset.*/
#define DB_MAX_NCHAR_PRECISION (DB_MAX_STRING_LENGTH/2)

/* The maximum precision that can be specified for a NATIONAL CHARACTER
   VARYING domain.
   This probably isn't restrictive enough.  We may need to define
   this functionally as the maximum precision will depend on the size
   multiplier of the codeset. */
#define DB_MAX_VARNCHAR_PRECISION DB_MAX_NCHAR_PRECISION

/*  The maximum precision that can be specified for a BIT domain. */
#define DB_MAX_BIT_PRECISION DB_MAX_BIT_LENGTH

/* The maximum precision that can be specified for a BIT VARYING domain. */
#define DB_MAX_VARBIT_PRECISION DB_MAX_BIT_PRECISION

/* This constant indicates that the system defined default for
   determining the length of a string is to be used for a DB_VALUE. */
#define DB_DEFAULT_STRING_LENGTH -1

/* This constant indicates that the system defined default for
   precision is to be used for a DB_VALUE. */
#define DB_DEFAULT_PRECISION -1

/* This constant indicates that the system defined default for
   scale is to be used for a DB_VALUE. */
#define DB_DEFAULT_SCALE -1

/* This constant defines the default precision of DB_TYPE_NUMERIC. */
#define DB_DEFAULT_NUMERIC_PRECISION 15

/* This constant defines the default scale of DB_TYPE_NUMERIC. */
#define DB_DEFAULT_NUMERIC_SCALE 0

/* This constant defines the default scale of result
   of numeric division operation */
#define DB_DEFAULT_NUMERIC_DIVISION_SCALE 9

/* These constants define the size of buffers within a DB_VALUE. */
#define DB_NUMERIC_BUF_SIZE	(2*sizeof(double))
#define DB_SMALL_CHAR_BUF_SIZE	(2*sizeof(double) - 3*sizeof(unsigned char))

/* This constant defines the default precision of DB_TYPE_BIGINT. */
#define DB_BIGINT_PRECISION      19

/* This constant defines the default precision of DB_TYPE_INTEGER. */
#define DB_INTEGER_PRECISION      10

/* This constant defines the default precision of DB_TYPE_SMALLINT. */
#define DB_SMALLINT_PRECISION      5

/* This constant defines the default precision of DB_TYPE_SHORT.*/
#define DB_SHORT_PRECISION      DB_SMALLINT_PRECISION

/* This constant defines the default decimal precision of DB_TYPE_FLOAT. */
#define DB_FLOAT_DECIMAL_PRECISION      7

/* This constant defines the default decimal precision of DB_TYPE_DOUBLE. */
#define DB_DOUBLE_DECIMAL_PRECISION      15

/* This constant defines the default decimal precision of DB_TYPE_MONETARY. */
#define DB_MONETARY_DECIMAL_PRECISION DB_DOUBLE_DECIMAL_PRECISION

/* This constant defines the default precision of DB_TYPE_TIME. */
#define DB_TIME_PRECISION      8

/* This constant defines the default precision */
#define DB_TIMETZ_PRECISION   DB_TIME_PRECISION

/* This constant defines the default precision of DB_TYPE_DATE. */
#define DB_DATE_PRECISION      10

/* This constant defines the default precision of DB_TYPE_TIMESTAMP. */
#define DB_TIMESTAMP_PRECISION      19

/* This constant defines the default precision of DB_TYPE_TIMESTAMPTZ. */
#define DB_TIMESTAMPTZ_PRECISION   DB_TIMESTAMP_PRECISION

/* This constant defines the default precision of DB_TYPE_DATETIME. */
#define DB_DATETIME_PRECISION      23

/* This constant defines the default precision of DB_TYPE_DATETIMETZ. */
#define DB_DATETIMETZ_PRECISION    DB_DATETIME_PRECISION

/* This constant defines the default scale of DB_TYPE_DATETIME. */
#define DB_DATETIME_DECIMAL_SCALE      3

#if !defined(SERVER_MODE) || defined(NDEBUG)
#define NO_SERVER_OR_DEBUG_MODE
#endif

/* Defines the state of a value as not being compressable due to its bad compression size or 
 * its uncompressed size being lower than PRIM_MINIMUM_STRING_LENGTH_FOR_COMPRESSION
 */
#define DB_UNCOMPRESSABLE -1

/* Defines the state of a value not being yet prompted for a compression process. */
#define DB_NOT_YET_COMPRESSED 0

#define DB_CURRENCY_DEFAULT db_get_currency_default()

#define db_set db_collection

#define db_make_utime db_make_timestamp

#define DB_MAKE_NULL(value) db_make_null(value)

#define DB_VALUE_CLONE_AS_NULL(src_value, dest_value)                   \
  do {                                                                  \
    if ((db_value_domain_init(dest_value,                               \
                              db_value_domain_type(src_value),          \
                              db_value_precision(src_value),            \
                              db_value_scale(src_value)))               \
        == NO_ERROR)                                                    \
      (void)db_value_put_null(dest_value);                              \
  } while (0)

#define DB_MAKE_INTEGER(value, num) db_make_int(value, num)

#define DB_MAKE_INT DB_MAKE_INTEGER

#define DB_MAKE_BIGINT(value, num) db_make_bigint(value, num)

#define DB_MAKE_BIGINTEGER DB_MAKE_BIGINT

#define DB_MAKE_FLOAT(value, num) db_make_float(value, num)

#define DB_MAKE_DOUBLE(value, num) db_make_double(value, num)

#define DB_MAKE_OBJECT(value, obj) db_make_object(value, obj)

#define DB_MAKE_OBJ DB_MAKE_OBJECT

#define DB_MAKE_SET(value, set) db_make_set(value, set)

#define DB_MAKE_MULTISET(value, set) db_make_multiset(value, set)

/* obsolete */
#define DB_MAKE_MULTI_SET DB_MAKE_MULTISET

#define DB_MAKE_SEQUENCE(value, set) db_make_sequence(value, set)

#define DB_MAKE_LIST DB_MAKE_SEQUENCE

/* obsolete */
#define DB_MAKE_SEQ DB_MAKE_SEQUENCE

/* new preferred interface */
#define DB_MAKE_COLLECTION(value, col) db_make_collection(value, col)

#define DB_MAKE_MIDXKEY(value, midxkey) db_make_midxkey(value, midxkey)

#define DB_MAKE_ELO(value, type, elo) db_make_elo(value, type, elo)

#define DB_MAKE_TIME(value, hour, minute, second) \
    db_make_time(value, hour, minute, second)

#define DB_MAKE_TIMETZ(value, timetz_value) \
    db_make_timetz(value, timetz_value)

#define DB_MAKE_TIMELTZ(value, time_value) \
    db_make_timeltz(value, time_value)

#define DB_MAKE_ENCODED_TIME(value, time_value) \
    db_value_put_encoded_time(value, time_value)

#define DB_MAKE_DATE(value, month, day, year) \
    db_make_date(value, month, day, year)

#define DB_MAKE_ENCODED_DATE(value, date_value) \
    db_value_put_encoded_date(value, date_value)

#define DB_MAKE_TIMESTAMP(value, timeval) \
    db_make_timestamp(value, timeval)

#define DB_MAKE_UTIME DB_MAKE_TIMESTAMP

#define DB_MAKE_TIMESTAMPTZ(value, ts_tz) \
    db_make_timestamptz(value, ts_tz)

#define DB_MAKE_TIMESTAMPLTZ(value, timeval) \
    db_make_timestampltz(value, timeval)

#define DB_MAKE_MONETARY_AMOUNT(value, amount) \
    db_make_monetary(value, DB_CURRENCY_DEFAULT, amount)

#define DB_MAKE_DATETIME(value, datetime_value) \
    db_make_datetime(value, datetime_value)

#define DB_MAKE_DATETIMETZ(value, datetimetz_value) \
    db_make_datetimetz(value, datetimetz_value)

#define DB_MAKE_DATETIMELTZ(value, datetime_value) \
    db_make_datetimeltz(value, datetime_value)

#define DB_MAKE_MONETARY DB_MAKE_MONETARY_AMOUNT

#define DB_MAKE_MONETARY_TYPE_AMOUNT(value, type, amount) \
    db_make_monetary(value, type, amount)

#define DB_MAKE_POINTER(value, ptr) db_make_pointer(value, ptr)

#define DB_MAKE_ERROR(value, errcode) db_make_error(value, errcode)

#define DB_MAKE_METHOD_ERROR(value, errcode, errmsg) \
	   db_make_method_error(value, errcode, errmsg)

#define DB_MAKE_SMALLINT(value, num) db_make_short(value, num)

#define DB_MAKE_SHORT DB_MAKE_SMALLINT

#define DB_MAKE_NUMERIC(value, num, precision, scale) \
	   db_make_numeric(value, num, precision, scale)

#define DB_MAKE_BIT(value, bit_length, bit_str, bit_str_bit_size) \
        db_make_bit(value, bit_length, bit_str, bit_str_bit_size)

#define DB_MAKE_VARBIT(value, max_bit_length, bit_str, bit_str_bit_size)\
        db_make_varbit(value, max_bit_length, bit_str, bit_str_bit_size)

#define DB_MAKE_CHAR(value, char_length, str, char_str_byte_size, \
		     codeset, collation) \
        db_make_char(value, char_length, str, char_str_byte_size, \
		     codeset, collation)

#define DB_MAKE_VARCHAR(value, max_char_length, str, char_str_byte_size, \
		        codeset, collation) \
        db_make_varchar(value, max_char_length, str, char_str_byte_size, \
			codeset, collation)

#define DB_MAKE_STRING(value, str) db_make_string(value, str)

#define DB_MAKE_NCHAR(value, nchar_length, str, nchar_str_byte_size, \
		      codeset, collation) \
        db_make_nchar(value, nchar_length, str, nchar_str_byte_size, \
		      codeset, collation)

#define DB_MAKE_VARNCHAR(value, max_nchar_length, str, nchar_str_byte_size, \
			 codeset, collation)\
        db_make_varnchar(value, max_nchar_length, str, nchar_str_byte_size, \
			 codeset, collation)

#define DB_MAKE_ENUMERATION(value, index, str, size, codeset, collation) \
	db_make_enumeration(value, index, str, size, codeset, collation)

#define DB_MAKE_RESULTSET(value, handle) db_make_resultset(value, handle)

#define db_get_collection db_get_set
#define db_get_utime db_get_timestamp

#define DB_IS_NULL(value)               db_value_is_null(value)

#define DB_VALUE_DOMAIN_TYPE(value)     db_value_domain_type(value)

#define DB_VALUE_TYPE(value)            db_value_type(value)

#define DB_VALUE_PRECISION(value)       db_value_precision(value)

#define DB_VALUE_SCALE(value)           db_value_scale(value)

#define DB_GET_COMPRESSED_SIZE(value) db_get_compressed_size(value)

#define DB_SET_COMPRESSED_STRING(value, compressed_string, compressed_size, compressed_need_clear) \
	db_set_compressed_string(value, compressed_string, compressed_size, compressed_need_clear)

#define DB_TRIED_COMPRESSION(value) (DB_GET_COMPRESSED_SIZE(value) != DB_NOT_YET_COMPRESSED)

#define DB_INT16_MIN   (-(DB_INT16_MAX)-1)
#define DB_INT16_MAX   0x7FFF
#define DB_UINT16_MAX  0xFFFFU
#define DB_INT32_MIN   (-(DB_INT32_MAX)-1)
#define DB_INT32_MAX   0x7FFFFFFF
#define DB_UINT32_MIN  0
#define DB_UINT32_MAX  0xFFFFFFFFU
#if (__WORDSIZE == 64) || defined(_WIN64)
#define DB_BIGINT_MAX  9223372036854775807L
#define DB_BIGINT_MIN  (-DB_BIGINT_MAX - 1L)
#else /* (__WORDSIZE == 64) || defined(_WIN64) */
#define DB_BIGINT_MAX  9223372036854775807LL
#define DB_BIGINT_MIN  (-DB_BIGINT_MAX - 1LL)
#endif /* (__WORDSIZE == 64) || defined(_WIN64) */
#define DB_ENUM_ELEMENTS_MAX  512
/* special ENUM index for PT_TO_ENUMERATION_VALUE function */
#define DB_ENUM_OVERFLOW_VAL  0xFFFF

/* DB_DATE_MIN and DB_DATE_MAX are calculated by julian_encode function
   with arguments (1,1,1) and (12,31,9999) respectively. */
#define DB_DATE_ZERO       DB_UINT32_MIN	/* 0 means zero date */
#define DB_DATE_MIN        1721424
#define DB_DATE_MAX        5373484

#define DB_TIME_MIN        DB_UINT32_MIN
#define DB_TIME_MAX        DB_UINT32_MAX

#define DB_UTIME_ZERO      DB_DATE_ZERO	/* 0 means zero date */
#define DB_UTIME_MIN       (DB_UTIME_ZERO + 1)
#define DB_UTIME_MAX       DB_UINT32_MAX

#define DB_IS_DATETIME_DEFAULT_EXPR(v) ((v) == DB_DEFAULT_SYSDATE || \
    (v) == DB_DEFAULT_CURRENTTIME || (v) == DB_DEFAULT_CURRENTDATE || \
    (v) == DB_DEFAULT_SYSDATETIME || (v) == DB_DEFAULT_SYSTIMESTAMP || \
    (v) == DB_DEFAULT_UNIX_TIMESTAMP || (v) == DB_DEFAULT_CURRENTDATETIME || \
    (v) == DB_DEFAULT_CURRENTTIMESTAMP || (v) == DB_DEFAULT_SYSTIME)

/* This defines the basic type identifier constants.  These are used in
   the domain specifications of attributes and method arguments and
   as value type tags in the DB_VALUE structures. */
typedef enum
{
  DB_TYPE_FIRST = 0,		/* first for iteration */
  DB_TYPE_UNKNOWN = 0,
  DB_TYPE_NULL = 0,
  DB_TYPE_INTEGER = 1,
  DB_TYPE_FLOAT = 2,
  DB_TYPE_DOUBLE = 3,
  DB_TYPE_STRING = 4,
  DB_TYPE_OBJECT = 5,
  DB_TYPE_SET = 6,
  DB_TYPE_MULTISET = 7,
  DB_TYPE_SEQUENCE = 8,
  DB_TYPE_ELO = 9,
  DB_TYPE_TIME = 10,
  DB_TYPE_TIMESTAMP = 11,
  DB_TYPE_DATE = 12,
  DB_TYPE_MONETARY = 13,
  DB_TYPE_VARIABLE = 14,	/* internal use only */
  DB_TYPE_SUB = 15,		/* internal use only */
  DB_TYPE_POINTER = 16,		/* method arguments only */
  DB_TYPE_ERROR = 17,		/* method arguments only */
  DB_TYPE_SHORT = 18,
  DB_TYPE_VOBJ = 19,		/* internal use only */
  DB_TYPE_OID = 20,		/* internal use only */
  DB_TYPE_DB_VALUE = 21,	/* special for esql */
  DB_TYPE_NUMERIC = 22,		/* SQL NUMERIC(p,s) values */
  DB_TYPE_BIT = 23,		/* SQL BIT(n) values */
  DB_TYPE_VARBIT = 24,		/* SQL BIT(n) VARYING values */
  DB_TYPE_CHAR = 25,		/* SQL CHAR(n) values */
  DB_TYPE_NCHAR = 26,		/* SQL NATIONAL CHAR(n) values */
  DB_TYPE_VARNCHAR = 27,	/* SQL NATIONAL CHAR(n) VARYING values */
  DB_TYPE_RESULTSET = 28,	/* internal use only */
  DB_TYPE_MIDXKEY = 29,		/* internal use only */
  DB_TYPE_TABLE = 30,		/* internal use only */
  DB_TYPE_BIGINT = 31,
  DB_TYPE_DATETIME = 32,
  DB_TYPE_BLOB = 33,
  DB_TYPE_CLOB = 34,
  DB_TYPE_ENUMERATION = 35,
  DB_TYPE_TIMESTAMPTZ = 36,
  DB_TYPE_TIMESTAMPLTZ = 37,
  DB_TYPE_DATETIMETZ = 38,
  DB_TYPE_DATETIMELTZ = 39,
  /* Disabled types */
  DB_TYPE_TIMETZ = 40,		/* internal use only - RESERVED */
  DB_TYPE_TIMELTZ = 41,		/* internal use only - RESERVED */
  /* end of disabled types */
  DB_TYPE_LIST = DB_TYPE_SEQUENCE,
  DB_TYPE_SMALLINT = DB_TYPE_SHORT,	/* SQL SMALLINT */
  DB_TYPE_VARCHAR = DB_TYPE_STRING,	/* SQL CHAR(n) VARYING values */
  DB_TYPE_UTIME = DB_TYPE_TIMESTAMP,	/* SQL TIMESTAMP */

  DB_TYPE_LAST = DB_TYPE_DATETIMELTZ
} DB_TYPE;

/* Domain information stored in DB_VALUE structures. */
typedef union db_domain_info DB_DOMAIN_INFO;
union db_domain_info
{
  struct general_info
  {
    unsigned char is_null;
    unsigned char type;
  } general_info;
  struct numeric_info
  {
    unsigned char is_null;
    unsigned char type;
    unsigned char precision;
    unsigned char scale;
  } numeric_info;
  struct char_info
  {
    unsigned char is_null;
    unsigned char type;
    int length;
    int collation_id;
  } char_info;
};

/* types used for the representation of bigint values. */
typedef INT64 DB_BIGINT;

/* Structure used for the representation of time values. */
typedef unsigned int DB_TIME;

typedef unsigned int TZ_ID;
typedef struct db_timetz DB_TIMETZ;
struct db_timetz
{
  DB_TIME time;
  TZ_ID tz_id;			/* zone id */
};

/* Structure used for the representation of universal times.
   These are compatible with the Unix time_t definition. */
typedef unsigned int DB_TIMESTAMP;

typedef DB_TIMESTAMP DB_UTIME;

typedef struct db_timestamptz DB_TIMESTAMPTZ;
struct db_timestamptz
{
  DB_TIMESTAMP timestamp;	/* Unix timestamp */
  TZ_ID tz_id;			/* zone id */
};

/* Structure used for the representation of date values. */
typedef unsigned int DB_DATE;

typedef struct db_datetime DB_DATETIME;
struct db_datetime
{
  unsigned int date;		/* date */
  unsigned int time;		/* time */
};

typedef struct db_datetimetz DB_DATETIMETZ;
struct db_datetimetz
{
  DB_DATETIME datetime;
  TZ_ID tz_id;			/* zone id */
};

/* Structure used for the representation of numeric values. */
typedef struct db_numeric DB_NUMERIC;
struct db_numeric
{
  union
  {
    unsigned char *digits;
    unsigned char buf[DB_NUMERIC_BUF_SIZE];
  } d;
};



typedef struct db_monetary DB_MONETARY;
struct db_monetary
{
  double amount;
  DB_CURRENCY type;
};

typedef struct db_set SETREF;
struct db_set
{
  /* 
   * a garbage collector ticket is not required for the "owner" field as
   * the entire set references area is registered for scanning in area_grow.
   */
  struct db_object *owner;
  struct db_set *ref_link;
  struct setobj *set;
  char *disk_set;
  DB_DOMAIN *disk_domain;
  int attribute;
  int ref_count;
  int disk_size;
  bool need_clear;
};

/* Definition for the collection descriptor structure. The structures for
 * the collection descriptors and the sequence descriptors are identical
 * internally but not all db_collection functions can be used with sequences
 * and no db_seq functions can be used with sets. It is advisable to
 * recognize the type of set being used, type it appropriately and only
 * call those db_ functions defined for that type.
 */
typedef struct db_set DB_COLLECTION;
typedef DB_COLLECTION DB_MULTISET;
typedef DB_COLLECTION DB_SEQ;
typedef DB_COLLECTION DB_SET;

typedef enum special_column_type MIN_MAX_COLUMN_TYPE;
enum special_column_type
{
  MIN_COLUMN = 0,
  MAX_COLUMN = 1
};

/* Used in btree_coerce_key and btree_ils_adjust_range to represent
 * min or max values, necessary in index search comparisons
 */
typedef struct special_column MIN_MAX_COLUMN_INFO;
struct special_column
{
  int position;			/* position in the list of columns */
  MIN_MAX_COLUMN_TYPE type;	/* MIN or MAX column */
};

typedef struct db_midxkey DB_MIDXKEY;
struct db_midxkey
{
  int size;			/* size of buf */
  int ncolumns;			/* # of elements */
  DB_DOMAIN *domain;		/* MIDXKEY domain */
  char *buf;			/* key structure */
  MIN_MAX_COLUMN_INFO min_max_val;	/* info about coerced column */
};


/*
 * DB_ELO
 * This is the run-time state structure for an ELO. The ELO is part of
 * the implementation of large object type and not intended to be used
 * directly by the API.
 *
 * NOTE:
 *  1. LOID and related definition which were in storage_common.h moved here.
 *  2. DB_ELO definition in dbi_compat.h does not expose the LOID and
 *     related data type. BE CAREFUL when you change following definitions.
 *     - VPID
 *     - VFID
 */

typedef struct vpid VPID;	/* REAL PAGE IDENTIFIER */
struct vpid
{
  INT32 pageid;			/* Page identifier */
  INT16 volid;			/* Volume identifier where the page reside */
};
#define VPID_INITIALIZER \
  { NULL_PAGEID, NULL_VOLID }

#define VPID_AS_ARGS(vpidp) (vpidp)->volid, (vpidp)->pageid

typedef struct vfid VFID;	/* REAL FILE IDENTIFIER */
struct vfid
{
  INT32 fileid;			/* File identifier */
  INT16 volid;			/* Volume identifier where the file reside */
};
#define VFID_INITIALIZER \
  { NULL_FILEID, NULL_VOLID }

#define VFID_AS_ARGS(vfidp) (vfidp)->volid, (vfidp)->fileid

typedef struct loid LOID;	/* LARGE OBJECT IDENTIFIER */
struct loid
{
  VPID vpid;			/* Real page identifier */
  VFID vfid;			/* Real file identifier */
};

typedef enum db_elo_type DB_ELO_TYPE;
typedef struct db_elo DB_ELO;

enum db_elo_type
{
  ELO_NULL,
  ELO_LO,
  ELO_FBO
};

struct db_elo
{
  INT64 size;
  LOID loid;
  char *locator;
  char *meta_data;
  DB_ELO_TYPE type;
  int es_type;
};

/* This is the memory representation of an internal object
 * identifier.  It is in the API only for a few functions that
 * are not intended for general use.
 * An object identifier is NOT a fixed identifier; it cannot be used
 * reliably as an object identifier across database sessions or even
 * across transaction boundaries.  API programs are not allowed
 * to make assumptions about the contents of this structure.
 */
typedef struct db_identifier DB_IDENTIFIER;
struct db_identifier
{
  int pageid;
  short slotid;
  short volid;
};

typedef DB_IDENTIFIER OID;

/* Structure used for the representation of char, nchar and bit values. */
typedef struct db_large_string DB_LARGE_STRING;

/* db_char.sm was formerly db_char.small.  small is an (undocumented)
 * reserved word on NT. */

typedef union db_char DB_CHAR;
union db_char
{
  struct
  {
    unsigned char style;
    unsigned char codeset;
    unsigned char is_max_string;
    unsigned char compressed_need_clear;
  } info;
  struct
  {
    unsigned char style;
    unsigned char codeset;
    unsigned char is_max_string;
    unsigned char compressed_need_clear;
    unsigned char size;
    char buf[DB_SMALL_CHAR_BUF_SIZE];
  } sm;
  struct
  {
    unsigned char style;
    unsigned char codeset;
    unsigned char is_max_string;
    unsigned char compressed_need_clear;
    int size;
    char *buf;
    int compressed_size;
    char *compressed_buf;
  } medium;
  struct
  {
    unsigned char style;
    unsigned char codeset;
    unsigned char is_max_string;
    unsigned char compressed_need_clear;
    DB_LARGE_STRING *str;
  } large;
};

typedef DB_CHAR DB_NCHAR;
typedef DB_CHAR DB_BIT;

typedef int DB_RESULTSET;

/* Structure for an ENUMERATION element */
typedef struct db_enum_element DB_ENUM_ELEMENT;
struct db_enum_element
{
  unsigned short short_val;	/* element index */
  DB_CHAR str_val;		/* element string */
};

/* Structure for an ENUMERATION */
typedef struct db_enumeration DB_ENUMERATION;
struct db_enumeration
{
  DB_ENUM_ELEMENT *elements;	/* array of enumeration elements */
  int collation_id;		/* collation */
  unsigned short count;		/* count of enumeration elements */
};

/* A union of all of the possible basic type values.  This is used in the
 * definition of the DB_VALUE which is the fundamental structure used
 * in passing data in and out of the db_ function layer.
 */

typedef union db_data DB_DATA;
union db_data
{
  int i;
  short sh;
  DB_BIGINT bigint;
  float f;
  double d;
  void *p;
  DB_OBJECT *op;
  DB_TIME time;
  DB_TIMETZ timetz;
  DB_DATE date;
  DB_TIMESTAMP utime;
  DB_TIMESTAMPTZ timestamptz;
  DB_DATETIME datetime;
  DB_DATETIMETZ datetimetz;
  DB_MONETARY money;
  DB_COLLECTION *set;
  DB_COLLECTION *collect;
  DB_MIDXKEY midxkey;
  DB_ELO elo;
  int error;
  DB_IDENTIFIER oid;
  DB_NUMERIC num;
  DB_CHAR ch;
  DB_RESULTSET rset;
  DB_ENUM_ELEMENT enumeration;
};

/* This is the primary structure used for passing values in and out of
 * the db_ function layer. Values are always tagged with a datatype
 * so that they can be identified and type checking can be performed.
 */

typedef struct db_value DB_VALUE;
struct db_value
{
  DB_DOMAIN_INFO domain;
  DB_DATA data;
  bool need_clear;
};

/* This is used to chain DB_VALUEs into a list. */
typedef struct db_value_list DB_VALUE_LIST;
struct db_value_list
{
  struct db_value_list *next;
  DB_VALUE val;
};

/* This is used to chain DB_VALUEs into a list.  It is used as an argument
   to db_send_arglist. */
typedef struct db_value_array DB_VALUE_ARRAY;
struct db_value_array
{
  int size;
  DB_VALUE *vals;
};

/* This is used to gather stats about the workspace.
 * It contains the number of object descriptors used and
 * total number of object descriptors allocated
 */
typedef struct db_workspace_stats DB_WORKSPACE_STATS;
struct db_workspace_stats
{
  int obj_desc_used;		/* number of object descriptors used */
  int obj_desc_total;		/* total # of object descriptors allocated */
};

/* This defines the C language type identifier constants.
 * These are used to describe the types of values used for setting
 * DB_VALUE contents or used to get DB_VALUE contents into.
 */
typedef enum
{
  DB_TYPE_C_DEFAULT = 0,
  DB_TYPE_C_FIRST = 100,	/* first for iteration */
  DB_TYPE_C_INT,
  DB_TYPE_C_SHORT,
  DB_TYPE_C_LONG,
  DB_TYPE_C_FLOAT,
  DB_TYPE_C_DOUBLE,
  DB_TYPE_C_CHAR,
  DB_TYPE_C_VARCHAR,
  DB_TYPE_C_NCHAR,
  DB_TYPE_C_VARNCHAR,
  DB_TYPE_C_BIT,
  DB_TYPE_C_VARBIT,
  DB_TYPE_C_OBJECT,
  DB_TYPE_C_SET,
  DB_TYPE_C_ELO,
  DB_TYPE_C_TIME,
  DB_TYPE_C_DATE,
  DB_TYPE_C_TIMESTAMP,
  DB_TYPE_C_MONETARY,
  DB_TYPE_C_NUMERIC,
  DB_TYPE_C_POINTER,
  DB_TYPE_C_ERROR,
  DB_TYPE_C_IDENTIFIER,
  DB_TYPE_C_DATETIME,
  DB_TYPE_C_BIGINT,
  DB_TYPE_C_LAST,		/* last for iteration */
  DB_TYPE_C_UTIME = DB_TYPE_C_TIMESTAMP
} DB_TYPE_C;

typedef DB_BIGINT DB_C_BIGINT;
typedef int DB_C_INT;
typedef short DB_C_SHORT;
typedef long DB_C_LONG;
typedef float DB_C_FLOAT;
typedef double DB_C_DOUBLE;
typedef char *DB_C_CHAR;
typedef char *DB_C_NCHAR;
typedef char *DB_C_BIT;
typedef DB_OBJECT DB_C_OBJECT;
typedef DB_COLLECTION DB_C_SET;
typedef DB_COLLECTION DB_C_COLLECTION;
typedef DB_ELO DB_C_ELO;
typedef struct db_c_time DB_C_TIME;
struct db_c_time
{
  int hour;
  int minute;
  int second;
};

typedef struct db_c_date DB_C_DATE;
struct db_c_date
{
  int year;
  int month;
  int day;
};

/* identifiers for the default expression */
typedef enum
{
  DB_DEFAULT_NONE = 0,
  DB_DEFAULT_SYSDATE = 1,
  DB_DEFAULT_SYSDATETIME = 2,
  DB_DEFAULT_SYSTIMESTAMP = 3,
  DB_DEFAULT_UNIX_TIMESTAMP = 4,
  DB_DEFAULT_USER = 5,
  DB_DEFAULT_CURR_USER = 6,
  DB_DEFAULT_CURRENTDATETIME = 7,
  DB_DEFAULT_CURRENTTIMESTAMP = 8,
  DB_DEFAULT_CURRENTTIME = 9,
  DB_DEFAULT_CURRENTDATE = 10,
  DB_DEFAULT_SYSTIME = 11,
} DB_DEFAULT_EXPR_TYPE;

typedef DB_DATETIME DB_C_DATETIME;
typedef DB_DATETIMETZ DB_C_DATETIMETZ;
typedef DB_TIMESTAMP DB_C_TIMESTAMP;
typedef DB_TIMESTAMPTZ DB_C_TIMESTAMPTZ;
typedef DB_MONETARY DB_C_MONETARY;
typedef unsigned char *DB_C_NUMERIC;
typedef void *DB_C_POINTER;
typedef DB_IDENTIFIER DB_C_IDENTIFIER;

/************************************************************************/
/* TODO:Decide how do we handle the references copied from other headers*/
/************************************************************************/

/* From object_primitive.h */
extern int pr_clone_value (const DB_VALUE * src, DB_VALUE * dest);

/* From dbi.h */
extern DB_TYPE db_col_type (DB_COLLECTION * col);

/* From db_date.h */
extern int db_date_encode (DB_DATE * date, int month, int day, int year);
extern void db_date_decode (const DB_DATE * date, int *monthp, int *dayp, int *yearp);
extern int db_time_encode (DB_TIME * timeval, int hour, int minute, int second);

/* From oid.h */
#define OID_ISNULL(oidp)        ((oidp)->pageid == NULL_PAGEID)

/* From set_object.h */
/*
 * struct setobj
 * The internal structure of a setobj data struct is private to this module.
 * all access to this structure should be encapsulated via function calls.
 */

struct setobj
{

  DB_TYPE coltype;
  int size;			/* valid indexes from 0 to size -1 aka the number of represented values in the
				 * collection */
  int lastinsert;		/* the last value insertion point 0 to size. */
  int topblock;			/* maximum index of an allocated block. This is the maximum non-NULL db_value pointer
				 * index of array. array[topblock] should be non-NULL. array[topblock+1] will be a NULL 
				 * pointer for future expansion. */
  int arraytop;			/* maximum indexable pointer in array the valid indexes for array are 0 to arraytop
				 * inclusive Generally this may be greater than topblock */
  int topblockcount;		/* This is the max index of the top block Since it may be shorter than a standard sized 
				 * block for space efficicency. */
  DB_VALUE **array;

  /* not stored on disk, attached at run time by the schema */
  struct tp_domain *domain;

  /* external reference list */
  DB_COLLECTION *references;

  /* clear if we can't guarentee sort order, always on for sequences */
  unsigned sorted:1;

  /* set if we can't guarentee that there are no temporary OID's in here */
  unsigned may_have_temporary_oids:1;
};

/*
 * SETOBJ
 *    This is the primitive set object header.
 */
typedef struct setobj SETOBJ;

typedef SETOBJ COL;

STATIC_INLINE DB_TYPE setobj_type (COL * set) __attribute__ ((ALWAYS_INLINE));

/*
 * setobj_type() - Returns the type of setobj is passed in
 *      return: DB_TYPE
 *  set(in) : set object
 *
 */

STATIC_INLINE DB_TYPE
setobj_type (COL * set)
{
  if (set)
    {
      return set->coltype;
    }
  return DB_TYPE_NULL;
}

extern DB_VALUE *db_value_create (void);
extern DB_VALUE *db_value_copy (DB_VALUE * value);
extern int db_value_clone (DB_VALUE * src, DB_VALUE * dest);
extern int db_value_clear (DB_VALUE * value);
extern int db_value_free (DB_VALUE * value);
extern int db_value_clear_array (DB_VALUE_ARRAY * value_array);
extern void db_value_print (const DB_VALUE * value);
extern int db_value_coerce (const DB_VALUE * src, DB_VALUE * dest, const DB_DOMAIN * desired_domain);

extern int db_value_equal (const DB_VALUE * value1, const DB_VALUE * value2);
extern int db_value_compare (const DB_VALUE * value1, const DB_VALUE * value2);
extern int db_value_domain_init (DB_VALUE * value, DB_TYPE type, const int precision, const int scale);
extern int db_value_domain_min (DB_VALUE * value, DB_TYPE type, const int precision, const int scale, const int codeset,
				const int collation_id, const DB_ENUMERATION * enumeration);
extern int db_value_domain_max (DB_VALUE * value, DB_TYPE type, const int precision, const int scale, const int codeset,
				const int collation_id, const DB_ENUMERATION * enumeration);
extern int db_value_domain_default (DB_VALUE * value, const DB_TYPE type, const int precision, const int scale,
				    const int codeset, const int collation_id, DB_ENUMERATION * enumeration);
extern int db_value_domain_zero (DB_VALUE * value, const DB_TYPE type, const int precision, const int scale);
extern int db_string_truncate (DB_VALUE * value, const int max_precision);
extern DB_TYPE db_value_domain_type (const DB_VALUE * value);
extern DB_TYPE db_value_type (const DB_VALUE * value);
extern int db_value_precision (const DB_VALUE * value);
extern int db_value_scale (const DB_VALUE * value);
extern int db_value_put_null (DB_VALUE * value);
extern int db_value_put (DB_VALUE * value, const DB_TYPE_C c_type, void *input, const int input_length);
extern bool db_value_type_is_collection (const DB_VALUE * value);
extern bool db_value_type_is_numeric (const DB_VALUE * value);
extern bool db_value_type_is_bit (const DB_VALUE * value);
extern bool db_value_type_is_char (const DB_VALUE * value);
extern bool db_value_type_is_internal (const DB_VALUE * value);
extern bool db_value_is_null (const DB_VALUE * value);
extern int db_value_get (DB_VALUE * value, const DB_TYPE_C type, void *buf, const int buflen, int *transferlen,
			 int *outputlen);
extern int db_value_size (const DB_VALUE * value, DB_TYPE_C type, int *size);
extern int db_value_char_size (const DB_VALUE * value, int *size);
extern DB_CURRENCY db_value_get_monetary_currency (const DB_VALUE * value);
extern double db_value_get_monetary_amount_as_double (const DB_VALUE * value);
extern int db_value_put_monetary_currency (DB_VALUE * value, const DB_CURRENCY type);
extern int db_value_put_monetary_amount_as_double (DB_VALUE * value, const double amount);

/*
 * DB_MAKE_ value constructors.
 * These macros are provided to make the construction of DB_VALUE
 * structures easier.  They will fill in the fields from the supplied
 * arguments. It is not necessary to use these macros but is usually more
 * convenient.
 */
STATIC_INLINE int db_make_null (DB_VALUE * value) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int db_make_int (DB_VALUE * value, const int num) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int db_make_float (DB_VALUE * value, const DB_C_FLOAT num) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int db_make_double (DB_VALUE * value, const DB_C_DOUBLE num) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int db_make_object (DB_VALUE * value, DB_C_OBJECT * obj) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int db_make_set (DB_VALUE * value, DB_C_SET * set) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int db_make_multiset (DB_VALUE * value, DB_C_SET * set) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int db_make_sequence (DB_VALUE * value, DB_C_SET * set) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int db_make_collection (DB_VALUE * value, DB_C_SET * set) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int db_make_midxkey (DB_VALUE * value, DB_MIDXKEY * midxkey) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int db_make_elo (DB_VALUE * value, DB_TYPE type, const DB_ELO * elo) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int db_make_time (DB_VALUE * value, const int hour, const int minute, const int second)
  __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int db_make_timetz (DB_VALUE * value, const DB_TIMETZ * timetz_value) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int db_make_timeltz (DB_VALUE * value, const DB_TIME * time_value) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int db_make_date (DB_VALUE * value, const int month, const int day, const int year)
  __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int db_make_timestamp (DB_VALUE * value, const DB_C_TIMESTAMP timeval) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int db_make_timestampltz (DB_VALUE * value, const DB_C_TIMESTAMP ts_val) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int db_make_timestamptz (DB_VALUE * value, const DB_C_TIMESTAMPTZ * ts_tz_val)
  __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int db_make_datetime (DB_VALUE * value, const DB_DATETIME * datetime) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int db_make_datetimeltz (DB_VALUE * value, const DB_DATETIME * datetime) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int db_make_datetimetz (DB_VALUE * value, const DB_DATETIMETZ * datetimetz)
  __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int db_make_monetary (DB_VALUE * value, const DB_CURRENCY type, const double amount)
  __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int db_make_pointer (DB_VALUE * value, DB_C_POINTER ptr) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int db_make_error (DB_VALUE * value, const int errcode) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int db_make_method_error (DB_VALUE * value, const int errcode, const char *errmsg)
  __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int db_make_short (DB_VALUE * value, const DB_C_SHORT num) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int db_make_bigint (DB_VALUE * value, const DB_BIGINT num) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int db_make_string (DB_VALUE * value, const char *str) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int db_make_string_copy (DB_VALUE * value, const char *str) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int db_make_numeric (DB_VALUE * value, const DB_C_NUMERIC num, const int precision, const int scale)
  __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int db_make_bit (DB_VALUE * value, const int bit_length, const DB_C_BIT bit_str,
			       const int bit_str_bit_size) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int db_make_varbit (DB_VALUE * value, const int max_bit_length, const DB_C_BIT bit_str,
				  const int bit_str_bit_size) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int db_make_char (DB_VALUE * value, const int char_length, const DB_C_CHAR str,
				const int char_str_byte_size, const int codeset, const int collation_id)
  __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int db_make_varchar (DB_VALUE * value, const int max_char_length, const DB_C_CHAR str,
				   const int char_str_byte_size, const int codeset, const int collation_id)
  __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int db_make_nchar (DB_VALUE * value, const int nchar_length, const DB_C_NCHAR str,
				 const int nchar_str_byte_size, const int codeset, const int collation_id)
  __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int db_make_varnchar (DB_VALUE * value, const int max_nchar_length, const DB_C_NCHAR str,
				    const int nchar_str_byte_size, const int codeset, const int collation_id)
  __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int db_make_enumeration (DB_VALUE * value, unsigned short index, DB_C_CHAR str, int size,
				       unsigned char codeset, const int collation_id) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int db_make_resultset (DB_VALUE * value, const DB_RESULTSET handle) __attribute__ ((ALWAYS_INLINE));

extern int db_value_put_encoded_time (DB_VALUE * value, const DB_TIME * time_value);
extern int db_value_put_encoded_date (DB_VALUE * value, const DB_DATE * date_value);
extern int db_value_put_numeric (DB_VALUE * value, DB_C_NUMERIC num);
extern DB_CURRENCY db_get_currency_default (void);
extern int db_value_put_varnchar (DB_VALUE * value, DB_C_NCHAR str, int size);
extern int db_value_put_nchar (DB_VALUE * value, DB_C_NCHAR str, int size);
extern int db_value_put_varchar (DB_VALUE * value, DB_C_CHAR str, int size);
extern int db_value_put_char (DB_VALUE * value, DB_C_CHAR str, int size);
extern int db_value_put_varbit (DB_VALUE * value, DB_C_BIT str, int size);
extern int db_value_put_bit (DB_VALUE * value, DB_C_BIT str, int size);

extern int db_string_put_cs_and_collation (DB_VALUE * value, const int codeset, const int collation_id);
extern int db_enum_put_cs_and_collation (DB_VALUE * value, const int codeset, const int collation_id);
extern int valcnv_convert_value_to_string (DB_VALUE * value);

extern int db_get_compressed_size (DB_VALUE * value);
extern void db_set_compressed_string (DB_VALUE * value, char *compressed_string,
				      int compressed_size, bool compressed_need_clear);
extern char *db_get_method_error_msg (void);
extern short db_get_enum_short (const DB_VALUE * value);
STATIC_INLINE char *db_get_enum_string (const DB_VALUE * value) __attribute__ ((ALWAYS_INLINE));
extern int db_get_enum_string_size (const DB_VALUE * value);

/* MACROS FOR ERROR CHECKING */
/* These should be used at the start of every db_ function so we can check
   various validations before executing. */

/* Argument checking macros */
#define CHECK_1ARG_RETURN_EXPR(obj, expr)                                      \
  do {                                                                         \
    if((obj) == NULL) {                                                        \
      er_set(ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_OBJ_INVALID_ARGUMENTS, 0); \
      return (expr);                                                           \
    }                                                                          \
  } while (0)

#define CHECK_2ARGS_RETURN_EXPR(obj1, obj2, expr)                              \
  do {                                                                         \
    if((obj1) == NULL || (obj2) == NULL) {                                     \
      er_set(ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_OBJ_INVALID_ARGUMENTS, 0); \
      return (expr);                                                           \
    }                                                                          \
  } while (0)

#define CHECK_3ARGS_RETURN_EXPR(obj1, obj2, obj3, expr)                        \
  do {                                                                         \
    if((obj1) == NULL || (obj2) == NULL || (obj3) == NULL) {                   \
      er_set(ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_OBJ_INVALID_ARGUMENTS, 0); \
      return (expr);                                                           \
    }                                                                          \
  } while (0)

#define CHECK_1ARG_NULL(obj)        \
  CHECK_1ARG_RETURN_EXPR(obj, NULL)

#define CHECK_2ARGS_NULL(obj1, obj2)    \
  CHECK_2ARGS_RETURN_EXPR(obj1,obj2,NULL)

#define CHECK_3ARGS_NULL(obj1, obj2, obj3) \
  CHECK_3ARGS_RETURN_EXPR(obj1,obj2,obj3,NULL)

#define CHECK_1ARG_FALSE(obj)  \
  CHECK_1ARG_RETURN_EXPR(obj,false)

#define CHECK_1ARG_TRUE(obj)   \
  CHECK_1ARG_RETURN_EXPR(obj, true)

#define CHECK_1ARG_ERROR(obj)  \
  CHECK_1ARG_RETURN_EXPR(obj,ER_OBJ_INVALID_ARGUMENTS)

#define CHECK_1ARG_ERROR_WITH_TYPE(obj, TYPE)  \
  CHECK_1ARG_RETURN_EXPR(obj,(TYPE)ER_OBJ_INVALID_ARGUMENTS)

#define CHECK_1ARG_MINUSONE(obj) \
  CHECK_1ARG_RETURN_EXPR(obj,-1)

#define CHECK_2ARGS_ERROR(obj1, obj2)   \
  CHECK_2ARGS_RETURN_EXPR(obj1, obj2, ER_OBJ_INVALID_ARGUMENTS)

#define CHECK_3ARGS_ERROR(obj1, obj2, obj3) \
  CHECK_3ARGS_RETURN_EXPR(obj1, obj2, obj3, ER_OBJ_INVALID_ARGUMENTS)

#define CHECK_1ARG_ZERO(obj)     \
  CHECK_1ARG_RETURN_EXPR(obj, 0)

#define CHECK_1ARG_ZERO_WITH_TYPE(obj1, RETURN_TYPE)     \
  CHECK_1ARG_RETURN_EXPR(obj1, (RETURN_TYPE) 0)

#define CHECK_2ARGS_ZERO(obj1, obj2)    \
  CHECK_2ARGS_RETURN_EXPR(obj1,obj2, 0)

#define CHECK_1ARG_UNKNOWN(obj1)        \
  CHECK_1ARG_RETURN_EXPR(obj1, DB_TYPE_UNKNOWN)

STATIC_INLINE int db_get_int (const DB_VALUE * value) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE DB_C_SHORT db_get_short (const DB_VALUE * value) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE DB_BIGINT db_get_bigint (const DB_VALUE * value) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE DB_C_CHAR db_get_string (const DB_VALUE * value) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE DB_C_FLOAT db_get_float (const DB_VALUE * value) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE DB_C_DOUBLE db_get_double (const DB_VALUE * value) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE DB_OBJECT *db_get_object (const DB_VALUE * value) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE DB_COLLECTION *db_get_set (const DB_VALUE * value) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE DB_MIDXKEY *db_get_midxkey (const DB_VALUE * value) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE DB_C_POINTER db_get_pointer (const DB_VALUE * value) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE DB_TIME *db_get_time (const DB_VALUE * value) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE DB_TIMETZ *db_get_timetz (const DB_VALUE * value) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE DB_TIMESTAMP *db_get_timestamp (const DB_VALUE * value) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE DB_TIMESTAMPTZ *db_get_timestamptz (const DB_VALUE * value) __attribute__ ((ALWAYS_INLINE));;
STATIC_INLINE DB_DATETIME *db_get_datetime (const DB_VALUE * value) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE DB_DATETIMETZ *db_get_datetimetz (const DB_VALUE * value) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE DB_DATE *db_get_date (const DB_VALUE * value) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE DB_MONETARY *db_get_monetary (const DB_VALUE * value) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int db_get_error (const DB_VALUE * value) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE DB_ELO *db_get_elo (const DB_VALUE * value) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE DB_C_NUMERIC db_get_numeric (const DB_VALUE * value) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE DB_C_BIT db_get_bit (const DB_VALUE * value, int *length) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE DB_C_CHAR db_get_char (const DB_VALUE * value, int *length) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE DB_C_NCHAR db_get_nchar (const DB_VALUE * value, int *length) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int db_get_string_size (const DB_VALUE * value) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int db_get_string_codeset (const DB_VALUE * value) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int db_get_string_collation (const DB_VALUE * value) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE DB_RESULTSET db_get_resultset (const DB_VALUE * value) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int db_get_enum_codeset (const DB_VALUE * value) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int db_get_enum_collation (const DB_VALUE * value) __attribute__ ((ALWAYS_INLINE));

/*
 * db_get_int() -
 * return :
 * value(in):
 */
STATIC_INLINE int
db_get_int (const DB_VALUE * value)
{
#if defined(NO_SERVER_OR_DEBUG_MODE)
  CHECK_1ARG_ZERO (value);
#endif

  assert (value->domain.general_info.type == DB_TYPE_INTEGER);

  return value->data.i;
}

/*
 * db_get_short() -
 * return :
 * value(in):
 */
STATIC_INLINE DB_C_SHORT
db_get_short (const DB_VALUE * value)
{
#if defined(NO_SERVER_OR_DEBUG_MODE)
  CHECK_1ARG_ZERO (value);
#endif

  assert (value->domain.general_info.type == DB_TYPE_SHORT);

  return value->data.sh;
}

/*
 * db_get_bigint() -
 * return :
 * value(in):
 */
STATIC_INLINE DB_BIGINT
db_get_bigint (const DB_VALUE * value)
{
#if defined(NO_SERVER_OR_DEBUG_MODE)
  CHECK_1ARG_ZERO (value);
#endif

  assert (value->domain.general_info.type == DB_TYPE_BIGINT);

  return value->data.bigint;
}

/*
 * db_get_string() -
 * return :
 * value(in):
 */
STATIC_INLINE DB_C_CHAR
db_get_string (const DB_VALUE * value)
{
  char *str = NULL;
  DB_TYPE type;

#if defined(NO_SERVER_OR_DEBUG_MODE)
  CHECK_1ARG_NULL (value);
#endif

  if (value->domain.general_info.is_null || value->domain.general_info.type == DB_TYPE_ERROR)
    {
      return NULL;
    }

  type = DB_VALUE_DOMAIN_TYPE (value);

  /* Needs to be checked !! */
  assert (type == DB_TYPE_VARCHAR || type == DB_TYPE_CHAR || type == DB_TYPE_VARNCHAR
	  || type == DB_TYPE_NCHAR || type == DB_TYPE_VARBIT || type == DB_TYPE_BIT);

  switch (value->data.ch.info.style)
    {
    case SMALL_STRING:
      str = (char *) value->data.ch.sm.buf;
      break;
    case MEDIUM_STRING:
      str = value->data.ch.medium.buf;
      break;
    case LARGE_STRING:
      /* Currently not implemented */
      str = NULL;
      break;
    }

  return str;
}

/*
 * db_get_float() -
 * return :
 * value(in):
 */
STATIC_INLINE DB_C_FLOAT
db_get_float (const DB_VALUE * value)
{
#if defined(NO_SERVER_OR_DEBUG_MODE)
  CHECK_1ARG_ZERO (value);
#endif

  assert (value->domain.general_info.type == DB_TYPE_FLOAT);

  return value->data.f;
}

/*
 * db_get_double() -
 * return :
 * value(in):
 */
STATIC_INLINE DB_C_DOUBLE
db_get_double (const DB_VALUE * value)
{
#if defined(NO_SERVER_OR_DEBUG_MODE)
  CHECK_1ARG_ZERO (value);
#endif

  assert (value->domain.general_info.type == DB_TYPE_DOUBLE);

  return value->data.d;
}

/*
 * db_get_object() -
 * return :
 * value(in):
 */
STATIC_INLINE DB_OBJECT *
db_get_object (const DB_VALUE * value)
{
#if defined(NO_SERVER_OR_DEBUG_MODE)
  CHECK_1ARG_NULL (value);
#endif

  if (value->domain.general_info.is_null || value->domain.general_info.type == DB_TYPE_ERROR)
    {
      return NULL;
    }
  else
    {
      return value->data.op;
    }
}

/*
 * db_get_set() -
 * return :
 * value(in):
 */
STATIC_INLINE DB_COLLECTION *
db_get_set (const DB_VALUE * value)
{
#if defined(NO_SERVER_OR_DEBUG_MODE)
  CHECK_1ARG_NULL (value);
#endif

  if (value->domain.general_info.is_null || value->domain.general_info.type == DB_TYPE_ERROR)
    {
      return NULL;
    }
  else
    {
      /* Needs to be checked !! */
      assert (DB_VALUE_DOMAIN_TYPE (value) == DB_TYPE_SET || DB_VALUE_DOMAIN_TYPE (value) == DB_TYPE_MULTISET
	      || DB_VALUE_DOMAIN_TYPE (value) == DB_TYPE_SEQUENCE || DB_VALUE_DOMAIN_TYPE (value) == DB_TYPE_VOBJ);

      return value->data.set;
    }
}

/*
 * db_get_midxkey() -
 * return :
 * value(in):
 */
STATIC_INLINE DB_MIDXKEY *
db_get_midxkey (const DB_VALUE * value)
{
#if defined(NO_SERVER_OR_DEBUG_MODE)
  CHECK_1ARG_NULL (value);
#endif

  if (value->domain.general_info.is_null || value->domain.general_info.type == DB_TYPE_ERROR)
    {
      return NULL;
    }
  else
    {
      /* This one needs to be checked !! */
      assert (DB_VALUE_DOMAIN_TYPE (value) == DB_TYPE_MIDXKEY);
      return (DB_MIDXKEY *) (&(value->data.midxkey));
    }
}

/*
 * db_get_pointer() -
 * return :
 * value(in):
 */
STATIC_INLINE DB_C_POINTER
db_get_pointer (const DB_VALUE * value)
{
#if defined(NO_SERVER_OR_DEBUG_MODE)
  CHECK_1ARG_NULL (value);
#endif

  if (value->domain.general_info.is_null || value->domain.general_info.type == DB_TYPE_ERROR)
    {
      return NULL;
    }
  else
    {
      /* Needs to be checked !! */
      assert (DB_VALUE_DOMAIN_TYPE (value) == DB_TYPE_POINTER);
      return value->data.p;
    }
}

/*
 * db_get_time() -
 * return :
 * value(in):
 */
STATIC_INLINE DB_TIME *
db_get_time (const DB_VALUE * value)
{
#if defined(NO_SERVER_OR_DEBUG_MODE)
  CHECK_1ARG_NULL (value);
#endif

  assert (value->domain.general_info.type == DB_TYPE_TIME || value->domain.general_info.type == DB_TYPE_TIMELTZ);

  return ((DB_TIME *) (&value->data.time));
}

/*
 * db_get_timetz() -
 * return :
 * value(in):
 */
STATIC_INLINE DB_TIMETZ *
db_get_timetz (const DB_VALUE * value)
{
#if defined(NO_SERVER_OR_DEBUG_MODE)
  CHECK_1ARG_NULL (value);
#endif

  assert (value->domain.general_info.type == DB_TYPE_TIMETZ);

  return ((DB_TIMETZ *) (&value->data.timetz));
}

/*
 * db_get_timestamp() -
 * return :
 * value(in):
 */
STATIC_INLINE DB_TIMESTAMP *
db_get_timestamp (const DB_VALUE * value)
{
#if defined(NO_SERVER_OR_DEBUG_MODE)
  CHECK_1ARG_NULL (value);
#endif

  assert (value->domain.general_info.type == DB_TYPE_TIMESTAMP
	  || value->domain.general_info.type == DB_TYPE_TIMESTAMPLTZ);

  return ((DB_TIMESTAMP *) (&value->data.utime));
}

/*
 * db_get_timestamptz() -
 * return :
 * value(in):
 */
STATIC_INLINE DB_TIMESTAMPTZ *
db_get_timestamptz (const DB_VALUE * value)
{
#if defined(NO_SERVER_OR_DEBUG_MODE)
  CHECK_1ARG_NULL (value);
#endif

  assert (value->domain.general_info.type == DB_TYPE_TIMESTAMPTZ);

  return ((DB_TIMESTAMPTZ *) (&value->data.timestamptz));
}

/*
 * db_get_datetime() -
 * return :
 * value(in):
 */
STATIC_INLINE DB_DATETIME *
db_get_datetime (const DB_VALUE * value)
{
#if defined(NO_SERVER_OR_DEBUG_MODE)
  CHECK_1ARG_NULL (value);
#endif

  assert (value->domain.general_info.type == DB_TYPE_DATETIME
	  || value->domain.general_info.type == DB_TYPE_DATETIMELTZ);

  return ((DB_DATETIME *) (&value->data.datetime));
}

/*
 * db_get_datetimetz() -
 * return :
 * value(in):
 */
STATIC_INLINE DB_DATETIMETZ *
db_get_datetimetz (const DB_VALUE * value)
{
#if defined(NO_SERVER_OR_DEBUG_MODE)
  CHECK_1ARG_NULL (value);
#endif

  assert (value->domain.general_info.type == DB_TYPE_DATETIMETZ);

  return ((DB_DATETIMETZ *) (&value->data.datetimetz));
}

/*
 * db_get_date() -
 * return :
 * value(in):
 */
STATIC_INLINE DB_DATE *
db_get_date (const DB_VALUE * value)
{
#if defined(NO_SERVER_OR_DEBUG_MODE)
  CHECK_1ARG_NULL (value);
#endif

  assert (value->domain.general_info.type == DB_TYPE_DATE);

  return ((DB_DATE *) (&value->data.date));
}

/*
 * db_get_monetary() -
 * return :
 * value(in):
 */
STATIC_INLINE DB_MONETARY *
db_get_monetary (const DB_VALUE * value)
{
#if defined(NO_SERVER_OR_DEBUG_MODE)
  CHECK_1ARG_NULL (value);
#endif

  assert (value->domain.general_info.type == DB_TYPE_MONETARY);

  return ((DB_MONETARY *) (&value->data.money));
}

/*
 * db_get_error() -
 * return :
 * value(in):
 */
STATIC_INLINE int
db_get_error (const DB_VALUE * value)
{
#if defined(NO_SERVER_OR_DEBUG_MODE)
  CHECK_1ARG_ZERO (value);
#endif

  assert (value->domain.general_info.type == DB_TYPE_ERROR);

  return value->data.error;
}

/*
 * db_get_elo() -
 * return :
 * value(in):
 */
STATIC_INLINE DB_ELO *
db_get_elo (const DB_VALUE * value)
{
#if defined(NO_SERVER_OR_DEBUG_MODE)
  CHECK_1ARG_NULL (value);
#endif

  if (value->domain.general_info.is_null || DB_VALUE_DOMAIN_TYPE (value) == DB_TYPE_ERROR)
    {
      return NULL;
    }
  else if (value->data.elo.type == ELO_NULL)
    {
      return NULL;
    }
  else
    {
      /* Needs to be checked !! */
      assert (DB_VALUE_DOMAIN_TYPE (value) == DB_TYPE_ELO || DB_VALUE_DOMAIN_TYPE (value) == DB_TYPE_CLOB
	      || DB_VALUE_DOMAIN_TYPE (value) == DB_TYPE_BLOB);

      return (DB_ELO *) (&value->data.elo);
    }
}

/*
 * db_get_numeric() -
 * return :
 * value(in):
 */
STATIC_INLINE DB_C_NUMERIC
db_get_numeric (const DB_VALUE * value)
{
#if defined(NO_SERVER_OR_DEBUG_MODE)
  CHECK_1ARG_ZERO (value);
#endif

  if (value->domain.general_info.is_null || value->domain.general_info.type == DB_TYPE_ERROR)
    {
      return NULL;
    }
  else
    {
      /* Needs to be checked !! */
      assert (DB_VALUE_DOMAIN_TYPE (value) == DB_TYPE_NUMERIC);
      return (DB_C_NUMERIC) value->data.num.d.buf;
    }
}

/*
 * db_get_bit() -
 * return :
 * value(in):
 * length(out):
 */
STATIC_INLINE DB_C_BIT
db_get_bit (const DB_VALUE * value, int *length)
{
  char *str = NULL;

#if defined(NO_SERVER_OR_DEBUG_MODE)
  CHECK_1ARG_NULL (value);
  CHECK_1ARG_NULL (length);
#endif

  if (value->domain.general_info.is_null)
    {
      return NULL;
    }

  /* Needs to be checked !! */
  assert (DB_VALUE_DOMAIN_TYPE (value) == DB_TYPE_BIT || DB_VALUE_DOMAIN_TYPE (value) == DB_TYPE_VARBIT);

  switch (value->data.ch.info.style)
    {
    case SMALL_STRING:
      {
	*length = value->data.ch.sm.size;
	str = (char *) value->data.ch.sm.buf;
      }
      break;
    case MEDIUM_STRING:
      {
	*length = value->data.ch.medium.size;
	str = value->data.ch.medium.buf;
      }
      break;
    case LARGE_STRING:
      {
	/* Currently not implemented */
	*length = 0;
	str = NULL;
      }
      break;
    }

  return str;
}

/*
 * db_get_char() -
 * return :
 * value(in):
 * length(out):
 */
STATIC_INLINE DB_C_CHAR
db_get_char (const DB_VALUE * value, int *length)
{
  char *str = NULL;
  DB_TYPE type;

#if defined(NO_SERVER_OR_DEBUG_MODE)
  CHECK_1ARG_NULL (value);
  CHECK_1ARG_NULL (length);
#endif

  if (value->domain.general_info.is_null || value->domain.general_info.type == DB_TYPE_ERROR)
    {
      return NULL;
    }

  type = DB_VALUE_DOMAIN_TYPE (value);

  assert (type == DB_TYPE_VARCHAR || type == DB_TYPE_CHAR || type == DB_TYPE_VARNCHAR
	  || type == DB_TYPE_NCHAR || type == DB_TYPE_VARBIT || type == DB_TYPE_BIT);

  switch (value->data.ch.info.style)
    {
    case SMALL_STRING:
      {
	str = (char *) value->data.ch.sm.buf;
	intl_char_count ((unsigned char *) str, value->data.ch.sm.size, (INTL_CODESET) value->data.ch.info.codeset,
			 length);
      }
      break;
    case MEDIUM_STRING:
      {
	str = value->data.ch.medium.buf;
	intl_char_count ((unsigned char *) str, value->data.ch.medium.size, (INTL_CODESET) value->data.ch.info.codeset,
			 length);
      }
      break;
    case LARGE_STRING:
      {
	/* Currently not implemented */
	str = NULL;
	*length = 0;
      }
      break;
    }

  return str;
}

/*
 * db_get_nchar() -
 * return :
 * value(in):
 * length(out):
 */
STATIC_INLINE DB_C_NCHAR
db_get_nchar (const DB_VALUE * value, int *length)
{
  return db_get_char (value, length);
}

/*
 * db_get_string_size() -
 * return :
 * value(in):
 */
STATIC_INLINE int
db_get_string_size (const DB_VALUE * value)
{
  int size = 0;

#if defined(NO_SERVER_OR_DEBUG_MODE)
  CHECK_1ARG_ZERO (value);
#endif

  switch (value->data.ch.info.style)
    {
    case SMALL_STRING:
      size = value->data.ch.sm.size;
      break;
    case MEDIUM_STRING:
      size = value->data.ch.medium.size;
      break;
    case LARGE_STRING:
      /* Currently not implemented */
      size = 0;
      break;
    }

  /* Convert the number of bits to the number of bytes */
  if (value->domain.general_info.type == DB_TYPE_BIT || value->domain.general_info.type == DB_TYPE_VARBIT)
    {
      size = (size + 7) / 8;
    }

  return size;
}

/*
 * db_get_string_codeset() -
 * return :
 * value(in):
 */
STATIC_INLINE int
db_get_string_codeset (const DB_VALUE * value)
{
#if defined(NO_SERVER_OR_DEBUG_MODE)
  CHECK_1ARG_ZERO_WITH_TYPE (value, INTL_CODESET);
#endif

  return value->data.ch.info.codeset;
}

/*
 * db_get_string_collation() -
 * return :
 * value(in):
 */
STATIC_INLINE int
db_get_string_collation (const DB_VALUE * value)
{
#if defined(NO_SERVER_OR_DEBUG_MODE)
  CHECK_1ARG_ZERO_WITH_TYPE (value, int);
#endif

  return value->domain.char_info.collation_id;
}

/*
 * db_get_resultset() -
 * return :
 * value(in):
 */
STATIC_INLINE DB_RESULTSET
db_get_resultset (const DB_VALUE * value)
{
#if defined(NO_SERVER_OR_DEBUG_MODE)
  CHECK_1ARG_ZERO (value);
#endif

  /* Needs to be checked !! */
  assert (value->domain.general_info.type == DB_TYPE_RESULTSET);

  return value->data.rset;
}


/*
 * db_get_enum_codeset() -
 * return :
 * value(in):
 */
STATIC_INLINE int
db_get_enum_codeset (const DB_VALUE * value)
{
#if defined(NO_SERVER_OR_DEBUG_MODE)
  CHECK_1ARG_ZERO_WITH_TYPE (value, INTL_CODESET);
#endif

  return value->data.enumeration.str_val.info.codeset;
}

/*
 * db_get_enum_collation() -
 * return :
 * value(in):
 */
STATIC_INLINE int
db_get_enum_collation (const DB_VALUE * value)
{
#if defined(NO_SERVER_OR_DEBUG_MODE)
  CHECK_1ARG_ZERO_WITH_TYPE (value, int);
#endif

  return value->domain.char_info.collation_id;
}

/************************************************************************/
/* TODO:Decide how do we handle the references copied from other headers*/
/************************************************************************/

/* From storage_common.h */
#define NULL_VOLID  (-1)	/* Value of an invalid volume identifier */
#define NULL_SECTID (-1)	/* Value of an invalid sector identifier */
#define NULL_PAGEID (-1)	/* Value of an invalid page identifier */
#define NULL_SLOTID (-1)	/* Value of an invalid slot identifier */
#define NULL_OFFSET (-1)	/* Value of an invalid offset */
#define NULL_FILEID (-1)	/* Value of an invalid file identifier */

/* From elo.c */
static const DB_ELO elo_Initializer = { -1LL, {{NULL_PAGEID, 0}, {NULL_FILEID, 0}}, NULL, NULL, ELO_NULL, ES_NONE };

/* From elo.h */
STATIC_INLINE void elo_init_structure (DB_ELO * elo) __attribute__ ((ALWAYS_INLINE));

/*
 * elo_init_structure () - init. ELO structure
 */
STATIC_INLINE void
elo_init_structure (DB_ELO * elo)
{
  if (elo != NULL)
    {
      *elo = elo_Initializer;
    }
}

/*
 * db_make_null() -
 * return :
 * value(out) :
 */
STATIC_INLINE int
db_make_null (DB_VALUE * value)
{
#if defined(NO_SERVER_OR_DEBUG_MODE)
  CHECK_1ARG_ERROR (value);
#endif

  value->domain.general_info.type = DB_TYPE_NULL;
  value->domain.general_info.is_null = 1;
  value->need_clear = false;

  return NO_ERROR;
}

/*
 * db_make_int() -
 * return :
 * value(out) :
 * num(in):
 */
STATIC_INLINE int
db_make_int (DB_VALUE * value, const int num)
{
#if defined(NO_SERVER_OR_DEBUG_MODE)
  CHECK_1ARG_ERROR (value);
#endif

  value->domain.general_info.type = DB_TYPE_INTEGER;
  value->data.i = num;
  value->domain.general_info.is_null = 0;
  value->need_clear = false;

  return NO_ERROR;
}

/*
 * db_make_short() -
 * return :
 * value(out) :
 * num(in) :
 */
STATIC_INLINE int
db_make_short (DB_VALUE * value, const short num)
{
#if defined(NO_SERVER_OR_DEBUG_MODE)
  CHECK_1ARG_ERROR (value);
#endif

  value->domain.general_info.type = DB_TYPE_SHORT;
  value->data.sh = num;
  value->domain.general_info.is_null = 0;
  value->need_clear = false;

  return NO_ERROR;
}

/*
 * db_make_bigint() -
 * return :
 * value(out) :
 * num(in) :
 */
STATIC_INLINE int
db_make_bigint (DB_VALUE * value, const DB_BIGINT num)
{
#if defined(NO_SERVER_OR_DEBUG_MODE)
  CHECK_1ARG_ERROR (value);
#endif

  value->domain.general_info.type = DB_TYPE_BIGINT;
  value->data.bigint = num;
  value->domain.general_info.is_null = 0;
  value->need_clear = false;

  return NO_ERROR;
}

/*
 * db_make_float() -
 * return :
 * value(out) :
 * num(in):
 */
STATIC_INLINE int
db_make_float (DB_VALUE * value, const float num)
{
#if defined(NO_SERVER_OR_DEBUG_MODE)
  CHECK_1ARG_ERROR (value);
#endif

  value->domain.general_info.type = DB_TYPE_FLOAT;
  value->data.f = num;
  value->domain.general_info.is_null = 0;
  value->need_clear = false;

  return NO_ERROR;
}

/*
 * db_make_double() -
 * return :
 * value(out) :
 * num(in):
 */
STATIC_INLINE int
db_make_double (DB_VALUE * value, const double num)
{
#if defined(NO_SERVER_OR_DEBUG_MODE)
  CHECK_1ARG_ERROR (value);
#endif

  value->domain.general_info.type = DB_TYPE_DOUBLE;
  value->data.d = num;
  value->domain.general_info.is_null = 0;
  value->need_clear = false;

  return NO_ERROR;
}

/*
 * db_make_numeric() -
 * return :
 * value(out) :
 * num(in):
 * precision(in):
 * scale(in):
 */
STATIC_INLINE int
db_make_numeric (DB_VALUE * value, const DB_C_NUMERIC num, const int precision, const int scale)
{
  int error = NO_ERROR;

#if defined(NO_SERVER_OR_DEBUG_MODE)
  CHECK_1ARG_ERROR (value);
#endif

  error = db_value_domain_init (value, DB_TYPE_NUMERIC, precision, scale);
  if (error != NO_ERROR)
    {
      return error;
    }

  if (num)
    {
      value->domain.general_info.is_null = 0;
      memcpy (value->data.num.d.buf, num, DB_NUMERIC_BUF_SIZE);
    }
  else
    {
      value->domain.general_info.is_null = 1;
    }
  return error;
}

/*
 * db_make_db_char() -
 * return :
 * value(out) :
 * codeset(in):
 * collation_id(in):
 * str(in):
 * size(in):
 */
STATIC_INLINE int
db_make_db_char (DB_VALUE * value, const INTL_CODESET codeset, const int collation_id, const char *str, const int size)
{
  int error = NO_ERROR;
  bool is_char_type;

#if defined(NO_SERVER_OR_DEBUG_MODE)
  CHECK_1ARG_ERROR (value);
#endif

  is_char_type = (value->domain.general_info.type == DB_TYPE_VARCHAR || value->domain.general_info.type == DB_TYPE_CHAR
		  || value->domain.general_info.type == DB_TYPE_NCHAR
		  || value->domain.general_info.type == DB_TYPE_VARNCHAR
		  || value->domain.general_info.type == DB_TYPE_BIT
		  || value->domain.general_info.type == DB_TYPE_VARBIT);

  if (is_char_type)
    {
#if 0
      if (size <= DB_SMALL_CHAR_BUF_SIZE)
	{
	  value->data.ch.info.style = SMALL_STRING;
	  value->data.ch.sm.codeset = codeset;
	  value->data.ch.sm.size = size;
	  memcpy (value->data.ch.sm.buf, str, size);
	}
      else
#endif
      if (size <= DB_MAX_STRING_LENGTH)
	{
	  value->data.ch.info.style = MEDIUM_STRING;
	  value->data.ch.info.codeset = codeset;
	  value->domain.char_info.collation_id = collation_id;
	  value->data.ch.info.is_max_string = false;
	  value->data.ch.info.compressed_need_clear = false;
	  value->data.ch.medium.compressed_buf = NULL;
	  value->data.ch.medium.compressed_size = 0;
	  /* 
	   * If size is set to the default, and the type is any
	   * kind of character string, assume the string is NULL
	   * terminated.
	   */
	  if (size == DB_DEFAULT_STRING_LENGTH && QSTR_IS_ANY_CHAR (value->domain.general_info.type))
	    {
	      value->data.ch.medium.size = str ? strlen (str) : 0;
	    }
	  else if (size < 0)
	    {
	      error = ER_QSTR_BAD_LENGTH;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QSTR_BAD_LENGTH, 1, size);
	    }
	  else
	    {
	      /* We need to ensure that we don't exceed the max size for the char value specified in the domain. */
	      if (value->domain.char_info.length == TP_FLOATING_PRECISION_VALUE || LANG_VARIABLE_CHARSET (codeset))
		{
		  value->data.ch.medium.size = size;
		}
	      else
		{
		  value->data.ch.medium.size = MIN (size, value->domain.char_info.length);
		}
	    }
	  value->data.ch.medium.buf = (char *) str;
	}
      else
	{
	  /* case LARGE_STRING: Currently Not Implemented */
	}

      if (str)
	{
	  value->domain.general_info.is_null = 0;
	}
      else
	{
	  value->domain.general_info.is_null = 1;
	}

      if (size == 0 && prm_get_bool_value (PRM_ID_ORACLE_STYLE_EMPTY_STRING))
	{
	  value->domain.general_info.is_null = 1;
	}

      value->need_clear = false;
    }
  else
    {
      error = ER_QPROC_INVALID_DATATYPE;
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_DATATYPE, 0);
    }
  return error;
}

/*
 * db_make_bit() -
 * return :
 * value(out) :
 * bit_length(in):
 * bit_str(in):
 * bit_str_bit_size(in):
 */
STATIC_INLINE int
db_make_bit (DB_VALUE * value, const int bit_length, const DB_C_BIT bit_str, const int bit_str_bit_size)
{
  int error;

#if defined(NO_SERVER_OR_DEBUG_MODE)
  CHECK_1ARG_ERROR (value);
#endif

  error = db_value_domain_init (value, DB_TYPE_BIT, bit_length, 0);
  if (error != NO_ERROR)
    {
      return error;
    }

  error = db_make_db_char (value, INTL_CODESET_RAW_BITS, 0, bit_str, bit_str_bit_size);
  return error;
}

/*
 * db_make_varbit() -
 * return :
 * value(out) :
 * max_bit_length(in):
 * bit_str(in):
 * bit_str_bit_size(in):
 */
STATIC_INLINE int
db_make_varbit (DB_VALUE * value, const int max_bit_length, const DB_C_BIT bit_str, const int bit_str_bit_size)
{
  int error;
#if defined(NO_SERVER_OR_DEBUG_MODE)
  CHECK_1ARG_ERROR (value);
#endif

  error = db_value_domain_init (value, DB_TYPE_VARBIT, max_bit_length, 0);
  if (error != NO_ERROR)
    {
      return error;
    }

  error = db_make_db_char (value, INTL_CODESET_RAW_BITS, 0, bit_str, bit_str_bit_size);

  return error;
}

/*
 * db_make_string() -
 * return :
 * value(out) :
 * str(in):
 */
STATIC_INLINE int
db_make_string (DB_VALUE * value, const char *str)
{
  int error;
  int size;

#if defined(NO_SERVER_OR_DEBUG_MODE)
  CHECK_1ARG_ERROR (value);
#endif

  error = db_value_domain_init (value, DB_TYPE_VARCHAR, TP_FLOATING_PRECISION_VALUE, 0);
  if (error == NO_ERROR)
    {
      if (str)
	{
	  size = strlen (str);
	}
      else
	{
	  size = 0;
	}
      error = db_make_db_char (value, LANG_SYS_CODESET, LANG_SYS_COLLATION, str, size);
    }
  return error;
}


/*
 * db_make_string_copy() - alloc buffer and copy str into the buffer.
 *                         need_clear will set as true.
 * return :
 * value(out) :
 * str(in):
 */
STATIC_INLINE int
db_make_string_copy (DB_VALUE * value, const char *str)
{
  int error;
  DB_VALUE tmp_value;

#if defined(NO_SERVER_OR_DEBUG_MODE)
  CHECK_1ARG_ERROR (value);
#endif

  error = db_make_string (&tmp_value, str);
  if (error == NO_ERROR)
    {
      error = pr_clone_value (&tmp_value, value);
    }

  return error;
}

/*
 * db_make_char() -
 * return :
 * value(out) :
 * char_length(in):
 * str(in):
 * char_str_byte_size(in):
 */
STATIC_INLINE int
db_make_char (DB_VALUE * value, const int char_length, const DB_C_CHAR str, const int char_str_byte_size,
	      const int codeset, const int collation_id)
{
  int error;

#if defined(NO_SERVER_OR_DEBUG_MODE)
  CHECK_1ARG_ERROR (value);
#endif

  error = db_value_domain_init (value, DB_TYPE_CHAR, char_length, 0);
  if (error == NO_ERROR)
    {
      error = db_make_db_char (value, codeset, collation_id, str, char_str_byte_size);
    }

  return error;
}

/*
 * db_make_varchar() -
 * return :
 * value(out) :
 * max_char_length(in):
 * str(in):
 * char_str_byte_size(in):
 */
STATIC_INLINE int
db_make_varchar (DB_VALUE * value, const int max_char_length, const DB_C_CHAR str, const int char_str_byte_size,
		 const int codeset, const int collation_id)
{
  int error;

#if defined(NO_SERVER_OR_DEBUG_MODE)
  CHECK_1ARG_ERROR (value);
#endif

  error = db_value_domain_init (value, DB_TYPE_VARCHAR, max_char_length, 0);
  if (error == NO_ERROR)
    {
      error = db_make_db_char (value, codeset, collation_id, str, char_str_byte_size);
    }

  return error;
}

/*
 * db_make_nchar() -
 * return :
 * value(out) :
 * nchar_length(in):
 * str(in):
 * nchar_str_byte_size(in):
 */
STATIC_INLINE int
db_make_nchar (DB_VALUE * value, const int nchar_length, const DB_C_NCHAR str, const int nchar_str_byte_size,
	       const int codeset, const int collation_id)
{
  int error;

#if defined(NO_SERVER_OR_DEBUG_MODE)
  CHECK_1ARG_ERROR (value);
#endif

  error = db_value_domain_init (value, DB_TYPE_NCHAR, nchar_length, 0);
  if (error == NO_ERROR)
    {
      error = db_make_db_char (value, codeset, collation_id, str, nchar_str_byte_size);
    }

  return error;
}

/*
 * db_make_varnchar() -
 * return :
 * value(out) :
 * max_nchar_length(in):
 * str(in):
 * nchar_str_byte_size(in):
 */
STATIC_INLINE int
db_make_varnchar (DB_VALUE * value, const int max_nchar_length, const DB_C_NCHAR str, const int nchar_str_byte_size,
		  const int codeset, const int collation_id)
{
  int error;

#if defined(NO_SERVER_OR_DEBUG_MODE)
  CHECK_1ARG_ERROR (value);
#endif

  error = db_value_domain_init (value, DB_TYPE_VARNCHAR, max_nchar_length, 0);
  if (error == NO_ERROR)
    {
      error = db_make_db_char (value, codeset, collation_id, str, nchar_str_byte_size);
    }

  return error;
}

/*
 * db_make_object() -
 * return :
 * value(out) :
 * obj(in):
 */
STATIC_INLINE int
db_make_object (DB_VALUE * value, DB_OBJECT * obj)
{
#if defined(NO_SERVER_OR_DEBUG_MODE)
  CHECK_1ARG_ERROR (value);
#endif

  value->domain.general_info.type = DB_TYPE_OBJECT;
  value->data.op = obj;
  if (obj)
    {
      value->domain.general_info.is_null = 0;
    }
  else
    {
      value->domain.general_info.is_null = 1;
    }

  value->need_clear = false;

  return NO_ERROR;
}

/*
 * db_make_set() -
 * return :
 * value(out) :
 * set(in):
 */
STATIC_INLINE int
db_make_set (DB_VALUE * value, DB_SET * set)
{
  int error = NO_ERROR;

#if defined(NO_SERVER_OR_DEBUG_MODE)
  CHECK_1ARG_ERROR (value);
#endif

  value->domain.general_info.type = DB_TYPE_SET;
  value->data.set = set;
  if (set)
    {
      if ((set->set && setobj_type (set->set) == DB_TYPE_SET) || set->disk_set)
	{
	  value->domain.general_info.is_null = 0;
	}
      else
	{
	  error = ER_QPROC_INVALID_DATATYPE;
	  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_DATATYPE, 0);
	}
    }
  else
    {
      value->domain.general_info.is_null = 1;
    }

  value->need_clear = false;

  return error;
}

/*
 * db_make_multiset() -
 * return :
 * value(out) :
 * set(in):
 */
STATIC_INLINE int
db_make_multiset (DB_VALUE * value, DB_SET * set)
{
  int error = NO_ERROR;

#if defined(NO_SERVER_OR_DEBUG_MODE)
  CHECK_1ARG_ERROR (value);
#endif

  value->domain.general_info.type = DB_TYPE_MULTISET;
  value->data.set = set;
  if (set)
    {
      if ((set->set && setobj_type (set->set) == DB_TYPE_MULTISET) || set->disk_set)
	{
	  value->domain.general_info.is_null = 0;
	}
      else
	{
	  error = ER_QPROC_INVALID_DATATYPE;
	  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_DATATYPE, 0);
	}
    }
  else
    {
      value->domain.general_info.is_null = 1;
    }

  value->need_clear = false;

  return error;
}

/*
 * db_make_sequence() -
 * return :
 * value(out) :
 * set(in):
 */
STATIC_INLINE int
db_make_sequence (DB_VALUE * value, DB_SET * set)
{
  int error = NO_ERROR;

#if defined(NO_SERVER_OR_DEBUG_MODE)
  CHECK_1ARG_ERROR (value);
#endif

  value->domain.general_info.type = DB_TYPE_SEQUENCE;
  value->data.set = set;
  if (set)
    {
      if ((set->set && setobj_type (set->set) == DB_TYPE_SEQUENCE) || set->disk_set)
	{
	  value->domain.general_info.is_null = 0;
	}
      else
	{
	  error = ER_QPROC_INVALID_DATATYPE;
	  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_DATATYPE, 0);
	}
    }
  else
    {
      value->domain.general_info.is_null = 1;
    }

  value->need_clear = false;

  return error;
}

/*
 * db_make_collection() -
 * return :
 * value(out) :
 * col(in):
 */
STATIC_INLINE int
db_make_collection (DB_VALUE * value, DB_COLLECTION * col)
{
  int error = NO_ERROR;

#if defined(NO_SERVER_OR_DEBUG_MODE)
  CHECK_1ARG_ERROR (value);
#endif

  /* Rather than being DB_TYPE_COLLECTION, the value type is taken from the base type of the collection. */
  if (col == NULL)
    {
      value->domain.general_info.type = DB_TYPE_SEQUENCE;	/* undefined */
      value->data.set = NULL;
      value->domain.general_info.is_null = 1;
    }
  else
    {
      value->domain.general_info.type = db_col_type (col);
      value->data.set = col;
      /* note, we have been testing set->set for non-NULL here in order to set the is_null flag, this isn't
       * appropriate, the set pointer can be NULL if the set has been swapped out.The existance of a set handle alone
       * determines the nullness of the value.  Actually, the act of calling db_col_type above will have resulted in a
       * re-fetch of the referenced set if it had been swapped out. */
      value->domain.general_info.is_null = 0;
    }
  value->need_clear = false;

  return error;
}

/*
 * db_make_midxkey() -
 * return :
 * value(out) :
 * midxkey(in):
 */
STATIC_INLINE int
db_make_midxkey (DB_VALUE * value, DB_MIDXKEY * midxkey)
{
  int error = NO_ERROR;

#if defined(NO_SERVER_OR_DEBUG_MODE)
  CHECK_1ARG_ERROR (value);
#endif

  value->domain.general_info.type = DB_TYPE_MIDXKEY;

  if (midxkey == NULL)
    {
      value->domain.general_info.is_null = 1;
      value->data.midxkey.ncolumns = -1;
      value->data.midxkey.domain = NULL;
      value->data.midxkey.size = 0;
      value->data.midxkey.buf = NULL;
      value->data.midxkey.min_max_val.position = -1;
      value->data.midxkey.min_max_val.type = MIN_COLUMN;
    }
  else
    {
      value->domain.general_info.is_null = 0;
      value->data.midxkey = *midxkey;
    }

  value->need_clear = false;

  return error;
}

/*
 * db_make_elo () -
 * return:
 * value(out):
 * type(in):
 * elo(in):
 */
STATIC_INLINE int
db_make_elo (DB_VALUE * value, DB_TYPE type, const DB_ELO * elo)
{
#if defined(NO_SERVER_OR_DEBUG_MODE)
  CHECK_1ARG_ERROR (value);
#endif

  value->domain.general_info.type = type;
  if (elo == NULL || elo->size < 0 || elo->type == ELO_NULL)
    {
      elo_init_structure (&value->data.elo);
      value->domain.general_info.is_null = 1;
    }
  else
    {
      value->data.elo = *elo;
      value->domain.general_info.is_null = 0;
    }
  value->need_clear = false;

  return NO_ERROR;
}

/*
 * db_make_pointer() -
 * return :
 * value(out) :
 * ptr(in):
 */
STATIC_INLINE int
db_make_pointer (DB_VALUE * value, void *ptr)
{
#if defined(NO_SERVER_OR_DEBUG_MODE)
  CHECK_1ARG_ERROR (value);
#endif

  value->domain.general_info.type = DB_TYPE_POINTER;
  value->data.p = ptr;
  if (ptr)
    {
      value->domain.general_info.is_null = 0;
    }
  else
    {
      value->domain.general_info.is_null = 1;
    }
  value->need_clear = false;

  return NO_ERROR;
}

/*
 * db_make_time() -
 * return :
 * value(out) :
 * hour(in):
 * min(in):
 * sec(in):
 */
STATIC_INLINE int
db_make_time (DB_VALUE * value, const int hour, const int min, const int sec)
{
#if defined(NO_SERVER_OR_DEBUG_MODE)
  CHECK_1ARG_ERROR (value);
#endif

  value->domain.general_info.type = DB_TYPE_TIME;
  value->domain.general_info.is_null = 0;
  value->need_clear = false;
  return db_time_encode (&value->data.time, hour, min, sec);
}

/*
 * db_make_timetz() -
 * return :
 * value(out) :
 * hour(in):
 * min(in):
 * sec(in):
 */
STATIC_INLINE int
db_make_timetz (DB_VALUE * value, const DB_TIMETZ * timetz_value)
{
#if defined(NO_SERVER_OR_DEBUG_MODE)
  CHECK_1ARG_ERROR (value);
#endif

  value->domain.general_info.type = DB_TYPE_TIMETZ;
  value->need_clear = false;
  if (timetz_value)
    {
      value->data.timetz.time = timetz_value->time;
      value->data.timetz.tz_id = timetz_value->tz_id;
      value->domain.general_info.is_null = 0;
    }
  else
    {
      value->domain.general_info.is_null = 1;
    }

  return NO_ERROR;
}

/*
 * db_make_timeltz() -
 * return :
 * value(out) :
 * hour(in):
 * min(in):
 * sec(in):
 */
STATIC_INLINE int
db_make_timeltz (DB_VALUE * value, const DB_TIME * time_value)
{
#if defined(NO_SERVER_OR_DEBUG_MODE)
  CHECK_1ARG_ERROR (value);
#endif

  value->domain.general_info.type = DB_TYPE_TIMELTZ;
  value->need_clear = false;
  if (time_value)
    {
      value->data.time = *time_value;
      value->domain.general_info.is_null = 0;
    }
  else
    {
      value->domain.general_info.is_null = 1;
    }

  return NO_ERROR;
}

/*
 * db_make_date() -
 * return :
 * value(out):
 * mon(in):
 * day(in):
 * year(in):
 */
STATIC_INLINE int
db_make_date (DB_VALUE * value, const int mon, const int day, const int year)
{
#if defined(NO_SERVER_OR_DEBUG_MODE)
  CHECK_1ARG_ERROR (value);
#endif

  value->domain.general_info.type = DB_TYPE_DATE;
  value->domain.general_info.is_null = 0;
  value->need_clear = false;
  return db_date_encode (&value->data.date, mon, day, year);
}

/*
 * db_make_monetary() -
 * return :
 * value(out):
 * type(in):
 * amount(in):
 */
STATIC_INLINE int
db_make_monetary (DB_VALUE * value, const DB_CURRENCY type, const double amount)
{
  int error;
#if defined(NO_SERVER_OR_DEBUG_MODE)
  CHECK_1ARG_ERROR (value);
#endif

  /* check for valid currency type don't put default case in the switch!!! */
  error = ER_INVALID_CURRENCY_TYPE;
  switch (type)
    {
    case DB_CURRENCY_DOLLAR:
    case DB_CURRENCY_YEN:
    case DB_CURRENCY_WON:
    case DB_CURRENCY_TL:
    case DB_CURRENCY_BRITISH_POUND:
    case DB_CURRENCY_CAMBODIAN_RIEL:
    case DB_CURRENCY_CHINESE_RENMINBI:
    case DB_CURRENCY_INDIAN_RUPEE:
    case DB_CURRENCY_RUSSIAN_RUBLE:
    case DB_CURRENCY_AUSTRALIAN_DOLLAR:
    case DB_CURRENCY_CANADIAN_DOLLAR:
    case DB_CURRENCY_BRASILIAN_REAL:
    case DB_CURRENCY_ROMANIAN_LEU:
    case DB_CURRENCY_EURO:
    case DB_CURRENCY_SWISS_FRANC:
    case DB_CURRENCY_DANISH_KRONE:
    case DB_CURRENCY_NORWEGIAN_KRONE:
    case DB_CURRENCY_BULGARIAN_LEV:
    case DB_CURRENCY_VIETNAMESE_DONG:
    case DB_CURRENCY_CZECH_KORUNA:
    case DB_CURRENCY_POLISH_ZLOTY:
    case DB_CURRENCY_SWEDISH_KRONA:
    case DB_CURRENCY_CROATIAN_KUNA:
    case DB_CURRENCY_SERBIAN_DINAR:
      error = NO_ERROR;		/* it's a type we expect */
      break;
    default:
      break;
    }

  if (error != NO_ERROR)
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, error, 1, type);
      return error;
    }

  value->domain.general_info.type = DB_TYPE_MONETARY;
  value->data.money.type = type;
  value->data.money.amount = amount;
  value->domain.general_info.is_null = 0;
  value->need_clear = false;

  return error;
}

/*
 * db_make_timestamp() -
 * return :
 * value(out):
 * timeval(in):
 */
STATIC_INLINE int
db_make_timestamp (DB_VALUE * value, const DB_TIMESTAMP timeval)
{
#if defined(NO_SERVER_OR_DEBUG_MODE)
  CHECK_1ARG_ERROR (value);
#endif

  value->domain.general_info.type = DB_TYPE_TIMESTAMP;
  value->data.utime = timeval;
  value->domain.general_info.is_null = 0;
  value->need_clear = false;

  return NO_ERROR;
}

/*
 * db_make_timestampltz() -
 * return :
 * value(out):
 * timeval(in):
 */
STATIC_INLINE int
db_make_timestampltz (DB_VALUE * value, const DB_TIMESTAMP ts_val)
{
#if defined(NO_SERVER_OR_DEBUG_MODE)
  CHECK_1ARG_ERROR (value);
#endif

  value->domain.general_info.type = DB_TYPE_TIMESTAMPLTZ;
  value->data.utime = ts_val;
  value->domain.general_info.is_null = 0;
  value->need_clear = false;

  return NO_ERROR;
}

/*
 * db_make_timestamptz() -
 * return :
 * value(out):
 * timeval(in):
 */
STATIC_INLINE int
db_make_timestamptz (DB_VALUE * value, const DB_TIMESTAMPTZ * ts_tz_val)
{
#if defined(NO_SERVER_OR_DEBUG_MODE)
  CHECK_1ARG_ERROR (value);
#endif

  value->domain.general_info.type = DB_TYPE_TIMESTAMPTZ;
  if (ts_tz_val)
    {
      value->data.timestamptz = *ts_tz_val;
      value->domain.general_info.is_null = 0;
    }
  else
    {
      value->domain.general_info.is_null = 1;
    }
  value->need_clear = false;

  return NO_ERROR;
}

/*
 * db_make_datetime() -
 * return :
 * value(out):
 * date(in):
 */
STATIC_INLINE int
db_make_datetime (DB_VALUE * value, const DB_DATETIME * datetime)
{
#if defined(NO_SERVER_OR_DEBUG_MODE)
  CHECK_1ARG_ERROR (value);
#endif

  value->domain.general_info.type = DB_TYPE_DATETIME;
  if (datetime)
    {
      value->data.datetime = *datetime;
      value->domain.general_info.is_null = 0;
    }
  else
    {
      value->domain.general_info.is_null = 1;
    }
  value->need_clear = false;

  return NO_ERROR;
}

/*
 * db_make_datetimeltz() -
 * return :
 * value(out):
 * date(in):
 */
STATIC_INLINE int
db_make_datetimeltz (DB_VALUE * value, const DB_DATETIME * datetime)
{
#if defined(NO_SERVER_OR_DEBUG_MODE)
  CHECK_1ARG_ERROR (value);
#endif

  value->domain.general_info.type = DB_TYPE_DATETIMELTZ;
  if (datetime)
    {
      value->data.datetime = *datetime;
      value->domain.general_info.is_null = 0;
    }
  else
    {
      value->domain.general_info.is_null = 1;
    }
  value->need_clear = false;

  return NO_ERROR;
}

/*
 * db_make_datetimetz() -
 * return :
 * value(out):
 * date(in):
 */
STATIC_INLINE int
db_make_datetimetz (DB_VALUE * value, const DB_DATETIMETZ * datetimetz)
{
#if defined(NO_SERVER_OR_DEBUG_MODE)
  CHECK_1ARG_ERROR (value);
#endif

  value->domain.general_info.type = DB_TYPE_DATETIMETZ;
  if (datetimetz)
    {
      value->data.datetimetz = *datetimetz;
      value->domain.general_info.is_null = 0;
    }
  else
    {
      value->domain.general_info.is_null = 1;
    }
  value->need_clear = false;

  return NO_ERROR;
}

/*
 * db_make_enumeration() -
 * return :
 * value(out):
 * index(in):
 * str(in):
 * size(in):
 * codeset(in):
 * collation_id(in):
 */
STATIC_INLINE int
db_make_enumeration (DB_VALUE * value, unsigned short index, DB_C_CHAR str, int size, unsigned char codeset,
		     const int collation_id)
{
#if defined(NO_SERVER_OR_DEBUG_MODE)
  CHECK_1ARG_ERROR (value);
#endif

  value->domain.general_info.type = DB_TYPE_ENUMERATION;
  value->data.enumeration.short_val = index;
  value->data.enumeration.str_val.info.codeset = codeset;
  value->domain.char_info.collation_id = collation_id;
  value->data.enumeration.str_val.info.style = MEDIUM_STRING;
  value->data.ch.info.is_max_string = false;
  value->data.ch.info.compressed_need_clear = false;
  value->data.ch.medium.compressed_buf = NULL;
  value->data.ch.medium.compressed_size = 0;
  value->data.enumeration.str_val.medium.size = size;
  value->data.enumeration.str_val.medium.buf = str;
  value->domain.general_info.is_null = 0;
  value->need_clear = false;

  return NO_ERROR;
}

/*
 * db_make_error() -
 * return :
 * value(out):
 * errcode(in):
 */
STATIC_INLINE int
db_make_error (DB_VALUE * value, const int errcode)
{
#if defined(NO_SERVER_OR_DEBUG_MODE)
  CHECK_1ARG_ERROR (value);
#endif

  assert (errcode != NO_ERROR);

  value->domain.general_info.type = DB_TYPE_ERROR;
  value->data.error = errcode;
  value->domain.general_info.is_null = 0;
  value->need_clear = false;

  return NO_ERROR;
}

/*
 * db_make_method_error() -
 * return :
 * value(out):
 * errcode(in):
 * errmsg(in);
 */
STATIC_INLINE int
db_make_method_error (DB_VALUE * value, const int errcode, const char *errmsg)
{
#if defined(NO_SERVER_OR_DEBUG_MODE)
  CHECK_1ARG_ERROR (value);
#endif

  value->domain.general_info.type = DB_TYPE_ERROR;
  value->data.error = errcode;
  value->domain.general_info.is_null = 0;
  value->need_clear = false;

#if !defined(SERVER_MODE)
  if (obj_Method_error_msg)
    {
      free (obj_Method_error_msg);	/* free old last error */
    }
  obj_Method_error_msg = NULL;
  if (errmsg)
    {
      obj_Method_error_msg = strdup (errmsg);
    }
#endif

  return NO_ERROR;
}

/*
 * db_make_oid() -
 * return :
 * value(out):
 * oid(in):
 */
STATIC_INLINE int
db_make_oid (DB_VALUE * value, const OID * oid)
{
#if defined(NO_SERVER_OR_DEBUG_MODE)
  CHECK_2ARGS_ERROR (value, oid);
#endif

  if (!oid || OID_ISNULL (oid))
    {
      value->domain.general_info.is_null = 1;
      return NO_ERROR;
    }

  value->domain.general_info.type = DB_TYPE_OID;
  value->data.oid.pageid = oid->pageid;
  value->data.oid.slotid = oid->slotid;
  value->data.oid.volid = oid->volid;
  value->domain.general_info.is_null = OID_ISNULL (oid);
  value->need_clear = false;

  return NO_ERROR;
}

/*
 * db_make_resultset() -
 * return :
 * value(out):
 * handle(in):
 */
STATIC_INLINE int
db_make_resultset (DB_VALUE * value, const DB_RESULTSET handle)
{
#if defined(NO_SERVER_OR_DEBUG_MODE)
  CHECK_1ARG_ERROR (value);
#endif

  value->domain.general_info.type = DB_TYPE_RESULTSET;
  value->data.rset = handle;
  value->domain.general_info.is_null = 0;
  value->need_clear = false;

  return NO_ERROR;
}


#define DB_GET_INTEGER(value)           db_get_int(value)

#define DB_GET_INT                      DB_GET_INTEGER

#define DB_GET_BIGINT(value)            db_get_bigint(value)

#define DB_GET_BIGINTEGER               DB_GET_BIGINT

#define DB_GET_FLOAT(value)             db_get_float(value)

#define DB_GET_DOUBLE(value)            db_get_double(value)

#define DB_GET_STRING(value)            db_get_string(value)

#define DB_GET_OBJECT(value)            db_get_object(value)

#define DB_GET_OBJ DB_GET_OBJECT

#define DB_GET_SET(value)               db_get_set(value)

#define DB_GET_MULTISET(value)          db_get_set(value)

#define DB_GET_COLLECTION(value)        db_get_set(value)

#define DB_GET_MIDXKEY(value)           db_get_midxkey(value)

#define DB_GET_ELO(value)               db_get_elo(value)

#define DB_GET_TIME(value)              db_get_time(value)

#define DB_GET_TIMETZ(value)          db_get_timetz(value)

#define DB_GET_DATE(value)              db_get_date(value)

#define DB_GET_TIMESTAMP(value)         db_get_timestamp(value)

#define DB_GET_TIMESTAMPTZ(value)     db_get_timestamptz(value)

#define DB_GET_DATETIME(value)          db_get_datetime(value)

#define DB_GET_DATETIMETZ(value)      db_get_datetimetz(value)

#define DB_GET_MONETARY(value)          db_get_monetary(value)

#define DB_GET_POINTER(value)           db_get_pointer(value)

#define DB_GET_ERROR(value)             db_get_error(value)

#define DB_GET_SHORT(value)             db_get_short(value)

#define DB_GET_SMALLINT(value)          db_get_short(value)

#define DB_GET_NUMERIC(value)           db_get_numeric(value)

#define DB_GET_BIT(value, length)       db_get_bit(value, length)

#define DB_GET_CHAR(value, length)      db_get_char(value, length)

#define DB_GET_NCHAR(value, length)     db_get_nchar(value, length)

#define DB_GET_STRING_SIZE(value)       db_get_string_size(value)

#define DB_GET_METHOD_ERROR_MSG()       db_get_method_error_msg()

#define DB_GET_RESULTSET(value)         db_get_resultset(value)

#define DB_GET_STRING_LENGTH(value) db_get_string_length(value)

#define DB_GET_STRING_CODESET(value) db_get_string_codeset(value)

#define DB_GET_STRING_COLLATION(value) db_get_string_collation(value)

#define DB_GET_ENUM_CODESET(value) db_get_enum_codeset(value)

#define DB_GET_ENUM_COLLATION(value) db_get_enum_collation(value)

/* obsolete */
#define DB_GET_MULTI_SET DB_GET_MULTISET

#define DB_GET_LIST(value)              db_get_set(value)

#define DB_GET_SEQUENCE DB_GET_LIST

/* obsolete */
#define DB_GET_SEQ DB_GET_SEQUENCE

/*
 * db_get_enum_string () -
 * return :
 * value(in):
 */
STATIC_INLINE char *
db_get_enum_string (const DB_VALUE * value)
{
  CHECK_1ARG_ZERO (value);
  if (value->domain.general_info.is_null || value->domain.general_info.type == DB_TYPE_ERROR)
    {
      return NULL;
    }
  return value->data.enumeration.str_val.medium.buf;
}

#define DB_GET_ENUM_STRING(v) db_get_enum_string(v)


#endif /* _DBTYPE_H_ */
