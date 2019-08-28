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
  int change_server_state (cubthread::entry *thread_p, SERVER_STATE state, bool force, int timeout, bool heartbeat)
  {
    HA_SERVER_STATE orig_state;
    int i;

    er_log_debug (ARG_FILE_LINE, "css_change_ha_server_state: ha_Server_state %s state %s force %c heartbeat %c\n",
		  css_ha_server_state_string (get_server_state ()), css_ha_server_state_string (state), (force ? 't' : 'f'),
		  (heartbeat ? 't' : 'f'));

    assert (state >= HA_SERVER_STATE_IDLE && state <= HA_SERVER_STATE_DEAD);

    csect_enter (thread_p, CSECT_HA_SERVER_STATE, INF_WAIT);

    // Return early if we are in the state we want to be in or if we already are transitioning to the requested state
    if (state == get_server_state ()
	|| (!force && get_server_state () == HA_SERVER_STATE_TO_BE_ACTIVE && state == HA_SERVER_STATE_ACTIVE)
	|| (!force && get_server_state () == HA_SERVER_STATE_TO_BE_STANDBY && state == HA_SERVER_STATE_STANDBY))
      {
	csect_exit (thread_p, CSECT_HA_SERVER_STATE);
	return NO_ERROR;
      }

    if (heartbeat == false
	&& ! (get_server_state () == HA_SERVER_STATE_STANDBY && state == HA_SERVER_STATE_MAINTENANCE)
	&& ! (get_server_state () == HA_SERVER_STATE_MAINTENANCE && state == HA_SERVER_STATE_STANDBY)
	&& ! (force && get_server_state () == HA_SERVER_STATE_TO_BE_ACTIVE && state == HA_SERVER_STATE_ACTIVE))
      {
	csect_exit (thread_p, CSECT_HA_SERVER_STATE);
	return NO_ERROR;
      }

    orig_state = get_server_state ();

    if (force)
      {
	if (get_server_state () != state)
	  {
	    er_log_debug (ARG_FILE_LINE, "css_change_ha_server_state: set force from %s to state %s\n",
			  css_ha_server_state_string (get_server_state ()), css_ha_server_state_string (state));

	    if (state == HA_SERVER_STATE_ACTIVE)
	      {
		er_log_debug (ARG_FILE_LINE, "css_change_ha_server_state: logtb_enable_update()\n");
		if (!HA_DISABLED ())
		  {
		    // todo: force interruptions
		    cubreplication::replication_node_manager::start_commute_to_master_state (thread_p, true);
		    cubreplication::replication_node_manager::wait_commute (get_server_state (), HA_SERVER_STATE_ACTIVE);
		  }
		else
		  {
		    logtb_enable_update (thread_p);
		    get_server_state () = state;
		  }
	      }
	    else if (state == HA_SERVER_STATE_STANDBY)
	      {
		assert (!HA_DISABLED ());
		cubreplication::replication_node_manager::start_commute_to_slave_state (thread_p, true);
		cubreplication::replication_node_manager::wait_commute (get_server_state (), HA_SERVER_STATE_STANDBY);
	      }
	    else
	      {
		get_server_state () = state;
	      }

	    // TODO: investigate the need for log_append
	    /* append a dummy log record for LFT to wake LWTs up */
	    log_append_ha_server_state (thread_p, state);

	    if (get_server_state () == HA_SERVER_STATE_ACTIVE)
	      {
		log_set_ha_promotion_time (thread_p, ((INT64) time (0)));
	      }
	  }

	if (state == HA_SERVER_STATE_ACTIVE || state == HA_SERVER_STATE_STANDBY)
	  {
	    // desired state was enforced
	    assert (get_server_state () == state);
	  }

	csect_exit (thread_p, CSECT_HA_SERVER_STATE);
	return NO_ERROR;
      }

    switch (state)
      {
      case HA_SERVER_STATE_ACTIVE:
	state = css_transit_ha_server_state (thread_p, HA_SERVER_STATE_ACTIVE);
	if (state == HA_SERVER_STATE_NA)
	  {
	    break;
	  }
	if (!HA_DISABLED () && state == HA_SERVER_STATE_TO_BE_ACTIVE)
	  {
	    cubreplication::replication_node_manager::start_commute_to_master_state (thread_p, false);
	  }

	if (HA_DISABLED ())
	  {
	    assert (state == HA_SERVER_STATE_TO_BE_ACTIVE);

	    logtb_enable_update (thread_p);
	    state = css_transit_ha_server_state (thread_p, HA_SERVER_STATE_ACTIVE);
	  }
	break;

      case HA_SERVER_STATE_STANDBY:
	state = css_transit_ha_server_state (thread_p, HA_SERVER_STATE_STANDBY);
	if (state == HA_SERVER_STATE_NA)
	  {
	    break;
	  }

	if (orig_state == HA_SERVER_STATE_MAINTENANCE)
	  {
	    boot_server_status (BOOT_SERVER_UP);
	  }

	if (state == HA_SERVER_STATE_STANDBY)
	  {
	    assert (!HA_DISABLED ());
	    cubreplication::replication_node_manager::start_commute_to_slave_state (thread_p, false);
	  }
	break;

      case HA_SERVER_STATE_MAINTENANCE:
	state = css_transit_ha_server_state (thread_p, HA_SERVER_STATE_MAINTENANCE);
	if (state == HA_SERVER_STATE_NA)
	  {
	    break;
	  }

	if (state == HA_SERVER_STATE_MAINTENANCE)
	  {
	    er_log_debug (ARG_FILE_LINE, "css_change_ha_server_state: logtb_enable_update() \n");
	    logtb_enable_update (thread_p);

	    boot_server_status (BOOT_SERVER_MAINTENANCE);
	  }

	for (i = 0; i < timeout; i++)
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
	    for (i = 1; i < log_Gl.trantable.num_total_indices; i++)
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
	break;

      default:
	state = HA_SERVER_STATE_NA;
	break;
      }

    csect_exit (thread_p, CSECT_HA_SERVER_STATE);

    return (state != HA_SERVER_STATE_NA) ? NO_ERROR : ER_FAILED;
  }


  /*
   * css_transit_ha_server_state - request to transit the current HA server
   *                               state to the required state
   *   return: new state changed if successful or HA_SERVER_STATE_NA
   *   req_state(in): the state for the server to transit
   *
   */
  SERVER_STATE
  transit_server_state (cubthread::entry *thread_p, SERVER_STATE req_state)
  {
    struct ha_server_state_transition_table
    {
      HA_SERVER_STATE cur_state;
      HA_SERVER_STATE req_state;
      HA_SERVER_STATE next_state;
    };
    static struct ha_server_state_transition_table ha_Server_state_transition[] =
    {
      /* idle -> active */
      {HA_SERVER_STATE_IDLE, HA_SERVER_STATE_ACTIVE, HA_SERVER_STATE_ACTIVE},
#if 0
      /* idle -> to-be-standby */
      {HA_SERVER_STATE_IDLE, HA_SERVER_STATE_STANDBY, HA_SERVER_STATE_TO_BE_STANDBY},
#else
      /* idle -> standby */
      {HA_SERVER_STATE_IDLE, HA_SERVER_STATE_STANDBY, HA_SERVER_STATE_STANDBY},
#endif
      /* idle -> maintenance */
      {HA_SERVER_STATE_IDLE, HA_SERVER_STATE_MAINTENANCE, HA_SERVER_STATE_MAINTENANCE},
      /* active -> active */
      {HA_SERVER_STATE_ACTIVE, HA_SERVER_STATE_ACTIVE, HA_SERVER_STATE_ACTIVE},
      /* active -> to-be-standby */
      {HA_SERVER_STATE_ACTIVE, HA_SERVER_STATE_STANDBY, HA_SERVER_STATE_TO_BE_STANDBY},
      /* to-be-active -> active */
      {HA_SERVER_STATE_TO_BE_ACTIVE, HA_SERVER_STATE_ACTIVE, HA_SERVER_STATE_ACTIVE},
      /* standby -> standby */
      {HA_SERVER_STATE_STANDBY, HA_SERVER_STATE_STANDBY, HA_SERVER_STATE_STANDBY},
      /* standby -> to-be-active */
      {HA_SERVER_STATE_STANDBY, HA_SERVER_STATE_ACTIVE, HA_SERVER_STATE_TO_BE_ACTIVE},
      /* statndby -> maintenance */
      {HA_SERVER_STATE_STANDBY, HA_SERVER_STATE_MAINTENANCE, HA_SERVER_STATE_MAINTENANCE},
      /* to-be-standby -> standby */
      {HA_SERVER_STATE_TO_BE_STANDBY, HA_SERVER_STATE_STANDBY, HA_SERVER_STATE_STANDBY},
      /* maintenance -> standby */
      {HA_SERVER_STATE_MAINTENANCE, HA_SERVER_STATE_STANDBY, HA_SERVER_STATE_TO_BE_STANDBY},
      /* end of table */
      {HA_SERVER_STATE_NA, HA_SERVER_STATE_NA, HA_SERVER_STATE_NA}
    };
    struct ha_server_state_transition_table *table;
    HA_SERVER_STATE new_state = HA_SERVER_STATE_NA;

    if (get_server_state () == req_state)
      {
	return req_state;
      }

    csect_enter (thread_p, CSECT_HA_SERVER_STATE, INF_WAIT);

    for (table = ha_Server_state_transition; table->cur_state != HA_SERVER_STATE_NA; table++)
      {
	if (table->cur_state == get_server_state () && table->req_state == req_state)
	  {
	    er_log_debug (ARG_FILE_LINE, "css_transit_ha_server_state: " "ha_Server_state (%s) -> (%s)\n",
			  css_ha_server_state_string (get_server_state ()), css_ha_server_state_string (table->next_state));
	    new_state = table->next_state;
	    /* append a dummy log record for LFT to wake LWTs up */
	    log_append_ha_server_state (thread_p, new_state);
	    if (!HA_DISABLED ())
	      {
		er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_SERVER_HA_MODE_CHANGE, 2,
			css_ha_server_state_string (get_server_state ()), css_ha_server_state_string (new_state));
	      }
	    get_server_state () = new_state;
	    /* sync up the current HA state with the system parameter */
	    prm_set_integer_value (PRM_ID_HA_SERVER_STATE, get_server_state ());

	    if (get_server_state () == HA_SERVER_STATE_ACTIVE)
	      {
		log_set_ha_promotion_time (thread_p, ((INT64) time (0)));
		css_start_all_threads ();
	      }

	    break;
	  }
      }

    csect_exit (thread_p, CSECT_HA_SERVER_STATE);
    return new_state;
  }
  void finish_transit (cubthread::entry *thread_p, bool force, SERVER_STATE req_state)
  {
    assert (req_state == HA_SERVER_STATE_ACTIVE || req_state == HA_SERVER_STATE_STANDBY);

    if (req_state == HA_SERVER_STATE_ACTIVE)
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
	HA_SERVER_STATE state = css_transit_ha_server_state (thread_p, req_state);
	assert (state == req_state);
      }

  }
}

