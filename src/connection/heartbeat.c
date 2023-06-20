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
 * heartbeat.c - heartbeat resource process common
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <assert.h>
#include <signal.h>

#if defined(WINDOWS)
#include <winsock2.h>
#include <windows.h>
#else /* WINDOWS */
#include <fcntl.h>
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
#include <pthread.h>
#endif /* SOLARIS || LINUX */

#include "environment_variable.h"
#include "error_context.hpp"
#include "porting.h"
#include "system_parameter.h"
#include "error_manager.h"
#include "connection_defs.h"
#include "connection_support.h"
#if defined(WINDOWS)
#include "wintcp.h"
#else /* WINDOWS */
#include "tcp.h"
#endif /* WINDOWS */
#include "release_string.h"
#include "heartbeat.h"

extern CSS_CONN_ENTRY *css_connect_to_master_server (int master_port_id, const char *server_name, int name_length);
extern void css_shutdown_conn (CSS_CONN_ENTRY * conn);

static THREAD_RET_T THREAD_CALLING_CONVENTION hb_thread_master_reader (void *arg);
static char *hb_pack_server_name (const char *server_name, int *name_length, const char *log_path, HB_PROC_TYPE type);

static CSS_CONN_ENTRY *hb_connect_to_master (const char *server_name, const char *log_path, HB_PROC_TYPE type);
static int hb_create_master_reader (void);
static int hb_process_master_request_info (CSS_CONN_ENTRY * conn);
static const char *hb_type_to_str (HB_PROC_TYPE type);

static pthread_t hb_Master_mon_th;

static CSS_CONN_ENTRY *hb_Conn = NULL;
static char hb_Exec_path[PATH_MAX];
static char **hb_Argv;

bool hb_Proc_shutdown = false;

SOCKET hb_Pipe_to_master = INVALID_SOCKET;

/*
 * hb_process_type_string () -
 *   return: process type string
 *
 *   ptype(in):
 */
const char *
hb_process_type_string (int ptype)
{
  switch (ptype)
    {
    case HB_PTYPE_TRAN_SERVER:
      return HB_PTYPE_TRAN_SERVER_STR;
    case HB_PTYPE_PAGE_SERVER:
      return HB_PTYPE_PAGE_SERVER_STR;
    case HB_PTYPE_SERVER:
      return HB_PTYPE_SERVER_STR;
    case HB_PTYPE_COPYLOGDB:
      return HB_PTYPE_COPYLOGDB_STR;
    case HB_PTYPE_APPLYLOGDB:
      return HB_PTYPE_APPLYLOGDB_STR;
    }
  return "invalid";
}

/*
 * hb_set_exec_path () -
 *   return: none
 *
 *   exec_path(in):
 */
void
hb_set_exec_path (char *exec_path)
{
  strncpy (hb_Exec_path, exec_path, sizeof (hb_Exec_path) - 1);
}

/*
 * hb_set_argv () -
 *   return: none
 *
 *   argv(in):
 */
void
hb_set_argv (char **argv)
{
  hb_Argv = argv;
}


/*
 * css_send_heartbeat_request () -
 *   return:
 *
 *   conn(in):
 *   command(in):
 */
int
css_send_heartbeat_request (CSS_CONN_ENTRY * conn, int command)
{
  int nbytes;
  int request;

  request = htonl (command);
  if (conn && !IS_INVALID_SOCKET (conn->fd))
    {
      nbytes = send (conn->fd, (char *) &request, sizeof (int), 0);
      if (nbytes == sizeof (int))
	{
	  return (NO_ERRORS);
	}
      return (ERROR_ON_WRITE);
    }
  return CONNECTION_CLOSED;
}

/*
 * css_send_heartbeat_data () -
 *   return:
 *
 *   conn(in):
 *   data(in):
 *   size(in):
 */
int
css_send_heartbeat_data (CSS_CONN_ENTRY * conn, const char *data, int size)
{
  int nbytes;

  if (conn && !IS_INVALID_SOCKET (conn->fd))
    {
      nbytes = send (conn->fd, (char *) data, size, 0);
      if (nbytes == size)
	{
	  return (NO_ERRORS);
	}
      return (ERROR_ON_WRITE);
    }
  return CONNECTION_CLOSED;
}

