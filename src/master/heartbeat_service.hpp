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
 * heartbeat_service.hpp - heartbeat communication service
 */

#ifndef _HEARTBEAT_SERVICE_HPP_
#define _HEARTBEAT_SERVICE_HPP_

#include "hostname.hpp"
#include "packable_object.hpp"

namespace cubhb
{
  // forward declarations
  class udp_server;
  class server_request;
  class cluster;

  class heartbeat_service
  {
    public:
      heartbeat_service (udp_server &server, cluster &cluster_);
      ~heartbeat_service () = default;

      heartbeat_service (heartbeat_service &&other) = delete;
      heartbeat_service &operator= (heartbeat_service &&other) = delete;
      heartbeat_service (const heartbeat_service &other) = delete;
      heartbeat_service &operator= (const heartbeat_service &other) = delete;

      void handle_heartbeat (server_request &request);
      void send_heartbeat (const cubbase::hostname_type &node_hostname);

    private:
      udp_server &m_server;
      cluster &m_cluster;
  };

  class heartbeat_arg : public cubpacking::packable_object
  {
    public:
      heartbeat_arg ();
      heartbeat_arg (bool is_request, const cubbase::hostname_type &dest_hostname, const cluster &cluster_);

      const bool &is_request () const;
      int get_state () const;
      const std::string &get_group_id () const;
      const cubbase::hostname_type &get_orig_hostname () const;
      const cubbase::hostname_type &get_dest_hostname () const;

      size_t get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const override;
      void pack (cubpacking::packer &serializator) const override;
      void unpack (cubpacking::unpacker &deserializator) override;

    private:
      bool m_is_request;
      int m_state;
      std::string m_group_id;
      cubbase::hostname_type m_orig_hostname;
      cubbase::hostname_type m_dest_hostname;
  };

} /* namespace cubhb */

#endif /* _HEARTBEAT_SERVICE_HPP_ */
