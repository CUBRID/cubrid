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
 * load_driver.hpp - interface for loader lexer and parser
 */

#ifndef _LOAD_DRIVER_HPP_
#define _LOAD_DRIVER_HPP_

#if !defined (SERVER_MODE) && !defined (SA_MODE)
#error Wrong module
#endif // not SERVER_MODE and not SA_MODE

#include "load_common.hpp"
#include "load_grammar.hpp"
#include "load_scanner.hpp"
#include "load_semantic_helper.hpp"

#include <istream>

namespace cubload
{

  /*
   * cubload::driver
   *
   * description
   *    A mediator class used by both lexer & grammar.
   *    The main purpose of the class is to offer interaction between flex scanner/bison parser classes and main code
   *    The class offers as well an entry point for parsing input files/string and to collects syntax errors if any.
   *    Be aware that copy c-tor and assignment operator are disable since a reference is passed to scanner and parser
   *
   * how to use
   *    cubload::driver driver;
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
  class driver
  {
    public:
      driver ();

      // Copy constructor (disabled).
      driver (const driver &copy) = delete;

      // Copy assignment operator (disabled)
      driver &operator= (const driver &other) = delete;

      // Destructor
      ~driver ();

      void initialize (class_installer *cls_installer, object_loader *obj_loader, error_handler *error_handler);
      bool is_initialized ();

      void uninitialize ();


      // Parse functions
      int parse (std::istream &iss, int line_offset = 0);

      class_installer &get_class_installer ();
      object_loader &get_object_loader ();
      semantic_helper &get_semantic_helper ();
      error_handler &get_error_handler ();
      scanner &get_scanner ();

    private:
      scanner *m_scanner;
      class_installer *m_class_installer;
      object_loader *m_object_loader;
      error_handler *m_error_handler;
      semantic_helper m_semantic_helper;

      bool m_is_initialized;
  }; // class driver

} // namespace cubload

#endif /* _LOAD_DRIVER_HPP_ */
