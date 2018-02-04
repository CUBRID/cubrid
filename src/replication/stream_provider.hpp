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
 * stream_provider.hpp
 */

#ident "$Id$"

#ifndef _STREAM_PROVIDER_HPP_
#define _STREAM_PROVIDER_HPP_

class serial_buffer;

class stream_provider
{
private:
public:
  int fetch_for_read (serial_buffer *existing_buffer, const size_t amount) = 0;
  int extend_for_write (serial_buffer **existing_buffer, const size_t amount) = 0;
};


#endif /* _STREAM_PROVIDER_HPP_ */
