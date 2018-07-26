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
 * loader_sr.cpp: Loader server definitions. Updated using design from fast loaddb prototype
 */

#ident "$Id$"

#include <map>

#include "db_value_converter.hpp"
#include "dbtype_def.h"
#include "heap_attrinfo.h"
#include "loader_sr.hpp"
#include "locator_sr.h"
#include "thread_manager.hpp"
#include "xserver_interface.h"

namespace cubload
{

  server_loader::server_loader ()
    : m_class_oid ()
    , m_attr_id (NULL)
    , m_attr_info ()
  {
    //
  }

  server_loader::~server_loader ()
  {
    delete m_attr_id;
  }

  void
  server_loader::act_setup_class_command_spec (string_t **class_name, class_cmd_spec_t **cmd_spec)
  {
    if (class_name == NULL || *class_name == NULL)
      {
	return;
      }
    if (cmd_spec == NULL || *cmd_spec == NULL)
      {
	return;
      }

    class_cmd_spec_t *cmd_spec_ = *cmd_spec;
    string_t *class_name_ = *class_name;

    char *string = NULL;
    int alloced_string = 0;
    int attr_idx = 0;
    string_t *attr;
    RECDES recdes;
    scan_cache_t scan_cache;
    attr_info_t attr_info;
    std::map<std::string, attr_id_t> attr_name_to_id;
    cubthread::entry &thread_ref = cubthread::get_entry ();

    LC_FIND_CLASSNAME found = xlocator_find_class_oid (&thread_ref, class_name_->val, &m_class_oid, IX_LOCK);

    if (found != LC_CLASSNAME_EXIST)
      {
	return;
      }

    heap_scancache_quick_start_with_class_oid (&thread_ref, &scan_cache, &m_class_oid);
    heap_attrinfo_start (&thread_ref, &m_class_oid, -1, NULL, &attr_info);
    heap_get_class_record (&thread_ref, &m_class_oid, &recdes, &scan_cache, PEEK);

    for (int i = 0; i < attr_info.num_values; i++)
      {
	alloced_string = 0;
	string = NULL;

	attr_id_t attr_id = attr_info.values[i].attrid;
	or_get_attrname (&recdes, attr_id, &string, &alloced_string);

	attr_name_to_id.insert (std::pair<std::string, attr_id_t> (std::string (string), attr_id));

	if (string != NULL && alloced_string == 1)
	  {
	    db_private_free_and_init (&thread_ref, string);
	  }
      }

    // alloc attribute ids array
    m_attr_id = new attr_id_t[attr_info.num_values];

    for (attr = cmd_spec_->attr_list; attr; attr = attr->next, attr_idx++)
      {
	m_attr_id[attr_idx] = attr_name_to_id.at (std::string ((attr->val)));
      }

    heap_attrinfo_end (&thread_ref, &attr_info);
    heap_scancache_end (&thread_ref, &scan_cache);

    // free
    ldr_class_command_spec_free (cmd_spec);
    ldr_string_free (class_name);
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
  server_loader::act_start_instance (int id, constant_t *cons)
  {
    cubthread::entry &thread_ref = cubthread::get_entry ();
    heap_attrinfo_start (&thread_ref, &m_class_oid, -1, NULL, &m_attr_info);
  }

  void
  server_loader::process_constants (constant_t *cons)
  {
    int attr_idx = 0;
    constant_t *c, *save;

    for (c = cons; c; c = save, attr_idx++)
      {
	DB_VALUE *db_val_ptr;
	save = c->next;

	switch (c->type)
	  {
	  case LDR_NULL:
	  {
	    DB_VALUE db_val = to_db_null <OR_ATTRIBUTE> (NULL, NULL);
	    db_val_ptr = &db_val;
	  }
	  break;
	  case LDR_INT:
	  {
	    string_t *str = (string_t *) c->val;
	    DB_VALUE db_val = to_db_int <OR_ATTRIBUTE> (str->val, NULL);
	    db_val_ptr = &db_val;

	    ldr_string_free (&str);
	  }
	  break;
	  case LDR_FLOAT:
	  case LDR_DOUBLE:
	  case LDR_NUMERIC:
	  case LDR_DATE:
	  case LDR_TIME:
	  case LDR_TIMELTZ:
	  case LDR_TIMETZ:
	  case LDR_TIMESTAMP:
	  case LDR_TIMESTAMPLTZ:
	  case LDR_TIMESTAMPTZ:
	  case LDR_DATETIME:
	  case LDR_DATETIMELTZ:
	  case LDR_DATETIMETZ:
	  case LDR_STR:
	  case LDR_NSTR:
	  {
	    string_t *str = (string_t *) c->val;

	    //(*ldr_act) (ldr_Current_context, str->val, str->size, (LDR_TYPE) c->type);
	    ldr_string_free (&str);
	  }
	  break;

	  case LDR_MONETARY:
	  {
	    monetary_t *mon = (monetary_t *) c->val;
	    string_t *str = (string_t *) mon->amount;
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

	    //(*ldr_act) (ldr_Current_context, full_mon_str_p, strlen (full_mon_str_p), (LDR_TYPE) c->type);
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
	    string_t *str = (string_t *) c->val;

	    //(*ldr_act) (ldr_Current_context, str->val, strlen (str->val), (LDR_TYPE) c->type);
	    ldr_string_free (&str);
	  }
	  break;

	  case LDR_OID:
	  case LDR_CLASS_OID:
	    //ldr_process_object_ref ((object_ref_t *) c->val, c->type);
	    break;

	  case LDR_COLLECTION:
	    //(*ldr_act) (ldr_Current_context, "{", 1, LDR_COLLECTION);
	    process_constants ((constant_t *) c->val);
	    //ldr_act_attr (ldr_Current_context, NULL, 0, LDR_COLLECTION);
	    break;

	  default:
	    break;
	  }

	if (c->need_free)
	  {
	    free_and_init (c);
	  }

	heap_attrinfo_set (&m_class_oid, m_attr_id[attr_idx], db_val_ptr, &m_attr_info);
      }
  }

  void
  server_loader::act_finish_line ()
  {
    oid_t oid;
    int ret;
    int force_count = 0;
    int pruning_type = 0;
    HEAP_SCANCACHE scan_cache;
    cubthread::entry &thread_ref = cubthread::get_entry ();

    ret = lock_object (&thread_ref, &m_class_oid, oid_Root_class_oid, IX_LOCK, LK_UNCOND_LOCK);
    if (ret != LK_GRANTED)
      {
	return;
      }

    heap_scancache_quick_start_with_class_oid (&thread_ref, &scan_cache, &m_class_oid);

    ret = locator_attribute_info_force (&thread_ref, &scan_cache.node.hfid, &oid, &m_attr_info, NULL, 0, LC_FLUSH_INSERT,
					SINGLE_ROW_INSERT, &scan_cache, &force_count, false, REPL_INFO_TYPE_RBR_NORMAL,
					pruning_type, NULL, NULL, NULL, UPDATE_INPLACE_NONE, NULL, false);
    if (ret != NO_ERROR)
      {
	return;
      }

    ret = heap_scancache_end (&thread_ref, &scan_cache);
    if (ret != NO_ERROR)
      {
	return;
      }

    heap_attrinfo_end (&thread_ref, &m_attr_info);
  }

  void
  server_loader::act_finish ()
  {
  }

  void
  server_loader::load_failed_error ()
  {
    //
  }

  void
  server_loader::increment_err_total ()
  {
    //
  }

  void
  server_loader::increment_fails ()
  {
    //
  }
} // namespace cubload
