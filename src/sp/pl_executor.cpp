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

#include "method_struct_invoke.hpp"
#include "method_connection_java.hpp"
#include "method_struct_value.hpp"
#include "jsp_comm.h"

namespace cubpl
{
  invoke_java::invoke_java (uint64_t id, int tid, pl_signature *sig, bool tc)
    : g_id (id)
    , tran_id (tid)
  {
    signature.assign (sig->name);
    auth.assign (sig->auth);
    lang = sig->type;
    result_type = sig->result_type;

    pl_arg &arg = sig->arg;
    num_args = arg.arg_size;
    arg_pos.resize (num_args);
    arg_mode.resize (num_args);
    arg_type.resize (num_args);
    arg_default_size.resize (num_args);
    arg_default.resize (num_args);

    for (int i = 0; i < num_args; i++)
      {
	arg_pos[i] = i;
	arg_mode[i] = arg.arg_mode[i];
	arg_type[i] = arg.arg_type[i];
	arg_default_size[i] = arg.arg_default_value_size[i];
	arg_default[i] = arg.arg_default_value[i];
      }

    transaction_control = tc;
  }

  void
  invoke_java::pack (cubpacking::packer &serializator) const
  {
    serializator.pack_bigint (g_id);
    serializator.pack_int (tran_id);
    serializator.pack_string (signature);
    serializator.pack_string (auth);
    serializator.pack_int (lang);
    serializator.pack_int (num_args);

    for (int i = 0; i < num_args; i++)
      {
	serializator.pack_int (arg_pos[i]);
	serializator.pack_int (arg_mode[i]);
	serializator.pack_int (arg_type[i]);
	serializator.pack_int (arg_default_size[i]);
	if (arg_default_size[i] > 0)
	  {
	    serializator.pack_c_string (arg_default[i], arg_default_size[i]);
	  }
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
    size_t size = serializator.get_packed_bigint_size (start_offset); // group_id
    size += serializator.get_packed_int_size (size); // tran_id
    size += serializator.get_packed_string_size (signature, size); // signature
    size += serializator.get_packed_string_size (auth, size); // auth
    size += serializator.get_packed_int_size (size); // lang
    size += serializator.get_packed_int_size (size); // num_args

    for (int i = 0; i < num_args; i++)
      {
	size += serializator.get_packed_int_size (size); // arg_pos
	size += serializator.get_packed_int_size (size); // arg_mode
	size += serializator.get_packed_int_size (size); // arg_type
	size += serializator.get_packed_int_size (size); // arg_default_size
	if (arg_default_size[i] > 0)
	  {
	    size += serializator.get_packed_c_string_size (arg_default[i], arg_default_size[i], size); // arg_default
	  }
      }

    size += serializator.get_packed_int_size (size); // return_type
    size += serializator.get_packed_bool_size (size); // transaction_control
    return size;
  }


//////////////////////////////////////////////////
  executor::executor (pl_signature &sig)
    : m_sig (sig)
  {
    m_stack = get_session ()->create_and_push_stack (nullptr);
  }

  executor::~executor ()
  {
    assert (m_stack != nullptr);

    (void) get_session ()->pop_and_destroy_stack (m_stack);
  }

  int
  executor::fetch_args_peek (regu_variable_list_node *val_list_p, VAL_DESCR *val_desc_p, OID *obj_oid_p,
			     QFILE_TUPLE tuple)
  {
    int error = NO_ERROR;
    int index = 0;
    REGU_VARIABLE_LIST operand;

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

	    m_args.push_back (std::ref (*value));

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
    return NO_ERROR;
  }

  int
  executor::execute (DB_VALUE &value)
  {
    int error = NO_ERROR;

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
    // restore execution rights
    change_exec_rights (NULL);

    return error;
  }

  // runtime
  int
  executor::change_exec_rights (const char *auth_name)
  {
    int error = NO_ERROR;
    cubthread::entry *thread_p = m_stack->get_thread_entry ();
    int is_restore = (auth_name == NULL) ? 1 : 0;

    error = m_stack->send_data_to_client (thread_p, METHOD_CALLBACK_CHANGE_RIGHTS, is_restore, auth_name);
    if (error != NO_ERROR)
      {
	return error;
      }

    return error;
  }

  int
  executor::request_invoke_command ()
  {
    int error = NO_ERROR;

    // prepare
    // 1) check supported dbtype
    // 2)

    SESSION_ID sid = get_session ()->get_id ();
    TRANID tid = m_stack->get_tran_id ();

    switch (m_sig.type)
      {
      case METHOD_TYPE_INSTANCE_METHOD:
      case METHOD_TYPE_CLASS_METHOD:
	assert (false); // TODO: port this
	break;
      case METHOD_TYPE_JAVA_SP:
      case METHOD_TYPE_PLCSQL:
      {
	cubmethod::header prepare_header (sid, SP_CODE_PREPARE_ARGS, m_stack->get_and_increment_request_id ());
	cubmethod::prepare_args prepare_arg ((std::uint64_t) this, tid, METHOD_TYPE_PLCSQL, m_args);

	cubmethod::header invoke_header (sid, SP_CODE_INVOKE, m_stack->get_and_increment_request_id ());
	invoke_java invoke_arg ((std::uint64_t) this, tid, &m_sig, true); // TODO

	error = mcon_send_data_to_java (m_stack->get_connection ()->get_socket (), prepare_header, prepare_arg, invoke_header,
					invoke_arg);
      }
      break;
      default:
	assert (false); // not implemented yet
	break;
      }

    return error;
  }

  int
  executor::response_invoke_command (DB_VALUE &value)
  {
    int error_code = NO_ERROR;
    int start_code;

    // response loop
    do
      {
	cubmem::block response_blk;
	error_code = cubmethod::mcon_read_data_from_java (m_stack->get_connection ()->get_socket (), response_blk);
	if (error_code != NO_ERROR)
	  {
	    break;
	  }

	cubpacking::unpacker unpacker (response_blk);
	unpacker.unpack_int (start_code);

	cubmem::block payload_blk = std::move (m_stack->get_payload_block (unpacker));
	m_stack->get_data_queue ().emplace (std::move (payload_blk));

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

	// free phase
	if (response_blk.is_valid ())
	  {
	    delete [] response_blk.ptr;
	    response_blk.ptr = NULL;
	    response_blk.dim = 0;
	  }
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
	cubmethod::dbvalue_java value_unpacker;
	db_make_null (&returnval);
	value_unpacker.value = &returnval;
	value_unpacker.unpack (unpacker);

	if (db_value_type (&returnval) == DB_TYPE_RESULTSET)
	  {
	    std::uint64_t query_id = db_get_resultset (&returnval);
	    m_stack->promote_to_session_cursor (query_id);
	  }

	// TODO: out_argument
      }
    else if (code == SP_CODE_ERROR)
      {
	std::string error_msg;
	unpacker.unpack_string (error_msg);
	m_stack->set_error_message (error_msg);
      }
    else
      {
	// it is handled in response_invoke_command
	assert (false);
      }

    return NO_ERROR;
  }

  int
  executor::response_callback_command ()
  {
    // check queue
    if (m_stack->get_data_queue().empty() == true)
      {
	return ER_FAILED;
      }

    cubmem::block &blk = m_stack->get_data_queue().front ();
    packing_unpacker unpacker (blk);

    int code;
    unpacker.unpack_int (code);

    return NO_ERROR;
  }
}

static bool
is_supported_dbtype (const DB_VALUE &value)
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
    case DB_TYPE_NCHAR:
    case DB_TYPE_VARNCHAR:
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