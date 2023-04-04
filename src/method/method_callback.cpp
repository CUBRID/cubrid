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

#include "method_callback.hpp"

#include "dbi.h"

#include "ddl_log.h"

#include "method_def.hpp"
#include "method_compile.hpp"
#include "method_query_util.hpp"
#include "method_struct_oid_info.hpp"
#include "method_schema_info.hpp"

#include "parser.h"
#include "api_compat.h" /* DB_SESSION */
#include "db.h"

#include "object_primitive.h"
#include "oid.h"

#include "transaction_cl.h"

#include "jsp_cl.h"
#include "authenticate.h"
#include "set_object.h"
#include "transform.h"
#include "execute_statement.h"
#include "schema_manager.h"

extern int ux_create_srv_handle_with_method_query_result (DB_QUERY_RESULT *result, int stmt_type, int num_column,
    DB_QUERY_TYPE *column_info, bool is_holdable);

namespace cubmethod
{
  callback_handler::callback_handler (int max_query_handler)
  {
    m_query_handlers.resize (max_query_handler, nullptr);
    m_oid_handler = nullptr;
  }

  callback_handler::~callback_handler ()
  {
    if (m_oid_handler)
      {
	delete m_oid_handler;
      }
  }

  int
  callback_handler::callback_dispatch (packing_unpacker &unpacker)
  {
    m_error_ctx.clear ();

    int code;
    unpacker.unpack_int (code);

    int error = NO_ERROR;
    switch (code)
      {
      case METHOD_CALLBACK_QUERY_PREPARE:
	error = prepare (unpacker);
	break;
      case METHOD_CALLBACK_QUERY_EXECUTE:
	error = execute (unpacker);
	break;
      case METHOD_CALLBACK_OID_GET:
	error = oid_get (unpacker);
	break;
      case METHOD_CALLBACK_OID_PUT:
	error = oid_put (unpacker);
	break;
      case METHOD_CALLBACK_OID_CMD:
	error = oid_cmd (unpacker);
	break;
      case METHOD_CALLBACK_COLLECTION:
	error = collection_cmd (unpacker);
	break;
      case METHOD_CALLBACK_MAKE_OUT_RS:
	error = make_out_resultset (unpacker);
	break;
      case METHOD_CALLBACK_GET_GENERATED_KEYS:
	error = generated_keys (unpacker);
	break;

      /* schema info */
      case METHOD_CALLBACK_GET_SCHEMA_INFO:
	// error = get_schema_info (unpacker);
	assert (false);
	break;

      /* compilation */
      case METHOD_CALLBACK_GET_SQL_SEMNATICS:
	error = get_sql_semantics (unpacker);
	break;
      case METHOD_CALLBACK_GET_GLOBAL_SEMANTICS:
	error = get_global_semantics (unpacker);
	break;
      default:
	assert (false);
	error = ER_FAILED;
	break;
      }

#if defined (CS_MODE)
    mcon_send_queue_data_to_server ();
#else
    /* do nothing for SA_MODE */
#endif

    return error;
  }

  int
  callback_handler::prepare (packing_unpacker &unpacker)
  {
    std::string sql;
    int flag;
    unpacker.unpack_all (sql, flag);

    /* find in m_sql_handler_map */
    query_handler *handler = get_query_handler_by_sql (sql);
    if (handler != nullptr)
      {
	/* found in statement handler cache */
	handler->set_is_occupied (true);
      }
    else
      {
	/* not found in statement handler cache */
	handler = new_query_handler ();
	if (handler == nullptr)
	  {
	    // TODO: proper error code
	    m_error_ctx.set_error (METHOD_CALLBACK_ER_NO_MORE_MEMORY, NULL, __FILE__, __LINE__);
	  }
	else
	  {
	    int error = handler->prepare (sql, flag);
	    if (error == NO_ERROR)
	      {
		// add to statement handler cache
		m_sql_handler_map.emplace (sql, handler->get_id ());
	      }
	    else
	      {
		m_error_ctx.set_error (db_error_code (), db_error_string (1), __FILE__, __LINE__);
	      }
	  }
      }

    /* DDL audit */
    if (handler && logddl_set_stmt_type (handler->get_statement_type()))
      {
	logddl_set_sql_text ((char *) sql.c_str (), sql.size ());
	logddl_set_err_code (m_error_ctx.get_error ());
      }

    if (m_error_ctx.has_error())
      {
	return mcon_pack_and_queue (METHOD_RESPONSE_ERROR, m_error_ctx);
      }
    else
      {
	return mcon_pack_and_queue (METHOD_RESPONSE_SUCCESS, handler->get_prepare_info ());
      }
  }

