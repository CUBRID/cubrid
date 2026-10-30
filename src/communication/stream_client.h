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

/*
 * stream_client.h - Client half of a stream kind's registration.
 *
 * The server cannot tell a driver's open from a statement's, and CUBRID's
 * privilege checks run in the client library, so who may open a kind is
 * answered here: the CAS asks before it forwards a driver's INIT or READ_OPEN,
 * and every READ_OPEN this library sends runs the kind's authorize first. Like
 * every other privilege check in CUBRID, this holds for every path through the
 * client library, not for a raw client that builds server requests itself.
 */

#ifndef _STREAM_CLIENT_H_
#define _STREAM_CLIENT_H_

/* NO_ERROR if the current user may open this config, an error code (set) otherwise. It decodes the config itself. */
typedef int (*stream_authorize_fn) (const char *config, int config_len);

struct stream_kind_client_desc
{
  bool driver_openable;		/* CAS_FC_STREAM_INIT / _READ_OPEN may name this kind */
  stream_authorize_fn authorize;	/* required when driver_openable, and for every download kind */
};

/* Refused, leaving the kind unregistered, when driver_openable comes without authorize. */
extern int stream_kind_client_register (int kind, const stream_kind_client_desc & desc);

/* May a driver open this kind? Checked by the CAS before it forwards INIT or READ_OPEN. */
extern int stream_kind_client_check_driver_open (int kind);

/* Run the kind's authorize; a kind with none is refused, since the question was never answered. */
extern int stream_kind_client_authorize (int kind, const char *config, int config_len);

#endif /* _STREAM_CLIENT_H_ */
