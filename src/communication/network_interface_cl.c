/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 */

/*
 * network_interface_cl.c - Interface functions for client requests.
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "porting.h"
#include "network.h"
#include "network_interface_cl.h"
#include "memory_alloc.h"
#include "storage_common.h"
#if defined(CS_MODE)
#include "server_interface.h"
#include "boot_cl.h"
#else /* !defined (CS_MODE) == defined (SA_MODE) */
#include "xserver_interface.h"
#include "boot_sr.h"
#include "locator_sr.h"
#include "query_executor.h"
#include "transaction_sr.h"
#include "jsp_sr.h"
#include "vacuum.h"
#include "serial.h"
#endif /* defined (SA_MODE) */
#include "oid.h"
#include "error_manager.h"
#include "object_representation.h"
#include "log_comm.h"
#include "log_writer.h"
#include "arithmetic.h"
#include "transaction_cl.h"
#include "language_support.h"
#include "statistics.h"
#include "system_parameter.h"
#include "replication.h"
#include "es.h"
#include "db.h"
#include "db_query.h"
#include "dbtype.h"
#if defined (SA_MODE)
#include "thread_manager.hpp"
#endif // SA_MODE

/*
 * Use db_clear_private_heap instead of db_destroy_private_heap
 */

#define NET_COPY_AREA_SENDRECV_SIZE (OR_INT_SIZE * 3)
#define NET_SENDRECV_BUFFSIZE (OR_INT_SIZE)

#if defined (CS_MODE)
#define NET_DEFER_END_QUERIES_MAX 5
static QUERY_ID net_Deferred_end_queries[NET_DEFER_END_QUERIES_MAX];
static int net_Deferred_end_queries_count = 0;
#endif /* CS_MODE */

/*
 * Flag to indicate whether we've crossed the client/server boundary.
 * It really only comes into play in standalone.
 */
unsigned int db_on_server = 0;

#if defined(CS_MODE)
static char *pack_const_string (char *buffer, const char *cstring);
static char *pack_string_with_null_padding (char *buffer, const char *stream, int len);
static int length_const_string (const char *cstring, int *strlen);
static int length_string_with_null_padding (int len);
#endif /* CS_MODE */
#if defined (SA_MODE)
static void enter_server_no_thread_entry (void);
static THREAD_ENTRY *enter_server (void);
static void exit_server_no_thread_entry (void);
static void exit_server (const THREAD_ENTRY & thread_ref);
#endif // SERVER_MODE

#if defined (SA_MODE)
//
// enter_server_no_thread_entry () - enter server mode without getting a thread entry (e.g. when "starting" server).
//
static void
enter_server_no_thread_entry (void)
{
  db_on_server++;
  er_stack_push_if_exists ();

  if (private_heap_id == 0)
    {
      assert (db_on_server == 1);
      private_heap_id = db_create_private_heap ();
    }
}

//
// enter_server () - start simulating server mode
//
// return : pointer to thread entry
//
static THREAD_ENTRY *
enter_server ()
{
  enter_server_no_thread_entry ();
  return thread_get_thread_entry_info ();
}

//
// exit_server_no_thread_entry () - exit server mode without getting a thread entry (e.g. when "starting" server).
//
static void
exit_server_no_thread_entry (void)
{
  if ((db_on_server - 1) == 0 && private_heap_id != 0)
    {
      db_clear_private_heap (NULL, private_heap_id);
    }
  er_restore_last_error ();
  db_on_server--;
}

//
// exit_server () - exit server mode simulation
//
// thread_ref (in) : reference to thread entry used to enter server mode
//
static void
exit_server (const THREAD_ENTRY & thread_ref)
{
  (void) thread_ref;		// not really used; just to force caller declare obtain thread entry

  exit_server_no_thread_entry ();
}
#endif // SA_MODE

#if defined(CS_MODE)
/*
 * pack_const_string -
 *
 * return:
 *
 *   buffer(in):
 *   cstring(in):
 *
 * NOTE:
 */
static char *
pack_const_string (char *buffer, const char *cstring)
{
  return or_pack_string (buffer, cstring);
}

/*
 * pack_string_with_null_padding - pack stream and add null.
 *                                 so stream is made as null terminated string.
 *
 * return:
 *
 *   buffer(in):
 *   stream(in):
 *   len(in):
 *
 * NOTE:
 */
static char *
pack_string_with_null_padding (char *buffer, const char *stream, int len)
{
  return or_pack_string_with_null_padding (buffer, stream, len);
}

/*
 * pack_const_string_with_length -
 *
 * return:
 *
 *   buffer(in):
 *   cstring(in):
 *   strlen(in):
 *
 * NOTE:
 */
static char *
pack_const_string_with_length (char *buffer, const char *cstring, int strlen)
{
  return or_pack_string_with_length (buffer, cstring, strlen);
}

/*
 * length_const_string -
 *
 * return:
 *
 *   cstring(in):
 *   strlen(out): strlen(cstring)
 */
static int
length_const_string (const char *cstring, int *strlen)
{
  return or_packed_string_length (cstring, strlen);
}


/*
 * length_string_with_null_padding - calculate length with null padding
 *
 * return:
 *
 *   len(in): stream length
 */
static int
length_string_with_null_padding (int len)
{
  return or_packed_stream_length (len + 1);	/* 1 for NULL padding */
}
#endif /* CS_MODE */

/*
 * locator_fetch -
 *
 * return:
 *
 *   oidp(in):
 *   chn(in):
 *   lock(in):
 *   fetch_type(in): fetch version type
 *   class_oid(in):
 *   class_chn(in):
 *   prefetch(in):
 *   fetch_copyarea(in):
 */
int
locator_fetch (OID * oidp, int chn, LOCK lock, LC_FETCH_VERSION_TYPE fetch_version_type, OID * class_oid, int class_chn,
	       int prefetch, LC_COPYAREA ** fetch_copyarea)
{
#if defined(CS_MODE)
  int success = ER_FAILED;
  int req_error;
  char *ptr;
  OR_ALIGNED_BUF ((OR_OID_SIZE * 2) + (OR_INT_SIZE * 5)) a_request;
  char *request;
  OR_ALIGNED_BUF (NET_COPY_AREA_SENDRECV_SIZE + OR_INT_SIZE) a_reply;
  char *reply;

  request = OR_ALIGNED_BUF_START (a_request);
  reply = OR_ALIGNED_BUF_START (a_reply);

  assert (oidp->volid >= 0 && oidp->pageid >= 0 && oidp->slotid >= 0);

  ptr = or_pack_oid (request, oidp);
  ptr = or_pack_int (ptr, chn);
  ptr = or_pack_lock (ptr, lock);
  ptr = or_pack_int (ptr, (int) fetch_version_type);
  ptr = or_pack_oid (ptr, class_oid);
  ptr = or_pack_int (ptr, class_chn);
  ptr = or_pack_int (ptr, prefetch);
  *fetch_copyarea = NULL;

  req_error =
    net_client_request_recv_copyarea (NET_SERVER_LC_FETCH, request, OR_ALIGNED_BUF_SIZE (a_request), reply,
				      OR_ALIGNED_BUF_SIZE (a_reply), fetch_copyarea);
  if (!req_error)
    {
      ptr = reply + NET_COPY_AREA_SENDRECV_SIZE;
      ptr = or_unpack_int (ptr, &success);
    }
  else
    {
      *fetch_copyarea = NULL;
    }

  return success;
#else /* CS_MODE */
  int success = ER_FAILED;

  THREAD_ENTRY *thread_p = enter_server ();

  success =
    xlocator_fetch (thread_p, oidp, chn, lock, fetch_version_type, fetch_version_type, class_oid, class_chn, prefetch,
		    fetch_copyarea);

  exit_server (*thread_p);

  return success;
#endif /* !CS_MODE */
}

/*
 * locator_get_class -
 *
 * return:
 *
 *   class_oid(in):
 *   class_chn(in):
 *   oid(in):
 *   lock(in):
 *   prefetching(in):
 *   fetch_copyarea(in):
 *
 * NOTE:
 */
int
locator_get_class (OID * class_oid, int class_chn, const OID * oid, LOCK lock, int prefetching,
		   LC_COPYAREA ** fetch_copyarea)
{
#if defined(CS_MODE)
  int req_error;
  char *ptr;
  int return_value = ER_FAILED;
  OR_ALIGNED_BUF ((OR_OID_SIZE * 2) + (OR_INT_SIZE * 3)) a_request;
  char *request;
  OR_ALIGNED_BUF (NET_COPY_AREA_SENDRECV_SIZE + OR_OID_SIZE + OR_INT_SIZE) a_reply;
  char *reply;

  request = OR_ALIGNED_BUF_START (a_request);
  reply = OR_ALIGNED_BUF_START (a_reply);

  ptr = or_pack_oid (request, class_oid);
  ptr = or_pack_int (ptr, class_chn);
  ptr = or_pack_oid (ptr, (OID *) oid);
  ptr = or_pack_lock (ptr, lock);
  ptr = or_pack_int (ptr, prefetching);
  *fetch_copyarea = NULL;

  req_error =
    net_client_request_recv_copyarea (NET_SERVER_LC_GET_CLASS, request, OR_ALIGNED_BUF_SIZE (a_request), reply,
				      OR_ALIGNED_BUF_SIZE (a_reply), fetch_copyarea);
  if (!req_error)
    {
      ptr = reply + NET_COPY_AREA_SENDRECV_SIZE;
      ptr = or_unpack_oid (ptr, class_oid);
      ptr = or_unpack_int (ptr, &return_value);
    }
  else
    {
      *fetch_copyarea = NULL;
    }

  return return_value;
#else /* CS_MODE */
  int success = ER_FAILED;

  THREAD_ENTRY *thread_p = enter_server ();

  success = xlocator_get_class (thread_p, class_oid, class_chn, oid, lock, prefetching, fetch_copyarea);

  exit_server (*thread_p);

  return success;
#endif /* !CS_MODE */
}

/*
 * locator_fetch_all -
 *
 * return:
 *
 *   hfid(in):
 *   lock(in):
 *   fetch_version_type(in): fetch version type
 *   class_oidp(in):
 *   nobjects(in):
 *   nfetched(in):
 *   last_oidp(in):
 *   fetch_copyarea(in):
 *
 * NOTE:
 */
int
locator_fetch_all (const HFID * hfid, LOCK * lock, LC_FETCH_VERSION_TYPE fetch_version_type, OID * class_oidp,
		   int *nobjects, int *nfetched, OID * last_oidp, LC_COPYAREA ** fetch_copyarea)
{
#if defined(CS_MODE)
  int req_error;
  char *ptr;
  int return_value = ER_FAILED;
  OR_ALIGNED_BUF (OR_HFID_SIZE + (OR_INT_SIZE * 4) + (OR_OID_SIZE * 2)) a_request;
  char *request;
  OR_ALIGNED_BUF (NET_COPY_AREA_SENDRECV_SIZE + (OR_INT_SIZE * 4) + OR_OID_SIZE) a_reply;
  char *reply;

  request = OR_ALIGNED_BUF_START (a_request);
  reply = OR_ALIGNED_BUF_START (a_reply);

  ptr = or_pack_hfid (request, hfid);
  ptr = or_pack_lock (ptr, *lock);
  ptr = or_pack_int (ptr, fetch_version_type);
  ptr = or_pack_oid (ptr, class_oidp);
  ptr = or_pack_int (ptr, *nobjects);
  ptr = or_pack_int (ptr, *nfetched);
  ptr = or_pack_oid (ptr, last_oidp);
  *fetch_copyarea = NULL;

  req_error =
    net_client_request_recv_copyarea (NET_SERVER_LC_FETCHALL, request, OR_ALIGNED_BUF_SIZE (a_request), reply,
				      OR_ALIGNED_BUF_SIZE (a_reply), fetch_copyarea);
  if (req_error == NO_ERROR)
    {
      ptr = reply + NET_COPY_AREA_SENDRECV_SIZE;
      ptr = or_unpack_lock (ptr, lock);
      ptr = or_unpack_int (ptr, nobjects);
      ptr = or_unpack_int (ptr, nfetched);
      ptr = or_unpack_oid (ptr, last_oidp);
      ptr = or_unpack_int (ptr, &return_value);
    }
  else
    {
      *fetch_copyarea = NULL;
    }

  return return_value;
#else /* CS_MODE */
  int success = ER_FAILED;

  THREAD_ENTRY *thread_p = enter_server ();

  success =
    xlocator_fetch_all (thread_p, hfid, lock, fetch_version_type, class_oidp, nobjects, nfetched, last_oidp,
			fetch_copyarea);

  exit_server (*thread_p);

  return success;
#endif /* !CS_MODE */
}

/*
 * locator_does_exist -
 *
 * return:
 *
 *   oidp(in):
 *   chn(in):
 *   lock(in):
 *   class_oid(in):
 *   class_chn(in):
 *   need_fetching(in):
 *   prefetch(in):
 *   fetch_copyarea(in):
 *
 * NOTE:
 */
int
locator_does_exist (OID * oidp, int chn, LOCK lock, OID * class_oid, int class_chn, int need_fetching, int prefetch,
		    LC_COPYAREA ** fetch_copyarea, LC_FETCH_VERSION_TYPE fetch_version_type)
{
#if defined(CS_MODE)
  int does_exist = LC_ERROR;
  int req_error;
  char *ptr;
  OR_ALIGNED_BUF ((OR_OID_SIZE * 2) + (OR_INT_SIZE * 6)) a_request;
  char *request;
  OR_ALIGNED_BUF (NET_COPY_AREA_SENDRECV_SIZE + OR_INT_SIZE + OR_OID_SIZE) a_reply;
  char *reply;
  OID class_;

  request = OR_ALIGNED_BUF_START (a_request);
  reply = OR_ALIGNED_BUF_START (a_reply);

  ptr = or_pack_oid (request, oidp);
  ptr = or_pack_int (ptr, chn);
  ptr = or_pack_lock (ptr, lock);
  ptr = or_pack_int (ptr, (int) fetch_version_type);
  ptr = or_pack_oid (ptr, class_oid);
  ptr = or_pack_int (ptr, class_chn);
  ptr = or_pack_int (ptr, need_fetching);
  ptr = or_pack_int (ptr, prefetch);

  if (fetch_copyarea != NULL && need_fetching)
    {
      *fetch_copyarea = NULL;
    }

  req_error =
    net_client_request_recv_copyarea (NET_SERVER_LC_DOESEXIST, request, OR_ALIGNED_BUF_SIZE (a_request), reply,
				      OR_ALIGNED_BUF_SIZE (a_reply), fetch_copyarea);
  if (!req_error)
    {
      ptr = reply + NET_COPY_AREA_SENDRECV_SIZE;
      ptr = or_unpack_int (ptr, &does_exist);
      ptr = or_unpack_oid (ptr, &class_);
      if (does_exist == LC_EXIST && class_oid)
	{
	  COPY_OID (class_oid, &class_);
	}
    }
  else if (fetch_copyarea != NULL)
    {
      *fetch_copyarea = NULL;
    }

  return does_exist;
#else /* CS_MODE */
  int success = ER_FAILED;

  THREAD_ENTRY *thread_p = enter_server ();

  success =
    xlocator_does_exist (thread_p, oidp, chn, lock, fetch_version_type, class_oid, class_chn, need_fetching, prefetch,
			 fetch_copyarea);

  exit_server (*thread_p);

  return success;
#endif /* !CS_MODE */
}

/*
 * locator_notify_isolation_incons -
 *
 * return:
 *
 *   synch_copyarea(in):
 *
 * NOTE:
 */
int
locator_notify_isolation_incons (LC_COPYAREA ** synch_copyarea)
{
#if defined(CS_MODE)
  int success = ER_FAILED;
  int req_error;
  char *ptr;
  OR_ALIGNED_BUF (NET_COPY_AREA_SENDRECV_SIZE + OR_INT_SIZE) a_reply;
  char *reply;

  reply = OR_ALIGNED_BUF_START (a_reply);

  *synch_copyarea = NULL;
  req_error =
    net_client_request_recv_copyarea (NET_SERVER_LC_NOTIFY_ISOLATION_INCONS, NULL, 0, reply,
				      OR_ALIGNED_BUF_SIZE (a_reply), synch_copyarea);

  if (!req_error)
    {
      ptr = reply + NET_COPY_AREA_SENDRECV_SIZE;
      ptr = or_unpack_int (ptr, &success);
    }
  else
    {
      *synch_copyarea = NULL;
    }

  return (success);
#else /* CS_MODE */
  int success = ER_FAILED;

  THREAD_ENTRY *thread_p = enter_server ();

  success = xlocator_notify_isolation_incons (thread_p, synch_copyarea);

  exit_server (*thread_p);

  return success;
#endif /* !CS_MODE */
}

/*
 * locator_repl_force - flush copy area containing replication objects and receive error occurred in server
 *
 * return:
 *
 *   copy_area(in):
 *   reply_copy_area(out):
 *
 * NOTE:
 */
int
locator_repl_force (LC_COPYAREA * copy_area, LC_COPYAREA ** reply_copy_area)
{
#if defined(CS_MODE)
  int error_code = ER_FAILED;
  OR_ALIGNED_BUF (NET_COPY_AREA_SENDRECV_SIZE) a_request;
  char *request;
  char *request_ptr;
  OR_ALIGNED_BUF (NET_COPY_AREA_SENDRECV_SIZE + OR_INT_SIZE) a_reply;
  char *reply;
  char *desc_ptr = NULL;
  int desc_size;
  char *content_ptr;
  int content_size;
  int num_objs = 0;
  int req_error;

  request = OR_ALIGNED_BUF_START (a_request);
  reply = OR_ALIGNED_BUF_START (a_reply);

  num_objs = locator_send_copy_area (copy_area, &content_ptr, &content_size, &desc_ptr, &desc_size);

  request_ptr = or_pack_int (request, num_objs);
  request_ptr = or_pack_int (request_ptr, desc_size);
  request_ptr = or_pack_int (request_ptr, content_size);

  req_error =
    net_client_request_3_data_recv_copyarea (NET_SERVER_LC_REPL_FORCE, request, NET_COPY_AREA_SENDRECV_SIZE, desc_ptr,
					     desc_size, content_ptr, content_size, reply, OR_ALIGNED_BUF_SIZE (a_reply),
					     reply_copy_area);

  if (req_error == NO_ERROR)
    {
      /* skip first 3 */
      (void) or_unpack_int (reply + NET_COPY_AREA_SENDRECV_SIZE, &error_code);
    }
  else
    {
      assert (er_errid () != NO_ERROR);
      error_code = er_errid ();
    }

  if (desc_ptr)
    {
      free_and_init (desc_ptr);
    }

  return error_code;
#else /* CS_MODE */
  /* Not allowed in SA_MODE */
  return ER_FAILED;
#endif /* !CS_MODE */
}

/*
 * locator_force -
 *
 * return:
 *
 *   copy_area(in):
 *
 * NOTE:
 */
int
locator_force (LC_COPYAREA * copy_area, int num_ignore_error_list, int *ignore_error_list, int content_size)
{
#if defined(CS_MODE)
  int error_code = ER_FAILED;
  char *request;
  char *request_ptr;
  int request_size;
  OR_ALIGNED_BUF (OR_INT_SIZE * 3) a_reply;
  char *reply;
  char *desc_ptr = NULL;
  int desc_size;
  char *content_ptr;
  int num_objs = 0;
  int req_error;
  int i;
  LC_COPYAREA_MANYOBJS *mobjs;

  mobjs = LC_MANYOBJS_PTR_IN_COPYAREA (copy_area);

  request_size = OR_INT_SIZE * (6 + num_ignore_error_list);
  request = (char *) malloc (request_size);

  if (request == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) request_size);
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  reply = OR_ALIGNED_BUF_START (a_reply);

  num_objs = locator_send_copy_area (copy_area, &content_ptr, NULL, &desc_ptr, &desc_size);

  request_ptr = or_pack_int (request, num_objs);
  request_ptr = or_pack_int (request_ptr, mobjs->start_multi_update);
  request_ptr = or_pack_int (request_ptr, mobjs->end_multi_update);
  request_ptr = or_pack_int (request_ptr, desc_size);
  request_ptr = or_pack_int (request_ptr, content_size);

  request_ptr = or_pack_int (request_ptr, num_ignore_error_list);
  for (i = 0; i < num_ignore_error_list; i++)
    {
      request_ptr = or_pack_int (request_ptr, ignore_error_list[i]);
    }

  req_error =
    net_client_request_3_data (NET_SERVER_LC_FORCE, request, request_size, desc_ptr, desc_size, content_ptr,
			       content_size, reply, OR_ALIGNED_BUF_SIZE (a_reply), desc_ptr, desc_size, NULL, 0);
  if (!req_error)
    {
      (void) or_unpack_int (reply, &error_code);
      if (error_code == NO_ERROR)
	{
	  locator_unpack_copy_area_descriptor (num_objs, copy_area, desc_ptr);
	}
    }
  if (desc_ptr)
    {
      free_and_init (desc_ptr);
    }
  if (request)
    {
      free_and_init (request);
    }

  return error_code;
#else /* CS_MODE */
  int error_code = ER_FAILED;
  LC_COPYAREA *copy_area_clone;

  /* If xlocator_force returns error, the original copy_area should not be changed. So copy_area_clone will be used. */
  copy_area_clone = locator_allocate_copy_area_by_length (copy_area->length);
  if (copy_area_clone == NULL)
    {
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  memcpy (copy_area_clone->mem, copy_area->mem, copy_area->length);

  THREAD_ENTRY *thread_p = enter_server ();

  error_code = xlocator_force (thread_p, copy_area, num_ignore_error_list, ignore_error_list);

  exit_server (*thread_p);

  if (error_code != NO_ERROR)
    {
      /* Restore copy_area */
      memcpy (copy_area->mem, copy_area_clone->mem, copy_area->length);
    }

  locator_free_copy_area (copy_area_clone);

  return error_code;
#endif /* !CS_MODE */
}

/*
 * locator_fetch_lockset -
 *
 * return:
 *
 *   lockset(in):
 *   fetch_copyarea(in):
 *
 * NOTE:
 */
int
locator_fetch_lockset (LC_LOCKSET * lockset, LC_COPYAREA ** fetch_copyarea)
{
#if defined(CS_MODE)
  int success = ER_FAILED;
  int req_error;
  char *ptr;
  OR_ALIGNED_BUF (NET_SENDRECV_BUFFSIZE + NET_COPY_AREA_SENDRECV_SIZE + OR_INT_SIZE) a_reply;
  char *reply;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_request;
  char *request;
  static int eid;
  char *packed = NULL;
  int packed_size;
  int send_size;

  request = OR_ALIGNED_BUF_START (a_request);
  reply = OR_ALIGNED_BUF_START (a_reply);

  if (lockset->first_fetch_lockset_call == true)
    {
      send_size = locator_pack_lockset (lockset, true, true);

      packed = lockset->packed;
      packed_size = lockset->packed_size;

      if (!packed)
	{
	  return ER_FAILED;
	}

      ptr = or_pack_int (request, send_size);

      req_error =
	net_client_request_2recv_copyarea (NET_SERVER_LC_FETCH_LOCKSET, request, OR_ALIGNED_BUF_SIZE (a_request), reply,
					   OR_ALIGNED_BUF_SIZE (a_reply), packed, send_size, packed, packed_size,
					   fetch_copyarea, &eid);
    }
  else
    {
      /* Don't need to send the lockset information any more */
      packed = lockset->packed;
      packed_size = lockset->packed_size;
      req_error =
	net_client_recv_copyarea (NET_SERVER_LC_FETCH_LOCKSET, reply, OR_ALIGNED_BUF_SIZE (a_reply), packed,
				  packed_size, fetch_copyarea, eid);
    }

  if (!req_error)
    {
      ptr = reply + NET_SENDRECV_BUFFSIZE + NET_COPY_AREA_SENDRECV_SIZE;
      ptr = or_unpack_int (ptr, &success);
      if (success == NO_ERROR)
	{
	  locator_unpack_lockset (lockset, lockset->first_fetch_lockset_call, false);
	}
    }
  else
    {
      *fetch_copyarea = NULL;
    }

  /* 
   * We will not need to send the lockset structure any more. We do not
   * need to receive the classes and objects in the lockset structure
   * any longer
   */
  lockset->first_fetch_lockset_call = false;

  return success;
#else /* CS_MODE */
  int success = ER_FAILED;

  THREAD_ENTRY *thread_p = enter_server ();

  success = xlocator_fetch_lockset (thread_p, lockset, fetch_copyarea);

  exit_server (*thread_p);

  return success;
#endif /* !CS_MODE */
}

/*
 * locator_fetch_all_reference_lockset -
 *
 * return:
 *
 *   oid(in):
 *   chn(in):
 *   class_oid(in):
 *   class_chn(in):
 *   lock(in):
 *   quit_on_errors(in):
 *   prune_level(in):
 *   lockset(in):
 *   fetch_copyarea(in):
 *
 * NOTE:
 */
int
locator_fetch_all_reference_lockset (OID * oid, int chn, OID * class_oid, int class_chn, LOCK lock, int quit_on_errors,
				     int prune_level, LC_LOCKSET ** lockset, LC_COPYAREA ** fetch_copyarea)
{
#if defined(CS_MODE)
  int success = ER_FAILED;
  int req_error;
  char *ptr;
  OR_ALIGNED_BUF ((OR_OID_SIZE * 2) + (OR_INT_SIZE * 5)) a_request;
  char *request;
  OR_ALIGNED_BUF (NET_SENDRECV_BUFFSIZE + NET_COPY_AREA_SENDRECV_SIZE + OR_INT_SIZE) a_reply;
  char *reply;
  char *packed;
  int packed_size;

  request = OR_ALIGNED_BUF_START (a_request);
  reply = OR_ALIGNED_BUF_START (a_reply);

  *fetch_copyarea = NULL;
  *lockset = NULL;

  ptr = or_pack_oid (request, oid);
  ptr = or_pack_int (ptr, chn);
  ptr = or_pack_oid (ptr, class_oid);
  ptr = or_pack_int (ptr, class_chn);
  ptr = or_pack_lock (ptr, lock);
  ptr = or_pack_int (ptr, quit_on_errors);
  ptr = or_pack_int (ptr, prune_level);

  req_error =
    net_client_request_3recv_copyarea (NET_SERVER_LC_FETCH_ALLREFS_LOCKSET, request, OR_ALIGNED_BUF_SIZE (a_request),
				       reply, OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0, &packed, &packed_size,
				       fetch_copyarea);

  if (!req_error)
    {
      ptr = reply + NET_SENDRECV_BUFFSIZE + NET_COPY_AREA_SENDRECV_SIZE;
      ptr = or_unpack_int (ptr, &success);
      if (packed_size > 0 && packed != NULL)
	{
	  *lockset = locator_allocate_and_unpack_lockset (packed, packed_size, true, true, false);
	}
      else
	{
	  *lockset = NULL;
	}

      if (packed)
	{
	  free_and_init (packed);
	}
    }
  else
    {
      *fetch_copyarea = NULL;
      *lockset = NULL;
    }

  return success;

#else /* CS_MODE */
  int success = ER_FAILED;

  THREAD_ENTRY *thread_p = enter_server ();

  success =
    xlocator_fetch_all_reference_lockset (thread_p, oid, chn, class_oid, class_chn, lock, quit_on_errors, prune_level,
					  lockset, fetch_copyarea);

  exit_server (*thread_p);

  return success;
#endif /* !CS_MODE */
}

/*
 * locator_find_class_oid -
 *
 * return:
 *
 *   class_name(in):
 *   class_oid(in):
 *   lock(in):
 *
 * NOTE:
 */
LC_FIND_CLASSNAME
locator_find_class_oid (const char *class_name, OID * class_oid, LOCK lock)
{
#if defined(CS_MODE)
  LC_FIND_CLASSNAME found = LC_CLASSNAME_ERROR;
  int xfound;
  int req_error;
  char *ptr;
  int request_size, strlen;
  char *request;
  OR_ALIGNED_BUF (OR_INT_SIZE + OR_OID_SIZE) a_reply;
  char *reply;

  reply = OR_ALIGNED_BUF_START (a_reply);

  request_size = length_const_string (class_name, &strlen) + OR_OID_SIZE + OR_INT_SIZE;
  request = (char *) malloc (request_size);
  if (request == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) request_size);
      return LC_CLASSNAME_ERROR;
    }

  ptr = pack_const_string_with_length (request, class_name, strlen);
  ptr = or_pack_oid (ptr, class_oid);
  ptr = or_pack_lock (ptr, lock);

  req_error = net_client_request (NET_SERVER_LC_FIND_CLASSOID, request, request_size, reply,
				  OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0, NULL, 0);
  if (!req_error)
    {
      ptr = or_unpack_int (reply, &xfound);
      found = (LC_FIND_CLASSNAME) xfound;
      ptr = or_unpack_oid (ptr, class_oid);
    }
  free_and_init (request);

  return found;
#else /* CS_MODE */
  LC_FIND_CLASSNAME found = LC_CLASSNAME_ERROR;

  THREAD_ENTRY *thread_p = enter_server ();

  found = xlocator_find_class_oid (thread_p, class_name, class_oid, lock);

  exit_server (*thread_p);

  return found;
#endif /* !CS_MODE */
}

/*
 * locator_reserve_class_names -
 *
 * return:
 *
 *   num_classes(in)
 *   class_names(in):
 *   class_oids(in):
 *
 * NOTE:
 */
LC_FIND_CLASSNAME
locator_reserve_class_names (const int num_classes, const char **class_names, OID * class_oids)
{
#if defined(CS_MODE)
  LC_FIND_CLASSNAME reserved = LC_CLASSNAME_ERROR;
  int xreserved;
  int request_size;
  int req_error;
  char *request, *ptr;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply;
  int i;

  reply = OR_ALIGNED_BUF_START (a_reply);

  request_size = OR_INT_SIZE;
  for (i = 0; i < num_classes; ++i)
    {
      request_size += length_const_string (class_names[i], NULL) + OR_OID_SIZE;
    }
  request = (char *) malloc (request_size);
  if (request == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) request_size);
      return LC_CLASSNAME_ERROR;
    }

  ptr = or_pack_int (request, num_classes);
  for (i = 0; i < num_classes; ++i)
    {
      ptr = pack_const_string (ptr, class_names[i]);
      ptr = or_pack_oid (ptr, &class_oids[i]);
    }

  req_error = net_client_request (NET_SERVER_LC_RESERVE_CLASSNAME, request, request_size, reply,
				  OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0, NULL, 0);
  if (!req_error)
    {
      (void) or_unpack_int (reply, &xreserved);
      reserved = (LC_FIND_CLASSNAME) xreserved;
    }

  free_and_init (request);

  return reserved;
#else /* CS_MODE */
  LC_FIND_CLASSNAME reserved = LC_CLASSNAME_ERROR;

  THREAD_ENTRY *thread_p = enter_server ();

  reserved = xlocator_reserve_class_names (thread_p, num_classes, class_names, class_oids);

  exit_server (*thread_p);

  return reserved;
#endif /* !CS_MODE */
}

/*
 * locator_get_reserved_class_name_oid () - Get OID of reserved class.
 *
 * return	   : Error code.
 * classname (in)  : Class name.
 * class_oid (out) : Class OID.
 */
int
locator_get_reserved_class_name_oid (const char *classname, OID * class_oid)
{
#if defined(CS_MODE)
  int request_size;
  int request_error = NO_ERROR;
  char *request = NULL;
  OR_ALIGNED_BUF (OR_OID_SIZE) a_reply;
  char *reply = NULL;

  assert (classname != NULL);
  assert (class_oid != NULL);

  OID_SET_NULL (class_oid);

  reply = OR_ALIGNED_BUF_START (a_reply);

  request_size = length_const_string (classname, NULL);
  request = (char *) malloc (request_size);
  if (request == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, request_size);
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  (void) pack_const_string (request, classname);

  request_error = net_client_request (NET_SERVER_LC_RESERVE_CLASSNAME_GET_OID, request, request_size, reply,
				      OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0, NULL, 0);
  free_and_init (request);

  if (request_error != NO_ERROR)
    {
      return request_error;
    }

  (void) or_unpack_oid (reply, class_oid);

  return NO_ERROR;
#else
  int is_reserved;

  THREAD_ENTRY *thread_p = enter_server ();

  is_reserved = xlocator_get_reserved_class_name_oid (thread_p, classname, class_oid);

  exit_server (*thread_p);

  return is_reserved;
#endif
}

/*
 * locator_delete_class_name -
 *
 * return:
 *
 *   class_name(in):
 *
 * NOTE:
 */
LC_FIND_CLASSNAME
locator_delete_class_name (const char *class_name)
{
#if defined(CS_MODE)
  LC_FIND_CLASSNAME deleted = LC_CLASSNAME_ERROR;
  int xdeleted;
  int req_error, request_size, strlen;
  char *request;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply;

  reply = OR_ALIGNED_BUF_START (a_reply);

  request_size = length_const_string (class_name, &strlen);
  request = (char *) malloc (request_size);
  if (request == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) request_size);
      return LC_CLASSNAME_ERROR;
    }

  (void) pack_const_string_with_length (request, class_name, strlen);
  req_error = net_client_request (NET_SERVER_LC_DELETE_CLASSNAME, request, request_size, reply,
				  OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0, NULL, 0);
  if (!req_error)
    {
      or_unpack_int (reply, &xdeleted);
      deleted = (LC_FIND_CLASSNAME) xdeleted;
    }

  free_and_init (request);

  return deleted;
#else /* CS_MODE */
  LC_FIND_CLASSNAME deleted = LC_CLASSNAME_ERROR;

  THREAD_ENTRY *thread_p = enter_server ();

  deleted = xlocator_delete_class_name (thread_p, class_name);

  exit_server (*thread_p);

  return deleted;
#endif /* !CS_MODE */
}

/*
 * locator_rename_class_name -
 *
 * return:
 *
 *   old_name(in):
 *   new_name(in):
 *   class_oid(in):
 *
 * NOTE:
 */
LC_FIND_CLASSNAME
locator_rename_class_name (const char *old_name, const char *new_name, OID * class_oid)
{
#if defined(CS_MODE)
  LC_FIND_CLASSNAME renamed = LC_CLASSNAME_ERROR;
  int xrenamed;
  int request_size, strlen1, strlen2;
  int req_error;
  char *request, *ptr;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply;

  reply = OR_ALIGNED_BUF_START (a_reply);

  request_size = length_const_string (old_name, &strlen1) + length_const_string (new_name, &strlen2) + OR_OID_SIZE;
  request = (char *) malloc (request_size);
  if (request == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) request_size);
      return LC_CLASSNAME_ERROR;
    }

  ptr = pack_const_string_with_length (request, old_name, strlen1);
  ptr = pack_const_string_with_length (ptr, new_name, strlen2);
  ptr = or_pack_oid (ptr, class_oid);

  req_error = net_client_request (NET_SERVER_LC_RENAME_CLASSNAME, request, request_size, reply,
				  OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0, NULL, 0);
  if (!req_error)
    {
      ptr = or_unpack_int (reply, &xrenamed);
      renamed = (LC_FIND_CLASSNAME) xrenamed;
    }
  free_and_init (request);

  return renamed;
