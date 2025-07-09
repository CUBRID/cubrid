/*
 * Copyright 2008 Search Solution Corporation
 * Copyright 2016 CUBRID Corporation
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
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

#include <algorithm>
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

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
  error_handler::get_driver_lineno ()
  {
    cubthread::entry &thread_ref = cubthread::get_entry ();
    assert (thread_ref.m_loaddb_driver != NULL);

    // We actually don't need the increment since we already incremented during update_start_line
    return thread_ref.m_loaddb_driver->get_start_line ();
  }

  int
  error_handler::get_scanner_lineno ()
  {
    cubthread::entry &thread_ref = cubthread::get_entry ();
    assert (thread_ref.m_loaddb_driver != NULL);

    return thread_ref.m_loaddb_driver->get_scanner ().lineno ();
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
  error_handler::on_syntax_failure (bool use_scanner_line)
  {
#if defined (SERVER_MODE)
    if (m_syntax_check)
      {
	// just log er_msg ()
	std::string er_msg;
	log_error_message (er_msg, false, use_scanner_line);
	er_clear ();
	return;
      }
#endif
    if (!is_last_error_filtered ())
      {
	std::string empty;
	log_error_message (empty, true, use_scanner_line);
      }
  }

  void
  error_handler::log_error_message (std::string &err_msg, bool fail, bool is_syntax_error)
  {
#if defined (SERVER_MODE)
    if (er_errid () != NO_ERROR && (er_has_error () || er_get_severity () == ER_WARNING_SEVERITY))
      {
	// if there is an error set via er_set then report it as well
	int lineno;
	if (!is_syntax_error)
	  {
	    lineno = get_driver_lineno ();
	  }
	else
	  {
	    lineno = get_scanner_lineno ();
	  }

	err_msg.append (format (get_message_from_catalog (LOADDB_MSG_LINE), lineno));
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