  int
  callback_handler::execute (packing_unpacker &unpacker)
  {
    execute_request request;
    request.unpack (unpacker);

    query_handler *handler = get_query_handler_by_id (request.handler_id);
    if (handler == nullptr)
      {
	// TODO: proper error code
	m_error_ctx.set_error (METHOD_CALLBACK_ER_INTERNAL, NULL, __FILE__, __LINE__);
	assert (false); // the error should have been handled in prepare function
      }
    else
      {
	int error = handler->execute (request);
	if (error == NO_ERROR)
	  {
	    /* register query_id for out resultset */
	    const cubmethod::query_result &qresult = handler->get_result();
	    if (qresult.stmt_type == CUBRID_STMT_SELECT)
	      {
		uint64_t qid = (uint64_t) handler->get_query_id ();
		m_qid_handler_map[qid] = request.handler_id;
	      }
	  }
	else
	  {
	    /* XASL cache is not found */
	    if (error == ER_QPROC_INVALID_XASLNODE)
	      {
		m_error_ctx.clear ();
		handler->prepare_retry ();
		error = handler->execute (request);
	      }

	    if (error != NO_ERROR)
	      {
		m_error_ctx.set_error (db_error_code (), db_error_string (1), __FILE__, __LINE__);
	      }
	  }

	/* DDL audit */
	logddl_write_end ();
      }

    if (m_error_ctx.has_error())
      {
	return mcon_pack_and_queue (METHOD_RESPONSE_ERROR, m_error_ctx);
      }
    else
      {
	return mcon_pack_and_queue (METHOD_RESPONSE_SUCCESS, handler->get_execute_info ());
      }
  }

  int
  callback_handler::make_out_resultset (packing_unpacker &unpacker)
  {
    uint64_t query_id;
    unpacker.unpack_all (query_id);

    cubmethod::query_handler *query_handler = get_query_handler_by_query_id (query_id);
    if (query_handler)
      {
	const query_result &qresult = query_handler->get_result();

	make_outresult_info info;
	query_handler->set_prepare_column_list_info (info.column_infos);
	query_handler->set_qresult_info (info.qresult_info);
	return mcon_pack_and_queue (METHOD_RESPONSE_SUCCESS, info);
      }

    /* unexpected error, should not be here */
    m_error_ctx.set_error (METHOD_CALLBACK_ER_INTERNAL, NULL, __FILE__, __LINE__);
    return mcon_pack_and_queue (METHOD_RESPONSE_ERROR, m_error_ctx);
  }

  int
  callback_handler::generated_keys (packing_unpacker &unpacker)
  {
    int handler_id = -1;
    unpacker.unpack_all (handler_id);

    query_handler *handler = get_query_handler_by_id (handler_id);
    if (handler == nullptr)
      {
	// TODO: proper error code
	m_error_ctx.set_error (METHOD_CALLBACK_ER_NO_MORE_MEMORY, NULL, __FILE__, __LINE__);
	return ER_FAILED;
      }

    get_generated_keys_info info = handler->generated_keys ();
    if (m_error_ctx.has_error())
      {
	return mcon_pack_and_queue (METHOD_RESPONSE_ERROR, m_error_ctx);
      }
    else
      {
	return mcon_pack_and_queue (METHOD_RESPONSE_SUCCESS, info);
      }
  }

//////////////////////////////////////////////////////////////////////////
// OID
//////////////////////////////////////////////////////////////////////////

  oid_handler *
  callback_handler::get_oid_handler ()
  {
    if (m_oid_handler == nullptr)
      {
	m_oid_handler = new (std::nothrow) oid_handler (m_error_ctx);
	if (m_oid_handler == nullptr)
	  {
	    assert (false);
	    m_error_ctx.set_error (METHOD_CALLBACK_ER_NO_MORE_MEMORY, NULL, __FILE__, __LINE__);
	  }
      }

    return m_oid_handler;
  }

