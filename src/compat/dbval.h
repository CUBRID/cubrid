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
#if !defined(NDEBUG)
#else
#undef DB_VALUE_DOMAIN_TYPE
#endif
#undef DB_VALUE_TYPE
#undef DB_VALUE_SCALE
#undef DB_VALUE_PRECISION
#undef DB_GET_INTEGER
#undef DB_GET_BIGINT
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
#undef DB_GET_TIMETZ
#undef DB_GET_DATE
#undef DB_GET_TIMESTAMP
#undef DB_GET_TIMESTAMPTZ
#undef DB_GET_DATETIME
#undef DB_GET_DATETIMETZ
#undef DB_GET_MONETARY
#undef DB_GET_POINTER
#undef DB_GET_ERROR
#undef DB_GET_SHORT
#undef DB_GET_SMALLINT
#undef DB_GET_NUMERIC
#undef DB_GET_STRING_SIZE
#undef DB_GET_RESULTSET
#undef DB_GET_STRING_CODESET
#undef DB_GET_STRING_COLLATION
#undef DB_GET_ENUM_CODESET
#undef DB_GET_ENUM_COLLATION


#define DB_IS_NULL(v) \
    ((((DB_VALUE *) (v) != NULL) && (v)->domain.general_info.is_null == 0) ? false : true)

#if !defined(NDEBUG)
#else
#define DB_VALUE_DOMAIN_TYPE(v)	\
    ((DB_TYPE) ((v)->domain.general_info.type))
#endif

#define DB_VALUE_TYPE(v) \
    (DB_IS_NULL(v) ? DB_TYPE_NULL : DB_VALUE_DOMAIN_TYPE(v))

#define DB_VALUE_SCALE(v) \
    ((DB_VALUE_DOMAIN_TYPE(v) == DB_TYPE_NUMERIC) ? \
      (v)->domain.numeric_info.scale : 0)

#define DB_VALUE_PRECISION(v) \
    ((DB_VALUE_DOMAIN_TYPE (v) == DB_TYPE_NUMERIC) \
       ? ((v)->domain.numeric_info.precision) : \
          ((DB_VALUE_DOMAIN_TYPE (v) == DB_TYPE_BIT \
	    || DB_VALUE_DOMAIN_TYPE (v) == DB_TYPE_VARBIT \
	    || DB_VALUE_DOMAIN_TYPE (v) == DB_TYPE_CHAR \
	    || DB_VALUE_DOMAIN_TYPE (v) == DB_TYPE_VARCHAR \
	    || DB_VALUE_DOMAIN_TYPE (v) == DB_TYPE_NCHAR \
	    || DB_VALUE_DOMAIN_TYPE (v) == DB_TYPE_VARNCHAR) \
           ? ((v)->domain.char_info.length) : 0))

#define DB_GET_INTEGER(v) \
    ((assert (DB_VALUE_DOMAIN_TYPE (v) == DB_TYPE_INTEGER)), (v)->data.i)

#define DB_GET_BIGINT(v) \
    ((assert (DB_VALUE_DOMAIN_TYPE (v) == DB_TYPE_BIGINT)), (v)->data.bigint)

#define DB_GET_FLOAT(v) \
    ((assert (DB_VALUE_DOMAIN_TYPE (v) == DB_TYPE_FLOAT)), (v)->data.f)

#define DB_GET_DOUBLE(v) \
    ((assert (DB_VALUE_DOMAIN_TYPE (v) == DB_TYPE_DOUBLE)), (v)->data.d)

/* note : this will have to change when we start using the small and large
          string buffers. */
