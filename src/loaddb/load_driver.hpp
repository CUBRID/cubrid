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
#include "message_catalog.h"
#include "utility.h"

#include <istream>

namespace cubload
{

  // Constants sizes
  static const std::size_t STRING_POOL_SIZE = 1024;
  static const std::size_t MAX_COPY_BUF_SIZE = 256;
  static const std::size_t COPY_BUF_POOL_SIZE = 512;
  static const std::size_t CONSTANT_POOL_SIZE = 1024;
  static const std::size_t QUOTED_STR_BUF_POOL_SIZE = 512;
  static const std::size_t MAX_QUOTED_STR_BUF_SIZE = 32 * 1024;

  // forward declaration
  class scanner;

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

      template<typename Loader, typename ... Args>
      void initialize (Args &&... args);

      // Parse functions
      int parse (std::istream &iss);

      // Function to report any error which occurs during the loading of the data
      template<typename... Args>
      void on_error (MSGCAT_LOADDB_MSG msg_id, bool include_line_msg, Args &&... args);

      template<typename... Args>
      void on_failure (MSGCAT_LOADDB_MSG msg_id, bool include_line_msg, Args &&... args);

      // Returns line number through scanner
      int scanner_lineno ();
      // Returns current text from scanner
      const char *scanner_text ();

      scanner &get_scanner ();

    private:
      char *get_message_from_catalog (MSGCAT_LOADDB_MSG msg_id);

      template<typename... Args>
      std::string generate_error_message (MSGCAT_LOADDB_MSG msg_id, bool include_line_msg, Args &&... args);

      // Format string based on format string passed as input parameter. Check snprintf function for more details
      template<typename... Args>
      std::string format (const char *fmt, Args &&... args);

      scanner *m_scanner;
      loader *m_loader;
      parser *m_parser;

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
       *    subclass of cubload::parser (see load_grammar.hpp) and move functionality of this class into parser subclass.
       *
       * how to use
       *    Interaction with semantic_helper class is done through an instance of driver e.g.
       *
       *    cubload::driver driver;
       *    constant_type *null_const = driver.get_semantic_helper ()->make_constant (LDR_NULL, NULL);
       */
      class semantic_helper
      {
	public:
	  explicit semantic_helper (const driver &parent_driver);

	  // Copy constructor (disabled).
	  semantic_helper (const semantic_helper &copy) = delete;

	  // Copy assignment operator (disabled)
	  semantic_helper &operator= (const semantic_helper &other) = delete;

	  // Destructor
	  virtual ~semantic_helper ();

	  void append_char (char c);
	  string_type *append_string_list (string_type *head, string_type *tail);
	  constant_type *append_constant_list (constant_type *head, constant_type *tail);

	  void set_quoted_string_buffer ();
	  string_type *make_string_by_buffer ();
	  string_type *make_string_by_yytext ();

	  constructor_spec_type *make_constructor_spec (string_type *id_name, string_type *arg_list);
	  class_command_spec_type *make_class_command_spec (int qualifier, string_type *attr_list,
	      constructor_spec_type *ctor_spec);

	  constant_type *make_constant (int type, void *val);
	  object_ref_type *make_object_ref_by_class_id (string_type *class_id);
	  object_ref_type *make_object_ref_by_class_name (string_type *class_name);
	  constant_type *make_monetary_constant (int currency_type, string_type *amount);
	  constant_type *make_real (string_type *str);

	  void reset_pool_indexes ();
	  bool in_instance_line ();
	  void set_in_instance_line (bool in_instance_line);

	  void reset ();

	private:
	  const driver &m_parent_driver;

	  bool m_in_instance_line;

	  std::size_t m_string_pool_idx;
	  string_type m_string_pool[STRING_POOL_SIZE];

	  // buffer pool for copying yytext and qstr_buffer
	  std::size_t m_copy_buf_pool_idx;
	  char m_copy_buf_pool[COPY_BUF_POOL_SIZE][MAX_COPY_BUF_SIZE];

	  // constant pool
	  std::size_t m_constant_pool_idx;
	  constant_type m_constant_pool[CONSTANT_POOL_SIZE];

	  // quoted string buffer pool
	  char *m_qstr_buffer; // using when pool overflow
	  char *m_qstr_buf_p;
	  bool m_use_qstr_buffer;
	  char **m_qstr_buf_pool;
	  std::size_t m_qstr_buf_idx;
	  std::size_t m_qstr_buf_pool_idx;
	  std::size_t m_qstr_buffer_size;

	  /* private functions */
	  string_type *make_string ();
	  object_ref_type *make_object_ref ();
	  monetary_type *make_monetary_value (int currency_type, string_type *amount);
	  bool is_utf8_valid (string_type *str);
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

/************************************************************************/
/* Template implementation                                              */
/************************************************************************/

namespace cubload
{

  template<typename Loader, typename ... Args>
  void
  driver::initialize (Args &&... args)
  {
    if (m_loader != NULL)
      {
	// if already initialized then do nothing
	return;
      }

    assert (m_parser == NULL);

    m_loader = new Loader (std::forward<Args> (args)...);
    m_parser = new parser (*this, *m_loader);
  }

  template<typename... Args>
  void
  driver::on_error (MSGCAT_LOADDB_MSG msg_id, bool include_line_msg, Args &&... args)
  {
    std::string err_msg = generate_error_message (msg_id, include_line_msg, std::forward<Args> (args)...);
    m_loader->on_error (err_msg);
  }

  template<typename... Args>
  void
  driver::on_failure (MSGCAT_LOADDB_MSG msg_id, bool include_line_msg, Args &&... args)
  {
    std::string err_msg = generate_error_message (msg_id, include_line_msg, std::forward<Args> (args)...);
    m_loader->on_failure (err_msg);
  }

  template<typename... Args>
  std::string
  driver::generate_error_message (MSGCAT_LOADDB_MSG msg_id, bool include_line_msg, Args &&... args)
  {
    std::string err_msg_line;

    if (include_line_msg)
      {
	err_msg_line = format (get_message_from_catalog (LOADDB_MSG_LINE), scanner_lineno () - 1);
      }

    std::string err_msg = format (get_message_from_catalog (msg_id), std::forward<Args> (args)...);

    err_msg_line.append (err_msg);

    return err_msg_line;
  }

  template<typename... Args>
  std::string
  driver::format (const char *fmt, Args &&... args)
  {
    // Determine required size
    int size = snprintf (NULL, 0, fmt, std::forward<Args> (args)...) + 1; // +1  for '\0'
    std::unique_ptr<char[]> msg (new char[size]);

    snprintf (msg.get (), (size_t) size, fmt, std::forward<Args> (args)...);

    return std::string (msg.get (), msg.get () + size - 1);
  }

} // namespace cubload

#endif /* _LOAD_DRIVER_HPP_ */
