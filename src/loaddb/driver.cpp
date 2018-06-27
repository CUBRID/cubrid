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
 * driver.cpp - TODO CBRD-21654
 */

#include <sstream>

#include "driver.hpp"
#include "error_manager.h"
#include "language_support.h"
#include "memory_alloc.h"

#define FREE_STRING(s)          \
do {                            \
  if ((s)->need_free_val)       \
    {                           \
      free_and_init ((s)->val); \
    }                           \
  if ((s)->need_free_self)      \
    {                           \
      free_and_init ((s));      \
    }                           \
} while (0)

namespace cubloaddb
{
  driver::driver ()
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

  driver::~driver ()
  {
    delete m_scanner;
    delete m_parser;
  }

  int
  driver::parse (std::istream &in)
  {
    m_scanner = new scanner (in, std::cout);
    m_parser = new loader_yyparser (*m_scanner, *this);

    // TODO CBRD-21654 remove this
    m_scanner->set_debug (1);
    m_parser->set_debug_level (1);

    int ret = m_parser->parse ();

    delete m_scanner;
    m_scanner = NULL;

    delete m_parser;
    m_parser = NULL;

    return ret;
  }

  void
  driver::error (const location &l, const std::string &m)
  {
    // TODO CBRD-21654 collect errors and report them to client
    std::cout << "location: " << l << ", m: " << m << "\n";
  }

  string_t *
  driver::make_string_by_yytext (char *yytext, int yyleng)
  {
    string_t *str;
    char *invalid_pos = NULL;

    str = get_string_container ();
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
	    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) (str->size + 1));
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

  string_t *
  driver::get_string_container ()
  {
    string_t *str;

    if (m_string_pool_idx < STRING_POOL_SIZE)
      {
	str = & (m_string_pool[m_string_pool_idx++]);
	str->need_free_self = false;
      }
    else
      {
	str = (string_t *) malloc (sizeof (string_t));

	if (str == NULL)
	  {
	    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (string_t));
	    //YY_FATAL_ERROR (er_msg());
	    return NULL;
	  }

	str->need_free_self = true;
      }

    return str;
  }

  void
  driver::set_quoted_string_buffer ()
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
	    m_qstr_buffer_size = MAX_QUOTED_STR_BUF_SIZE;
	    m_qstr_buffer = (char *) malloc (m_qstr_buffer_size);

	    if (m_qstr_buffer == NULL)
	      {
		er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) m_qstr_buffer_size);
		// YY_FATAL_ERROR (er_msg());
		return;
	      }
	  }

	m_qstr_buf_p = m_qstr_buffer;
	m_use_qstr_buffer = true;
      }

    m_qstr_buf_idx = 0;
  }

  void
  driver::append_char (char c)
  {
    if (m_use_qstr_buffer)
      {
	if (m_qstr_buf_idx >= m_qstr_buffer_size)
	  {
	    m_qstr_buffer_size *= 2;
	    m_qstr_buffer = (char *) realloc (m_qstr_buffer, m_qstr_buffer_size);

	    if (m_qstr_buffer == NULL)
	      {
		er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) m_qstr_buffer_size);
		//YY_FATAL_ERROR (er_msg());
		return;
	      }

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
		m_qstr_buffer_size = MAX_QUOTED_STR_BUF_SIZE * 2;
		m_qstr_buffer = (char *) malloc (m_qstr_buffer_size);

		if (m_qstr_buffer == NULL)
		  {
		    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) m_qstr_buffer_size);
		    //YY_FATAL_ERROR (er_msg());
		    return;
		  }
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
  driver::make_string_by_buffer ()
  {
    string_t *str;
    char *invalid_pos = NULL;

    str = get_string_container ();
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

		er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) m_qstr_buf_idx);
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

  void
  driver::reset_pool_indexes ()
  {
    m_string_pool_idx = 0;
    m_copy_buf_pool_idx = 0;
    m_qstr_buf_pool_idx = 0;
    m_constant_pool_idx = 0;
  }

  string_t *
  driver::append_string_list (string_t *head, string_t *tail)
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

  class_cmd_spec_t *
  driver::make_class_command_spec (int qualifier, string_t *attr_list, ctor_spec_t *ctor_spec)
  {
    class_cmd_spec_t *spec = (class_cmd_spec_t *) malloc (sizeof (class_cmd_spec_t));
    if (spec == NULL)
      {
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (class_cmd_spec_t));
	return NULL;
      }

    spec->qualifier = qualifier;
    spec->attr_list = attr_list;
    spec->ctor_spec = ctor_spec;

    return spec;
  }

  ctor_spec_t *
  driver::make_constructor_spec (string_t *idname, string_t *arg_list)
  {
    ctor_spec_t *spec = (ctor_spec_t *) malloc (sizeof (ctor_spec_t));
    if (spec == NULL)
      {
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (ctor_spec_t));
	//YYABORT;
	return NULL;
      }

    spec->idname = idname;
    spec->arg_list = arg_list;

    return spec;
  }

  constant_t *
  driver::make_constant (int type, void *val)
  {
    constant_t *con;

    if (m_constant_pool_idx < CONSTANT_POOL_SIZE)
      {
	con = & (m_constant_pool[m_constant_pool_idx++]);
	con->need_free = false;
      }
    else
      {
	con = (constant_t *) malloc (sizeof (constant_t));
	if (con == NULL)
	  {
	    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (constant_t));
	    return NULL;
	  }
	con->need_free = true;
      }

    con->type = type;
    con->val = val;

    return con;
  }

  monetary_t *
  driver::make_monetary_value (int currency_type, string_t *amount)
  {
    monetary_t *mon_value = (monetary_t *) malloc (sizeof (monetary_t));
    if (mon_value == NULL)
      {
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (monetary_t));
	return NULL;
      }

    mon_value->amount = amount;
    mon_value->currency_type = currency_type;

    return mon_value;
  }

  object_ref_t *
  driver::make_object_ref (string_t *class_name)
  {
    object_ref_t *ref = (object_ref_t *) malloc (sizeof (object_ref_t));
    if (ref == NULL)
      {
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (object_ref_t));
	//YYABORT;
	return NULL;
      }

    ref->class_id = class_name;
    ref->class_name = NULL;
    ref->instance_number = NULL;

    return ref;
  }

  constant_t *
  driver::append_constant_list (constant_t *head, constant_t *tail)
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

  void
  driver::free_ldr_string (string_t **string)
  {
    FREE_STRING (*string);
  }

  void
  driver::set_in_instance_line (bool in_instance_line)
  {
    m_in_instance_line = in_instance_line;
  }

  bool
  driver::in_instance_line ()
  {
    return m_in_instance_line;
  }
} // namespace cubloaddb
