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
 * load_server_loader.cpp: Loader server definitions. Updated using design from fast loaddb prototype
 */

#include "load_server_loader.hpp"

#include "load_db_value_converter.hpp"
#include "load_scanner.hpp"
#include "locator_sr.h"
#include "object_primitive.h"
#include "xserver_interface.h"

namespace cubload
{
  server_loader::server_loader (session &session, error_handler &error_handler)
    : m_session (session)
    , m_error_handler (error_handler)
    , m_thread_ref (NULL)
    , m_attr_info ()
    , m_scancache ()
    , m_scancache_started (false)
    , m_clsid (NULL_CLASS_ID)
    , m_class_entry (NULL)
  {
    //
  }

  void
  server_loader::init (class_id clsid)
  {
    m_clsid = clsid;
    m_thread_ref = &cubthread::get_entry ();

    m_class_entry = m_session.get_class_registry ().get_class_entry (clsid);
    if (m_class_entry == NULL)
      {
	return;
      }

    start_scancache_modify (&m_scancache);
    if (!m_session.is_failed ())
      {
	m_scancache_started = true;
      }

    OID &class_oid =  m_class_entry->get_class_oid ();
    int error_code = heap_attrinfo_start (m_thread_ref, &class_oid, -1, NULL, &m_attr_info);
    if (error_code != NO_ERROR)
      {
	m_error_handler.on_failure_with_line (LOADDB_MSG_LOAD_FAIL);
      }
  }

  void
  server_loader::check_class (const char *class_name, int class_id)
  {
    (void) class_id;

    OID class_oid;
    locate_class (class_name, &class_oid);
  }

  int
  server_loader::setup_class (const char *class_name)
  {
    (void) class_name;

    register_class (class_name);
    register_class_attributes (NULL);

    return NO_ERROR;
  }

  void
  server_loader::setup_class (string_type *class_name, class_command_spec_type *cmd_spec)
  {
    assert (m_clsid != NULL_CLASS_ID);

    if (class_name == NULL || class_name->val == NULL)
      {
	return;
      }

    register_class (class_name->val);
    register_class_attributes (cmd_spec == NULL ? NULL : cmd_spec->attr_list);

    OID &class_oid = m_class_entry->get_class_oid ();

    // lock class when batch starts, it will be unlocked on transaction commit/abort, see load_parse_task::execute
    lock_object (m_thread_ref, &class_oid, oid_Root_class_oid, IX_LOCK, LK_UNCOND_LOCK);
  }

  void
  server_loader::destroy ()
  {
    heap_attrinfo_end (m_thread_ref, &m_attr_info);
    if (m_scancache_started)
      {
	heap_scancache_end_modify (m_thread_ref, &m_scancache);
	m_scancache_started = false;
      }

    m_clsid = NULL_CLASS_ID;
    m_class_entry = NULL;
    m_thread_ref = NULL;
  }

  void
  server_loader::start_line (int object_id)
  {
    (void) object_id;

    // clear db values
    heap_attrinfo_clear_dbvalues (&m_attr_info);
  }

  void
  server_loader::process_line (constant_type *cons)
  {
    if (m_session.is_failed ())
      {
	return;
      }

    int attr_idx = 0;

    for (constant_type *c = cons; c != NULL; c = c->next, attr_idx++)
      {
	attribute &attr = m_class_entry->get_attribute (attr_idx);
	process_constant (c, attr);
      }
  }