  int
  callback_handler::oid_get (packing_unpacker &unpacker)
  {
    int error = NO_ERROR;

    oid_get_request request;
    request.unpack (unpacker);

    oid_get_info info = get_oid_handler()->oid_get (request.oid, request.attr_names);
    if (m_error_ctx.has_error())
      {
	return mcon_pack_and_queue (METHOD_RESPONSE_ERROR, m_error_ctx);
      }
    else
      {
	return mcon_pack_and_queue (METHOD_RESPONSE_SUCCESS, info);
      }
  }

  int
  callback_handler::oid_put (packing_unpacker &unpacker)
  {
    oid_put_request request;
    request.unpack (unpacker);

    int result = get_oid_handler()->oid_put (request.oid, request.attr_names, request.db_values);
    if (m_error_ctx.has_error())
      {
	return mcon_pack_and_queue (METHOD_RESPONSE_ERROR, m_error_ctx);
      }
    else
      {
	return mcon_pack_and_queue (METHOD_RESPONSE_SUCCESS, result);
      }
  }

  int
  callback_handler::oid_cmd (packing_unpacker &unpacker)
  {
    int cmd = OID_CMD_FIRST;
    unpacker.unpack_int (cmd);

    OID oid = OID_INITIALIZER;
    unpacker.unpack_oid (oid);

    std::string res; // result for OID_CLASS_NAME

    int res_code = get_oid_handler()->oid_cmd (oid, cmd, res);
    if (m_error_ctx.has_error())
      {
	return mcon_pack_and_queue (METHOD_RESPONSE_ERROR, m_error_ctx);
      }
    else
      {
	return mcon_pack_and_queue (METHOD_RESPONSE_SUCCESS, res_code, res);
      }
  }

//////////////////////////////////////////////////////////////////////////
// Collection
//////////////////////////////////////////////////////////////////////////

  int
  callback_handler::collection_cmd (packing_unpacker &unpacker)
  {
    // args
    collection_cmd_request request;
    request.unpack (unpacker);

    int result = m_oid_handler->collection_cmd (request.oid, request.command, request.index, request.attr_name,
		 request.value);

    if (m_error_ctx.has_error())
      {
	return mcon_pack_and_queue (METHOD_RESPONSE_ERROR, m_error_ctx);
      }
    else
      {
	return mcon_pack_and_queue (METHOD_RESPONSE_SUCCESS, result);
      }
  }

//////////////////////////////////////////////////////////////////////////
// Schema Info
//////////////////////////////////////////////////////////////////////////
  /*
  int
  callback_handler::get_schema_info (packing_unpacker &unpacker)
  {
    int error = NO_ERROR;

    schema_info_handler sch_handler (m_error_ctx);




    return error;
  }
  */

//////////////////////////////////////////////////////////////////////////
// Compile
//////////////////////////////////////////////////////////////////////////
  int
  callback_handler::get_sql_semantics (packing_unpacker &unpacker)
  {
    sql_semantics_request request;
    unpacker.unpack_all (request);
    int i = -1;
    int error = NO_ERROR;

    std::vector<sql_semantics> semantics_vec;
    for (const std::string s : request.sqls)
      {
	i++;
	query_handler *handler = new_query_handler ();
	if (handler == nullptr)
	  {
	    break;
	  }

	sql_semantics semantics;
	semantics.idx = i;

	er_clear ();
	m_error_ctx.clear ();

	error = handler->prepare_compile (s);
	if (error == NO_ERROR && m_error_ctx.has_error () == false)
	  {
	    DB_SESSION *db_session = handler->get_db_session ();
	    const prepare_info &info = handler->get_prepare_info ();

	    semantics.sql_type = info.stmt_type;

	    PARSER_CONTEXT *parser = db_get_parser (db_session);
	    PT_NODE *stmt = db_get_statement (db_session, 0);
	    semantics.rewritten_query = parser_print_tree (parser, stmt);

	    const std::vector<column_info> &column_infos = info.column_infos;
	    for (const column_info &c_info : column_infos)
	      {
		semantics.columns.emplace_back (c_info);
	      }

	    int markers_cnt = parser->host_var_count + parser->auto_param_count;
	    DB_MARKER *marker = db_get_input_markers (db_session, 1);

	    if (markers_cnt > 0)
	      {
		semantics.hvs.resize (markers_cnt);

		while (marker)
		  {
		    int idx = marker->info.host_var.index;
		    semantics.hvs[idx].mode = 1;
		    if (marker->info.host_var.label)
		      {
			semantics.hvs[idx].name.assign ((char *) marker->info.host_var.label);
		      }

		    TP_DOMAIN *hv_expected_domain = NULL;
		    if (parser->host_var_count <= idx)
		      {
			// auto parameterized
			hv_expected_domain = marker->expected_domain;
		      }
		    else
		      {
			hv_expected_domain = db_session->parser->host_var_expected_domains[idx];
		      }

		    // safe guard
		    if (hv_expected_domain == NULL)
		      {
			hv_expected_domain = pt_node_to_db_domain (parser, marker, NULL);
		      }

		    semantics.hvs[idx].type = TP_DOMAIN_TYPE (hv_expected_domain);
		    semantics.hvs[idx].precision = db_domain_precision (hv_expected_domain);
		    semantics.hvs[idx].scale = (short) db_domain_scale (hv_expected_domain);
		    semantics.hvs[idx].charset = db_domain_codeset (hv_expected_domain);

		    if (db_session->parser->host_variables[idx].domain.general_info.is_null == 0)
		      {
			pr_clone_value (& (db_session->parser->host_variables[idx]), & (semantics.hvs[idx].value));
		      }
		    else
		      {
			db_make_null (& (semantics.hvs[idx].value));
		      }

		    marker = db_marker_next (marker);
		  }
	      }

	    // into variable
	    char **external_into_label = db_session->parser->external_into_label;
	    if (external_into_label)
	      {
		for (int i = 0; i < db_session->parser->external_into_label_cnt; i++)
		  {
		    semantics.into_vars.push_back (external_into_label[i]);
		    free (external_into_label[i]);
		  }
		free (external_into_label);
	      }
	    db_session->parser->external_into_label = NULL;
	    db_session->parser->external_into_label_cnt = 0;
	  }
	else
	  {
	    // clear previous infos
	    semantics_vec.clear ();

	    error = ER_FAILED;
	    semantics.sql_type = m_error_ctx.get_error ();
	    semantics.rewritten_query = m_error_ctx.get_error_msg ();
	  }

	semantics_vec.push_back (semantics);
	free_query_handle (handler->get_id (), true);

	if (error != NO_ERROR)
	  {
	    break;
	  }
      }

    sql_semantics_response response;
    response.semantics = std::move (semantics_vec);

    if (error == NO_ERROR)
      {
	return mcon_pack_and_queue (METHOD_RESPONSE_SUCCESS, response);
      }
    else
      {
	return mcon_pack_and_queue (METHOD_RESPONSE_ERROR, response);
      }
  }

