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
#include "replication_stream_entry.hpp"
#include "string_buffer.hpp"
#include "system_parameter.h"
#include "thread_manager.hpp"

namespace cubreplication
{
  static bool enable_log_generator_logging = true;

  log_generator::~log_generator()
  {
    LOG_LSA start_lsa;
    LSA_SET_NULL (&start_lsa);
    m_stream_entry.destroy_objects_after_lsa (start_lsa);
  }

  void log_generator::set_tran_repl_info(MVCCID mvccid, stream_entry_header::TRAN_STATE state)
  {    
    assert(m_has_stream);
    m_stream_entry.set_mvccid(mvccid);
    m_stream_entry.set_state(state);
  }  

  void
    log_generator::add_statement(repl_info_sbr &stmt_info)
  {
    LOG_LSA *p_lsa;
    if (is_replication_disabled())
    {
      return;
    }

    cubthread::entry *thread_p = &cubthread::get_entry();
    p_lsa = logtb_find_current_tran_lsa(thread_p);
    assert(p_lsa != NULL);
    
    sbr_repl_entry *repl_obj = new sbr_repl_entry (stmt_info.stmt_text, stmt_info.db_user, stmt_info.sys_prm_context, *p_lsa);
    append_repl_object (*repl_obj);
  }

  void
    log_generator::add_delete_row(const DB_VALUE &key, const OID &class_oid)
  {    
    if (is_row_replication_disabled())
    {
      return;
    }

    LOG_LSA *p_lsa;
    /* Currently we set last lsa. Later we can adapt it to match the corresponding stream position. */
    char *classname = get_classname(class_oid);
    cubthread::entry *thread_p = &cubthread::get_entry();
    p_lsa = logtb_find_current_tran_lsa (thread_p);
    assert (p_lsa != NULL);
    
    single_row_repl_entry *repl_obj = new single_row_repl_entry(REPL_DELETE, classname, *p_lsa);
    repl_obj->set_key_value(key);
    append_repl_object(*repl_obj);

    free(classname);
  }

  void
    log_generator::add_insert_row(const DB_VALUE &key, const OID &class_oid, const RECDES &record)
  {    
    if (is_row_replication_disabled())
    {
      return;
    }

    LOG_LSA *p_lsa;
    char *classname = get_classname(class_oid);
    cubthread::entry *thread_p = &cubthread::get_entry();
    p_lsa = logtb_find_current_tran_lsa(thread_p);
    assert (p_lsa != NULL);

    rec_des_row_repl_entry *repl_obj = new rec_des_row_repl_entry(REPL_INSERT, classname, record, *p_lsa);
    repl_obj->set_key_value(key);
    append_repl_object(*repl_obj);

    free(classname);
  }

  void
    log_generator::append_repl_object(replication_object &object)
  {    
    m_stream_entry.add_packable_entry (&object);

    er_log_repl_obj(&object, "log_generator::append_repl_object");
  }

  /* in case inst_oid is not found, create a new entry and append it to pending,
   * else, add value and col_id to it
   * later, when setting key_dbvalue to it, move it to m_stream_entry
   */
  void
    log_generator::add_attribute_change(const OID &class_oid, const OID &inst_oid, ATTR_ID col_id,
      const DB_VALUE &value)
  {    
    if (is_row_replication_disabled())
    {
      return;
    }

    changed_attrs_row_repl_entry *entry = NULL;
    char *class_name = NULL;

    for (auto &repl_obj : m_pending_to_be_added)
    {
      if (repl_obj->compare_inst_oid(inst_oid))
      {
	entry = repl_obj;
	break;
      }
    }

    if (entry != NULL)
    {
      entry->copy_and_add_changed_value(col_id, value);
    }
    else
    {
      LOG_LSA *p_lsa;
      int error_code = NO_ERROR;

      char *class_name = get_classname(class_oid);

      cubthread::entry *thread_p = &cubthread::get_entry();
      p_lsa = logtb_find_current_tran_lsa(thread_p);
      assert(p_lsa != NULL);
      entry = new changed_attrs_row_repl_entry(cubreplication::repl_entry_type::REPL_UPDATE, class_name, inst_oid, *p_lsa);
      entry->copy_and_add_changed_value(col_id, value);

      m_pending_to_be_added.push_back(entry);

      free(class_name);
    }

    er_log_repl_obj(entry, "log_generator::add_attribute_change");
  }


