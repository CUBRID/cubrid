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
 * heartbeat_cluster.cpp - heartbeat cluster module
 */

#include "heartbeat_cluster.hpp"

#include "heartbeat_service.hpp"
#include "master_heartbeat.hpp"
#include "master_util.h"
#include "string_buffer.hpp"

namespace cubhb
{
  void trim_str (std::string &str);

  void split_str (const std::string &str, std::string &delim, std::vector<std::string> &tokens);
}

namespace cubhb
{

  node_entry::node_entry (cubbase::hostname_type &hostname, priority_type priority)
    : hostname (hostname)
    , priority (priority)
    , state (node_state::UNKNOWN)
    , score (0)
    , heartbeat_gap (0)
    , last_recv_hbtime (std::chrono::system_clock::time_point ())
  {
    //
  }

  const cubbase::hostname_type &
  node_entry::get_hostname () const
  {
    return hostname;
  }

  bool
  node_entry::is_time_initialized () const
  {
    return last_recv_hbtime != std::chrono::system_clock::time_point ();
  }

  ping_host::ping_host (const std::string &hostname)
    : hostname (hostname)
    , result (ping_result::UNKNOWN)
  {
    //
  }

  void
  ping_host::ping ()
  {
    result = hb_check_ping (hostname.as_c_str ());
  }

  bool
  ping_host::is_ping_successful ()
  {
    return result == ping_result::SUCCESS;
  }

  const cubbase::hostname_type &
  ping_host::get_hostname () const
  {
    return hostname;
  }

  ui_node::ui_node (const cubbase::hostname_type &hostname, const std::string &group_id, ipv4_type ip_addr,
		    ui_node_result v_result)
    : hostname (hostname)
    , group_id (group_id)
    , ip_addr (ip_addr)
    , last_recv_time (std::chrono::system_clock::now ())
    , v_result (v_result)
  {
    //
  }

  const cubbase::hostname_type &
  ui_node::get_hostname () const
  {
    return hostname;
  }

  cluster::cluster (udp_server *server)
    : lock ()
    , state (node_state ::UNKNOWN)
    , nodes ()
    , myself (NULL)
    , master (NULL)
    , shutdown (false)
    , hide_to_demote (false)
    , is_isolated (false)
    , is_ping_check_enabled (false)
    , ui_nodes ()
    , ping_hosts ()
    , m_server (server)
    , m_hb_service (new heartbeat_service (*server, *this))
    , m_group_id ()
    , m_hostname ()
  {
    pthread_mutex_init (&lock, NULL);
  }

  cluster::cluster (const cluster &other)
    : lock ()
    , state (other.state)
    , nodes (other.nodes)
    , myself (other.myself)
    , master (other.master)
    , shutdown (other.shutdown)
    , hide_to_demote (other.hide_to_demote)
    , is_isolated (other.is_isolated)
    , is_ping_check_enabled (other.is_ping_check_enabled)
    , ui_nodes (other.ui_nodes)
    , ping_hosts (other.ping_hosts)
    , m_server (other.m_server)
    , m_hb_service (other.m_hb_service)
    , m_group_id (other.m_group_id)
    , m_hostname (other.m_hostname)
  {
    memcpy (&lock, &other.lock, sizeof (pthread_mutex_t));
  }

  cluster &
  cluster::operator= (const cluster &other)
  {
    memcpy (&lock, &other.lock, sizeof (pthread_mutex_t));
    state = other.state;
    nodes = other.nodes;
    if (myself != NULL)
      {
	*myself = *other.myself;
      }
    if (master != NULL)
      {
	*master = *other.master;
      }
    shutdown = other.shutdown;
    hide_to_demote = other.hide_to_demote;
    is_isolated = other.is_isolated;
    is_ping_check_enabled = other.is_ping_check_enabled;
    ui_nodes = other.ui_nodes;
    ping_hosts = other.ping_hosts;
    m_server = other.m_server;
    m_hb_service = other.m_hb_service;
    m_group_id = other.m_group_id;
    m_hostname = other.m_hostname;

    return *this;
  }

  cluster::~cluster ()
  {
    destroy ();
  }

