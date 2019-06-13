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

#include "heap_file.h"
#include "locator_sr.h"
#include "mem_block.hpp"
#include "memory_alloc.h"
#include "object_primitive.h"
#include "object_representation.h"
#include "string_buffer.hpp"
#include "thread_manager.hpp"
#include "transaction_group.hpp"

#include <algorithm>
#include <cstring>

namespace cubreplication
{
  static const char *repl_entry_type_str[] = { "update", "insert", "delete" };

  static LC_COPYAREA_OPERATION
  op_type_from_repl_type_and_prunning (repl_entry_type repl_type)
  {
    assert (repl_type == REPL_UPDATE || repl_type == REPL_INSERT || repl_type == REPL_DELETE);

    switch (repl_type)
      {
      case REPL_UPDATE:
	return LC_FLUSH_UPDATE;

      case REPL_INSERT:
	return LC_FLUSH_INSERT;

      case REPL_DELETE:
	return LC_FLUSH_DELETE;

      default:
	assert (false);
      }

    return LC_FETCH;
  }

  replication_object::replication_object ()
  {
    LSA_SET_NULL (&m_lsa_stamp);
  }

  replication_object::replication_object (LOG_LSA &lsa_stamp)
  {
    LSA_COPY (&m_lsa_stamp, &lsa_stamp);
  }

  void
  replication_object::get_lsa_stamp (LOG_LSA &lsa_stamp)
  {
    LSA_COPY (&lsa_stamp, &m_lsa_stamp);
  }

  void
  replication_object::set_lsa_stamp (const LOG_LSA &lsa_stamp)
  {
    LSA_COPY (&m_lsa_stamp, &lsa_stamp);
  }

  bool
  replication_object::is_instance_changing_attr (const OID &inst_oid)
  {
    return false;
  }

  bool
  replication_object::is_statement_replication ()
  {
    return false;
  }

  repl_gc_info::repl_gc_info (const tx_group &tx_group_node)
  {
    for (const tx_group::node_info &node_info : tx_group_node.get_container ())
      {
	m_gc_trans.emplace_back (tran_info {node_info.m_mvccid, node_info.m_tran_state});
      }
  }

  tx_group
  repl_gc_info::as_tx_group () const
  {
    tx_group res;
    for (const tran_info &ti : m_gc_trans)
      {
	res.add (0, ti.m_mvccid, ti.m_tran_state);
      }
    return std::move (res);
  }

  int repl_gc_info::apply ()
  {
    return 0;
  }

  void repl_gc_info::pack (cubpacking::packer &serializator) const
  {
    serializator.pack_to_int (m_gc_trans.size ());
    for (const tran_info &t : m_gc_trans)
      {
	serializator.pack_to_int (t.m_tran_state);
      }

    for (const tran_info &t : m_gc_trans)
      {
	serializator.pack_bigint (t.m_mvccid);
      }
  }

  void repl_gc_info::unpack (cubpacking::unpacker &deserializator)
  {
    size_t gc_trans_sz;
    deserializator.unpack_from_int (gc_trans_sz);
    m_gc_trans.resize (gc_trans_sz);
    for (size_t i = 0; i < gc_trans_sz; ++i)
      {
	deserializator.unpack_from_int (m_gc_trans[i].m_tran_state);
      }
    for (size_t i = 0; i < gc_trans_sz; ++i)
      {
	deserializator.unpack_bigint (m_gc_trans[i].m_mvccid);
      }
  }

  std::size_t repl_gc_info::get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const
  {
    std::size_t entry_size = start_offset;
    entry_size += serializator.get_packed_int_vector_size (start_offset, m_gc_trans.size ());
    entry_size += DB_ALIGN (entry_size, MAX_ALIGNMENT) - entry_size;
    entry_size += OR_BIGINT_SIZE * m_gc_trans.size ();

    return entry_size;
  }

