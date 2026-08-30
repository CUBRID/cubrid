/*
 *  Copyright 2016 CUBRID Corporation
 *
 *   Licensed under the Apache License, Version 2.0 (the "License");
 *   you may not use this file except in compliance with the License.
 *   You may obtain a copy of the License at
 *
 *       http://www.apache.org/licenses/LICENSE-2.0
 *
 *   Unless required by applicable law or agreed to in writing, software
 *   distributed under the License is distributed on an "AS IS" BASIS,
 *   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *   See the License for the specific language governing permissions and
 *   limitations under the License.
 *
 */

/*
 * csql_wire.h - thin csql transport (wf122/B5 D6R)
 *
 * The whole client-side protocol surface of the thin csql: connect (local
 * DIRECT_CONNECT on the adoption socket, or broker TCP for db@host),
 * CAS_FC_CSQL_REQUEST (execute / session command / silent tran), and cancel.
 * Rendering happens server-side (#126 D2); replies are replayed onto the
 * csql output/error streams here.  CS-mode csql body only (CSQL_THIN).
 */

#ifndef _CSQL_WIRE_H_
#define _CSQL_WIRE_H_

#include <stdbool.h>
#include <stdio.h>

#include "csql.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* connect to db_name ("db" local via adoption socket, "db@host" via the
 * broker port from $CUBRID_CSQL_BROKER_PORT, default 33000).  Returns
 * NO_ERROR or an error code (also kept for csql_wire_last_error). */
  extern int csql_wire_connect (const char *db_name, const char *user_name, const char *passwd, int client_type);
  extern void csql_wire_disconnect (void);
  extern bool csql_wire_is_connected (void);

/* last wire/server error: returns the error code (0 if none) and copies the
 * server message when msg_buf is given */
  extern int csql_wire_last_error (char **msg);

/* run a statement buffer server-side; replays rendered output onto
 * csql_Output_fp / csql_Error_fp.  Returns the statement failure count
 * (>= 0) or a negative code on a wire error. */
  extern int csql_wire_execute (const CSQL_ARGUMENT * csql_arg, int input_type, int line_no, const char *text);

/* run one server-dependent session command line (";schema foo" form).
 * Returns the server DO_CMD_* code or a negative code on a wire error. */
  extern int csql_wire_session_cmd (const CSQL_ARGUMENT * csql_arg, const char *line);

/* silent commit ('C') / abort ('A') for the exit paths.  Returns 0 on
 * success, 1 on a transaction error, negative on a wire error. */
  extern int csql_wire_tran (char op);

/* whether the last reply reported uncommitted work (db_commit_is_needed) */
  extern bool csql_wire_tran_dirty (void);

/* cancel the in-flight request (called from the SIGINT handler, like the
 * fat client's db_set_interrupt) */
  extern void csql_wire_cancel (void);

/* client-tracked flags the requests carry (set by ;trace, --echo, etc.) */
  extern void csql_wire_set_time_on (bool on);
  extern void csql_wire_set_echo_on (bool on);
  extern void csql_wire_set_trace (bool on);
  extern void csql_wire_set_interactive (bool on);

#ifdef __cplusplus
}
#endif

#endif /* _CSQL_WIRE_H_ */
