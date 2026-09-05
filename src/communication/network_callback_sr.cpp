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

#include "network_callback_sr.hpp"

#include "error_manager.h" /* er_errid() */
#include "network.h" /* METHOD_CALL */
#include "network_interface_sr.h" /* xs_receive_data_from_client() */
#include "object_representation.h" /* OR_ */
#include "server_support.h"	/* css_send_reply_and_data_to_client(), css_get_comm_request_id() */

#include "query_method.hpp"
#include "method_callback.hpp"

#if defined (SERVER_MODE)
#include "client_session_context.hpp"
#endif

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

#if defined (SERVER_MODE)
int xs_callback_send (cubthread::entry *thread_p, const cubmem::extensible_block &mem)
{
  /* merged in-process session: the client half lives on this very thread (the
   * activation bracket is thread-local) — terminate the callback here, the
   * SA-mode shape (#120 D2/D7).  A legacy CAS connection's worker holds no
   * bracket and takes the wire path below. */
  if (csc_bracket_is_active ())
    {
      packing_unpacker unpacker (mem.get_read_ptr (), mem.get_size ());
      int error = method_dispatch (unpacker);
      if (error != NO_ERROR && er_errid () != NO_ERROR)
	{
	  /* legacy CS shipped er_errid () — not the raw dispatch status — back
	   * through METHOD_ERROR (network_cl.c), and the invoke loop's
	   * ER_SM_INVALID_METHOD_ENV pass-through keys on that id */
	  error = er_errid ();
	}
      return error;
    }

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
  return css_send_reply_and_data_to_client_direct (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply),
	 (char *) mem.get_read_ptr (), (int) mem.get_size ());
}

int xs_callback_receive (cubthread::entry *thread_p, const xs_callback_func &func)
{
  if (csc_bracket_is_active ())
    {
      /* in-process termination: the dispatch above queued its response on the
       * session's handler (the SA-mode shape) */
      std::queue <cubmem::extensible_block> &queue = cubmethod::get_callback_handler ()->get_data_queue ();

      assert (!queue.empty ());

      cubmem::extensible_block &blk = queue.front ();
      cubmem::block in_proc_buffer (blk.get_size (), blk.get_ptr ());
      int in_proc_error = func (in_proc_buffer);

      queue.pop ();
      return in_proc_error;
    }

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

  if (buffer.ptr != NULL)
    {
      thread_p->release_packet (buffer.ptr);
    }
  return error;
}
#else
static std::queue <cubmem::extensible_block> &
xs_get_data_queue_from_cl ()
{
  return cubmethod::get_callback_handler()->get_data_queue ();
}

int xs_callback_send (cubthread::entry *thread_p, const cubmem::extensible_block &ext_blk)
{
  packing_unpacker unpacker (ext_blk.get_read_ptr (), ext_blk.get_size ());
  return method_dispatch (unpacker);
}

int xs_callback_receive (cubthread::entry *thread_p, const xs_callback_func &func)
{
  std::queue <cubmem::extensible_block> &queue = xs_get_data_queue_from_cl ();

  assert (!queue.empty());

  cubmem::extensible_block &blk = queue.front ();
  cubmem::block buffer (blk.get_size(), blk.get_ptr());
  int error = func (buffer);

  queue.pop ();
  return error;
}
#endif
