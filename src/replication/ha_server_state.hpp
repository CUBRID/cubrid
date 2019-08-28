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

#ifndef _HA_SERVER_STATE_HPP_
#define _HA_SERVER_STATE_HPP_

namespace ha_operations
{
  enum SERVER_STATE
  {
    SERVER_STATE_NA = -1,	/* N/A */
    SERVER_STATE_IDLE = 0,	/* initial state */
    SERVER_STATE_ACTIVE = 1,
    SERVER_STATE_TO_BE_ACTIVE = 2,
    SERVER_STATE_STANDBY = 3,
    SERVER_STATE_TO_BE_STANDBY = 4,
    SERVER_STATE_MAINTENANCE = 5,	/* maintenance mode */
    SERVER_STATE_DEAD = 6	/* server is dead - virtual state; not exists */
  };

  SERVER_STATE &get_server_state ();
  const char *server_state_string (SERVER_STATE state);
}

using HA_SERVER_STATE = ha_operations::SERVER_STATE;

extern const char *HA_SERVER_STATE_IDLE_STR;
extern const char *HA_SERVER_STATE_ACTIVE_STR;
extern const char *HA_SERVER_STATE_TO_BE_ACTIVE_STR;
extern const char *HA_SERVER_STATE_STANDBY_STR;
extern const char *HA_SERVER_STATE_TO_BE_STANDBY_STR;
extern const char *HA_SERVER_STATE_MAINTENANCE_STR;
extern const char *HA_SERVER_STATE_DEAD_STR;

// Cannot add external linkage, extern const causes compile errors when using aliases as switch-cases
// TODO : find a way to make extern const happen or use #defines
constexpr HA_SERVER_STATE HA_SERVER_STATE_NA = ha_operations::SERVER_STATE_NA;
constexpr HA_SERVER_STATE HA_SERVER_STATE_IDLE = ha_operations::SERVER_STATE_IDLE;
constexpr HA_SERVER_STATE HA_SERVER_STATE_ACTIVE = ha_operations::SERVER_STATE_ACTIVE;
constexpr HA_SERVER_STATE HA_SERVER_STATE_TO_BE_ACTIVE = ha_operations::SERVER_STATE_TO_BE_ACTIVE;
constexpr HA_SERVER_STATE HA_SERVER_STATE_STANDBY = ha_operations::SERVER_STATE_STANDBY;
constexpr HA_SERVER_STATE HA_SERVER_STATE_TO_BE_STANDBY = ha_operations::SERVER_STATE_TO_BE_STANDBY;
constexpr HA_SERVER_STATE HA_SERVER_STATE_MAINTENANCE = ha_operations::SERVER_STATE_MAINTENANCE;
constexpr HA_SERVER_STATE HA_SERVER_STATE_DEAD = ha_operations::SERVER_STATE_DEAD;

extern decltype (&ha_operations::get_server_state) css_ha_server_state;
extern decltype (&ha_operations::server_state_string) css_ha_server_state_string;

#endif // !_HA_SERVER_STATE_HPP_
