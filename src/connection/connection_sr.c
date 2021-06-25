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
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 */

/*
 * connection_sr.c - Client/Server connection list management
 */

#ident "$Id$"

#include "config.h"

#if defined (WINDOWS)
#include <io.h>
#endif
#include <filesystem>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <assert.h>

#if defined(WINDOWS)
#include <winsock2.h>
#include <windows.h>
#else /* WINDOWS */
#include <sys/time.h>
#include <sys/ioctl.h>
#include <sys/uio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#endif /* WINDOWS */

#if defined(_AIX)
#include <sys/select.h>
#endif /* _AIX */

#if defined(SOLARIS)
#include <sys/filio.h>
#include <netdb.h>
#endif /* SOLARIS */

#if defined(SOLARIS) || defined(LINUX)
#include <unistd.h>
#endif /* SOLARIS || LINUX */

#include "porting.h"
#include "error_manager.h"
#include "connection_globals.h"
#include "filesys.hpp"
#include "filesys_temp.hpp"
#include "memory_alloc.h"
#include "environment_variable.h"
#include "system_parameter.h"
#include "critical_section.h"
#include "log_manager.h"
#include "object_representation.h"
#include "connection_error.h"
#include "log_impl.h"
#include "session.h"
#if defined(WINDOWS)
#include "wintcp.h"
#else /* WINDOWS */
#include "tcp.h"
#endif /* WINDOWS */
#include "connection_sr.h"
#include "server_support.h"
#include "thread_manager.hpp"	// for thread_get_thread_entry_info

#ifdef PACKET_TRACE
#define TRACE(string, arg)					\
	do {							\
		er_log_debug(ARG_FILE_LINE, string, arg);	\
	}							\
	while(0);
#else /* PACKET_TRACE */
#define TRACE(string, arg)
#endif /* PACKET_TRACE */

/* data wait queue */
typedef struct css_wait_queue_entry
{
  char **buffer;
  int *size;
  int *rc;
  THREAD_ENTRY *thrd_entry;	/* thread waiting for data */
  struct css_wait_queue_entry *next;
  unsigned int key;
} CSS_WAIT_QUEUE_ENTRY;

typedef struct queue_search_arg
{
  CSS_QUEUE_ENTRY *entry_ptr;
  int key;
  int remove_entry;
} CSS_QUEUE_SEARCH_ARG;

typedef struct wait_queue_search_arg
{
  CSS_WAIT_QUEUE_ENTRY *entry_ptr;
  unsigned int key;
  int remove_entry;
} CSS_WAIT_QUEUE_SEARCH_ARG;

#define NUM_NORMAL_CLIENTS (prm_get_integer_value(PRM_ID_CSS_MAX_CLIENTS))

#define RMUTEX_NAME_CONN_ENTRY "CONN_ENTRY"

static const int CSS_MAX_CLIENT_ID = INT_MAX - 1;

static int css_Client_id = 0;
static pthread_mutex_t css_Client_id_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t css_Conn_rule_lock = PTHREAD_MUTEX_INITIALIZER;
static CSS_CONN_ENTRY *css_Free_conn_anchor = NULL;
static int css_Num_free_conn = 0;
static int css_Num_max_conn = 101;	/* default max_clients + 1 for conn with master */

CSS_CONN_ENTRY *css_Conn_array = NULL;
CSS_CONN_ENTRY *css_Active_conn_anchor = NULL;
static int css_Num_active_conn = 0;

SYNC_RWLOCK css_Rwlock_active_conn_anchor;
SYNC_RWLOCK css_Rwlock_free_conn_anchor;

static LAST_ACCESS_STATUS *css_Access_status_anchor = NULL;
int css_Num_access_user = 0;

/* This will handle new connections */
css_error_code (*css_Connect_handler) (CSS_CONN_ENTRY *) = NULL;

/* This will handle new requests per connection */
CSS_THREAD_FN css_Request_handler = NULL;

/* This will handle closed connection errors */
CSS_THREAD_FN css_Connection_error_handler = NULL;

#define CSS_CONN_IDX(conn_arg) ((conn_arg) - css_Conn_array)

#define CSS_FREE_CONN_MSG "Free count = %d, head = %d"
#define CSS_FREE_CONN_ARGS css_Num_free_conn, CSS_CONN_IDX (css_Free_conn_anchor)

#define CSS_ACTIVE_CONN_MSG "Active count = %d, head = %d"
#define CSS_ACTIVE_CONN_ARGS css_Num_active_conn, CSS_CONN_IDX (css_Active_conn_anchor)

static int css_get_next_client_id (void);
static CSS_CONN_ENTRY *css_common_connect (CSS_CONN_ENTRY * conn, unsigned short *rid, const char *host_name,
					   int connect_type, const char *server_name, int server_name_length, int port);

static int css_abort_request (CSS_CONN_ENTRY * conn, unsigned short rid);
static void css_dealloc_conn (CSS_CONN_ENTRY * conn);

static unsigned int css_make_eid (unsigned short entry_id, unsigned short rid);

static CSS_QUEUE_ENTRY *css_claim_queue_entry (CSS_CONN_ENTRY * conn);
static void css_retire_queue_entry (CSS_CONN_ENTRY * conn, CSS_QUEUE_ENTRY * entry);
static void css_free_queue_entry_list (CSS_CONN_ENTRY * conn);

static CSS_WAIT_QUEUE_ENTRY *css_claim_wait_queue_entry (CSS_CONN_ENTRY * conn);
static void css_retire_wait_queue_entry (CSS_CONN_ENTRY * conn, CSS_WAIT_QUEUE_ENTRY * entry);
static void css_free_wait_queue_list (CSS_CONN_ENTRY * conn);

static NET_HEADER *css_claim_net_header_entry (CSS_CONN_ENTRY * conn);
static void css_retire_net_header_entry (CSS_CONN_ENTRY * conn, NET_HEADER * entry);
static void css_free_net_header_list (CSS_CONN_ENTRY * conn);

static CSS_QUEUE_ENTRY *css_make_queue_entry (CSS_CONN_ENTRY * conn, unsigned int key, char *buffer,
					      int size, int rc, int transid, int invalidate_snapshot, int db_error);
static void css_free_queue_entry (CSS_CONN_ENTRY * conn, CSS_QUEUE_ENTRY * entry);
static css_error_code css_add_queue_entry (CSS_CONN_ENTRY * conn, CSS_LIST * list, unsigned short request_id,
					   char *buffer, int buffer_size, int rc, int transid, int invalidate_snapshot,
					   int db_error);
static CSS_QUEUE_ENTRY *css_find_queue_entry (CSS_LIST * list, unsigned int key);
static CSS_QUEUE_ENTRY *css_find_and_remove_queue_entry (CSS_LIST * list, unsigned int key);
static CSS_WAIT_QUEUE_ENTRY *css_make_wait_queue_entry (CSS_CONN_ENTRY * conn, unsigned int key, char **buffer,
							int *size, int *rc);
static void css_free_wait_queue_entry (CSS_CONN_ENTRY * conn, CSS_WAIT_QUEUE_ENTRY * entry);
static CSS_WAIT_QUEUE_ENTRY *css_add_wait_queue_entry (CSS_CONN_ENTRY * conn, CSS_LIST * list,
						       unsigned short request_id, char **buffer, int *buffer_size,
						       int *rc);
static CSS_WAIT_QUEUE_ENTRY *css_find_and_remove_wait_queue_entry (CSS_LIST * list, unsigned int key);

static void css_process_close_packet (CSS_CONN_ENTRY * conn);
static void css_process_abort_packet (CSS_CONN_ENTRY * conn, unsigned short request_id);
static bool css_is_request_aborted (CSS_CONN_ENTRY * conn, unsigned short request_id);
static void clear_wait_queue_entry_and_free_buffer (THREAD_ENTRY * thrdp, CSS_CONN_ENTRY * conn, unsigned short rid,
						    char **bufferp);
static int css_return_queued_data_timeout (CSS_CONN_ENTRY * conn, unsigned short rid, char **buffer, int *bufsize,
					   int *rc, int waitsec);

static void css_queue_data_packet (CSS_CONN_ENTRY * conn, unsigned short request_id, const NET_HEADER * header,
				   THREAD_ENTRY ** wait_thrd);
static void css_queue_error_packet (CSS_CONN_ENTRY * conn, unsigned short request_id, const NET_HEADER * header);
static css_error_code css_queue_command_packet (CSS_CONN_ENTRY * conn, unsigned short request_id,
						const NET_HEADER * header, int size);
static bool css_is_valid_request_id (CSS_CONN_ENTRY * conn, unsigned short request_id);
static void css_remove_unexpected_packets (CSS_CONN_ENTRY * conn, unsigned short request_id);

static css_error_code css_queue_packet (CSS_CONN_ENTRY * conn, int type, unsigned short request_id,
					const NET_HEADER * header, int size);
static int css_remove_and_free_queue_entry (void *data, void *arg);
static int css_remove_and_free_wait_queue_entry (void *data, void *arg);

static int css_increment_num_conn_internal (CSS_CONN_RULE_INFO * conn_rule_info);
static void css_decrement_num_conn_internal (CSS_CONN_RULE_INFO * conn_rule_info);

/*
 * get_next_client_id() -
 *   return: client id
 */
static int
css_get_next_client_id (void)
{
  static bool overflow = false;
  int next_client_id, rv, i;
  bool retry;

  rv = pthread_mutex_lock (&css_Client_id_lock);
  if (rv != 0)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_PTHREAD_MUTEX_LOCK, 0);
      return ER_FAILED;
    }

  do
    {
      css_Client_id++;
      if (css_Client_id == CSS_MAX_CLIENT_ID)
	{
	  css_Client_id = 1;
	  overflow = true;
	}

      retry = false;
      for (i = 0; overflow && i < css_Num_max_conn; i++)
	{
	  if (css_Conn_array[i].client_id == css_Client_id)
	    {
	      retry = true;
	      break;
	    }
	}
    }
  while (retry);

  next_client_id = css_Client_id;

  rv = pthread_mutex_unlock (&css_Client_id_lock);
  if (rv != 0)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_PTHREAD_MUTEX_UNLOCK, 0);
      return ER_FAILED;
    }

  return next_client_id;
}

/*
 * css_initialize_conn() - initialize connection entry
 *   return: void
 *   conn(in):
 *   fd(in):
 */
int
css_initialize_conn (CSS_CONN_ENTRY * conn, SOCKET fd)
{
  int err;

  conn->fd = fd;
  conn->request_id = 0;
  conn->status = CONN_OPEN;
  conn->set_tran_index (NULL_TRAN_INDEX);
  conn->init_pending_request ();
  conn->invalidate_snapshot = 1;
  err = css_get_next_client_id ();
  if (err < 0)
    {
      return ER_CSS_CONN_INIT;
    }
  conn->client_id = err;
  conn->db_error = 0;
  conn->in_transaction = false;
  conn->reset_on_commit = false;
  conn->stop_talk = false;
  conn->ignore_repl_delay = false;
  conn->stop_phase = THREAD_STOP_WORKERS_EXCEPT_LOGWR;
  conn->version_string = NULL;
  /* ignore connection handler thread */
  conn->free_queue_list = NULL;
  conn->free_queue_count = 0;

  conn->free_wait_queue_list = NULL;
  conn->free_wait_queue_count = 0;

  conn->free_net_header_list = NULL;
  conn->free_net_header_count = 0;

  conn->session_id = DB_EMPTY_SESSION;
#if defined(SERVER_MODE)
  conn->session_p = NULL;
  conn->client_type = DB_CLIENT_TYPE_UNKNOWN;
#endif

  err = css_initialize_list (&conn->request_queue, 0);
  if (err != NO_ERROR)
    {
      return ER_CSS_CONN_INIT;
    }
  err = css_initialize_list (&conn->data_queue, 0);
  if (err != NO_ERROR)
    {
      return ER_CSS_CONN_INIT;
    }
  err = css_initialize_list (&conn->data_wait_queue, 0);
  if (err != NO_ERROR)
    {
      return ER_CSS_CONN_INIT;
    }
  err = css_initialize_list (&conn->abort_queue, 0);
  if (err != NO_ERROR)
    {
      return ER_CSS_CONN_INIT;
    }
  err = css_initialize_list (&conn->buffer_queue, 0);
  if (err != NO_ERROR)
    {
      return ER_CSS_CONN_INIT;
    }
  err = css_initialize_list (&conn->error_queue, 0);
  if (err != NO_ERROR)
    {
      return ER_CSS_CONN_INIT;
    }
  return NO_ERROR;
}

