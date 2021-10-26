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
    m_oid_handler = nullptr;
  }

  callback_handler::~callback_handler ()
  {
    if (m_oid_handler)
      {
	delete m_oid_handler;
      }
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

  int
  callback_handler::callback_dispatch (packing_unpacker &unpacker)
  {
    UINT64 id;
    int code;
    unpacker.unpack_all (id, code);

    m_error_ctx.clear ();

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
    unpacker.unpack_all (sql, flag);

    /* find in m_query_handler_map */
    prepare_info info;
    query_handler *handler = nullptr;
    for (auto it = m_query_handler_map.lower_bound (sql); it != m_query_handler_map.upper_bound (sql); it++)
      {
	handler = find_query_handler (it->second);
	if (handler != nullptr && handler->get_is_occupied() == false)
	  {
	    info.handle_id = it->second;
	    break;
	  }
	handler = nullptr;
      }

    if (handler != nullptr)
      {
	handler->get_prepare_info (info);
	handler->set_is_occupied (true);
      }
    else
      {
	/* not found in statement handler */
	int handle_id = new_query_handler (); /* new handler */
	if (handle_id < 0)
	  {
	    // TODO: proper error code
	    m_error_ctx.set_error (METHOD_CALLBACK_ER_NO_MORE_MEMORY, NULL, __FILE__, __LINE__);
	    return ER_FAILED;
	  }

	handler = find_query_handler (handle_id);
	if (handler == nullptr)
	  {
	    // TODO: proper error code
	    m_error_ctx.set_error (METHOD_CALLBACK_ER_INTERNAL, NULL, __FILE__, __LINE__);
	  }
	info = handler->prepare (sql, flag);
	info.handle_id = handle_id;
      }

    if (m_error_ctx.has_error())
      {
	return send_packable_object_to_server (METHOD_RESPONSE_ERROR, m_error_ctx.get_error(), m_error_ctx.get_error_msg());
      }
    else
      {
	// add to statement handler cache
	m_query_handler_map.emplace (sql, handler);
	return send_packable_object_to_server (METHOD_RESPONSE_SUCCESS, info);
      }
  }

  int
  callback_handler::execute (packing_unpacker &unpacker)
  {
    execute_request request;
    request.unpack (unpacker);

    query_handler *handler = find_query_handler (request.handler_id);
    if (handler == nullptr)
      {
	// TODO: proper error code
	m_error_ctx.set_error (METHOD_CALLBACK_ER_NO_MORE_MEMORY, NULL, __FILE__, __LINE__);
	return ER_FAILED;
      }

    execute_info info = handler->execute (request.param_values, request.execute_flag, request.max_field, -1);

    if (m_error_ctx.has_error())
      {
	return send_packable_object_to_server (METHOD_RESPONSE_ERROR, m_error_ctx.get_error(), m_error_ctx.get_error_msg());
      }
    else
      {
	return send_packable_object_to_server (METHOD_RESPONSE_SUCCESS, info);
      }

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
  callback_handler::new_oid_handler ()
  {
    if (m_oid_handler == nullptr)
      {
	m_oid_handler = new (std::nothrow) oid_handler (m_error_ctx);
	if (m_oid_handler == nullptr)
	  {
	    return ER_OUT_OF_VIRTUAL_MEMORY;
	  }
      }
    return NO_ERROR;
  }

  int
  callback_handler::oid_get (packing_unpacker &unpacker)
  {
    int error = NO_ERROR;

    oid_get_request request;
    request.unpack (unpacker);

    error = new_oid_handler ();
    if (error != NO_ERROR)
      {
	m_error_ctx.set_error (METHOD_CALLBACK_ER_NO_MORE_MEMORY, NULL, __FILE__, __LINE__);
      }
    else
      {
	oid_get_info info = m_oid_handler->oid_get (request.oid, request.attr_names);
	if (m_error_ctx.has_error())
	  {
	    return send_packable_object_to_server (METHOD_RESPONSE_ERROR, m_error_ctx.get_error(), m_error_ctx.get_error_msg());
	  }
	else
	  {
	    return send_packable_object_to_server (METHOD_RESPONSE_SUCCESS, info);
	  }
      }
    return send_packable_object_to_server (METHOD_RESPONSE_ERROR, m_error_ctx.get_error(), m_error_ctx.get_error_msg());
  }

  int
  callback_handler::oid_put (packing_unpacker &unpacker)
  {
    oid_put_request request;
    request.unpack (unpacker);

    int error = new_oid_handler ();
    if (error != NO_ERROR)
      {
	m_error_ctx.set_error (METHOD_CALLBACK_ER_NO_MORE_MEMORY, NULL, __FILE__, __LINE__);
      }
    else
      {
	int result = m_oid_handler->oid_put (request.oid, request.attr_names, request.db_values);
	if (!m_error_ctx.has_error())
	  {
	    return send_packable_object_to_server (METHOD_RESPONSE_SUCCESS, result);
	  }
      }
    return send_packable_object_to_server (METHOD_RESPONSE_ERROR, m_error_ctx.get_error(), m_error_ctx.get_error_msg());
  }

  int
  callback_handler::oid_cmd (packing_unpacker &unpacker)
  {
    int cmd = OID_CMD_FIRST;
    unpacker.unpack_int (cmd);

    OID oid = OID_INITIALIZER;
    unpacker.unpack_oid (oid);

    std::string res; // result for OID_CLASS_NAME

    int error = new_oid_handler ();
    if (error != NO_ERROR)
      {
	m_error_ctx.set_error (METHOD_CALLBACK_ER_NO_MORE_MEMORY, NULL, __FILE__, __LINE__);
      }
    else
      {
	int res_code = m_oid_handler->oid_cmd (oid, cmd, res);
	if (!m_error_ctx.has_error())
	  {
	    return send_packable_object_to_server (METHOD_RESPONSE_SUCCESS, res_code, res);
	  }
      }
    return send_packable_object_to_server (METHOD_RESPONSE_ERROR, m_error_ctx.get_error(), m_error_ctx.get_error_msg());
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
	return send_packable_object_to_server (METHOD_RESPONSE_ERROR, m_error_ctx.get_error(), m_error_ctx.get_error_msg());
      }
    else
      {
	return send_packable_object_to_server (METHOD_RESPONSE_SUCCESS, result);
      }
  }

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
	return ER_FAILED;
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
  callback_handler::free_query_handle (int id, bool is_free)
  {
    if (id < 0 || id >= (int) m_query_handlers.size())
      {
	return;
      }

    if (m_query_handlers[id] != nullptr)
      {
	if (is_free)
	  {
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
}
