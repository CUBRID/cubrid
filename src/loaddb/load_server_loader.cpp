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
#include "load_session.hpp"
#include "locator_sr.h"
#include "oid.h"
#include "thread_manager.hpp"
#include "xserver_interface.h"

#include <map>

namespace cubload
{

  server_loader::server_loader (session *session)
    : m_class_oid (OID_INITIALIZER)
    , m_attr_ids (NULL)
    , m_attr_info ()
    , m_scan_cache ()
    , m_scan_cache_started (false)
    , m_session (session)
  {
    //
  }

  server_loader::~server_loader ()
  {
    if (m_attr_ids != NULL)
      {
	delete m_attr_ids;
	m_attr_ids = NULL;
      }
  }

  void
  server_loader::check_class (const char *class_name, int class_id)
  {
    //
  }

  int
  server_loader::setup_class (const char *class_name)
  {
    return NO_ERROR;
  }

  void
  server_loader::setup_class (string_type *class_name, class_command_spec_type *cmd_spec)
  {
    // TODO CBRD-21654 refactor this function, as well implement functionality to setup based on loaddb --table cmd option

    hfid hfid;
    recdes recdes;
    int attr_idx = 0;
    string_type *attr;
    char *string = NULL;
    int error = NO_ERROR;
    int alloced_string = 0;
    std::map<std::string, ATTR_ID> attr_name_to_id;

    if (class_name == NULL || class_name->val == NULL)
      {
	return;
      }

    cubthread::entry &thread_ref = cubthread::get_entry ();

    LC_FIND_CLASSNAME found = xlocator_find_class_oid (&thread_ref, class_name->val, &m_class_oid, IX_LOCK);
    if (found != LC_CLASSNAME_EXIST)
      {
	// FIXME - error handling(reporting)
	return;
      }

    error = heap_get_hfid_from_class_oid (&thread_ref, &m_class_oid, &hfid);
    if (error != NO_ERROR)
      {
	// FIXME - error handling(reporting)
	return;
      }

    error = heap_scancache_start_modify (&thread_ref, &m_scan_cache, &hfid, &m_class_oid, SINGLE_ROW_INSERT, NULL);
    if (error != NO_ERROR)
      {
	// FIXME - error handling(reporting)
	m_scan_cache_started = false;
	return;
      }

    m_scan_cache_started = true;

    error = heap_attrinfo_start (&thread_ref, &m_class_oid, -1, NULL, &m_attr_info);
    if (error != NO_ERROR)
      {
	// FIXME - error handling(reporting)
	return;
      }

    SCAN_CODE scan_code = heap_get_class_record (&thread_ref, &m_class_oid, &recdes, &m_scan_cache, PEEK);
    if (scan_code != S_SUCCESS)
      {
	// FIXME - error handling(reporting)
	return;
      }

    // collect class attribute names
    for (int i = 0; i < m_attr_info.num_values; i++)
      {
	alloced_string = 0;
	string = NULL;

	ATTR_ID attr_id = m_attr_info.values[i].attrid;
	error = or_get_attrname (&recdes, attr_id, &string, &alloced_string);
	if (error != NO_ERROR)
	  {
	    // FIXME - error handling(reporting)
	    return;
	  }

	attr_name_to_id.insert (std::pair<std::string, ATTR_ID> (std::string (string), attr_id));

	if (string != NULL && alloced_string == 1)
	  {
	    db_private_free_and_init (&thread_ref, string);
	  }
      }

    if (cmd_spec == NULL)
      {
	return;
      }

    // alloc attribute ids array
    m_attr_ids = new ATTR_ID[m_attr_info.num_values];

    for (attr = cmd_spec->attr_list; attr; attr = attr->next, attr_idx++)
      {
	m_attr_ids[attr_idx] = attr_name_to_id.at (std::string (attr->val));
      }

    // lock class when batch starts, it will be unlocked on transaction commit/abort, see load_parse_task::execute
    lock_object (&thread_ref, &m_class_oid, oid_Root_class_oid, IX_LOCK, LK_UNCOND_LOCK);
  }

  void
  server_loader::destroy ()
  {
    if (m_attr_ids != NULL)
      {
	delete m_attr_ids;
	m_attr_ids = NULL;
      }

    cubthread::entry &thread_ref = cubthread::get_entry ();

    heap_attrinfo_end (&thread_ref, &m_attr_info);

    if (m_scan_cache_started)
      {
	heap_scancache_end_modify (&thread_ref, &m_scan_cache);
	m_scan_cache_started = false;
      }

    m_class_oid = OID_INITIALIZER;
  }

  void
  server_loader::start_line (int object_id)
  {
    // clear db values
    heap_attrinfo_clear_dbvalues (&m_attr_info);
  }

  void
  server_loader::process_line (constant_type *cons)
  {
    if (m_session->aborted ())
      {
	return;
      }

    int attr_idx = 0;
    constant_type *c, *save;

    for (c = cons; c; c = save, attr_idx++)
      {
	save = c->next;
	process_constant (c, attr_idx);
      }
  }

  void
  server_loader::process_constant (constant_type *cons, int attr_idx)
  {
    // TODO CBRD-21654 refactor this function
    db_value db_val;
    or_attribute *attr = heap_locate_last_attrepr (m_attr_ids[attr_idx], &m_attr_info);

    if (attr == NULL)
      {
	// TODO report an error
	return;
      }

    switch (cons->type)
      {
      case LDR_NULL:
	if (attr->is_notnull)
	  {
	    // TODO report an error
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
	conv_func &func = get_conv_func ((data_type) cons->type, attr->domain->type->id);

	if (func != NULL)
	  {
	    func (str != NULL ? str->val : NULL, attr->domain, &db_val);
	  }
	else
	  {
	    // TODO report an error
	    return;
	  }

	free_string (&str);
      }
      break;
      case LDR_MONETARY:
	process_monetary_constant (cons, attr->domain, &db_val);
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

    heap_attrinfo_set (&m_class_oid, m_attr_ids[attr_idx], &db_val, &m_attr_info);
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
    if (m_session->aborted ())
      {
	return;
      }

    OID oid;
    int force_count = 0;
    int pruning_type = 0;
    int error = NO_ERROR;
    int op_type = MULTI_ROW_INSERT;

    cubthread::entry &thread_ref = cubthread::get_entry ();

    if (!m_scan_cache_started)
      {
	return;
      }

    error = locator_attribute_info_force (&thread_ref, &m_scan_cache.node.hfid, &oid, &m_attr_info, NULL, 0,
					  LC_FLUSH_INSERT, op_type, &m_scan_cache, &force_count, false,
					  REPL_INFO_TYPE_RBR_NORMAL, pruning_type, NULL, NULL, NULL,
					  UPDATE_INPLACE_NONE, NULL, false);
    if (error != NO_ERROR)
      {
	// FIXME - error handling(reporting)
	return;
      }
  }

  void
  server_loader::on_error ()
  {
    m_session->error ();
    // TODO CBRD-21654 implement equivalent of
    // fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_LOADDB, LOADDB_MSG_SYNTAX_ERR),
    //          scanner_lineno (), scanner_text ());
    // on SERVER_MODE
  }

  void
  server_loader::on_failure ()
  {
    // TODO CBRD-21654 implement equivalent of ldr_load_failed_error on SERVER_MODE
    m_session->abort ();
  }
} // namespace cubload
