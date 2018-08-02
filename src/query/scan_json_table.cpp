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

#include "scan_json_table.hpp"

#include "access_json_table.hpp"
#include "list_file.h"
#include "query_list.h"

namespace cubscan
{
  namespace json_table
  {
    void
    scanner::init (cubxasl::json_table::spec_node &spec)
    {
      m_specp = &spec;

      // query file
      m_list_file = new qfile_list_id ();
      m_list_scan = new qfile_list_scan_id ();
      m_tuple = new qfile_tuple_record ();

      QFILE_CLEAR_LIST_ID (m_list_file);
    }

    void
    scanner::clear (void)
    {
      m_specp = NULL;

      delete m_list_file;
      delete m_list_scan;
      delete m_tuple;
    }

    void
    scanner::open (void)
    {
      // todo
    }

    void
    scanner::end (cubthread::entry *thread_p)
    {
      assert (thread_p != NULL);

      qfile_destroy_list (thread_p, m_list_file);
    }
  } // namespace json_table
} // namespace cubscan
