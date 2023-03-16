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
 * object_domain.h: data type definitions
 */

#ifndef _OBJECT_DOMAIN_H_
#define _OBJECT_DOMAIN_H_

#ident "$Id$"

#include "config.h"
#include "dbtype_def.h"

#include <stdio.h>

#if defined (__cplusplus)
class JSON_VALIDATOR;
#endif

#define DOM_GET_ENUMERATION(dom) \
    ((dom)->enumeration)
#define DOM_GET_ENUM_ELEMENTS(dom) \
    ((dom)->enumeration.elements)
#define DOM_GET_ENUM_ELEMS_COUNT(dom) \
    ((dom)->enumeration.count)
#define DOM_GET_ENUM_ELEM(dom, idx) \
    ((dom)->enumeration.elements[idx - 1])

#define DOM_SET_ENUM_ELEMENTS(dom, elems) \
    ((dom)->enumeration.elements = (elems))
#define DOM_SET_ENUM_ELEMS_COUNT(dom, cnt) \
    ((dom)->enumeration.count = (cnt))
#define DOM_SET_ENUM(dom, elems, cnt) \
    do{\
	(dom)->enumeration.count = (cnt); \
	(dom)->enumeration.elements = (elems); \
      } while(0)
/*
 * TP_DOMAIN_SELF_REF is used as an argument
 * to tp_domain_construct so that the self_ref flag can be set
 */
#define TP_DOMAIN_SELF_REF -1

typedef enum
{
  /* normal mode, collation applies as other domain parameters */
  TP_DOMAIN_COLL_NORMAL = 0,
  /* only collation applies, the type and precision are not changed */
  TP_DOMAIN_COLL_ENFORCE = 1,
  /* type and precision are applied, collation is leaved intact */
  TP_DOMAIN_COLL_LEAVE = 2
} TP_DOMAIN_COLL_ACTION;

typedef struct tp_domain
{
  struct tp_domain *next;	/* next in the same domain list */
  struct tp_domain *next_list;	/* next domain list */
  struct pr_type *type;

  int precision;
  int scale;

  struct db_object *class_mop;	/* swizzled class oid if on client */
  struct tp_domain *setdomain;	/* hierarchical domain for sets */

  DB_ENUMERATION enumeration;	/* enumeration values */

  OID class_oid;		/* Class OID if type is tp_Object */

  /* built-in reference number */
  int built_in_index;

  unsigned char codeset;	/* codeset if char */
  int collation_id;		/* collation identifier */

  TP_DOMAIN_COLL_ACTION collation_flag;

  unsigned self_ref:1;		/* object self reference */
  /*
   * merge this with self_ref when we get a chance to rebuild the whole
   * system
   */
  unsigned is_cached:1;		/* set when the domain has been cached */

  /* non-zero if this type can be parameterized */
  unsigned is_parameterized:1;

  unsigned is_desc:1;		/* desc order for index key_type */

  /* run-time flag used during domain comparison */
  unsigned is_visited:1;

  JSON_VALIDATOR *json_validator;	/* schema validator if type is json */
} TP_DOMAIN;

  /*
   * We probably should make this 0 rather than -1 so that we can more easily
   * represent precisions with unsigned integers.  Zero is not a valid
   * precision.
   */
#define TP_FLOATING_PRECISION_VALUE -1

/*
 * TP_ALLOC_CONTEXT
 *    This structure is used in places where the storage allocation of
 *    certain structures must be controlled.
 *
 */

typedef struct tp_alloc_context
{
  void *(*alloc_func) (int size, ...);	/* optional second argument */
  void *alloc_args;
  void (*free_func) (void *mem, ...);	/* optional second argument */
  void *free_args;
} TP_ALLOC_CONTEXT;

#define TP_ALLOC(con, size) \
  (*(con)->alloc_func)(size, (con)->alloc_args)

#define TP_FREE(con, mem) \
  (*(con)->free_func)(mem, (con)->free_args)

/*
 * BUILT IN DOMAINS
 */

