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
/*
 * csql_wire.c - thin csql transport (wf122/B5 D6R); see csql_wire.h
 */

#ident "$Id$"

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "csql_wire.h"

#include "cas_protocol.h"
#include "db_client_type.hpp"
#include "environment_variable.h"
#include "error_code.h"

#define ADOPTION_PROTOCOL_ONLY
#include "adoption.hpp"

#include <pthread.h>
#include <semaphore.h>
#include <signal.h>

#define WIRE_ERR_MSG_MAX 2048
#define WIRE_DEFAULT_BROKER_PORT 33000
/* server render cap is 32 MiB (CSQL_CAPTURE_LIMIT); allow framing headroom */
#define WIRE_REPLY_LIMIT (64 * 1024 * 1024)

/* single-connection client state (the csql process holds one session) */
static int wire_Fd = -1;
static unsigned int wire_Token = 0;
static bool wire_Local = false;
static char wire_Host[256];
static int wire_Port = 0;
static char wire_Db[64];
static char wire_User[64];
static char wire_Passwd[128];
static int wire_Client_type = 0;

static int wire_Err_code = 0;
static char wire_Err_msg[WIRE_ERR_MSG_MAX];
static bool wire_Tran_dirty = false;

static bool wire_Time_on = true;
static bool wire_Echo_on = false;
static bool wire_Trace_on = false;
static bool wire_Interactive = false;

/* SIGINT cancel plumbing (PR 7837 review): the signal handler may only call
 * async-signal-safe functions, so it posts a semaphore and this dedicated
 * thread performs the socket work.  The main thread is parked inside the
 * blocked roundtrip while a cancel is in flight, so wire_Fd/wire_Token are
 * stable when the thread reads them. */
static sem_t wire_Cancel_sem;
static volatile sig_atomic_t wire_Cancel_thread_up = 0;

static void wire_cancel_send (void);

static void *
wire_cancel_thread_run (void *arg)
{
  (void) arg;
  for (;;)
    {
      while (sem_wait (&wire_Cancel_sem) != 0 && errno == EINTR)
	;
      wire_cancel_send ();
    }
  return NULL;
}

static void
wire_cancel_thread_start (void)
{
  pthread_t tid;

  if (wire_Cancel_thread_up)
    {
      return;
    }
  if (sem_init (&wire_Cancel_sem, 0, 0) != 0)
    {
      return;
    }
  pthread_attr_t attr;
  pthread_attr_init (&attr);
  pthread_attr_setdetachstate (&attr, PTHREAD_CREATE_DETACHED);
  if (pthread_create (&tid, &attr, wire_cancel_thread_run, NULL) == 0)
    {
      wire_Cancel_thread_up = 1;
    }
  pthread_attr_destroy (&attr);
}

/* ------------------------------------------------------------------ */
/* low-level i/o                                                      */
/* ------------------------------------------------------------------ */

static int
wire_read_exact (int fd, void *buf, size_t n)
{
  char *p = (char *) buf;
  while (n > 0)
    {
      ssize_t r = read (fd, p, n);
      if (r < 0)
	{
	  if (errno == EINTR)
	    {
	      continue;
	    }
	  return ER_FAILED;
	}
      if (r == 0)
	{
	  return ER_FAILED;
	}
      p += r;
      n -= (size_t) r;
    }
  return NO_ERROR;
}

static int
wire_write_exact (int fd, const void *buf, size_t n)
{
  const char *p = (const char *) buf;
  while (n > 0)
    {
      ssize_t w = write (fd, p, n);
      if (w < 0)
	{
	  if (errno == EINTR)
	    {
	      continue;
	    }
	  return ER_FAILED;
	}
      p += w;
      n -= (size_t) w;
    }
  return NO_ERROR;
}

/* explicit truncating copy (keeps -Werror=stringop-truncation quiet) */
static void
wire_copy (char *dst, size_t dst_size, const char *src)
{
  size_t n = strlen (src);
  if (n >= dst_size)
    {
      n = dst_size - 1;
    }
  memcpy (dst, src, n);
  dst[n] = '\0';
}

static unsigned int
wire_get_be32 (const char *p)
{
  return ((unsigned int) (unsigned char) p[0] << 24) | ((unsigned int) (unsigned char) p[1] << 16)
    | ((unsigned int) (unsigned char) p[2] << 8) | (unsigned int) (unsigned char) p[3];
}

