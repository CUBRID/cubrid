/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 */

/*
 * network_interface_sr.c - Server side network interface functions 
 *                          for client requests.
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include <string.h>

#include "porting.h"
#include "memory_alloc.h"
#include "storage_common.h"
#include "xserver_interface.h"
#include "large_object.h"
#include "statistics_sr.h"
#include "btree_load.h"
#include "perf_monitor.h"
#include "log_impl.h"
#include "boot_sr.h"
#include "locator_sr.h"
#include "server_interface.h"
#include "oid.h"
#include "error_manager.h"
#include "object_representation.h"
#include "network.h"
#include "log_comm.h"
#include "network_interface_sr.h"
#include "page_buffer.h"
#include "file_manager.h"
#include "boot_sr.h"
#include "arithmetic.h"
#include "query_manager.h"
#include "transaction_sr.h"
#include "release_string.h"
#include "thread_impl.h"
#include "statistics.h"
#include "chartype.h"
#include "heap_file.h"
#include "jsp_sr.h"
#include "replication.h"
#include "server_support.h"
#include "connection_sr.h"

#define NET_COPY_AREA_SENDRECV_SIZE (OR_INT_SIZE * 3)
#define NET_SENDRECV_BUFFSIZE (OR_INT_SIZE)

/* This file is only included in the server.  So set the on_server flag on */
unsigned int db_on_server = 1;

static int server_capabilities (void);

/*
 * return_error_to_client -
 *
 * return:
 *
 *   rid(in):
 *
 * NOTE:
 */
void
return_error_to_client (unsigned int rid)
{
  void *error_string;
  int length;

  error_string = er_get_area_error (&length);

  if (error_string != NULL)
    {
      css_set_error_code (rid, er_errid ());
      css_send_error_to_client (rid, (char *) error_string, length);
      css_set_error_code (rid, 0);
      free_and_init (error_string);
    }
}

/*
 * server_capabilities -
 *
 * return:
 *
 * NOTE:
 */
static int
server_capabilities (void)
{
  return NET_INTERRUPT_ENABLED_CAP;
}

/*
 * server_ping -
 *
 * return:
 *
 *   rid(in): request id
 *
 * NOTE:
 * this function is obsolate... it remains here to detect old versions of
 *    clients without handshake ..
 */
void
server_ping (unsigned int rid)
{
  OR_ALIGNED_BUF (NET_PINGBUF_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  char *secure_errtxt_to_client;
  void *error_string;
  int length;

  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_NET_DIFFERENT_RELEASE, 2,
	  rel_major_release_string (), " ");

  /*
   * It is likely that the client doe snot knonw about the above errid...
   * reset it with another one... for security reasons...
   */

  secure_errtxt_to_client = (char *) db_private_alloc (NULL,
						       strlen (er_msg ()) +
						       1);
  if (secure_errtxt_to_client != NULL)
    {
      strcpy (secure_errtxt_to_client, er_msg ());
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ERR_CSS_ERROR_FROM_SERVER, 1,
	      secure_errtxt_to_client);
      db_private_free_and_init (NULL, secure_errtxt_to_client);
    }

  error_string = er_get_area_error (&length);

  if (error_string != NULL)
    {
      css_set_error_code (rid, er_errid ());
      css_send_error_to_client (rid, (char *) error_string, length);
      css_set_error_code (rid, 0);
      free_and_init (error_string);
    }

  strcpy (reply, " ");
  css_send_data_to_client (rid, reply, strlen (reply) + 1);
}

/*
 * server_ping_with_handshake -
 *
 * return:
 *
 *   rid(in): request id
 *   request(in):
 *   reqlen(in):
 *
 * NOTE: Handler for the SERVER_PING_WITH_HANDSHAKE request.
 *    We record the client's version string here and send back our own
 *    version string so the client can determine compatibility.
 */
int
server_ping_with_handshake (unsigned int rid, char *request, int reqlen)
{
  OR_ALIGNED_BUF (NET_PINGBUF_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  char *ptr, *client_version, *temp;
  unsigned int reply_size;
  int status = CSS_NO_ERRORS;
  bool compatible = true;

  if (reqlen > 0)
    {
      or_unpack_string_nocopy (request, &client_version);
      temp = css_add_client_version_string (client_version);
    }
  else
    {
      temp = css_add_client_version_string ("?.?.?");
    }

  /* Check to see if we successfully added the version internally.
   * If we can't store the client version, we don't know what protocol
   * to use, so we have to disconnect it.
   */
  if (temp == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_NET_SERVER_HAND_SHAKE, 0);
      return_error_to_client (rid);
      status = CSS_UNPLANNED_SHUTDOWN;
    }
  else
    {
      char *server_release = (char *) rel_release_string ();
      char *client_release = temp;
      if (!server_release || !client_release
	  || rel_compare (server_release, client_release) != 0)
	{
	  compatible = false;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_NET_DIFFERENT_RELEASE,
		  2, server_release, client_release);
	  return_error_to_client (rid);
	}
    }

  reply_size = (or_packed_string_length (rel_release_string ()) +
		or_packed_string_length (NET_CLIENT_SERVER_HAND_SHAKE) +
		OR_INT_SIZE);

  if (reply_size > NET_PINGBUF_SIZE)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_NET_CANT_ALLOC_BUFFER, 0);
      return_error_to_client (rid);
    }
  else
    {
      if (compatible)
	{
	  ptr = or_pack_string (reply, (char *) rel_release_string ());
	}
      else
	{
	  ptr = or_pack_string (reply, (char *) "?.?.?");
	}
      ptr = or_pack_string (ptr, NET_CLIENT_SERVER_HAND_SHAKE);
      ptr = or_pack_int (ptr, server_capabilities ());
      css_send_data_to_client (rid, reply, reply_size);
    }

  return status;
}

/*
 * slocator_fetch -
 *
 * return:
 *
 *   thrd(in):
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
slocator_fetch (THREAD_ENTRY * thrd, unsigned int rid, char *request,
		int reqlen)
{
  OID oid;
  int chn;
  LOCK lock;
  OID class_oid;
  int class_chn;
  int prefetch;
  LC_COPYAREA *copy_area;
  char *ptr;
  int success;
  OR_ALIGNED_BUF (NET_COPY_AREA_SENDRECV_SIZE + OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  char *desc_ptr = NULL;
  int desc_size;
  char *content_ptr;
  int content_size;
  int num_objs = 0;

  ptr = or_unpack_oid (request, &oid);
  ptr = or_unpack_int (ptr, &chn);
  ptr = or_unpack_lock (ptr, &lock);
  ptr = or_unpack_oid (ptr, &class_oid);
  ptr = or_unpack_int (ptr, &class_chn);
  ptr = or_unpack_int (ptr, &prefetch);

  copy_area = NULL;
  success = xlocator_fetch (thrd, &oid, chn, lock, &class_oid, class_chn,
			    prefetch, &copy_area);

  if (success != NO_ERROR)
    {
      /* ER_LK_UNILATERALLY_ABORTED may occur,
       * if it occurs, rollback transaction
       */
      if (er_errid () == ER_LK_UNILATERALLY_ABORTED)
	{
	  tran_server_unilaterally_abort_tran (thrd);
	}
      return_error_to_client (rid);
    }

  if (copy_area != NULL)
    {
      num_objs = locator_send_copy_area (copy_area, &content_ptr,
					 &content_size, &desc_ptr,
					 &desc_size);
    }
  else
    {
      desc_ptr = NULL;
      desc_size = 0;
      content_ptr = NULL;
      content_size = 0;
    }

  /* Send sizes of databuffer and copy area (descriptor + content) */

  ptr = or_pack_int (reply, num_objs);
  ptr = or_pack_int (ptr, desc_size);
  ptr = or_pack_int (ptr, content_size);
  ptr = or_pack_int (ptr, success);

  if (copy_area == NULL)
    {
      css_send_data_to_client (rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
    }
  else
    {
      css_send_reply_and_2_data_to_client (rid, reply,
					   OR_ALIGNED_BUF_SIZE (a_reply),
					   desc_ptr, desc_size, content_ptr,
					   content_size);
      locator_free_copy_area (copy_area);
    }
  if (desc_ptr)
    {
      free_and_init (desc_ptr);
    }
}

/*
 * slocator_get_class -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
slocator_get_class (THREAD_ENTRY * thread_p, unsigned int rid, char *request,
		    int reqlen)
{
  OID class_oid, oid;
  int class_chn;
  LOCK lock;
  int prefetching;
  LC_COPYAREA *copy_area;
  int success;
  char *ptr;
  OR_ALIGNED_BUF (NET_COPY_AREA_SENDRECV_SIZE + OR_OID_SIZE +
		  OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  char *desc_ptr = NULL;
  int desc_size;
  char *content_ptr;
  int content_size;
  int num_objs = 0;

  ptr = or_unpack_oid (request, &class_oid);
  ptr = or_unpack_int (ptr, &class_chn);
  ptr = or_unpack_oid (ptr, &oid);
  ptr = or_unpack_lock (ptr, &lock);
  ptr = or_unpack_int (ptr, &prefetching);

  copy_area = NULL;
  success = xlocator_get_class (thread_p, &class_oid, class_chn, &oid, lock,
				prefetching, &copy_area);

  if (success != NO_ERROR)
    {
      /* ER_LK_UNILATERALLY_ABORTED may occur,
       * if it occurs, rollback transaction
       */
      if (er_errid () == ER_LK_UNILATERALLY_ABORTED)
	{
	  tran_server_unilaterally_abort_tran (thread_p);
	}
      return_error_to_client (rid);
    }

  if (copy_area != NULL)
    {
      num_objs = locator_send_copy_area (copy_area, &content_ptr,
					 &content_size, &desc_ptr,
					 &desc_size);
    }
  else
    {
      desc_ptr = NULL;
      desc_size = 0;
      content_ptr = NULL;
      content_size = 0;
    }

  /* Send sizes of databuffer and copy area (descriptor + content) */

  ptr = or_pack_int (reply, num_objs);
  ptr = or_pack_int (ptr, desc_size);
  ptr = or_pack_int (ptr, content_size);
  ptr = or_pack_oid (ptr, &class_oid);
  ptr = or_pack_int (ptr, success);

  if (copy_area == NULL)
    {
      css_send_data_to_client (rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
    }
  else
    {
      css_send_reply_and_2_data_to_client (rid, reply,
					   OR_ALIGNED_BUF_SIZE (a_reply),
					   desc_ptr, desc_size,
					   content_ptr, content_size);
      locator_free_copy_area (copy_area);
    }

  if (desc_ptr)
    {
      free_and_init (desc_ptr);
    }
}

/*
 * slocator_fetch_all -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
slocator_fetch_all (THREAD_ENTRY * thread_p, unsigned int rid, char *request,
		    int reqlen)
{
  HFID hfid;
  LOCK lock;
  OID class_oid, last_oid;
  int nobjects, nfetched;
  LC_COPYAREA *copy_area;
  int success;
  char *ptr;
  OR_ALIGNED_BUF (NET_COPY_AREA_SENDRECV_SIZE + (OR_INT_SIZE * 4) +
		  OR_OID_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  char *desc_ptr = NULL;
  int desc_size;
  char *content_ptr;
  int content_size;
  int num_objs = 0;

  ptr = or_unpack_hfid (request, &hfid);
  ptr = or_unpack_lock (ptr, &lock);
  ptr = or_unpack_oid (ptr, &class_oid);
  ptr = or_unpack_int (ptr, &nobjects);
  ptr = or_unpack_int (ptr, &nfetched);
  ptr = or_unpack_oid (ptr, &last_oid);

  copy_area = NULL;
  success = xlocator_fetch_all (thread_p, &hfid, &lock, &class_oid, &nobjects,
				&nfetched, &last_oid, &copy_area);

  if (success != NO_ERROR)
    {
      /* ER_LK_UNILATERALLY_ABORTED may occur,
       * if it occurs, rollback transaction
       */
      if (er_errid () == ER_LK_UNILATERALLY_ABORTED)
	{
	  tran_server_unilaterally_abort_tran (thread_p);
	}
      return_error_to_client (rid);
    }

  if (copy_area != NULL)
    {
      num_objs = locator_send_copy_area (copy_area, &content_ptr,
					 &content_size, &desc_ptr,
					 &desc_size);
    }
  else
    {
      desc_ptr = NULL;
      desc_size = 0;
      content_ptr = NULL;
      content_size = 0;
    }

  /* Send sizes of databuffer and copy area (descriptor + content) */

  ptr = or_pack_int (reply, num_objs);
  ptr = or_pack_int (ptr, desc_size);
  ptr = or_pack_int (ptr, content_size);
  ptr = or_pack_lock (ptr, lock);
  ptr = or_pack_int (ptr, nobjects);
  ptr = or_pack_int (ptr, nfetched);
  ptr = or_pack_oid (ptr, &last_oid);
  ptr = or_pack_int (ptr, success);

  if (copy_area == NULL)
    {
      css_send_data_to_client (rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
    }
  else
    {
      css_send_reply_and_2_data_to_client (rid, reply,
					   OR_ALIGNED_BUF_SIZE (a_reply),
					   desc_ptr, desc_size,
					   content_ptr, content_size);
      locator_free_copy_area (copy_area);
    }

  if (desc_ptr)
    {
      free_and_init (desc_ptr);
    }
}

/*
 * slocator_does_exist -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
slocator_does_exist (THREAD_ENTRY * thread_p, unsigned int rid, char *request,
		     int reqlen)
{
  OID oid, class_oid;
  int chn, class_chn, prefetch, doesexist;
  int need_fetching;
  LOCK lock;
  LC_COPYAREA *copy_area;
  char *ptr;
  OR_ALIGNED_BUF (NET_COPY_AREA_SENDRECV_SIZE +
		  OR_INT_SIZE + OR_OID_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  char *desc_ptr = NULL;
  int desc_size;
  char *content_ptr;
  int content_size;
  int num_objs = 0;

  ptr = or_unpack_oid (request, &oid);
  ptr = or_unpack_int (ptr, &chn);
  ptr = or_unpack_lock (ptr, &lock);
  ptr = or_unpack_oid (ptr, &class_oid);
  ptr = or_unpack_int (ptr, &class_chn);
  ptr = or_unpack_int (ptr, &need_fetching);
  ptr = or_unpack_int (ptr, &prefetch);

  copy_area = NULL;
  doesexist = xlocator_does_exist (thread_p, &oid, chn, lock, &class_oid,
				   class_chn, need_fetching, prefetch,
				   &copy_area);

  if (doesexist == LC_ERROR)
    {
      /* ER_LK_UNILATERALLY_ABORTED may occur,
       * if it occurs, rollback transaction
       */
      if (er_errid () == ER_LK_UNILATERALLY_ABORTED)
	{
	  tran_server_unilaterally_abort_tran (thread_p);
	}
      return_error_to_client (rid);
    }

  if (copy_area != NULL)
    {
      num_objs =
	locator_send_copy_area (copy_area, &content_ptr, &content_size,
				&desc_ptr, &desc_size);
    }
  else
    {
      desc_ptr = NULL;
      desc_size = 0;
      content_ptr = NULL;
      content_size = 0;
    }

  /* Send sizes of databuffer and copy area (descriptor + content) */

  ptr = or_pack_int (reply, num_objs);
  ptr = or_pack_int (ptr, desc_size);
  ptr = or_pack_int (ptr, content_size);
  ptr = or_pack_int (ptr, doesexist);
  ptr = or_pack_oid (ptr, &class_oid);

  if (copy_area == NULL)
    {
      css_send_data_to_client (rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
    }
  else
    {
      css_send_reply_and_2_data_to_client (rid, reply,
					   OR_ALIGNED_BUF_SIZE (a_reply),
					   desc_ptr, desc_size,
					   content_ptr, content_size);
      locator_free_copy_area (copy_area);
    }
  if (desc_ptr)
    {
      free_and_init (desc_ptr);
    }
}

/*
 * slocator_notify_isolation_incons -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
slocator_notify_isolation_incons (THREAD_ENTRY * thread_p, unsigned int rid,
				  char *request, int reqlen)
{
  LC_COPYAREA *copy_area;
  char *ptr;
  int success;
  OR_ALIGNED_BUF (NET_COPY_AREA_SENDRECV_SIZE + OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  char *desc_ptr = NULL;
  int desc_size;
  char *content_ptr;
  int content_size;
  int num_objs = 0;

  copy_area = NULL;
  success = xlocator_notify_isolation_incons (thread_p, &copy_area);
  if (success != NO_ERROR)
    {
      return_error_to_client (rid);
    }

  if (copy_area != NULL)
    {
      num_objs = locator_send_copy_area (copy_area, &content_ptr,
					 &content_size, &desc_ptr,
					 &desc_size);
    }
  else
    {
      desc_ptr = NULL;
      desc_size = 0;
      content_ptr = NULL;
      content_size = 0;
    }

  /* Send sizes of databuffer and copy area (descriptor + content) */

  ptr = or_pack_int (reply, num_objs);
  ptr = or_pack_int (ptr, desc_size);
  ptr = or_pack_int (ptr, content_size);

  ptr = or_pack_int (ptr, success);

  if (copy_area == NULL)
    {
      css_send_data_to_client (rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
    }
  else
    {
      css_send_reply_and_2_data_to_client (rid, reply,
					   OR_ALIGNED_BUF_SIZE (a_reply),
					   desc_ptr, desc_size,
					   content_ptr, content_size);
      locator_free_copy_area (copy_area);
    }
  if (desc_ptr)
    {
      free_and_init (desc_ptr);
    }
}

/*
 * slocator_force -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
slocator_force (THREAD_ENTRY * thread_p, unsigned int rid, char *request,
		int reqlen)
{
  int size;
  int success;
  LC_COPYAREA *copy_area = NULL;
  char *ptr;
  int csserror;
  OR_ALIGNED_BUF (OR_INT_SIZE * 3) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  int content_size;
  char *content_ptr = NULL, *new_content_ptr = NULL;
  int num_objs;
  char *packed_desc = NULL;
  int packed_desc_size;
  int start_multi_update;
  int end_multi_update;
  OID class_oid;
  LC_COPYAREA_MANYOBJS *mobjs;

  ptr = or_unpack_int (request, &num_objs);
  ptr = or_unpack_int (ptr, &start_multi_update);
  ptr = or_unpack_int (ptr, &end_multi_update);
  ptr = or_unpack_oid (ptr, &class_oid);
  ptr = or_unpack_int (ptr, &packed_desc_size);
  ptr = or_unpack_int (ptr, &content_size);

  csserror = 0;

  copy_area = locator_recv_allocate_copyarea (num_objs, &content_ptr,
					      content_size);
  if (copy_area)
    {
      if (num_objs > 0)
	{
	  csserror = css_receive_data_from_client (rid, &packed_desc, &size);
	}

      if (csserror)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_NET_SERVER_DATA_RECEIVE, 0);
	  css_send_abort_to_client (rid);
	  goto end;
	}
      else
	{
	  locator_unpack_copy_area_descriptor (num_objs, copy_area,
					       packed_desc);
	  mobjs = LC_MANYOBJS_PTR_IN_COPYAREA (copy_area);
	  mobjs->start_multi_update = start_multi_update;
	  mobjs->end_multi_update = end_multi_update;
	  COPY_OID (&mobjs->class_oid, &class_oid);

	  if (content_size > 0)
	    {
	      csserror = css_receive_data_from_client (rid, &new_content_ptr,
						       &size);

	      if (new_content_ptr != NULL)
		{
		  memcpy (content_ptr, new_content_ptr, size);
		  free_and_init (new_content_ptr);
		}

	      if (csserror)
		{
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			  ER_NET_SERVER_DATA_RECEIVE, 0);
		  css_send_abort_to_client (rid);
		  goto end;
		}
	    }

	  success = xlocator_force (thread_p, copy_area);

	  /*
	   * Send the descriptor part since some information about the objects
	   * (e.g., OIDs) may be send back to client.
	   * Don't need to send the content since it is not updated.
	   */

	  locator_pack_copy_area_descriptor (num_objs, copy_area,
					     packed_desc);
	  ptr = or_pack_int (reply, success);
	  ptr = or_pack_int (ptr, packed_desc_size);
	  ptr = or_pack_int (ptr, 0);

	  if (success != NO_ERROR)
	    {
	      /* ER_LK_UNILATERALLY_ABORTED may not occur,
	       * but if it occurs, rollback transaction
	       */
	      if (er_errid () == ER_LK_UNILATERALLY_ABORTED)
		{
		  tran_server_unilaterally_abort_tran (thread_p);
		}
	      return_error_to_client (rid);
	    }

	  css_send_reply_and_2_data_to_client (rid, reply,
					       OR_ALIGNED_BUF_SIZE (a_reply),
					       packed_desc,
					       packed_desc_size, NULL, 0);
	  if (packed_desc)
	    {
	      free_and_init (packed_desc);
	    }
	}
    }

