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
 * network_interface_cl.c - Interface functions for client requests.
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include <string.h>

#include "porting.h"
#include "network.h"
#include "network_interface_cl.h"
#include "memory_alloc.h"
#include "storage_common.h"
#if defined(CS_MODE)
#include "server_interface.h"
#else
#include "xserver_interface.h"
#endif
#include "boot_sr.h"
#include "locator_sr.h"
#include "oid.h"
#include "error_manager.h"
#include "object_representation.h"
#include "log_comm.h"
#include "arithmetic.h"
#include "query_executor.h"
#include "transaction_cl.h"
#include "language_support.h"
#include "statistics.h"
#include "system_parameter.h"
#include "transaction_sr.h"
#include "jsp_sr.h"
#include "replication.h"

/*
 * TODO: To use db_clear_private_heap instead of db_destroy_private_heap
 */
#if defined(DEBUG_DB_ON_SERVER)
#define ENTER_SERVER() \
  do \
    { \
      if (db_on_server) \
        { \
          int x = 1; \
          fprintf (stderr, "call client code from server at %s:%d\n", \
                   __FILE__, __LINE__); \
          while (x) \
            { \
              sleep (1); \
            } \
        } \
      db_on_server = 1; \
      if (private_heap_id != 0) \
        { \
          db_destroy_private_heap (NULL, private_heap_id); \
        } \
      private_heap_id = db_create_private_heap (); \
      if (instant_heap_id != 0) \
        { \
          db_destroy_instant_heap (NULL, instant_heap_id); \
        } \
      instant_heap_id = db_create_instant_heap (); \
    } \
  while (0)
#else /* DEBUG_DB_ON_SERVER */
#define ENTER_SERVER() \
  do \
    { \
      db_on_server = 1; \
      if (private_heap_id != 0) \
        { \
          db_destroy_private_heap (NULL, private_heap_id); \
        } \
      private_heap_id = db_create_private_heap (); \
      if (instant_heap_id != 0) \
        { \
          db_destroy_instant_heap (NULL, instant_heap_id); \
        } \
      instant_heap_id = db_create_instant_heap (); \
    } \
  while (0)
#endif /* DEBUG_DB_ON_SERVER */

#define EXIT_SERVER() \
  do \
    { \
      if (private_heap_id != 0) \
        { \
          db_destroy_private_heap (NULL, private_heap_id); \
          private_heap_id = 0; \
        } \
      if (instant_heap_id != 0) \
        { \
          db_destroy_instant_heap (NULL, instant_heap_id); \
          instant_heap_id = 0; \
        } \
      db_on_server = 0; \
    } \
  while (0)

#define NET_COPY_AREA_SENDRECV_SIZE (OR_INT_SIZE * 3)
#define NET_SENDRECV_BUFFSIZE (OR_INT_SIZE)

#define MAX_ALIGN 7


unsigned int net_Interrupt_enabled = 1;	/* default is on */

/*
 * Flag to indicate whether we've crossed the client/server boundary.
 * It really only comes into play in standalone.
 */
unsigned int db_on_server = 0;

#if defined(CS_MODE)
static char *pack_const_string (char *buffer, const char *cstring);
static int length_const_string (const char *cstring);
#endif /* CS_MODE */

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
  return or_pack_string (buffer, ((char *) cstring));
}

/*
 * length_const_string -
 *
 * return:
 *
 *   cstring(in):
 *
 * NOTE:
 */
