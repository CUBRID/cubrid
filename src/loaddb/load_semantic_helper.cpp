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
 * load_semantic_helper.cpp - Helper class for building loaddb types
 */

#include "load_semantic_helper.hpp"

#include "error_manager.h"
#include "language_support.h"

#include <algorithm>
#include <cstring>
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace cubload
{

  semantic_helper::semantic_helper ()
    : m_in_instance_line (true)
    , m_string_pool_idx (0)
    , m_string_pool {}
    , m_string_list ()
    , m_copy_buf_pool_idx (0)
    , m_copy_buf_pool {}
    , m_constant_pool_idx (0)
    , m_constant_pool {}
    , m_constant_list ()
    , m_use_qstr_buf (false)
    , m_qstr_buf (cubmem::EXPONENTIAL_STANDARD_BLOCK_ALLOCATOR)
    , m_qstr_buf_ptr (NULL)
    , m_qstr_buf_idx (0)
    , m_qstr_buf_pool {}
    , m_qstr_buf_pool_idx (0)
  {
    //
  }

  semantic_helper::~semantic_helper ()
  {
    clear ();
  }

  void
  semantic_helper::append_char (char c)
  {
    if (m_use_qstr_buf)
      {
	extend_quoted_string_buffer (m_qstr_buf_idx + 1);
      }
    else
      {
	if (m_qstr_buf_idx >= MAX_QUOTED_STR_BUF_SIZE)
	  {
	    char *qstr_buf_tmp = m_qstr_buf_ptr;
	    extend_quoted_string_buffer (MAX_QUOTED_STR_BUF_SIZE * 2);

	    // in case we switch from m_qstr_buf_pool to m_qstr_buf
	    // we need to copy accumulated data to the new memory location
	    std::memcpy (m_qstr_buf_ptr, qstr_buf_tmp, m_qstr_buf_idx);

	    m_qstr_buf_pool_idx--;
	    m_use_qstr_buf = true;
	  }
      }

    m_qstr_buf_ptr[m_qstr_buf_idx++] = c;
  }

  string_type *
  semantic_helper::append_string_list (string_type *head, string_type *tail)
  {
    return append_list<string_type> (head, tail);
  }

  constant_type *
  semantic_helper::append_constant_list (constant_type *head, constant_type *tail)
  {
    return append_list<constant_type> (head, tail);
  }

  void
  semantic_helper::set_quoted_string_buffer ()
  {
    if (m_qstr_buf_pool_idx < QUOTED_STR_BUF_POOL_SIZE)
      {
	m_qstr_buf_ptr = &m_qstr_buf_pool[m_qstr_buf_pool_idx++][0];
	m_use_qstr_buf = false;
      }
    else
      {
	extend_quoted_string_buffer (MAX_QUOTED_STR_BUF_SIZE);
	m_use_qstr_buf = true;
      }

    m_qstr_buf_idx = 0;
  }

  string_type *
  semantic_helper::make_string_by_buffer ()
  {
    string_type *str = NULL;
    std::size_t str_size = m_qstr_buf_idx - 1;

    if (!m_use_qstr_buf)
      {
	str = make_string (m_qstr_buf_ptr, str_size, false);
      }
    else
      {
	str = make_string_and_copy (m_qstr_buf_ptr, str_size);
      }

    if (!is_utf8_valid (str))
      {
	return NULL;
      }

    return str;
  }

  string_type *
  semantic_helper::make_string_by_yytext (const char *text, int text_size)
  {
    std::size_t str_size = (std::size_t) text_size;
    string_type *str = make_string_and_copy (text, str_size);

    if (!is_utf8_valid (str))
      {
	return NULL;
      }

    return str;
  }

  constant_type *
  semantic_helper::make_constant (int type, void *val)
  {
    constant_type *con = NULL;

    if (m_constant_pool_idx < CONSTANT_POOL_SIZE)
      {
	con = &m_constant_pool[m_constant_pool_idx++];
	con->type = (data_type) type;
	con->val = val;
      }
    else
      {
	con = new constant_type ((data_type) type, val);

	// collect allocated constants in order to free the memory later
	m_constant_list.push_front (con);
      }

    return con;
  }

  constant_type *
  semantic_helper::make_real (string_type *str)
  {
    if (str == NULL || str->val == NULL)
      {
	return NULL;
      }

    if (strchr (str->val, 'F') != NULL || strchr (str->val, 'f') != NULL)
      {
	return make_constant (LDR_FLOAT, str);
      }
    else if (strchr (str->val, 'E') != NULL || strchr (str->val, 'e') != NULL)
      {
	return make_constant (LDR_DOUBLE, str);
      }
    else
      {
	return make_constant (LDR_NUMERIC, str);
      }
  }

  constant_type *
  semantic_helper::make_monetary_constant (int currency_type, string_type *amount)
  {
    return make_constant (LDR_MONETARY, new monetary_type (amount, currency_type));
  }

  void
  semantic_helper::reset_after_line ()
  {
    clear ();

    m_string_pool_idx = 0;
    m_copy_buf_pool_idx = 0;
    m_qstr_buf_pool_idx = 0;
    m_constant_pool_idx = 0;
  }

  bool
  semantic_helper::in_instance_line ()
  {
    return m_in_instance_line;
  }

  void
  semantic_helper::set_in_instance_line (bool in_instance_line)
  {
    m_in_instance_line = in_instance_line;
  }

  void
  semantic_helper::reset_after_batch ()
  {
    reset_after_line ();

    m_in_instance_line = true;
    m_qstr_buf_ptr = NULL;
    m_use_qstr_buf = false;
    m_qstr_buf_idx = 0;
  }

  string_type *
  semantic_helper::make_string (char *val, std::size_t size, bool need_free_val)
  {
    string_type *str = NULL;

    if (m_string_pool_idx < STRING_POOL_SIZE)
      {
	str = &m_string_pool[m_string_pool_idx++];
	str->val = val;
	str->size = size;
	str->need_free_val = need_free_val;
      }
    else
      {
	str = new string_type (val, size, need_free_val);

	// collect allocated string types in order to free the memory later
	m_string_list.push_front (str);
      }

    return str;
  }

  string_type *
  semantic_helper::make_string_and_copy (const char *src, size_t str_size)
  {
    string_type *str;

    if (use_copy_buf_pool (str_size))
      {
	str = make_string (&m_copy_buf_pool[m_copy_buf_pool_idx++][0], str_size, false);
      }
    else
      {
	// +1 for string null terminator
	str = make_string (new char[str_size + 1], str_size, true);
      }

    std::memcpy (str->val, src, str_size);
    str->val[str_size] = '\0';

    return str;
  }

  void
  semantic_helper::extend_quoted_string_buffer (size_t new_size)
  {
    m_qstr_buf.extend_to (new_size);
    // make sure that quoted string buffer points to new memory location
    m_qstr_buf_ptr = m_qstr_buf.get_ptr ();
  }

  bool
  semantic_helper::is_utf8_valid (string_type *str)
  {
    char *invalid_pos = NULL;

    if (intl_check_string (str->val, (int) str->size, &invalid_pos, LANG_SYS_CODESET) != INTL_UTF8_VALID)
      {
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_INVALID_CHAR, 1, invalid_pos - str->val);
	return false;
      }

    return true;
  }

  bool
  semantic_helper::use_copy_buf_pool (std::size_t str_size)
  {
    return m_copy_buf_pool_idx < COPY_BUF_POOL_SIZE && str_size < MAX_COPY_BUF_SIZE;
  }

  void
  semantic_helper::clear ()
  {
    size_t string_pool_end = std::min (m_string_pool_idx + 1, STRING_POOL_SIZE);
    for (size_t i = 0; i < string_pool_end; ++i)
      {
	// might be that some of the str.val within from m_string_pool where dynamically allocated
	m_string_pool[i].destroy ();
      }

    for (string_type *str : m_string_list)
      {
	delete str;
      }
    m_string_list.clear ();

    for (constant_type *con : m_constant_list)
      {
	delete con;
      }
    m_constant_list.clear ();
  }

  template<typename T>
  T *
  semantic_helper::append_list (T *head, T *tail)
  {
    tail->next = NULL;
    tail->last = NULL;

    if (head)
      {
	head->last->next = tail;
      }
    else
      {
	head = tail;
      }

    head->last = tail;
    return head;
  }

} // namespace cubload
