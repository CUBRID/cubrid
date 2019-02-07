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

#include "load_sa_loader.hpp"
#include "message_catalog.h"

namespace cubload
{

  error_handler::error_handler (lineno_function &lineno_function)
    : m_lineno_function (lineno_function)
#if defined (SERVER_MODE)
    , m_session (NULL)
#endif
  {
    //
  }

#if defined (SERVER_MODE)
  void
  error_handler::initialize (session *session)
  {
    if (m_session != NULL)
      {
	assert (false);
	return;
      }

    m_session = session;
  }
#endif

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
  error_handler::log_error_message (std::string &err_msg, bool fail)
  {
#if defined (SERVER_MODE)
    if (er_errid () != NO_ERROR && (er_has_error () || er_get_severity () == ER_WARNING_SEVERITY))
      {
	// if there is an error set via er_set then report it as well
	err_msg.append (format (get_message_from_catalog (LOADDB_MSG_LINE), m_lineno_function ()));
	err_msg.append (std::string (er_msg ()));
	err_msg.append ("\n");
      }

    m_session->on_error (err_msg);

    if (fail)
      {
	m_session->fail ();
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