#define DB_GET_STRING(v) \
      ((DB_IS_NULL (v) \
	|| DB_VALUE_DOMAIN_TYPE (v) == DB_TYPE_ERROR) ? NULL \
       : ((assert (DB_VALUE_DOMAIN_TYPE(v) == DB_TYPE_VARCHAR \
		   || DB_VALUE_DOMAIN_TYPE(v) == DB_TYPE_CHAR \
		   || DB_VALUE_DOMAIN_TYPE(v) == DB_TYPE_VARNCHAR \
		   || DB_VALUE_DOMAIN_TYPE(v) == DB_TYPE_NCHAR \
		   || DB_VALUE_DOMAIN_TYPE(v) == DB_TYPE_VARBIT \
		   || DB_VALUE_DOMAIN_TYPE(v) == DB_TYPE_BIT)), \
	  (v)->data.ch.medium.buf))

#define DB_GET_STRING_SAFE(v) \
      ((DB_IS_NULL (v) \
	|| DB_VALUE_DOMAIN_TYPE (v) == DB_TYPE_ERROR) ? "" \
       : ((assert (DB_VALUE_DOMAIN_TYPE(v) == DB_TYPE_VARCHAR \
		   || DB_VALUE_DOMAIN_TYPE(v) == DB_TYPE_CHAR \
		   || DB_VALUE_DOMAIN_TYPE(v) == DB_TYPE_VARNCHAR \
		   || DB_VALUE_DOMAIN_TYPE(v) == DB_TYPE_NCHAR \
		   || DB_VALUE_DOMAIN_TYPE(v) == DB_TYPE_VARBIT \
		   || DB_VALUE_DOMAIN_TYPE(v) == DB_TYPE_BIT)), \
	  (v)->data.ch.medium.buf))

#define DB_PULL_STRING(v) \
      ((assert (DB_VALUE_DOMAIN_TYPE(v) == DB_TYPE_VARCHAR \
		|| DB_VALUE_DOMAIN_TYPE(v) == DB_TYPE_CHAR \
		|| DB_VALUE_DOMAIN_TYPE(v) == DB_TYPE_VARNCHAR \
		|| DB_VALUE_DOMAIN_TYPE(v) == DB_TYPE_NCHAR \
		|| DB_VALUE_DOMAIN_TYPE(v) == DB_TYPE_VARBIT \
		|| DB_VALUE_DOMAIN_TYPE(v) == DB_TYPE_BIT)), \
       (v)->data.ch.medium.buf)

#define DB_GET_STRING_PRECISION(v) \
    ((v)->domain.char_info.length)

#define DB_GET_BIT_PRECISION(v) \
    ((v)->domain.char_info.length)

#define DB_GET_COMPRESSED_STRING(v) \
      ((DB_VALUE_DOMAIN_TYPE(v) != DB_TYPE_VARCHAR) && (DB_VALUE_DOMAIN_TYPE(v) != DB_TYPE_VARNCHAR) \
	? NULL : (v)->data.ch.medium.compressed_buf)

#define DB_NEED_CLEAR(v) \
      ((!DB_IS_NULL(v) \
	&& ((v)->need_clear == true \
	    || ((DB_VALUE_DOMAIN_TYPE(v) == DB_TYPE_VARCHAR || DB_VALUE_DOMAIN_TYPE(v) == DB_TYPE_VARNCHAR) \
		 && (v)->data.ch.info.compressed_need_clear == true))))

/* note : this will have to change when we start using the small and large
          string buffers. */
#define DB_GET_CHAR(v, l) \
      ((DB_IS_NULL (v) || DB_VALUE_DOMAIN_TYPE (v) == DB_TYPE_ERROR) ? \
       NULL : \
       (intl_char_count ((unsigned char *) (v)->data.ch.medium.buf, \
			 (v)->data.ch.medium.size, \
			 (INTL_CODESET) (v)->data.ch.medium.codeset, (l)), \
	(assert (DB_VALUE_DOMAIN_TYPE (v) == DB_TYPE_VARCHAR \
		 || DB_VALUE_DOMAIN_TYPE (v) == DB_TYPE_CHAR \
		 || DB_VALUE_DOMAIN_TYPE (v) == DB_TYPE_VARNCHAR \
		 || DB_VALUE_DOMAIN_TYPE (v) == DB_TYPE_NCHAR \
		 || DB_VALUE_DOMAIN_TYPE (v) == DB_TYPE_VARBIT \
		 || DB_VALUE_DOMAIN_TYPE (v) == DB_TYPE_BIT), \
	 (v)->data.ch.medium.buf)))

