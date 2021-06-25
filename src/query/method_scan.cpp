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

#include "method_scan.hpp"

#include "dbtype.h" /* db_value_* */
#include "object_representation.h" /* OR_ */
#include "list_file.h" /* qfile_ */
#include "method_def.hpp" /* METHOD_SUCCESS, METHOD_ERROR */

#if defined (SERVER_MODE)
#include "network.h" /* METHOD_CALL */
#include "network_interface_sr.h"
#include "memory_private_allocator.hpp" /* cubmem::PRIVATE_BLOCK_ALLOCATOR */
#include "packer.hpp" /* packing_packer */
#include "server_support.h" /* css_receive_data_from_client() */

#include "jsp_comm.h"
#include "jsp_sr.h"
#endif

int method_Num_method_jsp_calls = 0;

namespace cubscan
{
  namespace method
  {
    scanner::scanner ()
      : m_thread_p (nullptr)
      , m_method_sig_list (nullptr)
      , m_list_id (nullptr)
      , m_dbval_list (nullptr)
    {

    }

    int scanner::init (cubthread::entry *thread_p, METHOD_SIG_LIST *sig_list, qfile_list_id *list_id)
    {
      // check initialized
      if (m_thread_p != thread_p)
	{
	  m_thread_p = thread_p;
	}

      m_list_id = list_id;

      if (m_method_sig_list != sig_list)
	{
	  // TODO: interpret method signature and set internal representation for this class
	  m_method_sig_list = sig_list;

#if defined (SA_MODE)
	  m_result_vector.resize (m_method_sig_list->num_methods);
#endif

	  int arg_count = m_list_id->type_list.type_cnt;
	  m_arg_vector.resize (arg_count);
	  m_arg_dom_vector.resize (arg_count);

	  for (int i = 0; i < arg_count; i++)
	    {
	      TP_DOMAIN *domain = list_id->type_list.domp[i];
	      if (domain == NULL || domain->type == NULL)
		{
		  return ER_FAILED;
		}
	      m_arg_dom_vector[i] = domain;
	    }
	}

      if (m_dbval_list == nullptr)
	{
	  m_dbval_list = (qproc_db_value_list *) db_private_alloc (m_thread_p,
			 sizeof (m_dbval_list[0]) * m_method_sig_list->num_methods);
	  if (m_dbval_list == NULL)
	    {
	      return ER_FAILED;
	    }
	}

      return NO_ERROR;
    }

    int scanner::open ()
    {
      int error = NO_ERROR;
      error = qfile_open_list_scan (m_list_id, &m_scan_id);

#if defined(SERVER_MODE)
      m_caller.connect ();
#endif

      return error;
    }

    int scanner::close ()
    {
      int error = NO_ERROR;

#if defined(SERVER_MODE)
      m_caller.disconnect ();
#endif

      close_value_array ();

      for (DB_VALUE &value : m_arg_vector)
	{
	  db_value_clear (&value);
	}

#if defined (SA_MODE)
      for (DB_VALUE &value : m_result_vector)
	{
	  db_value_clear (&value);
	}
#endif

      qfile_close_scan (m_thread_p, &m_scan_id);
      return error;
    }

    int scanner::request (method_sig_node *method_sig)
    {
      int error = NO_ERROR;
#if defined(SERVER_MODE)
      switch (method_sig->method_type)
	{
	case METHOD_IS_JAVA_SP:
	  error = m_caller.request (method_sig, m_arg_vector);
	  break;
	case METHOD_IS_CLASS_METHOD:
	case METHOD_IS_INSTANCE_METHOD:
	{
	  packing_packer packer;
	  cubmem::extensible_block eb { cubmem::PRIVATE_BLOCK_ALLOCATOR };

	  /* get packed data size */
	  size_t total_size = packer.get_packed_int_size (0);
	  for_each (m_arg_vector.begin(), m_arg_vector.end(),
		    [&total_size, &packer] (DB_VALUE &value)
	  {
	    total_size += packer.get_packed_db_value_size (value, total_size);
	  });
	  total_size += method_sig->get_packed_size (packer, total_size);

	  eb.extend_to (total_size);
	  packer.set_buffer (eb.get_ptr(), total_size);

	  /* pack data */
	  packer.pack_int (m_arg_vector.size ());
	  for_each (m_arg_vector.begin(), m_arg_vector.end(),
		    [&packer] (DB_VALUE &value)
	  {
	    packer.pack_db_value (value);
	  });
	  method_sig->pack (packer);

	  cubmem::block b (total_size, eb.get_ptr ()); // deallocated by eb's destructor
	  error = xs_send (b);
	}
	break;
	default:
	  assert (false); /* This should not be happened. */
	  error = ER_FAILED;
	  break;
	}
#else
      // TODO for standalone mode
      assert (false);
#endif

      return error;
    }

