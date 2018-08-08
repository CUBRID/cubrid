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
 * load_driver.cpp - interface for loader lexer and parser
 */

#ident "$Id$"

#include <cassert>

#include "error_manager.h"
#include "language_support.h"
#if defined (SA_MODE)
#include "load_client_loader.h"
#endif // SA_MODE
#include "load_driver.hpp"
#if defined (SERVER_MODE)
#include "load_server_loader.hpp"
#endif // SERVER_MODE
#include "memory_alloc.h"
#include "message_catalog.h"
#include "utility.h"

namespace cubload
{

  driver::driver ()
#if defined (SERVER_MODE)
    : m_loader (new server_loader ())
#elif defined (SA_MODE)
    : m_loader (new client_loader ())
#endif
    , m_scanner (*this, *m_loader)
    , m_parser (*this, *m_loader)
    , m_semantic_helper (*this)
  {
    //
  }

  driver::~driver ()
  {
    delete m_loader;
  }

  int
  driver::parse (std::istream &iss)
  {
    m_scanner.switch_streams (&iss);

    m_semantic_helper.reset ();

    return m_parser.parse ();
  }

  void
  driver::error (const location &loc, const std::string &msg)
  {
    m_loader->increment_err_total ();
#if defined (SA_MODE)
    fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_LOADDB, LOADDB_MSG_SYNTAX_ERR), lineno (),
	     m_scanner.YYText ());
