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




#ifndef _DBVAL_H_
#define _DBVAL_H_

#ident "$Id$"

#ifndef _DBTYPE_H_
#error "It looks like dbval.h is included before dbtype.h; don't do that."
#endif

#include "language_support.h"
#include "system_parameter.h"
#include "object_domain.h"

#undef DB_IS_NULL
#undef DB_VALUE_DOMAIN_TYPE
#undef DB_VALUE_TYPE
#undef DB_VALUE_SCALE
#undef DB_VALUE_PRECISION
#undef DB_GET_INTEGER
#undef DB_GET_FLOAT
#undef DB_GET_DOUBLE
#undef DB_GET_STRING
#undef DB_GET_CHAR
#undef DB_GET_NCHAR
#undef DB_GET_BIT
#undef DB_GET_OBJECT
#undef DB_GET_OID
#undef DB_GET_SET
#undef DB_GET_MULTISET
#undef DB_GET_LIST
#undef DB_GET_MIDXKEY
#undef DB_GET_ELO
#undef DB_GET_TIME
#undef DB_GET_DATE
#undef DB_GET_TIMESTAMP
#undef DB_GET_MONETARY
#undef DB_GET_POINTER
#undef DB_GET_ERROR
#undef DB_GET_SHORT
#undef DB_GET_SMALLINT
#undef DB_GET_NUMERIC
#undef DB_GET_STRING_SIZE
#undef DB_GET_RESULTSET
#undef DB_GET_STRING_CODESET


#define DB_IS_NULL(v) \
    (((v) && (v)->domain.general_info.is_null == 0) ? false : true)

#define DB_VALUE_DOMAIN_TYPE(v)	\
    ((DB_TYPE) ((v)->domain.general_info.type))

#define DB_VALUE_TYPE(v) \
    ((DB_TYPE) (((v)->domain.general_info.is_null) ? \
     DB_TYPE_NULL : (v)->domain.general_info.type))

#define DB_VALUE_SCALE(v) \
    (((v)->domain.general_info.type == DB_TYPE_NUMERIC) ? \
                (v)->domain.numeric_info.scale : 0)

#define DB_VALUE_PRECISION(v) \
    (((v)->domain.general_info.type == DB_TYPE_NUMERIC) ? \
      ((v)->domain.numeric_info.precision) : \
      (((v)->domain.general_info.type == DB_TYPE_BIT || \
        (v)->domain.general_info.type == DB_TYPE_VARBIT || \
        (v)->domain.general_info.type == DB_TYPE_CHAR || \
        (v)->domain.general_info.type == DB_TYPE_VARCHAR || \
        (v)->domain.general_info.type == DB_TYPE_NCHAR || \
        (v)->domain.general_info.type == DB_TYPE_VARNCHAR) ? \
        ((v)->domain.char_info.length) : 0))

#define DB_GET_INTEGER(v) \
    ((v)->data.i)

#define DB_GET_FLOAT(v) \
    ((v)->data.f)

#define DB_GET_DOUBLE(v) \
    ((v)->data.d)

/* note : this will have to change when we start using the small and large
          string buffers. */
#define DB_GET_STRING(v) \
    (((v)->domain.general_info.is_null || \
      (v)->domain.general_info.type == DB_TYPE_ERROR) ? \
                          NULL : (v)->data.ch.medium.buf)

#define DB_GET_STRING_PRECISION(v) \
    ((v)->domain.char_info.length)

#define DB_GET_BIT_PRECISION(v) \
    ((v)->domain.char_info.length)



/* nore : this will have to change when we start using the small and large
          string buffers. */
#define DB_GET_CHAR(v, l) \
    (((v)->domain.general_info.is_null || \
      (v)->domain.general_info.type == DB_TYPE_ERROR) ? NULL : \
    (intl_char_count((unsigned char *)(v)->data.ch.medium.buf, \
         (v)->data.ch.medium.size, (INTL_CODESET) (v)->data.ch.medium.codeset, (l)), \
    (v)->data.ch.medium.buf))


#define DB_GET_NCHAR(v, l) DB_GET_CHAR(v, l)



/* note: this will have to change when we start using the small and large
         string buffers. */
#define DB_GET_BIT(v, l) \
    (((v)->domain.general_info.is_null || \
      (v)->domain.general_info.type == DB_TYPE_ERROR) ? NULL : \
      (((*(l)) = (v)->data.ch.medium.size), (v)->data.ch.medium.buf))

