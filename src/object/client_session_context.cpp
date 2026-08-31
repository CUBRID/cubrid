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
 * client_session_context.cpp - see client_session_context.hpp
 */

#if defined (SERVER_MODE)

#include "client_session_context.hpp"

#include <cassert>
#include <cstddef>
#include <cstdlib>
#include <cstring>

#include "object_domain.h"	// tp_session_domains_final (B4-D9)

static thread_local client_session_context *tl_Csc_active = NULL;

client_session_context::client_session_context ()
{
  /* mirror the CS build's static initializer for the pre-boot credential;
   * boot_client_common re-initializes it at registration */
  memset (&boot_server_credential, 0, sizeof (boot_server_credential));
  boot_server_credential.process_id = -1;
  OID_SET_NULL (&boot_server_credential.root_class_oid);
  HFID_SET_NULL (&boot_server_credential.root_class_hfid);
  boot_server_credential.page_size = -1;
  boot_server_credential.log_page_size = -1;
  boot_server_credential.ha_server_state = HA_SERVER_STATE_NA;
  memset (boot_server_credential.server_session_key, 0xFF, SERVER_SESSION_KEY_SIZE);
  boot_server_credential.db_charset = INTL_CODESET_NONE;
}

void
csc_activate (client_session_context *ctx)
{
  assert (ctx != NULL);
  if (tl_Csc_active != NULL)
    {
      /* brackets do not nest; in a release build proceeding would turn the
       * invariant violation into a silent self-deadlock on the non-recursive
       * bracket_mutex — fail fast instead (a4 codex review) */
      assert (false);
      abort ();
    }
  ctx->bracket_mutex.lock ();
  tl_Csc_active = ctx;
}

static void csc_teardown (client_session_context *ctx);

void
csc_deactivate (void)
{
  assert (tl_Csc_active != NULL);
  client_session_context *ctx = tl_Csc_active;
  if (ctx->orphaned)
    {
      /* the owning session retired this context while this thread was inside
       * it (see csc_retire_and_delete) — the bracket exit is the first safe
       * point to run the teardown */
      csc_teardown (ctx);
    }
  tl_Csc_active = NULL;
  ctx->bracket_mutex.unlock ();
  if (ctx->orphaned)
    {
      delete ctx;
    }
}

client_session_context *
csc_current (void)
{
  assert (tl_Csc_active != NULL);
  return tl_Csc_active;
}

bool
csc_bracket_is_active (void)
{
  return tl_Csc_active != NULL;
}

/* the method-dispatch er-stack floor (see client_session_context.hpp); 0 —
 * i.e. plain er_stack_clearall behavior — outside a bracket or a dispatch */
int
csc_er_stack_floor (void)
{
  return tl_Csc_active != NULL ? tl_Csc_active->er_dispatch_floor : 0;
}

/* whether this thread is inside an in-process method dispatch (the client
 * half of a PL callback).  page_buffer.c's commit-time unfix sweep gates on
 * it: the fixes on this thread belong to the suspended outer executor. */
bool
csc_in_method_dispatch (void)
{
  return tl_Csc_active != NULL && tl_Csc_active->tm.libcas_depth > 0;
}

/* the session's optimizer-level override slot (query_graph.c) */
int *
csc_qo_optimization_level (void)
{
  return &csc_current ()->qo_optimization_level;
}

/* has this session terminated a method/SP callback in-process? (its kept
 * statement handles may hold list files open across an outer query's end —
 * qexec's per-thread qlist balance check stands down only then) */
bool
csc_has_method_callback_state (void)
{
  return tl_Csc_active != NULL && tl_Csc_active->method_callback_handler != nullptr;
}

ws_context *
csc_ws (void)
{
  return &csc_current ()->ws;
}

tm_context *
csc_tm (void)
{
  return &csc_current ()->tm;
}

sm_context *
csc_sm (void)
{
  return &csc_current ()->sm;
}

tr_context *
csc_tr (void)
{
  return &csc_current ()->tr;
}

