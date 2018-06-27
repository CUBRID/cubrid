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
 * driver.cpp - TODO
 */

#include <sstream>
#include "driver.hpp"

namespace cubloaddb
{

  driver::driver ()
    : m_trace_scanning (0)
    , m_trace_parsing (0)
    , m_scanner (NULL)
    , m_parser (NULL)
  {
  }

  driver::driver (int trace_scanning, int trace_parsing)
    : m_trace_scanning (trace_scanning)
    , m_trace_parsing (trace_parsing)
    , m_scanner (NULL)
    , m_parser (NULL)
  {
  }

  driver::~driver ()
  {
  }

  int
  driver::parse (std::istream &in)
  {
    m_scanner = new scanner (in, std::cout);
    m_parser = new loader_yyparser (*m_scanner, *this);

    m_scanner->set_debug (m_trace_scanning);
    m_parser->set_debug_level (m_trace_parsing);

    int ret = m_parser->parse ();

    delete m_scanner;
    m_scanner = NULL;
    delete m_parser;
    m_parser = NULL;

    return ret;
  }

  void
  driver::error (const class location &l, const std::string &m)
  {
    std::cout << "location: " << l << ", m: " << m << "\n";
  }

  void
  driver::error (const std::string &m)
  {
    std::cout << "m: " << m << "\n";
  }
} // namespace cubloaddb
