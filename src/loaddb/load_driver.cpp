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
 * load_driver.cpp - interface for loader lexer and parser
 */

#include "load_driver.hpp"

#include <cassert>
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace cubload
{

  driver::driver ()
    : m_scanner (NULL)
    , m_class_installer (NULL)
    , m_object_loader (NULL)
    , m_error_handler (NULL)
    , m_semantic_helper ()
    , m_start_line_no (0)
    , m_is_initialized (false)
  {
    //
  }

  void driver::clear ()
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
    clear ();
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
    m_scanner->set_lineno (line_offset + 1);
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

  void
  driver::update_start_line ()
  {
    m_start_line_no = get_scanner ().lineno ();
  }

  int
  driver::get_start_line ()
  {
    return m_start_line_no;
  }

} // namespace cubload
