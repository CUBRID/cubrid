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
 * replication_schema_extract.hpp
 */

#ifndef _REPLICATION_SCHEMA_EXTRACT_HPP_
#define _REPLICATION_SCHEMA_EXTRACT_HPP_

#include "dbtype_def.h"
#include "mem_block.hpp"
#include "printer.hpp"
#include <list>

namespace cubreplication
{

  class net_print_output : public print_output
  {
    private:
      int m_buffer_type;
      size_t m_flush_size;
      int m_send_error_cnt;

    public:
      const static size_t DEFAULT_FLUSH_SIZE = 4096;

      net_print_output () = delete;
      net_print_output (const int buffer_type, const size_t flush_size = DEFAULT_FLUSH_SIZE);
      ~net_print_output () {}

      int flush (void);

      int send_to_network ();

      void set_buffer_type (const int buffer_type)
      {
	m_buffer_type = buffer_type;
      }

      int get_error_count ()
      {
	return m_send_error_cnt;
      }
  };

} /* namespace cubreplication */

extern int replication_schema_extract (const char *program_name);

#endif /* _REPLICATION_SCHEMA_EXTRACT_HPP_ */