  void
  server_loader::process_constant (constant_type *cons, attribute &attr)
  {
    db_value db_val;

    if (attr.m_attr_repr == NULL)
      {
	// TODO use LOADDB_MSG_MISSING_DOMAIN message
	m_error_handler.on_failure_with_line (LOADDB_MSG_LOAD_FAIL);
	return;
      }

    switch (cons->type)
      {
      case LDR_NULL:
	if (attr.m_attr_repr->is_notnull)
	  {
	    m_error_handler.on_failure_with_line (LOADDB_MSG_LOAD_FAIL);
	    return;
	  }
      case LDR_INT:
      case LDR_FLOAT:
      case LDR_DOUBLE:
      case LDR_NUMERIC:
      case LDR_DATE:
      case LDR_TIME:
      case LDR_TIMESTAMP:
      case LDR_TIMESTAMPLTZ:
      case LDR_TIMESTAMPTZ:
      case LDR_DATETIME:
      case LDR_DATETIMELTZ:
      case LDR_DATETIMETZ:
      case LDR_STR:
      case LDR_NSTR:
      {
	string_type *str = (string_type *) cons->val;
	conv_func &func = get_conv_func ((data_type) cons->type, attr.m_attr_repr->domain->type->id);

	if (func != NULL)
	  {
	    func (str != NULL ? str->val : NULL, attr.m_attr_repr->domain, &db_val);
	  }
	else
	  {
	    m_error_handler.on_error_with_line (LOADDB_MSG_CONVERSION_ERROR, str != NULL ? str->val : "",
						pr_type_name (TP_DOMAIN_TYPE (attr.m_attr_repr->domain)));
	    return;
	  }

	free_string (&str);
      }
      break;
      case LDR_MONETARY:
	process_monetary_constant (cons, attr.m_attr_repr->domain, &db_val);
	break;
      case LDR_BSTR:
      case LDR_XSTR:
      case LDR_ELO_INT:
      case LDR_ELO_EXT:
      case LDR_SYS_USER:
      case LDR_SYS_CLASS:
      {
	string_type *str = (string_type *) cons->val;

	//conv_func func = get_conv_func (cons->type, attr->domain->type->id);
	//func (str->val, attr->domain, &db_val);

	free_string (&str);
      }
      break;

      case LDR_OID:
      case LDR_CLASS_OID:
	//ldr_process_object_ref ((object_ref_type *) cons->val, cons->type);
	break;

      case LDR_COLLECTION:
	// TODO CBRD-21654 add support for collections
	break;

      default:
	break;
      }

    if (cons->need_free)
      {
	free_and_init (cons);
      }

    OID &class_oid = m_class_entry->get_class_oid ();

    heap_attrinfo_set (&class_oid, attr.m_attr_id, &db_val, &m_attr_info);
  }

  void
  server_loader::process_monetary_constant (constant_type *cons, tp_domain *domain, db_value *db_val)
  {
    monetary_type *mon = (monetary_type *) cons->val;
    string_type *str = mon->amount;

    /* buffer size for monetary : numeric size + grammar currency symbol + string terminator */
    char full_mon_str[NUM_BUF_SIZE + 3 + 1];
    char *full_mon_str_p = full_mon_str;

    /* In Loader grammar always print symbol before value (position of currency symbol is not localized) */
    char *curr_str = intl_get_money_esc_ISO_symbol ((DB_CURRENCY) mon->currency_type);
    size_t full_mon_str_len = (strlen (str->val) + strlen (curr_str));

    if (full_mon_str_len >= sizeof (full_mon_str))
      {
	full_mon_str_p = (char *) malloc (full_mon_str_len + 1);
      }

    strcpy (full_mon_str_p, curr_str);
    strcat (full_mon_str_p, str->val);

    conv_func &func = get_conv_func ((data_type) cons->type, domain->type->id);
    if (func != NULL)
      {
	func (str->val, domain, db_val);
      }

    if (full_mon_str_p != full_mon_str)
      {
	free_and_init (full_mon_str_p);
      }

    // free memory
    free_string (&str);
    free_and_init (mon);
  }

  void
  server_loader::finish_line ()
  {
    if (m_session.is_failed () || !m_scancache_started)
      {
	return;
      }

    OID oid;
    int force_count = 0;
    int pruning_type = 0;
    int op_type = SINGLE_ROW_INSERT;

    int error_code = locator_attribute_info_force (m_thread_ref, &m_scancache.node.hfid, &oid, &m_attr_info, NULL, 0,
		     LC_FLUSH_INSERT, op_type, &m_scancache, &force_count, false,
		     REPL_INFO_TYPE_RBR_NORMAL, pruning_type, NULL, NULL, NULL,
		     UPDATE_INPLACE_NONE, NULL, false);
    if (error_code != NO_ERROR)
      {
	m_error_handler.on_failure_with_line (LOADDB_MSG_LOAD_FAIL);
	return;
      }

    m_session.inc_total_objects ();
  }

  void
  server_loader::locate_class (const char *class_name, OID *class_oid)
  {
    LC_FIND_CLASSNAME found = xlocator_find_class_oid (m_thread_ref, class_name, class_oid, IX_LOCK);
    if (found != LC_CLASSNAME_EXIST)
      {
	m_error_handler.on_failure_with_line (LOADDB_MSG_UNKNOWN_CLASS, class_name);
      }
  }

