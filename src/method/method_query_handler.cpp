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

#include "method_query_handler.hpp"

#include "db.h"
#include "dbi.h"
#include "dbtype.h"
#include "method_query_util.hpp"
#include "method_schema_info.hpp"
#include "object_primitive.h"
#include "optimizer.h" /* qo_get_optimization_param, qo_set_optimization_param */

/* from jsp_cl.c */
extern void jsp_set_prepare_call ();
extern void jsp_unset_prepare_call ();

namespace cubmethod
{
  query_handler::query_handler (error_context &ctx, int id)
    : m_id (id)
    , m_error_ctx (ctx)
    , m_sql_stmt ()
    , m_stmt_type (CUBRID_STMT_NONE)
    , m_prepare_flag (0x00)
    , m_session (nullptr)
    , m_is_prepared (false)
    , m_use_plan_cache (false)
    , m_num_markers (-1)
    , m_max_col_size (-1)
    , m_has_result_set (false)
    , m_is_occupied (false)
    , m_query_id (-1)
    , m_prepare_info ()
    , m_execute_info ()
    , m_prepare_call_info ()
    , m_query_result ()
  {
    //
  }

  query_handler::~query_handler ()
  {
    end_qresult ();
    close_and_free_session ();
  }

  /* called after 1 iteration on method scan */
  void query_handler::reset ()
  {
    end_qresult ();
    m_is_occupied = false;
  }

  query_result::query_result ()
  {
    column_info = nullptr;
    result = nullptr;

    stmt_id = 0;
    stmt_type = 0;
    num_column = 0;
    tuple_count = 0;
    copied = false;
    include_oid = false;
  }

  int
  query_handler::get_id () const
  {
    return m_id;
  }

  std::string
  query_handler::get_sql_stmt () const
  {
    return m_sql_stmt;
  }

  int
  query_handler::get_statement_type () const
  {
    return m_stmt_type;
  }

  uint64_t
  query_handler::get_query_id () const
  {
    return m_query_id;
  }

  int
  query_handler::get_num_markers ()
  {
    const std::string &sql = m_sql_stmt;
    if (m_num_markers == -1 && !sql.empty ())
      {
	m_num_markers = calculate_num_markers (sql);
      }
    return m_num_markers;
  }

  bool query_handler::is_prepared () const
  {
    return m_is_prepared;
  }

  bool query_handler::get_is_occupied ()
  {
    return m_is_occupied;
  }

  void query_handler::set_is_occupied (bool flag)
  {
    m_is_occupied = flag;
  }

  prepare_info &
  query_handler::get_prepare_info ()
  {
    return m_prepare_info;
  }

  execute_info &
  query_handler::get_execute_info ()
  {
    return m_execute_info;
  }

  DB_SESSION *
  query_handler::get_db_session ()
  {
    return m_session;
  }

  DB_QUERY_TYPE *
  query_handler::get_column_info ()
  {
    if (m_session)
      {
	return db_get_query_type_list (m_session, m_query_result.stmt_id);
      }
    return nullptr;
  }

  const query_result &
  query_handler::get_result ()
  {
    return m_query_result;
  }

  int
  query_handler::prepare (std::string sql, int flag)
  {
    m_sql_stmt.assign (sql);
    m_prepare_flag = flag;

    int error = NO_ERROR;
    if (m_prepare_flag & PREPARE_CALL)
      {
	error = prepare_call ();
      }
    else
      {
	error = prepare_query ();
      }

    if (error == NO_ERROR)
      {
	m_prepare_info.handle_id = get_id ();
	m_prepare_info.stmt_type = m_query_result.stmt_type;
	m_prepare_info.num_markers = get_num_markers ();
	set_prepare_column_list_info (m_prepare_info.column_infos);

	m_is_occupied = true;
      }
    else
      {
	// error handling
	close_and_free_session ();
      }

    return error;
  }

  int
  query_handler::prepare_retry ()
  {
    if (is_prepared ())
      {
	return prepare (m_sql_stmt, m_prepare_flag);
      }
    return ER_FAILED;
  }

