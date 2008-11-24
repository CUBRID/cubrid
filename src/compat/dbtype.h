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
 * dbtype.h - Definitions related to the memory representations of database
 * attribute values. This is an application interface file. It should contain
 * only definitions available to CUBRID customer applications.
 */

#ifndef _DBTYPE_H_
#define _DBTYPE_H_

#ident "$Id$"

#include "config.h"

#include "dbdef.h"

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

/* Maximum allowable schema name. */
#define DB_MAX_SCHEMA_LENGTH DB_MAX_USER_LENGTH

/* Maximum allowable class name. */
#define DB_MAX_CLASS_LENGTH (DB_MAX_IDENTIFIER_LENGTH-DB_MAX_SCHEMA_LENGTH-4)

#define DB_MAX_SPEC_LENGTH       4096

/* This constant defines the maximum length of a character
   string that can be used as the value of an attribute. */
#define DB_MAX_STRING_LENGTH	0x3fffffff

/* This constant defines the maximum length of a bit string
   that can be used as the value of an attribute. */
#define DB_MAX_BIT_LENGTH 0x3fffffff

/* The maximum precision that can be specified for a numeric domain. */
#define DB_MAX_NUMERIC_PRECISION 38

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

/* This is for backward compatibility, shouldn't be using this anymore */
#define DB_TYPE_MULTI_SET DB_TYPE_MULTISET

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

#define DB_MAKE_ELO(value, elo) db_make_elo(value, elo)

#define DB_MAKE_TIME(value, hour, minute, second) \
    db_make_time(value, hour, minute, second)

#define DB_MAKE_ENCODED_TIME(value, time_value) \
    db_value_put_encoded_time(value, time_value)

#define DB_MAKE_DATE(value, month, day, year) \
    db_make_date(value, month, day, year)

#define DB_MAKE_ENCODED_DATE(value, date_value) \
    db_value_put_encoded_date(value, date_value)

#define DB_MAKE_TIMESTAMP(value, timeval) \
    db_make_timestamp(value, timeval)

#define DB_MAKE_UTIME DB_MAKE_TIMESTAMP
#define DB_MAKE_MONETARY_AMOUNT(value, amount) \
    db_make_monetary(value, DB_CURRENCY_DEFAULT, amount)

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

#define DB_MAKE_CHAR(value, char_length, str, char_str_byte_size) \
        db_make_char(value, char_length, str, char_str_byte_size)

#define DB_MAKE_VARCHAR(value, max_char_length, str, char_str_byte_size) \
        db_make_varchar(value, max_char_length, str, char_str_byte_size)

#define DB_MAKE_STRING(value, str) db_make_string(value, str)

#define DB_MAKE_NCHAR(value, nchar_length, str, nchar_str_byte_size) \
        db_make_nchar(value, nchar_length, str, nchar_str_byte_size)

#define DB_MAKE_VARNCHAR(value, max_nchar_length, str, nchar_str_byte_size)\
        db_make_varnchar(value, max_nchar_length, str, nchar_str_byte_size)

#define DB_MAKE_RESULTSET(value, handle) db_make_resultset(value, handle)

#define db_get_collection db_get_set
#define db_get_utime db_get_timestamp

#define DB_IS_NULL(value)               db_value_is_null(value)

#define DB_VALUE_DOMAIN_TYPE(value)     db_value_domain_type(value)

#define DB_VALUE_TYPE(value)            db_value_type(value)

#define DB_VALUE_PRECISION(value)       db_value_precision(value)

#define DB_VALUE_SCALE(value)           db_value_scale(value)

#define DB_GET_INTEGER(value)           db_get_int(value)

#define DB_GET_INT DB_GET_INTEGER

#define DB_GET_FLOAT(value)             db_get_float(value)

#define DB_GET_DOUBLE(value)            db_get_double(value)

#define DB_GET_STRING(value)            db_get_string(value)