/*
 * css_receive_heartbeat_request () -
 *   return:
 *
 *   conn(in):
 *   command(in):
 */
int
css_receive_heartbeat_request (CSS_CONN_ENTRY * conn, int *command)
{
  int nbytes;
  int request;
  int size = sizeof (request);

  if (conn && !IS_INVALID_SOCKET (conn->fd))
    {
      nbytes = css_readn (conn->fd, (char *) &request, size, -1);
      if (nbytes == size)
	{
	  *command = ntohl (request);
	  return NO_ERRORS;
	}
      return ERROR_ON_READ;
    }
  return CONNECTION_CLOSED;
}

/*
 * css_receive_heartbeat_data () -
 *   return:
 *
 *   conn(in):
 *   data(in):
 *   size(in):
 */
int
css_receive_heartbeat_data (CSS_CONN_ENTRY * conn, char *data, int size)
{
  int nbytes;

  if (conn && !IS_INVALID_SOCKET (conn->fd))
    {
      nbytes = css_readn (conn->fd, data, size, -1);
      if (nbytes == size)
	{
	  return NO_ERRORS;
	}
      return ERROR_ON_READ;
    }
  return CONNECTION_CLOSED;
}

/*
* hb_thread_master_reader () -
*   return: none
*
*   arg(in):
*/
static THREAD_RET_T THREAD_CALLING_CONVENTION
hb_thread_master_reader (void *arg)
{
#if !defined(WINDOWS)
  int error;

  /* *INDENT-OFF* */
  cuberr::context er_context (true);
  /* *INDENT-ON* */

  error = hb_process_master_request ();
  if (error != NO_ERROR)
    {
      hb_process_term ();

      /* wait 1 sec */
      sleep (1);


      /* is it ok? */
      kill (getpid (), SIGTERM);
    }
#endif
  return (THREAD_RET_T) 0;
}


/*
* hb_make_set_hbp_register () -
*   return:
*
*   type(in):
*/
static HBP_PROC_REGISTER *
hb_make_set_hbp_register (int type)
{
  HBP_PROC_REGISTER *hbp_register;
  char *p, *last;
  char **argv;

  hbp_register = (HBP_PROC_REGISTER *) malloc (sizeof (HBP_PROC_REGISTER));
  if (NULL == hbp_register)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (HBP_PROC_REGISTER));
      return (NULL);
    }

  memset ((void *) hbp_register, 0, sizeof (HBP_PROC_REGISTER));
  hbp_register->pid = htonl (getpid ());
  hbp_register->type = htonl (type);
  strncpy_bufsize (hbp_register->exec_path, hb_Exec_path);

  p = (char *) &hbp_register->args[0];
  last = (char *) (p + sizeof (hbp_register->args));
  for (argv = hb_Argv; *argv; argv++)
    {
      p += snprintf (p, MAX ((last - p), 0), "%s ", *argv);
    }

  return (hbp_register);
}


/*
* hb_deregister_from_master () -
*   return: NO_ERROR or ER_FAILED
*
*/
int
hb_deregister_from_master (void)
{
  int css_error;
  int pid;

  if (hb_Conn == NULL || IS_INVALID_SOCKET (hb_Conn->fd))
    {
      return ER_FAILED;
    }

  css_error = css_send_heartbeat_request (hb_Conn, SERVER_DEREGISTER_HA_PROCESS);
  if (css_error != NO_ERRORS)
    {
      return ER_FAILED;
    }

  pid = htonl (getpid ());
  css_error = css_send_heartbeat_data (hb_Conn, (char *) &pid, sizeof (pid));
  if (css_error != NO_ERRORS)
    {
      return ER_FAILED;
    }

  return NO_ERROR;
}