static void
wire_put_be32 (char *p, unsigned int v)
{
  p[0] = (char) (v >> 24);
  p[1] = (char) (v >> 16);
  p[2] = (char) (v >> 8);
  p[3] = (char) v;
}

static void
wire_set_error (int code, const char *msg)
{
  wire_Err_code = code;
  if (msg != NULL)
    {
      wire_copy (wire_Err_msg, sizeof (wire_Err_msg), msg);
    }
  else
    {
      wire_Err_msg[0] = '\0';
    }
}

static void
wire_close_fd (void)
{
  if (wire_Fd >= 0)
    {
      close (wire_Fd);
      wire_Fd = -1;
    }
  wire_Token = 0;
}

/* ------------------------------------------------------------------ */
/* connect                                                            */
/* ------------------------------------------------------------------ */

static void
wire_build_driver_header (char *header)
{
  memset (header, 0, cubconn::adoption::DRIVER_HEADER_SIZE);
  memcpy (header, "CUBRK", 5);
  header[5] = (char) CAS_CLIENT_CCI;
  header[6] = (char) (CAS_PROTO_INDICATOR | CURRENT_PROTOCOL);
  header[7] = (char) (BROKER_RENEWED_ERROR_CODE | BROKER_SUPPORT_HOLDABLE_RESULT);
}

static void
wire_build_db_info (char *buf, const char *db, const char *user, const char *passwd)
{
  memset (buf, 0, cubconn::adoption::DRIVER_DB_INFO_SIZE);
  wire_copy (buf, SRV_CON_DBNAME_SIZE, db);
  wire_copy (buf + SRV_CON_DBNAME_SIZE, SRV_CON_DBUSER_SIZE, user != NULL ? user : "");
  wire_copy (buf + SRV_CON_DBNAME_SIZE + SRV_CON_DBUSER_SIZE, SRV_CON_DBPASSWD_SIZE, passwd != NULL ? passwd : "");
  wire_copy (buf + SRV_CON_DBNAME_SIZE + SRV_CON_DBUSER_SIZE + SRV_CON_DBPASSWD_SIZE, SRV_CON_URL_SIZE, "thin_csql");
}

static void
wire_adoption_socket_path (const char *db, char *out, size_t out_size)
{
  const char *tmp = envvar_get ("TMP");
  if (tmp == NULL || tmp[0] == '\0')
    {
      tmp = "/tmp";
    }
  snprintf (out, out_size, "%s/%s_adopt_%s", tmp, envvar_prefix (), db);
}

/* parse the CAS connect reply already positioned after the 4-byte length;
 * returns NO_ERROR and sets wire_Token, or maps the error frame */
static int
wire_read_connect_reply (int fd, int length)
{
  char cas_info[4];
  if (wire_read_exact (fd, cas_info, 4) != NO_ERROR)
    {
      wire_set_error (ER_FAILED, "connection closed during connect");
      return ER_FAILED;
    }
  if (length < 0 || length != CAS_CONNECTION_REPLY_SIZE)
    {
      /* error frame: [indicator][code][msg].  The server-side admission
       * reject (driver_session send_error_reply) encodes the same frame
       * with a POSITIVE length, so any size that is not the 36-byte
       * connect reply is decoded as an error, not dropped (wf143 HA gate:
       * a standby's rejection used to surface as "unexpected reply size") */
      char head[8];
      int body_len = length < 0 ? -length : length;
      if (body_len < 8 || wire_read_exact (fd, head, 8) != NO_ERROR)
	{
	  wire_set_error (ER_FAILED, "malformed connect error frame");
	  return ER_FAILED;
	}
      int code = (int) wire_get_be32 (head + 4);
      int msg_len = body_len - 8;
      char *msg = NULL;
      if (msg_len > 0)
	{
	  msg = (char *) malloc ((size_t) msg_len + 1);
	  if (msg != NULL && wire_read_exact (fd, msg, (size_t) msg_len) == NO_ERROR)
	    {
	      msg[msg_len] = '\0';
	    }
	  else if (msg != NULL)
	    {
	      msg[0] = '\0';
	    }
	}
      wire_set_error (code, msg != NULL ? msg : "connect refused");
      free (msg);
      return code != 0 ? code : ER_FAILED;
    }

  char body[CAS_CONNECTION_REPLY_SIZE];
  if (wire_read_exact (fd, body, sizeof (body)) != NO_ERROR)
    {
      wire_set_error (ER_FAILED, "connection closed during connect reply");
      return ER_FAILED;
    }
  wire_Token = wire_get_be32 (body);
  return NO_ERROR;
}