/*
 * css_shutdown_conn() - close connection entry
 *   return: void
 *   conn(in):
 */
void
css_shutdown_conn (CSS_CONN_ENTRY * conn)
{
  int r;

  r = rmutex_lock (NULL, &conn->rmutex);
  assert (r == NO_ERROR);

  if (!IS_INVALID_SOCKET (conn->fd))
    {
      /* if this is the PC, it also shuts down Winsock */
      css_shutdown_socket (conn->fd);
      conn->fd = INVALID_SOCKET;
    }

  if (conn->status == CONN_OPEN || conn->status == CONN_CLOSING)
    {
      conn->status = CONN_CLOSED;
      conn->stop_talk = false;
      conn->stop_phase = THREAD_STOP_WORKERS_EXCEPT_LOGWR;

      if (conn->version_string)
	{
	  free_and_init (conn->version_string);
	}

      css_remove_all_unexpected_packets (conn);

      css_finalize_list (&conn->request_queue);
      css_finalize_list (&conn->data_queue);
      css_finalize_list (&conn->data_wait_queue);
      css_finalize_list (&conn->abort_queue);
      css_finalize_list (&conn->buffer_queue);
      css_finalize_list (&conn->error_queue);
    }

  if (conn->free_queue_list != NULL)
    {
      assert (conn->free_queue_count > 0);
      css_free_queue_entry_list (conn);
    }

  if (conn->free_wait_queue_list != NULL)
    {
      assert (conn->free_wait_queue_count > 0);
      css_free_wait_queue_list (conn);
    }

  if (conn->free_net_header_list != NULL)
    {
      assert (conn->free_net_header_count > 0);
      css_free_net_header_list (conn);
    }

#if defined(SERVER_MODE)
  if (conn->session_p)
    {
      session_state_decrease_ref_count (NULL, conn->session_p);
      conn->session_p = NULL;
      conn->session_id = DB_EMPTY_SESSION;
    }
#endif

  r = rmutex_unlock (NULL, &conn->rmutex);
  assert (r == NO_ERROR);
}

/*
 * css_init_conn_list() - initialize connection list
 *   return: NO_ERROR if success, or error code
 */
int
css_init_conn_list (void)
{
  int i, err;
  CSS_CONN_ENTRY *conn;

  css_init_conn_rules ();

  css_Num_max_conn = css_get_max_conn () + NUM_MASTER_CHANNEL;

  if (css_Conn_array != NULL)
    {
      return NO_ERROR;
    }

  err = rwlock_initialize (CSS_RWLOCK_ACTIVE_CONN_ANCHOR, CSS_RWLOCK_ACTIVE_CONN_ANCHOR_NAME);
  if (err != NO_ERROR)
    {
      ASSERT_ERROR ();
      return err;
    }

  err = rwlock_initialize (CSS_RWLOCK_FREE_CONN_ANCHOR, CSS_RWLOCK_FREE_CONN_ANCHOR_NAME);
  if (err != NO_ERROR)
    {
      ASSERT_ERROR ();
      (void) rwlock_finalize (CSS_RWLOCK_ACTIVE_CONN_ANCHOR);
      return err;
    }

  /*
   * allocate NUM_MASTER_CHANNEL + the total number of
   *  conn entries
   */
  css_Conn_array = (CSS_CONN_ENTRY *) malloc (sizeof (CSS_CONN_ENTRY) * (css_Num_max_conn));
  if (css_Conn_array == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
	      sizeof (CSS_CONN_ENTRY) * (css_Num_max_conn));
      err = ER_OUT_OF_VIRTUAL_MEMORY;
      goto error;
    }

  /* initialize all CSS_CONN_ENTRY */
  for (i = 0; i < css_Num_max_conn; i++)
    {
      conn = &css_Conn_array[i];
      conn->idx = i;
      err = css_initialize_conn (conn, -1);
      if (err != NO_ERROR)
	{
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_CONN_INIT, 0);
	  err = ER_CSS_CONN_INIT;
	  goto error;
	}

      err = rmutex_initialize (&conn->rmutex, RMUTEX_NAME_CONN_ENTRY);
      if (err != NO_ERROR)
	{
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_CONN_INIT, 0);
	  err = ER_CSS_CONN_INIT;
	  goto error;
	}

      if (i < css_Num_max_conn - 1)
	{
	  conn->next = &css_Conn_array[i + 1];
	}
      else
	{
	  conn->next = NULL;
	}
    }

  /* initialize active conn list, used for stopping all threads */
  css_Active_conn_anchor = NULL;
  css_Free_conn_anchor = &css_Conn_array[0];
  css_Num_free_conn = css_Num_max_conn;

  return NO_ERROR;

error:
  (void) rwlock_finalize (CSS_RWLOCK_ACTIVE_CONN_ANCHOR);
  (void) rwlock_finalize (CSS_RWLOCK_FREE_CONN_ANCHOR);

  if (css_Conn_array != NULL)
    {
      free_and_init (css_Conn_array);
    }

  return err;
}

/*
 * css_final_conn_list() - free connection list
 *   return: void
 */
void
css_final_conn_list (void)
{
  CSS_CONN_ENTRY *conn, *next;
  int i;

  if (css_Active_conn_anchor != NULL)
    {
      for (conn = css_Active_conn_anchor; conn != NULL; conn = next)
	{
	  next = conn->next;
	  css_shutdown_conn (conn);
	  css_dealloc_conn (conn);

	  css_Num_active_conn--;
	  assert (css_Num_active_conn >= 0);
	}

      css_Active_conn_anchor = NULL;
    }

  assert (css_Num_active_conn == 0);
  assert (css_Active_conn_anchor == NULL);

  if (css_Conn_array != NULL)
    {
      for (i = 0; i < css_Num_max_conn; i++)
	{
	  conn = &css_Conn_array[i];

#if defined(SERVER_MODE)
	  assert (conn->idx == i);
#endif
	  (void) rmutex_finalize (&conn->rmutex);
	}

      free_and_init (css_Conn_array);

      (void) rwlock_finalize (CSS_RWLOCK_ACTIVE_CONN_ANCHOR);
      (void) rwlock_finalize (CSS_RWLOCK_FREE_CONN_ANCHOR);
    }
}

/*
 * css_make_conn() - make new connection entry, but not insert into active
 *                   conn list
 *   return: new connection entry
 *   fd(in): socket discriptor
 */
CSS_CONN_ENTRY *
css_make_conn (SOCKET fd)
{
  CSS_CONN_ENTRY *conn = NULL;
  int r;

  START_EXCLUSIVE_ACCESS_FREE_CONN_ANCHOR (r);

  if (css_Free_conn_anchor != NULL)
    {
      conn = css_Free_conn_anchor;
      css_Free_conn_anchor = css_Free_conn_anchor->next;
      conn->next = NULL;

      css_Num_free_conn--;
      assert (css_Num_free_conn >= 0);
    }
  CSS_LOG_STACK ("css_make_conn: conn = %d, " CSS_FREE_CONN_MSG, CSS_CONN_IDX (conn), CSS_FREE_CONN_ARGS);

  END_EXCLUSIVE_ACCESS_FREE_CONN_ANCHOR (r);

  if (conn != NULL)
    {
      if (css_initialize_conn (conn, fd) != NO_ERROR)
	{
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_CONN_INIT, 0);
	  return NULL;
	}
    }

  return conn;
}

/*
 * css_insert_into_active_conn_list() - insert/remove into/from active conn
 *                                      list. this operation must be called
 *                                      after/before css_free_conn etc.
 *   return: void
 *   conn(in): connection entry will be inserted
 */
void
css_insert_into_active_conn_list (CSS_CONN_ENTRY * conn)
{
  int r;

  START_EXCLUSIVE_ACCESS_ACTIVE_CONN_ANCHOR (r);

  CSS_LOG_STACK ("css_insert_into_active_conn_list conn = %d, prev " CSS_ACTIVE_CONN_MSG, CSS_CONN_IDX (conn),
		 CSS_ACTIVE_CONN_ARGS);

  conn->next = css_Active_conn_anchor;
  css_Active_conn_anchor = conn;

  css_Num_active_conn++;

  assert (css_Num_active_conn > 0);
  assert (css_Num_active_conn <= css_Num_max_conn);

  END_EXCLUSIVE_ACCESS_ACTIVE_CONN_ANCHOR (r);
}

/*
 * css_dealloc_conn_rmutex() - free rmutex of connection entry
 *   return: void
 *   conn(in): connection entry
 */
void
css_dealloc_conn_rmutex (CSS_CONN_ENTRY * conn)
{
  (void) rmutex_finalize (&conn->rmutex);
}

/*
 * css_dealloc_conn() - free connection entry
 *   return: void
 *   conn(in): connection entry will be free
 */
static void
css_dealloc_conn (CSS_CONN_ENTRY * conn)
{
  int r;

  START_EXCLUSIVE_ACCESS_FREE_CONN_ANCHOR (r);

  CSS_LOG_STACK ("css_dealloc_conn conn = %d, prev " CSS_FREE_CONN_MSG, CSS_CONN_IDX (conn), CSS_FREE_CONN_ARGS);

  conn->next = css_Free_conn_anchor;
  css_Free_conn_anchor = conn;

  css_Num_free_conn++;

  assert (css_Num_free_conn > 0);
  assert (css_Num_free_conn <= css_Num_max_conn);

  END_EXCLUSIVE_ACCESS_FREE_CONN_ANCHOR (r);
}

/*
 * css_get_num_free_conn -
 */
int
css_get_num_free_conn (void)
{
  return css_Num_free_conn;
}

/*
 * css_increment_num_conn_internal() - increments conn counter
 *   based on client type
 *   return: error code
 *   client_type(in): a type of a client trying
 *   to release the connection
 */