  void repl_gc_info::stringify (string_buffer &str)
  {
    str ("repl_gc_info:\n");
    for (size_t i = 0; i< m_gc_trans.size (); ++i)
      {
	str ("\ttraninfo[%d]:: mvccid=%d, tran_state=%d\n", i, m_gc_trans[i].m_mvccid, m_gc_trans[i].m_tran_state);
      }
  }

  single_row_repl_entry::single_row_repl_entry (const repl_entry_type type, const char *class_name, LOG_LSA &lsa_stamp)
    : replication_object (lsa_stamp)
    , m_type (type)
    , m_class_name (class_name)
  {
    db_make_null (&m_key_value);
  }

  single_row_repl_entry::~single_row_repl_entry ()
  {
    //TODO[replication] optimize

    if (DB_IS_NULL (&m_key_value))
      {
	return;
      }

    cubthread::entry *my_thread = thread_get_thread_entry_info ();

    HL_HEAPID save_heapid = db_change_private_heap (my_thread, 0);
    pr_clear_value (&m_key_value);
    db_change_private_heap (my_thread, save_heapid);
  }

  int
  single_row_repl_entry::apply (void)
  {
    int err = NO_ERROR;
#if defined (SERVER_MODE)
    assert (m_type == REPL_DELETE);

    LC_COPYAREA_OPERATION op = op_type_from_repl_type_and_prunning (m_type);
    cubthread::entry &my_thread = cubthread::get_entry ();
    std::vector <int> dummy_int_vector;
    std::vector <DB_VALUE> dummy_val_vector;

    err = locator_repl_apply_rbr (&my_thread, op, m_class_name.c_str (), &m_key_value,
				  dummy_int_vector, dummy_val_vector, NULL);
#endif
    return err;
  }

