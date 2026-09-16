/*
 *
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
 * copy_column_types.h - The column types COPY can carry.
 *
 * Both decoders handle exactly this set, and the statement is refused when it
 * opens if a target column is outside it. The list is shared so that refusal
 * and the decoders cannot drift apart: a type added to copy_csv_decoder.cpp and
 * copy_binary_decoder.cpp belongs here too.
 */

#ifndef _COPY_COLUMN_TYPES_H_
#define _COPY_COLUMN_TYPES_H_

#include "dbtype_def.h"

#ifdef __cplusplus
extern "C"
{
#endif

  inline bool
  copy_type_is_supported (DB_TYPE type)
  {
    switch (type)
      {
      case DB_TYPE_SHORT:
      case DB_TYPE_INTEGER:
      case DB_TYPE_BIGINT:
      case DB_TYPE_FLOAT:
      case DB_TYPE_DOUBLE:
      case DB_TYPE_CHAR:
      case DB_TYPE_VARCHAR:
      case DB_TYPE_DATE:
      case DB_TYPE_TIME:
      case DB_TYPE_TIMESTAMP:
      case DB_TYPE_DATETIME:
	return true;
      default:
	return false;
      }
  }

#ifdef __cplusplus
}
#endif

#endif /* _COPY_COLUMN_TYPES_H_ */
