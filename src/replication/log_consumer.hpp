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
#include <queue>

namespace cubreplication
{
  class replication_stream_entry;

  /*
   * main class for consuming log packing stream entries;
   * it should be created only as a global instance
   */
  template <typename SE>
  class log_consumer
  {
    private:
      std::queue<SE *> m_stream_entries;

      cubstream::packing_stream *m_stream;

      /* start append position */
      cubstream::stream_position m_start_position;

      static log_consumer *global_log_consumer;

    public:

      log_consumer () {} ;

      ~log_consumer ()
      {
	assert (this == global_log_consumer);

	delete m_stream;
	global_log_consumer = NULL;
      };


      int append_entry (SE *entry)
      {
	/* TODO : split list of entries by transaction */
	m_stream_entries.push_back (entry);

	return NO_ERROR;
      };

      int fetch_stream_entry (SE *&entry)
      {
	int err = NO_ERROR;

	SE *se = new SE (get_stream ());

	err = se->prepare ();
	if (err != NO_ERROR)
	  {
	    return err;
	  }

	entry = se;

	return err;
      };

      int consume_thread (void)
      {
	int err = NO_ERROR;

	for (;;)
	  {
	    replication_stream_entry *se = NULL;

	    err = fetch_stream_entry (se);
	    if (err != NO_ERROR)
	      {
		break;
	      }

	    append_entry (se);
	  }

	return NO_ERROR;
      };

      static log_consumer *new_instance (const cubstream::stream_position &start_position)
      {
	int error_code = NO_ERROR;

	log_consumer *new_lc = new log_consumer ();

	new_lc->m_start_position = start_position;

	/* TODO : sys params */
	new_lc->m_stream = new cubstream::packing_stream (10 * 1024 * 1024, 2);
	new_lc->m_stream->init (new_lc->m_start_position);

	/* this is the global instance */
	assert (global_log_consumer == NULL);
	global_log_consumer = new_lc;

	return new_lc;
      };

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
