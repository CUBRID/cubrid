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

  /*
   * cubloader::loader_driver
   *
   * description
   *    A mediator class used by both lexer & grammar.
   *    The main purpose of the class is to offer interaction between flex scanner/bison parser classes and main code
   *    The class offers as well an entry point for parsing input files/string and to collects syntax errors if any.
   *    Be aware that copy c-tor and assignment operator are disable since a reference is passed to scanner and parser
   *
   * how to use
   *    cubloader::loader_driver driver;
   *    std::ifstream input (file_to_parse, std::fstream::in);
   *    // optionally input variable can be a string. e.g. std::string input = "";
   *
   *    int parse_result = driver.parse (input);
   *    if (parse_result != 0)
   *      {
   *        // handle the error
   *      }
   *    else
   *      {
   *        // parsing was done successfully
   *      }
   */
  class loader_driver
  {
    public:
      // Default constructor.
      loader_driver ();

      // Copy constructor (disabled).
      loader_driver (const loader_driver &copy) = delete;

      // Copy assignment operator (disabled)
      loader_driver &operator= (const loader_driver &other) = delete;

      // Destructor
      virtual ~loader_driver ();

      // Parse functions
      int parse (std::string &s);
      int parse (std::istream &iss);

      /**
       * Syntax error functions
       * @param loc where the syntax error is found.
       * @param msg a description of the syntax error.
       */
      void error (const location &loc, const std::string &msg);

      // Returns line number through scanner
      int lineno ();

      // Access to private members functions
      loader_scanner *get_scanner ();
      loader_semantic_helper *get_semantic_helper ();

    private:
      loader_parser *m_parser;
      loader_scanner *m_scanner;
      loader_semantic_helper *m_semantic_helper;

      // private parse function used by both parse overloaded functions
      int parse_internal (std::istream &is);

      // free dynamically allocated private members
      void destroy ();
  };
} // namespace cubloader

#endif // _LOADER_DRIVER_HPP_
