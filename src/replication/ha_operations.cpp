/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 */

#include "ha_operations.hpp"

#include "boot_sr.h"
#include "error_manager.h"
#include "ha_server_state.hpp"
#include "log_impl.h"
#include "log_manager.h"
#include "replication_node_manager.hpp"
#include "server_support.h" // css_start_all_threads TODO: remove this dependency
#include "thread_entry.hpp"

namespace ha_operations
{
  struct server_state_transition_entry
  {
    server_state cur_state;
    server_state req_state;
    server_state next_state;
  };

  std::vector<server_state_transition_entry> g_server_state_transition_table =
  {
    /* idle -> active */
    {SERVER_STATE_IDLE, SERVER_STATE_ACTIVE, SERVER_STATE_ACTIVE},
    /* idle -> to-be-standby */
    {SERVER_STATE_IDLE, SERVER_STATE_STANDBY, SERVER_STATE_TO_BE_STANDBY},
    /* idle -> maintenance */
    {SERVER_STATE_IDLE, SERVER_STATE_MAINTENANCE, SERVER_STATE_MAINTENANCE},
    /* active -> active */
    {SERVER_STATE_ACTIVE, SERVER_STATE_ACTIVE, SERVER_STATE_ACTIVE},
    /* active -> to-be-standby */
    {SERVER_STATE_ACTIVE, SERVER_STATE_STANDBY, SERVER_STATE_TO_BE_STANDBY},
    /* to-be-active -> active */
    {SERVER_STATE_TO_BE_ACTIVE, SERVER_STATE_ACTIVE, SERVER_STATE_ACTIVE},
    /* standby -> standby */
    {SERVER_STATE_STANDBY, SERVER_STATE_STANDBY, SERVER_STATE_STANDBY},
    /* standby -> to-be-active */
    {SERVER_STATE_STANDBY, SERVER_STATE_ACTIVE, SERVER_STATE_TO_BE_ACTIVE},
    /* statndby -> maintenance */
    {SERVER_STATE_STANDBY, SERVER_STATE_MAINTENANCE, SERVER_STATE_MAINTENANCE},
    /* to-be-standby -> standby */
    {SERVER_STATE_TO_BE_STANDBY, SERVER_STATE_STANDBY, SERVER_STATE_STANDBY},
    /* maintenance -> standby */
    {SERVER_STATE_MAINTENANCE, SERVER_STATE_STANDBY, SERVER_STATE_TO_BE_STANDBY}
  };

  std::recursive_mutex g_state_mtx;

  static void handle_force_change_state (cubthread::entry *thread_p, server_state req_state, bool from_heartbeat);
  static server_state handle_maintenance_change_state (cubthread::entry *thread_p, server_state state, int timeout);

  static void
  handle_force_change_state (cubthread::entry *thread_p, server_state req_state, bool from_heartbeat)
  {
    if (get_server_state () != req_state)
      {
	er_log_debug (ARG_FILE_LINE, "change_server_state: set force from %s to state %s\n",
		      server_state_string (get_server_state ()), server_state_string (req_state));

	if (req_state == SERVER_STATE_ACTIVE)
	  {
	    er_log_debug (ARG_FILE_LINE, "change_server_state: logtb_enable_update()\n");
	    if (!HA_DISABLED ())
	      {
		// todo: force interruptions
		cubreplication::replication_node_manager::start_commute_to_master_state (thread_p, true);
		cubreplication::replication_node_manager::wait_commute (get_server_state (), SERVER_STATE_ACTIVE);
	      }
	    else
	      {
		logtb_enable_update (thread_p);
		get_server_state () = req_state;
	      }
	  }
	else if (req_state == SERVER_STATE_STANDBY)
	  {
	    assert (!HA_DISABLED ());
	    cubreplication::replication_node_manager::start_commute_to_slave_state (thread_p, true);
	    cubreplication::replication_node_manager::wait_commute (get_server_state (), SERVER_STATE_STANDBY);
	  }
	else
	  {
	    get_server_state () = req_state;
	  }

	if (get_server_state () == SERVER_STATE_ACTIVE)
	  {
	    log_set_ha_promotion_time (thread_p, ((INT64) time (0)));
	  }
      }
    if (req_state == SERVER_STATE_ACTIVE || req_state == SERVER_STATE_STANDBY)
      {
	// desired state was enforced
	assert (get_server_state () == req_state);
      }
  }