#else /* CS_MODE */
  LC_FIND_CLASSNAME renamed = LC_CLASSNAME_ERROR;

  THREAD_ENTRY *thread_p = enter_server ();

  renamed = xlocator_rename_class_name (thread_p, old_name, new_name, class_oid);

  exit_server (*thread_p);

  return renamed;
#endif /* !CS_MODE */
}

/*
 * locator_assign_oid -
 *
 * return:
 *
 *   hfid(in):
 *   perm_oid(in):
 *   expected_length(in):
 *   class_oid(in):
 *   class_name(in):
 *
 * NOTE:
 */
int
locator_assign_oid (const HFID * hfid, OID * perm_oid, int expected_length, OID * class_oid, const char *class_name)
{
#if defined(CS_MODE)
  int success = ER_FAILED;
  int request_size, strlen;
  int req_error;
  char *request, *ptr;
  OR_ALIGNED_BUF (OR_OID_SIZE + OR_INT_SIZE) a_reply;
  char *reply;

  reply = OR_ALIGNED_BUF_START (a_reply);

  request_size = OR_HFID_SIZE + OR_INT_SIZE + OR_OID_SIZE + length_const_string (class_name, &strlen);
  request = (char *) malloc (request_size);
  if (request == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) request_size);
      return ER_FAILED;
    }

  ptr = or_pack_hfid (request, hfid);
  ptr = or_pack_int (ptr, expected_length);
  ptr = or_pack_oid (ptr, class_oid);
  ptr = pack_const_string_with_length (ptr, class_name, strlen);

  req_error = net_client_request (NET_SERVER_LC_ASSIGN_OID, request, request_size, reply,
				  OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0, NULL, 0);
  if (!req_error)
    {
      ptr = or_unpack_int (reply, &success);
      ptr = or_unpack_oid (ptr, perm_oid);
    }
  free_and_init (request);

  return success;
#else /* CS_MODE */
  int success = ER_FAILED;

  THREAD_ENTRY *thread_p = enter_server ();

  success = xlocator_assign_oid (thread_p, hfid, perm_oid, expected_length, class_oid, class_name);

  exit_server (*thread_p);

  return success;
#endif /* !CS_MODE */
}

/*
 * locator_assign_oid_batch -
 *
 * return:
 *
 *   oidset(in):
 *
 * NOTE:
 */
int
locator_assign_oid_batch (LC_OIDSET * oidset)
{
#if defined(CS_MODE)
  int success = ER_FAILED;
  int packed_size;
  char *buffer, *ptr;
  int req_error;

  /* 
   * Build a buffer in which to send and receive the goobers.  We'll
   * reuse the same buffer to receive the data as we used to send it.
   * First word is reserved for the return code.
   */
  packed_size = locator_get_packed_oid_set_size (oidset) + OR_INT_SIZE;
  buffer = (char *) malloc (packed_size);
  if (buffer == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) packed_size);
      return ER_FAILED;
    }

  ptr = buffer;
  ptr = or_pack_int (ptr, 0);

  if (locator_pack_oid_set (ptr, oidset) == NULL)
    {
      free_and_init (buffer);
      return ER_FAILED;
    }

  req_error =
    net_client_request (NET_SERVER_LC_ASSIGN_OID_BATCH, buffer, packed_size, buffer, packed_size, NULL, 0, NULL, 0);

  if (!req_error)
    {
      ptr = buffer;
      ptr = or_unpack_int (ptr, &success);
      if (success == NO_ERROR)
	{
	  if (locator_unpack_oid_set_to_exist (ptr, oidset) == false)
	    {
	      success = ER_FAILED;
	    }
	}
    }

  free_and_init (buffer);

  return success;
#else /* CS_MODE */
  int success = ER_FAILED;

  THREAD_ENTRY *thread_p = enter_server ();

  success = xlocator_assign_oid_batch (thread_p, oidset);

  exit_server (*thread_p);

  return (success);
#endif /* !CS_MODE */
}

/*
 * locator_find_lockhint_class_oids -
 *
 * return:
 *
 *   num_classes(in):
 *   many_classnames(in):
 *   many_locks(in):
 *   many_need_subclasses(in):
 *   many_flags(in):
 *   guessed_class_oids(in):
 *   guessed_class_chns(in):
 *   quit_on_errors(in):
 *   lock_rr_tran(in):
 *   lockhint(in):
 *   fetch_copyarea(in):
 *
 * NOTE:
 */
LC_FIND_CLASSNAME
locator_find_lockhint_class_oids (int num_classes, const char **many_classnames, LOCK * many_locks,
				  int *many_need_subclasses, LC_PREFETCH_FLAGS * many_flags, OID * guessed_class_oids,
				  int *guessed_class_chns, int quit_on_errors, LOCK lock_rr_tran,
				  LC_LOCKHINT ** lockhint, LC_COPYAREA ** fetch_copyarea)
{
#if defined(CS_MODE)
  LC_FIND_CLASSNAME allfind = LC_CLASSNAME_ERROR;
  int xallfind;
  int req_error;
  char *ptr;
  int request_size;
  char *request, *packed;
  int packed_size;
  OR_ALIGNED_BUF (NET_SENDRECV_BUFFSIZE + NET_COPY_AREA_SENDRECV_SIZE + OR_INT_SIZE) a_reply;
  char *reply;
  int i;

  reply = OR_ALIGNED_BUF_START (a_reply);

  *lockhint = NULL;
  *fetch_copyarea = NULL;

  request_size = OR_INT_SIZE + OR_INT_SIZE + OR_INT_SIZE;
  for (i = 0; i < num_classes; i++)
    {
      request_size += (length_const_string (many_classnames[i], NULL) + OR_INT_SIZE + OR_INT_SIZE + OR_INT_SIZE
		       + OR_OID_SIZE + OR_INT_SIZE);
    }

  request = (char *) malloc (request_size);
  if (request == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) request_size);
      return allfind;
    }

  ptr = or_pack_int (request, num_classes);
  ptr = or_pack_int (ptr, quit_on_errors);
  ptr = or_pack_int (ptr, (int) lock_rr_tran);
  for (i = 0; i < num_classes; i++)
    {
      ptr = pack_const_string (ptr, many_classnames[i]);
      ptr = or_pack_lock (ptr, many_locks[i]);
      ptr = or_pack_int (ptr, many_need_subclasses[i]);
      ptr = or_pack_int (ptr, (int) many_flags[i]);
      ptr = or_pack_oid (ptr, &guessed_class_oids[i]);
      ptr = or_pack_int (ptr, guessed_class_chns[i]);
    }

  req_error =
    net_client_request_3recv_copyarea (NET_SERVER_LC_FIND_LOCKHINT_CLASSOIDS, request, request_size, reply,
				       OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0, &packed, &packed_size, fetch_copyarea);

  if (!req_error)
    {
      ptr = reply + NET_SENDRECV_BUFFSIZE + NET_COPY_AREA_SENDRECV_SIZE;
      ptr = or_unpack_int (ptr, &xallfind);
      allfind = (LC_FIND_CLASSNAME) xallfind;

      if (packed_size > 0 && packed != NULL)
	{
	  *lockhint = locator_allocate_and_unpack_lockhint (packed, packed_size, true, false);
	}
      else
	{
	  *lockhint = NULL;
	}

      if (packed)
	{
	  free_and_init (packed);
	}
    }
  else
    {
      *lockhint = NULL;
      *fetch_copyarea = NULL;
    }

  if (request != NULL)
    {
      free_and_init (request);
    }

  tm_Tran_rep_read_lock = lock_rr_tran;

  return allfind;
#else /* CS_MODE */
  LC_FIND_CLASSNAME allfind = LC_CLASSNAME_ERROR;

  THREAD_ENTRY *thread_p = enter_server ();

  allfind =
    xlocator_find_lockhint_class_oids (thread_p, num_classes, many_classnames, many_locks, many_need_subclasses,
				       many_flags, guessed_class_oids, guessed_class_chns, quit_on_errors, lockhint,
				       fetch_copyarea);

  exit_server (*thread_p);

  return allfind;
#endif /* !CS_MODE */
}

/*
 * locator_fetch_lockhint_classes -
 *
 * return:
 *
 *   lockhint(in):
 *   fetch_copyarea(in):
 *
 * NOTE:
 */
int
locator_fetch_lockhint_classes (LC_LOCKHINT * lockhint, LC_COPYAREA ** fetch_copyarea)
{
#if defined(CS_MODE)
  static int eid;		/* TODO: remove static */
  int success = ER_FAILED;
  int req_error;
  char *ptr;
  OR_ALIGNED_BUF (NET_SENDRECV_BUFFSIZE + NET_COPY_AREA_SENDRECV_SIZE + OR_INT_SIZE) a_reply;
  char *reply;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_request;
  char *request;
  char *packed = NULL;
  int packed_size;
  int send_size;

  request = OR_ALIGNED_BUF_START (a_request);
  reply = OR_ALIGNED_BUF_START (a_reply);

  *fetch_copyarea = NULL;

  if (lockhint->first_fetch_lockhint_call == true)
    {
      send_size = locator_pack_lockhint (lockhint, true);

      packed = lockhint->packed;
      packed_size = lockhint->packed_size;

      if (!packed)
	{
	  return ER_FAILED;
	}

      ptr = or_pack_int (request, send_size);

      req_error =
	net_client_request_2recv_copyarea (NET_SERVER_LC_FETCH_LOCKHINT_CLASSES, request,
					   OR_ALIGNED_BUF_SIZE (a_request), reply, OR_ALIGNED_BUF_SIZE (a_reply),
					   packed, send_size, packed, packed_size, fetch_copyarea, &eid);
    }
  else
    {
      /* Don't need to send the lockhint information any more */
      packed = lockhint->packed;
      packed_size = lockhint->packed_size;
      req_error =
	net_client_recv_copyarea (NET_SERVER_LC_FETCH_LOCKHINT_CLASSES, reply, OR_ALIGNED_BUF_SIZE (a_reply), packed,
				  packed_size, fetch_copyarea, eid);
    }

  if (!req_error)
    {
      ptr = reply + NET_SENDRECV_BUFFSIZE + NET_COPY_AREA_SENDRECV_SIZE;
      ptr = or_unpack_int (ptr, &success);
      if (success == NO_ERROR)
	{
	  locator_unpack_lockhint (lockhint, lockhint->first_fetch_lockhint_call);
	}
    }
  else
    {
      *fetch_copyarea = NULL;
    }

  lockhint->first_fetch_lockhint_call = false;

  return success;

#else /* CS_MODE */
  int success = ER_FAILED;

  THREAD_ENTRY *thread_p = enter_server ();

  success = xlocator_fetch_lockhint_classes (thread_p, lockhint, fetch_copyarea);

  exit_server (*thread_p);

  return success;
#endif /* !CS_MODE */
}

/*
 * heap_create -
 *
 * return:
 *
 *   hfid(in):
 *   class_oid(in):
 *   reuse_oid(in):
 *
 * NOTE:
 */
int
heap_create (HFID * hfid, const OID * class_oid, bool reuse_oid)
{
#if defined(CS_MODE)
  int error = ER_NET_CLIENT_DATA_RECEIVE;
  int req_error;
  char *ptr;
  OR_ALIGNED_BUF (OR_HFID_SIZE + OR_OID_SIZE + OR_INT_SIZE) a_request;
  char *request;
  OR_ALIGNED_BUF (OR_INT_SIZE + OR_HFID_SIZE) a_reply;
  char *reply;

  request = OR_ALIGNED_BUF_START (a_request);
  reply = OR_ALIGNED_BUF_START (a_reply);

  ptr = or_pack_hfid (request, hfid);
  ptr = or_pack_oid (ptr, (OID *) class_oid);
  ptr = or_pack_int (ptr, (int) reuse_oid);
  req_error =
    net_client_request (NET_SERVER_HEAP_CREATE, request, OR_ALIGNED_BUF_SIZE (a_request), reply,
			OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0, NULL, 0);
  if (!req_error)
    {
      ptr = or_unpack_errcode (reply, &error);
      ptr = or_unpack_hfid (ptr, hfid);
    }

  return error;
#else /* CS_MODE */
  int success;

  THREAD_ENTRY *thread_p = enter_server ();

  success = xheap_create (thread_p, hfid, class_oid, reuse_oid);

  exit_server (*thread_p);

  return success;
#endif /* !CS_MODE */
}

#if defined(ENABLE_UNUSED_FUNCTION)
/*
 * heap_destroy -
 *
 * return:
 *
 *   hfid(in):
 *
 * NOTE:
 */
int
heap_destroy (const HFID * hfid)
{
#if defined(CS_MODE)
  int error = ER_NET_CLIENT_DATA_RECEIVE;
  int req_error;
  char *ptr;
  OR_ALIGNED_BUF (OR_HFID_SIZE) a_request;
  char *request;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply;

  request = OR_ALIGNED_BUF_START (a_request);
  reply = OR_ALIGNED_BUF_START (a_reply);

  (void) or_pack_hfid (request, hfid);

  req_error =
    net_client_request (NET_SERVER_HEAP_DESTROY, request, OR_ALIGNED_BUF_SIZE (a_request), reply,
			OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0, NULL, 0);
  if (!req_error)
    {
      ptr = or_unpack_errcode (reply, &error);
    }

  return error;
#else /* CS_MODE */
  int success;

  THREAD_ENTRY *thread_p = enter_server ();

  success = xheap_destroy (thread_p, hfid, NULL);

  exit_server (*thread_p);

  return success;
#endif /* !CS_MODE */
}
#endif /* ENABLE_UNUSED_FUNCTION */

/*
 * heap_destroy_newly_created -
 *
 * return:
 *
 *   hfid(in):
 *
 * NOTE:
 */
int
heap_destroy_newly_created (const HFID * hfid, const OID * class_oid)
{
#if defined(CS_MODE)
  int error = ER_NET_CLIENT_DATA_RECEIVE;
  int req_error;
  char *ptr;
  OR_ALIGNED_BUF (OR_HFID_SIZE + OR_OID_SIZE) a_request;
  char *request;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply;

  request = OR_ALIGNED_BUF_START (a_request);
  reply = OR_ALIGNED_BUF_START (a_reply);

  ptr = or_pack_hfid (request, hfid);
  ptr = or_pack_oid (ptr, class_oid);

  req_error =
    net_client_request (NET_SERVER_HEAP_DESTROY_WHEN_NEW, request, OR_ALIGNED_BUF_SIZE (a_request), reply,
			OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0, NULL, 0);
  if (!req_error)
    {
      ptr = or_unpack_errcode (reply, &error);
    }

  return error;
#else /* CS_MODE */
  int success = ER_FAILED;

  THREAD_ENTRY *thread_p = enter_server ();

  success = xheap_destroy_newly_created (thread_p, hfid, class_oid);

  exit_server (*thread_p);

  return success;
#endif /* !CS_MODE */
}

/*
 * heap_reclaim_addresses -
 *
 * return:
 *
 *   hfid(in):
 *   reclaim_mvcc_next_versions(in):
 *
 * NOTE:
 */
int
heap_reclaim_addresses (const HFID * hfid)
{
#if defined(CS_MODE)
  int error = ER_NET_CLIENT_DATA_RECEIVE;
  int req_error;
  char *ptr;
  OR_ALIGNED_BUF (OR_HFID_SIZE + OR_INT_SIZE) a_request;
  char *request;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply;

  request = OR_ALIGNED_BUF_START (a_request);
  reply = OR_ALIGNED_BUF_START (a_reply);

  ptr = or_pack_hfid (request, hfid);

  req_error =
    net_client_request (NET_SERVER_HEAP_RECLAIM_ADDRESSES, request, OR_ALIGNED_BUF_SIZE (a_request), reply,
			OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0, NULL, 0);
  if (!req_error)
    {
      ptr = or_unpack_errcode (reply, &error);
    }

  return error;
#else /* CS_MODE */
  int success = ER_FAILED;

  THREAD_ENTRY *thread_p = enter_server ();

  success = xheap_reclaim_addresses (thread_p, hfid);

  exit_server (*thread_p);

  return success;
#endif /* !CS_MODE */
}

/*
 * disk_get_total_numpages -
 *
 * return:
 *
 *   volid(in):
 *
 * NOTE:
 */
DKNPAGES
disk_get_total_numpages (VOLID volid)
{
#if defined(CS_MODE)
  DKNPAGES npages = 0;
  int req_error;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_request;
  char *request;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply;

  request = OR_ALIGNED_BUF_START (a_request);
  reply = OR_ALIGNED_BUF_START (a_reply);

  (void) or_pack_int (request, (int) volid);

  req_error =
    net_client_request (NET_SERVER_DISK_TOTALPGS, request, OR_ALIGNED_BUF_SIZE (a_request), reply,
			OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0, NULL, 0);
  if (!req_error)
    {
      (void) or_unpack_int (reply, &npages);
    }

  return npages;
#else /* CS_MODE */
  DKNPAGES npages = 0;

  THREAD_ENTRY *thread_p = enter_server ();

  npages = xdisk_get_total_numpages (thread_p, volid);

  exit_server (*thread_p);

  return npages;
#endif /* !CS_MODE */
}

/*
 * disk_get_free_numpages -
 *
 * return:
 *
 *   volid(in):
 *
 * NOTE:
 */
DKNPAGES
disk_get_free_numpages (VOLID volid)
{
#if defined(CS_MODE)
  DKNPAGES npages = 0;
  int req_error;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_request;
  char *request;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply;

  request = OR_ALIGNED_BUF_START (a_request);
  reply = OR_ALIGNED_BUF_START (a_reply);

  (void) or_pack_int (request, (int) volid);

  req_error =
    net_client_request (NET_SERVER_DISK_FREEPGS, request, OR_ALIGNED_BUF_SIZE (a_request), reply,
			OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0, NULL, 0);
  if (!req_error)
    {
      (void) or_unpack_int (reply, &npages);
    }

  return npages;

#else /* CS_MODE */
  DKNPAGES npages = 0;

  THREAD_ENTRY *thread_p = enter_server ();

  npages = xdisk_get_free_numpages (thread_p, volid);

  exit_server (*thread_p);

  return npages;
#endif /* !CS_MODE */
}

/*
 * disk_get_remarks -
 *
 * return:
 *
 *   volid(in):
 *
 * NOTE:
 */
char *
disk_get_remarks (VOLID volid)
{
#if defined(CS_MODE)
  int req_error;
  char *area = NULL;
  int area_size;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_request;
  char *request;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply;
  char *remark = NULL, *p = NULL;

  request = OR_ALIGNED_BUF_START (a_request);
  reply = OR_ALIGNED_BUF_START (a_reply);

  (void) or_pack_int (request, (int) volid);

  req_error =
    net_client_request2 (NET_SERVER_DISK_REMARKS, request, OR_ALIGNED_BUF_SIZE (a_request), reply,
			 OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0, &area, &area_size);
  if (!req_error && area != NULL)
    {
      or_unpack_int (reply, &area_size);
      or_unpack_string_nocopy (area, &p);
      if (p != NULL)
	{
	  remark = strdup (p);
	}
      free_and_init (area);
    }

  return remark;
#else /* CS_MODE */
  char *remark = NULL;

  THREAD_ENTRY *thread_p = enter_server ();

  remark = xdisk_get_remarks (thread_p, volid);

  exit_server (*thread_p);

  return remark;
#endif /* !CS_MODE */
}

/*
 * disk_get_fullname -
 *
 * return:
 *
 *   volid(in):
 *   vol_fullname(in):
 *
 * NOTE:
 */
char *
disk_get_fullname (VOLID volid, char *vol_fullname)
{
#if defined(CS_MODE)
  int req_error;
  char *area = NULL;
  int area_size;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_request;
  char *request;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply;
  char *name = NULL;

  request = OR_ALIGNED_BUF_START (a_request);
  reply = OR_ALIGNED_BUF_START (a_reply);

  (void) or_pack_int (request, (int) volid);

  req_error =
    net_client_request2 (NET_SERVER_DISK_VLABEL, request, OR_ALIGNED_BUF_SIZE (a_request), reply,
			 OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0, &area, &area_size);
  if (!req_error && area != NULL)
    {
      or_unpack_int (reply, &area_size);
      or_unpack_string_nocopy (area, &name);
      if (name != NULL)
	{
	  strcpy (vol_fullname, name);
	}
      free_and_init (area);
    }
  else
    {
      vol_fullname = NULL;
    }

  return vol_fullname;
#else /* CS_MODE */

  THREAD_ENTRY *thread_p = enter_server ();

  vol_fullname = xdisk_get_fullname (thread_p, volid, vol_fullname);

  exit_server (*thread_p);

  return vol_fullname;
#endif /* !CS_MODE */
}

/*
 * log_reset_wait_msecs -
 *
 * return:
 *
 *   wait_msecs(in):    in milliseconds
 *
 * NOTE:
 */
int
log_reset_wait_msecs (int wait_msecs)
{
#if defined(CS_MODE)
  int wait = -1;
  int req_error;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_request;
  char *request;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply;

  request = OR_ALIGNED_BUF_START (a_request);
  reply = OR_ALIGNED_BUF_START (a_reply);

  (void) or_pack_int (request, wait_msecs);

  req_error =
    net_client_request (NET_SERVER_LOG_RESET_WAIT_MSECS, request, OR_ALIGNED_BUF_SIZE (a_request), reply,
			OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0, NULL, 0);
  if (!req_error)
    {
      (void) or_unpack_int (reply, &wait);
    }

  return wait;
#else /* CS_MODE */
  int wait = -1;

  THREAD_ENTRY *thread_p = enter_server ();

  wait = xlogtb_reset_wait_msecs (thread_p, wait_msecs);

  exit_server (*thread_p);

  return wait;
#endif /* !CS_MODE */
}

/*
 * log_reset_isolation -
 *
 * return:
 *
 *   isolation(in):
 *
 * NOTE:
 */
int
log_reset_isolation (TRAN_ISOLATION isolation)
{
#if defined(CS_MODE)
  int req_error, error_code = ER_NET_CLIENT_DATA_RECEIVE;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_request;
  char *request, *ptr;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply;

  request = OR_ALIGNED_BUF_START (a_request);
  reply = OR_ALIGNED_BUF_START (a_reply);

  ptr = or_pack_int (request, (int) isolation);

  req_error =
    net_client_request (NET_SERVER_LOG_RESET_ISOLATION, request, OR_ALIGNED_BUF_SIZE (a_request), reply,
			OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0, NULL, 0);
  if (!req_error)
    {
      or_unpack_int (reply, &error_code);
    }

  return error_code;
#else /* CS_MODE */
  int error_code = NO_ERROR;

  THREAD_ENTRY *thread_p = enter_server ();

  error_code = xlogtb_reset_isolation (thread_p, isolation);

  exit_server (*thread_p);

  return error_code;
#endif /* !CS_MODE */
}

/*
 * log_set_interrupt -
 *
 * return:
 *
 *   set(in):
 *
 * NOTE:
 */
void
log_set_interrupt (int set)
{
#if defined(CS_MODE)
  OR_ALIGNED_BUF (OR_INT_SIZE) a_request;
  char *request;

  request = OR_ALIGNED_BUF_START (a_request);
  or_pack_int (request, set);

  (void) net_client_request_no_reply (NET_SERVER_LOG_SET_INTERRUPT, request, OR_ALIGNED_BUF_SIZE (a_request));
#else /* CS_MODE */

  THREAD_ENTRY *thread_p = enter_server ();

  xlogtb_set_interrupt (thread_p, set);

  exit_server (*thread_p);
#endif /* !CS_MODE */
}

/*
 * log_checkpoint -
 *
 * return:
 *
 * NOTE:
 */
int
log_checkpoint (void)
{
#if defined(CS_MODE)
  int error = ER_NET_CLIENT_DATA_RECEIVE;
  int req_error;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply;

  reply = OR_ALIGNED_BUF_START (a_reply);

  req_error =
    net_client_request (NET_SERVER_LOG_CHECKPOINT, NULL, 0, reply, OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0, NULL, 0);
  if (!req_error)
    {
      or_unpack_errcode (reply, &error);
    }

  return error;
#else /* CS_MODE */
  /* Cannot run in standalone mode */
  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_NOT_IN_STANDALONE, 1, "checkpoint");

  return ER_NOT_IN_STANDALONE;
#endif /* !CS_MODE */
}

/*
 * log_dump_stat -
 *
 * return:
 *
 *   outfp(in):
 *
 * NOTE:
 */
void
log_dump_stat (FILE * outfp)
{
#if defined(CS_MODE)
  int req_error;

  if (outfp == NULL)
    {
      outfp = stdout;
    }

  req_error = net_client_request_recv_stream (NET_SERVER_LOG_DUMP_STAT, NULL, 0, NULL, 0, NULL, 0, outfp);
#else /* CS_MODE */

  THREAD_ENTRY *thread_p = enter_server ();

  xlogpb_dump_stat (outfp);

  exit_server (*thread_p);
#endif /* !CS_MODE */
}

/*
 * log_set_suppress_repl_on_transaction -
 *
 * return:
 *
 *   set(in):
 *
 * NOTE:
 */
int
log_set_suppress_repl_on_transaction (int set)
{
#if defined(CS_MODE)
  int req_error = NO_ERROR;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_request;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *request, *reply;

  request = OR_ALIGNED_BUF_START (a_request);
  or_pack_int (request, set);

  reply = OR_ALIGNED_BUF_START (a_reply);

  req_error =
    net_client_request (NET_SERVER_LOG_SET_SUPPRESS_REPL_ON_TRANSACTION, request, OR_ALIGNED_BUF_SIZE (a_request),
			reply, OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0, NULL, 0);

  if (req_error == NO_ERROR)
    {
      or_unpack_int (reply, &req_error);
    }

  return req_error;
#else /* CS_MODE */
  THREAD_ENTRY *thread_p = enter_server ();

  xlogtb_set_suppress_repl_on_transaction (thread_p, set);

  exit_server (*thread_p);

  return NO_ERROR;
#endif /* !CS_MODE */
}

/*
 * log_find_lob_locator -
 *
 * return:
 *
 *   locator(in):
 *   real_locator(out):
 *
 * NOTE:
 */
LOB_LOCATOR_STATE
log_find_lob_locator (const char *locator, char *real_locator)
{
#if defined(CS_MODE)
  LOB_LOCATOR_STATE state = LOB_UNKNOWN;
  int req_error, state_int, request_size, strlen;
  char *request, *reply, *ptr;
  OR_ALIGNED_BUF (OR_INT_SIZE + OR_INT_SIZE) a_reply;
  int real_loc_size;

  reply = OR_ALIGNED_BUF_START (a_reply);

  request_size = length_const_string (locator, &strlen);
  request = (char *) malloc (request_size);
  if (request == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) request_size);
      return LOB_UNKNOWN;
    }

  (void) pack_const_string_with_length (request, locator, strlen);
  req_error = net_client_request2_no_malloc (NET_SERVER_LOG_FIND_LOB_LOCATOR, request, request_size, reply,
					     OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0, real_locator, &real_loc_size);
  if (!req_error)
    {
      ptr = or_unpack_int (reply, &real_loc_size);
      (void) or_unpack_int (ptr, &state_int);
      state = (LOB_LOCATOR_STATE) state_int;
    }
  free_and_init (request);

  return state;
#else /* CS_MODE */
  LOB_LOCATOR_STATE state;

  THREAD_ENTRY *thread_p = enter_server ();

  state = xlog_find_lob_locator (thread_p, locator, real_locator);

  exit_server (*thread_p);
  return state;
#endif /* !CS_MODE */
}

/*
 * log_add_lob_locator -
 *
 * return: NO_ERROR or error status
 *
 *   locator(in):
 *   state(in);
 *
 * NOTE:
 */
int
log_add_lob_locator (const char *locator, LOB_LOCATOR_STATE state)
{
#if defined(CS_MODE)
  int req_error, error_code = ER_FAILED, request_size, strlen;
  char *request, *reply;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *ptr;

  reply = OR_ALIGNED_BUF_START (a_reply);

  request_size = length_const_string (locator, &strlen) + OR_INT_SIZE;
  request = (char *) malloc (request_size);
  if (request == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) request_size);
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  ptr = pack_const_string_with_length (request, locator, strlen);
  ptr = or_pack_int (ptr, (int) state);
  req_error = net_client_request (NET_SERVER_LOG_ADD_LOB_LOCATOR, request, request_size, reply,
				  OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0, NULL, 0);
  if (!req_error)
    {
      or_unpack_int (reply, &error_code);
    }
  free_and_init (request);

  return error_code;
#else /* CS_MODE */
  int error_code;

  THREAD_ENTRY *thread_p = enter_server ();

  error_code = xlog_add_lob_locator (thread_p, locator, state);

  exit_server (*thread_p);

  return error_code;
#endif /* !CS_MODE */
}

/*
 * log_change_state_of_locator -
 *
 * return: NO_ERROR or error status
 *
 *   locator(in):
 *   state(in);
 *
 * NOTE:
 */
int
log_change_state_of_locator (const char *locator, const char *new_locator, LOB_LOCATOR_STATE state)
{
#if defined(CS_MODE)
  int req_error, error_code = ER_NET_CLIENT_DATA_RECEIVE;
  int request_size, strlen, strlen2;
  char *request, *reply;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *ptr;

  reply = OR_ALIGNED_BUF_START (a_reply);

  request_size = length_const_string (locator, &strlen) + length_const_string (new_locator, &strlen2) + OR_INT_SIZE;
  request = (char *) malloc (request_size);
  if (request == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) request_size);
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  ptr = pack_const_string_with_length (request, locator, strlen);
  ptr = pack_const_string_with_length (ptr, new_locator, strlen2);
  ptr = or_pack_int (ptr, (int) state);
  req_error = net_client_request (NET_SERVER_LOG_CHANGE_STATE_OF_LOCATOR, request, request_size, reply,
				  OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0, NULL, 0);
  if (!req_error)
    {
      or_unpack_int (reply, &error_code);
    }
  free_and_init (request);

  return error_code;
#else /* CS_MODE */
  int error_code;

  THREAD_ENTRY *thread_p = enter_server ();

  error_code = xlog_change_state_of_locator (thread_p, locator, new_locator, state);

  exit_server (*thread_p);

  return error_code;
#endif /* !CS_MODE */
}

/*
 * log_drop_lob_locator -
 *
 * return: NO_ERROR or error status
 *
 *   locator(in):
 *
 * NOTE:
 */
int
log_drop_lob_locator (const char *locator)
{
#if defined(CS_MODE)
  int req_error, error_code = ER_NET_CLIENT_DATA_RECEIVE;
  int request_size, strlen;
  char *request, *reply;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *ptr;

  reply = OR_ALIGNED_BUF_START (a_reply);

  request_size = length_const_string (locator, &strlen);
  request = (char *) malloc (request_size);
  if (request == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) request_size);
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  ptr = pack_const_string_with_length (request, locator, strlen);
  req_error = net_client_request (NET_SERVER_LOG_DROP_LOB_LOCATOR, request, request_size, reply,
				  OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0, NULL, 0);
  if (!req_error)
    {
      or_unpack_int (reply, &error_code);
    }
  free_and_init (request);

  return error_code;
#else /* CS_MODE */
  int error_code;

  THREAD_ENTRY *thread_p = enter_server ();

  error_code = xlog_drop_lob_locator (thread_p, locator);

  exit_server (*thread_p);

  return error_code;
#endif /* !CS_MODE */
}

/*
 * tran_server_commit -
 *
 * return:
 *
 *   retain_lock(in):
 *
 * NOTE:
 */
TRAN_STATE
tran_server_commit (bool retain_lock)
{
#if defined(CS_MODE)
  TRAN_STATE tran_state = TRAN_UNACTIVE_UNKNOWN;
  int req_error, tran_state_int, reset_on_commit;
  int i = 0;
  char *ptr;
  OR_ALIGNED_BUF (OR_INT_SIZE	/* retain_lock */
		  + OR_INT_SIZE	/* row_count */
		  + OR_INT_SIZE	/* count of end query requests */
		  + OR_PTR_SIZE * NET_DEFER_END_QUERIES_MAX	/* query ids */
		  + MAX_ALIGNMENT	/* aligmnent */
    )a_request;
  char *request;
  OR_ALIGNED_BUF (OR_INT_SIZE + OR_INT_SIZE) a_reply;
  char *reply;
  int row_count = 0;

  request = OR_ALIGNED_BUF_START (a_request);
  reply = OR_ALIGNED_BUF_START (a_reply);

  /* Pack retain_lock */
  ptr = or_pack_int (request, (int) retain_lock);
  /* Pack row_count */
  row_count = db_get_row_count_cache ();
  ptr = or_pack_int (ptr, row_count);
  /* Pack query ids to end */
  assert (net_Deferred_end_queries_count <= NET_DEFER_END_QUERIES_MAX);
  ptr = or_pack_int (ptr, net_Deferred_end_queries_count);
  for (i = 0; i < net_Deferred_end_queries_count; i++)
    {
      ptr = or_pack_ptr (ptr, net_Deferred_end_queries[i]);
    }
  net_Deferred_end_queries_count = 0;
  assert (CAST_BUFLEN (ptr - request) <= (int) OR_ALIGNED_BUF_SIZE (a_request));

  req_error =
    net_client_request (NET_SERVER_TM_SERVER_COMMIT, request, CAST_BUFLEN (ptr - request), reply,
			OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0, NULL, 0);
  if (!req_error)
    {
      ptr = or_unpack_int (reply, &tran_state_int);
      tran_state = (TRAN_STATE) tran_state_int;
      ptr = or_unpack_int (ptr, &reset_on_commit);
      if (reset_on_commit != 0 && log_does_allow_replication ())
	{
	  /* 
	   * fail-back action
	   * make the client to reconnect to the active server
	   */
	  db_Connect_status = DB_CONNECTION_STATUS_RESET;
	  er_log_debug (ARG_FILE_LINE, "tran_server_commit: DB_CONNECTION_STATUS_RESET\n");
	}
    }

  net_cleanup_client_queues ();

  return tran_state;
#else /* CS_MODE */
  TRAN_STATE tran_state = TRAN_UNACTIVE_UNKNOWN;

  THREAD_ENTRY *thread_p = enter_server ();

  tran_state = xtran_server_commit (thread_p, retain_lock);

  exit_server (*thread_p);

  return tran_state;
#endif /* !CS_MODE */
}

/*
 * tran_server_abort -
 *
 * return:
 *
 * NOTE:
 */
