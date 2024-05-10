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

#include "method_connection_sr.hpp"

#if defined (SERVER_MODE)
#include "network.h" /* METHOD_CALL */
#include "network_interface_sr.h" /* xs_receive_data_from_client() */
#include "server_support.h"	/* css_send_reply_and_data_to_client(), css_get_comm_request_id() */
#else
#include "query_method.hpp"
#include "method_callback.hpp"
#endif

#include "object_representation.h" /* OR_ */
#include "jsp_comm.h" /* jsp_writen() */
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace cubmethod
{
//////////////////////////////////////////////////////////////////////////
// General interface to communicate with CAS
//////////////////////////////////////////////////////////////////////////
#if defined (SERVER_MODE)
  int xs_send (cubthread::entry *thread_p, cubmem::extensible_block &mem)
  {
    OR_ALIGNED_BUF (OR_INT_SIZE * 2) a_reply;
    char *reply = OR_ALIGNED_BUF_START (a_reply);

    /* pack headers */
    char *ptr = or_pack_int (reply, (int) METHOD_CALL);
    ptr = or_pack_int (ptr, (int) mem.get_size ());

#if !defined(NDEBUG)
    /* suppress valgrind UMW error */
    memset (ptr, 0, OR_ALIGNED_BUF_SIZE (a_reply) - (ptr - reply));
#endif

    if (thread_p == NULL || thread_p->conn_entry == NULL)
      {
	return ER_FAILED;
      }

    /* send */
    unsigned int rid = css_get_comm_request_id (thread_p);
    return css_send_reply_and_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply),
	   mem.get_ptr(), (int) mem.get_size ());
  }

  int xs_receive (cubthread::entry *thread_p, const xs_callback_func &func)
  {
    cubmem::block buffer (0, nullptr);

    int error = xs_receive_data_from_client (thread_p, &buffer.ptr, (int *) &buffer.dim);
    if (error == NO_ERROR && er_errid () == NO_ERROR)
      {
	error = func (buffer);
      }
    else
      {
	if (error == NO_ERROR)
	  {
	    error = er_errid (); // ER_SP_TOO_MANY_NESTED_CALL, ER_INTERRUPTED... (interrupt reasons)
	  }
      }

    free_and_init (buffer.ptr);
    return error;
  }

  int xs_receive (cubthread::entry *thread_p, SOCKET socket, const xs_callback_func_with_sock &func)
  {
    cubmem::block buffer (0, nullptr);

    int error = xs_receive_data_from_client (thread_p, &buffer.ptr, (int *) &buffer.dim);
    if (error == NO_ERROR && er_errid () == NO_ERROR)
      {
	error = func (socket, buffer);
      }
    else
      {
	if (error == NO_ERROR)
	  {
	    error = er_errid (); // ER_SP_TOO_MANY_NESTED_CALL, ER_INTERRUPTED... (interrupt reasons)
	  }
      }

    free_and_init (buffer.ptr);
    return error;
  }
#else // SA_MODE
  int xs_send (cubthread::entry *thread_p, cubmem::extensible_block &ext_blk)
  {
    packing_unpacker unpacker (ext_blk.get_ptr (), ext_blk.get_size ());
    return method_dispatch (unpacker);
  }

  int xs_receive (cubthread::entry *thread_p, const xs_callback_func &func)
  {
    std::queue <cubmem::extensible_block> &queue = mcon_get_data_queue ();

    assert (!queue.empty());

    cubmem::extensible_block &blk = queue.front ();
    cubmem::block buffer (blk.get_size(), blk.get_ptr());
    int error = func (buffer);

    queue.pop ();
    return error;
  }

  int xs_receive (cubthread::entry *thread_p, SOCKET socket, const xs_callback_func_with_sock &func)
  {
    std::queue <cubmem::extensible_block> &queue = mcon_get_data_queue ();

    assert (!queue.empty());

    cubmem::extensible_block &blk = queue.front ();
    cubmem::block buffer (blk.get_size(), blk.get_ptr());

    int error = func (socket, buffer);

    queue.pop ();
    return error;
  }
#endif

  //////////////////////////////////////////////////////////////////////////
  // Interface to communicate with Java SP Server
  //////////////////////////////////////////////////////////////////////////

  int mcon_send_buffer_to_java (SOCKET socket, cubmem::block &blk)
  {
    int error = NO_ERROR;
    OR_ALIGNED_BUF (OR_INT_SIZE) a_request;
    char *request = OR_ALIGNED_BUF_START (a_request);

    int request_size = (int) blk.dim;
    or_pack_int (request, request_size);

    int nbytes = jsp_writen (socket, request, OR_INT_SIZE);
    if (nbytes != OR_INT_SIZE)
      {
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_NETWORK_ERROR, 1, nbytes);
	error = er_errid ();
	return error;
      }

    nbytes = jsp_writen (socket, blk.ptr, blk.dim);
    if (nbytes != (int) blk.dim)
      {
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_NETWORK_ERROR, 1, nbytes);
	error = er_errid ();
      }
    return error;
  }

} // namespace cubmethod
