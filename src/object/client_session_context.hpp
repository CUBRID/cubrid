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
 *                              embedded in cub_server
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
#include "boot.h"
#include "db.h"
#include "object_template.h"
#include "schema_manager.h"
#include "transaction_cl.h"
#include "trigger_manager.h"
#include "work_space.h"
#include "xasl_generation.h"

#include <mutex>

// *INDENT-OFF*
struct mht_table;

class client_session_context
{
  public:
    /* worker threads are pooled per request with no session affinity, and a
     * connection's out-of-band requests can land on another worker while one
     * is inside this session (#123 D3) — every bracket holds this for its
     * whole extent, so client-half work on one session is serialized */
    std::mutex bracket_mutex;

    authenticate_context au_context;

    /* the MOP closed system: table, dirty lists, classname cache, lea heap */
    ws_context ws;

    /* client-half transaction state (tm_Tran_*) */
    tm_context tm;

    /* schema manager: root class MOP, descriptors, current schema */
    sm_context sm;

    /* trigger manager: caches keyed by MOP, deferred activity lists */
    tr_context tr;

    /* boot_cl.c: the server credential of this session's registration —
     * server_session_key must not be shared between sessions */
    BOOT_SERVER_CREDENTIAL boot_server_credential;

    /* db.h connection identity (db_Session_id and friends) */
    db_cl_context db;

    /* plan-dump handle (xasl_generation.h) */
    plan_dump_context plan_dump;

    /* object templates (object_template.h) */
    obt_context obt;

    /* object_accessor.c: method invocation nesting */
    int obj_method_call_level = 0;

    /* execute_schema.c: online-index thread count of the running DDL */
    int ib_thread_count = 0;

    /* db_query.c: session trace of the last execution plan */
    char *db_execution_plan = nullptr;
    int db_execution_plan_length = -1;

    client_session_context ();
    ~client_session_context () = default;

    /* interpreter label table - owned here because labels are
     * session data; created lazily by pt_associate_label_with_value.
     * Freed by the owning session_state (session_state_uninit). */
    struct mht_table *label_table = nullptr;
};
// *INDENT-ON*

/* activation bracket; brackets do not nest.  Locks the context's
 * bracket_mutex for the bracket's extent. */
extern void csc_activate (client_session_context *ctx);
extern void csc_deactivate (void);

/* the context the calling thread's bracket installed; a caller off any
 * bracket is an ownership bug — fail fast */
extern client_session_context *csc_current (void);

/* run the client half's session teardown under a temporary bracket and free
 * the context; called by the owning session_state when it is uninitialized */
extern void csc_retire_and_delete (client_session_context *ctx);

#endif /* SERVER_MODE */

#endif /* _CLIENT_SESSION_CONTEXT_HPP_ */