/*
* hb_register_to_master () -
*   return: NO_ERROR or ER_FAILED
*
*   conn(in):
*   type(in):
*/
int
hb_register_to_master (CSS_CONN_ENTRY * conn, int type)
{
  int error;
  HBP_PROC_REGISTER *hbp_register = NULL;

  if (NULL == conn)
    {
      er_log_debug (ARG_FILE_LINE, "invalid conn. (conn:NULL).\n");
      return (ER_FAILED);
    }

  hbp_register = hb_make_set_hbp_register (type);
  if (NULL == hbp_register)
    {
      er_log_debug (ARG_FILE_LINE, "hbp_register failed. \n");
      return (ER_FAILED);
    }

  if (!IS_INVALID_SOCKET (conn->fd))
    {
      error = css_send_heartbeat_request (conn, SERVER_REGISTER_HA_PROCESS);
      if (error != NO_ERRORS)
	{
	  goto error_return;
	}

      error = css_send_heartbeat_data (conn, (const char *) hbp_register, sizeof (*hbp_register));
      if (error != NO_ERRORS)
	{
	  goto error_return;
	}
    }
  free_and_init (hbp_register);
  return (NO_ERROR);

error_return:
  free_and_init (hbp_register);
  return (ER_FAILED);
}

/*
* hb_process_master_request_info () -
*   return: NO_ERROR or ER_FAILED
*
*   conn(in):
*/
static int
hb_process_master_request_info (CSS_CONN_ENTRY * conn)
{
  int rc;
  int command;

  if (NULL == conn)
    {
      er_log_debug (ARG_FILE_LINE, "invalid conn. (conn:NULL).\n");
      return (ER_FAILED);
    }

  rc = css_receive_heartbeat_request (conn, &command);
  if (rc == NO_ERRORS)
    {
      /* Ignore request, just check connection is alive or not */
      return (NO_ERROR);
    }

  return (ER_FAILED);
}

static const char *
hb_type_to_str (HB_PROC_TYPE type)
{
  if (type == HB_PTYPE_COPYLOGDB)
    {
      return "copylogdb";
    }
  else if (type == HB_PTYPE_APPLYLOGDB)
    {
      return "applylogdb";
    }
  else
    {
      return "";
    }
}

/*
* hb_process_to_master () -
*   return: NO_ERROR or ER_FAILED
*
*   argv(in):
*/
int
hb_process_master_request (void)
{
  int error;
  int r, status = 0;
  struct pollfd po[1] = { {0, 0, 0} };

  if (NULL == hb_Conn)
    {
      er_log_debug (ARG_FILE_LINE, "hb_Conn did not allocated yet. \n");
      return (ER_FAILED);
    }

  while (false == hb_Proc_shutdown)
    {
      po[0].fd = hb_Conn->fd;
      po[0].events = POLLIN;
      r = css_platform_independent_poll (po, 1, (prm_get_integer_value (PRM_ID_TCP_CONNECTION_TIMEOUT) * 1000));

      switch (r)
	{
	case 0:
	  break;
	case -1:
	  if (!IS_INVALID_SOCKET (hb_Conn->fd)
#if defined(WINDOWS)
	      && ioctlsocket (hb_Conn->fd, FIONREAD, (u_long *) (&status)) == SOCKET_ERROR
#else /* WINDOWS */
	      && fcntl (hb_Conn->fd, F_GETFL, status) < 0
#endif /* WINDOWS */
	    )
	    hb_Proc_shutdown = true;
	  break;
	default:
	  error = hb_process_master_request_info (hb_Conn);
	  if (NO_ERROR != error)
	    {
	      hb_Proc_shutdown = true;
	    }
	  break;
	}
    }

  return (ER_FAILED);
}


/*
 * hb_pack_server_name() - make a "server_name" string
 *   return: packed name
 *   server_name(in)   : server name
 *   name_length(out)  : length of packed name
 *   log_path(in)      : log path
 *   copylogdbyn(in)   : true if copylogdb
 *
 * Note:
 *         make a "server_name" string to connect to master
 *         server_name = server_type ( # ) +
 *                       server_name +
 *                       release_string +
 *                       env_name +   ($CUBRID path)
 *                       pid_string   (process id)
 */
