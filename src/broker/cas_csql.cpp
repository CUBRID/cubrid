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
 * cas_csql.cpp - CAS_FC_CSQL_REQUEST: server-rendered csql execution
 *
 * wf122/B5 (thin csql).  A thin csql ships each statement buffer or
 * server-dependent session command here; the folded csql body renders the
 * result text inside the session bracket exactly as the fat client would,
 * and the ordered stdout/stderr interleaving is returned as tagged chunks.
 */

#ident "$Id$"

#include <poll.h>
#include <sys/eventfd.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <new>
#include <string>
#include <system_error>
#include <thread>
#include <vector>
#include <utility>

#include "cas.h"
#include "cas_common.h"
#include "cas_common_vars.h"
#include "cas_function.h"
#include "cas_network.h"
#include "cas_net_buf.h"
#include "cas_log.h"
#include "cas_error.h"
#include "cas_protocol.h"
#include "error_manager.h"
#include "language_support.h"
#include "log_impl.h"
#include "object_representation.h"
#include "system_parameter.h"
#include "thread_entry.hpp"
#include "thread_manager.hpp"

#include "csql.h"
#include "adoption.hpp"
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

typedef std::vector<std::pair<char, std::string>> csql_chunk_vec;

/* codex review #2: cap the server-side render capture so a query with
 * gigantic printable output cannot exhaust cub_server memory before a byte
 * is sent.  32 MiB is far above any legitimate csql result page; on overflow
 * the request becomes a controlled statement error. */
#define CSQL_CAPTURE_LIMIT (32 * 1024 * 1024)
/* error-log records only feed the client's csql.err, which the fat client wrote without failing the
 * statement; past this share of the cap further records are dropped (a PL loop that catches 100000
 * NO_DATA_FOUND errors must still run, shell cbrd_26555) */
#define CSQL_CAPTURE_LOG_LIMIT (4 * 1024 * 1024)
/* past this much render the captured chunks go to the client in a partial frame, as the fat client
 * streamed a result it fetched, instead of piling up in the server (shell cbrd_20654: 800 MB of rows) */
#define CSQL_CAPTURE_FLUSH (1024 * 1024)

typedef struct
{
  csql_chunk_vec *chunks;
  size_t *total;
  bool *overflow;
  char tag;
  size_t log_bytes;		/* CAS_CSQL_CHUNK_LOG only: what the records took so far */
  SOCKET fd;			/* the client, for partial frames */
} csql_capture_cookie;

static thread_local csql_capture_cookie *csql_Error_cookie = NULL;

/* A CS csql that died mid-statement had its transaction interrupted by the server's connection handler
 * (css_connection_handler_thread), so neither its work nor its auto-commit survived. The folded statement
 * runs on the client's own session thread, which reads the socket only between requests: watch it for the
 * hang-up while a request executes. */
class csql_hangup_watch
{
  public:
    csql_hangup_watch (SOCKET fd, int tran_index)
      : m_wake (eventfd (0, EFD_CLOEXEC))
    {
      if (m_wake < 0)
	{
	  return;		/* no watch: the statement runs as it would without it */
	}
      try
	{
	  m_thread = std::thread ([fd, tran_index, wake = m_wake] ()
	  {
	    struct pollfd pfd[2] = { { fd, POLLRDHUP, 0 }, { wake, POLLIN, 0 } };

	    if (poll (pfd, 2, -1) > 0 && (pfd[0].revents & (POLLRDHUP | POLLHUP | POLLERR)) != 0)
	      {
		interrupt (tran_index);
	      }
	  });
	}
      catch (const std::system_error &)
	{
	}
    }

    ~csql_hangup_watch ()
    {
      if (m_thread.joinable ())
	{
	  (void) eventfd_write (m_wake, 1);
	  m_thread.join ();
	}
      if (m_wake >= 0)
	{
	  close (m_wake);
	}
    }

