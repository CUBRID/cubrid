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

namespace cubload
{

#if defined (SERVER_MODE)
  error_handler::error_handler (session &session)
    : m_session (session)
  {
    m_syntax_check = m_session.get_args ().syntax_check;
  }
#endif

  int
  error_handler::get_lineno ()
  {
    cubthread::entry &thread_ref = cubthread::get_entry ();
    assert (thread_ref.m_loaddb_driver != NULL);

    return thread_ref.m_loaddb_driver->get_scanner ().lineno () + 1;
  }

  char *
  error_handler::get_message_from_catalog (MSGCAT_LOADDB_MSG msg_id)
  {
    return msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_LOADDB, msg_id);
  }

  void
  error_handler::on_failure ()
  {
    std::string empty;
    log_error_message (empty, true);
  }

  void
  error_handler::on_syntax_failure ()
  {
#if defined (SERVER_MODE)
    if (m_syntax_check)
      {
	// Do not do anything here
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

} // namespace cubload