TRAN_STATE
tran_server_abort (void)
{
#if defined(CS_MODE)
  TRAN_STATE tran_state = TRAN_UNACTIVE_UNKNOWN;
  int req_error, tran_state_int, reset_on_commit;
  char *ptr;
  OR_ALIGNED_BUF (OR_INT_SIZE + OR_INT_SIZE) a_reply;
  char *reply;

  reply = OR_ALIGNED_BUF_START (a_reply);

  /* Queries will be aborted. */
  net_Deferred_end_queries_count = 0;

  req_error =
    net_client_request (NET_SERVER_TM_SERVER_ABORT, NULL, 0, reply, OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0, NULL, 0);
  if (!req_error)
    {
      ptr = or_unpack_int (reply, &tran_state_int);
      tran_state = (TRAN_STATE) tran_state_int;
      ptr = or_unpack_int (ptr, &reset_on_commit);
      if (reset_on_commit != 0 && log_does_allow_replication ())
	{
	  /* 
	   * fail-back action
	   * make the client to reconnect to the active server
	   */
	  db_Connect_status = DB_CONNECTION_STATUS_RESET;
	  er_log_debug (ARG_FILE_LINE, "tran_server_abort: DB_CONNECTION_STATUS_RESET\n");
	}
    }

  net_cleanup_client_queues ();

  return tran_state;
#else /* CS_MODE */
  TRAN_STATE tran_state = TRAN_UNACTIVE_UNKNOWN;

  THREAD_ENTRY *thread_p = enter_server ();

  tran_state = xtran_server_abort (thread_p);

  exit_server (*thread_p);

  return tran_state;
#endif /* !CS_MODE */
}

const char *
tran_get_tranlist_state_name (TRAN_STATE state)
{
  switch (state)
    {
    case TRAN_RECOVERY:
      return "(RECOVERY)";
    case TRAN_ACTIVE:
      return "(ACTIVE)";
    case TRAN_UNACTIVE_COMMITTED:
      return "(COMMITTED)";
    case TRAN_UNACTIVE_WILL_COMMIT:
      return "(COMMITTING)";
    case TRAN_UNACTIVE_COMMITTED_WITH_POSTPONE:
      return "(COMMITTED1)";
    case TRAN_UNACTIVE_TOPOPE_COMMITTED_WITH_POSTPONE:
      return "(COMMITTED3)";
    case TRAN_UNACTIVE_ABORTED:
      return "(ABORTED)";
    case TRAN_UNACTIVE_UNILATERALLY_ABORTED:
      return "(KILLED)";
    case TRAN_UNACTIVE_2PC_PREPARE:
      return "(2PC1)";
    case TRAN_UNACTIVE_2PC_COLLECTING_PARTICIPANT_VOTES:
      return "(2PC2)";
    case TRAN_UNACTIVE_2PC_ABORT_DECISION:
      return "(2PC3)";
    case TRAN_UNACTIVE_2PC_COMMIT_DECISION:
      return "(2PC4)";
    case TRAN_UNACTIVE_COMMITTED_INFORMING_PARTICIPANTS:
      return "(COMMITTED5)";
    case TRAN_UNACTIVE_ABORTED_INFORMING_PARTICIPANTS:
      return "(ABORTED3)";
    case TRAN_UNACTIVE_UNKNOWN:
    default:
      return "(UNKNOWN)";
    }

  return "(UNKNOWN)";
}

/*
 * tran_is_blocked -
 *
 * return:
 *
 *   tran_index(in):
 *
 * NOTE:
 */
bool
tran_is_blocked (int tran_index)
{
#if defined(CS_MODE)
  bool blocked = false;
  int req_error;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_request;
  char *request;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply;
  int temp;

  request = OR_ALIGNED_BUF_START (a_request);
  reply = OR_ALIGNED_BUF_START (a_reply);

  (void) or_pack_int (request, tran_index);

  req_error =
    net_client_request (NET_SERVER_TM_ISBLOCKED, request, OR_ALIGNED_BUF_SIZE (a_request), reply,
			OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0, NULL, 0);
  if (!req_error)
    {
      (void) or_unpack_int (reply, &temp);
      blocked = (temp == 1) ? true : false;
    }

  return blocked;
#else /* CS_MODE */
  bool blocked = false;

  THREAD_ENTRY *thread_p = enter_server ();

  blocked = xtran_is_blocked (thread_p, tran_index);

  exit_server (*thread_p);

  return blocked;
#endif /* !CS_MODE */
}

/*
 * tran_server_has_updated -
 *
 * return:
 *
 * NOTE:
 */
int
tran_server_has_updated (void)
{
#if defined(CS_MODE)
  int has_updated = 0;		/* TODO */
  int req_error;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply;

  reply = OR_ALIGNED_BUF_START (a_reply);

  req_error =
    net_client_request (NET_SERVER_TM_SERVER_HAS_UPDATED, NULL, 0, reply, OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0, NULL,
			0);
  if (!req_error)
    {
      or_unpack_int (reply, &has_updated);
    }

  return has_updated;
#else /* CS_MODE */
  int has_updated = 0;		/* TODO */

  THREAD_ENTRY *thread_p = enter_server ();

  has_updated = xtran_server_has_updated (thread_p);

  exit_server (*thread_p);

  return has_updated;
#endif /* !CS_MODE */
}

/*
 * tran_server_is_active_and_has_updated -
 *
 * return:
 *
 * NOTE:
 */
int
tran_server_is_active_and_has_updated (void)
{
#if defined(CS_MODE)
  int isactive_and_has_updated = 0;	/* TODO */
  int req_error;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply;

  reply = OR_ALIGNED_BUF_START (a_reply);

  req_error =
    net_client_request (NET_SERVER_TM_SERVER_ISACTIVE_AND_HAS_UPDATED, NULL, 0, reply, OR_ALIGNED_BUF_SIZE (a_reply),
			NULL, 0, NULL, 0);
  if (!req_error)
    {
      or_unpack_int (reply, &isactive_and_has_updated);
    }

  return isactive_and_has_updated;
#else /* CS_MODE */
  int isactive_and_has_updated = 0;	/* TODO */

  if (!BO_IS_SERVER_RESTARTED ())
    {
      // server is killed already
      return 0;
    }

  THREAD_ENTRY *thread_p = enter_server ();

  isactive_and_has_updated = xtran_server_is_active_and_has_updated (thread_p);

  exit_server (*thread_p);

  return (isactive_and_has_updated);
#endif /* !CS_MODE */
}

/*
 * tran_wait_server_active_trans -
 *
 * return:
 *
 * NOTE:
 */
int
tran_wait_server_active_trans (void)
{
#if defined(CS_MODE)
  int status = 0;
  int req_error;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply;

  reply = OR_ALIGNED_BUF_START (a_reply);

  req_error =
    net_client_request (NET_SERVER_TM_WAIT_SERVER_ACTIVE_TRANS, NULL, 0, reply, OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0,
			NULL, 0);
  if (!req_error)
    {
      or_unpack_int (reply, &status);
    }

  return status;
#else /* CS_MODE */
  return 0;
#endif /* !CS_MODE */
}

/*
 * tran_server_set_global_tran_info -
 *
 * return:
 *
 *   gtrid(in):
 *   info(in):
 *   size(in):
 *
 * NOTE:
 */
int
tran_server_set_global_tran_info (int gtrid, void *info, int size)
{
#if defined(CS_MODE)
  int success = ER_FAILED;
  int req_error;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_request;
  char *request;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply;

  request = OR_ALIGNED_BUF_START (a_request);
  reply = OR_ALIGNED_BUF_START (a_reply);

  or_pack_int (request, gtrid);

  req_error =
    net_client_request (NET_SERVER_TM_SERVER_SET_GTRINFO, request, OR_ALIGNED_BUF_SIZE (a_request), reply,
			OR_ALIGNED_BUF_SIZE (a_reply), (char *) info, size, NULL, 0);
  if (!req_error)
    {
      or_unpack_int (reply, &success);
    }

  return success;
#else /* CS_MODE */
  int success;

  THREAD_ENTRY *thread_p = enter_server ();

  success = xtran_server_set_global_tran_info (thread_p, gtrid, info, size);

  exit_server (*thread_p);

  return success;
#endif /* !CS_MODE */
}

/*
 * tran_server_get_global_tran_info -
 *
 * return:
 *
 *   gtrid(in):
 *   buffer(in):
 *   size(in):
 *
 * NOTE:
 */
int
tran_server_get_global_tran_info (int gtrid, void *buffer, int size)
{
#if defined(CS_MODE)
  int success = ER_FAILED;
  int req_error;
  OR_ALIGNED_BUF (OR_INT_SIZE * 2) a_request;
  char *request;
  OR_ALIGNED_BUF (OR_INT_SIZE * 2) a_reply;
  char *reply, *ptr;

  request = OR_ALIGNED_BUF_START (a_request);
  reply = OR_ALIGNED_BUF_START (a_reply);

  ptr = or_pack_int (request, gtrid);
  ptr = or_pack_int (ptr, size);
  req_error =
    net_client_request2_no_malloc (NET_SERVER_TM_SERVER_GET_GTRINFO, request, OR_ALIGNED_BUF_SIZE (a_request), reply,
				   OR_ALIGNED_BUF_SIZE (a_reply), (char *) buffer, size, (char *) buffer, &size);
  if (!req_error)
    {
      ptr = or_unpack_int (reply, &size);
      ptr = or_unpack_int (ptr, &success);
    }

  return success;

#else /* CS_MODE */
  int success;

  THREAD_ENTRY *thread_p = enter_server ();

  success = xtran_server_get_global_tran_info (thread_p, gtrid, buffer, size);

  exit_server (*thread_p);

  return success;
#endif /* !CS_MODE */
}

/*
 * tran_server_2pc_start -
 *
 * return:
 *
 * NOTE:
 */
int
tran_server_2pc_start (void)
{
#if defined(CS_MODE)
  int gtrid = NULL_TRANID;
  int req_error;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply;

  reply = OR_ALIGNED_BUF_START (a_reply);

  req_error =
    net_client_request (NET_SERVER_TM_SERVER_2PC_START, NULL, 0, reply, OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0, NULL,
			0);
  if (!req_error)
    {
      or_unpack_int (reply, &gtrid);
    }

  return gtrid;
#else /* CS_MODE */
  int gtrid = NULL_TRANID;

  THREAD_ENTRY *thread_p = enter_server ();

  gtrid = xtran_server_2pc_start (thread_p);

  exit_server (*thread_p);

  return gtrid;
#endif /* !CS_MODE */
}

/*
 * tran_server_2pc_prepare -
 *
 * return:
 *
 * NOTE:
 */
TRAN_STATE
tran_server_2pc_prepare (void)
{
#if defined(CS_MODE)
  TRAN_STATE state = TRAN_UNACTIVE_UNKNOWN;
  int req_error;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply;

  reply = OR_ALIGNED_BUF_START (a_reply);

  req_error =
    net_client_request (NET_SERVER_TM_SERVER_2PC_PREPARE, NULL, 0, reply, OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0, NULL,
			0);
  if (!req_error)
    {
      or_unpack_int (reply, (int *) &state);
    }

  return state;
#else /* CS_MODE */
  TRAN_STATE state = TRAN_UNACTIVE_UNKNOWN;

  THREAD_ENTRY *thread_p = enter_server ();

  state = xtran_server_2pc_prepare (thread_p);

  exit_server (*thread_p);

  return (state);
#endif /* !CS_MODE */
}

/*
 * tran_server_2pc_recovery_prepared -
 *
 * return:
 *
 *   gtrids(in):
 *   size(in):
 *
 * NOTE:
 */
int
tran_server_2pc_recovery_prepared (int gtrids[], int size)
{
#if defined(CS_MODE)
  int count = -1, i;
  int req_error;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_request;
  char *request;
  int reply_size;
  char *reply, *ptr;

  request = OR_ALIGNED_BUF_START (a_request);
  reply_size = OR_INT_SIZE + (OR_INT_SIZE * size);
  reply = (char *) malloc (reply_size);
  if (reply == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) reply_size);
      return -1;
    }

  or_pack_int (request, size);

  req_error = net_client_request (NET_SERVER_TM_SERVER_2PC_RECOVERY_PREPARED, request, OR_ALIGNED_BUF_SIZE (a_request),
				  reply, reply_size, NULL, 0, NULL, 0);
  if (!req_error)
    {
      ptr = or_unpack_int (reply, &count);
      for (i = 0; i < count && i < size; i++)
	{
	  ptr = or_unpack_int (ptr, &gtrids[i]);
	}
    }
  free_and_init (reply);

  return count;
#else /* CS_MODE */
  int count;

  THREAD_ENTRY *thread_p = enter_server ();

  count = xtran_server_2pc_recovery_prepared (thread_p, gtrids, size);

  exit_server (*thread_p);

  return count;
#endif /* !CS_MODE */
}

/*
 * tran_server_2pc_attach_global_tran -
 *
 * return:
 *
 *   gtrid(in):
 *
 * NOTE:
 */
int
tran_server_2pc_attach_global_tran (int gtrid)
{
#if defined(CS_MODE)
  int tran_index = NULL_TRAN_INDEX;
  int req_error;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_request;
  char *request;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply;

  request = OR_ALIGNED_BUF_START (a_request);
  reply = OR_ALIGNED_BUF_START (a_reply);

  or_pack_int (request, gtrid);

  req_error =
    net_client_request (NET_SERVER_TM_SERVER_2PC_ATTACH_GT, request, OR_ALIGNED_BUF_SIZE (a_request), reply,
			OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0, NULL, 0);
  if (!req_error)
    {
      or_unpack_int (reply, &tran_index);
    }

  return tran_index;
#else /* CS_MODE */
  int tran_index = NULL_TRAN_INDEX;

  THREAD_ENTRY *thread_p = enter_server ();

  tran_index = xtran_server_2pc_attach_global_tran (thread_p, gtrid);

  exit_server (*thread_p);

  return tran_index;
#endif /* !CS_MODE */
}

/*
 * tran_server_2pc_prepare_global_tran -
 *
 * return:
 *
 *   gtrid(in):
 *
 * NOTE:
 */
TRAN_STATE
tran_server_2pc_prepare_global_tran (int gtrid)
{
#if defined(CS_MODE)
  TRAN_STATE tran_state = TRAN_UNACTIVE_UNKNOWN;
  int req_error, tran_state_int;
  char *ptr;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_request;
  char *request;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply;

  request = OR_ALIGNED_BUF_START (a_request);
  reply = OR_ALIGNED_BUF_START (a_reply);

  or_pack_int (request, gtrid);
  req_error =
    net_client_request (NET_SERVER_TM_SERVER_2PC_PREPARE_GT, request, OR_ALIGNED_BUF_SIZE (a_request), reply,
			OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0, NULL, 0);
  if (!req_error)
    {
      ptr = or_unpack_int (reply, &tran_state_int);
      tran_state = (TRAN_STATE) tran_state_int;
    }

  return tran_state;
#else /* CS_MODE */
  TRAN_STATE tran_state = TRAN_UNACTIVE_UNKNOWN;

  THREAD_ENTRY *thread_p = enter_server ();

  tran_state = xtran_server_2pc_prepare_global_tran (thread_p, gtrid);

  exit_server (*thread_p);

  return tran_state;
#endif /* !CS_MODE */
}

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * tran_server_start_topop -
 *
 * return:
 *
 *   topop_lsa(in):
 *
 * NOTE:
 */
int
tran_server_start_topop (LOG_LSA * topop_lsa)
{
#if defined(CS_MODE)
  int success = ER_FAILED;
  int req_error;
  OR_ALIGNED_BUF (OR_INT_SIZE + OR_LOG_LSA_ALIGNED_SIZE) a_reply;
  char *reply;
  char *ptr;

  reply = OR_ALIGNED_BUF_START (a_reply);

  req_error =
    net_client_request (NET_SERVER_TM_SERVER_START_TOPOP, NULL, 0, reply, OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0, NULL,
			0);
  if (!req_error)
    {
      ptr = or_unpack_int (reply, &success);
      ptr = or_unpack_log_lsa (ptr, topop_lsa);
    }
  return success;
#else /* CS_MODE */
  int success = ER_FAILED;

  THREAD_ENTRY *thread_p = enter_server ();

  if (xtran_server_start_topop (thread_p, topop_lsa) != NO_ERROR)
    {
      success = ER_FAILED;
    }
  else
    {
      success = NO_ERROR;
    }

  exit_server (*thread_p);

  return success;
#endif /* !CS_MODE */
}

/*
 * tran_server_end_topop -
 *
 * return:
 *
 *   result(in):
 *   topop_lsa(in):
 *
 * NOTE:
 */
TRAN_STATE
tran_server_end_topop (LOG_RESULT_TOPOP result, LOG_LSA * topop_lsa)
{
#if defined(CS_MODE)
  TRAN_STATE tran_state = TRAN_UNACTIVE_UNKNOWN;
  int req_error, tran_state_int;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_request;
  char *request;
  OR_ALIGNED_BUF (OR_INT_SIZE + OR_LOG_LSA_ALIGNED_SIZE) a_reply;
  char *reply;
  char *ptr;

  request = OR_ALIGNED_BUF_START (a_request);
  reply = OR_ALIGNED_BUF_START (a_reply);

  ptr = or_pack_int (request, (int) result);
  req_error =
    net_client_request (NET_SERVER_TM_SERVER_END_TOPOP, NULL, 0, reply, OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0, NULL,
			0);
  if (!req_error)
    {
      ptr = or_unpack_int (reply, &tran_state_int);
      tran_state = (TRAN_STATE) tran_state_int;
      ptr = or_unpack_log_lsa (ptr, topop_lsa);
    }
  return tran_state;
#else /* CS_MODE */
  TRAN_STATE tran_state = TRAN_UNACTIVE_UNKNOWN;

  THREAD_ENTRY *thread_p = enter_server ();

  tran_state = xtran_server_end_topop (thread_p, result, topop_lsa);

  exit_server (*thread_p);

  return (tran_state);
#endif /* !CS_MODE */
}
#endif

/*
 * tran_server_savepoint -
 *
 * return:
 *
 *   savept_name(in):
 *   savept_lsa(in):
 *
 * NOTE:
 */
int
tran_server_savepoint (const char *savept_name, LOG_LSA * savept_lsa)
{
#if defined(CS_MODE)
  int success = ER_FAILED;
  int req_error, request_size, strlen;
  char *request, *reply;
  OR_ALIGNED_BUF (OR_INT_SIZE + OR_LOG_LSA_ALIGNED_SIZE) a_reply;
  char *ptr;

  reply = OR_ALIGNED_BUF_START (a_reply);

  request_size = length_const_string (savept_name, &strlen);
  request = (char *) malloc (request_size);
  if (request == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) request_size);
      return ER_FAILED;
    }

  ptr = pack_const_string_with_length (request, savept_name, strlen);
  req_error = net_client_request (NET_SERVER_TM_SERVER_SAVEPOINT, request, request_size, reply,
				  OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0, NULL, 0);
  if (!req_error)
    {
      ptr = or_unpack_int (reply, &success);
      ptr = or_unpack_log_lsa (ptr, savept_lsa);
    }
  free_and_init (request);

  return success;

#else /* CS_MODE */
  int success = ER_FAILED;

  THREAD_ENTRY *thread_p = enter_server ();

  success = xtran_server_savepoint (thread_p, savept_name, savept_lsa);

  exit_server (*thread_p);

  return success;
#endif /* !CS_MODE */
}

/*
 * tran_server_partial_abort -
 *
 * return:
 *
 *   savept_name(in):
 *   savept_lsa(in):
 *
 * NOTE:
 */
TRAN_STATE
tran_server_partial_abort (const char *savept_name, LOG_LSA * savept_lsa)
{
#if defined(CS_MODE)
  TRAN_STATE tran_state = TRAN_UNACTIVE_UNKNOWN;
  int req_error, tran_state_int, request_size, strlen;
  char *request, *reply;
  OR_ALIGNED_BUF (OR_INT_SIZE + OR_LOG_LSA_ALIGNED_SIZE) a_reply;
  char *ptr;

  reply = OR_ALIGNED_BUF_START (a_reply);

  request_size = length_const_string (savept_name, &strlen);
  request = (char *) malloc (request_size);
  if (request == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) request_size);
      return TRAN_UNACTIVE_UNKNOWN;
    }

  ptr = pack_const_string_with_length (request, savept_name, strlen);
  req_error = net_client_request (NET_SERVER_TM_SERVER_PARTIAL_ABORT, request, request_size, reply,
				  OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0, NULL, 0);
  if (!req_error)
    {
      ptr = or_unpack_int (reply, &tran_state_int);
      tran_state = (TRAN_STATE) tran_state_int;
      ptr = or_unpack_log_lsa (ptr, savept_lsa);
    }
  free_and_init (request);

  return tran_state;
#else /* CS_MODE */
  TRAN_STATE tran_state = TRAN_UNACTIVE_UNKNOWN;

  THREAD_ENTRY *thread_p = enter_server ();

  tran_state = xtran_server_partial_abort (thread_p, savept_name, savept_lsa);

  exit_server (*thread_p);

  return tran_state;
#endif /* !CS_MODE */
}

/*
 * acl_reload -
 *
 * return:
 *
 */
int
acl_reload ()
{
#if defined(CS_MODE)
  int error = ER_NET_CLIENT_DATA_RECEIVE;
  int req_error;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply;

  reply = OR_ALIGNED_BUF_START (a_reply);

  req_error =
    net_client_request (NET_SERVER_ACL_RELOAD, NULL, 0, reply, OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0, NULL, 0);
  if (!req_error)
    {
      or_unpack_errcode (reply, &error);
    }

  return error;
#else
  return NO_ERROR;
#endif /* !CS_MODE */
}


/*
 * acl_dump -
 *
 * return:
 *
 *   outfp(in):
 */
void
acl_dump (FILE * outfp)
{
#if defined(CS_MODE)
  int req_error;

  if (outfp == NULL)
    {
      outfp = stdout;
    }

  req_error = net_client_request_recv_stream (NET_SERVER_ACL_DUMP, NULL, 0, NULL, 0, NULL, 0, outfp);
#endif /* !CS_MODE */
}

/*
 * lock_dump -
 *
 * return:
 *
 *   outfp(in):
 */
void
lock_dump (FILE * outfp)
{
#if defined(CS_MODE)
  int req_error;

  if (outfp == NULL)
    {
      outfp = stdout;
    }

  req_error = net_client_request_recv_stream (NET_SERVER_LK_DUMP, NULL, 0, NULL, 0, NULL, 0, outfp);
#else /* CS_MODE */

  THREAD_ENTRY *thread_p = enter_server ();

  xlock_dump (thread_p, outfp);

  exit_server (*thread_p);
#endif /* !CS_MODE */
}

/*
 * boot_initialize_server -
 *
 * return:
 *
 *   print_version(in):
 *   db_overwrite(in):
 *   db_desired_pagesize(in):
 *   db_name(in):
 *   db_path(in):
 *   vol_path(in):
 *   db_comments(in):
 *   db_npages(in):
 *   file_addmore_vols(in):
 *   db_server_host(in):
 *   log_path(in):
 *   log_npages(in):
 *   rootclass_oid(in):
 *   rootclass_hfid(in):
 *   client_prog_name(in):
 *   client_user_name(in):
 *   client_host_name(in):
 *   client_process_id(in):
 *   client_lock_wait(in):
 *   client_isolation(in):
 *
 * NOTE:
 */
int
boot_initialize_server (const BOOT_CLIENT_CREDENTIAL * client_credential, BOOT_DB_PATH_INFO * db_path_info,
			bool db_overwrite, const char *file_addmore_vols, DKNPAGES db_npages,
			PGLENGTH db_desired_pagesize, DKNPAGES log_npages, PGLENGTH db_desired_log_page_size,
			OID * rootclass_oid, HFID * rootclass_hfid, int client_lock_wait,
			TRAN_ISOLATION client_isolation)
{
#if defined(CS_MODE)
#if defined(ENABLE_UNUSED_FUNCTION)
  int tran_index = NULL_TRAN_INDEX;
  int request_size;
  int req_error;
  char *request, *ptr;
  OR_ALIGNED_BUF (OR_INT_SIZE + OR_OID_SIZE + OR_HFID_SIZE) a_reply;
  char *reply;

  reply = OR_ALIGNED_BUF_START (a_reply);

  request_size = (OR_INT_SIZE	/* client_type */
		  + length_const_string (client_credential->client_info, NULL)	/* client_info */
		  + length_const_string (client_credential->db_name, NULL)	/* db_name */
		  + length_const_string (client_credential->db_user, NULL)	/* db_user */
		  + length_const_string (client_credential->db_password, NULL)	/* db_password */
		  + length_const_string (client_credential->program_name, NULL)	/* program_name */
		  + length_const_string (client_credential->login_name, NULL)	/* login_name */
		  + length_const_string (client_credential->host_name, NULL)	/* host_name */
		  + OR_INT_SIZE	/* process_id */
		  + OR_INT_SIZE	/* db_overwrite */
		  + OR_INT_SIZE	/* db_desired_pagesize */
		  + OR_INT_SIZE	/* db_npages */
		  + OR_INT_SIZE	/* db_desired_log_page_size */
		  + OR_INT_SIZE	/* log_npages */
		  + length_const_string (db_path_info->db_path, NULL)	/* db_path */
		  + length_const_string (db_path_info->vol_path, NULL)	/* vol_path */
		  + length_const_string (db_path_info->log_path, NULL)	/* log_path */
		  + length_const_string (db_path_info->db_host, NULL)	/* db_host */
		  + length_const_string (db_path_info->db_comments, NULL)	/* db_comments */
		  + length_const_string (file_addmore_vols, NULL)	/* file_addmore_vols */
		  + OR_INT_SIZE	/* client_lock_wait */
		  + OR_INT_SIZE /* client_isolation */ );

  request = (char *) malloc (request_size);
  if (request)
    {
      ptr = or_pack_int (request, client_credential->client_type);
      ptr = pack_const_string (ptr, client_credential->client_info);
      ptr = pack_const_string (ptr, client_credential->db_name);
      ptr = pack_const_string (ptr, client_credential->db_user);
      ptr = pack_const_string (ptr, client_credential->db_password);
      ptr = pack_const_string (ptr, client_credential->program_name);
      ptr = pack_const_string (ptr, client_credential->login_name);
      ptr = pack_const_string (ptr, client_credential->host_name);
      ptr = or_pack_int (ptr, client_credential->process_id);
      ptr = or_pack_int (ptr, db_overwrite);
      ptr = or_pack_int (ptr, db_desired_pagesize);
      ptr = or_pack_int (ptr, db_npages);
      ptr = or_pack_int (ptr, db_desired_log_page_size);
      ptr = or_pack_int (ptr, log_npages);
      ptr = pack_const_string (ptr, db_path_info->db_path);
      ptr = pack_const_string (ptr, db_path_info->vol_path);
      ptr = pack_const_string (ptr, db_path_info->log_path);
      ptr = pack_const_string (ptr, db_path_info->db_host);
      ptr = pack_const_string (ptr, db_path_info->db_comments);
      ptr = pack_const_string (ptr, file_addmore_vols);
      ptr = or_pack_int (ptr, client_lock_wait);
      ptr = or_pack_int (ptr, (int) client_isolation);

      req_error =
	net_client_request (NET_SERVER_BO_INIT_SERVER, request, request_size, reply, OR_ALIGNED_BUF_SIZE (a_reply),
			    NULL, 0, NULL, 0);
      if (!req_error)
	{
	  ptr = or_unpack_int (reply, &tran_index);
	  ptr = or_unpack_oid (ptr, rootclass_oid);
	  ptr = or_unpack_hfid (ptr, rootclass_hfid);
	}
      free_and_init (request);
    }
  else
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) request_size);
    }

  return tran_index;
#else /* ENABLE_UNUSED_FUNCTION */
  /* Should not called in CS_MODE */
  assert (0);
  return NULL_TRAN_INDEX;
#endif /* !ENABLE_UNUSED_FUNCTION */
#else /* CS_MODE */
  int tran_index = NULL_TRAN_INDEX;

  enter_server_no_thread_entry ();

  tran_index =
    xboot_initialize_server (client_credential, db_path_info, db_overwrite, file_addmore_vols, db_npages,
			     db_desired_pagesize, log_npages, db_desired_log_page_size, rootclass_oid, rootclass_hfid,
			     client_lock_wait, client_isolation);

  exit_server_no_thread_entry ();

  return (tran_index);
#endif /* !CS_MODE */
}

/*
 * boot_register_client -
 *
 * return:
 *
 *   client_credential(in)
 *   client_lock_wait(in):
 *   client_isolation(in):
 *   tran_state(out):
 *   server_credential(out):
 */
int
boot_register_client (BOOT_CLIENT_CREDENTIAL * client_credential, int client_lock_wait, TRAN_ISOLATION client_isolation,
		      TRAN_STATE * tran_state, BOOT_SERVER_CREDENTIAL * server_credential)
{
#if defined(CS_MODE)
  int tran_index = NULL_TRAN_INDEX;
  int request_size, area_size, req_error, temp_int;
  char *request, *reply, *area, *ptr;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;

  reply = OR_ALIGNED_BUF_START (a_reply);

  request_size = (OR_INT_SIZE	/* client_type */
		  + length_const_string (client_credential->client_info, NULL)	/* client_info */
		  + length_const_string (client_credential->db_name, NULL)	/* db_name */
		  + length_const_string (client_credential->db_user, NULL)	/* db_user */
		  + length_const_string (client_credential->db_password, NULL)	/* db_password */
		  + length_const_string (client_credential->program_name, NULL)	/* prog_name */
		  + length_const_string (client_credential->login_name, NULL)	/* login_name */
		  + length_const_string (client_credential->host_name, NULL)	/* host_name */
		  + OR_INT_SIZE	/* process_id */
		  + OR_INT_SIZE	/* client_lock_wait */
		  + OR_INT_SIZE /* client_isolation */ );

  request = (char *) malloc (request_size);
  if (request == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) request_size);
      return NULL_TRAN_INDEX;
    }

  ptr = or_pack_int (request, (int) client_credential->client_type);
  ptr = pack_const_string (ptr, client_credential->client_info);
  ptr = pack_const_string (ptr, client_credential->db_name);
  ptr = pack_const_string (ptr, client_credential->db_user);
  ptr = pack_const_string (ptr, client_credential->db_password);
  ptr = pack_const_string (ptr, client_credential->program_name);
  ptr = pack_const_string (ptr, client_credential->login_name);
  ptr = pack_const_string (ptr, client_credential->host_name);
  ptr = or_pack_int (ptr, client_credential->process_id);
  ptr = or_pack_int (ptr, client_lock_wait);
  ptr = or_pack_int (ptr, (int) client_isolation);

  req_error = net_client_request2 (NET_SERVER_BO_REGISTER_CLIENT, request, request_size, reply,
				   OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0, &area, &area_size);
  if (!req_error)
    {
      or_unpack_int (reply, &area_size);
      if (area_size > 0)
	{
	  int ha_state_to_int;

	  ptr = or_unpack_int (area, &tran_index);

	  ptr = or_unpack_int (ptr, &temp_int);
	  *tran_state = (TRAN_STATE) temp_int;

	  ptr = or_unpack_string (ptr, &server_credential->db_full_name);
	  ptr = or_unpack_string (ptr, &server_credential->host_name);
	  ptr = or_unpack_string (ptr, &server_credential->lob_path);
	  ptr = or_unpack_int (ptr, &server_credential->process_id);
	  ptr = or_unpack_oid (ptr, &server_credential->root_class_oid);
	  ptr = or_unpack_hfid (ptr, &server_credential->root_class_hfid);

	  ptr = or_unpack_int (ptr, &temp_int);
	  server_credential->page_size = (PGLENGTH) temp_int;

	  ptr = or_unpack_int (ptr, &temp_int);
	  server_credential->log_page_size = (PGLENGTH) temp_int;

	  ptr = or_unpack_float (ptr, &server_credential->disk_compatibility);

	  ptr = or_unpack_int (ptr, &ha_state_to_int);
	  server_credential->ha_server_state = (HA_SERVER_STATE) ha_state_to_int;

	  ptr = or_unpack_int (ptr, &server_credential->db_charset);
	  ptr = or_unpack_string (ptr, &server_credential->db_lang);
	}
      free_and_init (area);
    }

  free_and_init (request);

  return tran_index;
#else /* CS_MODE */
  int tran_index = NULL_TRAN_INDEX;

  enter_server_no_thread_entry ();

  tran_index =
    xboot_register_client (NULL, client_credential, client_lock_wait, client_isolation, tran_state, server_credential);
  exit_server_no_thread_entry ();

  return tran_index;
#endif /* !CS_MODE */
}

/*
 * boot_unregister_client -
 *
 * return:
 *
 *   tran_index(in):
 *
 * NOTE:
 */
int
boot_unregister_client (int tran_index)
{
#if defined(CS_MODE)
  int error_code = ER_FAILED;
  int req_error;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_request;
  char *request;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply;

  request = OR_ALIGNED_BUF_START (a_request);
  reply = OR_ALIGNED_BUF_START (a_reply);

  (void) or_pack_int (request, tran_index);

  req_error = net_client_request (NET_SERVER_BO_UNREGISTER_CLIENT, request, OR_ALIGNED_BUF_SIZE (a_request), reply,
				  OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0, NULL, 0);
  if (req_error == NO_ERROR)
    {
      or_unpack_int (reply, &error_code);
    }
  else
    {
      error_code = req_error;
    }

  return error_code;
#else /* CS_MODE */
  int error_code = NO_ERROR;

  if (!BO_IS_SERVER_RESTARTED ())
    {
      // server is killed already
      return NO_ERROR;
    }

  THREAD_ENTRY *thread_p = enter_server ();

  error_code = xboot_unregister_client (thread_p, tran_index);
  if (tran_index == NULL_TRAN_INDEX)
    {
      assert (thread_p == NULL);
      exit_server_no_thread_entry ();
    }
  else
    {
      exit_server (*thread_p);
    }

  return error_code;
#endif /* !CS_MODE */
}

/*
 * boot_backup -
 *
 * return:
 *
 *   backup_path(in):
 *   backup_level(in):
 *   delete_unneeded_logarchives(in):
 *   backup_verbose_file(in):
 *   num_threads(in):
 *   zip_method(in):
 *   zip_level(in):
 *   skip_activelog(in):
 *   sleep_msecs(in):
 *
 * NOTE:
 */