  private:
    /* the interrupt reaches engine code that needs a thread entry and its error context:
     * register this foreign thread the way the adoption control channel does */
    static void interrupt (int tran_index)
    {
      cubthread::entry *entry_p = cubthread::get_manager ()->claim_entry ();

      if (entry_p == NULL)
	{
	  return;
	}
      entry_p->register_id ();
      entry_p->type = TT_SERVER;
      entry_p->tran_index = -1;
      entry_p->m_status = cubthread::entry::status::TS_RUN;
      entry_p->shutdown = false;
      entry_p->get_error_context ().register_thread_local ();

      (void) logtb_set_tran_index_interrupt (NULL, tran_index, true);

      entry_p->tran_index = NULL_TRAN_INDEX;
      entry_p->m_status = cubthread::entry::status::TS_DEAD;
      entry_p->get_error_context ().deregister_thread_local ();
      entry_p->unregister_id ();
      cubthread::get_manager ()->retire_entry (*entry_p);
    }

    int m_wake;
    std::thread m_thread;
};

static void
csql_capture_error ()
{
  csql_capture_cookie *cap = csql_Error_cookie;
  if (cap == NULL || *cap->overflow)
    {
      return;
    }

  int err_id, severity, levels, line;
  const char *file, *msg;
  er_all (&err_id, &severity, &levels, &line, &file, &msg);
  size_t size = OR_INT_SIZE * 3 + strlen (msg) + 1;
  /* er_get_area_error substitutes "(null)" for an empty message. */
  if (msg[0] == '\0')
    {
      size += strlen ("(null)");
    }
  if (size > CSQL_CAPTURE_LOG_LIMIT - cap->log_bytes || size > CSQL_CAPTURE_LIMIT - *cap->total)
    {
      cap->log_bytes = CSQL_CAPTURE_LOG_LIMIT;
      return;
    }

  char *area = (char *) malloc (size);
  if (area == NULL)
    {
      *cap->overflow = true;
      return;
    }
  int length = (int) size;
  er_get_area_error (area, &length);
  try
    {
      /* Each record has its own frame; adjacent log records must not merge. */
      cap->chunks->emplace_back (CAS_CSQL_CHUNK_LOG, std::string (area, length));
      *cap->total += length;
      cap->log_bytes += length;
    }
  catch (const std::bad_alloc &)
    {
      *cap->overflow = true;
    }
  free_and_init (area);
}

class csql_error_capture_guard
{
  public:
    explicit csql_error_capture_guard (csql_capture_cookie *cookie)
      : m_previous_cookie (csql_Error_cookie)
      , m_previous_observer (er_register_error_observer (csql_capture_error))
    {
      csql_Error_cookie = cookie;
    }

    ~csql_error_capture_guard ()
    {
      er_register_error_observer (m_previous_observer);
      csql_Error_cookie = m_previous_cookie;
    }

  private:
    csql_capture_cookie *m_previous_cookie;
    er_error_observer_t m_previous_observer;
};

/* a frame of csql_reply_chunks' layout ending in CAS_CSQL_CHUNK_MORE; the final frame carries the status */
static bool
csql_send_partial (SOCKET fd, csql_chunk_vec &chunks)
{
  std::string frame (8, '\0');
  auto put_int = [&frame] (int v)
  {
    int n = htonl (v);
    frame.append ((const char *) &n, sizeof (n));
  };

  put_int (0);			/* result code */
  put_int (0);			/* status */
  frame.push_back (db_commit_is_needed ()? 1 : 0);
  for (const auto &c : chunks)
    {
      frame.push_back (c.first);
      put_int ((int) c.second.size ());
      frame.append (c.second);
    }
  frame.push_back (CAS_CSQL_CHUNK_MORE);
  int body_size = htonl ((int) frame.size () - 8);
  memcpy (&frame[0], &body_size, sizeof (body_size));
  frame[4] = CAS_INFO_STATUS_ACTIVE;
  frame[5] = frame[6] = frame[7] = CAS_INFO_RESERVED_DEFAULT;
  chunks.clear ();
  return net_write_stream (fd, frame.data (), (int) frame.size ()) == 0;
}