  void
  server_loader::register_class (const char *class_name)
  {
    OID class_oid;

    locate_class (class_name, &class_oid);
    if (m_session.is_failed ())
      {
	return;
      }

    heap_cache_attrinfo attr_info;
    int error_code = heap_attrinfo_start (m_thread_ref, &class_oid, -1, NULL, &attr_info);
    if (error_code != NO_ERROR)
      {
	m_error_handler.on_failure_with_line (LOADDB_MSG_LOAD_FAIL);
	return;
      }

    m_class_entry = m_session.get_class_registry ().register_class (class_name, m_clsid, class_oid,
		    attr_info.num_values);

    heap_attrinfo_end (m_thread_ref, &attr_info);
  }

  void
  server_loader::register_class_attributes (string_type *attr_list)
  {
    start_scancache_modify (&m_scancache);

    OID &class_oid = m_class_entry->get_class_oid ();

    heap_cache_attrinfo attr_info;
    int error_code = heap_attrinfo_start (m_thread_ref, &class_oid, -1, NULL, &attr_info);
    if (error_code != NO_ERROR)
      {
	m_error_handler.on_failure_with_line (LOADDB_MSG_LOAD_FAIL);
	return;
      }

    recdes recdes;
    SCAN_CODE scan_code = heap_get_class_record (m_thread_ref, &class_oid, &recdes, &m_scancache, PEEK);
    if (scan_code != S_SUCCESS)
      {
	heap_attrinfo_end (m_thread_ref, &attr_info);
	m_error_handler.on_failure_with_line (LOADDB_MSG_LOAD_FAIL);
	return;
      }

    heap_scancache_end_modify (m_thread_ref, &m_scancache);

    // collect class attribute names
    std::map<std::string, ATTR_ID> attr_map;
    for (int i = 0; i < attr_info.num_values; i++)
      {
	char *attr_name = NULL;
	int free_attr_name = 0;
	ATTR_ID attr_id = attr_info.values[i].attrid;

	error_code = or_get_attrname (&recdes, attr_id, &attr_name, &free_attr_name);
	if (error_code != NO_ERROR)
	  {
	    m_error_handler.on_failure_with_line (LOADDB_MSG_LOAD_FAIL);
	    return;
	  }

	std::string attr_name_ (attr_name);
	if (attr_list == NULL)
	  {
	    // if attr_list is NULL then register attributes in default order
	    or_attribute *attrepr = heap_locate_last_attrepr (attr_id, &attr_info);
	    m_class_entry->register_attribute (attr_id, attr_name_, attrepr);
	  }
	else
	  {
	    attr_map.insert (std::make_pair (attr_name_, attr_id));
	  }

	// free attr_name if it was allocated
	if (attr_name != NULL && free_attr_name == 1)
	  {
	    db_private_free_and_init (m_thread_ref, attr_name);
	  }
      }

    // register attributes in specific order required by attr_list
    int attr_idx = 0;
    for (string_type *attr = attr_list; attr != NULL; attr = attr->next, attr_idx++)
      {
	std::string attr_name_ (attr->val);
	ATTR_ID attr_id = attr_map.at (attr_name_);

	or_attribute *attrepr = heap_locate_last_attrepr (attr_id, &attr_info);

	m_class_entry->register_attribute (attr_id, attr_name_, attrepr);
      }

    heap_attrinfo_end (m_thread_ref, &attr_info);
  }

  void
  server_loader::start_scancache_modify (heap_scancache *scancache)
  {
    hfid hfid;
    OID &class_oid = m_class_entry->get_class_oid ();

    int error_code = heap_get_hfid_from_class_oid (m_thread_ref, &class_oid, &hfid);
    if (error_code != NO_ERROR)
      {
	m_error_handler.on_failure_with_line (LOADDB_MSG_LOAD_FAIL);
	return;
      }

    error_code = heap_scancache_start_modify (m_thread_ref, scancache, &hfid, &class_oid, SINGLE_ROW_INSERT, NULL);
    if (error_code != NO_ERROR)
      {
	m_error_handler.on_failure_with_line (LOADDB_MSG_LOAD_FAIL);
      }
  }

} // namespace cubload
