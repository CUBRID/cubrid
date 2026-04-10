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

#include "pl_executor.hpp"

#include "regu_var.hpp"
#include "fetch.h"
#include "memory_alloc.h"

// runtime
#include "dbtype.h"
#include "db_date.h"
#include "string_opfunc.h"
#include "system_parameter.h"
#include "log_impl.h"
#include "tz_support.h"

#include "method_struct_invoke.hpp"
#include "method_struct_query.hpp"
#include "method_struct_value.hpp"
#include "method_struct_oid_info.hpp"
#include "method_struct_parameter_info.hpp"

#include "pl_comm.h"
#include "pl_query_cursor.hpp"
#include "sp_code.hpp"
#include "xserver_interface.h"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"
namespace cubpl
{
  using namespace cubmethod;

  static int
  sp_evaluate_default_expr (THREAD_ENTRY *thread_p, const DB_DEFAULT_EXPR_TYPE expr_type, const int expr_op,
			    const char *expr_format, const DB_VALUE *sys_datetime, const DB_VALUE *sys_epochtime,
			    DB_VALUE *result)
  {
    int error = NO_ERROR;
    DB_VALUE temp_value, format_val, lang_val;
    TP_DOMAIN *result_domain = NULL;
    bool has_user_format = false;
    const char *lang_str = NULL;
    int flag = 0;

    db_make_null (&temp_value);
    db_make_null (&format_val);
    db_make_null (&lang_val);
    db_make_null (result);

    switch (expr_type)
      {
      case DB_DEFAULT_SYSTIME:
      {
	int month, day, year, hour, minute, second, millisecond;
	db_datetime_decode ((DB_DATETIME *) db_get_datetime (sys_datetime), &month, &day, &year, &hour, &minute,
			    &second, &millisecond);
	db_make_time (&temp_value, hour, minute, second);
      }
      break;

      case DB_DEFAULT_CURRENTTIME:
      {
	DB_TIME cur_time, db_time;
	const char *t_source, *t_dest;
	DB_DATETIME *datetime = db_get_datetime (sys_datetime);

	t_source = tz_get_system_timezone ();
	t_dest = tz_get_session_local_timezone ();
	db_time = datetime->time / 1000;
	error =
		tz_conv_tz_time_w_zone_name (&db_time, t_source, (int) strlen (t_source), t_dest, (int) strlen (t_dest),
					     &cur_time);
	if (error == NO_ERROR)
	  {
	    db_value_put_encoded_time (&temp_value, &cur_time);
	  }
      }
      break;

      case DB_DEFAULT_SYSDATE:
	error = db_value_put_encoded_date (&temp_value, &db_get_datetime (sys_datetime)->date);
	break;

      case DB_DEFAULT_CURRENTDATE:
      case DB_DEFAULT_CURRENTDATETIME:
      {
	TZ_REGION system_tz_region, session_tz_region;
	DB_DATETIME dest_dt;

	tz_get_system_tz_region (&system_tz_region);
	tz_get_session_tz_region (&session_tz_region);
	error =
		tz_conv_tz_datetime_w_region (db_get_datetime (sys_datetime), &system_tz_region, &session_tz_region, &dest_dt,
					      NULL, NULL);
	if (error == NO_ERROR)
	  {
	    if (expr_type == DB_DEFAULT_CURRENTDATE)
	      {
		db_value_put_encoded_date (&temp_value, &dest_dt.date);
	      }
	    else
	      {
		db_make_datetime (&temp_value, &dest_dt);
	      }
	  }
      }
      break;

      case DB_DEFAULT_SYSDATETIME:
	error = pr_clone_value (sys_datetime, &temp_value);
	break;

      case DB_DEFAULT_SYSTIMESTAMP:
	error = db_datetime_to_timestamp (sys_datetime, &temp_value);
	break;

      case DB_DEFAULT_CURRENTTIMESTAMP:
      {
	DB_DATE tmp_date = db_get_datetime (sys_datetime)->date;
	DB_TIME tmp_time = db_get_datetime (sys_datetime)->time / 1000;
	DB_TIMESTAMP tmp_timestamp;

	db_timestamp_encode_sys (&tmp_date, &tmp_time, &tmp_timestamp, NULL);
	db_make_timestamp (&temp_value, tmp_timestamp);
      }
      break;

      case DB_DEFAULT_UNIX_TIMESTAMP:
	error = db_unix_timestamp (sys_datetime, &temp_value);
	break;

      case DB_DEFAULT_USER:
      {
	int tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
	LOG_TDES *tdes = LOG_FIND_TDES (tran_index);
	char *temp = NULL;

	if (tdes != NULL)
	  {
	    size_t len = tdes->client.db_user.length () + tdes->client.host_name.length () + 2;
	    temp = (char *) db_private_alloc (thread_p, len);
	    if (temp == NULL)
	      {
		error = er_errid ();
	      }
	    else
	      {
		strcpy (temp, tdes->client.get_db_user ());
		strcat (temp, "@");
		strcat (temp, tdes->client.get_host_name ());
		db_make_string (&temp_value, temp);
		temp_value.need_clear = true;
	      }
	  }
      }
      break;

      case DB_DEFAULT_CURR_USER:
      {
	int tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
	LOG_TDES *tdes = LOG_FIND_TDES (tran_index);
	char *temp = (tdes != NULL) ? CONST_CAST (char *, tdes->client.get_db_user ()) : NULL;
	db_make_string (&temp_value, temp);
      }
      break;

      case DB_DEFAULT_SYSGUID:
	error = db_uuidv4 (thread_p, &temp_value);
	break;

      case DB_DEFAULT_UUIDV4:
	error = db_uuid_bin (thread_p, UUID_V4, 0, &temp_value);
	break;

      case DB_DEFAULT_UUIDV7:
	error = db_uuid_bin (thread_p, UUID_V7,
			     ((uint64_t) db_get_timestamp (sys_epochtime) * 1000ULL)
			     + (uint64_t) (db_get_datetime (sys_datetime)->time % 1000),
			     &temp_value);
	break;

      case DB_DEFAULT_NONE:
      default:
	error = ER_OBJ_INVALID_ARGUMENTS;
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
	break;
      }

    if (error != NO_ERROR)
      {
	pr_clear_value (&temp_value);
	return error;
      }

    if (expr_op == T_TO_CHAR)
      {
	if (expr_format != NULL)
	  {
	    db_make_string (&format_val, expr_format);
	    has_user_format = true;
	  }
	lang_str = prm_get_string_value (PRM_ID_INTL_DATE_LANG);
	lang_set_flag_from_lang (lang_str, has_user_format, 0, &flag);
	db_make_int (&lang_val, flag);
	error = db_to_char (&temp_value, &format_val, &lang_val, result, result_domain);
	pr_clear_value (&format_val);
      }
    else
      {
	error = pr_clone_value (&temp_value, result);
      }

    pr_clear_value (&temp_value);
    return error;
  }

