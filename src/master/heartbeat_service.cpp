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
 * heartbeat_service.cpp - heartbeat communication service
 */

#include "heartbeat_service.hpp"

#include "heartbeat_cluster.hpp"
#include "heartbeat_transport.hpp"

namespace cubhb
{

  heartbeat_service::heartbeat_service (transport &transport_, cluster &cluster_)
    : m_transport (transport_)
    , m_cluster (cluster_)
  {
    transport::handler_type handler = std::bind (&heartbeat_service::on_heartbeat_request, std::ref (*this),
				      std::placeholders::_1, std::placeholders::_2);
    m_transport.register_handler (HEARTBEAT, handler);
  }

  int
  heartbeat_service::send_heartbeat_request (const cubbase::hostname_type &dest_hostname) const
  {
    request_type request (dest_hostname);
    heartbeat_arg arg (true, dest_hostname, m_cluster);
    request.set_body (HEARTBEAT, arg);

    return m_transport.send_request (request);
  }

  void
  heartbeat_service::on_heartbeat_request (const request_type &request, response_type &response)
  {
    heartbeat_arg request_arg;
    request.get_body (request_arg);

    m_cluster.on_heartbeat_request (request_arg, (const sockaddr_in *) request.get_saddr ());

    // must send heartbeat response in order to avoid split-brain when heartbeat configuration changed
    if (request_arg.is_request () && !m_cluster.hide_to_demote)
      {
	heartbeat_arg response_arg (false, request_arg.get_orig_hostname (), m_cluster);
	response.set_body (HEARTBEAT, response_arg);
      }
  }

  heartbeat_arg::heartbeat_arg ()
    : m_is_request (false)
    , m_state (node_state::UNKNOWN)
    , m_group_id ()
    , m_orig_hostname ()
    , m_dest_hostname ()
  {
    //
  }

  heartbeat_arg::heartbeat_arg (bool is_request, const cubbase::hostname_type &dest_hostname, const cluster &cluster_)
    : m_is_request (is_request)
    , m_state (cluster_.get_state ())
    , m_group_id (cluster_.get_group_id ())
    , m_orig_hostname ()
    , m_dest_hostname (dest_hostname)
  {
    if (cluster_.get_myself_node () != NULL)
      {
	m_orig_hostname = cluster_.get_myself_node ()->get_hostname ();
      }
  }

  const bool &
  heartbeat_arg::is_request () const
  {
    return m_is_request;
  }

  int
  heartbeat_arg::get_state () const
  {
    return m_state;
  }

  const std::string &
  heartbeat_arg::get_group_id () const
  {
    return m_group_id;
  }

  const cubbase::hostname_type &
  heartbeat_arg::get_orig_hostname () const
  {
    return m_orig_hostname;
  }

  const cubbase::hostname_type &
  heartbeat_arg::get_dest_hostname () const
  {
    return m_dest_hostname;
  }

  size_t
  heartbeat_arg::get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const
  {
    size_t size = serializator.get_packed_bool_size (start_offset); // m_is_request
    size += serializator.get_packed_int_size (size); // m_state
    size += serializator.get_packed_string_size (m_group_id, size);
    size += m_orig_hostname.get_packed_size (serializator, size);
    size += m_dest_hostname.get_packed_size (serializator, size);

    return size;
  }

  void
  heartbeat_arg::pack (cubpacking::packer &serializator) const
  {
    serializator.pack_bool (m_is_request);
    serializator.pack_int (m_state);
    serializator.pack_string (m_group_id);
    m_orig_hostname.pack (serializator);
    m_dest_hostname.pack (serializator);
  }

  void
  heartbeat_arg::unpack (cubpacking::unpacker &deserializator)
  {
    deserializator.unpack_bool (m_is_request);
    deserializator.unpack_int (m_state);
    deserializator.unpack_string (m_group_id);
    m_orig_hostname.unpack (deserializator);
    m_dest_hostname.unpack (deserializator);
  }

} /* namespace cubhb */
