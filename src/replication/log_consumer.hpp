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
 * log_consumer.hpp
 */

#ident "$Id$"

#ifndef _LOG_CONSUMER_HPP_
#define _LOG_CONSUMER_HPP_

#include "packing_stream.hpp"
#include <cstddef>

namespace cubreplication
{
  class slave_replication_channel;
  class replication_stream_entry;

  /*
   * main class for consuming log packing stream entries;
   * it should be created only as a global instance
   */
  class log_consumer
  {
    private:
      std::vector<replication_stream_entry *> m_stream_entries;

      cubstream::packing_stream *m_stream;

      /* start append position */
      cubstream::stream_position m_start_position;

      static log_consumer *global_instance;

    public:

      log_consumer () {} ;

      ~log_consumer ();

      int append_entry (replication_stream_entry *entry);

      int fetch_stream_entry (replication_stream_entry *&entry);

      int consume_thread (void);

      static log_consumer *new_instance (const cubstream::stream_position &start_position);

      cubstream::packing_stream *get_stream (void)
      {
	return m_stream;
      };

      cubstream::stream_position &get_start_position ()
      {
	return m_start_position;
      };
  };

} /* namespace cubreplication */

#endif /* _LOG_CONSUMER_HPP_ */