int
boot_backup (const char *backup_path, FILEIO_BACKUP_LEVEL backup_level, bool delete_unneeded_logarchives,
	     const char *backup_verbose_file, int num_threads, FILEIO_ZIP_METHOD zip_method, FILEIO_ZIP_LEVEL zip_level,
	     int skip_activelog, int sleep_msecs)
{
#if defined(CS_MODE)
  int success = ER_FAILED;
  int request_size, strlen1, strlen2;
  char *request, *ptr;
  int req_error;
  OR_ALIGNED_BUF (OR_INT_SIZE * 3) a_reply;
  char *reply;
  char *rd1, *rd2;
  int d1, d2;
  int cb_type;

  reply = OR_ALIGNED_BUF_START (a_reply);

  request_size = (length_const_string (backup_path, &strlen1) + OR_INT_SIZE + OR_INT_SIZE
		  + length_const_string (backup_verbose_file, &strlen2) + OR_INT_SIZE + OR_INT_SIZE + OR_INT_SIZE
		  + OR_INT_SIZE + OR_INT_SIZE);

  request = (char *) malloc (request_size);
  if (request == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) request_size);
      return ER_FAILED;
    }

  ptr = pack_const_string_with_length (request, backup_path, strlen1);
  ptr = or_pack_int (ptr, backup_level);
  ptr = or_pack_int (ptr, delete_unneeded_logarchives ? 1 : 0);
  ptr = pack_const_string_with_length (ptr, backup_verbose_file, strlen2);
  ptr = or_pack_int (ptr, num_threads);
  ptr = or_pack_int (ptr, zip_method);
  ptr = or_pack_int (ptr, zip_level);
  ptr = or_pack_int (ptr, skip_activelog);
  ptr = or_pack_int (ptr, sleep_msecs);
  req_error =
    net_client_request_with_callback (NET_SERVER_BO_BACKUP, request, request_size, reply,
				      OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0, NULL, 0, &rd1, &d1, &rd2, &d2, NULL,
				      NULL);
  if (!req_error)
    {
      ptr = or_unpack_int (reply, &cb_type);
      or_unpack_int (ptr, &success);
      /* we just ignore the last part of the reply */
    }
  free_and_init (request);

  return success;
#else /* CS_MODE */
  int success = false;

  THREAD_ENTRY *thread_p = enter_server ();

  success = xboot_backup (thread_p, backup_path, backup_level, delete_unneeded_logarchives, backup_verbose_file,
			  num_threads, zip_method, zip_level, skip_activelog, sleep_msecs);

  exit_server (*thread_p);

  return success;
#endif /* !CS_MODE */
}

/*
 * boot_add_volume_extension -
 *
 * return:
 *
 *   ext_info(in):
 *
 * NOTE:
 */
VOLID
boot_add_volume_extension (DBDEF_VOL_EXT_INFO * ext_info)
{
#if defined(CS_MODE)
  int int_volid;
  VOLID volid = NULL_VOLID;
  int request_size, strlen1, strlen2, strlen3;
  char *request, *ptr;
  int req_error;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply;

  reply = OR_ALIGNED_BUF_START (a_reply);

  request_size = (length_const_string (ext_info->path, &strlen1)
		  + length_const_string (ext_info->name, &strlen2)
		  + length_const_string (ext_info->comments, &strlen3)
		  + OR_INT_SIZE + OR_INT_SIZE + OR_INT_SIZE + OR_INT_SIZE);

  request = (char *) malloc (request_size);
  if (request == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) request_size);
      return NULL_VOLID;
    }

  ptr = pack_const_string_with_length (request, ext_info->path, strlen1);
  ptr = pack_const_string_with_length (ptr, ext_info->name, strlen2);
  ptr = pack_const_string_with_length (ptr, ext_info->comments, strlen3);
  ptr = or_pack_int (ptr, (int) ext_info->max_npages);
  ptr = or_pack_int (ptr, (int) ext_info->max_writesize_in_sec);
  ptr = or_pack_int (ptr, (int) ext_info->purpose);
  ptr = or_pack_int (ptr, (int) ext_info->overwrite);
  req_error = net_client_request (NET_SERVER_BO_ADD_VOLEXT, request, request_size, reply,
				  OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0, NULL, 0);
  if (!req_error)
    {
      or_unpack_int (reply, &int_volid);
      volid = int_volid;
    }
  free_and_init (request);

  return volid;
#else /* CS_MODE */
  VOLID volid;

  THREAD_ENTRY *thread_p = enter_server ();

  volid = xboot_add_volume_extension (thread_p, ext_info);

  exit_server (*thread_p);

  return volid;
#endif /* !CS_MODE */
}

/*
 * boot_check_db_consistency -
 *
 * return:
 *
 *   check_flag(in):
 *
 * NOTE:
 */
int
boot_check_db_consistency (int check_flag, OID * oids, int num_oids, BTID * index_btid)
{
#if defined(CS_MODE)
  int success = ER_FAILED;
  int req_error;
  int cb_type;
  char *rd1, *rd2;
  int d1, d2;
  OR_ALIGNED_BUF (OR_INT_SIZE * 3) a_reply;
  char *reply;
  char *request, *ptr;
  size_t request_size;
  int i;

  request_size = OR_INT_SIZE;	/* check_flag */
  request_size += OR_INT_SIZE;	/* num_oid */
  request_size += (OR_OID_SIZE * num_oids);
  request_size += OR_BTID_ALIGNED_SIZE;

  request = (char *) malloc (request_size);
  if (request == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) request_size);
      return ER_FAILED;
    }

  ptr = or_pack_int (request, check_flag);
  ptr = or_pack_int (ptr, num_oids);
  for (i = 0; i < num_oids; i++)
    {
      ptr = or_pack_oid (ptr, &oids[i]);
    }
  ptr = or_pack_btid (ptr, index_btid);

  reply = OR_ALIGNED_BUF_START (a_reply);
  req_error = net_client_request_with_callback (NET_SERVER_BO_CHECK_DBCONSISTENCY, request, (int) request_size, reply,
						OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0, NULL, 0, &rd1, &d1, &rd2, &d2,
						NULL, NULL);
  free_and_init (request);

  if (!req_error)
    {
      ptr = or_unpack_int (reply, &cb_type);
      or_unpack_int (ptr, &success);
    }

  return success;
#else /* CS_MODE */
  int success = ER_FAILED;

  THREAD_ENTRY *thread_p = enter_server ();

  success = xboot_check_db_consistency (thread_p, check_flag, oids, num_oids, index_btid);

  exit_server (*thread_p);

  return success;
#endif /* !CS_MODE */
}

/*
 * boot_find_number_permanent_volumes -
 *
 * return:
 *
 * NOTE:
 */
int
boot_find_number_permanent_volumes (void)
{
#if defined(CS_MODE)
  int nvols = -1;
  int req_error;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply;

  reply = OR_ALIGNED_BUF_START (a_reply);

  req_error =
    net_client_request (NET_SERVER_BO_FIND_NPERM_VOLS, NULL, 0, reply, OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0, NULL, 0);
  if (!req_error)
    {
      or_unpack_int (reply, &nvols);
    }

  return nvols;
#else /* CS_MODE */
  int nvols = -1;

  THREAD_ENTRY *thread_p = enter_server ();

  nvols = xboot_find_number_permanent_volumes (thread_p);

  exit_server (*thread_p);

  return nvols;
#endif /* !CS_MODE */
}

/*
 * boot_find_number_temp_volumes -
 *
 * return:
 *
 * NOTE:
 */
int
boot_find_number_temp_volumes (void)
{
#if defined(CS_MODE)
  int nvols = -1;
  int req_error;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply;

  reply = OR_ALIGNED_BUF_START (a_reply);

  req_error =
    net_client_request (NET_SERVER_BO_FIND_NTEMP_VOLS, NULL, 0, reply, OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0, NULL, 0);
  if (!req_error)
    {
      or_unpack_int (reply, &nvols);
    }

  return nvols;
#else /* CS_MODE */
  int nvols = -1;

  THREAD_ENTRY *thread_p = enter_server ();

  nvols = xboot_find_number_temp_volumes (thread_p);

  exit_server (*thread_p);

  return nvols;
#endif /* !CS_MODE */
}

/*
 * boot_find_last_permanant -
 *
 * return:
 *
 * NOTE:
 */
VOLID
boot_find_last_permanent (void)
{
  VOLID volid = NULL_VOLID;
#if defined(CS_MODE)
  int int_volid;
  int req_error;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply;

  reply = OR_ALIGNED_BUF_START (a_reply);

  req_error =
    net_client_request (NET_SERVER_BO_FIND_LAST_PERM, NULL, 0, reply, OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0, NULL, 0);
  if (!req_error)
    {
      or_unpack_int (reply, &int_volid);
      volid = (VOLID) int_volid;
    }

  return volid;
#else /* CS_MODE */
  THREAD_ENTRY *thread_p = enter_server ();

  volid = xboot_find_last_permanent (thread_p);

  exit_server (*thread_p);

  return volid;
#endif /* !CS_MODE */
}

/*
 * boot_find_last_temp -
 *
 * return:
 *
 * NOTE:
 */
int
boot_find_last_temp (void)
{
#if defined(CS_MODE)
  int nvols = -1;
  int req_error;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply;

  reply = OR_ALIGNED_BUF_START (a_reply);

  req_error =
    net_client_request (NET_SERVER_BO_FIND_LAST_TEMP, NULL, 0, reply, OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0, NULL, 0);
  if (!req_error)
    {
      or_unpack_int (reply, &nvols);
    }

  return nvols;
#else /* CS_MODE */
  int nvols = -1;

  THREAD_ENTRY *thread_p = enter_server ();

  nvols = xboot_find_last_temp (thread_p);

  exit_server (*thread_p);

  return nvols;
#endif /* !CS_MODE */
}

/*
 * boot_delete -
 *
 * return:
 *
 * NOTE:
 */
int
boot_delete (const char *db_name, bool force_delete)
{
#if defined(CS_MODE)
  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ONLY_IN_STANDALONE, 1, "delete database");
  return ER_FAILED;
#else /* CS_MODE */
  int error_code;

  enter_server_no_thread_entry ();

  error_code = xboot_delete (db_name, force_delete, BOOT_SHUTDOWN_ALL_MODULES);

  exit_server_no_thread_entry ();

  return error_code;
#endif /* !CS_MODE */
}

/*
 * boot_restart_from_backup -
 *
 * return:
 *
 * NOTE:
 */
int
boot_restart_from_backup (int print_restart, const char *db_name, BO_RESTART_ARG * r_args)
{
#if defined(CS_MODE)
  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ONLY_IN_STANDALONE, 1, "restart from backup");
  return NULL_TRAN_INDEX;
#else /* CS_MODE */
  int tran_index;

  enter_server_no_thread_entry ();

  tran_index = xboot_restart_from_backup (NULL, print_restart, db_name, r_args);

  exit_server_no_thread_entry ();

  return tran_index;
#endif /* !CS_MODE */
}

/*
 * boot_shutdown_server -
 *
 * return:
 *
 * NOTE:
 */
bool
boot_shutdown_server (ER_FINAL_CODE iserfinal)
{
#if defined(CS_MODE)
  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ONLY_IN_STANDALONE, 1, "");
  return false;
#else /* CS_MODE */
  bool result;

  THREAD_ENTRY *thread_p = enter_server ();

  result = xboot_shutdown_server (thread_p, iserfinal);
  assert (thread_p == NULL);

  exit_server_no_thread_entry ();

  return result;
#endif /* !CS_MODE */
}

/*
 * csession_find_or_create_session - check if session is still active
 *                                     if not, create a new session
 *
 * return	   : error code or NO_ERROR
 * session_id (in/out) : the id of the session to end
 * row_count (out)     : the value of row count for this session
 * server_session_key (in/out) :
 */
int
csession_find_or_create_session (SESSION_ID * session_id, int *row_count, char *server_session_key, const char *db_user,
				 const char *host, const char *program_name)
{
#if defined (CS_MODE)
  int req_error;
  OR_ALIGNED_BUF (OR_INT_SIZE + OR_INT_SIZE) a_reply;
  char *reply = NULL;
  char *request = NULL, *area = NULL;
  char *ptr;
  int request_size, area_size;
  int db_user_len, host_len, program_name_len;
  SESSION_PARAM *session_params = NULL;
  int error = NO_ERROR, update_parameter_values = 0;

  reply = OR_ALIGNED_BUF_START (a_reply);
  request_size = OR_INT_SIZE;	/* session_id */
  request_size += or_packed_stream_length (SERVER_SESSION_KEY_SIZE);
  request_size += sysprm_packed_session_parameters_length (cached_session_parameters, request_size);
  request_size += length_const_string (db_user, &db_user_len);
  request_size += length_const_string (host, &host_len);
  request_size += length_const_string (program_name, &program_name_len);

  reply = OR_ALIGNED_BUF_START (a_reply);

  request = (char *) malloc (request_size);
  if (request == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) request_size);
      return ER_FAILED;
    }

  ptr = or_pack_int (request, ((int) *session_id));
  ptr = or_pack_stream (ptr, server_session_key, SERVER_SESSION_KEY_SIZE);
  ptr = sysprm_pack_session_parameters (ptr, cached_session_parameters);
  ptr = pack_const_string_with_length (ptr, db_user, db_user_len);
  ptr = pack_const_string_with_length (ptr, host, host_len);
  ptr = pack_const_string_with_length (ptr, program_name, program_name_len);

  req_error =
    net_client_request2 (NET_SERVER_SES_CHECK_SESSION, request, request_size, reply, OR_ALIGNED_BUF_SIZE (a_reply),
			 NULL, 0, &area, &area_size);

  if (req_error == NO_ERROR)
    {
      ptr = or_unpack_int (reply, &area_size);
      ptr = or_unpack_int (ptr, &error);
      if (error != NO_ERROR)
	{
	  free_and_init (request);
	  return error;
	}

      if (area_size > 0)
	{
	  ptr = or_unpack_int (area, (int *) session_id);
	  ptr = or_unpack_int (ptr, row_count);
	  ptr = or_unpack_stream (ptr, server_session_key, SERVER_SESSION_KEY_SIZE);
	  ptr = or_unpack_int (ptr, &update_parameter_values);
	  if (update_parameter_values)
	    {
	      ptr = sysprm_unpack_session_parameters (ptr, &session_params);
	    }

	  free_and_init (area);
	}
      if (update_parameter_values)
	{
	  /* session parameters were found in session state and must update parameter values */
	  if (session_params == NULL)
	    {
	      assert (er_errid () != NO_ERROR);
	      error = er_errid ();
	      if (error == NO_ERROR)
		{
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
		  error = ER_FAILED;
		}
	      free_and_init (request);
	      return error;
	    }
	  sysprm_update_client_session_parameters (session_params);
	}
      else
	{
	  /* use the values stored in cached_session_parameters */
	  sysprm_update_client_session_parameters (cached_session_parameters);
	}
      sysprm_free_session_parameters (&session_params);
    }
  free_and_init (request);

  return NO_ERROR;
#else
  int result = NO_ERROR;
  SESSION_ID id;

  THREAD_ENTRY *thread_p = enter_server ();

  if (db_Session_id == DB_EMPTY_SESSION)
    {
      result = xsession_create_new (thread_p, &id);
    }
  else
    {
      id = db_Session_id;
      if (xsession_check_session (thread_p, id) != NO_ERROR)
	{
	  /* create new session */
	  if (xsession_create_new (thread_p, &id) != NO_ERROR)
	    {
	      result = ER_FAILED;
	    }
	}
    }

  db_Session_id = id;
  *session_id = db_Session_id;

  /* get row count */
  if (result != ER_FAILED)
    {
      xsession_get_row_count (thread_p, row_count);
    }

  exit_server (*thread_p);

  return result;
#endif
}

/*
 * csession_end_session - end the session identified by session_id
 *
 * return	   : error code or NO_ERROR
 * session_id (in) : the id of the session to end
 */
int
csession_end_session (SESSION_ID session_id)
{
#if defined (CS_MODE)
  int req_error;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_request;
  char *request;
  char *ptr;

  reply = OR_ALIGNED_BUF_START (a_reply);
  request = OR_ALIGNED_BUF_START (a_request);

  ptr = or_pack_int (request, session_id);

  req_error =
    net_client_request (NET_SERVER_SES_END_SESSION, request, OR_ALIGNED_BUF_SIZE (a_request), reply,
			OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0, NULL, 0);
  if (req_error != NO_ERROR)
    {
      return ER_FAILED;
    }

  return NO_ERROR;
#else
  int result = NO_ERROR;

  THREAD_ENTRY *thread_p = enter_server ();

  result = xsession_end_session (thread_p, session_id);

  exit_server (*thread_p);

  return result;
#endif
}

/*
 * csession_set_row_count - set affected rows count for the current
 *				session
 * return    : error code or NO_ERROR
 * rows (in) : the count of affected rows
 * NOTE : the affected rows count is the number of rows affected by the last
 * INSERT, UPDATE or DELETE statement
 */
int
csession_set_row_count (int rows)
{
#if defined (CS_MODE)
  int req_error;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_request;
  char *request;
  char *ptr;

  reply = OR_ALIGNED_BUF_START (a_reply);
  request = OR_ALIGNED_BUF_START (a_request);

  ptr = or_pack_int (request, rows);

  req_error =
    net_client_request (NET_SERVER_SES_SET_ROW_COUNT, request, OR_ALIGNED_BUF_SIZE (a_request), reply,
			OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0, NULL, 0);
  if (req_error != NO_ERROR)
    {
      return ER_FAILED;
    }

  or_unpack_int (reply, &req_error);
  return req_error;
#else
  int result = NO_ERROR;

  THREAD_ENTRY *thread_p = enter_server ();

  result = xsession_set_row_count (thread_p, rows);

  exit_server (*thread_p);

  return result;
#endif
}

/*
 * csession_get_row_count - get affected rows count for the current
 *			    session
 * return    : error code or NO_ERROR
 * rows (out): the count of affected rows
 * NOTE : the affected rows count is the number of rows affected by the last
 * INSERT, UPDATE or DELETE statement
 */
int
csession_get_row_count (int *rows)
{
#if defined (CS_MODE)
  int req_error;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_request;
  char *request;

  reply = OR_ALIGNED_BUF_START (a_reply);
  request = OR_ALIGNED_BUF_START (a_request);

  req_error =
    net_client_request (NET_SERVER_SES_GET_ROW_COUNT, request, OR_ALIGNED_BUF_SIZE (a_request), reply,
			OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0, NULL, 0);
  if (req_error != NO_ERROR)
    {
      return ER_FAILED;
    }
  or_unpack_int (reply, &req_error);
  or_unpack_int (reply, rows);

  return NO_ERROR;
#else
  int result = NO_ERROR;

  THREAD_ENTRY *thread_p = enter_server ();

  result = xsession_get_row_count (thread_p, rows);

  exit_server (*thread_p);

  return result;
#endif
}

/*
 * csession_get_last_insert_id - get the value of the last update serial
 * return   : error code or NO_ERROR
 * value (out) : the value of the last insert id
 * update_last_insert_id(in): whether update the last insert id
 */
int
csession_get_last_insert_id (DB_VALUE * value, bool update_last_insert_id)
{
#if defined (CS_MODE)
  int req_error;
  OR_ALIGNED_BUF (OR_INT_SIZE + OR_INT_SIZE) a_reply;
  char *reply;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_request;
  char *request;
  char *ptr = NULL;
  char *data_reply = NULL;
  int data_size = 0;

  db_make_null (value);

  reply = OR_ALIGNED_BUF_START (a_reply);

  request = OR_ALIGNED_BUF_START (a_request);
  ptr = or_pack_int (request, update_last_insert_id);

  req_error =
    net_client_request2 (NET_SERVER_SES_GET_LAST_INSERT_ID, request, OR_ALIGNED_BUF_SIZE (a_request), reply,
			 OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0, &data_reply, &data_size);
  if (req_error != NO_ERROR)
    {
      db_make_null (value);
      req_error = ER_FAILED;
      goto cleanup;
    }

  /* data_size */
  ptr = or_unpack_int (reply, &req_error);
  /* req_error */
  ptr = or_unpack_int (ptr, &req_error);
  if (req_error != NO_ERROR || data_size == 0)
    {
      req_error = (req_error != NO_ERROR) ? req_error : ER_FAILED;
      goto cleanup;
    }

  or_unpack_value (data_reply, value);

cleanup:
  if (data_reply != NULL)
    {
      free_and_init (data_reply);
    }

  return req_error;
#else
  int result = NO_ERROR;
  THREAD_ENTRY *thread_p = enter_server ();

  result = xsession_get_last_insert_id (thread_p, value, update_last_insert_id);

  exit_server (*thread_p);

  return result;
#endif
}

/*
 * csession_reset_cur_insert_id - reset cur insert id as NULL
 * return   : error code or NO_ERROR
 */
int
csession_reset_cur_insert_id (void)
{
#if defined (CS_MODE)
  int req_error;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply;

  reply = OR_ALIGNED_BUF_START (a_reply);

  req_error =
    net_client_request (NET_SERVER_SES_RESET_CUR_INSERT_ID, NULL, 0, reply, OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0,
			NULL, 0);
  if (req_error != NO_ERROR)
    {
      return ER_FAILED;
    }
  or_unpack_int (reply, &req_error);

  return req_error;
#else
  int result = NO_ERROR;

  THREAD_ENTRY *thread_p = enter_server ();

  result = xsession_reset_cur_insert_id (thread_p);

  exit_server (*thread_p);

  return result;
#endif
}

/*
 * csession_create_prepared_statement () - create a prepared session statement
 * return	  : error code or NO_ERROR
 * name (in)	  : the name of the prepared statement
 * alias_print(in): the compiled statement string
 * stmt_info (in) : serialized prepared statement information
 * info_length(in): the size of the serialized buffer
 */
int
csession_create_prepared_statement (const char *name, const char *alias_print, char *stmt_info, int info_length)
{
#if defined (CS_MODE)
  int req_error;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *request = NULL;
  char *reply = NULL;
  char *ptr = NULL;
  int req_size = 0, name_len = 0, alias_print_len = 0;
  SHA1Hash alias_sha1 = SHA1_HASH_INITIALIZER;

  reply = OR_ALIGNED_BUF_START (a_reply);

  req_size = 0;
  /* packed size for name */
  req_size += length_const_string (name, &name_len);
  /* packed size for alias_print */
  req_size += length_const_string (alias_print, &alias_print_len);
  /* data_size */
  req_size += OR_INT_SIZE;
  if (alias_print != NULL)
    {
      /* sha1 */
      req_size += OR_SHA1_SIZE;
    }

  request = (char *) malloc (req_size);
  if (request == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) req_size);
      req_error = ER_FAILED;
      goto cleanup;
    }

  /* name */
  ptr = pack_const_string_with_length (request, name, name_len);
  /* alias_print */
  ptr = pack_const_string_with_length (ptr, alias_print, alias_print_len);
  /* data size */
  ptr = or_pack_int (ptr, info_length);

  if (alias_print)
    {
      req_error = SHA1Compute ((const unsigned char *) alias_print, (unsigned) strlen (alias_print), &alias_sha1);
      if (req_error != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  goto cleanup;
	}
      ptr = or_pack_sha1 (ptr, &alias_sha1);
    }

  req_error =
    net_client_request (NET_SERVER_SES_CREATE_PREPARED_STATEMENT, request, req_size, reply,
			OR_ALIGNED_BUF_SIZE (a_reply), stmt_info, info_length, NULL, 0);
  if (req_error != NO_ERROR)
    {
      goto cleanup;
    }
  ptr = or_unpack_int (reply, &req_error);

cleanup:
  if (request != NULL)
    {
      free_and_init (request);
    }
  return req_error;
#else
  int result = NO_ERROR;
  char *local_name = NULL;
  char *local_alias_print = NULL;
  char *local_stmt_info = NULL;
  size_t len = 0;
  SHA1Hash alias_sha1 = SHA1_HASH_INITIALIZER;

  THREAD_ENTRY *thread_p = enter_server ();

  /* The server keeps a packed version of the prepared statement information and we need to pack it here */

  /* copy name */
  if (name != NULL)
    {
      len = strlen (name);
      local_name = (char *) malloc (len + 1);
      if (local_name == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, len + 1);
	  result = ER_FAILED;
	  goto error;
	}
      memcpy (local_name, name, len);
      local_name[len] = 0;
    }

  /* copy alias print */
  if (alias_print != NULL)
    {
      len = strlen (alias_print);
      local_alias_print = (char *) malloc (len + 1);
      if (local_alias_print == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, len + 1);
	  result = ER_FAILED;
	  goto error;
	}
      memcpy (local_alias_print, alias_print, len);
      local_alias_print[len] = 0;

      result = SHA1Compute ((const unsigned char *) alias_print, (unsigned) strlen (alias_print), &alias_sha1);
      if (result != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  return result;
	}
    }

  /* copy stmt_info */
  if (stmt_info != NULL)
    {
      local_stmt_info = (char *) malloc (info_length);
      if (local_stmt_info == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) info_length);
	  result = ER_FAILED;
	  goto error;
	}
      memcpy (local_stmt_info, stmt_info, info_length);
    }

  result =
    xsession_create_prepared_statement (thread_p, local_name, local_alias_print, &alias_sha1, local_stmt_info,
					info_length);
  if (result != NO_ERROR)
    {
      goto error;
    }

  exit_server (*thread_p);

  return result;

error:

  if (local_name != NULL)
    {
      free_and_init (local_name);
    }
  if (local_alias_print != NULL)
    {
      free_and_init (local_alias_print);
    }
  if (local_stmt_info != NULL)
    {
      free_and_init (local_stmt_info);
    }

  exit_server (*thread_p);

  return result;
#endif
}

/*
 * csession_get_prepared_statement () - get information about a prepared session statement
 * return	       : error code or no error
 * name (in)	       : the name of the prepared statement
 * xasl_id (out)       : XASL id
 * stmt_info (out)     : serialized prepared statement information
 * xasl_header_p (out) : if pointer is not null, request XASL node header along with XASL_ID.
 */
int
csession_get_prepared_statement (const char *name, XASL_ID * xasl_id, char **stmt_info,
				 XASL_NODE_HEADER * xasl_header_p)
{
#if defined (CS_MODE)
  int req_error = NO_ERROR;
  OR_ALIGNED_BUF (OR_INT_SIZE * 2 + OR_XASL_ID_SIZE) a_reply;
  char *reply = NULL;
  char *reply_data = NULL;
  char *request = NULL;
  char *ptr = NULL;
  int req_size = 0, len = 0;
  int reply_size = 0;
  int get_xasl_header = xasl_header_p != NULL;

  INIT_XASL_NODE_HEADER (xasl_header_p);

  req_size = (or_packed_string_length (name, &len)	/* name */
	      + OR_INT_SIZE /* get_xasl_header */ );
  request = (char *) malloc (req_size);
  if (request == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) req_size);
      req_error = ER_OUT_OF_VIRTUAL_MEMORY;
      goto error;
    }

  ptr = or_pack_string_with_length (request, name, len);
  ptr = or_pack_int (ptr, get_xasl_header);

  req_error =
    net_client_request2 (NET_SERVER_SES_GET_PREPARED_STATEMENT, request, req_size, OR_ALIGNED_BUF_START (a_reply),
			 OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0, &reply_data, &reply_size);
  if (req_error != NO_ERROR)
    {
      goto error;
    }

  reply = OR_ALIGNED_BUF_START (a_reply);
  /* unpack reply_size */
  ptr = or_unpack_int (reply, &req_error);
  /* unpack error code */
  ptr = or_unpack_int (ptr, &req_error);
  if (req_error != NO_ERROR)
    {
      /* there was an error fetching session data */
      goto error;
    }
  OR_UNPACK_XASL_ID (ptr, xasl_id);

  ptr = or_unpack_string_alloc (reply_data, stmt_info);
  if (*stmt_info == NULL)
    {
      req_error = ER_GENERIC_ERROR;
      goto error;
    }
  if (get_xasl_header)
    {
      OR_UNPACK_XASL_NODE_HEADER (ptr, xasl_header_p);
    }

  if (request != NULL)
    {
      free_and_init (request);
    }
  if (reply_data != NULL)
    {
      free_and_init (reply_data);
    }
  return req_error;

error:
  if (request != NULL)
    {
      free_and_init (request);
    }
  if (reply_data != NULL)
    {
      free_and_init (reply_data);
    }

  return req_error;
#else
  int result = NO_ERROR;
  int stmt_info_len = 0;

  THREAD_ENTRY *thread_p = enter_server ();

  INIT_XASL_NODE_HEADER (xasl_header_p);
  result = xsession_get_prepared_statement (thread_p, name, stmt_info, &stmt_info_len, xasl_id, xasl_header_p);

  exit_server (*thread_p);

  return result;
#endif
}

/*
 * csession_delete_prepared_statement () - delete a prepared session statement
 * return    : error code or no error
 * name (in) : name of the prepared statement
 */
int
csession_delete_prepared_statement (const char *name)
{
#if defined (CS_MODE)
  int req_error;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *request = NULL;
  int name_len, req_len;

  req_len = length_const_string (name, &name_len);

  request = (char *) malloc (req_len);
  if (request == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) req_len);
      return ER_FAILED;
    }

  pack_const_string_with_length (request, name, name_len);

  req_error =
    net_client_request (NET_SERVER_SES_DELETE_PREPARED_STATEMENT, request, req_len, OR_ALIGNED_BUF_START (a_reply),
			OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0, NULL, 0);
  if (request != NULL)
    {
      free_and_init (request);
    }

  if (req_error != NO_ERROR)
    {
      return ER_FAILED;
    }

  or_unpack_int (OR_ALIGNED_BUF_START (a_reply), &req_error);

  return req_error;
#else
  int result = NO_ERROR;

  THREAD_ENTRY *thread_p = enter_server ();

  result = xsession_delete_prepared_statement (thread_p, name);

  exit_server (*thread_p);

  return result;
#endif
}

/*
 * clogin_user () - login user
 * return	  : error code or NO_ERROR
 * username (in) : name of the user
 */
int
clogin_user (const char *username)
{
#if defined (CS_MODE)
  int req_error;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *request = NULL;
  int username_len, req_len;

  req_len = length_const_string (username, &username_len);

  request = (char *) malloc (req_len);
  if (request == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) req_len);
      return ER_FAILED;
    }

  pack_const_string_with_length (request, username, username_len);

  req_error =
    net_client_request (NET_SERVER_AU_LOGIN_USER, request, req_len, OR_ALIGNED_BUF_START (a_reply),
			OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0, NULL, 0);
  if (request != NULL)
    {
      free_and_init (request);
    }

  if (req_error != NO_ERROR)
    {
      return ER_FAILED;
    }

  or_unpack_int (OR_ALIGNED_BUF_START (a_reply), &req_error);

  return req_error;
#else
  int result = NO_ERROR;

  THREAD_ENTRY *thread_p = enter_server ();

  result = xlogin_user (thread_p, username);

  exit_server (*thread_p);

  return result;
#endif
}

/*
 * csession_set_session_variables () - set session variables
 * return	  : error code or NO_ERROR
 * variables (in) : variables
 * count (in)	  : count
 */
int
csession_set_session_variables (DB_VALUE * variables, const int count)
{
#if defined (CS_MODE)
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_request;
  char *request = NULL, *data_request = NULL, *ptr = NULL;
  int req_size = 0, i = 0, err = NO_ERROR;

  request = OR_ALIGNED_BUF_START (a_request);
  or_pack_int (request, count);

  req_size = 0;
  for (i = 0; i < count; i++)
    {
      req_size += OR_VALUE_ALIGNED_SIZE (&variables[i]);
    }

  data_request = (char *) malloc (req_size);
  if (data_request == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) req_size);
      err = ER_FAILED;
      goto cleanup;
    }

  ptr = data_request;
  for (i = 0; i < count; i++)
    {
      ptr = or_pack_db_value (ptr, &variables[i]);
    }

  err =
    net_client_request (NET_SERVER_SES_SET_SESSION_VARIABLES, OR_ALIGNED_BUF_START (a_request),
			OR_ALIGNED_BUF_SIZE (a_request), OR_ALIGNED_BUF_START (a_reply), OR_ALIGNED_BUF_SIZE (a_reply),
			data_request, req_size, NULL, 0);
  if (err != NO_ERROR)
    {
      goto cleanup;
    }

  or_unpack_int (OR_ALIGNED_BUF_START (a_reply), &err);

cleanup:
  if (data_request != NULL)
    {
      free_and_init (data_request);
    }

  return err;
#else
  int err = 0;

  THREAD_ENTRY *thread_p = enter_server ();

  err = xsession_set_session_variables (thread_p, variables, count);

  exit_server (*thread_p);

  return err;
#endif
}

/*
 * csession_drop_session_variables () - drop session variables
 * return	  : error code or NO_ERROR
 * variables (in) : variables
 * count (in)	  : count
 */
int
csession_drop_session_variables (DB_VALUE * variables, const int count)
{
#if defined (CS_MODE)
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_request;
  char *request = NULL, *data_request = NULL, *ptr = NULL;
  int req_size = 0, i = 0, err = NO_ERROR;

  request = OR_ALIGNED_BUF_START (a_request);
  or_pack_int (request, count);

  req_size = 0;
  for (i = 0; i < count; i++)
    {
      req_size += OR_VALUE_ALIGNED_SIZE (&variables[i]);
    }

  data_request = (char *) malloc (req_size);
  if (data_request == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) req_size);
      err = ER_FAILED;
      goto cleanup;
    }

  ptr = data_request;
  for (i = 0; i < count; i++)
    {
      ptr = or_pack_db_value (ptr, &variables[i]);
    }

  err =
    net_client_request (NET_SERVER_SES_DROP_SESSION_VARIABLES, OR_ALIGNED_BUF_START (a_request),
			OR_ALIGNED_BUF_SIZE (a_request), OR_ALIGNED_BUF_START (a_reply), OR_ALIGNED_BUF_SIZE (a_reply),
			data_request, req_size, NULL, 0);
  if (err != NO_ERROR)
    {
      goto cleanup;
    }

  or_unpack_int (OR_ALIGNED_BUF_START (a_reply), &err);

cleanup:
  if (data_request != NULL)
    {
      free_and_init (data_request);
    }

  return err;
#else
  int err = 0;

  THREAD_ENTRY *thread_p = enter_server ();

  err = xsession_drop_session_variables (thread_p, variables, count);

  exit_server (*thread_p);

  return err;
#endif
}

/*
 * csession_get_variable () - get the value of a session variable
 * return : error code or NO_ERROR
 * name (in)	: the name of the session variable
 * value (out)	: the value of the session variable
 */
int
csession_get_variable (DB_VALUE * name, DB_VALUE * value)
{
#if defined (CS_MODE)
  OR_ALIGNED_BUF (OR_INT_SIZE * 2) a_reply;
  char *request = NULL, *reply = NULL, *ptr = NULL;
  int req_size = 0, err = NO_ERROR, reply_size = 0;

  assert (value != NULL);

  req_size = OR_VALUE_ALIGNED_SIZE (name);

  request = (char *) malloc (req_size);
  if (request == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) req_size);
      return ER_FAILED;
    }

  or_pack_db_value (request, name);

  err =
    net_client_request2 (NET_SERVER_SES_GET_SESSION_VARIABLE, request, req_size, OR_ALIGNED_BUF_START (a_reply),
			 OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0, &reply, &reply_size);
  if (err != NO_ERROR)
    {
      goto cleanup;
    }

  /* a_reply contains reply_size code and error code */
  ptr = OR_ALIGNED_BUF_START (a_reply);
  ptr = or_unpack_int (ptr, &err);
  ptr = or_unpack_int (ptr, &err);
  if (err != NO_ERROR)
    {
      goto cleanup;
    }

  or_unpack_db_value (reply, value);

