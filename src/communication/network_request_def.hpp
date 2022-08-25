/*
 *
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

#ifndef _NETWORK_REQUEST_DEF_HPP_
#define _NETWORK_REQUEST_DEF_HPP_

#include "thread_compat.hpp" /* THREAD_ENTRY */

#if !defined (SERVER_MODE) && !defined (SA_MODE)
#error Belongs to server module
#endif /* !defined (SERVER_MODE) && !defined (SA_MODE) */

enum net_req_act
{
  CHECK_DB_MODIFICATION = 0x0001,
  CHECK_AUTHORIZATION = 0x0002,
  SET_DIAGNOSTICS_INFO = 0x0004,
  IN_TRANSACTION = 0x0008,
  OUT_TRANSACTION = 0x0010,
};

/* net_request struct */
typedef void (*net_server_func) (THREAD_ENTRY *thrd, unsigned int rid, char *request, int reqlen);
struct net_request
{
  int action_attribute; /* bitmask, see net_req_act */
  net_server_func processing_function;

  /* use get_net_request_name () */
  // const char *name;

  net_request () = default;
};

#endif /* _NETWORK_REQUEST_DEF_HPP_ */