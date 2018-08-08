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

#ident "$Id$"

#include <map>

#include "load_db_value_converter.hpp"
#include "load_server_loader.hpp"
#include "locator_sr.h"
#include "thread_manager.hpp"
#include "transform.h"
#include "xserver_interface.h"

namespace cubload
{

  server_loader::server_loader ()
    : m_class_oid (NULL_OID_INITIALIZER)
    , m_attr_ids (NULL)
    , m_attr_info ()
    , m_err_total (0)
    , m_total_fails (0)
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
  server_loader::act_setup_class_command_spec (string_type **class_name, class_command_spec_type **cmd_spec)
  {
    // TODO CBRD-21654 refactor this function, as well implement functionality to setup based on loaddb --table cmd option
    if (class_name == NULL || *class_name == NULL)
      {
	return;
      }

    HFID hfid;
    RECDES recdes;
    int attr_idx = 0;
    string_type *attr;
    char *string = NULL;
    int error = NO_ERROR;
    int alloced_string = 0;
    HEAP_SCANCACHE scan_cache;
    string_type *class_name_ = *class_name;
    std::map<std::string, ATTR_ID> attr_name_to_id;

    cubthread::entry &thread_ref = cubthread::get_entry ();

    LC_FIND_CLASSNAME found = xlocator_find_class_oid (&thread_ref, class_name_->val, &m_class_oid, IX_LOCK);
    if (found != LC_CLASSNAME_EXIST)
      {
	ldr_string_free (class_name);
	ldr_class_command_spec_free (cmd_spec);
	return;
      }

    // we do not need anymore class_name
    ldr_string_free (class_name);

    error = heap_get_hfid_from_class_oid (&thread_ref, &m_class_oid, &hfid);
    if (error != NO_ERROR)
      {
	// FIXME - error handling(reporting)
	ldr_class_command_spec_free (cmd_spec);
	return;
      }

    error = heap_scancache_start_modify (&thread_ref, &scan_cache, &hfid, &m_class_oid, SINGLE_ROW_INSERT, NULL);
    if (error != NO_ERROR)
      {
	// FIXME - error handling(reporting)
	ldr_class_command_spec_free (cmd_spec);
	return;
      }

    error = heap_attrinfo_start (&thread_ref, &m_class_oid, -1, NULL, &m_attr_info);
    if (error != NO_ERROR)
      {
	// FIXME - error handling(reporting)
	ldr_class_command_spec_free (cmd_spec);
	return;
      }

    SCAN_CODE scan_code = heap_get_class_record (&thread_ref, &m_class_oid, &recdes, &scan_cache, PEEK);
    if (scan_code != S_SUCCESS)
      {
	// FIXME - error handling(reporting)
	ldr_class_command_spec_free (cmd_spec);
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
	    ldr_class_command_spec_free (cmd_spec);
	    return;
	  }

	attr_name_to_id.insert (std::pair<std::string, ATTR_ID> (std::string (string), attr_id));

	if (string != NULL && alloced_string == 1)
	  {
	    db_private_free_and_init (&thread_ref, string);
	  }
      }

    if (cmd_spec == NULL || *cmd_spec == NULL)
      {
	return;
      }
    class_command_spec_type *cmd_spec_ = *cmd_spec;

    // alloc attribute ids array
    m_attr_ids = new ATTR_ID[m_attr_info.num_values];

    for (attr = cmd_spec_->attr_list; attr; attr = attr->next, attr_idx++)
      {
	m_attr_ids[attr_idx] = attr_name_to_id.at (std::string (attr->val));
      }
    ldr_class_command_spec_free (cmd_spec);

    error = heap_scancache_end_modify (&thread_ref, &scan_cache);
    if (error != NO_ERROR)
      {
	// FIXME - error handling(reporting)
	return;
      }

