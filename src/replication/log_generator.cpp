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
 * log_generator.cpp
 */

#include "log_generator.hpp"

#include "error_manager.h"
#include "heap_file.h"
#include "log_impl.h"
#include "multi_thread_stream.hpp"
#include "replication.h"
#include "replication_common.hpp"
#include "replication_stream_entry.hpp"
#include "string_buffer.hpp"
#include "system_parameter.h"
#include "thread_manager.hpp"

namespace cubreplication
{
  static bool enable_log_generator_logging = true;

  log_generator::~log_generator ()
  {
    m_stream_entry.destroy_objects ();
  }

  void
  log_generator::add_statement (repl_info_sbr &stmt_info)
  {
    LOG_LSA *p_lsa;
    if (is_replication_disabled ())
      {
	return;
      }

    cubthread::entry *thread_p = &cubthread::get_entry ();
    p_lsa = logtb_find_current_tran_lsa (thread_p);
    assert (p_lsa != NULL);

    sbr_repl_entry *repl_obj =
	    new sbr_repl_entry (stmt_info.stmt_text, stmt_info.db_user, stmt_info.db_password,
				stmt_info.sys_prm_context, *p_lsa);
    append_repl_object (*repl_obj);
  }

  void
  log_generator::add_delete_row (const DB_VALUE &key, const OID &class_oid)
  {
    if (is_row_replication_disabled ())
      {
	return;
      }

    LOG_LSA *p_lsa;
    /* Currently we set last lsa. Later we can adapt it to match the corresponding stream position. */
    char *classname = get_classname (class_oid);
    cubthread::entry *thread_p = &cubthread::get_entry ();
    p_lsa = logtb_find_current_tran_lsa (thread_p);
    assert (p_lsa != NULL);

    single_row_repl_entry *repl_obj = new single_row_repl_entry (REPL_DELETE, classname, *p_lsa);
    repl_obj->set_key_value (key);
    append_repl_object (*repl_obj);

    free (classname);
  }

  void
  log_generator::add_insert_row (const DB_VALUE &key, const OID &class_oid, const RECDES &record)
  {
    if (is_row_replication_disabled ())
      {
	return;
      }

    LOG_LSA *p_lsa;
    char *classname = get_classname (class_oid);
    cubthread::entry *thread_p = &cubthread::get_entry ();
    p_lsa = logtb_find_current_tran_lsa (thread_p);
    assert (p_lsa != NULL);

    rec_des_row_repl_entry *repl_obj = new rec_des_row_repl_entry (REPL_INSERT, classname, record, *p_lsa);
    repl_obj->set_key_value (key);
    append_repl_object (*repl_obj);

    free (classname);
  }

  void
  log_generator::append_repl_object (replication_object &object)
  {
    m_stream_entry.add_packable_entry (&object);

    er_log_repl_obj (&object, "log_generator::append_repl_object");
  }

  /* in case inst_oid is not found, create a new entry and append it to pending,
   * else, add value and col_id to it
   * later, when setting key_dbvalue to it, move it to m_stream_entry
   */
  void
  log_generator::add_attribute_change (const OID &class_oid, const OID &inst_oid, ATTR_ID col_id,
				       const DB_VALUE &value)
  {
    if (is_row_replication_disabled ())
      {
	return;
      }

    changed_attrs_row_repl_entry *entry = NULL;
    for (auto &repl_obj : m_pending_to_be_added)
      {
	if (repl_obj->compare_inst_oid (inst_oid))
	  {
	    entry = repl_obj;
	    break;
	  }
      }

    if (entry != NULL)
      {
	entry->copy_and_add_changed_value (col_id, value);
      }
    else
      {
	LOG_LSA *p_lsa;
	char *class_name = get_classname (class_oid);

	cubthread::entry *thread_p = &cubthread::get_entry ();
	p_lsa = logtb_find_current_tran_lsa (thread_p);
	assert (p_lsa != NULL);
	entry = new changed_attrs_row_repl_entry (cubreplication::repl_entry_type::REPL_UPDATE, class_name, inst_oid, *p_lsa);
	entry->copy_and_add_changed_value (col_id, value);

	m_pending_to_be_added.push_back (entry);

	free (class_name);
      }

    er_log_repl_obj (entry, "log_generator::add_attribute_change");
  }

