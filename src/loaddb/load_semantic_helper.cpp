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
 * load_semantic_helper.cpp - Helper class for building loaddb types
 */

#include "load_semantic_helper.hpp"

#include "error_manager.h"
#include "language_support.h"

#include <cstring>

namespace cubload
{

  semantic_helper::semantic_helper ()
    : m_in_instance_line (true)
    , m_string_pool_idx (0)
    , m_string_pool {}
    , m_copy_buf_pool_idx (0)
    , m_copy_buf_pool {}
    , m_constant_pool_idx (0)
    , m_constant_pool {}
    , m_use_qstr_buf (false)
    , m_qstr_buf ()
    , m_qstr_buf_p (NULL)
    , m_qstr_buf_idx (0)
    , m_qstr_buf_pool (NULL)
    , m_qstr_buf_pool_idx (0)
  {
    m_qstr_buf_pool = new char *[QUOTED_STR_BUF_POOL_SIZE];
    for (std::size_t i = 0; i < QUOTED_STR_BUF_POOL_SIZE; ++i)
      {
	m_qstr_buf_pool[i] = new char[MAX_QUOTED_STR_BUF_SIZE];
      }
  }

  semantic_helper::~semantic_helper ()
  {
    for (std::size_t i = 0; i < QUOTED_STR_BUF_POOL_SIZE; ++i)
      {
	delete [] m_qstr_buf_pool[i];
      }
    delete [] m_qstr_buf_pool;
  }

  void
  semantic_helper::append_char (char c)
  {
    if (m_use_qstr_buf)
      {
	if (m_qstr_buf_idx >= m_qstr_buf.get_size ())
	  {
	    // double qstr_buffer memory size
	    m_qstr_buf.extend_by (m_qstr_buf.get_size ());
	    m_qstr_buf_p = m_qstr_buf.get_ptr ();
	  }
      }
    else
      {
	if (m_qstr_buf_idx >= MAX_QUOTED_STR_BUF_SIZE)
	  {
	    if (m_qstr_buf.get_ptr () != NULL && m_qstr_buf.get_size () <= MAX_QUOTED_STR_BUF_SIZE)
	      {
		// reset qstr buffer
		m_qstr_buf.~extensible_block ();
	      }

	    if (m_qstr_buf.get_ptr () == NULL)
	      {
		m_qstr_buf.extend_to (MAX_QUOTED_STR_BUF_SIZE * 2);
	      }

	    std::memcpy (m_qstr_buf.get_ptr (), m_qstr_buf_p, m_qstr_buf_idx);
	    m_qstr_buf_p = m_qstr_buf.get_ptr ();
	    m_qstr_buf_pool_idx--;
	    m_use_qstr_buf = true;
	  }
      }

    m_qstr_buf_p[m_qstr_buf_idx++] = c;
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
	m_qstr_buf_p = &m_qstr_buf_pool[m_qstr_buf_pool_idx++][0];
	m_use_qstr_buf = false;
      }
    else
      {
	if (m_qstr_buf.get_ptr () == NULL)
	  {
	    m_qstr_buf.extend_to (MAX_QUOTED_STR_BUF_SIZE);
	  }

	m_qstr_buf_p = m_qstr_buf.get_ptr ();
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
	str = make_string (m_qstr_buf_p, str_size, false);
      }
    else
      {
	if (use_copy_buf_pool (str_size))
	  {
	    str = make_string (&m_copy_buf_pool[m_copy_buf_pool_idx++][0], str_size, false);
	  }
	else
	  {
	    str = make_string (new char[m_qstr_buf_idx], str_size, true);
	  }
      }

    std::memcpy (str->val, m_qstr_buf_p, m_qstr_buf_idx);

    if (!is_utf8_valid (str))
      {
	str->destroy ();
	return NULL;
      }

    return str;
  }

  string_type *
  semantic_helper::make_string_by_yytext (const char *text, int text_size)
  {
    string_type *str = NULL;
    std::size_t str_size = (std::size_t) text_size;

    if (use_copy_buf_pool (str_size))
      {
	str = make_string (&m_copy_buf_pool[m_copy_buf_pool_idx++][0], str_size, false);
      }
    else
      {
	str = make_string (new char[str_size + 1], str_size, true);
      }

    std::memcpy (str->val, text, str_size);
    str->val[str_size] = '\0';

    if (!is_utf8_valid (str))
      {
	str->destroy ();
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
	con->need_free = false;
	con->type = type;
	con->val = val;
      }
    else
      {
	con = new constant_type (type, val);
	con->need_free = true;
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

  void
  semantic_helper::reset_pool_indexes ()
  {
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
  semantic_helper::reset ()
  {
    reset_pool_indexes ();

    m_in_instance_line = true;
    m_qstr_buf.~extensible_block ();
    m_qstr_buf_p = NULL;
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
	str->need_free_self = false;
	str->val = val;
	str->size = size;
	str->need_free_val = need_free_val;
      }
    else
      {
	str = new string_type (val, size, need_free_val);
	str->need_free_self = true;
      }

    return str;
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
