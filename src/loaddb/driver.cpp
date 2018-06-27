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
 * driver.cpp - TODO CBRD-21654
 */

#include <sstream>

#include "driver.hpp"
#include "error_manager.h"
#include "language_support.h"
#include "memory_alloc.h"

#define FREE_STRING(s)                              \
do {                                                \
  if ((s)->need_free_val) free_and_init ((s)->val); \
  if ((s)->need_free_self) free_and_init ((s));     \
} while (0)

namespace cubloaddb
{
  driver::driver ()
    : m_trace_scanning (0)
    , m_trace_parsing (0)
    , m_scanner (NULL)
    , m_parser (NULL)
    , m_string_pool_idx (0)
    , m_copy_buf_pool_idx (0)
    , m_constant_pool_idx (0)
    , m_qstr_buf_pool_idx (0)
    , m_qstr_malloc_buffer (NULL)
    , m_qstr_malloc_buffer_size (0)
    , m_use_qstr_malloc_buffer (false)
    , m_qstr_buf_p (NULL)
    , m_qstr_buf_idx (0)
  {
  }

  driver::driver (int trace_scanning, int trace_parsing)
    : m_trace_scanning (trace_scanning)
    , m_trace_parsing (trace_parsing)
    , m_scanner (NULL)
    , m_parser (NULL)
    , m_string_pool_idx (0)
    , m_copy_buf_pool_idx (0)
    , m_constant_pool_idx (0)
    , m_qstr_buf_pool_idx (0)
    , m_qstr_malloc_buffer (NULL)
    , m_qstr_malloc_buffer_size (0)
    , m_use_qstr_malloc_buffer (false)
    , m_qstr_buf_p (NULL)
    , m_qstr_buf_idx (0)
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

