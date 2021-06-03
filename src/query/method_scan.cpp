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

#include "packer.hpp"
#include "memory_private_allocator.hpp" /* cubmem::PRIVATE_BLOCK_ALLOCATOR */

#include "network_interface_sr.h"
#include "xasl.h"

#include "server_support.h"

#include "list_file.h" /* qfile_ */

int method_Num_method_jsp_calls = 0;

namespace cubscan
{
  namespace method
  {
    int scanner::init (cubthread::entry *thread_p, METHOD_SIG_LIST *sig_list, qfile_list_id *list_id)
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

      m_dbval_list = (qproc_db_value_list *) malloc (sizeof (m_dbval_list[0]) * m_num_methods);
      if (m_dbval_list == NULL)
	{
	  return ER_FAILED;
	}

#ifdef SERVER_MODE
      /*
        if (m_vacomm_buffer == NULL)
        {
      m_vacomm_buffer = method_initialize_vacomm_buffer ();
      if (m_vacomm_buffer == NULL)
      {
        return ER_FAILED;
      }
        }
      */
#endif /* SERVER_MODE */
    }

    int scanner::open ()
    {
      /* do nothing yet */

      return NO_ERROR;
    }

    int scanner::close ()
    {
      close_value_array ();
    }

    SCAN_CODE scanner::receive_value (DB_VALUE *value)
    {
      /*
      #ifdef SERVER_MODE
      int error;
      char *p;
      VACOMM_BUFFER *vacomm_buffer_p = m_vacomm_buffer;
      if (vacomm_buffer_p->cur_pos == 0)
      {
      if (vacomm_buffer_p->status == METHOD_EOF)
      {
      return S_END;
      }

      vacomm_buffer_p->status = METHOD_ERROR;

      error = xs_receive_data_from_client (m_thread_p, &vacomm_buffer_p->area, &vacomm_buffer_p->size);
      if (error == NO_ERROR)
      {
      vacomm_buffer_p->buffer = vacomm_buffer_p->area + VACOMM_BUFFER_HEADER_SIZE;
      p = or_unpack_int (vacomm_buffer_p->area + VACOMM_BUFFER_HEADER_LENGTH_OFFSET, &vacomm_buffer_p->length);
      p = or_unpack_int (vacomm_buffer_p->area + VACOMM_BUFFER_HEADER_STATUS_OFFSET, &vacomm_buffer_p->status);

      if (vacomm_buffer_p->status == METHOD_ERROR)
      {
        p = or_unpack_int (vacomm_buffer_p->area + VACOMM_BUFFER_HEADER_ERROR_OFFSET, &vacomm_buffer_p->error);
      }
      else
      {
        p =
      	  or_unpack_int (vacomm_buffer_p->area + VACOMM_BUFFER_HEADER_NO_VALS_OFFSET, &vacomm_buffer_p->num_vals);
      }

      if (vacomm_buffer_p->status == METHOD_SUCCESS)
      {
        error = xs_send_action_to_client (m_thread_p, (VACOMM_BUFFER_CLIENT_ACTION) vacomm_buffer_p->action);
        if (error != NO_ERROR)
          {
            return S_ERROR;
          }
      }
      }
      else
      {
      xs_send_action_to_client (m_thread_p, (VACOMM_BUFFER_CLIENT_ACTION) vacomm_buffer_p->action);
      return S_ERROR;
      }
      }

      if (vacomm_buffer_p->status == METHOD_ERROR)
      {
      return S_ERROR;
      }

      if (vacomm_buffer_p->num_vals > 0)
      {
      p = or_unpack_db_value (vacomm_buffer_p->buffer + vacomm_buffer_p->cur_pos, dbval_p);
      vacomm_buffer_p->cur_pos += OR_VALUE_ALIGNED_SIZE (dbval_p);
      vacomm_buffer_p->num_vals--;
      }
      else
      {
      return S_END;
      }

      if (vacomm_buffer_p->num_vals == 0)
      {
      vacomm_buffer_p->cur_pos = 0;
      }

      return S_SUCCESS;
      #endif
      */
      return S_SUCCESS;
    }

    int
    scanner::xs_send ()
    {
#if defined(SERVER_MODE)
      OR_ALIGNED_BUF (OR_INT_SIZE * 2) a_reply;
      char *reply = OR_ALIGNED_BUF_START (a_reply);

      unsigned int  rid = css_get_comm_request_id (m_thread_p);

      char *ptr;
      int length = OR_INT_SIZE; /* arg count */
      for (DB_VALUE &value : m_arg_vector)
	{
	  length += or_db_value_size (&value);
	}
      length += or_method_sig_list_length ((void *) m_method_sig_list);

      ptr = or_pack_int (reply, (int) METHOD_CALL);
      ptr = or_pack_int (ptr, length);

      cubmem::extensible_block databuf { cubmem::PRIVATE_BLOCK_ALLOCATOR };
      databuf.extend_to (length);

      ptr = databuf.get_ptr ();
      ptr = or_pack_int (ptr, m_arg_vector.size ());
      for (DB_VALUE &value : m_arg_vector)
	{
	  ptr = or_pack_db_value (ptr, &value);
	}
      ptr = or_pack_method_sig_list (ptr, (void *) m_method_sig_list);

      css_send_reply_and_data_to_client (m_thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply),
					 databuf.get_ptr (), length);
#endif
      return NO_ERROR;
    }

    int
    scanner::xs_receive ()
    {
#if defined(SERVER_MODE)
      char *data_p = NULL;
      int data_size;
      xs_receive_data_from_client (m_thread_p, &data_p, &data_size);

      int count;
      char *ptr = or_unpack_int (data_p, &count);
      assert (m_result_vector.size() == count);

      for (int i = 0; i < count; i++)
	{
	  ptr = or_unpack_db_value (ptr, &m_result_vector[i]);
	}
#endif
      return NO_ERROR;
    }

    SCAN_CODE scanner::next_scan (val_list_node &vl)
    {
      SCAN_CODE result = S_ERROR;

#if defined(SERVER_MODE)
      if (!get_single_tuple ())
	{
	  return S_ERROR;
	}

      // send
      xs_send ();
      xs_receive ();

      qproc_db_value_list *dbval_list_p = m_dbval_list;
      for (int i = 0; i < m_num_methods; i++)
	{
	  DB_VALUE *dbval_p = (DB_VALUE *) malloc (sizeof (DB_VALUE));
	  db_make_null (dbval_p);
	  db_value_clone (&m_result_vector[i], dbval_p);
	  dbval_list_p->val = dbval_p;
	  dbval_list_p++;
	}

      vl.val_cnt = m_num_methods;
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