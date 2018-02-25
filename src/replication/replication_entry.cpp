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

#include "stream_packer.hpp"
#include "replication_entry.hpp"
#include "object_representation.h"


size_t single_row_repl_entry::get_packed_size (packer *serializator)
{
  int i;
  size_t entry_size = 0;

  entry_size += OR_INT_SIZE
    + OR_BYTE_SIZE + strlen (class_name)
    + OR_INT_SIZE * (changed_attributes.size () + 1);

  for (i = 0; i < new_values.size() ; i++)
    {
      entry_size += or_db_value_size (&new_values[i]);
    }

  return entry_size;
}

int single_row_repl_entry::pack (packer *serializator)
{
  int i;

  serializator->pack_int ((int) m_type);
  serializator->pack_small_string (class_name);
  serializator->pack_int_vector (changed_attributes);
  serializator->pack_int ((int) new_values.size());
  for (i = 0; i < new_values.size(); i++)
    {
      serializator->pack_db_value (new_values[i]);
    }
  return NO_ERROR;
}

int single_row_repl_entry::unpack (packer *serializator)
{
  int count_new_values = 0;
  int i;
  int int_val;

  serializator->unpack_int (&int_val);
  m_type = (REPL_ENTRY_TYPE) int_val;
  serializator->unpack_small_string (class_name, sizeof (class_name) - 1);
  serializator->unpack_int_vector (changed_attributes);
  
  serializator->unpack_int (&count_new_values);
  for (i = 0; i < count_new_values; i++)
    {
      DB_VALUE val;

      /* this copies the DB_VALUE to contains, should we avoid this ? */
      new_values.push_back (val);
      serializator->unpack_db_value (&val);
    }
  return NO_ERROR;
}

/////////////////////////////////
size_t sbr_repl_entry::get_packed_size (packer *serializator)
{
  return m_statement.size () + 1;
}

int sbr_repl_entry::pack (packer *serializator)
{
  NOT_IMPLEMENTED ();
  return NO_ERROR;
}

int sbr_repl_entry::unpack (packer *serializator)
{
  NOT_IMPLEMENTED ();
  return NO_ERROR;
}
//////////////////////////
 replication_object_builder::replication_object_builder ()
 {
   add_pattern_object (new sbr_repl_entry());
   add_pattern_object (new single_row_repl_entry());
 }