    int scanner::receive (METHOD_TYPE method_type, DB_VALUE &val)
    {
      int error = NO_ERROR;
#if defined(SERVER_MODE)
      switch (method_type)
	{
	case METHOD_IS_JAVA_SP:
	{
	  m_caller.receive (val);
	}
	break;
	case METHOD_IS_CLASS_METHOD:
	case METHOD_IS_INSTANCE_METHOD:
	{
	  auto get_method_result = [&val] (cubmem::block& b)
	  {
	    int e = NO_ERROR;
	    packing_unpacker unpacker (b.ptr, (size_t) b.dim);
	    int status;
	    unpacker.unpack_int (status);
	    if (status == METHOD_SUCCESS)
	      {
		unpacker.unpack_db_value (val);
	      }
	    else
	      {
		unpacker.unpack_int (e); /* er_errid */
	      }
	    return e;
	  };
	  error = xs_receive (get_method_result);
	}
	break;
	default:
	  assert (false); /* This should not be happened. */
	  error = ER_FAILED;
	  break;
	}
#else
      // TODO for standalone mode
      assert (false);
#endif
      return error;
    }

#if defined(SERVER_MODE)
    int
    scanner::xs_send (cubmem::block &mem)
    {
      OR_ALIGNED_BUF (OR_INT_SIZE * 2) a_reply;
      char *reply = OR_ALIGNED_BUF_START (a_reply);

      /* pack headers */
      char *ptr = or_pack_int (reply, (int) METHOD_CALL);
      ptr = or_pack_int (ptr, mem.dim);

      /* send */
      unsigned int rid = css_get_comm_request_id (m_thread_p);
      int error = css_send_reply_and_data_to_client (m_thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply),
		  mem.ptr, mem.dim);

      return error;
    }

    int
    scanner::xs_receive (const xs_callback_func &func)
    {
      cubmem::block buffer;
      int error = xs_receive_data_from_client (m_thread_p, &buffer.ptr, (int *) &buffer.dim);
      if (error == NO_ERROR)
	{
	  error = func (buffer);
	}
      free_and_init (buffer.ptr);
      return error;
    }
#endif

    SCAN_CODE scanner::next_scan (val_list_node &vl)
    {
      SCAN_CODE scan_code = S_SUCCESS;

      next_value_array (vl);

      int num_methods = m_method_sig_list->num_methods;
      // DB_VALUE *dbval_p = (DB_VALUE *) db_private_alloc (m_thread_p, sizeof (DB_VALUE) * num_methods);
      for (int i = 0; i < num_methods; i++)
	{
	  DB_VALUE *dbval_p = (DB_VALUE *) db_private_alloc (m_thread_p, sizeof (DB_VALUE));
	  db_make_null (dbval_p);
	  m_dbval_list[i].val = dbval_p;
	}

      method_sig_node *method_sig = m_method_sig_list->method_sig;
      int i = 0;
      scan_code = get_single_tuple ();
      while (scan_code && method_sig)
	{
	  if (request (method_sig) != NO_ERROR)
	    {
	      scan_code = S_ERROR;
	    }

	  DB_VALUE *result = m_dbval_list[i].val;
	  if (receive (method_sig->method_type, *result) != NO_ERROR)
	    {
	      scan_code = S_ERROR;
	    }

	  i++;
	  method_sig = method_sig->next;
	}

      return scan_code;
    }

    int
    scanner::close_value_array ()
    {
      db_private_free_and_init (m_thread_p, m_dbval_list);
      return NO_ERROR;
    }

    void scanner::next_value_array (val_list_node &vl)
    {
      qproc_db_value_list *dbval_list = m_dbval_list;

      vl.val_cnt = m_method_sig_list->num_methods;
      for (int n = 0; n < vl.val_cnt; n++)
	{
	  dbval_list->val = nullptr;
	  dbval_list->next = dbval_list + 1;
	  dbval_list++;
	}

      m_dbval_list[vl.val_cnt - 1].next = NULL;
      vl.valp = m_dbval_list;
    }