#define DB_PULL_CHAR(v, l) \
      (intl_char_count ((unsigned char *) (v)->data.ch.medium.buf, \
			(v)->data.ch.medium.size, \
			(INTL_CODESET) (v)->data.ch.medium.codeset, (l)), \
       ((assert (DB_VALUE_DOMAIN_TYPE (v) == DB_TYPE_VARCHAR \
		 || DB_VALUE_DOMAIN_TYPE (v) == DB_TYPE_CHAR \
		 || DB_VALUE_DOMAIN_TYPE (v) == DB_TYPE_VARNCHAR \
		 || DB_VALUE_DOMAIN_TYPE (v) == DB_TYPE_NCHAR \
		 || DB_VALUE_DOMAIN_TYPE (v) == DB_TYPE_VARBIT \
		 || DB_VALUE_DOMAIN_TYPE (v) == DB_TYPE_BIT)), \
	(v)->data.ch.medium.buf))

#define DB_GET_NCHAR(v, l) DB_GET_CHAR(v, l)
#define DB_PULL_NCHAR(v, l) DB_PULL_CHAR(v, l)


/* note: this will have to change when we start using the small and large
         string buffers. */
#define DB_GET_BIT(v, l) \
      ((DB_IS_NULL (v) || DB_VALUE_DOMAIN_TYPE (v) == DB_TYPE_ERROR) ? \
       NULL : \
       (assert (DB_VALUE_DOMAIN_TYPE (v) == DB_TYPE_BIT \
		|| DB_VALUE_DOMAIN_TYPE (v) == DB_TYPE_VARBIT), \
	((*(l)) = (v)->data.ch.medium.size), (v)->data.ch.medium.buf))

#define DB_PULL_BIT(v, l) \
      ((assert (DB_VALUE_DOMAIN_TYPE (v) == DB_TYPE_BIT \
		|| DB_VALUE_DOMAIN_TYPE (v) == DB_TYPE_VARBIT)), \
       ((*(l)) = (v)->data.ch.medium.size), (v)->data.ch.medium.buf)

#define DB_GET_OBJECT(v) \
      ((DB_IS_NULL (v) || DB_VALUE_DOMAIN_TYPE (v) == DB_TYPE_ERROR) ? \
       (DB_OBJECT *) (NULL) : (v)->data.op)

#define DB_PULL_OBJECT(v) \
      ((v)->data.op)

#define DB_GET_OID(v) \
      ((DB_IS_NULL (v) || DB_VALUE_DOMAIN_TYPE (v) == DB_TYPE_ERROR) ? \
       (OID *) (NULL) : \
       (assert (DB_VALUE_DOMAIN_TYPE (v) == DB_TYPE_OID), \
	&((v)->data.oid)))

#define DB_PULL_OID(v) \
      (assert (DB_VALUE_DOMAIN_TYPE (v) == DB_TYPE_OID), \
       &((v)->data.oid))

#define DB_GET_SET(v) \
      ((DB_IS_NULL (v) || DB_VALUE_DOMAIN_TYPE (v) == DB_TYPE_ERROR) ? \
       NULL : \
       (assert (DB_VALUE_DOMAIN_TYPE (v) == DB_TYPE_SET \
		|| DB_VALUE_DOMAIN_TYPE (v) == DB_TYPE_MULTISET \
		|| DB_VALUE_DOMAIN_TYPE (v) == DB_TYPE_SEQUENCE \
		|| DB_VALUE_DOMAIN_TYPE (v) == DB_TYPE_VOBJ), \
	(v)->data.set))

