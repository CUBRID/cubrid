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
 * replication_node.cpp
 */

#ident "$Id$"

#include "replication_node.hpp"
#include "multi_thread_stream.hpp"
#include "error_code.h"

namespace cubreplication
{
  replication_node::~replication_node ()
  {
    delete m_stream;
    m_stream = NULL;
  }

  int replication_node::apply_start_position (void)
  {
    /* TODO set m_start_position from recovery log ? */
    return NO_ERROR;
  }

} /* namespace cubreplication */
