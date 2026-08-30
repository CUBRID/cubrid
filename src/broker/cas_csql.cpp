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

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <new>
#include <string>
#include <vector>
#include <utility>

#include "cas.h"
#include "cas_common.h"
#include "cas_function.h"
#include "cas_network.h"
#include "cas_net_buf.h"
#include "cas_log.h"
#include "cas_error.h"
#include "cas_protocol.h"

#include "csql.h"

typedef std::vector<std::pair<char, std::string>> csql_chunk_vec;

/* codex review #2: cap the server-side render capture so a query with
 * gigantic printable output cannot exhaust cub_server memory before a byte
 * is sent.  32 MiB is far above any legitimate csql result page; on overflow
 * the request becomes a controlled statement error. */
#define CSQL_CAPTURE_LIMIT (32 * 1024 * 1024)

typedef struct
{
  csql_chunk_vec *chunks;
  size_t *total;
  bool *overflow;
  char tag;
} csql_capture_cookie;

static ssize_t
csql_capture_write (void *cookie, const char *buf, size_t size)
{
  csql_capture_cookie *cap = (csql_capture_cookie *) cookie;

  if (size > 0 && !*cap->overflow)
    {
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

static FILE *
csql_capture_open (csql_capture_cookie * cap)
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
csql_fill_arg_from_flags (CSQL_ARGUMENT * csql_arg, int flags)
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
csql_reply_chunks (T_NET_BUF * net_buf, int status, const csql_chunk_vec & chunks)
{
  net_buf_cp_int (net_buf, 0, NULL);	/* result code */
  net_buf_cp_int (net_buf, status, NULL);
  /* the thin client's exit paths mirror the fat client's
   * db_commit_is_needed () decisions from this byte */
  net_buf_cp_byte (net_buf, db_commit_is_needed ()? 1 : 0);
  for (const auto & c : chunks)
    {
      net_buf_cp_byte (net_buf, c.first);
      net_buf_cp_int (net_buf, (int) c.second.size (), NULL);
      net_buf_cp_str (net_buf, c.second.data (), (int) c.second.size ());
    }
  net_buf_cp_byte (net_buf, CAS_CSQL_CHUNK_END);
}

FN_RETURN
fn_csql_request (SOCKET sock_fd, int argc, void **argv, T_NET_BUF * net_buf, T_REQ_INFO * req_info)
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

  csql_chunk_vec chunks;
  size_t captured = 0;
  bool overflow = false;
  csql_capture_cookie out_cookie = { &chunks, &captured, &overflow, CAS_CSQL_CHUNK_OUT };
  csql_capture_cookie err_cookie = { &chunks, &captured, &overflow, CAS_CSQL_CHUNK_ERR };
  FILE *out_fp = NULL, *err_fp = NULL;
  int status = -1;

  out_fp = csql_capture_open (&out_cookie);
  err_fp = csql_capture_open (&err_cookie);
  if (out_fp == NULL || err_fp == NULL)
    {
      goto mem_error;
    }

  /* the folded csql body and the render capture are C++; keep a bad_alloc
   * (huge result, OOM under the cap) from escaping into the C dispatcher */
  try
  {
    if (sub_code == CAS_CSQL_SUB_EXECUTE)
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

	cas_log_write (0, true, "csql_request execute");
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
	opts.is_echo_on = (flags & CAS_CSQL_FLAG_ECHO) != 0;
	opts.is_time_on = (flags & CAS_CSQL_FLAG_TIME_ON) != 0;
	opts.query_trace = (flags & CAS_CSQL_FLAG_QUERY_TRACE) != 0;
	opts.column_widths = (column_widths[0] != '\0') ? column_widths : NULL;

	cas_log_write (0, true, "csql_request session_cmd");
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
  out_fp = err_fp = NULL;

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
  ERROR_INFO_SET (cas_err, CAS_ERROR_INDICATOR);
  NET_BUF_ERR_SET (net_buf);
  return FN_KEEP_CONN;
}
