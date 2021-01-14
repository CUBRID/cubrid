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

      void clear ();

      // Parse functions
      int parse (std::istream &iss, int line_offset = 0);

      class_installer &get_class_installer ();
      object_loader &get_object_loader ();
      semantic_helper &get_semantic_helper ();
      error_handler &get_error_handler ();
      scanner &get_scanner ();
      void update_start_line ();
      int get_start_line ();

    private:
      scanner *m_scanner;
      class_installer *m_class_installer;
      object_loader *m_object_loader;
      error_handler *m_error_handler;
      semantic_helper m_semantic_helper;
      int m_start_line_no;

      bool m_is_initialized;
  }; // class driver

} // namespace cubload

#endif /* _LOAD_DRIVER_HPP_ */
