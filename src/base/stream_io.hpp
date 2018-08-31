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
 * stream_io.hpp
 */

#ifndef _STREAM_IO_HPP_
#define _STREAM_IO_HPP_

#ident "$Id$"

#include "cubstream.hpp"

namespace cubstream
{

  class stream_io
  {
    public:
      virtual int write (const stream_position &pos, const char *buf, const size_t amount) = 0;

      virtual int read (const stream_position &pos, const char *buf, const size_t amount) = 0;
  };

} /*  namespace cubstream */

#endif /* _STREAM_IO_HPP_ */
