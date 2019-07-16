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

//
// manage logging and replication for sub-transactions (changes of serial/click counter)
//

#include "replication_subtran_generate.hpp"

#include "cubstream.hpp"
#include "log_generator.hpp"
#include "log_manager.h"
#include "thread_manager.hpp"

namespace cubreplication
{
  subtran_generate::subtran_generate ()
    : m_generator (cubreplication::log_generator::get_global_stream ())
    , m_started (false)
  {
    m_generator.set_row_replication_disabled (false);
  }

  subtran_generate::~subtran_generate ()
  {
    if (m_started)
      {
	// should be manually stopped; but make sure we don't "leak" sysops
	assert (false);
	abort ();
      }
  }

  void
  subtran_generate::start ()
  {
    log_sysop_start (&cubthread::get_entry ());
  }

  void
  subtran_generate::commit ()
  {
    cubstream::stream_position start_stream_pos;

    m_generator.on_subtran_commit ();
    start_stream_pos = m_generator.pack_stream_entry ();
    log_sysop_commit_replicated (&cubthread::get_entry (), start_stream_pos);
  }

  void
  subtran_generate::abort ()
  {
    log_sysop_abort (&cubthread::get_entry ());
  }

  log_generator &
  subtran_generate::get_repl_generator ()
  {
    return m_generator;
  }

} // namespace cubreplication
