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
 * driver.hpp - TODO
 */

#ifndef _DRIVER_HPP_
#define _DRIVER_HPP_

#include <istream>

#include "scanner.hpp"
#include "loader_parser.tab.hpp"

namespace cubloaddb
{

  class driver
  {
    public:
      driver ();
      driver (int trace_scanning, int trace_parsing);
      virtual ~driver ();

      int parse (std::istream &in);

      void error (const class location &l, const std::string &m);
      void error (const std::string &m);

    private:
      int m_trace_scanning;
      int m_trace_parsing;

      cubloaddb::scanner *m_scanner;
      cubloaddb::loader_yyparser *m_parser;
  };

} // namespace cubloaddb

#endif // _DRIVER_HPP_