  void
  log_generator::remove_attribute_change (const OID &class_oid, const OID &inst_oid)
  {
    if (is_row_replication_disabled ())
      {
	return;
      }

    for (auto repl_obj = m_pending_to_be_added.begin (); repl_obj != m_pending_to_be_added.end (); ++repl_obj)
      {
	if ((*repl_obj)->compare_inst_oid (inst_oid))
	  {
	    changed_attrs_row_repl_entry *entry = *repl_obj;
	    (void) m_pending_to_be_added.erase (repl_obj--);
	    delete entry;
	    break;
	  }
      }
  }

  /* first fetch the class name, then set key */
  void
  log_generator::add_update_row (const DB_VALUE &key, const OID &inst_oid, const OID &class_oid,
				 const RECDES *optional_recdes)
  {
    if (is_row_replication_disabled ())
      {
	return;
      }

    char *class_name = get_classname (class_oid);
    bool found = false;
    LOG_LSA *p_lsa;
    cubthread::entry *thread_p = &cubthread::get_entry ();

    p_lsa = logtb_find_current_tran_lsa (thread_p);
    assert (p_lsa != NULL);

    for (auto repl_obj_it = m_pending_to_be_added.begin (); repl_obj_it != m_pending_to_be_added.end (); ++repl_obj_it)
      {
	changed_attrs_row_repl_entry *repl_obj = *repl_obj_it;
	if (repl_obj->compare_inst_oid (inst_oid))
	  {
	    repl_obj->set_key_value (key);

	    /* Set the current transaction lsa. It may be rewritten later. */
	    repl_obj->set_lsa_stamp (*p_lsa);

	    append_repl_object (*repl_obj);
	    er_log_repl_obj (repl_obj, "log_generator::set_key_to_repl_object");

	    // remove
	    (void) m_pending_to_be_added.erase (repl_obj_it--);

	    found = true;

	    break;
	  }
      }

    if (!found)
      {
	assert (optional_recdes != NULL);

	cubreplication::rec_des_row_repl_entry *entry =
		new cubreplication::rec_des_row_repl_entry (cubreplication::repl_entry_type::REPL_UPDATE, class_name,
		    *optional_recdes, *p_lsa);

	entry->set_key_value (key);

	append_repl_object (*entry);

	er_log_repl_obj (entry, "log_generator::set_key_to_repl_object");
      }

    free (class_name);
  }

  void
  log_generator::update_lsastamp_for_changed_repl_object (const OID &inst_oid)
  {
    if (is_row_replication_disabled ())
      {
	return;
      }

    replication_object *repl_obj;
    cubthread::entry *thread_p = &cubthread::get_entry ();
    bool found = false;
    int count_entries = (int) m_stream_entry.count_entries ();
    LOG_LSA *p_lsa = logtb_find_current_tran_lsa (thread_p);
    assert (p_lsa != NULL);

    for (int i = count_entries - 1; i >= 0; i--)
      {
	repl_obj = m_stream_entry.get_object_at (i);
	if (repl_obj->is_instance_changing_attr (inst_oid))
	  {
	    repl_obj->set_lsa_stamp (*p_lsa);
	    break;
	  }
      }
  }

  char *
  log_generator::get_classname (const OID &class_oid)
  {
    char *classname = NULL;
    cubthread::entry *thread_p = &cubthread::get_entry ();
    bool save = logtb_set_check_interrupt (thread_p, false);
    if (heap_get_class_name (thread_p, &class_oid, &classname) != NO_ERROR || classname == NULL)
      {
	assert (false);
      }
    (void)logtb_set_check_interrupt (thread_p, save);
    return classname;
  }

  /* in case of error, abort all pending replication objects */
  void
  log_generator::abort_pending_repl_objects (void)
  {
    for (changed_attrs_row_repl_entry *entry : m_pending_to_be_added)
      {
	delete entry;
      }
    m_pending_to_be_added.clear ();
  }

  stream_entry *
  log_generator::get_stream_entry (void)
  {
    return &m_stream_entry;
  }

