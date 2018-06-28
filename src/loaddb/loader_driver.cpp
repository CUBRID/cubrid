/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 */

/*
 * loader_driver.cpp - TODO CBRD-21654
 */

#include <sstream>

#include "loader_driver.hpp"
#include "language_support.h"
#include "memory_alloc.h"

namespace cubloader
{
  loader_driver::loader_driver ()
    : m_scanner (NULL)
    , m_parser (NULL)
    , m_in_instance_line (true)
    , m_string_pool_idx (0)
    , m_copy_buf_pool_idx (0)
    , m_constant_pool_idx (0)
    , m_qstr_buffer (NULL)
    , m_qstr_buf_p (NULL)
    , m_use_qstr_buffer (false)
    , m_qstr_buf_idx (0)
    , m_qstr_buf_pool_idx (0)
    , m_qstr_buffer_size (0)
  {
  }

  loader_driver::~loader_driver ()
  {
    delete m_scanner;
    delete m_parser;
  }

  int
  loader_driver::parse (std::string &s)
  {
    std::istringstream *iss  = new std::istringstream (s);

    m_scanner = new loader_scanner (iss);
    m_parser = new loader_parser (*m_scanner, *this);

    // TODO CBRD-21654 remove this
    m_scanner->set_debug (0);
    m_parser->set_debug_level (1);

    int ret = m_parser->parse ();

    delete m_parser;
    m_parser = NULL;

    delete m_scanner;
    m_scanner = NULL;

    delete iss;
    iss = NULL;

    return ret;
  }

  void
  loader_driver::error (const location &l, const std::string &m)
  {
    // TODO CBRD-21654 collect errors and report them to client
    std::cout << "location: " << l << ", m: " << m << "\n";
  }

  void
  loader_driver::append_char (char c)
  {
    if (m_use_qstr_buffer)
      {
	if (m_qstr_buf_idx >= m_qstr_buffer_size)
	  {
	    realloc_qstr_buffer (m_qstr_buffer_size * 2);
	    m_qstr_buf_p = m_qstr_buffer;
	  }
      }
    else
      {
	if (m_qstr_buf_idx >= MAX_QUOTED_STR_BUF_SIZE)
	  {
	    if (m_qstr_buffer != NULL && m_qstr_buffer_size <= MAX_QUOTED_STR_BUF_SIZE)
	      {
		free_and_init (m_qstr_buffer);
		m_qstr_buffer = NULL;
	      }

	    if (m_qstr_buffer == NULL)
	      {
		alloc_qstr_buffer (MAX_QUOTED_STR_BUF_SIZE * 2);
	      }

	    memcpy (m_qstr_buffer, m_qstr_buf_p, m_qstr_buf_idx);
	    m_qstr_buf_p = m_qstr_buffer;
	    m_qstr_buf_pool_idx--;
	    m_use_qstr_buffer = true;
	  }
      }

    m_qstr_buf_p[m_qstr_buf_idx++] = c;
  }

  string_t *
  loader_driver::append_string_list (string_t *head, string_t *tail)
  {
    return append_list<string_t> (head, tail);
  }

  constant_t *
  loader_driver::append_constant_list (constant_t *head, constant_t *tail)
  {
    return append_list<constant_t> (head, tail);
  }

  void
  loader_driver::set_quoted_string_buffer ()
  {
    if (m_qstr_buf_pool_idx < QUOTED_STR_BUF_POOL_SIZE)
      {
	m_qstr_buf_p = & (m_qstr_buf_pool[m_qstr_buf_pool_idx++][0]);
	m_use_qstr_buffer = false;
      }
    else
      {
	if (m_qstr_buffer == NULL)
	  {
	    alloc_qstr_buffer (MAX_QUOTED_STR_BUF_SIZE);
	  }

	m_qstr_buf_p = m_qstr_buffer;
	m_use_qstr_buffer = true;
      }

    m_qstr_buf_idx = 0;
  }

  string_t *
  loader_driver::make_string_by_buffer ()
  {
    char *invalid_pos = NULL;

    string_t *str = make_string ();
    if (str == NULL)
      {
	return NULL;
      }

    str->size = m_qstr_buf_idx - 1;

    if (!m_use_qstr_buffer)
      {
	str->val = m_qstr_buf_p;
	str->need_free_val = false;
      }
    else
      {
	if (m_copy_buf_pool_idx < COPY_BUF_POOL_SIZE && str->size < MAX_COPY_BUF_SIZE)
	  {
	    str->val = & (m_copy_buf_pool[m_copy_buf_pool_idx++][0]);
	    memcpy (str->val, m_qstr_buf_p, m_qstr_buf_idx);
	    str->need_free_val = false;
	  }
	else
	  {
	    str->val = (char *) malloc (m_qstr_buf_idx);

	    if (str->val == NULL)
	      {
		if (str->need_free_self)
		  {
		    free_and_init (str);
		  }

		er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, m_qstr_buf_idx);
		//YY_FATAL_ERROR (er_msg());
		return NULL;
	      }

	    memcpy (str->val, m_qstr_buf_p, m_qstr_buf_idx);
	    str->need_free_val = true;
	  }
      }