  int
  query_handler::prepare_compile (const std::string &sql)
  {
    int level;
    qo_get_optimization_param (&level, QO_PARAM_LEVEL);
    qo_set_optimization_param (NULL, QO_PARAM_LEVEL, 2);

    int error = prepare (sql, 0);

    // restore
    qo_set_optimization_param (NULL, QO_PARAM_LEVEL, level);

    return error;
  }

  int
  query_handler::execute (const execute_request &request)
  {
    int error = NO_ERROR;

    int flag = request.execute_flag;
    int max_col_size = request.max_field;
    const std::vector<DB_VALUE> &bind_values = request.param_values;
    int max_row = -1; // TODO

    // 0) clear qresult
    end_qresult ();

    if (m_prepare_flag & PREPARE_CALL)
      {
	error = execute_internal_call (flag, max_col_size, max_row, bind_values, request.param_modes);
      }
    else
      {
	error = execute_internal (flag, max_col_size, max_row, bind_values);
      }

    if (error == NO_ERROR)
      {
	/* set max_col_size */
	m_max_col_size = max_col_size;
	m_execute_info.handle_id = get_id ();
	m_query_id = m_execute_info.qresult_info.query_id;

	/* include column info? */
	if (db_check_single_query (m_session) != NO_ERROR) /* ER_IT_MULTIPLE_STATEMENT */
	  {
	    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IT_MULTIPLE_STATEMENT, 0);
	    m_error_ctx.set_error (db_error_code (), db_error_string (1), __FILE__, __LINE__);
	    return ER_FAILED;
	  }
      }
    else
      {
	if (m_prepare_flag & PREPARE_XASL_CACHE_PINNED)
	  {
	    db_session_set_xasl_cache_pinned (m_session, false, false);
	    m_prepare_flag &= ~PREPARE_XASL_CACHE_PINNED;
	  }
      }

