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
 * network_common.cpp - functions for client/server network support.
 */

#include "network.h"
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

/* client: used to collect histogram, server: used to log an error */
const char *net_server_request_name[NET_SERVER_REQUEST_END] =
{
  "NET_SERVER_REQUEST_START",
#define NET_SERVER_REQUEST_ITEM(name) #name,
  NET_SERVER_REQUEST_LIST
#undef NET_SERVER_REQUEST_ITEM
};

/*
 * get_net_request_name () - get the request name in net_server_request array
 *   return:
 *   request(in): the request index in net_server_request array.
 */
const char *
get_net_request_name (int request)
{
  if (NET_SERVER_REQUEST_START < request && request < NET_SERVER_REQUEST_END)
    {
      /* skip NET_SERVER_ */
      return (net_server_request_name[request] + sizeof ("NET_SERVER_") - 1);
    }
  else if (request == NET_SERVER_PING_WITH_HANDSHAKE)
    {
      return "PING_WITH_HANDSHAKE";
    }
  else
    {
      return "UNKNOWN";
    }
}

/*
 * get_capability_string - for the purpose of error logging,
 *                         it translate cap into a word
 *
 * return:
 */
const char *
get_capability_string (int cap, int cap_type)
{
  switch (cap_type)
    {
    case NET_CAP_INTERRUPT_ENABLED:
      if (cap & NET_CAP_INTERRUPT_ENABLED)
	{
	  return "enabled";
	}
      return "disabled";
    case NET_CAP_UPDATE_DISABLED:
      if (cap & NET_CAP_UPDATE_DISABLED)
	{
	  return "read only";
	}
      return "read/write";
    default:
      return "-";
    }
}
