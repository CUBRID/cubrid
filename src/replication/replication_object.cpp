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
 * replication_object.cpp
 */

#ident "$Id$"

#include "memory_alloc.h"
#include "object_primitive.h"
#include "object_representation.h"
#include "thread_manager.hpp"
#include "replication_object.hpp"

namespace cubreplication
{

  single_row_repl_entry::single_row_repl_entry (const REPL_ENTRY_TYPE type, const char *class_name)
  {
    m_type = type;
    set_class_name (class_name);
  }

  single_row_repl_entry::~single_row_repl_entry ()
  {
    cubthread::entry *my_thread = thread_get_thread_entry_info ();

    HL_HEAPID save_heapid;
    save_heapid = db_change_private_heap (my_thread, 0);

    pr_clear_value (&m_key_value);

    for (std::vector <DB_VALUE>::iterator it = m_new_values.begin (); it != m_new_values.end (); it++)
      {
	pr_clear_value (& (*it));
      }
    (void) db_change_private_heap (my_thread, save_heapid);
  }

  int single_row_repl_entry::apply (void)
  {
    /* TODO */
    return NO_ERROR;
  }

  bool single_row_repl_entry::is_equal (const packable_object *other)
  {
    size_t i;
    const single_row_repl_entry *other_t = dynamic_cast<const single_row_repl_entry *> (other);

    if (other_t == NULL
	|| m_type != other_t->m_type
	|| strcmp (m_class_name, other_t->m_class_name) != 0
	|| db_value_compare (&m_key_value, &other_t->m_key_value) != DB_EQ
	|| changed_attributes.size () != other_t->changed_attributes.size ()
	|| m_new_values.size () != other_t->m_new_values.size ())
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

    for (i = 0; i < m_new_values.size (); i++)
      {
	if (db_value_compare (&m_new_values[i], &other_t->m_new_values[i]) != DB_EQ)
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
    HL_HEAPID save_heapid;
    save_heapid = db_change_private_heap (NULL, 0);
    pr_clone_value (db_val, &m_key_value);
    (void) db_change_private_heap (NULL, save_heapid);
  }

  void single_row_repl_entry::copy_and_add_changed_value (const int att_id, DB_VALUE *db_val)
  {
    HL_HEAPID save_heapid;

    m_new_values.emplace_back ();
    DB_VALUE &last_new_value = m_new_values.back ();

    changed_attributes.push_back (att_id);

    save_heapid = db_change_private_heap (NULL, 0);
    pr_clone_value (db_val, &last_new_value);
    (void) db_change_private_heap (NULL, save_heapid);
  }

  size_t single_row_repl_entry::get_packed_size (cubpacking::packer &serializator) const
  {
    size_t i;
    size_t entry_size = 0;

    /* we assume that offset start has already MAX_ALIGNMENT */

    /* type of packed object + type of RBR entry */
    entry_size += 2 * serializator.get_packed_int_size (entry_size);

    entry_size += serializator.get_packed_int_vector_size (entry_size, (int) changed_attributes.size ());

    entry_size += serializator.get_packed_small_string_size (m_class_name, entry_size);

    entry_size += serializator.get_packed_db_value_size (m_key_value, entry_size);

    /* count of new_values */
    entry_size += serializator.get_packed_int_size (entry_size);

    for (i = 0; i < m_new_values.size (); i++)
      {
	entry_size += serializator.get_packed_db_value_size (m_new_values[i], entry_size);
      }

    return entry_size;
  }

  void single_row_repl_entry::pack (cubpacking::packer &serializator) const
  {
    size_t i;

    serializator.pack_int (single_row_repl_entry::PACKING_ID);

    serializator.pack_int ((int) m_type);

    serializator.pack_int_vector (changed_attributes);

    serializator.pack_small_string (m_class_name);

    serializator.pack_db_value (m_key_value);

    serializator.pack_int ((int) m_new_values.size ());

    for (i = 0; i < m_new_values.size (); i++)
      {
	serializator.pack_db_value (m_new_values[i]);
      }
  }

  void single_row_repl_entry::unpack (cubpacking::unpacker &deserializator)
  {
    int count_new_values = 0;
    int i;
    int int_val;
#if defined (SERVER_MODE)
    HL_HEAPID save_heapid;

    save_heapid = db_private_set_heapid_to_thread (NULL, 0);
#endif
    /* create id */
    deserializator.unpack_int (int_val);

    /* RBR type */
    deserializator.unpack_int (int_val);
    m_type = (REPL_ENTRY_TYPE) int_val;

    deserializator.unpack_int_vector (changed_attributes);

    deserializator.unpack_small_string (m_class_name, sizeof (m_class_name) - 1);

    deserializator.unpack_db_value (m_key_value);

    deserializator.unpack_int (count_new_values);

    for (i = 0; i < count_new_values; i++)
      {
	m_new_values.emplace_back ();
	deserializator.unpack_db_value (m_new_values.back ());
      }

#if defined (SERVER_MODE)
    db_private_set_heapid_to_thread (NULL, save_heapid);
#endif
  }

  /////////////////////////////////
  int sbr_repl_entry::apply (void)
  {
    /* TODO */
    return NO_ERROR;
  }

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

  size_t sbr_repl_entry::get_packed_size (cubpacking::packer &serializator) const
  {
    /* we assume that offset start has already MAX_ALIGNMENT */

    /* type of packed object */
    size_t entry_size = serializator.get_packed_int_size (0);

    entry_size += serializator.get_packed_large_string_size (m_statement, entry_size);

    return entry_size;
  }

  void sbr_repl_entry::pack (cubpacking::packer &serializator) const
  {
    serializator.pack_int (sbr_repl_entry::PACKING_ID);
    serializator.pack_large_string (m_statement);
  }

  void sbr_repl_entry::unpack (cubpacking::unpacker &deserializator)
  {
    int entry_type_not_used;

    deserializator.unpack_int (entry_type_not_used);
    deserializator.unpack_large_string (m_statement);
  }

} /* namespace cubreplication */