#define DB_PULL_SET(v) \
      (assert (DB_VALUE_DOMAIN_TYPE (v) == DB_TYPE_SET \
	       || DB_VALUE_DOMAIN_TYPE (v) == DB_TYPE_MULTISET \
	       || DB_VALUE_DOMAIN_TYPE (v) == DB_TYPE_SEQUENCE \
	       || DB_VALUE_DOMAIN_TYPE (v) == DB_TYPE_VOBJ), \
       (v)->data.set)

#define DB_GET_MULTISET(v) DB_GET_SET(v)
#define DB_PULL_MULTISET(v) DB_PULL_SET(v)

#define DB_GET_LIST(v) DB_GET_SET(v)
#define DB_PULL_LIST(v) DB_PULL_SET(v)

#define DB_PULL_SEQUENCE(v) DB_PULL_LIST(v)

#define DB_PULL_MIDXKEY(v) \
      (assert (DB_VALUE_DOMAIN_TYPE (v) == DB_TYPE_MIDXKEY), \
       (DB_MIDXKEY *) (&(v)->data.midxkey))

#define DB_GET_MIDXKEY(v) \
      ((DB_IS_NULL (v) || DB_VALUE_DOMAIN_TYPE (v) == DB_TYPE_ERROR) ? \
       NULL : DB_PULL_MIDXKEY(v))

#define DB_GET_ELO(v) \
      ((DB_IS_NULL (v) || DB_VALUE_DOMAIN_TYPE (v) == DB_TYPE_ERROR || \
	(v)->data.elo.type == ELO_NULL) ? NULL : \
       (assert (DB_VALUE_DOMAIN_TYPE (v) == DB_TYPE_ELO \
		|| DB_VALUE_DOMAIN_TYPE (v) == DB_TYPE_CLOB \
		|| DB_VALUE_DOMAIN_TYPE (v) == DB_TYPE_BLOB), \
	(DB_ELO *) (&(v)->data.elo)))

#define DB_PULL_ELO(v) \
      (assert (DB_VALUE_DOMAIN_TYPE (v) == DB_TYPE_ELO \
	       || DB_VALUE_DOMAIN_TYPE (v) == DB_TYPE_CLOB \
	       || DB_VALUE_DOMAIN_TYPE (v) == DB_TYPE_BLOB), \
       (DB_ELO *) (&((v)->data.elo)))

#define DB_GET_TIME(v) \
      (assert (DB_VALUE_DOMAIN_TYPE (v) == DB_TYPE_TIME		\
	       || DB_VALUE_DOMAIN_TYPE (v) == DB_TYPE_TIMELTZ), \
       (DB_TIME *) (&(v)->data.time))

#define DB_GET_TIMETZ(v) \
    (assert (DB_VALUE_DOMAIN_TYPE (v) == DB_TYPE_TIMETZ), \
     (DB_TIMETZ *) (&(v)->data.timetz))

#define DB_GET_DATE(v) \
      (assert (DB_VALUE_DOMAIN_TYPE (v) == DB_TYPE_DATE), \
       (DB_DATE *) (&(v)->data.date))

#define DB_GET_TIMESTAMP(v) \
      (assert (DB_VALUE_DOMAIN_TYPE (v) == DB_TYPE_TIMESTAMP	     \
	       || DB_VALUE_DOMAIN_TYPE (v) == DB_TYPE_TIMESTAMPLTZ), \
       (DB_TIMESTAMP *) (&(v)->data.utime))

#define DB_GET_TIMESTAMPTZ(v) \
    (assert (DB_VALUE_DOMAIN_TYPE (v) == DB_TYPE_TIMESTAMPTZ),	    \
     (DB_TIMESTAMPTZ *) (&(v)->data.timestamptz))

#define DB_GET_DATETIME(v) \
      (assert (DB_VALUE_DOMAIN_TYPE (v) == DB_TYPE_DATETIME	    \
	       || DB_VALUE_DOMAIN_TYPE (v) == DB_TYPE_DATETIMELTZ), \
       (DB_DATETIME *) (&(v)->data.datetime))

