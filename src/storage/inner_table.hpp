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
 * inner_table.hpp - Inner Table POC: large VARCHAR values stored as rows of a companion chunk class
 *
 * A table T is opted in when the parameter inner_table_threshold is positive and a class named T__inner exists with a
 * BIGINT key attribute, a BIT VARYING chunk attribute and a unique index on the key. A VARCHAR or VARBIT value of T whose disk
 * encoding has at least the threshold bytes is split into chunk rows. The key of chunk ORD of value VALUE_ID is
 * (VALUE_ID << INNER_TABLE_ORD_BITS) | ORD. The parent record keeps a reference in the value's place.
 *
 * The chunk rows are written and MVCC-deleted by the same transaction and statement as the parent row, so rollback,
 * savepoints, restart undo and vacuum treat them as ordinary rows. Every record fetched from the heap of an opted-in
 * class is expanded before it leaves the heap layer, so decoders, index-key builders and clients only see ordinary
 * complete records.
 */

#ifndef _INNER_TABLE_HPP_
#define _INNER_TABLE_HPP_

#if !defined (SERVER_MODE) && !defined (SA_MODE)
#error Belongs to server module
#endif /* !defined (SERVER_MODE) && !defined (SA_MODE) */

#include "storage_common.h"
#include "thread_compat.hpp"

struct heap_scancache;
struct mvcc_snapshot;

/* The low bits of a chunk key hold the chunk ordinal. */
#define INNER_TABLE_ORD_BITS 20

typedef struct inner_table_refs INNER_TABLE_REFS;

extern bool inner_table_enabled (void);
extern bool inner_table_is_opted_in (THREAD_ENTRY *thread_p, const OID *class_oid);

extern int inner_table_rewrite (THREAD_ENTRY *thread_p, const OID *class_oid, RECDES *recdes,
				INNER_TABLE_REFS *old_refs, const ATTR_ID *att_id, int n_att_id, RECDES *out,
				bool *rewritten);
extern void inner_table_free_record (RECDES *rec);

extern int inner_table_collect_refs (THREAD_ENTRY *thread_p, const OID *class_oid, const OID *oid,
				     INNER_TABLE_REFS **refs);
extern int inner_table_retire_refs (THREAD_ENTRY *thread_p, INNER_TABLE_REFS *refs);
extern void inner_table_free_refs (INNER_TABLE_REFS *refs);

extern SCAN_CODE inner_table_expand (THREAD_ENTRY *thread_p, const OID *class_oid, RECDES *recdes,
				     struct heap_scancache *scan_cache, int ispeeking, struct mvcc_snapshot *snapshot);

extern void inner_table_forget_classes (void);

#endif /* _INNER_TABLE_HPP_ */
