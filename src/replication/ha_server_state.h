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

#ifndef _HA_SERVER_STATE_
#define _HA_SERVER_STATE_

enum ha_server_state
{
  HA_SERVER_STATE_NA = -1,	/* N/A */
  HA_SERVER_STATE_IDLE = 0,	/* initial state */
  HA_SERVER_STATE_ACTIVE = 1,
  HA_SERVER_STATE_TO_BE_ACTIVE = 2,
  HA_SERVER_STATE_STANDBY = 3,
  HA_SERVER_STATE_TO_BE_STANDBY = 4,
  HA_SERVER_STATE_MAINTENANCE = 5,	/* maintenance mode */
  HA_SERVER_STATE_DEAD = 6	/* server is dead - virtual state; not exists */
};
typedef enum ha_server_state HA_SERVER_STATE;
#define HA_SERVER_STATE_IDLE_STR                "idle"
#define HA_SERVER_STATE_ACTIVE_STR              "active"
#define HA_SERVER_STATE_TO_BE_ACTIVE_STR        "to-be-active"
#define HA_SERVER_STATE_STANDBY_STR             "standby"
#define HA_SERVER_STATE_TO_BE_STANDBY_STR       "to-be-standby"
#define HA_SERVER_STATE_MAINTENANCE_STR         "maintenance"
#define HA_SERVER_STATE_DEAD_STR                "dead"

#endif // !_HA_SERVER_STATE_