static int
css_increment_num_conn_internal (CSS_CONN_RULE_INFO * conn_rule_info)
{
  int error = NO_ERROR;

  switch (conn_rule_info->rule)
    {
    case CR_NORMAL_ONLY:
      if (conn_rule_info->num_curr_conn == conn_rule_info->max_num_conn)
	{
	  error = ER_CSS_CLIENTS_EXCEEDED;
	}
      else
	{
	  conn_rule_info->num_curr_conn++;
	}
      break;
    case CR_NORMAL_FIRST:
      /* tries to use a normal conn first */
      if (css_increment_num_conn_internal (&css_Conn_rules[CSS_CR_NORMAL_ONLY_IDX]) != NO_ERROR)
	{
	  /* if normal conns are all occupied, uses a reserved conn */
	  if (conn_rule_info->num_curr_conn == conn_rule_info->max_num_conn)
	    {
	      error = ER_CSS_CLIENTS_EXCEEDED;
	    }
	  else
	    {
	      conn_rule_info->num_curr_conn++;
	      assert (conn_rule_info->num_curr_conn <= conn_rule_info->max_num_conn);
	    }
	}
      break;
    case CR_RESERVED_FIRST:
      /* tries to use a reserved conn first */
      if (conn_rule_info->num_curr_conn < conn_rule_info->max_num_conn)
	{
	  conn_rule_info->num_curr_conn++;
	}
      else			/* uses a normal conn if no reserved conn is available */
	{
	  if (css_increment_num_conn_internal (&css_Conn_rules[CSS_CR_NORMAL_ONLY_IDX]) != NO_ERROR)
	    {
	      error = ER_CSS_CLIENTS_EXCEEDED;
	    }
	  else
	    {
	      /* also increments its own conn counter */
	      conn_rule_info->num_curr_conn++;
	      assert (conn_rule_info->num_curr_conn <=
		      (css_Conn_rules[CSS_CR_NORMAL_ONLY_IDX].max_num_conn + conn_rule_info->max_num_conn));
	    }
	}
      break;
    default:
      assert (false);
      break;
    }

  return error;
}

/*
 * css_decrement_num_conn_internal() - decrements conn counter
 *   based on client type
 *   return:
 *   client_type(in): a type of a client trying
 *   to release the connection
 */
static void
css_decrement_num_conn_internal (CSS_CONN_RULE_INFO * conn_rule_info)
{
  int i;

  switch (conn_rule_info->rule)
    {
    case CR_NORMAL_ONLY:
      /* When a normal client decrements the counter, it should first check that other normal-first-reserved-last
       * clients need to take the released connection first. */
      for (i = 1; i < css_Conn_rules_size; i++)
	{
	  if (css_Conn_rules[i].rule == CR_NORMAL_FIRST && css_Conn_rules[i].num_curr_conn > 0)
	    {
	      css_Conn_rules[i].num_curr_conn--;

	      return;
	    }
	}
      conn_rule_info->num_curr_conn--;
      break;

    case CR_NORMAL_FIRST:
      /* decrements reserved conn counter first if exists */
      if (conn_rule_info->num_curr_conn > 0)
	{
	  conn_rule_info->num_curr_conn--;
	}
      else			/* decrements normal conn counter if no reserved conn is in use */
	{
	  css_decrement_num_conn_internal (&css_Conn_rules[CSS_CR_NORMAL_ONLY_IDX]);
	}
      break;

    case CR_RESERVED_FIRST:
      /* decrements normal conn counter if exists */
      if (conn_rule_info->num_curr_conn > conn_rule_info->max_num_conn)
	{
	  css_decrement_num_conn_internal (&css_Conn_rules[CSS_CR_NORMAL_ONLY_IDX]);
	}
      /* also decrements its own conn counter */
      conn_rule_info->num_curr_conn--;
      break;

    default:
      assert (false);
      break;
    }

  assert (conn_rule_info->num_curr_conn >= 0);

  return;
}

/*
 * css_increment_num_conn() - increment a connection counter
 * and check if a client can take its connection
 *   return: error code
 *   client_type(in): a type of a client trying
 *   to take the connection
 */
int
css_increment_num_conn (BOOT_CLIENT_TYPE client_type)
{
  int i;
  int error = NO_ERROR;

  for (i = 0; i < css_Conn_rules_size; i++)
    {
      if (css_Conn_rules[i].check_client_type_fn (client_type))
	{
	  pthread_mutex_lock (&css_Conn_rule_lock);
	  error = css_increment_num_conn_internal (&css_Conn_rules[i]);
	  pthread_mutex_unlock (&css_Conn_rule_lock);
	  break;
	}
    }

  return error;
}

/*
 * css_decrement_num_conn() - decrement a connection counter
 *   return:
 *   client_type(in): a type of a client trying
 *   to release the connection
 */
void
css_decrement_num_conn (BOOT_CLIENT_TYPE client_type)
{
  int i;

  if (client_type == DB_CLIENT_TYPE_UNKNOWN)
    {
      return;
    }

  for (i = 0; i < css_Conn_rules_size; i++)
    {
      if (css_Conn_rules[i].check_client_type_fn (client_type))
	{
	  pthread_mutex_lock (&css_Conn_rule_lock);
	  css_decrement_num_conn_internal (&css_Conn_rules[i]);
	  pthread_mutex_unlock (&css_Conn_rule_lock);
	  break;
	}
    }

  return;
}

/*
 * css_free_conn() - destroy all connection related structures, and free conn
 *                   entry, delete from css_Active_conn_anchor list
 *   return: void
 *   conn(in): connection entry will be free
 */
void
css_free_conn (CSS_CONN_ENTRY * conn)
{
  CSS_CONN_ENTRY *p, *prev = NULL, *next;
  int r;

  START_EXCLUSIVE_ACCESS_ACTIVE_CONN_ANCHOR (r);

  /* find and remove from active conn list */
  for (p = css_Active_conn_anchor; p != NULL; p = next)
    {
      next = p->next;

      if (p == conn)
	{
	  if (prev == NULL)
	    {
	      css_Active_conn_anchor = next;
	    }
	  else
	    {
	      prev->next = next;
	    }

	  css_Num_active_conn--;

	  assert (css_Num_active_conn >= 0);
	  assert (css_Num_active_conn < css_Num_max_conn);

	  CSS_LOG_STACK ("css_free_conn - removed conn = %d from " CSS_ACTIVE_CONN_MSG, CSS_CONN_IDX (conn),
			 CSS_ACTIVE_CONN_ARGS);

	  break;
	}

      prev = p;
    }

  if (p == NULL)
    {
      CSS_LOG_STACK ("css_free_conn - not found conn = %p in " CSS_ACTIVE_CONN_MSG, conn, CSS_ACTIVE_CONN_ARGS);
    }

  css_shutdown_conn (conn);
  css_dealloc_conn (conn);
  css_decrement_num_conn (conn->client_type);

  END_EXCLUSIVE_ACCESS_ACTIVE_CONN_ANCHOR (r);
}

/*
 * css_print_conn_entry_info() - print connection entry information to stderr
 *   return: void
 *   conn(in): connection entry
 */
void
css_print_conn_entry_info (CSS_CONN_ENTRY * conn)
{
  fprintf (stderr,
	   "CONN_ENTRY: %p, next(%p), idx(%d),fd(%lld),request_id(%d),transaction_id(%d),client_id(%d)\n",
	   conn, conn->next, conn->idx, (long long) conn->fd, conn->request_id, conn->get_tran_index (),
	   conn->client_id);
}

/*
 * css_print_conn_list() - print active connection list to stderr
 *   return: void
 */
void
css_print_conn_list (void)
{
  CSS_CONN_ENTRY *conn, *next;
  int i, r;

  if (css_Active_conn_anchor != NULL)
    {
      START_SHARED_ACCESS_ACTIVE_CONN_ANCHOR (r);

      fprintf (stderr, "active conn list (%d)\n", css_Num_active_conn);

      for (conn = css_Active_conn_anchor, i = 0; conn != NULL; conn = next, i++)
	{
	  next = conn->next;
	  css_print_conn_entry_info (conn);
	}

      assert (i == css_Num_active_conn);

      END_SHARED_ACCESS_ACTIVE_CONN_ANCHOR (r);
    }
}

/*
 * css_print_free_conn_list() - print free connection list to stderr
 *   return: void
 */
void
css_print_free_conn_list (void)
{
  CSS_CONN_ENTRY *conn, *next;
  int i, r;

  if (css_Free_conn_anchor != NULL)
    {
      START_SHARED_ACCESS_FREE_CONN_ANCHOR (r);

      fprintf (stderr, "free conn list (%d)\n", css_Num_free_conn);

      for (conn = css_Free_conn_anchor, i = 0; conn != NULL; conn = next, i++)
	{
	  next = conn->next;
	  css_print_conn_entry_info (conn);
	}

      assert (i == css_Num_free_conn);

      END_SHARED_ACCESS_FREE_CONN_ANCHOR (r);
    }
}

/*
 * css_register_handler_routines() - enroll handler routines
 *   return: void
 *   connect_handler(in): connection handler function pointer
 *   conn(in): connection entry
 *   request_handler(in): request handler function pointer
 *   connection_error_handler(in): error handler function pointer
 *
 * Note: This is the routine that will enroll various handler routines
 *       that the client/server interface software may use. Any of these
 *       routines may be given a NULL value in which case a default routine
 *       will be used, or nothing will be done.
 *
 *       The connect handler is called when a new connection is made.
 *
 *       The request handler is called to handle a new request. This must
 *       return non zero, otherwise, the server will halt.
 *
 *       The abort handler is called by the server when an abort command
 *       is sent from the client.
 *
 *       The alloc function is called instead of malloc when new buffers
 *       are to be created.
 *
 *       The free function is called when a buffer is to be released.
 *
 *       The error handler function is called when the client/server system
 *       detects an error it considers to be fatal.
 */
void
css_register_handler_routines (css_error_code (*connect_handler) (CSS_CONN_ENTRY * conn),
			       CSS_THREAD_FN request_handler, CSS_THREAD_FN connection_error_handler)
{
  css_Connect_handler = connect_handler;
  css_Request_handler = request_handler;

  if (connection_error_handler)
    {
      css_Connection_error_handler = connection_error_handler;
    }
}

/*
 * css_common_connect() - actually try to make a connection to a server.
 *   return: connection entry if success, or NULL
 *   conn(in): connection entry will be connected
 *   rid(out): request id
 *   host_name(in): host name of server
 *   connect_type(in):
 *   server_name(in):
 *   server_name_length(in):
 *   port(in):
 */
static CSS_CONN_ENTRY *
css_common_connect (CSS_CONN_ENTRY * conn, unsigned short *rid,
		    const char *host_name, int connect_type, const char *server_name, int server_name_length, int port)
{
  SOCKET fd;

  fd = css_tcp_client_open ((char *) host_name, port);

  if (!IS_INVALID_SOCKET (fd))
    {
      conn->fd = fd;

      if (css_send_magic (conn) != NO_ERRORS)
	{
	  return NULL;
	}

      if (css_send_request (conn, connect_type, rid, server_name, server_name_length) == NO_ERRORS)
	{
	  return conn;
	}
    }

  return NULL;
}

/*
 * css_connect_to_master_server() - Connect to the master from the server.
 *   return: connection entry if success, or NULL
 *   master_port_id(in):
 *   server_name(in): name + version
 *   name_length(in):
 */
