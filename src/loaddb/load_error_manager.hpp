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
 * load_error_manager.hpp - TODO - add brief documentation
 */

#ifndef _LOAD_ERROR_MANAGER_HPP_
#define _LOAD_ERROR_MANAGER_HPP_

#include "message_catalog.h"
#include "utility.h"

#include <memory>
#include <string>

namespace cubload
{

  // forward declaration
  class scanner;
  class session;

  // TODO CBRD-22254 split into sa and server error_manager classes
  class error_manager
  {
    public:
      error_manager (session &session, scanner &scanner);

      template<typename ... Args>
      void on_error (MSGCAT_LOADDB_MSG msg_id, bool include_line_msg, Args ... args);

    private:
      session &m_session;
      scanner &m_scanner;

      template<typename ... Args>
      std::string format (const char *format, Args ... args);

      int get_lineno ();

#if defined (SERVER_MODE)
      void abort_session (std::string &err_msg);
#endif
  };
} // namespace cubload

namespace cubload
{

  template<typename ... Args>
  void
  error_manager::on_error (MSGCAT_LOADDB_MSG msg_id, bool include_line_msg, Args ... args)
  {
#if defined (SERVER_MODE)
    std::string err_msg_line;
    if (include_line_msg)
      {
	err_msg_line = format (msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_LOADDB, LOADDB_MSG_LINE),
			       get_lineno () - 1);
      }

    std::string err_msg = format (msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_LOADDB, msg_id), args ...);

    if (!err_msg_line.empty ())
      {
	err_msg_line.append (err_msg);
	abort_session (err_msg_line);
      }
    else
      {
	abort_session (err_msg);
      }
#endif
  }

  template<typename ... Args>
  std::string
  error_manager::format (const char *format, Args ... args)
  {
    int size = snprintf (nullptr, 0, format, args ... ) + 1; // Extra space for '\0'
    std::unique_ptr<char[]> buf (new char[size]);
    snprintf (buf.get (), (size_t) size, format, args ...);
    return std::string (buf.get (), buf.get () + size - 1); // We don't want the '\0' inside
  }
} // namespace cubload

#endif /* _LOAD_ERROR_MANAGER_HPP_ */
