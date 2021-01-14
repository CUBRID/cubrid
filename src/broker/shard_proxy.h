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
 * shard_proxy.h -
 */

#ifndef _SHARD_PROXY_H_
#define _SHARD_PROXY_H_

#ident "$Id$"

#include "shard_proxy_io.h"
#include "broker_env_def.h"
#include "broker_shm.h"
#include "shard_metadata.h"
#include "shard_shm.h"
#include "cas_protocol.h"
#include "shard_proxy_handler.h"
#include "shard_proxy_function.h"
#include "shard_statement.h"
#include "shard_parser.h"
#include "shard_key_func.h"

#define DFLT_SOCKBUF	16384

extern void proxy_term (void);

#endif /* _SHARD_PROXY_H_ */
