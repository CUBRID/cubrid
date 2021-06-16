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
      return error;
    }

    int scanner::close ()
    {
      int error = NO_ERROR;

      close_value_array ();

#if defined (SA_MODE)
      for (DB_VALUE &value : m_result_vector)
	{
	  db_value_clear (&value);
	}
#endif

      qfile_close_scan (m_thread_p, &m_scan_id);
      return error;
    }

    int scanner::request ()
    {
      int error = NO_ERROR;
#if defined(SERVER_MODE)
      error = xs_send ();
#else
      // TODO for standalone mode
      assert (false);
#endif

      return error;
    }

    int scanner::receive (DB_VALUE &val)
    {
      int error = NO_ERROR;
#if defined(SERVER_MODE)
      error = xs_receive (val);
#else
      // TODO for standalone mode
      assert (false);
#endif
      return error;
    }

#if defined(SERVER_MODE)
    int
    scanner::xs_send ()
    {
      packing_packer packer;
      cubmem::extensible_block eb { cubmem::PRIVATE_BLOCK_ALLOCATOR };

      /* get packed data size */
      size_t total_size = packer.get_packed_int_size (0);
      for (DB_VALUE &value : m_arg_vector)
	{
	  total_size += packer.get_packed_db_value_size (value, total_size);
	}
      total_size += m_method_sig_list->get_packed_size (packer, total_size);

      /* set databuf size and get start ptr */
      eb.extend_to (total_size);
      packer.set_buffer (eb.get_ptr(), total_size);

      /* pack data */
      packer.pack_int (m_arg_vector.size ());
      for (DB_VALUE &value : m_arg_vector)
	{
	  packer.pack_db_value (value);
	}

      m_method_sig_list->pack (packer);

      OR_ALIGNED_BUF (OR_INT_SIZE * 2) a_reply;
      char *reply = OR_ALIGNED_BUF_START (a_reply);

      /* pack headers with legacy or_pack_* */
      char *ptr = or_pack_int (reply, (int) METHOD_CALL);
      ptr = or_pack_int (ptr, total_size);

      /* send */
      unsigned int rid = css_get_comm_request_id (m_thread_p);
      int error = css_send_reply_and_data_to_client (m_thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply),
		  eb.get_ptr (), total_size);

      return error;
    }

    int
    scanner::xs_receive (DB_VALUE &val)
    {
      char *data_p = NULL;
      int data_size;
      int error = xs_receive_data_from_client (m_thread_p, &data_p, &data_size);
      if (error == NO_ERROR)
	{
	  packing_unpacker unpacker (data_p, (size_t) data_size);

	  int status;
	  unpacker.unpack_int (status);

	  if (status == METHOD_SUCCESS)
	    {
	      unpacker.unpack_db_value (val);
	    }
	  else
	    {
	      unpacker.unpack_int (error); /* er_errid */
	    }
	}
      return error;
    }
#endif

    SCAN_CODE scanner::next_scan (val_list_node &vl)
    {
      SCAN_CODE scan_code = S_SUCCESS;
      scan_code = get_single_tuple ();
      if (scan_code == S_SUCCESS)
	{
	  if (request () != NO_ERROR)
	    {
	      scan_code = S_ERROR;
	    }

	  // clear
	  for (DB_VALUE &value : m_arg_vector)
	    {
	      db_value_clear (&value);
	    }
	}

      if (scan_code == S_SUCCESS)
	{
	  qproc_db_value_list *dbval_list = m_dbval_list;
	  int num_methods = m_method_sig_list->num_methods;
	  for (int i = 0; i < num_methods; i++)
	    {
	      DB_VALUE *dbval_p = (DB_VALUE *) db_private_alloc (m_thread_p, sizeof (DB_VALUE));
	      dbval_list->val = dbval_p;

	      db_make_null (dbval_p);
	      if (receive (*dbval_p) != NO_ERROR)
		{
		  scan_code = S_ERROR;
		  break;
		}
	      dbval_list++;
	    }
	}

      if (scan_code == S_SUCCESS)
	{
	  next_value_array (vl);
	}

      return scan_code;
    }

    int
    scanner::close_value_array ()
    {
      db_private_free_and_init (m_thread_p, m_dbval_list);
      return NO_ERROR;
    }

    SCAN_CODE scanner::next_value_array (val_list_node &vl)
    {
      SCAN_CODE scan_result = S_SUCCESS;
      qproc_db_value_list *dbval_list = m_dbval_list;

      vl.val_cnt = m_method_sig_list->num_methods;
      for (int n = 0; n < vl.val_cnt; n++)
	{
	  dbval_list->next = dbval_list + 1;
	  dbval_list++;
	}

      m_dbval_list[vl.val_cnt - 1].next = NULL;
      vl.valp = m_dbval_list;

      return scan_result;
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
  }

}
