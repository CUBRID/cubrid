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
 * Correlated Scalar Subquery Result Cache.
 */

#ifndef _SQ_CACHE_H_
#define _SQ_CACHE_H_

#include "xasl.h"

#ident "$Id$"

extern int sq_cache_initialize ();
extern int sq_put (xasl_node * xasl, DB_VALUE * result);
extern bool sq_get (xasl_node * xasl, DB_VALUE ** retp);
extern void sq_cache_drop_all ();

#endif /* _SQ_CACHE_H_ */
