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
 * heartbeat_cluster.hpp - heartbeat cluster module
 */

#ifndef _HEARTBEAT_CLUSTER_HPP_
#define _HEARTBEAT_CLUSTER_HPP_

#include "hostname.hpp"
#include "system_parameter.h"
#include "udp_rpc.hpp"

#include <chrono>
#include <list>
#include <string>
#include <vector>

namespace cubhb
{

  static const std::chrono::milliseconds UI_NODE_CACHE_TIME_IN_MSECS (60 * 1000);
  static const std::chrono::milliseconds UI_NODE_CLEANUP_TIME_IN_MSECS (3600 * 1000);

  // forward declarations
  class heartbeat_arg;
  class heartbeat_service;

  enum node_state
  {
    UNKNOWN = 0,
    SLAVE = 1,
    TO_BE_MASTER = 2,
    TO_BE_SLAVE = 3,
    MASTER = 4,
    REPLICA = 5,
    MAX
  };

  /* heartbeat node entries */
  class node_entry
  {
    public:
      using priority_type = unsigned short;

      static const priority_type HIGHEST_PRIORITY = std::numeric_limits<priority_type>::min () + 1;
      static const priority_type LOWEST_PRIORITY = std::numeric_limits<priority_type>::max ();
      static const priority_type REPLICA_PRIORITY = LOWEST_PRIORITY;

      node_entry () = delete;
      node_entry (cubbase::hostname_type &hostname, priority_type priority);
      ~node_entry () = default;

      node_entry (const node_entry &other) = default; // Copy c-tor
      node_entry &operator= (const node_entry &other) = default; // Copy assignment

      const cubbase::hostname_type &get_hostname () const;
      bool is_time_initialized () const;

    public: // TODO CBRD-22864 members should be private
      cubbase::hostname_type hostname;
      priority_type priority;
      node_state state;
      short score;
      short heartbeat_gap;
      std::chrono::system_clock::time_point last_recv_hbtime; // last received heartbeat time
  };

  /* heartbeat ping host entries */
  class ping_host
  {
    public:
      ping_host () = delete;
      explicit ping_host (const std::string &hostname);
      ~ping_host () = default;

      void ping ();
      bool is_ping_successful ();

      const cubbase::hostname_type &get_hostname () const;

      enum ping_result
      {
	UNKNOWN = -1,
	SUCCESS = 0,
	USELESS_HOST = 1,
	SYS_ERR = 2,
	FAILURE = 3
      };

    public: // TODO CBRD-22864 members should be private
      cubbase::hostname_type hostname;
      ping_result result;
  };

  enum ui_node_result
  {
    VALID_NODE = 0,
    UNIDENTIFIED_NODE = 1,
    GROUP_NAME_MISMATCH = 2,
    IP_ADDR_MISMATCH = 3,
    CANNOT_RESOLVE_HOST = 4
  };

  /* heartbeat unidentified host entries */
  class ui_node
  {
    public:
      ui_node (const cubbase::hostname_type &hostname, const std::string &group_id, ipv4_type ip_addr,
	       ui_node_result v_result);
      ~ui_node () = default;

      const cubbase::hostname_type &get_hostname () const;

    public: // TODO CBRD-22864 members should be private
      cubbase::hostname_type hostname;
      std::string group_id;
      ipv4_type ip_addr;
      std::chrono::system_clock::time_point last_recv_time;
      ui_node_result v_result;
  };

  class cluster
  {
    public:
      explicit cluster (udp_server *server);

      cluster (const cluster &other); // Copy c-tor
      cluster &operator= (const cluster &other); // Copy assignment

      ~cluster ();

      int init ();
      void destroy ();
      int reload ();
      void stop ();

      const cubbase::hostname_type &get_hostname () const;
      const node_state &get_state () const;
      const std::string &get_group_id () const;
      const node_entry *get_myself_node () const;

      void handle_heartbeat (const heartbeat_arg &arg, ipv4_type from_ip);
      void send_heartbeat_to_all ();
      bool is_heartbeat_received_from_all ();

      void cleanup_ui_nodes ();

      bool check_valid_ping_host ();

    private:
      void get_config_node_list (PARAM_ID prm_id, std::string &group, std::vector<std::string> &hostnames) const;

      int init_nodes ();
      int init_replica_nodes ();
      void init_ping_hosts ();

      node_entry *find_node (const cubbase::hostname_type &node_hostname) const;
      node_entry *find_node_except_me (const cubbase::hostname_type &node_hostname) const;

      void remove_ui_node (ui_node *&node);
      ui_node *find_ui_node (const cubbase::hostname_type &node_hostname, const std::string &node_group_id,
			     ipv4_type ip_addr) const;
      ui_node *insert_ui_node (const cubbase::hostname_type &node_hostname, const std::string &node_group_id,
			       ipv4_type ip_addr, ui_node_result v_result);

      node_entry *insert_host_node (const std::string &node_hostname, node_entry::priority_type priority);

      ui_node_result is_heartbeat_valid (const cubbase::hostname_type &node_hostname, const std::string &node_group_id,
					 ipv4_type from_ip_addr) const;

    public: // TODO CBRD-22864 members should be private
      pthread_mutex_t lock; // TODO CBRD-22864 replace with std::mutex

      node_state state;

      std::list<node_entry *> nodes;

      node_entry *myself;
      node_entry *master;

      bool shutdown;
      bool hide_to_demote;
      bool is_isolated;
      bool is_ping_check_enabled;

      std::list<ui_node *> ui_nodes;
      std::list<ping_host> ping_hosts;

    protected:
      udp_server *m_server;
      heartbeat_service *m_hb_service;

      std::string m_group_id;
      cubbase::hostname_type m_hostname;
  };

} // namespace cubhb

#endif /* _HEARTBEAT_CLUSTER_HPP_ */