    if (intl_check_string (str->val, str->size, &invalid_pos, LANG_SYS_CODESET) != INTL_UTF8_VALID)
      {
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_INVALID_CHAR, 1, invalid_pos - str->val);
	//YY_FATAL_ERROR (er_msg());
	return NULL;
      }

    return str;
  }

  string_t *
  loader_driver::make_string_by_yytext (char *yytext, int yyleng)
  {
    char *invalid_pos = NULL;

    string_t *str = make_string ();
    if (str == NULL)
      {
	return NULL;
      }

    str->size = yyleng;
    char *text = yytext;

    if (m_copy_buf_pool_idx < COPY_BUF_POOL_SIZE && str->size < MAX_COPY_BUF_SIZE)
      {
	str->val = & (m_copy_buf_pool[m_copy_buf_pool_idx++][0]);
	memcpy (str->val, text, str->size);
	str->val[str->size] = '\0';
	str->need_free_val = false;
      }
    else
      {
	str->val = (char *) malloc (str->size + 1);

	if (str->val == NULL)
	  {
	    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, str->size + 1);
	    //YY_FATAL_ERROR (er_msg());
	    return NULL;
	  }

	memcpy (str->val, text, str->size);
	str->val[str->size] = '\0';
	str->need_free_val = true;
      }

    if (intl_check_string (str->val, str->size, &invalid_pos, LANG_SYS_CODESET) != INTL_UTF8_VALID)
      {
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_INVALID_CHAR, 1, invalid_pos - str->val);
	//YY_FATAL_ERROR (er_msg());
	return NULL;
      }

    return str;
  }

  ctor_spec_t *
  loader_driver::make_constructor_spec (string_t *idname, string_t *arg_list)
  {
    ctor_spec_t *spec = alloc_ldr_type<ctor_spec_t> ();

    spec->idname = idname;
    spec->arg_list = arg_list;

    return spec;
  }

  class_cmd_spec_t *
  loader_driver::make_class_command_spec (int qualifier, string_t *attr_list, ctor_spec_t *ctor_spec)
  {
    class_cmd_spec_t *spec = alloc_ldr_type<class_cmd_spec_t> ();

    spec->qualifier = qualifier;
    spec->attr_list = attr_list;
    spec->ctor_spec = ctor_spec;

    return spec;
  }

  constant_t *
  loader_driver::make_constant (int type, void *val)
  {
    constant_t *con = NULL;

    if (m_constant_pool_idx < CONSTANT_POOL_SIZE)
      {
	con = & (m_constant_pool[m_constant_pool_idx++]);
	con->need_free = false;
      }
    else
      {
	con = alloc_ldr_type<constant_t> ();
	con->need_free = true;
      }

    con->type = type;
    con->val = val;

    return con;
  }

  object_ref_t *
  loader_driver::make_object_ref (string_t *class_name)
  {
    object_ref_t *ref = alloc_ldr_type<object_ref_t> ();

    ref->class_id = class_name;
    ref->class_name = NULL;
    ref->instance_number = NULL;

    return ref;
  }

  monetary_t *
  loader_driver::make_monetary_value (int currency_type, string_t *amount)
  {
    monetary_t *mon_value = alloc_ldr_type<monetary_t> ();

    mon_value->amount = amount;
    mon_value->currency_type = currency_type;

    return mon_value;
  }

  void
  loader_driver::reset_pool_indexes ()
  {
    m_string_pool_idx = 0;
    m_copy_buf_pool_idx = 0;
    m_qstr_buf_pool_idx = 0;
    m_constant_pool_idx = 0;
  }

  void
  loader_driver::free_ldr_string (string_t **string)
  {
    FREE_STRING (*string);
  }

  bool
  loader_driver::in_instance_line ()
  {
    return m_in_instance_line;
  }

  void
  loader_driver::set_in_instance_line (bool in_instance_line)
  {
    m_in_instance_line = in_instance_line;
  }

  string_t *
  loader_driver::make_string ()
  {
    string_t *str = NULL;

    if (m_string_pool_idx < STRING_POOL_SIZE)
      {
	str = & (m_string_pool[m_string_pool_idx++]);
	str->need_free_self = false;
      }
    else
      {
	str = alloc_ldr_type<string_t> ();
	str->need_free_self = true;
      }

    return str;
  }

  void
  loader_driver::alloc_qstr_buffer (std::size_t size)
  {
    m_qstr_buffer_size = size;
    m_qstr_buffer = (char *) malloc (m_qstr_buffer_size);

    assert (m_qstr_buffer != NULL);
  }

  void
  loader_driver::realloc_qstr_buffer (std::size_t new_size)
  {
    m_qstr_buffer_size = new_size;
    m_qstr_buffer = (char *) realloc (m_qstr_buffer, m_qstr_buffer_size);

    assert (m_qstr_buffer != NULL);
  }
} // namespace cubloader