static char *
hb_pack_server_name (const char *server_name, int *name_length, const char *log_path, HB_PROC_TYPE type)
{
  char *packed_name = NULL;
  const char *env_name = NULL;
  char pid_string[16];
  int n_len, l_len, r_len, e_len, p_len;

  if (server_name != NULL)
    {
      env_name = envvar_root ();
      if (env_name == NULL)
	{
	  return NULL;
	}

      /* here we changed the 2nd string in packed_name from rel_release_string() to rel_major_release_string() solely
       * for the purpose of matching the name of the CUBRID driver. */

      snprintf (pid_string, sizeof (pid_string), "%d", getpid ());
      n_len = (int) strlen (server_name) + 1;
      l_len = (log_path) ? (int) strlen (log_path) + 1 : 0;
      r_len = (int) strlen (rel_major_release_string ()) + 1;
      e_len = (int) strlen (env_name) + 1;
      p_len = (int) strlen (pid_string) + 1;
      *name_length = n_len + l_len + r_len + e_len + p_len + 5;

      packed_name = (char *) malloc (*name_length);
      if (packed_name == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (*name_length));
	  return NULL;
	}

      if (type == HB_PTYPE_COPYLOGDB)
	{
	  packed_name[0] = '$';
	}
      else if (type == HB_PTYPE_APPLYLOGDB)
	{
	  packed_name[0] = '%';
	}
      else
	{
	  assert (0);

	  free_and_init (packed_name);
	  return NULL;
	}
      memcpy (packed_name + 1, server_name, n_len);
      if (l_len)
	{
	  packed_name[(1 + n_len) - 1] = ':';
	  memcpy (packed_name + 1 + n_len, log_path, l_len);
	}
      memcpy (packed_name + 1 + n_len + l_len, rel_major_release_string (), r_len);
      memcpy (packed_name + 1 + n_len + l_len + r_len, env_name, e_len);
      memcpy (packed_name + 1 + n_len + l_len + r_len + e_len, pid_string, p_len);
    }
  return (packed_name);
}

/*
 * hb_connect_to_master() - connect to the master server
 *   return: conn
 *   server_name(in): server name
 *   log_path(in): log path
 *   copylogdbyn(in):
 */
static CSS_CONN_ENTRY *
hb_connect_to_master (const char *server_name, const char *log_path, HB_PROC_TYPE type)
{
  CSS_CONN_ENTRY *conn;
  char *packed_name;
  int name_length = 0;

  packed_name = hb_pack_server_name (server_name, &name_length, log_path, type);
  if (packed_name == NULL)
    {
      return NULL;
    }
  conn = css_connect_to_master_server (prm_get_master_port_id (), packed_name, name_length);
  if (conn == NULL)
    {
      free_and_init (packed_name);
      return NULL;
    }

  hb_Pipe_to_master = conn->fd;
  free_and_init (packed_name);
  return conn;
}

/*
* hb_create_master_reader () -
*   return: NO_ERROR or ER_FAILED
*
*   conn(in):
*/
static int
hb_create_master_reader (void)
{
#if !defined (WINDOWS)
  int rv;
  pthread_attr_t thread_attr;
  size_t ts_size;
  pthread_t master_reader_th;

  rv = pthread_attr_init (&thread_attr);
  if (rv != 0)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_PTHREAD_ATTR_INIT, 0);
      return ER_CSS_PTHREAD_ATTR_INIT;
    }

  rv = pthread_attr_setdetachstate (&thread_attr, PTHREAD_CREATE_DETACHED);
  if (rv != 0)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_PTHREAD_ATTR_SETDETACHSTATE, 0);
      return ER_CSS_PTHREAD_ATTR_SETDETACHSTATE;
    }

#if defined(AIX)
  /* AIX's pthread is slightly different from other systems. Its performance highly depends on the pthread's scope and
   * it's related kernel parameters. */
  rv =
    pthread_attr_setscope (&thread_attr,
			   prm_get_bool_value (PRM_ID_PTHREAD_SCOPE_PROCESS) ? PTHREAD_SCOPE_PROCESS :
			   PTHREAD_SCOPE_SYSTEM);