  // TODO: move it to proper place
  static int
  get_user_defined_procedure_function_info (global_semantics_question &question, global_semantics_response_udpf &res)
  {
    DB_OBJECT *mop_p;
    DB_VALUE return_type;
    int err = NO_ERROR;
    int save;
    const char *name = question.name.c_str ();

    AU_DISABLE (save);
    {
      mop_p = jsp_find_stored_procedure (name);
      if (mop_p == NULL)
	{
	  assert (er_errid () != NO_ERROR);
	  err = er_errid ();
	  goto exit;
	}

      DB_VALUE temp;
      int num_args = -1;
      err = db_get (mop_p, SP_ATTR_ARG_COUNT, &temp);
      if (err == NO_ERROR)
	{
	  num_args = db_get_int (&temp);
	}

      pr_clear_value (&temp);
      if (num_args == -1)
	{
	  goto exit;
	}

      DB_VALUE args;
      /* arg_mode, arg_type */
      err = db_get (mop_p, SP_ATTR_ARGS, &args);
      if (err == NO_ERROR)
	{
	  DB_SET *param_set = db_get_set (&args);
	  DB_VALUE mode, arg_type;
	  int i;
	  for (i = 0; i < num_args; i++)
	    {
	      pl_parameter_info param_info;
	      set_get_element (param_set, i, &temp);
	      DB_OBJECT *arg_mop_p = db_get_object (&temp);
	      if (arg_mop_p)
		{
		  if (db_get (arg_mop_p, SP_ATTR_MODE, &mode) == NO_ERROR)
		    {
		      param_info.mode = db_get_int (&mode);
		    }

		  if (db_get (arg_mop_p, SP_ATTR_DATA_TYPE, &arg_type) == NO_ERROR)
		    {
		      param_info.type = db_get_int (&arg_type);
		    }

		  pr_clear_value (&mode);
		  pr_clear_value (&arg_type);
		  pr_clear_value (&temp);
		}
	      else
		{
		  break;
		}
	    }
	  pr_clear_value (&args);
	}

      if (db_get (mop_p, SP_ATTR_RETURN_TYPE, &return_type) == NO_ERROR)
	{
	  res.ret.type = db_get_int (&return_type);
	  pr_clear_value (&return_type);
	}
    }

exit:
    AU_ENABLE (save);

    res.err_id = err;
    if (err != NO_ERROR)
      {
	res.err_msg = er_msg ();
      }

    er_clear ();
    return err;
  }