#define DB_GET_OBJECT(value)            db_get_object(value)

#define DB_GET_OBJ DB_GET_OBJECT

#define DB_GET_SET(value)               db_get_set(value)

#define DB_GET_MULTISET(value)          db_get_set(value)

/* obsolete */
#define DB_GET_MULTI_SET DB_GET_MULTISET

#define DB_GET_LIST(value)              db_get_set(value)

#define DB_GET_SEQUENCE DB_GET_LIST

/* obsolete */
#define DB_GET_SEQ DB_GET_SEQUENCE

/* new preferred interface */
#define DB_GET_COLLECTION(value)        db_get_set(value)

#define DB_GET_MIDXKEY(value)           db_get_midxkey(value)

#define DB_GET_ELO(value)               db_get_elo(value)

#define DB_GET_TIME(value)              db_get_time(value)

#define DB_GET_DATE(value)              db_get_date(value)

#define DB_GET_TIMESTAMP(value)         db_get_timestamp(value)
#define DB_GET_UTIME DB_GET_TIMESTAMP

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

/* TODO : remove this??? */
/* These are the limits of the integer types: DB_INT16, DB_UINT16,
   DB_INT32 & DB_UINT32. */
#define DB_INT16_MIN   (-(DB_INT16_MAX)-1)
#define DB_INT16_MAX   0x7FFFL
#define DB_UINT16_MAX  0xFFFFUL
#define DB_INT32_MIN   (-(DB_INT32_MAX)-1)
#define DB_INT32_MAX   0x7FFFFFFFL
#define DB_UINT32_MIN  0
#define DB_UINT32_MAX  0xFFFFFFFFUL

#define DB_DATE_MIN        DB_UINT32_MIN
#define DB_DATE_MAX        DB_UINT32_MAX

#define DB_TIME_MIN        DB_UINT32_MIN
#define DB_TIME_MAX        DB_UINT32_MAX

#define DB_UTIME_MIN       DB_UINT32_MIN
#define DB_UTIME_MAX       DB_UINT32_MAX

/* This defines the basic type identifier constants.  These are used in
   the domain specifications of attributes and method arguments and
   as value type tags in the DB_VALUE structures. */
typedef enum
{
  DB_TYPE_FIRST = 0,		/* first for iteration   */
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
  DB_TYPE_NUMERIC = 22,		/* SQL NUMERIC(p,s) values      */
  DB_TYPE_BIT = 23,		/* SQL BIT(n) values            */
  DB_TYPE_VARBIT = 24,		/* SQL BIT(n) VARYING values    */
  DB_TYPE_CHAR = 25,		/* SQL CHAR(n) values   */
  DB_TYPE_NCHAR = 26,		/* SQL NATIONAL CHAR(n) values  */
  DB_TYPE_VARNCHAR = 27,	/* SQL NATIONAL CHAR(n) VARYING values  */
  DB_TYPE_RESULTSET = 28,	/* internal use only */
  DB_TYPE_MIDXKEY = 29,		/* internal use only */
  DB_TYPE_TABLE = 30,		/* internal use only */
  DB_TYPE_LIST = DB_TYPE_SEQUENCE,
  DB_TYPE_SMALLINT = DB_TYPE_SHORT,	/* SQL SMALLINT           */
  DB_TYPE_VARCHAR = DB_TYPE_STRING,	/* SQL CHAR(n) VARYING values   */
  DB_TYPE_UTIME = DB_TYPE_TIMESTAMP,	/* SQL TIMESTAMP  */

  DB_TYPE_LAST = DB_TYPE_TABLE
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
  } char_info;
};

/* Structure used for the representation of time values. */
typedef unsigned int DB_TIME;

/* Structure used for the representation of universal times.
   These are compatible with the Unix time_t definition. */
typedef unsigned int DB_TIMESTAMP;

typedef DB_TIMESTAMP DB_UTIME;

/* Structure used for the representation of date values. */
typedef unsigned int DB_DATE;

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