cleanup:
  if (request != NULL)
    {
      free_and_init (request);
    }
  if (reply != NULL)
    {
      free_and_init (reply);
    }

  return err;
#else
  int err = NO_ERROR;
  DB_VALUE *val_ref = NULL;

  THREAD_ENTRY *thread_p = enter_server ();
  /* we cannot use the allocation methods from the server context so we will just get a reference here */
  err = xsession_get_session_variable_no_copy (thread_p, name, &val_ref);

  exit_server (*thread_p);

  if (err == NO_ERROR)
    {
      assert (val_ref != NULL);
      /* create a clone of this value to be used in the client scope */
      pr_clone_value (val_ref, value);
    }
  else
    {
      db_make_null (value);
    }
  return err;
#endif
}

/*
 * boot_soft_rename -
 *
 * return:
 *
 * NOTE:
 */
int
boot_soft_rename (const char *old_db_name, const char *new_db_name, const char *new_db_path, const char *new_log_path,
		  const char *new_db_server_host, const char *new_volext_path, const char *fileof_vols_and_renamepaths,
		  bool new_db_overwrite, bool extern_rename, bool force_delete)
{
#if defined(CS_MODE)
  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ONLY_IN_STANDALONE, 1, "install database");
  return ER_FAILED;
#else /* CS_MODE */
  int error_code;

  THREAD_ENTRY *thread_p = enter_server ();

  error_code =
    xboot_soft_rename (thread_p, old_db_name, new_db_name, new_db_path, new_log_path, new_db_server_host,
		       new_volext_path, fileof_vols_and_renamepaths, new_db_overwrite, extern_rename, force_delete);

  exit_server (*thread_p);

  return error_code;
#endif /* !CS_MODE */
}

/*
 * boot_copy -
 *
 * return:
 *
 * NOTE:
 */
int
boot_copy (const char *from_dbname, const char *new_db_name, const char *new_db_path, const char *new_log_path,
	   const char *new_lob_path, const char *new_db_server_host, const char *new_volext_path,
	   const char *fileof_vols_and_copypaths, bool new_db_overwrite)
{
#if defined(CS_MODE)
  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ONLY_IN_STANDALONE, 1, "copy database");
  return ER_FAILED;
#else /* CS_MODE */
  int error_code;

  THREAD_ENTRY *thread_p = enter_server ();

  error_code =
    xboot_copy (thread_p, from_dbname, new_db_name, new_db_path, new_log_path, new_lob_path, new_db_server_host,
		new_volext_path, fileof_vols_and_copypaths, new_db_overwrite);
  assert (thread_p == NULL);

  exit_server_no_thread_entry ();

  return error_code;
#endif /* !CS_MODE */
}

/*
 * boot_emergency_patch -
 *
 * return:
 *
 * NOTE:
 */
int
boot_emergency_patch (const char *db_name, bool recreate_log, DKNPAGES log_npages, const char *db_locale, FILE * out_fp)
{
#if defined(CS_MODE)
  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ONLY_IN_STANDALONE, 1, "emergency patch");
  return ER_FAILED;
#else /* CS_MODE */
  int error_code;

  enter_server_no_thread_entry ();

  error_code = xboot_emergency_patch (db_name, recreate_log, log_npages, db_locale, out_fp);

  exit_server_no_thread_entry ();

  return error_code;
#endif /* !CS_MODE */
}

/*
 * boot_change_ha_mode - change server's HA state
 *   return: new state
 */
HA_SERVER_STATE
boot_change_ha_mode (HA_SERVER_STATE state, bool force, int timeout)
{
#if defined(CS_MODE)
  int req_error;
  int server_state;
  HA_SERVER_STATE cur_state = HA_SERVER_STATE_NA;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply;
  OR_ALIGNED_BUF (OR_INT_SIZE + OR_INT_SIZE + OR_INT_SIZE) a_request;
  char *request;
  char *ptr;

  reply = OR_ALIGNED_BUF_START (a_reply);
  request = OR_ALIGNED_BUF_START (a_request);

  ptr = or_pack_int (request, (int) state);
  ptr = or_pack_int (ptr, (int) force);
  ptr = or_pack_int (ptr, (int) timeout);

  req_error =
    net_client_request (NET_SERVER_BO_CHANGE_HA_MODE, request, OR_ALIGNED_BUF_SIZE (a_request), reply,
			OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0, NULL, 0);
  if (!req_error)
    {
      or_unpack_int (reply, &server_state);
      cur_state = (HA_SERVER_STATE) server_state;
    }

  return cur_state;
#else /* CS_MODE */
  /* Cannot run in standalone mode */
  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_NOT_IN_STANDALONE, 1, "changemode");
  return HA_SERVER_STATE_NA;
#endif /* !CS_MODE */
}

/*
 * boot_notify_ha_log_applier_state - notify log applier's state to the server
 *   return: NO_ERROR or ER_FAILED
 */
int
boot_notify_ha_log_applier_state (HA_LOG_APPLIER_STATE state)
{
#if defined(CS_MODE)
  int req_error, status = ER_FAILED;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_request;
  char *request;
  char *ptr;

  reply = OR_ALIGNED_BUF_START (a_reply);
  request = OR_ALIGNED_BUF_START (a_request);

  ptr = or_pack_int (request, (int) state);

  req_error =
    net_client_request (NET_SERVER_BO_NOTIFY_HA_LOG_APPLIER_STATE, request, OR_ALIGNED_BUF_SIZE (a_request), reply,
			OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0, NULL, 0);
  if (!req_error)
    {
      or_unpack_int (reply, &status);
    }

  return status;
#else /* CS_MODE */
  /* Cannot run in standalone mode */
  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_NOT_IN_STANDALONE, 1, "log applier");
  return ER_FAILED;
#endif /* !CS_MODE */
}

/*
 * stats_get_statistics_from_server () -
 *
 * return:
 *
 *   classoid(in):
 *   timestamp(in):
 *   length_ptr(in):
 *
 * NOTE:
 */
char *
stats_get_statistics_from_server (OID * classoid, unsigned int timestamp, int *length_ptr)
{
#if defined(CS_MODE)
  int req_error;
  char *area = NULL;
  char *ptr;
  OR_ALIGNED_BUF (OR_OID_SIZE + OR_INT_SIZE) a_request;
  char *request;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply;

  request = OR_ALIGNED_BUF_START (a_request);
  reply = OR_ALIGNED_BUF_START (a_reply);

  ptr = or_pack_oid (request, classoid);
  ptr = or_pack_int (ptr, (int) timestamp);

  req_error =
    net_client_request2 (NET_SERVER_QST_GET_STATISTICS, request, OR_ALIGNED_BUF_SIZE (a_request), reply,
			 OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0, &area, length_ptr);
  if (!req_error)
    {
      or_unpack_int (reply, length_ptr);
      return area;
    }
  else
    {
      return NULL;
    }
#else /* CS_MODE */
  char *area;

  THREAD_ENTRY *thread_p = enter_server ();

  area = xstats_get_statistics_from_server (thread_p, classoid, timestamp, length_ptr);

  exit_server (*thread_p);

  return area;
#endif /* !CS_MODE */
}

/*
 * stats_update_statistics -
 *
 * return:
 *
 *   classoid(in):
 *   with_fullscan(in):
 *
 * NOTE:
 */
int
stats_update_statistics (OID * classoid, int with_fullscan)
{
#if defined(CS_MODE)
  int error = ER_NET_CLIENT_DATA_RECEIVE;
  int req_error;
  OR_ALIGNED_BUF (OR_OID_SIZE + OR_INT_SIZE) a_request;
  char *request;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply;
  char *ptr;

  request = OR_ALIGNED_BUF_START (a_request);
  reply = OR_ALIGNED_BUF_START (a_reply);

  ptr = or_pack_oid (request, classoid);
  ptr = or_pack_int (ptr, with_fullscan);

  req_error =
    net_client_request (NET_SERVER_QST_UPDATE_STATISTICS, request, OR_ALIGNED_BUF_SIZE (a_request), reply,
			OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0, NULL, 0);
  if (!req_error)
    {
      or_unpack_errcode (reply, &error);
    }

  return error;
#else /* CS_MODE */
  int success;

  THREAD_ENTRY *thread_p = enter_server ();

  success = xstats_update_statistics (thread_p, classoid, (with_fullscan ? STATS_WITH_FULLSCAN : STATS_WITH_SAMPLING));

  exit_server (*thread_p);

  return success;
#endif /* !CS_MODE */
}

/*
 * stats_update_all_statistics -
 *
 * return:
 *   with_fullscan(in): true iff WITH FULLSCAN
 *
 * NOTE:
 */
int
stats_update_all_statistics (int with_fullscan)
{
#if defined(CS_MODE)
  int error = ER_NET_CLIENT_DATA_RECEIVE;
  int req_error;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_request;
  char *request;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply;
  char *ptr;

  request = OR_ALIGNED_BUF_START (a_request);
  reply = OR_ALIGNED_BUF_START (a_reply);

  ptr = or_pack_int (request, with_fullscan);

  req_error =
    net_client_request (NET_SERVER_QST_UPDATE_ALL_STATISTICS, request, OR_ALIGNED_BUF_SIZE (a_request), reply,
			OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0, NULL, 0);
  if (!req_error)
    {
      or_unpack_errcode (reply, &error);
    }

  return error;
#else /* CS_MODE */
  int success;

  THREAD_ENTRY *thread_p = enter_server ();

  success = xstats_update_all_statistics (thread_p, (with_fullscan ? STATS_WITH_FULLSCAN : STATS_WITH_SAMPLING));

  exit_server (*thread_p);

  return success;
#endif /* !CS_MODE */
}

/*
 * btree_add_index () -
 *
 * return:
 *
 *   btid(in):
 *   key_type(in):
 *   class_oid(in):
 *   attr_id(in):
 *   unique_pk(in):
 *
 * NOTE:
 */
int
btree_add_index (BTID * btid, TP_DOMAIN * key_type, OID * class_oid, int attr_id, int unique_pk)
{
#if defined(CS_MODE)
  int error = NO_ERROR;
  int req_error, request_size, domain_size;
  char *ptr;
  char *request;
  OR_ALIGNED_BUF (OR_INT_SIZE + OR_BTID_ALIGNED_SIZE) a_reply;
  char *reply;

  reply = OR_ALIGNED_BUF_START (a_reply);

  domain_size = or_packed_domain_size (key_type, 0);
  request_size = OR_BTID_ALIGNED_SIZE + domain_size + OR_OID_SIZE + OR_INT_SIZE + OR_INT_SIZE;

  request = (char *) malloc (request_size);
  if (request == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) request_size);
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  ptr = or_pack_btid (request, btid);
  ptr = or_pack_domain (ptr, key_type, 0, 0);
  ptr = or_pack_oid (ptr, class_oid);
  ptr = or_pack_int (ptr, attr_id);
  ptr = or_pack_int (ptr, unique_pk);

  req_error =
    net_client_request (NET_SERVER_BTREE_ADDINDEX, request, request_size, reply, OR_ALIGNED_BUF_SIZE (a_reply),
			NULL, 0, NULL, 0);
  if (!req_error)
    {
      ptr = or_unpack_int (reply, &error);
      ptr = or_unpack_btid (ptr, btid);
      if (error != NO_ERROR)
	{
	  btid = NULL;
	}
    }

  free_and_init (request);

  return error;
#else /* CS_MODE */
  int error = NO_ERROR;

  THREAD_ENTRY *thread_p = enter_server ();

  btid = xbtree_add_index (thread_p, btid, key_type, class_oid, attr_id, unique_pk, 0, 0, 0);
  if (btid == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
    }

  exit_server (*thread_p);

  return error;
#endif /* !CS_MODE */
}

/*
 * btree_load_index -
 *
 * return:
 *
 *   btid(in):
 *   bt_name(in):
 *   key_type(in):
 *   class_oids(in):
 *   n_classes(in):
 *   n_attrs(in):
 *   attr_ids(in):
 *   hfids(in):
 *   unique_pk(in):
 *   fk_refcls_oid(in):
 *   fk_refcls_pk_btid(in):
 *   fk_name(in):
 *
 * NOTE:
 */
int
btree_load_index (BTID * btid, const char *bt_name, TP_DOMAIN * key_type, OID * class_oids, int n_classes, int n_attrs,
		  int *attr_ids, int *attrs_prefix_length, HFID * hfids, int unique_pk, int not_null_flag,
		  OID * fk_refcls_oid, BTID * fk_refcls_pk_btid, const char *fk_name, char *pred_stream,
		  int pred_stream_size, char *expr_stream, int expr_stream_size, int func_col_id,
		  int func_attr_index_start)
{
#if defined(CS_MODE)
  int error = NO_ERROR, req_error, request_size, domain_size;
  char *ptr;
  char *request;
  OR_ALIGNED_BUF (OR_INT_SIZE + OR_BTID_ALIGNED_SIZE) a_reply;
  char *reply;
  int i, total_attrs, bt_strlen, fk_strlen;
  int index_info_size = 0;
  char *stream = NULL;
  int stream_size = 0;

  reply = OR_ALIGNED_BUF_START (a_reply);

  if (pred_stream && expr_stream)
    {
      return ER_FAILED;
    }

  index_info_size = (OR_INT_SIZE	/* index info type */
		     + (pred_stream ? OR_INT_SIZE : 0) + (expr_stream ? OR_INT_SIZE * 3 : 0));

  domain_size = or_packed_domain_size (key_type, 0);
  request_size = (OR_BTID_ALIGNED_SIZE	/* BTID */
		  + or_packed_string_length (bt_name, &bt_strlen)	/* index name */
		  + domain_size	/* key_type */
		  + (n_classes * OR_OID_SIZE)	/* class_oids */
		  + OR_INT_SIZE	/* n_classes */
		  + OR_INT_SIZE	/* n_attrs */
		  + (n_classes * n_attrs * OR_INT_SIZE)	/* attr_ids */
		  + ((n_classes == 1) ? (n_attrs * OR_INT_SIZE) : 0)	/* attrs_prefix_length */
		  + (n_classes * OR_HFID_SIZE)	/* hfids */
		  + OR_INT_SIZE	/* unique_pk */
		  + OR_INT_SIZE	/* not_null_flag */
		  + OR_OID_SIZE	/* fk_refcls_oid */
		  + OR_BTID_ALIGNED_SIZE	/* fk_refcls_pk_btid */
		  + or_packed_string_length (fk_name, &fk_strlen)	/* fk_name */
		  + index_info_size /* filter predicate or function index stream size */ );

  request = (char *) malloc (request_size);
  if (request == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) request_size);
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  ptr = or_pack_btid (request, btid);
  ptr = or_pack_string_with_length (ptr, bt_name, bt_strlen);
  ptr = or_pack_domain (ptr, key_type, 0, 0);
  ptr = or_pack_int (ptr, n_classes);
  ptr = or_pack_int (ptr, n_attrs);

  for (i = 0; i < n_classes; i++)
    {
      ptr = or_pack_oid (ptr, &class_oids[i]);
    }

  total_attrs = n_classes * n_attrs;
  for (i = 0; i < total_attrs; i++)
    {
      ptr = or_pack_int (ptr, attr_ids[i]);
    }

  if (n_classes == 1)
    {
      for (i = 0; i < n_attrs; i++)
	{
	  if (attrs_prefix_length)
	    {
	      ptr = or_pack_int (ptr, attrs_prefix_length[i]);
	    }
	  else
	    {
	      ptr = or_pack_int (ptr, -1);
	    }
	}
    }

  for (i = 0; i < n_classes; i++)
    {
      ptr = or_pack_hfid (ptr, &hfids[i]);
    }

  ptr = or_pack_int (ptr, unique_pk);
  ptr = or_pack_int (ptr, not_null_flag);

  ptr = or_pack_oid (ptr, fk_refcls_oid);
  ptr = or_pack_btid (ptr, fk_refcls_pk_btid);
  ptr = or_pack_string_with_length (ptr, fk_name, fk_strlen);

  if (pred_stream)
    {
      ptr = or_pack_int (ptr, 0);
      ptr = or_pack_int (ptr, pred_stream_size);
      stream = pred_stream;
      stream_size = pred_stream_size;
    }
  else if (expr_stream)
    {
      ptr = or_pack_int (ptr, 1);
      ptr = or_pack_int (ptr, expr_stream_size);
      ptr = or_pack_int (ptr, func_col_id);
      ptr = or_pack_int (ptr, func_attr_index_start);
      stream = expr_stream;
      stream_size = expr_stream_size;
    }
  else
    {
      ptr = or_pack_int (ptr, -1);	/* stream=NULL, stream_size=0 */
    }

  req_error =
    net_client_request (NET_SERVER_BTREE_LOADINDEX, request, request_size, reply, OR_ALIGNED_BUF_SIZE (a_reply),
			stream, stream_size, NULL, 0);

  if (!req_error)
    {
      ptr = or_unpack_int (reply, &error);
      ptr = or_unpack_btid (ptr, btid);
      if (error != NO_ERROR)
	{
	  btid = NULL;
	}
    }
  else
    {
      btid = NULL;
    }

  free_and_init (request);

  return error;
#else /* CS_MODE */
  int error = NO_ERROR;

  THREAD_ENTRY *thread_p = enter_server ();

  btid =
    xbtree_load_index (thread_p, btid, bt_name, key_type, class_oids, n_classes, n_attrs, attr_ids, attrs_prefix_length,
		       hfids, unique_pk, not_null_flag, fk_refcls_oid, fk_refcls_pk_btid, fk_name, pred_stream,
		       pred_stream_size, expr_stream, expr_stream_size, func_col_id, func_attr_index_start);
  if (btid == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
    }
  else
    {
      error = NO_ERROR;
    }

  exit_server (*thread_p);

  return error;
#endif /* !CS_MODE */
}

/*
 * btree_delete_index -
 *
 * return:
 *
 *   btid(in):
 *
 * NOTE:
 */
int
btree_delete_index (BTID * btid)
{
#if defined(CS_MODE)
  int req_error, status = NO_ERROR;
  OR_ALIGNED_BUF (OR_BTID_ALIGNED_SIZE) a_request;
  char *request;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply;

  request = OR_ALIGNED_BUF_START (a_request);
  reply = OR_ALIGNED_BUF_START (a_reply);

  (void) or_pack_btid (request, btid);

  req_error =
    net_client_request (NET_SERVER_BTREE_DELINDEX, request, OR_ALIGNED_BUF_SIZE (a_request), reply,
			OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0, NULL, 0);
  if (!req_error)
    {
      or_unpack_int (reply, &status);
    }

  return status;
#else /* CS_MODE */
  int success = ER_FAILED;

  THREAD_ENTRY *thread_p = enter_server ();

  success = xbtree_delete_index (thread_p, btid);

  exit_server (*thread_p);

  return success;
#endif /* !CS_MODE */
}

/*
 * locator_log_force_nologging -
 *
 * return:
 *
 * NOTE:
 */
int
locator_log_force_nologging (void)
{
#if defined(CS_MODE)
  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ONLY_IN_STANDALONE, 1, "no logging");
  return ER_FAILED;
#else /* CS_MODE */
  int success = ER_FAILED;

  THREAD_ENTRY *thread_p = enter_server ();

  success = log_set_no_logging ();

  exit_server (*thread_p);

  return success;
#endif /* !CS_MODE */
}

/*
 * locator_remove_class_from_index -
 *
 * return:
 *
 *   oid(in):
 *   btid(in):
 *   hfid(in):
 *
 * NOTE:
 */
int
locator_remove_class_from_index (OID * oid, BTID * btid, HFID * hfid)
{
#if defined(CS_MODE)
  char *request, *reply, *ptr;
  int req_error, status = NO_ERROR;
  OR_ALIGNED_BUF (OR_OID_SIZE + OR_BTID_ALIGNED_SIZE + OR_HFID_SIZE) a_request;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;

  request = OR_ALIGNED_BUF_START (a_request);
  reply = OR_ALIGNED_BUF_START (a_reply);

  ptr = or_pack_oid (request, oid);
  ptr = or_pack_btid (ptr, btid);
  ptr = or_pack_hfid (ptr, hfid);

  req_error =
    net_client_request (NET_SERVER_LC_REM_CLASS_FROM_INDEX, request, OR_ALIGNED_BUF_SIZE (a_request), reply,
			OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0, NULL, 0);
  if (!req_error)
    {
      or_unpack_int (reply, &status);
    }

  return status;
#else /* CS_MODE */
  int success = ER_FAILED;

  THREAD_ENTRY *thread_p = enter_server ();

  success = xlocator_remove_class_from_index (thread_p, oid, btid, hfid);

  exit_server (*thread_p);

  return success;
#endif /* !CS_MODE */
}

/*
 * btree_find_unique -
 *
 * return:
 *
 *   btid(in):
 *   key(in):
 *   class_oid(in):
 *   oid(in):
 *
 * NOTE:
 */
BTREE_SEARCH
btree_find_unique (BTID * btid, DB_VALUE * key, OID * class_oid, OID * oid)
{
#if defined(CS_MODE)
  BTREE_SEARCH status = BTREE_ERROR_OCCURRED;
  int req_error, request_size, key_size;
  char *ptr;
  char *request;
  OR_ALIGNED_BUF (OR_INT_SIZE + OR_OID_SIZE) a_reply;
  char *reply;
  enum net_server_request request_id;

  if (btid == NULL || key == NULL || class_oid == NULL || oid == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OBJ_INVALID_ARGUMENTS, 0);
      return BTREE_ERROR_OCCURRED;
    }

  reply = OR_ALIGNED_BUF_START (a_reply);

  key_size = OR_VALUE_ALIGNED_SIZE (key);
  request_size = key_size + OR_OID_SIZE + OR_BTID_ALIGNED_SIZE;

  request = (char *) malloc (request_size);
  if (request == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) request_size);
      return BTREE_ERROR_OCCURRED;
    }

  ptr = request;
  ptr = or_pack_value (ptr, key);
  request_id = NET_SERVER_BTREE_FIND_UNIQUE;

  ptr = or_pack_oid (ptr, class_oid);
  ptr = or_pack_btid (ptr, btid);

  /* reset request_size as real packed size */
  request_size = CAST_BUFLEN (ptr - request);

  req_error =
    net_client_request (request_id, request, request_size, reply, OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0, NULL, 0);
  if (!req_error)
    {
      int istatus;
      ptr = or_unpack_int (reply, &istatus);
      ptr = or_unpack_oid (ptr, oid);
      status = (BTREE_SEARCH) istatus;
    }
  else
    {
      OID_SET_NULL (oid);
    }

  free_and_init (request);
  return status;

#else /* CS_MODE */
  BTREE_SEARCH status = BTREE_ERROR_OCCURRED;

  if (btid == NULL || key == NULL || class_oid == NULL || oid == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OBJ_INVALID_ARGUMENTS, 0);
      return BTREE_ERROR_OCCURRED;
    }

  THREAD_ENTRY *thread_p = enter_server ();
  status = xbtree_find_unique (thread_p, btid, S_SELECT_WITH_LOCK, key, class_oid, oid, false);
  exit_server (*thread_p);
  return status;

#endif /* !CS_MODE */
}

/*
 * btree_find_multi_uniques () - search a list of indexes of a specified class for a list of values
 * return: index search result or BTREE_ERROR_OCCURRED
 * class_oid (in) : class OID
 * pruning_type (in) : pruning type
 * btids (in)	  : list of indexes to search
 * keys (in)	  : list of values to search for
 * count (in)	  : number of indexes
 * op_type (in)	  : operation type
 * oids (in/out)  : found OIDs
 * oids_count (in/out) : number of OIDs found
 */
BTREE_SEARCH
btree_find_multi_uniques (OID * class_oid, int pruning_type, BTID * btids, DB_VALUE * keys, int count,
			  SCAN_OPERATION_TYPE op_type, OID ** oids, int *oids_count)
{
#if defined (CS_MODE)
  int req_error, req_size, area_size, i;
  /* reply contains area size and return code */
  OR_ALIGNED_BUF (2 * OR_INT_SIZE) a_reply;
  char *request = NULL, *ptr = NULL;
  char *area = NULL;
  BTREE_SEARCH result = BTREE_KEY_NOTFOUND;

  /* compute request size */
  req_size = (OR_INT_SIZE	/* number of indexes to search */
	      + OR_INT_SIZE	/* needs pruning */
	      + OR_OID_SIZE	/* class OID */
	      + OR_INT_SIZE	/* operation type */
	      + (count * OR_BTID_ALIGNED_SIZE) /* indexes */ );

  for (i = 0; i < count; i++)
    {
      req_size += OR_VALUE_ALIGNED_SIZE (&keys[i]);
    }

  request = (char *) malloc (req_size);
  if (request == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) req_size);
      result = BTREE_ERROR_OCCURRED;
      goto cleanup;
    }

  /* pack oid */
  ptr = or_pack_oid (request, class_oid);
  /* needs pruning */
  ptr = or_pack_int (ptr, pruning_type);
  /* operation type */
  ptr = or_pack_int (ptr, op_type);
  /* number of indexes */
  ptr = or_pack_int (ptr, count);
  /* indexes */
  for (i = 0; i < count; i++)
    {
      ptr = or_pack_btid (ptr, &btids[i]);
    }
  /* values */
  for (i = 0; i < count; i++)
    {
      ptr = or_pack_value (ptr, &keys[i]);
    }

  req_error =
    net_client_request2 (NET_SERVER_BTREE_FIND_MULTI_UNIQUES, request, req_size, OR_ALIGNED_BUF_START (a_reply),
			 OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0, &area, &area_size);
  if (req_error != NO_ERROR)
    {
      result = BTREE_ERROR_OCCURRED;
      goto cleanup;
    }

  /* skip over area_size */
  ptr = or_unpack_int (OR_ALIGNED_BUF_START (a_reply), &area_size);

  /* unpack error code returned from server */
  ptr = or_unpack_int (ptr, &req_error);
  if (req_error != NO_ERROR)
    {
      result = BTREE_ERROR_OCCURRED;
      goto cleanup;
    }

  /* if no data was returned, no offending OID was found */
  if (area_size == 0)
    {
      *oids = NULL;
      result = BTREE_KEY_NOTFOUND;
      goto cleanup;
    }

  /* unpack number of returned OIDs */
  ptr = or_unpack_int (area, oids_count);
  if (*oids_count == 0)
    {
      assert (false);
      *oids = NULL;
      result = BTREE_KEY_NOTFOUND;
      goto cleanup;
    }
  else
    {
      assert (*oids_count > 0);
      result = BTREE_KEY_FOUND;
    }

  *oids = (OID *) malloc ((*oids_count) * sizeof (OID));
  if (*oids == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) req_size);
      result = BTREE_ERROR_OCCURRED;
      goto cleanup;
    }

  for (i = 0; i < *oids_count; i++)
    {
      ptr = or_unpack_oid (ptr, &((*oids)[i]));
    }

cleanup:
  if (request != NULL)
    {
      free_and_init (request);
    }
  if (area != NULL)
    {
      free_and_init (area);
    }
  if (result != BTREE_KEY_FOUND)
    {
      *oids_count = 0;
      *oids = NULL;
    }
  return result;

#else
  BTREE_SEARCH result = BTREE_KEY_NOTFOUND;
  OID *local_oids = NULL;
  int local_count = 0;

  THREAD_ENTRY *thread_p = enter_server ();

  result =
    xbtree_find_multi_uniques (thread_p, class_oid, pruning_type, btids, keys, count, op_type, &local_oids,
			       &local_count);
  if (result == BTREE_ERROR_OCCURRED)
    {
      exit_server (*thread_p);
      return result;
    }

  /* need to copy memory from server context to local context */
  if (local_count > 0)
    {
      *oids = (OID *) malloc (local_count * sizeof (OID));
      if (*oids == NULL)
	{
	  db_private_free (thread_p, local_oids);
	  exit_server (*thread_p);
	  return BTREE_ERROR_OCCURRED;
	}
      *oids_count = local_count;
      memcpy (*oids, local_oids, local_count * sizeof (OID));
      db_private_free (thread_p, local_oids);
    }

  exit_server (*thread_p);

  return result;
#endif
}

/*
 * btree_class_test_unique -
 *
 * return:
 *
 *   buf(in):
 *   buf_size(in):
 *
 * NOTE:
 */
int
btree_class_test_unique (char *buf, int buf_size)
{
#if defined(CS_MODE)
  int req_error, status = NO_ERROR;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply;

  reply = OR_ALIGNED_BUF_START (a_reply);

  req_error =
    net_client_request (NET_SERVER_BTREE_CLASS_UNIQUE_TEST, buf, buf_size, reply, OR_ALIGNED_BUF_SIZE (a_reply), NULL,
			0, NULL, 0);
  if (!req_error)
    {
      or_unpack_int (reply, &status);
    }

  return status;
#else /* CS_MODE */
  int success = ER_FAILED;

  THREAD_ENTRY *thread_p = enter_server ();

  success = xbtree_class_test_unique (thread_p, buf, buf_size);

  exit_server (*thread_p);

  return success;
#endif /* !CS_MODE */
}

/*
 * qfile_get_list_file_page -
 *
 * return:
 *
 *   query_id(in):
 *   volid(in):
 *   pageid(in):
 *   buffer(in):
 *   buffer_size(in):
 *
 * NOTE:
 */
int
qfile_get_list_file_page (QUERY_ID query_id, VOLID volid, PAGEID pageid, char *buffer, int *buffer_size)
{
#if defined(CS_MODE)
  int error = ER_NET_CLIENT_DATA_RECEIVE;
  int req_error;
  char *ptr;
  OR_ALIGNED_BUF (OR_PTR_SIZE + OR_INT_SIZE + OR_INT_SIZE) a_request;
  char *request;
  OR_ALIGNED_BUF (OR_INT_SIZE * 2) a_reply;
  char *reply;

  request = OR_ALIGNED_BUF_START (a_request);
  reply = OR_ALIGNED_BUF_START (a_reply);

  ptr = or_pack_ptr (request, query_id);
  ptr = or_pack_int (ptr, (int) volid);
  ptr = or_pack_int (ptr, (int) pageid);

  req_error =
    net_client_request2_no_malloc (NET_SERVER_LS_GET_LIST_FILE_PAGE, request, OR_ALIGNED_BUF_SIZE (a_request), reply,
				   OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0, buffer, buffer_size);
  if (!req_error)
    {
      ptr = or_unpack_int (&reply[OR_INT_SIZE], &error);
    }

  return error;
#else /* CS_MODE */
  int success;
  int page_size;

  THREAD_ENTRY *thread_p = enter_server ();

  success = xqfile_get_list_file_page (thread_p, query_id, volid, pageid, buffer, &page_size);

  exit_server (*thread_p);

  return success;
#endif /* !CS_MODE */
}

/*
 * qmgr_prepare_query - Send a SERVER_QM_PREPARE request to the server
 *
 * Send XASL stream and receive XASL file id (XASL_ID) as a result.
 * If xasl_buffer == NULL, the server will look up the XASL cache and then
 * return the cached XASL file id if found, otherwise return NULL.
 * This function is a counter part to sqmgr_prepare_query().
 *
 * return: Error Code.
 *
 *   context(in): query string & plan
 *   stream(in/out): xasl stream, size, xasl ID & xasl_header
 *
 * NOTE: If xasl_header_p is not null, also XASL node header will be requested
 */
int
qmgr_prepare_query (COMPILE_CONTEXT * context, XASL_STREAM * stream)
{
#if defined(CS_MODE)
  int error = NO_ERROR;
  int req_error, request_size = 0;
  int sql_hash_text_len, sql_plan_text_len, reply_buffer_size = 0;
  char *request = NULL, *reply = NULL, *ptr = NULL, *reply_buffer = NULL;
  OR_ALIGNED_BUF (OR_INT_SIZE + OR_INT_SIZE + OR_XASL_ID_SIZE) a_reply;
  int get_xasl_header = stream->xasl_header != NULL;

  INIT_XASL_NODE_HEADER (stream->xasl_header);

  reply = OR_ALIGNED_BUF_START (a_reply);

  /* sql hash text */
  request_size += length_const_string (context->sql_hash_text, &sql_hash_text_len);

  /* sql plan text */
  request_size += length_const_string (context->sql_plan_text, &sql_plan_text_len);

  /* sql user text */
  request_size += length_string_with_null_padding (context->sql_user_text_len);

  request_size += OR_INT_SIZE;	/* size */
  request_size += OR_INT_SIZE;	/* get_xasl_header */
  request_size += OR_INT_SIZE;	/* whether to pin xasl cache entry */
  request_size += OR_INT_SIZE;	/* whether to recompile the pinned xasl cache entry */
  request_size += OR_INT_SIZE;	/* context->recompile_xasl */
  request_size += OR_SHA1_SIZE;	/* sha1 */

  request = (char *) malloc (request_size);
  if (request == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) request_size);
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  /* pack query alias string as a request data */
  ptr = pack_const_string_with_length (request, context->sql_hash_text, sql_hash_text_len);
  /* pack query plan as a request data */
  ptr = pack_const_string_with_length (ptr, context->sql_plan_text, sql_plan_text_len);
  /* pack query string as a request data */
  ptr = pack_string_with_null_padding (ptr, context->sql_user_text, context->sql_user_text_len);

  /* pack size of XASL stream */
  ptr = or_pack_int (ptr, stream->buffer_size);
  /* Pack get_xasl_header. */
  ptr = or_pack_int (ptr, get_xasl_header);
  /* Pack context. */
  ptr = or_pack_int (ptr, (int) context->is_xasl_pinned_reference);
  ptr = or_pack_int (ptr, (int) context->recompile_xasl_pinned);
  ptr = or_pack_int (ptr, (int) context->recompile_xasl);
  ptr = or_pack_sha1 (ptr, &context->sha1);

  /* send SERVER_QM_QUERY_PREPARE request with request data and XASL stream; receive XASL file id (XASL_ID) as a reply */
  req_error =
    net_client_request2 (NET_SERVER_QM_QUERY_PREPARE, request, request_size, reply, OR_ALIGNED_BUF_SIZE (a_reply),
			 (char *) stream->buffer, stream->buffer_size, &reply_buffer, &reply_buffer_size);
  if (!req_error)
    {
      ptr = or_unpack_int (reply, &reply_buffer_size);
      ptr = or_unpack_int (ptr, &error);
      if (error == NO_ERROR)
	{
	  /* NULL XASL_ID will be returned when cache not found */
	  OR_UNPACK_XASL_ID (ptr, stream->xasl_id);
	  context->recompile_xasl = false;

	  if (reply_buffer != NULL && reply_buffer_size != 0)
	    {
	      ptr = reply_buffer;
	      if (get_xasl_header)
		{
		  OR_UNPACK_XASL_NODE_HEADER (ptr, stream->xasl_header);
		}
	      if (ptr < reply_buffer + reply_buffer_size)
		{
		  /* Doesn't really matter what it's packed... We need to force recompile; see sqmgr_prepare_query */
		  assert ((ptr + OR_INT_SIZE) == (reply_buffer + reply_buffer_size));
		  assert (!context->recompile_xasl);
		  context->recompile_xasl = true;
		}
	    }
	}
      else
	{
	  ASSERT_ERROR ();
	}
    }
  else
    {
      error = req_error;
    }

  if (request != NULL)
    {
      free_and_init (request);
    }

  if (reply_buffer != NULL)
    {
      free_and_init (reply_buffer);
    }

  return error;