db_cl_context *
csc_db (void)
{
  return &csc_current ()->db;
}

void **
csc_tp_domains_slot (void)
{
  return &csc_current ()->tp_domains;
}

/* B4-D8: db_Connect_status expands to this.  Outside a bracket the server
 * sees the constant CONNECTED upstream's SERVER_MODE global hardwired
 * (db_macro.c), so compat-layer CHECK_CONNECT_* guards reached from server
 * execution pass instead of asserting; inside a bracket the session's own
 * status applies. */
int *
db_connect_status_ptr (void)
{
  static int server_Connect_status = DB_CONNECTION_STATUS_CONNECTED;
  if (tl_Csc_active != NULL)
    {
      return &tl_Csc_active->db.connect_status;
    }
  return &server_Connect_status;
}

plan_dump_context *
csc_plan_dump (void)
{
  return &csc_current ()->plan_dump;
}

obt_context *
csc_obt (void)
{
  return &csc_current ()->obt;
}

/* pt_free_label_table (parser.h) frees the label table the parser owns;
 * db_free_execution_plan (db_query.h) frees the plan trace buffer */

/* method/SP callback termination state (#120): the handler caches
 * workspace-backed query handles (method_callback.cpp) and the runtime-args
 * map holds session DB_VALUEs (query_method.cpp) — both must retire before
 * ws_final */
extern void method_callback_session_final (void);
extern void method_runtime_args_session_final (void);

/* client/server boundary flag (network_interface_sr.cpp) */
extern thread_local unsigned int db_on_server;

/* per-session teardown; the calling thread's bracket must hold ctx */
static void
csc_teardown (client_session_context *ctx)
{
  assert (tl_Csc_active == ctx);

  /* teardown is client-half work: allocation routing must resolve to the
   * session workspace even when a server worker (db_on_server == 1) drives
   * the retirement — e.g. session expiry or disconnect cleanup */
  unsigned int save_on_server = db_on_server;
  db_on_server = 0;

  db_free_execution_plan ();

  if (ctx->obj_method_error_msg != NULL)
    {
      free (ctx->obj_method_error_msg);
      ctx->obj_method_error_msg = NULL;
    }

  if (ctx->label_table != NULL)
    {
      pt_free_label_table ();
    }

  method_callback_session_final ();
  method_runtime_args_session_final ();

  if (ctx->ws.mop_table != NULL)
    {
      /* schema-manager teardown first: it clears Current_Schema and frees
       * descriptors/virtual-query caches before ws_final frees their MOPs */
      sm_final ();

      /* remaining query results and the registry storage (db_query.c) */
      db_final_client_query_results ();

      /* frees the MOP table, classname cache, resident class list and the
       * session's lea heap; shared areas are left alone (#123 D5) */
      ws_final ();
    }
  else if (ctx->ws.heap_id != 0)
    {
      /* boot failed between heap creation and table build */
      db_destroy_workspace_heap ();
    }

  /* the session's domain lists (B4-D9) go AFTER ws_final: while the
   * workspace lives, class attributes point at these nodes and every
   * teardown-path tp_domain_free (classobj_clear_attribute etc.) relies on
   * is_cached == 1 being a no-op — sweeping earlier double-frees them.
   * Only now are the nodes unreferenced.  tp_domain_free never dereferences
   * the (now dangling) embedded class_mop. */
  tp_session_domains_final ();

  db_on_server = save_on_server;
}

void
csc_retire_and_delete (client_session_context *ctx)
{
  assert (ctx != NULL);

  if (tl_Csc_active == ctx)
    {
      /* the retiring thread is inside this very context (a session ended by
       * its own client call, e.g. db_end_session): re-activating would
       * self-deadlock, and the caller's stack still works on this context —
       * hand the teardown to the bracket exit */
      ctx->orphaned = true;
      return;
    }

  csc_activate (ctx);
  csc_teardown (ctx);
  csc_deactivate ();

  delete ctx;
}

#endif /* SERVER_MODE */