    SCAN_CODE scanner::get_single_tuple ()
    {
      QFILE_TUPLE_RECORD tuple_record = { NULL, 0 };
      SCAN_CODE scan_code = qfile_scan_list_next (m_thread_p, &m_scan_id, &tuple_record, PEEK);
      if (scan_code == S_SUCCESS)
	{
	  char *ptr;
	  int length;
	  OR_BUF buf;
	  for (int i = 0; i < m_list_id->type_list.type_cnt; i++)
	    {
	      QFILE_TUPLE_VALUE_FLAG flag = (QFILE_TUPLE_VALUE_FLAG) qfile_locate_tuple_value (tuple_record.tpl, i, &ptr, &length);
	      OR_BUF_INIT (buf, ptr, length);

	      DB_VALUE *value = &m_arg_vector [i];
	      TP_DOMAIN *domain = m_arg_dom_vector [i];
	      PR_TYPE *pr_type = domain->type;

	      db_make_null (value);
	      if (flag == V_BOUND)
		{
		  if (pr_type->data_readval (&buf, value, domain, -1, true, NULL, 0) != NO_ERROR)
		    {
		      scan_code = S_ERROR;
		      break;
		    }
		}
	      else
		{
		  /* If value is NULL, properly initialize the result */
		  db_value_domain_init (value, pr_type->id, DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE);
		}
	    }
	}
      return scan_code;
    }

//////////////////////////////////////////////////////////////////////////
// communication with Java SP Server routine implementation
//////////////////////////////////////////////////////////////////////////
#if defined (SERVER_MODE)
    bool
    javasp_caller::connect ()
    {
      if (!is_connected)
	{
	  int server_port = jsp_server_port ();
	  m_sock_fd = jsp_connect_server (server_port);
	  is_connected = !IS_INVALID_SOCKET (m_sock_fd);
	}
      return is_connected;
    }

    bool
    javasp_caller::disconnect ()
    {
      if (is_connected)
	{
	  jsp_disconnect_server (m_sock_fd);
	  is_connected = false;
	}
      return IS_INVALID_SOCKET (m_sock_fd);
    }

    int
    javasp_caller::request (METHOD_SIG *&method_sig, std::vector<DB_VALUE> &arg_base)
    {
      int num_args = method_sig->num_method_args + 1;
      std::vector <DB_VALUE *> arg_val_p (num_args, nullptr);
      for (int i = 0; i < num_args; ++i)
	{
	  int pos = method_sig->method_arg_pos[i];
	  arg_val_p[i] = &arg_base[pos];
	}

      DB_ARG_LIST *val_list = 0, *vl, **next_val_list;
      next_val_list = &val_list;
      for (int i = 0; i < method_sig->num_method_args; i++)
	{
	  DB_VALUE *db_val;
	  *next_val_list = (DB_ARG_LIST *) calloc (1, sizeof (DB_ARG_LIST));
	  if (*next_val_list == NULL)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (DB_ARG_LIST));
	      return ER_OUT_OF_VIRTUAL_MEMORY;
	    }
	  (*next_val_list)->next = (DB_ARG_LIST *) 0;

	  if (arg_val_p[i] == NULL)
	    {
	      return -1;		/* error, clean */
	    }
	  db_val = arg_val_p[i];
	  (*next_val_list)->label = "";	/* check out mode in select statement */
	  (*next_val_list)->val = db_val;