    // lock class when batch starts, it will be unlocked on transaction commit/abort, see load_parse_task::execute
    lock_object (&thread_ref, &m_class_oid, oid_Root_class_oid, IX_LOCK, LK_UNCOND_LOCK);
  }

  void
  server_loader::act_start_id (char *name)
  {
  }

  void
  server_loader::act_set_id (int id)
  {
    // fetch class having id
  }

  void
  server_loader::act_start_instance (int id, constant_type *cons)
  {
    // clear db values
    heap_attrinfo_clear_dbvalues (&m_attr_info);
  }

  void
  server_loader::process_constants (constant_type *cons)
  {
    // TODO CBRD-21654 refactor this function
    int attr_idx = 0;
    constant_type *c, *save;

    for (c = cons; c; c = save, attr_idx++)
      {
	DB_VALUE db_val;
	save = c->next;

	TP_DOMAIN *domain = heap_locate_last_attrepr (m_attr_ids[attr_idx], &m_attr_info)->domain;

	switch (c->type)
	  {
	  case LDR_NULL:
	    to_db_null (NULL, NULL, &db_val);
	    break;
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
	    string_type *str = (string_type *) c->val;
	    conv_func func = get_conv_func (c->type, domain);

	    if (func != NULL)
	      {
		func (str->val, domain, &db_val);
	      }

	    ldr_string_free (&str);
	  }
	  break;

	  case LDR_MONETARY:
	  {
	    monetary_type *mon = (monetary_type *) c->val;
	    string_type *str = mon->amount;

	    /* buffer size for monetary : numeric size + grammar currency symbol + string terminator */
	    char full_mon_str[NUM_BUF_SIZE + 3 + 1];
	    char *full_mon_str_p = full_mon_str;

	    /* In Loader grammar always print symbol before value (position of currency symbol is not localized) */
	    char *curr_str = intl_get_money_esc_ISO_symbol ((DB_CURRENCY) mon->currency_type);
	    unsigned int full_mon_str_len = (strlen (str->val) + strlen (curr_str));

	    if (full_mon_str_len >= sizeof (full_mon_str))
	      {
		full_mon_str_p = (char *) malloc (full_mon_str_len + 1);
	      }

	    strcpy (full_mon_str_p, curr_str);
	    strcat (full_mon_str_p, str->val);

	    conv_func func = get_conv_func (c->type, domain);
	    if (func != NULL)
	      {
		func (str->val, domain, &db_val);
	      }

	    if (full_mon_str_p != full_mon_str)
	      {
		free_and_init (full_mon_str_p);
	      }
	    ldr_string_free (&str);
	    free_and_init (mon);
	  }
	  break;

	  case LDR_BSTR:
	  case LDR_XSTR:
	  case LDR_ELO_INT:
	  case LDR_ELO_EXT:
	  case LDR_SYS_USER:
	  case LDR_SYS_CLASS:
	  {
	    string_type *str = (string_type *) c->val;

	    //conv_func func = get_conv_func (c->type, domain);
	    //func (str->val, domain, &db_val);

	    ldr_string_free (&str);
	  }
	  break;

	  case LDR_OID:
	  case LDR_CLASS_OID:
	    //ldr_process_object_ref ((object_ref_type *) c->val, c->type);
	    break;

	  case LDR_COLLECTION:
	    // TODO CBRD-21654 add support for collections
	    //(*ldr_act) (ldr_Current_context, "{", 1, LDR_COLLECTION);
	    process_constants ((constant_type *) c->val);
	    //ldr_act_attr (ldr_Current_context, NULL, 0, LDR_COLLECTION);
	    break;

	  default:
	    break;
	  }

	if (c->need_free)
	  {
	    free_and_init (c);
	  }

	heap_attrinfo_set (&m_class_oid, m_attr_ids[attr_idx], &db_val, &m_attr_info);
      }
  }

  void
  server_loader::act_finish_line ()
  {
    OID oid;
    HFID hfid;
    int force_count = 0;
    int pruning_type = 0;
    int error = NO_ERROR;
    HEAP_SCANCACHE scan_cache;
    int op_type = MULTI_ROW_INSERT;

    cubthread::entry &thread_ref = cubthread::get_entry ();

    // TODO: What about keeping "scancache" for the class? act_setup_command_spec may keep it.

    error = heap_get_hfid_from_class_oid (&thread_ref, &m_class_oid, &hfid);
    if (error != NO_ERROR)
      {
	// FIXME - error handling(reporting)
	return;
      }

    error = heap_scancache_start_modify (&thread_ref, &scan_cache, &hfid, &m_class_oid, op_type, NULL);
    if (error != NO_ERROR)
      {
	// FIXME - error handling(reporting)
	return;
      }

    error = locator_attribute_info_force (&thread_ref, &scan_cache.node.hfid, &oid, &m_attr_info, NULL, 0,
					  LC_FLUSH_INSERT, op_type, &scan_cache, &force_count, false,
					  REPL_INFO_TYPE_RBR_NORMAL, pruning_type, NULL, NULL, NULL,
					  UPDATE_INPLACE_NONE, NULL, false);
    if (error != NO_ERROR)
      {
	// FIXME - error handling(reporting)
      }

    error = heap_scancache_end_modify (&thread_ref, &scan_cache);
    if (error != NO_ERROR)
      {
	// FIXME - error handling(reporting)
	return;
      }
  }

  void
  server_loader::act_finish ()
  {
    if (m_attr_ids != NULL)
      {
	delete m_attr_ids;
	m_attr_ids = NULL;
      }

    heap_attrinfo_end (&cubthread::get_entry (), &m_attr_info);
    m_class_oid = NULL_OID_INITIALIZER;

    m_err_total = 0;
    m_total_fails = 0;
  }

  void
  server_loader::load_failed_error ()
  {
    //
  }

  void
  server_loader::increment_err_total ()
  {
    m_err_total++;
  }

  void
  server_loader::increment_fails ()
  {
    m_total_fails++;
  }
} // namespace cubload
