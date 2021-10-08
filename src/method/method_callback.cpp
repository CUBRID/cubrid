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
#include "network_interface_cl.h"

namespace cubmethod
{
  callback_handler::callback_handler (int max_query_handler)
  {
    m_query_handlers.resize (max_query_handler, nullptr);
  }

  void
  callback_handler::set_server_info (int rc, char *host)
  {
    m_rid = rc;
    m_host = host;
  }

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

    /* find in m_query_handler_map */
    query_handler *handler = nullptr;
    for (auto it = m_query_handler_map.lower_bound(sql); it != m_query_handler_map.upper_bound(sql); it++) 
    {
        handler = find_query_handler (it->second);
        if (handler != nullptr && handler->get_is_occupied() == false) {
          prepare_info info;
          handler->get_prepare_info (info);
          handler->set_is_occupied (true);
          info.handle_id = it->second;
          int error = send_packable_object_to_server (info);
          return error;
        }
    }

    /* not found in statement handler */
    int handle_id = new_query_handler (); /* new handler */
    if (handle_id < 0)
      {
  // TODO
  // error handling
      }

    handler = find_query_handler (handle_id);
    if (handler == nullptr)
      {
  // TODO
  // error handling
      }

    prepare_info info = handler->prepare (sql, flag);
    info.handle_id = handle_id;
    
    // add to statement handler cache
    m_query_handler_map.emplace (sql, handle_id);

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

  int callback_handler::schema_info (packing_unpacker &unpacker)
  {
    // TODO
    int error = NO_ERROR;
    return error;
  }

//////////////////////////////////////////////////////////////////////////
// OID
//////////////////////////////////////////////////////////////////////////
  int
  callback_handler::check_object (DB_OBJECT *obj)
  {
    // TODO
    int error = NO_ERROR;

    if (obj == NULL)
      {

      }

    er_clear ();
    error = db_is_instance (obj);
    if (error < 0)
      {
	return error;
      }
    else if (error > 0)
      {
	return 0;
      }
    else
      {
	error = db_error_code ();
	if (error < 0)
	  {
	    return error;
	  }

	// return CAS_ER_OBJECT;
      }

    return error;
  }

#if 0
  int
  callback_handler::oid_get (OID oid)
  {
    int error = NO_ERROR;

    DB_OBJECT *obj = db_object (&oid);
    error = check_object (obj);
    if (error < 0)
      {
	// TODO : error handling
      }

    // get attr name
    std::string class_name;
    std::vector<std::string> attr_names;

    const char *cname = db_get_class_name (obj);
    if (cname != NULL)
      {
	class_name.assign (cname);
      }

    // error = oid_attr_info_set (net_buf, obj, attr_num, attr_name);
    if (error < 0)
      {

      }
    /*
    if (oid_data_set (obj, attr_num, attr_name) < 0)
    {

    }
    */

    return error;
  }

  int
  callback_handler::oid_put (OID oid)
  {
    int error = NO_ERROR;

    DB_OBJECT *obj = db_object (&oid);
    error = check_object (obj);
    if (error < 0)
      {
	// TODO : error handling
      }

    DB_OTMPL *otmpl = dbt_edit_object (obj);
    if (otmpl == NULL)
      {
	// TODO : error handling
	error = db_error_code ();
      }

    /* TODO */

    obj = dbt_finish_object (otmpl);
    if (obj == NULL)
      {
	// TODO : error handling
	error = db_error_code ();
      }
    return error;
  }

  int
  callback_handler::oid_cmd (char cmd, OID oid)
  {
    // TODO
    int error = NO_ERROR;
    DB_OBJECT *obj = db_object (&oid);

    if (cmd != OID_IS_INSTANCE)
      {
	error = check_object (obj);
	if (error < 0)
	  {
	    // TODO : error handling
	  }
      }

    if (cmd == OID_DROP)
      {

      }
    else if (cmd == OID_IS_INSTANCE)
      {

      }
    else if (cmd == OID_LOCK_READ)
      {

      }
    else if (cmd == OID_LOCK_WRITE)
      {

      }
    else if (cmd == OID_CLASS_NAME)
      {

      }
    else
      {

      }

    if (error < 0)
      {

      }
    else
      {
	if (cmd == OID_CLASS_NAME)
	  {

	  }
      }
    return error;
  }

//////////////////////////////////////////////////////////////////////////
// Collection
//////////////////////////////////////////////////////////////////////////


  int
  callback_handler::col_get (DB_COLLECTION *col, char col_type, char ele_type, DB_DOMAIN *ele_domain)
  {
    int error = NO_ERROR;
    int col_size, i;

    if (col == NULL)
      {
	col_size = -1;
      }
    else
      {
	col_size = db_col_size (col);
      }

    // net_buf_column_info_set

    DB_VALUE ele_val;
    if (col_size > 0)
      {
	for (i = 0; i < col_size; i++)
	  {
	    if (db_col_get (col, i, &ele_val) < 0)
	      {
		db_make_null (&ele_val);
	      }
	    // dbval_to_net_buf (&ele_val, net_buf, 1, 0, 0);
	    // db_value_clear (&ele_val);
	  }
      }

    return error;
  }

  int
  callback_handler::col_size (DB_COLLECTION *col)
  {
    int error = NO_ERROR;
    int col_size;
    if (col == NULL)
      {
	col_size = -1;
      }
    else
      {
	col_size = db_col_size (col);
      }

    //net_buf_cp_int (net_buf, 0, NULL);	/* result code */
    //net_buf_cp_int (net_buf, col_size, NULL);	/* result msg */

    return error;
  }

  int
  callback_handler::col_set_drop (DB_COLLECTION *col, DB_VALUE *ele_val)
  {
    if (col != NULL)
      {
	int error = db_set_drop (col, ele_val);
	if (error < 0)
	  {
	    // TODO: error handling
	    return ER_FAILED;
	  }
      }
    return NO_ERROR;
  }

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
