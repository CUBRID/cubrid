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
#include <unordered_map>
#include <vector>

// *INDENT-OFF*
struct mht_table;

namespace cubmethod
{
  class callback_handler;
}

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

    /* query_method.cpp: er-stack depth at the innermost method-dispatch
     * boundary.  Frames at or below this depth belong to the invoking server
     * executor; the client half's er_stack_clearall (transaction_cl.c) must
     * not clear past it (legacy CAS cleared only its own process's stack). */
    int er_dispatch_floor = 0;

    /* object_accessor.c: method invocation nesting + last method error */
    int obj_method_call_level = 0;
    char *obj_method_error_msg = nullptr;

    /* execute_schema.c: online-index thread count of the running DDL */
    int ib_thread_count = 0;

    /* execute_statement.c: per-session savepoint-name counters */
    int tr_savepoint_number = 0;
    int update_savepoint_number = 0;
    int delete_savepoint_number = 0;
    int insert_savepoint_number = 0;
    int merge_savepoint_number = 0;

    /* db_query.c: session trace of the last execution plan */
    char *db_execution_plan = nullptr;
    int db_execution_plan_length = -1;

    /* object_domain.c: per-session lists for the MOP-capable domain types
     * (B4-D9).  The process-wide domain cache is structural — one node
     * serves every session — so a cached node that embeds a workspace MOP
     * outlives the workspace owning that MOP (use-after-free once sessions
     * retire promptly).  The legacy CAS ran one client process per session,
     * i.e. one domain cache per session; this restores that shape.  Opaque
     * TP_SESSION_DOMAINS, allocated lazily and freed by
     * tp_session_domains_final() in the bracketed teardown. */
    void *tp_domains = nullptr;

    /* optimizer level override (qo_get/set_optimization_param): the CAS
     * original wrote the process sysprm, which was per-session in effect —
     * here the write lands on the session instead of the shared parameter.
     * -1 = unset (reads fall through to sysprm). */
    int qo_optimization_level = -1;

    /* method/SP callback termination (#120 D8): the CAS-side process-global
     * handle cache was per-session in effect (one CAS per session) — its
     * faithful translation here is per-session ownership.  It holds
     * workspace-backed query handles, so it must retire inside the bracketed
     * teardown, before ws_final — which is why it lives here and not on
     * cubpl::session (session_state_uninit deletes that outside the bracket).
     * Created lazily by get_callback_handler(). */
    cubmethod::callback_handler *method_callback_handler = nullptr;

    /* query_method.c: builtin C-method runtime arguments keyed by invoke-group id */
    std::unordered_map<UINT64, std::vector<DB_VALUE>> method_runtime_args;

    /* set when the owning session retired this context while the very thread
     * inside it requested the retirement (db_end_session under its own
     * bracket): the bracket keeps working on it, and csc_deactivate runs the
     * teardown and frees it on exit instead */
    bool orphaned = false;

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

/* does the calling thread hold an activation bracket? (no assert — memory
 * routing probes this on paths shared with pure server threads) */
extern bool csc_bracket_is_active (void);

/* has the bracketed session terminated a method/SP callback in-process?
 * (qexec's qlist balance check stands down only for such sessions) */
extern bool csc_has_method_callback_state (void);

/* is the calling thread inside an in-process method dispatch? (page_buffer's
 * commit-time unfix sweep must spare the suspended outer executor's fixes) */
extern bool csc_in_method_dispatch (void);

/* the session's domain-cache slot (object_domain.c owns the contents, B4-D9) */
extern void **csc_tp_domains_slot (void);

/* run the client half's session teardown under a temporary bracket and free
 * the context; called by the owning session_state when it is uninitialized */
extern void csc_retire_and_delete (client_session_context *ctx);

#endif /* SERVER_MODE */

#endif /* _CLIENT_SESSION_CONTEXT_HPP_ */
