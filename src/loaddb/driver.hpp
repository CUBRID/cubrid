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
 * driver.hpp - TODO CBRD-21654
 */

#ifndef _DRIVER_HPP_
#define _DRIVER_HPP_

#include <istream>

#include "scanner.hpp"
#include "loader.h"
#include "loader_parser.tab.hpp"

namespace cubloaddb
{
  const std::size_t STRING_POOL_SIZE = 1024;
  const std::size_t COPY_BUF_POOL_SIZE = 512;
  const std::size_t QUOTED_STR_BUF_POOL_SIZE = 512;
  const std::size_t MAX_QUOTED_STR_BUF_SIZE = 1024; // 32768; 32 * 1024; TODO CBRD-21654 allocate dynamically
  const std::size_t MAX_COPY_BUF_SIZE = 256;
  const std::size_t CONSTANT_POOL_SIZE = 1024;

  class driver
  {
    public:
      driver ();
      driver (int trace_scanning, int trace_parsing);
      virtual ~driver ();

      int parse (std::istream &in);

      void error (const location &l, const std::string &m);

      /* scanner functions */
      LDR_STRING *make_string_by_yytext (char *yytext, int yyleng);
      LDR_STRING *make_string_by_buffer ();
      void set_quoted_string_buffer ();
      void append_string (char c);

      void reset_string_pool ();
      void initialize_lexer ();

      /* parser functions */
      LDR_STRING *append_string_list (LDR_STRING *head, LDR_STRING *tail);
      LDR_CLASS_COMMAND_SPEC *make_class_command_spec (int qualifier, LDR_STRING *attr_list,
	  LDR_CONSTRUCTOR_SPEC *ctor_spec);
      LDR_CONSTANT *make_constant (int type, void *val);
      LDR_OBJECT_REF *make_object_ref (LDR_STRING *class_name);
      LDR_MONETARY_VALUE *make_monetary_value (int currency_type, LDR_STRING *amount);
      LDR_CONSTANT *append_constant_list (LDR_CONSTANT *head, LDR_CONSTANT *tail);

      void free_ldr_string (LDR_STRING **string);

    private:
      int m_trace_scanning;
      int m_trace_parsing;

      cubloaddb::scanner *m_scanner;
      cubloaddb::loader_yyparser *m_parser;

      LDR_STRING m_string_pool[STRING_POOL_SIZE];
      std::size_t m_string_pool_idx;

      /* buffer pool for copying yytext and qstr_malloc_buffer */
      char m_copy_buf_pool[COPY_BUF_POOL_SIZE][MAX_COPY_BUF_SIZE];
      std::size_t m_copy_buf_pool_idx;

      /* constant pool */
      LDR_CONSTANT m_constant_pool[CONSTANT_POOL_SIZE];
      std::size_t m_constant_pool_idx;

      /* quoted string buffer pool */
      char m_qstr_buf_pool[QUOTED_STR_BUF_POOL_SIZE][MAX_QUOTED_STR_BUF_SIZE];
      std::size_t m_qstr_buf_pool_idx;
      char *m_qstr_malloc_buffer; /* using when pool overflow */
      std::size_t m_qstr_malloc_buffer_size;
      bool m_use_qstr_malloc_buffer;
      char *m_qstr_buf_p;
      std::size_t m_qstr_buf_idx;

      /* private functions */
      LDR_STRING *get_string_container ();
  };

} // namespace cubloaddb

#endif // _DRIVER_HPP_