#endif // SA_MODE
  }

  int driver::lineno ()
  {
    return m_scanner.lineno ();
  }

  scanner &
  driver::get_scanner ()
  {
    return m_scanner;
  }

  driver::semantic_helper &
  driver::get_semantic_helper ()
  {
    return m_semantic_helper;
  }

  /*
   * driver::semantic_helper functions definition
   */
  driver::semantic_helper::semantic_helper (const driver &parent_driver)
    : m_parent_driver (parent_driver)
    , m_qstr_buf_pool {}
  {
    initialize ();
  }

  driver::semantic_helper::~semantic_helper ()
  {
    destroy ();
  }

  void
  driver::semantic_helper::append_char (char c)
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

		if (m_qstr_buffer == NULL)
		  {
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

  string_type *
  driver::semantic_helper::append_string_list (string_type *head, string_type *tail)
  {
    return append_list<string_type> (head, tail);
  }

  constant_type *
  driver::semantic_helper::append_constant_list (constant_type *head, constant_type *tail)
  {
    return append_list<constant_type> (head, tail);
  }

  void
  driver::semantic_helper::set_quoted_string_buffer ()
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

  string_type *
  driver::semantic_helper::make_string_by_buffer ()
  {
    string_type *str = make_string ();
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
	if (use_copy_buf_pool (str->size))
	  {
	    str->val = & (m_copy_buf_pool[m_copy_buf_pool_idx++][0]);
	    str->need_free_val = false;
	  }
	else
	  {
	    str->val = (char *) malloc (m_qstr_buf_idx);
	    str->need_free_val = true;
	  }
      }

    if (str->val == NULL)
      {
	return NULL;
      }

    memcpy (str->val, m_qstr_buf_p, m_qstr_buf_idx);

    if (!is_utf8_valid (str))
      {
	ldr_string_free (&str);
	return NULL;
      }

    return str;
  }

  string_type *
  driver::semantic_helper::make_string_by_yytext ()
  {
    string_type *str = make_string ();
    if (str == NULL)
      {
	return NULL;
      }

    str->size = (std::size_t) m_parent_driver.m_scanner.YYLeng ();
    const char *text = m_parent_driver.m_scanner.YYText ();

    if (use_copy_buf_pool (str->size))
      {
	str->val = & (m_copy_buf_pool[m_copy_buf_pool_idx++][0]);
	str->need_free_val = false;
      }
    else
      {
	str->val = (char *) malloc (str->size + 1);
	str->need_free_val = true;
      }

    if (str->val == NULL)
      {
	return NULL;
      }

    memcpy (str->val, text, str->size);
    str->val[str->size] = '\0';

    if (!is_utf8_valid (str))
      {
	ldr_string_free (&str);
	return NULL;
      }

    return str;
  }

  constructor_spec_type *
  driver::semantic_helper::make_constructor_spec (string_type *id_name, string_type *arg_list)
  {
    constructor_spec_type *spec = alloc_ldr_type<constructor_spec_type> ();

    spec->id_name= id_name;
    spec->arg_list = arg_list;

    return spec;
  }

  class_command_spec_type *
  driver::semantic_helper::make_class_command_spec (int qualifier, string_type *attr_list,
      constructor_spec_type *ctor_spec)
  {
    class_command_spec_type *spec = alloc_ldr_type<class_command_spec_type> ();

    spec->qualifier = qualifier;
    spec->attr_list = attr_list;
    spec->ctor_spec = ctor_spec;

    return spec;
  }

  constant_type *
  driver::semantic_helper::make_constant (int type, void *val)
  {
    constant_type *con = NULL;

    if (m_constant_pool_idx < CONSTANT_POOL_SIZE)
      {
	con = & (m_constant_pool[m_constant_pool_idx++]);
	con->need_free = false;
      }
    else
      {
	con = alloc_ldr_type<constant_type> ();
	con->need_free = true;
      }

    con->type = type;
    con->val = val;

    return con;
  }

  object_ref_type *
  driver::semantic_helper::make_object_ref_by_class_id (string_type *class_id)
  {
    object_ref_type *ref = make_object_ref ();

    ref->class_id = class_id;

    return ref;
  }

  object_ref_type *
  driver::semantic_helper::make_object_ref_by_class_name (string_type *class_name)
  {
    object_ref_type *ref = make_object_ref ();

    ref->class_name = class_name;

    return ref;
  }

  constant_type *
  driver::semantic_helper::make_monetary_constant (int currency_type, string_type *amount)
  {
    monetary_type *mon_value = make_monetary_value (currency_type, amount);
    return make_constant (LDR_MONETARY, mon_value);
  }

  constant_type *
  driver::semantic_helper::make_real (string_type *str)
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
  driver::semantic_helper::reset_pool_indexes ()
  {
    m_string_pool_idx = 0;
    m_copy_buf_pool_idx = 0;
    m_qstr_buf_pool_idx = 0;
    m_constant_pool_idx = 0;
  }

  bool
  driver::semantic_helper::in_instance_line ()
  {
    return m_in_instance_line;
  }

  void
  driver::semantic_helper::set_in_instance_line (bool in_instance_line)
  {
    m_in_instance_line = in_instance_line;
  }

  void
  driver::semantic_helper::reset ()
  {
    reset_pool_indexes ();

    m_in_instance_line = true;
    m_qstr_buffer = NULL;
    m_qstr_buf_p = NULL;
    m_use_qstr_buffer = false;
    m_qstr_buf_idx = 0;
    m_qstr_buffer_size = 0;
  }

  string_type *
  driver::semantic_helper::make_string ()
  {
    string_type *str = NULL;

    if (m_string_pool_idx < STRING_POOL_SIZE)
      {
	str = & (m_string_pool[m_string_pool_idx++]);
	str->need_free_self = false;
      }
    else
      {
	str = alloc_ldr_type<string_type> ();
	str->need_free_self = true;
      }

    return str;
  }

  object_ref_type *
  driver::semantic_helper::make_object_ref ()
  {
    object_ref_type *ref = alloc_ldr_type<object_ref_type> ();

    ref->class_id = NULL;
    ref->class_name = NULL;
    ref->instance_number = NULL;

    return ref;
  }

  monetary_type *
  driver::semantic_helper::make_monetary_value (int currency_type, string_type *amount)
  {
    monetary_type *mon_value = alloc_ldr_type<monetary_type> ();

    mon_value->amount = amount;
    mon_value->currency_type = currency_type;

    return mon_value;
  }

  bool
  driver::semantic_helper::is_utf8_valid (string_type *str)
  {
    char *invalid_pos = NULL;

    if (intl_check_string (str->val, str->size, &invalid_pos, LANG_SYS_CODESET) != INTL_UTF8_VALID)
      {
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_INVALID_CHAR, 1, invalid_pos - str->val);
	return false;
      }

    return true;
  }

  bool
  driver::semantic_helper::use_copy_buf_pool (std::size_t str_size)
  {
    return m_copy_buf_pool_idx < COPY_BUF_POOL_SIZE && str_size < MAX_COPY_BUF_SIZE;
  }

  void
  driver::semantic_helper::alloc_qstr_buffer (std::size_t size)
  {
    m_qstr_buffer_size = size;
    m_qstr_buffer = (char *) malloc (size);
    assert (m_qstr_buffer != NULL);
  }

  void
  driver::semantic_helper::realloc_qstr_buffer (std::size_t new_size)
  {
    m_qstr_buffer_size = new_size;
    m_qstr_buffer = (char *) realloc (m_qstr_buffer, m_qstr_buffer_size);
    assert (m_qstr_buffer != NULL);
  }

  void
  driver::semantic_helper::initialize ()
  {
    reset ();

    m_qstr_buf_pool = new char *[QUOTED_STR_BUF_POOL_SIZE];
    for (std::size_t i = 0; i < QUOTED_STR_BUF_POOL_SIZE; ++i)
      {
	m_qstr_buf_pool[i] = new char[MAX_QUOTED_STR_BUF_SIZE];
      }
  }

  void
  driver::semantic_helper::destroy ()
  {
    for (std::size_t i = 0; i < QUOTED_STR_BUF_POOL_SIZE; ++i)
      {
	delete [] m_qstr_buf_pool[i];
      }
    delete [] m_qstr_buf_pool;
  }

  template<typename T>
  T *
  driver::semantic_helper::alloc_ldr_type ()
  {
    T *ptr = (T *) malloc (sizeof (T));

    assert (ptr != NULL);

    return ptr;
  };

  template<typename T>
  T *
  driver::semantic_helper::append_list (T *head, T *tail)
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