	  next_val_list = & (*next_val_list)->next;
	}

      SP_ARGS sp_args;
      sp_args.name = method_sig->method_name;
      sp_args.args = val_list;
      for (int i = 0; i < method_sig->num_method_args; i++)
	{
	  sp_args.arg_mode[i] = method_sig->arg_info.arg_mode[i];
	  sp_args.arg_type[i] = method_sig->arg_info.arg_type[i];
	}
      sp_args.return_type =  method_sig->arg_info.result_type;

      int error_code = NO_ERROR;
      size_t nbytes;

      packing_packer packer;
      packing_packer packer2;

      cubmem::extensible_block header_buf;
      cubmem::extensible_block args_buf;

      packer.set_buffer_and_pack_all (args_buf, sp_args);

      SP_HEADER header;
      header.command = (int) SP_CODE_INVOKE;
      header.size = packer.get_current_size ();

      packer2.set_buffer_and_pack_all (header_buf, header);
      nbytes = jsp_writen (m_sock_fd, packer2.get_buffer_start (), packer2.get_current_size ());
      if (nbytes != packer2.get_current_size ())
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_NETWORK_ERROR, 1, nbytes);
	  error_code = er_errid ();
	}

      nbytes = jsp_writen (m_sock_fd, packer.get_buffer_start (), packer.get_current_size ());
      if (nbytes != packer.get_current_size ())
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_NETWORK_ERROR, 1, nbytes);
	  error_code = er_errid ();
	}

      while (val_list)
	{
	  vl = val_list->next;
	  free_and_init (val_list);
	  val_list = vl;
	}

      return error_code;
    }

    int
    javasp_caller::receive (DB_VALUE &returnval)
    {
      /* read request code */
      int start_code, error_code = NO_ERROR;

	  do 
	  {
      int nbytes = jsp_readn (m_sock_fd, (char *) &start_code, (int) sizeof (int));
      if (nbytes != (int) sizeof (int))
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_NETWORK_ERROR, 1, nbytes);
	  return ER_SP_NETWORK_ERROR;
	}

      start_code = ntohl (start_code);
      if (start_code == SP_CODE_INTERNAL_JDBC)
		{
		assert (false);
		}
		else
		{
			break;
		}
	  } while (start_code == SP_CODE_INTERNAL_JDBC);

      if (start_code == SP_CODE_RESULT || start_code == SP_CODE_ERROR)
	{
	  /* read size of buffer to allocate and data */
	  cubmem::extensible_block blk;
	  error_code = alloc_response (blk);
	  if (error_code != NO_ERROR)
	    {
	      assert (false);
	    }

	  switch (start_code)
	    {
	    case SP_CODE_RESULT:
	      error_code = receive_result (blk, returnval);
	      break;
	    case SP_CODE_ERROR:
	      error_code = receive_error (blk, returnval);
	      assert (false);
	      break;
	    }
	}
      else
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_NETWORK_ERROR, 1, start_code);
	  error_code = ER_SP_NETWORK_ERROR;
	}

      return error_code;
    }

    int
    javasp_caller::alloc_response (cubmem::extensible_block &blk)
    {
      int nbytes, res_size;
      nbytes = jsp_readn (m_sock_fd, (char *) &res_size, (int) sizeof (int));
      if (nbytes != (int) sizeof (int))
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_NETWORK_ERROR, 1, nbytes);
	  return ER_SP_NETWORK_ERROR;
	}
      res_size = ntohl (res_size);

      blk.extend_to (res_size);

      nbytes = jsp_readn (m_sock_fd, blk.get_ptr (), res_size);
      if (nbytes != res_size)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_NETWORK_ERROR, 1, nbytes);
	  return ER_SP_NETWORK_ERROR;
	}

      return NO_ERROR;
    }

    int
    javasp_caller::receive_result (cubmem::extensible_block &blk, DB_VALUE &returnval)
    {
      int error_code = NO_ERROR;

      packing_unpacker unpacker;
      unpacker.set_buffer (blk.get_ptr (), blk.get_size ());

      SP_VALUE value_unpacker;
      db_make_null (&returnval);
      value_unpacker.value = &returnval;
      value_unpacker.unpack (unpacker);

      return error_code;
    }

    int
    javasp_caller::receive_error (cubmem::extensible_block &blk, DB_VALUE &returnval)
    {
      int error_code = NO_ERROR;
      DB_VALUE error_value, error_msg;

      db_make_null (&error_value);
      db_make_null (&error_msg);

      db_make_null (&returnval);

      packing_unpacker unpacker;
      unpacker.set_buffer (blk.get_ptr (), blk.get_size ());

      SP_VALUE value_unpacker;

      value_unpacker.value = &error_value;
      value_unpacker.unpack (unpacker);

      value_unpacker.value = &error_msg;
      value_unpacker.unpack (unpacker);

      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_EXECUTE_ERROR, 1, db_get_string (&error_msg));
      error_code = er_errid ();

      db_value_clear (&error_value);
      db_value_clear (&error_msg);

      return error_code;
    }

#endif

  }
}
