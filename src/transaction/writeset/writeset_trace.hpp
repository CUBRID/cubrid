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
 * writeset_trace.hpp - writeset diagnostic trace
 */

#ifndef _WRITESET_TRACE_HPP_
#define _WRITESET_TRACE_HPP_

#ident "$Id$"

#include "storage_common.h"
#include "writeset_types.hpp"

#include "error_manager.h"

typedef struct log_tdes LOG_TDES;

extern bool wset_trace_is_enabled (int level);
extern const char *wset_trace_kind_name (LOG_WSET_KIND kind);
extern void wset_trace_key_source (LOG_TDES *tdes, const char *operation, const char *image,
				   LOG_WSET_KIND kind, const OID *table_oid, const OID *row_oid,
				   const char *table_name, const char *constraint_name, const OID *key_class_oid,
				   const VFID *key_index_vfid);

#define WSET_TRACE(level, ...) \
  do \
    { \
      if (wset_trace_is_enabled (level)) \
	{ \
	  _er_log_debug (ARG_FILE_LINE, "[WSET] " __VA_ARGS__); \
	} \
    } \
  while (0)

#endif /* _WRITESET_TRACE_HPP_ */
