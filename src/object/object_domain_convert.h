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

/* Fixed-type numeric conversion cells. Include only from conversion implementation/resolver. */
#ifndef _OBJECT_DOMAIN_CONVERT_H_
#define _OBJECT_DOMAIN_CONVERT_H_

#include "object_domain.h"
#include <array>

enum DOMAIN_CONVERT_MODE
{
  DOMAIN_CONVERT_ASSIGN,
  DOMAIN_CONVERT_COMPARE,
  DOMAIN_CONVERT_OPERAND
};

using DOMAIN_CONVERTER = TP_DOMAIN_STATUS (*) (const DB_VALUE *, DB_VALUE *, const TP_DOMAIN *);

/* The caller handles NULL and aliasing and initializes the target domain before calling a cell. */
template <DB_TYPE SRC, DB_TYPE DST, DOMAIN_CONVERT_MODE MODE>
TP_DOMAIN_STATUS tp_value_convert_number (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain);

/* The first seven source types are also the destination types. */
constexpr DB_TYPE tp_numeric_convert_types[] = {
  DB_TYPE_SHORT, DB_TYPE_INTEGER, DB_TYPE_BIGINT, DB_TYPE_FLOAT, DB_TYPE_DOUBLE,
  DB_TYPE_MONETARY, DB_TYPE_NUMERIC, DB_TYPE_CHAR, DB_TYPE_VARCHAR
};
using DOMAIN_NUMERIC_CONVERTERS = std::array<DOMAIN_CONVERTER, 3 * 9 * 7>;
extern const DOMAIN_NUMERIC_CONVERTERS tp_numeric_convert_table;

/*
 * TP_COMPARE_COERCION
 *    Which operand tp_value_compare_with_error coerces before comparing two values of different types.
 *    The rule is shared with the server domain resolver so that one copy of it exists (dpin-07, P6).
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

DOMAIN_CONVERTER domain_lookup_converter (DB_TYPE src_type, const TP_DOMAIN *desired_domain, DOMAIN_CONVERT_MODE mode);
/* the cell tp_value_coerce runs on a value of src_type brought into a domain of another type, a JSON value excepted
 * (#356) */
DOMAIN_CONVERTER domain_lookup_coerce_converter (DB_TYPE src_type, const TP_DOMAIN *desired_domain);
/* ENUM -> its name -> DOUBLE, ASSIGN: the ENUM operand of ENUM + string without plus_as_concat (D-335-05) */
DOMAIN_CONVERTER domain_enumeration_name_converter (void);
const char *domain_converter_name (DOMAIN_CONVERTER converter);

#endif /* _OBJECT_DOMAIN_CONVERT_H_ */