/* Structure used for the representation of monetary amounts. */
typedef enum
{
  DB_CURRENCY_DOLLAR,
  DB_CURRENCY_YEN,
  DB_CURRENCY_POUND,
  DB_CURRENCY_WON,
  DB_CURRENCY_NULL
} DB_CURRENCY;

typedef struct db_monetary DB_MONETARY;
struct db_monetary
{
  double amount;
  DB_CURRENCY type;
};

/* Definition for the collection descriptor structure. The structures for
 * the collection descriptors and the sequence descriptors are identical
 * internally but not all db_collection functions can be used with sequences
 * and no db_seq functions can be used with sets. It is advisable to
 * recognize the type of set being used, type it appropriately and only
 * call those db_ functions defined for that type.
 */
typedef struct db_collection DB_COLLECTION;
typedef DB_COLLECTION DB_MULTISET;
typedef DB_COLLECTION DB_SEQ;
typedef DB_COLLECTION DB_SET;


typedef struct db_midxkey DB_MIDXKEY;
struct db_midxkey
{
  int size;			/* size of buf */
  int ncolumns;			/* # of elements */
  DB_DOMAIN *domain;		/* MIDXKEY domain */
  char *buf;			/* key structure */
};

/* This is used only by the system Glo objects.  User defined
   classes are not allowed to use these data types. */
typedef struct db_elo DB_ELO;

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
  } info;
  struct
  {
    unsigned char style;
    unsigned char codeset;
    unsigned char size;
    char buf[DB_SMALL_CHAR_BUF_SIZE];
  } sm;
  struct
  {
    unsigned char style;
    unsigned char codeset;
    int size;
    char *buf;
  } medium;
  struct
  {
    unsigned char style;
    unsigned char codeset;
    DB_LARGE_STRING *str;
  } large;
};

typedef DB_CHAR DB_NCHAR;
typedef DB_CHAR DB_BIT;

typedef int DB_RESULTSET;

/* A union of all of the possible basic type values.  This is used in the
 * definition of the DB_VALUE which is the fundamental structure used
 * in passing data in and out of the db_ function layer.
 */

typedef union db_data DB_DATA;
union db_data
{
  int i;
  short sh;
  float f;
  double d;
  void *p;
  DB_OBJECT *op;
  DB_TIME time;
  DB_DATE date;
  DB_TIMESTAMP utime;
  DB_MONETARY money;
  DB_COLLECTION *set;
  DB_COLLECTION *collect;
  DB_MIDXKEY midxkey;
  DB_ELO *elo;
  int error;
  DB_IDENTIFIER oid;
  DB_NUMERIC num;
  DB_CHAR ch;
  DB_RESULTSET rset;
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
  int obj_desc_total;		/* total # of object descriptors allocated  */
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
  DB_TYPE_C_LAST,		/* last for iteration   */
  DB_TYPE_C_UTIME = DB_TYPE_C_TIMESTAMP
} DB_TYPE_C;

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

typedef DB_TIMESTAMP DB_C_TIMESTAMP;
typedef DB_MONETARY DB_C_MONETARY;
typedef unsigned char *DB_C_NUMERIC;
typedef void *DB_C_POINTER;
typedef DB_IDENTIFIER DB_C_IDENTIFIER;

extern DB_VALUE *db_value_create (void);
extern DB_VALUE *db_value_copy (DB_VALUE * value);
extern int db_value_clone (DB_VALUE * src, DB_VALUE * dest);
extern int db_value_clear (DB_VALUE * value);
extern int db_value_free (DB_VALUE * value);
extern void db_value_print (const DB_VALUE * value);
extern int db_value_coerce (const DB_VALUE * src,
			    DB_VALUE * dest,
			    const DB_DOMAIN * desired_domain);

extern int db_value_equal (const DB_VALUE * value1, const DB_VALUE * value2);
extern int db_value_compare (const DB_VALUE * value1,
			     const DB_VALUE * value2);