  void
  log_generator::pack_stream_entry (void)
  {
    assert (m_has_stream);
    assert (!m_stream_entry.is_tran_state_undefined ());
    assert (MVCCID_IS_VALID (m_stream_entry.get_mvccid ()));

    if (prm_get_bool_value (PRM_ID_DEBUG_REPLICATION_DATA))
      {
	string_buffer sb;
	m_stream_entry.stringify (sb, stream_entry::detailed_dump);
	_er_log_debug (ARG_FILE_LINE, "log_generator::pack_stream_entry\n%s\n", sb.get_buffer ());
      }

    m_stream_entry.pack ();
    m_stream_entry.reset ();
  }

  void
  log_generator::pack_group_commit_entry (const tx_group &group,
					  cubstream::stream_position &stream_start_pos,
					  cubstream::stream_position &stream_end_pos)
  {
    /* use non-NULL MVCCID to prevent assertion fail on stream packer */
    static stream_entry gc_stream_entry (s_stream, MVCCID_FIRST, stream_entry_header::GROUP_COMMIT);

    repl_gc_info *gc_entry = new repl_gc_info (group);
    gc_stream_entry.add_packable_entry (gc_entry);
    gc_stream_entry.pack ();
    gc_stream_entry.reset ();

    stream_start_pos = gc_stream_entry.get_stream_entry_start_position ();
    stream_end_pos = gc_stream_entry.get_stream_entry_end_position ();
  }

  void
  log_generator::set_global_stream (cubstream::multi_thread_stream *stream)
  {
    for (int i = 0; i < log_Gl.trantable.num_total_indices; i++)
      {
	LOG_TDES *tdes = LOG_FIND_TDES (i);

	log_generator *lg = &tdes->replication_log_generator;

	lg->set_stream (stream);
      }
    log_generator::s_stream = stream;
  }

  void
  log_generator::er_log_repl_obj (replication_object *obj, const char *message)
  {
    string_buffer strb;

    if (!enable_log_generator_logging)
      {
	return;
      }

    obj->stringify (strb);

    _er_log_debug (ARG_FILE_LINE, "%s\n%s", message, strb.get_buffer ());
  }

  void
  log_generator::check_commit_end_tran (void)
  {
    /* check there are no pending replication objects */
    assert (m_pending_to_be_added.size () == 0);
  }

  void
  log_generator::on_transaction_finish ()
  {
    if (is_replication_disabled () || !MVCCID_IS_VALID (m_stream_entry.get_mvccid ()))
      {
	return;
      }

#if !defined (NDEBUG)
    if (prm_get_bool_value (PRM_ID_REPL_LOG_LOCAL_DEBUG))
      {
	/* Reset stream entry. */
	m_stream_entry.reset();
	return;
      }
#endif

    pack_stream_entry ();
  }

  void
  log_generator::on_transaction_commit (void)
  {
    assert (m_pending_to_be_added.size () == 0);
    on_transaction_finish ();
  }

  void log_generator::on_sysop_commit (LOG_LSA &start_lsa)
  {
    if (m_stream_entry.count_entries () == 0)
      {
	return;
      }

    cubreplication::stream_entry local_stream_entry (m_stream_entry.get_stream ());

    m_stream_entry.move_replication_objects_after_lsa_to_stream (start_lsa, local_stream_entry);

#if !defined (NDEBUG)
    if (prm_get_bool_value (PRM_ID_REPL_LOG_LOCAL_DEBUG))
      {
	/* Reset stream entry. */
	local_stream_entry.reset ();
	return;
      }
#endif

    if (local_stream_entry.count_entries () == 0)
      {
	return;
      }

    local_stream_entry.set_mvccid (m_stream_entry.get_mvccid ());
    local_stream_entry.set_state (stream_entry_header::ACTIVE);

    assert (MVCCID_IS_VALID (local_stream_entry.get_mvccid ()));

    /* Write objects in stream and then destroy them. */
    local_stream_entry.pack ();
    local_stream_entry.reset ();
  }

  void
  log_generator::on_transaction_abort (void)
  {
    on_transaction_finish ();
  }

  void
  log_generator::on_sysop_abort (LOG_LSA &start_lsa)
  {
    m_stream_entry.destroy_objects_after_lsa (start_lsa);
  }

  void
  log_generator::clear_transaction (void)
  {
    if (is_replication_disabled ())
      {
	return;
      }

    m_is_row_replication_disabled = false;
  }

  bool
  log_generator::is_replication_disabled ()
  {
#if defined (SERVER_MODE)
    return !log_does_allow_replication ();
#else
    return true;
#endif
  }