#define DB_GET_DATETIMETZ(v) \
    (assert (DB_VALUE_DOMAIN_TYPE (v) == DB_TYPE_DATETIMETZ),  \
     (DB_DATETIMETZ *) (&(v)->data.datetimetz))

#define DB_GET_MONETARY(v) \
      (assert (DB_VALUE_DOMAIN_TYPE (v) == DB_TYPE_MONETARY), \
       (DB_MONETARY *) (&(v)->data.money))

#define DB_GET_POINTER(v) \
      ((DB_IS_NULL (v) || DB_VALUE_DOMAIN_TYPE (v) == DB_TYPE_ERROR) ? \
       NULL : \
       (assert (DB_VALUE_DOMAIN_TYPE (v) == DB_TYPE_POINTER), (v)->data.p))

#define DB_PULL_POINTER(v) \
      (assert (DB_VALUE_DOMAIN_TYPE (v) == DB_TYPE_POINTER), (v)->data.p)

#define DB_GET_ERROR(v) \
      (assert (DB_VALUE_DOMAIN_TYPE (v) == DB_TYPE_ERROR), (v)->data.error)

#define DB_GET_SHORT(v) \
      (assert (DB_VALUE_DOMAIN_TYPE (v) == DB_TYPE_SHORT), (v)->data.sh)

#define DB_GET_SMALLINT(v) DB_GET_SHORT(v)

#define DB_GET_NUMERIC(v) \
      ((DB_IS_NULL (v) || DB_VALUE_DOMAIN_TYPE (v) == DB_TYPE_ERROR) ? \
       NULL : \
       (assert (DB_VALUE_DOMAIN_TYPE (v) == DB_TYPE_NUMERIC), \
	(v)->data.num.d.buf))

#define DB_PULL_NUMERIC(v) \
      (assert (DB_VALUE_DOMAIN_TYPE (v) == DB_TYPE_NUMERIC), \
       (v)->data.num.d.buf)

#define DB_GET_STRING_SIZE(v) \
      (((v)->data.ch.info.style == MEDIUM_STRING) \
       ? ((DB_VALUE_DOMAIN_TYPE (v) == DB_TYPE_BIT \
	   || DB_VALUE_DOMAIN_TYPE (v) == DB_TYPE_VARBIT) \
	  ? (((v)->data.ch.medium.size + 7) / 8) \
	  : (v)->data.ch.medium.size) \
       : (((v)->data.ch.info.style == SMALL_STRING) \
	  ? ((DB_VALUE_DOMAIN_TYPE (v) == DB_TYPE_BIT \
	      || DB_VALUE_DOMAIN_TYPE (v) == DB_TYPE_VARBIT) \
	     ? (((v)->data.ch.sm.size + 7) / 8)  \
	     : (v)->data.ch.sm.size)  \
	  : 0))

#define DB_GET_RESULTSET(v) \
      ((v)->data.rset)

#define DB_GET_STRING_CODESET(v) \
      ((INTL_CODESET) ((v)->data.ch.info.codeset))
#define DB_GET_STRING_COLLATION(v) \
      (((v)->domain.char_info.collation_id))

#define DB_GET_ENUM_ELEM_SHORT(elem) \
      ((elem)->short_val)
#define DB_GET_ENUM_ELEM_DBCHAR(elem) \
      ((elem)->str_val)
#define DB_GET_ENUM_ELEM_STRING(elem) \
      ((elem)->str_val.medium.buf)
#define DB_GET_ENUM_ELEM_STRING_SIZE(elem) \
      ((elem)->str_val.medium.size)

#define DB_GET_ENUM_ELEM_CODESET(elem) \
      ((elem)->str_val.info.codeset)
#define DB_SET_ENUM_ELEM_CODESET(elem, cs) \
      ((elem)->str_val.info.codeset = (cs))

#define DB_SET_ENUM_ELEM_SHORT(elem, sv) \
      ((elem)->short_val = (sv))
