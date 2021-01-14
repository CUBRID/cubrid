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
 * load_error_handler.hpp - Error handling class for loaddb functionality
 */

#ifndef _LOAD_ERROR_HANDLER_HPP_
#define _LOAD_ERROR_HANDLER_HPP_

#include "load_common.hpp"
#include "porting.h"
#include "utility.h"

#include <memory>
#include <string>

namespace cubload
{

  class session;

  class error_handler
  {
    public:
#if defined (SERVER_MODE)
      explicit error_handler (session &session);
#else
      error_handler () = default;
#endif

      ~error_handler () = default; // Destructor

      template<typename... Args>
      void on_error (MSGCAT_LOADDB_MSG msg_id, Args &&... args);

      // just log er_msg from error_manager and fail the session
      void on_failure ();

      // In case of syntax check argument do nothing, else keep the behavior as on_failure
      void on_syntax_failure (bool use_scanner_line = false);

      template<typename... Args>
      void on_error_with_line (MSGCAT_LOADDB_MSG msg_id, Args &&... args);

      template<typename... Args>
      void on_error_with_line (int lineno, MSGCAT_LOADDB_MSG msg_id, Args &&... args);

      template<typename... Args>
      void on_failure (MSGCAT_LOADDB_MSG msg_id, Args &&... args);

      template<typename... Args>
      void on_failure_with_line (MSGCAT_LOADDB_MSG msg_id, Args &&... args);

      template<typename... Args>
      void log_date_time_conversion_error (Args &&... args);

      template<typename... Args>
      static std::string format_log_msg (MSGCAT_LOADDB_MSG msg_id, Args &&... args);

      bool current_line_has_error ();
      void set_error_on_current_line (bool has_error);

      /*
       *  This function will return the line number at the beginning of the record. We use this in case we encounter
       *  an error during the insert part of loading the row, since the lexer in this case would have already advanced
       *  to the next record.
       */
      int get_driver_lineno ();

      /*
       *  This function will return the line number at the current position in the record in the object file. We use
       *  this in case we encounter an error during the parsing of the row, and we report the line at which the lexer
       *  is currently situated. This proves useful in case of multi-lines records, using "+" at the end of the line
       *  and will get the current line, instead of the line where the record starts.
       */
      int get_scanner_lineno ();

    private:

      // Format string based on format string passed as input parameter. Check snprintf function for more details
      template<typename... Args>
      static std::string format (const char *fmt, Args &&... args);

      static char *get_message_from_catalog (MSGCAT_LOADDB_MSG msg_id);

      void log_error_message (std::string &err_msg, bool fail, bool is_syntax_error = false);
      bool is_last_error_filtered ();

      bool m_current_line_has_error;

#if defined (SERVER_MODE)
      session &m_session;
      bool m_syntax_check;

      bool is_error_filtered (int err_id);
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
    if (get_driver_lineno () == 0)
      {
	// Parsing has not started yet!
	on_error (msg_id, std::forward<Args> (args)...);
	return;
      }

    on_error_with_line (get_driver_lineno (), msg_id, std::forward<Args> (args)...);
  }

  template<typename... Args>
  void
  error_handler::on_error_with_line (int lineno, MSGCAT_LOADDB_MSG msg_id, Args &&... args)
  {
    std::string err_msg;

    err_msg.append (format (get_message_from_catalog (LOADDB_MSG_LINE), lineno));
    err_msg.append (format (get_message_from_catalog (msg_id), std::forward<Args> (args)...));

    log_error_message (err_msg, false);
  }

  template<typename... Args>
  void
  error_handler::on_failure (MSGCAT_LOADDB_MSG msg_id, Args &&... args)
  {
    if (!is_last_error_filtered ())
      {
	std::string err_msg = format (get_message_from_catalog (msg_id), std::forward<Args> (args)...);
	log_error_message (err_msg, true);
      }
  }

  template<typename... Args>
  void
  error_handler::on_failure_with_line (MSGCAT_LOADDB_MSG msg_id, Args &&... args)
  {
    if (!is_last_error_filtered ())
      {
	std::string err_msg;

	if (get_driver_lineno () == 0)
	  {
	    // Parsing has not started yet!
	    on_failure (msg_id, std::forward<Args> (args)...);
	    return;
	  }

	err_msg.append (format (get_message_from_catalog (LOADDB_MSG_LINE), get_driver_lineno ()));
	err_msg.append (format (get_message_from_catalog (msg_id), std::forward<Args> (args)...));

	log_error_message (err_msg, true);
      }
  }

  template<typename... Args>
  std::string
  error_handler::format (const char *fmt, Args &&... args)
  {
    // Determine required size
    int size = snprintf (NULL, 0, fmt, std::forward<Args> (args)...) + 1; // +1 for '\0'
    std::unique_ptr<char[]> msg (new char[size]);

    snprintf (msg.get (), (size_t) size, fmt, std::forward<Args> (args)...);

    return std::string (msg.get (), msg.get () + size - 1);
  }

  template<typename... Args>
  void
  error_handler::log_date_time_conversion_error (Args &&... args)
  {
    std::string err_msg;

    err_msg.append (format (get_message_from_catalog (LOADDB_MSG_LINE), get_driver_lineno ()));
    err_msg.append (format (get_message_from_catalog (LOADDB_MSG_CONVERSION_ERROR), std::forward<Args> (args)...));

    log_error_message (err_msg, false);
  }

  template<typename... Args>
  std::string
  error_handler::format_log_msg (MSGCAT_LOADDB_MSG msg_id, Args &&... args)
  {
    std::string log_msg;

    log_msg.append (format (get_message_from_catalog (msg_id), std::forward<Args> (args)...));
    return log_msg;
  }
} // namespace cubload

#endif /* _LOAD_ERROR_HANDLER_HPP_ */
