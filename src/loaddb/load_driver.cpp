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

#include "load_driver.hpp"

#include <cassert>

namespace cubload
{

  driver::driver ()
    : m_scanner (NULL)
    , m_loader (NULL)
    , m_error_handler (NULL)
    , m_semantic_helper ()
    , m_is_initialized (false)
  {
    //
  }

  driver::~driver ()
  {
    delete m_loader;
    m_loader = NULL;

    delete m_scanner;
    m_scanner = NULL;

    delete m_error_handler;
    m_error_handler = NULL;
  }

  void
  driver::initialize (loader *loader, error_handler *error_handler)
  {
    assert (!m_is_initialized);

    m_loader = loader;
    m_error_handler = error_handler;
    m_scanner = new scanner (m_semantic_helper, *m_error_handler);
    m_is_initialized = true;
  }

  bool
  driver::is_initialized ()
  {
    return m_is_initialized;
  }

  int
  driver::parse (std::istream &iss)
  {
    m_scanner->switch_streams (&iss);
    m_semantic_helper.reset ();

    assert (m_loader != NULL);
    parser parser (*this);

    return parser.parse ();
  }

  loader &
  driver::get_loader ()
  {
    return *m_loader;
  }

  semantic_helper &
  driver::get_semantic_helper ()
  {
    return m_semantic_helper;
  }

  error_handler &
  driver::get_error_handler ()
  {
    return *m_error_handler;
  }

  scanner &
  driver::get_scanner ()
  {
    return *m_scanner;
  }

} // namespace cubload