static ssize_t
csql_capture_write (void *cookie, const char *buf, size_t size)
{
  csql_capture_cookie *cap = (csql_capture_cookie *) cookie;

  if (size > 0 && !*cap->overflow)
    {
      if (*cap->total + size > CSQL_CAPTURE_FLUSH && !cap->chunks->empty () && !IS_INVALID_SOCKET (cap->fd))
	{
	  if (!csql_send_partial (cap->fd, *cap->chunks))
	    {
	      *cap->overflow = true;	/* the client is gone; the final reply fails the same way */
	      return (ssize_t) size;
	    }
	  *cap->total = 0;
	}
      if (*cap->total + size > CSQL_CAPTURE_LIMIT)
	{
	  *cap->overflow = true;
	  return (ssize_t) size;	/* swallow the rest; the handler reports the overflow */
	}
      /* std::string can still throw bad_alloc; the fn_csql_request C++ body
       * catches it at the boundary and turns it into a CAS error */
      if (!cap->chunks->empty () && cap->chunks->back ().first == cap->tag)
	{
	  cap->chunks->back ().second.append (buf, size);
	}
      else
	{
	  cap->chunks->emplace_back (cap->tag, std::string (buf, size));
	}
      *cap->total += size;
    }
  return (ssize_t) size;
}

/* the chunks of the csql request this thread is rendering, for csc_render_fflush */
static thread_local csql_chunk_vec *csql_Flush_chunks = nullptr;

void
csql_capture_mark_flush (void)
{
  if (csql_Flush_chunks != nullptr)
    {
      csql_Flush_chunks->emplace_back (CAS_CSQL_CHUNK_FLUSH, std::string ());
    }
}

static FILE *
csql_capture_open (csql_capture_cookie *cap)
{
  cookie_io_functions_t io;

  memset (&io, 0, sizeof (io));
  io.write = csql_capture_write;

  FILE *fp = fopencookie (cap, "w", io);
  if (fp != NULL)
    {
      setvbuf (fp, NULL, _IONBF, 0);
    }
  return fp;
}

static void
csql_fill_arg_from_flags (CSQL_ARGUMENT *csql_arg, int flags)
{
  memset (csql_arg, 0, sizeof (*csql_arg));
  csql_arg->cs_mode = true;
  csql_arg->nopager = true;
  csql_arg->string_width = 0;
  csql_arg->column_delimiter = -1;
  csql_arg->column_enclosure = -1;
  csql_arg->auto_commit = (flags & CAS_CSQL_FLAG_AUTO_COMMIT) != 0;
  csql_arg->continue_on_error = (flags & CAS_CSQL_FLAG_CONTINUE_ON_ERROR) != 0;
  csql_arg->plain_output = (flags & CAS_CSQL_FLAG_PLAIN_OUTPUT) != 0;
  csql_arg->skip_column_names = (flags & CAS_CSQL_FLAG_SKIP_COLUMN_NAMES) != 0;
  csql_arg->line_output = (flags & CAS_CSQL_FLAG_LINE_OUTPUT) != 0;
  csql_arg->query_output = (flags & CAS_CSQL_FLAG_QUERY_OUTPUT) != 0;
  csql_arg->loaddb_output = (flags & CAS_CSQL_FLAG_LOADDB_OUTPUT) != 0;
  csql_arg->sysadm = (flags & CAS_CSQL_FLAG_SYSADM) != 0;
  csql_arg->write_on_standby = (flags & CAS_CSQL_FLAG_WRITE_ON_STANDBY) != 0;
  csql_arg->midxkey_print = (flags & CAS_CSQL_FLAG_MIDXKEY_PRINT) != 0;
  csql_arg->pl_server_output = (flags & CAS_CSQL_FLAG_PL_SERVER_OUTPUT) != 0;
  csql_arg->trigger_action_flag = (flags & CAS_CSQL_FLAG_TRIGGER_ACTION) != 0;
  csql_arg->single_line_execution = (flags & CAS_CSQL_FLAG_SINGLE_LINE) != 0;
  csql_arg->read_only = (flags & CAS_CSQL_FLAG_READ_ONLY) != 0;
}

