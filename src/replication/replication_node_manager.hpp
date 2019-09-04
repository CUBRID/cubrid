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
#include "server_support.h"

namespace cubstream
{
  class multi_thread_stream;
  class stream_file;
}

namespace cubthread
{
  class entry;
}

namespace cubreplication
{
  class master_node;
  class replication_node;
  class slave_node;

  namespace replication_node_manager
  {
    void init (const char *name);
    void finalize ();

    master_node *get_master_node ();
    slave_node *get_slave_node ();

    void start_commute_to_master_state (cubthread::entry *thread_p, bool force);
    void start_commute_to_slave_state (cubthread::entry *thread_p, bool force);

    void wait_commute (HA_SERVER_STATE &ha_state, HA_SERVER_STATE req_state);

    void inc_ha_tasks ();
    void dec_ha_tasks ();
  };
}

#endif
