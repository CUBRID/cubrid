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
 * replication_master_node.hpp
 */

#ident "$Id$"

#ifndef _REPLICATION_MASTER_NODE_HPP_
#define _REPLICATION_MASTER_NODE_HPP_

#include "replication_node.hpp"
#include "stream_file.hpp"
#include "multi_thread_stream.hpp"

#include <mutex>

namespace cubreplication
{
  class master_ctrl;
  class slave_node;

  class master_node : public replication_node
  {
    private:
      static std::mutex g_enable_active_mtx;

    public:
      master_node (const char *nam, cubstream::multi_thread_stream *stream, cubstream::stream_file *stream_file);
      ~master_node ();
      master_ctrl *m_control_channel_manager;

      void init (const char *name);
      void new_slave (int fd);
      void add_ctrl_chn (int fd);
      void final (void);
      void enable_active (void);
      void update_senders_min_position (const cubstream::stream_position &pos);
  };

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
      replication_node_manager (const char *name);
      ~replication_node_manager ();

      cubstream::stream_file *m_stream_file;
      cubstream::multi_thread_stream *m_stream;
      cubreplication::replication_node *m_repl_node;
  };
} /* namespace cubreplication */

#endif /* _REPLICATION_MASTER_NODE_HPP_ */