end:
  if (packed_desc)
    {
      free_and_init (packed_desc);
    }
  if (copy_area != NULL)
    {
      locator_free_copy_area (copy_area);
    }
}

/*
 * slocator_fetch_lockset -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
slocator_fetch_lockset (THREAD_ENTRY * thread_p, unsigned int rid,
			char *request, int reqlen)
{
  int success;
  LC_COPYAREA *copy_area;
  LC_LOCKSET *lockset;
  OR_ALIGNED_BUF (NET_SENDRECV_BUFFSIZE + NET_COPY_AREA_SENDRECV_SIZE +
		  OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  char *desc_ptr;
  int desc_size;
  char *content_ptr;
  int content_size;
  char *ptr;
  bool first_call;
  int num_objs;
  char *packed = NULL;
  int packed_size;
  int send_size;

  ptr = or_unpack_int (request, &packed_size);

  if (packed_size == 0
      || css_receive_data_from_client (rid, &packed, (int *) &packed_size))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_NET_SERVER_DATA_RECEIVE,
	      0);
      css_send_abort_to_client (rid);
      if (packed)
	free_and_init (packed);
      return;
    }

  lockset = locator_allocate_and_unpack_lockset (packed, packed_size, true,
						 true, false);
  if (packed)
    {
      free_and_init (packed);
    }
  if ((lockset == NULL) || (lockset->length <= 0))
    {
      return_error_to_client (rid);
      ptr = or_pack_int (reply, 0);
      ptr = or_pack_int (ptr, 0);
      ptr = or_pack_int (ptr, 0);
      ptr = or_pack_int (ptr, 0);
      ptr = or_pack_int (ptr, ER_FAILED);
      css_send_data_to_client (rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
      return;
    }

  first_call = true;
  do
    {
      desc_ptr = NULL;
      num_objs = 0;

      copy_area = NULL;
      success = xlocator_fetch_lockset (thread_p, lockset, &copy_area);

      if (success != NO_ERROR)
	{
	  /* ER_LK_UNILATERALLY_ABORTED may occur,
	   * if it occurs, rollback transaction
	   */
	  if (er_errid () == ER_LK_UNILATERALLY_ABORTED)
	    {
	      tran_server_unilaterally_abort_tran (thread_p);
	    }
	  return_error_to_client (rid);
	}

      if (copy_area != NULL)
	{
	  num_objs = locator_send_copy_area (copy_area, &content_ptr,
					     &content_size, &desc_ptr,
					     &desc_size);
	}
      else
	{
	  desc_ptr = NULL;
	  desc_size = 0;
	  content_ptr = NULL;
	  content_size = 0;
	}

      /* Send sizes of databuffer and copy area (descriptor + content) */

      send_size = locator_pack_lockset (lockset, first_call, false);

      packed = lockset->packed;
      packed_size = lockset->packed_size;

      ptr = or_pack_int (reply, send_size);
      ptr = or_pack_int (ptr, num_objs);
      ptr = or_pack_int (ptr, desc_size);
      ptr = or_pack_int (ptr, content_size);

      ptr = or_pack_int (ptr, success);

      if (copy_area == NULL && lockset == NULL)
	{
	  css_send_data_to_client (rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
	}
      else
	{
	  css_send_reply_and_3_data_to_client (rid, reply,
					       OR_ALIGNED_BUF_SIZE (a_reply),
					       packed, send_size,
					       desc_ptr, desc_size,
					       content_ptr, content_size);
	}
      if (copy_area != NULL)
	{
	  locator_free_copy_area (copy_area);
	}
      if (desc_ptr)
	{
	  free_and_init (desc_ptr);
	}

      first_call = false;
    }
  while (copy_area && lockset
	 && ((lockset->num_classes_of_reqobjs >
	      lockset->num_classes_of_reqobjs_processed)
	     || (lockset->num_reqobjs > lockset->num_reqobjs_processed)));

  if (lockset)
    {
      locator_free_lockset (lockset);
    }
}

/*
 * slocator_fetch_all_reference_lockset -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
slocator_fetch_all_reference_lockset (THREAD_ENTRY * thread_p,
				      unsigned int rid, char *request,
				      int reqlen)
{
  OID oid;
  int chn;
  LOCK lock;
  OID class_oid;
  int class_chn;
  int prune_level;
  int quit_on_errors;
  int success;
  LC_COPYAREA *copy_area;
  LC_LOCKSET *lockset;
  OR_ALIGNED_BUF (NET_SENDRECV_BUFFSIZE + NET_COPY_AREA_SENDRECV_SIZE +
		  OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  char *desc_ptr = NULL;
  int desc_size;
  char *content_ptr;
  int content_size;
  char *ptr;
  int num_objs = 0;
  char *packed = NULL;
  int packed_size;
  int send_size = 0;

  ptr = or_unpack_oid (request, &oid);
  ptr = or_unpack_int (ptr, &chn);
  ptr = or_unpack_oid (ptr, &class_oid);
  ptr = or_unpack_int (ptr, &class_chn);
  ptr = or_unpack_lock (ptr, &lock);
  ptr = or_unpack_int (ptr, &quit_on_errors);
  ptr = or_unpack_int (ptr, &prune_level);

  lockset = NULL;
  copy_area = NULL;

  success = xlocator_fetch_all_reference_lockset (thread_p, &oid, chn,
						  &class_oid, class_chn, lock,
						  quit_on_errors, prune_level,
						  &lockset, &copy_area);

  if (success != NO_ERROR)
    {
      /* ER_LK_UNILATERALLY_ABORTED may occur,
       * if it occurs, rollback transaction
       */
      if (er_errid () == ER_LK_UNILATERALLY_ABORTED)
	{
	  tran_server_unilaterally_abort_tran (thread_p);
	}
      return_error_to_client (rid);
    }

  if (lockset != NULL && lockset->length > 0)
    {
      send_size = locator_pack_lockset (lockset, true, true);

      packed = lockset->packed;
      packed_size = lockset->packed_size;

      if (!packed)
	{
	  return_error_to_client (rid);
	  success = ER_FAILED;
	}
    }

  if (copy_area != NULL)
    {
      num_objs = locator_send_copy_area (copy_area, &content_ptr,
					 &content_size, &desc_ptr,
					 &desc_size);
    }
  else
    {
      desc_ptr = NULL;
      desc_size = 0;
      content_ptr = NULL;
      content_size = 0;
    }

  /* Send sizes of databuffer and copy area (descriptor + content) */

  ptr = or_pack_int (reply, send_size);
  ptr = or_pack_int (ptr, num_objs);
  ptr = or_pack_int (ptr, desc_size);
  ptr = or_pack_int (ptr, content_size);

  ptr = or_pack_int (ptr, success);

  if (copy_area == NULL && lockset == NULL)
    {
      css_send_data_to_client (rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
    }
  else
    {
      css_send_reply_and_3_data_to_client (rid, reply,
					   OR_ALIGNED_BUF_SIZE (a_reply),
					   packed, send_size,
					   desc_ptr, desc_size,
					   content_ptr, content_size);
      if (copy_area != NULL)
	{
	  locator_free_copy_area (copy_area);
	}

      if (lockset != NULL)
	{
	  locator_free_lockset (lockset);
	}

      if (desc_ptr)
	{
	  free_and_init (desc_ptr);
	}
    }
}