#define DB_SET_ENUM_ELEM_STRING(elem, str) \
      ((elem)->str_val.medium.buf = (str),  \
       (elem)->str_val.info.style = MEDIUM_STRING)
#define DB_SET_ENUM_ELEM_STRING_SIZE(elem, sz) \
      ((elem)->str_val.medium.size = (sz))

#define DB_GET_ENUMERATION(v) \
      ((v)->data.enumeration)
#define DB_GET_ENUM_SHORT(v) \
      ((v)->data.enumeration.short_val)
#define DB_GET_ENUM_STRING(v) \
      ((v)->data.enumeration.str_val.medium.buf)
#define DB_GET_ENUM_STRING_SIZE(v) \
      ((v)->data.enumeration.str_val.medium.size)

#define DB_GET_ENUM_CODESET(v) \
      ((INTL_CODESET) ((v)->data.enumeration.str_val.info.codeset))
#define DB_GET_ENUM_COLLATION(v) \
      ((v)->domain.char_info.collation_id)

#define db_value_is_null(v) DB_IS_NULL(v)
#define db_value_type(v) DB_VALUE_TYPE(v)
#define db_value_scale(v) DB_VALUE_SCALE(v)
#define db_value_precision(v) DB_VALUE_PRECISION(v)
#define db_get_int(v) DB_GET_INTEGER(v)
#define db_get_float(v) DB_GET_FLOAT(v)
#define db_get_double(v) DB_GET_DOUBLE(v)
#define db_get_string(v) DB_GET_STRING(v)
#define db_pull_string(v) DB_PULL_STRING(v)
#define db_get_char(v, l) DB_GET_CHAR(v, l)
#define db_pull_char(v, l) DB_PULL_CHAR(v, l)
#define db_get_nchar(v, l) DB_GET_NCHAR(v, l)
#define db_pull_nchar(v, l) DB_PULL_NCHAR(v, l)
#define db_get_bit(v, l) DB_GET_BIT(v, l)
#define db_pull_bit(v, l) DB_PULL_BIT(v, l)
#define db_get_object(v) DB_GET_OBJECT(v)
#define db_pull_object(v) DB_PULL_OBJECT(v)
#define db_get_oid(v) DB_GET_OID(v)
#define db_pull_oid(v) DB_PULL_OID(v)
#define db_get_set(v) DB_GET_SET(v)
#define db_pull_set(v) DB_PULL_SET(v)
#define db_get_midxkey(v) DB_GET_MIDXKEY(v)
#define db_pull_midxkey(v) DB_PULL_MIDXKEY(v)
#define db_get_elo(v) DB_GET_ELO(v)
#define db_pull_elo(v) DB_PULL_ELO(v)
#define db_get_time(v) DB_GET_TIME(v)
#define db_get_timetz(v) DB_GET_TIMETZ(v)
#define db_get_date(v) DB_GET_DATE(v)
#define db_get_timestamp(v) DB_GET_TIMESTAMP(v)
#define db_get_timestamptz(v) DB_GET_TIMESTAMPTZ(v)
#define db_get_datetime(v) DB_GET_DATETIME(v)
#define db_get_datetimetz(v) DB_GET_DATETIMETZ(v)
#define db_get_monetary(v) DB_GET_MONETARY(v)
#define db_get_pointer(v) DB_GET_POINTER(v)
#define db_pull_pointer(v) DB_PULL_POINTER(v)
#define db_get_error(v) DB_GET_ERROR(v)
#define db_get_short(v) DB_GET_SHORT(v)
#define db_get_smallint(v) DB_GET_SHORT(v)
#define db_get_numeric(v) DB_GET_NUMERIC(v)
#define db_pull_numeric(v) DB_PULL_NUMERIC(v)
#define db_get_string_size(v) DB_GET_STRING_SIZE(v)
#define db_get_resultset(v) DB_GET_RESULTSET(v)
#define db_get_string_codeset(v) DB_GET_STRING_CODESET(v)
#define db_get_string_collation(v) DB_GET_STRING_COLLATION(v)
#define db_get_enum_short(v) DB_GET_ENUM_SHORT(v)
#define db_get_enum_string(v) DB_GET_ENUM_STRING(v)
#define db_get_enum_string_size(v) DB_GET_ENUM_STRING_SIZE(v)
#define db_get_enum_codeset(v) DB_GET_ENUM_CODESET(v)
#define db_get_enum_collation(v) DB_GET_ENUM_COLLATION(v)

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

