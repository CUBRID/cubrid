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

/*
 * replication_db_copy.hpp
 */

#ident "$Id$"

#ifndef _REPLICATION_DB_COPY_HPP_
#define _REPLICATION_DB_COPY_HPP_

#include "replication_object.hpp"

namespace cubstream
{
  class multi_thread_stream;
};

namespace cubreplication
{
  class row_object;

  class copy_context
  {
    public:
      enum copy_stage
      {
	NOT_STARTED = 0,
	SCHEMA_APPLY_CLASSES,
	SCHEMA_APPLY_CLASSES_FINISHED,
	SCHEMA_TRIGGERS_RECEIVED,
	SCHEMA_INDEXES_RECEIVED,
	HEAP_COPY,
	HEAP_COPY_FINISHED,
	SCHEMA_APPLY_TRIGGERS_INDEXES,
	SCHEMA_APPLY_TRIGGERS_INDEXES_FINISHED
      };

      copy_context ();

      ~copy_context () = default;

      void set_credentials (const char *user);

      void pack_and_add_object (row_object &obj);
      void pack_and_add_sbr (sbr_repl_entry &sbr);

      int transit_state (copy_stage new_state);

      void append_class_schema (const char *buffer, const size_t buf_size);
      void append_triggers_schema (const char *buffer, const size_t buf_size);
      void append_indexes_schema (const char *buffer, const size_t buf_size);

    private:
      cubstream::multi_thread_stream *m_stream;

      sbr_repl_entry m_class_schema;
      sbr_repl_entry m_triggers;
      sbr_repl_entry m_indexes;

      copy_stage m_state;
  };

} /* namespace cubreplication */

#endif /* _REPLICATION_DB_COPY_HPP_ */
