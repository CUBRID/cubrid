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
 * writeset.hpp - transaction-local writeset collection
 */

#ifndef _WRITESET_HPP_
#define _WRITESET_HPP_

#ident "$Id$"

#include "storage_common.h"	/* OID */
#include "log_lsa.hpp"		/* LOG_LSA */
#include "dbtype.h"		/* DB_VALUE */
#include "writeset_types.hpp"	/* LOG_WSET_KIND */

/* forward declaration of transaction descriptor (defined in log_impl.h) */
typedef struct log_tdes LOG_TDES;

/* forward declaration of value domain (defined in object_domain.h) */
struct tp_domain;

extern int wset_add_write_key (LOG_TDES *tdes, const OID *class_oid, const VFID *index_vfid,
			       DB_VALUE *key);
extern int wset_add_ref_key (LOG_TDES *tdes, const OID *ref_class_oid, const VFID *index_vfid,
			     DB_VALUE *fk_value, struct tp_domain *parent_pk_domain);
extern void wset_switch_to_commit_order (LOG_TDES *tdes, WSET_FALLBACK_REASON reason);

/* A system operation that replicated rows (serial update, INCR/DECR, checksumdb) is labeled for commit
 * order at its SYSOP_END and the keys it collected are dropped unpublished. */
extern void wset_discard_sysop_keys (LOG_TDES *tdes, size_t sysop_key_begin);
extern void wset_discard_keys_after_lsa (LOG_TDES *tdes, const LOG_LSA *start_lsa);

#endif /* _WRITESET_HPP_ */
