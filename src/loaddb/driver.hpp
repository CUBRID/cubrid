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
 * driver.hpp - interface for loader lexer and parser
 */

#ifndef _DRIVER_HPP_
#define _DRIVER_HPP_

#include <istream>

#include "loader.h"
#include "loader_grammar.hpp"
#include "scanner.hpp"

namespace cubload
{

  // Constants sizes
  const std::size_t STRING_POOL_SIZE = 1024;
  const std::size_t MAX_COPY_BUF_SIZE = 256;
  const std::size_t COPY_BUF_POOL_SIZE = 512;
  const std::size_t CONSTANT_POOL_SIZE = 1024;
  const std::size_t QUOTED_STR_BUF_POOL_SIZE = 512;
  const std::size_t MAX_QUOTED_STR_BUF_SIZE = 32 * 1024;

  // type aliases
  using string_t = LDR_STRING;
  using constant_t = LDR_CONSTANT;
  using object_ref_t = LDR_OBJECT_REF;
  using monetary_t = LDR_MONETARY_VALUE;
  using ctor_spec_t = LDR_CONSTRUCTOR_SPEC;
  using class_cmd_spec_t = LDR_CLASS_COMMAND_SPEC;

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
      // Default constructor.
      driver ();

      // Copy constructor (disabled).
      driver (const driver &copy) = delete;

      // Copy assignment operator (disabled)
      driver &operator= (const driver &other) = delete;

      // Destructor
      virtual ~driver ();

      // Parse functions
      int parse (std::istream &iss);

      /**
       * Syntax error functions
       * @param loc where the syntax error is found.
       * @param msg a description of the syntax error.
       */
      void error (const location &loc, const std::string &msg);

      // Returns line number through scanner
      int lineno ();

      scanner &get_scanner ();

    private:
      scanner m_scanner;
      parser m_parser;

    private:
      /*
       * cubload::driver::semantic_helper
       *
       * description
       *    A helper class for building semantic types, see cubload::parser::semantic_type union for more details.
       *    The class contains ported functionality from old C lexer & grammar. Be aware that copy constructor and
       *    assignment operator are disable since class make use of buffers/pools which use almost 17 Megabytes of memory
       *
       *    semantic_helper class is inner class related to driver since it cannot be used independently. The class does
       *    contain all legacy functions of the old C lexer & grammar implementation.
       *
       *    TODO
       *    Normally all functionality from semantic_helper should be used only by grammar and not by both lexer & grammar,
       *    Since it is used now by both (legacy behaviour) it is included into driver. Later as improvement we can add a
       *    subclass of cubload::parser (see loader_grammar.hpp) and move functionality of this class into parser subclass.
       *
       * how to use
       *    Interaction with semantic_helper class is done through an instance of driver e.g.
       *
       *    cubload::driver driver;
       *    LDR_CONSTANT *null_const = driver.get_semantic_helper ()->make_constant (LDR_NULL, NULL);
       */
      class semantic_helper
      {
	public:
	  semantic_helper (const driver &parent_driver);

	  // Copy constructor (disabled).
	  semantic_helper (const semantic_helper &copy) = delete;

	  // Copy assignment operator (disabled)
	  semantic_helper &operator= (const semantic_helper &other) = delete;

	  // Destructor
	  virtual ~semantic_helper ();

	  void append_char (char c);
	  string_t *append_string_list (string_t *head, string_t *tail);
	  constant_t *append_constant_list (constant_t *head, constant_t *tail);

	  void set_quoted_string_buffer ();
	  string_t *make_string_by_buffer ();
	  string_t *make_string_by_yytext ();

	  ctor_spec_t *make_constructor_spec (string_t *idname, string_t *arg_list);
	  class_cmd_spec_t *make_class_command_spec (int qualifier, string_t *attr_list, ctor_spec_t *ctor_spec);

	  constant_t *make_constant (int type, void *val);
	  object_ref_t *make_object_ref_by_class_id (string_t *class_id);
	  object_ref_t *make_object_ref_by_class_name (string_t *class_name);
	  constant_t *make_monetary_constant (int currency_type, string_t *amount);

	  void reset_pool_indexes ();
	  bool in_instance_line ();
	  void set_in_instance_line (bool in_instance_line);

	  void reset ();

	private:
	  const driver &m_parent_driver;

	  bool m_in_instance_line;

	  std::size_t m_string_pool_idx;
	  string_t m_string_pool[STRING_POOL_SIZE];

	  // buffer pool for copying yytext and qstr_buffer
	  std::size_t m_copy_buf_pool_idx;
	  char m_copy_buf_pool[COPY_BUF_POOL_SIZE][MAX_COPY_BUF_SIZE];

	  // constant pool
	  std::size_t m_constant_pool_idx;
	  constant_t m_constant_pool[CONSTANT_POOL_SIZE];

	  // quoted string buffer pool
	  char *m_qstr_buffer; // using when pool overflow
	  char *m_qstr_buf_p;
	  bool m_use_qstr_buffer;
	  char **m_qstr_buf_pool;
	  std::size_t m_qstr_buf_idx;
	  std::size_t m_qstr_buf_pool_idx;
	  std::size_t m_qstr_buffer_size;

	  /* private functions */
	  string_t *make_string ();
	  object_ref_t *make_object_ref ();
	  monetary_t *make_monetary_value (int currency_type, string_t *amount);
	  bool is_utf8_valid (string_t *str);
	  bool use_copy_buf_pool (std::size_t str_size);
	  void alloc_qstr_buffer (std::size_t size);
	  void realloc_qstr_buffer (std::size_t new_size);

	  void initialize ();
	  void destroy ();

	  // template private functions
	  template<typename T>
	  T *alloc_ldr_type ();

	  template<typename T>
	  T *append_list (T *head, T *tail);
      }; // class semantic_helper

      semantic_helper m_semantic_helper;

    public:
      semantic_helper &get_semantic_helper ();
  }; // class driver

} // namespace cubload

#endif // _DRIVER_HPP_