  void log_generator::remove_attribute_change(const OID &class_oid, const OID &inst_oid)
  {
    if (is_row_replication_disabled())
    {
      return;
    }

    for (auto repl_obj = m_pending_to_be_added.begin(); repl_obj != m_pending_to_be_added.end(); ++repl_obj)
    {
      if ((*repl_obj)->compare_inst_oid(inst_oid))
      {
	(void)m_pending_to_be_added.erase(repl_obj);
	break;
      }
    }
  }

  /* first fetch the class name, then set key */
  void
    log_generator::add_update_row(const DB_VALUE &key, const OID &inst_oid, const OID &class_oid,
      const RECDES *optional_recdes)
  {
    if (is_row_replication_disabled())
    {
      return;
    }

    char *class_name = get_classname(class_oid);
    bool found = false;
    LOG_LSA *p_lsa;
    cubthread::entry *thread_p = &cubthread::get_entry();

    p_lsa = logtb_find_current_tran_lsa(thread_p);
    assert (p_lsa != NULL);

    for (auto repl_obj_it = m_pending_to_be_added.begin(); repl_obj_it != m_pending_to_be_added.end(); ++repl_obj_it)
    {
      changed_attrs_row_repl_entry *repl_obj = *repl_obj_it;
      if (repl_obj->compare_inst_oid(inst_oid))
      {
	repl_obj->set_key_value(key);        

        /* Set the current transaction lsa. It may be rewritten later. */
        repl_obj->set_lsa_stamp (*p_lsa);

	append_repl_object(*repl_obj);
	er_log_repl_obj(repl_obj, "log_generator::set_key_to_repl_object");

	// remove
	(void)m_pending_to_be_added.erase(repl_obj_it);

	found = true;

	break;
      }
    }

    if (!found)
    {
      assert(optional_recdes != NULL);
            
      cubreplication::rec_des_row_repl_entry *entry =
	new cubreplication::rec_des_row_repl_entry(cubreplication::repl_entry_type::REPL_UPDATE, class_name,
	  *optional_recdes, *p_lsa);

      entry->set_key_value(key);

      append_repl_object(*entry);

      er_log_repl_obj(entry, "log_generator::set_key_to_repl_object");
    }

    free(class_name);
  }

  void
    log_generator::update_lsastamp_for_changed_repl_object (const OID &inst_oid)
  {
    if (is_row_replication_disabled())
    {
      return;
    }
    
    replication_object *repl_obj;
    changed_attrs_row_repl_entry *changed_repl_entry;
    cubthread::entry *thread_p = &cubthread::get_entry();
    bool found = false;
    int count_entries = (int) m_stream_entry.count_entries ();            
    LOG_LSA *p_lsa = logtb_find_current_tran_lsa(thread_p);
    assert (p_lsa != NULL);

    for (int i = count_entries - 1; i >= 0; i--)
    {
      repl_obj = m_stream_entry.get_object_at(i);
      changed_repl_entry = dynamic_cast<changed_attrs_row_repl_entry *> (repl_obj);
      if (changed_repl_entry == NULL)
      {
        continue;
      }
            
      if (changed_repl_entry->compare_inst_oid (inst_oid))
      {        
        changed_repl_entry->set_lsa_stamp (*p_lsa);
        break;
      }
    }
  }