CSS_CONN_ENTRY *
css_connect_to_master_server (int master_port_id, const char *server_name, int name_length)
{
  char hname[CUB_MAXHOSTNAMELEN];
  CSS_CONN_ENTRY *conn;
  unsigned short rid;
  int response, response_buff;
  int server_port_id;
  int connection_protocol;
#if !defined(WINDOWS)
  std::string pname;
  int datagram_fd, socket_fd;
#endif

  css_Service_id = master_port_id;
  if (GETHOSTNAME (hname, CUB_MAXHOSTNAMELEN) != 0)
    {
      return NULL;
    }

  conn = css_make_conn (0);
  if (conn == NULL)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ERR_CSS_ERROR_DURING_SERVER_CONNECT, 1, server_name);
      return NULL;
    }

  /* select the connection protocol */
  if (css_Server_use_new_connection_protocol)
    {
      // Windows
      connection_protocol = SERVER_REQUEST_NEW;
    }
  else
    {
      // Linux and Unix
      connection_protocol = SERVER_REQUEST;
    }

  if (css_common_connect (conn, &rid, hname, connection_protocol, server_name, name_length, master_port_id) == NULL)
    {
      goto fail_end;
    }

  if (css_readn (conn->fd, (char *) &response_buff, sizeof (int), -1) != sizeof (int))
    {
      goto fail_end;
    }

  response = ntohl (response_buff);
  TRACE ("css_connect_to_master_server received %d as response from master\n", response);

  switch (response)
    {
    case SERVER_ALREADY_EXISTS:
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ERR_CSS_SERVER_ALREADY_EXISTS, 1, server_name);
      goto fail_end;

    case SERVER_REQUEST_ACCEPTED_NEW:
      /*
       * Master requests a new-style connect, must go get
       * our port id and set up our connection socket.
       * For drivers, we don't need a connection socket and we
       * don't want to allocate a bunch of them.  Let a flag variable
       * control whether or not we actually create one of these.
       */
      if (css_Server_inhibit_connection_socket)
	{
	  server_port_id = -1;
	}
      else
	{
	  server_port_id = css_open_server_connection_socket ();
	}

      response = htonl (server_port_id);
      css_net_send (conn, (char *) &response, sizeof (int), -1);
      /* this connection remains our only contact with the master */
      return conn;

    case SERVER_REQUEST_ACCEPTED:
#if defined(WINDOWS)
      /* PC's can't handle this style of connection at all */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ERR_CSS_ERROR_DURING_SERVER_CONNECT, 1, server_name);
      goto fail_end;
#else /* WINDOWS */
      /* send the "pathname" for the datagram */
      /* be sure to open the datagram first.  */
      pname = std::filesystem::temp_directory_path ();
      pname += "/cubrid_tcp_setup_server" + std::to_string (getpid ());
      (void) unlink (pname.c_str ());	// make sure file is deleted

      if (!css_tcp_setup_server_datagram (pname.c_str (), &socket_fd))
	{
	  (void) unlink (pname.c_str ());
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ERR_CSS_ERROR_DURING_SERVER_CONNECT, 1, server_name);
	  goto fail_end;
	}
      if (css_send_data (conn, rid, pname.c_str (), pname.length () + 1) != NO_ERRORS)
	{
	  (void) unlink (pname.c_str ());
	  close (socket_fd);
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ERR_CSS_ERROR_DURING_SERVER_CONNECT, 1, server_name);
	  goto fail_end;
	}
      if (!css_tcp_listen_server_datagram (socket_fd, &datagram_fd))
	{
	  (void) unlink (pname.c_str ());
	  close (socket_fd);
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ERR_CSS_ERROR_DURING_SERVER_CONNECT, 1, server_name);
	  goto fail_end;
	}
      // success
      (void) unlink (pname.c_str ());
      css_free_conn (conn);
      close (socket_fd);
      return (css_make_conn (datagram_fd));
#endif /* WINDOWS */
    }

fail_end:
  css_free_conn (conn);
  return NULL;
}

/*
 * css_find_conn_by_tran_index() - find connection entry having given
 *                                 transaction id
 *   return: connection entry if find, or NULL
 *   tran_index(in): transaction id
 */
CSS_CONN_ENTRY *
css_find_conn_by_tran_index (int tran_index)
{
  CSS_CONN_ENTRY *conn = NULL, *next;
  int r;

  if (css_Active_conn_anchor != NULL)
    {
      START_SHARED_ACCESS_ACTIVE_CONN_ANCHOR (r);

      for (conn = css_Active_conn_anchor; conn != NULL; conn = next)
	{
	  next = conn->next;
	  if (conn->get_tran_index () == tran_index)
	    {
	      break;
	    }
	}

      END_SHARED_ACCESS_ACTIVE_CONN_ANCHOR (r);
    }

  return conn;
}

/*
 * css_find_conn_from_fd() - find a connection having given socket fd.
 *   return: connection entry if find, or NULL
 *   fd(in): socket fd
 */
CSS_CONN_ENTRY *
css_find_conn_from_fd (SOCKET fd)
{
  CSS_CONN_ENTRY *conn = NULL, *next;
  int r;

  if (css_Active_conn_anchor != NULL)
    {
      START_SHARED_ACCESS_ACTIVE_CONN_ANCHOR (r);

      for (conn = css_Active_conn_anchor; conn != NULL; conn = next)
	{
	  next = conn->next;
	  if (conn->fd == fd)
	    {
	      break;
	    }
	}

      END_SHARED_ACCESS_ACTIVE_CONN_ANCHOR (r);
    }
  return conn;
}

/*
 * css_get_session_ids_for_active_connections () - get active session ids
 * return : error code or NO_ERROR
 * session_ids (out)  : holder for session ids
 * count (out)	      : number of session ids
 */
int
css_get_session_ids_for_active_connections (SESSION_ID ** session_ids, int *count)
{
  CSS_CONN_ENTRY *conn = NULL, *next = NULL;
  SESSION_ID *sessions_p = NULL;
  int error = NO_ERROR, i = 0, r;

  assert (count != NULL);
  if (count == NULL)
    {
      error = ER_FAILED;
      goto error_return;
    }

  if (css_Active_conn_anchor == NULL)
    {
      *session_ids = NULL;
      *count = 0;
      return NO_ERROR;
    }

  START_SHARED_ACCESS_ACTIVE_CONN_ANCHOR (r);
  *count = css_Num_active_conn;
  sessions_p = (SESSION_ID *) malloc (css_Num_active_conn * sizeof (SESSION_ID));

  if (sessions_p == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, css_Num_active_conn * sizeof (SESSION_ID));
      error = ER_FAILED;
      END_SHARED_ACCESS_ACTIVE_CONN_ANCHOR (r);
      goto error_return;
    }

  for (conn = css_Active_conn_anchor; conn != NULL; conn = next)
    {
      next = conn->next;
      sessions_p[i] = conn->session_id;
      i++;
    }

  END_SHARED_ACCESS_ACTIVE_CONN_ANCHOR (r);
  *session_ids = sessions_p;
  return error;

error_return:
  if (sessions_p != NULL)
    {
      free_and_init (sessions_p);
    }

  *session_ids = NULL;

  if (count != NULL)
    {
      *count = 0;
    }

  return error;
}

/*
 * css_shutdown_conn_by_tran_index() - shutdown connection having given
 *                                     transaction id
 *   return: void
 *   tran_index(in): transaction id
 */
void
css_shutdown_conn_by_tran_index (int tran_index)
{
  CSS_CONN_ENTRY *conn = NULL;
  int r;

  if (css_Active_conn_anchor != NULL)
    {
      START_EXCLUSIVE_ACCESS_ACTIVE_CONN_ANCHOR (r);

      for (conn = css_Active_conn_anchor; conn != NULL; conn = conn->next)
	{
	  if (conn->get_tran_index () == tran_index)
	    {
	      if (conn->status == CONN_OPEN)
		{
		  conn->status = CONN_CLOSING;
		}
	      break;
	    }
	}

      END_EXCLUSIVE_ACCESS_ACTIVE_CONN_ANCHOR (r);
    }
}

/*
 * css_get_request_id() - return the next valid request id
 *   return: request id
 *   conn(in): connection entry
 */
unsigned short
css_get_request_id (CSS_CONN_ENTRY * conn)
{
  unsigned short old_rid;
  unsigned short request_id;
  int r;

  r = rmutex_lock (NULL, &conn->rmutex);
  assert (r == NO_ERROR);

  old_rid = conn->request_id++;
  if (conn->request_id == 0)
    {
      conn->request_id++;
    }

  while (conn->request_id != old_rid)
    {
      if (css_is_valid_request_id (conn, conn->request_id))
	{
	  request_id = conn->request_id;

	  r = rmutex_unlock (NULL, &conn->rmutex);
	  assert (r == NO_ERROR);

	  return (request_id);
	}
      else
	{
	  conn->request_id++;
	  if (conn->request_id == 0)
	    {
	      conn->request_id++;
	    }
	}
    }

  r = rmutex_unlock (NULL, &conn->rmutex);
  assert (r == NO_ERROR);

  /* Should never reach this point */
  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ERR_CSS_REQUEST_ID_FAILURE, 0);
  return (0);
}

/*
 * css_abort_request() - helper routine to actually send the abort request.
 *   return:  0 if success, or error code
 *   conn(in): connection entry
 *   rid(in): request id
 */
static int
css_abort_request (CSS_CONN_ENTRY * conn, unsigned short rid)
{
  NET_HEADER header = DEFAULT_HEADER_DATA;
  unsigned short flags = 0;

  header.type = htonl (ABORT_TYPE);
  header.request_id = htonl (rid);
  header.transaction_id = htonl (conn->get_tran_index ());

  if (conn->invalidate_snapshot)
    {
      flags |= NET_HEADER_FLAG_INVALIDATE_SNAPSHOT;
    }
  header.flags = htons (flags);
  header.db_error = htonl (conn->db_error);

  /* timeout in milli-second in css_net_send() */
  return css_net_send (conn, (char *) &header, sizeof (NET_HEADER), -1);
}

/*
 * css_send_abort_request() - abort an outstanding request.
 *   return:  0 if success, or error code
 *   conn(in): connection entry
 *   request_id(in): request id
 *
 * Note: Once this is issued, any queued data buffers for this command will be
 *       released.
 */
int
css_send_abort_request (CSS_CONN_ENTRY * conn, unsigned short request_id)
{
  int rc, r;

  if (!conn || conn->status != CONN_OPEN)
    {
      return CONNECTION_CLOSED;
    }

  r = rmutex_lock (NULL, &conn->rmutex);
  assert (r == NO_ERROR);

  css_remove_unexpected_packets (conn, request_id);
  rc = css_abort_request (conn, request_id);

  r = rmutex_unlock (NULL, &conn->rmutex);
  assert (r == NO_ERROR);

  return rc;
}

/*
 * css_read_header() - helper routine that will read a header from the socket.
 *   return: 0 if success, or error code
 *   conn(in): connection entry
 *   local_header(in):
 *
 * Note: It is a blocking read.
 */
int
css_read_header (CSS_CONN_ENTRY * conn, const NET_HEADER * local_header)
{
  int buffer_size;
  int rc = 0;
  unsigned short flags = 0;

  buffer_size = sizeof (NET_HEADER);

  if (conn->stop_talk == true)
    {
      return CONNECTION_CLOSED;
    }

  rc = css_net_read_header (conn->fd, (char *) local_header, &buffer_size, -1);
  if (rc == NO_ERRORS && ntohl (local_header->type) == CLOSE_TYPE)
    {
      return CONNECTION_CLOSED;
    }
  if (rc != NO_ERRORS && rc != RECORD_TRUNCATED)
    {
      return CONNECTION_CLOSED;
    }

  conn->set_tran_index (ntohl (local_header->transaction_id));
  conn->db_error = (int) ntohl (local_header->db_error);

  flags = ntohs (local_header->flags);
  conn->invalidate_snapshot = flags | NET_HEADER_FLAG_INVALIDATE_SNAPSHOT ? 1 : 0;

  return rc;
}