/* codex review #1: net_decode_str only fixes each argument's byte extent; it
 * does not guarantee an int arg holds 4 bytes or a str arg ends in NUL.
 * Validate the declared slot size before decoding, and require string args to
 * be NUL-terminated so the folded csql body's strlen()/strdup() stay in
 * bounds. */
static bool
csql_arg_int_ok (void *arg)
{
  int size = 0;
  net_arg_get_size (&size, arg);
  return size >= NET_SIZE_INT;
}

static bool
csql_arg_char_ok (void *arg)
{
  int size = 0;
  net_arg_get_size (&size, arg);
  return size >= 1;
}

/* on success returns the payload (NUL-terminated) and its size; a
 * zero-length arg yields an empty string, not NULL, so callers need no
 * NULL guard */
static bool
csql_arg_str_ok (void *arg, char **value, int *size)
{
  int slot = 0;
  net_arg_get_size (&slot, arg);
  if (slot < 0)
    {
      return false;
    }
  if (slot == 0)
    {
      *value = (char *) "";
      *size = 0;
      return true;
    }
  net_arg_get_str (value, size, arg);
  /* the driver must terminate the string (default stance: untrusted) */
  return *value != NULL && (*value)[slot - 1] == '\0';
}

static void
csql_reply_chunks (T_NET_BUF *net_buf, int status, const csql_chunk_vec &chunks)
{
  net_buf_cp_int (net_buf, 0, NULL);	/* result code */
  net_buf_cp_int (net_buf, status, NULL);
  /* the thin client's exit paths mirror the fat client's
   * db_commit_is_needed () decisions from this byte */
  net_buf_cp_byte (net_buf, db_commit_is_needed ()? 1 : 0);
  for (const auto &c : chunks)
    {
      net_buf_cp_byte (net_buf, c.first);
      net_buf_cp_int (net_buf, (int) c.second.size (), NULL);
      net_buf_cp_str (net_buf, c.second.data (), (int) c.second.size ());
    }
  net_buf_cp_byte (net_buf, CAS_CSQL_CHUNK_END);
}

