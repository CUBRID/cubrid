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
 * load_error_manager.cpp - TODO - add brief documentation
 */

#include "load_error_manager.hpp"

#include "load_scanner.hpp"
#include "load_session.hpp"

namespace cubload
{
  error_manager::error_manager (session &session, scanner &scanner)
    : m_session (session)
    , m_scanner (scanner)
  {
    m_scanner.set_error_manager (this);
  }

  int
  error_manager::get_lineno ()
  {
    return m_scanner.lineno ();
  }

#if defined (SERVER_MODE)
  void
  error_manager::abort_session (std::string &err_msg)
  {
    m_session.abort (std::forward<std::string> (err_msg));
  }
#endif
} // namespace cubload