    return error;
  }

  get_generated_keys_info
  query_handler::generated_keys ()
  {
    int error = NO_ERROR;

    get_generated_keys_info info;

    int stmt_type;
    DB_QUERY_RESULT *qres = (DB_QUERY_RESULT *) m_query_result.result;
    if (qres == NULL)
      {
	// TODO: proper error code
	m_error_ctx.set_error (METHOD_CALLBACK_ER_INTERNAL, NULL, __FILE__, __LINE__);
	error = ER_FAILED;
      }
    else
      {
	if (qres->type == T_SELECT)
	  {
	    stmt_type = qres->res.s.stmt_type;
	    error = get_generated_keys_server_insert (info, *qres);
	  }
	else if (qres->type == T_CALL)
	  {
	    stmt_type = m_query_result.stmt_type;
	    error = get_generated_keys_client_insert (info, *qres);
	  }
	else
	  {
	    // TODO: proper error code
	    m_error_ctx.set_error (METHOD_CALLBACK_ER_INTERNAL, NULL, __FILE__, __LINE__);
	    error = ER_FAILED;
	  }
      }

    if (error == NO_ERROR)
      {
	/* set qresult_info */
	query_result_info &result_info = info.qresult_info;
	result_info.stmt_type = stmt_type;
	result_info.tuple_count = info.generated_keys.tuples.size ();
	result_info.query_id = -1; /* initialized value, intead of garbage */
      }

    return info;
  }

  int
  query_handler::get_generated_keys_client_insert (get_generated_keys_info &info, DB_QUERY_RESULT &qres)
  {
    int error = NO_ERROR;
    int tuple_count = 0;
    DB_SEQ *seq = NULL;
    DB_VALUE oid_val;

    assert (qres.type == T_CALL);

    DB_VALUE *val_ptr = qres.res.c.val_ptr;
    DB_TYPE db_type = DB_VALUE_DOMAIN_TYPE (val_ptr);
    if (db_type == DB_TYPE_SEQUENCE)
      {
	seq = db_get_set (val_ptr);
	if (seq == NULL)
	  {
	    // TODO: proper error code
	    m_error_ctx.set_error (METHOD_CALLBACK_ER_INTERNAL, NULL, __FILE__, __LINE__);
	    return ER_FAILED;
	  }
	tuple_count = db_col_size (seq);
      }
    else if (db_type == DB_TYPE_OBJECT)
      {
	tuple_count = 1;
	db_make_object (&oid_val, db_get_object (val_ptr));
      }
    else
      {
	// TODO: proper error code
	m_error_ctx.set_error (METHOD_CALLBACK_ER_INTERNAL, NULL, __FILE__, __LINE__);
	return ER_FAILED;
      }

    for (int i = 0; i < tuple_count; i++)
      {
	if (seq != NULL)
	  {
	    error = db_col_get (seq, i, &oid_val);
	    if (error < 0)
	      {
		// TODO: proper error code
		m_error_ctx.set_error (METHOD_CALLBACK_ER_INTERNAL, NULL, __FILE__, __LINE__);
		return ER_FAILED;
	      }
	  }

	error = make_attributes_by_oid_value (info, oid_val, i + 1);
	if (error < 0)
	  {
	    // TODO: proper error code
	    m_error_ctx.set_error (METHOD_CALLBACK_ER_INTERNAL, NULL, __FILE__, __LINE__);
	    return ER_FAILED;
	  }
      }
    return NO_ERROR;
  }

  int
  query_handler::get_generated_keys_server_insert (get_generated_keys_info &info, DB_QUERY_RESULT &qres)
  {
    int error = NO_ERROR;

    assert (qres.type == T_SELECT);

    error = db_query_next_tuple (&qres);
    if (error < 0)
      {
	// TODO: proper error code
	m_error_ctx.set_error (METHOD_CALLBACK_ER_INTERNAL, NULL, __FILE__, __LINE__);
	return ER_FAILED;
      }

    int tuple_count = 0;
    DB_VALUE oid_val;
    while (qres.res.s.cursor_id.position == C_ON)
      {
	tuple_count++;

	error = db_query_get_tuple_value (&qres, 0, &oid_val);
	if (error < 0)
	  {
	    // TODO: proper error code
	    m_error_ctx.set_error (METHOD_CALLBACK_ER_INTERNAL, NULL, __FILE__, __LINE__);
	    return ER_FAILED;
	  }

	error = make_attributes_by_oid_value (info, oid_val, tuple_count);
	if (error < 0)
	  {
	    // TODO: proper error code
	    m_error_ctx.set_error (METHOD_CALLBACK_ER_INTERNAL, NULL, __FILE__, __LINE__);
	    return ER_FAILED;
	  }

	error = db_query_next_tuple (&qres);
	if (error < 0)
	  {
	    // TODO: proper error code
	    m_error_ctx.set_error (METHOD_CALLBACK_ER_INTERNAL, NULL, __FILE__, __LINE__);
	    return ER_FAILED;
	  }
      }

    return NO_ERROR;
  }

  int
  query_handler::make_attributes_by_oid_value (get_generated_keys_info &info, const DB_VALUE &oid_val, int tuple_offset)
  {
    int error = NO_ERROR;

    DB_OBJECT *obj = db_get_object (&oid_val);
    DB_OBJECT *class_obj = db_get_class (obj);
    DB_ATTRIBUTE *attributes = db_get_attributes (class_obj);

    OID ins_oid = OID_INITIALIZER;
    set_dbobj_to_oid (obj, &ins_oid);

    std::vector<DB_VALUE> attribute_values;
    for (DB_ATTRIBUTE *attr = attributes; attr; attr = db_attribute_next (attr))
      {
	DB_VALUE value;
	if (db_attribute_is_auto_increment (attr))
	  {
	    const char *attr_name = db_attribute_name (attr);
	    error = db_get (obj, attr_name, &value);
	    if (error < 0)
	      {
		error = ER_FAILED;
		break;
	      }

	    if (tuple_offset == 1)
	      {
		const char *class_name = db_get_class_name (class_obj);
		DB_DOMAIN *domain = db_attribute_domain (attr);
		int precision = db_domain_precision (domain);
		short scale = db_domain_scale (domain);
		char charset = db_domain_codeset (domain);
		int db_type = TP_DOMAIN_TYPE (domain);
		int set_type = DB_TYPE_NULL;

		if (TP_IS_SET_TYPE (db_type))
		  {
		    set_type = get_set_domain (domain, precision, scale, charset);
		  }

		/* first tuple, store attribute info */
		column_info c_info = set_column_info (db_type, set_type, scale, precision, charset, attr_name, attr_name, class_name,
						      false);

		info.column_infos.push_back (c_info);
	      }

	    attribute_values.push_back (value);
	  }
      }

    info.generated_keys.tuples.emplace_back (tuple_offset, attribute_values, ins_oid);
    return error;
  }

  int
  query_handler::set_qresult_info (query_result_info &qinfo)
  {
    int error = NO_ERROR;
    OID ins_oid = OID_INITIALIZER;
    DB_OBJECT *ins_obj_p;

    query_result &qresult = m_query_result;
    DB_QUERY_RESULT *qres = (DB_QUERY_RESULT *) qresult.result;

    /* set result of a GET_GENERATED_KEYS request */
    if (qresult.stmt_type == CUBRID_STMT_INSERT && qres != NULL)
      {
	if (qres->type == T_SELECT)
	  {
	    /* result of a GET_GENERATED_KEYS request, server insert */
	    /* do nothing */
	  }
	else // qres->type == T_CALL
	  {
	    DB_VALUE val;
	    error = db_query_get_tuple_value ((DB_QUERY_RESULT *) qresult.result, 0, &val);
	    if (error >= 0)
	      {
		if (DB_VALUE_DOMAIN_TYPE (&val) == DB_TYPE_OBJECT)
		  {
		    ins_obj_p = db_get_object (&val);
		    set_dbobj_to_oid (ins_obj_p, &ins_oid);
		    db_value_clear (&val);
		  }
		else if (DB_VALUE_DOMAIN_TYPE (&val) == DB_TYPE_SEQUENCE)
		  {
		    /* result of a GET_GENERATED_KEYS request, client insert */
		    DB_VALUE value;
		    DB_SEQ *seq = db_get_set (&val);
		    if (seq != NULL && db_col_size (seq) == 1)
		      {
			db_col_get (seq, 0, &value);
			ins_obj_p = db_get_object (&value);
			set_dbobj_to_oid (ins_obj_p, &ins_oid);
			db_value_clear (&value);
		      }
		    db_value_clear (&val);
		  }
	      }
	  }
      }

    query_result_info &result_info = qinfo;
    result_info.ins_oid = ins_oid;
    result_info.stmt_type = qresult.stmt_type;
    result_info.tuple_count = qresult.tuple_count;
    result_info.include_oid = qresult.include_oid;

    if (qres && qres->type == T_SELECT)
      {
	result_info.query_id = qres->res.s.query_id;
      }
    return error;
  }

  int
  query_handler::execute_internal_call (int flag, int max_col_size, int max_row,
					const std::vector<DB_VALUE> &bind_values, const std::vector<int> &param_modes)
  {
    int error = NO_ERROR;

    int num_bind = get_num_markers ();
    assert (num_bind == (int) bind_values.size ());

    prepare_call_info &call_info = m_prepare_call_info;
    // set host variables
    if (num_bind > 0)
      {
	if (call_info.is_first_out)
	  {
	    error = set_host_variables (num_bind - 1, (DB_VALUE *) &bind_values[1]);
	  }
	else
	  {
	    error = set_host_variables (num_bind, (DB_VALUE *) &bind_values[0]);
	  }

	if (error != NO_ERROR)
	  {
	    m_error_ctx.set_error (error, NULL, __FILE__, __LINE__);
	    return ER_FAILED;
	  }
      }

    DB_QUERY_RESULT *result = NULL;
    int stmt_id = m_query_result.stmt_id;
    jsp_set_prepare_call ();
    int n = db_execute_and_keep_statement (m_session, stmt_id, &result);
    jsp_unset_prepare_call ();
    if (n < 0)
      {
	m_error_ctx.set_error (n, NULL, __FILE__, __LINE__);
	return ER_FAILED;
      }

    if (result != NULL)
      {
	/* success; copy the values in tuples */
	(void) db_query_set_copy_tplvalue (result, 1 /* copy */ );
      }

    int i = 0;
    if (call_info.is_first_out)
      {
	if (result != NULL)
	  {
	    db_query_get_tuple_value (result, 0, &call_info.dbval_args[0]);
	  }
	i++;
      }

    DB_VALUE *out_vals = db_get_hostvars (m_session);
    for (int j = 0; i < call_info.num_args; i++, j++)
      {
	call_info.dbval_args[i] = out_vals[j];
      }

    m_execute_info.num_affected = n;

    m_has_result_set = true;

    /* set query result */
    m_query_result.result = result;
    m_query_result.tuple_count = n;

    error = set_qresult_info (m_execute_info.qresult_info);

    // set prepare call info
    if (error == NO_ERROR)
      {
	assert (call_info.param_modes.size() == param_modes.size());
	for (int i = 0; i < (int) call_info.param_modes.size(); i++)
	  {
	    call_info.param_modes[i] = param_modes[i];
	  }
	m_execute_info.call_info = &call_info;
      }

    return error;
  }

  void
  query_handler::end_qresult ()
  {
    query_result &q_result = m_query_result;
    if (q_result.copied == false && q_result.result)
      {
	DB_QUERY_RESULT *result = q_result.result;
	db_query_end_internal (result, false);
      }
    q_result.result = NULL;

    if (q_result.column_info)
      {
	db_query_format_free (q_result.column_info);
	q_result.column_info = NULL;
      }

    m_has_result_set = false;
    m_query_id = -1;
  }

  int
  query_handler::execute_internal (int flag, int max_col_size, int max_row,
				   const std::vector<DB_VALUE> &bind_values)
  {
    int error = NO_ERROR;

    assert (is_prepared ());

    // bind host variables
    int num_bind = get_num_markers ();
    assert (num_bind == (int) bind_values.size ());
    if (num_bind > 0)
      {
	error = set_host_variables (num_bind, (DB_VALUE *) bind_values.data());
	if (error != NO_ERROR)
	  {
	    return error;
	  }
      }

    if (flag & EXEC_RETURN_GENERATED_KEYS)
      {
	db_session_set_return_generated_keys ((DB_SESSION *) m_session, true);
      }
    else
      {
	db_session_set_return_generated_keys ((DB_SESSION *) m_session, false);
      }

    int stmt_id = -1;

    // check unexpected behavior trying to execute query handler un-prepared
    if (m_is_prepared == false)
      {
	assert (false); // something wrong
	m_error_ctx.set_error (METHOD_CALLBACK_ER_STMT_POOLING, NULL, __FILE__, __LINE__);
	return ER_FAILED;
      }
    else
      {
	stmt_id = m_query_result.stmt_id;
      }

    DB_QUERY_RESULT *result = NULL;
    int n = db_execute_and_keep_statement (m_session, stmt_id, &result);
    if (n < 0)
      {
	return ER_FAILED;
      }
    else if (result != NULL)
      {
	/* success; peek the values in tuples */
	(void) db_query_set_copy_tplvalue (result, 0 /* peek */ );
      }

    if (max_row > 0 && db_get_statement_type (m_session, stmt_id) == CUBRID_STMT_SELECT)
      {
	// TODO: max_row?
      }

    m_execute_info.num_affected = n;

    if (m_prepare_flag & PREPARE_XASL_CACHE_PINNED)
      {
	db_session_set_xasl_cache_pinned (m_session, false, false);
	m_prepare_flag &= ~PREPARE_XASL_CACHE_PINNED;
      }

    db_get_cacheinfo (m_session, stmt_id, &m_use_plan_cache, NULL);

    /* set to query result */
    m_query_result.result = result;
    m_query_result.tuple_count = n;

    if (has_stmt_result_set (m_query_result.stmt_type))
      {
	m_has_result_set = true;
      }

    error = set_qresult_info (m_execute_info.qresult_info);

    return error;
  }

  bool
  query_handler::has_stmt_result_set (char stmt_type)
  {
    switch (stmt_type)
      {
      case CUBRID_STMT_SELECT:
      case CUBRID_STMT_CALL:
      case CUBRID_STMT_GET_STATS:
      case CUBRID_STMT_EVALUATE:
	return true;

      default:
	break;
      }

    return false;
  }

  int
  query_handler::prepare_query ()
  {
    int &flag = m_prepare_flag;

    m_session = db_open_buffer (m_sql_stmt.c_str());
    if (!m_session)
      {
	m_error_ctx.set_error (db_error_code (), db_error_string (1), __FILE__, __LINE__);
	return ER_FAILED;
      }

    if (db_check_single_query (m_session) == ER_IT_MULTIPLE_STATEMENT)
      {
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IT_MULTIPLE_STATEMENT, 0);
	m_error_ctx.set_error (db_error_code (), db_error_string (1), __FILE__, __LINE__);
	return ER_FAILED;
      }

    flag |= (flag & PREPARE_UPDATABLE) ? PREPARE_INCLUDE_OID : 0;
    if (flag & PREPARE_INCLUDE_OID)
      {
	db_include_oid (m_session, DB_ROW_OIDS);
      }

    if (flag & PREPARE_XASL_CACHE_PINNED)
      {
	db_session_set_xasl_cache_pinned (m_session, true, false);
      }

    m_stmt_type = CUBRID_STMT_NONE;
    int stmt_id = db_compile_statement (m_session);
    if (stmt_id < 0)
      {
	m_stmt_type = get_stmt_type (m_sql_stmt);
	if (stmt_id == ER_PT_SEMANTIC && m_stmt_type != CUBRID_STMT_SELECT && m_stmt_type != CUBRID_MAX_STMT_TYPE)
	  {
	    close_and_free_session ();
	  }
	else
	  {
	    m_error_ctx.set_error (stmt_id, db_error_string (1), __FILE__, __LINE__);
	  }
	m_is_prepared = false;
	return ER_FAILED;
      }
    else
      {
	m_stmt_type = db_get_statement_type (m_session, stmt_id);
	m_is_prepared = true;
      }

    db_get_cacheinfo (m_session, stmt_id, &m_use_plan_cache, NULL);

    /* prepare result set */
    m_num_markers = get_num_markers ();

    query_result &q_result = m_query_result;
    q_result.stmt_type = m_stmt_type;
    q_result.stmt_id = stmt_id;

    return NO_ERROR;
  }

  int
  query_handler::prepare_call ()
  {
    std::string sql_stmt_copy = m_sql_stmt;
    int error = m_prepare_call_info.set_is_first_out (sql_stmt_copy);
    if (error != NO_ERROR)
      {
	m_error_ctx.set_error (METHOD_CALLBACK_ER_INVALID_CALL_STMT, NULL, __FILE__, __LINE__);
	return ER_FAILED;
      }

    str_trim (sql_stmt_copy);
    int stmt_type = get_stmt_type (sql_stmt_copy);
    if (stmt_type != CUBRID_STMT_CALL)
      {
	m_error_ctx.set_error (METHOD_CALLBACK_ER_INVALID_CALL_STMT, NULL, __FILE__, __LINE__);
	return ER_FAILED;
      }

    m_session = db_open_buffer (sql_stmt_copy.c_str());
    if (!m_session)
      {
	m_error_ctx.set_error (db_error_code(), db_error_string (1), __FILE__, __LINE__);
	return ER_FAILED;
      }

    if (db_check_single_query (m_session) == ER_IT_MULTIPLE_STATEMENT)
      {
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IT_MULTIPLE_STATEMENT, 0);
	m_error_ctx.set_error (db_error_code (), db_error_string (1), __FILE__, __LINE__);
	er_clear ();
	return ER_FAILED;
      }

    int stmt_id = db_compile_statement (m_session);
    if (stmt_id < 0)
      {
	m_error_ctx.set_error (stmt_id, db_error_string (1), __FILE__, __LINE__);
	return ER_FAILED;
      }

    m_is_prepared = true;

    db_get_cacheinfo (m_session, stmt_id, &m_use_plan_cache, NULL);

    /* prepare result set */
    m_stmt_type = CUBRID_STMT_CALL_SP;
    m_num_markers = get_num_markers ();
    m_prepare_call_info.set_prepare_call_info (m_num_markers);

    query_result &q_result = m_query_result;
    q_result.stmt_type = m_stmt_type;
    q_result.stmt_id = stmt_id;

    return NO_ERROR;
  }

  void
  query_handler::close_and_free_session ()
  {
    if (m_session)
      {
	db_close_session ((DB_SESSION *) (m_session));
      }
    m_session = NULL;
  }

  void
  query_handler::set_prepare_column_list_info (std::vector<column_info> &infos)
  {
    m_query_result.include_oid = false;

    if (!m_query_result.null_type_column.empty())
      {
	m_query_result.null_type_column.clear();
      }

    int stmt_id = m_query_result.stmt_id;
    char stmt_type = m_query_result.stmt_type;
    if (stmt_type == CUBRID_STMT_SELECT)
      {
	// TODO: updatable
	if (m_prepare_flag)
	  {
	    if (db_query_produce_updatable_result (m_session, stmt_id) <= 0)
	      {
		// TODO: updatable
	      }
	    else
	      {
		m_query_result.include_oid = true;
	      }
	  }

	DB_QUERY_TYPE *db_column_info = db_get_query_type_list (m_session, stmt_id);
	if (db_column_info == NULL)
	  {
	    m_error_ctx.set_error (db_error_code(), db_error_string (1), __FILE__, __LINE__);
	  }

	int num_cols = 0;
	char *col_name = NULL, *class_name = NULL, *attr_name = NULL;

	char set_type;
	int precision = 0;
	short scale = 0;
	char charset;

	DB_QUERY_TYPE *col;
	for (col = db_column_info; col != NULL; col = db_query_format_next (col))
	  {
#if 0
	    // TODO: stripped_column_name
	    if (stripped_column_name)
	      {
		col_name = (char *) db_query_format_name (col);
	      }
	    else
#endif
	      {
		col_name = (char *) db_query_format_original_name (col);
		if (strchr (col_name, '*') != NULL)
		  {
		    col_name = (char *) db_query_format_name (col);
		  }
	      }
	    class_name = (char *) db_query_format_class_name (col);
	    attr_name = (char *) db_query_format_attr_name (col);

	    // TODO: related to updatable flag

	    DB_DOMAIN *domain = db_query_format_domain (col);
	    DB_TYPE db_type = TP_DOMAIN_TYPE (domain);

	    if (TP_IS_SET_TYPE (db_type))
	      {
		// TODO: set type
		set_type = get_set_domain (domain, precision, scale, charset);
	      }
	    else
	      {
		set_type = DB_TYPE_NULL;
		precision = db_domain_precision (domain);
		scale = (short) db_domain_scale (domain);
		charset = db_domain_codeset (domain);
	      }

	    if (db_type == DB_TYPE_NULL)
	      {
		m_query_result.null_type_column.push_back (1);
	      }
	    else
	      {
		m_query_result.null_type_column.push_back (0);
	      }

	    column_info info = set_column_info ((int) db_type, set_type, scale, precision, charset, col_name, attr_name, class_name,
						(char) db_query_format_is_non_null (col));
	    infos.push_back (info);
	    num_cols++;
	  }

	m_query_result.num_column = num_cols;

	// TODO: updatable
	//q_result->col_updatable = updatable_flag;
	//q_result->col_update_info = col_update_info;
	if (db_column_info)
	  {
	    db_query_format_free (db_column_info);
	  }
      }
    else if (stmt_type == CUBRID_STMT_CALL || stmt_type == CUBRID_STMT_GET_STATS || stmt_type == CUBRID_STMT_EVALUATE)
      {
	m_query_result.null_type_column.push_back (1);
	column_info info; // default constructor
	infos.push_back (info);
      }
    else
      {
	//
      }
  }

  column_info
  query_handler::set_column_info (int dbType, int setType, short scale, int prec, char charset, const char *col_name,
				  const char *attr_name,
				  const char *class_name, char is_non_null)
  {
    DB_OBJECT *class_obj = db_find_class (class_name);
    DB_ATTRIBUTE *attr = db_get_attribute (class_obj, col_name);

    char auto_increment = db_attribute_is_auto_increment (attr);
    char unique_key = db_attribute_is_unique (attr);
    char primary_key = db_attribute_is_primary_key (attr);
    char reverse_index = db_attribute_is_reverse_indexed (attr);
    char reverse_unique = db_attribute_is_reverse_unique (attr);
    char foreign_key = db_attribute_is_foreign_key (attr);
    char shared = db_attribute_is_shared (attr);

    std::string col_name_string (col_name ? col_name : "");
    std::string attr_name_string (attr_name? attr_name : "");
    std::string class_name_string (class_name? class_name : "");

    std::string default_value_string = get_column_default_as_string (attr);

    column_info info (dbType, setType, scale, prec, charset,
		      col_name_string, default_value_string,
		      auto_increment, unique_key, primary_key, reverse_index, reverse_unique, foreign_key, shared,
		      attr_name_string, class_name_string, is_non_null);

    return info;
  }

  /*
  * set_host_variables ()
  *
  *   return: error code or NO_ERROR
  *   db_session(in):
  *   num_bind(in):
  *   in_values(in):
  */
  int
  query_handler::set_host_variables (int num_bind, DB_VALUE *in_values)
  {
    int err_code;
    DB_CLASS_MODIFICATION_STATUS cls_status;
    int stmt_id;

    err_code = db_push_values (m_session, num_bind, in_values);
    if (err_code != NO_ERROR)
      {
	int stmt_count = db_statement_count (m_session);
	for (stmt_id = 0; stmt_id < stmt_count; stmt_id++)
	  {
	    cls_status = db_has_modified_class (m_session, stmt_id);
	    if (cls_status == DB_CLASS_MODIFIED)
	      {
		m_error_ctx.set_error (METHOD_CALLBACK_ER_STMT_POOLING, NULL, __FILE__, __LINE__);
		return err_code;
	      }
	    else if (cls_status == DB_CLASS_ERROR)
	      {
		assert (er_errid () != NO_ERROR);
		err_code = er_errid ();
		if (err_code == NO_ERROR)
		  {
		    err_code = ER_FAILED;
		  }
		m_error_ctx.set_error (err_code, NULL, __FILE__, __LINE__);
		return err_code;
	      }
	  }
	m_error_ctx.set_error (err_code, NULL, __FILE__, __LINE__);
      }

    return err_code;
  }

  void
  query_result::clear ()
  {
    column_info = NULL;
    result = NULL;
    stmt_id = 0;
    stmt_type = 0;
    num_column = 0;
    tuple_count = 0;

    include_oid = false;
    copied = false;
  }

  void query_handler::set_dbobj_to_oid (DB_OBJECT *obj, OID *oid)
  {
    OID *objs_oid = db_identifier (obj);

    assert (oid != NULL);

    if (objs_oid == NULL)
      {
	oid->pageid = 0;
	oid->volid = 0;
	oid->slotid = 0;
      }
    else
      {
	oid->pageid = objs_oid->pageid;
	oid->volid = objs_oid->volid;
	oid->slotid = objs_oid->slotid;
      }
  }
}