#define db_make_bigint(v, n) \
    ((v)->domain.general_info.type = DB_TYPE_BIGINT, \
     (v)->data.bigint = (n), \
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

#define db_push_oid(v, n) \
    ((v)->domain.general_info.type = DB_TYPE_OID, \
     (v)->data.oid.pageid = (n)->pageid, \
     (v)->data.oid.slotid = (n)->slotid, \
     (v)->data.oid.volid = (n)->volid, \
     (v)->domain.general_info.is_null = OID_ISNULL(n), \
     (v)->need_clear = false, \
     NO_ERROR)

#define db_make_oid(v, n) \
    (((n) == NULL) ? ((v)->domain.general_info.is_null = 1, NO_ERROR) : \
     db_push_oid((v), (n)))

#define db_push_elo(v, t, n) \
    ((v)->domain.general_info.type = (t), \
     (v)->data.elo.size = (n)->size, \
     (v)->data.elo.locator = (n)->locator, \
     (v)->data.elo.meta_data = (n)->meta_data, \
     (v)->data.elo.type = (n)->type, \
     (v)->data.elo.es_type = (n)->es_type, \
     (v)->domain.general_info.is_null = 0, \
     (v)->need_clear = false, \
     NO_ERROR)

#define db_make_elo(v, t, n) \
     (((n) == NULL || ((n)->size < 0) || ((n)->type == ELO_NULL)) ? \
          ((v)->domain.general_info.is_null = 1, NO_ERROR) : \
      db_push_elo((v), (t), (n)))

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

#define db_make_db_char(v, c, coll, p, s) \
    ((v)->data.ch.info.style = MEDIUM_STRING, \
     (v)->data.ch.info.is_max_string = false, \
     (v)->data.ch.info.compressed_need_clear = false, \
     (v)->data.ch.medium.codeset = (c), \
     (v)->data.ch.medium.size = (s), \
     (v)->data.ch.medium.buf = (char *) (p), \
     (v)->data.ch.medium.compressed_buf = NULL, \
     (v)->data.ch.medium.compressed_size = 0, \
     (v)->domain.general_info.is_null = (((void *) (p) != NULL) ? 0 : 1), \
     (v)->domain.general_info.is_null = \
         ((s) == 0 && prm_get_bool_value (PRM_ID_ORACLE_STYLE_EMPTY_STRING)) \
	  ? 1 : DB_IS_NULL(v), \
     (v)->domain.char_info.collation_id = (coll), \
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
     db_make_db_char((v), INTL_CODESET_RAW_BITS, 0, (p), (s)), \
     NO_ERROR)

#define db_make_varbit(v, l, p, s) \
    ((v)->domain.char_info.length = ((l) == DB_DEFAULT_PRECISION) ? \
      DB_MAX_VARBIT_PRECISION : (l), \
     (v)->domain.general_info.type = DB_TYPE_VARBIT, \
     (v)->need_clear = false, \
     db_make_db_char((v), INTL_CODESET_RAW_BITS, 0, (p), (s)), \
     NO_ERROR)

#define db_make_string(v, p) \
    ((v)->domain.char_info.length = DB_MAX_VARCHAR_PRECISION, \
     (v)->domain.general_info.type = DB_TYPE_VARCHAR, \
     (v)->need_clear = false, \
     db_make_db_char((v), LANG_SYS_CODESET, LANG_SYS_COLLATION, \
		     (p), (((void *) (p) != NULL) ? strlen(p) : 0)), \
     NO_ERROR)

#define db_make_char(v, l, p, s, cs, col) \
    ((v)->domain.char_info.length = ((l) == DB_DEFAULT_PRECISION) ? \
      TP_FLOATING_PRECISION_VALUE : (l), \
     (v)->domain.general_info.type = DB_TYPE_CHAR, \
     (v)->need_clear = false, \
     db_make_db_char((v), (cs), (col), (p), (s)), \
     NO_ERROR)

#define db_make_varchar(v, l, p, s, cs, col) \
    ((v)->domain.char_info.length = ((l) == DB_DEFAULT_PRECISION) ? \
      DB_MAX_VARCHAR_PRECISION : (l), \
     (v)->domain.general_info.type = DB_TYPE_VARCHAR, \
     (v)->need_clear = false, \
     db_make_db_char((v), (cs), (col), (p), (s)), \
     NO_ERROR)

#define db_make_nchar(v, l, p, s, cs, col) \
    ((v)->domain.char_info.length = ((l) == DB_DEFAULT_PRECISION) ? \
      TP_FLOATING_PRECISION_VALUE : (l), \
     (v)->domain.general_info.type = DB_TYPE_NCHAR, \
     (v)->need_clear = false, \
     db_make_db_char((v), (cs), (col), (p), (s)), \
     NO_ERROR)

#define db_make_varnchar(v, l, p, s, cs, col) \
    ((v)->domain.char_info.length = ((l) == DB_DEFAULT_PRECISION) ? \
      DB_MAX_VARNCHAR_PRECISION : (l), \
     (v)->domain.general_info.type = DB_TYPE_VARNCHAR, \
     (v)->need_clear = false, \
     db_make_db_char((v), (cs), (col), (p), (s)), \
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
        (v)->data.midxkey.buf            = NULL, \
	(v)->data.midxkey.min_max_val.position = -1, \
	(v)->data.midxkey.min_max_val.type = MIN_COLUMN) \
      : \
       ((v)->domain.general_info.is_null = 0, \
        (v)->data.midxkey.ncolumns       = (m)->ncolumns, \
        (v)->data.midxkey.domain         = (m)->domain, \
        (v)->data.midxkey.size           = (m)->size, \
        (v)->data.midxkey.buf            = (m)->buf, \
	(v)->data.midxkey.min_max_val.position = (m)->min_max_val.position, \
	(v)->data.midxkey.min_max_val.type = (m)->min_max_val.type)), \
     (v)->need_clear = false, \
     NO_ERROR)