  invoke_java::invoke_java (int tid, pl_signature *sig, bool tc)
    : tran_id (tid)
  {
    signature.assign (sig->ext.sp.target_class_name).append (".").append (sig->ext.sp.target_method_name);
    auth.assign (sig->auth);
    lang = sig->type;
    result_type = sig->result_type;

    pl_arg &arg = sig->arg;
    num_args = arg.arg_size;
    arg_mode.resize (num_args);
    arg_type.resize (num_args);

    for (int i = 0; i < num_args; i++)
      {
	arg_mode[i] = arg.arg_mode[i];
	arg_type[i] = arg.arg_type[i];
      }

    transaction_control = (lang == SP_LANG_PLCSQL) ? true : tc;
  }

  void
  invoke_java::pack (cubpacking::packer &serializator) const
  {
    serializator.pack_int (tran_id);
    serializator.pack_string (signature);
    serializator.pack_string (auth);
    serializator.pack_int (lang);
    serializator.pack_int (num_args);

    for (int i = 0; i < num_args; i++)
      {
	serializator.pack_int (arg_mode[i]);
	serializator.pack_int (arg_type[i]);
      }

    serializator.pack_int (result_type);
    serializator.pack_bool (transaction_control);
  }

  void
  invoke_java::unpack (cubpacking::unpacker &deserializator)
  {
    // TODO: unpacking is not necessary
    assert (false);
  }

  size_t
  invoke_java::get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const
  {
    size_t size = serializator.get_packed_int_size (start_offset); // tran_id
    size += serializator.get_packed_string_size (signature, size); // signature
    size += serializator.get_packed_string_size (auth, size); // auth
    size += serializator.get_packed_int_size (size); // lang
    size += serializator.get_packed_int_size (size); // num_args

    for (int i = 0; i < num_args; i++)
      {
	size += serializator.get_packed_int_size (size); // arg_mode
	size += serializator.get_packed_int_size (size); // arg_type
      }

    size += serializator.get_packed_int_size (size); // return_type
    size += serializator.get_packed_bool_size (size); // transaction_control
    return size;
  }


//////////////////////////////////////////////////
  executor::executor (pl_signature &sig)
    : m_sig (sig)
  {
    session *sess = get_session ();
    if (sess)
      {
	m_stack = sess->create_and_push_stack (nullptr);
      }
  }

  executor::~executor ()
  {
    // destory local resources
    pr_clear_value_vector (m_default_in_args);
    pr_clear_value_vector (m_out_args);

    // exit stack
    if (get_session () && m_stack != nullptr)
      {
	delete m_stack;
      }
  }

