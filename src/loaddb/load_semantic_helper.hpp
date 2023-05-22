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
 * load_semantic_helper.hpp - Helper class for building loaddb types
 */

#ifndef _LOAD_SEMANTIC_HELPER_HPP_
#define _LOAD_SEMANTIC_HELPER_HPP_

#include "load_common.hpp"
#include "mem_block.hpp"

#include <forward_list>

namespace cubload
{
  // Constants sizes
  static const std::size_t STRING_POOL_SIZE = 1024;
  static const std::size_t MAX_COPY_BUF_SIZE = 256;
  static const std::size_t COPY_BUF_POOL_SIZE = 512;
  static const std::size_t CONSTANT_POOL_SIZE = 1024;
  static const std::size_t QUOTED_STR_BUF_POOL_SIZE = 512;
  static const std::size_t MAX_QUOTED_STR_BUF_SIZE = 32 * 1024;

  /*
   * cubload::semantic_helper
   *
   * description
   *    A helper class for building semantic types, see cubload::parser::semantic_type union for more details.
   *    The class contains ported functionality from old C lexer & grammar. Be aware that copy constructor and
   *    assignment operator are disable since class make use of buffers/pools which use almost 17 Megabytes of memory
   *
   *    The class contains all legacy functions of the old C lexer & grammar implementation.
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
      semantic_helper ();

      // Copy constructor (disabled).
      semantic_helper (const semantic_helper &copy) = delete;

      // Copy assignment operator (disabled)
      semantic_helper &operator= (const semantic_helper &other) = delete;

      // Destructor
      ~semantic_helper ();

      void append_char (char c);
      string_type *append_string_list (string_type *head, string_type *tail);
      constant_type *append_constant_list (constant_type *head, constant_type *tail);

      void set_quoted_string_buffer ();
      string_type *make_string_by_buffer ();
      string_type *make_string_by_yytext (const char *text, int text_size);

      constant_type *make_constant (int type, void *val);
      constant_type *make_real (string_type *str);
      constant_type *make_monetary_constant (int currency_type, string_type *amount);

      void reset_after_line ();
      void reset_after_batch ();

      bool in_instance_line ();
      void set_in_instance_line (bool in_instance_line);

    private:
      bool m_in_instance_line;

      std::size_t m_string_pool_idx;
      string_type m_string_pool[STRING_POOL_SIZE];
      std::forward_list<string_type *> m_string_list;

      // buffer pool for copying yytext and qstr_buffer
      std::size_t m_copy_buf_pool_idx;
      char m_copy_buf_pool[COPY_BUF_POOL_SIZE][MAX_COPY_BUF_SIZE];

      // constant pool
      std::size_t m_constant_pool_idx;
      constant_type m_constant_pool[CONSTANT_POOL_SIZE];
      std::forward_list<constant_type *> m_constant_list;

      // quoted string buffer pool
      bool m_use_qstr_buf;

      cubmem::extensible_block m_qstr_buf; // using when pool overflow
      char *m_qstr_buf_ptr;
      std::size_t m_qstr_buf_idx;

      char m_qstr_buf_pool[QUOTED_STR_BUF_POOL_SIZE][MAX_QUOTED_STR_BUF_SIZE];
      std::size_t m_qstr_buf_pool_idx;

      /* private functions */
      string_type *make_string (char *val, std::size_t size, bool need_free_val);
      string_type *make_string_and_copy (const char *src, size_t str_size);

      void extend_quoted_string_buffer (size_t new_size);

      bool is_utf8_valid (string_type *str);
      bool use_copy_buf_pool (std::size_t str_size);

      void clear ();

      // template private functions
      template<typename T>
      T *append_list (T *head, T *tail);
  }; // class semantic_helper
}

#endif /* _LOAD_SEMANTIC_HELPER_HPP_ */