/*
 * css_receive_request() - receive request from client
 *   return: 0 if success, or error code
 *   conn(in): connection entry
 *   rid(out): request id
 *   request(out): request
 *   buffer_size(out): request data size
 */
int
css_receive_request (CSS_CONN_ENTRY * conn, unsigned short *rid, int *request, int *buffer_size)
{
  return css_return_queued_request (conn, rid, request, buffer_size);
}

/*
 * css_read_and_queue() - Attempt to read any data packet from the connection.
 *   return: 0 if success, or error code
 *   conn(in): connection entry
 *   type(out): request type
 */
int
css_read_and_queue (CSS_CONN_ENTRY * conn, int *type)
{
  int rc;
  NET_HEADER header = DEFAULT_HEADER_DATA;

  if (!conn || conn->status != CONN_OPEN)
    {
      return ERROR_ON_READ;
    }

  rc = css_read_header (conn, &header);

  if (conn->stop_talk == true)
    {
      return CONNECTION_CLOSED;
    }

  if (rc != NO_ERRORS)
    {
      return rc;
    }

  *type = ntohl (header.type);
  rc = css_queue_packet (conn, (int) ntohl (header.type), (unsigned short) ntohl (header.request_id), &header,
			 sizeof (NET_HEADER));
  return rc;
}

/*
 * css_receive_data() - receive a data for an associated request.
 *   return: 0 if success, or error code
 *   conn(in): connection entry
 *   req_id(in): request id
 *   buffer(out): buffer for data
 *   buffer_size(out): buffer size
 *   timeout(in):
 *
 * Note: this is a blocking read.
 */
int
css_receive_data (CSS_CONN_ENTRY * conn, unsigned short req_id, char **buffer, int *buffer_size, int timeout)
{
  int *r, rc;

  /* at here, do not use stack variable; must alloc it */
  r = (int *) malloc (sizeof (int));
  if (r == NULL)
    {
      return NO_DATA_AVAILABLE;
    }

  css_return_queued_data_timeout (conn, req_id, buffer, buffer_size, r, timeout);
  rc = *r;

  free_and_init (r);
  return rc;
}

/*
 * css_return_eid_from_conn() - get enquiry id from connection entry
 *   return: enquiry id
 *   conn(in): connection entry
 *   rid(in): request id
 */
unsigned int
css_return_eid_from_conn (CSS_CONN_ENTRY * conn, unsigned short rid)
{
  return css_make_eid ((unsigned short) conn->idx, rid);
}

/*
 * css_make_eid() - make enquiry id
 *   return: enquiry id
 *   entry_id(in): connection entry id
 *   rid(in): request id
 */
static unsigned int
css_make_eid (unsigned short entry_id, unsigned short rid)
{
  int top;

  top = entry_id;
  return ((top << 16) | rid);
}

/* CSS_CONN_ENTRY's queues related functions */

/*
 * css_claim_queue_entry() - claim a queue entry from free list.
 *   return: CSS_QUEUE_ENTRY *
 *   conn(in): connection entry
 */
static CSS_QUEUE_ENTRY *
css_claim_queue_entry (CSS_CONN_ENTRY * conn)
{
  CSS_QUEUE_ENTRY *p;

  assert (conn != NULL);

  p = conn->free_queue_list;
  if (p == NULL)
    {
      return NULL;
    }

  conn->free_queue_list = p->next;

  conn->free_queue_count--;
  assert (0 <= conn->free_queue_count);

  p->next = NULL;

  return p;
}

/*
 * css_retire_queue_entry() - retire a queue entry to free list.
 *   return: void
 *   conn(in): connection entry
 *   entry(in): CSS_QUEUE_ENTRY * to be retired
 */
static void
css_retire_queue_entry (CSS_CONN_ENTRY * conn, CSS_QUEUE_ENTRY * entry)
{
  assert (conn != NULL && entry != NULL);

  entry->next = conn->free_queue_list;
  conn->free_queue_list = entry;

  conn->free_queue_count++;
  assert (0 < conn->free_queue_count);
}

/*
 * css_free_queue_entry_list() - free all entries of free queue list
 *   return: void
 *   conn(in): connection entry
 */
static void
css_free_queue_entry_list (CSS_CONN_ENTRY * conn)
{
  CSS_QUEUE_ENTRY *p;

  assert (conn != NULL);

  while (conn->free_queue_list != NULL)
    {
      p = conn->free_queue_list;
      conn->free_queue_list = p->next;

      free (p);
      conn->free_queue_count--;
    }

  conn->free_queue_list = NULL;
  assert (conn->free_queue_count == 0);
}

/*
 * css_claim_wait_queue_entry() - claim a wait queue entry from free list.
 *   return: CSS_WAIT_QUEUE_ENTRY *
 *   conn(in): connection entry
 */
static CSS_WAIT_QUEUE_ENTRY *
css_claim_wait_queue_entry (CSS_CONN_ENTRY * conn)
{
  CSS_WAIT_QUEUE_ENTRY *p;

  assert (conn != NULL);

  p = conn->free_wait_queue_list;
  if (p == NULL)
    {
      return NULL;
    }

  conn->free_wait_queue_list = p->next;

  conn->free_wait_queue_count--;
  assert (0 <= conn->free_wait_queue_count);

  p->next = NULL;

  return p;
}

/*
 * css_retire_wait_queue_entry() - retire a wait_queue entry to free list.
 *   return: void
 *   conn(in): connection entry
 *   entry(in): CSS_WAIT_QUEUE_ENTRY * to be retired
 */
static void
css_retire_wait_queue_entry (CSS_CONN_ENTRY * conn, CSS_WAIT_QUEUE_ENTRY * entry)
{
  assert (conn != NULL && entry != NULL);

  entry->next = conn->free_wait_queue_list;
  conn->free_wait_queue_list = entry;

  conn->free_wait_queue_count++;
  assert (0 < conn->free_wait_queue_count);
}

/*
 * css_free_wait_queue_list() - free all entries of free wait queue list
 *   return: void
 *   conn(in): connection entry
 */
static void
css_free_wait_queue_list (CSS_CONN_ENTRY * conn)
{
  CSS_WAIT_QUEUE_ENTRY *p;

  assert (conn != NULL);

  while (conn->free_wait_queue_list != NULL)
    {
      p = conn->free_wait_queue_list;
      conn->free_wait_queue_list = p->next;

      free (p);
      conn->free_wait_queue_count--;
    }

  conn->free_wait_queue_list = NULL;
  assert (conn->free_wait_queue_count == 0);
}

/*
 * css_claim_net_header_entry() - claim a net header entry from free list.
 *   return: NET_HEADER *
 *   conn(in): connection entry
 *
 * TODO - rewrite this to avoid ugly
 */
static NET_HEADER *
css_claim_net_header_entry (CSS_CONN_ENTRY * conn)
{
  NET_HEADER *p;

  assert (conn != NULL);

  p = (NET_HEADER *) conn->free_net_header_list;
  if (p == NULL)
    {
      return NULL;
    }

  conn->free_net_header_list = (char *) (*(UINTPTR *) p);

  conn->free_net_header_count--;
  assert (0 <= conn->free_net_header_count);

  return p;
}

/*
 * css_retire_net_header_entry() - retire a net header entry to free list.
 *   return: void
 *   conn(in): connection entry
 *   entry(in): NET_HEADER * to be retired
 */
static void
css_retire_net_header_entry (CSS_CONN_ENTRY * conn, NET_HEADER * entry)
{
  assert (conn != NULL && entry != NULL);

  *(UINTPTR *) entry = (UINTPTR) conn->free_net_header_list;
  conn->free_net_header_list = (char *) entry;

  conn->free_net_header_count++;
  assert (0 < conn->free_net_header_count);
}

/*
 * css_free_net_header_list() - free all entries of free net header list
 *   return: void
 *   conn(in): connection entry
 */
static void
css_free_net_header_list (CSS_CONN_ENTRY * conn)
{
  char *p;

  assert (conn != NULL);

  while (conn->free_net_header_list != NULL)
    {
      p = conn->free_net_header_list;

      conn->free_net_header_list = (char *) (*(UINTPTR *) p);
      conn->free_net_header_count--;

      free (p);
    }

  conn->free_net_header_list = NULL;
  assert (conn->free_net_header_count == 0);
}

/*
 * css_make_queue_entry() - make queue entey
 *   return: queue entry
 *   conn(in): connection entry
 *   key(in):
 *   buffer(in):
 *   size(in):
 *   rc(in):
 *   transid(in):
 *   db_error(in):
 */
static CSS_QUEUE_ENTRY *
css_make_queue_entry (CSS_CONN_ENTRY * conn, unsigned int key, char *buffer,
		      int size, int rc, int transid, int invalidate_snapshot, int db_error)
{
  CSS_QUEUE_ENTRY *p;

  if (conn->free_queue_list != NULL)
    {
      p = css_claim_queue_entry (conn);
    }
  else
    {
      p = (CSS_QUEUE_ENTRY *) malloc (sizeof (CSS_QUEUE_ENTRY));
    }

  if (p == NULL)
    {
      return NULL;
    }

  p->key = key;
  p->buffer = buffer;
  p->size = size;
  p->rc = rc;
  p->transaction_id = transid;
  p->invalidate_snapshot = invalidate_snapshot;
  p->db_error = db_error;

  return p;
}

/*
 * css_free_queue_entry() - free queue entry
 *   return: void
 *   conn(in): connection entry
 *   entry(in): queue entry
 */
static void
css_free_queue_entry (CSS_CONN_ENTRY * conn, CSS_QUEUE_ENTRY * entry)
{
  if (entry == NULL)
    {
      return;
    }

  if (entry->buffer != NULL)
    {
      free_and_init (entry->buffer);
    }

  css_retire_queue_entry (conn, entry);
}

/*
 * css_add_queue_entry() - add queue entry
 *   return: 0 if success, or error code
 *   conn(in): connection entry
 *   list(in): queue list
 *   request_id(in): request id
 *   buffer(in):
 *   buffer_size(in):
 *   rc(in):
 *   transid(in):
 *   db_error(in):
 */
static css_error_code
css_add_queue_entry (CSS_CONN_ENTRY * conn, CSS_LIST * list, unsigned short request_id, char *buffer, int buffer_size,
		     int rc, int transid, int invalidate_snapshot, int db_error)
{
  CSS_QUEUE_ENTRY *p;
  int r;

  p = css_make_queue_entry (conn, request_id, buffer, buffer_size, rc, transid, invalidate_snapshot, db_error);
  if (p == NULL)
    {
      return CANT_ALLOC_BUFFER;
    }

  r = css_add_list (list, p);
  if (r != NO_ERROR)
    {
      css_retire_queue_entry (conn, p);
      return CANT_ALLOC_BUFFER;
    }

  return NO_ERRORS;
}

/*
 * css_find_queue_entry_by_key() - find queue entry using key
 *   return: status of traverse
 *   data(in): queue entry
 *   user(in): search argument
 */
static int
css_find_queue_entry_by_key (void *data, void *user)
{
  CSS_QUEUE_SEARCH_ARG *arg = (CSS_QUEUE_SEARCH_ARG *) user;
  CSS_QUEUE_ENTRY *p = (CSS_QUEUE_ENTRY *) data;

  if (p->key == arg->key)
    {
      arg->entry_ptr = p;
      if (arg->remove_entry)
	{
	  return TRAV_STOP_DELETE;
	}
      else
	{
	  return TRAV_STOP;
	}
    }

  return TRAV_CONT;
}

