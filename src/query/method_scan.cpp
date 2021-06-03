/*
 * Copyright 2008 Search Solution Corporation
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
 *  See the License for the specific language governing permissions and1
 *  limitations under the License.
 *
 */

#include "method_scan.hpp"

#include "dbtype.h"
#include "object_representation.h"

#include "packer.hpp" /* packing_packer */
#include "memory_private_allocator.hpp" /* cubmem::PRIVATE_BLOCK_ALLOCATOR */

#include "network_interface_sr.h"
#include "xasl.h"

#include "method_def.hpp" /* METHOD_SUCCESS, METHOD_ERROR */

#include "server_support.h"

#include "list_file.h" /* qfile_ */

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

	int scanner::init (cubthread::entry *thread_p, METHOD_SIG_LIST *sig_list, qfile_list_id *list_id)
    {
      // check initialized
      if (m_thread_p != thread_p)
	{
	  m_thread_p = thread_p;
	}

      if (m_method_sig_list != sig_list)
	{
	  // TODO: interpret method signature and set internal representation for this class
	  m_method_sig_list = sig_list;
	  m_num_methods = m_method_sig_list->num_methods;
	  if (m_num_methods <= 0)
	    {
	      // something wrong, cannot reach here
	      assert (false);
	      return ER_FAILED;
	    }
	  m_result_vector.resize (m_num_methods);
	}

      if (m_list_id != list_id)
	{
	  m_list_id = list_id;
	  if (m_list_id->tuple_cnt == 1)
	    {
	      int type_count = list_id->type_list.type_cnt;
	      m_arg_vector.resize (type_count);
	      m_arg_dom_vector.resize (type_count);

	      for (int i = 0; i < type_count; i++)
		{
		  TP_DOMAIN *domain = list_id->type_list.domp[i];
		  if (domain == NULL || domain->type == NULL)
		    {
		      return ER_FAILED;
		    }
		  m_arg_dom_vector[i] = domain;
		}
	    }
	  else
	    {
	      // something wrong, cannot reach here
	      assert (false);
	      return ER_FAILED;
	    }
	}

      if (m_dbval_list == nullptr)
	{
	  m_dbval_list = (qproc_db_value_list *) malloc (sizeof (m_dbval_list[0]) * m_num_methods);
	  if (m_dbval_list == NULL)
	    {
	      return ER_FAILED;
	    }
	}

      return NO_ERROR;
    }

    int scanner::open ()
    {
      if (get_single_tuple () != NO_ERROR)
	{
	  return ER_FAILED;
	}

      request ();

      return NO_ERROR;
    }

    int scanner::close ()
    {
      close_value_array ();

      return S_SUCCESS;
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
      OR_ALIGNED_BUF (OR_INT_SIZE * 2) a_reply;
      char *reply = OR_ALIGNED_BUF_START (a_reply);

      /* pack headers with legacy or_pack_* */
      char *ptr = or_pack_int (reply, (int) METHOD_CALL);
      ptr = or_pack_int (ptr, length);

      packing_packer packer;
      cubmem::extensible_block databuf { cubmem::PRIVATE_BLOCK_ALLOCATOR };

      /* get packed data size */
      int length = OR_INT_SIZE; /* arg count */
      for (DB_VALUE &value : m_arg_vector)
	{
	  length += or_db_value_size (&value);
	}
      length += or_method_sig_list_length ((void *) m_method_sig_list);

      /* set databuf size and get start ptr */
      databuf.extend_to (length);
      ptr = databuf.get_ptr ();

      /* pack data */
      ptr = or_pack_int (ptr, m_arg_vector.size ());
      for (DB_VALUE &value : m_arg_vector)
	{
	  ptr = or_pack_db_value (ptr, &value);
	}
      ptr = or_pack_method_sig_list (ptr, (void *) m_method_sig_list);

      /* send */
      unsigned int rid = css_get_comm_request_id (m_thread_p);
      return css_send_reply_and_data_to_client (m_thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply),
	     databuf.get_ptr (), length);
    }
#endif

#if defined(SERVER_MODE)
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
      SCAN_CODE result = S_SUCCESS;

#if defined(SERVER_MODE)
      for (int i = 0; i < m_num_methods; i++)
	{
	  DB_VALUE *dbval_p = (DB_VALUE *) malloc (sizeof (DB_VALUE));
	  db_make_null (dbval_p);

	  receive (m_result_vector[i]);

	  db_value_clone (&m_result_vector[i], dbval_p);
	  m_dbval_list[i].val = dbval_p;
	  m_dbval_list[i].next = NULL;
	}

      next_value_array (vl);
#endif

      return result;
    }

    int
    scanner::close_value_array ()
    {
      free_and_init (m_dbval_list);
      return NO_ERROR;
    }

    SCAN_CODE scanner::next_value_array (val_list_node &vl)
    {
      SCAN_CODE scan_result = S_SUCCESS;
      qproc_db_value_list *dbval_list = m_dbval_list;

      vl.val_cnt = m_num_methods;
      for (int n = 0; n < vl.val_cnt; n++)
	{
	  dbval_list->next = dbval_list + 1;
	  dbval_list++;
	}

      m_dbval_list[vl.val_cnt - 1].next = NULL;
      vl.valp = m_dbval_list;

      return scan_result;
    }

    int scanner::get_single_tuple ()
    {
      assert (m_list_id->type_list.type_cnt == m_arg_vector.size ());
      assert (m_list_id->type_list.type_cnt == m_arg_dom_vector.size ());

      int error_code = NO_ERROR;
      QFILE_LIST_SCAN_ID scan_id;

      error_code = qfile_open_list_scan (m_list_id, &scan_id);
      if (error_code != NO_ERROR)
	{
	  return error_code;
	}

      QFILE_TUPLE_RECORD tuple_record = { NULL, 0 };
      if (qfile_scan_list_next (m_thread_p, &scan_id, &tuple_record, PEEK) == S_SUCCESS)
	{
	  char *ptr;
	  int length;
	  for (int i = 0; i < m_list_id->type_list.type_cnt; i++)
	    {
	      DB_VALUE *value = &m_arg_vector [i];
	      TP_DOMAIN *domain = m_arg_dom_vector [i];
	      PR_TYPE *pr_type = domain->type;
	      QFILE_TUPLE_VALUE_FLAG flag = (QFILE_TUPLE_VALUE_FLAG) qfile_locate_tuple_value (tuple_record.tpl, i, &ptr, &length);

	      OR_BUF buf;
	      OR_BUF_INIT (buf, ptr, length);
	      if (flag == V_BOUND)
		{
		  if (pr_type->data_readval (&buf, value, domain, -1, true, NULL, 0) != NO_ERROR)
		    {
		      error_code = ER_FAILED;
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
      qfile_close_scan (m_thread_p, &scan_id);
      return error_code;
    }
  }

}