  bool
  log_generator::is_row_replication_disabled ()
  {
    return is_replication_disabled () || m_is_row_replication_disabled;
  }

  void
  log_generator::set_row_replication_disabled (bool disable_if_true)
  {
    m_is_row_replication_disabled = disable_if_true;
  }

  void
  log_generator::apply_tran_mvccid (void)
  {
    cubthread::entry *thread_p = &cubthread::get_entry ();
    int tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
    LOG_TDES *tdes = LOG_FIND_TDES (tran_index);

    m_stream_entry.set_mvccid (tdes->mvccinfo.id);
    m_stream_entry.set_state (stream_entry_header::ACTIVE);
  }

#if !defined (NDEBUG) && defined (SERVER_MODE)
  int
  log_generator::abort_sysop_and_simulate_apply_repl_rbr_on_master (LOG_LSA &filter_replication_lsa)
  {
    /* This function is used for debug purpose only. */
    int err_code = NO_ERROR;
    replication_object *repl_obj;
    cubthread::entry *thread_p = &cubthread::get_entry ();
    LOG_TDES *tdes = logtb_get_tdes (&cubthread::get_entry ());
    cubreplication::stream_entry *stream_entry = tdes->replication_log_generator.get_stream_entry ();

    assert (stream_entry->count_entries () > 0);

    /* Save the replication objects, before abort. */
    cubstream::multi_thread_stream *p_multi_thread_stream = get_stream ();
    cubreplication::stream_entry local_stream_entry (p_multi_thread_stream);

    m_stream_entry.move_replication_objects_after_lsa_to_stream (filter_replication_lsa, local_stream_entry);

    if (local_stream_entry.count_entries () == 0)
      {
	log_sysop_attach_to_outer (thread_p);
	return NO_ERROR;
      }

    /* First abort the operation. */
    log_sysop_abort (thread_p);

    /* Disable row replication, while we apply. */
    logtb_get_tdes (thread_p)->replication_log_generator.set_row_replication_disabled (true);

    /* Simulate it again with apply . */
    for (unsigned int i = 0; i < local_stream_entry.count_entries (); i++)
      {
	repl_obj = local_stream_entry.get_object_at (i);
	if (repl_obj != NULL)
	  {
	    /* Debug code, check for SBR, quick fix. */
	    if ((dynamic_cast<sbr_repl_entry *> (repl_obj)) != NULL)
	      {
		continue;
	      }

	    err_code = repl_obj->apply ();
	    if (err_code != NO_ERROR)
	      {
		break;
	      }
	  }
      }

    logtb_get_tdes (thread_p)->replication_log_generator.set_row_replication_disabled (false);

    return err_code;
  }
#endif

#if !defined (NDEBUG) && defined (SERVER_MODE)
  int
  log_generator::abort_partial_and_simulate_apply_sbr_repl_on_master (const char *savepoint_name)
  {
    /* This function is used for debug purpose only. */
    int err_code = NO_ERROR;
    replication_object *repl_obj;
    cubthread::entry *thread_p = &cubthread::get_entry ();
    LOG_TDES *tdes = logtb_get_tdes (&cubthread::get_entry ());
    cubreplication::stream_entry *stream_entry = tdes->replication_log_generator.get_stream_entry ();
    LOG_LSA filter_replication_lsa, savept_lsa;

    assert (stream_entry->count_entries () > 0);

    /* Currently we are testing is only one entry. */
    if (m_stream_entry.count_entries () != 1)
      {
	return NO_ERROR;
      }

    /* Save the replication objects, before abort. */
    cubstream::multi_thread_stream *p_multi_thread_stream = get_stream ();
    cubreplication::stream_entry local_stream_entry (p_multi_thread_stream);

    LSA_SET_NULL (&filter_replication_lsa);
    m_stream_entry.move_replication_objects_after_lsa_to_stream (filter_replication_lsa, local_stream_entry);

    log_abort_partial (thread_p, savepoint_name, &savept_lsa);

    /* Simulate it again with apply. */
    assert (local_stream_entry.count_entries () == 1);

    repl_obj = local_stream_entry.get_object_at (0);
    if (repl_obj != NULL)
      {
	err_code = repl_obj->apply ();
      }

    return err_code;
  }
#endif

  cubstream::multi_thread_stream *log_generator::s_stream = NULL;
} /* namespace cubreplication */