/*
 * css_find_queue_entry() - find queue entry
 *   return: queue entry
 *   list(in): queue list
 *   key(in): key
 */
static CSS_QUEUE_ENTRY *
css_find_queue_entry (CSS_LIST * list, unsigned int key)
{
  CSS_QUEUE_SEARCH_ARG arg;

  arg.entry_ptr = NULL;
  arg.key = key;
  arg.remove_entry = 0;

  css_traverse_list (list, css_find_queue_entry_by_key, &arg);

  return arg.entry_ptr;
}

/*
 * css_find_and_remove_queue_entry() - find queue entry and remove it
 *   return: queue entry
 *   list(in): queue list
 *   key(in): key
 */
static CSS_QUEUE_ENTRY *
css_find_and_remove_queue_entry (CSS_LIST * list, unsigned int key)
{
  CSS_QUEUE_SEARCH_ARG arg;

  arg.entry_ptr = NULL;
  arg.key = key;
  arg.remove_entry = 1;

  css_traverse_list (list, css_find_queue_entry_by_key, &arg);

  return arg.entry_ptr;
}

/*
 * css_make_wait_queue_entry() - make wait queue entry
 *   return: wait queue entry
 *   conn(in): connection entry
 *   key(in):
 *   buffer(out):
 *   size(out):
 *   rc(out):
 */
static CSS_WAIT_QUEUE_ENTRY *
css_make_wait_queue_entry (CSS_CONN_ENTRY * conn, unsigned int key, char **buffer, int *size, int *rc)
{
  CSS_WAIT_QUEUE_ENTRY *p;

  if (conn->free_wait_queue_list != NULL)
    {
      p = css_claim_wait_queue_entry (conn);
    }
  else
    {
      p = (CSS_WAIT_QUEUE_ENTRY *) malloc (sizeof (CSS_WAIT_QUEUE_ENTRY));
    }

  if (p == NULL)
    {
      return NULL;
    }

  p->key = key;
  p->buffer = buffer;
  p->size = size;
  p->rc = rc;
  p->thrd_entry = thread_get_thread_entry_info ();

  return p;
}

/*
 * css_free_wait_queue_entry() - free wait queue entry
 *   return: void
 *   conn(in): connection entry
 *   entry(in): wait queue entry
 */
static void
css_free_wait_queue_entry (CSS_CONN_ENTRY * conn, CSS_WAIT_QUEUE_ENTRY * entry)
{
  if (entry == NULL)
    {
      return;
    }

  if (entry->thrd_entry != NULL)
    {
      thread_lock_entry (entry->thrd_entry);

      assert (entry->thrd_entry->resume_status == THREAD_CSS_QUEUE_SUSPENDED);
      thread_wakeup_already_had_mutex (entry->thrd_entry, THREAD_CSS_QUEUE_RESUMED);

      thread_unlock_entry (entry->thrd_entry);
    }

  css_retire_wait_queue_entry (conn, entry);
}

/*
 * css_add_wait_queue_entry() - add wait queue entry
 *   return: wait queue entry
 *   conn(in): connection entry
 *   list(in): wait queue list
 *   request_id(in): request id
 *   buffer(out):
 *   buffer_size(out):
 *   rc(out):
 */
static CSS_WAIT_QUEUE_ENTRY *
css_add_wait_queue_entry (CSS_CONN_ENTRY * conn, CSS_LIST * list, unsigned short request_id, char **buffer,
			  int *buffer_size, int *rc)
{
  CSS_WAIT_QUEUE_ENTRY *p;

  p = css_make_wait_queue_entry (conn, request_id, buffer, buffer_size, rc);
  if (p == NULL)
    {
      return NULL;
    }

  if (css_add_list (list, p) != NO_ERROR)
    {
      css_retire_wait_queue_entry (conn, p);
      return NULL;
    }

  return p;
}

/*
 * find_wait_queue_entry_by_key() - find wait queue entry using key
 *   return: status of traverse
 *   data(in): wait queue entry
 *   user(in): search argument
 */
static int
find_wait_queue_entry_by_key (void *data, void *user)
{
  CSS_WAIT_QUEUE_SEARCH_ARG *arg = (CSS_WAIT_QUEUE_SEARCH_ARG *) user;
  CSS_WAIT_QUEUE_ENTRY *p = (CSS_WAIT_QUEUE_ENTRY *) data;

  if (p->key == arg->key)
    {
      arg->entry_ptr = p;
      if (arg->remove_entry)
	{
	  return TRAV_STOP_DELETE;
	}
      else
	{
	  return TRAV_STOP;
	}
    }

  return TRAV_CONT;
}

/*
 * css_find_and_remove_wait_queue_entry() - find wait queue entry and remove it
 *   return: wait queue entry
 *   list(in): wait queue list
 *   key(in):
 */
static CSS_WAIT_QUEUE_ENTRY *
css_find_and_remove_wait_queue_entry (CSS_LIST * list, unsigned int key)
{
  CSS_WAIT_QUEUE_SEARCH_ARG arg;

  arg.entry_ptr = NULL;
  arg.key = key;
  arg.remove_entry = 1;

  css_traverse_list (list, find_wait_queue_entry_by_key, &arg);

  return arg.entry_ptr;
}


/*
 * css_queue_packet() - queue packet
 *   return: void
 *   conn(in): connection entry
 *   type(in): packet type
 *   request_id(in): request id
 *   header(in): network header
 *   size(in): packet size
 */
static css_error_code
css_queue_packet (CSS_CONN_ENTRY * conn, int type, unsigned short request_id, const NET_HEADER * header, int size)
{
  THREAD_ENTRY *wait_thrd = NULL, *p, *next;
  unsigned short flags = 0;
  int r;
  int transaction_id, db_error, invalidate_snapshot;
  css_error_code rc = NO_ERRORS;

  transaction_id = ntohl (header->transaction_id);
  db_error = (int) ntohl (header->db_error);
  flags = ntohs (header->flags);
  invalidate_snapshot = flags | NET_HEADER_FLAG_INVALIDATE_SNAPSHOT ? 1 : 0;

  r = rmutex_lock (NULL, &conn->rmutex);
  assert (r == NO_ERROR);

  if (conn->stop_talk)
    {
      r = rmutex_unlock (NULL, &conn->rmutex);
      assert (r == NO_ERROR);
      return CONNECTION_CLOSED;
    }

  conn->set_tran_index (transaction_id);
  conn->db_error = db_error;
  conn->invalidate_snapshot = invalidate_snapshot;

  switch (type)
    {
    case CLOSE_TYPE:
      css_process_close_packet (conn);
      break;

    case ABORT_TYPE:
      css_process_abort_packet (conn, request_id);
      break;

    case DATA_TYPE:
      css_queue_data_packet (conn, request_id, header, &wait_thrd);
      break;

    case ERROR_TYPE:
      css_queue_error_packet (conn, request_id, header);
      break;

    case COMMAND_TYPE:
      rc = css_queue_command_packet (conn, request_id, header, size);
      if (rc != NO_ERRORS)
	{
	  r = rmutex_unlock (NULL, &conn->rmutex);
	  assert (r == NO_ERROR);
	  return rc;
	}
      break;

    default:
      CSS_TRACE2 ("Asked to queue an unknown packet id = %d.\n", type);
      assert (false);
      return WRONG_PACKET_TYPE;
    }

  p = wait_thrd;
  while (p != NULL)
    {
      thread_lock_entry (p);

      assert (p->resume_status == THREAD_CSS_QUEUE_SUSPENDED || p->resume_status == THREAD_CSECT_WRITER_SUSPENDED);
      next = p->next_wait_thrd;
      p->next_wait_thrd = NULL;

      /* When the resume_status is THREAD_CSS_QUEUE_SUSPENDED, it means the data waiting thread is still waiting on the
       * data queue. Otherwise, in case of THREAD_CSECT_WRITER_SUSPENDED, it means that the thread was timed out, is
       * trying to clear its queue buffer (see clear_wait_queue_entry_and_free_buffer function), and waiting for its
       * conn->csect. We don't need to wakeup the thread for this case. We may send useless signal for it, but it may
       * bring other anomalies: the thread may sleep on another resources which we don't know at this moment. */
      if (p->resume_status == THREAD_CSS_QUEUE_SUSPENDED)
	{
	  thread_wakeup_already_had_mutex (p, THREAD_CSS_QUEUE_RESUMED);
	}

      thread_unlock_entry (p);
      p = next;
    }

  r = rmutex_unlock (NULL, &conn->rmutex);
  assert (r == NO_ERROR);

  return NO_ERRORS;
}

/*
 * css_process_close_packet() - prccess close packet
 *   return: void
 *   conn(in): conenction entry
 */
static void
css_process_close_packet (CSS_CONN_ENTRY * conn)
{
  if (!IS_INVALID_SOCKET (conn->fd))
    {
      css_shutdown_socket (conn->fd);
      conn->fd = INVALID_SOCKET;
    }

  conn->status = CONN_CLOSED;
}

/*
 * css_process_abort_packet() - process abort packet
 *   return: void
 *   conn(in): connection entry
 *   request_id(in): request id
 */
static void
css_process_abort_packet (CSS_CONN_ENTRY * conn, unsigned short request_id)
{
  CSS_QUEUE_ENTRY *request, *data;

  request = css_find_and_remove_queue_entry (&conn->request_queue, request_id);
  if (request)
    {
      css_free_queue_entry (conn, request);
    }

  data = css_find_and_remove_queue_entry (&conn->data_queue, request_id);
  if (data)
    {
      css_free_queue_entry (conn, data);
    }

  if (css_find_queue_entry (&conn->abort_queue, request_id) == NULL)
    {
      css_add_queue_entry (conn, &conn->abort_queue, request_id, NULL, 0,
			   NO_ERRORS, conn->get_tran_index (), conn->invalidate_snapshot, conn->db_error);
    }
}

/*
 * css_queue_data_packet() - queue data packet
 *   return: void
 *   conn(in): connection entry
 *   request_id(in): request id
 *   header(in): network header
 *   wake_thrd(out): thread that wake up
 */
