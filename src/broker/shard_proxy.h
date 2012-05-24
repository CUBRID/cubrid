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
