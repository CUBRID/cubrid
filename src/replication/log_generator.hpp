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
 * log_generator.hpp
 */

#ident "$Id$"

#ifndef _LOG_GENERATOR_HPP_
#define _LOG_GENERATOR_HPP_

#include "packable_object.hpp"
#include "packing_stream.hpp"
#include "thread_compat.hpp"
#include <vector>

namespace cubreplication
{

  /*
   * main class for producing log stream entries
   * it is a templatized class : stream entries depends on the actual stream contents
   * only a global instance (per template class) is allowed
   */

  template <typename SE>
  class log_generator
  {
    private:
      std::vector<SE *> m_stream_entries;

      cubstream::packing_stream *m_stream;

      /* start append position of generator stream */
      cubstream::stream_position m_start_append_position;

      static log_generator *global_log_generator;

    public:

      log_generator ()
      {
	m_stream = NULL;
      };

      ~log_generator ()
      {
	assert (this == global_log_generator);

	delete m_stream;
	log_generator::global_log_generator = NULL;

	for (int i = 0; i < m_stream_entries.size (); i++)
	  {
	    delete m_stream_entries[i];
	    m_stream_entries[i] = NULL;
	  }
      }

      int append_repl_entry (THREAD_ENTRY *th_entry, cubpacking::packable_object *object)
      {
	replication_stream_entry *my_stream_entry = get_stream_entry (th_entry);

	my_stream_entry->add_packable_entry (object);

	return NO_ERROR;
      }


      void set_ready_to_pack (THREAD_ENTRY *th_entry)
      {
	if (th_entry == NULL)
	  {
	    size_t i;
	    for (i = 0; i < m_stream_entries.size (); i++)
	      {
		m_stream_entries[i]->set_packable (true);
	      }
	  }
	else
	  {
	    cubstream::entry *my_stream_entry = get_stream_entry (th_entry);
	    my_stream_entry->set_packable (true);
	  }
      }

      SE *get_stream_entry (THREAD_ENTRY *th_entry)
      {
	int stream_entry_idx;

	if (th_entry == NULL)
	  {
	    stream_entry_idx = 0;
	  }
	else
	  {
	    stream_entry_idx = th_entry->tran_index;
	  }

	replication_stream_entry *my_stream_entry = m_stream_entries[stream_entry_idx];
	return my_stream_entry;
      }

      int pack_stream_entries (THREAD_ENTRY *th_entry)
      {
	size_t i;
	size_t repl_entries_size = 0;

	if (th_entry == NULL)
	  {
	    for (i = 0; i < m_stream_entries.size (); i++)
	      {
		m_stream_entries[i]->pack ();
		m_stream_entries[i]->reset ();
	      }
	  }
	else
	  {
	    cubstream::entry *my_stream_entry = get_stream_entry (th_entry);
	    my_stream_entry->pack ();
	    my_stream_entry->reset ();
	  }

	return NO_ERROR;
      }

      static log_generator *new_instance (const cubstream::stream_position &start_position)
      {
	log_generator *new_lg = new log_generator ();
	new_lg->m_start_append_position = start_position;

	/* create stream only for global instance */
	/* TODO : sys params */
	new_lg->m_stream = new cubstream::packing_stream (100 * 1024 * 1024, 1000);
	new_lg->m_stream->init (new_lg->m_start_append_position);

	/* this is the global instance */
	assert (global_log_generator == NULL);
	global_log_generator = new_lg;

	/* TODO : actual number of transactions */
	new_lg->m_stream_entries.resize (1000);
	for (int i = 0; i < 1000; i++)
	  {
	    new_lg->m_stream_entries[i] = new replication_stream_entry (new_lg->m_stream);
	  }

	return new_lg;
      }

      cubstream::packing_stream *get_stream (void)
      {
	return m_stream;
      };
  };

} /* namespace cubreplication */

#endif /* _LOG_GENERATOR_HPP_ */