  bool
  single_row_repl_entry::is_equal (const packable_object *other)
  {
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

  void
  single_row_repl_entry::set_key_value (const DB_VALUE &db_val)
  {
    HL_HEAPID save_heapid = db_change_private_heap (NULL, 0);
    pr_clone_value (&db_val, &m_key_value);
    db_change_private_heap (NULL, save_heapid);
  }

  void
  single_row_repl_entry::set_class_name (const char *class_name)
  {
    m_class_name = class_name;
  }

  std::size_t
  single_row_repl_entry::get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const
  {
    std::size_t entry_size = start_offset;

    /* we assume that offset start has already MAX_ALIGNMENT */

    /* type of RBR entry */
    entry_size += serializator.get_packed_int_size (entry_size);
    entry_size += serializator.get_packed_string_size (m_class_name, entry_size);

    HL_HEAPID save_heapid = db_change_private_heap (NULL, 0);
    entry_size += serializator.get_packed_db_value_size (m_key_value, entry_size);
    db_change_private_heap (NULL, save_heapid);

    return entry_size;
  }

  void
  single_row_repl_entry::pack (cubpacking::packer &serializator) const
  {
    serializator.pack_int ((int) m_type);
    serializator.pack_string (m_class_name);

    HL_HEAPID save_heapid = db_change_private_heap (NULL, 0);
    serializator.pack_db_value (m_key_value);
    db_change_private_heap (NULL, save_heapid);
  }

  void
  single_row_repl_entry::unpack (cubpacking::unpacker &deserializator)
  {
    HL_HEAPID save_heapid = db_change_private_heap (NULL, 0);

    /* RBR type */
    int int_val;
    deserializator.unpack_int (int_val);
    m_type = (repl_entry_type) int_val;

    deserializator.unpack_string (m_class_name);
    deserializator.unpack_db_value (m_key_value);

    db_change_private_heap (NULL, save_heapid);
  }

  void
  single_row_repl_entry::stringify (string_buffer &str)
  {
    char *key_to_string = pr_valstring (&m_key_value);

    str ("single_row_repl_entry(%s) key_type=%s key_dbvalue=%s table=%s\n", repl_entry_type_str[m_type],
	 pr_type_name (DB_VALUE_TYPE (&m_key_value)), key_to_string,
	 m_class_name.c_str ());

    db_private_free (NULL, key_to_string);
  }

  /////////////////////////////////
  sbr_repl_entry::sbr_repl_entry (const char *statement, const char *user, const char *password,
				  const char *sys_prm_ctx, LOG_LSA &lsa_stamp)
    : replication_object (lsa_stamp)
    , m_statement (statement)
    , m_db_user (user)
    , m_db_password (password ? password : "")
    , m_sys_prm_context (sys_prm_ctx ? sys_prm_ctx : "")
  {
  }

  int
  sbr_repl_entry::apply (void)
  {
    int err = NO_ERROR;
#if defined (SERVER_MODE)
    cubthread::entry &my_thread = cubthread::get_entry ();
    err = locator_repl_apply_sbr (&my_thread, m_db_user.c_str (), m_db_password.c_str(),
				  m_sys_prm_context.empty () ? NULL : m_sys_prm_context.c_str (),
				  m_statement.c_str ());
#endif
    return err;
  }

  void sbr_repl_entry::set_params (const char *statement, const char *user, const char *password,
				   const char *sys_prm_ctx)
  {
    m_statement = statement;
    m_db_user = user;
    m_db_password = password;
    m_sys_prm_context = sys_prm_ctx;
  }

  void sbr_repl_entry::append_statement (const char *buffer, const size_t buf_size)
  {
    m_statement.append (buffer, buf_size);
  }

  bool
  sbr_repl_entry::is_equal (const packable_object *other)
  {
    const sbr_repl_entry *other_t = dynamic_cast<const sbr_repl_entry *> (other);

    if (other_t == NULL
	|| m_statement != other_t->m_statement
	|| m_db_user != other_t->m_db_user
	|| m_sys_prm_context != other_t->m_sys_prm_context)
      {
	return false;
      }

    assert (m_db_password == other_t->m_db_password);
    return true;
  }

  bool
  sbr_repl_entry::is_statement_replication ()
  {
    return true;
  }

  std::size_t
  sbr_repl_entry::get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const
  {
    /* we assume that offset start has already MAX_ALIGNMENT */

    /* type of packed object */
    std::size_t entry_size = start_offset;

    entry_size += serializator.get_packed_int_size (0);

    entry_size += serializator.get_packed_string_size (m_statement, entry_size);
    entry_size += serializator.get_packed_string_size (m_db_user, entry_size);
    entry_size += serializator.get_packed_string_size (m_db_password, entry_size);
    entry_size += serializator.get_packed_string_size (m_sys_prm_context, entry_size);

    return entry_size;
  }

  void
  sbr_repl_entry::pack (cubpacking::packer &serializator) const
  {
    serializator.pack_int (sbr_repl_entry::PACKING_ID);
    serializator.pack_string (m_statement);
    serializator.pack_string (m_db_user);
    serializator.pack_string (m_db_password);
    serializator.pack_string (m_sys_prm_context);
  }

  void
  sbr_repl_entry::unpack (cubpacking::unpacker &deserializator)
  {
    int entry_type_not_used;

    deserializator.unpack_int (entry_type_not_used);
    deserializator.unpack_string (m_statement);
    deserializator.unpack_string (m_db_user);
    deserializator.unpack_string (m_db_password);
    deserializator.unpack_string (m_sys_prm_context);
  }

  void
  sbr_repl_entry::stringify (string_buffer &str)
  {
    str ("sbr_repl_entry: statement=%s\nUSER=%s\nSYS_PRM=%s\n",
	 m_statement.c_str (), m_db_user.c_str (), m_sys_prm_context.c_str ());
  }

  changed_attrs_row_repl_entry::~changed_attrs_row_repl_entry ()
  {
    cubthread::entry *my_thread = thread_get_thread_entry_info ();

    HL_HEAPID save_heapid = db_change_private_heap (my_thread, 0);

    for (std::vector <DB_VALUE>::iterator it = m_new_values.begin (); it != m_new_values.end (); it++)
      {
	pr_clear_value (& (*it));
      }
    db_change_private_heap (my_thread, save_heapid);
  }

  void
  changed_attrs_row_repl_entry::copy_and_add_changed_value (const ATTR_ID att_id, const DB_VALUE &db_val)
  {
    m_new_values.emplace_back ();
    DB_VALUE &last_new_value = m_new_values.back ();

    m_changed_attributes.push_back (att_id);

    HL_HEAPID save_heapid = db_change_private_heap (NULL, 0);
    pr_clone_value (&db_val, &last_new_value);
    db_change_private_heap (NULL, save_heapid);
  }

  int
  changed_attrs_row_repl_entry::apply (void)
  {
    int err = NO_ERROR;

#if defined (SERVER_MODE)
    LC_COPYAREA_OPERATION op = op_type_from_repl_type_and_prunning (m_type);
    cubthread::entry &my_thread = cubthread::get_entry ();

    err = locator_repl_apply_rbr (&my_thread, op, m_class_name.c_str (), &m_key_value,
				  m_changed_attributes, m_new_values, NULL);
#endif

    return err;
  }

  void
  changed_attrs_row_repl_entry::pack (cubpacking::packer &serializator) const
  {
    serializator.pack_int (changed_attrs_row_repl_entry::PACKING_ID);
    single_row_repl_entry::pack (serializator);
    serializator.pack_int_vector (m_changed_attributes);
    serializator.pack_int ((int) m_new_values.size ());

    for (std::size_t i = 0; i < m_new_values.size (); i++)
      {
	serializator.pack_db_value (m_new_values[i]);
      }
  }

  void
  changed_attrs_row_repl_entry::unpack (cubpacking::unpacker &deserializator)
  {
    int count_new_values = 0;
    int int_val;

    OID_SET_NULL (&m_inst_oid);

#if defined (SERVER_MODE)
    HL_HEAPID save_heapid = db_change_private_heap (NULL, 0);
#endif
    /* create id */
    deserializator.unpack_int (int_val);

    single_row_repl_entry::unpack (deserializator);

    deserializator.unpack_int_vector (m_changed_attributes);

    deserializator.unpack_int (count_new_values);

    for (std::size_t i = 0; (int) i < count_new_values; i++)
      {
	m_new_values.emplace_back ();
	DB_VALUE &val = m_new_values.back ();
	deserializator.unpack_db_value (val);
      }

#if defined (SERVER_MODE)
    db_change_private_heap (NULL, save_heapid);
#endif
  }

  std::size_t
  changed_attrs_row_repl_entry::get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const
  {
    /* we assume that offset start has already MAX_ALIGNMENT */

    /* type of packed object */
    std::size_t entry_size = start_offset;

    entry_size += serializator.get_packed_int_size (0);

    entry_size = single_row_repl_entry::get_packed_size (serializator, entry_size);

    entry_size += serializator.get_packed_int_vector_size (entry_size, (int) m_changed_attributes.size ());
    entry_size += serializator.get_packed_int_size (entry_size);
    for (std::size_t i = 0; i < m_new_values.size (); i++)
      {
	entry_size += serializator.get_packed_db_value_size (m_new_values[i], entry_size);
      }

    return entry_size;
  }

  bool
  changed_attrs_row_repl_entry::is_equal (const packable_object *other)
  {
    std::size_t i;
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

  void
  changed_attrs_row_repl_entry::stringify (string_buffer &str)
  {
    str ("changed_attrs_row_repl_entry::");
    single_row_repl_entry::stringify (str);

    for (std::size_t i = 0; i < m_changed_attributes.size (); i++)
      {
	char *key_to_string = pr_valstring (&m_new_values[i]);
	assert (key_to_string != NULL);

	str ("\tattr_id=%d type=%s value=%s\n", m_changed_attributes[i], pr_type_name (DB_VALUE_TYPE (&m_new_values[i])),
	     key_to_string);

	db_private_free (NULL, key_to_string);
      }

    str ("inst oid: pageid:%d slotid:%d volid:%d\n", m_inst_oid.pageid, m_inst_oid.slotid, m_inst_oid.volid);
  }

  bool
  changed_attrs_row_repl_entry::is_instance_changing_attr (const OID &inst_oid)
  {
    if (compare_inst_oid (inst_oid))
      {
	return true;
      }

    return false;
  }

  changed_attrs_row_repl_entry::changed_attrs_row_repl_entry (repl_entry_type type, const char *class_name,
      const OID &inst_oid, LOG_LSA &lsa_stamp)
    : single_row_repl_entry (type, class_name, lsa_stamp)
    , m_changed_attributes ()
    , m_new_values ()
  {
    m_inst_oid = inst_oid;
  }

  int
  rec_des_row_repl_entry::apply (void)
  {
    int err = NO_ERROR;

#if defined (SERVER_MODE)
    assert (m_type != REPL_DELETE);

    LC_COPYAREA_OPERATION op = op_type_from_repl_type_and_prunning (m_type);
    cubthread::entry &my_thread = cubthread::get_entry ();

    std::vector<int> dummy_int_vector;
    std::vector<DB_VALUE> dummy_val_vector;

    err = locator_repl_apply_rbr (&my_thread, op, m_class_name.c_str (), &m_key_value,
				  dummy_int_vector, dummy_val_vector, &m_rec_des.get_recdes ());
#endif

    return err;
  }

  void
  rec_des_row_repl_entry::pack (cubpacking::packer &serializator) const
  {
    serializator.pack_int (rec_des_row_repl_entry::PACKING_ID);
    (void) single_row_repl_entry::pack (serializator);

    m_rec_des.pack (serializator);
  }

  void
  rec_des_row_repl_entry::unpack (cubpacking::unpacker &deserializator)
  {
    int entry_type_not_used;

    deserializator.unpack_int (entry_type_not_used);

    (void) single_row_repl_entry::unpack (deserializator);

    m_rec_des.resize_buffer ((size_t) DB_PAGESIZE);
    m_rec_des.unpack (deserializator);
    // record may be resized by heap_update_adjust_recdes_header; make sure there is enough space.
    heap_record_reserve_for_adjustments (m_rec_des);
  }

  std::size_t
  rec_des_row_repl_entry::get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const
  {
    /* we assume that offset start has already MAX_ALIGNMENT */

    /* type of packed object */
    std::size_t entry_size = start_offset;

    entry_size += serializator.get_packed_int_size (0);

    entry_size = single_row_repl_entry::get_packed_size (serializator, entry_size);
    entry_size += m_rec_des.get_packed_size (serializator, entry_size);

    return entry_size;
  }

  bool
  rec_des_row_repl_entry::is_equal (const packable_object *other)
  {
    bool check = single_row_repl_entry::is_equal (other);
    const rec_des_row_repl_entry *other_t = NULL;

    if (!check)
      {
	return false;
      }

    other_t = dynamic_cast<const rec_des_row_repl_entry *> (other);

    if (other_t == NULL || m_type != other_t->m_type)
      {
	return false;
      }

    if (m_type == cubreplication::repl_entry_type::REPL_DELETE)
      {
	return true;
      }

    if (m_rec_des.get_size () != other_t->m_rec_des.get_size ()
	|| m_rec_des.get_recdes().type != other_t->m_rec_des.get_recdes ().type
	|| std::memcmp (m_rec_des.get_recdes ().data, other_t->m_rec_des.get_recdes().data, m_rec_des.get_size ()) != 0)
      {
	return false;
      }

    return true;
  }

  rec_des_row_repl_entry::rec_des_row_repl_entry (repl_entry_type type, const char *class_name, const RECDES &rec_des,
      LOG_LSA &lsa_stamp)
    : single_row_repl_entry (type, class_name, lsa_stamp)
    , m_rec_des (rec_des, cubmem::STANDARD_BLOCK_ALLOCATOR)
  {
  }

  rec_des_row_repl_entry::~rec_des_row_repl_entry ()
  {
  }

  void
  rec_des_row_repl_entry::stringify (string_buffer &str)
  {
    str ("rec_des_row_repl_entry::");
    single_row_repl_entry::stringify (str);
    str ("recdes length=%zu\n", m_rec_des.get_size ());
  }


  row_object::row_object (const char *class_name)
  {
    m_class_name = class_name;
    m_data_size = 0;
  }

  row_object::~row_object ()
  {
  }

  void row_object::reset (void)
  {
    m_data_size = 0;

    m_rec_des_list.clear ();
  }


  int row_object::apply (void)
  {
    /* TODO[replication] */
    return NO_ERROR;
  }

  void row_object::pack (cubpacking::packer &serializator) const
  {
    serializator.pack_int (row_object::PACKING_ID);
    serializator.pack_string (m_class_name);
    serializator.pack_int ((int) m_rec_des_list.size ());

    for (const record_descriptor &rec : m_rec_des_list)
      {
	rec.pack (serializator);
      }
  }

  void row_object::unpack (cubpacking::unpacker &deserializator)
  {
    int entry_type_not_used;

    deserializator.unpack_int (entry_type_not_used);

    deserializator.unpack_string (m_class_name);
    int rec_des_cnt = 0;

    deserializator.unpack_int (rec_des_cnt);

    for (int i = 0; i < rec_des_cnt; i++)
      {
	m_rec_des_list.emplace_back ();
	record_descriptor &rec = m_rec_des_list.back ();
	rec.unpack (deserializator);
      }
  }

  std::size_t row_object::get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const
  {
    std::size_t entry_size = start_offset;

    entry_size += serializator.get_packed_int_size (0);

    entry_size += serializator.get_packed_string_size (m_class_name, entry_size);
    entry_size += serializator.get_packed_int_size (entry_size);
    for (const record_descriptor &rec : m_rec_des_list)
      {
	entry_size += rec.get_packed_size (serializator, entry_size);
      }

    return entry_size;
  }


  bool row_object::is_equal (const cubpacking::packable_object *other)
  {
    const row_object *other_t = dynamic_cast<const row_object *> (other);

    if (other_t == NULL)
      {
	return false;
      }

    if (m_class_name.compare (other_t->m_class_name) != 0)
      {
	return false;
      }

    if (m_rec_des_list.size () != other_t->m_rec_des_list.size ())
      {
	return false;
      }

    for (int i = 0; i < m_rec_des_list.size (); i++)
      {
	if (m_rec_des_list[i].get_size () != other_t->m_rec_des_list[i].get_size ())
	  {
	    return false;
	  }

	if (m_rec_des_list[i].get_recdes ().type != other_t->m_rec_des_list[i].get_recdes ().type)
	  {
	    return false;
	  }

	if (std::memcmp (m_rec_des_list[i].get_recdes ().data, other_t->m_rec_des_list[i].get_recdes().data,
			 m_rec_des_list[i].get_size ()) != 0)
	  {
	    return false;
	  }
      }
    return true;
  }

  void row_object::stringify (string_buffer &str)
  {
    str ("row_object::row_object table=%s records_cnt:%d\n", m_class_name.c_str (), m_rec_des_list.size ());
    for (int i = 0; i < m_rec_des_list.size (); i++)
      {
	size_t buf_size = m_rec_des_list[i].get_size ();

	str ("\trecord:%d, size:%d\n", i, buf_size);
	buf_size = std::min (buf_size, (size_t) 256);
	str.hex_dump (m_rec_des_list[i].get_recdes ().data, buf_size);
      }
  }

  void row_object::move_record (record_descriptor &&record)
  {
    size_t rec_size = record.get_recdes ().length;

    m_rec_des_list.push_back (std::move (record));
    m_data_size += rec_size;
  }

} /* namespace cubreplication */
