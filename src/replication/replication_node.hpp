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
 * replication_node.hpp
 */

#ident "$Id$"

#ifndef _REPLICATION_NODE_HPP_
#define _REPLICATION_NODE_HPP_

#include "cubstream.hpp"

namespace cubstream
{
  class multi_thread_stream;
  class stream_file;
};

namespace cubreplication
{
  /*
   * definition of a (replication) node (machine)
   * this is used to store virtual nodes (without actually being connected)
   */
  class node_definition
  {
    private:
      std::string m_hostname;
      std::string m_ip_address;
      int m_data_port;

    public:
      node_definition (const char *hostname)
	: m_hostname (hostname),
	  m_data_port (-1)
      {
      }

      void set_hostname (const char *hostname)
      {
	m_hostname = hostname;
      }

      void set_port (const int port)
      {
	m_data_port = port;
      }

      const std::string &get_hostname (void) const
      {
	return m_hostname;
      }

      int get_port () const
      {
	return m_data_port;
      }
  };

  /* base class for collection of data and methods of a replication node (actually running) */
  class replication_node
  {
    public:
      const static unsigned long long SETUP_REPLICATION_MAGIC;
      const static unsigned long long SETUP_COPY_REPLICATION_MAGIC;
      const static unsigned long long SETUP_COPY_END_REPLICATION_MAGIC;

    protected:
      cubstream::multi_thread_stream *m_stream;
      cubstream::stream_file *m_stream_file;
      node_definition m_identity;

      replication_node (const char *name)
	: m_identity (name)
      {
	m_stream = NULL;
	m_stream_file = NULL;
      }

    public:

      virtual ~replication_node ();

      const node_definition &get_node_identity () const
      {
	return m_identity;
      }

      static std::string get_replication_file_path ();
  };

} /* namespace cubreplication */

#endif /* _REPLICATION_NODE_HPP_ */
