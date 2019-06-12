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

#include <mutex>

namespace cubreplication
{
  class master_ctrl;

  class master_node : public replication_node
  {
    private:
      static master_node *g_instance;
      static std::mutex g_enable_active_mtx;

      master_node (const char *name)
	: replication_node (name)
      {
      }

    public:
      master_ctrl *m_control_channel_manager;

      static master_node *get_instance (const char *name);

      static void init (const char *name);
      static void new_slave (int fd);
      static void add_ctrl_chn (int fd);
      static void final (void);

      static void enable_active (void);

      static void update_senders_min_position (const cubstream::stream_position &pos);
  };

} /* namespace cubreplication */

#endif /* _REPLICATION_MASTER_NODE_HPP_ */
