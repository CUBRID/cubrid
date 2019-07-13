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

namespace cubcomm
{
  class channel;
}

namespace cubthread
{
  class daemon;
}

namespace cubreplication
{
  class log_consumer;
  class slave_control_sender;

  class slave_node : public replication_node
  {
    private:
      /* difference in bytes (stream positions) between slave recovered (start) position and 
       * source available position which is acceptable to start replication wihout replication copy db phase */
      const static long long ACCEPTABLE_POS_DIFF_BEFORE_COPY = 100000;

      static slave_node *g_instance;
      log_consumer *m_lc;

      node_definition m_master_identity;
      cubstream::transfer_receiver *m_transfer_receiver;
      cubthread::daemon *m_ctrl_sender_daemon;
      slave_control_sender *m_ctrl_sender;

      cubstream::stream_position m_source_available_pos;

      slave_node (const char *name)
	: replication_node (name)
	, m_lc (NULL)
	, m_master_identity ("")
	, m_transfer_receiver (NULL)
	, m_ctrl_sender_daemon (NULL)
	, m_ctrl_sender (NULL)
      {
         m_source_available_pos = std::numeric_limits<cubstream::stream_position>::max (); 
      }

      ~slave_node ();

    public:
      static slave_node *get_instance (const char *name);

      static void init (const char *hostname);
      static int connect_to_master (const char *master_node_hostname, const int master_node_port_id);
      static int start_online_replication (cubcomm::server_channel &srv_chn,
                                           const cubstream::stream_position start_position);
      static void final (void);
      static void stop_and_destroy_online_repl (void);
      
      int setup_protocol (cubcomm::channel &chn);

      bool need_replication_copy (const cubstream::stream_position start_position) const;

      int replication_copy_slave (cubthread::entry &entry, node_definition *source_node,
                                  const bool start_replication_after_copy);
  };


  
} /* namespace cubreplication */

#endif /* _REPLICATION_SLAVE_NODE_HPP_ */