extern int db_value_domain_init (DB_VALUE * value, DB_TYPE type,
				 const int precision, const int scale);
extern int db_value_domain_min (DB_VALUE * value, DB_TYPE type,
				const int precision, const int scale);
extern int db_value_domain_max (DB_VALUE * value, DB_TYPE type,
				const int precision, const int scale);
extern int db_string_truncate (DB_VALUE * value, const int max_precision);
extern DB_TYPE db_value_domain_type (const DB_VALUE * value);
extern DB_TYPE db_value_type (const DB_VALUE * value);
extern int db_value_precision (const DB_VALUE * value);
extern int db_value_scale (const DB_VALUE * value);
extern int db_value_put_null (DB_VALUE * value);
extern int db_value_put (DB_VALUE * value, const DB_TYPE_C c_type,
			 void *input, const int input_length);
extern bool db_value_type_is_collection (const DB_VALUE * value);
extern bool db_value_type_is_numeric (const DB_VALUE * value);
extern bool db_value_type_is_bit (const DB_VALUE * value);
extern bool db_value_type_is_char (const DB_VALUE * value);
extern bool db_value_type_is_internal (const DB_VALUE * value);
extern bool db_value_is_null (const DB_VALUE * value);
extern int db_value_get (DB_VALUE * value,
			 const DB_TYPE_C type,
			 void *buf,
			 const int buflen, int *transferlen, int *outputlen);
extern int db_value_size (const DB_VALUE * value, DB_TYPE_C type, int *size);
extern int db_value_char_size (const DB_VALUE * value, int *size);
extern DB_CURRENCY db_value_get_monetary_currency (const DB_VALUE * value);
extern double db_value_get_monetary_amount_as_double (const DB_VALUE * value);
extern int db_value_put_monetary_currency (DB_VALUE * value,
					   const DB_CURRENCY type);
extern int db_value_put_monetary_amount_as_double (DB_VALUE * value,
						   const double amount);

/*
 * DB_MAKE_ value constructors.
 * These macros are provided to make the construction of DB_VALUE
 * structures easier.  They will fill in the fields from the supplied
 * arguments. It is not necessary to use these macros but is usually more
 * convenient.
 */
extern int db_make_null (DB_VALUE * value);
extern int db_make_int (DB_VALUE * value, const int num);
extern int db_make_float (DB_VALUE * value, const DB_C_FLOAT num);
extern int db_make_double (DB_VALUE * value, const DB_C_DOUBLE num);
extern int db_make_object (DB_VALUE * value, DB_C_OBJECT * obj);
extern int db_make_set (DB_VALUE * value, DB_C_SET * set);
extern int db_make_multiset (DB_VALUE * value, DB_C_SET * set);
extern int db_make_sequence (DB_VALUE * value, DB_C_SET * set);
extern int db_make_collection (DB_VALUE * value, DB_C_SET * set);
extern int db_make_midxkey (DB_VALUE * value, DB_MIDXKEY * midxkey);
extern int db_make_elo (DB_VALUE * value, DB_C_ELO * elo);
extern int db_make_time (DB_VALUE * value,
			 const int hour, const int minute, const int second);
extern int db_value_put_encoded_time (DB_VALUE * value,
				      const DB_TIME * time_value);
extern int db_make_date (DB_VALUE * value,
			 const int month, const int day, const int year);
extern int db_value_put_encoded_date (DB_VALUE * value,
				      const DB_DATE * date_value);
extern int db_make_timestamp (DB_VALUE * value, const DB_C_TIMESTAMP timeval);
extern int db_make_monetary (DB_VALUE * value,
			     const DB_CURRENCY type, const double amount);
extern int db_make_pointer (DB_VALUE * value, DB_C_POINTER ptr);
extern int db_make_error (DB_VALUE * value, const int errcode);
extern int db_make_method_error (DB_VALUE * value,
				 const int errcode, const char *errmsg);