#else /* CS_MODE */
  int error_code = NO_ERROR;
  XASL_STREAM server_stream;

  THREAD_ENTRY *thread_p = enter_server ();

  /* We cannot use the stream created on client context. XASL cache will save the stream in cache entry and it will
   * suppose the stream buffer was allocated using malloc.
   * Duplicate the stream for server context.
   */
  server_stream.xasl_id = stream->xasl_id;
  server_stream.xasl_header = stream->xasl_header;
  server_stream.buffer_size = stream->buffer_size;
  if (stream->buffer_size > 0)
    {
      server_stream.buffer = (char *) malloc (stream->buffer_size);
      if (server_stream.buffer == NULL)
	{
	  exit_server (*thread_p);

	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, stream->buffer_size);
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}
      memcpy (server_stream.buffer, stream->buffer, stream->buffer_size);
    }
  else
    {
      /* No stream. This is just a lookup for existing entry. */
      server_stream.buffer = NULL;
    }

  INIT_XASL_NODE_HEADER (server_stream.xasl_header);

  /* call the server routine of query prepare */
  error_code = xqmgr_prepare_query (thread_p, context, &server_stream);
  if (server_stream.buffer != NULL)
    {
      free_and_init (server_stream.buffer);
    }
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
    }

  exit_server (*thread_p);

  return error_code;

#endif /* !CS_MODE */
}


/*
 * qmgr_execute_query - Send a SERVER_QM_EXECUTE request to the server
 *
 * Send XASL file id and parameter values if exist and receive list file id
 * that contains query result. If an error occurs, return NULL QFILE_LIST_ID.
 * This function is a counter part to sqmgr_execute_query().
 */
/*
 * qmgr_execute_query -
 *
 * return:
 *
 *   xasl_id(in):
 *   query_idp(in):
 *   dbval_cnt(in):
 *   dbvals(in):
 *   flag(in):
 *   clt_cache_time(in):
 *   srv_cache_time(in):
 *   query_timeout(in):
 *   end_of_queries(in):
 *
 * NOTE:
 */
QFILE_LIST_ID *
qmgr_execute_query (const XASL_ID * xasl_id, QUERY_ID * query_idp, int dbval_cnt, const DB_VALUE * dbvals,
		    QUERY_FLAG flag, CACHE_TIME * clt_cache_time, CACHE_TIME * srv_cache_time, int query_timeout)
{
#if defined(CS_MODE)
  QFILE_LIST_ID *list_id = NULL;
  int req_error, senddata_size, replydata_size_listid, replydata_size_page, replydata_size_plan;
  char *request, *reply, *senddata = NULL;
  char *replydata_listid = NULL, *replydata_page = NULL, *replydata_plan = NULL, *ptr;
  OR_ALIGNED_BUF (OR_XASL_ID_SIZE + OR_INT_SIZE * 4 + OR_CACHE_TIME_SIZE
		  + EXECUTE_QUERY_MAX_ARGUMENT_DATA_SIZE) a_request;
  OR_ALIGNED_BUF (OR_INT_SIZE * 4 + OR_PTR_ALIGNED_SIZE + OR_CACHE_TIME_SIZE) a_reply;
  int i, request_len;
  const DB_VALUE *dbval;

  request = OR_ALIGNED_BUF_START (a_request);
  reply = OR_ALIGNED_BUF_START (a_reply);

  /* make send data using if parameter values for host variables are given */
  senddata_size = 0;
  for (i = 0, dbval = dbvals; i < dbval_cnt; i++, dbval++)
    {
      senddata_size += OR_VALUE_ALIGNED_SIZE ((DB_VALUE *) dbval);
    }
  if (senddata_size != 0)
    {
      senddata = (char *) malloc (senddata_size);
      if (senddata == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) senddata_size);
	  return NULL;
	}

      ptr = senddata;
      for (i = 0, dbval = dbvals; i < dbval_cnt; i++, dbval++)
	{
	  ptr = or_pack_db_value (ptr, (DB_VALUE *) dbval);
	}

      /* change senddata_size as real packing size */
      senddata_size = CAST_BUFLEN (ptr - senddata);
      if (senddata_size < EXECUTE_QUERY_MAX_ARGUMENT_DATA_SIZE)
	{
	  flag |= EXECUTE_QUERY_WITHOUT_DATA_BUFFERS;
	}
    }

  /* pack XASL file id (XASL_ID), number of parameter values, size of the send data, and query execution mode flag as a 
   * request data */
  ptr = request;
  OR_PACK_XASL_ID (ptr, xasl_id);
  ptr = or_pack_int (ptr, dbval_cnt);
  ptr = or_pack_int (ptr, senddata_size);
  ptr = or_pack_int (ptr, flag);
  OR_PACK_CACHE_TIME (ptr, clt_cache_time);
  ptr = or_pack_int (ptr, query_timeout);

  request_len = OR_XASL_ID_SIZE + OR_INT_SIZE * 4 + OR_CACHE_TIME_SIZE;
  if (IS_QUERY_EXECUTED_WITHOUT_DATA_BUFFERS (flag))
    {
      /* Execute without data buffers. The data has small size. Include the data in the argument buffer. */
      assert (senddata != NULL && 0 < senddata_size);

      memcpy (ptr, senddata, senddata_size);
      request_len += senddata_size;

      free_and_init (senddata);
      senddata_size = 0;
    }
  else
    {
      /* Execute with data buffer. The data has big size. */
    }

  req_error = net_client_request_with_callback (NET_SERVER_QM_QUERY_EXECUTE, request, request_len, reply,
						OR_ALIGNED_BUF_SIZE (a_reply), senddata, senddata_size, NULL, 0,
						&replydata_listid, &replydata_size_listid, &replydata_page,
						&replydata_size_page, &replydata_plan, &replydata_size_plan);
  if (replydata_plan != NULL)
    {
      db_set_execution_plan (replydata_plan, replydata_size_plan);
      free_and_init (replydata_plan);
    }

  if (senddata != NULL)
    {
      free_and_init (senddata);
    }

  if (!req_error)
    {
      /* first argument should be QUERY_END ptr = or_unpack_int(reply, &status); */
      /* second argument should be the same with replydata_size_listid ptr = or_unpack_int(ptr, &listid_length); */
      /* third argument should be the same with replydata_size_page ptr = or_unpack_int(ptr, &page_size); */
      /* third argument should be the same with replydata_size_plan ptr = or_unpack_int(ptr, &plan_size); */
      /* fourth argument should be query_id */
      ptr = or_unpack_ptr (reply + OR_INT_SIZE * 4, query_idp);
      OR_UNPACK_CACHE_TIME (ptr, srv_cache_time);

      if (replydata_listid && replydata_size_listid)
	{
	  /* unpack list file id of query result from the reply data */
	  ptr = or_unpack_unbound_listid (replydata_listid, (void **) (&list_id));
	  /* QFILE_LIST_ID shipped with last page */
	  if (replydata_size_page)
	    {
	      list_id->last_pgptr = replydata_page;
	    }
	  else
	    {
	      list_id->last_pgptr = NULL;
	    }
	  free_and_init (replydata_listid);
	}
    }

  return list_id;
#else /* CS_MODE */
  QFILE_LIST_ID *list_id = NULL;
  DB_VALUE *server_db_values = NULL;
  OID *oid;
  int i;

  THREAD_ENTRY *thread_p = enter_server ();

  /* reallocate dbvals to use server allocation */
  if (dbval_cnt > 0)
    {
      size_t s = dbval_cnt * sizeof (DB_VALUE);

      server_db_values = (DB_VALUE *) db_private_alloc (thread_p, s);
      if (server_db_values == NULL)
	{
	  goto cleanup;
	}
      for (i = 0; i < dbval_cnt; i++)
	{
	  db_make_null (&server_db_values[i]);
	}
      for (i = 0; i < dbval_cnt; i++)
	{
	  switch (DB_VALUE_TYPE (&dbvals[i]))
	    {
	    case DB_TYPE_OBJECT:
	      /* server cannot handle objects, convert to OID instead */
	      oid = ws_identifier (db_get_object (&dbvals[i]));
	      if (oid != NULL)
		{
		  db_make_oid (&server_db_values[i], oid);
		}
	      break;

	    default:
	      /* Clone value */
	      if (db_value_clone ((DB_VALUE *) (&dbvals[i]), &server_db_values[i]) != NO_ERROR)
		{
		  goto cleanup;
		}
	      break;
	    }
	}
    }
  else
    {
      /* No dbvals */
      server_db_values = NULL;
    }

  /* call the server routine of query execute */
  list_id =
    xqmgr_execute_query (thread_p, xasl_id, query_idp, dbval_cnt, server_db_values, &flag, clt_cache_time,
			 srv_cache_time, query_timeout, NULL);

cleanup:
  if (server_db_values != NULL)
    {
      for (i = 0; i < dbval_cnt; i++)
	{
	  db_value_clear (&server_db_values[i]);
	}
      db_private_free (thread_p, server_db_values);
    }

  exit_server (*thread_p);

  return list_id;
#endif /* !CS_MODE */
}

/*
 * qmgr_prepare_and_execute_query -
 *
 * return:
 *
 *   xasl_stream(in):
 *   xasl_stream_size(in):
 *   query_id(in):
 *   dbval_cnt(in):
 *   dbval_ptr(in):
 *   flag(in):
 *   query_timeout(in):
 *
 * NOTE:
 */
QFILE_LIST_ID *
qmgr_prepare_and_execute_query (char *xasl_stream, int xasl_stream_size, QUERY_ID * query_idp, int dbval_cnt,
				DB_VALUE * dbval_ptr, QUERY_FLAG flag, int query_timeout)
{
#if defined(CS_MODE)
  QFILE_LIST_ID *regu_result = NULL;
  int req_error, senddata_size, replydata_size_listid, replydata_size_page;
  int i, size;
  char *ptr, *senddata, *replydata;
  DB_VALUE *dbval;
  OR_ALIGNED_BUF (OR_INT_SIZE * 4) a_request;
  char *request;
  OR_ALIGNED_BUF (OR_INT_SIZE * 4 + OR_PTR_ALIGNED_SIZE) a_reply;
  char *reply;
  char *page_ptr;
  int page_size, dummy_plan_size;

  request = OR_ALIGNED_BUF_START (a_request);
  reply = OR_ALIGNED_BUF_START (a_reply);

  senddata = NULL;
  senddata_size = 0;
  for (i = 0, dbval = dbval_ptr; i < dbval_cnt; i++, dbval++)
    {
      senddata_size += OR_VALUE_ALIGNED_SIZE (dbval);
    }

  if (senddata_size != 0)
    {
      senddata = (char *) malloc (senddata_size);
      if (senddata == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) senddata_size);
	  return NULL;
	}
    }

  ptr = or_pack_int (request, dbval_cnt);
  ptr = or_pack_int (ptr, senddata_size);
  ptr = or_pack_int (ptr, flag);
  ptr = or_pack_int (ptr, query_timeout);

  ptr = senddata;
  for (i = 0, dbval = dbval_ptr; i < dbval_cnt; i++, dbval++)
    {
      ptr = or_pack_db_value (ptr, dbval);
    }

  /* change senddata_size as real packing size */
  senddata_size = CAST_BUFLEN (ptr - senddata);

  req_error = net_client_request_with_callback (NET_SERVER_QM_QUERY_PREPARE_AND_EXECUTE, request,
						OR_ALIGNED_BUF_SIZE (a_request), reply, OR_ALIGNED_BUF_SIZE (a_reply),
						xasl_stream, xasl_stream_size, senddata, senddata_size, &replydata,
						&replydata_size_listid, &page_ptr, &replydata_size_page, NULL, NULL);
  if (!req_error)
    {
      /* should be the same as replydata_size */
      ptr = or_unpack_int (&reply[OR_INT_SIZE], &size);
      ptr = or_unpack_int (ptr, &page_size);
      ptr = or_unpack_int (ptr, &dummy_plan_size);
      ptr = or_unpack_ptr (ptr, query_idp);

      /* not interested in the return size in the reply buffer, should do some kind of range checking here */
      if (replydata != NULL && size)
	{
	  ptr = or_unpack_unbound_listid (replydata, (void **) (&regu_result));
	  regu_result->last_pgptr = NULL;
	  if (page_size)
	    {
	      regu_result->last_pgptr = page_ptr;
	    }

	  free_and_init (replydata);
	}
    }

  if (senddata != NULL)
    {
      free_and_init (senddata);
    }

  return regu_result;
#else /* CS_MODE */
  QFILE_LIST_ID *regu_result;

  THREAD_ENTRY *thread_p = enter_server ();

  regu_result =
    xqmgr_prepare_and_execute_query (thread_p, xasl_stream, xasl_stream_size, query_idp, dbval_cnt, dbval_ptr, &flag,
				     query_timeout);

  exit_server (*thread_p);

  return regu_result;
#endif /* !CS_MODE */
}

/*
 * qmgr_end_query -
 *
 * return:
 *
 *   query_id(in):
 *
 * NOTE:
 */
int
qmgr_end_query (QUERY_ID query_id)
{
#if defined(CS_MODE)
  int status = ER_FAILED;
  int req_error = NO_ERROR;
  int i = 0;
  OR_ALIGNED_BUF (OR_INT_SIZE	/* number of query ids */
		  + OR_PTR_SIZE * NET_DEFER_END_QUERIES_MAX	/* query ids */
		  + MAX_ALIGNMENT	/* alignment */
    )a_request;
  char *request = NULL, *ptr = NULL;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = NULL;

  net_Deferred_end_queries[net_Deferred_end_queries_count++] = query_id;
  if (net_Deferred_end_queries_count < NET_DEFER_END_QUERIES_MAX)
    {
      /* Do not create a request for now. Defer it until NET_DEFER_END_QUERIES_MAX requests are collected or until
       * commit/abort. */
      return NO_ERROR;
    }
  assert (net_Deferred_end_queries_count == NET_DEFER_END_QUERIES_MAX);
  net_Deferred_end_queries_count = 0;

  request = OR_ALIGNED_BUF_START (a_request);
  reply = OR_ALIGNED_BUF_START (a_reply);

  ptr = or_pack_int (request, NET_DEFER_END_QUERIES_MAX);
  for (i = 0; i < NET_DEFER_END_QUERIES_MAX; i++)
    {
      ptr = or_pack_ptr (ptr, net_Deferred_end_queries[i]);
    }
  assert (CAST_BUFLEN (ptr - request) <= (int) OR_ALIGNED_BUF_SIZE (a_request));

  req_error =
    net_client_request (NET_SERVER_QM_QUERY_END, request, CAST_BUFLEN (ptr - request), reply,
			OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0, NULL, 0);
  if (req_error == NO_ERROR)
    {
      or_unpack_int (reply, &status);
    }
  else
    {
      status = req_error;
    }

  return status;
#else /* CS_MODE */
  int success = ER_FAILED;

  THREAD_ENTRY *thread_p = enter_server ();

  success = xqmgr_end_query (thread_p, query_id);

  exit_server (*thread_p);

  return success;
#endif /* !CS_MODE */
}

/*
 * qm_query_drop_all_plan - Send a SERVER_QM_DROP_ALL_PLAN request to the server
 *
 * Request the server to clear all XASL cache entires out. When the client
 * want to delete all cached query plans, this function will be used.
 * This function is a counter part to sqmgr_drop_all_query_plans().
 */
/*
 * qmgr_drop_all_query_plans -
 *
 * return:
 *
 * NOTE:
 */
int
qmgr_drop_all_query_plans (void)
{
#if defined(CS_MODE)
  int status = ER_FAILED;
  int req_error, request_size;
  char *request, *reply;
  OR_ALIGNED_BUF (OR_XASL_ID_SIZE) a_request;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;

  request = OR_ALIGNED_BUF_START (a_request);
  request_size = OR_INT_SIZE;
  reply = OR_ALIGNED_BUF_START (a_reply);

  or_pack_int (request, 0);	/* dummy parameter */

  /* send SERVER_QM_QUERY_DROP_ALL_PLANS request with request data; receive status code (int) as a reply */
  req_error =
    net_client_request (NET_SERVER_QM_QUERY_DROP_ALL_PLANS, request, request_size, reply, OR_ALIGNED_BUF_SIZE (a_reply),
			NULL, 0, NULL, 0);
  if (!req_error)
    {
      /* first argument should be status code (int) */
      (void) or_unpack_int (reply, &status);
    }

  return status;
#else /* CS_MODE */
  int status;

  THREAD_ENTRY *thread_p = enter_server ();

  /* call the server routine of query drop plan */
  status = xqmgr_drop_all_query_plans (thread_p);

  exit_server (*thread_p);

  return status;
#endif /* !CS_MODE */
}

/*
 * qmgr_dump_query_plans -
 *
 * return:
 *
 *   outfp(in):
 *
 * NOTE:
 */
void
qmgr_dump_query_plans (FILE * outfp)
{
#if defined(CS_MODE)
  int req_error;

  if (outfp == NULL)
    {
      outfp = stdout;
    }

  req_error = net_client_request_recv_stream (NET_SERVER_QM_QUERY_DUMP_PLANS, NULL, 0, NULL, 0, NULL, 0, outfp);
#else /* CS_MODE */

  THREAD_ENTRY *thread_p = enter_server ();

  xqmgr_dump_query_plans (thread_p, outfp);

  exit_server (*thread_p);
#endif /* !CS_MODE */
}

/*
 * qmgr_dump_query_cache -
 *
 * return:
 *
 *   outfp(in):
 *
 * NOTE:
 */
void
qmgr_dump_query_cache (FILE * outfp)
{
#if defined(CS_MODE)
  int req_error;

  if (outfp == NULL)
    {
      outfp = stdout;
    }

  req_error = net_client_request_recv_stream (NET_SERVER_QM_QUERY_DUMP_CACHE, NULL, 0, NULL, 0, NULL, 0, outfp);
#else /* CS_MODE */

  THREAD_ENTRY *thread_p = enter_server ();

  xqmgr_dump_query_cache (thread_p, outfp);

  exit_server (*thread_p);
#endif /* !CS_MODE */
}

#if defined(ENABLE_UNUSED_FUNCTION)
/*
 * qp_get_sys_timestamp -
 *
 * return:
 *
 *   value(in):
 *
 * NOTE:
 */
int
qp_get_sys_timestamp (DB_VALUE * value)
{
#if defined(CS_MODE)
  int req_error;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_request;
  char *request;
  OR_ALIGNED_BUF (OR_UTIME_SIZE) a_reply;
  char *reply;
  DB_TIMESTAMP sysutime;

  request = OR_ALIGNED_BUF_START (a_request);
  reply = OR_ALIGNED_BUF_START (a_reply);

  req_error =
    net_client_request (NET_SERVER_QPROC_GET_SYS_TIMESTAMP, request, OR_ALIGNED_BUF_SIZE (a_request), reply,
			OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0, NULL, 0);
  if (!req_error)
    {
      (void) or_unpack_utime (reply, &sysutime);
      db_make_timestamp (value, sysutime);
    }

  return NO_ERROR;
#else /* CS_MODE */

  THREAD_ENTRY *thread_p = enter_server ();

  db_sys_timestamp (value);

  exit_server (*thread_p);

  return NO_ERROR;
#endif /* !CS_MODE */
}
#endif /* ENABLE_UNUSED_FUNCTION */

/*
 * serial_get_current_value -
 *
 * return:
 *
 *   value(in):
 *   oid(in):
 *
 * NOTE:
 */
int
serial_get_current_value (DB_VALUE * value, OID * oid_p, int cached_num)
{
#if defined(CS_MODE)
  int req_error, error = ER_NET_CLIENT_DATA_RECEIVE;
  char *request, *area = NULL;
  int area_size;
  OR_ALIGNED_BUF (OR_OID_SIZE + OR_INT_SIZE) a_request;
  OR_ALIGNED_BUF (OR_INT_SIZE + OR_INT_SIZE) a_reply;
  char *reply, *ptr;

  request = OR_ALIGNED_BUF_START (a_request);
  reply = OR_ALIGNED_BUF_START (a_reply);

  ptr = or_pack_oid (request, oid_p);
  ptr = or_pack_int (ptr, cached_num);

  req_error =
    net_client_request2 (NET_SERVER_QPROC_GET_CURRENT_VALUE, request, OR_ALIGNED_BUF_SIZE (a_request), reply,
			 OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0, &area, &area_size);
  if (!req_error && area != NULL)
    {
      ptr = or_unpack_int (reply, &area_size);
      ptr = or_unpack_int (ptr, &error);
      (void) or_unpack_value (area, value);
      free_and_init (area);
    }

  return error;
#else /* CS_MODE */
  int error = NO_ERROR;

  THREAD_ENTRY *thread_p = enter_server ();

  error = xserial_get_current_value (thread_p, value, oid_p, cached_num);

  exit_server (*thread_p);

  return error;
#endif /* !CS_MODE */
}

/*
 * serial_get_next_value -
 *
 * return:
 *
 *   value(in):
 *   oid(in):
 *
 * NOTE:
 */
int
serial_get_next_value (DB_VALUE * value, OID * oid_p, int cached_num, int num_alloc, int is_auto_increment)
{
#if defined(CS_MODE)
  int req_error, error = ER_NET_CLIENT_DATA_RECEIVE;
  char *request, *area = NULL;
  int area_size;
  OR_ALIGNED_BUF (OR_OID_SIZE + (OR_INT_SIZE * 3)) a_request;
  OR_ALIGNED_BUF (OR_INT_SIZE + OR_INT_SIZE) a_reply;
  char *reply, *ptr;

  request = OR_ALIGNED_BUF_START (a_request);
  reply = OR_ALIGNED_BUF_START (a_reply);

  ptr = or_pack_oid (request, oid_p);
  ptr = or_pack_int (ptr, cached_num);
  ptr = or_pack_int (ptr, num_alloc);
  ptr = or_pack_int (ptr, is_auto_increment);

  req_error =
    net_client_request2 (NET_SERVER_QPROC_GET_NEXT_VALUE, request, OR_ALIGNED_BUF_SIZE (a_request), reply,
			 OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0, &area, &area_size);
  if (!req_error && area != NULL)
    {
      ptr = or_unpack_int (reply, &area_size);
      ptr = or_unpack_int (ptr, &error);
      (void) or_unpack_value (area, value);
      free_and_init (area);
    }

  return error;
#else /* CS_MODE */
  int error;

  THREAD_ENTRY *thread_p = enter_server ();

  /* 
   * If a client wants to generate AUTO_INCREMENT value during client-side
   * insertion, a server should update LAST_INSERT_ID on a session.
   */
  error = xserial_get_next_value (thread_p, value, oid_p, cached_num, num_alloc, is_auto_increment, true);

  exit_server (*thread_p);

  return error;
#endif /* !CS_MODE */
}

/*
 * serial_decache -
 *
 * return: NO_ERROR or error status
 *
 *   oid(in):
 *
 * NOTE:
 */
int
serial_decache (OID * oid)
{
#if defined(CS_MODE)
  int req_error;
  OR_ALIGNED_BUF (OR_OID_SIZE) a_request;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;	/* need dummy reply message */
  char *request;
  char *reply;
  int status;

  request = OR_ALIGNED_BUF_START (a_request);
  reply = OR_ALIGNED_BUF_START (a_reply);

  or_pack_oid (request, oid);

  req_error =
    net_client_request (NET_SERVER_SERIAL_DECACHE, request, OR_ALIGNED_BUF_SIZE (a_request), reply,
			OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0, NULL, 0);
  if (!req_error)
    {
      or_unpack_int (reply, &status);
    }

  return NO_ERROR;
#else /* CS_MODE */
  THREAD_ENTRY *thread_p = enter_server ();

  xserial_decache (thread_p, oid);

  exit_server (*thread_p);

  return NO_ERROR;
#endif /* !CS_MODE */
}

/*
 * perfmon_server_start_stats -
 *
 * return:
 *
 * NOTE:
 */
int
perfmon_server_start_stats (void)
{
#if defined(CS_MODE)
  int status = ER_FAILED;
  int req_error;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply;

  reply = OR_ALIGNED_BUF_START (a_reply);

  req_error =
    net_client_request (NET_SERVER_MNT_SERVER_START_STATS, NULL, 0, reply, OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0,
			NULL, 0);
  if (!req_error)
    {
      or_unpack_int (reply, &status);
    }

  return (status);
#else /* CS_MODE */
  THREAD_ENTRY *thread_p = enter_server ();

  perfmon_start_watch (thread_p);

  exit_server (*thread_p);

  return NO_ERROR;
#endif /* !CS_MODE */
}

/*
 * perfmon_server_stop_stats -
 *
 * return:
 *
 * NOTE:
 */
int
perfmon_server_stop_stats (void)
{
#if defined(CS_MODE)
  int req_error;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;	/* need dummy reply message */
  char *reply;

  reply = OR_ALIGNED_BUF_START (a_reply);

  req_error =
    net_client_request (NET_SERVER_MNT_SERVER_STOP_STATS, NULL, 0, reply, OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0, NULL,
			0);
  if (!req_error)
    {
      return ER_FAILED;
    }
  return NO_ERROR;
#else /* CS_MODE */

  THREAD_ENTRY *thread_p = enter_server ();

  perfmon_stop_watch (thread_p);

  exit_server (*thread_p);
  return NO_ERROR;
#endif /* !CS_MODE */
}

/*
 * perfmon_server_copy_stats -
 *
 * return:
 *
 *   to_stats(in):
 *
 * NOTE:
 */
int
perfmon_server_copy_stats (UINT64 * to_stats)
{
#if defined(CS_MODE)
  int req_error;
  char *reply;
  int nr_statistic_values;
  int err = NO_ERROR;

  nr_statistic_values = perfmon_get_number_of_statistic_values ();
  reply = (char *) malloc (nr_statistic_values * OR_INT64_SIZE);

  if (reply == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
	      (size_t) nr_statistic_values * OR_INT64_SIZE);
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  req_error =
    net_client_request (NET_SERVER_MNT_SERVER_COPY_STATS, NULL, 0, reply, nr_statistic_values * OR_INT64_SIZE,
			NULL, 0, NULL, 0);
  if (!req_error)
    {
      perfmon_unpack_stats (reply, to_stats);
    }
  else
    {
      perfmon_Iscollecting_stats = false;
    }

  free_and_init (reply);
  return err;

#else /* CS_MODE */

  THREAD_ENTRY *thread_p = enter_server ();

  xperfmon_server_copy_stats (thread_p, to_stats);

  exit_server (*thread_p);
  return NO_ERROR;
#endif /* !CS_MODE */
}

/*
 * perfmon_server_copy_global_stats -
 *
 * return:
 *
 *   to_stats(in):
 *
 * NOTE:
 */
int
perfmon_server_copy_global_stats (UINT64 * to_stats)
{
#if defined(CS_MODE)
  int req_error;
  char *reply = NULL;
  int nr_statistic_values;
  int err = NO_ERROR;

  nr_statistic_values = perfmon_get_number_of_statistic_values ();
  reply = (char *) malloc (nr_statistic_values * OR_INT64_SIZE);
  if (reply == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
	      (size_t) nr_statistic_values * OR_INT64_SIZE);
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  req_error = net_client_request (NET_SERVER_MNT_SERVER_COPY_GLOBAL_STATS, NULL, 0, reply,
				  nr_statistic_values * OR_INT64_SIZE, NULL, 0, NULL, 0);
  if (!req_error)
    {
      perfmon_unpack_stats (reply, to_stats);
    }
  else
    {
      perfmon_Iscollecting_stats = false;
    }

  free_and_init (reply);
  return err;
#else /* CS_MODE */

  THREAD_ENTRY *thread_p = enter_server ();

  xperfmon_server_copy_global_stats (to_stats);

  exit_server (*thread_p);
  return NO_ERROR;
#endif /* !CS_MODE */
}

/*
 * catalog_check_rep_dir -
 *
 * return:
 *
 *   class_id(in):
 *   rep_dir_p(out):
 *
 * NOTE:
 */
int
catalog_check_rep_dir (OID * class_id, OID * rep_dir_p)
{
#if defined(CS_MODE)
  int req_error, status;
  OR_ALIGNED_BUF (OR_OID_SIZE) a_request;
  char *request;
  OR_ALIGNED_BUF (OR_INT_SIZE + OR_OID_SIZE) a_reply;
  char *reply;
  char *ptr;

  request = OR_ALIGNED_BUF_START (a_request);
  reply = OR_ALIGNED_BUF_START (a_reply);

  ptr = or_pack_oid (request, class_id);

  req_error =
    net_client_request (NET_SERVER_CT_CHECK_REP_DIR, request, OR_ALIGNED_BUF_SIZE (a_request), reply,
			OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0, NULL, 0);

  if (req_error)
    {
      status = ER_FAILED;
    }
  else
    {
      ptr = or_unpack_int (reply, &status);
      ptr = or_unpack_oid (ptr, rep_dir_p);
    }

  return status;
#else /* CS_MODE */
  int success = ER_FAILED;

  THREAD_ENTRY *thread_p = enter_server ();

  success = xcatalog_check_rep_dir (thread_p, class_id, rep_dir_p);

  exit_server (*thread_p);

  return success;
#endif /* !CS_MODE */
}


/*
 * thread_kill_tran_index -
 *
 * return:
 *
 *   kill_tran_index(in):
 *   kill_user(in):
 *   kill_host(in):
 *   kill_pid(in):
 *
 * NOTE:
 */
int
thread_kill_tran_index (int kill_tran_index, char *kill_user, char *kill_host, int kill_pid)
{
#if defined(CS_MODE)
  int success = ER_FAILED;
  int request_size, strlen1, strlen2;
  char *request;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply, *ptr;
  int req_error;

  reply = OR_ALIGNED_BUF_START (a_reply);

  request_size = (OR_INT_SIZE + OR_INT_SIZE + length_const_string (kill_user, &strlen1)
		  + length_const_string (kill_host, &strlen2));

  request = (char *) malloc (request_size);
  if (request == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) request_size);
      return ER_FAILED;
    }

  ptr = or_pack_int (request, kill_tran_index);
  ptr = pack_const_string_with_length (ptr, kill_user, strlen1);
  ptr = pack_const_string_with_length (ptr, kill_host, strlen2);
  ptr = or_pack_int (ptr, kill_pid);

  req_error = net_client_request (NET_SERVER_CSS_KILL_TRANSACTION, request, request_size, reply,
				  OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0, NULL, 0);
  if (!req_error)
    {
      or_unpack_int (reply, &success);
    }

  free_and_init (request);

  return success;
#else /* CS_MODE */
  er_log_debug (ARG_FILE_LINE, "css_kill_client: THIS IS ONLY a C/S function");
  return ER_FAILED;
#endif /* !CS_MODE */
}

/*
 * thread_kill_or_interrupt_tran -
 *
 * return:
 *
 *   tran_index_list(in):
 *   num_tran_index(in):
 *   is_dba_group_member(in):
 *   interrupt_only(in):
 *   num_killed(out):
 *
 * NOTE:
 */
int
thread_kill_or_interrupt_tran (int *tran_index_list, int num_tran_index, bool is_dba_group_member, bool interrupt_only,
			       int *num_killed)
{
#if defined(CS_MODE)
  int success = ER_FAILED;
  int request_size;
  char *request;
  OR_ALIGNED_BUF (OR_INT_SIZE + OR_INT_SIZE) a_reply;
  char *reply, *ptr;
  int req_error;
  int i;

  reply = OR_ALIGNED_BUF_START (a_reply);

  request_size = OR_INT_SIZE * (2 + num_tran_index);	/* num_tran_index + tran_index_list + interrupt_only */
  request_size += OR_INT_SIZE;	/* is_dba_group_member */

  request = (char *) malloc (request_size);
  if (request == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) request_size);
      return ER_FAILED;
    }

  ptr = or_pack_int (request, (int) is_dba_group_member);
  ptr = or_pack_int (ptr, num_tran_index);

  for (i = 0; i < num_tran_index; i++)
    {
      ptr = or_pack_int (ptr, tran_index_list[i]);
    }

  ptr = or_pack_int (ptr, (interrupt_only) ? 1 : 0);

  req_error =
    net_client_request (NET_SERVER_CSS_KILL_OR_INTERRUPT_TRANSACTION, request, request_size, reply,
			OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0, NULL, 0);

  if (!req_error)
    {
      ptr = or_unpack_int (reply, &success);
      ptr = or_unpack_int (ptr, num_killed);
    }

  free_and_init (request);

  return success;
#else /* CS_MODE */
  er_log_debug (ARG_FILE_LINE, "thread_kill_or_interrupt_tran: THIS IS ONLY a C/S function");
  return ER_FAILED;
#endif /* !CS_MODE */
}

/*
 * thread_dump_cs_stat -
 *
 * return:
 *
 *   outfp(in):
 */
void
thread_dump_cs_stat (FILE * outfp)
{
#if defined(CS_MODE)
  int req_error;

  if (outfp == NULL)
    {
      outfp = stdout;
    }

  req_error = net_client_request_recv_stream (NET_SERVER_CSS_DUMP_CS_STAT, NULL, 0, NULL, 0, NULL, 0, outfp);
#else /* CS_MODE */
  er_log_debug (ARG_FILE_LINE, "thread_dump_cs_stat: THIS IS ONLY a C/S function");
  return;
#endif /* !CS_MODE */
}

/*
 * logtb_get_pack_tran_table -
 *
 * return:
 *
 *   buffer_p(in):
 *   size_p(in):
 *   include_query_exec_info(in):
 *
 * NOTE:
 */
