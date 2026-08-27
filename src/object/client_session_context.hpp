/*
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
 * client_session_context.hpp - session-scoped state of the client half
 *                              embedded in cub_server (wf122 track A)
 *
 * One instance holds everything the merged-in client half used to keep in
 * process-wide globals, one client process at a time.  Authorization is the
 * first tenant; parser and workspace state land here in later stages.
 *
 * Anchoring contract: the durable owner of a context is the server session
 * (session_state).  A worker thread gains access only through an explicit
 * activation bracket around the work it does on behalf of that session —
 * the thread_local below carries the bracket, never the session state
 * itself, because worker threads are pooled per request and have no session
 * affinity.
 */

#ifndef _CLIENT_SESSION_CONTEXT_HPP_
#define _CLIENT_SESSION_CONTEXT_HPP_

#if defined (SERVER_MODE)

#include "authenticate_context.hpp"

// *INDENT-OFF*
struct mht_table;

class client_session_context
{
  public:
    authenticate_context au_context;

    /* interpreter label table (wf122 A2 D5) - owned here because labels are
     * session data; created lazily by pt_associate_label_with_value.
     * Teardown (pt_free_label_table on session end) lands with the A4
     * session_state anchor - until then only unit/smoke contexts exist. */
    struct mht_table *label_table = nullptr;
};
// *INDENT-ON*

/* activation bracket; brackets do not nest */
extern void csc_activate (client_session_context *ctx);
extern void csc_deactivate (void);

/* the context the calling thread's bracket installed; a caller off any
 * bracket is an ownership bug — fail fast */
extern client_session_context *csc_current (void);

#endif /* SERVER_MODE */

#endif /* _CLIENT_SESSION_CONTEXT_HPP_ */