  int
  cluster::init ()
  {
    int error_code = m_hostname.fetch ();
    if (error_code != NO_ERROR)
      {
	MASTER_ER_SET_WITH_OSERROR (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_BO_UNABLE_TO_FIND_HOSTNAME, 0);
	return ER_BO_UNABLE_TO_FIND_HOSTNAME;
      }

    is_ping_check_enabled = true;

    if (HA_GET_MODE () == HA_MODE_REPLICA)
      {
	state = node_state::REPLICA;
      }
    else
      {
	state = node_state::SLAVE;
      }

    error_code = init_nodes ();
    if (error_code != NO_ERROR)
      {
	return error_code;
      }

    if (state == node_state::REPLICA && myself != NULL)
      {
	MASTER_ER_LOG_DEBUG (ARG_FILE_LINE, "myself should be in the ha_replica_list\n");
	return ER_FAILED;
      }

    error_code = init_replica_nodes ();
    if (error_code != NO_ERROR)
      {
	return error_code;
      }

    if (myself == NULL)
      {
	MASTER_ER_LOG_DEBUG (ARG_FILE_LINE, "cannot find myself\n");
	return ER_FAILED;
      }
    if (nodes.empty ())
      {
	MASTER_ER_LOG_DEBUG (ARG_FILE_LINE, "cluster cluster node list is empty\n");
	MASTER_ER_SET (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_PRM_BAD_VALUE, 1, prm_get_name (PRM_ID_HA_NODE_LIST));
	return ER_PRM_BAD_VALUE;
      }

    init_ping_hosts ();
    bool valid_ping_host_exists = check_valid_ping_host ();
    if (!valid_ping_host_exists)
      {
	return ER_FAILED;
      }

    return error_code;
  }

  void
  cluster::destroy ()
  {
    for (const node_entry *node : nodes)
      {
	delete node;
      }
    nodes.clear ();

    for (const ui_node *node : ui_nodes)
      {
	delete node;
      }
    ui_nodes.clear ();

    ping_hosts.clear ();

    delete m_hb_service;
    delete m_server;

    pthread_mutex_destroy (&lock);
  }

  int
  cluster::reload ()
  {
    int error_code = sysprm_reload_and_init (NULL, NULL);
    if (error_code != NO_ERROR)
      {
	return error_code;
      }

    // save current state of the cluster
    cluster copy = *this;

    // clear existing hosts
    ping_hosts.clear ();
    nodes.clear ();

    error_code = init ();
    if (error_code != NO_ERROR)
      {
	// something went wrong, restore old state of the cluster
	*this = copy;
	return error_code;
      }
    if (master != NULL && find_node (master->get_hostname ()) == NULL)
      {
	// could not find master, restore old state of the cluster
	*this = copy;

	MASTER_ER_SET (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_PRM_BAD_VALUE, 1, prm_get_name (PRM_ID_HA_NODE_LIST));
	return ER_PRM_BAD_VALUE;
      }

    // re initialization went successfully
    for (node_entry *new_node : nodes)
      {
	node_entry *old_node = copy.find_node (new_node->get_hostname ());
	if (old_node != NULL)
	  {
	    // copy node members
	    *new_node = *old_node;
	  }

	if (copy.master && new_node->get_hostname () == copy.master->get_hostname ())
	  {
	    master = new_node;
	  }
      }

    state = copy.state;
    is_ping_check_enabled = copy.is_ping_check_enabled;

    // since copy instance will run out of scope, destructor will be called,
    // set these pointers to NULL because this and copy instances share same pointers
    copy.m_server = NULL;
    copy.m_hb_service = NULL;

    return NO_ERROR;
  }

  void
  cluster::stop ()
  {
    if (shutdown)
      {
	return;
      }

    master = NULL;
    myself = NULL;
    shutdown = true;
    state = node_state::UNKNOWN;
    m_server->stop ();
  }

  const cubbase::hostname_type &
  cluster::get_hostname () const
  {
    return m_hostname;
  }

  const node_state &
  cluster::get_state () const
  {
    return state;
  }

  const std::string &
  cluster::get_group_id () const
  {
    return m_group_id;
  }

  const node_entry *
  cluster::get_myself_node () const
  {
    return myself;
  }

