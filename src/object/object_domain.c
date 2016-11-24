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
 * object_domain.c: type, domain and value operations.
 * This module primarily defines support for domain structures.
 */

#ident "$Id$"


#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <float.h>
#include <math.h>
#include <errno.h>

#include "memory_alloc.h"
#include "area_alloc.h"

#include "mprec.h"
#include "object_representation.h"
#include "db.h"

#include "object_primitive.h"
#include "object_domain.h"

#include "work_space.h"
#if !defined (SERVER_MODE)
#include "virtual_object.h"
#include "object_accessor.h"
#else /* SERVER_MODE */
#include "object_accessor.h"
#endif /* !SERVER_MODE */
#include "set_object.h"

#include "string_opfunc.h"
#include "cnv.h"
#include "cnverr.h"
#include "tz_support.h"

#if !defined (SERVER_MODE)
#include "schema_manager.h"
#include "locator_cl.h"
#endif /* !SERVER_MODE */

#if defined (SERVER_MODE)
#include "connection_error.h"
#include "language_support.h"
#include "xserver_interface.h"
#endif /* SERVER_MODE */

#include "server_interface.h"
#include "chartype.h"


/* this must be the last header file included!!! */
#include "dbval.h"

#if !defined (SERVER_MODE)
#define pthread_mutex_init(a, b)
#define pthread_mutex_destroy(a)
#define pthread_mutex_lock(b) 0
#define pthread_mutex_unlock(a)
static int rv;
#endif /* !SERVER_MODE */

/*
 * used by versant_driver to avoid doing foolish things
 * like au_fetch_instance on DB_TYPE_OBJECT values that
 * contain versant (not CUBRID) mops.
 */

#define ARE_COMPARABLE(typ1, typ2)                        \
    ((typ1 == typ2) ||                                    \
     (QSTR_IS_CHAR(typ1) && QSTR_IS_CHAR(typ2)) ||    \
     (QSTR_IS_NATIONAL_CHAR(typ1) && QSTR_IS_NATIONAL_CHAR(typ2)))

#define DBL_MAX_DIGITS    ((int)ceil(DBL_MAX_EXP * log10((double) FLT_RADIX)))

#define TP_NEAR_MATCH(t1, t2)                                       \
         (((t1) == (t2)) ||                                         \
	  ((t1) == DB_TYPE_CHAR     && (t2) == DB_TYPE_VARCHAR) ||  \
	  ((t1) == DB_TYPE_VARCHAR  && (t2) == DB_TYPE_CHAR) ||     \
	  ((t1) == DB_TYPE_NCHAR    && (t2) == DB_TYPE_VARNCHAR) || \
	  ((t1) == DB_TYPE_VARNCHAR && (t2) == DB_TYPE_VARCHAR) ||  \
	  ((t1) == DB_TYPE_BIT      && (t2) == DB_TYPE_VARBIT) ||   \
	  ((t1) == DB_TYPE_VARBIT   && (t2) == DB_TYPE_BIT))

#define TP_NUM_MIDXKEY_DOMAIN_LIST      (10)

#define DB_DATETIMETZ_INITIALIZER { {0, 0}, 0 }

typedef enum tp_coersion_mode
{
  TP_EXPLICIT_COERCION = 0,
  TP_IMPLICIT_COERCION
} TP_COERCION_MODE;

/*
 * These are arranged to get relative types for symmetrical
 * coercion selection. The absolute position is not critical.
 * If two types are mutually coercible, the more general
 * should appear later. Eg. Float should appear after integer.
 */

static const DB_TYPE db_type_rank[] = { DB_TYPE_NULL,
  DB_TYPE_SHORT,
  DB_TYPE_INTEGER,
  DB_TYPE_BIGINT,
  DB_TYPE_NUMERIC,
  DB_TYPE_FLOAT,
  DB_TYPE_DOUBLE,
  DB_TYPE_MONETARY,
  DB_TYPE_SET,
  DB_TYPE_SEQUENCE,
  DB_TYPE_MULTISET,
  DB_TYPE_TIME,
  DB_TYPE_DATE,
  DB_TYPE_TIMESTAMP,
  DB_TYPE_TIMESTAMPLTZ,
  DB_TYPE_TIMESTAMPTZ,
  DB_TYPE_DATETIME,
  DB_TYPE_DATETIMELTZ,
  DB_TYPE_DATETIMETZ,
  DB_TYPE_OID,
  DB_TYPE_VOBJ,
  DB_TYPE_OBJECT,
  DB_TYPE_CHAR,
  DB_TYPE_VARCHAR,
  DB_TYPE_NCHAR,
  DB_TYPE_VARNCHAR,
  DB_TYPE_BIT,
  DB_TYPE_VARBIT,
  DB_TYPE_ELO,
  DB_TYPE_BLOB,
  DB_TYPE_CLOB,
  DB_TYPE_VARIABLE,
  DB_TYPE_SUB,
  DB_TYPE_POINTER,
  DB_TYPE_ERROR,
  DB_TYPE_DB_VALUE,
  (DB_TYPE) (DB_TYPE_LAST + 1)
};

AREA *tp_Domain_area = NULL;
static bool tp_Initialized = false;

extern unsigned int db_on_server;


/*
 * Shorthand to initialize a bunch of fields without duplication.
 * Initializes the fields from precision to the end.
 */
#define DOMAIN_INIT                                    \
  0,            /* precision */                        \
  0,            /* scale */                            \
  NULL,         /* class */                            \
  NULL,         /* set domain */                       \
  {NULL, 0, 0},	/* enumeration */		       \
  {-1, -1, -1}, /* class OID */                        \
  1,            /* built_in_index (set in tp_init) */  \
  0,            /* codeset */                          \
  0,            /* collation id */		       \
  TP_DOMAIN_COLL_NORMAL,  /* collation flag */	       \
  0,            /* self_ref */                         \
  1,            /* is_cached */                        \
  0,            /* is_parameterized */                 \
  false,        /* is_desc */                          \
  0				/* is_visited */

/* Same as above, but leaves off the prec and scale, and sets the codeset */
#define DOMAIN_INIT2(codeset, coll)                    \
  NULL,         /* class */                            \
  NULL,         /* set domain */                       \
  {NULL, 0, 0},	/* enumeration */		       \
  {-1, -1, -1}, /* class OID */                        \
  1,            /* built_in_index (set in tp_init) */  \
  (codeset),    /* codeset */                          \
  (coll),	/* collation id */		       \
  TP_DOMAIN_COLL_NORMAL,  /* collation flag */	       \
  0,            /* self_ref */                         \
  1,            /* is_cached */                        \
  1,            /* is_parameterized */                 \
  false,        /* is_desc */                          \
  0				/* is_visited */

/*
 * Same as DOMAIN_INIT but it sets the is_parameterized flag.
 * Used for things that don't have a precision but which are parameterized in
 * other ways.
 */
#define DOMAIN_INIT3                                   \
  0,            /* precision */                        \
  0,            /* scale */                            \
  NULL,         /* class */                            \
  NULL,         /* set domain */                       \
  {NULL, 0, 0},	/* enumeration */		       \
  {-1, -1, -1}, /* class OID */                        \
  1,            /* built_in_index (set in tp_init) */  \
  0,            /* codeset */                          \
  0,            /* collation id */		       \
  TP_DOMAIN_COLL_NORMAL,  /* collation flag */	       \
  0,            /* self_ref */                         \
  1,            /* is_cached */                        \
  1,            /* is_parameterized */                 \
  false,        /* is_desc */                          \
  0				/* is_visited */

/* Same as DOMAIN_INIT but set the prec and scale. */
#define DOMAIN_INIT4(prec, scale)                      \
  (prec),       /* precision */                        \
  (scale),      /* scale */                            \
  NULL,         /* class */                            \
  NULL,         /* set domain */                       \
  {NULL, 0, 0},    /* enumeration */                   \
  {-1, -1, -1}, /* class OID */                        \
  1,            /* built_in_index (set in tp_init) */  \
  0,            /* codeset */                          \
  0,            /* collation id */                     \
  TP_DOMAIN_COLL_NORMAL,  /* collation flag */	       \
  0,            /* self_ref */                         \
  1,            /* is_cached */                        \
  0,            /* is_parameterized */                 \
  false,        /* is_desc */                          \
  0				/* is_visited */

TP_DOMAIN tp_Null_domain = { NULL, NULL, &tp_Null, DOMAIN_INIT };
TP_DOMAIN tp_Short_domain = { NULL, NULL, &tp_Short, DOMAIN_INIT4 (DB_SHORT_PRECISION, 0) };
TP_DOMAIN tp_Integer_domain = { NULL, NULL, &tp_Integer, DOMAIN_INIT4 (DB_INTEGER_PRECISION, 0) };
TP_DOMAIN tp_Bigint_domain = { NULL, NULL, &tp_Bigint, DOMAIN_INIT4 (DB_BIGINT_PRECISION, 0) };
TP_DOMAIN tp_Float_domain = { NULL, NULL, &tp_Float, DOMAIN_INIT4 (DB_FLOAT_DECIMAL_PRECISION, 0) };
TP_DOMAIN tp_Double_domain = { NULL, NULL, &tp_Double, DOMAIN_INIT4 (DB_DOUBLE_DECIMAL_PRECISION, 0) };
TP_DOMAIN tp_Monetary_domain = { NULL, NULL, &tp_Monetary, DOMAIN_INIT4 (DB_MONETARY_DECIMAL_PRECISION, 0) };

TP_DOMAIN tp_String_domain = { NULL, NULL, &tp_String, DB_MAX_VARCHAR_PRECISION, 0,
  DOMAIN_INIT2 (INTL_CODESET_ISO88591, LANG_COLL_ISO_BINARY)
};

TP_DOMAIN tp_Object_domain = { NULL, NULL, &tp_Object, DOMAIN_INIT3 };
TP_DOMAIN tp_Set_domain = { NULL, NULL, &tp_Set, DOMAIN_INIT3 };

TP_DOMAIN tp_Multiset_domain = { NULL, NULL, &tp_Multiset, DOMAIN_INIT3 };

TP_DOMAIN tp_Sequence_domain = { NULL, NULL, &tp_Sequence, DOMAIN_INIT3 };

TP_DOMAIN tp_Midxkey_domain_list_heads[TP_NUM_MIDXKEY_DOMAIN_LIST] = {
  {NULL, NULL, &tp_Midxkey, DOMAIN_INIT3},
  {NULL, NULL, &tp_Midxkey, DOMAIN_INIT3},
  {NULL, NULL, &tp_Midxkey, DOMAIN_INIT3},
  {NULL, NULL, &tp_Midxkey, DOMAIN_INIT3},
  {NULL, NULL, &tp_Midxkey, DOMAIN_INIT3},
  {NULL, NULL, &tp_Midxkey, DOMAIN_INIT3},
  {NULL, NULL, &tp_Midxkey, DOMAIN_INIT3},
  {NULL, NULL, &tp_Midxkey, DOMAIN_INIT3},
  {NULL, NULL, &tp_Midxkey, DOMAIN_INIT3},
  {NULL, NULL, &tp_Midxkey, DOMAIN_INIT3}
};
TP_DOMAIN tp_Elo_domain = { NULL, NULL, &tp_Elo, DOMAIN_INIT };	/* todo: remove me */
TP_DOMAIN tp_Blob_domain = { NULL, NULL, &tp_Blob, DOMAIN_INIT };
TP_DOMAIN tp_Clob_domain = { NULL, NULL, &tp_Clob, DOMAIN_INIT };
TP_DOMAIN tp_Time_domain = { NULL, NULL, &tp_Time, DOMAIN_INIT4 (DB_TIME_PRECISION, 0) };
TP_DOMAIN tp_Timetz_domain = { NULL, NULL, &tp_Timetz, DOMAIN_INIT4 (DB_TIMETZ_PRECISION, 0) };
TP_DOMAIN tp_Timeltz_domain = { NULL, NULL, &tp_Timeltz, DOMAIN_INIT4 (DB_TIME_PRECISION, 0) };
TP_DOMAIN tp_Utime_domain = { NULL, NULL, &tp_Utime, DOMAIN_INIT4 (DB_TIMESTAMP_PRECISION, 0) };
TP_DOMAIN tp_Timestamptz_domain = { NULL, NULL, &tp_Timestamptz, DOMAIN_INIT4 (DB_TIMESTAMPTZ_PRECISION, 0) };
TP_DOMAIN tp_Timestampltz_domain = { NULL, NULL, &tp_Timestampltz, DOMAIN_INIT4 (DB_TIMESTAMP_PRECISION, 0) };
TP_DOMAIN tp_Date_domain = { NULL, NULL, &tp_Date, DOMAIN_INIT4 (DB_DATE_PRECISION, 0) };
TP_DOMAIN tp_Datetime_domain = { NULL, NULL, &tp_Datetime,
  DOMAIN_INIT4 (DB_DATETIME_PRECISION, DB_DATETIME_DECIMAL_SCALE)
};
TP_DOMAIN tp_Datetimetz_domain = { NULL, NULL, &tp_Datetimetz,
  DOMAIN_INIT4 (DB_DATETIMETZ_PRECISION, DB_DATETIME_DECIMAL_SCALE)
};
TP_DOMAIN tp_Datetimeltz_domain = { NULL, NULL, &tp_Datetimeltz,
  DOMAIN_INIT4 (DB_DATETIME_PRECISION, DB_DATETIME_DECIMAL_SCALE)
};

TP_DOMAIN tp_Variable_domain = { NULL, NULL, &tp_Variable, DOMAIN_INIT3 };

TP_DOMAIN tp_Substructure_domain = { NULL, NULL, &tp_Substructure, DOMAIN_INIT3 };
TP_DOMAIN tp_Pointer_domain = { NULL, NULL, &tp_Pointer, DOMAIN_INIT };
TP_DOMAIN tp_Error_domain = { NULL, NULL, &tp_Error, DOMAIN_INIT };
TP_DOMAIN tp_Vobj_domain = { NULL, NULL, &tp_Vobj, DOMAIN_INIT3 };
TP_DOMAIN tp_Oid_domain = { NULL, NULL, &tp_Oid, DOMAIN_INIT3 };
TP_DOMAIN tp_Enumeration_domain = { NULL, NULL, &tp_Enumeration, 0, 0,
  DOMAIN_INIT2 (INTL_CODESET_ISO88591, LANG_COLL_ISO_BINARY)
};

TP_DOMAIN tp_Numeric_domain = { NULL, NULL, &tp_Numeric, DB_DEFAULT_NUMERIC_PRECISION, DB_DEFAULT_NUMERIC_SCALE,
  DOMAIN_INIT2 (0, 0)
};

TP_DOMAIN tp_Bit_domain = { NULL, NULL, &tp_Bit, TP_FLOATING_PRECISION_VALUE, 0,
  DOMAIN_INIT2 (INTL_CODESET_RAW_BITS, 0)
};

TP_DOMAIN tp_VarBit_domain = { NULL, NULL, &tp_VarBit, DB_MAX_VARBIT_PRECISION, 0,
  DOMAIN_INIT2 (INTL_CODESET_RAW_BITS, 0)
};

TP_DOMAIN tp_Char_domain = { NULL, NULL, &tp_Char, TP_FLOATING_PRECISION_VALUE, 0,
  DOMAIN_INIT2 (INTL_CODESET_ISO88591, LANG_COLL_ISO_BINARY)
};

TP_DOMAIN tp_NChar_domain = { NULL, NULL, &tp_NChar, TP_FLOATING_PRECISION_VALUE, 0,
  DOMAIN_INIT2 (INTL_CODESET_ISO88591, LANG_COLL_ISO_BINARY)
};

TP_DOMAIN tp_VarNChar_domain = { NULL, NULL, &tp_VarNChar, DB_MAX_VARNCHAR_PRECISION, 0,
  DOMAIN_INIT2 (INTL_CODESET_ISO88591, LANG_COLL_ISO_BINARY)
};

/* These must be in DB_TYPE order */
static TP_DOMAIN *tp_Domains[] = {
  &tp_Null_domain,
  &tp_Integer_domain,
  &tp_Float_domain,
  &tp_Double_domain,
  &tp_String_domain,
  &tp_Object_domain,
  &tp_Set_domain,
  &tp_Multiset_domain,
  &tp_Sequence_domain,
  &tp_Elo_domain,
  &tp_Time_domain,
  &tp_Utime_domain,
  &tp_Date_domain,
  &tp_Monetary_domain,
  &tp_Variable_domain,
  &tp_Substructure_domain,
  &tp_Pointer_domain,
  &tp_Error_domain,
  &tp_Short_domain,
  &tp_Vobj_domain,
  &tp_Oid_domain,		/* does this make sense? shouldn't we share tp_Object_domain */
  &tp_Null_domain,		/* current position of DB_TYPE_DB_VALUE */
  &tp_Numeric_domain,
  &tp_Bit_domain,
  &tp_VarBit_domain,
  &tp_Char_domain,
  &tp_NChar_domain,
  &tp_VarNChar_domain,
  &tp_Null_domain,		/* result set */
  &tp_Midxkey_domain_list_heads[0],
  &tp_Null_domain,
  &tp_Bigint_domain,
  &tp_Datetime_domain,

  /* beginning of some "padding" built-in domains that can be used as expansion space when new primitive data types are 
   * added. */
  &tp_Blob_domain,
  &tp_Clob_domain,
  &tp_Enumeration_domain,
  &tp_Timestamptz_domain,
  &tp_Timestampltz_domain,
  &tp_Datetimetz_domain,
  &tp_Datetimeltz_domain,
  &tp_Timetz_domain,
  &tp_Timeltz_domain,
  &tp_Null_domain,
  &tp_Null_domain,
  &tp_Null_domain,
  &tp_Null_domain,
  &tp_Null_domain,
  &tp_Null_domain,
  &tp_Null_domain,

  /* beginning of the built-in, complex domains */


  /* end of built-in domain marker */
  NULL
};

/*
 * Lists for MIDXKEY domain cache.
 * They are hashed by the length of setdomain to decrease congestion on
 * the list (See the function tp_domain_get_list). tp_Midxkey_domain should
 * be located on the head of tp_Midxkey_domains[0] because it is one of
 * built-in domains and is used to make XASL.
 */
static TP_DOMAIN *tp_Midxkey_domains[TP_NUM_MIDXKEY_DOMAIN_LIST + 1] = {
  &tp_Midxkey_domain_list_heads[0],
  &tp_Midxkey_domain_list_heads[1],
  &tp_Midxkey_domain_list_heads[2],
  &tp_Midxkey_domain_list_heads[3],
  &tp_Midxkey_domain_list_heads[4],
  &tp_Midxkey_domain_list_heads[5],
  &tp_Midxkey_domain_list_heads[6],
  &tp_Midxkey_domain_list_heads[7],
  &tp_Midxkey_domain_list_heads[8],
  &tp_Midxkey_domain_list_heads[9],

  /* end of built-in domain marker */
  NULL
};

static TP_DOMAIN *tp_Bigint_conv[] = {
  &tp_Bigint_domain, &tp_Integer_domain, &tp_Short_domain, &tp_Float_domain,
  &tp_Double_domain, &tp_Numeric_domain, &tp_Monetary_domain, &tp_Time_domain,
  &tp_Timeltz_domain, NULL
};

static TP_DOMAIN *tp_Integer_conv[] = {
  &tp_Integer_domain, &tp_Bigint_domain, &tp_Short_domain, &tp_Float_domain,
  &tp_Double_domain, &tp_Numeric_domain, &tp_Monetary_domain,
  &tp_Time_domain, &tp_Timeltz_domain, NULL
};

static TP_DOMAIN *tp_Short_conv[] = {
  &tp_Short_domain, &tp_Integer_domain, &tp_Bigint_domain, &tp_Float_domain,
  &tp_Double_domain, &tp_Numeric_domain, &tp_Monetary_domain,
  &tp_Time_domain, &tp_Timeltz_domain, NULL
};

static TP_DOMAIN *tp_Float_conv[] = {
  &tp_Float_domain, &tp_Double_domain, &tp_Numeric_domain, &tp_Bigint_domain,
  &tp_Integer_domain, &tp_Short_domain, &tp_Monetary_domain,
  &tp_Time_domain, &tp_Timeltz_domain, NULL
};

static TP_DOMAIN *tp_Double_conv[] = {
  &tp_Double_domain, &tp_Float_domain, &tp_Numeric_domain, &tp_Bigint_domain,
  &tp_Integer_domain, &tp_Short_domain, &tp_Monetary_domain,
  &tp_Time_domain, &tp_Timeltz_domain, NULL
};

static TP_DOMAIN *tp_Numeric_conv[] = {
  &tp_Numeric_domain, &tp_Double_domain, &tp_Float_domain, &tp_Bigint_domain,
  &tp_Integer_domain, &tp_Short_domain, &tp_Monetary_domain,
  &tp_Time_domain, &tp_Timeltz_domain, NULL
};

static TP_DOMAIN *tp_Monetary_conv[] = {
  &tp_Monetary_domain, &tp_Double_domain, &tp_Float_domain,
  &tp_Bigint_domain, &tp_Integer_domain,
  &tp_Short_domain, &tp_Time_domain, &tp_Timeltz_domain, NULL
};

static TP_DOMAIN *tp_String_conv[] = {
  &tp_String_domain, &tp_Char_domain, &tp_VarNChar_domain, &tp_NChar_domain,
  &tp_Datetime_domain, &tp_Utime_domain, &tp_Time_domain,
  &tp_Date_domain, &tp_Datetimetz_domain, &tp_Timestamptz_domain,
  &tp_Timetz_domain, NULL
};

static TP_DOMAIN *tp_Char_conv[] = {
  &tp_Char_domain, &tp_String_domain, &tp_NChar_domain, &tp_VarNChar_domain,
  &tp_Datetime_domain, &tp_Utime_domain, &tp_Time_domain,
  &tp_Date_domain, &tp_Datetimetz_domain, &tp_Timestamptz_domain,
  &tp_Timetz_domain, NULL
};

static TP_DOMAIN *tp_NChar_conv[] = {
  &tp_NChar_domain, &tp_VarNChar_domain, &tp_Char_domain, &tp_String_domain,
  &tp_Datetime_domain, &tp_Utime_domain, &tp_Time_domain,
  &tp_Date_domain, &tp_Datetimetz_domain, &tp_Timestamptz_domain,
  &tp_Timetz_domain, NULL
};

static TP_DOMAIN *tp_VarNChar_conv[] = {
  &tp_VarNChar_domain, &tp_NChar_domain, &tp_String_domain, &tp_Char_domain,
  &tp_Datetime_domain, &tp_Utime_domain, &tp_Time_domain,
  &tp_Date_domain, &tp_Datetimetz_domain, &tp_Timestamptz_domain,
  &tp_Timetz_domain, NULL
};

static TP_DOMAIN *tp_Bit_conv[] = {
  &tp_Bit_domain, &tp_VarBit_domain, NULL
};

static TP_DOMAIN *tp_VarBit_conv[] = {
  &tp_VarBit_domain, &tp_Bit_domain, NULL
};

static TP_DOMAIN *tp_Set_conv[] = {
  &tp_Set_domain, &tp_Multiset_domain, &tp_Sequence_domain, NULL
};

static TP_DOMAIN *tp_Multiset_conv[] = {
  &tp_Multiset_domain, &tp_Sequence_domain, NULL
};

static TP_DOMAIN *tp_Sequence_conv[] = {
  &tp_Sequence_domain, &tp_Multiset_domain, NULL
};

/*
 * tp_Domain_conversion_matrix
 *    This is the matrix of conversion rules.  It is used primarily
 *    in the coercion of sets.
 */

TP_DOMAIN **tp_Domain_conversion_matrix[] = {
  NULL,				/* DB_TYPE_NULL */
  tp_Integer_conv,
  tp_Float_conv,
  tp_Double_conv,
  tp_String_conv,
  NULL,				/* DB_TYPE_OBJECT */
  tp_Set_conv,
  tp_Multiset_conv,
  tp_Sequence_conv,
  NULL,				/* DB_TYPE_ELO */
  NULL,				/* DB_TYPE_TIME */
  NULL,				/* DB_TYPE_TIMESTAMP */
  NULL,				/* DB_TYPE_DATE */
  tp_Monetary_conv,
  NULL,				/* DB_TYPE_VARIABLE */
  NULL,				/* DB_TYPE_SUBSTRUCTURE */
  NULL,				/* DB_TYPE_POINTER */
  NULL,				/* DB_TYPE_ERROR */
  tp_Short_conv,
  NULL,				/* DB_TYPE_VOBJ */
  NULL,				/* DB_TYPE_OID */
  NULL,				/* DB_TYPE_DB_VALUE */
  tp_Numeric_conv,		/* DB_TYPE_NUMERIC */
  tp_Bit_conv,			/* DB_TYPE_BIT */
  tp_VarBit_conv,		/* DB_TYPE_VARBIT */
  tp_Char_conv,			/* DB_TYPE_CHAR */
  tp_NChar_conv,		/* DB_TYPE_NCHAR */
  tp_VarNChar_conv,		/* DB_TYPE_VARNCHAR */
  NULL,				/* DB_TYPE_RESULTSET */
  NULL,				/* DB_TYPE_MIDXKEY */
  NULL,				/* DB_TYPE_TABLE */
  tp_Bigint_conv,		/* DB_TYPE_BIGINT */
  NULL,				/* DB_TYPE_DATETIME */
  NULL,				/* DB_TYPE_BLOB */
  NULL,				/* DB_TYPE_CLOB */
  NULL,				/* DB_TYPE_ENUMERATION */
  NULL,				/* DB_TYPE_TIMESTAMPTZ */
  NULL,				/* DB_TYPE_TIMESTAMPLTZ */
  NULL,				/* DB_TYPE_DATETIMETZ */
  NULL,				/* DB_TYPE_DATETIMELTZ */
  NULL,				/* DB_TYPE_TIMETZ */
  NULL				/* DB_TYPE_TIMELTZ */
};

#if defined (SERVER_MODE)
/* lock for domain list cache */
static pthread_mutex_t tp_domain_cache_lock = PTHREAD_MUTEX_INITIALIZER;
#endif /* SERVER_MODE */

static void domain_init (TP_DOMAIN * domain, DB_TYPE typeid_);
static int tp_domain_size_internal (const TP_DOMAIN * domain);
static void tp_value_slam_domain (DB_VALUE * value, const DB_DOMAIN * domain);
static TP_DOMAIN *tp_is_domain_cached (TP_DOMAIN * dlist, TP_DOMAIN * transient, TP_MATCH exact, TP_DOMAIN ** ins_pos);
#if !defined (SERVER_MODE)
static void tp_swizzle_oid (TP_DOMAIN * domain);
#endif /* SERVER_MODE */
static int tp_domain_check_class (TP_DOMAIN * domain, int *change);
static const TP_DOMAIN *tp_domain_find_compatible (const TP_DOMAIN * src, const TP_DOMAIN * dest);
#if defined(ENABLE_UNUSED_FUNCTION)
static int tp_null_terminate (const DB_VALUE * src, char **strp, int str_len, bool * do_alloc);
#endif
static int tp_atotime (const DB_VALUE * src, DB_TIME * temp);
static int tp_atotimetz (const DB_VALUE * src, DB_TIMETZ * temp, bool to_timeltz);
static int tp_atodate (const DB_VALUE * src, DB_DATE * temp);
static int tp_atoutime (const DB_VALUE * src, DB_UTIME * temp);
static int tp_atotimestamptz (const DB_VALUE * src, DB_TIMESTAMPTZ * temp);
static int tp_atoudatetime (const DB_VALUE * src, DB_DATETIME * temp);
static int tp_atodatetimetz (const DB_VALUE * src, DB_DATETIMETZ * temp);
static int tp_atonumeric (const DB_VALUE * src, DB_VALUE * temp);
static int tp_atof (const DB_VALUE * src, double *num_value, DB_DATA_STATUS * data_stat);
static int tp_atobi (const DB_VALUE * src, DB_BIGINT * num_value, DB_DATA_STATUS * data_stat);
#if defined(ENABLE_UNUSED_FUNCTION)
static char *tp_itoa (int value, char *string, int radix);
#endif
static char *tp_ltoa (DB_BIGINT value, char *string, int radix);
static void format_floating_point (char *new_string, char *rve, int ndigits, int decpt, int sign);
static void tp_ftoa (DB_VALUE const *src, DB_VALUE * result);
static void tp_dtoa (DB_VALUE const *src, DB_VALUE * result);
static int bfmt_print (int bfmt, const DB_VALUE * the_db_bit, char *string, int max_size);
static TP_DOMAIN_STATUS tp_value_cast_internal (const DB_VALUE * src, DB_VALUE * dest, const TP_DOMAIN * desired_domain,
						TP_COERCION_MODE coercion_mode, bool do_domain_select,
						bool preserve_domain);
static int oidcmp (OID * oid1, OID * oid2);
static int tp_domain_match_internal (const TP_DOMAIN * dom1, const TP_DOMAIN * dom2, TP_MATCH exact, bool match_order);
#if defined(CUBRID_DEBUG)
static void fprint_domain (FILE * fp, TP_DOMAIN * domain);
#endif
static INLINE TP_DOMAIN **tp_domain_get_list_ptr (DB_TYPE type, TP_DOMAIN * setdomain) __attribute__ ((ALWAYS_INLINE));
static INLINE TP_DOMAIN *tp_domain_get_list (DB_TYPE type, TP_DOMAIN * setdomain) __attribute__ ((ALWAYS_INLINE));

static int tp_enumeration_match (const DB_ENUMERATION * db_enum1, const DB_ENUMERATION * db_enum2);
static int tp_digit_number_str_to_bi (char *start, char *end, INTL_CODESET codeset, bool is_negative,
				      DB_BIGINT * num_value, DB_DATA_STATUS * data_stat);
static int tp_hex_str_to_bi (char *start, char *end, INTL_CODESET codeset, bool is_negative, DB_BIGINT * num_value,
			     DB_DATA_STATUS * data_stat);
static int tp_scientific_str_to_bi (char *start, char *end, INTL_CODESET codeset, bool is_negative,
				    DB_BIGINT * num_value, DB_DATA_STATUS * data_stat);
static DB_BIGINT tp_ubi_to_bi_with_args (UINT64 ubi, bool is_negative, bool truncated, bool round,
					 DB_DATA_STATUS * data_stat);

static UINT64 tp_ubi_times_ten (UINT64 ubi, bool * truncated);

/*
 * tp_init - Global initialization for this module.
 *    return: NO_ERROR or error code
 */
int
tp_init (void)
{
  TP_DOMAIN *d;
  int i;

  if (tp_Initialized)
    {
      assert (tp_Domain_area != NULL);
      return NO_ERROR;
    }

  /* create our allocation area */
  tp_Domain_area = area_create ("Domains", sizeof (TP_DOMAIN), 1024);
  if (tp_Domain_area == NULL)
    {
      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }

  /* 
   * Make sure the next pointer on all the built-in domains is clear.
   * Also make sure the built-in domain numbers are assigned consistently.
   * Assign the builtin indexes starting from 1 so we can use zero to mean
   * that the domain isn't built-in.
   */
  for (i = 0; tp_Domains[i] != NULL; i++)
    {
      d = tp_Domains[i];
      d->next_list = NULL;
      d->class_mop = NULL;
      d->self_ref = 0;
      d->setdomain = NULL;
      DOM_SET_ENUM (d, NULL, 0);
      d->class_oid.volid = d->class_oid.pageid = d->class_oid.slotid = -1;
      d->is_cached = 1;
      d->built_in_index = i + 1;
      d->is_desc = false;

      /* ! need to be adding this to the corresponding list */
    }

  /* tp_Midxkey_domains[0] was already initialized by the above codes. */
  for (i = 1; tp_Midxkey_domains[i] != NULL; i++)
    {
      d = tp_Midxkey_domains[i];
      d->next_list = NULL;
      d->class_mop = NULL;
      d->self_ref = 0;
      d->setdomain = NULL;
      DOM_SET_ENUM (d, NULL, 0);
      d->class_oid.volid = d->class_oid.pageid = d->class_oid.slotid = -1;
      d->is_cached = 1;
      d->built_in_index = tp_Midxkey_domains[0]->built_in_index;
      d->is_desc = false;
    }

  tp_Initialized = true;

  return NO_ERROR;
}

/*
 * tp_apply_sys_charset - applies system charset to string domains
 *    return: none
 */
void
tp_apply_sys_charset (void)
{
  if (tp_Domain_area == NULL)
    {
      return;
    }

  /* update string domains with current codeset */
  tp_String_domain.codeset = LANG_SYS_CODESET;
  tp_Char_domain.codeset = LANG_SYS_CODESET;
  tp_NChar_domain.codeset = LANG_SYS_CODESET;
  tp_VarNChar_domain.codeset = LANG_SYS_CODESET;
  tp_Enumeration_domain.codeset = LANG_SYS_CODESET;

  tp_String_domain.collation_id = LANG_SYS_COLLATION;
  tp_Char_domain.collation_id = LANG_SYS_COLLATION;
  tp_NChar_domain.collation_id = LANG_SYS_COLLATION;
  tp_VarNChar_domain.collation_id = LANG_SYS_COLLATION;
  tp_Enumeration_domain.collation_id = LANG_SYS_COLLATION;
}

/*
 * tp_final - Global shutdown for this module.
 *    return: none
 * Note:
 *    Frees all the cached domains.  It isn't absolutely necessary
 *    that we do this since area_final() will destroy the areas but when
 *    leak statistics are enabled, it will dump a bunch of messages
 *    about dangling domain allocations.
 */
void
tp_final (void)
{
  TP_DOMAIN *dlist, *d, *next, *prev;
  int i;

  if (!tp_Initialized)
    {
      assert (tp_Domain_area == NULL);
      return;
    }

  /* 
   * Make sure the next pointer on all the built-in domains is clear.
   * Also make sure the built-in domain numbers are assigned consistently.
   */
  for (i = 0; tp_Domains[i] != NULL; i++)
    {
      dlist = tp_Domains[i];
      /* 
       * The first element in the domain array is always a built-in, there
       * can potentially be other built-ins in the list mixed in with
       * allocated domains.
       */
      for (d = dlist->next_list, prev = dlist, next = NULL; d != NULL; d = next)
	{
	  next = d->next_list;
	  if (d->built_in_index)
	    {
	      prev = d;
	    }
	  else
	    {
	      prev->next_list = next;

	      /* 
	       * Make sure to turn off the cache bit or else tp_domain_free
	       * will ignore the request.
	       */
	      d->is_cached = 0;
	      tp_domain_free (d);
	    }
	}
    }

  /* 
   * tp_Midxkey_domains[0] was cleared by the above for-loop.
   * It holds a pointer of tp_Midxkey_domain_list_heads[0] on its head.
   * The pointer is also stored on tp_Domains[DB_TYPE_MIDXKEY].
   */
  for (i = 1; tp_Midxkey_domains[i] != NULL; i++)
    {
      dlist = tp_Midxkey_domains[i];

      for (d = dlist->next_list, prev = dlist, next = NULL; d != NULL; d = next)
	{
	  next = d->next_list;
	  if (d->built_in_index)
	    {
	      prev = d;
	    }
	  else
	    {
	      prev->next_list = next;

	      d->is_cached = 0;
	      tp_domain_free (d);
	    }
	}
    }

  if (tp_Domain_area != NULL)
    {
      area_destroy (tp_Domain_area);
      tp_Domain_area = NULL;
    }

  tp_Initialized = false;
}

/*
 * tp_domain_clear_enumeration () - free memory allocated for an enumeration type
 * return : void
 * enumeration (in/out): enumeration
 */
void
tp_domain_clear_enumeration (DB_ENUMERATION * enumeration)
{
  int i = 0;

  if (enumeration == NULL || enumeration->count == 0)
    {
      return;
    }

  for (i = 0; i < enumeration->count; i++)
    {
      if (DB_GET_ENUM_ELEM_STRING (&enumeration->elements[i]) != NULL)
	{
	  free_and_init (DB_GET_ENUM_ELEM_STRING (&enumeration->elements[i]));
	}
    }

  free_and_init (enumeration->elements);
}

/*
 * tp_enumeration_match () check if two enumerations match
 * return : 1 if the two enums match, 0 otherwise
 * db_enum1 (in):
 * db_enum2 (in);
 */
static int
tp_enumeration_match (const DB_ENUMERATION * db_enum1, const DB_ENUMERATION * db_enum2)
{
  int i;
  DB_ENUM_ELEMENT *enum1 = NULL, *enum2 = NULL;

  if (db_enum1 == db_enum2)
    {
      return 1;
    }

  if (db_enum1 == NULL || db_enum2 == NULL)
    {
      return 0;
    }

  if (db_enum1->count != db_enum2->count)
    {
      return 0;
    }
  if (db_enum1->collation_id != db_enum2->collation_id)
    {
      return 0;
    }

  for (i = 0; i < db_enum1->count; i++)
    {
      enum1 = &db_enum1->elements[i];
      enum2 = &db_enum2->elements[i];

      if (DB_GET_ENUM_ELEM_STRING_SIZE (enum1) != DB_GET_ENUM_ELEM_STRING_SIZE (enum2))
	{
	  return 0;
	}

      /* 
       * memcmp is used here because it is necessary for domains like
       * ENUM('a', 'b') COLLATE utf8_en_ci and
       * ENUM('A', 'B') COLLATE utf8_en_ci to be regarded as different
       * domains, despite their common case-insensitive collation.
       * Thus, collation-based comparison is not correct here.
       */
      if (memcmp (DB_GET_ENUM_ELEM_STRING (enum1), DB_GET_ENUM_ELEM_STRING (enum2),
		  DB_GET_ENUM_ELEM_STRING_SIZE (enum1)) != 0)
	{
	  return 0;
	}
    }

  return 1;
}

/*
 * tp_get_fixed_precision - return the fixed precision of the given type.
 *    return: the fixed precision for the fixed types, otherwise -1.
 *    domain_type(in): The type of the domain
 */
int
tp_get_fixed_precision (DB_TYPE domain_type)
{
  int precision;

  switch (domain_type)
    {
    case DB_TYPE_INTEGER:
      precision = DB_INTEGER_PRECISION;
      break;
    case DB_TYPE_SHORT:
      precision = DB_SHORT_PRECISION;
      break;
    case DB_TYPE_BIGINT:
      precision = DB_BIGINT_PRECISION;
      break;
    case DB_TYPE_FLOAT:
      precision = DB_FLOAT_DECIMAL_PRECISION;
      break;
    case DB_TYPE_DOUBLE:
      precision = DB_DOUBLE_DECIMAL_PRECISION;
      break;
    case DB_TYPE_DATE:
      precision = DB_DATE_PRECISION;
      break;
    case DB_TYPE_TIME:
    case DB_TYPE_TIMELTZ:
      precision = DB_TIME_PRECISION;
      break;
    case DB_TYPE_TIMETZ:
      precision = DB_TIMETZ_PRECISION;
      break;
    case DB_TYPE_TIMESTAMP:
    case DB_TYPE_TIMESTAMPLTZ:
      precision = DB_TIMESTAMP_PRECISION;
      break;
    case DB_TYPE_TIMESTAMPTZ:
      precision = DB_TIMESTAMPTZ_PRECISION;
      break;
    case DB_TYPE_DATETIME:
    case DB_TYPE_DATETIMELTZ:
      precision = DB_DATETIME_PRECISION;
      break;
    case DB_TYPE_DATETIMETZ:
      precision = DB_DATETIMETZ_PRECISION;
      break;
    case DB_TYPE_MONETARY:
      precision = DB_MONETARY_DECIMAL_PRECISION;
      break;
    default:
      precision = DB_DEFAULT_PRECISION;
      break;
    }

  return precision;
}

/*
 * tp_domain_free - free a hierarchical domain structure.
 *    return: none
 *    dom(out): domain to free
 * Note:
 *    This routine can be called for a transient or cached domain.  If
 *    the domain has been cached, the request is ignored.
 *    Note that you can only call this on the root of a domain hierarchy,
 *    you are not allowed to grab pointers into the middle of a hierarchical
 *    domain and free that.
 */
void
tp_domain_free (TP_DOMAIN * dom)
{
  TP_DOMAIN *d, *next;

  if (dom != NULL && !dom->is_cached)
    {

      /* NULL things that might be problems for garbage collection */
      dom->class_mop = NULL;

      /* 
       * sub-domains are always completely owned by their root domain,
       * they cannot be cached anywhere else.
       */
      for (d = dom->setdomain, next = NULL; d != NULL; d = next)
	{
	  next = d->next;
	  tp_domain_free (d);
	}

      if (dom->type->id == DB_TYPE_ENUMERATION)
	{
	  tp_domain_clear_enumeration (&DOM_GET_ENUMERATION (dom));
	}

      (void) area_free (tp_Domain_area, dom);
    }
}

/*
 * domain_init - initializes a domain structure to contain reasonable "default"
 * values.
 *    return: none
 *    domain(out): domain structure to initialize
 *    type_id(in): basic type of the domain
 * Note:
 *    Used by tp_domain_new and also in some other places
 *    where we need to quickly synthesize some transient domain structures.
 */
static void
domain_init (TP_DOMAIN * domain, DB_TYPE type_id)
{
  assert (type_id <= DB_TYPE_LAST);

  domain->next = NULL;
  domain->next_list = NULL;
  domain->type = PR_TYPE_FROM_ID (type_id);
  domain->precision = 0;
  domain->scale = 0;
  domain->class_mop = NULL;
  domain->self_ref = 0;
  domain->setdomain = NULL;
  DOM_SET_ENUM (domain, NULL, 0);
  OID_SET_NULL (&domain->class_oid);

  domain->collation_flag = TP_DOMAIN_COLL_NORMAL;
  if (TP_TYPE_HAS_COLLATION (type_id))
    {
      domain->codeset = LANG_SYS_CODESET;
      domain->collation_id = LANG_SYS_COLLATION;
      if (type_id == DB_TYPE_ENUMERATION)
	{
	  domain->enumeration.collation_id = LANG_SYS_COLLATION;
	}
    }
  else if (TP_IS_BIT_TYPE (type_id))
    {
      domain->codeset = INTL_CODESET_RAW_BITS;
      domain->collation_id = 0;
    }
  else
    {
      domain->codeset = 0;
      domain->collation_id = 0;
    }
  domain->is_cached = 0;
  domain->built_in_index = 0;

  /* use the built-in domain template to see if we're parameterized or not */
  domain->is_parameterized = tp_Domains[type_id]->is_parameterized;

  domain->is_desc = 0;
}

/*
 * tp_domain_new - returns a new initialized transient domain.
 *    return: new transient domain
 *    type(in): type id
 * Note:
 *    It is intended for use in places where domains are being created
 *    incrementally for eventual passing to tp_domain_cache.
 *    Only the type id is passed here since that is the only common
 *    piece of information shared by all domains.
 *    The contents of the domain can be filled in by the caller assuming
 *    they obey the rules.
 */
TP_DOMAIN *
tp_domain_new (DB_TYPE type)
{
  TP_DOMAIN *new_dm;

  new_dm = (TP_DOMAIN *) area_alloc (tp_Domain_area);
  if (new_dm != NULL)
    {
      domain_init (new_dm, type);
    }

  return new_dm;
}


/*
 * tp_domain_construct - create a transient domain object with type, class,
 * precision, scale and setdomain.
 *    return:
 *    domain_type(in): The basic type of the domain
 *    class_obj(in): The class of the domain (for DB_TYPE_OBJECT)
 *    precision(in): The precision of the domain
 *    scale(in): The class of the domain
 *    setdomain(in): The setdomain of the domain
 * Note:
 *    Used in a few places, callers must be aware that there may be more
 *    initializations to do since not all of the domain parameters are
 *    arguments to this function.
 *
 *    The setdomain must also be a transient domain list.
 */
TP_DOMAIN *
tp_domain_construct (DB_TYPE domain_type, DB_OBJECT * class_obj, int precision, int scale, TP_DOMAIN * setdomain)
{
  TP_DOMAIN *new_dm;
  int fixed_precision;

  new_dm = tp_domain_new (domain_type);
  if (new_dm)
    {
      fixed_precision = tp_get_fixed_precision (domain_type);
      if (fixed_precision != DB_DEFAULT_PRECISION)
	{
#if !defined (NDEBUG)
	  if (precision != fixed_precision)
	    {
	      assert (false);
	    }
#endif
	  precision = fixed_precision;
	}

      new_dm->precision = precision;
      new_dm->scale = scale;
      new_dm->setdomain = setdomain;

#if !defined (NDEBUG)
      if (domain_type == DB_TYPE_MIDXKEY)
	{
	  assert ((new_dm->setdomain && new_dm->precision == tp_domain_size (new_dm->setdomain))
		  || (new_dm->setdomain == NULL && new_dm->precision == 0));

	  {
	    TP_DOMAIN *d;
	    for (d = new_dm->setdomain; d != NULL; d = d->next)
	      {
		assert (d->is_cached == 0);
	      }
	  }
	}
#endif /* NDEBUG */

      if (class_obj == (DB_OBJECT *) TP_DOMAIN_SELF_REF)
	{
	  new_dm->class_mop = NULL;
	  new_dm->self_ref = 1;
	}
      else
	{
	  new_dm->class_mop = class_obj;
	  new_dm->self_ref = 0;
	  /* 
	   * For compatibility on the server side, class objects must have
	   * the oid in the domain match the oid in the class object.
	   */
	  if (class_obj)
	    {
	      new_dm->class_oid = class_obj->oid_info.oid;
	    }
	}

      /* 
       * have to leave the class OID uninitialized because we don't know how
       * to get an OID out of a DB_OBJECT on the server.
       * That shouldn't matter since the server side unpackers will use
       * tp_domain_new and set the domain fields directly.
       */
    }
  return new_dm;
}

/*
 * tp_domain_copy_enumeration () - copy an enumeration
 * return: error code or NO_ERROR
 * dest (in/out): destination enumeration
 * src (in) : source enumeration
 */
int
tp_domain_copy_enumeration (DB_ENUMERATION * dest, const DB_ENUMERATION * src)
{
  int error = NO_ERROR, i;
  DB_ENUM_ELEMENT *dest_elem = NULL, *src_elem = NULL;
  char *dest_str = NULL;

  if (src == NULL)
    {
      assert (false);
      return ER_FAILED;
    }

  if (dest == NULL)
    {
      assert (false);
      return ER_FAILED;
    }

  dest->collation_id = src->collation_id;
  dest->count = 0;
  dest->elements = NULL;
  if (src->count == 0 && src->elements == NULL)
    {
      /* nothing else to do */
      return NO_ERROR;
    }

  /* validate source enumeration */
  if (src->count == 0 && src->elements != NULL)
    {
      assert (false);
      return ER_FAILED;
    }
  else if (src->count != 0 && src->elements == NULL)
    {
      assert (false);
      return ER_FAILED;
    }

  dest->count = src->count;

  dest->elements = malloc (src->count * sizeof (DB_ENUM_ELEMENT));
  if (dest->elements == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, src->count * sizeof (DB_ENUM_ELEMENT));
      return ER_FAILED;
    }

  for (i = 0; i < src->count; i++)
    {
      src_elem = &src->elements[i];
      dest_elem = &dest->elements[i];

      DB_SET_ENUM_ELEM_SHORT (dest_elem, DB_GET_ENUM_ELEM_SHORT (src_elem));

      if (DB_GET_ENUM_ELEM_STRING (src_elem) != NULL)
	{
	  dest_str = malloc (DB_GET_ENUM_ELEM_STRING_SIZE (src_elem) + 1);
	  if (dest_str == NULL)
	    {
	      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
		      (size_t) (DB_GET_ENUM_ELEM_STRING_SIZE (src_elem) + 1));
	      error = ER_OUT_OF_VIRTUAL_MEMORY;
	      goto error_return;
	    }
	  memcpy (dest_str, DB_GET_ENUM_ELEM_STRING (src_elem), DB_GET_ENUM_ELEM_STRING_SIZE (src_elem));
	  dest_str[DB_GET_ENUM_ELEM_STRING_SIZE (src_elem)] = 0;
	  DB_SET_ENUM_ELEM_STRING (dest_elem, dest_str);
	  DB_SET_ENUM_ELEM_STRING_SIZE (dest_elem, DB_GET_ENUM_ELEM_STRING_SIZE (src_elem));
	}
      else
	{
	  DB_SET_ENUM_ELEM_STRING (dest_elem, NULL);
	}
      DB_SET_ENUM_ELEM_CODESET (dest_elem, DB_GET_ENUM_ELEM_CODESET (src_elem));
    }

  return NO_ERROR;

error_return:
  if (dest->elements != NULL)
    {
      for (--i; i >= 0; i--)
	{
	  if (DB_GET_ENUM_ELEM_STRING (&dest->elements[i]) != NULL)
	    {
	      free_and_init (DB_GET_ENUM_ELEM_STRING (&dest->elements[i]));
	    }
	}
      free_and_init (dest->elements);
    }

  return error;
}

/*
 * tp_domain_copy - copy a hierarcical domain structure
 *    return: new domain
 *    dom(in): domain to copy
 *    check_cache(in): if set, return cached instance
 * Note:
 *    If the domain was cached, we simply return a handle to the cached
 *    domain, otherwise we make a full structure copy.
 *    This should only be used in a few places in the schema manager which
 *    maintains separate copies of all the attribute domains during
 *    flattening.  Could be converted to used cached domains perhaps.
 *    But the "self referencing" domain is the problem.
 *
 *    New functionality:  If the check_cache parameter is false, we make
 *    a NEW copy of the parameter domain whether it is cached or not.   This
 *    is used for updating fields of a cached domain.  We don't want to
 *    update a domain that has already been cached because multiple structures
 *    may be pointing to it.
 */
TP_DOMAIN *
tp_domain_copy (const TP_DOMAIN * domain, bool check_cache)
{
  TP_DOMAIN *new_domain, *first, *last;
  const TP_DOMAIN *d;

  if (check_cache && domain->is_cached)
    {
      return (TP_DOMAIN *) domain;
    }

  first = NULL;
  if (domain != NULL)
    {
      last = NULL;

      for (d = domain; d != NULL; d = d->next)
	{
	  new_domain = tp_domain_new (TP_DOMAIN_TYPE (d));
	  if (new_domain == NULL)
	    {
	      tp_domain_free (first);
	      return NULL;
	    }
	  else
	    {
	      /* copy over the domain parameters */
	      new_domain->class_mop = d->class_mop;
	      new_domain->class_oid = d->class_oid;
	      new_domain->precision = d->precision;
	      new_domain->scale = d->scale;
	      new_domain->codeset = d->codeset;
	      new_domain->collation_id = d->collation_id;
	      new_domain->self_ref = d->self_ref;
	      new_domain->is_parameterized = d->is_parameterized;
	      new_domain->is_desc = d->is_desc;

	      if (d->type->id == DB_TYPE_ENUMERATION)
		{
		  int error;
		  error = tp_domain_copy_enumeration (&DOM_GET_ENUMERATION (new_domain), &DOM_GET_ENUMERATION (d));
		  if (error != NO_ERROR)
		    {
		      tp_domain_free (first);
		      return NULL;
		    }
		}

	      if (d->setdomain != NULL)
		{
		  new_domain->setdomain = tp_domain_copy (d->setdomain, true);
		  if (new_domain->setdomain == NULL)
		    {
		      tp_domain_free (first);
		      return NULL;
		    }
		}

	      if (first == NULL)
		{
		  first = new_domain;
		}
	      else
		{
		  last->next = new_domain;
		}
	      last = new_domain;
	    }
	}
    }

  return first;
}


/*
 * tp_domain_size_internal - count the number of domains in a domain list
 *    return: number of domains in a domain list
 *    domain(in): a domain list
 */
static int
tp_domain_size_internal (const TP_DOMAIN * domain)
{
  int size = 0;

  while (domain)
    {
      ++size;
      domain = domain->next;
    }

  return size;
}

/*
 * tp_domain_size - count the number of domains in a domain list
 *    return: number of domains in a domain list
 *    domain(in): domain
 */
int
tp_domain_size (const TP_DOMAIN * domain)
{
  return tp_domain_size_internal (domain);
}

/*
 * tp_setdomain_size - count the number of domains in a setdomain list
 *    return: number of domains in a setdomain list
 *    domain(in): domain
 */
int
tp_setdomain_size (const TP_DOMAIN * domain)
{
  if (TP_DOMAIN_TYPE (domain) == DB_TYPE_MIDXKEY)
    {
      assert ((domain->setdomain && domain->precision == tp_domain_size (domain->setdomain))
	      || (domain->setdomain == NULL && domain->precision == 0));
      return domain->precision;
    }
  else
    {
      return tp_domain_size_internal (domain->setdomain);
    }
}

/*
 * tp_value_slam_domain - alter the domain of an existing DB_VALUE
 *    return: nothing
 *    value(out): value whose domain is to be altered
 *    domain(in): domain descriptor
 * Note:
 * used usually in a context like tp_value_cast where we know that we
 * have a perfectly good fixed-length string that we want tocast as a varchar.
 *
 * This is a dangerous function and should not be exported to users.  use
 * only if you know exactly what you're doing!!!!
 */
static void
tp_value_slam_domain (DB_VALUE * value, const DB_DOMAIN * domain)
{
  switch (TP_DOMAIN_TYPE (domain))
    {
    case DB_TYPE_CHAR:
    case DB_TYPE_VARCHAR:
    case DB_TYPE_NCHAR:
    case DB_TYPE_VARNCHAR:
      if (domain->collation_flag == TP_DOMAIN_COLL_NORMAL || domain->collation_flag == TP_DOMAIN_COLL_ENFORCE)
	{
	  db_string_put_cs_and_collation (value, TP_DOMAIN_CODESET (domain), TP_DOMAIN_COLLATION (domain));
	}
      if (domain->collation_flag == TP_DOMAIN_COLL_ENFORCE)
	{
	  /* don't apply precision and type */
	  break;
	}
    case DB_TYPE_BIT:
    case DB_TYPE_VARBIT:
      value->domain.char_info.type = TP_DOMAIN_TYPE (domain);
      value->domain.char_info.length = domain->precision;
      break;

    case DB_TYPE_NUMERIC:
      value->domain.numeric_info.type = TP_DOMAIN_TYPE (domain);
      value->domain.numeric_info.precision = domain->precision;
      value->domain.numeric_info.scale = domain->scale;
      break;

    default:
      value->domain.general_info.type = TP_DOMAIN_TYPE (domain);
      break;
    }
}

/*
 * tp_domain_match - examins two domains to see if they are logically same
 *    return: non-zero if the domains are the same
 *    dom1(in): first domain
 *    dom2(in): second domain
 *    exact(in): how tolerant we are of mismatches
 */
int
tp_domain_match (const TP_DOMAIN * dom1, const TP_DOMAIN * dom2, TP_MATCH exact)
{
  return tp_domain_match_internal (dom1, dom2, exact, true);
}

/*
 * tp_domain_match_ignore_order - examins two domains to see if they are logically same
 *    return: non-zero if the domains are the same
 *    dom1(in): first domain
 *    dom2(in): second domain
 *    exact(in): how tolerant we are of mismatches
 */
int
tp_domain_match_ignore_order (const TP_DOMAIN * dom1, const TP_DOMAIN * dom2, TP_MATCH exact)
{
  return tp_domain_match_internal (dom1, dom2, exact, false);
}

/*
 * tp_domain_match_internal - examins two domains to see if they are logically same
 *    return: non-zero if the domains are the same
 *    dom1(in): first domain
 *    dom2(in): second domain
 *    exact(in): how tolerant we are of mismatches
 *    match_order(in): check for asc/desc
 */
static int
tp_domain_match_internal (const TP_DOMAIN * dom1, const TP_DOMAIN * dom2, TP_MATCH exact, bool match_order)
{
  int match = 0;

  if (dom1 == NULL || dom2 == NULL)
    {
      return 0;
    }

  /* in the case where their both cached */
  if (dom1 == dom2)
    {
      return 1;
    }

  if ((TP_DOMAIN_TYPE (dom1) != TP_DOMAIN_TYPE (dom2))
      && (exact != TP_STR_MATCH || !TP_NEAR_MATCH (TP_DOMAIN_TYPE (dom1), TP_DOMAIN_TYPE (dom2))))
    {
      return 0;
    }

  /* 
   * At this point, either dom1 and dom2 have exactly the same type, or
   * exact_match is TP_STR_MATCH and dom1 and dom2 are a char/varchar
   * (nchar/varnchar, bit/varbit) pair.
   */

  /* check for asc/desc */
  if (TP_DOMAIN_TYPE (dom1) == TP_DOMAIN_TYPE (dom2) && tp_valid_indextype (TP_DOMAIN_TYPE (dom1))
      && match_order == true && dom1->is_desc != dom2->is_desc)
    {
      return 0;
    }

  /* could use the new is_parameterized flag to avoid the switch ? */

  switch (TP_DOMAIN_TYPE (dom1))
    {

    case DB_TYPE_NULL:
    case DB_TYPE_INTEGER:
    case DB_TYPE_BIGINT:
    case DB_TYPE_FLOAT:
    case DB_TYPE_DOUBLE:
    case DB_TYPE_BLOB:
    case DB_TYPE_CLOB:
    case DB_TYPE_TIME:
    case DB_TYPE_TIMELTZ:
    case DB_TYPE_TIMETZ:
    case DB_TYPE_TIMESTAMP:
    case DB_TYPE_TIMESTAMPTZ:
    case DB_TYPE_TIMESTAMPLTZ:
    case DB_TYPE_DATETIME:
    case DB_TYPE_DATETIMETZ:
    case DB_TYPE_DATETIMELTZ:
    case DB_TYPE_DATE:
    case DB_TYPE_MONETARY:
    case DB_TYPE_SHORT:
      /* 
       * these domains have no parameters, they match if the types are the
       * same.
       */
      match = 1;
      break;

    case DB_TYPE_VOBJ:
    case DB_TYPE_OBJECT:
    case DB_TYPE_SUB:

      /* 
       * if "exact" is zero, we should be checking the subclass hierarchy of
       * dom1 to see id dom2 is in it !
       */

      /* Always prefer comparison of MOPs */
      if (dom1->class_mop != NULL && dom2->class_mop != NULL)
	{
	  match = (dom1->class_mop == dom2->class_mop);
	}
      else if (dom1->class_mop == NULL && dom2->class_mop == NULL)
	{
	  match = OID_EQ (&dom1->class_oid, &dom2->class_oid);
	}
      else
	{
	  /* 
	   * We have a mixture of OID & MOPS, it probably isn't necessary to
	   * be this general but try to avoid assuming the class OIDs have
	   * been set when there is a MOP present.
	   */
	  if (dom1->class_mop == NULL)
	    {
	      match = OID_EQ (&dom1->class_oid, WS_OID (dom2->class_mop));
	    }
	  else
	    {
	      match = OID_EQ (WS_OID (dom1->class_mop), &dom2->class_oid);
	    }
	}

      if (match == 0 && exact == TP_SET_MATCH && dom1->class_mop == NULL && OID_ISNULL (&dom1->class_oid))
	{
	  match = 1;
	}
      break;

    case DB_TYPE_VARIABLE:
    case DB_TYPE_SET:
    case DB_TYPE_MULTISET:
    case DB_TYPE_SEQUENCE:
#if 1
      /* >>>>> NEED MORE CONSIDERATION <<<<< do not check order must be rollback with tp_domain_add() */
      if (dom1->setdomain == dom2->setdomain)
	{
	  match = 1;
	}
      else
	{
	  int dsize;

	  /* don't bother comparing the lists unless the sizes are the same */
	  dsize = tp_domain_size (dom1->setdomain);
	  if (dsize == tp_domain_size (dom2->setdomain))
	    {
	      /* handle the simple single domain case quickly */
	      if (dsize == 1)
		{
		  match = tp_domain_match (dom1->setdomain, dom2->setdomain, exact);
		}
	      else
		{
		  TP_DOMAIN *d1, *d2;

		  match = 1;
		  for (d1 = dom1->setdomain, d2 = dom2->setdomain; d1 != NULL && d2 != NULL;
		       d1 = d1->next, d2 = d2->next)
		    {
		      if (!tp_domain_match (d1, d2, exact))
			{
			  match = 0;
			  break;	/* immediately exit for loop */
			}
		    }
		}
	    }
	}
#else /* 0 */
      if (dom1->setdomain == dom2->setdomain)
	{
	  match = 1;
	}
      else
	{
	  int dsize;

	  /* don't bother comparing the lists unless the sizes are the same */
	  dsize = tp_domain_size (dom1->setdomain);
	  if (dsize == tp_domain_size (dom2->setdomain))
	    {

	      /* handle the simple single domain case quickly */
	      if (dsize == 1)
		{
		  match = tp_domain_match (dom1->setdomain, dom2->setdomain, exact);
		}
	      else
		{
		  TP_DOMAIN *d1, *d2;

		  /* clear the visited flag in the second subdomain list */
		  for (d2 = dom2->setdomain; d2 != NULL; d2 = d2->next)
		    {
		      d2->is_visited = 0;
		    }

		  match = 1;
		  for (d1 = dom1->setdomain; d1 != NULL && match; d1 = d1->next)
		    {
		      for (d2 = dom2->setdomain; d2 != NULL; d2 = d2->next)
			{
			  if (!d2->is_visited && tp_domain_match (d1, d2, exact))
			    {
			      break;
			    }
			}
		      /* did we find the domain in the other list ? */
		      if (d2 != NULL)
			{
			  d2->is_visited = 1;
			}
		      else
			{
			  match = 0;
			}
		    }
		}
	    }
	}
#endif /* 1 */
      break;

    case DB_TYPE_MIDXKEY:
      if (dom1->setdomain == dom2->setdomain)
	{
	  match = 1;
	}
      else
	{
	  int i, dsize1, dsize2;
	  TP_DOMAIN *element_dom1;
	  TP_DOMAIN *element_dom2;

	  dsize1 = tp_domain_size (dom1->setdomain);
	  dsize2 = tp_domain_size (dom2->setdomain);

	  if (dsize1 == dsize2)
	    {
	      match = 1;
	      element_dom1 = dom1->setdomain;
	      element_dom2 = dom2->setdomain;

	      for (i = 0; i < dsize1; i++)
		{
		  match = tp_domain_match (element_dom1, element_dom2, exact);
		  if (match == 0)
		    {
		      break;
		    }
		  element_dom1 = element_dom1->next;
		  element_dom2 = element_dom2->next;
		}
	    }
	}
      break;

    case DB_TYPE_VARCHAR:
      if (dom1->collation_id != dom2->collation_id)
	{
	  match = 0;
	  break;
	}
      /* fall through */
    case DB_TYPE_VARBIT:
      if (exact == TP_EXACT_MATCH || exact == TP_SET_MATCH)
	{
	  match = dom1->precision == dom2->precision;
	}
      else if (exact == TP_STR_MATCH)
	{
	  /* 
	   * Allow the match if the precisions would allow us to reuse the
	   * string without modification.
	   */
	  match = (dom1->precision >= dom2->precision);
	}
      else
	{
	  /* 
	   * Allow matches regardless of precision, let the actual length of the
	   * value determine if it can be assigned.  This is important for
	   * literal strings as their precision will be the maximum but they
	   * can still be assigned to domains with a smaller precision
	   * provided the actual value is within the destination domain
	   * tolerance.
	   */
	  match = 1;
	}
      break;

    case DB_TYPE_CHAR:
      if (dom1->collation_id != dom2->collation_id)
	{
	  match = 0;
	  break;
	}
      /* fall through */
    case DB_TYPE_BIT:
      /* 
       * Unlike varchar, we have to be a little tighter on domain matches for
       * fixed width char.  Not as much of a problem since these won't be
       * used for literal strings.
       */
      if (exact == TP_EXACT_MATCH || exact == TP_STR_MATCH || exact == TP_SET_MATCH)
	{
	  match = (dom1->precision == dom2->precision);
	}
      else
	{
	  /* Recognize a precision of TP_FLOATING_PRECISION_VALUE to indicate a precision whose coercability must be
	   * determined by examing the value.  This is used primarily by db_coerce() since it must pick a reasonable
	   * CHAR domain for the representation of a literal string. Accept zero here too since it seems to creep into
	   * domains sometimes. */
	  match = (dom2->precision == 0 || dom2->precision == TP_FLOATING_PRECISION_VALUE
		   || dom1->precision >= dom2->precision);
	}
      break;

    case DB_TYPE_NCHAR:
      if (exact == TP_EXACT_MATCH || exact == TP_STR_MATCH || exact == TP_SET_MATCH)
	{
	  match = ((dom1->precision == dom2->precision) && (dom1->collation_id == dom2->collation_id));
	}
      else
	{
	  /* 
	   * see discussion of special domain precision values in the
	   * DB_TYPE_CHAR case above.
	   */
	  match = ((dom1->collation_id == dom2->collation_id)
		   && (dom2->precision == 0 || dom2->precision == TP_FLOATING_PRECISION_VALUE
		       || dom1->precision >= dom2->precision));
	}

      break;

    case DB_TYPE_VARNCHAR:
      if (exact == TP_EXACT_MATCH || exact == TP_STR_MATCH || exact == TP_SET_MATCH)
	{
	  match = ((dom1->precision == dom2->precision) && (dom1->collation_id == dom2->collation_id));
	}
      else
	{
	  /* see notes above under the DB_TYPE_VARCHAR clause */
	  match = dom1->collation_id == dom2->collation_id;
	}
      break;

    case DB_TYPE_NUMERIC:
      /* 
       * note that we never allow inexact matches here because the
       * mr_setmem_numeric function is not currently able to perform the
       * deferred coercion.
       */
      match = ((dom1->precision == dom2->precision) && (dom1->scale == dom2->scale));
      break;

    case DB_TYPE_POINTER:
    case DB_TYPE_ERROR:
    case DB_TYPE_OID:
    case DB_TYPE_DB_VALUE:
      /* 
       * These are internal domains, they shouldn't be seen, in case they are,
       * just let them match without parameters.
       */
      match = 1;
      break;

    case DB_TYPE_ENUMERATION:
      match = tp_enumeration_match (&DOM_GET_ENUMERATION (dom1), &DOM_GET_ENUMERATION (dom2));
      break;

    case DB_TYPE_RESULTSET:
    case DB_TYPE_TABLE:
      break;
      /* don't have a default so we make sure to add clauses for all types */
    }

#if !defined (NDEBUG)
  if (match && TP_TYPE_HAS_COLLATION (TP_DOMAIN_TYPE (dom1)))
    {
      assert (dom1->codeset == dom2->codeset);
    }
#endif

  return match;
}

/*
 * tp_domain_get_list_ptr - get the pointer of the head of domain list
 *    return: pointer of the head of the list
 *    type(in): type of value
 *    setdomain(in): used to find appropriate list of MIDXKEY
 */
STATIC_INLINE TP_DOMAIN **
tp_domain_get_list_ptr (DB_TYPE type, TP_DOMAIN * setdomain)
{
  int list_index;

  if (type == DB_TYPE_MIDXKEY)
    {
      list_index = tp_domain_size (setdomain);
      list_index %= TP_NUM_MIDXKEY_DOMAIN_LIST;
      return &(tp_Midxkey_domains[list_index]);
    }
  else
    {
      return &(tp_Domains[type]);
    }
}

/*
 * tp_domain_get_list - get the head of domain list
 *    return: the head of the list
 *    type(in): type of value
 *    setdomain(in): used to find appropriate list of MIDXKEY
 */
STATIC_INLINE TP_DOMAIN *
tp_domain_get_list (DB_TYPE type, TP_DOMAIN * setdomain)
{
  TP_DOMAIN **dlist;

  dlist = tp_domain_get_list_ptr (type, setdomain);
  return *dlist;
}

/*
 * tp_is_domain_cached - find matching domain from domain list
 *    return: matched domain
 *    dlist(in): domain list
 *    transient(in): transient domain
 *    exact(in): matching level
 *    ins_pos(out): domain found
 * Note:
 * DB_TYPE_VARCHAR, DB_TYPE_VARBIT, DB_TYPE_VARNCHAR : precision's desc order
 *                                             others: precision's asc order
 */
static TP_DOMAIN *
tp_is_domain_cached (TP_DOMAIN * dlist, TP_DOMAIN * transient, TP_MATCH exact, TP_DOMAIN ** ins_pos)
{
  TP_DOMAIN *domain = dlist;
  int match = 0;

  /* in the case where their both cached */
  if (domain == transient)
    {
      return domain;
    }

  if ((TP_DOMAIN_TYPE (domain) != TP_DOMAIN_TYPE (transient))
      && (exact != TP_STR_MATCH || !TP_NEAR_MATCH (TP_DOMAIN_TYPE (domain), TP_DOMAIN_TYPE (transient))))
    {
      return NULL;
    }

  *ins_pos = domain;

  /* 
   * At this point, either domain and transient have exactly the same type, or
   * exact_match is TP_STR_MATCH and domain and transient are a char/varchar
   * (nchar/varnchar, bit/varbit) pair.
   */

  /* could use the new is_parameterized flag to avoid the switch ? */
  switch (TP_DOMAIN_TYPE (domain))
    {

    case DB_TYPE_NULL:
    case DB_TYPE_INTEGER:
    case DB_TYPE_BIGINT:
    case DB_TYPE_FLOAT:
    case DB_TYPE_DOUBLE:
    case DB_TYPE_BLOB:
    case DB_TYPE_CLOB:
    case DB_TYPE_TIME:
    case DB_TYPE_TIMELTZ:
    case DB_TYPE_TIMETZ:
    case DB_TYPE_TIMESTAMP:
    case DB_TYPE_TIMESTAMPLTZ:
    case DB_TYPE_TIMESTAMPTZ:
    case DB_TYPE_DATETIME:
    case DB_TYPE_DATETIMELTZ:
    case DB_TYPE_DATETIMETZ:
    case DB_TYPE_DATE:
    case DB_TYPE_MONETARY:
    case DB_TYPE_SHORT:
      /* 
       * these domains have no parameters, they match if asc/desc are the
       * same
       */
      while (domain)
	{
	  if (domain->is_desc == transient->is_desc)
	    {
	      match = 1;
	      break;
	    }

	  *ins_pos = domain;
	  domain = domain->next_list;
	}
      break;

    case DB_TYPE_VOBJ:
    case DB_TYPE_OBJECT:
    case DB_TYPE_SUB:

      while (domain)
	{
	  /* 
	   * if "exact" is zero, we should be checking the subclass hierarchy
	   * of domain to see id transient is in it !
	   */

	  /* Always prefer comparison of MOPs */
	  if (domain->class_mop != NULL && transient->class_mop != NULL)
	    {
	      match = (domain->class_mop == transient->class_mop);
	    }
	  else if (domain->class_mop == NULL && transient->class_mop == NULL)
	    {
	      match = OID_EQ (&domain->class_oid, &transient->class_oid);
	    }
	  else
	    {
	      /* 
	       * We have a mixture of OID & MOPS, it probably isn't necessary
	       * to be this general but try to avoid assuming the class OIDs
	       * have been set when there is a MOP present.
	       */
	      if (domain->class_mop != NULL)
		{
		  match = OID_EQ (WS_OID (domain->class_mop), &transient->class_oid);
		}
	    }

	  if (match == 0 && exact == TP_SET_MATCH && domain->class_mop == NULL && OID_ISNULL (&domain->class_oid))
	    {
	      /* check for asc/desc */
	      if (domain->is_desc == transient->is_desc)
		{
		  match = 1;
		}
	    }

	  if (match)
	    {
	      break;
	    }

	  *ins_pos = domain;
	  domain = domain->next_list;
	}
      break;

    case DB_TYPE_VARIABLE:
    case DB_TYPE_SET:
    case DB_TYPE_MULTISET:
    case DB_TYPE_SEQUENCE:
      {
	int dsize2;

	dsize2 = tp_domain_size (transient->setdomain);
	while (domain)
	  {
#if 1
	    /* >>>>> NEED MORE CONSIDERATION <<<<< do not check order must be rollback with tp_domain_add() */
	    if (domain->setdomain == transient->setdomain)
	      {
		match = 1;
	      }
	    else
	      {
		int dsize1;

		/* 
		 * don't bother comparing the lists unless the sizes are the
		 * same
		 */
		dsize1 = tp_domain_size (domain->setdomain);
		if (dsize1 > dsize2)
		  {
		    break;
		  }

		if (dsize1 == dsize2)
		  {

		    /* handle the simple single domain case quickly */
		    if (dsize1 == 1)
		      {
			match = tp_domain_match (domain->setdomain, transient->setdomain, exact);
		      }
		    else
		      {
			TP_DOMAIN *d1, *d2;

			match = 1;
			for (d1 = domain->setdomain, d2 = transient->setdomain; d1 != NULL && d2 != NULL;
			     d1 = d1->next, d2 = d2->next)
			  {
			    if (!tp_domain_match (d1, d2, exact))
			      {
				match = 0;
				break;	/* immediately exit for loop */
			      }
			  }	/* for */
		      }
		  }		/* if (dsize1 == dsize2) */
	      }
#else /* #if 1 */
	    if (domain->setdomain == transient->setdomain)
	      match = 1;

	    else
	      {
		int dsize;

		/* 
		 * don't bother comparing the lists unless the sizes are the
		 * same
		 */
		dsize = tp_domain_size (domain->setdomain);
		if (dsize == tp_domain_size (transient->setdomain))
		  {

		    /* handle the simple single domain case quickly */
		    if (dsize == 1)
		      {
			match = tp_domain_match (domain->setdomain, transient->setdomain, exact);
		      }
		    else
		      {
			TP_DOMAIN *d1, *d2;

			/* clear the visited flag of second subdomain list */
			for (d2 = transient->setdomain; d2 != NULL; d2 = d2->next)
			  {
			    d2->is_visited = 0;
			  }

			match = 1;
			for (d1 = domain->setdomain; d1 != NULL && match; d1 = d1->next)
			  {
			    for (d2 = transient->setdomain; d2 != NULL; d2 = d2->next)
			      {
				if (!d2->is_visited && tp_domain_match (d1, d2, exact))
				  {
				    break;
				  }
			      }
			    /* did we find the domain in the other list ? */
			    if (d2 != NULL)
			      {
				d2->is_visited = 1;
			      }
			    else
			      {
				match = 0;
			      }
			  }
		      }
		  }
	      }
#endif /* #if 1 */

	    if (match)
	      {
		break;
	      }

	    *ins_pos = domain;
	    domain = domain->next_list;
	  }
      }
      break;

    case DB_TYPE_MIDXKEY:
      {
	int dsize2;

	dsize2 = tp_setdomain_size (transient);
	while (domain)
	  {
	    if (domain->setdomain == transient->setdomain)
	      {
		match = 1;
	      }
	    else
	      {
		int i, dsize1;
		TP_DOMAIN *element_dom1;
		TP_DOMAIN *element_dom2;

		dsize1 = tp_setdomain_size (domain);
		if (dsize1 > dsize2)
		  {
		    break;
		  }

		if (dsize1 == dsize2)
		  {
		    match = 1;
		    element_dom1 = domain->setdomain;
		    element_dom2 = transient->setdomain;

		    for (i = 0; i < dsize1; i++)
		      {
			match = tp_domain_match (element_dom1, element_dom2, exact);
			if (match == 0)
			  {
			    break;
			  }

			element_dom1 = element_dom1->next;
			element_dom2 = element_dom2->next;
		      }
		  }
	      }
	    if (match)
	      {
		break;
	      }

	    *ins_pos = domain;
	    domain = domain->next_list;
	  }
      }
      break;

    case DB_TYPE_VARCHAR:
      while (domain)
	{
	  if (exact == TP_EXACT_MATCH || exact == TP_SET_MATCH)
	    {
	      /* check for descending order */
	      if (domain->precision < transient->precision)
		{
		  break;
		}

	      match = ((domain->precision == transient->precision) && (domain->collation_id == transient->collation_id)
		       && (domain->is_desc == transient->is_desc)
		       && (domain->collation_flag == transient->collation_flag));
	    }
	  else if (exact == TP_STR_MATCH)
	    {
	      /* 
	       * Allow the match if the precisions would allow us to reuse the
	       * string without modification.
	       */
	      match = ((domain->precision >= transient->precision) && (domain->collation_id == transient->collation_id)
		       && (domain->is_desc == transient->is_desc)
		       && (domain->collation_flag == transient->collation_flag));
	    }
	  else
	    {
	      /* 
	       * Allow matches regardless of precision, let the actual length
	       * of the value determine if it can be assigned.  This is
	       * important for literal strings as their precision will be the
	       * maximum but they can still be assigned to domains with a
	       * smaller precision provided the actual value is within the
	       * destination domain tolerance.
	       */
	      match = ((domain->collation_id == transient->collation_id) && (domain->is_desc == transient->is_desc)
		       && (domain->collation_flag == transient->collation_flag));
	    }

	  if (match)
	    {
	      assert (domain->codeset == transient->codeset);
	      break;
	    }

	  *ins_pos = domain;
	  domain = domain->next_list;
	}
      break;

    case DB_TYPE_VARBIT:
      while (domain)
	{
	  if (exact == TP_EXACT_MATCH || exact == TP_SET_MATCH)
	    {
	      /* check for descending order */
	      if (domain->precision < transient->precision)
		{
		  break;
		}

	      match = ((domain->precision == transient->precision) && (domain->is_desc == transient->is_desc));
	    }
	  else if (exact == TP_STR_MATCH)
	    {
	      /* 
	       * Allow the match if the precisions would allow us to reuse the
	       * string without modification.
	       */
	      match = ((domain->precision >= transient->precision) && (domain->is_desc == transient->is_desc));
	    }
	  else
	    {
	      /* 
	       * Allow matches regardless of precision, let the actual length
	       * of the value determine if it can be assigned.  This is
	       * important for literal strings as their precision will be the
	       * maximum but they can still be assigned to domains with a
	       * smaller precision provided the actual value is within the
	       * destination domain tolerance.
	       */
	      match = (domain->is_desc == transient->is_desc);
	    }

	  if (match)
	    {
	      break;
	    }

	  *ins_pos = domain;
	  domain = domain->next_list;
	}
      break;

    case DB_TYPE_BIT:
      while (domain)
	{
	  /* 
	   * Unlike varchar, we have to be a little tighter on domain matches
	   * for fixed width char.  Not as much of a problem since these won't
	   * be used for literal strings.
	   */
	  if (exact == TP_EXACT_MATCH || exact == TP_STR_MATCH || exact == TP_SET_MATCH)
	    {
	      if (domain->precision > transient->precision)
		{
		  break;
		}

	      match = ((domain->precision == transient->precision) && (domain->is_desc == transient->is_desc));
	    }
	  else
	    {
	      /* 
	       * Recognize a precision of TP_FLOATING_PRECISION_VALUE to
	       * indicate a precision whose coercability must be determined
	       * by examing the value.  This is used primarily by db_coerce()
	       * since it must pick a reasonable CHAR domain for the
	       * representation of a literal string.
	       * Accept zero here too since it seems to creep into domains
	       * sometimes.
	       */
	      match =
		((transient->precision == 0 || transient->precision == TP_FLOATING_PRECISION_VALUE
		  || domain->precision >= transient->precision) && (domain->is_desc == transient->is_desc));
	    }

	  if (match)
	    {
	      break;
	    }

	  *ins_pos = domain;
	  domain = domain->next_list;
	}
      break;

    case DB_TYPE_CHAR:
    case DB_TYPE_NCHAR:
      while (domain)
	{
	  if (exact == TP_EXACT_MATCH || exact == TP_STR_MATCH || exact == TP_SET_MATCH)
	    {
	      if (domain->precision > transient->precision)
		{
		  break;
		}

	      match = ((domain->precision == transient->precision) && (domain->collation_id == transient->collation_id)
		       && (domain->is_desc == transient->is_desc)
		       && (domain->collation_flag == transient->collation_flag));
	    }
	  else
	    {
	      /* 
	       * see discussion of special domain precision values
	       * in the DB_TYPE_CHAR case above.
	       */
	      match = ((domain->collation_id == transient->collation_id)
		       && (transient->precision == 0 || (transient->precision == TP_FLOATING_PRECISION_VALUE)
			   || domain->precision >= transient->precision) && (domain->is_desc == transient->is_desc)
		       && (domain->collation_flag == transient->collation_flag));
	    }

	  if (match)
	    {
	      assert (domain->codeset == transient->codeset);
	      break;
	    }

	  *ins_pos = domain;
	  domain = domain->next_list;
	}

      break;

    case DB_TYPE_VARNCHAR:
      while (domain)
	{
	  if (exact == TP_EXACT_MATCH || exact == TP_STR_MATCH || exact == TP_SET_MATCH)
	    {
	      /* check for descending order */
	      if (domain->precision < transient->precision)
		{
		  break;
		}

	      match = ((domain->precision == transient->precision) && (domain->collation_id == transient->collation_id)
		       && (domain->is_desc == transient->is_desc)
		       && (domain->collation_flag == transient->collation_flag));
	    }
	  else
	    {
	      /* see notes above under the DB_TYPE_VARCHAR clause */
	      match = ((domain->collation_id == transient->collation_id) && (domain->is_desc == transient->is_desc)
		       && (domain->collation_flag == transient->collation_flag));
	    }

	  if (match)
	    {
	      assert (domain->codeset == transient->codeset);
	      break;
	    }

	  *ins_pos = domain;
	  domain = domain->next_list;
	}
      break;

    case DB_TYPE_NUMERIC:
      /* 
       * The first domain is a default domain for numeric type,
       * actually NUMERIC(15,0). We try to match it first.
       */
      if (transient->precision == domain->precision && transient->scale == domain->scale
	  && transient->is_desc == domain->is_desc)
	{
	  match = 1;
	  break;
	}

      domain = domain->next_list;
      while (domain)
	{
	  /* 
	   * The other domains for numeric values are sorted
	   * by descending order of precision and scale.
	   */
	  if ((domain->precision < transient->precision)
	      || ((domain->precision == transient->precision) && (domain->scale < transient->scale)))
	    {
	      break;
	    }

	  /* 
	   * note that we never allow inexact matches here because
	   * the mr_setmem_numeric function is not currently able
	   * to perform the deferred coercion.
	   */
	  match = ((domain->precision == transient->precision) && (domain->scale == transient->scale)
		   && (domain->is_desc == transient->is_desc));
	  if (match)
	    {
	      break;
	    }

	  *ins_pos = domain;
	  domain = domain->next_list;
	}
      break;

    case DB_TYPE_POINTER:
    case DB_TYPE_ERROR:
    case DB_TYPE_OID:
    case DB_TYPE_DB_VALUE:
      /* 
       * These are internal domains, they shouldn't be seen, in case they are,
       * just let them match without parameters.
       */

      while (domain)
	{
	  if (domain->is_desc == transient->is_desc)
	    {
	      match = 1;
	      break;
	    }

	  *ins_pos = domain;
	  domain = domain->next_list;
	}
      break;

    case DB_TYPE_ENUMERATION:
      while (domain != NULL)
	{
	  if (tp_enumeration_match (&DOM_GET_ENUMERATION (domain), &DOM_GET_ENUMERATION (transient)) != 0)
	    {
	      match = 1;
	      break;
	    }
	  domain = domain->next_list;
	}
      break;

    case DB_TYPE_RESULTSET:
    case DB_TYPE_TABLE:
      break;

      /* don't have a default so we make sure to add clauses for all types */
    }

  return (match ? domain : NULL);
}

#if !defined (SERVER_MODE)
/*
 * tp_swizzle_oid - swizzle oid of a domain class recursively
 *    return: void
 *    domain(in): domain to swizzle
 * Note:
 *   If the code caching the domain was written for the server, we will
 *   only have the OID of the class here if this is an object domain.  If
 *   the domain table is being shared by the client and server (e.g. in
 *   standalone mode), it is important that we "swizzle" the OID into
 *   a corresponding workspace MOP during the cache.  This ensures that we
 *   never get an object domain entered into the client's domain table that
 *   doesn't have a real DB_OBJECT pointer for the domain class.  There is
 *   a lot of code that expects this to be the case.
 */
static void
tp_swizzle_oid (TP_DOMAIN * domain)
{
  TP_DOMAIN *d;
  DB_TYPE type;

  type = TP_DOMAIN_TYPE (domain);

  if ((type == DB_TYPE_OBJECT || type == DB_TYPE_OID || type == DB_TYPE_VOBJ) && domain->class_mop == NULL
      && !OID_ISNULL (&domain->class_oid))
    {
      /* swizzle the pointer if we're on the client */
      domain->class_mop = ws_mop (&domain->class_oid, NULL);
    }
  else if (TP_IS_SET_TYPE (type))
    {
      for (d = domain->setdomain; d != NULL; d = d->next)
	{
	  tp_swizzle_oid (d);
	}
    }
}
#endif /* !SERVER_MODE */

/*
 * tp_domain_find_noparam - get domain for give type
 *    return: domain
 *    type(in): domain type
 *    is_desc(in): desc order for index key_type
 */
TP_DOMAIN *
tp_domain_find_noparam (DB_TYPE type, bool is_desc)
{
  TP_DOMAIN *dom;

  /* tp_domain_find_with_no_param */
  /* type : DB_TYPE_NULL DB_TYPE_INTEGER DB_TYPE_FLOAT DB_TYPE_DOUBLE DB_TYPE_ELO DB_TYPE_TIME DB_TYPE_BLOB
   * DB_TYPE_CLOB DB_TYPE_TIMESTAMP DB_TYPE_DATE DB_TYPE_DATETIME DB_TYPE_MONETARY DB_TYPE_SHORT DB_TYPE_BIGINT
   * DB_TYPE_TIMESTAMPTZ DB_TYPE_TIMESTAMPLTZ DB_TYPE_DATETIMETZ DB_TYPE_DATETIMELTZ DB_TYPE_TIMETZ DB_TYPE_TIMELTZ */

  for (dom = tp_domain_get_list (type, NULL); dom != NULL; dom = dom->next_list)
    {
      if (dom->is_desc == is_desc)
	{
	  break;		/* found */
	}
    }

  return dom;
}

/*
 * tp_domain_find_numeric - find domain for given type, precision and scale
 *    return: domain that matches
 *    type(in): DB_TYPE
 *    precision(in): precision
 *    scale(in): scale
 *    is_desc(in): desc order for index key_type
 */
TP_DOMAIN *
tp_domain_find_numeric (DB_TYPE type, int precision, int scale, bool is_desc)
{
  TP_DOMAIN *dom;

  /* tp_domain_find_with_precision_scale */
  /* type : DB_TYPE_NUMERIC */
  assert (type == DB_TYPE_NUMERIC);

  /* 
   * The first domain is a default domain for numeric type,
   * actually NUMERIC(15,0). We try to match it first.
   */
  dom = tp_domain_get_list (type, NULL);
  if (precision == dom->precision && scale == dom->scale && is_desc == dom->is_desc)
    {
      return dom;
    }

  /* search the list for a domain that matches */
  for (dom = dom->next_list; dom != NULL; dom = dom->next_list)
    {
      if ((precision > dom->precision) || ((precision == dom->precision) && (scale > dom->scale)))
	{
	  return NULL;		/* not exist */
	}

      /* we MUST perform exact matches here */
      if (dom->precision == precision && dom->scale == scale && dom->is_desc == is_desc)
	{
	  break;		/* found */
	}
    }

  return dom;
}

/*
 * tp_domain_find_charbit - find domain for given codeset and precision
 *    return: domain that matches
 *    type(in): DB_TYPE
 *    codeset(in): code set
 *    collation_id(in): collation id
 *    precision(in): precision
 *    is_desc(in): desc order for index key_type
 */
TP_DOMAIN *
tp_domain_find_charbit (DB_TYPE type, int codeset, int collation_id, unsigned char collation_flag, int precision,
			bool is_desc)
{
  TP_DOMAIN *dom;

  /* tp_domain_find_with_codeset_precision */
  /* 
   * type : DB_TYPE_NCHAR   DB_TYPE_VARNCHAR
   * DB_TYPE_CHAR    DB_TYPE_VARCHAR
   * DB_TYPE_BIT     DB_TYPE_VARBIT
   */
  assert (type == DB_TYPE_CHAR || type == DB_TYPE_VARCHAR || type == DB_TYPE_NCHAR || type == DB_TYPE_VARNCHAR
	  || type == DB_TYPE_BIT || type == DB_TYPE_VARBIT);

  if (type == DB_TYPE_VARCHAR || type == DB_TYPE_VARNCHAR || type == DB_TYPE_VARBIT)
    {
      /* search the list for a domain that matches */
      for (dom = tp_domain_get_list (type, NULL); dom != NULL; dom = dom->next_list)
	{
	  /* Variable character/bit is sorted in descending order of precision. */
	  if (precision > dom->precision)
	    {
	      return NULL;	/* not exist */
	    }

	  /* we MUST perform exact matches here */
	  if (dom->precision == precision && dom->is_desc == is_desc)
	    {
	      if (type == DB_TYPE_VARBIT)
		{
		  break;	/* found */
		}
	      else if (dom->collation_id == collation_id && dom->collation_flag == collation_flag)
		{
		  /* codeset should be the same if collations are equal */
		  assert (dom->codeset == codeset);
		  break;
		}
	    }
	}
    }
  else
    {
      /* search the list for a domain that matches */
      for (dom = tp_domain_get_list (type, NULL); dom != NULL; dom = dom->next_list)
	{
	  /* Fixed character/bit is sorted in ascending order of precision. */
	  if (precision < dom->precision)
	    {
	      return NULL;	/* not exist */
	    }

	  /* we MUST perform exact matches here */
	  if (dom->precision == precision && dom->is_desc == is_desc)
	    {
	      if (type == DB_TYPE_BIT)
		{
		  break;	/* found */
		}
	      else if (dom->collation_id == collation_id && dom->collation_flag == collation_flag)
		{
		  /* codeset should be the same if collations are equal */
		  assert (dom->codeset == codeset);
		  break;
		}
	    }
	}
    }

  return dom;
}

/*
 * tp_domain_find_object - find domain for given class OID and class
 *    return: domain that matches
 *    type(in): DB_TYPE
 *    class_oid(in): class oid
 *    class_mop(in): class structure
 *    is_desc(in): desc order for index key_type
 */
TP_DOMAIN *
tp_domain_find_object (DB_TYPE type, OID * class_oid, struct db_object * class_mop, bool is_desc)
{
  TP_DOMAIN *dom;

  /* tp_domain_find_with_classinfo */

  /* search the list for a domain that matches */
  for (dom = tp_domain_get_list (type, NULL); dom != NULL; dom = dom->next_list)
    {
      /* we MUST perform exact matches here */

      /* Always prefer comparison of MOPs */
      if (dom->class_mop != NULL && class_mop != NULL)
	{
	  if (dom->class_mop == class_mop && dom->is_desc == is_desc)
	    {
	      break;		/* found */
	    }
	}
      else if (dom->class_mop == NULL && class_mop == NULL)
	{
	  if (OID_EQ (&dom->class_oid, class_oid) && dom->is_desc == is_desc)
	    {
	      break;		/* found */
	    }
	}
      else
	{
	  /* 
	   * We have a mixture of OID & MOPS, it probably isn't necessary to be
	   * this general but try to avoid assuming the class OIDs have been set
	   * when there is a MOP present.
	   */
	  if (dom->class_mop == NULL)
	    {
	      if (OID_EQ (&dom->class_oid, WS_OID (class_mop)) && dom->is_desc == is_desc)
		{
		  break;	/* found */
		}
	    }
	  else
	    {
	      if (OID_EQ (WS_OID (dom->class_mop), class_oid) && dom->is_desc == is_desc)
		{
		  break;	/* found */
		}
	    }
	}
    }

  return dom;
}

/*
 * tp_domain_find_set - find domain that matches for given set domain
 *    return: domain that matches
 *    type(in): DB_TYPE
 *    setdomain(in): set domain
 *    is_desc(in): desc order for index key_type
 */
TP_DOMAIN *
tp_domain_find_set (DB_TYPE type, TP_DOMAIN * setdomain, bool is_desc)
{
  TP_DOMAIN *dom;
  int dsize;
  int src_dsize;

  src_dsize = tp_domain_size (setdomain);

  /* search the list for a domain that matches */
  for (dom = tp_domain_get_list (type, setdomain); dom != NULL; dom = dom->next_list)
    {
      /* we MUST perform exact matches here */
      if (dom->setdomain == setdomain)
	{
	  break;
	}

      if (dom->is_desc == is_desc)
	{
	  /* don't bother comparing the lists unless the sizes are the same */
	  dsize = tp_setdomain_size (dom);
	  if (dsize == src_dsize)
	    {
	      /* handle the simple single domain case quickly */
	      if (dsize == 1)
		{
		  if (tp_domain_match (dom->setdomain, setdomain, TP_EXACT_MATCH))
		    {
		      break;
		    }
		}
	      else
		{
		  TP_DOMAIN *d1, *d2;
		  int match, i;

		  if (type == DB_TYPE_SEQUENCE || type == DB_TYPE_MIDXKEY)
		    {
		      if (dsize == src_dsize)
			{
			  match = 1;
			  d1 = dom->setdomain;
			  d2 = setdomain;

			  for (i = 0; i < dsize; i++)
			    {
			      match = tp_domain_match (d1, d2, TP_EXACT_MATCH);
			      if (match == 0)
				{
				  break;
				}
			      d1 = d1->next;
			      d2 = d2->next;
			    }
			  if (match == 1)
			    {
			      break;
			    }
			}
		    }
		  else
		    {
		      /* clear the visited flag in the second subdomain list */
		      for (d2 = setdomain; d2 != NULL; d2 = d2->next)
			{
			  d2->is_visited = 0;
			}

		      match = 1;
		      for (d1 = dom->setdomain; d1 != NULL && match; d1 = d1->next)
			{
			  for (d2 = setdomain; d2 != NULL; d2 = d2->next)
			    {
			      if (!d2->is_visited && tp_domain_match (d1, d2, TP_EXACT_MATCH))
				{
				  break;
				}
			    }
			  /* did we find the domain in the other list ? */
			  if (d2 != NULL)
			    {
			      d2->is_visited = 1;
			    }
			  else
			    {
			      match = 0;
			    }
			}

		      if (match == 1)
			{
			  break;
			}
		    }
		}
	    }
	}
    }

  return dom;
}

/*
 * tp_domain_find_enumeration () - Find a chached domain with this enumeration
 * enumeration(in): enumeration to look for
 * is_desc(in): true if desc, false if asc (for only index key)
 */
TP_DOMAIN *
tp_domain_find_enumeration (const DB_ENUMERATION * enumeration, bool is_desc)
{
  TP_DOMAIN *dom = NULL;
  DB_ENUM_ELEMENT *db_enum1 = NULL, *db_enum2 = NULL;

  /* search the list for a domain that matches */
  for (dom = tp_domain_get_list (DB_TYPE_ENUMERATION, NULL); dom != NULL; dom = dom->next_list)
    {
      if (dom->is_desc == is_desc && tp_enumeration_match (&DOM_GET_ENUMERATION (dom), enumeration))
	{
	  return dom;
	}
    }

  return NULL;
}

/*
 * tp_domain_cache - caches a transient domain
 *    return: cached domain
 *    transient(in/out): transient domain
 * Note:
 *    If the domain has already been cached, it is located and returned.
 *    Otherwise, a new domain is cached and returned.
 *    In either case, the transient domain may be freed so you should never
 *    depend on it being valid after this function returns.
 *
 *    Note that if a new domain is added to the list, it is always appended
 *    to the end.  It is vital that the deafult "built-in" domain be
 *    at the head of the domain lists in tp_Domains.
 */
TP_DOMAIN *
tp_domain_cache (TP_DOMAIN * transient)
{
  TP_DOMAIN *domain, **dlist;
  TP_DOMAIN *ins_pos = NULL;
#if defined (SERVER_MODE)
  int rv;
#endif /* SERVER_MODE */

  /* guard against a bad transient domain */
  if (transient == NULL || transient->type == NULL)
    {
      return NULL;
    }

  /* return this domain if its already cached */
  if (transient->is_cached)
    {
      return transient;
    }

#if !defined (SERVER_MODE)
  /* see comments for tp_swizzle_oid */
  tp_swizzle_oid (transient);
#endif /* !SERVER_MODE */

  /* 
   * first search stage: NO LOCK
   */
  /* locate the root of the cache list for domains of this type */
  dlist = tp_domain_get_list_ptr (TP_DOMAIN_TYPE (transient), transient->setdomain);

  /* search the list for a domain that matches */
  if (*dlist != NULL)
    {
      ins_pos = NULL;
      domain = tp_is_domain_cached (*dlist, transient, TP_EXACT_MATCH, &ins_pos);
      if (domain != NULL)
	{
	  /* 
	   * We found one in the cache, free the supplied domain and return
	   * the cached one
	   */
	  tp_domain_free (transient);
	  return domain;
	}
    }

  /* 
   * second search stage: LOCK
   */
#if defined (SERVER_MODE)
  rv = pthread_mutex_lock (&tp_domain_cache_lock);	/* LOCK */

  /* locate the root of the cache list for domains of this type */
  dlist = tp_domain_get_list_ptr (TP_DOMAIN_TYPE (transient), transient->setdomain);

  /* search the list for a domain that matches */
  if (*dlist != NULL)
    {
      ins_pos = NULL;
      domain = tp_is_domain_cached (*dlist, transient, TP_EXACT_MATCH, &ins_pos);
      if (domain != NULL)
	{
	  /* 
	   * We found one in the cache, free the supplied domain and return
	   * the cached one
	   */
	  tp_domain_free (transient);
	  pthread_mutex_unlock (&tp_domain_cache_lock);
	  return domain;
	}
    }
#endif /* SERVER_MODE */

  /* 
   * We couldn't find one, install the transient domain that was passed in.
   * Since by far the most common domain match is going to be the built-in
   * domain at the head of the list, append new domains to the end of the
   * list as they are encountered.
   */
  transient->is_cached = 1;

  if (*dlist)
    {
      if (ins_pos)
	{
	  TP_DOMAIN *tmp;

	  tmp = ins_pos->next_list;
	  ins_pos->next_list = transient;
	  transient->next_list = tmp;
	}
    }
  else
    {
      *dlist = transient;
    }

  domain = transient;

#if defined (SERVER_MODE)
  pthread_mutex_unlock (&tp_domain_cache_lock);
#endif /* SERVER_MODE */

  return domain;
}

/*
 * tp_domain_resolve - Find a domain object that matches the type, class,
 * precision, scale and setdomain.
 *    return: domain found
 *    domain_type(in): The basic type of the domain
 *    class_obj(in): The class of the domain (for DB_TYPE_OBJECT)
 *    precision(in): The precision of the domain
 *    scale(in): The class of the domain
 *    setdomain(in): The setdomain of the domain
 *    collation(in): The collation of domain
 * Note:
 *    Current implementation just creates a new one then returns it.
 */
TP_DOMAIN *
tp_domain_resolve (DB_TYPE domain_type, DB_OBJECT * class_obj, int precision, int scale, TP_DOMAIN * setdomain,
		   int collation)
{
  TP_DOMAIN *d;

  d = tp_domain_new (domain_type);
  if (d != NULL)
    {
      d->precision = precision;
      d->scale = scale;
      d->class_mop = class_obj;
      d->setdomain = setdomain;
      if (TP_TYPE_HAS_COLLATION (domain_type))
	{
	  LANG_COLLATION *lc;
	  d->collation_id = collation;

	  lc = lang_get_collation (collation);
	  d->codeset = lc->codeset;
	}

      d = tp_domain_cache (d);
    }

  return d;
}

/*
 * tp_domain_resolve_default - returns the built-in "default" domain for a
 * given primitive type
 *    return: cached domain
 *    type(in): type id
 * Note:
 *    This is used only in special cases where we need to get quickly to
 *    a built-in domain without worrying about domain parameters.
 *    Note that this relies on the fact that the built-in domain is at
 *    the head of our domain lists.
 */
TP_DOMAIN *
tp_domain_resolve_default (DB_TYPE type)
{
  if (type >= sizeof (tp_Domains) / sizeof (tp_Domains[0]))
    {
      assert_release (false);
      return NULL;
    }

  return tp_Domains[type];
}

/*
 * tp_domain_resolve_default_w_coll -
 *
 *    return: cached domain
 *    type(in): type id
 *    coll_id(in): collation
 *    coll_flag(in): collation flag
 * Note:
 *  It returns a special domain having the desired collation and collation
 *  mode flag. Use this in context of type inference for argument coercion 
 */
TP_DOMAIN *
tp_domain_resolve_default_w_coll (DB_TYPE type, int coll_id, TP_DOMAIN_COLL_ACTION coll_flag)
{
  TP_DOMAIN *default_dom;
  TP_DOMAIN *resolved_dom;

  default_dom = tp_domain_resolve_default (type);

  if (TP_TYPE_HAS_COLLATION (type) && coll_flag != TP_DOMAIN_COLL_NORMAL)
    {
      resolved_dom = tp_domain_copy (default_dom, false);

      if (coll_flag == TP_DOMAIN_COLL_ENFORCE)
	{
	  LANG_COLLATION *lc = lang_get_collation (coll_id);

	  resolved_dom->collation_id = coll_id;
	  resolved_dom->codeset = lc->codeset;
	}
      else
	{
	  assert (coll_flag == TP_DOMAIN_COLL_LEAVE);
	}

      resolved_dom->collation_flag = coll_flag;

      resolved_dom = tp_domain_cache (resolved_dom);
    }
  else
    {
      resolved_dom = default_dom;
    }

  return resolved_dom;
}


/*
 * tp_domain_resolve_value - Find a domain object that describes the type info
 * in the DB_VALUE.
 *    return: domain found
 *    val(in): A DB_VALUE for which we need to obtain a domain
 *    dbuf(out): if not NULL, founded domain initialized on dbuf
 */
TP_DOMAIN *
tp_domain_resolve_value (DB_VALUE * val, TP_DOMAIN * dbuf)
{
  TP_DOMAIN *domain;
  DB_TYPE value_type;

  domain = NULL;
  value_type = DB_VALUE_DOMAIN_TYPE (val);

  if (TP_IS_SET_TYPE (value_type))
    {
      DB_SET *set;
      /* 
       * For sets, just return the domain attached to the set since it
       * will already have been cached.
       */
      set = db_get_set (val);
      if (set != NULL)
	{
	  domain = set_get_domain (set);
	  /* handle case of incomplete set domain: build full domain */
	  if (domain->setdomain == NULL || tp_domain_check (domain, val, TP_EXACT_MATCH) != DOMAIN_COMPATIBLE)
	    {
	      if (domain->is_cached)
		{
		  domain = tp_domain_new (value_type);
		}

	      if (domain != NULL)
		{
		  int err_status;

		  err_status = setobj_build_domain_from_col (set->set, &(domain->setdomain));
		  if (err_status != NO_ERROR && !domain->is_cached)
		    {
		      tp_domain_free (domain);
		      domain = NULL;
		    }
		  else
		    {
		      /* cache this new domain */
		      domain = tp_domain_cache (domain);
		    }
		}
	    }
	}
      else
	{
	  /* we need to synthesize a wildcard set domain for this value */
	  domain = tp_domain_resolve_default (value_type);
	}
    }
  else if (value_type == DB_TYPE_MIDXKEY)
    {
      DB_MIDXKEY *midxkey;

      /* For midxkey type, return the domain attached to the value */
      midxkey = DB_GET_MIDXKEY (val);
      if (midxkey != NULL)
	{
	  domain = midxkey->domain;
	}
      else
	{
	  assert (DB_VALUE_TYPE (val) == DB_TYPE_NULL);
	  domain = tp_domain_resolve_default (value_type);
	}
    }
  else
    {
      switch (value_type)
	{
	case DB_TYPE_NULL:
	case DB_TYPE_INTEGER:
	case DB_TYPE_BIGINT:
	case DB_TYPE_FLOAT:
	case DB_TYPE_DOUBLE:
	case DB_TYPE_BLOB:
	case DB_TYPE_CLOB:
	case DB_TYPE_TIME:
	case DB_TYPE_TIMELTZ:
	case DB_TYPE_TIMETZ:
	case DB_TYPE_TIMESTAMP:
	case DB_TYPE_TIMESTAMPTZ:
	case DB_TYPE_TIMESTAMPLTZ:
	case DB_TYPE_DATE:
	case DB_TYPE_DATETIME:
	case DB_TYPE_DATETIMETZ:
	case DB_TYPE_DATETIMELTZ:
	case DB_TYPE_MONETARY:
	case DB_TYPE_SHORT:
	  /* domains without parameters, return the built-in domain */
	  domain = tp_domain_resolve_default (value_type);
	  break;

	case DB_TYPE_OBJECT:
	  {
#if !defined (SERVER_MODE)
	    DB_OBJECT *mop;

	    domain = &tp_Object_domain;
	    mop = db_get_object (val);
	    if ((mop == NULL) || (WS_IS_DELETED (mop)))
	      {
		/* just let the oid thing stand?, this is a NULL anyway */
	      }
	    else
	      {
		/* this is a virtual mop */
		if (WS_ISVID (mop))
		  {
		    domain = &tp_Vobj_domain;
		  }
	      }
#else
	    /* We don't have mops on server */
	    assert (false);
#endif
	  }
	  break;

	case DB_TYPE_OID:
	  domain = &tp_Object_domain;
	  break;

	case DB_TYPE_CHAR:
	case DB_TYPE_VARCHAR:	/* new name for DB_TYPE_STRING */
	case DB_TYPE_BIT:
	case DB_TYPE_VARBIT:
	case DB_TYPE_NCHAR:
	case DB_TYPE_VARNCHAR:
	  /* must find one with a matching precision */
	  if (dbuf == NULL)
	    {
	      domain = tp_domain_new (value_type);
	      if (domain == NULL)
		{
		  return NULL;
		}
	    }
	  else
	    {
	      domain = dbuf;
	      domain_init (domain, value_type);
	    }
	  domain->codeset = db_get_string_codeset (val);
	  domain->collation_id = db_get_string_collation (val);
	  domain->precision = db_value_precision (val);

	  /* 
	   * Convert references to the "floating" precisions to actual
	   * precisions.  This may not be necessary or desireable?
	   * Zero seems to pop up occasionally in DB_VALUE precisions, until
	   * this is fixed, treat it as the floater for the variable width
	   * types.
	   */
	  if (TP_DOMAIN_TYPE (domain) == DB_TYPE_VARCHAR)
	    {
	      if (domain->precision == 0 || domain->precision == TP_FLOATING_PRECISION_VALUE
		  || domain->precision > DB_MAX_VARCHAR_PRECISION)
		{
		  domain->precision = DB_MAX_VARCHAR_PRECISION;
		}
	    }
	  else if (TP_DOMAIN_TYPE (domain) == DB_TYPE_VARBIT)
	    {
	      if (domain->precision == 0 || domain->precision == TP_FLOATING_PRECISION_VALUE
		  || domain->precision > DB_MAX_VARBIT_PRECISION)
		{
		  domain->precision = DB_MAX_VARBIT_PRECISION;
		}
	    }
	  else if (value_type == DB_TYPE_VARNCHAR)
	    {
	      if (domain->precision == 0 || domain->precision == TP_FLOATING_PRECISION_VALUE
		  || domain->precision >= DB_MAX_VARNCHAR_PRECISION)
		{
		  domain->precision = DB_MAX_VARNCHAR_PRECISION;
		}
	    }

	  if (dbuf == NULL)
	    {
	      domain = tp_domain_cache (domain);
	    }
	  break;
	case DB_TYPE_ENUMERATION:
	  /* 
	   * We have no choice but to return the default enumeration domain
	   * because we cannot construct the domain from a DB_VALUE
	   */
	  domain = tp_domain_resolve_default (value_type);
	  break;

	case DB_TYPE_NUMERIC:
	  /* must find one with a matching precision and scale */
	  if (dbuf == NULL)
	    {
	      domain = tp_domain_new (value_type);
	      if (domain == NULL)
		{
		  return NULL;
		}
	    }
	  else
	    {
	      domain = dbuf;
	      domain_init (dbuf, value_type);
	    }
	  domain->precision = db_value_precision (val);
	  domain->scale = db_value_scale (val);

	  /* 
	   * Hack, precision seems to be commonly -1 DB_VALUES, turn this into
	   * the default "maximum" precision.
	   * This may not be necessary any more.
	   */
	  if (domain->precision == -1)
	    {
	      domain->precision = DB_DEFAULT_NUMERIC_PRECISION;
	    }

	  if (domain->scale == -1)
	    {
	      domain->scale = DB_DEFAULT_NUMERIC_SCALE;
	    }

	  if (dbuf == NULL)
	    {
	      domain = tp_domain_cache (domain);
	    }
	  break;

	case DB_TYPE_POINTER:
	case DB_TYPE_ERROR:
	case DB_TYPE_VOBJ:
	case DB_TYPE_SUB:
	case DB_TYPE_VARIABLE:
	case DB_TYPE_DB_VALUE:
	  /* 
	   * These are internal domains, they shouldn't be seen, in case they
	   * are, match to a built-in
	   */
	  domain = tp_domain_resolve_default (value_type);
	  break;

	  /* 
	   * things handled in logic outside the switch, shuts up compiler
	   * warnings
	   */
	case DB_TYPE_SET:
	case DB_TYPE_MULTISET:
	case DB_TYPE_SEQUENCE:
	case DB_TYPE_MIDXKEY:
	  break;
	case DB_TYPE_RESULTSET:
	case DB_TYPE_TABLE:
	  break;
	}
    }

  return domain;
}

#if defined(ENABLE_UNUSED_FUNCTION)
/*
 * tp_create_domain_resolve_value - adjust domain of a DB_VALUE with respect to
 * the primitive value of the value
 *    return: domain
 *    val(in): DB_VALUE
 *    domain(in): domain
 * Note: val->domain changes
 */
TP_DOMAIN *
tp_create_domain_resolve_value (DB_VALUE * val, TP_DOMAIN * domain)
{
  DB_TYPE value_type;

  value_type = DB_VALUE_DOMAIN_TYPE (val);

  switch (value_type)
    {
    case DB_TYPE_INTEGER:
    case DB_TYPE_BIGINT:
    case DB_TYPE_FLOAT:
    case DB_TYPE_DOUBLE:
    case DB_TYPE_TIME:
    case DB_TYPE_TIMESTAMP:
    case DB_TYPE_DATE:
    case DB_TYPE_DATETIME:
    case DB_TYPE_MONETARY:
    case DB_TYPE_SHORT:
    case DB_TYPE_OBJECT:
    case DB_TYPE_OID:
      break;

    case DB_TYPE_CHAR:
    case DB_TYPE_BIT:
    case DB_TYPE_NCHAR:
    case DB_TYPE_VARCHAR:	/* new name for DB_TYPE_STRING */
    case DB_TYPE_VARBIT:
    case DB_TYPE_VARNCHAR:
      if (db_value_precision (val) == TP_FLOATING_PRECISION_VALUE)
	{
	  /* Check for floating precision. */
	  val->domain.char_info.length = domain->precision;
	}
      else
	{
	  if (domain->precision == TP_FLOATING_PRECISION_VALUE)
	    {
	      ;			/* nop */
	    }
	  else
	    {
	      if (db_value_precision (val) > domain->precision)
		{
		  val->domain.char_info.length = domain->precision;
		}
	    }
	}
      break;

    case DB_TYPE_NUMERIC:
      break;

    case DB_TYPE_NULL:		/* for midxkey elements */
      break;

    default:
      return NULL;
    }

  /* if(domain) return tp_domain_cache(domain); */
  return domain;
}
#endif /* ENABLE_UNUSED_FUNCTION */

/*
 * tp_domain_add - Adds a domain structure to a domain list if it doesn't
 * already exist.
 *    return: error code
 *    dlist(in/out): domain list
 *    domain(in): domain structure
 * Note:
 *    This routine should only be used to construct a transient domain.
 *    Note that there are no error messages if a duplicate isn't added.
 */
int
tp_domain_add (TP_DOMAIN ** dlist, TP_DOMAIN * domain)
{
  int error = NO_ERROR;
  TP_DOMAIN *d, *found, *last;
  DB_TYPE type_id;

  last = NULL;
  type_id = TP_DOMAIN_TYPE (domain);

  for (d = *dlist, found = NULL; d != NULL && found == NULL; d = d->next)
    {
#if 1
      /* >>>>> NEED MORE CONSIDERATION <<<<< do not check duplication must be rollback with tp_domain_match() */
#else /* 0 */
      if (TP_DOMAIN_TYPE (d) == type_id)
	{
	  switch (type_id)
	    {
	    case DB_TYPE_INTEGER:
	    case DB_TYPE_BIGINT:
	    case DB_TYPE_FLOAT:
	    case DB_TYPE_DOUBLE:
	    case DB_TYPE_BLOB:
	    case DB_TYPE_CLOB:
	    case DB_TYPE_TIME:
	    case DB_TYPE_TIMELTZ:
	    case DB_TYPE_TIMETZ:
	    case DB_TYPE_TIMESTAMP:
	    case DB_TYPE_TIMESTAMPTZ:
	    case DB_TYPE_TIMESTAMPLTZ:
	    case DB_TYPE_DATE:
	    case DB_TYPE_DATETIME:
	    case DB_TYPE_DATETIMETZ:
	    case DB_TYPE_DATETIMELTZ:
	    case DB_TYPE_MONETARY:
	    case DB_TYPE_SUB:
	    case DB_TYPE_POINTER:
	    case DB_TYPE_ERROR:
	    case DB_TYPE_SHORT:
	    case DB_TYPE_VOBJ:
	    case DB_TYPE_OID:
	    case DB_TYPE_NULL:
	    case DB_TYPE_VARIABLE:
	    case DB_TYPE_SET:
	    case DB_TYPE_MULTISET:
	    case DB_TYPE_SEQUENCE:
	    case DB_TYPE_DB_VALUE:
	    case DB_TYPE_VARCHAR:
	    case DB_TYPE_VARNCHAR:
	    case DB_TYPE_VARBIT:
	      found = d;
	      break;

	    case DB_TYPE_NUMERIC:
	      if ((d->precision == domain->precision) && (d->scale == domain->scale))
		{
		  found = d;
		}
	      break;

	    case DB_TYPE_CHAR:
	    case DB_TYPE_NCHAR:
	    case DB_TYPE_BIT:
	      /* 
	       * PR)  1.deficient character related with CHAR & VARCHAR in set.
	       * ==> distinguishing VARCHAR from CHAR.
	       * 2. core dumped & deficient character related with
	       * CONST CHAR & CHAR in set.
	       * ==> In case of CHAR,NCHAR,BIT,  cosidering precision.
	       */
	      if (d->precision == domain->precision)
		{
		  found = d;
		}
	      break;

	    case DB_TYPE_OBJECT:
	      if (d->class_mop == domain->class_mop)
		{
		  found = d;
		}
	      break;

	    default:
	      break;
	    }
	}
#endif /* 1 */

      last = d;
    }

  if (found == NULL)
    {
      if (last == NULL)
	{
	  *dlist = domain;
	}
      else
	{
	  last->next = domain;
	}
    }
  else
    {
      /* the domain already existed, free the supplied domain */
      tp_domain_free (domain);
    }

  return error;
}

#if !defined (SERVER_MODE)
/*
 * tp_domain_attach - concatenate two domains
 *    return: concatenated domain
 *    dlist(out): domain 1
 *    domain(in): domain 2
 */
int
tp_domain_attach (TP_DOMAIN ** dlist, TP_DOMAIN * domain)
{
  int error = NO_ERROR;
  TP_DOMAIN *d;

  d = *dlist;

  if (*dlist == NULL)
    {
      *dlist = domain;
    }
  else
    {
      while (d->next)
	d = d->next;

      d->next = domain;
    }

  return error;
}

/*
 * tp_domain_drop - Removes a domain from a list if it was found.
 *    return: non-zero if domain was dropped
 *    dlist(in/out): domain list
 *    domain(in/out):  domain class
 * Note:
 *    This routine should only be used to modify a transient domain.
 */
int
tp_domain_drop (TP_DOMAIN ** dlist, TP_DOMAIN * domain)
{
  TP_DOMAIN *d, *found, *prev;
  int dropped = 0;
  DB_TYPE type_id;

  type_id = TP_DOMAIN_TYPE (domain);
  for (d = *dlist, prev = NULL, found = NULL; d != NULL && found == NULL; d = d->next)
    {
      if (TP_DOMAIN_TYPE (d) == type_id)
	{
	  switch (type_id)
	    {
	    case DB_TYPE_INTEGER:
	    case DB_TYPE_BIGINT:
	    case DB_TYPE_FLOAT:
	    case DB_TYPE_DOUBLE:
	    case DB_TYPE_BLOB:
	    case DB_TYPE_CLOB:
	    case DB_TYPE_TIME:
	    case DB_TYPE_TIMELTZ:
	    case DB_TYPE_TIMETZ:
	    case DB_TYPE_TIMESTAMP:
	    case DB_TYPE_TIMESTAMPLTZ:
	    case DB_TYPE_TIMESTAMPTZ:
	    case DB_TYPE_DATE:
	    case DB_TYPE_DATETIME:
	    case DB_TYPE_DATETIMELTZ:
	    case DB_TYPE_DATETIMETZ:
	    case DB_TYPE_MONETARY:
	    case DB_TYPE_SUB:
	    case DB_TYPE_POINTER:
	    case DB_TYPE_ERROR:
	    case DB_TYPE_SHORT:
	    case DB_TYPE_VOBJ:
	    case DB_TYPE_OID:
	    case DB_TYPE_NULL:
	    case DB_TYPE_VARIABLE:
	    case DB_TYPE_SET:
	    case DB_TYPE_MULTISET:
	    case DB_TYPE_SEQUENCE:
	    case DB_TYPE_DB_VALUE:
	    case DB_TYPE_VARCHAR:
	    case DB_TYPE_VARNCHAR:
	    case DB_TYPE_VARBIT:
	      found = d;
	      break;

	    case DB_TYPE_NUMERIC:
	      if (d->precision == domain->precision && d->scale == domain->scale)
		{
		  found = d;
		}
	      break;

	    case DB_TYPE_CHAR:
	    case DB_TYPE_NCHAR:
	    case DB_TYPE_BIT:
	      /* 1.deficient character related with CHAR & VARCHAR in set. ==> distinguishing VARCHAR from CHAR. 2.
	       * core dumped & deficient character related with CONST CHAR & CHAR in set. ==> In case of
	       * CHAR,NCHAR,BIT, cosidering precision. */
	      if (d->precision == domain->precision)
		{
		  found = d;
		}
	      break;

	    case DB_TYPE_OBJECT:
	      if (d->class_mop == domain->class_mop)
		{
		  found = d;
		}
	      break;

	    default:
	      break;
	    }
	}

      if (found == NULL)
	{
	  prev = d;
	}
    }

  if (found != NULL)
    {
      if (prev == NULL)
	{
	  *dlist = found->next;
	}
      else
	{
	  prev->next = found->next;
	}

      found->next = NULL;
      tp_domain_free (found);

      dropped = 1;
    }

  return dropped;
}
#endif /* !SERVER_MODE */


/*
 * tp_domain_check_class - Work function for tp_domain_filter_list and
 * sm_filter_domain.
 *    return: error code
 *    domain(in): domain to examine
 *    change(out): non-zero if the domain was modified
 * Note:
 *    Check the class in a domain and if it was deleted, downgrade the
 *    domain to "object".
 */
static int
tp_domain_check_class (TP_DOMAIN * domain, int *change)
{
  int error = NO_ERROR;
#if !defined (SERVER_MODE)
  int status;
#endif /* !SERVER_MODE */

  if (change != NULL)
    {
      *change = 0;
    }

#if !defined (SERVER_MODE)
  if (!db_on_server)
    {
      if (domain != NULL && domain->type == tp_Type_object && domain->class_mop != NULL)
	{
	  /* check for deletion of the domain class, assume just one for now */
	  status = locator_does_exist_object (domain->class_mop, DB_FETCH_READ);

	  if (status == LC_DOESNOT_EXIST)
	    {
	      WS_SET_DELETED (domain->class_mop);
	      domain->class_mop = NULL;
	      if (change != NULL)
		{
		  *change = 1;
		}
	    }
	  else if (status == LC_ERROR)
	    {
	      ASSERT_ERROR ();
	      error = er_errid ();
	    }
	}
    }
#endif /* !SERVER_MODE */

  return error;
}


/*
 * tp_domain_filter_list - filter out any domain references to classes that
 * have been deleted or are otherwise invalid from domain list
 *    return: error code
 *    dlist(in):  domain list
 *    list_changes(out) : non-zero if changes were made
 * Note:
 *    The semantic for deleted classes is that the domain reverts
 *    to the root "object" domain, thereby allowing all object references.
 *    This could become more sophisticated but not without a lot of extra
 *    bookkeeping in the database.   If a domain is downgraded to "object",
 *    be sure to remove it from the list entirely if there is already an
 *    "object" domain present.
 */
int
tp_domain_filter_list (TP_DOMAIN * dlist, int *list_changes)
{
  TP_DOMAIN *d, *prev, *next;
  int has_object, changes, set_changes, domain_change;
  int error;

  has_object = changes = 0;
  if (list_changes != NULL)
    {
      *list_changes = 0;
    }

  for (d = dlist, prev = NULL, next = NULL; d != NULL; d = next)
    {
      next = d->next;
      error = tp_domain_check_class (d, &domain_change);
      if (error != NO_ERROR)
	{
	  return error;
	}

      if (domain_change)
	{
	  /* domain reverted to "object" */
	  if (!has_object)
	    {
	      has_object = 1;
	      prev = d;
	    }
	  else
	    {
	      /* 
	       * redundant "object" domain, remove, prev can't be NULL here,
	       * will always have at least one domain structure at the head of
	       * the list
	       */
	      prev->next = next;
	      d->next = NULL;
	      tp_domain_free (d);
	      changes |= 1;
	    }
	}
      else
	{
	  /* domain is still valid, see if its "object" */
	  if (d->type == tp_Type_object && d->class_mop == NULL)
	    {
	      has_object = 1;
	    }
	  else if (pr_is_set_type (TP_DOMAIN_TYPE (d)) && d->setdomain != NULL)
	    {
	      /* recurse on set domain list */
	      error = tp_domain_filter_list (d->setdomain, &set_changes);
	      if (error != NO_ERROR)
		{
		  return error;
		}

	      changes |= set_changes;
	    }
	  prev = d;
	}
    }

  if (list_changes != NULL)
    {
      *list_changes = changes;
    }

  return NO_ERROR;
}

/*
 * tp_domain_name - generate a printed representation for the given domain.
 *    return: non-zero if buffer overflow, -1 for error
 *    domain(in): domain structure
 *    buffer(out): output buffer
 *    maxlen(in): maximum length of buffer
 */
int
tp_domain_name (const TP_DOMAIN * domain, char *buffer, int maxlen)
{
  /* 
   * need to get more sophisticated here, do full name decomposition and
   * check maxlen
   */
  strncpy (buffer, domain->type->name, maxlen);
  buffer[maxlen - 1] = '\0';
  return (0);
}

/*
 * tp_value_domain_name - generates printed representation of the domain for a
 * given DB_VALUE.
 *    return: non-zero if buffer overflow, -1 if error
 *    value(in): value to examine
 *    buffer(out): output buffer
 *    maxlen(in): maximum length of buffer
 */
int
tp_value_domain_name (const DB_VALUE * value, char *buffer, int maxlen)
{
  /* need to get more sophisticated here */

  strncpy (buffer, pr_type_name (DB_VALUE_TYPE (value)), maxlen);
  buffer[maxlen - 1] = '\0';
  return (0);
}

/*
 * tp_domain_find_compatible - two domains are compatible for the purposes of
 * assignment of values.
 *    return: non-zero if domains are compatible
 *    src(in): domain we're wanting to assign
 *    dest(in): domain we're trying to go into
 * Note:
 *    Domains are compatible if they are equal.
 *    Further, domain 1 is compatible with domain 2 if domain 2 is more
 *    general.
 *
 *    This will not properly detect of the domains are compatible due
 *    to a proper subclass superclass relationship between object domains.
 *    It will only check to see if the class matches exactly.
 *
 *    This is the function used to test to see if a particular set domain
 *    is "within" another set domain during assignment of set values to
 *    attributes.  src in this case will be the domain of the set we were
 *    given and dest will be the domain of the attribute.
 *    All of the sub-domains in src must also be found in dest.
 *
 *    This is somewhat different than tp_domain_match because the comparison
 *    of set domains is more of an "is it a subset" operation rather than
 *    an "is it equal to" operation.
 */
static const TP_DOMAIN *
tp_domain_find_compatible (const TP_DOMAIN * src, const TP_DOMAIN * dest)
{
  const TP_DOMAIN *d, *found;

  found = NULL;

  /* 
   * If we have a hierarchical domain, perform a lenient "superset" comparison
   * rather than an exact match.
   */
  if (TP_IS_SET_TYPE (TP_DOMAIN_TYPE (src)) || TP_DOMAIN_TYPE (src) == DB_TYPE_VARIABLE)
    {
      for (d = dest; d != NULL && found == NULL; d = d->next)
	{
	  if (TP_DOMAIN_TYPE (src) == TP_DOMAIN_TYPE (d) && tp_domain_compatible (src->setdomain, dest->setdomain))
	    {
	      found = d;
	    }
	}
    }
  else
    {

      for (d = dest; d != NULL && found == NULL; d = d->next)
	{
	  if (tp_domain_match ((TP_DOMAIN *) src, (TP_DOMAIN *) d, TP_EXACT_MATCH))
	    {
	      /* exact match flag is on */
	      found = d;
	    }
	}

    }

  return found;
}

/*
 * tp_domain_compatible - check compatibility of src domain w.r.t dest
 *    return: 1 if compatible, 0 otherwise
 *    src(in): src domain
 *    dest(in): dest domain
 */
int
tp_domain_compatible (const TP_DOMAIN * src, const TP_DOMAIN * dest)
{
  const TP_DOMAIN *d;
  int equal = 0;

  if (src != NULL && dest != NULL)
    {
      equal = 1;
      if (src != dest)
	{
	  /* 
	   * for every domain in src, make sure we have a compatible one in
	   * dest
	   */
	  for (d = src; equal && d != NULL; d = d->next)
	    {
	      if (tp_domain_find_compatible (d, dest) == NULL)
		{
		  equal = 0;
		}
	    }
	}
    }

  return equal;
}


/*
 * tp_domain_select - select a domain from a list of possible domains that is
 * the exact match (or closest, depending on the value of exact_match) to the
 * supplied value.
 *    return: domain
 *    domain_list(in): list of possible domains
 *    value(in): value of interest
 *    allow_coercion(in): non-zero if coercion will be allowed
 *    exact_match(in): controls tolerance permitted during match
 * Note:
 *    This operation is used for basic domain compatibility checking
 *    as well as value coercion.
 *    If the allow_coercion flag is on, the tp_Domain_conversion_matrix
 *    will be consulted to find an appropriate domain in the case
 *    where there is no exact match.
 *    If an appropriate domain could not be found, NULL is returned.
 *
 *    This is known not to work correctly for nested set domains.  In order
 *    for the best domain to be selected, we must recursively check the
 *    complete set domains here.
 *
 *    The exact_match flag determines if we allow "tolerance" matches when
 *    checking domains for attributes.  See commentary in tp_domain_match
 *    for more information.
 */
TP_DOMAIN *
tp_domain_select (const TP_DOMAIN * domain_list, const DB_VALUE * value, int allow_coercion, TP_MATCH exact_match)
{
  TP_DOMAIN *best, *d;
  TP_DOMAIN **others;
  DB_TYPE vtype;
  int i;

  best = NULL;

  /* 
   * NULL values are allowed in any domain, a NULL domain means that any value
   * is allowed, return the first thing on the list.
   */
  if (value == NULL || domain_list == NULL || (vtype = DB_VALUE_TYPE (value)) == DB_TYPE_NULL)
    {
      return (TP_DOMAIN *) domain_list;
    }


  if (vtype == DB_TYPE_OID)
    {
      if (db_on_server)
	{
	  /* 
	   * On the server, just make sure that we have any object domain in
	   * the list.
	   */
	  for (d = (TP_DOMAIN *) domain_list; d != NULL && best == NULL; d = d->next)
	    {
	      if (TP_DOMAIN_TYPE (d) == DB_TYPE_OBJECT)
		{
		  best = d;
		}
	    }
	}
#if !defined (SERVER_MODE)
      else
	{
	  /* 
	   * On the client, swizzle to an object and fall in to the next
	   * clause
	   */
	  OID *oid;
	  DB_OBJECT *mop;
	  DB_VALUE temp;

	  oid = (OID *) db_get_oid (value);
	  if (oid)
	    {
	      if (OID_ISNULL (oid))
		{
		  /* this is the same as the NULL case above */
		  return (TP_DOMAIN *) domain_list;
		}
	      else
		{
		  mop = ws_mop (oid, NULL);
		  db_make_object (&temp, mop);
		  /* 
		   * we don't have to worry about clearing this since its an
		   * object
		   */
		  value = (const DB_VALUE *) &temp;
		  vtype = DB_TYPE_OBJECT;
		}
	    }
	}
#endif /* !SERVER_MODE */
    }

  /* 
   * Handling of object domains is more complex than just comparing the
   * types and parameters.  We have to see if the instance's class is
   * somewhere in the subclass hierarchy of the domain class.
   * This can't be done on the server yet though presumably we could
   * implement something like this using OID chasing.
   */

  if (vtype == DB_TYPE_OBJECT)
    {
      if (db_on_server)
	{
	  /* 
	   * we really shouldn't get here but if we do, handle it like the
	   * OID case above, just return the first object domain that we find.
	   */
	  for (d = (TP_DOMAIN *) domain_list; d != NULL && best == NULL; d = d->next)
	    {
	      if (TP_DOMAIN_TYPE (d) == DB_TYPE_OBJECT)
		{
		  best = d;
		}
	    }
	  return best;
	}
#if !defined (SERVER_MODE)
      else
	{
	  /* 
	   * On the client, check to see if the instance is within the subclass
	   * hierarchy of the object domains.  If there are more than one
	   * acceptable domains, we just pick the first one.
	   */
	  DB_OBJECT *obj = db_get_object (value);

	  for (d = (TP_DOMAIN *) domain_list; d != NULL && best == NULL; d = d->next)
	    {
	      if (TP_DOMAIN_TYPE (d) == DB_TYPE_OBJECT && sm_check_object_domain (d, obj))
		{
		  best = d;
		}
	    }
	}
#endif /* !SERVER_MODE */
    }

#if !defined (SERVER_MODE)
  else if (vtype == DB_TYPE_POINTER)
    {
      /* 
       * This is necessary in order to correctly choose an object domain from
       * the domain list when doing an insert nested inside a heterogeneous
       * set, e.g.:
       * create class foo (a int);
       * create class bar (b set_of(string, integer, foo));
       * insert into bar (b) values ({insert into foo values (1)});
       */
      DB_OTMPL *val_tmpl;

      val_tmpl = (DB_OTMPL *) DB_GET_POINTER (value);
      if (val_tmpl)
	{
	  for (d = (TP_DOMAIN *) domain_list; d != NULL && best == NULL; d = d->next)
	    {
	      if (TP_DOMAIN_TYPE (d) == DB_TYPE_OBJECT && sm_check_class_domain (d, val_tmpl->classobj))
		{
		  best = d;
		}
	    }
	}
    }
#endif /* !SERVER_MODE */

  else if (TP_IS_SET_TYPE (vtype))
    {
      /* 
       * Now that we cache set domains, there might be a faster way to do
       * this !
       */
      DB_SET *set;

      set = db_get_set (value);
      for (d = (TP_DOMAIN *) domain_list; d != NULL && best == NULL; d = d->next)
	{
	  if (TP_DOMAIN_TYPE (d) == vtype)
	    {
	      if (set_check_domain (set, d) == DOMAIN_COMPATIBLE)
		{
		  best = d;
		}
	    }
	}
    }
  else if (vtype == DB_TYPE_ENUMERATION)
    {
      int val_idx, dom_size, val_size;
      char *dom_str = NULL, *val_str = NULL;

      if ((DB_GET_ENUM_SHORT (value) == 0 && DB_GET_ENUM_STRING (value) != NULL))
	{
	  /* An enumeration should be NULL or should at least have an index */
	  assert (false);
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	  return NULL;
	}

      val_idx = DB_GET_ENUM_SHORT (value);

      val_str = DB_GET_ENUM_STRING (value);
      val_size = DB_GET_ENUM_STRING_SIZE (value);

      for (d = (TP_DOMAIN *) domain_list; d != NULL && best == NULL; d = d->next)
	{
	  if (TP_DOMAIN_TYPE (d) != DB_TYPE_ENUMERATION)
	    {
	      continue;
	    }

	  if (val_idx == 0)
	    {
	      /* this is an invalid enum value so any domain matches */
	      best = d;
	      break;
	    }
	  if (DOM_GET_ENUM_ELEMS_COUNT (d) == 0 && best == NULL)
	    {
	      /* this is the default enum domain and we haven't found any matching domain yet. This is our best
	       * candidate so far */
	      best = d;
	      continue;
	    }
	  if (DOM_GET_ENUM_ELEMS_COUNT (d) < val_idx)
	    {
	      continue;
	    }
	  if (val_str == NULL)
	    {
	      /* The enumeration string value is not specified. This means that the domain we have so far is the best
	       * match because there is no way of deciding between other domains based only on the short value */
	      best = d;
	      break;
	    }

	  if (DB_GET_ENUM_COLLATION (value) != d->collation_id)
	    {
	      continue;
	    }

	  dom_str = DB_GET_ENUM_ELEM_STRING (&DOM_GET_ENUM_ELEM (d, val_idx));
	  dom_size = DB_GET_ENUM_ELEM_STRING_SIZE (&DOM_GET_ENUM_ELEM (d, val_idx));

	  /* We have already checked that val_str is not null */
	  if (dom_str == NULL)
	    {
	      assert (false);
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	      return NULL;
	    }
	  if (QSTR_COMPARE (d->collation_id, (const unsigned char *) dom_str, dom_size,
			    (const unsigned char *) val_str, val_size) == 0)
	    {
	      if (best == NULL)
		{
		  best = d;
		}
	      else if (DOM_GET_ENUM_ELEMS_COUNT (best) < DOM_GET_ENUM_ELEMS_COUNT (d))
		{
		  /* The best match is the domain that has the largest element count. We're not interested in the value
		   * of the exact_match argument since we cannot find an exact enumeration match based on a DB_VALUE */
		  best = d;
		}
	    }
	}
    }
  else
    {
      /* 
       * synthesize a domain for the value and look for a match.
       * Could we be doing this for the set values too ?
       * Hack, since this will be used only for comparison purposes,
       * don't go through the overhead of caching the domain every time,
       * especially for numeric types.  This will be a lot simpler if we
       * store the domain
       * pointer directly in the DB_VALUE.
       */
      TP_DOMAIN temp_domain, *val_domain;

      val_domain = tp_domain_resolve_value ((DB_VALUE *) value, &temp_domain);

      for (d = (TP_DOMAIN *) domain_list; d != NULL && best == NULL; d = d->next)
	{
	  /* hack, try allowing "tolerance" matches of the domain ! */
	  if (tp_domain_match (d, val_domain, exact_match))
	    {
	      best = d;
	    }
	}
    }

  if (best == NULL && allow_coercion)
    {
      others = tp_Domain_conversion_matrix[vtype];
      if (others != NULL)
	{
	  for (i = 0; others[i] != NULL && best == NULL; i++)
	    {
	      for (d = (TP_DOMAIN *) domain_list; d != NULL && best == NULL; d = d->next)
		{
		  if (d->type == others[i]->type)
		    {
		      best = d;
		    }
		}
	    }
	}
    }

  return best;
}

#if !defined (SERVER_MODE)
/*
 * tp_domain_select_type - similar to tp_domain_select except that it does not
 * require the existance of an actual DB_VALUE containing a proposed value.
 *    return: best domain from the list, NULL if none
 *    domain_list(in): domain lis t
 *    type(in): basic data type
 *    class(in): class if type == DB_TYPE_OBJECT
 *    allow_coercion(in): flag to enable coercions
 * Note:
 *    this cannot be used for checking set domains.
 */
TP_DOMAIN *
tp_domain_select_type (const TP_DOMAIN * domain_list, DB_TYPE type, DB_OBJECT * class_mop, int allow_coercion)
{
  const TP_DOMAIN *best, *d;
  TP_DOMAIN **others;
  int i;

  /* 
   * NULL values are allowed in any domain, a NULL domain means that any value
   * is allowed, return the first thing on the list
   */
  if (type == DB_TYPE_NULL || domain_list == NULL)
    {
      best = domain_list;
    }
  else
    {
      best = NULL;
      /* 
       * loop through the domain elements looking for one the fits,
       * rather than make type comparisons for each element in the loop,
       * do them out here and duplicate the loop
       */

      if (type == DB_TYPE_OBJECT)
	{
	  for (d = domain_list; d != NULL && best == NULL; d = d->next)
	    {
	      if (TP_DOMAIN_TYPE (d) == DB_TYPE_OBJECT && sm_check_class_domain ((TP_DOMAIN *) d, class_mop))
		{
		  best = d;
		}
	    }
	}
      else if (TP_IS_SET_TYPE (type))
	{
	  for (d = domain_list; d != NULL && best == NULL; d = d->next)
	    {
	      if (TP_DOMAIN_TYPE (d) == type)
		{
		  /* can't check the actual set domain here, assume its ok */
		  best = d;
		}
	    }
	}
      else
	{
	  for (d = domain_list; d != NULL && best == NULL; d = d->next)
	    {
	      if (TP_DOMAIN_TYPE (d) == type || TP_DOMAIN_TYPE (d) == DB_TYPE_VARIABLE)
		{
		  best = d;
		}
	    }
	}

      if (best == NULL && allow_coercion)
	{
	  others = tp_Domain_conversion_matrix[type];
	  if (others != NULL)
	    {
	      /* 
	       * loop through the allowable conversions until we find
	       * one that appears in the supplied domain list, the
	       * array is ordered in terms of priority,
	       * THIS WILL NOT WORK CORRECTLY FOR NESTED SETS
	       */
	      for (i = 0; others[i] != NULL && best == NULL; i++)
		{
		  for (d = domain_list; d != NULL && best == NULL; d = d->next)
		    {
		      if (d->type == others[i]->type)
			{
			  best = d;
			}
		    }
		}
	    }
	}
    }

  return ((TP_DOMAIN *) best);
}
#endif /* !SERVER_MODE */


/*
 * tp_domain_check - does basic validation of a value against a domain.
 *    return: domain status
 *    domain(in): destination domain
 *    value(in): value to look at
 *    exact_match(in): controls the tolerance permitted for the match
 * Note:
 *    It does NOT do coercion.  If the intention is to perform coercion,
 *    them tp_domain_select should be used.
 *    Exact match is used to request a deferred coercion of values that
 *    are within "tolerance" of the destination domain.  This is currently
 *    only specified for assignment of attribute values and will be
 *    recognized only by those types whose "setmem" and "writeval" functions
 *    are able to perform delayed coercion.  Examples are the CHAR types
 *    which will do truncation or blank padding as the values are being
 *    assigned.  See commentary in tp_domain_match for more information.
 */
TP_DOMAIN_STATUS
tp_domain_check (const TP_DOMAIN * domain, const DB_VALUE * value, TP_MATCH exact_match)
{
  TP_DOMAIN_STATUS status;
  TP_DOMAIN *d;

  if (domain == NULL)
    {
      status = DOMAIN_COMPATIBLE;
    }
  else
    {
      d = tp_domain_select (domain, value, 0, exact_match);
      if (d == NULL)
	{
	  status = DOMAIN_INCOMPATIBLE;
	}
      else
	{
	  status = DOMAIN_COMPATIBLE;
	}
    }

  return status;
}

/*
 * COERCION
 */


/*
 * tp_can_steal_string - check if the string currently held in "val" can be
 * safely reused
 *    WITHOUT copying.
 *    return: error code
 *    val(in): source (and destination) value
 *    desired_domain(in): desired domain for coerced value
 * Note:
 *    Basically, this holds if
 *       1. the dest precision is "floating", or
 *       2. the dest type is varying and the length of the string is less
 *          than or equal to the dest precision, or
 *       3. the dest type is fixed and the length of the string is exactly
 *          equal to the dest precision.
 *    Since the desired domain is often a varying char, this wins often.
 */
int
tp_can_steal_string (const DB_VALUE * val, const DB_DOMAIN * desired_domain)
{
  DB_TYPE original_type, desired_type;
  int original_length, original_size, desired_precision;

  original_type = DB_VALUE_DOMAIN_TYPE (val);
  if (!TP_IS_CHAR_BIT_TYPE (original_type))
    {
      return 0;
    }

  original_length = DB_GET_STRING_LENGTH (val);
  original_size = DB_GET_STRING_SIZE (val);
  desired_type = TP_DOMAIN_TYPE (desired_domain);
  desired_precision = desired_domain->precision;

  /* this condition covers both the cases when string conversion is needed, and when byte reinterpretation will be
   * performed (destination charset = BINARY) */
  if (original_size > desired_precision)
    {
      return 0;
    }

  if (TP_IS_CHAR_TYPE (original_type) && TP_IS_CHAR_TYPE (TP_DOMAIN_TYPE (desired_domain)))
    {
      if (desired_domain->collation_flag != TP_DOMAIN_COLL_LEAVE
	  && DB_GET_STRING_COLLATION (val) != TP_DOMAIN_COLLATION (desired_domain)
	  && !LANG_IS_COERCIBLE_COLL (DB_GET_STRING_COLLATION (val)))
	{
	  return 0;
	}

      if (desired_domain->collation_flag != TP_DOMAIN_COLL_LEAVE
	  && !INTL_CAN_STEAL_CS (DB_GET_STRING_CODESET (val), TP_DOMAIN_CODESET (desired_domain)))
	{
	  return 0;
	}
    }

  if (desired_domain->collation_flag == TP_DOMAIN_COLL_ENFORCE)
    {
      return 1;
    }

  if (desired_precision == TP_FLOATING_PRECISION_VALUE)
    {
      desired_precision = original_length;
    }

  switch (desired_type)
    {
    case DB_TYPE_CHAR:
      return (desired_precision == original_length
	      && (original_type == DB_TYPE_CHAR || original_type == DB_TYPE_VARCHAR)
	      && DB_GET_COMPRESSED_STRING (val) == NULL);
    case DB_TYPE_VARCHAR:
      return (desired_precision >= original_length
	      && (original_type == DB_TYPE_CHAR || original_type == DB_TYPE_VARCHAR));
    case DB_TYPE_NCHAR:
      return (desired_precision == original_length
	      && (original_type == DB_TYPE_NCHAR || original_type == DB_TYPE_VARNCHAR)
	      && DB_GET_COMPRESSED_STRING (val) == NULL);
    case DB_TYPE_VARNCHAR:
      return (desired_precision >= original_length
	      && (original_type == DB_TYPE_NCHAR || original_type == DB_TYPE_VARNCHAR));
    case DB_TYPE_BIT:
      return (desired_precision == original_length
	      && (original_type == DB_TYPE_BIT || original_type == DB_TYPE_VARBIT));
    case DB_TYPE_VARBIT:
      return (desired_precision >= original_length
	      && (original_type == DB_TYPE_BIT || original_type == DB_TYPE_VARBIT));
    default:
      return 0;
    }
}

#if defined(ENABLE_UNUSED_FUNCTION)
/*
 * tp_null_terminate - NULL terminate the given DB_VALUE string.
 *    return: NO_ERROR or error code
 *    src(in): string to null terminate
 *    strp(out): pointer for output
 *    str_len(in): length of 'str'
 *    do_alloc(out): set true if allocation occurred
 * Note:
 *    Don't call this unless src is a string db_value.
 */
static int
tp_null_terminate (const DB_VALUE * src, char **strp, int str_len, bool * do_alloc)
{
  char *str;
  int str_size;

  *do_alloc = false;		/* init */

  str = DB_GET_STRING (src);
  if (str == NULL)
    {
      return ER_FAILED;
    }

  str_size = DB_GET_STRING_SIZE (src);

  if (str[str_size] == '\0')
    {
      /* already NULL terminated */
      *strp = str;

      return NO_ERROR;
    }

  if (str_size >= str_len)
    {
      *strp = (char *) malloc (str_size + 1);
      if (*strp == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) (str_size + 1));
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}

      *do_alloc = true;		/* mark as alloced */
    }

  memcpy (*strp, str, str_size);
  (*strp)[str_size] = '\0';

  return NO_ERROR;
}
#endif /* ENABLE_UNUSED_FUNCTION */

/*
 * tp_atotime - coerce a string to a time
 *    return: NO_ERROR or error code
 *    src(in): string DB_VALUE
 *    temp(out): time container
 * Note:
 *    Accepts strings that are not null terminated. Don't call this unless
 *    src is a string db_value.
 */
static int
tp_atotime (const DB_VALUE * src, DB_TIME * temp)
{
  int milisec;
  char *strp = DB_GET_STRING (src);
  int str_len = DB_GET_STRING_SIZE (src);
  int status = NO_ERROR;

  if (db_date_parse_time (strp, str_len, temp, &milisec) != NO_ERROR)
    {
      status = ER_FAILED;
    }

  return status;
}

/*
 * tp_atotimetz - coerce a string to a time with time zone.
 *    return: NO_ERROR or error code
 *    src(in): string DB_VALUE
 *    temp(out): time with TZ info container
 *    to_timeltz(in): flag that tells us if src will be converted to a timeltz
 *		      type

 * Note:
 *    Accepts strings that are not null terminated. Don't call this unless
 *    src is a string db_value.
 */
static int
tp_atotimetz (const DB_VALUE * src, DB_TIMETZ * temp, bool to_timeltz)
{
  char *strp = DB_GET_STRING (src);
  int str_len = DB_GET_STRING_SIZE (src);
  int status = NO_ERROR;
  bool dummy_has_zone;

  if (db_string_to_timetz_ex (strp, str_len, to_timeltz, temp, &dummy_has_zone) != NO_ERROR)
    {
      status = ER_FAILED;
    }

  return status;
}

/*
 * tp_atodate - coerce a string to a date
 *    return: NO_ERROR or error code
 *    src(in): string DB_VALUE
 *    temp(out): date container
 * Note:
 *    Accepts strings that are not null terminated. Don't call this unless
 *    src is a string db_value.
 */
static int
tp_atodate (const DB_VALUE * src, DB_DATE * temp)
{
  char *strp = DB_GET_STRING (src);
  int str_len = DB_GET_STRING_SIZE (src);
  int status = NO_ERROR;

  if (db_date_parse_date (strp, str_len, temp) != NO_ERROR)
    {
      status = ER_FAILED;
    }

  return status;
}

/*
 * tp_atoutime - coerce a string to a utime.
 *    return: NO_ERROR or error code
 *    src(in): string DB_VALUE
 *    temp(out): utime container
 * Note:
 *    Accepts strings that are not null terminated. Don't call this unless
 *    src is a string db_value.
 */
static int
tp_atoutime (const DB_VALUE * src, DB_UTIME * temp)
{
  char *strp = DB_GET_STRING (src);
  int str_len = DB_GET_STRING_SIZE (src);
  int status = NO_ERROR;

  if (db_date_parse_utime (strp, str_len, temp) != NO_ERROR)
    {
      status = ER_FAILED;
    }

  return status;
}

/*
 * tp_atotimestamptz - coerce a string to a timestamp with time zone.
 *    return: NO_ERROR or error code
 *    src(in): string DB_VALUE
 *    temp(out): timestamp with TZ info container
 * Note:
 *    Accepts strings that are not null terminated. Don't call this unless
 *    src is a string db_value.
 */
static int
tp_atotimestamptz (const DB_VALUE * src, DB_TIMESTAMPTZ * temp)
{
  char *strp = DB_GET_STRING (src);
  int str_len = DB_GET_STRING_SIZE (src);
  int status = NO_ERROR;
  bool dummy_has_zone;

  if (db_string_to_timestamptz_ex (strp, str_len, temp, &dummy_has_zone, true) != NO_ERROR)
    {
      status = ER_FAILED;
    }

  return status;
}

/*
 * tp_atoudatetime - coerce a string to a datetime.
 *    return: NO_ERROR or error code
 *    src(in): string DB_VALUE
 *    temp(out): datetime container
 * Note:
 *    Accepts strings that are not null terminated. Don't call this unless
 *    src is a string db_value.
 */
static int
tp_atoudatetime (const DB_VALUE * src, DB_DATETIME * temp)
{
  char *strp = DB_GET_STRING (src);
  int str_len = DB_GET_STRING_SIZE (src);
  int status = NO_ERROR;

  if (db_date_parse_datetime (strp, str_len, temp) != NO_ERROR)
    {
      status = ER_FAILED;
    }

  return status;
}

/*
 * tp_atoudatetimetz - coerce a string to a datetime with time zone.
 *    return: NO_ERROR or error code
 *    src(in): string DB_VALUE
 *    temp(out): datetime with time zone container
 * Note:
 *    Accepts strings that are not null terminated. Don't call this unless
 *    src is a string db_value.
 */
static int
tp_atodatetimetz (const DB_VALUE * src, DB_DATETIMETZ * temp)
{
  char *strp = DB_GET_STRING (src);
  int str_len = DB_GET_STRING_SIZE (src);
  int status = NO_ERROR;
  bool dummy_has_zone;

  if (db_string_to_datetimetz_ex (strp, str_len, temp, &dummy_has_zone) != NO_ERROR)
    {
      status = ER_FAILED;
    }

  return status;
}

/*
 * tp_atonumeric - Coerce a string to a numeric.
 *    return: NO_ERROR or error code
 *    src(in): string DB_VALUE
 *    temp(out): numeirc container
 * Note:
 *    Accepts strings that are not null terminated. Don't call this unless
 *    src is a string db_value.
 */
static int
tp_atonumeric (const DB_VALUE * src, DB_VALUE * temp)
{
  char *strp;
  int status = NO_ERROR;
  int str_len;

  strp = DB_PULL_STRING (src);
  if (strp == NULL)
    {
      return ER_FAILED;
    }

  str_len = DB_GET_STRING_SIZE (src);

  if (numeric_coerce_string_to_num (strp, str_len, DB_GET_STRING_CODESET (src), temp) != NO_ERROR)
    {
      status = ER_FAILED;
    }

  return status;
}

/*
 * tp_atof - Coerce a string to a double.
 *    return: NO_ERROR or error code.
 *    src(in): string DB_VALUE
 *    num_value(out): float container
 *    data_stat(out): if overflow is detected, this is set to
 *		      DATA_STATUS_TRUNCATED. If there exists some characters
 *		      that are not numeric codes or spaces then it is set to
 *		      DATA_STATUS_NOT_CONSUMED.
 * Note:
 *    Accepts strings that are not null terminated. Don't call this unless
 *    src is a string db_value.
 */
static int
tp_atof (const DB_VALUE * src, double *num_value, DB_DATA_STATUS * data_stat)
{
  char str[NUM_BUF_SIZE];
  char *strp = str;
  bool do_alloc = false;
  double d;
  char *p, *end;
  int status = NO_ERROR;
  unsigned int size;
  INTL_CODESET codeset;

  *data_stat = DATA_STATUS_OK;

  p = DB_GET_STRING (src);
  size = DB_GET_STRING_SIZE (src);
  codeset = DB_GET_STRING_CODESET (src);
  end = p + size - 1;

  if (*end)
    {
      while (p <= end && char_isspace (*p))
	{
	  p++;
	}

      while (p < end && char_isspace (*end))
	{
	  end--;
	}

      size = CAST_BUFLEN (end - p) + 1;

      if (size > sizeof (str) - 1)
	{
	  strp = (char *) malloc (size + 1);
	  if (strp == NULL)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) (size + 1));
	      return ER_OUT_OF_VIRTUAL_MEMORY;
	    }
	  do_alloc = true;
	}
      if (size)
	{
	  memcpy (strp, p, size);
	}
      strp[size] = '\0';
    }
  else
    {
      strp = p;
    }

  /* don't use atof() which cannot detect the error. */
  errno = 0;
  d = string_to_double (strp, &p);

  if (errno == ERANGE)
    {
      if (d != 0)
	{
	  /* overflow */
	  *data_stat = DATA_STATUS_TRUNCATED;
	}
      /* d == 0 is underflow, we don't have an error for this */
    }

  /* ignore trailing spaces */
  p = (char *) intl_skip_spaces (p, NULL, codeset);
  if (*p)			/* all input does not consumed */
    {
      *data_stat = DATA_STATUS_NOT_CONSUMED;
    }
  *num_value = d;

  if (do_alloc)
    {
      free_and_init (strp);
    }

  return status;
}

/*
 * tp_atobi - Coerce a string to a bigint.
 *    return: NO_ERROR or error code
 *    src(in): string DB_VALUE
 *    num_value(out): bigint container
 *    data_stat(out): if overflow is detected, this is set to
 *		      DATA_STATUS_TRUNCATED
 * Note:
 *    Accepts strings that are not null terminated. Don't call this unless
 *    src is a string db_value.
 *    If string contains decimal part, performs rounding.
 *
 */
static int
tp_atobi (const DB_VALUE * src, DB_BIGINT * num_value, DB_DATA_STATUS * data_stat)
{
  char *strp = DB_GET_STRING (src);
  char *stre = NULL;
  char *p = NULL, *old_p = NULL;
  int status = NO_ERROR;
  bool is_negative = false;
  INTL_CODESET codeset = DB_GET_STRING_CODESET (src);
  bool is_hex = false, is_scientific = false;
  bool has_leading_zero = false;

  if (strp == NULL)
    {
      return ER_FAILED;
    }

  stre = strp + DB_GET_STRING_SIZE (src);

  /* skip leading spaces */
  while (strp != stre && char_isspace (*strp))
    {
      strp++;
    }

  /* read sign if any */
  if (strp != stre && (*strp == '-' || *strp == '+'))
    {
      is_negative = (*strp == '-');
      strp++;
    }
  else
    {
      is_negative = false;
    }

  /* 0x or 0X */
  if (strp != stre && *strp == '0' && (strp + 1) != stre && (*(strp + 1) == 'x' || *(strp + 1) == 'X'))
    {
      is_hex = true;
      strp += 2;
    }

  /* skip leading zeros */
  while (strp != stre && *strp == '0')
    {
      strp++;

      if (!has_leading_zero)
	{
	  has_leading_zero = true;
	}
    }

  if (!is_hex)
    {
      /* check whether is scientific format */
      p = strp;

      /* check first part */
      while (p != stre && char_isdigit (*p))
	{
	  ++p;
	}

      if (p != stre && *p == '.')
	{
	  ++p;

	  while (p != stre && char_isdigit (*p))
	    {
	      ++p;
	    }
	}

      /* no first part */
      if (!has_leading_zero && p == strp)
	{
	  return ER_FAILED;
	}

      /* skip trailing white spaces of first part */
      p = (char *) intl_skip_spaces (p, stre, codeset);

      /* check exponent part */
      if (p != stre)
	{
	  if (*p == 'e' || *p == 'E')
	    {
	      ++p;

	      /* check second part */
	      if (p != stre && (*p == '+' || *p == '-'))
		{
		  ++p;
		}

	      old_p = p;
	      while (p != stre && char_isdigit (*p))
		{
		  ++p;
		}

	      if (p == old_p)
		{
		  return ER_FAILED;
		}

	      /* skip trailing white spaces of second part */
	      p = (char *) intl_skip_spaces (p, stre, codeset);
	      if (p == stre)
		{
		  is_scientific = true;
		}
	    }
	  else
	    {
	      return ER_FAILED;
	    }
	}
    }

  /* convert to bigint */
  if (is_hex)
    {
      status = tp_hex_str_to_bi (strp, stre, codeset, is_negative, num_value, data_stat);
    }
  else if (is_scientific)
    {
      status = tp_scientific_str_to_bi (strp, stre, codeset, is_negative, num_value, data_stat);
    }
  else
    {
      status = tp_digit_number_str_to_bi (strp, stre, codeset, is_negative, num_value, data_stat);
    }

  return status;
}

#if defined(ENABLE_UNUSED_FUNCTION)
/*
 * tp_itoa - int to string representation for given radix
 *    return: string pointer (given or malloc'd)
 *    value(in): int value
 *    string(in/out): dest buffer or NULL
 *    radix(in): int value between 2 and 36
 */
static char *
tp_itoa (int value, char *string, int radix)
{
  char tmp[33];
  char *tp = tmp;
  int i;
  unsigned v;
  int sign;
  char *sp;

  if (radix > 36 || radix <= 1)
    {
      return 0;
    }

  sign = (radix == 10 && value < 0);

  if (sign)
    {
      v = -value;
    }
  else
    {
      v = (unsigned) value;
    }

  while (v || tp == tmp)
    {
      i = v % radix;
      v = v / radix;
      if (i < 10)
	{
	  *tp++ = i + '0';
	}
      else
	{
	  *tp++ = i + 'a' - 10;
	}
    }

  if (string == NULL)
    {
      string = (char *) malloc ((tp - tmp) + sign + 1);
      if (string == NULL)
	{
	  return string;
	}
    }
  sp = string;

  if (sign)
    {
      *sp++ = '-';
    }
  while (tp > tmp)
    {
      *sp++ = *--tp;
    }
  *sp = '\0';
  return string;
}
#endif /* ENABLE_UNUSED_FUNCTION */

/*
 * tp_ltoa - bigint to string representation for given radix
 *    return: string pointer (given or malloc'd)
 *    value(in): bigint value
 *    string(in/out): dest buffer or NULL
 *    radix(in): int value between 2 and 36
 */
static char *
tp_ltoa (DB_BIGINT value, char *string, int radix)
{
  char tmp[33];
  char *tp = tmp;
  int i;
  UINT64 v;
  int sign;
  char *sp;

  if (radix > 36 || radix <= 1)
    {
      return 0;
    }

  sign = (radix == 10 && value < 0);

  if (sign)
    {
      v = -value;
    }
  else
    {
      v = (UINT64) value;
    }

  while (v || tp == tmp)
    {
      i = v % radix;
      v = v / radix;
      if (i < 10)
	{
	  *tp++ = i + '0';
	}
      else
	{
	  *tp++ = i + 'a' - 10;
	}
    }

  if (string == NULL)
    {
      string = (char *) malloc ((tp - tmp) + sign + 1);
      if (string == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) ((tp - tmp) + sign + 1));
	  return NULL;
	}
    }
  sp = string;

  if (sign)
    {
      *sp++ = '-';
    }
  while (tp > tmp)
    {
      *sp++ = *--tp;
    }
  *sp = '\0';

  return string;
}

/*
 * format_floating_point() - formats a digits sequence and an integer exponent
 *			     for a floating-point number (from dtoa) to the
 *			     printable representation
 *
 *  return:
 *  new_string(out):	the sequence of decimal digits for the floating-point
 *			number mantisa, to be reformated into a character
 *			sequence for a printable floating point number.
 *			the buffer is assumed to be large enought for the
 *			printable sequence
 *  rve(out):		end of sequence of digits
 *  ndigits(in):	floating number precision to be used for printing
 *  decpt(in):		decimal point position in the digits sequence
 *			(similar to the exponent)
 *  sign(in):		sign of the floating-point number
 */
static void
format_floating_point (char *new_string, char *rve, int ndigits, int decpt, int sign)
{
  assert (new_string && rve);

  if (decpt != 9999)
    {
      if (ndigits >= decpt && decpt > -4)	/* as in the C 2005 standard for printf conversion specification */
	{
	  /* print as a fractional number */
	  if (decpt > rve - new_string)
	    {
	      /* append with zeros until the decimal point is encountered */
	      while (new_string + decpt > rve)
		{
		  *rve++ = '0';
		}
	      *rve = '\0';
	      /* no decimal point needed */
	    }
	  else if (decpt <= 0)
	    {
	      /* prepend zeroes until the decimal point is encountered */
	      /* if decpt is -3, insert 3 zeroes between the decimal point and first non-zero digit */
	      size_t n_left_pad = +2 - decpt;

	      char *p = new_string + n_left_pad;

	      rve += n_left_pad;
	      do
		{
		  *rve = *(rve - n_left_pad);
		  rve--;
		}
	      while (rve != p);

	      *rve = *(rve - n_left_pad);

	      p--;
	      while (p != new_string + 1)
		{
		  *p-- = '0';
		}
	      *p-- = '.';
	      *p = '0';
	    }
	  else if (decpt != rve - new_string)
	    {
	      /* insert decimal point within the digits sequence at position indicated by decpt */
	      rve++;

	      while (rve != new_string + decpt)
		{
		  *rve = *(rve - 1);
		  rve--;
		}
	      *rve = '.';
	    }
	}
      else
	{
	  /* print as mantisa followed by exponent */
	  if (rve > new_string + 1)
	    {
	      /* insert the decimal point before the second digit, if any */
	      char *p = rve;

	      while (p != new_string)
		{
		  *p = *(p - 1);
		  p--;
		}

	      p[1] = '.';
	      rve++;
	    }
	  *rve++ = 'e';

	  decpt--;		/* convert from 0.432e12 to 4.32e11 */
	  if (decpt < 0)
	    {
	      *rve++ = '-';
	      decpt = -decpt;
	    }
	  else
	    {
	      *rve++ = '+';
	    }

	  if (decpt > 99)
	    {
	      *rve++ = '0' + decpt / 100;
	      *rve++ = '0' + decpt % 100 / 10;
	      *rve++ = '0' + decpt % 10;
	    }
	  else if (decpt > 9)
	    {
	      *rve++ = '0' + decpt / 10;
	      *rve++ = '0' + decpt % 10;
	    }
	  else
	    *rve++ = '0' + decpt;

	  *rve = '\0';
	}
    }

  /* prepend '-' sign if number is negative */
  if (sign)
    {
      char ch = *new_string;

      rve = new_string + 1;

      while (*rve)
	{
	  /* swap(ch, *rve); */
	  ch ^= *rve;
	  *rve = ch ^ *rve;
	  ch ^= *rve++;
	}

      /* swap(ch, *rve); */
      ch ^= *rve;
      *rve = ch ^ *rve;
      ch ^= *rve++;

      rve[0] = '\0';
      *new_string = '-';
    }
}

/*
 * tp_ftoa() - convert a float DB_VALUE to a string DB_VALUE.
 *	       Only the decimal representation is preserved by
 *	       by the conversion, in order to avoid printing
 *	       inexact digits. This means that if the number is
 *	       read back from string into a float, the binary
 *	       representation for the float might be different
 *	       than the original, but the printed number shall
 *             always be the same.
 *
 * return:
 * src(in):	    float DB_VALUE to be converted to string
 * result(in/out):  string DB_VALUE of the desired [VAR][N]CHAR
 *		    domain type and null value, that receives
 *		    the string resulting from conversion.
 */
void
tp_ftoa (DB_VALUE const *src, DB_VALUE * result)
{
  /* dtoa() appears to ignore the requested number of digits... */
  const int ndigits = TP_FLOAT_MANTISA_DECIMAL_PRECISION;
  char *str_float, *rve;
  int decpt, sign;

  assert (DB_VALUE_TYPE (src) == DB_TYPE_FLOAT);
  assert (DB_VALUE_TYPE (result) == DB_TYPE_NULL);

  rve = str_float = db_private_alloc (NULL, TP_FLOAT_AS_CHAR_LENGTH + 1);
  if (str_float == NULL)
    {
      DB_MAKE_NULL (result);
      return;
    }

  /* _dtoa just returns the digits sequence and the exponent as for a number in the form 0.4321344e+14 */
  _dtoa (DB_GET_FLOAT (src), 0, ndigits, &decpt, &sign, &rve, str_float, 1);

  /* rounding should also be performed here */
  str_float[ndigits] = '\0';	/* _dtoa() disregards ndigits */

  format_floating_point (str_float, str_float + strlen (str_float), ndigits, decpt, sign);

  switch (DB_VALUE_DOMAIN_TYPE (result))
    {
    case DB_TYPE_CHAR:
      DB_MAKE_CHAR (result, DB_VALUE_PRECISION (result), str_float, strlen (str_float), DB_GET_STRING_CODESET (result),
		    DB_GET_STRING_COLLATION (result));
      result->need_clear = true;
      break;

    case DB_TYPE_NCHAR:
      DB_MAKE_NCHAR (result, DB_VALUE_PRECISION (result), str_float, strlen (str_float), DB_GET_STRING_CODESET (result),
		     DB_GET_STRING_COLLATION (result));
      result->need_clear = true;
      break;

    case DB_TYPE_VARCHAR:
      DB_MAKE_VARCHAR (result, DB_VALUE_PRECISION (result), str_float, strlen (str_float),
		       DB_GET_STRING_CODESET (result), DB_GET_STRING_COLLATION (result));
      result->need_clear = true;
      break;

    case DB_TYPE_VARNCHAR:
      DB_MAKE_VARNCHAR (result, DB_VALUE_PRECISION (result), str_float, strlen (str_float),
			DB_GET_STRING_CODESET (result), DB_GET_STRING_COLLATION (result));
      result->need_clear = true;
      break;

    default:
      db_private_free_and_init (NULL, str_float);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TP_CANT_COERCE, 2, pr_type_name (DB_VALUE_DOMAIN_TYPE (src)),
	      pr_type_name (DB_VALUE_DOMAIN_TYPE (result)));
      DB_MAKE_NULL (result);
      break;
    }
}

/*
 *  tp_dtoa():	    converts a double DB_VALUE to a string DB_VALUE.
 *		    Only as many digits as can be computed exactly are
 *		    written in the resulting string.
 *
 * return:
 * src(in):	    double DB_VALUE to be converted to string
 * result(in/out):  string DB_VALUE of the desired [VAR][N]CHAR domain
 *		    type and null value, to receive the converted float
 */
void
tp_dtoa (DB_VALUE const *src, DB_VALUE * result)
{
  /* dtoa() appears to ignore the requested number of digits... */
  const int ndigits = TP_DOUBLE_MANTISA_DECIMAL_PRECISION;
  char *str_double, *rve;
  int decpt, sign;

  assert (DB_VALUE_TYPE (src) == DB_TYPE_DOUBLE);
  assert (DB_VALUE_TYPE (result) == DB_TYPE_NULL);

  rve = str_double = db_private_alloc (NULL, TP_DOUBLE_AS_CHAR_LENGTH + 1);
  if (str_double == NULL)
    {
      DB_MAKE_NULL (result);
      return;
    }

  _dtoa (DB_GET_DOUBLE (src), 0, ndigits, &decpt, &sign, &rve, str_double, 0);
  /* rounding should also be performed here */
  str_double[ndigits] = '\0';	/* _dtoa() disregards ndigits */

  format_floating_point (str_double, str_double + strlen (str_double), ndigits, decpt, sign);

  switch (DB_VALUE_DOMAIN_TYPE (result))
    {
    case DB_TYPE_CHAR:
      DB_MAKE_CHAR (result, DB_VALUE_PRECISION (result), str_double, strlen (str_double),
		    DB_GET_STRING_CODESET (result), DB_GET_STRING_COLLATION (result));
      result->need_clear = true;
      break;

    case DB_TYPE_NCHAR:
      DB_MAKE_NCHAR (result, DB_VALUE_PRECISION (result), str_double, strlen (str_double),
		     DB_GET_STRING_CODESET (result), DB_GET_STRING_COLLATION (result));
      result->need_clear = true;
      break;

    case DB_TYPE_VARCHAR:
      DB_MAKE_VARCHAR (result, DB_VALUE_PRECISION (result), str_double, strlen (str_double),
		       DB_GET_STRING_CODESET (result), DB_GET_STRING_COLLATION (result));
      result->need_clear = true;
      break;

    case DB_TYPE_VARNCHAR:
      DB_MAKE_VARNCHAR (result, DB_VALUE_PRECISION (result), str_double, strlen (str_double),
			DB_GET_STRING_CODESET (result), DB_GET_STRING_COLLATION (result));
      result->need_clear = true;
      break;

    default:
      db_private_free_and_init (NULL, str_double);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TP_CANT_COERCE, 2, pr_type_name (DB_VALUE_DOMAIN_TYPE (src)),
	      pr_type_name (DB_VALUE_DOMAIN_TYPE (result)));
      DB_MAKE_NULL (result);
      break;
    }
}

/*
 * tp_enumeration_to_varchar  - Get the value of an enumeration as a varchar.
 *    return: error code or NO_ERROR
 *    src(in): enumeration
 *    result(in/out): varchar
 * Note:
 *    The string value of the varchar value is not a copy of the enumeration
 *    value, it just points to it
 */
int
tp_enumeration_to_varchar (const DB_VALUE * src, DB_VALUE * result)
{
  int error = NO_ERROR;

  if (src == NULL || result == NULL)
    {
      assert (false);
      return ER_FAILED;
    }

  if (DB_GET_ENUM_STRING (src) == NULL)
    {
      db_make_varchar (result, DB_DEFAULT_PRECISION, "", 0, DB_GET_ENUM_CODESET (src), DB_GET_ENUM_COLLATION (src));
    }
  else
    {
      db_make_varchar (result, DB_DEFAULT_PRECISION, DB_GET_ENUM_STRING (src), DB_GET_ENUM_STRING_SIZE (src),
		       DB_GET_ENUM_CODESET (src), DB_GET_ENUM_COLLATION (src));
    }

  return error;
}

#define BITS_IN_BYTE            8
#define HEX_IN_BYTE             2
#define BITS_IN_HEX             4

#define BYTE_COUNT(bit_cnt)     (((bit_cnt)+BITS_IN_BYTE-1)/BITS_IN_BYTE)
#define BYTE_COUNT_HEX(bit_cnt) (((bit_cnt)+BITS_IN_HEX-1)/BITS_IN_HEX)

/*
 * bfmt_print - Change the given string to a representation of the given
 * bit string value in the given format.
 *    return: NO_ERROR or -1 if max_size is too small
 *    bfmt(in): 0: for binary representation or 1: for hex representation
 *    the_db_bit(in): DB_VALUE
 *    string(out): output buffer
 *    max_size(in): size of output buffer
 */
static int
bfmt_print (int bfmt, const DB_VALUE * the_db_bit, char *string, int max_size)
{
  int length = 0;
  int string_index = 0;
  int byte_index;
  int bit_index;
  char *bstring;
  int error = NO_ERROR;
  static const char digits[16] = {
    '0', '1', '2', '3', '4', '5', '6', '7',
    '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'
  };

  /* Get the buffer and the length from the_db_bit */
  bstring = DB_GET_BIT (the_db_bit, &length);

  switch (bfmt)
    {
    case 0:			/* BIT_STRING_BINARY */
      if (length + 1 > max_size)
	{
	  error = -1;
	}
      else
	{
	  for (byte_index = 0; byte_index < BYTE_COUNT (length); byte_index++)
	    {
	      for (bit_index = 7; bit_index >= 0 && string_index < length; bit_index--)
		{
		  *string = digits[((bstring[byte_index] >> bit_index) & 0x1)];
		  string++;
		  string_index++;
		}
	    }
	  *string = '\0';
	}
      break;

    case 1:			/* BIT_STRING_HEX */
      if (BYTE_COUNT_HEX (length) + 1 > max_size)
	{
	  error = -1;
	}
      else
	{
	  for (byte_index = 0; byte_index < BYTE_COUNT (length); byte_index++)
	    {
	      *string = digits[((bstring[byte_index] >> BITS_IN_HEX) & 0x0f)];
	      string++;
	      string_index++;
	      if (string_index < BYTE_COUNT_HEX (length))
		{
		  *string = digits[((bstring[byte_index] & 0x0f))];
		  string++;
		  string_index++;
		}
	    }
	  *string = '\0';
	}
      break;

    default:
      break;
    }

  return error;
}


#define ROUND(x)		  ((x) > 0 ? ((x) + .5) : ((x) - .5))
#define SECONDS_IN_A_DAY	  (long)(86400)	/* 24L * 60L * 60L */
#define TP_IS_CHAR_STRING(db_val_type)					\
    (db_val_type == DB_TYPE_CHAR || db_val_type == DB_TYPE_VARCHAR ||	\
     db_val_type == DB_TYPE_NCHAR || db_val_type == DB_TYPE_VARNCHAR)

#define TP_IS_LOB(db_val_type)                                          \
    (db_val_type == DB_TYPE_BLOB || db_val_type == DB_TYPE_CLOB)

#define TP_IS_DATETIME_TYPE(db_val_type) TP_IS_DATE_OR_TIME_TYPE (db_val_type)

#define TP_IMPLICIT_COERCION_NOT_ALLOWED(src_type, dest_type)		\
   ((TP_IS_CHAR_STRING(src_type) && !(TP_IS_CHAR_STRING(dest_type) ||	\
				      TP_IS_DATETIME_TYPE(dest_type) || \
				      TP_IS_NUMERIC_TYPE(dest_type) ||	\
				      dest_type == DB_TYPE_ENUMERATION)) ||\
    (!TP_IS_CHAR_STRING(src_type) && src_type != DB_TYPE_ENUMERATION &&	\
     TP_IS_CHAR_STRING(dest_type)) ||					\
    (TP_IS_LOB(src_type) || TP_IS_LOB(dest_type)))

/*
 * tp_value_string_to_double - Coerce a string to a double.
 *    return: NO_ERROR, ER_OUT_OF_VIRTUAL_MEMORY or ER_FAILED.
 *    src(in): string DB_VALUE
 *    result(in/out): float container
 * Note:
 *    Accepts strings that are not null terminated.
 */
int
tp_value_string_to_double (const DB_VALUE * value, DB_VALUE * result)
{
  DB_DATA_STATUS data_stat;
  double dbl;
  int ret;
  DB_TYPE type = DB_VALUE_TYPE (value);

  if (!TP_IS_CHAR_STRING (type))
    {
      DB_MAKE_DOUBLE (result, 0);
      return ER_FAILED;
    }

  ret = tp_atof (value, &dbl, &data_stat);
  if (ret != NO_ERROR)
    {
      DB_MAKE_DOUBLE (result, 0);
    }
  else
    {
      DB_MAKE_DOUBLE (result, dbl);
    }

  return ret;
}

static void
make_desired_string_db_value (DB_TYPE desired_type, const TP_DOMAIN * desired_domain, const char *new_string,
			      DB_VALUE * target, TP_DOMAIN_STATUS * status, DB_DATA_STATUS * data_stat)
{
  DB_VALUE temp;

  assert (desired_domain->collation_flag == TP_DOMAIN_COLL_NORMAL
	  || desired_domain->collation_flag == TP_DOMAIN_COLL_LEAVE);

  *status = DOMAIN_COMPATIBLE;
  switch (desired_type)
    {
    case DB_TYPE_CHAR:
      db_make_char (&temp, desired_domain->precision, new_string, strlen (new_string),
		    TP_DOMAIN_CODESET (desired_domain), TP_DOMAIN_COLLATION (desired_domain));
      break;
    case DB_TYPE_NCHAR:
      db_make_nchar (&temp, desired_domain->precision, new_string, strlen (new_string),
		     TP_DOMAIN_CODESET (desired_domain), TP_DOMAIN_COLLATION (desired_domain));
      break;
    case DB_TYPE_VARCHAR:
      db_make_varchar (&temp, desired_domain->precision, new_string, strlen (new_string),
		       TP_DOMAIN_CODESET (desired_domain), TP_DOMAIN_COLLATION (desired_domain));
      break;
    case DB_TYPE_VARNCHAR:
      db_make_varnchar (&temp, desired_domain->precision, new_string, strlen (new_string),
			TP_DOMAIN_CODESET (desired_domain), TP_DOMAIN_COLLATION (desired_domain));
      break;
    default:			/* Can't get here.  This just quiets the compiler */
      break;
    }

  temp.need_clear = true;
  if (db_char_string_coerce (&temp, target, data_stat) != NO_ERROR)
    {
      *status = DOMAIN_INCOMPATIBLE;
    }
  else
    {
      *status = DOMAIN_COMPATIBLE;
    }
  pr_clear_value (&temp);
}

/*
 * tp_value_coerce - Coerce a value into one of another domain.
 *    return: TP_DOMAIN_STATUS
 *    src(in): source value
 *    dest(out): destination value
 *    desired_domain(in): destination domain
 */
TP_DOMAIN_STATUS
tp_value_coerce (const DB_VALUE * src, DB_VALUE * dest, const TP_DOMAIN * desired_domain)
{
  return tp_value_cast (src, dest, desired_domain, true);
}

/*
 * tp_value_coerce_strict () - convert a value to desired domain without loss
 *			       of precision
 * return : error code or NO_ERROR
 * src (in)   : source value
 * dest (out) : destination value
 * desired_domain (in) : destination domain
 */
int
tp_value_coerce_strict (const DB_VALUE * src, DB_VALUE * dest, const TP_DOMAIN * desired_domain)
{
  DB_TYPE desired_type, original_type;
  int err = NO_ERROR;
  DB_VALUE temp, *target;

  /* A NULL src is allowed but destination remains NULL, not desired_domain */
  if (src == NULL || (original_type = DB_VALUE_TYPE (src)) == DB_TYPE_NULL)
    {
      db_make_null (dest);
      return err;
    }

  if (desired_domain == NULL)
    {
      db_make_null (dest);
      return ER_FAILED;
    }

  desired_type = TP_DOMAIN_TYPE (desired_domain);

  if (!TP_IS_NUMERIC_TYPE (desired_type) && !TP_IS_DATETIME_TYPE (desired_type))
    {
      db_make_null (dest);
      return ER_FAILED;
    }

  if (desired_type == original_type)
    {
      if (src != dest)
	{
	  pr_clone_value ((DB_VALUE *) src, dest);
	  return NO_ERROR;
	}
    }

  /* 
   * If src == dest, coerce into a temporary variable and
   * handle the conversion before returning.
   */
  if (src == dest)
    {
      target = &temp;
    }
  else
    {
      target = dest;
    }

  /* 
   * Initialize the destination domain, important for the
   * nm_ coercion functions which take domain information inside the
   * destination db value.
   */
  db_value_domain_init (target, desired_type, desired_domain->precision, desired_domain->scale);

  switch (desired_type)
    {
    case DB_TYPE_SHORT:
      switch (original_type)
	{
	case DB_TYPE_MONETARY:
	  {
	    double i = 0;
	    const double val = DB_GET_MONETARY (src)->amount;
	    if (OR_CHECK_SHORT_OVERFLOW (val))
	      {
		err = ER_FAILED;
		break;
	      }
	    if (modf (val, &i) != 0)
	      {
		err = ER_FAILED;
		break;
	      }
	    db_make_short (target, (short) i);
	    break;
	  }
	case DB_TYPE_INTEGER:
	  if (OR_CHECK_SHORT_OVERFLOW (DB_GET_INT (src)))
	    {
	      err = ER_FAILED;
	      break;
	    }
	  db_make_short (target, (short) DB_GET_INT (src));
	  break;
	case DB_TYPE_BIGINT:
	  if (OR_CHECK_SHORT_OVERFLOW (DB_GET_BIGINT (src)))
	    {
	      err = ER_FAILED;
	      break;
	    }
	  db_make_short (target, (short) DB_GET_BIGINT (src));
	  break;
	case DB_TYPE_FLOAT:
	  {
	    float i = 0;
	    const float val = DB_GET_FLOAT (src);
	    if (OR_CHECK_SHORT_OVERFLOW (val))
	      {
		err = ER_FAILED;
		break;
	      }
	    if (modff (val, &i) != 0)
	      {
		err = ER_FAILED;
		break;
	      }
	    db_make_short (target, (short) i);
	    break;
	  }
	case DB_TYPE_DOUBLE:
	  {
	    double i = 0;
	    const double val = DB_GET_DOUBLE (src);
	    if (OR_CHECK_SHORT_OVERFLOW (val))
	      {
		err = ER_FAILED;
		break;
	      }
	    if (modf (val, &i) != 0)
	      {
		err = ER_FAILED;
		break;
	      }
	    db_make_short (target, (short) i);
	    break;
	  }
	case DB_TYPE_NUMERIC:
	  err = numeric_db_value_coerce_from_num_strict ((DB_VALUE *) src, target);
	  break;
	case DB_TYPE_CHAR:
	case DB_TYPE_VARCHAR:
	case DB_TYPE_NCHAR:
	case DB_TYPE_VARNCHAR:
	  {
	    double num_value = 0.0, i = 0.0;
	    DB_DATA_STATUS data_stat = DATA_STATUS_OK;
	    if (tp_atof (src, &num_value, &data_stat) != NO_ERROR)
	      {
		err = ER_FAILED;
		break;
	      }
	    if (data_stat != DATA_STATUS_OK || OR_CHECK_SHORT_OVERFLOW (num_value))
	      {
		err = ER_FAILED;
		break;
	      }
	    if (modf (num_value, &i) != 0)
	      {
		err = ER_FAILED;
		break;
	      }
	    db_make_short (target, (short) i);
	    break;
	  }
	default:
	  err = ER_FAILED;
	  break;
	}
      break;

    case DB_TYPE_INTEGER:
      switch (original_type)
	{
	case DB_TYPE_SHORT:
	  db_make_int (target, (int) DB_GET_SHORT (src));
	  err = NO_ERROR;
	  break;
	case DB_TYPE_MONETARY:
	  {
	    double i = 0;
	    const double val = DB_GET_MONETARY (src)->amount;
	    if (OR_CHECK_INT_OVERFLOW (val))
	      {
		err = ER_FAILED;
		break;
	      }
	    if (modf (val, &i) != 0)
	      {
		err = ER_FAILED;
		break;
	      }
	    db_make_int (target, (int) i);
	    break;
	  }
	case DB_TYPE_BIGINT:
	  if (OR_CHECK_INT_OVERFLOW (DB_GET_BIGINT (src)))
	    {
	      err = ER_FAILED;
	      break;
	    }
	  db_make_int (target, (int) DB_GET_BIGINT (src));
	  break;
	case DB_TYPE_FLOAT:
	  {
	    float i = 0;
	    const float val = DB_GET_FLOAT (src);
	    if (OR_CHECK_INT_OVERFLOW (val))
	      {
		err = ER_FAILED;
		break;
	      }
	    if (modff (val, &i) != 0)
	      {
		err = ER_FAILED;
		break;
	      }
	    db_make_int (target, (int) i);
	    break;
	  }
	case DB_TYPE_DOUBLE:
	  {
	    double i = 0;
	    const double val = DB_GET_DOUBLE (src);
	    if (OR_CHECK_INT_OVERFLOW (val))
	      {
		err = ER_FAILED;
		break;
	      }
	    if (modf (val, &i) != 0)
	      {
		err = ER_FAILED;
		break;
	      }
	    db_make_int (target, (int) i);
	    break;
	  }
	case DB_TYPE_NUMERIC:
	  err = numeric_db_value_coerce_from_num_strict ((DB_VALUE *) src, target);
	  break;
	case DB_TYPE_CHAR:
	case DB_TYPE_VARCHAR:
	case DB_TYPE_NCHAR:
	case DB_TYPE_VARNCHAR:
	  {
	    double num_value = 0.0, i = 0.0;
	    DB_DATA_STATUS data_stat = DATA_STATUS_OK;
	    if (tp_atof (src, &num_value, &data_stat) != NO_ERROR)
	      {
		err = ER_FAILED;
		break;
	      }
	    if (data_stat != DATA_STATUS_OK || OR_CHECK_INT_OVERFLOW (num_value))
	      {
		err = ER_FAILED;
		break;
	      }
	    if (modf (num_value, &i) != 0)
	      {
		err = ER_FAILED;
		break;
	      }
	    db_make_int (target, (int) i);
	    break;
	  }
	default:
	  err = ER_FAILED;
	  break;
	}
      break;

    case DB_TYPE_BIGINT:
      switch (original_type)
	{
	case DB_TYPE_SHORT:
	  db_make_bigint (target, DB_GET_SHORT (src));
	  break;
	case DB_TYPE_INTEGER:
	  db_make_bigint (target, DB_GET_INT (src));
	  break;
	case DB_TYPE_MONETARY:
	  {
	    double i = 0;
	    const double val = DB_GET_MONETARY (src)->amount;
	    if (OR_CHECK_BIGINT_OVERFLOW (val))
	      {
		err = ER_FAILED;
		break;
	      }
	    if (modf (val, &i) != 0)
	      {
		err = ER_FAILED;
		break;
	      }
	    db_make_bigint (target, (DB_BIGINT) i);
	    break;
	  }
	  break;
	case DB_TYPE_FLOAT:
	  {
	    float i = 0;
	    const float val = DB_GET_FLOAT (src);
	    if (OR_CHECK_BIGINT_OVERFLOW (val))
	      {
		err = ER_FAILED;
		break;
	      }
	    if (modff (val, &i) != 0)
	      {
		err = ER_FAILED;
		break;
	      }
	    db_make_bigint (target, (DB_BIGINT) i);
	    break;
	  }
	case DB_TYPE_DOUBLE:
	  {
	    double i = 0;
	    const double val = DB_GET_DOUBLE (src);
	    if (OR_CHECK_BIGINT_OVERFLOW (val))
	      {
		err = ER_FAILED;
		break;
	      }
	    if (modf (val, &i) != 0)
	      {
		err = ER_FAILED;
		break;
	      }
	    db_make_bigint (target, (DB_BIGINT) i);
	    break;
	  }
	case DB_TYPE_NUMERIC:
	  err = numeric_db_value_coerce_from_num_strict ((DB_VALUE *) src, target);
	  break;
	case DB_TYPE_CHAR:
	case DB_TYPE_VARCHAR:
	case DB_TYPE_NCHAR:
	case DB_TYPE_VARNCHAR:
	  {
	    double num_value = 0.0, i = 0.0;
	    DB_DATA_STATUS data_stat = DATA_STATUS_OK;
	    if (tp_atof (src, &num_value, &data_stat) != NO_ERROR)
	      {
		err = ER_FAILED;
		break;
	      }
	    if (data_stat != DATA_STATUS_OK || OR_CHECK_BIGINT_OVERFLOW (num_value))
	      {
		err = ER_FAILED;
		break;
	      }
	    if (modf (num_value, &i) != 0)
	      {
		err = ER_FAILED;
		break;
	      }
	    db_make_bigint (target, (DB_BIGINT) i);
	    break;
	  }
	default:
	  err = ER_FAILED;
	  break;
	}
      break;

    case DB_TYPE_FLOAT:
      switch (original_type)
	{
	case DB_TYPE_SHORT:
	  db_make_float (target, (float) DB_GET_SHORT (src));
	  break;
	case DB_TYPE_INTEGER:
	  db_make_float (target, (float) DB_GET_INT (src));
	  break;
	case DB_TYPE_BIGINT:
	  db_make_float (target, (float) DB_GET_BIGINT (src));
	  break;
	case DB_TYPE_NUMERIC:
	  err = numeric_db_value_coerce_from_num_strict ((DB_VALUE *) src, target);
	  break;
	case DB_TYPE_CHAR:
	case DB_TYPE_VARCHAR:
	case DB_TYPE_NCHAR:
	case DB_TYPE_VARNCHAR:
	  {
	    double num_value = 0.0;
	    DB_DATA_STATUS data_stat = DATA_STATUS_OK;

	    if (tp_atof (src, &num_value, &data_stat) != NO_ERROR)
	      {
		err = ER_FAILED;
		break;
	      }

	    if (data_stat != DATA_STATUS_OK || OR_CHECK_FLOAT_OVERFLOW (num_value))
	      {
		err = ER_FAILED;
		break;
	      }
	    db_make_float (target, (float) num_value);
	    break;
	  }
	default:
	  err = ER_FAILED;
	  break;
	}
      break;

    case DB_TYPE_DOUBLE:
      switch (original_type)
	{
	case DB_TYPE_SHORT:
	  db_make_double (target, (double) DB_GET_SHORT (src));
	  break;
	case DB_TYPE_INTEGER:
	  db_make_double (target, (double) DB_GET_INT (src));
	  break;
	case DB_TYPE_BIGINT:
	  db_make_double (target, (double) DB_GET_BIGINT (src));
	  break;
	case DB_TYPE_FLOAT:
	  db_make_double (target, (double) DB_GET_FLOAT (src));
	  break;
	case DB_TYPE_MONETARY:
	  db_make_double (target, DB_GET_MONETARY (src)->amount);
	  break;
	case DB_TYPE_NUMERIC:
	  err = numeric_db_value_coerce_from_num_strict ((DB_VALUE *) src, target);
	  break;
	case DB_TYPE_CHAR:
	case DB_TYPE_VARCHAR:
	case DB_TYPE_NCHAR:
	case DB_TYPE_VARNCHAR:
	  {
	    DB_DATA_STATUS data_stat = DATA_STATUS_OK;
	    double num_value = 0.0;
	    if (tp_atof (src, &num_value, &data_stat) != NO_ERROR)
	      {
		err = ER_FAILED;
		break;
	      }
	    if (data_stat != DATA_STATUS_OK)
	      {
		err = ER_FAILED;
		break;
	      }
	    db_make_double (target, num_value);
	    break;
	  }
	default:
	  err = ER_FAILED;
	  break;
	}
      break;

    case DB_TYPE_NUMERIC:
      switch (original_type)
	{
	case DB_TYPE_CHAR:
	case DB_TYPE_VARCHAR:
	case DB_TYPE_NCHAR:
	case DB_TYPE_VARNCHAR:
	  {
	    if (tp_atonumeric (src, target) != NO_ERROR)
	      {
		err = ER_FAILED;
	      }
	    break;
	  }
	case DB_TYPE_SHORT:
	case DB_TYPE_INTEGER:
	case DB_TYPE_BIGINT:
	case DB_TYPE_NUMERIC:
	  {
	    DB_DATA_STATUS data_stat = DATA_STATUS_OK;
	    err = numeric_db_value_coerce_to_num ((DB_VALUE *) src, target, &data_stat);
	    if (data_stat != DATA_STATUS_OK)
	      {
		err = ER_FAILED;
		break;
	      }
	    break;
	  }
	default:
	  err = ER_FAILED;
	  break;
	}
      break;

    case DB_TYPE_MONETARY:
      switch (original_type)
	{
	case DB_TYPE_SHORT:
	  db_make_monetary (target, DB_CURRENCY_DEFAULT, (double) DB_GET_SHORT (src));
	  break;
	case DB_TYPE_INTEGER:
	  db_make_monetary (target, DB_CURRENCY_DEFAULT, (double) DB_GET_INT (src));
	  break;
	case DB_TYPE_BIGINT:
	  db_make_monetary (target, DB_CURRENCY_DEFAULT, (double) DB_GET_BIGINT (src));
	  break;
	case DB_TYPE_FLOAT:
	  db_make_monetary (target, DB_CURRENCY_DEFAULT, (double) DB_GET_FLOAT (src));
	  break;
	case DB_TYPE_DOUBLE:
	  db_make_monetary (target, DB_CURRENCY_DEFAULT, DB_GET_DOUBLE (src));
	  break;
	case DB_TYPE_NUMERIC:
	  err = numeric_db_value_coerce_from_num_strict ((DB_VALUE *) src, target);
	  break;
	case DB_TYPE_CHAR:
	case DB_TYPE_VARCHAR:
	case DB_TYPE_NCHAR:
	case DB_TYPE_VARNCHAR:
	  {
	    double num_value = 0.0;
	    DB_DATA_STATUS data_stat = DATA_STATUS_OK;
	    if (tp_atof (src, &num_value, &data_stat) != NO_ERROR)
	      {
		err = ER_FAILED;
		break;
	      }
	    if (data_stat == DATA_STATUS_TRUNCATED || data_stat == DATA_STATUS_NOT_CONSUMED)
	      {
		err = ER_FAILED;
		break;
	      }
	    db_make_monetary (target, DB_CURRENCY_DEFAULT, num_value);
	    break;
	  }
	default:
	  err = ER_FAILED;
	  break;
	}
      break;

    case DB_TYPE_TIME:
      switch (original_type)
	{
	case DB_TYPE_CHAR:
	case DB_TYPE_VARCHAR:
	case DB_TYPE_NCHAR:
	case DB_TYPE_VARNCHAR:
	  {
	    DB_TIME time = 0;
	    if (tp_atotime (src, &time) != NO_ERROR)
	      {
		err = ER_FAILED;
		break;
	      }
	    db_value_put_encoded_time (target, &time);
	    break;
	  }
	case DB_TYPE_TIMETZ:
	  {
	    DB_TIMETZ *time_tz = DB_GET_TIMETZ (src);
	    db_value_put_encoded_time (target, &time_tz->time);
	    break;
	  }
	case DB_TYPE_TIMELTZ:
	  {
	    DB_TIME *time = DB_GET_TIME (src);
	    db_value_put_encoded_time (target, time);
	    break;
	  }
	default:
	  err = ER_FAILED;
	  break;
	}
      break;

    case DB_TYPE_TIMETZ:
      switch (original_type)
	{
	case DB_TYPE_CHAR:
	case DB_TYPE_VARCHAR:
	case DB_TYPE_NCHAR:
	case DB_TYPE_VARNCHAR:
	  {
	    DB_TIMETZ time_tz = { 0, 0 };

	    if (tp_atotimetz (src, &time_tz, false) != NO_ERROR)
	      {
		err = ER_FAILED;
		break;
	      }
	    db_make_timetz (target, &time_tz);
	    break;
	  }
	case DB_TYPE_TIME:
	case DB_TYPE_TIMELTZ:
	  {
	    DB_TIMETZ time_tz = { 0, 0 };
	    DB_TIME *time = DB_GET_TIME (src);
	    bool time_is_utc = (original_type == DB_TYPE_TIMELTZ) ? true : false;

	    time_tz.time = *time;
	    if (tz_create_session_tzid_for_time (time, time_is_utc, &time_tz.tz_id) != NO_ERROR)
	      {
		err = ER_FAILED;
		break;
	      }

	    tz_tzid_convert_region_to_offset (&time_tz.tz_id);
	    db_make_timetz (target, &time_tz);
	    break;
	  }
	default:
	  err = ER_FAILED;
	  break;
	}
      break;

    case DB_TYPE_TIMELTZ:
      switch (original_type)
	{
	case DB_TYPE_CHAR:
	case DB_TYPE_VARCHAR:
	case DB_TYPE_NCHAR:
	case DB_TYPE_VARNCHAR:
	  {
	    DB_TIMETZ time_tz = { 0, 0 };

	    if (tp_atotimetz (src, &time_tz, true) != NO_ERROR)
	      {
		err = ER_FAILED;
		break;
	      }
	    /* store only time part in UTC */
	    db_make_timeltz (target, &time_tz.time);
	    break;
	  }
	case DB_TYPE_TIMETZ:
	  {
	    DB_TIMETZ *time_tz = DB_GET_TIMETZ (src);

	    assert (tz_check_geographic_tz (&time_tz->tz_id) == NO_ERROR);

	    /* copy time value (UTC) */
	    db_make_timeltz (target, &time_tz->time);
	    break;
	  }
	case DB_TYPE_TIME:
	  {
	    DB_TIME *time = DB_GET_TIME (src);
	    DB_TIMETZ time_tz;

	    if (tz_check_session_has_geographic_tz () != NO_ERROR)
	      {
		err = ER_FAILED;
		break;
	      }

	    err = tz_create_timetz_from_ses (time, &time_tz);
	    if (err != NO_ERROR)
	      {
		err = ER_FAILED;
		break;
	      }
	    db_make_timeltz (target, time);
	    break;
	  }
	default:
	  err = ER_FAILED;
	  break;
	}
      break;

    case DB_TYPE_DATE:
      switch (original_type)
	{
	case DB_TYPE_CHAR:
	case DB_TYPE_VARCHAR:
	case DB_TYPE_NCHAR:
	case DB_TYPE_VARNCHAR:
	  {
	    DB_DATE date = 0;
	    int year = 0, month = 0, day = 0;

	    if (tp_atodate (src, &date) != NO_ERROR)
	      {
		err = ER_FAILED;
		break;
	      }
	    db_value_put_encoded_date (target, &date);
	    break;
	  }
	case DB_TYPE_DATETIME:
	  {
	    DB_DATE v_date = 0;
	    DB_DATETIME *src_dt = NULL;

	    src_dt = DB_GET_DATETIME (src);
	    if (src_dt->time != 0)
	      {
		/* only "downcast" if time is 0 */
		err = ER_FAILED;
		break;
	      }
	    db_value_put_encoded_date (target, (DB_DATE *) (&src_dt->date));
	    break;
	  }
	case DB_TYPE_DATETIMELTZ:
	case DB_TYPE_DATETIMETZ:
	  {
	    DB_DATETIME *utc_dt_p;
	    DB_DATETIMETZ *dt_tz_p;
	    DB_DATETIME local_dt;
	    TZ_ID tz_id;

	    /* DATETIMELTZ and DATETIMETZ store in UTC, convert to session */
	    if (original_type == DB_TYPE_DATETIMELTZ)
	      {
		utc_dt_p = DB_GET_DATETIME (src);
		if (tz_create_session_tzid_for_datetime (utc_dt_p, true, &tz_id) != NO_ERROR)
		  {
		    err = ER_FAILED;
		    break;
		  }
	      }
	    else
	      {
		dt_tz_p = DB_GET_DATETIMETZ (src);
		utc_dt_p = &dt_tz_p->datetime;
		tz_id = dt_tz_p->tz_id;
	      }

	    if (tz_utc_datetimetz_to_local (utc_dt_p, &tz_id, &local_dt) != NO_ERROR)
	      {
		err = ER_FAILED;
		break;
	      }

	    if (local_dt.time != 0)
	      {
		/* only "downcast" if time is 0 */
		err = ER_FAILED;
		break;
	      }

	    db_value_put_encoded_date (target, (DB_DATE *) (&local_dt.date));
	    break;
	  }
	case DB_TYPE_TIMESTAMP:
	case DB_TYPE_TIMESTAMPLTZ:
	  {
	    DB_DATE date = 0;
	    DB_TIME time = 0;
	    DB_TIMESTAMP *ts = NULL;

	    ts = DB_GET_TIMESTAMP (src);
	    (void) db_timestamp_decode_ses (ts, &date, &time);
	    if (time != 0)
	      {
		/* only "downcast" if time is 0 */
		err = ER_FAILED;
		break;
	      }
	    db_value_put_encoded_date (target, &date);
	    break;
	  }
	case DB_TYPE_TIMESTAMPTZ:
	  {
	    DB_DATE date = 0;
	    DB_TIME time = 0;
	    DB_TIMESTAMPTZ *ts_tz = NULL;

	    ts_tz = DB_GET_TIMESTAMPTZ (src);
	    err = db_timestamp_decode_w_tz_id (&ts_tz->timestamp, &ts_tz->tz_id, &date, &time);
	    if (err != NO_ERROR || time != 0)
	      {
		/* only "downcast" if time is 0 */
		err = ER_FAILED;
		break;
	      }
	    db_value_put_encoded_date (target, &date);
	    break;
	  }
	default:
	  err = ER_FAILED;
	  break;
	}
      break;

    case DB_TYPE_DATETIME:
      switch (original_type)
	{
	case DB_TYPE_DATE:
	  {
	    DB_DATETIME datetime = { 0, 0 };
	    datetime.date = *DB_GET_DATE (src);
	    datetime.time = 0;
	    db_make_datetime (target, &datetime);
	    break;
	  }
	case DB_TYPE_DATETIMETZ:
	  {
	    DB_DATETIMETZ *dt_tz = DB_GET_DATETIMETZ (src);
	    db_make_datetime (target, &dt_tz->datetime);
	    break;
	  }
	case DB_TYPE_DATETIMELTZ:
	  {
	    DB_DATETIME *dt = DB_GET_DATETIME (src);
	    db_make_datetime (target, dt);
	    break;
	  }
	case DB_TYPE_VARCHAR:
	case DB_TYPE_CHAR:
	case DB_TYPE_NCHAR:
	case DB_TYPE_VARNCHAR:
	  {
	    DB_DATETIME datetime = { 0, 0 };
	    if (tp_atoudatetime (src, &datetime) != NO_ERROR)
	      {
		err = ER_FAILED;
		break;
	      }
	    db_make_datetime (target, &datetime);
	    break;
	  }
	case DB_TYPE_TIMESTAMP:
	case DB_TYPE_TIMESTAMPLTZ:
	  {
	    DB_DATETIME datetime = { 0, 0 };
	    DB_TIMESTAMP *utime = DB_GET_TIMESTAMP (src);
	    DB_DATE date;
	    DB_TIME time;

	    if (db_timestamp_decode_ses (utime, &date, &time) != NO_ERROR)
	      {
		err = ER_FAILED;
		break;
	      }
	    datetime.time = time * 1000;
	    datetime.date = date;
	    db_make_datetime (target, &datetime);
	    break;
	  }
	case DB_TYPE_TIMESTAMPTZ:
	  {
	    DB_DATETIME datetime = { 0, 0 };
	    DB_DATE date;
	    DB_TIME time;
	    DB_TIMESTAMPTZ *ts_tz = DB_GET_TIMESTAMPTZ (src);

	    if (db_timestamp_decode_w_tz_id (&ts_tz->timestamp, &ts_tz->tz_id, &date, &time) != NO_ERROR)
	      {
		err = ER_FAILED;
		break;
	      }

	    datetime.time = time * 1000;
	    datetime.date = date;
	    db_make_datetime (target, &datetime);
	    break;
	  }
	default:
	  err = ER_FAILED;
	  break;
	}
      break;

    case DB_TYPE_DATETIMETZ:
      switch (original_type)
	{
	case DB_TYPE_DATE:
	case DB_TYPE_DATETIME:
	  {
	    DB_DATETIMETZ dt_tz = DB_DATETIMETZ_INITIALIZER;

	    if (original_type == DB_TYPE_DATE)
	      {
		dt_tz.datetime.date = *DB_GET_DATE (src);
		dt_tz.datetime.time = 0;
	      }
	    else
	      {
		dt_tz.datetime = *DB_GET_DATETIME (src);
	      }

	    err = tz_create_datetimetz_from_ses (&(dt_tz.datetime), &dt_tz);
	    if (err == NO_ERROR)
	      {
		db_make_datetimetz (target, &dt_tz);
	      }
	    break;
	  }

	case DB_TYPE_DATETIMELTZ:
	  {
	    DB_DATETIMETZ dt_tz = DB_DATETIMETZ_INITIALIZER;
	    DB_DATETIME *dt = DB_GET_DATETIME (src);

	    dt_tz.datetime = *dt;
	    err = tz_create_session_tzid_for_datetime (dt, false, &dt_tz.tz_id);
	    if (err == NO_ERROR)
	      {
		db_make_datetimetz (target, &dt_tz);
	      }
	    break;
	  }
	case DB_TYPE_VARCHAR:
	case DB_TYPE_CHAR:
	case DB_TYPE_NCHAR:
	case DB_TYPE_VARNCHAR:
	  {
	    DB_DATETIMETZ dt_tz = DB_DATETIMETZ_INITIALIZER;

	    if (tp_atodatetimetz (src, &dt_tz) != NO_ERROR)
	      {
		err = ER_FAILED;
		break;
	      }

	    db_make_datetimetz (target, &dt_tz);
	  }
	  break;
	case DB_TYPE_TIMESTAMP:
	case DB_TYPE_TIMESTAMPLTZ:
	  {
	    DB_DATETIMETZ dt_tz = DB_DATETIMETZ_INITIALIZER;
	    DB_TIMESTAMP *utime = DB_GET_TIMESTAMP (src);
	    DB_DATE date;
	    DB_TIME time;

	    /* convert DT to TS in UTC reference */
	    db_timestamp_decode_utc (utime, &date, &time);
	    dt_tz.datetime.date = date;
	    dt_tz.datetime.time = time * 1000;
	    err = tz_create_session_tzid_for_datetime (&dt_tz.datetime, true, &(dt_tz.tz_id));
	    if (err == NO_ERROR)
	      {
		db_make_datetimetz (target, &dt_tz);
	      }
	    break;
	  }
	case DB_TYPE_TIMESTAMPTZ:
	  {
	    DB_DATETIMETZ dt_tz = DB_DATETIMETZ_INITIALIZER;
	    DB_TIMESTAMPTZ *ts_tz = DB_GET_TIMESTAMPTZ (src);
	    DB_DATE date;
	    DB_TIME time;

	    (void) db_timestamp_decode_utc (&ts_tz->timestamp, &date, &time);
	    dt_tz.datetime.time = time * 1000;
	    dt_tz.datetime.date = date;
	    dt_tz.tz_id = ts_tz->tz_id;
	    db_make_datetimetz (target, &dt_tz);
	    break;
	  }
	default:
	  err = ER_FAILED;
	  break;
	}
      break;

    case DB_TYPE_DATETIMELTZ:
      switch (original_type)
	{
	case DB_TYPE_DATE:
	case DB_TYPE_DATETIME:
	  {
	    DB_DATETIME datetime;
	    DB_DATETIMETZ dt_tz;

	    if (original_type == DB_TYPE_DATE)
	      {
		datetime.date = *DB_GET_DATE (src);
		datetime.time = 0;
	      }
	    else
	      {
		datetime = *DB_GET_DATETIME (src);
	      }

	    err = tz_create_datetimetz_from_ses (&datetime, &dt_tz);
	    if (err != NO_ERROR)
	      {
		err = ER_FAILED;
		break;
	      }

	    db_make_datetimeltz (target, &dt_tz.datetime);
	    break;
	  }
	case DB_TYPE_DATETIMETZ:
	  {
	    DB_DATETIMETZ *dt_tz = DB_GET_DATETIMETZ (src);

	    /* copy datetime (UTC) */
	    db_make_datetimeltz (target, &dt_tz->datetime);
	    break;
	  }
	case DB_TYPE_VARCHAR:
	case DB_TYPE_CHAR:
	case DB_TYPE_NCHAR:
	case DB_TYPE_VARNCHAR:
	  {
	    DB_DATETIMETZ dt_tz = DB_DATETIMETZ_INITIALIZER;

	    if (tp_atodatetimetz (src, &dt_tz) != NO_ERROR)
	      {
		err = ER_FAILED;
		break;
	      }
	    db_make_datetimeltz (target, &dt_tz.datetime);
	    break;
	  }
	case DB_TYPE_TIMESTAMP:
	case DB_TYPE_TIMESTAMPLTZ:
	  {
	    DB_DATETIME datetime = { 0, 0 };
	    DB_TIMESTAMP *utime = DB_GET_TIMESTAMP (src);
	    DB_DATE date;
	    DB_TIME time;

	    (void) db_timestamp_decode_utc (utime, &date, &time);
	    datetime.time = time * 1000;
	    datetime.date = date;
	    db_make_datetimeltz (target, &datetime);
	    break;
	  }
	case DB_TYPE_TIMESTAMPTZ:
	  {
	    DB_DATETIME datetime = { 0, 0 };
	    DB_TIMESTAMPTZ *ts_tz = DB_GET_TIMESTAMPTZ (src);
	    DB_DATE date;
	    DB_TIME time;

	    (void) db_timestamp_decode_utc (&ts_tz->timestamp, &date, &time);
	    datetime.time = time * 1000;
	    datetime.date = date;
	    db_make_datetimeltz (target, &datetime);
	    break;
	  }
	default:
	  err = ER_FAILED;
	  break;
	}
      break;

    case DB_TYPE_TIMESTAMP:
      switch (original_type)
	{
	case DB_TYPE_VARCHAR:
	case DB_TYPE_CHAR:
	case DB_TYPE_NCHAR:
	case DB_TYPE_VARNCHAR:
	  {
	    DB_TIMESTAMP ts = 0;

	    if (tp_atoutime (src, &ts) != NO_ERROR)
	      {
		err = ER_FAILED;
		break;
	      }
	    db_make_timestamp (target, ts);
	    break;
	  }
	case DB_TYPE_DATETIME:
	  {
	    DB_DATETIME dt = *DB_GET_DATETIME (src);
	    DB_DATE date = dt.date;
	    DB_TIME time = dt.time / 1000;
	    DB_TIMESTAMP ts = 0;

	    if (db_timestamp_encode_ses (&date, &time, &ts, NULL) != NO_ERROR)
	      {
		err = ER_FAILED;
		break;
	      }
	    db_make_timestamp (target, ts);
	    break;
	  }
	case DB_TYPE_DATETIMELTZ:
	  {
	    DB_DATETIME dt = *DB_GET_DATETIME (src);
	    DB_DATE date = dt.date;
	    DB_TIME time = dt.time / 1000;
	    DB_TIMESTAMP ts = 0;

	    if (db_timestamp_encode_utc (&date, &time, &ts) != NO_ERROR)
	      {
		err = ER_FAILED;
		break;
	      }
	    db_make_timestamp (target, ts);
	    break;
	  }

	case DB_TYPE_DATETIMETZ:
	  {
	    DB_DATETIMETZ *dt_tz = DB_GET_DATETIMETZ (src);
	    DB_DATE date = dt_tz->datetime.date;
	    DB_TIME time = dt_tz->datetime.time / 1000;
	    DB_TIMESTAMP ts = 0;

	    if (db_timestamp_encode_utc (&date, &time, &ts) != NO_ERROR)
	      {
		err = ER_FAILED;
		break;
	      }
	    db_make_timestamp (target, ts);
	    break;
	  }

	case DB_TYPE_DATE:
	  {
	    DB_TIME tm = 0;
	    DB_DATE date = *DB_GET_DATE (src);
	    DB_TIMESTAMP ts = 0;

	    db_time_encode (&tm, 0, 0, 0);
	    if (db_timestamp_encode_ses (&date, &tm, &ts, NULL) != NO_ERROR)
	      {
		err = ER_FAILED;
		break;
	      }
	    db_make_timestamp (target, ts);
	    break;
	  }

	case DB_TYPE_TIMESTAMPTZ:
	  {
	    DB_TIMESTAMPTZ *ts_tz = DB_GET_TIMESTAMPTZ (src);

	    /* copy timestamp value (UTC) */
	    db_make_timestamp (target, ts_tz->timestamp);
	    break;
	  }

	case DB_TYPE_TIMESTAMPLTZ:
	  {
	    DB_TIMESTAMP *ts = DB_GET_TIMESTAMP (src);

	    /* copy timestamp value (UTC) */
	    db_make_timestamp (target, *ts);
	    break;
	  }

	default:
	  err = ER_FAILED;
	  break;
	}
      break;

    case DB_TYPE_TIMESTAMPLTZ:
      switch (original_type)
	{
	case DB_TYPE_VARCHAR:
	case DB_TYPE_CHAR:
	case DB_TYPE_NCHAR:
	case DB_TYPE_VARNCHAR:
	  {
	    DB_TIMESTAMPTZ ts_tz = { 0, 0 };

	    if (tp_atotimestamptz (src, &ts_tz) != NO_ERROR)
	      {
		err = ER_FAILED;
		break;
	      }
	    db_make_timestampltz (target, ts_tz.timestamp);
	    break;
	  }
	case DB_TYPE_DATETIME:
	  {
	    DB_DATETIME dt = *DB_GET_DATETIME (src);
	    DB_DATE date = dt.date;
	    DB_TIME time = dt.time / 1000;
	    DB_TIMESTAMP ts = 0;

	    if (db_timestamp_encode_ses (&date, &time, &ts, NULL) != NO_ERROR)
	      {
		err = ER_FAILED;
		break;
	      }
	    db_make_timestampltz (target, ts);
	    break;
	  }
	case DB_TYPE_DATETIMELTZ:
	  {
	    DB_DATETIME dt = *DB_GET_DATETIME (src);
	    DB_DATE date = dt.date;
	    DB_TIME time = dt.time / 1000;
	    DB_TIMESTAMP ts = 0;

	    if (db_timestamp_encode_utc (&date, &time, &ts) != NO_ERROR)
	      {
		err = ER_FAILED;
		break;
	      }
	    db_make_timestampltz (target, ts);
	    break;
	  }

	case DB_TYPE_DATETIMETZ:
	  {
	    DB_DATETIMETZ *dt_tz = DB_GET_DATETIMETZ (src);
	    DB_DATE date = dt_tz->datetime.date;
	    DB_TIME time = dt_tz->datetime.time / 1000;
	    DB_TIMESTAMP ts = 0;

	    if (db_timestamp_encode_utc (&date, &time, &ts) != NO_ERROR)
	      {
		err = ER_FAILED;
		break;
	      }
	    db_make_timestampltz (target, ts);
	    break;
	  }

	case DB_TYPE_DATE:
	  {
	    DB_TIME tm = 0;
	    DB_DATE date = *DB_GET_DATE (src);
	    DB_TIMESTAMP ts = 0;

	    db_time_encode (&tm, 0, 0, 0);
	    if (db_timestamp_encode_ses (&date, &tm, &ts, NULL) != NO_ERROR)
	      {
		err = ER_FAILED;
		break;
	      }
	    db_make_timestampltz (target, ts);
	    break;
	  }

	case DB_TYPE_TIMESTAMP:
	  {
	    DB_TIMESTAMP *ts = DB_GET_TIMESTAMP (src);

	    /* copy val timestamp value (UTC) */
	    db_make_timestampltz (target, *ts);
	    break;
	  }

	case DB_TYPE_TIMESTAMPTZ:
	  {
	    DB_TIMESTAMPTZ *ts_tz = DB_GET_TIMESTAMPTZ (src);

	    /* copy val timestamp value (UTC) */
	    db_make_timestampltz (target, ts_tz->timestamp);
	    break;
	  }

	default:
	  err = ER_FAILED;
	  break;
	}
      break;

    case DB_TYPE_TIMESTAMPTZ:
      switch (original_type)
	{
	case DB_TYPE_VARCHAR:
	case DB_TYPE_CHAR:
	case DB_TYPE_NCHAR:
	case DB_TYPE_VARNCHAR:
	  {
	    DB_TIMESTAMPTZ ts_tz = { 0, 0 };

	    if (tp_atotimestamptz (src, &ts_tz) != NO_ERROR)
	      {
		err = ER_FAILED;
		break;
	      }
	    db_make_timestamptz (target, &ts_tz);
	    break;
	  }
	case DB_TYPE_DATETIME:
	  {
	    DB_TIMESTAMPTZ ts_tz = { 0, 0 };
	    DB_DATETIME dt = *DB_GET_DATETIME (src);
	    DB_DATE date = dt.date;
	    DB_TIME time = dt.time / 1000;

	    if (db_timestamp_encode_ses (&date, &time, &ts_tz.timestamp, &ts_tz.tz_id) != NO_ERROR)
	      {
		err = ER_FAILED;
		break;
	      }
	    db_make_timestamptz (target, &ts_tz);
	    break;
	  }

	case DB_TYPE_DATETIMELTZ:
	  {
	    DB_TIMESTAMPTZ ts_tz = { 0, 0 };
	    DB_DATETIME dt = *DB_GET_DATETIME (src);
	    DB_DATE date = dt.date;
	    DB_TIME time = dt.time / 1000;

	    if (db_timestamp_encode_utc (&date, &time, &ts_tz.timestamp) != NO_ERROR)
	      {
		err = ER_FAILED;
		break;
	      }
	    ts_tz.tz_id = *tz_get_utc_tz_id ();
	    db_make_timestamptz (target, &ts_tz);
	    break;
	  }

	case DB_TYPE_DATETIMETZ:
	  {
	    DB_TIMESTAMPTZ ts_tz = { 0, 0 };
	    DB_DATETIMETZ *dt_tz = DB_GET_DATETIMETZ (src);
	    DB_DATE date = dt_tz->datetime.date;
	    DB_TIME time = dt_tz->datetime.time / 1000;

	    if (db_timestamp_encode_utc (&date, &time, &ts_tz.timestamp) != NO_ERROR)
	      {
		err = ER_FAILED;
		break;
	      }
	    ts_tz.tz_id = dt_tz->tz_id;
	    db_make_timestamptz (target, &ts_tz);
	    break;
	  }

	case DB_TYPE_DATE:
	  {
	    DB_TIMESTAMPTZ ts_tz = { 0, 0 };
	    DB_TIME tm = 0;
	    DB_DATE date = *DB_GET_DATE (src);

	    db_time_encode (&tm, 0, 0, 0);
	    if (db_timestamp_encode_ses (&date, &tm, &ts_tz.timestamp, &ts_tz.tz_id) != NO_ERROR)
	      {
		err = ER_FAILED;
		break;
	      }
	    db_make_timestamptz (target, &ts_tz);
	    break;
	  }

	case DB_TYPE_TIMESTAMP:
	case DB_TYPE_TIMESTAMPLTZ:
	  {
	    DB_TIMESTAMPTZ ts_tz = { 0, 0 };

	    ts_tz.timestamp = *DB_GET_TIMESTAMP (src);

	    err = tz_create_session_tzid_for_timestamp (&(ts_tz.timestamp), &(ts_tz.tz_id));

	    if (err != NO_ERROR)
	      {
		err = ER_FAILED;
		break;
	      }
	    db_make_timestamptz (target, &ts_tz);
	    break;
	  }

	default:
	  err = ER_FAILED;
	  break;
	}
      break;
    default:
      err = ER_FAILED;
      break;
    }

  if (err == ER_FAILED)
    {
      /* the above code might have set an error message but we don't want to propagate it in this context */
      er_clear ();
    }

  return err;
}

/*
 * tp_value_coerce_internal - Coerce a value into one of another domain.
 *    return: error code
 *    src(in): source value
 *    dest(out): destination value
 *    desired_domain(in): destination domain
 *    coercion_mode(in): flag for the coercion mode
 *    do_domain_select(in): flag for select appropriate domain from
 *                          'desired_domain'
 *    preserve_domain(in): flag to preserve dest's domain
 */
static TP_DOMAIN_STATUS
tp_value_cast_internal (const DB_VALUE * src, DB_VALUE * dest, const TP_DOMAIN * desired_domain,
			const TP_COERCION_MODE coercion_mode, bool do_domain_select, bool preserve_domain)
{
  DB_TYPE desired_type, original_type;
  int err;
  TP_DOMAIN_STATUS status;
  TP_DOMAIN *best, *p_tmp_desired_domain;
  TP_DOMAIN tmp_desired_domain;
  const DB_MONETARY *v_money;
  DB_UTIME v_utime;
  DB_TIMESTAMPTZ v_timestamptz;
  DB_DATETIME v_datetime;
  DB_DATETIMETZ v_datetimetz;
  DB_TIME v_time;
  DB_TIMETZ v_timetz;
  DB_DATE v_date;
  DB_DATA_STATUS data_stat;
  DB_VALUE temp, *target;
  int hour, minute, second, millisecond;
  int year, month, day;
  TZ_ID ses_tz_id;

  err = NO_ERROR;
  status = DOMAIN_COMPATIBLE;

  if (desired_domain == NULL)
    {
      db_make_null (dest);
      return DOMAIN_INCOMPATIBLE;
    }

  /* If more than one destination domain, select the most appropriate */
  if (do_domain_select)
    {
      if (desired_domain->next != NULL)
	{
	  best = tp_domain_select (desired_domain, src, 1, TP_ANY_MATCH);
	  if (best != NULL)
	    {
	      desired_domain = best;
	    }
	}
    }
  desired_type = TP_DOMAIN_TYPE (desired_domain);

  /* A NULL src is allowed but destination remains NULL, not desired_domain */
  if (src == NULL || (original_type = DB_VALUE_TYPE (src)) == DB_TYPE_NULL)
    {
      if (preserve_domain)
	{
	  db_value_domain_init (dest, desired_type, desired_domain->precision, desired_domain->scale);
	  db_value_put_null (dest);
	}
      else
	{
	  db_make_null (dest);
	}
      return status;
    }

  if (desired_type == original_type)
    {
      /* 
       * If there is an easy to check exact match on a non-parameterized
       * domain, just do a simple clone of the value.
       */
      if (!desired_domain->is_parameterized)
	{
	  if (src != dest)
	    {
	      pr_clone_value ((DB_VALUE *) src, dest);
	    }
	  return status;
	}
      else
	{			/* is parameterized domain */
	  switch (desired_type)
	    {
	    case DB_TYPE_NUMERIC:
	      if (desired_domain->precision == DB_VALUE_PRECISION (src)
		  && desired_domain->scale == DB_VALUE_SCALE (src))
		{
		  if (src != dest)
		    {
		      pr_clone_value ((DB_VALUE *) src, dest);
		    }
		  return (status);
		}
	      break;
	    case DB_TYPE_OID:
	      if (src != dest)
		{
		  pr_clone_value ((DB_VALUE *) src, dest);
		}
	      return (status);
	    default:
	      /* pr_is_string_type(desired_type) - NEED MORE CONSIDERATION */
	      break;
	    }
	}
    }

  /* 
   * If the coercion_mode is TP_IMPLICIT_COERCION, check to see if the original
   * type can be implicitly coerced to the desired_type.
   *
   * (Note: This macro only picks up only coercions that are not allowed
   *        implicitly but are allowed explicitly.)
   */
  if (coercion_mode == TP_IMPLICIT_COERCION)
    {
      if (TP_IMPLICIT_COERCION_NOT_ALLOWED (original_type, desired_type))
	{
	  if (preserve_domain)
	    {
	      db_value_domain_init (dest, desired_type, desired_domain->precision, desired_domain->scale);
	      db_value_put_null (dest);
	    }
	  else
	    {
	      db_make_null (dest);
	    }
	  return DOMAIN_INCOMPATIBLE;
	}
    }

  /* 
   * If src == dest, coerce into a temporary variable and
   * handle the conversion before returning.
   */
  if (src == dest)
    {
      target = &temp;
    }
  else
    {
      target = dest;
    }

  /* 
   * Initialize the destination domain, important for the
   * nm_ coercion functions thich take domain information inside the
   * destination db value.
   */
  db_value_domain_init (target, desired_type, desired_domain->precision, desired_domain->scale);

  if (TP_IS_CHAR_TYPE (desired_type))
    {
      if (desired_domain->collation_flag == TP_DOMAIN_COLL_ENFORCE)
	{
	  if (TP_IS_CHAR_TYPE (original_type))
	    {
	      db_string_put_cs_and_collation (target, TP_DOMAIN_CODESET (desired_domain),
					      TP_DOMAIN_COLLATION (desired_domain));

	      /* create a domain from source value */
	      p_tmp_desired_domain = tp_domain_resolve_value ((DB_VALUE *) src, &tmp_desired_domain);
	      p_tmp_desired_domain->codeset = TP_DOMAIN_CODESET (desired_domain);
	      p_tmp_desired_domain->collation_id = TP_DOMAIN_COLLATION (desired_domain);

	      desired_domain = p_tmp_desired_domain;
	    }
	  else if (TP_IS_SET_TYPE (original_type))
	    {
	      TP_DOMAIN *curr_set_dom;
	      TP_DOMAIN *elem_dom;
	      TP_DOMAIN *new_elem_dom;
	      TP_DOMAIN *save_elem_dom_next;

	      /* source value already exists, we expect that a collection domain already exists and is cached */
	      curr_set_dom = tp_domain_resolve_value ((DB_VALUE *) src, NULL);
	      elem_dom = curr_set_dom->setdomain;
	      curr_set_dom->setdomain = NULL;

	      /* copy only parent collection domain */
	      p_tmp_desired_domain = tp_domain_copy (curr_set_dom, false);
	      curr_set_dom->setdomain = elem_dom;
	      while (elem_dom != NULL)
		{
		  /* create a new domain from this */
		  save_elem_dom_next = elem_dom->next;
		  elem_dom->next = NULL;
		  new_elem_dom = tp_domain_copy (elem_dom, false);
		  elem_dom->next = save_elem_dom_next;

		  if (TP_IS_CHAR_TYPE (TP_DOMAIN_TYPE (elem_dom)))
		    {
		      /* for string domains overwrite collation */
		      new_elem_dom->collation_id = TP_DOMAIN_COLLATION (desired_domain);
		      new_elem_dom->codeset = TP_DOMAIN_CODESET (desired_domain);
		    }

		  tp_domain_add (&(p_tmp_desired_domain->setdomain), new_elem_dom);
		  elem_dom = elem_dom->next;
		}

	      desired_domain = p_tmp_desired_domain;
	    }
	  else
	    {
	      /* ENUM values are included here (cannot be HV) */
	      /* source value does not have collation, we leave it as it is */
	      if (src != dest)
		{
		  pr_clone_value ((DB_VALUE *) src, dest);
		}
	      return DOMAIN_COMPATIBLE;
	    }
	  desired_type = TP_DOMAIN_TYPE (desired_domain);
	}
      else if (desired_domain->collation_flag == TP_DOMAIN_COLL_NORMAL)
	{
	  db_string_put_cs_and_collation (target, TP_DOMAIN_CODESET (desired_domain),
					  TP_DOMAIN_COLLATION (desired_domain));
	}
      else
	{
	  assert (desired_domain->collation_flag == TP_DOMAIN_COLL_LEAVE);
	  if (TP_IS_CHAR_TYPE (original_type))
	    {
	      db_string_put_cs_and_collation (target, DB_GET_STRING_CODESET (src), DB_GET_STRING_COLLATION (src));
	    }
	}
    }

  switch (desired_type)
    {
    case DB_TYPE_SHORT:
      switch (original_type)
	{
	case DB_TYPE_MONETARY:
	  v_money = DB_GET_MONETARY (src);
	  if (OR_CHECK_SHORT_OVERFLOW (v_money->amount))
	    {
	      status = DOMAIN_OVERFLOW;
	    }
	  else
	    {
	      db_make_short (target, (short) ROUND (v_money->amount));
	    }
	  break;
	case DB_TYPE_INTEGER:
	  if (OR_CHECK_SHORT_OVERFLOW (DB_GET_INT (src)))
	    {
	      status = DOMAIN_OVERFLOW;
	    }
	  else
	    {
	      db_make_short (target, DB_GET_INT (src));
	    }
	  break;
	case DB_TYPE_BIGINT:
	  if (OR_CHECK_SHORT_OVERFLOW (DB_GET_BIGINT (src)))
	    {
	      status = DOMAIN_OVERFLOW;
	    }
	  else
	    {
	      db_make_short (target, (short) DB_GET_BIGINT (src));
	    }
	  break;
	case DB_TYPE_FLOAT:
	  if (OR_CHECK_SHORT_OVERFLOW (DB_GET_FLOAT (src)))
	    {
	      status = DOMAIN_OVERFLOW;
	    }
	  else
	    {
	      db_make_short (target, (short) ROUND (DB_GET_FLOAT (src)));
	    }
	  break;
	case DB_TYPE_DOUBLE:
	  if (OR_CHECK_SHORT_OVERFLOW (DB_GET_DOUBLE (src)))
	    {
	      status = DOMAIN_OVERFLOW;
	    }
	  else
	    {
	      db_make_short (target, (short) ROUND (DB_GET_DOUBLE (src)));
	    }
	  break;
	case DB_TYPE_NUMERIC:
	  status = (TP_DOMAIN_STATUS) numeric_db_value_coerce_from_num ((DB_VALUE *) src, target, &data_stat);
	  if (status != NO_ERROR)
	    {
	      status = DOMAIN_OVERFLOW;
	    }
	  break;
	case DB_TYPE_CHAR:
	case DB_TYPE_VARCHAR:
	case DB_TYPE_NCHAR:
	case DB_TYPE_VARNCHAR:
	  {
	    double num_value = 0.0;

	    if (tp_atof (src, &num_value, &data_stat) != NO_ERROR || data_stat == DATA_STATUS_NOT_CONSUMED)
	      {
		if (er_errid () != NO_ERROR)	/* i.e, malloc failure */
		  {
		    return DOMAIN_ERROR;
		  }
		status = DOMAIN_INCOMPATIBLE;	/* conversion error */
		break;
	      }

	    if (data_stat == DATA_STATUS_TRUNCATED || OR_CHECK_SHORT_OVERFLOW (num_value))
	      {
		status = DOMAIN_OVERFLOW;
	      }
	    else
	      {
		db_make_short (target, (short) ROUND (num_value));
	      }
	    break;
	  }
	case DB_TYPE_ENUMERATION:
	  db_make_short (target, DB_GET_ENUM_SHORT (src));
	  break;
	default:
	  status = DOMAIN_INCOMPATIBLE;
	  break;
	}
      break;

    case DB_TYPE_INTEGER:
      switch (original_type)
	{
	case DB_TYPE_SHORT:
	  db_make_int (target, DB_GET_SHORT (src));
	  break;
	case DB_TYPE_BIGINT:
	  if (OR_CHECK_INT_OVERFLOW (DB_GET_BIGINT (src)))
	    {
	      status = DOMAIN_OVERFLOW;
	    }
	  else
	    {
	      db_make_int (target, (int) DB_GET_BIGINT (src));
	    }
	  break;
	case DB_TYPE_MONETARY:
	  v_money = DB_GET_MONETARY (src);
	  if (OR_CHECK_INT_OVERFLOW (v_money->amount))
	    {
	      status = DOMAIN_OVERFLOW;
	    }
	  else
	    {
	      db_make_int (target, (int) ROUND (v_money->amount));
	    }
	  break;
	case DB_TYPE_FLOAT:
	  {
	    int tmp_int;
	    float tmp_float;

	    if (OR_CHECK_INT_OVERFLOW (DB_GET_FLOAT (src)))
	      {
		status = DOMAIN_OVERFLOW;
	      }
	    else
	      {
		tmp_float = DB_GET_FLOAT (src);
		tmp_int = (int) ROUND (tmp_float);

#if defined(AIX)
		/* in AIX, float/double to int will not overflow, make it the same as linux. */
		if (tmp_float == (float) DB_INT32_MAX)
		  {
		    tmp_int = DB_INT32_MIN;
		  }
#endif

		if (OR_CHECK_ASSIGN_OVERFLOW (tmp_int, tmp_float))
		  {
		    status = DOMAIN_OVERFLOW;
		  }
		else
		  {
		    db_make_int (target, tmp_int);
		  }
	      }
	  }
	  break;
	case DB_TYPE_DOUBLE:
	  if (OR_CHECK_INT_OVERFLOW (DB_GET_DOUBLE (src)))
	    {
	      status = DOMAIN_OVERFLOW;
	    }
	  else
	    {
	      db_make_int (target, (int) ROUND (DB_GET_DOUBLE (src)));
	    }
	  break;
	case DB_TYPE_NUMERIC:
	  status = (TP_DOMAIN_STATUS) numeric_db_value_coerce_from_num ((DB_VALUE *) src, target, &data_stat);
	  if (status != NO_ERROR)
	    {
	      status = DOMAIN_OVERFLOW;
	    }
	  break;
	case DB_TYPE_CHAR:
	case DB_TYPE_VARCHAR:
	case DB_TYPE_NCHAR:
	case DB_TYPE_VARNCHAR:
	  {
	    double num_value = 0.0;

	    if (tp_atof (src, &num_value, &data_stat) != NO_ERROR || data_stat == DATA_STATUS_NOT_CONSUMED)
	      {
		if (er_errid () != NO_ERROR)	/* i.e, malloc failure */
		  {
		    return DOMAIN_ERROR;
		  }
		status = DOMAIN_INCOMPATIBLE;	/* conversion error */
		break;
	      }

	    if (data_stat == DATA_STATUS_TRUNCATED || OR_CHECK_INT_OVERFLOW (num_value))
	      {
		status = DOMAIN_OVERFLOW;
	      }
	    else
	      {
		db_make_int (target, (int) ROUND (num_value));
	      }
	    break;
	  }
	case DB_TYPE_ENUMERATION:
	  db_make_int (target, DB_GET_ENUM_SHORT (src));
	  break;
	default:
	  status = DOMAIN_INCOMPATIBLE;
	  break;
	}
      break;

    case DB_TYPE_BIGINT:
      switch (original_type)
	{
	case DB_TYPE_SHORT:
	  db_make_bigint (target, DB_GET_SHORT (src));
	  break;
	case DB_TYPE_INTEGER:
	  db_make_bigint (target, DB_GET_INT (src));
	  break;
	case DB_TYPE_MONETARY:
	  {
	    DB_BIGINT tmp_bi;

	    v_money = DB_GET_MONETARY (src);
	    if (OR_CHECK_BIGINT_OVERFLOW (v_money->amount))
	      {
		status = DOMAIN_OVERFLOW;
	      }
	    else
	      {
		tmp_bi = (DB_BIGINT) ROUND (v_money->amount);
		if (OR_CHECK_ASSIGN_OVERFLOW (tmp_bi, v_money->amount))
		  {
		    status = DOMAIN_OVERFLOW;
		  }
		else
		  {
		    db_make_bigint (target, tmp_bi);
		  }
	      }
	  }
	  break;
	case DB_TYPE_FLOAT:
	  {
	    float tmp_float;
	    DB_BIGINT tmp_bi;

	    if (OR_CHECK_BIGINT_OVERFLOW (DB_GET_FLOAT (src)))
	      {
		status = DOMAIN_OVERFLOW;
	      }
	    else
	      {
		tmp_float = DB_GET_FLOAT (src);
		tmp_bi = (DB_BIGINT) ROUND (tmp_float);

#if defined(AIX)
		/* in AIX, float/double to int64 will not overflow, make it the same as linux. */
		if (tmp_float == (float) DB_BIGINT_MAX)
		  {
		    tmp_bi = DB_BIGINT_MIN;
		  }
#endif
		if (OR_CHECK_ASSIGN_OVERFLOW (tmp_bi, tmp_float))
		  {
		    status = DOMAIN_OVERFLOW;
		  }
		else
		  {
		    db_make_bigint (target, tmp_bi);
		  }
	      }
	  }
	  break;
	case DB_TYPE_DOUBLE:
	  {
	    double tmp_double;
	    DB_BIGINT tmp_bi;

	    if (OR_CHECK_BIGINT_OVERFLOW (DB_GET_DOUBLE (src)))
	      {
		status = DOMAIN_OVERFLOW;
	      }
	    else
	      {
		tmp_double = DB_GET_DOUBLE (src);
		tmp_bi = (DB_BIGINT) ROUND (tmp_double);

#if defined(AIX)
		/* in AIX, float/double to int64 will not overflow, make it the same as linux. */
		if (tmp_double == (double) DB_BIGINT_MAX)
		  {
		    tmp_bi = DB_BIGINT_MIN;
		  }
#endif
		if (OR_CHECK_ASSIGN_OVERFLOW (tmp_bi, tmp_double))
		  {
		    status = DOMAIN_OVERFLOW;
		  }
		else
		  {
		    db_make_bigint (target, tmp_bi);
		  }
	      }
	  }
	  break;
	case DB_TYPE_NUMERIC:
	  status = (TP_DOMAIN_STATUS) numeric_db_value_coerce_from_num ((DB_VALUE *) src, target, &data_stat);
	  if (status != NO_ERROR)
	    {
	      status = DOMAIN_OVERFLOW;
	    }
	  break;
	case DB_TYPE_CHAR:
	case DB_TYPE_VARCHAR:
	case DB_TYPE_NCHAR:
	case DB_TYPE_VARNCHAR:
	  {
	    DB_BIGINT num_value = 0;

	    if (tp_atobi (src, &num_value, &data_stat) != NO_ERROR)
	      {
		if (er_errid () != NO_ERROR)	/* i.e, malloc failure */
		  {
		    return DOMAIN_ERROR;
		  }
		status = DOMAIN_INCOMPATIBLE;	/* conversion error */
		break;
	      }

	    if (data_stat == DATA_STATUS_TRUNCATED)
	      {
		status = DOMAIN_OVERFLOW;
		break;
	      }
	    db_make_bigint (target, num_value);
	    break;
	  }
	case DB_TYPE_ENUMERATION:
	  db_make_bigint (target, DB_GET_ENUM_SHORT (src));
	  break;
	default:
	  status = DOMAIN_INCOMPATIBLE;
	  break;
	}
      break;

    case DB_TYPE_FLOAT:
      switch (original_type)
	{
	case DB_TYPE_SHORT:
	  db_make_float (target, (float) DB_GET_SHORT (src));
	  break;
	case DB_TYPE_INTEGER:
	  db_make_float (target, (float) DB_GET_INT (src));
	  break;
	case DB_TYPE_BIGINT:
	  db_make_float (target, (float) DB_GET_BIGINT (src));
	  break;
	case DB_TYPE_DOUBLE:
	  if (OR_CHECK_FLOAT_OVERFLOW (DB_GET_DOUBLE (src)))
	    {
	      status = DOMAIN_OVERFLOW;
	    }
	  else
	    {
	      db_make_float (target, (float) DB_GET_DOUBLE (src));
	    }
	  break;
	case DB_TYPE_MONETARY:
	  v_money = DB_GET_MONETARY (src);
	  if (OR_CHECK_FLOAT_OVERFLOW (v_money->amount))
	    {
	      status = DOMAIN_OVERFLOW;
	    }
	  else
	    {
	      db_make_float (target, (float) v_money->amount);
	    }
	  break;
	case DB_TYPE_NUMERIC:
	  status = (TP_DOMAIN_STATUS) numeric_db_value_coerce_from_num ((DB_VALUE *) src, target, &data_stat);
	  if (status != NO_ERROR)
	    {
	      status = DOMAIN_OVERFLOW;
	    }
	  break;
	case DB_TYPE_CHAR:
	case DB_TYPE_VARCHAR:
	case DB_TYPE_NCHAR:
	case DB_TYPE_VARNCHAR:
	  {
	    double num_value = 0.0;

	    if (tp_atof (src, &num_value, &data_stat) != NO_ERROR || data_stat == DATA_STATUS_NOT_CONSUMED)
	      {
		if (er_errid () != NO_ERROR)	/* i.e, malloc failure */
		  {
		    return DOMAIN_ERROR;
		  }
		status = DOMAIN_INCOMPATIBLE;	/* conversion error */
		break;
	      }

	    if (data_stat == DATA_STATUS_TRUNCATED || OR_CHECK_FLOAT_OVERFLOW (num_value))
	      {
		status = DOMAIN_OVERFLOW;
	      }
	    else
	      {
		db_make_float (target, (float) num_value);
	      }
	    break;
	  }
	case DB_TYPE_ENUMERATION:
	  db_make_float (target, (float) DB_GET_ENUM_SHORT (src));
	  break;
	default:
	  status = DOMAIN_INCOMPATIBLE;
	  break;
	}
      break;

    case DB_TYPE_DOUBLE:
      switch (original_type)
	{
	case DB_TYPE_SHORT:
	  db_make_double (target, (double) DB_GET_SHORT (src));
	  break;
	case DB_TYPE_INTEGER:
	  db_make_double (target, (double) DB_GET_INT (src));
	  break;
	case DB_TYPE_BIGINT:
	  db_make_double (target, (double) DB_GET_BIGINT (src));
	  break;
	case DB_TYPE_FLOAT:
	  db_make_double (target, (double) DB_GET_FLOAT (src));
	  break;
	case DB_TYPE_MONETARY:
	  v_money = DB_GET_MONETARY (src);
	  db_make_double (target, (double) v_money->amount);
	  break;
	case DB_TYPE_NUMERIC:
	  status = (TP_DOMAIN_STATUS) numeric_db_value_coerce_from_num ((DB_VALUE *) src, target, &data_stat);
	  if (status != NO_ERROR)
	    {
	      status = DOMAIN_OVERFLOW;
	    }
	  break;
	case DB_TYPE_CHAR:
	case DB_TYPE_VARCHAR:
	case DB_TYPE_NCHAR:
	case DB_TYPE_VARNCHAR:
	  {
	    double num_value = 0.0;

	    if (tp_atof (src, &num_value, &data_stat) != NO_ERROR || data_stat == DATA_STATUS_NOT_CONSUMED)
	      {
		if (er_errid () != NO_ERROR)	/* i.e, malloc failure */
		  {
		    return DOMAIN_ERROR;
		  }
		status = DOMAIN_INCOMPATIBLE;	/* conversion error */
		break;
	      }

	    if (data_stat == DATA_STATUS_TRUNCATED)
	      {
		status = DOMAIN_OVERFLOW;
	      }
	    else
	      {
		db_make_double (target, num_value);
	      }

	    break;
	  }
	case DB_TYPE_ENUMERATION:
	  db_make_double (target, (double) DB_GET_ENUM_SHORT (src));
	  break;
	default:
	  status = DOMAIN_INCOMPATIBLE;
	  break;
	}
      break;

    case DB_TYPE_NUMERIC:
      /* 
       * Numeric-to-numeric coercion will be handled in the nm_ module.
       * The desired precision & scale is communicated through the destination
       * value.
       */
      switch (original_type)
	{
	case DB_TYPE_CHAR:
	case DB_TYPE_VARCHAR:
	case DB_TYPE_NCHAR:
	case DB_TYPE_VARNCHAR:
	  {
	    DB_VALUE temp;

	    if (tp_atonumeric (src, &temp) != NO_ERROR)
	      {
		if (er_errid () != NO_ERROR)
		  {
		    return DOMAIN_ERROR;
		  }
		status = DOMAIN_INCOMPATIBLE;
	      }
	    else
	      {
		status = tp_value_coerce (&temp, target, desired_domain);
	      }
	    break;
	  }
	default:
	  {
	    int error_code = numeric_db_value_coerce_to_num ((DB_VALUE *) src,
							     target,
							     &data_stat);
	    if (error_code == ER_IT_DATA_OVERFLOW || data_stat == DATA_STATUS_TRUNCATED)
	      {
		status = DOMAIN_OVERFLOW;
	      }
	    else if (error_code != NO_ERROR)
	      {
		status = DOMAIN_INCOMPATIBLE;
	      }
	    else
	      {
		status = DOMAIN_COMPATIBLE;
	      }
	  }
	  break;
	}
      break;

    case DB_TYPE_MONETARY:
      switch (original_type)
	{
	case DB_TYPE_SHORT:
	  db_make_monetary (target, DB_CURRENCY_DEFAULT, (double) DB_GET_SHORT (src));
	  break;
	case DB_TYPE_INTEGER:
	  db_make_monetary (target, DB_CURRENCY_DEFAULT, (double) DB_GET_INT (src));
	  break;
	case DB_TYPE_BIGINT:
	  db_make_monetary (target, DB_CURRENCY_DEFAULT, (double) DB_GET_BIGINT (src));
	  break;
	case DB_TYPE_FLOAT:
	  db_make_monetary (target, DB_CURRENCY_DEFAULT, (double) DB_GET_FLOAT (src));
	  break;
	case DB_TYPE_DOUBLE:
	  db_make_monetary (target, DB_CURRENCY_DEFAULT, DB_GET_DOUBLE (src));
	  break;
	case DB_TYPE_NUMERIC:
	  status = (TP_DOMAIN_STATUS) numeric_db_value_coerce_from_num ((DB_VALUE *) src, target, &data_stat);
	  if (status != NO_ERROR)
	    {
	      status = DOMAIN_OVERFLOW;
	    }
	  break;
	case DB_TYPE_CHAR:
	case DB_TYPE_VARCHAR:
	case DB_TYPE_NCHAR:
	case DB_TYPE_VARNCHAR:
	  {
	    double num_value = 0.0;

	    if (tp_atof (src, &num_value, &data_stat) != NO_ERROR || data_stat == DATA_STATUS_NOT_CONSUMED)
	      {
		if (er_errid () != NO_ERROR)	/* i.e, malloc failure */
		  {
		    return DOMAIN_ERROR;
		  }
		status = DOMAIN_INCOMPATIBLE;	/* conversion error */
		break;
	      }

	    if (data_stat == DATA_STATUS_TRUNCATED)
	      {
		status = DOMAIN_OVERFLOW;
	      }
	    else
	      {
		db_make_monetary (target, DB_CURRENCY_DEFAULT, num_value);
	      }
	    break;
	  }
	case DB_TYPE_ENUMERATION:
	  db_make_monetary (target, DB_CURRENCY_DEFAULT, DB_GET_ENUM_SHORT (src));
	  break;
	default:
	  status = DOMAIN_INCOMPATIBLE;
	  break;
	}
      break;

    case DB_TYPE_UTIME:
      switch (original_type)
	{
	case DB_TYPE_VARCHAR:
	case DB_TYPE_CHAR:
	case DB_TYPE_NCHAR:
	case DB_TYPE_VARNCHAR:
	  if (tp_atoutime (src, &v_utime) != NO_ERROR)
	    {
	      return DOMAIN_ERROR;
	    }
	  else
	    {
	      db_make_timestamp (target, v_utime);
	    }
	  break;

	case DB_TYPE_ENUMERATION:
	  {
	    DB_VALUE varchar_val;
	    if (tp_enumeration_to_varchar (src, &varchar_val) != NO_ERROR)
	      {
		status = DOMAIN_ERROR;
		break;
	      }
	    status =
	      tp_value_cast_internal (&varchar_val, target, desired_domain, coercion_mode, do_domain_select, false);
	    break;
	  }

	case DB_TYPE_DATETIME:
	case DB_TYPE_DATE:
	  {
	    if (original_type == DB_TYPE_DATE)
	      {
		v_date = *DB_GET_DATE (src);
		db_time_encode (&v_time, 0, 0, 0);
	      }
	    else
	      {
		v_datetime = *DB_GET_DATETIME (src);
		v_date = v_datetime.date;
		v_time = v_datetime.time / 1000;
	      }

	    if (db_timestamp_encode_ses (&v_date, &v_time, &v_utime, NULL) == NO_ERROR)
	      {
		db_make_timestamp (target, v_utime);
	      }
	    else
	      {
		status = DOMAIN_OVERFLOW;
	      }
	    break;
	  }

	case DB_TYPE_DATETIMELTZ:
	  v_datetime = *DB_GET_DATETIME (src);
	  v_date = v_datetime.date;
	  v_time = v_datetime.time / 1000;

	  if (db_timestamp_encode_utc (&v_date, &v_time, &v_utime) == NO_ERROR)
	    {
	      db_make_timestamp (target, v_utime);
	    }
	  else
	    {
	      status = DOMAIN_OVERFLOW;
	    }
	  break;

	case DB_TYPE_DATETIMETZ:
	  v_datetimetz = *DB_GET_DATETIMETZ (src);
	  v_date = v_datetimetz.datetime.date;
	  v_time = v_datetimetz.datetime.time / 1000;

	  if (db_timestamp_encode_utc (&v_date, &v_time, &v_utime) == NO_ERROR)
	    {
	      db_make_timestamp (target, v_utime);
	    }
	  else
	    {
	      status = DOMAIN_OVERFLOW;
	    }
	  break;

	case DB_TYPE_TIMESTAMPLTZ:
	  /* copy timestamp (UTC) */
	  db_make_timestamp (target, *DB_GET_TIMESTAMP (src));
	  break;

	case DB_TYPE_TIMESTAMPTZ:
	  v_timestamptz = *DB_GET_TIMESTAMPTZ (src);
	  /* copy timestamp (UTC) */
	  db_make_timestamp (target, v_timestamptz.timestamp);
	  break;

	default:
	  status = tp_value_coerce ((DB_VALUE *) src, target, &tp_Integer_domain);
	  if (status == DOMAIN_COMPATIBLE)
	    {
	      int tmpint;
	      if ((tmpint = DB_GET_INT (target)) >= 0)
		{
		  db_make_timestamp (target, (DB_UTIME) tmpint);
		}
	      else
		{
		  status = DOMAIN_INCOMPATIBLE;
		}
	    }
	  break;
	}
      break;

    case DB_TYPE_TIMESTAMPTZ:
      switch (original_type)
	{
	case DB_TYPE_VARCHAR:
	case DB_TYPE_CHAR:
	case DB_TYPE_NCHAR:
	case DB_TYPE_VARNCHAR:
	  if (tp_atotimestamptz (src, &v_timestamptz) != NO_ERROR)
	    {
	      return DOMAIN_ERROR;
	    }
	  else
	    {
	      db_make_timestamptz (target, &v_timestamptz);
	    }
	  break;

	case DB_TYPE_ENUMERATION:
	  {
	    DB_VALUE varchar_val;
	    if (tp_enumeration_to_varchar (src, &varchar_val) != NO_ERROR)
	      {
		status = DOMAIN_ERROR;
		break;
	      }
	    status =
	      tp_value_cast_internal (&varchar_val, target, desired_domain, coercion_mode, do_domain_select, false);
	    break;
	  }

	case DB_TYPE_DATETIME:
	case DB_TYPE_DATE:
	  /* convert from session to UTC */
	  if (original_type == DB_TYPE_DATETIME)
	    {
	      v_datetime = *DB_GET_DATETIME (src);
	      v_date = v_datetime.date;
	      v_time = v_datetime.time / 1000;
	    }
	  else
	    {
	      assert (original_type == DB_TYPE_DATE);
	      v_date = *DB_GET_DATE (src);
	      v_time = 0;
	    }

	  if (db_timestamp_encode_ses (&v_date, &v_time, &v_timestamptz.timestamp, &v_timestamptz.tz_id) != NO_ERROR)
	    {
	      status = DOMAIN_ERROR;
	    }
	  else
	    {
	      db_make_timestamptz (target, &v_timestamptz);
	    }
	  break;

	case DB_TYPE_DATETIMELTZ:
	  v_datetime = *DB_GET_DATETIME (src);
	  v_date = v_datetime.date;
	  v_time = v_datetime.time / 1000;

	  /* encode DT as UTC and the TZ of session */
	  if (db_timestamp_encode_utc (&v_date, &v_time, &v_timestamptz.timestamp) != NO_ERROR)
	    {
	      status = DOMAIN_ERROR;
	      break;
	    }
	  if (tz_create_session_tzid_for_datetime (&v_datetime, true, &(v_timestamptz.tz_id)) != NO_ERROR)
	    {
	      status = DOMAIN_ERROR;
	    }
	  else
	    {
	      db_make_timestamptz (target, &v_timestamptz);
	    }
	  break;

	case DB_TYPE_DATETIMETZ:
	  v_datetimetz = *DB_GET_DATETIMETZ (src);
	  v_date = v_datetimetz.datetime.date;
	  v_time = v_datetimetz.datetime.time / 1000;

	  /* encode TS to DT (UTC) and copy TZ from DT_TZ */
	  if (db_timestamp_encode_utc (&v_date, &v_time, &v_timestamptz.timestamp) == NO_ERROR)
	    {
	      v_timestamptz.tz_id = v_datetimetz.tz_id;
	      db_make_timestamptz (target, &v_timestamptz);
	    }
	  else
	    {
	      status = DOMAIN_OVERFLOW;
	    }
	  break;

	case DB_TYPE_TIMESTAMP:
	case DB_TYPE_TIMESTAMPLTZ:
	  /* copy TS value and create TZ_ID for system TZ */
	  v_timestamptz.timestamp = *DB_GET_TIMESTAMP (src);

	  if (tz_create_session_tzid_for_timestamp (&v_timestamptz.timestamp, &(v_timestamptz.tz_id)) != NO_ERROR)
	    {
	      status = DOMAIN_ERROR;
	      break;
	    }

	  db_make_timestamptz (target, &v_timestamptz);
	  break;

	default:
	  status = tp_value_coerce ((DB_VALUE *) src, target, &tp_Integer_domain);
	  if (status == DOMAIN_COMPATIBLE)
	    {
	      int tmpint;

	      tmpint = DB_GET_INT (target);
	      if (tmpint < 0)
		{
		  status = DOMAIN_INCOMPATIBLE;
		  break;
		}
	      v_timestamptz.timestamp = (DB_UTIME) tmpint;

	      if (tz_create_session_tzid_for_timestamp (&v_timestamptz.timestamp, &v_timestamptz.tz_id) != NO_ERROR)
		{
		  status = DOMAIN_INCOMPATIBLE;
		  break;
		}

	      db_make_timestamptz (target, &v_timestamptz);
	    }
	  break;
	}
      break;

    case DB_TYPE_TIMESTAMPLTZ:
      switch (original_type)
	{
	case DB_TYPE_VARCHAR:
	case DB_TYPE_CHAR:
	case DB_TYPE_NCHAR:
	case DB_TYPE_VARNCHAR:
	  /* read as DATETIMETZ */
	  if (tp_atotimestamptz (src, &v_timestamptz) != NO_ERROR)
	    {
	      status = DOMAIN_ERROR;
	      break;
	    }
	  else
	    {
	      db_make_timestampltz (target, v_timestamptz.timestamp);
	    }
	  break;

	case DB_TYPE_ENUMERATION:
	  {
	    DB_VALUE varchar_val;
	    if (tp_enumeration_to_varchar (src, &varchar_val) != NO_ERROR)
	      {
		status = DOMAIN_ERROR;
		break;
	      }
	    status =
	      tp_value_cast_internal (&varchar_val, target, desired_domain, coercion_mode, do_domain_select, false);
	    break;
	  }

	case DB_TYPE_DATETIME:
	case DB_TYPE_DATE:
	  {
	    if (original_type == DB_TYPE_DATETIME)
	      {
		v_datetime = *DB_GET_DATETIME (src);
		v_date = v_datetime.date;
		v_time = v_datetime.time / 1000;
	      }
	    else
	      {
		assert (original_type == DB_TYPE_DATE);
		v_date = *DB_GET_DATE (src);
		v_time = 0;
	      }

	    if (db_timestamp_encode_ses (&v_date, &v_time, &v_utime, NULL) != NO_ERROR)
	      {
		status = DOMAIN_OVERFLOW;
		break;
	      }

	    db_make_timestampltz (target, v_utime);
	  }
	  break;

	case DB_TYPE_DATETIMELTZ:
	case DB_TYPE_DATETIMETZ:
	  if (original_type == DB_TYPE_DATETIMELTZ)
	    {
	      v_datetime = *DB_GET_DATETIME (src);
	      v_date = v_datetime.date;
	      v_time = v_datetime.time / 1000;
	    }
	  else
	    {
	      assert (original_type == DB_TYPE_DATETIMETZ);
	      v_datetimetz = *DB_GET_DATETIMETZ (src);
	      v_date = v_datetimetz.datetime.date;
	      v_time = v_datetimetz.datetime.time / 1000;
	    }

	  /* both values are in UTC */
	  if (db_timestamp_encode_utc (&v_date, &v_time, &v_utime) == NO_ERROR)
	    {
	      db_make_timestampltz (target, v_utime);
	    }
	  else
	    {
	      status = DOMAIN_OVERFLOW;
	    }
	  break;

	case DB_TYPE_TIMESTAMP:
	  /* original value stored in UTC, copy it */
	  db_make_timestampltz (target, *DB_GET_TIMESTAMP (src));
	  break;

	case DB_TYPE_TIMESTAMPTZ:
	  v_timestamptz = *DB_GET_TIMESTAMPTZ (src);
	  /* original value stored in UTC, copy it */
	  db_make_timestampltz (target, v_timestamptz.timestamp);
	  break;

	default:
	  status = tp_value_coerce ((DB_VALUE *) src, target, &tp_Integer_domain);
	  if (status == DOMAIN_COMPATIBLE)
	    {
	      int tmpint;
	      if ((tmpint = DB_GET_INT (target)) >= 0)
		{
		  db_make_timestampltz (target, (DB_UTIME) tmpint);
		}
	      else
		{
		  status = DOMAIN_INCOMPATIBLE;
		}
	    }
	  break;
	}
      break;

    case DB_TYPE_DATETIME:
      switch (original_type)
	{
	case DB_TYPE_VARCHAR:
	case DB_TYPE_CHAR:
	case DB_TYPE_NCHAR:
	case DB_TYPE_VARNCHAR:
	  if (tp_atoudatetime (src, &v_datetime) != NO_ERROR)
	    {
	      return DOMAIN_ERROR;
	    }
	  else
	    {
	      db_make_datetime (target, &v_datetime);
	    }
	  break;

	case DB_TYPE_ENUMERATION:
	  {
	    DB_VALUE varchar_val;
	    if (tp_enumeration_to_varchar (src, &varchar_val) != NO_ERROR)
	      {
		status = DOMAIN_ERROR;
		break;
	      }
	    status =
	      tp_value_cast_internal (&varchar_val, target, desired_domain, coercion_mode, do_domain_select, false);
	    break;
	  }

	case DB_TYPE_UTIME:
	case DB_TYPE_TIMESTAMPLTZ:
	  v_utime = *DB_GET_TIMESTAMP (src);
	  if (db_timestamp_decode_ses (&v_utime, &v_date, &v_time) != NO_ERROR)
	    {
	      status = DOMAIN_ERROR;
	      break;
	    }
	  v_datetime.date = v_date;
	  v_datetime.time = v_time * 1000;
	  db_make_datetime (target, &v_datetime);
	  break;

	case DB_TYPE_TIMESTAMPTZ:
	  v_timestamptz = *DB_GET_TIMESTAMPTZ (src);
	  if (db_timestamp_decode_w_tz_id (&v_timestamptz.timestamp, &v_timestamptz.tz_id, &v_date, &v_time) !=
	      NO_ERROR)
	    {
	      status = DOMAIN_ERROR;
	      break;
	    }
	  v_datetime.date = v_date;
	  v_datetime.time = v_time * 1000;
	  db_make_datetime (target, &v_datetime);
	  break;

	case DB_TYPE_DATE:
	  v_datetime.date = *DB_GET_DATE (src);
	  v_datetime.time = 0;
	  db_make_datetime (target, &v_datetime);
	  break;

	case DB_TYPE_DATETIMELTZ:
	  {
	    DB_DATETIME utc_dt;

	    /* DATETIMELTZ store in UTC, DATETIME in session TZ */
	    utc_dt = *DB_GET_DATETIME (src);
	    if (tz_datetimeltz_to_local (&utc_dt, &v_datetime) != NO_ERROR)
	      {
		status = DOMAIN_ERROR;
		break;
	      }

	    db_make_datetime (target, &v_datetime);
	  }
	  break;

	case DB_TYPE_DATETIMETZ:
	  /* DATETIMETZ store in UTC, DATETIME in session TZ */
	  v_datetimetz = *DB_GET_DATETIMETZ (src);
	  if (tz_utc_datetimetz_to_local (&v_datetimetz.datetime, &v_datetimetz.tz_id, &v_datetime) == NO_ERROR)
	    {
	      db_make_datetime (target, &v_datetime);
	    }
	  else
	    {
	      status = DOMAIN_ERROR;
	    }
	  break;

	default:
	  status = DOMAIN_INCOMPATIBLE;
	  break;
	}
      break;

    case DB_TYPE_DATETIMELTZ:
      switch (original_type)
	{
	case DB_TYPE_VARCHAR:
	case DB_TYPE_CHAR:
	case DB_TYPE_NCHAR:
	case DB_TYPE_VARNCHAR:
	  if (tp_atodatetimetz (src, &v_datetimetz) != NO_ERROR)
	    {
	      return DOMAIN_ERROR;
	    }
	  else
	    {
	      db_make_datetimeltz (target, &v_datetimetz.datetime);
	    }
	  break;

	case DB_TYPE_ENUMERATION:
	  {
	    DB_VALUE varchar_val;
	    if (tp_enumeration_to_varchar (src, &varchar_val) != NO_ERROR)
	      {
		status = DOMAIN_ERROR;
		break;
	      }
	    status =
	      tp_value_cast_internal (&varchar_val, target, desired_domain, coercion_mode, do_domain_select, false);
	    break;
	  }

	case DB_TYPE_UTIME:
	case DB_TYPE_TIMESTAMPLTZ:
	  v_utime = *DB_GET_TIMESTAMP (src);

	  (void) db_timestamp_decode_utc (&v_utime, &v_date, &v_time);
	  v_datetime.time = v_time * 1000;
	  v_datetime.date = v_date;
	  db_make_datetimeltz (target, &v_datetime);
	  break;

	case DB_TYPE_TIMESTAMPTZ:
	  v_timestamptz = *DB_GET_TIMESTAMPTZ (src);
	  (void) db_timestamp_decode_utc (&v_timestamptz.timestamp, &v_date, &v_time);
	  v_datetime.time = v_time * 1000;
	  v_datetime.date = v_date;
	  db_make_datetimeltz (target, &v_datetime);
	  break;

	case DB_TYPE_DATE:
	case DB_TYPE_DATETIME:
	  if (original_type == DB_TYPE_DATE)
	    {
	      v_datetime.date = *DB_GET_DATE (src);
	      v_datetime.time = 0;
	    }
	  else
	    {
	      v_datetime = *DB_GET_DATETIME (src);
	    }

	  if (tz_create_datetimetz_from_ses (&v_datetime, &v_datetimetz) != NO_ERROR)
	    {
	      status = DOMAIN_ERROR;
	      break;
	    }
	  db_make_datetimeltz (target, &v_datetimetz.datetime);
	  break;

	case DB_TYPE_DATETIMETZ:
	  /* copy (UTC) */
	  v_datetimetz = *DB_GET_DATETIMETZ (src);
	  db_make_datetimeltz (target, &v_datetimetz.datetime);
	  break;

	case DB_TYPE_TIME:
	case DB_TYPE_TIMELTZ:
	case DB_TYPE_TIMETZ:
	  status = DOMAIN_INCOMPATIBLE;
	  break;

	default:
	  status = DOMAIN_INCOMPATIBLE;
	  break;
	}
      break;

    case DB_TYPE_DATETIMETZ:
      switch (original_type)
	{
	case DB_TYPE_VARCHAR:
	case DB_TYPE_CHAR:
	case DB_TYPE_NCHAR:
	case DB_TYPE_VARNCHAR:
	  {
	    if (tp_atodatetimetz (src, &v_datetimetz) != NO_ERROR)
	      {
		status = DOMAIN_ERROR;
		break;
	      }

	    db_make_datetimetz (target, &v_datetimetz);
	  }

	  break;

	case DB_TYPE_ENUMERATION:
	  {
	    DB_VALUE varchar_val;
	    if (tp_enumeration_to_varchar (src, &varchar_val) != NO_ERROR)
	      {
		status = DOMAIN_ERROR;
		break;
	      }
	    status =
	      tp_value_cast_internal (&varchar_val, target, desired_domain, coercion_mode, do_domain_select, false);
	    break;
	  }

	case DB_TYPE_UTIME:
	case DB_TYPE_TIMESTAMPLTZ:
	  {
	    v_utime = *DB_GET_TIMESTAMP (src);
	    db_timestamp_decode_utc (&v_utime, &v_date, &v_time);
	    v_datetimetz.datetime.time = v_time * 1000;
	    v_datetimetz.datetime.date = v_date;

	    if (tz_create_session_tzid_for_datetime (&v_datetimetz.datetime, true, &v_datetimetz.tz_id) != NO_ERROR)
	      {
		status = DOMAIN_ERROR;
		break;
	      }

	    db_make_datetimetz (target, &v_datetimetz);
	  }
	  break;

	case DB_TYPE_TIMESTAMPTZ:
	  v_timestamptz = *DB_GET_TIMESTAMPTZ (src);
	  (void) db_timestamp_decode_utc (&v_timestamptz.timestamp, &v_date, &v_time);
	  v_datetimetz.datetime.time = v_time * 1000;
	  v_datetimetz.datetime.date = v_date;
	  v_datetimetz.tz_id = v_timestamptz.tz_id;
	  db_make_datetimetz (target, &v_datetimetz);
	  break;

	case DB_TYPE_DATE:
	  v_datetime.date = *DB_GET_DATE (src);
	  v_datetime.time = 0;

	  if (tz_create_datetimetz_from_ses (&v_datetime, &v_datetimetz) != NO_ERROR)
	    {
	      status = DOMAIN_ERROR;
	      break;
	    }
	  db_make_datetimetz (target, &v_datetimetz);
	  break;

	case DB_TYPE_DATETIME:
	  if (tz_create_datetimetz_from_ses (DB_GET_DATETIME (src), &v_datetimetz) != NO_ERROR)
	    {
	      status = DOMAIN_ERROR;
	      break;
	    }
	  db_make_datetimetz (target, &v_datetimetz);
	  break;

	case DB_TYPE_DATETIMELTZ:
	  v_datetimetz.datetime = *DB_GET_DATETIME (src);
	  if (tz_create_session_tzid_for_datetime (&v_datetimetz.datetime, true, &v_datetimetz.tz_id) != NO_ERROR)
	    {
	      status = DOMAIN_ERROR;
	      break;
	    }
	  db_make_datetimetz (target, &v_datetimetz);
	  break;

	case DB_TYPE_TIME:
	case DB_TYPE_TIMELTZ:
	case DB_TYPE_TIMETZ:
	  status = DOMAIN_INCOMPATIBLE;
	  break;

	default:
	  status = DOMAIN_INCOMPATIBLE;
	  break;
	}
      break;

    case DB_TYPE_DATE:
      switch (original_type)
	{
	case DB_TYPE_VARCHAR:
	case DB_TYPE_CHAR:
	case DB_TYPE_NCHAR:
	case DB_TYPE_VARNCHAR:
	  if (tp_atodate (src, &v_date) == NO_ERROR)
	    {
	      db_date_decode (&v_date, &month, &day, &year);
	    }
	  else
	    {
	      return DOMAIN_ERROR;
	    }

	  if (db_make_date (target, month, day, year) != NO_ERROR)
	    {
	      return DOMAIN_ERROR;
	    }
	  break;

	case DB_TYPE_ENUMERATION:
	  {
	    DB_VALUE varchar_val;
	    if (tp_enumeration_to_varchar (src, &varchar_val) != NO_ERROR)
	      {
		status = DOMAIN_ERROR;
		break;
	      }
	    status =
	      tp_value_cast_internal (&varchar_val, target, desired_domain, coercion_mode, do_domain_select, false);
	    break;
	  }

	case DB_TYPE_UTIME:
	case DB_TYPE_TIMESTAMPLTZ:
	  (void) db_timestamp_decode_ses (DB_GET_UTIME (src), &v_date, NULL);
	  db_date_decode (&v_date, &month, &day, &year);
	  db_make_date (target, month, day, year);
	  break;

	case DB_TYPE_TIMESTAMPTZ:
	  v_timestamptz = *DB_GET_TIMESTAMPTZ (src);
	  if (db_timestamp_decode_w_tz_id (&v_timestamptz.timestamp, &v_timestamptz.tz_id, &v_date, NULL) != NO_ERROR)
	    {
	      status = DOMAIN_ERROR;
	      break;
	    }
	  db_date_decode (&v_date, &month, &day, &year);
	  db_make_date (target, month, day, year);
	  break;

	case DB_TYPE_DATETIME:
	  db_datetime_decode ((DB_DATETIME *) DB_GET_DATETIME (src), &month, &day, &year, &hour, &minute, &second,
			      &millisecond);
	  db_make_date (target, month, day, year);
	  break;
	case DB_TYPE_DATETIMELTZ:
	case DB_TYPE_DATETIMETZ:
	  {
	    DB_DATETIME *utc_dt_p;
	    DB_DATETIMETZ *dt_tz_p;
	    TZ_ID tz_id;

	    /* DATETIMELTZ and DATETIMETZ store in UTC, convert to session */
	    if (original_type == DB_TYPE_DATETIMELTZ)
	      {
		utc_dt_p = DB_GET_DATETIME (src);
		if (tz_create_session_tzid_for_datetime (utc_dt_p, true, &tz_id) != NO_ERROR)
		  {
		    status = DOMAIN_ERROR;
		    break;
		  }
	      }
	    else
	      {
		dt_tz_p = DB_GET_DATETIMETZ (src);
		utc_dt_p = &dt_tz_p->datetime;
		tz_id = dt_tz_p->tz_id;
	      }

	    if (tz_utc_datetimetz_to_local (utc_dt_p, &tz_id, &v_datetime) != NO_ERROR)
	      {
		status = DOMAIN_ERROR;
		break;
	      }

	    db_datetime_decode (&v_datetime, &month, &day, &year, &hour, &minute, &second, &millisecond);

	    db_make_date (target, month, day, year);
	    break;
	  }

	default:
	  status = DOMAIN_INCOMPATIBLE;
	  break;
	}
      break;

    case DB_TYPE_TIME:
      switch (original_type)
	{
	case DB_TYPE_TIMELTZ:
	  {
	    DB_TIME *time_utc_p;
	    time_utc_p = DB_GET_TIME (src);

	    if (tz_timeltz_to_local (time_utc_p, &v_time) != NO_ERROR)
	      {
		status = DOMAIN_ERROR;
		break;
	      }
	    db_value_put_encoded_time (target, &v_time);
	  }
	  break;
	case DB_TYPE_TIMETZ:
	  {
	    DB_TIMETZ *time_tz_p = DB_GET_TIMETZ (src);

	    if (tz_utc_timetz_to_local (&time_tz_p->time, &time_tz_p->tz_id, &v_time) != NO_ERROR)
	      {
		status = DOMAIN_ERROR;
		break;
	      }

	    db_value_put_encoded_time (target, &v_time);
	  }
	  break;

	case DB_TYPE_UTIME:
	case DB_TYPE_TIMESTAMPLTZ:
	  if (db_timestamp_decode_ses (DB_GET_UTIME (src), NULL, &v_time) != NO_ERROR)
	    {
	      status = DOMAIN_ERROR;
	      break;
	    }
	  db_value_put_encoded_time (target, &v_time);
	  break;

	case DB_TYPE_TIMESTAMPTZ:
	  /* convert TS from UTC to value TZ */
	  v_timestamptz = *DB_GET_TIMESTAMPTZ (src);
	  if (db_timestamp_decode_w_tz_id (&v_timestamptz.timestamp, &v_timestamptz.tz_id, NULL, &v_time) != NO_ERROR)
	    {
	      status = DOMAIN_ERROR;
	      break;
	    }
	  db_value_put_encoded_time (target, &v_time);
	  break;
	case DB_TYPE_DATETIME:
	  db_datetime_decode ((DB_DATETIME *) DB_GET_DATETIME (src), &month, &day, &year, &hour, &minute, &second,
			      &millisecond);
	  db_make_time (target, hour, minute, second);
	  break;
	case DB_TYPE_DATETIMELTZ:
	  {
	    DB_DATETIME dt_local;

	    v_datetime = *DB_GET_DATETIME (src);

	    if (tz_datetimeltz_to_local (&v_datetime, &dt_local) != NO_ERROR)
	      {
		status = DOMAIN_ERROR;
		break;
	      }

	    db_datetime_decode (&dt_local, &month, &day, &year, &hour, &minute, &second, &millisecond);
	    db_make_time (target, hour, minute, second);
	    break;
	  }
	case DB_TYPE_DATETIMETZ:
	  {
	    DB_DATETIME dt_local;

	    v_datetimetz = *DB_GET_DATETIMETZ (src);
	    if (tz_utc_datetimetz_to_local (&v_datetimetz.datetime, &v_datetimetz.tz_id, &dt_local) != NO_ERROR)
	      {
		status = DOMAIN_ERROR;
		break;
	      }
	    db_datetime_decode (&dt_local, &month, &day, &year, &hour, &minute, &second, &millisecond);
	    db_make_time (target, hour, minute, second);
	    break;
	  }
	case DB_TYPE_SHORT:
	  v_time = DB_GET_SHORT (src) % SECONDS_IN_A_DAY;
	  db_time_decode (&v_time, &hour, &minute, &second);
	  db_make_time (target, hour, minute, second);
	  break;
	case DB_TYPE_INTEGER:
	  v_time = DB_GET_INT (src) % SECONDS_IN_A_DAY;
	  db_time_decode (&v_time, &hour, &minute, &second);
	  db_make_time (target, hour, minute, second);
	  break;
	case DB_TYPE_BIGINT:
	  v_time = DB_GET_BIGINT (src) % SECONDS_IN_A_DAY;
	  db_time_decode (&v_time, &hour, &minute, &second);
	  db_make_time (target, hour, minute, second);
	  break;
	case DB_TYPE_MONETARY:
	  v_money = DB_GET_MONETARY (src);
	  if (OR_CHECK_INT_OVERFLOW (v_money->amount))
	    {
	      status = DOMAIN_OVERFLOW;
	    }
	  else
	    {
	      v_time = (int) ROUND (v_money->amount) % SECONDS_IN_A_DAY;
	      db_time_decode (&v_time, &hour, &minute, &second);
	      db_make_time (target, hour, minute, second);
	    }
	  break;
	case DB_TYPE_FLOAT:
	  {
	    float ftmp = DB_GET_FLOAT (src);
	    if (OR_CHECK_INT_OVERFLOW (ftmp))
	      {
		status = DOMAIN_OVERFLOW;
	      }
	    else
	      {
		v_time = ((int) ROUND (ftmp)) % SECONDS_IN_A_DAY;
		db_time_decode (&v_time, &hour, &minute, &second);
		db_make_time (target, hour, minute, second);
	      }
	    break;
	  }
	case DB_TYPE_DOUBLE:
	  {
	    double dtmp = DB_GET_DOUBLE (src);
	    if (OR_CHECK_INT_OVERFLOW (dtmp))
	      {
		status = DOMAIN_OVERFLOW;
	      }
	    else
	      {
		v_time = ((int) ROUND (dtmp)) % SECONDS_IN_A_DAY;
		db_time_decode (&v_time, &hour, &minute, &second);
		db_make_time (target, hour, minute, second);
	      }
	    break;
	  }
	case DB_TYPE_VARCHAR:
	case DB_TYPE_CHAR:
	case DB_TYPE_NCHAR:
	case DB_TYPE_VARNCHAR:
	  if (tp_atotime (src, &v_time) == NO_ERROR)
	    {
	      db_time_decode (&v_time, &hour, &minute, &second);
	    }
	  else
	    {
	      return DOMAIN_ERROR;
	    }

	  if (db_make_time (target, hour, minute, second) != NO_ERROR)
	    {
	      return DOMAIN_ERROR;
	    }
	  break;
	case DB_TYPE_ENUMERATION:
	  {
	    DB_VALUE varchar_val;
	    if (tp_enumeration_to_varchar (src, &varchar_val) != NO_ERROR)
	      {
		status = DOMAIN_ERROR;
		break;
	      }
	    status =
	      tp_value_cast_internal (&varchar_val, target, desired_domain, coercion_mode, do_domain_select, false);
	    break;
	  }
	default:
	  status = DOMAIN_INCOMPATIBLE;
	  break;
	}
      break;

    case DB_TYPE_TIMELTZ:
      switch (original_type)
	{
	case DB_TYPE_TIME:
	  v_time = *(DB_GET_TIME (src));

	  if (tz_check_session_has_geographic_tz () != NO_ERROR)
	    {
	      status = DOMAIN_ERROR;
	      break;
	    }

	  if (tz_create_timetz_from_ses (DB_GET_TIME (src), &v_timetz) != NO_ERROR)
	    {
	      status = DOMAIN_ERROR;
	      break;
	    }
	  db_make_timeltz (target, &v_timetz.time);
	  break;
	case DB_TYPE_TIMETZ:
	  v_timetz = *(DB_GET_TIMETZ (src));
	  assert (tz_check_geographic_tz (&v_timetz.tz_id) == NO_ERROR);
	  /* copy time value (UTC) */
	  db_make_timeltz (target, &v_timetz.time);
	  break;
	case DB_TYPE_UTIME:
	  db_timestamp_decode_utc (DB_GET_UTIME (src), NULL, &v_time);
	  db_make_timeltz (target, &v_time);
	  break;
	case DB_TYPE_TIMESTAMPLTZ:
	  db_timestamp_decode_utc (DB_GET_UTIME (src), NULL, &v_time);
	  db_make_timeltz (target, &v_time);
	  break;
	case DB_TYPE_TIMESTAMPTZ:
	  v_timestamptz = *DB_GET_TIMESTAMPTZ (src);
	  db_timestamp_decode_utc (&v_timestamptz.timestamp, NULL, &v_time);
	  db_make_timeltz (target, &v_time);
	  break;
	case DB_TYPE_DATETIME:
	  /* convert to UTC */
	  if (tz_create_datetimetz_from_ses (DB_GET_DATETIME (src), &v_datetimetz) != NO_ERROR)
	    {
	      status = DOMAIN_ERROR;
	      break;
	    }
	  db_datetime_decode (&v_datetimetz.datetime, &month, &day, &year, &hour, &minute, &second, &millisecond);
	  db_time_encode (&v_time, hour, minute, second);
	  db_make_timeltz (target, &v_time);
	  break;
	case DB_TYPE_DATETIMELTZ:
	  db_datetime_decode ((DB_DATETIME *) DB_GET_DATETIME (src), &month, &day, &year, &hour, &minute, &second,
			      &millisecond);
	  db_time_encode (&v_time, hour, minute, second);
	  db_make_timeltz (target, &v_time);
	  break;
	case DB_TYPE_DATETIMETZ:
	  /* copy time part (UTC) */
	  v_datetimetz = *DB_GET_DATETIMETZ (src);
	  db_datetime_decode (&v_datetimetz.datetime, &month, &day, &year, &hour, &minute, &second, &millisecond);
	  db_time_encode (&v_time, hour, minute, second);
	  db_make_timeltz (target, &v_time);
	  break;
	case DB_TYPE_SHORT:
	case DB_TYPE_INTEGER:
	case DB_TYPE_BIGINT:
	case DB_TYPE_MONETARY:
	case DB_TYPE_FLOAT:
	case DB_TYPE_DOUBLE:
	  status =
	    tp_value_cast_internal (src, &temp, tp_domain_resolve_default (DB_TYPE_TIME), coercion_mode,
				    do_domain_select, preserve_domain);
	  if (status != DOMAIN_COMPATIBLE)
	    {
	      break;
	    }

	  assert (DB_VALUE_TYPE (&temp) == DB_TYPE_TIME);

	  if (tz_check_session_has_geographic_tz () != NO_ERROR)
	    {
	      status = DOMAIN_ERROR;
	      break;
	    }
	  if (tz_create_timetz_from_ses (DB_GET_TIME (&temp), &v_timetz) != NO_ERROR)
	    {
	      status = DOMAIN_ERROR;
	      break;
	    }
	  db_make_timeltz (target, &v_timetz.time);
	  break;
	case DB_TYPE_VARCHAR:
	case DB_TYPE_CHAR:
	case DB_TYPE_NCHAR:
	case DB_TYPE_VARNCHAR:
	  if (tp_atotimetz (src, &v_timetz, true) == NO_ERROR)
	    {
	      db_time_decode (&v_timetz.time, &hour, &minute, &second);
	      db_time_encode (&v_time, hour, minute, second);
	    }
	  else
	    {
	      return DOMAIN_ERROR;
	    }
	  db_make_timeltz (target, &v_time);
	  break;
	case DB_TYPE_ENUMERATION:
	  {
	    DB_VALUE varchar_val;
	    if (tp_enumeration_to_varchar (src, &varchar_val) != NO_ERROR)
	      {
		status = DOMAIN_ERROR;
		break;
	      }
	    status =
	      tp_value_cast_internal (&varchar_val, target, desired_domain, coercion_mode, do_domain_select, false);
	    break;
	  }
	default:
	  status = DOMAIN_INCOMPATIBLE;
	  break;
	}
      break;

    case DB_TYPE_TIMETZ:
      switch (original_type)
	{
	case DB_TYPE_TIME:
	  if (tz_create_timetz_from_ses (DB_GET_TIME (src), &v_timetz) != NO_ERROR)
	    {
	      status = DOMAIN_ERROR;
	      break;
	    }

	  db_make_timetz (target, &v_timetz);
	  break;
	case DB_TYPE_TIMELTZ:
	  /* copy time value and store TZ of session */
	  v_timetz.time = *(DB_GET_TIME (src));
	  if (tz_create_session_tzid_for_time (&(v_timetz.time), true, &(v_timetz.tz_id)) != NO_ERROR)
	    {
	      status = DOMAIN_ERROR;
	      break;
	    }

	  /* Convert region zone into offset zone for timetz type */
	  tz_tzid_convert_region_to_offset (&v_timetz.tz_id);
	  db_make_timetz (target, &v_timetz);
	  break;
	case DB_TYPE_UTIME:
	case DB_TYPE_TIMESTAMPLTZ:
	  {
	    db_timestamp_decode_utc (DB_GET_UTIME (src), NULL, &v_time);
	    if (tz_create_session_tzid_for_time (&v_time, true, &v_timetz.tz_id) != NO_ERROR)
	      {
		status = DOMAIN_ERROR;
		break;
	      }
	    v_timetz.time = v_time;

	    /* Convert region zone into offset zone for timetz type */
	    tz_tzid_convert_region_to_offset (&v_timetz.tz_id);
	    db_make_timetz (target, &v_timetz);
	    break;
	  }
	case DB_TYPE_TIMESTAMPTZ:
	  {
	    v_timestamptz = *DB_GET_TIMESTAMPTZ (src);

	    db_timestamp_decode_utc (&v_timestamptz.timestamp, NULL, &v_time);
	    v_timetz.time = v_time;
	    v_timetz.tz_id = v_timestamptz.tz_id;

	    /* Convert region zone into offset zone for timetz type */
	    tz_tzid_convert_region_to_offset (&v_timetz.tz_id);
	    db_make_timetz (target, &v_timetz);
	    break;
	  }
	case DB_TYPE_DATETIME:
	  {
	    if (tz_create_datetimetz_from_ses (DB_GET_DATETIME (src), &v_datetimetz) != NO_ERROR)
	      {
		status = DOMAIN_ERROR;
		break;
	      }

	    db_datetime_decode (&v_datetimetz.datetime, &month, &day, &year, &hour, &minute, &second, &millisecond);
	    db_time_encode (&v_time, hour, minute, second);
	    v_timetz.time = v_time;
	    v_timetz.tz_id = v_datetimetz.tz_id;

	    /* Convert region zone into offset zone for timetz type */
	    tz_tzid_convert_region_to_offset (&v_timetz.tz_id);
	    db_make_timetz (target, &v_timetz);
	    break;
	  }
	case DB_TYPE_DATETIMELTZ:
	  {
	    db_datetime_decode ((DB_DATETIME *) DB_GET_DATETIME (src), &month, &day, &year, &hour, &minute, &second,
				&millisecond);
	    db_time_encode (&v_time, hour, minute, second);
	    if (tz_create_session_tzid_for_time (&v_time, true, &v_timetz.tz_id) != NO_ERROR)
	      {
		status = DOMAIN_ERROR;
		break;
	      }

	    /* Convert region zone into offset zone for timetz type */
	    tz_tzid_convert_region_to_offset (&v_timetz.tz_id);
	    v_timetz.time = v_time;
	    db_make_timetz (target, &v_timetz);
	    break;
	  }
	case DB_TYPE_DATETIMETZ:
	  {
	    v_datetimetz = *DB_GET_DATETIMETZ (src);
	    db_datetime_decode (&v_datetimetz.datetime, &month, &day, &year, &hour, &minute, &second, &millisecond);
	    db_time_encode (&v_time, hour, minute, second);
	    v_timetz.time = v_time;
	    v_timetz.tz_id = v_datetimetz.tz_id;

	    /* Convert region zone into offset zone for timetz type */
	    tz_tzid_convert_region_to_offset (&v_timetz.tz_id);
	    db_make_timetz (target, &v_timetz);
	    break;
	  }
	case DB_TYPE_SHORT:
	case DB_TYPE_INTEGER:
	case DB_TYPE_BIGINT:
	case DB_TYPE_MONETARY:
	case DB_TYPE_FLOAT:
	case DB_TYPE_DOUBLE:
	  status =
	    tp_value_cast_internal (src, &temp, tp_domain_resolve_default (DB_TYPE_TIME), coercion_mode,
				    do_domain_select, preserve_domain);
	  if (status != DOMAIN_COMPATIBLE)
	    {
	      break;
	    }

	  assert (DB_VALUE_TYPE (&temp) == DB_TYPE_TIME);

	  if (tz_create_timetz_from_ses (DB_GET_TIME (&temp), &v_timetz) != NO_ERROR)
	    {
	      status = DOMAIN_ERROR;
	      break;
	    }
	  db_make_timetz (target, &v_timetz);
	  break;
	case DB_TYPE_VARCHAR:
	case DB_TYPE_CHAR:
	case DB_TYPE_NCHAR:
	case DB_TYPE_VARNCHAR:
	  if (tp_atotimetz (src, &v_timetz, false) == NO_ERROR)
	    {
	      db_make_timetz (target, &v_timetz);
	    }
	  else
	    {
	      return DOMAIN_ERROR;
	    }
	  break;
	case DB_TYPE_ENUMERATION:
	  {
	    DB_VALUE varchar_val;
	    if (tp_enumeration_to_varchar (src, &varchar_val) != NO_ERROR)
	      {
		status = DOMAIN_ERROR;
		break;
	      }
	    status =
	      tp_value_cast_internal (&varchar_val, target, desired_domain, coercion_mode, do_domain_select, false);
	    break;
	  }
	default:
	  status = DOMAIN_INCOMPATIBLE;
	  break;
	}
      break;

#if !defined (SERVER_MODE)
    case DB_TYPE_OBJECT:
      {
	DB_OBJECT *v_obj = NULL;
	int is_vclass = 0;

	/* Make sure the domains are compatible.  Coerce view objects to real objects. */
	switch (original_type)
	  {
	  case DB_TYPE_OBJECT:
	    if (!sm_coerce_object_domain ((TP_DOMAIN *) desired_domain, DB_GET_OBJECT (src), &v_obj))
	      {
		status = DOMAIN_INCOMPATIBLE;
	      }
	    break;
	  case DB_TYPE_POINTER:
	    if (!sm_check_class_domain ((TP_DOMAIN *) desired_domain, ((DB_OTMPL *) DB_GET_POINTER (src))->classobj))
	      {
		status = DOMAIN_INCOMPATIBLE;
	      }
	    db_make_pointer (target, DB_GET_POINTER (src));
	    break;
	  case DB_TYPE_OID:
	    vid_oid_to_object (src, &v_obj);
	    break;

	  case DB_TYPE_VOBJ:
	    vid_vobj_to_object (src, &v_obj);
	    is_vclass = db_is_vclass (desired_domain->class_mop);
	    if (is_vclass < 0)
	      {
		return DOMAIN_ERROR;
	      }
	    if (!is_vclass)
	      {
		v_obj = db_real_instance (v_obj);
	      }
	    break;

	  default:
	    status = DOMAIN_INCOMPATIBLE;
	  }
	if (original_type != DB_TYPE_POINTER)
	  {
	    /* check we got an object in a proper class */
	    if (v_obj && desired_domain->class_mop)
	      {
		DB_OBJECT *obj_class;

		obj_class = db_get_class (v_obj);
		if (obj_class == desired_domain->class_mop)
		  {
		    /* everything is fine */
		  }
		else if (db_is_subclass (obj_class, desired_domain->class_mop) > 0)
		  {
		    /* everything is also ok */
		  }
		else
		  {
		    is_vclass = db_is_vclass (desired_domain->class_mop);
		    if (is_vclass < 0)
		      {
			return DOMAIN_ERROR;
		      }
		    if (is_vclass)
		      {
			/* 
			 * This should still be an error, and the above
			 * code should have constructed a virtual mop.
			 * I'm not sure the rest of the code is consistent
			 * in this regard.
			 */
		      }
		    else
		      {
			status = DOMAIN_INCOMPATIBLE;
		      }
		  }
	      }
	    db_make_object (target, v_obj);
	  }
      }
      break;
#endif /* !SERVER_MODE */

    case DB_TYPE_SET:
    case DB_TYPE_MULTISET:
    case DB_TYPE_SEQUENCE:
      if (!TP_IS_SET_TYPE (original_type))
	{
	  status = DOMAIN_INCOMPATIBLE;
	}
      else
	{
	  SETREF *setref;

	  setref = db_get_set (src);
	  if (setref)
	    {
	      TP_DOMAIN *set_domain;

	      set_domain = setobj_domain (setref->set);
	      if (src == dest && tp_domain_compatible (set_domain, desired_domain))
		{
		  /* 
		   * We know that this is a "coerce-in-place" operation, and
		   * we know that no coercion is necessary, so do nothing: we
		   * can use the exact same set without any conversion.
		   * Setting "src" to NULL prevents the wrapup code from
		   * clearing the set; that's important since we haven't made
		   * a copy.
		   */
		  setobj_put_domain (setref->set, (TP_DOMAIN *) desired_domain);
		  src = NULL;
		}
	      else
		{
		  if (tp_domain_compatible (set_domain, desired_domain))
		    {
		      /* 
		       * Well, we can't use the exact same set, but we don't
		       * have to do the whole hairy coerce thing either: we
		       * can just make a copy and then take the more general
		       * domain.  setobj_put_domain() guards against null
		       * pointers, there's no need to check first.
		       */
		      setref = set_copy (setref);
		      if (setref)
			{
			  setobj_put_domain (setref->set, (TP_DOMAIN *) desired_domain);
			}
		    }
		  else
		    {
		      /* 
		       * Well, now we have to use the whole hairy coercion
		       * thing.  Too bad...
		       *
		       * This case will crop up when someone tries to cast a
		       * "set of int" as a "set of float", for example.
		       */
		      setref =
			set_coerce (setref, (TP_DOMAIN *) desired_domain, (coercion_mode == TP_IMPLICIT_COERCION));
		    }

		  if (setref == NULL)
		    {
		      assert (er_errid () != NO_ERROR);
		      err = er_errid ();
		    }
		  else if (desired_type == DB_TYPE_SET)
		    {
		      err = db_make_set (target, setref);
		    }
		  else if (desired_type == DB_TYPE_MULTISET)
		    {
		      err = db_make_multiset (target, setref);
		    }
		  else
		    {
		      err = db_make_sequence (target, setref);
		    }
		}
	      if (!setref || err < 0)
		{
		  status = DOMAIN_INCOMPATIBLE;
		}
	    }
	}
      break;

    case DB_TYPE_VOBJ:
      if (original_type == DB_TYPE_VOBJ)
	{
	  SETREF *setref;
	  /* 
	   * We should try and convert the view of the src to match
	   * the view of the desired_domain. However, the desired
	   * domain generally does not contain this information.
	   * We will detect domain incompatibly later on assignment,
	   * so we treat casting any DB_TYPE_VOBJ to DB_TYPE_VOBJ
	   * as success.
	   */
	  status = DOMAIN_COMPATIBLE;
	  setref = db_get_set (src);
	  if (src != dest || !setref)
	    {
	      pr_clone_value ((DB_VALUE *) src, target);
	    }
	  else
	    {
	      /* 
	       * this is a "coerce-in-place", and no coercion is necessary,
	       * so do nothing: use the same vobj without any conversion. set
	       * "src" to NULL to prevent the wrapup code from clearing dest.
	       */
	      setobj_put_domain (setref->set, (TP_DOMAIN *) desired_domain);
	      src = NULL;
	    }
	}
      else
#if !defined (SERVER_MODE)
      if (original_type == DB_TYPE_OBJECT)
	{
	  if (vid_object_to_vobj (DB_GET_OBJECT (src), target) < 0)
	    {
	      status = DOMAIN_INCOMPATIBLE;
	    }
	  else
	    {
	      status = DOMAIN_COMPATIBLE;
	    }
	  break;
	}
      else
#endif /* !SERVER_MODE */
      if (original_type == DB_TYPE_OID || original_type == DB_TYPE_OBJECT)
	{
	  DB_VALUE view_oid;
	  DB_VALUE class_oid;
	  DB_VALUE keys;
	  OID nulloid;
	  DB_SEQ *seq;

	  OID_SET_NULL (&nulloid);
	  DB_MAKE_OID (&class_oid, &nulloid);
	  DB_MAKE_OID (&view_oid, &nulloid);
	  seq = db_seq_create (NULL, NULL, 3);
	  keys = *src;

	  /* 
	   * if we are on the server, and get a DB_TYPE_OBJECT,
	   * then its only possible representation is a DB_TYPE_OID,
	   * and it may be treated that way. However, this should
	   * not really be a case that can happen. It may still
	   * for historical reasons, so is not falgged as an error.
	   * On the client, a worskapce based scheme must be used,
	   * which is just above in a conditional compiled section.
	   */

	  if ((db_seq_put (seq, 0, &view_oid) != NO_ERROR) || (db_seq_put (seq, 1, &class_oid) != NO_ERROR)
	      || (db_seq_put (seq, 2, &keys) != NO_ERROR))
	    {
	      status = DOMAIN_INCOMPATIBLE;
	    }
	  else
	    {
	      db_make_sequence (target, seq);
	      db_value_alter_type (target, DB_TYPE_VOBJ);
	      status = DOMAIN_COMPATIBLE;
	    }
	}
      else
	{
	  status = DOMAIN_INCOMPATIBLE;
	}
      break;

    case DB_TYPE_BIT:
    case DB_TYPE_VARBIT:
      switch (original_type)
	{
	case DB_TYPE_VARCHAR:
	case DB_TYPE_CHAR:
	case DB_TYPE_NCHAR:
	case DB_TYPE_VARNCHAR:
	  {
	    DB_VALUE temp;
	    char *bit_char_string;
	    int src_size = DB_GET_STRING_SIZE (src);
	    int dst_size = (src_size + 1) / 2;

	    bit_char_string = db_private_alloc (NULL, dst_size + 1);
	    if (bit_char_string)
	      {
		if (qstr_hex_to_bin (bit_char_string, dst_size, DB_GET_STRING (src), src_size) != src_size)
		  {
		    status = DOMAIN_ERROR;
		    db_private_free_and_init (NULL, bit_char_string);
		  }
		else
		  {
		    db_make_bit (&temp, TP_FLOATING_PRECISION_VALUE, bit_char_string, src_size * 4);
		    temp.need_clear = true;
		    if (db_bit_string_coerce (&temp, target, &data_stat) != NO_ERROR)
		      {
			status = DOMAIN_INCOMPATIBLE;
		      }
		    else if (data_stat == DATA_STATUS_TRUNCATED && coercion_mode == TP_IMPLICIT_COERCION)
		      {
			status = DOMAIN_OVERFLOW;
		      }
		    else
		      {
			status = DOMAIN_COMPATIBLE;
		      }
		    pr_clear_value (&temp);
		  }
	      }
	    else
	      {
		/* Couldn't allocate space for bit_char_string */
		status = DOMAIN_INCOMPATIBLE;
	      }
	  }
	  break;

	case DB_TYPE_ENUMERATION:
	  {
	    DB_VALUE varchar_val;

	    if (tp_enumeration_to_varchar (src, &varchar_val) != NO_ERROR)
	      {
		status = DOMAIN_ERROR;
		break;
	      }
	    status =
	      tp_value_cast_internal (&varchar_val, target, desired_domain, coercion_mode, do_domain_select, false);
	  }
	  break;

	case DB_TYPE_BLOB:
	  {
	    DB_VALUE tmpval;

	    DB_MAKE_NULL (&tmpval);

	    err = db_blob_to_bit (src, NULL, &tmpval);
	    if (err == NO_ERROR)
	      {
		err = tp_value_cast_internal (&tmpval, target, desired_domain, coercion_mode, do_domain_select, false);
	      }
	    (void) pr_clear_value (&tmpval);
	  }
	  break;

	default:
	  if (src == dest && tp_can_steal_string (src, desired_domain))
	    {
	      tp_value_slam_domain (dest, desired_domain);
	      /* 
	       * Set "src" to NULL to prevent the wrapup code from undoing
	       * our work; since we haven't actually made a copy, we don't
	       * want to clear the original.
	       */
	      src = NULL;
	    }
	  else if (db_bit_string_coerce (src, target, &data_stat) != NO_ERROR)
	    {
	      status = DOMAIN_INCOMPATIBLE;
	    }
	  else if (data_stat == DATA_STATUS_TRUNCATED && coercion_mode == TP_IMPLICIT_COERCION)
	    {
	      status = DOMAIN_OVERFLOW;
	      db_value_clear (target);
	    }
	  else
	    {
	      status = DOMAIN_COMPATIBLE;
	    }
	  break;
	}
      break;

    case DB_TYPE_VARCHAR:
    case DB_TYPE_CHAR:
    case DB_TYPE_NCHAR:
    case DB_TYPE_VARNCHAR:
      switch (original_type)
	{
	case DB_TYPE_VARCHAR:
	case DB_TYPE_CHAR:
	case DB_TYPE_NCHAR:
	case DB_TYPE_VARNCHAR:
	  if (src == dest && tp_can_steal_string (src, desired_domain))
	    {
	      tp_value_slam_domain (dest, desired_domain);
	      /* 
	       * Set "src" to NULL to prevent the wrapup code from undoing
	       * our work; since we haven't actually made a copy, we don't
	       * want to clear the original.
	       */
	      src = NULL;
	    }
	  else if (db_char_string_coerce (src, target, &data_stat) != NO_ERROR)
	    {
	      status = DOMAIN_INCOMPATIBLE;
	    }
	  else if (data_stat == DATA_STATUS_TRUNCATED && coercion_mode == TP_IMPLICIT_COERCION)
	    {
	      status = DOMAIN_OVERFLOW;
	      db_value_clear (target);
	    }
	  else if (desired_domain->collation_flag != TP_DOMAIN_COLL_LEAVE)
	    {
	      db_string_put_cs_and_collation (target, TP_DOMAIN_CODESET (desired_domain),
					      TP_DOMAIN_COLLATION (desired_domain));
	      status = DOMAIN_COMPATIBLE;
	    }
	  break;

	case DB_TYPE_ENUMERATION:
	  {
	    DB_VALUE varchar_val;

	    if (desired_type == DB_TYPE_NCHAR || desired_type == DB_TYPE_VARNCHAR)
	      {
		status = DOMAIN_INCOMPATIBLE;
	      }
	    else if (tp_enumeration_to_varchar (src, &varchar_val) != NO_ERROR)
	      {
		status = DOMAIN_ERROR;
	      }
	    else
	      {
		status =
		  tp_value_cast_internal (&varchar_val, target, desired_domain, coercion_mode, do_domain_select, false);
	      }
	  }
	  break;

	case DB_TYPE_BIGINT:
	case DB_TYPE_INTEGER:
	case DB_TYPE_SMALLINT:
	  {
	    int max_size = TP_BIGINT_PRECISION + 2 + 1;
	    char *new_string;
	    DB_BIGINT num;

	    new_string = (char *) db_private_alloc (NULL, max_size);
	    if (!new_string)
	      {
		return DOMAIN_ERROR;
	      }

	    if (original_type == DB_TYPE_BIGINT)
	      {
		num = DB_GET_BIGINT (src);
	      }
	    else if (original_type == DB_TYPE_INTEGER)
	      {
		num = (DB_BIGINT) DB_GET_INT (src);
	      }
	    else		/* DB_TYPE_SHORT */
	      {
		num = (DB_BIGINT) DB_GET_SHORT (src);
	      }

	    if (tp_ltoa (num, new_string, 10))
	      {
		if (db_value_precision (target) != TP_FLOATING_PRECISION_VALUE
		    && db_value_precision (target) < (int) strlen (new_string))
		  {
		    status = DOMAIN_OVERFLOW;
		    db_private_free_and_init (NULL, new_string);
		  }
		else
		  {
		    make_desired_string_db_value (desired_type, desired_domain, new_string, target, &status,
						  &data_stat);
		  }
	      }
	    else
	      {
		status = DOMAIN_ERROR;
		db_private_free_and_init (NULL, new_string);
	      }
	  }
	  break;

	case DB_TYPE_DOUBLE:
	case DB_TYPE_FLOAT:
	  {
	    if (original_type == DB_TYPE_FLOAT)
	      {
		tp_ftoa (src, target);
	      }
	    else
	      {
		tp_dtoa (src, target);
	      }

	    if (DB_IS_NULL (target))
	      {
		if (er_errid () == ER_OUT_OF_VIRTUAL_MEMORY)
		  {
		    /* no way to report "out of memory" from tp_value_cast_internal() ?? */
		    status = DOMAIN_ERROR;
		  }
		else
		  {
		    status = DOMAIN_INCOMPATIBLE;
		  }
	      }
	    else if (DB_VALUE_PRECISION (target) != TP_FLOATING_PRECISION_VALUE
		     && (DB_GET_STRING_LENGTH (target) > DB_VALUE_PRECISION (target)))
	      {
		status = DOMAIN_OVERFLOW;
		pr_clear_value (target);
	      }
	  }
	  break;

	case DB_TYPE_NUMERIC:
	  {
	    char str_buf[NUMERIC_MAX_STRING_SIZE];
	    char *new_string;
	    int max_size;

	    numeric_db_value_print ((DB_VALUE *) src, str_buf);

	    max_size = strlen (str_buf) + 1;
	    new_string = (char *) db_private_alloc (NULL, max_size);
	    if (new_string == NULL)
	      {
		return DOMAIN_ERROR;
	      }

	    strcpy (new_string, str_buf);

	    if (db_value_precision (target) != TP_FLOATING_PRECISION_VALUE
		&& db_value_precision (target) < max_size - 1)
	      {
		status = DOMAIN_OVERFLOW;
		db_private_free_and_init (NULL, new_string);
	      }
	    else
	      {
		make_desired_string_db_value (desired_type, desired_domain, new_string, target, &status, &data_stat);
	      }
	  }
	  break;

	case DB_TYPE_MONETARY:
	  {
	    /* monetary symbol = 3 sign = 1 dot = 1 fraction digits = 2 NUL terminator = 1 */
	    int max_size = DBL_MAX_DIGITS + 3 + 1 + 1 + 2 + 1;
	    char *new_string;
	    char *p;

	    new_string = (char *) db_private_alloc (NULL, max_size);
	    if (!new_string)
	      {
		return DOMAIN_ERROR;
	      }

	    snprintf (new_string, max_size - 1, "%s%.*f", lang_currency_symbol (DB_GET_MONETARY (src)->type), 2,
		      DB_GET_MONETARY (src)->amount);
	    new_string[max_size - 1] = '\0';

	    p = new_string + strlen (new_string);
	    for (--p; p >= new_string && *p == '0'; p--)
	      {			/* remove trailing zeros */
		*p = '\0';
	      }
	    if (*p == '.')	/* remove point */
	      {
		*p = '\0';
	      }

	    if (db_value_precision (target) != TP_FLOATING_PRECISION_VALUE
		&& db_value_precision (target) < (int) strlen (new_string))
	      {
		status = DOMAIN_OVERFLOW;
		db_private_free_and_init (NULL, new_string);
	      }
	    else
	      {
		make_desired_string_db_value (desired_type, desired_domain, new_string, target, &status, &data_stat);
	      }
	  }
	  break;

	case DB_TYPE_DATE:
	case DB_TYPE_TIME:
	case DB_TYPE_TIMETZ:
	case DB_TYPE_TIMELTZ:
	case DB_TYPE_TIMESTAMP:
	case DB_TYPE_TIMESTAMPTZ:
	case DB_TYPE_TIMESTAMPLTZ:
	case DB_TYPE_DATETIME:
	case DB_TYPE_DATETIMETZ:
	case DB_TYPE_DATETIMELTZ:
	  {
	    int max_size = DATETIMETZ_BUF_SIZE;
	    char *new_string;

	    new_string = (char *) db_private_alloc (NULL, max_size);
	    if (!new_string)
	      {
		return DOMAIN_ERROR;
	      }

	    err = NO_ERROR;

	    switch (original_type)
	      {
	      case DB_TYPE_DATE:
		db_date_to_string (new_string, max_size, (DB_DATE *) DB_GET_DATE (src));
		break;
	      case DB_TYPE_TIME:
		db_time_to_string (new_string, max_size, (DB_TIME *) DB_GET_TIME (src));
		break;
	      case DB_TYPE_TIMETZ:
		v_timetz = *DB_GET_TIMETZ (src);
		db_timetz_to_string (new_string, max_size, &v_timetz.time, &v_timetz.tz_id);
		break;
	      case DB_TYPE_TIMELTZ:
		v_time = *DB_GET_TIME (src);
		err = tz_create_session_tzid_for_time (&v_time, true, &ses_tz_id);
		if (err != NO_ERROR)
		  {
		    break;
		  }

		db_timetz_to_string (new_string, max_size, &v_time, &ses_tz_id);
		break;
	      case DB_TYPE_TIMESTAMP:
		db_timestamp_to_string (new_string, max_size, (DB_TIMESTAMP *) DB_GET_TIMESTAMP (src));
		break;
	      case DB_TYPE_TIMESTAMPLTZ:
		v_utime = *DB_GET_TIMESTAMP (src);
		err = tz_create_session_tzid_for_timestamp (&v_utime, &ses_tz_id);
		if (err != NO_ERROR)
		  {
		    break;
		  }
		db_timestamptz_to_string (new_string, max_size, &v_utime, &ses_tz_id);
		break;
	      case DB_TYPE_TIMESTAMPTZ:
		v_timestamptz = *DB_GET_TIMESTAMPTZ (src);
		db_timestamptz_to_string (new_string, max_size, &v_timestamptz.timestamp, &v_timestamptz.tz_id);
		break;
	      case DB_TYPE_DATETIMELTZ:
		v_datetime = *DB_GET_DATETIME (src);
		err = tz_create_session_tzid_for_datetime (&v_datetime, true, &ses_tz_id);
		if (err != NO_ERROR)
		  {
		    break;
		  }
		db_datetimetz_to_string (new_string, max_size, &v_datetime, &ses_tz_id);
		break;
	      case DB_TYPE_DATETIMETZ:
		v_datetimetz = *DB_GET_DATETIMETZ (src);
		db_datetimetz_to_string (new_string, max_size, &v_datetimetz.datetime, &v_datetimetz.tz_id);
		break;
	      case DB_TYPE_DATETIME:
	      default:
		db_datetime_to_string (new_string, max_size, (DB_DATETIME *) DB_GET_DATETIME (src));
		break;
	      }

	    if (err != NO_ERROR)
	      {
		status = DOMAIN_ERROR;
		break;
	      }

	    if (db_value_precision (target) != TP_FLOATING_PRECISION_VALUE
		&& db_value_precision (target) < (int) strlen (new_string))
	      {
		status = DOMAIN_OVERFLOW;
		db_private_free_and_init (NULL, new_string);
	      }
	    else
	      {
		make_desired_string_db_value (desired_type, desired_domain, new_string, target, &status, &data_stat);
	      }
	  }
	  break;

	case DB_TYPE_BIT:
	case DB_TYPE_VARBIT:
	  {
	    int max_size;
	    char *new_string;
	    int convert_error;

	    max_size = ((db_get_string_length (src) + 3) / 4) + 1;
	    new_string = (char *) db_private_alloc (NULL, max_size);
	    if (!new_string)
	      {
		return DOMAIN_ERROR;
	      }

	    convert_error = bfmt_print (1 /* BIT_STRING_HEX */ , src,
					new_string, max_size);

	    if (convert_error == NO_ERROR)
	      {
		if (db_value_precision (target) != TP_FLOATING_PRECISION_VALUE
		    && (db_value_precision (target) < (int) strlen (new_string)))
		  {
		    status = DOMAIN_OVERFLOW;
		    db_private_free_and_init (NULL, new_string);
		  }
		else
		  {
		    make_desired_string_db_value (desired_type, desired_domain, new_string, target, &status,
						  &data_stat);
		  }
	      }
	    else if (convert_error == -1)
	      {
		status = DOMAIN_OVERFLOW;
		db_private_free_and_init (NULL, new_string);
	      }
	    else
	      {
		status = DOMAIN_ERROR;
		db_private_free_and_init (NULL, new_string);
	      }
	  }
	  break;

	case DB_TYPE_CLOB:
	  switch (desired_type)
	    {
	    case DB_TYPE_NCHAR:
	    case DB_TYPE_VARNCHAR:
	      status = DOMAIN_INCOMPATIBLE;
	      break;
	    default:
	      {
		DB_VALUE tmpval;
		DB_VALUE cs;

		DB_MAKE_NULL (&tmpval);
		/* convert directly from CLOB into charset of desired domain string */
		DB_MAKE_INTEGER (&cs, desired_domain->codeset);
		err = db_clob_to_char (src, &cs, &tmpval);
		if (err == NO_ERROR)
		  {
		    err =
		      tp_value_cast_internal (&tmpval, dest, desired_domain, coercion_mode, do_domain_select, false);
		  }

		pr_clear_value (&tmpval);
	      }
	      break;
	    }
	  break;
	default:
	  status = DOMAIN_INCOMPATIBLE;
	  break;
	}
      break;

    case DB_TYPE_BLOB:
      switch (original_type)
	{
	case DB_TYPE_BLOB:
	  err = db_value_clone ((DB_VALUE *) src, target);
	  break;
	case DB_TYPE_BIT:
	case DB_TYPE_VARBIT:
	  err = db_bit_to_blob (src, target);
	  break;
	case DB_TYPE_CHAR:
	case DB_TYPE_VARCHAR:
	case DB_TYPE_NCHAR:
	case DB_TYPE_VARNCHAR:
	  err = db_char_to_blob (src, target);
	  break;
	case DB_TYPE_ENUMERATION:
	  {
	    DB_VALUE varchar_val;

	    if (tp_enumeration_to_varchar (src, &varchar_val) != NO_ERROR)
	      {
		status = DOMAIN_ERROR;
		break;
	      }
	    status =
	      tp_value_cast_internal (&varchar_val, target, desired_domain, coercion_mode, do_domain_select, false);
	  }
	  break;
	default:
	  status = DOMAIN_INCOMPATIBLE;
	  break;
	}
      break;

    case DB_TYPE_CLOB:
      switch (original_type)
	{
	case DB_TYPE_CLOB:
	  err = db_value_clone ((DB_VALUE *) src, target);
	  break;
	case DB_TYPE_CHAR:
	case DB_TYPE_VARCHAR:
	  err = db_char_to_clob (src, target);
	  break;
	case DB_TYPE_ENUMERATION:
	  {
	    DB_VALUE varchar_val;
	    if (tp_enumeration_to_varchar (src, &varchar_val) != NO_ERROR)
	      {
		status = DOMAIN_ERROR;
		break;
	      }
	    status =
	      tp_value_cast_internal (&varchar_val, target, desired_domain, coercion_mode, do_domain_select, false);
	    break;
	  }
	default:
	  status = DOMAIN_INCOMPATIBLE;
	  break;
	}
      break;

    case DB_TYPE_ENUMERATION:
      {
	unsigned short val_idx = 0;
	int val_str_size = 0;
	char *val_str = NULL;
	bool exit = false, alloc_string = true;
	DB_VALUE conv_val;

	DB_MAKE_NULL (&conv_val);

	if (src->domain.general_info.is_null)
	  {
	    db_make_null (target);
	    break;
	  }

	switch (original_type)
	  {
	  case DB_TYPE_SHORT:
	    val_idx = (unsigned short) DB_GET_SHORT (src);
	    break;
	  case DB_TYPE_INTEGER:
	    if (OR_CHECK_USHRT_OVERFLOW (DB_GET_INT (src)))
	      {
		status = DOMAIN_INCOMPATIBLE;
	      }
	    else
	      {
		val_idx = (unsigned short) DB_GET_INT (src);
	      }
	    break;
	  case DB_TYPE_BIGINT:
	    if (OR_CHECK_USHRT_OVERFLOW (DB_GET_BIGINT (src)))
	      {
		status = DOMAIN_INCOMPATIBLE;
	      }
	    else
	      {
		val_idx = (unsigned short) DB_GET_BIGINT (src);
	      }
	    break;
	  case DB_TYPE_FLOAT:
	    if (OR_CHECK_USHRT_OVERFLOW (floor (DB_GET_FLOAT (src))))
	      {
		status = DOMAIN_INCOMPATIBLE;
	      }
	    else
	      {
		val_idx = (unsigned short) floor (DB_GET_FLOAT (src));
	      }
	    break;
	  case DB_TYPE_DOUBLE:
	    if (OR_CHECK_USHRT_OVERFLOW (floor (DB_GET_DOUBLE (src))))
	      {
		status = DOMAIN_INCOMPATIBLE;
	      }
	    else
	      {
		val_idx = (unsigned short) floor (DB_GET_DOUBLE (src));
	      }
	    break;
	  case DB_TYPE_NUMERIC:
	    {
	      DB_VALUE val;
	      DB_DATA_STATUS stat = DATA_STATUS_OK;
	      int err = NO_ERROR;

	      DB_MAKE_DOUBLE (&val, 0);
	      err = numeric_db_value_coerce_from_num ((DB_VALUE *) src, &val, &stat);
	      if (err != NO_ERROR)
		{
		  status = DOMAIN_ERROR;
		}
	      else
		{
		  if (OR_CHECK_USHRT_OVERFLOW (floor (DB_GET_DOUBLE (&val))))
		    {
		      status = DOMAIN_INCOMPATIBLE;
		    }
		  else
		    {
		      val_idx = (unsigned short) floor (DB_GET_DOUBLE (&val));
		    }
		}
	      break;
	    }
	  case DB_TYPE_MONETARY:
	    v_money = DB_GET_MONETARY (src);
	    if (OR_CHECK_USHRT_OVERFLOW (floor (v_money->amount)))
	      {
		status = DOMAIN_INCOMPATIBLE;
	      }
	    else
	      {
		val_idx = (unsigned short) floor (v_money->amount);
	      }
	    break;
	  case DB_TYPE_UTIME:
	  case DB_TYPE_TIMESTAMPLTZ:
	  case DB_TYPE_TIMESTAMPTZ:
	  case DB_TYPE_DATETIME:
	  case DB_TYPE_DATETIMETZ:
	  case DB_TYPE_DATETIMELTZ:
	  case DB_TYPE_DATE:
	  case DB_TYPE_TIME:
	  case DB_TYPE_TIMELTZ:
	  case DB_TYPE_TIMETZ:
	  case DB_TYPE_BIT:
	  case DB_TYPE_VARBIT:
	  case DB_TYPE_BLOB:
	  case DB_TYPE_CLOB:
	    {
	      status =
		tp_value_cast_internal (src, &conv_val, tp_domain_resolve_default (DB_TYPE_STRING), coercion_mode,
					do_domain_select, false);
	      if (status == DOMAIN_COMPATIBLE)
		{
		  val_str = DB_GET_STRING (&conv_val);
		  val_str_size = DB_GET_STRING_SIZE (&conv_val);
		}
	    }
	    break;
	  case DB_TYPE_CHAR:
	  case DB_TYPE_VARCHAR:
	    if (DB_GET_STRING_CODESET (src) != TP_DOMAIN_CODESET (desired_domain))
	      {
		DB_DATA_STATUS data_status = DATA_STATUS_OK;

		if (TP_DOMAIN_CODESET (desired_domain) == INTL_CODESET_RAW_BYTES)
		  {
		    /* avoid data truncation when converting to binary charset */
		    db_value_domain_init (&conv_val, DB_VALUE_TYPE (src), DB_GET_STRING_SIZE (src), 0);
		  }
		else
		  {
		    db_value_domain_init (&conv_val, DB_VALUE_TYPE (src), DB_VALUE_PRECISION (src), 0);
		  }

		db_string_put_cs_and_collation (&conv_val, TP_DOMAIN_CODESET (desired_domain),
						TP_DOMAIN_COLLATION (desired_domain));

		if (db_char_string_coerce (src, &conv_val, &data_status) != NO_ERROR || data_status != DATA_STATUS_OK)
		  {
		    status = DOMAIN_ERROR;
		    pr_clear_value (&conv_val);
		  }
		else
		  {
		    val_str = DB_GET_STRING (&conv_val);
		    val_str_size = DB_GET_STRING_SIZE (&conv_val);
		  }
	      }
	    else
	      {
		val_str = DB_GET_STRING (src);
		val_str_size = DB_GET_STRING_SIZE (src);
	      }
	    break;
	  case DB_TYPE_ENUMERATION:
	    if (DOM_GET_ENUM_ELEMS_COUNT (desired_domain) == 0)
	      {
		pr_clone_value (src, target);
		exit = true;
		break;
	      }
	    val_str = DB_GET_ENUM_STRING (src);
	    val_str_size = DB_GET_ENUM_STRING_SIZE (src);
	    if (val_str == NULL)
	      {
		/* src has a short value or a string value or both. We prefer to use the string value when matching
		 * against the desired domain but, if this is not set, we will use the value index */
		val_idx = DB_GET_ENUM_SHORT (src);
	      }
	    else
	      {
		if (DB_GET_ENUM_CODESET (src) != TP_DOMAIN_CODESET (desired_domain))
		  {
		    /* first convert charset of the original value to charset of destination domain */
		    DB_VALUE tmp;
		    DB_DATA_STATUS data_status = DATA_STATUS_OK;

		    /* charset conversion can handle only CHAR/VARCHAR DB_VALUEs, create a STRING value with max
		     * precision (so that no truncation occurs) from the ENUM source string */
		    DB_MAKE_VARCHAR (&tmp, DB_MAX_STRING_LENGTH, val_str, val_str_size, DB_GET_ENUM_CODESET (src),
				     DB_GET_ENUM_COLLATION (src));

		    /* initialize destination value of conversion */
		    db_value_domain_init (&conv_val, DB_TYPE_STRING, DB_MAX_STRING_LENGTH, 0);
		    db_string_put_cs_and_collation (&conv_val, TP_DOMAIN_CODESET (desired_domain),
						    TP_DOMAIN_COLLATION (desired_domain));

		    if (db_char_string_coerce (&tmp, &conv_val, &data_status) != NO_ERROR
			|| data_status != DATA_STATUS_OK)
		      {
			status = DOMAIN_ERROR;
			pr_clear_value (&conv_val);
			val_str = NULL;
			val_idx = 0;
		      }
		    else
		      {
			val_str = DB_GET_STRING (&conv_val);
			val_str_size = DB_GET_STRING_SIZE (&conv_val);
		      }
		    pr_clear_value (&tmp);
		  }
	      }
	    break;
	  default:
	    status = DOMAIN_INCOMPATIBLE;
	    break;
	  }

	if (exit)
	  {
	    break;
	  }

	if (status == DOMAIN_COMPATIBLE)
	  {
	    if (val_str != NULL)
	      {
		/* We have to search through the elements of the desired domain to find the index for val_str. */
		int i, size;
		DB_ENUM_ELEMENT *db_enum = NULL;
		int elem_count = DOM_GET_ENUM_ELEMS_COUNT (desired_domain);

		for (i = 1; i <= elem_count; i++)
		  {
		    db_enum = &DOM_GET_ENUM_ELEM (desired_domain, i);
		    size = DB_GET_ENUM_ELEM_STRING_SIZE (db_enum);

		    /* use collation from the PT_TYPE_ENUMERATION */
		    if (QSTR_COMPARE (desired_domain->collation_id, (const unsigned char *) val_str, val_str_size,
				      (const unsigned char *) DB_GET_ENUM_ELEM_STRING (db_enum), size) == 0)
		      {
			break;
		      }
		  }

		val_idx = i;
		if (i > elem_count)
		  {
		    if (val_str[0] == 0)
		      {
			/* The source value is string with length 0 and can be matched with enum "special error value"
			 * if it's not a valid ENUM value */
			DB_MAKE_ENUMERATION (target, 0, NULL, 0, TP_DOMAIN_CODESET (desired_domain),
					     TP_DOMAIN_COLLATION (desired_domain));
			break;
		      }
		    else
		      {
			status = DOMAIN_INCOMPATIBLE;
		      }
		  }
	      }
	    else
	      {
		/* We have the index, we need to get the actual string value from the desired domain */
		if (val_idx > DOM_GET_ENUM_ELEMS_COUNT (desired_domain))
		  {
		    status = DOMAIN_INCOMPATIBLE;
		  }
		else if (val_idx == 0)
		  {
		    /* ENUM Special error value */
		    DB_MAKE_ENUMERATION (target, 0, NULL, 0, TP_DOMAIN_CODESET (desired_domain),
					 TP_DOMAIN_COLLATION (desired_domain));
		    break;
		  }
		else
		  {
		    val_str_size = DB_GET_ENUM_ELEM_STRING_SIZE (&DOM_GET_ENUM_ELEM (desired_domain, val_idx));
		    val_str = DB_GET_ENUM_ELEM_STRING (&DOM_GET_ENUM_ELEM (desired_domain, val_idx));
		  }
	      }

	    if (status == DOMAIN_COMPATIBLE)
	      {
		char *enum_str;

		assert (val_str != NULL);

		if (!DB_IS_NULL (&conv_val))
		  {
		    /* if charset conversion, than use the converted value buffer to avoid an additional copy */
		    alloc_string = false;
		    conv_val.need_clear = false;
		  }

		if (alloc_string)
		  {
		    enum_str = db_private_alloc (NULL, val_str_size + 1);
		    if (enum_str == NULL)
		      {
			status = DOMAIN_ERROR;
			pr_clear_value (&conv_val);
			break;
		      }
		    else
		      {
			memcpy (enum_str, val_str, val_str_size);
			enum_str[val_str_size] = 0;
		      }
		  }
		else
		  {
		    enum_str = val_str;
		  }
		DB_MAKE_ENUMERATION (target, val_idx, enum_str, val_str_size, TP_DOMAIN_CODESET (desired_domain),
				     TP_DOMAIN_COLLATION (desired_domain));
		target->need_clear = true;
	      }
	  }
	pr_clear_value (&conv_val);
      }
      break;

    default:
      status = DOMAIN_INCOMPATIBLE;
      break;
    }

  if (err < 0)
    {
      status = DOMAIN_ERROR;
    }

  if (status != DOMAIN_COMPATIBLE)
    {
      if (src != dest)
	{
#if 0				/* TODO - */
	  db_value_clear (dest);
#endif

	  /* make sure this doesn't have any partial results */
	  if (preserve_domain)
	    {
	      db_value_domain_init (dest, desired_type, desired_domain->precision, desired_domain->scale);
	      db_value_put_null (dest);
	    }
	  else
	    {
	      db_make_null (dest);
	    }
	}
    }
  else if (src == dest)
    {
      /* coercsion successful, transfer the value if src == dest */
      db_value_clear (dest);
      *dest = temp;
    }

  return status;
}

/*
 * tp_value_cast - Coerce a value into one of another domain.
 *    return: TP_DOMAIN_STATUS
 *    src(in): src DB_VALUE
 *    dest(out): dest DB_VALUE
 *    desired_domain(in):
 *    implicit_coercion(in): flag for the coercion is implicit
 * Note:
 *    This function does select domain from desired_domain
 */
TP_DOMAIN_STATUS
tp_value_cast (const DB_VALUE * src, DB_VALUE * dest, const TP_DOMAIN * desired_domain, bool implicit_coercion)
{
  TP_COERCION_MODE mode;

  mode = (implicit_coercion ? TP_IMPLICIT_COERCION : TP_EXPLICIT_COERCION);
  return tp_value_cast_internal (src, dest, desired_domain, mode, true, false);
}

/*
 * tp_value_cast_preserve_domain - Coerce a value into one of another domain.
 *    return: TP_DOMAIN_STATUS
 *    src(in): src DB_VALUE
 *    dest(out): dest DB_VALUE
 *    desired_domain(in):
 *    implicit_coercion(in): flag for the coercion is implicit
 *    preserve_domain(in): flag to preserve dest's domain
 * Note:
 *    This function dose not change the domain type of dest to a DB_NULL_TYPE.
 */
TP_DOMAIN_STATUS
tp_value_cast_preserve_domain (const DB_VALUE * src, DB_VALUE * dest, const TP_DOMAIN * desired_domain,
			       bool implicit_coercion, bool preserve_domain)
{
  TP_COERCION_MODE mode;

  mode = (implicit_coercion ? TP_IMPLICIT_COERCION : TP_EXPLICIT_COERCION);
  return tp_value_cast_internal (src, dest, desired_domain, mode, true, true);
}

/*
 * tp_value_cast_no_domain_select - Coerce a value into one of another domain.
 *    return: TP_DOMAIN_STATUS
 *    src(in): src DB_VALUE
 *    dest(out): dest DB_VALUE
 *    desired_domain(in):
 *    implicit_coercion(in): flag for the coercion is implicit
 * Note:
 *    This function does not select domain from desired_domain
 */
TP_DOMAIN_STATUS
tp_value_cast_no_domain_select (const DB_VALUE * src, DB_VALUE * dest, const TP_DOMAIN * desired_domain,
				bool implicit_coercion)
{
  TP_COERCION_MODE mode;

  mode = (implicit_coercion ? TP_IMPLICIT_COERCION : TP_EXPLICIT_COERCION);
  return tp_value_cast_internal (src, dest, desired_domain, mode, false, false);
}

/*
 * tp_value_change_coll_and_codeset () - change the collation and codeset of a 
 *                                       value
 *   returns: cast operation result
 *   src(in): source DB_VALUE
 *   dest(in): destination DB_VALUE where src will be copied/adjusted to
 *   coll_id(in): destination collation id
 *   codeset(in): destination codeset
 */
TP_DOMAIN_STATUS
tp_value_change_coll_and_codeset (DB_VALUE * src, DB_VALUE * dest, int coll_id, int codeset)
{
  TP_DOMAIN *temp_domain;

  assert (src != NULL && dest != NULL);
  assert (TP_IS_STRING_TYPE (DB_VALUE_TYPE (src)));

  if (DB_GET_STRING_COLLATION (src) == coll_id && DB_GET_STRING_CODESET (src) == codeset)
    {
      /* early exit scenario */
      return DOMAIN_COMPATIBLE;
    }

  /* create new domain and adjust collation and codeset */
  temp_domain = tp_domain_resolve_value (src, NULL);
  if (temp_domain != NULL && temp_domain->is_cached)
    {
      temp_domain = tp_domain_copy (temp_domain, false);
    }
  if (temp_domain == NULL)
    {
      /* not exactly a relevant error code, but should serve it's purpose */
      assert (false);
      return DOMAIN_ERROR;
    }

  temp_domain->collation_id = coll_id;
  temp_domain->codeset = codeset;

  /* cache domain */
  temp_domain = tp_domain_cache (temp_domain);

  /* cast the value */
  return tp_value_cast (src, dest, temp_domain, true);
}

/*
 * VALUE COMPARISON
 */

/*
 * oidcmp - Compares two OIDs and returns a DB_ style status code.
 *    return: DB_ comparison status code
 *    oid1(in): first oid
 *    oid2(in): second oid
 * Note:
 *    The underlying oid_compare should be using these so we can avoid
 *    an extra level of indirection.
 */
static int
oidcmp (OID * oid1, OID * oid2)
{
  int status;

  status = oid_compare (oid1, oid2);
  if (status < 0)
    {
      status = DB_LT;
    }
  else if (status > 0)
    {
      status = DB_GT;
    }
  else
    {
      status = DB_EQ;
    }

  return status;
}

/*
 * tp_more_general_type - compares two type with respect to generality
 *    return: 0 if same type,
 *           <0 if type1 less general then type2,
 *           >0 otherwise
 *    type1(in): first type
 *    type2(in): second type
 */
int
tp_more_general_type (const DB_TYPE type1, const DB_TYPE type2)
{
  static int rank[DB_TYPE_LAST + 1];
  static int rank_init = 0;
  int i;

  if (type1 == type2)
    {
      return 0;
    }
  if ((unsigned) type1 > DB_TYPE_LAST)
    {
#if defined (CUBRID_DEBUG)
      printf ("tp_more_general_type: DB type 1 out of range: %d\n", type1);
#endif /* CUBRID_DEBUG */
      return 0;
    }
  if ((unsigned) type2 > DB_TYPE_LAST)
    {
#if defined (CUBRID_DEBUG)
      printf ("tp_more_general_type: DB type 2 out of range: %d\n", type2);
#endif /* CUBRID_DEBUG */
      return 0;
    }
  if (!rank_init)
    {
      /* set up rank so we can do fast table lookup */
      for (i = 0; i <= DB_TYPE_LAST; i++)
	{
	  rank[i] = 0;
	}
      for (i = 0; db_type_rank[i] < (DB_TYPE_LAST + 1); i++)
	{
	  rank[db_type_rank[i]] = i;
	}
      rank_init = 1;
    }

  return rank[type1] - rank[type2];
}

/*
 * tp_set_compare - compare two collection
 *    return: zero if equal, <0 if less, >0 if greater
 *    value1(in): first collection value
 *    value2(in): second collection value
 *    do_coercion(in): coercion flag
 *    total_order(in): total order flag
 * Note:
 *    If the total_order flag is set, it will return one of DB_LT, DB_GT, or
 *    SB_SUBSET, DB_SUPERSET, or DB_EQ, it will not return DB_UNK.
 */
int
tp_set_compare (const DB_VALUE * value1, const DB_VALUE * value2, int do_coercion, int total_order)
{
  DB_VALUE temp;
  int status, coercion;
  DB_VALUE *v1, *v2;
  DB_TYPE vtype1, vtype2;
  DB_SET *s1, *s2;

  coercion = 0;
  if (DB_IS_NULL (value1))
    {
      if (DB_IS_NULL (value2))
	{
	  status = (total_order ? DB_EQ : DB_UNK);
	}
      else
	{
	  status = (total_order ? DB_LT : DB_UNK);
	}
    }
  else if (DB_IS_NULL (value2))
    {
      status = (total_order ? DB_GT : DB_UNK);
    }
  else
    {
      v1 = (DB_VALUE *) value1;
      v2 = (DB_VALUE *) value2;

      vtype1 = DB_VALUE_DOMAIN_TYPE (v1);
      vtype2 = DB_VALUE_DOMAIN_TYPE (v2);
      if (vtype1 != DB_TYPE_SET && vtype1 != DB_TYPE_MULTISET && vtype1 != DB_TYPE_SEQUENCE)
	{
	  return DB_NE;
	}

      if (vtype2 != DB_TYPE_SET && vtype2 != DB_TYPE_MULTISET && vtype2 != DB_TYPE_SEQUENCE)
	{
	  return DB_NE;
	}

      if (vtype1 != vtype2)
	{
	  if (!do_coercion)
	    {
	      /* types are not comparable */
	      return DB_NE;
	    }
	  else
	    {
	      DB_MAKE_NULL (&temp);
	      coercion = 1;
	      if (tp_more_general_type (vtype1, vtype2) > 0)
		{
		  /* vtype1 is more general, coerce value 2 */
		  status = tp_value_coerce (v2, &temp, tp_domain_resolve_default (vtype1));
		  if (status != DOMAIN_COMPATIBLE)
		    {
		      /* 
		       * This is arguably an error condition
		       * but Not Equal is as close as we can come
		       * to reporting it.
		       */
		      pr_clear_value (&temp);
		      return DB_NE;
		    }
		  else
		    {
		      v2 = &temp;
		      vtype2 = DB_VALUE_TYPE (v2);
		    }
		}
	      else
		{
		  /* coerce value1 to value2's type */
		  status = tp_value_coerce (v1, &temp, tp_domain_resolve_default (vtype2));
		  if (status != DOMAIN_COMPATIBLE)
		    {
		      /* 
		       * This is arguably an error condition
		       * but Not Equal is as close as we can come
		       * to reporting it.
		       */
		      pr_clear_value (&temp);
		      return DB_NE;
		    }
		  else
		    {
		      v1 = &temp;
		      vtype1 = DB_VALUE_TYPE (v1);
		    }
		}
	    }
	}
      /* Here, we have two collections of the same type */

      s1 = db_get_set (v1);
      s2 = db_get_set (v2);

      /* 
       * there may ba a call for set_compare returning a total
       * ordering some day.
       */
      if (s1 && s2)
	{
	  status = set_compare (s1, s2, do_coercion);
	}
      else
	{
	  status = DB_UNK;
	}

      if (coercion)
	{
	  pr_clear_value (&temp);
	}
    }

  return status;
}

/*
 * tp_value_compare - calls tp_value_compare_with_error, but does not log error
 *    return: zero if equal, <0 if less, >0 if greater
 *    value1(in): first value
 *    value2(in): second value
 *    do_coercion(in): coercion flag
 *    total_order(in): total order flag
 */
int
tp_value_compare (const DB_VALUE * value1, const DB_VALUE * value2, int allow_coercion, int total_order)
{
  return tp_value_compare_with_error (value1, value2, allow_coercion, total_order, NULL);
}

/*
 * tp_value_compare_with_error - compares two values
 *    return: zero if equal, <0 if less, >0 if greater
 *    value1(in): first value
 *    value2(in): second value
 *    do_coercion(in): coercion flag
 *    total_order(in): total order flag
 *    can_compare(out): set if values are comparable
 * Note:
 *    There is some implicit conversion going on here, not sure if this
 *    is a good idea because it gives the impression that these have
 *    compatible domains.
 *
 *    If the total_order flag is set, it will return one of DB_LT, DB_GT, or
 *    DB_EQ, it will not return DB_UNK.  For the purposes of the total
 *    ordering, two NULL values are DB_EQ and if only one value is NULL, that
 *    value is less than the non-null value.
 *
 *    If "can_compare" is not null, in the event of incomparable values an
 *    error will be logged and the boolean that is pointed by "can_compare"
 *    will be set to false.
 */
int
tp_value_compare_with_error (const DB_VALUE * value1, const DB_VALUE * value2, int do_coercion, int total_order,
			     bool * can_compare)
{
  DB_VALUE temp1, temp2, tmp_char_conv;
  int status, coercion, char_conv;
  DB_VALUE *v1, *v2;
  DB_TYPE vtype1, vtype2;
  DB_OBJECT *mop;
  DB_IDENTIFIER *oid1, *oid2;
  bool use_collation_of_v1 = false;
  bool use_collation_of_v2 = false;

  status = DB_UNK;
  coercion = 0;
  char_conv = 0;

  if (can_compare != NULL)
    {
      *can_compare = true;
    }

  if (DB_IS_NULL (value1))
    {
      if (DB_IS_NULL (value2))
	{
	  status = (total_order ? DB_EQ : DB_UNK);
	}
      else
	{
	  status = (total_order ? DB_LT : DB_UNK);
	}
    }
  else if (DB_IS_NULL (value2))
    {
      status = (total_order ? DB_GT : DB_UNK);
    }
  else
    {
      int common_coll = -1;
      v1 = (DB_VALUE *) value1;
      v2 = (DB_VALUE *) value2;

      vtype1 = DB_VALUE_DOMAIN_TYPE (v1);
      vtype2 = DB_VALUE_DOMAIN_TYPE (v2);

      /* 
       * Hack, DB_TYPE_OID & DB_TYPE_OBJECT are logically the same domain
       * although their physical representations are different.
       * If we see a pair of those, handle it up front before we
       * fall in and try to perform coercion.  Avoid "coercion" between
       * OIDs and OBJECTs because we usually try to keep OIDs unswizzled
       * as long as possible.
       */
      if (vtype1 != vtype2)
	{
	  if (vtype1 == DB_TYPE_OBJECT)
	    {
	      if (vtype2 == DB_TYPE_OID)
		{
		  mop = db_get_object (v1);
		  oid1 = mop ? WS_OID (mop) : NULL;
		  oid2 = db_get_oid (v2);
		  if (oid1 && oid2)
		    {
		      return oidcmp (oid1, oid2);
		    }
		  else
		    {
		      return DB_UNK;
		    }
		}
	    }
	  else if (vtype2 == DB_TYPE_OBJECT)
	    {
	      if (vtype1 == DB_TYPE_OID)
		{
		  oid1 = db_get_oid (v1);
		  mop = db_get_object (v2);
		  oid2 = mop ? WS_OID (mop) : NULL;

		  if (oid1 && oid2)
		    {
		      return oidcmp (oid1, oid2);
		    }
		  else
		    {
		      return DB_UNK;
		    }
		}
	    }

	  /* 
	   * If value types aren't exact, try coercion.
	   * May need to be using the domain returned by
	   * tp_domain_resolve_value here ?
	   */
	  if (do_coercion && !ARE_COMPARABLE (vtype1, vtype2))
	    {
	      DB_MAKE_NULL (&temp1);
	      DB_MAKE_NULL (&temp2);
	      coercion = 1;

	      if (TP_IS_CHAR_TYPE (vtype1) && TP_IS_NUMERIC_TYPE (vtype2))
		{
		  /* coerce v1 to double */
		  status = tp_value_coerce (v1, &temp1, tp_domain_resolve_default (DB_TYPE_DOUBLE));
		  if (status == DOMAIN_COMPATIBLE)
		    {
		      v1 = &temp1;
		      vtype1 = DB_VALUE_TYPE (v1);

		      if (vtype2 != DB_TYPE_DOUBLE)
			{
			  status = tp_value_coerce (v2, &temp2, tp_domain_resolve_default (DB_TYPE_DOUBLE));

			  if (status == DOMAIN_COMPATIBLE)
			    {
			      v2 = &temp2;
			      vtype2 = DB_VALUE_TYPE (v2);
			    }
			}
		    }
		}
	      else if (TP_IS_NUMERIC_TYPE (vtype1) && TP_IS_CHAR_TYPE (vtype2))
		{
		  /* coerce v2 to double */
		  status = tp_value_coerce (v2, &temp2, tp_domain_resolve_default (DB_TYPE_DOUBLE));
		  if (status == DOMAIN_COMPATIBLE)
		    {
		      v2 = &temp2;
		      vtype2 = DB_VALUE_TYPE (v2);

		      if (vtype1 != DB_TYPE_DOUBLE)
			{
			  status = tp_value_coerce (v1, &temp1, tp_domain_resolve_default (DB_TYPE_DOUBLE));

			  if (status == DOMAIN_COMPATIBLE)
			    {
			      v1 = &temp1;
			      vtype1 = DB_VALUE_TYPE (v1);
			    }
			}
		    }
		}
	      else if (TP_IS_CHAR_TYPE (vtype1) && TP_IS_DATE_OR_TIME_TYPE (vtype2))
		{
		  /* vtype2 is the date or time type, coerce value 1 */
		  TP_DOMAIN *d2 = tp_domain_resolve_default (vtype2);
		  status = tp_value_coerce (v1, &temp1, d2);
		  if (status == DOMAIN_COMPATIBLE)
		    {
		      v1 = &temp1;
		      vtype1 = DB_VALUE_TYPE (v1);
		    }
		}
	      else if (TP_IS_DATE_OR_TIME_TYPE (vtype1) && TP_IS_CHAR_TYPE (vtype2))
		{
		  /* vtype1 is the date or time type, coerce value 2 */
		  TP_DOMAIN *d1 = tp_domain_resolve_default (vtype1);
		  status = tp_value_coerce (v2, &temp2, d1);
		  if (status == DOMAIN_COMPATIBLE)
		    {
		      v2 = &temp2;
		      vtype2 = DB_VALUE_TYPE (v2);
		    }
		}
	      else if (tp_more_general_type (vtype1, vtype2) > 0)
		{
		  /* vtype1 is more general, coerce value 2 */
		  TP_DOMAIN *d1 = tp_domain_resolve_default (vtype1);

		  if (TP_TYPE_HAS_COLLATION (vtype2) && TP_IS_CHAR_TYPE (vtype1))
		    {
		      /* create a new domain with type of v1 */
		      d1 = tp_domain_copy (d1, false);
		      if (TP_IS_CHAR_TYPE (vtype2))
			{
			  /* keep the codeset and collation from original value v2 */
			  d1->codeset = DB_GET_STRING_CODESET (v2);
			  d1->collation_id = DB_GET_STRING_COLLATION (v2);
			}
		      else
			{
			  /* v2 is ENUM, and is coerced to string this should happend when the other operand is a HV;
			   * in this case we remember to use collation and charset from ENUM (v2) */
			  use_collation_of_v2 = true;
			  d1->codeset = DB_GET_ENUM_CODESET (v2);
			  d1->collation_id = DB_GET_ENUM_COLLATION (v2);
			  common_coll = d1->collation_id;
			}

		      d1 = tp_domain_cache (d1);
		    }

		  status = tp_value_coerce (v2, &temp2, d1);
		  if (status != DOMAIN_COMPATIBLE)
		    {
		      /* 
		       * This is arguably an error condition
		       * but Not Equal is as close as we can come
		       * to reporting it.
		       */
		    }
		  else
		    {
		      v2 = &temp2;
		      vtype2 = DB_VALUE_TYPE (v2);
		    }
		}
	      else
		{
		  /* coerce value1 to value2's type */
		  TP_DOMAIN *d2 = tp_domain_resolve_default (vtype2);

		  if (TP_TYPE_HAS_COLLATION (vtype1) && TP_IS_CHAR_TYPE (vtype2))
		    {
		      /* create a new domain with type of v2 */
		      d2 = tp_domain_copy (d2, false);
		      if (TP_IS_CHAR_TYPE (vtype1))
			{
			  /* keep the codeset and collation from original value v1 */
			  d2->codeset = DB_GET_STRING_CODESET (v1);
			  d2->collation_id = DB_GET_STRING_COLLATION (v1);
			}
		      else
			{
			  /* v1 is ENUM, and is coerced to string this should happend when the other operand is a HV;
			   * in this case we remember to use collation and charset from ENUM (v1) */
			  use_collation_of_v1 = true;
			  d2->codeset = DB_GET_ENUM_CODESET (v1);
			  d2->collation_id = DB_GET_ENUM_COLLATION (v1);
			  common_coll = d2->collation_id;
			}

		      d2 = tp_domain_cache (d2);
		    }

		  status = tp_value_coerce (v1, &temp1, d2);
		  if (status != DOMAIN_COMPATIBLE)
		    {
		      /* 
		       * This is arguably an error condition
		       * but Not Equal is as close as we can come
		       * to reporting it.
		       */
		    }
		  else
		    {
		      v1 = &temp1;
		      vtype1 = DB_VALUE_TYPE (v1);
		    }
		}
	    }
	}

      if (!ARE_COMPARABLE (vtype1, vtype2))
	{
	  /* 
	   * Default status for mismatched types.
	   * Not correct but will be consistent.
	   */
	  if (tp_more_general_type (vtype1, vtype2) > 0)
	    {
	      status = DB_GT;
	    }
	  else
	    {
	      status = DB_LT;
	    }

	  /* set incompatibility flag */
	  if (can_compare != NULL)
	    {
	      *can_compare = false;

	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TP_CANT_COERCE, 2, pr_type_name (vtype1),
		      pr_type_name (vtype2));
	    }
	}
      else
	{
	  PR_TYPE *pr_type;

	  pr_type = PR_TYPE_FROM_ID (vtype1);
	  assert (pr_type != NULL);

	  if (pr_type)
	    {
	      /* Either both arguments are enums, or both are not. If one is enum and one is not, it means that
	       * tp_value_cast_internal failed somewhere */
	      assert ((vtype1 == DB_TYPE_ENUMERATION && vtype1 == vtype2)
		      || (vtype1 != DB_TYPE_ENUMERATION && vtype2 != DB_TYPE_ENUMERATION));

	      if (!TP_IS_CHAR_TYPE (vtype1))
		{
		  common_coll = 0;
		}
	      else if (DB_GET_STRING_COLLATION (v1) == DB_GET_STRING_COLLATION (v2))
		{
		  common_coll = DB_GET_STRING_COLLATION (v1);
		}
	      else if (TP_IS_CHAR_TYPE (vtype1) && (use_collation_of_v1 || use_collation_of_v2))
		{
		  INTL_CODESET codeset;
		  DB_DATA_STATUS data_status;
		  int error_status;

		  DB_MAKE_NULL (&tmp_char_conv);
		  char_conv = 1;

		  if (use_collation_of_v1)
		    {
		      assert (!use_collation_of_v2);
		      common_coll = DB_GET_STRING_COLLATION (v1);
		    }
		  else
		    {
		      assert (use_collation_of_v2 == true);
		      common_coll = DB_GET_STRING_COLLATION (v2);
		    }

		  codeset = lang_get_collation (common_coll)->codeset;

		  if (DB_GET_STRING_CODESET (v1) != codeset)
		    {
		      db_value_domain_init (&tmp_char_conv, vtype1, DB_VALUE_PRECISION (v1), 0);

		      db_string_put_cs_and_collation (&tmp_char_conv, codeset, common_coll);
		      error_status = db_char_string_coerce (v1, &tmp_char_conv, &data_status);

		      if (error_status != NO_ERROR)
			{
			  status = DB_UNK;
			  pr_clear_value (&tmp_char_conv);
			  if (coercion)
			    {
			      pr_clear_value (&temp1);
			      pr_clear_value (&temp2);
			    }
			  return status;
			}

		      assert (data_status == DATA_STATUS_OK);

		      v1 = &tmp_char_conv;
		    }
		  else if (DB_GET_STRING_CODESET (v2) != codeset)
		    {
		      db_value_domain_init (&tmp_char_conv, vtype2, DB_VALUE_PRECISION (v2), 0);

		      db_string_put_cs_and_collation (&tmp_char_conv, codeset, common_coll);
		      error_status = db_char_string_coerce (v2, &tmp_char_conv, &data_status);

		      if (error_status != NO_ERROR)
			{
			  pr_clear_value (&tmp_char_conv);
			  if (coercion)
			    {
			      pr_clear_value (&temp1);
			      pr_clear_value (&temp2);
			    }
			  status = DB_UNK;
			  return status;
			}

		      assert (data_status == DATA_STATUS_OK);

		      v2 = &tmp_char_conv;
		    }
		}
	      else if (TP_IS_CHAR_TYPE (vtype1) && DB_GET_STRING_CODESET (v1) == DB_GET_STRING_CODESET (v2))
		{
		  LANG_RT_COMMON_COLL (DB_GET_STRING_COLLATION (v1), DB_GET_STRING_COLLATION (v2), common_coll);
		}

	      if (common_coll == -1)
		{
		  status = DB_UNK;
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QSTR_INCOMPATIBLE_COLLATIONS, 0);
		  if (can_compare != NULL)
		    {
		      *can_compare = false;
		    }
		}
	      else
		{
		  status = (*(pr_type->cmpval)) (v1, v2, do_coercion, total_order, NULL, common_coll);
		}

	      if (status == DB_UNK)
		{
		  /* safe guard */
		  if (pr_type->id == DB_TYPE_MIDXKEY)
		    {
		      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_MR_NULL_DOMAIN, 0);
		      assert (false);
		    }
		}
	    }
	  else
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_MR_NULL_DOMAIN, 0);
	      status = DB_UNK;
	    }
	}

      if (coercion)
	{
	  pr_clear_value (&temp1);
	  pr_clear_value (&temp2);
	}
      if (char_conv)
	{
	  pr_clear_value (&tmp_char_conv);
	}
    }

  return status;
}

/*
 * tp_value_equal - compares the contents of two DB_VALUE structures and
 * determines if they are equal
 *    return: non-zero if the values are equal
 *    value1(in): first value
 *    value2(in): second value
 *    do_coercion(): coercion flag
 * Note:
 *    determines if they are equal.  This is a boolean comparison, you
 *    cannot use this for sorting.
 *
 *    This used to be fully implemented, since this got a lot more complicated
 *    with the introduction of parameterized types, and it is doubtfull that
 *    it saved much in performance anyway, it has been reimplemented to simply
 *    call tp_value_compare.  The old function is commented out below in case
 *    this causes problems.  After awhile, it can be removed.
 *
 */
int
tp_value_equal (const DB_VALUE * value1, const DB_VALUE * value2, int do_coercion)
{
  return tp_value_compare (value1, value2, do_coercion, 0) == DB_EQ;
}

/*
 * DOMAIN INFO FUNCTIONS
 */


/*
 * tp_domain_disk_size - Caluclate the disk size necessary to store a value
 * for a particular domain.
 *    return: disk size in bytes. -1 if this is a variable width domain or
 *            floating precision in fixed domain.
 *    domain(in): domain to consider
 * Note:
 *    This is here because it takes a domain handle.
 *    Since this is going to get called a lot, we might want to just add
 *    this to the TP_DOMAIN structure and calculate it internally when
 *    it is cached.
 */
int
tp_domain_disk_size (TP_DOMAIN * domain)
{
  int size;

  if (domain->type->variable_p)
    {
      return -1;
    }

  if (domain->type->data_lengthmem != NULL
      && (domain->type->id == DB_TYPE_CHAR || domain->type->id == DB_TYPE_NCHAR || domain->type->id == DB_TYPE_BIT)
      && domain->precision == TP_FLOATING_PRECISION_VALUE)
    {
      return -1;
    }

  assert (domain->precision != TP_FLOATING_PRECISION_VALUE);

  /* 
   * Use the "lengthmem" function here with a NULL pointer.  The size will
   * not be dependent on the actual value.
   * The decision of whether or not to use the lengthmem function probably
   * should be based on the value of "disksize" ?
   */
  if (domain->type->data_lengthmem != NULL)
    {
      size = (*(domain->type->data_lengthmem)) (NULL, domain, 1);
    }
  else
    {
      size = domain->type->disksize;
    }

  return size;
}


/*
 * tp_domain_memory_size - Calculates the "instance memory" size required
 * to hold a value for a particular domain.
 *    return: bytes size
 *    domain(in): domain to consider
 */
int
tp_domain_memory_size (TP_DOMAIN * domain)
{
  int size;

  if (domain->type->data_lengthmem != NULL
      && (domain->type->id == DB_TYPE_CHAR || domain->type->id == DB_TYPE_NCHAR || domain->type->id == DB_TYPE_BIT)
      && domain->precision == TP_FLOATING_PRECISION_VALUE)
    {
      return -1;
    }

  /* 
   * Use the "lengthmem" function here with a NULL pointer and a "disk"
   * flag of zero.
   * This will cause it to return the instance memory size.
   */
  if (domain->type->data_lengthmem != NULL)
    {
      size = (*(domain->type->data_lengthmem)) (NULL, domain, 0);
    }
  else
    {
      size = domain->type->size;
    }

  return size;
}

/*
 * tp_init_value_domain - initializes the domain information in a DB_VALUE to
 * correspond to the information from a TP_DOMAIN structure.
 *    return: none
 *    domain(out): domain information
 *    value(in): value to initialize
 * Note:
 *    Used primarily by the value unpacking functions.
 *    It uses the "initval" type function.  This needs to be changed
 *    to take a full domain rather than just precision/scale but the
 *    currently behavior will work for now.
 *
 *    Think about the need for "initval" all it really does is call
 *    db_value_domain_init() with the supplied arguments.
 */
void
tp_init_value_domain (TP_DOMAIN * domain, DB_VALUE * value)
{
  if (domain == NULL)
    {
      /* shouldn't happen ? */
      db_value_domain_init (value, DB_TYPE_NULL, DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE);
    }
  else
    {
      (*(domain->type->initval)) (value, domain->precision, domain->scale);
    }
}


/*
 * tp_check_value_size - check a particular variable sized value (e.g.
 * varchar, char, bit) against a destination domain.
 *    return: domain status (ok or overflow)
 *    domain(in): target domain
 *    value(in): value to be assigned
 * Note:
 *    It is assumed that basic domain compatibility has already been
 *    performed and that the supplied domain will match with what is
 *    in the value.
 *    This is used primarily for character data that is allowed to fit
 *    within a domain if the byte size is within tolerance.
 */
TP_DOMAIN_STATUS
tp_check_value_size (TP_DOMAIN * domain, DB_VALUE * value)
{
  TP_DOMAIN_STATUS status;
  int src_precision, src_length;
  DB_TYPE dbtype;
  char *src;

  status = DOMAIN_COMPATIBLE;

  /* if target domain is "floating", its always ok */
  if (domain->precision != TP_FLOATING_PRECISION_VALUE)
    {

      dbtype = TP_DOMAIN_TYPE (domain);
      switch (dbtype)
	{
	case DB_TYPE_CHAR:
	case DB_TYPE_NCHAR:
	case DB_TYPE_BIT:
	  /* 
	   * The compatibility will be determined by the precision.
	   * A floating precision is determined by the length of the string
	   * value.
	   */
	  src = DB_GET_STRING (value);
	  if (src != NULL)
	    {
	      src_precision = db_value_precision (value);
	      if (!TP_IS_BIT_TYPE (dbtype))
		{
		  src_length = db_get_string_length (value);
		  assert (src_length >= 0);
		}
	      else
		{
		  src_length = db_get_string_size (value);
		}

	      /* Check for floating precision. */
	      if (src_precision == TP_FLOATING_PRECISION_VALUE)
		{
		  src_precision = src_length;
		}

	      if (src_precision > domain->precision)
		{
		  status = DOMAIN_OVERFLOW;
		}
	    }
	  break;

	case DB_TYPE_VARCHAR:
	case DB_TYPE_VARNCHAR:
	case DB_TYPE_VARBIT:
	  /* 
	   * The compatibility of the value is always determined by the
	   * actual length of the value, not the destination precision.
	   */
	  src = DB_GET_STRING (value);
	  if (src != NULL)
	    {
	      if (!TP_IS_BIT_TYPE (dbtype))
		{
		  src_length = db_get_string_length (value);
		  assert (src_length >= 0);
		}
	      else
		{
		  src_length = db_get_string_size (value);
		}

	      /* 
	       * Work backwards from the source length into a minimum precision.
	       * This feels like it should be a nice packed utility
	       * function somewhere.
	       */
	      src_precision = src_length;

	      if (src_precision > domain->precision)
		{
		  status = DOMAIN_OVERFLOW;
		}
	    }
	  break;

	default:
	  /* 
	   * None of the other types require this form of value dependent domain
	   * precision checking.
	   */
	  break;
	}
    }

  return status;
}

#if defined(CUBRID_DEBUG)
/*
 * fprint_domain - print information of a domain
 *    return: void
 *    fp(out): FILE pointer
 *    domain(in): domain to print
 */
static void
fprint_domain (FILE * fp, TP_DOMAIN * domain)
{
  TP_DOMAIN *d;

  for (d = domain; d != NULL; d = d->next)
    {

      switch (TP_DOMAIN_TYPE (d))
	{

	case DB_TYPE_OBJECT:
	case DB_TYPE_OID:
	case DB_TYPE_SUB:
	  if (TP_DOMAIN_TYPE (d) == DB_TYPE_SUB)
	    {
	      fprintf (fp, "sub(");
	    }
#if !defined (SERVER_MODE)
	  if (d->class_mop != NULL)
	    {
	      fprintf (fp, "%s", db_get_class_name (d->class_mop));
	    }
	  else if (OID_ISNULL (&d->class_oid))
	    {
	      fprintf (fp, "object");
	    }
	  else
#endif /* !SERVER_MODE */
	    {
	      fprintf (fp, "object(%d,%d,%d)", d->class_oid.volid, d->class_oid.pageid, d->class_oid.slotid);
	    }
	  if (TP_DOMAIN_TYPE (d) == DB_TYPE_SUB)
	    {
	      fprintf (fp, ")");
	    }
	  break;

	case DB_TYPE_VARIABLE:
	  fprintf (fp, "union(");
	  fprint_domain (fp, d->setdomain);
	  fprintf (fp, ")");
	  break;

	case DB_TYPE_SET:
	  fprintf (fp, "set(");
	  fprint_domain (fp, d->setdomain);
	  fprintf (fp, ")");
	  break;
	case DB_TYPE_MULTISET:
	  fprintf (fp, "multiset(");
	  fprint_domain (fp, d->setdomain);
	  fprintf (fp, ")");
	  break;
	case DB_TYPE_SEQUENCE:
	  fprintf (fp, "sequence(");
	  fprint_domain (fp, d->setdomain);
	  fprintf (fp, ")");
	  break;

	case DB_TYPE_BIT:
	case DB_TYPE_VARBIT:
	  fprintf (fp, "%s(%d)", d->type->name, d->precision);
	  break;

	case DB_TYPE_CHAR:
	case DB_TYPE_VARCHAR:
	  fprintf (fp, "%s(%d) collate %s", d->type->name, d->precision, lang_get_collation_name (d->collation_id));
	  break;

	case DB_TYPE_NCHAR:
	case DB_TYPE_VARNCHAR:
	  fprintf (fp, "%s(%d) NATIONAL collate %s", d->type->name, d->precision,
		   lang_get_collation_name (d->collation_id));
	  break;

	case DB_TYPE_NUMERIC:
	  fprintf (fp, "%s(%d,%d)", d->type->name, d->precision, d->scale);
	  break;

	default:
	  fprintf (fp, "%s", d->type->name);
	  break;
	}

      if (d->next != NULL)
	{
	  fprintf (fp, ",");
	}
    }
}

/*
 * tp_dump_domain - fprint_domain to stdout
 *    return: void
 *    domain(in): domain to print
 */
void
tp_dump_domain (TP_DOMAIN * domain)
{
  fprint_domain (stdout, domain);
  fprintf (stdout, "\n");
}

/*
 * tp_domain_print - fprint_domain to stdout
 *    return: void
 *    domain(in): domain to print
 */
void
tp_domain_print (TP_DOMAIN * domain)
{
  fprint_domain (stdout, domain);
}

/*
 * tp_domain_fprint - fprint_domain to stdout
 *    return: void
 *    fp(out): FILE pointer
 *    domain(in): domain to print
 */
void
tp_domain_fprint (FILE * fp, TP_DOMAIN * domain)
{
  fprint_domain (fp, domain);
}
#endif

/*
 * tp_valid_indextype - check for valid index type
 *    return: 1 if type is a valid index type, 0 otherwise.
 *    type(in): a database type constant
 */
int
tp_valid_indextype (DB_TYPE type)
{
  switch (type)
    {
    case DB_TYPE_INTEGER:
    case DB_TYPE_FLOAT:
    case DB_TYPE_DOUBLE:
    case DB_TYPE_STRING:
    case DB_TYPE_OBJECT:
    case DB_TYPE_TIME:
    case DB_TYPE_TIMETZ:
    case DB_TYPE_TIMELTZ:
    case DB_TYPE_TIMESTAMP:
    case DB_TYPE_TIMESTAMPTZ:
    case DB_TYPE_TIMESTAMPLTZ:
    case DB_TYPE_DATETIME:
    case DB_TYPE_DATETIMETZ:
    case DB_TYPE_DATETIMELTZ:
    case DB_TYPE_DATE:
    case DB_TYPE_MONETARY:
    case DB_TYPE_SHORT:
    case DB_TYPE_BIGINT:
    case DB_TYPE_OID:
    case DB_TYPE_NUMERIC:
    case DB_TYPE_BIT:
    case DB_TYPE_VARBIT:
    case DB_TYPE_CHAR:
    case DB_TYPE_NCHAR:
    case DB_TYPE_VARNCHAR:
    case DB_TYPE_ENUMERATION:
      return 1;
    default:
      return 0;
    }
}


/*
 * tp_domain_references_objects - check if domain is an object domain or a
 * collection domain that might include objects.
 *    return: int (true or false)
 *    dom(in): the domain to be inspected
 */
int
tp_domain_references_objects (const TP_DOMAIN * dom)
{
  switch (TP_DOMAIN_TYPE (dom))
    {
    case DB_TYPE_OBJECT:
    case DB_TYPE_OID:
    case DB_TYPE_VOBJ:
      return true;
    case DB_TYPE_SET:
    case DB_TYPE_MULTISET:
    case DB_TYPE_SEQUENCE:
      dom = dom->setdomain;
      if (dom)
	{
	  /* 
	   * If domains are specified, we can assume that the upper levels
	   * have enforced the rule that no value in the collection has a
	   * domain that isn't included in this list.  If this list has no
	   * object domain, then no collection with this domain can include
	   * an object reference.
	   */
	  for (; dom; dom = dom->next)
	    {
	      if (tp_domain_references_objects (dom))
		{
		  return true;
		}
	    }

	  return false;
	}
      else
	{
	  /* 
	   * We've got hold of one of our fabulous "collection of anything"
	   * attributes.  We've got no choice but to assume that it might
	   * have objects in it.
	   */
	  return true;
	}
    default:
      return false;
    }
}

/*
 * tp_value_auto_cast - Cast a value into one of another domain, returning an
 *			error only
 *    return: TP_DOMAIN_STATUS
 *    src(in): src DB_VALUE
 *    dest(out): dest DB_VALUE
 *    desired_domain(in): destion domain
 *
 *  Note : this function is used at execution stage, by operators performing
 *	   late binding on Host Variables; the automatic cast is a replacement
 *	   for implicit cast performed at type-checking for operators that do
 *	   not require late binding.
 */
TP_DOMAIN_STATUS
tp_value_auto_cast (const DB_VALUE * src, DB_VALUE * dest, const TP_DOMAIN * desired_domain)
{
  TP_DOMAIN_STATUS status;

  status = tp_value_cast (src, dest, desired_domain, false);
  if (status != DOMAIN_COMPATIBLE)
    {
      if (prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS) == true)
	{
	  status = DOMAIN_COMPATIBLE;
	  pr_clear_value (dest);
	  DB_MAKE_NULL (dest);
	  er_clear ();
	}
    }

  return status;
}

/*
 * tp_value_str_auto_cast_to_number () - checks if the original value
 *	  is of type string, and cast it to a DOUBLE type domain.
 *   return: error code.
 *   src(in): source DB_VALUE
 *   dest(out): destination DB_VALUE
 *   val_type(in/out): db type of value; modified if the cast is performed
 *
 *  Note : this is a helper function used by arithmetic functions to accept
 *	   string arguments.
 */
int
tp_value_str_auto_cast_to_number (DB_VALUE * src, DB_VALUE * dest, DB_TYPE * val_type)
{
  TP_DOMAIN *cast_dom = NULL;
  TP_DOMAIN_STATUS dom_status;
  int er_status = NO_ERROR;

  assert (src != NULL);
  assert (dest != NULL);
  assert (val_type != NULL);
  assert (TP_IS_CHAR_TYPE (*val_type));
  assert (src != dest);

  DB_MAKE_NULL (dest);

  /* cast string to DOUBLE */
  cast_dom = tp_domain_resolve_default (DB_TYPE_DOUBLE);
  if (cast_dom == NULL)
    {
      return ER_FAILED;
    }

  dom_status = tp_value_auto_cast (src, dest, cast_dom);
  if (dom_status != DOMAIN_COMPATIBLE)
    {
      er_status = tp_domain_status_er_set (dom_status, ARG_FILE_LINE, src, cast_dom);

      pr_clear_value (dest);
      return er_status;
    }

  *val_type = DB_VALUE_DOMAIN_TYPE (dest);

  return NO_ERROR;
}

/*
 * tp_infer_common_domain () -
 *   return:
 *
 *   arg1(in):
 *   arg2(in):
 *
 *  Note :
 */
TP_DOMAIN *
tp_infer_common_domain (TP_DOMAIN * arg1, TP_DOMAIN * arg2)
{
  TP_DOMAIN *target_domain;
  DB_TYPE arg1_type, arg2_type, common_type;
  bool need_to_domain_update = false;

  assert (arg1 && arg2);

  arg1_type = arg1->type->id;
  arg2_type = arg2->type->id;

  if (arg1_type == arg2_type)
    {
      common_type = arg1_type;
      target_domain = tp_domain_copy (arg1, false);
      need_to_domain_update = true;
    }
  else if (arg1_type == DB_TYPE_NULL)
    {
      common_type = arg2_type;
      target_domain = tp_domain_copy (arg2, false);
    }
  else if (arg2_type == DB_TYPE_NULL)
    {
      common_type = arg1_type;
      target_domain = tp_domain_copy (arg1, false);
    }
  else if ((TP_IS_BIT_TYPE (arg1_type) && TP_IS_BIT_TYPE (arg2_type))
	   || (TP_IS_CHAR_TYPE (arg1_type) && TP_IS_CHAR_TYPE (arg2_type)) || (TP_IS_DATE_TYPE (arg1_type)
									       && TP_IS_DATE_TYPE (arg2_type))
	   || (TP_IS_SET_TYPE (arg1_type) && TP_IS_SET_TYPE (arg2_type)) || (TP_IS_NUMERIC_TYPE (arg1_type)
									     && TP_IS_NUMERIC_TYPE (arg2_type)))
    {
      if (tp_more_general_type (arg1_type, arg2_type) > 0)
	{
	  common_type = arg1_type;
	  target_domain = tp_domain_copy (arg1, false);
	}
      else
	{
	  common_type = arg2_type;
	  target_domain = tp_domain_copy (arg2, false);
	}
      need_to_domain_update = true;
    }
  else
    {
      common_type = DB_TYPE_VARCHAR;
      target_domain = db_type_to_db_domain (common_type);
    }

  if (need_to_domain_update)
    {
      int arg1_prec, arg2_prec, arg1_scale, arg2_scale;

      arg1_prec = arg1->precision;
      arg1_scale = arg1->scale;

      arg2_prec = arg2->precision;
      arg2_scale = arg2->scale;

      if (arg1_prec == TP_FLOATING_PRECISION_VALUE || arg2_prec == TP_FLOATING_PRECISION_VALUE)
	{
	  target_domain->precision = TP_FLOATING_PRECISION_VALUE;
	  target_domain->scale = 0;
	}
      else if (common_type == DB_TYPE_NUMERIC)
	{
	  int integral_digits1, integral_digits2;

	  integral_digits1 = arg1_prec - arg1_scale;
	  integral_digits2 = arg2_prec - arg2_scale;
	  target_domain->scale = MAX (arg1_scale, arg2_scale);
	  target_domain->precision = (target_domain->scale + MAX (integral_digits1, integral_digits2));
	  target_domain->precision = MIN (target_domain->precision, DB_MAX_NUMERIC_PRECISION);
	}
      else
	{
	  target_domain->precision = MAX (arg1_prec, arg2_prec);
	  target_domain->scale = 0;
	}
    }

  target_domain = tp_domain_cache (target_domain);
  return target_domain;
}

/*
 * tp_domain_status_er_set () -
 *   return:
 *
 *  Note :
 */
int
tp_domain_status_er_set (TP_DOMAIN_STATUS status, const char *file_name, const int line_no, const DB_VALUE * src,
			 const TP_DOMAIN * domain)
{
  int error = NO_ERROR;

  assert (src != NULL);
  assert (domain != NULL);

  /* prefer to change error code; hide internal errors from users view */
  if (status == DOMAIN_ERROR)
    {
#if 0				/* TODO */
      assert (er_errid () != NO_ERROR);
#endif
      error = er_errid ();

      if (error == ER_IT_DATA_OVERFLOW)
	{
	  status = DOMAIN_OVERFLOW;
	}
      else
	{
	  status = DOMAIN_INCOMPATIBLE;
	}
    }

  assert (status != DOMAIN_ERROR);

  switch (status)
    {
    case DOMAIN_INCOMPATIBLE:
      error = ER_TP_CANT_COERCE;
      er_set (ER_ERROR_SEVERITY, file_name, line_no, error, 2, pr_type_name (DB_VALUE_DOMAIN_TYPE (src)),
	      pr_type_name (TP_DOMAIN_TYPE (domain)));
      break;

    case DOMAIN_OVERFLOW:
      error = ER_IT_DATA_OVERFLOW;
      er_set (ER_ERROR_SEVERITY, file_name, line_no, error, 1, pr_type_name (TP_DOMAIN_TYPE (domain)));
      break;

    case DOMAIN_ERROR:
      assert (false);		/* is impossible */
      break;

    default:
      assert (false);		/* is impossible */
      break;
    }

  return error;
}

/*
 * tp_digit_number_str_to_bi - Coerce a digit number string to a bigint.
 *    return: NO_ERROR or error code
 *    start(in): string start pos
 *    end(in): string end pos
 *    codeset(in):
 *    is_negative(in):
 *    num_value(out): bigint container
 *    data_stat(out): if overflow is detected, this is set to
 *                    DATA_STATUS_TRUNCATED
 */
int
tp_digit_number_str_to_bi (char *start, char *end, INTL_CODESET codeset, bool is_negative, DB_BIGINT * num_value,
			   DB_DATA_STATUS * data_stat)
{
  char str[64] = { 0 };
  char *p = NULL;
  char *strp = NULL, *stre = NULL;
  size_t n_digits = 0;
  DB_BIGINT bigint = 0;
  bool round = false;
  bool truncated = false;

  assert (start != NULL && end != NULL && num_value != NULL && data_stat != NULL);

  strp = start;
  stre = end;

  /* count number of significant digits */
  p = strp;

  while (p != stre && char_isdigit (*p))
    {
      p++;
    }

  n_digits = p - strp;
  if (n_digits > 62)
    {
      /* more than 62 significant digits in the input number (63 chars with sign) */
      truncated = true;
    }

  /* skip decimal point and the digits after, keep the round flag */
  if (p != stre && *p == '.')
    {
      p++;

      if (p != stre)
	{
	  if (char_isdigit (*p))
	    {
	      if (*p >= '5')
		{
		  round = true;
		}

	      /* skip all digits after decimal point */
	      do
		{
		  p++;
		}
	      while (p != stre && char_isdigit (*p));
	    }
	}
    }

  /* skip trailing whitespace characters */
  p = (char *) intl_skip_spaces (p, stre, codeset);

  if (p != stre)
    {
      /* trailing characters in string */
      return ER_FAILED;
    }

  if (truncated)
    {
      *data_stat = DATA_STATUS_TRUNCATED;
      if (is_negative)
	{
	  bigint = DB_BIGINT_MIN;
	}
      else
	{
	  bigint = DB_BIGINT_MAX;
	}
    }
  else
    {
      /* Copy the number to str, excluding leading spaces and '0's and trailing spaces. Anything other than leading and 
       * trailing spaces already resulted in an error. */
      if (is_negative)
	{
	  str[0] = '-';
	  strncpy (str + 1, strp, n_digits);
	  str[n_digits + 1] = '\0';
	  strp = str;
	}
      else
	{
	  strp = strncpy (str, strp, n_digits);
	  str[n_digits] = '\0';
	}

      errno = 0;
      bigint = strtoll (strp, &p, 10);

      if (errno == ERANGE)
	{
	  *data_stat = DATA_STATUS_TRUNCATED;
	}
      else
	{
	  *data_stat = DATA_STATUS_OK;
	}

      /* round number if a '5' or greater digit was found after the decimal point */
      if (round)
	{
	  if (is_negative)
	    {
	      if (bigint > DB_BIGINT_MIN)
		{
		  bigint--;
		}
	      else
		{
		  *data_stat = DATA_STATUS_TRUNCATED;
		}
	    }
	  else
	    {
	      if (bigint < DB_BIGINT_MAX)
		{
		  bigint++;
		}
	      else
		{
		  *data_stat = DATA_STATUS_TRUNCATED;
		}
	    }
	}
    }

  *num_value = bigint;

  return NO_ERROR;
}

/*
 * tp_hex_str_to_bi - Coerce a hex string to a bigint.
 *    return: NO_ERROR or error code
 *    start(in): string start pos
 *    end(in): string end pos
 *    codeset(in):
 *    is_negative(in):
 *    num_value(out): bigint container
 *    data_stat(out): if overflow is detected, this is set to
 *                    DATA_STATUS_TRUNCATED
 */
int
tp_hex_str_to_bi (char *start, char *end, INTL_CODESET codeset, bool is_negative, DB_BIGINT * num_value,
		  DB_DATA_STATUS * data_stat)
{
#define HIGHEST_4BITS_OF_UBI 0xF000000000000000

  int error = NO_ERROR;
  char *p = NULL;
  size_t n_digits = 0;
  DB_BIGINT bigint = 0;
  UINT64 ubi = 0;
  unsigned int tmp_ui = 0;
  bool round = false;
  bool truncated = false;

  assert (start != NULL && end != NULL && num_value != NULL && data_stat != NULL);

  *data_stat = DATA_STATUS_OK;

  /* convert */
  p = start;
  while (p != end)
    {
      /* convert chars one by one */
      if (char_isdigit (*p))
	{
	  tmp_ui = *p - '0';
	}
      else if (*p >= 'a' && *p <= 'f')
	{
	  tmp_ui = *p - 'a' + 10;
	}
      else if (*p >= 'A' && *p <= 'F')
	{
	  tmp_ui = *p - 'A' + 10;
	}
      else if (*p == '.')
	{
	  if (++p != end)
	    {
	      if (char_isxdigit (*p))
		{
		  if (*p >= '0' && *p < '8')
		    {
		      round = false;
		    }
		  else
		    {
		      round = true;
		    }

		  /* check the rest chars */
		  while (++p != end && char_isxdigit (*p))
		    ;

		  /* skip trailing whitespace characters */
		  p = (char *) intl_skip_spaces (p, end, codeset);
		  if (p != end)
		    {
		      error = ER_FAILED;
		      goto end;
		    }
		}
	      else
		{
		  /* skip trailing whitespace characters */
		  p = (char *) intl_skip_spaces (p, end, codeset);
		  if (p != end)
		    {
		      error = ER_FAILED;
		      goto end;
		    }
		}
	    }

	  break;
	}
      else
	{
	  /* skip trailing whitespace characters */
	  p = (char *) intl_skip_spaces (p, end, codeset);
	  if (p != end)
	    {
	      error = ER_FAILED;
	      goto end;
	    }

	  break;
	}

      if (ubi & HIGHEST_4BITS_OF_UBI)
	{
	  truncated = true;
	  break;
	}

      ubi = ubi << 4;

      /* never overflow */
      ubi += tmp_ui;

      ++p;
    }

  *num_value = tp_ubi_to_bi_with_args (ubi, is_negative, truncated, round, data_stat);

end:

  return error;

#undef HIGHEST_4BITS_OF_UBI
}

/*
 * tp_scientific_str_to_bi - Coerce a scientific string to a bigint.
 *    return: NO_ERROR or error code
 *    start(in): string start pos
 *    end(in): string end pos
 *    codeset(in):
 *    is_negative(in):
 *    num_value(out): bigint container
 *    data_stat(out): if overflow is detected, this is set to
 *                    DATA_STATUS_TRUNCATED
 *
 *    NOTE: format check has been done by caller already
 *          see tp_atobi
 */
int
tp_scientific_str_to_bi (char *start, char *end, INTL_CODESET codeset, bool is_negative, DB_BIGINT * num_value,
			 DB_DATA_STATUS * data_stat)
{
  int error = NO_ERROR;
  double d = 0.0;
  DB_BIGINT bigint = 0;
  UINT64 ubi = 0;
  bool truncated = false;
  bool round = false;
  char *p = NULL;
  char *base_int_start = NULL, *base_int_end = NULL;
  char *base_float_start = NULL, *base_float_end = NULL;
  char *exp_start = NULL, *exp_end = NULL;
  bool is_exp_negative = false;
  int exp = 0;			/* at most 308 */

  assert (start != NULL && end != NULL && num_value != NULL && data_stat != NULL);

  *data_stat = DATA_STATUS_OK;

  base_int_start = start;
  p = base_int_start;
  while (p != end && char_isdigit (*p))
    {
      ++p;
    }

  base_int_end = p;

  /* no int part */
  if (base_int_start == base_int_end)
    {
      base_int_start = NULL;
      base_int_end = NULL;
    }

  if (p != end && *p == '.')
    {
      ++p;
      if (p != end)
	{
	  base_float_start = p;
	  while (p != end && char_isdigit (*p))
	    {
	      ++p;
	    }

	  base_float_end = p;

	  /* no float part */
	  if (base_float_start == base_float_end)
	    {
	      base_float_start = NULL;
	      base_float_end = NULL;
	    }
	}
    }

  /* this is an error */
  if (base_int_start == NULL && base_float_start == NULL)
    {
      error = ER_FAILED;
      goto end;
    }

  if (p != end && (*p == 'e' || *p == 'E'))
    {
      ++p;
      if (p != end)
	{
	  if (*p == '-')
	    {
	      is_exp_negative = true;
	      ++p;
	    }
	  else if (*p == '+')
	    {
	      ++p;
	    }

	  exp_start = p;
	  while (p != end && char_isdigit (*p))
	    {
	      ++p;
	    }

	  exp_end = p;

	  /* no exp part */
	  if (exp_start == exp_end)
	    {
	      error = ER_FAILED;
	      goto end;
	    }
	}
    }

  if (exp_start == NULL)
    {
      error = ER_FAILED;
      goto end;
    }

  /* start to calculate */

  /* exponent */
  p = exp_start;
  while (p != exp_end)
    {
      exp = exp * 10 + (*p - '0');
      if (exp > 308)
	{
	  error = ER_FAILED;
	  goto end;
	}

      ++p;
    }

  if (is_exp_negative)
    {
      exp = -exp;
    }

  /* calculate int part */
  if (base_int_start != NULL)
    {
      assert (base_int_end != NULL);

      if (exp < 0)
	{
	  if (base_int_end - base_int_start >= -exp)
	    {
	      base_int_end += exp;
	    }
	  else
	    {
	      base_int_end = base_int_start - 1;
	    }
	}

      p = base_int_start;
      while (p < base_int_end)
	{
	  ubi = tp_ubi_times_ten (ubi, &truncated);
	  if (truncated)
	    {
	      break;
	    }

	  /* never overflow */
	  ubi = ubi + *p - '0';

	  ++p;
	}

      /* need round ? */
      if (exp < 0 && base_int_end >= base_int_start && *base_int_end >= '5')
	{
	  round = true;
	}
    }

  /* calculate float part */
  if (!truncated)
    {
      if (exp > 0)
	{
	  if (base_float_start != NULL)
	    {
	      assert (base_float_end != NULL);

	      p = base_float_start;
	      while (p != base_float_end && exp > 0)
		{
		  ubi = tp_ubi_times_ten (ubi, &truncated);
		  if (truncated)
		    {
		      break;
		    }

		  /* never overflow */
		  ubi = ubi + *p - '0';

		  ++p;
		  --exp;
		}

	      /* need round ? */
	      if (p != base_float_end && *p >= '5')
		{
		  round = true;
		}
	    }

	  /* exp */
	  if (!truncated)
	    {
	      while (exp > 0)
		{
		  ubi = tp_ubi_times_ten (ubi, &truncated);
		  if (truncated)
		    {
		      break;
		    }

		  --exp;
		}
	    }
	}
      else if (exp == 0 && base_float_start != NULL && *base_float_start >= '5')
	{
	  round = true;
	}
    }


  *num_value = tp_ubi_to_bi_with_args (ubi, is_negative, truncated, round, data_stat);

end:

  return error;
}

/*
 * tp_ubi_to_bi_with_args -
 *    return: bigint
 *    ubi(in): unsigned bigint
 *    is_negative(in):
 *    truncated(in):
 *    round(in):
 *    data_stat(out): if overflow is detected, this is set to
 *                    DATA_STATUS_TRUNCATED
 *
 *    NOTE: This is an internal function for convert string to bigint
 */
DB_BIGINT
tp_ubi_to_bi_with_args (UINT64 ubi, bool is_negative, bool truncated, bool round, DB_DATA_STATUS * data_stat)
{
#define HIGHEST_BIT_OF_UINT64 0x8000000000000000

  DB_BIGINT bigint = 0;

  assert (data_stat != NULL);

  if (!truncated)
    {
      if (is_negative)
	{
	  if (ubi == HIGHEST_BIT_OF_UINT64)
	    {
	      bigint = DB_BIGINT_MIN;
	    }
	  else if (ubi & HIGHEST_BIT_OF_UINT64)
	    {
	      truncated = true;
	    }
	  else
	    {
	      bigint = (DB_BIGINT) ubi;
	      bigint = -bigint;
	    }
	}
      else
	{
	  if (ubi & HIGHEST_BIT_OF_UINT64)
	    {
	      truncated = true;
	    }
	  else
	    {
	      bigint = (DB_BIGINT) ubi;
	    }
	}
    }

  if (truncated)
    {
      *data_stat = DATA_STATUS_TRUNCATED;
      if (is_negative)
	{
	  bigint = DB_BIGINT_MIN;
	}
      else
	{
	  bigint = DB_BIGINT_MAX;
	}
    }
  else if (round)
    {
      if (is_negative)
	{
	  if (bigint > DB_BIGINT_MIN)
	    {
	      --bigint;
	    }
	  else
	    {
	      *data_stat = DATA_STATUS_TRUNCATED;
	      bigint = DB_BIGINT_MIN;
	    }
	}
      else
	{
	  if (bigint < DB_BIGINT_MAX)
	    {
	      ++bigint;
	    }
	  else
	    {
	      *data_stat = DATA_STATUS_TRUNCATED;
	      bigint = DB_BIGINT_MAX;
	    }
	}
    }

  return bigint;

#undef HIGHEST_BIT_OF_UINT64
}

/*
 * tp_ubi_times_ten -
 *    return: bigint
 *    ubi(in): unsigned bigint
 *    truncated(in/out): set to true if truncated
 *
 */
UINT64
tp_ubi_times_ten (UINT64 ubi, bool * truncated)
{
#define HIGHEST_3BITS_OF_UBI 0xE000000000000000

  UINT64 tmp_ubi = 0;

  assert (truncated != NULL);

  if (ubi & HIGHEST_3BITS_OF_UBI)
    {
      *truncated = true;

      goto end;
    }

  /* ubi*10 = ubi*8 + ubi*2 = ubi<<3 + ubi<<1 */
  tmp_ubi = ubi << 3;
  ubi = (ubi << 1) + tmp_ubi;
  if (ubi < tmp_ubi)
    {
      *truncated = true;

      goto end;
    }

end:

  return ubi;

#undef HIGHEST_3BITS_OF_UBI
}