extern TP_DOMAIN tp_Null_domain;
extern TP_DOMAIN tp_Integer_domain;
extern TP_DOMAIN tp_Float_domain;
extern TP_DOMAIN tp_Double_domain;
extern TP_DOMAIN tp_String_domain;
extern TP_DOMAIN tp_Object_domain;
extern TP_DOMAIN tp_Set_domain;
extern TP_DOMAIN tp_Multiset_domain;
extern TP_DOMAIN tp_Sequence_domain;
extern TP_DOMAIN tp_Elo_domain;
extern TP_DOMAIN tp_Blob_domain;
extern TP_DOMAIN tp_Clob_domain;
extern TP_DOMAIN tp_Time_domain;
extern TP_DOMAIN tp_Utime_domain;
extern TP_DOMAIN tp_Date_domain;
extern TP_DOMAIN tp_Monetary_domain;
extern TP_DOMAIN tp_Variable_domain;
extern TP_DOMAIN tp_Substructure_domain;
extern TP_DOMAIN tp_Pointer_domain;
extern TP_DOMAIN tp_Error_domain;
extern TP_DOMAIN tp_Short_domain;
extern TP_DOMAIN tp_Vobj_domain;
extern TP_DOMAIN tp_Oid_domain;
extern TP_DOMAIN tp_Numeric_domain;
extern TP_DOMAIN tp_Char_domain;
extern TP_DOMAIN tp_NChar_domain;
extern TP_DOMAIN tp_VarNChar_domain;
extern TP_DOMAIN tp_Bit_domain;
extern TP_DOMAIN tp_VarBit_domain;
extern TP_DOMAIN tp_Midxkey_domain;
extern TP_DOMAIN tp_Enumeration_domain;
extern TP_DOMAIN tp_Json_domain;
extern TP_DOMAIN tp_Bigint_domain;

/*
 * TP_DOMAIN_STATUS
 *    This is used to defined a set of possible return codes
 *    for the domain comparison and checking functions.
 *    These don't set errors but rather rely on the higher level
 *    modules to set a more appropriate error.
 */

typedef enum tp_domain_status
{
  DOMAIN_COMPATIBLE = 0,	/* success */
  DOMAIN_INCOMPATIBLE,		/* can't be coerced */
  DOMAIN_OVERFLOW,		/* value out of range */
  DOMAIN_ERROR			/* an error has been set */
} TP_DOMAIN_STATUS;

/*
 * TP_MATCH
 *    This is used to describe the amount of "tolerance" to be exhibited by
 *    domain matching functions when looking for compatible domains.
 */
typedef enum tp_match
{
  TP_ANY_MATCH,			/* Any coercible domain matches. */
  TP_EXACT_MATCH,		/* Domain must match exactly.  */
  TP_STR_MATCH,			/* "String" domains match subject to length constraints and fixed/varying types, i.e.,
				 * a varying domain with precision n "matches" a fixed domain with precision m if n >=
				 * m. Only used in very special circumstances where we're trying to avoid copying
				 * strings. */

  TP_SET_MATCH
} TP_MATCH;

/*
 * TP_IS_SET_TYPE
 *    Macros for detecting the set types, saves a function call.
 */
/*
 * !!! DB_TYPE_VOBJ probably should be added to this macro as this
 * is now the behavior of pr_is_set_type() which we should try to
 * phase out in favor of the faster inline macro.  Unfortunately, there
 * are a number of usages of both TP_IS_SET_TYPE that may break if
 * we change the semantics.  Will have to think carefully about this
 */

#define TP_IS_SET_TYPE(typenum) \
  ((((typenum) == DB_TYPE_SET) || ((typenum) == DB_TYPE_MULTISET) || \
    ((typenum) == DB_TYPE_SEQUENCE)) ? true : false)

/*
 * TP_IS_BIT_TYPE
 *    Tests to see if the type id is one of the binary string types.
 */

#define TP_IS_BIT_TYPE(typeid) \
  (((typeid) == DB_TYPE_VARBIT) || ((typeid) == DB_TYPE_BIT))

/*
 * TP_IS_CHAR_TYPE
 *    Tests to see if a type is any one of the character types.
 */

#define TP_IS_CHAR_TYPE(typeid) \
  (((typeid) == DB_TYPE_VARCHAR)  || ((typeid) == DB_TYPE_CHAR) || \
   ((typeid) == DB_TYPE_VARNCHAR) || ((typeid) == DB_TYPE_NCHAR))

#define TP_IS_FIXED_LEN_CHAR_TYPE(typeid) \
  (((typeid) == DB_TYPE_CHAR) || ((typeid) == DB_TYPE_NCHAR))