int
logtb_get_pack_tran_table (char **buffer_p, int *size_p, bool include_query_exec_info)
{
#if defined(CS_MODE)
  int error = NO_ERROR;
  int req_error;
  int ival;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_request;
  char *request = OR_ALIGNED_BUF_START (a_request);
  OR_ALIGNED_BUF (OR_INT_SIZE + OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  char *ptr;

  /* --query-exec-info */
  or_pack_int (request, ((include_query_exec_info) ? 1 : 0));

  req_error =
    net_client_request2 (NET_SERVER_LOG_GETPACK_TRANTB, request, OR_ALIGNED_BUF_SIZE (a_request), reply,
			 OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0, buffer_p, size_p);
  if (req_error)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
    }
  else
    {
      /* first word is buffer size, second is error code */
      ptr = reply;
      ptr = or_unpack_int (ptr, &ival);
      ptr = or_unpack_int (ptr, &ival);
      error = (int) ival;
    }

  return error;
#else /* CS_MODE */
  int error;

  THREAD_ENTRY *thread_p = enter_server ();

  error = xlogtb_get_pack_tran_table (thread_p, buffer_p, size_p, 0);

  exit_server (*thread_p);

  return error;
#endif /* !CS_MODE */
}

/*
 * logtb_free_trans_info - Free transaction table information
 *   return: none
 *   info(in): TRANS_INFO to be freed
 */
void
logtb_free_trans_info (TRANS_INFO * info)
{
  int i;

  if (info == NULL)
    {
      return;
    }

  for (i = 0; i < info->num_trans; i++)
    {
      if (info->tran[i].db_user != NULL)
	{
	  db_private_free_and_init (NULL, info->tran[i].db_user);
	}
      if (info->tran[i].program_name != NULL)
	{
	  db_private_free_and_init (NULL, info->tran[i].program_name);
	}
      if (info->tran[i].login_name != NULL)
	{
	  db_private_free_and_init (NULL, info->tran[i].login_name);
	}
      if (info->tran[i].host_name != NULL)
	{
	  db_private_free_and_init (NULL, info->tran[i].host_name);
	}

      if (info->include_query_exec_info)
	{
	  if (info->tran[i].query_exec_info.query_stmt)
	    {
	      db_private_free_and_init (NULL, info->tran[i].query_exec_info.query_stmt);
	    }
	  if (info->tran[i].query_exec_info.wait_for_tran_index_string)
	    {
	      db_private_free_and_init (NULL, info->tran[i].query_exec_info.wait_for_tran_index_string);
	    }
	  if (info->tran[i].query_exec_info.sql_id)
	    {
	      db_private_free_and_init (NULL, info->tran[i].query_exec_info.sql_id);
	    }
	}
      else
	{
	  assert_release (info->tran[i].query_exec_info.query_stmt == NULL);
	  assert_release (info->tran[i].query_exec_info.wait_for_tran_index_string == NULL);
	}
    }
  free_and_init (info);
}

/*
 * logtb_get_trans_info - Get transaction table information which identifies
 *                        active transactions
 * include_query_exec_info(in) :
 *   return: TRANS_INFO array or NULL
 */
TRANS_INFO *
logtb_get_trans_info (bool include_query_exec_info)
{
  TRANS_INFO *info = NULL;
  char *buffer, *ptr;
  int num_trans, bufsize, i;
  int error;

  error = logtb_get_pack_tran_table (&buffer, &bufsize, include_query_exec_info);
  if (error != NO_ERROR || buffer == NULL)
    {
      return NULL;
    }

  ptr = buffer;
  ptr = or_unpack_int (ptr, &num_trans);

  if (num_trans == 0)
    {
      /* can't happen, there must be at least one transaction */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      goto error;
    }

  i = sizeof (TRANS_INFO) + ((num_trans - 1) * sizeof (ONE_TRAN_INFO));
  info = (TRANS_INFO *) malloc (i);
  if (info == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) i);
      goto error;
    }
  memset (info, '\0', i);

  info->num_trans = num_trans;
  info->include_query_exec_info = include_query_exec_info;
  for (i = 0; i < num_trans; i++)
    {
      int unpack_int_value;
      ptr = or_unpack_int (ptr, &info->tran[i].tran_index);
      ptr = or_unpack_int (ptr, &unpack_int_value);
      info->tran[i].state = (TRAN_STATE) unpack_int_value;
      ptr = or_unpack_int (ptr, &info->tran[i].process_id);
      ptr = or_unpack_string (ptr, &info->tran[i].db_user);
      ptr = or_unpack_string (ptr, &info->tran[i].program_name);
      ptr = or_unpack_string (ptr, &info->tran[i].login_name);
      ptr = or_unpack_string (ptr, &info->tran[i].host_name);
      if (ptr == NULL)
	{
	  assert (false);
	  goto error;
	}

      if (include_query_exec_info)
	{
	  ptr = or_unpack_float (ptr, &info->tran[i].query_exec_info.query_time);
	  ptr = or_unpack_float (ptr, &info->tran[i].query_exec_info.tran_time);
	  ptr = or_unpack_string (ptr, &info->tran[i].query_exec_info.wait_for_tran_index_string);
	  ptr = or_unpack_string (ptr, &info->tran[i].query_exec_info.query_stmt);
	  ptr = or_unpack_string (ptr, &info->tran[i].query_exec_info.sql_id);
	  if (ptr == NULL)
	    {
	      assert (false);
	      goto error;
	    }
	  OR_UNPACK_XASL_ID (ptr, &info->tran[i].query_exec_info.xasl_id);
	}
    }

  if (((int) (ptr - buffer)) != bufsize)
    {
      /* unpacking didn't match size, garbage */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      goto error;
    }

  free_and_init (buffer);

  return info;

error:
  if (buffer != NULL)
    {
      free_and_init (buffer);
    }

  if (info != NULL)
    {
      logtb_free_trans_info (info);
    }

  return NULL;
}

/*
 * logtb_dump_trantable -
 *
 * return:
 *
 *   outfp(in):
 */
void
logtb_dump_trantable (FILE * outfp)
{
#if defined(CS_MODE)
  int req_error;

  if (outfp == NULL)
    {
      outfp = stdout;
    }

  req_error = net_client_request_recv_stream (NET_SERVER_LOG_DUMP_TRANTB, NULL, 0, NULL, 0, NULL, 0, outfp);
#else /* CS_MODE */

  THREAD_ENTRY *thread_p = enter_server ();

  xlogtb_dump_trantable (thread_p, outfp);

  exit_server (*thread_p);
#endif /* !CS_MODE */
}

/*
 * heap_get_class_num_objects_pages -
 *
 * return:
 *
 *   hfid(in):
 *   approximation(in):
 *   nobjs(in):
 *   npages(in):
 *
 * NOTE:
 */
int
heap_get_class_num_objects_pages (HFID * hfid, int approximation, int *nobjs, int *npages)
{
#if defined(CS_MODE)
  int req_error, status = ER_FAILED, num_objs, num_pages;
  OR_ALIGNED_BUF (OR_HFID_SIZE + OR_INT_SIZE) a_request;
  char *request;
  OR_ALIGNED_BUF (OR_INT_SIZE * 3) a_reply;
  char *reply;
  char *ptr;

  request = OR_ALIGNED_BUF_START (a_request);
  reply = OR_ALIGNED_BUF_START (a_reply);

  ptr = or_pack_hfid (request, hfid);
  ptr = or_pack_int (ptr, approximation);

  req_error =
    net_client_request (NET_SERVER_HEAP_GET_CLASS_NOBJS_AND_NPAGES, request, OR_ALIGNED_BUF_SIZE (a_request), reply,
			OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0, NULL, 0);
  if (!req_error)
    {
      ptr = or_unpack_int (reply, &status);
      ptr = or_unpack_int (ptr, &num_objs);
      ptr = or_unpack_int (ptr, &num_pages);
      *nobjs = (int) num_objs;
      *npages = (int) num_pages;
    }

  return status;
#else /* CS_MODE */
  int success = ER_FAILED;

  THREAD_ENTRY *thread_p = enter_server ();

  success = xheap_get_class_num_objects_pages (thread_p, hfid, approximation, nobjs, npages);

  exit_server (*thread_p);

  return success;
#endif /* !CS_MODE */
}

/*
 * btree_get_statistics -
 *
 * return:
 *
 *   btid(in):
 *   stat_info(in):
 *
 * NOTE:
 */
int
btree_get_statistics (BTID * btid, BTREE_STATS * stat_info)
{
#if defined(CS_MODE)
  int req_error, status = ER_FAILED;
  OR_ALIGNED_BUF (OR_BTID_ALIGNED_SIZE) a_request;
  char *request;
  OR_ALIGNED_BUF (OR_INT_SIZE * 5) a_reply;
  char *reply;
  char *ptr;

  request = OR_ALIGNED_BUF_START (a_request);
  reply = OR_ALIGNED_BUF_START (a_reply);

  ptr = or_pack_btid (request, btid);

  req_error =
    net_client_request (NET_SERVER_BTREE_GET_STATISTICS, request, OR_ALIGNED_BUF_SIZE (a_request), reply,
			OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0, NULL, 0);
  if (!req_error)
    {
      ptr = or_unpack_int (reply, &status);
      ptr = or_unpack_int (ptr, &stat_info->leafs);
      ptr = or_unpack_int (ptr, &stat_info->pages);
      ptr = or_unpack_int (ptr, &stat_info->height);
      ptr = or_unpack_int (ptr, &stat_info->keys);

      assert_release (stat_info->leafs > 0);
      assert_release (stat_info->pages > 0);
      assert_release (stat_info->height > 0);
      assert_release (stat_info->keys >= 0);
    }

  return status;
#else /* CS_MODE */
  int success = ER_FAILED;

  THREAD_ENTRY *thread_p = enter_server ();

  assert_release (!BTID_IS_NULL (btid));
  assert_release (stat_info->keys == 0);
  assert_release (stat_info->pkeys_size == 0);
  assert_release (stat_info->pkeys == NULL);

  BTID_COPY (&stat_info->btid, btid);
  if (stat_info->pkeys_size != 0)
    {
      stat_info->pkeys_size = 0;	/* do not request pkeys info */
    }

  success = btree_get_stats (thread_p, stat_info, STATS_WITH_SAMPLING);

  assert_release (stat_info->leafs > 0);
  assert_release (stat_info->pages > 0);
  assert_release (stat_info->height > 0);
  assert_release (stat_info->keys >= 0);

  exit_server (*thread_p);

  return success;
#endif /* !CS_MODE */
}

/*
 * btree_get_index_key_type () - Get index key type.
 *
 * return	    : Error code.
 * btid (in)	    : Index b-tree identifier.
 * key_type_p (out) : Index key type.
 */
int
btree_get_index_key_type (BTID btid, TP_DOMAIN ** key_type_p)
{
#if defined(CS_MODE)
  int error;
  OR_ALIGNED_BUF (OR_BTID_ALIGNED_SIZE) a_request;
  char *request;
  OR_ALIGNED_BUF (OR_INT_SIZE * 2) a_reply;
  char *reply = NULL;
  char *ptr = NULL;
  char *reply_data = NULL;
  int reply_data_size = 0;
  int dummy;

  request = OR_ALIGNED_BUF_START (a_request);
  reply = OR_ALIGNED_BUF_START (a_reply);

  /* Send BTID to server */
  ptr = or_pack_btid (request, &btid);

  error =
    net_client_request2 (NET_SERVER_BTREE_GET_KEY_TYPE, request, OR_ALIGNED_BUF_SIZE (a_request), reply,
			 OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0, &reply_data, &reply_data_size);
  if (error != NO_ERROR)
    {
      return error;
    }
  ptr = or_unpack_int (reply, &reply_data_size);
  ptr = or_unpack_int (ptr, &error);
  if (error != NO_ERROR)
    {
      return error;
    }
  if (reply_data != NULL)
    {
      /* Obtain key type from server */
      (void) or_unpack_domain (reply_data, key_type_p, &dummy);
    }
  free_and_init (reply_data);

  return error;
#else /* CS_MODE */
  int error = NO_ERROR;

  THREAD_ENTRY *thread_p = enter_server ();

  assert_release (!BTID_IS_NULL (&btid));
  assert_release (key_type_p != NULL);

  error = xbtree_get_key_type (thread_p, btid, key_type_p);

  exit_server (*thread_p);

  return error;
#endif /* !CS_MODE */
}

/*
 * db_local_transaction_id -
 *
 * return:
 *
 *   result_trid(in):
 *
 * NOTE:
 */
int
db_local_transaction_id (DB_VALUE * result_trid)
{
#if defined(CS_MODE)
  int req_error, trid = 0;
  int success = ER_FAILED;
  OR_ALIGNED_BUF (OR_INT_SIZE + OR_INT_SIZE) a_reply;
  char *reply, *ptr;

  reply = OR_ALIGNED_BUF_START (a_reply);

  req_error =
    net_client_request (NET_SERVER_TM_LOCAL_TRANSACTION_ID, NULL, 0, reply, OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0,
			NULL, 0);
  if (!req_error)
    {
      ptr = or_unpack_int (reply, &success);
      ptr = or_unpack_int (ptr, &trid);
    }

  db_make_int (result_trid, trid);
  return success;
#else /* CS_MODE */
  int success;

  THREAD_ENTRY *thread_p = enter_server ();

  success = xtran_get_local_transaction_id (thread_p, result_trid);

  exit_server (*thread_p);

  return success;
#endif /* !CS_MODE */
}

/*
 * qp_get_server_info -
 *
 * return:
 *
 *   server_info(in):
 *
 * NOTE:
 */
int
qp_get_server_info (PARSER_CONTEXT * parser, int server_info_bits)
{
#if defined(CS_MODE)
  int status = ER_FAILED;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_request;
  char *request;
  OR_ALIGNED_BUF (OR_INT_SIZE + OR_INT_SIZE) a_reply;
  char *reply;
  char *ptr, *area = NULL;
  int val_size;

  request = OR_ALIGNED_BUF_START (a_request);
  reply = OR_ALIGNED_BUF_START (a_reply);

  ptr = or_pack_int (request, server_info_bits);

  status =
    net_client_request2 (NET_SERVER_QPROC_GET_SERVER_INFO, request, OR_ALIGNED_BUF_SIZE (a_request), reply,
			 OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0, &area, &val_size);
  if (status != NO_ERROR)
    {
      goto error_exit;
    }

  if (area != NULL)
    {
      ptr = or_unpack_int (reply, &val_size);
      ptr = or_unpack_int (ptr, &status);

      if (status != NO_ERROR)
	{
	  goto error_exit;
	}

      ptr = area;
      if (server_info_bits & SI_SYS_DATETIME)
	{
	  ptr = or_unpack_value (ptr, &(parser->sys_datetime));
	  ptr = or_unpack_value (ptr, &(parser->sys_epochtime));
	}
      if (server_info_bits & SI_LOCAL_TRANSACTION_ID)
	{
	  ptr = or_unpack_value (ptr, &parser->local_transaction_id);
	}
    }

error_exit:
  if (area != NULL)
    {
      free_and_init (area);
    }

  return status;
#else /* CS_MODE */
  int success = NO_ERROR;

  THREAD_ENTRY *thread_p = enter_server ();

  if (server_info_bits & SI_SYS_DATETIME)
    {
      success = db_sys_date_and_epoch_time (&(parser->sys_datetime), &(parser->sys_epochtime));
      if (success != NO_ERROR)
	{
	  goto error_exit;
	}
    }

  if (server_info_bits & SI_LOCAL_TRANSACTION_ID)
    {
      success = db_local_transaction_id (&parser->local_transaction_id);
    }

error_exit:
  exit_server (*thread_p);
  return success;
#endif /* !CS_MODE */
}

/*
 * sysprm_change_server_parameters () - Sends a list of assignments to server in order to change system parameter
 *					values.
 *
 * return	    : SYSPRM_ERR code.
 * assignments (in) : list of assignments.
 */
SYSPRM_ERR
sysprm_change_server_parameters (const SYSPRM_ASSIGN_VALUE * assignments)
{
#if defined(CS_MODE)
  SYSPRM_ERR rc = PRM_ERR_COMM_ERR;
  int request_size = 0, req_error = NO_ERROR;
  char *request = NULL, *reply = NULL;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;

  reply = OR_ALIGNED_BUF_START (a_reply);

  request_size = sysprm_packed_assign_values_length (assignments, 0);
  request = (char *) malloc (request_size);
  if (request == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) request_size);
      return PRM_ERR_NO_MEM_FOR_PRM;
    }

  (void) sysprm_pack_assign_values (request, assignments);
  req_error =
    net_client_request (NET_SERVER_PRM_SET_PARAMETERS, request, request_size, reply, OR_ALIGNED_BUF_SIZE (a_reply),
			NULL, 0, NULL, 0);
  if (req_error == NO_ERROR)
    {
      int unpack_value;
      or_unpack_int (reply, &unpack_value);
      rc = (SYSPRM_ERR) unpack_value;
    }
  else
    {
      rc = PRM_ERR_COMM_ERR;
    }

  free_and_init (request);

  return rc;
#else /* CS_MODE */
  THREAD_ENTRY *thread_p = enter_server ();
  xsysprm_change_server_parameters (assignments);
  exit_server (*thread_p);

  return PRM_ERR_NO_ERROR;
#endif /* !CS_MODE */
}

/*
 * sysprm_obtain_server_parameters () - Obtain values for system parameters from server.
 *
 * return		   : SYSPRM_ERR code.
 * prm_values_ptr (in/out) : list of parameter values.
 */
SYSPRM_ERR
sysprm_obtain_server_parameters (SYSPRM_ASSIGN_VALUE ** prm_values_ptr)
{
#if defined(CS_MODE)
  SYSPRM_ERR rc = PRM_ERR_COMM_ERR;
  int req_error = NO_ERROR, request_size = 0, receive_size = 0;
  OR_ALIGNED_BUF (OR_INT_SIZE * 2) a_reply;
  char *reply = NULL, *request_data = NULL, *receive_data = NULL;
  char *ptr = NULL;
  SYSPRM_ASSIGN_VALUE *updated_prm_values = NULL;

  assert (prm_values_ptr != NULL && *prm_values_ptr != NULL);

  reply = OR_ALIGNED_BUF_START (a_reply);

  request_size = sysprm_packed_assign_values_length (*prm_values_ptr, 0);
  request_data = (char *) malloc (request_size);
  if (request_data == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) request_size);
      return PRM_ERR_NO_MEM_FOR_PRM;
    }

  (void) sysprm_pack_assign_values (request_data, *prm_values_ptr);
  req_error =
    net_client_request2 (NET_SERVER_PRM_GET_PARAMETERS, request_data, request_size, reply,
			 OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0, &receive_data, &receive_size);
  if (req_error != NO_ERROR)
    {
      rc = PRM_ERR_COMM_ERR;
    }
  else
    {
      int unpack_value;
      ptr = or_unpack_int (reply, &receive_size);
      ptr = or_unpack_int (ptr, &unpack_value);
      rc = (SYSPRM_ERR) unpack_value;
      if (rc != PRM_ERR_NO_ERROR || receive_data == NULL)
	{
	  goto cleanup;
	}

      (void) sysprm_unpack_assign_values (receive_data, &updated_prm_values);

      /* free old values */
      sysprm_free_assign_values (prm_values_ptr);
      /* update values */
      *prm_values_ptr = updated_prm_values;
    }

cleanup:
  if (request_data != NULL)
    {
      free_and_init (request_data);
    }

  if (receive_data != NULL)
    {
      free_and_init (receive_data);
    }

  return rc;
#else /* CS_MODE */
  THREAD_ENTRY *thread_p = enter_server ();
  xsysprm_obtain_server_parameters (*prm_values_ptr);
  exit_server (*thread_p);

  return PRM_ERR_NO_ERROR;
#endif /* !CS_MODE */
}

/*
 * sysprm_get_force_server_parameters () - Get from server values for system parameters marked with
 *					   PRM_FORCE_SERVER flag.
 *
 * return	       : error code
 * change_values (out) : list of parameter values.
 */
int
sysprm_get_force_server_parameters (SYSPRM_ASSIGN_VALUE ** change_values)
{
#if defined (CS_MODE)
  int req_error;
  OR_ALIGNED_BUF (OR_INT_SIZE * 2) a_reply;
  int area_size, error;
  char *reply = NULL, *area = NULL, *ptr = NULL;

  assert (change_values != NULL);
  *change_values = NULL;

  reply = OR_ALIGNED_BUF_START (a_reply);

  req_error =
    net_client_request2 (NET_SERVER_PRM_GET_FORCE_PARAMETERS, NULL, 0, reply, OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0,
			 &area, &area_size);
  if (req_error != NO_ERROR)
    {
      error = req_error;
      goto error;
    }

  ptr = or_unpack_int (reply, &area_size);
  ptr = or_unpack_int (ptr, &error);
  if (error != NO_ERROR)
    {
      goto error;
    }

  if (area != NULL)
    {
      (void) sysprm_unpack_assign_values (area, change_values);
      free_and_init (area);
    }

  return NO_ERROR;

error:
  if (area != NULL)
    {
      free_and_init (area);
    }

  return error;
#else /* CS_MODE */
  assert (change_values != NULL);
  *change_values = NULL;
  return NO_ERROR;
#endif /* !CS_MODE */
}

/*
 * sysprm_dump_server_parameters -
 *
 * return:
 */
void
sysprm_dump_server_parameters (FILE * outfp)
{
#if defined(CS_MODE)
  int req_error;

  if (outfp == NULL)
    {
      outfp = stdout;
    }

  req_error = net_client_request_recv_stream (NET_SERVER_PRM_DUMP_PARAMETERS, NULL, 0, NULL, 0, NULL, 0, outfp);
#else /* CS_MODE */
  THREAD_ENTRY *thread_p = enter_server ();

  xsysprm_dump_server_parameters (outfp);

  exit_server (*thread_p);
#endif /* !CS_MODE */
}

/*
 * heap_has_instance -
 *
 * return:
 *
 *   hfid(in):
 *   class_oid(in):
 *   has_visible_instance(in): true if we need to check for a visible record
 *
 * NOTE:
 */
int
heap_has_instance (HFID * hfid, OID * class_oid, int has_visible_instance)
{
#if defined(CS_MODE)
  int req_error, status = ER_FAILED;
  OR_ALIGNED_BUF (OR_HFID_SIZE + OR_OID_SIZE + OR_INT_SIZE) a_request;
  char *request;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply;
  char *ptr;

  request = OR_ALIGNED_BUF_START (a_request);
  reply = OR_ALIGNED_BUF_START (a_reply);

  ptr = or_pack_hfid (request, hfid);
  ptr = or_pack_oid (ptr, class_oid);
  ptr = or_pack_int (ptr, has_visible_instance);

  req_error =
    net_client_request (NET_SERVER_HEAP_HAS_INSTANCE, request, OR_ALIGNED_BUF_SIZE (a_request), reply,
			OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0, NULL, 0);
  if (!req_error)
    {
      ptr = or_unpack_int (reply, &status);
    }

  return status;
#else /* CS_MODE */
  int r = ER_FAILED;

  THREAD_ENTRY *thread_p = enter_server ();
  r = xheap_has_instance (thread_p, hfid, class_oid, has_visible_instance);
  exit_server (*thread_p);

  return r;
#endif /* !CS_MODE */
}

/*
 * jsp_get_server_port -
 *
 * return:
 *
 * NOTE:
 */
int
jsp_get_server_port (void)
{
#if defined(CS_MODE)
  int req_error;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_request;
  char *request;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply;
  int port = -1;

  request = OR_ALIGNED_BUF_START (a_request);
  reply = OR_ALIGNED_BUF_START (a_reply);

  req_error =
    net_client_request (NET_SERVER_JSP_GET_SERVER_PORT, request, OR_ALIGNED_BUF_SIZE (a_request), reply,
			OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0, NULL, 0);
  if (!req_error)
    {
      or_unpack_int (reply, &port);
    }

  return port;
#else /* CS_MODE */
  int port;
  THREAD_ENTRY *thread_p = enter_server ();
  port = jsp_server_port ();
  exit_server (*thread_p);
  return port;
#endif /* !CS_MODE */
}

/*
 * repl_log_get_append_lsa -
 *
 * return:
 *
 *   lsa(in):
 *
 * NOTE:
 */
int
repl_log_get_append_lsa (LOG_LSA * lsa)
{
#if defined(CS_MODE)
  int req_error, success = ER_FAILED;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_request;
  char *request;
  OR_ALIGNED_BUF (OR_LOG_LSA_ALIGNED_SIZE) a_reply;
  char *reply;

  request = OR_ALIGNED_BUF_START (a_request);
  reply = OR_ALIGNED_BUF_START (a_reply);
  req_error =
    net_client_request (NET_SERVER_REPL_LOG_GET_APPEND_LSA, request, OR_ALIGNED_BUF_SIZE (a_request), reply,
			OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0, NULL, 0);
  if (!req_error)
    {
      or_unpack_log_lsa (reply, lsa);
      success = NO_ERROR;
    }

  return success;
#else /* CS_MODE */
  LOG_LSA *tmp_lsa = NULL;
  int r = ER_FAILED;

  THREAD_ENTRY *thread_p = enter_server ();
  tmp_lsa = xrepl_log_get_append_lsa ();
  if (lsa && tmp_lsa)
    {
      LSA_COPY (lsa, tmp_lsa);
      r = NO_ERROR;
    }
  else
    {
      r = ER_FAILED;
    }
  exit_server (*thread_p);

  return r;
#endif /* !CS_MODE */
}

/*
 * repl_set_info -
 *
 * return:
 *
 *   repl_info(in):
 *
 * NOTE:
 */
int
repl_set_info (REPL_INFO * repl_info)
{
#if defined(CS_MODE)
  int req_error, success = ER_FAILED;
  int request_size = 0, strlen1, strlen2, strlen3, strlen4;
  char *request = NULL, *ptr;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply;
  REPL_INFO_SBR *repl_schema;

  reply = OR_ALIGNED_BUF_START (a_reply);

  switch (repl_info->repl_info_type)
    {
    case REPL_INFO_TYPE_SBR:
      repl_schema = (REPL_INFO_SBR *) repl_info->info;
      request_size = (OR_INT_SIZE	/* REPL_INFO.REPL_INFO_TYPE */
		      + OR_INT_SIZE	/* REPL_INFO_SCHEMA.statement_type */
		      + length_const_string (repl_schema->name, &strlen1)
		      + length_const_string (repl_schema->stmt_text, &strlen2)
		      + length_const_string (repl_schema->db_user, &strlen3)
		      + length_const_string (repl_schema->sys_prm_context, &strlen4));

      request = (char *) malloc (request_size);
      if (request == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) request_size);
	  return ER_FAILED;
	}

      ptr = or_pack_int (request, REPL_INFO_TYPE_SBR);
      ptr = or_pack_int (ptr, repl_schema->statement_type);
      ptr = pack_const_string_with_length (ptr, repl_schema->name, strlen1);
      ptr = pack_const_string_with_length (ptr, repl_schema->stmt_text, strlen2);
      ptr = pack_const_string_with_length (ptr, repl_schema->db_user, strlen3);
      ptr = pack_const_string_with_length (ptr, repl_schema->sys_prm_context, strlen4);
      req_error =
	net_client_request (NET_SERVER_REPL_INFO, request, request_size, reply, OR_ALIGNED_BUF_SIZE (a_reply), NULL,
			    0, NULL, 0);
      if (!req_error)
	{
	  or_unpack_int (reply, &success);
	}

      free_and_init (request);
      break;

    default:
      break;
    }

  return success;
#else /* CS_MODE */
  int r = ER_FAILED;

  THREAD_ENTRY *thread_p = enter_server ();
  r = xrepl_set_info (thread_p, repl_info);
  exit_server (*thread_p);
  return r;
#endif /* !CS_MODE */
}

/*
 * locator_check_fk_validity -
 *
 * return:
 *
 *   cls_oid(in):
 *   hfid(in):
 *   key_type(in):
 *   n_attrs(in):
 *   attr_ids(in):
 *   pk_cls_oid(in):
 *   pk_btid(in):
 *   fk_name(in):
 *
 * NOTE:
 */
int
locator_check_fk_validity (OID * cls_oid, HFID * hfid, TP_DOMAIN * key_type, int n_attrs, int *attr_ids,
			   OID * pk_cls_oid, BTID * pk_btid, char *fk_name)
{
#if defined(CS_MODE)
  int error, req_error, request_size, domain_size;
  char *ptr;
  char *request;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply;
  int i, strlen;

  error = NO_ERROR;
  reply = OR_ALIGNED_BUF_START (a_reply);

  domain_size = or_packed_domain_size (key_type, 0);
  request_size = (OR_OID_SIZE + OR_HFID_SIZE + domain_size + OR_INT_SIZE + (n_attrs * OR_INT_SIZE)
		  + OR_OID_SIZE + OR_BTID_ALIGNED_SIZE + or_packed_string_length (fk_name, &strlen));

  request = (char *) malloc (request_size);
  if (request == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) request_size);
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  ptr = or_pack_oid (request, cls_oid);
  ptr = or_pack_hfid (ptr, hfid);
  ptr = or_pack_domain (ptr, key_type, 0, 0);
  ptr = or_pack_int (ptr, n_attrs);
  for (i = 0; i < n_attrs; i++)
    {
      ptr = or_pack_int (ptr, attr_ids[i]);
    }
  ptr = or_pack_oid (ptr, pk_cls_oid);
  ptr = or_pack_btid (ptr, pk_btid);
  ptr = or_pack_string_with_length (ptr, fk_name, strlen);

  req_error = net_client_request (NET_SERVER_LC_CHECK_FK_VALIDITY, request, request_size, reply,
				  OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0, NULL, 0);
  if (!req_error)
    {
      ptr = or_unpack_int (reply, &error);
    }

  free_and_init (request);

  return error;
#else /* CS_MODE */
  int error = NO_ERROR;

  THREAD_ENTRY *thread_p = enter_server ();

  error =
    xlocator_check_fk_validity (thread_p, cls_oid, hfid, key_type, n_attrs, attr_ids, pk_cls_oid, pk_btid, fk_name);

  exit_server (*thread_p);
  return error;
#endif /* !CS_MODE */
}

/*
 * logwr_get_log_pages -
 *
 * return:
 *
 *   rc_ptr(in/out): request context
 *
 * NOTE:
 */
int
logwr_get_log_pages (LOGWR_CONTEXT * ctx_ptr)
{
#if defined(CS_MODE)
  OR_ALIGNED_BUF (OR_INT64_SIZE + OR_INT_SIZE * 2) a_request;
  OR_ALIGNED_BUF (OR_INT_SIZE * 2) a_reply;
  char *request, *reply;
  char *replydata1, *replydata2;
  int replydata_size1, replydata_size2;
  char *ptr;
  LOG_PAGEID first_pageid_torecv;
  LOGWR_MODE mode, save_mode;
  int req_error, error = NO_ERROR;

  /* Do it as async mode at the first request to the server. And, if several pages are left to get, keep it as async
   * mode */

  assert (logwr_Gl.last_recv_pageid <= logwr_Gl.hdr.eof_lsa.pageid);
  if (logwr_Gl.last_recv_pageid == logwr_Gl.hdr.eof_lsa.pageid)
    {
      /* In case of synchronous request */
      first_pageid_torecv = logwr_Gl.last_recv_pageid;

      mode = (logwr_Gl.last_recv_pageid == NULL_PAGEID) ? LOGWR_MODE_ASYNC : logwr_Gl.mode;
    }
  else
    {
      /* In the middle of sending the pages which are already flushed */
      if (logwr_Gl.last_recv_pageid == NULL_PAGEID)
	{
	  /* To check database equality at first, get the header page */
	  first_pageid_torecv = LOGPB_HEADER_PAGE_ID;
	}
      else
	{
	  /* When it overtakes the state of the server, it set ha_file_state is sync'ed. And, the flush action is not
	   * sync'ed with the server. So, it requests the last page again to get the missing log records. */
	  if (logwr_Gl.hdr.ha_file_status == LOG_HA_FILESTAT_SYNCHRONIZED)
	    {
	      first_pageid_torecv = logwr_Gl.last_recv_pageid;
	    }
	  else
	    {
	      first_pageid_torecv = logwr_Gl.last_recv_pageid + 1;
	    }
	}
      /* In case of archiving, not replication delay */
      if (first_pageid_torecv == logwr_Gl.ori_nxarv_pageid)
	{
	  mode = logwr_Gl.mode;
	}
      else
	{
	  mode = LOGWR_MODE_ASYNC;
	}
    }

  er_log_debug (ARG_FILE_LINE, "logwr_get_log_pages, fpageid(%lld), mode(%s)", first_pageid_torecv,
		mode == LOGWR_MODE_SYNC ? "sync" : (mode == LOGWR_MODE_ASYNC ? "async" : "semisync"));

  save_mode = logwr_Gl.mode;
  logwr_Gl.mode = mode;

  request = OR_ALIGNED_BUF_START (a_request);
  reply = OR_ALIGNED_BUF_START (a_reply);

  if (first_pageid_torecv == NULL_PAGEID && logwr_Gl.start_pageid >= NULL_PAGEID)
    {
      ptr = or_pack_int64 (request, logwr_Gl.start_pageid);
      mode = (LOGWR_MODE) (mode | LOGWR_COPY_FROM_FIRST_PHY_PAGE_MASK);
    }
  else
    {
      ptr = or_pack_int64 (request, first_pageid_torecv);
    }

  ptr = or_pack_int (ptr, mode);
  ptr = or_pack_int (ptr, ctx_ptr->last_error);

  req_error =
    net_client_request_with_logwr_context (ctx_ptr, NET_SERVER_LOGWR_GET_LOG_PAGES, request,
					   OR_ALIGNED_BUF_SIZE (a_request), reply, OR_ALIGNED_BUF_SIZE (a_reply), NULL,
					   0, NULL, 0, &replydata1, &replydata_size1, &replydata2, &replydata_size2);

  logwr_Gl.mode = save_mode;

  if (req_error != NO_ERROR)
    {
      error = req_error;
      if (error == ER_NET_SERVER_CRASHED)
	{
	  if (logwr_Gl.mode == LOGWR_MODE_SEMISYNC)
	    {
	      logwr_Gl.force_flush = true;
	      logwr_write_log_pages ();
	    }
	  /* Write the server is dead at the log header */
	  logwr_Gl.hdr.ha_server_state = HA_SERVER_STATE_DEAD;
	  logwr_flush_header_page ();
	  ctx_ptr->shutdown = true;
	}
      else if (error == ER_HA_LW_FAILED_GET_LOG_PAGE)
	{
	  if (logwr_Gl.mode == LOGWR_MODE_SEMISYNC)
	    {
	      logwr_Gl.force_flush = true;
	      logwr_write_log_pages ();
	    }

	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, first_pageid_torecv);

	  ctx_ptr->shutdown = true;
	}
    }

  if (logwr_Gl.start_pageid >= NULL_PAGEID && logwr_Gl.hdr.eof_lsa.pageid == logwr_Gl.hdr.append_lsa.pageid
      && logwr_Gl.hdr.ha_file_status == LOG_HA_FILESTAT_SYNCHRONIZED)
    {
      ctx_ptr->shutdown = true;
      error = NO_ERROR;
    }

  return error;

