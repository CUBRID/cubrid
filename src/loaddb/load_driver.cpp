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
    , m_class_installer (NULL)
    , m_object_loader (NULL)
    , m_error_handler (NULL)
    , m_semantic_helper ()
    , m_is_initialized (false)
  {
    //
  }

  void driver::uninitialize ()
  {
    delete m_class_installer;
    m_class_installer = NULL;

    delete m_object_loader;
    m_object_loader = NULL;

    delete m_scanner;
    m_scanner = NULL;

    delete m_error_handler;
    m_error_handler = NULL;

    m_is_initialized = false;
  }

  driver::~driver ()
  {
    delete m_class_installer;
    m_class_installer = NULL;

    delete m_object_loader;
    m_object_loader = NULL;

    delete m_scanner;
    m_scanner = NULL;

    delete m_error_handler;
    m_error_handler = NULL;
  }

  void
  driver::initialize (class_installer *cls_installer, object_loader *obj_loader, error_handler *error_handler)
  {
    assert (!m_is_initialized);

    m_object_loader = obj_loader;
    m_class_installer = cls_installer;
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
  driver::parse (std::istream &iss, int line_offset)
  {
    m_scanner->switch_streams (&iss);
    m_scanner->set_lineno (line_offset);
    m_semantic_helper.reset_after_batch ();

    assert (m_class_installer != NULL && m_object_loader != NULL);
    parser parser (*this);

    return parser.parse ();
  }

  class_installer &
  driver::get_class_installer ()
  {
    return *m_class_installer;
  }

  object_loader &
  driver::get_object_loader ()
  {
    return *m_object_loader;
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