  char *
    log_generator::get_classname(const OID &class_oid)
  {
    char *classname = NULL;
    cubthread::entry *thread_p = &cubthread::get_entry();
    bool save = logtb_set_check_interrupt(thread_p, false);
    if (heap_get_class_name(thread_p, &class_oid, &classname) != NO_ERROR || classname == NULL)
    {
      assert(false);
    }
    (void)logtb_set_check_interrupt(thread_p, save);
    return classname;
  }

  /* in case of error, abort all pending replication objects */
  void
    log_generator::abort_pending_repl_objects(void)
  {
    for (changed_attrs_row_repl_entry *entry : m_pending_to_be_added)
    {
      delete entry;
    }
    m_pending_to_be_added.clear();
  }

  stream_entry *log_generator::get_stream_entry(void)
  {
    return &m_stream_entry;
  }

  void
    log_generator::pack_stream_entry(void)
  {
    assert(m_has_stream);
    
    m_stream_entry.pack();
    m_stream_entry.reset();
    // reset state
    m_stream_entry.set_state(stream_entry_header::ACTIVE);
  }

  void
    log_generator::pack_group_commit_entry(void)
  {
    static stream_entry gc_stream_entry(s_stream, MVCCID_NULL, stream_entry_header::GROUP_COMMIT);
    gc_stream_entry.pack();
  }

  void
    log_generator::set_global_stream(cubstream::multi_thread_stream *stream)
  {
    for (int i = 0; i < log_Gl.trantable.num_total_indices; i++)
    {
      LOG_TDES *tdes = LOG_FIND_TDES(i);

      log_generator *lg = &(tdes->replication_log_generator);

      lg->set_stream(stream);
    }
    log_generator::s_stream = stream;
  }

  void
    log_generator::er_log_repl_obj(replication_object *obj, const char *message)
  {
    string_buffer strb;

    if (!enable_log_generator_logging)
    {
      return;
    }

    obj->stringify(strb);

    _er_log_debug(ARG_FILE_LINE, "%s\n%s", message, strb.get_buffer());
  }

  void
    log_generator::check_commit_end_tran(void)
  {
    /* check there are no pending replication objects */
    assert(m_pending_to_be_added.size() == 0);
  }

  void
    log_generator::on_transaction_finish(stream_entry_header::TRAN_STATE state)
  {
    if (is_replication_disabled())
    {
      return;
    }

    cubthread::entry *thread_p = &cubthread::get_entry();
    int tran_index = LOG_FIND_THREAD_TRAN_INDEX(thread_p);
    LOG_TDES *tdes = LOG_FIND_TDES(tran_index);

#if !defined(NDEBUG)
    if (prm_get_bool_value (PRM_ID_REPL_LOG_LOCAL_DEBUG))
    {
      /* Reset stream entry. */      
      m_stream_entry.reset();      
      return;
    }    
#endif

    set_tran_repl_info (tdes->mvccinfo.id, state);
    pack_stream_entry ();
  }

  void
    log_generator::on_transaction_commit(void)
  {
    on_transaction_finish(stream_entry_header::TRAN_STATE::COMMITTED);
  }

  void log_generator::on_sysop_commit (LOG_LSA &start_lsa)
  {
    replication_object *repl_obj;
    LOG_LSA highest_repl_lsa_stamp;
    cubstream::multi_thread_stream *p_multi_thread_stream = get_stream();
    cubreplication::stream_entry local_stream_entry (p_multi_thread_stream);    
    
    if (m_stream_entry.count_entries() == 0)
      {
        return;
      }
    
    /* Get the highest lsa stamp. */
    repl_obj = m_stream_entry.get_object_at ((int) (m_stream_entry.count_entries() - 1));
    repl_obj->get_lsa_stamp (highest_repl_lsa_stamp);
    if (LSA_LE (&highest_repl_lsa_stamp, &start_lsa))
    {
      /* No object in current sysop. */
      return;
    }
    
    m_stream_entry.move_replication_objects_after_lsa_to_stream (start_lsa, local_stream_entry);
#if !defined(NDEBUG)
    if (prm_get_bool_value (PRM_ID_REPL_LOG_LOCAL_DEBUG))
    {
      /* Reset stream entry. */      
      local_stream_entry.reset();
      return;
    }
#endif

    cubthread::entry *thread_p = &cubthread::get_entry ();
    int tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
    LOG_TDES *tdes = LOG_FIND_TDES (tran_index);

    MVCCID mvccid = logtb_find_current_mvccid (&cubthread::get_entry ());
    set_tran_repl_info (mvccid, stream_entry_header::ACTIVE);

    /* Write objects in stream and then destroy them. */
    local_stream_entry.pack ();
    local_stream_entry.reset ();
  }  