  static server_state
  handle_maintenance_change_state (cubthread::entry *thread_p, server_state state, int timeout)
  {
    state = transit_server_state (thread_p, SERVER_STATE_MAINTENANCE);
    if (state == SERVER_STATE_NA)
      {
	return state;
      }

    if (state == SERVER_STATE_MAINTENANCE)
      {
	er_log_debug (ARG_FILE_LINE, "change_server_state: logtb_enable_update() \n");
	logtb_enable_update (thread_p);

	boot_server_status (BOOT_SERVER_MAINTENANCE);
      }

    for (int i = 0; i < timeout; ++i)
      {
	/* waiting timeout second while transaction terminated normally. */
	if (logtb_count_not_allowed_clients_in_maintenance_mode (thread_p) == 0)
	  {
	    break;
	  }
	thread_sleep (1000);	/* 1000 msec */
      }

    if (logtb_count_not_allowed_clients_in_maintenance_mode (thread_p) != 0)
      {
	LOG_TDES *tdes;

	/* try to kill transaction. */
	TR_TABLE_CS_ENTER (thread_p);
	// start from transaction index i = 1; system transaction cannot be killed
	for (int i = 1; i < log_Gl.trantable.num_total_indices; ++i)
	  {
	    tdes = log_Gl.trantable.all_tdes[i];
	    if (tdes != NULL && tdes->trid != NULL_TRANID)
	      {
		if (!BOOT_IS_ALLOWED_CLIENT_TYPE_IN_MT_MODE (tdes->client.get_host_name (), boot_Host_name,
		    tdes->client.client_type))
		  {
		    logtb_slam_transaction (thread_p, tdes->tran_index);
		  }
	      }
	  }
	TR_TABLE_CS_EXIT (thread_p);

	thread_sleep (2000);	/* 2000 msec */
      }
    return state;
  }

  int
  change_server_state (cubthread::entry *thread_p, server_state state, bool force, int timeout, bool heartbeat)
  {
    er_log_debug (ARG_FILE_LINE, "change_server_state: ha_Server_state %s state %s force %c heartbeat %c\n",
		  server_state_string (get_server_state ()), server_state_string (state), (force ? 't' : 'f'),
		  (heartbeat ? 't' : 'f'));

    assert (state >= SERVER_STATE_IDLE && state <= SERVER_STATE_DEAD);

    std::lock_guard<std::recursive_mutex> lg (g_state_mtx);

    // Return early if we are in the state we want to be in or if we already are transitioning to the requested state
    if (state == get_server_state ()
	|| (!force && get_server_state () == SERVER_STATE_TO_BE_ACTIVE && state == SERVER_STATE_ACTIVE)
	|| (!force && get_server_state () == SERVER_STATE_TO_BE_STANDBY && state == SERVER_STATE_STANDBY))
      {
	return NO_ERROR;
      }

    if (heartbeat == false && ! (get_server_state () == SERVER_STATE_STANDBY && state == SERVER_STATE_MAINTENANCE)
	&& ! (get_server_state () == SERVER_STATE_MAINTENANCE && state == SERVER_STATE_STANDBY)
	&& ! (force && get_server_state () == SERVER_STATE_TO_BE_ACTIVE && state == SERVER_STATE_ACTIVE))
      {
	return NO_ERROR;
      }

    if (force)
      {
	// Do transitions in 1 phase
	handle_force_change_state (thread_p, state, heartbeat);
	if (get_server_state () == SERVER_STATE_ACTIVE)
	  {
	    // spawn threads be able to handle a potential flood after fail-over
	    css_start_all_threads ();
	  }
	return NO_ERROR;
      }

    switch (state)
      {
      case SERVER_STATE_ACTIVE:
	// Phase 1: transit to SERVER_STATE_TO_BE_ACTIVE
	state = transit_server_state (thread_p, SERVER_STATE_ACTIVE);
	if (state == SERVER_STATE_NA)
	  {
	    break;
	  }

	if (!HA_DISABLED () && state == SERVER_STATE_TO_BE_ACTIVE)
	  {
	    // Phase 2: task will transit to SERVER_STATE_ACTIVE
	    cubreplication::replication_node_manager::start_commute_to_master_state (thread_p, false);
	  }

	if (HA_DISABLED ())
	  {
	    assert (state == SERVER_STATE_TO_BE_ACTIVE);

	    logtb_enable_update (thread_p);
	    // Phase 2: transit to SERVER_STATE_ACTIVE
	    state = transit_server_state (thread_p, SERVER_STATE_ACTIVE);
	  }
	break;

      case SERVER_STATE_STANDBY:
      {
	server_state orig_state = get_server_state ();
	// Phase 1: transit to SERVER_STATE_TO_BE_STANDBY
	state = transit_server_state (thread_p, SERVER_STATE_STANDBY);
	if (state == SERVER_STATE_NA)
	  {
	    break;
	  }

	if (state == SERVER_STATE_TO_BE_STANDBY)
	  {
	    assert (!HA_DISABLED ());
	    // Phase 2: task will transit to SERVER_STATE_STANDBY
	    cubreplication::replication_node_manager::start_commute_to_slave_state (thread_p, false);
	  }

	if (orig_state == SERVER_STATE_MAINTENANCE)
	  {
	    boot_server_status (BOOT_SERVER_UP);
	  }
      }
      break;

      case SERVER_STATE_MAINTENANCE:
	state = handle_maintenance_change_state (thread_p, state, timeout);
	break;

      default:
	state = SERVER_STATE_NA;
	break;
      }

    return (state != SERVER_STATE_NA) ? NO_ERROR : ER_FAILED;
  }