extern int db_make_short (DB_VALUE * value, const DB_C_SHORT num);
extern int db_make_string (DB_VALUE * value, const char *str);
extern int db_make_numeric (DB_VALUE * value,
			    const DB_C_NUMERIC num,
			    const int precision, const int scale);
extern int db_value_put_numeric (DB_VALUE * value, DB_C_NUMERIC num);
extern int db_make_bit (DB_VALUE * value, const int bit_length,
			const DB_C_BIT bit_str, const int bit_str_bit_size);
extern int db_value_put_bit (DB_VALUE * value, DB_C_BIT str, int size);
extern int db_make_varbit (DB_VALUE * value, const int max_bit_length,
			   const DB_C_BIT bit_str,
			   const int bit_str_bit_size);
extern int db_value_put_varbit (DB_VALUE * value, DB_C_BIT str, int size);
extern int db_make_char (DB_VALUE * value, const int char_length,
			 const DB_C_CHAR str, const int char_str_byte_size);
extern int db_value_put_char (DB_VALUE * value, DB_C_CHAR str, int size);
extern int db_make_varchar (DB_VALUE * value, const int max_char_length,
			    const DB_C_CHAR str,
			    const int char_str_byte_size);
extern int db_value_put_varchar (DB_VALUE * value, DB_C_CHAR str, int size);
extern int db_make_nchar (DB_VALUE * value, const int nchar_length,
			  const DB_C_NCHAR str,
			  const int nchar_str_byte_size);
extern int db_value_put_nchar (DB_VALUE * value, DB_C_NCHAR str, int size);
extern int db_make_varnchar (DB_VALUE * value,
			     const int max_nchar_length,
			     const DB_C_NCHAR str,
			     const int nchar_str_byte_size);
extern int db_value_put_varnchar (DB_VALUE * value, DB_C_NCHAR str, int size);

extern DB_CURRENCY db_get_currency_default (void);

extern int db_make_resultset (DB_VALUE * value, const DB_RESULTSET handle);

/*
 * DB_GET_ accessor macros.
 * These macros can be used to extract a particular value from a
 * DB_VALUE structure. No type checking is done so you need to make sure
 * that the type is correct.
 */
extern int db_get_int (const DB_VALUE * value);
extern DB_C_SHORT db_get_short (const DB_VALUE * value);
extern DB_C_CHAR db_get_string (const DB_VALUE * value);
extern DB_C_FLOAT db_get_float (const DB_VALUE * value);
extern DB_C_DOUBLE db_get_double (const DB_VALUE * value);
extern DB_OBJECT *db_get_object (const DB_VALUE * value);
extern DB_COLLECTION *db_get_set (const DB_VALUE * value);
extern DB_MIDXKEY *db_get_midxkey (const DB_VALUE * value);
extern DB_C_POINTER db_get_pointer (const DB_VALUE * value);
extern DB_TIME *db_get_time (const DB_VALUE * value);
extern DB_TIMESTAMP *db_get_timestamp (const DB_VALUE * value);
extern DB_DATE *db_get_date (const DB_VALUE * value);
extern DB_MONETARY *db_get_monetary (const DB_VALUE * value);
extern int db_get_error (const DB_VALUE * value);
extern DB_ELO *db_get_elo (const DB_VALUE * value);
extern DB_C_NUMERIC db_get_numeric (const DB_VALUE * value);
extern DB_C_BIT db_get_bit (const DB_VALUE * value, int *length);
extern DB_C_CHAR db_get_char (const DB_VALUE * value, int *length);
extern DB_C_NCHAR db_get_nchar (const DB_VALUE * value, int *length);
extern int db_get_string_size (const DB_VALUE * value);

extern DB_C_CHAR db_get_method_error_msg (void);

extern DB_RESULTSET db_get_resultset (const DB_VALUE * value);

#endif /* _DBTYPE_H_ */