  static int
  get_serial_info (global_semantics_question &question, global_semantics_response_serial &res)
  {
    int result = NO_ERROR;
    MOP serial_class_mop, serial_mop;
    DB_IDENTIFIER serial_obj_id;

    const char *serial_name = question.name.c_str ();
    serial_class_mop = sm_find_class (CT_SERIAL_NAME);

    serial_mop = do_get_serial_obj_id (&serial_obj_id, serial_class_mop, serial_name);
    if (serial_mop == NULL)
      {
	result = ER_QPROC_SERIAL_NOT_FOUND;
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_SERIAL_NOT_FOUND, 1, serial_name);
      }

    res.err_id = result;
    if (result != NO_ERROR)
      {
	res.err_msg = er_msg ();
      }

    er_clear();
    return result;
  }

  static int
  get_column_info (global_semantics_question &question, global_semantics_response_column &res)
  {
    int err = NO_ERROR;

    const std::string &name = question.name;
    if (name.empty () == true)
      {
	err = res.err_id = ER_FAILED;
	res.err_msg = "Invalid parameter";
      }

    std::string owner_name;
    std::string class_name;
    std::string attr_name;

    auto split_str = [] (const std::string& name, size_t &prev, size_t &cur, std::string& out_name)
    {
      cur = name.find ('.', prev);
      if (cur != std::string::npos)
	{
	  out_name = name.substr (prev, cur - prev);
	  prev = cur + 1;
	}
    };

    size_t prev = 0, cur;
    int dot_cnt = std::count (name.begin (), name.end(), '.');
    if (dot_cnt == 2) // with owner name
      {
	split_str (name, prev, cur, owner_name);
      }

    split_str (name, prev, cur, class_name);
    split_str (name, prev, cur, attr_name);

    std::string class_name_with_owner = owner_name + class_name;
    char realname[DB_MAX_IDENTIFIER_LENGTH] = { '\0' };
    sm_user_specified_name (class_name_with_owner.c_str (), realname, DB_MAX_IDENTIFIER_LENGTH);

    DB_ATTRIBUTE *attr = db_get_attribute_by_name (realname, attr_name.c_str ());
    if (attr == NULL)
      {
	err = res.err_id = ER_FAILED;
	res.err_msg = "Failed to get attribute information";
      }
    else
      {
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

	char auto_increment = db_attribute_is_auto_increment (attr);
	char unique_key = db_attribute_is_unique (attr);
	char primary_key = db_attribute_is_primary_key (attr);
	char reverse_index = db_attribute_is_reverse_indexed (attr);
	char reverse_unique = db_attribute_is_reverse_unique (attr);
	char foreign_key = db_attribute_is_foreign_key (attr);
	char shared = db_attribute_is_shared (attr);

	const char *c_attr_name = db_attribute_name (attr);

	std::string attr_name_string (c_attr_name? c_attr_name : "");
	std::string class_name_string (realname? realname : "");

	std::string default_value_string = get_column_default_as_string (attr);

	column_info info (db_type, set_type, scale, precision, charset,
			  attr_name_string, default_value_string,
			  auto_increment, unique_key, primary_key, reverse_index, reverse_unique, foreign_key, shared,
			  attr_name_string, class_name_string, false);

	res.c_info = std::move (info);
      }

    return err;
  }

  int
  callback_handler::get_global_semantics (packing_unpacker &unpacker)
  {
    int error = NO_ERROR;
    global_semantics_request request;
    unpacker.unpack_all (request);

    global_semantics_response response;

    for (global_semantics_question &question : request.qsqs)
      {
	switch (question.type)
	  {
	  case 1: // PROCEDURE
	  case 2: // FUNCTION
	  {
	    global_semantics_response_udpf udpf_res;
	    error = get_user_defined_procedure_function_info (question, udpf_res);

	    udpf_res.idx = request.qsqs.size ();
	    response.qs.push_back (std::move (udpf_res));
	    break;
	  }
	  case 3: // SERIAL
	  {
	    global_semantics_response_serial serial_res;
	    error = get_serial_info (question, serial_res);

	    serial_res.idx = request.qsqs.size ();
	    response.qs.push_back (std::move (serial_res));
	    break;
	  }
	  case 4: // COLUMN
	  {
	    global_semantics_response_column column_res;
	    error = get_column_info (question, column_res);

	    column_res.idx = request.qsqs.size ();
	    response.qs.push_back (std::move (column_res));
	    break;
	  }
	  default:
	  {
	    assert (false);
	    global_semantics_response_common error_response;
	    error = error_response.err_id = ER_FAILED;
	    error_response.err_msg = "Invalid request type";
	    error_response.idx = request.qsqs.size ();
	    response.qs.push_back (std::move (error_response));
	    break;
	  }
	  }

	if (error != NO_ERROR)
	  {
	    break;
	  }
      }

    if (error == NO_ERROR)
      {
	return mcon_pack_and_queue (METHOD_RESPONSE_SUCCESS, response);
      }
    else
      {
	return mcon_pack_and_queue (METHOD_RESPONSE_ERROR, response);
      }
  }

