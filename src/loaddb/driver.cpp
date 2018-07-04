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
 * driver.cpp - interface for loader lexer and parser
 */

#include <cassert>

#include "driver.hpp"
#include "message_catalog.h"
#include "utility.h"

namespace cubload
{

  driver::driver ()
    : m_parser (NULL)
    , m_scanner (NULL)
    , m_semantic_helper (NULL)
  {
  }

  driver::~driver ()
  {
    destroy ();
  }

  int
  driver::parse (std::istream &iss)
  {
    m_scanner = new scanner (&iss);
    m_parser = new parser (*this);
    m_semantic_helper = new semantic_helper (*m_scanner);

    int ret = m_parser->parse ();

    destroy ();

    return ret;
  }

  void
  driver::error (const location &loc, const std::string &msg)
  {
    ldr_increment_err_total (ldr_Current_context);
    fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_LOADDB, LOADDB_MSG_SYNTAX_ERR), lineno (),
	     get_scanner ()->YYText ());
  }

  int driver::lineno ()
  {
    return get_scanner ()->lineno ();
  }

  scanner *
  driver::get_scanner ()
  {
    assert (m_scanner != NULL);
    return m_scanner;
  }

  semantic_helper *
  driver::get_semantic_helper ()
  {
    assert (m_semantic_helper != NULL);
    return m_semantic_helper;
  }

  void
  driver::destroy ()
  {
    delete m_parser;
    m_parser = NULL;

    delete m_scanner;
    m_scanner = NULL;

    delete m_semantic_helper;
    m_semantic_helper = NULL;
  }
} // namespace cubload