/*
 * slocator_find_class_oid -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
slocator_find_class_oid (THREAD_ENTRY * thread_p, unsigned int rid,
			 char *request, int reqlen)
{
  LC_FIND_CLASSNAME found;
  char *classname;
  OID class_oid;
  LOCK lock;
  char *ptr;
  OR_ALIGNED_BUF (OR_INT_SIZE + OR_OID_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  ptr = or_unpack_string_nocopy (request, &classname);
  ptr = or_unpack_oid (ptr, &class_oid);
  ptr = or_unpack_lock (ptr, &lock);

  found = xlocator_find_class_oid (thread_p, classname, &class_oid, lock);

  if (found == LC_CLASSNAME_ERROR)
    {
      /* ER_LK_UNILATERALLY_ABORTED may occur,
       * if it occurs, rollback transaction
       */
      if (er_errid () == ER_LK_UNILATERALLY_ABORTED)
	{
	  tran_server_unilaterally_abort_tran (thread_p);
	}
      return_error_to_client (rid);
    }

  ptr = or_pack_int (reply, found);
  ptr = or_pack_oid (ptr, &class_oid);
  css_send_data_to_client (rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * slocator_reserve_classname -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
slocator_reserve_classname (THREAD_ENTRY * thread_p, unsigned int rid,
			    char *request, int reqlen)
{
  LC_FIND_CLASSNAME reserved;
  char *classname;
  OID class_oid;
  char *ptr;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  ptr = or_unpack_string_nocopy (request, &classname);
  ptr = or_unpack_oid (ptr, &class_oid);

  reserved = xlocator_reserve_class_name (thread_p, classname, &class_oid);
  if (reserved == LC_CLASSNAME_ERROR)
    {
      /* ER_LK_UNILATERALLY_ABORTED may occur,
       * if it occurs, rollback transaction
       */
      if (er_errid () == ER_LK_UNILATERALLY_ABORTED)
	{
	  tran_server_unilaterally_abort_tran (thread_p);
	}
      return_error_to_client (rid);
    }

  (void) or_pack_int (reply, reserved);
  css_send_data_to_client (rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * slocator_delete_class_name -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
slocator_delete_class_name (THREAD_ENTRY * thread_p, unsigned int rid,
			    char *request, int reqlen)
{
  char *classname;
  LC_FIND_CLASSNAME deleted;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  (void) or_unpack_string_nocopy (request, &classname);

  deleted = xlocator_delete_class_name (thread_p, classname);
  if (deleted == LC_CLASSNAME_ERROR)
    {
      /* ER_LK_UNILATERALLY_ABORTED may occur,
       * if it occurs, rollback transaction
       */
      if (er_errid () == ER_LK_UNILATERALLY_ABORTED)
	{
	  tran_server_unilaterally_abort_tran (thread_p);
	}
      return_error_to_client (rid);
    }

  (void) or_pack_int (reply, deleted);
  css_send_data_to_client (rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * slocator_rename_class_name -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
slocator_rename_class_name (THREAD_ENTRY * thread_p, unsigned int rid,
			    char *request, int reqlen)
{
  char *oldname, *newname;
  OID class_oid;
  LC_FIND_CLASSNAME renamed;
  char *ptr;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  ptr = or_unpack_string_nocopy (request, &oldname);
  ptr = or_unpack_string_nocopy (ptr, &newname);
  ptr = or_unpack_oid (ptr, &class_oid);

  renamed = xlocator_rename_class_name (thread_p, oldname, newname,
					&class_oid);
  if (renamed == LC_CLASSNAME_ERROR)
    {
      /* ER_LK_UNILATERALLY_ABORTED may occur,
       * if it occurs, rollback transaction
       */
      if (er_errid () == ER_LK_UNILATERALLY_ABORTED)
	{
	  tran_server_unilaterally_abort_tran (thread_p);
	}
      return_error_to_client (rid);
    }

  (void) or_pack_int (reply, renamed);
  css_send_data_to_client (rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * slocator_assign_oid -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
slocator_assign_oid (THREAD_ENTRY * thread_p, unsigned int rid, char *request,
		     int reqlen)
{
  HFID hfid;
  int expected_length;
  OID class_oid, perm_oid;
  char *classname;
  int success = NO_ERROR;
  char *ptr;
  OR_ALIGNED_BUF (OR_INT_SIZE + OR_OID_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  ptr = or_unpack_hfid (request, &hfid);
  ptr = or_unpack_int (ptr, &expected_length);
  ptr = or_unpack_oid (ptr, &class_oid);
  ptr = or_unpack_string_nocopy (ptr, &classname);

  if (xlocator_assign_oid (thread_p, &hfid, &perm_oid, expected_length,
			   &class_oid, classname) != NO_ERROR)
    {
      success = ER_FAILED;
      /* ER_LK_UNILATERALLY_ABORTED may not occur,
       * but if it occurs, rollback transaction
       */
      if (er_errid () == ER_LK_UNILATERALLY_ABORTED)
	{
	  tran_server_unilaterally_abort_tran (thread_p);
	}
      return_error_to_client (rid);
    }

  ptr = or_pack_int (reply, success);
  ptr = or_pack_oid (ptr, &perm_oid);
  css_send_data_to_client (rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * sqst_server_get_statistics -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
sqst_server_get_statistics (THREAD_ENTRY * thread_p, unsigned int rid,
			    char *request, int reqlen)
{
  OID classoid;
  unsigned int timestamp;
  char *ptr;
  char *buffer;
  int buffer_length;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  ptr = or_unpack_oid (request, &classoid);
  ptr = or_unpack_int (ptr, (int *) &timestamp);

  buffer = xstats_get_statistics_from_server (thread_p, &classoid, timestamp,
					      &buffer_length);
  if (buffer == NULL && buffer_length < 0)
    {
      return_error_to_client (rid);
      buffer_length = 0;
    }

  (void) or_pack_int (reply, buffer_length);

  css_send_reply_and_data_to_client (rid, reply,
				     OR_ALIGNED_BUF_SIZE (a_reply), buffer,
				     buffer_length);
  if (buffer != NULL)
    {
      /* since this was copied to the client, we don't need it on the server */
      free_and_init (buffer);
    }
}

/*
 * slog_client_get_first_postpone -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
slog_client_get_first_postpone (THREAD_ENTRY * thread_p, unsigned int rid,
				char *request, int reqlen)
{
  char *ptr;
  LOG_COPY *log_area;
  LOG_LSA lsa;
  OR_ALIGNED_BUF (OR_INT_SIZE * 3 + OR_LOG_LSA_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  char *desc_ptr = NULL;
  int desc_size;
  char *content_ptr;
  int content_size;
  int num_records = 0;

  log_area = xlog_client_get_first_postpone (thread_p, &lsa);

  if (log_area == NULL)
    {
      return_error_to_client (rid);
      desc_ptr = NULL;
      desc_size = 0;
      content_ptr = NULL;
      content_size = 0;
    }
  else
    {
      num_records = log_copy_area_send (log_area, &content_ptr, &content_size,
					&desc_ptr, &desc_size);
    }
  ptr = or_pack_int (reply, num_records);
  ptr = or_pack_int (ptr, desc_size);
  ptr = or_pack_int (ptr, content_size);
  ptr = or_pack_log_lsa (ptr, &lsa);

  if (log_area == NULL)
    {
      css_send_data_to_client (rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
    }
  else
    {
      css_send_reply_and_2_data_to_client (rid, reply,
					   OR_ALIGNED_BUF_SIZE (a_reply),
					   desc_ptr, desc_size,
					   content_ptr, content_size);
      log_free_client_copy_area (log_area);
    }
  if (desc_ptr)
    {
      free_and_init (desc_ptr);
    }
}

/*
 * slog_client_get_next_postpone -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
slog_client_get_next_postpone (THREAD_ENTRY * thread_p, unsigned int rid,
			       char *request, int reqlen)
{
  LOG_COPY *log_area;
  LOG_LSA lsa;
  char *ptr;
  OR_ALIGNED_BUF (OR_INT_SIZE * 3 + OR_LOG_LSA_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  char *desc_ptr = NULL;
  int desc_size;
  char *content_ptr;
  int content_size;
  int num_records = 0;

  log_area = xlog_client_get_next_postpone (thread_p, &lsa);

  if (log_area == NULL)
    {
      return_error_to_client (rid);
      desc_ptr = NULL;
      desc_size = 0;
      content_ptr = NULL;
      content_size = 0;
    }
  else
    {
      num_records = log_copy_area_send (log_area, &content_ptr, &content_size,
					&desc_ptr, &desc_size);
    }
  ptr = or_pack_int (reply, num_records);
  ptr = or_pack_int (ptr, desc_size);
  ptr = or_pack_int (ptr, content_size);
  ptr = or_pack_log_lsa (ptr, &lsa);

  if (log_area == NULL)
    {
      css_send_data_to_client (rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
    }
  else
    {
      css_send_reply_and_2_data_to_client (rid, reply,
					   OR_ALIGNED_BUF_SIZE (a_reply),
					   desc_ptr, desc_size,
					   content_ptr, content_size);
      log_free_client_copy_area (log_area);
    }
  if (desc_ptr)
    {
      free_and_init (desc_ptr);
    }
}

/*
 * slog_client_get_first_undo -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
slog_client_get_first_undo (THREAD_ENTRY * thread_p, unsigned int rid,
			    char *request, int reqlen)
{
  char *ptr;
  LOG_COPY *log_area;
  LOG_LSA lsa;
  OR_ALIGNED_BUF (OR_INT_SIZE * 3 + OR_LOG_LSA_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  char *desc_ptr = NULL;
  int desc_size;
  char *content_ptr;
  int content_size;
  int num_records = 0;

  log_area = xlog_client_get_first_undo (thread_p, &lsa);

  if (log_area == NULL)
    {
      return_error_to_client (rid);
      desc_ptr = NULL;
      desc_size = 0;
      content_ptr = NULL;
      content_size = 0;
    }
  else
    {
      num_records = log_copy_area_send (log_area, &content_ptr, &content_size,
					&desc_ptr, &desc_size);
    }
  ptr = or_pack_int (reply, num_records);
  ptr = or_pack_int (ptr, desc_size);
  ptr = or_pack_int (ptr, content_size);
  ptr = or_pack_log_lsa (ptr, &lsa);

  if (log_area == NULL)
    {
      css_send_data_to_client (rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
    }
  else
    {
      css_send_reply_and_2_data_to_client (rid, reply,
					   OR_ALIGNED_BUF_SIZE (a_reply),
					   desc_ptr, desc_size,
					   content_ptr, content_size);
      log_free_client_copy_area (log_area);
    }
  if (desc_ptr)
    {
      free_and_init (desc_ptr);
    }
}

/*
 * slog_client_get_next_undo -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
slog_client_get_next_undo (THREAD_ENTRY * thread_p, unsigned int rid,
			   char *request, int reqlen)
{
  LOG_COPY *log_area;
  LOG_LSA lsa;
  char *ptr;
  OR_ALIGNED_BUF (OR_INT_SIZE * 3 + OR_LOG_LSA_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  char *desc_ptr = NULL;
  int desc_size;
  char *content_ptr;
  int content_size;
  int num_records = 0;

  log_area = xlog_client_get_next_undo (thread_p, &lsa);

  if (log_area == NULL)
    {
      return_error_to_client (rid);
      desc_ptr = NULL;
      desc_size = 0;
      content_ptr = NULL;
      content_size = 0;
    }
  else
    {
      num_records = log_copy_area_send (log_area, &content_ptr, &content_size,
					&desc_ptr, &desc_size);
    }
  ptr = or_pack_int (reply, num_records);
  ptr = or_pack_int (ptr, desc_size);
  ptr = or_pack_int (ptr, content_size);
  ptr = or_pack_log_lsa (ptr, &lsa);

  if (log_area == NULL)
    {
      css_send_data_to_client (rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
    }
  else
    {
      css_send_reply_and_2_data_to_client (rid, reply,
					   OR_ALIGNED_BUF_SIZE (a_reply),
					   desc_ptr, desc_size,
					   content_ptr, content_size);
      log_free_client_copy_area (log_area);
    }
  if (desc_ptr)
    {
      free_and_init (desc_ptr);
    }
}

/*
 * slog_client_unknown_state_abort_get_first_undo -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
slog_client_unknown_state_abort_get_first_undo (THREAD_ENTRY * thread_p,
						unsigned int rid,
						char *request, int reqlen)
{
  LOG_COPY *log_area;
  LOG_LSA lsa;
  char *ptr;
  OR_ALIGNED_BUF (OR_INT_SIZE * 3 + OR_LOG_LSA_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  char *desc_ptr = NULL;
  int desc_size;
  char *content_ptr;
  int content_size;
  int num_records = 0;

  log_area = xlog_client_unknown_state_abort_get_first_undo (thread_p, &lsa);

  if (log_area == NULL)
    {
      return_error_to_client (rid);
      desc_ptr = NULL;
      desc_size = 0;
      content_ptr = NULL;
      content_size = 0;
    }
  else
    {
      num_records = log_copy_area_send (log_area, &content_ptr, &content_size,
					&desc_ptr, &desc_size);
    }
  ptr = or_pack_int (reply, num_records);
  ptr = or_pack_int (ptr, desc_size);
  ptr = or_pack_int (ptr, content_size);
  ptr = or_pack_log_lsa (ptr, &lsa);

  if (log_area == NULL)
    {
      css_send_data_to_client (rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
    }
  else
    {
      css_send_reply_and_2_data_to_client (rid, reply,
					   OR_ALIGNED_BUF_SIZE (a_reply),
					   desc_ptr, desc_size,
					   content_ptr, content_size);
      log_free_client_copy_area (log_area);
    }
  if (desc_ptr)
    {
      free_and_init (desc_ptr);
    }
}

/*
 * slog_append_client_undo -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
slog_append_client_undo (THREAD_ENTRY * thread_p, unsigned int rid,
			 char *request, int reqlen)
{
  LOG_RCVCLIENT_INDEX rcvindex;
  int length;
  char *data;
  int csserror;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  (void) or_unpack_int (request, &rcvindex);

  data = NULL;
  csserror = css_receive_data_from_client (rid, &data, (int *) &length);

  if (csserror)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_NET_SERVER_DATA_RECEIVE,
	      0);
      css_send_abort_to_client (rid);
    }
  else
    {
      xlog_append_client_undo (thread_p, rcvindex, length, data);
    }
  if (data != NULL)
    {
      free_and_init (data);
    }
  /* just send back a dummy message */
  or_pack_int (reply, 0);
  css_send_data_to_client (rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * slog_append_client_postpone -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
slog_append_client_postpone (THREAD_ENTRY * thread_p, unsigned int rid,
			     char *request, int reqlen)
{
  LOG_RCVCLIENT_INDEX rcvindex;
  int length;
  char *data;
  int csserror;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  (void) or_unpack_int (request, &rcvindex);

  data = NULL;
  csserror = css_receive_data_from_client (rid, &data, (int *) &length);

  if (csserror)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_NET_SERVER_DATA_RECEIVE,
	      0);
      css_send_abort_to_client (rid);
    }
  else
    {
      xlog_append_client_postpone (thread_p, rcvindex, length, data);
    }
  if (data != NULL)
    {
      free_and_init (data);
    }
  /* just send back a dummy message */
  or_pack_int (reply, 0);
  css_send_data_to_client (rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * slog_client_complete_postpone -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
slog_client_complete_postpone (THREAD_ENTRY * thread_p, unsigned int rid,
			       char *request, int reqlen)
{
  TRAN_STATE transtate;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  transtate = xlog_client_complete_postpone (thread_p);

  (void) or_pack_int (reply, (int) transtate);
  css_send_data_to_client (rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * slog_client_complete_undo -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
slog_client_complete_undo (THREAD_ENTRY * thread_p, unsigned int rid,
			   char *request, int reqlen)
{
  TRAN_STATE transtate;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  transtate = xlog_client_complete_undo (thread_p);

  (void) or_pack_int (reply, (int) transtate);
  css_send_data_to_client (rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * slogtb_has_updated -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
slogtb_has_updated (THREAD_ENTRY * thread_p, unsigned int rid, char *request,
		    int reqlen)
{
  int has_updated;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  has_updated = logtb_has_updated (thread_p);

  (void) or_pack_int (reply, has_updated);
  css_send_data_to_client (rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * slogtb_set_interrupt -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
slogtb_set_interrupt (THREAD_ENTRY * thread_p, unsigned int rid,
		      char *request, int reqlen)
{
  int set;

  (void) or_unpack_int (request, &set);
  xlogtb_set_interrupt (thread_p, set);

  /*
   *  No reply expected...
   */
}

/*
 * slogtb_reset_wait_secs -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
slogtb_reset_wait_secs (THREAD_ENTRY * thread_p, unsigned int rid,
			char *request, int reqlen)
{
  int waitsecs;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  (void) or_unpack_int (request, &waitsecs);

  waitsecs = xlogtb_reset_wait_secs (thread_p, waitsecs);

  (void) or_pack_int (reply, waitsecs);
  css_send_data_to_client (rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * slogtb_reset_isolation -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
slogtb_reset_isolation (THREAD_ENTRY * thread_p, unsigned int rid,
			char *request, int reqlen)
{
  int isolation, unlock_by_isolation, error_code;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  char *ptr;

  ptr = or_unpack_int (request, &isolation);
  ptr = or_unpack_int (ptr, &unlock_by_isolation);

  error_code = (int) xlogtb_reset_isolation (thread_p,
					     (TRAN_ISOLATION) isolation,
					     (bool) unlock_by_isolation);

  (void) or_pack_int (reply, error_code);
  if (error_code != NO_ERROR)
    {
      return_error_to_client (rid);
    }
  css_send_data_to_client (rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * slogpb_dump_stat -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
slogpb_dump_stat (unsigned int rid, char *request, int reqlen)
{
  FILE *outfp;
  int file_size;
  char *buffer;
  int buffer_size;
  int send_size;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  (void) or_unpack_int (request, &buffer_size);

  buffer = (char *) db_private_alloc (NULL, buffer_size);
  if (buffer == NULL)
    {
      css_send_abort_to_client (rid);
      return;
    }

  outfp = tmpfile ();
  if (outfp == NULL)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR,
			   0);
      css_send_abort_to_client (rid);
      db_private_free_and_init (NULL, buffer);
      return;
    }

  xlogpb_dump_stat (outfp);
  file_size = ftell (outfp);

  /*
   * Send the file in pieces
   */
  rewind (outfp);

  (void) or_pack_int (reply, (int) file_size);
  css_send_data_to_client (rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));

  while (file_size > 0)
    {
      if (file_size > buffer_size)
	{
	  send_size = buffer_size;
	}
      else
	{
	  send_size = file_size;
	}

      file_size -= send_size;
      if (fread (buffer, 1, send_size, outfp) == 0)
	{
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			       ER_GENERIC_ERROR, 0);
	  css_send_abort_to_client (rid);
	  /*
	   * Continue sending the stuff that was prmoised to client. In this case
	   * junk (i.e., whatever it is in the buffers) is sent.
	   */
	}
      css_send_data_to_client (rid, buffer, send_size);
    }
  fclose (outfp);
  db_private_free_and_init (NULL, buffer);
}

/*
 * slock_dump -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
slock_dump (THREAD_ENTRY * thread_p, unsigned int rid, char *request,
	    int reqlen)
{
  FILE *outfp;
  int file_size;
  char *buffer;
  int buffer_size;
  int send_size;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  (void) or_unpack_int (request, &buffer_size);

  buffer = (char *) db_private_alloc (thread_p, buffer_size);
  if (buffer == NULL)
    {
      css_send_abort_to_client (rid);
      return;
    }

  outfp = tmpfile ();
  if (outfp == NULL)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR,
			   0);
      css_send_abort_to_client (rid);
      db_private_free_and_init (thread_p, buffer);
      return;
    }

  xlock_dump (thread_p, outfp);
  file_size = ftell (outfp);

  /*
   * Send the file in pieces
   */
  rewind (outfp);

  (void) or_pack_int (reply, (int) file_size);
  css_send_data_to_client (rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));

  while (file_size > 0)
    {
      if (file_size > buffer_size)
	{
	  send_size = buffer_size;
	}
      else
	{
	  send_size = file_size;
	}

      file_size -= send_size;
      if (fread (buffer, 1, send_size, outfp) == 0)
	{
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			       ER_GENERIC_ERROR, 0);
	  css_send_abort_to_client (rid);
	  /*
	   * Continue sending the stuff that was prmoised to client. In this case
	   * junk (i.e., whatever it is in the buffers) is sent.
	   */
	}
      css_send_data_to_client (rid, buffer, send_size);
    }
  fclose (outfp);
  db_private_free_and_init (thread_p, buffer);
}

/*
 * shf_create -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
shf_create (THREAD_ENTRY * thread_p, unsigned int rid, char *request,
	    int reqlen)
{
  int error;
  HFID hfid;
  char *ptr;
  OID class_oid;
  OR_ALIGNED_BUF (OR_INT_SIZE + OR_HFID_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  ptr = or_unpack_hfid (request, &hfid);
  ptr = or_unpack_oid (ptr, &class_oid);

  error = xheap_create (thread_p, &hfid, &class_oid);
  if (error != NO_ERROR)
    {
      /* ER_LK_UNILATERALLY_ABORTED may not occur,
       * but if it occurs, rollback transaction
       */
      if (er_errid () == ER_LK_UNILATERALLY_ABORTED)
	{
	  tran_server_unilaterally_abort_tran (thread_p);
	}
      return_error_to_client (rid);
    }

  ptr = or_pack_errcode (reply, error);
  ptr = or_pack_hfid (ptr, &hfid);
  css_send_data_to_client (rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * shf_destroy -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
shf_destroy (THREAD_ENTRY * thread_p, unsigned int rid, char *request,
	     int reqlen)
{
  int error;
  HFID hfid;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  (void) or_unpack_hfid (request, &hfid);

  error = xheap_destroy (thread_p, &hfid);
  if (error != NO_ERROR)
    {
      /* ER_LK_UNILATERALLY_ABORTED may not occur,
       * but if it occurs, rollback transaction
       */
      if (er_errid () == ER_LK_UNILATERALLY_ABORTED)
	{
	  tran_server_unilaterally_abort_tran (thread_p);
	}
      return_error_to_client (rid);
    }

  (void) or_pack_errcode (reply, error);
  css_send_data_to_client (rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * shf_destroy_when_new -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
shf_destroy_when_new (THREAD_ENTRY * thread_p, unsigned int rid,
		      char *request, int reqlen)
{
  int error;
  HFID hfid;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  (void) or_unpack_hfid (request, &hfid);

  error = xheap_destroy_newly_created (thread_p, &hfid);
  if (error != NO_ERROR)
    {
      /* ER_LK_UNILATERALLY_ABORTED may not occur,
       * but if it occurs, rollback transaction
       */
      if (er_errid () == ER_LK_UNILATERALLY_ABORTED)
	{
	  tran_server_unilaterally_abort_tran (thread_p);
	}
      return_error_to_client (rid);
    }

  (void) or_pack_errcode (reply, error);
  css_send_data_to_client (rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * stran_server_commit -
 *
 * return:
 *
 *   thrd(in):
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
stran_server_commit (THREAD_ENTRY * thrd, unsigned int rid,
		     char *request, int reqlen)
{
  TRAN_STATE state;
  int xretain_lock;
  bool retain_lock;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  (void) or_unpack_int (request, &xretain_lock);
  retain_lock = (bool) xretain_lock;

  state = xtran_server_commit (thrd, retain_lock);

  net_cleanup_server_queues (rid);

  if (state != TRAN_UNACTIVE_COMMITTED
      && state != TRAN_UNACTIVE_COMMITTED_INFORMING_PARTICIPANTS
      && state != TRAN_UNACTIVE_COMMITTED_WITH_CLIENT_USER_LOOSE_ENDS)
    {
      /* Likely the commit failed.. somehow */
      return_error_to_client (rid);
    }

  (void) or_pack_int (reply, (int) state);
  css_send_data_to_client (rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * stran_server_abort -
 *
 * return:
 *
 *   thrd(in):
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
stran_server_abort (THREAD_ENTRY * thrd, unsigned int rid,
		    char *request, int reqlen)
{
  TRAN_STATE state;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  state = xtran_server_abort (thrd);

  net_cleanup_server_queues (rid);

  if (state != TRAN_UNACTIVE_ABORTED
      && state != TRAN_UNACTIVE_ABORTED_INFORMING_PARTICIPANTS
      && state != TRAN_UNACTIVE_ABORTED_WITH_CLIENT_USER_LOOSE_ENDS)
    {
      /* Likely the abort failed.. somehow */
      return_error_to_client (rid);
    }

  (void) or_pack_int (reply, state);
  css_send_data_to_client (rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * stran_server_has_updated -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
stran_server_has_updated (THREAD_ENTRY * thread_p, unsigned int rid,
			  char *request, int reqlen)
{
  int has_updated;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  has_updated = xtran_server_has_updated (thread_p);

  (void) or_pack_int (reply, has_updated);
  css_send_data_to_client (rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * stran_server_start_topop -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
stran_server_start_topop (THREAD_ENTRY * thread_p, unsigned int rid,
			  char *request, int reqlen)
{
  int success = NO_ERROR;
  LOG_LSA topop_lsa;
  OR_ALIGNED_BUF (OR_INT_SIZE + OR_LOG_LSA_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  char *ptr;

  if (xtran_server_start_topop (thread_p, &topop_lsa) != NO_ERROR)
    {
      success = ER_FAILED;
      return_error_to_client (rid);
    }

  ptr = or_pack_int (reply, success);
  ptr = or_pack_log_lsa (ptr, &topop_lsa);
  css_send_data_to_client (rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * stran_server_end_topop -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
stran_server_end_topop (THREAD_ENTRY * thread_p, unsigned int rid,
			char *request, int reqlen)
{
  TRAN_STATE state;
  LOG_LSA topop_lsa;
  int xresult;
  LOG_RESULT_TOPOP result;
  OR_ALIGNED_BUF (OR_INT_SIZE + OR_LOG_LSA_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  char *ptr;

  (void) or_unpack_int (request, &xresult);
  result = (LOG_RESULT_TOPOP) xresult;

  state = xtran_server_end_topop (thread_p, result, &topop_lsa);

  ptr = or_pack_int (reply, (int) state);
  ptr = or_pack_log_lsa (ptr, &topop_lsa);
  css_send_data_to_client (rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * stran_server_savepoint -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
stran_server_savepoint (THREAD_ENTRY * thread_p, unsigned int rid,
			char *request, int reqlen)
{
  int success = NO_ERROR;
  char *savept_name;
  LOG_LSA topop_lsa;
  OR_ALIGNED_BUF (OR_INT_SIZE + OR_LOG_LSA_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  char *ptr;

  ptr = or_unpack_string_nocopy (request, &savept_name);

  if (xtran_server_savepoint (thread_p, savept_name, &topop_lsa) != NO_ERROR)
    {
      success = ER_FAILED;
      return_error_to_client (rid);
    }

  ptr = or_pack_int (reply, success);
  ptr = or_pack_log_lsa (ptr, &topop_lsa);
  css_send_data_to_client (rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * stran_server_partial_abort -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
stran_server_partial_abort (THREAD_ENTRY * thread_p, unsigned int rid,
			    char *request, int reqlen)
{
  TRAN_STATE state;
  char *savept_name;
  LOG_LSA savept_lsa;
  OR_ALIGNED_BUF (OR_INT_SIZE + OR_LOG_LSA_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  char *ptr;

  ptr = or_unpack_string_nocopy (request, &savept_name);

  state = xtran_server_partial_abort (thread_p, savept_name, &savept_lsa);
  if (state != TRAN_UNACTIVE_ABORTED
      && state != TRAN_UNACTIVE_TOPOPE_ABORTED_WITH_CLIENT_USER_LOOSE_ENDS
      && state != TRAN_UNACTIVE_ABORTED_WITH_CLIENT_USER_LOOSE_ENDS)
    {
      /* Likely the abort failed.. somehow */
      return_error_to_client (rid);
    }

  ptr = or_pack_int (reply, (int) state);
  ptr = or_pack_log_lsa (ptr, &savept_lsa);
  css_send_data_to_client (rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * stran_server_is_active_and_has_updated -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
stran_server_is_active_and_has_updated (THREAD_ENTRY * thread_p,
					unsigned int rid, char *request,
					int reqlen)
{
  int isactive_has_updated;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  isactive_has_updated = xtran_server_is_active_and_has_updated (thread_p);

  (void) or_pack_int (reply, isactive_has_updated);
  css_send_data_to_client (rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * stran_wait_server_active_trans -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
stran_wait_server_active_trans (THREAD_ENTRY * thread_p, unsigned int rid,
				char *request, int reqlen)
{
  int status;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  status = xtran_wait_server_active_trans (thread_p);

  (void) or_pack_int (reply, status);
  css_send_data_to_client (rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * stran_is_blocked -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
stran_is_blocked (THREAD_ENTRY * thread_p, unsigned int rid, char *request,
		  int reqlen)
{
  int tran_index;
  bool blocked;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  (void) or_unpack_int (request, &tran_index);

  blocked = xtran_is_blocked (thread_p, tran_index);

  (void) or_pack_int (reply, blocked ? 1 : 0);
  css_send_data_to_client (rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * stran_server_set_global_tran_info -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
stran_server_set_global_tran_info (THREAD_ENTRY * thread_p, unsigned int rid,
				   char *request, int reqlen)
{
  int success = NO_ERROR;
  int gtrid;
  void *info;
  int size;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  (void) or_unpack_int (request, &gtrid);

  if (css_receive_data_from_client (rid, (char **) &info, (int *) &size))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_NET_SERVER_DATA_RECEIVE,
	      0);
      css_send_abort_to_client (rid);
      return;
    }

  if (xtran_server_set_global_tran_info (thread_p, gtrid, info, size) !=
      NO_ERROR)
    {
      success = ER_FAILED;
      return_error_to_client (rid);
    }

  (void) or_pack_int (reply, success);
  css_send_data_to_client (rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));

  if (info != NULL)
    {
      free_and_init (info);
    }
}

/*
 * stran_server_get_global_tran_info -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
stran_server_get_global_tran_info (THREAD_ENTRY * thread_p, unsigned int rid,
				   char *request, int reqlen)
{
  int success = NO_ERROR;
  int gtrid;
  void *buffer;
  int size;
  OR_ALIGNED_BUF (OR_INT_SIZE * 2) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply), *ptr;

  ptr = or_unpack_int (request, &gtrid);
  ptr = or_unpack_int (ptr, &size);

  buffer = malloc (size);
  if (buffer == NULL)
    {
      css_send_abort_to_client (rid);
      return;
    }

  if (xtran_server_get_global_tran_info (thread_p, gtrid, buffer, size) !=
      NO_ERROR)
    {
      success = ER_FAILED;
      return_error_to_client (rid);
      size = 0;
    }

  ptr = or_pack_int (reply, size);
  ptr = or_pack_int (ptr, success);
  css_send_reply_and_data_to_client (rid, reply,
				     OR_ALIGNED_BUF_SIZE (a_reply),
				     (char *) buffer, size);
  free_and_init (buffer);
}

/*
 * stran_server_2pc_start -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
stran_server_2pc_start (THREAD_ENTRY * thread_p, unsigned int rid,
			char *request, int reqlen)
{
  int gtrid = NULL_TRANID;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  gtrid = xtran_server_2pc_start (thread_p);
  if (gtrid < 0)
    {
      return_error_to_client (rid);
    }

  (void) or_pack_int (reply, gtrid);
  css_send_data_to_client (rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * stran_server_2pc_prepare -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
stran_server_2pc_prepare (THREAD_ENTRY * thread_p, unsigned int rid,
			  char *request, int reqlen)
{
  TRAN_STATE state = TRAN_UNACTIVE_UNKNOWN;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  state = xtran_server_2pc_prepare (thread_p);
  if (state != TRAN_UNACTIVE_2PC_PREPARE && state != TRAN_UNACTIVE_COMMITTED)
    {
      /* the prepare failed. */
      return_error_to_client (rid);
    }

  (void) or_pack_int (reply, state);
  css_send_data_to_client (rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * stran_server_2pc_recovery_prepared -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
stran_server_2pc_recovery_prepared (THREAD_ENTRY * thread_p, unsigned int rid,
				    char *request, int reqlen)
{
  int count, *gtrids, size, i;
  int reply_size;
  char *reply, *ptr;

  (void) or_unpack_int (request, &size);

  gtrids = (int *) malloc (sizeof (int) * size);
  if (gtrids == NULL)
    {
      css_send_abort_to_client (rid);
      return;
    }

  count = xtran_server_2pc_recovery_prepared (thread_p, gtrids, size);
  if (count < 0 || count > size)
    {
      return_error_to_client (rid);
    }

  reply_size = OR_INT_SIZE + (OR_INT_SIZE * size);
  reply = (char *) malloc (reply_size);
  if (reply == NULL)
    {
      css_send_abort_to_client (rid);
      free_and_init (gtrids);
      return;
    }
  (void) memset (reply, 0, reply_size);
  ptr = or_pack_int (reply, count);
  for (i = 0; i < count; i++)
    {
      ptr = or_pack_int (ptr, gtrids[i]);
    }
  css_send_data_to_client (rid, reply, reply_size);

  free_and_init (gtrids);
  free_and_init (reply);
}

/*
 * stran_server_2pc_attach_global_tran -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
stran_server_2pc_attach_global_tran (THREAD_ENTRY * thread_p,
				     unsigned int rid, char *request,
				     int reqlen)
{
  int gtrid;
  int tran_index;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  (void) or_unpack_int (request, &gtrid);

  tran_index = xtran_server_2pc_attach_global_tran (thread_p, gtrid);
  if (tran_index == NULL_TRAN_INDEX)
    {
      return_error_to_client (rid);
    }

  (void) or_pack_int (reply, tran_index);
  css_send_data_to_client (rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * stran_server_2pc_prepare_global_tran -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
stran_server_2pc_prepare_global_tran (THREAD_ENTRY * thread_p,
				      unsigned int rid, char *request,
				      int reqlen)
{
  TRAN_STATE state;
  int gtrid;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  (void) or_unpack_int (request, &gtrid);

  state = xtran_server_2pc_prepare_global_tran (thread_p, gtrid);
  if (state != TRAN_UNACTIVE_2PC_PREPARE && state != TRAN_UNACTIVE_COMMITTED)
    {
      /* Likely the prepare failed.. somehow */
      return_error_to_client (rid);
    }

  (void) or_pack_int (reply, state);
  css_send_data_to_client (rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * sboot_initialize_server -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
sboot_initialize_server (THREAD_ENTRY * thread_p, unsigned int rid,
			 char *request, int reqlen)
{
  int xint;
  int print_version;
  char *db_name, *db_path, *vol_path, *log_path, *db_server_host;
  int db_overwrite;
  char *db_comments;
  DKNPAGES db_npages;
  int db_desired_pagesize;
  DKNPAGES log_npages;
  OID rootclass_oid;
  HFID rootclass_hfid;
  char *file_addmore_vols;
  char *client_prog_name;
  char *client_user_name;
  char *client_host_name;
  int client_pid;
  int client_lock_wait;
  TRAN_ISOLATION client_isolation;
  int tran_index;
  char *ptr;
  OR_ALIGNED_BUF (OR_INT_SIZE + OR_OID_SIZE + OR_HFID_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  ptr = or_unpack_int (request, &print_version);
  ptr = or_unpack_int (ptr, &db_overwrite);
  ptr = or_unpack_int (ptr, &db_desired_pagesize);
  ptr = or_unpack_string_nocopy (ptr, &db_name);
  ptr = or_unpack_string_nocopy (ptr, &db_path);
  ptr = or_unpack_string_nocopy (ptr, &vol_path);
  ptr = or_unpack_string_nocopy (ptr, &db_comments);
  ptr = or_unpack_int (ptr, &db_npages);
  ptr = or_unpack_string_nocopy (ptr, &file_addmore_vols);
  ptr = or_unpack_string_nocopy (ptr, &db_server_host);
  ptr = or_unpack_string_nocopy (ptr, &log_path);
  ptr = or_unpack_int (ptr, &log_npages);
  ptr = or_unpack_string_nocopy (ptr, &client_prog_name);
  ptr = or_unpack_string_nocopy (ptr, &client_user_name);
  ptr = or_unpack_string_nocopy (ptr, &client_host_name);
  ptr = or_unpack_int (ptr, &client_pid);
  ptr = or_unpack_int (ptr, &client_lock_wait);
  ptr = or_unpack_int (ptr, &xint);
  client_isolation = (TRAN_ISOLATION) xint;

  tran_index = xboot_initialize_server (thread_p, print_version, db_overwrite,
					db_desired_pagesize, db_name,
					db_path, vol_path, db_comments,
					(DKNPAGES) db_npages,
					file_addmore_vols,
					db_server_host, log_path,
					log_npages, &rootclass_oid,
					&rootclass_hfid,
					client_prog_name,
					client_user_name,
					client_host_name, client_pid,
					client_lock_wait, client_isolation);
  if (tran_index == NULL_TRAN_INDEX)
    {
      return_error_to_client (rid);
    }

  ptr = or_pack_int (reply, tran_index);
  ptr = or_pack_oid (ptr, &rootclass_oid);
  ptr = or_pack_hfid (ptr, &rootclass_hfid);
  css_send_data_to_client (rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * sboot_register_client -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
sboot_register_client (THREAD_ENTRY * thread_p, unsigned int rid,
		       char *request, int reqlen)
{
  int xint;
  int tran_index;
  int print_version;
  char *db_name;
  char *client_prog_name;
  char *client_user_name;
  char *client_host_name;
  int client_pid;
  int client_lock_wait;
  TRAN_ISOLATION client_isolation;
  OID oid;
  HFID hfid;
  TRAN_STATE transtate;
  PGLENGTH current_pagesize;
  char *ptr;
  struct timeval server_clock;

  OR_ALIGNED_BUF (OR_INT_SIZE + OR_OID_SIZE + OR_HFID_SIZE + OR_INT_SIZE +
		  OR_INT_SIZE + (OR_INT_SIZE * 2) + OR_FLOAT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  ptr = or_unpack_int (request, &print_version);
  ptr = or_unpack_string_nocopy (ptr, &db_name);
  ptr = or_unpack_string_nocopy (ptr, &client_prog_name);
  ptr = or_unpack_string_nocopy (ptr, &client_user_name);
  ptr = or_unpack_string_nocopy (ptr, &client_host_name);
  ptr = or_unpack_int (ptr, &client_pid);
  ptr = or_unpack_int (ptr, &client_lock_wait);
  ptr = or_unpack_int (ptr, &xint);
  client_isolation = (TRAN_ISOLATION) xint;

#if defined(DIAG_DEVEL) && defined(SERVER_MODE)
  SET_DIAG_VALUE (diag_executediag, DIAG_OBJ_TYPE_CONN_CONN_REQ, 1,
		  DIAG_VAL_SETTYPE_INC, NULL);
#endif
  tran_index = xboot_register_client (thread_p, print_version, db_name,
				      &oid, &hfid, client_prog_name,
				      client_user_name, client_host_name,
				      client_pid, client_lock_wait,
				      client_isolation, &transtate,
				      &current_pagesize);
  if (tran_index == NULL_TRAN_INDEX)
    {
#if defined(DIAG_DEVEL) && defined(SERVER_MODE)
      SET_DIAG_VALUE (diag_executediag, DIAG_OBJ_TYPE_CONN_CONN_REJECT, 1,
		      DIAG_VAL_SETTYPE_INC, NULL);
#endif
      return_error_to_client (rid);
    }

  gettimeofday (&server_clock, NULL);

  ptr = or_pack_int (reply, tran_index);
  ptr = or_pack_oid (ptr, &oid);
  ptr = or_pack_hfid (ptr, &hfid);
  ptr = or_pack_int (ptr, (int) transtate);
  ptr = or_pack_int (ptr, (int) current_pagesize);
  ptr = or_pack_int (ptr, server_clock.tv_sec);
  ptr = or_pack_int (ptr, server_clock.tv_usec);
  ptr = or_pack_float (ptr, rel_disk_compatible ());
  css_send_data_to_client (rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * sboot_notify_unregister_client -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
sboot_notify_unregister_client (THREAD_ENTRY * thread_p, unsigned int rid,
				char *request, int reqlen)
{
  int tran_index;
  int success = NO_ERROR;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  (void) or_unpack_int (request, &tran_index);

  xboot_notify_unregister_client (thread_p, tran_index);

  (void) or_pack_int (reply, success);
  css_send_data_to_client (rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * sboot_backup -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
sboot_backup (THREAD_ENTRY * thread_p, unsigned int rid, char *request,
	      int reqlen)
{
  int success;
  OR_ALIGNED_BUF (OR_INT_SIZE * 3) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  char *ptr;
  char *backup_path;
  FILEIO_BACKUP_LEVEL backup_level;
  int delete_unneeded_logarchives;
  char *backup_verbose_file;
  int num_threads;
  FILEIO_ZIP_METHOD zip_method;
  FILEIO_ZIP_LEVEL zip_level;
  int skip_activelog;
  PAGEID safe_pageid;

  ptr = or_unpack_string_nocopy (request, &backup_path);
  ptr = or_unpack_int (ptr, (int *) &backup_level);
  ptr = or_unpack_int (ptr, &delete_unneeded_logarchives);
  ptr = or_unpack_string_nocopy (ptr, &backup_verbose_file);
  ptr = or_unpack_int (ptr, &num_threads);
  ptr = or_unpack_int (ptr, (int *) &zip_method);
  ptr = or_unpack_int (ptr, (int *) &zip_level);
  ptr = or_unpack_int (ptr, (int *) &skip_activelog);
  ptr = or_unpack_int (ptr, (int *) &safe_pageid);

  success = xboot_backup (thread_p, backup_path, backup_level,
			  delete_unneeded_logarchives, backup_verbose_file,
			  num_threads, zip_method, zip_level, skip_activelog,
			  safe_pageid);

  if (success != NO_ERROR)
    {
      /* ER_LK_UNILATERALLY_ABORTED may not occur,
       * but if it occurs, rollback transaction
       */
      if (er_errid () == ER_LK_UNILATERALLY_ABORTED)
	{
	  tran_server_unilaterally_abort_tran (thread_p);
	}
      return_error_to_client (rid);
    }

  /*
   * To indicate results we really only need 2 ints, but the remote
   * bo and callback routine was expecting to receive 3 ints.
   */
  ptr = or_pack_int (reply, (int) END_CALLBACK);
  ptr = or_pack_int (ptr, success);
  ptr = or_pack_int (ptr, 0xEEABCDFFL);	/* padding, not used */

  css_send_data_to_client (rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * sboot_add_volume_extension -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
sboot_add_volume_extension (THREAD_ENTRY * thread_p, unsigned int rid,
			    char *request, int reqlen)
{
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  char *ptr;
  char *ext_path;
  char *ext_name;
  char *ext_comments;
  int ext_npages;
  int ext_purpose;
  int ext_overwrite;
  VOLID volid;

  ptr = or_unpack_string_nocopy (request, &ext_path);
  ptr = or_unpack_string_nocopy (ptr, &ext_name);
  ptr = or_unpack_string_nocopy (ptr, &ext_comments);
  ptr = or_unpack_int (ptr, &ext_npages);
  ptr = or_unpack_int (ptr, &ext_purpose);
  ptr = or_unpack_int (ptr, &ext_overwrite);

  volid = xboot_add_volume_extension (thread_p, ext_path, ext_name,
				      ext_comments,
				      (DKNPAGES) ext_npages,
				      (DISK_VOLPURPOSE) ext_purpose,
				      ext_overwrite);

  if (volid == NULL_VOLID)
    {
      /* ER_LK_UNILATERALLY_ABORTED may not occur,
       * but if it occurs, rollback transaction
       */
      if (er_errid () == ER_LK_UNILATERALLY_ABORTED)
	{
	  tran_server_unilaterally_abort_tran (thread_p);
	}
      return_error_to_client (rid);
    }

  (void) or_pack_int (reply, (int) volid);
  css_send_data_to_client (rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * sboot_check_db_consistency -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
sboot_check_db_consistency (THREAD_ENTRY * thread_p, unsigned int rid,
			    char *request, int reqlen)
{
  int success;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  int check_flag;

  if (request == NULL)
    {
      check_flag = CHECKDB_ALL_CHECK;
    }
  else
    {
      or_unpack_int (request, &check_flag);
    }

  success = ((xboot_check_db_consistency (thread_p, check_flag)
	      == NO_ERROR) ? NO_ERROR : ER_FAILED);

  if (success != NO_ERROR)
    {
      return_error_to_client (rid);
    }

  (void) or_pack_int (reply, success);
  css_send_data_to_client (rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * sboot_find_number_permanent_volumes -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
sboot_find_number_permanent_volumes (THREAD_ENTRY * thread_p,
				     unsigned int rid, char *request,
				     int reqlen)
{
  int nvols;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  nvols = xboot_find_number_permanent_volumes (thread_p);

  (void) or_pack_int (reply, nvols);
  css_send_data_to_client (rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * sboot_find_number_temp_volumes -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
sboot_find_number_temp_volumes (THREAD_ENTRY * thread_p, unsigned int rid,
				char *request, int reqlen)
{
  int nvols;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  nvols = xboot_find_number_temp_volumes (thread_p);

  (void) or_pack_int (reply, nvols);
  css_send_data_to_client (rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * sboot_find_last_temp -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
sboot_find_last_temp (THREAD_ENTRY * thread_p, unsigned int rid,
		      char *request, int reqlen)
{
  int nvols;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  nvols = xboot_find_last_temp (thread_p);

  (void) or_pack_int (reply, nvols);
  css_send_data_to_client (rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * slargeobjmgr_create () -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
slargeobjmgr_create (THREAD_ENTRY * thread_p, unsigned int rid, char *request,
		     int reqlen)
{
  LOID loid;
  int datasize, length, est_length, csserror;
  char *buffer, *ptr;
  OID oid;
  OR_ALIGNED_BUF (OR_LOID_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  ptr = or_unpack_loid (request, &loid);
  ptr = or_unpack_int (ptr, &datasize);
  ptr = or_unpack_int (ptr, &est_length);
  ptr = or_unpack_oid (ptr, &oid);

  csserror = 0;
  buffer = NULL;
  if (datasize)
    {
      csserror = css_receive_data_from_client (rid, &buffer, (int *) &length);
    }

  if (csserror)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_NET_SERVER_DATA_RECEIVE,
	      0);
      css_send_abort_to_client (rid);
    }
  else
    {
      /* make sure length matches datasize ? */
      if (xlargeobjmgr_create (thread_p, &loid, datasize, buffer,
			       est_length, &oid) == NULL)
	{
	  /* ER_LK_UNILATERALLY_ABORTED may not occur,
	   * but if it occurs, rollback transaction
	   */
	  if (er_errid () == ER_LK_UNILATERALLY_ABORTED)
	    {
	      tran_server_unilaterally_abort_tran (thread_p);
	    }
	  return_error_to_client (rid);
	}

      ptr = or_pack_loid (reply, &loid);
      css_send_data_to_client (rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
    }

  if (buffer != NULL)
    {
      free_and_init (buffer);
    }
}

/*
 * slargeobjmgr_destroy () -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
slargeobjmgr_destroy (THREAD_ENTRY * thread_p, unsigned int rid,
		      char *request, int reqlen)
{
  LOID loid;
  int success = NO_ERROR;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  (void) or_unpack_loid (request, &loid);

  if (xlargeobjmgr_destroy (thread_p, &loid) != NO_ERROR)
    {
      /* ER_LK_UNILATERALLY_ABORTED may not occur,
       * but if it occurs, rollback transaction
       */
      if (er_errid () == ER_LK_UNILATERALLY_ABORTED)
	{
	  tran_server_unilaterally_abort_tran (thread_p);
	}
      return_error_to_client (rid);
    }

  (void) or_pack_int (reply, success);
  css_send_data_to_client (rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * slargeobjmgr_read () -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
slargeobjmgr_read (THREAD_ENTRY * thread_p, unsigned int rid, char *request,
		   int reqlen)
{
  LOID loid;
  int offset, length, rlength;
  char *buffer, *ptr;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  ptr = or_unpack_loid (request, &loid);
  ptr = or_unpack_int (ptr, &offset);
  ptr = or_unpack_int (ptr, &length);

  buffer = (char *) db_private_alloc (thread_p, length);
  if (buffer == NULL)
    {
      css_send_abort_to_client (rid);
    }
  else
    {
      rlength = xlargeobjmgr_read (thread_p, &loid, offset, length, buffer);
      if (rlength < 0)
	{
	  /* ER_LK_UNILATERALLY_ABORTED may not occur,
	   * but if it occurs, rollback transaction
	   */
	  if (er_errid () == ER_LK_UNILATERALLY_ABORTED)
	    {
	      tran_server_unilaterally_abort_tran (thread_p);
	    }
	  return_error_to_client (rid);
	}

      ptr = or_pack_int (reply, rlength);
      css_send_reply_and_data_to_client (rid, reply,
					 OR_ALIGNED_BUF_SIZE (a_reply),
					 buffer, rlength);
      db_private_free_and_init (thread_p, buffer);
    }
}

/*
 * slargeobjmgr_write () -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
slargeobjmgr_write (THREAD_ENTRY * thread_p, unsigned int rid, char *request,
		    int reqlen)
{
  LOID loid;
  int datasize, offset;
  int csserror, length, rlength;
  char *buffer, *ptr;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  ptr = or_unpack_int (request, &datasize);
  ptr = or_unpack_loid (ptr, &loid);
  ptr = or_unpack_int (ptr, &offset);

  csserror = 0;
  buffer = NULL;
  if (datasize)
    {
      csserror = css_receive_data_from_client (rid, &buffer, (int *) &length);
    }

  if (csserror)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_NET_SERVER_DATA_RECEIVE,
	      0);
      css_send_abort_to_client (rid);
    }
  else
    {
      rlength = xlargeobjmgr_write (thread_p, &loid, offset, length, buffer);
      if (rlength < 0)
	{
	  /* ER_LK_UNILATERALLY_ABORTED may not occur,
	   * but if it occurs, rollback transaction
	   */
	  if (er_errid () == ER_LK_UNILATERALLY_ABORTED)
	    {
	      tran_server_unilaterally_abort_tran (thread_p);
	    }
	  return_error_to_client (rid);
	}

      ptr = or_pack_int (reply, rlength);
      css_send_data_to_client (rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
    }
  if (buffer != NULL)
    {
      free_and_init (buffer);
    }
}

/*
 * slargeobjmgr_insert () -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
slargeobjmgr_insert (THREAD_ENTRY * thread_p, unsigned int rid, char *request,
		     int reqlen)
{
  LOID loid;
  int datasize, offset;
  int csserror, length, rlength;
  char *ptr, *buffer;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  ptr = or_unpack_int (request, &datasize);
  ptr = or_unpack_loid (ptr, &loid);
  ptr = or_unpack_int (ptr, &offset);

  csserror = 0;
  buffer = NULL;
  if (datasize)
    {
      csserror = css_receive_data_from_client (rid, &buffer, (int *) &length);
    }

  if (csserror)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_NET_SERVER_DATA_RECEIVE,
	      0);
      css_send_abort_to_client (rid);
    }
  else
    {
      rlength = xlargeobjmgr_insert (thread_p, &loid, offset, length, buffer);
      if (rlength < 0)
	{
	  /* ER_LK_UNILATERALLY_ABORTED may not occur,
	   * but if it occurs, rollback transaction
	   */
	  if (er_errid () == ER_LK_UNILATERALLY_ABORTED)
	    {
	      tran_server_unilaterally_abort_tran (thread_p);
	    }
	  return_error_to_client (rid);
	}

      ptr = or_pack_int (reply, rlength);
      css_send_data_to_client (rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
    }
  if (buffer != NULL)
    {
      free_and_init (buffer);
    }
}

/*
 * slargeobjmgr_delete () -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
slargeobjmgr_delete (THREAD_ENTRY * thread_p, unsigned int rid, char *request,
		     int reqlen)
{
  LOID loid;
  int offset, length, rlength;
  char *ptr;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  ptr = or_unpack_loid (request, &loid);
  ptr = or_unpack_int (ptr, &offset);
  ptr = or_unpack_int (ptr, &length);

  rlength = xlargeobjmgr_delete (thread_p, &loid, offset, length);
  if (rlength < 0)
    {
      /* ER_LK_UNILATERALLY_ABORTED may not occur,
       * but if it occurs, rollback transaction
       */
      if (er_errid () == ER_LK_UNILATERALLY_ABORTED)
	{
	  tran_server_unilaterally_abort_tran (thread_p);
	}
      return_error_to_client (rid);
    }

  ptr = or_pack_int (reply, rlength);
  css_send_data_to_client (rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * slargeobjmgr_append () -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
slargeobjmgr_append (THREAD_ENTRY * thread_p, unsigned int rid, char *request,
		     int reqlen)
{
  LOID loid;
  int datasize;
  int csserror, length, rlength;
  char *ptr, *buffer;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  ptr = or_unpack_int (request, &datasize);
  ptr = or_unpack_loid (ptr, &loid);

  csserror = 0;
  buffer = NULL;
  if (datasize)
    {
      csserror = css_receive_data_from_client (rid, &buffer, (int *) &length);
    }

  if (csserror)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_NET_SERVER_DATA_RECEIVE,
	      0);
      css_send_abort_to_client (rid);
    }
  else
    {
      rlength = xlargeobjmgr_append (thread_p, &loid, length, buffer);
      if (rlength < 0)
	{
	  /* ER_LK_UNILATERALLY_ABORTED may not occur,
	   * but if it occurs, rollback transaction
	   */
	  if (er_errid () == ER_LK_UNILATERALLY_ABORTED)
	    {
	      tran_server_unilaterally_abort_tran (thread_p);
	    }
	  return_error_to_client (rid);
	}

      ptr = or_pack_int (reply, rlength);
      css_send_data_to_client (rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
    }
  if (buffer != NULL)
    {
      free_and_init (buffer);
    }
}

/*
 * slargeobjmgr_truncate () -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
slargeobjmgr_truncate (THREAD_ENTRY * thread_p, unsigned int rid,
		       char *request, int reqlen)
{
  LOID loid;
  int offset, rlength;
  char *ptr;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  ptr = or_unpack_loid (request, &loid);
  ptr = or_unpack_int (ptr, &offset);

  rlength = xlargeobjmgr_truncate (thread_p, &loid, offset);
  if (rlength < 0)
    {
      /* ER_LK_UNILATERALLY_ABORTED may not occur,
       * but if it occurs, rollback transaction
       */
      if (er_errid () == ER_LK_UNILATERALLY_ABORTED)
	{
	  tran_server_unilaterally_abort_tran (thread_p);
	}
      return_error_to_client (rid);
    }

  ptr = or_pack_int (reply, rlength);
  css_send_data_to_client (rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * slargeobjmgr_compress () -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
slargeobjmgr_compress (THREAD_ENTRY * thread_p, unsigned int rid,
		       char *request, int reqlen)
{
  LOID loid;
  int success = NO_ERROR;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  (void) or_unpack_loid (request, &loid);

  if (xlargeobjmgr_compress (thread_p, &loid) != NO_ERROR)
    {
      /* ER_LK_UNILATERALLY_ABORTED may not occur,
       * but if it occurs, rollback transaction
       */
      if (er_errid () == ER_LK_UNILATERALLY_ABORTED)
	{
	  tran_server_unilaterally_abort_tran (thread_p);
	}
      return_error_to_client (rid);
    }

  (void) or_pack_int (reply, success);
  css_send_data_to_client (rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * slargeobjmgr_length () -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
slargeobjmgr_length (THREAD_ENTRY * thread_p, unsigned int rid, char *request,
		     int reqlen)
{
  LOID loid;
  int length;
  char *ptr;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  (void) or_unpack_loid (request, &loid);

  length = xlargeobjmgr_length (thread_p, &loid);
  if (length < 0)
    {
      /* ER_LK_UNILATERALLY_ABORTED may not occur,
       * but if it occurs, rollback transaction
       */
      if (er_errid () == ER_LK_UNILATERALLY_ABORTED)
	{
	  tran_server_unilaterally_abort_tran (thread_p);
	}
      return_error_to_client (rid);
    }

  ptr = or_pack_int (reply, length);
  css_send_data_to_client (rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * sqst_update_class_statistics -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
sqst_update_class_statistics (THREAD_ENTRY * thread_p, unsigned int rid,
			      char *request, int reqlen)
{
  int error;
  OID classoid;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  (void) or_unpack_oid (request, &classoid);

  error = xstats_update_class_statistics (thread_p, &classoid);
  if (error != NO_ERROR)
    {
      /* ER_LK_UNILATERALLY_ABORTED may not occur,
       * but if it occurs, rollback transaction
       */
      if (er_errid () == ER_LK_UNILATERALLY_ABORTED)
	{
	  tran_server_unilaterally_abort_tran (thread_p);
	}
      return_error_to_client (rid);
    }

  (void) or_pack_errcode (reply, error);
  css_send_data_to_client (rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * sqst_update_statistics -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
sqst_update_statistics (THREAD_ENTRY * thread_p, unsigned int rid,
			char *request, int reqlen)
{
  int error;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  error = xstats_update_statistics (thread_p);
  if (error != NO_ERROR)
    {
      /* ER_LK_UNILATERALLY_ABORTED may not occur,
       * but if it occurs, rollback transaction
       */
      if (er_errid () == ER_LK_UNILATERALLY_ABORTED)
	{
	  tran_server_unilaterally_abort_tran (thread_p);
	}
      return_error_to_client (rid);
    }

  (void) or_pack_errcode (reply, error);
  css_send_data_to_client (rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * sbtree_add_index -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
sbtree_add_index (THREAD_ENTRY * thread_p, unsigned int rid, char *request,
		  int reqlen)
{
  int error = NO_ERROR;
  BTID btid;
  BTID *return_btid;
  TP_DOMAIN *key_type;
  OID class_oid;
  int attr_id, unique_btree;
  int reverse_btree;
  char *ptr;
  OR_ALIGNED_BUF (OR_INT_SIZE + OR_BTID_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  ptr = or_unpack_btid (request, &btid);
  ptr = PTR_ALIGN (ptr, OR_INT_SIZE);
  ptr = or_unpack_domain (ptr, &key_type, 0);
  ptr = or_unpack_oid (ptr, &class_oid);
  ptr = or_unpack_int (ptr, &attr_id);
  ptr = or_unpack_int (ptr, &unique_btree);
  ptr = or_unpack_int (ptr, &reverse_btree);

  return_btid = xbtree_add_index (thread_p, &btid, key_type, &class_oid,
				  attr_id, unique_btree, reverse_btree, 0, 0,
				  0);
  if (return_btid == NULL)
    {
      /* ER_LK_UNILATERALLY_ABORTED may not occur,
       * but if it occurs, rollback transaction
       */
      if (er_errid () == ER_LK_UNILATERALLY_ABORTED)
	{
	  tran_server_unilaterally_abort_tran (thread_p);
	}
      return_error_to_client (rid);
      error = er_errid ();
    }

  ptr = or_pack_int (reply, error);
  ptr = or_pack_btid (ptr, &btid);
  css_send_data_to_client (rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));

}

/*
 * sbtree_load_index -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
sbtree_load_index (THREAD_ENTRY * thread_p, unsigned int rid, char *request,
		   int reqlen)
{
  int error;
  BTID btid;
  BTID *return_btid;
  OID *class_oids;
  HFID *hfids;
  int unique_flag;
  int reverse_flag;
  OID fk_refcls_oid;
  BTID fk_refcls_pk_btid;
  int cache_attr_id;
  char *fk_name;
  int n_classes, n_attrs, *attr_ids;
  TP_DOMAIN *key_type;
  char *ptr;
  OR_ALIGNED_BUF (OR_INT_SIZE + OR_BTID_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  ptr = or_unpack_btid (request, &btid);
  ptr = PTR_ALIGN (ptr, OR_INT_SIZE);
  ptr = or_unpack_domain (ptr, &key_type, 0);

  ptr = or_unpack_int (ptr, &n_classes);
  ptr = or_unpack_int (ptr, &n_attrs);

  ptr = or_unpack_oid_array (ptr, n_classes, &class_oids);
  ptr = or_unpack_int_array (ptr, (n_classes * n_attrs), &attr_ids);
  ptr = or_unpack_hfid_array (ptr, n_classes, &hfids);

  ptr = or_unpack_int (ptr, &unique_flag);
  ptr = or_unpack_int (ptr, &reverse_flag);

  ptr = or_unpack_oid (ptr, &fk_refcls_oid);
  ptr = or_unpack_btid (ptr, &fk_refcls_pk_btid);
  ptr = PTR_ALIGN (ptr, OR_INT_SIZE);
  ptr = or_unpack_int (ptr, &cache_attr_id);
  ptr = or_unpack_string_nocopy (ptr, &fk_name);

  error = 0;
  return_btid = xbtree_load_index (thread_p, &btid, key_type, class_oids,
				   n_classes, n_attrs,
				   attr_ids, hfids, unique_flag, reverse_flag,
				   &fk_refcls_oid, &fk_refcls_pk_btid,
				   cache_attr_id, fk_name);
  if (return_btid == NULL)
    {
      /* ER_LK_UNILATERALLY_ABORTED may not occur,
       * but if it occurs, rollback transaction
       */
      if (er_errid () == ER_LK_UNILATERALLY_ABORTED)
	{
	  tran_server_unilaterally_abort_tran (thread_p);
	}
      return_error_to_client (rid);
      error = er_errid ();
    }

  ptr = or_pack_int (reply, error);
  ptr = or_pack_btid (ptr, &btid);
  css_send_data_to_client (rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));

  db_private_free_and_init (thread_p, class_oids);
  db_private_free_and_init (thread_p, attr_ids);
  db_private_free_and_init (thread_p, hfids);
}

/*
 * sbtree_delete_index -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
sbtree_delete_index (THREAD_ENTRY * thread_p, unsigned int rid, char *request,
		     int reqlen)
{
  BTID btid;
  int success;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  (void) or_unpack_btid (request, &btid);

  success = ((xbtree_delete_index (thread_p, &btid) == NO_ERROR)
	     ? NO_ERROR : ER_FAILED);
  if (success != NO_ERROR)
    {
      /* ER_LK_UNILATERALLY_ABORTED may not occur,
       * but if it occurs, rollback transaction
       */
      if (er_errid () == ER_LK_UNILATERALLY_ABORTED)
	{
	  tran_server_unilaterally_abort_tran (thread_p);
	}
      return_error_to_client (rid);
    }

  (void) or_pack_int (reply, (int) success);
  css_send_data_to_client (rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * slocator_remove_class_from_index -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
slocator_remove_class_from_index (THREAD_ENTRY * thread_p, unsigned int rid,
				  char *request, int reqlen)
{
  OID oid;
  BTID btid;
  HFID hfid;
  int success;
  char *ptr;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  ptr = or_unpack_oid (request, &oid);
  ptr = or_unpack_btid (ptr, &btid);
  ptr = PTR_ALIGN (ptr, OR_INT_SIZE);
  ptr = or_unpack_hfid (ptr, &hfid);

  success = ((xlocator_remove_class_from_index (thread_p, &oid, &btid, &hfid)
	      == NO_ERROR) ? NO_ERROR : ER_FAILED);
  if (success != NO_ERROR)
    {
      /* ER_LK_UNILATERALLY_ABORTED may not occur,
       * but if it occurs, rollback transaction
       */
      if (er_errid () == ER_LK_UNILATERALLY_ABORTED)
	{
	  tran_server_unilaterally_abort_tran (thread_p);
	}
      return_error_to_client (rid);
    }

  (void) or_pack_int (reply, (int) success);
  css_send_data_to_client (rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * sbtree_find_unique -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
sbtree_find_unique (THREAD_ENTRY * thread_p, unsigned int rid, char *request,
		    int reqlen)
{
  BTID btid;
  OID class_oid;
  OID oid;
  DB_VALUE key;
  char *ptr;
  int success;
  OR_ALIGNED_BUF (OR_OID_SIZE + OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  ptr = or_unpack_btid (request, &btid);
  ptr = PTR_ALIGN (ptr, OR_INT_SIZE);
  ptr = or_unpack_value (ptr, &key);
  ptr = or_unpack_oid (ptr, &class_oid);

  OID_SET_NULL (&oid);
  success = xbtree_find_unique (thread_p, &btid, &key, &class_oid, &oid,
				false);
  if (success == BTREE_ERROR_OCCURRED)
    {
      /* ER_LK_UNILATERALLY_ABORTED may not occur,
       * but if it occurs, rollback transaction
       */
      if (er_errid () == ER_LK_UNILATERALLY_ABORTED)
	{
	  tran_server_unilaterally_abort_tran (thread_p);
	}
      return_error_to_client (rid);
    }

  /* free storage if the key was a string */
  pr_clear_value (&key);

  ptr = or_pack_int (reply, success);
  ptr = or_pack_oid (ptr, &oid);
  css_send_data_to_client (rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * sbtree_class_test_unique -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
sbtree_class_test_unique (THREAD_ENTRY * thread_p, unsigned int rid,
			  char *request, int reqlen)
{
  int success;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  success = xbtree_class_test_unique (thread_p, request, reqlen);

  if (success != NO_ERROR)
    {
      return_error_to_client (rid);
    }

  (void) or_pack_int (reply, (int) success);
  css_send_data_to_client (rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * sdk_totalpgs -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
sdk_totalpgs (THREAD_ENTRY * thread_p, unsigned int rid, char *request,
	      int reqlen)
{
  DKNPAGES npages;
  VOLID volid;
  int int_volid;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  (void) or_unpack_int (request, &int_volid);
  volid = (VOLID) int_volid;

  npages = xdisk_get_total_numpages (thread_p, volid);
  if (npages < 0)
    {
      /* ER_LK_UNILATERALLY_ABORTED may not occur,
       * but if it occurs, rollback transaction
       */
      if (er_errid () == ER_LK_UNILATERALLY_ABORTED)
	{
	  tran_server_unilaterally_abort_tran (thread_p);
	}
      return_error_to_client (rid);
    }

  (void) or_pack_int (reply, npages);
  css_send_data_to_client (rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * sdk_freepgs -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
sdk_freepgs (THREAD_ENTRY * thread_p, unsigned int rid, char *request,
	     int reqlen)
{
  DKNPAGES npages;
  VOLID volid;
  int int_volid;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  (void) or_unpack_int (request, &int_volid);
  volid = (VOLID) int_volid;

  npages = xdisk_get_free_numpages (thread_p, volid);
  if (npages < 0)
    {
      /* ER_LK_UNILATERALLY_ABORTED may not occur,
       * but if it occurs, rollback transaction
       */
      if (er_errid () == ER_LK_UNILATERALLY_ABORTED)
	{
	  tran_server_unilaterally_abort_tran (thread_p);
	}
      return_error_to_client (rid);
    }

  (void) or_pack_int (reply, npages);
  css_send_data_to_client (rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * sdk_remarks -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
sdk_remarks (THREAD_ENTRY * thread_p, unsigned int rid, char *request,
	     int reqlen)
{
  int int_volid;
  char *remark;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  int area_length;
  char *area;

  (void) or_unpack_int (request, &int_volid);

  remark = xdisk_get_remarks (thread_p, (VOLID) int_volid);

  if (remark == NULL)
    {
      /* ER_LK_UNILATERALLY_ABORTED may not occur,
       * but if it occurs, rollback transaction
       */
      if (er_errid () == ER_LK_UNILATERALLY_ABORTED)
	{
	  tran_server_unilaterally_abort_tran (thread_p);
	}
      return_error_to_client (rid);
      area_length = 0;
      area = NULL;
    }
  else
    {
      area_length = or_packed_string_length (remark);
      area = (char *) db_private_alloc (thread_p, area_length);
      if (area == NULL)
	{
	  return_error_to_client (rid);
	  area_length = 0;
	}
      else
	{
	  (void) or_pack_string (area, remark);
	}
    }

  (void) or_pack_int (reply, area_length);
  css_send_reply_and_data_to_client (rid, reply,
				     OR_ALIGNED_BUF_SIZE (a_reply), area,
				     area_length);
  if (remark != NULL)
    {

      /* since this was copied to the client, we don't need it on the server */
      free_and_init (remark);
    }
  if (area)
    {
      db_private_free_and_init (thread_p, area);
    }
}

/*
 * sdk_purpose -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
sdk_purpose (THREAD_ENTRY * thread_p, unsigned int rid, char *request,
	     int reqlen)
{
  DISK_VOLPURPOSE vol_purpose;
  VOLID volid;
  int int_volid;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  (void) or_unpack_int (request, &int_volid);
  volid = (VOLID) int_volid;

  vol_purpose = xdisk_get_purpose (thread_p, volid);
  if (vol_purpose < DISK_UNKNOWN_PURPOSE)
    {
      /* ER_LK_UNILATERALLY_ABORTED may not occur,
       * but if it occurs, rollback transaction
       */
      if (er_errid () == ER_LK_UNILATERALLY_ABORTED)
	{
	  tran_server_unilaterally_abort_tran (thread_p);
	}
      return_error_to_client (rid);
    }

  (void) or_pack_int (reply, vol_purpose);
  css_send_data_to_client (rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * sdk_purpose_totalpgs_and_freepgs -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
sdk_purpose_totalpgs_and_freepgs (THREAD_ENTRY * thread_p, unsigned int rid,
				  char *request, int reqlen)
{
  int int_volid;
  VOLID volid;
  DISK_VOLPURPOSE vol_purpose;
  DKNPAGES vol_ntotal_pages;
  DKNPAGES vol_nfree_pages;
  char *ptr;
  OR_ALIGNED_BUF (OR_INT_SIZE * 4) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  (void) or_unpack_int (request, &int_volid);
  volid = (VOLID) int_volid;

  volid = xdisk_get_purpose_and_total_free_numpages (thread_p, volid,
						     &vol_purpose,
						     &vol_ntotal_pages,
						     &vol_nfree_pages);

  if (volid == NULL_VOLID)
    {
      /* ER_LK_UNILATERALLY_ABORTED may not occur,
       * but if it occurs, rollback transaction
       */
      if (er_errid () == ER_LK_UNILATERALLY_ABORTED)
	{
	  tran_server_unilaterally_abort_tran (thread_p);
	}
      return_error_to_client (rid);
    }

  ptr = or_pack_int (reply, vol_purpose);
  ptr = or_pack_int (ptr, vol_ntotal_pages);
  ptr = or_pack_int (ptr, vol_nfree_pages);
  ptr = or_pack_int (ptr, (int) volid);
  css_send_data_to_client (rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * sdk_vlabel -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
sdk_vlabel (THREAD_ENTRY * thread_p, unsigned int rid, char *request,
	    int reqlen)
{
  OR_ALIGNED_BUF (PATH_MAX) a_vol_fullname;
  char *vol_fullname = OR_ALIGNED_BUF_START (a_vol_fullname);
  int int_volid;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  int area_length;
  char *area;

  (void) or_unpack_int (request, &int_volid);

  if (xdisk_get_fullname (thread_p, (VOLID) int_volid, vol_fullname) == NULL)
    {
      /* ER_LK_UNILATERALLY_ABORTED may not occur,
       * but if it occurs, rollback transaction
       */
      if (er_errid () == ER_LK_UNILATERALLY_ABORTED)
	{
	  tran_server_unilaterally_abort_tran (thread_p);
	}
      return_error_to_client (rid);
      area_length = 0;
      area = NULL;
    }
  else
    {
      area_length = or_packed_string_length (vol_fullname);
      area = (char *) db_private_alloc (thread_p, area_length);
      if (area == NULL)
	{
	  return_error_to_client (rid);
	  area_length = 0;
	}
      else
	{
	  (void) or_pack_string (area, vol_fullname);
	}
    }

  (void) or_pack_int (reply, area_length);
  css_send_reply_and_data_to_client (rid, reply,
				     OR_ALIGNED_BUF_SIZE (a_reply), area,
				     area_length);
  if (area)
    {
      db_private_free_and_init (thread_p, area);
    }
}

/*
 * sqfile_get_list_file_page -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
sqfile_get_list_file_page (THREAD_ENTRY * thread_p, unsigned int rid,
			   char *request, int reqlen)
{
  int query_id;
  int volid, pageid;
  char *ptr;
  OR_ALIGNED_BUF (OR_INT_SIZE * 2) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  char page_buf[IO_MAX_PAGE_SIZE];
  int page_size;
  int error = NO_ERROR;

  ptr = or_unpack_int (request, &query_id);
  ptr = or_unpack_int (ptr, &volid);
  ptr = or_unpack_int (ptr, &pageid);

  if (volid == NULL_VOLID && pageid == NULL_PAGEID)
    {
      goto empty_page;
    }

  error = xqfile_get_list_file_page (thread_p, query_id, volid, pageid,
				     page_buf, &page_size);
  if (error != NO_ERROR)
    {
      if (error == ER_INTERRUPT && query_id >= 0
	  && qmgr_get_query_error_with_id (thread_p, query_id) == NO_ERROR)
	{
	  xqmgr_sync_query (thread_p, query_id, false, NULL, true);
	}

      return_error_to_client (rid);
      goto empty_page;
    }
  if (page_size == 0)
    {
      goto empty_page;
    }

  ptr = or_pack_int (reply, page_size);
  ptr = or_pack_int (ptr, error);
  css_send_reply_and_data_to_client (rid, reply,
				     OR_ALIGNED_BUF_SIZE (a_reply), page_buf,
				     page_size);
  return;

empty_page:
  /* setup empty list file page and return it */
  qmgr_setup_empty_list_file (page_buf);
  page_size = QFILE_PAGE_HEADER_SIZE;
  ptr = or_pack_int (reply, page_size);
  ptr = or_pack_int (ptr, error);
  css_send_reply_and_data_to_client (rid, reply,
				     OR_ALIGNED_BUF_SIZE (a_reply), page_buf,
				     page_size);
}

/*
 * sqmgr_prepare_query - Process a SERVER_QM_PREPARE request
 *
 * return:
 *
 *   thrd(in):
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 * Receive XASL stream and return XASL file id (QFILE_LIST_ID) as a result.
 * If xasl_buffer == NULL, the server will look up the XASL cache and then
 * return the cached XASL file id if found, otherwise return NULL QFILE_LIST_ID.
 * This function is a counter part to qmgr_prepare_query().
 */
void
sqmgr_prepare_query (THREAD_ENTRY * thrd, unsigned int rid,
		     char *request, int reqlen)
{
  XASL_ID xasl_id, *p;
  LOID loid;
  char *ptr, *query_str, *xasl_stream = NULL, *reply;
  int csserror, xasl_size;
  OID user_oid;
  OR_ALIGNED_BUF (OR_INT_SIZE + OR_LOID_SIZE) a_reply;

  reply = OR_ALIGNED_BUF_START (a_reply);

  /* unpack query string from the request data */
  ptr = or_unpack_string_nocopy (request, &query_str);
  /* unpack OID of the current user */
  ptr = or_unpack_oid (ptr, &user_oid);
  /* unpack size of XASL stream */
  ptr = or_unpack_int (ptr, &xasl_size);

  if (xasl_size > 0)
    {
      /* receive XASL stream from the client */
      csserror = css_receive_data_from_client (rid, &xasl_stream, &xasl_size);
      if (csserror)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_NET_SERVER_DATA_RECEIVE, 0);
	  css_send_abort_to_client (rid);
	  if (xasl_stream)
	    free_and_init (xasl_stream);
	  return;
	}
    }

  /* call the server routine of query prepare */
  XASL_ID_SET_NULL (&xasl_id);
  p = xqmgr_prepare_query (thrd, query_str, &user_oid, xasl_stream, xasl_size,
			   &xasl_id);
  if (xasl_stream && !p)
    {
      /* ER_LK_UNILATERALLY_ABORTED may not occur,
         but if it occurs, rollback transaction */
      if (er_errid () == ER_LK_UNILATERALLY_ABORTED)
	tran_server_unilaterally_abort_tran (thrd);
      return_error_to_client (rid);
    }
  if (xasl_stream)
    {
      free_and_init (xasl_stream);
    }
  /* pack XASL file id as a reply;
     borrow LOID structure only for transmission purpose */
  if (p)
    {
      (void) memcpy ((void *) &loid, (void *) p, sizeof (XASL_ID));
      ptr = or_pack_int (reply, NO_ERROR);
      ptr = or_pack_loid (ptr, &loid);
    }
  else
    {
      ptr = or_pack_int (reply, ER_FAILED);
    }

  /* send reply and data to the client */
  css_send_reply_and_data_to_client (rid,
				     reply, OR_ALIGNED_BUF_SIZE (a_reply),
				     NULL, 0);
}

/*
 * sqmgr_execute_query - Process a SERVER_QM_EXECUTE request
 *
 * return:
 *
 *   thrd(in):
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 * Receive XASL file id and parameter values if exist and return list file id
 * that contains query result. If an error occurs, return NULL QFILE_LIST_ID.
 * This function is a counter part to qmgr_execute_query().
 */
void
sqmgr_execute_query (THREAD_ENTRY * thrd, unsigned int rid,
		     char *request, int reqlen)
{
  XASL_ID xasl_id;
  QFILE_LIST_ID *list_id;
  int csserror, dbval_cnt, data_size, replydata_size, page_size,
    query_id = -1;
  char *ptr, *data = NULL, *reply, *replydata = NULL;
  PAGE_PTR page_ptr;
  char page_buf[IO_MAX_PAGE_SIZE];	/* page buffer */
  DB_VALUE *dbvals = NULL, *dbval;
  QUERY_FLAG query_flag;
  OR_ALIGNED_BUF (OR_INT_SIZE * 4 + OR_CACHE_TIME_SIZE) a_reply;
  int i;
  CACHE_TIME clt_cache_time;
  CACHE_TIME srv_cache_time;

  reply = OR_ALIGNED_BUF_START (a_reply);

  /* unpack XASL file id (XASL_ID), number of parameter values,
     size of the reecieved data, and query execution mode flag
     from the request data;
     borrow LOID structure only for transmission purpose */
  ptr = or_unpack_loid (request, (LOID *) & xasl_id);
  ptr = or_unpack_int (ptr, &dbval_cnt);
  ptr = or_unpack_int (ptr, &data_size);
  ptr = or_unpack_int (ptr, &query_flag);
  OR_UNPACK_CACHE_TIME (ptr, &clt_cache_time);

  if (dbval_cnt)
    {
      /* if the request contains parameter values for the query,
         allocate space for them */
      dbvals = (DB_VALUE *) db_private_alloc (thrd,
					      sizeof (DB_VALUE) * dbval_cnt);
      if (!dbvals)
	{
	  css_send_abort_to_client (rid);
	  return;
	}

      /* receive parameter values (DB_VALUE) from the client */
      csserror = css_receive_data_from_client (rid, &data, &data_size);
      if (csserror)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_NET_SERVER_DATA_RECEIVE, 0);
	  css_send_abort_to_client (rid);
	  if (data)
	    free_and_init (data);
	  db_private_free_and_init (thrd, dbvals);
	  return;
	}
      else
	{
	  /* unpack DB_VALUEs from the received data */
	  ptr = data;
	  for (i = 0, dbval = dbvals; i < dbval_cnt; i++, dbval++)
	    ptr = or_unpack_db_value (ptr, dbval);
	}
      if (data)
	{
	  free_and_init (data);
	}
    }

  CACHE_TIME_RESET (&srv_cache_time);

  /* call the server routine of query execute */
  list_id = xqmgr_execute_query (thrd, &xasl_id, &query_id,
				 dbval_cnt, dbvals, &query_flag,
				 &clt_cache_time, &srv_cache_time);
  if (!list_id && !CACHE_TIME_EQ (&clt_cache_time, &srv_cache_time))
    {
      /* ER_LK_UNILATERALLY_ABORTED may not occur,
         but if it occurs, rollback transaction */
      if (er_errid () == ER_LK_UNILATERALLY_ABORTED)
	{
	  tran_server_unilaterally_abort_tran (thrd);
	}
      return_error_to_client (rid);
    }

  /* clear and free space for DB_VALUEs after the query is executed
     In the case of async query, the space will be freed
     when the query is completed. See xs_execute_async_select() */
  if (IS_SYNC_EXEC_MODE (query_flag) && dbvals)
    {
      for (i = 0, dbval = dbvals; i < dbval_cnt; i++, dbval++)
	{
	  db_value_clear (dbval);
	}
      if (dbvals)
	{
	  db_private_free_and_init (thrd, dbvals);
	}
    }

  page_size = 0;
  page_ptr = NULL;
  if (list_id && IS_SYNC_EXEC_MODE (query_flag))
    {
      /* get the first page of the list file */
      page_ptr = (VPID_ISNULL (&(list_id->first_vpid))) ?
	NULL : qmgr_get_old_page (thrd, &(list_id->first_vpid),
				  list_id->tfile_vfid);
      if (page_ptr)
	{
	  /* calculate page size */
	  page_size = (((QFILE_GET_TUPLE_COUNT (page_ptr) == -2)
			|| (QFILE_GET_OVERFLOW_PAGE_ID (page_ptr) !=
			    NULL_PAGEID))
		       ? DB_PAGESIZE :
		       (QFILE_GET_LAST_TUPLE_OFFSET (page_ptr) +
			QFILE_GET_TUPLE_LENGTH (page_ptr +
						QFILE_GET_LAST_TUPLE_OFFSET
						(page_ptr))));
	  (void) memcpy (page_buf, page_ptr, page_size);
	  qmgr_free_old_page (thrd, page_ptr, list_id->tfile_vfid);
	  page_ptr = page_buf;
	}
      else
	{
	  /* for sync query, following error may not occur,
	     if it does, rollback transaction */
	  if (er_errid () == ER_LK_UNILATERALLY_ABORTED)
	    {
	      tran_server_unilaterally_abort_tran (thrd);
	    }
	  return_error_to_client (rid);
	}
    }

  /* pack 'QUERY_END' as a first argument of the reply */
  ptr = or_pack_int (reply, QUERY_END);
  /* pack size of list file id to return as a second argument of the reply */
  replydata_size = list_id ? or_listid_length (list_id) : 0;
  ptr = or_pack_int (ptr, replydata_size);
  if (replydata_size)
    {
      /* pack list file id as a reply data */
      replydata = (char *) db_private_alloc (thrd, replydata_size);
      if (replydata)
	{
	  (void) or_pack_listid (replydata, list_id);
	}
      else
	{
	  return_error_to_client (rid);
	}
    }
  /* pack size of a page to return as a third argumnet of the reply */
  ptr = or_pack_int (ptr, page_size);
  /* query id to return as a fourth argument of the reply */
  ptr = or_pack_int (ptr, query_id);
  /* result cache created time */
  OR_PACK_CACHE_TIME (ptr, &srv_cache_time);

  css_send_reply_and_2_data_to_client (rid,
				       reply, OR_ALIGNED_BUF_SIZE (a_reply),
				       replydata, replydata_size,
				       page_ptr, page_size);

  /* free QFILE_LIST_ID duplicated by xqmgr_execute_query() */
  if (replydata)
    {
      db_private_free_and_init (thrd, replydata);
    }
  if (list_id)
    {
      qfile_free_list_id (list_id);
    }
}

/*
 * sqmgr_prepare_and_execute_query -
 *
 * return:
 *
 *   thrd(in):
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
sqmgr_prepare_and_execute_query (THREAD_ENTRY * thrd,
				 unsigned int rid, char *request, int reqlen)
{
  int query_id, var_count, var_datasize, var_actual_datasize;
  QFILE_LIST_ID *q_result;
  int csserror, i, listid_length, error_id;
  char *xasl_buffer;
  int xasl_size;
  char *ptr, *var_data, *list_data;
  DB_VALUE *dbvals;
  OR_ALIGNED_BUF (OR_INT_SIZE * 4) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  PAGE_PTR page_ptr;
  int page_size;
  char page_buf[IO_MAX_PAGE_SIZE];	/* page buffer */
  QUERY_FLAG flag;

  xasl_buffer = NULL;
  xasl_size = 0;
  var_data = NULL;
  var_datasize = 0;
  dbvals = NULL;
  list_data = NULL;
  page_ptr = NULL;
  page_size = 0;
  q_result = NULL;

  csserror = css_receive_data_from_client (rid, &xasl_buffer,
					   (int *) &xasl_size);
  if (csserror)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_NET_SERVER_DATA_RECEIVE,
	      0);
      css_send_abort_to_client (rid);
      goto cleanup;
    }

  ptr = or_unpack_int (request, &var_count);
  ptr = or_unpack_int (ptr, &var_datasize);
  ptr = or_unpack_int (ptr, &flag);

  if (var_count && var_datasize)
    {
      csserror = css_receive_data_from_client (rid, &var_data,
					       (int *) &var_actual_datasize);
      if (csserror)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_NET_SERVER_DATA_RECEIVE, 0);
	  css_send_abort_to_client (rid);
	  goto cleanup;
	}
      dbvals = (DB_VALUE *) db_private_alloc (thrd,
					      sizeof (DB_VALUE) * var_count);
      if (dbvals == NULL)
	{
	  css_send_abort_to_client (rid);
	  goto cleanup;
	}
      ptr = var_data;
      for (i = 0; i < var_count; i++)
	{
	  ptr = or_unpack_db_value (ptr, &dbvals[i]);
	}
      /*
       * Don't need this anymore; might as well return the memory now.  It
       * could conceivably be largish if we sent down some big sets as host
       * vars.
       */
      free_and_init (var_data);
      var_data = NULL;
    }

  /*
   * After this point, xqmgr_prepare_and_execute_query has assumed
   * responsibility for freeing xasl_buffer...
   */
  q_result = xqmgr_prepare_and_execute_query (thrd, xasl_buffer, xasl_size,
					      &query_id, var_count, dbvals,
					      &flag);
  if (xasl_buffer)
    {
      free_and_init (xasl_buffer);	/* allocated at css_receive_data_from_client() */
      xasl_buffer = NULL;
    }

  /*
   * For streaming queries, the dbvals will be cleared when the query
   * has completed.
   */
  if (IS_SYNC_EXEC_MODE (flag) && (dbvals))
    {
      for (i = 0; i < var_count; i++)
	{
	  db_value_clear (&dbvals[i]);
	}
      db_private_free_and_init (thrd, dbvals);
    }

  if (q_result == NULL)
    {
      /* ER_LK_UNILATERALLY_ABORTED may occur,
       * if it occurs, rollback transaction
       */
      if (er_errid () == ER_LK_UNILATERALLY_ABORTED)
	{
	  tran_server_unilaterally_abort_tran (thrd);
	}
      return_error_to_client (rid);
    }

  ptr = or_pack_int (reply, (int) QUERY_END);
  listid_length = or_listid_length (q_result);

  /* listid_length can be reset after pb_fetch() return
   * move this after reset statement
   ptr = or_pack_int(ptr, listid_length);
   */

  if (listid_length)
    {
      if (IS_SYNC_EXEC_MODE (flag))
	{
	  if (!VPID_ISNULL (&q_result->first_vpid))
	    {
	      page_ptr = qmgr_get_old_page (thrd, &q_result->first_vpid,
					    q_result->tfile_vfid);
	    }
	  if (page_ptr)
	    {
	      if ((QFILE_GET_TUPLE_COUNT (page_ptr) == -2)
		  || (QFILE_GET_OVERFLOW_PAGE_ID (page_ptr) != NULL_PAGEID))
		{
		  page_size = DB_PAGESIZE;
		}
	      else
		{
		  page_size = QFILE_GET_LAST_TUPLE_OFFSET (page_ptr) +
		    QFILE_GET_TUPLE_LENGTH (page_ptr +
					    QFILE_GET_LAST_TUPLE_OFFSET
					    (page_ptr));
		}
	      /* to free page_ptr early */
	      memcpy (page_buf, page_ptr, page_size);
	      qmgr_free_old_page (thrd, page_ptr, q_result->tfile_vfid);
	    }
	  else
	    {
	      /* for sync query, following error may not occur,
	       * if it does, rollback transaction
	       */
	      if ((error_id = er_errid ()) < 0)
		{
		  if (error_id == ER_LK_UNILATERALLY_ABORTED)
		    {
		      tran_server_unilaterally_abort_tran (thrd);
		    }
		  return_error_to_client (rid);
		  listid_length = 0;
		}
	      /* if query type is not select, page ptr can be null */
	    }
	}
      if ((page_size > DB_PAGESIZE) || (page_size < 0))
	{
	  page_size = 0;
	}
      ptr = or_pack_int (ptr, listid_length);
      ptr = or_pack_int (ptr, page_size);
      ptr = or_pack_int (ptr, query_id);
      if (listid_length > 0)
	{
	  list_data = (char *) db_private_alloc (thrd, listid_length);
	}
      if (list_data)
	{
	  ptr = or_pack_listid (list_data, q_result);
	}
    }
  else
    {
      /* pack a couple of zeros for page_size and query_id
       * since the client will unpack them.
       */
      ptr = or_pack_int (ptr, listid_length);
      ptr = or_pack_int (ptr, 0);
      ptr = or_pack_int (ptr, 0);
    }

  css_send_reply_and_2_data_to_client (rid, reply,
				       OR_ALIGNED_BUF_SIZE (a_reply),
				       list_data, listid_length, page_buf,
				       page_size);

cleanup:
  if (xasl_buffer)
    {
      free_and_init (xasl_buffer);	/* allocated at css_receive_data_from_client() */
    }

  if (var_data)
    {
      free_and_init (var_data);
    }
  if (list_data)
    {
      db_private_free_and_init (thrd, list_data);
    }

  /* since the listid was copied over to the client, we don't need
     this one on the server */
  if (q_result)
    {
      qfile_free_list_id (q_result);
    }
}

/*
 * sqmgr_end_query -
 *
 * return:
 *
 *   thrd(in):
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
sqmgr_end_query (THREAD_ENTRY * thrd, unsigned int rid,
		 char *request, int reqlen)
{
  int query_id;
  int success = NO_ERROR;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  (void) or_unpack_int (request, &query_id);
  if (query_id > 0)
    {
      success = xqmgr_end_query (thrd, query_id);
      if (success != NO_ERROR)
	{
	  /* ER_LK_UNILATERALLY_ABORTED may not occur,
	   * but if it occurs, rollback transaction
	   */
	  if (er_errid () == ER_LK_UNILATERALLY_ABORTED)
	    {
	      tran_server_unilaterally_abort_tran (thrd);
	    }
	  return_error_to_client (rid);
	}
    }

  (void) or_pack_int (reply, (int) success);
  css_send_data_to_client (rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * sqmgr_drop_query_plan - Process a SERVER_QM_DROP_PLAN request
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 * Delete the XASL cache specified by either the query string or the XASL file
 * id upon request of the client.
 * This function is a counter part to qmgr_drop_query_plan().
 */
void
sqmgr_drop_query_plan (THREAD_ENTRY * thread_p, unsigned int rid,
		       char *request, int reqlen)
{
  XASL_ID xasl_id;
  int status;
  int drop;
  char *ptr, *query_str, *reply;
  OID user_oid;
  OR_ALIGNED_BUF (OR_LOID_SIZE) a_reply;

  reply = OR_ALIGNED_BUF_START (a_reply);

  /* unpack query string from the request data */
  ptr = or_unpack_string_nocopy (request, &query_str);
  /* unpack OID of the current user */
  ptr = or_unpack_oid (ptr, &user_oid);
  /* unpack XASL_ID
     - borrow LOID structure only for transmision purpose */
  ptr = or_unpack_loid (ptr, (LOID *) & xasl_id);
  /* unpack 'delete' flag */
  ptr = or_unpack_int (ptr, &drop);

  /* call the server routine of query drop plan */
  status =
    xqmgr_drop_query_plan (thread_p, query_str, &user_oid, &xasl_id, drop);
  if (status != NO_ERROR)
    {
      /* ER_LK_UNILATERALLY_ABORTED may not occur,
         but if it occurs, rollback transaction */
      if (er_errid () == ER_LK_UNILATERALLY_ABORTED)
	tran_server_unilaterally_abort_tran (thread_p);
      return_error_to_client (rid);
    }

  /* pack status (DB_IN32) as a reply */
  (void) or_pack_int (reply, status);

  /* send reply and data to the client */
  css_send_data_to_client (rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));

}

/*
 * sqmgr_drop_all_query_plans - Process a SERVER_QM_DROP_ALL_PLANS request
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 * Clear all XASL cache entires out upon request of the client.
 * This function is a counter part to qmgr_drop_all_query_plans().
 */
void
sqmgr_drop_all_query_plans (THREAD_ENTRY * thread_p, unsigned int rid,
			    char *request, int reqlen)
{
  int status;
  char *reply;
  OR_ALIGNED_BUF (OR_LOID_SIZE) a_reply;

  reply = OR_ALIGNED_BUF_START (a_reply);

  /* call the server routine of query drop plan */
  status = xqmgr_drop_all_query_plans (thread_p);
  if (status != NO_ERROR)
    {
      /* ER_LK_UNILATERALLY_ABORTED may not occur,
         but if it occurs, rollback transaction */
      if (er_errid () == ER_LK_UNILATERALLY_ABORTED)
	{
	  tran_server_unilaterally_abort_tran (thread_p);
	}
      return_error_to_client (rid);
    }

  /* pack status (DB_IN32) as a reply */
  (void) or_pack_int (reply, status);

  /* send reply and data to the client */
  css_send_data_to_client (rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * sqmgr_dump_query_plans -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
sqmgr_dump_query_plans (THREAD_ENTRY * thread_p, unsigned int rid,
			char *request, int reqlen)
{
  FILE *outfp;
  int file_size;
  char *buffer;
  int buffer_size;
  int send_size;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  (void) or_unpack_int (request, &buffer_size);

  buffer = (char *) db_private_alloc (thread_p, buffer_size);
  if (buffer == NULL)
    {
      css_send_abort_to_client (rid);
      return;
    }

  outfp = tmpfile ();
  if (outfp == NULL)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR,
			   0);
      css_send_abort_to_client (rid);
      db_private_free_and_init (thread_p, buffer);
      return;
    }

  xqmgr_dump_query_plans (thread_p, outfp);
  file_size = ftell (outfp);

  /*
   * Send the file in pieces
   */
  rewind (outfp);

  (void) or_pack_int (reply, (int) file_size);
  css_send_data_to_client (rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));

  while (file_size > 0)
    {
      if (file_size > buffer_size)
	{
	  send_size = buffer_size;
	}
      else
	{
	  send_size = file_size;
	}

      file_size -= send_size;
      if (fread (buffer, 1, send_size, outfp) == 0)
	{
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			       ER_GENERIC_ERROR, 0);
	  css_send_abort_to_client (rid);
	  /*
	   * Continue sending the stuff that was prmoised to client. In this case
	   * junk (i.e., whatever it is in the buffers) is sent.
	   */
	}
      css_send_data_to_client (rid, buffer, send_size);
    }

  fclose (outfp);
  db_private_free_and_init (thread_p, buffer);
}

/*
 * sqmgr_dump_query_cache -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
sqmgr_dump_query_cache (THREAD_ENTRY * thread_p, unsigned int rid,
			char *request, int reqlen)
{
  FILE *outfp;
  int file_size;
  char *buffer;
  int buffer_size;
  int send_size;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  (void) or_unpack_int (request, &buffer_size);

  buffer = (char *) db_private_alloc (thread_p, buffer_size);
  if (buffer == NULL)
    {
      css_send_abort_to_client (rid);
      return;
    }

  outfp = tmpfile ();
  if (outfp == NULL)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR,
			   0);
      css_send_abort_to_client (rid);
      db_private_free_and_init (thread_p, buffer);
      return;
    }

  xqmgr_dump_query_cache (thread_p, outfp);
  file_size = ftell (outfp);

  /*
   * Send the file in pieces
   */
  rewind (outfp);

  (void) or_pack_int (reply, (int) file_size);
  css_send_data_to_client (rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));

  while (file_size > 0)
    {
      if (file_size > buffer_size)
	{
	  send_size = buffer_size;
	}
      else
	{
	  send_size = file_size;
	}

      file_size -= send_size;
      if (fread (buffer, 1, send_size, outfp) == 0)
	{
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			       ER_GENERIC_ERROR, 0);
	  css_send_abort_to_client (rid);
	  /*
	   * Continue sending the stuff that was prmoised to client. In this case
	   * junk (i.e., whatever it is in the buffers) is sent.
	   */
	}
      css_send_data_to_client (rid, buffer, send_size);
    }
  fclose (outfp);
  db_private_free_and_init (thread_p, buffer);
}

/*
 * sqmgr_get_query_info -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
sqmgr_get_query_info (THREAD_ENTRY * thread_p, unsigned int rid,
		      char *request, int reqlen)
{
  int query_id;
  int count;
  int length;
  char *buf;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  (void) or_unpack_int (request, &query_id);

  count = xqmgr_get_query_info (thread_p, query_id);

  buf = (char *) qmgr_get_area_error_async (thread_p, &length, count,
					    query_id);

  (void) or_pack_int (reply, length);

  /* xqmgr_get_query_info() check query error,
   * if error was ER_LK_UNILATERALLY_ABORTED,
   * then rollback transaction
   */
  if (count < 0 && er_errid () == ER_LK_UNILATERALLY_ABORTED)
    {
      tran_server_unilaterally_abort_tran (thread_p);
    }
  css_send_reply_and_data_to_client (rid, reply,
				     OR_ALIGNED_BUF_SIZE (a_reply), buf,
				     length);
  free_and_init (buf);
}

/*
 * sqmgr_sync_query -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
sqmgr_sync_query (THREAD_ENTRY * thread_p, unsigned int rid, char *request,
		  int reqlen)
{
  int query_id;
  int wait, success;
  char *ptr;
  OR_ALIGNED_BUF (OR_INT_SIZE * 2) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  QFILE_LIST_ID new_list_id;
  int list_length;
  char *list_data;

  ptr = or_unpack_int (request, &query_id);
  ptr = or_unpack_int (ptr, &wait);

  memset (&new_list_id, 0, sizeof (QFILE_LIST_ID));

  success = xqmgr_sync_query (thread_p, query_id, wait, &new_list_id, false);

  if (success != NO_ERROR)
    {
      success = er_errid ();
      ptr = or_pack_int (reply, 0);
      ptr = or_pack_int (ptr, success);
      list_length = 0;
      list_data = NULL;
    }
  else
    {
      list_length = or_listid_length (&new_list_id);
      ptr = or_pack_int (reply, list_length);
      ptr = or_pack_int (ptr, success);
      list_data = (char *) db_private_alloc (thread_p, list_length);
    }

  if (list_data == NULL)
    {
      /* During query execution, ER_LK_UNILATERALLY_ABORTED may have occurred.
       * then, xqmgr_sync_query() set this error.
       * if it occurs, rollback transaction
       */
      if (er_errid () == ER_LK_UNILATERALLY_ABORTED)
	{
	  tran_server_unilaterally_abort_tran (thread_p);
	}
      return_error_to_client (rid);
      list_length = 0;
    }
  else
    {
      ptr = or_pack_listid (list_data, &new_list_id);
    }

  css_send_reply_and_data_to_client (rid, reply,
				     OR_ALIGNED_BUF_SIZE (a_reply), list_data,
				     list_length);
  qfile_clear_list_id (&new_list_id);
  db_private_free_and_init (thread_p, list_data);
}

/*
 * sqp_get_sys_timestamp -
 *
 * return:
 *
 *   rid(in):
 *
 * NOTE:
 */
void
sqp_get_sys_timestamp (unsigned int rid)
{
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  DB_VALUE sys_timestamp;

  db_sys_timestamp (&sys_timestamp);
  (void) or_pack_utime (reply,
			*(DB_TIMESTAMP *) DB_GET_TIMESTAMP (&sys_timestamp));
  css_send_data_to_client (rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * sqp_get_current_value -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
sqp_get_current_value (THREAD_ENTRY * thread_p, unsigned int rid,
		       char *request, int reqlen)
{
  int error_status = NO_ERROR;
  DB_VALUE oid, cur_val;
  char *buffer;
  int buffer_length;
  OR_ALIGNED_BUF (OR_INT_SIZE + OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  char *p;

  (void) or_unpack_value (request, &oid);
  error_status = xqp_get_serial_current_value (thread_p, &oid, &cur_val);
  db_value_clear (&oid);

  if (error_status != NO_ERROR)
    {
      error_status = er_errid ();
      p = or_pack_int (reply, 0);
      p = or_pack_int (p, error_status);
      buffer_length = 0;
      buffer = 0;
    }
  else
    {
      buffer_length = or_db_value_size (&cur_val);
      buffer = (char *) db_private_alloc (thread_p, buffer_length);
      p = or_pack_int (reply, buffer_length);
      p = or_pack_int (p, 0);
    }

  if (buffer == NULL)
    {
      /* ER_LK_UNILATERALLY_ABORTED may occur,
       * if it occurs, rollback transaction
       */
      if (error_status == ER_LK_UNILATERALLY_ABORTED)
	{
	  tran_server_unilaterally_abort_tran (thread_p);
	}
      return_error_to_client (rid);
      buffer_length = 0;
    }
  else
    {
      (void) or_pack_value (buffer, &cur_val);
      db_value_clear (&cur_val);
    }

  css_send_reply_and_data_to_client (rid, reply,
				     OR_ALIGNED_BUF_SIZE (a_reply), buffer,
				     buffer_length);
  if (buffer != NULL)
    {
      /* since this was copied to the client, we don't need it on the server */
      db_private_free_and_init (thread_p, buffer);
    }
}

/*
 * sqp_get_next_value -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
sqp_get_next_value (THREAD_ENTRY * thread_p, unsigned int rid, char *request,
		    int reqlen)
{
  int error_status = NO_ERROR;
  DB_VALUE oid, next_val;
  char *buffer;
  int buffer_length;
  OR_ALIGNED_BUF (OR_INT_SIZE + OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  char *p;

  (void) or_unpack_value (request, &oid);
  error_status = xqp_get_serial_next_value (thread_p, &oid, &next_val);
  db_value_clear (&oid);

  if (error_status != NO_ERROR)
    {
      error_status = er_errid ();
      p = or_pack_int (reply, 0);
      p = or_pack_int (p, error_status);
      buffer_length = 0;
      buffer = 0;
    }
  else
    {
      buffer_length = or_db_value_size (&next_val);
      buffer = (char *) db_private_alloc (thread_p, buffer_length);
      p = or_pack_int (reply, buffer_length);
      p = or_pack_int (p, 0);
    }

  if (buffer == NULL)
    {
      /* ER_LK_UNILATERALLY_ABORTED may occur,
       * if it occurs, rollback transaction
       */
      if (error_status == ER_LK_UNILATERALLY_ABORTED)
	{
	  tran_server_unilaterally_abort_tran (thread_p);
	}
      return_error_to_client (rid);
      buffer_length = 0;
    }
  else
    {
      (void) or_pack_value (buffer, &next_val);
      db_value_clear (&next_val);
    }

  css_send_reply_and_data_to_client (rid, reply,
				     OR_ALIGNED_BUF_SIZE (a_reply), buffer,
				     buffer_length);
  if (buffer != NULL)
    {
      /* since this was copied to the client, we don't need it on the server */
      db_private_free_and_init (thread_p, buffer);
    }
}

/*
 * smnt_server_start_stats -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
smnt_server_start_stats (THREAD_ENTRY * thread_p, unsigned int rid,
			 char *request, int reqlen)
{
  int success;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  success = xmnt_server_start_stats (thread_p);
  if (success != NO_ERROR)
    {
      return_error_to_client (rid);
    }

  (void) or_pack_int (reply, (int) success);
  css_send_data_to_client (rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * smnt_server_stop_stats -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
smnt_server_stop_stats (THREAD_ENTRY * thread_p, unsigned int rid,
			char *request, int reqlen)
{
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  xmnt_server_stop_stats (thread_p);
  /* dummy reply message */
  (void) or_pack_int (reply, 1);
  css_send_data_to_client (rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * smnt_server_reset_stats -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
smnt_server_reset_stats (THREAD_ENTRY * thread_p, unsigned int rid,
			 char *request, int reqlen)
{
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  xmnt_server_reset_stats (thread_p);
  /* dummy reply message */
  (void) or_pack_int (reply, 1);
  css_send_data_to_client (rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * smnt_server_copy_stats -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
smnt_server_copy_stats (THREAD_ENTRY * thread_p, unsigned int rid,
			char *request, int reqlen)
{
  OR_ALIGNED_BUF (STAT_SIZE_PACKED) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  MNT_SERVER_EXEC_STATS stats;

  /* check to see if the pack/unpack functions match the current
     structure definition */
#if !defined(PRODUCTION)
  {
    int size;

    size = STAT_SIZE_MEMORY;
    if ((DB_ALIGN (size, 8) + sizeof (MUTEX_T))
	!= sizeof (MNT_SERVER_EXEC_STATS))
      {
	/* could use er_set here but is an unusual condition that will
	   be caught before product ships */
	printf ("Server statistics structure does not match "
		"expected size !\n");
      }
  }
#endif /* !PRODUCTION */
  xmnt_server_copy_stats (thread_p, &stats);
  net_pack_stats (reply, &stats);
  css_send_data_to_client (rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * sct_can_accept_new_repr -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
sct_can_accept_new_repr (THREAD_ENTRY * thread_p, unsigned int rid,
			 char *request, int reqlen)
{
  OID classoid;
  HFID hfid;
  int success, can_accept;
  OR_ALIGNED_BUF (OR_INT_SIZE * 2) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  char *ptr;

  ptr = or_unpack_oid (request, &classoid);
  ptr = or_unpack_hfid (ptr, &hfid);

  success = xcatalog_is_acceptable_new_representation (thread_p, &classoid,
						       &hfid, &can_accept);

  if (success != NO_ERROR)
    {
      /* ER_LK_UNILATERALLY_ABORTED may not occur,
       * but if it occurs, rollback transaction
       */
      if (er_errid () == ER_LK_UNILATERALLY_ABORTED)
	{
	  tran_server_unilaterally_abort_tran (thread_p);
	}
      return_error_to_client (rid);
    }

  ptr = or_pack_int (reply, (int) success);
  ptr = or_pack_int (ptr, (int) can_accept);

  css_send_data_to_client (rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * xs_send_method_call_info_to_client -
 *
 * return:
 *
 *   list_id(in):
 *   method_sig_list(in):
 *
 * NOTE:
 */
int
xs_send_method_call_info_to_client (THREAD_ENTRY * thread_p,
				    QFILE_LIST_ID * list_id,
				    METHOD_SIG_LIST * method_sig_list)
{
  int length = 0;
  char *databuf;
  char *ptr;
  unsigned int rid;
  OR_ALIGNED_BUF (OR_INT_SIZE * 4) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  rid = thread_get_comm_request_id (thread_p);
  length = or_listid_length ((void *) list_id);
  length += or_method_sig_list_length ((void *) method_sig_list);
  ptr = or_pack_int (reply, (int) METHOD_CALL);
  ptr = or_pack_int (ptr, length);

  databuf = (char *) db_private_alloc (thread_p, length);
  if (databuf == NULL)
    {
      return ER_FAILED;
    }

  ptr = or_pack_listid (databuf, (void *) list_id);
  ptr = or_pack_method_sig_list (ptr, (void *) method_sig_list);
  css_send_reply_and_data_to_client (rid, reply,
				     OR_ALIGNED_BUF_SIZE (a_reply), databuf,
				     length);
  db_private_free_and_init (thread_p, databuf);
  return NO_ERROR;
}

/*
 * xs_receive_data_from_client -
 *
 * return:
 *
 *   area(in):
 *   datasize(in):
 *
 * NOTE:
 */
int
xs_receive_data_from_client (THREAD_ENTRY * thread_p, char **area,
			     int *datasize)
{
  unsigned int rid;
  bool continue_checking = true;

  if (*area)
    {
      free_and_init (*area);
    }
  rid = thread_get_comm_request_id (thread_p);

  if (css_receive_data_from_client (rid, area, (int *) datasize))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_NET_SERVER_DATA_RECEIVE,
	      0);
      return ER_FAILED;
    }
  if (logtb_is_interrupt (thread_p, false, &continue_checking))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_INTERRUPT, 0);
      return ER_FAILED;
    }
  return NO_ERROR;
}

/*
 * xs_send_action_to_client -
 *
 * return:
 *
 *   action(in):
 *
 * NOTE:
 */
int
xs_send_action_to_client (THREAD_ENTRY * thread_p,
			  VACOMM_BUFFER_CLIENT_ACTION action)
{
  unsigned int rid;
  bool continue_checking = true;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  if (logtb_is_interrupt (thread_p, false, &continue_checking))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_INTERRUPT, 0);
      return ER_FAILED;
    }

  rid = thread_get_comm_request_id (thread_p);
  (void) or_pack_int (reply, (int) action);
  if (css_send_data_to_client (rid, reply, OR_INT_SIZE))
    {
      return ER_FAILED;
    }

  return NO_ERROR;
}

/*
 * stest_performance -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
stest_performance (unsigned int rid, char *request, int reqlen)
{
  int return_size;
  OR_ALIGNED_BUF (10000) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  if (reqlen >= OR_INT_SIZE)
    {
      or_unpack_int (request, &return_size);
      if (return_size > 0)
	{
	  css_send_data_to_client (rid, reply, return_size);
	}
    }
}

/*
 * slocator_assign_oid_batch -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
slocator_assign_oid_batch (THREAD_ENTRY * thread_p, unsigned int rid,
			   char *request, int reqlen)
{
  int success = ER_FAILED;
  LC_OIDSET *oidset = NULL;

  /* skip over the word at the front reserved for the return code */
  oidset = locator_unpack_oid_set_to_new (thread_p, request + OR_INT_SIZE);
  if (oidset == NULL)
    {
      return_error_to_client (rid);
      return;
    }

  success = xlocator_assign_oid_batch (thread_p, oidset);

  /* the buffer we send back is identical in size to the buffer that was
   * received so we can reuse it.
   */

  /* first word is reserved for return code */
  or_pack_int (request, success);
  if (success == NO_ERROR)
    {
      if (locator_pack_oid_set (request + OR_INT_SIZE, oidset) == NULL)
	{
	  /* trouble packing oidset for the return trip, severe error */
	  success = ER_FAILED;
	  or_pack_int (request, success);
	}
    }

  if (success != NO_ERROR)
    {
      /* ER_LK_UNILATERALLY_ABORTED may not occur,
       * but if it occurs, rollback transaction
       */
      if (er_errid () == ER_LK_UNILATERALLY_ABORTED)
	{
	  tran_server_unilaterally_abort_tran (thread_p);
	}
      return_error_to_client (rid);
    }

  css_send_data_to_client (rid, request, reqlen);

  locator_free_oid_set (thread_p, oidset);
}

/*
 * slocator_find_lockhint_class_oids -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
slocator_find_lockhint_class_oids (THREAD_ENTRY * thread_p, unsigned int rid,
				   char *request, int reqlen)
{
  int num_classes;
  char **many_classnames;
  LOCK *many_locks = NULL;
  int *many_need_subclasses = NULL;
  OID *guessed_class_oids = NULL;
  int *guessed_class_chns = NULL;
  int quit_on_errors;
  LC_FIND_CLASSNAME allfind = LC_CLASSNAME_ERROR;
  LC_LOCKHINT *found_lockhint;
  LC_COPYAREA *copy_area;
  char *desc_ptr = NULL;
  int desc_size;
  char *content_ptr;
  int content_size;
  char *ptr;
  int num_objs = 0;
  char *packed = NULL;
  int packed_size;
  int send_size = 0;
  int i;
  int malloc_size;
  char *malloc_area;

  OR_ALIGNED_BUF (NET_SENDRECV_BUFFSIZE + NET_COPY_AREA_SENDRECV_SIZE +
		  OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  found_lockhint = NULL;
  copy_area = NULL;

  ptr = or_unpack_int (request, &num_classes);
  ptr = or_unpack_int (ptr, &quit_on_errors);

  malloc_size = ((DB_SIZEOF (char *) + DB_SIZEOF (LOCK) + DB_SIZEOF (int) +
		  DB_SIZEOF (OID) + DB_SIZEOF (int)) * num_classes);

  malloc_area = (char *) db_private_alloc (thread_p, malloc_size);
  if (malloc_area != NULL)
    {
      many_classnames = (char **) malloc_area;
      many_locks = (LOCK *) ((char *) malloc_area +
			     (DB_SIZEOF (char *) * num_classes));
      many_need_subclasses = (int *) ((char *) many_locks +
				      (DB_SIZEOF (LOCK) * num_classes));
      guessed_class_oids = (OID *) ((char *) many_need_subclasses +
				    (DB_SIZEOF (int) * num_classes));
      guessed_class_chns = (int *) ((char *) guessed_class_oids +
				    (DB_SIZEOF (OID) * num_classes));

      for (i = 0; i < num_classes; i++)
	{
	  ptr = or_unpack_string_nocopy (ptr, &many_classnames[i]);
	  ptr = or_unpack_lock (ptr, &many_locks[i]);
	  ptr = or_unpack_int (ptr, &many_need_subclasses[i]);
	  ptr = or_unpack_oid (ptr, &guessed_class_oids[i]);
	  ptr = or_unpack_int (ptr, &guessed_class_chns[i]);
	}

      allfind = xlocator_find_lockhint_class_oids (thread_p, num_classes,
						   (const char **)
						   many_classnames,
						   many_locks,
						   many_need_subclasses,
						   guessed_class_oids,
						   guessed_class_chns,
						   quit_on_errors,
						   &found_lockhint,
						   &copy_area);
    }
  if (allfind != LC_CLASSNAME_EXIST)
    {
      /* ER_LK_UNILATERALLY_ABORTED may occur,
       * if it occurs, rollback transaction
       */
      if (er_errid () == ER_LK_UNILATERALLY_ABORTED)
	{
	  tran_server_unilaterally_abort_tran (thread_p);
	}
      return_error_to_client (rid);
    }

  if (found_lockhint != NULL && found_lockhint->length > 0)
    {
      send_size = locator_pack_lockhint (found_lockhint, true);

      packed = found_lockhint->packed;
      packed_size = found_lockhint->packed_size;

      if (!packed)
	{
	  return_error_to_client (rid);
	  allfind = LC_CLASSNAME_ERROR;
	}
    }

  if (copy_area != NULL)
    {
      num_objs = locator_send_copy_area (copy_area, &content_ptr,
					 &content_size, &desc_ptr,
					 &desc_size);
    }
  else
    {
      desc_ptr = NULL;
      desc_size = 0;
      content_ptr = NULL;
      content_size = 0;
    }

  /* Send sizes of databuffer and copy area (descriptor + content) */

  ptr = or_pack_int (reply, send_size);
  ptr = or_pack_int (ptr, num_objs);
  ptr = or_pack_int (ptr, desc_size);
  ptr = or_pack_int (ptr, content_size);

  ptr = or_pack_int (ptr, allfind);

  if (copy_area == NULL && found_lockhint == NULL)
    {
      css_send_data_to_client (rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
    }
  else
    {
      css_send_reply_and_3_data_to_client (rid, reply,
					   OR_ALIGNED_BUF_SIZE (a_reply),
					   packed, send_size,
					   desc_ptr, desc_size,
					   content_ptr, content_size);
      if (copy_area != NULL)
	{
	  locator_free_copy_area (copy_area);
	}

      if (found_lockhint != NULL)
	{
	  locator_free_lockhint (found_lockhint);
	}

      if (desc_ptr)
	{
	  free_and_init (desc_ptr);
	}
    }

  if (malloc_area)
    {
      db_private_free_and_init (thread_p, malloc_area);
    }
}

/*
 * slocator_fetch_lockhint_classes -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
slocator_fetch_lockhint_classes (THREAD_ENTRY * thread_p, unsigned int rid,
				 char *request, int reqlen)
{
  int success;
  LC_COPYAREA *copy_area;
  LC_LOCKHINT *lockhint;
  OR_ALIGNED_BUF (NET_SENDRECV_BUFFSIZE + NET_COPY_AREA_SENDRECV_SIZE +
		  OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  char *desc_ptr;
  int desc_size;
  char *content_ptr;
  int content_size;
  char *ptr;
  bool first_call;
  int num_objs;
  char *packed = NULL;
  int packed_size;
  int send_size;

  ptr = or_unpack_int (request, &packed_size);

  if (packed_size == 0
      || css_receive_data_from_client (rid, &packed, (int *) &packed_size))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_NET_SERVER_DATA_RECEIVE,
	      0);
      css_send_abort_to_client (rid);
      if (packed)
	{
	  free_and_init (packed);
	}
      return;
    }

  lockhint = locator_allocate_and_unpack_lockhint (packed, packed_size,
						   true, false);

  if (packed)
    {
      free_and_init (packed);
    }
  if ((lockhint == NULL) || (lockhint->length <= 0))
    {
      return_error_to_client (rid);
      ptr = or_pack_int (reply, 0);
      ptr = or_pack_int (ptr, 0);
      ptr = or_pack_int (ptr, 0);
      ptr = or_pack_int (ptr, 0);
      ptr = or_pack_int (ptr, ER_FAILED);
      css_send_data_to_client (rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
      return;
    }

  first_call = true;
  do
    {
      desc_ptr = NULL;
      num_objs = 0;

      copy_area = NULL;
      success = xlocator_fetch_lockhint_classes (thread_p, lockhint,
						 &copy_area);
      if (success != NO_ERROR)
	{
	  /* ER_LK_UNILATERALLY_ABORTED may occur,
	   * if it occurs, rollback transaction
	   */
	  if (er_errid () == ER_LK_UNILATERALLY_ABORTED)
	    {
	      tran_server_unilaterally_abort_tran (thread_p);
	    }
	  return_error_to_client (rid);
	}

      if (copy_area != NULL)
	{
	  num_objs = locator_send_copy_area (copy_area, &content_ptr,
					     &content_size, &desc_ptr,
					     &desc_size);
	}
      else
	{
	  desc_ptr = NULL;
	  desc_size = 0;
	  content_ptr = NULL;
	  content_size = 0;
	}

      /* Send sizes of databuffer and copy area (descriptor + content) */

      send_size = locator_pack_lockhint (lockhint, first_call);

      packed = lockhint->packed;
      packed_size = lockhint->packed_size;

      ptr = or_pack_int (reply, send_size);
      ptr = or_pack_int (ptr, num_objs);
      ptr = or_pack_int (ptr, desc_size);
      ptr = or_pack_int (ptr, content_size);

      ptr = or_pack_int (ptr, success);

      if (copy_area == NULL && lockhint == NULL)
	{
	  css_send_data_to_client (rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
	}
      else
	{
	  css_send_reply_and_3_data_to_client (rid, reply,
					       OR_ALIGNED_BUF_SIZE (a_reply),
					       packed, send_size,
					       desc_ptr, desc_size,
					       content_ptr, content_size);
	}
      if (copy_area != NULL)
	{
	  locator_free_copy_area (copy_area);
	}
      if (desc_ptr)
	{
	  free_and_init (desc_ptr);
	}

      first_call = false;
    }
  while (copy_area && lockhint
	 && ((lockhint->num_classes > lockhint->num_classes_processed)));

  if (lockhint)
    {
      locator_free_lockhint (lockhint);
    }
}

/*
 * tm_restart_event_handler -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
tm_restart_event_handler (unsigned int rid, char *request, int reqlen)
{
  int status = false;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  status = true;

  (void) or_pack_int (reply, status);
  css_send_data_to_client (rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * sthread_kill_tran_index -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
sthread_kill_tran_index (THREAD_ENTRY * thread_p, unsigned int rid,
			 char *request, int reqlen)
{
  int success;
  int kill_tran_index;
  int kill_pid;
  char *kill_user;
  char *kill_host;
  char *ptr;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  ptr = or_unpack_int (request, &kill_tran_index);
  ptr = or_unpack_string_nocopy (ptr, &kill_user);
  ptr = or_unpack_string_nocopy (ptr, &kill_host);
  ptr = or_unpack_int (ptr, &kill_pid);

  if (xthread_kill_tran_index (thread_p, kill_tran_index, kill_user,
			       kill_host, kill_pid) != NO_ERROR)
    {
      success = ER_FAILED;
      return_error_to_client (rid);
    }
  else
    {
      success = NO_ERROR;
    }

  ptr = or_pack_int (reply, success);
  css_send_data_to_client (rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * slogtb_get_pack_tran_table -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
slogtb_get_pack_tran_table (THREAD_ENTRY * thread_p, unsigned int rid,
			    char *request, int reqlen)
{
  char *buffer, *ptr;
  int size;
  OR_ALIGNED_BUF (OR_INT_SIZE + OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  int error;

  error = xlogtb_get_pack_tran_table (thread_p, &buffer, &size);

  if (error != NO_ERROR)
    {
      ptr = or_pack_int (reply, 0);
      ptr = or_pack_int (ptr, error);
      css_send_data_to_client (rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
    }
  else
    {
      ptr = or_pack_int (reply, size);
      ptr = or_pack_int (ptr, error);
      css_send_data_to_client (rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
      css_send_data_to_client (rid, buffer, size);
      free_and_init (buffer);
    }
}

/*
 * xio_send_user_prompt_to_client -
 *
 * return:
 *
 *   prompt_id(in):
 *   prompt(in):
 *   failure_prompt(in):
 *   range_low(in):
 *   range_high(in):
 *   secondary_prompt(in):
 *   reprompt_value(in):
 *
 * NOTE:
 * can be called only in the context of a net_client_callback
 * that is waiting for the size of 3 integers to be returned.
 * presently, on the client side that is bo_backup.
 */
int
xio_send_user_prompt_to_client (THREAD_ENTRY * thread_p,
				FILEIO_REMOTE_PROMPT_TYPE prompt_id,
				const char *prompt,
				const char *failure_prompt, int range_low,
				int range_high, const char *secondary_prompt,
				int reprompt_value)
{
  OR_ALIGNED_BUF (OR_INT_SIZE * 3) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  int prompt_length;
  unsigned int rid, rc;
  char *ptr;
  char *databuf;

  rid = thread_get_comm_request_id (thread_p);
  /* need to know length of prompt string we are sending */
  prompt_length = or_packed_string_length (prompt);
  prompt_length += or_packed_string_length (failure_prompt);
  prompt_length += OR_INT_SIZE * 2;
  prompt_length += or_packed_string_length (secondary_prompt);
  prompt_length += OR_INT_SIZE;

  /*
   * Client side caller must be expecting a reply/callback followed
   * by 2 ints, otherwise client will abort due to protocol error
   * Prompt_length tells the receiver how big the followon message is.
   */
  ptr = or_pack_int (reply, (int) ASYNC_OBTAIN_USER_INPUT);
  ptr = or_pack_int (ptr, (int) prompt_id);
  ptr = or_pack_int (ptr, prompt_length);

  databuf = (char *) db_private_alloc (thread_p, prompt_length);
  if (databuf == NULL)
    {
      return ER_FAILED;
    }

  ptr = or_pack_string (databuf, (char *) prompt);
  ptr = or_pack_string (ptr, (char *) failure_prompt);
  ptr = or_pack_int (ptr, range_low);
  ptr = or_pack_int (ptr, range_high);
  ptr = or_pack_string (ptr, (char *) secondary_prompt);
  ptr = or_pack_int (ptr, reprompt_value);

  rc = css_send_reply_and_data_to_client (rid, reply,
					  OR_ALIGNED_BUF_SIZE (a_reply),
					  databuf, prompt_length);
  db_private_free_and_init (thread_p, databuf);

  if (rc)
    {
      return ER_FAILED;
    }

  return NO_ERROR;
}

/*
 * shf_get_class_num_objs_and_pages -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
shf_get_class_num_objs_and_pages (THREAD_ENTRY * thread_p, unsigned int rid,
				  char *request, int reqlen)
{
  HFID hfid;
  int success, approximation, nobjs, npages;
  OR_ALIGNED_BUF (OR_INT_SIZE * 3) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  char *ptr;

  ptr = or_unpack_hfid (request, &hfid);
  ptr = or_unpack_int (ptr, &approximation);

  success = ((xheap_get_class_num_objects_pages (thread_p, &hfid,
						 approximation, &nobjs,
						 &npages) ==
	      NO_ERROR) ? NO_ERROR : ER_FAILED);

  if (success != NO_ERROR)
    {
      /* ER_LK_UNILATERALLY_ABORTED may occur,
       * if it occurs, rollback transaction
       */
      if (er_errid () == ER_LK_UNILATERALLY_ABORTED)
	{
	  tran_server_unilaterally_abort_tran (thread_p);
	}
      return_error_to_client (rid);
    }

  ptr = or_pack_int (reply, (int) success);
  ptr = or_pack_int (ptr, (int) nobjs);
  ptr = or_pack_int (ptr, (int) npages);

  css_send_data_to_client (rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * sbtree_get_statistics -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
sbtree_get_statistics (THREAD_ENTRY * thread_p, unsigned int rid,
		       char *request, int reqlen)
{
  BTID btid;
  BTREE_STATS stat_info;
  int success;
  OR_ALIGNED_BUF (OR_INT_SIZE * 5) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  char *ptr;

  ptr = or_unpack_btid (request, &btid);

  success = ((btree_get_stats (thread_p, &btid, &stat_info,
			       false) == NO_ERROR) ? NO_ERROR : ER_FAILED);
  if (success != NO_ERROR)
    {
      /* ER_LK_UNILATERALLY_ABORTED may occur,
       * if it occurs, rollback transaction
       */
      if (er_errid () == ER_LK_UNILATERALLY_ABORTED)
	{
	  tran_server_unilaterally_abort_tran (thread_p);
	}
      return_error_to_client (rid);
    }

  ptr = or_pack_int (reply, success);
  ptr = or_pack_int (ptr, stat_info.leafs);
  ptr = or_pack_int (ptr, stat_info.pages);
  ptr = or_pack_int (ptr, stat_info.height);
  ptr = or_pack_int (ptr, stat_info.keys);

  css_send_data_to_client (rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * sqp_get_server_info -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
sqp_get_server_info (THREAD_ENTRY * thread_p, unsigned int rid, char *request,
		     int reqlen)
{
  int success = NO_ERROR;
  OR_ALIGNED_BUF (OR_INT_SIZE + OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  char *ptr, *buffer = NULL;
  int buffer_length;
  int info_bits, i;
  DB_VALUE value[SI_CNT];

  ptr = or_unpack_int (request, &info_bits);

  for (i = 0, buffer_length = 0; i < SI_CNT && success == NO_ERROR; i++)
    {
      DB_MAKE_NULL (&value[i]);
      if (info_bits & (1 << i))
	{
	  switch ((1 << i))
	    {
	    case SI_SYS_TIMESTAMP:
	      success = ((db_sys_timestamp (&value[i]) == NO_ERROR)
			 ? NO_ERROR : ER_FAILED);
	      break;

	    case SI_LOCAL_TRANSACTION_ID:
	      success = ((xtran_get_local_transaction_id (thread_p,
							  &value[i]) ==
			  NO_ERROR) ? NO_ERROR : ER_FAILED);
	      break;
	    }
	  buffer_length += or_db_value_size (&value[i]);	/* increase buf length */
	}
    }
  if (success == ER_FAILED)
    {
      return_error_to_client (rid);
    }

  if (success == NO_ERROR)
    {				/* buffer_length > 0 */
      buffer = (char *) db_private_alloc (thread_p, buffer_length);

      if (buffer != NULL)
	{
	  for (i = 0, ptr = buffer; i < SI_CNT; i++)
	    {
	      if (info_bits & (1 << i))
		{
		  ptr = or_pack_value (ptr, &value[i]);
		  db_value_clear (&value[i]);
		}
	    }
	}
      else
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_OUT_OF_VIRTUAL_MEMORY, 1, buffer_length);
	  success = ER_FAILED;
	}
    }
  ptr = or_pack_int (reply, buffer_length);
  ptr = or_pack_int (ptr, success);

  css_send_reply_and_data_to_client (rid, reply,
				     OR_ALIGNED_BUF_SIZE (a_reply), buffer,
				     buffer_length);

  if (buffer != NULL)
    {
      db_private_free_and_init (thread_p, buffer);
    }
}

/*
 * sprm_server_change_parameters -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
sprm_server_change_parameters (THREAD_ENTRY * thread_p, unsigned int rid,
			       char *request, int reqlen)
{
  char *data;
  int rc;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  (void) or_unpack_string_nocopy (request, &data);

  rc = xsysprm_change_server_parameters (data);

  if (er_errid () == ER_LK_UNILATERALLY_ABORTED)
    {
      tran_server_unilaterally_abort_tran (thread_p);
      return_error_to_client (rid);
    }

  (void) or_pack_int (reply, rc);
  css_send_data_to_client (rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * sprm_server_obtain_parameters -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
sprm_server_obtain_parameters (THREAD_ENTRY * thread_p, unsigned int rid,
			       char *request, int reqlen)
{
  char *data;
  int len;
  int rc;
  OR_ALIGNED_BUF (OR_INT_SIZE * 2) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  char *ptr;

  (void) or_unpack_int (request, &len);

  data = NULL;
  rc = css_receive_data_from_client (rid, &data, &len);
  if (rc)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_NET_SERVER_DATA_RECEIVE,
	      0);
      css_send_abort_to_client (rid);
    }
  else
    {
      rc = xsysprm_obtain_server_parameters (data, len);

      if (er_errid () == ER_LK_UNILATERALLY_ABORTED)
	{
	  tran_server_unilaterally_abort_tran (thread_p);
	  return_error_to_client (rid);
	}

      ptr = or_pack_int (reply, len);
      ptr = or_pack_int (ptr, rc);
      css_send_reply_and_data_to_client (rid,
					 reply, OR_ALIGNED_BUF_SIZE (a_reply),
					 data, len);

    }
  if (data != NULL)
    {
      free_and_init (data);
    }
}

/*
 * shf_has_instance -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
shf_has_instance (THREAD_ENTRY * thread_p, unsigned int rid, char *request,
		  int reqlen)
{
  HFID hfid;
  OID class_oid;
  int r;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  char *ptr;

  ptr = or_unpack_hfid (request, &hfid);
  ptr = or_unpack_oid (ptr, &class_oid);

  r = xheap_has_instance (thread_p, &hfid, &class_oid);

  if (r == -1)
    {
      /* ER_LK_UNILATERALLY_ABORTED may occur,
       * if it occurs, rollback transaction
       */
      if (er_errid () == ER_LK_UNILATERALLY_ABORTED)
	{
	  tran_server_unilaterally_abort_tran (thread_p);
	}
      return_error_to_client (rid);
    }

  ptr = or_pack_int (reply, (int) r);

  css_send_data_to_client (rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * stran_get_local_transaction_id -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
stran_get_local_transaction_id (THREAD_ENTRY * thread_p, unsigned int rid,
				char *request, int reqlen)
{
  OR_ALIGNED_BUF (OR_INT_SIZE + OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  char *ptr;
  DB_VALUE val;
  int success, trid;

  success = ((xtran_get_local_transaction_id (thread_p, &val) ==
	      NO_ERROR) ? NO_ERROR : ER_FAILED);
  trid = DB_GET_INTEGER (&val);
  ptr = or_pack_int (reply, success);
  ptr = or_pack_int (ptr, trid);
  css_send_data_to_client (rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * sjsp_get_server_port -
 *
 * return:
 *
 *   rid(in):
 *
 * NOTE:
 */
void
sjsp_get_server_port (unsigned int rid)
{
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  (void) or_pack_int (reply, jsp_server_port ());
  css_send_data_to_client (rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * srepl_set_info -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
srepl_set_info (THREAD_ENTRY * thread_p, unsigned int rid, char *request,
		int reqlen)
{
  int success = NO_ERROR;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  char *ptr;
  REPL_INFO repl_info = { 0, NULL };
  REPL_INFO_SCHEMA repl_schema = { 0, NULL, NULL };

  if (PRM_REPLICATION_MODE)
    {
      ptr = or_unpack_int (request, &repl_info.repl_info_type);
      switch (repl_info.repl_info_type)
	{
	case REPL_INFO_TYPE_SCHEMA:
	  {
	    ptr = or_unpack_int (ptr, &repl_schema.statement_type);
	    ptr = or_unpack_string_nocopy (ptr, &repl_schema.name);
	    ptr = or_unpack_string_nocopy (ptr, &repl_schema.ddl);

	    repl_info.info = (char *) &repl_schema;
	    break;
	  }
	default:
	  success = ER_FAILED;
	  break;
	}

      if (success == NO_ERROR)
	{
	  success = xrepl_set_info (thread_p, &repl_info);

	  switch (repl_info.repl_info_type)
	    {
	    case REPL_INFO_TYPE_SCHEMA:
	      break;
	    default:
	      success = ER_FAILED;
	      break;
	    }
	}
    }

  (void) or_pack_int (reply, success);
  css_send_data_to_client (rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * srepl_log_get_append_lsa -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
srepl_log_get_append_lsa (unsigned int rid, char *request, int reqlen)
{
  OR_ALIGNED_BUF (OR_LOG_LSA_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  LOG_LSA *lsa;

  lsa = xrepl_log_get_append_lsa ();

  reply = OR_ALIGNED_BUF_START (a_reply);
  (void) or_pack_log_lsa (reply, lsa);

  css_send_data_to_client (rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));
}

/*
 * slocator_build_fk_object_cache -
 *
 * return:
 *
 *   rid(in):
 *   request(in):
 *   reqlen(in):
 *
 * NOTE:
 */
void
slocator_build_fk_object_cache (THREAD_ENTRY * thread_p, unsigned int rid,
				char *request, int reqlen)
{
  int error = NO_ERROR;
  OID class_oid;
  HFID hfid;
  OID pk_cls_oid;
  BTID pk_btid;
  int cache_attr_id;
  int n_attrs, *attr_ids;
  TP_DOMAIN *key_type;
  char *fk_name;
  char *ptr;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);

  ptr = or_unpack_oid (request, &class_oid);
  ptr = or_unpack_hfid (ptr, &hfid);
  ptr = or_unpack_domain (ptr, &key_type, 0);
  ptr = or_unpack_int (ptr, &n_attrs);
  ptr = or_unpack_int_array (ptr, n_attrs, &attr_ids);
  ptr = or_unpack_oid (ptr, &pk_cls_oid);
  ptr = or_unpack_btid (ptr, &pk_btid);
  ptr = PTR_ALIGN (ptr, OR_INT_SIZE);
  ptr = or_unpack_int (ptr, &cache_attr_id);
  ptr = or_unpack_string (ptr, &fk_name);

  if (xlocator_build_fk_object_cache (thread_p, &class_oid, &hfid, key_type,
				      n_attrs, attr_ids, &pk_cls_oid,
				      &pk_btid, cache_attr_id,
				      fk_name) != NO_ERROR)
    {
      if (er_errid () == ER_LK_UNILATERALLY_ABORTED)
	{
	  tran_server_unilaterally_abort_tran (thread_p);
	}
      return_error_to_client (rid);
      error = er_errid ();
    }

  ptr = or_pack_int (reply, error);
  css_send_data_to_client (rid, reply, OR_ALIGNED_BUF_SIZE (a_reply));

  db_private_free_and_init (thread_p, attr_ids);
  db_private_free_and_init (thread_p, fk_name);
}
