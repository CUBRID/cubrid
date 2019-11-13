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
 * load_error_handler.cpp - Error handling class for loaddb functionality
 */

#include "load_error_handler.hpp"

#include "load_driver.hpp"
#include "load_sa_loader.hpp"
#if defined (SERVER_MODE)
#include "load_session.hpp"
#endif
#include "message_catalog.h"
#include "thread_manager.hpp"
#include "error_manager.h"

namespace cubload
{

#if defined (SERVER_MODE)
  error_handler::error_handler (session &session)
    : m_current_line_has_error (false)
    , m_session (session)
  {
    m_syntax_check = m_session.get_args ().syntax_check;
  }
#endif

  int
  error_handler::get_lineno ()
  {
    cubthread::entry &thread_ref = cubthread::get_entry ();
    assert (thread_ref.m_loaddb_driver != NULL);

    return thread_ref.m_loaddb_driver->get_start_line ();
  }

  char *
  error_handler::get_message_from_catalog (MSGCAT_LOADDB_MSG msg_id)
  {
    return msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_LOADDB, msg_id);
  }

  void
  error_handler::on_failure ()
  {
    if (!is_last_error_filtered ())
      {
	std::string empty;
	log_error_message (empty, true);
      }
  }

  void
  error_handler::on_syntax_failure ()
  {
#if defined (SERVER_MODE)
    if (m_syntax_check)
      {
	// just log er_msg ()
	std::string er_msg;
	log_error_message (er_msg, false);
	er_clear ();
	return;
      }
#endif
    on_failure ();
  }

  void
  error_handler::log_error_message (std::string &err_msg, bool fail)
  {
#if defined (SERVER_MODE)
    if (er_errid () != NO_ERROR && (er_has_error () || er_get_severity () == ER_WARNING_SEVERITY))
      {
	// if there is an error set via er_set then report it as well
	err_msg.append (format (get_message_from_catalog (LOADDB_MSG_LINE), get_lineno ()));
	err_msg.append (std::string (er_msg ()));
	err_msg.append ("\n");
      }

    m_session.on_error (err_msg);

    if (fail)
      {
	m_session.fail ();
      }
#elif defined (SA_MODE)
    if (fail)
      {
	ldr_increment_fails ();
      }
    else
      {
	ldr_increment_err_total ();
      }

    fprintf (stderr, "%s", err_msg.c_str ());
#endif
  }

#if defined (SERVER_MODE)
  bool
  error_handler::is_error_filtered (int err_id)
  {
    std::vector<int> ignored_errors = m_session.get_args ().m_ignored_errors;
    bool is_filtered = false;

    is_filtered = std::find (ignored_errors.begin (), ignored_errors.end (), err_id) != ignored_errors.end ();

    return is_filtered;
  }
#endif //SERVER_MODE

  bool
  error_handler::is_last_error_filtered ()
  {
#if defined (SERVER_MODE)
    int err = er_errid ();

    bool is_filtered = is_error_filtered (err);

    // Clear the error if it is filtered
    if (is_filtered)
      {
	er_clear ();
	set_error_on_current_line (true);
      }

    return is_filtered;
#endif
    return false;
  }

  bool
  error_handler::current_line_has_error ()
  {
    return m_current_line_has_error;
  }

  void
  error_handler::set_error_on_current_line (bool has_error)
  {
    m_current_line_has_error = has_error;
  }

} // namespace cubload