    m_scanner->set_debug (m_trace_scanning);
    m_parser->set_debug_level (m_trace_parsing);

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
    std::cout << "location: " << l << ", m: " << m << "\n";
  }

  LDR_STRING *
  driver::make_string_by_yytext (char *yytext, int yyleng)
  {
    LDR_STRING *str;
    char *invalid_pos = NULL;

    str = get_string_container ();
    if (str == NULL)
      {
	return NULL;
      }

    str->size = yyleng;
    char *text = yytext;

    if (m_copy_buf_pool_idx < COPY_BUF_POOL_SIZE  && str->size < MAX_COPY_BUF_SIZE)
      {
	str->val = & (m_copy_buf_pool[m_copy_buf_pool_idx][0]);
	memcpy (str->val, text, str->size);
	str->val[str->size] = '\0';
	str->need_free_val = false;
	m_copy_buf_pool_idx++;
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

  LDR_STRING *
  driver::get_string_container ()
  {
    LDR_STRING *str;

    if (m_string_pool_idx < STRING_POOL_SIZE)
      {
	str = & (m_string_pool[m_string_pool_idx]);
	str->need_free_self = false;
	m_string_pool_idx++;
      }
    else
      {
	str = (LDR_STRING *) malloc (sizeof (LDR_STRING));

	if (str == NULL)
	  {
	    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (LDR_STRING));
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
	m_qstr_buf_p = & (m_qstr_buf_pool[m_qstr_buf_pool_idx][0]);
	m_qstr_buf_pool_idx++;
	m_use_qstr_malloc_buffer = false;
      }
    else
      {
	if (m_qstr_malloc_buffer == NULL)
	  {
	    m_qstr_malloc_buffer_size = MAX_QUOTED_STR_BUF_SIZE;
	    m_qstr_malloc_buffer = (char *) malloc (m_qstr_malloc_buffer_size);

	    if (m_qstr_malloc_buffer == NULL)
	      {
		er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) m_qstr_malloc_buffer_size);
		// YY_FATAL_ERROR (er_msg());
		return;
	      }
	  }

	m_qstr_buf_p = m_qstr_malloc_buffer;
	m_use_qstr_malloc_buffer = true;
      }

    m_qstr_buf_idx = 0;
  }

  void
  driver::append_string (char c)
  {
    if (m_use_qstr_malloc_buffer)
      {
	if (m_qstr_buf_idx >= m_qstr_malloc_buffer_size)
	  {
	    m_qstr_malloc_buffer_size *= 2;
	    m_qstr_malloc_buffer = (char *) realloc (m_qstr_malloc_buffer, m_qstr_malloc_buffer_size);

	    if (m_qstr_malloc_buffer == NULL)
	      {
		er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) m_qstr_malloc_buffer_size);
		//YY_FATAL_ERROR (er_msg());
		return;
	      }

	    m_qstr_buf_p = m_qstr_malloc_buffer;
	  }
      }
    else
      {
	if (m_qstr_buf_idx >= MAX_QUOTED_STR_BUF_SIZE)
	  {
	    if (m_qstr_malloc_buffer != NULL && m_qstr_malloc_buffer_size <= MAX_QUOTED_STR_BUF_SIZE)
	      {
		free_and_init (m_qstr_malloc_buffer);
		m_qstr_malloc_buffer = NULL;
	      }

	    if (m_qstr_malloc_buffer == NULL)
	      {
		m_qstr_malloc_buffer_size = MAX_QUOTED_STR_BUF_SIZE * 2;
		m_qstr_malloc_buffer = (char *) malloc (m_qstr_malloc_buffer_size);

		if (m_qstr_malloc_buffer == NULL)
		  {
		    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) m_qstr_malloc_buffer_size);
		    //YY_FATAL_ERROR (er_msg());
		    return;
		  }
	      }

	    memcpy (m_qstr_malloc_buffer, m_qstr_buf_p, m_qstr_buf_idx);
	    m_qstr_buf_p = m_qstr_malloc_buffer;
	    m_qstr_buf_pool_idx--;
	    m_use_qstr_malloc_buffer = true;
	  }
      }

    m_qstr_buf_p[m_qstr_buf_idx] = c;
    m_qstr_buf_idx++;
  }

  LDR_STRING *driver::make_string_by_buffer ()
  {
    LDR_STRING *str;
    char *invalid_pos = NULL;

    str = get_string_container ();
    if (str == NULL)
      {
	return NULL;
      }

    str->size = m_qstr_buf_idx - 1;

    if (!m_use_qstr_malloc_buffer)
      {
	str->val = m_qstr_buf_p;
	str->need_free_val = false;
      }
    else
      {
	if (m_copy_buf_pool_idx < COPY_BUF_POOL_SIZE  && str->size < MAX_COPY_BUF_SIZE)
	  {
	    str->val = & (m_copy_buf_pool[m_copy_buf_pool_idx][0]);
	    memcpy (str->val, m_qstr_buf_p, m_qstr_buf_idx);
	    str->need_free_val = false;
	    m_copy_buf_pool_idx++;
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

  void driver::reset_string_pool ()
  {
    m_string_pool_idx = 0;
    m_copy_buf_pool_idx = 0;
    m_qstr_buf_pool_idx = 0;
    m_constant_pool_idx = 0;
  }

  void driver::initialize_lexer ()
  {
    m_string_pool_idx = 0;
    m_copy_buf_pool_idx = 0;
    m_qstr_buf_pool_idx = 0;
    m_constant_pool_idx = 0;
    m_qstr_malloc_buffer = NULL;
  }

  LDR_STRING *
  driver::append_string_list (LDR_STRING *head, LDR_STRING *tail)
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

  LDR_CLASS_COMMAND_SPEC *
  driver::make_class_command_spec (int qualifier, LDR_STRING *attr_list, LDR_CONSTRUCTOR_SPEC *ctor_spec)
  {
    LDR_CLASS_COMMAND_SPEC *spec;

    spec = (LDR_CLASS_COMMAND_SPEC *) malloc (sizeof (LDR_CLASS_COMMAND_SPEC));
    if (spec == NULL)
      {
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (LDR_CLASS_COMMAND_SPEC));
	return NULL;
      }

    spec->qualifier = qualifier;
    spec->attr_list = attr_list;
    spec->ctor_spec = ctor_spec;

    return spec;
  }

  LDR_CONSTANT *
  driver::make_constant (int type, void *val)
  {
    LDR_CONSTANT *con;

    if (m_constant_pool_idx < CONSTANT_POOL_SIZE)
      {
	con = & (m_constant_pool[m_constant_pool_idx]);
	m_constant_pool_idx++;
	con->need_free = false;
      }
    else
      {
	con = (LDR_CONSTANT *) malloc (sizeof (LDR_CONSTANT));
	if (con == NULL)
	  {
	    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (LDR_CONSTANT));
	    return NULL;
	  }
	con->need_free = true;
      }

    con->type = type;
    con->val = val;

    return con;
  }

  LDR_MONETARY_VALUE *
  driver::make_monetary_value (int currency_type, LDR_STRING *amount)
  {
    LDR_MONETARY_VALUE *mon_value = NULL;

    mon_value = (LDR_MONETARY_VALUE *) malloc (sizeof (LDR_MONETARY_VALUE));
    if (mon_value == NULL)
      {
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (LDR_MONETARY_VALUE));
	return NULL;
      }

    mon_value->amount = amount;
    mon_value->currency_type = currency_type;

    return mon_value;
  }

  LDR_OBJECT_REF *
  driver::make_object_ref (LDR_STRING *class_name)
  {
    LDR_OBJECT_REF *ref;

    ref = (LDR_OBJECT_REF *) malloc (sizeof (LDR_OBJECT_REF));
    if (ref == NULL)
      {
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (LDR_OBJECT_REF));
	//YYABORT;
	return NULL;
      }

    ref->class_id = class_name;
    ref->class_name = NULL;
    ref->instance_number = NULL;

    return ref;
  }

  LDR_CONSTANT *
  driver::append_constant_list (LDR_CONSTANT *head, LDR_CONSTANT *tail)
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
  driver::free_ldr_string (LDR_STRING **string)
  {
    FREE_STRING (*string);
  }
} // namespace cubloaddb
