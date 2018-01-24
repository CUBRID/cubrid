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
 * replication_serialization.hpp
 */

#ident "$Id$"

#ifndef _REPLICATION_SERIALIZATION_HPP_
#define _REPLICATION_SERIALIZATION_HPP_

#include "dbtype.h"

class serial_buffer;

class repl_serialization
{
public:
  repl_serialization (serial_buffer *buf) : buffer (buf)
    {
    };

  int pack_int (const int &value);
  int pack_db_value (const DB_VALUE &value);

  int unpack_int (int &value);
  int unpack_db_value (DB_VALUE &value);
private:
  serial_buffer *buffer;
};


#endif /* _REPLICATION_SERIALIZATION_HPP_ */
