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
 * pinnable_buffer.cpp
 */

#ident "$Id$"

#include "pinnable_buffer.hpp"
#include "pinning.hpp"

namespace mem
{

  int pinnable_buffer::init (char *ptr, const size_t buf_size, cubbase::pinner *referencer)
  {
    m_storage = ptr;
    m_end_ptr = (ptr + buf_size);

    if (referencer != NULL)
      {
	referencer->pin (this);
      }

    return NO_ERROR;
  }

} /* namespace mem */