/* the connect reply length word doubles as the discriminator: a local
 * DIRECT_CONNECT refusal arrives as an adoption HANDOFF_REJECT frame whose
 * first bytes are the "ADOP" magic in host order */
static int
wire_finish_connect (int fd)
{
  char first4[4];
  if (wire_read_exact (fd, first4, 4) != NO_ERROR)
    {
      wire_set_error (ER_FAILED, "connection closed during connect");
      return ER_FAILED;
    }

  unsigned int host_word;
  memcpy (&host_word, first4, 4);
  if (host_word == cubconn::adoption::PROTO_MAGIC)
    {
      char rest[8];
      int reason = 0;
      if (wire_read_exact (fd, rest, 8) == NO_ERROR)
	{
	  unsigned int len;
	  memcpy (&len, rest + 4, 4);
	  if (len == 4)
	    {
	      char rb[4];
	      if (wire_read_exact (fd, rb, 4) == NO_ERROR)
		{
		  memcpy (&reason, rb, 4);
		}
	    }
	}
      switch ((cubconn::adoption::reject_reason) reason)
	{
	case cubconn::adoption::reject_reason::CLIENTS_EXCEEDED:
	  wire_set_error (ER_CSS_CLIENTS_EXCEEDED, "too many clients");
	  break;
	case cubconn::adoption::reject_reason::DBNAME_MISMATCH:
	  wire_set_error (ER_FAILED, "database name does not match this server");
	  break;
	case cubconn::adoption::reject_reason::NOT_AUTHORIZED:
	  wire_set_error (ER_FAILED, "direct connection refused (not authorized)");
	  break;
	default:
	  wire_set_error (ER_FAILED, "direct connection refused");
	  break;
	}
      return ER_FAILED;
    }

  return wire_read_connect_reply (fd, (int) wire_get_be32 (first4));
}

static int
wire_connect_local (const char *db, const char *user, const char *passwd, int client_type)
{
  char path[512];
  wire_adoption_socket_path (db, path, sizeof (path));

  int fd = socket (AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0)
    {
      wire_set_error (ER_FAILED, "cannot create socket");
      return ER_FAILED;
    }
  struct sockaddr_un addr;
  memset (&addr, 0, sizeof (addr));
  addr.sun_family = AF_UNIX;
  wire_copy (addr.sun_path, sizeof (addr.sun_path), path);
  if (connect (fd, (struct sockaddr *) &addr, sizeof (addr)) != 0)
    {
      close (fd);
      wire_set_error (ER_FAILED, "cannot connect to the server adoption socket (is the server running?)");
      return ER_FAILED;
    }

  cubconn::adoption::msg_header header;
  cubconn::adoption::direct_connect_body body;
  memset (&body, 0, sizeof (body));
  header.magic = cubconn::adoption::PROTO_MAGIC;
  header.op = (unsigned int) cubconn::adoption::msg_op::DIRECT_CONNECT;
  header.length = (unsigned int) sizeof (body);
  body.client_type = (unsigned char) client_type;
  wire_build_driver_header (body.driver_header);
  wire_build_db_info (body.db_info, db, user, passwd);

  if (wire_write_exact (fd, &header, sizeof (header)) != NO_ERROR
      || wire_write_exact (fd, &body, sizeof (body)) != NO_ERROR)
    {
      close (fd);
      wire_set_error (ER_FAILED, "cannot send direct connect frame");
      return ER_FAILED;
    }

  int err = wire_finish_connect (fd);
  if (err != NO_ERROR)
    {
      close (fd);
      return err;
    }
  wire_Fd = fd;
  wire_Local = true;
  return NO_ERROR;
}

