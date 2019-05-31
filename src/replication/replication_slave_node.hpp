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
 * replication_slave_node.hpp
 */

#ident "$Id$"

#ifndef _REPLICATION_SLAVE_NODE_HPP_
#define _REPLICATION_SLAVE_NODE_HPP_

#include "replication_node.hpp"
#include "communication_server_channel.hpp"

namespace cubstream
{
  class transfer_receiver;
}

namespace cubreplication
{
  class log_consumer;
  class slave_control_channel;

  class slave_node : public replication_node
  {
    private:
      static slave_node *g_instance;
      log_consumer *m_lc;

#if defined (SERVER_MODE)
      node_definition m_master_identity;
      cubstream::transfer_receiver *m_transfer_receiver;
      slave_control_channel *m_control_channel_sender;
#endif

      slave_node (const char *name)
	: replication_node (name)
	, m_lc (NULL)
#if defined (SERVER_MODE)
	, m_master_identity ("")
	, m_transfer_receiver (NULL)
	, m_control_channel_sender (NULL)
#endif
      {
      }

      ~slave_node ();

    public:
      static slave_node *get_instance (const char *name);

      static void init (const char *hostname);
      static int connect_to_master (const char *master_node_hostname, const int master_node_port_id);
      static void final (void);
  };

} /* namespace cubreplication */

#endif /* _REPLICATION_SLAVE_NODE_HPP_ */
