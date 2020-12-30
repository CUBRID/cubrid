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
 * load_db_value_converter.hpp - conversion from string to DB_VALUE
 */

#ifndef _LOAD_DB_VALUE_CONVERTER_HPP_
#define _LOAD_DB_VALUE_CONVERTER_HPP_

#include "dbtype_def.h"
#include "load_common.hpp"

namespace cubload
{
  // forward declaration
  class attribute;

  typedef int (*conv_func) (const char *, const size_t, const attribute *, db_value *);

  conv_func &get_conv_func (const data_type ldr_type, const DB_TYPE db_type);

} // namespace cubload

#endif /* _LOAD_DB_VALUE_CONVERTER_HPP_ */