static int
wire_connect_broker (const char *host, int port, const char *db, const char *user, const char *passwd)
{
  struct addrinfo hints, *res = NULL;
  char port_str[16];

  memset (&hints, 0, sizeof (hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  snprintf (port_str, sizeof (port_str), "%d", port);
  if (getaddrinfo (host, port_str, &hints, &res) != 0 || res == NULL)
    {
      wire_set_error (ER_FAILED, "cannot resolve broker host");
      return ER_FAILED;
    }
  int fd = socket (res->ai_family, res->ai_socktype, res->ai_protocol);
  if (fd < 0 || connect (fd, res->ai_addr, res->ai_addrlen) != 0)
    {
      if (fd >= 0)
	{
	  close (fd);
	}
      freeaddrinfo (res);
      wire_set_error (ER_FAILED, "cannot connect to the broker");
      return ER_FAILED;
    }
  freeaddrinfo (res);

  char header[cubconn::adoption::DRIVER_HEADER_SIZE];
  wire_build_driver_header (header);
  if (wire_write_exact (fd, header, sizeof (header)) != NO_ERROR)
    {
      close (fd);
      wire_set_error (ER_FAILED, "cannot send driver header");
      return ER_FAILED;
    }
  char ack[4];
  if (wire_read_exact (fd, ack, 4) != NO_ERROR)
    {
      close (fd);
      wire_set_error (ER_FAILED, "broker closed the connection");
      return ER_FAILED;
    }
  int ack_val = (int) wire_get_be32 (ack);
  if (ack_val != 0)
    {
      /* the broker's port-redirect dialect is not carried into the thin
       * csql (DIRECT_HANDOFF brokers always answer 0) */
      close (fd);
      wire_set_error (ack_val < 0 ? ack_val : ER_FAILED, "broker refused the connection");
      return ER_FAILED;
    }

  char db_info[cubconn::adoption::DRIVER_DB_INFO_SIZE];
  wire_build_db_info (db_info, db, user, passwd);
  if (wire_write_exact (fd, db_info, sizeof (db_info)) != NO_ERROR)
    {
      close (fd);
      wire_set_error (ER_FAILED, "cannot send db info");
      return ER_FAILED;
    }

  char len4[4];
  if (wire_read_exact (fd, len4, 4) != NO_ERROR)
    {
      close (fd);
      wire_set_error (ER_FAILED, "connection closed during connect");
      return ER_FAILED;
    }
  int err = wire_read_connect_reply (fd, (int) wire_get_be32 (len4));
  if (err != NO_ERROR)
    {
      close (fd);
      return err;
    }
  wire_Fd = fd;
  wire_Local = false;
  return NO_ERROR;
}

int
csql_wire_connect (const char *db_name, const char *user_name, const char *passwd, int client_type)
{
  char db[64];
  char host[256];
  const char *at;

  wire_close_fd ();
  wire_set_error (0, NULL);
  wire_Tran_dirty = false;

  if (db_name == NULL || db_name[0] == '\0')
    {
      wire_set_error (ER_FAILED, "no database name");
      return ER_FAILED;
    }
  at = strchr (db_name, '@');
  if (at != NULL)
    {
      size_t n = (size_t) (at - db_name);
      if (n >= sizeof (db))
	{
	  n = sizeof (db) - 1;
	}
      memcpy (db, db_name, n);
      db[n] = '\0';
      wire_copy (host, sizeof (host), at + 1);
    }
  else
    {
      wire_copy (db, sizeof (db), db_name);
      host[0] = '\0';
    }

  int err;
  if (host[0] == '\0' || strcmp (host, "localhost") == 0)
    {
      err = wire_connect_local (db, user_name, passwd, client_type);
    }
  else
    {
      /* codex review #3: the broker handoff synthesizes the client type from
       * ACCESS_MODE (a broker property), so a remote thin csql cannot carry
       * --read-only / --sysadm / --skip-vacuum semantics.  Rather than let
       * those modes silently degrade to a plain RW broker session, refuse
       * them for db@host until a distinct thin-csql type flows through the
       * handoff (B5 fog item).  Plain csql is unaffected. */
      if (client_type != DB_CLIENT_TYPE_CSQL)
	{
	  wire_set_error (ER_FAILED,
			  "--sysadm / --read-only / --skip-vacuum are not supported for a remote "
			  "connection (db@host); use them on a local connection");
	  return ER_FAILED;
	}
      const char *env = getenv ("CUBRID_CSQL_BROKER_PORT");
      int port = (env != NULL && atoi (env) > 0) ? atoi (env) : WIRE_DEFAULT_BROKER_PORT;
      err = wire_connect_broker (host, port, db, user_name, passwd);
      wire_Port = port;
    }
  if (err != NO_ERROR)
    {
      return err;
    }

  wire_copy (wire_Db, sizeof (wire_Db), db);
  wire_copy (wire_Host, sizeof (wire_Host), host);
  wire_copy (wire_User, sizeof (wire_User), user_name != NULL ? user_name : "");
  wire_copy (wire_Passwd, sizeof (wire_Passwd), passwd != NULL ? passwd : "");
  wire_Client_type = client_type;
  wire_cancel_thread_start ();
  return NO_ERROR;
}

void
csql_wire_disconnect (void)
{
  wire_close_fd ();
}

bool
csql_wire_is_connected (void)
{
  return wire_Fd >= 0;
}

int
csql_wire_last_error (char **msg)
{
  if (msg != NULL)
    {
      *msg = wire_Err_msg;
    }
  return wire_Err_code;
}

bool
csql_wire_tran_dirty (void)
{
  return wire_Tran_dirty;
}

void
csql_wire_set_time_on (bool on)
{
  wire_Time_on = on;
}

void
csql_wire_set_echo_on (bool on)
{
  wire_Echo_on = on;
}

void
csql_wire_set_trace (bool on)
{
  wire_Trace_on = on;
}

void
csql_wire_set_interactive (bool on)
{
  wire_Interactive = on;
}

/* ------------------------------------------------------------------ */
/* CAS_FC_CSQL_REQUEST                                                */
/* ------------------------------------------------------------------ */

typedef struct
{
  char *buf;
  size_t len;
  size_t cap;
} wire_body;

static int
wire_body_append (wire_body * b, const void *data, size_t n)
{
  if (b->len + n > b->cap)
    {
      size_t new_cap = (b->cap == 0) ? 4096 : b->cap;
      while (b->len + n > new_cap)
	{
	  new_cap *= 2;
	}
      char *nb = (char *) realloc (b->buf, new_cap);
      if (nb == NULL)
	{
	  return ER_FAILED;
	}
      b->buf = nb;
      b->cap = new_cap;
    }
  memcpy (b->buf + b->len, data, n);
  b->len += n;
  return NO_ERROR;
}

static int
wire_arg_int (wire_body * b, int v)
{
  char tmp[8];
  wire_put_be32 (tmp, 4);
  wire_put_be32 (tmp + 4, (unsigned int) v);
  return wire_body_append (b, tmp, 8);
}

static int
wire_arg_char (wire_body * b, char c)
{
  char tmp[5];
  wire_put_be32 (tmp, 1);
  tmp[4] = c;
  return wire_body_append (b, tmp, 5);
}

static int
wire_arg_str (wire_body * b, const char *s)
{
  size_t n = strlen (s) + 1;
  char tmp[4];
  wire_put_be32 (tmp, (unsigned int) n);
  if (wire_body_append (b, tmp, 4) != NO_ERROR)
    {
      return ER_FAILED;
    }
  return wire_body_append (b, s, n);
}

static int
wire_flags_from_arg (const CSQL_ARGUMENT * a)
{
  int flags = 0;
  if (a->auto_commit)
    flags |= CAS_CSQL_FLAG_AUTO_COMMIT;
  if (a->continue_on_error)
    flags |= CAS_CSQL_FLAG_CONTINUE_ON_ERROR;
  if (a->plain_output)
    flags |= CAS_CSQL_FLAG_PLAIN_OUTPUT;
  if (a->skip_column_names)
    flags |= CAS_CSQL_FLAG_SKIP_COLUMN_NAMES;
  if (a->line_output)
    flags |= CAS_CSQL_FLAG_LINE_OUTPUT;
  if (a->query_output)
    flags |= CAS_CSQL_FLAG_QUERY_OUTPUT;
  if (a->loaddb_output)
    flags |= CAS_CSQL_FLAG_LOADDB_OUTPUT;
  if (wire_Echo_on)
    flags |= CAS_CSQL_FLAG_ECHO;
  if (wire_Time_on)
    flags |= CAS_CSQL_FLAG_TIME_ON;
  if (wire_Trace_on)
    flags |= CAS_CSQL_FLAG_QUERY_TRACE;
  if (a->sysadm)
    flags |= CAS_CSQL_FLAG_SYSADM;
  if (a->write_on_standby)
    flags |= CAS_CSQL_FLAG_WRITE_ON_STANDBY;
  if (a->midxkey_print)
    flags |= CAS_CSQL_FLAG_MIDXKEY_PRINT;
  if (a->pl_server_output)
    flags |= CAS_CSQL_FLAG_PL_SERVER_OUTPUT;
  if (a->trigger_action_flag)
    flags |= CAS_CSQL_FLAG_TRIGGER_ACTION;
  if (a->single_line_execution)
    flags |= CAS_CSQL_FLAG_SINGLE_LINE;
  if (a->read_only)
    flags |= CAS_CSQL_FLAG_READ_ONLY;
  if (wire_Interactive)
    flags |= CAS_CSQL_FLAG_INTERACTIVE;
  return flags;
}

/* send the assembled body and replay the reply; returns the reply status or
 * a negative code on a wire/server error */
static int
wire_roundtrip (wire_body * b)
{
  char head[8];
  int status = ER_FAILED;

  if (wire_Fd < 0)
    {
      wire_set_error (ER_FAILED, "not connected");
      return ER_FAILED;
    }

  wire_put_be32 (head, (unsigned int) b->len);
  memset (head + 4, 0xff, 4);
  if (wire_write_exact (wire_Fd, head, 8) != NO_ERROR || wire_write_exact (wire_Fd, b->buf, b->len) != NO_ERROR)
    {
      wire_set_error (ER_FAILED, "cannot send request (server connection lost)");
      wire_close_fd ();
      return ER_FAILED;
    }

  char rh[8];
  if (wire_read_exact (wire_Fd, rh, 8) != NO_ERROR)
    {
      wire_set_error (ER_FAILED, "server connection lost");
      wire_close_fd ();
      return ER_FAILED;
    }
  /* codex review #5: bound the reply — a hostile/broken endpoint must not be
   * able to force an unbounded malloc.  The server caps its render at 32 MiB
   * (CSQL_CAPTURE_LIMIT); allow generous framing headroom above that. */
  int length = (int) wire_get_be32 (rh);
  if (length < 4 || length > WIRE_REPLY_LIMIT)
    {
      wire_set_error (ER_FAILED, "malformed reply");
      wire_close_fd ();
      return ER_FAILED;
    }
  char *reply = (char *) malloc ((size_t) length);
  if (reply == NULL || wire_read_exact (wire_Fd, reply, (size_t) length) != NO_ERROR)
    {
      free (reply);
      wire_set_error (ER_FAILED, "server connection lost");
      wire_close_fd ();
      return ER_FAILED;
    }

  int first = (int) wire_get_be32 (reply);
  if (first < 0)
    {
      /* error frame: [indicator][code][msg]; msg is not guaranteed
       * NUL-terminated, so copy it with an explicit bounded length */
      int code = (length >= 8) ? (int) wire_get_be32 (reply + 4) : ER_FAILED;
      if (length > 8)
	{
	  int mlen = length - 8;
	  char tmp[WIRE_ERR_MSG_MAX];
	  int n = (mlen < (int) sizeof (tmp) - 1) ? mlen : (int) sizeof (tmp) - 1;
	  memcpy (tmp, reply + 8, (size_t) n);
	  tmp[n] = '\0';
	  wire_set_error (code, tmp);
	}
      else
	{
	  wire_set_error (code, "server error");
	}
      free (reply);
      return code < 0 ? code : ER_FAILED;
    }
  if (length < 9)
    {
      free (reply);
      wire_set_error (ER_FAILED, "short reply");
      wire_close_fd ();
      return ER_FAILED;
    }
  status = (int) wire_get_be32 (reply + 4);
  wire_Tran_dirty = (reply[8] != 0);

  /* replay ordered out/err chunks (size_t arithmetic: no signed overflow) */
  size_t pos = 9;
  size_t ulength = (size_t) length;
  bool saw_end = false;
  bool framing_error = false;
  while (pos < ulength)
    {
      int tag = (unsigned char) reply[pos];
      pos += 1;
      if (tag == CAS_CSQL_CHUNK_END)
	{
	  saw_end = true;
	  break;
	}
      if (tag != CAS_CSQL_CHUNK_OUT && tag != CAS_CSQL_CHUNK_ERR)
	{
	  framing_error = true;
	  break;
	}
      if (pos + 4 > ulength)
	{
	  framing_error = true;
	  break;
	}
      int clen_i = (int) wire_get_be32 (reply + pos);
      pos += 4;
      if (clen_i < 0 || (size_t) clen_i > ulength - pos)
	{
	  framing_error = true;
	  break;
	}
      int clen = clen_i;
      FILE *fp = (tag == CAS_CSQL_CHUNK_ERR) ? csql_Error_fp : csql_Output_fp;
      if (fp != NULL && clen > 0)
	{
	  /* server text is UTF-8; apply the console conversion the fat
	   * client would have applied while rendering */
	  if (csql_text_utf8_to_console != NULL)
	    {
	      char *con_buf = NULL;
	      int con_size = 0;
	      if ((*csql_text_utf8_to_console) (reply + pos, clen, &con_buf, &con_size) == NO_ERROR && con_buf != NULL)
		{
		  fwrite (con_buf, 1, (size_t) con_size, fp);
		  free (con_buf);
		}
	      else
		{
		  fwrite (reply + pos, 1, (size_t) clen, fp);
		}
	    }
	  else
	    {
	      fwrite (reply + pos, 1, (size_t) clen, fp);
	    }
	}
      pos += (size_t) clen;
    }
  if (csql_Output_fp != NULL)
    {
      fflush (csql_Output_fp);
    }
  if (csql_Error_fp != NULL)
    {
      fflush (csql_Error_fp);
    }

  free (reply);

  /* a truncated/garbled frame must not read as a successful statement */
  if (framing_error || !saw_end)
    {
      wire_set_error (ER_FAILED, "malformed reply framing");
      wire_close_fd ();
      return ER_FAILED;
    }
  return status;
}

int
csql_wire_execute (const CSQL_ARGUMENT * csql_arg, int input_type, int line_no, const char *text)
{
  wire_body b = { NULL, 0, 0 };
  char fc = (char) CAS_FC_CSQL_REQUEST;
  char widths[4096];

  csql_column_widths_serialize (widths, sizeof (widths));

  if (wire_body_append (&b, &fc, 1) != NO_ERROR
      || wire_arg_int (&b, CAS_CSQL_SUB_EXECUTE) != NO_ERROR
      || wire_arg_int (&b, wire_flags_from_arg (csql_arg)) != NO_ERROR
      || wire_arg_int (&b, input_type) != NO_ERROR
      || wire_arg_int (&b, line_no) != NO_ERROR || wire_arg_int (&b, csql_arg->string_width) != NO_ERROR)
    {
      free (b.buf);
      wire_set_error (ER_FAILED, "out of memory");
      return ER_FAILED;
    }
  char delims[3] = { csql_arg->column_delimiter, csql_arg->column_enclosure, '\0' };
  if (csql_arg->column_delimiter == -1 && csql_arg->column_enclosure == -1)
    {
      delims[0] = '\0';
    }
  if (wire_arg_str (&b, delims) != NO_ERROR
      || wire_arg_str (&b, widths) != NO_ERROR
      || wire_arg_str (&b, csql_arg->in_file_name != NULL ? csql_arg->in_file_name : "") != NO_ERROR
      || wire_arg_str (&b, text) != NO_ERROR)
    {
      free (b.buf);
      wire_set_error (ER_FAILED, "out of memory");
      return ER_FAILED;
    }

  int status = wire_roundtrip (&b);
  free (b.buf);
  return status;
}

int
csql_wire_session_cmd (const CSQL_ARGUMENT * csql_arg, const char *line)
{
  wire_body b = { NULL, 0, 0 };
  char fc = (char) CAS_FC_CSQL_REQUEST;
  char widths[4096];

  csql_column_widths_serialize (widths, sizeof (widths));

  if (wire_body_append (&b, &fc, 1) != NO_ERROR
      || wire_arg_int (&b, CAS_CSQL_SUB_SESSION_CMD) != NO_ERROR
      || wire_arg_int (&b, wire_flags_from_arg (csql_arg)) != NO_ERROR
      || wire_arg_int (&b, csql_arg->string_width) != NO_ERROR
      || wire_arg_str (&b, widths) != NO_ERROR || wire_arg_str (&b, line) != NO_ERROR)
    {
      free (b.buf);
      wire_set_error (ER_FAILED, "out of memory");
      return ER_FAILED;
    }

  int status = wire_roundtrip (&b);
  free (b.buf);
  return status;
}

int
csql_wire_tran (char op)
{
  wire_body b = { NULL, 0, 0 };
  char fc = (char) CAS_FC_CSQL_REQUEST;

  if (wire_body_append (&b, &fc, 1) != NO_ERROR
      || wire_arg_int (&b, CAS_CSQL_SUB_TRAN) != NO_ERROR || wire_arg_char (&b, op) != NO_ERROR)
    {
      free (b.buf);
      wire_set_error (ER_FAILED, "out of memory");
      return ER_FAILED;
    }

  int status = wire_roundtrip (&b);
  free (b.buf);
  return status;
}

/* ------------------------------------------------------------------ */
/* cancel                                                             */
/* ------------------------------------------------------------------ */

/* async-signal-safe: called from the SIGINT handler */
void
csql_wire_cancel (void)
{
  if (wire_Cancel_thread_up)
    {
      (void) sem_post (&wire_Cancel_sem);
    }
}

static void
wire_cancel_send (void)
{
  if (wire_Fd < 0 || wire_Token == 0)
    {
      return;
    }

  if (wire_Local)
    {
      char path[512];
      wire_adoption_socket_path (wire_Db, path, sizeof (path));
      int fd = socket (AF_UNIX, SOCK_STREAM, 0);
      if (fd < 0)
	{
	  return;
	}
      struct sockaddr_un addr;
      memset (&addr, 0, sizeof (addr));
      addr.sun_family = AF_UNIX;
      wire_copy (addr.sun_path, sizeof (addr.sun_path), path);
      if (connect (fd, (struct sockaddr *) &addr, sizeof (addr)) == 0)
	{
	  cubconn::adoption::msg_header header;
	  cubconn::adoption::token_body body;
	  header.magic = cubconn::adoption::PROTO_MAGIC;
	  header.op = (unsigned int) cubconn::adoption::msg_op::CANCEL;
	  header.length = (unsigned int) sizeof (body);
	  body.token = wire_Token;
	  (void) wire_write_exact (fd, &header, sizeof (header));
	  (void) wire_write_exact (fd, &body, sizeof (body));
	}
      close (fd);
    }
  else
    {
      /* legacy "QC" cancel packet to the broker port (B1) */
      struct addrinfo hints, *res = NULL;
      char port_str[16];
      memset (&hints, 0, sizeof (hints));
      hints.ai_family = AF_INET;
      hints.ai_socktype = SOCK_STREAM;
      snprintf (port_str, sizeof (port_str), "%d", wire_Port);
      if (getaddrinfo (wire_Host, port_str, &hints, &res) != 0 || res == NULL)
	{
	  return;
	}
      int fd = socket (res->ai_family, res->ai_socktype, res->ai_protocol);
      if (fd >= 0 && connect (fd, res->ai_addr, res->ai_addrlen) == 0)
	{
	  struct sockaddr_in local;
	  socklen_t local_len = sizeof (local);
	  unsigned short port = 0;
	  if (getsockname (wire_Fd, (struct sockaddr *) &local, &local_len) == 0)
	    {
	      port = ntohs (local.sin_port);
	    }
	  char msg[10];
	  msg[0] = 'Q';
	  msg[1] = 'C';
	  wire_put_be32 (msg + 2, wire_Token);
	  msg[6] = (char) (port >> 8);
	  msg[7] = (char) port;
	  msg[8] = 0;
	  msg[9] = 0;
	  (void) wire_write_exact (fd, msg, sizeof (msg));
	  char reply[4];
	  (void) wire_read_exact (fd, reply, 4);
	}
      if (fd >= 0)
	{
	  close (fd);
	}
      if (res != NULL)
	{
	  freeaddrinfo (res);
	}
    }
}
