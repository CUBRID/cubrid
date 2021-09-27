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

//
// method_query_cursor.hpp
//

#ifndef _METHOD_QUERY_CURSOR_HPP_
#define _METHOD_QUERY_CURSOR_HPP_

#ident "$Id$"

#include <vector>

#include "dbtype_def.h"
#include "query_list.h" /* QUERY_ID, QFILE_LIST_ID */

// thread_entry.hpp
namespace cubthread
{
  class entry;
}

namespace cubmethod
{

  class query_cursor
  {
    public:
      query_cursor (cubthread::entry *thread_p, QUERY_ID query_id);

      int open ();
      void close ();

      void reset (QUERY_ID query_id);

      int first ();
      int last ();

      SCAN_CODE cursor (int peek);

      SCAN_CODE prev_row ();
      SCAN_CODE next_row ();

      std::vector<DB_VALUE> get_current_tuple ()
      {
	return m_current_tuple;
      }

      void clear ();

      int get_current_index ()
      {
	return m_current_row;
      }

      int get_tuple_value (int index, DB_VALUE &result);
      int get_current_oid (DB_VALUE &value);

    private:
      cubthread::entry *m_thread;

      QUERY_ID m_query_id;		/* Query id for this cursor */
      QFILE_LIST_ID *m_list_id;	/* List file identifier */
      QFILE_LIST_SCAN_ID m_scan_id;	/* scan on list_id */

      std::vector<DB_VALUE> m_current_tuple;
      int m_current_row;

      // bool is_updatable;		/* Cursor updatable ? */
      bool is_oid_included;		/* Cursor has first hidden oid col. */
  };
}		// namespace cubmethod

#endif				/* _METHOD_QUERY_CURSOR_HPP_ */