FN_RETURN
fn_csql_request (SOCKET sock_fd, int argc, void **argv, T_NET_BUF *net_buf, T_REQ_INFO *req_info)
{
  int sub_code = 0;
  int cas_err = CAS_ER_ARGS;	/* the shared error epilogue's code (arg_error keeps it) */

  if (argc < 1 || !csql_arg_int_ok (argv[0]))
    {
      ERROR_INFO_SET (CAS_ER_ARGS, CAS_ERROR_INDICATOR);
      NET_BUF_ERR_SET (net_buf);
      return FN_KEEP_CONN;
    }
  net_arg_get_int (&sub_code, argv[0]);
  cubconn::adoption::registry_histogram_csql_request (sub_code);

  csql_chunk_vec chunks;
  size_t captured = 0;
  bool overflow = false;
  struct flush_target
  {
    explicit flush_target (csql_chunk_vec *chunks)
    {
      csql_Flush_chunks = chunks;
    }
    ~flush_target ()
    {
      csql_Flush_chunks = nullptr;
    }
  } flush_target (&chunks);
  csql_capture_cookie out_cookie = { &chunks, &captured, &overflow, CAS_CSQL_CHUNK_OUT, 0, sock_fd };
  csql_capture_cookie err_cookie = { &chunks, &captured, &overflow, CAS_CSQL_CHUNK_ERR, 0, sock_fd };
  csql_capture_cookie log_cookie = { &chunks, &captured, &overflow, CAS_CSQL_CHUNK_LOG, 0, sock_fd };
  csql_capture_cookie stdout_cookie = { &chunks, &captured, &overflow, CAS_CSQL_CHUNK_STDOUT, 0, sock_fd };
  FILE *out_fp = NULL, *err_fp = NULL, *stdout_fp = NULL;
  int status = -1;

  out_fp = csql_capture_open (&out_cookie);
  err_fp = csql_capture_open (&err_cookie);
  stdout_fp = csql_capture_open (&stdout_cookie);
  if (out_fp == NULL || err_fp == NULL || stdout_fp == NULL)
    {
      goto mem_error;
    }

  /* the folded csql body and the render capture are C++; keep a bad_alloc
   * (huge result, OOM under the cap) from escaping into the C dispatcher */
  try
    {
      csql_error_capture_guard error_capture (&log_cookie);
      if (sub_code == CAS_CSQL_SUB_INIT_PARAMETERS)
	{
	  char *parameters = NULL;
	  int size = 0, test_mode = 0;
	  if (!is_first_request || argc != 3 || !csql_arg_str_ok (argv[1], &parameters, &size)
	      || !csql_arg_int_ok (argv[2]))
	    {
	      goto arg_error;
	    }
	  net_arg_get_int (&test_mode, argv[2]);
	  if (test_mode != 0 && test_mode != 1)
	    {
	      goto arg_error;
	    }
	  SYSPRM_ERR error = sysprm_init_client_compile_parameters (parameters, test_mode != 0);
	  status = error == PRM_ERR_NO_ERROR ? NO_ERROR : sysprm_set_error (error, parameters);
	}
      else if (sub_code == CAS_CSQL_SUB_EXECUTE)
	{
	  int flags = 0, input_type = 0, line_no = -1, string_width = 0;
	  char *delims = NULL, *column_widths = NULL, *in_file_name = NULL, *text = NULL;
	  int sz = 0;
	  CSQL_ARGUMENT csql_arg;
	  CSQL_SERVER_EXEC_OPTS opts;

	  if (argc < 9 || !csql_arg_int_ok (argv[1]) || !csql_arg_int_ok (argv[2]) || !csql_arg_int_ok (argv[3])
	      || !csql_arg_int_ok (argv[4]) || !csql_arg_str_ok (argv[5], &delims, &sz)
	      || !csql_arg_str_ok (argv[6], &column_widths, &sz) || !csql_arg_str_ok (argv[7], &in_file_name, &sz)
	      || !csql_arg_str_ok (argv[8], &text, &sz))
	    {
	      goto arg_error;
	    }
	  net_arg_get_int (&flags, argv[1]);
	  net_arg_get_int (&input_type, argv[2]);
	  net_arg_get_int (&line_no, argv[3]);
	  net_arg_get_int (&string_width, argv[4]);

	  csql_fill_arg_from_flags (&csql_arg, flags);
	  csql_arg.string_width = string_width;
	  if (strlen (delims) >= 2)
	    {
	      csql_arg.column_delimiter = delims[0];
	      csql_arg.column_enclosure = delims[1];
	    }
	  if (in_file_name[0] != '\0')
	    {
	      csql_arg.in_file_name = in_file_name;
	    }

	  memset (&opts, 0, sizeof (opts));
	  opts.input_type = input_type;
	  opts.line_no = line_no;
	  opts.is_interactive = (flags & CAS_CSQL_FLAG_INTERACTIVE) != 0;
	  opts.is_echo_on = (flags & CAS_CSQL_FLAG_ECHO) != 0;
	  opts.is_time_on = (flags & CAS_CSQL_FLAG_TIME_ON) != 0;
	  opts.query_trace = (flags & CAS_CSQL_FLAG_QUERY_TRACE) != 0;
	  opts.column_widths = (column_widths[0] != '\0') ? column_widths : NULL;
	  opts.stdout_fp = stdout_fp;

	  cas_log_write (0, true, "csql_request execute");
	  csql_hangup_watch watch (sock_fd, LOG_FIND_THREAD_TRAN_INDEX (thread_get_thread_entry_info ()));
	  status = csql_server_execute_request (&csql_arg, &opts, text, out_fp, err_fp);
	}
      else if (sub_code == CAS_CSQL_SUB_SESSION_CMD)
	{
	  int flags = 0, string_width = 0;
	  char *column_widths = NULL, *line = NULL;
	  int sz = 0;
	  CSQL_ARGUMENT csql_arg;
	  CSQL_SERVER_EXEC_OPTS opts;

	  if (argc < 5 || !csql_arg_int_ok (argv[1]) || !csql_arg_int_ok (argv[2])
	      || !csql_arg_str_ok (argv[3], &column_widths, &sz) || !csql_arg_str_ok (argv[4], &line, &sz))
	    {
	      goto arg_error;
	    }
	  net_arg_get_int (&flags, argv[1]);
	  net_arg_get_int (&string_width, argv[2]);

	  csql_fill_arg_from_flags (&csql_arg, flags);
	  csql_arg.string_width = string_width;

	  memset (&opts, 0, sizeof (opts));
	  opts.input_type = 1;	/* STRING semantics */
	  opts.line_no = -1;
	  opts.is_interactive = (flags & CAS_CSQL_FLAG_INTERACTIVE) != 0;
	  opts.is_echo_on = (flags & CAS_CSQL_FLAG_ECHO) != 0;
	  opts.is_time_on = (flags & CAS_CSQL_FLAG_TIME_ON) != 0;
	  opts.query_trace = (flags & CAS_CSQL_FLAG_QUERY_TRACE) != 0;
	  opts.column_widths = (column_widths[0] != '\0') ? column_widths : NULL;
	  opts.stdout_fp = stdout_fp;

	  cas_log_write (0, true, "csql_request session_cmd");
	  csql_hangup_watch watch (sock_fd, LOG_FIND_THREAD_TRAN_INDEX (thread_get_thread_entry_info ()));
	  status = csql_server_session_cmd_request (&csql_arg, &opts, line, out_fp, err_fp);
	}
      else if (sub_code == CAS_CSQL_SUB_TRAN)
	{
	  char op = 0;

	  if (argc < 2 || !csql_arg_char_ok (argv[1]))
	    {
	      goto arg_error;
	    }
	  net_arg_get_char (op, argv[1]);
	  if (op == 'C')
	    {
	      status = (db_commit_transaction () < 0) ? 1 : 0;
	    }
	  else if (op == 'A')
	    {
	      status = (db_abort_transaction () < 0) ? 1 : 0;
	    }
	  else
	    {
	      goto arg_error;
	    }
	  cas_log_write (0, true, "csql_request tran %c", op);
	}
      else if (sub_code == CAS_CSQL_SUB_USER_NAME)
	{
	  /* the fat client asked db_get_user_name () per \u prompt; the thin
	   * one never logs in locally, so the session answers */
	  char *user = db_get_user_name ();
	  if (user != NULL)
	    {
	      try
		{
		  chunks.emplace_back (CAS_CSQL_CHUNK_USER, user);
		}
	      catch (...)
		{
		  db_string_free (user);
		  throw;
		}
	      db_string_free (user);
	    }
	  status = NO_ERROR;
	}
      else if (sub_code == CAS_CSQL_SUB_DB_CODESET)
	{
	  /* the fat client's lang_charset () was the database's; the thin
	   * one's is the environment's, so the server reports its own */
	  chunks.emplace_back (CAS_CSQL_CHUNK_CODESET, std::to_string ((int) lang_charset ()));
	  status = NO_ERROR;
	}
      else
	{
	  goto arg_error;
	}
    }
  catch (const std::bad_alloc &)
    {
      goto mem_error;
    }

  fclose (out_fp);
  fclose (err_fp);
  fclose (stdout_fp);
  out_fp = err_fp = stdout_fp = NULL;

  if (overflow)
    {
      /* the render exceeded CSQL_CAPTURE_LIMIT — report a controlled error
       * instead of returning a truncated result as success */
      goto mem_error;
    }

  csql_reply_chunks (net_buf, status, chunks);
  return FN_KEEP_CONN;

mem_error:
  cas_err = CAS_ER_NO_MORE_MEMORY;
  /* FALLTHRU */
arg_error:
  if (out_fp != NULL)
    {
      fclose (out_fp);
    }
  if (err_fp != NULL)
    {
      fclose (err_fp);
    }
  if (stdout_fp != NULL)
    {
      fclose (stdout_fp);
    }
  ERROR_INFO_SET (cas_err, CAS_ERROR_INDICATOR);
  NET_BUF_ERR_SET (net_buf);
  return FN_KEEP_CONN;
}