#define TP_IS_VAR_LEN_CHAR_TYPE(typeid) \
    (((typeid) == DB_TYPE_VARCHAR) || ((typeid) == DB_TYPE_VARNCHAR))

/*
 * TP_IS_CHAR_BIT_TYPE
 *    Tests to see if a type is one of the character or bit types.
 */

#define TP_IS_CHAR_BIT_TYPE(typeid) (TP_IS_CHAR_TYPE(typeid) \
                                     || TP_IS_BIT_TYPE(typeid))

#define TP_IS_STRING_TYPE(typeid) TP_IS_CHAR_BIT_TYPE((typeid))

#define TP_IS_NUMERIC_TYPE(typeid) \
  (((typeid) == DB_TYPE_INTEGER) || ((typeid) == DB_TYPE_FLOAT) \
   || ((typeid) == DB_TYPE_DOUBLE)  || ((typeid) == DB_TYPE_SMALLINT) \
   || ((typeid) == DB_TYPE_NUMERIC) || ((typeid) == DB_TYPE_MONETARY) \
   || ((typeid) == DB_TYPE_BIGINT))

#define TP_IS_DOUBLE_ALIGN_TYPE(typeid) \
  ((typeid) == DB_TYPE_DOUBLE || (typeid) == DB_TYPE_BIGINT)

#define TP_IS_DATE_WITH_TZ_TYPE(typeid) \
  (((typeid) == DB_TYPE_DATETIMELTZ) || ((typeid) == DB_TYPE_DATETIMETZ) \
   || ((typeid) == DB_TYPE_TIMESTAMPLTZ) \
   || ((typeid) == DB_TYPE_TIMESTAMPTZ))

#define TP_IS_DATE_TYPE(typeid) \
  (((typeid) == DB_TYPE_DATE) || ((typeid) == DB_TYPE_DATETIME) \
   || ((typeid) == DB_TYPE_TIMESTAMP) || TP_IS_DATE_WITH_TZ_TYPE (typeid))

#define TP_IS_DATE_OR_TIME_TYPE(typeid) \
  (((typeid) == DB_TYPE_TIME) || TP_IS_DATE_TYPE(typeid))

#define TP_IS_FLOATING_NUMBER_TYPE(typeid) \
  (((typeid) == DB_TYPE_FLOAT) || ((typeid) == DB_TYPE_DOUBLE) \
   || ((typeid) == DB_TYPE_NUMERIC) || ((typeid) == DB_TYPE_MONETARY))

#define TP_IS_FIXED_NUMBER_TYPE(typeid) \
  (((typeid) == DB_TYPE_INTEGER) || ((typeid) == DB_TYPE_SMALLINT) \
   || ((typeid) == DB_TYPE_BIGINT))

/*
 * Precision for non-parameterized predefined types
 *
 * SQL-3 says time and timestamp types should be parameterized.
 * Should Cubrid implement the standard then the corresponding time/timestamp
 * macros found here would be removed and placed in dbtype.h similar to
 * the numeric macros (DB_MAX_NUMERIC_PRECISION).
 */

#define TP_DOUBLE_MANTISA_BINARY_PRECISION  53
#define TP_DOUBLE_EXPONENT_BINARY_PRECISION 11
#define TP_DOUBLE_BINARY_PRECISION	    TP_DOUBLE_MANTISA_BINARY_PRECISION

#define TP_DOUBLE_MANTISA_DECIMAL_PRECISION   16	/* 15.955 */
#define TP_DOUBLE_EXPONENT_DECIMAL_PRECISION  3	/* 3.311 */
#define TP_DOUBLE_DECIMAL_PRECISION	    TP_DOUBLE_MANTISA_DECIMAL_PRECISION

/* add 4 for exponent and mantisa sign, decimal point and the exponent
 * introducer 'e' in a floating-point literal */
#define TP_DOUBLE_AS_CHAR_LENGTH \
    (4 + TP_DOUBLE_MANTISA_DECIMAL_PRECISION + \
    TP_DOUBLE_EXPONENT_DECIMAL_PRECISION)

#define TP_MONETARY_MANTISA_PRECISION	  TP_DOUBLE_MANTISA_DECIMAL_PRECISION
#define TP_MONETARY_EXPONENT_PRECISION	  TP_DOUBLE_EXPONENT_DECIMAL_PRECISION
#define TP_MONETARY_PRECISION		  TP_DOUBLE_DECIMAL_PRECISION
#define TP_MONETARY_AS_CHAR_LENGTH	  TP_DOUBLE_AS_CHAR_LENGTH