static void
css_queue_data_packet (CSS_CONN_ENTRY * conn, unsigned short request_id,
		       const NET_HEADER * header, THREAD_ENTRY ** wake_thrd)
{
  THREAD_ENTRY *thrd = NULL, *last = NULL;
  CSS_QUEUE_ENTRY *buffer_entry;
  CSS_WAIT_QUEUE_ENTRY *data_wait = NULL;
  char *buffer = NULL;
  int rc;
  int size;			/* size to be read */

  /* setup wake_thrd. hmm.. consider recursion */
  if (*wake_thrd != NULL)
    {
      last = *wake_thrd;
      while (last->next_wait_thrd != NULL)
	{
	  last = last->next_wait_thrd;
	}
    }

  size = ntohl (header->buffer_size);
  /* check if user have given a buffer */
  buffer_entry = css_find_and_remove_queue_entry (&conn->buffer_queue, request_id);
  if (buffer_entry != NULL)
    {
      /* compare data and buffer size. if different? something wrong!!! */
      if (size > buffer_entry->size)
	{
	  size = buffer_entry->size;
	}
      buffer = buffer_entry->buffer;
      buffer_entry->buffer = NULL;

      css_free_queue_entry (conn, buffer_entry);
    }
  else if (size == 0)
    {
      buffer = NULL;
    }
  else
    {
      buffer = (char *) malloc (size);
    }

  /*
   * check if there exists thread waiting for data.
   * Add to wake_thrd list.
   */
  data_wait = css_find_and_remove_wait_queue_entry (&conn->data_wait_queue, request_id);

  if (data_wait != NULL)
    {
      thrd = data_wait->thrd_entry;
      thrd->next_wait_thrd = NULL;
      if (last == NULL)
	{
	  *wake_thrd = thrd;
	}
      else
	{
	  last->next_wait_thrd = thrd;
	}
      last = thrd;
    }

  /* receive data into buffer and queue data if there's no waiting thread */
  if (buffer != NULL)
    {
      rc = css_net_recv (conn->fd, buffer, &size, -1);
      if (rc == NO_ERRORS || rc == RECORD_TRUNCATED)
	{
	  if (!css_is_request_aborted (conn, request_id))
	    {
	      if (data_wait == NULL)
		{
		  /* if waiter not exists, add to data queue */
		  css_add_queue_entry (conn, &conn->data_queue, request_id, buffer, size, rc, conn->get_tran_index (),
				       conn->invalidate_snapshot, conn->db_error);
		  return;
		}
	      else
		{
		  *data_wait->buffer = buffer;
		  *data_wait->size = size;
		  *data_wait->rc = rc;
		  data_wait->thrd_entry = NULL;
		  css_free_wait_queue_entry (conn, data_wait);
		  return;
		}
	    }
	}
      /* if error occurred */
      free_and_init (buffer);
    }
  else
    {
      rc = CANT_ALLOC_BUFFER;
      css_read_remaining_bytes (conn->fd, sizeof (int) + size);
      if (!css_is_request_aborted (conn, request_id))
	{
	  if (data_wait == NULL)
	    {
	      css_add_queue_entry (conn, &conn->data_queue, request_id, NULL,
				   0, rc, conn->get_tran_index (), conn->invalidate_snapshot, conn->db_error);
	      return;
	    }
	}
    }

  /* if error was occurred, setup error status */
  if (data_wait != NULL)
    {
      *data_wait->buffer = NULL;
      *data_wait->size = 0;
      *data_wait->rc = rc;
    }
}

/*
 * css_queue_error_packet() - queue error packet
 *   return: void
 *   conn(in): connection entry
 *   request_id(in): request id
 *   header(in): network header
 */
static void
css_queue_error_packet (CSS_CONN_ENTRY * conn, unsigned short request_id, const NET_HEADER * header)
{
  char *buffer;
  int rc;
  int size;

  size = ntohl (header->buffer_size);
  buffer = (char *) malloc (size);

  if (buffer != NULL)
    {
      rc = css_net_recv (conn->fd, buffer, &size, -1);
      if (rc == NO_ERRORS || rc == RECORD_TRUNCATED)
	{
	  if (!css_is_request_aborted (conn, request_id))
	    {
	      css_add_queue_entry (conn, &conn->error_queue, request_id,
				   buffer, size, rc, conn->get_tran_index (), conn->invalidate_snapshot,
				   conn->db_error);
	      return;
	    }
	}
      free_and_init (buffer);
    }
  else
    {
      rc = CANT_ALLOC_BUFFER;
      css_read_remaining_bytes (conn->fd, sizeof (int) + size);
      if (!css_is_request_aborted (conn, request_id))
	{
	  css_add_queue_entry (conn, &conn->error_queue, request_id, NULL, 0,
			       rc, conn->get_tran_index (), conn->invalidate_snapshot, conn->db_error);
	}
    }
}

/*
 * css_queue_command_packet() - queue command packet
 *   return: void
 *   conn(in): connection entry
 *   request_id(in): request id
 *   header(in): network header
 *   size(in): packet size
 */
static css_error_code
css_queue_command_packet (CSS_CONN_ENTRY * conn, unsigned short request_id, const NET_HEADER * header, int size)
{
  NET_HEADER *p;
  NET_HEADER data_header = DEFAULT_HEADER_DATA;
  css_error_code rc = NO_ERRORS;

  assert (!conn->stop_talk);

  if (css_is_request_aborted (conn, request_id))
    {
      // ignore
      return NO_ERRORS;
    }

  if (conn->free_net_header_list != NULL)
    {
      p = css_claim_net_header_entry (conn);
    }
  else
    {
      p = (NET_HEADER *) malloc (sizeof (NET_HEADER));
    }

  if (p == NULL)
    {
      assert (false);
      return CANT_ALLOC_BUFFER;
    }

  memcpy ((char *) p, (char *) header, sizeof (NET_HEADER));

  rc = css_add_queue_entry (conn, &conn->request_queue, request_id, (char *) p, size, NO_ERRORS,
			    conn->get_tran_index (), conn->invalidate_snapshot, conn->db_error);
  if (rc != NO_ERRORS)
    {
      css_retire_net_header_entry (conn, p);
      return rc;
    }

  if (ntohl (header->buffer_size) <= 0)
    {
      // a request without a buffer, e.g, NET_SERVER_LOG_CHECKPOINT, NET_SERVER_TM_SERVER_ABORT.
      return NO_ERRORS;
    }

  rc = (css_error_code) css_read_header (conn, &data_header);
  if (rc != NO_ERRORS)
    {
      // what to do?
      return rc;
    }

  rc = css_queue_packet (conn, (int) ntohl (data_header.type), (unsigned short) ntohl (data_header.request_id),
			 &data_header, sizeof (NET_HEADER));
  return rc;
}

/*
 * css_request_aborted() - check request is aborted
 *   return: true if aborted, or false
 *   conn(in): connection entry
 *   request_id(in): request id
 */
static bool
css_is_request_aborted (CSS_CONN_ENTRY * conn, unsigned short request_id)
{
  CSS_QUEUE_ENTRY *p;

  p = css_find_queue_entry (&conn->abort_queue, request_id);
  if (p != NULL)
    {
      return true;
    }
  else
    {
      return false;
    }
}

/*
 * css_return_queued_request() - get request from queue
 *   return: 0 if success, or error code
 *   conn(in): connection entry
 *   rid(out): request id
 *   request(out): request
 *   buffer_size(out): request buffer size
 */
int
css_return_queued_request (CSS_CONN_ENTRY * conn, unsigned short *rid, int *request, int *buffer_size)
{
  CSS_QUEUE_ENTRY *p;
  NET_HEADER *buffer;
  int rc, r;

  r = rmutex_lock (NULL, &conn->rmutex);
  assert (r == NO_ERROR);

  if (conn->status == CONN_OPEN)
    {
      p = (CSS_QUEUE_ENTRY *) css_remove_list_from_head (&conn->request_queue);
      if (p != NULL)
	{
	  *rid = p->key;

	  buffer = (NET_HEADER *) p->buffer;
	  p->buffer = NULL;

	  *request = ntohs (buffer->function_code);
	  *buffer_size = ntohl (buffer->buffer_size);

	  conn->set_tran_index (p->transaction_id);
	  conn->invalidate_snapshot = p->invalidate_snapshot;
	  conn->db_error = p->db_error;

	  css_retire_net_header_entry (conn, buffer);

	  css_free_queue_entry (conn, p);
	  rc = NO_ERRORS;
	}
      else
	{
	  rc = NO_DATA_AVAILABLE;
	}
    }
  else
    {
      rc = CONN_CLOSED;
    }

  r = rmutex_unlock (NULL, &conn->rmutex);
  assert (r == NO_ERROR);
  return rc;
}

/*
 * clear_wait_queue_entry_and_free_buffer () - remove data_wait_queue entry when completing or aborting
 *                                             to receive buffer from data_wait_queue.
 *   return: void
 *   conn(in): connection entry
 *   rid(in): request id
 *   bufferp(in): data buffer
 */
static void
clear_wait_queue_entry_and_free_buffer (THREAD_ENTRY * thrdp, CSS_CONN_ENTRY * conn, unsigned short rid, char **bufferp)
{
  CSS_WAIT_QUEUE_ENTRY *data_wait;
  int r;

  r = rmutex_lock (thrdp, &conn->rmutex);
  assert (r == NO_ERROR);

  /* check the deadlock related problem */
  data_wait = css_find_and_remove_wait_queue_entry (&conn->data_wait_queue, rid);

  /* data_wait might be always not NULL except the actual connection close */
  if (data_wait)
    {
      assert (data_wait->thrd_entry == thrdp);	/* it must be me */
      data_wait->thrd_entry = NULL;
      css_free_wait_queue_entry (conn, data_wait);
    }
  else
    {
      /* connection_handler_thread may proceed ahead of me right after timeout has happened. If the case, we must free
       * the buffer. */
      if (*bufferp != NULL)
	{
	  free_and_init (*bufferp);
	}
    }

  r = rmutex_unlock (thrdp, &conn->rmutex);
  assert (r == NO_ERROR);
}

/*
 * css_return_queued_data_timeout() - get request data from queue until timeout
 *   return: 0 if success, or error code
 *   conn(in): connection entry
 *   rid(out): request id
 *   buffer(out): data buffer
 *   bufsize(out): buffer size
 *   rc(out):
 *   waitsec: timeout second
 */
