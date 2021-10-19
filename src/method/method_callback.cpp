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
#include "method_def.hpp"
#include "method_query_util.hpp"
#include "method_struct_oid_info.hpp"
#include "network_interface_cl.h"

#include "object_primitive.h"
#include "oid.h"

namespace cubmethod
{
  callback_handler::callback_handler (int max_query_handler)
  {
    m_query_handlers.resize (max_query_handler, nullptr);
    m_oid_handler = new oid_handler (m_error_ctx);
  }

  callback_handler::~callback_handler ()
  {
    delete m_oid_handler;
  }

  void
  callback_handler::set_server_info (int rc, char *host)
  {
    m_rid = rc;
    m_host = host;
  }

  template<typename ... Args>
  int
  callback_handler::send_packable_object_to_server (Args &&... args)
  {
    packing_packer packer;
    cubmem::extensible_block eb;

    packer.set_buffer_and_pack_all (eb, std::forward<Args> (args)...);

    int error = net_client_send_data (m_host, m_rid, eb.get_ptr (), packer.get_current_size ());
    if (error != NO_ERROR)
      {
	return ER_FAILED;
      }

    return NO_ERROR;
  }

  /*
  int
  callback_handler::send_packable_object_to_server (cubpacking::packable_object &object)
  {
    packing_packer packer;
    cubmem::extensible_block eb;

    packer.set_buffer_and_pack_all (eb, object);

    int error = net_client_send_data (m_host, m_rid, eb.get_ptr (), packer.get_current_size ());
    if (error != NO_ERROR)
      {
  return ER_FAILED;
      }

    return NO_ERROR;
  }
  */

  int
  callback_handler::callback_dispatch (packing_unpacker &unpacker)
  {
    UINT64 id;
    unpacker.unpack_bigint (id);

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
      default:
	assert (false);
	error = ER_FAILED;
	break;
      }

    return error;
  }

  int
  callback_handler::prepare (packing_unpacker &unpacker)
  {
    std::string sql;
    int flag;

    unpacker.unpack_string (sql);
    unpacker.unpack_int (flag);

    int handle_id = new_query_handler ();
    if (handle_id < 0)
      {
	// TODO
	// error handling
      }

    query_handler *handler = find_query_handler (handle_id);
    if (handler == nullptr)
      {
	// TODO
	// error handling
      }

    prepare_info info = handler->prepare (sql, flag);
    info.handle_id = handle_id;

    int error = send_packable_object_to_server (info);
    return error;
  }

  int
  callback_handler::execute (packing_unpacker &unpacker)
  {
    execute_request request;
    request.unpack (unpacker);

    query_handler *handler = find_query_handler (request.handler_id);
    if (handler == nullptr)
      {
	// TODO
	// error handling
      }

    execute_info info = handler->execute (request.param_values, request.execute_flag, request.max_field, -1);
    int error = send_packable_object_to_server (info);
    return error;
  }

  int
  callback_handler::schema_info (packing_unpacker &unpacker)
  {
    // TODO
    int error = NO_ERROR;
    return error;
  }

//////////////////////////////////////////////////////////////////////////
// OID
//////////////////////////////////////////////////////////////////////////

  int
  callback_handler::oid_get (packing_unpacker &unpacker)
  {
    int error = NO_ERROR;

    oid_get_request request;
    request.unpack (unpacker);

    oid_get_info info = m_oid_handler->oid_get (request.oid, request.attr_names);

    error = send_packable_object_to_server (info);
    return error;
  }

  int
  callback_handler::oid_put (packing_unpacker &unpacker)
  {
    oid_put_request request;
    request.unpack (unpacker);

    int result = m_oid_handler->oid_put (request.oid, request.attr_names, request.db_values);
    int error = send_packable_object_to_server (result);
    return error;
  }

  int
  callback_handler::oid_cmd (packing_unpacker &unpacker)
  {
    int cmd = OID_CMD_FIRST;
    unpacker.unpack_int (cmd);

    OID oid = OID_INITIALIZER;
    unpacker.unpack_oid (oid);

    std::string res; // result for OID_CLASS_NAME
    int error = m_oid_handler->oid_cmd (oid, cmd, res);
    if (error < 0)
      {
	// TODO : error handling
	// ERROR_INFO_SET (err_code, DBMS_ERROR_INDICATOR);
      }
    else
      {
	error = send_packable_object_to_server (error, res);
      }
    return error;
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

    int error = m_oid_handler->collection_cmd (request.oid, request.command, request.index, request.attr_name,
		request.value);
    if (error < 0)
      {
	// TODO : error handling
	// ERROR_INFO_SET (err_code, DBMS_ERROR_INDICATOR);
      }
    else
      {
	error = send_packable_object_to_server (error);
      }
    return error;
  }

