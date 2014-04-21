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
 * shard_key_func.h -
 */

#ifndef	_SHARD_KEY_FUNC_H_
#define	_SHARD_KEY_FUNC_H_

#ident "$Id$"

#include "shard_key.h"
#include "shard_parser.h"


extern int register_fn_get_shard_key (void);

extern int fn_get_shard_key_default (const char *shard_key,
				     T_SHARD_U_TYPE type, const void *value,
				     int value_len);
extern int proxy_find_shard_id_by_hint_value (SP_VALUE * value_p,
					      const char *key_column);


#endif /* _SHARD_KEY_FUNC_H_ */
