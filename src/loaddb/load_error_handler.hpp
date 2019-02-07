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
 * load_error_handler.hpp - Error handling class for loaddb functionality
 */

#ifndef _LOAD_ERROR_HANDLER_HPP_
#define _LOAD_ERROR_HANDLER_HPP_

#include "load_common.hpp"
#if defined (SERVER_MODE)
#include "load_session.hpp"
#endif
#include "utility.h"

#include <functional>
#include <memory>
#include <string>

namespace cubload
{

  using lineno_function = std::function<int ()>;

  class error_handler
  {
    public:
      error_handler (lineno_function &lineno_function);

      ~error_handler () = default; // Destructor

#if defined (SERVER_MODE)
      void initialize (session *session);
#endif

      template<typename... Args>
      void on_error (MSGCAT_LOADDB_MSG msg_id, Args &&... args);

      // just log er_msg from error_manager and fail the session
      void on_failure ();

      template<typename... Args>
      void on_error_with_line (MSGCAT_LOADDB_MSG msg_id, Args &&... args);

      template<typename... Args>
      void on_failure (MSGCAT_LOADDB_MSG msg_id, Args &&... args);

      template<typename... Args>
      void on_failure_with_line (MSGCAT_LOADDB_MSG msg_id, Args &&... args);

    private:
      char *get_message_from_catalog (MSGCAT_LOADDB_MSG msg_id);

      void log_error_message (std::string &err_msg, bool fail);

      // Format string based on format string passed as input parameter. Check snprintf function for more details
      template<typename... Args>
      std::string format (const char *fmt, Args &&... args);

      lineno_function m_lineno_function;
#if defined (SERVER_MODE)
      session *m_session;
#endif
  };
}

/************************************************************************/
/* Template implementation                                              */
/************************************************************************/

namespace cubload
{

  template<typename... Args>
  void
  error_handler::on_error (MSGCAT_LOADDB_MSG msg_id, Args &&... args)
  {
    std::string err_msg = format (get_message_from_catalog (msg_id), std::forward<Args> (args)...);
    log_error_message (err_msg, false);
  }

  template<typename... Args>
  void
  error_handler::on_error_with_line (MSGCAT_LOADDB_MSG msg_id, Args &&... args)
  {
    std::string err_msg;

    err_msg.append (format (get_message_from_catalog (LOADDB_MSG_LINE), m_lineno_function ()));
    err_msg.append (format (get_message_from_catalog (msg_id), std::forward<Args> (args)...));

    log_error_message (err_msg, false);
  }

  template<typename... Args>
  void
  error_handler::on_failure (MSGCAT_LOADDB_MSG msg_id, Args &&... args)
  {
    std::string err_msg = format (get_message_from_catalog (msg_id), std::forward<Args> (args)...);
    log_error_message (err_msg, true);
  }

  template<typename... Args>
  void
  error_handler::on_failure_with_line (MSGCAT_LOADDB_MSG msg_id, Args &&... args)
  {
    std::string err_msg;

    err_msg.append (format (get_message_from_catalog (LOADDB_MSG_LINE), m_lineno_function ()));
    err_msg.append (format (get_message_from_catalog (msg_id), std::forward<Args> (args)...));

    log_error_message (err_msg, true);
  }

  template<typename... Args>
  std::string
  error_handler::format (const char *fmt, Args &&... args)
  {
    // Determine required size
    int size = snprintf (NULL, 0, fmt, std::forward<Args> (args)...) + 1; // +1  for '\0'
    std::unique_ptr<char[]> msg (new char[size]);

    snprintf (msg.get (), (size_t) size, fmt, std::forward<Args> (args)...);

    return std::string (msg.get (), msg.get () + size - 1);
  }

} // namespace cubload

#endif /* _LOAD_ERROR_HANDLER_HPP_ */
