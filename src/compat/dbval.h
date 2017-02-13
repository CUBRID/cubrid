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
#define DB_PULL_NCHAR(v, l) DB_PULL_CHAR(v, l)
#define DB_PULL_BIT(v, l) \
      ((assert (DB_VALUE_DOMAIN_TYPE (v) == DB_TYPE_BIT \
		|| DB_VALUE_DOMAIN_TYPE (v) == DB_TYPE_VARBIT)), \
       ((*(l)) = (v)->data.ch.medium.size), (v)->data.ch.medium.buf)
#define DB_PULL_OBJECT(v) \
      ((v)->data.op)
#define DB_PULL_OID(v) \
      (assert (DB_VALUE_DOMAIN_TYPE (v) == DB_TYPE_OID), \
       &((v)->data.oid))
#define DB_PULL_SET(v) \
      (assert (DB_VALUE_DOMAIN_TYPE (v) == DB_TYPE_SET \
	       || DB_VALUE_DOMAIN_TYPE (v) == DB_TYPE_MULTISET \
	       || DB_VALUE_DOMAIN_TYPE (v) == DB_TYPE_SEQUENCE \
	       || DB_VALUE_DOMAIN_TYPE (v) == DB_TYPE_VOBJ), \
       (v)->data.set)
#define DB_PULL_MULTISET(v) DB_PULL_SET(v)
#define DB_PULL_LIST(v) DB_PULL_SET(v)
#define DB_PULL_SEQUENCE(v) DB_PULL_LIST(v)
/* Needs to be checked !! */
#define DB_PULL_MIDXKEY(v) DB_GET_MIDXKEY(v)
#define DB_PULL_ELO(v) \
      (assert (DB_VALUE_DOMAIN_TYPE (v) == DB_TYPE_ELO \
	       || DB_VALUE_DOMAIN_TYPE (v) == DB_TYPE_CLOB \
	       || DB_VALUE_DOMAIN_TYPE (v) == DB_TYPE_BLOB), \
       (DB_ELO *) (&((v)->data.elo)))
#define DB_PULL_POINTER(v) \
      (assert (DB_VALUE_DOMAIN_TYPE (v) == DB_TYPE_POINTER), (v)->data.p)
#define DB_PULL_NUMERIC(v) \
      (assert (DB_VALUE_DOMAIN_TYPE (v) == DB_TYPE_NUMERIC), \
       (v)->data.num.d.buf)
/* TODO: Decide whether we keep using a macro or we also switch to an inline function approach */
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

#define DB_GET_UTIME DB_GET_TIMESTAMP
#define db_get_string_safe(v) DB_GET_STRING_SAFE(v)


#define DB_GET_ENUMERATION(v) \
  ((v)->data.enumeration)
#define DB_GET_ENUM_SHORT(v) \
  ((v)->data.enumeration.short_val)
#define DB_GET_ENUM_STRING_SIZE(v) \
  ((v)->data.enumeration.str_val.medium.size)

#define db_get_enum_short(v) DB_GET_ENUM_SHORT(v)

/* TODO: Decide whether we keep this as it is or we use inline functions */
#define db_value_is_null(v) DB_IS_NULL(v)
#define db_value_type(v) DB_VALUE_TYPE(v)
#define db_value_scale(v) DB_VALUE_SCALE(v)
#define db_value_precision(v) DB_VALUE_PRECISION(v)
#define db_pull_string(v) DB_PULL_STRING(v)
#define db_pull_numeric(v)  DB_PULL_NUMERIC(v)
#define db_pull_elo(v) DB_PULL_ELO(v)
#define db_pull_midxkey(v) DB_PULL_MIDXKEY(v)
#define db_pull_oid(v) DB_PULL_OID(v)
#define db_pull_object(v) DB_PULL_OBJECT(v)
#define db_pull_bit(v, l) DB_PULL_BIT(v, l)
#define db_pull_nchar(v, l) DB_PULL_NCHAR(v, l)
#define db_pull_char(v, l) DB_PULL_CHAR(v, l)

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

#define db_get_enum_string_size(v) DB_GET_ENUM_STRING_SIZE(v)

#endif /* _DBVAL_H_ */