#define TP_FLOAT_MANTISA_BINARY_PRECISION   24
#define TP_FLOAT_EXPONENT_BINARY_PRECISION  8
#define TP_FLOAT_BINARY_PRECISION	    TP_FLOAT_MANTISA_BINARY_PRECISION

#define TP_FLOAT_MANTISA_DECIMAL_PRECISION    7	/* 7.225 */
#define TP_FLOAT_EXPONENT_DECIMAL_PRECISION   2	/* 2.408 */
#define TP_FLOAT_DECIMAL_PRECISION	    TP_FLOAT_MANTISA_DECIMAL_PRECISION
#define TP_FLOAT_AS_CHAR_LENGTH \
    (4 + TP_FLOAT_MANTISA_DECIMAL_PRECISION + \
    TP_FLOAT_EXPONENT_DECIMAL_PRECISION)

#define TP_BIGINT_PRECISION	      19
#define TP_BIGINT_SCALE		      0
#define TP_BIGINT_AS_CHAR_LENGTH      20

#define TP_INTEGER_PRECISION	      10
#define TP_INTEGER_AS_CHAR_LENGTH     11

#define TP_SMALLINT_PRECISION	      5
#define TP_SMALLINT_AS_CHAR_LENGTH    6

#define TP_TIME_PRECISION	      6
#define TP_TIME_AS_CHAR_LENGTH	      11

#define TP_DATE_PRECISION	      8
#define TP_DATE_AS_CHAR_LENGTH	      10

#define TP_TIMESTAMP_PRECISION	      14
#define TP_TIMESTAMP_AS_CHAR_LENGTH   22
#define TP_TIMESTAMPTZ_AS_CHAR_LENGTH   64

#define TP_DATETIME_PRECISION	      17
#define TP_DATETIME_AS_CHAR_LENGTH    26
#define TP_DATETIMETZ_AS_CHAR_LENGTH    64

/* CHAR type and VARCHAR type are compatible with each other */
/* NCHAR type and VARNCHAR type are compatible with each other */
/* BIT type and VARBIT type are compatible with each other */
/* OID type and OBJECT type are compatible with each other */
/* Keys can come in with a type of DB_TYPE_OID, but the B+tree domain
   itself will always be a DB_TYPE_OBJECT. The comparison routines
   can handle OID and OBJECT as compatible type with each other . */
#define TP_ARE_COMPARABLE_KEY_TYPES(key1_type, key2_type) \
      (((key1_type) == (key2_type)) || \
      (((key1_type) == DB_TYPE_CHAR || (key1_type) == DB_TYPE_VARCHAR) && \
       ((key2_type) == DB_TYPE_CHAR || (key2_type) == DB_TYPE_VARCHAR)) || \
      (((key1_type) == DB_TYPE_NCHAR || (key1_type) == DB_TYPE_VARNCHAR) && \
       ((key2_type) == DB_TYPE_NCHAR || (key2_type) == DB_TYPE_VARNCHAR)) || \
      (((key1_type) == DB_TYPE_BIT || (key1_type) == DB_TYPE_VARBIT) && \
       ((key2_type) == DB_TYPE_BIT || (key2_type) == DB_TYPE_VARBIT)) || \
      (((key1_type) == DB_TYPE_OID || (key1_type) == DB_TYPE_OBJECT) && \
       ((key2_type) == DB_TYPE_OID || (key2_type) == DB_TYPE_OBJECT)))

#define TP_DOMAIN_TYPE(dom) \
   ((dom) ? (dom)->type->id : DB_TYPE_NULL)

#define TP_TYPE_HAS_COLLATION(typeid) \
  (TP_IS_CHAR_TYPE(typeid) || (typeid) == DB_TYPE_ENUMERATION)

#define TP_DOMAIN_CODESET(dom) (((dom) ? (INTL_CODESET)(dom)->codeset : LANG_SYS_CODESET))
#define TP_DOMAIN_COLLATION(dom) \
    ((dom) ? (dom)->collation_id : LANG_SYS_COLLATION)
#define TP_DOMAIN_COLLATION_FLAG(dom) \
  ((dom) ? (dom)->collation_flag: TP_DOMAIN_COLL_NORMAL)

