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

#include "btree.h"
#include "load_class_registry.hpp"
#include "load_db_value_converter.hpp"
#include "load_driver.hpp"
#include "load_error_handler.hpp"
#include "load_session.hpp"
#include "locator_sr.h"
#include "object_primitive.h"
#include "record_descriptor.hpp"
#include "set_object.h"
#include "string_opfunc.h"
#include "thread_manager.hpp"
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

    if (locate_class (class_name, class_oid) != LC_CLASSNAME_EXIST)
      {
	m_error_handler.on_failure_with_line (LOADDB_MSG_UNKNOWN_CLASS, class_name);
      }

    m_session.append_log_msg (LOADDB_MSG_CLASS_TITLE, class_name);
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

    register_class_with_attributes (class_name->val, cmd_spec);
  }

  LC_FIND_CLASSNAME
  server_class_installer::locate_class (const char *class_name, OID &class_oid)
  {
    cubthread::entry &thread_ref = cubthread::get_entry ();
    return xlocator_find_class_oid (&thread_ref, class_name, &class_oid, IX_LOCK);
  }

  void
  server_class_installer::register_class_with_attributes (const char *class_name, class_command_spec_type *cmd_spec)
  {
    OID class_oid;
    recdes recdes;
    heap_scancache scancache;
    heap_cache_attrinfo attrinfo;
    cubthread::entry &thread_ref = cubthread::get_entry ();
    bool is_syntax_check_only = m_session.get_args ().syntax_check;

    assert (m_clsid != NULL_CLASS_ID);
    OID_SET_NULL (&class_oid);

    if (m_session.is_failed ())
      {
	// return in case when class does not exists
	return;
      }

    if (locate_class (class_name, class_oid) != LC_CLASSNAME_EXIST)
      {
	if (is_syntax_check_only)
	  {
	    // We must have the class installed already.
	    // This translates into a syntax error.
	    m_error_handler.on_error_with_line (LOADDB_MSG_UNKNOWN_CLASS, class_name);
	  }
	else
	  {
	    m_error_handler.on_failure_with_line (LOADDB_MSG_UNKNOWN_CLASS, class_name);
	  }
	return;
      }

    int error_code = heap_attrinfo_start (&thread_ref, &class_oid, -1, NULL, &attrinfo);
    assert (attrinfo.num_values != -1);
    if (error_code != NO_ERROR)
      {
	m_error_handler.on_failure_with_line (LOADDB_MSG_LOAD_FAIL);
	return;
      }

    heap_scancache_quick_start_with_class_oid (&thread_ref, &scancache, &class_oid);
    SCAN_CODE scan_code = heap_get_class_record (&thread_ref, &class_oid, &recdes, &scancache, PEEK);
    if (scan_code != S_SUCCESS)
      {
	heap_scancache_end (&thread_ref, &scancache);
	heap_attrinfo_end (&thread_ref, &attrinfo);
	m_error_handler.on_failure_with_line (LOADDB_MSG_LOAD_FAIL);
	return;
      }

    int n_attributes = -1;
    or_attribute *or_attributes = NULL;
    attribute_type attr_type = cmd_spec != NULL ? cmd_spec->attr_type : LDR_ATTRIBUTE_DEFAULT;
    get_class_attributes (attrinfo, attr_type, or_attributes, &n_attributes);

    // collect class attribute names
    std::map<std::string, or_attribute *> attr_map;
    std::vector<const attribute *> attributes;
    attributes.reserve ((std::size_t) n_attributes);

    for (std::size_t attr_index = 0; attr_index < (std::size_t) n_attributes; ++attr_index)
      {
	char *attr_name = NULL;
	int free_attr_name = 0;
	or_attribute *attr_repr = &or_attributes[attr_index];

	error_code = or_get_attrname (&recdes, attr_repr->id, &attr_name, &free_attr_name);
	if (error_code != NO_ERROR)
	  {
	    heap_scancache_end (&thread_ref, &scancache);
	    heap_attrinfo_end (&thread_ref, &attrinfo);
	    m_error_handler.on_failure_with_line (LOADDB_MSG_LOAD_FAIL);
	    return;
	  }

	std::string attr_name_ (attr_name);
	if (cmd_spec == NULL)
	  {
	    // if attr_list is NULL then register attributes in default order
	    assert (attr_repr->domain != NULL);
	    const attribute *attr = new attribute (attr_name_, attr_index, attr_repr);

	    attributes.push_back (attr);
	  }
	else
	  {
	    attr_map.insert (std::make_pair (attr_name_, attr_repr));
	  }

	// free attr_name if it was allocated
	if (attr_name != NULL && free_attr_name == 1)
	  {
	    db_private_free_and_init (&thread_ref, attr_name);
	  }
      }

    // register attributes in specific order required by attr_list
    std::size_t attr_index = 0;
    string_type *str_attr = cmd_spec != NULL ? cmd_spec->attr_list : NULL;
    for (; str_attr != NULL; str_attr = str_attr->next, ++attr_index)
      {
	std::string attr_name_ (str_attr->val);

	auto found = attr_map.find (attr_name_);
	if (found == attr_map.end ())
	  {
	    heap_scancache_end (&thread_ref, &scancache);
	    heap_attrinfo_end (&thread_ref, &attrinfo);
	    m_error_handler.on_failure_with_line (LOADDB_MSG_LOAD_FAIL);
	    return;
	  }

	or_attribute *attr_repr = found->second;

	assert (attr_repr != NULL && attr_repr->domain != NULL);
	const attribute *attr = new attribute (attr_name_, attr_index, attr_repr);

	attributes.push_back (attr);
      }

    m_session.get_class_registry ().register_class (class_name, m_clsid, class_oid, attributes);

    heap_scancache_end (&thread_ref, &scancache);
    heap_attrinfo_end (&thread_ref, &attrinfo);
  }

  void
  server_class_installer::get_class_attributes (heap_cache_attrinfo &attrinfo, attribute_type attr_type,
      or_attribute *&or_attributes, int *n_attributes)
  {
    *n_attributes = -1;
    switch (attr_type)
      {
      case LDR_ATTRIBUTE_CLASS:
	or_attributes = attrinfo.last_classrepr->class_attrs;
	*n_attributes = attrinfo.last_classrepr->n_class_attrs;
	break;
      case LDR_ATTRIBUTE_SHARED:
	or_attributes = attrinfo.last_classrepr->shared_attrs;
	*n_attributes = attrinfo.last_classrepr->n_shared_attrs;
	break;
      case LDR_ATTRIBUTE_ANY:
      case LDR_ATTRIBUTE_DEFAULT:
	or_attributes = attrinfo.last_classrepr->attributes;
	*n_attributes = attrinfo.last_classrepr->n_attributes;
	break;
      default:
	assert (false);
	break;
      }
    assert (*n_attributes >= 0);
  }

  server_object_loader::server_object_loader (session &session, error_handler &error_handler)
    : m_session (session)
    , m_error_handler (error_handler)
    , m_thread_ref (NULL)
    , m_clsid (NULL_CLASS_ID)
    , m_class_entry (NULL)
    , m_attrinfo_started (false)
    , m_attrinfo ()
    , m_db_values ()
    , m_recdes_collected ()
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

    m_recdes_collected.clear ();

    m_clsid = NULL_CLASS_ID;
    m_class_entry = NULL;
    m_thread_ref = NULL;
  }

  void
  server_object_loader::start_line (int object_id)
  {
    (void) object_id;
  }

  void
  server_object_loader::process_line (constant_type *cons)
  {
    if (m_session.is_failed ())
      {
	return;
      }

    std::size_t attr_index = 0;
    std::size_t attr_size = m_class_entry->get_attributes_size ();

    if (cons != NULL && attr_size == 0)
      {
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LDR_NO_CLASS_OR_NO_ATTRIBUTE, 0);
	m_error_handler.on_syntax_failure ();
	return;
      }

    for (constant_type *c = cons; c != NULL; c = c->next, attr_index++)
      {
	if (attr_index == attr_size)
	  {
	    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LDR_VALUE_OVERFLOW, 1, attr_index);
	    m_error_handler.on_syntax_failure ();
	    return;
	  }

	const attribute &attr = m_class_entry->get_attribute (attr_index);
	int error_code = process_constant (c, attr);
	if (error_code != NO_ERROR)
	  {
	    m_error_handler.on_syntax_failure ();
	    return;
	  }

	db_value &db_val = get_attribute_db_value (attr_index);
	heap_attrinfo_set (&m_class_entry->get_class_oid (), attr.get_repr ().id, &db_val, &m_attrinfo);
      }

    if (attr_index < attr_size)
      {
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LDR_MISSING_ATTRIBUTES, 2, attr_size, attr_index);
	m_error_handler.on_syntax_failure ();
	return;
      }
  }

  void
  server_object_loader::finish_line ()
  {
    if (m_session.is_failed () || !m_scancache_started)
      {
	return;
      }

    bool is_syntax_check_only = m_session.get_args ().syntax_check;
    if (!is_syntax_check_only)
      {
	// Create the record and add it to the array of collected records.
	record_descriptor new_recdes;
	RECDES *old_recdes = NULL;

	if (heap_attrinfo_transform_to_disk (m_thread_ref, &m_attrinfo, old_recdes, &new_recdes) != S_SUCCESS)
	  {
	    m_error_handler.on_failure ();
	    return;
	  }

	// Add the recdes to the collected array.
	m_recdes_collected.push_back (std::move (new_recdes));
      }

    m_session.stats_update_current_line (m_thread_ref->m_loaddb_driver->get_scanner ().lineno () + 1);
    clear_db_values ();
  }

  void
  server_object_loader::flush_records ()
  {
    int force_count = 0;
    int pruning_type = 0;
    int op_type = SINGLE_ROW_INSERT;

    // First check if we have any errors set.
    if (m_session.is_failed ())
      {
	return;
      }

    int error_code = locator_multi_insert_force (m_thread_ref, &m_scancache.node.hfid, &m_scancache.node.class_oid,
		     m_recdes_collected, true, op_type, &m_scancache, &force_count, pruning_type, NULL, NULL,
		     UPDATE_INPLACE_NONE);
    if (error_code != NO_ERROR)
      {
	ASSERT_ERROR ();
	m_error_handler.on_failure ();
      }
  }

  int
  server_object_loader::process_constant (constant_type *cons, const attribute &attr)
  {
    string_type *str = NULL;
    int error_code = NO_ERROR;

    switch (cons->type)
      {
      case LDR_NULL:
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
      case LDR_BSTR:
      case LDR_XSTR:
      case LDR_ELO_INT:
      case LDR_ELO_EXT:
	error_code = process_generic_constant (cons, attr);
	break;

      case LDR_MONETARY:
	error_code = process_monetary_constant (cons, attr);
	break;

      case LDR_COLLECTION:
	error_code = process_collection_constant (reinterpret_cast<constant_type *> (cons->val), attr);
	break;

      case LDR_SYS_USER:
      case LDR_SYS_CLASS:
      {
	const char *class_name;
	str = reinterpret_cast<string_type *> (cons->val);

	if (str != NULL && str->val != NULL)
	  {
	    class_name = str->val;
	  }
	else
	  {
	    class_name = cons->type == LDR_SYS_USER ? "db_user" : "*system class*";
	  }

	error_code = ER_FAILED;
	m_error_handler.on_error_with_line (LOADDB_MSG_UNAUTHORIZED_CLASS, class_name);
      }
      break;

      case LDR_OID:
      case LDR_CLASS_OID:
	// Object References and Class Object Reference are not supported by server loaddb implementation
	error_code = ER_FAILED;
	m_error_handler.on_failure_with_line (LOADDB_MSG_OID_NOT_SUPPORTED);
	break;

      default:
	error_code = ER_FAILED;
	break;
      }

    return error_code;
  }

  int
  server_object_loader::process_generic_constant (constant_type *cons, const attribute &attr)
  {
    string_type *str = reinterpret_cast<string_type *> (cons->val);
    char *token = str != NULL ? str->val : NULL;

    db_value &db_val = get_attribute_db_value (attr.get_index ());
    conv_func &func = get_conv_func (cons->type, attr.get_domain ().type->get_id ());

    int error_code = func (token, &attr, &db_val);
    if (error_code == ER_DATE_CONVERSION)
      {
	m_error_handler.log_date_time_conversion_error (token, pr_type_name (attr.get_domain ().type->get_id ()));
      }

    return error_code;
  }

  int
  server_object_loader::process_monetary_constant (constant_type *cons, const attribute &attr)
  {
    int error_code = NO_ERROR;
    monetary_type *mon = reinterpret_cast<monetary_type *> (cons->val);
    string_type *str = mon->amount;

    /* buffer size for monetary : numeric size + grammar currency symbol + string terminator */
    char full_mon_str[NUM_BUF_SIZE + 3 + 1];
    char *full_mon_str_p = full_mon_str;

    /* In Loader grammar always print symbol before value (position of currency symbol is not localized) */
    char *curr_str = intl_get_money_esc_ISO_symbol ((DB_CURRENCY) mon->currency_type);
    size_t full_mon_str_len = (str->size + strlen (curr_str));

    if (full_mon_str_len >= sizeof (full_mon_str))
      {
	full_mon_str_p = new char[full_mon_str_len + 1];
      }

    std::strcpy (full_mon_str_p, curr_str);
    std::strcat (full_mon_str_p, str->val);

    db_value &db_val = get_attribute_db_value (attr.get_index ());
    conv_func &func = get_conv_func (cons->type, attr.get_domain ().type->get_id ());

    error_code = func (full_mon_str_p, &attr, &db_val);
    if (error_code != NO_ERROR)
      {
	return error_code;
      }

    if (full_mon_str_p != full_mon_str)
      {
	delete [] full_mon_str_p;
      }

    delete mon;

    return error_code;
  }

  int
  server_object_loader::process_collection_constant (constant_type *cons, const attribute &attr)
  {
    int error_code = NO_ERROR;
    const tp_domain &domain = attr.get_domain ();

    if (!TP_IS_SET_TYPE (domain.type->get_id ()))
      {
	error_code = ER_LDR_DOMAIN_MISMATCH;
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_code, 4, attr.get_name (), m_class_entry->get_class_name (),
		pr_type_name (DB_TYPE_SET), domain.type->get_name ());

	return error_code;
      }

    DB_COLLECTION *set = set_create_with_domain (const_cast<tp_domain *> (&domain), 0);
    if (set == NULL)
      {
	error_code = er_errid ();
	assert (error_code != NO_ERROR);

	return error_code;
      }

    db_value &db_val = get_attribute_db_value (attr.get_index ());
    for (constant_type *c = cons; c != NULL; c = c->next)
      {
	switch (c->type)
	  {
	  case LDR_COLLECTION:
	    error_code = ER_LDR_NESTED_SET;
	    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_code, 0);
	    break;

	  case LDR_MONETARY:
	    error_code = process_monetary_constant (c, attr);
	    break;

	  case LDR_OID:
	  case LDR_CLASS_OID:
	    // Object References and Class Object Reference are not supported by server loaddb implementation
	    error_code = ER_FAILED;
	    m_error_handler.on_failure_with_line (LOADDB_MSG_OID_NOT_SUPPORTED);
	    break;

	  default:
	    error_code = process_generic_constant (c, attr);
	    break;
	  }

	if (error_code != NO_ERROR)
	  {
	    set_free (set);
	    return error_code;
	  }

	// add element to the set (db_val will be cloned, so it is safe to reuse same variable)
	error_code = set_add_element (set, &db_val);
	if (error_code != NO_ERROR)
	  {
	    set_free (set);
	    return error_code;
	  }
      }

    db_make_collection (&db_val, set);

    return error_code;
  }

  void
  server_object_loader::clear_db_values ()
  {
    size_t n_values = m_db_values.size ();
    if (m_attrinfo.num_values > 0 && m_attrinfo.num_values < (int) m_db_values.size ())
      {
	n_values = (size_t) m_attrinfo.num_values;
      }

    for (size_t i = 0; i < n_values; ++i)
      {
	db_value_clear (&m_db_values[i]);
      }

    heap_attrinfo_clear_dbvalues (&m_attrinfo);
  }

  db_value &
  server_object_loader::get_attribute_db_value (size_t attr_index)
  {
    if (attr_index == m_db_values.size ())
      {
	m_db_values.emplace_back ();
      }

    return m_db_values[attr_index];
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
