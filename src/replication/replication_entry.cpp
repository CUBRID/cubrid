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
 * replication_entry.cpp
 */

#ident "$Id$"

#include "replication_serialization.hpp"


const size_t replication_entry::UNDEFINED_SIZE;

size_t single_row_repl_entry::get_packed_size (const replication_serialization *serializator)
{
  if (packed_size != UNDEFINED_SIZE)
    {
      return packed_size;
    }

  size_t entry_size = 0;

  entry_size += OR_INT_SIZE
    + OR_BYTE_SIZE + strlen (class_name);
    + OR_INT_SIZE * (changed_attributes.size () + 1);

  for (i = 0; i < new_values.size() ; i++)
    {
      entry_size += or_db_value_size (new_values[i]);
    }

  packed_size = entry_size;

  return packed_size;
}

int single_row_repl_entry::pack (const replication_serialization *serializator)
{
  serializator->pack_int (type);
  serializator->pack_small_string (class_name);
  serializator->pack_int_vector (changed_attributes);
  serializator->pack_int (new_values.size());
  for (i = 0; i < new_values.size())
    {
      serializator->pack_db_value (new_values[i]);
    }
  return NO_ERROR;
}
