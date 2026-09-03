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
 * qfile_tuple_layout.h - temporary list file tuple slot & accessor API (CBRD-27365, ADR 0016)
 *
 * Shared by the server (list_file.c, fetch.c, ...), the SA build and the client cursor (cursor.c).
 * PR-1a scope: slot lifetime only. The tuple bytes are still the legacy per-value
 * [flag 4B][len 4B][value] format, so the deform cache fields are reset but not yet consulted.
 */

#ifndef _QFILE_TUPLE_LAYOUT_H_
#define _QFILE_TUPLE_LAYOUT_H_

#include "query_list.h"

/*
 * qfile_slot_bind () - bind the layout descriptor of the list this record will read tuples from.
 *   Called once when the scan/cursor is opened (D-182-6); the descriptor address stays stable for the
 *   life of the scan (late DB_TYPE_VARIABLE domain resolution rewrites its contents in place).
 */
inline void
qfile_slot_bind (QFILE_TUPLE_RECORD * rec, const QFILE_TUPLE_VALUE_TYPE_LIST * tl)
{
  rec->tl = tl;
  rec->nvalid = 0;
  rec->fast_limit = 0;
  rec->off = 0;
}

/*
 * qfile_slot_set_tuple () - point the record at another tuple and reset the deform cache.
 *   The only sanctioned way to change rec->tpl once the record is used as a slot (D-182-5, mutator-owns-reset).
 *   Buffer management (alloc/realloc/free of an owned tpl) still assigns rec->tpl directly; the copy that fills
 *   the buffer must be followed by this call. Ownership (rec->size) is untouched.
 */
inline void
qfile_slot_set_tuple (QFILE_TUPLE_RECORD * rec, char *tpl)
{
  rec->tpl = tpl;
  rec->nvalid = 0;
  rec->off = 0;
}

/*
 * qfile_slot_fill () - bind + set_tuple in one step for the code that FILLS a record from a list
 *   (qfile_retrieve_tuple): the scan that hands a tuple out also binds the record to its own layout descriptor
 *   (filler-owns-bind), so every record a scan fills is a usable slot without a separate bind at the caller.
 *   Records filled outside a scan (a raw page tuple wrapped in a stack slot, a cursor) are bound explicitly.
 */
inline void
qfile_slot_fill (QFILE_TUPLE_RECORD * rec, char *tpl, const QFILE_TUPLE_VALUE_TYPE_LIST * tl)
{
  rec->tl = tl;
  rec->fast_limit = 0;
  qfile_slot_set_tuple (rec, tpl);
}

extern void qfile_slot_clear (QFILE_TUPLE_RECORD * rec);

#endif /* _QFILE_TUPLE_LAYOUT_H_ */
