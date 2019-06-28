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

#include "cubstream.hpp"

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
      static void init (const char *name);
      static void finalize ();

      // master methods
      static void new_slave (int fd);
      static void add_ctrl_chn (int fd);
      static void enable_active ();
      static void update_senders_min_position (const cubstream::stream_position &pos);

      // slave methods
      static int connect_to_master (const char *master_node_hostname, const int master_node_port_id);

    private:
      enum repl_mode
      {
	SLAVE_MODE,
	MASTER_MODE
      };

      static replication_node_manager *get_instance ();
      master_node *get_master_node ();
      slave_node *get_slave_node ();
      replication_node_manager ();
      ~replication_node_manager ();

      void commute_to_master_state ();
      void commute_to_slave_state ();

      cubstream::stream_file *m_stream_file;
      cubstream::multi_thread_stream *m_stream;
      repl_mode m_mode;
      cubreplication::replication_node *m_repl_node;
  };
}

#endif
