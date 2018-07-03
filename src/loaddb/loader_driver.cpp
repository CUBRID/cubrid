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
 * loader_driver.cpp - interface for loader lexer and parser
 */

#include <sstream>
#include <cassert>

#include "loader_driver.hpp"
#include "message_catalog.h"
#include "utility.h"

namespace cubloader
{

  loader_driver::loader_driver ()
    : m_parser (NULL)
    , m_scanner (NULL)
    , m_semantic_helper (NULL)
  {
  }

  loader_driver::~loader_driver ()
  {
    destroy ();
  }

  int
  loader_driver::parse (std::string &s)
  {
    std::istringstream iss (s);

    return parse_internal (iss);
  }

  int
  loader_driver::parse (std::istream &iss)
  {
    return parse_internal (iss);
  }

  void
  loader_driver::error (const location &loc, const std::string &msg)
  {
    ldr_increment_err_total (ldr_Current_context);
    fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_LOADDB, LOADDB_MSG_SYNTAX_ERR), lineno (),
	     get_scanner ()->YYText ());
  }

  int loader_driver::lineno ()
  {
    return get_scanner ()->lineno ();
  }

  int
  loader_driver::parse_internal (std::istream &is)
  {
    m_scanner = new loader_scanner (&is);
    m_parser = new loader_parser (*this);
    m_semantic_helper = new loader_semantic_helper (*m_scanner);

    int ret = m_parser->parse ();

    destroy ();

    return ret;
  }

  loader_scanner *
  loader_driver::get_scanner ()
  {
    assert (m_scanner != NULL);
    return m_scanner;
  }

  loader_semantic_helper *
  loader_driver::get_semantic_helper ()
  {
    assert (m_semantic_helper != NULL);
    return m_semantic_helper;
  }

  void
  loader_driver::destroy ()
  {
    delete m_parser;
    m_parser = NULL;

    delete m_scanner;
    m_scanner = NULL;

    delete m_semantic_helper;
    m_semantic_helper = NULL;
  }
} // namespace cubloader
