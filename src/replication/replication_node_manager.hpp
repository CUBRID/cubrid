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
 * replication_node_manager.hpp
 */


#ifndef _REPLICATION_NODE_MANAGER_HPP_
#define _REPLICATION_NODE_MANAGER_HPP_

namespace cubstream
{
  class multi_thread_stream;
  class stream_file;
}

namespace cubreplication
{
  class master_node;
  class replication_node;
  class slave_node;

  class replication_node_manager
  {
    public:
      static void init_hostname (const char *name);
      static replication_node_manager *get_instance ();
      static void finalize ();

      void commute_to_master_state ();
      void commute_to_slave_state ();

      master_node *get_master_node ();
      slave_node *get_slave_node ();

    private:
      replication_node_manager ();
      ~replication_node_manager ();

      cubstream::stream_file *m_stream_file;
      cubstream::multi_thread_stream *m_stream;
      cubreplication::replication_node *m_repl_node;
  };
}

#endif