#define DB_GET_OBJECT(v) \
    (((v)->domain.general_info.is_null || \
      (v)->domain.general_info.type == DB_TYPE_ERROR) ? (DB_OBJECT *)(NULL) : (v)->data.op)

#define DB_GET_OID(v) \
    (((v)->domain.general_info.is_null || \
      (v)->domain.general_info.type == DB_TYPE_ERROR) ? (OID *)(NULL) : &((v)->data.oid))

#define DB_GET_SET(v) \
    (((v)->domain.general_info.is_null || \
      (v)->domain.general_info.type == DB_TYPE_ERROR) ? NULL : (v)->data.set)

#define DB_GET_MULTISET(v) DB_GET_SET(v)

#define DB_GET_LIST(v) DB_GET_SET(v)

#define DB_GET_MIDXKEY(v) \
    (((v)->domain.general_info.is_null || \
      (v)->domain.general_info.type == DB_TYPE_ERROR) ? \
                  NULL : (DB_MIDXKEY *)(&(v)->data.midxkey))

#define DB_GET_ELO(v) \
    (((v)->domain.general_info.is_null || \
      (v)->domain.general_info.type == DB_TYPE_ERROR) ? NULL : (v)->data.elo)

#define DB_GET_TIME(v) \
    ((DB_TIME *)(&(v)->data.time))

#define DB_GET_DATE(v) \
    ((DB_DATE *)(&(v)->data.date))

#define DB_GET_TIMESTAMP(v) \
    ((DB_TIMESTAMP *)(&(v)->data.utime))

#define DB_GET_MONETARY(v) \
    ((DB_MONETARY *)(&(v)->data.money))

#define DB_GET_POINTER(v) \
    (((v)->domain.general_info.is_null || \
      (v)->domain.general_info.type == DB_TYPE_ERROR) ? NULL : (v)->data.p)

#define DB_GET_ERROR(v) \
    ((v)->data.error)

#define DB_GET_SHORT(v) \
    ((v)->data.sh)

#define DB_GET_SMALLINT(v) DB_GET_SHORT(v)

#define DB_GET_NUMERIC(v) \
    (((v)->domain.general_info.is_null || \
      (v)->domain.general_info.type == DB_TYPE_ERROR) ? \
                      NULL : (v)->data.num.d.buf)

#define DB_GET_STRING_SIZE(v) \
    (((v)->data.ch.info.style == MEDIUM_STRING) ? \
      (((v)->domain.general_info.type == DB_TYPE_BIT || \
        (v)->domain.general_info.type == DB_TYPE_VARBIT) ? \
        ((v)->data.ch.medium.size + 7) / 8 : (v)->data.ch.medium.size) : ( \
      ((v)->data.ch.info.style == SMALL_STRING) ? \
      (((v)->domain.general_info.type == DB_TYPE_BIT || \
        (v)->domain.general_info.type == DB_TYPE_VARBIT) ? \
        ((v)->data.ch.medium.size + 7) / 8 : (v)->data.ch.medium.size) : 0))

#define DB_GET_RESULTSET(v) \
    ((v)->data.rset)

#define DB_GET_STRING_CODESET(v) \
    ((INTL_CODESET) ((v)->data.ch.info.codeset))



#define db_value_is_null(v) DB_IS_NULL(v)
#define db_value_type(v) DB_VALUE_TYPE(v)
#define db_value_scale(v) DB_VALUE_SCALE(v)
#define db_value_precision(v) DB_VALUE_PRECISION(v)
#define db_get_int(v) DB_GET_INTEGER(v)
#define db_get_float(v) DB_GET_FLOAT(v)
#define db_get_double(v) DB_GET_DOUBLE(v)
#define db_get_string(v) DB_GET_STRING(v)
#define db_get_char(v, l) DB_GET_CHAR(v, l)
#define db_get_nchar(v, l) DB_GET_NCHAR(v, l)
#define db_get_bit(v, l) DB_GET_BIT(v, l)
#define db_get_object(v) DB_GET_OBJECT(v)
#define db_get_oid(v) DB_GET_OID(v)
#define db_get_set(v) DB_GET_SET(v)
#define db_get_midxkey(v) DB_GET_MIDXKEY(v)
#define db_get_elo(v) DB_GET_ELO(v)
#define db_get_time(v) DB_GET_TIME(v)
#define db_get_date(v) DB_GET_DATE(v)
#define db_get_timestamp(v) DB_GET_TIMESTAMP(v)
#define db_get_monetary(v) DB_GET_MONETARY(v)
#define db_get_pointer(v) DB_GET_POINTER(v)
#define db_get_error(v) DB_GET_ERROR(v)
#define db_get_short(v) DB_GET_SHORT(v)
#define db_get_smallint(v) DB_GET_SHORT(v)
#define db_get_numeric(v) DB_GET_NUMERIC(v)
#define db_get_string_size(v) DB_GET_STRING_SIZE(v)
#define db_get_resultset(v) DB_GET_RESULTSET(v)
#define db_get_string_codeset(v) DB_GET_STRING_CODESET(v)