  void
    log_generator::on_transaction_abort(void)
  {
    on_transaction_finish (stream_entry_header::TRAN_STATE::ABORTED);
  }

  void log_generator::on_sysop_abort (LOG_LSA &start_lsa)
  {
    cubthread::entry *thread_p = &cubthread::get_entry();
    cubreplication::stream_entry *stream_entry = logtb_get_tdes (thread_p)->replication_log_generator.get_stream_entry ();
    int count_entries = (int) stream_entry->count_entries ();

    if (count_entries == 0)
    {
      return;
    }

    stream_entry->destroy_objects_after_lsa (start_lsa);    
  }

  void
    log_generator::clear_transaction(void)
  {
    if (is_replication_disabled())
    {
      return;
    }

    m_is_row_replication_disabled = false;
  }

  bool
    log_generator::is_replication_disabled()
  {
#if defined (SERVER_MODE)
    return !log_does_allow_replication();
#else
    return true;
#endif
  }

  bool
    log_generator::is_row_replication_disabled()
  {
    return is_replication_disabled() || m_is_row_replication_disabled;
  }

  void
    log_generator::set_row_replication_disabled(bool disable_if_true)
  {
    m_is_row_replication_disabled = disable_if_true;
  }
  
#if !defined(NDEBUG) && defined (SERVER_MODE)
  int log_generator::abort_sysop_and_simulate_apply_repl_on_master (LOG_LSA &filter_replication_lsa)
  {
    /* This function is used for debug purpose only. */
    int err_code = NO_ERROR;
    replication_object *repl_obj;
    cubthread::entry *thread_p = &cubthread::get_entry();       
    LOG_TDES * tdes = logtb_get_tdes (&cubthread::get_entry());
    cubreplication::stream_entry *stream_entry = tdes->replication_log_generator.get_stream_entry ();

    assert(stream_entry->count_entries() > 0);
    
    /* Save the replication objects, before abort. */
    std::vector <cubreplication::replication_object *> repl_objects_after_lsa;
    stream_entry->move_replication_objects_after_lsa (filter_replication_lsa, repl_objects_after_lsa);

    /* First abort the operation. */
    log_sysop_abort (thread_p);          

    /* Disable row replication, while we apply. */
    logtb_get_tdes (thread_p)->replication_log_generator.set_row_replication_disabled (true);

    /* Simulate it again with apply . */
    for (unsigned int i = 0; i < repl_objects_after_lsa.size (); i++)
    {
      repl_obj = repl_objects_after_lsa[i];
      if (repl_obj != NULL)
      {
        err_code = repl_obj->apply ();
        if (err_code != NO_ERROR)
        {
          break;
        }
      }
    }  
    logtb_get_tdes(thread_p)->replication_log_generator.set_row_replication_disabled (false);

    /* Now, destroy the replication objects. */
    for (unsigned int i = 0; i < repl_objects_after_lsa.size(); i++)
    {
      if (repl_objects_after_lsa[i] != NULL)
      {
        delete (repl_objects_after_lsa[i]);
      }
    }
    repl_objects_after_lsa.clear ();

    return err_code;
  }
#endif

  cubstream::multi_thread_stream *log_generator::s_stream = NULL;
} /* namespace cubreplication */
