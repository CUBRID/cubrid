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
 * cubstream.cpp
 */

#ident "$Id$"

#include "cubstream.hpp"
#include "error_code.h"
#include <algorithm>

namespace cubstream
{

  stream::stream ()
  {
    m_last_reported_ready_pos = 0;
    m_read_position = 0;


    set_filled_stream_handler (NULL);
    set_fetch_data_handler (NULL);
    set_ready_pos_handler (NULL);

    init (0);
  }

  int stream::init (const stream_position &start_position)
  {
    m_append_position = start_position;

    return NO_ERROR;
  }

} /* namespace cubstream */
