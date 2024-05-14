/*
 *
 * Copyright 2016 CUBRID Corporation
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 */

#include "method_schema_info.hpp"

#include <algorithm>
#include <cstring>
#include <string>

#include "dbtype_def.h"
#include "dbi.h"
#include "method_query_handler.hpp"
#include "method_query_util.hpp"
#include "trigger_manager.h"
#include "work_space.h"
#include "schema_manager.h"

#include "language_support.h"
#include "deduplicate_key.h"
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace cubmethod
{
  schema_info
  schema_info_handler::get_schema_info (int schema_type, std::string &arg1, std::string &arg2, int flag)
  {
    int error = NO_ERROR;
    schema_info info;
    switch (schema_type)
      {
      case SCH_CLASS:
	error = sch_class_info (info, arg1, flag, 0);
	break;
      case SCH_VCLASS:
	error = sch_class_info (info, arg1, flag, 1);
	break;
      case SCH_QUERY_SPEC:
	error = sch_queryspec (info, arg1);
	break;
      case SCH_ATTRIBUTE:
	error = sch_attr_info (info, arg1, arg2, flag, 0);
	break;
      case SCH_CLASS_ATTRIBUTE:
	error = sch_attr_info (info, arg1, arg2, flag, 1);
	break;
      case SCH_METHOD:
	error = sch_method_info (info, arg1, 0);
	break;
      case SCH_CLASS_METHOD:
	error = sch_method_info (info, arg1, 1);
	break;
      case SCH_METHOD_FILE:
	error = sch_methfile_info (info, arg1);
	break;
      case SCH_SUPERCLASS:
	error = sch_superclass (info, arg1, 1);
	break;
      case SCH_SUBCLASS:
	error = sch_superclass (info, arg1, 0);
	break;
      case SCH_CONSTRAINT:
	error = sch_constraint (info, arg1);
	break;
      case SCH_TRIGGER:
	error = sch_trigger (info, arg1, flag);
	break;
      case SCH_CLASS_PRIVILEGE:
	error = sch_class_priv (info, arg1, flag);
	break;
      case SCH_ATTR_PRIVILEGE:
	error = sch_attr_priv (info, arg1, arg2, flag);
	break;
      case SCH_DIRECT_SUPER_CLASS:
	error = sch_direct_super_class (info, arg1, flag);
	break;
      case SCH_PRIMARY_KEY:
	error = sch_primary_key (info, arg1);
	break;
      case SCH_IMPORTED_KEYS:
	error = sch_imported_keys (info, arg1);
	break;
      case SCH_EXPORTED_KEYS:
	error = sch_exported_keys_or_cross_reference (info, arg1, arg2, false);
	break;
      case SCH_CROSS_REFERENCE:
	error = sch_exported_keys_or_cross_reference (info, arg1, arg2, true);
	break;
      default:
	error = ER_FAILED;
	break;
      }

    if (error == NO_ERROR)
      {
	// TODO: what should I do here?
	info.schema_type = schema_type;
      }
    else
      {
	close_and_free_session ();

	// TODO: proper error code
	m_error_ctx.set_error (METHOD_CALLBACK_ER_SCHEMA_TYPE, NULL, __FILE__, __LINE__);
      }

    return info;
  }

  int
  schema_info_handler::execute_schema_info_query (std::string &sql_stmt)
  {
    lang_set_parser_use_client_charset (false);

    m_session = db_open_buffer (sql_stmt.c_str());
    if (!m_session)
      {
	lang_set_parser_use_client_charset (true);
	m_error_ctx.set_error (db_error_code (), db_error_string (1), __FILE__, __LINE__);
	return ER_FAILED;
      }

    int stmt_id = db_compile_statement (m_session);
    if (stmt_id < 0)
      {
	close_and_free_session ();
	m_error_ctx.set_error (stmt_id, NULL, __FILE__, __LINE__);
	return ER_FAILED;
      }

    DB_QUERY_RESULT *result = NULL;
    int stmt_type = db_get_statement_type (m_session, stmt_id);
    lang_set_parser_use_client_charset (false);
    int num_result = db_execute_statement (m_session, stmt_id, &result);
    lang_set_parser_use_client_charset (true);

    if (num_result < 0)
      {
	close_and_free_session ();
	m_error_ctx.set_error (stmt_id, NULL, __FILE__, __LINE__);
	return ER_FAILED;
      }

    query_result &q_result = m_q_result;
    q_result.stmt_type = stmt_type;
    q_result.stmt_id = stmt_id;
    q_result.tuple_count = num_result;
    q_result.result = result;
    q_result.include_oid = false;

    // TODO: managing structure for schema info queries
    /*
    m_q_result.push_back (q_result);
    m_max_col_size = -1;
    m_current_result_index = 0;
    m_current_result = &m_q_result[m_current_result_index];
    m_has_result_set = true;
    */

    return num_result;
  }

  int
  schema_info_handler::sch_class_info (schema_info &info, std::string &class_name,
				       int pattern_flag, int v_class_flag)
  {
    std::string schema_name;
    std::string class_name_only;
    std::size_t found;

    std::transform (class_name.begin(), class_name.end(), class_name.begin(), ::tolower);

    class_name_only = class_name;
    found = class_name.find ('.');
    if (found != std::string::npos)
      {
	/* If the length is not correct, the username is invalid, so compare the entire class_name. */
	if (found > 0 && found < DB_MAX_SCHEMA_LENGTH)
	  {
	    schema_name = class_name.substr (0, found);
	    class_name_only = class_name.substr (found + 1);
	  }
      }

    // *INDENT-OFF*
    std::string sql = ""
	"SELECT "
	  "CASE "
	    "WHEN is_system_class = 'NO' THEN LOWER (owner_name) || '.' || class_name "
	    "ELSE class_name "
	    "END AS unique_name, "
	  "CAST ( "
	      "CASE "
		"WHEN is_system_class = 'YES' THEN 0 "
		"WHEN class_type = 'CLASS' THEN 2 "
		"ELSE 1 "
		"END "
	      "AS SHORT "
	    "), "
	  "comment "
	"FROM "
	  "db_class "
	"WHERE 1 = 1 ";
    // *INDENT-ON*

    if (v_class_flag)
      {
	sql.append ("AND class_type = 'VCLASS' ");
      }

    if (pattern_flag & CLASS_NAME_PATTERN_MATCH)
      {
	if (!class_name_only.empty())
	  {
	    /* AND class_name LIKE '%s' ESCAPE '%s' */
	    sql.append ("AND class_name LIKE '");
	    sql.append (class_name_only);
	    sql.append ("' ESCAPE '");
	    sql.append (get_backslash_escape_string ());
	    sql.append ("' ");
	  }
      }
    else
      {
	/* AND class_name = '%s' */
	sql.append ("AND class_name = '");
	sql.append (class_name_only);
	sql.append ("' ");
      }

    if (!schema_name.empty())
      {
	/* AND owner_name = UPPER ('%s') */
	sql.append ("AND owner_name = UPPER ('");
	sql.append (schema_name);
	sql.append ("') ");
      }

    int num_result = execute_schema_info_query (sql);
    if (num_result < 0)
      {
	return num_result;
      }

    info.num_result = num_result;
    return NO_ERROR;
  }

  int
  schema_info_handler::sch_attr_info (schema_info &info, std::string &class_name,
				      std::string &attr_name, int pattern_flag,
				      int class_attr_flag)
  {
    std::string schema_name;
    std::string class_name_only;
    std::size_t found;

    std::transform (class_name.begin(), class_name.end(), class_name.begin(), ::tolower);
    std::transform (attr_name.begin(), attr_name.end(), attr_name.begin(), ::tolower);

    class_name_only = class_name;
    found = class_name.find ('.');
    if (found != std::string::npos)
      {
	/* If the length is not correct, the username is invalid, so compare the entire class_name. */
	if (found > 0 && found < DB_MAX_SCHEMA_LENGTH)
	  {
	    schema_name = class_name.substr (0, found);
	    class_name_only = class_name.substr (found + 1);
	  }
      }

    // *INDENT-OFF*
    std::string sql = ""
	"SELECT "
	  "CASE "
	    "WHEN ( "
		"SELECT b.is_system_class "
		"FROM db_class b "
		"WHERE b.class_name = a.class_name AND b.owner_name = a.owner_name "
	      ") = 'NO' THEN LOWER (a.owner_name) || '.' || a.class_name "
	    "ELSE a.class_name "
	    "END AS unique_name, "
	  "a.attr_name "
	"FROM "
	  "db_attribute a "
	"WHERE 1 = 1 ";
    // *INDENT-ON*

    if (class_attr_flag)
      {
	sql.append ("AND a.attr_type = 'CLASS' ");
      }
    else
      {
	sql.append ("AND a.attr_type in {'INSTANCE', 'SHARED'} ");
      }

    if (pattern_flag & CLASS_NAME_PATTERN_MATCH)
      {
	if (!class_name_only.empty())
	  {
	    /* AND class_name LIKE '%s' ESCAPE '%s' */
	    sql.append ("AND a.class_name LIKE '");
	    sql.append (class_name_only);
	    sql.append ("' ESCAPE '");
	    sql.append (get_backslash_escape_string ());
	    sql.append ("' ");
	  }
      }
    else
      {
	/* AND class_name = '%s' */
	sql.append ("AND a.class_name = '");
	sql.append (class_name_only);
	sql.append ("' ");
      }

    if (pattern_flag & ATTR_NAME_PATTERN_MATCH)
      {
	if (!attr_name.empty())
	  {
	    /* AND a.attr_name LIKE '%s' ESCAPE '%s' */
	    sql.append ("AND a.attr_name LIKE '");
	    sql.append (attr_name);
	    sql.append ("' ESCAPE '");
	    sql.append (get_backslash_escape_string ());
	    sql.append ("' ");
	  }
      }
    else
      {
	/* AND a.attr_name = '%s' */
	sql.append ("AND a.class_name = '");
	sql.append (attr_name);
	sql.append ("' ");
      }

    if (!schema_name.empty())
      {
	/* AND owner_name = UPPER ('%s') */
	sql.append ("AND a.owner_name = UPPER ('");
	sql.append (schema_name);
	sql.append ("') ");
      }

    sql.append ("ORDER BY a.class_name, a.def_order ");

    int num_result = execute_schema_info_query (sql);
    if (num_result < 0)
      {
	return num_result;
      }

    info.num_result = num_result;
    return NO_ERROR;
  }

  int
  schema_info_handler::sch_queryspec (schema_info &info, std::string &class_name)
  {
    std::string schema_name;
    std::string class_name_only;
    std::size_t found;

    std::transform (class_name.begin(), class_name.end(), class_name.begin(), ::tolower);

    class_name_only = class_name;
    found = class_name.find ('.');
    if (found != std::string::npos)
      {
	/* If the length is not correct, the username is invalid, so compare the entire class_name. */
	if (found > 0 && found < DB_MAX_SCHEMA_LENGTH)
	  {
	    schema_name = class_name.substr (0, found);
	    class_name_only = class_name.substr (found + 1);
	  }
      }

    std::string sql = "SELECT vclass_def FROM db_vclass WHERE unique_name = '";
    sql.append (class_name_only);
    sql.append ("' ");

    if (!schema_name.empty())
      {
	/* AND owner_name = UPPER ('%s') */
	sql.append ("AND owner_name = UPPER ('");
	sql.append (schema_name);
	sql.append ("') ");
      }

    int num_result = execute_schema_info_query (sql);
    if (num_result < 0)
      {
	return num_result;
      }

    info.num_result = num_result;
    return NO_ERROR;
  }

  int
  schema_info_handler::sch_method_info (schema_info &info, std::string &class_name, int flag)
  {
    DB_OBJECT *class_obj = db_find_class (class_name.c_str());
    DB_METHOD *method_list;
    if (flag)
      {
	method_list = db_get_class_methods (class_obj);
      }
    else
      {
	method_list = db_get_methods (class_obj);
      }

    int num_method = 0;
    for (DB_METHOD *method = method_list; method; method = db_method_next (method))
      {
	num_method++;
      }

    info.num_result = num_method;
    m_method_list = method_list;
    return NO_ERROR;
  }

  int
  schema_info_handler::sch_methfile_info (schema_info &info, std::string &class_name)
  {
    DB_OBJECT *class_obj = db_find_class (class_name.c_str());
    DB_METHFILE *method_files = db_get_method_files (class_obj);

    int num_mf= 0;
    for (DB_METHFILE *mf = method_files; mf; mf = db_methfile_next (mf))
      {
	num_mf++;
      }
    info.num_result = num_mf;
    m_method_files = method_files;
    return NO_ERROR;
  }

  int
  schema_info_handler::sch_superclass (schema_info &info, std::string &class_name, int flag)
  {
    int error = NO_ERROR;
    DB_OBJECT *class_obj = db_find_class (class_name.c_str());
    DB_OBJLIST *obj_list = NULL;
    if (flag)
      {
	obj_list = db_get_superclasses (class_obj);
      }
    else
      {
	obj_list = db_get_subclasses (class_obj);
      }

    class_table ct;
    for (DB_OBJLIST *tmp = obj_list; tmp; tmp = tmp->next)
      {
	char *p = (char *) db_get_class_name (tmp->op);
	int cls_type = class_type (tmp->op);
	if (cls_type < 0)
	  {
	    error = cls_type;
	    break;
	  }

	ct.class_name.assign (p);
	ct.class_type = cls_type;
	m_obj_list.push_back (ct);
      }

    db_objlist_free (obj_list);
    info.num_result = m_obj_list.size ();
    return error;
  }

  int
  schema_info_handler::sch_constraint (schema_info &info, std::string &class_name)
  {
    DB_OBJECT *class_obj = db_find_class (class_name.c_str ());
    DB_CONSTRAINT *constraint = db_get_constraints (class_obj);
    int num_const = 0;
    for (DB_CONSTRAINT *tmp_c = constraint; tmp_c; tmp_c = db_constraint_next (tmp_c))
      {
	int type = db_constraint_type (tmp_c);
	switch (type)
	  {
	  case DB_CONSTRAINT_UNIQUE:
	  case DB_CONSTRAINT_INDEX:
	  case DB_CONSTRAINT_REVERSE_UNIQUE:
	  case DB_CONSTRAINT_REVERSE_INDEX:
	  {
	    DB_ATTRIBUTE **attr = db_constraint_attributes (tmp_c);
	    for (int i = 0; attr[i]; i++)
	      {
		num_const++;
	      }
	  }
	  default:
	    break;
	  }
      }
    info.num_result = num_const;
    m_constraint = constraint;
    return NO_ERROR;
  }

  int
  schema_info_handler::sch_trigger (schema_info &info, std::string &class_name, int flag)
  {
    int error = NO_ERROR;
    bool is_pattern_match = (flag & CLASS_NAME_PATTERN_MATCH) ? true : false;

    info.num_result = 0;
    if (class_name.empty () && !is_pattern_match)
      {
	return NO_ERROR;
      }

    DB_OBJLIST *tmp_trigger = NULL;
    if (db_find_all_triggers (&tmp_trigger) < 0)
      {
	return NO_ERROR;
      }

    int num_trig = 0;
    DB_OBJLIST *all_trigger = NULL;
    if (class_name.empty ())
      {
	all_trigger = tmp_trigger;
	num_trig = db_list_length ((DB_LIST *) all_trigger);
      }
    else
      {
	std::string schema_name;
	DB_OBJECT *owner = NULL;

	std::string class_name_only = class_name;
	std::size_t found = class_name.find ('.');
	if (found != std::string::npos)
	  {
	    /* If the length is not correct, the username is invalid, so compare the entire class_name. */
	    if (found > 0 && found < DB_MAX_SCHEMA_LENGTH)
	      {
		schema_name = class_name.substr (0, found);

		/* If the user does not exist, compare the entire class_name. */
		owner = db_find_user (schema_name.c_str ());
		if (owner != NULL)
		  {
		    class_name_only = class_name.substr (found + 1);
		  }
	      }
	  }

	DB_OBJLIST *tmp = NULL;
	for (tmp = tmp_trigger; tmp; tmp = tmp->next)
	  {
	    MOP tmp_obj = tmp->op;
	    assert (tmp_obj != NULL);

	    TR_TRIGGER *trigger = tr_map_trigger (tmp_obj, 1);
	    if (trigger == NULL)
	      {
		assert (er_errid () != NO_ERROR);
		error = er_errid ();
		break;
	      }

	    DB_OBJECT *obj_trigger_target = trigger->class_mop;
	    assert (obj_trigger_target != NULL);

	    const char *name_trigger_target = sm_get_ch_name (obj_trigger_target);
	    if (name_trigger_target == NULL)
	      {
		assert (er_errid () != NO_ERROR);
		error = er_errid ();
		break;
	      }

	    const char *only_name_trigger_target = name_trigger_target;
	    /* If the user does not exist, compare the entire class_name. */
	    if (owner)
	      {
		only_name_trigger_target = strchr (name_trigger_target, '.');
		if (only_name_trigger_target)
		  {
		    only_name_trigger_target = only_name_trigger_target + 1;
		  }
		else
		  {
		    assert (false);
		  }

		/* If the owner is different from the specified owner, skip it. */
		if (db_get_owner (tmp_obj) != owner)
		  {
		    continue;
		  }
	      }

	    if (is_pattern_match)
	      {
		if (str_like (std::string (name_trigger_target), class_name_only.c_str (), '\\') == 1)
		  {
		    error = ml_ext_add (&all_trigger, tmp_obj, NULL);
		    if (error != NO_ERROR)
		      {
			break;
		      }
		    num_trig++;
		  }
	      }
	    else
	      {
		if (strcmp (class_name_only.c_str (), name_trigger_target) == 0)
		  {
		    error = ml_ext_add (&all_trigger, tmp_obj, NULL);
		    if (error != NO_ERROR)
		      {
			break;
		      }
		    num_trig++;
		  }
	      }
	  }

	if (tmp_trigger)
	  {
	    ml_ext_free (tmp_trigger);
	  }

	if (error != NO_ERROR && all_trigger)
	  {
	    ml_ext_free (all_trigger);
	    all_trigger = NULL;
	    num_trig = 0;
	  }
      }

    info.num_result = num_trig;
    m_all_trigger = all_trigger;
    return error;
  }

  int
  schema_info_handler::sch_class_priv (schema_info &info, std::string &class_name, int pat_flag)
  {
    int num_tuple = 0;
    unsigned int class_priv;

    if ((pat_flag & CLASS_NAME_PATTERN_MATCH) == 0)
      {
	if (!class_name.empty())
	  {
	    DB_OBJECT *class_obj = db_find_class (class_name.c_str ());
	    if (class_obj != NULL)
	      {
		if (db_get_class_privilege (class_obj, &class_priv) >= 0)
		  {
		    num_tuple = set_priv_table (m_priv_tbl, 0, (char *) db_get_class_name (class_obj), class_priv);
		  }
	      }
	  }
      }
    else
      {
	std::string schema_name;
	DB_OBJECT *owner = NULL;

	std::string class_name_only = class_name;
	std::size_t found = class_name.find ('.');
	if (found != std::string::npos)
	  {
	    /* If the length is not correct, the username is invalid, so compare the entire class_name. */
	    if (found > 0 && found < DB_MAX_SCHEMA_LENGTH)
	      {
		schema_name = class_name.substr (0, found);

		/* If the user does not exist, compare the entire class_name. */
		owner = db_find_user (schema_name.c_str ());
		if (owner != NULL)
		  {
		    class_name_only = class_name.substr (found + 1);
		  }
	      }
	  }

	DB_OBJLIST *obj_list = db_get_all_classes ();
	for (DB_OBJLIST *tmp = obj_list; tmp; tmp = tmp->next)
	  {
	    char *p = (char *) db_get_class_name (tmp->op);
	    char *q = p;
	    /* If the user does not exist, compare the entire class_name. */
	    if (owner && db_is_system_class (tmp->op) == FALSE)
	      {
		/* p: unique_name, q: class_name */
		q = strchr (p, '.');
		if (q)
		  {
		    q = q + 1;
		  }
		else
		  {
		    assert (false);
		  }

		/* If the owner is different from the specified owner, skip it. */
		if (db_get_owner (tmp->op) != owner)
		  {
		    continue;
		  }
	      }
	    if (!class_name.empty() && str_like (std::string (q), class_name.c_str(), '\\') < 1)
	      {
		continue;
	      }

	    if (db_get_class_privilege (tmp->op, &class_priv) >= 0)
	      {
		num_tuple += set_priv_table (m_priv_tbl, num_tuple, (char *) db_get_class_name (tmp->op), class_priv);
	      }
	  }
	db_objlist_free (obj_list);
      }

    info.num_result = num_tuple;
    return NO_ERROR;
  }

  int
  schema_info_handler::sch_attr_priv (schema_info &info, std::string &class_name,
				      std::string &attr_name_pat,
				      int pat_flag)
  {
    int num_tuple = 0;

    DB_OBJECT *class_obj = db_find_class (class_name.c_str ());
    if (class_obj == NULL)
      {
	info.num_result = num_tuple;
	return NO_ERROR;
      }

    unsigned int class_priv;
    if (db_get_class_privilege (class_obj, &class_priv) >= 0)
      {
	DB_ATTRIBUTE *attributes = db_get_attributes (class_obj);
	for (DB_ATTRIBUTE *attr = attributes; attr; attr = db_attribute_next (attr))
	  {
	    char *attr_name = (char *) db_attribute_name (attr);
	    if (pat_flag & ATTR_NAME_PATTERN_MATCH)
	      {
		if (attr_name_pat.empty() == false && str_like (std::string (attr_name), attr_name_pat.c_str (), '\\') < 1)
		  {
		    continue;
		  }
	      }
	    else
	      {
		if (attr_name_pat.empty() == true || strcmp (attr_name, attr_name_pat.c_str ()) != 0)
		  {
		    continue;
		  }
	      }

	    num_tuple += set_priv_table (m_priv_tbl, num_tuple, (char *) db_get_class_name (class_obj), class_priv);
	  }
      }

    info.num_result = num_tuple;
    return NO_ERROR;
  }

  int
  schema_info_handler::sch_direct_super_class (schema_info &info, std::string &class_name,
      int pattern_flag)
  {
    std::string schema_name;
    std::string class_name_only;
    std::size_t found;

    std::transform (class_name.begin(), class_name.end(), class_name.begin(), ::tolower);

    class_name_only = class_name;
    found = class_name.find ('.');
    if (found != std::string::npos)
      {
	/* If the length is not correct, the username is invalid, so compare the entire class_name. */
	if (found > 0 && found < DB_MAX_SCHEMA_LENGTH)
	  {
	    schema_name = class_name.substr (0, found);
	    class_name_only = class_name.substr (found + 1);
	  }
      }

    // *INDENT-OFF*
    std::string sql = ""
	"SELECT "
	  "CASE "
	    "WHEN ( "
		"SELECT b.is_system_class "
		"FROM db_class b "
		"WHERE b.class_name = a.class_name AND b.owner_name = a.owner_name "
	      ") = 'NO' THEN LOWER (a.owner_name) || '.' || a.class_name "
	    "ELSE a.class_name "
	    "END AS unique_name, "
	  "CASE "
	    "WHEN ( "
		"SELECT b.is_system_class "
		"FROM db_class b "
		"WHERE b.class_name = a.super_class_name AND b.owner_name = a.super_owner_name "
	      ") = 'NO' THEN LOWER (a.super_owner_name) || '.' || a.super_class_name "
	    "ELSE a.super_class_name "
	    "END AS super_unique_name "
	"FROM "
	  "db_direct_super_class a "
	"WHERE 1 = 1 ";
    // *INDENT-ON*

    if (pattern_flag & CLASS_NAME_PATTERN_MATCH)
      {
	if (!class_name_only.empty())
	  {
	    /* AND class_name LIKE '%s' ESCAPE '%s' */
	    sql.append ("AND class_name LIKE '");
	    sql.append (class_name_only);
	    sql.append ("' ESCAPE '");
	    sql.append (get_backslash_escape_string ());
	    sql.append ("' ");
	  }
      }
    else
      {
	/* AND class_name = '%s' */
	sql.append ("AND class_name = '");
	sql.append (class_name_only);
	sql.append ("' ");
      }

    if (!schema_name.empty())
      {
	/* AND owner_name = UPPER ('%s') */
	sql.append ("AND owner_name = UPPER ('");
	sql.append (schema_name);
	sql.append ("') ");
      }

    int num_result = execute_schema_info_query (sql);
    if (num_result < 0)
      {
	return num_result;
      }

    info.num_result = num_result;
    return NO_ERROR;
  }

  int schema_info_handler::sch_primary_key (schema_info &info, std::string &class_name)
  {
    std::string schema_name;
    std::string class_name_only;
    std::size_t found;
    int num_result = 0;
    int i;

    std::transform (class_name.begin(), class_name.end(), class_name.begin(), ::tolower);

    class_name_only = class_name;
    found = class_name.find ('.');
    if (found != std::string::npos)
      {
	/* If the length is not correct, the username is invalid, so compare the entire class_name. */
	if (found > 0 && found < DB_MAX_SCHEMA_LENGTH)
	  {
	    schema_name = class_name.substr (0, found);
	    class_name_only = class_name.substr (found + 1);
	  }
      }

    DB_OBJECT *class_object = db_find_class (class_name.c_str ());
    if (class_object != NULL)
      {
	// *INDENT-OFF*
	std::string sql = ""
		"SELECT "
		  "CASE "
		    "WHEN ( "
			"SELECT c.is_system_class "
			"FROM db_class c "
			"WHERE c.class_name = a.class_name AND c.owner_name = a.owner_name "
		      ") = 'NO' THEN LOWER (a.owner_name) || '.' || a.class_name "
		    "ELSE a.class_name "
		    "END AS unique_name, "
		  "b.key_attr_name, "
		  "b.key_order + 1, "
		  "a.index_name "
		"FROM "
		  "db_index a, "
		  "db_index_key b "
		"WHERE "
		  "a.index_name = b.index_name "
		  "AND a.class_name = b.class_name "
		  "AMD a.owner_name = b.owner_name "
		  "AND a.is_primary_key = 'YES' "
		  "AND a.class_name = '";
	sql.append (class_name_only);
	sql.append ("' ");
	// *INDENT-ON*

	if (!schema_name.empty())
	  {
	    /* AND owner_name = UPPER ('%s') */
	    sql.append ("AND owner_name = UPPER ('");
	    sql.append (schema_name);
	    sql.append ("') ");
	  }

	sql.append ("ORDER BY b.key_attr_name");

	num_result = execute_schema_info_query (sql);
	if (num_result < 0)
	  {
	    return num_result;
	  }
      }

    info.num_result = num_result;
    return NO_ERROR;
  }

  int schema_info_handler::sch_imported_keys (schema_info &info, std::string &fktable_name)
  {
    int error = NO_ERROR;
    int num_fk_info = 0, i = 0;
    int fk_i;
    DB_OBJECT *fktable_obj = db_find_class (fktable_name.c_str ());
    if (fktable_obj == NULL)
      {
	/* The followings are possible situations.  - A table matching fktable_name does not exist.  - User has no
	 * authorization on the table.  - Other error we do not expect. In these cases, we will send an empty result. And
	 * this rule is also applied to CCI_SCH_EXPORTED_KEYS and CCI_SCH_CROSS_REFERENCE. */
	info.num_result = num_fk_info;
	return NO_ERROR;
      }

    for (DB_CONSTRAINT *fk_const = db_get_constraints (fktable_obj); fk_const != NULL;
	 fk_const = db_constraint_next (fk_const))
      {
	DB_CONSTRAINT_TYPE type = db_constraint_type (fk_const);
	if (type != DB_CONSTRAINT_FOREIGN_KEY)
	  {
	    continue;
	  }
	SM_FOREIGN_KEY_INFO *fk_info = fk_const->fk_info;

	/* Find referenced table to get table name and columns. */
	DB_OBJECT *pktable_obj = db_get_foreign_key_ref_class (fk_const);
	if (pktable_obj == NULL)
	  {
	    m_error_ctx.set_error (db_error_code (), db_error_string (1), __FILE__, __LINE__);
	    return ER_FAILED;
	  }

	const char *pktable_name = db_get_class_name (pktable_obj);
	if (pktable_name == NULL)
	  {
	    m_error_ctx.set_error (db_error_code (), db_error_string (1), __FILE__, __LINE__);
	    return ER_FAILED;
	  }

	DB_CONSTRAINT *pktable_cons = db_get_constraints (pktable_obj);

	error = db_error_code ();
	if (error != NO_ERROR)
	  {
	    m_error_ctx.set_error (db_error_code (), db_error_string (1), __FILE__, __LINE__);
	    return ER_FAILED;
	  }

	DB_CONSTRAINT *pk = db_constraint_find_primary_key (pktable_cons);
	if (pk == NULL)
	  {
	    m_error_ctx.set_error (ER_FK_REF_CLASS_HAS_NOT_PK, "Referenced class has no primary key.", __FILE__, __LINE__);
	    return ER_FAILED;
	  }

	const char *pk_name = db_constraint_name (pk);
	DB_ATTRIBUTE **pk_attr = db_constraint_attributes (pk);
	if (pk_attr == NULL)
	  {
	    m_error_ctx.set_error (ER_SM_INVALID_CONSTRAINT, "Primary key has no attribute.", __FILE__, __LINE__);
	    return ER_FAILED;
	  }

	DB_ATTRIBUTE **fk_attr = db_constraint_attributes (fk_const);
	if (fk_attr == NULL)
	  {
	    m_error_ctx.set_error (ER_SM_INVALID_CONSTRAINT, "Foreign key has no attribute.", __FILE__, __LINE__);
	    return ER_FAILED;
	  }

	for (i = 0; pk_attr[i] != NULL && fk_attr[i] != NULL; i++)
	  {
	    // TODO: not implemented yet
	    /*
	      fk_res =
	        add_fk_info_result (fk_res, pktable_name, db_attribute_name (pk_attr[i]), fktable_name,
	    			db_attribute_name (fk_attr[i]), (short) i + 1, fk_info->update_action,
	    			fk_info->delete_action, fk_info->name, pk_name, FK_INFO_SORT_BY_PKTABLE_NAME);
	      if (fk_res == NULL)
	    m_error_ctx.set_error (METHOD_CALLBACK_ER_NO_MORE_MEMORY, NULL, __FILE__, __LINE__);
	        {
	    return ER_FAILED;
	        }
	    */

	    num_fk_info++;
	  }

	/* pk_attr and fk_attr is null-terminated array. So, they should be null at this time. If one of them is not
	 * null, it means that they have different number of attributes. */
	fk_i = (fk_attr[i] && IS_DEDUPLICATE_KEY_ATTR_ID (fk_attr[i]->id)) ? (i + 1) : i;
	assert (pk_attr[i] == NULL && fk_attr[fk_i] == NULL);
	if (pk_attr[i] != NULL || fk_attr[fk_i] != NULL)
	  {
	    m_error_ctx.set_error (ER_FK_NOT_MATCH_KEY_COUNT,
				   "The number of keys of the foreign key is different from that of the primary key.", __FILE__, __LINE__);
	    return ER_FAILED;
	  }
      }

    info.num_result = num_fk_info;
    return NO_ERROR;
  }

  int schema_info_handler::sch_exported_keys_or_cross_reference (schema_info &info,
      std::string &pktable_name,
      std::string &fktable_name, bool find_cross_ref)
  {
    // TODO
    return NO_ERROR;
  }

  int
  schema_info_handler::class_type (DB_OBJECT *class_obj)
  {
    int error = db_is_system_class (class_obj);

    if (error < 0)
      {
	m_error_ctx.set_error (error, NULL, __FILE__, __LINE__);
	return ER_FAILED;
      }
    if (error > 0)
      {
	return 0;
      }

    error = db_is_vclass (class_obj);
    if (error < 0)
      {
	m_error_ctx.set_error (error, NULL, __FILE__, __LINE__);
	return ER_FAILED;
      }
    if (error > 0)
      {
	return 1;
      }

    return 2;
  }

  int
  schema_info_handler::set_priv_table (std::vector<priv_table> &pts, int index, char *name, unsigned int class_priv)
  {
    int grant_opt = class_priv >> 8;
    int priv_type = 1;
    int num_tuple = 0;
    for (int i = 0; i < 7; i++)
      {
	if (class_priv & priv_type)
	  {
	    priv_table pt;
	    pt.class_name.assign (name);
	    pt.priv = priv_type;

	    if (grant_opt & priv_type)
	      {
		pt.grant = 1;
	      }
	    else
	      {
		pt.grant = 0;
	      }

	    pts.push_back (pt);
	    num_tuple++;
	  }
	priv_type <<= 1;
      }

    return num_tuple;
  }

  void
  schema_info_handler::close_and_free_session ()
  {
    if (m_session)
      {
	db_close_session ((DB_SESSION *) (m_session));
      }
    m_session = NULL;
  }

  std::vector<column_info>
  get_schema_table_meta ()
  {
    column_info name (DB_TYPE_STRING, DB_TYPE_NULL, 0, DB_MAX_IDENTIFIER_LENGTH, lang_charset(), "NAME");
    column_info type (DB_TYPE_SHORT, DB_TYPE_NULL, 0, 0, lang_charset(), "TYPE");
    column_info remarks (DB_TYPE_STRING, DB_TYPE_NULL, 0, DB_MAX_CLASS_COMMENT_LENGTH, lang_charset(), "REMARKS");
    return std::vector<column_info> {name, type, remarks};
  }

  std::vector<column_info>
  get_schema_query_spec_meta ()
  {
    column_info query_spec (DB_TYPE_STRING, DB_TYPE_NULL, 0, DB_MAX_IDENTIFIER_LENGTH, lang_charset(), "QUERY_SPEC");
    return std::vector<column_info> {query_spec};
  }

  std::vector<column_info>
  get_schema_attr_meta ()
  {
    column_info attr_name (DB_TYPE_STRING, DB_TYPE_NULL, 0, DB_MAX_IDENTIFIER_LENGTH, lang_charset(), "ATTR_NAME");
    column_info domain (DB_TYPE_SHORT, DB_TYPE_NULL, 0, 0, lang_charset(), "DOMAIN");
    column_info scale (DB_TYPE_SHORT, DB_TYPE_NULL, 0, 0, lang_charset(), "SCALE");
    column_info precision (DB_TYPE_INTEGER, DB_TYPE_NULL, 0, 0, lang_charset(), "PRECISION");

    column_info indexed (DB_TYPE_SHORT, DB_TYPE_NULL, 0, 0, lang_charset(), "INDEXED");
    column_info non_null (DB_TYPE_SHORT, DB_TYPE_NULL, 0, 0, lang_charset(), "NON_NULL");
    column_info shared (DB_TYPE_SHORT, DB_TYPE_NULL, 0, 0, lang_charset(), "SHARED");
    column_info unique (DB_TYPE_SHORT, DB_TYPE_NULL, 0, 0, lang_charset(), "UNIQUE");

    column_info default_ (DB_TYPE_STRING, DB_TYPE_NULL, 0, DB_MAX_IDENTIFIER_LENGTH, lang_charset(), "DEFAULT");
    column_info attr_order (DB_TYPE_INTEGER, DB_TYPE_NULL, 0, 0, lang_charset(), "ATTR_ORDER");

    column_info class_name (DB_TYPE_STRING, DB_TYPE_NULL, 0, DB_MAX_IDENTIFIER_LENGTH, lang_charset(), "CLASS_NAME");
    column_info source_class (DB_TYPE_STRING, DB_TYPE_NULL, 0, DB_MAX_IDENTIFIER_LENGTH, lang_charset(), "SOURCE_CLASS");
    column_info is_key (DB_TYPE_SHORT, DB_TYPE_NULL, 0, 0, lang_charset(), "IS_KEY");
    column_info remarks (DB_TYPE_STRING, DB_TYPE_NULL, 0, DB_MAX_CLASS_COMMENT_LENGTH, lang_charset(), "REMARKS");
    return std::vector<column_info> {attr_name, domain, scale, precision, indexed, non_null, shared, unique, default_, attr_order, class_name, source_class, is_key, remarks};
  }

  std::vector<column_info>
  get_schema_method_meta ()
  {
    column_info name (DB_TYPE_STRING, DB_TYPE_NULL, 0, DB_MAX_IDENTIFIER_LENGTH, lang_charset(), "NAME");
    column_info ret_domain (DB_TYPE_SHORT, DB_TYPE_NULL, 0, 0, lang_charset(), "RET_DOMAIN");
    column_info arg_domain (DB_TYPE_STRING, DB_TYPE_NULL, 0, DB_MAX_IDENTIFIER_LENGTH, lang_charset(), "ARG_DOMAIN");
    return std::vector<column_info> {name, ret_domain, arg_domain};
  }

  std::vector<column_info>
  get_schema_methodfile_meta ()
  {
    column_info met_file (DB_TYPE_STRING, DB_TYPE_NULL, 0, DB_MAX_IDENTIFIER_LENGTH, lang_charset(), "METHOD_FILE");
    return std::vector<column_info> {met_file};
  }

  std::vector<column_info>
  get_schema_superclasss_meta ()
  {
    column_info class_name (DB_TYPE_STRING, DB_TYPE_NULL, 0, DB_MAX_IDENTIFIER_LENGTH, lang_charset(), "CLASS_NAME");
    column_info type (DB_TYPE_SHORT, DB_TYPE_NULL, 0, 0, lang_charset(), "TYPE");
    return std::vector<column_info> {class_name, type};
  }

  std::vector<column_info>
  get_schema_constraint_meta ()
  {
    column_info type (DB_TYPE_SHORT, DB_TYPE_NULL, 0, 0, lang_charset(), "TYPE");
    column_info name (DB_TYPE_STRING, DB_TYPE_NULL, 0, DB_MAX_IDENTIFIER_LENGTH, lang_charset(), "NAME");
    column_info attr_name (DB_TYPE_STRING, DB_TYPE_NULL, 0, DB_MAX_IDENTIFIER_LENGTH, lang_charset(), "ATTR_NAME");
    column_info num_pages (DB_TYPE_INTEGER, DB_TYPE_NULL, 0, 0, lang_charset(), "NUM_PAGES");
    column_info num_keys (DB_TYPE_INTEGER, DB_TYPE_NULL, 0, 0, lang_charset(), "NUM_KEYS");
    column_info primary_key (DB_TYPE_SHORT, DB_TYPE_NULL, 0, 0, lang_charset(), "PRIMARY_KEY");
    column_info key_order (DB_TYPE_SHORT, DB_TYPE_NULL, 0, 0, lang_charset(), "KEY_ORDER");
    column_info asc_desc (DB_TYPE_STRING, DB_TYPE_NULL, 0, DB_MAX_IDENTIFIER_LENGTH, lang_charset(), "ASC_DESC");
    return std::vector<column_info> {type, name, attr_name, num_pages, num_keys, primary_key, key_order, asc_desc};
  }

  std::vector<column_info>
  get_schema_trigger_meta ()
  {
    column_info name (DB_TYPE_STRING, DB_TYPE_NULL, 0, DB_MAX_IDENTIFIER_LENGTH, lang_charset(), "NAME");
    column_info status (DB_TYPE_STRING, DB_TYPE_NULL, 0, DB_MAX_IDENTIFIER_LENGTH, lang_charset(), "STATUS");
    column_info event (DB_TYPE_STRING, DB_TYPE_NULL, 0, DB_MAX_IDENTIFIER_LENGTH, lang_charset(), "EVENT");
    column_info target_class (DB_TYPE_STRING, DB_TYPE_NULL, 0, DB_MAX_IDENTIFIER_LENGTH, lang_charset(), "TARGET_CLASS");
    column_info target_attr (DB_TYPE_STRING, DB_TYPE_NULL, 0, DB_MAX_IDENTIFIER_LENGTH, lang_charset(), "TARGET_ATTR");
    column_info action_time (DB_TYPE_STRING, DB_TYPE_NULL, 0, DB_MAX_IDENTIFIER_LENGTH, lang_charset(), "ACTION_TIME");
    column_info action (DB_TYPE_STRING, DB_TYPE_NULL, 0, DB_MAX_IDENTIFIER_LENGTH, lang_charset(), "ACTION");
    column_info priority (DB_TYPE_FLOAT, DB_TYPE_NULL, 0, 0, lang_charset(), "PRIORITY");
    column_info condition_time (DB_TYPE_STRING, DB_TYPE_NULL, 0, DB_MAX_IDENTIFIER_LENGTH, lang_charset(),
				"CONDITION_TIME");
    column_info condition (DB_TYPE_STRING, DB_TYPE_NULL, 0, DB_MAX_IDENTIFIER_LENGTH, lang_charset(), "CONDITION");
    column_info remarks (DB_TYPE_STRING, DB_TYPE_NULL, 0, DB_MAX_CLASS_COMMENT_LENGTH, lang_charset(), "REMARKS");
    return std::vector<column_info> {name, status, event, target_class, target_attr, action_time, action, priority, condition_time, condition, remarks};

  }
  std::vector<column_info>
  get_schema_classpriv_meta ()
  {
    column_info class_name (DB_TYPE_STRING, DB_TYPE_NULL, 0, DB_MAX_IDENTIFIER_LENGTH, lang_charset(), "CLASS_NAME");
    column_info privilege (DB_TYPE_STRING, DB_TYPE_NULL, 0, 10, lang_charset(), "PRIVILEGE");
    column_info grantable (DB_TYPE_STRING, DB_TYPE_NULL, 0, 5, lang_charset(), "GRANTABLE");
    return std::vector<column_info> {class_name, privilege, grantable};
  }

  std::vector<column_info>
  get_schema_attrpriv_meta ()
  {
    column_info attr_name (DB_TYPE_STRING, DB_TYPE_NULL, 0, DB_MAX_IDENTIFIER_LENGTH, lang_charset(), "ATTR_NAME");
    column_info privilege (DB_TYPE_STRING, DB_TYPE_NULL, 0, 10, lang_charset(), "PRIVILEGE");
    column_info grantable (DB_TYPE_STRING, DB_TYPE_NULL, 0, 5, lang_charset(), "GRANTABLE");
    return std::vector<column_info> {attr_name, privilege, grantable};
  }

  std::vector<column_info>
  get_schema_directsuper_meta ()
  {
    column_info class_name (DB_TYPE_STRING, DB_TYPE_NULL, 0, DB_MAX_IDENTIFIER_LENGTH, lang_charset(), "CLASS_NAME");
    column_info super_class_name (DB_TYPE_STRING, DB_TYPE_NULL, 0, DB_MAX_IDENTIFIER_LENGTH, lang_charset(),
				  "SUPER_CLASS_NAME");
    return std::vector<column_info> {class_name, super_class_name};
  }

  std::vector<column_info>
  get_schema_primarykey_meta ()
  {
    column_info class_name (DB_TYPE_STRING, DB_TYPE_NULL, 0, DB_MAX_IDENTIFIER_LENGTH, lang_charset(), "CLASS_NAME");
    column_info attr_name (DB_TYPE_STRING, DB_TYPE_NULL, 0, DB_MAX_IDENTIFIER_LENGTH, lang_charset(), "ATTR_NAME");
    column_info num_keys (DB_TYPE_INTEGER, DB_TYPE_NULL, 0, 0, lang_charset(), "KEY_SEQ");
    column_info key_name (DB_TYPE_STRING, DB_TYPE_NULL, 0, DB_MAX_IDENTIFIER_LENGTH, lang_charset(), "KEY_NAME");
    return std::vector<column_info> {class_name, attr_name, num_keys, key_name};
  }

  std::vector<column_info>
  get_schema_fk_info_meta ()
  {
    column_info pktable_name (DB_TYPE_STRING, DB_TYPE_NULL, 0, DB_MAX_IDENTIFIER_LENGTH, lang_charset(), "PKTABLE_NAME");
    column_info pkcolumn_name (DB_TYPE_STRING, DB_TYPE_NULL, 0, DB_MAX_IDENTIFIER_LENGTH, lang_charset(), "PKCOLUMN_NAME");
    column_info fktable_name (DB_TYPE_STRING, DB_TYPE_NULL, 0, DB_MAX_IDENTIFIER_LENGTH, lang_charset(), "FKTABLE_NAME");
    column_info fkcolumn_name (DB_TYPE_STRING, DB_TYPE_NULL, 0, DB_MAX_IDENTIFIER_LENGTH, lang_charset(), "FKCOLUMN_NAME");
    column_info key_seq (DB_TYPE_SHORT, DB_TYPE_NULL, 0, 0, lang_charset(), "KEY_SEQ");
    column_info update_rule (DB_TYPE_SHORT, DB_TYPE_NULL, 0, 0, lang_charset(), "UPDATE_RULE");
    column_info delete_rule (DB_TYPE_SHORT, DB_TYPE_NULL, 0, 0, lang_charset(), "DELETE_RULE");
    column_info fk_name (DB_TYPE_STRING, DB_TYPE_NULL, 0, DB_MAX_IDENTIFIER_LENGTH, lang_charset(), "FK_NAME");
    column_info pk_name (DB_TYPE_STRING, DB_TYPE_NULL, 0, DB_MAX_IDENTIFIER_LENGTH, lang_charset(), "PK_NAME");
    return std::vector<column_info> {pktable_name, pkcolumn_name, fktable_name, fkcolumn_name, key_seq, update_rule, delete_rule, fk_name, pk_name};
  }
}