#define db_make_null(v) \
    ((v)->domain.general_info.type = DB_TYPE_NULL, \
     (v)->domain.general_info.is_null = 1, \
     (v)->need_clear = false, \
     NO_ERROR)

#define db_make_int(v, n) \
    ((v)->domain.general_info.type = DB_TYPE_INTEGER, \
     (v)->data.i = (n), \
     (v)->domain.general_info.is_null = 0, \
     (v)->need_clear = false, \
     NO_ERROR)

#define db_make_float(v, n) \
    ((v)->domain.general_info.type = DB_TYPE_FLOAT, \
     (v)->data.f = (n), \
     (v)->domain.general_info.is_null = 0, \
     (v)->need_clear = false, \
     NO_ERROR)

#define db_make_double(v, n) \
    ((v)->domain.general_info.type = DB_TYPE_DOUBLE, \
     (v)->data.d = (n), \
     (v)->domain.general_info.is_null = 0, \
     (v)->need_clear = false, \
     NO_ERROR)

#define db_make_object(v, n) \
    ((v)->domain.general_info.type = DB_TYPE_OBJECT, \
     (v)->data.op = (n), \
     (v)->domain.general_info.is_null = ((n) ? 0 : 1), \
     (v)->need_clear = false, \
     NO_ERROR)

#define db_make_oid(v, n) \
    (((n) == NULL) ? ((v)->domain.general_info.is_null = 1, NO_ERROR) : \
     ((v)->domain.general_info.type = DB_TYPE_OID, \
     (v)->data.oid.pageid = (n)->pageid, \
     (v)->data.oid.slotid = (n)->slotid, \
     (v)->data.oid.volid = (n)->volid, \
     (v)->domain.general_info.is_null = OID_ISNULL(n), \
     (v)->need_clear = false, \
     NO_ERROR))

#define db_make_elo(v, n) \
    ((v)->domain.general_info.type = DB_TYPE_ELO, \
     (v)->data.elo = (n), \
     (v)->domain.general_info.is_null = ((n) ? 0 : 1), \
     (v)->need_clear = false, \
     NO_ERROR)

#define db_make_timestamp(v, n) \
    ((v)->domain.general_info.type = DB_TYPE_TIMESTAMP, \
     (v)->data.utime = (n), \
     (v)->domain.general_info.is_null = 0, \
     (v)->need_clear = false, \
     NO_ERROR)

#define db_make_pointer(v, n) \
    ((v)->domain.general_info.type = DB_TYPE_POINTER, \
     (v)->data.p = (n), \
     (v)->domain.general_info.is_null = ((n) ? 0 : 1), \
     (v)->need_clear = false, \
     NO_ERROR)

#define db_make_error(v, n) \
    ((v)->domain.general_info.type = DB_TYPE_ERROR, \
     (v)->data.error = (n), \
     (v)->domain.general_info.is_null = 0, \
     (v)->need_clear = false, \
     NO_ERROR)

#define db_make_short(v, n) \
    ((v)->domain.general_info.type = DB_TYPE_SHORT, \
     (v)->data.sh = (n), \
     (v)->domain.general_info.is_null = 0, \
     (v)->need_clear = false, \
     NO_ERROR)

#define db_make_db_char(v, c, p, s) \
    ((v)->data.ch.info.style = MEDIUM_STRING, \
     (v)->data.ch.medium.codeset = (c), \
     (v)->data.ch.medium.size = (s), \
     (v)->data.ch.medium.buf = (char *) (p), \
     (v)->domain.general_info.is_null = ((p) ? 0 : 1), \
     (v)->domain.general_info.is_null = \
         (PRM_ORACLE_STYLE_EMPTY_STRING && (s) == 0) \
         ? 1 \
         : (v)->domain.general_info.is_null, \
     (v)->need_clear = false, \
     NO_ERROR)

/* The following char- & string-related macros include functionality from
   db_value_domain_init as well as a call to db_make_db_char, redefined as a 
   macro above */