//////////////////////////////////////////////////////////////////////////
// Managing Query Handler Table
//////////////////////////////////////////////////////////////////////////

  query_handler *
  callback_handler::new_query_handler ()
  {
    int idx = 0;
    int handler_size = m_query_handlers.size();
    for (; idx < handler_size; idx++)
      {
	if (m_query_handlers[idx] == nullptr)
	  {
	    /* found */
	    break;
	  }
      }

    query_handler *handler = new (std::nothrow) query_handler (m_error_ctx, idx);
    if (handler == nullptr)
      {
	assert (false);
	return handler;
      }

    if (idx < handler_size)
      {
	m_query_handlers[idx] = handler;
      }
    else
      {
	m_query_handlers.push_back (handler);
      }

    return handler;
  }

  query_handler *
  callback_handler::get_query_handler_by_id (const int id)
  {
    if (id < 0 || id >= (int) m_query_handlers.size())
      {
	return nullptr;
      }

    return m_query_handlers[id];
  }

  void
  callback_handler::free_query_handle (int id, bool is_free)
  {
    if (id < 0 || id >= (int) m_query_handlers.size())
      {
	return;
      }
    if (m_query_handlers[id] != nullptr)
      {
	// clear <query ID -> handler ID>
	if (m_query_handlers[id]->get_query_id () != -1)
	  {
	    m_qid_handler_map.erase (m_query_handlers[id]->get_query_id ());
	  }
	if (is_free)
	  {
	    // clear <SQL string -> handler ID>
	    m_sql_handler_map.erase (m_query_handlers[id]->get_sql_stmt());

	    delete m_query_handlers[id];
	    m_query_handlers[id] = nullptr;
	  }
	else
	  {
	    m_query_handlers[id]->reset ();
	  }
      }
  }

  void
  callback_handler::free_query_handle_all (bool is_free)
  {
    for (int i = 0; i < (int) m_query_handlers.size(); i++)
      {
	free_query_handle (i, is_free);
      }
  }

  query_handler *
  callback_handler::get_query_handler_by_query_id (const uint64_t qid)
  {
    const auto &iter = m_qid_handler_map.find (qid);
    if (iter == m_qid_handler_map.end() )
      {
	return nullptr;
      }
    else
      {
	return get_query_handler_by_id (iter->second);
      }
  }

  query_handler *
  callback_handler::get_query_handler_by_sql (const std::string &sql)
  {
    for (auto it = m_sql_handler_map.lower_bound (sql); it != m_sql_handler_map.upper_bound (sql); it++)
      {
	query_handler *handler = get_query_handler_by_id (it->second);
	if (handler != nullptr && handler->get_is_occupied() == false)
	  {
	    /* found */
	    return handler;
	  }
      }

    return nullptr;
  }

  //////////////////////////////////////////////////////////////////////////
  // Global method callback handler interface
  //////////////////////////////////////////////////////////////////////////
  static callback_handler handler (100);

  callback_handler *get_callback_handler (void)
  {
    return &handler;
  }
}