  /*
   * transit_server_state - request to transit the current HA server
   *                        state to the required state
   *   return: new state changed if successful or SERVER_STATE_NA
   *   req_state(in): the state for the server to transit
   *
   */
  server_state
  transit_server_state (cubthread::entry *thread_p, server_state req_state)
  {
    server_state new_state = SERVER_STATE_NA;

    if (get_server_state () == req_state)
      {
	return req_state;
      }

    std::lock_guard<std::recursive_mutex> lg (g_state_mtx);

    for (auto entry : g_server_state_transition_table)
      {
	if (entry.cur_state == get_server_state () && entry.req_state == req_state)
	  {
	    er_log_debug (ARG_FILE_LINE, "transit_server_state: ha_Server_state (%s) -> (%s)\n",
			  server_state_string (get_server_state ()), server_state_string (entry.next_state));
	    new_state = entry.next_state;
	    if (!HA_DISABLED ())
	      {
		er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_SERVER_HA_MODE_CHANGE, 2,
			server_state_string (get_server_state ()), server_state_string (new_state));
	      }
	    get_server_state () = new_state;
	    /* sync up the current HA state with the system parameter */
	    prm_set_integer_value (PRM_ID_HA_SERVER_STATE, get_server_state ());

	    if (get_server_state () == SERVER_STATE_ACTIVE)
	      {
		log_set_ha_promotion_time (thread_p, ((INT64) time (0)));
		css_start_all_threads ();
	      }

	    break;
	  }
      }

    return new_state;
  }

  void
  finish_transit (cubthread::entry *thread_p, bool force, server_state req_state)
  {
    assert (req_state == SERVER_STATE_ACTIVE || req_state == SERVER_STATE_STANDBY);

    if (req_state == SERVER_STATE_ACTIVE)
      {
	logtb_enable_update (thread_p);
      }
    else
      {
	logtb_disable_update (thread_p);
      }

    if (force)
      {
	get_server_state () = req_state;
      }
    else
      {
	server_state state = css_transit_ha_server_state (thread_p, req_state);
	assert (state == req_state);
      }
  }
}

decltype (&ha_operations::change_server_state) css_change_ha_server_state = &ha_operations::change_server_state;
decltype (&ha_operations::finish_transit) css_finish_transit = &ha_operations::finish_transit;
decltype (&ha_operations::transit_server_state) css_transit_ha_server_state = &ha_operations::transit_server_state;
