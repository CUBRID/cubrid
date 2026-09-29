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
 * object_domain_convert.h - the value converters (object_domain_convert.cpp) and what the casts of object_domain.c
 *                           share with them. Include only from the conversion code and the domain rules.
 */

#ifndef _OBJECT_DOMAIN_CONVERT_H_
#define _OBJECT_DOMAIN_CONVERT_H_

#include "numeric_opfunc.h"	/* DB_DATA_STATUS */
#include "object_domain.h"

/* The conversion a caller asks for: a cast's, an implicit coercion's, a comparison's or an arithmetic operand's */
enum DOMAIN_CONVERT_MODE
{
  DOMAIN_CONVERT_ASSIGN,	/* a cast or an assignment (tp_value_cast) */
  DOMAIN_CONVERT_IMPLICIT,	/* tp_value_coerce: ASSIGN, but for the pairs implicit coercion refuses and for
				 * collections, whose elements coerce implicitly */
  DOMAIN_CONVERT_COMPARE,	/* the coercion a comparison makes: strict */
  DOMAIN_CONVERT_OPERAND	/* the coercion an arithmetic operand takes: strict */
};

#define TP_IS_CHAR_STRING(db_val_type)					\
    (db_val_type == DB_TYPE_CHAR || db_val_type == DB_TYPE_VARCHAR)

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
 * TP_COMPARE_COERCION
 *    Which operand tp_value_compare_with_error coerces before comparing two values of different types.
 *    The rule is shared with the server domain resolver so that one copy of it exists.
 */
typedef enum tp_compare_coercion
{
  TP_COMPARE_COERCE_NONE,	/* comparable as they are */
  TP_COMPARE_COERCE_TO_DOUBLE,	/* character vs number: the character operand first, then the other, to DOUBLE */
  TP_COMPARE_COERCE_FIRST_TO_DATE,	/* character first operand to the date/time type of the second */
  TP_COMPARE_COERCE_SECOND_TO_DATE,	/* character second operand to the date/time type of the first */
  TP_COMPARE_COERCE_SECOND_TO_FIRST,	/* second operand to the more general type of the first */
  TP_COMPARE_COERCE_FIRST_TO_SECOND	/* first operand to the type of the second */
} TP_COMPARE_COERCION;
TP_COMPARE_COERCION tp_value_compare_common_domain (DB_TYPE type1, DB_TYPE type2);

TP_VALUE_CONVERTER tp_value_find_converter (DB_TYPE src_type, const TP_DOMAIN * desired_domain,
					    DOMAIN_CONVERT_MODE mode);
TP_DOMAIN_STATUS tp_value_convert (TP_VALUE_CONVERTER converter, const TP_DOMAIN * target, const DB_VALUE * source,
				   DB_VALUE * result);
/* ENUM -> its name -> DOUBLE, ASSIGN: the ENUM operand of ENUM + string without plus_as_concat */
TP_VALUE_CONVERTER domain_enumeration_name_converter (void);

/* shared by the casts of object_domain.c and the converters */
bool tp_conversion_ignore_trailing_space ();
bool tp_json_unwrap_scalar (const DB_VALUE * src, bool bool_as_string, DB_VALUE * scalar);
int tp_atof (const DB_VALUE * src, double *num_value, DB_DATA_STATUS * data_stat);
int tp_atobi (const DB_VALUE * src, DB_BIGINT * num_value, DB_DATA_STATUS * data_stat);
char *tp_ltoa (DB_BIGINT value, char *string, int radix);
void format_floating_point (char *new_string, char *rve, int ndigits, int decpt, int sign);
int bfmt_print (int bfmt, const DB_VALUE * the_db_bit, char *string, int max_size);
char *tp_ftoa_buffer (const DB_VALUE * src, DB_VALUE * result);
char *tp_dtoa_buffer (const DB_VALUE * src, DB_VALUE * result);
void tp_ftoa_char (const DB_VALUE * src, DB_VALUE * result);
void tp_ftoa_varchar (const DB_VALUE * src, DB_VALUE * result);
void tp_dtoa_char (const DB_VALUE * src, DB_VALUE * result);
void tp_dtoa_varchar (const DB_VALUE * src, DB_VALUE * result);

#endif /* _OBJECT_DOMAIN_CONVERT_H_ */
