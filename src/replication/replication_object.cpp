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

#include "replication_object.hpp"
#include "object_representation.h"
#include "thread_manager.hpp"
#include "memory_alloc.h"
#include "object_primitive.h"
#include "string_buffer.hpp"

namespace cubreplication
{
  static const char *repl_entry_type_str[] = {"update",
					      "insert",
					      "delete"
					     };

  single_row_repl_entry::single_row_repl_entry (const REPL_ENTRY_TYPE type, const char *class_name) :  m_type (type),
    m_class_name (class_name)
  {
  }

  single_row_repl_entry::~single_row_repl_entry ()
  {
    //TODO[arnia] optimize
    cubthread::entry *my_thread = thread_get_thread_entry_info ();

    HL_HEAPID save_heapid;
    save_heapid = db_change_private_heap (my_thread, 0);

    pr_clear_value (&m_key_value);

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
	|| m_class_name.compare (other_t->m_class_name) != 0
	|| db_value_compare (&m_key_value, &other_t->m_key_value) != DB_EQ)
      {
	return false;
      }

    return true;
  }

  void single_row_repl_entry::set_key_value (DB_VALUE *db_val)
  {
    HL_HEAPID save_heapid;
    save_heapid = db_change_private_heap (NULL, 0);
    pr_clone_value (db_val, &m_key_value);
    (void) db_change_private_heap (NULL, save_heapid);
  }

  std::size_t single_row_repl_entry::get_packed_size (cubpacking::packer *serializator, std::size_t start_offset)
  {
    std::size_t entry_size = start_offset;

    /* we assume that offset start has already MAX_ALIGNMENT */

    /* type of RBR entry */
    entry_size += serializator->get_packed_int_size (entry_size);
    entry_size += serializator->get_packed_string_size (m_class_name, entry_size);
    entry_size += serializator->get_packed_db_value_size (m_key_value, entry_size);

    return entry_size;
  }

  int single_row_repl_entry::pack (cubpacking::packer *serializator)
  {
    serializator->pack_int ((int) m_type);
    serializator->pack_string (m_class_name);
    serializator->pack_db_value (m_key_value);

    return NO_ERROR;
  }

  int single_row_repl_entry::unpack (cubpacking::packer *serializator)
  {
    int i;
    int int_val;
    HL_HEAPID save_heapid;

    save_heapid = db_change_private_heap (NULL, 0);

    /* RBR type */
    serializator->unpack_int (&int_val);
    m_type = (REPL_ENTRY_TYPE) int_val;

    serializator->unpack_string (m_class_name);

    serializator->unpack_db_value (&m_key_value);

    (void) db_change_private_heap (NULL, save_heapid);

    return NO_ERROR;
  }

  void single_row_repl_entry::stringify (string_buffer &str)
  {
    char *key_to_string = pr_valstring (NULL, &m_key_value);

    str ("type=%s key_dbvalue=%s table=%s\n", repl_entry_type_str[m_type], key_to_string,
	 m_class_name.c_str ());

    db_private_free (NULL, key_to_string);
  }

  /////////////////////////////////
  sbr_repl_entry::sbr_repl_entry (const char *statement, const char *user,
				  const char *sys_prm_ctx) : m_statement (statement), m_db_user (user), m_sys_prm_context (sys_prm_ctx)
  {
  }

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

  std::size_t sbr_repl_entry::get_packed_size (cubpacking::packer *serializator, std::size_t start_offset)
  {
    /* we assume that offset start has already MAX_ALIGNMENT */

    /* type of packed object */
    std::size_t entry_size = start_offset;

    entry_size += serializator->get_packed_int_size (0);

    entry_size += serializator->get_packed_string_size (m_statement, entry_size);
    entry_size += serializator->get_packed_string_size (m_db_user, entry_size);
    entry_size += serializator->get_packed_string_size (m_sys_prm_context, entry_size);

    return entry_size;
  }

  int sbr_repl_entry::pack (cubpacking::packer *serializator)
  {
    serializator->pack_int (sbr_repl_entry::PACKING_ID);
    serializator->pack_string (m_statement);
    serializator->pack_string (m_db_user);
    serializator->pack_string (m_sys_prm_context);

    return NO_ERROR;
  }

  int sbr_repl_entry::unpack (cubpacking::packer *serializator)
  {
    int entry_type_not_used;

    serializator->unpack_int (&entry_type_not_used);
    serializator->unpack_string (m_statement);
    serializator->unpack_string (m_db_user);
    serializator->unpack_string (m_sys_prm_context);

    return NO_ERROR;
  }

  void sbr_repl_entry::stringify (string_buffer &str)
  {
    str ("sbr_repl_entry: statement=%s\n", m_statement.c_str ());
  }

  changed_attrs_row_repl_entry::~changed_attrs_row_repl_entry ()
  {
    cubthread::entry *my_thread = thread_get_thread_entry_info ();

    HL_HEAPID save_heapid;
    save_heapid = db_change_private_heap (my_thread, 0);

    for (std::vector <DB_VALUE>::iterator it = m_new_values.begin (); it != m_new_values.end (); it++)
      {
	pr_clear_value (& (*it));
      }
    (void) db_change_private_heap (my_thread, save_heapid);
  }

  void changed_attrs_row_repl_entry::copy_and_add_changed_value (const int att_id,
      DB_VALUE *db_val)
  {
    HL_HEAPID save_heapid;

#if defined(CUBRID_DEBUG)
    std::vector<int>::iterator it;

    it = find (m_changed_attributes.begin (), m_changed_attributes.end (), att_id);
    assert (it == m_changed_attributes.end ());
#endif

    m_new_values.emplace_back ();
    DB_VALUE &last_new_value = m_new_values.back ();

    m_changed_attributes.push_back (att_id);

    save_heapid = db_change_private_heap (NULL, 0);
    pr_clone_value (db_val, &last_new_value);
    (void) db_change_private_heap (NULL, save_heapid);
  }

  int changed_attrs_row_repl_entry::pack (cubpacking::packer *serializator)
  {
    int rc = NO_ERROR;

    rc = serializator->pack_int (changed_attrs_row_repl_entry::PACKING_ID);
    if (rc != NO_ERROR)
      {
	assert (false);
	return rc;
      }

    single_row_repl_entry::pack (serializator);
    serializator->pack_int_vector (m_changed_attributes);
    serializator->pack_int ((int) m_new_values.size ());

    for (std::size_t i = 0; i < m_new_values.size (); i++)
      {
	serializator->pack_db_value (m_new_values[i]);
      }

    return NO_ERROR;
  }

  int changed_attrs_row_repl_entry::unpack (cubpacking::packer *serializator)
  {
    int count_new_values = 0;
    int int_val;
#if defined (SERVER_MODE)
    HL_HEAPID save_heapid;

    save_heapid = db_change_private_heap (NULL, 0);
#endif
    /* create id */
    serializator->unpack_int (&int_val);

    single_row_repl_entry::unpack (serializator);

    serializator->unpack_int_vector (m_changed_attributes);

    serializator->unpack_int (&count_new_values);

    for (std::size_t i = 0; i < count_new_values; i++)
      {
	DB_VALUE val;

	/* this copies the DB_VALUE to contain, should we avoid this ? */
	m_new_values.push_back (val);
	serializator->unpack_db_value (&val);
      }

#if defined (SERVER_MODE)
    (void) db_change_private_heap (NULL, save_heapid);
#endif

    return NO_ERROR;
  }

  std::size_t changed_attrs_row_repl_entry::get_packed_size (cubpacking::packer *serializator, std::size_t start_offset)
  {
    /* we assume that offset start has already MAX_ALIGNMENT */

    /* type of packed object */
    std::size_t entry_size = start_offset;

    entry_size += serializator->get_packed_int_size (0);

    entry_size = single_row_repl_entry::get_packed_size (serializator, entry_size);

    entry_size += serializator->get_packed_int_vector_size (entry_size, (int) m_changed_attributes.size ());
    entry_size += serializator->get_packed_int_size (entry_size);
    for (std::size_t i = 0; i < m_new_values.size (); i++)
      {
	entry_size += serializator->get_packed_db_value_size (m_new_values[i], entry_size);
      }

    return entry_size;
  }

  bool changed_attrs_row_repl_entry::is_equal (const packable_object *other)
  {
    size_t i;
    bool check = single_row_repl_entry::is_equal (other);
    const changed_attrs_row_repl_entry *other_t = NULL;

    if (!check)
      {
	return false;
      }

    other_t = dynamic_cast<const changed_attrs_row_repl_entry *> (other);

    if (other_t == NULL
	|| m_changed_attributes.size () != other_t->m_changed_attributes.size ()
	|| m_new_values.size () != other_t->m_new_values.size ())
      {
	return false;
      }

    for (i = 0; i < m_changed_attributes.size (); i++)
      {
	if (m_changed_attributes[i] != other_t->m_changed_attributes[i])
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

  void changed_attrs_row_repl_entry::stringify (string_buffer &str)
  {
    str ("changed_attrs_row_repl_entry:\n");

    for (std::size_t i = 0; i < m_changed_attributes.size (); i++)
      {
	char *key_to_string = pr_valstring (NULL, &m_new_values[i]);
	assert (key_to_string != NULL);

	str ("id=%d value=%s\n", m_changed_attributes[i], key_to_string);

	db_private_free (NULL, key_to_string);
      }

    str ("inst oid: pageid:%d slotid:%d volid:%d\n", m_inst_oid.pageid, m_inst_oid.slotid, m_inst_oid.volid);

    single_row_repl_entry::stringify (str);
  }

  changed_attrs_row_repl_entry::changed_attrs_row_repl_entry (REPL_ENTRY_TYPE type,
      const char *class_name,
      const OID *inst_oid) : single_row_repl_entry (type, class_name)
  {
    if (inst_oid != NULL)
      {
	m_inst_oid = *inst_oid;
      }
  }

  int rec_des_row_repl_entry::pack (cubpacking::packer *serializator)
  {
    int rc;

    rc = serializator->pack_int (rec_des_row_repl_entry::PACKING_ID);
    if (rc != NO_ERROR)
      {
	assert (false);
	return rc;
      }

    (void) single_row_repl_entry::pack (serializator);

    rc = m_rec_des.pack (serializator);
    if (rc != NO_ERROR)
      {
	assert (false);
	return rc;
      }

    return NO_ERROR;
  }

  int rec_des_row_repl_entry::unpack (cubpacking::packer *serializator)
  {
    int entry_type_not_used, rc;

    rc = serializator->unpack_int (&entry_type_not_used);
    if (rc != NO_ERROR)
      {
	assert (false);
	return rc;
      }

    (void) single_row_repl_entry::unpack (serializator);

    rc = m_rec_des.unpack (serializator);
    if (rc != NO_ERROR)
      {
	assert (false);
	return rc;
      }

    return NO_ERROR;
  }

  std::size_t rec_des_row_repl_entry::get_packed_size (cubpacking::packer *serializator, std::size_t start_offset)
  {
    /* we assume that offset start has already MAX_ALIGNMENT */

    /* type of packed object */
    std::size_t entry_size = start_offset;

    entry_size += serializator->get_packed_int_size (0);

    entry_size = single_row_repl_entry::get_packed_size (serializator, entry_size);
    entry_size += m_rec_des.get_packed_size (serializator, entry_size);

    return entry_size;
  }

  bool rec_des_row_repl_entry::is_equal (const packable_object *other)
  {
    size_t i;
    bool check = single_row_repl_entry::is_equal (other);
    const rec_des_row_repl_entry *other_t = NULL;

    if (!check)
      {
	return false;
      }

    other_t = dynamic_cast<const rec_des_row_repl_entry *> (other);

    if (other_t == NULL ||
	m_type != other_t->m_type)
      {
	return false;
      }

    if (m_type == cubreplication::REPL_ENTRY_TYPE::REPL_DELETE)
      {
	return true;
      }

    if (m_rec_des.length != other_t->m_rec_des.length ||
	m_rec_des.area_size != other_t->m_rec_des.area_size ||
	m_rec_des.type != other_t->m_rec_des.type ||
	memcmp (m_rec_des.data, other_t->m_rec_des.data, m_rec_des.length) != 0)
      {
	return false;
      }

    return true;
  }

  rec_des_row_repl_entry::rec_des_row_repl_entry (REPL_ENTRY_TYPE type,
      const char *class_name,
      RECDES *rec_des) : single_row_repl_entry (type, class_name)
  {
    if (type != cubreplication::REPL_ENTRY_TYPE::REPL_DELETE)
      {
	assert (rec_des != NULL);

	m_rec_des.length = rec_des->length;
	m_rec_des.area_size = rec_des->area_size;
	m_rec_des.type = rec_des->type;

	m_rec_des.data = (char *) malloc (m_rec_des.length);
	if (m_rec_des.data == NULL)
	  {
	    assert (false);
	  }
	memcpy (m_rec_des.data, rec_des->data, m_rec_des.length);
      }
    else
      {
	memset (&m_rec_des, 0, sizeof (m_rec_des));
      }

  }

  rec_des_row_repl_entry::~rec_des_row_repl_entry ()
  {
    if (m_rec_des.data != NULL)
      {
	free (m_rec_des.data);
      }
  }

  void rec_des_row_repl_entry::stringify (string_buffer &str)
  {
    str ("rec_des_row_repl_entry:\n");

    if (m_rec_des.data != NULL)
      {
	str ("recdes length=%d\n", m_rec_des.length);
      }

    single_row_repl_entry::stringify (str);
  }

} /* namespace cubreplication */
