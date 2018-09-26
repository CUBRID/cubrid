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

#ident "$Id$"

#include "log_generator.hpp"
#include "replication_stream_entry.hpp"
#include "multi_thread_stream.hpp"
#include "log_impl.h"
#include "heap_file.h"
#include "system_parameter.h"
#include "error_manager.h"
#include "string_buffer.hpp"
#include "replication.h"

namespace cubreplication
{
  bool enable_log_generator_logging = true;

  log_generator::~log_generator ()
  {
    m_stream_entry.destroy_objects ();
  }

  int log_generator::start_tran_repl (MVCCID mvccid)
  {
    assert (m_is_initialized);
    m_stream_entry.set_mvccid (mvccid);

    return NO_ERROR;
  }

  int log_generator::set_repl_state (stream_entry_header::TRAN_STATE state)
  {
    m_stream_entry.set_state (state);

    return NO_ERROR;
  }

  int log_generator::append_repl_object (replication_object *object)
  {
    m_stream_entry.add_packable_entry (object);

    er_log_repl_obj (object, "log_generator::append_repl_object");

    return NO_ERROR;
  }

  /* in case inst_oid is not found, create a new entry and append it to pending,
   * else, add value and col_id to it
   * later, when setting key_dbvalue to it, move it to m_stream_entry
   */
  int log_generator::append_pending_repl_object (cubthread::entry &thread_entry, const OID *class_oid,
      const OID *inst_oid, ATTR_ID col_id, DB_VALUE *value)
  {
    changed_attrs_row_repl_entry *entry = NULL;
    char *class_name = NULL;

    if (inst_oid == NULL)
      {
	return NO_ERROR;
      }

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
	int error_code = NO_ERROR;

	if (heap_get_class_name (&thread_entry, class_oid, &class_name) != NO_ERROR || class_name == NULL)
	  {
	    assert (false);
	    return ER_FAILED;
	  }

	entry = new changed_attrs_row_repl_entry (cubreplication::repl_entry_type::REPL_UPDATE, class_name, inst_oid);
	entry->copy_and_add_changed_value (col_id, value);

	m_pending_to_be_added.push_back (entry);

	// FIXME - free class_name
      }

    er_log_repl_obj (entry, "log_generator::append_pending_repl_object");

