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

  server_class_installer::server_class_installer (session &session, error_handler &error_handler)
    : m_session (session)
    , m_error_handler (error_handler)
    , m_clsid (NULL_CLASS_ID)
  {
    //
  }

  void
  server_class_installer::set_class_id (class_id clsid)
  {
    m_clsid = clsid;
  }

  void
  server_class_installer::check_class (const char *class_name, int class_id)
  {
    (void) class_id;
    OID class_oid;

    locate_class (class_name, class_oid);
  }

  int
  server_class_installer::install_class (const char *class_name)
  {
    (void) class_name;

    register_class_with_attributes (class_name, NULL);

    return NO_ERROR;
  }

  void
  server_class_installer::install_class (string_type *class_name, class_command_spec_type *cmd_spec)
  {
    if (class_name == NULL || class_name->val == NULL)
      {
	return;
      }

    register_class_with_attributes (class_name->val, cmd_spec == NULL ? NULL : cmd_spec->attr_list);
  }

  void
  server_class_installer::locate_class (const char *class_name, OID &class_oid)
  {
    cubthread::entry &thread_ref = cubthread::get_entry ();
    LC_FIND_CLASSNAME found = xlocator_find_class_oid (&thread_ref, class_name, &class_oid, IX_LOCK);
    if (found != LC_CLASSNAME_EXIST)
      {
	m_error_handler.on_failure_with_line (LOADDB_MSG_UNKNOWN_CLASS, class_name);
      }
  }

  void
  server_class_installer::register_class_with_attributes (const char *class_name, string_type *attr_list)
  {
    OID class_oid;
    recdes recdes;
    heap_scancache scancache;
    heap_cache_attrinfo attrinfo;
    cubthread::entry &thread_ref = cubthread::get_entry ();

    assert (m_clsid != NULL_CLASS_ID);

    locate_class (class_name, class_oid);
    if (m_session.is_failed ())
      {
	// return in case when class does not exists
	return;
      }

    int error_code = heap_attrinfo_start (&thread_ref, &class_oid, -1, NULL, &attrinfo);
    if (error_code != NO_ERROR)
      {
	m_error_handler.on_failure_with_line (LOADDB_MSG_LOAD_FAIL);
	return;
      }

    heap_scancache_quick_start_with_class_oid (&thread_ref, &scancache, &class_oid);
    SCAN_CODE scan_code = heap_get_class_record (&thread_ref, &class_oid, &recdes, &scancache, PEEK);
    heap_scancache_end (&thread_ref, &scancache);

    if (scan_code != S_SUCCESS)
      {
	m_error_handler.on_failure_with_line (LOADDB_MSG_LOAD_FAIL);
	return;
      }

    // collect class attribute names
    std::map<std::string, ATTR_ID> attr_map;
    std::vector<const attribute *> attributes;
    attributes.reserve ((std::size_t) attrinfo.num_values);

    for (int i = 0; i < attrinfo.num_values; ++i)
      {
	char *attr_name = NULL;
	int free_attr_name = 0;
	ATTR_ID attr_id = attrinfo.values[i].attrid;

	error_code = or_get_attrname (&recdes, attr_id, &attr_name, &free_attr_name);
	if (error_code != NO_ERROR)
	  {
	    heap_attrinfo_end (&thread_ref, &attrinfo);
	    m_error_handler.on_failure_with_line (LOADDB_MSG_LOAD_FAIL);
	    return;
	  }

	std::string attr_name_ (attr_name);
	if (attr_list == NULL)
	  {
	    // if attr_list is NULL then register attributes in default order
	    or_attribute *attrepr = heap_locate_last_attrepr (attr_id, &attrinfo);
	    const attribute *attr = new attribute (attr_id, attr_name_, attrepr);

	    attributes.push_back (attr);
	  }
	else
	  {
	    attr_map.insert (std::make_pair (attr_name_, attr_id));
	  }

	// free attr_name if it was allocated
	if (attr_name != NULL && free_attr_name == 1)
	  {
	    db_private_free_and_init (&thread_ref, attr_name);
	  }
      }

    // register attributes in specific order required by attr_list
    for (string_type *str_attr = attr_list; str_attr != NULL; str_attr = str_attr->next)
      {
	std::string attr_name_ (str_attr->val);
	ATTR_ID attr_id = attr_map.at (attr_name_);

	or_attribute *attrepr = heap_locate_last_attrepr (attr_id, &attrinfo);
	const attribute *attr = new attribute (attr_id, attr_name_, attrepr);

	attributes.push_back (attr);
      }

    assert ((std::size_t) attrinfo.num_values == attributes.size ());
    m_session.get_class_registry ().register_class (class_name, m_clsid, class_oid, attributes);

    heap_attrinfo_end (&thread_ref, &attrinfo);
  }

  server_object_loader::server_object_loader (session &session, error_handler &error_handler)
    : m_session (session)
    , m_error_handler (error_handler)
    , m_thread_ref (NULL)
    , m_clsid (NULL_CLASS_ID)
    , m_class_entry (NULL)
    , m_attrinfo_started (false)
    , m_attrinfo ()
    , m_scancache_started (false)
    , m_scancache ()
  {
    //
  }

  void
  server_object_loader::init (class_id clsid)
  {
    m_clsid = clsid;
    m_thread_ref = &cubthread::get_entry ();

    assert (m_class_entry == NULL);
    m_class_entry = m_session.get_class_registry ().get_class_entry (clsid);
    if (m_class_entry == NULL)
      {
	return;
      }

    const OID &class_oid = m_class_entry->get_class_oid ();

    start_scancache (class_oid);
    start_attrinfo (class_oid);

    // lock class when batch starts, it will be unlocked on transaction commit/abort, see load_worker::execute
    lock_object (m_thread_ref, &class_oid, oid_Root_class_oid, IX_LOCK, LK_UNCOND_LOCK);
  }

  void
  server_object_loader::destroy ()
  {
    stop_attrinfo ();
    stop_scancache ();

    m_clsid = NULL_CLASS_ID;
    m_class_entry = NULL;
    m_thread_ref = NULL;
  }

  void
  server_object_loader::start_line (int object_id)
  {
    (void) object_id;

    // clear db values
    heap_attrinfo_clear_dbvalues (&m_attrinfo);
  }

  void
  server_object_loader::process_line (constant_type *cons)
  {
    if (m_session.is_failed ())
      {
	return;
      }

    std::size_t attr_idx = 0;
    for (constant_type *c = cons; c != NULL; c = c->next, attr_idx++)
      {
	const attribute &attr = m_class_entry->get_attribute (attr_idx);
	process_constant (c, attr);
      }
  }

  void
  server_object_loader::finish_line ()
  {
    if (m_session.is_failed () || !m_scancache_started)
      {
	return;
      }

    OID oid;
    int force_count = 0;
    int pruning_type = 0;
    int op_type = SINGLE_ROW_INSERT;

    int error_code = locator_attribute_info_force (m_thread_ref, &m_scancache.node.hfid, &oid, &m_attrinfo, NULL, 0,
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
  server_object_loader::process_constant (constant_type *cons, const attribute &attr)
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

    const OID &class_oid = m_class_entry->get_class_oid ();

    heap_attrinfo_set (&class_oid, attr.m_attr_id, &db_val, &m_attrinfo);
  }

  void
  server_object_loader::process_monetary_constant (constant_type *cons, tp_domain *domain, db_value *db_val)
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
  server_object_loader::start_scancache (const OID &class_oid)
  {
    hfid hfid;

    int error_code = heap_get_hfid_from_class_oid (m_thread_ref, &class_oid, &hfid);
    if (error_code != NO_ERROR)
      {
	m_error_handler.on_failure_with_line (LOADDB_MSG_LOAD_FAIL);
	return;
      }

    error_code = heap_scancache_start_modify (m_thread_ref, &m_scancache, &hfid, &class_oid, SINGLE_ROW_INSERT, NULL);
    if (error_code != NO_ERROR)
      {
	m_error_handler.on_failure_with_line (LOADDB_MSG_LOAD_FAIL);
	return;
      }

    m_scancache_started = true;
  }

  void
  server_object_loader::stop_scancache ()
  {
    if (!m_scancache_started)
      {
	return;
      }

    heap_scancache_end_modify (m_thread_ref, &m_scancache);
    m_scancache_started = false;
  }

  void
  server_object_loader::start_attrinfo (const OID &class_oid)
  {
    int error_code = heap_attrinfo_start (m_thread_ref, &class_oid, -1, NULL, &m_attrinfo);
    if (error_code != NO_ERROR)
      {
	m_error_handler.on_failure_with_line (LOADDB_MSG_LOAD_FAIL);
	return;
      }

    m_attrinfo_started = true;
  }

  void
  server_object_loader::stop_attrinfo ()
  {
    if (!m_attrinfo_started)
      {
	return;
      }

    heap_attrinfo_end (m_thread_ref, &m_attrinfo);
    m_attrinfo_started = false;
  }

} // namespace cubload