  node_entry *
  cluster::find_node (const cubbase::hostname_type &node_hostname) const
  {
    for (node_entry *node : nodes)
      {
	if (node_hostname == node->get_hostname ())
	  {
	    return node;
	  }
      }

    return NULL;
  }

  node_entry *
  cluster::find_node_except_me (const cubbase::hostname_type &node_hostname) const
  {
    for (node_entry *node : nodes)
      {
	if (node->get_hostname () != node_hostname || m_hostname == node_hostname)
	  {
	    continue;
	  }

	return node;
      }

    return NULL;
  }

  void
  cluster::remove_ui_node (ui_node *&node)
  {
    if (node == NULL)
      {
	return;
      }

    ui_nodes.remove (node);
    delete node;
    node = NULL;
  }

  void
  cluster::cleanup_ui_nodes ()
  {
    std::chrono::system_clock::time_point now = std::chrono::system_clock::now ();
    for (ui_node *node : ui_nodes)
      {
	if ((now - node->last_recv_time) > UI_NODE_CLEANUP_TIME_IN_MSECS)
	  {
	    remove_ui_node (node);
	  }
      }
  }

  ui_node *
  cluster::find_ui_node (const cubbase::hostname_type &node_hostname, const std::string &node_group_id,
			 ipv4_type ip_addr) const
  {
    for (ui_node *node : ui_nodes)
      {
	if (node->get_hostname () == node_hostname && node->group_id == node_group_id
	    && node->ip_addr == ip_addr)
	  {
	    return node;
	  }
      }

    return NULL;
  }

  ui_node *
  cluster::insert_ui_node (const cubbase::hostname_type &node_hostname, const std::string &node_group_id,
			   ipv4_type ip_addr, const ui_node_result v_result)
  {
    assert (v_result != VALID_NODE);

    ui_node *node = find_ui_node (node_hostname, node_group_id, ip_addr);
    if (node)
      {
	return node;
      }

    node = new ui_node (node_hostname, node_group_id, ip_addr, v_result);
    ui_nodes.push_front (node);

    return node;
  }

  /*
   * check_valid_ping_host
   *   return: whether a valid ping host exists or not
   *
   * NOTE: it returns true when no ping host is specified.
   */
  bool
  cluster::check_valid_ping_host ()
  {
    if (ping_hosts.empty ())
      {
	return true;
      }

    bool valid_ping_host_exists = false;
    for (ping_host &host : ping_hosts)
      {
	host.ping ();
	if (host.is_ping_successful ())
	  {
	    valid_ping_host_exists = true;
	  }
      }

    return valid_ping_host_exists;
  }

  void
  cluster::handle_heartbeat (const heartbeat_arg &arg, ipv4_type from_ip)
  {
    if (shutdown || m_hostname != arg.get_dest_hostname ())
      {
	// if the cluster is stopped or there is a mismatch on hostname then exit
	return;
      }

    pthread_mutex_lock (&lock);

    // apply heartbeat request on unidentified host entries
    apply_heartbeat_on_ui_node (arg, from_ip);

    if (m_group_id != arg.get_group_id ())
      {
	// if there is a mismatch on group, exit
	pthread_mutex_unlock (&lock);
	return;
      }

    // apply heartbeat request on cluster member node
    bool is_state_changed = apply_heartbeat_on_node (arg);

    pthread_mutex_unlock (&lock);

    if (is_state_changed)
      {
	MASTER_ER_LOG_DEBUG (ARG_FILE_LINE, "peer node state has been changed.");
	hb_cluster_job_set_expire_and_reorder (HB_CJOB_CALC_SCORE, HB_JOB_TIMER_IMMEDIATELY);
      }
  }

  void
  cluster::send_heartbeat_to_all ()
  {
    if (myself == NULL)
      {
	// if myself is NULL then cluster is not healthy
	return;
      }

    for (node_entry *node : nodes)
      {
	if (m_hostname == node->get_hostname ())
	  {
	    continue;
	  }

	m_hb_service->send_heartbeat (node->get_hostname ());
	node->heartbeat_gap++;
      }
  }