    return NO_ERROR;
  }

  /* set key_dbvalue to inst_oid associated entry and, if not found,
   * it means we are in a special case where update uses recdes, instead
   * of changed db values
   */
  int log_generator::set_key_to_repl_object (DB_VALUE *key, const OID *inst_oid,
      char *class_name, RECDES *optional_recdes)
  {
    bool found = false;

    assert (inst_oid != NULL);

    for (auto repl_obj_it = m_pending_to_be_added.begin ();
	 repl_obj_it != m_pending_to_be_added.end (); ++repl_obj_it)
      {
	if ((*repl_obj_it)->compare_inst_oid (inst_oid))
	  {
	    (*repl_obj_it)->set_key_value (key);

	    (void) log_generator::append_repl_object (*repl_obj_it);
	    er_log_repl_obj (*repl_obj_it, "log_generator::set_key_to_repl_object");

	    repl_obj_it = m_pending_to_be_added.erase (repl_obj_it);

	    found = true;

	    break;
	  }
      }

    if (!found)
      {
	if (optional_recdes == NULL)
	  {
	    assert (false);
	    return ER_FAILED;
	  }

	cubreplication::rec_des_row_repl_entry *entry = new cubreplication::rec_des_row_repl_entry (
		cubreplication::repl_entry_type::REPL_UPDATE,
		class_name,
		optional_recdes);

	(void) log_generator::append_repl_object (entry);

	er_log_repl_obj (entry, "log_generator::set_key_to_repl_object");
      }

    return NO_ERROR;
  }

  /* first fetch the class name, then set key */
  int log_generator::set_key_to_repl_object (DB_VALUE *key, const OID *inst_oid,
      const OID *class_oid, RECDES *optional_recdes)
  {
    char *class_name;
    int rc;

    if (heap_get_class_name (NULL, class_oid, &class_name) != NO_ERROR || class_name == NULL)
      {
	assert (false);
	return ER_FAILED;
      }

    rc = set_key_to_repl_object (key, inst_oid, class_name, optional_recdes);
    free (class_name);

    if (rc != NO_ERROR)
      {
	assert (false);
	return rc;
      }

    return NO_ERROR;
  }

  /* in case of error, abort all pending repl objects */
  void log_generator::abort_pending_repl_objects ()
  {
    for (changed_attrs_row_repl_entry *entry : m_pending_to_be_added)
      {
	delete entry;
      }
    m_pending_to_be_added.clear ();
  }

  stream_entry *log_generator::get_stream_entry (void)
  {
    return &m_stream_entry;
  }

  int log_generator::pack_stream_entry (void)
  {
    if (m_is_initialized)
      {
	m_stream_entry.pack ();
	m_stream_entry.reset ();
	m_stream_entry.set_state (stream_entry_header::ACTIVE);
      }

    return NO_ERROR;
  }

  void log_generator::pack_group_commit_entry (void)
  {
    static stream_entry gc_stream_entry (g_stream, MVCCID_NULL, stream_entry_header::GROUP_COMMIT);
    gc_stream_entry.pack ();
  }

  void log_generator::set_global_stream (cubstream::multi_thread_stream *stream)
  {
    for (int i = 0; i < log_Gl.trantable.num_total_indices; i++)
      {
	LOG_TDES *tdes = LOG_FIND_TDES (i);

	log_generator *lg = & (tdes->replication_log_generator);

	lg->set_stream (stream);
      }
    log_generator::g_stream = stream;
  }

  void log_generator::er_log_repl_obj (replication_object *obj, const char *message)
  {
    string_buffer strb;

    if (!enable_log_generator_logging)
      {
	return;
      }

    obj->stringify (strb);

    _er_log_debug (ARG_FILE_LINE, "%s\n%s", message, strb.get_buffer ());
  }

  void log_generator::check_commit_end_tran (void)
  {
#if !defined(NDEBUG)
    /* check there are no pending replication objects */
    assert (m_pending_to_be_added.size () == 0);
#endif
  }

  cubstream::multi_thread_stream *log_generator::g_stream = NULL;

  int
  repl_log_insert_with_recdes (THREAD_ENTRY *thread_p, const char *class_name,
			       cubreplication::repl_entry_type rbr_type, DB_VALUE *key_dbvalue, RECDES *recdes)
  {
    int tran_index;
    LOG_TDES *tdes;
    int error = NO_ERROR;

    tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
    tdes = LOG_FIND_TDES (tran_index);
    if (tdes == NULL)
      {
	return ER_FAILED;
      }

    /* If suppress_replication flag is set, do not write replication log. */
    if (tdes->suppress_replication != 0)
      {
	return NO_ERROR;
      }

    cubreplication::single_row_repl_entry *new_rbr;

    char *ptr_to_packed_key_value_size = NULL;
    int packed_key_len = 0;

    new_rbr = new cubreplication::rec_des_row_repl_entry (rbr_type, class_name, recdes);
    new_rbr->set_key_value (key_dbvalue);

    tdes->replication_log_generator.append_repl_object (new_rbr);
    return error;
  }

  int
  repl_log_insert_statement (THREAD_ENTRY *thread_p, repl_info_sbr *repl_info)
  {
    int tran_index;
    LOG_TDES *tdes;
    int error = NO_ERROR;

    tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
    tdes = LOG_FIND_TDES (tran_index);
    if (tdes == NULL)
      {
	return ER_FAILED;
      }

    /* If suppress_replication flag is set, do not write replication log. */
    if (tdes->suppress_replication != 0)
      {
	return NO_ERROR;
      }

    cubreplication::sbr_repl_entry *new_sbr =
	    new cubreplication::sbr_repl_entry (repl_info->stmt_text, repl_info->db_user, repl_info->sys_prm_context);

    tdes->replication_log_generator.append_repl_object (new_sbr);

    er_log_debug (ARG_FILE_LINE,
		  "repl_log_insert_statement: repl_info_sbr { type %d, name %s, stmt_txt %s, user %s, "
		  "sys_prm_context %s }\n", repl_info->statement_type, repl_info->name, repl_info->stmt_text,
		  repl_info->db_user, repl_info->sys_prm_context);

    return error;
  }
} /* namespace cubreplication */