static int
css_return_queued_data_timeout (CSS_CONN_ENTRY * conn, unsigned short rid,
				char **buffer, int *bufsize, int *rc, int waitsec)
{
  CSS_QUEUE_ENTRY *data_entry, *buffer_entry;
  CSS_WAIT_QUEUE_ENTRY *data_wait;

  int r;

  /* enter the critical section of this connection */
  r = rmutex_lock (NULL, &conn->rmutex);
  assert (r == NO_ERROR);

  *buffer = NULL;
  *bufsize = -1;

  /* if conn is closed or to be closed, return CONECTION_CLOSED */
  if (conn->status == CONN_OPEN)
    {
      /* look up the data queue first to see if the required data is arrived and queued already */
      data_entry = css_find_and_remove_queue_entry (&conn->data_queue, rid);

      if (data_entry)
	{
	  /* look up the buffer queue to see if the user provided the receive data buffer */
	  buffer_entry = css_find_and_remove_queue_entry (&conn->buffer_queue, rid);

	  if (buffer_entry)
	    {
	      /* copy the received data to the user provided buffer area */
	      *buffer = buffer_entry->buffer;
	      *bufsize = MIN (data_entry->size, buffer_entry->size);
	      if (*buffer != data_entry->buffer || *bufsize != data_entry->size)
		{
		  memcpy (*buffer, data_entry->buffer, *bufsize);
		}

	      /* destroy the buffer queue entry */
	      buffer_entry->buffer = NULL;
	      css_free_queue_entry (conn, buffer_entry);
	    }
	  else
	    {
	      /* set the buffer to point to the data queue entry */
	      *buffer = data_entry->buffer;
	      *bufsize = data_entry->size;
	      data_entry->buffer = NULL;
	    }

	  /* set return code, transaction id, and error code */
	  *rc = data_entry->rc;
	  conn->set_tran_index (data_entry->transaction_id);
	  conn->invalidate_snapshot = data_entry->invalidate_snapshot;
	  conn->db_error = data_entry->db_error;

	  css_free_queue_entry (conn, data_entry);

	  r = rmutex_unlock (NULL, &conn->rmutex);
	  assert (r == NO_ERROR);

	  return NO_ERRORS;
	}
      else
	{
	  THREAD_ENTRY *thrd;

	  /* no data queue entry means that the data is not arrived yet; wait until the data arrives */
	  *rc = NO_DATA_AVAILABLE;

	  /* lock thread entry before enqueue an entry to data wait queue in order to prevent being woken up by
	   * 'css_queue_packet()' before this thread suspends */
	  thrd = thread_get_thread_entry_info ();
	  thread_lock_entry (thrd);
	  /* make a data wait queue entry */
	  data_wait = css_add_wait_queue_entry (conn, &conn->data_wait_queue, rid, buffer, bufsize, rc);
	  if (data_wait)
	    {
	      /* exit the critical section before to be suspended */
	      r = rmutex_unlock (NULL, &conn->rmutex);
	      assert (r == NO_ERROR);

	      /* fall to the thread sleep until the socket listener 'css_server_thread()' receives and enqueues the
	       * data */
	      if (waitsec < 0)
		{
		  thread_suspend_wakeup_and_unlock_entry (thrd, THREAD_CSS_QUEUE_SUSPENDED);

		  if (thrd->resume_status != THREAD_CSS_QUEUE_RESUMED)
		    {
		      assert (thrd->resume_status == THREAD_RESUME_DUE_TO_INTERRUPT);

		      clear_wait_queue_entry_and_free_buffer (thrd, conn, rid, buffer);
		      *buffer = NULL;
		      *bufsize = -1;
		      return NO_DATA_AVAILABLE;
		    }
		  else
		    {
		      assert (thrd->resume_status == THREAD_CSS_QUEUE_RESUMED);
		    }
		}
	      else
		{
		  int r;
		  struct timespec abstime;

		  abstime.tv_sec = (int) time (NULL) + waitsec;
		  abstime.tv_nsec = 0;

		  r = thread_suspend_timeout_wakeup_and_unlock_entry (thrd, &abstime, THREAD_CSS_QUEUE_SUSPENDED);

		  if (r == ER_CSS_PTHREAD_COND_TIMEDOUT)
		    {
		      clear_wait_queue_entry_and_free_buffer (thrd, conn, rid, buffer);
		      *rc = TIMEDOUT_ON_QUEUE;
		      *buffer = NULL;
		      *bufsize = -1;
		      return TIMEDOUT_ON_QUEUE;
		    }
		  else if (thrd->resume_status != THREAD_CSS_QUEUE_RESUMED)
		    {
		      assert (thrd->resume_status == THREAD_RESUME_DUE_TO_INTERRUPT);

		      clear_wait_queue_entry_and_free_buffer (thrd, conn, rid, buffer);
		      *buffer = NULL;
		      *bufsize = -1;
		      return NO_DATA_AVAILABLE;
		    }
		  else
		    {
		      assert (thrd->resume_status == THREAD_CSS_QUEUE_RESUMED);
		    }
		}

	      if (*buffer == NULL || *bufsize < 0)
		{
		  return CONNECTION_CLOSED;
		}

	      if (*rc == CONNECTION_CLOSED)
		{
		  clear_wait_queue_entry_and_free_buffer (thrd, conn, rid, buffer);
		}

	      return NO_ERRORS;
	    }
	  else
	    {
	      /* oops! error! unlock thread entry */
	      thread_unlock_entry (thrd);
	      /* allocation error */
	      *rc = CANT_ALLOC_BUFFER;
	    }
	}
    }
  else
    {
      /* conn->status == CONN_CLOSED || CONN_CLOSING; the connection was closed */
      *rc = CONNECTION_CLOSED;
    }

  /* exit the critical section */
  r = rmutex_unlock (NULL, &conn->rmutex);
  assert (r == NO_ERROR);
  return *rc;
}

/*
 * css_return_queued_data() - get data from queue
 *   return: 0 if success, or error code
 *   conn(in): connection entry
 *   rid(out): request id
 *   buffer(out): data buffer
 *   bufsize(out): buffer size
 *   rc(out):
 */
int
css_return_queued_data (CSS_CONN_ENTRY * conn, unsigned short rid, char **buffer, int *bufsize, int *rc)
{
  return css_return_queued_data_timeout (conn, rid, buffer, bufsize, rc, -1);
}

/*
 * css_return_queued_error() - get error from queue
 *   return: 0 if success, or error code
 *   conn(in): connection entry
 *   request_id(out): request id
 *   buffer(out): data buffer
 *   buffer_size(out): buffer size
 *   rc(out):
 */
int
css_return_queued_error (CSS_CONN_ENTRY * conn, unsigned short request_id, char **buffer, int *buffer_size, int *rc)
{
  CSS_QUEUE_ENTRY *p;
  int ret = 0, r;

  r = rmutex_lock (NULL, &conn->rmutex);
  assert (r == NO_ERROR);

  p = css_find_and_remove_queue_entry (&conn->error_queue, request_id);
  if (p != NULL)
    {
      *buffer = p->buffer;
      *buffer_size = p->size;
      *rc = p->db_error;
      p->buffer = NULL;
      css_free_queue_entry (conn, p);
      ret = 1;
    }

  r = rmutex_unlock (NULL, &conn->rmutex);
  assert (r == NO_ERROR);

  return ret;
}

/*
 * css_is_valid_request_id() - check request id id valid
 *   return: true if valid, or false
 *   conn(in): connection entry
 *   request_id(in): request id
 */
static bool
css_is_valid_request_id (CSS_CONN_ENTRY * conn, unsigned short request_id)
{
  if (css_find_queue_entry (&conn->data_queue, request_id) != NULL)
    {
      return false;
    }

  if (css_find_queue_entry (&conn->request_queue, request_id) != NULL)
    {
      return false;
    }

  if (css_find_queue_entry (&conn->abort_queue, request_id) != NULL)
    {
      return false;
    }

  if (css_find_queue_entry (&conn->error_queue, request_id) != NULL)
    {
      return false;
    }

  return true;
}

/*
 * css_remove_unexpected_packets() - remove unexpected packet
 *   return: void
 *   conn(in): connection entry
 *   request_id(in): request id
 */
void
css_remove_unexpected_packets (CSS_CONN_ENTRY * conn, unsigned short request_id)
{
  css_free_queue_entry (conn, css_find_and_remove_queue_entry (&conn->request_queue, request_id));
  css_free_queue_entry (conn, css_find_and_remove_queue_entry (&conn->data_queue, request_id));
  css_free_queue_entry (conn, css_find_and_remove_queue_entry (&conn->error_queue, request_id));
}

/*
 * css_queue_user_data_buffer() - queue user data
 *   return: 0 if success, or error code
 *   conn(in): connection entry
 *   request_id(in): request id
 *   size(in): buffer size
 *   buffer(in): buffer
 */
int
css_queue_user_data_buffer (CSS_CONN_ENTRY * conn, unsigned short request_id, int size, char *buffer)
{
  int rc = NO_ERRORS, r;

  r = rmutex_lock (NULL, &conn->rmutex);
  assert (r == NO_ERROR);

  if (buffer && (!css_is_request_aborted (conn, request_id)))
    {
      rc = css_add_queue_entry (conn, &conn->buffer_queue, request_id, buffer,
				size, NO_ERRORS, conn->get_tran_index (), conn->invalidate_snapshot, conn->db_error);
    }

  r = rmutex_unlock (NULL, &conn->rmutex);
  assert (r == NO_ERROR);

  return rc;
}

/*
 * css_remove_and_free_queue_entry() - free queue entry
 *   return: status if traverse
 *   data(in): connection entry
 *   arg(in): queue entry
 */
static int
css_remove_and_free_queue_entry (void *data, void *arg)
{
  css_free_queue_entry ((CSS_CONN_ENTRY *) arg, (CSS_QUEUE_ENTRY *) data);
  return TRAV_CONT_DELETE;
}

/*
 * css_remove_and_free_wait_queue_entry() - free wait queue entry
 *   return: status if traverse
 *   data(in): connection entry
 *   arg(in): wait queue entry
 */
static int
css_remove_and_free_wait_queue_entry (void *data, void *arg)
{
  css_free_wait_queue_entry ((CSS_CONN_ENTRY *) arg, (CSS_WAIT_QUEUE_ENTRY *) data);
  return TRAV_CONT_DELETE;
}

/*
 * css_remove_all_unexpected_packets() - remove all unexpected packets
 *   return: void
 *   conn(in): connection entry
 */
void
css_remove_all_unexpected_packets (CSS_CONN_ENTRY * conn)
{
  int r;

  r = rmutex_lock (NULL, &conn->rmutex);
  assert (r == NO_ERROR);

  css_traverse_list (&conn->request_queue, css_remove_and_free_queue_entry, conn);

  css_traverse_list (&conn->data_queue, css_remove_and_free_queue_entry, conn);

  css_traverse_list (&conn->data_wait_queue, css_remove_and_free_wait_queue_entry, conn);

  css_traverse_list (&conn->abort_queue, css_remove_and_free_queue_entry, conn);

  css_traverse_list (&conn->error_queue, css_remove_and_free_queue_entry, conn);

  r = rmutex_unlock (NULL, &conn->rmutex);
  assert (r == NO_ERROR);
}

/*
 * css_set_user_access_status() - set user access status information
 *   return: void
 *   db_user(in):
 *   host(in):
 *   program_name(in):
 */
void
css_set_user_access_status (const char *db_user, const char *host, const char *program_name)
{
  LAST_ACCESS_STATUS *access = NULL;

  assert (db_user != NULL);
  assert (host != NULL);
  assert (program_name != NULL);

  csect_enter (NULL, CSECT_ACCESS_STATUS, INF_WAIT);

  for (access = css_Access_status_anchor; access != NULL; access = access->next)
    {
      if (strcmp (access->db_user, db_user) == 0)
	{
	  break;
	}
    }

  if (access == NULL)
    {
      access = (LAST_ACCESS_STATUS *) malloc (sizeof (LAST_ACCESS_STATUS));
      if (access == NULL)
	{
	  /* if memory allocation fail, just ignore and return */
	  csect_exit (NULL, CSECT_ACCESS_STATUS);
	  return;
	}
      css_Num_access_user++;

      memset (access, 0, sizeof (LAST_ACCESS_STATUS));

      access->next = css_Access_status_anchor;
      css_Access_status_anchor = access;

      strncpy (access->db_user, db_user, sizeof (access->db_user) - 1);
    }

  csect_exit (NULL, CSECT_ACCESS_STATUS);

  access->time = time (NULL);
  strncpy (access->host, host, sizeof (access->host) - 1);
  strncpy (access->program_name, program_name, sizeof (access->program_name) - 1);

  return;
}

/*
 * css_get_user_access_status() - get user access status informations
 *   return: void
 *   num_user(in):
 *   access_status_array(out):
 */
void
css_get_user_access_status (int num_user, LAST_ACCESS_STATUS ** access_status_array)
{
  int i = 0;
  LAST_ACCESS_STATUS *access = NULL;

  csect_enter_as_reader (NULL, CSECT_ACCESS_STATUS, INF_WAIT);

  for (access = css_Access_status_anchor; (access != NULL && i < num_user); access = access->next, i++)
    {
      access_status_array[i] = access;
    }

  csect_exit (NULL, CSECT_ACCESS_STATUS);

  return;
}

/*
 * css_free_user_access_status() - free all user access status information
 *   return: void
 */
void
css_free_user_access_status (void)
{
  LAST_ACCESS_STATUS *access = NULL;

  csect_enter (NULL, CSECT_ACCESS_STATUS, INF_WAIT);

  while (css_Access_status_anchor != NULL)
    {
      access = css_Access_status_anchor;
      css_Access_status_anchor = access->next;

      free_and_init (access);
    }

  css_Num_access_user = 0;

  csect_exit (NULL, CSECT_ACCESS_STATUS);

  return;
}