#define db_make_bit(v, l, p, s) \
    ((v)->domain.char_info.length = ((l) == DB_DEFAULT_PRECISION) ? \
      TP_FLOATING_PRECISION_VALUE : (l), \
     (v)->domain.general_info.type = DB_TYPE_BIT, \
     (v)->need_clear = false, \
     db_make_db_char((v), INTL_CODESET_RAW_BITS, (p), (s)), \
     NO_ERROR)

#define db_make_varbit(v, l, p, s) \
    ((v)->domain.char_info.length = ((l) == DB_DEFAULT_PRECISION) ? \
      DB_MAX_VARBIT_PRECISION : (l), \
     (v)->domain.general_info.type = DB_TYPE_VARBIT, \
     (v)->need_clear = false, \
     db_make_db_char((v), INTL_CODESET_RAW_BITS, (p), (s)), \
     NO_ERROR)

#define db_make_string(v, p) \
    ((v)->domain.char_info.length = DB_MAX_VARCHAR_PRECISION, \
     (v)->domain.general_info.type = DB_TYPE_VARCHAR, \
     (v)->need_clear = false, \
     db_make_db_char((v), INTL_CODESET_ISO88591, (p), ((p) ? strlen(p) : 0)), \
     NO_ERROR)

#define db_make_char(v, l, p, s) \
    ((v)->domain.char_info.length = ((l) == DB_DEFAULT_PRECISION) ? \
      TP_FLOATING_PRECISION_VALUE : (l), \
     (v)->domain.general_info.type = DB_TYPE_CHAR, \
     (v)->need_clear = false, \
     db_make_db_char((v), INTL_CODESET_ISO88591, (p), (s)), \
     NO_ERROR)

#define db_make_varchar(v, l, p, s) \
    ((v)->domain.char_info.length = ((l) == DB_DEFAULT_PRECISION) ? \
      DB_MAX_VARCHAR_PRECISION : (l), \
     (v)->domain.general_info.type = DB_TYPE_VARCHAR, \
     (v)->need_clear = false, \
     db_make_db_char((v), INTL_CODESET_ISO88591, (p), (s)), \
     NO_ERROR)

#define db_make_nchar(v, l, p, s) \
    ((v)->domain.char_info.length = ((l) == DB_DEFAULT_PRECISION) ? \
      TP_FLOATING_PRECISION_VALUE : (l), \
     (v)->domain.general_info.type = DB_TYPE_NCHAR, \
     (v)->need_clear = false, \
     db_make_db_char((v), lang_charset(), (p), (s)), \
     NO_ERROR)

#define db_make_varnchar(v, l, p, s) \
    ((v)->domain.char_info.length = ((l) == DB_DEFAULT_PRECISION) ? \
      DB_MAX_VARNCHAR_PRECISION : (l), \
     (v)->domain.general_info.type = DB_TYPE_VARNCHAR, \
     (v)->need_clear = false, \
     db_make_db_char((v), lang_charset(), (p), (s)), \
     NO_ERROR)

#define db_make_resultset(v, n) \
    ((v)->domain.general_info.type = DB_TYPE_RESULTSET, \
     (v)->data.rset = (n), \
     (v)->domain.general_info.is_null = 0, \
     (v)->need_clear = false, \
     NO_ERROR)

#define db_make_midxkey(v, m) \
    ((v)->domain.general_info.type = DB_TYPE_MIDXKEY, \
     (((m) == NULL) ? \
       ((v)->domain.general_info.is_null = 1, \
        (v)->data.midxkey.ncolumns       = -1, \
        (v)->data.midxkey.domain         = NULL, \
        (v)->data.midxkey.size           = 0, \
        (v)->data.midxkey.buf            = NULL) \
      : \
       ((v)->domain.general_info.is_null = 0, \
        (v)->data.midxkey.ncolumns       = (m)->ncolumns, \
        (v)->data.midxkey.domain         = (m)->domain, \
        (v)->data.midxkey.size           = (m)->size, \
        (v)->data.midxkey.buf            = (m)->buf)), \
     (v)->need_clear = false, \
     NO_ERROR)

#define DB_GET_NUMERIC_PRECISION(val) \
    ((val)->domain.numeric_info.precision)

#define DB_GET_NUMERIC_SCALE(val) \
    ((val)->domain.numeric_info.scale)

typedef enum 
{
  SMALL_STRING,
  MEDIUM_STRING,
  LARGE_STRING
} STRING_STYLE;

#endif /* _DBVAL_H_ */
