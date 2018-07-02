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
 * loader_driver.hpp - interface for loader lexer and parser
 */

#ifndef _LOADER_DRIVER_HPP_
#define _LOADER_DRIVER_HPP_

#include <istream>

#include "loader_grammar.hpp"
#include "loader_scanner.hpp"
#include "loader_semantic_helper.hpp"

namespace cubloader
{
  // TODO CBRD-21654 add class documentation
  class loader_driver
  {
    public:
      loader_driver ();
      loader_driver (const loader_driver &copy) = delete;
      loader_driver &operator= (const loader_driver &other) = delete;

      virtual ~loader_driver ();

      int parse (std::string &s);
      int parse (std::istream &iss);

      void error (const location &l, const std::string &m);
      int lineno ();

      loader_scanner *get_scanner ();
      loader_semantic_helper *get_semantic_helper ();

    private:
      loader_parser *m_parser;
      loader_scanner *m_scanner;
      loader_semantic_helper *m_semantic_helper;

      int parse_internal (std::istream &is);
  };
} // namespace cubloader

#endif // _LOADER_DRIVER_HPP_
