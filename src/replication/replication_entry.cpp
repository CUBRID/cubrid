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

#include "replication_entry.hpp"
#include "object_representation.h"

namespace cubreplication
{

  single_row_repl_entry::single_row_repl_entry (const REPL_ENTRY_TYPE type, const char *class_name)
  {
    m_type = type;
    set_class_name (class_name);
  }

  single_row_repl_entry::~single_row_repl_entry()
  {
    /* TODO : clear DB_VALUE ? */
    pr_clear_value (&m_key_value);
    for (std::vector <DB_VALUE>::iterator it = new_values.begin (); it != new_values.end (); it++)
      {
	pr_clear_value (& (*it));
      }
  }

  bool single_row_repl_entry::is_equal (const packable_object *other)
  {
    int i;
    const single_row_repl_entry *other_t = dynamic_cast<const single_row_repl_entry *> (other);

    if (other_t == NULL
	|| m_type != other_t->m_type
	|| strcmp (m_class_name, other_t->m_class_name) != 0
	|| db_value_compare (&m_key_value, &other_t->m_key_value) != DB_EQ
	|| changed_attributes.size () != other_t->changed_attributes.size ()
	|| new_values.size () != other_t->new_values.size ())
      {
	return false;
      }

    for (i = 0; i < changed_attributes.size (); i++)
      {
	if (changed_attributes[i] != other_t->changed_attributes[i])
	  {
	    return false;
	  }
      }

    for (i = 0; i < new_values.size (); i++)
      {
	if (db_value_compare (&new_values[i], &other_t->new_values[i]) != DB_EQ)
	  {
	    return false;
	  }
      }

    return true;
  }

  void single_row_repl_entry::set_class_name (const char *class_name)
  {
    strncpy (m_class_name, class_name, sizeof (m_class_name) - 1);
  }

  void single_row_repl_entry::set_key_value (DB_VALUE *db_val)
  {
    pr_clone_value (db_val, &m_key_value);
  }

  void single_row_repl_entry::add_changed_value (const int att_id, DB_VALUE *db_val)
  {
    changed_attributes.push_back (att_id);
    new_values.push_back (*db_val);
  }

  size_t single_row_repl_entry::get_packed_size (cubpacking::packer *serializator)
  {
    int i;
    size_t entry_size = 0;

    /* we assume that offset start has already MAX_ALIGNMENT */

    /* type of packed object  + type of RBR entry  */
    entry_size += 2 * serializator->get_packed_int_size (entry_size);
    entry_size += serializator->get_packed_int_vector_size (entry_size, (int) changed_attributes.size ());
    entry_size += serializator->get_packed_small_string_size (m_class_name, entry_size);
    entry_size += serializator->get_packed_db_value_size (m_key_value, entry_size);
    /* count of new_values */
    entry_size += serializator->get_packed_int_size (entry_size);
    for (i = 0; i < new_values.size() ; i++)
      {
	entry_size += serializator->get_packed_db_value_size (new_values[i], entry_size);
      }

    return entry_size;
  }

  int single_row_repl_entry::pack (cubpacking::packer *serializator)
  {
    int i;

    serializator->pack_int (single_row_repl_entry::ID);
    serializator->pack_int ((int) m_type);
    serializator->pack_int_vector (changed_attributes);
    serializator->pack_small_string (m_class_name);
    serializator->pack_db_value (m_key_value);
    serializator->pack_int ((int) new_values.size ());
    for (i = 0; i < new_values.size(); i++)
      {
	serializator->pack_db_value (new_values[i]);
      }

    return NO_ERROR;
  }

  int single_row_repl_entry::unpack (cubpacking::packer *serializator)
  {
    int count_new_values = 0;
    int i;
    int int_val;

    /* create id */
    serializator->unpack_int (&int_val);
    /* RBR type */
    serializator->unpack_int (&int_val);
    m_type = (REPL_ENTRY_TYPE) int_val;
    serializator->unpack_int_vector (changed_attributes);
    serializator->unpack_small_string (m_class_name, sizeof (m_class_name) - 1);

    serializator->unpack_db_value (&m_key_value);
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

  bool sbr_repl_entry::is_equal (const packable_object *other)
  {
    const sbr_repl_entry *other_t = dynamic_cast<const sbr_repl_entry *> (other);

    if (other_t == NULL
	|| m_statement != other_t->m_statement)
      {
	return false;
      }
    return true;
  }

  size_t sbr_repl_entry::get_packed_size (cubpacking::packer *serializator)
  {
    /* we assume that offset start has already MAX_ALIGNMENT */

    /* type of packed object */
    size_t entry_size = serializator->get_packed_int_size (0);

    entry_size += serializator->get_packed_large_string_size (m_statement, entry_size);

    return entry_size;
  }

  int sbr_repl_entry::pack (cubpacking::packer *serializator)
  {
    serializator->pack_int (sbr_repl_entry::ID);
    serializator->pack_large_string (m_statement);
    return NO_ERROR;
  }

  int sbr_repl_entry::unpack (cubpacking::packer *serializator)
  {
    int entry_type_not_used;
    serializator->unpack_int (&entry_type_not_used);
    serializator->unpack_large_string (m_statement);
    return NO_ERROR;
  }

} /* namespace cubreplication */