  /**
   * @return: Whether current node received heartbeat from all nodes
   */
  bool
  cluster::is_heartbeat_received_from_all ()
  {
    std::chrono::system_clock::time_point now = std::chrono::system_clock::now ();
    std::chrono::milliseconds heartbeat_interval (prm_get_integer_value (PRM_ID_HA_HEARTBEAT_INTERVAL_IN_MSECS));

    for (node_entry *node : nodes)
      {
	if (myself != node && ((now - node->last_recv_hbtime) > heartbeat_interval))
	  {
	    return false;
	  }
      }

    return true;
  }

  ui_node_result
  cluster::is_heartbeat_valid (const cubbase::hostname_type &node_hostname, const std::string &node_group_id,
			       ipv4_type from_ip_addr) const
  {
    node_entry *node = find_node_except_me (node_hostname);
    if (node == NULL)
      {
	return UNIDENTIFIED_NODE;
      }

    if (m_group_id != node_group_id)
      {
	return GROUP_NAME_MISMATCH;
      }

    unsigned char sin_addr[4];
    int error_code = css_hostname_to_ip (node_hostname.as_c_str (), sin_addr);
    if (error_code == NO_ERROR)
      {
	if (memcmp (&sin_addr, &from_ip_addr, sizeof (in_addr)) != 0)
	  {
	    return IP_ADDR_MISMATCH;
	  }
      }
    else
      {
	return CANNOT_RESOLVE_HOST;
      }

    return VALID_NODE;
  }

  bool
  cluster::apply_heartbeat_on_node (const heartbeat_arg &arg)
  {
    bool is_state_changed = false;
    node_entry *node = find_node_except_me (arg.get_orig_hostname ());
    if (node == NULL)
      {
	MASTER_ER_LOG_DEBUG (ARG_FILE_LINE, "receive heartbeat have unknown host_name. (host_name:{%s}).\n",
			     arg.get_orig_hostname ().as_c_str ());
	return is_state_changed;
      }

    if (node->state == node_state::MASTER && node->state != (node_state) arg.get_state ())
      {
	is_state_changed = true;
      }

    node->state = (node_state) arg.get_state ();
    node->heartbeat_gap = std::max (0, (node->heartbeat_gap - 1));
    node->last_recv_hbtime = std::chrono::system_clock::now ();

    return is_state_changed;
  }

  void
  cluster::apply_heartbeat_on_ui_node (const heartbeat_arg &arg, ipv4_type from_ip)
  {
    ui_node_result result = is_heartbeat_valid (arg.get_orig_hostname (), arg.get_group_id (), from_ip);
    if (result == VALID_NODE)
      {
	// if the node is validated then do nothing
	return;
      }

    ui_node *ui_node = find_ui_node (arg.get_orig_hostname (), arg.get_group_id (), from_ip);
    if (ui_node != NULL)
      {
	if (ui_node->v_result == result)
	  {
	    // if the result is same then update time when hb was received and exit
	    ui_node->last_recv_time = std::chrono::system_clock::now ();
	    return;
	  }
	else
	  {
	    // if result differ then remove exiting node and reinsert with the new result
	    remove_ui_node (ui_node);
	  }
      }

    // add the node to unidentified host entries
    insert_ui_node (arg.get_orig_hostname (), arg.get_group_id (), from_ip, result);

    string_buffer sb;
    unsigned char *ipv4_p = (unsigned char *) &from_ip;
    sb ("Receive heartbeat from unidentified host. ");
    sb ("(host_name:'%s', group:'%s', ", arg.get_orig_hostname ().as_c_str (), arg.get_group_id ().c_str ());
    sb ("ip_addr:'%u.%u.%u.%u', ", ipv4_p[0], ipv4_p[1], ipv4_p[2], ipv4_p[3]);
    sb ("state:'%s')", hb_valid_result_string (result));

    MASTER_ER_SET (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HB_NODE_EVENT, 1, sb.get_buffer ());
  }

  void
  cluster::get_config_node_list (PARAM_ID prm_id, std::string &group, std::vector<std::string> &hostnames) const
  {
    char *prm_string_value = prm_get_string_value (prm_id);
    if (prm_string_value == NULL)
      {
	return;
      }

    std::string delimiter = "@:,";
    std::vector<std::string> tokens;
    split_str (prm_string_value, delimiter, tokens);

    group.clear ();
    hostnames.clear ();

    if (tokens.size () < 2)
      {
	return;
      }

    // first entry is group id
    group.assign (tokens.front ());

    // starting from second element, all elements are hostnames
    hostnames.resize (tokens.size () - 1);
    std::copy (tokens.begin () + 1, tokens.end (), hostnames.begin ());
  }

