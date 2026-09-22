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

DOMAIN_CONVERTER domain_lookup_converter (DB_TYPE src_type, const TP_DOMAIN *desired_domain, DOMAIN_CONVERT_MODE mode);
const char *domain_converter_name (DOMAIN_CONVERTER converter);

#endif /* _OBJECT_DOMAIN_CONVERT_H_ */
