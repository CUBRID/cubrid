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
 * shard_key_func.h -
 */

#ifndef	_SHARD_KEY_FUNC_H_
#define	_SHARD_KEY_FUNC_H_

#ident "$Id$"

#include "shard_key.h"
#include "shard_parser.h"


extern int register_fn_get_shard_key (void);

extern int fn_get_shard_key_default (const char *shard_key, T_SHARD_U_TYPE type, const void *value, int value_len);
extern int proxy_find_shard_id_by_hint_value (SP_VALUE * value_p, const char *key_column);


#endif /* _SHARD_KEY_FUNC_H_ */