  int
  cluster::init_nodes ()
  {
    std::vector<std::string> hostnames;

    get_config_node_list (PRM_ID_HA_NODE_LIST, m_group_id, hostnames);
    if (hostnames.empty () || m_group_id.empty ())
      {
	MASTER_ER_SET (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_PRM_BAD_VALUE, 1, prm_get_name (PRM_ID_HA_NODE_LIST));
	return ER_PRM_BAD_VALUE;
      }

    node_entry::priority_type priority = node_entry::HIGHEST_PRIORITY;
    for (const std::string &node_hostname : hostnames)
      {
	node_entry *node = insert_host_node (node_hostname, priority);
	if (node->get_hostname () == m_hostname)
	  {
	    myself = node;
#if defined (HB_VERBOSE_DEBUG)
	    MASTER_ER_LOG_DEBUG (ARG_FILE_LINE, "find myself node (myself:%p, priority:%d)\n", myself, myself->priority);
#endif
	  }

	++priority;
      }

    return NO_ERROR;
  }

  int
  cluster::init_replica_nodes ()
  {
    std::string replica_group_id;
    std::vector<std::string> hostnames;

    get_config_node_list (PRM_ID_HA_REPLICA_LIST, replica_group_id, hostnames);
    if (hostnames.empty ())
      {
	return NO_ERROR;
      }
    if (replica_group_id != m_group_id)
      {
	MASTER_ER_LOG_DEBUG (ARG_FILE_LINE, "different group id ('ha_node_list', 'ha_replica_list')\n");
	return ER_FAILED;
      }

    for (const std::string &replica_hostname : hostnames)
      {
	node_entry *replica_node = insert_host_node (replica_hostname, node_entry::REPLICA_PRIORITY);
	if (replica_node->get_hostname () == m_hostname)
	  {
	    myself = replica_node;
	    state = node_state::REPLICA;
	  }
      }

    return NO_ERROR;
  }

  void
  cluster::init_ping_hosts ()
  {
    char *ha_ping_hosts = prm_get_string_value (PRM_ID_HA_PING_HOSTS);
    if (ha_ping_hosts == NULL)
      {
	return;
      }

    std::string delimiter = ":,";
    std::vector<std::string> tokens;
    split_str (ha_ping_hosts, delimiter, tokens);

    for (const std::string &token : tokens)
      {
	ping_hosts.emplace_front (token);
      }
  }

  node_entry *
  cluster::insert_host_node (const std::string &node_hostname, const node_entry::priority_type priority)
  {
    node_entry *node = NULL;
    cubbase::hostname_type node_hostname_ (node_hostname);

    if (node_hostname_ == "localhost")
      {
	node = new node_entry (m_hostname, priority);
      }
    else
      {
	node = new node_entry (node_hostname_, priority);
      }

    nodes.push_front (node);

    return node;
  }

  inline void
  trim_str (std::string &str)
  {
    std::string ws (" \t\f\v\n\r");

    str.erase (0, str.find_first_not_of (ws));
    str.erase (str.find_last_not_of (ws) + 1);
  }

  /*
   * split string to tokens by a specified delimiter:
   *
   * @param str: input string
   * @param delimiter: delimiter to use for split
   * @param tokens: output vector of tokens
   */
  void
  split_str (const std::string &str, std::string &delimiter, std::vector<std::string> &tokens)
  {
    if (str.empty ())
      {
	return;
      }

    std::string::size_type start = str.find_first_not_of (delimiter);
    std::string::size_type pos = str.find_first_of (delimiter, start);

    while (pos != std::string::npos)
      {
	std::string token = str.substr (start, pos - start);
	trim_str (token);
	tokens.push_back (token);

	start = str.find_first_not_of (delimiter, pos);
	pos = str.find_first_of (delimiter, start);
      }

    std::string::size_type str_length = str.length ();
    if (str_length > start) // rest
      {
	std::string token = str.substr (start, str_length - start);
	trim_str (token);
	tokens.push_back (token);
      }
  }

} // namespace cubhb