#else /* CS_MODE */
  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_NOT_IN_STANDALONE, 1, "copylog database");
  return ER_NOT_IN_STANDALONE;
#endif /* !CS_MODE */
}

bool
histo_is_supported (void)
{
  return prm_get_bool_value (PRM_ID_ENABLE_HISTO);
}

int
histo_start (bool for_all_trans)
{
#if defined (CS_MODE)
  return net_histo_start (for_all_trans);
#else /* CS_MODE */
  return perfmon_start_stats (for_all_trans);
#endif /* !CS_MODE */
}

int
histo_stop (void)
{
#if defined (CS_MODE)
  return net_histo_stop ();
#else /* CS_MODE */
  return perfmon_stop_stats ();
#endif /* !CS_MODE */
}

int
histo_print (FILE * stream)
{
  int err = NO_ERROR;

#if defined (CS_MODE)
  err = net_histo_print (stream);
#else /* CS_MODE */
  err = perfmon_print_stats (stream);
#endif /* !CS_MODE */

  return err;
}

int
histo_print_global_stats (FILE * stream, bool cumulative, const char *substr)
{
  int err = NO_ERROR;

#if defined (CS_MODE)
  err = net_histo_print_global_stats (stream, cumulative, substr);
#else /* CS_MODE */
  err = perfmon_print_global_stats (stream, cumulative, substr);
#endif /* !CS_MODE */

  return err;
}

void
histo_clear (void)
{
#if defined (CS_MODE)
  net_histo_clear ();
#else /* CS_MODE */
  perfmon_reset_stats ();
#endif /* !CS_MODE */
}

/*
 * boot_compact_classes () - compact specified classes
 *
 * return : NO_ERROR if all OK, ER_ status otherwise
 *
 *   class_oids(in): the class oids list to process
 *   n_classes(in): the length of class_oids
 *   space_to_process(in): the maximum space to process
 *   instance_lock_timeout(in): the lock timeout for instances
 *   class_lock_timeout(in): the lock timeout for classes
 *   delete_old_repr(in): true if old class representations must be deleted
 *   last_processed_class_oid(in,out): last processed class oid
 *   last_processed_oid(in,out): last processed oid
 *   total_objects(in,out): count processed objects for each class
 *   failed_objects(in,out): count failed objects for each class
 *   modified_objects(in,out): count modified objects for each class
 *   big_objects(in,out): count big objects for each class
 *   initial_last_repr_id(in,out): the list of last class representation
 *
 * Note:
 */
int
boot_compact_classes (OID ** class_oids, int num_classes, int space_to_process, int instance_lock_timeout,
		      int class_lock_timeout, bool delete_old_repr, OID * last_processed_class_oid,
		      OID * last_processed_oid, int *total_objects, int *failed_objects, int *modified_objects,
		      int *big_objects, int *ids_repr)
{
#if defined(CS_MODE)
  int success = ER_FAILED, request_size, req_error, i, reply_size;
  char *reply = NULL, *request = NULL, *ptr = NULL;

  if (class_oids == NULL || num_classes < 0 || space_to_process < 0 || last_processed_class_oid == NULL
      || last_processed_oid == NULL || total_objects == NULL || failed_objects == NULL || modified_objects == NULL
      || big_objects == NULL || ids_repr == NULL)
    {
      return ER_QPROC_INVALID_PARAMETER;
    }

  request_size = OR_OID_SIZE * (num_classes + 2) + OR_INT_SIZE * (5 * num_classes + 5);

  request = (char *) malloc (request_size);
  if (request == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) request_size);
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  reply_size = OR_OID_SIZE * 2 + OR_INT_SIZE * (5 * num_classes + 1);
  reply = (char *) malloc (reply_size);
  if (reply == NULL)
    {
      free_and_init (request);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) reply_size);
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  ptr = or_pack_int (request, num_classes);
  for (i = 0; i < num_classes; i++)
    {
      ptr = or_pack_oid (ptr, class_oids[i]);
    }

  ptr = or_pack_int (ptr, space_to_process);
  ptr = or_pack_int (ptr, instance_lock_timeout);
  ptr = or_pack_int (ptr, class_lock_timeout);
  ptr = or_pack_int (ptr, (int) delete_old_repr);
  ptr = or_pack_oid (ptr, last_processed_class_oid);
  ptr = or_pack_oid (ptr, last_processed_oid);

  for (i = 0; i < num_classes; i++)
    {
      ptr = or_pack_int (ptr, total_objects[i]);
    }

  for (i = 0; i < num_classes; i++)
    {
      ptr = or_pack_int (ptr, failed_objects[i]);
    }

  for (i = 0; i < num_classes; i++)
    {
      ptr = or_pack_int (ptr, modified_objects[i]);
    }

  for (i = 0; i < num_classes; i++)
    {
      ptr = or_pack_int (ptr, big_objects[i]);
    }

  for (i = 0; i < num_classes; i++)
    {
      ptr = or_pack_int (ptr, ids_repr[i]);
    }

  req_error = net_client_request (NET_SERVER_BO_COMPACT_DB, request, request_size, reply, reply_size, NULL, 0, NULL, 0);
  if (!req_error)
    {
      ptr = or_unpack_int (reply, &success);
      ptr = or_unpack_oid (ptr, last_processed_class_oid);
      ptr = or_unpack_oid (ptr, last_processed_oid);

      for (i = 0; i < num_classes; i++)
	{
	  ptr = or_unpack_int (ptr, total_objects + i);
	}

      for (i = 0; i < num_classes; i++)
	{
	  ptr = or_unpack_int (ptr, failed_objects + i);
	}

      for (i = 0; i < num_classes; i++)
	{
	  ptr = or_unpack_int (ptr, modified_objects + i);
	}

      for (i = 0; i < num_classes; i++)
	{
	  ptr = or_unpack_int (ptr, big_objects + i);
	}

      for (i = 0; i < num_classes; i++)
	{
	  ptr = or_unpack_int (ptr, ids_repr + i);
	}
    }

  free_and_init (request);
  free_and_init (reply);

  return success;
#else /* CS_MODE */
  return ER_FAILED;
#endif /* !CS_MODE */
}

/*
 * boot_heap_compact () - compact heap file of gived class
 *
 * return : NO_ERROR if all OK, ER_ status otherwise
 *
 *   class_oid(in): the class oid of heap to compact
 *
 * Note:
 */
int
boot_heap_compact (OID * class_oid)
{
#if defined(CS_MODE)
  int success = ER_FAILED, request_size, req_error, reply_size;

  OR_ALIGNED_BUF (OR_OID_SIZE) a_request;
  char *request;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply;

  if (class_oid == NULL)
    {
      return ER_QPROC_INVALID_PARAMETER;
    }

  request = OR_ALIGNED_BUF_START (a_request);
  reply = OR_ALIGNED_BUF_START (a_reply);

  request_size = OR_OID_SIZE;
  reply_size = OR_INT_SIZE;

  or_pack_oid (request, class_oid);

  req_error =
    net_client_request (NET_SERVER_BO_HEAP_COMPACT, request, request_size, reply, reply_size, NULL, 0, NULL, 0);
  if (!req_error)
    {
      (void) or_unpack_int (reply, &success);
    }

  return success;
#else /* CS_MODE */
  return ER_FAILED;
#endif /* !CS_MODE */
}

/*
 * compact_db_start () -  start database compaction
 *
 * return : error code
 *
 */
int
compact_db_start (void)
{
#if defined(CS_MODE)
  int success = ER_FAILED, req_error, reply_size;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply;

  reply_size = OR_INT_SIZE;
  reply = OR_ALIGNED_BUF_START (a_reply);

  req_error = net_client_request (NET_SERVER_BO_COMPACT_DB_START, NULL, 0, reply, reply_size, NULL, 0, NULL, 0);
  if (!req_error)
    {
      (void) or_unpack_int (reply, &success);
    }

  return success;
#else /* CS_MODE */
  return ER_FAILED;
#endif /* !CS_MODE */
}

/*
 * compact_db_stop () - stop database compaction
 *
 * return :error code
 *
 */
int
compact_db_stop (void)
{
#if defined(CS_MODE)
  int success = ER_FAILED, req_error, reply_size;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply;

  reply_size = OR_INT_SIZE;
  reply = OR_ALIGNED_BUF_START (a_reply);

  req_error = net_client_request (NET_SERVER_BO_COMPACT_DB_STOP, NULL, 0, reply, reply_size, NULL, 0, NULL, 0);
  if (!req_error)
    {
      (void) or_unpack_int (reply, &success);
    }

  return success;
#else /* CS_MODE */
  return ER_FAILED;
#endif /* !CS_MODE */
}

/*
 * es_posix_create_file () - create an external file in a posix storage
 *
 * return :error code
 *
 */
int
es_posix_create_file (char *new_path)
{
#if defined(CS_MODE)
  int error = ER_NET_CLIENT_DATA_RECEIVE;
  int req_error, path_size;
  OR_ALIGNED_BUF (OR_INT_SIZE * 2) a_reply;
  char *reply, *ptr;

  reply = OR_ALIGNED_BUF_START (a_reply);

  req_error =
    net_client_request2_no_malloc (NET_SERVER_ES_CREATE_FILE, NULL, 0, reply, OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0,
				   new_path, &path_size);
  if (!req_error)
    {
      ptr = or_unpack_int (reply, &path_size);
      ptr = or_unpack_int (ptr, &error);
    }

  return error;
#else /* CS_MODE */
  return ER_FAILED;
#endif /* !CS_MODE */
}

/*
 * es_posix_write_file () - write data in an esternal file
 *
 * return :error code
 * path(in):
 * buf(in):
 * count(in):
 * offset(in):
 */
ssize_t
es_posix_write_file (const char *path, const void *buf, size_t count, off_t offset)
{
#if defined(CS_MODE)
  INT64 ret = ER_NET_CLIENT_DATA_RECEIVE;
  int req_error, request_size, strlen;
  char *request, *reply;
  OR_ALIGNED_BUF (OR_INT64_SIZE) a_reply;
  char *ptr;

  reply = OR_ALIGNED_BUF_START (a_reply);

  request_size = DB_ALIGN (length_const_string (path, &strlen), MAX_ALIGNMENT) + OR_INT64_SIZE + OR_INT64_SIZE;
  request = (char *) malloc (request_size);
  if (request == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) request_size);
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  ptr = pack_const_string_with_length (request, path, strlen);
  ptr = or_pack_int64 (ptr, (INT64) offset);
  ptr = or_pack_int64 (ptr, (INT64) count);

  req_error = net_client_request (NET_SERVER_ES_WRITE_FILE, request, request_size, reply,
				  OR_ALIGNED_BUF_SIZE (a_reply), (char *) buf, (int) count, NULL, 0);
  if (!req_error)
    {
      ptr = or_unpack_int64 (reply, &ret);
    }
  free_and_init (request);

  return ret;
#else /* CS_MODE */
  return ER_FAILED;
#endif /* !CS_MODE */
}

/*
 * es_posix_read_file () - read data from an esternal file
 *
 * return :error code
 * path(in):
 * buf(out):
 * count(in):
 * offset(in):
 */
ssize_t
es_posix_read_file (const char *path, void *buf, size_t count, off_t offset)
{
#if defined(CS_MODE)
  INT64 ret = ER_NET_CLIENT_DATA_RECEIVE;
  int req_error, request_size, strlen;
  char *request, *reply;
  OR_ALIGNED_BUF (OR_INT64_SIZE) a_reply;
  char *ptr;

  reply = OR_ALIGNED_BUF_START (a_reply);

  request_size = DB_ALIGN (length_const_string (path, &strlen), MAX_ALIGNMENT) + OR_INT64_SIZE + OR_INT64_SIZE;
  request = (char *) malloc (request_size);
  if (request == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) request_size);
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  ptr = pack_const_string_with_length (request, path, strlen);
  ptr = or_pack_int64 (ptr, (INT64) offset);
  ptr = or_pack_int64 (ptr, (INT64) count);

  req_error = net_client_request (NET_SERVER_ES_READ_FILE, request, request_size, reply, OR_ALIGNED_BUF_SIZE (a_reply),
				  NULL, 0, (char *) buf, (int) count);
  if (!req_error)
    {
      ptr = or_unpack_int64 (reply, &ret);
    }

  free_and_init (request);

  return ret;
#else /* CS_MODE */
  return ER_FAILED;
#endif
}

/*
 * es_posix_delete_file () - delete an external file
 *
 * return :error code
 * path(in):
 */
int
es_posix_delete_file (const char *path)
{
#if defined(CS_MODE)
  int error = ER_NET_CLIENT_DATA_RECEIVE;
  int req_error, request_size, strlen;
  char *request, *reply;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *ptr;

  reply = OR_ALIGNED_BUF_START (a_reply);

  request_size = length_const_string (path, &strlen);
  request = (char *) malloc (request_size);
  if (request == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) request_size);
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  ptr = pack_const_string_with_length (request, path, strlen);

  req_error = net_client_request (NET_SERVER_ES_DELETE_FILE, request, request_size, reply,
				  OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0, NULL, 0);
  if (!req_error)
    {
      ptr = or_unpack_int (reply, &error);
    }

  free_and_init (request);

  return error;
#else /* CS_MODE */
  return ER_FAILED;
#endif
}

/*
 * es_posix_copy_file () - copy an external file
 *
 * return :error code
 * src_path(in):
 * metaname(in):
 * new_path(out):
 */
int
es_posix_copy_file (const char *src_path, const char *metaname, char *new_path)
{
#if defined(CS_MODE)
  int error = ER_NET_CLIENT_DATA_RECEIVE;
  int req_error, request_size, path_size, srclen, metalen;
  char *request, *reply;
  OR_ALIGNED_BUF (OR_INT_SIZE * 2) a_reply;
  char *ptr;

  reply = OR_ALIGNED_BUF_START (a_reply);

  request_size = length_const_string (src_path, &srclen) + length_const_string (metaname, &metalen);
  request = (char *) malloc (request_size);
  if (request == NULL)
    {
      error = ER_OUT_OF_VIRTUAL_MEMORY;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, (size_t) request_size);
      return error;
    }

  ptr = pack_const_string_with_length (request, src_path, srclen);
  ptr = pack_const_string_with_length (ptr, metaname, metalen);

  req_error =
    net_client_request2_no_malloc (NET_SERVER_ES_COPY_FILE, request, request_size, reply,
				   OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0, new_path, &path_size);
  if (!req_error)
    {
      ptr = or_unpack_int (reply, &path_size);
      ptr = or_unpack_int (ptr, &error);
    }

  free_and_init (request);

  return error;
#else /* CS_MODE */
  return ER_FAILED;
#endif
}

/*
 * es_posix_rename_file () - rename an external file
 *
 * return :error code
 * src_path(in):
 * metaname(in):
 * new_path(out):
 */
int
es_posix_rename_file (const char *src_path, const char *metaname, char *new_path)
{
#if defined(CS_MODE)
  int error = ER_NET_CLIENT_DATA_RECEIVE;
  int req_error, request_size, path_size, srclen, metalen;
  char *request, *reply;
  OR_ALIGNED_BUF (OR_INT_SIZE * 2) a_reply;
  char *ptr;

  reply = OR_ALIGNED_BUF_START (a_reply);

  request_size = length_const_string (src_path, &srclen) + length_const_string (metaname, &metalen);
  request = (char *) malloc (request_size);
  if (request == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) request_size);
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  ptr = pack_const_string_with_length (request, src_path, srclen);
  ptr = pack_const_string_with_length (ptr, metaname, metalen);

  req_error =
    net_client_request2_no_malloc (NET_SERVER_ES_RENAME_FILE, request, request_size, reply,
				   OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0, new_path, &path_size);
  if (!req_error)
    {
      ptr = or_unpack_int (reply, &path_size);
      ptr = or_unpack_int (ptr, &error);
    }

  free_and_init (request);

  return error;
#else /* CS_MODE */
  return ER_FAILED;
#endif
}


/*
 * es_posix_get_file_size () - get the size of an esternal file
 *
 * return : file size or -1 on error
 * path(in):
 */
off_t
es_posix_get_file_size (const char *path)
{
#if defined(CS_MODE)
  INT64 tmp_int64;
  off_t file_size = -1;
  int req_error, request_size, strlen;
  char *request, *reply;
  OR_ALIGNED_BUF (OR_INT64_SIZE) a_reply;
  char *ptr;

  reply = OR_ALIGNED_BUF_START (a_reply);

  request_size = length_const_string (path, &strlen);
  request = (char *) malloc (request_size);
  if (request == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) request_size);
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  ptr = pack_const_string_with_length (request, path, strlen);

  req_error = net_client_request (NET_SERVER_ES_GET_FILE_SIZE, request, request_size, reply,
				  OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0, NULL, 0);
  if (!req_error)
    {
      ptr = or_unpack_int64 (reply, &tmp_int64);
      file_size = (off_t) tmp_int64;
    }

  free_and_init (request);

  return file_size;
#else /* CS_MODE */
  return -1;
#endif
}

/*
 * cvacuum () - Client side function for vacuuming.
 *
 * return	    : Error code.
 */
int
cvacuum (void)
{
#if defined(CS_MODE)
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = NULL;
  int err = NO_ERROR;

  /* Reply should include error code */
  reply = OR_ALIGNED_BUF_START (a_reply);

  /* Send request to server */
  err = net_client_request (NET_SERVER_VACUUM, NULL, 0, reply, OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0, NULL, 0);

  if (err == NO_ERROR)
    {
      (void) or_unpack_int (reply, &err);
    }

  return err;
#else /* !CS_MODE */
  int err;

  THREAD_ENTRY *thread_p = enter_server ();

  /* Call server function for vacuuming */
  err = xvacuum (thread_p);

  exit_server (*thread_p);

  return err;
#endif /* CS_MODE */
}

/*
 * log_get_mvcc_snapshot () - Get MVCC snapshot on server.
 *
 * return : Error code.
 */
int
log_get_mvcc_snapshot (void)
{
#if defined(CS_MODE)
  char *reply;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  int err = NO_ERROR;

  reply = OR_ALIGNED_BUF_START (a_reply);
  err =
    net_client_request (NET_SERVER_GET_MVCC_SNAPSHOT, NULL, 0, reply, OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0, NULL, 0);
  if (err != NO_ERROR)
    {
      or_unpack_int (reply, &err);
    }

  return err;
#else /* !CS_MODE */
  int err;

  THREAD_ENTRY *thread_p = enter_server ();

  err = xlogtb_get_mvcc_snapshot (thread_p);

  exit_server (*thread_p);

  return err;
#endif /* CS_MODE */
}

/*
 * locator_upgrade_instances_domain () - performs an upgrade of domain for
 *					 instances of a class
 * return : NO_ERROR if all OK, error status otherwise
 * class_oid(in) : class whose instances must be upgraded
 * attribute_id(in) : id attribute which must be upgraded
 *
 * Note: Used on ALTER CHANGE (with type change syntax) context
 */
int
locator_upgrade_instances_domain (OID * class_oid, int attribute_id)
{
#if defined(CS_MODE)
  int success = ER_FAILED, req_error;
  OR_ALIGNED_BUF (OR_OID_SIZE + OR_INT_SIZE) a_request;
  char *request, *reply, *ptr;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;

  request = OR_ALIGNED_BUF_START (a_request);
  reply = OR_ALIGNED_BUF_START (a_reply);

  if (class_oid == NULL || OID_ISNULL (class_oid) || attribute_id < 0)
    {
      return ER_QPROC_INVALID_PARAMETER;
    }

  ptr = request;
  ptr = or_pack_oid (ptr, class_oid);
  ptr = or_pack_int (ptr, attribute_id);

  req_error =
    net_client_request (NET_SERVER_LC_UPGRADE_INSTANCES_DOMAIN, request, OR_ALIGNED_BUF_SIZE (a_request), reply,
			OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0, NULL, 0);
  if (!req_error)
    {
      ptr = or_unpack_int (reply, &success);
    }

  return success;
#else /* CS_MODE */
  int error = NO_ERROR;

  THREAD_ENTRY *thread_p = enter_server ();

  error = xlocator_upgrade_instances_domain (thread_p, class_oid, attribute_id);

  exit_server (*thread_p);
  return error;
#endif /* !CS_MODE */
}

/*
 * boot_get_server_locales () - get information about server locales
 *
 * return : error code or no error
 */
int
boot_get_server_locales (LANG_COLL_COMPAT ** server_collations, LANG_LOCALE_COMPAT ** server_locales,
			 int *server_coll_cnt, int *server_locales_cnt)
{
#if defined(CS_MODE)
  int req_error;
  OR_ALIGNED_BUF (OR_INT_SIZE * 2) a_reply;
  char *reply = NULL;
  char *reply_data = NULL;
  char *ptr = NULL;
  int reply_size = 0, temp_int, i, dummy;
  char *temp_str;
  size_t size;

  assert (server_collations != NULL);
  assert (server_locales != NULL);
  assert (server_coll_cnt != NULL);
  assert (server_locales_cnt != NULL);

  *server_collations = NULL;
  *server_locales = NULL;
  *server_coll_cnt = 0;
  *server_locales_cnt = 0;

  req_error =
    net_client_request2 (NET_SERVER_BO_GET_LOCALES_INFO, NULL, 0, OR_ALIGNED_BUF_START (a_reply),
			 OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0, &reply_data, &reply_size);
  if (req_error != NO_ERROR)
    {
      goto error;
    }

  reply = OR_ALIGNED_BUF_START (a_reply);
  /* unpack data size */
  ptr = or_unpack_int (reply, &dummy);
  /* unpack error code */
  ptr = or_unpack_int (ptr, &req_error);
  if (req_error != NO_ERROR)
    {
      goto error;
    }

  assert (req_error == NO_ERROR);
  assert (reply_data != NULL);

  ptr = or_unpack_int (reply_data, server_coll_cnt);
  ptr = or_unpack_int (ptr, server_locales_cnt);

  size = (*server_coll_cnt * sizeof (LANG_COLL_COMPAT));
  *server_collations = (LANG_COLL_COMPAT *) malloc (size);
  if (*server_collations == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) size);
      req_error = ER_FAILED;
      goto error;
    }

  size = (*server_locales_cnt * sizeof (LANG_LOCALE_COMPAT));
  *server_locales = (LANG_LOCALE_COMPAT *) malloc (size);
  if (*server_locales == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) size);
      req_error = ER_FAILED;
      goto error;
    }

  for (i = 0; i < *server_coll_cnt; i++)
    {
      LANG_COLL_COMPAT *ref_coll = &((*server_collations)[i]);

      ptr = or_unpack_int (ptr, &(ref_coll->coll_id));

      ptr = or_unpack_string_nocopy (ptr, &(temp_str));
      strncpy (ref_coll->coll_name, temp_str, sizeof (ref_coll->coll_name) - 1);
      ref_coll->coll_name[sizeof (ref_coll->coll_name) - 1] = '\0';

      ptr = or_unpack_int (ptr, &(temp_int));
      ref_coll->codeset = (INTL_CODESET) temp_int;

      ptr = or_unpack_string_nocopy (ptr, &(temp_str));
      strncpy (ref_coll->checksum, temp_str, sizeof (ref_coll->checksum) - 1);
      ref_coll->checksum[sizeof (ref_coll->checksum) - 1] = '\0';
    }

  for (i = 0; i < *server_locales_cnt; i++)
    {
      LANG_LOCALE_COMPAT *ref_loc = &((*server_locales)[i]);

      ptr = or_unpack_string_nocopy (ptr, &(temp_str));
      strncpy (ref_loc->lang_name, temp_str, sizeof (ref_loc->lang_name) - 1);
      ref_loc->lang_name[sizeof (ref_loc->lang_name) - 1] = '\0';

      ptr = or_unpack_int (ptr, &(temp_int));
      ref_loc->codeset = (INTL_CODESET) temp_int;

      ptr = or_unpack_string_nocopy (ptr, &(temp_str));
      strncpy (ref_loc->checksum, temp_str, sizeof (ref_loc->checksum) - 1);
      ref_loc->checksum[sizeof (ref_loc->checksum) - 1] = '\0';
    }

  if (reply_data != NULL)
    {
      free_and_init (reply_data);
    }
  return req_error;

error:
  if (reply_data != NULL)
    {
      free_and_init (reply_data);
    }

  return req_error;
#else
  return -1;
#endif
}

/*
 * tran_lock_rep_read () - Lock the object that is common to all RR transactions
 * return : NO_ERROR if all OK, error status otherwise
 * lock_rr_tran(in) : type of lock
 *
 * Note: This is used in ALTER TABLE ... ADD COLUMN ... NOT NULL scenarios
 */
int
tran_lock_rep_read (LOCK lock_rr_tran)
{
#if defined(CS_MODE)
  int req_error;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_request;
  char *request, *reply, *ptr;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;

  request = OR_ALIGNED_BUF_START (a_request);
  reply = OR_ALIGNED_BUF_START (a_reply);

  ptr = request;
  ptr = or_pack_int (ptr, (int) lock_rr_tran);

  req_error =
    net_client_request (NET_SERVER_LOCK_RR, request, OR_ALIGNED_BUF_SIZE (a_request), reply,
			OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0, NULL, 0);
  if (!req_error)
    {
      ptr = or_unpack_int (reply, &req_error);
    }

  if (req_error == NO_ERROR)
    {
      tm_Tran_rep_read_lock = lock_rr_tran;
    }
  return req_error;
#else /* CS_MODE */
  return NO_ERROR;		/* No need to lock */
#endif /* !CS_MODE */
}

/*
 * boot_get_server_timezone_checksum () - get the timezone checksum from the 
 *					  server
 *
 * return : error code or no error
 */
int
boot_get_server_timezone_checksum (char *timezone_checksum)
{
#if defined(CS_MODE)
  int req_error;
  OR_ALIGNED_BUF (OR_INT_SIZE * 2) a_reply;
  char *reply = NULL;
  char *reply_data = NULL;
  char *ptr = NULL;
  int reply_size = 0, dummy;
  char *temp_str;

  req_error =
    net_client_request2 (NET_SERVER_TZ_GET_CHECKSUM, NULL, 0, OR_ALIGNED_BUF_START (a_reply),
			 OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0, &reply_data, &reply_size);
  if (req_error != NO_ERROR)
    {
      goto error;
    }

  reply = OR_ALIGNED_BUF_START (a_reply);
  /* unpack data size */
  ptr = or_unpack_int (reply, &dummy);
  /* unpack error code */
  ptr = or_unpack_int (ptr, &req_error);
  if (req_error != NO_ERROR)
    {
      goto error;
    }

  assert (reply_data != NULL);

  ptr = or_unpack_string_nocopy (reply_data, &(temp_str));
  strncpy (timezone_checksum, temp_str, TZ_CHECKSUM_SIZE);
  timezone_checksum[TZ_CHECKSUM_SIZE] = '\0';

  if (reply_data != NULL)
    {
      free_and_init (reply_data);
    }
  return req_error;

error:
  if (reply_data != NULL)
    {
      free_and_init (reply_data);
    }

  return req_error;
#else
  return -1;
#endif
}

int
chksum_insert_repl_log_and_demote_table_lock (REPL_INFO * repl_info, const OID * class_oidp)
{
#if defined(CS_MODE)
  int req_error, success = ER_FAILED;
  int request_size = 0, strlen1, strlen2, strlen3, strlen4;
  char *request = NULL, *ptr;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply;
  REPL_INFO_SBR *repl_stmt;

  reply = OR_ALIGNED_BUF_START (a_reply);

  switch (repl_info->repl_info_type)
    {
    case REPL_INFO_TYPE_SBR:
      repl_stmt = (REPL_INFO_SBR *) repl_info->info;
      request_size = (OR_OID_SIZE	/* class oid */
		      + OR_INT_SIZE	/* REPL_INFO.REPL_INFO_TYPE */
		      + OR_INT_SIZE	/* REPL_INFO_SCHEMA.statement_type */
		      + length_const_string (repl_stmt->name, &strlen1)
		      + length_const_string (repl_stmt->stmt_text, &strlen2)
		      + length_const_string (repl_stmt->db_user, &strlen3)
		      + length_const_string (repl_stmt->sys_prm_context, &strlen4));

      request = (char *) malloc (request_size);
      if (request == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) request_size);
	  return ER_FAILED;
	}

      ptr = or_pack_oid (request, class_oidp);
      ptr = or_pack_int (ptr, REPL_INFO_TYPE_SBR);
      ptr = or_pack_int (ptr, repl_stmt->statement_type);
      ptr = pack_const_string_with_length (ptr, repl_stmt->name, strlen1);
      ptr = pack_const_string_with_length (ptr, repl_stmt->stmt_text, strlen2);
      ptr = pack_const_string_with_length (ptr, repl_stmt->db_user, strlen3);
      ptr = pack_const_string_with_length (ptr, repl_stmt->sys_prm_context, strlen4);
      req_error =
	net_client_request (NET_SERVER_CHKSUM_REPL, request, request_size, reply, OR_ALIGNED_BUF_SIZE (a_reply),
			    NULL, 0, NULL, 0);
      if (!req_error)
	{
	  or_unpack_int (reply, &success);
	}

      free_and_init (request);
      break;

    default:
      break;
    }

  return success;
#else /* CS_MODE */
  int r = ER_FAILED;

  THREAD_ENTRY *thread_p = enter_server ();

  r = xchksum_insert_repl_log_and_demote_table_lock (thread_p, repl_info, class_oidp);

  exit_server (*thread_p);

  return r;
#endif /* !CS_MODE */
}

/*
 * log_does_active_user_exist () - Cbeck the specified user is an
 * 			active user or not
 *   return: error code
 *   user_name(in) :
 *   existed (out) : true mean user is an active user
 */
int
log_does_active_user_exist (const char *user_name, bool * existed)
{
#if defined(CS_MODE)
  int error;
  int xexisted;
  int req_error, request_size, strlen;
  char *request;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply;

  *existed = false;

  reply = OR_ALIGNED_BUF_START (a_reply);

  request_size = length_const_string (user_name, &strlen);
  request = (char *) malloc (request_size);
  if (request == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) request_size);
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  (void) pack_const_string_with_length (request, user_name, strlen);

  req_error = net_client_request (NET_SERVER_AU_DOES_ACTIVE_USER_EXIST, request, request_size, reply,
				  OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0, NULL, 0);
  if (!req_error)
    {
      or_unpack_int (reply, &xexisted);
      if ((bool) xexisted)
	{
	  *existed = true;
	}
      error = NO_ERROR;
    }
  else
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
    }

  free_and_init (request);

  return error;
#else /* CS_MODE */

  /* in SA_MODE, no other active user */
  *existed = false;
  return NO_ERROR;
#endif /* !CS_MODE */
}

/*
 * locator_redistribute_partition_data () - redistribute partition data
 *
 * return : error code
 *
 * thread_p (in)      :
 * class_oid (in)     : parent class OID
 * no_oids (in)	      : number of OIDs in the list (promoted partitions)
 * oid_list (in)      : partition OID list (promoted partitions)
 */
int
locator_redistribute_partition_data (OID * class_oid, int no_oids, OID * oid_list)
{
#if defined(CS_MODE)
  int success = ER_FAILED, req_error;
  char *request, *reply, *ptr;
  int request_size = 0;
  int i;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;

  reply = OR_ALIGNED_BUF_START (a_reply);

  if (oid_list == NULL || no_oids < 1)
    {
      return ER_QPROC_INVALID_PARAMETER;
    }

  request_size = OR_OID_SIZE + OR_INT_SIZE + no_oids * OR_OID_SIZE;

  request = (char *) malloc (request_size);
  if (request == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) request_size);
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  ptr = request;
  ptr = or_pack_oid (ptr, class_oid);
  ptr = or_pack_int (ptr, no_oids);

  for (i = 0; i < no_oids; i++)
    {
      ptr = or_pack_oid (ptr, &oid_list[i]);
    }

  req_error =
    net_client_request (NET_SERVER_LC_REDISTRIBUTE_PARTITION_DATA, request, request_size, reply,
			OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0, NULL, 0);
  if (!req_error)
    {
      ptr = or_unpack_int (reply, &success);
    }

  free_and_init (request);

  return success;

#else /* CS_MODE */
  int success = ER_FAILED;

  THREAD_ENTRY *thread_p = enter_server ();

  success = xlocator_redistribute_partition_data (thread_p, class_oid, no_oids, oid_list);

  exit_server (*thread_p);

  return success;
#endif /* !CS_MODE */
}

/*
 * netcl_spacedb () - client-side function to get database space info
 *
 * return           : error code
 * spaceall (out)   : output aggregated space information
 * spacevols (out)  : if not NULL, output space information per volume
 * spacefiles (out) : if not NULL, out detailed space information on file usage
 */
int
netcl_spacedb (SPACEDB_ALL * spaceall, SPACEDB_ONEVOL ** spacevols, SPACEDB_FILES * spacefiles)
{
#if defined (CS_MODE)
  int error_code = NO_ERROR;
  OR_ALIGNED_BUF (2 * OR_INT_SIZE) a_request;
  char *request;
  OR_ALIGNED_BUF (2 * OR_INT_SIZE) a_reply;
  char *reply;
  char *data_reply = NULL;
  int data_reply_size = 0;
  char *ptr;

  request = OR_ALIGNED_BUF_START (a_request);
  reply = OR_ALIGNED_BUF_START (a_reply);

  ptr = or_pack_int (request, spacevols != NULL ? 1 : 0);
  ptr = or_pack_int (ptr, spacefiles != NULL ? 1 : 0);

  error_code = net_client_request2 (NET_SERVER_SPACEDB, request, OR_ALIGNED_BUF_SIZE (a_request), reply,
				    OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0, &data_reply, &data_reply_size);
  if (error_code != NO_ERROR)
    {
      assert (data_reply == NULL);
      return error_code;
    }
  ptr = or_unpack_int (reply, &data_reply_size);
  ptr = or_unpack_int (ptr, &error_code);
  if (error_code != NO_ERROR)
    {
      /* error */
      ASSERT_ERROR ();
      return error_code;
    }
  if (data_reply == NULL)
    {
      assert_release (false);
      return ER_FAILED;
    }
  ptr = or_unpack_spacedb (data_reply, spaceall, spacevols, spacefiles);
  if (ptr == NULL)
    {
      ASSERT_ERROR_AND_SET (error_code);
      return error_code;
    }
  assert ((ptr - data_reply) == data_reply_size);

  free_and_init (data_reply);
  return NO_ERROR;

#else	/* !CS_MODE */	       /* SA_MDOE */
  int error_code = ER_FAILED;

  THREAD_ENTRY *thread_p = enter_server ();
  error_code = disk_spacedb (thread_p, spaceall, spacevols);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
    }
  else if (spacefiles != NULL)
    {
      error_code = file_spacedb (thread_p, spacefiles);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	}
    }
  exit_server (*thread_p);

  return error_code;
#endif /* SA_MODE */
}
