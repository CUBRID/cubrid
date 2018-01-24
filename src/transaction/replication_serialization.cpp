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
 * replication_serialization.cpp
 */

#ident "$Id$"

#ifndef _REPLICATION_SERIALIZATION_CPP_
#define _REPLICATION_SERIALIZATION_CPP_

#include "replication_serialization.hpp"
#include "replication_entry.hpp"

int replication_serialization::pack_int (const int &value)
{
  ptr = buffer->reserve (OR_INT_SIZE);

  if (ptr != NULL)
    {
      OR_PUT_INT (ptr, value);
    }
}

int replication_serialization::un_pack_int (const int *value)
{
  ptr = buffer->reserve (OR_INT_SIZE);

  if (ptr != NULL)
    {
    OR_PUT_INT (ptr, value);
    }
}

int replication_serialization::pack_db_value (const DB_VALUE &value)
{
  size_t value_size = or_packed_value_size (&value, 1, 0, 0);

  ptr = buffer->reserve (value_size);

  if (ptr != NULL)
    {
      or_pack_value (ptr, &value);
    }
}

#endif /* _REPLICATION_SERIALIZATION_CPP_ */