#define TP_TYPE_NOT_SUPPORT_COVERING(typeid) \
   ((typeid) == DB_TYPE_TIMESTAMPTZ || (typeid) == DB_TYPE_DATETIMETZ)


/*
 * FUNCTIONS
 */
#ifdef __cplusplus
extern "C"
{
#endif

/* called during workspace initialization */

  extern void tp_area_init (void);

/* Domain support functions */

  extern int tp_init (void);
  extern void tp_apply_sys_charset (void);
  extern void tp_final (void);
  extern TP_DOMAIN *tp_domain_resolve (DB_TYPE domain_type, DB_OBJECT * class_obj, int precision, int scale,
				       TP_DOMAIN * setdomain, int collation);
  extern TP_DOMAIN *tp_domain_resolve_default (DB_TYPE type);
  extern TP_DOMAIN *tp_domain_resolve_default_w_coll (DB_TYPE type, int coll_id, TP_DOMAIN_COLL_ACTION coll_flag);

  extern void tp_domain_init (TP_DOMAIN * domain, DB_TYPE type_id);
  extern void tp_domain_free (TP_DOMAIN * dom);
  extern TP_DOMAIN *tp_domain_new (DB_TYPE type);
  extern int tp_domain_copy_enumeration (DB_ENUMERATION * dest, const DB_ENUMERATION * src);
  extern TP_DOMAIN *tp_domain_copy (const TP_DOMAIN * dom, bool check_cache);
  extern TP_DOMAIN *tp_domain_construct (DB_TYPE domain_type, DB_OBJECT * class_obj, int precision, int scale,
					 TP_DOMAIN * setdomain);

  extern void tp_init_value_domain (TP_DOMAIN * domain, DB_VALUE * value);

  extern TP_DOMAIN *tp_domain_cache (TP_DOMAIN * domain);

  extern int tp_domain_add (TP_DOMAIN ** dlist, TP_DOMAIN * domain);

  extern int tp_domain_drop (TP_DOMAIN ** dlist, TP_DOMAIN * domain);

  extern int tp_domain_filter_list (TP_DOMAIN * dlist, int *list_changes);

  extern int tp_domain_size (const TP_DOMAIN * domain);

  extern int tp_setdomain_size (const TP_DOMAIN * domain);

  extern int tp_domain_match (const TP_DOMAIN * dom1, const TP_DOMAIN * dom2, TP_MATCH exact);
  extern int tp_domain_match_ignore_order (const TP_DOMAIN * dom1, const TP_DOMAIN * dom2, TP_MATCH exact);
  extern int tp_domain_compatible (const TP_DOMAIN * dom1, const TP_DOMAIN * dom2);

  extern TP_DOMAIN *tp_domain_select (const TP_DOMAIN * domain_list, const DB_VALUE * value, int allow_coercion,
				      TP_MATCH exact_match);

#if !defined (SERVER_MODE)
  extern TP_DOMAIN *tp_domain_select_type (const TP_DOMAIN * domain_list, DB_TYPE type, DB_OBJECT * class_mop,
					   int allow_coercion);
#endif

  extern TP_DOMAIN_STATUS tp_domain_check (const TP_DOMAIN * domain, const DB_VALUE * value, TP_MATCH exact_match);
  TP_DOMAIN *tp_domain_find_noparam (DB_TYPE type, bool is_desc);
  TP_DOMAIN *tp_domain_find_numeric (DB_TYPE type, int precision, int scale, bool is_desc);
  TP_DOMAIN *tp_domain_find_charbit (DB_TYPE type, int codeset, int collation_id, unsigned char collation_flag,
				     int precision, bool is_desc);
  TP_DOMAIN *tp_domain_find_object (DB_TYPE type, OID * class_oid, struct db_object *class_, bool is_desc);
  TP_DOMAIN *tp_domain_find_set (DB_TYPE type, TP_DOMAIN * setdomain, bool is_desc);
  TP_DOMAIN *tp_domain_find_enumeration (const DB_ENUMERATION * enumeration, bool is_desc);
  TP_DOMAIN *tp_domain_resolve_value (const DB_VALUE * val, TP_DOMAIN * dbuf);
#if defined(ENABLE_UNUSED_FUNCTION)
  TP_DOMAIN *tp_create_domain_resolve_value (DB_VALUE * val, TP_DOMAIN * domain);
#endif				/* ENABLE_UNUSED_FUNCTION */
  int tp_can_steal_string (const DB_VALUE * val, const DB_DOMAIN * desired_domain);
  bool tp_domain_references_objects (const TP_DOMAIN * dom);

  int tp_get_fixed_precision (DB_TYPE domain_type);

/* value functions */

  extern TP_DOMAIN_STATUS tp_value_coerce (const DB_VALUE * src, DB_VALUE * dest, const TP_DOMAIN * desired_domain);
  extern int tp_value_coerce_strict (const DB_VALUE * src, DB_VALUE * dest, const TP_DOMAIN * desired_domain);

  extern TP_DOMAIN_STATUS tp_value_cast (const DB_VALUE * src, DB_VALUE * dest, const TP_DOMAIN * desired_domain,
					 bool implicit_coercion);

  extern TP_DOMAIN_STATUS tp_value_cast_force (const DB_VALUE * src, DB_VALUE * dest,
					       const TP_DOMAIN * desired_domain, bool implicit_coercion);

  extern TP_DOMAIN_STATUS tp_value_cast_preserve_domain (const DB_VALUE * src, DB_VALUE * dest,
							 const TP_DOMAIN * desired_domain, bool implicit_coercion,
							 bool preserve_domain);

  extern TP_DOMAIN_STATUS tp_value_cast_no_domain_select (const DB_VALUE * src, DB_VALUE * dest,
							  const TP_DOMAIN * desired_domain, bool implicit_coercion);
  TP_DOMAIN_STATUS tp_value_change_coll_and_codeset (DB_VALUE * src, DB_VALUE * dest, int coll_id, int codeset);

  extern int tp_value_equal (const DB_VALUE * value1, const DB_VALUE * value2, int allow_coercion);

  extern int tp_more_general_type (const DB_TYPE type1, const DB_TYPE type2);

  extern DB_VALUE_COMPARE_RESULT tp_value_compare (const DB_VALUE * value1, const DB_VALUE * value2, int allow_coercion,
						   int total_order);

  extern DB_VALUE_COMPARE_RESULT tp_value_compare_with_error (const DB_VALUE * value1, const DB_VALUE * value2,
							      int allow_coercion, int total_order, bool * can_compare);

  extern DB_VALUE_COMPARE_RESULT tp_set_compare (const DB_VALUE * value1, const DB_VALUE * value2, int allow_coercion,
						 int total_order);

/* printed representations */

  extern int tp_domain_name (const TP_DOMAIN * domain, char *buffer, int maxlen);
  extern int tp_value_domain_name (const DB_VALUE * value, char *buffer, int maxlen);

/* misc info */

  extern int tp_domain_disk_size (TP_DOMAIN * domain);
  extern int tp_domain_memory_size (TP_DOMAIN * domain);
  extern TP_DOMAIN_STATUS tp_check_value_size (TP_DOMAIN * domain, DB_VALUE * value);

  extern int tp_valid_indextype (DB_TYPE type);
#if defined(CUBRID_DEBUG)
  extern void tp_dump_domain (TP_DOMAIN * domain);
  extern void tp_domain_print (TP_DOMAIN * domain);
  extern void tp_domain_fprint (FILE * fp, TP_DOMAIN * domain);
#endif
  extern int tp_domain_attach (TP_DOMAIN ** dlist, TP_DOMAIN * domain);

  extern TP_DOMAIN_STATUS tp_value_auto_cast (const DB_VALUE * src, DB_VALUE * dest, const TP_DOMAIN * desired_domain);
  extern int tp_value_str_auto_cast_to_number (DB_VALUE * src, DB_VALUE * dest, DB_TYPE * val_type);
  extern TP_DOMAIN *tp_infer_common_domain (TP_DOMAIN * arg1, TP_DOMAIN * arg2);
  extern int tp_value_string_to_double (const DB_VALUE * value, DB_VALUE * result);
  extern void tp_domain_clear_enumeration (DB_ENUMERATION * enumeration);
  extern int tp_enumeration_to_varchar (const DB_VALUE * src, DB_VALUE * result);
  extern int tp_domain_status_er_set (TP_DOMAIN_STATUS status, const char *file_name, const int line_no,
				      const DB_VALUE * src, const TP_DOMAIN * domain);
#ifdef __cplusplus
}
#endif
#endif				/* _OBJECT_DOMAIN_H_ */
