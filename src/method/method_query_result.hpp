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

#ifndef _METHOD_QUERY_RESULT_HPP_
#define _METHOD_QUERY_RESULT_HPP_

#ident "$Id$"

#if defined (SERVER_MODE)
#error Does not belong to server module
#endif /* SERVER_MODE */

#include <vector>
#include "dbtype_def.h"

namespace cubmethod
{
  struct query_result
  {
    query_result ();

    DB_QUERY_TYPE *column_info;
    DB_QUERY_RESULT *result;
    // TODO: column info
    std::vector<char> null_type_column;
    // char col_updatable;
    // T_COL_UPDATE_INFO *m_col_update_info;
    // bool is_holdable : not supported
    int stmt_id;
    int stmt_type;
    int num_column;
    int tuple_count;
    bool copied;
    bool include_oid;

    void clear ();
  };
}		// namespace cubmethod
#endif				/* _METHOD_QUERY_RESULT_HPP_ */
