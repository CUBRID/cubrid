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

#include <stdio.h>

#define API_ACTIVE_CHECKS
#define _DBTYPE_FUNCTION_SELF_	/* suppress int-typed alias decl */

#include "db.h"			// must be before dbtype_function.h for bool definition
#include "dbtype_function.h"

#include "db_set.h"
#include "error_manager.h"
#include "elo.h"
#include "intl_support.h"
#include "language_support.h"
#include "memory_alloc.h"
#include "oid.h"
#include "set_object.h"
#include "system_parameter.h"

// hidden functions (suppress -Wmissing-prototypes and -Wimplicit-function-declaration)
/* C linkage — the definitions (db_macro.c, set_object.c) declare these
 * extern "C" via their headers; this TU now also builds as C++ in SERVER_MODE */
#ifdef __cplusplus
extern "C"
{
#endif
  int db_make_db_char (DB_VALUE * value, const INTL_CODESET codeset, const int collation_id, const char *str,
		       const int size);
  DB_TYPE setobj_type (struct setobj *set);
  /* _DBTYPE_FUNCTION_SELF_ suppressed the header's int-typed alias, so the
   * definition in dbtype_function.i needs this same-typed previous prototype
   * (-Werror=missing-prototypes in the C compile) */
  INTL_CODESET db_get_string_codeset (const DB_VALUE * value);
#ifdef __cplusplus
}
#endif

#include "dbtype_function.i"
#ifdef __cplusplus		/* SA/CS compile this TU as plain C (COMPAT_SOURCES_C) */
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"
#endif