#if 0

//////////////////////////////////////////////////////////////////////////
// LOB
//////////////////////////////////////////////////////////////////////////

  int
  callback_handler::lob_new (DB_TYPE lob_type)
  {
    int error = NO_ERROR;

    DB_VALUE lob_dbval;
    error = db_create_fbo (&lob_dbval, lob_type);
    if (error < 0)
      {
	// TODO : error handling
	return error;
      }

    lob_handle lhandle;
    DB_ELO *elo = db_get_elo (&lob_dbval);
    if (elo == NULL)
      {
	lhandle.lob_size = -1;
	lhandle.locator_size = 0;
	lhandle.locator = NULL;
      }
    else
      {
	lhandle.lob_size = elo->size;
	lhandle.locator_size = elo->locator ? strlen (elo->locator) + 1 : 0;
	/* including null character */
	lhandle.locator = elo->locator;
      }

    // TODO
    db_value_clear (&lob_dbval);
    return error;
  }

  int
  callback_handler::lob_write (DB_VALUE *lob_dbval, int64_t offset, int size, char *data)
  {
    int error = NO_ERROR;

    DB_ELO *elo = db_get_elo (lob_dbval);
    DB_BIGINT size_written;
    error = db_elo_write (elo, offset, data, size, &size_written);
    if (error < 0)
      {
	// TODO : error handling
	return error;
      }

    /* set result: on success, bytes written */
    // net_buf_cp_int (net_buf, (int) size_written, NULL);

    return error;
  }

  int
  callback_handler::lob_read (DB_TYPE lob_type)
  {
    int error = NO_ERROR;
    // TODO

    DB_VALUE lob_dbval;
    DB_ELO *elo = db_get_elo (&lob_dbval);

    /*
    DB_BIGINT size_read;
    error = db_elo_read (elo, offset, data, size, &size_read);
    if (error < 0)
    {
        // TODO : error handling
        return error;
    }
    */

    /* set result: on success, bytes read */
    // net_buf_cp_int (net_buf, (int) size_read, NULL);
    // net_buf->data_size += (int) size_read;

    return error;
  }
#endif

//////////////////////////////////////////////////////////////////////////
// Managing Query Handler Table
//////////////////////////////////////////////////////////////////////////

  int
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

    query_handler *handler = new query_handler (m_error_ctx, idx);
    if (handler == nullptr)
      {
	// TODO: error handling
	return -1;
      }

    if (idx < handler_size)
      {
	m_query_handlers[idx] = handler;
      }
    else
      {
	m_query_handlers.push_back (handler);
      }
    return idx;
  }

  query_handler *
  callback_handler::find_query_handler (int id)
  {
    if (id < 0 || id >= (int) m_query_handlers.size())
      {
	return nullptr;
      }

    return m_query_handlers[id];
  }

  void
  callback_handler::free_query_handle (int id)
  {
    if (id < 0 || id >= (int) m_query_handlers.size())
      {
	return;
      }

    if (m_query_handlers[id] != nullptr)
      {
	delete m_query_handlers[id];
	m_query_handlers[id] = nullptr;
      }
  }

  void
  callback_handler::free_query_handle_all ()
  {
    for (int i = 0; i < (int) m_query_handlers.size(); i++)
      {
	free_query_handle (i);
      }
  }
}