#define db_make_enumeration(v, i, p, s, cs, coll_id) \
  ((v)->domain.general_info.type		  = DB_TYPE_ENUMERATION, \
    (v)->domain.general_info.is_null		  = 0, \
    (v)->data.enumeration.short_val		  = (i), \
    (v)->data.enumeration.str_val.info.codeset	  = (cs), \
    (v)->domain.char_info.collation_id		  = (coll_id), \
    (v)->data.enumeration.str_val.info.style	  = MEDIUM_STRING, \
    (v)->data.enumeration.str_val.medium.size	  = (s), \
    (v)->data.enumeration.str_val.medium.buf	  = (char*) (p), \
    (v)->need_clear				  = false)

#define DB_GET_NUMERIC_PRECISION(val) \
    ((val)->domain.numeric_info.precision)

#define DB_GET_NUMERIC_SCALE(val) \
    ((val)->domain.numeric_info.scale)

#define db_string_put_cs_and_collation(v, cs, coll) \
    ((v)->data.ch.info.codeset = (cs), \
     (v)->domain.char_info.collation_id = (coll), \
     NO_ERROR)

#define db_enum_put_cs_and_collation(v, cs, coll) \
    (v)->data.enumeration.str_val.info.codeset	  = (cs), \
    (v)->domain.char_info.collation_id		  = (coll)

typedef enum
{
  SMALL_STRING,
  MEDIUM_STRING,
  LARGE_STRING
} STRING_STYLE;

#endif /* _DBVAL_H_ */