#else /* AIX */
  rv = pthread_attr_setscope (&thread_attr, PTHREAD_SCOPE_SYSTEM);
#endif /* AIX */
  if (rv != 0)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_PTHREAD_ATTR_SETSCOPE, 0);
      return ER_CSS_PTHREAD_ATTR_SETSCOPE;
    }

#if defined(_POSIX_THREAD_ATTR_STACKSIZE)
  rv = pthread_attr_getstacksize (&thread_attr, &ts_size);
  if (ts_size != (size_t) prm_get_bigint_value (PRM_ID_THREAD_STACKSIZE))
    {
      rv = pthread_attr_setstacksize (&thread_attr, prm_get_bigint_value (PRM_ID_THREAD_STACKSIZE));
      if (rv != 0)
	{
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_PTHREAD_ATTR_SETSTACKSIZE, 0);
	  return ER_CSS_PTHREAD_ATTR_SETSTACKSIZE;
	}
    }
#endif /* _POSIX_THREAD_ATTR_STACKSIZE */

  rv = pthread_create (&master_reader_th, &thread_attr, hb_thread_master_reader, (void *) NULL);
  if (rv != 0)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_PTHREAD_CREATE, 0);
      return ER_CSS_PTHREAD_CREATE;
    }

  return (NO_ERROR);
#else
  return ER_FAILED;
#endif
}

/*
* hb_process_init () -
*   return: NO_ERROR or ER_FAILED
*
*   server_name(in):
*   log_path(in):
*   copylogdbyn(in):
*/
int
hb_process_init (const char *server_name, const char *log_path, HB_PROC_TYPE type)
{
#if !defined(SERVER_MODE)
  int error;
  static bool is_first = true;

  if (is_first == false)
    {
      return (NO_ERROR);
    }

  er_log_debug (ARG_FILE_LINE, "hb_process_init. (type:%s). \n", hb_type_to_str (type));

  if (hb_Exec_path[0] == '\0' || *(hb_Argv) == 0)
    {
      er_log_debug (ARG_FILE_LINE, "hb_Exec_path or hb_Argv is not set. \n");
      return (ER_FAILED);
    }

  hb_Conn = hb_connect_to_master (server_name, log_path, type);

  /* wait 1 sec */
  sleep (1);

  error = hb_register_to_master (hb_Conn, type);
  if (NO_ERROR != error)
    {
      er_log_debug (ARG_FILE_LINE, "hb_register_to_master failed. \n");
      return (error);
    }

  error = hb_create_master_reader ();
  if (NO_ERROR != error)
    {
      er_log_debug (ARG_FILE_LINE, "hb_create_master_reader failed. \n");
      return (error);
    }

  is_first = false;
  return (NO_ERROR);
#else
  return (ER_FAILED);
#endif
}


/*
* hb_process_term () -
*   return: none
*
*   type(in):
*/
void
hb_process_term (void)
{
  if (hb_Conn)
    {
      css_shutdown_conn (hb_Conn);
      hb_Conn = NULL;
    }
  hb_Proc_shutdown = true;
}

/*
 * hb_node_state_string -
 *   return: node state sring
*
 *   nstate(in):
 */
const char *
hb_node_state_string (HB_NODE_STATE_TYPE nstate)
{
  switch (nstate)
    {
    case HB_NSTATE_UNKNOWN:
      return HB_NSTATE_UNKNOWN_STR;
    case HB_NSTATE_SLAVE:
      return HB_NSTATE_SLAVE_STR;
    case HB_NSTATE_TO_BE_MASTER:
      return HB_NSTATE_TO_BE_MASTER_STR;
    case HB_NSTATE_TO_BE_SLAVE:
      return HB_NSTATE_TO_BE_SLAVE_STR;
    case HB_NSTATE_MASTER:
      return HB_NSTATE_MASTER_STR;
    case HB_NSTATE_REPLICA:
      return HB_NSTATE_REPLICA_STR;

    default:
      return "invalid";
    }
}
