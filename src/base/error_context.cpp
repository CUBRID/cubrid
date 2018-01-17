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
 * error_context.cpp - implementation for error context
 */

#include "error_context.hpp"

#include "error_code.h"
#include "error_manager.h"

#include <cstring>

er_messages::er_messages ()
  : err_id (NO_ERROR)
  , severity (ER_WARNING_SEVERITY)
  , file_name (NULL)
  , line_no (-1)
  , msg_area_size (0)
  , msg_area (NULL)
  , stack (NULL)
  , args (NULL)
  , nargs (0)
{
}

er_messages::~er_messages ()
{
  while (stack != NULL)
    {
      delete pop_stack ();
    }
}

ER_MSG *
er_messages::pop_stack (void)
{
  if (stack != NULL)
    {
      ER_MSG *popped = stack;
      stack = stack->stack;
      return popped;
    }
  else
    {
      return NULL;
    }
}

namespace cuberr
{
  thread_local context *tl_Context_p = NULL;

  context::context ()
    : m_all_errors ()
    , m_crt_error_p (m_all_errors)
    , m_msgbuf ()
  {
    std::memset (m_msgbuf, 0, sizeof (m_msgbuf));
  }

  const ER_MSG &
  context::get_crt_error (void)
  {
    return m_crt_error_p;
  }

  cuberr::context &
  context::get_context (void)
  {
    return *tl_Context_p; // crashes if null!
  }

} // namespace cuberr