static int
length_const_string (const char *cstring)
{
  return or_packed_string_length (cstring);
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
 *   class_oid(in):
 *   class_chn(in):
 *   prefetch(in):
 *   fetch_copyarea(in):
 *
 * NOTE:
 */
int
locator_fetch (OID * oidp, int chn, LOCK lock, OID * class_oid,
	       int class_chn, int prefetch, LC_COPYAREA ** fetch_copyarea)
{
#if defined(CS_MODE)
  int success = ER_FAILED;
  int req_error;
  char *ptr;
  OR_ALIGNED_BUF ((OR_OID_SIZE * 2) + (OR_INT_SIZE * 4)) a_request;
  char *request;
  OR_ALIGNED_BUF (NET_COPY_AREA_SENDRECV_SIZE + OR_INT_SIZE) a_reply;
  char *reply;

  request = OR_ALIGNED_BUF_START (a_request);
  reply = OR_ALIGNED_BUF_START (a_reply);

  ptr = or_pack_oid (request, oidp);
  ptr = or_pack_int (ptr, chn);
  ptr = or_pack_lock (ptr, lock);
  ptr = or_pack_oid (ptr, class_oid);
  ptr = or_pack_int (ptr, class_chn);
  ptr = or_pack_int (ptr, prefetch);
  *fetch_copyarea = NULL;

  req_error = net_client_request_recv_copyarea (SERVER_LC_FETCH,
						request,
						OR_ALIGNED_BUF_SIZE
						(a_request), reply,
						OR_ALIGNED_BUF_SIZE (a_reply),
						fetch_copyarea);
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

  ENTER_SERVER ();

  success = xlocator_fetch (NULL, oidp, chn, lock, class_oid, class_chn,
			    prefetch, fetch_copyarea);

  EXIT_SERVER ();

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
locator_get_class (OID * class_oid, int class_chn, const OID * oid, LOCK lock,
		   int prefetching, LC_COPYAREA ** fetch_copyarea)
{
#if defined(CS_MODE)
  int req_error;
  char *ptr;
  int return_value = ER_FAILED;
  OR_ALIGNED_BUF ((OR_OID_SIZE * 2) + (OR_INT_SIZE * 3)) a_request;
  char *request;
  OR_ALIGNED_BUF (NET_COPY_AREA_SENDRECV_SIZE + OR_OID_SIZE +
		  OR_INT_SIZE) a_reply;
  char *reply;

  request = OR_ALIGNED_BUF_START (a_request);
  reply = OR_ALIGNED_BUF_START (a_reply);

  ptr = or_pack_oid (request, class_oid);
  ptr = or_pack_int (ptr, class_chn);
  ptr = or_pack_oid (ptr, (OID *) oid);
  ptr = or_pack_lock (ptr, lock);
  ptr = or_pack_int (ptr, prefetching);
  *fetch_copyarea = NULL;

  req_error = net_client_request_recv_copyarea (SERVER_LC_GET_CLASS,
						request,
						OR_ALIGNED_BUF_SIZE
						(a_request), reply,
						OR_ALIGNED_BUF_SIZE (a_reply),
						fetch_copyarea);
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

  ENTER_SERVER ();

  success = xlocator_get_class (NULL, class_oid, class_chn, oid, lock,
				prefetching, fetch_copyarea);

  EXIT_SERVER ();

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
 *   class_oidp(in):
 *   nobjects(in):
 *   nfetched(in):
 *   last_oidp(in):
 *   fetch_copyarea(in):
 *
 * NOTE:
 */
int
locator_fetch_all (const HFID * hfid, LOCK * lock, OID * class_oidp,
		   int *nobjects, int *nfetched, OID * last_oidp,
		   LC_COPYAREA ** fetch_copyarea)
{
#if defined(CS_MODE)
  int req_error;
  char *ptr;
  int return_value = ER_FAILED;
  OR_ALIGNED_BUF (OR_HFID_SIZE + (OR_INT_SIZE * 3) +
		  (OR_OID_SIZE * 2)) a_request;
  char *request;
  OR_ALIGNED_BUF (NET_COPY_AREA_SENDRECV_SIZE + (OR_INT_SIZE * 4) +
		  OR_OID_SIZE) a_reply;
  char *reply;

  request = OR_ALIGNED_BUF_START (a_request);
  reply = OR_ALIGNED_BUF_START (a_reply);

  ptr = or_pack_hfid (request, hfid);
  ptr = or_pack_lock (ptr, *lock);
  ptr = or_pack_oid (ptr, class_oidp);
  ptr = or_pack_int (ptr, *nobjects);
  ptr = or_pack_int (ptr, *nfetched);
  ptr = or_pack_oid (ptr, last_oidp);
  *fetch_copyarea = NULL;

  req_error = net_client_request_recv_copyarea (SERVER_LC_FETCHALL,
						request,
						OR_ALIGNED_BUF_SIZE
						(a_request), reply,
						OR_ALIGNED_BUF_SIZE (a_reply),
						fetch_copyarea);
  if (!req_error)
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

  ENTER_SERVER ();

  success =
    xlocator_fetch_all (NULL, hfid, lock, class_oidp, nobjects, nfetched,
			last_oidp, fetch_copyarea);

  EXIT_SERVER ();

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
locator_does_exist (OID * oidp, int chn, LOCK lock, OID * class_oid,
		    int class_chn, int need_fetching, int prefetch,
		    LC_COPYAREA ** fetch_copyarea)
{
#if defined(CS_MODE)
  int does_exist = LC_ERROR;
  int req_error;
  char *ptr;
  OR_ALIGNED_BUF ((OR_OID_SIZE * 2) + (OR_INT_SIZE * 5)) a_request;
  char *request;
  OR_ALIGNED_BUF (NET_COPY_AREA_SENDRECV_SIZE +
		  OR_INT_SIZE + OR_OID_SIZE) a_reply;
  char *reply;
  OID class_;

  request = OR_ALIGNED_BUF_START (a_request);
  reply = OR_ALIGNED_BUF_START (a_reply);

  ptr = or_pack_oid (request, oidp);
  ptr = or_pack_int (ptr, chn);
  ptr = or_pack_lock (ptr, lock);
  ptr = or_pack_oid (ptr, class_oid);
  ptr = or_pack_int (ptr, class_chn);
  ptr = or_pack_int (ptr, need_fetching);
  ptr = or_pack_int (ptr, prefetch);

  if (need_fetching)
    {
      *fetch_copyarea = NULL;
    }

  req_error = net_client_request_recv_copyarea (SERVER_LC_DOESEXIST,
						request,
						OR_ALIGNED_BUF_SIZE
						(a_request), reply,
						OR_ALIGNED_BUF_SIZE (a_reply),
						fetch_copyarea);
  if (!req_error)
    {
      ptr = reply + NET_COPY_AREA_SENDRECV_SIZE;
      ptr = or_unpack_int (ptr, &does_exist);
      ptr = or_unpack_oid (ptr, &class_);
      if (does_exist == LC_EXIST && class_oid)
	COPY_OID (class_oid, &class_);
    }
  else
    {
      *fetch_copyarea = NULL;
    }

  return does_exist;
#else /* CS_MODE */
  int success = ER_FAILED;

  ENTER_SERVER ();

  success = xlocator_does_exist (NULL, oidp, chn, lock, class_oid, class_chn,
				 need_fetching, prefetch, fetch_copyarea);

  EXIT_SERVER ();

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
    net_client_request_recv_copyarea (SERVER_LC_NOTIFY_ISOLATION_INCONS, NULL,
				      0, reply, OR_ALIGNED_BUF_SIZE (a_reply),
				      synch_copyarea);

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

  ENTER_SERVER ();

  success = xlocator_notify_isolation_incons (NULL, synch_copyarea);

  EXIT_SERVER ();

  return success;
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
locator_force (LC_COPYAREA * copy_area)
{
#if defined(CS_MODE)
  int success = ER_FAILED;
  OR_ALIGNED_BUF (OR_INT_SIZE * 5 + OR_OID_SIZE) a_request;
  char *request;
  char *request_ptr;
  OR_ALIGNED_BUF (OR_INT_SIZE * 3) a_reply;
  char *reply;
  char *desc_ptr = NULL;
  int desc_size;
  char *content_ptr;
  int content_size;
  int num_objs = 0;
  int req_error;
  LC_COPYAREA_MANYOBJS *mobjs;

  mobjs = LC_MANYOBJS_PTR_IN_COPYAREA (copy_area);

  request = OR_ALIGNED_BUF_START (a_request);
  reply = OR_ALIGNED_BUF_START (a_reply);

  num_objs = locator_send_copy_area (copy_area, &content_ptr, &content_size,
				     &desc_ptr, &desc_size);

  request_ptr = or_pack_int (request, num_objs);
  request_ptr = or_pack_int (request_ptr, mobjs->start_multi_update);
  request_ptr = or_pack_int (request_ptr, mobjs->end_multi_update);
  request_ptr = or_pack_oid (request_ptr, &mobjs->class_oid);
  request_ptr = or_pack_int (request_ptr, desc_size);
  request_ptr = or_pack_int (request_ptr, content_size);

  req_error = net_client_request_3_data (SERVER_LC_FORCE,
					 request,
					 OR_ALIGNED_BUF_SIZE (a_request),
					 desc_ptr, desc_size, content_ptr,
					 content_size, reply,
					 OR_ALIGNED_BUF_SIZE (a_reply),
					 desc_ptr, desc_size, NULL, 0);
  if (!req_error)
    {
      (void) or_unpack_int (reply, &success);
      if (success == NO_ERROR)
	{
	  locator_unpack_copy_area_descriptor (num_objs, copy_area, desc_ptr);
	}
    }
  if (desc_ptr)
    {
      free_and_init (desc_ptr);
    }

  return success;
#else /* CS_MODE */
  int success = ER_FAILED;

  ENTER_SERVER ();

  success = xlocator_force (NULL, copy_area);

  EXIT_SERVER ();

  return success;
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
  OR_ALIGNED_BUF (NET_SENDRECV_BUFFSIZE + NET_COPY_AREA_SENDRECV_SIZE +
		  OR_INT_SIZE) a_reply;
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

      req_error = net_client_request_2recv_nomalloc_buffer_and_malloc_copyarea
	(SERVER_LC_FETCH_LOCKSET,
	 request, OR_ALIGNED_BUF_SIZE (a_request),
	 reply, OR_ALIGNED_BUF_SIZE (a_reply),
	 packed, send_size, packed, packed_size, fetch_copyarea, &eid);
    }
  else
    {
      /* Don't need to send the lockset information any more */
      packed = lockset->packed;
      packed_size = lockset->packed_size;
      req_error = net_client_recv_nomalloc_buffer_and_malloc_copyarea
	(SERVER_LC_FETCH_LOCKSET,
	 reply, OR_ALIGNED_BUF_SIZE (a_reply),
	 packed, packed_size, fetch_copyarea, eid);
    }

  if (!req_error)
    {
      ptr = reply + NET_SENDRECV_BUFFSIZE + NET_COPY_AREA_SENDRECV_SIZE;
      ptr = or_unpack_int (ptr, &success);
      if (success == NO_ERROR)
	{
	  locator_unpack_lockset (lockset, lockset->first_fetch_lockset_call,
				  false);
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

  ENTER_SERVER ();

  success = xlocator_fetch_lockset (NULL, lockset, fetch_copyarea);

  EXIT_SERVER ();

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
locator_fetch_all_reference_lockset (OID * oid, int chn, OID * class_oid,
				     int class_chn, LOCK lock,
				     int quit_on_errors, int prune_level,
				     LC_LOCKSET ** lockset,
				     LC_COPYAREA ** fetch_copyarea)
{
#if defined(CS_MODE)
  int success = ER_FAILED;
  int req_error;
  char *ptr;
  OR_ALIGNED_BUF ((OR_OID_SIZE * 2) + (OR_INT_SIZE * 5)) a_request;
  char *request;
  OR_ALIGNED_BUF (NET_SENDRECV_BUFFSIZE + NET_COPY_AREA_SENDRECV_SIZE +
		  OR_INT_SIZE) a_reply;
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

  req_error = net_client_request_3recv_malloc_buffer_and_malloc_copyarea
    (SERVER_LC_FETCH_ALLREFS_LOCKSET,
     request, OR_ALIGNED_BUF_SIZE (a_request),
     reply, OR_ALIGNED_BUF_SIZE (a_reply),
     NULL, 0, &packed, &packed_size, fetch_copyarea);

  if (!req_error)
    {
      ptr = reply + NET_SENDRECV_BUFFSIZE + NET_COPY_AREA_SENDRECV_SIZE;
      ptr = or_unpack_int (ptr, &success);
      if (packed_size > 0 && packed != NULL)
	{
	  *lockset = locator_allocate_and_unpack_lockset (packed, packed_size,
							  true, true, false);
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

  ENTER_SERVER ();

  success = xlocator_fetch_all_reference_lockset (NULL, oid, chn, class_oid,
						  class_chn, lock,
						  quit_on_errors, prune_level,
						  lockset, fetch_copyarea);

  EXIT_SERVER ();

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
  int request_size;
  char *request;
  OR_ALIGNED_BUF (OR_INT_SIZE + OR_OID_SIZE) a_reply;
  char *reply;

  reply = OR_ALIGNED_BUF_START (a_reply);

  request_size = length_const_string (class_name) + OR_OID_SIZE + OR_INT_SIZE;
  request = (char *) malloc (request_size);
  if (request)
    {
      ptr = pack_const_string (request, class_name);
      ptr = or_pack_oid (ptr, class_oid);
      ptr = or_pack_lock (ptr, lock);

      req_error = net_client_request (SERVER_LC_FIND_CLASSOID,
				      request, request_size, reply,
				      OR_ALIGNED_BUF_SIZE (a_reply),
				      NULL, 0, NULL, 0);
      if (!req_error)
	{
	  ptr = or_unpack_int (reply, &xfound);
	  found = (LC_FIND_CLASSNAME) xfound;
	  ptr = or_unpack_oid (ptr, class_oid);
	}
      free_and_init (request);
    }

  return found;

#else /* CS_MODE */
  LC_FIND_CLASSNAME found = LC_CLASSNAME_ERROR;

  ENTER_SERVER ();

  found = xlocator_find_class_oid (NULL, class_name, class_oid, lock);

  EXIT_SERVER ();

  return found;
#endif /* !CS_MODE */
}

/*
 * locator_reserve_class_name -
 *
 * return:
 *
 *   class_name(in):
 *   class_oid(in):
 *
 * NOTE:
 */
LC_FIND_CLASSNAME
locator_reserve_class_name (const char *class_name, OID * class_oid)
{
#if defined(CS_MODE)
  LC_FIND_CLASSNAME reserved = LC_CLASSNAME_ERROR;
  int xreserved;
  int request_size;
  int req_error;
  char *request, *ptr;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply;

  reply = OR_ALIGNED_BUF_START (a_reply);

  request_size = length_const_string (class_name) + OR_OID_SIZE;
  request = (char *) malloc (request_size);
  if (request)
    {
      ptr = pack_const_string (request, class_name);
      ptr = or_pack_oid (ptr, class_oid);

      req_error = net_client_request (SERVER_LC_RESERVE_CLASSNAME,
				      request, request_size, reply,
				      OR_ALIGNED_BUF_SIZE (a_reply),
				      NULL, 0, NULL, 0);
      if (!req_error)
	{
	  (void) or_unpack_int (reply, &xreserved);
	  reserved = (LC_FIND_CLASSNAME) xreserved;
	}

      free_and_init (request);
    }

  return reserved;

#else /* CS_MODE */
  LC_FIND_CLASSNAME reserved = LC_CLASSNAME_ERROR;

  ENTER_SERVER ();

  reserved = xlocator_reserve_class_name (NULL, class_name, class_oid);

  EXIT_SERVER ();

  return reserved;
#endif /* !CS_MODE */
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
  int req_error, request_size;
  char *request;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply;

  reply = OR_ALIGNED_BUF_START (a_reply);

  request_size = length_const_string (class_name);
  request = (char *) malloc (request_size);
  if (request)
    {
      (void) pack_const_string (request, class_name);
      req_error = net_client_request (SERVER_LC_DELETE_CLASSNAME,
				      request, request_size, reply,
				      OR_ALIGNED_BUF_SIZE (a_reply),
				      NULL, 0, NULL, 0);
      if (!req_error)
	{
	  or_unpack_int (reply, &xdeleted);
	  deleted = (LC_FIND_CLASSNAME) xdeleted;
	}

      free_and_init (request);
    }
  return deleted;

#else /* CS_MODE */
  LC_FIND_CLASSNAME deleted = LC_CLASSNAME_ERROR;

  ENTER_SERVER ();

  deleted = xlocator_delete_class_name (NULL, class_name);

  EXIT_SERVER ();

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
locator_rename_class_name (const char *old_name, const char *new_name,
			   OID * class_oid)
{
#if defined(CS_MODE)
  LC_FIND_CLASSNAME renamed = LC_CLASSNAME_ERROR;
  int xrenamed;
  int request_size;
  int req_error;
  char *request, *ptr;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply;

  reply = OR_ALIGNED_BUF_START (a_reply);

  request_size = length_const_string (old_name)
    + length_const_string (new_name) + OR_OID_SIZE;
  request = (char *) malloc (request_size);
  if (request)
    {
      ptr = pack_const_string (request, old_name);
      ptr = pack_const_string (ptr, new_name);
      ptr = or_pack_oid (ptr, class_oid);

      req_error = net_client_request (SERVER_LC_RENAME_CLASSNAME,
				      request, request_size, reply,
				      OR_ALIGNED_BUF_SIZE (a_reply),
				      NULL, 0, NULL, 0);
      if (!req_error)
	{
	  ptr = or_unpack_int (reply, &xrenamed);
	  renamed = (LC_FIND_CLASSNAME) xrenamed;
	}
      free_and_init (request);
    }

  return renamed;
#else /* CS_MODE */
  LC_FIND_CLASSNAME renamed = LC_CLASSNAME_ERROR;

  ENTER_SERVER ();

  renamed = xlocator_rename_class_name (NULL, old_name, new_name, class_oid);

  EXIT_SERVER ();

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
locator_assign_oid (const HFID * hfid, OID * perm_oid, int expected_length,
		    OID * class_oid, const char *class_name)
{
#if defined(CS_MODE)
  int success = ER_FAILED;
  int request_size;
  int req_error;
  char *request, *ptr;
  OR_ALIGNED_BUF (OR_OID_SIZE + OR_INT_SIZE) a_reply;
  char *reply;

  reply = OR_ALIGNED_BUF_START (a_reply);

  request_size = OR_HFID_SIZE + OR_INT_SIZE + OR_OID_SIZE
    + length_const_string (class_name);
  request = (char *) malloc (request_size);
  if (request)
    {
      ptr = or_pack_hfid (request, hfid);
      ptr = or_pack_int (ptr, expected_length);
      ptr = or_pack_oid (ptr, class_oid);
      ptr = pack_const_string (ptr, class_name);

      req_error = net_client_request (SERVER_LC_ASSIGN_OID,
				      request, request_size, reply,
				      OR_ALIGNED_BUF_SIZE (a_reply),
				      NULL, 0, NULL, 0);
      if (!req_error)
	{
	  ptr = or_unpack_int (reply, &success);
	  ptr = or_unpack_oid (ptr, perm_oid);
	}
      free_and_init (request);
    }

  return success;
#else /* CS_MODE */
  int success = ER_FAILED;

  ENTER_SERVER ();

  success = xlocator_assign_oid (NULL, hfid, perm_oid, expected_length,
				 class_oid, class_name);

  EXIT_SERVER ();

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
  char *buffer;
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
      return ER_FAILED;
    }

  if (locator_pack_oid_set (buffer + OR_INT_SIZE, oidset) == NULL)
    {
      free_and_init (buffer);
      return ER_FAILED;
    }

  req_error = net_client_request (SERVER_LC_ASSIGN_OID_BATCH,
				  buffer, packed_size,
				  buffer, packed_size, NULL, 0, NULL, 0);

  if (!req_error)
    {
      (void) or_unpack_int (buffer, &success);
      if (success == NO_ERROR)
	{
	  if (locator_unpack_oid_set_to_exist (buffer + OR_INT_SIZE, oidset)
	      == false)
	    {
	      success = ER_FAILED;
	    }
	}
    }

  free_and_init (buffer);

  return success;
#else /* CS_MODE */
  int success = ER_FAILED;

  ENTER_SERVER ();

  success = xlocator_assign_oid_batch (NULL, oidset);

  EXIT_SERVER ();

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
 *   guessed_class_oids(in):
 *   guessed_class_chns(in):
 *   quit_on_errors(in):
 *   lockhint(in):
 *   fetch_copyarea(in):
 *
 * NOTE:
 */
LC_FIND_CLASSNAME
locator_find_lockhint_class_oids (int num_classes,
				  const char **many_classnames,
				  LOCK * many_locks,
				  int *many_need_subclasses,
				  OID * guessed_class_oids,
				  int *guessed_class_chns, int quit_on_errors,
				  LC_LOCKHINT ** lockhint,
				  LC_COPYAREA ** fetch_copyarea)
{
#if defined(CS_MODE)
  LC_FIND_CLASSNAME allfind = LC_CLASSNAME_ERROR;
  int xallfind;
  int req_error;
  char *ptr;
  int request_size;
  char *request;
  char *packed;
  int packed_size;
  OR_ALIGNED_BUF (NET_SENDRECV_BUFFSIZE + NET_COPY_AREA_SENDRECV_SIZE +
		  OR_INT_SIZE) a_reply;
  char *reply;
  int i;

  reply = OR_ALIGNED_BUF_START (a_reply);

  *lockhint = NULL;
  *fetch_copyarea = NULL;

  request_size = OR_INT_SIZE + OR_INT_SIZE;
  for (i = 0; i < num_classes; i++)
    {
      request_size += (length_const_string (many_classnames[i]) +
		       OR_INT_SIZE + OR_INT_SIZE + OR_OID_SIZE + OR_INT_SIZE);
    }

  request = (char *) malloc (request_size);
  if (request == NULL)
    {
      return allfind;
    }

  ptr = or_pack_int (request, num_classes);
  ptr = or_pack_int (ptr, quit_on_errors);
  for (i = 0; i < num_classes; i++)
    {
      ptr = pack_const_string (ptr, many_classnames[i]);
      ptr = or_pack_lock (ptr, many_locks[i]);
      ptr = or_pack_int (ptr, many_need_subclasses[i]);
      ptr = or_pack_oid (ptr, &guessed_class_oids[i]);
      ptr = or_pack_int (ptr, guessed_class_chns[i]);
    }

  req_error = net_client_request_3recv_malloc_buffer_and_malloc_copyarea
    (SERVER_LC_FIND_LOCKHINT_CLASSOIDS,
     request, request_size,
     reply, OR_ALIGNED_BUF_SIZE (a_reply),
     NULL, 0, &packed, &packed_size, fetch_copyarea);

  if (!req_error)
    {
      ptr = reply + NET_SENDRECV_BUFFSIZE + NET_COPY_AREA_SENDRECV_SIZE;
      ptr = or_unpack_int (ptr, &xallfind);
      allfind = (LC_FIND_CLASSNAME) xallfind;

      if (packed_size > 0 && packed != NULL)
	{
	  *lockhint =
	    locator_allocate_and_unpack_lockhint (packed, packed_size, true,
						  false);
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

  return allfind;
#else /* CS_MODE */
  LC_FIND_CLASSNAME allfind = LC_CLASSNAME_ERROR;

  ENTER_SERVER ();

  allfind =
    xlocator_find_lockhint_class_oids (NULL, num_classes, many_classnames,
				       many_locks, many_need_subclasses,
				       guessed_class_oids, guessed_class_chns,
				       quit_on_errors, lockhint,
				       fetch_copyarea);

  EXIT_SERVER ();

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
locator_fetch_lockhint_classes (LC_LOCKHINT * lockhint,
				LC_COPYAREA ** fetch_copyarea)
{
#if defined(CS_MODE)
  static int eid;		/* TODO: remove static */
  int success = ER_FAILED;
  int req_error;
  char *ptr;
  OR_ALIGNED_BUF (NET_SENDRECV_BUFFSIZE + NET_COPY_AREA_SENDRECV_SIZE +
		  OR_INT_SIZE) a_reply;
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

      req_error = net_client_request_2recv_nomalloc_buffer_and_malloc_copyarea
	(SERVER_LC_FETCH_LOCKHINT_CLASSES, request,
	 OR_ALIGNED_BUF_SIZE (a_request), reply,
	 OR_ALIGNED_BUF_SIZE (a_reply), packed, send_size, packed,
	 packed_size, fetch_copyarea, &eid);
    }
  else
    {
      /* Don't need to send the lockhint information any more */
      packed = lockhint->packed;
      packed_size = lockhint->packed_size;
      req_error = net_client_recv_nomalloc_buffer_and_malloc_copyarea
	(SERVER_LC_FETCH_LOCKHINT_CLASSES,
	 reply, OR_ALIGNED_BUF_SIZE (a_reply),
	 packed, packed_size, fetch_copyarea, eid);
    }

  if (!req_error)
    {
      ptr = reply + NET_SENDRECV_BUFFSIZE + NET_COPY_AREA_SENDRECV_SIZE;
      ptr = or_unpack_int (ptr, &success);
      if (success == NO_ERROR)
	{
	  locator_unpack_lockhint (lockhint,
				   lockhint->first_fetch_lockhint_call);
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

  ENTER_SERVER ();

  success = xlocator_fetch_lockhint_classes (NULL, lockhint, fetch_copyarea);

  EXIT_SERVER ();

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
 *
 * NOTE:
 */
int
heap_create (HFID * hfid, const OID * class_oid)
{
#if defined(CS_MODE)
  int error = ER_NET_CLIENT_DATA_RECEIVE;
  int req_error;
  char *ptr;
  OR_ALIGNED_BUF (OR_HFID_SIZE + OR_OID_SIZE) a_request;
  char *request;
  OR_ALIGNED_BUF (OR_INT_SIZE + OR_HFID_SIZE) a_reply;
  char *reply;

  request = OR_ALIGNED_BUF_START (a_request);
  reply = OR_ALIGNED_BUF_START (a_reply);

  ptr = or_pack_hfid (request, hfid);
  ptr = or_pack_oid (ptr, (OID *) class_oid);
  req_error = net_client_request (SERVER_HEAP_CREATE,
				  request, OR_ALIGNED_BUF_SIZE (a_request),
				  reply,
				  OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0,
				  NULL, 0);
  if (!req_error)
    {
      ptr = or_unpack_errcode (reply, &error);
      ptr = or_unpack_hfid (ptr, hfid);
    }

  return error;
#else /* CS_MODE */
  int success;

  ENTER_SERVER ();

  success = xheap_create (NULL, hfid, class_oid);

  EXIT_SERVER ();

  return success;
#endif /* !CS_MODE */
}

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

  req_error = net_client_request (SERVER_HEAP_DESTROY,
				  request, OR_ALIGNED_BUF_SIZE (a_request),
				  reply,
				  OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0,
				  NULL, 0);
  if (!req_error)
    {
      ptr = or_unpack_errcode (reply, &error);
    }

  return error;
#else /* CS_MODE */
  int success;

  ENTER_SERVER ();

  success = xheap_destroy (NULL, hfid);

  EXIT_SERVER ();

  return success;
#endif /* !CS_MODE */
}

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
heap_destroy_newly_created (const HFID * hfid)
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

  req_error = net_client_request (SERVER_HEAP_DESTROY_WHEN_NEW,
				  request, OR_ALIGNED_BUF_SIZE (a_request),
				  reply,
				  OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0,
				  NULL, 0);
  if (!req_error)
    {
      ptr = or_unpack_errcode (reply, &error);
    }

  return error;
#else /* CS_MODE */
  int success = ER_FAILED;

  ENTER_SERVER ();

  success = xheap_destroy_newly_created (NULL, hfid);

  EXIT_SERVER ();

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

  req_error = net_client_request (SERVER_DISK_TOTALPGS,
				  request, OR_ALIGNED_BUF_SIZE (a_request),
				  reply,
				  OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0,
				  NULL, 0);
  if (!req_error)
    {
      (void) or_unpack_int (reply, &npages);
    }

  return npages;
#else /* CS_MODE */
  DKNPAGES npages = 0;

  ENTER_SERVER ();

  npages = xdisk_get_total_numpages (NULL, volid);

  EXIT_SERVER ();

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

  req_error = net_client_request (SERVER_DISK_FREEPGS,
				  request, OR_ALIGNED_BUF_SIZE (a_request),
				  reply,
				  OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0,
				  NULL, 0);
  if (!req_error)
    {
      (void) or_unpack_int (reply, &npages);
    }

  return npages;

#else /* CS_MODE */
  DKNPAGES npages = 0;

  ENTER_SERVER ();

  npages = xdisk_get_free_numpages (NULL, volid);

  EXIT_SERVER ();

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
  char *area;
  int area_size;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_request;
  char *request;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply;
  char *remark = NULL;

  request = OR_ALIGNED_BUF_START (a_request);
  reply = OR_ALIGNED_BUF_START (a_reply);

  (void) or_pack_int (request, (int) volid);

  req_error = net_client_request2 (SERVER_DISK_REMARKS,
				   request, OR_ALIGNED_BUF_SIZE (a_request),
				   reply, OR_ALIGNED_BUF_SIZE (a_reply),
				   NULL, 0, &area, &area_size);
  if (!req_error)
    {
      or_unpack_int (reply, &area_size);
      or_unpack_string (area, &remark);
      free_and_init (area);
    }

  return remark;
#else /* CS_MODE */
  char *remark = NULL;

  ENTER_SERVER ();

  remark = xdisk_get_remarks (NULL, volid);

  EXIT_SERVER ();

  return remark;
#endif /* !CS_MODE */
}

/*
 * disk_get_purpose -
 *
 * return:
 *
 *   volid(in):
 *
 * NOTE:
 */
DISK_VOLPURPOSE
disk_get_purpose (VOLID volid)
{
#if defined(CS_MODE)
  DISK_VOLPURPOSE purpose = DISK_UNKNOWN_PURPOSE;
  int int_purpose;
  int req_error;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_request;
  char *request;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply;

  request = OR_ALIGNED_BUF_START (a_request);
  reply = OR_ALIGNED_BUF_START (a_reply);

  (void) or_pack_int (request, (int) volid);

  req_error = net_client_request (SERVER_DISK_PURPOSE,
				  request, OR_ALIGNED_BUF_SIZE (a_request),
				  reply,
				  OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0,
				  NULL, 0);
  if (!req_error)
    {
      or_unpack_int (reply, &int_purpose);
      purpose = (DB_VOLPURPOSE) int_purpose;
    }

  return purpose;
#else /* CS_MODE */
  DISK_VOLPURPOSE purpose = DISK_UNKNOWN_PURPOSE;

  ENTER_SERVER ();

  purpose = xdisk_get_purpose (NULL, volid);

  EXIT_SERVER ();

  return purpose;
#endif /* !CS_MODE */
}

/*
 * disk_get_purpose_and_total_free_numpages -
 *
 * return:
 *
 *   volid(in):
 *   vol_purpose(in):
 *   vol_ntotal_pages(in):
 *   vol_nfree_pages(in):
 *
 * NOTE:
 */
VOLID
disk_get_purpose_and_total_free_numpages (VOLID volid,
					  DISK_VOLPURPOSE * vol_purpose,
					  DKNPAGES * vol_ntotal_pages,
					  DKNPAGES * vol_nfree_pages)
{
#if defined(CS_MODE)
  int temp;
  int req_error;
  char *ptr;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_request;
  char *request;
  OR_ALIGNED_BUF (OR_INT_SIZE * 4) a_reply;
  char *reply;

  request = OR_ALIGNED_BUF_START (a_request);
  reply = OR_ALIGNED_BUF_START (a_reply);

  (void) or_pack_int (request, (int) volid);

  req_error = net_client_request (SERVER_DISK_PURPOSE_TOTALPGS_AND_FREEPGS,
				  request, OR_ALIGNED_BUF_SIZE (a_request),
				  reply,
				  OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0,
				  NULL, 0);
  if (!req_error)
    {
      ptr = or_unpack_int (reply, &temp);
      *vol_purpose = (DB_VOLPURPOSE) temp;
      ptr = or_unpack_int (ptr, vol_ntotal_pages);
      ptr = or_unpack_int (ptr, vol_nfree_pages);
      ptr = or_unpack_int (ptr, &temp);
      volid = temp;
    }
  else
    {
      volid = NULL_VOLID;
    }

  return volid;
#else /* CS_MODE */

  ENTER_SERVER ();

  volid = xdisk_get_purpose_and_total_free_numpages (NULL, volid, vol_purpose,
						     vol_ntotal_pages,
						     vol_nfree_pages);

  EXIT_SERVER ();

  return volid;
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
  char *area;
  int area_size;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_request;
  char *request;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply;
  char *name;

  request = OR_ALIGNED_BUF_START (a_request);
  reply = OR_ALIGNED_BUF_START (a_reply);

  (void) or_pack_int (request, (int) volid);

  req_error = net_client_request2 (SERVER_DISK_VLABEL,
				   request, OR_ALIGNED_BUF_SIZE (a_request),
				   reply, OR_ALIGNED_BUF_SIZE (a_reply),
				   NULL, 0, &area, &area_size);
  if (!req_error)
    {
      or_unpack_int (reply, &area_size);
      or_unpack_string_nocopy (area, &name);
      strcpy (vol_fullname, name);
      free_and_init (area);
    }
  else
    {
      vol_fullname = NULL;
    }

  return vol_fullname;
#else /* CS_MODE */

  ENTER_SERVER ();

  vol_fullname = xdisk_get_fullname (NULL, volid, vol_fullname);

  EXIT_SERVER ();

  return vol_fullname;
#endif /* !CS_MODE */
}

/*
 * log_client_get_first_postpone -
 *
 * return:
 *
 *   next_lsa(in):
 *
 * NOTE:
 */
LOG_COPY *
log_client_get_first_postpone (LOG_LSA * next_lsa)
{
#if defined(CS_MODE)
  int req_error;
  char *ptr;
  LOG_COPY *log_area = NULL;
  OR_ALIGNED_BUF (OR_INT_SIZE * 3 + OR_LOG_LSA_SIZE) a_reply;
  char *reply;

  reply = OR_ALIGNED_BUF_START (a_reply);

  req_error = net_client_request_recv_logarea
    (SERVER_LOG_CLIENT_GET_FIRST_POSTPONE,
     NULL, 0, reply, OR_ALIGNED_BUF_SIZE (a_reply), &log_area);
  if (!req_error)
    {
      ptr = or_unpack_log_lsa (reply + OR_INT_SIZE * 3, next_lsa);
    }
  else
    {
      log_area = NULL;
    }

  return log_area;
#else /* CS_MODE */
  LOG_COPY *log_area = NULL;

  ENTER_SERVER ();

  log_area = xlog_client_get_first_postpone (NULL, next_lsa);

  EXIT_SERVER ();

  return log_area;
#endif /* !CS_MODE */
}

/*
 * log_client_get_next_postpone -
 *
 * return:
 *
 *   next_lsa(in):
 *
 * NOTE:
 */
LOG_COPY *
log_client_get_next_postpone (LOG_LSA * next_lsa)
{
#if defined(CS_MODE)
  int req_error;
  char *ptr;
  LOG_COPY *log_area = NULL;
  OR_ALIGNED_BUF (OR_LOG_LSA_SIZE) a_request;
  char *request;
  OR_ALIGNED_BUF (OR_INT_SIZE * 3 + OR_LOG_LSA_SIZE) a_reply;
  char *reply;

  request = OR_ALIGNED_BUF_START (a_request);
  reply = OR_ALIGNED_BUF_START (a_reply);

  ptr = or_pack_log_lsa (request, next_lsa);

  req_error = net_client_request_recv_logarea
    (SERVER_LOG_CLIENT_GET_NEXT_POSTPONE, request,
     OR_ALIGNED_BUF_SIZE (a_request), reply, OR_ALIGNED_BUF_SIZE (a_reply),
     &log_area);

  if (!req_error)
    {
      ptr = or_unpack_log_lsa (reply + OR_INT_SIZE * 3, next_lsa);
    }
  else
    {
      log_area = NULL;
    }

  return log_area;
#else /* CS_MODE */
  LOG_COPY *log_area = NULL;

  ENTER_SERVER ();

  log_area = xlog_client_get_next_postpone (NULL, next_lsa);

  EXIT_SERVER ();

  return log_area;
#endif /* !CS_MODE */
}

/*
 * log_client_get_first_undo -
 *
 * return:
 *
 *   next_lsa(in):
 *
 * NOTE:
 */
LOG_COPY *
log_client_get_first_undo (LOG_LSA * next_lsa)
{
#if defined(CS_MODE)
  LOG_COPY *log_area = NULL;
  int req_error;
  char *ptr;
  OR_ALIGNED_BUF (OR_INT_SIZE * 3 + OR_LOG_LSA_SIZE) a_reply;
  char *reply;

  reply = OR_ALIGNED_BUF_START (a_reply);

  req_error = net_client_request_recv_logarea
    (SERVER_LOG_CLIENT_GET_FIRST_UNDO,
     NULL, 0, reply, OR_ALIGNED_BUF_SIZE (a_reply), &log_area);

  if (!req_error)
    {
      ptr = or_unpack_log_lsa (reply + OR_INT_SIZE * 3, next_lsa);
    }
  else
    {
      log_area = NULL;
    }

  return log_area;
#else /* CS_MODE */
  LOG_COPY *log_area = NULL;

  ENTER_SERVER ();

  log_area = xlog_client_get_first_undo (NULL, next_lsa);

  EXIT_SERVER ();

  return log_area;
#endif /* !CS_MODE */
}

/*
 * log_client_get_next_undo -
 *
 * return:
 *
 *   next_lsa(in):
 *
 * NOTE:
 */
LOG_COPY *
log_client_get_next_undo (LOG_LSA * next_lsa)
{
#if defined(CS_MODE)
  LOG_COPY *log_area = NULL;
  int req_error;
  char *ptr;
  OR_ALIGNED_BUF (OR_LOG_LSA_SIZE) a_request;
  char *request;
  OR_ALIGNED_BUF (OR_INT_SIZE * 3 + OR_LOG_LSA_SIZE) a_reply;
  char *reply;

  request = OR_ALIGNED_BUF_START (a_request);
  reply = OR_ALIGNED_BUF_START (a_reply);

  ptr = or_pack_log_lsa (request, next_lsa);

  req_error = net_client_request_recv_logarea
    (SERVER_LOG_CLIENT_GET_NEXT_UNDO, request,
     OR_ALIGNED_BUF_SIZE (a_request), reply, OR_ALIGNED_BUF_SIZE (a_reply),
     &log_area);

  if (!req_error)
    {
      ptr = or_unpack_log_lsa (reply + OR_INT_SIZE * 3, next_lsa);
    }
  else
    {
      log_area = NULL;
    }

  return log_area;
#else /* CS_MODE */
  LOG_COPY *log_area = NULL;

  ENTER_SERVER ();

  log_area = xlog_client_get_next_undo (NULL, next_lsa);

  EXIT_SERVER ();

  return log_area;
#endif /* !CS_MODE */
}

/*
 * log_client_unknown_state_abort_get_first_undo -
 *
 * return:
 *
 *   next_lsa(in):
 *
 * NOTE:
 */
LOG_COPY *
log_client_unknown_state_abort_get_first_undo (LOG_LSA * next_lsa)
{
#if defined(CS_MODE)
  LOG_COPY *log_area = NULL;
  int req_error;
  char *ptr;
  OR_ALIGNED_BUF (OR_INT_SIZE * 3 + OR_LOG_LSA_SIZE) a_reply;
  char *reply;

  reply = OR_ALIGNED_BUF_START (a_reply);

  req_error = net_client_request_recv_logarea
    (SERVER_LOG_CLIENT_UNKNOWN_STATE_ABORT_GET_FIRST_UNDO,
     NULL, 0, reply, OR_ALIGNED_BUF_SIZE (a_reply), &log_area);

  if (!req_error)
    {
      ptr = or_unpack_log_lsa (reply + OR_INT_SIZE * 3, next_lsa);
    }
  else
    {
      log_area = NULL;
    }

  return log_area;
#else /* CS_MODE */
  LOG_COPY *log_area = NULL;

  ENTER_SERVER ();

  log_area = xlog_client_unknown_state_abort_get_first_undo (NULL, next_lsa);

  EXIT_SERVER ();

  return log_area;
#endif /* !CS_MODE */
}

/*
 * log_append_client_undo -
 *
 * return:
 *
 *   rcv_index(in):
 *   length(in):
 *   data(in):
 *
 * NOTE:
 */
void
log_append_client_undo (LOG_RCVCLIENT_INDEX rcv_index, int length, void *data)
{
#if defined(CS_MODE)
  int req_error;
  OR_ALIGNED_BUF (OR_INT_SIZE * 2) a_request;
  char *request;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply;

  request = OR_ALIGNED_BUF_START (a_request);
  reply = OR_ALIGNED_BUF_START (a_reply);

  (void) or_pack_int (request, rcv_index);

  req_error = net_client_request (SERVER_LOG_CLIENT_UNDO,
				  request, OR_ALIGNED_BUF_SIZE (a_request),
				  reply,
				  OR_ALIGNED_BUF_SIZE (a_reply),
				  (char *) data, length, NULL, 0);
#else /* CS_MODE */

  ENTER_SERVER ();

  xlog_append_client_undo (NULL, rcv_index, length, data);

  EXIT_SERVER ();
#endif /* !CS_MODE */
}

/*
 * log_append_client_postpone -
 *
 * return:
 *
 *   rcv_index(in):
 *   length(in):
 *   data(in):
 *
 * NOTE:
 */
void
log_append_client_postpone (LOG_RCVCLIENT_INDEX rcv_index, int length,
			    void *data)
{
#if defined(CS_MODE)
  int req_error;
  OR_ALIGNED_BUF (OR_INT_SIZE * 3) a_request;
  char *request;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply;

  request = OR_ALIGNED_BUF_START (a_request);
  reply = OR_ALIGNED_BUF_START (a_reply);

  (void) or_pack_int (request, rcv_index);

  req_error = net_client_request (SERVER_LOG_CLIENT_POSTPONE,
				  request, OR_ALIGNED_BUF_SIZE (a_request),
				  reply,
				  OR_ALIGNED_BUF_SIZE (a_reply),
				  (char *) data, length, NULL, 0);
#else /* CS_MODE */

  ENTER_SERVER ();

  xlog_append_client_postpone (NULL, rcv_index, length, data);

  EXIT_SERVER ();
#endif /* !CS_MODE */
}

/*
 * log_has_finished_client_postpone -
 *
 * return:
 *
 * NOTE:
 */
TRAN_STATE
log_has_finished_client_postpone (void)
{
#if defined(CS_MODE)
  TRAN_STATE tran_state = TRAN_UNACTIVE_UNKNOWN;
  int req_error, tran_state_int;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply;

  reply = OR_ALIGNED_BUF_START (a_reply);

  req_error = net_client_request (SERVER_LOG_HAS_FINISHED_CLIENT_POSTPONE,
				  NULL, 0, reply,
				  OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0,
				  NULL, 0);
  if (!req_error)
    {
      (void) or_unpack_int (reply, &tran_state_int);
      tran_state = (TRAN_STATE) tran_state_int;
    }

  return tran_state;
#else /* CS_MODE */
  TRAN_STATE tran_state = TRAN_UNACTIVE_UNKNOWN;

  ENTER_SERVER ();

  tran_state = xlog_client_complete_postpone (NULL);

  EXIT_SERVER ();

  return tran_state;
#endif /* !CS_MODE */
}

/*
 * log_has_finished_client_undo -
 *
 * return:
 *
 * NOTE:
 */
TRAN_STATE
log_has_finished_client_undo (void)
{
#if defined(CS_MODE)
  TRAN_STATE tran_state = TRAN_UNACTIVE_UNKNOWN;
  int req_error, tran_state_int;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply;

  reply = OR_ALIGNED_BUF_START (a_reply);

  req_error = net_client_request (SERVER_LOG_HAS_FINISHED_CLIENT_UNDO,
				  NULL, 0, reply,
				  OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0,
				  NULL, 0);
  if (!req_error)
    {
      (void) or_unpack_int (reply, &tran_state_int);
      tran_state = (TRAN_STATE) tran_state_int;
    }

  return tran_state;
#else /* CS_MODE */
  TRAN_STATE tran_state = TRAN_UNACTIVE_UNKNOWN;

  ENTER_SERVER ();

  tran_state = xlog_client_complete_undo (NULL);

  EXIT_SERVER ();

  return tran_state;
#endif /* !CS_MODE */
}

/*
 * log_reset_waitsecs -
 *
 * return:
 *
 *   waitsecs(in):    in miliseconds
 *
 * NOTE:
 */
int
log_reset_waitsecs (int waitsecs)
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

  (void) or_pack_int (request, waitsecs);

  req_error = net_client_request (SERVER_LOG_RESET_WAITSECS,
				  request, OR_ALIGNED_BUF_SIZE (a_request),
				  reply,
				  OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0,
				  NULL, 0);
  if (!req_error)
    {
      (void) or_unpack_int (reply, &wait);
    }

  return wait;
#else /* CS_MODE */
  int wait = -1;

  ENTER_SERVER ();

  wait = xlogtb_reset_wait_secs (NULL, waitsecs);

  EXIT_SERVER ();

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
log_reset_isolation (TRAN_ISOLATION isolation, bool unlock_by_isolation)
{
#if defined(CS_MODE)
  int req_error, error_code = ER_NET_CLIENT_DATA_RECEIVE;
  OR_ALIGNED_BUF (OR_INT_SIZE * 2) a_request;
  char *request, *ptr;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply;

  request = OR_ALIGNED_BUF_START (a_request);
  reply = OR_ALIGNED_BUF_START (a_reply);

  ptr = or_pack_int (request, (int) isolation);
  ptr = or_pack_int (ptr, (int) unlock_by_isolation);

  req_error = net_client_request (SERVER_LOG_RESET_ISOLATION,
				  request, OR_ALIGNED_BUF_SIZE (a_request),
				  reply,
				  OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0,
				  NULL, 0);
  if (!req_error)
    {
      or_unpack_int (reply, &error_code);
    }

  return error_code;
#else /* CS_MODE */
  int error_code = NO_ERROR;

  ENTER_SERVER ();

  error_code = xlogtb_reset_isolation (NULL, isolation, unlock_by_isolation);

  EXIT_SERVER ();

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
  int req_error;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_request;
  char *request;

  request = OR_ALIGNED_BUF_START (a_request);

  /*
   * only send the interrupt if the server can handle it without taking
   * down the whole machine like Solaris is apt to do.
   */
  if (net_Interrupt_enabled)
    {
      or_pack_int (request, set);

      req_error = net_client_req_no_reply_via_oob (SERVER_LOG_SET_INTERRUPT,
						   request,
						   OR_ALIGNED_BUF_SIZE
						   (a_request));
    }
#else /* CS_MODE */

  ENTER_SERVER ();

  xlogtb_set_interrupt (NULL, set);

  EXIT_SERVER ();
#endif /* !CS_MODE */
}

/* AsyncCommit */
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

  /* TODO: change msg function */
  req_error = net_client_request_recv_stream (SERVER_LOG_DUMP_STAT,
					      NULL, 0, NULL, 0,
					      NULL, 0, outfp);
#else /* CS_MODE */

  ENTER_SERVER ();

  xlogpb_dump_stat (outfp);

  EXIT_SERVER ();
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
  int req_error, tran_state_int;
  char *ptr;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_request;
  char *request;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply;

  request = OR_ALIGNED_BUF_START (a_request);
  reply = OR_ALIGNED_BUF_START (a_reply);

  (void) or_pack_int (request, (int) retain_lock);

  req_error = net_client_request (SERVER_TM_SERVER_COMMIT,
				  request, OR_ALIGNED_BUF_SIZE (a_request),
				  reply, OR_ALIGNED_BUF_SIZE (a_reply),
				  NULL, 0, NULL, 0);
  if (!req_error)
    {
      ptr = or_unpack_int (reply, &tran_state_int);
      tran_state = (TRAN_STATE) tran_state_int;
    }

  net_cleanup_client_queues ();

  return tran_state;
#else /* CS_MODE */
  TRAN_STATE tran_state = TRAN_UNACTIVE_UNKNOWN;

  ENTER_SERVER ();

  tran_state = xtran_server_commit (NULL, retain_lock);

  EXIT_SERVER ();

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
  int req_error, tran_state_int;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply;

  reply = OR_ALIGNED_BUF_START (a_reply);

  req_error = net_client_request (SERVER_TM_SERVER_ABORT,
				  NULL, 0,
				  reply, OR_ALIGNED_BUF_SIZE (a_reply),
				  NULL, 0, NULL, 0);
  if (!req_error)
    {
      (void) or_unpack_int (reply, &tran_state_int);
      tran_state = (TRAN_STATE) tran_state_int;
    }

  net_cleanup_client_queues ();

  return tran_state;
#else /* CS_MODE */
  TRAN_STATE tran_state = TRAN_UNACTIVE_UNKNOWN;

  ENTER_SERVER ();

  tran_state = xtran_server_abort (NULL);

  EXIT_SERVER ();

  return tran_state;
#endif /* !CS_MODE */
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

  req_error = net_client_request (SERVER_TM_ISBLOCKED,
				  request, OR_ALIGNED_BUF_SIZE (a_request),
				  reply,
				  OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0,
				  NULL, 0);
  if (!req_error)
    {
      (void) or_unpack_int (reply, &temp);
      blocked = (temp == 1) ? true : false;
    }

  return blocked;
#else /* CS_MODE */
  bool blocked = false;

  ENTER_SERVER ();

  blocked = xtran_is_blocked (NULL, tran_index);

  EXIT_SERVER ();

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

  req_error = net_client_request (SERVER_TM_SERVER_HAS_UPDATED,
				  NULL, 0, reply,
				  OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0,
				  NULL, 0);
  if (!req_error)
    {
      or_unpack_int (reply, &has_updated);
    }

  return has_updated;
#else /* CS_MODE */
  int has_updated = 0;		/* TODO */

  ENTER_SERVER ();

  has_updated = xtran_server_has_updated (NULL);

  EXIT_SERVER ();

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

  req_error = net_client_request (SERVER_TM_SERVER_ISACTIVE_AND_HAS_UPDATED,
				  NULL, 0, reply,
				  OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0,
				  NULL, 0);
  if (!req_error)
    {
      or_unpack_int (reply, &isactive_and_has_updated);
    }

  return isactive_and_has_updated;
#else /* CS_MODE */
  int isactive_and_has_updated = 0;	/* TODO */

  ENTER_SERVER ();

  isactive_and_has_updated = xtran_server_is_active_and_has_updated (NULL);

  EXIT_SERVER ();

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

  req_error = net_client_request (SERVER_TM_WAIT_SERVER_ACTIVE_TRANS,
				  NULL, 0, reply,
				  OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0,
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

  req_error = net_client_request (SERVER_TM_SERVER_SET_GTRINFO,
				  request, OR_ALIGNED_BUF_SIZE (a_request),
				  reply, OR_ALIGNED_BUF_SIZE (a_reply),
				  (char *) info, size, NULL, 0);
  if (!req_error)
    {
      or_unpack_int (reply, &success);
    }

  return success;
#else /* CS_MODE */
  int success;

  ENTER_SERVER ();

  success = xtran_server_set_global_tran_info (NULL, gtrid, info, size);

  EXIT_SERVER ();

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
  req_error = net_client_request2_no_malloc (SERVER_TM_SERVER_GET_GTRINFO,
					     request,
					     OR_ALIGNED_BUF_SIZE (a_request),
					     reply,
					     OR_ALIGNED_BUF_SIZE (a_reply),
					     (char *) buffer, size,
					     (char *) buffer, &size);
  if (!req_error)
    {
      ptr = or_unpack_int (reply, &size);
      ptr = or_unpack_int (ptr, &success);
    }

  return success;

#else /* CS_MODE */
  int success;

  ENTER_SERVER ();

  success = xtran_server_get_global_tran_info (NULL, gtrid, buffer, size);

  EXIT_SERVER ();

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

  req_error = net_client_request (SERVER_TM_SERVER_2PC_START,
				  NULL, 0, reply,
				  OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0,
				  NULL, 0);
  if (!req_error)
    {
      or_unpack_int (reply, &gtrid);
    }

  return gtrid;
#else /* CS_MODE */
  int gtrid = NULL_TRANID;

  ENTER_SERVER ();

  gtrid = xtran_server_2pc_start (NULL);

  EXIT_SERVER ();

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

  req_error = net_client_request (SERVER_TM_SERVER_2PC_PREPARE,
				  NULL, 0, reply,
				  OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0,
				  NULL, 0);
  if (!req_error)
    {
      or_unpack_int (reply, (int *) &state);
    }

  return state;
#else /* CS_MODE */
  TRAN_STATE state = TRAN_UNACTIVE_UNKNOWN;

  ENTER_SERVER ();

  state = xtran_server_2pc_prepare (NULL);

  EXIT_SERVER ();

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
  if (reply)
    {
      or_pack_int (request, size);

      req_error = net_client_request (SERVER_TM_SERVER_2PC_RECOVERY_PREPARED,
				      request,
				      OR_ALIGNED_BUF_SIZE (a_request), reply,
				      reply_size, NULL, 0, NULL, 0);
      if (!req_error)
	{
	  ptr = or_unpack_int (reply, &count);
	  for (i = 0; i < count && i < size; i++)
	    {
	      ptr = or_unpack_int (ptr, &gtrids[i]);
	    }
	}
    }

  return count;
#else /* CS_MODE */
  int count;

  ENTER_SERVER ();

  count = xtran_server_2pc_recovery_prepared (NULL, gtrids, size);

  EXIT_SERVER ();

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

  req_error = net_client_request (SERVER_TM_SERVER_2PC_ATTACH_GT, request,
				  OR_ALIGNED_BUF_SIZE (a_request), reply,
				  OR_ALIGNED_BUF_SIZE (a_reply),
				  NULL, 0, NULL, 0);
  if (!req_error)
    {
      or_unpack_int (reply, &tran_index);
    }

  return tran_index;
#else /* CS_MODE */
  int tran_index = NULL_TRAN_INDEX;

  ENTER_SERVER ();

  tran_index = xtran_server_2pc_attach_global_tran (NULL, gtrid);

  EXIT_SERVER ();

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
  req_error = net_client_request (SERVER_TM_SERVER_2PC_PREPARE_GT,
				  request, OR_ALIGNED_BUF_SIZE (a_request),
				  reply,
				  OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0,
				  NULL, 0);
  if (!req_error)
    {
      ptr = or_unpack_int (reply, &tran_state_int);
      tran_state = (TRAN_STATE) tran_state_int;
    }

  return tran_state;
#else /* CS_MODE */
  TRAN_STATE tran_state = TRAN_UNACTIVE_UNKNOWN;

  ENTER_SERVER ();

  tran_state = xtran_server_2pc_prepare_global_tran (NULL, gtrid);

  EXIT_SERVER ();

  return tran_state;
#endif /* !CS_MODE */
}

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
  OR_ALIGNED_BUF (OR_INT_SIZE + OR_LOG_LSA_SIZE) a_reply;
  char *reply;
  char *ptr;

  reply = OR_ALIGNED_BUF_START (a_reply);

  req_error = net_client_request (SERVER_TM_SERVER_START_TOPOP,
				  NULL, 0, reply,
				  OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0,
				  NULL, 0);
  if (!req_error)
    {
      ptr = or_unpack_int (reply, &success);
      ptr = or_unpack_log_lsa (ptr, topop_lsa);
    }
  return success;
#else /* CS_MODE */
  int success = ER_FAILED;

  ENTER_SERVER ();

  if (xtran_server_start_topop (NULL, topop_lsa) != NO_ERROR)
    {
      success = ER_FAILED;
    }
  else
    {
      success = NO_ERROR;
    }

  EXIT_SERVER ();

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
  OR_ALIGNED_BUF (OR_INT_SIZE + OR_LOG_LSA_SIZE) a_reply;
  char *reply;
  char *ptr;

  request = OR_ALIGNED_BUF_START (a_request);
  reply = OR_ALIGNED_BUF_START (a_reply);

  ptr = or_pack_int (request, (int) result);
  req_error = net_client_request (SERVER_TM_SERVER_END_TOPOP,
				  NULL, 0, reply,
				  OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0,
				  NULL, 0);
  if (!req_error)
    {
      ptr = or_unpack_int (reply, &tran_state_int);
      tran_state = (TRAN_STATE) tran_state_int;
      ptr = or_unpack_log_lsa (ptr, topop_lsa);
    }
  return tran_state;
#else /* CS_MODE */
  TRAN_STATE tran_state = TRAN_UNACTIVE_UNKNOWN;

  ENTER_SERVER ();

  tran_state = xtran_server_end_topop (NULL, result, topop_lsa);

  EXIT_SERVER ();

  return (tran_state);
#endif /* !CS_MODE */
}

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
  int req_error;
  int request_size;
  char *request;
  OR_ALIGNED_BUF (OR_INT_SIZE + OR_LOG_LSA_SIZE) a_reply;
  char *reply;
  char *ptr;

  reply = OR_ALIGNED_BUF_START (a_reply);

  request_size = length_const_string (savept_name);
  request = (char *) malloc (request_size);
  if (request)
    {
      ptr = pack_const_string (request, savept_name);
      req_error = net_client_request (SERVER_TM_SERVER_SAVEPOINT,
				      request, request_size, reply,
				      OR_ALIGNED_BUF_SIZE (a_reply),
				      NULL, 0, NULL, 0);
      if (!req_error)
	{
	  ptr = or_unpack_int (reply, &success);
	  ptr = or_unpack_log_lsa (ptr, savept_lsa);
	}
      free_and_init (request);
    }

  return success;

#else /* CS_MODE */
  int success = ER_FAILED;

  ENTER_SERVER ();

  success = xtran_server_savepoint (NULL, savept_name, savept_lsa);

  EXIT_SERVER ();

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
  int req_error, tran_state_int;
  int request_size;
  char *request;
  OR_ALIGNED_BUF (OR_INT_SIZE + OR_LOG_LSA_SIZE) a_reply;
  char *reply;
  char *ptr;

  reply = OR_ALIGNED_BUF_START (a_reply);

  request_size = length_const_string (savept_name);
  request = (char *) malloc (request_size);
  if (request)
    {
      ptr = pack_const_string (request, savept_name);
      req_error = net_client_request (SERVER_TM_SERVER_PARTIAL_ABORT,
				      request, request_size, reply,
				      OR_ALIGNED_BUF_SIZE (a_reply),
				      NULL, 0, NULL, 0);
      if (!req_error)
	{
	  ptr = or_unpack_int (reply, &tran_state_int);
	  tran_state = (TRAN_STATE) tran_state_int;
	  ptr = or_unpack_log_lsa (ptr, savept_lsa);
	}
      free_and_init (request);
    }

  return tran_state;
#else /* CS_MODE */
  TRAN_STATE tran_state = TRAN_UNACTIVE_UNKNOWN;

  ENTER_SERVER ();

  tran_state = xtran_server_partial_abort (NULL, savept_name, savept_lsa);

  EXIT_SERVER ();

  return tran_state;
#endif /* !CS_MODE */
}

/*
 * lock_dump -
 *
 * return:
 *
 *   outfp(in):
 *
 * NOTE:
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

  /* TODO: simpler function */
  req_error = net_client_request_recv_stream (SERVER_LK_DUMP,
					      NULL, 0, NULL, 0,
					      NULL, 0, outfp);
#else /* CS_MODE */

  ENTER_SERVER ();

  xlock_dump (NULL, outfp);

  EXIT_SERVER ();
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
boot_initialize_server (int print_version, bool db_overwrite,
			PGLENGTH db_desired_pagesize, const char *db_name,
			const char *db_path, const char *vol_path,
			const char *db_comments, DKNPAGES db_npages,
			const char *file_addmore_vols,
			const char *db_server_host, const char *log_path,
			DKNPAGES log_npages, OID * rootclass_oid,
			HFID * rootclass_hfid, const char *client_prog_name,
			const char *client_user_name,
			const char *client_host_name, int client_process_id,
			int client_lock_wait, TRAN_ISOLATION client_isolation)
{
#if defined(CS_MODE)
  int tran_index = NULL_TRAN_INDEX;
  int request_size;
  int req_error;
  char *request, *ptr;
  OR_ALIGNED_BUF (OR_INT_SIZE + OR_OID_SIZE + OR_HFID_SIZE) a_reply;
  char *reply;

  reply = OR_ALIGNED_BUF_START (a_reply);

  request_size = OR_INT_SIZE + OR_INT_SIZE + OR_INT_SIZE
    + length_const_string (db_name) + length_const_string (db_path)
    + length_const_string (vol_path) + length_const_string (db_comments)
    + OR_INT_SIZE + length_const_string (file_addmore_vols)
    + length_const_string (db_server_host) + length_const_string (log_path)
    + OR_INT_SIZE + length_const_string (client_prog_name)
    + length_const_string (client_user_name)
    + length_const_string (client_host_name)
    + OR_INT_SIZE + OR_INT_SIZE + OR_INT_SIZE;

  request = (char *) malloc (request_size);
  if (request)
    {
      ptr = or_pack_int (request, print_version);
      ptr = or_pack_int (ptr, db_overwrite);
      or_pack_int (ptr, (int) db_desired_pagesize);
      ptr = pack_const_string (ptr, db_name);
      ptr = pack_const_string (ptr, db_path);
      ptr = pack_const_string (ptr, vol_path);
      ptr = pack_const_string (ptr, db_comments);
      ptr = or_pack_int (ptr, db_npages);
      ptr = pack_const_string (ptr, file_addmore_vols);
      ptr = pack_const_string (ptr, db_server_host);
      ptr = pack_const_string (ptr, log_path);
      ptr = or_pack_int (ptr, log_npages);
      ptr = pack_const_string (ptr, client_prog_name);
      ptr = pack_const_string (ptr, client_user_name);
      ptr = pack_const_string (ptr, client_host_name);
      ptr = or_pack_int (ptr, client_process_id);
      ptr = or_pack_int (ptr, client_lock_wait);
      ptr = or_pack_int (ptr, (int) client_isolation);

      req_error = net_client_request (SERVER_BO_INIT_SERVER,
				      request, request_size,
				      reply, OR_ALIGNED_BUF_SIZE (a_reply),
				      NULL, 0, NULL, 0);
      if (!req_error)
	{
	  ptr = or_unpack_int (reply, &tran_index);
	  ptr = or_unpack_oid (ptr, rootclass_oid);
	  ptr = or_unpack_hfid (ptr, rootclass_hfid);
	}
      free_and_init (request);
    }

  return tran_index;
#else /* CS_MODE */
  int tran_index = NULL_TRAN_INDEX;

  ENTER_SERVER ();

  tran_index = xboot_initialize_server (NULL, print_version, db_overwrite,
					db_desired_pagesize, db_name, db_path,
					vol_path, db_comments, db_npages,
					file_addmore_vols, db_server_host,
					log_path, log_npages, rootclass_oid,
					rootclass_hfid, client_prog_name,
					client_user_name, client_host_name,
					client_process_id, client_lock_wait,
					client_isolation);

  EXIT_SERVER ();

  return (tran_index);
#endif /* !CS_MODE */
}

/*
 * boot_register_client -
 *
 * return:
 *
 *   print_restart(in):
 *   db_name(in):
 *   rootclass_oid(in):
 *   rootclass_hfid(in):
 *   client_prog_name(in):
 *   client_user_name(in):
 *   client_host_name(in):
 *   client_process_id(in):
 *   client_lock_wait(in):
 *   client_isolation(in):
 *   transtate(in):
 *   current_pagesize(in):
 *   server_clock(in):
 *   client_clock(in):
 *   server_disk_compatibility_level(in):
 *
 * NOTE:
 */
int
boot_register_client (int print_restart, const char *db_name,
		      OID * rootclass_oid,
		      HFID * rootclass_hfid, const char *client_prog_name,
		      const char *client_user_name,
		      const char *client_host_name, int client_process_id,
		      int client_lock_wait,
		      TRAN_ISOLATION client_isolation, TRAN_STATE * transtate,
		      PGLENGTH * current_pagesize,
		      struct timeval *server_clock,
		      struct timeval *client_clock,
		      float *server_disk_compatibility_level)
{
#if defined(CS_MODE)
  int tran_index = NULL_TRAN_INDEX;
  int request_size;
  int req_error, transtate_int;
  char *request, *ptr;
  OR_ALIGNED_BUF (OR_INT_SIZE + OR_OID_SIZE + OR_HFID_SIZE + OR_INT_SIZE +
		  OR_INT_SIZE + (OR_INT_SIZE * 2) + OR_FLOAT_SIZE) a_reply;
  char *reply;
  int int_pagesize;

  reply = OR_ALIGNED_BUF_START (a_reply);

  request_size = OR_INT_SIZE + length_const_string (db_name)
    + length_const_string (client_prog_name)
    + length_const_string (client_user_name)
    + length_const_string (client_host_name)
    + OR_INT_SIZE + OR_INT_SIZE + OR_INT_SIZE;
  request = (char *) malloc (request_size);
  if (request)
    {
      ptr = or_pack_int (request, print_restart);
      ptr = pack_const_string (ptr, db_name);
      ptr = pack_const_string (ptr, client_prog_name);
      ptr = pack_const_string (ptr, client_user_name);
      ptr = pack_const_string (ptr, client_host_name);
      ptr = or_pack_int (ptr, client_process_id);
      ptr = or_pack_int (ptr, client_lock_wait);
      ptr = or_pack_int (ptr, (int) client_isolation);

      req_error = net_client_request (SERVER_BO_REGISTER_CLIENT,
				      request, request_size, reply,
				      OR_ALIGNED_BUF_SIZE (a_reply),
				      NULL, 0, NULL, 0);
      gettimeofday (client_clock, NULL);

      if (!req_error)
	{
	  ptr = or_unpack_int (reply, &tran_index);
	  ptr = or_unpack_oid (ptr, rootclass_oid);
	  ptr = or_unpack_hfid (ptr, rootclass_hfid);
	  ptr = or_unpack_int (ptr, &transtate_int);
	  *transtate = (TRAN_STATE) transtate_int;
	  ptr = or_unpack_int (ptr, &int_pagesize);
	  *current_pagesize = int_pagesize;
	  ptr = or_unpack_int (ptr, (int *) &(server_clock->tv_sec));
	  ptr = or_unpack_int (ptr, (int *) &(server_clock->tv_usec));
	  ptr = or_unpack_float (ptr, server_disk_compatibility_level);
	}
      free_and_init (request);
    }

  return tran_index;
#else /* CS_MODE */
  int tran_index = NULL_TRAN_INDEX;

  ENTER_SERVER ();

  tran_index =
    xboot_register_client (NULL, print_restart, db_name, rootclass_oid,
			   rootclass_hfid, client_prog_name, client_user_name,
			   client_host_name, client_process_id,
			   client_lock_wait, client_isolation, transtate,
			   current_pagesize);
  gettimeofday (server_clock, NULL);
  *client_clock = *server_clock;
  *server_disk_compatibility_level = rel_disk_compatible ();

  EXIT_SERVER ();

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
  int success = ER_FAILED;
  int req_error;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_request;
  char *request;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply;

  request = OR_ALIGNED_BUF_START (a_request);
  reply = OR_ALIGNED_BUF_START (a_reply);

  (void) or_pack_int (request, tran_index);

  req_error = net_client_request (SERVER_BO_UNREGISTER_CLIENT,
				  request, OR_ALIGNED_BUF_SIZE (a_request),
				  reply,
				  OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0,
				  NULL, 0);
  if (!req_error)
    {
      or_unpack_int (reply, &success);
    }

  return success;
#else /* CS_MODE */
  int success = ER_FAILED;

  ENTER_SERVER ();

  success = xboot_unregister_client (NULL, tran_index);

  EXIT_SERVER ();

  return success;
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
 *   safe_pageid(in):
 *
 * NOTE:
 */
int
boot_backup (const char *backup_path, FILEIO_BACKUP_LEVEL backup_level,
	     bool delete_unneeded_logarchives,
	     const char *backup_verbose_file, int num_threads,
	     FILEIO_ZIP_METHOD zip_method, FILEIO_ZIP_LEVEL zip_level,
	     int skip_activelog, PAGEID safe_pageid)
{
#if defined(CS_MODE)
  int success = ER_FAILED;
  int request_size;
  char *request, *ptr;
  int req_error;
  OR_ALIGNED_BUF (OR_INT_SIZE * 3) a_reply;
  char *reply;
  char *rd1, *rd2;
  int d1, d2;
  int cb_type;

  reply = OR_ALIGNED_BUF_START (a_reply);

  request_size = length_const_string (backup_path) + OR_INT_SIZE
    + OR_INT_SIZE + length_const_string (backup_verbose_file)
    + OR_INT_SIZE + OR_INT_SIZE + OR_INT_SIZE + OR_INT_SIZE + OR_INT_SIZE;

  request = (char *) malloc (request_size);
  if (request)
    {
      ptr = pack_const_string (request, backup_path);
      ptr = or_pack_int (ptr, backup_level);
      ptr = or_pack_int (ptr, delete_unneeded_logarchives ? 1 : 0);
      ptr = or_pack_string (ptr, (char *) backup_verbose_file);
      ptr = or_pack_int (ptr, num_threads);
      ptr = or_pack_int (ptr, zip_method);
      ptr = or_pack_int (ptr, zip_level);
      ptr = or_pack_int (ptr, skip_activelog);
      ptr = or_pack_int (ptr, safe_pageid);
      req_error = net_client_request_with_callback (SERVER_BO_BACKUP,
						    request, request_size,
						    reply,
						    OR_ALIGNED_BUF_SIZE
						    (a_reply), NULL, 0, NULL,
						    0, &rd1, &d1, &rd2, &d2);
      if (!req_error)
	{
	  ptr = or_unpack_int (reply, &cb_type);
	  or_unpack_int (ptr, &success);
	  /* we just ignore the last part of the reply */
	}
      free_and_init (request);
    }

  return success;
#else /* CS_MODE */
  int success = false;

  ENTER_SERVER ();

  success = xboot_backup (NULL, backup_path, backup_level,
			  delete_unneeded_logarchives, backup_verbose_file,
			  num_threads, zip_method, zip_level, skip_activelog,
			  safe_pageid);

  EXIT_SERVER ();

  return success;
#endif /* !CS_MODE */
}

/*
 * boot_add_volume_extension -
 *
 * return:
 *
 *   ext_path(in):
 *   ext_name(in):
 *   ext_comments(in):
 *   ext_npages(in):
 *   ext_purpose(in):
 *   ext_overwrite(in):
 *
 * NOTE:
 */
VOLID
boot_add_volume_extension (const char *ext_path, const char *ext_name,
			   const char *ext_comments, DKNPAGES ext_npages,
			   DISK_VOLPURPOSE ext_purpose, int ext_overwrite)
{
#if defined(CS_MODE)
  int int_volid;
  VOLID volid = NULL_VOLID;
  int request_size;
  char *request, *ptr;
  int req_error;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply;

  reply = OR_ALIGNED_BUF_START (a_reply);

  request_size = length_const_string (ext_path)
    + length_const_string (ext_name) + length_const_string (ext_comments)
    + OR_INT_SIZE + OR_INT_SIZE + OR_INT_SIZE;

  request = (char *) malloc (request_size);
  if (request)
    {
      ptr = pack_const_string (request, ext_path);
      ptr = pack_const_string (ptr, ext_name);
      ptr = pack_const_string (ptr, ext_comments);
      ptr = or_pack_int (ptr, (int) ext_npages);
      ptr = or_pack_int (ptr, (int) ext_purpose);
      ptr = or_pack_int (ptr, (int) ext_overwrite);
      req_error = net_client_request (SERVER_BO_ADD_VOLEXT,
				      request, request_size, reply,
				      OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0,
				      NULL, 0);
      if (!req_error)
	{
	  or_unpack_int (reply, &int_volid);
	  volid = int_volid;
	}
      free_and_init (request);
    }

  return volid;
#else /* CS_MODE */
  VOLID volid;

  ENTER_SERVER ();

  volid =
    xboot_add_volume_extension (NULL, ext_path, ext_name, ext_comments,
				ext_npages, ext_purpose, ext_overwrite);

  EXIT_SERVER ();

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
boot_check_db_consistency (int check_flag)
{
#if defined(CS_MODE)
  int success = ER_FAILED;
  int req_error;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_request;
  char *request;
  int request_size;

  reply = OR_ALIGNED_BUF_START (a_reply);
  request = OR_ALIGNED_BUF_START (a_request);
  request_size = OR_INT_SIZE;

  or_pack_int (request, check_flag);
  req_error = net_client_request (SERVER_BO_CHECK_DBCONSISTENCY,
				  request, request_size, reply,
				  OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0,
				  NULL, 0);
  if (!req_error)
    {
      or_unpack_int (reply, &success);
    }

  return success;
#else /* CS_MODE */
  int success = ER_FAILED;

  ENTER_SERVER ();

  success = xboot_check_db_consistency (NULL, check_flag);

  EXIT_SERVER ();

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

  req_error = net_client_request (SERVER_BO_FIND_NPERM_VOLS,
				  NULL, 0, reply,
				  OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0,
				  NULL, 0);
  if (!req_error)
    {
      or_unpack_int (reply, &nvols);
    }

  return nvols;
#else /* CS_MODE */
  int nvols = -1;

  ENTER_SERVER ();

  nvols = xboot_find_number_permanent_volumes (NULL);

  EXIT_SERVER ();

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

  req_error = net_client_request (SERVER_BO_FIND_NTEMP_VOLS,
				  NULL, 0, reply,
				  OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0,
				  NULL, 0);
  if (!req_error)
    {
      or_unpack_int (reply, &nvols);
    }

  return nvols;
#else /* CS_MODE */
  int nvols = -1;

  ENTER_SERVER ();

  nvols = xboot_find_number_temp_volumes (NULL);

  EXIT_SERVER ();

  return nvols;
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

  req_error = net_client_request (SERVER_BO_FIND_LAST_TEMP,
				  NULL, 0, reply,
				  OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0,
				  NULL, 0);
  if (!req_error)
    {
      or_unpack_int (reply, &nvols);
    }

  return nvols;
#else /* CS_MODE */
  int nvols = -1;

  ENTER_SERVER ();

  nvols = xboot_find_last_temp (NULL);

  EXIT_SERVER ();

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
  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
	  ER_ONLY_IN_STANDALONE, 1, "delete database");
  return ER_FAILED;
#else /* CS_MODE */
  int error_code;

  ENTER_SERVER ();

  error_code = xboot_delete (NULL, db_name, force_delete);

  EXIT_SERVER ();

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
boot_restart_from_backup (int print_restart, const char *db_name,
			  BO_RESTART_ARG * r_args)
{
#if defined(CS_MODE)
  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
	  ER_ONLY_IN_STANDALONE, 1, "restart from backup");
  return NULL_TRAN_INDEX;
#else /* CS_MODE */
  int tran_index;

  ENTER_SERVER ();

  tran_index = xboot_restart_from_backup (NULL, print_restart, db_name,
					  r_args);

  EXIT_SERVER ();

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
boot_shutdown_server (bool iserfinal)
{
#if defined(CS_MODE)
  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ONLY_IN_STANDALONE, 1, "");
  return false;
#else /* CS_MODE */
  bool result;

  ENTER_SERVER ();

  result = xboot_shutdown_server (NULL, iserfinal);

  EXIT_SERVER ();

  return result;
#endif /* !CS_MODE */
}

/*
 * boot_soft_rename -
 *
 * return:
 *
 * NOTE:
 */
int
boot_soft_rename (const char *olddb_name,
		  const char *newdb_name, const char *newdb_path,
		  const char *newlog_path, const char *newdb_server_host,
		  const char *new_volext_path,
		  const char *fileof_vols_and_renamepaths,
		  bool newdb_overwrite, bool extern_rename, bool force_delete)
{
#if defined(CS_MODE)
  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
	  ER_ONLY_IN_STANDALONE, 1, "install database");
  return ER_FAILED;
#else /* CS_MODE */
  int error_code;

  ENTER_SERVER ();

  error_code =
    xboot_soft_rename (NULL, olddb_name, newdb_name, newdb_path, newlog_path,
		       newdb_server_host, new_volext_path,
		       fileof_vols_and_renamepaths, newdb_overwrite,
		       extern_rename, force_delete);

  EXIT_SERVER ();

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
boot_copy (const char *from_dbname, const char *newdb_name,
	   const char *newdb_path, const char *newlog_path,
	   const char *newdb_server_host, const char *new_volext_path,
	   const char *fileof_vols_and_copypaths, bool newdb_overwrite)
{
#if defined(CS_MODE)
  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
	  ER_ONLY_IN_STANDALONE, 1, "copy database");
  return ER_FAILED;
#else /* CS_MODE */
  int error_code;

  ENTER_SERVER ();

  error_code = xboot_copy (NULL, from_dbname, newdb_name, newdb_path,
			   newlog_path, newdb_server_host, new_volext_path,
			   fileof_vols_and_copypaths, newdb_overwrite);

  EXIT_SERVER ();

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
boot_emergency_patch (const char *db_name, bool recreate_log)
{
#if defined(CS_MODE)
  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
	  ER_ONLY_IN_STANDALONE, 1, "emergency patch");
  return ER_FAILED;
#else /* CS_MODE */
  int error_code;

  ENTER_SERVER ();

  error_code = xboot_emergency_patch (NULL, db_name, recreate_log);

  EXIT_SERVER ();

  return error_code;
#endif /* !CS_MODE */
}

/*
 * largeobjmgr_create () -
 *
 * return:
 *
 *   loid(in):
 *   length(in):
 *   buffer(in):
 *   est_lo_length(in):
 *   oid(in):
 *
 * NOTE:
 */
LOID *
largeobjmgr_create (LOID * loid, int length, char *buffer,
		    int est_lo_length, OID * oid)
{
#if defined(CS_MODE)
  int req_error;
  char *ptr;
  OR_ALIGNED_BUF (OR_LOID_SIZE + OR_INT_SIZE + OR_INT_SIZE
		  + OR_OID_SIZE) a_request;
  char *request;
  OR_ALIGNED_BUF (OR_LOID_SIZE) a_reply;
  char *reply;

  request = OR_ALIGNED_BUF_START (a_request);
  reply = OR_ALIGNED_BUF_START (a_reply);

  ptr = or_pack_loid (request, loid);
  ptr = or_pack_int (ptr, length);
  ptr = or_pack_int (ptr, est_lo_length);
  ptr = or_pack_oid (ptr, oid);

  req_error = net_client_request (SERVER_LARGEOBJMGR_CREATE,
				  request, OR_ALIGNED_BUF_SIZE (a_request),
				  reply, OR_ALIGNED_BUF_SIZE (a_reply),
				  buffer, length, NULL, 0);
  if (!req_error)
    {
      ptr = or_unpack_loid (reply, loid);
    }
  else
    {
      loid = NULL;
    }

  return loid;
#else /* CS_MODE */

  ENTER_SERVER ();

  loid = xlargeobjmgr_create (NULL, loid, length, buffer, est_lo_length, oid);

  EXIT_SERVER ();

  return loid;
#endif /* !CS_MODE */
}

/*
 * largeobjmgr_destroy () -
 *
 * return:
 *
 *   loid(in):
 *
 * NOTE:
 */
int
largeobjmgr_destroy (LOID * loid)
{
#if defined(CS_MODE)
  int status = ER_FAILED;
  int req_error;
  OR_ALIGNED_BUF (OR_LOID_SIZE) a_request;
  char *request;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply;

  request = OR_ALIGNED_BUF_START (a_request);
  reply = OR_ALIGNED_BUF_START (a_reply);

  or_pack_loid (request, loid);
  req_error = net_client_request (SERVER_LARGEOBJMGR_DESTROY,
				  request, OR_ALIGNED_BUF_SIZE (a_request),
				  reply,
				  OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0,
				  NULL, 0);
  if (!req_error)
    {
      or_unpack_int (reply, &status);
    }

  return ((int) status);
#else /* CS_MODE */
  int success = ER_FAILED;

  ENTER_SERVER ();

  success = xlargeobjmgr_destroy (NULL, loid);

  EXIT_SERVER ();

  return success;
#endif /* !CS_MODE */
}

/*
 * largeobjmgr_read () -
 *
 * return:
 *
 *   loid(in):
 *   offset(in):
 *   length(in):
 *   buffer(in):
 *
 * NOTE:
 */
/* TODO: offset should be off_t rather than int?? */
int
largeobjmgr_read (LOID * loid, int offset, int length, char *buffer)
{
#if defined(CS_MODE)
  int processed_length = -1;
  int req_error;
  char *ptr;
  OR_ALIGNED_BUF (OR_LOID_SIZE + (OR_INT_SIZE * 2)) a_request;
  char *request;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply;

  request = OR_ALIGNED_BUF_START (a_request);
  reply = OR_ALIGNED_BUF_START (a_reply);

  ptr = or_pack_loid (request, loid);
  ptr = or_pack_int (ptr, offset);
  ptr = or_pack_int (ptr, length);

  req_error = net_client_request2_no_malloc (SERVER_LARGEOBJMGR_READ,
					     request,
					     OR_ALIGNED_BUF_SIZE (a_request),
					     reply,
					     OR_ALIGNED_BUF_SIZE (a_reply),
					     NULL, 0, buffer, &length);
  if (!req_error)
    {
      ptr = or_unpack_int (reply, &processed_length);
    }

  return processed_length;
#else /* CS_MODE */
  int processed_length = -1;

  ENTER_SERVER ();

  processed_length = xlargeobjmgr_read (NULL, loid, offset, length, buffer);

  EXIT_SERVER ();

  return processed_length;
#endif /* !CS_MODE */
}

/*
 * largeobjmgr_write () -
 *
 * return:
 *
 *   loid(in):
 *   offset(in):
 *   length(in):
 *   buffer(in):
 *
 * NOTE:
 */
/* TODO: offset should be off_t rather than int?? */
int
largeobjmgr_write (LOID * loid, int offset, int length, char *buffer)
{
#if defined(CS_MODE)
  int processed_length = -1;
  int req_error;
  char *ptr;
  OR_ALIGNED_BUF (OR_LOID_SIZE + OR_INT_SIZE + OR_INT_SIZE) a_request;
  char *request;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply;

  request = OR_ALIGNED_BUF_START (a_request);
  reply = OR_ALIGNED_BUF_START (a_reply);

  ptr = or_pack_int (request, length);
  ptr = or_pack_loid (ptr, loid);
  ptr = or_pack_int (ptr, offset);

  req_error = net_client_request (SERVER_LARGEOBJMGR_WRITE,
				  request, OR_ALIGNED_BUF_SIZE (a_request),
				  reply,
				  OR_ALIGNED_BUF_SIZE (a_reply), buffer,
				  length, NULL, 0);
  if (!req_error)
    {
      ptr = or_unpack_int (reply, &processed_length);
    }

  return processed_length;
#else /* CS_MODE */
  int processed_length = -1;

  ENTER_SERVER ();

  processed_length = xlargeobjmgr_write (NULL, loid, offset, length, buffer);

  EXIT_SERVER ();

  return processed_length;
#endif /* !CS_MODE */
}

/*
 * largeobjmgr_insert () -
 *
 * return:
 *
 *   loid(in):
 *   offset(in):
 *   length(in):
 *   buffer(in):
 *
 * NOTE:
 */
/* TODO: offset should be off_t rather than int?? */
int
largeobjmgr_insert (LOID * loid, int offset, int length, char *buffer)
{
#if defined(CS_MODE)
  int processed_length = -1;
  int req_error;
  char *ptr;
  OR_ALIGNED_BUF (OR_LOID_SIZE + OR_INT_SIZE + OR_INT_SIZE) a_request;
  char *request;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply;

  request = OR_ALIGNED_BUF_START (a_request);
  reply = OR_ALIGNED_BUF_START (a_reply);

  ptr = or_pack_int (request, length);
  ptr = or_pack_loid (ptr, loid);
  ptr = or_pack_int (ptr, offset);

  req_error = net_client_request (SERVER_LARGEOBJMGR_INSERT,
				  request, OR_ALIGNED_BUF_SIZE (a_request),
				  reply,
				  OR_ALIGNED_BUF_SIZE (a_reply), buffer,
				  length, NULL, 0);
  if (!req_error)
    {
      ptr = or_unpack_int (reply, &processed_length);
    }

  return processed_length;
#else /* CS_MODE */
  int processed_length = -1;

  ENTER_SERVER ();

  processed_length = xlargeobjmgr_insert (NULL, loid, offset, length, buffer);

  EXIT_SERVER ();

  return processed_length;
#endif /* !CS_MODE */
}

/*
 * largeobjmgr_delete () -
 *
 * return:
 *
 *   loid(in):
 *   offset(in):
 *   length(in):
 *
 * NOTE:
 */
/* TODO: offset should be off_t rather than int?? */
int
largeobjmgr_delete (LOID * loid, int offset, int length)
{
#if defined(CS_MODE)
  int processed_length = -1;
  int req_error;
  char *ptr;
  OR_ALIGNED_BUF (OR_LOID_SIZE + OR_INT_SIZE + OR_INT_SIZE) a_request;
  char *request;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply;

  request = OR_ALIGNED_BUF_START (a_request);
  reply = OR_ALIGNED_BUF_START (a_reply);

  ptr = or_pack_loid (request, loid);
  ptr = or_pack_int (ptr, offset);
  ptr = or_pack_int (ptr, length);

  req_error = net_client_request (SERVER_LARGEOBJMGR_DELETE,
				  request, OR_ALIGNED_BUF_SIZE (a_request),
				  reply, OR_ALIGNED_BUF_SIZE (a_reply), NULL,
				  0, NULL, 0);
  if (!req_error)
    {
      ptr = or_unpack_int (reply, &processed_length);
    }

  return processed_length;
#else /* CS_MODE */
  int processed_length = -1;

  ENTER_SERVER ();

  processed_length = xlargeobjmgr_delete (NULL, loid, offset, length);

  EXIT_SERVER ();

  return processed_length;
#endif /* !CS_MODE */
}

/*
 * largeobjmgr_append () -
 *
 * return:
 *
 *   loid(in):
 *   length(in):
 *   buffer(in):
 *
 * NOTE:
 */
int
largeobjmgr_append (LOID * loid, int length, char *buffer)
{
#if defined(CS_MODE)
  int processed_length = -1;
  int req_error;
  char *ptr;
  OR_ALIGNED_BUF (OR_LOID_SIZE + OR_INT_SIZE) a_request;
  char *request;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply;

  request = OR_ALIGNED_BUF_START (a_request);
  reply = OR_ALIGNED_BUF_START (a_reply);

  ptr = or_pack_int (request, length);
  ptr = or_pack_loid (ptr, loid);

  req_error = net_client_request (SERVER_LARGEOBJMGR_APPEND,
				  request, OR_ALIGNED_BUF_SIZE (a_request),
				  reply,
				  OR_ALIGNED_BUF_SIZE (a_reply), buffer,
				  length, NULL, 0);
  if (!req_error)
    {
      ptr = or_unpack_int (reply, &processed_length);
    }

  return processed_length;
#else /* CS_MODE */
  int processed_length = -1;

  ENTER_SERVER ();

  processed_length = xlargeobjmgr_append (NULL, loid, length, buffer);

  EXIT_SERVER ();

  return processed_length;
#endif /* !CS_MODE */
}

/*
 * largeobjmgr_truncate () -
 *
 * return:
 *
 *   loid(in):
 *   offset(in):
 *
 * NOTE:
 */
/* TODO: offset should be off_t rather than int?? */
int
largeobjmgr_truncate (LOID * loid, int offset)
{
#if defined(CS_MODE)
  int processed_length = -1;
  int req_error;
  char *ptr;
  OR_ALIGNED_BUF (OR_LOID_SIZE + OR_INT_SIZE) a_request;
  char *request;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply;

  request = OR_ALIGNED_BUF_START (a_request);
  reply = OR_ALIGNED_BUF_START (a_reply);

  ptr = or_pack_loid (request, loid);
  ptr = or_pack_int (ptr, offset);

  req_error = net_client_request (SERVER_LARGEOBJMGR_TRUNCATE,
				  request, OR_ALIGNED_BUF_SIZE (a_request),
				  reply, OR_ALIGNED_BUF_SIZE (a_reply), NULL,
				  0, NULL, 0);
  if (!req_error)
    {
      ptr = or_unpack_int (reply, &processed_length);
    }

  return processed_length;
#else /* CS_MODE */
  int processed_length = -1;

  ENTER_SERVER ();

  processed_length = xlargeobjmgr_truncate (NULL, loid, offset);

  EXIT_SERVER ();

  return processed_length;
#endif /* !CS_MODE */
}

/*
 * largeobjmgr_compress () -
 *
 * return:
 *
 *   loid(in):
 *
 * NOTE:
 */
int
largeobjmgr_compress (LOID * loid)
{
#if defined(CS_MODE)
  int status = ER_FAILED;
  int req_error;
  OR_ALIGNED_BUF (OR_LOID_SIZE) a_request;
  char *request;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply;

  request = OR_ALIGNED_BUF_START (a_request);
  reply = OR_ALIGNED_BUF_START (a_reply);

  or_pack_loid (request, loid);

  req_error = net_client_request (SERVER_LARGEOBJMGR_COMPRESS,
				  request, OR_ALIGNED_BUF_SIZE (a_request),
				  reply,
				  OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0,
				  NULL, 0);
  if (!req_error)
    {
      or_unpack_int (reply, &status);
    }

  return status;
#else /* CS_MODE */
  int success = ER_FAILED;

  ENTER_SERVER ();

  success = xlargeobjmgr_compress (NULL, loid);

  EXIT_SERVER ();

  return success;
#endif /* !CS_MODE */
}

/*
 * largeobjmgr_length () -
 *
 * return:
 *
 *   loid(in):
 *
 * NOTE:
 */
int
largeobjmgr_length (LOID * loid)
{
#if defined(CS_MODE)
  int length = -1;
  int req_error;
  char *ptr;
  OR_ALIGNED_BUF (OR_LOID_SIZE) a_request;
  char *request;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply;

  request = OR_ALIGNED_BUF_START (a_request);
  reply = OR_ALIGNED_BUF_START (a_reply);

  or_pack_loid (request, loid);

  req_error = net_client_request (SERVER_LARGEOBJMGR_LENGTH,
				  request, OR_ALIGNED_BUF_SIZE (a_request),
				  reply,
				  OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0,
				  NULL, 0);
  if (!req_error)
    {
      ptr = or_unpack_int (reply, &length);
    }

  return length;
#else /* CS_MODE */
  int length = -1;

  ENTER_SERVER ();

  length = xlargeobjmgr_length (NULL, loid);

  EXIT_SERVER ();

  return length;
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
stats_get_statistics_from_server (OID * classoid, unsigned int timestamp,
				  int *length_ptr)
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

  req_error = net_client_request2 (SERVER_QST_SERVER_GET_STATISTICS,
				   request, OR_ALIGNED_BUF_SIZE (a_request),
				   reply, OR_ALIGNED_BUF_SIZE (a_reply),
				   NULL, 0, &area, length_ptr);
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

  ENTER_SERVER ();

  area =
    xstats_get_statistics_from_server (NULL, classoid, timestamp, length_ptr);

  EXIT_SERVER ();

  return area;
#endif /* !CS_MODE */
}

/*
 * stats_update_class_statistics -
 *
 * return:
 *
 *   classoid(in):
 *
 * NOTE:
 */
int
stats_update_class_statistics (OID * classoid)
{
#if defined(CS_MODE)
  int error = ER_NET_CLIENT_DATA_RECEIVE;
  int req_error;
  OR_ALIGNED_BUF (OR_OID_SIZE) a_request;
  char *request;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply;

  request = OR_ALIGNED_BUF_START (a_request);
  reply = OR_ALIGNED_BUF_START (a_reply);

  or_pack_oid (request, classoid);

  req_error = net_client_request (SERVER_QST_UPDATE_CLASS_STATISTICS,
				  request, OR_ALIGNED_BUF_SIZE (a_request),
				  reply,
				  OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0,
				  NULL, 0);
  if (!req_error)
    {
      or_unpack_errcode (reply, &error);
    }

  return error;
#else /* CS_MODE */
  int success;

  ENTER_SERVER ();

  success = xstats_update_class_statistics (NULL, classoid);

  EXIT_SERVER ();

  return success;
#endif /* !CS_MODE */
}

/*
 * stats_update_statistics -
 *
 * return:
 *
 * NOTE:
 */
int
stats_update_statistics (void)
{
#if defined(CS_MODE)
  int error = ER_NET_CLIENT_DATA_RECEIVE;
  int req_error;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply;

  reply = OR_ALIGNED_BUF_START (a_reply);

  req_error = net_client_request (SERVER_QST_UPDATE_STATISTICS,
				  NULL, 0, reply,
				  OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0,
				  NULL, 0);
  if (!req_error)
    {
      or_unpack_errcode (reply, &error);
    }

  return error;
#else /* CS_MODE */
  int success;

  ENTER_SERVER ();

  success = xstats_update_statistics (NULL);

  EXIT_SERVER ();

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
 *   unique_btree(in):
 *   reverse_btree(in):
 *
 * NOTE:
 */
int
btree_add_index (BTID * btid, TP_DOMAIN * key_type, OID * class_oid,
		 int attr_id, int unique_btree, int reverse_btree)
{
#if defined(CS_MODE)
  int error;
  int req_error, request_size, domain_size;
  char *ptr;
  char *request;
  OR_ALIGNED_BUF (OR_INT_SIZE + OR_BTID_SIZE) a_reply;
  char *reply;

  reply = OR_ALIGNED_BUF_START (a_reply);

  domain_size = or_packed_domain_size (key_type, 0);
  request_size = or_align_length_for_btree (OR_BTID_SIZE) + domain_size
    + OR_OID_SIZE + OR_INT_SIZE + OR_INT_SIZE + OR_INT_SIZE;

  request = (char *) malloc (request_size);
  if (request == NULL)
    {
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  ptr = or_pack_btid (request, btid);
  ptr = PTR_ALIGN (ptr, OR_INT_SIZE);
  ptr = or_pack_domain (ptr, key_type, 0, 0);
  ptr = or_pack_oid (ptr, class_oid);
  ptr = or_pack_int (ptr, attr_id);
  ptr = or_pack_int (ptr, unique_btree);
  ptr = or_pack_int (ptr, reverse_btree);

  req_error = net_client_request (SERVER_BTREE_ADDINDEX,
				  request, request_size, reply,
				  OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0,
				  NULL, 0);
  if (!req_error)
    {
      ptr = or_unpack_int (reply, &error);
      ptr = or_unpack_btid (ptr, btid);
      if (error)
	btid = NULL;
    }

  free_and_init (request);
  return error;
#else /* CS_MODE */
  int error = NO_ERROR;

  ENTER_SERVER ();

  btid =
    xbtree_add_index (NULL, btid, key_type, class_oid, attr_id, unique_btree,
		      reverse_btree, 0, 0, 0);
  if (btid == NULL)
    {
      error = er_errid ();
    }

  EXIT_SERVER ();

  return error;
#endif /* !CS_MODE */
}

/*
 * btree_load_index -
 *
 * return:
 *
 *   btid(in):
 *   key_type(in):
 *   class_oids(in):
 *   n_classes(in):
 *   n_attrs(in):
 *   attr_ids(in):
 *   hfids(in):
 *   unique_flag(in):
 *   reverse_flag(in):
 *   fk_refcls_oid(in):
 *   fk_refcls_pk_btid(in):
 *   cache_attr_id(in):
 *   fk_name(in):
 *
 * NOTE:
 */
int
btree_load_index (BTID * btid, TP_DOMAIN * key_type, OID * class_oids,
		  int n_classes, int n_attrs,
		  int *attr_ids, HFID * hfids,
		  int unique_flag, int reverse_flag,
		  OID * fk_refcls_oid, BTID * fk_refcls_pk_btid,
		  int cache_attr_id, const char *fk_name)
{
#if defined(CS_MODE)
  int error, req_error, request_size, domain_size;
  char *ptr;
  char *request;
  OR_ALIGNED_BUF (OR_INT_SIZE + OR_BTID_SIZE) a_reply;
  char *reply;
  int i, total_attrs;

  reply = OR_ALIGNED_BUF_START (a_reply);

  domain_size = or_packed_domain_size (key_type, 0);
  request_size = or_align_length_for_btree (OR_BTID_SIZE)
    + domain_size + (n_classes * OR_OID_SIZE) + OR_INT_SIZE + OR_INT_SIZE
    + (n_classes * n_attrs * OR_INT_SIZE) + (n_classes * OR_HFID_SIZE)
    + OR_INT_SIZE + OR_INT_SIZE + OR_OID_SIZE
    + or_align_length_for_btree (OR_BTID_SIZE) + OR_INT_SIZE
    + or_packed_string_length (fk_name);

  request = (char *) malloc (request_size);

  if (request == NULL)
    {
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  ptr = or_pack_btid (request, btid);
  ptr = PTR_ALIGN (ptr, OR_INT_SIZE);
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

  for (i = 0; i < n_classes; i++)
    {
      ptr = or_pack_hfid (ptr, &hfids[i]);
    }

  ptr = or_pack_int (ptr, unique_flag);
  ptr = or_pack_int (ptr, reverse_flag);

  ptr = or_pack_oid (ptr, fk_refcls_oid);
  ptr = or_pack_btid (ptr, fk_refcls_pk_btid);
  ptr = PTR_ALIGN (ptr, OR_INT_SIZE);
  ptr = or_pack_int (ptr, cache_attr_id);
  ptr = or_pack_string (ptr, (char *) fk_name);

  req_error = net_client_request (SERVER_BTREE_LOADINDEX,
				  request, request_size, reply,
				  OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0,
				  NULL, 0);
  if (!req_error)
    {
      ptr = or_unpack_int (reply, &error);
      ptr = or_unpack_btid (ptr, btid);
      if (error)
	btid = NULL;
    }
  else
    {
      btid = NULL;
    }

  free_and_init (request);
  return error;
#else /* CS_MODE */
  int error = NO_ERROR;

  ENTER_SERVER ();

  btid =
    xbtree_load_index (NULL, btid, key_type, class_oids, n_classes, n_attrs,
		       attr_ids, hfids, unique_flag, reverse_flag,
		       fk_refcls_oid, fk_refcls_pk_btid, cache_attr_id,
		       fk_name);
  if (btid == NULL)
    {
      error = er_errid ();
    }
  else
    {
      error = NO_ERROR;
    }

  EXIT_SERVER ();

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
  int req_error, status;
  OR_ALIGNED_BUF (OR_BTID_SIZE) a_request;
  char *request;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply;

  request = OR_ALIGNED_BUF_START (a_request);
  reply = OR_ALIGNED_BUF_START (a_reply);

  (void) or_pack_btid (request, btid);

  req_error = net_client_request (SERVER_BTREE_DELINDEX,
				  request, OR_ALIGNED_BUF_SIZE (a_request),
				  reply, OR_ALIGNED_BUF_SIZE (a_reply), NULL,
				  0, NULL, 0);
  if (!req_error)
    {
      or_unpack_int (reply, &status);
    }

  return ((int) status);

#else /* CS_MODE */
  int success = ER_FAILED;

  ENTER_SERVER ();

  success = xbtree_delete_index (NULL, btid);

  EXIT_SERVER ();

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
  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
	  ER_ONLY_IN_STANDALONE, 1, "no logging");
  return ER_FAILED;
#else /* CS_MODE */
  int success = ER_FAILED;

  ENTER_SERVER ();

  success = log_set_no_logging ();

  EXIT_SERVER ();

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
  int req_error, status, request_size;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;

  reply = OR_ALIGNED_BUF_START (a_reply);
  request_size = OR_OID_SIZE + or_align_length_for_btree (OR_BTID_SIZE)
    + OR_HFID_SIZE;

  request = (char *) malloc (request_size);
  if (request == NULL)
    {
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  ptr = or_pack_oid (request, oid);
  ptr = or_pack_btid (ptr, btid);
  ptr = PTR_ALIGN (ptr, OR_INT_SIZE);
  ptr = or_pack_hfid (ptr, hfid);

  req_error = net_client_request (SERVER_LC_REM_CLASS_FROM_INDEX,
				  request, request_size, reply,
				  OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0,
				  NULL, 0);
  if (!req_error)
    {
      or_unpack_int (reply, &status);
    }

  free_and_init (request);
  return ((int) status);

#else /* CS_MODE */
  int success = ER_FAILED;

  ENTER_SERVER ();

  success = xlocator_remove_class_from_index (NULL, oid, btid, hfid);

  EXIT_SERVER ();

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
  BTREE_SEARCH status = BTREE_ERROR_OCCURRED;

  if (btid == NULL || key == NULL || class_oid == NULL || oid == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OBJ_INVALID_ARGUMENTS, 0);
    }
  else
    {
#if defined(CS_MODE)
      int req_error, request_size, key_size;
      char *ptr;
      char *request;
      OR_ALIGNED_BUF (OR_INT_SIZE + OR_OID_SIZE) a_reply;
      char *reply;

      reply = OR_ALIGNED_BUF_START (a_reply);

      key_size = or_packed_value_size (key, 0, 1, 1);
      request_size = or_align_length_for_btree (OR_BTID_SIZE)
	+ key_size + OR_OID_SIZE;

      request = (char *) malloc (request_size);
      if (request == NULL)
	{
	  return status;
	}

      ptr = or_pack_btid (request, btid);
      ptr = PTR_ALIGN (ptr, OR_INT_SIZE);
      ptr = or_pack_value (ptr, key);
      ptr = or_pack_oid (ptr, class_oid);

      req_error = net_client_request (SERVER_BTREE_FIND_UNIQUE,
				      request, request_size, reply,
				      OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0,
				      NULL, 0);
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
#else /* CS_MODE */

      ENTER_SERVER ();
      status = xbtree_find_unique (NULL, btid, key, class_oid, oid, false);
      EXIT_SERVER ();

#endif /* !CS_MODE */
    }

  return status;
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
  int req_error, status;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply;

  reply = OR_ALIGNED_BUF_START (a_reply);

  req_error = net_client_request (SERVER_BTREE_CLASS_UNIQUE_TEST,
				  buf, buf_size, reply,
				  OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0,
				  NULL, 0);
  if (!req_error)
    {
      or_unpack_int (reply, &status);
    }

  return ((int) status);
#else /* CS_MODE */
  int success = ER_FAILED;

  ENTER_SERVER ();

  success = xbtree_class_test_unique (NULL, buf, buf_size);

  EXIT_SERVER ();

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
qfile_get_list_file_page (int query_id, VOLID volid, PAGEID pageid,
			  char *buffer, int *buffer_size)
{
#if defined(CS_MODE)
  int error = ER_NET_CLIENT_DATA_RECEIVE;
  int req_error;
  char *ptr;
  OR_ALIGNED_BUF (OR_INT_SIZE + OR_INT_SIZE + OR_INT_SIZE) a_request;
  char *request;
  OR_ALIGNED_BUF (OR_INT_SIZE * 2) a_reply;
  char *reply;

  request = OR_ALIGNED_BUF_START (a_request);
  reply = OR_ALIGNED_BUF_START (a_reply);

  ptr = or_pack_int (request, (int) query_id);
  ptr = or_pack_int (ptr, (int) volid);
  ptr = or_pack_int (ptr, (int) pageid);

  req_error = net_client_request2_no_malloc (SERVER_LS_GET_LIST_FILE_PAGE,
					     request,
					     OR_ALIGNED_BUF_SIZE (a_request),
					     reply,
					     OR_ALIGNED_BUF_SIZE (a_reply),
					     NULL, 0, buffer, buffer_size);
  if (!req_error)
    {
      ptr = or_unpack_int (&reply[OR_INT_SIZE], &error);
    }

  return error;
#else /* CS_MODE */
  int success;
  int page_size;

  ENTER_SERVER ();

  success = xqfile_get_list_file_page (NULL, query_id, volid, pageid,
				       buffer, &page_size);

  EXIT_SERVER ();

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
 */
/*
 * qmgr_prepare_query -
 *
 * return:
 *
 *   query_str(in):
 *   user_oid(in):
 *   xasl_buffer(in):
 *   size(in):
 *   xasl_id(in):
 *
 * NOTE:
 */
XASL_ID *
qmgr_prepare_query (const char *query_str, const OID * user_oid,
		    const char *xasl_buffer, int size, XASL_ID * xasl_id)
{
#if defined(CS_MODE)
  int success = NO_ERROR;
  LOID loid;
  int req_error, request_size;
  char *request, *reply, *ptr;
  OR_ALIGNED_BUF (OR_INT_SIZE + OR_LOID_SIZE) a_reply;

  reply = OR_ALIGNED_BUF_START (a_reply);

  request_size = length_const_string (query_str) + OR_OID_SIZE + OR_INT_SIZE;
  request = (char *) malloc (request_size);
  if (!request)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (request_size));
      return NULL;
    }

  /* pack query string as a request data */
  ptr = pack_const_string (request, query_str);
  /* pack OID of the current user */
  ptr = or_pack_oid (ptr, (OID *) user_oid);
  /* pack size of XASL stream */
  ptr = or_pack_int (ptr, size);

  /* send SERVER_QM_QUERY_PREPARE request with request data and XASL stream;
     receive XASL file id (XASL_ID) as a reply */
  req_error = net_client_request (SERVER_QM_QUERY_PREPARE,
				  request, request_size,
				  reply, OR_ALIGNED_BUF_SIZE (a_reply),
				  (char *) xasl_buffer, size, NULL, 0);
  if (!req_error)
    {
      /* first argument should be XASL_ID
         borrow LOID structure only for transmission purpose */
      ptr = or_unpack_int (reply, &success);
      if (success == NO_ERROR)
	{
	  ptr = or_unpack_loid (ptr, &loid);
	  /* NULL XASL_ID will be returned when cache not found */
	  (void) memcpy ((void *) xasl_id, (void *) &loid, sizeof (XASL_ID));
	}
      else
	{
	  xasl_id = NULL;
	}
    }
  else
    {
      xasl_id = NULL;
    }

  if (request)
    {
      free_and_init (request);
    }

  return xasl_id;

#else /* CS_MODE */
  XASL_ID *p;

  ENTER_SERVER ();

  /* call the server routine of query prepare */
  p = xqmgr_prepare_query (NULL, query_str, user_oid, xasl_buffer, size,
			   xasl_id);
  if (p)
    {
      *xasl_id = *p;
    }
  else
    {
      xasl_id = NULL;
    }

  EXIT_SERVER ();

  return xasl_id;
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
 *
 * NOTE:
 */
QFILE_LIST_ID *
qmgr_execute_query (const XASL_ID * xasl_id, int *query_idp,
		    int dbval_cnt, const DB_VALUE * dbvals,
		    QUERY_FLAG flag, CACHE_TIME * clt_cache_time,
		    CACHE_TIME * srv_cache_time)
{
#if defined(CS_MODE)
  QFILE_LIST_ID *list_id = NULL;
  int req_error, senddata_size, replydata_size1, replydata_size2;
  char *request, *reply, *senddata = NULL, *replydata1 = NULL,
    *replydata2 = NULL, *ptr;
  OR_ALIGNED_BUF (OR_LOID_SIZE + OR_INT_SIZE * 3 +
		  OR_CACHE_TIME_SIZE) a_request;
  OR_ALIGNED_BUF (OR_INT_SIZE * 4 + OR_CACHE_TIME_SIZE) a_reply;
  int i;
  const DB_VALUE *dbval;

  request = OR_ALIGNED_BUF_START (a_request);
  reply = OR_ALIGNED_BUF_START (a_reply);

  /* make send data using if parameter values for host variables are given */
  senddata_size = 0;
  for (i = 0, dbval = dbvals; i < dbval_cnt; i++, dbval++)
    {
      senddata_size += or_db_value_size ((DB_VALUE *) dbval);
    }
  if (senddata_size)
    {
      senddata = (char *) malloc (senddata_size);
      if (senddata == NULL)
	{
	  return NULL;
	}

      ptr = senddata;
      for (i = 0, dbval = dbvals; i < dbval_cnt; i++, dbval++)
	{
	  ptr = or_pack_db_value (ptr, (DB_VALUE *) dbval);
	}
    }

  /* pack XASL file id (XASL_ID), number of parameter values,
     size of the send data, and query execution mode flag as a request data;
     borrow LOID structure only for transmission purpose */
  ptr = or_pack_loid (request, (LOID *) xasl_id);
  ptr = or_pack_int (ptr, dbval_cnt);
  ptr = or_pack_int (ptr, senddata_size);
  ptr = or_pack_int (ptr, flag);
  OR_PACK_CACHE_TIME (ptr, clt_cache_time);

  req_error = net_client_request_with_callback (SERVER_QM_QUERY_EXECUTE,
						request,
						OR_ALIGNED_BUF_SIZE
						(a_request), reply,
						OR_ALIGNED_BUF_SIZE (a_reply),
						senddata, senddata_size, NULL,
						0, &replydata1,
						&replydata_size1, &replydata2,
						&replydata_size2);
  if (senddata)
    {
      free_and_init (senddata);
    }

  if (!req_error)
    {
      /* first argument should be QUERY_END
         ptr = or_unpack_int(reply, &status); */
      /* second argument should be the same with replydata_size1
         ptr = or_unpack_int(ptr, &listid_length); */
      /* third argument should be the same with replydata_size2
         ptr = or_unpack_int(ptr, &page_size); */
      /* fourth argument should be query_id */
      ptr = or_unpack_int (reply + OR_INT_SIZE * 3, query_idp);
      OR_UNPACK_CACHE_TIME (ptr, srv_cache_time);

      if (replydata1 && replydata_size1)
	{
	  /* unpack list file id of query result from the reply data */
	  ptr = or_unpack_unbound_listid (replydata1, (void **) &list_id);
	  /* QFILE_LIST_ID shipped with last page */
	  if (replydata_size2)
	    {
	      list_id->last_pgptr = replydata2;
	    }
	  else
	    {
	      list_id->last_pgptr = NULL;
	    }
	  free_and_init (replydata1);
	}
    }

  return list_id;
#else /* CS_MODE */
  QFILE_LIST_ID *list_id = NULL;

  ENTER_SERVER ();

  /* call the server routine of query execute */
  list_id = xqmgr_execute_query (NULL, xasl_id, query_idp, dbval_cnt,
				 dbvals, &flag, clt_cache_time,
				 srv_cache_time);

  EXIT_SERVER ();

  return list_id;
#endif /* !CS_MODE */
}

/*
 * qmgr_prepare_and_execute_query -
 *
 * return:
 *
 *   xasl_buffer(in):
 *   xasl_size(in):
 *   query_id(in):
 *   dbval_cnt(in):
 *   dbval_ptr(in):
 *   flag(in):
 *
 * NOTE:
 */
QFILE_LIST_ID *
qmgr_prepare_and_execute_query (char *xasl_buffer, int xasl_size,
				int *query_id, int dbval_cnt,
				DB_VALUE * dbval_ptr, QUERY_FLAG flag)
{
#if defined(CS_MODE)
  QFILE_LIST_ID *regu_result = NULL;
  int req_error, senddata_size, replydata_size1, replydata_size2;
  int i, size;
  char *ptr, *senddata, *replydata;
  DB_VALUE *dbval;
  OR_ALIGNED_BUF (OR_INT_SIZE * 3) a_request;
  char *request;
  OR_ALIGNED_BUF (OR_INT_SIZE * 4) a_reply;
  char *reply;
  char *page_ptr;
  int page_size, request_type;

  request = OR_ALIGNED_BUF_START (a_request);
  reply = OR_ALIGNED_BUF_START (a_reply);

  senddata = NULL;
  senddata_size = 0;
  for (i = 0, dbval = dbval_ptr; i < dbval_cnt; i++, dbval++)
    {
      senddata_size += or_db_value_size (dbval);
    }

  if (senddata_size)
    {
      senddata = (char *) malloc (senddata_size);
      if (senddata == NULL)
	{
	  return NULL;
	}
    }

  ptr = or_pack_int (request, dbval_cnt);
  ptr = or_pack_int (ptr, senddata_size);
  ptr = or_pack_int (ptr, flag);

  ptr = senddata;
  for (i = 0, dbval = dbval_ptr; i < dbval_cnt; i++, dbval++)
    {
      ptr = or_pack_db_value (ptr, dbval);
    }

  if (IS_SYNC_EXEC_MODE (flag))
    {
      request_type = SERVER_QM_QUERY_PREPARE_AND_EXECUTE;
    }
  else
    {
      request_type = SERVER_QM_QUERY_PREPARE_AND_EXECUTE_ASYNC;
    }

  req_error = net_client_request_with_callback (request_type,
						request,
						OR_ALIGNED_BUF_SIZE
						(a_request), reply,
						OR_ALIGNED_BUF_SIZE (a_reply),
						xasl_buffer, xasl_size,
						senddata, senddata_size,
						&replydata, &replydata_size1,
						&page_ptr, &replydata_size2);
  if (!req_error)
    {
      /* should be the same as replydata_size */
      ptr = or_unpack_int (&reply[OR_INT_SIZE], &size);
      ptr = or_unpack_int (ptr, &page_size);
      ptr = or_unpack_int (ptr, query_id);

      /* not interested in the return size in the reply buffer, should do some
         kind of range checking here */
      if (replydata != NULL && size)
	{
	  ptr = or_unpack_unbound_listid (replydata, (void **) &regu_result);
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

  ENTER_SERVER ();

  regu_result = xqmgr_prepare_and_execute_query (NULL, xasl_buffer, xasl_size,
						 query_id, dbval_cnt,
						 dbval_ptr, &flag);

  EXIT_SERVER ();

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
qmgr_end_query (int query_id)
{
#if defined(CS_MODE)
  int status = ER_FAILED;
  int req_error;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_request;
  char *request;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply;

  request = OR_ALIGNED_BUF_START (a_request);
  reply = OR_ALIGNED_BUF_START (a_reply);

  or_pack_int (request, query_id);

  req_error = net_client_request (SERVER_QM_QUERY_END,
				  request, OR_ALIGNED_BUF_SIZE (a_request),
				  reply, OR_ALIGNED_BUF_SIZE (a_reply), NULL,
				  0, NULL, 0);
  if (!req_error)
    {
      or_unpack_int (reply, &status);
    }

  return status;
#else /* CS_MODE */
  int success = ER_FAILED;

  ENTER_SERVER ();

  success = xqmgr_end_query (NULL, query_id);

  EXIT_SERVER ();

  return success;
#endif /* !CS_MODE */
}

/*
 * qmgr_drop_query_plan - Send a SERVER_QM_DROP_PLAN request to the server
 *
 * Request the server to delete the XASL cache specified by either
 * the query string or the XASL file id. When the client want to delete
 * the old XASL cache entry and update it with new one, this function will be
 * used.
 * This function is a counter part to sqmgr_drop_query_plan().
 */
/*
 * qmgr_drop_query_plan -
 *
 * return:
 *
 *   query_str(in):
 *   user_oid(in):
 *   xasl_id(in):
 *   delete(in):
 *
 * NOTE:
 */
int
qmgr_drop_query_plan (const char *query_str, const OID * user_oid,
		      const XASL_ID * xasl_id, bool drop)
{
#if defined(CS_MODE)
  int status = ER_FAILED;
  int req_error, request_size;
  char *request, *reply, *ptr;
  OR_ALIGNED_BUF (OR_LOID_SIZE) a_reply;

  reply = OR_ALIGNED_BUF_START (a_reply);

  request_size = length_const_string (query_str) + OR_OID_SIZE + OR_LOID_SIZE
    + OR_INT_SIZE;

  request = (char *) malloc (request_size);
  if (!request)
    {
      return ER_FAILED;
    }

  /* pack query string as a request data */
  ptr = pack_const_string (request, query_str);
  /* pack OID of the current user */
  ptr = or_pack_oid (ptr, (OID *) user_oid);
  /* pack XASL file id (XASL_ID)
     - borrow LOID structure only for transmision purpose */
  ptr = or_pack_loid (ptr, (LOID *) xasl_id);
  /* pack 'delete' flag */
  ptr = or_pack_int (ptr, drop);

  /* send SERVER_QM_QUERY_DROP_PLAN request with request data;
     receive status code (int) as a reply */
  req_error = net_client_request (SERVER_QM_QUERY_DROP_PLAN,
				  request, request_size,
				  reply, OR_ALIGNED_BUF_SIZE (a_reply),
				  NULL, 0, NULL, 0);
  if (!req_error)
    {
      /* first argument should be status code (int) */
      (void) or_unpack_int (reply, &status);
    }

  if (request)
    {
      free_and_init (request);
    }

  return status;

#else /* CS_MODE */
  int status;

  ENTER_SERVER ();

  /* call the server routine of query drop plan */
  status = xqmgr_drop_query_plan (NULL, query_str, user_oid, xasl_id, drop);

  EXIT_SERVER ();

  return status;
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
  OR_ALIGNED_BUF (OR_LOID_SIZE) a_request;
  OR_ALIGNED_BUF (OR_LOID_SIZE) a_reply;

  request = OR_ALIGNED_BUF_START (a_request);
  request_size = OR_INT_SIZE;
  reply = OR_ALIGNED_BUF_START (a_reply);

  or_pack_int (request, 0);	/* dummy parameter */

  /* send SERVER_QM_QUERY_DROP_ALL_PLANS request with request data;
     receive status code (int) as a reply */
  req_error = net_client_request (SERVER_QM_QUERY_DROP_ALL_PLANS,
				  request, request_size,
				  reply, OR_ALIGNED_BUF_SIZE (a_reply),
				  NULL, 0, NULL, 0);
  if (!req_error)
    {
      /* first argument should be status code (int) */
      (void) or_unpack_int (reply, &status);
    }

  return status;
#else /* CS_MODE */
  int status;

  ENTER_SERVER ();

  /* call the server routine of query drop plan */
  status = xqmgr_drop_all_query_plans (NULL);

  EXIT_SERVER ();

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

  /* TODO: simpler function */
  req_error = net_client_request_recv_stream (SERVER_QM_QUERY_DUMP_PLANS,
					      NULL, 0, NULL, 0,
					      NULL, 0, outfp);
#else /* CS_MODE */

  ENTER_SERVER ();

  xqmgr_dump_query_plans (NULL, outfp);

  EXIT_SERVER ();
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

  /* TODO: simpler function */
  req_error = net_client_request_recv_stream (SERVER_QM_QUERY_DUMP_CACHE,
					      NULL, 0, NULL, 0,
					      NULL, 0, outfp);
#else /* CS_MODE */

  ENTER_SERVER ();

  xqmgr_dump_query_cache (NULL, outfp);

  EXIT_SERVER ();
#endif /* !CS_MODE */
}

/*
 * qmgr_get_query_info -
 *
 * return:
 *
 *   query_result(in):
 *   done(in):
 *   count(in):
 *   error(in):
 *   error_string(in):
 *
 * NOTE:
 */
int
qmgr_get_query_info (DB_QUERY_RESULT * query_result, int *done,
		     int *count, int *error, char **error_string)
{
#if defined(CS_MODE)
  int req_error;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_request;
  char *request;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *area, *reply, *ptr;
  int area_size;

  request = OR_ALIGNED_BUF_START (a_request);
  reply = OR_ALIGNED_BUF_START (a_reply);

  or_pack_int (request, (int) (query_result->res.s.query_id));

  req_error = net_client_request2 (SERVER_QM_GET_QUERY_INFO,
				   request, OR_ALIGNED_BUF_SIZE (a_request),
				   reply, OR_ALIGNED_BUF_SIZE (a_reply),
				   NULL, 0, &area, &area_size);
  if (!req_error)
    {
      int dummy_int;
      char *dummy_str = NULL;

      ptr = or_unpack_int (area, &area_size);
      ptr = or_unpack_int (ptr, done);
      ptr = or_unpack_int (ptr, count);
      query_result->res.s.cursor_id.list_id.tuple_cnt = *count;

      /*
       * Have to unpack these fields unconditionally to keep from screwing
       * up others who need to understand the buffer layout.  (Well, I
       * suppose we could blow off the string if we wanted to [since it's
       * last], but sure as we do, someone will add something to the end of
       * this message format and forget about this problem.)
       */
      ptr = or_unpack_int (ptr, error ? error : &dummy_int);
      ptr = or_unpack_string_nocopy (ptr, &dummy_str);
      if (error_string)
	{
	  if (dummy_str == NULL)
	    {
	      *error_string = NULL;
	    }
	  else
	    {
	      /*
	       * we're handing it off to API-land and
	       * making them responsible for freeing it.  In that case, we'd
	       * better copy it into a buffer that can be free'd via ordinary free().
	       */
	      int size;

	      size = strlen (dummy_str) + 1;
	      *error_string = (char *) malloc (size);
	      if (*error_string == NULL)
		{
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			  ER_OUT_OF_VIRTUAL_MEMORY, 1, size);
		  *done = ER_OUT_OF_VIRTUAL_MEMORY;
		}
	      else
		{
		  strcpy (*error_string, dummy_str);
		}
	    }
	}

      if (area)
	{
	  free_and_init (area);
	}
      return *done;
    }

  return req_error;

#else /* CS_MODE */
  /* Cannot run in standalone mode */
  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_INVALID_OPERATION, 0);
  return ER_FAILED;
#endif /* !CS_MODE */
}

/*
 * qmgr_sync_query -
 *
 * return:
 *
 *   query_result(in):
 *   wait(in):
 *
 * NOTE:
 */
int
qmgr_sync_query (DB_QUERY_RESULT * query_result, int wait)
{
#if defined(CS_MODE)
  int req_error;
  OR_ALIGNED_BUF (OR_INT_SIZE * 2) a_request;
  char *request, *ptr;
  OR_ALIGNED_BUF (OR_INT_SIZE * 2) a_reply;
  char *reply;
  int status = ER_FAILED;
  QFILE_LIST_ID *list_id;
  char *replydata = 0;
  int replydata_size;

  request = OR_ALIGNED_BUF_START (a_request);
  reply = OR_ALIGNED_BUF_START (a_reply);

  ptr = or_pack_int (request, (int) (query_result->res.s.query_id));
  ptr = or_pack_int (ptr, wait);

  req_error = net_client_request2 (SERVER_QM_QUERY_SYNC,
				   request, OR_ALIGNED_BUF_SIZE (a_request),
				   reply, OR_ALIGNED_BUF_SIZE (a_reply), NULL,
				   0, &replydata, &replydata_size);
  if (!req_error)
    {
      ptr = or_unpack_int (reply, &replydata_size);
      ptr = or_unpack_int (ptr, &status);
      if (replydata && replydata_size)
	{
	  ptr = or_unpack_unbound_listid (replydata, (void **) &list_id);
	  /* free old list_id */
	  cursor_free_list_id (&query_result->res.s.cursor_id.list_id, false);
	  cursor_copy_list_id (&query_result->res.s.cursor_id.list_id,
			       list_id);
	  cursor_free_list_id (list_id, true);

	  free_and_init (replydata);
	}
    }

  return status;
#else /* CS_MODE */
  /* Cannot run in standalone mode */
  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_INVALID_OPERATION, 0);
  return ER_INVALID_OPERATION;
#endif /* !CS_MODE */
}

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
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply;
  DB_TIMESTAMP sysutime;

  request = OR_ALIGNED_BUF_START (a_request);
  reply = OR_ALIGNED_BUF_START (a_reply);

  req_error = net_client_request (SERVER_QPROC_GET_SYS_TIMESTAMP,
				  request, OR_ALIGNED_BUF_SIZE (a_request),
				  reply, OR_ALIGNED_BUF_SIZE (a_reply), NULL,
				  0, NULL, 0);
  if (!req_error)
    {
      (void) or_unpack_utime (reply, &sysutime);
      db_make_timestamp (value, sysutime);
    }

  return NO_ERROR;
#else /* CS_MODE */

  ENTER_SERVER ();

  db_sys_timestamp (value);

  EXIT_SERVER ();

  return NO_ERROR;
#endif /* !CS_MODE */
}

/*
 * qp_get_serial_current_value -
 *
 * return:
 *
 *   value(in):
 *   oid(in):
 *
 * NOTE:
 */
int
qp_get_serial_current_value (DB_VALUE * value, DB_VALUE * oid)
{
#if defined(CS_MODE)
  int req_error;
  char *request;
  int request_size;
  char *area = 0;
  int val_size;
  OR_ALIGNED_BUF (OR_INT_SIZE + OR_INT_SIZE) a_reply;
  char *reply;
  int rc;

  reply = OR_ALIGNED_BUF_START (a_reply);

  request_size = or_db_value_size (oid);
  request = (char *) malloc (request_size);
  if (request == NULL)
    {
      return ER_FAILED;
    }

  or_pack_value (request, oid);

  req_error = net_client_request2 (SERVER_QPROC_GET_CURRENT_VALUE,
				   request, request_size,
				   reply, OR_ALIGNED_BUF_SIZE (a_reply),
				   NULL, 0, &area, &val_size);
  free_and_init (request);
  if (!req_error)
    {
      reply = or_unpack_int (reply, &val_size);
      or_unpack_int (reply, &req_error);
    }

  if (!req_error)
    {
      or_unpack_value (area, value);
      rc = NO_ERROR;
    }
  else
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, req_error, 0);
      rc = ER_FAILED;
    }

  if (area != NULL)
    {
      free_and_init (area);
    }

  return rc;

#else /* CS_MODE */
  int req_error = NO_ERROR;

  ENTER_SERVER ();

  req_error = xqp_get_serial_current_value (NULL, oid, value);
  if (req_error != NO_ERROR)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, er_errid (), 0);
    }

  EXIT_SERVER ();

  return req_error;
#endif /* !CS_MODE */
}

/*
 * qp_get_serial_next_value -
 *
 * return:
 *
 *   value(in):
 *   oid(in):
 *
 * NOTE:
 */
int
qp_get_serial_next_value (DB_VALUE * value, DB_VALUE * oid)
{
#if defined(CS_MODE)
  int req_error;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_request;
  char *request;
  int request_size;
  char *area = 0;
  int val_size;
  OR_ALIGNED_BUF (OR_INT_SIZE + OR_INT_SIZE) a_reply;
  char *reply;
  int rc;

  request = OR_ALIGNED_BUF_START (a_request);
  reply = OR_ALIGNED_BUF_START (a_reply);

  request_size = or_db_value_size (oid);
  request = (char *) malloc (request_size);
  if (request == NULL)
    {
      return ER_FAILED;
    }

  or_pack_value (request, oid);

  req_error = net_client_request2 (SERVER_QPROC_GET_NEXT_VALUE,
				   request, request_size,
				   reply, OR_ALIGNED_BUF_SIZE (a_reply),
				   NULL, 0, &area, &val_size);
  free_and_init (request);
  if (!req_error)
    {
      reply = or_unpack_int (reply, &val_size);
      (void) or_unpack_int (reply, &req_error);
    }

  if (req_error == NO_ERROR)
    {
      (void) or_unpack_value (area, value);
      rc = NO_ERROR;
    }
  else
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, req_error, 0);
      rc = ER_FAILED;
    }

  if (area != NULL)
    {
      free_and_init (area);
    }

  return rc;
#else /* CS_MODE */
  int req_error;

  ENTER_SERVER ();

  req_error = xqp_get_serial_next_value (NULL, oid, value);
  if (req_error != NO_ERROR)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, er_errid (), 0);
    }

  EXIT_SERVER ();

  return req_error;
#endif /* !CS_MODE */
}

/*
 * mnt_server_start_stats -
 *
 * return:
 *
 * NOTE:
 */
int
mnt_server_start_stats (void)
{
#if defined(CS_MODE)
  int status = ER_FAILED;
  int req_error;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply;

  reply = OR_ALIGNED_BUF_START (a_reply);

  req_error = net_client_request (SERVER_MNT_SERVER_START_STATS,
				  NULL, 0, reply,
				  OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0,
				  NULL, 0);
  if (!req_error)
    {
      or_unpack_int (reply, &status);
    }

  return (status);

#else /* CS_MODE */
  int success = ER_FAILED;

  ENTER_SERVER ();

  success = xmnt_server_start_stats (NULL);

  EXIT_SERVER ();

  return success;
#endif /* !CS_MODE */
}

/*
 * mnt_server_stop_stats -
 *
 * return:
 *
 * NOTE:
 */
void
mnt_server_stop_stats (void)
{
#if defined(CS_MODE)
  int req_error;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;	/* need dummy reply message */
  char *reply;

  reply = OR_ALIGNED_BUF_START (a_reply);

  req_error = net_client_request (SERVER_MNT_SERVER_STOP_STATS,
				  NULL, 0, reply,
				  OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0,
				  NULL, 0);
#else /* CS_MODE */

  ENTER_SERVER ();

  xmnt_server_stop_stats (NULL);

  EXIT_SERVER ();
#endif /* !CS_MODE */
}

/*
 * mnt_server_reset_stats -
 *
 * return:
 *
 * NOTE:
 */
void
mnt_server_reset_stats (void)
{
#if defined(CS_MODE)
  int req_error;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;	/* need dummy reply message */
  char *reply;

  reply = OR_ALIGNED_BUF_START (a_reply);

  req_error = net_client_request (SERVER_MNT_SERVER_RESET_STATS,
				  NULL, 0, reply,
				  OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0,
				  NULL, 0);
#else /* CS_MODE */

  ENTER_SERVER ();

  xmnt_server_reset_stats (NULL);

  EXIT_SERVER ();
#endif /* !CS_MODE */
}

/*
 * mnt_server_copy_stats -
 *
 * return:
 *
 *   to_stats(in):
 *
 * NOTE:
 */
void
mnt_server_copy_stats (MNT_SERVER_EXEC_STATS * to_stats)
{
#if defined(CS_MODE)
  int req_error;
  OR_ALIGNED_BUF (STAT_SIZE_PACKED) a_reply;
  char *reply;

  reply = OR_ALIGNED_BUF_START (a_reply);

  req_error = net_client_request (SERVER_MNT_SERVER_COPY_STATS,
				  NULL, 0, reply,
				  OR_ALIGNED_BUF_SIZE (a_reply),
				  NULL, 0, NULL, 0);
  if (!req_error)
    {
      net_unpack_stats (reply, to_stats);
    }
#else /* CS_MODE */

  ENTER_SERVER ();

  xmnt_server_copy_stats (NULL, to_stats);

  EXIT_SERVER ();
#endif /* !CS_MODE */
}

/*
 * catalog_is_acceptable_new_representation -
 *
 * return:
 *
 *   class_id(in):
 *   hfid(in):
 *   can_accept(in):
 *
 * NOTE:
 */
int
catalog_is_acceptable_new_representation (OID * class_id, HFID * hfid,
					  int *can_accept)
{
#if defined(CS_MODE)
  int req_error, status, accept;
  OR_ALIGNED_BUF (OR_OID_SIZE + OR_HFID_SIZE) a_request;
  char *request;
  OR_ALIGNED_BUF (OR_INT_SIZE * 2) a_reply;
  char *reply;
  char *ptr;

  request = OR_ALIGNED_BUF_START (a_request);
  reply = OR_ALIGNED_BUF_START (a_reply);

  ptr = or_pack_oid (request, class_id);
  ptr = or_pack_hfid (ptr, hfid);

  req_error = net_client_request (SERVER_CT_CAN_ACCEPT_NEW_REPR,
				  request, OR_ALIGNED_BUF_SIZE (a_request),
				  reply, OR_ALIGNED_BUF_SIZE (a_reply),
				  NULL, 0, NULL, 0);

  if (req_error)
    {
      status = (int) ER_FAILED;
    }
  else
    {
      ptr = or_unpack_int (reply, &status);
      ptr = or_unpack_int (ptr, &accept);
      *can_accept = (int) accept;
    }

  return status;
#else /* CS_MODE */
  int success = ER_FAILED;

  ENTER_SERVER ();

  success =
    xcatalog_is_acceptable_new_representation (NULL, class_id, hfid,
					       can_accept);

  EXIT_SERVER ();

  return success;
#endif /* !CS_MODE */
}

/*
 * css_restart_event_handler -
 *
 * return:
 *
 * NOTE:
 */
int
css_restart_event_handler (void)
{
#if defined(CS_MODE)
  OR_ALIGNED_BUF (OR_INT_SIZE) a_request;
  char *request;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply, *ptr;
  int status, req_error;

  request = OR_ALIGNED_BUF_START (a_request);
  reply = OR_ALIGNED_BUF_START (a_reply);

  req_error = net_client_request (SERVER_RESTART_EVENT_HANDLER,
				  request, OR_ALIGNED_BUF_SIZE (a_request),
				  reply, OR_ALIGNED_BUF_SIZE (a_reply),
				  NULL, 0, NULL, 0);
  if (req_error)
    {
      status = ER_FAILED;
    }
  else
    {
      ptr = or_unpack_int (reply, &status);
    }

  return status;
#else /* CS_MODE */
  return 1;
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
thread_kill_tran_index (int kill_tran_index, char *kill_user,
			char *kill_host, int kill_pid)
{
#if defined(CS_MODE)
  int success = ER_FAILED;
  int request_size;
  char *request;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply, *ptr;
  int req_error;

  reply = OR_ALIGNED_BUF_START (a_reply);

  request_size = OR_INT_SIZE + OR_INT_SIZE + length_const_string (kill_user)
    + length_const_string (kill_host);

  request = (char *) malloc (request_size);
  if (request)
    {
      ptr = or_pack_int (request, kill_tran_index);
      ptr = pack_const_string (ptr, kill_user);
      ptr = pack_const_string (ptr, kill_host);
      ptr = or_pack_int (ptr, kill_pid);

      req_error = net_client_request (SERVER_CSS_KILL_TRANSACTION,
				      request, request_size,
				      reply, OR_ALIGNED_BUF_SIZE (a_reply),
				      NULL, 0, NULL, 0);

      if (!req_error)
	{
	  or_unpack_int (reply, &success);
	}

      free_and_init (request);
    }

  return success;
#else /* CS_MODE */
  er_log_debug (ARG_FILE_LINE,
		"css_kill_client: THIS IS ONLY a C/S function");
  return ER_FAILED;
#endif /* !CS_MODE */
}

/*
 * logtb_get_pack_tran_table -
 *
 * return:
 *
 *   buffer_p(in):
 *   size_p(in):
 *
 * NOTE:
 */
int
logtb_get_pack_tran_table (char **buffer_p, int *size_p)
{
#if defined(CS_MODE)
  int error = NO_ERROR;
  int req_error;
  int ival;
  OR_ALIGNED_BUF (OR_INT_SIZE + OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  char *ptr;

  req_error = net_client_request2 (SERVER_LOG_GETPACK_TRANTB,
				   NULL, 0,
				   reply, OR_ALIGNED_BUF_SIZE (a_reply),
				   NULL, 0, buffer_p, size_p);
  if (req_error)
    {
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

  ENTER_SERVER ();

  error = xlogtb_get_pack_tran_table (NULL, buffer_p, size_p);

  EXIT_SERVER ();

  return error;
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
heap_get_class_num_objects_pages (HFID * hfid, int approximation,
				  int *nobjs, int *npages)
{
#if defined(CS_MODE)
  int req_error, status, num_objs, num_pages;
  OR_ALIGNED_BUF (OR_HFID_SIZE + OR_INT_SIZE) a_request;
  char *request;
  OR_ALIGNED_BUF (OR_INT_SIZE * 3) a_reply;
  char *reply;
  char *ptr;

  request = OR_ALIGNED_BUF_START (a_request);
  reply = OR_ALIGNED_BUF_START (a_reply);

  ptr = or_pack_hfid (request, hfid);
  ptr = or_pack_int (ptr, approximation);

  req_error = net_client_request (SERVER_HEAP_GET_CLASS_NOBJS_AND_NPAGES,
				  request, OR_ALIGNED_BUF_SIZE (a_request),
				  reply, OR_ALIGNED_BUF_SIZE (a_reply),
				  NULL, 0, NULL, 0);

  if (req_error)
    {
      status = ER_FAILED;
    }
  else
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

  ENTER_SERVER ();

  success = xheap_get_class_num_objects_pages (NULL, hfid, approximation,
					       nobjs, npages);

  EXIT_SERVER ();

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
  int req_error, status;
  OR_ALIGNED_BUF (OR_BTID_SIZE) a_request;
  char *request;
  OR_ALIGNED_BUF (OR_INT_SIZE * 5) a_reply;
  char *reply;
  char *ptr;

  request = OR_ALIGNED_BUF_START (a_request);
  reply = OR_ALIGNED_BUF_START (a_reply);

  ptr = or_pack_btid (request, btid);

  req_error = net_client_request (SERVER_BTREE_GET_STATISTICS,
				  request,
				  OR_ALIGNED_BUF_SIZE (a_request),
				  reply,
				  OR_ALIGNED_BUF_SIZE (a_reply),
				  NULL, 0, NULL, 0);

  if (req_error)
    {
      status = ER_FAILED;
    }
  else
    {
      ptr = or_unpack_int (reply, &status);
      ptr = or_unpack_int (ptr, &stat_info->leafs);
      ptr = or_unpack_int (ptr, &stat_info->pages);
      ptr = or_unpack_int (ptr, &stat_info->height);
      ptr = or_unpack_int (ptr, &stat_info->keys);
    }

  return status;

#else /* CS_MODE */
  int success = ER_FAILED;

  ENTER_SERVER ();

  success = btree_get_stats (NULL, btid, stat_info, false);

  EXIT_SERVER ();

  return success;
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
  int req_error, trid;
  int success = NO_ERROR;
  OR_ALIGNED_BUF (OR_INT_SIZE + OR_INT_SIZE) a_reply;
  char *reply, *ptr;

  reply = OR_ALIGNED_BUF_START (a_reply);

  req_error = net_client_request (SERVER_TM_LOCAL_TRANSACTION_ID,
				  NULL, 0, reply,
				  OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0,
				  NULL, 0);
  if (!req_error)
    {
      ptr = or_unpack_int (reply, &success);
      ptr = or_unpack_int (ptr, &trid);
    }
  else
    {
      success = ER_FAILED;
    }

  DB_MAKE_INTEGER (result_trid, trid);
  return success;

#else /* CS_MODE */
  int success;

  ENTER_SERVER ();

  success = xtran_get_local_transaction_id (NULL, result_trid);

  EXIT_SERVER ();

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
qp_get_server_info (SERVER_INFO * server_info)
{
#if defined(CS_MODE)
  int req_error, status;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_request;
  char *request;
  OR_ALIGNED_BUF (OR_INT_SIZE + OR_INT_SIZE) a_reply;
  char *reply;
  char *ptr, *area = NULL;
  int val_size, i;

  request = OR_ALIGNED_BUF_START (a_request);
  reply = OR_ALIGNED_BUF_START (a_reply);

  ptr = or_pack_int (request, server_info->info_bits);

  req_error = net_client_request2 (SERVER_QPROC_GET_SERVER_INFO,
				   request, OR_ALIGNED_BUF_SIZE (a_request),
				   reply, OR_ALIGNED_BUF_SIZE (a_reply),
				   NULL, 0, &area, &val_size);

  if (req_error)
    {
      status = ER_FAILED;
    }
  else
    {
      ptr = or_unpack_int (reply, &val_size);
      ptr = or_unpack_int (ptr, &status);
      if (status == NO_ERROR)
	{
	  ptr = area;
	  for (i = 0; i < SI_CNT; i++)
	    {
	      if (server_info->info_bits & (1 << i))
		{
		  ptr = or_unpack_value (ptr, server_info->value[i]);
		}
	    }
	}
    }

  if (area != NULL)
    free_and_init (area);

  return status;

#else /* CS_MODE */
  int success = NO_ERROR;
  int i;

  ENTER_SERVER ();

  for (i = 0; i < SI_CNT && success == NO_ERROR; i++)
    {
      if (server_info->info_bits & (1 << i))
	{
	  switch (1 << i)
	    {
	    case SI_SYS_TIMESTAMP:
	      success = db_sys_timestamp (server_info->value[i]);
	      break;
	    case SI_LOCAL_TRANSACTION_ID:
	      success = db_local_transaction_id (server_info->value[i]);
	      break;
	    }
	}
    }

  EXIT_SERVER ();
  return success;
#endif /* !CS_MODE */
}

/*
 * sysprm_change_server_parameters -
 *
 * return:
 *
 *   data(in):
 *
 * NOTE:
 */
int
sysprm_change_server_parameters (const char *data)
{
#if defined(CS_MODE)
  int rc = PRM_ERR_COMM_ERR;
  int request_size;
  int req_error;
  char *request;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply;

  reply = OR_ALIGNED_BUF_START (a_reply);

  request_size = length_const_string (data);
  request = (char *) malloc (request_size);
  if (request)
    {
      pack_const_string (request, data);
      req_error = net_client_request (SERVER_PRM_SET_PARAMETERS,
				      request, request_size,
				      reply, OR_ALIGNED_BUF_SIZE (a_reply),
				      NULL, 0, NULL, 0);
      if (!req_error)
	{
	  or_unpack_int (reply, &rc);
	}

      free_and_init (request);
    }
  return rc;
#else /* CS_MODE */
  int rc;
  ENTER_SERVER ();
  rc = xsysprm_change_server_parameters (data);
  EXIT_SERVER ();
  return rc;
#endif /* !CS_MODE */
}

/*
 * sysprm_obtain_server_parameters -
 *
 * return:
 *
 *   data(in):
 *   len(in):
 *
 * NOTE:
 */
int
sysprm_obtain_server_parameters (char *data, int len)
{
#if defined(CS_MODE)
  int rc = PRM_ERR_COMM_ERR;
  int req_error;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_request;
  char *request;
  OR_ALIGNED_BUF (OR_INT_SIZE * 2) a_reply;
  char *reply;
  char *ptr;

  request = OR_ALIGNED_BUF_START (a_request);
  reply = OR_ALIGNED_BUF_START (a_reply);

  or_pack_int (request, len);

  req_error = net_client_request2_no_malloc (SERVER_PRM_GET_PARAMETERS,
					     request,
					     OR_ALIGNED_BUF_SIZE (a_request),
					     reply,
					     OR_ALIGNED_BUF_SIZE (a_reply),
					     data, len, data, &len);
  if (!req_error)
    {
      ptr = or_unpack_int (reply, &len);
      ptr = or_unpack_int (ptr, &rc);
    }

  return rc;
#else /* CS_MODE */
  int rc;
  ENTER_SERVER ();
  rc = xsysprm_obtain_server_parameters (data, len);
  EXIT_SERVER ();
  return rc;
#endif /* !CS_MODE */
}

/*
 * heap_has_instance -
 *
 * return:
 *
 *   hfid(in):
 *   class_oid(in):
 *
 * NOTE:
 */
int
heap_has_instance (HFID * hfid, OID * class_oid)
{
#if defined(CS_MODE)
  int req_error, status;
  OR_ALIGNED_BUF (OR_HFID_SIZE + OR_OID_SIZE) a_request;
  char *request;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply;
  char *ptr;

  request = OR_ALIGNED_BUF_START (a_request);
  reply = OR_ALIGNED_BUF_START (a_reply);

  ptr = or_pack_hfid (request, hfid);
  ptr = or_pack_oid (ptr, class_oid);

  req_error = net_client_request (SERVER_HEAP_HAS_INSTANCE,
				  request, OR_ALIGNED_BUF_SIZE (a_request),
				  reply, OR_ALIGNED_BUF_SIZE (a_reply),
				  NULL, 0, NULL, 0);

  if (req_error)
    {
      status = ER_FAILED;
    }
  else
    {
      ptr = or_unpack_int (reply, &status);
    }

  return status;
#else /* CS_MODE */
  int r = ER_FAILED;
  ENTER_SERVER ();
  r = xheap_has_instance (NULL, hfid, class_oid);
  EXIT_SERVER ();
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

  req_error = net_client_request (SERVER_JSP_GET_SERVER_PORT,
				  request, OR_ALIGNED_BUF_SIZE (a_request),
				  reply, OR_ALIGNED_BUF_SIZE (a_reply), NULL,
				  0, NULL, 0);
  if (!req_error)
    {
      or_unpack_int (reply, &port);
    }

  return port;
#else /* CS_MODE */
  int port;
  ENTER_SERVER ();
  port = jsp_server_port ();
  EXIT_SERVER ();
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
  OR_ALIGNED_BUF (OR_LOG_LSA_SIZE) a_reply;
  char *reply;

  request = OR_ALIGNED_BUF_START (a_request);
  reply = OR_ALIGNED_BUF_START (a_reply);
  req_error = net_client_request (SERVER_REPL_LOG_GET_APPEND_LSA,
				  request, OR_ALIGNED_BUF_SIZE (a_request),
				  reply, OR_ALIGNED_BUF_SIZE (a_reply), NULL,
				  0, NULL, 0);
  if (!req_error)
    {
      or_unpack_log_lsa (reply, lsa);
      success = NO_ERROR;
    }

  return success;
#else /* CS_MODE */
  LOG_LSA *tmp_lsa = NULL;
  int r = ER_FAILED;
  ENTER_SERVER ();
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
  EXIT_SERVER ();
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
  int request_size = 0;
  char *request = NULL, *ptr;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply;
  REPL_INFO_SCHEMA *repl_schema;

  reply = OR_ALIGNED_BUF_START (a_reply);

  switch (repl_info->repl_info_type)
    {
    case REPL_INFO_TYPE_SCHEMA:
      repl_schema = (REPL_INFO_SCHEMA *) repl_info->info;
      request_size = OR_INT_SIZE;	/* REPL_INFO.REPL_INFO_TYPE */
      request_size += OR_INT_SIZE;	/* REPL_INFO_SCHEMA.statement_type */
      request_size += length_const_string (repl_schema->name);
      request_size += length_const_string (repl_schema->ddl);
      request = (char *) malloc (request_size);
      if (request == NULL)
	{
	  return success;
	}

      ptr = request;
      ptr = or_pack_int (ptr, REPL_INFO_TYPE_SCHEMA);
      ptr = or_pack_int (ptr, repl_schema->statement_type);
      ptr = pack_const_string (ptr, repl_schema->name);
      ptr = pack_const_string (ptr, repl_schema->ddl);
      break;

    default:
      return success;
    }

  if (request)
    {
      req_error = net_client_request (SERVER_REPL_INFO,
				      request, request_size, reply,
				      OR_ALIGNED_BUF_SIZE (a_reply),
				      NULL, 0, NULL, 0);
      free_and_init (request);
      if (!req_error)
	{
	  or_unpack_int (reply, &success);
	}
    }

  return success;
#else /* CS_MODE */
  int r = ER_FAILED;

  ENTER_SERVER ();
  r = xrepl_set_info (NULL, repl_info);
  EXIT_SERVER ();
  return r;
#endif /* !CS_MODE */
}

/*
 * locator_build_fk_obj_cache -
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
 *   cache_attr_id(in):
 *   fk_name(in):
 *
 * NOTE:
 */
int
locator_build_fk_obj_cache (OID * cls_oid, HFID * hfid, TP_DOMAIN * key_type,
			    int n_attrs, int *attr_ids, OID * pk_cls_oid,
			    BTID * pk_btid, int cache_attr_id, char *fk_name)
{
#if defined(CS_MODE)
  int error, req_error, request_size, domain_size;
  char *ptr;
  char *request;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply;
  int i;

  error = NO_ERROR;
  reply = OR_ALIGNED_BUF_START (a_reply);

  domain_size = or_packed_domain_size (key_type, 0);
  request_size = OR_OID_SIZE + OR_HFID_SIZE + domain_size + OR_INT_SIZE
    + (n_attrs * OR_INT_SIZE) + OR_OID_SIZE
    + or_align_length_for_btree (OR_BTID_SIZE) + OR_INT_SIZE
    + or_packed_string_length (fk_name);

  request = (char *) malloc (request_size);
  if (request == NULL)
    {
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
  ptr = PTR_ALIGN (ptr, OR_INT_SIZE);
  ptr = or_pack_int (ptr, cache_attr_id);
  ptr = or_pack_string (ptr, fk_name);

  req_error = net_client_request (SERVER_LC_BUILD_FK_OBJECT_CACHE,
				  request, request_size, reply,
				  OR_ALIGNED_BUF_SIZE (a_reply), NULL, 0,
				  NULL, 0);
  if (!req_error)
    {
      ptr = or_unpack_int (reply, &error);
    }

  free_and_init (request);
  return error;
#else /* CS_MODE */
  int error = NO_ERROR;

  ENTER_SERVER ();

  error = xlocator_build_fk_object_cache (NULL, cls_oid, hfid, key_type,
					  n_attrs, attr_ids, pk_cls_oid,
					  pk_btid, cache_attr_id, fk_name);
  if (error == ER_FAILED)
    {
      error = er_errid ();
    }

  EXIT_SERVER ();
  return error;
#endif /* !CS_MODE */
}

/* stub for histogram */
#if defined(SA_MODE)
bool
histo_is_supported (void)
{
  return false;
}

void
histo_start (void)
{
}

void
histo_stop (void)
{
}

void
histo_print (void)
{
}

void
histo_clear (void)
{
  printf ("\nHistograms not enabled in standalone mode.\n");
}
#endif /* SA_MODE */