  int
  executor::append_missing_default_args ()
  {
    DB_VALUE sys_datetime, sys_epochtime;
    THREAD_ENTRY *thread_p = NULL;
    int error = NO_ERROR;
    const int supplied_arg_count = (int) m_args.size ();

    if (supplied_arg_count >= m_sig.arg.arg_size)
      {
	return NO_ERROR;
      }

    db_make_null (&sys_datetime);
    db_make_null (&sys_epochtime);

    if (m_stack == NULL)
      {
	return ER_FAILED;
      }

    thread_p = m_stack->get_thread_entry ();
    if (db_sys_date_and_epoch_time (&sys_datetime, &sys_epochtime) != NO_ERROR)
      {
	error = er_errid ();
	goto exit_clear;
      }

    m_default_in_args.reserve ((size_t) (m_sig.arg.arg_size - supplied_arg_count));

    for (int i = supplied_arg_count; i < m_sig.arg.arg_size; i++)
      {
	DB_VALUE default_arg_value;

	db_make_null (&default_arg_value);

	if (m_sig.arg.arg_default_value_size[i] == PL_ARG_DEFAULT_NONE)
	  {
	    error = ER_SP_INVALID_PARAM_COUNT;
	    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 2, m_sig.arg.arg_size, supplied_arg_count);
	    db_value_clear (&default_arg_value);
	    goto exit_clear;
	  }

	if (m_sig.arg.arg_default_value_size[i] == PL_ARG_DEFAULT_NULL)
	  {
	    db_make_null (&default_arg_value);
	  }
	else if ((DB_DEFAULT_EXPR_TYPE) m_sig.arg.arg_default_expr_type[i] != DB_DEFAULT_NONE)
	  {
	    error = sp_evaluate_default_expr (thread_p, (DB_DEFAULT_EXPR_TYPE) m_sig.arg.arg_default_expr_type[i],
					      m_sig.arg.arg_default_expr_op[i], m_sig.arg.arg_default_expr_format[i],
					      &sys_datetime, &sys_epochtime, &default_arg_value);
	    if (error != NO_ERROR)
	      {
		db_value_clear (&default_arg_value);
		goto exit_clear;
	      }
	  }
	else
	  {
	    error = db_make_string_copy (&default_arg_value, m_sig.arg.arg_default_value_size[i] > 0
					 ? m_sig.arg.arg_default_value[i] : "");
	    if (error != NO_ERROR)
	      {
		db_value_clear (&default_arg_value);
		goto exit_clear;
	      }
	  }

	m_default_in_args.emplace_back (default_arg_value);
	m_args.emplace_back (std::ref (m_default_in_args.back ()));
      }

exit_clear:
    db_value_clear (&sys_datetime);
    db_value_clear (&sys_epochtime);
    return error;
  }

  int
  executor::fetch_args_peek (regu_variable_list_node *val_list_p, VAL_DESCR *val_desc_p, OID *obj_oid_p,
			     QFILE_TUPLE tuple)
  {
    int error = NO_ERROR;
    int index = 0;
    REGU_VARIABLE_LIST operand;

    if (m_stack == NULL)
      {
	// Check if the session is in an interrupting state by calling get_session()
	// TODO: Verify the behavior of get_session() and validate this code flow.
	session *sess = get_session ();
	if (sess && er_errid () == NO_ERROR)
	  {
	    // If send_data_to_client() fails, it means the connection with CAS has been disconnected.
	    // In this case, get_session() should return NULL.
	    // According to the current analysis, the following code should be unreachable.
	    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	  }

	return er_errid ();
      }

    cubthread::entry *m_thread_p = m_stack->get_thread_entry ();

    if (m_sig.has_args ())
      {
	DB_VALUE *value = NULL;

	operand = val_list_p;
	while (operand != NULL)
	  {
	    error = fetch_peek_dbval (m_thread_p, &operand->value, val_desc_p, NULL, obj_oid_p, tuple, &value);
	    if (error != NO_ERROR)
	      {
		m_args.clear ();
		break;
	      }

	    if (is_supported_dbtype (*value) == false)
	      {
		er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_NOT_SUPPORTED_ARG_TYPE, 1,
			pr_type_name ((DB_TYPE) value->domain.general_info.type));
		m_stack->set_error_message (std::string (er_msg ()));
		error = er_errid ();
		break;
	      }

	    m_args.emplace_back (std::ref (*value));

	    operand = operand->next;
	  }
      }

    return error;
  }

  int
  executor::fetch_args_peek (std::vector <std::reference_wrapper <DB_VALUE>> args)
  {
    assert (m_args.empty ());
    m_args = args;
    for (const DB_VALUE &val : m_args)
      {
	if (is_supported_dbtype (val) == false)
	  {
	    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_NOT_SUPPORTED_ARG_TYPE, 1,
		    pr_type_name ((DB_TYPE) val.domain.general_info.type));
	    m_stack->set_error_message (std::string (er_msg ()));
	    return er_errid ();
	  }
      }

    return append_missing_default_args ();
  }

  bool
  executor::is_supported_dbtype (const DB_VALUE &value)
  {
    bool res = false;
    switch (DB_VALUE_TYPE (&value))
      {
      case DB_TYPE_INTEGER:
      case DB_TYPE_SHORT:
      case DB_TYPE_BIGINT:
      case DB_TYPE_FLOAT:
      case DB_TYPE_DOUBLE:
      case DB_TYPE_MONETARY:
      case DB_TYPE_NUMERIC:
      case DB_TYPE_CHAR:
      case DB_TYPE_STRING:
      case DB_TYPE_DATE:
      case DB_TYPE_TIME:
      case DB_TYPE_TIMESTAMP:
      case DB_TYPE_DATETIME:
      case DB_TYPE_SET:
      case DB_TYPE_MULTISET:
      case DB_TYPE_SEQUENCE:
      case DB_TYPE_OID:
      case DB_TYPE_OBJECT:
      case DB_TYPE_RESULTSET:
      case DB_TYPE_NULL:
	res = true;
	break;
      // unsupported types
      case DB_TYPE_BIT:
      case DB_TYPE_VARBIT:
      case DB_TYPE_TABLE:
      case DB_TYPE_BLOB:
      case DB_TYPE_CLOB:
      case DB_TYPE_TIMESTAMPTZ:
      case DB_TYPE_TIMESTAMPLTZ:
      case DB_TYPE_DATETIMETZ:
      case DB_TYPE_DATETIMELTZ:
      case DB_TYPE_JSON:
      case DB_TYPE_ENUMERATION:
	res = false;
	break;

      // obsolete, internal, unused type
      case DB_TYPE_ELO:
      case DB_TYPE_VARIABLE:
      case DB_TYPE_SUB:
      case DB_TYPE_POINTER:
      case DB_TYPE_ERROR:
      case DB_TYPE_VOBJ:
      case DB_TYPE_DB_VALUE:
      case DB_TYPE_MIDXKEY:
      default:
	assert (false);
	break;
      }
    return res;
  }

  int
  executor::execute (DB_VALUE &value)
  {
    int error = NO_ERROR;

    if (m_stack == NULL)
      {
	// Check if the session is in an interrupting state by calling get_session()
	// TODO: Verify the behavior of get_session() and validate this code flow.
	session *sess = get_session ();
	if (sess && er_errid () == NO_ERROR)
	  {
	    // If send_data_to_client() fails, it means the connection with CAS has been disconnected.
	    // In this case, get_session() should return NULL.
	    // According to the current analysis, the following code should be unreachable.
	    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	  }

	return er_errid ();
      }

    // execution rights
    assert (m_sig.auth != NULL);
    error = change_exec_rights (m_sig.auth);
    if (error != NO_ERROR)
      {
	goto exit;
      }

    error = request_invoke_command ();
    if (error != NO_ERROR)
      {
	goto exit;
      }

    error = response_invoke_command (value);
    if (error != NO_ERROR)
      {
	goto exit;
      }

exit:
    if (m_stack != NULL)
      {
	m_stack->reset_query_handlers ();
      }

    // restore execution rights
    if (change_exec_rights (NULL) != NO_ERROR)
      {
	error = er_errid ();
      }
    return error;
  }

  // runtime
  int
  executor::change_exec_rights (const char *auth_name)
  {
    int error = NO_ERROR;
    int is_restore = (auth_name == NULL) ? 1 : 0;

    if (is_restore == 0)
      {
	error = m_stack->send_data_to_client (METHOD_CALLBACK_CHANGE_RIGHTS, is_restore, std::string (auth_name));
      }
    else
      {
	error = m_stack->send_data_to_client (METHOD_CALLBACK_CHANGE_RIGHTS, is_restore);
      }

    if (error != NO_ERROR)
      {
	// Check if the session is in an interrupting state by calling get_session()
	// TODO: Verify the behavior of get_session() and validate this code flow.
	session *sess = get_session ();
	if (sess && er_errid () == NO_ERROR)
	  {
	    // If send_data_to_client() fails, it means the connection with CAS has been disconnected.
	    // In this case, get_session() should return NULL.
	    // According to the current analysis, the following code should be unreachable.
	    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	  }

	error = er_errid ();
      }

    return error;
  }

  int
  executor::request_invoke_command ()
  {
    int error = NO_ERROR;

    TRANID tid = m_stack->get_tran_id ();

    m_stack->set_java_command (SP_CODE_INVOKE);

    // get changed session parameters
    cubpl::session *sess = get_session ();
    std::vector<sys_param> session_params;
    if (sess)
      {
	session_params = sess->obtain_session_parameters (m_stack->get_connection ());
      }
    // handling 'else' is not required because send_data_to_java will handle the case when sess is not found

    prepare_args prepare_arg ((std::uint64_t) this, tid, METHOD_TYPE_PLCSQL, m_args);
    invoke_java invoke_arg (tid, &m_sig,
			    (m_sig.type == PL_TYPE_PLCSQL) ? true : prm_get_bool_value (PRM_ID_PL_TRANSACTION_CONTROL));

    error = m_stack->send_data_to_java (session_params, prepare_arg, invoke_arg);
    return error;
  }

  void
  executor::handle_type_resultset (DB_VALUE &returnval)
  {
    if (db_value_type (&returnval) == DB_TYPE_RESULTSET)
      {
	std::uint64_t query_id = db_get_resultset (&returnval);
	// qfile_update_qlist_count (thread_p, m_list_id, -1);
	m_stack->promote_to_session_cursor (query_id);
      }
  }

  int
  executor::response_invoke_command (DB_VALUE &value)
  {
    int error_code = NO_ERROR;
    int start_code = -1;

    // response loop
    do
      {
	cubmem::block response_blk;
	error_code = m_stack->read_data_from_java (response_blk);
	if (error_code != NO_ERROR)
	  {
	    break;
	  }

	if (!response_blk.is_valid ())
	  {
	    error_code = ER_SP_NETWORK_ERROR;
	    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_code, 1, sizeof (int));
	    break;
	  }

	cubpacking::unpacker unpacker (response_blk);
	unpacker.unpack_int (start_code);

	(void) m_stack->read_payload_block (unpacker);

	/* processing */
	if (start_code == SP_CODE_INTERNAL_JDBC)
	  {
	    error_code = response_callback_command ();
	  }
	else if (start_code == SP_CODE_RESULT || start_code == SP_CODE_ERROR)
	  {
	    error_code = response_result (start_code, value);
	  }
	else
	  {
	    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_NETWORK_ERROR, 1,
		    start_code);
	    error_code = ER_SP_NETWORK_ERROR;
	  }

	if (m_stack->get_data_queue ().empty() == false)
	  {
	    m_stack->get_data_queue ().pop ();
	  }

	// free response block
	response_blk.freemem ();
      }
    while (error_code == NO_ERROR && start_code == SP_CODE_INTERNAL_JDBC);

    return error_code;
  }

  int
  executor::response_result (int code, DB_VALUE &returnval)
  {
    // check queue
    if (m_stack->get_data_queue().empty() == true)
      {
	return ER_FAILED;
      }

    cubmem::block &blk = m_stack->get_data_queue().front ();
    packing_unpacker unpacker (blk);

    if (code == SP_CODE_RESULT)
      {
	dbvalue_java value_unpacker;
	db_make_null (&returnval);
	value_unpacker.value = &returnval;
	value_unpacker.unpack (unpacker);

	handle_type_resultset (returnval);

	for (int i = 0; i < m_sig.arg.arg_size; i++)
	  {
	    DB_VALUE out_val;
	    db_make_null (&out_val);
	    if (m_sig.arg.arg_mode[i] != SP_MODE_IN)
	      {
		value_unpacker.value = &out_val;
		value_unpacker.unpack (unpacker);
		m_out_args.emplace_back (out_val);

		handle_type_resultset (out_val);
	      }
	  }
	return NO_ERROR;
      }
    else if (code == SP_CODE_ERROR)
      {
	std::string error_msg;
	unpacker.unpack_string (error_msg);
	m_stack->set_error_message (error_msg);
	return ER_SP_EXECUTE_ERROR;
      }
    else
      {
	// it is handled in response_invoke_command
	assert (false);
	return ER_FAILED;
      }

    return NO_ERROR;
  }

  int
  executor::response_callback_command ()
  {
    int error_code = NO_ERROR;
    // check queue
    if (m_stack->get_data_queue().empty() == true)
      {
	return ER_FAILED;
      }

    cubmem::block &blk = m_stack->get_data_queue().front ();
    packing_unpacker unpacker (blk);
    cubthread::entry &thread_ref = *m_stack->get_thread_entry ();

    int code;
    unpacker.unpack_int (code);

    switch (code)
      {
      /* NOTE: we don't need to implement it
      case METHOD_CALLBACK_GET_DB_VERSION:
      break;
      */

      case METHOD_CALLBACK_GET_DB_PARAMETER:
	error_code = callback_get_db_parameter (thread_ref, unpacker);
	break;

      case METHOD_CALLBACK_QUERY_PREPARE:
	error_code = callback_prepare (thread_ref, unpacker);
	break;

      case METHOD_CALLBACK_QUERY_EXECUTE:
	error_code = callback_execute (thread_ref, unpacker);
	break;

      case METHOD_CALLBACK_FETCH:
	error_code = callback_fetch (thread_ref, unpacker);
	break;

      case METHOD_CALLBACK_OID_GET:
	error_code = callback_oid_get (thread_ref, unpacker);
	break;

      case METHOD_CALLBACK_OID_PUT:
	error_code = callback_oid_put (thread_ref, unpacker);
	break;

      case METHOD_CALLBACK_OID_CMD:
	error_code = callback_oid_cmd (thread_ref, unpacker);
	break;

      case METHOD_CALLBACK_COLLECTION:
	error_code = callback_collection_cmd (thread_ref, unpacker);
	break;

      case METHOD_CALLBACK_MAKE_OUT_RS:
	error_code = callback_make_outresult (thread_ref, unpacker);
	break;

      case METHOD_CALLBACK_GET_GENERATED_KEYS:
	error_code = callback_get_generated_keys (thread_ref, unpacker);
	break;
      case METHOD_CALLBACK_END_TRANSACTION:
	error_code = callback_end_transaction (thread_ref, unpacker);
	break;
      case METHOD_CALLBACK_GET_CODE_ATTR:
	error_code = callback_get_code_attr (thread_ref, unpacker);
	break;

      case METHOD_CALLBACK_SET_PL_SESSION_PARAM:
	error_code = callback_set_pl_session_param (thread_ref, unpacker);
	break;
      default:
	// TODO: not implemented yet, do we need error handling?
	assert (false);
	error_code = ER_FAILED;
	break;
      }

    return error_code;
  }

  std::vector <DB_VALUE> &
  executor::get_out_args ()
  {
    return m_out_args;
  }

  execution_stack *
  executor::get_stack ()
  {
    return m_stack;
  }

  int
  executor::callback_get_db_parameter (cubthread::entry &thread_ref, packing_unpacker &unpacker)
  {
    cubpl::session *pl_session = cubpl::get_session ();
    if (!pl_session)
      {
	return ER_SES_SESSION_EXPIRED;
      }

    int error = NO_ERROR;
    int code = METHOD_CALLBACK_GET_DB_PARAMETER;

    db_parameter_info *parameter_info = pl_session->get_db_parameter_info ();
    if (parameter_info == nullptr)
      {
	int tran_index = LOG_FIND_THREAD_TRAN_INDEX (m_stack->get_thread_entry());
	parameter_info = new db_parameter_info ();

	parameter_info->tran_isolation = logtb_find_isolation (tran_index);
	parameter_info->wait_msec = logtb_find_wait_msecs (tran_index);
	logtb_get_client_ids (tran_index, &parameter_info->client_ids);

	pl_session->set_db_parameter_info (parameter_info);
      }

    cubmem::block blk;
    if (parameter_info)
      {
	blk = std::move (pack_data_block (METHOD_RESPONSE_SUCCESS, *parameter_info));
      }
    else
      {
	blk = std::move (pack_data_block (METHOD_RESPONSE_ERROR, ER_FAILED, std::string ("unknown error"),
					  ARG_FILE_LINE));
      }

    if (blk.is_valid ())
      {
	m_stack->send_data_to_java (blk);
	blk.freemem ();
      }

    return error;
  }

  int
  executor::callback_prepare (cubthread::entry &thread_ref, packing_unpacker &unpacker)
  {
    int error = NO_ERROR;
    int code = METHOD_CALLBACK_QUERY_PREPARE;
    std::string sql;
    int flag;

    unpacker.unpack_all (sql, flag);

    auto get_prepare_info = [&] (const cubmem::block & b)
    {
      packing_unpacker unpacker (b.ptr, (size_t) b.dim);

      int res_code;
      unpacker.unpack_int (res_code);

      if (res_code == METHOD_RESPONSE_SUCCESS)
	{
	  prepare_info info;
	  info.unpack (unpacker);

	  m_stack->add_query_handler (info.handle_id);
	}

      m_stack->send_data_to_java (b);

      return error;
    };

    error = m_stack->send_data_to_client_recv (get_prepare_info, code, sql, flag, m_stack->get_tran_id ());
    return error;
  }

  int
  executor::callback_execute (cubthread::entry &thread_ref, packing_unpacker &unpacker)
  {
    int error = NO_ERROR;
    int code = METHOD_CALLBACK_QUERY_EXECUTE;
    execute_request request;

    unpacker.unpack_all (request);
    request.has_parameter = 1;

    auto get_execute_info = [&] (const cubmem::block & b)
    {
      packing_unpacker unpacker (b.ptr, (size_t) b.dim);

      int res_code;
      unpacker.unpack_int (res_code);

      if (res_code == METHOD_RESPONSE_SUCCESS)
	{
	  execute_info info;
	  info.unpack (unpacker);

	  query_result_info &current_result_info = info.qresult_info;
	  int stmt_type = current_result_info.stmt_type;
	  if (stmt_type == CUBRID_STMT_SELECT)
	    {
	      std::uint64_t qid = current_result_info.query_id;
	      if (current_result_info.tuple_count > 0)
		{
		  int hid = info.handle_id;
		  bool is_oid_included = current_result_info.include_oid;
		  (void) m_stack->add_cursor (hid, qid, is_oid_included);
		}
	      else
		{
		  QMGR_QUERY_ENTRY *query_entry = qmgr_get_query_entry (&thread_ref, qid, NULL_TRAN_INDEX);
		  if (query_entry)
		    {
		      // Since the list was not created in this thread,
		      // incrementing the count of the list (m_qlist_count) is required
		      // to make the assertion on m_qlist_count in qexec_execute_query() hold
		      qfile_update_qlist_count (&thread_ref, query_entry->list_id, 1);
		      qfile_close_list (&thread_ref, query_entry->list_id);
		    }

		  xqmgr_end_query (&thread_ref, qid);
		}
	    }
	}

      error = m_stack->send_data_to_java (b);
      return error;
    };

    error = m_stack->send_data_to_client_recv (get_execute_info, code, request);
    request.clear ();

    return error;
  }

  int
  executor::callback_fetch (cubthread::entry &thread_ref, packing_unpacker &unpacker)
  {
    int error = NO_ERROR;
    int code = METHOD_CALLBACK_FETCH;
    std::uint64_t qid;
    int pos;
    int fetch_count;
    int fetch_flag;

    unpacker.unpack_all (qid, pos, fetch_count, fetch_flag);

    /* find query cursor */
    query_cursor *cursor = m_stack->get_cursor (qid);
    if (cursor == nullptr)
      {
	cubmem::block b = std::move (pack_data_block (METHOD_RESPONSE_ERROR, ER_SP_INVALID_CURSOR,
				     std::string ("cursor closed"), ARG_FILE_LINE));
	error = m_stack->send_data_to_java (b);
	return error;
      }

    if (cursor->get_is_opened () == false)
      {
	cursor->open ();
      }

    cursor->set_fetch_count (fetch_count);

    fetch_info info;

    SCAN_CODE s_code = S_SUCCESS;

    /* Most cases, fetch_count will be the same value
     * To handle an invalid value of fetch_count is set at `cursor->set_fetch_count (fetch_count);`
     * Here, I'm going to get the fetch_count from the getter again.
    */
    fetch_count = cursor->get_fetch_count ();

    int start_index = cursor->get_current_index ();
    while (s_code == S_SUCCESS)
      {
	s_code = cursor->next_row ();
	if (s_code == S_END || s_code == S_ERROR)
	  {
	    break;
	  }

	int tuple_index = cursor->get_current_index ();
	std::vector<DB_VALUE> tuple_values = cursor->get_current_tuple ();

	if (cursor->get_is_oid_included())
	  {
	    /* FIXME!!: For more optimized way, refactoring method_query_cursor is needed */
	    OID *oid = cursor->get_current_oid ();
	    std::vector<DB_VALUE> sub_vector = {tuple_values.begin() + 1, tuple_values.end ()};
	    info.tuples.emplace_back (tuple_index, sub_vector, *oid);
	  }
	else
	  {
	    info.tuples.emplace_back (tuple_index, tuple_values);
	  }

	if (tuple_index - start_index >= fetch_count - 1)
	  {
	    break;
	  }
      }

    cubmem::block blk;
    if (s_code != S_ERROR)
      {
	blk = std::move (pack_data_block (METHOD_RESPONSE_SUCCESS, info));
      }
    else
      {
	blk = std::move (pack_data_block (METHOD_RESPONSE_ERROR, ER_SP_INVALID_CURSOR,
					  std::string ("cursor closed"), ARG_FILE_LINE));
      }

    error = m_stack->send_data_to_java (blk);
    blk.freemem ();
    return error;
  }

  int
  executor::callback_oid_get (cubthread::entry &thread_ref, packing_unpacker &unpacker)
  {
    int error = NO_ERROR;
    int code = METHOD_CALLBACK_OID_GET;
    oid_get_request request;
    request.unpack (unpacker);

    auto java_lambda = [&] (const cubmem::block & b)
    {
      return m_stack->send_data_to_java (b);
    };

    error = m_stack->send_data_to_client_recv (java_lambda, code, request);
    return error;
  }

  int
  executor::callback_oid_put (cubthread::entry &thread_ref, packing_unpacker &unpacker)
  {
    int error = NO_ERROR;
    int code = METHOD_CALLBACK_OID_PUT;
    oid_put_request request;
    request.is_compatible_java = true;
    request.unpack (unpacker);
    request.is_compatible_java = false;

    auto java_lambda = [&] (const cubmem::block & b)
    {
      return m_stack->send_data_to_java (b);
    };

    error = m_stack->send_data_to_client_recv (java_lambda, code, request);
    return error;
  }

  int
  executor::callback_oid_cmd (cubthread::entry &thread_ref, packing_unpacker &unpacker)
  {
    int error = NO_ERROR;
    int code = METHOD_CALLBACK_OID_CMD;
    int command;
    OID oid;
    unpacker.unpack_all (command, oid);

    auto java_lambda = [&] (const cubmem::block & b)
    {
      return m_stack->send_data_to_java (b);
    };

    error = m_stack->send_data_to_client_recv (java_lambda, code, command, oid);
    return error;
  }

  int
  executor::callback_collection_cmd (cubthread::entry &thread_ref, packing_unpacker &unpacker)
  {
    int error = NO_ERROR;

    int code = METHOD_CALLBACK_COLLECTION;
    collection_cmd_request request;
    request.is_compatible_java = true;
    request.unpack (unpacker);

    request.is_compatible_java = false;

    auto java_lambda = [&] (const cubmem::block & b)
    {
      return m_stack->send_data_to_java (b);
    };

    error = m_stack->send_data_to_client_recv (java_lambda, code, request);
    return error;
  }

  int
  executor::callback_make_outresult (cubthread::entry &thread_ref, packing_unpacker &unpacker)
  {
    int error = NO_ERROR;

    int code = METHOD_CALLBACK_MAKE_OUT_RS;
    uint64_t query_id;
    unpacker.unpack_all (query_id);

    auto get_make_outresult_info = [&] (const cubmem::block & b)
    {
      packing_unpacker unpacker (b.ptr, (size_t) b.dim);

      int res_code;
      unpacker.unpack_int (res_code);

      if (res_code == METHOD_RESPONSE_SUCCESS)
	{
	  make_outresult_info info;
	  info.unpack (unpacker);

	  const query_result_info &current_result_info = info.qresult_info;
	  query_cursor *cursor = m_stack->get_cursor (current_result_info.query_id);
	  if (cursor)
	    {
	      cursor->change_owner (&thread_ref);
	      return m_stack->send_data_to_java (b);
	    }
	  else
	    {
	      assert (false);
	      return ER_FAILED;
	    }
	}
      else
	{
	  return ER_FAILED;
	}
    };

    error = m_stack->send_data_to_client_recv (get_make_outresult_info, code, query_id);

    return error;
  }

  int
  executor::callback_get_generated_keys (cubthread::entry &thread_ref, packing_unpacker &unpacker)
  {
    int error = NO_ERROR;
    int code = METHOD_CALLBACK_GET_GENERATED_KEYS;
    int handler_id;
    unpacker.unpack_all (handler_id);

    auto java_lambda = [&] (const cubmem::block & b)
    {
      return m_stack->send_data_to_java (b);
    };

    error = m_stack->send_data_to_client_recv (java_lambda, code, handler_id);
    return error;
  }

  int
  executor::callback_end_transaction (cubthread::entry &thread_ref, packing_unpacker &unpacker)
  {
    int error = NO_ERROR;
    int code = METHOD_CALLBACK_END_TRANSACTION;
    int command; // commit=1 or abort=2

    unpacker.unpack_all (command);
    if (command == 2)
      {
	cubpl::session *pl_session = cubpl::get_session ();
	if (pl_session)
	  {
	    pl_session->destroy_all_cursors();
	  }
	else
	  {
	    return ER_SES_SESSION_EXPIRED;
	  }
      }

    auto java_lambda = [&] (const cubmem::block & b)
    {
      return m_stack->send_data_to_java (b);
    };

    error = m_stack->send_data_to_client_recv (java_lambda, code, command);
    return error;
  }

  int
  executor::callback_get_code_attr (cubthread::entry &thread_ref, packing_unpacker &unpacker)
  {
    int error = NO_ERROR;
    int code = METHOD_CALLBACK_GET_CODE_ATTR;

    std::string attr_name;

    DB_VALUE res;
    db_make_null (&res);
    unpacker.unpack_all (attr_name);

    OID *code_oid = &m_sig.ext.sp.code_oid;
    if (OID_ISNULL (code_oid))
      {
	error = ER_FAILED;
      }

    if (error == NO_ERROR)
      {
	error = sp_get_code_attr (&thread_ref, attr_name, code_oid, &res);
      }

    cubmem::block blk;
    if (error == NO_ERROR)
      {
	dbvalue_java java_packer;
	java_packer.value = &res;

	blk = std::move (pack_data_block (error, java_packer));
      }
    else
      {
	blk = std::move (pack_data_block (error));
      }

    db_value_clear (&res);

    error = m_stack->send_data_to_java (blk);
    blk.freemem ();

    return error;
  }

  int
  executor::callback_set_pl_session_param (cubthread::entry &thread_ref, packing_unpacker &unpacker)
  {
    cubpl::session *pl_session = cubpl::get_session ();
    if (!pl_session)
      {
	return ER_SES_SESSION_EXPIRED;
      }

    int error = NO_ERROR;
    int code = METHOD_CALLBACK_SET_PL_SESSION_PARAM;

    std::vector<sys_param> params;
    unpacker.unpack_all (params);

    for (const auto &prm : params)
      {
	if (prm.prm_id < static_cast<int> (sys_param_id::PRM_ID_BEGIN))
	  {
	    continue;
	  }
	else
	  {
	    pl_session->mark_session_param_changed (prm.prm_id);
	    pl_session->set_session_param (prm);
	  }
      }

    cubmem::block blk = std::move (pack_data_block (METHOD_RESPONSE_SUCCESS));
    if (blk.is_valid ())
      {
	m_stack->send_data_to_java (blk);
	blk.freemem ();
      }

    return error;
  }
}
